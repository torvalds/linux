//===- llvm/CodeGen/DwarfCompileUnit.cpp - Dwarf Compile Units ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for constructing a dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#include "DwarfCompileUnit.h"
#include "AddressPool.h"
#include "DwarfDebug.h"
#include "DwarfExpression.h"
#include "DwarfUnit.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Casting.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

using namespace llvm;

DwarfCompileUnit::DwarfCompileUnit(unsigned UID, const DICompileUnit *Node,
                                   AsmPrinter *A, DwarfDebug *DW,
                                   DwarfFile *DWU)
    : DwarfUnit(dwarf::DW_TAG_compile_unit, Node, A, DW, DWU), UniqueID(UID) {
  insertDIE(Node, &getUnitDie());
  MacroLabelBegin = Asm->createTempSymbol("cu_macro_begin");
}

/// addLabelAddress - Add a dwarf label attribute data and value using
/// DW_FORM_addr or DW_FORM_GNU_addr_index.
void DwarfCompileUnit::addLabelAddress(DIE &Die, dwarf::Attribute Attribute,
                                       const MCSymbol *Label) {
  // Don't use the address pool in non-fission or in the skeleton unit itself.
  // FIXME: Once GDB supports this, it's probably worthwhile using the address
  // pool from the skeleton - maybe even in non-fission (possibly fewer
  // relocations by sharing them in the pool, but we have other ideas about how
  // to reduce the number of relocations as well/instead).
  if ((!DD->useSplitDwarf() || !Skeleton) && DD->getDwarfVersion() < 5)
    return addLocalLabelAddress(Die, Attribute, Label);

  if (Label)
    DD->addArangeLabel(SymbolCU(this, Label));

  unsigned idx = DD->getAddressPool().getIndex(Label);
  Die.addValue(DIEValueAllocator, Attribute,
               DD->getDwarfVersion() >= 5 ? dwarf::DW_FORM_addrx
                                          : dwarf::DW_FORM_GNU_addr_index,
               DIEInteger(idx));
}

void DwarfCompileUnit::addLocalLabelAddress(DIE &Die,
                                            dwarf::Attribute Attribute,
                                            const MCSymbol *Label) {
  if (Label)
    DD->addArangeLabel(SymbolCU(this, Label));

  if (Label)
    Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_addr,
                 DIELabel(Label));
  else
    Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_addr,
                 DIEInteger(0));
}

unsigned DwarfCompileUnit::getOrCreateSourceID(const DIFile *File) {
  // If we print assembly, we can't separate .file entries according to
  // compile units. Thus all files will belong to the default compile unit.

  // FIXME: add a better feature test than hasRawTextSupport. Even better,
  // extend .file to support this.
  unsigned CUID = Asm->OutStreamer->hasRawTextSupport() ? 0 : getUniqueID();
  if (!File)
    return Asm->OutStreamer->EmitDwarfFileDirective(0, "", "", nullptr, None, CUID);
  return Asm->OutStreamer->EmitDwarfFileDirective(
      0, File->getDirectory(), File->getFilename(), getMD5AsBytes(File),
      File->getSource(), CUID);
}

