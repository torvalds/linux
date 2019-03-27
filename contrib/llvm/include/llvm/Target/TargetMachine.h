//===-- llvm/Target/TargetMachine.h - Target Information --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TargetMachine and LLVMTargetMachine classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETMACHINE_H
#define LLVM_TARGET_TARGETMACHINE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"
#include <string>

namespace llvm {

class Function;
class GlobalValue;
class MachineModuleInfo;
class Mangler;
class MCAsmInfo;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCSymbol;
class raw_pwrite_stream;
class PassManagerBuilder;
class Target;
class TargetIntrinsicInfo;
class TargetIRAnalysis;
class TargetTransformInfo;
class TargetLoweringObjectFile;
class TargetPassConfig;
class TargetSubtargetInfo;

// The old pass manager infrastructure is hidden in a legacy namespace now.
namespace legacy {
class PassManagerBase;
}
using legacy::PassManagerBase;

//===----------------------------------------------------------------------===//
///
/// Primary interface to the complete machine description for the target
/// machine.  All target-specific information should be accessible through this
/// interface.
///
class TargetMachine {
protected: // Can only create subclasses.
  TargetMachine(const Target &T, StringRef DataLayoutString,
                const Triple &TargetTriple, StringRef CPU, StringRef FS,
                const TargetOptions &Options);

  /// The Target that this machine was created for.
  const Target &TheTarget;

  /// DataLayout for the target: keep ABI type size and alignment.
  ///
  /// The DataLayout is created based on the string representation provided
  /// during construction. It is kept here only to avoid reparsing the string
  /// but should not really be used during compilation, because it has an
  /// internal cache that is context specific.
  const DataLayout DL;

  /// Triple string, CPU name, and target feature strings the TargetMachine
  /// instance is created with.
  Triple TargetTriple;
  std::string TargetCPU;
  std::string TargetFS;

  Reloc::Model RM = Reloc::Static;
  CodeModel::Model CMModel = CodeModel::Small;
  CodeGenOpt::Level OptLevel = CodeGenOpt::Default;

  /// Contains target specific asm information.
  std::unique_ptr<const MCAsmInfo> AsmInfo;
  std::unique_ptr<const MCRegisterInfo> MRI;
  std::unique_ptr<const MCInstrInfo> MII;
  std::unique_ptr<const MCSubtargetInfo> STI;

  unsigned RequireStructuredCFG : 1;
  unsigned O0WantsFastISel : 1;

public:
  const TargetOptions DefaultOptions;
  mutable TargetOptions Options;

  TargetMachine(const TargetMachine &) = delete;
  void operator=(const TargetMachine &) = delete;
  virtual ~TargetMachine();

  const Target &getTarget() const { return TheTarget; }

  const Triple &getTargetTriple() const { return TargetTriple; }
  StringRef getTargetCPU() const { return TargetCPU; }
  StringRef getTargetFeatureString() const { return TargetFS; }

  /// Virtual method implemented by subclasses that returns a reference to that
  /// target's TargetSubtargetInfo-derived member variable.
  virtual const TargetSubtargetInfo *getSubtargetImpl(const Function &) const {
    return nullptr;
  }
  virtual TargetLoweringObjectFile *getObjFileLowering() const {
    return nullptr;
  }

  /// This method returns a pointer to the specified type of
  /// TargetSubtargetInfo.  In debug builds, it verifies that the object being
  /// returned is of the correct type.
  template <typename STC> const STC &getSubtarget(const Function &F) const {
    return *static_cast<const STC*>(getSubtargetImpl(F));
  }

  /// Create a DataLayout.
  const DataLayout createDataLayout() const { return DL; }

  /// Test if a DataLayout if compatible with the CodeGen for this target.
  ///
  /// The LLVM Module owns a DataLayout that is used for the target independent
  /// optimizations and code generation. This hook provides a target specific
  /// check on the validity of this DataLayout.
  bool isCompatibleDataLayout(const DataLayout &Candidate) const {
    return DL == Candidate;
  }

  /// Get the pointer size for this target.
  ///
  /// This is the only time the DataLayout in the TargetMachine is used.
  unsigned getPointerSize(unsigned AS) const {
    return DL.getPointerSize(AS);
  }

  unsigned getPointerSizeInBits(unsigned AS) const {
    return DL.getPointerSizeInBits(AS);
  }

  unsigned getProgramPointerSize() const {
    return DL.getPointerSize(DL.getProgramAddressSpace());
  }

  unsigned getAllocaPointerSize() const {
    return DL.getPointerSize(DL.getAllocaAddrSpace());
  }

