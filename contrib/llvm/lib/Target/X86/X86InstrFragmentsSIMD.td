//===-- X86InstrFragmentsSIMD.td - x86 SIMD ISA ------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides pattern fragments useful for SIMD instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MMX specific DAG Nodes.
//===----------------------------------------------------------------------===//

// Low word of MMX to GPR.
def MMX_X86movd2w : SDNode<"X86ISD::MMX_MOVD2W", SDTypeProfile<1, 1,
                            [SDTCisVT<0, i32>, SDTCisVT<1, x86mmx>]>>;
// GPR to low word of MMX.
def MMX_X86movw2d : SDNode<"X86ISD::MMX_MOVW2D", SDTypeProfile<1, 1,
                            [SDTCisVT<0, x86mmx>, SDTCisVT<1, i32>]>>;

//===----------------------------------------------------------------------===//
// MMX Pattern Fragments
//===----------------------------------------------------------------------===//

def load_mmx : PatFrag<(ops node:$ptr), (x86mmx (load node:$ptr))>;

//===----------------------------------------------------------------------===//
// SSE specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDTX86VFCMP : SDTypeProfile<1, 3, [SDTCisFP<0>, SDTCisVec<0>,
                                       SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>,
                                       SDTCisVT<3, i8>]>;

def X86fmin    : SDNode<"X86ISD::FMIN",      SDTFPBinOp>;
def X86fmax    : SDNode<"X86ISD::FMAX",      SDTFPBinOp>;
def X86fmins   : SDNode<"X86ISD::FMINS",     SDTFPBinOp>;
def X86fmaxs   : SDNode<"X86ISD::FMAXS",     SDTFPBinOp>;

// Commutative and Associative FMIN and FMAX.
def X86fminc    : SDNode<"X86ISD::FMINC", SDTFPBinOp,
    [SDNPCommutative, SDNPAssociative]>;
def X86fmaxc    : SDNode<"X86ISD::FMAXC", SDTFPBinOp,
    [SDNPCommutative, SDNPAssociative]>;

def X86fand    : SDNode<"X86ISD::FAND",      SDTFPBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def X86for     : SDNode<"X86ISD::FOR",       SDTFPBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def X86fxor    : SDNode<"X86ISD::FXOR",      SDTFPBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def X86fandn   : SDNode<"X86ISD::FANDN",     SDTFPBinOp>;
def X86frsqrt  : SDNode<"X86ISD::FRSQRT",    SDTFPUnaryOp>;
def X86frcp    : SDNode<"X86ISD::FRCP",      SDTFPUnaryOp>;
def X86fhadd   : SDNode<"X86ISD::FHADD",     SDTFPBinOp>;
def X86fhsub   : SDNode<"X86ISD::FHSUB",     SDTFPBinOp>;
def X86hadd    : SDNode<"X86ISD::HADD",      SDTIntBinOp>;
def X86hsub    : SDNode<"X86ISD::HSUB",      SDTIntBinOp>;
def X86comi    : SDNode<"X86ISD::COMI",      SDTX86CmpTest>;
def X86ucomi   : SDNode<"X86ISD::UCOMI",     SDTX86CmpTest>;
def X86cmps    : SDNode<"X86ISD::FSETCC",     SDTX86Cmps>;
def X86pshufb  : SDNode<"X86ISD::PSHUFB",
                 SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i8>, SDTCisSameAs<0,1>,
                                      SDTCisSameAs<0,2>]>>;
def X86psadbw  : SDNode<"X86ISD::PSADBW",
                 SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i64>,
                                      SDTCVecEltisVT<1, i8>,
                                      SDTCisSameSizeAs<0,1>,
                                      SDTCisSameAs<1,2>]>, [SDNPCommutative]>;
def X86dbpsadbw : SDNode<"X86ISD::DBPSADBW",
                  SDTypeProfile<1, 3, [SDTCVecEltisVT<0, i16>,
                                       SDTCVecEltisVT<1, i8>,
                                       SDTCisSameSizeAs<0,1>,
                                       SDTCisSameAs<1,2>, SDTCisVT<3, i8>]>>;
def X86andnp   : SDNode<"X86ISD::ANDNP",
                 SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                      SDTCisSameAs<0,2>]>>;
def X86multishift   : SDNode<"X86ISD::MULTISHIFT",
                 SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                      SDTCisSameAs<1,2>]>>;
def X86pextrb  : SDNode<"X86ISD::PEXTRB",
                 SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisVT<1, v16i8>,
                                      SDTCisPtrTy<2>]>>;
def X86pextrw  : SDNode<"X86ISD::PEXTRW",
                 SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisVT<1, v8i16>,
                                      SDTCisPtrTy<2>]>>;
def X86pinsrb  : SDNode<"X86ISD::PINSRB",
                 SDTypeProfile<1, 3, [SDTCisVT<0, v16i8>, SDTCisSameAs<0,1>,
                                      SDTCisVT<2, i32>, SDTCisPtrTy<3>]>>;
def X86pinsrw  : SDNode<"X86ISD::PINSRW",
                 SDTypeProfile<1, 3, [SDTCisVT<0, v8i16>, SDTCisSameAs<0,1>,
                                      SDTCisVT<2, i32>, SDTCisPtrTy<3>]>>;
def X86insertps : SDNode<"X86ISD::INSERTPS",
                 SDTypeProfile<1, 3, [SDTCisVT<0, v4f32>, SDTCisSameAs<0,1>,
                                      SDTCisVT<2, v4f32>, SDTCisVT<3, i8>]>>;
def X86vzmovl  : SDNode<"X86ISD::VZEXT_MOVL",
                 SDTypeProfile<1, 1, [SDTCisSameAs<0,1>]>>;

def X86vzload  : SDNode<"X86ISD::VZEXT_LOAD", SDTLoad,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def SDTVtrunc    : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>,
                                        SDTCisInt<0>, SDTCisInt<1>,
                                        SDTCisOpSmallerThanOp<0, 1>]>;
def SDTVmtrunc   : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisVec<1>,
                                        SDTCisInt<0>, SDTCisInt<1>,
                                        SDTCisOpSmallerThanOp<0, 1>,
                                        SDTCisSameAs<0, 2>,
                                        SDTCVecEltisVT<3, i1>,
                                        SDTCisSameNumEltsAs<1, 3>]>;

def X86vtrunc    : SDNode<"X86ISD::VTRUNC",   SDTVtrunc>;
def X86vtruncs   : SDNode<"X86ISD::VTRUNCS",  SDTVtrunc>;
def X86vtruncus  : SDNode<"X86ISD::VTRUNCUS", SDTVtrunc>;
def X86vmtrunc   : SDNode<"X86ISD::VMTRUNC",   SDTVmtrunc>;
def X86vmtruncs  : SDNode<"X86ISD::VMTRUNCS",  SDTVmtrunc>;
def X86vmtruncus : SDNode<"X86ISD::VMTRUNCUS", SDTVmtrunc>;

def X86vfpext  : SDNode<"X86ISD::VFPEXT",
                        SDTypeProfile<1, 1, [SDTCVecEltisVT<0, f64>,
                                             SDTCVecEltisVT<1, f32>,
                                             SDTCisSameSizeAs<0, 1>]>>;
def X86vfpround: SDNode<"X86ISD::VFPROUND",
                        SDTypeProfile<1, 1, [SDTCVecEltisVT<0, f32>,
                                             SDTCVecEltisVT<1, f64>,
                                             SDTCisSameSizeAs<0, 1>]>>;

