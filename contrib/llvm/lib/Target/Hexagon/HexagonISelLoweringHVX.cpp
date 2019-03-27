//===-- HexagonISelLoweringHVX.cpp --- Lowering HVX operations ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "HexagonISelLowering.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static const MVT LegalV64[] =  { MVT::v64i8,  MVT::v32i16,  MVT::v16i32 };
static const MVT LegalW64[] =  { MVT::v128i8, MVT::v64i16,  MVT::v32i32 };
static const MVT LegalV128[] = { MVT::v128i8, MVT::v64i16,  MVT::v32i32 };
static const MVT LegalW128[] = { MVT::v256i8, MVT::v128i16, MVT::v64i32 };


void
HexagonTargetLowering::initializeHVXLowering() {
  if (Subtarget.useHVX64BOps()) {
    addRegisterClass(MVT::v64i8,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v32i16, &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v16i32, &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v128i8, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v64i16, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v32i32, &Hexagon::HvxWRRegClass);
    // These "short" boolean vector types should be legal because
    // they will appear as results of vector compares. If they were
    // not legal, type legalization would try to make them legal
    // and that would require using operations that do not use or
    // produce such types. That, in turn, would imply using custom
    // nodes, which would be unoptimizable by the DAG combiner.
    // The idea is to rely on target-independent operations as much
    // as possible.
    addRegisterClass(MVT::v16i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v32i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v64i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v512i1, &Hexagon::HvxQRRegClass);
  } else if (Subtarget.useHVX128BOps()) {
    addRegisterClass(MVT::v128i8,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v64i16,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v32i32,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v256i8,  &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v128i16, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v64i32,  &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v32i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v64i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v128i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v1024i1, &Hexagon::HvxQRRegClass);
  }

  // Set up operation actions.

  bool Use64b = Subtarget.useHVX64BOps();
  ArrayRef<MVT> LegalV = Use64b ? LegalV64 : LegalV128;
  ArrayRef<MVT> LegalW = Use64b ? LegalW64 : LegalW128;
  MVT ByteV = Use64b ?  MVT::v64i8 : MVT::v128i8;
  MVT ByteW = Use64b ? MVT::v128i8 : MVT::v256i8;

  auto setPromoteTo = [this] (unsigned Opc, MVT FromTy, MVT ToTy) {
    setOperationAction(Opc, FromTy, Promote);
    AddPromotedToType(Opc, FromTy, ToTy);
  };

  setOperationAction(ISD::VECTOR_SHUFFLE, ByteV, Legal);
  setOperationAction(ISD::VECTOR_SHUFFLE, ByteW, Legal);

  for (MVT T : LegalV) {
    setIndexedLoadAction(ISD::POST_INC,  T, Legal);
    setIndexedStoreAction(ISD::POST_INC, T, Legal);

    setOperationAction(ISD::AND,            T, Legal);
    setOperationAction(ISD::OR,             T, Legal);
    setOperationAction(ISD::XOR,            T, Legal);
    setOperationAction(ISD::ADD,            T, Legal);
    setOperationAction(ISD::SUB,            T, Legal);
    setOperationAction(ISD::CTPOP,          T, Legal);
    setOperationAction(ISD::CTLZ,           T, Legal);
    if (T != ByteV) {
      setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, T, Legal);
      setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, T, Legal);
      setOperationAction(ISD::BSWAP,                    T, Legal);
    }

    setOperationAction(ISD::CTTZ,               T, Custom);
    setOperationAction(ISD::LOAD,               T, Custom);
    setOperationAction(ISD::MUL,                T, Custom);
    setOperationAction(ISD::MULHS,              T, Custom);
    setOperationAction(ISD::MULHU,              T, Custom);
    setOperationAction(ISD::BUILD_VECTOR,       T, Custom);
    // Make concat-vectors custom to handle concats of more than 2 vectors.
    setOperationAction(ISD::CONCAT_VECTORS,     T, Custom);
    setOperationAction(ISD::INSERT_SUBVECTOR,   T, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT,  T, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR,  T, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, T, Custom);
    setOperationAction(ISD::ANY_EXTEND,         T, Custom);
    setOperationAction(ISD::SIGN_EXTEND,        T, Custom);
    setOperationAction(ISD::ZERO_EXTEND,        T, Custom);
    if (T != ByteV) {
      setOperationAction(ISD::ANY_EXTEND_VECTOR_INREG, T, Custom);
      // HVX only has shifts of words and halfwords.
      setOperationAction(ISD::SRA,                     T, Custom);
      setOperationAction(ISD::SHL,                     T, Custom);
      setOperationAction(ISD::SRL,                     T, Custom);

      // Promote all shuffles to operate on vectors of bytes.
      setPromoteTo(ISD::VECTOR_SHUFFLE, T, ByteV);
    }

    setCondCodeAction(ISD::SETNE,  T, Expand);
    setCondCodeAction(ISD::SETLE,  T, Expand);
    setCondCodeAction(ISD::SETGE,  T, Expand);
    setCondCodeAction(ISD::SETLT,  T, Expand);
    setCondCodeAction(ISD::SETULE, T, Expand);
    setCondCodeAction(ISD::SETUGE, T, Expand);
    setCondCodeAction(ISD::SETULT, T, Expand);
  }

  for (MVT T : LegalW) {
    // Custom-lower BUILD_VECTOR for vector pairs. The standard (target-
    // independent) handling of it would convert it to a load, which is
    // not always the optimal choice.
    setOperationAction(ISD::BUILD_VECTOR,   T, Custom);
    // Make concat-vectors custom to handle concats of more than 2 vectors.
    setOperationAction(ISD::CONCAT_VECTORS, T, Custom);

    // Custom-lower these operations for pairs. Expand them into a concat
    // of the corresponding operations on individual vectors.
    setOperationAction(ISD::ANY_EXTEND,               T, Custom);
    setOperationAction(ISD::SIGN_EXTEND,              T, Custom);
    setOperationAction(ISD::ZERO_EXTEND,              T, Custom);
    setOperationAction(ISD::SIGN_EXTEND_INREG,        T, Custom);
    setOperationAction(ISD::ANY_EXTEND_VECTOR_INREG,  T, Custom);
    setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, T, Legal);
    setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, T, Legal);

    setOperationAction(ISD::LOAD,     T, Custom);
    setOperationAction(ISD::STORE,    T, Custom);
    setOperationAction(ISD::CTLZ,     T, Custom);
    setOperationAction(ISD::CTTZ,     T, Custom);
    setOperationAction(ISD::CTPOP,    T, Custom);

    setOperationAction(ISD::ADD,      T, Legal);
    setOperationAction(ISD::SUB,      T, Legal);
    setOperationAction(ISD::MUL,      T, Custom);
    setOperationAction(ISD::MULHS,    T, Custom);
    setOperationAction(ISD::MULHU,    T, Custom);
    setOperationAction(ISD::AND,      T, Custom);
    setOperationAction(ISD::OR,       T, Custom);
    setOperationAction(ISD::XOR,      T, Custom);
    setOperationAction(ISD::SETCC,    T, Custom);
    setOperationAction(ISD::VSELECT,  T, Custom);
    if (T != ByteW) {
      setOperationAction(ISD::SRA,      T, Custom);
      setOperationAction(ISD::SHL,      T, Custom);
      setOperationAction(ISD::SRL,      T, Custom);

      // Promote all shuffles to operate on vectors of bytes.
      setPromoteTo(ISD::VECTOR_SHUFFLE, T, ByteW);
    }
  }

  // Boolean vectors.

  for (MVT T : LegalW) {
    // Boolean types for vector pairs will overlap with the boolean
    // types for single vectors, e.g.
    //   v64i8  -> v64i1 (single)
    //   v64i16 -> v64i1 (pair)
    // Set these actions first, and allow the single actions to overwrite
    // any duplicates.
    MVT BoolW = MVT::getVectorVT(MVT::i1, T.getVectorNumElements());
    setOperationAction(ISD::SETCC,              BoolW, Custom);
    setOperationAction(ISD::AND,                BoolW, Custom);
    setOperationAction(ISD::OR,                 BoolW, Custom);
    setOperationAction(ISD::XOR,                BoolW, Custom);
  }

  for (MVT T : LegalV) {
    MVT BoolV = MVT::getVectorVT(MVT::i1, T.getVectorNumElements());
    setOperationAction(ISD::BUILD_VECTOR,       BoolV, Custom);
    setOperationAction(ISD::CONCAT_VECTORS,     BoolV, Custom);
    setOperationAction(ISD::INSERT_SUBVECTOR,   BoolV, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT,  BoolV, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR,  BoolV, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, BoolV, Custom);
    setOperationAction(ISD::AND,                BoolV, Legal);
    setOperationAction(ISD::OR,                 BoolV, Legal);
    setOperationAction(ISD::XOR,                BoolV, Legal);
  }
}

