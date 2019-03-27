//===- DebugInfo.cpp - Debug Information Helper Classes -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the helper classes used to build and interpret debug
// information in LLVM IR form.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/DebugInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <utility>

using namespace llvm;
using namespace llvm::dwarf;

DISubprogram *llvm::getDISubprogram(const MDNode *Scope) {
  if (auto *LocalScope = dyn_cast_or_null<DILocalScope>(Scope))
    return LocalScope->getSubprogram();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// DebugInfoFinder implementations.
//===----------------------------------------------------------------------===//

void DebugInfoFinder::reset() {
  CUs.clear();
  SPs.clear();
  GVs.clear();
  TYs.clear();
  Scopes.clear();
  NodesSeen.clear();
}

void DebugInfoFinder::processModule(const Module &M) {
  for (auto *CU : M.debug_compile_units())
    processCompileUnit(CU);
  for (auto &F : M.functions()) {
    if (auto *SP = cast_or_null<DISubprogram>(F.getSubprogram()))
      processSubprogram(SP);
    // There could be subprograms from inlined functions referenced from
    // instructions only. Walk the function to find them.
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB)
        processInstruction(M, I);
  }
}

void DebugInfoFinder::processCompileUnit(DICompileUnit *CU) {
  if (!addCompileUnit(CU))
    return;
  for (auto DIG : CU->getGlobalVariables()) {
    if (!addGlobalVariable(DIG))
      continue;
    auto *GV = DIG->getVariable();
    processScope(GV->getScope());
    processType(GV->getType().resolve());
  }
  for (auto *ET : CU->getEnumTypes())
    processType(ET);
  for (auto *RT : CU->getRetainedTypes())
    if (auto *T = dyn_cast<DIType>(RT))
      processType(T);
    else
      processSubprogram(cast<DISubprogram>(RT));
  for (auto *Import : CU->getImportedEntities()) {
    auto *Entity = Import->getEntity().resolve();
    if (auto *T = dyn_cast<DIType>(Entity))
      processType(T);
    else if (auto *SP = dyn_cast<DISubprogram>(Entity))
      processSubprogram(SP);
    else if (auto *NS = dyn_cast<DINamespace>(Entity))
      processScope(NS->getScope());
    else if (auto *M = dyn_cast<DIModule>(Entity))
      processScope(M->getScope());
  }
}

void DebugInfoFinder::processInstruction(const Module &M,
                                         const Instruction &I) {
  if (auto *DDI = dyn_cast<DbgDeclareInst>(&I))
    processDeclare(M, DDI);
  else if (auto *DVI = dyn_cast<DbgValueInst>(&I))
    processValue(M, DVI);

  if (auto DbgLoc = I.getDebugLoc())
    processLocation(M, DbgLoc.get());
}

void DebugInfoFinder::processLocation(const Module &M, const DILocation *Loc) {
  if (!Loc)
    return;
  processScope(Loc->getScope());
  processLocation(M, Loc->getInlinedAt());
}

void DebugInfoFinder::processType(DIType *DT) {
  if (!addType(DT))
    return;
  processScope(DT->getScope().resolve());
  if (auto *ST = dyn_cast<DISubroutineType>(DT)) {
    for (DITypeRef Ref : ST->getTypeArray())
      processType(Ref.resolve());
    return;
  }
  if (auto *DCT = dyn_cast<DICompositeType>(DT)) {
    processType(DCT->getBaseType().resolve());
    for (Metadata *D : DCT->getElements()) {
      if (auto *T = dyn_cast<DIType>(D))
        processType(T);
      else if (auto *SP = dyn_cast<DISubprogram>(D))
        processSubprogram(SP);
    }
    return;
  }
  if (auto *DDT = dyn_cast<DIDerivedType>(DT)) {
    processType(DDT->getBaseType().resolve());
  }
}

void DebugInfoFinder::processScope(DIScope *Scope) {
  if (!Scope)
    return;
  if (auto *Ty = dyn_cast<DIType>(Scope)) {
    processType(Ty);
    return;
  }
  if (auto *CU = dyn_cast<DICompileUnit>(Scope)) {
    addCompileUnit(CU);
    return;
  }
  if (auto *SP = dyn_cast<DISubprogram>(Scope)) {
    processSubprogram(SP);
    return;
  }
  if (!addScope(Scope))
    return;
  if (auto *LB = dyn_cast<DILexicalBlockBase>(Scope)) {
    processScope(LB->getScope());
  } else if (auto *NS = dyn_cast<DINamespace>(Scope)) {
    processScope(NS->getScope());
  } else if (auto *M = dyn_cast<DIModule>(Scope)) {
    processScope(M->getScope());
  }
}

void DebugInfoFinder::processSubprogram(DISubprogram *SP) {
  if (!addSubprogram(SP))
    return;
  processScope(SP->getScope().resolve());
  // Some of the users, e.g. CloneFunctionInto / CloneModule, need to set up a
  // ValueMap containing identity mappings for all of the DICompileUnit's, not
  // just DISubprogram's, referenced from anywhere within the Function being
  // cloned prior to calling MapMetadata / RemapInstruction to avoid their
  // duplication later as DICompileUnit's are also directly referenced by
  // llvm.dbg.cu list. Thefore we need to collect DICompileUnit's here as well.
  // Also, DICompileUnit's may reference DISubprogram's too and therefore need
  // to be at least looked through.
  processCompileUnit(SP->getUnit());
  processType(SP->getType());
  for (auto *Element : SP->getTemplateParams()) {
    if (auto *TType = dyn_cast<DITemplateTypeParameter>(Element)) {
      processType(TType->getType().resolve());
    } else if (auto *TVal = dyn_cast<DITemplateValueParameter>(Element)) {
      processType(TVal->getType().resolve());
    }
  }
}

void DebugInfoFinder::processDeclare(const Module &M,
                                     const DbgDeclareInst *DDI) {
  auto *N = dyn_cast<MDNode>(DDI->getVariable());
  if (!N)
    return;

  auto *DV = dyn_cast<DILocalVariable>(N);
  if (!DV)
    return;

  if (!NodesSeen.insert(DV).second)
    return;
  processScope(DV->getScope());
  processType(DV->getType().resolve());
}

void DebugInfoFinder::processValue(const Module &M, const DbgValueInst *DVI) {
  auto *N = dyn_cast<MDNode>(DVI->getVariable());
  if (!N)
    return;

  auto *DV = dyn_cast<DILocalVariable>(N);
  if (!DV)
    return;

  if (!NodesSeen.insert(DV).second)
    return;
  processScope(DV->getScope());
  processType(DV->getType().resolve());
}

bool DebugInfoFinder::addType(DIType *DT) {
  if (!DT)
    return false;

  if (!NodesSeen.insert(DT).second)
    return false;

  TYs.push_back(const_cast<DIType *>(DT));
  return true;
}

bool DebugInfoFinder::addCompileUnit(DICompileUnit *CU) {
  if (!CU)
    return false;
  if (!NodesSeen.insert(CU).second)
    return false;

  CUs.push_back(CU);
  return true;
}

bool DebugInfoFinder::addGlobalVariable(DIGlobalVariableExpression *DIG) {
  if (!NodesSeen.insert(DIG).second)
    return false;

  GVs.push_back(DIG);
  return true;
}