def X86froundRnd: SDNode<"X86ISD::VFPROUNDS_RND",
                        SDTypeProfile<1, 3, [SDTCVecEltisVT<0, f32>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCVecEltisVT<2, f64>,
                                             SDTCisSameSizeAs<0, 2>,
                                             SDTCisVT<3, i32>]>>;

def X86fpextRnd  : SDNode<"X86ISD::VFPEXTS_RND",
                        SDTypeProfile<1, 3, [SDTCVecEltisVT<0, f64>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCVecEltisVT<2, f32>,
                                             SDTCisSameSizeAs<0, 2>,
                                             SDTCisVT<3, i32>]>>;

def X86vmfpround: SDNode<"X86ISD::VMFPROUND",
                         SDTypeProfile<1, 3, [SDTCVecEltisVT<0, f32>,
                                              SDTCVecEltisVT<1, f64>,
                                              SDTCisSameSizeAs<0, 1>,
                                              SDTCisSameAs<0, 2>,
                                              SDTCVecEltisVT<3, i1>,
                                              SDTCisSameNumEltsAs<1, 3>]>>;

def X86vshiftimm : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                        SDTCisVT<2, i8>, SDTCisInt<0>]>;

def X86vshldq  : SDNode<"X86ISD::VSHLDQ",    X86vshiftimm>;
def X86vshrdq  : SDNode<"X86ISD::VSRLDQ",    X86vshiftimm>;
def X86cmpp    : SDNode<"X86ISD::CMPP",      SDTX86VFCMP>;
def X86pcmpeq  : SDNode<"X86ISD::PCMPEQ", SDTIntBinOp, [SDNPCommutative]>;
def X86pcmpgt  : SDNode<"X86ISD::PCMPGT", SDTIntBinOp>;

def X86CmpMaskCC :
      SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCVecEltisVT<0, i1>,
                       SDTCisVec<1>, SDTCisSameAs<2, 1>,
                       SDTCisSameNumEltsAs<0, 1>, SDTCisVT<3, i8>]>;
def X86CmpMaskCCRound :
      SDTypeProfile<1, 4, [SDTCisVec<0>,SDTCVecEltisVT<0, i1>,
                       SDTCisVec<1>, SDTCisFP<1>, SDTCisSameAs<2, 1>,
                       SDTCisSameNumEltsAs<0, 1>, SDTCisVT<3, i8>,
                       SDTCisVT<4, i32>]>;
def X86CmpMaskCCScalar :
      SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisFP<1>, SDTCisSameAs<1, 2>,
                           SDTCisVT<3, i8>]>;

def X86CmpMaskCCScalarRound :
      SDTypeProfile<1, 4, [SDTCisInt<0>, SDTCisFP<1>, SDTCisSameAs<1, 2>,
                           SDTCisVT<3, i8>, SDTCisVT<4, i32>]>;

def X86cmpm     : SDNode<"X86ISD::CMPM",     X86CmpMaskCC>;
// Hack to make CMPM commutable in tablegen patterns for load folding.
def X86cmpm_c   : SDNode<"X86ISD::CMPM",     X86CmpMaskCC, [SDNPCommutative]>;
def X86cmpmRnd  : SDNode<"X86ISD::CMPM_RND", X86CmpMaskCCRound>;
def X86cmpms    : SDNode<"X86ISD::FSETCCM",   X86CmpMaskCCScalar>;
def X86cmpmsRnd : SDNode<"X86ISD::FSETCCM_RND",   X86CmpMaskCCScalarRound>;

def X86phminpos: SDNode<"X86ISD::PHMINPOS", 
                 SDTypeProfile<1, 1, [SDTCisVT<0, v8i16>, SDTCisVT<1, v8i16>]>>;

def X86vshiftuniform : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                            SDTCisVec<2>, SDTCisInt<0>,
                                            SDTCisInt<2>]>;

def X86vshl    : SDNode<"X86ISD::VSHL", X86vshiftuniform>;
def X86vsrl    : SDNode<"X86ISD::VSRL", X86vshiftuniform>;
def X86vsra    : SDNode<"X86ISD::VSRA", X86vshiftuniform>;

def X86vshiftvariable : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>, SDTCisInt<0>]>;

def X86vshlv   : SDNode<"X86ISD::VSHLV", X86vshiftvariable>;
def X86vsrlv   : SDNode<"X86ISD::VSRLV", X86vshiftvariable>;
def X86vsrav   : SDNode<"X86ISD::VSRAV", X86vshiftvariable>;

def X86vshli   : SDNode<"X86ISD::VSHLI", X86vshiftimm>;
def X86vsrli   : SDNode<"X86ISD::VSRLI", X86vshiftimm>;
def X86vsrai   : SDNode<"X86ISD::VSRAI", X86vshiftimm>;

def X86kshiftl : SDNode<"X86ISD::KSHIFTL",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i1>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>]>>;
def X86kshiftr : SDNode<"X86ISD::KSHIFTR",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i1>,
                                             SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i8>]>>;

def X86kadd : SDNode<"X86ISD::KADD", SDTIntBinOp, [SDNPCommutative]>;

def X86vrotli  : SDNode<"X86ISD::VROTLI", X86vshiftimm>;
def X86vrotri  : SDNode<"X86ISD::VROTRI", X86vshiftimm>;

def X86vpshl   : SDNode<"X86ISD::VPSHL", X86vshiftvariable>;
def X86vpsha   : SDNode<"X86ISD::VPSHA", X86vshiftvariable>;

def X86vpcom   : SDNode<"X86ISD::VPCOM",
                        SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisVT<3, i8>, SDTCisInt<0>]>>;
def X86vpcomu  : SDNode<"X86ISD::VPCOMU",
                        SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisVT<3, i8>, SDTCisInt<0>]>>;
def X86vpermil2 : SDNode<"X86ISD::VPERMIL2",
                        SDTypeProfile<1, 4, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisFP<0>, SDTCisInt<3>,
                                             SDTCisSameNumEltsAs<0, 3>,
                                             SDTCisSameSizeAs<0,3>,
                                             SDTCisVT<4, i8>]>>;
def X86vpperm : SDNode<"X86ISD::VPPERM",
                        SDTypeProfile<1, 3, [SDTCisVT<0, v16i8>, SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>, SDTCisSameAs<0, 3>]>>;

def SDTX86CmpPTest : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                          SDTCisVec<1>,
                                          SDTCisSameAs<2, 1>]>;

def X86mulhrs  : SDNode<"X86ISD::MULHRS", SDTIntBinOp, [SDNPCommutative]>;
def X86avg     : SDNode<"X86ISD::AVG" , SDTIntBinOp, [SDNPCommutative]>;
def X86ptest   : SDNode<"X86ISD::PTEST", SDTX86CmpPTest>;
def X86testp   : SDNode<"X86ISD::TESTP", SDTX86CmpPTest>;
def X86kortest : SDNode<"X86ISD::KORTEST", SDTX86CmpPTest>;
def X86ktest   : SDNode<"X86ISD::KTEST", SDTX86CmpPTest>;

def X86movmsk : SDNode<"X86ISD::MOVMSK",
                        SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVec<1>]>>;

def X86selects : SDNode<"X86ISD::SELECTS",
                        SDTypeProfile<1, 3, [SDTCisVT<1, v1i1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<2, 3>]>>;

def X86pmuludq : SDNode<"X86ISD::PMULUDQ",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i64>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<1,2>]>,
                                             [SDNPCommutative]>;
def X86pmuldq  : SDNode<"X86ISD::PMULDQ",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i64>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<1,2>]>,
                                             [SDNPCommutative]>;

