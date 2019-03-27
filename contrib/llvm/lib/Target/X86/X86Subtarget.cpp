//===-- X86Subtarget.cpp - X86 Subtarget Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the X86 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "X86.h"

#include "X86CallLowering.h"
#include "X86LegalizerInfo.h"
#include "X86RegisterBankInfo.h"
#include "X86Subtarget.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "X86GenSubtargetInfo.inc"

// Temporary option to control early if-conversion for x86 while adding machine
// models.
static cl::opt<bool>
X86EarlyIfConv("x86-early-ifcvt", cl::Hidden,
               cl::desc("Enable early if-conversion on X86"));


/// Classify a blockaddress reference for the current subtarget according to how
/// we should reference it in a non-pcrel context.
unsigned char X86Subtarget::classifyBlockAddressReference() const {
  return classifyLocalReference(nullptr);
}

/// Classify a global variable reference for the current subtarget according to
/// how we should reference it in a non-pcrel context.
unsigned char
X86Subtarget::classifyGlobalReference(const GlobalValue *GV) const {
  return classifyGlobalReference(GV, *GV->getParent());
}

unsigned char
X86Subtarget::classifyLocalReference(const GlobalValue *GV) const {
  // If we're not PIC, it's not very interesting.
  if (!isPositionIndependent())
    return X86II::MO_NO_FLAG;

  if (is64Bit()) {
    // 64-bit ELF PIC local references may use GOTOFF relocations.
    if (isTargetELF()) {
      switch (TM.getCodeModel()) {
      // 64-bit small code model is simple: All rip-relative.
      case CodeModel::Tiny:
        llvm_unreachable("Tiny codesize model not supported on X86");
      case CodeModel::Small:
      case CodeModel::Kernel:
        return X86II::MO_NO_FLAG;

      // The large PIC code model uses GOTOFF.
      case CodeModel::Large:
        return X86II::MO_GOTOFF;

      // Medium is a hybrid: RIP-rel for code, GOTOFF for DSO local data.
      case CodeModel::Medium:
        if (isa<Function>(GV))
          return X86II::MO_NO_FLAG; // All code is RIP-relative
        return X86II::MO_GOTOFF;    // Local symbols use GOTOFF.
      }
      llvm_unreachable("invalid code model");
    }

    // Otherwise, this is either a RIP-relative reference or a 64-bit movabsq,
    // both of which use MO_NO_FLAG.
    return X86II::MO_NO_FLAG;
  }

  // The COFF dynamic linker just patches the executable sections.
  if (isTargetCOFF())
    return X86II::MO_NO_FLAG;

  if (isTargetDarwin()) {
    // 32 bit macho has no relocation for a-b if a is undefined, even if
    // b is in the section that is being relocated.
    // This means we have to use o load even for GVs that are known to be
    // local to the dso.
    if (GV && (GV->isDeclarationForLinker() || GV->hasCommonLinkage()))
      return X86II::MO_DARWIN_NONLAZY_PIC_BASE;

    return X86II::MO_PIC_BASE_OFFSET;
  }

  return X86II::MO_GOTOFF;
}

unsigned char X86Subtarget::classifyGlobalReference(const GlobalValue *GV,
                                                    const Module &M) const {
  // The static large model never uses stubs.
  if (TM.getCodeModel() == CodeModel::Large && !isPositionIndependent())
    return X86II::MO_NO_FLAG;

  // Absolute symbols can be referenced directly.
  if (GV) {
    if (Optional<ConstantRange> CR = GV->getAbsoluteSymbolRange()) {
      // See if we can use the 8-bit immediate form. Note that some instructions
      // will sign extend the immediate operand, so to be conservative we only
      // accept the range [0,128).
      if (CR->getUnsignedMax().ult(128))
        return X86II::MO_ABS8;
      else
        return X86II::MO_NO_FLAG;
    }
  }

  if (TM.shouldAssumeDSOLocal(M, GV))
    return classifyLocalReference(GV);

  if (isTargetCOFF()) {
    if (GV->hasDLLImportStorageClass())
      return X86II::MO_DLLIMPORT;
    return X86II::MO_COFFSTUB;
  }

  if (is64Bit()) {
    // ELF supports a large, truly PIC code model with non-PC relative GOT
    // references. Other object file formats do not. Use the no-flag, 64-bit
    // reference for them.
    if (TM.getCodeModel() == CodeModel::Large)
      return isTargetELF() ? X86II::MO_GOT : X86II::MO_NO_FLAG;
    return X86II::MO_GOTPCREL;
  }

  if (isTargetDarwin()) {
    if (!isPositionIndependent())
      return X86II::MO_DARWIN_NONLAZY;
    return X86II::MO_DARWIN_NONLAZY_PIC_BASE;
  }

  return X86II::MO_GOT;
}

