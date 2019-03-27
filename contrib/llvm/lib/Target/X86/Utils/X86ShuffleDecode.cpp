//===-- X86ShuffleDecode.cpp - X86 shuffle decode logic -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Define several functions to decode x86 specific shuffle semantics into a
// generic vector mask.
//
//===----------------------------------------------------------------------===//

#include "X86ShuffleDecode.h"
#include "llvm/ADT/ArrayRef.h"

//===----------------------------------------------------------------------===//
//  Vector Mask Decoding
//===----------------------------------------------------------------------===//

namespace llvm {

void DecodeINSERTPSMask(unsigned Imm, SmallVectorImpl<int> &ShuffleMask) {
  // Defaults the copying the dest value.
  ShuffleMask.push_back(0);
  ShuffleMask.push_back(1);
  ShuffleMask.push_back(2);
  ShuffleMask.push_back(3);

  // Decode the immediate.
  unsigned ZMask = Imm & 15;
  unsigned CountD = (Imm >> 4) & 3;
  unsigned CountS = (Imm >> 6) & 3;

  // CountS selects which input element to use.
  unsigned InVal = 4 + CountS;
  // CountD specifies which element of destination to update.
  ShuffleMask[CountD] = InVal;
  // ZMask zaps values, potentially overriding the CountD elt.
  if (ZMask & 1) ShuffleMask[0] = SM_SentinelZero;
  if (ZMask & 2) ShuffleMask[1] = SM_SentinelZero;
  if (ZMask & 4) ShuffleMask[2] = SM_SentinelZero;
  if (ZMask & 8) ShuffleMask[3] = SM_SentinelZero;
}

void DecodeInsertElementMask(unsigned NumElts, unsigned Idx, unsigned Len,
                             SmallVectorImpl<int> &ShuffleMask) {
  assert((Idx + Len) <= NumElts && "Insertion out of range");

  for (unsigned i = 0; i != NumElts; ++i)
    ShuffleMask.push_back(i);
  for (unsigned i = 0; i != Len; ++i)
    ShuffleMask[Idx + i] = NumElts + i;
}

// <3,1> or <6,7,2,3>
void DecodeMOVHLPSMask(unsigned NElts, SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned i = NElts / 2; i != NElts; ++i)
    ShuffleMask.push_back(NElts + i);

  for (unsigned i = NElts / 2; i != NElts; ++i)
    ShuffleMask.push_back(i);
}

// <0,2> or <0,1,4,5>
void DecodeMOVLHPSMask(unsigned NElts, SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned i = 0; i != NElts / 2; ++i)
    ShuffleMask.push_back(i);

  for (unsigned i = 0; i != NElts / 2; ++i)
    ShuffleMask.push_back(NElts + i);
}

void DecodeMOVSLDUPMask(unsigned NumElts, SmallVectorImpl<int> &ShuffleMask) {
  for (int i = 0, e = NumElts / 2; i < e; ++i) {
    ShuffleMask.push_back(2 * i);
    ShuffleMask.push_back(2 * i);
  }
}

void DecodeMOVSHDUPMask(unsigned NumElts, SmallVectorImpl<int> &ShuffleMask) {
  for (int i = 0, e = NumElts / 2; i < e; ++i) {
    ShuffleMask.push_back(2 * i + 1);
    ShuffleMask.push_back(2 * i + 1);
  }
}

void DecodeMOVDDUPMask(unsigned NumElts, SmallVectorImpl<int> &ShuffleMask) {
  const unsigned NumLaneElts = 2;

  for (unsigned l = 0; l < NumElts; l += NumLaneElts)
    for (unsigned i = 0; i < NumLaneElts; ++i)
      ShuffleMask.push_back(l);
}