def X86extrqi : SDNode<"X86ISD::EXTRQI",
                  SDTypeProfile<1, 3, [SDTCisVT<0, v2i64>, SDTCisSameAs<0,1>,
                                       SDTCisVT<2, i8>, SDTCisVT<3, i8>]>>;
def X86insertqi : SDNode<"X86ISD::INSERTQI",
                    SDTypeProfile<1, 4, [SDTCisVT<0, v2i64>, SDTCisSameAs<0,1>,
                                         SDTCisSameAs<1,2>, SDTCisVT<3, i8>,
                                         SDTCisVT<4, i8>]>>;

// Specific shuffle nodes - At some point ISD::VECTOR_SHUFFLE will always get
// translated into one of the target nodes below during lowering.
// Note: this is a work in progress...
def SDTShuff1Op : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def SDTShuff2Op : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                SDTCisSameAs<0,2>]>;
def SDTShuff2OpFP : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisFP<0>,
                                         SDTCisSameAs<0,1>, SDTCisSameAs<0,2>]>;

def SDTShuff2OpM : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                        SDTCisFP<0>, SDTCisInt<2>,
                                        SDTCisSameNumEltsAs<0,2>,
                                        SDTCisSameSizeAs<0,2>]>;
def SDTShuff2OpI : SDTypeProfile<1, 2, [SDTCisVec<0>,
                                 SDTCisSameAs<0,1>, SDTCisVT<2, i8>]>;
def SDTShuff3OpI : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                 SDTCisSameAs<0,2>, SDTCisVT<3, i8>]>;
def SDTFPBinOpImm: SDTypeProfile<1, 3, [SDTCisFP<0>, SDTCisVec<0>,
                                        SDTCisSameAs<0,1>,
                                        SDTCisSameAs<0,2>,
                                        SDTCisVT<3, i32>]>;
def SDTFPBinOpImmRound: SDTypeProfile<1, 4, [SDTCisFP<0>, SDTCisVec<0>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisVT<3, i32>,
                                             SDTCisVT<4, i32>]>;
def SDTFPTernaryOpImmRound: SDTypeProfile<1, 5, [SDTCisFP<0>, SDTCisSameAs<0,1>,
                                                 SDTCisSameAs<0,2>,
                                                 SDTCisInt<3>,
                                                 SDTCisSameSizeAs<0, 3>,
                                                 SDTCisSameNumEltsAs<0, 3>,
                                                 SDTCisVT<4, i32>,
                                                 SDTCisVT<5, i32>]>;
def SDTFPUnaryOpImm: SDTypeProfile<1, 2, [SDTCisFP<0>, SDTCisVec<0>,
                                          SDTCisSameAs<0,1>,
                                          SDTCisVT<2, i32>]>;
def SDTFPUnaryOpImmRound: SDTypeProfile<1, 3, [SDTCisFP<0>, SDTCisVec<0>,
                                               SDTCisSameAs<0,1>,
                                               SDTCisVT<2, i32>,
                                               SDTCisVT<3, i32>]>;

def SDTVBroadcast  : SDTypeProfile<1, 1, [SDTCisVec<0>]>;
def SDTVBroadcastm : SDTypeProfile<1, 1, [SDTCisVec<0>,
                                          SDTCisInt<0>, SDTCisInt<1>]>;

def SDTBlend : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                             SDTCisSameAs<1,2>, SDTCisVT<3, i8>]>;

def SDTTernlog  : SDTypeProfile<1, 4, [SDTCisInt<0>, SDTCisVec<0>,
                                       SDTCisSameAs<0,1>, SDTCisSameAs<0,2>,
                                       SDTCisSameAs<0,3>, SDTCisVT<4, i8>]>;

def SDTFPBinOpRound : SDTypeProfile<1, 3, [      // fadd_round, fmul_round, etc.
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisFP<0>, SDTCisVT<3, i32>]>;

def SDTFPUnaryOpRound : SDTypeProfile<1, 2, [      // fsqrt_round, fgetexp_round, etc.
  SDTCisSameAs<0, 1>, SDTCisFP<0>, SDTCisVT<2, i32>]>;

def SDTFmaRound : SDTypeProfile<1, 4, [SDTCisSameAs<0,1>,
                           SDTCisSameAs<1,2>, SDTCisSameAs<1,3>,
                           SDTCisFP<0>, SDTCisVT<4, i32>]>;

def X86PAlignr : SDNode<"X86ISD::PALIGNR",
                        SDTypeProfile<1, 3, [SDTCVecEltisVT<0, i8>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisVT<3, i8>]>>;
def X86VAlign  : SDNode<"X86ISD::VALIGN", SDTShuff3OpI>;

def X86VShld   : SDNode<"X86ISD::VSHLD", SDTShuff3OpI>;
def X86VShrd   : SDNode<"X86ISD::VSHRD", SDTShuff3OpI>;
def X86VShldv  : SDNode<"X86ISD::VSHLDV",
                        SDTypeProfile<1, 3, [SDTCisVec<0>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisSameAs<0,3>]>>;
def X86VShrdv  : SDNode<"X86ISD::VSHRDV",
                        SDTypeProfile<1, 3, [SDTCisVec<0>,
                                             SDTCisSameAs<0,1>,
                                             SDTCisSameAs<0,2>,
                                             SDTCisSameAs<0,3>]>>;

def X86Conflict : SDNode<"X86ISD::CONFLICT", SDTIntUnaryOp>;

def X86PShufd  : SDNode<"X86ISD::PSHUFD", SDTShuff2OpI>;
def X86PShufhw : SDNode<"X86ISD::PSHUFHW", SDTShuff2OpI>;
def X86PShuflw : SDNode<"X86ISD::PSHUFLW", SDTShuff2OpI>;

def X86Shufp   : SDNode<"X86ISD::SHUFP", SDTShuff3OpI>;
def X86Shuf128 : SDNode<"X86ISD::SHUF128", SDTShuff3OpI>;

def X86Movddup  : SDNode<"X86ISD::MOVDDUP", SDTShuff1Op>;
def X86Movshdup : SDNode<"X86ISD::MOVSHDUP", SDTShuff1Op>;
def X86Movsldup : SDNode<"X86ISD::MOVSLDUP", SDTShuff1Op>;

def X86Movsd : SDNode<"X86ISD::MOVSD", SDTShuff2OpFP>;
def X86Movss : SDNode<"X86ISD::MOVSS", SDTShuff2OpFP>;

def X86Movlhps : SDNode<"X86ISD::MOVLHPS", SDTShuff2OpFP>;
def X86Movhlps : SDNode<"X86ISD::MOVHLPS", SDTShuff2OpFP>;

def SDTPack : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisInt<0>,
                                   SDTCisVec<1>, SDTCisInt<1>,
                                   SDTCisSameSizeAs<0,1>,
                                   SDTCisSameAs<1,2>,
                                   SDTCisOpSmallerThanOp<0, 1>]>;
def X86Packss : SDNode<"X86ISD::PACKSS", SDTPack>;
def X86Packus : SDNode<"X86ISD::PACKUS", SDTPack>;

def X86Unpckl : SDNode<"X86ISD::UNPCKL", SDTShuff2Op>;
def X86Unpckh : SDNode<"X86ISD::UNPCKH", SDTShuff2Op>;

def X86vpmaddubsw  : SDNode<"X86ISD::VPMADDUBSW",
                            SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i16>,
                                                 SDTCVecEltisVT<1, i8>,
                                                 SDTCisSameSizeAs<0,1>,
                                                 SDTCisSameAs<1,2>]>>;
def X86vpmaddwd    : SDNode<"X86ISD::VPMADDWD",
                            SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i32>,
                                                 SDTCVecEltisVT<1, i16>,
                                                 SDTCisSameSizeAs<0,1>,
                                                 SDTCisSameAs<1,2>]>,
                            [SDNPCommutative]>;

