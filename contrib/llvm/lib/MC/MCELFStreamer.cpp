//===- lib/MC/MCELFStreamer.cpp - ELF Object Output -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits ELF .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

MCELFStreamer::MCELFStreamer(MCContext &Context,
                             std::unique_ptr<MCAsmBackend> TAB,
                             std::unique_ptr<MCObjectWriter> OW,
                             std::unique_ptr<MCCodeEmitter> Emitter)
    : MCObjectStreamer(Context, std::move(TAB), std::move(OW),
                       std::move(Emitter)) {}

bool MCELFStreamer::isBundleLocked() const {
  return getCurrentSectionOnly()->isBundleLocked();
}

void MCELFStreamer::mergeFragment(MCDataFragment *DF,
                                  MCDataFragment *EF) {
  MCAssembler &Assembler = getAssembler();

  if (Assembler.isBundlingEnabled() && Assembler.getRelaxAll()) {
    uint64_t FSize = EF->getContents().size();

    if (FSize > Assembler.getBundleAlignSize())
      report_fatal_error("Fragment can't be larger than a bundle size");

    uint64_t RequiredBundlePadding = computeBundlePadding(
        Assembler, EF, DF->getContents().size(), FSize);

    if (RequiredBundlePadding > UINT8_MAX)
      report_fatal_error("Padding cannot exceed 255 bytes");

    if (RequiredBundlePadding > 0) {
      SmallString<256> Code;
      raw_svector_ostream VecOS(Code);
      EF->setBundlePadding(static_cast<uint8_t>(RequiredBundlePadding));
      Assembler.writeFragmentPadding(VecOS, *EF, FSize);

      DF->getContents().append(Code.begin(), Code.end());
    }
  }

  flushPendingLabels(DF, DF->getContents().size());

  for (unsigned i = 0, e = EF->getFixups().size(); i != e; ++i) {
    EF->getFixups()[i].setOffset(EF->getFixups()[i].getOffset() +
                                 DF->getContents().size());
    DF->getFixups().push_back(EF->getFixups()[i]);
  }
  if (DF->getSubtargetInfo() == nullptr && EF->getSubtargetInfo())
    DF->setHasInstructions(*EF->getSubtargetInfo());
  DF->getContents().append(EF->getContents().begin(), EF->getContents().end());
}

void MCELFStreamer::InitSections(bool NoExecStack) {
  MCContext &Ctx = getContext();
  SwitchSection(Ctx.getObjectFileInfo()->getTextSection());
  EmitCodeAlignment(4);

  if (NoExecStack)
    SwitchSection(Ctx.getAsmInfo()->getNonexecutableStackSection(Ctx));
}

void MCELFStreamer::EmitLabel(MCSymbol *S, SMLoc Loc) {
  auto *Symbol = cast<MCSymbolELF>(S);
  MCObjectStreamer::EmitLabel(Symbol, Loc);

  const MCSectionELF &Section =
      static_cast<const MCSectionELF &>(*getCurrentSectionOnly());
  if (Section.getFlags() & ELF::SHF_TLS)
    Symbol->setType(ELF::STT_TLS);
}

void MCELFStreamer::EmitLabel(MCSymbol *S, SMLoc Loc, MCFragment *F) {
  auto *Symbol = cast<MCSymbolELF>(S);
  MCObjectStreamer::EmitLabel(Symbol, Loc, F);

  const MCSectionELF &Section =
      static_cast<const MCSectionELF &>(*getCurrentSectionOnly());
  if (Section.getFlags() & ELF::SHF_TLS)
    Symbol->setType(ELF::STT_TLS);
}

void MCELFStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);
  // Do any generic stuff we need to do.
  switch (Flag) {
  case MCAF_SyntaxUnified: return; // no-op here.
  case MCAF_Code16: return; // Change parsing mode; no-op here.
  case MCAF_Code32: return; // Change parsing mode; no-op here.
  case MCAF_Code64: return; // Change parsing mode; no-op here.
  case MCAF_SubsectionsViaSymbols:
    getAssembler().setSubsectionsViaSymbols(true);
    return;
  }

  llvm_unreachable("invalid assembler flag!");
}

