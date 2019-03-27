//===- llvm/Support/DiagnosticInfo.cpp - Diagnostic Definitions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#include "llvm/IR/DiagnosticInfo.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <string>

using namespace llvm;

int llvm::getNextAvailablePluginDiagnosticKind() {
  static std::atomic<int> PluginKindID(DK_FirstPluginKind);
  return ++PluginKindID;
}

const char *OptimizationRemarkAnalysis::AlwaysPrint = "";

DiagnosticInfoInlineAsm::DiagnosticInfoInlineAsm(const Instruction &I,
                                                 const Twine &MsgStr,
                                                 DiagnosticSeverity Severity)
    : DiagnosticInfo(DK_InlineAsm, Severity), MsgStr(MsgStr), Instr(&I) {
  if (const MDNode *SrcLoc = I.getMetadata("srcloc")) {
    if (SrcLoc->getNumOperands() != 0)
      if (const auto *CI =
              mdconst::dyn_extract<ConstantInt>(SrcLoc->getOperand(0)))
        LocCookie = CI->getZExtValue();
  }
}

void DiagnosticInfoInlineAsm::print(DiagnosticPrinter &DP) const {
  DP << getMsgStr();
  if (getLocCookie())
    DP << " at line " << getLocCookie();
}

void DiagnosticInfoResourceLimit::print(DiagnosticPrinter &DP) const {
  DP << getResourceName() << " limit";

  if (getResourceLimit() != 0)
    DP << " of " << getResourceLimit();

  DP << " exceeded (" <<  getResourceSize() << ") in " << getFunction();
}

void DiagnosticInfoDebugMetadataVersion::print(DiagnosticPrinter &DP) const {
  DP << "ignoring debug info with an invalid version (" << getMetadataVersion()
     << ") in " << getModule();
}

void DiagnosticInfoIgnoringInvalidDebugMetadata::print(
    DiagnosticPrinter &DP) const {
  DP << "ignoring invalid debug info in " << getModule().getModuleIdentifier();
}

void DiagnosticInfoSampleProfile::print(DiagnosticPrinter &DP) const {
  if (!FileName.empty()) {
    DP << getFileName();
    if (LineNum > 0)
      DP << ":" << getLineNum();
    DP << ": ";
  }
  DP << getMsg();
}

void DiagnosticInfoPGOProfile::print(DiagnosticPrinter &DP) const {
  if (getFileName())
    DP << getFileName() << ": ";
  DP << getMsg();
}

void DiagnosticInfo::anchor() {}
void DiagnosticInfoStackSize::anchor() {}
void DiagnosticInfoWithLocationBase::anchor() {}
void DiagnosticInfoIROptimization::anchor() {}

DiagnosticLocation::DiagnosticLocation(const DebugLoc &DL) {
  if (!DL)
    return;
  File = DL->getFile();
  Line = DL->getLine();
  Column = DL->getColumn();
}

DiagnosticLocation::DiagnosticLocation(const DISubprogram *SP) {
  if (!SP)
    return;
  
  File = SP->getFile();
  Line = SP->getScopeLine();
  Column = 0;
}

StringRef DiagnosticLocation::getRelativePath() const {
  return File->getFilename();
}

std::string DiagnosticLocation::getAbsolutePath() const {
  StringRef Name = File->getFilename();
  if (sys::path::is_absolute(Name))
    return Name;

  SmallString<128> Path;
  sys::path::append(Path, File->getDirectory(), Name);
  return sys::path::remove_leading_dotslash(Path).str();
}

std::string DiagnosticInfoWithLocationBase::getAbsolutePath() const {
  return Loc.getAbsolutePath();
}

void DiagnosticInfoWithLocationBase::getLocation(StringRef &RelativePath,
                                                 unsigned &Line,
                                                 unsigned &Column) const {
  RelativePath = Loc.getRelativePath();
  Line = Loc.getLine();
  Column = Loc.getColumn();
}

