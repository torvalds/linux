//===- Action.cpp - Abstract compilation steps ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Action.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <string>

using namespace clang;
using namespace driver;
using namespace llvm::opt;

Action::~Action() = default;

const char *Action::getClassName(ActionClass AC) {
  switch (AC) {
  case InputClass: return "input";
  case BindArchClass: return "bind-arch";
  case OffloadClass:
    return "offload";
  case PreprocessJobClass: return "preprocessor";
  case PrecompileJobClass: return "precompiler";
  case HeaderModulePrecompileJobClass: return "header-module-precompiler";
  case AnalyzeJobClass: return "analyzer";
  case MigrateJobClass: return "migrator";
  case CompileJobClass: return "compiler";
  case BackendJobClass: return "backend";
  case AssembleJobClass: return "assembler";
  case LinkJobClass: return "linker";
  case LipoJobClass: return "lipo";
  case DsymutilJobClass: return "dsymutil";
  case VerifyDebugInfoJobClass: return "verify-debug-info";
  case VerifyPCHJobClass: return "verify-pch";
  case OffloadBundlingJobClass:
    return "clang-offload-bundler";
  case OffloadUnbundlingJobClass:
    return "clang-offload-unbundler";
  }

  llvm_unreachable("invalid class");
}

void Action::propagateDeviceOffloadInfo(OffloadKind OKind, const char *OArch) {
  // Offload action set its own kinds on their dependences.
  if (Kind == OffloadClass)
    return;
  // Unbundling actions use the host kinds.
  if (Kind == OffloadUnbundlingJobClass)
    return;

  assert((OffloadingDeviceKind == OKind || OffloadingDeviceKind == OFK_None) &&
         "Setting device kind to a different device??");
  assert(!ActiveOffloadKindMask && "Setting a device kind in a host action??");
  OffloadingDeviceKind = OKind;
  OffloadingArch = OArch;

  for (auto *A : Inputs)
    A->propagateDeviceOffloadInfo(OffloadingDeviceKind, OArch);
}

void Action::propagateHostOffloadInfo(unsigned OKinds, const char *OArch) {
  // Offload action set its own kinds on their dependences.
  if (Kind == OffloadClass)
    return;

  assert(OffloadingDeviceKind == OFK_None &&
         "Setting a host kind in a device action.");
  ActiveOffloadKindMask |= OKinds;
  OffloadingArch = OArch;

  for (auto *A : Inputs)
    A->propagateHostOffloadInfo(ActiveOffloadKindMask, OArch);
}

void Action::propagateOffloadInfo(const Action *A) {
  if (unsigned HK = A->getOffloadingHostActiveKinds())
    propagateHostOffloadInfo(HK, A->getOffloadingArch());
  else
    propagateDeviceOffloadInfo(A->getOffloadingDeviceKind(),
                               A->getOffloadingArch());
}

std::string Action::getOffloadingKindPrefix() const {
  switch (OffloadingDeviceKind) {
  case OFK_None:
    break;
  case OFK_Host:
    llvm_unreachable("Host kind is not an offloading device kind.");
    break;
  case OFK_Cuda:
    return "device-cuda";
  case OFK_OpenMP:
    return "device-openmp";
  case OFK_HIP:
    return "device-hip";

    // TODO: Add other programming models here.
  }

  if (!ActiveOffloadKindMask)
    return {};

  std::string Res("host");
  assert(!((ActiveOffloadKindMask & OFK_Cuda) &&
           (ActiveOffloadKindMask & OFK_HIP)) &&
         "Cannot offload CUDA and HIP at the same time");
  if (ActiveOffloadKindMask & OFK_Cuda)
    Res += "-cuda";
  if (ActiveOffloadKindMask & OFK_HIP)
    Res += "-hip";
  if (ActiveOffloadKindMask & OFK_OpenMP)
    Res += "-openmp";

  // TODO: Add other programming models here.

  return Res;
}

/// Return a string that can be used as prefix in order to generate unique files
/// for each offloading kind.
std::string
Action::GetOffloadingFileNamePrefix(OffloadKind Kind,
                                    StringRef NormalizedTriple,
                                    bool CreatePrefixForHost) {
  // Don't generate prefix for host actions unless required.
  if (!CreatePrefixForHost && (Kind == OFK_None || Kind == OFK_Host))
    return {};

  std::string Res("-");
  Res += GetOffloadKindName(Kind);
  Res += "-";
  Res += NormalizedTriple;
  return Res;
}

/// Return a string with the offload kind name. If that is not defined, we
/// assume 'host'.
StringRef Action::GetOffloadKindName(OffloadKind Kind) {
  switch (Kind) {
  case OFK_None:
  case OFK_Host:
    return "host";
  case OFK_Cuda:
    return "cuda";
  case OFK_OpenMP:
    return "openmp";
  case OFK_HIP:
    return "hip";

    // TODO: Add other programming models here.
  }

  llvm_unreachable("invalid offload kind");
}

void InputAction::anchor() {}