// If bundle alignment is used and there are any instructions in the section, it
// needs to be aligned to at least the bundle size.
static void setSectionAlignmentForBundling(const MCAssembler &Assembler,
                                           MCSection *Section) {
  if (Section && Assembler.isBundlingEnabled() && Section->hasInstructions() &&
      Section->getAlignment() < Assembler.getBundleAlignSize())
    Section->setAlignment(Assembler.getBundleAlignSize());
}

void MCELFStreamer::ChangeSection(MCSection *Section,
                                  const MCExpr *Subsection) {
  MCSection *CurSection = getCurrentSectionOnly();
  if (CurSection && isBundleLocked())
    report_fatal_error("Unterminated .bundle_lock when changing a section");

  MCAssembler &Asm = getAssembler();
  // Ensure the previous section gets aligned if necessary.
  setSectionAlignmentForBundling(Asm, CurSection);
  auto *SectionELF = static_cast<const MCSectionELF *>(Section);
  const MCSymbol *Grp = SectionELF->getGroup();
  if (Grp)
    Asm.registerSymbol(*Grp);

  changeSectionImpl(Section, Subsection);
  Asm.registerSymbol(*Section->getBeginSymbol());
}

void MCELFStreamer::EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {
  getAssembler().registerSymbol(*Symbol);
  const MCExpr *Value = MCSymbolRefExpr::create(
      Symbol, MCSymbolRefExpr::VK_WEAKREF, getContext());
  Alias->setVariableValue(Value);
}

// When GNU as encounters more than one .type declaration for an object it seems
// to use a mechanism similar to the one below to decide which type is actually
// used in the object file.  The greater of T1 and T2 is selected based on the
// following ordering:
//  STT_NOTYPE < STT_OBJECT < STT_FUNC < STT_GNU_IFUNC < STT_TLS < anything else
// If neither T1 < T2 nor T2 < T1 according to this ordering, use T2 (the user
// provided type).
static unsigned CombineSymbolTypes(unsigned T1, unsigned T2) {
  for (unsigned Type : {ELF::STT_NOTYPE, ELF::STT_OBJECT, ELF::STT_FUNC,
                        ELF::STT_GNU_IFUNC, ELF::STT_TLS}) {
    if (T1 == Type)
      return T2;
    if (T2 == Type)
      return T1;
  }

  return T2;
}

bool MCELFStreamer::EmitSymbolAttribute(MCSymbol *S, MCSymbolAttr Attribute) {
  auto *Symbol = cast<MCSymbolELF>(S);

  // Adding a symbol attribute always introduces the symbol, note that an
  // important side effect of calling registerSymbol here is to register
  // the symbol with the assembler.
  getAssembler().registerSymbol(*Symbol);

  // The implementation of symbol attributes is designed to match 'as', but it
  // leaves much to desired. It doesn't really make sense to arbitrarily add and
  // remove flags, but 'as' allows this (in particular, see .desc).
  //
  // In the future it might be worth trying to make these operations more well
  // defined.
  switch (Attribute) {
  case MCSA_LazyReference:
  case MCSA_Reference:
  case MCSA_SymbolResolver:
  case MCSA_PrivateExtern:
  case MCSA_WeakDefinition:
  case MCSA_WeakDefAutoPrivate:
  case MCSA_Invalid:
  case MCSA_IndirectSymbol:
    return false;

  case MCSA_NoDeadStrip:
    // Ignore for now.
    break;

  case MCSA_ELF_TypeGnuUniqueObject:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_OBJECT));
    Symbol->setBinding(ELF::STB_GNU_UNIQUE);
    Symbol->setExternal(true);
    break;

  case MCSA_Global:
    Symbol->setBinding(ELF::STB_GLOBAL);
    Symbol->setExternal(true);
    break;

  case MCSA_WeakReference:
  case MCSA_Weak:
    Symbol->setBinding(ELF::STB_WEAK);
    Symbol->setExternal(true);
    break;

  case MCSA_Local:
    Symbol->setBinding(ELF::STB_LOCAL);
    Symbol->setExternal(false);
    break;

  case MCSA_ELF_TypeFunction:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_FUNC));
    break;

  case MCSA_ELF_TypeIndFunction:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_GNU_IFUNC));
    break;

  case MCSA_ELF_TypeObject:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_OBJECT));
    break;

  case MCSA_ELF_TypeTLS:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_TLS));
    break;

  case MCSA_ELF_TypeCommon:
    // TODO: Emit these as a common symbol.
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_OBJECT));
    break;

  case MCSA_ELF_TypeNoType:
    Symbol->setType(CombineSymbolTypes(Symbol->getType(), ELF::STT_NOTYPE));
    break;

  case MCSA_Protected:
    Symbol->setVisibility(ELF::STV_PROTECTED);
    break;

  case MCSA_Hidden:
    Symbol->setVisibility(ELF::STV_HIDDEN);
    break;

  case MCSA_Internal:
    Symbol->setVisibility(ELF::STV_INTERNAL);
    break;

  case MCSA_AltEntry:
    llvm_unreachable("ELF doesn't support the .alt_entry attribute");
  }

  return true;
}