SDValue
HexagonTargetLowering::getInt(unsigned IntId, MVT ResTy, ArrayRef<SDValue> Ops,
                              const SDLoc &dl, SelectionDAG &DAG) const {
  SmallVector<SDValue,4> IntOps;
  IntOps.push_back(DAG.getConstant(IntId, dl, MVT::i32));
  for (const SDValue &Op : Ops)
    IntOps.push_back(Op);
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, ResTy, IntOps);
}

MVT
HexagonTargetLowering::typeJoin(const TypePair &Tys) const {
  assert(Tys.first.getVectorElementType() == Tys.second.getVectorElementType());

  MVT ElemTy = Tys.first.getVectorElementType();
  return MVT::getVectorVT(ElemTy, Tys.first.getVectorNumElements() +
                                  Tys.second.getVectorNumElements());
}

HexagonTargetLowering::TypePair
HexagonTargetLowering::typeSplit(MVT VecTy) const {
  assert(VecTy.isVector());
  unsigned NumElem = VecTy.getVectorNumElements();
  assert((NumElem % 2) == 0 && "Expecting even-sized vector type");
  MVT HalfTy = MVT::getVectorVT(VecTy.getVectorElementType(), NumElem/2);
  return { HalfTy, HalfTy };
}

MVT
HexagonTargetLowering::typeExtElem(MVT VecTy, unsigned Factor) const {
  MVT ElemTy = VecTy.getVectorElementType();
  MVT NewElemTy = MVT::getIntegerVT(ElemTy.getSizeInBits() * Factor);
  return MVT::getVectorVT(NewElemTy, VecTy.getVectorNumElements());
}

MVT
HexagonTargetLowering::typeTruncElem(MVT VecTy, unsigned Factor) const {
  MVT ElemTy = VecTy.getVectorElementType();
  MVT NewElemTy = MVT::getIntegerVT(ElemTy.getSizeInBits() / Factor);
  return MVT::getVectorVT(NewElemTy, VecTy.getVectorNumElements());
}

SDValue
HexagonTargetLowering::opCastElem(SDValue Vec, MVT ElemTy,
                                  SelectionDAG &DAG) const {
  if (ty(Vec).getVectorElementType() == ElemTy)
    return Vec;
  MVT CastTy = tyVector(Vec.getValueType().getSimpleVT(), ElemTy);
  return DAG.getBitcast(CastTy, Vec);
}

SDValue
HexagonTargetLowering::opJoin(const VectorPair &Ops, const SDLoc &dl,
                              SelectionDAG &DAG) const {
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, typeJoin(ty(Ops)),
                     Ops.second, Ops.first);
}

HexagonTargetLowering::VectorPair
HexagonTargetLowering::opSplit(SDValue Vec, const SDLoc &dl,
                               SelectionDAG &DAG) const {
  TypePair Tys = typeSplit(ty(Vec));
  if (Vec.getOpcode() == HexagonISD::QCAT)
    return VectorPair(Vec.getOperand(0), Vec.getOperand(1));
  return DAG.SplitVector(Vec, dl, Tys.first, Tys.second);
}

bool
HexagonTargetLowering::isHvxSingleTy(MVT Ty) const {
  return Subtarget.isHVXVectorType(Ty) &&
         Ty.getSizeInBits() == 8 * Subtarget.getVectorLength();
}

bool
HexagonTargetLowering::isHvxPairTy(MVT Ty) const {
  return Subtarget.isHVXVectorType(Ty) &&
         Ty.getSizeInBits() == 16 * Subtarget.getVectorLength();
}

SDValue
HexagonTargetLowering::convertToByteIndex(SDValue ElemIdx, MVT ElemTy,
                                          SelectionDAG &DAG) const {
  if (ElemIdx.getValueType().getSimpleVT() != MVT::i32)
    ElemIdx = DAG.getBitcast(MVT::i32, ElemIdx);

  unsigned ElemWidth = ElemTy.getSizeInBits();
  if (ElemWidth == 8)
    return ElemIdx;

  unsigned L = Log2_32(ElemWidth/8);
  const SDLoc &dl(ElemIdx);
  return DAG.getNode(ISD::SHL, dl, MVT::i32,
                     {ElemIdx, DAG.getConstant(L, dl, MVT::i32)});
}

SDValue
HexagonTargetLowering::getIndexInWord32(SDValue Idx, MVT ElemTy,
                                        SelectionDAG &DAG) const {
  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  if (ElemWidth == 32)
    return Idx;

  if (ty(Idx) != MVT::i32)
    Idx = DAG.getBitcast(MVT::i32, Idx);
  const SDLoc &dl(Idx);
  SDValue Mask = DAG.getConstant(32/ElemWidth - 1, dl, MVT::i32);
  SDValue SubIdx = DAG.getNode(ISD::AND, dl, MVT::i32, {Idx, Mask});
  return SubIdx;
}

SDValue
HexagonTargetLowering::getByteShuffle(const SDLoc &dl, SDValue Op0,
                                      SDValue Op1, ArrayRef<int> Mask,
                                      SelectionDAG &DAG) const {
  MVT OpTy = ty(Op0);
  assert(OpTy == ty(Op1));

  MVT ElemTy = OpTy.getVectorElementType();
  if (ElemTy == MVT::i8)
    return DAG.getVectorShuffle(OpTy, dl, Op0, Op1, Mask);
  assert(ElemTy.getSizeInBits() >= 8);

  MVT ResTy = tyVector(OpTy, MVT::i8);
  unsigned ElemSize = ElemTy.getSizeInBits() / 8;

  SmallVector<int,128> ByteMask;
  for (int M : Mask) {
    if (M < 0) {
      for (unsigned I = 0; I != ElemSize; ++I)
        ByteMask.push_back(-1);
    } else {
      int NewM = M*ElemSize;
      for (unsigned I = 0; I != ElemSize; ++I)
        ByteMask.push_back(NewM+I);
    }
  }
  assert(ResTy.getVectorNumElements() == ByteMask.size());
  return DAG.getVectorShuffle(ResTy, dl, opCastElem(Op0, MVT::i8, DAG),
                              opCastElem(Op1, MVT::i8, DAG), ByteMask);
}