void DecodePSLLDQMask(unsigned NumElts, unsigned Imm,
                      SmallVectorImpl<int> &ShuffleMask) {
  const unsigned NumLaneElts = 16;

  for (unsigned l = 0; l < NumElts; l += NumLaneElts)
    for (unsigned i = 0; i < NumLaneElts; ++i) {
      int M = SM_SentinelZero;
      if (i >= Imm) M = i - Imm + l;
      ShuffleMask.push_back(M);
    }
}

void DecodePSRLDQMask(unsigned NumElts, unsigned Imm,
                      SmallVectorImpl<int> &ShuffleMask) {
  const unsigned NumLaneElts = 16;

  for (unsigned l = 0; l < NumElts; l += NumLaneElts)
    for (unsigned i = 0; i < NumLaneElts; ++i) {
      unsigned Base = i + Imm;
      int M = Base + l;
      if (Base >= NumLaneElts) M = SM_SentinelZero;
      ShuffleMask.push_back(M);
    }
}

void DecodePALIGNRMask(unsigned NumElts, unsigned Imm,
                       SmallVectorImpl<int> &ShuffleMask) {
  const unsigned NumLaneElts = 16;

  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    for (unsigned i = 0; i != NumLaneElts; ++i) {
      unsigned Base = i + Imm;
      // if i+imm is out of this lane then we actually need the other source
      if (Base >= NumLaneElts) Base += NumElts - NumLaneElts;
      ShuffleMask.push_back(Base + l);
    }
  }
}

void DecodeVALIGNMask(unsigned NumElts, unsigned Imm,
                      SmallVectorImpl<int> &ShuffleMask) {
  // Not all bits of the immediate are used so mask it.
  assert(isPowerOf2_32(NumElts) && "NumElts should be power of 2");
  Imm = Imm & (NumElts - 1);
  for (unsigned i = 0; i != NumElts; ++i)
    ShuffleMask.push_back(i + Imm);
}

/// DecodePSHUFMask - This decodes the shuffle masks for pshufw, pshufd, and vpermilp*.
/// VT indicates the type of the vector allowing it to handle different
/// datatypes and vector widths.
void DecodePSHUFMask(unsigned NumElts, unsigned ScalarBits, unsigned Imm,
                     SmallVectorImpl<int> &ShuffleMask) {
  unsigned Size = NumElts * ScalarBits;
  unsigned NumLanes = Size / 128;
  if (NumLanes == 0) NumLanes = 1;  // Handle MMX
  unsigned NumLaneElts = NumElts / NumLanes;

  uint32_t SplatImm = (Imm & 0xff) * 0x01010101;
  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    for (unsigned i = 0; i != NumLaneElts; ++i) {
      ShuffleMask.push_back(SplatImm % NumLaneElts + l);
      SplatImm /= NumLaneElts;
    }
  }
}

void DecodePSHUFHWMask(unsigned NumElts, unsigned Imm,
                       SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned l = 0; l != NumElts; l += 8) {
    unsigned NewImm = Imm;
    for (unsigned i = 0, e = 4; i != e; ++i) {
      ShuffleMask.push_back(l + i);
    }
    for (unsigned i = 4, e = 8; i != e; ++i) {
      ShuffleMask.push_back(l + 4 + (NewImm & 3));
      NewImm >>= 2;
    }
  }
}

void DecodePSHUFLWMask(unsigned NumElts, unsigned Imm,
                       SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned l = 0; l != NumElts; l += 8) {
    unsigned NewImm = Imm;
    for (unsigned i = 0, e = 4; i != e; ++i) {
      ShuffleMask.push_back(l + (NewImm & 3));
      NewImm >>= 2;
    }
    for (unsigned i = 4, e = 8; i != e; ++i) {
      ShuffleMask.push_back(l + i);
    }
  }
}

void DecodePSWAPMask(unsigned NumElts, SmallVectorImpl<int> &ShuffleMask) {
  unsigned NumHalfElts = NumElts / 2;

  for (unsigned l = 0; l != NumHalfElts; ++l)
    ShuffleMask.push_back(l + NumHalfElts);
  for (unsigned h = 0; h != NumHalfElts; ++h)
    ShuffleMask.push_back(h);
}