def X86VPermilpv  : SDNode<"X86ISD::VPERMILPV", SDTShuff2OpM>;
def X86VPermilpi  : SDNode<"X86ISD::VPERMILPI", SDTShuff2OpI>;
def X86VPermv     : SDNode<"X86ISD::VPERMV",
                           SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisInt<1>,
                                                SDTCisSameNumEltsAs<0,1>,
                                                SDTCisSameSizeAs<0,1>,
                                                SDTCisSameAs<0,2>]>>;
def X86VPermi     : SDNode<"X86ISD::VPERMI",    SDTShuff2OpI>;
def X86VPermt2     : SDNode<"X86ISD::VPERMV3",
                    SDTypeProfile<1, 3, [SDTCisVec<0>,
                                         SDTCisSameAs<0,1>, SDTCisInt<2>,
                                         SDTCisVec<2>, SDTCisSameNumEltsAs<0, 2>,
                                         SDTCisSameSizeAs<0,2>,
                                         SDTCisSameAs<0,3>]>, []>;

def X86vpternlog  : SDNode<"X86ISD::VPTERNLOG", SDTTernlog>;

def X86VPerm2x128 : SDNode<"X86ISD::VPERM2X128", SDTShuff3OpI>;

def X86VFixupimm   : SDNode<"X86ISD::VFIXUPIMM", SDTFPTernaryOpImmRound>;
def X86VFixupimmScalar   : SDNode<"X86ISD::VFIXUPIMMS", SDTFPTernaryOpImmRound>;
def X86VRange      : SDNode<"X86ISD::VRANGE",        SDTFPBinOpImm>;
def X86VRangeRnd   : SDNode<"X86ISD::VRANGE_RND",    SDTFPBinOpImmRound>;
def X86VReduce     : SDNode<"X86ISD::VREDUCE",       SDTFPUnaryOpImm>;
def X86VReduceRnd  : SDNode<"X86ISD::VREDUCE_RND",   SDTFPUnaryOpImmRound>;
def X86VRndScale   : SDNode<"X86ISD::VRNDSCALE",     SDTFPUnaryOpImm>;
def X86VRndScaleRnd: SDNode<"X86ISD::VRNDSCALE_RND", SDTFPUnaryOpImmRound>;
def X86VGetMant    : SDNode<"X86ISD::VGETMANT",      SDTFPUnaryOpImm>;
def X86VGetMantRnd : SDNode<"X86ISD::VGETMANT_RND",  SDTFPUnaryOpImmRound>;
def X86Vfpclass    : SDNode<"X86ISD::VFPCLASS",
                       SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i1>,
                                            SDTCisFP<1>,
                                            SDTCisSameNumEltsAs<0,1>,
                                            SDTCisVT<2, i32>]>, []>;
def X86Vfpclasss   : SDNode<"X86ISD::VFPCLASSS",
                       SDTypeProfile<1, 2, [SDTCisVT<0, v1i1>,
                                            SDTCisFP<1>, SDTCisVT<2, i32>]>,[]>;

def X86SubVBroadcast : SDNode<"X86ISD::SUBV_BROADCAST",
                    SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>,
                                         SDTCisSubVecOfVec<1, 0>]>, []>;

def X86VBroadcast : SDNode<"X86ISD::VBROADCAST", SDTVBroadcast>;
def X86VBroadcastm : SDNode<"X86ISD::VBROADCASTM", SDTVBroadcastm>;

def X86Blendi    : SDNode<"X86ISD::BLENDI",   SDTBlend>;

def X86Addsub    : SDNode<"X86ISD::ADDSUB", SDTFPBinOp>;

def X86faddRnd   : SDNode<"X86ISD::FADD_RND",  SDTFPBinOpRound>;
def X86faddRnds  : SDNode<"X86ISD::FADDS_RND", SDTFPBinOpRound>;
def X86fsubRnd   : SDNode<"X86ISD::FSUB_RND",  SDTFPBinOpRound>;
def X86fsubRnds  : SDNode<"X86ISD::FSUBS_RND", SDTFPBinOpRound>;
def X86fmulRnd   : SDNode<"X86ISD::FMUL_RND",  SDTFPBinOpRound>;
def X86fmulRnds  : SDNode<"X86ISD::FMULS_RND", SDTFPBinOpRound>;
def X86fdivRnd   : SDNode<"X86ISD::FDIV_RND",  SDTFPBinOpRound>;
def X86fdivRnds  : SDNode<"X86ISD::FDIVS_RND", SDTFPBinOpRound>;
def X86fmaxRnd   : SDNode<"X86ISD::FMAX_RND",  SDTFPBinOpRound>;
def X86fmaxRnds  : SDNode<"X86ISD::FMAXS_RND", SDTFPBinOpRound>;
def X86fminRnd   : SDNode<"X86ISD::FMIN_RND",  SDTFPBinOpRound>;
def X86fminRnds  : SDNode<"X86ISD::FMINS_RND", SDTFPBinOpRound>;
def X86scalef    : SDNode<"X86ISD::SCALEF",         SDTFPBinOpRound>;
def X86scalefs   : SDNode<"X86ISD::SCALEFS",        SDTFPBinOpRound>;
def X86fsqrtRnd     : SDNode<"X86ISD::FSQRT_RND",   SDTFPUnaryOpRound>;
def X86fsqrtRnds    : SDNode<"X86ISD::FSQRTS_RND", SDTFPBinOpRound>;
def X86fgetexpRnd   : SDNode<"X86ISD::FGETEXP_RND", SDTFPUnaryOpRound>;
def X86fgetexpRnds  : SDNode<"X86ISD::FGETEXPS_RND", SDTFPBinOpRound>;

def X86Fmadd     : SDNode<"ISD::FMA",          SDTFPTernaryOp, [SDNPCommutative]>;
def X86Fnmadd    : SDNode<"X86ISD::FNMADD",    SDTFPTernaryOp, [SDNPCommutative]>;
def X86Fmsub     : SDNode<"X86ISD::FMSUB",     SDTFPTernaryOp, [SDNPCommutative]>;
def X86Fnmsub    : SDNode<"X86ISD::FNMSUB",    SDTFPTernaryOp, [SDNPCommutative]>;
def X86Fmaddsub  : SDNode<"X86ISD::FMADDSUB",  SDTFPTernaryOp, [SDNPCommutative]>;
def X86Fmsubadd  : SDNode<"X86ISD::FMSUBADD",  SDTFPTernaryOp, [SDNPCommutative]>;

def X86FmaddRnd     : SDNode<"X86ISD::FMADD_RND",     SDTFmaRound, [SDNPCommutative]>;
def X86FnmaddRnd    : SDNode<"X86ISD::FNMADD_RND",    SDTFmaRound, [SDNPCommutative]>;
def X86FmsubRnd     : SDNode<"X86ISD::FMSUB_RND",     SDTFmaRound, [SDNPCommutative]>;
def X86FnmsubRnd    : SDNode<"X86ISD::FNMSUB_RND",    SDTFmaRound, [SDNPCommutative]>;
def X86FmaddsubRnd  : SDNode<"X86ISD::FMADDSUB_RND",  SDTFmaRound, [SDNPCommutative]>;
def X86FmsubaddRnd  : SDNode<"X86ISD::FMSUBADD_RND",  SDTFmaRound, [SDNPCommutative]>;

def SDTIFma : SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisSameAs<0,1>,
                           SDTCisSameAs<1,2>, SDTCisSameAs<1,3>]>;
