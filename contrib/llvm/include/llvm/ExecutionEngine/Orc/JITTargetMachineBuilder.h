//===- JITTargetMachineBuilder.h - Build TargetMachines for JIT -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A utitily for building TargetMachines for JITs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H
#define LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace orc {

/// A utility class for building TargetMachines for JITs.
class JITTargetMachineBuilder {
public:
  /// Create a JITTargetMachineBuilder based on the given triple.
  ///
  /// Note: TargetOptions is default-constructed, then EmulatedTLS and
  /// ExplicitEmulatedTLS are set to true. If EmulatedTLS is not
  /// required, these values should be reset before calling
  /// createTargetMachine.
  JITTargetMachineBuilder(Triple TT);

  /// Create a JITTargetMachineBuilder for the host system.
  ///
  /// Note: TargetOptions is default-constructed, then EmulatedTLS and
  /// ExplicitEmulatedTLS are set to true. If EmulatedTLS is not
  /// required, these values should be reset before calling
  /// createTargetMachine.
  static Expected<JITTargetMachineBuilder> detectHost();

  /// Create a TargetMachine.
  ///
  /// This operation will fail if the requested target is not registered,
  /// in which case see llvm/Support/TargetSelect.h. To JIT IR the Target and
  /// the target's AsmPrinter must both be registered. To JIT assembly
  /// (including inline and module level assembly) the target's AsmParser must
  /// also be registered.
  Expected<std::unique_ptr<TargetMachine>> createTargetMachine();

  /// Get the default DataLayout for the target.
  ///
  /// Note: This is reasonably expensive, as it creates a temporary
  /// TargetMachine instance under the hood. It is only suitable for use during
  /// JIT setup.
  Expected<DataLayout> getDefaultDataLayoutForTarget() {
    auto TM = createTargetMachine();
    if (!TM)
      return TM.takeError();
    return (*TM)->createDataLayout();
  }

  /// Set the CPU string.
  JITTargetMachineBuilder &setCPU(std::string CPU) {
    this->CPU = std::move(CPU);
    return *this;
  }

  /// Set the relocation model.
  JITTargetMachineBuilder &setRelocationModel(Optional<Reloc::Model> RM) {
    this->RM = std::move(RM);
    return *this;
  }

  /// Set the code model.
  JITTargetMachineBuilder &setCodeModel(Optional<CodeModel::Model> CM) {
    this->CM = std::move(CM);
    return *this;
  }

  /// Set the LLVM CodeGen optimization level.
  JITTargetMachineBuilder &setCodeGenOptLevel(CodeGenOpt::Level OptLevel) {
    this->OptLevel = OptLevel;
    return *this;
  }

  /// Add subtarget features.
  JITTargetMachineBuilder &
  addFeatures(const std::vector<std::string> &FeatureVec);

  /// Access subtarget features.
  SubtargetFeatures &getFeatures() { return Features; }

  /// Access subtarget features.
  const SubtargetFeatures &getFeatures() const { return Features; }

  /// Access TargetOptions.
  TargetOptions &getOptions() { return Options; }

  /// Access TargetOptions.
  const TargetOptions &getOptions() const { return Options; }

  /// Access Triple.
  Triple &getTargetTriple() { return TT; }

  /// Access Triple.
  const Triple &getTargetTriple() const { return TT; }

private:
  Triple TT;
  std::string CPU;
  SubtargetFeatures Features;
  TargetOptions Options;
  Optional<Reloc::Model> RM;
  Optional<CodeModel::Model> CM;
  CodeGenOpt::Level OptLevel = CodeGenOpt::None;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_JITTARGETMACHINEBUILDER_H
