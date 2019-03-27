//==-- WebAssemblyTargetStreamer.cpp - WebAssembly Target Streamer Methods --=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines WebAssembly-specific target streamer classes.
/// These are for implementing support for target-specific assembly directives.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetStreamer.h"
#include "InstPrinter/WebAssemblyInstPrinter.h"
#include "WebAssemblyMCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

WebAssemblyTargetStreamer::WebAssemblyTargetStreamer(MCStreamer &S)
    : MCTargetStreamer(S) {}

void WebAssemblyTargetStreamer::emitValueType(wasm::ValType Type) {
  Streamer.EmitIntValue(uint8_t(Type), 1);
}

WebAssemblyTargetAsmStreamer::WebAssemblyTargetAsmStreamer(
    MCStreamer &S, formatted_raw_ostream &OS)
    : WebAssemblyTargetStreamer(S), OS(OS) {}

WebAssemblyTargetWasmStreamer::WebAssemblyTargetWasmStreamer(MCStreamer &S)
    : WebAssemblyTargetStreamer(S) {}

static void printTypes(formatted_raw_ostream &OS,
                       ArrayRef<wasm::ValType> Types) {
  bool First = true;
  for (auto Type : Types) {
    if (First)
      First = false;
    else
      OS << ", ";
    OS << WebAssembly::typeToString(Type);
  }
  OS << '\n';
}

void WebAssemblyTargetAsmStreamer::emitLocal(ArrayRef<wasm::ValType> Types) {
  if (!Types.empty()) {
    OS << "\t.local  \t";
    printTypes(OS, Types);
  }
}

void WebAssemblyTargetAsmStreamer::emitEndFunc() { OS << "\t.endfunc\n"; }

void WebAssemblyTargetAsmStreamer::emitSignature(
    const wasm::WasmSignature *Sig) {
  OS << "(";
  emitParamList(Sig);
  OS << ") -> (";
  emitReturnList(Sig);
  OS << ")";
}

void WebAssemblyTargetAsmStreamer::emitParamList(
    const wasm::WasmSignature *Sig) {
  auto &Params = Sig->Params;
  for (auto &Ty : Params) {
    if (&Ty != &Params[0])
      OS << ", ";
    OS << WebAssembly::typeToString(Ty);
  }
}

void WebAssemblyTargetAsmStreamer::emitReturnList(
    const wasm::WasmSignature *Sig) {
  auto &Returns = Sig->Returns;
  for (auto &Ty : Returns) {
    if (&Ty != &Returns[0])
      OS << ", ";
    OS << WebAssembly::typeToString(Ty);
  }
}

void WebAssemblyTargetAsmStreamer::emitFunctionType(const MCSymbolWasm *Sym) {
  assert(Sym->isFunction());
  OS << "\t.functype\t" << Sym->getName() << " ";
  emitSignature(Sym->getSignature());
  OS << "\n";
}

void WebAssemblyTargetAsmStreamer::emitGlobalType(const MCSymbolWasm *Sym) {
  assert(Sym->isGlobal());
  OS << "\t.globaltype\t" << Sym->getName() << ", "
     << WebAssembly::typeToString(
            static_cast<wasm::ValType>(Sym->getGlobalType().Type))
     << '\n';
}

void WebAssemblyTargetAsmStreamer::emitEventType(const MCSymbolWasm *Sym) {
  assert(Sym->isEvent());
  OS << "\t.eventtype\t" << Sym->getName() << " ";
  emitParamList(Sym->getSignature());
  OS << "\n";
}

void WebAssemblyTargetAsmStreamer::emitImportModule(const MCSymbolWasm *Sym,
                                                    StringRef ImportModule) {
  OS << "\t.import_module\t" << Sym->getName() << ", "
                             << ImportModule << '\n';
}

void WebAssemblyTargetAsmStreamer::emitImportName(const MCSymbolWasm *Sym,
                                                  StringRef ImportName) {
  OS << "\t.import_name\t" << Sym->getName() << ", "
                           << ImportName << '\n';
}

void WebAssemblyTargetAsmStreamer::emitIndIdx(const MCExpr *Value) {
  OS << "\t.indidx  \t" << *Value << '\n';
}

void WebAssemblyTargetWasmStreamer::emitLocal(ArrayRef<wasm::ValType> Types) {
  SmallVector<std::pair<wasm::ValType, uint32_t>, 4> Grouped;
  for (auto Type : Types) {
    if (Grouped.empty() || Grouped.back().first != Type)
      Grouped.push_back(std::make_pair(Type, 1));
    else
      ++Grouped.back().second;
  }

  Streamer.EmitULEB128IntValue(Grouped.size());
  for (auto Pair : Grouped) {
    Streamer.EmitULEB128IntValue(Pair.second);
    emitValueType(Pair.first);
  }
}

void WebAssemblyTargetWasmStreamer::emitEndFunc() {
  llvm_unreachable(".end_func is not needed for direct wasm output");
}

void WebAssemblyTargetWasmStreamer::emitIndIdx(const MCExpr *Value) {
  llvm_unreachable(".indidx encoding not yet implemented");
}