def x86vpmadd52l     : SDNode<"X86ISD::VPMADD52L",     SDTIFma, [SDNPCommutative]>;
def x86vpmadd52h     : SDNode<"X86ISD::VPMADD52H",     SDTIFma, [SDNPCommutative]>;

def X86rsqrt14   : SDNode<"X86ISD::RSQRT14",  SDTFPUnaryOp>;
def X86rcp14     : SDNode<"X86ISD::RCP14",    SDTFPUnaryOp>;

// VNNI
def SDTVnni : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                   SDTCisSameAs<1,2>, SDTCisSameAs<1,3>]>;
def X86Vpdpbusd  : SDNode<"X86ISD::VPDPBUSD", SDTVnni>;
def X86Vpdpbusds : SDNode<"X86ISD::VPDPBUSDS", SDTVnni>;
def X86Vpdpwssd  : SDNode<"X86ISD::VPDPWSSD", SDTVnni>;
def X86Vpdpwssds : SDNode<"X86ISD::VPDPWSSDS", SDTVnni>;

def X86rsqrt28   : SDNode<"X86ISD::RSQRT28",  SDTFPUnaryOpRound>;
def X86rcp28     : SDNode<"X86ISD::RCP28",    SDTFPUnaryOpRound>;
def X86exp2      : SDNode<"X86ISD::EXP2",     SDTFPUnaryOpRound>;

def X86rsqrt14s  : SDNode<"X86ISD::RSQRT14S",   SDTFPBinOp>;
def X86rcp14s    : SDNode<"X86ISD::RCP14S",     SDTFPBinOp>;
def X86rsqrt28s  : SDNode<"X86ISD::RSQRT28S",   SDTFPBinOpRound>;
def X86rcp28s    : SDNode<"X86ISD::RCP28S",     SDTFPBinOpRound>;
def X86Ranges    : SDNode<"X86ISD::VRANGES",    SDTFPBinOpImm>;
def X86RndScales : SDNode<"X86ISD::VRNDSCALES", SDTFPBinOpImm>;
def X86Reduces   : SDNode<"X86ISD::VREDUCES",   SDTFPBinOpImm>;
def X86GetMants  : SDNode<"X86ISD::VGETMANTS",  SDTFPBinOpImm>;
def X86RangesRnd    : SDNode<"X86ISD::VRANGES_RND",    SDTFPBinOpImmRound>;
def X86RndScalesRnd : SDNode<"X86ISD::VRNDSCALES_RND", SDTFPBinOpImmRound>;
def X86ReducesRnd   : SDNode<"X86ISD::VREDUCES_RND",   SDTFPBinOpImmRound>;
def X86GetMantsRnd  : SDNode<"X86ISD::VGETMANTS_RND",  SDTFPBinOpImmRound>;

def X86compress: SDNode<"X86ISD::COMPRESS", SDTypeProfile<1, 1,
                              [SDTCisSameAs<0, 1>, SDTCisVec<1>]>, []>;
def X86expand  : SDNode<"X86ISD::EXPAND", SDTypeProfile<1, 1,
                              [SDTCisSameAs<0, 1>, SDTCisVec<1>]>, []>;

// vpshufbitqmb
def X86Vpshufbitqmb : SDNode<"X86ISD::VPSHUFBITQMB",
                             SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                                  SDTCisSameAs<1,2>,
                                                  SDTCVecEltisVT<0,i1>,
                                                  SDTCisSameNumEltsAs<0,1>]>>;

def SDTintToFPRound: SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisFP<0>,
                                          SDTCisSameAs<0,1>, SDTCisInt<2>,
                                          SDTCisVT<3, i32>]>;

def SDTFloatToInt: SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>,
                                        SDTCisInt<0>, SDTCisFP<1>]>;
def SDTFloatToIntRnd: SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                           SDTCisInt<0>, SDTCisFP<1>,
                                           SDTCisVT<2, i32>]>;
def SDTSFloatToInt: SDTypeProfile<1, 1, [SDTCisInt<0>, SDTCisFP<1>,
                                         SDTCisVec<1>]>;
def SDTSFloatToIntRnd: SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisFP<1>,
                                            SDTCisVec<1>, SDTCisVT<2, i32>]>;

def SDTVintToFP: SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVec<1>,
                                      SDTCisFP<0>, SDTCisInt<1>]>;
def SDTVintToFPRound: SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                           SDTCisFP<0>, SDTCisInt<1>,
                                           SDTCisVT<2, i32>]>;

// Scalar
def X86SintToFpRnd  : SDNode<"X86ISD::SCALAR_SINT_TO_FP_RND",  SDTintToFPRound>;
def X86UintToFpRnd  : SDNode<"X86ISD::SCALAR_UINT_TO_FP_RND",  SDTintToFPRound>;

def X86cvtts2Int  : SDNode<"X86ISD::CVTTS2SI",  SDTSFloatToInt>;
def X86cvtts2UInt : SDNode<"X86ISD::CVTTS2UI",  SDTSFloatToInt>;
def X86cvtts2IntRnd  : SDNode<"X86ISD::CVTTS2SI_RND",  SDTSFloatToIntRnd>;
def X86cvtts2UIntRnd : SDNode<"X86ISD::CVTTS2UI_RND",  SDTSFloatToIntRnd>;

def X86cvts2si  : SDNode<"X86ISD::CVTS2SI", SDTSFloatToInt>;
def X86cvts2usi : SDNode<"X86ISD::CVTS2UI", SDTSFloatToInt>;
def X86cvts2siRnd  : SDNode<"X86ISD::CVTS2SI_RND", SDTSFloatToIntRnd>;
def X86cvts2usiRnd : SDNode<"X86ISD::CVTS2UI_RND", SDTSFloatToIntRnd>;

// Vector with rounding mode

// cvtt fp-to-int staff
def X86cvttp2siRnd    : SDNode<"X86ISD::CVTTP2SI_RND", SDTFloatToIntRnd>;
def X86cvttp2uiRnd    : SDNode<"X86ISD::CVTTP2UI_RND", SDTFloatToIntRnd>;

def X86VSintToFpRnd   : SDNode<"X86ISD::SINT_TO_FP_RND",  SDTVintToFPRound>;
def X86VUintToFpRnd   : SDNode<"X86ISD::UINT_TO_FP_RND",  SDTVintToFPRound>;

// cvt fp-to-int staff
def X86cvtp2IntRnd      : SDNode<"X86ISD::CVTP2SI_RND",  SDTFloatToIntRnd>;
def X86cvtp2UIntRnd     : SDNode<"X86ISD::CVTP2UI_RND",  SDTFloatToIntRnd>;

// Vector without rounding mode

// cvtt fp-to-int staff
def X86cvttp2si      : SDNode<"X86ISD::CVTTP2SI",  SDTFloatToInt>;
def X86cvttp2ui      : SDNode<"X86ISD::CVTTP2UI",  SDTFloatToInt>;

def X86VSintToFP      : SDNode<"X86ISD::CVTSI2P",  SDTVintToFP>;
def X86VUintToFP      : SDNode<"X86ISD::CVTUI2P",  SDTVintToFP>;

// cvt int-to-fp staff
def X86cvtp2Int      : SDNode<"X86ISD::CVTP2SI",  SDTFloatToInt>;
def X86cvtp2UInt     : SDNode<"X86ISD::CVTP2UI",  SDTFloatToInt>;


def SDTMFloatToInt: SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisVec<1>,
                                         SDTCisInt<0>, SDTCisFP<1>,
                                         SDTCisSameSizeAs<0, 1>,
                                         SDTCisSameAs<0, 2>,
                                         SDTCVecEltisVT<3, i1>,
                                         SDTCisSameNumEltsAs<1, 3>]>;

