//===---- llvm/MDBuilder.cpp - Builder for LLVM metadata ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MDBuilder class, which is used as a convenient way to
// create LLVM metadata with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
using namespace llvm;

MDString *MDBuilder::createString(StringRef Str) {
  return MDString::get(Context, Str);
}

ConstantAsMetadata *MDBuilder::createConstant(Constant *C) {
  return ConstantAsMetadata::get(C);
}

MDNode *MDBuilder::createFPMath(float Accuracy) {
  if (Accuracy == 0.0)
    return nullptr;
  assert(Accuracy > 0.0 && "Invalid fpmath accuracy!");
  auto *Op =
      createConstant(ConstantFP::get(Type::getFloatTy(Context), Accuracy));
  return MDNode::get(Context, Op);
}

MDNode *MDBuilder::createBranchWeights(uint32_t TrueWeight,
                                       uint32_t FalseWeight) {
  return createBranchWeights({TrueWeight, FalseWeight});
}

MDNode *MDBuilder::createBranchWeights(ArrayRef<uint32_t> Weights) {
  assert(Weights.size() >= 1 && "Need at least one branch weights!");

  SmallVector<Metadata *, 4> Vals(Weights.size() + 1);
  Vals[0] = createString("branch_weights");

  Type *Int32Ty = Type::getInt32Ty(Context);
  for (unsigned i = 0, e = Weights.size(); i != e; ++i)
    Vals[i + 1] = createConstant(ConstantInt::get(Int32Ty, Weights[i]));

  return MDNode::get(Context, Vals);
}

MDNode *MDBuilder::createUnpredictable() {
  return MDNode::get(Context, None);
}

MDNode *MDBuilder::createFunctionEntryCount(
    uint64_t Count, bool Synthetic,
    const DenseSet<GlobalValue::GUID> *Imports) {
  Type *Int64Ty = Type::getInt64Ty(Context);
  SmallVector<Metadata *, 8> Ops;
  if (Synthetic)
    Ops.push_back(createString("synthetic_function_entry_count"));
  else
    Ops.push_back(createString("function_entry_count"));
  Ops.push_back(createConstant(ConstantInt::get(Int64Ty, Count)));
  if (Imports) {
    SmallVector<GlobalValue::GUID, 2> OrderID(Imports->begin(), Imports->end());
    std::stable_sort(OrderID.begin(), OrderID.end(),
      [] (GlobalValue::GUID A, GlobalValue::GUID B) {
        return A < B;});
    for (auto ID : OrderID)
      Ops.push_back(createConstant(ConstantInt::get(Int64Ty, ID)));
  }
  return MDNode::get(Context, Ops);
}

MDNode *MDBuilder::createFunctionSectionPrefix(StringRef Prefix) {
  return MDNode::get(Context,
                     {createString("function_section_prefix"),
                      createString(Prefix)});
}

MDNode *MDBuilder::createRange(const APInt &Lo, const APInt &Hi) {
  assert(Lo.getBitWidth() == Hi.getBitWidth() && "Mismatched bitwidths!");

  Type *Ty = IntegerType::get(Context, Lo.getBitWidth());
  return createRange(ConstantInt::get(Ty, Lo), ConstantInt::get(Ty, Hi));
}

MDNode *MDBuilder::createRange(Constant *Lo, Constant *Hi) {
  // If the range is everything then it is useless.
  if (Hi == Lo)
    return nullptr;

  // Return the range [Lo, Hi).
  return MDNode::get(Context, {createConstant(Lo), createConstant(Hi)});
}

MDNode *MDBuilder::createCallees(ArrayRef<Function *> Callees) {
  SmallVector<Metadata *, 4> Ops;
  for (Function *F : Callees)
    Ops.push_back(createConstant(F));
  return MDNode::get(Context, Ops);
}

MDNode *MDBuilder::createAnonymousAARoot(StringRef Name, MDNode *Extra) {
  // To ensure uniqueness the root node is self-referential.
  auto Dummy = MDNode::getTemporary(Context, None);

  SmallVector<Metadata *, 3> Args(1, Dummy.get());
  if (Extra)
    Args.push_back(Extra);
  if (!Name.empty())
    Args.push_back(createString(Name));
  MDNode *Root = MDNode::get(Context, Args);

  // At this point we have
  //   !0 = metadata !{}            <- dummy
  //   !1 = metadata !{metadata !0} <- root
  // Replace the dummy operand with the root node itself and delete the dummy.
  Root->replaceOperandWith(0, Root);

  // We now have
  //   !1 = metadata !{metadata !1} <- self-referential root
  return Root;
}

MDNode *MDBuilder::createTBAARoot(StringRef Name) {
  return MDNode::get(Context, createString(Name));
}

/// Return metadata for a non-root TBAA node with the given name,
/// parent in the TBAA tree, and value for 'pointsToConstantMemory'.
MDNode *MDBuilder::createTBAANode(StringRef Name, MDNode *Parent,
                                  bool isConstant) {
  if (isConstant) {
    Constant *Flags = ConstantInt::get(Type::getInt64Ty(Context), 1);
    return MDNode::get(Context,
                       {createString(Name), Parent, createConstant(Flags)});
  }
  return MDNode::get(Context, {createString(Name), Parent});
}

MDNode *MDBuilder::createAliasScopeDomain(StringRef Name) {
  return MDNode::get(Context, createString(Name));
}

MDNode *MDBuilder::createAliasScope(StringRef Name, MDNode *Domain) {
  return MDNode::get(Context, {createString(Name), Domain});
}