SDValue
HexagonTargetLowering::buildHvxVectorReg(ArrayRef<SDValue> Values,
                                         const SDLoc &dl, MVT VecTy,
                                         SelectionDAG &DAG) const {
  unsigned VecLen = Values.size();
  MachineFunction &MF = DAG.getMachineFunction();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();
  unsigned HwLen = Subtarget.getVectorLength();

  unsigned ElemSize = ElemWidth / 8;
  assert(ElemSize*VecLen == HwLen);
  SmallVector<SDValue,32> Words;

  if (VecTy.getVectorElementType() != MVT::i32) {
    assert((ElemSize == 1 || ElemSize == 2) && "Invalid element size");
    unsigned OpsPerWord = (ElemSize == 1) ? 4 : 2;
    MVT PartVT = MVT::getVectorVT(VecTy.getVectorElementType(), OpsPerWord);
    for (unsigned i = 0; i != VecLen; i += OpsPerWord) {
      SDValue W = buildVector32(Values.slice(i, OpsPerWord), dl, PartVT, DAG);
      Words.push_back(DAG.getBitcast(MVT::i32, W));
    }
  } else {
    Words.assign(Values.begin(), Values.end());
  }

  unsigned NumWords = Words.size();
  bool IsSplat = true, IsUndef = true;
  SDValue SplatV;
  for (unsigned i = 0; i != NumWords && IsSplat; ++i) {
    if (isUndef(Words[i]))
      continue;
    IsUndef = false;
    if (!SplatV.getNode())
      SplatV = Words[i];
    else if (SplatV != Words[i])
      IsSplat = false;
  }
  if (IsUndef)
    return DAG.getUNDEF(VecTy);
  if (IsSplat) {
    assert(SplatV.getNode());
    auto *IdxN = dyn_cast<ConstantSDNode>(SplatV.getNode());
    if (IdxN && IdxN->isNullValue())
      return getZero(dl, VecTy, DAG);
    return DAG.getNode(HexagonISD::VSPLATW, dl, VecTy, SplatV);
  }

  // Delay recognizing constant vectors until here, so that we can generate
  // a vsplat.
  SmallVector<ConstantInt*, 128> Consts(VecLen);
  bool AllConst = getBuildVectorConstInts(Values, VecTy, DAG, Consts);
  if (AllConst) {
    ArrayRef<Constant*> Tmp((Constant**)Consts.begin(),
                            (Constant**)Consts.end());
    Constant *CV = ConstantVector::get(Tmp);
    unsigned Align = HwLen;
    SDValue CP = LowerConstantPool(DAG.getConstantPool(CV, VecTy, Align), DAG);
    return DAG.getLoad(VecTy, dl, DAG.getEntryNode(), CP,
                       MachinePointerInfo::getConstantPool(MF), Align);
  }

  // A special case is a situation where the vector is built entirely from
  // elements extracted from another vector. This could be done via a shuffle
  // more efficiently, but typically, the size of the source vector will not
  // match the size of the vector being built (which precludes the use of a
  // shuffle directly).
  // This only handles a single source vector, and the vector being built
  // should be of a sub-vector type of the source vector type.
  auto IsBuildFromExtracts = [this,&Values] (SDValue &SrcVec,
                                             SmallVectorImpl<int> &SrcIdx) {
    SDValue Vec;
    for (SDValue V : Values) {
      if (isUndef(V)) {
        SrcIdx.push_back(-1);
        continue;
      }
      if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
        return false;
      // All extracts should come from the same vector.
      SDValue T = V.getOperand(0);
      if (Vec.getNode() != nullptr && T.getNode() != Vec.getNode())
        return false;
      Vec = T;
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(V.getOperand(1));
      if (C == nullptr)
        return false;
      int I = C->getSExtValue();
      assert(I >= 0 && "Negative element index");
      SrcIdx.push_back(I);
    }
    SrcVec = Vec;
    return true;
  };

  SmallVector<int,128> ExtIdx;
  SDValue ExtVec;
  if (IsBuildFromExtracts(ExtVec, ExtIdx)) {
    MVT ExtTy = ty(ExtVec);
    unsigned ExtLen = ExtTy.getVectorNumElements();
    if (ExtLen == VecLen || ExtLen == 2*VecLen) {
      // Construct a new shuffle mask that will produce a vector with the same
      // number of elements as the input vector, and such that the vector we
      // want will be the initial subvector of it.
      SmallVector<int,128> Mask;
      BitVector Used(ExtLen);

      for (int M : ExtIdx) {
        Mask.push_back(M);
        if (M >= 0)
          Used.set(M);
      }
      // Fill the rest of the mask with the unused elements of ExtVec in hopes
      // that it will result in a permutation of ExtVec's elements. It's still
      // fine if it doesn't (e.g. if undefs are present, or elements are
      // repeated), but permutations can always be done efficiently via vdelta
      // and vrdelta.
      for (unsigned I = 0; I != ExtLen; ++I) {
        if (Mask.size() == ExtLen)
          break;
        if (!Used.test(I))
          Mask.push_back(I);
      }

      SDValue S = DAG.getVectorShuffle(ExtTy, dl, ExtVec,
                                       DAG.getUNDEF(ExtTy), Mask);
      if (ExtLen == VecLen)
        return S;
      return DAG.getTargetExtractSubreg(Hexagon::vsub_lo, dl, VecTy, S);
    }
  }

  // Construct two halves in parallel, then or them together.
  assert(4*Words.size() == Subtarget.getVectorLength());
  SDValue HalfV0 = getInstr(Hexagon::V6_vd0, dl, VecTy, {}, DAG);
  SDValue HalfV1 = getInstr(Hexagon::V6_vd0, dl, VecTy, {}, DAG);
  SDValue S = DAG.getConstant(4, dl, MVT::i32);
  for (unsigned i = 0; i != NumWords/2; ++i) {
    SDValue N = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy,
                            {HalfV0, Words[i]});
    SDValue M = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy,
                            {HalfV1, Words[i+NumWords/2]});
    HalfV0 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {N, S});
    HalfV1 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {M, S});
  }

  HalfV0 = DAG.getNode(HexagonISD::VROR, dl, VecTy,
                       {HalfV0, DAG.getConstant(HwLen/2, dl, MVT::i32)});
  SDValue DstV = DAG.getNode(ISD::OR, dl, VecTy, {HalfV0, HalfV1});
  return DstV;
}

SDValue
HexagonTargetLowering::createHvxPrefixPred(SDValue PredV, const SDLoc &dl,
      unsigned BitBytes, bool ZeroFill, SelectionDAG &DAG) const {
  MVT PredTy = ty(PredV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);

  if (Subtarget.isHVXVectorType(PredTy, true)) {
    // Move the vector predicate SubV to a vector register, and scale it
    // down to match the representation (bytes per type element) that VecV
    // uses. The scaling down will pick every 2nd or 4th (every Scale-th
    // in general) element and put them at the front of the resulting
    // vector. This subvector will then be inserted into the Q2V of VecV.
    // To avoid having an operation that generates an illegal type (short
    // vector), generate a full size vector.
    //
    SDValue T = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, PredV);
    SmallVector<int,128> Mask(HwLen);
    // Scale = BitBytes(PredV) / Given BitBytes.
    unsigned Scale = HwLen / (PredTy.getVectorNumElements() * BitBytes);
    unsigned BlockLen = PredTy.getVectorNumElements() * BitBytes;

    for (unsigned i = 0; i != HwLen; ++i) {
      unsigned Num = i % Scale;
      unsigned Off = i / Scale;
      Mask[BlockLen*Num + Off] = i;
    }
    SDValue S = DAG.getVectorShuffle(ByteTy, dl, T, DAG.getUNDEF(ByteTy), Mask);
    if (!ZeroFill)
      return S;
    // Fill the bytes beyond BlockLen with 0s.
    MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
    SDValue Q = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                         {DAG.getConstant(BlockLen, dl, MVT::i32)}, DAG);
    SDValue M = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, Q);
    return DAG.getNode(ISD::AND, dl, ByteTy, S, M);
  }

  // Make sure that this is a valid scalar predicate.
  assert(PredTy == MVT::v2i1 || PredTy == MVT::v4i1 || PredTy == MVT::v8i1);

  unsigned Bytes = 8 / PredTy.getVectorNumElements();
  SmallVector<SDValue,4> Words[2];
  unsigned IdxW = 0;

  auto Lo32 = [&DAG, &dl] (SDValue P) {
    return DAG.getTargetExtractSubreg(Hexagon::isub_lo, dl, MVT::i32, P);
  };
  auto Hi32 = [&DAG, &dl] (SDValue P) {
    return DAG.getTargetExtractSubreg(Hexagon::isub_hi, dl, MVT::i32, P);
  };

  SDValue W0 = isUndef(PredV)
                  ? DAG.getUNDEF(MVT::i64)
                  : DAG.getNode(HexagonISD::P2D, dl, MVT::i64, PredV);
  Words[IdxW].push_back(Hi32(W0));
  Words[IdxW].push_back(Lo32(W0));

  while (Bytes < BitBytes) {
    IdxW ^= 1;
    Words[IdxW].clear();

    if (Bytes < 4) {
      for (const SDValue &W : Words[IdxW ^ 1]) {
        SDValue T = expandPredicate(W, dl, DAG);
        Words[IdxW].push_back(Hi32(T));
        Words[IdxW].push_back(Lo32(T));
      }
    } else {
      for (const SDValue &W : Words[IdxW ^ 1]) {
        Words[IdxW].push_back(W);
        Words[IdxW].push_back(W);
      }
    }
    Bytes *= 2;
  }

  assert(Bytes == BitBytes);

  SDValue Vec = ZeroFill ? getZero(dl, ByteTy, DAG) : DAG.getUNDEF(ByteTy);
  SDValue S4 = DAG.getConstant(HwLen-4, dl, MVT::i32);
  for (const SDValue &W : Words[IdxW]) {
    Vec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, Vec, S4);
    Vec = DAG.getNode(HexagonISD::VINSERTW0, dl, ByteTy, Vec, W);
  }

  return Vec;
}

SDValue
HexagonTargetLowering::buildHvxVectorPred(ArrayRef<SDValue> Values,
                                          const SDLoc &dl, MVT VecTy,
                                          SelectionDAG &DAG) const {
  // Construct a vector V of bytes, such that a comparison V >u 0 would
  // produce the required vector predicate.
  unsigned VecLen = Values.size();
  unsigned HwLen = Subtarget.getVectorLength();
  assert(VecLen <= HwLen || VecLen == 8*HwLen);
  SmallVector<SDValue,128> Bytes;
  bool AllT = true, AllF = true;

  auto IsTrue = [] (SDValue V) {
    if (const auto *N = dyn_cast<ConstantSDNode>(V.getNode()))
      return !N->isNullValue();
    return false;
  };
  auto IsFalse = [] (SDValue V) {
    if (const auto *N = dyn_cast<ConstantSDNode>(V.getNode()))
      return N->isNullValue();
    return false;
  };

  if (VecLen <= HwLen) {
    // In the hardware, each bit of a vector predicate corresponds to a byte
    // of a vector register. Calculate how many bytes does a bit of VecTy
    // correspond to.
    assert(HwLen % VecLen == 0);
    unsigned BitBytes = HwLen / VecLen;
    for (SDValue V : Values) {
      AllT &= IsTrue(V);
      AllF &= IsFalse(V);

      SDValue Ext = !V.isUndef() ? DAG.getZExtOrTrunc(V, dl, MVT::i8)
                                 : DAG.getUNDEF(MVT::i8);
      for (unsigned B = 0; B != BitBytes; ++B)
        Bytes.push_back(Ext);
    }
  } else {
    // There are as many i1 values, as there are bits in a vector register.
    // Divide the values into groups of 8 and check that each group consists
    // of the same value (ignoring undefs).
    for (unsigned I = 0; I != VecLen; I += 8) {
      unsigned B = 0;
      // Find the first non-undef value in this group.
      for (; B != 8; ++B) {
        if (!Values[I+B].isUndef())
          break;
      }
      SDValue F = Values[I+B];
      AllT &= IsTrue(F);
      AllF &= IsFalse(F);

      SDValue Ext = (B < 8) ? DAG.getZExtOrTrunc(F, dl, MVT::i8)
                            : DAG.getUNDEF(MVT::i8);
      Bytes.push_back(Ext);
      // Verify that the rest of values in the group are the same as the
      // first.
      for (; B != 8; ++B)
        assert(Values[I+B].isUndef() || Values[I+B] == F);
    }
  }

  if (AllT)
    return DAG.getNode(HexagonISD::QTRUE, dl, VecTy);
  if (AllF)
    return DAG.getNode(HexagonISD::QFALSE, dl, VecTy);

  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = buildHvxVectorReg(Bytes, dl, ByteTy, DAG);
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, ByteVec);
}

