//===- FaultMaps.cpp ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/FaultMaps.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "faultmaps"

static const int FaultMapVersion = 1;
const char *FaultMaps::WFMP = "Fault Maps: ";

FaultMaps::FaultMaps(AsmPrinter &AP) : AP(AP) {}

void FaultMaps::recordFaultingOp(FaultKind FaultTy,
                                 const MCSymbol *HandlerLabel) {
  MCContext &OutContext = AP.OutStreamer->getContext();
  MCSymbol *FaultingLabel = OutContext.createTempSymbol();

  AP.OutStreamer->EmitLabel(FaultingLabel);

  const MCExpr *FaultingOffset = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(FaultingLabel, OutContext),
      MCSymbolRefExpr::create(AP.CurrentFnSymForSize, OutContext), OutContext);

  const MCExpr *HandlerOffset = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(HandlerLabel, OutContext),
      MCSymbolRefExpr::create(AP.CurrentFnSymForSize, OutContext), OutContext);

  FunctionInfos[AP.CurrentFnSym].emplace_back(FaultTy, FaultingOffset,
                                              HandlerOffset);
}

void FaultMaps::serializeToFaultMapSection() {
  if (FunctionInfos.empty())
    return;

  MCContext &OutContext = AP.OutStreamer->getContext();
  MCStreamer &OS = *AP.OutStreamer;

  // Create the section.
  MCSection *FaultMapSection =
      OutContext.getObjectFileInfo()->getFaultMapSection();
  OS.SwitchSection(FaultMapSection);

  // Emit a dummy symbol to force section inclusion.
  OS.EmitLabel(OutContext.getOrCreateSymbol(Twine("__LLVM_FaultMaps")));

  LLVM_DEBUG(dbgs() << "********** Fault Map Output **********\n");

  // Header
  OS.EmitIntValue(FaultMapVersion, 1); // Version.
  OS.EmitIntValue(0, 1);               // Reserved.
  OS.EmitIntValue(0, 2);               // Reserved.

  LLVM_DEBUG(dbgs() << WFMP << "#functions = " << FunctionInfos.size() << "\n");
  OS.EmitIntValue(FunctionInfos.size(), 4);

  LLVM_DEBUG(dbgs() << WFMP << "functions:\n");

  for (const auto &FFI : FunctionInfos)
    emitFunctionInfo(FFI.first, FFI.second);
}

void FaultMaps::emitFunctionInfo(const MCSymbol *FnLabel,
                                 const FunctionFaultInfos &FFI) {
  MCStreamer &OS = *AP.OutStreamer;

  LLVM_DEBUG(dbgs() << WFMP << "  function addr: " << *FnLabel << "\n");
  OS.EmitSymbolValue(FnLabel, 8);

  LLVM_DEBUG(dbgs() << WFMP << "  #faulting PCs: " << FFI.size() << "\n");
  OS.EmitIntValue(FFI.size(), 4);

  OS.EmitIntValue(0, 4); // Reserved

  for (auto &Fault : FFI) {
    LLVM_DEBUG(dbgs() << WFMP << "    fault type: "
                      << faultTypeToString(Fault.Kind) << "\n");
    OS.EmitIntValue(Fault.Kind, 4);

    LLVM_DEBUG(dbgs() << WFMP << "    faulting PC offset: "
                      << *Fault.FaultingOffsetExpr << "\n");
    OS.EmitValue(Fault.FaultingOffsetExpr, 4);

    LLVM_DEBUG(dbgs() << WFMP << "    fault handler PC offset: "
                      << *Fault.HandlerOffsetExpr << "\n");
    OS.EmitValue(Fault.HandlerOffsetExpr, 4);
  }
}

const char *FaultMaps::faultTypeToString(FaultMaps::FaultKind FT) {
  switch (FT) {
  default:
    llvm_unreachable("unhandled fault type!");
  case FaultMaps::FaultingLoad:
    return "FaultingLoad";
  case FaultMaps::FaultingLoadStore:
    return "FaultingLoadStore";
  case FaultMaps::FaultingStore:
    return "FaultingStore";
  }
}

raw_ostream &llvm::
operator<<(raw_ostream &OS,
           const FaultMapParser::FunctionFaultInfoAccessor &FFI) {
  OS << "Fault kind: "
     << FaultMaps::faultTypeToString((FaultMaps::FaultKind)FFI.getFaultKind())
     << ", faulting PC offset: " << FFI.getFaultingPCOffset()
     << ", handling PC offset: " << FFI.getHandlerPCOffset();
  return OS;
}

raw_ostream &llvm::
operator<<(raw_ostream &OS, const FaultMapParser::FunctionInfoAccessor &FI) {
  OS << "FunctionAddress: " << format_hex(FI.getFunctionAddr(), 8)
     << ", NumFaultingPCs: " << FI.getNumFaultingPCs() << "\n";
  for (unsigned i = 0, e = FI.getNumFaultingPCs(); i != e; ++i)
    OS << FI.getFunctionFaultInfoAt(i) << "\n";
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const FaultMapParser &FMP) {
  OS << "Version: " << format_hex(FMP.getFaultMapVersion(), 2) << "\n";
  OS << "NumFunctions: " << FMP.getNumFunctions() << "\n";

  if (FMP.getNumFunctions() == 0)
    return OS;

  FaultMapParser::FunctionInfoAccessor FI;

  for (unsigned i = 0, e = FMP.getNumFunctions(); i != e; ++i) {
    FI = (i == 0) ? FMP.getFirstFunctionInfo() : FI.getNextFunctionInfo();
    OS << FI;
  }

  return OS;
}