def X86mcvtp2Int     : SDNode<"X86ISD::MCVTP2SI",  SDTMFloatToInt>;
def X86mcvtp2UInt    : SDNode<"X86ISD::MCVTP2UI",  SDTMFloatToInt>;
def X86mcvttp2si     : SDNode<"X86ISD::MCVTTP2SI", SDTMFloatToInt>;
def X86mcvttp2ui     : SDNode<"X86ISD::MCVTTP2UI", SDTMFloatToInt>;


def X86cvtph2ps     : SDNode<"X86ISD::CVTPH2PS",
                              SDTypeProfile<1, 1, [SDTCVecEltisVT<0, f32>,
                                                   SDTCVecEltisVT<1, i16>]> >;

def X86cvtph2psRnd  : SDNode<"X86ISD::CVTPH2PS_RND",
                              SDTypeProfile<1, 2, [SDTCVecEltisVT<0, f32>,
                                                   SDTCVecEltisVT<1, i16>,
                                                   SDTCisVT<2, i32>]> >;

def X86cvtps2ph   : SDNode<"X86ISD::CVTPS2PH",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, i16>,
                                             SDTCVecEltisVT<1, f32>,
                                             SDTCisVT<2, i32>]> >;
def X86mcvtps2ph   : SDNode<"X86ISD::MCVTPS2PH",
                        SDTypeProfile<1, 4, [SDTCVecEltisVT<0, i16>,
                                             SDTCVecEltisVT<1, f32>,
                                             SDTCisVT<2, i32>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCVecEltisVT<4, i1>,
                                             SDTCisSameNumEltsAs<1, 4>]> >;
def X86vfpextRnd  : SDNode<"X86ISD::VFPEXT_RND",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, f64>,
                                             SDTCVecEltisVT<1, f32>,
                                             SDTCisOpSmallerThanOp<1, 0>,
                                             SDTCisVT<2, i32>]>>;
def X86vfproundRnd: SDNode<"X86ISD::VFPROUND_RND",
                        SDTypeProfile<1, 2, [SDTCVecEltisVT<0, f32>,
                                             SDTCVecEltisVT<1, f64>,
                                             SDTCisOpSmallerThanOp<0, 1>,
                                             SDTCisVT<2, i32>]>>;

// galois field arithmetic
def X86GF2P8affineinvqb : SDNode<"X86ISD::GF2P8AFFINEINVQB", SDTBlend>;
def X86GF2P8affineqb    : SDNode<"X86ISD::GF2P8AFFINEQB", SDTBlend>;
def X86GF2P8mulb        : SDNode<"X86ISD::GF2P8MULB", SDTIntBinOp>;

//===----------------------------------------------------------------------===//
// SSE Complex Patterns
//===----------------------------------------------------------------------===//

// These are 'extloads' from a scalar to the low element of a vector, zeroing
// the top elements.  These are used for the SSE 'ss' and 'sd' instruction
// forms.
def sse_load_f32 : ComplexPattern<v4f32, 5, "selectScalarSSELoad", [],
                                  [SDNPHasChain, SDNPMayLoad, SDNPMemOperand,
                                   SDNPWantRoot, SDNPWantParent]>;
def sse_load_f64 : ComplexPattern<v2f64, 5, "selectScalarSSELoad", [],
                                  [SDNPHasChain, SDNPMayLoad, SDNPMemOperand,
                                   SDNPWantRoot, SDNPWantParent]>;

def ssmem : Operand<v4f32> {
  let PrintMethod = "printf32mem";
  let MIOperandInfo = (ops ptr_rc, i8imm, ptr_rc_nosp, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86Mem32AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}
def sdmem : Operand<v2f64> {
  let PrintMethod = "printf64mem";
  let MIOperandInfo = (ops ptr_rc, i8imm, ptr_rc_nosp, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86Mem64AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

//===----------------------------------------------------------------------===//
// SSE pattern fragments
//===----------------------------------------------------------------------===//

// 128-bit load pattern fragments
def loadv4f32    : PatFrag<(ops node:$ptr), (v4f32 (load node:$ptr))>;
def loadv2f64    : PatFrag<(ops node:$ptr), (v2f64 (load node:$ptr))>;
def loadv2i64    : PatFrag<(ops node:$ptr), (v2i64 (load node:$ptr))>;
def loadv4i32    : PatFrag<(ops node:$ptr), (v4i32 (load node:$ptr))>;
def loadv8i16    : PatFrag<(ops node:$ptr), (v8i16 (load node:$ptr))>;
def loadv16i8    : PatFrag<(ops node:$ptr), (v16i8 (load node:$ptr))>;

// 256-bit load pattern fragments
def loadv8f32    : PatFrag<(ops node:$ptr), (v8f32  (load node:$ptr))>;
def loadv4f64    : PatFrag<(ops node:$ptr), (v4f64  (load node:$ptr))>;
def loadv4i64    : PatFrag<(ops node:$ptr), (v4i64  (load node:$ptr))>;
def loadv8i32    : PatFrag<(ops node:$ptr), (v8i32  (load node:$ptr))>;
def loadv16i16   : PatFrag<(ops node:$ptr), (v16i16 (load node:$ptr))>;
def loadv32i8    : PatFrag<(ops node:$ptr), (v32i8  (load node:$ptr))>;

// 512-bit load pattern fragments
def loadv16f32   : PatFrag<(ops node:$ptr), (v16f32 (load node:$ptr))>;
def loadv8f64    : PatFrag<(ops node:$ptr), (v8f64  (load node:$ptr))>;
def loadv8i64    : PatFrag<(ops node:$ptr), (v8i64  (load node:$ptr))>;
def loadv16i32   : PatFrag<(ops node:$ptr), (v16i32 (load node:$ptr))>;
def loadv32i16   : PatFrag<(ops node:$ptr), (v32i16 (load node:$ptr))>;
def loadv64i8    : PatFrag<(ops node:$ptr), (v64i8  (load node:$ptr))>;

// 128-/256-/512-bit extload pattern fragments
def extloadv2f32 : PatFrag<(ops node:$ptr), (v2f64 (extloadvf32 node:$ptr))>;
def extloadv4f32 : PatFrag<(ops node:$ptr), (v4f64 (extloadvf32 node:$ptr))>;
def extloadv8f32 : PatFrag<(ops node:$ptr), (v8f64 (extloadvf32 node:$ptr))>;

// Like 'store', but always requires vector size alignment.
def alignedstore : PatFrag<(ops node:$val, node:$ptr),
                           (store node:$val, node:$ptr), [{
  auto *St = cast<StoreSDNode>(N);
  return St->getAlignment() >= St->getMemoryVT().getStoreSize();
}]>;

// Like 'load', but always requires vector size alignment.
def alignedload : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  auto *Ld = cast<LoadSDNode>(N);
  return Ld->getAlignment() >= Ld->getMemoryVT().getStoreSize();
}]>;

// 128-bit aligned load pattern fragments
// NOTE: all 128-bit integer vector loads are promoted to v2i64
def alignedloadv4f32 : PatFrag<(ops node:$ptr),
                               (v4f32 (alignedload node:$ptr))>;
def alignedloadv2f64 : PatFrag<(ops node:$ptr),
                               (v2f64 (alignedload node:$ptr))>;
def alignedloadv2i64 : PatFrag<(ops node:$ptr),
                               (v2i64 (alignedload node:$ptr))>;
def alignedloadv4i32 : PatFrag<(ops node:$ptr),
                               (v4i32 (alignedload node:$ptr))>;
def alignedloadv8i16 : PatFrag<(ops node:$ptr),
                               (v8i16 (alignedload node:$ptr))>;
def alignedloadv16i8 : PatFrag<(ops node:$ptr),
                               (v16i8 (alignedload node:$ptr))>;

// 256-bit aligned load pattern fragments
// NOTE: all 256-bit integer vector loads are promoted to v4i64
def alignedloadv8f32  : PatFrag<(ops node:$ptr),
                                (v8f32  (alignedload node:$ptr))>;
def alignedloadv4f64  : PatFrag<(ops node:$ptr),
                                (v4f64  (alignedload node:$ptr))>;
def alignedloadv4i64  : PatFrag<(ops node:$ptr),
                                (v4i64  (alignedload node:$ptr))>;
def alignedloadv8i32  : PatFrag<(ops node:$ptr),
                                (v8i32  (alignedload node:$ptr))>;
def alignedloadv16i16 : PatFrag<(ops node:$ptr),
                                (v16i16 (alignedload node:$ptr))>;
def alignedloadv32i8  : PatFrag<(ops node:$ptr),
                                (v32i8  (alignedload node:$ptr))>;

// 512-bit aligned load pattern fragments
def alignedloadv16f32 : PatFrag<(ops node:$ptr),
                                (v16f32 (alignedload node:$ptr))>;
def alignedloadv8f64  : PatFrag<(ops node:$ptr),
                                (v8f64  (alignedload node:$ptr))>;
def alignedloadv8i64  : PatFrag<(ops node:$ptr),
                                (v8i64  (alignedload node:$ptr))>;
def alignedloadv16i32 : PatFrag<(ops node:$ptr),
                                (v16i32 (alignedload node:$ptr))>;
def alignedloadv32i16 : PatFrag<(ops node:$ptr),
                                (v32i16 (alignedload node:$ptr))>;
def alignedloadv64i8  : PatFrag<(ops node:$ptr),
                                (v64i8  (alignedload node:$ptr))>;

// Like 'load', but uses special alignment checks suitable for use in
// memory operands in most SSE instructions, which are required to
// be naturally aligned on some targets but not on others.  If the subtarget
// allows unaligned accesses, match any load, though this may require
// setting a feature bit in the processor (on startup, for example).
// Opteron 10h and later implement such a feature.
def memop : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  auto *Ld = cast<LoadSDNode>(N);
  return Subtarget->hasSSEUnalignedMem() ||
         Ld->getAlignment() >= Ld->getMemoryVT().getStoreSize();
}]>;

