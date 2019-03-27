//===- RecordStreamer.h - Record asm defined and used symbols ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJECT_RECORDSTREAMER_H
#define LLVM_LIB_OBJECT_RECORDSTREAMER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {

class GlobalValue;
class Module;

class RecordStreamer : public MCStreamer {
public:
  enum State { NeverSeen, Global, Defined, DefinedGlobal, DefinedWeak, Used,
               UndefinedWeak};

private:
  const Module &M;
  StringMap<State> Symbols;
  // Map of aliases created by .symver directives, saved so we can update
  // their symbol binding after parsing complete. This maps from each
  // aliasee to its list of aliases.
  DenseMap<const MCSymbol *, std::vector<StringRef>> SymverAliasMap;

  /// Get the state recorded for the given symbol.
  State getSymbolState(const MCSymbol *Sym);

  void markDefined(const MCSymbol &Symbol);
  void markGlobal(const MCSymbol &Symbol, MCSymbolAttr Attribute);
  void markUsed(const MCSymbol &Symbol);
  void visitUsedSymbol(const MCSymbol &Sym) override;

public:
  RecordStreamer(MCContext &Context, const Module &M);

  void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                       bool) override;
  void EmitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  void EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  bool EmitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  void EmitZerofill(MCSection *Section, MCSymbol *Symbol, uint64_t Size,
                    unsigned ByteAlignment, SMLoc Loc = SMLoc()) override;
  void EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        unsigned ByteAlignment) override;
  /// Record .symver aliases for later processing.
  void emitELFSymverDirective(StringRef AliasName,
                              const MCSymbol *Aliasee) override;

  // Emit ELF .symver aliases and ensure they have the same binding as the
  // defined symbol they alias with.
  void flushSymverDirectives();

  // Symbols iterators
  using const_iterator = StringMap<State>::const_iterator;
  const_iterator begin();
  const_iterator end();

  // SymverAliasMap iterators
  using const_symver_iterator = decltype(SymverAliasMap)::const_iterator;
  iterator_range<const_symver_iterator> symverAliases();
};

} // end namespace llvm

#endif // LLVM_LIB_OBJECT_RECORDSTREAMER_H
