//===- llvm-stress.cpp - Generate random LL files to stress-test LLVM -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that generates random .ll files to stress-test
// different components in LLVM.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace llvm {

static cl::opt<unsigned> SeedCL("seed",
  cl::desc("Seed used for randomness"), cl::init(0));

static cl::opt<unsigned> SizeCL("size",
  cl::desc("The estimated size of the generated function (# of instrs)"),
  cl::init(100));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static LLVMContext Context;

namespace cl {

template <> class parser<Type*> final : public basic_parser<Type*> {
public:
  parser(Option &O) : basic_parser(O) {}

  // Parse options as IR types. Return true on error.
  bool parse(Option &O, StringRef, StringRef Arg, Type *&Value) {
    if      (Arg == "half")      Value = Type::getHalfTy(Context);
    else if (Arg == "fp128")     Value = Type::getFP128Ty(Context);
    else if (Arg == "x86_fp80")  Value = Type::getX86_FP80Ty(Context);
    else if (Arg == "ppc_fp128") Value = Type::getPPC_FP128Ty(Context);
    else if (Arg == "x86_mmx")   Value = Type::getX86_MMXTy(Context);
    else if (Arg.startswith("i")) {
      unsigned N = 0;
      Arg.drop_front().getAsInteger(10, N);
      if (N > 0)
        Value = Type::getIntNTy(Context, N);
    }

    if (!Value)
      return O.error("Invalid IR scalar type: '" + Arg + "'!");
    return false;
  }

  StringRef getValueName() const override { return "IR scalar type"; }
};

} // end namespace cl

static cl::list<Type*> AdditionalScalarTypes("types", cl::CommaSeparated,
  cl::desc("Additional IR scalar types "
           "(always includes i1, i8, i16, i32, i64, float and double)"));

namespace {

/// A utility class to provide a pseudo-random number generator which is
/// the same across all platforms. This is somewhat close to the libc
/// implementation. Note: This is not a cryptographically secure pseudorandom
/// number generator.
class Random {
public:
  /// C'tor
  Random(unsigned _seed):Seed(_seed) {}

  /// Return a random integer, up to a
  /// maximum of 2**19 - 1.
  uint32_t Rand() {
    uint32_t Val = Seed + 0x000b07a1;
    Seed = (Val * 0x3c7c0ac1);
    // Only lowest 19 bits are random-ish.
    return Seed & 0x7ffff;
  }

  /// Return a random 64 bit integer.
  uint64_t Rand64() {
    uint64_t Val = Rand() & 0xffff;
    Val |= uint64_t(Rand() & 0xffff) << 16;
    Val |= uint64_t(Rand() & 0xffff) << 32;
    Val |= uint64_t(Rand() & 0xffff) << 48;
    return Val;
  }

  /// Rand operator for STL algorithms.
  ptrdiff_t operator()(ptrdiff_t y) {
    return  Rand64() % y;
  }

  /// Make this like a C++11 random device
  using result_type = uint32_t ;

  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return 0x7ffff; }

  uint32_t operator()() {
    uint32_t Val = Rand();
    assert(Val <= max() && "Random value out of range");
    return Val;
  }

private:
  unsigned Seed;
};

/// Generate an empty function with a default argument list.
Function *GenEmptyFunction(Module *M) {
  // Define a few arguments
  LLVMContext &Context = M->getContext();
  Type* ArgsTy[] = {
    Type::getInt8PtrTy(Context),
    Type::getInt32PtrTy(Context),
    Type::getInt64PtrTy(Context),
    Type::getInt32Ty(Context),
    Type::getInt64Ty(Context),
    Type::getInt8Ty(Context)
  };

  auto *FuncTy = FunctionType::get(Type::getVoidTy(Context), ArgsTy, false);
  // Pick a unique name to describe the input parameters
  Twine Name = "autogen_SD" + Twine{SeedCL};
  auto *Func = Function::Create(FuncTy, GlobalValue::ExternalLinkage, Name, M);
  Func->setCallingConv(CallingConv::C);
  return Func;
}