// 128-bit memop pattern fragments
// NOTE: all 128-bit integer vector loads are promoted to v2i64
def memopv4f32 : PatFrag<(ops node:$ptr), (v4f32 (memop node:$ptr))>;
def memopv2f64 : PatFrag<(ops node:$ptr), (v2f64 (memop node:$ptr))>;
def memopv2i64 : PatFrag<(ops node:$ptr), (v2i64 (memop node:$ptr))>;
def memopv4i32 : PatFrag<(ops node:$ptr), (v4i32 (memop node:$ptr))>;
def memopv8i16 : PatFrag<(ops node:$ptr), (v8i16 (memop node:$ptr))>;
def memopv16i8 : PatFrag<(ops node:$ptr), (v16i8 (memop node:$ptr))>;

def X86masked_gather : SDNode<"X86ISD::MGATHER",
                              SDTypeProfile<2, 3, [SDTCisVec<0>,
                                                   SDTCisVec<1>, SDTCisInt<1>,
                                                   SDTCisSameAs<0, 2>,
                                                   SDTCisSameAs<1, 3>,
                                                   SDTCisPtrTy<4>]>,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def X86masked_scatter : SDNode<"X86ISD::MSCATTER",
                              SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisVec<1>,
                                                   SDTCisSameAs<0, 2>,
                                                   SDTCVecEltisVT<0, i1>,
                                                   SDTCisPtrTy<3>]>,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def mgatherv4i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v4i32;
}]>;

def mgatherv8i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v8i32;
}]>;

def mgatherv2i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v2i64;
}]>;
def mgatherv4i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v4i64;
}]>;
def mgatherv8i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v8i64;
}]>;
def mgatherv16i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_gather node:$src1, node:$src2, node:$src3) , [{
  X86MaskedGatherSDNode *Mgt = cast<X86MaskedGatherSDNode>(N);
  return Mgt->getIndex().getValueType() == MVT::v16i32;
}]>;

def mscatterv2i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v2i64;
}]>;

def mscatterv4i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v4i32;
}]>;

def mscatterv4i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v4i64;
}]>;

def mscatterv8i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v8i32;
}]>;

def mscatterv8i64 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v8i64;
}]>;
def mscatterv16i32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
  (X86masked_scatter node:$src1, node:$src2, node:$src3) , [{
  X86MaskedScatterSDNode *Sc = cast<X86MaskedScatterSDNode>(N);
  return Sc->getIndex().getValueType() == MVT::v16i32;
}]>;

// 128-bit bitconvert pattern fragments
def bc_v4f32 : PatFrag<(ops node:$in), (v4f32 (bitconvert node:$in))>;
def bc_v2f64 : PatFrag<(ops node:$in), (v2f64 (bitconvert node:$in))>;
def bc_v16i8 : PatFrag<(ops node:$in), (v16i8 (bitconvert node:$in))>;
def bc_v8i16 : PatFrag<(ops node:$in), (v8i16 (bitconvert node:$in))>;
def bc_v4i32 : PatFrag<(ops node:$in), (v4i32 (bitconvert node:$in))>;
def bc_v2i64 : PatFrag<(ops node:$in), (v2i64 (bitconvert node:$in))>;

// 256-bit bitconvert pattern fragments
def bc_v32i8 : PatFrag<(ops node:$in), (v32i8 (bitconvert node:$in))>;
def bc_v16i16 : PatFrag<(ops node:$in), (v16i16 (bitconvert node:$in))>;
def bc_v8i32 : PatFrag<(ops node:$in), (v8i32 (bitconvert node:$in))>;
def bc_v4i64 : PatFrag<(ops node:$in), (v4i64 (bitconvert node:$in))>;
def bc_v8f32 : PatFrag<(ops node:$in), (v8f32 (bitconvert node:$in))>;
def bc_v4f64 : PatFrag<(ops node:$in), (v4f64 (bitconvert node:$in))>;

// 512-bit bitconvert pattern fragments
def bc_v64i8 : PatFrag<(ops node:$in), (v64i8 (bitconvert node:$in))>;
def bc_v32i16 : PatFrag<(ops node:$in), (v32i16 (bitconvert node:$in))>;
def bc_v16i32 : PatFrag<(ops node:$in), (v16i32 (bitconvert node:$in))>;
def bc_v8i64 : PatFrag<(ops node:$in), (v8i64 (bitconvert node:$in))>;
def bc_v8f64 : PatFrag<(ops node:$in), (v8f64 (bitconvert node:$in))>;
def bc_v16f32 : PatFrag<(ops node:$in), (v16f32 (bitconvert node:$in))>;

def vzmovl_v2i64 : PatFrag<(ops node:$src),
                           (bitconvert (v2i64 (X86vzmovl
                             (v2i64 (scalar_to_vector (loadi64 node:$src))))))>;
def vzmovl_v4i32 : PatFrag<(ops node:$src),
                           (bitconvert (v4i32 (X86vzmovl
                             (v4i32 (scalar_to_vector (loadi32 node:$src))))))>;

def vzload_v2i64 : PatFrag<(ops node:$src),
                           (bitconvert (v2i64 (X86vzload node:$src)))>;


