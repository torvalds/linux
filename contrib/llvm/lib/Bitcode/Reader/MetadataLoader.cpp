//===- MetadataLoader.cpp - Internal BitcodeReader implementation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MetadataLoader.h"
#include "ValueList.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GVMaterializer.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalIndirectSymbol.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "bitcode-reader"

STATISTIC(NumMDStringLoaded, "Number of MDStrings loaded");
STATISTIC(NumMDNodeTemporary, "Number of MDNode::Temporary created");
STATISTIC(NumMDRecordLoaded, "Number of Metadata records loaded");

/// Flag whether we need to import full type definitions for ThinLTO.
/// Currently needed for Darwin and LLDB.
static cl::opt<bool> ImportFullTypeDefinitions(
    "import-full-type-definitions", cl::init(false), cl::Hidden,
    cl::desc("Import full type definitions for ThinLTO."));

static cl::opt<bool> DisableLazyLoading(
    "disable-ondemand-mds-loading", cl::init(false), cl::Hidden,
    cl::desc("Force disable the lazy-loading on-demand of metadata when "
             "loading bitcode for importing."));

namespace {

static int64_t unrotateSign(uint64_t U) { return U & 1 ? ~(U >> 1) : U >> 1; }

class BitcodeReaderMetadataList {
  /// Array of metadata references.
  ///
  /// Don't use std::vector here.  Some versions of libc++ copy (instead of
  /// move) on resize, and TrackingMDRef is very expensive to copy.
  SmallVector<TrackingMDRef, 1> MetadataPtrs;

  /// The set of indices in MetadataPtrs above of forward references that were
  /// generated.
  SmallDenseSet<unsigned, 1> ForwardReference;

  /// The set of indices in MetadataPtrs above of Metadata that need to be
  /// resolved.
  SmallDenseSet<unsigned, 1> UnresolvedNodes;

  /// Structures for resolving old type refs.
  struct {
    SmallDenseMap<MDString *, TempMDTuple, 1> Unknown;
    SmallDenseMap<MDString *, DICompositeType *, 1> Final;
    SmallDenseMap<MDString *, DICompositeType *, 1> FwdDecls;
    SmallVector<std::pair<TrackingMDRef, TempMDTuple>, 1> Arrays;
  } OldTypeRefs;

  LLVMContext &Context;

public:
  BitcodeReaderMetadataList(LLVMContext &C) : Context(C) {}

  // vector compatibility methods
  unsigned size() const { return MetadataPtrs.size(); }
  void resize(unsigned N) { MetadataPtrs.resize(N); }
  void push_back(Metadata *MD) { MetadataPtrs.emplace_back(MD); }
  void clear() { MetadataPtrs.clear(); }
  Metadata *back() const { return MetadataPtrs.back(); }
  void pop_back() { MetadataPtrs.pop_back(); }
  bool empty() const { return MetadataPtrs.empty(); }

  Metadata *operator[](unsigned i) const {
    assert(i < MetadataPtrs.size());
    return MetadataPtrs[i];
  }

  Metadata *lookup(unsigned I) const {
    if (I < MetadataPtrs.size())
      return MetadataPtrs[I];
    return nullptr;
  }

  void shrinkTo(unsigned N) {
    assert(N <= size() && "Invalid shrinkTo request!");
    assert(ForwardReference.empty() && "Unexpected forward refs");
    assert(UnresolvedNodes.empty() && "Unexpected unresolved node");
    MetadataPtrs.resize(N);
  }

  /// Return the given metadata, creating a replaceable forward reference if
  /// necessary.
  Metadata *getMetadataFwdRef(unsigned Idx);

  /// Return the given metadata only if it is fully resolved.
  ///
  /// Gives the same result as \a lookup(), unless \a MDNode::isResolved()
  /// would give \c false.
  Metadata *getMetadataIfResolved(unsigned Idx);

  MDNode *getMDNodeFwdRefOrNull(unsigned Idx);
  void assignValue(Metadata *MD, unsigned Idx);
  void tryToResolveCycles();
  bool hasFwdRefs() const { return !ForwardReference.empty(); }
  int getNextFwdRef() {
    assert(hasFwdRefs());
    return *ForwardReference.begin();
  }

  /// Upgrade a type that had an MDString reference.
  void addTypeRef(MDString &UUID, DICompositeType &CT);

  /// Upgrade a type that had an MDString reference.
  Metadata *upgradeTypeRef(Metadata *MaybeUUID);

  /// Upgrade a type ref array that may have MDString references.
  Metadata *upgradeTypeRefArray(Metadata *MaybeTuple);

private:
  Metadata *resolveTypeRefArray(Metadata *MaybeTuple);
};

void BitcodeReaderMetadataList::assignValue(Metadata *MD, unsigned Idx) {
  if (auto *MDN = dyn_cast<MDNode>(MD))
    if (!MDN->isResolved())
      UnresolvedNodes.insert(Idx);

  if (Idx == size()) {
    push_back(MD);
    return;
  }

  if (Idx >= size())
    resize(Idx + 1);

  TrackingMDRef &OldMD = MetadataPtrs[Idx];
  if (!OldMD) {
    OldMD.reset(MD);
    return;
  }

  // If there was a forward reference to this value, replace it.
  TempMDTuple PrevMD(cast<MDTuple>(OldMD.get()));
  PrevMD->replaceAllUsesWith(MD);
  ForwardReference.erase(Idx);
}

Metadata *BitcodeReaderMetadataList::getMetadataFwdRef(unsigned Idx) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Metadata *MD = MetadataPtrs[Idx])
    return MD;

  // Track forward refs to be resolved later.
  ForwardReference.insert(Idx);

  // Create and return a placeholder, which will later be RAUW'd.
  ++NumMDNodeTemporary;
  Metadata *MD = MDNode::getTemporary(Context, None).release();
  MetadataPtrs[Idx].reset(MD);
  return MD;
}

Metadata *BitcodeReaderMetadataList::getMetadataIfResolved(unsigned Idx) {
  Metadata *MD = lookup(Idx);
  if (auto *N = dyn_cast_or_null<MDNode>(MD))
    if (!N->isResolved())
      return nullptr;
  return MD;
}

MDNode *BitcodeReaderMetadataList::getMDNodeFwdRefOrNull(unsigned Idx) {
  return dyn_cast_or_null<MDNode>(getMetadataFwdRef(Idx));
}

void BitcodeReaderMetadataList::tryToResolveCycles() {
  if (!ForwardReference.empty())
    // Still forward references... can't resolve cycles.
    return;

  // Give up on finding a full definition for any forward decls that remain.
  for (const auto &Ref : OldTypeRefs.FwdDecls)
    OldTypeRefs.Final.insert(Ref);
  OldTypeRefs.FwdDecls.clear();

  // Upgrade from old type ref arrays.  In strange cases, this could add to
  // OldTypeRefs.Unknown.
  for (const auto &Array : OldTypeRefs.Arrays)
    Array.second->replaceAllUsesWith(resolveTypeRefArray(Array.first.get()));
  OldTypeRefs.Arrays.clear();

  // Replace old string-based type refs with the resolved node, if possible.
  // If we haven't seen the node, leave it to the verifier to complain about
  // the invalid string reference.
  for (const auto &Ref : OldTypeRefs.Unknown) {
    if (DICompositeType *CT = OldTypeRefs.Final.lookup(Ref.first))
      Ref.second->replaceAllUsesWith(CT);
    else
      Ref.second->replaceAllUsesWith(Ref.first);
  }
  OldTypeRefs.Unknown.clear();

  if (UnresolvedNodes.empty())
    // Nothing to do.
    return;

  // Resolve any cycles.
  for (unsigned I : UnresolvedNodes) {
    auto &MD = MetadataPtrs[I];
    auto *N = dyn_cast_or_null<MDNode>(MD);
    if (!N)
      continue;

    assert(!N->isTemporary() && "Unexpected forward reference");
    N->resolveCycles();
  }

  // Make sure we return early again until there's another unresolved ref.
  UnresolvedNodes.clear();
}

void BitcodeReaderMetadataList::addTypeRef(MDString &UUID,
                                           DICompositeType &CT) {
  assert(CT.getRawIdentifier() == &UUID && "Mismatched UUID");
  if (CT.isForwardDecl())
    OldTypeRefs.FwdDecls.insert(std::make_pair(&UUID, &CT));
  else
    OldTypeRefs.Final.insert(std::make_pair(&UUID, &CT));
}

Metadata *BitcodeReaderMetadataList::upgradeTypeRef(Metadata *MaybeUUID) {
  auto *UUID = dyn_cast_or_null<MDString>(MaybeUUID);
  if (LLVM_LIKELY(!UUID))
    return MaybeUUID;

  if (auto *CT = OldTypeRefs.Final.lookup(UUID))
    return CT;

  auto &Ref = OldTypeRefs.Unknown[UUID];
  if (!Ref)
    Ref = MDNode::getTemporary(Context, None);
  return Ref.get();
}