InputAction::InputAction(const Arg &_Input, types::ID _Type)
    : Action(InputClass, _Type), Input(_Input) {}

void BindArchAction::anchor() {}

BindArchAction::BindArchAction(Action *Input, StringRef ArchName)
    : Action(BindArchClass, Input), ArchName(ArchName) {}

void OffloadAction::anchor() {}

OffloadAction::OffloadAction(const HostDependence &HDep)
    : Action(OffloadClass, HDep.getAction()), HostTC(HDep.getToolChain()) {
  OffloadingArch = HDep.getBoundArch();
  ActiveOffloadKindMask = HDep.getOffloadKinds();
  HDep.getAction()->propagateHostOffloadInfo(HDep.getOffloadKinds(),
                                             HDep.getBoundArch());
}

OffloadAction::OffloadAction(const DeviceDependences &DDeps, types::ID Ty)
    : Action(OffloadClass, DDeps.getActions(), Ty),
      DevToolChains(DDeps.getToolChains()) {
  auto &OKinds = DDeps.getOffloadKinds();
  auto &BArchs = DDeps.getBoundArchs();

  // If all inputs agree on the same kind, use it also for this action.
  if (llvm::all_of(OKinds, [&](OffloadKind K) { return K == OKinds.front(); }))
    OffloadingDeviceKind = OKinds.front();

  // If we have a single dependency, inherit the architecture from it.
  if (OKinds.size() == 1)
    OffloadingArch = BArchs.front();

  // Propagate info to the dependencies.
  for (unsigned i = 0, e = getInputs().size(); i != e; ++i)
    getInputs()[i]->propagateDeviceOffloadInfo(OKinds[i], BArchs[i]);
}

OffloadAction::OffloadAction(const HostDependence &HDep,
                             const DeviceDependences &DDeps)
    : Action(OffloadClass, HDep.getAction()), HostTC(HDep.getToolChain()),
      DevToolChains(DDeps.getToolChains()) {
  // We use the kinds of the host dependence for this action.
  OffloadingArch = HDep.getBoundArch();
  ActiveOffloadKindMask = HDep.getOffloadKinds();
  HDep.getAction()->propagateHostOffloadInfo(HDep.getOffloadKinds(),
                                             HDep.getBoundArch());

  // Add device inputs and propagate info to the device actions. Do work only if
  // we have dependencies.
  for (unsigned i = 0, e = DDeps.getActions().size(); i != e; ++i)
    if (auto *A = DDeps.getActions()[i]) {
      getInputs().push_back(A);
      A->propagateDeviceOffloadInfo(DDeps.getOffloadKinds()[i],
                                    DDeps.getBoundArchs()[i]);
    }
}

void OffloadAction::doOnHostDependence(const OffloadActionWorkTy &Work) const {
  if (!HostTC)
    return;
  assert(!getInputs().empty() && "No dependencies for offload action??");
  auto *A = getInputs().front();
  Work(A, HostTC, A->getOffloadingArch());
}

void OffloadAction::doOnEachDeviceDependence(
    const OffloadActionWorkTy &Work) const {
  auto I = getInputs().begin();
  auto E = getInputs().end();
  if (I == E)
    return;

  // We expect to have the same number of input dependences and device tool
  // chains, except if we also have a host dependence. In that case we have one
  // more dependence than we have device tool chains.
  assert(getInputs().size() == DevToolChains.size() + (HostTC ? 1 : 0) &&
         "Sizes of action dependences and toolchains are not consistent!");

  // Skip host action
  if (HostTC)
    ++I;

  auto TI = DevToolChains.begin();
  for (; I != E; ++I, ++TI)
    Work(*I, *TI, (*I)->getOffloadingArch());
}

void OffloadAction::doOnEachDependence(const OffloadActionWorkTy &Work) const {
  doOnHostDependence(Work);
  doOnEachDeviceDependence(Work);
}

void OffloadAction::doOnEachDependence(bool IsHostDependence,
                                       const OffloadActionWorkTy &Work) const {
  if (IsHostDependence)
    doOnHostDependence(Work);
  else
    doOnEachDeviceDependence(Work);
}

bool OffloadAction::hasHostDependence() const { return HostTC != nullptr; }

Action *OffloadAction::getHostDependence() const {
  assert(hasHostDependence() && "Host dependence does not exist!");
  assert(!getInputs().empty() && "No dependencies for offload action??");
  return HostTC ? getInputs().front() : nullptr;
}

bool OffloadAction::hasSingleDeviceDependence(
    bool DoNotConsiderHostActions) const {
  if (DoNotConsiderHostActions)
    return getInputs().size() == (HostTC ? 2 : 1);
  return !HostTC && getInputs().size() == 1;
}

Action *
OffloadAction::getSingleDeviceDependence(bool DoNotConsiderHostActions) const {
  assert(hasSingleDeviceDependence(DoNotConsiderHostActions) &&
         "Single device dependence does not exist!");
  // The previous assert ensures the number of entries in getInputs() is
  // consistent with what we are doing here.
  return HostTC ? getInputs()[1] : getInputs().front();
}