void MCELFStreamer::EmitCommonSymbol(MCSymbol *S, uint64_t Size,
                                     unsigned ByteAlignment) {
  auto *Symbol = cast<MCSymbolELF>(S);
  getAssembler().registerSymbol(*Symbol);

  if (!Symbol->isBindingSet()) {
    Symbol->setBinding(ELF::STB_GLOBAL);
    Symbol->setExternal(true);
  }

  Symbol->setType(ELF::STT_OBJECT);

  if (Symbol->getBinding() == ELF::STB_LOCAL) {
    MCSection &Section = *getAssembler().getContext().getELFSection(
        ".bss", ELF::SHT_NOBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
    MCSectionSubPair P = getCurrentSection();
    SwitchSection(&Section);

    EmitValueToAlignment(ByteAlignment, 0, 1, 0);
    EmitLabel(Symbol);
    EmitZeros(Size);

    // Update the maximum alignment of the section if necessary.
    if (ByteAlignment > Section.getAlignment())
      Section.setAlignment(ByteAlignment);

    SwitchSection(P.first, P.second);
  } else {
    if(Symbol->declareCommon(Size, ByteAlignment))
      report_fatal_error("Symbol: " + Symbol->getName() +
                         " redeclared as different type");
  }

  cast<MCSymbolELF>(Symbol)
      ->setSize(MCConstantExpr::create(Size, getContext()));
}

void MCELFStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  cast<MCSymbolELF>(Symbol)->setSize(Value);
}

void MCELFStreamer::emitELFSymverDirective(StringRef AliasName,
                                           const MCSymbol *Aliasee) {
  getAssembler().Symvers.push_back({AliasName, Aliasee});
}

void MCELFStreamer::EmitLocalCommonSymbol(MCSymbol *S, uint64_t Size,
                                          unsigned ByteAlignment) {
  auto *Symbol = cast<MCSymbolELF>(S);
  // FIXME: Should this be caught and done earlier?
  getAssembler().registerSymbol(*Symbol);
  Symbol->setBinding(ELF::STB_LOCAL);
  Symbol->setExternal(false);
  EmitCommonSymbol(Symbol, Size, ByteAlignment);
}

void MCELFStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size,
                                  SMLoc Loc) {
  if (isBundleLocked())
    report_fatal_error("Emitting values inside a locked bundle is forbidden");
  fixSymbolsInTLSFixups(Value);
  MCObjectStreamer::EmitValueImpl(Value, Size, Loc);
}

void MCELFStreamer::EmitValueToAlignment(unsigned ByteAlignment,
                                         int64_t Value,
                                         unsigned ValueSize,
                                         unsigned MaxBytesToEmit) {
  if (isBundleLocked())
    report_fatal_error("Emitting values inside a locked bundle is forbidden");
  MCObjectStreamer::EmitValueToAlignment(ByteAlignment, Value,
                                         ValueSize, MaxBytesToEmit);
}

void MCELFStreamer::emitCGProfileEntry(const MCSymbolRefExpr *From,
                                       const MCSymbolRefExpr *To,
                                       uint64_t Count) {
  getAssembler().CGProfile.push_back({From, To, Count});
}