SDValue
HexagonTargetLowering::extractHvxElementReg(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT ElemTy = ty(VecV).getVectorElementType();

  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  (void)ElemWidth;

  SDValue ByteIdx = convertToByteIndex(IdxV, ElemTy, DAG);
  SDValue ExWord = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32,
                               {VecV, ByteIdx});
  if (ElemTy == MVT::i32)
    return ExWord;

  // Have an extracted word, need to extract the smaller element out of it.
  // 1. Extract the bits of (the original) IdxV that correspond to the index
  //    of the desired element in the 32-bit word.
  SDValue SubIdx = getIndexInWord32(IdxV, ElemTy, DAG);
  // 2. Extract the element from the word.
  SDValue ExVec = DAG.getBitcast(tyVector(ty(ExWord), ElemTy), ExWord);
  return extractVector(ExVec, SubIdx, dl, ElemTy, MVT::i32, DAG);
}

SDValue
HexagonTargetLowering::extractHvxElementPred(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  // Implement other return types if necessary.
  assert(ResTy == MVT::i1);

  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);

  unsigned Scale = HwLen / ty(VecV).getVectorNumElements();
  SDValue ScV = DAG.getConstant(Scale, dl, MVT::i32);
  IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, ScV);

  SDValue ExtB = extractHvxElementReg(ByteVec, IdxV, dl, MVT::i32, DAG);
  SDValue Zero = DAG.getTargetConstant(0, dl, MVT::i32);
  return getInstr(Hexagon::C2_cmpgtui, dl, MVT::i1, {ExtB, Zero}, DAG);
}

SDValue
HexagonTargetLowering::insertHvxElementReg(SDValue VecV, SDValue IdxV,
      SDValue ValV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT ElemTy = ty(VecV).getVectorElementType();

  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  (void)ElemWidth;

  auto InsertWord = [&DAG,&dl,this] (SDValue VecV, SDValue ValV,
                                     SDValue ByteIdxV) {
    MVT VecTy = ty(VecV);
    unsigned HwLen = Subtarget.getVectorLength();
    SDValue MaskV = DAG.getNode(ISD::AND, dl, MVT::i32,
                                {ByteIdxV, DAG.getConstant(-4, dl, MVT::i32)});
    SDValue RotV = DAG.getNode(HexagonISD::VROR, dl, VecTy, {VecV, MaskV});
    SDValue InsV = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy, {RotV, ValV});
    SDValue SubV = DAG.getNode(ISD::SUB, dl, MVT::i32,
                               {DAG.getConstant(HwLen, dl, MVT::i32), MaskV});
    SDValue TorV = DAG.getNode(HexagonISD::VROR, dl, VecTy, {InsV, SubV});
    return TorV;
  };

  SDValue ByteIdx = convertToByteIndex(IdxV, ElemTy, DAG);
  if (ElemTy == MVT::i32)
    return InsertWord(VecV, ValV, ByteIdx);

  // If this is not inserting a 32-bit word, convert it into such a thing.
  // 1. Extract the existing word from the target vector.
  SDValue WordIdx = DAG.getNode(ISD::SRL, dl, MVT::i32,
                                {ByteIdx, DAG.getConstant(2, dl, MVT::i32)});
  SDValue Ext = extractHvxElementReg(opCastElem(VecV, MVT::i32, DAG), WordIdx,
                                     dl, MVT::i32, DAG);

  // 2. Treating the extracted word as a 32-bit vector, insert the given
  //    value into it.
  SDValue SubIdx = getIndexInWord32(IdxV, ElemTy, DAG);
  MVT SubVecTy = tyVector(ty(Ext), ElemTy);
  SDValue Ins = insertVector(DAG.getBitcast(SubVecTy, Ext),
                             ValV, SubIdx, dl, ElemTy, DAG);

  // 3. Insert the 32-bit word back into the original vector.
  return InsertWord(VecV, Ins, ByteIdx);
}

SDValue
HexagonTargetLowering::insertHvxElementPred(SDValue VecV, SDValue IdxV,
      SDValue ValV, const SDLoc &dl, SelectionDAG &DAG) const {
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);

  unsigned Scale = HwLen / ty(VecV).getVectorNumElements();
  SDValue ScV = DAG.getConstant(Scale, dl, MVT::i32);
  IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, ScV);
  ValV = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i32, ValV);

  SDValue InsV = insertHvxElementReg(ByteVec, IdxV, ValV, dl, DAG);
  return DAG.getNode(HexagonISD::V2Q, dl, ty(VecV), InsV);
}

SDValue
HexagonTargetLowering::extractHvxSubvectorReg(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  unsigned HwLen = Subtarget.getVectorLength();
  unsigned Idx = cast<ConstantSDNode>(IdxV.getNode())->getZExtValue();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();

  // If the source vector is a vector pair, get the single vector containing
  // the subvector of interest. The subvector will never overlap two single
  // vectors.
  if (isHvxPairTy(VecTy)) {
    unsigned SubIdx;
    if (Idx * ElemWidth >= 8*HwLen) {
      SubIdx = Hexagon::vsub_hi;
      Idx -= VecTy.getVectorNumElements() / 2;
    } else {
      SubIdx = Hexagon::vsub_lo;
    }
    VecTy = typeSplit(VecTy).first;
    VecV = DAG.getTargetExtractSubreg(SubIdx, dl, VecTy, VecV);
    if (VecTy == ResTy)
      return VecV;
  }

  // The only meaningful subvectors of a single HVX vector are those that
  // fit in a scalar register.
  assert(ResTy.getSizeInBits() == 32 || ResTy.getSizeInBits() == 64);

  MVT WordTy = tyVector(VecTy, MVT::i32);
  SDValue WordVec = DAG.getBitcast(WordTy, VecV);
  unsigned WordIdx = (Idx*ElemWidth) / 32;

  SDValue W0Idx = DAG.getConstant(WordIdx, dl, MVT::i32);
  SDValue W0 = extractHvxElementReg(WordVec, W0Idx, dl, MVT::i32, DAG);
  if (ResTy.getSizeInBits() == 32)
    return DAG.getBitcast(ResTy, W0);

  SDValue W1Idx = DAG.getConstant(WordIdx+1, dl, MVT::i32);
  SDValue W1 = extractHvxElementReg(WordVec, W1Idx, dl, MVT::i32, DAG);
  SDValue WW = DAG.getNode(HexagonISD::COMBINE, dl, MVT::i64, {W1, W0});
  return DAG.getBitcast(ResTy, WW);
}

SDValue
HexagonTargetLowering::extractHvxSubvectorPred(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);
  // IdxV is required to be a constant.
  unsigned Idx = cast<ConstantSDNode>(IdxV.getNode())->getZExtValue();

  unsigned ResLen = ResTy.getVectorNumElements();
  unsigned BitBytes = HwLen / VecTy.getVectorNumElements();
  unsigned Offset = Idx * BitBytes;
  SDValue Undef = DAG.getUNDEF(ByteTy);
  SmallVector<int,128> Mask;

  if (Subtarget.isHVXVectorType(ResTy, true)) {
    // Converting between two vector predicates. Since the result is shorter
    // than the source, it will correspond to a vector predicate with the
    // relevant bits replicated. The replication count is the ratio of the
    // source and target vector lengths.
    unsigned Rep = VecTy.getVectorNumElements() / ResLen;
    assert(isPowerOf2_32(Rep) && HwLen % Rep == 0);
    for (unsigned i = 0; i != HwLen/Rep; ++i) {
      for (unsigned j = 0; j != Rep; ++j)
        Mask.push_back(i + Offset);
    }
    SDValue ShuffV = DAG.getVectorShuffle(ByteTy, dl, ByteVec, Undef, Mask);
    return DAG.getNode(HexagonISD::V2Q, dl, ResTy, ShuffV);
  }

  // Converting between a vector predicate and a scalar predicate. In the
  // vector predicate, a group of BitBytes bits will correspond to a single
  // i1 element of the source vector type. Those bits will all have the same
  // value. The same will be true for ByteVec, where each byte corresponds
  // to a bit in the vector predicate.
  // The algorithm is to traverse the ByteVec, going over the i1 values from
  // the source vector, and generate the corresponding representation in an
  // 8-byte vector. To avoid repeated extracts from ByteVec, shuffle the
  // elements so that the interesting 8 bytes will be in the low end of the
  // vector.
  unsigned Rep = 8 / ResLen;
  // Make sure the output fill the entire vector register, so repeat the
  // 8-byte groups as many times as necessary.
  for (unsigned r = 0; r != HwLen/ResLen; ++r) {
    // This will generate the indexes of the 8 interesting bytes.
    for (unsigned i = 0; i != ResLen; ++i) {
      for (unsigned j = 0; j != Rep; ++j)
        Mask.push_back(Offset + i*BitBytes);
    }
  }

  SDValue Zero = getZero(dl, MVT::i32, DAG);
  SDValue ShuffV = DAG.getVectorShuffle(ByteTy, dl, ByteVec, Undef, Mask);
  // Combine the two low words from ShuffV into a v8i8, and byte-compare
  // them against 0.
  SDValue W0 = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32, {ShuffV, Zero});
  SDValue W1 = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32,
                           {ShuffV, DAG.getConstant(4, dl, MVT::i32)});
  SDValue Vec64 = DAG.getNode(HexagonISD::COMBINE, dl, MVT::v8i8, {W1, W0});
  return getInstr(Hexagon::A4_vcmpbgtui, dl, ResTy,
                  {Vec64, DAG.getTargetConstant(0, dl, MVT::i32)}, DAG);
}

