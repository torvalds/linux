//===- AsmWriter.cpp - Printing LLVM as an assembly file ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This library implements `print` family of functions in classes like
// Module, Function, Value, etc. In-memory representation of those classes is
// converted to IR strings.
//
// Note that these routines must be extremely tolerant of various errors in the
// LLVM code, because it can be used for debugging transformations.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalIndirectSymbol.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/UseListOrder.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

// Make virtual table appear in this compilation unit.
AssemblyAnnotationWriter::~AssemblyAnnotationWriter() = default;

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

namespace {

struct OrderMap {
  DenseMap<const Value *, std::pair<unsigned, bool>> IDs;

  unsigned size() const { return IDs.size(); }
  std::pair<unsigned, bool> &operator[](const Value *V) { return IDs[V]; }

  std::pair<unsigned, bool> lookup(const Value *V) const {
    return IDs.lookup(V);
  }

  void index(const Value *V) {
    // Explicitly sequence get-size and insert-value operations to avoid UB.
    unsigned ID = IDs.size() + 1;
    IDs[V].first = ID;
  }
};

} // end anonymous namespace

static void orderValue(const Value *V, OrderMap &OM) {
  if (OM.lookup(V).first)
    return;

  if (const Constant *C = dyn_cast<Constant>(V))
    if (C->getNumOperands() && !isa<GlobalValue>(C))
      for (const Value *Op : C->operands())
        if (!isa<BasicBlock>(Op) && !isa<GlobalValue>(Op))
          orderValue(Op, OM);

  // Note: we cannot cache this lookup above, since inserting into the map
  // changes the map's size, and thus affects the other IDs.
  OM.index(V);
}

static OrderMap orderModule(const Module *M) {
  // This needs to match the order used by ValueEnumerator::ValueEnumerator()
  // and ValueEnumerator::incorporateFunction().
  OrderMap OM;

  for (const GlobalVariable &G : M->globals()) {
    if (G.hasInitializer())
      if (!isa<GlobalValue>(G.getInitializer()))
        orderValue(G.getInitializer(), OM);
    orderValue(&G, OM);
  }
  for (const GlobalAlias &A : M->aliases()) {
    if (!isa<GlobalValue>(A.getAliasee()))
      orderValue(A.getAliasee(), OM);
    orderValue(&A, OM);
  }
  for (const GlobalIFunc &I : M->ifuncs()) {
    if (!isa<GlobalValue>(I.getResolver()))
      orderValue(I.getResolver(), OM);
    orderValue(&I, OM);
  }
  for (const Function &F : *M) {
    for (const Use &U : F.operands())
      if (!isa<GlobalValue>(U.get()))
        orderValue(U.get(), OM);

    orderValue(&F, OM);

    if (F.isDeclaration())
      continue;

    for (const Argument &A : F.args())
      orderValue(&A, OM);
    for (const BasicBlock &BB : F) {
      orderValue(&BB, OM);
      for (const Instruction &I : BB) {
        for (const Value *Op : I.operands())
          if ((isa<Constant>(*Op) && !isa<GlobalValue>(*Op)) ||
              isa<InlineAsm>(*Op))
            orderValue(Op, OM);
        orderValue(&I, OM);
      }
    }
  }
  return OM;
}

static void predictValueUseListOrderImpl(const Value *V, const Function *F,
                                         unsigned ID, const OrderMap &OM,
                                         UseListOrderStack &Stack) {
  // Predict use-list order for this one.
  using Entry = std::pair<const Use *, unsigned>;
  SmallVector<Entry, 64> List;
  for (const Use &U : V->uses())
    // Check if this user will be serialized.
    if (OM.lookup(U.getUser()).first)
      List.push_back(std::make_pair(&U, List.size()));

  if (List.size() < 2)
    // We may have lost some users.
    return;

  bool GetsReversed =
      !isa<GlobalVariable>(V) && !isa<Function>(V) && !isa<BasicBlock>(V);
  if (auto *BA = dyn_cast<BlockAddress>(V))
    ID = OM.lookup(BA->getBasicBlock()).first;
  llvm::sort(List, [&](const Entry &L, const Entry &R) {
    const Use *LU = L.first;
    const Use *RU = R.first;
    if (LU == RU)
      return false;

    auto LID = OM.lookup(LU->getUser()).first;
    auto RID = OM.lookup(RU->getUser()).first;

    // If ID is 4, then expect: 7 6 5 1 2 3.
    if (LID < RID) {
      if (GetsReversed)
        if (RID <= ID)
          return true;
      return false;
    }
    if (RID < LID) {
      if (GetsReversed)
        if (LID <= ID)
          return false;
      return true;
    }

    // LID and RID are equal, so we have different operands of the same user.
    // Assume operands are added in order for all instructions.
    if (GetsReversed)
      if (LID <= ID)
        return LU->getOperandNo() < RU->getOperandNo();
    return LU->getOperandNo() > RU->getOperandNo();
  });

  if (std::is_sorted(
          List.begin(), List.end(),
          [](const Entry &L, const Entry &R) { return L.second < R.second; }))
    // Order is already correct.
    return;

  // Store the shuffle.
  Stack.emplace_back(V, F, List.size());
  assert(List.size() == Stack.back().Shuffle.size() && "Wrong size");
  for (size_t I = 0, E = List.size(); I != E; ++I)
    Stack.back().Shuffle[I] = List[I].second;
}

static void predictValueUseListOrder(const Value *V, const Function *F,
                                     OrderMap &OM, UseListOrderStack &Stack) {
  auto &IDPair = OM[V];
  assert(IDPair.first && "Unmapped value");
  if (IDPair.second)
    // Already predicted.
    return;

  // Do the actual prediction.
  IDPair.second = true;
  if (!V->use_empty() && std::next(V->use_begin()) != V->use_end())
    predictValueUseListOrderImpl(V, F, IDPair.first, OM, Stack);

  // Recursive descent into constants.
  if (const Constant *C = dyn_cast<Constant>(V))
    if (C->getNumOperands()) // Visit GlobalValues.
      for (const Value *Op : C->operands())
        if (isa<Constant>(Op)) // Visit GlobalValues.
          predictValueUseListOrder(Op, F, OM, Stack);
}

static UseListOrderStack predictUseListOrder(const Module *M) {
  OrderMap OM = orderModule(M);

  // Use-list orders need to be serialized after all the users have been added
  // to a value, or else the shuffles will be incomplete.  Store them per
  // function in a stack.
  //
  // Aside from function order, the order of values doesn't matter much here.
  UseListOrderStack Stack;

  // We want to visit the functions backward now so we can list function-local
  // constants in the last Function they're used in.  Module-level constants
  // have already been visited above.
  for (const Function &F : make_range(M->rbegin(), M->rend())) {
    if (F.isDeclaration())
      continue;
    for (const BasicBlock &BB : F)
      predictValueUseListOrder(&BB, &F, OM, Stack);
    for (const Argument &A : F.args())
      predictValueUseListOrder(&A, &F, OM, Stack);
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB)
        for (const Value *Op : I.operands())
          if (isa<Constant>(*Op) || isa<InlineAsm>(*Op)) // Visit GlobalValues.
            predictValueUseListOrder(Op, &F, OM, Stack);
    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB)
        predictValueUseListOrder(&I, &F, OM, Stack);
  }

  // Visit globals last.
  for (const GlobalVariable &G : M->globals())
    predictValueUseListOrder(&G, nullptr, OM, Stack);
  for (const Function &F : *M)
    predictValueUseListOrder(&F, nullptr, OM, Stack);
  for (const GlobalAlias &A : M->aliases())
    predictValueUseListOrder(&A, nullptr, OM, Stack);
  for (const GlobalIFunc &I : M->ifuncs())
    predictValueUseListOrder(&I, nullptr, OM, Stack);
  for (const GlobalVariable &G : M->globals())
    if (G.hasInitializer())
      predictValueUseListOrder(G.getInitializer(), nullptr, OM, Stack);
  for (const GlobalAlias &A : M->aliases())
    predictValueUseListOrder(A.getAliasee(), nullptr, OM, Stack);
  for (const GlobalIFunc &I : M->ifuncs())
    predictValueUseListOrder(I.getResolver(), nullptr, OM, Stack);
  for (const Function &F : *M)
    for (const Use &U : F.operands())
      predictValueUseListOrder(U.get(), nullptr, OM, Stack);

  return Stack;
}

static const Module *getModuleFromVal(const Value *V) {
  if (const Argument *MA = dyn_cast<Argument>(V))
    return MA->getParent() ? MA->getParent()->getParent() : nullptr;

  if (const BasicBlock *BB = dyn_cast<BasicBlock>(V))
    return BB->getParent() ? BB->getParent()->getParent() : nullptr;

  if (const Instruction *I = dyn_cast<Instruction>(V)) {
    const Function *M = I->getParent() ? I->getParent()->getParent() : nullptr;
    return M ? M->getParent() : nullptr;
  }

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(V))
    return GV->getParent();

  if (const auto *MAV = dyn_cast<MetadataAsValue>(V)) {
    for (const User *U : MAV->users())
      if (isa<Instruction>(U))
        if (const Module *M = getModuleFromVal(U))
          return M;
    return nullptr;
  }

  return nullptr;
}

static void PrintCallingConv(unsigned cc, raw_ostream &Out) {
  switch (cc) {
  default:                         Out << "cc" << cc; break;
  case CallingConv::Fast:          Out << "fastcc"; break;
  case CallingConv::Cold:          Out << "coldcc"; break;
  case CallingConv::WebKit_JS:     Out << "webkit_jscc"; break;
  case CallingConv::AnyReg:        Out << "anyregcc"; break;
  case CallingConv::PreserveMost:  Out << "preserve_mostcc"; break;
  case CallingConv::PreserveAll:   Out << "preserve_allcc"; break;
  case CallingConv::CXX_FAST_TLS:  Out << "cxx_fast_tlscc"; break;
  case CallingConv::GHC:           Out << "ghccc"; break;
  case CallingConv::X86_StdCall:   Out << "x86_stdcallcc"; break;
  case CallingConv::X86_FastCall:  Out << "x86_fastcallcc"; break;
  case CallingConv::X86_ThisCall:  Out << "x86_thiscallcc"; break;
  case CallingConv::X86_RegCall:   Out << "x86_regcallcc"; break;
  case CallingConv::X86_VectorCall:Out << "x86_vectorcallcc"; break;
  case CallingConv::Intel_OCL_BI:  Out << "intel_ocl_bicc"; break;
  case CallingConv::ARM_APCS:      Out << "arm_apcscc"; break;
  case CallingConv::ARM_AAPCS:     Out << "arm_aapcscc"; break;
  case CallingConv::ARM_AAPCS_VFP: Out << "arm_aapcs_vfpcc"; break;
  case CallingConv::AArch64_VectorCall: Out << "aarch64_vector_pcs"; break;
  case CallingConv::MSP430_INTR:   Out << "msp430_intrcc"; break;
  case CallingConv::AVR_INTR:      Out << "avr_intrcc "; break;
  case CallingConv::AVR_SIGNAL:    Out << "avr_signalcc "; break;
  case CallingConv::PTX_Kernel:    Out << "ptx_kernel"; break;
  case CallingConv::PTX_Device:    Out << "ptx_device"; break;
  case CallingConv::X86_64_SysV:   Out << "x86_64_sysvcc"; break;
  case CallingConv::Win64:         Out << "win64cc"; break;
  case CallingConv::SPIR_FUNC:     Out << "spir_func"; break;
  case CallingConv::SPIR_KERNEL:   Out << "spir_kernel"; break;
  case CallingConv::Swift:         Out << "swiftcc"; break;
  case CallingConv::X86_INTR:      Out << "x86_intrcc"; break;
  case CallingConv::HHVM:          Out << "hhvmcc"; break;
  case CallingConv::HHVM_C:        Out << "hhvm_ccc"; break;
  case CallingConv::AMDGPU_VS:     Out << "amdgpu_vs"; break;
  case CallingConv::AMDGPU_LS:     Out << "amdgpu_ls"; break;
  case CallingConv::AMDGPU_HS:     Out << "amdgpu_hs"; break;
  case CallingConv::AMDGPU_ES:     Out << "amdgpu_es"; break;
  case CallingConv::AMDGPU_GS:     Out << "amdgpu_gs"; break;
  case CallingConv::AMDGPU_PS:     Out << "amdgpu_ps"; break;
  case CallingConv::AMDGPU_CS:     Out << "amdgpu_cs"; break;
  case CallingConv::AMDGPU_KERNEL: Out << "amdgpu_kernel"; break;
  }
}

enum PrefixType {
  GlobalPrefix,
  ComdatPrefix,
  LabelPrefix,
  LocalPrefix,
  NoPrefix
};

void llvm::printLLVMNameWithoutPrefix(raw_ostream &OS, StringRef Name) {
  assert(!Name.empty() && "Cannot get empty name!");

  // Scan the name to see if it needs quotes first.
  bool NeedsQuotes = isdigit(static_cast<unsigned char>(Name[0]));
  if (!NeedsQuotes) {
    for (unsigned i = 0, e = Name.size(); i != e; ++i) {
      // By making this unsigned, the value passed in to isalnum will always be
      // in the range 0-255.  This is important when building with MSVC because
      // its implementation will assert.  This situation can arise when dealing
      // with UTF-8 multibyte characters.
      unsigned char C = Name[i];
      if (!isalnum(static_cast<unsigned char>(C)) && C != '-' && C != '.' &&
          C != '_') {
        NeedsQuotes = true;
        break;
      }
    }
  }

  // If we didn't need any quotes, just write out the name in one blast.
  if (!NeedsQuotes) {
    OS << Name;
    return;
  }

  // Okay, we need quotes.  Output the quotes and escape any scary characters as
  // needed.
  OS << '"';
  printEscapedString(Name, OS);
  OS << '"';
}

/// Turn the specified name into an 'LLVM name', which is either prefixed with %
/// (if the string only contains simple characters) or is surrounded with ""'s
/// (if it has special chars in it). Print it out.
static void PrintLLVMName(raw_ostream &OS, StringRef Name, PrefixType Prefix) {
  switch (Prefix) {
  case NoPrefix:
    break;
  case GlobalPrefix:
    OS << '@';
    break;
  case ComdatPrefix:
    OS << '$';
    break;
  case LabelPrefix:
    break;
  case LocalPrefix:
    OS << '%';
    break;
  }
  printLLVMNameWithoutPrefix(OS, Name);
}

/// Turn the specified name into an 'LLVM name', which is either prefixed with %
/// (if the string only contains simple characters) or is surrounded with ""'s
/// (if it has special chars in it). Print it out.
static void PrintLLVMName(raw_ostream &OS, const Value *V) {
  PrintLLVMName(OS, V->getName(),
                isa<GlobalValue>(V) ? GlobalPrefix : LocalPrefix);
}

namespace {

class TypePrinting {
public:
  TypePrinting(const Module *M = nullptr) : DeferredM(M) {}

  TypePrinting(const TypePrinting &) = delete;
  TypePrinting &operator=(const TypePrinting &) = delete;

  /// The named types that are used by the current module.
  TypeFinder &getNamedTypes();

  /// The numbered types, number to type mapping.
  std::vector<StructType *> &getNumberedTypes();

  bool empty();

  void print(Type *Ty, raw_ostream &OS);

  void printStructBody(StructType *Ty, raw_ostream &OS);

private:
  void incorporateTypes();

  /// A module to process lazily when needed. Set to nullptr as soon as used.
  const Module *DeferredM;

  TypeFinder NamedTypes;

  // The numbered types, along with their value.
  DenseMap<StructType *, unsigned> Type2Number;

  std::vector<StructType *> NumberedTypes;
};

} // end anonymous namespace

TypeFinder &TypePrinting::getNamedTypes() {
  incorporateTypes();
  return NamedTypes;
}

std::vector<StructType *> &TypePrinting::getNumberedTypes() {
  incorporateTypes();

  // We know all the numbers that each type is used and we know that it is a
  // dense assignment. Convert the map to an index table, if it's not done
  // already (judging from the sizes):
  if (NumberedTypes.size() == Type2Number.size())
    return NumberedTypes;

  NumberedTypes.resize(Type2Number.size());
  for (const auto &P : Type2Number) {
    assert(P.second < NumberedTypes.size() && "Didn't get a dense numbering?");
    assert(!NumberedTypes[P.second] && "Didn't get a unique numbering?");
    NumberedTypes[P.second] = P.first;
  }
  return NumberedTypes;
}

bool TypePrinting::empty() {
  incorporateTypes();
  return NamedTypes.empty() && Type2Number.empty();
}

void TypePrinting::incorporateTypes() {
  if (!DeferredM)
    return;

  NamedTypes.run(*DeferredM, false);
  DeferredM = nullptr;

  // The list of struct types we got back includes all the struct types, split
  // the unnamed ones out to a numbering and remove the anonymous structs.
  unsigned NextNumber = 0;

  std::vector<StructType*>::iterator NextToUse = NamedTypes.begin(), I, E;
  for (I = NamedTypes.begin(), E = NamedTypes.end(); I != E; ++I) {
    StructType *STy = *I;

    // Ignore anonymous types.
    if (STy->isLiteral())
      continue;

    if (STy->getName().empty())
      Type2Number[STy] = NextNumber++;
    else
      *NextToUse++ = STy;
  }

  NamedTypes.erase(NextToUse, NamedTypes.end());
}

/// Write the specified type to the specified raw_ostream, making use of type
/// names or up references to shorten the type name where possible.
void TypePrinting::print(Type *Ty, raw_ostream &OS) {
  switch (Ty->getTypeID()) {
  case Type::VoidTyID:      OS << "void"; return;
  case Type::HalfTyID:      OS << "half"; return;
  case Type::FloatTyID:     OS << "float"; return;
  case Type::DoubleTyID:    OS << "double"; return;
  case Type::X86_FP80TyID:  OS << "x86_fp80"; return;
  case Type::FP128TyID:     OS << "fp128"; return;
  case Type::PPC_FP128TyID: OS << "ppc_fp128"; return;
  case Type::LabelTyID:     OS << "label"; return;
  case Type::MetadataTyID:  OS << "metadata"; return;
  case Type::X86_MMXTyID:   OS << "x86_mmx"; return;
  case Type::TokenTyID:     OS << "token"; return;
  case Type::IntegerTyID:
    OS << 'i' << cast<IntegerType>(Ty)->getBitWidth();
    return;

  case Type::FunctionTyID: {
    FunctionType *FTy = cast<FunctionType>(Ty);
    print(FTy->getReturnType(), OS);
    OS << " (";
    for (FunctionType::param_iterator I = FTy->param_begin(),
         E = FTy->param_end(); I != E; ++I) {
      if (I != FTy->param_begin())
        OS << ", ";
      print(*I, OS);
    }
    if (FTy->isVarArg()) {
      if (FTy->getNumParams()) OS << ", ";
      OS << "...";
    }
    OS << ')';
    return;
  }
  case Type::StructTyID: {
    StructType *STy = cast<StructType>(Ty);

    if (STy->isLiteral())
      return printStructBody(STy, OS);

    if (!STy->getName().empty())
      return PrintLLVMName(OS, STy->getName(), LocalPrefix);

    incorporateTypes();
    const auto I = Type2Number.find(STy);
    if (I != Type2Number.end())
      OS << '%' << I->second;
    else  // Not enumerated, print the hex address.
      OS << "%\"type " << STy << '\"';
    return;
  }
  case Type::PointerTyID: {
    PointerType *PTy = cast<PointerType>(Ty);
    print(PTy->getElementType(), OS);
    if (unsigned AddressSpace = PTy->getAddressSpace())
      OS << " addrspace(" << AddressSpace << ')';
    OS << '*';
    return;
  }
  case Type::ArrayTyID: {
    ArrayType *ATy = cast<ArrayType>(Ty);
    OS << '[' << ATy->getNumElements() << " x ";
    print(ATy->getElementType(), OS);
    OS << ']';
    return;
  }
  case Type::VectorTyID: {
    VectorType *PTy = cast<VectorType>(Ty);
    OS << "<" << PTy->getNumElements() << " x ";
    print(PTy->getElementType(), OS);
    OS << '>';
    return;
  }
  }
  llvm_unreachable("Invalid TypeID");
}