void MCELFStreamer::EmitIdent(StringRef IdentString) {
  MCSection *Comment = getAssembler().getContext().getELFSection(
      ".comment", ELF::SHT_PROGBITS, ELF::SHF_MERGE | ELF::SHF_STRINGS, 1, "");
  PushSection();
  SwitchSection(Comment);
  if (!SeenIdent) {
    EmitIntValue(0, 1);
    SeenIdent = true;
  }
  EmitBytes(IdentString);
  EmitIntValue(0, 1);
  PopSection();
}

void MCELFStreamer::fixSymbolsInTLSFixups(const MCExpr *expr) {
  switch (expr->getKind()) {
  case MCExpr::Target:
    cast<MCTargetExpr>(expr)->fixELFSymbolsInTLSFixups(getAssembler());
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *be = cast<MCBinaryExpr>(expr);
    fixSymbolsInTLSFixups(be->getLHS());
    fixSymbolsInTLSFixups(be->getRHS());
    break;
  }

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &symRef = *cast<MCSymbolRefExpr>(expr);
    switch (symRef.getKind()) {
    default:
      return;
    case MCSymbolRefExpr::VK_GOTTPOFF:
    case MCSymbolRefExpr::VK_INDNTPOFF:
    case MCSymbolRefExpr::VK_NTPOFF:
    case MCSymbolRefExpr::VK_GOTNTPOFF:
    case MCSymbolRefExpr::VK_TLSGD:
    case MCSymbolRefExpr::VK_TLSLD:
    case MCSymbolRefExpr::VK_TLSLDM:
    case MCSymbolRefExpr::VK_TPOFF:
    case MCSymbolRefExpr::VK_TPREL:
    case MCSymbolRefExpr::VK_DTPOFF:
    case MCSymbolRefExpr::VK_DTPREL:
    case MCSymbolRefExpr::VK_PPC_DTPMOD:
    case MCSymbolRefExpr::VK_PPC_TPREL_LO:
    case MCSymbolRefExpr::VK_PPC_TPREL_HI:
    case MCSymbolRefExpr::VK_PPC_TPREL_HA:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGH:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHA:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHER:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHERA:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHEST:
    case MCSymbolRefExpr::VK_PPC_TPREL_HIGHESTA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_LO:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HI:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGH:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHER:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHERA:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHEST:
    case MCSymbolRefExpr::VK_PPC_DTPREL_HIGHESTA:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TPREL_HA:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_DTPREL_HA:
    case MCSymbolRefExpr::VK_PPC_TLS:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSGD_HA:
    case MCSymbolRefExpr::VK_PPC_TLSGD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_LO:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_HI:
    case MCSymbolRefExpr::VK_PPC_GOT_TLSLD_HA:
    case MCSymbolRefExpr::VK_PPC_TLSLD:
      break;
    }
    getAssembler().registerSymbol(symRef.getSymbol());
    cast<MCSymbolELF>(symRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixSymbolsInTLSFixups(cast<MCUnaryExpr>(expr)->getSubExpr());
    break;
  }
}

void MCELFStreamer::finalizeCGProfileEntry(const MCSymbolRefExpr *&SRE) {
  const MCSymbol *S = &SRE->getSymbol();
  if (S->isTemporary()) {
    if (!S->isInSection()) {
      getContext().reportError(
          SRE->getLoc(), Twine("Reference to undefined temporary symbol ") +
                             "`" + S->getName() + "`");
      return;
    }
    S = S->getSection().getBeginSymbol();
    S->setUsedInReloc();
    SRE =
        MCSymbolRefExpr::create(S, SRE->getKind(), getContext(), SRE->getLoc());
    return;
  }
  // Not a temporary, referece it as a weak undefined.
  bool Created;
  getAssembler().registerSymbol(*S, &Created);
  if (Created) {
    cast<MCSymbolELF>(S)->setBinding(ELF::STB_WEAK);
    cast<MCSymbolELF>(S)->setExternal(true);
  }
}

void MCELFStreamer::finalizeCGProfile() {
  for (MCAssembler::CGProfileEntry &E : getAssembler().CGProfile) {
    finalizeCGProfileEntry(E.From);
    finalizeCGProfileEntry(E.To);
  }
}