SDValue
HexagonTargetLowering::insertHvxSubvectorReg(SDValue VecV, SDValue SubV,
      SDValue IdxV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  MVT SubTy = ty(SubV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();

  bool IsPair = isHvxPairTy(VecTy);
  MVT SingleTy = MVT::getVectorVT(ElemTy, (8*HwLen)/ElemWidth);
  // The two single vectors that VecV consists of, if it's a pair.
  SDValue V0, V1;
  SDValue SingleV = VecV;
  SDValue PickHi;

  if (IsPair) {
    V0 = DAG.getTargetExtractSubreg(Hexagon::vsub_lo, dl, SingleTy, VecV);
    V1 = DAG.getTargetExtractSubreg(Hexagon::vsub_hi, dl, SingleTy, VecV);

    SDValue HalfV = DAG.getConstant(SingleTy.getVectorNumElements(),
                                    dl, MVT::i32);
    PickHi = DAG.getSetCC(dl, MVT::i1, IdxV, HalfV, ISD::SETUGT);
    if (isHvxSingleTy(SubTy)) {
      if (const auto *CN = dyn_cast<const ConstantSDNode>(IdxV.getNode())) {
        unsigned Idx = CN->getZExtValue();
        assert(Idx == 0 || Idx == VecTy.getVectorNumElements()/2);
        unsigned SubIdx = (Idx == 0) ? Hexagon::vsub_lo : Hexagon::vsub_hi;
        return DAG.getTargetInsertSubreg(SubIdx, dl, VecTy, VecV, SubV);
      }
      // If IdxV is not a constant, generate the two variants: with the
      // SubV as the high and as the low subregister, and select the right
      // pair based on the IdxV.
      SDValue InLo = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {SubV, V1});
      SDValue InHi = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {V0, SubV});
      return DAG.getNode(ISD::SELECT, dl, VecTy, PickHi, InHi, InLo);
    }
    // The subvector being inserted must be entirely contained in one of
    // the vectors V0 or V1. Set SingleV to the correct one, and update
    // IdxV to be the index relative to the beginning of that vector.
    SDValue S = DAG.getNode(ISD::SUB, dl, MVT::i32, IdxV, HalfV);
    IdxV = DAG.getNode(ISD::SELECT, dl, MVT::i32, PickHi, S, IdxV);
    SingleV = DAG.getNode(ISD::SELECT, dl, SingleTy, PickHi, V1, V0);
  }

  // The only meaningful subvectors of a single HVX vector are those that
  // fit in a scalar register.
  assert(SubTy.getSizeInBits() == 32 || SubTy.getSizeInBits() == 64);
  // Convert IdxV to be index in bytes.
  auto *IdxN = dyn_cast<ConstantSDNode>(IdxV.getNode());
  if (!IdxN || !IdxN->isNullValue()) {
    IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                       DAG.getConstant(ElemWidth/8, dl, MVT::i32));
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV, IdxV);
  }
  // When inserting a single word, the rotation back to the original position
  // would be by HwLen-Idx, but if two words are inserted, it will need to be
  // by (HwLen-4)-Idx.
  unsigned RolBase = HwLen;
  if (VecTy.getSizeInBits() == 32) {
    SDValue V = DAG.getBitcast(MVT::i32, SubV);
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, V);
  } else {
    SDValue V = DAG.getBitcast(MVT::i64, SubV);
    SDValue R0 = DAG.getTargetExtractSubreg(Hexagon::isub_lo, dl, MVT::i32, V);
    SDValue R1 = DAG.getTargetExtractSubreg(Hexagon::isub_hi, dl, MVT::i32, V);
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, SingleV, R0);
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV,
                          DAG.getConstant(4, dl, MVT::i32));
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, SingleV, R1);
    RolBase = HwLen-4;
  }
  // If the vector wasn't ror'ed, don't ror it back.
  if (RolBase != 4 || !IdxN || !IdxN->isNullValue()) {
    SDValue RolV = DAG.getNode(ISD::SUB, dl, MVT::i32,
                               DAG.getConstant(RolBase, dl, MVT::i32), IdxV);
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV, RolV);
  }

  if (IsPair) {
    SDValue InLo = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {SingleV, V1});
    SDValue InHi = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {V0, SingleV});
    return DAG.getNode(ISD::SELECT, dl, VecTy, PickHi, InHi, InLo);
  }
  return SingleV;
}

SDValue
HexagonTargetLowering::insertHvxSubvectorPred(SDValue VecV, SDValue SubV,
      SDValue IdxV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  MVT SubTy = ty(SubV);
  assert(Subtarget.isHVXVectorType(VecTy, true));
  // VecV is an HVX vector predicate. SubV may be either an HVX vector
  // predicate as well, or it can be a scalar predicate.

  unsigned VecLen = VecTy.getVectorNumElements();
  unsigned HwLen = Subtarget.getVectorLength();
  assert(HwLen % VecLen == 0 && "Unexpected vector type");

  unsigned Scale = VecLen / SubTy.getVectorNumElements();
  unsigned BitBytes = HwLen / VecLen;
  unsigned BlockLen = HwLen / Scale;

  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);
  SDValue ByteSub = createHvxPrefixPred(SubV, dl, BitBytes, false, DAG);
  SDValue ByteIdx;

  auto *IdxN = dyn_cast<ConstantSDNode>(IdxV.getNode());
  if (!IdxN || !IdxN->isNullValue()) {
    ByteIdx = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                          DAG.getConstant(BitBytes, dl, MVT::i32));
    ByteVec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, ByteVec, ByteIdx);
  }

  // ByteVec is the target vector VecV rotated in such a way that the
  // subvector should be inserted at index 0. Generate a predicate mask
  // and use vmux to do the insertion.
  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  SDValue Q = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                       {DAG.getConstant(BlockLen, dl, MVT::i32)}, DAG);
  ByteVec = getInstr(Hexagon::V6_vmux, dl, ByteTy, {Q, ByteSub, ByteVec}, DAG);
  // Rotate ByteVec back, and convert to a vector predicate.
  if (!IdxN || !IdxN->isNullValue()) {
    SDValue HwLenV = DAG.getConstant(HwLen, dl, MVT::i32);
    SDValue ByteXdi = DAG.getNode(ISD::SUB, dl, MVT::i32, HwLenV, ByteIdx);
    ByteVec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, ByteVec, ByteXdi);
  }
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, ByteVec);
}

SDValue
HexagonTargetLowering::extendHvxVectorPred(SDValue VecV, const SDLoc &dl,
      MVT ResTy, bool ZeroExt, SelectionDAG &DAG) const {
  // Sign- and any-extending of a vector predicate to a vector register is
  // equivalent to Q2V. For zero-extensions, generate a vmux between 0 and
  // a vector of 1s (where the 1s are of type matching the vector type).
  assert(Subtarget.isHVXVectorType(ResTy));
  if (!ZeroExt)
    return DAG.getNode(HexagonISD::Q2V, dl, ResTy, VecV);

  assert(ty(VecV).getVectorNumElements() == ResTy.getVectorNumElements());
  SDValue True = DAG.getNode(HexagonISD::VSPLAT, dl, ResTy,
                             DAG.getConstant(1, dl, MVT::i32));
  SDValue False = getZero(dl, ResTy, DAG);
  return DAG.getSelect(dl, ResTy, VecV, True, False);
}