/// DecodeSHUFPMask - This decodes the shuffle masks for shufp*. VT indicates
/// the type of the vector allowing it to handle different datatypes and vector
/// widths.
void DecodeSHUFPMask(unsigned NumElts, unsigned ScalarBits,
                     unsigned Imm, SmallVectorImpl<int> &ShuffleMask) {
  unsigned NumLaneElts = 128 / ScalarBits;

  unsigned NewImm = Imm;
  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    // each half of a lane comes from different source
    for (unsigned s = 0; s != NumElts * 2; s += NumElts) {
      for (unsigned i = 0; i != NumLaneElts / 2; ++i) {
        ShuffleMask.push_back(NewImm % NumLaneElts + s + l);
        NewImm /= NumLaneElts;
      }
    }
    if (NumLaneElts == 4) NewImm = Imm; // reload imm
  }
}

/// DecodeUNPCKHMask - This decodes the shuffle masks for unpckhps/unpckhpd
/// and punpckh*. VT indicates the type of the vector allowing it to handle
/// different datatypes and vector widths.
void DecodeUNPCKHMask(unsigned NumElts, unsigned ScalarBits,
                      SmallVectorImpl<int> &ShuffleMask) {
  // Handle 128 and 256-bit vector lengths. AVX defines UNPCK* to operate
  // independently on 128-bit lanes.
  unsigned NumLanes = (NumElts * ScalarBits) / 128;
  if (NumLanes == 0) NumLanes = 1;  // Handle MMX
  unsigned NumLaneElts = NumElts / NumLanes;

  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    for (unsigned i = l + NumLaneElts / 2, e = l + NumLaneElts; i != e; ++i) {
      ShuffleMask.push_back(i);           // Reads from dest/src1
      ShuffleMask.push_back(i + NumElts); // Reads from src/src2
    }
  }
}

/// DecodeUNPCKLMask - This decodes the shuffle masks for unpcklps/unpcklpd
/// and punpckl*. VT indicates the type of the vector allowing it to handle
/// different datatypes and vector widths.
void DecodeUNPCKLMask(unsigned NumElts, unsigned ScalarBits,
                      SmallVectorImpl<int> &ShuffleMask) {
  // Handle 128 and 256-bit vector lengths. AVX defines UNPCK* to operate
  // independently on 128-bit lanes.
  unsigned NumLanes = (NumElts * ScalarBits) / 128;
  if (NumLanes == 0 ) NumLanes = 1;  // Handle MMX
  unsigned NumLaneElts = NumElts / NumLanes;

  for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
    for (unsigned i = l, e = l + NumLaneElts / 2; i != e; ++i) {
      ShuffleMask.push_back(i);           // Reads from dest/src1
      ShuffleMask.push_back(i + NumElts); // Reads from src/src2
    }
  }
}

/// Decodes a broadcast of the first element of a vector.
void DecodeVectorBroadcast(unsigned NumElts,
                           SmallVectorImpl<int> &ShuffleMask) {
  ShuffleMask.append(NumElts, 0);
}

/// Decodes a broadcast of a subvector to a larger vector type.
void DecodeSubVectorBroadcast(unsigned DstNumElts, unsigned SrcNumElts,
                              SmallVectorImpl<int> &ShuffleMask) {
  unsigned Scale = DstNumElts / SrcNumElts;

  for (unsigned i = 0; i != Scale; ++i)
    for (unsigned j = 0; j != SrcNumElts; ++j)
      ShuffleMask.push_back(j);
}