Metadata *BitcodeReaderMetadataList::upgradeTypeRefArray(Metadata *MaybeTuple) {
  auto *Tuple = dyn_cast_or_null<MDTuple>(MaybeTuple);
  if (!Tuple || Tuple->isDistinct())
    return MaybeTuple;

  // Look through the array immediately if possible.
  if (!Tuple->isTemporary())
    return resolveTypeRefArray(Tuple);

  // Create and return a placeholder to use for now.  Eventually
  // resolveTypeRefArrays() will be resolve this forward reference.
  OldTypeRefs.Arrays.emplace_back(
      std::piecewise_construct, std::forward_as_tuple(Tuple),
      std::forward_as_tuple(MDTuple::getTemporary(Context, None)));
  return OldTypeRefs.Arrays.back().second.get();
}

Metadata *BitcodeReaderMetadataList::resolveTypeRefArray(Metadata *MaybeTuple) {
  auto *Tuple = dyn_cast_or_null<MDTuple>(MaybeTuple);
  if (!Tuple || Tuple->isDistinct())
    return MaybeTuple;

  // Look through the DITypeRefArray, upgrading each DITypeRef.
  SmallVector<Metadata *, 32> Ops;
  Ops.reserve(Tuple->getNumOperands());
  for (Metadata *MD : Tuple->operands())
    Ops.push_back(upgradeTypeRef(MD));

  return MDTuple::get(Context, Ops);
}

namespace {

class PlaceholderQueue {
  // Placeholders would thrash around when moved, so store in a std::deque
  // instead of some sort of vector.
  std::deque<DistinctMDOperandPlaceholder> PHs;

public:
  ~PlaceholderQueue() {
    assert(empty() && "PlaceholderQueue hasn't been flushed before being destroyed");
  }
  bool empty() { return PHs.empty(); }
  DistinctMDOperandPlaceholder &getPlaceholderOp(unsigned ID);
  void flush(BitcodeReaderMetadataList &MetadataList);

  /// Return the list of temporaries nodes in the queue, these need to be
  /// loaded before we can flush the queue.
  void getTemporaries(BitcodeReaderMetadataList &MetadataList,
                      DenseSet<unsigned> &Temporaries) {
    for (auto &PH : PHs) {
      auto ID = PH.getID();
      auto *MD = MetadataList.lookup(ID);
      if (!MD) {
        Temporaries.insert(ID);
        continue;
      }
      auto *N = dyn_cast_or_null<MDNode>(MD);
      if (N && N->isTemporary())
        Temporaries.insert(ID);
    }
  }
};

} // end anonymous namespace

DistinctMDOperandPlaceholder &PlaceholderQueue::getPlaceholderOp(unsigned ID) {
  PHs.emplace_back(ID);
  return PHs.back();
}

void PlaceholderQueue::flush(BitcodeReaderMetadataList &MetadataList) {
  while (!PHs.empty()) {
    auto *MD = MetadataList.lookup(PHs.front().getID());
    assert(MD && "Flushing placeholder on unassigned MD");
#ifndef NDEBUG
    if (auto *MDN = dyn_cast<MDNode>(MD))
      assert(MDN->isResolved() &&
             "Flushing Placeholder while cycles aren't resolved");
#endif
    PHs.front().replaceUseWith(MD);
    PHs.pop_front();
  }
}

} // anonynous namespace

static Error error(const Twine &Message) {
  return make_error<StringError>(
      Message, make_error_code(BitcodeError::CorruptedBitcode));
}

class MetadataLoader::MetadataLoaderImpl {
  BitcodeReaderMetadataList MetadataList;
  BitcodeReaderValueList &ValueList;
  BitstreamCursor &Stream;
  LLVMContext &Context;
  Module &TheModule;
  std::function<Type *(unsigned)> getTypeByID;

  /// Cursor associated with the lazy-loading of Metadata. This is the easy way
  /// to keep around the right "context" (Abbrev list) to be able to jump in
  /// the middle of the metadata block and load any record.
  BitstreamCursor IndexCursor;

  /// Index that keeps track of MDString values.
  std::vector<StringRef> MDStringRef;

  /// On-demand loading of a single MDString. Requires the index above to be
  /// populated.
  MDString *lazyLoadOneMDString(unsigned Idx);

  /// Index that keeps track of where to find a metadata record in the stream.
  std::vector<uint64_t> GlobalMetadataBitPosIndex;

  /// Populate the index above to enable lazily loading of metadata, and load
  /// the named metadata as well as the transitively referenced global
  /// Metadata.
  Expected<bool> lazyLoadModuleMetadataBlock();

  /// On-demand loading of a single metadata. Requires the index above to be
  /// populated.
  void lazyLoadOneMetadata(unsigned Idx, PlaceholderQueue &Placeholders);

  // Keep mapping of seens pair of old-style CU <-> SP, and update pointers to
  // point from SP to CU after a block is completly parsed.
  std::vector<std::pair<DICompileUnit *, Metadata *>> CUSubprograms;

  /// Functions that need to be matched with subprograms when upgrading old
  /// metadata.
  SmallDenseMap<Function *, DISubprogram *, 16> FunctionsWithSPs;

  // Map the bitcode's custom MDKind ID to the Module's MDKind ID.
  DenseMap<unsigned, unsigned> MDKindMap;

  bool StripTBAA = false;
  bool HasSeenOldLoopTags = false;
  bool NeedUpgradeToDIGlobalVariableExpression = false;
  bool NeedDeclareExpressionUpgrade = false;

  /// True if metadata is being parsed for a module being ThinLTO imported.
  bool IsImporting = false;

  Error parseOneMetadata(SmallVectorImpl<uint64_t> &Record, unsigned Code,
                         PlaceholderQueue &Placeholders, StringRef Blob,
                         unsigned &NextMetadataNo);
  Error parseMetadataStrings(ArrayRef<uint64_t> Record, StringRef Blob,
                             function_ref<void(StringRef)> CallBack);
  Error parseGlobalObjectAttachment(GlobalObject &GO,
                                    ArrayRef<uint64_t> Record);
  Error parseMetadataKindRecord(SmallVectorImpl<uint64_t> &Record);

  void resolveForwardRefsAndPlaceholders(PlaceholderQueue &Placeholders);

  /// Upgrade old-style CU <-> SP pointers to point from SP to CU.
  void upgradeCUSubprograms() {
    for (auto CU_SP : CUSubprograms)
      if (auto *SPs = dyn_cast_or_null<MDTuple>(CU_SP.second))
        for (auto &Op : SPs->operands())
          if (auto *SP = dyn_cast_or_null<DISubprogram>(Op))
            SP->replaceUnit(CU_SP.first);
    CUSubprograms.clear();
  }

  /// Upgrade old-style bare DIGlobalVariables to DIGlobalVariableExpressions.
  void upgradeCUVariables() {
    if (!NeedUpgradeToDIGlobalVariableExpression)
      return;

    // Upgrade list of variables attached to the CUs.
    if (NamedMDNode *CUNodes = TheModule.getNamedMetadata("llvm.dbg.cu"))
      for (unsigned I = 0, E = CUNodes->getNumOperands(); I != E; ++I) {
        auto *CU = cast<DICompileUnit>(CUNodes->getOperand(I));
        if (auto *GVs = dyn_cast_or_null<MDTuple>(CU->getRawGlobalVariables()))
          for (unsigned I = 0; I < GVs->getNumOperands(); I++)
            if (auto *GV =
                    dyn_cast_or_null<DIGlobalVariable>(GVs->getOperand(I))) {
              auto *DGVE = DIGlobalVariableExpression::getDistinct(
                  Context, GV, DIExpression::get(Context, {}));
              GVs->replaceOperandWith(I, DGVE);
            }
      }

    // Upgrade variables attached to globals.
    for (auto &GV : TheModule.globals()) {
      SmallVector<MDNode *, 1> MDs;
      GV.getMetadata(LLVMContext::MD_dbg, MDs);
      GV.eraseMetadata(LLVMContext::MD_dbg);
      for (auto *MD : MDs)
        if (auto *DGV = dyn_cast_or_null<DIGlobalVariable>(MD)) {
          auto *DGVE = DIGlobalVariableExpression::getDistinct(
              Context, DGV, DIExpression::get(Context, {}));
          GV.addMetadata(LLVMContext::MD_dbg, *DGVE);
        } else
          GV.addMetadata(LLVMContext::MD_dbg, *MD);
    }
  }