DIE *DwarfCompileUnit::getOrCreateGlobalVariableDIE(
    const DIGlobalVariable *GV, ArrayRef<GlobalExpr> GlobalExprs) {
  // Check for pre-existence.
  if (DIE *Die = getDIE(GV))
    return Die;

  assert(GV);

  auto *GVContext = GV->getScope();
  auto *GTy = DD->resolve(GV->getType());

  // Construct the context before querying for the existence of the DIE in
  // case such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(GVContext);

  // Add to map.
  DIE *VariableDIE = &createAndAddDIE(GV->getTag(), *ContextDIE, GV);
  DIScope *DeclContext;
  if (auto *SDMDecl = GV->getStaticDataMemberDeclaration()) {
    DeclContext = resolve(SDMDecl->getScope());
    assert(SDMDecl->isStaticMember() && "Expected static member decl");
    assert(GV->isDefinition());
    // We need the declaration DIE that is in the static member's class.
    DIE *VariableSpecDIE = getOrCreateStaticMemberDIE(SDMDecl);
    addDIEEntry(*VariableDIE, dwarf::DW_AT_specification, *VariableSpecDIE);
    // If the global variable's type is different from the one in the class
    // member type, assume that it's more specific and also emit it.
    if (GTy != DD->resolve(SDMDecl->getBaseType()))
      addType(*VariableDIE, GTy);
  } else {
    DeclContext = GV->getScope();
    // Add name and type.
    addString(*VariableDIE, dwarf::DW_AT_name, GV->getDisplayName());
    addType(*VariableDIE, GTy);

    // Add scoping info.
    if (!GV->isLocalToUnit())
      addFlag(*VariableDIE, dwarf::DW_AT_external);

    // Add line number info.
    addSourceLine(*VariableDIE, GV);
  }

  if (!GV->isDefinition())
    addFlag(*VariableDIE, dwarf::DW_AT_declaration);
  else
    addGlobalName(GV->getName(), *VariableDIE, DeclContext);

  if (uint32_t AlignInBytes = GV->getAlignInBytes())
    addUInt(*VariableDIE, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
            AlignInBytes);

  if (MDTuple *TP = GV->getTemplateParams())
    addTemplateParams(*VariableDIE, DINodeArray(TP));

  // Add location.
  bool addToAccelTable = false;
  DIELoc *Loc = nullptr;
  std::unique_ptr<DIEDwarfExpression> DwarfExpr;
  for (const auto &GE : GlobalExprs) {
    const GlobalVariable *Global = GE.Var;
    const DIExpression *Expr = GE.Expr;

    // For compatibility with DWARF 3 and earlier,
    // DW_AT_location(DW_OP_constu, X, DW_OP_stack_value) becomes
    // DW_AT_const_value(X).
    if (GlobalExprs.size() == 1 && Expr && Expr->isConstant()) {
      addToAccelTable = true;
      addConstantValue(*VariableDIE, /*Unsigned=*/true, Expr->getElement(1));
      break;
    }

    // We cannot describe the location of dllimport'd variables: the
    // computation of their address requires loads from the IAT.
    if (Global && Global->hasDLLImportStorageClass())
      continue;

    // Nothing to describe without address or constant.
    if (!Global && (!Expr || !Expr->isConstant()))
      continue;

    if (Global && Global->isThreadLocal() &&
        !Asm->getObjFileLowering().supportDebugThreadLocalLocation())
      continue;

    if (!Loc) {
      addToAccelTable = true;
      Loc = new (DIEValueAllocator) DIELoc;
      DwarfExpr = llvm::make_unique<DIEDwarfExpression>(*Asm, *this, *Loc);
    }

    if (Expr)
      DwarfExpr->addFragmentOffset(Expr);

    if (Global) {
      const MCSymbol *Sym = Asm->getSymbol(Global);
      if (Global->isThreadLocal()) {
        if (Asm->TM.useEmulatedTLS()) {
          // TODO: add debug info for emulated thread local mode.
        } else {
          // FIXME: Make this work with -gsplit-dwarf.
          unsigned PointerSize = Asm->getDataLayout().getPointerSize();
          assert((PointerSize == 4 || PointerSize == 8) &&
                 "Add support for other sizes if necessary");
          // Based on GCC's support for TLS:
          if (!DD->useSplitDwarf()) {
            // 1) Start with a constNu of the appropriate pointer size
            addUInt(*Loc, dwarf::DW_FORM_data1,
                    PointerSize == 4 ? dwarf::DW_OP_const4u
                                     : dwarf::DW_OP_const8u);
            // 2) containing the (relocated) offset of the TLS variable
            //    within the module's TLS block.
            addExpr(*Loc, dwarf::DW_FORM_udata,
                    Asm->getObjFileLowering().getDebugThreadLocalSymbol(Sym));
          } else {
            addUInt(*Loc, dwarf::DW_FORM_data1, dwarf::DW_OP_GNU_const_index);
            addUInt(*Loc, dwarf::DW_FORM_udata,
                    DD->getAddressPool().getIndex(Sym, /* TLS */ true));
          }
          // 3) followed by an OP to make the debugger do a TLS lookup.
          addUInt(*Loc, dwarf::DW_FORM_data1,
                  DD->useGNUTLSOpcode() ? dwarf::DW_OP_GNU_push_tls_address
                                        : dwarf::DW_OP_form_tls_address);
        }
      } else {
        DD->addArangeLabel(SymbolCU(this, Sym));
        addOpAddress(*Loc, Sym);
      }
    }
    // Global variables attached to symbols are memory locations.
    // It would be better if this were unconditional, but malformed input that
    // mixes non-fragments and fragments for the same variable is too expensive
    // to detect in the verifier.
    if (DwarfExpr->isUnknownLocation())
      DwarfExpr->setMemoryLocationKind();
    DwarfExpr->addExpression(Expr);
  }
  if (Loc)
    addBlock(*VariableDIE, dwarf::DW_AT_location, DwarfExpr->finalize());

  if (DD->useAllLinkageNames())
    addLinkageName(*VariableDIE, GV->getLinkageName());

  if (addToAccelTable) {
    DD->addAccelName(*CUNode, GV->getName(), *VariableDIE);

    // If the linkage name is different than the name, go ahead and output
    // that as well into the name table.
    if (GV->getLinkageName() != "" && GV->getName() != GV->getLinkageName() &&
        DD->useAllLinkageNames())
      DD->addAccelName(*CUNode, GV->getLinkageName(), *VariableDIE);
  }

  return VariableDIE;
}

void DwarfCompileUnit::addRange(RangeSpan Range) {
  bool SameAsPrevCU = this == DD->getPrevCU();
  DD->setPrevCU(this);
  // If we have no current ranges just add the range and return, otherwise,
  // check the current section and CU against the previous section and CU we
  // emitted into and the subprogram was contained within. If these are the
  // same then extend our current range, otherwise add this as a new range.
  if (CURanges.empty() || !SameAsPrevCU ||
      (&CURanges.back().getEnd()->getSection() !=
       &Range.getEnd()->getSection())) {
    CURanges.push_back(Range);
    DD->addSectionLabel(Range.getStart());
    return;
  }

  CURanges.back().setEnd(Range.getEnd());
}

void DwarfCompileUnit::initStmtList() {
  if (CUNode->isDebugDirectivesOnly())
    return;

  // Define start line table label for each Compile Unit.
  MCSymbol *LineTableStartSym;
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  if (DD->useSectionsAsReferences()) {
    LineTableStartSym = TLOF.getDwarfLineSection()->getBeginSymbol();
  } else {
    LineTableStartSym =
        Asm->OutStreamer->getDwarfLineTableSymbol(getUniqueID());
  }

  // DW_AT_stmt_list is a offset of line number information for this
  // compile unit in debug_line section. For split dwarf this is
  // left in the skeleton CU and so not included.
  // The line table entries are not always emitted in assembly, so it
  // is not okay to use line_table_start here.
  StmtListValue =
      addSectionLabel(getUnitDie(), dwarf::DW_AT_stmt_list, LineTableStartSym,
                      TLOF.getDwarfLineSection()->getBeginSymbol());
}

void DwarfCompileUnit::applyStmtList(DIE &D) {
  D.addValue(DIEValueAllocator, *StmtListValue);
}

void DwarfCompileUnit::attachLowHighPC(DIE &D, const MCSymbol *Begin,
                                       const MCSymbol *End) {
  assert(Begin && "Begin label should not be null!");
  assert(End && "End label should not be null!");
  assert(Begin->isDefined() && "Invalid starting label");
  assert(End->isDefined() && "Invalid end label");

  addLabelAddress(D, dwarf::DW_AT_low_pc, Begin);
  if (DD->getDwarfVersion() < 4)
    addLabelAddress(D, dwarf::DW_AT_high_pc, End);
  else
    addLabelDelta(D, dwarf::DW_AT_high_pc, End, Begin);
}