void TypePrinting::printStructBody(StructType *STy, raw_ostream &OS) {
  if (STy->isOpaque()) {
    OS << "opaque";
    return;
  }

  if (STy->isPacked())
    OS << '<';

  if (STy->getNumElements() == 0) {
    OS << "{}";
  } else {
    StructType::element_iterator I = STy->element_begin();
    OS << "{ ";
    print(*I++, OS);
    for (StructType::element_iterator E = STy->element_end(); I != E; ++I) {
      OS << ", ";
      print(*I, OS);
    }

    OS << " }";
  }
  if (STy->isPacked())
    OS << '>';
}

namespace llvm {

//===----------------------------------------------------------------------===//
// SlotTracker Class: Enumerate slot numbers for unnamed values
//===----------------------------------------------------------------------===//
/// This class provides computation of slot numbers for LLVM Assembly writing.
///
class SlotTracker {
public:
  /// ValueMap - A mapping of Values to slot numbers.
  using ValueMap = DenseMap<const Value *, unsigned>;

private:
  /// TheModule - The module for which we are holding slot numbers.
  const Module* TheModule;

  /// TheFunction - The function for which we are holding slot numbers.
  const Function* TheFunction = nullptr;
  bool FunctionProcessed = false;
  bool ShouldInitializeAllMetadata;

  /// The summary index for which we are holding slot numbers.
  const ModuleSummaryIndex *TheIndex = nullptr;

  /// mMap - The slot map for the module level data.
  ValueMap mMap;
  unsigned mNext = 0;

  /// fMap - The slot map for the function level data.
  ValueMap fMap;
  unsigned fNext = 0;

  /// mdnMap - Map for MDNodes.
  DenseMap<const MDNode*, unsigned> mdnMap;
  unsigned mdnNext = 0;

  /// asMap - The slot map for attribute sets.
  DenseMap<AttributeSet, unsigned> asMap;
  unsigned asNext = 0;

  /// ModulePathMap - The slot map for Module paths used in the summary index.
  StringMap<unsigned> ModulePathMap;
  unsigned ModulePathNext = 0;

  /// GUIDMap - The slot map for GUIDs used in the summary index.
  DenseMap<GlobalValue::GUID, unsigned> GUIDMap;
  unsigned GUIDNext = 0;

  /// TypeIdMap - The slot map for type ids used in the summary index.
  StringMap<unsigned> TypeIdMap;
  unsigned TypeIdNext = 0;

public:
  /// Construct from a module.
  ///
  /// If \c ShouldInitializeAllMetadata, initializes all metadata in all
  /// functions, giving correct numbering for metadata referenced only from
  /// within a function (even if no functions have been initialized).
  explicit SlotTracker(const Module *M,
                       bool ShouldInitializeAllMetadata = false);

  /// Construct from a function, starting out in incorp state.
  ///
  /// If \c ShouldInitializeAllMetadata, initializes all metadata in all
  /// functions, giving correct numbering for metadata referenced only from
  /// within a function (even if no functions have been initialized).
  explicit SlotTracker(const Function *F,
                       bool ShouldInitializeAllMetadata = false);

  /// Construct from a module summary index.
  explicit SlotTracker(const ModuleSummaryIndex *Index);

  SlotTracker(const SlotTracker &) = delete;
  SlotTracker &operator=(const SlotTracker &) = delete;

  /// Return the slot number of the specified value in it's type
  /// plane.  If something is not in the SlotTracker, return -1.
  int getLocalSlot(const Value *V);
  int getGlobalSlot(const GlobalValue *V);
  int getMetadataSlot(const MDNode *N);
  int getAttributeGroupSlot(AttributeSet AS);
  int getModulePathSlot(StringRef Path);
  int getGUIDSlot(GlobalValue::GUID GUID);
  int getTypeIdSlot(StringRef Id);

  /// If you'd like to deal with a function instead of just a module, use
  /// this method to get its data into the SlotTracker.
  void incorporateFunction(const Function *F) {
    TheFunction = F;
    FunctionProcessed = false;
  }

  const Function *getFunction() const { return TheFunction; }

  /// After calling incorporateFunction, use this method to remove the
  /// most recently incorporated function from the SlotTracker. This
  /// will reset the state of the machine back to just the module contents.
  void purgeFunction();

  /// MDNode map iterators.
  using mdn_iterator = DenseMap<const MDNode*, unsigned>::iterator;

  mdn_iterator mdn_begin() { return mdnMap.begin(); }
  mdn_iterator mdn_end() { return mdnMap.end(); }
  unsigned mdn_size() const { return mdnMap.size(); }
  bool mdn_empty() const { return mdnMap.empty(); }

  /// AttributeSet map iterators.
  using as_iterator = DenseMap<AttributeSet, unsigned>::iterator;

  as_iterator as_begin()   { return asMap.begin(); }
  as_iterator as_end()     { return asMap.end(); }
  unsigned as_size() const { return asMap.size(); }
  bool as_empty() const    { return asMap.empty(); }

  /// GUID map iterators.
  using guid_iterator = DenseMap<GlobalValue::GUID, unsigned>::iterator;

  /// These functions do the actual initialization.
  inline void initializeIfNeeded();
  void initializeIndexIfNeeded();

  // Implementation Details
private:
  /// CreateModuleSlot - Insert the specified GlobalValue* into the slot table.
  void CreateModuleSlot(const GlobalValue *V);

  /// CreateMetadataSlot - Insert the specified MDNode* into the slot table.
  void CreateMetadataSlot(const MDNode *N);

  /// CreateFunctionSlot - Insert the specified Value* into the slot table.
  void CreateFunctionSlot(const Value *V);

  /// Insert the specified AttributeSet into the slot table.
  void CreateAttributeSetSlot(AttributeSet AS);

  inline void CreateModulePathSlot(StringRef Path);
  void CreateGUIDSlot(GlobalValue::GUID GUID);
  void CreateTypeIdSlot(StringRef Id);

  /// Add all of the module level global variables (and their initializers)
  /// and function declarations, but not the contents of those functions.
  void processModule();
  void processIndex();

  /// Add all of the functions arguments, basic blocks, and instructions.
  void processFunction();

  /// Add the metadata directly attached to a GlobalObject.
  void processGlobalObjectMetadata(const GlobalObject &GO);

  /// Add all of the metadata from a function.
  void processFunctionMetadata(const Function &F);

  /// Add all of the metadata from an instruction.
  void processInstructionMetadata(const Instruction &I);
};

} // end namespace llvm

ModuleSlotTracker::ModuleSlotTracker(SlotTracker &Machine, const Module *M,
                                     const Function *F)
    : M(M), F(F), Machine(&Machine) {}

ModuleSlotTracker::ModuleSlotTracker(const Module *M,
                                     bool ShouldInitializeAllMetadata)
    : ShouldCreateStorage(M),
      ShouldInitializeAllMetadata(ShouldInitializeAllMetadata), M(M) {}

ModuleSlotTracker::~ModuleSlotTracker() = default;

SlotTracker *ModuleSlotTracker::getMachine() {
  if (!ShouldCreateStorage)
    return Machine;

  ShouldCreateStorage = false;
  MachineStorage =
      llvm::make_unique<SlotTracker>(M, ShouldInitializeAllMetadata);
  Machine = MachineStorage.get();
  return Machine;
}

void ModuleSlotTracker::incorporateFunction(const Function &F) {
  // Using getMachine() may lazily create the slot tracker.
  if (!getMachine())
    return;

  // Nothing to do if this is the right function already.
  if (this->F == &F)
    return;
  if (this->F)
    Machine->purgeFunction();
  Machine->incorporateFunction(&F);
  this->F = &F;
}

int ModuleSlotTracker::getLocalSlot(const Value *V) {
  assert(F && "No function incorporated");
  return Machine->getLocalSlot(V);
}

static SlotTracker *createSlotTracker(const Value *V) {
  if (const Argument *FA = dyn_cast<Argument>(V))
    return new SlotTracker(FA->getParent());

  if (const Instruction *I = dyn_cast<Instruction>(V))
    if (I->getParent())
      return new SlotTracker(I->getParent()->getParent());

  if (const BasicBlock *BB = dyn_cast<BasicBlock>(V))
    return new SlotTracker(BB->getParent());

  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    return new SlotTracker(GV->getParent());

  if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(V))
    return new SlotTracker(GA->getParent());

  if (const GlobalIFunc *GIF = dyn_cast<GlobalIFunc>(V))
    return new SlotTracker(GIF->getParent());

  if (const Function *Func = dyn_cast<Function>(V))
    return new SlotTracker(Func);

  return nullptr;
}

#if 0
#define ST_DEBUG(X) dbgs() << X
#else
#define ST_DEBUG(X)
#endif

// Module level constructor. Causes the contents of the Module (sans functions)
// to be added to the slot table.
SlotTracker::SlotTracker(const Module *M, bool ShouldInitializeAllMetadata)
    : TheModule(M), ShouldInitializeAllMetadata(ShouldInitializeAllMetadata) {}

// Function level constructor. Causes the contents of the Module and the one
// function provided to be added to the slot table.
SlotTracker::SlotTracker(const Function *F, bool ShouldInitializeAllMetadata)
    : TheModule(F ? F->getParent() : nullptr), TheFunction(F),
      ShouldInitializeAllMetadata(ShouldInitializeAllMetadata) {}

SlotTracker::SlotTracker(const ModuleSummaryIndex *Index)
    : TheModule(nullptr), ShouldInitializeAllMetadata(false), TheIndex(Index) {}

inline void SlotTracker::initializeIfNeeded() {
  if (TheModule) {
    processModule();
    TheModule = nullptr; ///< Prevent re-processing next time we're called.
  }

  if (TheFunction && !FunctionProcessed)
    processFunction();
}

void SlotTracker::initializeIndexIfNeeded() {
  if (!TheIndex)
    return;
  processIndex();
  TheIndex = nullptr; ///< Prevent re-processing next time we're called.
}

// Iterate through all the global variables, functions, and global
// variable initializers and create slots for them.
void SlotTracker::processModule() {
  ST_DEBUG("begin processModule!\n");

  // Add all of the unnamed global variables to the value table.
  for (const GlobalVariable &Var : TheModule->globals()) {
    if (!Var.hasName())
      CreateModuleSlot(&Var);
    processGlobalObjectMetadata(Var);
    auto Attrs = Var.getAttributes();
    if (Attrs.hasAttributes())
      CreateAttributeSetSlot(Attrs);
  }

  for (const GlobalAlias &A : TheModule->aliases()) {
    if (!A.hasName())
      CreateModuleSlot(&A);
  }

  for (const GlobalIFunc &I : TheModule->ifuncs()) {
    if (!I.hasName())
      CreateModuleSlot(&I);
  }

  // Add metadata used by named metadata.
  for (const NamedMDNode &NMD : TheModule->named_metadata()) {
    for (unsigned i = 0, e = NMD.getNumOperands(); i != e; ++i)
      CreateMetadataSlot(NMD.getOperand(i));
  }

  for (const Function &F : *TheModule) {
    if (!F.hasName())
      // Add all the unnamed functions to the table.
      CreateModuleSlot(&F);

    if (ShouldInitializeAllMetadata)
      processFunctionMetadata(F);

    // Add all the function attributes to the table.
    // FIXME: Add attributes of other objects?
    AttributeSet FnAttrs = F.getAttributes().getFnAttributes();
    if (FnAttrs.hasAttributes())
      CreateAttributeSetSlot(FnAttrs);
  }

  ST_DEBUG("end processModule!\n");
}

// Process the arguments, basic blocks, and instructions  of a function.
void SlotTracker::processFunction() {
  ST_DEBUG("begin processFunction!\n");
  fNext = 0;

  // Process function metadata if it wasn't hit at the module-level.
  if (!ShouldInitializeAllMetadata)
    processFunctionMetadata(*TheFunction);

  // Add all the function arguments with no names.
  for(Function::const_arg_iterator AI = TheFunction->arg_begin(),
      AE = TheFunction->arg_end(); AI != AE; ++AI)
    if (!AI->hasName())
      CreateFunctionSlot(&*AI);

  ST_DEBUG("Inserting Instructions:\n");

  // Add all of the basic blocks and instructions with no names.
  for (auto &BB : *TheFunction) {
    if (!BB.hasName())
      CreateFunctionSlot(&BB);

    for (auto &I : BB) {
      if (!I.getType()->isVoidTy() && !I.hasName())
        CreateFunctionSlot(&I);

      // We allow direct calls to any llvm.foo function here, because the
      // target may not be linked into the optimizer.
      if (const auto *Call = dyn_cast<CallBase>(&I)) {
        // Add all the call attributes to the table.
        AttributeSet Attrs = Call->getAttributes().getFnAttributes();
        if (Attrs.hasAttributes())
          CreateAttributeSetSlot(Attrs);
      }
    }
  }

  FunctionProcessed = true;

  ST_DEBUG("end processFunction!\n");
}

// Iterate through all the GUID in the index and create slots for them.
void SlotTracker::processIndex() {
  ST_DEBUG("begin processIndex!\n");
  assert(TheIndex);

  // The first block of slots are just the module ids, which start at 0 and are
  // assigned consecutively. Since the StringMap iteration order isn't
  // guaranteed, use a std::map to order by module ID before assigning slots.
  std::map<uint64_t, StringRef> ModuleIdToPathMap;
  for (auto &ModPath : TheIndex->modulePaths())
    ModuleIdToPathMap[ModPath.second.first] = ModPath.first();
  for (auto &ModPair : ModuleIdToPathMap)
    CreateModulePathSlot(ModPair.second);

  // Start numbering the GUIDs after the module ids.
  GUIDNext = ModulePathNext;

  for (auto &GlobalList : *TheIndex)
    CreateGUIDSlot(GlobalList.first);

  // Start numbering the TypeIds after the GUIDs.
  TypeIdNext = GUIDNext;

  for (auto TidIter = TheIndex->typeIds().begin();
       TidIter != TheIndex->typeIds().end(); TidIter++)
    CreateTypeIdSlot(TidIter->second.first);

  ST_DEBUG("end processIndex!\n");
}

void SlotTracker::processGlobalObjectMetadata(const GlobalObject &GO) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  GO.getAllMetadata(MDs);
  for (auto &MD : MDs)
    CreateMetadataSlot(MD.second);
}

void SlotTracker::processFunctionMetadata(const Function &F) {
  processGlobalObjectMetadata(F);
  for (auto &BB : F) {
    for (auto &I : BB)
      processInstructionMetadata(I);
  }
}

void SlotTracker::processInstructionMetadata(const Instruction &I) {
  // Process metadata used directly by intrinsics.
  if (const CallInst *CI = dyn_cast<CallInst>(&I))
    if (Function *F = CI->getCalledFunction())
      if (F->isIntrinsic())
        for (auto &Op : I.operands())
          if (auto *V = dyn_cast_or_null<MetadataAsValue>(Op))
            if (MDNode *N = dyn_cast<MDNode>(V->getMetadata()))
              CreateMetadataSlot(N);

  // Process metadata attached to this instruction.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  I.getAllMetadata(MDs);
  for (auto &MD : MDs)
    CreateMetadataSlot(MD.second);
}

/// Clean up after incorporating a function. This is the only way to get out of
/// the function incorporation state that affects get*Slot/Create*Slot. Function
/// incorporation state is indicated by TheFunction != 0.
void SlotTracker::purgeFunction() {
  ST_DEBUG("begin purgeFunction!\n");
  fMap.clear(); // Simply discard the function level map
  TheFunction = nullptr;
  FunctionProcessed = false;
  ST_DEBUG("end purgeFunction!\n");
}

/// getGlobalSlot - Get the slot number of a global value.
int SlotTracker::getGlobalSlot(const GlobalValue *V) {
  // Check for uninitialized state and do lazy initialization.
  initializeIfNeeded();

  // Find the value in the module map
  ValueMap::iterator MI = mMap.find(V);
  return MI == mMap.end() ? -1 : (int)MI->second;
}

/// getMetadataSlot - Get the slot number of a MDNode.
int SlotTracker::getMetadataSlot(const MDNode *N) {
  // Check for uninitialized state and do lazy initialization.
  initializeIfNeeded();

  // Find the MDNode in the module map
  mdn_iterator MI = mdnMap.find(N);
  return MI == mdnMap.end() ? -1 : (int)MI->second;
}

/// getLocalSlot - Get the slot number for a value that is local to a function.
int SlotTracker::getLocalSlot(const Value *V) {
  assert(!isa<Constant>(V) && "Can't get a constant or global slot with this!");

  // Check for uninitialized state and do lazy initialization.
  initializeIfNeeded();

  ValueMap::iterator FI = fMap.find(V);
  return FI == fMap.end() ? -1 : (int)FI->second;
}

int SlotTracker::getAttributeGroupSlot(AttributeSet AS) {
  // Check for uninitialized state and do lazy initialization.
  initializeIfNeeded();

  // Find the AttributeSet in the module map.
  as_iterator AI = asMap.find(AS);
  return AI == asMap.end() ? -1 : (int)AI->second;
}

int SlotTracker::getModulePathSlot(StringRef Path) {
  // Check for uninitialized state and do lazy initialization.
  initializeIndexIfNeeded();

  // Find the Module path in the map
  auto I = ModulePathMap.find(Path);
  return I == ModulePathMap.end() ? -1 : (int)I->second;
}

int SlotTracker::getGUIDSlot(GlobalValue::GUID GUID) {
  // Check for uninitialized state and do lazy initialization.
  initializeIndexIfNeeded();

  // Find the GUID in the map
  guid_iterator I = GUIDMap.find(GUID);
  return I == GUIDMap.end() ? -1 : (int)I->second;
}

int SlotTracker::getTypeIdSlot(StringRef Id) {
  // Check for uninitialized state and do lazy initialization.
  initializeIndexIfNeeded();

  // Find the TypeId string in the map
  auto I = TypeIdMap.find(Id);
  return I == TypeIdMap.end() ? -1 : (int)I->second;
}

