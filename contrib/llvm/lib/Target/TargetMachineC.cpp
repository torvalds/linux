//===-- TargetMachine.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM-C part of TargetMachine.h
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm-c/Target.h"
#include "llvm-c/TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/CodeGenCWrappers.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

using namespace llvm;

static TargetMachine *unwrap(LLVMTargetMachineRef P) {
  return reinterpret_cast<TargetMachine *>(P);
}
static Target *unwrap(LLVMTargetRef P) {
  return reinterpret_cast<Target*>(P);
}
static LLVMTargetMachineRef wrap(const TargetMachine *P) {
  return reinterpret_cast<LLVMTargetMachineRef>(const_cast<TargetMachine *>(P));
}
static LLVMTargetRef wrap(const Target * P) {
  return reinterpret_cast<LLVMTargetRef>(const_cast<Target*>(P));
}

LLVMTargetRef LLVMGetFirstTarget() {
  if (TargetRegistry::targets().begin() == TargetRegistry::targets().end()) {
    return nullptr;
  }

  const Target *target = &*TargetRegistry::targets().begin();
  return wrap(target);
}
LLVMTargetRef LLVMGetNextTarget(LLVMTargetRef T) {
  return wrap(unwrap(T)->getNext());
}

LLVMTargetRef LLVMGetTargetFromName(const char *Name) {
  StringRef NameRef = Name;
  auto I = find_if(TargetRegistry::targets(),
                   [&](const Target &T) { return T.getName() == NameRef; });
  return I != TargetRegistry::targets().end() ? wrap(&*I) : nullptr;
}

LLVMBool LLVMGetTargetFromTriple(const char* TripleStr, LLVMTargetRef *T,
                                 char **ErrorMessage) {
  std::string Error;

  *T = wrap(TargetRegistry::lookupTarget(TripleStr, Error));

  if (!*T) {
    if (ErrorMessage)
      *ErrorMessage = strdup(Error.c_str());

    return 1;
  }

  return 0;
}

const char * LLVMGetTargetName(LLVMTargetRef T) {
  return unwrap(T)->getName();
}

const char * LLVMGetTargetDescription(LLVMTargetRef T) {
  return unwrap(T)->getShortDescription();
}

LLVMBool LLVMTargetHasJIT(LLVMTargetRef T) {
  return unwrap(T)->hasJIT();
}

LLVMBool LLVMTargetHasTargetMachine(LLVMTargetRef T) {
  return unwrap(T)->hasTargetMachine();
}

LLVMBool LLVMTargetHasAsmBackend(LLVMTargetRef T) {
  return unwrap(T)->hasMCAsmBackend();
}

LLVMTargetMachineRef LLVMCreateTargetMachine(LLVMTargetRef T,
        const char *Triple, const char *CPU, const char *Features,
        LLVMCodeGenOptLevel Level, LLVMRelocMode Reloc,
        LLVMCodeModel CodeModel) {
  Optional<Reloc::Model> RM;
  switch (Reloc){
    case LLVMRelocStatic:
      RM = Reloc::Static;
      break;
    case LLVMRelocPIC:
      RM = Reloc::PIC_;
      break;
    case LLVMRelocDynamicNoPic:
      RM = Reloc::DynamicNoPIC;
      break;
    case LLVMRelocROPI:
      RM = Reloc::ROPI;
      break;
    case LLVMRelocRWPI:
      RM = Reloc::RWPI;
      break;
    case LLVMRelocROPI_RWPI:
      RM = Reloc::ROPI_RWPI;
      break;
    default:
      break;
  }

  bool JIT;
  Optional<CodeModel::Model> CM = unwrap(CodeModel, JIT);

  CodeGenOpt::Level OL;
  switch (Level) {
    case LLVMCodeGenLevelNone:
      OL = CodeGenOpt::None;
      break;
    case LLVMCodeGenLevelLess:
      OL = CodeGenOpt::Less;
      break;
    case LLVMCodeGenLevelAggressive:
      OL = CodeGenOpt::Aggressive;
      break;
    default:
      OL = CodeGenOpt::Default;
      break;
  }

  TargetOptions opt;
  return wrap(unwrap(T)->createTargetMachine(Triple, CPU, Features, opt, RM, CM,
                                             OL, JIT));
}