// Find DIE for the given subprogram and attach appropriate DW_AT_low_pc
// and DW_AT_high_pc attributes. If there are global variables in this
// scope then create and insert DIEs for these variables.
DIE &DwarfCompileUnit::updateSubprogramScopeDIE(const DISubprogram *SP) {
  DIE *SPDie = getOrCreateSubprogramDIE(SP, includeMinimalInlineScopes());

  attachLowHighPC(*SPDie, Asm->getFunctionBegin(), Asm->getFunctionEnd());
  if (DD->useAppleExtensionAttributes() &&
      !DD->getCurrentFunction()->getTarget().Options.DisableFramePointerElim(
          *DD->getCurrentFunction()))
    addFlag(*SPDie, dwarf::DW_AT_APPLE_omit_frame_ptr);

  // Only include DW_AT_frame_base in full debug info
  if (!includeMinimalInlineScopes()) {
    if (Asm->MF->getTarget().getTargetTriple().isNVPTX()) {
      DIELoc *Loc = new (DIEValueAllocator) DIELoc;
      addUInt(*Loc, dwarf::DW_FORM_data1, dwarf::DW_OP_call_frame_cfa);
      addBlock(*SPDie, dwarf::DW_AT_frame_base, Loc);
    } else {
      const TargetRegisterInfo *RI = Asm->MF->getSubtarget().getRegisterInfo();
      MachineLocation Location(RI->getFrameRegister(*Asm->MF));
      if (RI->isPhysicalRegister(Location.getReg()))
        addAddress(*SPDie, dwarf::DW_AT_frame_base, Location);
    }
  }

  // Add name to the name table, we do this here because we're guaranteed
  // to have concrete versions of our DW_TAG_subprogram nodes.
  DD->addSubprogramNames(*CUNode, SP, *SPDie);

  return *SPDie;
}

// Construct a DIE for this scope.
void DwarfCompileUnit::constructScopeDIE(
    LexicalScope *Scope, SmallVectorImpl<DIE *> &FinalChildren) {
  if (!Scope || !Scope->getScopeNode())
    return;

  auto *DS = Scope->getScopeNode();

  assert((Scope->getInlinedAt() || !isa<DISubprogram>(DS)) &&
         "Only handle inlined subprograms here, use "
         "constructSubprogramScopeDIE for non-inlined "
         "subprograms");

  SmallVector<DIE *, 8> Children;

  // We try to create the scope DIE first, then the children DIEs. This will
  // avoid creating un-used children then removing them later when we find out
  // the scope DIE is null.
  DIE *ScopeDIE;
  if (Scope->getParent() && isa<DISubprogram>(DS)) {
    ScopeDIE = constructInlinedScopeDIE(Scope);
    if (!ScopeDIE)
      return;
    // We create children when the scope DIE is not null.
    createScopeChildrenDIE(Scope, Children);
  } else {
    // Early exit when we know the scope DIE is going to be null.
    if (DD->isLexicalScopeDIENull(Scope))
      return;

    bool HasNonScopeChildren = false;

    // We create children here when we know the scope DIE is not going to be
    // null and the children will be added to the scope DIE.
    createScopeChildrenDIE(Scope, Children, &HasNonScopeChildren);

    // If there are only other scopes as children, put them directly in the
    // parent instead, as this scope would serve no purpose.
    if (!HasNonScopeChildren) {
      FinalChildren.insert(FinalChildren.end(),
                           std::make_move_iterator(Children.begin()),
                           std::make_move_iterator(Children.end()));
      return;
    }
    ScopeDIE = constructLexicalScopeDIE(Scope);
    assert(ScopeDIE && "Scope DIE should not be null.");
  }

  // Add children
  for (auto &I : Children)
    ScopeDIE->addChild(std::move(I));

  FinalChildren.push_back(std::move(ScopeDIE));
}

void DwarfCompileUnit::addScopeRangeList(DIE &ScopeDIE,
                                         SmallVector<RangeSpan, 2> Range) {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  // Emit the offset into .debug_ranges or .debug_rnglists as a relocatable
  // label. emitDIE() will handle emitting it appropriately.
  const MCSymbol *RangeSectionSym =
      DD->getDwarfVersion() >= 5
          ? TLOF.getDwarfRnglistsSection()->getBeginSymbol()
          : TLOF.getDwarfRangesSection()->getBeginSymbol();

  HasRangeLists = true;

  // Add the range list to the set of ranges to be emitted.
  auto IndexAndList =
      (DD->getDwarfVersion() < 5 && Skeleton ? Skeleton->DU : DU)
          ->addRange(*(Skeleton ? Skeleton : this), std::move(Range));

  uint32_t Index = IndexAndList.first;
  auto &List = *IndexAndList.second;

  // Under fission, ranges are specified by constant offsets relative to the
  // CU's DW_AT_GNU_ranges_base.
  // FIXME: For DWARF v5, do not generate the DW_AT_ranges attribute under
  // fission until we support the forms using the .debug_addr section
  // (DW_RLE_startx_endx etc.).
  if (DD->getDwarfVersion() >= 5)
    addUInt(ScopeDIE, dwarf::DW_AT_ranges, dwarf::DW_FORM_rnglistx, Index);
  else if (isDwoUnit())
    addSectionDelta(ScopeDIE, dwarf::DW_AT_ranges, List.getSym(),
                    RangeSectionSym);
  else
    addSectionLabel(ScopeDIE, dwarf::DW_AT_ranges, List.getSym(),
                    RangeSectionSym);
}

void DwarfCompileUnit::attachRangesOrLowHighPC(
    DIE &Die, SmallVector<RangeSpan, 2> Ranges) {
  if (Ranges.size() == 1 || !DD->useRangesSection()) {
    const RangeSpan &Front = Ranges.front();
    const RangeSpan &Back = Ranges.back();
    attachLowHighPC(Die, Front.getStart(), Back.getEnd());
  } else
    addScopeRangeList(Die, std::move(Ranges));
}