/// Decode a shuffle packed values at 128-bit granularity
/// (SHUFF32x4/SHUFF64x2/SHUFI32x4/SHUFI64x2)
/// immediate mask into a shuffle mask.
void decodeVSHUF64x2FamilyMask(unsigned NumElts, unsigned ScalarSize,
                               unsigned Imm,
                               SmallVectorImpl<int> &ShuffleMask) {
  unsigned NumElementsInLane = 128 / ScalarSize;
  unsigned NumLanes = NumElts / NumElementsInLane;

  for (unsigned l = 0; l != NumElts; l += NumElementsInLane) {
    unsigned Index = (Imm % NumLanes) * NumElementsInLane;
    Imm /= NumLanes; // Discard the bits we just used.
    // We actually need the other source.
    if (l >= (NumElts / 2))
      Index += NumElts;
    for (unsigned i = 0; i != NumElementsInLane; ++i)
      ShuffleMask.push_back(Index + i);
  }
}

void DecodeVPERM2X128Mask(unsigned NumElts, unsigned Imm,
                          SmallVectorImpl<int> &ShuffleMask) {
  unsigned HalfSize = NumElts / 2;

  for (unsigned l = 0; l != 2; ++l) {
    unsigned HalfMask = Imm >> (l * 4);
    unsigned HalfBegin = (HalfMask & 0x3) * HalfSize;
    for (unsigned i = HalfBegin, e = HalfBegin + HalfSize; i != e; ++i)
      ShuffleMask.push_back(HalfMask & 8 ? SM_SentinelZero : (int)i);
  }
}

void DecodePSHUFBMask(ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                      SmallVectorImpl<int> &ShuffleMask) {
  for (int i = 0, e = RawMask.size(); i < e; ++i) {
    uint64_t M = RawMask[i];
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }
    // For 256/512-bit vectors the base of the shuffle is the 128-bit
    // subvector we're inside.
    int Base = (i / 16) * 16;
    // If the high bit (7) of the byte is set, the element is zeroed.
    if (M & (1 << 7))
      ShuffleMask.push_back(SM_SentinelZero);
    else {
      // Only the least significant 4 bits of the byte are used.
      int Index = Base + (M & 0xf);
      ShuffleMask.push_back(Index);
    }
  }
}

void DecodeBLENDMask(unsigned NumElts, unsigned Imm,
                     SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned i = 0; i < NumElts; ++i) {
    // If there are more than 8 elements in the vector, then any immediate blend
    // mask wraps around.
    unsigned Bit = i % 8;
    ShuffleMask.push_back(((Imm >> Bit) & 1) ? NumElts + i : i);
  }
}

void DecodeVPPERMMask(ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                      SmallVectorImpl<int> &ShuffleMask) {
  assert(RawMask.size() == 16 && "Illegal VPPERM shuffle mask size");

  // VPPERM Operation
  // Bits[4:0] - Byte Index (0 - 31)
  // Bits[7:5] - Permute Operation
  //
  // Permute Operation:
  // 0 - Source byte (no logical operation).
  // 1 - Invert source byte.
  // 2 - Bit reverse of source byte.
  // 3 - Bit reverse of inverted source byte.
  // 4 - 00h (zero - fill).
  // 5 - FFh (ones - fill).
  // 6 - Most significant bit of source byte replicated in all bit positions.
  // 7 - Invert most significant bit of source byte and replicate in all bit positions.
  for (int i = 0, e = RawMask.size(); i < e; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    uint64_t M = RawMask[i];
    uint64_t PermuteOp = (M >> 5) & 0x7;
    if (PermuteOp == 4) {
      ShuffleMask.push_back(SM_SentinelZero);
      continue;
    }
    if (PermuteOp != 0) {
      ShuffleMask.clear();
      return;
    }

    uint64_t Index = M & 0x1F;
    ShuffleMask.push_back((int)Index);
  }
}

/// DecodeVPERMMask - this decodes the shuffle masks for VPERMQ/VPERMPD.
void DecodeVPERMMask(unsigned NumElts, unsigned Imm,
                     SmallVectorImpl<int> &ShuffleMask) {
  for (unsigned l = 0; l != NumElts; l += 4)
    for (unsigned i = 0; i != 4; ++i)
      ShuffleMask.push_back(l + ((Imm >> (2 * i)) & 3));
}