const std::string DiagnosticInfoWithLocationBase::getLocationStr() const {
  StringRef Filename("<unknown>");
  unsigned Line = 0;
  unsigned Column = 0;
  if (isLocationAvailable())
    getLocation(Filename, Line, Column);
  return (Filename + ":" + Twine(Line) + ":" + Twine(Column)).str();
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, const Value *V)
    : Key(Key) {
  if (auto *F = dyn_cast<Function>(V)) {
    if (DISubprogram *SP = F->getSubprogram())
      Loc = SP;
  }
  else if (auto *I = dyn_cast<Instruction>(V))
    Loc = I->getDebugLoc();

  // Only include names that correspond to user variables.  FIXME: We should use
  // debug info if available to get the name of the user variable.
  if (isa<llvm::Argument>(V) || isa<GlobalValue>(V))
    Val = GlobalValue::dropLLVMManglingEscape(V->getName());
  else if (isa<Constant>(V)) {
    raw_string_ostream OS(Val);
    V->printAsOperand(OS, /*PrintType=*/false);
  } else if (auto *I = dyn_cast<Instruction>(V))
    Val = I->getOpcodeName();
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, const Type *T)
    : Key(Key) {
  raw_string_ostream OS(Val);
  OS << *T;
}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, StringRef S)
    : Key(Key), Val(S.str()) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, int N)
    : Key(Key), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, float N)
    : Key(Key), Val(llvm::to_string(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, long N)
    : Key(Key), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, long long N)
    : Key(Key), Val(itostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, unsigned N)
    : Key(Key), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   unsigned long N)
    : Key(Key), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key,
                                                   unsigned long long N)
    : Key(Key), Val(utostr(N)) {}

DiagnosticInfoOptimizationBase::Argument::Argument(StringRef Key, DebugLoc Loc)
    : Key(Key), Loc(Loc) {
  if (Loc) {
    Val = (Loc->getFilename() + ":" + Twine(Loc.getLine()) + ":" +
           Twine(Loc.getCol())).str();
  } else {
    Val = "<UNKNOWN LOCATION>";
  }
}

void DiagnosticInfoOptimizationBase::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getMsg();
  if (Hotness)
    DP << " (hotness: " << *Hotness << ")";
}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const DiagnosticLocation &Loc,
                                       const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemark, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                   RemarkName, *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

// Helper to allow for an assert before attempting to return an invalid
// reference.
static const BasicBlock &getFirstFunctionBlock(const Function *Func) {
  assert(!Func->empty() && "Function does not have a body");
  return Func->front();
}

OptimizationRemark::OptimizationRemark(const char *PassName,
                                       StringRef RemarkName,
                                       const Function *Func)
    : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                   RemarkName, *Func, Func->getSubprogram(),
                                   &getFirstFunctionBlock(Func)) {}

bool OptimizationRemark::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled(getPassName());
}