/// A base class, implementing utilities needed for
/// modifying and adding new random instructions.
struct Modifier {
  /// Used to store the randomly generated values.
  using PieceTable = std::vector<Value *>;

public:
  /// C'tor
  Modifier(BasicBlock *Block, PieceTable *PT, Random *R)
      : BB(Block), PT(PT), Ran(R), Context(BB->getContext()) {}

  /// virtual D'tor to silence warnings.
  virtual ~Modifier() = default;

  /// Add a new instruction.
  virtual void Act() = 0;

  /// Add N new instructions,
  virtual void ActN(unsigned n) {
    for (unsigned i=0; i<n; ++i)
      Act();
  }

protected:
  /// Return a random integer.
  uint32_t getRandom() {
    return Ran->Rand();
  }

  /// Return a random value from the list of known values.
  Value *getRandomVal() {
    assert(PT->size());
    return PT->at(getRandom() % PT->size());
  }

  Constant *getRandomConstant(Type *Tp) {
    if (Tp->isIntegerTy()) {
      if (getRandom() & 1)
        return ConstantInt::getAllOnesValue(Tp);
      return ConstantInt::getNullValue(Tp);
    } else if (Tp->isFloatingPointTy()) {
      if (getRandom() & 1)
        return ConstantFP::getAllOnesValue(Tp);
      return ConstantFP::getNullValue(Tp);
    }
    return UndefValue::get(Tp);
  }

  /// Return a random value with a known type.
  Value *getRandomValue(Type *Tp) {
    unsigned index = getRandom();
    for (unsigned i=0; i<PT->size(); ++i) {
      Value *V = PT->at((index + i) % PT->size());
      if (V->getType() == Tp)
        return V;
    }

    // If the requested type was not found, generate a constant value.
    if (Tp->isIntegerTy()) {
      if (getRandom() & 1)
        return ConstantInt::getAllOnesValue(Tp);
      return ConstantInt::getNullValue(Tp);
    } else if (Tp->isFloatingPointTy()) {
      if (getRandom() & 1)
        return ConstantFP::getAllOnesValue(Tp);
      return ConstantFP::getNullValue(Tp);
    } else if (Tp->isVectorTy()) {
      VectorType *VTp = cast<VectorType>(Tp);

      std::vector<Constant*> TempValues;
      TempValues.reserve(VTp->getNumElements());
      for (unsigned i = 0; i < VTp->getNumElements(); ++i)
        TempValues.push_back(getRandomConstant(VTp->getScalarType()));

      ArrayRef<Constant*> VectorValue(TempValues);
      return ConstantVector::get(VectorValue);
    }

    return UndefValue::get(Tp);
  }

  /// Return a random value of any pointer type.
  Value *getRandomPointerValue() {
    unsigned index = getRandom();
    for (unsigned i=0; i<PT->size(); ++i) {
      Value *V = PT->at((index + i) % PT->size());
      if (V->getType()->isPointerTy())
        return V;
    }
    return UndefValue::get(pickPointerType());
  }

  /// Return a random value of any vector type.
  Value *getRandomVectorValue() {
    unsigned index = getRandom();
    for (unsigned i=0; i<PT->size(); ++i) {
      Value *V = PT->at((index + i) % PT->size());
      if (V->getType()->isVectorTy())
        return V;
    }
    return UndefValue::get(pickVectorType());
  }

  /// Pick a random type.
  Type *pickType() {
    return (getRandom() & 1 ? pickVectorType() : pickScalarType());
  }

  /// Pick a random pointer type.
  Type *pickPointerType() {
    Type *Ty = pickType();
    return PointerType::get(Ty, 0);
  }

  /// Pick a random vector type.
  Type *pickVectorType(unsigned len = (unsigned)-1) {
    // Pick a random vector width in the range 2**0 to 2**4.
    // by adding two randoms we are generating a normal-like distribution
    // around 2**3.
    unsigned width = 1<<((getRandom() % 3) + (getRandom() % 3));
    Type *Ty;

    // Vectors of x86mmx are illegal; keep trying till we get something else.
    do {
      Ty = pickScalarType();
    } while (Ty->isX86_MMXTy());

    if (len != (unsigned)-1)
      width = len;
    return VectorType::get(Ty, width);
  }