void DecodeZeroExtendMask(unsigned SrcScalarBits, unsigned DstScalarBits,
                          unsigned NumDstElts, SmallVectorImpl<int> &Mask) {
  unsigned Scale = DstScalarBits / SrcScalarBits;
  assert(SrcScalarBits < DstScalarBits &&
         "Expected zero extension mask to increase scalar size");

  for (unsigned i = 0; i != NumDstElts; i++) {
    Mask.push_back(i);
    for (unsigned j = 1; j != Scale; j++)
      Mask.push_back(SM_SentinelZero);
  }
}

void DecodeZeroMoveLowMask(unsigned NumElts,
                           SmallVectorImpl<int> &ShuffleMask) {
  ShuffleMask.push_back(0);
  for (unsigned i = 1; i < NumElts; i++)
    ShuffleMask.push_back(SM_SentinelZero);
}

void DecodeScalarMoveMask(unsigned NumElts, bool IsLoad,
                          SmallVectorImpl<int> &Mask) {
  // First element comes from the first element of second source.
  // Remaining elements: Load zero extends / Move copies from first source.
  Mask.push_back(NumElts);
  for (unsigned i = 1; i < NumElts; i++)
    Mask.push_back(IsLoad ? static_cast<int>(SM_SentinelZero) : i);
}

void DecodeEXTRQIMask(unsigned NumElts, unsigned EltSize, int Len, int Idx,
                      SmallVectorImpl<int> &ShuffleMask) {
  unsigned HalfElts = NumElts / 2;

  // Only the bottom 6 bits are valid for each immediate.
  Len &= 0x3F;
  Idx &= 0x3F;

  // We can only decode this bit extraction instruction as a shuffle if both the
  // length and index work with whole elements.
  if (0 != (Len % EltSize) || 0 != (Idx % EltSize))
    return;

  // A length of zero is equivalent to a bit length of 64.
  if (Len == 0)
    Len = 64;

  // If the length + index exceeds the bottom 64 bits the result is undefined.
  if ((Len + Idx) > 64) {
    ShuffleMask.append(NumElts, SM_SentinelUndef);
    return;
  }

  // Convert index and index to work with elements.
  Len /= EltSize;
  Idx /= EltSize;

  // EXTRQ: Extract Len elements starting from Idx. Zero pad the remaining
  // elements of the lower 64-bits. The upper 64-bits are undefined.
  for (int i = 0; i != Len; ++i)
    ShuffleMask.push_back(i + Idx);
  for (int i = Len; i != (int)HalfElts; ++i)
    ShuffleMask.push_back(SM_SentinelZero);
  for (int i = HalfElts; i != (int)NumElts; ++i)
    ShuffleMask.push_back(SM_SentinelUndef);
}

void DecodeINSERTQIMask(unsigned NumElts, unsigned EltSize, int Len, int Idx,
                        SmallVectorImpl<int> &ShuffleMask) {
  unsigned HalfElts = NumElts / 2;

  // Only the bottom 6 bits are valid for each immediate.
  Len &= 0x3F;
  Idx &= 0x3F;

  // We can only decode this bit insertion instruction as a shuffle if both the
  // length and index work with whole elements.
  if (0 != (Len % EltSize) || 0 != (Idx % EltSize))
    return;

  // A length of zero is equivalent to a bit length of 64.
  if (Len == 0)
    Len = 64;

  // If the length + index exceeds the bottom 64 bits the result is undefined.
  if ((Len + Idx) > 64) {
    ShuffleMask.append(NumElts, SM_SentinelUndef);
    return;
  }

  // Convert index and index to work with elements.
  Len /= EltSize;
  Idx /= EltSize;

  // INSERTQ: Extract lowest Len elements from lower half of second source and
  // insert over first source starting at Idx element. The upper 64-bits are
  // undefined.
  for (int i = 0; i != Idx; ++i)
    ShuffleMask.push_back(i);
  for (int i = 0; i != Len; ++i)
    ShuffleMask.push_back(i + NumElts);
  for (int i = Idx + Len; i != (int)HalfElts; ++i)
    ShuffleMask.push_back(i);
  for (int i = HalfElts; i != (int)NumElts; ++i)
    ShuffleMask.push_back(SM_SentinelUndef);
}