bool DebugInfoFinder::addSubprogram(DISubprogram *SP) {
  if (!SP)
    return false;

  if (!NodesSeen.insert(SP).second)
    return false;

  SPs.push_back(SP);
  return true;
}

bool DebugInfoFinder::addScope(DIScope *Scope) {
  if (!Scope)
    return false;
  // FIXME: Ocaml binding generates a scope with no content, we treat it
  // as null for now.
  if (Scope->getNumOperands() == 0)
    return false;
  if (!NodesSeen.insert(Scope).second)
    return false;
  Scopes.push_back(Scope);
  return true;
}

static MDNode *stripDebugLocFromLoopID(MDNode *N) {
  assert(!empty(N->operands()) && "Missing self reference?");

  // if there is no debug location, we do not have to rewrite this MDNode.
  if (std::none_of(N->op_begin() + 1, N->op_end(), [](const MDOperand &Op) {
        return isa<DILocation>(Op.get());
      }))
    return N;

  // If there is only the debug location without any actual loop metadata, we
  // can remove the metadata.
  if (std::none_of(N->op_begin() + 1, N->op_end(), [](const MDOperand &Op) {
        return !isa<DILocation>(Op.get());
      }))
    return nullptr;

  SmallVector<Metadata *, 4> Args;
  // Reserve operand 0 for loop id self reference.
  auto TempNode = MDNode::getTemporary(N->getContext(), None);
  Args.push_back(TempNode.get());
  // Add all non-debug location operands back.
  for (auto Op = N->op_begin() + 1; Op != N->op_end(); Op++) {
    if (!isa<DILocation>(*Op))
      Args.push_back(*Op);
  }

  // Set the first operand to itself.
  MDNode *LoopID = MDNode::get(N->getContext(), Args);
  LoopID->replaceOperandWith(0, LoopID);
  return LoopID;
}

bool llvm::stripDebugInfo(Function &F) {
  bool Changed = false;
  if (F.hasMetadata(LLVMContext::MD_dbg)) {
    Changed = true;
    F.setSubprogram(nullptr);
  }

  DenseMap<MDNode*, MDNode*> LoopIDsMap;
  for (BasicBlock &BB : F) {
    for (auto II = BB.begin(), End = BB.end(); II != End;) {
      Instruction &I = *II++; // We may delete the instruction, increment now.
      if (isa<DbgInfoIntrinsic>(&I)) {
        I.eraseFromParent();
        Changed = true;
        continue;
      }
      if (I.getDebugLoc()) {
        Changed = true;
        I.setDebugLoc(DebugLoc());
      }
    }

    auto *TermInst = BB.getTerminator();
    if (!TermInst)
      // This is invalid IR, but we may not have run the verifier yet
      continue;
    if (auto *LoopID = TermInst->getMetadata(LLVMContext::MD_loop)) {
      auto *NewLoopID = LoopIDsMap.lookup(LoopID);
      if (!NewLoopID)
        NewLoopID = LoopIDsMap[LoopID] = stripDebugLocFromLoopID(LoopID);
      if (NewLoopID != LoopID)
        TermInst->setMetadata(LLVMContext::MD_loop, NewLoopID);
    }
  }
  return Changed;
}

bool llvm::StripDebugInfo(Module &M) {
  bool Changed = false;

  for (Module::named_metadata_iterator NMI = M.named_metadata_begin(),
         NME = M.named_metadata_end(); NMI != NME;) {
    NamedMDNode *NMD = &*NMI;
    ++NMI;

    // We're stripping debug info, and without them, coverage information
    // doesn't quite make sense.
    if (NMD->getName().startswith("llvm.dbg.") ||
        NMD->getName() == "llvm.gcov") {
      NMD->eraseFromParent();
      Changed = true;
    }
  }

  for (Function &F : M)
    Changed |= stripDebugInfo(F);

  for (auto &GV : M.globals()) {
    Changed |= GV.eraseMetadata(LLVMContext::MD_dbg);
  }

  if (GVMaterializer *Materializer = M.getMaterializer())
    Materializer->setStripDebugInfo();

  return Changed;
}

namespace {

/// Helper class to downgrade -g metadata to -gline-tables-only metadata.
class DebugTypeInfoRemoval {
  DenseMap<Metadata *, Metadata *> Replacements;

public:
  /// The (void)() type.
  MDNode *EmptySubroutineType;

private:
  /// Remember what linkage name we originally had before stripping. If we end
  /// up making two subprograms identical who originally had different linkage
  /// names, then we need to make one of them distinct, to avoid them getting
  /// uniqued. Maps the new node to the old linkage name.
  DenseMap<DISubprogram *, StringRef> NewToLinkageName;

  // TODO: Remember the distinct subprogram we created for a given linkage name,
  // so that we can continue to unique whenever possible. Map <newly created
  // node, old linkage name> to the first (possibly distinct) mdsubprogram
  // created for that combination. This is not strictly needed for correctness,
  // but can cut down on the number of MDNodes and let us diff cleanly with the
  // output of -gline-tables-only.

public:
  DebugTypeInfoRemoval(LLVMContext &C)
      : EmptySubroutineType(DISubroutineType::get(C, DINode::FlagZero, 0,
                                                  MDNode::get(C, {}))) {}

  Metadata *map(Metadata *M) {
    if (!M)
      return nullptr;
    auto Replacement = Replacements.find(M);
    if (Replacement != Replacements.end())
      return Replacement->second;

    return M;
  }
  MDNode *mapNode(Metadata *N) { return dyn_cast_or_null<MDNode>(map(N)); }

  /// Recursively remap N and all its referenced children. Does a DF post-order
  /// traversal, so as to remap bottoms up.
  void traverseAndRemap(MDNode *N) { traverse(N); }

private:
  // Create a new DISubprogram, to replace the one given.
  DISubprogram *getReplacementSubprogram(DISubprogram *MDS) {
    auto *FileAndScope = cast_or_null<DIFile>(map(MDS->getFile()));
    StringRef LinkageName = MDS->getName().empty() ? MDS->getLinkageName() : "";
    DISubprogram *Declaration = nullptr;
    auto *Type = cast_or_null<DISubroutineType>(map(MDS->getType()));
    DITypeRef ContainingType(map(MDS->getContainingType()));
    auto *Unit = cast_or_null<DICompileUnit>(map(MDS->getUnit()));
    auto Variables = nullptr;
    auto TemplateParams = nullptr;

    // Make a distinct DISubprogram, for situations that warrent it.
    auto distinctMDSubprogram = [&]() {
      return DISubprogram::getDistinct(
          MDS->getContext(), FileAndScope, MDS->getName(), LinkageName,
          FileAndScope, MDS->getLine(), Type, MDS->getScopeLine(),
          ContainingType, MDS->getVirtualIndex(), MDS->getThisAdjustment(),
          MDS->getFlags(), MDS->getSPFlags(), Unit, TemplateParams, Declaration,
          Variables);
    };

    if (MDS->isDistinct())
      return distinctMDSubprogram();

    auto *NewMDS = DISubprogram::get(
        MDS->getContext(), FileAndScope, MDS->getName(), LinkageName,
        FileAndScope, MDS->getLine(), Type, MDS->getScopeLine(), ContainingType,
        MDS->getVirtualIndex(), MDS->getThisAdjustment(), MDS->getFlags(),
        MDS->getSPFlags(), Unit, TemplateParams, Declaration, Variables);

    StringRef OldLinkageName = MDS->getLinkageName();

    // See if we need to make a distinct one.
    auto OrigLinkage = NewToLinkageName.find(NewMDS);
    if (OrigLinkage != NewToLinkageName.end()) {
      if (OrigLinkage->second == OldLinkageName)
        // We're good.
        return NewMDS;

      // Otherwise, need to make a distinct one.
      // TODO: Query the map to see if we already have one.
      return distinctMDSubprogram();
    }

    NewToLinkageName.insert({NewMDS, MDS->getLinkageName()});
    return NewMDS;
  }

