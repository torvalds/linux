//===-- SystemZOperators.td - SystemZ-specific operators ------*- tblgen-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Type profiles
//===----------------------------------------------------------------------===//
def SDT_CallSeqStart        : SDCallSeqStart<[SDTCisVT<0, i64>,
                                              SDTCisVT<1, i64>]>;
def SDT_CallSeqEnd          : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                            SDTCisVT<1, i64>]>;
def SDT_ZCall               : SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>;
def SDT_ZCmp                : SDTypeProfile<1, 2,
                                            [SDTCisVT<0, i32>,
                                             SDTCisSameAs<1, 2>]>;
def SDT_ZICmp               : SDTypeProfile<1, 3,
                                            [SDTCisVT<0, i32>,
                                             SDTCisSameAs<1, 2>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZBRCCMask           : SDTypeProfile<0, 4,
                                            [SDTCisVT<0, i32>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<2, OtherVT>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZSelectCCMask       : SDTypeProfile<1, 5,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<1, 2>,
                                             SDTCisVT<3, i32>,
                                             SDTCisVT<4, i32>,
                                             SDTCisVT<5, i32>]>;
def SDT_ZWrapPtr            : SDTypeProfile<1, 1,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisPtrTy<0>]>;
def SDT_ZWrapOffset         : SDTypeProfile<1, 2,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisPtrTy<0>]>;
def SDT_ZAdjDynAlloc        : SDTypeProfile<1, 0, [SDTCisVT<0, i64>]>;
def SDT_ZGR128Binary        : SDTypeProfile<1, 2,
                                            [SDTCisVT<0, untyped>,
                                             SDTCisInt<1>,
                                             SDTCisInt<2>]>;
def SDT_ZBinaryWithFlags    : SDTypeProfile<2, 2,
                                            [SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>]>;
def SDT_ZBinaryWithCarry    : SDTypeProfile<2, 3,
                                            [SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisVT<1, i32>]>;
def SDT_ZAtomicLoadBinaryW  : SDTypeProfile<1, 5,
                                            [SDTCisVT<0, i32>,
                                             SDTCisPtrTy<1>,
                                             SDTCisVT<2, i32>,
                                             SDTCisVT<3, i32>,
                                             SDTCisVT<4, i32>,
                                             SDTCisVT<5, i32>]>;
def SDT_ZAtomicCmpSwapW     : SDTypeProfile<2, 6,
                                            [SDTCisVT<0, i32>,
                                             SDTCisVT<1, i32>,
                                             SDTCisPtrTy<2>,
                                             SDTCisVT<3, i32>,
                                             SDTCisVT<4, i32>,
                                             SDTCisVT<5, i32>,
                                             SDTCisVT<6, i32>,
                                             SDTCisVT<7, i32>]>;
def SDT_ZAtomicCmpSwap      : SDTypeProfile<2, 3,
                                            [SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisPtrTy<2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisSameAs<0, 4>]>;
def SDT_ZAtomicLoad128      : SDTypeProfile<1, 1,
                                            [SDTCisVT<0, untyped>,
                                             SDTCisPtrTy<1>]>;
def SDT_ZAtomicStore128     : SDTypeProfile<0, 2,
                                            [SDTCisVT<0, untyped>,
                                             SDTCisPtrTy<1>]>;
def SDT_ZAtomicCmpSwap128   : SDTypeProfile<2, 3,
                                            [SDTCisVT<0, untyped>,
                                             SDTCisVT<1, i32>,
                                             SDTCisPtrTy<2>,
                                             SDTCisVT<3, untyped>,
                                             SDTCisVT<4, untyped>]>;
def SDT_ZMemMemLength       : SDTypeProfile<0, 3,
                                            [SDTCisPtrTy<0>,
                                             SDTCisPtrTy<1>,
                                             SDTCisVT<2, i64>]>;
def SDT_ZMemMemLengthCC     : SDTypeProfile<1, 3,
                                            [SDTCisVT<0, i32>,
                                             SDTCisPtrTy<1>,
                                             SDTCisPtrTy<2>,
                                             SDTCisVT<3, i64>]>;
def SDT_ZMemMemLoop         : SDTypeProfile<0, 4,
                                            [SDTCisPtrTy<0>,
                                             SDTCisPtrTy<1>,
                                             SDTCisVT<2, i64>,
                                             SDTCisVT<3, i64>]>;
def SDT_ZMemMemLoopCC       : SDTypeProfile<1, 4,
                                            [SDTCisVT<0, i32>,
                                             SDTCisPtrTy<1>,
                                             SDTCisPtrTy<2>,
                                             SDTCisVT<3, i64>,
                                             SDTCisVT<4, i64>]>;