def fp32imm0 : PatLeaf<(f32 fpimm), [{
  return N->isExactlyValue(+0.0);
}]>;

def fp64imm0 : PatLeaf<(f64 fpimm), [{
  return N->isExactlyValue(+0.0);
}]>;

def I8Imm : SDNodeXForm<imm, [{
  // Transformation function: get the low 8 bits.
  return getI8Imm((uint8_t)N->getZExtValue(), SDLoc(N));
}]>;

def FROUND_NO_EXC : PatLeaf<(i32 8)>;
def FROUND_CURRENT : PatLeaf<(i32 4)>;

// BYTE_imm - Transform bit immediates into byte immediates.
def BYTE_imm  : SDNodeXForm<imm, [{
  // Transformation function: imm >> 3
  return getI32Imm(N->getZExtValue() >> 3, SDLoc(N));
}]>;

// EXTRACT_get_vextract128_imm xform function: convert extract_subvector index
// to VEXTRACTF128/VEXTRACTI128 imm.
def EXTRACT_get_vextract128_imm : SDNodeXForm<extract_subvector, [{
  return getExtractVEXTRACTImmediate(N, 128, SDLoc(N));
}]>;

// INSERT_get_vinsert128_imm xform function: convert insert_subvector index to
// VINSERTF128/VINSERTI128 imm.
def INSERT_get_vinsert128_imm : SDNodeXForm<insert_subvector, [{
  return getInsertVINSERTImmediate(N, 128, SDLoc(N));
}]>;

// EXTRACT_get_vextract256_imm xform function: convert extract_subvector index
// to VEXTRACTF64x4 imm.
def EXTRACT_get_vextract256_imm : SDNodeXForm<extract_subvector, [{
  return getExtractVEXTRACTImmediate(N, 256, SDLoc(N));
}]>;

// INSERT_get_vinsert256_imm xform function: convert insert_subvector index to
// VINSERTF64x4 imm.
def INSERT_get_vinsert256_imm : SDNodeXForm<insert_subvector, [{
  return getInsertVINSERTImmediate(N, 256, SDLoc(N));
}]>;

def vextract128_extract : PatFrag<(ops node:$bigvec, node:$index),
                                   (extract_subvector node:$bigvec,
                                                      node:$index), [{}],
                                  EXTRACT_get_vextract128_imm>;

def vinsert128_insert : PatFrag<(ops node:$bigvec, node:$smallvec,
                                      node:$index),
                                 (insert_subvector node:$bigvec, node:$smallvec,
                                                   node:$index), [{}],
                                INSERT_get_vinsert128_imm>;

def vextract256_extract : PatFrag<(ops node:$bigvec, node:$index),
                                   (extract_subvector node:$bigvec,
                                                      node:$index), [{}],
                                  EXTRACT_get_vextract256_imm>;

def vinsert256_insert : PatFrag<(ops node:$bigvec, node:$smallvec,
                                      node:$index),
                                 (insert_subvector node:$bigvec, node:$smallvec,
                                                   node:$index), [{}],
                                INSERT_get_vinsert256_imm>;

def X86mload : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (masked_load node:$src1, node:$src2, node:$src3), [{
  return !cast<MaskedLoadSDNode>(N)->isExpandingLoad() &&
    cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
}]>;

def masked_load_aligned128 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mload node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedLoadSDNode>(N)->getAlignment() >= 16;
}]>;

def masked_load_aligned256 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mload node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedLoadSDNode>(N)->getAlignment() >= 32;
}]>;

def masked_load_aligned512 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mload node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedLoadSDNode>(N)->getAlignment() >= 64;
}]>;

def masked_load_unaligned : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (masked_load node:$src1, node:$src2, node:$src3), [{
  return !cast<MaskedLoadSDNode>(N)->isExpandingLoad() &&
    cast<MaskedLoadSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
}]>;

def X86mExpandingLoad : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (masked_load node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedLoadSDNode>(N)->isExpandingLoad();
}]>;

// Masked store fragments.
// X86mstore can't be implemented in core DAG files because some targets
// do not support vector types (llvm-tblgen will fail).
def X86mstore : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                        (masked_store node:$src1, node:$src2, node:$src3), [{
  return (!cast<MaskedStoreSDNode>(N)->isTruncatingStore()) &&
         (!cast<MaskedStoreSDNode>(N)->isCompressingStore());
}]>;

def masked_store_aligned128 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getAlignment() >= 16;
}]>;

def masked_store_aligned256 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getAlignment() >= 32;
}]>;

def masked_store_aligned512 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (X86mstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getAlignment() >= 64;
}]>;

def masked_store_unaligned : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                         (masked_store node:$src1, node:$src2, node:$src3), [{
  return (!cast<MaskedStoreSDNode>(N)->isTruncatingStore()) &&
         (!cast<MaskedStoreSDNode>(N)->isCompressingStore());
}]>;

def X86mCompressingStore : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                             (masked_store node:$src1, node:$src2, node:$src3), [{
    return cast<MaskedStoreSDNode>(N)->isCompressingStore();
}]>;

// masked truncstore fragments
// X86mtruncstore can't be implemented in core DAG files because some targets
// doesn't support vector type ( llvm-tblgen will fail)
def X86mtruncstore : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                             (masked_store node:$src1, node:$src2, node:$src3), [{
    return cast<MaskedStoreSDNode>(N)->isTruncatingStore();
}]>;
def masked_truncstorevi8 :
  PatFrag<(ops node:$src1, node:$src2, node:$src3),
          (X86mtruncstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def masked_truncstorevi16 :
  PatFrag<(ops node:$src1, node:$src2, node:$src3),
          (X86mtruncstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def masked_truncstorevi32 :
  PatFrag<(ops node:$src1, node:$src2, node:$src3),
          (X86mtruncstore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def X86TruncSStore : SDNode<"X86ISD::VTRUNCSTORES",  SDTStore,
                       [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def X86TruncUSStore : SDNode<"X86ISD::VTRUNCSTOREUS",  SDTStore,
                       [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def X86MTruncSStore : SDNode<"X86ISD::VMTRUNCSTORES",  SDTMaskedStore,
                       [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def X86MTruncUSStore : SDNode<"X86ISD::VMTRUNCSTOREUS",  SDTMaskedStore,
                       [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def truncstore_s_vi8 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncSStore node:$val, node:$ptr), [{
  return cast<TruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;

def truncstore_us_vi8 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncUSStore node:$val, node:$ptr), [{
  return cast<TruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;

def truncstore_s_vi16 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncSStore node:$val, node:$ptr), [{
  return cast<TruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;

def truncstore_us_vi16 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncUSStore node:$val, node:$ptr), [{
  return cast<TruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;

def truncstore_s_vi32 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncSStore node:$val, node:$ptr), [{
  return cast<TruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def truncstore_us_vi32 : PatFrag<(ops node:$val, node:$ptr),
                               (X86TruncUSStore node:$val, node:$ptr), [{
  return cast<TruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def masked_truncstore_s_vi8 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                     (X86MTruncSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;

def masked_truncstore_us_vi8 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                               (X86MTruncUSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i8;
}]>;

def masked_truncstore_s_vi16 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                               (X86MTruncSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;

def masked_truncstore_us_vi16 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                               (X86MTruncUSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i16;
}]>;

def masked_truncstore_s_vi32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                               (X86MTruncSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;

def masked_truncstore_us_vi32 : PatFrag<(ops node:$src1, node:$src2, node:$src3),
                               (X86MTruncUSStore node:$src1, node:$src2, node:$src3), [{
  return cast<MaskedTruncUSStoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::i32;
}]>;