  /// Reset the target options based on the function's attributes.
  // FIXME: Remove TargetOptions that affect per-function code generation
  // from TargetMachine.
  void resetTargetOptions(const Function &F) const;

  /// Return target specific asm information.
  const MCAsmInfo *getMCAsmInfo() const { return AsmInfo.get(); }

  const MCRegisterInfo *getMCRegisterInfo() const { return MRI.get(); }
  const MCInstrInfo *getMCInstrInfo() const { return MII.get(); }
  const MCSubtargetInfo *getMCSubtargetInfo() const { return STI.get(); }

  /// If intrinsic information is available, return it.  If not, return null.
  virtual const TargetIntrinsicInfo *getIntrinsicInfo() const {
    return nullptr;
  }

  bool requiresStructuredCFG() const { return RequireStructuredCFG; }
  void setRequiresStructuredCFG(bool Value) { RequireStructuredCFG = Value; }

  /// Returns the code generation relocation model. The choices are static, PIC,
  /// and dynamic-no-pic, and target default.
  Reloc::Model getRelocationModel() const;

  /// Returns the code model. The choices are small, kernel, medium, large, and
  /// target default.
  CodeModel::Model getCodeModel() const;

  bool isPositionIndependent() const;

  bool shouldAssumeDSOLocal(const Module &M, const GlobalValue *GV) const;

  /// Returns true if this target uses emulated TLS.
  bool useEmulatedTLS() const;

  /// Returns the TLS model which should be used for the given global variable.
  TLSModel::Model getTLSModel(const GlobalValue *GV) const;

  /// Returns the optimization level: None, Less, Default, or Aggressive.
  CodeGenOpt::Level getOptLevel() const;

  /// Overrides the optimization level.
  void setOptLevel(CodeGenOpt::Level Level);

  void setFastISel(bool Enable) { Options.EnableFastISel = Enable; }
  bool getO0WantsFastISel() { return O0WantsFastISel; }
  void setO0WantsFastISel(bool Enable) { O0WantsFastISel = Enable; }
  void setGlobalISel(bool Enable) { Options.EnableGlobalISel = Enable; }
  void setGlobalISelAbort(GlobalISelAbortMode Mode) {
    Options.GlobalISelAbort = Mode;
  }
  void setMachineOutliner(bool Enable) {
    Options.EnableMachineOutliner = Enable;
  }
  void setSupportsDefaultOutlining(bool Enable) {
    Options.SupportsDefaultOutlining = Enable;
  }

  bool shouldPrintMachineCode() const { return Options.PrintMachineCode; }

  bool getUniqueSectionNames() const { return Options.UniqueSectionNames; }

  /// Return true if data objects should be emitted into their own section,
  /// corresponds to -fdata-sections.
  bool getDataSections() const {
    return Options.DataSections;
  }

  /// Return true if functions should be emitted into their own section,
  /// corresponding to -ffunction-sections.
  bool getFunctionSections() const {
    return Options.FunctionSections;
  }

  /// Get a \c TargetIRAnalysis appropriate for the target.
  ///
  /// This is used to construct the new pass manager's target IR analysis pass,
  /// set up appropriately for this target machine. Even the old pass manager
  /// uses this to answer queries about the IR.
  TargetIRAnalysis getTargetIRAnalysis();

  /// Return a TargetTransformInfo for a given function.
  ///
  /// The returned TargetTransformInfo is specialized to the subtarget
  /// corresponding to \p F.
  virtual TargetTransformInfo getTargetTransformInfo(const Function &F);

  /// Allow the target to modify the pass manager, e.g. by calling
  /// PassManagerBuilder::addExtension.
  virtual void adjustPassManager(PassManagerBuilder &) {}

  /// These enums are meant to be passed into addPassesToEmitFile to indicate
  /// what type of file to emit, and returned by it to indicate what type of
  /// file could actually be made.
  enum CodeGenFileType {
    CGFT_AssemblyFile,
    CGFT_ObjectFile,
    CGFT_Null         // Do not emit any output.
  };

  /// Add passes to the specified pass manager to get the specified file
  /// emitted.  Typically this will involve several steps of code generation.
  /// This method should return true if emission of this file type is not
  /// supported, or false on success.
  /// \p MMI is an optional parameter that, if set to non-nullptr,
  /// will be used to set the MachineModuloInfo for this PM.
  virtual bool addPassesToEmitFile(PassManagerBase &, raw_pwrite_stream &,
                                   raw_pwrite_stream *, CodeGenFileType,
                                   bool /*DisableVerify*/ = true,
                                   MachineModuleInfo *MMI = nullptr) {
    return true;
  }