void LLVMDisposeTargetMachine(LLVMTargetMachineRef T) { delete unwrap(T); }

LLVMTargetRef LLVMGetTargetMachineTarget(LLVMTargetMachineRef T) {
  const Target* target = &(unwrap(T)->getTarget());
  return wrap(target);
}

char* LLVMGetTargetMachineTriple(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetTriple().str();
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineCPU(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetCPU();
  return strdup(StringRep.c_str());
}

char* LLVMGetTargetMachineFeatureString(LLVMTargetMachineRef T) {
  std::string StringRep = unwrap(T)->getTargetFeatureString();
  return strdup(StringRep.c_str());
}

void LLVMSetTargetMachineAsmVerbosity(LLVMTargetMachineRef T,
                                      LLVMBool VerboseAsm) {
  unwrap(T)->Options.MCOptions.AsmVerbose = VerboseAsm;
}

LLVMTargetDataRef LLVMCreateTargetDataLayout(LLVMTargetMachineRef T) {
  return wrap(new DataLayout(unwrap(T)->createDataLayout()));
}

static LLVMBool LLVMTargetMachineEmit(LLVMTargetMachineRef T, LLVMModuleRef M,
                                      raw_pwrite_stream &OS,
                                      LLVMCodeGenFileType codegen,
                                      char **ErrorMessage) {
  TargetMachine* TM = unwrap(T);
  Module* Mod = unwrap(M);

  legacy::PassManager pass;

  std::string error;

  Mod->setDataLayout(TM->createDataLayout());

  TargetMachine::CodeGenFileType ft;
  switch (codegen) {
    case LLVMAssemblyFile:
      ft = TargetMachine::CGFT_AssemblyFile;
      break;
    default:
      ft = TargetMachine::CGFT_ObjectFile;
      break;
  }
  if (TM->addPassesToEmitFile(pass, OS, nullptr, ft)) {
    error = "TargetMachine can't emit a file of this type";
    *ErrorMessage = strdup(error.c_str());
    return true;
  }

  pass.run(*Mod);

  OS.flush();
  return false;
}

LLVMBool LLVMTargetMachineEmitToFile(LLVMTargetMachineRef T, LLVMModuleRef M,
  char* Filename, LLVMCodeGenFileType codegen, char** ErrorMessage) {
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::F_None);
  if (EC) {
    *ErrorMessage = strdup(EC.message().c_str());
    return true;
  }
  bool Result = LLVMTargetMachineEmit(T, M, dest, codegen, ErrorMessage);
  dest.flush();
  return Result;
}

LLVMBool LLVMTargetMachineEmitToMemoryBuffer(LLVMTargetMachineRef T,
  LLVMModuleRef M, LLVMCodeGenFileType codegen, char** ErrorMessage,
  LLVMMemoryBufferRef *OutMemBuf) {
  SmallString<0> CodeString;
  raw_svector_ostream OStream(CodeString);
  bool Result = LLVMTargetMachineEmit(T, M, OStream, codegen, ErrorMessage);

  StringRef Data = OStream.str();
  *OutMemBuf =
      LLVMCreateMemoryBufferWithMemoryRangeCopy(Data.data(), Data.size(), "");
  return Result;
}

char *LLVMGetDefaultTargetTriple(void) {
  return strdup(sys::getDefaultTargetTriple().c_str());
}

char *LLVMNormalizeTargetTriple(const char* triple) {
  return strdup(Triple::normalize(StringRef(triple)).c_str());
}

char *LLVMGetHostCPUName(void) {
  return strdup(sys::getHostCPUName().data());
}

char *LLVMGetHostCPUFeatures(void) {
  SubtargetFeatures Features;
  StringMap<bool> HostFeatures;

  if (sys::getHostCPUFeatures(HostFeatures))
    for (auto &F : HostFeatures)
      Features.AddFeature(F.first(), F.second);

  return strdup(Features.getString().c_str());
}

void LLVMAddAnalysisPasses(LLVMTargetMachineRef T, LLVMPassManagerRef PM) {
  unwrap(PM)->add(
      createTargetTransformInfoWrapperPass(unwrap(T)->getTargetIRAnalysis()));
}