  /// Remove a leading DW_OP_deref from DIExpressions in a dbg.declare that
  /// describes a function argument.
  void upgradeDeclareExpressions(Function &F) {
    if (!NeedDeclareExpressionUpgrade)
      return;

    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *DDI = dyn_cast<DbgDeclareInst>(&I))
          if (auto *DIExpr = DDI->getExpression())
            if (DIExpr->startsWithDeref() &&
                dyn_cast_or_null<Argument>(DDI->getAddress())) {
              SmallVector<uint64_t, 8> Ops;
              Ops.append(std::next(DIExpr->elements_begin()),
                         DIExpr->elements_end());
              auto *E = DIExpression::get(Context, Ops);
              DDI->setOperand(2, MetadataAsValue::get(Context, E));
            }
  }

  /// Upgrade the expression from previous versions.
  Error upgradeDIExpression(uint64_t FromVersion,
                            MutableArrayRef<uint64_t> &Expr,
                            SmallVectorImpl<uint64_t> &Buffer) {
    auto N = Expr.size();
    switch (FromVersion) {
    default:
      return error("Invalid record");
    case 0:
      if (N >= 3 && Expr[N - 3] == dwarf::DW_OP_bit_piece)
        Expr[N - 3] = dwarf::DW_OP_LLVM_fragment;
      LLVM_FALLTHROUGH;
    case 1:
      // Move DW_OP_deref to the end.
      if (N && Expr[0] == dwarf::DW_OP_deref) {
        auto End = Expr.end();
        if (Expr.size() >= 3 &&
            *std::prev(End, 3) == dwarf::DW_OP_LLVM_fragment)
          End = std::prev(End, 3);
        std::move(std::next(Expr.begin()), End, Expr.begin());
        *std::prev(End) = dwarf::DW_OP_deref;
      }
      NeedDeclareExpressionUpgrade = true;
      LLVM_FALLTHROUGH;
    case 2: {
      // Change DW_OP_plus to DW_OP_plus_uconst.
      // Change DW_OP_minus to DW_OP_uconst, DW_OP_minus
      auto SubExpr = ArrayRef<uint64_t>(Expr);
      while (!SubExpr.empty()) {
        // Skip past other operators with their operands
        // for this version of the IR, obtained from
        // from historic DIExpression::ExprOperand::getSize().
        size_t HistoricSize;
        switch (SubExpr.front()) {
        default:
          HistoricSize = 1;
          break;
        case dwarf::DW_OP_constu:
        case dwarf::DW_OP_minus:
        case dwarf::DW_OP_plus:
          HistoricSize = 2;
          break;
        case dwarf::DW_OP_LLVM_fragment:
          HistoricSize = 3;
          break;
        }

        // If the expression is malformed, make sure we don't
        // copy more elements than we should.
        HistoricSize = std::min(SubExpr.size(), HistoricSize);
        ArrayRef<uint64_t> Args = SubExpr.slice(1, HistoricSize-1);

        switch (SubExpr.front()) {
        case dwarf::DW_OP_plus:
          Buffer.push_back(dwarf::DW_OP_plus_uconst);
          Buffer.append(Args.begin(), Args.end());
          break;
        case dwarf::DW_OP_minus:
          Buffer.push_back(dwarf::DW_OP_constu);
          Buffer.append(Args.begin(), Args.end());
          Buffer.push_back(dwarf::DW_OP_minus);
          break;
        default:
          Buffer.push_back(*SubExpr.begin());
          Buffer.append(Args.begin(), Args.end());
          break;
        }

        // Continue with remaining elements.
        SubExpr = SubExpr.slice(HistoricSize);
      }
      Expr = MutableArrayRef<uint64_t>(Buffer);
      LLVM_FALLTHROUGH;
    }
    case 3:
      // Up-to-date!
      break;
    }

    return Error::success();
  }

  void upgradeDebugInfo() {
    upgradeCUSubprograms();
    upgradeCUVariables();
  }

public:
  MetadataLoaderImpl(BitstreamCursor &Stream, Module &TheModule,
                     BitcodeReaderValueList &ValueList,
                     std::function<Type *(unsigned)> getTypeByID,
                     bool IsImporting)
      : MetadataList(TheModule.getContext()), ValueList(ValueList),
        Stream(Stream), Context(TheModule.getContext()), TheModule(TheModule),
        getTypeByID(std::move(getTypeByID)), IsImporting(IsImporting) {}

  Error parseMetadata(bool ModuleLevel);

  bool hasFwdRefs() const { return MetadataList.hasFwdRefs(); }

  Metadata *getMetadataFwdRefOrLoad(unsigned ID) {
    if (ID < MDStringRef.size())
      return lazyLoadOneMDString(ID);
    if (auto *MD = MetadataList.lookup(ID))
      return MD;
    // If lazy-loading is enabled, we try recursively to load the operand
    // instead of creating a temporary.
    if (ID < (MDStringRef.size() + GlobalMetadataBitPosIndex.size())) {
      PlaceholderQueue Placeholders;
      lazyLoadOneMetadata(ID, Placeholders);
      resolveForwardRefsAndPlaceholders(Placeholders);
      return MetadataList.lookup(ID);
    }
    return MetadataList.getMetadataFwdRef(ID);
  }

  DISubprogram *lookupSubprogramForFunction(Function *F) {
    return FunctionsWithSPs.lookup(F);
  }

  bool hasSeenOldLoopTags() { return HasSeenOldLoopTags; }

  Error parseMetadataAttachment(
      Function &F, const SmallVectorImpl<Instruction *> &InstructionList);

  Error parseMetadataKinds();

  void setStripTBAA(bool Value) { StripTBAA = Value; }
  bool isStrippingTBAA() { return StripTBAA; }

  unsigned size() const { return MetadataList.size(); }
  void shrinkTo(unsigned N) { MetadataList.shrinkTo(N); }
  void upgradeDebugIntrinsics(Function &F) { upgradeDeclareExpressions(F); }
};

Expected<bool>
MetadataLoader::MetadataLoaderImpl::lazyLoadModuleMetadataBlock() {
  IndexCursor = Stream;
  SmallVector<uint64_t, 64> Record;
  // Get the abbrevs, and preload record positions to make them lazy-loadable.
  while (true) {
    BitstreamEntry Entry = IndexCursor.advanceSkippingSubblocks(
        BitstreamCursor::AF_DontPopBlockAtEnd);
    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock: {
      return true;
    }
    case BitstreamEntry::Record: {
      // The interesting case.
      ++NumMDRecordLoaded;
      uint64_t CurrentPos = IndexCursor.GetCurrentBitNo();
      auto Code = IndexCursor.skipRecord(Entry.ID);
      switch (Code) {
      case bitc::METADATA_STRINGS: {
        // Rewind and parse the strings.
        IndexCursor.JumpToBit(CurrentPos);
        StringRef Blob;
        Record.clear();
        IndexCursor.readRecord(Entry.ID, Record, &Blob);
        unsigned NumStrings = Record[0];
        MDStringRef.reserve(NumStrings);
        auto IndexNextMDString = [&](StringRef Str) {
          MDStringRef.push_back(Str);
        };
        if (auto Err = parseMetadataStrings(Record, Blob, IndexNextMDString))
          return std::move(Err);
        break;
      }
      case bitc::METADATA_INDEX_OFFSET: {
        // This is the offset to the index, when we see this we skip all the
        // records and load only an index to these.
        IndexCursor.JumpToBit(CurrentPos);
        Record.clear();
        IndexCursor.readRecord(Entry.ID, Record);
        if (Record.size() != 2)
          return error("Invalid record");
        auto Offset = Record[0] + (Record[1] << 32);
        auto BeginPos = IndexCursor.GetCurrentBitNo();
        IndexCursor.JumpToBit(BeginPos + Offset);
        Entry = IndexCursor.advanceSkippingSubblocks(
            BitstreamCursor::AF_DontPopBlockAtEnd);
        assert(Entry.Kind == BitstreamEntry::Record &&
               "Corrupted bitcode: Expected `Record` when trying to find the "
               "Metadata index");
        Record.clear();
        auto Code = IndexCursor.readRecord(Entry.ID, Record);
        (void)Code;
        assert(Code == bitc::METADATA_INDEX && "Corrupted bitcode: Expected "
                                               "`METADATA_INDEX` when trying "
                                               "to find the Metadata index");

        // Delta unpack
        auto CurrentValue = BeginPos;
        GlobalMetadataBitPosIndex.reserve(Record.size());
        for (auto &Elt : Record) {
          CurrentValue += Elt;
          GlobalMetadataBitPosIndex.push_back(CurrentValue);
        }
        break;
      }
      case bitc::METADATA_INDEX:
        // We don't expect to get there, the Index is loaded when we encounter
        // the offset.
        return error("Corrupted Metadata block");
      case bitc::METADATA_NAME: {
        // Named metadata need to be materialized now and aren't deferred.
        IndexCursor.JumpToBit(CurrentPos);
        Record.clear();
        unsigned Code = IndexCursor.readRecord(Entry.ID, Record);
        assert(Code == bitc::METADATA_NAME);

        // Read name of the named metadata.
        SmallString<8> Name(Record.begin(), Record.end());
        Code = IndexCursor.ReadCode();

        // Named Metadata comes in two parts, we expect the name to be followed
        // by the node
        Record.clear();
        unsigned NextBitCode = IndexCursor.readRecord(Code, Record);
        assert(NextBitCode == bitc::METADATA_NAMED_NODE);
        (void)NextBitCode;

        // Read named metadata elements.
        unsigned Size = Record.size();
        NamedMDNode *NMD = TheModule.getOrInsertNamedMetadata(Name);
        for (unsigned i = 0; i != Size; ++i) {
          // FIXME: We could use a placeholder here, however NamedMDNode are
          // taking MDNode as operand and not using the Metadata infrastructure.
          // It is acknowledged by 'TODO: Inherit from Metadata' in the
          // NamedMDNode class definition.
          MDNode *MD = MetadataList.getMDNodeFwdRefOrNull(Record[i]);
          assert(MD && "Invalid metadata: expect fwd ref to MDNode");
          NMD->addOperand(MD);
        }
        break;
      }
      case bitc::METADATA_GLOBAL_DECL_ATTACHMENT: {
        // FIXME: we need to do this early because we don't materialize global
        // value explicitly.
        IndexCursor.JumpToBit(CurrentPos);
        Record.clear();
        IndexCursor.readRecord(Entry.ID, Record);
        if (Record.size() % 2 == 0)
          return error("Invalid record");
        unsigned ValueID = Record[0];
        if (ValueID >= ValueList.size())
          return error("Invalid record");
        if (auto *GO = dyn_cast<GlobalObject>(ValueList[ValueID]))
          if (Error Err = parseGlobalObjectAttachment(
                  *GO, ArrayRef<uint64_t>(Record).slice(1)))
            return std::move(Err);
        break;
      }
      case bitc::METADATA_KIND:
      case bitc::METADATA_STRING_OLD:
      case bitc::METADATA_OLD_FN_NODE:
      case bitc::METADATA_OLD_NODE:
      case bitc::METADATA_VALUE:
      case bitc::METADATA_DISTINCT_NODE:
      case bitc::METADATA_NODE:
      case bitc::METADATA_LOCATION:
      case bitc::METADATA_GENERIC_DEBUG:
      case bitc::METADATA_SUBRANGE:
      case bitc::METADATA_ENUMERATOR:
      case bitc::METADATA_BASIC_TYPE:
      case bitc::METADATA_DERIVED_TYPE:
      case bitc::METADATA_COMPOSITE_TYPE:
      case bitc::METADATA_SUBROUTINE_TYPE:
      case bitc::METADATA_MODULE:
      case bitc::METADATA_FILE:
      case bitc::METADATA_COMPILE_UNIT:
      case bitc::METADATA_SUBPROGRAM:
      case bitc::METADATA_LEXICAL_BLOCK:
      case bitc::METADATA_LEXICAL_BLOCK_FILE:
      case bitc::METADATA_NAMESPACE:
      case bitc::METADATA_MACRO:
      case bitc::METADATA_MACRO_FILE:
      case bitc::METADATA_TEMPLATE_TYPE:
      case bitc::METADATA_TEMPLATE_VALUE:
      case bitc::METADATA_GLOBAL_VAR:
      case bitc::METADATA_LOCAL_VAR:
      case bitc::METADATA_LABEL:
      case bitc::METADATA_EXPRESSION:
      case bitc::METADATA_OBJC_PROPERTY:
      case bitc::METADATA_IMPORTED_ENTITY:
      case bitc::METADATA_GLOBAL_VAR_EXPR:
        // We don't expect to see any of these, if we see one, give up on
        // lazy-loading and fallback.
        MDStringRef.clear();
        GlobalMetadataBitPosIndex.clear();
        return false;
      }
      break;
    }
    }
  }
}