void DwarfCompileUnit::attachRangesOrLowHighPC(
    DIE &Die, const SmallVectorImpl<InsnRange> &Ranges) {
  SmallVector<RangeSpan, 2> List;
  List.reserve(Ranges.size());
  for (const InsnRange &R : Ranges)
    List.push_back(RangeSpan(DD->getLabelBeforeInsn(R.first),
                             DD->getLabelAfterInsn(R.second)));
  attachRangesOrLowHighPC(Die, std::move(List));
}

// This scope represents inlined body of a function. Construct DIE to
// represent this concrete inlined copy of the function.
DIE *DwarfCompileUnit::constructInlinedScopeDIE(LexicalScope *Scope) {
  assert(Scope->getScopeNode());
  auto *DS = Scope->getScopeNode();
  auto *InlinedSP = getDISubprogram(DS);
  // Find the subprogram's DwarfCompileUnit in the SPMap in case the subprogram
  // was inlined from another compile unit.
  DIE *OriginDIE = getAbstractSPDies()[InlinedSP];
  assert(OriginDIE && "Unable to find original DIE for an inlined subprogram.");

  auto ScopeDIE = DIE::get(DIEValueAllocator, dwarf::DW_TAG_inlined_subroutine);
  addDIEEntry(*ScopeDIE, dwarf::DW_AT_abstract_origin, *OriginDIE);

  attachRangesOrLowHighPC(*ScopeDIE, Scope->getRanges());

  // Add the call site information to the DIE.
  const DILocation *IA = Scope->getInlinedAt();
  addUInt(*ScopeDIE, dwarf::DW_AT_call_file, None,
          getOrCreateSourceID(IA->getFile()));
  addUInt(*ScopeDIE, dwarf::DW_AT_call_line, None, IA->getLine());
  if (IA->getDiscriminator() && DD->getDwarfVersion() >= 4)
    addUInt(*ScopeDIE, dwarf::DW_AT_GNU_discriminator, None,
            IA->getDiscriminator());

  // Add name to the name table, we do this here because we're guaranteed
  // to have concrete versions of our DW_TAG_inlined_subprogram nodes.
  DD->addSubprogramNames(*CUNode, InlinedSP, *ScopeDIE);

  return ScopeDIE;
}

// Construct new DW_TAG_lexical_block for this scope and attach
// DW_AT_low_pc/DW_AT_high_pc labels.
DIE *DwarfCompileUnit::constructLexicalScopeDIE(LexicalScope *Scope) {
  if (DD->isLexicalScopeDIENull(Scope))
    return nullptr;

  auto ScopeDIE = DIE::get(DIEValueAllocator, dwarf::DW_TAG_lexical_block);
  if (Scope->isAbstractScope())
    return ScopeDIE;

  attachRangesOrLowHighPC(*ScopeDIE, Scope->getRanges());

  return ScopeDIE;
}

/// constructVariableDIE - Construct a DIE for the given DbgVariable.
DIE *DwarfCompileUnit::constructVariableDIE(DbgVariable &DV, bool Abstract) {
  auto D = constructVariableDIEImpl(DV, Abstract);
  DV.setDIE(*D);
  return D;
}

DIE *DwarfCompileUnit::constructLabelDIE(DbgLabel &DL,
                                         const LexicalScope &Scope) {
  auto LabelDie = DIE::get(DIEValueAllocator, DL.getTag());
  insertDIE(DL.getLabel(), LabelDie);
  DL.setDIE(*LabelDie);

  if (Scope.isAbstractScope())
    applyLabelAttributes(DL, *LabelDie);

  return LabelDie;
}

DIE *DwarfCompileUnit::constructVariableDIEImpl(const DbgVariable &DV,
                                                bool Abstract) {
  // Define variable debug information entry.
  auto VariableDie = DIE::get(DIEValueAllocator, DV.getTag());
  insertDIE(DV.getVariable(), VariableDie);

  if (Abstract) {
    applyVariableAttributes(DV, *VariableDie);
    return VariableDie;
  }

  // Add variable address.

  unsigned Offset = DV.getDebugLocListIndex();
  if (Offset != ~0U) {
    addLocationList(*VariableDie, dwarf::DW_AT_location, Offset);
    return VariableDie;
  }

  // Check if variable is described by a DBG_VALUE instruction.
  if (const MachineInstr *DVInsn = DV.getMInsn()) {
    assert(DVInsn->getNumOperands() == 4);
    if (DVInsn->getOperand(0).isReg()) {
      auto RegOp = DVInsn->getOperand(0);
      auto Op1 = DVInsn->getOperand(1);
      // If the second operand is an immediate, this is an indirect value.
      assert((!Op1.isImm() || (Op1.getImm() == 0)) && "unexpected offset");
      MachineLocation Location(RegOp.getReg(), Op1.isImm());
      addVariableAddress(DV, *VariableDie, Location);
    } else if (DVInsn->getOperand(0).isImm()) {
      // This variable is described by a single constant.
      // Check whether it has a DIExpression.
      auto *Expr = DV.getSingleExpression();
      if (Expr && Expr->getNumElements()) {
        DIELoc *Loc = new (DIEValueAllocator) DIELoc;
        DIEDwarfExpression DwarfExpr(*Asm, *this, *Loc);
        // If there is an expression, emit raw unsigned bytes.
        DwarfExpr.addFragmentOffset(Expr);
        DwarfExpr.addUnsignedConstant(DVInsn->getOperand(0).getImm());
        DwarfExpr.addExpression(Expr);
        addBlock(*VariableDie, dwarf::DW_AT_location, DwarfExpr.finalize());
      } else
        addConstantValue(*VariableDie, DVInsn->getOperand(0), DV.getType());
    } else if (DVInsn->getOperand(0).isFPImm())
      addConstantFPValue(*VariableDie, DVInsn->getOperand(0));
    else if (DVInsn->getOperand(0).isCImm())
      addConstantValue(*VariableDie, DVInsn->getOperand(0).getCImm(),
                       DV.getType());

    return VariableDie;
  }

  // .. else use frame index.
  if (!DV.hasFrameIndexExprs())
    return VariableDie;

  DIELoc *Loc = new (DIEValueAllocator) DIELoc;
  DIEDwarfExpression DwarfExpr(*Asm, *this, *Loc);
  for (auto &Fragment : DV.getFrameIndexExprs()) {
    unsigned FrameReg = 0;
    const DIExpression *Expr = Fragment.Expr;
    const TargetFrameLowering *TFI = Asm->MF->getSubtarget().getFrameLowering();
    int Offset = TFI->getFrameIndexReference(*Asm->MF, Fragment.FI, FrameReg);
    DwarfExpr.addFragmentOffset(Expr);
    SmallVector<uint64_t, 8> Ops;
    Ops.push_back(dwarf::DW_OP_plus_uconst);
    Ops.push_back(Offset);
    Ops.append(Expr->elements_begin(), Expr->elements_end());
    DIExpressionCursor Cursor(Ops);
    DwarfExpr.setMemoryLocationKind();
    if (const MCSymbol *FrameSymbol = Asm->getFunctionFrameSymbol())
      addOpAddress(*Loc, FrameSymbol);
    else
      DwarfExpr.addMachineRegExpression(
          *Asm->MF->getSubtarget().getRegisterInfo(), Cursor, FrameReg);
    DwarfExpr.addExpression(std::move(Cursor));
  }
  addBlock(*VariableDie, dwarf::DW_AT_location, DwarfExpr.finalize());

  return VariableDie;
}

