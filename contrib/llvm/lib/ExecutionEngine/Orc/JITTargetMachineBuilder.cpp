//===----- JITTargetMachineBuilder.cpp - Build TargetMachines for JIT -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include "llvm/Support/TargetRegistry.h"

namespace llvm {
namespace orc {

JITTargetMachineBuilder::JITTargetMachineBuilder(Triple TT)
    : TT(std::move(TT)) {
  Options.EmulatedTLS = true;
  Options.ExplicitEmulatedTLS = true;
}

Expected<JITTargetMachineBuilder> JITTargetMachineBuilder::detectHost() {
  // FIXME: getProcessTriple is bogus. It returns the host LLVM was compiled on,
  //        rather than a valid triple for the current process.
  return JITTargetMachineBuilder(Triple(sys::getProcessTriple()));
}

Expected<std::unique_ptr<TargetMachine>>
JITTargetMachineBuilder::createTargetMachine() {

  std::string ErrMsg;
  auto *TheTarget = TargetRegistry::lookupTarget(TT.getTriple(), ErrMsg);
  if (!TheTarget)
    return make_error<StringError>(std::move(ErrMsg), inconvertibleErrorCode());

  auto *TM =
      TheTarget->createTargetMachine(TT.getTriple(), CPU, Features.getString(),
                                     Options, RM, CM, OptLevel, /*JIT*/ true);
  if (!TM)
    return make_error<StringError>("Could not allocate target machine",
                                   inconvertibleErrorCode());

  return std::unique_ptr<TargetMachine>(TM);
}

JITTargetMachineBuilder &JITTargetMachineBuilder::addFeatures(
    const std::vector<std::string> &FeatureVec) {
  for (const auto &F : FeatureVec)
    Features.AddFeature(F);
  return *this;
}

} // End namespace orc.
} // End namespace llvm.