  /// Pick a random scalar type.
  Type *pickScalarType() {
    static std::vector<Type*> ScalarTypes;
    if (ScalarTypes.empty()) {
      ScalarTypes.assign({
        Type::getInt1Ty(Context),
        Type::getInt8Ty(Context),
        Type::getInt16Ty(Context),
        Type::getInt32Ty(Context),
        Type::getInt64Ty(Context),
        Type::getFloatTy(Context),
        Type::getDoubleTy(Context)
      });
      ScalarTypes.insert(ScalarTypes.end(),
        AdditionalScalarTypes.begin(), AdditionalScalarTypes.end());
    }

    return ScalarTypes[getRandom() % ScalarTypes.size()];
  }

  /// Basic block to populate
  BasicBlock *BB;

  /// Value table
  PieceTable *PT;

  /// Random number generator
  Random *Ran;

  /// Context
  LLVMContext &Context;
};

struct LoadModifier: public Modifier {
  LoadModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    // Try to use predefined pointers. If non-exist, use undef pointer value;
    Value *Ptr = getRandomPointerValue();
    Value *V = new LoadInst(Ptr, "L", BB->getTerminator());
    PT->push_back(V);
  }
};

struct StoreModifier: public Modifier {
  StoreModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    // Try to use predefined pointers. If non-exist, use undef pointer value;
    Value *Ptr = getRandomPointerValue();
    PointerType *Tp = cast<PointerType>(Ptr->getType());
    Value *Val = getRandomValue(Tp->getElementType());
    Type  *ValTy = Val->getType();

    // Do not store vectors of i1s because they are unsupported
    // by the codegen.
    if (ValTy->isVectorTy() && ValTy->getScalarSizeInBits() == 1)
      return;

    new StoreInst(Val, Ptr, BB->getTerminator());
  }
};

struct BinModifier: public Modifier {
  BinModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *Val0 = getRandomVal();
    Value *Val1 = getRandomValue(Val0->getType());

    // Don't handle pointer types.
    if (Val0->getType()->isPointerTy() ||
        Val1->getType()->isPointerTy())
      return;

    // Don't handle i1 types.
    if (Val0->getType()->getScalarSizeInBits() == 1)
      return;

    bool isFloat = Val0->getType()->getScalarType()->isFloatingPointTy();
    Instruction* Term = BB->getTerminator();
    unsigned R = getRandom() % (isFloat ? 7 : 13);
    Instruction::BinaryOps Op;

    switch (R) {
    default: llvm_unreachable("Invalid BinOp");
    case 0:{Op = (isFloat?Instruction::FAdd : Instruction::Add); break; }
    case 1:{Op = (isFloat?Instruction::FSub : Instruction::Sub); break; }
    case 2:{Op = (isFloat?Instruction::FMul : Instruction::Mul); break; }
    case 3:{Op = (isFloat?Instruction::FDiv : Instruction::SDiv); break; }
    case 4:{Op = (isFloat?Instruction::FDiv : Instruction::UDiv); break; }
    case 5:{Op = (isFloat?Instruction::FRem : Instruction::SRem); break; }
    case 6:{Op = (isFloat?Instruction::FRem : Instruction::URem); break; }
    case 7: {Op = Instruction::Shl;  break; }
    case 8: {Op = Instruction::LShr; break; }
    case 9: {Op = Instruction::AShr; break; }
    case 10:{Op = Instruction::And;  break; }
    case 11:{Op = Instruction::Or;   break; }
    case 12:{Op = Instruction::Xor;  break; }
    }

    PT->push_back(BinaryOperator::Create(Op, Val0, Val1, "B", Term));
  }
};

/// Generate constant values.
struct ConstModifier: public Modifier {
  ConstModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Type *Ty = pickType();