/// CreateModuleSlot - Insert the specified GlobalValue* into the slot table.
void SlotTracker::CreateModuleSlot(const GlobalValue *V) {
  assert(V && "Can't insert a null Value into SlotTracker!");
  assert(!V->getType()->isVoidTy() && "Doesn't need a slot!");
  assert(!V->hasName() && "Doesn't need a slot!");

  unsigned DestSlot = mNext++;
  mMap[V] = DestSlot;

  ST_DEBUG("  Inserting value [" << V->getType() << "] = " << V << " slot=" <<
           DestSlot << " [");
  // G = Global, F = Function, A = Alias, I = IFunc, o = other
  ST_DEBUG((isa<GlobalVariable>(V) ? 'G' :
            (isa<Function>(V) ? 'F' :
             (isa<GlobalAlias>(V) ? 'A' :
              (isa<GlobalIFunc>(V) ? 'I' : 'o')))) << "]\n");
}

/// CreateSlot - Create a new slot for the specified value if it has no name.
void SlotTracker::CreateFunctionSlot(const Value *V) {
  assert(!V->getType()->isVoidTy() && !V->hasName() && "Doesn't need a slot!");

  unsigned DestSlot = fNext++;
  fMap[V] = DestSlot;

  // G = Global, F = Function, o = other
  ST_DEBUG("  Inserting value [" << V->getType() << "] = " << V << " slot=" <<
           DestSlot << " [o]\n");
}

/// CreateModuleSlot - Insert the specified MDNode* into the slot table.
void SlotTracker::CreateMetadataSlot(const MDNode *N) {
  assert(N && "Can't insert a null Value into SlotTracker!");

  // Don't make slots for DIExpressions. We just print them inline everywhere.
  if (isa<DIExpression>(N))
    return;

  unsigned DestSlot = mdnNext;
  if (!mdnMap.insert(std::make_pair(N, DestSlot)).second)
    return;
  ++mdnNext;

  // Recursively add any MDNodes referenced by operands.
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i)
    if (const MDNode *Op = dyn_cast_or_null<MDNode>(N->getOperand(i)))
      CreateMetadataSlot(Op);
}

void SlotTracker::CreateAttributeSetSlot(AttributeSet AS) {
  assert(AS.hasAttributes() && "Doesn't need a slot!");

  as_iterator I = asMap.find(AS);
  if (I != asMap.end())
    return;

  unsigned DestSlot = asNext++;
  asMap[AS] = DestSlot;
}

/// Create a new slot for the specified Module
void SlotTracker::CreateModulePathSlot(StringRef Path) {
  ModulePathMap[Path] = ModulePathNext++;
}

/// Create a new slot for the specified GUID
void SlotTracker::CreateGUIDSlot(GlobalValue::GUID GUID) {
  GUIDMap[GUID] = GUIDNext++;
}

/// Create a new slot for the specified Id
void SlotTracker::CreateTypeIdSlot(StringRef Id) {
  TypeIdMap[Id] = TypeIdNext++;
}

//===----------------------------------------------------------------------===//
// AsmWriter Implementation
//===----------------------------------------------------------------------===//

static void WriteAsOperandInternal(raw_ostream &Out, const Value *V,
                                   TypePrinting *TypePrinter,
                                   SlotTracker *Machine,
                                   const Module *Context);

static void WriteAsOperandInternal(raw_ostream &Out, const Metadata *MD,
                                   TypePrinting *TypePrinter,
                                   SlotTracker *Machine, const Module *Context,
                                   bool FromValue = false);

static void WriteOptimizationInfo(raw_ostream &Out, const User *U) {
  if (const FPMathOperator *FPO = dyn_cast<const FPMathOperator>(U)) {
    // 'Fast' is an abbreviation for all fast-math-flags.
    if (FPO->isFast())
      Out << " fast";
    else {
      if (FPO->hasAllowReassoc())
        Out << " reassoc";
      if (FPO->hasNoNaNs())
        Out << " nnan";
      if (FPO->hasNoInfs())
        Out << " ninf";
      if (FPO->hasNoSignedZeros())
        Out << " nsz";
      if (FPO->hasAllowReciprocal())
        Out << " arcp";
      if (FPO->hasAllowContract())
        Out << " contract";
      if (FPO->hasApproxFunc())
        Out << " afn";
    }
  }

  if (const OverflowingBinaryOperator *OBO =
        dyn_cast<OverflowingBinaryOperator>(U)) {
    if (OBO->hasNoUnsignedWrap())
      Out << " nuw";
    if (OBO->hasNoSignedWrap())
      Out << " nsw";
  } else if (const PossiblyExactOperator *Div =
               dyn_cast<PossiblyExactOperator>(U)) {
    if (Div->isExact())
      Out << " exact";
  } else if (const GEPOperator *GEP = dyn_cast<GEPOperator>(U)) {
    if (GEP->isInBounds())
      Out << " inbounds";
  }
}

static void WriteConstantInternal(raw_ostream &Out, const Constant *CV,
                                  TypePrinting &TypePrinter,
                                  SlotTracker *Machine,
                                  const Module *Context) {
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CV)) {
    if (CI->getType()->isIntegerTy(1)) {
      Out << (CI->getZExtValue() ? "true" : "false");
      return;
    }
    Out << CI->getValue();
    return;
  }

  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CV)) {
    const APFloat &APF = CFP->getValueAPF();
    if (&APF.getSemantics() == &APFloat::IEEEsingle() ||
        &APF.getSemantics() == &APFloat::IEEEdouble()) {
      // We would like to output the FP constant value in exponential notation,
      // but we cannot do this if doing so will lose precision.  Check here to
      // make sure that we only output it in exponential format if we can parse
      // the value back and get the same value.
      //
      bool ignored;
      bool isDouble = &APF.getSemantics() == &APFloat::IEEEdouble();
      bool isInf = APF.isInfinity();
      bool isNaN = APF.isNaN();
      if (!isInf && !isNaN) {
        double Val = isDouble ? APF.convertToDouble() : APF.convertToFloat();
        SmallString<128> StrVal;
        APF.toString(StrVal, 6, 0, false);
        // Check to make sure that the stringized number is not some string like
        // "Inf" or NaN, that atof will accept, but the lexer will not.  Check
        // that the string matches the "[-+]?[0-9]" regex.
        //
        assert(((StrVal[0] >= '0' && StrVal[0] <= '9') ||
                ((StrVal[0] == '-' || StrVal[0] == '+') &&
                 (StrVal[1] >= '0' && StrVal[1] <= '9'))) &&
               "[-+]?[0-9] regex does not match!");
        // Reparse stringized version!
        if (APFloat(APFloat::IEEEdouble(), StrVal).convertToDouble() == Val) {
          Out << StrVal;
          return;
        }
      }
      // Otherwise we could not reparse it to exactly the same value, so we must
      // output the string in hexadecimal format!  Note that loading and storing
      // floating point types changes the bits of NaNs on some hosts, notably
      // x86, so we must not use these types.
      static_assert(sizeof(double) == sizeof(uint64_t),
                    "assuming that double is 64 bits!");
      APFloat apf = APF;
      // Floats are represented in ASCII IR as double, convert.
      if (!isDouble)
        apf.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven,
                          &ignored);
      Out << format_hex(apf.bitcastToAPInt().getZExtValue(), 0, /*Upper=*/true);
      return;
    }

    // Either half, or some form of long double.
    // These appear as a magic letter identifying the type, then a
    // fixed number of hex digits.
    Out << "0x";
    APInt API = APF.bitcastToAPInt();
    if (&APF.getSemantics() == &APFloat::x87DoubleExtended()) {
      Out << 'K';
      Out << format_hex_no_prefix(API.getHiBits(16).getZExtValue(), 4,
                                  /*Upper=*/true);
      Out << format_hex_no_prefix(API.getLoBits(64).getZExtValue(), 16,
                                  /*Upper=*/true);
      return;
    } else if (&APF.getSemantics() == &APFloat::IEEEquad()) {
      Out << 'L';
      Out << format_hex_no_prefix(API.getLoBits(64).getZExtValue(), 16,
                                  /*Upper=*/true);
      Out << format_hex_no_prefix(API.getHiBits(64).getZExtValue(), 16,
                                  /*Upper=*/true);
    } else if (&APF.getSemantics() == &APFloat::PPCDoubleDouble()) {
      Out << 'M';
      Out << format_hex_no_prefix(API.getLoBits(64).getZExtValue(), 16,
                                  /*Upper=*/true);
      Out << format_hex_no_prefix(API.getHiBits(64).getZExtValue(), 16,
                                  /*Upper=*/true);
    } else if (&APF.getSemantics() == &APFloat::IEEEhalf()) {
      Out << 'H';
      Out << format_hex_no_prefix(API.getZExtValue(), 4,
                                  /*Upper=*/true);
    } else
      llvm_unreachable("Unsupported floating point type");
    return;
  }

  if (isa<ConstantAggregateZero>(CV)) {
    Out << "zeroinitializer";
    return;
  }

  if (const BlockAddress *BA = dyn_cast<BlockAddress>(CV)) {
    Out << "blockaddress(";
    WriteAsOperandInternal(Out, BA->getFunction(), &TypePrinter, Machine,
                           Context);
    Out << ", ";
    WriteAsOperandInternal(Out, BA->getBasicBlock(), &TypePrinter, Machine,
                           Context);
    Out << ")";
    return;
  }

  if (const ConstantArray *CA = dyn_cast<ConstantArray>(CV)) {
    Type *ETy = CA->getType()->getElementType();
    Out << '[';
    TypePrinter.print(ETy, Out);
    Out << ' ';
    WriteAsOperandInternal(Out, CA->getOperand(0),
                           &TypePrinter, Machine,
                           Context);
    for (unsigned i = 1, e = CA->getNumOperands(); i != e; ++i) {
      Out << ", ";
      TypePrinter.print(ETy, Out);
      Out << ' ';
      WriteAsOperandInternal(Out, CA->getOperand(i), &TypePrinter, Machine,
                             Context);
    }
    Out << ']';
    return;
  }

  if (const ConstantDataArray *CA = dyn_cast<ConstantDataArray>(CV)) {
    // As a special case, print the array as a string if it is an array of
    // i8 with ConstantInt values.
    if (CA->isString()) {
      Out << "c\"";
      printEscapedString(CA->getAsString(), Out);
      Out << '"';
      return;
    }

    Type *ETy = CA->getType()->getElementType();
    Out << '[';
    TypePrinter.print(ETy, Out);
    Out << ' ';
    WriteAsOperandInternal(Out, CA->getElementAsConstant(0),
                           &TypePrinter, Machine,
                           Context);
    for (unsigned i = 1, e = CA->getNumElements(); i != e; ++i) {
      Out << ", ";
      TypePrinter.print(ETy, Out);
      Out << ' ';
      WriteAsOperandInternal(Out, CA->getElementAsConstant(i), &TypePrinter,
                             Machine, Context);
    }
    Out << ']';
    return;
  }

  if (const ConstantStruct *CS = dyn_cast<ConstantStruct>(CV)) {
    if (CS->getType()->isPacked())
      Out << '<';
    Out << '{';
    unsigned N = CS->getNumOperands();
    if (N) {
      Out << ' ';
      TypePrinter.print(CS->getOperand(0)->getType(), Out);
      Out << ' ';

      WriteAsOperandInternal(Out, CS->getOperand(0), &TypePrinter, Machine,
                             Context);

      for (unsigned i = 1; i < N; i++) {
        Out << ", ";
        TypePrinter.print(CS->getOperand(i)->getType(), Out);
        Out << ' ';

        WriteAsOperandInternal(Out, CS->getOperand(i), &TypePrinter, Machine,
                               Context);
      }
      Out << ' ';
    }

    Out << '}';
    if (CS->getType()->isPacked())
      Out << '>';
    return;
  }

  if (isa<ConstantVector>(CV) || isa<ConstantDataVector>(CV)) {
    Type *ETy = CV->getType()->getVectorElementType();
    Out << '<';
    TypePrinter.print(ETy, Out);
    Out << ' ';
    WriteAsOperandInternal(Out, CV->getAggregateElement(0U), &TypePrinter,
                           Machine, Context);
    for (unsigned i = 1, e = CV->getType()->getVectorNumElements(); i != e;++i){
      Out << ", ";
      TypePrinter.print(ETy, Out);
      Out << ' ';
      WriteAsOperandInternal(Out, CV->getAggregateElement(i), &TypePrinter,
                             Machine, Context);
    }
    Out << '>';
    return;
  }

  if (isa<ConstantPointerNull>(CV)) {
    Out << "null";
    return;
  }

  if (isa<ConstantTokenNone>(CV)) {
    Out << "none";
    return;
  }

  if (isa<UndefValue>(CV)) {
    Out << "undef";
    return;
  }

  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(CV)) {
    Out << CE->getOpcodeName();
    WriteOptimizationInfo(Out, CE);
    if (CE->isCompare())
      Out << ' ' << CmpInst::getPredicateName(
                        static_cast<CmpInst::Predicate>(CE->getPredicate()));
    Out << " (";

    Optional<unsigned> InRangeOp;
    if (const GEPOperator *GEP = dyn_cast<GEPOperator>(CE)) {
      TypePrinter.print(GEP->getSourceElementType(), Out);
      Out << ", ";
      InRangeOp = GEP->getInRangeIndex();
      if (InRangeOp)
        ++*InRangeOp;
    }

    for (User::const_op_iterator OI=CE->op_begin(); OI != CE->op_end(); ++OI) {
      if (InRangeOp && unsigned(OI - CE->op_begin()) == *InRangeOp)
        Out << "inrange ";
      TypePrinter.print((*OI)->getType(), Out);
      Out << ' ';
      WriteAsOperandInternal(Out, *OI, &TypePrinter, Machine, Context);
      if (OI+1 != CE->op_end())
        Out << ", ";
    }

    if (CE->hasIndices()) {
      ArrayRef<unsigned> Indices = CE->getIndices();
      for (unsigned i = 0, e = Indices.size(); i != e; ++i)
        Out << ", " << Indices[i];
    }

    if (CE->isCast()) {
      Out << " to ";
      TypePrinter.print(CE->getType(), Out);
    }

    Out << ')';
    return;
  }

  Out << "<placeholder or erroneous Constant>";
}

static void writeMDTuple(raw_ostream &Out, const MDTuple *Node,
                         TypePrinting *TypePrinter, SlotTracker *Machine,
                         const Module *Context) {
  Out << "!{";
  for (unsigned mi = 0, me = Node->getNumOperands(); mi != me; ++mi) {
    const Metadata *MD = Node->getOperand(mi);
    if (!MD)
      Out << "null";
    else if (auto *MDV = dyn_cast<ValueAsMetadata>(MD)) {
      Value *V = MDV->getValue();
      TypePrinter->print(V->getType(), Out);
      Out << ' ';
      WriteAsOperandInternal(Out, V, TypePrinter, Machine, Context);
    } else {
      WriteAsOperandInternal(Out, MD, TypePrinter, Machine, Context);
    }
    if (mi + 1 != me)
      Out << ", ";
  }

  Out << "}";
}

namespace {

struct FieldSeparator {
  bool Skip = true;
  const char *Sep;

  FieldSeparator(const char *Sep = ", ") : Sep(Sep) {}
};

raw_ostream &operator<<(raw_ostream &OS, FieldSeparator &FS) {
  if (FS.Skip) {
    FS.Skip = false;
    return OS;
  }
  return OS << FS.Sep;
}

struct MDFieldPrinter {
  raw_ostream &Out;
  FieldSeparator FS;
  TypePrinting *TypePrinter = nullptr;
  SlotTracker *Machine = nullptr;
  const Module *Context = nullptr;

  explicit MDFieldPrinter(raw_ostream &Out) : Out(Out) {}
  MDFieldPrinter(raw_ostream &Out, TypePrinting *TypePrinter,
                 SlotTracker *Machine, const Module *Context)
      : Out(Out), TypePrinter(TypePrinter), Machine(Machine), Context(Context) {
  }

  void printTag(const DINode *N);
  void printMacinfoType(const DIMacroNode *N);
  void printChecksum(const DIFile::ChecksumInfo<StringRef> &N);
  void printString(StringRef Name, StringRef Value,
                   bool ShouldSkipEmpty = true);
  void printMetadata(StringRef Name, const Metadata *MD,
                     bool ShouldSkipNull = true);
  template <class IntTy>
  void printInt(StringRef Name, IntTy Int, bool ShouldSkipZero = true);
  void printBool(StringRef Name, bool Value, Optional<bool> Default = None);
  void printDIFlags(StringRef Name, DINode::DIFlags Flags);
  void printDISPFlags(StringRef Name, DISubprogram::DISPFlags Flags);
  template <class IntTy, class Stringifier>
  void printDwarfEnum(StringRef Name, IntTy Value, Stringifier toString,
                      bool ShouldSkipZero = true);
  void printEmissionKind(StringRef Name, DICompileUnit::DebugEmissionKind EK);
  void printNameTableKind(StringRef Name,
                          DICompileUnit::DebugNameTableKind NTK);
};

} // end anonymous namespace

void MDFieldPrinter::printTag(const DINode *N) {
  Out << FS << "tag: ";
  auto Tag = dwarf::TagString(N->getTag());
  if (!Tag.empty())
    Out << Tag;
  else
    Out << N->getTag();
}

void MDFieldPrinter::printMacinfoType(const DIMacroNode *N) {
  Out << FS << "type: ";
  auto Type = dwarf::MacinfoString(N->getMacinfoType());
  if (!Type.empty())
    Out << Type;
  else
    Out << N->getMacinfoType();
}

void MDFieldPrinter::printChecksum(
    const DIFile::ChecksumInfo<StringRef> &Checksum) {
  Out << FS << "checksumkind: " << Checksum.getKindAsString();
  printString("checksum", Checksum.Value, /* ShouldSkipEmpty */ false);
}

void MDFieldPrinter::printString(StringRef Name, StringRef Value,
                                 bool ShouldSkipEmpty) {
  if (ShouldSkipEmpty && Value.empty())
    return;

  Out << FS << Name << ": \"";
  printEscapedString(Value, Out);
  Out << "\"";
}

static void writeMetadataAsOperand(raw_ostream &Out, const Metadata *MD,
                                   TypePrinting *TypePrinter,
                                   SlotTracker *Machine,
                                   const Module *Context) {
  if (!MD) {
    Out << "null";
    return;
  }
  WriteAsOperandInternal(Out, MD, TypePrinter, Machine, Context);
}

void MDFieldPrinter::printMetadata(StringRef Name, const Metadata *MD,
                                   bool ShouldSkipNull) {
  if (ShouldSkipNull && !MD)
    return;

  Out << FS << Name << ": ";
  writeMetadataAsOperand(Out, MD, TypePrinter, Machine, Context);
}

template <class IntTy>
void MDFieldPrinter::printInt(StringRef Name, IntTy Int, bool ShouldSkipZero) {
  if (ShouldSkipZero && !Int)
    return;

  Out << FS << Name << ": " << Int;
}

void MDFieldPrinter::printBool(StringRef Name, bool Value,
                               Optional<bool> Default) {
  if (Default && Value == *Default)
    return;
  Out << FS << Name << ": " << (Value ? "true" : "false");
}