  /// Create a new compile unit, to replace the one given
  DICompileUnit *getReplacementCU(DICompileUnit *CU) {
    // Drop skeleton CUs.
    if (CU->getDWOId())
      return nullptr;

    auto *File = cast_or_null<DIFile>(map(CU->getFile()));
    MDTuple *EnumTypes = nullptr;
    MDTuple *RetainedTypes = nullptr;
    MDTuple *GlobalVariables = nullptr;
    MDTuple *ImportedEntities = nullptr;
    return DICompileUnit::getDistinct(
        CU->getContext(), CU->getSourceLanguage(), File, CU->getProducer(),
        CU->isOptimized(), CU->getFlags(), CU->getRuntimeVersion(),
        CU->getSplitDebugFilename(), DICompileUnit::LineTablesOnly, EnumTypes,
        RetainedTypes, GlobalVariables, ImportedEntities, CU->getMacros(),
        CU->getDWOId(), CU->getSplitDebugInlining(),
        CU->getDebugInfoForProfiling(), CU->getNameTableKind(),
        CU->getRangesBaseAddress());
  }

  DILocation *getReplacementMDLocation(DILocation *MLD) {
    auto *Scope = map(MLD->getScope());
    auto *InlinedAt = map(MLD->getInlinedAt());
    if (MLD->isDistinct())
      return DILocation::getDistinct(MLD->getContext(), MLD->getLine(),
                                     MLD->getColumn(), Scope, InlinedAt);
    return DILocation::get(MLD->getContext(), MLD->getLine(), MLD->getColumn(),
                           Scope, InlinedAt);
  }

  /// Create a new generic MDNode, to replace the one given
  MDNode *getReplacementMDNode(MDNode *N) {
    SmallVector<Metadata *, 8> Ops;
    Ops.reserve(N->getNumOperands());
    for (auto &I : N->operands())
      if (I)
        Ops.push_back(map(I));
    auto *Ret = MDNode::get(N->getContext(), Ops);
    return Ret;
  }

  /// Attempt to re-map N to a newly created node.
  void remap(MDNode *N) {
    if (Replacements.count(N))
      return;

    auto doRemap = [&](MDNode *N) -> MDNode * {
      if (!N)
        return nullptr;
      if (auto *MDSub = dyn_cast<DISubprogram>(N)) {
        remap(MDSub->getUnit());
        return getReplacementSubprogram(MDSub);
      }
      if (isa<DISubroutineType>(N))
        return EmptySubroutineType;
      if (auto *CU = dyn_cast<DICompileUnit>(N))
        return getReplacementCU(CU);
      if (isa<DIFile>(N))
        return N;
      if (auto *MDLB = dyn_cast<DILexicalBlockBase>(N))
        // Remap to our referenced scope (recursively).
        return mapNode(MDLB->getScope());
      if (auto *MLD = dyn_cast<DILocation>(N))
        return getReplacementMDLocation(MLD);

      // Otherwise, if we see these, just drop them now. Not strictly necessary,
      // but this speeds things up a little.
      if (isa<DINode>(N))
        return nullptr;

      return getReplacementMDNode(N);
    };
    Replacements[N] = doRemap(N);
  }

  /// Do the remapping traversal.
  void traverse(MDNode *);
};

} // end anonymous namespace

void DebugTypeInfoRemoval::traverse(MDNode *N) {
  if (!N || Replacements.count(N))
    return;

  // To avoid cycles, as well as for efficiency sake, we will sometimes prune
  // parts of the graph.
  auto prune = [](MDNode *Parent, MDNode *Child) {
    if (auto *MDS = dyn_cast<DISubprogram>(Parent))
      return Child == MDS->getRetainedNodes().get();
    return false;
  };

  SmallVector<MDNode *, 16> ToVisit;
  DenseSet<MDNode *> Opened;

  // Visit each node starting at N in post order, and map them.
  ToVisit.push_back(N);
  while (!ToVisit.empty()) {
    auto *N = ToVisit.back();
    if (!Opened.insert(N).second) {
      // Close it.
      remap(N);
      ToVisit.pop_back();
      continue;
    }
    for (auto &I : N->operands())
      if (auto *MDN = dyn_cast_or_null<MDNode>(I))
        if (!Opened.count(MDN) && !Replacements.count(MDN) && !prune(N, MDN) &&
            !isa<DICompileUnit>(MDN))
          ToVisit.push_back(MDN);
  }
}

bool llvm::stripNonLineTableDebugInfo(Module &M) {
  bool Changed = false;

  // First off, delete the debug intrinsics.
  auto RemoveUses = [&](StringRef Name) {
    if (auto *DbgVal = M.getFunction(Name)) {
      while (!DbgVal->use_empty())
        cast<Instruction>(DbgVal->user_back())->eraseFromParent();
      DbgVal->eraseFromParent();
      Changed = true;
    }
  };
  RemoveUses("llvm.dbg.declare");
  RemoveUses("llvm.dbg.value");

  // Delete non-CU debug info named metadata nodes.
  for (auto NMI = M.named_metadata_begin(), NME = M.named_metadata_end();
       NMI != NME;) {
    NamedMDNode *NMD = &*NMI;
    ++NMI;
    // Specifically keep dbg.cu around.
    if (NMD->getName() == "llvm.dbg.cu")
      continue;
  }

  // Drop all dbg attachments from global variables.
  for (auto &GV : M.globals())
    GV.eraseMetadata(LLVMContext::MD_dbg);

  DebugTypeInfoRemoval Mapper(M.getContext());
  auto remap = [&](MDNode *Node) -> MDNode * {
    if (!Node)
      return nullptr;
    Mapper.traverseAndRemap(Node);
    auto *NewNode = Mapper.mapNode(Node);
    Changed |= Node != NewNode;
    Node = NewNode;
    return NewNode;
  };

  // Rewrite the DebugLocs to be equivalent to what
  // -gline-tables-only would have created.
  for (auto &F : M) {
    if (auto *SP = F.getSubprogram()) {
      Mapper.traverseAndRemap(SP);
      auto *NewSP = cast<DISubprogram>(Mapper.mapNode(SP));
      Changed |= SP != NewSP;
      F.setSubprogram(NewSP);
    }
    for (auto &BB : F) {
      for (auto &I : BB) {
        auto remapDebugLoc = [&](DebugLoc DL) -> DebugLoc {
          auto *Scope = DL.getScope();
          MDNode *InlinedAt = DL.getInlinedAt();
          Scope = remap(Scope);
          InlinedAt = remap(InlinedAt);
          return DebugLoc::get(DL.getLine(), DL.getCol(), Scope, InlinedAt);
        };

        if (I.getDebugLoc() != DebugLoc())
          I.setDebugLoc(remapDebugLoc(I.getDebugLoc()));

        // Remap DILocations in untyped MDNodes (e.g., llvm.loop).
        SmallVector<std::pair<unsigned, MDNode *>, 2> MDs;
        I.getAllMetadata(MDs);
        for (auto Attachment : MDs)
          if (auto *T = dyn_cast_or_null<MDTuple>(Attachment.second))
            for (unsigned N = 0; N < T->getNumOperands(); ++N)
              if (auto *Loc = dyn_cast_or_null<DILocation>(T->getOperand(N)))
                if (Loc != DebugLoc())
                  T->replaceOperandWith(N, remapDebugLoc(Loc));
      }
    }
  }

  // Create a new llvm.dbg.cu, which is equivalent to the one
  // -gline-tables-only would have created.
  for (auto &NMD : M.getNamedMDList()) {
    SmallVector<MDNode *, 8> Ops;
    for (MDNode *Op : NMD.operands())
      Ops.push_back(remap(Op));

    if (!Changed)
      continue;

    NMD.clearOperands();
    for (auto *Op : Ops)
      if (Op)
        NMD.addOperand(Op);
  }
  return Changed;
}