    if (Ty->isVectorTy()) {
      switch (getRandom() % 2) {
      case 0: if (Ty->isIntOrIntVectorTy())
                return PT->push_back(ConstantVector::getAllOnesValue(Ty));
              break;
      case 1: if (Ty->isIntOrIntVectorTy())
                return PT->push_back(ConstantVector::getNullValue(Ty));
      }
    }

    if (Ty->isFloatingPointTy()) {
      // Generate 128 random bits, the size of the (currently)
      // largest floating-point types.
      uint64_t RandomBits[2];
      for (unsigned i = 0; i < 2; ++i)
        RandomBits[i] = Ran->Rand64();

      APInt RandomInt(Ty->getPrimitiveSizeInBits(), makeArrayRef(RandomBits));
      APFloat RandomFloat(Ty->getFltSemantics(), RandomInt);

      if (getRandom() & 1)
        return PT->push_back(ConstantFP::getNullValue(Ty));
      return PT->push_back(ConstantFP::get(Ty->getContext(), RandomFloat));
    }

    if (Ty->isIntegerTy()) {
      switch (getRandom() % 7) {
      case 0:
        return PT->push_back(ConstantInt::get(
            Ty, APInt::getAllOnesValue(Ty->getPrimitiveSizeInBits())));
      case 1:
        return PT->push_back(ConstantInt::get(
            Ty, APInt::getNullValue(Ty->getPrimitiveSizeInBits())));
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
        PT->push_back(ConstantInt::get(Ty, getRandom()));
      }
    }
  }
};

struct AllocaModifier: public Modifier {
  AllocaModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Type *Tp = pickType();
    const DataLayout &DL = BB->getModule()->getDataLayout();
    PT->push_back(new AllocaInst(Tp, DL.getAllocaAddrSpace(),
                                 "A", BB->getFirstNonPHI()));
  }
};

struct ExtractElementModifier: public Modifier {
  ExtractElementModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *Val0 = getRandomVectorValue();
    Value *V = ExtractElementInst::Create(Val0,
             ConstantInt::get(Type::getInt32Ty(BB->getContext()),
             getRandom() % cast<VectorType>(Val0->getType())->getNumElements()),
             "E", BB->getTerminator());
    return PT->push_back(V);
  }
};

struct ShuffModifier: public Modifier {
  ShuffModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *Val0 = getRandomVectorValue();
    Value *Val1 = getRandomValue(Val0->getType());

    unsigned Width = cast<VectorType>(Val0->getType())->getNumElements();
    std::vector<Constant*> Idxs;

    Type *I32 = Type::getInt32Ty(BB->getContext());
    for (unsigned i=0; i<Width; ++i) {
      Constant *CI = ConstantInt::get(I32, getRandom() % (Width*2));
      // Pick some undef values.
      if (!(getRandom() % 5))
        CI = UndefValue::get(I32);
      Idxs.push_back(CI);
    }

    Constant *Mask = ConstantVector::get(Idxs);

    Value *V = new ShuffleVectorInst(Val0, Val1, Mask, "Shuff",
                                     BB->getTerminator());
    PT->push_back(V);
  }
};

struct InsertElementModifier: public Modifier {
  InsertElementModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *Val0 = getRandomVectorValue();
    Value *Val1 = getRandomValue(Val0->getType()->getScalarType());

    Value *V = InsertElementInst::Create(Val0, Val1,
              ConstantInt::get(Type::getInt32Ty(BB->getContext()),
              getRandom() % cast<VectorType>(Val0->getType())->getNumElements()),
              "I",  BB->getTerminator());
    return PT->push_back(V);
  }
};

struct CastModifier: public Modifier {
  CastModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *V = getRandomVal();
    Type *VTy = V->getType();
    Type *DestTy = pickScalarType();

    // Handle vector casts vectors.
    if (VTy->isVectorTy()) {
      VectorType *VecTy = cast<VectorType>(VTy);
      DestTy = pickVectorType(VecTy->getNumElements());
    }

    // no need to cast.
    if (VTy == DestTy) return;

