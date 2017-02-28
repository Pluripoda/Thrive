#include <iostream>
#include <cmath>
#include <algorithm>

#include "engine/component_factory.h"
#include "engine/engine.h"
#include "engine/entity.h"
#include "engine/game_state.h"
#include "engine/entity_filter.h"
#include "engine/serialization.h"

#include "microbe_stage/compound.h"
#include "microbe_stage/compound_registry.h"
#include "microbe_stage/bio_process_registry.h"
#include "microbe_stage/process_system.h"

using namespace thrive;

REGISTER_COMPONENT(ProcessorComponent)

luabind::scope
ProcessorComponent::luaBindings() {
    using namespace luabind;
    return class_<ProcessorComponent, Component>("ProcessorComponent")
        .enum_("ID") [
            value("TYPE_ID", ProcessorComponent::TYPE_ID)
        ]
        .scope [
            def("TYPE_NAME", &ProcessorComponent::TYPE_NAME)
        ]
        .def(constructor<>())
        .def("setCapacity", &ProcessorComponent::setCapacity)
    ;
}


void
ProcessorComponent::load(const StorageContainer& storage)
{
    Component::load(storage);
    StorageContainer processes = storage.get<StorageContainer>("processes");
    for (const std::string& id : processes.keys())
    {
        this->process_capacities[std::atoi(id.c_str())] = processes.get<float>(id);
	}
}

StorageContainer
ProcessorComponent::storage() const
{
	StorageContainer storage = Component::storage();

	StorageContainer processes;
    for (auto entry : this->process_capacities) {
        processes.set<float>(std::to_string(static_cast<int>(entry.first)), entry.second);
    }
    storage.set<StorageContainer>("processes", processes);


	return storage;
}

void
ProcessorComponent::setCapacity(BioProcessId id, float capacity)
{
    this->process_capacities[id] = capacity;
}

REGISTER_COMPONENT(CompoundBagComponent)

luabind::scope
CompoundBagComponent::luaBindings() {
    using namespace luabind;
    return class_<CompoundBagComponent, Component>("CompoundBagComponent")
        .enum_("ID") [
            value("TYPE_ID", CompoundBagComponent::TYPE_ID)
        ]
        .scope [
            def("TYPE_NAME", &CompoundBagComponent::TYPE_NAME)
        ]
        .def(constructor<>())
        .def("setProcessor", &CompoundBagComponent::setProcessor)
        .def("giveCompound", &CompoundBagComponent::giveCompound)
        .def("takeCompound", &CompoundBagComponent::takeCompound)
        .def("getCompoundAmount", &CompoundBagComponent::getCompoundAmount)
        .def("getPrice", &CompoundBagComponent::getPrice)
        .def_readwrite("storageSpace", &CompoundBagComponent::storageSpace)
    ;
}

CompoundBagComponent::CompoundBagComponent() {
    storageSpace = 0;
    storageSpaceOccupied = 0;
    for (CompoundId id : CompoundRegistry::getCompoundList()) {
        compounds[id] = 0;
        prices[id] = 1;
    }
}

void
CompoundBagComponent::load(const StorageContainer& storage)
{
    Component::load(storage);

    StorageContainer compounds = storage.get<StorageContainer>("compounds");
    StorageContainer prices = storage.get<StorageContainer>("prices");

    for (const std::string& id : compounds.keys())
    {
        this->compounds[std::atoi(id.c_str())] = compounds.get<float>(id);
        this->prices[std::atoi(id.c_str())] = prices.get<float>(id);
	}

	this->speciesName = storage.get<std::string>("speciesName");
	this->processor = static_cast<ProcessorComponent*>(Entity(this->speciesName).getComponent(ProcessorComponent::TYPE_ID));
}

StorageContainer
CompoundBagComponent::storage() const
{
    StorageContainer storage = Component::storage();

    StorageContainer compounds;
    for (auto entry : this->compounds) {
        compounds.set<float>(""+entry.first, entry.second);
    }

    StorageContainer prices;
    for (auto entry : this->prices) {
        prices.set<float>(""+entry.first, entry.second);
    }

    storage.set("compounds", std::move(compounds));
    storage.set("prices", std::move(compounds));
    storage.set("speciesName", this->speciesName);

    return storage;
}

void
CompoundBagComponent::setProcessor(ProcessorComponent& processor, const std::string& speciesName) {
    this->processor = &processor;
    this->speciesName = speciesName;
}

// helper methods for integrating compound bags with current, un-refactored, lua microbes
float
CompoundBagComponent::getCompoundAmount(CompoundId id) {
    return compounds[id];
}

void
CompoundBagComponent::giveCompound(CompoundId id, float amt) {
    compounds[id] += amt;
}