unsigned llvm::getDebugMetadataVersionFromModule(const Module &M) {
  if (auto *Val = mdconst::dyn_extract_or_null<ConstantInt>(
          M.getModuleFlag("Debug Info Version")))
    return Val->getZExtValue();
  return 0;
}

void Instruction::applyMergedLocation(const DILocation *LocA,
                                      const DILocation *LocB) {
  setDebugLoc(DILocation::getMergedLocation(LocA, LocB));
}

//===----------------------------------------------------------------------===//
// LLVM C API implementations.
//===----------------------------------------------------------------------===//

static unsigned map_from_llvmDWARFsourcelanguage(LLVMDWARFSourceLanguage lang) {
  switch (lang) {
#define HANDLE_DW_LANG(ID, NAME, LOWER_BOUND, VERSION, VENDOR)                 \
  case LLVMDWARFSourceLanguage##NAME:                                          \
    return ID;
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DW_LANG
  }
  llvm_unreachable("Unhandled Tag");
}

template <typename DIT> DIT *unwrapDI(LLVMMetadataRef Ref) {
  return (DIT *)(Ref ? unwrap<MDNode>(Ref) : nullptr);
}

static DINode::DIFlags map_from_llvmDIFlags(LLVMDIFlags Flags) {
  return static_cast<DINode::DIFlags>(Flags);
}

static LLVMDIFlags map_to_llvmDIFlags(DINode::DIFlags Flags) {
  return static_cast<LLVMDIFlags>(Flags);
}

static DISubprogram::DISPFlags
pack_into_DISPFlags(bool IsLocalToUnit, bool IsDefinition, bool IsOptimized) {
  return DISubprogram::toSPFlags(IsLocalToUnit, IsDefinition, IsOptimized);
}

unsigned LLVMDebugMetadataVersion() {
  return DEBUG_METADATA_VERSION;
}

LLVMDIBuilderRef LLVMCreateDIBuilderDisallowUnresolved(LLVMModuleRef M) {
  return wrap(new DIBuilder(*unwrap(M), false));
}

LLVMDIBuilderRef LLVMCreateDIBuilder(LLVMModuleRef M) {
  return wrap(new DIBuilder(*unwrap(M)));
}

unsigned LLVMGetModuleDebugMetadataVersion(LLVMModuleRef M) {
  return getDebugMetadataVersionFromModule(*unwrap(M));
}

LLVMBool LLVMStripModuleDebugInfo(LLVMModuleRef M) {
  return StripDebugInfo(*unwrap(M));
}

void LLVMDisposeDIBuilder(LLVMDIBuilderRef Builder) {
  delete unwrap(Builder);
}

void LLVMDIBuilderFinalize(LLVMDIBuilderRef Builder) {
  unwrap(Builder)->finalize();
}

LLVMMetadataRef LLVMDIBuilderCreateCompileUnit(
    LLVMDIBuilderRef Builder, LLVMDWARFSourceLanguage Lang,
    LLVMMetadataRef FileRef, const char *Producer, size_t ProducerLen,
    LLVMBool isOptimized, const char *Flags, size_t FlagsLen,
    unsigned RuntimeVer, const char *SplitName, size_t SplitNameLen,
    LLVMDWARFEmissionKind Kind, unsigned DWOId, LLVMBool SplitDebugInlining,
    LLVMBool DebugInfoForProfiling) {
  auto File = unwrapDI<DIFile>(FileRef);

  return wrap(unwrap(Builder)->createCompileUnit(
                 map_from_llvmDWARFsourcelanguage(Lang), File,
                 StringRef(Producer, ProducerLen), isOptimized,
                 StringRef(Flags, FlagsLen), RuntimeVer,
                 StringRef(SplitName, SplitNameLen),
                 static_cast<DICompileUnit::DebugEmissionKind>(Kind), DWOId,
                 SplitDebugInlining, DebugInfoForProfiling));
}

LLVMMetadataRef
LLVMDIBuilderCreateFile(LLVMDIBuilderRef Builder, const char *Filename,
                        size_t FilenameLen, const char *Directory,
                        size_t DirectoryLen) {
  return wrap(unwrap(Builder)->createFile(StringRef(Filename, FilenameLen),
                                          StringRef(Directory, DirectoryLen)));
}

LLVMMetadataRef
LLVMDIBuilderCreateModule(LLVMDIBuilderRef Builder, LLVMMetadataRef ParentScope,
                          const char *Name, size_t NameLen,
                          const char *ConfigMacros, size_t ConfigMacrosLen,
                          const char *IncludePath, size_t IncludePathLen,
                          const char *ISysRoot, size_t ISysRootLen) {
  return wrap(unwrap(Builder)->createModule(
      unwrapDI<DIScope>(ParentScope), StringRef(Name, NameLen),
      StringRef(ConfigMacros, ConfigMacrosLen),
      StringRef(IncludePath, IncludePathLen),
      StringRef(ISysRoot, ISysRootLen)));
}

LLVMMetadataRef LLVMDIBuilderCreateNameSpace(LLVMDIBuilderRef Builder,
                                             LLVMMetadataRef ParentScope,
                                             const char *Name, size_t NameLen,
                                             LLVMBool ExportSymbols) {
  return wrap(unwrap(Builder)->createNameSpace(
      unwrapDI<DIScope>(ParentScope), StringRef(Name, NameLen), ExportSymbols));
}