  /// Add passes to the specified pass manager to get machine code emitted with
  /// the MCJIT. This method returns true if machine code is not supported. It
  /// fills the MCContext Ctx pointer which can be used to build custom
  /// MCStreamer.
  ///
  virtual bool addPassesToEmitMC(PassManagerBase &, MCContext *&,
                                 raw_pwrite_stream &,
                                 bool /*DisableVerify*/ = true) {
    return true;
  }

  /// True if subtarget inserts the final scheduling pass on its own.
  ///
  /// Branch relaxation, which must happen after block placement, can
  /// on some targets (e.g. SystemZ) expose additional post-RA
  /// scheduling opportunities.
  virtual bool targetSchedulesPostRAScheduling() const { return false; };

  void getNameWithPrefix(SmallVectorImpl<char> &Name, const GlobalValue *GV,
                         Mangler &Mang, bool MayAlwaysUsePrivate = false) const;
  MCSymbol *getSymbol(const GlobalValue *GV) const;
};

/// This class describes a target machine that is implemented with the LLVM
/// target-independent code generator.
///
class LLVMTargetMachine : public TargetMachine {
protected: // Can only create subclasses.
  LLVMTargetMachine(const Target &T, StringRef DataLayoutString,
                    const Triple &TT, StringRef CPU, StringRef FS,
                    const TargetOptions &Options, Reloc::Model RM,
                    CodeModel::Model CM, CodeGenOpt::Level OL);

  void initAsmInfo();

public:
  /// Get a TargetTransformInfo implementation for the target.
  ///
  /// The TTI returned uses the common code generator to answer queries about
  /// the IR.
  TargetTransformInfo getTargetTransformInfo(const Function &F) override;

  /// Create a pass configuration object to be used by addPassToEmitX methods
  /// for generating a pipeline of CodeGen passes.
  virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);

  /// Add passes to the specified pass manager to get the specified file
  /// emitted.  Typically this will involve several steps of code generation.
  /// \p MMI is an optional parameter that, if set to non-nullptr,
  /// will be used to set the MachineModuloInfofor this PM.
  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                           raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                           bool DisableVerify = true,
                           MachineModuleInfo *MMI = nullptr) override;

  /// Add passes to the specified pass manager to get machine code emitted with
  /// the MCJIT. This method returns true if machine code is not supported. It
  /// fills the MCContext Ctx pointer which can be used to build custom
  /// MCStreamer.
  bool addPassesToEmitMC(PassManagerBase &PM, MCContext *&Ctx,
                         raw_pwrite_stream &Out,
                         bool DisableVerify = true) override;

  /// Returns true if the target is expected to pass all machine verifier
  /// checks. This is a stopgap measure to fix targets one by one. We will
  /// remove this at some point and always enable the verifier when
  /// EXPENSIVE_CHECKS is enabled.
  virtual bool isMachineVerifierClean() const { return true; }

  /// Adds an AsmPrinter pass to the pipeline that prints assembly or
  /// machine code from the MI representation.
  bool addAsmPrinter(PassManagerBase &PM, raw_pwrite_stream &Out,
                     raw_pwrite_stream *DwoOut, CodeGenFileType FileTYpe,
                     MCContext &Context);

  /// True if the target uses physical regs at Prolog/Epilog insertion
  /// time. If true (most machines), all vregs must be allocated before
  /// PEI. If false (virtual-register machines), then callee-save register
  /// spilling and scavenging are not needed or used.
  virtual bool usesPhysRegsForPEI() const { return true; }

  /// True if the target wants to use interprocedural register allocation by
  /// default. The -enable-ipra flag can be used to override this.
  virtual bool useIPRA() const {
    return false;
  }
};

/// Helper method for getting the code model, returning Default if
/// CM does not have a value. The tiny and kernel models will produce
/// an error, so targets that support them or require more complex codemodel
/// selection logic should implement and call their own getEffectiveCodeModel.
inline CodeModel::Model getEffectiveCodeModel(Optional<CodeModel::Model> CM,
                                              CodeModel::Model Default) {
  if (CM) {
    // By default, targets do not support the tiny and kernel models.
    if (*CM == CodeModel::Tiny)
      report_fatal_error("Target does not support the tiny CodeModel");
    if (*CM == CodeModel::Kernel)
      report_fatal_error("Target does not support the kernel CodeModel");
    return *CM;
  }
  return Default;
}

} // end namespace llvm

#endif // LLVM_TARGET_TARGETMACHINE_H