void MDFieldPrinter::printDIFlags(StringRef Name, DINode::DIFlags Flags) {
  if (!Flags)
    return;

  Out << FS << Name << ": ";

  SmallVector<DINode::DIFlags, 8> SplitFlags;
  auto Extra = DINode::splitFlags(Flags, SplitFlags);

  FieldSeparator FlagsFS(" | ");
  for (auto F : SplitFlags) {
    auto StringF = DINode::getFlagString(F);
    assert(!StringF.empty() && "Expected valid flag");
    Out << FlagsFS << StringF;
  }
  if (Extra || SplitFlags.empty())
    Out << FlagsFS << Extra;
}

void MDFieldPrinter::printDISPFlags(StringRef Name,
                                    DISubprogram::DISPFlags Flags) {
  // Always print this field, because no flags in the IR at all will be
  // interpreted as old-style isDefinition: true.
  Out << FS << Name << ": ";

  if (!Flags) {
    Out << 0;
    return;
  }

  SmallVector<DISubprogram::DISPFlags, 8> SplitFlags;
  auto Extra = DISubprogram::splitFlags(Flags, SplitFlags);

  FieldSeparator FlagsFS(" | ");
  for (auto F : SplitFlags) {
    auto StringF = DISubprogram::getFlagString(F);
    assert(!StringF.empty() && "Expected valid flag");
    Out << FlagsFS << StringF;
  }
  if (Extra || SplitFlags.empty())
    Out << FlagsFS << Extra;
}

void MDFieldPrinter::printEmissionKind(StringRef Name,
                                       DICompileUnit::DebugEmissionKind EK) {
  Out << FS << Name << ": " << DICompileUnit::emissionKindString(EK);
}

void MDFieldPrinter::printNameTableKind(StringRef Name,
                                        DICompileUnit::DebugNameTableKind NTK) {
  if (NTK == DICompileUnit::DebugNameTableKind::Default)
    return;
  Out << FS << Name << ": " << DICompileUnit::nameTableKindString(NTK);
}

template <class IntTy, class Stringifier>
void MDFieldPrinter::printDwarfEnum(StringRef Name, IntTy Value,
                                    Stringifier toString, bool ShouldSkipZero) {
  if (!Value)
    return;

  Out << FS << Name << ": ";
  auto S = toString(Value);
  if (!S.empty())
    Out << S;
  else
    Out << Value;
}

static void writeGenericDINode(raw_ostream &Out, const GenericDINode *N,
                               TypePrinting *TypePrinter, SlotTracker *Machine,
                               const Module *Context) {
  Out << "!GenericDINode(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printTag(N);
  Printer.printString("header", N->getHeader());
  if (N->getNumDwarfOperands()) {
    Out << Printer.FS << "operands: {";
    FieldSeparator IFS;
    for (auto &I : N->dwarf_operands()) {
      Out << IFS;
      writeMetadataAsOperand(Out, I, TypePrinter, Machine, Context);
    }
    Out << "}";
  }
  Out << ")";
}

static void writeDILocation(raw_ostream &Out, const DILocation *DL,
                            TypePrinting *TypePrinter, SlotTracker *Machine,
                            const Module *Context) {
  Out << "!DILocation(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  // Always output the line, since 0 is a relevant and important value for it.
  Printer.printInt("line", DL->getLine(), /* ShouldSkipZero */ false);
  Printer.printInt("column", DL->getColumn());
  Printer.printMetadata("scope", DL->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("inlinedAt", DL->getRawInlinedAt());
  Printer.printBool("isImplicitCode", DL->isImplicitCode(),
                    /* Default */ false);
  Out << ")";
}

static void writeDISubrange(raw_ostream &Out, const DISubrange *N,
                            TypePrinting *TypePrinter, SlotTracker *Machine,
                            const Module *Context) {
  Out << "!DISubrange(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  if (auto *CE = N->getCount().dyn_cast<ConstantInt*>())
    Printer.printInt("count", CE->getSExtValue(), /* ShouldSkipZero */ false);
  else
    Printer.printMetadata("count", N->getCount().dyn_cast<DIVariable*>(),
                          /*ShouldSkipNull */ false);
  Printer.printInt("lowerBound", N->getLowerBound());
  Out << ")";
}

static void writeDIEnumerator(raw_ostream &Out, const DIEnumerator *N,
                              TypePrinting *, SlotTracker *, const Module *) {
  Out << "!DIEnumerator(";
  MDFieldPrinter Printer(Out);
  Printer.printString("name", N->getName(), /* ShouldSkipEmpty */ false);
  if (N->isUnsigned()) {
    auto Value = static_cast<uint64_t>(N->getValue());
    Printer.printInt("value", Value, /* ShouldSkipZero */ false);
    Printer.printBool("isUnsigned", true);
  } else {
    Printer.printInt("value", N->getValue(), /* ShouldSkipZero */ false);
  }
  Out << ")";
}

static void writeDIBasicType(raw_ostream &Out, const DIBasicType *N,
                             TypePrinting *, SlotTracker *, const Module *) {
  Out << "!DIBasicType(";
  MDFieldPrinter Printer(Out);
  if (N->getTag() != dwarf::DW_TAG_base_type)
    Printer.printTag(N);
  Printer.printString("name", N->getName());
  Printer.printInt("size", N->getSizeInBits());
  Printer.printInt("align", N->getAlignInBits());
  Printer.printDwarfEnum("encoding", N->getEncoding(),
                         dwarf::AttributeEncodingString);
  Printer.printDIFlags("flags", N->getFlags());
  Out << ")";
}

static void writeDIDerivedType(raw_ostream &Out, const DIDerivedType *N,
                               TypePrinting *TypePrinter, SlotTracker *Machine,
                               const Module *Context) {
  Out << "!DIDerivedType(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printTag(N);
  Printer.printString("name", N->getName());
  Printer.printMetadata("scope", N->getRawScope());
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("baseType", N->getRawBaseType(),
                        /* ShouldSkipNull */ false);
  Printer.printInt("size", N->getSizeInBits());
  Printer.printInt("align", N->getAlignInBits());
  Printer.printInt("offset", N->getOffsetInBits());
  Printer.printDIFlags("flags", N->getFlags());
  Printer.printMetadata("extraData", N->getRawExtraData());
  if (const auto &DWARFAddressSpace = N->getDWARFAddressSpace())
    Printer.printInt("dwarfAddressSpace", *DWARFAddressSpace,
                     /* ShouldSkipZero */ false);
  Out << ")";
}

static void writeDICompositeType(raw_ostream &Out, const DICompositeType *N,
                                 TypePrinting *TypePrinter,
                                 SlotTracker *Machine, const Module *Context) {
  Out << "!DICompositeType(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printTag(N);
  Printer.printString("name", N->getName());
  Printer.printMetadata("scope", N->getRawScope());
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("baseType", N->getRawBaseType());
  Printer.printInt("size", N->getSizeInBits());
  Printer.printInt("align", N->getAlignInBits());
  Printer.printInt("offset", N->getOffsetInBits());
  Printer.printDIFlags("flags", N->getFlags());
  Printer.printMetadata("elements", N->getRawElements());
  Printer.printDwarfEnum("runtimeLang", N->getRuntimeLang(),
                         dwarf::LanguageString);
  Printer.printMetadata("vtableHolder", N->getRawVTableHolder());
  Printer.printMetadata("templateParams", N->getRawTemplateParams());
  Printer.printString("identifier", N->getIdentifier());
  Printer.printMetadata("discriminator", N->getRawDiscriminator());
  Out << ")";
}

static void writeDISubroutineType(raw_ostream &Out, const DISubroutineType *N,
                                  TypePrinting *TypePrinter,
                                  SlotTracker *Machine, const Module *Context) {
  Out << "!DISubroutineType(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printDIFlags("flags", N->getFlags());
  Printer.printDwarfEnum("cc", N->getCC(), dwarf::ConventionString);
  Printer.printMetadata("types", N->getRawTypeArray(),
                        /* ShouldSkipNull */ false);
  Out << ")";
}

static void writeDIFile(raw_ostream &Out, const DIFile *N, TypePrinting *,
                        SlotTracker *, const Module *) {
  Out << "!DIFile(";
  MDFieldPrinter Printer(Out);
  Printer.printString("filename", N->getFilename(),
                      /* ShouldSkipEmpty */ false);
  Printer.printString("directory", N->getDirectory(),
                      /* ShouldSkipEmpty */ false);
  // Print all values for checksum together, or not at all.
  if (N->getChecksum())
    Printer.printChecksum(*N->getChecksum());
  Printer.printString("source", N->getSource().getValueOr(StringRef()),
                      /* ShouldSkipEmpty */ true);
  Out << ")";
}

static void writeDICompileUnit(raw_ostream &Out, const DICompileUnit *N,
                               TypePrinting *TypePrinter, SlotTracker *Machine,
                               const Module *Context) {
  Out << "!DICompileUnit(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printDwarfEnum("language", N->getSourceLanguage(),
                         dwarf::LanguageString, /* ShouldSkipZero */ false);
  Printer.printMetadata("file", N->getRawFile(), /* ShouldSkipNull */ false);
  Printer.printString("producer", N->getProducer());
  Printer.printBool("isOptimized", N->isOptimized());
  Printer.printString("flags", N->getFlags());
  Printer.printInt("runtimeVersion", N->getRuntimeVersion(),
                   /* ShouldSkipZero */ false);
  Printer.printString("splitDebugFilename", N->getSplitDebugFilename());
  Printer.printEmissionKind("emissionKind", N->getEmissionKind());
  Printer.printMetadata("enums", N->getRawEnumTypes());
  Printer.printMetadata("retainedTypes", N->getRawRetainedTypes());
  Printer.printMetadata("globals", N->getRawGlobalVariables());
  Printer.printMetadata("imports", N->getRawImportedEntities());
  Printer.printMetadata("macros", N->getRawMacros());
  Printer.printInt("dwoId", N->getDWOId());
  Printer.printBool("splitDebugInlining", N->getSplitDebugInlining(), true);
  Printer.printBool("debugInfoForProfiling", N->getDebugInfoForProfiling(),
                    false);
  Printer.printNameTableKind("nameTableKind", N->getNameTableKind());
  Printer.printBool("rangesBaseAddress", N->getRangesBaseAddress(), false);
  Out << ")";
}

static void writeDISubprogram(raw_ostream &Out, const DISubprogram *N,
                              TypePrinting *TypePrinter, SlotTracker *Machine,
                              const Module *Context) {
  Out << "!DISubprogram(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printString("linkageName", N->getLinkageName());
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("type", N->getRawType());
  Printer.printInt("scopeLine", N->getScopeLine());
  Printer.printMetadata("containingType", N->getRawContainingType());
  if (N->getVirtuality() != dwarf::DW_VIRTUALITY_none ||
      N->getVirtualIndex() != 0)
    Printer.printInt("virtualIndex", N->getVirtualIndex(), false);
  Printer.printInt("thisAdjustment", N->getThisAdjustment());
  Printer.printDIFlags("flags", N->getFlags());
  Printer.printDISPFlags("spFlags", N->getSPFlags());
  Printer.printMetadata("unit", N->getRawUnit());
  Printer.printMetadata("templateParams", N->getRawTemplateParams());
  Printer.printMetadata("declaration", N->getRawDeclaration());
  Printer.printMetadata("retainedNodes", N->getRawRetainedNodes());
  Printer.printMetadata("thrownTypes", N->getRawThrownTypes());
  Out << ")";
}

static void writeDILexicalBlock(raw_ostream &Out, const DILexicalBlock *N,
                                TypePrinting *TypePrinter, SlotTracker *Machine,
                                const Module *Context) {
  Out << "!DILexicalBlock(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printInt("column", N->getColumn());
  Out << ")";
}

static void writeDILexicalBlockFile(raw_ostream &Out,
                                    const DILexicalBlockFile *N,
                                    TypePrinting *TypePrinter,
                                    SlotTracker *Machine,
                                    const Module *Context) {
  Out << "!DILexicalBlockFile(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("discriminator", N->getDiscriminator(),
                   /* ShouldSkipZero */ false);
  Out << ")";
}

static void writeDINamespace(raw_ostream &Out, const DINamespace *N,
                             TypePrinting *TypePrinter, SlotTracker *Machine,
                             const Module *Context) {
  Out << "!DINamespace(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printBool("exportSymbols", N->getExportSymbols(), false);
  Out << ")";
}

static void writeDIMacro(raw_ostream &Out, const DIMacro *N,
                         TypePrinting *TypePrinter, SlotTracker *Machine,
                         const Module *Context) {
  Out << "!DIMacro(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMacinfoType(N);
  Printer.printInt("line", N->getLine());
  Printer.printString("name", N->getName());
  Printer.printString("value", N->getValue());
  Out << ")";
}

static void writeDIMacroFile(raw_ostream &Out, const DIMacroFile *N,
                             TypePrinting *TypePrinter, SlotTracker *Machine,
                             const Module *Context) {
  Out << "!DIMacroFile(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("file", N->getRawFile(), /* ShouldSkipNull */ false);
  Printer.printMetadata("nodes", N->getRawElements());
  Out << ")";
}

static void writeDIModule(raw_ostream &Out, const DIModule *N,
                          TypePrinting *TypePrinter, SlotTracker *Machine,
                          const Module *Context) {
  Out << "!DIModule(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printString("name", N->getName());
  Printer.printString("configMacros", N->getConfigurationMacros());
  Printer.printString("includePath", N->getIncludePath());
  Printer.printString("isysroot", N->getISysRoot());
  Out << ")";
}


static void writeDITemplateTypeParameter(raw_ostream &Out,
                                         const DITemplateTypeParameter *N,
                                         TypePrinting *TypePrinter,
                                         SlotTracker *Machine,
                                         const Module *Context) {
  Out << "!DITemplateTypeParameter(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printMetadata("type", N->getRawType(), /* ShouldSkipNull */ false);
  Out << ")";
}

static void writeDITemplateValueParameter(raw_ostream &Out,
                                          const DITemplateValueParameter *N,
                                          TypePrinting *TypePrinter,
                                          SlotTracker *Machine,
                                          const Module *Context) {
  Out << "!DITemplateValueParameter(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  if (N->getTag() != dwarf::DW_TAG_template_value_parameter)
    Printer.printTag(N);
  Printer.printString("name", N->getName());
  Printer.printMetadata("type", N->getRawType());
  Printer.printMetadata("value", N->getValue(), /* ShouldSkipNull */ false);
  Out << ")";
}

static void writeDIGlobalVariable(raw_ostream &Out, const DIGlobalVariable *N,
                                  TypePrinting *TypePrinter,
                                  SlotTracker *Machine, const Module *Context) {
  Out << "!DIGlobalVariable(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printString("linkageName", N->getLinkageName());
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("type", N->getRawType());
  Printer.printBool("isLocal", N->isLocalToUnit());
  Printer.printBool("isDefinition", N->isDefinition());
  Printer.printMetadata("declaration", N->getRawStaticDataMemberDeclaration());
  Printer.printMetadata("templateParams", N->getRawTemplateParams());
  Printer.printInt("align", N->getAlignInBits());
  Out << ")";
}

static void writeDILocalVariable(raw_ostream &Out, const DILocalVariable *N,
                                 TypePrinting *TypePrinter,
                                 SlotTracker *Machine, const Module *Context) {
  Out << "!DILocalVariable(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printInt("arg", N->getArg());
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printMetadata("type", N->getRawType());
  Printer.printDIFlags("flags", N->getFlags());
  Printer.printInt("align", N->getAlignInBits());
  Out << ")";
}

static void writeDILabel(raw_ostream &Out, const DILabel *N,
                         TypePrinting *TypePrinter,
                         SlotTracker *Machine, const Module *Context) {
  Out << "!DILabel(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printString("name", N->getName());
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Out << ")";
}

static void writeDIExpression(raw_ostream &Out, const DIExpression *N,
                              TypePrinting *TypePrinter, SlotTracker *Machine,
                              const Module *Context) {
  Out << "!DIExpression(";
  FieldSeparator FS;
  if (N->isValid()) {
    for (auto I = N->expr_op_begin(), E = N->expr_op_end(); I != E; ++I) {
      auto OpStr = dwarf::OperationEncodingString(I->getOp());
      assert(!OpStr.empty() && "Expected valid opcode");

      Out << FS << OpStr;
      for (unsigned A = 0, AE = I->getNumArgs(); A != AE; ++A)
        Out << FS << I->getArg(A);
    }
  } else {
    for (const auto &I : N->getElements())
      Out << FS << I;
  }
  Out << ")";
}

static void writeDIGlobalVariableExpression(raw_ostream &Out,
                                            const DIGlobalVariableExpression *N,
                                            TypePrinting *TypePrinter,
                                            SlotTracker *Machine,
                                            const Module *Context) {
  Out << "!DIGlobalVariableExpression(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printMetadata("var", N->getVariable());
  Printer.printMetadata("expr", N->getExpression());
  Out << ")";
}

static void writeDIObjCProperty(raw_ostream &Out, const DIObjCProperty *N,
                                TypePrinting *TypePrinter, SlotTracker *Machine,
                                const Module *Context) {
  Out << "!DIObjCProperty(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printString("name", N->getName());
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Printer.printString("setter", N->getSetterName());
  Printer.printString("getter", N->getGetterName());
  Printer.printInt("attributes", N->getAttributes());
  Printer.printMetadata("type", N->getRawType());
  Out << ")";
}

static void writeDIImportedEntity(raw_ostream &Out, const DIImportedEntity *N,
                                  TypePrinting *TypePrinter,
                                  SlotTracker *Machine, const Module *Context) {
  Out << "!DIImportedEntity(";
  MDFieldPrinter Printer(Out, TypePrinter, Machine, Context);
  Printer.printTag(N);
  Printer.printString("name", N->getName());
  Printer.printMetadata("scope", N->getRawScope(), /* ShouldSkipNull */ false);
  Printer.printMetadata("entity", N->getRawEntity());
  Printer.printMetadata("file", N->getRawFile());
  Printer.printInt("line", N->getLine());
  Out << ")";
}

static void WriteMDNodeBodyInternal(raw_ostream &Out, const MDNode *Node,
                                    TypePrinting *TypePrinter,
                                    SlotTracker *Machine,
                                    const Module *Context) {
  if (Node->isDistinct())
    Out << "distinct ";
  else if (Node->isTemporary())
    Out << "<temporary!> "; // Handle broken code.

  switch (Node->getMetadataID()) {
  default:
    llvm_unreachable("Expected uniquable MDNode");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case Metadata::CLASS##Kind:                                                  \
    write##CLASS(Out, cast<CLASS>(Node), TypePrinter, Machine, Context);       \
    break;
#include "llvm/IR/Metadata.def"
  }
}