void MCELFStreamer::EmitInstToFragment(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  this->MCObjectStreamer::EmitInstToFragment(Inst, STI);
  MCRelaxableFragment &F = *cast<MCRelaxableFragment>(getCurrentFragment());

  for (unsigned i = 0, e = F.getFixups().size(); i != e; ++i)
    fixSymbolsInTLSFixups(F.getFixups()[i].getValue());
}

// A fragment can only have one Subtarget, and when bundling is enabled we
// sometimes need to use the same fragment. We give an error if there
// are conflicting Subtargets.
static void CheckBundleSubtargets(const MCSubtargetInfo *OldSTI,
                                  const MCSubtargetInfo *NewSTI) {
  if (OldSTI && NewSTI && OldSTI != NewSTI)
    report_fatal_error("A Bundle can only have one Subtarget.");
}

void MCELFStreamer::EmitInstToData(const MCInst &Inst,
                                   const MCSubtargetInfo &STI) {
  MCAssembler &Assembler = getAssembler();
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  raw_svector_ostream VecOS(Code);
  Assembler.getEmitter().encodeInstruction(Inst, VecOS, Fixups, STI);

  for (unsigned i = 0, e = Fixups.size(); i != e; ++i)
    fixSymbolsInTLSFixups(Fixups[i].getValue());

  // There are several possibilities here:
  //
  // If bundling is disabled, append the encoded instruction to the current data
  // fragment (or create a new such fragment if the current fragment is not a
  // data fragment, or the Subtarget has changed).
  //
  // If bundling is enabled:
  // - If we're not in a bundle-locked group, emit the instruction into a
  //   fragment of its own. If there are no fixups registered for the
  //   instruction, emit a MCCompactEncodedInstFragment. Otherwise, emit a
  //   MCDataFragment.
  // - If we're in a bundle-locked group, append the instruction to the current
  //   data fragment because we want all the instructions in a group to get into
  //   the same fragment. Be careful not to do that for the first instruction in
  //   the group, though.
  MCDataFragment *DF;

  if (Assembler.isBundlingEnabled()) {
    MCSection &Sec = *getCurrentSectionOnly();
    if (Assembler.getRelaxAll() && isBundleLocked()) {
      // If the -mc-relax-all flag is used and we are bundle-locked, we re-use
      // the current bundle group.
      DF = BundleGroups.back();
      CheckBundleSubtargets(DF->getSubtargetInfo(), &STI);
    }
    else if (Assembler.getRelaxAll() && !isBundleLocked())
      // When not in a bundle-locked group and the -mc-relax-all flag is used,
      // we create a new temporary fragment which will be later merged into
      // the current fragment.
      DF = new MCDataFragment();
    else if (isBundleLocked() && !Sec.isBundleGroupBeforeFirstInst()) {
      // If we are bundle-locked, we re-use the current fragment.
      // The bundle-locking directive ensures this is a new data fragment.
      DF = cast<MCDataFragment>(getCurrentFragment());
      CheckBundleSubtargets(DF->getSubtargetInfo(), &STI);
    }
    else if (!isBundleLocked() && Fixups.size() == 0) {
      // Optimize memory usage by emitting the instruction to a
      // MCCompactEncodedInstFragment when not in a bundle-locked group and
      // there are no fixups registered.
      MCCompactEncodedInstFragment *CEIF = new MCCompactEncodedInstFragment();
      insert(CEIF);
      CEIF->getContents().append(Code.begin(), Code.end());
      CEIF->setHasInstructions(STI);
      return;
    } else {
      DF = new MCDataFragment();
      insert(DF);
    }
    if (Sec.getBundleLockState() == MCSection::BundleLockedAlignToEnd) {
      // If this fragment is for a group marked "align_to_end", set a flag
      // in the fragment. This can happen after the fragment has already been
      // created if there are nested bundle_align groups and an inner one
      // is the one marked align_to_end.
      DF->setAlignToBundleEnd(true);
    }

    // We're now emitting an instruction in a bundle group, so this flag has
    // to be turned off.
    Sec.setBundleGroupBeforeFirstInst(false);
  } else {
    DF = getOrCreateDataFragment(&STI);
  }

  // Add the fixups and data.
  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    Fixups[i].setOffset(Fixups[i].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[i]);
  }
  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());

  if (Assembler.isBundlingEnabled() && Assembler.getRelaxAll()) {
    if (!isBundleLocked()) {
      mergeFragment(getOrCreateDataFragment(&STI), DF);
      delete DF;
    }
  }
}