void OffloadAction::DeviceDependences::add(Action &A, const ToolChain &TC,
                                           const char *BoundArch,
                                           OffloadKind OKind) {
  DeviceActions.push_back(&A);
  DeviceToolChains.push_back(&TC);
  DeviceBoundArchs.push_back(BoundArch);
  DeviceOffloadKinds.push_back(OKind);
}

OffloadAction::HostDependence::HostDependence(Action &A, const ToolChain &TC,
                                              const char *BoundArch,
                                              const DeviceDependences &DDeps)
    : HostAction(A), HostToolChain(TC), HostBoundArch(BoundArch) {
  for (auto K : DDeps.getOffloadKinds())
    HostOffloadKinds |= K;
}

void JobAction::anchor() {}

JobAction::JobAction(ActionClass Kind, Action *Input, types::ID Type)
    : Action(Kind, Input, Type) {}

JobAction::JobAction(ActionClass Kind, const ActionList &Inputs, types::ID Type)
    : Action(Kind, Inputs, Type) {}

void PreprocessJobAction::anchor() {}

PreprocessJobAction::PreprocessJobAction(Action *Input, types::ID OutputType)
    : JobAction(PreprocessJobClass, Input, OutputType) {}

void PrecompileJobAction::anchor() {}

PrecompileJobAction::PrecompileJobAction(Action *Input, types::ID OutputType)
    : JobAction(PrecompileJobClass, Input, OutputType) {}

PrecompileJobAction::PrecompileJobAction(ActionClass Kind, Action *Input,
                                         types::ID OutputType)
    : JobAction(Kind, Input, OutputType) {
  assert(isa<PrecompileJobAction>((Action*)this) && "invalid action kind");
}

void HeaderModulePrecompileJobAction::anchor() {}

HeaderModulePrecompileJobAction::HeaderModulePrecompileJobAction(
    Action *Input, types::ID OutputType, const char *ModuleName)
    : PrecompileJobAction(HeaderModulePrecompileJobClass, Input, OutputType),
      ModuleName(ModuleName) {}

void AnalyzeJobAction::anchor() {}

AnalyzeJobAction::AnalyzeJobAction(Action *Input, types::ID OutputType)
    : JobAction(AnalyzeJobClass, Input, OutputType) {}

void MigrateJobAction::anchor() {}

MigrateJobAction::MigrateJobAction(Action *Input, types::ID OutputType)
    : JobAction(MigrateJobClass, Input, OutputType) {}

void CompileJobAction::anchor() {}

CompileJobAction::CompileJobAction(Action *Input, types::ID OutputType)
    : JobAction(CompileJobClass, Input, OutputType) {}

void BackendJobAction::anchor() {}

BackendJobAction::BackendJobAction(Action *Input, types::ID OutputType)
    : JobAction(BackendJobClass, Input, OutputType) {}

void AssembleJobAction::anchor() {}

AssembleJobAction::AssembleJobAction(Action *Input, types::ID OutputType)
    : JobAction(AssembleJobClass, Input, OutputType) {}

void LinkJobAction::anchor() {}

LinkJobAction::LinkJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(LinkJobClass, Inputs, Type) {}

void LipoJobAction::anchor() {}

LipoJobAction::LipoJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(LipoJobClass, Inputs, Type) {}

void DsymutilJobAction::anchor() {}

DsymutilJobAction::DsymutilJobAction(ActionList &Inputs, types::ID Type)
    : JobAction(DsymutilJobClass, Inputs, Type) {}

void VerifyJobAction::anchor() {}

VerifyJobAction::VerifyJobAction(ActionClass Kind, Action *Input,
                                 types::ID Type)
    : JobAction(Kind, Input, Type) {
  assert((Kind == VerifyDebugInfoJobClass || Kind == VerifyPCHJobClass) &&
         "ActionClass is not a valid VerifyJobAction");
}

void VerifyDebugInfoJobAction::anchor() {}

VerifyDebugInfoJobAction::VerifyDebugInfoJobAction(Action *Input,
                                                   types::ID Type)
    : VerifyJobAction(VerifyDebugInfoJobClass, Input, Type) {}

void VerifyPCHJobAction::anchor() {}

VerifyPCHJobAction::VerifyPCHJobAction(Action *Input, types::ID Type)
    : VerifyJobAction(VerifyPCHJobClass, Input, Type) {}

void OffloadBundlingJobAction::anchor() {}

OffloadBundlingJobAction::OffloadBundlingJobAction(ActionList &Inputs)
    : JobAction(OffloadBundlingJobClass, Inputs, Inputs.back()->getType()) {}

void OffloadUnbundlingJobAction::anchor() {}

OffloadUnbundlingJobAction::OffloadUnbundlingJobAction(Action *Input)
    : JobAction(OffloadUnbundlingJobClass, Input, Input->getType()) {}