// Full implementation of printing a Value as an operand with support for
// TypePrinting, etc.
static void WriteAsOperandInternal(raw_ostream &Out, const Value *V,
                                   TypePrinting *TypePrinter,
                                   SlotTracker *Machine,
                                   const Module *Context) {
  if (V->hasName()) {
    PrintLLVMName(Out, V);
    return;
  }

  const Constant *CV = dyn_cast<Constant>(V);
  if (CV && !isa<GlobalValue>(CV)) {
    assert(TypePrinter && "Constants require TypePrinting!");
    WriteConstantInternal(Out, CV, *TypePrinter, Machine, Context);
    return;
  }

  if (const InlineAsm *IA = dyn_cast<InlineAsm>(V)) {
    Out << "asm ";
    if (IA->hasSideEffects())
      Out << "sideeffect ";
    if (IA->isAlignStack())
      Out << "alignstack ";
    // We don't emit the AD_ATT dialect as it's the assumed default.
    if (IA->getDialect() == InlineAsm::AD_Intel)
      Out << "inteldialect ";
    Out << '"';
    printEscapedString(IA->getAsmString(), Out);
    Out << "\", \"";
    printEscapedString(IA->getConstraintString(), Out);
    Out << '"';
    return;
  }

  if (auto *MD = dyn_cast<MetadataAsValue>(V)) {
    WriteAsOperandInternal(Out, MD->getMetadata(), TypePrinter, Machine,
                           Context, /* FromValue */ true);
    return;
  }

  char Prefix = '%';
  int Slot;
  // If we have a SlotTracker, use it.
  if (Machine) {
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      Slot = Machine->getGlobalSlot(GV);
      Prefix = '@';
    } else {
      Slot = Machine->getLocalSlot(V);

      // If the local value didn't succeed, then we may be referring to a value
      // from a different function.  Translate it, as this can happen when using
      // address of blocks.
      if (Slot == -1)
        if ((Machine = createSlotTracker(V))) {
          Slot = Machine->getLocalSlot(V);
          delete Machine;
        }
    }
  } else if ((Machine = createSlotTracker(V))) {
    // Otherwise, create one to get the # and then destroy it.
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      Slot = Machine->getGlobalSlot(GV);
      Prefix = '@';
    } else {
      Slot = Machine->getLocalSlot(V);
    }
    delete Machine;
    Machine = nullptr;
  } else {
    Slot = -1;
  }

  if (Slot != -1)
    Out << Prefix << Slot;
  else
    Out << "<badref>";
}

static void WriteAsOperandInternal(raw_ostream &Out, const Metadata *MD,
                                   TypePrinting *TypePrinter,
                                   SlotTracker *Machine, const Module *Context,
                                   bool FromValue) {
  // Write DIExpressions inline when used as a value. Improves readability of
  // debug info intrinsics.
  if (const DIExpression *Expr = dyn_cast<DIExpression>(MD)) {
    writeDIExpression(Out, Expr, TypePrinter, Machine, Context);
    return;
  }

  if (const MDNode *N = dyn_cast<MDNode>(MD)) {
    std::unique_ptr<SlotTracker> MachineStorage;
    if (!Machine) {
      MachineStorage = make_unique<SlotTracker>(Context);
      Machine = MachineStorage.get();
    }
    int Slot = Machine->getMetadataSlot(N);
    if (Slot == -1) {
      if (const DILocation *Loc = dyn_cast<DILocation>(N)) {
        writeDILocation(Out, Loc, TypePrinter, Machine, Context);
        return;
      }
      // Give the pointer value instead of "badref", since this comes up all
      // the time when debugging.
      Out << "<" << N << ">";
    } else
      Out << '!' << Slot;
    return;
  }

  if (const MDString *MDS = dyn_cast<MDString>(MD)) {
    Out << "!\"";
    printEscapedString(MDS->getString(), Out);
    Out << '"';
    return;
  }

  auto *V = cast<ValueAsMetadata>(MD);
  assert(TypePrinter && "TypePrinter required for metadata values");
  assert((FromValue || !isa<LocalAsMetadata>(V)) &&
         "Unexpected function-local metadata outside of value argument");

  TypePrinter->print(V->getValue()->getType(), Out);
  Out << ' ';
  WriteAsOperandInternal(Out, V->getValue(), TypePrinter, Machine, Context);
}

namespace {

class AssemblyWriter {
  formatted_raw_ostream &Out;
  const Module *TheModule = nullptr;
  const ModuleSummaryIndex *TheIndex = nullptr;
  std::unique_ptr<SlotTracker> SlotTrackerStorage;
  SlotTracker &Machine;
  TypePrinting TypePrinter;
  AssemblyAnnotationWriter *AnnotationWriter = nullptr;
  SetVector<const Comdat *> Comdats;
  bool IsForDebug;
  bool ShouldPreserveUseListOrder;
  UseListOrderStack UseListOrders;
  SmallVector<StringRef, 8> MDNames;
  /// Synchronization scope names registered with LLVMContext.
  SmallVector<StringRef, 8> SSNs;
  DenseMap<const GlobalValueSummary *, GlobalValue::GUID> SummaryToGUIDMap;

public:
  /// Construct an AssemblyWriter with an external SlotTracker
  AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac, const Module *M,
                 AssemblyAnnotationWriter *AAW, bool IsForDebug,
                 bool ShouldPreserveUseListOrder = false);

  AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac,
                 const ModuleSummaryIndex *Index, bool IsForDebug);

  void printMDNodeBody(const MDNode *MD);
  void printNamedMDNode(const NamedMDNode *NMD);

  void printModule(const Module *M);

  void writeOperand(const Value *Op, bool PrintType);
  void writeParamOperand(const Value *Operand, AttributeSet Attrs);
  void writeOperandBundles(const CallBase *Call);
  void writeSyncScope(const LLVMContext &Context,
                      SyncScope::ID SSID);
  void writeAtomic(const LLVMContext &Context,
                   AtomicOrdering Ordering,
                   SyncScope::ID SSID);
  void writeAtomicCmpXchg(const LLVMContext &Context,
                          AtomicOrdering SuccessOrdering,
                          AtomicOrdering FailureOrdering,
                          SyncScope::ID SSID);

  void writeAllMDNodes();
  void writeMDNode(unsigned Slot, const MDNode *Node);
  void writeAllAttributeGroups();

  void printTypeIdentities();
  void printGlobal(const GlobalVariable *GV);
  void printIndirectSymbol(const GlobalIndirectSymbol *GIS);
  void printComdat(const Comdat *C);
  void printFunction(const Function *F);
  void printArgument(const Argument *FA, AttributeSet Attrs);
  void printBasicBlock(const BasicBlock *BB);
  void printInstructionLine(const Instruction &I);
  void printInstruction(const Instruction &I);

  void printUseListOrder(const UseListOrder &Order);
  void printUseLists(const Function *F);

  void printModuleSummaryIndex();
  void printSummaryInfo(unsigned Slot, const ValueInfo &VI);
  void printSummary(const GlobalValueSummary &Summary);
  void printAliasSummary(const AliasSummary *AS);
  void printGlobalVarSummary(const GlobalVarSummary *GS);
  void printFunctionSummary(const FunctionSummary *FS);
  void printTypeIdSummary(const TypeIdSummary &TIS);
  void printTypeTestResolution(const TypeTestResolution &TTRes);
  void printArgs(const std::vector<uint64_t> &Args);
  void printWPDRes(const WholeProgramDevirtResolution &WPDRes);
  void printTypeIdInfo(const FunctionSummary::TypeIdInfo &TIDInfo);
  void printVFuncId(const FunctionSummary::VFuncId VFId);
  void
  printNonConstVCalls(const std::vector<FunctionSummary::VFuncId> VCallList,
                      const char *Tag);
  void
  printConstVCalls(const std::vector<FunctionSummary::ConstVCall> VCallList,
                   const char *Tag);

private:
  /// Print out metadata attachments.
  void printMetadataAttachments(
      const SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs,
      StringRef Separator);

  // printInfoComment - Print a little comment after the instruction indicating
  // which slot it occupies.
  void printInfoComment(const Value &V);

  // printGCRelocateComment - print comment after call to the gc.relocate
  // intrinsic indicating base and derived pointer names.
  void printGCRelocateComment(const GCRelocateInst &Relocate);
};

} // end anonymous namespace

AssemblyWriter::AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac,
                               const Module *M, AssemblyAnnotationWriter *AAW,
                               bool IsForDebug, bool ShouldPreserveUseListOrder)
    : Out(o), TheModule(M), Machine(Mac), TypePrinter(M), AnnotationWriter(AAW),
      IsForDebug(IsForDebug),
      ShouldPreserveUseListOrder(ShouldPreserveUseListOrder) {
  if (!TheModule)
    return;
  for (const GlobalObject &GO : TheModule->global_objects())
    if (const Comdat *C = GO.getComdat())
      Comdats.insert(C);
}

AssemblyWriter::AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac,
                               const ModuleSummaryIndex *Index, bool IsForDebug)
    : Out(o), TheIndex(Index), Machine(Mac), TypePrinter(/*Module=*/nullptr),
      IsForDebug(IsForDebug), ShouldPreserveUseListOrder(false) {}

void AssemblyWriter::writeOperand(const Value *Operand, bool PrintType) {
  if (!Operand) {
    Out << "<null operand!>";
    return;
  }
  if (PrintType) {
    TypePrinter.print(Operand->getType(), Out);
    Out << ' ';
  }
  WriteAsOperandInternal(Out, Operand, &TypePrinter, &Machine, TheModule);
}

void AssemblyWriter::writeSyncScope(const LLVMContext &Context,
                                    SyncScope::ID SSID) {
  switch (SSID) {
  case SyncScope::System: {
    break;
  }
  default: {
    if (SSNs.empty())
      Context.getSyncScopeNames(SSNs);

    Out << " syncscope(\"";
    printEscapedString(SSNs[SSID], Out);
    Out << "\")";
    break;
  }
  }
}

void AssemblyWriter::writeAtomic(const LLVMContext &Context,
                                 AtomicOrdering Ordering,
                                 SyncScope::ID SSID) {
  if (Ordering == AtomicOrdering::NotAtomic)
    return;

  writeSyncScope(Context, SSID);
  Out << " " << toIRString(Ordering);
}

void AssemblyWriter::writeAtomicCmpXchg(const LLVMContext &Context,
                                        AtomicOrdering SuccessOrdering,
                                        AtomicOrdering FailureOrdering,
                                        SyncScope::ID SSID) {
  assert(SuccessOrdering != AtomicOrdering::NotAtomic &&
         FailureOrdering != AtomicOrdering::NotAtomic);

  writeSyncScope(Context, SSID);
  Out << " " << toIRString(SuccessOrdering);
  Out << " " << toIRString(FailureOrdering);
}

void AssemblyWriter::writeParamOperand(const Value *Operand,
                                       AttributeSet Attrs) {
  if (!Operand) {
    Out << "<null operand!>";
    return;
  }

  // Print the type
  TypePrinter.print(Operand->getType(), Out);
  // Print parameter attributes list
  if (Attrs.hasAttributes())
    Out << ' ' << Attrs.getAsString();
  Out << ' ';
  // Print the operand
  WriteAsOperandInternal(Out, Operand, &TypePrinter, &Machine, TheModule);
}

void AssemblyWriter::writeOperandBundles(const CallBase *Call) {
  if (!Call->hasOperandBundles())
    return;

  Out << " [ ";

  bool FirstBundle = true;
  for (unsigned i = 0, e = Call->getNumOperandBundles(); i != e; ++i) {
    OperandBundleUse BU = Call->getOperandBundleAt(i);

    if (!FirstBundle)
      Out << ", ";
    FirstBundle = false;

    Out << '"';
    printEscapedString(BU.getTagName(), Out);
    Out << '"';

    Out << '(';

    bool FirstInput = true;
    for (const auto &Input : BU.Inputs) {
      if (!FirstInput)
        Out << ", ";
      FirstInput = false;

      TypePrinter.print(Input->getType(), Out);
      Out << " ";
      WriteAsOperandInternal(Out, Input, &TypePrinter, &Machine, TheModule);
    }

    Out << ')';
  }

  Out << " ]";
}

void AssemblyWriter::printModule(const Module *M) {
  Machine.initializeIfNeeded();

  if (ShouldPreserveUseListOrder)
    UseListOrders = predictUseListOrder(M);

  if (!M->getModuleIdentifier().empty() &&
      // Don't print the ID if it will start a new line (which would
      // require a comment char before it).
      M->getModuleIdentifier().find('\n') == std::string::npos)
    Out << "; ModuleID = '" << M->getModuleIdentifier() << "'\n";

  if (!M->getSourceFileName().empty()) {
    Out << "source_filename = \"";
    printEscapedString(M->getSourceFileName(), Out);
    Out << "\"\n";
  }

  const std::string &DL = M->getDataLayoutStr();
  if (!DL.empty())
    Out << "target datalayout = \"" << DL << "\"\n";
  if (!M->getTargetTriple().empty())
    Out << "target triple = \"" << M->getTargetTriple() << "\"\n";

  if (!M->getModuleInlineAsm().empty()) {
    Out << '\n';

    // Split the string into lines, to make it easier to read the .ll file.
    StringRef Asm = M->getModuleInlineAsm();
    do {
      StringRef Front;
      std::tie(Front, Asm) = Asm.split('\n');

      // We found a newline, print the portion of the asm string from the
      // last newline up to this newline.
      Out << "module asm \"";
      printEscapedString(Front, Out);
      Out << "\"\n";
    } while (!Asm.empty());
  }

  printTypeIdentities();

  // Output all comdats.
  if (!Comdats.empty())
    Out << '\n';
  for (const Comdat *C : Comdats) {
    printComdat(C);
    if (C != Comdats.back())
      Out << '\n';
  }

  // Output all globals.
  if (!M->global_empty()) Out << '\n';
  for (const GlobalVariable &GV : M->globals()) {
    printGlobal(&GV); Out << '\n';
  }

  // Output all aliases.
  if (!M->alias_empty()) Out << "\n";
  for (const GlobalAlias &GA : M->aliases())
    printIndirectSymbol(&GA);

  // Output all ifuncs.
  if (!M->ifunc_empty()) Out << "\n";
  for (const GlobalIFunc &GI : M->ifuncs())
    printIndirectSymbol(&GI);

  // Output global use-lists.
  printUseLists(nullptr);

  // Output all of the functions.
  for (const Function &F : *M)
    printFunction(&F);
  assert(UseListOrders.empty() && "All use-lists should have been consumed");

  // Output all attribute groups.
  if (!Machine.as_empty()) {
    Out << '\n';
    writeAllAttributeGroups();
  }

  // Output named metadata.
  if (!M->named_metadata_empty()) Out << '\n';

  for (const NamedMDNode &Node : M->named_metadata())
    printNamedMDNode(&Node);

  // Output metadata.
  if (!Machine.mdn_empty()) {
    Out << '\n';
    writeAllMDNodes();
  }
}

void AssemblyWriter::printModuleSummaryIndex() {
  assert(TheIndex);
  Machine.initializeIndexIfNeeded();

  Out << "\n";

  // Print module path entries. To print in order, add paths to a vector
  // indexed by module slot.
  std::vector<std::pair<std::string, ModuleHash>> moduleVec;
  std::string RegularLTOModuleName = "[Regular LTO]";
  moduleVec.resize(TheIndex->modulePaths().size());
  for (auto &ModPath : TheIndex->modulePaths())
    moduleVec[Machine.getModulePathSlot(ModPath.first())] = std::make_pair(
        // A module id of -1 is a special entry for a regular LTO module created
        // during the thin link.
        ModPath.second.first == -1u ? RegularLTOModuleName
                                    : (std::string)ModPath.first(),
        ModPath.second.second);

  unsigned i = 0;
  for (auto &ModPair : moduleVec) {
    Out << "^" << i++ << " = module: (";
    Out << "path: \"";
    printEscapedString(ModPair.first, Out);
    Out << "\", hash: (";
    FieldSeparator FS;
    for (auto Hash : ModPair.second)
      Out << FS << Hash;
    Out << "))\n";
  }

  // FIXME: Change AliasSummary to hold a ValueInfo instead of summary pointer
  // for aliasee (then update BitcodeWriter.cpp and remove get/setAliaseeGUID).
  for (auto &GlobalList : *TheIndex) {
    auto GUID = GlobalList.first;
    for (auto &Summary : GlobalList.second.SummaryList)
      SummaryToGUIDMap[Summary.get()] = GUID;
  }

  // Print the global value summary entries.
  for (auto &GlobalList : *TheIndex) {
    auto GUID = GlobalList.first;
    auto VI = TheIndex->getValueInfo(GlobalList);
    printSummaryInfo(Machine.getGUIDSlot(GUID), VI);
  }

  // Print the TypeIdMap entries.
  for (auto TidIter = TheIndex->typeIds().begin();
       TidIter != TheIndex->typeIds().end(); TidIter++) {
    Out << "^" << Machine.getTypeIdSlot(TidIter->second.first)
        << " = typeid: (name: \"" << TidIter->second.first << "\"";
    printTypeIdSummary(TidIter->second.second);
    Out << ") ; guid = " << TidIter->first << "\n";
  }
}

static const char *
getWholeProgDevirtResKindName(WholeProgramDevirtResolution::Kind K) {
  switch (K) {
  case WholeProgramDevirtResolution::Indir:
    return "indir";
  case WholeProgramDevirtResolution::SingleImpl:
    return "singleImpl";
  case WholeProgramDevirtResolution::BranchFunnel:
    return "branchFunnel";
  }
  llvm_unreachable("invalid WholeProgramDevirtResolution kind");
}

static const char *getWholeProgDevirtResByArgKindName(
    WholeProgramDevirtResolution::ByArg::Kind K) {
  switch (K) {
  case WholeProgramDevirtResolution::ByArg::Indir:
    return "indir";
  case WholeProgramDevirtResolution::ByArg::UniformRetVal:
    return "uniformRetVal";
  case WholeProgramDevirtResolution::ByArg::UniqueRetVal:
    return "uniqueRetVal";
  case WholeProgramDevirtResolution::ByArg::VirtualConstProp:
    return "virtualConstProp";
  }
  llvm_unreachable("invalid WholeProgramDevirtResolution::ByArg kind");
}

static const char *getTTResKindName(TypeTestResolution::Kind K) {
  switch (K) {
  case TypeTestResolution::Unsat:
    return "unsat";
  case TypeTestResolution::ByteArray:
    return "byteArray";
  case TypeTestResolution::Inline:
    return "inline";
  case TypeTestResolution::Single:
    return "single";
  case TypeTestResolution::AllOnes:
    return "allOnes";
  }
  llvm_unreachable("invalid TypeTestResolution kind");
}

void AssemblyWriter::printTypeTestResolution(const TypeTestResolution &TTRes) {
  Out << "typeTestRes: (kind: " << getTTResKindName(TTRes.TheKind)
      << ", sizeM1BitWidth: " << TTRes.SizeM1BitWidth;

  // The following fields are only used if the target does not support the use
  // of absolute symbols to store constants. Print only if non-zero.
  if (TTRes.AlignLog2)
    Out << ", alignLog2: " << TTRes.AlignLog2;
  if (TTRes.SizeM1)
    Out << ", sizeM1: " << TTRes.SizeM1;
  if (TTRes.BitMask)
    // BitMask is uint8_t which causes it to print the corresponding char.
    Out << ", bitMask: " << (unsigned)TTRes.BitMask;
  if (TTRes.InlineBits)
    Out << ", inlineBits: " << TTRes.InlineBits;

  Out << ")";
}

void AssemblyWriter::printTypeIdSummary(const TypeIdSummary &TIS) {
  Out << ", summary: (";
  printTypeTestResolution(TIS.TTRes);
  if (!TIS.WPDRes.empty()) {
    Out << ", wpdResolutions: (";
    FieldSeparator FS;
    for (auto &WPDRes : TIS.WPDRes) {
      Out << FS;
      Out << "(offset: " << WPDRes.first << ", ";
      printWPDRes(WPDRes.second);
      Out << ")";
    }
    Out << ")";
  }
  Out << ")";
}