unsigned char
X86Subtarget::classifyGlobalFunctionReference(const GlobalValue *GV) const {
  return classifyGlobalFunctionReference(GV, *GV->getParent());
}

unsigned char
X86Subtarget::classifyGlobalFunctionReference(const GlobalValue *GV,
                                              const Module &M) const {
  if (TM.shouldAssumeDSOLocal(M, GV))
    return X86II::MO_NO_FLAG;

  if (isTargetCOFF()) {
    assert(GV->hasDLLImportStorageClass() &&
           "shouldAssumeDSOLocal gave inconsistent answer");
    return X86II::MO_DLLIMPORT;
  }

  const Function *F = dyn_cast_or_null<Function>(GV);

  if (isTargetELF()) {
    if (is64Bit() && F && (CallingConv::X86_RegCall == F->getCallingConv()))
      // According to psABI, PLT stub clobbers XMM8-XMM15.
      // In Regcall calling convention those registers are used for passing
      // parameters. Thus we need to prevent lazy binding in Regcall.
      return X86II::MO_GOTPCREL;
    // If PLT must be avoided then the call should be via GOTPCREL.
    if (((F && F->hasFnAttribute(Attribute::NonLazyBind)) ||
         (!F && M.getRtLibUseGOT())) &&
        is64Bit())
       return X86II::MO_GOTPCREL;
    return X86II::MO_PLT;
  }

  if (is64Bit()) {
    if (F && F->hasFnAttribute(Attribute::NonLazyBind))
      // If the function is marked as non-lazy, generate an indirect call
      // which loads from the GOT directly. This avoids runtime overhead
      // at the cost of eager binding (and one extra byte of encoding).
      return X86II::MO_GOTPCREL;
    return X86II::MO_NO_FLAG;
  }

  return X86II::MO_NO_FLAG;
}

/// Return true if the subtarget allows calls to immediate address.
bool X86Subtarget::isLegalToCallImmediateAddr() const {
  // FIXME: I386 PE/COFF supports PC relative calls using IMAGE_REL_I386_REL32
  // but WinCOFFObjectWriter::RecordRelocation cannot emit them.  Once it does,
  // the following check for Win32 should be removed.
  if (In64BitMode || isTargetWin32())
    return false;
  return isTargetELF() || TM.getRelocationModel() == Reloc::Static;
}

