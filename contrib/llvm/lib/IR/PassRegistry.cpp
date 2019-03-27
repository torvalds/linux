//===- PassRegistry.cpp - Pass Registration Implementation ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PassRegistry, with which passes are registered on
// initialization, and supports the PassManager in dependency resolution.
//
//===----------------------------------------------------------------------===//

#include "llvm/PassRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/PassInfo.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/ManagedStatic.h"
#include <cassert>
#include <memory>
#include <utility>

using namespace llvm;

// FIXME: We use ManagedStatic to erase the pass registrar on shutdown.
// Unfortunately, passes are registered with static ctors, and having
// llvm_shutdown clear this map prevents successful resurrection after
// llvm_shutdown is run.  Ideally we should find a solution so that we don't
// leak the map, AND can still resurrect after shutdown.
static ManagedStatic<PassRegistry> PassRegistryObj;
PassRegistry *PassRegistry::getPassRegistry() {
  return &*PassRegistryObj;
}

//===----------------------------------------------------------------------===//
// Accessors
//

PassRegistry::~PassRegistry() = default;

const PassInfo *PassRegistry::getPassInfo(const void *TI) const {
  sys::SmartScopedReader<true> Guard(Lock);
  MapType::const_iterator I = PassInfoMap.find(TI);
  return I != PassInfoMap.end() ? I->second : nullptr;
}

const PassInfo *PassRegistry::getPassInfo(StringRef Arg) const {
  sys::SmartScopedReader<true> Guard(Lock);
  StringMapType::const_iterator I = PassInfoStringMap.find(Arg);
  return I != PassInfoStringMap.end() ? I->second : nullptr;
}

//===----------------------------------------------------------------------===//
// Pass Registration mechanism
//

void PassRegistry::registerPass(const PassInfo &PI, bool ShouldFree) {
  sys::SmartScopedWriter<true> Guard(Lock);
  bool Inserted =
      PassInfoMap.insert(std::make_pair(PI.getTypeInfo(), &PI)).second;
  assert(Inserted && "Pass registered multiple times!");
  (void)Inserted;
  PassInfoStringMap[PI.getPassArgument()] = &PI;

  // Notify any listeners.
  for (auto *Listener : Listeners)
    Listener->passRegistered(&PI);

  if (ShouldFree)
    ToFree.push_back(std::unique_ptr<const PassInfo>(&PI));
}

void PassRegistry::enumerateWith(PassRegistrationListener *L) {
  sys::SmartScopedReader<true> Guard(Lock);
  for (auto PassInfoPair : PassInfoMap)
    L->passEnumerate(PassInfoPair.second);
}

/// Analysis Group Mechanisms.
void PassRegistry::registerAnalysisGroup(const void *InterfaceID,
                                         const void *PassID,
                                         PassInfo &Registeree, bool isDefault,
                                         bool ShouldFree) {
  PassInfo *InterfaceInfo = const_cast<PassInfo *>(getPassInfo(InterfaceID));
  if (!InterfaceInfo) {
    // First reference to Interface, register it now.
    registerPass(Registeree);
    InterfaceInfo = &Registeree;
  }
  assert(Registeree.isAnalysisGroup() &&
         "Trying to join an analysis group that is a normal pass!");

  if (PassID) {
    PassInfo *ImplementationInfo = const_cast<PassInfo *>(getPassInfo(PassID));
    assert(ImplementationInfo &&
           "Must register pass before adding to AnalysisGroup!");

    sys::SmartScopedWriter<true> Guard(Lock);

    // Make sure we keep track of the fact that the implementation implements
    // the interface.
    ImplementationInfo->addInterfaceImplemented(InterfaceInfo);

    if (isDefault) {
      assert(InterfaceInfo->getNormalCtor() == nullptr &&
             "Default implementation for analysis group already specified!");
      assert(
          ImplementationInfo->getNormalCtor() &&
          "Cannot specify pass as default if it does not have a default ctor");
      InterfaceInfo->setNormalCtor(ImplementationInfo->getNormalCtor());
    }
  }

  if (ShouldFree)
    ToFree.push_back(std::unique_ptr<const PassInfo>(&Registeree));
}

void PassRegistry::addRegistrationListener(PassRegistrationListener *L) {
  sys::SmartScopedWriter<true> Guard(Lock);
  Listeners.push_back(L);
}

void PassRegistry::removeRegistrationListener(PassRegistrationListener *L) {
  sys::SmartScopedWriter<true> Guard(Lock);

  auto I = llvm::find(Listeners, L);
  Listeners.erase(I);
}