DIE *DwarfCompileUnit::constructVariableDIE(DbgVariable &DV,
                                            const LexicalScope &Scope,
                                            DIE *&ObjectPointer) {
  auto Var = constructVariableDIE(DV, Scope.isAbstractScope());
  if (DV.isObjectPointer())
    ObjectPointer = Var;
  return Var;
}

/// Return all DIVariables that appear in count: expressions.
static SmallVector<const DIVariable *, 2> dependencies(DbgVariable *Var) {
  SmallVector<const DIVariable *, 2> Result;
  auto *Array = dyn_cast<DICompositeType>(Var->getType());
  if (!Array || Array->getTag() != dwarf::DW_TAG_array_type)
    return Result;
  for (auto *El : Array->getElements()) {
    if (auto *Subrange = dyn_cast<DISubrange>(El)) {
      auto Count = Subrange->getCount();
      if (auto *Dependency = Count.dyn_cast<DIVariable *>())
        Result.push_back(Dependency);
    }
  }
  return Result;
}

/// Sort local variables so that variables appearing inside of helper
/// expressions come first.
static SmallVector<DbgVariable *, 8>
sortLocalVars(SmallVectorImpl<DbgVariable *> &Input) {
  SmallVector<DbgVariable *, 8> Result;
  SmallVector<PointerIntPair<DbgVariable *, 1>, 8> WorkList;
  // Map back from a DIVariable to its containing DbgVariable.
  SmallDenseMap<const DILocalVariable *, DbgVariable *> DbgVar;
  // Set of DbgVariables in Result.
  SmallDenseSet<DbgVariable *, 8> Visited;
  // For cycle detection.
  SmallDenseSet<DbgVariable *, 8> Visiting;

  // Initialize the worklist and the DIVariable lookup table.
  for (auto Var : reverse(Input)) {
    DbgVar.insert({Var->getVariable(), Var});
    WorkList.push_back({Var, 0});
  }

  // Perform a stable topological sort by doing a DFS.
  while (!WorkList.empty()) {
    auto Item = WorkList.back();
    DbgVariable *Var = Item.getPointer();
    bool visitedAllDependencies = Item.getInt();
    WorkList.pop_back();

    // Dependency is in a different lexical scope or a global.
    if (!Var)
      continue;

    // Already handled.
    if (Visited.count(Var))
      continue;

    // Add to Result if all dependencies are visited.
    if (visitedAllDependencies) {
      Visited.insert(Var);
      Result.push_back(Var);
      continue;
    }

    // Detect cycles.
    auto Res = Visiting.insert(Var);
    if (!Res.second) {
      assert(false && "dependency cycle in local variables");
      return Result;
    }

    // Push dependencies and this node onto the worklist, so that this node is
    // visited again after all of its dependencies are handled.
    WorkList.push_back({Var, 1});
    for (auto *Dependency : dependencies(Var)) {
      auto Dep = dyn_cast_or_null<const DILocalVariable>(Dependency);
      WorkList.push_back({DbgVar[Dep], 0});
    }
  }
  return Result;
}

DIE *DwarfCompileUnit::createScopeChildrenDIE(LexicalScope *Scope,
                                              SmallVectorImpl<DIE *> &Children,
                                              bool *HasNonScopeChildren) {
  assert(Children.empty());
  DIE *ObjectPointer = nullptr;

  // Emit function arguments (order is significant).
  auto Vars = DU->getScopeVariables().lookup(Scope);
  for (auto &DV : Vars.Args)
    Children.push_back(constructVariableDIE(*DV.second, *Scope, ObjectPointer));

  // Emit local variables.
  auto Locals = sortLocalVars(Vars.Locals);
  for (DbgVariable *DV : Locals)
    Children.push_back(constructVariableDIE(*DV, *Scope, ObjectPointer));

  // Skip imported directives in gmlt-like data.
  if (!includeMinimalInlineScopes()) {
    // There is no need to emit empty lexical block DIE.
    for (const auto *IE : ImportedEntities[Scope->getScopeNode()])
      Children.push_back(
          constructImportedEntityDIE(cast<DIImportedEntity>(IE)));
  }

  if (HasNonScopeChildren)
    *HasNonScopeChildren = !Children.empty();

  for (DbgLabel *DL : DU->getScopeLabels().lookup(Scope))
    Children.push_back(constructLabelDIE(*DL, *Scope));

  for (LexicalScope *LS : Scope->getChildren())
    constructScopeDIE(LS, Children);

  return ObjectPointer;
}