LLVMMetadataRef LLVMDIBuilderCreateFunction(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, const char *LinkageName, size_t LinkageNameLen,
    LLVMMetadataRef File, unsigned LineNo, LLVMMetadataRef Ty,
    LLVMBool IsLocalToUnit, LLVMBool IsDefinition,
    unsigned ScopeLine, LLVMDIFlags Flags, LLVMBool IsOptimized) {
  return wrap(unwrap(Builder)->createFunction(
      unwrapDI<DIScope>(Scope), {Name, NameLen}, {LinkageName, LinkageNameLen},
      unwrapDI<DIFile>(File), LineNo, unwrapDI<DISubroutineType>(Ty), ScopeLine,
      map_from_llvmDIFlags(Flags),
      pack_into_DISPFlags(IsLocalToUnit, IsDefinition, IsOptimized), nullptr,
      nullptr, nullptr));
}


LLVMMetadataRef LLVMDIBuilderCreateLexicalBlock(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope,
    LLVMMetadataRef File, unsigned Line, unsigned Col) {
  return wrap(unwrap(Builder)->createLexicalBlock(unwrapDI<DIScope>(Scope),
                                                  unwrapDI<DIFile>(File),
                                                  Line, Col));
}

LLVMMetadataRef
LLVMDIBuilderCreateLexicalBlockFile(LLVMDIBuilderRef Builder,
                                    LLVMMetadataRef Scope,
                                    LLVMMetadataRef File,
                                    unsigned Discriminator) {
  return wrap(unwrap(Builder)->createLexicalBlockFile(unwrapDI<DIScope>(Scope),
                                                      unwrapDI<DIFile>(File),
                                                      Discriminator));
}

LLVMMetadataRef
LLVMDIBuilderCreateImportedModuleFromNamespace(LLVMDIBuilderRef Builder,
                                               LLVMMetadataRef Scope,
                                               LLVMMetadataRef NS,
                                               LLVMMetadataRef File,
                                               unsigned Line) {
  return wrap(unwrap(Builder)->createImportedModule(unwrapDI<DIScope>(Scope),
                                                    unwrapDI<DINamespace>(NS),
                                                    unwrapDI<DIFile>(File),
                                                    Line));
}

LLVMMetadataRef
LLVMDIBuilderCreateImportedModuleFromAlias(LLVMDIBuilderRef Builder,
                                           LLVMMetadataRef Scope,
                                           LLVMMetadataRef ImportedEntity,
                                           LLVMMetadataRef File,
                                           unsigned Line) {
  return wrap(unwrap(Builder)->createImportedModule(
                  unwrapDI<DIScope>(Scope),
                  unwrapDI<DIImportedEntity>(ImportedEntity),
                  unwrapDI<DIFile>(File), Line));
}

LLVMMetadataRef
LLVMDIBuilderCreateImportedModuleFromModule(LLVMDIBuilderRef Builder,
                                            LLVMMetadataRef Scope,
                                            LLVMMetadataRef M,
                                            LLVMMetadataRef File,
                                            unsigned Line) {
  return wrap(unwrap(Builder)->createImportedModule(unwrapDI<DIScope>(Scope),
                                                    unwrapDI<DIModule>(M),
                                                    unwrapDI<DIFile>(File),
                                                    Line));
}

LLVMMetadataRef
LLVMDIBuilderCreateImportedDeclaration(LLVMDIBuilderRef Builder,
                                       LLVMMetadataRef Scope,
                                       LLVMMetadataRef Decl,
                                       LLVMMetadataRef File,
                                       unsigned Line,
                                       const char *Name, size_t NameLen) {
  return wrap(unwrap(Builder)->createImportedDeclaration(
                  unwrapDI<DIScope>(Scope),
                  unwrapDI<DINode>(Decl),
                  unwrapDI<DIFile>(File), Line, {Name, NameLen}));
}

LLVMMetadataRef
LLVMDIBuilderCreateDebugLocation(LLVMContextRef Ctx, unsigned Line,
                                 unsigned Column, LLVMMetadataRef Scope,
                                 LLVMMetadataRef InlinedAt) {
  return wrap(DILocation::get(*unwrap(Ctx), Line, Column, unwrap(Scope),
                              unwrap(InlinedAt)));
}

unsigned LLVMDILocationGetLine(LLVMMetadataRef Location) {
  return unwrapDI<DILocation>(Location)->getLine();
}

unsigned LLVMDILocationGetColumn(LLVMMetadataRef Location) {
  return unwrapDI<DILocation>(Location)->getColumn();
}

LLVMMetadataRef LLVMDILocationGetScope(LLVMMetadataRef Location) {
  return wrap(unwrapDI<DILocation>(Location)->getScope());
}

LLVMMetadataRef LLVMDIBuilderCreateEnumerationType(
  LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
  size_t NameLen, LLVMMetadataRef File, unsigned LineNumber,
  uint64_t SizeInBits, uint32_t AlignInBits, LLVMMetadataRef *Elements,
  unsigned NumElements, LLVMMetadataRef ClassTy) {
auto Elts = unwrap(Builder)->getOrCreateArray({unwrap(Elements),
                                               NumElements});
return wrap(unwrap(Builder)->createEnumerationType(
    unwrapDI<DIScope>(Scope), {Name, NameLen}, unwrapDI<DIFile>(File),
    LineNumber, SizeInBits, AlignInBits, Elts, unwrapDI<DIType>(ClassTy)));
}

LLVMMetadataRef LLVMDIBuilderCreateUnionType(
  LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
  size_t NameLen, LLVMMetadataRef File, unsigned LineNumber,
  uint64_t SizeInBits, uint32_t AlignInBits, LLVMDIFlags Flags,
  LLVMMetadataRef *Elements, unsigned NumElements, unsigned RunTimeLang,
  const char *UniqueId, size_t UniqueIdLen) {
  auto Elts = unwrap(Builder)->getOrCreateArray({unwrap(Elements),
                                                 NumElements});
  return wrap(unwrap(Builder)->createUnionType(
     unwrapDI<DIScope>(Scope), {Name, NameLen}, unwrapDI<DIFile>(File),
     LineNumber, SizeInBits, AlignInBits, map_from_llvmDIFlags(Flags),
     Elts, RunTimeLang, {UniqueId, UniqueIdLen}));
}


LLVMMetadataRef
LLVMDIBuilderCreateArrayType(LLVMDIBuilderRef Builder, uint64_t Size,
                             uint32_t AlignInBits, LLVMMetadataRef Ty,
                             LLVMMetadataRef *Subscripts,
                             unsigned NumSubscripts) {
  auto Subs = unwrap(Builder)->getOrCreateArray({unwrap(Subscripts),
                                                 NumSubscripts});
  return wrap(unwrap(Builder)->createArrayType(Size, AlignInBits,
                                               unwrapDI<DIType>(Ty), Subs));
}

LLVMMetadataRef
LLVMDIBuilderCreateVectorType(LLVMDIBuilderRef Builder, uint64_t Size,
                              uint32_t AlignInBits, LLVMMetadataRef Ty,
                              LLVMMetadataRef *Subscripts,
                              unsigned NumSubscripts) {
  auto Subs = unwrap(Builder)->getOrCreateArray({unwrap(Subscripts),
                                                 NumSubscripts});
  return wrap(unwrap(Builder)->createVectorType(Size, AlignInBits,
                                                unwrapDI<DIType>(Ty), Subs));
}