def SDT_ZString             : SDTypeProfile<1, 3,
                                            [SDTCisPtrTy<0>,
                                             SDTCisPtrTy<1>,
                                             SDTCisPtrTy<2>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZStringCC           : SDTypeProfile<2, 3,
                                            [SDTCisPtrTy<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisPtrTy<2>,
                                             SDTCisPtrTy<3>,
                                             SDTCisVT<4, i32>]>;
def SDT_ZIPM                : SDTypeProfile<1, 1,
                                            [SDTCisVT<0, i32>,
                                             SDTCisVT<1, i32>]>;
def SDT_ZPrefetch           : SDTypeProfile<0, 2,
                                            [SDTCisVT<0, i32>,
                                             SDTCisPtrTy<1>]>;
def SDT_ZTBegin             : SDTypeProfile<1, 2,
                                            [SDTCisVT<0, i32>,
                                             SDTCisPtrTy<1>,
                                             SDTCisVT<2, i32>]>;
def SDT_ZTEnd               : SDTypeProfile<1, 0,
                                            [SDTCisVT<0, i32>]>;
def SDT_ZInsertVectorElt    : SDTypeProfile<1, 3,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZExtractVectorElt   : SDTypeProfile<1, 2,
                                            [SDTCisVec<1>,
                                             SDTCisVT<2, i32>]>;
def SDT_ZReplicate          : SDTypeProfile<1, 1,
                                            [SDTCisVec<0>]>;
def SDT_ZVecUnaryConv       : SDTypeProfile<1, 1,
                                            [SDTCisVec<0>,
                                             SDTCisVec<1>]>;
def SDT_ZVecUnary           : SDTypeProfile<1, 1,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>]>;
def SDT_ZVecUnaryCC         : SDTypeProfile<2, 1,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>]>;
def SDT_ZVecBinary          : SDTypeProfile<1, 2,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>]>;
def SDT_ZVecBinaryCC        : SDTypeProfile<2, 2,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 2>]>;
def SDT_ZVecBinaryInt       : SDTypeProfile<1, 2,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i32>]>;
def SDT_ZVecBinaryConv      : SDTypeProfile<1, 2,
                                            [SDTCisVec<0>,
                                             SDTCisVec<1>,
                                             SDTCisSameAs<1, 2>]>;
def SDT_ZVecBinaryConvCC    : SDTypeProfile<2, 2,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVec<2>,
                                             SDTCisSameAs<2, 3>]>;
def SDT_ZVecBinaryConvIntCC : SDTypeProfile<2, 2,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVec<2>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZRotateMask         : SDTypeProfile<1, 2,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<2, i32>]>;
def SDT_ZJoinDwords         : SDTypeProfile<1, 2,
                                            [SDTCisVT<0, v2i64>,
                                             SDTCisVT<1, i64>,
                                             SDTCisVT<2, i64>]>;
def SDT_ZVecTernary         : SDTypeProfile<1, 3,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>]>;
def SDT_ZVecTernaryInt      : SDTypeProfile<1, 3,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisVT<3, i32>]>;
def SDT_ZVecTernaryIntCC    : SDTypeProfile<2, 3,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisVT<4, i32>]>;
def SDT_ZVecQuaternaryInt   : SDTypeProfile<1, 4,
                                            [SDTCisVec<0>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisVT<4, i32>]>;
def SDT_ZVecQuaternaryIntCC : SDTypeProfile<2, 4,
                                            [SDTCisVec<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisSameAs<0, 4>,
                                             SDTCisVT<5, i32>]>;
def SDT_ZTest               : SDTypeProfile<1, 2,
                                            [SDTCisVT<0, i32>,
                                             SDTCisVT<2, i64>]>;

//===----------------------------------------------------------------------===//
// Node definitions
//===----------------------------------------------------------------------===//

// These are target-independent nodes, but have target-specific formats.
def callseq_start       : SDNode<"ISD::CALLSEQ_START", SDT_CallSeqStart,
                                 [SDNPHasChain, SDNPSideEffect, SDNPOutGlue]>;
def callseq_end         : SDNode<"ISD::CALLSEQ_END",   SDT_CallSeqEnd,
                                 [SDNPHasChain, SDNPSideEffect, SDNPOptInGlue,
                                  SDNPOutGlue]>;
def global_offset_table : SDNode<"ISD::GLOBAL_OFFSET_TABLE", SDTPtrLeaf>;

