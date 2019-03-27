//===-- RecordStreamer.cpp - Record asm defined and used symbols ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RecordStreamer.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"

using namespace llvm;

void RecordStreamer::markDefined(const MCSymbol &Symbol) {
  State &S = Symbols[Symbol.getName()];
  switch (S) {
  case DefinedGlobal:
  case Global:
    S = DefinedGlobal;
    break;
  case NeverSeen:
  case Defined:
  case Used:
    S = Defined;
    break;
  case DefinedWeak:
    break;
  case UndefinedWeak:
    S = DefinedWeak;
  }
}

void RecordStreamer::markGlobal(const MCSymbol &Symbol,
                                MCSymbolAttr Attribute) {
  State &S = Symbols[Symbol.getName()];
  switch (S) {
  case DefinedGlobal:
  case Defined:
    S = (Attribute == MCSA_Weak) ? DefinedWeak : DefinedGlobal;
    break;

  case NeverSeen:
  case Global:
  case Used:
    S = (Attribute == MCSA_Weak) ? UndefinedWeak : Global;
    break;
  case UndefinedWeak:
  case DefinedWeak:
    break;
  }
}

void RecordStreamer::markUsed(const MCSymbol &Symbol) {
  State &S = Symbols[Symbol.getName()];
  switch (S) {
  case DefinedGlobal:
  case Defined:
  case Global:
  case DefinedWeak:
  case UndefinedWeak:
    break;

  case NeverSeen:
  case Used:
    S = Used;
    break;
  }
}

void RecordStreamer::visitUsedSymbol(const MCSymbol &Sym) { markUsed(Sym); }

RecordStreamer::RecordStreamer(MCContext &Context, const Module &M)
    : MCStreamer(Context), M(M) {}

RecordStreamer::const_iterator RecordStreamer::begin() {
  return Symbols.begin();
}

RecordStreamer::const_iterator RecordStreamer::end() { return Symbols.end(); }

void RecordStreamer::EmitInstruction(const MCInst &Inst,
                                     const MCSubtargetInfo &STI, bool) {
  MCStreamer::EmitInstruction(Inst, STI);
}

void RecordStreamer::EmitLabel(MCSymbol *Symbol, SMLoc Loc) {
  MCStreamer::EmitLabel(Symbol);
  markDefined(*Symbol);
}

void RecordStreamer::EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  markDefined(*Symbol);
  MCStreamer::EmitAssignment(Symbol, Value);
}

bool RecordStreamer::EmitSymbolAttribute(MCSymbol *Symbol,
                                         MCSymbolAttr Attribute) {
  if (Attribute == MCSA_Global || Attribute == MCSA_Weak)
    markGlobal(*Symbol, Attribute);
  if (Attribute == MCSA_LazyReference)
    markUsed(*Symbol);
  return true;
}

void RecordStreamer::EmitZerofill(MCSection *Section, MCSymbol *Symbol,
                                  uint64_t Size, unsigned ByteAlignment,
                                  SMLoc Loc) {
  markDefined(*Symbol);
}

void RecordStreamer::EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                      unsigned ByteAlignment) {
  markDefined(*Symbol);
}

RecordStreamer::State RecordStreamer::getSymbolState(const MCSymbol *Sym) {
  auto SI = Symbols.find(Sym->getName());
  if (SI == Symbols.end())
    return NeverSeen;
  return SI->second;
}

void RecordStreamer::emitELFSymverDirective(StringRef AliasName,
                                            const MCSymbol *Aliasee) {
  SymverAliasMap[Aliasee].push_back(AliasName);
}

iterator_range<RecordStreamer::const_symver_iterator>
RecordStreamer::symverAliases() {
  return {SymverAliasMap.begin(), SymverAliasMap.end()};
}

void RecordStreamer::flushSymverDirectives() {
  // Mapping from mangled name to GV.
  StringMap<const GlobalValue *> MangledNameMap;
  // The name in the assembler will be mangled, but the name in the IR
  // might not, so we first compute a mapping from mangled name to GV.
  Mangler Mang;
  SmallString<64> MangledName;
  for (const GlobalValue &GV : M.global_values()) {
    if (!GV.hasName())
      continue;
    MangledName.clear();
    MangledName.reserve(GV.getName().size() + 1);
    Mang.getNameWithPrefix(MangledName, &GV, /*CannotUsePrivateLabel=*/false);
    MangledNameMap[MangledName] = &GV;
  }

  // Walk all the recorded .symver aliases, and set up the binding
  // for each alias.
  for (auto &Symver : SymverAliasMap) {
    const MCSymbol *Aliasee = Symver.first;
    MCSymbolAttr Attr = MCSA_Invalid;
    bool IsDefined = false;

    // First check if the aliasee binding was recorded in the asm.
    RecordStreamer::State state = getSymbolState(Aliasee);
    switch (state) {
    case RecordStreamer::Global:
    case RecordStreamer::DefinedGlobal:
      Attr = MCSA_Global;
      break;
    case RecordStreamer::UndefinedWeak:
    case RecordStreamer::DefinedWeak:
      Attr = MCSA_Weak;
      break;
    default:
      break;
    }

    switch (state) {
    case RecordStreamer::Defined:
    case RecordStreamer::DefinedGlobal:
    case RecordStreamer::DefinedWeak:
      IsDefined = true;
      break;
    case RecordStreamer::NeverSeen:
    case RecordStreamer::Global:
    case RecordStreamer::Used:
    case RecordStreamer::UndefinedWeak:
      break;
    }

    if (Attr == MCSA_Invalid || !IsDefined) {
      const GlobalValue *GV = M.getNamedValue(Aliasee->getName());
      if (!GV) {
        auto MI = MangledNameMap.find(Aliasee->getName());
        if (MI != MangledNameMap.end())
          GV = MI->second;
      }
      if (GV) {
        // If we don't have a symbol attribute from assembly, then check if
        // the aliasee was defined in the IR.
        if (Attr == MCSA_Invalid) {
          if (GV->hasExternalLinkage())
            Attr = MCSA_Global;
          else if (GV->hasLocalLinkage())
            Attr = MCSA_Local;
          else if (GV->isWeakForLinker())
            Attr = MCSA_Weak;
        }
        IsDefined = IsDefined || !GV->isDeclarationForLinker();
      }
    }

    // Set the detected binding on each alias with this aliasee.
    for (auto AliasName : Symver.second) {
      std::pair<StringRef, StringRef> Split = AliasName.split("@@@");
      SmallString<128> NewName;
      if (!Split.second.empty() && !Split.second.startswith("@")) {
        // Special processing for "@@@" according
        // https://sourceware.org/binutils/docs/as/Symver.html
        const char *Separator = IsDefined ? "@@" : "@";
        AliasName =
            (Split.first + Separator + Split.second).toStringRef(NewName);
      }
      MCSymbol *Alias = getContext().getOrCreateSymbol(AliasName);
      // TODO: Handle "@@@". Depending on SymbolAttribute value it needs to be
      // converted into @ or @@.
      const MCExpr *Value = MCSymbolRefExpr::create(Aliasee, getContext());
      if (IsDefined)
        markDefined(*Alias);
      // Don't use EmitAssignment override as it always marks alias as defined.
      MCStreamer::EmitAssignment(Alias, Value);
      if (Attr != MCSA_Invalid)
        EmitSymbolAttribute(Alias, Attr);
    }
  }
}