LLVMMetadataRef
LLVMDIBuilderCreateBasicType(LLVMDIBuilderRef Builder, const char *Name,
                             size_t NameLen, uint64_t SizeInBits,
                             LLVMDWARFTypeEncoding Encoding,
                             LLVMDIFlags Flags) {
  return wrap(unwrap(Builder)->createBasicType({Name, NameLen},
                                               SizeInBits, Encoding,
                                               map_from_llvmDIFlags(Flags)));
}

LLVMMetadataRef LLVMDIBuilderCreatePointerType(
    LLVMDIBuilderRef Builder, LLVMMetadataRef PointeeTy,
    uint64_t SizeInBits, uint32_t AlignInBits, unsigned AddressSpace,
    const char *Name, size_t NameLen) {
  return wrap(unwrap(Builder)->createPointerType(unwrapDI<DIType>(PointeeTy),
                                         SizeInBits, AlignInBits,
                                         AddressSpace, {Name, NameLen}));
}

LLVMMetadataRef LLVMDIBuilderCreateStructType(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, LLVMMetadataRef File, unsigned LineNumber,
    uint64_t SizeInBits, uint32_t AlignInBits, LLVMDIFlags Flags,
    LLVMMetadataRef DerivedFrom, LLVMMetadataRef *Elements,
    unsigned NumElements, unsigned RunTimeLang, LLVMMetadataRef VTableHolder,
    const char *UniqueId, size_t UniqueIdLen) {
  auto Elts = unwrap(Builder)->getOrCreateArray({unwrap(Elements),
                                                 NumElements});
  return wrap(unwrap(Builder)->createStructType(
      unwrapDI<DIScope>(Scope), {Name, NameLen}, unwrapDI<DIFile>(File),
      LineNumber, SizeInBits, AlignInBits, map_from_llvmDIFlags(Flags),
      unwrapDI<DIType>(DerivedFrom), Elts, RunTimeLang,
      unwrapDI<DIType>(VTableHolder), {UniqueId, UniqueIdLen}));
}

LLVMMetadataRef LLVMDIBuilderCreateMemberType(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, LLVMMetadataRef File, unsigned LineNo, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits, LLVMDIFlags Flags,
    LLVMMetadataRef Ty) {
  return wrap(unwrap(Builder)->createMemberType(unwrapDI<DIScope>(Scope),
      {Name, NameLen}, unwrapDI<DIFile>(File), LineNo, SizeInBits, AlignInBits,
      OffsetInBits, map_from_llvmDIFlags(Flags), unwrapDI<DIType>(Ty)));
}

LLVMMetadataRef
LLVMDIBuilderCreateUnspecifiedType(LLVMDIBuilderRef Builder, const char *Name,
                                   size_t NameLen) {
  return wrap(unwrap(Builder)->createUnspecifiedType({Name, NameLen}));
}

LLVMMetadataRef
LLVMDIBuilderCreateStaticMemberType(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, LLVMMetadataRef File, unsigned LineNumber,
    LLVMMetadataRef Type, LLVMDIFlags Flags, LLVMValueRef ConstantVal,
    uint32_t AlignInBits) {
  return wrap(unwrap(Builder)->createStaticMemberType(
                  unwrapDI<DIScope>(Scope), {Name, NameLen},
                  unwrapDI<DIFile>(File), LineNumber, unwrapDI<DIType>(Type),
                  map_from_llvmDIFlags(Flags), unwrap<Constant>(ConstantVal),
                  AlignInBits));
}

LLVMMetadataRef
LLVMDIBuilderCreateObjCIVar(LLVMDIBuilderRef Builder,
                            const char *Name, size_t NameLen,
                            LLVMMetadataRef File, unsigned LineNo,
                            uint64_t SizeInBits, uint32_t AlignInBits,
                            uint64_t OffsetInBits, LLVMDIFlags Flags,
                            LLVMMetadataRef Ty, LLVMMetadataRef PropertyNode) {
  return wrap(unwrap(Builder)->createObjCIVar(
                  {Name, NameLen}, unwrapDI<DIFile>(File), LineNo,
                  SizeInBits, AlignInBits, OffsetInBits,
                  map_from_llvmDIFlags(Flags), unwrapDI<DIType>(Ty),
                  unwrapDI<MDNode>(PropertyNode)));
}

LLVMMetadataRef
LLVMDIBuilderCreateObjCProperty(LLVMDIBuilderRef Builder,
                                const char *Name, size_t NameLen,
                                LLVMMetadataRef File, unsigned LineNo,
                                const char *GetterName, size_t GetterNameLen,
                                const char *SetterName, size_t SetterNameLen,
                                unsigned PropertyAttributes,
                                LLVMMetadataRef Ty) {
  return wrap(unwrap(Builder)->createObjCProperty(
                  {Name, NameLen}, unwrapDI<DIFile>(File), LineNo,
                  {GetterName, GetterNameLen}, {SetterName, SetterNameLen},
                  PropertyAttributes, unwrapDI<DIType>(Ty)));
}

LLVMMetadataRef
LLVMDIBuilderCreateObjectPointerType(LLVMDIBuilderRef Builder,
                                     LLVMMetadataRef Type) {
  return wrap(unwrap(Builder)->createObjectPointerType(unwrapDI<DIType>(Type)));
}

LLVMMetadataRef
LLVMDIBuilderCreateTypedef(LLVMDIBuilderRef Builder, LLVMMetadataRef Type,
                           const char *Name, size_t NameLen,
                           LLVMMetadataRef File, unsigned LineNo,
                           LLVMMetadataRef Scope) {
  return wrap(unwrap(Builder)->createTypedef(
                  unwrapDI<DIType>(Type), {Name, NameLen},
                  unwrapDI<DIFile>(File), LineNo,
                  unwrapDI<DIScope>(Scope)));
}

LLVMMetadataRef
LLVMDIBuilderCreateInheritance(LLVMDIBuilderRef Builder,
                               LLVMMetadataRef Ty, LLVMMetadataRef BaseTy,
                               uint64_t BaseOffset, uint32_t VBPtrOffset,
                               LLVMDIFlags Flags) {
  return wrap(unwrap(Builder)->createInheritance(
                  unwrapDI<DIType>(Ty), unwrapDI<DIType>(BaseTy),
                  BaseOffset, VBPtrOffset, map_from_llvmDIFlags(Flags)));
}

LLVMMetadataRef
LLVMDIBuilderCreateForwardDecl(
    LLVMDIBuilderRef Builder, unsigned Tag, const char *Name,
    size_t NameLen, LLVMMetadataRef Scope, LLVMMetadataRef File, unsigned Line,
    unsigned RuntimeLang, uint64_t SizeInBits, uint32_t AlignInBits,
    const char *UniqueIdentifier, size_t UniqueIdentifierLen) {
  return wrap(unwrap(Builder)->createForwardDecl(
                  Tag, {Name, NameLen}, unwrapDI<DIScope>(Scope),
                  unwrapDI<DIFile>(File), Line, RuntimeLang, SizeInBits,
                  AlignInBits, {UniqueIdentifier, UniqueIdentifierLen}));
}