SDValue
HexagonTargetLowering::LowerHvxBuildVector(SDValue Op, SelectionDAG &DAG)
      const {
  const SDLoc &dl(Op);
  MVT VecTy = ty(Op);

  unsigned Size = Op.getNumOperands();
  SmallVector<SDValue,128> Ops;
  for (unsigned i = 0; i != Size; ++i)
    Ops.push_back(Op.getOperand(i));

  if (VecTy.getVectorElementType() == MVT::i1)
    return buildHvxVectorPred(Ops, dl, VecTy, DAG);

  if (VecTy.getSizeInBits() == 16*Subtarget.getVectorLength()) {
    ArrayRef<SDValue> A(Ops);
    MVT SingleTy = typeSplit(VecTy).first;
    SDValue V0 = buildHvxVectorReg(A.take_front(Size/2), dl, SingleTy, DAG);
    SDValue V1 = buildHvxVectorReg(A.drop_front(Size/2), dl, SingleTy, DAG);
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, V0, V1);
  }

  return buildHvxVectorReg(Ops, dl, VecTy, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxConcatVectors(SDValue Op, SelectionDAG &DAG)
      const {
  // Vector concatenation of two integer (non-bool) vectors does not need
  // special lowering. Custom-lower concats of bool vectors and expand
  // concats of more than 2 vectors.
  MVT VecTy = ty(Op);
  const SDLoc &dl(Op);
  unsigned NumOp = Op.getNumOperands();
  if (VecTy.getVectorElementType() != MVT::i1) {
    if (NumOp == 2)
      return Op;
    // Expand the other cases into a build-vector.
    SmallVector<SDValue,8> Elems;
    for (SDValue V : Op.getNode()->ops())
      DAG.ExtractVectorElements(V, Elems);
    // A vector of i16 will be broken up into a build_vector of i16's.
    // This is a problem, since at the time of operation legalization,
    // all operations are expected to be type-legalized, and i16 is not
    // a legal type. If any of the extracted elements is not of a valid
    // type, sign-extend it to a valid one.
    for (unsigned i = 0, e = Elems.size(); i != e; ++i) {
      SDValue V = Elems[i];
      MVT Ty = ty(V);
      if (!isTypeLegal(Ty)) {
        EVT NTy = getTypeToTransformTo(*DAG.getContext(), Ty);
        if (V.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
          Elems[i] = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, NTy,
                                 DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NTy,
                                             V.getOperand(0), V.getOperand(1)),
                                 DAG.getValueType(Ty));
          continue;
        }
        // A few less complicated cases.
        if (V.getOpcode() == ISD::Constant)
          Elems[i] = DAG.getSExtOrTrunc(V, dl, NTy);
        else if (V.isUndef())
          Elems[i] = DAG.getUNDEF(NTy);
        else
          llvm_unreachable("Unexpected vector element");
      }
    }
    return DAG.getBuildVector(VecTy, dl, Elems);
  }

  assert(VecTy.getVectorElementType() == MVT::i1);
  unsigned HwLen = Subtarget.getVectorLength();
  assert(isPowerOf2_32(NumOp) && HwLen % NumOp == 0);

  SDValue Op0 = Op.getOperand(0);

  // If the operands are HVX types (i.e. not scalar predicates), then
  // defer the concatenation, and create QCAT instead.
  if (Subtarget.isHVXVectorType(ty(Op0), true)) {
    if (NumOp == 2)
      return DAG.getNode(HexagonISD::QCAT, dl, VecTy, Op0, Op.getOperand(1));

    ArrayRef<SDUse> U(Op.getNode()->ops());
    SmallVector<SDValue,4> SV(U.begin(), U.end());
    ArrayRef<SDValue> Ops(SV);

    MVT HalfTy = typeSplit(VecTy).first;
    SDValue V0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, HalfTy,
                             Ops.take_front(NumOp/2));
    SDValue V1 = DAG.getNode(ISD::CONCAT_VECTORS, dl, HalfTy,
                             Ops.take_back(NumOp/2));
    return DAG.getNode(HexagonISD::QCAT, dl, VecTy, V0, V1);
  }

  // Count how many bytes (in a vector register) each bit in VecTy
  // corresponds to.
  unsigned BitBytes = HwLen / VecTy.getVectorNumElements();

  SmallVector<SDValue,8> Prefixes;
  for (SDValue V : Op.getNode()->op_values()) {
    SDValue P = createHvxPrefixPred(V, dl, BitBytes, true, DAG);
    Prefixes.push_back(P);
  }

  unsigned InpLen = ty(Op.getOperand(0)).getVectorNumElements();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue S = DAG.getConstant(InpLen*BitBytes, dl, MVT::i32);
  SDValue Res = getZero(dl, ByteTy, DAG);
  for (unsigned i = 0, e = Prefixes.size(); i != e; ++i) {
    Res = DAG.getNode(HexagonISD::VROR, dl, ByteTy, Res, S);
    Res = DAG.getNode(ISD::OR, dl, ByteTy, Res, Prefixes[e-i-1]);
  }
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, Res);
}

SDValue
HexagonTargetLowering::LowerHvxExtractElement(SDValue Op, SelectionDAG &DAG)
      const {
  // Change the type of the extracted element to i32.
  SDValue VecV = Op.getOperand(0);
  MVT ElemTy = ty(VecV).getVectorElementType();
  const SDLoc &dl(Op);
  SDValue IdxV = Op.getOperand(1);
  if (ElemTy == MVT::i1)
    return extractHvxElementPred(VecV, IdxV, dl, ty(Op), DAG);

  return extractHvxElementReg(VecV, IdxV, dl, ty(Op), DAG);
}