DIE &DwarfCompileUnit::constructSubprogramScopeDIE(const DISubprogram *Sub,
                                                   LexicalScope *Scope) {
  DIE &ScopeDIE = updateSubprogramScopeDIE(Sub);

  if (Scope) {
    assert(!Scope->getInlinedAt());
    assert(!Scope->isAbstractScope());
    // Collect lexical scope children first.
    // ObjectPointer might be a local (non-argument) local variable if it's a
    // block's synthetic this pointer.
    if (DIE *ObjectPointer = createAndAddScopeChildren(Scope, ScopeDIE))
      addDIEEntry(ScopeDIE, dwarf::DW_AT_object_pointer, *ObjectPointer);
  }

  // If this is a variadic function, add an unspecified parameter.
  DITypeRefArray FnArgs = Sub->getType()->getTypeArray();

  // If we have a single element of null, it is a function that returns void.
  // If we have more than one elements and the last one is null, it is a
  // variadic function.
  if (FnArgs.size() > 1 && !FnArgs[FnArgs.size() - 1] &&
      !includeMinimalInlineScopes())
    ScopeDIE.addChild(
        DIE::get(DIEValueAllocator, dwarf::DW_TAG_unspecified_parameters));

  return ScopeDIE;
}

DIE *DwarfCompileUnit::createAndAddScopeChildren(LexicalScope *Scope,
                                                 DIE &ScopeDIE) {
  // We create children when the scope DIE is not null.
  SmallVector<DIE *, 8> Children;
  DIE *ObjectPointer = createScopeChildrenDIE(Scope, Children);

  // Add children
  for (auto &I : Children)
    ScopeDIE.addChild(std::move(I));

  return ObjectPointer;
}

void DwarfCompileUnit::constructAbstractSubprogramScopeDIE(
    LexicalScope *Scope) {
  DIE *&AbsDef = getAbstractSPDies()[Scope->getScopeNode()];
  if (AbsDef)
    return;

  auto *SP = cast<DISubprogram>(Scope->getScopeNode());

  DIE *ContextDIE;
  DwarfCompileUnit *ContextCU = this;

  if (includeMinimalInlineScopes())
    ContextDIE = &getUnitDie();
  // Some of this is duplicated from DwarfUnit::getOrCreateSubprogramDIE, with
  // the important distinction that the debug node is not associated with the
  // DIE (since the debug node will be associated with the concrete DIE, if
  // any). It could be refactored to some common utility function.
  else if (auto *SPDecl = SP->getDeclaration()) {
    ContextDIE = &getUnitDie();
    getOrCreateSubprogramDIE(SPDecl);
  } else {
    ContextDIE = getOrCreateContextDIE(resolve(SP->getScope()));
    // The scope may be shared with a subprogram that has already been
    // constructed in another CU, in which case we need to construct this
    // subprogram in the same CU.
    ContextCU = DD->lookupCU(ContextDIE->getUnitDie());
  }

  // Passing null as the associated node because the abstract definition
  // shouldn't be found by lookup.
  AbsDef = &ContextCU->createAndAddDIE(dwarf::DW_TAG_subprogram, *ContextDIE, nullptr);
  ContextCU->applySubprogramAttributesToDefinition(SP, *AbsDef);

  if (!ContextCU->includeMinimalInlineScopes())
    ContextCU->addUInt(*AbsDef, dwarf::DW_AT_inline, None, dwarf::DW_INL_inlined);
  if (DIE *ObjectPointer = ContextCU->createAndAddScopeChildren(Scope, *AbsDef))
    ContextCU->addDIEEntry(*AbsDef, dwarf::DW_AT_object_pointer, *ObjectPointer);
}

DIE &DwarfCompileUnit::constructCallSiteEntryDIE(DIE &ScopeDIE,
                                                 const DISubprogram &CalleeSP,
                                                 bool IsTail,
                                                 const MCExpr *PCOffset) {
  // Insert a call site entry DIE within ScopeDIE.
  DIE &CallSiteDIE =
      createAndAddDIE(dwarf::DW_TAG_call_site, ScopeDIE, nullptr);

  // For the purposes of showing tail call frames in backtraces, a key piece of
  // information is DW_AT_call_origin, a pointer to the callee DIE.
  DIE *CalleeDIE = getOrCreateSubprogramDIE(&CalleeSP);
  assert(CalleeDIE && "Could not create DIE for call site entry origin");
  addDIEEntry(CallSiteDIE, dwarf::DW_AT_call_origin, *CalleeDIE);

  if (IsTail) {
    // Attach DW_AT_call_tail_call to tail calls for standards compliance.
    addFlag(CallSiteDIE, dwarf::DW_AT_call_tail_call);
  } else {
    // Attach the return PC to allow the debugger to disambiguate call paths
    // from one function to another.
    assert(PCOffset && "Missing return PC information for a call");
    addAddressExpr(CallSiteDIE, dwarf::DW_AT_call_return_pc, PCOffset);
  }
  return CallSiteDIE;
}

DIE *DwarfCompileUnit::constructImportedEntityDIE(
    const DIImportedEntity *Module) {
  DIE *IMDie = DIE::get(DIEValueAllocator, (dwarf::Tag)Module->getTag());
  insertDIE(Module, IMDie);
  DIE *EntityDie;
  auto *Entity = resolve(Module->getEntity());
  if (auto *NS = dyn_cast<DINamespace>(Entity))
    EntityDie = getOrCreateNameSpace(NS);
  else if (auto *M = dyn_cast<DIModule>(Entity))
    EntityDie = getOrCreateModule(M);
  else if (auto *SP = dyn_cast<DISubprogram>(Entity))
    EntityDie = getOrCreateSubprogramDIE(SP);
  else if (auto *T = dyn_cast<DIType>(Entity))
    EntityDie = getOrCreateTypeDIE(T);
  else if (auto *GV = dyn_cast<DIGlobalVariable>(Entity))
    EntityDie = getOrCreateGlobalVariableDIE(GV, {});
  else
    EntityDie = getDIE(Entity);
  assert(EntityDie);
  addSourceLine(*IMDie, Module->getLine(), Module->getFile());
  addDIEEntry(*IMDie, dwarf::DW_AT_import, *EntityDie);
  StringRef Name = Module->getName();
  if (!Name.empty())
    addString(*IMDie, dwarf::DW_AT_name, Name);

  return IMDie;
}