// Nodes for SystemZISD::*.  See SystemZISelLowering.h for more details.
def z_retflag           : SDNode<"SystemZISD::RET_FLAG", SDTNone,
                                 [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def z_call              : SDNode<"SystemZISD::CALL", SDT_ZCall,
                                 [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                                  SDNPVariadic]>;
def z_sibcall           : SDNode<"SystemZISD::SIBCALL", SDT_ZCall,
                                 [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                                  SDNPVariadic]>;
def z_tls_gdcall        : SDNode<"SystemZISD::TLS_GDCALL", SDT_ZCall,
                                 [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                                  SDNPVariadic]>;
def z_tls_ldcall        : SDNode<"SystemZISD::TLS_LDCALL", SDT_ZCall,
                                 [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                                  SDNPVariadic]>;
def z_pcrel_wrapper     : SDNode<"SystemZISD::PCREL_WRAPPER", SDT_ZWrapPtr, []>;
def z_pcrel_offset      : SDNode<"SystemZISD::PCREL_OFFSET",
                                 SDT_ZWrapOffset, []>;
def z_iabs              : SDNode<"SystemZISD::IABS", SDTIntUnaryOp, []>;
def z_icmp              : SDNode<"SystemZISD::ICMP", SDT_ZICmp>;
def z_fcmp              : SDNode<"SystemZISD::FCMP", SDT_ZCmp>;
def z_tm                : SDNode<"SystemZISD::TM", SDT_ZICmp>;
def z_br_ccmask_1       : SDNode<"SystemZISD::BR_CCMASK", SDT_ZBRCCMask,
                                 [SDNPHasChain]>;
def z_select_ccmask_1   : SDNode<"SystemZISD::SELECT_CCMASK",
                                 SDT_ZSelectCCMask>;
def z_ipm_1             : SDNode<"SystemZISD::IPM", SDT_ZIPM>;
def z_adjdynalloc       : SDNode<"SystemZISD::ADJDYNALLOC", SDT_ZAdjDynAlloc>;
def z_popcnt            : SDNode<"SystemZISD::POPCNT", SDTIntUnaryOp>;
def z_smul_lohi         : SDNode<"SystemZISD::SMUL_LOHI", SDT_ZGR128Binary>;
def z_umul_lohi         : SDNode<"SystemZISD::UMUL_LOHI", SDT_ZGR128Binary>;
def z_sdivrem           : SDNode<"SystemZISD::SDIVREM", SDT_ZGR128Binary>;
def z_udivrem           : SDNode<"SystemZISD::UDIVREM", SDT_ZGR128Binary>;
def z_saddo             : SDNode<"SystemZISD::SADDO", SDT_ZBinaryWithFlags>;
def z_ssubo             : SDNode<"SystemZISD::SSUBO", SDT_ZBinaryWithFlags>;
def z_uaddo             : SDNode<"SystemZISD::UADDO", SDT_ZBinaryWithFlags>;
def z_usubo             : SDNode<"SystemZISD::USUBO", SDT_ZBinaryWithFlags>;
def z_addcarry_1        : SDNode<"SystemZISD::ADDCARRY", SDT_ZBinaryWithCarry>;
def z_subcarry_1        : SDNode<"SystemZISD::SUBCARRY", SDT_ZBinaryWithCarry>;

def z_membarrier        : SDNode<"SystemZISD::MEMBARRIER", SDTNone,
                                 [SDNPHasChain, SDNPSideEffect]>;

def z_loadbswap        : SDNode<"SystemZISD::LRV", SDTLoad,
                                 [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def z_storebswap       : SDNode<"SystemZISD::STRV", SDTStore,
                                 [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def z_tdc               : SDNode<"SystemZISD::TDC", SDT_ZTest>;

// Defined because the index is an i32 rather than a pointer.
def z_vector_insert     : SDNode<"ISD::INSERT_VECTOR_ELT",
                                 SDT_ZInsertVectorElt>;
def z_vector_extract    : SDNode<"ISD::EXTRACT_VECTOR_ELT",
                                 SDT_ZExtractVectorElt>;
def z_byte_mask         : SDNode<"SystemZISD::BYTE_MASK", SDT_ZReplicate>;
def z_rotate_mask       : SDNode<"SystemZISD::ROTATE_MASK", SDT_ZRotateMask>;
def z_replicate         : SDNode<"SystemZISD::REPLICATE", SDT_ZReplicate>;
def z_join_dwords       : SDNode<"SystemZISD::JOIN_DWORDS", SDT_ZJoinDwords>;
def z_splat             : SDNode<"SystemZISD::SPLAT", SDT_ZVecBinaryInt>;
def z_merge_high        : SDNode<"SystemZISD::MERGE_HIGH", SDT_ZVecBinary>;
def z_merge_low         : SDNode<"SystemZISD::MERGE_LOW", SDT_ZVecBinary>;
def z_shl_double        : SDNode<"SystemZISD::SHL_DOUBLE", SDT_ZVecTernaryInt>;
def z_permute_dwords    : SDNode<"SystemZISD::PERMUTE_DWORDS",
                                 SDT_ZVecTernaryInt>;
def z_permute           : SDNode<"SystemZISD::PERMUTE", SDT_ZVecTernary>;
def z_pack              : SDNode<"SystemZISD::PACK", SDT_ZVecBinaryConv>;
def z_packs_cc          : SDNode<"SystemZISD::PACKS_CC", SDT_ZVecBinaryConvCC>;
def z_packls_cc         : SDNode<"SystemZISD::PACKLS_CC", SDT_ZVecBinaryConvCC>;
def z_unpack_high       : SDNode<"SystemZISD::UNPACK_HIGH", SDT_ZVecUnaryConv>;
def z_unpackl_high      : SDNode<"SystemZISD::UNPACKL_HIGH", SDT_ZVecUnaryConv>;
def z_unpack_low        : SDNode<"SystemZISD::UNPACK_LOW", SDT_ZVecUnaryConv>;
def z_unpackl_low       : SDNode<"SystemZISD::UNPACKL_LOW", SDT_ZVecUnaryConv>;
def z_vshl_by_scalar    : SDNode<"SystemZISD::VSHL_BY_SCALAR",
                                 SDT_ZVecBinaryInt>;
def z_vsrl_by_scalar    : SDNode<"SystemZISD::VSRL_BY_SCALAR",
                                 SDT_ZVecBinaryInt>;
def z_vsra_by_scalar    : SDNode<"SystemZISD::VSRA_BY_SCALAR",
                                 SDT_ZVecBinaryInt>;
def z_vsum              : SDNode<"SystemZISD::VSUM", SDT_ZVecBinaryConv>;
def z_vicmpe            : SDNode<"SystemZISD::VICMPE", SDT_ZVecBinary>;
def z_vicmph            : SDNode<"SystemZISD::VICMPH", SDT_ZVecBinary>;
def z_vicmphl           : SDNode<"SystemZISD::VICMPHL", SDT_ZVecBinary>;
def z_vicmpes           : SDNode<"SystemZISD::VICMPES", SDT_ZVecBinaryCC>;
def z_vicmphs           : SDNode<"SystemZISD::VICMPHS", SDT_ZVecBinaryCC>;
def z_vicmphls          : SDNode<"SystemZISD::VICMPHLS", SDT_ZVecBinaryCC>;
def z_vfcmpe            : SDNode<"SystemZISD::VFCMPE", SDT_ZVecBinaryConv>;
def z_vfcmph            : SDNode<"SystemZISD::VFCMPH", SDT_ZVecBinaryConv>;
def z_vfcmphe           : SDNode<"SystemZISD::VFCMPHE", SDT_ZVecBinaryConv>;
def z_vfcmpes           : SDNode<"SystemZISD::VFCMPES", SDT_ZVecBinaryConvCC>;
def z_vfcmphs           : SDNode<"SystemZISD::VFCMPHS", SDT_ZVecBinaryConvCC>;
def z_vfcmphes          : SDNode<"SystemZISD::VFCMPHES", SDT_ZVecBinaryConvCC>;
def z_vextend           : SDNode<"SystemZISD::VEXTEND", SDT_ZVecUnaryConv>;
def z_vround            : SDNode<"SystemZISD::VROUND", SDT_ZVecUnaryConv>;
def z_vtm               : SDNode<"SystemZISD::VTM", SDT_ZCmp>;
def z_vfae_cc           : SDNode<"SystemZISD::VFAE_CC", SDT_ZVecTernaryIntCC>;
def z_vfaez_cc          : SDNode<"SystemZISD::VFAEZ_CC", SDT_ZVecTernaryIntCC>;
def z_vfee_cc           : SDNode<"SystemZISD::VFEE_CC", SDT_ZVecBinaryCC>;
def z_vfeez_cc          : SDNode<"SystemZISD::VFEEZ_CC", SDT_ZVecBinaryCC>;
def z_vfene_cc          : SDNode<"SystemZISD::VFENE_CC", SDT_ZVecBinaryCC>;
def z_vfenez_cc         : SDNode<"SystemZISD::VFENEZ_CC", SDT_ZVecBinaryCC>;
def z_vistr_cc          : SDNode<"SystemZISD::VISTR_CC", SDT_ZVecUnaryCC>;
def z_vstrc_cc          : SDNode<"SystemZISD::VSTRC_CC",
                                 SDT_ZVecQuaternaryIntCC>;
def z_vstrcz_cc         : SDNode<"SystemZISD::VSTRCZ_CC",
                                 SDT_ZVecQuaternaryIntCC>;
def z_vftci             : SDNode<"SystemZISD::VFTCI", SDT_ZVecBinaryConvIntCC>;

class AtomicWOp<string name, SDTypeProfile profile = SDT_ZAtomicLoadBinaryW>
  : SDNode<"SystemZISD::"##name, profile,
           [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;

def z_atomic_swapw      : AtomicWOp<"ATOMIC_SWAPW">;
def z_atomic_loadw_add  : AtomicWOp<"ATOMIC_LOADW_ADD">;
def z_atomic_loadw_sub  : AtomicWOp<"ATOMIC_LOADW_SUB">;
def z_atomic_loadw_and  : AtomicWOp<"ATOMIC_LOADW_AND">;
def z_atomic_loadw_or   : AtomicWOp<"ATOMIC_LOADW_OR">;
def z_atomic_loadw_xor  : AtomicWOp<"ATOMIC_LOADW_XOR">;
def z_atomic_loadw_nand : AtomicWOp<"ATOMIC_LOADW_NAND">;
def z_atomic_loadw_min  : AtomicWOp<"ATOMIC_LOADW_MIN">;
def z_atomic_loadw_max  : AtomicWOp<"ATOMIC_LOADW_MAX">;
def z_atomic_loadw_umin : AtomicWOp<"ATOMIC_LOADW_UMIN">;
def z_atomic_loadw_umax : AtomicWOp<"ATOMIC_LOADW_UMAX">;

def z_atomic_cmp_swap   : SDNode<"SystemZISD::ATOMIC_CMP_SWAP",
                                 SDT_ZAtomicCmpSwap,
                                 [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                                  SDNPMemOperand]>;
def z_atomic_cmp_swapw  : SDNode<"SystemZISD::ATOMIC_CMP_SWAPW",
                                 SDT_ZAtomicCmpSwapW,
                                 [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                                  SDNPMemOperand]>;

def z_atomic_load_128   : SDNode<"SystemZISD::ATOMIC_LOAD_128",
                                 SDT_ZAtomicLoad128,
                                 [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def z_atomic_store_128  : SDNode<"SystemZISD::ATOMIC_STORE_128",
                                 SDT_ZAtomicStore128,
                                 [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def z_atomic_cmp_swap_128 : SDNode<"SystemZISD::ATOMIC_CMP_SWAP_128",
                                   SDT_ZAtomicCmpSwap128,
                                   [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                                    SDNPMemOperand]>;

def z_mvc               : SDNode<"SystemZISD::MVC", SDT_ZMemMemLength,
                                 [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_mvc_loop          : SDNode<"SystemZISD::MVC_LOOP", SDT_ZMemMemLoop,
                                 [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_nc                : SDNode<"SystemZISD::NC", SDT_ZMemMemLength,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_nc_loop           : SDNode<"SystemZISD::NC_LOOP", SDT_ZMemMemLoop,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_oc                : SDNode<"SystemZISD::OC", SDT_ZMemMemLength,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_oc_loop           : SDNode<"SystemZISD::OC_LOOP", SDT_ZMemMemLoop,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_xc                : SDNode<"SystemZISD::XC", SDT_ZMemMemLength,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_xc_loop           : SDNode<"SystemZISD::XC_LOOP", SDT_ZMemMemLoop,
                                  [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_clc               : SDNode<"SystemZISD::CLC", SDT_ZMemMemLengthCC,
                                 [SDNPHasChain, SDNPMayLoad]>;
def z_clc_loop          : SDNode<"SystemZISD::CLC_LOOP", SDT_ZMemMemLoopCC,
                                 [SDNPHasChain, SDNPMayLoad]>;
def z_strcmp            : SDNode<"SystemZISD::STRCMP", SDT_ZStringCC,
                                 [SDNPHasChain, SDNPMayLoad]>;
def z_stpcpy            : SDNode<"SystemZISD::STPCPY", SDT_ZString,
                                 [SDNPHasChain, SDNPMayStore, SDNPMayLoad]>;
def z_search_string     : SDNode<"SystemZISD::SEARCH_STRING", SDT_ZStringCC,
                                 [SDNPHasChain, SDNPMayLoad]>;
def z_prefetch          : SDNode<"SystemZISD::PREFETCH", SDT_ZPrefetch,
                                 [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                                  SDNPMemOperand]>;

def z_tbegin            : SDNode<"SystemZISD::TBEGIN", SDT_ZTBegin,
                                 [SDNPHasChain, SDNPMayStore, SDNPSideEffect]>;
def z_tbegin_nofloat    : SDNode<"SystemZISD::TBEGIN_NOFLOAT", SDT_ZTBegin,
                                 [SDNPHasChain, SDNPMayStore, SDNPSideEffect]>;
def z_tend              : SDNode<"SystemZISD::TEND", SDT_ZTEnd,
                                 [SDNPHasChain, SDNPSideEffect]>;

def z_vshl              : SDNode<"ISD::SHL", SDT_ZVecBinary>;
def z_vsra              : SDNode<"ISD::SRA", SDT_ZVecBinary>;
def z_vsrl              : SDNode<"ISD::SRL", SDT_ZVecBinary>;

//===----------------------------------------------------------------------===//
// Pattern fragments
//===----------------------------------------------------------------------===//

def z_loadbswap16 : PatFrag<(ops node:$addr), (z_loadbswap node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def z_loadbswap32 : PatFrag<(ops node:$addr), (z_loadbswap node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;
def z_loadbswap64 : PatFrag<(ops node:$addr), (z_loadbswap node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

def z_storebswap16 : PatFrag<(ops node:$src, node:$addr),
                             (z_storebswap node:$src, node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def z_storebswap32 : PatFrag<(ops node:$src, node:$addr),
                             (z_storebswap node:$src, node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;
def z_storebswap64 : PatFrag<(ops node:$src, node:$addr),
                             (z_storebswap node:$src, node:$addr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

// Fragments including CC as an implicit source.
def z_br_ccmask
  : PatFrag<(ops node:$valid, node:$mask, node:$bb),
            (z_br_ccmask_1 node:$valid, node:$mask, node:$bb, CC)>;
def z_select_ccmask
  : PatFrag<(ops node:$true, node:$false, node:$valid, node:$mask),
            (z_select_ccmask_1 node:$true, node:$false,
                               node:$valid, node:$mask, CC)>;
def z_ipm : PatFrag<(ops), (z_ipm_1 CC)>;
def z_addcarry : PatFrag<(ops node:$lhs, node:$rhs),
                              (z_addcarry_1 node:$lhs, node:$rhs, CC)>;
def z_subcarry : PatFrag<(ops node:$lhs, node:$rhs),
                              (z_subcarry_1 node:$lhs, node:$rhs, CC)>;

// Signed and unsigned comparisons.
def z_scmp : PatFrag<(ops node:$a, node:$b), (z_icmp node:$a, node:$b, imm), [{
  unsigned Type = cast<ConstantSDNode>(N->getOperand(2))->getZExtValue();
  return Type != SystemZICMP::UnsignedOnly;
}]>;
def z_ucmp : PatFrag<(ops node:$a, node:$b), (z_icmp node:$a, node:$b, imm), [{
  unsigned Type = cast<ConstantSDNode>(N->getOperand(2))->getZExtValue();
  return Type != SystemZICMP::SignedOnly;
}]>;

// Register- and memory-based TEST UNDER MASK.
def z_tm_reg : PatFrag<(ops node:$a, node:$b), (z_tm node:$a, node:$b, imm)>;
def z_tm_mem : PatFrag<(ops node:$a, node:$b), (z_tm node:$a, node:$b, 0)>;

// Register sign-extend operations.  Sub-32-bit values are represented as i32s.
def sext8  : PatFrag<(ops node:$src), (sext_inreg node:$src, i8)>;
def sext16 : PatFrag<(ops node:$src), (sext_inreg node:$src, i16)>;
def sext32 : PatFrag<(ops node:$src), (sext (i32 node:$src))>;

// Match extensions of an i32 to an i64, followed by an in-register sign
// extension from a sub-i32 value.
def sext8dbl : PatFrag<(ops node:$src), (sext8 (anyext node:$src))>;
def sext16dbl : PatFrag<(ops node:$src), (sext16 (anyext node:$src))>;

// Register zero-extend operations.  Sub-32-bit values are represented as i32s.
def zext8  : PatFrag<(ops node:$src), (and node:$src, 0xff)>;
def zext16 : PatFrag<(ops node:$src), (and node:$src, 0xffff)>;
def zext32 : PatFrag<(ops node:$src), (zext (i32 node:$src))>;

// Extending loads in which the extension type can be signed.
def asextload : PatFrag<(ops node:$ptr), (unindexedload node:$ptr), [{
  unsigned Type = cast<LoadSDNode>(N)->getExtensionType();
  return Type == ISD::EXTLOAD || Type == ISD::SEXTLOAD;
}]>;
def asextloadi8 : PatFrag<(ops node:$ptr), (asextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;
def asextloadi16 : PatFrag<(ops node:$ptr), (asextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def asextloadi32 : PatFrag<(ops node:$ptr), (asextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

// Extending loads in which the extension type can be unsigned.
def azextload : PatFrag<(ops node:$ptr), (unindexedload node:$ptr), [{
  unsigned Type = cast<LoadSDNode>(N)->getExtensionType();
  return Type == ISD::EXTLOAD || Type == ISD::ZEXTLOAD;
}]>;
def azextloadi8 : PatFrag<(ops node:$ptr), (azextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;
def azextloadi16 : PatFrag<(ops node:$ptr), (azextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def azextloadi32 : PatFrag<(ops node:$ptr), (azextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

// Extending loads in which the extension type doesn't matter.
def anyextload : PatFrag<(ops node:$ptr), (unindexedload node:$ptr), [{
  return cast<LoadSDNode>(N)->getExtensionType() != ISD::NON_EXTLOAD;
}]>;
def anyextloadi8 : PatFrag<(ops node:$ptr), (anyextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;
def anyextloadi16 : PatFrag<(ops node:$ptr), (anyextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def anyextloadi32 : PatFrag<(ops node:$ptr), (anyextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

// Aligned loads.
class AlignedLoad<SDPatternOperator load>
  : PatFrag<(ops node:$addr), (load node:$addr), [{
  auto *Load = cast<LoadSDNode>(N);
  return Load->getAlignment() >= Load->getMemoryVT().getStoreSize();
}]>;
def aligned_load         : AlignedLoad<load>;
def aligned_asextloadi16 : AlignedLoad<asextloadi16>;
def aligned_asextloadi32 : AlignedLoad<asextloadi32>;
def aligned_azextloadi16 : AlignedLoad<azextloadi16>;
def aligned_azextloadi32 : AlignedLoad<azextloadi32>;

// Aligned stores.
class AlignedStore<SDPatternOperator store>
  : PatFrag<(ops node:$src, node:$addr), (store node:$src, node:$addr), [{
  auto *Store = cast<StoreSDNode>(N);
  return Store->getAlignment() >= Store->getMemoryVT().getStoreSize();
}]>;
def aligned_store         : AlignedStore<store>;
def aligned_truncstorei16 : AlignedStore<truncstorei16>;
def aligned_truncstorei32 : AlignedStore<truncstorei32>;

// Non-volatile loads.  Used for instructions that might access the storage
// location multiple times.
class NonvolatileLoad<SDPatternOperator load>
  : PatFrag<(ops node:$addr), (load node:$addr), [{
  auto *Load = cast<LoadSDNode>(N);
  return !Load->isVolatile();
}]>;
def nonvolatile_anyextloadi8  : NonvolatileLoad<anyextloadi8>;
def nonvolatile_anyextloadi16 : NonvolatileLoad<anyextloadi16>;
def nonvolatile_anyextloadi32 : NonvolatileLoad<anyextloadi32>;

// Non-volatile stores.
class NonvolatileStore<SDPatternOperator store>
  : PatFrag<(ops node:$src, node:$addr), (store node:$src, node:$addr), [{
  auto *Store = cast<StoreSDNode>(N);
  return !Store->isVolatile();
}]>;
def nonvolatile_truncstorei8  : NonvolatileStore<truncstorei8>;
def nonvolatile_truncstorei16 : NonvolatileStore<truncstorei16>;
def nonvolatile_truncstorei32 : NonvolatileStore<truncstorei32>;

// A store of a load that can be implemented using MVC.
def mvc_store : PatFrag<(ops node:$value, node:$addr),
                        (unindexedstore node:$value, node:$addr),
                        [{ return storeLoadCanUseMVC(N); }]>;

// Binary read-modify-write operations on memory in which the other
// operand is also memory and for which block operations like NC can
// be used.  There are two patterns for each operator, depending on
// which operand contains the "other" load.
multiclass block_op<SDPatternOperator operator> {
  def "1" : PatFrag<(ops node:$value, node:$addr),
                    (unindexedstore (operator node:$value,
                                              (unindexedload node:$addr)),
                                    node:$addr),
                    [{ return storeLoadCanUseBlockBinary(N, 0); }]>;
  def "2" : PatFrag<(ops node:$value, node:$addr),
                    (unindexedstore (operator (unindexedload node:$addr),
                                              node:$value),
                                    node:$addr),
                    [{ return storeLoadCanUseBlockBinary(N, 1); }]>;
}
defm block_and : block_op<and>;
defm block_or  : block_op<or>;
defm block_xor : block_op<xor>;

// Insertions.
def inserti8 : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, -256), node:$src2)>;
def insertll : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0xffffffffffff0000), node:$src2)>;
def insertlh : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0xffffffff0000ffff), node:$src2)>;
def inserthl : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0xffff0000ffffffff), node:$src2)>;
def inserthh : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0x0000ffffffffffff), node:$src2)>;
def insertlf : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0xffffffff00000000), node:$src2)>;
def inserthf : PatFrag<(ops node:$src1, node:$src2),
                       (or (and node:$src1, 0x00000000ffffffff), node:$src2)>;

// ORs that can be treated as insertions.
def or_as_inserti8 : PatFrag<(ops node:$src1, node:$src2),
                             (or node:$src1, node:$src2), [{
  unsigned BitWidth = N->getValueType(0).getScalarSizeInBits();
  return CurDAG->MaskedValueIsZero(N->getOperand(0),
                                   APInt::getLowBitsSet(BitWidth, 8));
}]>;

// ORs that can be treated as reversed insertions.
def or_as_revinserti8 : PatFrag<(ops node:$src1, node:$src2),
                                (or node:$src1, node:$src2), [{
  unsigned BitWidth = N->getValueType(0).getScalarSizeInBits();
  return CurDAG->MaskedValueIsZero(N->getOperand(1),
                                   APInt::getLowBitsSet(BitWidth, 8));
}]>;

// Negative integer absolute.
def z_inegabs : PatFrag<(ops node:$src), (ineg (z_iabs node:$src))>;

// Integer absolute, matching the canonical form generated by DAGCombiner.
def z_iabs32 : PatFrag<(ops node:$src),
                       (xor (add node:$src, (sra node:$src, (i32 31))),
                            (sra node:$src, (i32 31)))>;
def z_iabs64 : PatFrag<(ops node:$src),
                       (xor (add node:$src, (sra node:$src, (i32 63))),
                            (sra node:$src, (i32 63)))>;
def z_inegabs32 : PatFrag<(ops node:$src), (ineg (z_iabs32 node:$src))>;
def z_inegabs64 : PatFrag<(ops node:$src), (ineg (z_iabs64 node:$src))>;

// Integer multiply-and-add
def z_muladd : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                       (add (mul node:$src1, node:$src2), node:$src3)>;

// Alternatives to match operations with or without an overflow CC result.
def z_sadd : PatFrags<(ops node:$src1, node:$src2),
                      [(z_saddo node:$src1, node:$src2),
                       (add node:$src1, node:$src2)]>;
def z_uadd : PatFrags<(ops node:$src1, node:$src2),
                      [(z_uaddo node:$src1, node:$src2),
                       (add node:$src1, node:$src2)]>;
def z_ssub : PatFrags<(ops node:$src1, node:$src2),
                      [(z_ssubo node:$src1, node:$src2),
                       (sub node:$src1, node:$src2)]>;
def z_usub : PatFrags<(ops node:$src1, node:$src2),
                      [(z_usubo node:$src1, node:$src2),
                       (sub node:$src1, node:$src2)]>;

// Fused multiply-subtract, using the natural operand order.
def fms : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                  (fma node:$src1, node:$src2, (fneg node:$src3))>;

// Fused multiply-add and multiply-subtract, but with the order of the
// operands matching SystemZ's MA and MS instructions.
def z_fma : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                    (fma node:$src2, node:$src3, node:$src1)>;
def z_fms : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                    (fma node:$src2, node:$src3, (fneg node:$src1))>;

// Negative fused multiply-add and multiply-subtract.
def fnma : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                   (fneg (fma node:$src1, node:$src2, node:$src3))>;
def fnms : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                   (fneg (fms node:$src1, node:$src2, node:$src3))>;

// Floating-point negative absolute.
def fnabs : PatFrag<(ops node:$ptr), (fneg (fabs node:$ptr))>;

// Create a unary operator that loads from memory and then performs
// the given operation on it.
class loadu<SDPatternOperator operator, SDPatternOperator load = load>
  : PatFrag<(ops node:$addr), (operator (load node:$addr))>;

// Create a store operator that performs the given unary operation
// on the value before storing it.
class storeu<SDPatternOperator operator, SDPatternOperator store = store>
  : PatFrag<(ops node:$value, node:$addr),
            (store (operator node:$value), node:$addr)>;

// Create a store operator that performs the given inherent operation
// and stores the resulting value.
class storei<SDPatternOperator operator, SDPatternOperator store = store>
  : PatFrag<(ops node:$addr),
            (store (operator), node:$addr)>;

// Create a shift operator that optionally ignores an AND of the
// shift count with an immediate if the bottom 6 bits are all set.
def imm32bottom6set : PatLeaf<(i32 imm), [{
  return (N->getZExtValue() & 0x3f) == 0x3f;
}]>;
class shiftop<SDPatternOperator operator>
  : PatFrags<(ops node:$val, node:$count),
             [(operator node:$val, node:$count),
              (operator node:$val, (and node:$count, imm32bottom6set))]>;

// Vector representation of all-zeros and all-ones.
def z_vzero : PatFrag<(ops), (bitconvert (v16i8 (z_byte_mask (i32 0))))>;
def z_vones : PatFrag<(ops), (bitconvert (v16i8 (z_byte_mask (i32 65535))))>;

// Load a scalar and replicate it in all elements of a vector.
class z_replicate_load<ValueType scalartype, SDPatternOperator load>
  : PatFrag<(ops node:$addr),
            (z_replicate (scalartype (load node:$addr)))>;
def z_replicate_loadi8  : z_replicate_load<i32, anyextloadi8>;
def z_replicate_loadi16 : z_replicate_load<i32, anyextloadi16>;
def z_replicate_loadi32 : z_replicate_load<i32, load>;
def z_replicate_loadi64 : z_replicate_load<i64, load>;
def z_replicate_loadf32 : z_replicate_load<f32, load>;
def z_replicate_loadf64 : z_replicate_load<f64, load>;

// Load a scalar and insert it into a single element of a vector.
class z_vle<ValueType scalartype, SDPatternOperator load>
  : PatFrag<(ops node:$vec, node:$addr, node:$index),
            (z_vector_insert node:$vec, (scalartype (load node:$addr)),
                             node:$index)>;
def z_vlei8  : z_vle<i32, anyextloadi8>;
def z_vlei16 : z_vle<i32, anyextloadi16>;
def z_vlei32 : z_vle<i32, load>;
def z_vlei64 : z_vle<i64, load>;
def z_vlef32 : z_vle<f32, load>;
def z_vlef64 : z_vle<f64, load>;

// Load a scalar and insert it into the low element of the high i64 of a
// zeroed vector.
class z_vllez<ValueType scalartype, SDPatternOperator load, int index>
  : PatFrag<(ops node:$addr),
            (z_vector_insert (z_vzero),
                             (scalartype (load node:$addr)), (i32 index))>;
def z_vllezi8  : z_vllez<i32, anyextloadi8, 7>;
def z_vllezi16 : z_vllez<i32, anyextloadi16, 3>;
def z_vllezi32 : z_vllez<i32, load, 1>;
def z_vllezi64 : PatFrags<(ops node:$addr),
                          [(z_vector_insert (z_vzero),
                                            (i64 (load node:$addr)), (i32 0)),
                           (z_join_dwords (i64 (load node:$addr)), (i64 0))]>;
// We use high merges to form a v4f32 from four f32s.  Propagating zero
// into all elements but index 1 gives this expression.
def z_vllezf32 : PatFrag<(ops node:$addr),
                         (z_merge_high
                          (v2i64
                           (z_unpackl_high
                            (v4i32
                             (bitconvert
                              (v4f32 (scalar_to_vector
                                      (f32 (load node:$addr)))))))),
                          (v2i64 (z_vzero)))>;
def z_vllezf64 : PatFrag<(ops node:$addr),
                         (z_merge_high
                          (v2f64 (scalar_to_vector (f64 (load node:$addr)))),
                          (z_vzero))>;

// Similarly for the high element of a zeroed vector.
def z_vllezli32 : z_vllez<i32, load, 0>;
def z_vllezlf32 : PatFrag<(ops node:$addr),
                          (z_merge_high
                           (v2i64
                            (bitconvert
                             (z_merge_high
                              (v4f32 (scalar_to_vector
                                      (f32 (load node:$addr)))),
                              (v4f32 (z_vzero))))),
                           (v2i64 (z_vzero)))>;

// Store one element of a vector.
class z_vste<ValueType scalartype, SDPatternOperator store>
  : PatFrag<(ops node:$vec, node:$addr, node:$index),
            (store (scalartype (z_vector_extract node:$vec, node:$index)),
                   node:$addr)>;
def z_vstei8  : z_vste<i32, truncstorei8>;
def z_vstei16 : z_vste<i32, truncstorei16>;
def z_vstei32 : z_vste<i32, store>;
def z_vstei64 : z_vste<i64, store>;
def z_vstef32 : z_vste<f32, store>;
def z_vstef64 : z_vste<f64, store>;

// Arithmetic negation on vectors.
def z_vneg : PatFrag<(ops node:$x), (sub (z_vzero), node:$x)>;

// Bitwise negation on vectors.
def z_vnot : PatFrag<(ops node:$x), (xor node:$x, (z_vones))>;

// Signed "integer greater than zero" on vectors.
def z_vicmph_zero : PatFrag<(ops node:$x), (z_vicmph node:$x, (z_vzero))>;

// Signed "integer less than zero" on vectors.
def z_vicmpl_zero : PatFrag<(ops node:$x), (z_vicmph (z_vzero), node:$x)>;

// Integer absolute on vectors.
class z_viabs<int shift>
  : PatFrag<(ops node:$src),
            (xor (add node:$src, (z_vsra_by_scalar node:$src, (i32 shift))),
                 (z_vsra_by_scalar node:$src, (i32 shift)))>;
def z_viabs8  : z_viabs<7>;
def z_viabs16 : z_viabs<15>;
def z_viabs32 : z_viabs<31>;
def z_viabs64 : z_viabs<63>;

// Sign-extend the i64 elements of a vector.
class z_vse<int shift>
  : PatFrag<(ops node:$src),
            (z_vsra_by_scalar (z_vshl_by_scalar node:$src, shift), shift)>;
def z_vsei8  : z_vse<56>;
def z_vsei16 : z_vse<48>;
def z_vsei32 : z_vse<32>;

// ...and again with the extensions being done on individual i64 scalars.
class z_vse_by_parts<SDPatternOperator operator, int index1, int index2>
  : PatFrag<(ops node:$src),
            (z_join_dwords
             (operator (z_vector_extract node:$src, index1)),
             (operator (z_vector_extract node:$src, index2)))>;
def z_vsei8_by_parts  : z_vse_by_parts<sext8dbl, 7, 15>;
def z_vsei16_by_parts : z_vse_by_parts<sext16dbl, 3, 7>;
def z_vsei32_by_parts : z_vse_by_parts<sext32, 1, 3>;