    // Pointers:
    if (VTy->isPointerTy()) {
      if (!DestTy->isPointerTy())
        DestTy = PointerType::get(DestTy, 0);
      return PT->push_back(
        new BitCastInst(V, DestTy, "PC", BB->getTerminator()));
    }

    unsigned VSize = VTy->getScalarType()->getPrimitiveSizeInBits();
    unsigned DestSize = DestTy->getScalarType()->getPrimitiveSizeInBits();

    // Generate lots of bitcasts.
    if ((getRandom() & 1) && VSize == DestSize) {
      return PT->push_back(
        new BitCastInst(V, DestTy, "BC", BB->getTerminator()));
    }

    // Both types are integers:
    if (VTy->isIntOrIntVectorTy() && DestTy->isIntOrIntVectorTy()) {
      if (VSize > DestSize) {
        return PT->push_back(
          new TruncInst(V, DestTy, "Tr", BB->getTerminator()));
      } else {
        assert(VSize < DestSize && "Different int types with the same size?");
        if (getRandom() & 1)
          return PT->push_back(
            new ZExtInst(V, DestTy, "ZE", BB->getTerminator()));
        return PT->push_back(new SExtInst(V, DestTy, "Se", BB->getTerminator()));
      }
    }

    // Fp to int.
    if (VTy->isFPOrFPVectorTy() && DestTy->isIntOrIntVectorTy()) {
      if (getRandom() & 1)
        return PT->push_back(
          new FPToSIInst(V, DestTy, "FC", BB->getTerminator()));
      return PT->push_back(new FPToUIInst(V, DestTy, "FC", BB->getTerminator()));
    }

    // Int to fp.
    if (VTy->isIntOrIntVectorTy() && DestTy->isFPOrFPVectorTy()) {
      if (getRandom() & 1)
        return PT->push_back(
          new SIToFPInst(V, DestTy, "FC", BB->getTerminator()));
      return PT->push_back(new UIToFPInst(V, DestTy, "FC", BB->getTerminator()));
    }

    // Both floats.
    if (VTy->isFPOrFPVectorTy() && DestTy->isFPOrFPVectorTy()) {
      if (VSize > DestSize) {
        return PT->push_back(
          new FPTruncInst(V, DestTy, "Tr", BB->getTerminator()));
      } else if (VSize < DestSize) {
        return PT->push_back(
          new FPExtInst(V, DestTy, "ZE", BB->getTerminator()));
      }
      // If VSize == DestSize, then the two types must be fp128 and ppc_fp128,
      // for which there is no defined conversion. So do nothing.
    }
  }
};

struct SelectModifier: public Modifier {
  SelectModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    // Try a bunch of different select configuration until a valid one is found.
    Value *Val0 = getRandomVal();
    Value *Val1 = getRandomValue(Val0->getType());

    Type *CondTy = Type::getInt1Ty(Context);

    // If the value type is a vector, and we allow vector select, then in 50%
    // of the cases generate a vector select.
    if (Val0->getType()->isVectorTy() && (getRandom() % 1)) {
      unsigned NumElem = cast<VectorType>(Val0->getType())->getNumElements();
      CondTy = VectorType::get(CondTy, NumElem);
    }

    Value *Cond = getRandomValue(CondTy);
    Value *V = SelectInst::Create(Cond, Val0, Val1, "Sl", BB->getTerminator());
    return PT->push_back(V);
  }
};

struct CmpModifier: public Modifier {
  CmpModifier(BasicBlock *BB, PieceTable *PT, Random *R)
      : Modifier(BB, PT, R) {}

  void Act() override {
    Value *Val0 = getRandomVal();
    Value *Val1 = getRandomValue(Val0->getType());

    if (Val0->getType()->isPointerTy()) return;
    bool fp = Val0->getType()->getScalarType()->isFloatingPointTy();

    int op;
    if (fp) {
      op = getRandom() %
      (CmpInst::LAST_FCMP_PREDICATE - CmpInst::FIRST_FCMP_PREDICATE) +
       CmpInst::FIRST_FCMP_PREDICATE;
    } else {
      op = getRandom() %
      (CmpInst::LAST_ICMP_PREDICATE - CmpInst::FIRST_ICMP_PREDICATE) +
       CmpInst::FIRST_ICMP_PREDICATE;
    }

    Value *V = CmpInst::Create(fp ? Instruction::FCmp : Instruction::ICmp,
                               (CmpInst::Predicate)op, Val0, Val1, "Cmp",
                               BB->getTerminator());
    return PT->push_back(V);
  }
};

} // end anonymous namespace