LLVMMetadataRef
LLVMDIBuilderCreateReplaceableCompositeType(
    LLVMDIBuilderRef Builder, unsigned Tag, const char *Name,
    size_t NameLen, LLVMMetadataRef Scope, LLVMMetadataRef File, unsigned Line,
    unsigned RuntimeLang, uint64_t SizeInBits, uint32_t AlignInBits,
    LLVMDIFlags Flags, const char *UniqueIdentifier,
    size_t UniqueIdentifierLen) {
  return wrap(unwrap(Builder)->createReplaceableCompositeType(
                  Tag, {Name, NameLen}, unwrapDI<DIScope>(Scope),
                  unwrapDI<DIFile>(File), Line, RuntimeLang, SizeInBits,
                  AlignInBits, map_from_llvmDIFlags(Flags),
                  {UniqueIdentifier, UniqueIdentifierLen}));
}

LLVMMetadataRef
LLVMDIBuilderCreateQualifiedType(LLVMDIBuilderRef Builder, unsigned Tag,
                                 LLVMMetadataRef Type) {
  return wrap(unwrap(Builder)->createQualifiedType(Tag,
                                                   unwrapDI<DIType>(Type)));
}

LLVMMetadataRef
LLVMDIBuilderCreateReferenceType(LLVMDIBuilderRef Builder, unsigned Tag,
                                 LLVMMetadataRef Type) {
  return wrap(unwrap(Builder)->createReferenceType(Tag,
                                                   unwrapDI<DIType>(Type)));
}

LLVMMetadataRef
LLVMDIBuilderCreateNullPtrType(LLVMDIBuilderRef Builder) {
  return wrap(unwrap(Builder)->createNullPtrType());
}

LLVMMetadataRef
LLVMDIBuilderCreateMemberPointerType(LLVMDIBuilderRef Builder,
                                     LLVMMetadataRef PointeeType,
                                     LLVMMetadataRef ClassType,
                                     uint64_t SizeInBits,
                                     uint32_t AlignInBits,
                                     LLVMDIFlags Flags) {
  return wrap(unwrap(Builder)->createMemberPointerType(
                  unwrapDI<DIType>(PointeeType),
                  unwrapDI<DIType>(ClassType), AlignInBits, SizeInBits,
                  map_from_llvmDIFlags(Flags)));
}

LLVMMetadataRef
LLVMDIBuilderCreateBitFieldMemberType(LLVMDIBuilderRef Builder,
                                      LLVMMetadataRef Scope,
                                      const char *Name, size_t NameLen,
                                      LLVMMetadataRef File, unsigned LineNumber,
                                      uint64_t SizeInBits,
                                      uint64_t OffsetInBits,
                                      uint64_t StorageOffsetInBits,
                                      LLVMDIFlags Flags, LLVMMetadataRef Type) {
  return wrap(unwrap(Builder)->createBitFieldMemberType(
                  unwrapDI<DIScope>(Scope), {Name, NameLen},
                  unwrapDI<DIFile>(File), LineNumber,
                  SizeInBits, OffsetInBits, StorageOffsetInBits,
                  map_from_llvmDIFlags(Flags), unwrapDI<DIType>(Type)));
}

LLVMMetadataRef LLVMDIBuilderCreateClassType(LLVMDIBuilderRef Builder,
    LLVMMetadataRef Scope, const char *Name, size_t NameLen,
    LLVMMetadataRef File, unsigned LineNumber, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits, LLVMDIFlags Flags,
    LLVMMetadataRef DerivedFrom,
    LLVMMetadataRef *Elements, unsigned NumElements,
    LLVMMetadataRef VTableHolder, LLVMMetadataRef TemplateParamsNode,
    const char *UniqueIdentifier, size_t UniqueIdentifierLen) {
  auto Elts = unwrap(Builder)->getOrCreateArray({unwrap(Elements),
                                                 NumElements});
  return wrap(unwrap(Builder)->createClassType(
                  unwrapDI<DIScope>(Scope), {Name, NameLen},
                  unwrapDI<DIFile>(File), LineNumber,
                  SizeInBits, AlignInBits, OffsetInBits,
                  map_from_llvmDIFlags(Flags), unwrapDI<DIType>(DerivedFrom),
                  Elts, unwrapDI<DIType>(VTableHolder),
                  unwrapDI<MDNode>(TemplateParamsNode),
                  {UniqueIdentifier, UniqueIdentifierLen}));
}

LLVMMetadataRef
LLVMDIBuilderCreateArtificialType(LLVMDIBuilderRef Builder,
                                  LLVMMetadataRef Type) {
  return wrap(unwrap(Builder)->createArtificialType(unwrapDI<DIType>(Type)));
}

const char *LLVMDITypeGetName(LLVMMetadataRef DType, size_t *Length) {
  StringRef Str = unwrap<DIType>(DType)->getName();
  *Length = Str.size();
  return Str.data();
}

uint64_t LLVMDITypeGetSizeInBits(LLVMMetadataRef DType) {
  return unwrapDI<DIType>(DType)->getSizeInBits();
}

uint64_t LLVMDITypeGetOffsetInBits(LLVMMetadataRef DType) {
  return unwrapDI<DIType>(DType)->getOffsetInBits();
}

uint32_t LLVMDITypeGetAlignInBits(LLVMMetadataRef DType) {
  return unwrapDI<DIType>(DType)->getAlignInBits();
}

unsigned LLVMDITypeGetLine(LLVMMetadataRef DType) {
  return unwrapDI<DIType>(DType)->getLine();
}

LLVMDIFlags LLVMDITypeGetFlags(LLVMMetadataRef DType) {
  return map_to_llvmDIFlags(unwrapDI<DIType>(DType)->getFlags());
}

LLVMMetadataRef LLVMDIBuilderGetOrCreateTypeArray(LLVMDIBuilderRef Builder,
                                                  LLVMMetadataRef *Types,
                                                  size_t Length) {
  return wrap(
      unwrap(Builder)->getOrCreateTypeArray({unwrap(Types), Length}).get());
}

LLVMMetadataRef
LLVMDIBuilderCreateSubroutineType(LLVMDIBuilderRef Builder,
                                  LLVMMetadataRef File,
                                  LLVMMetadataRef *ParameterTypes,
                                  unsigned NumParameterTypes,
                                  LLVMDIFlags Flags) {
  auto Elts = unwrap(Builder)->getOrCreateTypeArray({unwrap(ParameterTypes),
                                                     NumParameterTypes});
  return wrap(unwrap(Builder)->createSubroutineType(
    Elts, map_from_llvmDIFlags(Flags)));
}

LLVMMetadataRef LLVMDIBuilderCreateExpression(LLVMDIBuilderRef Builder,
                                              int64_t *Addr, size_t Length) {
  return wrap(unwrap(Builder)->createExpression(ArrayRef<int64_t>(Addr,
                                                                  Length)));
}

LLVMMetadataRef
LLVMDIBuilderCreateConstantValueExpression(LLVMDIBuilderRef Builder,
                                           int64_t Value) {
  return wrap(unwrap(Builder)->createConstantValueExpression(Value));
}