void AssemblyWriter::printArgs(const std::vector<uint64_t> &Args) {
  Out << "args: (";
  FieldSeparator FS;
  for (auto arg : Args) {
    Out << FS;
    Out << arg;
  }
  Out << ")";
}

void AssemblyWriter::printWPDRes(const WholeProgramDevirtResolution &WPDRes) {
  Out << "wpdRes: (kind: ";
  Out << getWholeProgDevirtResKindName(WPDRes.TheKind);

  if (WPDRes.TheKind == WholeProgramDevirtResolution::SingleImpl)
    Out << ", singleImplName: \"" << WPDRes.SingleImplName << "\"";

  if (!WPDRes.ResByArg.empty()) {
    Out << ", resByArg: (";
    FieldSeparator FS;
    for (auto &ResByArg : WPDRes.ResByArg) {
      Out << FS;
      printArgs(ResByArg.first);
      Out << ", byArg: (kind: ";
      Out << getWholeProgDevirtResByArgKindName(ResByArg.second.TheKind);
      if (ResByArg.second.TheKind ==
              WholeProgramDevirtResolution::ByArg::UniformRetVal ||
          ResByArg.second.TheKind ==
              WholeProgramDevirtResolution::ByArg::UniqueRetVal)
        Out << ", info: " << ResByArg.second.Info;

      // The following fields are only used if the target does not support the
      // use of absolute symbols to store constants. Print only if non-zero.
      if (ResByArg.second.Byte || ResByArg.second.Bit)
        Out << ", byte: " << ResByArg.second.Byte
            << ", bit: " << ResByArg.second.Bit;

      Out << ")";
    }
    Out << ")";
  }
  Out << ")";
}

static const char *getSummaryKindName(GlobalValueSummary::SummaryKind SK) {
  switch (SK) {
  case GlobalValueSummary::AliasKind:
    return "alias";
  case GlobalValueSummary::FunctionKind:
    return "function";
  case GlobalValueSummary::GlobalVarKind:
    return "variable";
  }
  llvm_unreachable("invalid summary kind");
}

void AssemblyWriter::printAliasSummary(const AliasSummary *AS) {
  Out << ", aliasee: ";
  // The indexes emitted for distributed backends may not include the
  // aliasee summary (only if it is being imported directly). Handle
  // that case by just emitting "null" as the aliasee.
  if (AS->hasAliasee())
    Out << "^" << Machine.getGUIDSlot(SummaryToGUIDMap[&AS->getAliasee()]);
  else
    Out << "null";
}

void AssemblyWriter::printGlobalVarSummary(const GlobalVarSummary *GS) {
  Out << ", varFlags: (readonly: " << GS->VarFlags.ReadOnly << ")";
}

static std::string getLinkageName(GlobalValue::LinkageTypes LT) {
  switch (LT) {
  case GlobalValue::ExternalLinkage:
    return "external";
  case GlobalValue::PrivateLinkage:
    return "private";
  case GlobalValue::InternalLinkage:
    return "internal";
  case GlobalValue::LinkOnceAnyLinkage:
    return "linkonce";
  case GlobalValue::LinkOnceODRLinkage:
    return "linkonce_odr";
  case GlobalValue::WeakAnyLinkage:
    return "weak";
  case GlobalValue::WeakODRLinkage:
    return "weak_odr";
  case GlobalValue::CommonLinkage:
    return "common";
  case GlobalValue::AppendingLinkage:
    return "appending";
  case GlobalValue::ExternalWeakLinkage:
    return "extern_weak";
  case GlobalValue::AvailableExternallyLinkage:
    return "available_externally";
  }
  llvm_unreachable("invalid linkage");
}

// When printing the linkage types in IR where the ExternalLinkage is
// not printed, and other linkage types are expected to be printed with
// a space after the name.
static std::string getLinkageNameWithSpace(GlobalValue::LinkageTypes LT) {
  if (LT == GlobalValue::ExternalLinkage)
    return "";
  return getLinkageName(LT) + " ";
}

void AssemblyWriter::printFunctionSummary(const FunctionSummary *FS) {
  Out << ", insts: " << FS->instCount();

  FunctionSummary::FFlags FFlags = FS->fflags();
  if (FFlags.ReadNone | FFlags.ReadOnly | FFlags.NoRecurse |
      FFlags.ReturnDoesNotAlias) {
    Out << ", funcFlags: (";
    Out << "readNone: " << FFlags.ReadNone;
    Out << ", readOnly: " << FFlags.ReadOnly;
    Out << ", noRecurse: " << FFlags.NoRecurse;
    Out << ", returnDoesNotAlias: " << FFlags.ReturnDoesNotAlias;
    Out << ", noInline: " << FFlags.NoInline;
    Out << ")";
  }
  if (!FS->calls().empty()) {
    Out << ", calls: (";
    FieldSeparator IFS;
    for (auto &Call : FS->calls()) {
      Out << IFS;
      Out << "(callee: ^" << Machine.getGUIDSlot(Call.first.getGUID());
      if (Call.second.getHotness() != CalleeInfo::HotnessType::Unknown)
        Out << ", hotness: " << getHotnessName(Call.second.getHotness());
      else if (Call.second.RelBlockFreq)
        Out << ", relbf: " << Call.second.RelBlockFreq;
      Out << ")";
    }
    Out << ")";
  }

  if (const auto *TIdInfo = FS->getTypeIdInfo())
    printTypeIdInfo(*TIdInfo);
}

void AssemblyWriter::printTypeIdInfo(
    const FunctionSummary::TypeIdInfo &TIDInfo) {
  Out << ", typeIdInfo: (";
  FieldSeparator TIDFS;
  if (!TIDInfo.TypeTests.empty()) {
    Out << TIDFS;
    Out << "typeTests: (";
    FieldSeparator FS;
    for (auto &GUID : TIDInfo.TypeTests) {
      auto TidIter = TheIndex->typeIds().equal_range(GUID);
      if (TidIter.first == TidIter.second) {
        Out << FS;
        Out << GUID;
        continue;
      }
      // Print all type id that correspond to this GUID.
      for (auto It = TidIter.first; It != TidIter.second; ++It) {
        Out << FS;
        auto Slot = Machine.getTypeIdSlot(It->second.first);
        assert(Slot != -1);
        Out << "^" << Slot;
      }
    }
    Out << ")";
  }
  if (!TIDInfo.TypeTestAssumeVCalls.empty()) {
    Out << TIDFS;
    printNonConstVCalls(TIDInfo.TypeTestAssumeVCalls, "typeTestAssumeVCalls");
  }
  if (!TIDInfo.TypeCheckedLoadVCalls.empty()) {
    Out << TIDFS;
    printNonConstVCalls(TIDInfo.TypeCheckedLoadVCalls, "typeCheckedLoadVCalls");
  }
  if (!TIDInfo.TypeTestAssumeConstVCalls.empty()) {
    Out << TIDFS;
    printConstVCalls(TIDInfo.TypeTestAssumeConstVCalls,
                     "typeTestAssumeConstVCalls");
  }
  if (!TIDInfo.TypeCheckedLoadConstVCalls.empty()) {
    Out << TIDFS;
    printConstVCalls(TIDInfo.TypeCheckedLoadConstVCalls,
                     "typeCheckedLoadConstVCalls");
  }
  Out << ")";
}

void AssemblyWriter::printVFuncId(const FunctionSummary::VFuncId VFId) {
  auto TidIter = TheIndex->typeIds().equal_range(VFId.GUID);
  if (TidIter.first == TidIter.second) {
    Out << "vFuncId: (";
    Out << "guid: " << VFId.GUID;
    Out << ", offset: " << VFId.Offset;
    Out << ")";
    return;
  }
  // Print all type id that correspond to this GUID.
  FieldSeparator FS;
  for (auto It = TidIter.first; It != TidIter.second; ++It) {
    Out << FS;
    Out << "vFuncId: (";
    auto Slot = Machine.getTypeIdSlot(It->second.first);
    assert(Slot != -1);
    Out << "^" << Slot;
    Out << ", offset: " << VFId.Offset;
    Out << ")";
  }
}

void AssemblyWriter::printNonConstVCalls(
    const std::vector<FunctionSummary::VFuncId> VCallList, const char *Tag) {
  Out << Tag << ": (";
  FieldSeparator FS;
  for (auto &VFuncId : VCallList) {
    Out << FS;
    printVFuncId(VFuncId);
  }
  Out << ")";
}

void AssemblyWriter::printConstVCalls(
    const std::vector<FunctionSummary::ConstVCall> VCallList, const char *Tag) {
  Out << Tag << ": (";
  FieldSeparator FS;
  for (auto &ConstVCall : VCallList) {
    Out << FS;
    Out << "(";
    printVFuncId(ConstVCall.VFunc);
    if (!ConstVCall.Args.empty()) {
      Out << ", ";
      printArgs(ConstVCall.Args);
    }
    Out << ")";
  }
  Out << ")";
}

void AssemblyWriter::printSummary(const GlobalValueSummary &Summary) {
  GlobalValueSummary::GVFlags GVFlags = Summary.flags();
  GlobalValue::LinkageTypes LT = (GlobalValue::LinkageTypes)GVFlags.Linkage;
  Out << getSummaryKindName(Summary.getSummaryKind()) << ": ";
  Out << "(module: ^" << Machine.getModulePathSlot(Summary.modulePath())
      << ", flags: (";
  Out << "linkage: " << getLinkageName(LT);
  Out << ", notEligibleToImport: " << GVFlags.NotEligibleToImport;
  Out << ", live: " << GVFlags.Live;
  Out << ", dsoLocal: " << GVFlags.DSOLocal;
  Out << ")";

  if (Summary.getSummaryKind() == GlobalValueSummary::AliasKind)
    printAliasSummary(cast<AliasSummary>(&Summary));
  else if (Summary.getSummaryKind() == GlobalValueSummary::FunctionKind)
    printFunctionSummary(cast<FunctionSummary>(&Summary));
  else
    printGlobalVarSummary(cast<GlobalVarSummary>(&Summary));

  auto RefList = Summary.refs();
  if (!RefList.empty()) {
    Out << ", refs: (";
    FieldSeparator FS;
    for (auto &Ref : RefList) {
      Out << FS;
      if (Ref.isReadOnly())
        Out << "readonly ";
      Out << "^" << Machine.getGUIDSlot(Ref.getGUID());
    }
    Out << ")";
  }

  Out << ")";
}

void AssemblyWriter::printSummaryInfo(unsigned Slot, const ValueInfo &VI) {
  Out << "^" << Slot << " = gv: (";
  if (!VI.name().empty())
    Out << "name: \"" << VI.name() << "\"";
  else
    Out << "guid: " << VI.getGUID();
  if (!VI.getSummaryList().empty()) {
    Out << ", summaries: (";
    FieldSeparator FS;
    for (auto &Summary : VI.getSummaryList()) {
      Out << FS;
      printSummary(*Summary);
    }
    Out << ")";
  }
  Out << ")";
  if (!VI.name().empty())
    Out << " ; guid = " << VI.getGUID();
  Out << "\n";
}

static void printMetadataIdentifier(StringRef Name,
                                    formatted_raw_ostream &Out) {
  if (Name.empty()) {
    Out << "<empty name> ";
  } else {
    if (isalpha(static_cast<unsigned char>(Name[0])) || Name[0] == '-' ||
        Name[0] == '$' || Name[0] == '.' || Name[0] == '_')
      Out << Name[0];
    else
      Out << '\\' << hexdigit(Name[0] >> 4) << hexdigit(Name[0] & 0x0F);
    for (unsigned i = 1, e = Name.size(); i != e; ++i) {
      unsigned char C = Name[i];
      if (isalnum(static_cast<unsigned char>(C)) || C == '-' || C == '$' ||
          C == '.' || C == '_')
        Out << C;
      else
        Out << '\\' << hexdigit(C >> 4) << hexdigit(C & 0x0F);
    }
  }
}

void AssemblyWriter::printNamedMDNode(const NamedMDNode *NMD) {
  Out << '!';
  printMetadataIdentifier(NMD->getName(), Out);
  Out << " = !{";
  for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
    if (i)
      Out << ", ";

    // Write DIExpressions inline.
    // FIXME: Ban DIExpressions in NamedMDNodes, they will serve no purpose.
    MDNode *Op = NMD->getOperand(i);
    if (auto *Expr = dyn_cast<DIExpression>(Op)) {
      writeDIExpression(Out, Expr, nullptr, nullptr, nullptr);
      continue;
    }

    int Slot = Machine.getMetadataSlot(Op);
    if (Slot == -1)
      Out << "<badref>";
    else
      Out << '!' << Slot;
  }
  Out << "}\n";
}

static void PrintVisibility(GlobalValue::VisibilityTypes Vis,
                            formatted_raw_ostream &Out) {
  switch (Vis) {
  case GlobalValue::DefaultVisibility: break;
  case GlobalValue::HiddenVisibility:    Out << "hidden "; break;
  case GlobalValue::ProtectedVisibility: Out << "protected "; break;
  }
}

static void PrintDSOLocation(const GlobalValue &GV,
                             formatted_raw_ostream &Out) {
  // GVs with local linkage or non default visibility are implicitly dso_local,
  // so we don't print it.
  bool Implicit = GV.hasLocalLinkage() ||
                  (!GV.hasExternalWeakLinkage() && !GV.hasDefaultVisibility());
  if (GV.isDSOLocal() && !Implicit)
    Out << "dso_local ";
}

static void PrintDLLStorageClass(GlobalValue::DLLStorageClassTypes SCT,
                                 formatted_raw_ostream &Out) {
  switch (SCT) {
  case GlobalValue::DefaultStorageClass: break;
  case GlobalValue::DLLImportStorageClass: Out << "dllimport "; break;
  case GlobalValue::DLLExportStorageClass: Out << "dllexport "; break;
  }
}

static void PrintThreadLocalModel(GlobalVariable::ThreadLocalMode TLM,
                                  formatted_raw_ostream &Out) {
  switch (TLM) {
    case GlobalVariable::NotThreadLocal:
      break;
    case GlobalVariable::GeneralDynamicTLSModel:
      Out << "thread_local ";
      break;
    case GlobalVariable::LocalDynamicTLSModel:
      Out << "thread_local(localdynamic) ";
      break;
    case GlobalVariable::InitialExecTLSModel:
      Out << "thread_local(initialexec) ";
      break;
    case GlobalVariable::LocalExecTLSModel:
      Out << "thread_local(localexec) ";
      break;
  }
}

static StringRef getUnnamedAddrEncoding(GlobalVariable::UnnamedAddr UA) {
  switch (UA) {
  case GlobalVariable::UnnamedAddr::None:
    return "";
  case GlobalVariable::UnnamedAddr::Local:
    return "local_unnamed_addr";
  case GlobalVariable::UnnamedAddr::Global:
    return "unnamed_addr";
  }
  llvm_unreachable("Unknown UnnamedAddr");
}

static void maybePrintComdat(formatted_raw_ostream &Out,
                             const GlobalObject &GO) {
  const Comdat *C = GO.getComdat();
  if (!C)
    return;

  if (isa<GlobalVariable>(GO))
    Out << ',';
  Out << " comdat";

  if (GO.getName() == C->getName())
    return;

  Out << '(';
  PrintLLVMName(Out, C->getName(), ComdatPrefix);
  Out << ')';
}

void AssemblyWriter::printGlobal(const GlobalVariable *GV) {
  if (GV->isMaterializable())
    Out << "; Materializable\n";

  WriteAsOperandInternal(Out, GV, &TypePrinter, &Machine, GV->getParent());
  Out << " = ";

  if (!GV->hasInitializer() && GV->hasExternalLinkage())
    Out << "external ";

  Out << getLinkageNameWithSpace(GV->getLinkage());
  PrintDSOLocation(*GV, Out);
  PrintVisibility(GV->getVisibility(), Out);
  PrintDLLStorageClass(GV->getDLLStorageClass(), Out);
  PrintThreadLocalModel(GV->getThreadLocalMode(), Out);
  StringRef UA = getUnnamedAddrEncoding(GV->getUnnamedAddr());
  if (!UA.empty())
      Out << UA << ' ';

  if (unsigned AddressSpace = GV->getType()->getAddressSpace())
    Out << "addrspace(" << AddressSpace << ") ";
  if (GV->isExternallyInitialized()) Out << "externally_initialized ";
  Out << (GV->isConstant() ? "constant " : "global ");
  TypePrinter.print(GV->getValueType(), Out);

  if (GV->hasInitializer()) {
    Out << ' ';
    writeOperand(GV->getInitializer(), false);
  }

  if (GV->hasSection()) {
    Out << ", section \"";
    printEscapedString(GV->getSection(), Out);
    Out << '"';
  }
  maybePrintComdat(Out, *GV);
  if (GV->getAlignment())
    Out << ", align " << GV->getAlignment();

  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  GV->getAllMetadata(MDs);
  printMetadataAttachments(MDs, ", ");

  auto Attrs = GV->getAttributes();
  if (Attrs.hasAttributes())
    Out << " #" << Machine.getAttributeGroupSlot(Attrs);

  printInfoComment(*GV);
}

void AssemblyWriter::printIndirectSymbol(const GlobalIndirectSymbol *GIS) {
  if (GIS->isMaterializable())
    Out << "; Materializable\n";

  WriteAsOperandInternal(Out, GIS, &TypePrinter, &Machine, GIS->getParent());
  Out << " = ";

  Out << getLinkageNameWithSpace(GIS->getLinkage());
  PrintDSOLocation(*GIS, Out);
  PrintVisibility(GIS->getVisibility(), Out);
  PrintDLLStorageClass(GIS->getDLLStorageClass(), Out);
  PrintThreadLocalModel(GIS->getThreadLocalMode(), Out);
  StringRef UA = getUnnamedAddrEncoding(GIS->getUnnamedAddr());
  if (!UA.empty())
      Out << UA << ' ';

  if (isa<GlobalAlias>(GIS))
    Out << "alias ";
  else if (isa<GlobalIFunc>(GIS))
    Out << "ifunc ";
  else
    llvm_unreachable("Not an alias or ifunc!");

  TypePrinter.print(GIS->getValueType(), Out);

  Out << ", ";

  const Constant *IS = GIS->getIndirectSymbol();

  if (!IS) {
    TypePrinter.print(GIS->getType(), Out);
    Out << " <<NULL ALIASEE>>";
  } else {
    writeOperand(IS, !isa<ConstantExpr>(IS));
  }

  printInfoComment(*GIS);
  Out << '\n';
}

void AssemblyWriter::printComdat(const Comdat *C) {
  C->print(Out);
}