static void FillFunction(Function *F, Random &R) {
  // Create a legal entry block.
  BasicBlock *BB = BasicBlock::Create(F->getContext(), "BB", F);
  ReturnInst::Create(F->getContext(), BB);

  // Create the value table.
  Modifier::PieceTable PT;

  // Consider arguments as legal values.
  for (auto &arg : F->args())
    PT.push_back(&arg);

  // List of modifiers which add new random instructions.
  std::vector<std::unique_ptr<Modifier>> Modifiers;
  Modifiers.emplace_back(new LoadModifier(BB, &PT, &R));
  Modifiers.emplace_back(new StoreModifier(BB, &PT, &R));
  auto SM = Modifiers.back().get();
  Modifiers.emplace_back(new ExtractElementModifier(BB, &PT, &R));
  Modifiers.emplace_back(new ShuffModifier(BB, &PT, &R));
  Modifiers.emplace_back(new InsertElementModifier(BB, &PT, &R));
  Modifiers.emplace_back(new BinModifier(BB, &PT, &R));
  Modifiers.emplace_back(new CastModifier(BB, &PT, &R));
  Modifiers.emplace_back(new SelectModifier(BB, &PT, &R));
  Modifiers.emplace_back(new CmpModifier(BB, &PT, &R));

  // Generate the random instructions
  AllocaModifier{BB, &PT, &R}.ActN(5); // Throw in a few allocas
  ConstModifier{BB, &PT, &R}.ActN(40); // Throw in a few constants

  for (unsigned i = 0; i < SizeCL / Modifiers.size(); ++i)
    for (auto &Mod : Modifiers)
      Mod->Act();

  SM->ActN(5); // Throw in a few stores.
}

static void IntroduceControlFlow(Function *F, Random &R) {
  std::vector<Instruction*> BoolInst;
  for (auto &Instr : F->front()) {
    if (Instr.getType() == IntegerType::getInt1Ty(F->getContext()))
      BoolInst.push_back(&Instr);
  }

  std::shuffle(BoolInst.begin(), BoolInst.end(), R);

  for (auto *Instr : BoolInst) {
    BasicBlock *Curr = Instr->getParent();
    BasicBlock::iterator Loc = Instr->getIterator();
    BasicBlock *Next = Curr->splitBasicBlock(Loc, "CF");
    Instr->moveBefore(Curr->getTerminator());
    if (Curr != &F->getEntryBlock()) {
      BranchInst::Create(Curr, Next, Instr, Curr->getTerminator());
      Curr->getTerminator()->eraseFromParent();
    }
  }
}

} // end namespace llvm

int main(int argc, char **argv) {
  using namespace llvm;

  // Init LLVM, call llvm_shutdown() on exit, parse args, etc.
  PrettyStackTraceProgram X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "llvm codegen stress-tester\n");
  llvm_shutdown_obj Y;

  auto M = llvm::make_unique<Module>("/tmp/autogen.bc", Context);
  Function *F = GenEmptyFunction(M.get());

  // Pick an initial seed value
  Random R(SeedCL);
  // Generate lots of random instructions inside a single basic block.
  FillFunction(F, R);
  // Break the basic block into many loops.
  IntroduceControlFlow(F, R);

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<ToolOutputFile> Out;
  // Default to standard output.
  if (OutputFilename.empty())
    OutputFilename = "-";

  std::error_code EC;
  Out.reset(new ToolOutputFile(OutputFilename, EC, sys::fs::F_None));
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  legacy::PassManager Passes;
  Passes.add(createVerifierPass());
  Passes.add(createPrintModulePass(Out->os()));
  Passes.run(*M.get());
  Out->keep();

  return 0;
}