void X86Subtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "generic";

  std::string FullFS = FS;
  if (In64BitMode) {
    // SSE2 should default to enabled in 64-bit mode, but can be turned off
    // explicitly.
    if (!FullFS.empty())
      FullFS = "+sse2," + FullFS;
    else
      FullFS = "+sse2";

    // If no CPU was specified, enable 64bit feature to satisy later check.
    if (CPUName == "generic") {
      if (!FullFS.empty())
        FullFS = "+64bit," + FullFS;
      else
        FullFS = "+64bit";
    }
  }

  // LAHF/SAHF are always supported in non-64-bit mode.
  if (!In64BitMode) {
    if (!FullFS.empty())
      FullFS = "+sahf," + FullFS;
    else
      FullFS = "+sahf";
  }

  // Parse features string and set the CPU.
  ParseSubtargetFeatures(CPUName, FullFS);

  // All CPUs that implement SSE4.2 or SSE4A support unaligned accesses of
  // 16-bytes and under that are reasonably fast. These features were
  // introduced with Intel's Nehalem/Silvermont and AMD's Family10h
  // micro-architectures respectively.
  if (hasSSE42() || hasSSE4A())
    IsUAMem16Slow = false;

  // It's important to keep the MCSubtargetInfo feature bits in sync with
  // target data structure which is shared with MC code emitter, etc.
  if (In64BitMode)
    ToggleFeature(X86::Mode64Bit);
  else if (In32BitMode)
    ToggleFeature(X86::Mode32Bit);
  else if (In16BitMode)
    ToggleFeature(X86::Mode16Bit);
  else
    llvm_unreachable("Not 16-bit, 32-bit or 64-bit mode!");

  LLVM_DEBUG(dbgs() << "Subtarget features: SSELevel " << X86SSELevel
                    << ", 3DNowLevel " << X863DNowLevel << ", 64bit "
                    << HasX86_64 << "\n");
  if (In64BitMode && !HasX86_64)
    report_fatal_error("64-bit code requested on a subtarget that doesn't "
                       "support it!");

  // Stack alignment is 16 bytes on Darwin, Linux, kFreeBSD and Solaris (both
  // 32 and 64 bit) and for all 64-bit targets.
  if (StackAlignOverride)
    stackAlignment = StackAlignOverride;
  else if (isTargetDarwin() || isTargetLinux() || isTargetSolaris() ||
           isTargetKFreeBSD() || In64BitMode)
    stackAlignment = 16;

  // Some CPUs have more overhead for gather. The specified overhead is relative
  // to the Load operation. "2" is the number provided by Intel architects. This
  // parameter is used for cost estimation of Gather Op and comparison with
  // other alternatives.
  // TODO: Remove the explicit hasAVX512()?, That would mean we would only
  // enable gather with a -march.
  if (hasAVX512() || (hasAVX2() && hasFastGather()))
    GatherOverhead = 2;
  if (hasAVX512())
    ScatterOverhead = 2;

  // Consume the vector width attribute or apply any target specific limit.
  if (PreferVectorWidthOverride)
    PreferVectorWidth = PreferVectorWidthOverride;
  else if (Prefer256Bit)
    PreferVectorWidth = 256;
}

X86Subtarget &X86Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef FS) {
  initSubtargetFeatures(CPU, FS);
  return *this;
}

X86Subtarget::X86Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                           const X86TargetMachine &TM,
                           unsigned StackAlignOverride,
                           unsigned PreferVectorWidthOverride,
                           unsigned RequiredVectorWidth)
    : X86GenSubtargetInfo(TT, CPU, FS),
      PICStyle(PICStyles::None), TM(TM), TargetTriple(TT),
      StackAlignOverride(StackAlignOverride),
      PreferVectorWidthOverride(PreferVectorWidthOverride),
      RequiredVectorWidth(RequiredVectorWidth),
      In64BitMode(TargetTriple.getArch() == Triple::x86_64),
      In32BitMode(TargetTriple.getArch() == Triple::x86 &&
                  TargetTriple.getEnvironment() != Triple::CODE16),
      In16BitMode(TargetTriple.getArch() == Triple::x86 &&
                  TargetTriple.getEnvironment() == Triple::CODE16),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this, getStackAlignment()) {
  // Determine the PICStyle based on the target selected.
  if (!isPositionIndependent())
    setPICStyle(PICStyles::None);
  else if (is64Bit())
    setPICStyle(PICStyles::RIPRel);
  else if (isTargetCOFF())
    setPICStyle(PICStyles::None);
  else if (isTargetDarwin())
    setPICStyle(PICStyles::StubPIC);
  else if (isTargetELF())
    setPICStyle(PICStyles::GOT);

  CallLoweringInfo.reset(new X86CallLowering(*getTargetLowering()));
  Legalizer.reset(new X86LegalizerInfo(*this, TM));

  auto *RBI = new X86RegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);
  InstSelector.reset(createX86InstructionSelector(TM, *this, *RBI));
}

const CallLowering *X86Subtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

const InstructionSelector *X86Subtarget::getInstructionSelector() const {
  return InstSelector.get();
}

const LegalizerInfo *X86Subtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

const RegisterBankInfo *X86Subtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}

bool X86Subtarget::enableEarlyIfConversion() const {
  return hasCMov() && X86EarlyIfConv;
}