void AssemblyWriter::printTypeIdentities() {
  if (TypePrinter.empty())
    return;

  Out << '\n';

  // Emit all numbered types.
  auto &NumberedTypes = TypePrinter.getNumberedTypes();
  for (unsigned I = 0, E = NumberedTypes.size(); I != E; ++I) {
    Out << '%' << I << " = type ";

    // Make sure we print out at least one level of the type structure, so
    // that we do not get %2 = type %2
    TypePrinter.printStructBody(NumberedTypes[I], Out);
    Out << '\n';
  }

  auto &NamedTypes = TypePrinter.getNamedTypes();
  for (unsigned I = 0, E = NamedTypes.size(); I != E; ++I) {
    PrintLLVMName(Out, NamedTypes[I]->getName(), LocalPrefix);
    Out << " = type ";

    // Make sure we print out at least one level of the type structure, so
    // that we do not get %FILE = type %FILE
    TypePrinter.printStructBody(NamedTypes[I], Out);
    Out << '\n';
  }
}

/// printFunction - Print all aspects of a function.
void AssemblyWriter::printFunction(const Function *F) {
  // Print out the return type and name.
  Out << '\n';

  if (AnnotationWriter) AnnotationWriter->emitFunctionAnnot(F, Out);

  if (F->isMaterializable())
    Out << "; Materializable\n";

  const AttributeList &Attrs = F->getAttributes();
  if (Attrs.hasAttributes(AttributeList::FunctionIndex)) {
    AttributeSet AS = Attrs.getFnAttributes();
    std::string AttrStr;

    for (const Attribute &Attr : AS) {
      if (!Attr.isStringAttribute()) {
        if (!AttrStr.empty()) AttrStr += ' ';
        AttrStr += Attr.getAsString();
      }
    }

    if (!AttrStr.empty())
      Out << "; Function Attrs: " << AttrStr << '\n';
  }

  Machine.incorporateFunction(F);

  if (F->isDeclaration()) {
    Out << "declare";
    SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
    F->getAllMetadata(MDs);
    printMetadataAttachments(MDs, " ");
    Out << ' ';
  } else
    Out << "define ";

  Out << getLinkageNameWithSpace(F->getLinkage());
  PrintDSOLocation(*F, Out);
  PrintVisibility(F->getVisibility(), Out);
  PrintDLLStorageClass(F->getDLLStorageClass(), Out);

  // Print the calling convention.
  if (F->getCallingConv() != CallingConv::C) {
    PrintCallingConv(F->getCallingConv(), Out);
    Out << " ";
  }

  FunctionType *FT = F->getFunctionType();
  if (Attrs.hasAttributes(AttributeList::ReturnIndex))
    Out << Attrs.getAsString(AttributeList::ReturnIndex) << ' ';
  TypePrinter.print(F->getReturnType(), Out);
  Out << ' ';
  WriteAsOperandInternal(Out, F, &TypePrinter, &Machine, F->getParent());
  Out << '(';

  // Loop over the arguments, printing them...
  if (F->isDeclaration() && !IsForDebug) {
    // We're only interested in the type here - don't print argument names.
    for (unsigned I = 0, E = FT->getNumParams(); I != E; ++I) {
      // Insert commas as we go... the first arg doesn't get a comma
      if (I)
        Out << ", ";
      // Output type...
      TypePrinter.print(FT->getParamType(I), Out);

      AttributeSet ArgAttrs = Attrs.getParamAttributes(I);
      if (ArgAttrs.hasAttributes())
        Out << ' ' << ArgAttrs.getAsString();
    }
  } else {
    // The arguments are meaningful here, print them in detail.
    for (const Argument &Arg : F->args()) {
      // Insert commas as we go... the first arg doesn't get a comma
      if (Arg.getArgNo() != 0)
        Out << ", ";
      printArgument(&Arg, Attrs.getParamAttributes(Arg.getArgNo()));
    }
  }

  // Finish printing arguments...
  if (FT->isVarArg()) {
    if (FT->getNumParams()) Out << ", ";
    Out << "...";  // Output varargs portion of signature!
  }
  Out << ')';
  StringRef UA = getUnnamedAddrEncoding(F->getUnnamedAddr());
  if (!UA.empty())
    Out << ' ' << UA;
  // We print the function address space if it is non-zero or if we are writing
  // a module with a non-zero program address space or if there is no valid
  // Module* so that the file can be parsed without the datalayout string.
  const Module *Mod = F->getParent();
  if (F->getAddressSpace() != 0 || !Mod ||
      Mod->getDataLayout().getProgramAddressSpace() != 0)
    Out << " addrspace(" << F->getAddressSpace() << ")";
  if (Attrs.hasAttributes(AttributeList::FunctionIndex))
    Out << " #" << Machine.getAttributeGroupSlot(Attrs.getFnAttributes());
  if (F->hasSection()) {
    Out << " section \"";
    printEscapedString(F->getSection(), Out);
    Out << '"';
  }
  maybePrintComdat(Out, *F);
  if (F->getAlignment())
    Out << " align " << F->getAlignment();
  if (F->hasGC())
    Out << " gc \"" << F->getGC() << '"';
  if (F->hasPrefixData()) {
    Out << " prefix ";
    writeOperand(F->getPrefixData(), true);
  }
  if (F->hasPrologueData()) {
    Out << " prologue ";
    writeOperand(F->getPrologueData(), true);
  }
  if (F->hasPersonalityFn()) {
    Out << " personality ";
    writeOperand(F->getPersonalityFn(), /*PrintType=*/true);
  }

  if (F->isDeclaration()) {
    Out << '\n';
  } else {
    SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
    F->getAllMetadata(MDs);
    printMetadataAttachments(MDs, " ");

    Out << " {";
    // Output all of the function's basic blocks.
    for (const BasicBlock &BB : *F)
      printBasicBlock(&BB);

    // Output the function's use-lists.
    printUseLists(F);

    Out << "}\n";
  }

  Machine.purgeFunction();
}

/// printArgument - This member is called for every argument that is passed into
/// the function.  Simply print it out
void AssemblyWriter::printArgument(const Argument *Arg, AttributeSet Attrs) {
  // Output type...
  TypePrinter.print(Arg->getType(), Out);

  // Output parameter attributes list
  if (Attrs.hasAttributes())
    Out << ' ' << Attrs.getAsString();

  // Output name, if available...
  if (Arg->hasName()) {
    Out << ' ';
    PrintLLVMName(Out, Arg);
  }
}

/// printBasicBlock - This member is called for each basic block in a method.
void AssemblyWriter::printBasicBlock(const BasicBlock *BB) {
  if (BB->hasName()) {              // Print out the label if it exists...
    Out << "\n";
    PrintLLVMName(Out, BB->getName(), LabelPrefix);
    Out << ':';
  } else if (!BB->use_empty()) {      // Don't print block # of no uses...
    Out << "\n; <label>:";
    int Slot = Machine.getLocalSlot(BB);
    if (Slot != -1)
      Out << Slot << ":";
    else
      Out << "<badref>";
  }

  if (!BB->getParent()) {
    Out.PadToColumn(50);
    Out << "; Error: Block without parent!";
  } else if (BB != &BB->getParent()->getEntryBlock()) {  // Not the entry block?
    // Output predecessors for the block.
    Out.PadToColumn(50);
    Out << ";";
    const_pred_iterator PI = pred_begin(BB), PE = pred_end(BB);

    if (PI == PE) {
      Out << " No predecessors!";
    } else {
      Out << " preds = ";
      writeOperand(*PI, false);
      for (++PI; PI != PE; ++PI) {
        Out << ", ";
        writeOperand(*PI, false);
      }
    }
  }

  Out << "\n";

  if (AnnotationWriter) AnnotationWriter->emitBasicBlockStartAnnot(BB, Out);

  // Output all of the instructions in the basic block...
  for (const Instruction &I : *BB) {
    printInstructionLine(I);
  }

  if (AnnotationWriter) AnnotationWriter->emitBasicBlockEndAnnot(BB, Out);
}

/// printInstructionLine - Print an instruction and a newline character.
void AssemblyWriter::printInstructionLine(const Instruction &I) {
  printInstruction(I);
  Out << '\n';
}

/// printGCRelocateComment - print comment after call to the gc.relocate
/// intrinsic indicating base and derived pointer names.
void AssemblyWriter::printGCRelocateComment(const GCRelocateInst &Relocate) {
  Out << " ; (";
  writeOperand(Relocate.getBasePtr(), false);
  Out << ", ";
  writeOperand(Relocate.getDerivedPtr(), false);
  Out << ")";
}

/// printInfoComment - Print a little comment after the instruction indicating
/// which slot it occupies.
void AssemblyWriter::printInfoComment(const Value &V) {
  if (const auto *Relocate = dyn_cast<GCRelocateInst>(&V))
    printGCRelocateComment(*Relocate);

  if (AnnotationWriter)
    AnnotationWriter->printInfoComment(V, Out);
}

static void maybePrintCallAddrSpace(const Value *Operand, const Instruction *I,
                                    raw_ostream &Out) {
  // We print the address space of the call if it is non-zero.
  unsigned CallAddrSpace = Operand->getType()->getPointerAddressSpace();
  bool PrintAddrSpace = CallAddrSpace != 0;
  if (!PrintAddrSpace) {
    const Module *Mod = getModuleFromVal(I);
    // We also print it if it is zero but not equal to the program address space
    // or if we can't find a valid Module* to make it possible to parse
    // the resulting file even without a datalayout string.
    if (!Mod || Mod->getDataLayout().getProgramAddressSpace() != 0)
      PrintAddrSpace = true;
  }
  if (PrintAddrSpace)
    Out << " addrspace(" << CallAddrSpace << ")";
}

// This member is called for each Instruction in a function..
void AssemblyWriter::printInstruction(const Instruction &I) {
  if (AnnotationWriter) AnnotationWriter->emitInstructionAnnot(&I, Out);

  // Print out indentation for an instruction.
  Out << "  ";

  // Print out name if it exists...
  if (I.hasName()) {
    PrintLLVMName(Out, &I);
    Out << " = ";
  } else if (!I.getType()->isVoidTy()) {
    // Print out the def slot taken.
    int SlotNum = Machine.getLocalSlot(&I);
    if (SlotNum == -1)
      Out << "<badref> = ";
    else
      Out << '%' << SlotNum << " = ";
  }

  if (const CallInst *CI = dyn_cast<CallInst>(&I)) {
    if (CI->isMustTailCall())
      Out << "musttail ";
    else if (CI->isTailCall())
      Out << "tail ";
    else if (CI->isNoTailCall())
      Out << "notail ";
  }

  // Print out the opcode...
  Out << I.getOpcodeName();

  // If this is an atomic load or store, print out the atomic marker.
  if ((isa<LoadInst>(I)  && cast<LoadInst>(I).isAtomic()) ||
      (isa<StoreInst>(I) && cast<StoreInst>(I).isAtomic()))
    Out << " atomic";

  if (isa<AtomicCmpXchgInst>(I) && cast<AtomicCmpXchgInst>(I).isWeak())
    Out << " weak";

  // If this is a volatile operation, print out the volatile marker.
  if ((isa<LoadInst>(I)  && cast<LoadInst>(I).isVolatile()) ||
      (isa<StoreInst>(I) && cast<StoreInst>(I).isVolatile()) ||
      (isa<AtomicCmpXchgInst>(I) && cast<AtomicCmpXchgInst>(I).isVolatile()) ||
      (isa<AtomicRMWInst>(I) && cast<AtomicRMWInst>(I).isVolatile()))
    Out << " volatile";

  // Print out optimization information.
  WriteOptimizationInfo(Out, &I);

  // Print out the compare instruction predicates
  if (const CmpInst *CI = dyn_cast<CmpInst>(&I))
    Out << ' ' << CmpInst::getPredicateName(CI->getPredicate());

  // Print out the atomicrmw operation
  if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(&I))
    Out << ' ' << AtomicRMWInst::getOperationName(RMWI->getOperation());

  // Print out the type of the operands...
  const Value *Operand = I.getNumOperands() ? I.getOperand(0) : nullptr;

  // Special case conditional branches to swizzle the condition out to the front
  if (isa<BranchInst>(I) && cast<BranchInst>(I).isConditional()) {
    const BranchInst &BI(cast<BranchInst>(I));
    Out << ' ';
    writeOperand(BI.getCondition(), true);
    Out << ", ";
    writeOperand(BI.getSuccessor(0), true);
    Out << ", ";
    writeOperand(BI.getSuccessor(1), true);

  } else if (isa<SwitchInst>(I)) {
    const SwitchInst& SI(cast<SwitchInst>(I));
    // Special case switch instruction to get formatting nice and correct.
    Out << ' ';
    writeOperand(SI.getCondition(), true);
    Out << ", ";
    writeOperand(SI.getDefaultDest(), true);
    Out << " [";
    for (auto Case : SI.cases()) {
      Out << "\n    ";
      writeOperand(Case.getCaseValue(), true);
      Out << ", ";
      writeOperand(Case.getCaseSuccessor(), true);
    }
    Out << "\n  ]";
  } else if (isa<IndirectBrInst>(I)) {
    // Special case indirectbr instruction to get formatting nice and correct.
    Out << ' ';
    writeOperand(Operand, true);
    Out << ", [";

    for (unsigned i = 1, e = I.getNumOperands(); i != e; ++i) {
      if (i != 1)
        Out << ", ";
      writeOperand(I.getOperand(i), true);
    }
    Out << ']';
  } else if (const PHINode *PN = dyn_cast<PHINode>(&I)) {
    Out << ' ';
    TypePrinter.print(I.getType(), Out);
    Out << ' ';

    for (unsigned op = 0, Eop = PN->getNumIncomingValues(); op < Eop; ++op) {
      if (op) Out << ", ";
      Out << "[ ";
      writeOperand(PN->getIncomingValue(op), false); Out << ", ";
      writeOperand(PN->getIncomingBlock(op), false); Out << " ]";
    }
  } else if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(&I)) {
    Out << ' ';
    writeOperand(I.getOperand(0), true);
    for (const unsigned *i = EVI->idx_begin(), *e = EVI->idx_end(); i != e; ++i)
      Out << ", " << *i;
  } else if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(&I)) {
    Out << ' ';
    writeOperand(I.getOperand(0), true); Out << ", ";
    writeOperand(I.getOperand(1), true);
    for (const unsigned *i = IVI->idx_begin(), *e = IVI->idx_end(); i != e; ++i)
      Out << ", " << *i;
  } else if (const LandingPadInst *LPI = dyn_cast<LandingPadInst>(&I)) {
    Out << ' ';
    TypePrinter.print(I.getType(), Out);
    if (LPI->isCleanup() || LPI->getNumClauses() != 0)
      Out << '\n';

    if (LPI->isCleanup())
      Out << "          cleanup";

    for (unsigned i = 0, e = LPI->getNumClauses(); i != e; ++i) {
      if (i != 0 || LPI->isCleanup()) Out << "\n";
      if (LPI->isCatch(i))
        Out << "          catch ";
      else
        Out << "          filter ";

      writeOperand(LPI->getClause(i), true);
    }
  } else if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(&I)) {
    Out << " within ";
    writeOperand(CatchSwitch->getParentPad(), /*PrintType=*/false);
    Out << " [";
    unsigned Op = 0;
    for (const BasicBlock *PadBB : CatchSwitch->handlers()) {
      if (Op > 0)
        Out << ", ";
      writeOperand(PadBB, /*PrintType=*/true);
      ++Op;
    }
    Out << "] unwind ";
    if (const BasicBlock *UnwindDest = CatchSwitch->getUnwindDest())
      writeOperand(UnwindDest, /*PrintType=*/true);
    else
      Out << "to caller";
  } else if (const auto *FPI = dyn_cast<FuncletPadInst>(&I)) {
    Out << " within ";
    writeOperand(FPI->getParentPad(), /*PrintType=*/false);
    Out << " [";
    for (unsigned Op = 0, NumOps = FPI->getNumArgOperands(); Op < NumOps;
         ++Op) {
      if (Op > 0)
        Out << ", ";
      writeOperand(FPI->getArgOperand(Op), /*PrintType=*/true);
    }
    Out << ']';
  } else if (isa<ReturnInst>(I) && !Operand) {
    Out << " void";
  } else if (const auto *CRI = dyn_cast<CatchReturnInst>(&I)) {
    Out << " from ";
    writeOperand(CRI->getOperand(0), /*PrintType=*/false);

    Out << " to ";
    writeOperand(CRI->getOperand(1), /*PrintType=*/true);
  } else if (const auto *CRI = dyn_cast<CleanupReturnInst>(&I)) {
    Out << " from ";
    writeOperand(CRI->getOperand(0), /*PrintType=*/false);

    Out << " unwind ";
    if (CRI->hasUnwindDest())
      writeOperand(CRI->getOperand(1), /*PrintType=*/true);
    else
      Out << "to caller";
  } else if (const CallInst *CI = dyn_cast<CallInst>(&I)) {
    // Print the calling convention being used.
    if (CI->getCallingConv() != CallingConv::C) {
      Out << " ";
      PrintCallingConv(CI->getCallingConv(), Out);
    }

    Operand = CI->getCalledValue();
    FunctionType *FTy = CI->getFunctionType();
    Type *RetTy = FTy->getReturnType();
    const AttributeList &PAL = CI->getAttributes();

    if (PAL.hasAttributes(AttributeList::ReturnIndex))
      Out << ' ' << PAL.getAsString(AttributeList::ReturnIndex);

    // Only print addrspace(N) if necessary:
    maybePrintCallAddrSpace(Operand, &I, Out);

    // If possible, print out the short form of the call instruction.  We can
    // only do this if the first argument is a pointer to a nonvararg function,
    // and if the return type is not a pointer to a function.
    //
    Out << ' ';
    TypePrinter.print(FTy->isVarArg() ? FTy : RetTy, Out);
    Out << ' ';
    writeOperand(Operand, false);
    Out << '(';
    for (unsigned op = 0, Eop = CI->getNumArgOperands(); op < Eop; ++op) {
      if (op > 0)
        Out << ", ";
      writeParamOperand(CI->getArgOperand(op), PAL.getParamAttributes(op));
    }

    // Emit an ellipsis if this is a musttail call in a vararg function.  This
    // is only to aid readability, musttail calls forward varargs by default.
    if (CI->isMustTailCall() && CI->getParent() &&
        CI->getParent()->getParent() &&
        CI->getParent()->getParent()->isVarArg())
      Out << ", ...";

    Out << ')';
    if (PAL.hasAttributes(AttributeList::FunctionIndex))
      Out << " #" << Machine.getAttributeGroupSlot(PAL.getFnAttributes());

    writeOperandBundles(CI);
  } else if (const InvokeInst *II = dyn_cast<InvokeInst>(&I)) {
    Operand = II->getCalledValue();
    FunctionType *FTy = II->getFunctionType();
    Type *RetTy = FTy->getReturnType();
    const AttributeList &PAL = II->getAttributes();

    // Print the calling convention being used.
    if (II->getCallingConv() != CallingConv::C) {
      Out << " ";
      PrintCallingConv(II->getCallingConv(), Out);
    }

    if (PAL.hasAttributes(AttributeList::ReturnIndex))
      Out << ' ' << PAL.getAsString(AttributeList::ReturnIndex);

    // Only print addrspace(N) if necessary:
    maybePrintCallAddrSpace(Operand, &I, Out);

    // If possible, print out the short form of the invoke instruction. We can
    // only do this if the first argument is a pointer to a nonvararg function,
    // and if the return type is not a pointer to a function.
    //
    Out << ' ';
    TypePrinter.print(FTy->isVarArg() ? FTy : RetTy, Out);
    Out << ' ';
    writeOperand(Operand, false);
    Out << '(';
    for (unsigned op = 0, Eop = II->getNumArgOperands(); op < Eop; ++op) {
      if (op)
        Out << ", ";
      writeParamOperand(II->getArgOperand(op), PAL.getParamAttributes(op));
    }

    Out << ')';
    if (PAL.hasAttributes(AttributeList::FunctionIndex))
      Out << " #" << Machine.getAttributeGroupSlot(PAL.getFnAttributes());

    writeOperandBundles(II);

    Out << "\n          to ";
    writeOperand(II->getNormalDest(), true);
    Out << " unwind ";
    writeOperand(II->getUnwindDest(), true);
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
    Out << ' ';
    if (AI->isUsedWithInAlloca())
      Out << "inalloca ";
    if (AI->isSwiftError())
      Out << "swifterror ";
    TypePrinter.print(AI->getAllocatedType(), Out);

    // Explicitly write the array size if the code is broken, if it's an array
    // allocation, or if the type is not canonical for scalar allocations.  The
    // latter case prevents the type from mutating when round-tripping through
    // assembly.
    if (!AI->getArraySize() || AI->isArrayAllocation() ||
        !AI->getArraySize()->getType()->isIntegerTy(32)) {
      Out << ", ";
      writeOperand(AI->getArraySize(), true);
    }
    if (AI->getAlignment()) {
      Out << ", align " << AI->getAlignment();
    }

    unsigned AddrSpace = AI->getType()->getAddressSpace();
    if (AddrSpace != 0) {
      Out << ", addrspace(" << AddrSpace << ')';
    }
  } else if (isa<CastInst>(I)) {
    if (Operand) {
      Out << ' ';
      writeOperand(Operand, true);   // Work with broken code
    }
    Out << " to ";
    TypePrinter.print(I.getType(), Out);
  } else if (isa<VAArgInst>(I)) {
    if (Operand) {
      Out << ' ';
      writeOperand(Operand, true);   // Work with broken code
    }
    Out << ", ";
    TypePrinter.print(I.getType(), Out);
  } else if (Operand) {   // Print the normal way.
    if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      Out << ' ';
      TypePrinter.print(GEP->getSourceElementType(), Out);
      Out << ',';
    } else if (const auto *LI = dyn_cast<LoadInst>(&I)) {
      Out << ' ';
      TypePrinter.print(LI->getType(), Out);
      Out << ',';
    }

    // PrintAllTypes - Instructions who have operands of all the same type
    // omit the type from all but the first operand.  If the instruction has
    // different type operands (for example br), then they are all printed.
    bool PrintAllTypes = false;
    Type *TheType = Operand->getType();

    // Select, Store and ShuffleVector always print all types.
    if (isa<SelectInst>(I) || isa<StoreInst>(I) || isa<ShuffleVectorInst>(I)
        || isa<ReturnInst>(I)) {
      PrintAllTypes = true;
    } else {
      for (unsigned i = 1, E = I.getNumOperands(); i != E; ++i) {
        Operand = I.getOperand(i);
        // note that Operand shouldn't be null, but the test helps make dump()
        // more tolerant of malformed IR
        if (Operand && Operand->getType() != TheType) {
          PrintAllTypes = true;    // We have differing types!  Print them all!
          break;
        }
      }
    }

    if (!PrintAllTypes) {
      Out << ' ';
      TypePrinter.print(TheType, Out);
    }

    Out << ' ';
    for (unsigned i = 0, E = I.getNumOperands(); i != E; ++i) {
      if (i) Out << ", ";
      writeOperand(I.getOperand(i), PrintAllTypes);
    }
  }

  // Print atomic ordering/alignment for memory operations
  if (const LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    if (LI->isAtomic())
      writeAtomic(LI->getContext(), LI->getOrdering(), LI->getSyncScopeID());
    if (LI->getAlignment())
      Out << ", align " << LI->getAlignment();
  } else if (const StoreInst *SI = dyn_cast<StoreInst>(&I)) {
    if (SI->isAtomic())
      writeAtomic(SI->getContext(), SI->getOrdering(), SI->getSyncScopeID());
    if (SI->getAlignment())
      Out << ", align " << SI->getAlignment();
  } else if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(&I)) {
    writeAtomicCmpXchg(CXI->getContext(), CXI->getSuccessOrdering(),
                       CXI->getFailureOrdering(), CXI->getSyncScopeID());
  } else if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(&I)) {
    writeAtomic(RMWI->getContext(), RMWI->getOrdering(),
                RMWI->getSyncScopeID());
  } else if (const FenceInst *FI = dyn_cast<FenceInst>(&I)) {
    writeAtomic(FI->getContext(), FI->getOrdering(), FI->getSyncScopeID());
  }

  // Print Metadata info.
  SmallVector<std::pair<unsigned, MDNode *>, 4> InstMD;
  I.getAllMetadata(InstMD);
  printMetadataAttachments(InstMD, ", ");

  // Print a nice comment.
  printInfoComment(I);
}