void DwarfCompileUnit::finishSubprogramDefinition(const DISubprogram *SP) {
  DIE *D = getDIE(SP);
  if (DIE *AbsSPDIE = getAbstractSPDies().lookup(SP)) {
    if (D)
      // If this subprogram has an abstract definition, reference that
      addDIEEntry(*D, dwarf::DW_AT_abstract_origin, *AbsSPDIE);
  } else {
    assert(D || includeMinimalInlineScopes());
    if (D)
      // And attach the attributes
      applySubprogramAttributesToDefinition(SP, *D);
  }
}

void DwarfCompileUnit::finishEntityDefinition(const DbgEntity *Entity) {
  DbgEntity *AbsEntity = getExistingAbstractEntity(Entity->getEntity());

  auto *Die = Entity->getDIE();
  /// Label may be used to generate DW_AT_low_pc, so put it outside
  /// if/else block.
  const DbgLabel *Label = nullptr;
  if (AbsEntity && AbsEntity->getDIE()) {
    addDIEEntry(*Die, dwarf::DW_AT_abstract_origin, *AbsEntity->getDIE());
    Label = dyn_cast<const DbgLabel>(Entity);
  } else {
    if (const DbgVariable *Var = dyn_cast<const DbgVariable>(Entity))
      applyVariableAttributes(*Var, *Die);
    else if ((Label = dyn_cast<const DbgLabel>(Entity)))
      applyLabelAttributes(*Label, *Die);
    else
      llvm_unreachable("DbgEntity must be DbgVariable or DbgLabel.");
  }

  if (Label)
    if (const auto *Sym = Label->getSymbol())
      addLabelAddress(*Die, dwarf::DW_AT_low_pc, Sym);
}

DbgEntity *DwarfCompileUnit::getExistingAbstractEntity(const DINode *Node) {
  auto &AbstractEntities = getAbstractEntities();
  auto I = AbstractEntities.find(Node);
  if (I != AbstractEntities.end())
    return I->second.get();
  return nullptr;
}

void DwarfCompileUnit::createAbstractEntity(const DINode *Node,
                                            LexicalScope *Scope) {
  assert(Scope && Scope->isAbstractScope());
  auto &Entity = getAbstractEntities()[Node];
  if (isa<const DILocalVariable>(Node)) {
    Entity = llvm::make_unique<DbgVariable>(
                        cast<const DILocalVariable>(Node), nullptr /* IA */);;
    DU->addScopeVariable(Scope, cast<DbgVariable>(Entity.get()));
  } else if (isa<const DILabel>(Node)) {
    Entity = llvm::make_unique<DbgLabel>(
                        cast<const DILabel>(Node), nullptr /* IA */);
    DU->addScopeLabel(Scope, cast<DbgLabel>(Entity.get()));
  }
}

void DwarfCompileUnit::emitHeader(bool UseOffsets) {
  // Don't bother labeling the .dwo unit, as its offset isn't used.
  if (!Skeleton && !DD->useSectionsAsReferences()) {
    LabelBegin = Asm->createTempSymbol("cu_begin");
    Asm->OutStreamer->EmitLabel(LabelBegin);
  }

  dwarf::UnitType UT = Skeleton ? dwarf::DW_UT_split_compile
                                : DD->useSplitDwarf() ? dwarf::DW_UT_skeleton
                                                      : dwarf::DW_UT_compile;
  DwarfUnit::emitCommonHeader(UseOffsets, UT);
  if (DD->getDwarfVersion() >= 5 && UT != dwarf::DW_UT_compile)
    Asm->emitInt64(getDWOId());
}

bool DwarfCompileUnit::hasDwarfPubSections() const {
  switch (CUNode->getNameTableKind()) {
  case DICompileUnit::DebugNameTableKind::None:
    return false;
    // Opting in to GNU Pubnames/types overrides the default to ensure these are
    // generated for things like Gold's gdb_index generation.
  case DICompileUnit::DebugNameTableKind::GNU:
    return true;
  case DICompileUnit::DebugNameTableKind::Default:
    return DD->tuneForGDB() && !includeMinimalInlineScopes() &&
           !CUNode->isDebugDirectivesOnly();
  }
  llvm_unreachable("Unhandled DICompileUnit::DebugNameTableKind enum");
}

/// addGlobalName - Add a new global name to the compile unit.
void DwarfCompileUnit::addGlobalName(StringRef Name, const DIE &Die,
                                     const DIScope *Context) {
  if (!hasDwarfPubSections())
    return;
  std::string FullName = getParentContextString(Context) + Name.str();
  GlobalNames[FullName] = &Die;
}

void DwarfCompileUnit::addGlobalNameForTypeUnit(StringRef Name,
                                                const DIScope *Context) {
  if (!hasDwarfPubSections())
    return;
  std::string FullName = getParentContextString(Context) + Name.str();
  // Insert, allowing the entry to remain as-is if it's already present
  // This way the CU-level type DIE is preferred over the "can't describe this
  // type as a unit offset because it's not really in the CU at all, it's only
  // in a type unit"
  GlobalNames.insert(std::make_pair(std::move(FullName), &getUnitDie()));
}

/// Add a new global type to the unit.
void DwarfCompileUnit::addGlobalType(const DIType *Ty, const DIE &Die,
                                     const DIScope *Context) {
  if (!hasDwarfPubSections())
    return;
  std::string FullName = getParentContextString(Context) + Ty->getName().str();
  GlobalTypes[FullName] = &Die;
}

void DwarfCompileUnit::addGlobalTypeUnitType(const DIType *Ty,
                                             const DIScope *Context) {
  if (!hasDwarfPubSections())
    return;
  std::string FullName = getParentContextString(Context) + Ty->getName().str();
  // Insert, allowing the entry to remain as-is if it's already present
  // This way the CU-level type DIE is preferred over the "can't describe this
  // type as a unit offset because it's not really in the CU at all, it's only
  // in a type unit"
  GlobalTypes.insert(std::make_pair(std::move(FullName), &getUnitDie()));
}