float
CompoundBagComponent::takeCompound(CompoundId id, float to_take) {
    float& ref = compounds[id];
    float amt = ref > to_take ? to_take : ref;
    ref -= amt;
    return amt;
}

float
CompoundBagComponent::getPrice(CompoundId compoundId) {
    return prices[compoundId];
}

luabind::scope
ProcessSystem::luaBindings() {
    using namespace luabind;
    return class_<ProcessSystem, System>("ProcessSystem")
        .def(constructor<>())
    ;
}
struct ProcessSystem::Implementation {

    EntityFilter<
        CompoundBagComponent
    > m_entities;

    void update(int);
    void updateAddedEntites(int);
    void updateRemovedEntities(int);

    static constexpr float TIME_SCALING_FACTOR = 1000;
};

ProcessSystem::ProcessSystem()
    : m_impl(new Implementation())
{

}

ProcessSystem::~ProcessSystem()
{

}

void
ProcessSystem::init(GameState* gameState)
{
    System::initNamed("ProcessSystem", gameState);
    m_impl->m_entities.setEntityManager(&gameState->entityManager());
}

void
ProcessSystem::shutdown()
{

}

void
ProcessSystem::Implementation::updateRemovedEntities(int) {
    // std::cerr << logicTime;
    // for (EntityId entityId : this->m_entities.removedEntities()) {
        // std::cerr << &entityId;
    // }
}

void
ProcessSystem::Implementation::updateAddedEntites(int) {
    // std::cerr << logicTime;
    // for (auto& value : this->m_entities.addedEntities()) {
        // std::cerr << &value;
    // }
}


void
ProcessSystem::Implementation::update(int logicTime) {
    //Iterating on each entity with a ProcessorComponent.
    for (auto& value : this->m_entities) {
        CompoundBagComponent* bag = std::get<0>(value.second);
        ProcessorComponent* processor = bag->processor;

        // Avoiding zero-division errors.
        if(bag->storageSpace > 0)
        {
            //Calculating the storage space occupied;
            bag->storageSpaceOccupied = 0;
            for (const auto& compound : bag->compounds) {
                float compoundAmount = compound.second;
                bag->storageSpaceOccupied += compoundAmount;
            }

            //Phase one: setting up the prices.
            for (const auto& compound : bag->compounds) {
                CompoundId compoundId = compound.first;
                float compoundAmount = compound.second;
                bag->prices[compoundId] = 1 / (compoundAmount + 1);
            }

            //Phase two: setting up the processes.
            for (const auto& process : processor->process_capacities) {
                BioProcessId processId = process.first;
                float processCapacity = process.second;

                //The maximum capacity this process could have with the current amount of input compounds.
                float processLimitCapacity = processCapacity * logicTime;

                //Calculating the cost of the process's inputs.
                float cost = 0;
                for (const auto& input : BioProcessRegistry::getInputCompounds(processId)) {
                    CompoundId inputId = input.first;
                    int inputNeeded = input.second;
                    float spaceFreed = inputNeeded * CompoundRegistry::getCompoundUnitVolume(inputId);
                    cost += bag->prices[inputId] * inputNeeded - spaceFreed / bag->storageSpace;

                    //Limiting the process by the amount of this required compound.
                    processLimitCapacity = std::min(processLimitCapacity, bag->compounds[inputId] / inputNeeded);
                }

                //Calculating the revenue generated by the process's outputs.
                float revenue = 0;
                for (const auto& output : BioProcessRegistry::getOutputCompounds(processId)) {
                    CompoundId outputId = output.first;
                    int outputGenerated = output.second;
                    float spaceUsed = outputGenerated * CompoundRegistry::getCompoundUnitVolume(outputId);
                    revenue += bag->prices[outputId] * outputGenerated - spaceUsed / bag->storageSpace;
                }

                //Setting the process capacity rate.
                float rate = 0;
                if(revenue > cost) rate = std::min(processCapacity * logicTime / 1000, processLimitCapacity);

                //Running the process at the specified rate, transforming the inputs...
                for (const auto& input : BioProcessRegistry::getInputCompounds(processId)) {
                    CompoundId inputId = input.first;
                    int inputNeeded = input.second;
                    bag->compounds[inputId] -= rate * inputNeeded;
                }

                //...into the outputs.
                for (const auto& output : BioProcessRegistry::getOutputCompounds(processId)) {
                    CompoundId outputId = output.first;
                    int outputGenerated = output.second;
                    bag->compounds[outputId] += rate * outputGenerated;
                }
            }
        }
    }
}

void
ProcessSystem::update(int, int logicTime)
{
    m_impl->updateRemovedEntities(logicTime);
    m_impl->updateAddedEntites(logicTime);
    m_impl->m_entities.clearChanges();
    m_impl->update(logicTime);
}