SDValue
HexagonTargetLowering::LowerHvxInsertElement(SDValue Op, SelectionDAG &DAG)
      const {
  const SDLoc &dl(Op);
  SDValue VecV = Op.getOperand(0);
  SDValue ValV = Op.getOperand(1);
  SDValue IdxV = Op.getOperand(2);
  MVT ElemTy = ty(VecV).getVectorElementType();
  if (ElemTy == MVT::i1)
    return insertHvxElementPred(VecV, IdxV, ValV, dl, DAG);

  return insertHvxElementReg(VecV, IdxV, ValV, dl, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxExtractSubvector(SDValue Op, SelectionDAG &DAG)
      const {
  SDValue SrcV = Op.getOperand(0);
  MVT SrcTy = ty(SrcV);
  MVT DstTy = ty(Op);
  SDValue IdxV = Op.getOperand(1);
  unsigned Idx = cast<ConstantSDNode>(IdxV.getNode())->getZExtValue();
  assert(Idx % DstTy.getVectorNumElements() == 0);
  (void)Idx;
  const SDLoc &dl(Op);

  MVT ElemTy = SrcTy.getVectorElementType();
  if (ElemTy == MVT::i1)
    return extractHvxSubvectorPred(SrcV, IdxV, dl, DstTy, DAG);

  return extractHvxSubvectorReg(SrcV, IdxV, dl, DstTy, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxInsertSubvector(SDValue Op, SelectionDAG &DAG)
      const {
  // Idx does not need to be a constant.
  SDValue VecV = Op.getOperand(0);
  SDValue ValV = Op.getOperand(1);
  SDValue IdxV = Op.getOperand(2);

  const SDLoc &dl(Op);
  MVT VecTy = ty(VecV);
  MVT ElemTy = VecTy.getVectorElementType();
  if (ElemTy == MVT::i1)
    return insertHvxSubvectorPred(VecV, ValV, IdxV, dl, DAG);

  return insertHvxSubvectorReg(VecV, ValV, IdxV, dl, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxAnyExt(SDValue Op, SelectionDAG &DAG) const {
  // Lower any-extends of boolean vectors to sign-extends, since they
  // translate directly to Q2V. Zero-extending could also be done equally
  // fast, but Q2V is used/recognized in more places.
  // For all other vectors, use zero-extend.
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return LowerHvxSignExt(Op, DAG);
  return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(Op), ResTy, InpV);
}

SDValue
HexagonTargetLowering::LowerHvxSignExt(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return extendHvxVectorPred(InpV, SDLoc(Op), ty(Op), false, DAG);
  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxZeroExt(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return extendHvxVectorPred(InpV, SDLoc(Op), ty(Op), true, DAG);
  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxCttz(SDValue Op, SelectionDAG &DAG) const {
  // Lower vector CTTZ into a computation using CTLZ (Hacker's Delight):
  // cttz(x) = bitwidth(x) - ctlz(~x & (x-1))
  const SDLoc &dl(Op);
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  assert(ResTy == ty(InpV));

  // Calculate the vectors of 1 and bitwidth(x).
  MVT ElemTy = ty(InpV).getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();
  // Using uint64_t because a shift by 32 can happen.
  uint64_t Splat1 = 0, SplatW = 0;
  assert(isPowerOf2_32(ElemWidth) && ElemWidth <= 32);
  for (unsigned i = 0; i != 32/ElemWidth; ++i) {
    Splat1 = (Splat1 << ElemWidth) | 1;
    SplatW = (SplatW << ElemWidth) | ElemWidth;
  }
  SDValue Vec1 = DAG.getNode(HexagonISD::VSPLATW, dl, ResTy,
                             DAG.getConstant(uint32_t(Splat1), dl, MVT::i32));
  SDValue VecW = DAG.getNode(HexagonISD::VSPLATW, dl, ResTy,
                             DAG.getConstant(uint32_t(SplatW), dl, MVT::i32));
  SDValue VecN1 = DAG.getNode(HexagonISD::VSPLATW, dl, ResTy,
                              DAG.getConstant(-1, dl, MVT::i32));
  // Do not use DAG.getNOT, because that would create BUILD_VECTOR with
  // a BITCAST. Here we can skip the BITCAST (so we don't have to handle
  // it separately in custom combine or selection).
  SDValue A = DAG.getNode(ISD::AND, dl, ResTy,
                          {DAG.getNode(ISD::XOR, dl, ResTy, {InpV, VecN1}),
                           DAG.getNode(ISD::SUB, dl, ResTy, {InpV, Vec1})});
  return DAG.getNode(ISD::SUB, dl, ResTy,
                     {VecW, DAG.getNode(ISD::CTLZ, dl, ResTy, A)});
}

SDValue
HexagonTargetLowering::LowerHvxMul(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  assert(ResTy.isVector() && isHvxSingleTy(ResTy));
  const SDLoc &dl(Op);
  SmallVector<int,256> ShuffMask;

  MVT ElemTy = ResTy.getVectorElementType();
  unsigned VecLen = ResTy.getVectorNumElements();
  SDValue Vs = Op.getOperand(0);
  SDValue Vt = Op.getOperand(1);

  switch (ElemTy.SimpleTy) {
    case MVT::i8: {
      // For i8 vectors Vs = (a0, a1, ...), Vt = (b0, b1, ...),
      // V6_vmpybv Vs, Vt produces a pair of i16 vectors Hi:Lo,
      // where Lo = (a0*b0, a2*b2, ...), Hi = (a1*b1, a3*b3, ...).
      MVT ExtTy = typeExtElem(ResTy, 2);
      unsigned MpyOpc = ElemTy == MVT::i8 ? Hexagon::V6_vmpybv
                                          : Hexagon::V6_vmpyhv;
      SDValue M = getInstr(MpyOpc, dl, ExtTy, {Vs, Vt}, DAG);

      // Discard high halves of the resulting values, collect the low halves.
      for (unsigned I = 0; I < VecLen; I += 2) {
        ShuffMask.push_back(I);         // Pick even element.
        ShuffMask.push_back(I+VecLen);  // Pick odd element.
      }
      VectorPair P = opSplit(opCastElem(M, ElemTy, DAG), dl, DAG);
      SDValue BS = getByteShuffle(dl, P.first, P.second, ShuffMask, DAG);
      return DAG.getBitcast(ResTy, BS);
    }
    case MVT::i16:
      // For i16 there is V6_vmpyih, which acts exactly like the MUL opcode.
      // (There is also V6_vmpyhv, which behaves in an analogous way to
      // V6_vmpybv.)
      return getInstr(Hexagon::V6_vmpyih, dl, ResTy, {Vs, Vt}, DAG);
    case MVT::i32: {
      // Use the following sequence for signed word multiply:
      // T0 = V6_vmpyiowh Vs, Vt
      // T1 = V6_vaslw T0, 16
      // T2 = V6_vmpyiewuh_acc T1, Vs, Vt
      SDValue S16 = DAG.getConstant(16, dl, MVT::i32);
      SDValue T0 = getInstr(Hexagon::V6_vmpyiowh, dl, ResTy, {Vs, Vt}, DAG);
      SDValue T1 = getInstr(Hexagon::V6_vaslw, dl, ResTy, {T0, S16}, DAG);
      SDValue T2 = getInstr(Hexagon::V6_vmpyiewuh_acc, dl, ResTy,
                            {T1, Vs, Vt}, DAG);
      return T2;
    }
    default:
      break;
  }
  return SDValue();
}

SDValue
HexagonTargetLowering::LowerHvxMulh(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  assert(ResTy.isVector());
  const SDLoc &dl(Op);
  SmallVector<int,256> ShuffMask;

  MVT ElemTy = ResTy.getVectorElementType();
  unsigned VecLen = ResTy.getVectorNumElements();
  SDValue Vs = Op.getOperand(0);
  SDValue Vt = Op.getOperand(1);
  bool IsSigned = Op.getOpcode() == ISD::MULHS;

  if (ElemTy == MVT::i8 || ElemTy == MVT::i16) {
    // For i8 vectors Vs = (a0, a1, ...), Vt = (b0, b1, ...),
    // V6_vmpybv Vs, Vt produces a pair of i16 vectors Hi:Lo,
    // where Lo = (a0*b0, a2*b2, ...), Hi = (a1*b1, a3*b3, ...).
    // For i16, use V6_vmpyhv, which behaves in an analogous way to
    // V6_vmpybv: results Lo and Hi are products of even/odd elements
    // respectively.
    MVT ExtTy = typeExtElem(ResTy, 2);
    unsigned MpyOpc = ElemTy == MVT::i8
        ? (IsSigned ? Hexagon::V6_vmpybv : Hexagon::V6_vmpyubv)
        : (IsSigned ? Hexagon::V6_vmpyhv : Hexagon::V6_vmpyuhv);
    SDValue M = getInstr(MpyOpc, dl, ExtTy, {Vs, Vt}, DAG);

    // Discard low halves of the resulting values, collect the high halves.
    for (unsigned I = 0; I < VecLen; I += 2) {
      ShuffMask.push_back(I+1);         // Pick even element.
      ShuffMask.push_back(I+VecLen+1);  // Pick odd element.
    }
    VectorPair P = opSplit(opCastElem(M, ElemTy, DAG), dl, DAG);
    SDValue BS = getByteShuffle(dl, P.first, P.second, ShuffMask, DAG);
    return DAG.getBitcast(ResTy, BS);
  }

  assert(ElemTy == MVT::i32);
  SDValue S16 = DAG.getConstant(16, dl, MVT::i32);

  if (IsSigned) {
    // mulhs(Vs,Vt) =
    //   = [(Hi(Vs)*2^16 + Lo(Vs)) *s (Hi(Vt)*2^16 + Lo(Vt))] >> 32
    //   = [Hi(Vs)*2^16 *s Hi(Vt)*2^16 + Hi(Vs) *su Lo(Vt)*2^16
    //      + Lo(Vs) *us (Hi(Vt)*2^16 + Lo(Vt))] >> 32
    //   = [Hi(Vs) *s Hi(Vt)*2^32 + Hi(Vs) *su Lo(Vt)*2^16
    //      + Lo(Vs) *us Vt] >> 32
    // The low half of Lo(Vs)*Lo(Vt) will be discarded (it's not added to
    // anything, so it cannot produce any carry over to higher bits),
    // so everything in [] can be shifted by 16 without loss of precision.
    //   = [Hi(Vs) *s Hi(Vt)*2^16 + Hi(Vs)*su Lo(Vt) + Lo(Vs)*Vt >> 16] >> 16
    //   = [Hi(Vs) *s Hi(Vt)*2^16 + Hi(Vs)*su Lo(Vt) + V6_vmpyewuh(Vs,Vt)] >> 16
    // Denote Hi(Vs) = Vs':
    //   = [Vs'*s Hi(Vt)*2^16 + Vs' *su Lo(Vt) + V6_vmpyewuh(Vt,Vs)] >> 16
    //   = Vs'*s Hi(Vt) + (V6_vmpyiewuh(Vs',Vt) + V6_vmpyewuh(Vt,Vs)) >> 16
    SDValue T0 = getInstr(Hexagon::V6_vmpyewuh, dl, ResTy, {Vt, Vs}, DAG);
    // Get Vs':
    SDValue S0 = getInstr(Hexagon::V6_vasrw, dl, ResTy, {Vs, S16}, DAG);
    SDValue T1 = getInstr(Hexagon::V6_vmpyiewuh_acc, dl, ResTy,
                          {T0, S0, Vt}, DAG);
    // Shift by 16:
    SDValue S2 = getInstr(Hexagon::V6_vasrw, dl, ResTy, {T1, S16}, DAG);
    // Get Vs'*Hi(Vt):
    SDValue T2 = getInstr(Hexagon::V6_vmpyiowh, dl, ResTy, {S0, Vt}, DAG);
    // Add:
    SDValue T3 = DAG.getNode(ISD::ADD, dl, ResTy, {S2, T2});
    return T3;
  }

  // Unsigned mulhw. (Would expansion using signed mulhw be better?)

  auto LoVec = [&DAG,ResTy,dl] (SDValue Pair) {
    return DAG.getTargetExtractSubreg(Hexagon::vsub_lo, dl, ResTy, Pair);
  };
  auto HiVec = [&DAG,ResTy,dl] (SDValue Pair) {
    return DAG.getTargetExtractSubreg(Hexagon::vsub_hi, dl, ResTy, Pair);
  };

  MVT PairTy = typeJoin({ResTy, ResTy});
  SDValue P = getInstr(Hexagon::V6_lvsplatw, dl, ResTy,
                       {DAG.getConstant(0x02020202, dl, MVT::i32)}, DAG);
  // Multiply-unsigned halfwords:
  //   LoVec = Vs.uh[2i] * Vt.uh[2i],
  //   HiVec = Vs.uh[2i+1] * Vt.uh[2i+1]
  SDValue T0 = getInstr(Hexagon::V6_vmpyuhv, dl, PairTy, {Vs, Vt}, DAG);
  // The low halves in the LoVec of the pair can be discarded. They are
  // not added to anything (in the full-precision product), so they cannot
  // produce a carry into the higher bits.
  SDValue T1 = getInstr(Hexagon::V6_vlsrw, dl, ResTy, {LoVec(T0), S16}, DAG);
  // Swap low and high halves in Vt, and do the halfword multiplication
  // to get products Vs.uh[2i] * Vt.uh[2i+1] and Vs.uh[2i+1] * Vt.uh[2i].
  SDValue D0 = getInstr(Hexagon::V6_vdelta, dl, ResTy, {Vt, P}, DAG);
  SDValue T2 = getInstr(Hexagon::V6_vmpyuhv, dl, PairTy, {Vs, D0}, DAG);
  // T2 has mixed products of halfwords: Lo(Vt)*Hi(Vs) and Hi(Vt)*Lo(Vs).
  // These products are words, but cannot be added directly because the
  // sums could overflow. Add these products, by halfwords, where each sum
  // of a pair of halfwords gives a word.
  SDValue T3 = getInstr(Hexagon::V6_vadduhw, dl, PairTy,
                        {LoVec(T2), HiVec(T2)}, DAG);
  // Add the high halfwords from the products of the low halfwords.
  SDValue T4 = DAG.getNode(ISD::ADD, dl, ResTy, {T1, LoVec(T3)});
  SDValue T5 = getInstr(Hexagon::V6_vlsrw, dl, ResTy, {T4, S16}, DAG);
  SDValue T6 = DAG.getNode(ISD::ADD, dl, ResTy, {HiVec(T0), HiVec(T3)});
  SDValue T7 = DAG.getNode(ISD::ADD, dl, ResTy, {T5, T6});
  return T7;
}

SDValue
HexagonTargetLowering::LowerHvxExtend(SDValue Op, SelectionDAG &DAG) const {
  // Sign- and zero-extends are legal.
  assert(Op.getOpcode() == ISD::ANY_EXTEND_VECTOR_INREG);
  return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, SDLoc(Op), ty(Op),
                     Op.getOperand(0));
}

SDValue
HexagonTargetLowering::LowerHvxShift(SDValue Op, SelectionDAG &DAG) const {
  if (SDValue S = getVectorShiftByInt(Op, DAG))
    return S;
  return Op;
}

SDValue
HexagonTargetLowering::SplitHvxPairOp(SDValue Op, SelectionDAG &DAG) const {
  assert(!Op.isMachineOpcode());
  SmallVector<SDValue,2> OpsL, OpsH;
  const SDLoc &dl(Op);

  auto SplitVTNode = [&DAG,this] (const VTSDNode *N) {
    MVT Ty = typeSplit(N->getVT().getSimpleVT()).first;
    SDValue TV = DAG.getValueType(Ty);
    return std::make_pair(TV, TV);
  };

  for (SDValue A : Op.getNode()->ops()) {
    VectorPair P = Subtarget.isHVXVectorType(ty(A), true)
                    ? opSplit(A, dl, DAG)
                    : std::make_pair(A, A);
    // Special case for type operand.
    if (Op.getOpcode() == ISD::SIGN_EXTEND_INREG) {
      if (const auto *N = dyn_cast<const VTSDNode>(A.getNode()))
        P = SplitVTNode(N);
    }
    OpsL.push_back(P.first);
    OpsH.push_back(P.second);
  }

  MVT ResTy = ty(Op);
  MVT HalfTy = typeSplit(ResTy).first;
  SDValue L = DAG.getNode(Op.getOpcode(), dl, HalfTy, OpsL);
  SDValue H = DAG.getNode(Op.getOpcode(), dl, HalfTy, OpsH);
  SDValue S = DAG.getNode(ISD::CONCAT_VECTORS, dl, ResTy, L, H);
  return S;
}

SDValue
HexagonTargetLowering::SplitHvxMemOp(SDValue Op, SelectionDAG &DAG) const {
  LSBaseSDNode *BN = cast<LSBaseSDNode>(Op.getNode());
  assert(BN->isUnindexed());
  MVT MemTy = BN->getMemoryVT().getSimpleVT();
  if (!isHvxPairTy(MemTy))
    return Op;

  const SDLoc &dl(Op);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT SingleTy = typeSplit(MemTy).first;
  SDValue Chain = BN->getChain();
  SDValue Base0 = BN->getBasePtr();
  SDValue Base1 = DAG.getMemBasePlusOffset(Base0, HwLen, dl);

  MachineMemOperand *MOp0 = nullptr, *MOp1 = nullptr;
  if (MachineMemOperand *MMO = BN->getMemOperand()) {
    MachineFunction &MF = DAG.getMachineFunction();
    MOp0 = MF.getMachineMemOperand(MMO, 0, HwLen);
    MOp1 = MF.getMachineMemOperand(MMO, HwLen, HwLen);
  }

  unsigned MemOpc = BN->getOpcode();
  SDValue NewOp;

  if (MemOpc == ISD::LOAD) {
    SDValue Load0 = DAG.getLoad(SingleTy, dl, Chain, Base0, MOp0);
    SDValue Load1 = DAG.getLoad(SingleTy, dl, Chain, Base1, MOp1);
    NewOp = DAG.getMergeValues(
              { DAG.getNode(ISD::CONCAT_VECTORS, dl, MemTy, Load0, Load1),
                DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                            Load0.getValue(1), Load1.getValue(1)) }, dl);
  } else {
    assert(MemOpc == ISD::STORE);
    VectorPair Vals = opSplit(cast<StoreSDNode>(Op)->getValue(), dl, DAG);
    SDValue Store0 = DAG.getStore(Chain, dl, Vals.first, Base0, MOp0);
    SDValue Store1 = DAG.getStore(Chain, dl, Vals.second, Base1, MOp1);
    NewOp = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Store0, Store1);
  }

  return NewOp;
}

SDValue
HexagonTargetLowering::LowerHvxOperation(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  bool IsPairOp = isHvxPairTy(ty(Op)) ||
                  llvm::any_of(Op.getNode()->ops(), [this] (SDValue V) {
                    return isHvxPairTy(ty(V));
                  });

  if (IsPairOp) {
    switch (Opc) {
      default:
        break;
      case ISD::LOAD:
      case ISD::STORE:
        return SplitHvxMemOp(Op, DAG);
      case ISD::CTPOP:
      case ISD::CTLZ:
      case ISD::CTTZ:
      case ISD::MUL:
      case ISD::MULHS:
      case ISD::MULHU:
      case ISD::AND:
      case ISD::OR:
      case ISD::XOR:
      case ISD::SRA:
      case ISD::SHL:
      case ISD::SRL:
      case ISD::SETCC:
      case ISD::VSELECT:
      case ISD::SIGN_EXTEND_INREG:
        return SplitHvxPairOp(Op, DAG);
    }
  }

  switch (Opc) {
    default:
      break;
    case ISD::BUILD_VECTOR:            return LowerHvxBuildVector(Op, DAG);
    case ISD::CONCAT_VECTORS:          return LowerHvxConcatVectors(Op, DAG);
    case ISD::INSERT_SUBVECTOR:        return LowerHvxInsertSubvector(Op, DAG);
    case ISD::INSERT_VECTOR_ELT:       return LowerHvxInsertElement(Op, DAG);
    case ISD::EXTRACT_SUBVECTOR:       return LowerHvxExtractSubvector(Op, DAG);
    case ISD::EXTRACT_VECTOR_ELT:      return LowerHvxExtractElement(Op, DAG);

    case ISD::ANY_EXTEND:              return LowerHvxAnyExt(Op, DAG);
    case ISD::SIGN_EXTEND:             return LowerHvxSignExt(Op, DAG);
    case ISD::ZERO_EXTEND:             return LowerHvxZeroExt(Op, DAG);
    case ISD::CTTZ:                    return LowerHvxCttz(Op, DAG);
    case ISD::SRA:
    case ISD::SHL:
    case ISD::SRL:                     return LowerHvxShift(Op, DAG);
    case ISD::MUL:                     return LowerHvxMul(Op, DAG);
    case ISD::MULHS:
    case ISD::MULHU:                   return LowerHvxMulh(Op, DAG);
    case ISD::ANY_EXTEND_VECTOR_INREG: return LowerHvxExtend(Op, DAG);
    case ISD::SETCC:
    case ISD::INTRINSIC_VOID:          return Op;
    // Unaligned loads will be handled by the default lowering.
    case ISD::LOAD:                    return SDValue();
  }
#ifndef NDEBUG
  Op.dumpr(&DAG);
#endif
  llvm_unreachable("Unhandled HVX operation");
}

bool
HexagonTargetLowering::isHvxOperation(SDValue Op) const {
  // If the type of the result, or any operand type are HVX vector types,
  // this is an HVX operation.
  return Subtarget.isHVXVectorType(ty(Op), true) ||
         llvm::any_of(Op.getNode()->ops(),
                      [this] (SDValue V) {
                        return Subtarget.isHVXVectorType(ty(V), true);
                      });
}