/// addVariableAddress - Add DW_AT_location attribute for a
/// DbgVariable based on provided MachineLocation.
void DwarfCompileUnit::addVariableAddress(const DbgVariable &DV, DIE &Die,
                                          MachineLocation Location) {
  // addBlockByrefAddress is obsolete and will be removed soon.
  // The clang frontend always generates block byref variables with a
  // complex expression that encodes exactly what addBlockByrefAddress
  // would do.
  assert((!DV.isBlockByrefVariable() || DV.hasComplexAddress()) &&
         "block byref variable without a complex expression");
  if (DV.hasComplexAddress())
    addComplexAddress(DV, Die, dwarf::DW_AT_location, Location);
  else
    addAddress(Die, dwarf::DW_AT_location, Location);
}

/// Add an address attribute to a die based on the location provided.
void DwarfCompileUnit::addAddress(DIE &Die, dwarf::Attribute Attribute,
                                  const MachineLocation &Location) {
  DIELoc *Loc = new (DIEValueAllocator) DIELoc;
  DIEDwarfExpression DwarfExpr(*Asm, *this, *Loc);
  if (Location.isIndirect())
    DwarfExpr.setMemoryLocationKind();

  DIExpressionCursor Cursor({});
  const TargetRegisterInfo &TRI = *Asm->MF->getSubtarget().getRegisterInfo();
  if (!DwarfExpr.addMachineRegExpression(TRI, Cursor, Location.getReg()))
    return;
  DwarfExpr.addExpression(std::move(Cursor));

  // Now attach the location information to the DIE.
  addBlock(Die, Attribute, DwarfExpr.finalize());
}

/// Start with the address based on the location provided, and generate the
/// DWARF information necessary to find the actual variable given the extra
/// address information encoded in the DbgVariable, starting from the starting
/// location.  Add the DWARF information to the die.
void DwarfCompileUnit::addComplexAddress(const DbgVariable &DV, DIE &Die,
                                         dwarf::Attribute Attribute,
                                         const MachineLocation &Location) {
  DIELoc *Loc = new (DIEValueAllocator) DIELoc;
  DIEDwarfExpression DwarfExpr(*Asm, *this, *Loc);
  const DIExpression *DIExpr = DV.getSingleExpression();
  DwarfExpr.addFragmentOffset(DIExpr);
  if (Location.isIndirect())
    DwarfExpr.setMemoryLocationKind();

  DIExpressionCursor Cursor(DIExpr);
  const TargetRegisterInfo &TRI = *Asm->MF->getSubtarget().getRegisterInfo();
  if (!DwarfExpr.addMachineRegExpression(TRI, Cursor, Location.getReg()))
    return;
  DwarfExpr.addExpression(std::move(Cursor));

  // Now attach the location information to the DIE.
  addBlock(Die, Attribute, DwarfExpr.finalize());
}

/// Add a Dwarf loclistptr attribute data and value.
void DwarfCompileUnit::addLocationList(DIE &Die, dwarf::Attribute Attribute,
                                       unsigned Index) {
  dwarf::Form Form = DD->getDwarfVersion() >= 4 ? dwarf::DW_FORM_sec_offset
                                                : dwarf::DW_FORM_data4;
  Die.addValue(DIEValueAllocator, Attribute, Form, DIELocList(Index));
}

void DwarfCompileUnit::applyVariableAttributes(const DbgVariable &Var,
                                               DIE &VariableDie) {
  StringRef Name = Var.getName();
  if (!Name.empty())
    addString(VariableDie, dwarf::DW_AT_name, Name);
  const auto *DIVar = Var.getVariable();
  if (DIVar)
    if (uint32_t AlignInBytes = DIVar->getAlignInBytes())
      addUInt(VariableDie, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
              AlignInBytes);

  addSourceLine(VariableDie, DIVar);
  addType(VariableDie, Var.getType());
  if (Var.isArtificial())
    addFlag(VariableDie, dwarf::DW_AT_artificial);
}

void DwarfCompileUnit::applyLabelAttributes(const DbgLabel &Label,
                                            DIE &LabelDie) {
  StringRef Name = Label.getName();
  if (!Name.empty())
    addString(LabelDie, dwarf::DW_AT_name, Name);
  const auto *DILabel = Label.getLabel();
  addSourceLine(LabelDie, DILabel);
}

/// Add a Dwarf expression attribute data and value.
void DwarfCompileUnit::addExpr(DIELoc &Die, dwarf::Form Form,
                               const MCExpr *Expr) {
  Die.addValue(DIEValueAllocator, (dwarf::Attribute)0, Form, DIEExpr(Expr));
}

void DwarfCompileUnit::addAddressExpr(DIE &Die, dwarf::Attribute Attribute,
                                      const MCExpr *Expr) {
  Die.addValue(DIEValueAllocator, Attribute, dwarf::DW_FORM_addr,
               DIEExpr(Expr));
}

void DwarfCompileUnit::applySubprogramAttributesToDefinition(
    const DISubprogram *SP, DIE &SPDie) {
  auto *SPDecl = SP->getDeclaration();
  auto *Context = resolve(SPDecl ? SPDecl->getScope() : SP->getScope());
  applySubprogramAttributes(SP, SPDie, includeMinimalInlineScopes());
  addGlobalName(SP->getName(), SPDie, Context);
}

bool DwarfCompileUnit::isDwoUnit() const {
  return DD->useSplitDwarf() && Skeleton;
}

bool DwarfCompileUnit::includeMinimalInlineScopes() const {
  return getCUNode()->getEmissionKind() == DICompileUnit::LineTablesOnly ||
         (DD->useSplitDwarf() && !Skeleton);
}

void DwarfCompileUnit::addAddrTableBase() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  MCSymbol *Label = DD->getAddressPool().getLabel();
  addSectionLabel(getUnitDie(),
                  getDwarfVersion() >= 5 ? dwarf::DW_AT_addr_base
                                         : dwarf::DW_AT_GNU_addr_base,
                  Label, TLOF.getDwarfAddrSection()->getBeginSymbol());
}