/// Return metadata for a tbaa.struct node with the given
/// struct field descriptions.
MDNode *MDBuilder::createTBAAStructNode(ArrayRef<TBAAStructField> Fields) {
  SmallVector<Metadata *, 4> Vals(Fields.size() * 3);
  Type *Int64 = Type::getInt64Ty(Context);
  for (unsigned i = 0, e = Fields.size(); i != e; ++i) {
    Vals[i * 3 + 0] = createConstant(ConstantInt::get(Int64, Fields[i].Offset));
    Vals[i * 3 + 1] = createConstant(ConstantInt::get(Int64, Fields[i].Size));
    Vals[i * 3 + 2] = Fields[i].Type;
  }
  return MDNode::get(Context, Vals);
}

/// Return metadata for a TBAA struct node in the type DAG
/// with the given name, a list of pairs (offset, field type in the type DAG).
MDNode *MDBuilder::createTBAAStructTypeNode(
    StringRef Name, ArrayRef<std::pair<MDNode *, uint64_t>> Fields) {
  SmallVector<Metadata *, 4> Ops(Fields.size() * 2 + 1);
  Type *Int64 = Type::getInt64Ty(Context);
  Ops[0] = createString(Name);
  for (unsigned i = 0, e = Fields.size(); i != e; ++i) {
    Ops[i * 2 + 1] = Fields[i].first;
    Ops[i * 2 + 2] = createConstant(ConstantInt::get(Int64, Fields[i].second));
  }
  return MDNode::get(Context, Ops);
}

/// Return metadata for a TBAA scalar type node with the
/// given name, an offset and a parent in the TBAA type DAG.
MDNode *MDBuilder::createTBAAScalarTypeNode(StringRef Name, MDNode *Parent,
                                            uint64_t Offset) {
  ConstantInt *Off = ConstantInt::get(Type::getInt64Ty(Context), Offset);
  return MDNode::get(Context,
                     {createString(Name), Parent, createConstant(Off)});
}

/// Return metadata for a TBAA tag node with the given
/// base type, access type and offset relative to the base type.
MDNode *MDBuilder::createTBAAStructTagNode(MDNode *BaseType, MDNode *AccessType,
                                           uint64_t Offset, bool IsConstant) {
  IntegerType *Int64 = Type::getInt64Ty(Context);
  ConstantInt *Off = ConstantInt::get(Int64, Offset);
  if (IsConstant) {
    return MDNode::get(Context, {BaseType, AccessType, createConstant(Off),
                                 createConstant(ConstantInt::get(Int64, 1))});
  }
  return MDNode::get(Context, {BaseType, AccessType, createConstant(Off)});
}

MDNode *MDBuilder::createTBAATypeNode(MDNode *Parent, uint64_t Size,
                                      Metadata *Id,
                                      ArrayRef<TBAAStructField> Fields) {
  SmallVector<Metadata *, 4> Ops(3 + Fields.size() * 3);
  Type *Int64 = Type::getInt64Ty(Context);
  Ops[0] = Parent;
  Ops[1] = createConstant(ConstantInt::get(Int64, Size));
  Ops[2] = Id;
  for (unsigned I = 0, E = Fields.size(); I != E; ++I) {
    Ops[I * 3 + 3] = Fields[I].Type;
    Ops[I * 3 + 4] = createConstant(ConstantInt::get(Int64, Fields[I].Offset));
    Ops[I * 3 + 5] = createConstant(ConstantInt::get(Int64, Fields[I].Size));
  }
  return MDNode::get(Context, Ops);
}

MDNode *MDBuilder::createTBAAAccessTag(MDNode *BaseType, MDNode *AccessType,
                                       uint64_t Offset, uint64_t Size,
                                       bool IsImmutable) {
  IntegerType *Int64 = Type::getInt64Ty(Context);
  auto *OffsetNode = createConstant(ConstantInt::get(Int64, Offset));
  auto *SizeNode = createConstant(ConstantInt::get(Int64, Size));
  if (IsImmutable) {
    auto *ImmutabilityFlagNode = createConstant(ConstantInt::get(Int64, 1));
    return MDNode::get(Context, {BaseType, AccessType, OffsetNode, SizeNode,
                                 ImmutabilityFlagNode});
  }
  return MDNode::get(Context, {BaseType, AccessType, OffsetNode, SizeNode});
}

MDNode *MDBuilder::createMutableTBAAAccessTag(MDNode *Tag) {
  MDNode *BaseType = cast<MDNode>(Tag->getOperand(0));
  MDNode *AccessType = cast<MDNode>(Tag->getOperand(1));
  Metadata *OffsetNode = Tag->getOperand(2);
  uint64_t Offset = mdconst::extract<ConstantInt>(OffsetNode)->getZExtValue();

  bool NewFormat = isa<MDNode>(AccessType->getOperand(0));

  // See if the tag is already mutable.
  unsigned ImmutabilityFlagOp = NewFormat ? 4 : 3;
  if (Tag->getNumOperands() <= ImmutabilityFlagOp)
    return Tag;

  // If Tag is already mutable then return it.
  Metadata *ImmutabilityFlagNode = Tag->getOperand(ImmutabilityFlagOp);
  if (!mdconst::extract<ConstantInt>(ImmutabilityFlagNode)->getValue())
    return Tag;

  // Otherwise, create another node.
  if (!NewFormat)
    return createTBAAStructTagNode(BaseType, AccessType, Offset);

  Metadata *SizeNode = Tag->getOperand(3);
  uint64_t Size = mdconst::extract<ConstantInt>(SizeNode)->getZExtValue();
  return createTBAAAccessTag(BaseType, AccessType, Offset, Size);
}

MDNode *MDBuilder::createIrrLoopHeaderWeight(uint64_t Weight) {
  Metadata *Vals[] = {
    createString("loop_header_weight"),
    createConstant(ConstantInt::get(Type::getInt64Ty(Context), Weight)),
  };
  return MDNode::get(Context, Vals);
}