void MCELFStreamer::EmitBundleAlignMode(unsigned AlignPow2) {
  assert(AlignPow2 <= 30 && "Invalid bundle alignment");
  MCAssembler &Assembler = getAssembler();
  if (AlignPow2 > 0 && (Assembler.getBundleAlignSize() == 0 ||
                        Assembler.getBundleAlignSize() == 1U << AlignPow2))
    Assembler.setBundleAlignSize(1U << AlignPow2);
  else
    report_fatal_error(".bundle_align_mode cannot be changed once set");
}

void MCELFStreamer::EmitBundleLock(bool AlignToEnd) {
  MCSection &Sec = *getCurrentSectionOnly();

  // Sanity checks
  //
  if (!getAssembler().isBundlingEnabled())
    report_fatal_error(".bundle_lock forbidden when bundling is disabled");

  if (!isBundleLocked())
    Sec.setBundleGroupBeforeFirstInst(true);

  if (getAssembler().getRelaxAll() && !isBundleLocked()) {
    // TODO: drop the lock state and set directly in the fragment
    MCDataFragment *DF = new MCDataFragment();
    BundleGroups.push_back(DF);
  }

  Sec.setBundleLockState(AlignToEnd ? MCSection::BundleLockedAlignToEnd
                                    : MCSection::BundleLocked);
}

void MCELFStreamer::EmitBundleUnlock() {
  MCSection &Sec = *getCurrentSectionOnly();

  // Sanity checks
  if (!getAssembler().isBundlingEnabled())
    report_fatal_error(".bundle_unlock forbidden when bundling is disabled");
  else if (!isBundleLocked())
    report_fatal_error(".bundle_unlock without matching lock");
  else if (Sec.isBundleGroupBeforeFirstInst())
    report_fatal_error("Empty bundle-locked group is forbidden");

  // When the -mc-relax-all flag is used, we emit instructions to fragments
  // stored on a stack. When the bundle unlock is emitted, we pop a fragment
  // from the stack a merge it to the one below.
  if (getAssembler().getRelaxAll()) {
    assert(!BundleGroups.empty() && "There are no bundle groups");
    MCDataFragment *DF = BundleGroups.back();

    // FIXME: Use BundleGroups to track the lock state instead.
    Sec.setBundleLockState(MCSection::NotBundleLocked);

    // FIXME: Use more separate fragments for nested groups.
    if (!isBundleLocked()) {
      mergeFragment(getOrCreateDataFragment(DF->getSubtargetInfo()), DF);
      BundleGroups.pop_back();
      delete DF;
    }

    if (Sec.getBundleLockState() != MCSection::BundleLockedAlignToEnd)
      getOrCreateDataFragment()->setAlignToBundleEnd(false);
  } else
    Sec.setBundleLockState(MCSection::NotBundleLocked);
}

void MCELFStreamer::FinishImpl() {
  // Ensure the last section gets aligned if necessary.
  MCSection *CurSection = getCurrentSectionOnly();
  setSectionAlignmentForBundling(getAssembler(), CurSection);

  finalizeCGProfile();
  EmitFrames(nullptr);

  this->MCObjectStreamer::FinishImpl();
}

void MCELFStreamer::EmitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("Generic ELF doesn't support this directive");
}

void MCELFStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitZerofill(MCSection *Section, MCSymbol *Symbol,
                                 uint64_t Size, unsigned ByteAlignment,
                                 SMLoc Loc) {
  llvm_unreachable("ELF doesn't support this directive");
}

void MCELFStreamer::EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, unsigned ByteAlignment) {
  llvm_unreachable("ELF doesn't support this directive");
}

MCStreamer *llvm::createELFStreamer(MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&CE,
                                    bool RelaxAll) {
  MCELFStreamer *S =
      new MCELFStreamer(Context, std::move(MAB), std::move(OW), std::move(CE));
  if (RelaxAll)
    S->getAssembler().setRelaxAll(true);
  return S;
}