void AssemblyWriter::printMetadataAttachments(
    const SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs,
    StringRef Separator) {
  if (MDs.empty())
    return;

  if (MDNames.empty())
    MDs[0].second->getContext().getMDKindNames(MDNames);

  for (const auto &I : MDs) {
    unsigned Kind = I.first;
    Out << Separator;
    if (Kind < MDNames.size()) {
      Out << "!";
      printMetadataIdentifier(MDNames[Kind], Out);
    } else
      Out << "!<unknown kind #" << Kind << ">";
    Out << ' ';
    WriteAsOperandInternal(Out, I.second, &TypePrinter, &Machine, TheModule);
  }
}

void AssemblyWriter::writeMDNode(unsigned Slot, const MDNode *Node) {
  Out << '!' << Slot << " = ";
  printMDNodeBody(Node);
  Out << "\n";
}

void AssemblyWriter::writeAllMDNodes() {
  SmallVector<const MDNode *, 16> Nodes;
  Nodes.resize(Machine.mdn_size());
  for (SlotTracker::mdn_iterator I = Machine.mdn_begin(), E = Machine.mdn_end();
       I != E; ++I)
    Nodes[I->second] = cast<MDNode>(I->first);

  for (unsigned i = 0, e = Nodes.size(); i != e; ++i) {
    writeMDNode(i, Nodes[i]);
  }
}

void AssemblyWriter::printMDNodeBody(const MDNode *Node) {
  WriteMDNodeBodyInternal(Out, Node, &TypePrinter, &Machine, TheModule);
}

void AssemblyWriter::writeAllAttributeGroups() {
  std::vector<std::pair<AttributeSet, unsigned>> asVec;
  asVec.resize(Machine.as_size());

  for (SlotTracker::as_iterator I = Machine.as_begin(), E = Machine.as_end();
       I != E; ++I)
    asVec[I->second] = *I;

  for (const auto &I : asVec)
    Out << "attributes #" << I.second << " = { "
        << I.first.getAsString(true) << " }\n";
}

void AssemblyWriter::printUseListOrder(const UseListOrder &Order) {
  bool IsInFunction = Machine.getFunction();
  if (IsInFunction)
    Out << "  ";

  Out << "uselistorder";
  if (const BasicBlock *BB =
          IsInFunction ? nullptr : dyn_cast<BasicBlock>(Order.V)) {
    Out << "_bb ";
    writeOperand(BB->getParent(), false);
    Out << ", ";
    writeOperand(BB, false);
  } else {
    Out << " ";
    writeOperand(Order.V, true);
  }
  Out << ", { ";

  assert(Order.Shuffle.size() >= 2 && "Shuffle too small");
  Out << Order.Shuffle[0];
  for (unsigned I = 1, E = Order.Shuffle.size(); I != E; ++I)
    Out << ", " << Order.Shuffle[I];
  Out << " }\n";
}

void AssemblyWriter::printUseLists(const Function *F) {
  auto hasMore =
      [&]() { return !UseListOrders.empty() && UseListOrders.back().F == F; };
  if (!hasMore())
    // Nothing to do.
    return;

  Out << "\n; uselistorder directives\n";
  while (hasMore()) {
    printUseListOrder(UseListOrders.back());
    UseListOrders.pop_back();
  }
}

//===----------------------------------------------------------------------===//
//                       External Interface declarations
//===----------------------------------------------------------------------===//

void Function::print(raw_ostream &ROS, AssemblyAnnotationWriter *AAW,
                     bool ShouldPreserveUseListOrder,
                     bool IsForDebug) const {
  SlotTracker SlotTable(this->getParent());
  formatted_raw_ostream OS(ROS);
  AssemblyWriter W(OS, SlotTable, this->getParent(), AAW,
                   IsForDebug,
                   ShouldPreserveUseListOrder);
  W.printFunction(this);
}

void Module::print(raw_ostream &ROS, AssemblyAnnotationWriter *AAW,
                   bool ShouldPreserveUseListOrder, bool IsForDebug) const {
  SlotTracker SlotTable(this);
  formatted_raw_ostream OS(ROS);
  AssemblyWriter W(OS, SlotTable, this, AAW, IsForDebug,
                   ShouldPreserveUseListOrder);
  W.printModule(this);
}

void NamedMDNode::print(raw_ostream &ROS, bool IsForDebug) const {
  SlotTracker SlotTable(getParent());
  formatted_raw_ostream OS(ROS);
  AssemblyWriter W(OS, SlotTable, getParent(), nullptr, IsForDebug);
  W.printNamedMDNode(this);
}

void NamedMDNode::print(raw_ostream &ROS, ModuleSlotTracker &MST,
                        bool IsForDebug) const {
  Optional<SlotTracker> LocalST;
  SlotTracker *SlotTable;
  if (auto *ST = MST.getMachine())
    SlotTable = ST;
  else {
    LocalST.emplace(getParent());
    SlotTable = &*LocalST;
  }

  formatted_raw_ostream OS(ROS);
  AssemblyWriter W(OS, *SlotTable, getParent(), nullptr, IsForDebug);
  W.printNamedMDNode(this);
}

void Comdat::print(raw_ostream &ROS, bool /*IsForDebug*/) const {
  PrintLLVMName(ROS, getName(), ComdatPrefix);
  ROS << " = comdat ";

  switch (getSelectionKind()) {
  case Comdat::Any:
    ROS << "any";
    break;
  case Comdat::ExactMatch:
    ROS << "exactmatch";
    break;
  case Comdat::Largest:
    ROS << "largest";
    break;
  case Comdat::NoDuplicates:
    ROS << "noduplicates";
    break;
  case Comdat::SameSize:
    ROS << "samesize";
    break;
  }

  ROS << '\n';
}

void Type::print(raw_ostream &OS, bool /*IsForDebug*/, bool NoDetails) const {
  TypePrinting TP;
  TP.print(const_cast<Type*>(this), OS);

  if (NoDetails)
    return;

  // If the type is a named struct type, print the body as well.
  if (StructType *STy = dyn_cast<StructType>(const_cast<Type*>(this)))
    if (!STy->isLiteral()) {
      OS << " = type ";
      TP.printStructBody(STy, OS);
    }
}

static bool isReferencingMDNode(const Instruction &I) {
  if (const auto *CI = dyn_cast<CallInst>(&I))
    if (Function *F = CI->getCalledFunction())
      if (F->isIntrinsic())
        for (auto &Op : I.operands())
          if (auto *V = dyn_cast_or_null<MetadataAsValue>(Op))
            if (isa<MDNode>(V->getMetadata()))
              return true;
  return false;
}

void Value::print(raw_ostream &ROS, bool IsForDebug) const {
  bool ShouldInitializeAllMetadata = false;
  if (auto *I = dyn_cast<Instruction>(this))
    ShouldInitializeAllMetadata = isReferencingMDNode(*I);
  else if (isa<Function>(this) || isa<MetadataAsValue>(this))
    ShouldInitializeAllMetadata = true;

  ModuleSlotTracker MST(getModuleFromVal(this), ShouldInitializeAllMetadata);
  print(ROS, MST, IsForDebug);
}

void Value::print(raw_ostream &ROS, ModuleSlotTracker &MST,
                  bool IsForDebug) const {
  formatted_raw_ostream OS(ROS);
  SlotTracker EmptySlotTable(static_cast<const Module *>(nullptr));
  SlotTracker &SlotTable =
      MST.getMachine() ? *MST.getMachine() : EmptySlotTable;
  auto incorporateFunction = [&](const Function *F) {
    if (F)
      MST.incorporateFunction(*F);
  };

  if (const Instruction *I = dyn_cast<Instruction>(this)) {
    incorporateFunction(I->getParent() ? I->getParent()->getParent() : nullptr);
    AssemblyWriter W(OS, SlotTable, getModuleFromVal(I), nullptr, IsForDebug);
    W.printInstruction(*I);
  } else if (const BasicBlock *BB = dyn_cast<BasicBlock>(this)) {
    incorporateFunction(BB->getParent());
    AssemblyWriter W(OS, SlotTable, getModuleFromVal(BB), nullptr, IsForDebug);
    W.printBasicBlock(BB);
  } else if (const GlobalValue *GV = dyn_cast<GlobalValue>(this)) {
    AssemblyWriter W(OS, SlotTable, GV->getParent(), nullptr, IsForDebug);
    if (const GlobalVariable *V = dyn_cast<GlobalVariable>(GV))
      W.printGlobal(V);
    else if (const Function *F = dyn_cast<Function>(GV))
      W.printFunction(F);
    else
      W.printIndirectSymbol(cast<GlobalIndirectSymbol>(GV));
  } else if (const MetadataAsValue *V = dyn_cast<MetadataAsValue>(this)) {
    V->getMetadata()->print(ROS, MST, getModuleFromVal(V));
  } else if (const Constant *C = dyn_cast<Constant>(this)) {
    TypePrinting TypePrinter;
    TypePrinter.print(C->getType(), OS);
    OS << ' ';
    WriteConstantInternal(OS, C, TypePrinter, MST.getMachine(), nullptr);
  } else if (isa<InlineAsm>(this) || isa<Argument>(this)) {
    this->printAsOperand(OS, /* PrintType */ true, MST);
  } else {
    llvm_unreachable("Unknown value to print out!");
  }
}

/// Print without a type, skipping the TypePrinting object.
///
/// \return \c true iff printing was successful.
static bool printWithoutType(const Value &V, raw_ostream &O,
                             SlotTracker *Machine, const Module *M) {
  if (V.hasName() || isa<GlobalValue>(V) ||
      (!isa<Constant>(V) && !isa<MetadataAsValue>(V))) {
    WriteAsOperandInternal(O, &V, nullptr, Machine, M);
    return true;
  }
  return false;
}

static void printAsOperandImpl(const Value &V, raw_ostream &O, bool PrintType,
                               ModuleSlotTracker &MST) {
  TypePrinting TypePrinter(MST.getModule());
  if (PrintType) {
    TypePrinter.print(V.getType(), O);
    O << ' ';
  }

  WriteAsOperandInternal(O, &V, &TypePrinter, MST.getMachine(),
                         MST.getModule());
}

void Value::printAsOperand(raw_ostream &O, bool PrintType,
                           const Module *M) const {
  if (!M)
    M = getModuleFromVal(this);

  if (!PrintType)
    if (printWithoutType(*this, O, nullptr, M))
      return;

  SlotTracker Machine(
      M, /* ShouldInitializeAllMetadata */ isa<MetadataAsValue>(this));
  ModuleSlotTracker MST(Machine, M);
  printAsOperandImpl(*this, O, PrintType, MST);
}

void Value::printAsOperand(raw_ostream &O, bool PrintType,
                           ModuleSlotTracker &MST) const {
  if (!PrintType)
    if (printWithoutType(*this, O, MST.getMachine(), MST.getModule()))
      return;

  printAsOperandImpl(*this, O, PrintType, MST);
}

static void printMetadataImpl(raw_ostream &ROS, const Metadata &MD,
                              ModuleSlotTracker &MST, const Module *M,
                              bool OnlyAsOperand) {
  formatted_raw_ostream OS(ROS);

  TypePrinting TypePrinter(M);

  WriteAsOperandInternal(OS, &MD, &TypePrinter, MST.getMachine(), M,
                         /* FromValue */ true);

  auto *N = dyn_cast<MDNode>(&MD);
  if (OnlyAsOperand || !N || isa<DIExpression>(MD))
    return;

  OS << " = ";
  WriteMDNodeBodyInternal(OS, N, &TypePrinter, MST.getMachine(), M);
}

void Metadata::printAsOperand(raw_ostream &OS, const Module *M) const {
  ModuleSlotTracker MST(M, isa<MDNode>(this));
  printMetadataImpl(OS, *this, MST, M, /* OnlyAsOperand */ true);
}

void Metadata::printAsOperand(raw_ostream &OS, ModuleSlotTracker &MST,
                              const Module *M) const {
  printMetadataImpl(OS, *this, MST, M, /* OnlyAsOperand */ true);
}

void Metadata::print(raw_ostream &OS, const Module *M,
                     bool /*IsForDebug*/) const {
  ModuleSlotTracker MST(M, isa<MDNode>(this));
  printMetadataImpl(OS, *this, MST, M, /* OnlyAsOperand */ false);
}

void Metadata::print(raw_ostream &OS, ModuleSlotTracker &MST,
                     const Module *M, bool /*IsForDebug*/) const {
  printMetadataImpl(OS, *this, MST, M, /* OnlyAsOperand */ false);
}

void ModuleSummaryIndex::print(raw_ostream &ROS, bool IsForDebug) const {
  SlotTracker SlotTable(this);
  formatted_raw_ostream OS(ROS);
  AssemblyWriter W(OS, SlotTable, this, IsForDebug);
  W.printModuleSummaryIndex();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// Value::dump - allow easy printing of Values from the debugger.
LLVM_DUMP_METHOD
void Value::dump() const { print(dbgs(), /*IsForDebug=*/true); dbgs() << '\n'; }

// Type::dump - allow easy printing of Types from the debugger.
LLVM_DUMP_METHOD
void Type::dump() const { print(dbgs(), /*IsForDebug=*/true); dbgs() << '\n'; }

// Module::dump() - Allow printing of Modules from the debugger.
LLVM_DUMP_METHOD
void Module::dump() const {
  print(dbgs(), nullptr,
        /*ShouldPreserveUseListOrder=*/false, /*IsForDebug=*/true);
}

// Allow printing of Comdats from the debugger.
LLVM_DUMP_METHOD
void Comdat::dump() const { print(dbgs(), /*IsForDebug=*/true); }

// NamedMDNode::dump() - Allow printing of NamedMDNodes from the debugger.
LLVM_DUMP_METHOD
void NamedMDNode::dump() const { print(dbgs(), /*IsForDebug=*/true); }

LLVM_DUMP_METHOD
void Metadata::dump() const { dump(nullptr); }

LLVM_DUMP_METHOD
void Metadata::dump(const Module *M) const {
  print(dbgs(), M, /*IsForDebug=*/true);
  dbgs() << '\n';
}

// Allow printing of ModuleSummaryIndex from the debugger.
LLVM_DUMP_METHOD
void ModuleSummaryIndex::dump() const { print(dbgs(), /*IsForDebug=*/true); }
#endif