OptimizationRemarkMissed::OptimizationRemarkMissed(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkMissed, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemarkMissed::OptimizationRemarkMissed(const char *PassName,
                                                   StringRef RemarkName,
                                                   const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemarkMissed, DS_Remark,
                                   PassName, RemarkName,
                                   *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

bool OptimizationRemarkMissed::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isMissedOptRemarkEnabled(getPassName());
}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationRemarkAnalysis, DS_Remark, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(const char *PassName,
                                                       StringRef RemarkName,
                                                       const Instruction *Inst)
    : DiagnosticInfoIROptimization(DK_OptimizationRemarkAnalysis, DS_Remark,
                                   PassName, RemarkName,
                                   *Inst->getParent()->getParent(),
                                   Inst->getDebugLoc(), Inst->getParent()) {}

OptimizationRemarkAnalysis::OptimizationRemarkAnalysis(
    enum DiagnosticKind Kind, const char *PassName, StringRef RemarkName,
    const DiagnosticLocation &Loc, const Value *CodeRegion)
    : DiagnosticInfoIROptimization(Kind, DS_Remark, PassName, RemarkName,
                                   *cast<BasicBlock>(CodeRegion)->getParent(),
                                   Loc, CodeRegion) {}

bool OptimizationRemarkAnalysis::isEnabled() const {
  const Function &Fn = getFunction();
  LLVMContext &Ctx = Fn.getContext();
  return Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(getPassName()) ||
         shouldAlwaysPrint();
}

void DiagnosticInfoMIRParser::print(DiagnosticPrinter &DP) const {
  DP << Diagnostic;
}

DiagnosticInfoOptimizationFailure::DiagnosticInfoOptimizationFailure(
    const char *PassName, StringRef RemarkName, const DiagnosticLocation &Loc,
    const Value *CodeRegion)
    : DiagnosticInfoIROptimization(
          DK_OptimizationFailure, DS_Warning, PassName, RemarkName,
          *cast<BasicBlock>(CodeRegion)->getParent(), Loc, CodeRegion) {}

bool DiagnosticInfoOptimizationFailure::isEnabled() const {
  // Only print warnings.
  return getSeverity() == DS_Warning;
}

void DiagnosticInfoUnsupported::print(DiagnosticPrinter &DP) const {
  std::string Str;
  raw_string_ostream OS(Str);

  OS << getLocationStr() << ": in function " << getFunction().getName() << ' '
     << *getFunction().getFunctionType() << ": " << Msg << '\n';
  OS.flush();
  DP << Str;
}

void DiagnosticInfoISelFallback::print(DiagnosticPrinter &DP) const {
  DP << "Instruction selection used fallback path for " << getFunction();
}

void DiagnosticInfoOptimizationBase::insert(StringRef S) {
  Args.emplace_back(S);
}

void DiagnosticInfoOptimizationBase::insert(Argument A) {
  Args.push_back(std::move(A));
}

void DiagnosticInfoOptimizationBase::insert(setIsVerbose V) {
  IsVerbose = true;
}

void DiagnosticInfoOptimizationBase::insert(setExtraArgs EA) {
  FirstExtraArgIndex = Args.size();
}

std::string DiagnosticInfoOptimizationBase::getMsg() const {
  std::string Str;
  raw_string_ostream OS(Str);
  for (const DiagnosticInfoOptimizationBase::Argument &Arg :
       make_range(Args.begin(), FirstExtraArgIndex == -1
                                    ? Args.end()
                                    : Args.begin() + FirstExtraArgIndex))
    OS << Arg.Val;
  return OS.str();
}

void OptimizationRemarkAnalysisFPCommute::anchor() {}
void OptimizationRemarkAnalysisAliasing::anchor() {}

namespace llvm {
namespace yaml {

void MappingTraits<DiagnosticInfoOptimizationBase *>::mapping(
    IO &io, DiagnosticInfoOptimizationBase *&OptDiag) {
  assert(io.outputting() && "input not yet implemented");

  if (io.mapTag("!Passed",
                (OptDiag->getKind() == DK_OptimizationRemark ||
                 OptDiag->getKind() == DK_MachineOptimizationRemark)))
    ;
  else if (io.mapTag(
               "!Missed",
               (OptDiag->getKind() == DK_OptimizationRemarkMissed ||
                OptDiag->getKind() == DK_MachineOptimizationRemarkMissed)))
    ;
  else if (io.mapTag(
               "!Analysis",
               (OptDiag->getKind() == DK_OptimizationRemarkAnalysis ||
                OptDiag->getKind() == DK_MachineOptimizationRemarkAnalysis)))
    ;
  else if (io.mapTag("!AnalysisFPCommute",
                     OptDiag->getKind() ==
                         DK_OptimizationRemarkAnalysisFPCommute))
    ;
  else if (io.mapTag("!AnalysisAliasing",
                     OptDiag->getKind() ==
                         DK_OptimizationRemarkAnalysisAliasing))
    ;
  else if (io.mapTag("!Failure", OptDiag->getKind() == DK_OptimizationFailure))
    ;
  else
    llvm_unreachable("Unknown remark type");

  // These are read-only for now.
  DiagnosticLocation DL = OptDiag->getLocation();
  StringRef FN =
      GlobalValue::dropLLVMManglingEscape(OptDiag->getFunction().getName());

  StringRef PassName(OptDiag->PassName);
  io.mapRequired("Pass", PassName);
  io.mapRequired("Name", OptDiag->RemarkName);
  if (!io.outputting() || DL.isValid())
    io.mapOptional("DebugLoc", DL);
  io.mapRequired("Function", FN);
  io.mapOptional("Hotness", OptDiag->Hotness);
  io.mapOptional("Args", OptDiag->Args);
}

template <> struct MappingTraits<DiagnosticLocation> {
  static void mapping(IO &io, DiagnosticLocation &DL) {
    assert(io.outputting() && "input not yet implemented");

    StringRef File = DL.getRelativePath();
    unsigned Line = DL.getLine();
    unsigned Col = DL.getColumn();

    io.mapRequired("File", File);
    io.mapRequired("Line", Line);
    io.mapRequired("Column", Col);
  }

  static const bool flow = true;
};

// Implement this as a mapping for now to get proper quotation for the value.
template <> struct MappingTraits<DiagnosticInfoOptimizationBase::Argument> {
  static void mapping(IO &io, DiagnosticInfoOptimizationBase::Argument &A) {
    assert(io.outputting() && "input not yet implemented");
    io.mapRequired(A.Key.data(), A.Val);
    if (A.Loc.isValid())
      io.mapOptional("DebugLoc", A.Loc);
  }
};

} // end namespace yaml
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(DiagnosticInfoOptimizationBase::Argument)