void DecodeVPERMILPMask(unsigned NumElts, unsigned ScalarBits,
                        ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                        SmallVectorImpl<int> &ShuffleMask) {
  unsigned VecSize = NumElts * ScalarBits;
  unsigned NumLanes = VecSize / 128;
  unsigned NumEltsPerLane = NumElts / NumLanes;
  assert((VecSize == 128 || VecSize == 256 || VecSize == 512) &&
         "Unexpected vector size");
  assert((ScalarBits == 32 || ScalarBits == 64) && "Unexpected element size");

  for (unsigned i = 0, e = RawMask.size(); i < e; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }
    uint64_t M = RawMask[i];
    M = (ScalarBits == 64 ? ((M >> 1) & 0x1) : (M & 0x3));
    unsigned LaneOffset = i & ~(NumEltsPerLane - 1);
    ShuffleMask.push_back((int)(LaneOffset + M));
  }
}

void DecodeVPERMIL2PMask(unsigned NumElts, unsigned ScalarBits, unsigned M2Z,
                         ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                         SmallVectorImpl<int> &ShuffleMask) {
  unsigned VecSize = NumElts * ScalarBits;
  unsigned NumLanes = VecSize / 128;
  unsigned NumEltsPerLane = NumElts / NumLanes;
  assert((VecSize == 128 || VecSize == 256) && "Unexpected vector size");
  assert((ScalarBits == 32 || ScalarBits == 64) && "Unexpected element size");
  assert((NumElts == RawMask.size()) && "Unexpected mask size");

  for (unsigned i = 0, e = RawMask.size(); i < e; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }

    // VPERMIL2 Operation.
    // Bits[3] - Match Bit.
    // Bits[2:1] - (Per Lane) PD Shuffle Mask.
    // Bits[2:0] - (Per Lane) PS Shuffle Mask.
    uint64_t Selector = RawMask[i];
    unsigned MatchBit = (Selector >> 3) & 0x1;

    // M2Z[0:1]     MatchBit
    //   0Xb           X        Source selected by Selector index.
    //   10b           0        Source selected by Selector index.
    //   10b           1        Zero.
    //   11b           0        Zero.
    //   11b           1        Source selected by Selector index.
    if ((M2Z & 0x2) != 0 && MatchBit != (M2Z & 0x1)) {
      ShuffleMask.push_back(SM_SentinelZero);
      continue;
    }

    int Index = i & ~(NumEltsPerLane - 1);
    if (ScalarBits == 64)
      Index += (Selector >> 1) & 0x1;
    else
      Index += Selector & 0x3;

    int Src = (Selector >> 2) & 0x1;
    Index += Src * NumElts;
    ShuffleMask.push_back(Index);
  }
}

void DecodeVPERMVMask(ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                      SmallVectorImpl<int> &ShuffleMask) {
  uint64_t EltMaskSize = RawMask.size() - 1;
  for (int i = 0, e = RawMask.size(); i != e; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }
    uint64_t M = RawMask[i];
    M &= EltMaskSize;
    ShuffleMask.push_back((int)M);
  }
}

void DecodeVPERMV3Mask(ArrayRef<uint64_t> RawMask, const APInt &UndefElts,
                      SmallVectorImpl<int> &ShuffleMask) {
  uint64_t EltMaskSize = (RawMask.size() * 2) - 1;
  for (int i = 0, e = RawMask.size(); i != e; ++i) {
    if (UndefElts[i]) {
      ShuffleMask.push_back(SM_SentinelUndef);
      continue;
    }
    uint64_t M = RawMask[i];
    M &= EltMaskSize;
    ShuffleMask.push_back((int)M);
  }
}

} // llvm namespace
