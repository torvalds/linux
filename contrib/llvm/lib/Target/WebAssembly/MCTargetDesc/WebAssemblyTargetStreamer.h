//==-- WebAssemblyTargetStreamer.h - WebAssembly Target Streamer -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares WebAssembly-specific target streamer classes.
/// These are for implementing support for target-specific assembly directives.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYTARGETSTREAMER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYTARGETSTREAMER_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/MachineValueType.h"

namespace llvm {

class MCWasmStreamer;
class MCSymbolWasm;

/// WebAssembly-specific streamer interface, to implement support
/// WebAssembly-specific assembly directives.
class WebAssemblyTargetStreamer : public MCTargetStreamer {
public:
  explicit WebAssemblyTargetStreamer(MCStreamer &S);

  /// .local
  virtual void emitLocal(ArrayRef<wasm::ValType> Types) = 0;
  /// .endfunc
  virtual void emitEndFunc() = 0;
  /// .functype
  virtual void emitFunctionType(const MCSymbolWasm *Sym) = 0;
  /// .indidx
  virtual void emitIndIdx(const MCExpr *Value) = 0;
  /// .globaltype
  virtual void emitGlobalType(const MCSymbolWasm *Sym) = 0;
  /// .eventtype
  virtual void emitEventType(const MCSymbolWasm *Sym) = 0;
  /// .import_module
  virtual void emitImportModule(const MCSymbolWasm *Sym,
                                StringRef ImportModule) = 0;
  /// .import_name
  virtual void emitImportName(const MCSymbolWasm *Sym,
                              StringRef ImportName) = 0;

protected:
  void emitValueType(wasm::ValType Type);
};

/// This part is for ascii assembly output
class WebAssemblyTargetAsmStreamer final : public WebAssemblyTargetStreamer {
  formatted_raw_ostream &OS;
  void emitSignature(const wasm::WasmSignature *Sig);
  void emitParamList(const wasm::WasmSignature *Sig);
  void emitReturnList(const wasm::WasmSignature *Sig);

public:
  WebAssemblyTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitLocal(ArrayRef<wasm::ValType> Types) override;
  void emitEndFunc() override;
  void emitFunctionType(const MCSymbolWasm *Sym) override;
  void emitIndIdx(const MCExpr *Value) override;
  void emitGlobalType(const MCSymbolWasm *Sym) override;
  void emitEventType(const MCSymbolWasm *Sym) override;
  void emitImportModule(const MCSymbolWasm *Sym, StringRef ImportModule) override;
  void emitImportName(const MCSymbolWasm *Sym, StringRef ImportName) override;
};

/// This part is for Wasm object output
class WebAssemblyTargetWasmStreamer final : public WebAssemblyTargetStreamer {
public:
  explicit WebAssemblyTargetWasmStreamer(MCStreamer &S);

  void emitLocal(ArrayRef<wasm::ValType> Types) override;
  void emitEndFunc() override;
  void emitFunctionType(const MCSymbolWasm *Sym) override {}
  void emitIndIdx(const MCExpr *Value) override;
  void emitGlobalType(const MCSymbolWasm *Sym) override {}
  void emitEventType(const MCSymbolWasm *Sym) override {}
  void emitImportModule(const MCSymbolWasm *Sym,
                        StringRef ImportModule) override {}
  void emitImportName(const MCSymbolWasm *Sym,
                      StringRef ImportName) override {}
};

/// This part is for null output
class WebAssemblyTargetNullStreamer final : public WebAssemblyTargetStreamer {
public:
  explicit WebAssemblyTargetNullStreamer(MCStreamer &S)
      : WebAssemblyTargetStreamer(S) {}

  void emitLocal(ArrayRef<wasm::ValType>) override {}
  void emitEndFunc() override {}
  void emitFunctionType(const MCSymbolWasm *) override {}
  void emitIndIdx(const MCExpr *) override {}
  void emitGlobalType(const MCSymbolWasm *) override {}
  void emitEventType(const MCSymbolWasm *) override {}
  void emitImportModule(const MCSymbolWasm *, StringRef) override {}
  void emitImportName(const MCSymbolWasm *, StringRef) override {}
};

} // end namespace llvm

#endif