LLVMMetadataRef LLVMDIBuilderCreateGlobalVariableExpression(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, const char *Linkage, size_t LinkLen, LLVMMetadataRef File,
    unsigned LineNo, LLVMMetadataRef Ty, LLVMBool LocalToUnit,
    LLVMMetadataRef Expr, LLVMMetadataRef Decl, uint32_t AlignInBits) {
  return wrap(unwrap(Builder)->createGlobalVariableExpression(
      unwrapDI<DIScope>(Scope), {Name, NameLen}, {Linkage, LinkLen},
      unwrapDI<DIFile>(File), LineNo, unwrapDI<DIType>(Ty), LocalToUnit,
      unwrap<DIExpression>(Expr), unwrapDI<MDNode>(Decl),
      nullptr, AlignInBits));
}

LLVMMetadataRef LLVMTemporaryMDNode(LLVMContextRef Ctx, LLVMMetadataRef *Data,
                                    size_t Count) {
  return wrap(
      MDTuple::getTemporary(*unwrap(Ctx), {unwrap(Data), Count}).release());
}

void LLVMDisposeTemporaryMDNode(LLVMMetadataRef TempNode) {
  MDNode::deleteTemporary(unwrapDI<MDNode>(TempNode));
}

void LLVMMetadataReplaceAllUsesWith(LLVMMetadataRef TargetMetadata,
                                    LLVMMetadataRef Replacement) {
  auto *Node = unwrapDI<MDNode>(TargetMetadata);
  Node->replaceAllUsesWith(unwrap<Metadata>(Replacement));
  MDNode::deleteTemporary(Node);
}

LLVMMetadataRef LLVMDIBuilderCreateTempGlobalVariableFwdDecl(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, const char *Linkage, size_t LnkLen, LLVMMetadataRef File,
    unsigned LineNo, LLVMMetadataRef Ty, LLVMBool LocalToUnit,
    LLVMMetadataRef Decl, uint32_t AlignInBits) {
  return wrap(unwrap(Builder)->createTempGlobalVariableFwdDecl(
      unwrapDI<DIScope>(Scope), {Name, NameLen}, {Linkage, LnkLen},
      unwrapDI<DIFile>(File), LineNo, unwrapDI<DIType>(Ty), LocalToUnit,
      unwrapDI<MDNode>(Decl), nullptr, AlignInBits));
}

LLVMValueRef
LLVMDIBuilderInsertDeclareBefore(LLVMDIBuilderRef Builder, LLVMValueRef Storage,
                                 LLVMMetadataRef VarInfo, LLVMMetadataRef Expr,
                                 LLVMMetadataRef DL, LLVMValueRef Instr) {
  return wrap(unwrap(Builder)->insertDeclare(
                  unwrap(Storage), unwrap<DILocalVariable>(VarInfo),
                  unwrap<DIExpression>(Expr), unwrap<DILocation>(DL),
                  unwrap<Instruction>(Instr)));
}

LLVMValueRef LLVMDIBuilderInsertDeclareAtEnd(
    LLVMDIBuilderRef Builder, LLVMValueRef Storage, LLVMMetadataRef VarInfo,
    LLVMMetadataRef Expr, LLVMMetadataRef DL, LLVMBasicBlockRef Block) {
  return wrap(unwrap(Builder)->insertDeclare(
                  unwrap(Storage), unwrap<DILocalVariable>(VarInfo),
                  unwrap<DIExpression>(Expr), unwrap<DILocation>(DL),
                  unwrap(Block)));
}

LLVMValueRef LLVMDIBuilderInsertDbgValueBefore(LLVMDIBuilderRef Builder,
                                               LLVMValueRef Val,
                                               LLVMMetadataRef VarInfo,
                                               LLVMMetadataRef Expr,
                                               LLVMMetadataRef DebugLoc,
                                               LLVMValueRef Instr) {
  return wrap(unwrap(Builder)->insertDbgValueIntrinsic(
                  unwrap(Val), unwrap<DILocalVariable>(VarInfo),
                  unwrap<DIExpression>(Expr), unwrap<DILocation>(DebugLoc),
                  unwrap<Instruction>(Instr)));
}

LLVMValueRef LLVMDIBuilderInsertDbgValueAtEnd(LLVMDIBuilderRef Builder,
                                              LLVMValueRef Val,
                                              LLVMMetadataRef VarInfo,
                                              LLVMMetadataRef Expr,
                                              LLVMMetadataRef DebugLoc,
                                              LLVMBasicBlockRef Block) {
  return wrap(unwrap(Builder)->insertDbgValueIntrinsic(
                  unwrap(Val), unwrap<DILocalVariable>(VarInfo),
                  unwrap<DIExpression>(Expr), unwrap<DILocation>(DebugLoc),
                  unwrap(Block)));
}

LLVMMetadataRef LLVMDIBuilderCreateAutoVariable(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, LLVMMetadataRef File, unsigned LineNo, LLVMMetadataRef Ty,
    LLVMBool AlwaysPreserve, LLVMDIFlags Flags, uint32_t AlignInBits) {
  return wrap(unwrap(Builder)->createAutoVariable(
                  unwrap<DIScope>(Scope), {Name, NameLen}, unwrap<DIFile>(File),
                  LineNo, unwrap<DIType>(Ty), AlwaysPreserve,
                  map_from_llvmDIFlags(Flags), AlignInBits));
}

LLVMMetadataRef LLVMDIBuilderCreateParameterVariable(
    LLVMDIBuilderRef Builder, LLVMMetadataRef Scope, const char *Name,
    size_t NameLen, unsigned ArgNo, LLVMMetadataRef File, unsigned LineNo,
    LLVMMetadataRef Ty, LLVMBool AlwaysPreserve, LLVMDIFlags Flags) {
  return wrap(unwrap(Builder)->createParameterVariable(
                  unwrap<DIScope>(Scope), {Name, NameLen}, ArgNo, unwrap<DIFile>(File),
                  LineNo, unwrap<DIType>(Ty), AlwaysPreserve,
                  map_from_llvmDIFlags(Flags)));
}

LLVMMetadataRef LLVMDIBuilderGetOrCreateSubrange(LLVMDIBuilderRef Builder,
                                                 int64_t Lo, int64_t Count) {
  return wrap(unwrap(Builder)->getOrCreateSubrange(Lo, Count));
}

LLVMMetadataRef LLVMDIBuilderGetOrCreateArray(LLVMDIBuilderRef Builder,
                                              LLVMMetadataRef *Data,
                                              size_t Length) {
  Metadata **DataValue = unwrap(Data);
  return wrap(unwrap(Builder)->getOrCreateArray({DataValue, Length}).get());
}

LLVMMetadataRef LLVMGetSubprogram(LLVMValueRef Func) {
  return wrap(unwrap<Function>(Func)->getSubprogram());
}

void LLVMSetSubprogram(LLVMValueRef Func, LLVMMetadataRef SP) {
  unwrap<Function>(Func)->setSubprogram(unwrap<DISubprogram>(SP));
}

LLVMMetadataKind LLVMGetMetadataKind(LLVMMetadataRef Metadata) {
  switch(unwrap(Metadata)->getMetadataID()) {
#define HANDLE_METADATA_LEAF(CLASS) \
  case Metadata::CLASS##Kind: \
    return (LLVMMetadataKind)LLVM##CLASS##MetadataKind;
#include "llvm/IR/Metadata.def"
  default:
    return (LLVMMetadataKind)LLVMGenericDINodeMetadataKind;
  }
}