/// Parse a METADATA_BLOCK. If ModuleLevel is true then we are parsing
/// module level metadata.
Error MetadataLoader::MetadataLoaderImpl::parseMetadata(bool ModuleLevel) {
  if (!ModuleLevel && MetadataList.hasFwdRefs())
    return error("Invalid metadata: fwd refs into function blocks");

  // Record the entry position so that we can jump back here and efficiently
  // skip the whole block in case we lazy-load.
  auto EntryPos = Stream.GetCurrentBitNo();

  if (Stream.EnterSubBlock(bitc::METADATA_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  PlaceholderQueue Placeholders;

  // We lazy-load module-level metadata: we build an index for each record, and
  // then load individual record as needed, starting with the named metadata.
  if (ModuleLevel && IsImporting && MetadataList.empty() &&
      !DisableLazyLoading) {
    auto SuccessOrErr = lazyLoadModuleMetadataBlock();
    if (!SuccessOrErr)
      return SuccessOrErr.takeError();
    if (SuccessOrErr.get()) {
      // An index was successfully created and we will be able to load metadata
      // on-demand.
      MetadataList.resize(MDStringRef.size() +
                          GlobalMetadataBitPosIndex.size());

      // Reading the named metadata created forward references and/or
      // placeholders, that we flush here.
      resolveForwardRefsAndPlaceholders(Placeholders);
      upgradeDebugInfo();
      // Return at the beginning of the block, since it is easy to skip it
      // entirely from there.
      Stream.ReadBlockEnd(); // Pop the abbrev block context.
      Stream.JumpToBit(EntryPos);
      if (Stream.SkipBlock())
        return error("Invalid record");
      return Error::success();
    }
    // Couldn't load an index, fallback to loading all the block "old-style".
  }

  unsigned NextMetadataNo = MetadataList.size();

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      resolveForwardRefsAndPlaceholders(Placeholders);
      upgradeDebugInfo();
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    StringRef Blob;
    ++NumMDRecordLoaded;
    unsigned Code = Stream.readRecord(Entry.ID, Record, &Blob);
    if (Error Err =
            parseOneMetadata(Record, Code, Placeholders, Blob, NextMetadataNo))
      return Err;
  }
}

MDString *MetadataLoader::MetadataLoaderImpl::lazyLoadOneMDString(unsigned ID) {
  ++NumMDStringLoaded;
  if (Metadata *MD = MetadataList.lookup(ID))
    return cast<MDString>(MD);
  auto MDS = MDString::get(Context, MDStringRef[ID]);
  MetadataList.assignValue(MDS, ID);
  return MDS;
}

void MetadataLoader::MetadataLoaderImpl::lazyLoadOneMetadata(
    unsigned ID, PlaceholderQueue &Placeholders) {
  assert(ID < (MDStringRef.size()) + GlobalMetadataBitPosIndex.size());
  assert(ID >= MDStringRef.size() && "Unexpected lazy-loading of MDString");
  // Lookup first if the metadata hasn't already been loaded.
  if (auto *MD = MetadataList.lookup(ID)) {
    auto *N = dyn_cast_or_null<MDNode>(MD);
    if (!N->isTemporary())
      return;
  }
  SmallVector<uint64_t, 64> Record;
  StringRef Blob;
  IndexCursor.JumpToBit(GlobalMetadataBitPosIndex[ID - MDStringRef.size()]);
  auto Entry = IndexCursor.advanceSkippingSubblocks();
  ++NumMDRecordLoaded;
  unsigned Code = IndexCursor.readRecord(Entry.ID, Record, &Blob);
  if (Error Err = parseOneMetadata(Record, Code, Placeholders, Blob, ID))
    report_fatal_error("Can't lazyload MD");
}

/// Ensure that all forward-references and placeholders are resolved.
/// Iteratively lazy-loading metadata on-demand if needed.
void MetadataLoader::MetadataLoaderImpl::resolveForwardRefsAndPlaceholders(
    PlaceholderQueue &Placeholders) {
  DenseSet<unsigned> Temporaries;
  while (1) {
    // Populate Temporaries with the placeholders that haven't been loaded yet.
    Placeholders.getTemporaries(MetadataList, Temporaries);

    // If we don't have any temporary, or FwdReference, we're done!
    if (Temporaries.empty() && !MetadataList.hasFwdRefs())
      break;

    // First, load all the temporaries. This can add new placeholders or
    // forward references.
    for (auto ID : Temporaries)
      lazyLoadOneMetadata(ID, Placeholders);
    Temporaries.clear();

    // Second, load the forward-references. This can also add new placeholders
    // or forward references.
    while (MetadataList.hasFwdRefs())
      lazyLoadOneMetadata(MetadataList.getNextFwdRef(), Placeholders);
  }
  // At this point we don't have any forward reference remaining, or temporary
  // that haven't been loaded. We can safely drop RAUW support and mark cycles
  // as resolved.
  MetadataList.tryToResolveCycles();

  // Finally, everything is in place, we can replace the placeholders operands
  // with the final node they refer to.
  Placeholders.flush(MetadataList);
}

Error MetadataLoader::MetadataLoaderImpl::parseOneMetadata(
    SmallVectorImpl<uint64_t> &Record, unsigned Code,
    PlaceholderQueue &Placeholders, StringRef Blob, unsigned &NextMetadataNo) {

  bool IsDistinct = false;
  auto getMD = [&](unsigned ID) -> Metadata * {
    if (ID < MDStringRef.size())
      return lazyLoadOneMDString(ID);
    if (!IsDistinct) {
      if (auto *MD = MetadataList.lookup(ID))
        return MD;
      // If lazy-loading is enabled, we try recursively to load the operand
      // instead of creating a temporary.
      if (ID < (MDStringRef.size() + GlobalMetadataBitPosIndex.size())) {
        // Create a temporary for the node that is referencing the operand we
        // will lazy-load. It is needed before recursing in case there are
        // uniquing cycles.
        MetadataList.getMetadataFwdRef(NextMetadataNo);
        lazyLoadOneMetadata(ID, Placeholders);
        return MetadataList.lookup(ID);
      }
      // Return a temporary.
      return MetadataList.getMetadataFwdRef(ID);
    }
    if (auto *MD = MetadataList.getMetadataIfResolved(ID))
      return MD;
    return &Placeholders.getPlaceholderOp(ID);
  };
  auto getMDOrNull = [&](unsigned ID) -> Metadata * {
    if (ID)
      return getMD(ID - 1);
    return nullptr;
  };
  auto getMDOrNullWithoutPlaceholders = [&](unsigned ID) -> Metadata * {
    if (ID)
      return MetadataList.getMetadataFwdRef(ID - 1);
    return nullptr;
  };
  auto getMDString = [&](unsigned ID) -> MDString * {
    // This requires that the ID is not really a forward reference.  In
    // particular, the MDString must already have been resolved.
    auto MDS = getMDOrNull(ID);
    return cast_or_null<MDString>(MDS);
  };

  // Support for old type refs.
  auto getDITypeRefOrNull = [&](unsigned ID) {
    return MetadataList.upgradeTypeRef(getMDOrNull(ID));
  };

#define GET_OR_DISTINCT(CLASS, ARGS)                                           \
  (IsDistinct ? CLASS::getDistinct ARGS : CLASS::get ARGS)

  switch (Code) {
  default: // Default behavior: ignore.
    break;
  case bitc::METADATA_NAME: {
    // Read name of the named metadata.
    SmallString<8> Name(Record.begin(), Record.end());
    Record.clear();
    Code = Stream.ReadCode();

    ++NumMDRecordLoaded;
    unsigned NextBitCode = Stream.readRecord(Code, Record);
    if (NextBitCode != bitc::METADATA_NAMED_NODE)
      return error("METADATA_NAME not followed by METADATA_NAMED_NODE");

    // Read named metadata elements.
    unsigned Size = Record.size();
    NamedMDNode *NMD = TheModule.getOrInsertNamedMetadata(Name);
    for (unsigned i = 0; i != Size; ++i) {
      MDNode *MD = MetadataList.getMDNodeFwdRefOrNull(Record[i]);
      if (!MD)
        return error("Invalid named metadata: expect fwd ref to MDNode");
      NMD->addOperand(MD);
    }
    break;
  }
  case bitc::METADATA_OLD_FN_NODE: {
    // FIXME: Remove in 4.0.
    // This is a LocalAsMetadata record, the only type of function-local
    // metadata.
    if (Record.size() % 2 == 1)
      return error("Invalid record");

    // If this isn't a LocalAsMetadata record, we're dropping it.  This used
    // to be legal, but there's no upgrade path.
    auto dropRecord = [&] {
      MetadataList.assignValue(MDNode::get(Context, None), NextMetadataNo);
      NextMetadataNo++;
    };
    if (Record.size() != 2) {
      dropRecord();
      break;
    }

    Type *Ty = getTypeByID(Record[0]);
    if (Ty->isMetadataTy() || Ty->isVoidTy()) {
      dropRecord();
      break;
    }

    MetadataList.assignValue(
        LocalAsMetadata::get(ValueList.getValueFwdRef(Record[1], Ty)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_OLD_NODE: {
    // FIXME: Remove in 4.0.
    if (Record.size() % 2 == 1)
      return error("Invalid record");

    unsigned Size = Record.size();
    SmallVector<Metadata *, 8> Elts;
    for (unsigned i = 0; i != Size; i += 2) {
      Type *Ty = getTypeByID(Record[i]);
      if (!Ty)
        return error("Invalid record");
      if (Ty->isMetadataTy())
        Elts.push_back(getMD(Record[i + 1]));
      else if (!Ty->isVoidTy()) {
        auto *MD =
            ValueAsMetadata::get(ValueList.getValueFwdRef(Record[i + 1], Ty));
        assert(isa<ConstantAsMetadata>(MD) &&
               "Expected non-function-local metadata");
        Elts.push_back(MD);
      } else
        Elts.push_back(nullptr);
    }
    MetadataList.assignValue(MDNode::get(Context, Elts), NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_VALUE: {
    if (Record.size() != 2)
      return error("Invalid record");

    Type *Ty = getTypeByID(Record[0]);
    if (Ty->isMetadataTy() || Ty->isVoidTy())
      return error("Invalid record");

    MetadataList.assignValue(
        ValueAsMetadata::get(ValueList.getValueFwdRef(Record[1], Ty)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_DISTINCT_NODE:
    IsDistinct = true;
    LLVM_FALLTHROUGH;
  case bitc::METADATA_NODE: {
    SmallVector<Metadata *, 8> Elts;
    Elts.reserve(Record.size());
    for (unsigned ID : Record)
      Elts.push_back(getMDOrNull(ID));
    MetadataList.assignValue(IsDistinct ? MDNode::getDistinct(Context, Elts)
                                        : MDNode::get(Context, Elts),
                             NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_LOCATION: {
    if (Record.size() != 5 && Record.size() != 6)
      return error("Invalid record");

    IsDistinct = Record[0];
    unsigned Line = Record[1];
    unsigned Column = Record[2];
    Metadata *Scope = getMD(Record[3]);
    Metadata *InlinedAt = getMDOrNull(Record[4]);
    bool ImplicitCode = Record.size() == 6 && Record[5];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DILocation, (Context, Line, Column, Scope, InlinedAt,
                                     ImplicitCode)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_GENERIC_DEBUG: {
    if (Record.size() < 4)
      return error("Invalid record");

    IsDistinct = Record[0];
    unsigned Tag = Record[1];
    unsigned Version = Record[2];

    if (Tag >= 1u << 16 || Version != 0)
      return error("Invalid record");

    auto *Header = getMDString(Record[3]);
    SmallVector<Metadata *, 8> DwarfOps;
    for (unsigned I = 4, E = Record.size(); I != E; ++I)
      DwarfOps.push_back(getMDOrNull(Record[I]));
    MetadataList.assignValue(
        GET_OR_DISTINCT(GenericDINode, (Context, Tag, Header, DwarfOps)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_SUBRANGE: {
    Metadata *Val = nullptr;
    // Operand 'count' is interpreted as:
    // - Signed integer (version 0)
    // - Metadata node  (version 1)
    switch (Record[0] >> 1) {
    case 0:
      Val = GET_OR_DISTINCT(DISubrange,
                            (Context, Record[1], unrotateSign(Record.back())));
      break;
    case 1:
      Val = GET_OR_DISTINCT(DISubrange, (Context, getMDOrNull(Record[1]),
                                         unrotateSign(Record.back())));
      break;
    default:
      return error("Invalid record: Unsupported version of DISubrange");
    }

    MetadataList.assignValue(Val, NextMetadataNo);
    IsDistinct = Record[0] & 1;
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_ENUMERATOR: {
    if (Record.size() != 3)
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    bool IsUnsigned = Record[0] & 2;
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIEnumerator, (Context, unrotateSign(Record[1]),
                                       IsUnsigned, getMDString(Record[2]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_BASIC_TYPE: {
    if (Record.size() < 6 || Record.size() > 7)
      return error("Invalid record");

    IsDistinct = Record[0];
    DINode::DIFlags Flags = (Record.size() > 6) ?
                    static_cast<DINode::DIFlags>(Record[6]) : DINode::FlagZero;

    MetadataList.assignValue(
        GET_OR_DISTINCT(DIBasicType,
                        (Context, Record[1], getMDString(Record[2]), Record[3],
                         Record[4], Record[5], Flags)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_DERIVED_TYPE: {
    if (Record.size() < 12 || Record.size() > 13)
      return error("Invalid record");

    // DWARF address space is encoded as N->getDWARFAddressSpace() + 1. 0 means
    // that there is no DWARF address space associated with DIDerivedType.
    Optional<unsigned> DWARFAddressSpace;
    if (Record.size() > 12 && Record[12])
      DWARFAddressSpace = Record[12] - 1;

    IsDistinct = Record[0];
    DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[10]);
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIDerivedType,
                        (Context, Record[1], getMDString(Record[2]),
                         getMDOrNull(Record[3]), Record[4],
                         getDITypeRefOrNull(Record[5]),
                         getDITypeRefOrNull(Record[6]), Record[7], Record[8],
                         Record[9], DWARFAddressSpace, Flags,
                         getDITypeRefOrNull(Record[11]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_COMPOSITE_TYPE: {
    if (Record.size() < 16 || Record.size() > 17)
      return error("Invalid record");

    // If we have a UUID and this is not a forward declaration, lookup the
    // mapping.
    IsDistinct = Record[0] & 0x1;
    bool IsNotUsedInTypeRef = Record[0] >= 2;
    unsigned Tag = Record[1];
    MDString *Name = getMDString(Record[2]);
    Metadata *File = getMDOrNull(Record[3]);
    unsigned Line = Record[4];
    Metadata *Scope = getDITypeRefOrNull(Record[5]);
    Metadata *BaseType = nullptr;
    uint64_t SizeInBits = Record[7];
    if (Record[8] > (uint64_t)std::numeric_limits<uint32_t>::max())
      return error("Alignment value is too large");
    uint32_t AlignInBits = Record[8];
    uint64_t OffsetInBits = 0;
    DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[10]);
    Metadata *Elements = nullptr;
    unsigned RuntimeLang = Record[12];
    Metadata *VTableHolder = nullptr;
    Metadata *TemplateParams = nullptr;
    Metadata *Discriminator = nullptr;
    auto *Identifier = getMDString(Record[15]);
    // If this module is being parsed so that it can be ThinLTO imported
    // into another module, composite types only need to be imported
    // as type declarations (unless full type definitions requested).
    // Create type declarations up front to save memory. Also, buildODRType
    // handles the case where this is type ODRed with a definition needed
    // by the importing module, in which case the existing definition is
    // used.
    if (IsImporting && !ImportFullTypeDefinitions && Identifier &&
        (Tag == dwarf::DW_TAG_enumeration_type ||
         Tag == dwarf::DW_TAG_class_type ||
         Tag == dwarf::DW_TAG_structure_type ||
         Tag == dwarf::DW_TAG_union_type)) {
      Flags = Flags | DINode::FlagFwdDecl;
    } else {
      BaseType = getDITypeRefOrNull(Record[6]);
      OffsetInBits = Record[9];
      Elements = getMDOrNull(Record[11]);
      VTableHolder = getDITypeRefOrNull(Record[13]);
      TemplateParams = getMDOrNull(Record[14]);
      if (Record.size() > 16)
        Discriminator = getMDOrNull(Record[16]);
    }
    DICompositeType *CT = nullptr;
    if (Identifier)
      CT = DICompositeType::buildODRType(
          Context, *Identifier, Tag, Name, File, Line, Scope, BaseType,
          SizeInBits, AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
          VTableHolder, TemplateParams, Discriminator);

    // Create a node if we didn't get a lazy ODR type.
    if (!CT)
      CT = GET_OR_DISTINCT(DICompositeType,
                           (Context, Tag, Name, File, Line, Scope, BaseType,
                            SizeInBits, AlignInBits, OffsetInBits, Flags,
                            Elements, RuntimeLang, VTableHolder, TemplateParams,
                            Identifier, Discriminator));
    if (!IsNotUsedInTypeRef && Identifier)
      MetadataList.addTypeRef(*Identifier, *cast<DICompositeType>(CT));

    MetadataList.assignValue(CT, NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_SUBROUTINE_TYPE: {
    if (Record.size() < 3 || Record.size() > 4)
      return error("Invalid record");
    bool IsOldTypeRefArray = Record[0] < 2;
    unsigned CC = (Record.size() > 3) ? Record[3] : 0;

    IsDistinct = Record[0] & 0x1;
    DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[1]);
    Metadata *Types = getMDOrNull(Record[2]);
    if (LLVM_UNLIKELY(IsOldTypeRefArray))
      Types = MetadataList.upgradeTypeRefArray(Types);

    MetadataList.assignValue(
        GET_OR_DISTINCT(DISubroutineType, (Context, Flags, CC, Types)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }

  case bitc::METADATA_MODULE: {
    if (Record.size() != 6)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIModule,
                        (Context, getMDOrNull(Record[1]),
                         getMDString(Record[2]), getMDString(Record[3]),
                         getMDString(Record[4]), getMDString(Record[5]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }

  case bitc::METADATA_FILE: {
    if (Record.size() != 3 && Record.size() != 5 && Record.size() != 6)
      return error("Invalid record");

    IsDistinct = Record[0];
    Optional<DIFile::ChecksumInfo<MDString *>> Checksum;
    // The BitcodeWriter writes null bytes into Record[3:4] when the Checksum
    // is not present. This matches up with the old internal representation,
    // and the old encoding for CSK_None in the ChecksumKind. The new
    // representation reserves the value 0 in the ChecksumKind to continue to
    // encode None in a backwards-compatible way.
    if (Record.size() > 4 && Record[3] && Record[4])
      Checksum.emplace(static_cast<DIFile::ChecksumKind>(Record[3]),
                       getMDString(Record[4]));
    MetadataList.assignValue(
        GET_OR_DISTINCT(
            DIFile,
            (Context, getMDString(Record[1]), getMDString(Record[2]), Checksum,
             Record.size() > 5 ? Optional<MDString *>(getMDString(Record[5]))
                               : None)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_COMPILE_UNIT: {
    if (Record.size() < 14 || Record.size() > 19)
      return error("Invalid record");

    // Ignore Record[0], which indicates whether this compile unit is
    // distinct.  It's always distinct.
    IsDistinct = true;
    auto *CU = DICompileUnit::getDistinct(
        Context, Record[1], getMDOrNull(Record[2]), getMDString(Record[3]),
        Record[4], getMDString(Record[5]), Record[6], getMDString(Record[7]),
        Record[8], getMDOrNull(Record[9]), getMDOrNull(Record[10]),
        getMDOrNull(Record[12]), getMDOrNull(Record[13]),
        Record.size() <= 15 ? nullptr : getMDOrNull(Record[15]),
        Record.size() <= 14 ? 0 : Record[14],
        Record.size() <= 16 ? true : Record[16],
        Record.size() <= 17 ? false : Record[17],
        Record.size() <= 18 ? 0 : Record[18],
        Record.size() <= 19 ? 0 : Record[19]);

    MetadataList.assignValue(CU, NextMetadataNo);
    NextMetadataNo++;

    // Move the Upgrade the list of subprograms.
    if (Metadata *SPs = getMDOrNullWithoutPlaceholders(Record[11]))
      CUSubprograms.push_back({CU, SPs});
    break;
  }
  case bitc::METADATA_SUBPROGRAM: {
    if (Record.size() < 18 || Record.size() > 21)
      return error("Invalid record");

    bool HasSPFlags = Record[0] & 4;
    DISubprogram::DISPFlags SPFlags =
        HasSPFlags
            ? static_cast<DISubprogram::DISPFlags>(Record[9])
            : DISubprogram::toSPFlags(
                  /*IsLocalToUnit=*/Record[7], /*IsDefinition=*/Record[8],
                  /*IsOptimized=*/Record[14], /*Virtuality=*/Record[11]);

    // All definitions should be distinct.
    IsDistinct = (Record[0] & 1) || (SPFlags & DISubprogram::SPFlagDefinition);
    // Version 1 has a Function as Record[15].
    // Version 2 has removed Record[15].
    // Version 3 has the Unit as Record[15].
    // Version 4 added thisAdjustment.
    // Version 5 repacked flags into DISPFlags, changing many element numbers.
    bool HasUnit = Record[0] & 2;
    if (!HasSPFlags && HasUnit && Record.size() < 19)
      return error("Invalid record");
    if (HasSPFlags && !HasUnit)
      return error("Invalid record");
    // Accommodate older formats.
    bool HasFn = false;
    bool HasThisAdj = true;
    bool HasThrownTypes = true;
    unsigned OffsetA = 0;
    unsigned OffsetB = 0;
    if (!HasSPFlags) {
      OffsetA = 2;
      OffsetB = 2;
      if (Record.size() >= 19) {
        HasFn = !HasUnit;
        OffsetB++;
      }
      HasThisAdj = Record.size() >= 20;
      HasThrownTypes = Record.size() >= 21;
    }
    Metadata *CUorFn = getMDOrNull(Record[12 + OffsetB]);
    DISubprogram *SP = GET_OR_DISTINCT(
        DISubprogram,
        (Context,
         getDITypeRefOrNull(Record[1]),                     // scope
         getMDString(Record[2]),                            // name
         getMDString(Record[3]),                            // linkageName
         getMDOrNull(Record[4]),                            // file
         Record[5],                                         // line
         getMDOrNull(Record[6]),                            // type
         Record[7 + OffsetA],                               // scopeLine
         getDITypeRefOrNull(Record[8 + OffsetA]),           // containingType
         Record[10 + OffsetA],                              // virtualIndex
         HasThisAdj ? Record[16 + OffsetB] : 0,             // thisAdjustment
         static_cast<DINode::DIFlags>(Record[11 + OffsetA]),// flags
         SPFlags,                                           // SPFlags
         HasUnit ? CUorFn : nullptr,                        // unit
         getMDOrNull(Record[13 + OffsetB]),                 // templateParams
         getMDOrNull(Record[14 + OffsetB]),                 // declaration
         getMDOrNull(Record[15 + OffsetB]),                 // retainedNodes
         HasThrownTypes ? getMDOrNull(Record[17 + OffsetB])
                        : nullptr                           // thrownTypes
         ));
    MetadataList.assignValue(SP, NextMetadataNo);
    NextMetadataNo++;

    // Upgrade sp->function mapping to function->sp mapping.
    if (HasFn) {
      if (auto *CMD = dyn_cast_or_null<ConstantAsMetadata>(CUorFn))
        if (auto *F = dyn_cast<Function>(CMD->getValue())) {
          if (F->isMaterializable())
            // Defer until materialized; unmaterialized functions may not have
            // metadata.
            FunctionsWithSPs[F] = SP;
          else if (!F->empty())
            F->setSubprogram(SP);
        }
    }
    break;
  }
  case bitc::METADATA_LEXICAL_BLOCK: {
    if (Record.size() != 5)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DILexicalBlock,
                        (Context, getMDOrNull(Record[1]),
                         getMDOrNull(Record[2]), Record[3], Record[4])),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_LEXICAL_BLOCK_FILE: {
    if (Record.size() != 4)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DILexicalBlockFile,
                        (Context, getMDOrNull(Record[1]),
                         getMDOrNull(Record[2]), Record[3])),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_NAMESPACE: {
    // Newer versions of DINamespace dropped file and line.
    MDString *Name;
    if (Record.size() == 3)
      Name = getMDString(Record[2]);
    else if (Record.size() == 5)
      Name = getMDString(Record[3]);
    else
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    bool ExportSymbols = Record[0] & 2;
    MetadataList.assignValue(
        GET_OR_DISTINCT(DINamespace,
                        (Context, getMDOrNull(Record[1]), Name, ExportSymbols)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_MACRO: {
    if (Record.size() != 5)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIMacro,
                        (Context, Record[1], Record[2], getMDString(Record[3]),
                         getMDString(Record[4]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_MACRO_FILE: {
    if (Record.size() != 5)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIMacroFile,
                        (Context, Record[1], Record[2], getMDOrNull(Record[3]),
                         getMDOrNull(Record[4]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_TEMPLATE_TYPE: {
    if (Record.size() != 3)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(GET_OR_DISTINCT(DITemplateTypeParameter,
                                             (Context, getMDString(Record[1]),
                                              getDITypeRefOrNull(Record[2]))),
                             NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_TEMPLATE_VALUE: {
    if (Record.size() != 5)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DITemplateValueParameter,
                        (Context, Record[1], getMDString(Record[2]),
                         getDITypeRefOrNull(Record[3]),
                         getMDOrNull(Record[4]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_GLOBAL_VAR: {
    if (Record.size() < 11 || Record.size() > 13)
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    unsigned Version = Record[0] >> 1;

    if (Version == 2) {
      MetadataList.assignValue(
          GET_OR_DISTINCT(
              DIGlobalVariable,
              (Context, getMDOrNull(Record[1]), getMDString(Record[2]),
               getMDString(Record[3]), getMDOrNull(Record[4]), Record[5],
               getDITypeRefOrNull(Record[6]), Record[7], Record[8],
               getMDOrNull(Record[9]), getMDOrNull(Record[10]), Record[11])),
          NextMetadataNo);

      NextMetadataNo++;
    } else if (Version == 1) {
      // No upgrade necessary. A null field will be introduced to indicate
      // that no parameter information is available.
      MetadataList.assignValue(
          GET_OR_DISTINCT(DIGlobalVariable,
                          (Context, getMDOrNull(Record[1]),
                           getMDString(Record[2]), getMDString(Record[3]),
                           getMDOrNull(Record[4]), Record[5],
                           getDITypeRefOrNull(Record[6]), Record[7], Record[8],
                           getMDOrNull(Record[10]), nullptr, Record[11])),
          NextMetadataNo);

      NextMetadataNo++;
    } else if (Version == 0) {
      // Upgrade old metadata, which stored a global variable reference or a
      // ConstantInt here.
      NeedUpgradeToDIGlobalVariableExpression = true;
      Metadata *Expr = getMDOrNull(Record[9]);
      uint32_t AlignInBits = 0;
      if (Record.size() > 11) {
        if (Record[11] > (uint64_t)std::numeric_limits<uint32_t>::max())
          return error("Alignment value is too large");
        AlignInBits = Record[11];
      }
      GlobalVariable *Attach = nullptr;
      if (auto *CMD = dyn_cast_or_null<ConstantAsMetadata>(Expr)) {
        if (auto *GV = dyn_cast<GlobalVariable>(CMD->getValue())) {
          Attach = GV;
          Expr = nullptr;
        } else if (auto *CI = dyn_cast<ConstantInt>(CMD->getValue())) {
          Expr = DIExpression::get(Context,
                                   {dwarf::DW_OP_constu, CI->getZExtValue(),
                                    dwarf::DW_OP_stack_value});
        } else {
          Expr = nullptr;
        }
      }
      DIGlobalVariable *DGV = GET_OR_DISTINCT(
          DIGlobalVariable,
          (Context, getMDOrNull(Record[1]), getMDString(Record[2]),
           getMDString(Record[3]), getMDOrNull(Record[4]), Record[5],
           getDITypeRefOrNull(Record[6]), Record[7], Record[8],
           getMDOrNull(Record[10]), nullptr, AlignInBits));

      DIGlobalVariableExpression *DGVE = nullptr;
      if (Attach || Expr)
        DGVE = DIGlobalVariableExpression::getDistinct(
            Context, DGV, Expr ? Expr : DIExpression::get(Context, {}));
      if (Attach)
        Attach->addDebugInfo(DGVE);

      auto *MDNode = Expr ? cast<Metadata>(DGVE) : cast<Metadata>(DGV);
      MetadataList.assignValue(MDNode, NextMetadataNo);
      NextMetadataNo++;
    } else
      return error("Invalid record");

    break;
  }
  case bitc::METADATA_LOCAL_VAR: {
    // 10th field is for the obseleted 'inlinedAt:' field.
    if (Record.size() < 8 || Record.size() > 10)
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    bool HasAlignment = Record[0] & 2;
    // 2nd field used to be an artificial tag, either DW_TAG_auto_variable or
    // DW_TAG_arg_variable, if we have alignment flag encoded it means, that
    // this is newer version of record which doesn't have artificial tag.
    bool HasTag = !HasAlignment && Record.size() > 8;
    DINode::DIFlags Flags = static_cast<DINode::DIFlags>(Record[7 + HasTag]);
    uint32_t AlignInBits = 0;
    if (HasAlignment) {
      if (Record[8 + HasTag] > (uint64_t)std::numeric_limits<uint32_t>::max())
        return error("Alignment value is too large");
      AlignInBits = Record[8 + HasTag];
    }
    MetadataList.assignValue(
        GET_OR_DISTINCT(DILocalVariable,
                        (Context, getMDOrNull(Record[1 + HasTag]),
                         getMDString(Record[2 + HasTag]),
                         getMDOrNull(Record[3 + HasTag]), Record[4 + HasTag],
                         getDITypeRefOrNull(Record[5 + HasTag]),
                         Record[6 + HasTag], Flags, AlignInBits)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_LABEL: {
    if (Record.size() != 5)
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    MetadataList.assignValue(
        GET_OR_DISTINCT(DILabel,
                        (Context, getMDOrNull(Record[1]),
                         getMDString(Record[2]),
                         getMDOrNull(Record[3]), Record[4])),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_EXPRESSION: {
    if (Record.size() < 1)
      return error("Invalid record");

    IsDistinct = Record[0] & 1;
    uint64_t Version = Record[0] >> 1;
    auto Elts = MutableArrayRef<uint64_t>(Record).slice(1);

    SmallVector<uint64_t, 6> Buffer;
    if (Error Err = upgradeDIExpression(Version, Elts, Buffer))
      return Err;

    MetadataList.assignValue(
        GET_OR_DISTINCT(DIExpression, (Context, Elts)), NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_GLOBAL_VAR_EXPR: {
    if (Record.size() != 3)
      return error("Invalid record");

    IsDistinct = Record[0];
    Metadata *Expr = getMDOrNull(Record[2]);
    if (!Expr)
      Expr = DIExpression::get(Context, {});
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIGlobalVariableExpression,
                        (Context, getMDOrNull(Record[1]), Expr)),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_OBJC_PROPERTY: {
    if (Record.size() != 8)
      return error("Invalid record");

    IsDistinct = Record[0];
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIObjCProperty,
                        (Context, getMDString(Record[1]),
                         getMDOrNull(Record[2]), Record[3],
                         getMDString(Record[4]), getMDString(Record[5]),
                         Record[6], getDITypeRefOrNull(Record[7]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_IMPORTED_ENTITY: {
    if (Record.size() != 6 && Record.size() != 7)
      return error("Invalid record");

    IsDistinct = Record[0];
    bool HasFile = (Record.size() == 7);
    MetadataList.assignValue(
        GET_OR_DISTINCT(DIImportedEntity,
                        (Context, Record[1], getMDOrNull(Record[2]),
                         getDITypeRefOrNull(Record[3]),
                         HasFile ? getMDOrNull(Record[6]) : nullptr,
                         HasFile ? Record[4] : 0, getMDString(Record[5]))),
        NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_STRING_OLD: {
    std::string String(Record.begin(), Record.end());

    // Test for upgrading !llvm.loop.
    HasSeenOldLoopTags |= mayBeOldLoopAttachmentTag(String);
    ++NumMDStringLoaded;
    Metadata *MD = MDString::get(Context, String);
    MetadataList.assignValue(MD, NextMetadataNo);
    NextMetadataNo++;
    break;
  }
  case bitc::METADATA_STRINGS: {
    auto CreateNextMDString = [&](StringRef Str) {
      ++NumMDStringLoaded;
      MetadataList.assignValue(MDString::get(Context, Str), NextMetadataNo);
      NextMetadataNo++;
    };
    if (Error Err = parseMetadataStrings(Record, Blob, CreateNextMDString))
      return Err;
    break;
  }
  case bitc::METADATA_GLOBAL_DECL_ATTACHMENT: {
    if (Record.size() % 2 == 0)
      return error("Invalid record");
    unsigned ValueID = Record[0];
    if (ValueID >= ValueList.size())
      return error("Invalid record");
    if (auto *GO = dyn_cast<GlobalObject>(ValueList[ValueID]))
      if (Error Err = parseGlobalObjectAttachment(
              *GO, ArrayRef<uint64_t>(Record).slice(1)))
        return Err;
    break;
  }
  case bitc::METADATA_KIND: {
    // Support older bitcode files that had METADATA_KIND records in a
    // block with METADATA_BLOCK_ID.
    if (Error Err = parseMetadataKindRecord(Record))
      return Err;
    break;
  }
  }
  return Error::success();
#undef GET_OR_DISTINCT
}

Error MetadataLoader::MetadataLoaderImpl::parseMetadataStrings(
    ArrayRef<uint64_t> Record, StringRef Blob,
    function_ref<void(StringRef)> CallBack) {
  // All the MDStrings in the block are emitted together in a single
  // record.  The strings are concatenated and stored in a blob along with
  // their sizes.
  if (Record.size() != 2)
    return error("Invalid record: metadata strings layout");

  unsigned NumStrings = Record[0];
  unsigned StringsOffset = Record[1];
  if (!NumStrings)
    return error("Invalid record: metadata strings with no strings");
  if (StringsOffset > Blob.size())
    return error("Invalid record: metadata strings corrupt offset");

  StringRef Lengths = Blob.slice(0, StringsOffset);
  SimpleBitstreamCursor R(Lengths);

  StringRef Strings = Blob.drop_front(StringsOffset);
  do {
    if (R.AtEndOfStream())
      return error("Invalid record: metadata strings bad length");

    unsigned Size = R.ReadVBR(6);
    if (Strings.size() < Size)
      return error("Invalid record: metadata strings truncated chars");

    CallBack(Strings.slice(0, Size));
    Strings = Strings.drop_front(Size);
  } while (--NumStrings);

  return Error::success();
}

Error MetadataLoader::MetadataLoaderImpl::parseGlobalObjectAttachment(
    GlobalObject &GO, ArrayRef<uint64_t> Record) {
  assert(Record.size() % 2 == 0);
  for (unsigned I = 0, E = Record.size(); I != E; I += 2) {
    auto K = MDKindMap.find(Record[I]);
    if (K == MDKindMap.end())
      return error("Invalid ID");
    MDNode *MD = MetadataList.getMDNodeFwdRefOrNull(Record[I + 1]);
    if (!MD)
      return error("Invalid metadata attachment: expect fwd ref to MDNode");
    GO.addMetadata(K->second, *MD);
  }
  return Error::success();
}

/// Parse metadata attachments.
Error MetadataLoader::MetadataLoaderImpl::parseMetadataAttachment(
    Function &F, const SmallVectorImpl<Instruction *> &InstructionList) {
  if (Stream.EnterSubBlock(bitc::METADATA_ATTACHMENT_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;
  PlaceholderQueue Placeholders;

  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      resolveForwardRefsAndPlaceholders(Placeholders);
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a metadata attachment record.
    Record.clear();
    ++NumMDRecordLoaded;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: // Default behavior: ignore.
      break;
    case bitc::METADATA_ATTACHMENT: {
      unsigned RecordLength = Record.size();
      if (Record.empty())
        return error("Invalid record");
      if (RecordLength % 2 == 0) {
        // A function attachment.
        if (Error Err = parseGlobalObjectAttachment(F, Record))
          return Err;
        continue;
      }

      // An instruction attachment.
      Instruction *Inst = InstructionList[Record[0]];
      for (unsigned i = 1; i != RecordLength; i = i + 2) {
        unsigned Kind = Record[i];
        DenseMap<unsigned, unsigned>::iterator I = MDKindMap.find(Kind);
        if (I == MDKindMap.end())
          return error("Invalid ID");
        if (I->second == LLVMContext::MD_tbaa && StripTBAA)
          continue;

        auto Idx = Record[i + 1];
        if (Idx < (MDStringRef.size() + GlobalMetadataBitPosIndex.size()) &&
            !MetadataList.lookup(Idx)) {
          // Load the attachment if it is in the lazy-loadable range and hasn't
          // been loaded yet.
          lazyLoadOneMetadata(Idx, Placeholders);
          resolveForwardRefsAndPlaceholders(Placeholders);
        }

        Metadata *Node = MetadataList.getMetadataFwdRef(Idx);
        if (isa<LocalAsMetadata>(Node))
          // Drop the attachment.  This used to be legal, but there's no
          // upgrade path.
          break;
        MDNode *MD = dyn_cast_or_null<MDNode>(Node);
        if (!MD)
          return error("Invalid metadata attachment");

        if (HasSeenOldLoopTags && I->second == LLVMContext::MD_loop)
          MD = upgradeInstructionLoopAttachment(*MD);

        if (I->second == LLVMContext::MD_tbaa) {
          assert(!MD->isTemporary() && "should load MDs before attachments");
          MD = UpgradeTBAANode(*MD);
        }
        Inst->setMetadata(I->second, MD);
      }
      break;
    }
    }
  }
}

/// Parse a single METADATA_KIND record, inserting result in MDKindMap.
Error MetadataLoader::MetadataLoaderImpl::parseMetadataKindRecord(
    SmallVectorImpl<uint64_t> &Record) {
  if (Record.size() < 2)
    return error("Invalid record");

  unsigned Kind = Record[0];
  SmallString<8> Name(Record.begin() + 1, Record.end());

  unsigned NewKind = TheModule.getMDKindID(Name.str());
  if (!MDKindMap.insert(std::make_pair(Kind, NewKind)).second)
    return error("Conflicting METADATA_KIND records");
  return Error::success();
}

/// Parse the metadata kinds out of the METADATA_KIND_BLOCK.
Error MetadataLoader::MetadataLoaderImpl::parseMetadataKinds() {
  if (Stream.EnterSubBlock(bitc::METADATA_KIND_BLOCK_ID))
    return error("Invalid record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (true) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return error("Malformed block");
    case BitstreamEntry::EndBlock:
      return Error::success();
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    ++NumMDRecordLoaded;
    unsigned Code = Stream.readRecord(Entry.ID, Record);
    switch (Code) {
    default: // Default behavior: ignore.
      break;
    case bitc::METADATA_KIND: {
      if (Error Err = parseMetadataKindRecord(Record))
        return Err;
      break;
    }
    }
  }
}

MetadataLoader &MetadataLoader::operator=(MetadataLoader &&RHS) {
  Pimpl = std::move(RHS.Pimpl);
  return *this;
}
MetadataLoader::MetadataLoader(MetadataLoader &&RHS)
    : Pimpl(std::move(RHS.Pimpl)) {}

MetadataLoader::~MetadataLoader() = default;
MetadataLoader::MetadataLoader(BitstreamCursor &Stream, Module &TheModule,
                               BitcodeReaderValueList &ValueList,
                               bool IsImporting,
                               std::function<Type *(unsigned)> getTypeByID)
    : Pimpl(llvm::make_unique<MetadataLoaderImpl>(
          Stream, TheModule, ValueList, std::move(getTypeByID), IsImporting)) {}

Error MetadataLoader::parseMetadata(bool ModuleLevel) {
  return Pimpl->parseMetadata(ModuleLevel);
}

bool MetadataLoader::hasFwdRefs() const { return Pimpl->hasFwdRefs(); }

/// Return the given metadata, creating a replaceable forward reference if
/// necessary.
Metadata *MetadataLoader::getMetadataFwdRefOrLoad(unsigned Idx) {
  return Pimpl->getMetadataFwdRefOrLoad(Idx);
}

DISubprogram *MetadataLoader::lookupSubprogramForFunction(Function *F) {
  return Pimpl->lookupSubprogramForFunction(F);
}

Error MetadataLoader::parseMetadataAttachment(
    Function &F, const SmallVectorImpl<Instruction *> &InstructionList) {
  return Pimpl->parseMetadataAttachment(F, InstructionList);
}

Error MetadataLoader::parseMetadataKinds() {
  return Pimpl->parseMetadataKinds();
}

void MetadataLoader::setStripTBAA(bool StripTBAA) {
  return Pimpl->setStripTBAA(StripTBAA);
}

bool MetadataLoader::isStrippingTBAA() { return Pimpl->isStrippingTBAA(); }

unsigned MetadataLoader::size() const { return Pimpl->size(); }
void MetadataLoader::shrinkTo(unsigned N) { return Pimpl->shrinkTo(N); }

void MetadataLoader::upgradeDebugIntrinsics(Function &F) {
  return Pimpl->upgradeDebugIntrinsics(F);
}
