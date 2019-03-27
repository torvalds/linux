//=- AArch64InstrInfo.td - Describe the AArch64 Instructions -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AArch64 Instruction definitions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ARM Instruction Predicate Definitions.
//
def HasV8_1a         : Predicate<"Subtarget->hasV8_1aOps()">,
                                 AssemblerPredicate<"HasV8_1aOps", "armv8.1a">;
def HasV8_2a         : Predicate<"Subtarget->hasV8_2aOps()">,
                                 AssemblerPredicate<"HasV8_2aOps", "armv8.2a">;
def HasV8_3a         : Predicate<"Subtarget->hasV8_3aOps()">,
                                 AssemblerPredicate<"HasV8_3aOps", "armv8.3a">;
def HasV8_4a         : Predicate<"Subtarget->hasV8_4aOps()">,
                                 AssemblerPredicate<"HasV8_4aOps", "armv8.4a">;
def HasV8_5a         : Predicate<"Subtarget->hasV8_5aOps()">,
                                 AssemblerPredicate<"HasV8_5aOps", "armv8.5a">;
def HasVH            : Predicate<"Subtarget->hasVH()">,
                       AssemblerPredicate<"FeatureVH", "vh">;

def HasLOR           : Predicate<"Subtarget->hasLOR()">,
                       AssemblerPredicate<"FeatureLOR", "lor">;

def HasPA            : Predicate<"Subtarget->hasPA()">,
                       AssemblerPredicate<"FeaturePA", "pa">;

def HasJS            : Predicate<"Subtarget->hasJS()">,
                       AssemblerPredicate<"FeatureJS", "jsconv">;

def HasCCIDX         : Predicate<"Subtarget->hasCCIDX()">,
                       AssemblerPredicate<"FeatureCCIDX", "ccidx">;

def HasComplxNum      : Predicate<"Subtarget->hasComplxNum()">,
                       AssemblerPredicate<"FeatureComplxNum", "complxnum">;

def HasNV            : Predicate<"Subtarget->hasNV()">,
                       AssemblerPredicate<"FeatureNV", "nv">;

def HasRASv8_4       : Predicate<"Subtarget->hasRASv8_4()">,
                       AssemblerPredicate<"FeatureRASv8_4", "rasv8_4">;

def HasMPAM          : Predicate<"Subtarget->hasMPAM()">,
                       AssemblerPredicate<"FeatureMPAM", "mpam">;

def HasDIT           : Predicate<"Subtarget->hasDIT()">,
                       AssemblerPredicate<"FeatureDIT", "dit">;

def HasTRACEV8_4         : Predicate<"Subtarget->hasTRACEV8_4()">,
                       AssemblerPredicate<"FeatureTRACEV8_4", "tracev8.4">;

def HasAM            : Predicate<"Subtarget->hasAM()">,
                       AssemblerPredicate<"FeatureAM", "am">;

def HasSEL2          : Predicate<"Subtarget->hasSEL2()">,
                       AssemblerPredicate<"FeatureSEL2", "sel2">;

def HasTLB_RMI          : Predicate<"Subtarget->hasTLB_RMI()">,
                       AssemblerPredicate<"FeatureTLB_RMI", "tlb-rmi">;

def HasFMI           : Predicate<"Subtarget->hasFMI()">,
                       AssemblerPredicate<"FeatureFMI", "fmi">;

def HasRCPC_IMMO      : Predicate<"Subtarget->hasRCPCImm()">,
                       AssemblerPredicate<"FeatureRCPC_IMMO", "rcpc-immo">;

def HasFPARMv8       : Predicate<"Subtarget->hasFPARMv8()">,
                               AssemblerPredicate<"FeatureFPARMv8", "fp-armv8">;
def HasNEON          : Predicate<"Subtarget->hasNEON()">,
                                 AssemblerPredicate<"FeatureNEON", "neon">;
def HasCrypto        : Predicate<"Subtarget->hasCrypto()">,
                                 AssemblerPredicate<"FeatureCrypto", "crypto">;
def HasSM4           : Predicate<"Subtarget->hasSM4()">,
                                 AssemblerPredicate<"FeatureSM4", "sm4">;
def HasSHA3          : Predicate<"Subtarget->hasSHA3()">,
                                 AssemblerPredicate<"FeatureSHA3", "sha3">;
def HasSHA2          : Predicate<"Subtarget->hasSHA2()">,
                                 AssemblerPredicate<"FeatureSHA2", "sha2">;
def HasAES           : Predicate<"Subtarget->hasAES()">,
                                 AssemblerPredicate<"FeatureAES", "aes">;
def HasDotProd       : Predicate<"Subtarget->hasDotProd()">,
                                 AssemblerPredicate<"FeatureDotProd", "dotprod">;
def HasCRC           : Predicate<"Subtarget->hasCRC()">,
                                 AssemblerPredicate<"FeatureCRC", "crc">;
def HasLSE           : Predicate<"Subtarget->hasLSE()">,
                                 AssemblerPredicate<"FeatureLSE", "lse">;
def HasRAS           : Predicate<"Subtarget->hasRAS()">,
                                 AssemblerPredicate<"FeatureRAS", "ras">;
def HasRDM           : Predicate<"Subtarget->hasRDM()">,
                                 AssemblerPredicate<"FeatureRDM", "rdm">;
def HasPerfMon       : Predicate<"Subtarget->hasPerfMon()">;
def HasFullFP16      : Predicate<"Subtarget->hasFullFP16()">,
                                 AssemblerPredicate<"FeatureFullFP16", "fullfp16">;
def HasFP16FML       : Predicate<"Subtarget->hasFP16FML()">,
                                 AssemblerPredicate<"FeatureFP16FML", "fp16fml">;
def HasSPE           : Predicate<"Subtarget->hasSPE()">,
                                 AssemblerPredicate<"FeatureSPE", "spe">;
def HasFuseAES       : Predicate<"Subtarget->hasFuseAES()">,
                                 AssemblerPredicate<"FeatureFuseAES",
                                 "fuse-aes">;
def HasSVE           : Predicate<"Subtarget->hasSVE()">,
                                 AssemblerPredicate<"FeatureSVE", "sve">;
def HasRCPC          : Predicate<"Subtarget->hasRCPC()">,
                                 AssemblerPredicate<"FeatureRCPC", "rcpc">;
def HasAltNZCV       : Predicate<"Subtarget->hasAlternativeNZCV()">,
                       AssemblerPredicate<"FeatureAltFPCmp", "altnzcv">;
def HasFRInt3264     : Predicate<"Subtarget->hasFRInt3264()">,
                       AssemblerPredicate<"FeatureFRInt3264", "frint3264">;
def HasSB            : Predicate<"Subtarget->hasSB()">,
                       AssemblerPredicate<"FeatureSB", "sb">;
def HasPredRes      : Predicate<"Subtarget->hasPredRes()">,
                       AssemblerPredicate<"FeaturePredRes", "predres">;
def HasCCDP          : Predicate<"Subtarget->hasCCDP()">,
                       AssemblerPredicate<"FeatureCacheDeepPersist", "ccdp">;
def HasBTI           : Predicate<"Subtarget->hasBTI()">,
                       AssemblerPredicate<"FeatureBranchTargetId", "bti">;
def HasMTE           : Predicate<"Subtarget->hasMTE()">,
                       AssemblerPredicate<"FeatureMTE", "mte">;
def IsLE             : Predicate<"Subtarget->isLittleEndian()">;
def IsBE             : Predicate<"!Subtarget->isLittleEndian()">;
def UseAlternateSExtLoadCVTF32
    : Predicate<"Subtarget->useAlternateSExtLoadCVTF32Pattern()">;

def UseNegativeImmediates
    : Predicate<"false">, AssemblerPredicate<"!FeatureNoNegativeImmediates",
                                             "NegativeImmediates">;

def AArch64LocalRecover : SDNode<"ISD::LOCAL_RECOVER",
                                  SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                       SDTCisInt<1>]>>;


//===----------------------------------------------------------------------===//
// AArch64-specific DAG Nodes.
//

// SDTBinaryArithWithFlagsOut - RES1, FLAGS = op LHS, RHS
def SDTBinaryArithWithFlagsOut : SDTypeProfile<2, 2,
                                              [SDTCisSameAs<0, 2>,
                                               SDTCisSameAs<0, 3>,
                                               SDTCisInt<0>, SDTCisVT<1, i32>]>;

// SDTBinaryArithWithFlagsIn - RES1, FLAGS = op LHS, RHS, FLAGS
def SDTBinaryArithWithFlagsIn : SDTypeProfile<1, 3,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisInt<0>,
                                             SDTCisVT<3, i32>]>;

// SDTBinaryArithWithFlagsInOut - RES1, FLAGS = op LHS, RHS, FLAGS
def SDTBinaryArithWithFlagsInOut : SDTypeProfile<2, 3,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<4, i32>]>;

def SDT_AArch64Brcond  : SDTypeProfile<0, 3,
                                     [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>,
                                      SDTCisVT<2, i32>]>;
def SDT_AArch64cbz : SDTypeProfile<0, 2, [SDTCisInt<0>, SDTCisVT<1, OtherVT>]>;
def SDT_AArch64tbz : SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisInt<1>,
                                        SDTCisVT<2, OtherVT>]>;


def SDT_AArch64CSel  : SDTypeProfile<1, 4,
                                   [SDTCisSameAs<0, 1>,
                                    SDTCisSameAs<0, 2>,
                                    SDTCisInt<3>,
                                    SDTCisVT<4, i32>]>;
def SDT_AArch64CCMP : SDTypeProfile<1, 5,
                                    [SDTCisVT<0, i32>,
                                     SDTCisInt<1>,
                                     SDTCisSameAs<1, 2>,
                                     SDTCisInt<3>,
                                     SDTCisInt<4>,
                                     SDTCisVT<5, i32>]>;
def SDT_AArch64FCCMP : SDTypeProfile<1, 5,
                                     [SDTCisVT<0, i32>,
                                      SDTCisFP<1>,
                                      SDTCisSameAs<1, 2>,
                                      SDTCisInt<3>,
                                      SDTCisInt<4>,
                                      SDTCisVT<5, i32>]>;
def SDT_AArch64FCmp   : SDTypeProfile<0, 2,
                                   [SDTCisFP<0>,
                                    SDTCisSameAs<0, 1>]>;
def SDT_AArch64Dup   : SDTypeProfile<1, 1, [SDTCisVec<0>]>;
def SDT_AArch64DupLane   : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisInt<2>]>;
def SDT_AArch64Zip   : SDTypeProfile<1, 2, [SDTCisVec<0>,
                                          SDTCisSameAs<0, 1>,
                                          SDTCisSameAs<0, 2>]>;
def SDT_AArch64MOVIedit : SDTypeProfile<1, 1, [SDTCisInt<1>]>;
def SDT_AArch64MOVIshift : SDTypeProfile<1, 2, [SDTCisInt<1>, SDTCisInt<2>]>;
def SDT_AArch64vecimm : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisInt<2>, SDTCisInt<3>]>;
def SDT_AArch64UnaryVec: SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def SDT_AArch64ExtVec: SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                          SDTCisSameAs<0,2>, SDTCisInt<3>]>;
def SDT_AArch64vshift : SDTypeProfile<1, 2, [SDTCisSameAs<0,1>, SDTCisInt<2>]>;

def SDT_AArch64unvec : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0,1>]>;
def SDT_AArch64fcmpz : SDTypeProfile<1, 1, []>;
def SDT_AArch64fcmp  : SDTypeProfile<1, 2, [SDTCisSameAs<1,2>]>;
def SDT_AArch64binvec : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisSameAs<0,2>]>;
def SDT_AArch64trivec : SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0,1>,
                                           SDTCisSameAs<0,2>,
                                           SDTCisSameAs<0,3>]>;
def SDT_AArch64TCRET : SDTypeProfile<0, 2, [SDTCisPtrTy<0>]>;
def SDT_AArch64PREFETCH : SDTypeProfile<0, 2, [SDTCisVT<0, i32>, SDTCisPtrTy<1>]>;

def SDT_AArch64ITOF  : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisSameAs<0,1>]>;

def SDT_AArch64TLSDescCall : SDTypeProfile<0, -2, [SDTCisPtrTy<0>,
                                                 SDTCisPtrTy<1>]>;

// Generates the general dynamic sequences, i.e.
//  adrp  x0, :tlsdesc:var
//  ldr   x1, [x0, #:tlsdesc_lo12:var]
//  add   x0, x0, #:tlsdesc_lo12:var
//  .tlsdesccall var
//  blr   x1

// (the TPIDR_EL0 offset is put directly in X0, hence no "result" here)
// number of operands (the variable)
def SDT_AArch64TLSDescCallSeq : SDTypeProfile<0,1,
                                          [SDTCisPtrTy<0>]>;

def SDT_AArch64WrapperLarge : SDTypeProfile<1, 4,
                                        [SDTCisVT<0, i64>, SDTCisVT<1, i32>,
                                         SDTCisSameAs<1, 2>, SDTCisSameAs<1, 3>,
                                         SDTCisSameAs<1, 4>]>;


// Node definitions.
def AArch64adrp          : SDNode<"AArch64ISD::ADRP", SDTIntUnaryOp, []>;
def AArch64adr           : SDNode<"AArch64ISD::ADR", SDTIntUnaryOp, []>;
def AArch64addlow        : SDNode<"AArch64ISD::ADDlow", SDTIntBinOp, []>;
def AArch64LOADgot       : SDNode<"AArch64ISD::LOADgot", SDTIntUnaryOp>;
def AArch64callseq_start : SDNode<"ISD::CALLSEQ_START",
                                SDCallSeqStart<[ SDTCisVT<0, i32>,
                                                 SDTCisVT<1, i32> ]>,
                                [SDNPHasChain, SDNPOutGlue]>;
def AArch64callseq_end   : SDNode<"ISD::CALLSEQ_END",
                                SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                               SDTCisVT<1, i32> ]>,
                                [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def AArch64call          : SDNode<"AArch64ISD::CALL",
                                SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                 SDNPVariadic]>;
def AArch64brcond        : SDNode<"AArch64ISD::BRCOND", SDT_AArch64Brcond,
                                [SDNPHasChain]>;
def AArch64cbz           : SDNode<"AArch64ISD::CBZ", SDT_AArch64cbz,
                                [SDNPHasChain]>;
def AArch64cbnz           : SDNode<"AArch64ISD::CBNZ", SDT_AArch64cbz,
                                [SDNPHasChain]>;
def AArch64tbz           : SDNode<"AArch64ISD::TBZ", SDT_AArch64tbz,
                                [SDNPHasChain]>;
def AArch64tbnz           : SDNode<"AArch64ISD::TBNZ", SDT_AArch64tbz,
                                [SDNPHasChain]>;


def AArch64csel          : SDNode<"AArch64ISD::CSEL", SDT_AArch64CSel>;
def AArch64csinv         : SDNode<"AArch64ISD::CSINV", SDT_AArch64CSel>;
def AArch64csneg         : SDNode<"AArch64ISD::CSNEG", SDT_AArch64CSel>;
def AArch64csinc         : SDNode<"AArch64ISD::CSINC", SDT_AArch64CSel>;
def AArch64retflag       : SDNode<"AArch64ISD::RET_FLAG", SDTNone,
                                [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def AArch64adc       : SDNode<"AArch64ISD::ADC",  SDTBinaryArithWithFlagsIn >;
def AArch64sbc       : SDNode<"AArch64ISD::SBC",  SDTBinaryArithWithFlagsIn>;
def AArch64add_flag  : SDNode<"AArch64ISD::ADDS",  SDTBinaryArithWithFlagsOut,
                            [SDNPCommutative]>;
def AArch64sub_flag  : SDNode<"AArch64ISD::SUBS",  SDTBinaryArithWithFlagsOut>;
def AArch64and_flag  : SDNode<"AArch64ISD::ANDS",  SDTBinaryArithWithFlagsOut,
                            [SDNPCommutative]>;
def AArch64adc_flag  : SDNode<"AArch64ISD::ADCS",  SDTBinaryArithWithFlagsInOut>;
def AArch64sbc_flag  : SDNode<"AArch64ISD::SBCS",  SDTBinaryArithWithFlagsInOut>;

def AArch64ccmp      : SDNode<"AArch64ISD::CCMP",  SDT_AArch64CCMP>;
def AArch64ccmn      : SDNode<"AArch64ISD::CCMN",  SDT_AArch64CCMP>;
def AArch64fccmp     : SDNode<"AArch64ISD::FCCMP", SDT_AArch64FCCMP>;

def AArch64threadpointer : SDNode<"AArch64ISD::THREAD_POINTER", SDTPtrLeaf>;

def AArch64fcmp      : SDNode<"AArch64ISD::FCMP", SDT_AArch64FCmp>;

def AArch64dup       : SDNode<"AArch64ISD::DUP", SDT_AArch64Dup>;
def AArch64duplane8  : SDNode<"AArch64ISD::DUPLANE8", SDT_AArch64DupLane>;
def AArch64duplane16 : SDNode<"AArch64ISD::DUPLANE16", SDT_AArch64DupLane>;
def AArch64duplane32 : SDNode<"AArch64ISD::DUPLANE32", SDT_AArch64DupLane>;
def AArch64duplane64 : SDNode<"AArch64ISD::DUPLANE64", SDT_AArch64DupLane>;

def AArch64zip1      : SDNode<"AArch64ISD::ZIP1", SDT_AArch64Zip>;
def AArch64zip2      : SDNode<"AArch64ISD::ZIP2", SDT_AArch64Zip>;
def AArch64uzp1      : SDNode<"AArch64ISD::UZP1", SDT_AArch64Zip>;
def AArch64uzp2      : SDNode<"AArch64ISD::UZP2", SDT_AArch64Zip>;
def AArch64trn1      : SDNode<"AArch64ISD::TRN1", SDT_AArch64Zip>;
def AArch64trn2      : SDNode<"AArch64ISD::TRN2", SDT_AArch64Zip>;

def AArch64movi_edit : SDNode<"AArch64ISD::MOVIedit", SDT_AArch64MOVIedit>;
def AArch64movi_shift : SDNode<"AArch64ISD::MOVIshift", SDT_AArch64MOVIshift>;
def AArch64movi_msl : SDNode<"AArch64ISD::MOVImsl", SDT_AArch64MOVIshift>;
def AArch64mvni_shift : SDNode<"AArch64ISD::MVNIshift", SDT_AArch64MOVIshift>;
def AArch64mvni_msl : SDNode<"AArch64ISD::MVNImsl", SDT_AArch64MOVIshift>;
def AArch64movi : SDNode<"AArch64ISD::MOVI", SDT_AArch64MOVIedit>;
def AArch64fmov : SDNode<"AArch64ISD::FMOV", SDT_AArch64MOVIedit>;

def AArch64rev16 : SDNode<"AArch64ISD::REV16", SDT_AArch64UnaryVec>;
def AArch64rev32 : SDNode<"AArch64ISD::REV32", SDT_AArch64UnaryVec>;
def AArch64rev64 : SDNode<"AArch64ISD::REV64", SDT_AArch64UnaryVec>;
def AArch64ext : SDNode<"AArch64ISD::EXT", SDT_AArch64ExtVec>;

def AArch64vashr : SDNode<"AArch64ISD::VASHR", SDT_AArch64vshift>;
def AArch64vlshr : SDNode<"AArch64ISD::VLSHR", SDT_AArch64vshift>;
def AArch64vshl : SDNode<"AArch64ISD::VSHL", SDT_AArch64vshift>;
def AArch64sqshli : SDNode<"AArch64ISD::SQSHL_I", SDT_AArch64vshift>;
def AArch64uqshli : SDNode<"AArch64ISD::UQSHL_I", SDT_AArch64vshift>;
def AArch64sqshlui : SDNode<"AArch64ISD::SQSHLU_I", SDT_AArch64vshift>;
def AArch64srshri : SDNode<"AArch64ISD::SRSHR_I", SDT_AArch64vshift>;
def AArch64urshri : SDNode<"AArch64ISD::URSHR_I", SDT_AArch64vshift>;

def AArch64not: SDNode<"AArch64ISD::NOT", SDT_AArch64unvec>;
def AArch64bit: SDNode<"AArch64ISD::BIT", SDT_AArch64trivec>;
def AArch64bsl: SDNode<"AArch64ISD::BSL", SDT_AArch64trivec>;

def AArch64cmeq: SDNode<"AArch64ISD::CMEQ", SDT_AArch64binvec>;
def AArch64cmge: SDNode<"AArch64ISD::CMGE", SDT_AArch64binvec>;
def AArch64cmgt: SDNode<"AArch64ISD::CMGT", SDT_AArch64binvec>;
def AArch64cmhi: SDNode<"AArch64ISD::CMHI", SDT_AArch64binvec>;
def AArch64cmhs: SDNode<"AArch64ISD::CMHS", SDT_AArch64binvec>;

def AArch64fcmeq: SDNode<"AArch64ISD::FCMEQ", SDT_AArch64fcmp>;
def AArch64fcmge: SDNode<"AArch64ISD::FCMGE", SDT_AArch64fcmp>;
def AArch64fcmgt: SDNode<"AArch64ISD::FCMGT", SDT_AArch64fcmp>;

def AArch64cmeqz: SDNode<"AArch64ISD::CMEQz", SDT_AArch64unvec>;
def AArch64cmgez: SDNode<"AArch64ISD::CMGEz", SDT_AArch64unvec>;
def AArch64cmgtz: SDNode<"AArch64ISD::CMGTz", SDT_AArch64unvec>;
def AArch64cmlez: SDNode<"AArch64ISD::CMLEz", SDT_AArch64unvec>;
def AArch64cmltz: SDNode<"AArch64ISD::CMLTz", SDT_AArch64unvec>;
def AArch64cmtst : PatFrag<(ops node:$LHS, node:$RHS),
                        (AArch64not (AArch64cmeqz (and node:$LHS, node:$RHS)))>;

def AArch64fcmeqz: SDNode<"AArch64ISD::FCMEQz", SDT_AArch64fcmpz>;
def AArch64fcmgez: SDNode<"AArch64ISD::FCMGEz", SDT_AArch64fcmpz>;
def AArch64fcmgtz: SDNode<"AArch64ISD::FCMGTz", SDT_AArch64fcmpz>;
def AArch64fcmlez: SDNode<"AArch64ISD::FCMLEz", SDT_AArch64fcmpz>;
def AArch64fcmltz: SDNode<"AArch64ISD::FCMLTz", SDT_AArch64fcmpz>;

def AArch64bici: SDNode<"AArch64ISD::BICi", SDT_AArch64vecimm>;
def AArch64orri: SDNode<"AArch64ISD::ORRi", SDT_AArch64vecimm>;

def AArch64neg : SDNode<"AArch64ISD::NEG", SDT_AArch64unvec>;

def AArch64tcret: SDNode<"AArch64ISD::TC_RETURN", SDT_AArch64TCRET,
                  [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def AArch64Prefetch        : SDNode<"AArch64ISD::PREFETCH", SDT_AArch64PREFETCH,
                               [SDNPHasChain, SDNPSideEffect]>;

def AArch64sitof: SDNode<"AArch64ISD::SITOF", SDT_AArch64ITOF>;
def AArch64uitof: SDNode<"AArch64ISD::UITOF", SDT_AArch64ITOF>;

def AArch64tlsdesc_callseq : SDNode<"AArch64ISD::TLSDESC_CALLSEQ",
                                    SDT_AArch64TLSDescCallSeq,
                                    [SDNPInGlue, SDNPOutGlue, SDNPHasChain,
                                     SDNPVariadic]>;


def AArch64WrapperLarge : SDNode<"AArch64ISD::WrapperLarge",
                                 SDT_AArch64WrapperLarge>;

def AArch64NvCast : SDNode<"AArch64ISD::NVCAST", SDTUnaryOp>;

def SDT_AArch64mull : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisInt<1>,
                                    SDTCisSameAs<1, 2>]>;
def AArch64smull    : SDNode<"AArch64ISD::SMULL", SDT_AArch64mull>;
def AArch64umull    : SDNode<"AArch64ISD::UMULL", SDT_AArch64mull>;

def AArch64frecpe   : SDNode<"AArch64ISD::FRECPE", SDTFPUnaryOp>;
def AArch64frecps   : SDNode<"AArch64ISD::FRECPS", SDTFPBinOp>;
def AArch64frsqrte  : SDNode<"AArch64ISD::FRSQRTE", SDTFPUnaryOp>;
def AArch64frsqrts  : SDNode<"AArch64ISD::FRSQRTS", SDTFPBinOp>;

def AArch64saddv    : SDNode<"AArch64ISD::SADDV", SDT_AArch64UnaryVec>;
def AArch64uaddv    : SDNode<"AArch64ISD::UADDV", SDT_AArch64UnaryVec>;
def AArch64sminv    : SDNode<"AArch64ISD::SMINV", SDT_AArch64UnaryVec>;
def AArch64uminv    : SDNode<"AArch64ISD::UMINV", SDT_AArch64UnaryVec>;
def AArch64smaxv    : SDNode<"AArch64ISD::SMAXV", SDT_AArch64UnaryVec>;
def AArch64umaxv    : SDNode<"AArch64ISD::UMAXV", SDT_AArch64UnaryVec>;

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

// AArch64 Instruction Predicate Definitions.
// We could compute these on a per-module basis but doing so requires accessing
// the Function object through the <Target>Subtarget and objections were raised
// to that (see post-commit review comments for r301750).
let RecomputePerFunction = 1 in {
  def ForCodeSize   : Predicate<"MF->getFunction().optForSize()">;
  def NotForCodeSize   : Predicate<"!MF->getFunction().optForSize()">;
  // Avoid generating STRQro if it is slow, unless we're optimizing for code size.
  def UseSTRQro : Predicate<"!Subtarget->isSTRQroSlow() || MF->getFunction().optForSize()">;

  def UseBTI : Predicate<[{ MF->getFunction().hasFnAttribute("branch-target-enforcement") }]>;
  def NotUseBTI : Predicate<[{ !MF->getFunction().hasFnAttribute("branch-target-enforcement") }]>;
}

include "AArch64InstrFormats.td"
include "SVEInstrFormats.td"

//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Miscellaneous instructions.
//===----------------------------------------------------------------------===//

let Defs = [SP], Uses = [SP], hasSideEffects = 1, isCodeGenOnly = 1 in {
// We set Sched to empty list because we expect these instructions to simply get
// removed in most cases.
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(AArch64callseq_start timm:$amt1, timm:$amt2)]>,
                              Sched<[]>;
def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                            [(AArch64callseq_end timm:$amt1, timm:$amt2)]>,
                            Sched<[]>;
} // Defs = [SP], Uses = [SP], hasSideEffects = 1, isCodeGenOnly = 1

let isReMaterializable = 1, isCodeGenOnly = 1 in {
// FIXME: The following pseudo instructions are only needed because remat
// cannot handle multiple instructions.  When that changes, they can be
// removed, along with the AArch64Wrapper node.

let AddedComplexity = 10 in
def LOADgot : Pseudo<(outs GPR64:$dst), (ins i64imm:$addr),
                     [(set GPR64:$dst, (AArch64LOADgot tglobaladdr:$addr))]>,
              Sched<[WriteLDAdr]>;

// The MOVaddr instruction should match only when the add is not folded
// into a load or store address.
def MOVaddr
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp tglobaladdr:$hi),
                                            tglobaladdr:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrJT
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp tjumptable:$hi),
                                             tjumptable:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrCP
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp tconstpool:$hi),
                                             tconstpool:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrBA
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp tblockaddress:$hi),
                                             tblockaddress:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrTLS
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp tglobaltlsaddr:$hi),
                                            tglobaltlsaddr:$low))]>,
      Sched<[WriteAdrAdr]>;
def MOVaddrEXT
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$hi, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow (AArch64adrp texternalsym:$hi),
                                            texternalsym:$low))]>,
      Sched<[WriteAdrAdr]>;
// Normally AArch64addlow either gets folded into a following ldr/str,
// or together with an adrp into MOVaddr above. For cases with TLS, it
// might appear without either of them, so allow lowering it into a plain
// add.
def ADDlowTLS
    : Pseudo<(outs GPR64:$dst), (ins GPR64:$src, i64imm:$low),
             [(set GPR64:$dst, (AArch64addlow GPR64:$src,
                                            tglobaltlsaddr:$low))]>,
      Sched<[WriteAdr]>;

} // isReMaterializable, isCodeGenOnly

def : Pat<(AArch64LOADgot tglobaltlsaddr:$addr),
          (LOADgot tglobaltlsaddr:$addr)>;

def : Pat<(AArch64LOADgot texternalsym:$addr),
          (LOADgot texternalsym:$addr)>;

def : Pat<(AArch64LOADgot tconstpool:$addr),
          (LOADgot tconstpool:$addr)>;

// 32-bit jump table destination is actually only 2 instructions since we can
// use the table itself as a PC-relative base. But optimization occurs after
// branch relaxation so be pessimistic.
let Size = 12, Constraints = "@earlyclobber $dst,@earlyclobber $scratch" in {
def JumpTableDest32 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                             (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                      Sched<[]>;
def JumpTableDest16 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                             (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                      Sched<[]>;
def JumpTableDest8 : Pseudo<(outs GPR64:$dst, GPR64sp:$scratch),
                            (ins GPR64:$table, GPR64:$entry, i32imm:$jti), []>,
                     Sched<[]>;
}

// Space-consuming pseudo to aid testing of placement and reachability
// algorithms. Immediate operand is the number of bytes this "instruction"
// occupies; register operands can be used to enforce dependency and constrain
// the scheduler.
let hasSideEffects = 1, mayLoad = 1, mayStore = 1 in
def SPACE : Pseudo<(outs GPR64:$Rd), (ins i32imm:$size, GPR64:$Rn),
                   [(set GPR64:$Rd, (int_aarch64_space imm:$size, GPR64:$Rn))]>,
            Sched<[]>;

let hasSideEffects = 1, isCodeGenOnly = 1 in {
  def SpeculationSafeValueX
      : Pseudo<(outs GPR64:$dst), (ins GPR64:$src), []>, Sched<[]>;
  def SpeculationSafeValueW
      : Pseudo<(outs GPR32:$dst), (ins GPR32:$src), []>, Sched<[]>;
}


//===----------------------------------------------------------------------===//
// System instructions.
//===----------------------------------------------------------------------===//

def HINT : HintI<"hint">;
def : InstAlias<"nop",  (HINT 0b000)>;
def : InstAlias<"yield",(HINT 0b001)>;
def : InstAlias<"wfe",  (HINT 0b010)>;
def : InstAlias<"wfi",  (HINT 0b011)>;
def : InstAlias<"sev",  (HINT 0b100)>;
def : InstAlias<"sevl", (HINT 0b101)>;
def : InstAlias<"esb",  (HINT 0b10000)>, Requires<[HasRAS]>;
def : InstAlias<"csdb", (HINT 20)>;
def : InstAlias<"bti",  (HINT 32)>, Requires<[HasBTI]>;
def : InstAlias<"bti $op", (HINT btihint_op:$op)>, Requires<[HasBTI]>;

// v8.2a Statistical Profiling extension
def : InstAlias<"psb $op",  (HINT psbhint_op:$op)>, Requires<[HasSPE]>;

// As far as LLVM is concerned this writes to the system's exclusive monitors.
let mayLoad = 1, mayStore = 1 in
def CLREX : CRmSystemI<imm0_15, 0b010, "clrex">;

// NOTE: ideally, this would have mayStore = 0, mayLoad = 0, but we cannot
// model patterns with sufficiently fine granularity.
let mayLoad = ?, mayStore = ? in {
def DMB   : CRmSystemI<barrier_op, 0b101, "dmb",
                       [(int_aarch64_dmb (i32 imm32_0_15:$CRm))]>;

def DSB   : CRmSystemI<barrier_op, 0b100, "dsb",
                       [(int_aarch64_dsb (i32 imm32_0_15:$CRm))]>;

def ISB   : CRmSystemI<barrier_op, 0b110, "isb",
                       [(int_aarch64_isb (i32 imm32_0_15:$CRm))]>;

def TSB   : CRmSystemI<barrier_op, 0b010, "tsb", []> {
  let CRm        = 0b0010;
  let Inst{12}   = 0;
  let Predicates = [HasTRACEV8_4];
}
}

// ARMv8.2-A Dot Product
let Predicates = [HasDotProd] in {
defm SDOT : SIMDThreeSameVectorDot<0, "sdot", int_aarch64_neon_sdot>;
defm UDOT : SIMDThreeSameVectorDot<1, "udot", int_aarch64_neon_udot>;
defm SDOTlane : SIMDThreeSameVectorDotIndex<0, "sdot", int_aarch64_neon_sdot>;
defm UDOTlane : SIMDThreeSameVectorDotIndex<1, "udot", int_aarch64_neon_udot>;
}

// ARMv8.2-A FP16 Fused Multiply-Add Long
let Predicates = [HasNEON, HasFP16FML] in {
defm FMLAL      : SIMDThreeSameVectorFML<0, 1, 0b001, "fmlal", int_aarch64_neon_fmlal>;
defm FMLSL      : SIMDThreeSameVectorFML<0, 1, 0b101, "fmlsl", int_aarch64_neon_fmlsl>;
defm FMLAL2     : SIMDThreeSameVectorFML<1, 0, 0b001, "fmlal2", int_aarch64_neon_fmlal2>;
defm FMLSL2     : SIMDThreeSameVectorFML<1, 0, 0b101, "fmlsl2", int_aarch64_neon_fmlsl2>;
defm FMLALlane  : SIMDThreeSameVectorFMLIndex<0, 0b0000, "fmlal", int_aarch64_neon_fmlal>;
defm FMLSLlane  : SIMDThreeSameVectorFMLIndex<0, 0b0100, "fmlsl", int_aarch64_neon_fmlsl>;
defm FMLAL2lane : SIMDThreeSameVectorFMLIndex<1, 0b1000, "fmlal2", int_aarch64_neon_fmlal2>;
defm FMLSL2lane : SIMDThreeSameVectorFMLIndex<1, 0b1100, "fmlsl2", int_aarch64_neon_fmlsl2>;
}

// Armv8.2-A Crypto extensions
let Predicates = [HasSHA3] in {
def SHA512H   : CryptoRRRTied<0b0, 0b00, "sha512h">;
def SHA512H2  : CryptoRRRTied<0b0, 0b01, "sha512h2">;
def SHA512SU0 : CryptoRRTied_2D<0b0, 0b00, "sha512su0">;
def SHA512SU1 : CryptoRRRTied_2D<0b0, 0b10, "sha512su1">;
def RAX1      : CryptoRRR_2D<0b0,0b11, "rax1">;
def EOR3      : CryptoRRRR_16B<0b00, "eor3">;
def BCAX      : CryptoRRRR_16B<0b01, "bcax">;
def XAR       : CryptoRRRi6<"xar">;
} // HasSHA3

let Predicates = [HasSM4] in {
def SM3TT1A   : CryptoRRRi2Tied<0b0, 0b00, "sm3tt1a">;
def SM3TT1B   : CryptoRRRi2Tied<0b0, 0b01, "sm3tt1b">;
def SM3TT2A   : CryptoRRRi2Tied<0b0, 0b10, "sm3tt2a">;
def SM3TT2B   : CryptoRRRi2Tied<0b0, 0b11, "sm3tt2b">;
def SM3SS1    : CryptoRRRR_4S<0b10, "sm3ss1">;
def SM3PARTW1 : CryptoRRRTied_4S<0b1, 0b00, "sm3partw1">;
def SM3PARTW2 : CryptoRRRTied_4S<0b1, 0b01, "sm3partw2">;
def SM4ENCKEY : CryptoRRR_4S<0b1, 0b10, "sm4ekey">;
def SM4E      : CryptoRRTied_4S<0b0, 0b01, "sm4e">;
} // HasSM4

let Predicates = [HasRCPC] in {
  // v8.3 Release Consistent Processor Consistent support, optional in v8.2.
  def LDAPRB  : RCPCLoad<0b00, "ldaprb", GPR32>;
  def LDAPRH  : RCPCLoad<0b01, "ldaprh", GPR32>;
  def LDAPRW  : RCPCLoad<0b10, "ldapr", GPR32>;
  def LDAPRX  : RCPCLoad<0b11, "ldapr", GPR64>;
}

// v8.3a complex add and multiply-accumulate. No predicate here, that is done
// inside the multiclass as the FP16 versions need different predicates.
defm FCMLA : SIMDThreeSameVectorTiedComplexHSD<1, 0b110, complexrotateop,
                                               "fcmla", null_frag>;
defm FCADD : SIMDThreeSameVectorComplexHSD<1, 0b111, complexrotateopodd,
                                           "fcadd", null_frag>;
defm FCMLA : SIMDIndexedTiedComplexHSD<1, 0, 1, complexrotateop, "fcmla",
                                       null_frag>;

// v8.3a Pointer Authentication
// These instructions inhabit part of the hint space and so can be used for
// armv8 targets
let Uses = [LR], Defs = [LR] in {
  def PACIAZ   : SystemNoOperands<0b000, "paciaz">;
  def PACIBZ   : SystemNoOperands<0b010, "pacibz">;
  def AUTIAZ   : SystemNoOperands<0b100, "autiaz">;
  def AUTIBZ   : SystemNoOperands<0b110, "autibz">;
}
let Uses = [LR, SP], Defs = [LR] in {
  def PACIASP  : SystemNoOperands<0b001, "paciasp">;
  def PACIBSP  : SystemNoOperands<0b011, "pacibsp">;
  def AUTIASP  : SystemNoOperands<0b101, "autiasp">;
  def AUTIBSP  : SystemNoOperands<0b111, "autibsp">;
}
let Uses = [X16, X17], Defs = [X17], CRm = 0b0001 in {
  def PACIA1716  : SystemNoOperands<0b000, "pacia1716">;
  def PACIB1716  : SystemNoOperands<0b010, "pacib1716">;
  def AUTIA1716  : SystemNoOperands<0b100, "autia1716">;
  def AUTIB1716  : SystemNoOperands<0b110, "autib1716">;
}

let Uses = [LR], Defs = [LR], CRm = 0b0000 in {
  def XPACLRI   : SystemNoOperands<0b111, "xpaclri">;
}

// These pointer authentication isntructions require armv8.3a
let Predicates = [HasPA] in {
  multiclass SignAuth<bits<3> prefix, bits<3> prefix_z, string asm> {
    def IA   : SignAuthOneData<prefix, 0b00, !strconcat(asm, "ia")>;
    def IB   : SignAuthOneData<prefix, 0b01, !strconcat(asm, "ib")>;
    def DA   : SignAuthOneData<prefix, 0b10, !strconcat(asm, "da")>;
    def DB   : SignAuthOneData<prefix, 0b11, !strconcat(asm, "db")>;
    def IZA  : SignAuthZero<prefix_z, 0b00, !strconcat(asm, "iza")>;
    def DZA  : SignAuthZero<prefix_z, 0b10, !strconcat(asm, "dza")>;
    def IZB  : SignAuthZero<prefix_z, 0b01, !strconcat(asm, "izb")>;
    def DZB  : SignAuthZero<prefix_z, 0b11, !strconcat(asm, "dzb")>;
  }

  defm PAC : SignAuth<0b000, 0b010, "pac">;
  defm AUT : SignAuth<0b001, 0b011, "aut">;

  def XPACI : SignAuthZero<0b100, 0b00, "xpaci">;
  def XPACD : SignAuthZero<0b100, 0b01, "xpacd">;
  def PACGA : SignAuthTwoOperand<0b1100, "pacga", null_frag>;

  // Combined Instructions
  def BRAA    : AuthBranchTwoOperands<0, 0, "braa">;
  def BRAB    : AuthBranchTwoOperands<0, 1, "brab">;
  def BLRAA   : AuthBranchTwoOperands<1, 0, "blraa">;
  def BLRAB   : AuthBranchTwoOperands<1, 1, "blrab">;

  def BRAAZ   : AuthOneOperand<0b000, 0, "braaz">;
  def BRABZ   : AuthOneOperand<0b000, 1, "brabz">;
  def BLRAAZ  : AuthOneOperand<0b001, 0, "blraaz">;
  def BLRABZ  : AuthOneOperand<0b001, 1, "blrabz">;

  let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
    def RETAA   : AuthReturn<0b010, 0, "retaa">;
    def RETAB   : AuthReturn<0b010, 1, "retab">;
    def ERETAA  : AuthReturn<0b100, 0, "eretaa">;
    def ERETAB  : AuthReturn<0b100, 1, "eretab">;
  }

  defm LDRAA  : AuthLoad<0, "ldraa", simm10Scaled>;
  defm LDRAB  : AuthLoad<1, "ldrab", simm10Scaled>;

}

// v8.3a floating point conversion for javascript
let Predicates = [HasJS, HasFPARMv8] in
def FJCVTZS  : BaseFPToIntegerUnscaled<0b01, 0b11, 0b110, FPR64, GPR32,
                                      "fjcvtzs", []> {
  let Inst{31} = 0;
} // HasJS, HasFPARMv8

// v8.4 Flag manipulation instructions
let Predicates = [HasFMI] in {
def CFINV : SimpleSystemI<0, (ins), "cfinv", "">, Sched<[WriteSys]> {
  let Inst{20-5} = 0b0000001000000000;
}
def SETF8  : BaseFlagManipulation<0, 0, (ins GPR32:$Rn), "setf8", "{\t$Rn}">;
def SETF16 : BaseFlagManipulation<0, 1, (ins GPR32:$Rn), "setf16", "{\t$Rn}">;
def RMIF   : FlagRotate<(ins GPR64:$Rn, uimm6:$imm, imm0_15:$mask), "rmif",
                        "{\t$Rn, $imm, $mask}">;
} // HasFMI

// v8.5 flag manipulation instructions
let Predicates = [HasAltNZCV], Uses = [NZCV], Defs = [NZCV] in {

def XAFLAG : PstateWriteSimple<(ins), "xaflag", "">, Sched<[WriteSys]> {
  let Inst{18-16} = 0b000;
  let Inst{11-8} = 0b0000;
  let Unpredictable{11-8} = 0b1111;
  let Inst{7-5} = 0b001;
}

def AXFLAG : PstateWriteSimple<(ins), "axflag", "">, Sched<[WriteSys]> {
  let Inst{18-16} = 0b000;
  let Inst{11-8} = 0b0000;
  let Unpredictable{11-8} = 0b1111;
  let Inst{7-5} = 0b010;
}
} // HasAltNZCV


// Armv8.5-A speculation barrier
def SB : SimpleSystemI<0, (ins), "sb", "">, Sched<[]> {
  let Inst{20-5} = 0b0001100110000111;
  let Unpredictable{11-8} = 0b1111;
  let Predicates = [HasSB];
  let hasSideEffects = 1;
}

def : InstAlias<"clrex", (CLREX 0xf)>;
def : InstAlias<"isb", (ISB 0xf)>;
def : InstAlias<"ssbb", (DSB 0)>;
def : InstAlias<"pssbb", (DSB 4)>;

def MRS    : MRSI;
def MSR    : MSRI;
def MSRpstateImm1 : MSRpstateImm0_1;
def MSRpstateImm4 : MSRpstateImm0_15;

// The thread pointer (on Linux, at least, where this has been implemented) is
// TPIDR_EL0.
def MOVbaseTLS : Pseudo<(outs GPR64:$dst), (ins),
                       [(set GPR64:$dst, AArch64threadpointer)]>, Sched<[WriteSys]>;

// The cycle counter PMC register is PMCCNTR_EL0.
let Predicates = [HasPerfMon] in
def : Pat<(readcyclecounter), (MRS 0xdce8)>;

// FPCR register
def : Pat<(i64 (int_aarch64_get_fpcr)), (MRS 0xda20)>;

// Generic system instructions
def SYSxt  : SystemXtI<0, "sys">;
def SYSLxt : SystemLXtI<1, "sysl">;

def : InstAlias<"sys $op1, $Cn, $Cm, $op2",
                (SYSxt imm0_7:$op1, sys_cr_op:$Cn,
                 sys_cr_op:$Cm, imm0_7:$op2, XZR)>;

//===----------------------------------------------------------------------===//
// Move immediate instructions.
//===----------------------------------------------------------------------===//

defm MOVK : InsertImmediate<0b11, "movk">;
defm MOVN : MoveImmediate<0b00, "movn">;

let PostEncoderMethod = "fixMOVZ" in
defm MOVZ : MoveImmediate<0b10, "movz">;

// First group of aliases covers an implicit "lsl #0".
def : InstAlias<"movk $dst, $imm", (MOVKWi GPR32:$dst, imm0_65535:$imm, 0), 0>;
def : InstAlias<"movk $dst, $imm", (MOVKXi GPR64:$dst, imm0_65535:$imm, 0), 0>;
def : InstAlias<"movn $dst, $imm", (MOVNWi GPR32:$dst, imm0_65535:$imm, 0)>;
def : InstAlias<"movn $dst, $imm", (MOVNXi GPR64:$dst, imm0_65535:$imm, 0)>;
def : InstAlias<"movz $dst, $imm", (MOVZWi GPR32:$dst, imm0_65535:$imm, 0)>;
def : InstAlias<"movz $dst, $imm", (MOVZXi GPR64:$dst, imm0_65535:$imm, 0)>;

// Next, we have various ELF relocations with the ":XYZ_g0:sym" syntax.
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movz_symbol_g3:$sym, 48)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movz_symbol_g2:$sym, 32)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movz_symbol_g1:$sym, 16)>;
def : InstAlias<"movz $Rd, $sym", (MOVZXi GPR64:$Rd, movz_symbol_g0:$sym, 0)>;

def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movz_symbol_g3:$sym, 48)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movz_symbol_g2:$sym, 32)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movz_symbol_g1:$sym, 16)>;
def : InstAlias<"movn $Rd, $sym", (MOVNXi GPR64:$Rd, movz_symbol_g0:$sym, 0)>;

def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movk_symbol_g3:$sym, 48), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movk_symbol_g2:$sym, 32), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movk_symbol_g1:$sym, 16), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKXi GPR64:$Rd, movk_symbol_g0:$sym, 0), 0>;

def : InstAlias<"movz $Rd, $sym", (MOVZWi GPR32:$Rd, movz_symbol_g1:$sym, 16)>;
def : InstAlias<"movz $Rd, $sym", (MOVZWi GPR32:$Rd, movz_symbol_g0:$sym, 0)>;

def : InstAlias<"movn $Rd, $sym", (MOVNWi GPR32:$Rd, movz_symbol_g1:$sym, 16)>;
def : InstAlias<"movn $Rd, $sym", (MOVNWi GPR32:$Rd, movz_symbol_g0:$sym, 0)>;

def : InstAlias<"movk $Rd, $sym", (MOVKWi GPR32:$Rd, movk_symbol_g1:$sym, 16), 0>;
def : InstAlias<"movk $Rd, $sym", (MOVKWi GPR32:$Rd, movk_symbol_g0:$sym, 0), 0>;

// Final group of aliases covers true "mov $Rd, $imm" cases.
multiclass movw_mov_alias<string basename,Instruction INST, RegisterClass GPR,
                          int width, int shift> {
  def _asmoperand : AsmOperandClass {
    let Name = basename # width # "_lsl" # shift # "MovAlias";
    let PredicateMethod = "is" # basename # "MovAlias<" # width # ", "
                               # shift # ">";
    let RenderMethod = "add" # basename # "MovAliasOperands<" # shift # ">";
  }

  def _movimm : Operand<i32> {
    let ParserMatchClass = !cast<AsmOperandClass>(NAME # "_asmoperand");
  }

  def : InstAlias<"mov $Rd, $imm",
                  (INST GPR:$Rd, !cast<Operand>(NAME # "_movimm"):$imm, shift)>;
}

defm : movw_mov_alias<"MOVZ", MOVZWi, GPR32, 32, 0>;
defm : movw_mov_alias<"MOVZ", MOVZWi, GPR32, 32, 16>;

defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 0>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 16>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 32>;
defm : movw_mov_alias<"MOVZ", MOVZXi, GPR64, 64, 48>;

defm : movw_mov_alias<"MOVN", MOVNWi, GPR32, 32, 0>;
defm : movw_mov_alias<"MOVN", MOVNWi, GPR32, 32, 16>;

defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 0>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 16>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 32>;
defm : movw_mov_alias<"MOVN", MOVNXi, GPR64, 64, 48>;

let isReMaterializable = 1, isCodeGenOnly = 1, isMoveImm = 1,
    isAsCheapAsAMove = 1 in {
// FIXME: The following pseudo instructions are only needed because remat
// cannot handle multiple instructions.  When that changes, we can select
// directly to the real instructions and get rid of these pseudos.

def MOVi32imm
    : Pseudo<(outs GPR32:$dst), (ins i32imm:$src),
             [(set GPR32:$dst, imm:$src)]>,
      Sched<[WriteImm]>;
def MOVi64imm
    : Pseudo<(outs GPR64:$dst), (ins i64imm:$src),
             [(set GPR64:$dst, imm:$src)]>,
      Sched<[WriteImm]>;
} // isReMaterializable, isCodeGenOnly

// If possible, we want to use MOVi32imm even for 64-bit moves. This gives the
// eventual expansion code fewer bits to worry about getting right. Marshalling
// the types is a little tricky though:
def i64imm_32bit : ImmLeaf<i64, [{
  return (Imm & 0xffffffffULL) == static_cast<uint64_t>(Imm);
}]>;

def s64imm_32bit : ImmLeaf<i64, [{
  int64_t Imm64 = static_cast<int64_t>(Imm);
  return Imm64 >= std::numeric_limits<int32_t>::min() &&
         Imm64 <= std::numeric_limits<int32_t>::max();
}]>;

def trunc_imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def gi_trunc_imm : GICustomOperandRenderer<"renderTruncImm">,
  GISDNodeXFormEquiv<trunc_imm>;

def : Pat<(i64 i64imm_32bit:$src),
          (SUBREG_TO_REG (i64 0), (MOVi32imm (trunc_imm imm:$src)), sub_32)>;

// Materialize FP constants via MOVi32imm/MOVi64imm (MachO large code model).
def bitcast_fpimm_to_i32 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def bitcast_fpimm_to_i64 : SDNodeXForm<fpimm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i64);
}]>;


def : Pat<(f32 fpimm:$in),
  (COPY_TO_REGCLASS (MOVi32imm (bitcast_fpimm_to_i32 f32:$in)), FPR32)>;
def : Pat<(f64 fpimm:$in),
  (COPY_TO_REGCLASS (MOVi64imm (bitcast_fpimm_to_i64 f64:$in)), FPR64)>;


// Deal with the various forms of (ELF) large addressing with MOVZ/MOVK
// sequences.
def : Pat<(AArch64WrapperLarge tglobaladdr:$g3, tglobaladdr:$g2,
                             tglobaladdr:$g1, tglobaladdr:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tglobaladdr:$g0, 0),
                                  tglobaladdr:$g1, 16),
                          tglobaladdr:$g2, 32),
                  tglobaladdr:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tblockaddress:$g3, tblockaddress:$g2,
                             tblockaddress:$g1, tblockaddress:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tblockaddress:$g0, 0),
                                  tblockaddress:$g1, 16),
                          tblockaddress:$g2, 32),
                  tblockaddress:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tconstpool:$g3, tconstpool:$g2,
                             tconstpool:$g1, tconstpool:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tconstpool:$g0, 0),
                                  tconstpool:$g1, 16),
                          tconstpool:$g2, 32),
                  tconstpool:$g3, 48)>;

def : Pat<(AArch64WrapperLarge tjumptable:$g3, tjumptable:$g2,
                             tjumptable:$g1, tjumptable:$g0),
          (MOVKXi (MOVKXi (MOVKXi (MOVZXi tjumptable:$g0, 0),
                                  tjumptable:$g1, 16),
                          tjumptable:$g2, 32),
                  tjumptable:$g3, 48)>;


//===----------------------------------------------------------------------===//
// Arithmetic instructions.
//===----------------------------------------------------------------------===//

// Add/subtract with carry.
defm ADC : AddSubCarry<0, "adc", "adcs", AArch64adc, AArch64adc_flag>;
defm SBC : AddSubCarry<1, "sbc", "sbcs", AArch64sbc, AArch64sbc_flag>;

def : InstAlias<"ngc $dst, $src",  (SBCWr  GPR32:$dst, WZR, GPR32:$src)>;
def : InstAlias<"ngc $dst, $src",  (SBCXr  GPR64:$dst, XZR, GPR64:$src)>;
def : InstAlias<"ngcs $dst, $src", (SBCSWr GPR32:$dst, WZR, GPR32:$src)>;
def : InstAlias<"ngcs $dst, $src", (SBCSXr GPR64:$dst, XZR, GPR64:$src)>;

// Add/subtract
defm ADD : AddSub<0, "add", "sub", add>;
defm SUB : AddSub<1, "sub", "add">;

def : InstAlias<"mov $dst, $src",
                (ADDWri GPR32sponly:$dst, GPR32sp:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDWri GPR32sp:$dst, GPR32sponly:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDXri GPR64sponly:$dst, GPR64sp:$src, 0, 0)>;
def : InstAlias<"mov $dst, $src",
                (ADDXri GPR64sp:$dst, GPR64sponly:$src, 0, 0)>;

defm ADDS : AddSubS<0, "adds", AArch64add_flag, "cmn", "subs", "cmp">;
defm SUBS : AddSubS<1, "subs", AArch64sub_flag, "cmp", "adds", "cmn">;

// Use SUBS instead of SUB to enable CSE between SUBS and SUB.
def : Pat<(sub GPR32sp:$Rn, addsub_shifted_imm32:$imm),
          (SUBSWri GPR32sp:$Rn, addsub_shifted_imm32:$imm)>;
def : Pat<(sub GPR64sp:$Rn, addsub_shifted_imm64:$imm),
          (SUBSXri GPR64sp:$Rn, addsub_shifted_imm64:$imm)>;
def : Pat<(sub GPR32:$Rn, GPR32:$Rm),
          (SUBSWrr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(sub GPR64:$Rn, GPR64:$Rm),
          (SUBSXrr GPR64:$Rn, GPR64:$Rm)>;
def : Pat<(sub GPR32:$Rn, arith_shifted_reg32:$Rm),
          (SUBSWrs GPR32:$Rn, arith_shifted_reg32:$Rm)>;
def : Pat<(sub GPR64:$Rn, arith_shifted_reg64:$Rm),
          (SUBSXrs GPR64:$Rn, arith_shifted_reg64:$Rm)>;
let AddedComplexity = 1 in {
def : Pat<(sub GPR32sp:$R2, arith_extended_reg32<i32>:$R3),
          (SUBSWrx GPR32sp:$R2, arith_extended_reg32<i32>:$R3)>;
def : Pat<(sub GPR64sp:$R2, arith_extended_reg32to64<i64>:$R3),
          (SUBSXrx GPR64sp:$R2, arith_extended_reg32to64<i64>:$R3)>;
}

// Because of the immediate format for add/sub-imm instructions, the
// expression (add x, -1) must be transformed to (SUB{W,X}ri x, 1).
//  These patterns capture that transformation.
let AddedComplexity = 1 in {
def : Pat<(add GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (SUBSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(add GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (SUBSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
def : Pat<(sub GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (ADDWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(sub GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (ADDXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
}

// Because of the immediate format for add/sub-imm instructions, the
// expression (add x, -1) must be transformed to (SUB{W,X}ri x, 1).
//  These patterns capture that transformation.
let AddedComplexity = 1 in {
def : Pat<(AArch64add_flag GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (SUBSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(AArch64add_flag GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (SUBSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
def : Pat<(AArch64sub_flag GPR32:$Rn, neg_addsub_shifted_imm32:$imm),
          (ADDSWri GPR32:$Rn, neg_addsub_shifted_imm32:$imm)>;
def : Pat<(AArch64sub_flag GPR64:$Rn, neg_addsub_shifted_imm64:$imm),
          (ADDSXri GPR64:$Rn, neg_addsub_shifted_imm64:$imm)>;
}

def : InstAlias<"neg $dst, $src", (SUBWrs GPR32:$dst, WZR, GPR32:$src, 0), 3>;
def : InstAlias<"neg $dst, $src", (SUBXrs GPR64:$dst, XZR, GPR64:$src, 0), 3>;
def : InstAlias<"neg $dst, $src$shift",
                (SUBWrs GPR32:$dst, WZR, GPR32:$src, arith_shift32:$shift), 2>;
def : InstAlias<"neg $dst, $src$shift",
                (SUBXrs GPR64:$dst, XZR, GPR64:$src, arith_shift64:$shift), 2>;

def : InstAlias<"negs $dst, $src", (SUBSWrs GPR32:$dst, WZR, GPR32:$src, 0), 3>;
def : InstAlias<"negs $dst, $src", (SUBSXrs GPR64:$dst, XZR, GPR64:$src, 0), 3>;
def : InstAlias<"negs $dst, $src$shift",
                (SUBSWrs GPR32:$dst, WZR, GPR32:$src, arith_shift32:$shift), 2>;
def : InstAlias<"negs $dst, $src$shift",
                (SUBSXrs GPR64:$dst, XZR, GPR64:$src, arith_shift64:$shift), 2>;


// Unsigned/Signed divide
defm UDIV : Div<0, "udiv", udiv>;
defm SDIV : Div<1, "sdiv", sdiv>;

def : Pat<(int_aarch64_udiv GPR32:$Rn, GPR32:$Rm), (UDIVWr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(int_aarch64_udiv GPR64:$Rn, GPR64:$Rm), (UDIVXr GPR64:$Rn, GPR64:$Rm)>;
def : Pat<(int_aarch64_sdiv GPR32:$Rn, GPR32:$Rm), (SDIVWr GPR32:$Rn, GPR32:$Rm)>;
def : Pat<(int_aarch64_sdiv GPR64:$Rn, GPR64:$Rm), (SDIVXr GPR64:$Rn, GPR64:$Rm)>;

// Variable shift
defm ASRV : Shift<0b10, "asr", sra>;
defm LSLV : Shift<0b00, "lsl", shl>;
defm LSRV : Shift<0b01, "lsr", srl>;
defm RORV : Shift<0b11, "ror", rotr>;

def : ShiftAlias<"asrv", ASRVWr, GPR32>;
def : ShiftAlias<"asrv", ASRVXr, GPR64>;
def : ShiftAlias<"lslv", LSLVWr, GPR32>;
def : ShiftAlias<"lslv", LSLVXr, GPR64>;
def : ShiftAlias<"lsrv", LSRVWr, GPR32>;
def : ShiftAlias<"lsrv", LSRVXr, GPR64>;
def : ShiftAlias<"rorv", RORVWr, GPR32>;
def : ShiftAlias<"rorv", RORVXr, GPR64>;

// Multiply-add
let AddedComplexity = 5 in {
defm MADD : MulAccum<0, "madd", add>;
defm MSUB : MulAccum<1, "msub", sub>;

def : Pat<(i32 (mul GPR32:$Rn, GPR32:$Rm)),
          (MADDWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (mul GPR64:$Rn, GPR64:$Rm)),
          (MADDXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;

def : Pat<(i32 (ineg (mul GPR32:$Rn, GPR32:$Rm))),
          (MSUBWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (ineg (mul GPR64:$Rn, GPR64:$Rm))),
          (MSUBXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;
def : Pat<(i32 (mul (ineg GPR32:$Rn), GPR32:$Rm)),
          (MSUBWrrr GPR32:$Rn, GPR32:$Rm, WZR)>;
def : Pat<(i64 (mul (ineg GPR64:$Rn), GPR64:$Rm)),
          (MSUBXrrr GPR64:$Rn, GPR64:$Rm, XZR)>;
} // AddedComplexity = 5

let AddedComplexity = 5 in {
def SMADDLrrr : WideMulAccum<0, 0b001, "smaddl", add, sext>;
def SMSUBLrrr : WideMulAccum<1, 0b001, "smsubl", sub, sext>;
def UMADDLrrr : WideMulAccum<0, 0b101, "umaddl", add, zext>;
def UMSUBLrrr : WideMulAccum<1, 0b101, "umsubl", sub, zext>;

def : Pat<(i64 (mul (sext GPR32:$Rn), (sext GPR32:$Rm))),
          (SMADDLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;
def : Pat<(i64 (mul (zext GPR32:$Rn), (zext GPR32:$Rm))),
          (UMADDLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;

def : Pat<(i64 (ineg (mul (sext GPR32:$Rn), (sext GPR32:$Rm)))),
          (SMSUBLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;
def : Pat<(i64 (ineg (mul (zext GPR32:$Rn), (zext GPR32:$Rm)))),
          (UMSUBLrrr GPR32:$Rn, GPR32:$Rm, XZR)>;

def : Pat<(i64 (mul (sext GPR32:$Rn), (s64imm_32bit:$C))),
          (SMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (mul (zext GPR32:$Rn), (i64imm_32bit:$C))),
          (UMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C))),
          (SMADDLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), XZR)>;

def : Pat<(i64 (ineg (mul (sext GPR32:$Rn), (s64imm_32bit:$C)))),
          (SMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (ineg (mul (zext GPR32:$Rn), (i64imm_32bit:$C)))),
          (UMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), XZR)>;
def : Pat<(i64 (ineg (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C)))),
          (SMSUBLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), XZR)>;

def : Pat<(i64 (add (mul (sext GPR32:$Rn), (s64imm_32bit:$C)), GPR64:$Ra)),
          (SMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (add (mul (zext GPR32:$Rn), (i64imm_32bit:$C)), GPR64:$Ra)),
          (UMADDLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (add (mul (sext_inreg GPR64:$Rn, i32), (s64imm_32bit:$C)),
                    GPR64:$Ra)),
          (SMADDLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;

def : Pat<(i64 (sub GPR64:$Ra, (mul (sext GPR32:$Rn), (s64imm_32bit:$C)))),
          (SMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (mul (zext GPR32:$Rn), (i64imm_32bit:$C)))),
          (UMSUBLrrr GPR32:$Rn, (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
def : Pat<(i64 (sub GPR64:$Ra, (mul (sext_inreg GPR64:$Rn, i32),
                                    (s64imm_32bit:$C)))),
          (SMSUBLrrr (i32 (EXTRACT_SUBREG GPR64:$Rn, sub_32)),
                     (MOVi32imm (trunc_imm imm:$C)), GPR64:$Ra)>;
} // AddedComplexity = 5

def : MulAccumWAlias<"mul", MADDWrrr>;
def : MulAccumXAlias<"mul", MADDXrrr>;
def : MulAccumWAlias<"mneg", MSUBWrrr>;
def : MulAccumXAlias<"mneg", MSUBXrrr>;
def : WideMulAccumAlias<"smull", SMADDLrrr>;
def : WideMulAccumAlias<"smnegl", SMSUBLrrr>;
def : WideMulAccumAlias<"umull", UMADDLrrr>;
def : WideMulAccumAlias<"umnegl", UMSUBLrrr>;

// Multiply-high
def SMULHrr : MulHi<0b010, "smulh", mulhs>;
def UMULHrr : MulHi<0b110, "umulh", mulhu>;

// CRC32
def CRC32Brr : BaseCRC32<0, 0b00, 0, GPR32, int_aarch64_crc32b, "crc32b">;
def CRC32Hrr : BaseCRC32<0, 0b01, 0, GPR32, int_aarch64_crc32h, "crc32h">;
def CRC32Wrr : BaseCRC32<0, 0b10, 0, GPR32, int_aarch64_crc32w, "crc32w">;
def CRC32Xrr : BaseCRC32<1, 0b11, 0, GPR64, int_aarch64_crc32x, "crc32x">;

def CRC32CBrr : BaseCRC32<0, 0b00, 1, GPR32, int_aarch64_crc32cb, "crc32cb">;
def CRC32CHrr : BaseCRC32<0, 0b01, 1, GPR32, int_aarch64_crc32ch, "crc32ch">;
def CRC32CWrr : BaseCRC32<0, 0b10, 1, GPR32, int_aarch64_crc32cw, "crc32cw">;
def CRC32CXrr : BaseCRC32<1, 0b11, 1, GPR64, int_aarch64_crc32cx, "crc32cx">;

// v8.1 atomic CAS
defm CAS   : CompareAndSwap<0, 0, "">;
defm CASA  : CompareAndSwap<1, 0, "a">;
defm CASL  : CompareAndSwap<0, 1, "l">;
defm CASAL : CompareAndSwap<1, 1, "al">;

// v8.1 atomic CASP
defm CASP   : CompareAndSwapPair<0, 0, "">;
defm CASPA  : CompareAndSwapPair<1, 0, "a">;
defm CASPL  : CompareAndSwapPair<0, 1, "l">;
defm CASPAL : CompareAndSwapPair<1, 1, "al">;

// v8.1 atomic SWP
defm SWP   : Swap<0, 0, "">;
defm SWPA  : Swap<1, 0, "a">;
defm SWPL  : Swap<0, 1, "l">;
defm SWPAL : Swap<1, 1, "al">;

// v8.1 atomic LD<OP>(register). Performs load and then ST<OP>(register)
defm LDADD   : LDOPregister<0b000, "add", 0, 0, "">;
defm LDADDA  : LDOPregister<0b000, "add", 1, 0, "a">;
defm LDADDL  : LDOPregister<0b000, "add", 0, 1, "l">;
defm LDADDAL : LDOPregister<0b000, "add", 1, 1, "al">;

defm LDCLR   : LDOPregister<0b001, "clr", 0, 0, "">;
defm LDCLRA  : LDOPregister<0b001, "clr", 1, 0, "a">;
defm LDCLRL  : LDOPregister<0b001, "clr", 0, 1, "l">;
defm LDCLRAL : LDOPregister<0b001, "clr", 1, 1, "al">;

defm LDEOR   : LDOPregister<0b010, "eor", 0, 0, "">;
defm LDEORA  : LDOPregister<0b010, "eor", 1, 0, "a">;
defm LDEORL  : LDOPregister<0b010, "eor", 0, 1, "l">;
defm LDEORAL : LDOPregister<0b010, "eor", 1, 1, "al">;

defm LDSET   : LDOPregister<0b011, "set", 0, 0, "">;
defm LDSETA  : LDOPregister<0b011, "set", 1, 0, "a">;
defm LDSETL  : LDOPregister<0b011, "set", 0, 1, "l">;
defm LDSETAL : LDOPregister<0b011, "set", 1, 1, "al">;

defm LDSMAX   : LDOPregister<0b100, "smax", 0, 0, "">;
defm LDSMAXA  : LDOPregister<0b100, "smax", 1, 0, "a">;
defm LDSMAXL  : LDOPregister<0b100, "smax", 0, 1, "l">;
defm LDSMAXAL : LDOPregister<0b100, "smax", 1, 1, "al">;

defm LDSMIN   : LDOPregister<0b101, "smin", 0, 0, "">;
defm LDSMINA  : LDOPregister<0b101, "smin", 1, 0, "a">;
defm LDSMINL  : LDOPregister<0b101, "smin", 0, 1, "l">;
defm LDSMINAL : LDOPregister<0b101, "smin", 1, 1, "al">;

defm LDUMAX   : LDOPregister<0b110, "umax", 0, 0, "">;
defm LDUMAXA  : LDOPregister<0b110, "umax", 1, 0, "a">;
defm LDUMAXL  : LDOPregister<0b110, "umax", 0, 1, "l">;
defm LDUMAXAL : LDOPregister<0b110, "umax", 1, 1, "al">;

defm LDUMIN   : LDOPregister<0b111, "umin", 0, 0, "">;
defm LDUMINA  : LDOPregister<0b111, "umin", 1, 0, "a">;
defm LDUMINL  : LDOPregister<0b111, "umin", 0, 1, "l">;
defm LDUMINAL : LDOPregister<0b111, "umin", 1, 1, "al">;

// v8.1 atomic ST<OP>(register) as aliases to "LD<OP>(register) when Rt=xZR"
defm : STOPregister<"stadd","LDADD">; // STADDx
defm : STOPregister<"stclr","LDCLR">; // STCLRx
defm : STOPregister<"steor","LDEOR">; // STEORx
defm : STOPregister<"stset","LDSET">; // STSETx
defm : STOPregister<"stsmax","LDSMAX">;// STSMAXx
defm : STOPregister<"stsmin","LDSMIN">;// STSMINx
defm : STOPregister<"stumax","LDUMAX">;// STUMAXx
defm : STOPregister<"stumin","LDUMIN">;// STUMINx

// v8.5 Memory Tagging Extension
let Predicates = [HasMTE] in {

def IRG   : BaseTwoOperand<0b0100, GPR64sp, "irg", null_frag, GPR64sp, GPR64>,
            Sched<[]>{
  let Inst{31} = 1;
}
def GMI   : BaseTwoOperand<0b0101, GPR64, "gmi", null_frag, GPR64sp>, Sched<[]>{
  let Inst{31} = 1;
  let isNotDuplicable = 1;
}
def ADDG  : AddSubG<0, "addg", null_frag>;
def SUBG  : AddSubG<1, "subg", null_frag>;

def : InstAlias<"irg $dst, $src", (IRG GPR64sp:$dst, GPR64sp:$src, XZR), 1>;

def SUBP : SUBP<0, "subp", null_frag>, Sched<[]>;
def SUBPS : SUBP<1, "subps", null_frag>, Sched<[]>{
  let Defs = [NZCV];
}

def : InstAlias<"cmpp $lhs, $rhs", (SUBPS XZR, GPR64sp:$lhs, GPR64sp:$rhs), 0>;

def LDG : MemTagLoad<"ldg", "\t$Rt, [$Rn, $offset]">;
def : InstAlias<"ldg $Rt, [$Rn]", (LDG GPR64:$Rt, GPR64sp:$Rn, 0), 1>;

def LDGV : MemTagVector<1, "ldgv", "\t$Rt, [$Rn]!",
                   (outs GPR64sp:$wback, GPR64:$Rt), (ins GPR64sp:$Rn)> {
  let DecoderMethod = "DecodeLoadAllocTagArrayInstruction";
}
def STGV : MemTagVector<0, "stgv", "\t$Rt, [$Rn]!",
                   (outs GPR64sp:$wback), (ins GPR64:$Rt, GPR64sp:$Rn)>;

defm STG   : MemTagStore<0b00, "stg">;
defm STZG  : MemTagStore<0b01, "stzg">;
defm ST2G  : MemTagStore<0b10, "st2g">;
defm STZ2G : MemTagStore<0b11, "stz2g">;

defm STGP     : StorePairOffset <0b01, 0, GPR64z, simm7s16, "stgp">;
def  STGPpre  : StorePairPreIdx <0b01, 0, GPR64z, simm7s16, "stgp">;
def  STGPpost : StorePairPostIdx<0b01, 0, GPR64z, simm7s16, "stgp">;

} // Predicates = [HasMTE]

//===----------------------------------------------------------------------===//
// Logical instructions.
//===----------------------------------------------------------------------===//

// (immediate)
defm ANDS : LogicalImmS<0b11, "ands", AArch64and_flag, "bics">;
defm AND  : LogicalImm<0b00, "and", and, "bic">;
defm EOR  : LogicalImm<0b10, "eor", xor, "eon">;
defm ORR  : LogicalImm<0b01, "orr", or, "orn">;

// FIXME: these aliases *are* canonical sometimes (when movz can't be
// used). Actually, it seems to be working right now, but putting logical_immXX
// here is a bit dodgy on the AsmParser side too.
def : InstAlias<"mov $dst, $imm", (ORRWri GPR32sp:$dst, WZR,
                                          logical_imm32:$imm), 0>;
def : InstAlias<"mov $dst, $imm", (ORRXri GPR64sp:$dst, XZR,
                                          logical_imm64:$imm), 0>;


// (register)
defm ANDS : LogicalRegS<0b11, 0, "ands", AArch64and_flag>;
defm BICS : LogicalRegS<0b11, 1, "bics",
                        BinOpFrag<(AArch64and_flag node:$LHS, (not node:$RHS))>>;
defm AND  : LogicalReg<0b00, 0, "and", and>;
defm BIC  : LogicalReg<0b00, 1, "bic",
                       BinOpFrag<(and node:$LHS, (not node:$RHS))>>;
defm EON  : LogicalReg<0b10, 1, "eon",
                       BinOpFrag<(not (xor node:$LHS, node:$RHS))>>;
defm EOR  : LogicalReg<0b10, 0, "eor", xor>;
defm ORN  : LogicalReg<0b01, 1, "orn",
                       BinOpFrag<(or node:$LHS, (not node:$RHS))>>;
defm ORR  : LogicalReg<0b01, 0, "orr", or>;

def : InstAlias<"mov $dst, $src", (ORRWrs GPR32:$dst, WZR, GPR32:$src, 0), 2>;
def : InstAlias<"mov $dst, $src", (ORRXrs GPR64:$dst, XZR, GPR64:$src, 0), 2>;

def : InstAlias<"mvn $Wd, $Wm", (ORNWrs GPR32:$Wd, WZR, GPR32:$Wm, 0), 3>;
def : InstAlias<"mvn $Xd, $Xm", (ORNXrs GPR64:$Xd, XZR, GPR64:$Xm, 0), 3>;

def : InstAlias<"mvn $Wd, $Wm$sh",
                (ORNWrs GPR32:$Wd, WZR, GPR32:$Wm, logical_shift32:$sh), 2>;
def : InstAlias<"mvn $Xd, $Xm$sh",
                (ORNXrs GPR64:$Xd, XZR, GPR64:$Xm, logical_shift64:$sh), 2>;

def : InstAlias<"tst $src1, $src2",
                (ANDSWri WZR, GPR32:$src1, logical_imm32:$src2), 2>;
def : InstAlias<"tst $src1, $src2",
                (ANDSXri XZR, GPR64:$src1, logical_imm64:$src2), 2>;

def : InstAlias<"tst $src1, $src2",
                        (ANDSWrs WZR, GPR32:$src1, GPR32:$src2, 0), 3>;
def : InstAlias<"tst $src1, $src2",
                        (ANDSXrs XZR, GPR64:$src1, GPR64:$src2, 0), 3>;

def : InstAlias<"tst $src1, $src2$sh",
               (ANDSWrs WZR, GPR32:$src1, GPR32:$src2, logical_shift32:$sh), 2>;
def : InstAlias<"tst $src1, $src2$sh",
               (ANDSXrs XZR, GPR64:$src1, GPR64:$src2, logical_shift64:$sh), 2>;


def : Pat<(not GPR32:$Wm), (ORNWrr WZR, GPR32:$Wm)>;
def : Pat<(not GPR64:$Xm), (ORNXrr XZR, GPR64:$Xm)>;


//===----------------------------------------------------------------------===//
// One operand data processing instructions.
//===----------------------------------------------------------------------===//

defm CLS    : OneOperandData<0b101, "cls">;
defm CLZ    : OneOperandData<0b100, "clz", ctlz>;
defm RBIT   : OneOperandData<0b000, "rbit", bitreverse>;

def  REV16Wr : OneWRegData<0b001, "rev16",
                                  UnOpFrag<(rotr (bswap node:$LHS), (i64 16))>>;
def  REV16Xr : OneXRegData<0b001, "rev16", null_frag>;

def : Pat<(cttz GPR32:$Rn),
          (CLZWr (RBITWr GPR32:$Rn))>;
def : Pat<(cttz GPR64:$Rn),
          (CLZXr (RBITXr GPR64:$Rn))>;
def : Pat<(ctlz (or (shl (xor (sra GPR32:$Rn, (i64 31)), GPR32:$Rn), (i64 1)),
                (i32 1))),
          (CLSWr GPR32:$Rn)>;
def : Pat<(ctlz (or (shl (xor (sra GPR64:$Rn, (i64 63)), GPR64:$Rn), (i64 1)),
                (i64 1))),
          (CLSXr GPR64:$Rn)>;

// Unlike the other one operand instructions, the instructions with the "rev"
// mnemonic do *not* just different in the size bit, but actually use different
// opcode bits for the different sizes.
def REVWr   : OneWRegData<0b010, "rev", bswap>;
def REVXr   : OneXRegData<0b011, "rev", bswap>;
def REV32Xr : OneXRegData<0b010, "rev32",
                                 UnOpFrag<(rotr (bswap node:$LHS), (i64 32))>>;

def : InstAlias<"rev64 $Rd, $Rn", (REVXr GPR64:$Rd, GPR64:$Rn), 0>;

// The bswap commutes with the rotr so we want a pattern for both possible
// orders.
def : Pat<(bswap (rotr GPR32:$Rn, (i64 16))), (REV16Wr GPR32:$Rn)>;
def : Pat<(bswap (rotr GPR64:$Rn, (i64 32))), (REV32Xr GPR64:$Rn)>;

//===----------------------------------------------------------------------===//
// Bitfield immediate extraction instruction.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0 in
defm EXTR : ExtractImm<"extr">;
def : InstAlias<"ror $dst, $src, $shift",
            (EXTRWrri GPR32:$dst, GPR32:$src, GPR32:$src, imm0_31:$shift)>;
def : InstAlias<"ror $dst, $src, $shift",
            (EXTRXrri GPR64:$dst, GPR64:$src, GPR64:$src, imm0_63:$shift)>;

def : Pat<(rotr GPR32:$Rn, (i64 imm0_31:$imm)),
          (EXTRWrri GPR32:$Rn, GPR32:$Rn, imm0_31:$imm)>;
def : Pat<(rotr GPR64:$Rn, (i64 imm0_63:$imm)),
          (EXTRXrri GPR64:$Rn, GPR64:$Rn, imm0_63:$imm)>;

//===----------------------------------------------------------------------===//
// Other bitfield immediate instructions.
//===----------------------------------------------------------------------===//
let hasSideEffects = 0 in {
defm BFM  : BitfieldImmWith2RegArgs<0b01, "bfm">;
defm SBFM : BitfieldImm<0b00, "sbfm">;
defm UBFM : BitfieldImm<0b10, "ubfm">;
}

def i32shift_a : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = (32 - N->getZExtValue()) & 0x1f;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i32shift_b : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(7, 31 - shift_amt)
def i32shift_sext_i8 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  enc = enc > 7 ? 7 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(15, 31 - shift_amt)
def i32shift_sext_i16 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 31 - N->getZExtValue();
  enc = enc > 15 ? 15 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i64shift_a : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = (64 - N->getZExtValue()) & 0x3f;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def i64shift_b : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(7, 63 - shift_amt)
def i64shift_sext_i8 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 7 ? 7 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(15, 63 - shift_amt)
def i64shift_sext_i16 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 15 ? 15 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

// min(31, 63 - shift_amt)
def i64shift_sext_i32 : Operand<i64>, SDNodeXForm<imm, [{
  uint64_t enc = 63 - N->getZExtValue();
  enc = enc > 31 ? 31 : enc;
  return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i64);
}]>;

def : Pat<(shl GPR32:$Rn, (i64 imm0_31:$imm)),
          (UBFMWri GPR32:$Rn, (i64 (i32shift_a imm0_31:$imm)),
                              (i64 (i32shift_b imm0_31:$imm)))>;
def : Pat<(shl GPR64:$Rn, (i64 imm0_63:$imm)),
          (UBFMXri GPR64:$Rn, (i64 (i64shift_a imm0_63:$imm)),
                              (i64 (i64shift_b imm0_63:$imm)))>;

let AddedComplexity = 10 in {
def : Pat<(sra GPR32:$Rn, (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, imm0_31:$imm, 31)>;
def : Pat<(sra GPR64:$Rn, (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, imm0_63:$imm, 63)>;
}

def : InstAlias<"asr $dst, $src, $shift",
                (SBFMWri GPR32:$dst, GPR32:$src, imm0_31:$shift, 31)>;
def : InstAlias<"asr $dst, $src, $shift",
                (SBFMXri GPR64:$dst, GPR64:$src, imm0_63:$shift, 63)>;
def : InstAlias<"sxtb $dst, $src", (SBFMWri GPR32:$dst, GPR32:$src, 0, 7)>;
def : InstAlias<"sxtb $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 7)>;
def : InstAlias<"sxth $dst, $src", (SBFMWri GPR32:$dst, GPR32:$src, 0, 15)>;
def : InstAlias<"sxth $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 15)>;
def : InstAlias<"sxtw $dst, $src", (SBFMXri GPR64:$dst, GPR64:$src, 0, 31)>;

def : Pat<(srl GPR32:$Rn, (i64 imm0_31:$imm)),
          (UBFMWri GPR32:$Rn, imm0_31:$imm, 31)>;
def : Pat<(srl GPR64:$Rn, (i64 imm0_63:$imm)),
          (UBFMXri GPR64:$Rn, imm0_63:$imm, 63)>;

def : InstAlias<"lsr $dst, $src, $shift",
                (UBFMWri GPR32:$dst, GPR32:$src, imm0_31:$shift, 31)>;
def : InstAlias<"lsr $dst, $src, $shift",
                (UBFMXri GPR64:$dst, GPR64:$src, imm0_63:$shift, 63)>;
def : InstAlias<"uxtb $dst, $src", (UBFMWri GPR32:$dst, GPR32:$src, 0, 7)>;
def : InstAlias<"uxtb $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 7)>;
def : InstAlias<"uxth $dst, $src", (UBFMWri GPR32:$dst, GPR32:$src, 0, 15)>;
def : InstAlias<"uxth $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 15)>;
def : InstAlias<"uxtw $dst, $src", (UBFMXri GPR64:$dst, GPR64:$src, 0, 31)>;

//===----------------------------------------------------------------------===//
// Conditional comparison instructions.
//===----------------------------------------------------------------------===//
defm CCMN : CondComparison<0, "ccmn", AArch64ccmn>;
defm CCMP : CondComparison<1, "ccmp", AArch64ccmp>;

//===----------------------------------------------------------------------===//
// Conditional select instructions.
//===----------------------------------------------------------------------===//
defm CSEL  : CondSelect<0, 0b00, "csel">;

def inc : PatFrag<(ops node:$in), (add node:$in, 1)>;
defm CSINC : CondSelectOp<0, 0b01, "csinc", inc>;
defm CSINV : CondSelectOp<1, 0b00, "csinv", not>;
defm CSNEG : CondSelectOp<1, 0b01, "csneg", ineg>;

def : Pat<(AArch64csinv GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinv GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csneg GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSNEGWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csneg GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSNEGXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinc GPR32:$tval, GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$tval, GPR32:$fval, (i32 imm:$cc))>;
def : Pat<(AArch64csinc GPR64:$tval, GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$tval, GPR64:$fval, (i32 imm:$cc))>;

def : Pat<(AArch64csel (i32 0), (i32 1), (i32 imm:$cc), NZCV),
          (CSINCWr WZR, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i64 0), (i64 1), (i32 imm:$cc), NZCV),
          (CSINCXr XZR, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR32:$tval, (i32 1), (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$tval, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR64:$tval, (i64 1), (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$tval, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i32 1), GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINCWr GPR32:$fval, WZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i64 1), GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINCXr GPR64:$fval, XZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i32 0), (i32 -1), (i32 imm:$cc), NZCV),
          (CSINVWr WZR, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i64 0), (i64 -1), (i32 imm:$cc), NZCV),
          (CSINVXr XZR, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR32:$tval, (i32 -1), (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$tval, WZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel GPR64:$tval, (i64 -1), (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$tval, XZR, (i32 imm:$cc))>;
def : Pat<(AArch64csel (i32 -1), GPR32:$fval, (i32 imm:$cc), NZCV),
          (CSINVWr GPR32:$fval, WZR, (i32 (inv_cond_XFORM imm:$cc)))>;
def : Pat<(AArch64csel (i64 -1), GPR64:$fval, (i32 imm:$cc), NZCV),
          (CSINVXr GPR64:$fval, XZR, (i32 (inv_cond_XFORM imm:$cc)))>;

// The inverse of the condition code from the alias instruction is what is used
// in the aliased instruction. The parser all ready inverts the condition code
// for these aliases.
def : InstAlias<"cset $dst, $cc",
                (CSINCWr GPR32:$dst, WZR, WZR, inv_ccode:$cc)>;
def : InstAlias<"cset $dst, $cc",
                (CSINCXr GPR64:$dst, XZR, XZR, inv_ccode:$cc)>;

def : InstAlias<"csetm $dst, $cc",
                (CSINVWr GPR32:$dst, WZR, WZR, inv_ccode:$cc)>;
def : InstAlias<"csetm $dst, $cc",
                (CSINVXr GPR64:$dst, XZR, XZR, inv_ccode:$cc)>;

def : InstAlias<"cinc $dst, $src, $cc",
                (CSINCWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cinc $dst, $src, $cc",
                (CSINCXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

def : InstAlias<"cinv $dst, $src, $cc",
                (CSINVWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cinv $dst, $src, $cc",
                (CSINVXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

def : InstAlias<"cneg $dst, $src, $cc",
                (CSNEGWr GPR32:$dst, GPR32:$src, GPR32:$src, inv_ccode:$cc)>;
def : InstAlias<"cneg $dst, $src, $cc",
                (CSNEGXr GPR64:$dst, GPR64:$src, GPR64:$src, inv_ccode:$cc)>;

//===----------------------------------------------------------------------===//
// PC-relative instructions.
//===----------------------------------------------------------------------===//
let isReMaterializable = 1 in {
let hasSideEffects = 0, mayStore = 0, mayLoad = 0 in {
def ADR  : ADRI<0, "adr", adrlabel,
                [(set GPR64:$Xd, (AArch64adr tglobaladdr:$label))]>;
} // hasSideEffects = 0

def ADRP : ADRI<1, "adrp", adrplabel,
                [(set GPR64:$Xd, (AArch64adrp tglobaladdr:$label))]>;
} // isReMaterializable = 1

// page address of a constant pool entry, block address
def : Pat<(AArch64adr tconstpool:$cp), (ADR tconstpool:$cp)>;
def : Pat<(AArch64adr tblockaddress:$cp), (ADR tblockaddress:$cp)>;
def : Pat<(AArch64adr texternalsym:$sym), (ADR texternalsym:$sym)>;
def : Pat<(AArch64adr tjumptable:$sym), (ADR tjumptable:$sym)>;
def : Pat<(AArch64adrp tconstpool:$cp), (ADRP tconstpool:$cp)>;
def : Pat<(AArch64adrp tblockaddress:$cp), (ADRP tblockaddress:$cp)>;
def : Pat<(AArch64adrp texternalsym:$sym), (ADRP texternalsym:$sym)>;

//===----------------------------------------------------------------------===//
// Unconditional branch (register) instructions.
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
def RET  : BranchReg<0b0010, "ret", []>;
def DRPS : SpecialReturn<0b0101, "drps">;
def ERET : SpecialReturn<0b0100, "eret">;
} // isReturn = 1, isTerminator = 1, isBarrier = 1

// Default to the LR register.
def : InstAlias<"ret", (RET LR)>;

let isCall = 1, Defs = [LR], Uses = [SP] in {
def BLR : BranchReg<0b0001, "blr", [(AArch64call GPR64:$Rn)]>;
} // isCall

let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
def BR  : BranchReg<0b0000, "br", [(brind GPR64:$Rn)]>;
} // isBranch, isTerminator, isBarrier, isIndirectBranch

// Create a separate pseudo-instruction for codegen to use so that we don't
// flag lr as used in every function. It'll be restored before the RET by the
// epilogue if it's legitimately used.
def RET_ReallyLR : Pseudo<(outs), (ins), [(AArch64retflag)]>,
                   Sched<[WriteBrReg]> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
}

// This is a directive-like pseudo-instruction. The purpose is to insert an
// R_AARCH64_TLSDESC_CALL relocation at the offset of the following instruction
// (which in the usual case is a BLR).
let hasSideEffects = 1 in
def TLSDESCCALL : Pseudo<(outs), (ins i64imm:$sym), []>, Sched<[]> {
  let AsmString = ".tlsdesccall $sym";
}

// Pseudo instruction to tell the streamer to emit a 'B' character into the
// augmentation string.
def EMITBKEY : Pseudo<(outs), (ins), []>, Sched<[]> {}

// FIXME: maybe the scratch register used shouldn't be fixed to X1?
// FIXME: can "hasSideEffects be dropped?
let isCall = 1, Defs = [LR, X0, X1], hasSideEffects = 1,
    isCodeGenOnly = 1 in
def TLSDESC_CALLSEQ
    : Pseudo<(outs), (ins i64imm:$sym),
             [(AArch64tlsdesc_callseq tglobaltlsaddr:$sym)]>,
      Sched<[WriteI, WriteLD, WriteI, WriteBrReg]>;
def : Pat<(AArch64tlsdesc_callseq texternalsym:$sym),
          (TLSDESC_CALLSEQ texternalsym:$sym)>;

//===----------------------------------------------------------------------===//
// Conditional branch (immediate) instruction.
//===----------------------------------------------------------------------===//
def Bcc : BranchCond;

//===----------------------------------------------------------------------===//
// Compare-and-branch instructions.
//===----------------------------------------------------------------------===//
defm CBZ  : CmpBranch<0, "cbz", AArch64cbz>;
defm CBNZ : CmpBranch<1, "cbnz", AArch64cbnz>;

//===----------------------------------------------------------------------===//
// Test-bit-and-branch instructions.
//===----------------------------------------------------------------------===//
defm TBZ  : TestBranch<0, "tbz", AArch64tbz>;
defm TBNZ : TestBranch<1, "tbnz", AArch64tbnz>;

//===----------------------------------------------------------------------===//
// Unconditional branch (immediate) instructions.
//===----------------------------------------------------------------------===//
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
def B  : BranchImm<0, "b", [(br bb:$addr)]>;
} // isBranch, isTerminator, isBarrier

let isCall = 1, Defs = [LR], Uses = [SP] in {
def BL : CallImm<1, "bl", [(AArch64call tglobaladdr:$addr)]>;
} // isCall
def : Pat<(AArch64call texternalsym:$func), (BL texternalsym:$func)>;

//===----------------------------------------------------------------------===//
// Exception generation instructions.
//===----------------------------------------------------------------------===//
let isTrap = 1 in {
def BRK   : ExceptionGeneration<0b001, 0b00, "brk">;
}
def DCPS1 : ExceptionGeneration<0b101, 0b01, "dcps1">;
def DCPS2 : ExceptionGeneration<0b101, 0b10, "dcps2">;
def DCPS3 : ExceptionGeneration<0b101, 0b11, "dcps3">;
def HLT   : ExceptionGeneration<0b010, 0b00, "hlt">;
def HVC   : ExceptionGeneration<0b000, 0b10, "hvc">;
def SMC   : ExceptionGeneration<0b000, 0b11, "smc">;
def SVC   : ExceptionGeneration<0b000, 0b01, "svc">;

// DCPSn defaults to an immediate operand of zero if unspecified.
def : InstAlias<"dcps1", (DCPS1 0)>;
def : InstAlias<"dcps2", (DCPS2 0)>;
def : InstAlias<"dcps3", (DCPS3 0)>;

def UDF : UDFType<0, "udf">;

//===----------------------------------------------------------------------===//
// Load instructions.
//===----------------------------------------------------------------------===//

// Pair (indexed, offset)
defm LDPW : LoadPairOffset<0b00, 0, GPR32z, simm7s4, "ldp">;
defm LDPX : LoadPairOffset<0b10, 0, GPR64z, simm7s8, "ldp">;
defm LDPS : LoadPairOffset<0b00, 1, FPR32Op, simm7s4, "ldp">;
defm LDPD : LoadPairOffset<0b01, 1, FPR64Op, simm7s8, "ldp">;
defm LDPQ : LoadPairOffset<0b10, 1, FPR128Op, simm7s16, "ldp">;

defm LDPSW : LoadPairOffset<0b01, 0, GPR64z, simm7s4, "ldpsw">;

// Pair (pre-indexed)
def LDPWpre : LoadPairPreIdx<0b00, 0, GPR32z, simm7s4, "ldp">;
def LDPXpre : LoadPairPreIdx<0b10, 0, GPR64z, simm7s8, "ldp">;
def LDPSpre : LoadPairPreIdx<0b00, 1, FPR32Op, simm7s4, "ldp">;
def LDPDpre : LoadPairPreIdx<0b01, 1, FPR64Op, simm7s8, "ldp">;
def LDPQpre : LoadPairPreIdx<0b10, 1, FPR128Op, simm7s16, "ldp">;

def LDPSWpre : LoadPairPreIdx<0b01, 0, GPR64z, simm7s4, "ldpsw">;

// Pair (post-indexed)
def LDPWpost : LoadPairPostIdx<0b00, 0, GPR32z, simm7s4, "ldp">;
def LDPXpost : LoadPairPostIdx<0b10, 0, GPR64z, simm7s8, "ldp">;
def LDPSpost : LoadPairPostIdx<0b00, 1, FPR32Op, simm7s4, "ldp">;
def LDPDpost : LoadPairPostIdx<0b01, 1, FPR64Op, simm7s8, "ldp">;
def LDPQpost : LoadPairPostIdx<0b10, 1, FPR128Op, simm7s16, "ldp">;

def LDPSWpost : LoadPairPostIdx<0b01, 0, GPR64z, simm7s4, "ldpsw">;


// Pair (no allocate)
defm LDNPW : LoadPairNoAlloc<0b00, 0, GPR32z, simm7s4, "ldnp">;
defm LDNPX : LoadPairNoAlloc<0b10, 0, GPR64z, simm7s8, "ldnp">;
defm LDNPS : LoadPairNoAlloc<0b00, 1, FPR32Op, simm7s4, "ldnp">;
defm LDNPD : LoadPairNoAlloc<0b01, 1, FPR64Op, simm7s8, "ldnp">;
defm LDNPQ : LoadPairNoAlloc<0b10, 1, FPR128Op, simm7s16, "ldnp">;

//---
// (register offset)
//---

// Integer
defm LDRBB : Load8RO<0b00,  0, 0b01, GPR32, "ldrb", i32, zextloadi8>;
defm LDRHH : Load16RO<0b01, 0, 0b01, GPR32, "ldrh", i32, zextloadi16>;
defm LDRW  : Load32RO<0b10, 0, 0b01, GPR32, "ldr", i32, load>;
defm LDRX  : Load64RO<0b11, 0, 0b01, GPR64, "ldr", i64, load>;

// Floating-point
defm LDRB : Load8RO<0b00,   1, 0b01, FPR8Op,   "ldr", untyped, load>;
defm LDRH : Load16RO<0b01,  1, 0b01, FPR16Op,  "ldr", f16, load>;
defm LDRS : Load32RO<0b10,  1, 0b01, FPR32Op,  "ldr", f32, load>;
defm LDRD : Load64RO<0b11,  1, 0b01, FPR64Op,  "ldr", f64, load>;
defm LDRQ : Load128RO<0b00, 1, 0b11, FPR128Op, "ldr", f128, load>;

// Load sign-extended half-word
defm LDRSHW : Load16RO<0b01, 0, 0b11, GPR32, "ldrsh", i32, sextloadi16>;
defm LDRSHX : Load16RO<0b01, 0, 0b10, GPR64, "ldrsh", i64, sextloadi16>;

// Load sign-extended byte
defm LDRSBW : Load8RO<0b00, 0, 0b11, GPR32, "ldrsb", i32, sextloadi8>;
defm LDRSBX : Load8RO<0b00, 0, 0b10, GPR64, "ldrsb", i64, sextloadi8>;

// Load sign-extended word
defm LDRSW  : Load32RO<0b10, 0, 0b10, GPR64, "ldrsw", i64, sextloadi32>;

// Pre-fetch.
defm PRFM : PrefetchRO<0b11, 0, 0b10, "prfm">;

// For regular load, we do not have any alignment requirement.
// Thus, it is safe to directly map the vector loads with interesting
// addressing modes.
// FIXME: We could do the same for bitconvert to floating point vectors.
multiclass ScalToVecROLoadPat<ROAddrMode ro, SDPatternOperator loadop,
                              ValueType ScalTy, ValueType VecTy,
                              Instruction LOADW, Instruction LOADX,
                              SubRegIndex sub> {
  def : Pat<(VecTy (scalar_to_vector (ScalTy
              (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$offset))))),
            (INSERT_SUBREG (VecTy (IMPLICIT_DEF)),
                           (LOADW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$offset),
                           sub)>;

  def : Pat<(VecTy (scalar_to_vector (ScalTy
              (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$offset))))),
            (INSERT_SUBREG (VecTy (IMPLICIT_DEF)),
                           (LOADX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$offset),
                           sub)>;
}

let AddedComplexity = 10 in {
defm : ScalToVecROLoadPat<ro8,  extloadi8,  i32, v8i8,  LDRBroW, LDRBroX, bsub>;
defm : ScalToVecROLoadPat<ro8,  extloadi8,  i32, v16i8, LDRBroW, LDRBroX, bsub>;

defm : ScalToVecROLoadPat<ro16, extloadi16, i32, v4i16, LDRHroW, LDRHroX, hsub>;
defm : ScalToVecROLoadPat<ro16, extloadi16, i32, v8i16, LDRHroW, LDRHroX, hsub>;

defm : ScalToVecROLoadPat<ro16, load,       i32, v4f16, LDRHroW, LDRHroX, hsub>;
defm : ScalToVecROLoadPat<ro16, load,       i32, v8f16, LDRHroW, LDRHroX, hsub>;

defm : ScalToVecROLoadPat<ro32, load,       i32, v2i32, LDRSroW, LDRSroX, ssub>;
defm : ScalToVecROLoadPat<ro32, load,       i32, v4i32, LDRSroW, LDRSroX, ssub>;

defm : ScalToVecROLoadPat<ro32, load,       f32, v2f32, LDRSroW, LDRSroX, ssub>;
defm : ScalToVecROLoadPat<ro32, load,       f32, v4f32, LDRSroW, LDRSroX, ssub>;

defm : ScalToVecROLoadPat<ro64, load,       i64, v2i64, LDRDroW, LDRDroX, dsub>;

defm : ScalToVecROLoadPat<ro64, load,       f64, v2f64, LDRDroW, LDRDroX, dsub>;


def : Pat <(v1i64 (scalar_to_vector (i64
                      (load (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                           ro_Wextend64:$extend))))),
           (LDRDroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend)>;

def : Pat <(v1i64 (scalar_to_vector (i64
                      (load (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                           ro_Xextend64:$extend))))),
           (LDRDroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend)>;
}

// Match all load 64 bits width whose type is compatible with FPR64
multiclass VecROLoadPat<ROAddrMode ro, ValueType VecTy,
                        Instruction LOADW, Instruction LOADX> {

  def : Pat<(VecTy (load (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (LOADW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(VecTy (load (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (LOADX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
let Predicates = [IsLE] in {
  // We must do vector loads with LD1 in big-endian.
  defm : VecROLoadPat<ro64, v2i32, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v2f32, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v8i8,  LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v4i16, LDRDroW, LDRDroX>;
  defm : VecROLoadPat<ro64, v4f16, LDRDroW, LDRDroX>;
}

defm : VecROLoadPat<ro64, v1i64,  LDRDroW, LDRDroX>;
defm : VecROLoadPat<ro64, v1f64,  LDRDroW, LDRDroX>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  // We must do vector loads with LD1 in big-endian.
  defm : VecROLoadPat<ro128, v2i64,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v2f64,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v4i32,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v4f32,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v8i16,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v8f16,  LDRQroW, LDRQroX>;
  defm : VecROLoadPat<ro128, v16i8,  LDRQroW, LDRQroX>;
}
} // AddedComplexity = 10

// zextload -> i64
multiclass ExtLoadTo64ROPat<ROAddrMode ro, SDPatternOperator loadop,
                            Instruction INSTW, Instruction INSTX> {
  def : Pat<(i64 (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (SUBREG_TO_REG (i64 0),
                           (INSTW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend),
                           sub_32)>;

  def : Pat<(i64 (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (SUBREG_TO_REG (i64 0),
                           (INSTX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend),
                           sub_32)>;
}

let AddedComplexity = 10 in {
  defm : ExtLoadTo64ROPat<ro8,  zextloadi8,  LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo64ROPat<ro16, zextloadi16, LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo64ROPat<ro32, zextloadi32, LDRWroW,  LDRWroX>;

  // zextloadi1 -> zextloadi8
  defm : ExtLoadTo64ROPat<ro8,  zextloadi1,  LDRBBroW, LDRBBroX>;

  // extload -> zextload
  defm : ExtLoadTo64ROPat<ro8,  extloadi8,   LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo64ROPat<ro16, extloadi16,  LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo64ROPat<ro32, extloadi32,  LDRWroW,  LDRWroX>;

  // extloadi1 -> zextloadi8
  defm : ExtLoadTo64ROPat<ro8,  extloadi1,   LDRBBroW, LDRBBroX>;
}


// zextload -> i64
multiclass ExtLoadTo32ROPat<ROAddrMode ro, SDPatternOperator loadop,
                            Instruction INSTW, Instruction INSTX> {
  def : Pat<(i32 (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend))),
            (INSTW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(i32 (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend))),
            (INSTX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;

}

let AddedComplexity = 10 in {
  // extload -> zextload
  defm : ExtLoadTo32ROPat<ro8,  extloadi8,   LDRBBroW, LDRBBroX>;
  defm : ExtLoadTo32ROPat<ro16, extloadi16,  LDRHHroW, LDRHHroX>;
  defm : ExtLoadTo32ROPat<ro32, extloadi32,  LDRWroW,  LDRWroX>;

  // zextloadi1 -> zextloadi8
  defm : ExtLoadTo32ROPat<ro8, zextloadi1, LDRBBroW, LDRBBroX>;
}

//---
// (unsigned immediate)
//---
defm LDRX : LoadUI<0b11, 0, 0b01, GPR64z, uimm12s8, "ldr",
                   [(set GPR64z:$Rt,
                         (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)))]>;
defm LDRW : LoadUI<0b10, 0, 0b01, GPR32z, uimm12s4, "ldr",
                   [(set GPR32z:$Rt,
                         (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)))]>;
defm LDRB : LoadUI<0b00, 1, 0b01, FPR8Op, uimm12s1, "ldr",
                   [(set FPR8Op:$Rt,
                         (load (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset)))]>;
defm LDRH : LoadUI<0b01, 1, 0b01, FPR16Op, uimm12s2, "ldr",
                   [(set (f16 FPR16Op:$Rt),
                         (load (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset)))]>;
defm LDRS : LoadUI<0b10, 1, 0b01, FPR32Op, uimm12s4, "ldr",
                   [(set (f32 FPR32Op:$Rt),
                         (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)))]>;
defm LDRD : LoadUI<0b11, 1, 0b01, FPR64Op, uimm12s8, "ldr",
                   [(set (f64 FPR64Op:$Rt),
                         (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)))]>;
defm LDRQ : LoadUI<0b00, 1, 0b11, FPR128Op, uimm12s16, "ldr",
                 [(set (f128 FPR128Op:$Rt),
                       (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)))]>;

// For regular load, we do not have any alignment requirement.
// Thus, it is safe to directly map the vector loads with interesting
// addressing modes.
// FIXME: We could do the same for bitconvert to floating point vectors.
def : Pat <(v8i8 (scalar_to_vector (i32
               (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub)>;
def : Pat <(v16i8 (scalar_to_vector (i32
               (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub)>;
def : Pat <(v4i16 (scalar_to_vector (i32
               (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub)>;
def : Pat <(v8i16 (scalar_to_vector (i32
               (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub)>;
def : Pat <(v2i32 (scalar_to_vector (i32
               (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub)>;
def : Pat <(v4i32 (scalar_to_vector (i32
               (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub)>;
def : Pat <(v1i64 (scalar_to_vector (i64
               (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))))),
           (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat <(v2i64 (scalar_to_vector (i64
               (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))))),
           (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                          (LDRDui GPR64sp:$Rn, uimm12s8:$offset), dsub)>;

// Match all load 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  // We must use LD1 to perform vector loads in big-endian.
  def : Pat<(v2f32 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v8i8 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v4i16 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v2i32 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(v4f16 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
            (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
}
def : Pat<(v1f64 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
          (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(v1i64 (load (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))),
          (LDRDui GPR64sp:$Rn, uimm12s8:$offset)>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  // We must use LD1 to perform vector loads in big-endian.
  def : Pat<(v4f32 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v2f64 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v16i8 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v8i16 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v4i32 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v2i64 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(v8f16 (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
            (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;
}
def : Pat<(f128  (load (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset))),
          (LDRQui GPR64sp:$Rn, uimm12s16:$offset)>;

defm LDRHH : LoadUI<0b01, 0, 0b01, GPR32, uimm12s2, "ldrh",
                    [(set GPR32:$Rt,
                          (zextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                     uimm12s2:$offset)))]>;
defm LDRBB : LoadUI<0b00, 0, 0b01, GPR32, uimm12s1, "ldrb",
                    [(set GPR32:$Rt,
                          (zextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                   uimm12s1:$offset)))]>;
// zextload -> i64
def : Pat<(i64 (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRHHui GPR64sp:$Rn, uimm12s2:$offset), sub_32)>;

// zextloadi1 -> zextloadi8
def : Pat<(i32 (zextloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i64 (zextloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;

// extload -> zextload
def : Pat<(i32 (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
          (LDRHHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(i32 (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i32 (extloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(i64 (extloadi32 (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRWui GPR64sp:$Rn, uimm12s4:$offset), sub_32)>;
def : Pat<(i64 (extloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRHHui GPR64sp:$Rn, uimm12s2:$offset), sub_32)>;
def : Pat<(i64 (extloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;
def : Pat<(i64 (extloadi1 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))),
    (SUBREG_TO_REG (i64 0), (LDRBBui GPR64sp:$Rn, uimm12s1:$offset), sub_32)>;

// load sign-extended half-word
defm LDRSHW : LoadUI<0b01, 0, 0b11, GPR32, uimm12s2, "ldrsh",
                     [(set GPR32:$Rt,
                           (sextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                      uimm12s2:$offset)))]>;
defm LDRSHX : LoadUI<0b01, 0, 0b10, GPR64, uimm12s2, "ldrsh",
                     [(set GPR64:$Rt,
                           (sextloadi16 (am_indexed16 GPR64sp:$Rn,
                                                      uimm12s2:$offset)))]>;

// load sign-extended byte
defm LDRSBW : LoadUI<0b00, 0, 0b11, GPR32, uimm12s1, "ldrsb",
                     [(set GPR32:$Rt,
                           (sextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                    uimm12s1:$offset)))]>;
defm LDRSBX : LoadUI<0b00, 0, 0b10, GPR64, uimm12s1, "ldrsb",
                     [(set GPR64:$Rt,
                           (sextloadi8 (am_indexed8 GPR64sp:$Rn,
                                                    uimm12s1:$offset)))]>;

// load sign-extended word
defm LDRSW  : LoadUI<0b10, 0, 0b10, GPR64, uimm12s4, "ldrsw",
                     [(set GPR64:$Rt,
                           (sextloadi32 (am_indexed32 GPR64sp:$Rn,
                                                      uimm12s4:$offset)))]>;

// load zero-extended word
def : Pat<(i64 (zextloadi32 (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))),
      (SUBREG_TO_REG (i64 0), (LDRWui GPR64sp:$Rn, uimm12s4:$offset), sub_32)>;

// Pre-fetch.
def PRFMui : PrefetchUI<0b11, 0, 0b10, "prfm",
                        [(AArch64Prefetch imm:$Rt,
                                        (am_indexed64 GPR64sp:$Rn,
                                                      uimm12s8:$offset))]>;

def : InstAlias<"prfm $Rt, [$Rn]", (PRFMui prfop:$Rt, GPR64sp:$Rn, 0)>;

//---
// (literal)

def alignedglobal : PatLeaf<(iPTR iPTR:$label), [{
  if (auto *G = dyn_cast<GlobalAddressSDNode>(N)) {
    const DataLayout &DL = MF->getDataLayout();
    unsigned Align = G->getGlobal()->getPointerAlignment(DL);
    return Align >= 4 && G->getOffset() % 4 == 0;
  }
  if (auto *C = dyn_cast<ConstantPoolSDNode>(N))
    return C->getAlignment() >= 4 && C->getOffset() % 4 == 0;
  return false;
}]>;

def LDRWl : LoadLiteral<0b00, 0, GPR32z, "ldr",
  [(set GPR32z:$Rt, (load (AArch64adr alignedglobal:$label)))]>;
def LDRXl : LoadLiteral<0b01, 0, GPR64z, "ldr",
  [(set GPR64z:$Rt, (load (AArch64adr alignedglobal:$label)))]>;
def LDRSl : LoadLiteral<0b00, 1, FPR32Op, "ldr",
  [(set (f32 FPR32Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;
def LDRDl : LoadLiteral<0b01, 1, FPR64Op, "ldr",
  [(set (f64 FPR64Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;
def LDRQl : LoadLiteral<0b10, 1, FPR128Op, "ldr",
  [(set (f128 FPR128Op:$Rt), (load (AArch64adr alignedglobal:$label)))]>;

// load sign-extended word
def LDRSWl : LoadLiteral<0b10, 0, GPR64z, "ldrsw",
  [(set GPR64z:$Rt, (sextloadi32 (AArch64adr alignedglobal:$label)))]>;

let AddedComplexity = 20 in {
def : Pat<(i64 (zextloadi32 (AArch64adr alignedglobal:$label))),
        (SUBREG_TO_REG (i64 0), (LDRWl $label), sub_32)>;
}

// prefetch
def PRFMl : PrefetchLiteral<0b11, 0, "prfm", []>;
//                   [(AArch64Prefetch imm:$Rt, tglobaladdr:$label)]>;

//---
// (unscaled immediate)
defm LDURX : LoadUnscaled<0b11, 0, 0b01, GPR64z, "ldur",
                    [(set GPR64z:$Rt,
                          (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURW : LoadUnscaled<0b10, 0, 0b01, GPR32z, "ldur",
                    [(set GPR32z:$Rt,
                          (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURB : LoadUnscaled<0b00, 1, 0b01, FPR8Op, "ldur",
                    [(set FPR8Op:$Rt,
                          (load (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURH : LoadUnscaled<0b01, 1, 0b01, FPR16Op, "ldur",
                    [(set FPR16Op:$Rt,
                          (load (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURS : LoadUnscaled<0b10, 1, 0b01, FPR32Op, "ldur",
                    [(set (f32 FPR32Op:$Rt),
                          (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURD : LoadUnscaled<0b11, 1, 0b01, FPR64Op, "ldur",
                    [(set (f64 FPR64Op:$Rt),
                          (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURQ : LoadUnscaled<0b00, 1, 0b11, FPR128Op, "ldur",
                    [(set (f128 FPR128Op:$Rt),
                          (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset)))]>;

defm LDURHH
    : LoadUnscaled<0b01, 0, 0b01, GPR32, "ldurh",
             [(set GPR32:$Rt,
                    (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURBB
    : LoadUnscaled<0b00, 0, 0b01, GPR32, "ldurb",
             [(set GPR32:$Rt,
                    (zextloadi8 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;

// Match all load 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  def : Pat<(v2f32 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v2i32 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4i16 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8i8 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4f16 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
            (LDURDi GPR64sp:$Rn, simm9:$offset)>;
}
def : Pat<(v1f64 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
          (LDURDi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(v1i64 (load (am_unscaled64 GPR64sp:$Rn, simm9:$offset))),
          (LDURDi GPR64sp:$Rn, simm9:$offset)>;

// Match all load 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE] in {
  def : Pat<(v2f64 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v2i64 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4f32 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v4i32 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8i16 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v16i8 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(v8f16 (load (am_unscaled128 GPR64sp:$Rn, simm9:$offset))),
            (LDURQi GPR64sp:$Rn, simm9:$offset)>;
}

//  anyext -> zext
def : Pat<(i32 (extloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
          (LDURHHi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (extloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (extloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i64 (extloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURWi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (extloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
// unscaled zext
def : Pat<(i32 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
          (LDURHHi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i32 (zextloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(i64 (zextloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURWi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi1 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
    (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;


//---
// LDR mnemonics fall back to LDUR for negative or unaligned offsets.

// Define new assembler match classes as we want to only match these when
// the don't otherwise match the scaled addressing mode for LDR/STR. Don't
// associate a DiagnosticType either, as we want the diagnostic for the
// canonical form (the scaled operand) to take precedence.
class SImm9OffsetOperand<int Width> : AsmOperandClass {
  let Name = "SImm9OffsetFB" # Width;
  let PredicateMethod = "isSImm9OffsetFB<" # Width # ">";
  let RenderMethod = "addImmOperands";
}

def SImm9OffsetFB8Operand : SImm9OffsetOperand<8>;
def SImm9OffsetFB16Operand : SImm9OffsetOperand<16>;
def SImm9OffsetFB32Operand : SImm9OffsetOperand<32>;
def SImm9OffsetFB64Operand : SImm9OffsetOperand<64>;
def SImm9OffsetFB128Operand : SImm9OffsetOperand<128>;

def simm9_offset_fb8 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB8Operand;
}
def simm9_offset_fb16 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB16Operand;
}
def simm9_offset_fb32 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB32Operand;
}
def simm9_offset_fb64 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB64Operand;
}
def simm9_offset_fb128 : Operand<i64> {
  let ParserMatchClass = SImm9OffsetFB128Operand;
}

def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURBi FPR8Op:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURHi FPR16Op:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURSi FPR32Op:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
                (LDURDi FPR64Op:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"ldr $Rt, [$Rn, $offset]",
               (LDURQi FPR128Op:$Rt, GPR64sp:$Rn, simm9_offset_fb128:$offset), 0>;

// zextload -> i64
def : Pat<(i64 (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))),
  (SUBREG_TO_REG (i64 0), (LDURBBi GPR64sp:$Rn, simm9:$offset), sub_32)>;
def : Pat<(i64 (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))),
  (SUBREG_TO_REG (i64 0), (LDURHHi GPR64sp:$Rn, simm9:$offset), sub_32)>;

// load sign-extended half-word
defm LDURSHW
    : LoadUnscaled<0b01, 0, 0b11, GPR32, "ldursh",
               [(set GPR32:$Rt,
                    (sextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURSHX
    : LoadUnscaled<0b01, 0, 0b10, GPR64, "ldursh",
              [(set GPR64:$Rt,
                    (sextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset)))]>;

// load sign-extended byte
defm LDURSBW
    : LoadUnscaled<0b00, 0, 0b11, GPR32, "ldursb",
                [(set GPR32:$Rt,
                      (sextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;
defm LDURSBX
    : LoadUnscaled<0b00, 0, 0b10, GPR64, "ldursb",
                [(set GPR64:$Rt,
                      (sextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset)))]>;

// load sign-extended word
defm LDURSW
    : LoadUnscaled<0b10, 0, 0b10, GPR64, "ldursw",
              [(set GPR64:$Rt,
                    (sextloadi32 (am_unscaled32 GPR64sp:$Rn, simm9:$offset)))]>;

// zero and sign extending aliases from generic LDR* mnemonics to LDUR*.
def : InstAlias<"ldrb $Rt, [$Rn, $offset]",
                (LDURBBi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrh $Rt, [$Rn, $offset]",
                (LDURHHi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsb $Rt, [$Rn, $offset]",
                (LDURSBWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrsb $Rt, [$Rn, $offset]",
                (LDURSBXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"ldrsh $Rt, [$Rn, $offset]",
                (LDURSHWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsh $Rt, [$Rn, $offset]",
                (LDURSHXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"ldrsw $Rt, [$Rn, $offset]",
                (LDURSWi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;

// Pre-fetch.
defm PRFUM : PrefetchUnscaled<0b11, 0, 0b10, "prfum",
                  [(AArch64Prefetch imm:$Rt,
                                  (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;

//---
// (unscaled immediate, unprivileged)
defm LDTRX : LoadUnprivileged<0b11, 0, 0b01, GPR64, "ldtr">;
defm LDTRW : LoadUnprivileged<0b10, 0, 0b01, GPR32, "ldtr">;

defm LDTRH : LoadUnprivileged<0b01, 0, 0b01, GPR32, "ldtrh">;
defm LDTRB : LoadUnprivileged<0b00, 0, 0b01, GPR32, "ldtrb">;

// load sign-extended half-word
defm LDTRSHW : LoadUnprivileged<0b01, 0, 0b11, GPR32, "ldtrsh">;
defm LDTRSHX : LoadUnprivileged<0b01, 0, 0b10, GPR64, "ldtrsh">;

// load sign-extended byte
defm LDTRSBW : LoadUnprivileged<0b00, 0, 0b11, GPR32, "ldtrsb">;
defm LDTRSBX : LoadUnprivileged<0b00, 0, 0b10, GPR64, "ldtrsb">;

// load sign-extended word
defm LDTRSW  : LoadUnprivileged<0b10, 0, 0b10, GPR64, "ldtrsw">;

//---
// (immediate pre-indexed)
def LDRWpre : LoadPreIdx<0b10, 0, 0b01, GPR32z, "ldr">;
def LDRXpre : LoadPreIdx<0b11, 0, 0b01, GPR64z, "ldr">;
def LDRBpre : LoadPreIdx<0b00, 1, 0b01, FPR8Op,  "ldr">;
def LDRHpre : LoadPreIdx<0b01, 1, 0b01, FPR16Op, "ldr">;
def LDRSpre : LoadPreIdx<0b10, 1, 0b01, FPR32Op, "ldr">;
def LDRDpre : LoadPreIdx<0b11, 1, 0b01, FPR64Op, "ldr">;
def LDRQpre : LoadPreIdx<0b00, 1, 0b11, FPR128Op, "ldr">;

// load sign-extended half-word
def LDRSHWpre : LoadPreIdx<0b01, 0, 0b11, GPR32z, "ldrsh">;
def LDRSHXpre : LoadPreIdx<0b01, 0, 0b10, GPR64z, "ldrsh">;

// load sign-extended byte
def LDRSBWpre : LoadPreIdx<0b00, 0, 0b11, GPR32z, "ldrsb">;
def LDRSBXpre : LoadPreIdx<0b00, 0, 0b10, GPR64z, "ldrsb">;

// load zero-extended byte
def LDRBBpre : LoadPreIdx<0b00, 0, 0b01, GPR32z, "ldrb">;
def LDRHHpre : LoadPreIdx<0b01, 0, 0b01, GPR32z, "ldrh">;

// load sign-extended word
def LDRSWpre : LoadPreIdx<0b10, 0, 0b10, GPR64z, "ldrsw">;

//---
// (immediate post-indexed)
def LDRWpost : LoadPostIdx<0b10, 0, 0b01, GPR32z, "ldr">;
def LDRXpost : LoadPostIdx<0b11, 0, 0b01, GPR64z, "ldr">;
def LDRBpost : LoadPostIdx<0b00, 1, 0b01, FPR8Op,  "ldr">;
def LDRHpost : LoadPostIdx<0b01, 1, 0b01, FPR16Op, "ldr">;
def LDRSpost : LoadPostIdx<0b10, 1, 0b01, FPR32Op, "ldr">;
def LDRDpost : LoadPostIdx<0b11, 1, 0b01, FPR64Op, "ldr">;
def LDRQpost : LoadPostIdx<0b00, 1, 0b11, FPR128Op, "ldr">;

// load sign-extended half-word
def LDRSHWpost : LoadPostIdx<0b01, 0, 0b11, GPR32z, "ldrsh">;
def LDRSHXpost : LoadPostIdx<0b01, 0, 0b10, GPR64z, "ldrsh">;

// load sign-extended byte
def LDRSBWpost : LoadPostIdx<0b00, 0, 0b11, GPR32z, "ldrsb">;
def LDRSBXpost : LoadPostIdx<0b00, 0, 0b10, GPR64z, "ldrsb">;

// load zero-extended byte
def LDRBBpost : LoadPostIdx<0b00, 0, 0b01, GPR32z, "ldrb">;
def LDRHHpost : LoadPostIdx<0b01, 0, 0b01, GPR32z, "ldrh">;

// load sign-extended word
def LDRSWpost : LoadPostIdx<0b10, 0, 0b10, GPR64z, "ldrsw">;

//===----------------------------------------------------------------------===//
// Store instructions.
//===----------------------------------------------------------------------===//

// Pair (indexed, offset)
// FIXME: Use dedicated range-checked addressing mode operand here.
defm STPW : StorePairOffset<0b00, 0, GPR32z, simm7s4, "stp">;
defm STPX : StorePairOffset<0b10, 0, GPR64z, simm7s8, "stp">;
defm STPS : StorePairOffset<0b00, 1, FPR32Op, simm7s4, "stp">;
defm STPD : StorePairOffset<0b01, 1, FPR64Op, simm7s8, "stp">;
defm STPQ : StorePairOffset<0b10, 1, FPR128Op, simm7s16, "stp">;

// Pair (pre-indexed)
def STPWpre : StorePairPreIdx<0b00, 0, GPR32z, simm7s4, "stp">;
def STPXpre : StorePairPreIdx<0b10, 0, GPR64z, simm7s8, "stp">;
def STPSpre : StorePairPreIdx<0b00, 1, FPR32Op, simm7s4, "stp">;
def STPDpre : StorePairPreIdx<0b01, 1, FPR64Op, simm7s8, "stp">;
def STPQpre : StorePairPreIdx<0b10, 1, FPR128Op, simm7s16, "stp">;

// Pair (pre-indexed)
def STPWpost : StorePairPostIdx<0b00, 0, GPR32z, simm7s4, "stp">;
def STPXpost : StorePairPostIdx<0b10, 0, GPR64z, simm7s8, "stp">;
def STPSpost : StorePairPostIdx<0b00, 1, FPR32Op, simm7s4, "stp">;
def STPDpost : StorePairPostIdx<0b01, 1, FPR64Op, simm7s8, "stp">;
def STPQpost : StorePairPostIdx<0b10, 1, FPR128Op, simm7s16, "stp">;

// Pair (no allocate)
defm STNPW : StorePairNoAlloc<0b00, 0, GPR32z, simm7s4, "stnp">;
defm STNPX : StorePairNoAlloc<0b10, 0, GPR64z, simm7s8, "stnp">;
defm STNPS : StorePairNoAlloc<0b00, 1, FPR32Op, simm7s4, "stnp">;
defm STNPD : StorePairNoAlloc<0b01, 1, FPR64Op, simm7s8, "stnp">;
defm STNPQ : StorePairNoAlloc<0b10, 1, FPR128Op, simm7s16, "stnp">;

//---
// (Register offset)

// Integer
defm STRBB : Store8RO< 0b00, 0, 0b00, GPR32, "strb", i32, truncstorei8>;
defm STRHH : Store16RO<0b01, 0, 0b00, GPR32, "strh", i32, truncstorei16>;
defm STRW  : Store32RO<0b10, 0, 0b00, GPR32, "str",  i32, store>;
defm STRX  : Store64RO<0b11, 0, 0b00, GPR64, "str",  i64, store>;


// Floating-point
defm STRB : Store8RO< 0b00,  1, 0b00, FPR8Op,   "str", untyped, store>;
defm STRH : Store16RO<0b01,  1, 0b00, FPR16Op,  "str", f16,     store>;
defm STRS : Store32RO<0b10,  1, 0b00, FPR32Op,  "str", f32,     store>;
defm STRD : Store64RO<0b11,  1, 0b00, FPR64Op,  "str", f64,     store>;
defm STRQ : Store128RO<0b00, 1, 0b10, FPR128Op, "str", f128,    store>;

let Predicates = [UseSTRQro], AddedComplexity = 10 in {
  def : Pat<(store (f128 FPR128:$Rt),
                        (ro_Windexed128 GPR64sp:$Rn, GPR32:$Rm,
                                        ro_Wextend128:$extend)),
            (STRQroW FPR128:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend128:$extend)>;
  def : Pat<(store (f128 FPR128:$Rt),
                        (ro_Xindexed128 GPR64sp:$Rn, GPR64:$Rm,
                                        ro_Xextend128:$extend)),
            (STRQroX FPR128:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro_Wextend128:$extend)>;
}

multiclass TruncStoreFrom64ROPat<ROAddrMode ro, SDPatternOperator storeop,
                                 Instruction STRW, Instruction STRX> {

  def : Pat<(storeop GPR64:$Rt,
                     (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(storeop GPR64:$Rt,
                     (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
  // truncstore i64
  defm : TruncStoreFrom64ROPat<ro8,  truncstorei8,  STRBBroW, STRBBroX>;
  defm : TruncStoreFrom64ROPat<ro16, truncstorei16, STRHHroW, STRHHroX>;
  defm : TruncStoreFrom64ROPat<ro32, truncstorei32, STRWroW,  STRWroX>;
}

multiclass VecROStorePat<ROAddrMode ro, ValueType VecTy, RegisterClass FPR,
                         Instruction STRW, Instruction STRX> {
  def : Pat<(store (VecTy FPR:$Rt),
                   (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW FPR:$Rt, GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(store (VecTy FPR:$Rt),
                   (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX FPR:$Rt, GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 10 in {
// Match all store 64 bits width whose type is compatible with FPR64
let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  defm : VecROStorePat<ro64, v2i32, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v2f32, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v4i16, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v8i8, FPR64, STRDroW, STRDroX>;
  defm : VecROStorePat<ro64, v4f16, FPR64, STRDroW, STRDroX>;
}

defm : VecROStorePat<ro64, v1i64, FPR64, STRDroW, STRDroX>;
defm : VecROStorePat<ro64, v1f64, FPR64, STRDroW, STRDroX>;

// Match all store 128 bits width whose type is compatible with FPR128
let Predicates = [IsLE, UseSTRQro] in {
  // We must use ST1 to store vectors in big-endian.
  defm : VecROStorePat<ro128, v2i64, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v2f64, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v4i32, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v4f32, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v8i16, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v16i8, FPR128, STRQroW, STRQroX>;
  defm : VecROStorePat<ro128, v8f16, FPR128, STRQroW, STRQroX>;
}
} // AddedComplexity = 10

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecROStoreLane0Pat<ROAddrMode ro, SDPatternOperator storeop,
                              ValueType VecTy, ValueType STy,
                              SubRegIndex SubRegIdx,
                              Instruction STRW, Instruction STRX> {

  def : Pat<(storeop (STy (vector_extract (VecTy VecListOne128:$Vt), 0)),
                     (ro.Wpat GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)),
            (STRW (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx),
                  GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend)>;

  def : Pat<(storeop (STy (vector_extract (VecTy VecListOne128:$Vt), 0)),
                     (ro.Xpat GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)),
            (STRX (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx),
                  GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend)>;
}

let AddedComplexity = 19 in {
  defm : VecROStoreLane0Pat<ro16, truncstorei16, v8i16, i32, hsub, STRHroW, STRHroX>;
  defm : VecROStoreLane0Pat<ro16,         store, v8f16, f16, hsub, STRHroW, STRHroX>;
  defm : VecROStoreLane0Pat<ro32,         store, v4i32, i32, ssub, STRSroW, STRSroX>;
  defm : VecROStoreLane0Pat<ro32,         store, v4f32, f32, ssub, STRSroW, STRSroX>;
  defm : VecROStoreLane0Pat<ro64,         store, v2i64, i64, dsub, STRDroW, STRDroX>;
  defm : VecROStoreLane0Pat<ro64,         store, v2f64, f64, dsub, STRDroW, STRDroX>;
}

//---
// (unsigned immediate)
defm STRX : StoreUIz<0b11, 0, 0b00, GPR64z, uimm12s8, "str",
                   [(store GPR64z:$Rt,
                            (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))]>;
defm STRW : StoreUIz<0b10, 0, 0b00, GPR32z, uimm12s4, "str",
                    [(store GPR32z:$Rt,
                            (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))]>;
defm STRB : StoreUI<0b00, 1, 0b00, FPR8Op, uimm12s1, "str",
                    [(store FPR8Op:$Rt,
                            (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))]>;
defm STRH : StoreUI<0b01, 1, 0b00, FPR16Op, uimm12s2, "str",
                    [(store (f16 FPR16Op:$Rt),
                            (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))]>;
defm STRS : StoreUI<0b10, 1, 0b00, FPR32Op, uimm12s4, "str",
                    [(store (f32 FPR32Op:$Rt),
                            (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))]>;
defm STRD : StoreUI<0b11, 1, 0b00, FPR64Op, uimm12s8, "str",
                    [(store (f64 FPR64Op:$Rt),
                            (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset))]>;
defm STRQ : StoreUI<0b00, 1, 0b10, FPR128Op, uimm12s16, "str", []>;

defm STRHH : StoreUIz<0b01, 0, 0b00, GPR32z, uimm12s2, "strh",
                     [(truncstorei16 GPR32z:$Rt,
                                     (am_indexed16 GPR64sp:$Rn,
                                                   uimm12s2:$offset))]>;
defm STRBB : StoreUIz<0b00, 0, 0b00, GPR32z, uimm12s1,  "strb",
                     [(truncstorei8 GPR32z:$Rt,
                                    (am_indexed8 GPR64sp:$Rn,
                                                 uimm12s1:$offset))]>;

let AddedComplexity = 10 in {

// Match all store 64 bits width whose type is compatible with FPR64
def : Pat<(store (v1i64 FPR64:$Rt),
                 (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
          (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(store (v1f64 FPR64:$Rt),
                 (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
          (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v2f32 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v8i8 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v4i16 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v2i32 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
  def : Pat<(store (v4f16 FPR64:$Rt),
                   (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset)),
            (STRDui FPR64:$Rt, GPR64sp:$Rn, uimm12s8:$offset)>;
}

// Match all store 128 bits width whose type is compatible with FPR128
def : Pat<(store (f128  FPR128:$Rt),
                 (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
          (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v4f32 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v16i8 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v8i16 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v4i32 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v2i64 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
  def : Pat<(store (v8f16 FPR128:$Rt),
                   (am_indexed128 GPR64sp:$Rn, uimm12s16:$offset)),
            (STRQui FPR128:$Rt, GPR64sp:$Rn, uimm12s16:$offset)>;
}

// truncstore i64
def : Pat<(truncstorei32 GPR64:$Rt,
                         (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset)),
  (STRWui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s4:$offset)>;
def : Pat<(truncstorei16 GPR64:$Rt,
                         (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset)),
  (STRHHui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(truncstorei8 GPR64:$Rt, (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset)),
  (STRBBui (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, uimm12s1:$offset)>;

} // AddedComplexity = 10

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecStoreLane0Pat<Operand UIAddrMode, SDPatternOperator storeop,
                            ValueType VTy, ValueType STy,
                            SubRegIndex SubRegIdx, Operand IndexType,
                            Instruction STR> {
  def : Pat<(storeop (STy (vector_extract (VTy VecListOne128:$Vt), 0)),
                     (UIAddrMode GPR64sp:$Rn, IndexType:$offset)),
            (STR (EXTRACT_SUBREG VecListOne128:$Vt, SubRegIdx),
                 GPR64sp:$Rn, IndexType:$offset)>;
}

let AddedComplexity = 19 in {
  defm : VecStoreLane0Pat<am_indexed16, truncstorei16, v8i16, i32, hsub, uimm12s2, STRHui>;
  defm : VecStoreLane0Pat<am_indexed16,         store, v8f16, f16, hsub, uimm12s2, STRHui>;
  defm : VecStoreLane0Pat<am_indexed32,         store, v4i32, i32, ssub, uimm12s4, STRSui>;
  defm : VecStoreLane0Pat<am_indexed32,         store, v4f32, f32, ssub, uimm12s4, STRSui>;
  defm : VecStoreLane0Pat<am_indexed64,         store, v2i64, i64, dsub, uimm12s8, STRDui>;
  defm : VecStoreLane0Pat<am_indexed64,         store, v2f64, f64, dsub, uimm12s8, STRDui>;
}

//---
// (unscaled immediate)
defm STURX : StoreUnscaled<0b11, 0, 0b00, GPR64z, "stur",
                         [(store GPR64z:$Rt,
                                 (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;
defm STURW : StoreUnscaled<0b10, 0, 0b00, GPR32z, "stur",
                         [(store GPR32z:$Rt,
                                 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))]>;
defm STURB : StoreUnscaled<0b00, 1, 0b00, FPR8Op, "stur",
                         [(store FPR8Op:$Rt,
                                 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))]>;
defm STURH : StoreUnscaled<0b01, 1, 0b00, FPR16Op, "stur",
                         [(store (f16 FPR16Op:$Rt),
                                 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))]>;
defm STURS : StoreUnscaled<0b10, 1, 0b00, FPR32Op, "stur",
                         [(store (f32 FPR32Op:$Rt),
                                 (am_unscaled32 GPR64sp:$Rn, simm9:$offset))]>;
defm STURD : StoreUnscaled<0b11, 1, 0b00, FPR64Op, "stur",
                         [(store (f64 FPR64Op:$Rt),
                                 (am_unscaled64 GPR64sp:$Rn, simm9:$offset))]>;
defm STURQ : StoreUnscaled<0b00, 1, 0b10, FPR128Op, "stur",
                         [(store (f128 FPR128Op:$Rt),
                                 (am_unscaled128 GPR64sp:$Rn, simm9:$offset))]>;
defm STURHH : StoreUnscaled<0b01, 0, 0b00, GPR32z, "sturh",
                         [(truncstorei16 GPR32z:$Rt,
                                 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))]>;
defm STURBB : StoreUnscaled<0b00, 0, 0b00, GPR32z, "sturb",
                         [(truncstorei8 GPR32z:$Rt,
                                  (am_unscaled8 GPR64sp:$Rn, simm9:$offset))]>;

// Armv8.4 Weaker Release Consistency enhancements
//         LDAPR & STLR with Immediate Offset instructions
let Predicates = [HasRCPC_IMMO] in {
defm STLURB     : BaseStoreUnscaleV84<"stlurb",  0b00, 0b00, GPR32>;
defm STLURH     : BaseStoreUnscaleV84<"stlurh",  0b01, 0b00, GPR32>;
defm STLURW     : BaseStoreUnscaleV84<"stlur",   0b10, 0b00, GPR32>;
defm STLURX     : BaseStoreUnscaleV84<"stlur",   0b11, 0b00, GPR64>;
defm LDAPURB    : BaseLoadUnscaleV84<"ldapurb",  0b00, 0b01, GPR32>;
defm LDAPURSBW  : BaseLoadUnscaleV84<"ldapursb", 0b00, 0b11, GPR32>;
defm LDAPURSBX  : BaseLoadUnscaleV84<"ldapursb", 0b00, 0b10, GPR64>;
defm LDAPURH    : BaseLoadUnscaleV84<"ldapurh",  0b01, 0b01, GPR32>;
defm LDAPURSHW  : BaseLoadUnscaleV84<"ldapursh", 0b01, 0b11, GPR32>;
defm LDAPURSHX  : BaseLoadUnscaleV84<"ldapursh", 0b01, 0b10, GPR64>;
defm LDAPUR     : BaseLoadUnscaleV84<"ldapur",   0b10, 0b01, GPR32>;
defm LDAPURSW   : BaseLoadUnscaleV84<"ldapursw", 0b10, 0b10, GPR64>;
defm LDAPURX    : BaseLoadUnscaleV84<"ldapur",   0b11, 0b01, GPR64>;
}

// Match all store 64 bits width whose type is compatible with FPR64
def : Pat<(store (v1f64 FPR64:$Rt), (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
          (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(store (v1i64 FPR64:$Rt), (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
          (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;

let AddedComplexity = 10 in {

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v2f32 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8i8 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4i16 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2i32 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4f16 FPR64:$Rt),
                   (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
            (STURDi FPR64:$Rt, GPR64sp:$Rn, simm9:$offset)>;
}

// Match all store 128 bits width whose type is compatible with FPR128
def : Pat<(store (f128 FPR128:$Rt), (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
          (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;

let Predicates = [IsLE] in {
  // We must use ST1 to store vectors in big-endian.
  def : Pat<(store (v4f32 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v16i8 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8i16 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v4i32 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2i64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v2f64 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
  def : Pat<(store (v8f16 FPR128:$Rt),
                   (am_unscaled128 GPR64sp:$Rn, simm9:$offset)),
            (STURQi FPR128:$Rt, GPR64sp:$Rn, simm9:$offset)>;
}

} // AddedComplexity = 10

// unscaled i64 truncating stores
def : Pat<(truncstorei32 GPR64:$Rt, (am_unscaled32 GPR64sp:$Rn, simm9:$offset)),
  (STURWi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(truncstorei16 GPR64:$Rt, (am_unscaled16 GPR64sp:$Rn, simm9:$offset)),
  (STURHHi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;
def : Pat<(truncstorei8 GPR64:$Rt, (am_unscaled8 GPR64sp:$Rn, simm9:$offset)),
  (STURBBi (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$Rn, simm9:$offset)>;

// Match stores from lane 0 to the appropriate subreg's store.
multiclass VecStoreULane0Pat<SDPatternOperator StoreOp,
                             ValueType VTy, ValueType STy,
                             SubRegIndex SubRegIdx, Instruction STR> {
  defm : VecStoreLane0Pat<am_unscaled128, StoreOp, VTy, STy, SubRegIdx, simm9, STR>;
}

let AddedComplexity = 19 in {
  defm : VecStoreULane0Pat<truncstorei16, v8i16, i32, hsub, STURHi>;
  defm : VecStoreULane0Pat<store,         v8f16, f16, hsub, STURHi>;
  defm : VecStoreULane0Pat<store,         v4i32, i32, ssub, STURSi>;
  defm : VecStoreULane0Pat<store,         v4f32, f32, ssub, STURSi>;
  defm : VecStoreULane0Pat<store,         v2i64, i64, dsub, STURDi>;
  defm : VecStoreULane0Pat<store,         v2f64, f64, dsub, STURDi>;
}

//---
// STR mnemonics fall back to STUR for negative or unaligned offsets.
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURXi GPR64:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURWi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURBi FPR8Op:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURHi FPR16Op:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURSi FPR32Op:$Rt, GPR64sp:$Rn, simm9_offset_fb32:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURDi FPR64Op:$Rt, GPR64sp:$Rn, simm9_offset_fb64:$offset), 0>;
def : InstAlias<"str $Rt, [$Rn, $offset]",
                (STURQi FPR128Op:$Rt, GPR64sp:$Rn, simm9_offset_fb128:$offset), 0>;

def : InstAlias<"strb $Rt, [$Rn, $offset]",
                (STURBBi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb8:$offset), 0>;
def : InstAlias<"strh $Rt, [$Rn, $offset]",
                (STURHHi GPR32:$Rt, GPR64sp:$Rn, simm9_offset_fb16:$offset), 0>;

//---
// (unscaled immediate, unprivileged)
defm STTRW : StoreUnprivileged<0b10, 0, 0b00, GPR32, "sttr">;
defm STTRX : StoreUnprivileged<0b11, 0, 0b00, GPR64, "sttr">;

defm STTRH : StoreUnprivileged<0b01, 0, 0b00, GPR32, "sttrh">;
defm STTRB : StoreUnprivileged<0b00, 0, 0b00, GPR32, "sttrb">;

//---
// (immediate pre-indexed)
def STRWpre : StorePreIdx<0b10, 0, 0b00, GPR32z, "str",  pre_store, i32>;
def STRXpre : StorePreIdx<0b11, 0, 0b00, GPR64z, "str",  pre_store, i64>;
def STRBpre : StorePreIdx<0b00, 1, 0b00, FPR8Op,  "str",  pre_store, untyped>;
def STRHpre : StorePreIdx<0b01, 1, 0b00, FPR16Op, "str",  pre_store, f16>;
def STRSpre : StorePreIdx<0b10, 1, 0b00, FPR32Op, "str",  pre_store, f32>;
def STRDpre : StorePreIdx<0b11, 1, 0b00, FPR64Op, "str",  pre_store, f64>;
def STRQpre : StorePreIdx<0b00, 1, 0b10, FPR128Op, "str", pre_store, f128>;

def STRBBpre : StorePreIdx<0b00, 0, 0b00, GPR32z, "strb", pre_truncsti8,  i32>;
def STRHHpre : StorePreIdx<0b01, 0, 0b00, GPR32z, "strh", pre_truncsti16, i32>;

// truncstore i64
def : Pat<(pre_truncsti32 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRWpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
           simm9:$off)>;
def : Pat<(pre_truncsti16 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRHHpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;
def : Pat<(pre_truncsti8 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRBBpre (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;

def : Pat<(pre_store (v8i8 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4i16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2i32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2f32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v1i64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v1f64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4f16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpre FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;

def : Pat<(pre_store (v16i8 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v8i16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4i32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v4f32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2i64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v2f64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(pre_store (v8f16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpre FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;

//---
// (immediate post-indexed)
def STRWpost : StorePostIdx<0b10, 0, 0b00, GPR32z,  "str", post_store, i32>;
def STRXpost : StorePostIdx<0b11, 0, 0b00, GPR64z,  "str", post_store, i64>;
def STRBpost : StorePostIdx<0b00, 1, 0b00, FPR8Op,   "str", post_store, untyped>;
def STRHpost : StorePostIdx<0b01, 1, 0b00, FPR16Op,  "str", post_store, f16>;
def STRSpost : StorePostIdx<0b10, 1, 0b00, FPR32Op,  "str", post_store, f32>;
def STRDpost : StorePostIdx<0b11, 1, 0b00, FPR64Op,  "str", post_store, f64>;
def STRQpost : StorePostIdx<0b00, 1, 0b10, FPR128Op, "str", post_store, f128>;

def STRBBpost : StorePostIdx<0b00, 0, 0b00, GPR32z, "strb", post_truncsti8, i32>;
def STRHHpost : StorePostIdx<0b01, 0, 0b00, GPR32z, "strh", post_truncsti16, i32>;

// truncstore i64
def : Pat<(post_truncsti32 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRWpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
            simm9:$off)>;
def : Pat<(post_truncsti16 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRHHpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
             simm9:$off)>;
def : Pat<(post_truncsti8 GPR64:$Rt, GPR64sp:$addr, simm9:$off),
  (STRBBpost (EXTRACT_SUBREG GPR64:$Rt, sub_32), GPR64sp:$addr,
             simm9:$off)>;

def : Pat<(post_store (v8i8 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4i16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2i32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2f32 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v1i64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v1f64 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4f16 FPR64:$Rt), GPR64sp:$addr, simm9:$off),
          (STRDpost FPR64:$Rt, GPR64sp:$addr, simm9:$off)>;

def : Pat<(post_store (v16i8 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v8i16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4i32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v4f32 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2i64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v2f64 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;
def : Pat<(post_store (v8f16 FPR128:$Rt), GPR64sp:$addr, simm9:$off),
          (STRQpost FPR128:$Rt, GPR64sp:$addr, simm9:$off)>;

//===----------------------------------------------------------------------===//
// Load/store exclusive instructions.
//===----------------------------------------------------------------------===//

def LDARW  : LoadAcquire   <0b10, 1, 1, 0, 1, GPR32, "ldar">;
def LDARX  : LoadAcquire   <0b11, 1, 1, 0, 1, GPR64, "ldar">;
def LDARB  : LoadAcquire   <0b00, 1, 1, 0, 1, GPR32, "ldarb">;
def LDARH  : LoadAcquire   <0b01, 1, 1, 0, 1, GPR32, "ldarh">;

def LDAXRW : LoadExclusive <0b10, 0, 1, 0, 1, GPR32, "ldaxr">;
def LDAXRX : LoadExclusive <0b11, 0, 1, 0, 1, GPR64, "ldaxr">;
def LDAXRB : LoadExclusive <0b00, 0, 1, 0, 1, GPR32, "ldaxrb">;
def LDAXRH : LoadExclusive <0b01, 0, 1, 0, 1, GPR32, "ldaxrh">;

def LDXRW  : LoadExclusive <0b10, 0, 1, 0, 0, GPR32, "ldxr">;
def LDXRX  : LoadExclusive <0b11, 0, 1, 0, 0, GPR64, "ldxr">;
def LDXRB  : LoadExclusive <0b00, 0, 1, 0, 0, GPR32, "ldxrb">;
def LDXRH  : LoadExclusive <0b01, 0, 1, 0, 0, GPR32, "ldxrh">;

def STLRW  : StoreRelease  <0b10, 1, 0, 0, 1, GPR32, "stlr">;
def STLRX  : StoreRelease  <0b11, 1, 0, 0, 1, GPR64, "stlr">;
def STLRB  : StoreRelease  <0b00, 1, 0, 0, 1, GPR32, "stlrb">;
def STLRH  : StoreRelease  <0b01, 1, 0, 0, 1, GPR32, "stlrh">;

def STLXRW : StoreExclusive<0b10, 0, 0, 0, 1, GPR32, "stlxr">;
def STLXRX : StoreExclusive<0b11, 0, 0, 0, 1, GPR64, "stlxr">;
def STLXRB : StoreExclusive<0b00, 0, 0, 0, 1, GPR32, "stlxrb">;
def STLXRH : StoreExclusive<0b01, 0, 0, 0, 1, GPR32, "stlxrh">;

def STXRW  : StoreExclusive<0b10, 0, 0, 0, 0, GPR32, "stxr">;
def STXRX  : StoreExclusive<0b11, 0, 0, 0, 0, GPR64, "stxr">;
def STXRB  : StoreExclusive<0b00, 0, 0, 0, 0, GPR32, "stxrb">;
def STXRH  : StoreExclusive<0b01, 0, 0, 0, 0, GPR32, "stxrh">;

def LDAXPW : LoadExclusivePair<0b10, 0, 1, 1, 1, GPR32, "ldaxp">;
def LDAXPX : LoadExclusivePair<0b11, 0, 1, 1, 1, GPR64, "ldaxp">;

def LDXPW  : LoadExclusivePair<0b10, 0, 1, 1, 0, GPR32, "ldxp">;
def LDXPX  : LoadExclusivePair<0b11, 0, 1, 1, 0, GPR64, "ldxp">;

def STLXPW : StoreExclusivePair<0b10, 0, 0, 1, 1, GPR32, "stlxp">;
def STLXPX : StoreExclusivePair<0b11, 0, 0, 1, 1, GPR64, "stlxp">;

def STXPW  : StoreExclusivePair<0b10, 0, 0, 1, 0, GPR32, "stxp">;
def STXPX  : StoreExclusivePair<0b11, 0, 0, 1, 0, GPR64, "stxp">;

let Predicates = [HasLOR] in {
  // v8.1a "Limited Order Region" extension load-acquire instructions
  def LDLARW  : LoadAcquire   <0b10, 1, 1, 0, 0, GPR32, "ldlar">;
  def LDLARX  : LoadAcquire   <0b11, 1, 1, 0, 0, GPR64, "ldlar">;
  def LDLARB  : LoadAcquire   <0b00, 1, 1, 0, 0, GPR32, "ldlarb">;
  def LDLARH  : LoadAcquire   <0b01, 1, 1, 0, 0, GPR32, "ldlarh">;

  // v8.1a "Limited Order Region" extension store-release instructions
  def STLLRW  : StoreRelease   <0b10, 1, 0, 0, 0, GPR32, "stllr">;
  def STLLRX  : StoreRelease   <0b11, 1, 0, 0, 0, GPR64, "stllr">;
  def STLLRB  : StoreRelease   <0b00, 1, 0, 0, 0, GPR32, "stllrb">;
  def STLLRH  : StoreRelease   <0b01, 1, 0, 0, 0, GPR32, "stllrh">;
}

//===----------------------------------------------------------------------===//
// Scaled floating point to integer conversion instructions.
//===----------------------------------------------------------------------===//

defm FCVTAS : FPToIntegerUnscaled<0b00, 0b100, "fcvtas", int_aarch64_neon_fcvtas>;
defm FCVTAU : FPToIntegerUnscaled<0b00, 0b101, "fcvtau", int_aarch64_neon_fcvtau>;
defm FCVTMS : FPToIntegerUnscaled<0b10, 0b000, "fcvtms", int_aarch64_neon_fcvtms>;
defm FCVTMU : FPToIntegerUnscaled<0b10, 0b001, "fcvtmu", int_aarch64_neon_fcvtmu>;
defm FCVTNS : FPToIntegerUnscaled<0b00, 0b000, "fcvtns", int_aarch64_neon_fcvtns>;
defm FCVTNU : FPToIntegerUnscaled<0b00, 0b001, "fcvtnu", int_aarch64_neon_fcvtnu>;
defm FCVTPS : FPToIntegerUnscaled<0b01, 0b000, "fcvtps", int_aarch64_neon_fcvtps>;
defm FCVTPU : FPToIntegerUnscaled<0b01, 0b001, "fcvtpu", int_aarch64_neon_fcvtpu>;
defm FCVTZS : FPToIntegerUnscaled<0b11, 0b000, "fcvtzs", fp_to_sint>;
defm FCVTZU : FPToIntegerUnscaled<0b11, 0b001, "fcvtzu", fp_to_uint>;
defm FCVTZS : FPToIntegerScaled<0b11, 0b000, "fcvtzs", fp_to_sint>;
defm FCVTZU : FPToIntegerScaled<0b11, 0b001, "fcvtzu", fp_to_uint>;

multiclass FPToIntegerIntPats<Intrinsic round, string INST> {
  def : Pat<(i32 (round f16:$Rn)), (!cast<Instruction>(INST # UWHr) $Rn)>;
  def : Pat<(i64 (round f16:$Rn)), (!cast<Instruction>(INST # UXHr) $Rn)>;
  def : Pat<(i32 (round f32:$Rn)), (!cast<Instruction>(INST # UWSr) $Rn)>;
  def : Pat<(i64 (round f32:$Rn)), (!cast<Instruction>(INST # UXSr) $Rn)>;
  def : Pat<(i32 (round f64:$Rn)), (!cast<Instruction>(INST # UWDr) $Rn)>;
  def : Pat<(i64 (round f64:$Rn)), (!cast<Instruction>(INST # UXDr) $Rn)>;

  def : Pat<(i32 (round (fmul f16:$Rn, fixedpoint_f16_i32:$scale))),
            (!cast<Instruction>(INST # SWHri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f16:$Rn, fixedpoint_f16_i64:$scale))),
            (!cast<Instruction>(INST # SXHri) $Rn, $scale)>;
  def : Pat<(i32 (round (fmul f32:$Rn, fixedpoint_f32_i32:$scale))),
            (!cast<Instruction>(INST # SWSri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f32:$Rn, fixedpoint_f32_i64:$scale))),
            (!cast<Instruction>(INST # SXSri) $Rn, $scale)>;
  def : Pat<(i32 (round (fmul f64:$Rn, fixedpoint_f64_i32:$scale))),
            (!cast<Instruction>(INST # SWDri) $Rn, $scale)>;
  def : Pat<(i64 (round (fmul f64:$Rn, fixedpoint_f64_i64:$scale))),
            (!cast<Instruction>(INST # SXDri) $Rn, $scale)>;
}

defm : FPToIntegerIntPats<int_aarch64_neon_fcvtzs, "FCVTZS">;
defm : FPToIntegerIntPats<int_aarch64_neon_fcvtzu, "FCVTZU">;

multiclass FPToIntegerPats<SDNode to_int, SDNode round, string INST> {
  def : Pat<(i32 (to_int (round f32:$Rn))),
            (!cast<Instruction>(INST # UWSr) f32:$Rn)>;
  def : Pat<(i64 (to_int (round f32:$Rn))),
            (!cast<Instruction>(INST # UXSr) f32:$Rn)>;
  def : Pat<(i32 (to_int (round f64:$Rn))),
            (!cast<Instruction>(INST # UWDr) f64:$Rn)>;
  def : Pat<(i64 (to_int (round f64:$Rn))),
            (!cast<Instruction>(INST # UXDr) f64:$Rn)>;
}

defm : FPToIntegerPats<fp_to_sint, fceil,  "FCVTPS">;
defm : FPToIntegerPats<fp_to_uint, fceil,  "FCVTPU">;
defm : FPToIntegerPats<fp_to_sint, ffloor, "FCVTMS">;
defm : FPToIntegerPats<fp_to_uint, ffloor, "FCVTMU">;
defm : FPToIntegerPats<fp_to_sint, ftrunc, "FCVTZS">;
defm : FPToIntegerPats<fp_to_uint, ftrunc, "FCVTZU">;
defm : FPToIntegerPats<fp_to_sint, fround, "FCVTAS">;
defm : FPToIntegerPats<fp_to_uint, fround, "FCVTAU">;

//===----------------------------------------------------------------------===//
// Scaled integer to floating point conversion instructions.
//===----------------------------------------------------------------------===//

defm SCVTF : IntegerToFP<0, "scvtf", sint_to_fp>;
defm UCVTF : IntegerToFP<1, "ucvtf", uint_to_fp>;

//===----------------------------------------------------------------------===//
// Unscaled integer to floating point conversion instruction.
//===----------------------------------------------------------------------===//

defm FMOV : UnscaledConversion<"fmov">;

// Add pseudo ops for FMOV 0 so we can mark them as isReMaterializable
let isReMaterializable = 1, isCodeGenOnly = 1, isAsCheapAsAMove = 1 in {
def FMOVH0 : Pseudo<(outs FPR16:$Rd), (ins), [(set f16:$Rd, (fpimm0))]>,
    Sched<[WriteF]>, Requires<[HasFullFP16]>;
def FMOVS0 : Pseudo<(outs FPR32:$Rd), (ins), [(set f32:$Rd, (fpimm0))]>,
    Sched<[WriteF]>;
def FMOVD0 : Pseudo<(outs FPR64:$Rd), (ins), [(set f64:$Rd, (fpimm0))]>,
    Sched<[WriteF]>;
}
// Similarly add aliases
def : InstAlias<"fmov $Rd, #0.0", (FMOVWHr FPR16:$Rd, WZR), 0>,
    Requires<[HasFullFP16]>;
def : InstAlias<"fmov $Rd, #0.0", (FMOVWSr FPR32:$Rd, WZR), 0>;
def : InstAlias<"fmov $Rd, #0.0", (FMOVXDr FPR64:$Rd, XZR), 0>;

//===----------------------------------------------------------------------===//
// Floating point conversion instruction.
//===----------------------------------------------------------------------===//

defm FCVT : FPConversion<"fcvt">;

//===----------------------------------------------------------------------===//
// Floating point single operand instructions.
//===----------------------------------------------------------------------===//

defm FABS   : SingleOperandFPData<0b0001, "fabs", fabs>;
defm FMOV   : SingleOperandFPData<0b0000, "fmov">;
defm FNEG   : SingleOperandFPData<0b0010, "fneg", fneg>;
defm FRINTA : SingleOperandFPData<0b1100, "frinta", fround>;
defm FRINTI : SingleOperandFPData<0b1111, "frinti", fnearbyint>;
defm FRINTM : SingleOperandFPData<0b1010, "frintm", ffloor>;
defm FRINTN : SingleOperandFPData<0b1000, "frintn", int_aarch64_neon_frintn>;
defm FRINTP : SingleOperandFPData<0b1001, "frintp", fceil>;

def : Pat<(v1f64 (int_aarch64_neon_frintn (v1f64 FPR64:$Rn))),
          (FRINTNDr FPR64:$Rn)>;

defm FRINTX : SingleOperandFPData<0b1110, "frintx", frint>;
defm FRINTZ : SingleOperandFPData<0b1011, "frintz", ftrunc>;

let SchedRW = [WriteFDiv] in {
defm FSQRT  : SingleOperandFPData<0b0011, "fsqrt", fsqrt>;
}

let Predicates = [HasFRInt3264] in {
  defm FRINT32Z : FRIntNNT<0b00, "frint32z">;
  defm FRINT64Z : FRIntNNT<0b10, "frint64z">;
  defm FRINT32X : FRIntNNT<0b01, "frint32x">;
  defm FRINT64X : FRIntNNT<0b11, "frint64x">;
} // HasFRInt3264

//===----------------------------------------------------------------------===//
// Floating point two operand instructions.
//===----------------------------------------------------------------------===//

defm FADD   : TwoOperandFPData<0b0010, "fadd", fadd>;
let SchedRW = [WriteFDiv] in {
defm FDIV   : TwoOperandFPData<0b0001, "fdiv", fdiv>;
}
defm FMAXNM : TwoOperandFPData<0b0110, "fmaxnm", fmaxnum>;
defm FMAX   : TwoOperandFPData<0b0100, "fmax", fmaximum>;
defm FMINNM : TwoOperandFPData<0b0111, "fminnm", fminnum>;
defm FMIN   : TwoOperandFPData<0b0101, "fmin", fminimum>;
let SchedRW = [WriteFMul] in {
defm FMUL   : TwoOperandFPData<0b0000, "fmul", fmul>;
defm FNMUL  : TwoOperandFPDataNeg<0b1000, "fnmul", fmul>;
}
defm FSUB   : TwoOperandFPData<0b0011, "fsub", fsub>;

def : Pat<(v1f64 (fmaximum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMAXDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fminimum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMINDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fmaxnum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMAXNMDrr FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v1f64 (fminnum (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FMINNMDrr FPR64:$Rn, FPR64:$Rm)>;

//===----------------------------------------------------------------------===//
// Floating point three operand instructions.
//===----------------------------------------------------------------------===//

defm FMADD  : ThreeOperandFPData<0, 0, "fmadd", fma>;
defm FMSUB  : ThreeOperandFPData<0, 1, "fmsub",
     TriOpFrag<(fma node:$LHS, (fneg node:$MHS), node:$RHS)> >;
defm FNMADD : ThreeOperandFPData<1, 0, "fnmadd",
     TriOpFrag<(fneg (fma node:$LHS, node:$MHS, node:$RHS))> >;
defm FNMSUB : ThreeOperandFPData<1, 1, "fnmsub",
     TriOpFrag<(fma node:$LHS, node:$MHS, (fneg node:$RHS))> >;

// The following def pats catch the case where the LHS of an FMA is negated.
// The TriOpFrag above catches the case where the middle operand is negated.

// N.b. FMSUB etc have the accumulator at the *end* of (outs), unlike
// the NEON variant.
def : Pat<(f32 (fma (fneg FPR32:$Rn), FPR32:$Rm, FPR32:$Ra)),
          (FMSUBSrrr FPR32:$Rn, FPR32:$Rm, FPR32:$Ra)>;

def : Pat<(f64 (fma (fneg FPR64:$Rn), FPR64:$Rm, FPR64:$Ra)),
          (FMSUBDrrr FPR64:$Rn, FPR64:$Rm, FPR64:$Ra)>;

// We handled -(a + b*c) for FNMADD above, now it's time for "(-a) + (-b)*c" and
// "(-a) + b*(-c)".
def : Pat<(f32 (fma (fneg FPR32:$Rn), FPR32:$Rm, (fneg FPR32:$Ra))),
          (FNMADDSrrr FPR32:$Rn, FPR32:$Rm, FPR32:$Ra)>;

def : Pat<(f64 (fma (fneg FPR64:$Rn), FPR64:$Rm, (fneg FPR64:$Ra))),
          (FNMADDDrrr FPR64:$Rn, FPR64:$Rm, FPR64:$Ra)>;

def : Pat<(f32 (fma FPR32:$Rn, (fneg FPR32:$Rm), (fneg FPR32:$Ra))),
          (FNMADDSrrr FPR32:$Rn, FPR32:$Rm, FPR32:$Ra)>;

def : Pat<(f64 (fma FPR64:$Rn, (fneg FPR64:$Rm), (fneg FPR64:$Ra))),
          (FNMADDDrrr FPR64:$Rn, FPR64:$Rm, FPR64:$Ra)>;

//===----------------------------------------------------------------------===//
// Floating point comparison instructions.
//===----------------------------------------------------------------------===//

defm FCMPE : FPComparison<1, "fcmpe">;
defm FCMP  : FPComparison<0, "fcmp", AArch64fcmp>;

//===----------------------------------------------------------------------===//
// Floating point conditional comparison instructions.
//===----------------------------------------------------------------------===//

defm FCCMPE : FPCondComparison<1, "fccmpe">;
defm FCCMP  : FPCondComparison<0, "fccmp", AArch64fccmp>;

//===----------------------------------------------------------------------===//
// Floating point conditional select instruction.
//===----------------------------------------------------------------------===//

defm FCSEL : FPCondSelect<"fcsel">;

// CSEL instructions providing f128 types need to be handled by a
// pseudo-instruction since the eventual code will need to introduce basic
// blocks and control flow.
def F128CSEL : Pseudo<(outs FPR128:$Rd),
                      (ins FPR128:$Rn, FPR128:$Rm, ccode:$cond),
                      [(set (f128 FPR128:$Rd),
                            (AArch64csel FPR128:$Rn, FPR128:$Rm,
                                       (i32 imm:$cond), NZCV))]> {
  let Uses = [NZCV];
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
}

//===----------------------------------------------------------------------===//
// Instructions used for emitting unwind opcodes on ARM64 Windows.
//===----------------------------------------------------------------------===//
let isPseudo = 1 in {
  def SEH_StackAlloc : Pseudo<(outs), (ins i32imm:$size), []>, Sched<[]>;
  def SEH_SaveFPLR : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFPLR_X : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveReg : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveReg_X : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveRegP : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveRegP_X : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFReg : Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFReg_X :  Pseudo<(outs), (ins i32imm:$reg, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFRegP : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SaveFRegP_X : Pseudo<(outs), (ins i32imm:$reg0, i32imm:$reg1, i32imm:$offs), []>, Sched<[]>;
  def SEH_SetFP : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_AddFP : Pseudo<(outs), (ins i32imm:$offs), []>, Sched<[]>;
  def SEH_Nop : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_PrologEnd : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_EpilogStart : Pseudo<(outs), (ins), []>, Sched<[]>;
  def SEH_EpilogEnd : Pseudo<(outs), (ins), []>, Sched<[]>;
}

// Pseudo instructions for Windows EH
//===----------------------------------------------------------------------===//
let isTerminator = 1, hasSideEffects = 1, isBarrier = 1, hasCtrlDep = 1,
    isCodeGenOnly = 1, isReturn = 1, isEHScopeReturn = 1, isPseudo = 1 in {
   def CLEANUPRET : Pseudo<(outs), (ins), [(cleanupret)]>, Sched<[]>;
   let usesCustomInserter = 1 in
     def CATCHRET : Pseudo<(outs), (ins am_brcond:$dst, am_brcond:$src), [(catchret bb:$dst, bb:$src)]>,
                    Sched<[]>;
}

let hasSideEffects = 1, hasCtrlDep = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in
def CATCHPAD : Pseudo<(outs), (ins), [(catchpad)]>, Sched<[]>;

//===----------------------------------------------------------------------===//
// Floating point immediate move.
//===----------------------------------------------------------------------===//

let isReMaterializable = 1 in {
defm FMOV : FPMoveImmediate<"fmov">;
}

//===----------------------------------------------------------------------===//
// Advanced SIMD two vector instructions.
//===----------------------------------------------------------------------===//

defm UABDL   : SIMDLongThreeVectorBHSabdl<1, 0b0111, "uabdl",
                                          int_aarch64_neon_uabd>;
// Match UABDL in log2-shuffle patterns.
def : Pat<(abs (v8i16 (sub (zext (v8i8 V64:$opA)),
                           (zext (v8i8 V64:$opB))))),
          (UABDLv8i8_v8i16 V64:$opA, V64:$opB)>;
def : Pat<(xor (v8i16 (AArch64vashr v8i16:$src, (i32 15))),
               (v8i16 (add (sub (zext (v8i8 V64:$opA)),
                                (zext (v8i8 V64:$opB))),
                           (AArch64vashr v8i16:$src, (i32 15))))),
          (UABDLv8i8_v8i16 V64:$opA, V64:$opB)>;
def : Pat<(abs (v8i16 (sub (zext (extract_high_v16i8 V128:$opA)),
                           (zext (extract_high_v16i8 V128:$opB))))),
          (UABDLv16i8_v8i16 V128:$opA, V128:$opB)>;
def : Pat<(xor (v8i16 (AArch64vashr v8i16:$src, (i32 15))),
               (v8i16 (add (sub (zext (extract_high_v16i8 V128:$opA)),
                                (zext (extract_high_v16i8 V128:$opB))),
                           (AArch64vashr v8i16:$src, (i32 15))))),
          (UABDLv16i8_v8i16 V128:$opA, V128:$opB)>;
def : Pat<(abs (v4i32 (sub (zext (v4i16 V64:$opA)),
                           (zext (v4i16 V64:$opB))))),
          (UABDLv4i16_v4i32 V64:$opA, V64:$opB)>;
def : Pat<(abs (v4i32 (sub (zext (extract_high_v8i16 V128:$opA)),
                           (zext (extract_high_v8i16 V128:$opB))))),
          (UABDLv8i16_v4i32 V128:$opA, V128:$opB)>;
def : Pat<(abs (v2i64 (sub (zext (v2i32 V64:$opA)),
                           (zext (v2i32 V64:$opB))))),
          (UABDLv2i32_v2i64 V64:$opA, V64:$opB)>;
def : Pat<(abs (v2i64 (sub (zext (extract_high_v4i32 V128:$opA)),
                           (zext (extract_high_v4i32 V128:$opB))))),
          (UABDLv4i32_v2i64 V128:$opA, V128:$opB)>;

defm ABS    : SIMDTwoVectorBHSD<0, 0b01011, "abs", abs>;
defm CLS    : SIMDTwoVectorBHS<0, 0b00100, "cls", int_aarch64_neon_cls>;
defm CLZ    : SIMDTwoVectorBHS<1, 0b00100, "clz", ctlz>;
defm CMEQ   : SIMDCmpTwoVector<0, 0b01001, "cmeq", AArch64cmeqz>;
defm CMGE   : SIMDCmpTwoVector<1, 0b01000, "cmge", AArch64cmgez>;
defm CMGT   : SIMDCmpTwoVector<0, 0b01000, "cmgt", AArch64cmgtz>;
defm CMLE   : SIMDCmpTwoVector<1, 0b01001, "cmle", AArch64cmlez>;
defm CMLT   : SIMDCmpTwoVector<0, 0b01010, "cmlt", AArch64cmltz>;
defm CNT    : SIMDTwoVectorB<0, 0b00, 0b00101, "cnt", ctpop>;
defm FABS   : SIMDTwoVectorFP<0, 1, 0b01111, "fabs", fabs>;

defm FCMEQ  : SIMDFPCmpTwoVector<0, 1, 0b01101, "fcmeq", AArch64fcmeqz>;
defm FCMGE  : SIMDFPCmpTwoVector<1, 1, 0b01100, "fcmge", AArch64fcmgez>;
defm FCMGT  : SIMDFPCmpTwoVector<0, 1, 0b01100, "fcmgt", AArch64fcmgtz>;
defm FCMLE  : SIMDFPCmpTwoVector<1, 1, 0b01101, "fcmle", AArch64fcmlez>;
defm FCMLT  : SIMDFPCmpTwoVector<0, 1, 0b01110, "fcmlt", AArch64fcmltz>;
defm FCVTAS : SIMDTwoVectorFPToInt<0,0,0b11100, "fcvtas",int_aarch64_neon_fcvtas>;
defm FCVTAU : SIMDTwoVectorFPToInt<1,0,0b11100, "fcvtau",int_aarch64_neon_fcvtau>;
defm FCVTL  : SIMDFPWidenTwoVector<0, 0, 0b10111, "fcvtl">;
def : Pat<(v4f32 (int_aarch64_neon_vcvthf2fp (v4i16 V64:$Rn))),
          (FCVTLv4i16 V64:$Rn)>;
def : Pat<(v4f32 (int_aarch64_neon_vcvthf2fp (extract_subvector (v8i16 V128:$Rn),
                                                              (i64 4)))),
          (FCVTLv8i16 V128:$Rn)>;
def : Pat<(v2f64 (fpextend (v2f32 V64:$Rn))), (FCVTLv2i32 V64:$Rn)>;
def : Pat<(v2f64 (fpextend (v2f32 (extract_subvector (v4f32 V128:$Rn),
                                                    (i64 2))))),
          (FCVTLv4i32 V128:$Rn)>;

def : Pat<(v4f32 (fpextend (v4f16 V64:$Rn))), (FCVTLv4i16 V64:$Rn)>;
def : Pat<(v4f32 (fpextend (v4f16 (extract_subvector (v8f16 V128:$Rn),
                                                    (i64 4))))),
          (FCVTLv8i16 V128:$Rn)>;

defm FCVTMS : SIMDTwoVectorFPToInt<0,0,0b11011, "fcvtms",int_aarch64_neon_fcvtms>;
defm FCVTMU : SIMDTwoVectorFPToInt<1,0,0b11011, "fcvtmu",int_aarch64_neon_fcvtmu>;
defm FCVTNS : SIMDTwoVectorFPToInt<0,0,0b11010, "fcvtns",int_aarch64_neon_fcvtns>;
defm FCVTNU : SIMDTwoVectorFPToInt<1,0,0b11010, "fcvtnu",int_aarch64_neon_fcvtnu>;
defm FCVTN  : SIMDFPNarrowTwoVector<0, 0, 0b10110, "fcvtn">;
def : Pat<(v4i16 (int_aarch64_neon_vcvtfp2hf (v4f32 V128:$Rn))),
          (FCVTNv4i16 V128:$Rn)>;
def : Pat<(concat_vectors V64:$Rd,
                          (v4i16 (int_aarch64_neon_vcvtfp2hf (v4f32 V128:$Rn)))),
          (FCVTNv8i16 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
def : Pat<(v2f32 (fpround (v2f64 V128:$Rn))), (FCVTNv2i32 V128:$Rn)>;
def : Pat<(v4f16 (fpround (v4f32 V128:$Rn))), (FCVTNv4i16 V128:$Rn)>;
def : Pat<(concat_vectors V64:$Rd, (v2f32 (fpround (v2f64 V128:$Rn)))),
          (FCVTNv4i32 (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), V128:$Rn)>;
defm FCVTPS : SIMDTwoVectorFPToInt<0,1,0b11010, "fcvtps",int_aarch64_neon_fcvtps>;
defm FCVTPU : SIMDTwoVectorFPToInt<1,1,0b11010, "fcvtpu",int_aarch64_neon_fcvtpu>;
defm FCVTXN : SIMDFPInexactCvtTwoVector<1, 0, 0b10110, "fcvtxn",
                                        int_aarch64_neon_fcvtxn>;
defm FCVTZS : SIMDTwoVectorFPToInt<0, 1, 0b11011, "fcvtzs", fp_to_sint>;
defm FCVTZU : SIMDTwoVectorFPToInt<1, 1, 0b11011, "fcvtzu", fp_to_uint>;

def : Pat<(v4i16 (int_aarch64_neon_fcvtzs v4f16:$Rn)), (FCVTZSv4f16 $Rn)>;
def : Pat<(v8i16 (int_aarch64_neon_fcvtzs v8f16:$Rn)), (FCVTZSv8f16 $Rn)>;
def : Pat<(v2i32 (int_aarch64_neon_fcvtzs v2f32:$Rn)), (FCVTZSv2f32 $Rn)>;
def : Pat<(v4i32 (int_aarch64_neon_fcvtzs v4f32:$Rn)), (FCVTZSv4f32 $Rn)>;
def : Pat<(v2i64 (int_aarch64_neon_fcvtzs v2f64:$Rn)), (FCVTZSv2f64 $Rn)>;

def : Pat<(v4i16 (int_aarch64_neon_fcvtzu v4f16:$Rn)), (FCVTZUv4f16 $Rn)>;
def : Pat<(v8i16 (int_aarch64_neon_fcvtzu v8f16:$Rn)), (FCVTZUv8f16 $Rn)>;
def : Pat<(v2i32 (int_aarch64_neon_fcvtzu v2f32:$Rn)), (FCVTZUv2f32 $Rn)>;
def : Pat<(v4i32 (int_aarch64_neon_fcvtzu v4f32:$Rn)), (FCVTZUv4f32 $Rn)>;
def : Pat<(v2i64 (int_aarch64_neon_fcvtzu v2f64:$Rn)), (FCVTZUv2f64 $Rn)>;

defm FNEG   : SIMDTwoVectorFP<1, 1, 0b01111, "fneg", fneg>;
defm FRECPE : SIMDTwoVectorFP<0, 1, 0b11101, "frecpe", int_aarch64_neon_frecpe>;
defm FRINTA : SIMDTwoVectorFP<1, 0, 0b11000, "frinta", fround>;
defm FRINTI : SIMDTwoVectorFP<1, 1, 0b11001, "frinti", fnearbyint>;
defm FRINTM : SIMDTwoVectorFP<0, 0, 0b11001, "frintm", ffloor>;
defm FRINTN : SIMDTwoVectorFP<0, 0, 0b11000, "frintn", int_aarch64_neon_frintn>;
defm FRINTP : SIMDTwoVectorFP<0, 1, 0b11000, "frintp", fceil>;
defm FRINTX : SIMDTwoVectorFP<1, 0, 0b11001, "frintx", frint>;
defm FRINTZ : SIMDTwoVectorFP<0, 1, 0b11001, "frintz", ftrunc>;

let Predicates = [HasFRInt3264] in {
  defm FRINT32Z : FRIntNNTVector<0, 0, "frint32z">;
  defm FRINT64Z : FRIntNNTVector<0, 1, "frint64z">;
  defm FRINT32X : FRIntNNTVector<1, 0, "frint32x">;
  defm FRINT64X : FRIntNNTVector<1, 1, "frint64x">;
} // HasFRInt3264

defm FRSQRTE: SIMDTwoVectorFP<1, 1, 0b11101, "frsqrte", int_aarch64_neon_frsqrte>;
defm FSQRT  : SIMDTwoVectorFP<1, 1, 0b11111, "fsqrt", fsqrt>;
defm NEG    : SIMDTwoVectorBHSD<1, 0b01011, "neg",
                               UnOpFrag<(sub immAllZerosV, node:$LHS)> >;
defm NOT    : SIMDTwoVectorB<1, 0b00, 0b00101, "not", vnot>;
// Aliases for MVN -> NOT.
def : InstAlias<"mvn{ $Vd.8b, $Vn.8b|.8b $Vd, $Vn}",
                (NOTv8i8 V64:$Vd, V64:$Vn)>;
def : InstAlias<"mvn{ $Vd.16b, $Vn.16b|.16b $Vd, $Vn}",
                (NOTv16i8 V128:$Vd, V128:$Vn)>;

def : Pat<(AArch64neg (v8i8  V64:$Rn)),  (NEGv8i8  V64:$Rn)>;
def : Pat<(AArch64neg (v16i8 V128:$Rn)), (NEGv16i8 V128:$Rn)>;
def : Pat<(AArch64neg (v4i16 V64:$Rn)),  (NEGv4i16 V64:$Rn)>;
def : Pat<(AArch64neg (v8i16 V128:$Rn)), (NEGv8i16 V128:$Rn)>;
def : Pat<(AArch64neg (v2i32 V64:$Rn)),  (NEGv2i32 V64:$Rn)>;
def : Pat<(AArch64neg (v4i32 V128:$Rn)), (NEGv4i32 V128:$Rn)>;
def : Pat<(AArch64neg (v2i64 V128:$Rn)), (NEGv2i64 V128:$Rn)>;

def : Pat<(AArch64not (v8i8 V64:$Rn)),   (NOTv8i8  V64:$Rn)>;
def : Pat<(AArch64not (v16i8 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(AArch64not (v4i16 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(AArch64not (v8i16 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(AArch64not (v2i32 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(AArch64not (v1i64 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(AArch64not (v4i32 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(AArch64not (v2i64 V128:$Rn)), (NOTv16i8 V128:$Rn)>;

def : Pat<(vnot (v4i16 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(vnot (v8i16 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(vnot (v2i32 V64:$Rn)),  (NOTv8i8  V64:$Rn)>;
def : Pat<(vnot (v4i32 V128:$Rn)), (NOTv16i8 V128:$Rn)>;
def : Pat<(vnot (v2i64 V128:$Rn)), (NOTv16i8 V128:$Rn)>;

defm RBIT   : SIMDTwoVectorB<1, 0b01, 0b00101, "rbit", int_aarch64_neon_rbit>;
defm REV16  : SIMDTwoVectorB<0, 0b00, 0b00001, "rev16", AArch64rev16>;
defm REV32  : SIMDTwoVectorBH<1, 0b00000, "rev32", AArch64rev32>;
defm REV64  : SIMDTwoVectorBHS<0, 0b00000, "rev64", AArch64rev64>;
defm SADALP : SIMDLongTwoVectorTied<0, 0b00110, "sadalp",
       BinOpFrag<(add node:$LHS, (int_aarch64_neon_saddlp node:$RHS))> >;
defm SADDLP : SIMDLongTwoVector<0, 0b00010, "saddlp", int_aarch64_neon_saddlp>;
defm SCVTF  : SIMDTwoVectorIntToFP<0, 0, 0b11101, "scvtf", sint_to_fp>;
defm SHLL   : SIMDVectorLShiftLongBySizeBHS;
defm SQABS  : SIMDTwoVectorBHSD<0, 0b00111, "sqabs", int_aarch64_neon_sqabs>;
defm SQNEG  : SIMDTwoVectorBHSD<1, 0b00111, "sqneg", int_aarch64_neon_sqneg>;
defm SQXTN  : SIMDMixedTwoVector<0, 0b10100, "sqxtn", int_aarch64_neon_sqxtn>;
defm SQXTUN : SIMDMixedTwoVector<1, 0b10010, "sqxtun", int_aarch64_neon_sqxtun>;
defm SUQADD : SIMDTwoVectorBHSDTied<0, 0b00011, "suqadd",int_aarch64_neon_suqadd>;
defm UADALP : SIMDLongTwoVectorTied<1, 0b00110, "uadalp",
       BinOpFrag<(add node:$LHS, (int_aarch64_neon_uaddlp node:$RHS))> >;
defm UADDLP : SIMDLongTwoVector<1, 0b00010, "uaddlp",
                    int_aarch64_neon_uaddlp>;
defm UCVTF  : SIMDTwoVectorIntToFP<1, 0, 0b11101, "ucvtf", uint_to_fp>;
defm UQXTN  : SIMDMixedTwoVector<1, 0b10100, "uqxtn", int_aarch64_neon_uqxtn>;
defm URECPE : SIMDTwoVectorS<0, 1, 0b11100, "urecpe", int_aarch64_neon_urecpe>;
defm URSQRTE: SIMDTwoVectorS<1, 1, 0b11100, "ursqrte", int_aarch64_neon_ursqrte>;
defm USQADD : SIMDTwoVectorBHSDTied<1, 0b00011, "usqadd",int_aarch64_neon_usqadd>;
defm XTN    : SIMDMixedTwoVector<0, 0b10010, "xtn", trunc>;

def : Pat<(v4f16 (AArch64rev32 V64:$Rn)), (REV32v4i16 V64:$Rn)>;
def : Pat<(v4f16 (AArch64rev64 V64:$Rn)), (REV64v4i16 V64:$Rn)>;
def : Pat<(v8f16 (AArch64rev32 V128:$Rn)), (REV32v8i16 V128:$Rn)>;
def : Pat<(v8f16 (AArch64rev64 V128:$Rn)), (REV64v8i16 V128:$Rn)>;
def : Pat<(v2f32 (AArch64rev64 V64:$Rn)), (REV64v2i32 V64:$Rn)>;
def : Pat<(v4f32 (AArch64rev64 V128:$Rn)), (REV64v4i32 V128:$Rn)>;

// Patterns for vector long shift (by element width). These need to match all
// three of zext, sext and anyext so it's easier to pull the patterns out of the
// definition.
multiclass SIMDVectorLShiftLongBySizeBHSPats<SDPatternOperator ext> {
  def : Pat<(AArch64vshl (v8i16 (ext (v8i8 V64:$Rn))), (i32 8)),
            (SHLLv8i8 V64:$Rn)>;
  def : Pat<(AArch64vshl (v8i16 (ext (extract_high_v16i8 V128:$Rn))), (i32 8)),
            (SHLLv16i8 V128:$Rn)>;
  def : Pat<(AArch64vshl (v4i32 (ext (v4i16 V64:$Rn))), (i32 16)),
            (SHLLv4i16 V64:$Rn)>;
  def : Pat<(AArch64vshl (v4i32 (ext (extract_high_v8i16 V128:$Rn))), (i32 16)),
            (SHLLv8i16 V128:$Rn)>;
  def : Pat<(AArch64vshl (v2i64 (ext (v2i32 V64:$Rn))), (i32 32)),
            (SHLLv2i32 V64:$Rn)>;
  def : Pat<(AArch64vshl (v2i64 (ext (extract_high_v4i32 V128:$Rn))), (i32 32)),
            (SHLLv4i32 V128:$Rn)>;
}

defm : SIMDVectorLShiftLongBySizeBHSPats<anyext>;
defm : SIMDVectorLShiftLongBySizeBHSPats<zext>;
defm : SIMDVectorLShiftLongBySizeBHSPats<sext>;

//===----------------------------------------------------------------------===//
// Advanced SIMD three vector instructions.
//===----------------------------------------------------------------------===//

defm ADD     : SIMDThreeSameVector<0, 0b10000, "add", add>;
defm ADDP    : SIMDThreeSameVector<0, 0b10111, "addp", int_aarch64_neon_addp>;
defm CMEQ    : SIMDThreeSameVector<1, 0b10001, "cmeq", AArch64cmeq>;
defm CMGE    : SIMDThreeSameVector<0, 0b00111, "cmge", AArch64cmge>;
defm CMGT    : SIMDThreeSameVector<0, 0b00110, "cmgt", AArch64cmgt>;
defm CMHI    : SIMDThreeSameVector<1, 0b00110, "cmhi", AArch64cmhi>;
defm CMHS    : SIMDThreeSameVector<1, 0b00111, "cmhs", AArch64cmhs>;
defm CMTST   : SIMDThreeSameVector<0, 0b10001, "cmtst", AArch64cmtst>;
defm FABD    : SIMDThreeSameVectorFP<1,1,0b010,"fabd", int_aarch64_neon_fabd>;
let Predicates = [HasNEON] in {
foreach VT = [ v2f32, v4f32, v2f64 ] in
def : Pat<(fabs (fsub VT:$Rn, VT:$Rm)), (!cast<Instruction>("FABD"#VT) VT:$Rn, VT:$Rm)>;
}
let Predicates = [HasNEON, HasFullFP16] in {
foreach VT = [ v4f16, v8f16 ] in
def : Pat<(fabs (fsub VT:$Rn, VT:$Rm)), (!cast<Instruction>("FABD"#VT) VT:$Rn, VT:$Rm)>;
}
defm FACGE   : SIMDThreeSameVectorFPCmp<1,0,0b101,"facge",int_aarch64_neon_facge>;
defm FACGT   : SIMDThreeSameVectorFPCmp<1,1,0b101,"facgt",int_aarch64_neon_facgt>;
defm FADDP   : SIMDThreeSameVectorFP<1,0,0b010,"faddp",int_aarch64_neon_addp>;
defm FADD    : SIMDThreeSameVectorFP<0,0,0b010,"fadd", fadd>;
defm FCMEQ   : SIMDThreeSameVectorFPCmp<0, 0, 0b100, "fcmeq", AArch64fcmeq>;
defm FCMGE   : SIMDThreeSameVectorFPCmp<1, 0, 0b100, "fcmge", AArch64fcmge>;
defm FCMGT   : SIMDThreeSameVectorFPCmp<1, 1, 0b100, "fcmgt", AArch64fcmgt>;
defm FDIV    : SIMDThreeSameVectorFP<1,0,0b111,"fdiv", fdiv>;
defm FMAXNMP : SIMDThreeSameVectorFP<1,0,0b000,"fmaxnmp", int_aarch64_neon_fmaxnmp>;
defm FMAXNM  : SIMDThreeSameVectorFP<0,0,0b000,"fmaxnm", fmaxnum>;
defm FMAXP   : SIMDThreeSameVectorFP<1,0,0b110,"fmaxp", int_aarch64_neon_fmaxp>;
defm FMAX    : SIMDThreeSameVectorFP<0,0,0b110,"fmax", fmaximum>;
defm FMINNMP : SIMDThreeSameVectorFP<1,1,0b000,"fminnmp", int_aarch64_neon_fminnmp>;
defm FMINNM  : SIMDThreeSameVectorFP<0,1,0b000,"fminnm", fminnum>;
defm FMINP   : SIMDThreeSameVectorFP<1,1,0b110,"fminp", int_aarch64_neon_fminp>;
defm FMIN    : SIMDThreeSameVectorFP<0,1,0b110,"fmin", fminimum>;

// NOTE: The operands of the PatFrag are reordered on FMLA/FMLS because the
// instruction expects the addend first, while the fma intrinsic puts it last.
defm FMLA     : SIMDThreeSameVectorFPTied<0, 0, 0b001, "fmla",
            TriOpFrag<(fma node:$RHS, node:$MHS, node:$LHS)> >;
defm FMLS     : SIMDThreeSameVectorFPTied<0, 1, 0b001, "fmls",
            TriOpFrag<(fma node:$MHS, (fneg node:$RHS), node:$LHS)> >;

// The following def pats catch the case where the LHS of an FMA is negated.
// The TriOpFrag above catches the case where the middle operand is negated.
def : Pat<(v2f32 (fma (fneg V64:$Rn), V64:$Rm, V64:$Rd)),
          (FMLSv2f32 V64:$Rd, V64:$Rn, V64:$Rm)>;

def : Pat<(v4f32 (fma (fneg V128:$Rn), V128:$Rm, V128:$Rd)),
          (FMLSv4f32 V128:$Rd, V128:$Rn, V128:$Rm)>;

def : Pat<(v2f64 (fma (fneg V128:$Rn), V128:$Rm, V128:$Rd)),
          (FMLSv2f64 V128:$Rd, V128:$Rn, V128:$Rm)>;

defm FMULX    : SIMDThreeSameVectorFP<0,0,0b011,"fmulx", int_aarch64_neon_fmulx>;
defm FMUL     : SIMDThreeSameVectorFP<1,0,0b011,"fmul", fmul>;
defm FRECPS   : SIMDThreeSameVectorFP<0,0,0b111,"frecps", int_aarch64_neon_frecps>;
defm FRSQRTS  : SIMDThreeSameVectorFP<0,1,0b111,"frsqrts", int_aarch64_neon_frsqrts>;
defm FSUB     : SIMDThreeSameVectorFP<0,1,0b010,"fsub", fsub>;
defm MLA      : SIMDThreeSameVectorBHSTied<0, 0b10010, "mla",
                      TriOpFrag<(add node:$LHS, (mul node:$MHS, node:$RHS))> >;
defm MLS      : SIMDThreeSameVectorBHSTied<1, 0b10010, "mls",
                      TriOpFrag<(sub node:$LHS, (mul node:$MHS, node:$RHS))> >;
defm MUL      : SIMDThreeSameVectorBHS<0, 0b10011, "mul", mul>;
defm PMUL     : SIMDThreeSameVectorB<1, 0b10011, "pmul", int_aarch64_neon_pmul>;
defm SABA     : SIMDThreeSameVectorBHSTied<0, 0b01111, "saba",
      TriOpFrag<(add node:$LHS, (int_aarch64_neon_sabd node:$MHS, node:$RHS))> >;
defm SABD     : SIMDThreeSameVectorBHS<0,0b01110,"sabd", int_aarch64_neon_sabd>;
defm SHADD    : SIMDThreeSameVectorBHS<0,0b00000,"shadd", int_aarch64_neon_shadd>;
defm SHSUB    : SIMDThreeSameVectorBHS<0,0b00100,"shsub", int_aarch64_neon_shsub>;
defm SMAXP    : SIMDThreeSameVectorBHS<0,0b10100,"smaxp", int_aarch64_neon_smaxp>;
defm SMAX     : SIMDThreeSameVectorBHS<0,0b01100,"smax", smax>;
defm SMINP    : SIMDThreeSameVectorBHS<0,0b10101,"sminp", int_aarch64_neon_sminp>;
defm SMIN     : SIMDThreeSameVectorBHS<0,0b01101,"smin", smin>;
defm SQADD    : SIMDThreeSameVector<0,0b00001,"sqadd", int_aarch64_neon_sqadd>;
defm SQDMULH  : SIMDThreeSameVectorHS<0,0b10110,"sqdmulh",int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDThreeSameVectorHS<1,0b10110,"sqrdmulh",int_aarch64_neon_sqrdmulh>;
defm SQRSHL   : SIMDThreeSameVector<0,0b01011,"sqrshl", int_aarch64_neon_sqrshl>;
defm SQSHL    : SIMDThreeSameVector<0,0b01001,"sqshl", int_aarch64_neon_sqshl>;
defm SQSUB    : SIMDThreeSameVector<0,0b00101,"sqsub", int_aarch64_neon_sqsub>;
defm SRHADD   : SIMDThreeSameVectorBHS<0,0b00010,"srhadd",int_aarch64_neon_srhadd>;
defm SRSHL    : SIMDThreeSameVector<0,0b01010,"srshl", int_aarch64_neon_srshl>;
defm SSHL     : SIMDThreeSameVector<0,0b01000,"sshl", int_aarch64_neon_sshl>;
defm SUB      : SIMDThreeSameVector<1,0b10000,"sub", sub>;
defm UABA     : SIMDThreeSameVectorBHSTied<1, 0b01111, "uaba",
      TriOpFrag<(add node:$LHS, (int_aarch64_neon_uabd node:$MHS, node:$RHS))> >;
defm UABD     : SIMDThreeSameVectorBHS<1,0b01110,"uabd", int_aarch64_neon_uabd>;
defm UHADD    : SIMDThreeSameVectorBHS<1,0b00000,"uhadd", int_aarch64_neon_uhadd>;
defm UHSUB    : SIMDThreeSameVectorBHS<1,0b00100,"uhsub", int_aarch64_neon_uhsub>;
defm UMAXP    : SIMDThreeSameVectorBHS<1,0b10100,"umaxp", int_aarch64_neon_umaxp>;
defm UMAX     : SIMDThreeSameVectorBHS<1,0b01100,"umax", umax>;
defm UMINP    : SIMDThreeSameVectorBHS<1,0b10101,"uminp", int_aarch64_neon_uminp>;
defm UMIN     : SIMDThreeSameVectorBHS<1,0b01101,"umin", umin>;
defm UQADD    : SIMDThreeSameVector<1,0b00001,"uqadd", int_aarch64_neon_uqadd>;
defm UQRSHL   : SIMDThreeSameVector<1,0b01011,"uqrshl", int_aarch64_neon_uqrshl>;
defm UQSHL    : SIMDThreeSameVector<1,0b01001,"uqshl", int_aarch64_neon_uqshl>;
defm UQSUB    : SIMDThreeSameVector<1,0b00101,"uqsub", int_aarch64_neon_uqsub>;
defm URHADD   : SIMDThreeSameVectorBHS<1,0b00010,"urhadd", int_aarch64_neon_urhadd>;
defm URSHL    : SIMDThreeSameVector<1,0b01010,"urshl", int_aarch64_neon_urshl>;
defm USHL     : SIMDThreeSameVector<1,0b01000,"ushl", int_aarch64_neon_ushl>;
defm SQRDMLAH : SIMDThreeSameVectorSQRDMLxHTiedHS<1,0b10000,"sqrdmlah",
                                                  int_aarch64_neon_sqadd>;
defm SQRDMLSH : SIMDThreeSameVectorSQRDMLxHTiedHS<1,0b10001,"sqrdmlsh",
                                                    int_aarch64_neon_sqsub>;

defm AND : SIMDLogicalThreeVector<0, 0b00, "and", and>;
defm BIC : SIMDLogicalThreeVector<0, 0b01, "bic",
                                  BinOpFrag<(and node:$LHS, (vnot node:$RHS))> >;
defm BIF : SIMDLogicalThreeVector<1, 0b11, "bif">;
defm BIT : SIMDLogicalThreeVectorTied<1, 0b10, "bit", AArch64bit>;
defm BSL : SIMDLogicalThreeVectorTied<1, 0b01, "bsl",
    TriOpFrag<(or (and node:$LHS, node:$MHS), (and (vnot node:$LHS), node:$RHS))>>;
defm EOR : SIMDLogicalThreeVector<1, 0b00, "eor", xor>;
defm ORN : SIMDLogicalThreeVector<0, 0b11, "orn",
                                  BinOpFrag<(or node:$LHS, (vnot node:$RHS))> >;
defm ORR : SIMDLogicalThreeVector<0, 0b10, "orr", or>;


def : Pat<(AArch64bsl (v8i8 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSLv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsl (v4i16 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSLv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsl (v2i32 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSLv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;
def : Pat<(AArch64bsl (v1i64 V64:$Rd), V64:$Rn, V64:$Rm),
          (BSLv8i8 V64:$Rd, V64:$Rn, V64:$Rm)>;

def : Pat<(AArch64bsl (v16i8 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSLv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsl (v8i16 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSLv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsl (v4i32 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSLv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;
def : Pat<(AArch64bsl (v2i64 V128:$Rd), V128:$Rn, V128:$Rm),
          (BSLv16i8 V128:$Rd, V128:$Rn, V128:$Rm)>;

def : InstAlias<"mov{\t$dst.16b, $src.16b|.16b\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 1>;
def : InstAlias<"mov{\t$dst.8h, $src.8h|.8h\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;
def : InstAlias<"mov{\t$dst.4s, $src.4s|.4s\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;
def : InstAlias<"mov{\t$dst.2d, $src.2d|.2d\t$dst, $src}",
                (ORRv16i8 V128:$dst, V128:$src, V128:$src), 0>;

def : InstAlias<"mov{\t$dst.8b, $src.8b|.8b\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 1>;
def : InstAlias<"mov{\t$dst.4h, $src.4h|.4h\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;
def : InstAlias<"mov{\t$dst.2s, $src.2s|.2s\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;
def : InstAlias<"mov{\t$dst.1d, $src.1d|.1d\t$dst, $src}",
                (ORRv8i8 V64:$dst, V64:$src, V64:$src), 0>;

def : InstAlias<"{cmls\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmls.8b\t$dst, $src1, $src2}",
                (CMHSv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmls.16b\t$dst, $src1, $src2}",
                (CMHSv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmls.4h\t$dst, $src1, $src2}",
                (CMHSv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmls.8h\t$dst, $src1, $src2}",
                (CMHSv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmls.2s\t$dst, $src1, $src2}",
                (CMHSv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmls\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmls.4s\t$dst, $src1, $src2}",
                (CMHSv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmls\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmls.2d\t$dst, $src1, $src2}",
                (CMHSv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmlo\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmlo.8b\t$dst, $src1, $src2}",
                (CMHIv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmlo.16b\t$dst, $src1, $src2}",
                (CMHIv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmlo.4h\t$dst, $src1, $src2}",
                (CMHIv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmlo.8h\t$dst, $src1, $src2}",
                (CMHIv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmlo.2s\t$dst, $src1, $src2}",
                (CMHIv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmlo.4s\t$dst, $src1, $src2}",
                (CMHIv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlo\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmlo.2d\t$dst, $src1, $src2}",
                (CMHIv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmle\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmle.8b\t$dst, $src1, $src2}",
                (CMGEv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmle.16b\t$dst, $src1, $src2}",
                (CMGEv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmle.4h\t$dst, $src1, $src2}",
                (CMGEv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmle.8h\t$dst, $src1, $src2}",
                (CMGEv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmle.2s\t$dst, $src1, $src2}",
                (CMGEv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmle\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmle.4s\t$dst, $src1, $src2}",
                (CMGEv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmle\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmle.2d\t$dst, $src1, $src2}",
                (CMGEv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

def : InstAlias<"{cmlt\t$dst.8b, $src1.8b, $src2.8b" #
                "|cmlt.8b\t$dst, $src1, $src2}",
                (CMGTv8i8 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.16b, $src1.16b, $src2.16b" #
                "|cmlt.16b\t$dst, $src1, $src2}",
                (CMGTv16i8 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.4h, $src1.4h, $src2.4h" #
                "|cmlt.4h\t$dst, $src1, $src2}",
                (CMGTv4i16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.8h, $src1.8h, $src2.8h" #
                "|cmlt.8h\t$dst, $src1, $src2}",
                (CMGTv8i16 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.2s, $src1.2s, $src2.2s" #
                "|cmlt.2s\t$dst, $src1, $src2}",
                (CMGTv2i32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.4s, $src1.4s, $src2.4s" #
                "|cmlt.4s\t$dst, $src1, $src2}",
                (CMGTv4i32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{cmlt\t$dst.2d, $src1.2d, $src2.2d" #
                "|cmlt.2d\t$dst, $src1, $src2}",
                (CMGTv2i64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{fcmle\t$dst.4h, $src1.4h, $src2.4h" #
                "|fcmle.4h\t$dst, $src1, $src2}",
                (FCMGEv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.8h, $src1.8h, $src2.8h" #
                "|fcmle.8h\t$dst, $src1, $src2}",
                (FCMGEv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{fcmle\t$dst.2s, $src1.2s, $src2.2s" #
                "|fcmle.2s\t$dst, $src1, $src2}",
                (FCMGEv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.4s, $src1.4s, $src2.4s" #
                "|fcmle.4s\t$dst, $src1, $src2}",
                (FCMGEv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{fcmle\t$dst.2d, $src1.2d, $src2.2d" #
                "|fcmle.2d\t$dst, $src1, $src2}",
                (FCMGEv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{fcmlt\t$dst.4h, $src1.4h, $src2.4h" #
                "|fcmlt.4h\t$dst, $src1, $src2}",
                (FCMGTv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.8h, $src1.8h, $src2.8h" #
                "|fcmlt.8h\t$dst, $src1, $src2}",
                (FCMGTv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{fcmlt\t$dst.2s, $src1.2s, $src2.2s" #
                "|fcmlt.2s\t$dst, $src1, $src2}",
                (FCMGTv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.4s, $src1.4s, $src2.4s" #
                "|fcmlt.4s\t$dst, $src1, $src2}",
                (FCMGTv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{fcmlt\t$dst.2d, $src1.2d, $src2.2d" #
                "|fcmlt.2d\t$dst, $src1, $src2}",
                (FCMGTv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{facle\t$dst.4h, $src1.4h, $src2.4h" #
                "|facle.4h\t$dst, $src1, $src2}",
                (FACGEv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{facle\t$dst.8h, $src1.8h, $src2.8h" #
                "|facle.8h\t$dst, $src1, $src2}",
                (FACGEv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{facle\t$dst.2s, $src1.2s, $src2.2s" #
                "|facle.2s\t$dst, $src1, $src2}",
                (FACGEv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{facle\t$dst.4s, $src1.4s, $src2.4s" #
                "|facle.4s\t$dst, $src1, $src2}",
                (FACGEv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{facle\t$dst.2d, $src1.2d, $src2.2d" #
                "|facle.2d\t$dst, $src1, $src2}",
                (FACGEv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

let Predicates = [HasNEON, HasFullFP16] in {
def : InstAlias<"{faclt\t$dst.4h, $src1.4h, $src2.4h" #
                "|faclt.4h\t$dst, $src1, $src2}",
                (FACGTv4f16 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{faclt\t$dst.8h, $src1.8h, $src2.8h" #
                "|faclt.8h\t$dst, $src1, $src2}",
                (FACGTv8f16 V128:$dst, V128:$src2, V128:$src1), 0>;
}
def : InstAlias<"{faclt\t$dst.2s, $src1.2s, $src2.2s" #
                "|faclt.2s\t$dst, $src1, $src2}",
                (FACGTv2f32 V64:$dst, V64:$src2, V64:$src1), 0>;
def : InstAlias<"{faclt\t$dst.4s, $src1.4s, $src2.4s" #
                "|faclt.4s\t$dst, $src1, $src2}",
                (FACGTv4f32 V128:$dst, V128:$src2, V128:$src1), 0>;
def : InstAlias<"{faclt\t$dst.2d, $src1.2d, $src2.2d" #
                "|faclt.2d\t$dst, $src1, $src2}",
                (FACGTv2f64 V128:$dst, V128:$src2, V128:$src1), 0>;

//===----------------------------------------------------------------------===//
// Advanced SIMD three scalar instructions.
//===----------------------------------------------------------------------===//

defm ADD      : SIMDThreeScalarD<0, 0b10000, "add", add>;
defm CMEQ     : SIMDThreeScalarD<1, 0b10001, "cmeq", AArch64cmeq>;
defm CMGE     : SIMDThreeScalarD<0, 0b00111, "cmge", AArch64cmge>;
defm CMGT     : SIMDThreeScalarD<0, 0b00110, "cmgt", AArch64cmgt>;
defm CMHI     : SIMDThreeScalarD<1, 0b00110, "cmhi", AArch64cmhi>;
defm CMHS     : SIMDThreeScalarD<1, 0b00111, "cmhs", AArch64cmhs>;
defm CMTST    : SIMDThreeScalarD<0, 0b10001, "cmtst", AArch64cmtst>;
defm FABD     : SIMDFPThreeScalar<1, 1, 0b010, "fabd", int_aarch64_sisd_fabd>;
def : Pat<(v1f64 (int_aarch64_neon_fabd (v1f64 FPR64:$Rn), (v1f64 FPR64:$Rm))),
          (FABD64 FPR64:$Rn, FPR64:$Rm)>;
let Predicates = [HasFullFP16] in {
def : Pat<(fabs (fsub f16:$Rn, f16:$Rm)), (FABD16 f16:$Rn, f16:$Rm)>;
}
def : Pat<(fabs (fsub f32:$Rn, f32:$Rm)), (FABD32 f32:$Rn, f32:$Rm)>;
def : Pat<(fabs (fsub f64:$Rn, f64:$Rm)), (FABD64 f64:$Rn, f64:$Rm)>;
defm FACGE    : SIMDThreeScalarFPCmp<1, 0, 0b101, "facge",
                                     int_aarch64_neon_facge>;
defm FACGT    : SIMDThreeScalarFPCmp<1, 1, 0b101, "facgt",
                                     int_aarch64_neon_facgt>;
defm FCMEQ    : SIMDThreeScalarFPCmp<0, 0, 0b100, "fcmeq", AArch64fcmeq>;
defm FCMGE    : SIMDThreeScalarFPCmp<1, 0, 0b100, "fcmge", AArch64fcmge>;
defm FCMGT    : SIMDThreeScalarFPCmp<1, 1, 0b100, "fcmgt", AArch64fcmgt>;
defm FMULX    : SIMDFPThreeScalar<0, 0, 0b011, "fmulx", int_aarch64_neon_fmulx>;
defm FRECPS   : SIMDFPThreeScalar<0, 0, 0b111, "frecps", int_aarch64_neon_frecps>;
defm FRSQRTS  : SIMDFPThreeScalar<0, 1, 0b111, "frsqrts", int_aarch64_neon_frsqrts>;
defm SQADD    : SIMDThreeScalarBHSD<0, 0b00001, "sqadd", int_aarch64_neon_sqadd>;
defm SQDMULH  : SIMDThreeScalarHS<  0, 0b10110, "sqdmulh", int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDThreeScalarHS<  1, 0b10110, "sqrdmulh", int_aarch64_neon_sqrdmulh>;
defm SQRSHL   : SIMDThreeScalarBHSD<0, 0b01011, "sqrshl",int_aarch64_neon_sqrshl>;
defm SQSHL    : SIMDThreeScalarBHSD<0, 0b01001, "sqshl", int_aarch64_neon_sqshl>;
defm SQSUB    : SIMDThreeScalarBHSD<0, 0b00101, "sqsub", int_aarch64_neon_sqsub>;
defm SRSHL    : SIMDThreeScalarD<   0, 0b01010, "srshl", int_aarch64_neon_srshl>;
defm SSHL     : SIMDThreeScalarD<   0, 0b01000, "sshl", int_aarch64_neon_sshl>;
defm SUB      : SIMDThreeScalarD<   1, 0b10000, "sub", sub>;
defm UQADD    : SIMDThreeScalarBHSD<1, 0b00001, "uqadd", int_aarch64_neon_uqadd>;
defm UQRSHL   : SIMDThreeScalarBHSD<1, 0b01011, "uqrshl",int_aarch64_neon_uqrshl>;
defm UQSHL    : SIMDThreeScalarBHSD<1, 0b01001, "uqshl", int_aarch64_neon_uqshl>;
defm UQSUB    : SIMDThreeScalarBHSD<1, 0b00101, "uqsub", int_aarch64_neon_uqsub>;
defm URSHL    : SIMDThreeScalarD<   1, 0b01010, "urshl", int_aarch64_neon_urshl>;
defm USHL     : SIMDThreeScalarD<   1, 0b01000, "ushl", int_aarch64_neon_ushl>;
let Predicates = [HasRDM] in {
  defm SQRDMLAH : SIMDThreeScalarHSTied<1, 0, 0b10000, "sqrdmlah">;
  defm SQRDMLSH : SIMDThreeScalarHSTied<1, 0, 0b10001, "sqrdmlsh">;
  def : Pat<(i32 (int_aarch64_neon_sqadd
                   (i32 FPR32:$Rd),
                   (i32 (int_aarch64_neon_sqrdmulh (i32 FPR32:$Rn),
                                                   (i32 FPR32:$Rm))))),
            (SQRDMLAHv1i32 FPR32:$Rd, FPR32:$Rn, FPR32:$Rm)>;
  def : Pat<(i32 (int_aarch64_neon_sqsub
                   (i32 FPR32:$Rd),
                   (i32 (int_aarch64_neon_sqrdmulh (i32 FPR32:$Rn),
                                                   (i32 FPR32:$Rm))))),
            (SQRDMLSHv1i32 FPR32:$Rd, FPR32:$Rn, FPR32:$Rm)>;
}

def : InstAlias<"cmls $dst, $src1, $src2",
                (CMHSv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmle $dst, $src1, $src2",
                (CMGEv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmlo $dst, $src1, $src2",
                (CMHIv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"cmlt $dst, $src1, $src2",
                (CMGTv1i64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"fcmle $dst, $src1, $src2",
                (FCMGE32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"fcmle $dst, $src1, $src2",
                (FCMGE64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"fcmlt $dst, $src1, $src2",
                (FCMGT32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"fcmlt $dst, $src1, $src2",
                (FCMGT64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"facle $dst, $src1, $src2",
                (FACGE32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"facle $dst, $src1, $src2",
                (FACGE64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;
def : InstAlias<"faclt $dst, $src1, $src2",
                (FACGT32 FPR32:$dst, FPR32:$src2, FPR32:$src1), 0>;
def : InstAlias<"faclt $dst, $src1, $src2",
                (FACGT64 FPR64:$dst, FPR64:$src2, FPR64:$src1), 0>;

//===----------------------------------------------------------------------===//
// Advanced SIMD three scalar instructions (mixed operands).
//===----------------------------------------------------------------------===//
defm SQDMULL  : SIMDThreeScalarMixedHS<0, 0b11010, "sqdmull",
                                       int_aarch64_neon_sqdmulls_scalar>;
defm SQDMLAL  : SIMDThreeScalarMixedTiedHS<0, 0b10010, "sqdmlal">;
defm SQDMLSL  : SIMDThreeScalarMixedTiedHS<0, 0b10110, "sqdmlsl">;

def : Pat<(i64 (int_aarch64_neon_sqadd (i64 FPR64:$Rd),
                   (i64 (int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                                        (i32 FPR32:$Rm))))),
          (SQDMLALi32 FPR64:$Rd, FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_sqsub (i64 FPR64:$Rd),
                   (i64 (int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                                        (i32 FPR32:$Rm))))),
          (SQDMLSLi32 FPR64:$Rd, FPR32:$Rn, FPR32:$Rm)>;

//===----------------------------------------------------------------------===//
// Advanced SIMD two scalar instructions.
//===----------------------------------------------------------------------===//

defm ABS    : SIMDTwoScalarD<    0, 0b01011, "abs", abs>;
defm CMEQ   : SIMDCmpTwoScalarD< 0, 0b01001, "cmeq", AArch64cmeqz>;
defm CMGE   : SIMDCmpTwoScalarD< 1, 0b01000, "cmge", AArch64cmgez>;
defm CMGT   : SIMDCmpTwoScalarD< 0, 0b01000, "cmgt", AArch64cmgtz>;
defm CMLE   : SIMDCmpTwoScalarD< 1, 0b01001, "cmle", AArch64cmlez>;
defm CMLT   : SIMDCmpTwoScalarD< 0, 0b01010, "cmlt", AArch64cmltz>;
defm FCMEQ  : SIMDFPCmpTwoScalar<0, 1, 0b01101, "fcmeq", AArch64fcmeqz>;
defm FCMGE  : SIMDFPCmpTwoScalar<1, 1, 0b01100, "fcmge", AArch64fcmgez>;
defm FCMGT  : SIMDFPCmpTwoScalar<0, 1, 0b01100, "fcmgt", AArch64fcmgtz>;
defm FCMLE  : SIMDFPCmpTwoScalar<1, 1, 0b01101, "fcmle", AArch64fcmlez>;
defm FCMLT  : SIMDFPCmpTwoScalar<0, 1, 0b01110, "fcmlt", AArch64fcmltz>;
defm FCVTAS : SIMDFPTwoScalar<   0, 0, 0b11100, "fcvtas">;
defm FCVTAU : SIMDFPTwoScalar<   1, 0, 0b11100, "fcvtau">;
defm FCVTMS : SIMDFPTwoScalar<   0, 0, 0b11011, "fcvtms">;
defm FCVTMU : SIMDFPTwoScalar<   1, 0, 0b11011, "fcvtmu">;
defm FCVTNS : SIMDFPTwoScalar<   0, 0, 0b11010, "fcvtns">;
defm FCVTNU : SIMDFPTwoScalar<   1, 0, 0b11010, "fcvtnu">;
defm FCVTPS : SIMDFPTwoScalar<   0, 1, 0b11010, "fcvtps">;
defm FCVTPU : SIMDFPTwoScalar<   1, 1, 0b11010, "fcvtpu">;
def  FCVTXNv1i64 : SIMDInexactCvtTwoScalar<0b10110, "fcvtxn">;
defm FCVTZS : SIMDFPTwoScalar<   0, 1, 0b11011, "fcvtzs">;
defm FCVTZU : SIMDFPTwoScalar<   1, 1, 0b11011, "fcvtzu">;
defm FRECPE : SIMDFPTwoScalar<   0, 1, 0b11101, "frecpe">;
defm FRECPX : SIMDFPTwoScalar<   0, 1, 0b11111, "frecpx">;
defm FRSQRTE : SIMDFPTwoScalar<  1, 1, 0b11101, "frsqrte">;
defm NEG    : SIMDTwoScalarD<    1, 0b01011, "neg",
                                 UnOpFrag<(sub immAllZerosV, node:$LHS)> >;
defm SCVTF  : SIMDFPTwoScalarCVT<   0, 0, 0b11101, "scvtf", AArch64sitof>;
defm SQABS  : SIMDTwoScalarBHSD< 0, 0b00111, "sqabs", int_aarch64_neon_sqabs>;
defm SQNEG  : SIMDTwoScalarBHSD< 1, 0b00111, "sqneg", int_aarch64_neon_sqneg>;
defm SQXTN  : SIMDTwoScalarMixedBHS< 0, 0b10100, "sqxtn", int_aarch64_neon_scalar_sqxtn>;
defm SQXTUN : SIMDTwoScalarMixedBHS< 1, 0b10010, "sqxtun", int_aarch64_neon_scalar_sqxtun>;
defm SUQADD : SIMDTwoScalarBHSDTied< 0, 0b00011, "suqadd",
                                     int_aarch64_neon_suqadd>;
defm UCVTF  : SIMDFPTwoScalarCVT<   1, 0, 0b11101, "ucvtf", AArch64uitof>;
defm UQXTN  : SIMDTwoScalarMixedBHS<1, 0b10100, "uqxtn", int_aarch64_neon_scalar_uqxtn>;
defm USQADD : SIMDTwoScalarBHSDTied< 1, 0b00011, "usqadd",
                                    int_aarch64_neon_usqadd>;

def : Pat<(AArch64neg (v1i64 V64:$Rn)), (NEGv1i64 V64:$Rn)>;

def : Pat<(v1i64 (int_aarch64_neon_fcvtas (v1f64 FPR64:$Rn))),
          (FCVTASv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtau (v1f64 FPR64:$Rn))),
          (FCVTAUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtms (v1f64 FPR64:$Rn))),
          (FCVTMSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtmu (v1f64 FPR64:$Rn))),
          (FCVTMUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtns (v1f64 FPR64:$Rn))),
          (FCVTNSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtnu (v1f64 FPR64:$Rn))),
          (FCVTNUv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtps (v1f64 FPR64:$Rn))),
          (FCVTPSv1i64 FPR64:$Rn)>;
def : Pat<(v1i64 (int_aarch64_neon_fcvtpu (v1f64 FPR64:$Rn))),
          (FCVTPUv1i64 FPR64:$Rn)>;

def : Pat<(f16 (int_aarch64_neon_frecpe (f16 FPR16:$Rn))),
          (FRECPEv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frecpe (f32 FPR32:$Rn))),
          (FRECPEv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frecpe (f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frecpe (v1f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;

def : Pat<(f32 (AArch64frecpe (f32 FPR32:$Rn))),
          (FRECPEv1i32 FPR32:$Rn)>;
def : Pat<(v2f32 (AArch64frecpe (v2f32 V64:$Rn))),
          (FRECPEv2f32 V64:$Rn)>;
def : Pat<(v4f32 (AArch64frecpe (v4f32 FPR128:$Rn))),
          (FRECPEv4f32 FPR128:$Rn)>;
def : Pat<(f64 (AArch64frecpe (f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (AArch64frecpe (v1f64 FPR64:$Rn))),
          (FRECPEv1i64 FPR64:$Rn)>;
def : Pat<(v2f64 (AArch64frecpe (v2f64 FPR128:$Rn))),
          (FRECPEv2f64 FPR128:$Rn)>;

def : Pat<(f32 (AArch64frecps (f32 FPR32:$Rn), (f32 FPR32:$Rm))),
          (FRECPS32 FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(v2f32 (AArch64frecps (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
          (FRECPSv2f32 V64:$Rn, V64:$Rm)>;
def : Pat<(v4f32 (AArch64frecps (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm))),
          (FRECPSv4f32 FPR128:$Rn, FPR128:$Rm)>;
def : Pat<(f64 (AArch64frecps (f64 FPR64:$Rn), (f64 FPR64:$Rm))),
          (FRECPS64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v2f64 (AArch64frecps (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm))),
          (FRECPSv2f64 FPR128:$Rn, FPR128:$Rm)>;

def : Pat<(f16 (int_aarch64_neon_frecpx (f16 FPR16:$Rn))),
          (FRECPXv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frecpx (f32 FPR32:$Rn))),
          (FRECPXv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frecpx (f64 FPR64:$Rn))),
          (FRECPXv1i64 FPR64:$Rn)>;

def : Pat<(f16 (int_aarch64_neon_frsqrte (f16 FPR16:$Rn))),
          (FRSQRTEv1f16 FPR16:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_frsqrte (f32 FPR32:$Rn))),
          (FRSQRTEv1i32 FPR32:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_frsqrte (f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (int_aarch64_neon_frsqrte (v1f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;

def : Pat<(f32 (AArch64frsqrte (f32 FPR32:$Rn))),
          (FRSQRTEv1i32 FPR32:$Rn)>;
def : Pat<(v2f32 (AArch64frsqrte (v2f32 V64:$Rn))),
          (FRSQRTEv2f32 V64:$Rn)>;
def : Pat<(v4f32 (AArch64frsqrte (v4f32 FPR128:$Rn))),
          (FRSQRTEv4f32 FPR128:$Rn)>;
def : Pat<(f64 (AArch64frsqrte (f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v1f64 (AArch64frsqrte (v1f64 FPR64:$Rn))),
          (FRSQRTEv1i64 FPR64:$Rn)>;
def : Pat<(v2f64 (AArch64frsqrte (v2f64 FPR128:$Rn))),
          (FRSQRTEv2f64 FPR128:$Rn)>;

def : Pat<(f32 (AArch64frsqrts (f32 FPR32:$Rn), (f32 FPR32:$Rm))),
          (FRSQRTS32 FPR32:$Rn, FPR32:$Rm)>;
def : Pat<(v2f32 (AArch64frsqrts (v2f32 V64:$Rn), (v2f32 V64:$Rm))),
          (FRSQRTSv2f32 V64:$Rn, V64:$Rm)>;
def : Pat<(v4f32 (AArch64frsqrts (v4f32 FPR128:$Rn), (v4f32 FPR128:$Rm))),
          (FRSQRTSv4f32 FPR128:$Rn, FPR128:$Rm)>;
def : Pat<(f64 (AArch64frsqrts (f64 FPR64:$Rn), (f64 FPR64:$Rm))),
          (FRSQRTS64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(v2f64 (AArch64frsqrts (v2f64 FPR128:$Rn), (v2f64 FPR128:$Rm))),
          (FRSQRTSv2f64 FPR128:$Rn, FPR128:$Rm)>;

// If an integer is about to be converted to a floating point value,
// just load it on the floating point unit.
// Here are the patterns for 8 and 16-bits to float.
// 8-bits -> float.
multiclass UIntToFPROLoadPat<ValueType DstTy, ValueType SrcTy,
                             SDPatternOperator loadop, Instruction UCVTF,
                             ROAddrMode ro, Instruction LDRW, Instruction LDRX,
                             SubRegIndex sub> {
  def : Pat<(DstTy (uint_to_fp (SrcTy
                     (loadop (ro.Wpat GPR64sp:$Rn, GPR32:$Rm,
                                      ro.Wext:$extend))))),
           (UCVTF (INSERT_SUBREG (DstTy (IMPLICIT_DEF)),
                                 (LDRW GPR64sp:$Rn, GPR32:$Rm, ro.Wext:$extend),
                                 sub))>;

  def : Pat<(DstTy (uint_to_fp (SrcTy
                     (loadop (ro.Xpat GPR64sp:$Rn, GPR64:$Rm,
                                      ro.Wext:$extend))))),
           (UCVTF (INSERT_SUBREG (DstTy (IMPLICIT_DEF)),
                                 (LDRX GPR64sp:$Rn, GPR64:$Rm, ro.Xext:$extend),
                                 sub))>;
}

defm : UIntToFPROLoadPat<f32, i32, zextloadi8,
                         UCVTFv1i32, ro8, LDRBroW, LDRBroX, bsub>;
def : Pat <(f32 (uint_to_fp (i32
               (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub))>;
def : Pat <(f32 (uint_to_fp (i32
                     (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDURBi GPR64sp:$Rn, simm9:$offset), bsub))>;
// 16-bits -> float.
defm : UIntToFPROLoadPat<f32, i32, zextloadi16,
                         UCVTFv1i32, ro16, LDRHroW, LDRHroX, hsub>;
def : Pat <(f32 (uint_to_fp (i32
                  (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub))>;
def : Pat <(f32 (uint_to_fp (i32
                  (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i32 (INSERT_SUBREG (f32 (IMPLICIT_DEF)),
                          (LDURHi GPR64sp:$Rn, simm9:$offset), hsub))>;
// 32-bits are handled in target specific dag combine:
// performIntToFpCombine.
// 64-bits integer to 32-bits floating point, not possible with
// UCVTF on floating point registers (both source and destination
// must have the same size).

// Here are the patterns for 8, 16, 32, and 64-bits to double.
// 8-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, zextloadi8,
                         UCVTFv1i64, ro8, LDRBroW, LDRBroX, bsub>;
def : Pat <(f64 (uint_to_fp (i32
                    (zextloadi8 (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset), bsub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi8 (am_unscaled8 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURBi GPR64sp:$Rn, simm9:$offset), bsub))>;
// 16-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, zextloadi16,
                         UCVTFv1i64, ro16, LDRHroW, LDRHroX, hsub>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi16 (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRHui GPR64sp:$Rn, uimm12s2:$offset), hsub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (zextloadi16 (am_unscaled16 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURHi GPR64sp:$Rn, simm9:$offset), hsub))>;
// 32-bits -> double.
defm : UIntToFPROLoadPat<f64, i32, load,
                         UCVTFv1i64, ro32, LDRSroW, LDRSroX, ssub>;
def : Pat <(f64 (uint_to_fp (i32
                  (load (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDRSui GPR64sp:$Rn, uimm12s4:$offset), ssub))>;
def : Pat <(f64 (uint_to_fp (i32
                  (load (am_unscaled32 GPR64sp:$Rn, simm9:$offset))))),
           (UCVTFv1i64 (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                          (LDURSi GPR64sp:$Rn, simm9:$offset), ssub))>;
// 64-bits -> double are handled in target specific dag combine:
// performIntToFpCombine.

//===----------------------------------------------------------------------===//
// Advanced SIMD three different-sized vector instructions.
//===----------------------------------------------------------------------===//

defm ADDHN  : SIMDNarrowThreeVectorBHS<0,0b0100,"addhn", int_aarch64_neon_addhn>;
defm SUBHN  : SIMDNarrowThreeVectorBHS<0,0b0110,"subhn", int_aarch64_neon_subhn>;
defm RADDHN : SIMDNarrowThreeVectorBHS<1,0b0100,"raddhn",int_aarch64_neon_raddhn>;
defm RSUBHN : SIMDNarrowThreeVectorBHS<1,0b0110,"rsubhn",int_aarch64_neon_rsubhn>;
defm PMULL  : SIMDDifferentThreeVectorBD<0,0b1110,"pmull",int_aarch64_neon_pmull>;
defm SABAL  : SIMDLongThreeVectorTiedBHSabal<0,0b0101,"sabal",
                                             int_aarch64_neon_sabd>;
defm SABDL   : SIMDLongThreeVectorBHSabdl<0, 0b0111, "sabdl",
                                          int_aarch64_neon_sabd>;
defm SADDL   : SIMDLongThreeVectorBHS<   0, 0b0000, "saddl",
            BinOpFrag<(add (sext node:$LHS), (sext node:$RHS))>>;
defm SADDW   : SIMDWideThreeVectorBHS<   0, 0b0001, "saddw",
                 BinOpFrag<(add node:$LHS, (sext node:$RHS))>>;
defm SMLAL   : SIMDLongThreeVectorTiedBHS<0, 0b1000, "smlal",
    TriOpFrag<(add node:$LHS, (int_aarch64_neon_smull node:$MHS, node:$RHS))>>;
defm SMLSL   : SIMDLongThreeVectorTiedBHS<0, 0b1010, "smlsl",
    TriOpFrag<(sub node:$LHS, (int_aarch64_neon_smull node:$MHS, node:$RHS))>>;
defm SMULL   : SIMDLongThreeVectorBHS<0, 0b1100, "smull", int_aarch64_neon_smull>;
defm SQDMLAL : SIMDLongThreeVectorSQDMLXTiedHS<0, 0b1001, "sqdmlal",
                                               int_aarch64_neon_sqadd>;
defm SQDMLSL : SIMDLongThreeVectorSQDMLXTiedHS<0, 0b1011, "sqdmlsl",
                                               int_aarch64_neon_sqsub>;
defm SQDMULL : SIMDLongThreeVectorHS<0, 0b1101, "sqdmull",
                                     int_aarch64_neon_sqdmull>;
defm SSUBL   : SIMDLongThreeVectorBHS<0, 0b0010, "ssubl",
                 BinOpFrag<(sub (sext node:$LHS), (sext node:$RHS))>>;
defm SSUBW   : SIMDWideThreeVectorBHS<0, 0b0011, "ssubw",
                 BinOpFrag<(sub node:$LHS, (sext node:$RHS))>>;
defm UABAL   : SIMDLongThreeVectorTiedBHSabal<1, 0b0101, "uabal",
                                              int_aarch64_neon_uabd>;
defm UADDL   : SIMDLongThreeVectorBHS<1, 0b0000, "uaddl",
                 BinOpFrag<(add (zext node:$LHS), (zext node:$RHS))>>;
defm UADDW   : SIMDWideThreeVectorBHS<1, 0b0001, "uaddw",
                 BinOpFrag<(add node:$LHS, (zext node:$RHS))>>;
defm UMLAL   : SIMDLongThreeVectorTiedBHS<1, 0b1000, "umlal",
    TriOpFrag<(add node:$LHS, (int_aarch64_neon_umull node:$MHS, node:$RHS))>>;
defm UMLSL   : SIMDLongThreeVectorTiedBHS<1, 0b1010, "umlsl",
    TriOpFrag<(sub node:$LHS, (int_aarch64_neon_umull node:$MHS, node:$RHS))>>;
defm UMULL   : SIMDLongThreeVectorBHS<1, 0b1100, "umull", int_aarch64_neon_umull>;
defm USUBL   : SIMDLongThreeVectorBHS<1, 0b0010, "usubl",
                 BinOpFrag<(sub (zext node:$LHS), (zext node:$RHS))>>;
defm USUBW   : SIMDWideThreeVectorBHS<   1, 0b0011, "usubw",
                 BinOpFrag<(sub node:$LHS, (zext node:$RHS))>>;

// Additional patterns for SMULL and UMULL
multiclass Neon_mul_widen_patterns<SDPatternOperator opnode,
  Instruction INST8B, Instruction INST4H, Instruction INST2S> {
  def : Pat<(v8i16 (opnode (v8i8 V64:$Rn), (v8i8 V64:$Rm))),
            (INST8B V64:$Rn, V64:$Rm)>;
  def : Pat<(v4i32 (opnode (v4i16 V64:$Rn), (v4i16 V64:$Rm))),
            (INST4H V64:$Rn, V64:$Rm)>;
  def : Pat<(v2i64 (opnode (v2i32 V64:$Rn), (v2i32 V64:$Rm))),
            (INST2S V64:$Rn, V64:$Rm)>;
}

defm : Neon_mul_widen_patterns<AArch64smull, SMULLv8i8_v8i16,
  SMULLv4i16_v4i32, SMULLv2i32_v2i64>;
defm : Neon_mul_widen_patterns<AArch64umull, UMULLv8i8_v8i16,
  UMULLv4i16_v4i32, UMULLv2i32_v2i64>;

// Additional patterns for SMLAL/SMLSL and UMLAL/UMLSL
multiclass Neon_mulacc_widen_patterns<SDPatternOperator opnode,
  Instruction INST8B, Instruction INST4H, Instruction INST2S> {
  def : Pat<(v8i16 (opnode (v8i16 V128:$Rd), (v8i8 V64:$Rn), (v8i8 V64:$Rm))),
            (INST8B V128:$Rd, V64:$Rn, V64:$Rm)>;
  def : Pat<(v4i32 (opnode (v4i32 V128:$Rd), (v4i16 V64:$Rn), (v4i16 V64:$Rm))),
            (INST4H V128:$Rd, V64:$Rn, V64:$Rm)>;
  def : Pat<(v2i64 (opnode (v2i64 V128:$Rd), (v2i32 V64:$Rn), (v2i32 V64:$Rm))),
            (INST2S  V128:$Rd, V64:$Rn, V64:$Rm)>;
}

defm : Neon_mulacc_widen_patterns<
  TriOpFrag<(add node:$LHS, (AArch64smull node:$MHS, node:$RHS))>,
  SMLALv8i8_v8i16, SMLALv4i16_v4i32, SMLALv2i32_v2i64>;
defm : Neon_mulacc_widen_patterns<
  TriOpFrag<(add node:$LHS, (AArch64umull node:$MHS, node:$RHS))>,
  UMLALv8i8_v8i16, UMLALv4i16_v4i32, UMLALv2i32_v2i64>;
defm : Neon_mulacc_widen_patterns<
  TriOpFrag<(sub node:$LHS, (AArch64smull node:$MHS, node:$RHS))>,
  SMLSLv8i8_v8i16, SMLSLv4i16_v4i32, SMLSLv2i32_v2i64>;
defm : Neon_mulacc_widen_patterns<
  TriOpFrag<(sub node:$LHS, (AArch64umull node:$MHS, node:$RHS))>,
  UMLSLv8i8_v8i16, UMLSLv4i16_v4i32, UMLSLv2i32_v2i64>;

// Patterns for 64-bit pmull
def : Pat<(int_aarch64_neon_pmull64 V64:$Rn, V64:$Rm),
          (PMULLv1i64 V64:$Rn, V64:$Rm)>;
def : Pat<(int_aarch64_neon_pmull64 (extractelt (v2i64 V128:$Rn), (i64 1)),
                                    (extractelt (v2i64 V128:$Rm), (i64 1))),
          (PMULLv2i64 V128:$Rn, V128:$Rm)>;

// CodeGen patterns for addhn and subhn instructions, which can actually be
// written in LLVM IR without too much difficulty.

// ADDHN
def : Pat<(v8i8 (trunc (v8i16 (AArch64vlshr (add V128:$Rn, V128:$Rm), (i32 8))))),
          (ADDHNv8i16_v8i8 V128:$Rn, V128:$Rm)>;
def : Pat<(v4i16 (trunc (v4i32 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                           (i32 16))))),
          (ADDHNv4i32_v4i16 V128:$Rn, V128:$Rm)>;
def : Pat<(v2i32 (trunc (v2i64 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                           (i32 32))))),
          (ADDHNv2i64_v2i32 V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v8i8 V64:$Rd),
                          (trunc (v8i16 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 8))))),
          (ADDHNv8i16_v16i8 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v4i16 V64:$Rd),
                          (trunc (v4i32 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 16))))),
          (ADDHNv4i32_v8i16 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v2i32 V64:$Rd),
                          (trunc (v2i64 (AArch64vlshr (add V128:$Rn, V128:$Rm),
                                                    (i32 32))))),
          (ADDHNv2i64_v4i32 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;

// SUBHN
def : Pat<(v8i8 (trunc (v8i16 (AArch64vlshr (sub V128:$Rn, V128:$Rm), (i32 8))))),
          (SUBHNv8i16_v8i8 V128:$Rn, V128:$Rm)>;
def : Pat<(v4i16 (trunc (v4i32 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                           (i32 16))))),
          (SUBHNv4i32_v4i16 V128:$Rn, V128:$Rm)>;
def : Pat<(v2i32 (trunc (v2i64 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                           (i32 32))))),
          (SUBHNv2i64_v2i32 V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v8i8 V64:$Rd),
                          (trunc (v8i16 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 8))))),
          (SUBHNv8i16_v16i8 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v4i16 V64:$Rd),
                          (trunc (v4i32 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 16))))),
          (SUBHNv4i32_v8i16 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;
def : Pat<(concat_vectors (v2i32 V64:$Rd),
                          (trunc (v2i64 (AArch64vlshr (sub V128:$Rn, V128:$Rm),
                                                    (i32 32))))),
          (SUBHNv2i64_v4i32 (SUBREG_TO_REG (i32 0), V64:$Rd, dsub),
                            V128:$Rn, V128:$Rm)>;

//----------------------------------------------------------------------------
// AdvSIMD bitwise extract from vector instruction.
//----------------------------------------------------------------------------

defm EXT : SIMDBitwiseExtract<"ext">;

def AdjustExtImm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(8 + N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;
multiclass ExtPat<ValueType VT64, ValueType VT128, int N> {
  def : Pat<(VT64 (AArch64ext V64:$Rn, V64:$Rm, (i32 imm:$imm))),
            (EXTv8i8 V64:$Rn, V64:$Rm, imm:$imm)>;
  def : Pat<(VT128 (AArch64ext V128:$Rn, V128:$Rm, (i32 imm:$imm))),
            (EXTv16i8 V128:$Rn, V128:$Rm, imm:$imm)>;
  // We use EXT to handle extract_subvector to copy the upper 64-bits of a
  // 128-bit vector.
  def : Pat<(VT64 (extract_subvector V128:$Rn, (i64 N))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn, V128:$Rn, 8), dsub)>;
  // A 64-bit EXT of two halves of the same 128-bit register can be done as a
  // single 128-bit EXT.
  def : Pat<(VT64 (AArch64ext (extract_subvector V128:$Rn, (i64 0)),
                              (extract_subvector V128:$Rn, (i64 N)),
                              (i32 imm:$imm))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn, V128:$Rn, imm:$imm), dsub)>;
  // A 64-bit EXT of the high half of a 128-bit register can be done using a
  // 128-bit EXT of the whole register with an adjustment to the immediate. The
  // top half of the other operand will be unset, but that doesn't matter as it
  // will not be used.
  def : Pat<(VT64 (AArch64ext (extract_subvector V128:$Rn, (i64 N)),
                              V64:$Rm,
                              (i32 imm:$imm))),
            (EXTRACT_SUBREG (EXTv16i8 V128:$Rn,
                                      (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                                      (AdjustExtImm imm:$imm)), dsub)>;
}

defm : ExtPat<v8i8, v16i8, 8>;
defm : ExtPat<v4i16, v8i16, 4>;
defm : ExtPat<v4f16, v8f16, 4>;
defm : ExtPat<v2i32, v4i32, 2>;
defm : ExtPat<v2f32, v4f32, 2>;
defm : ExtPat<v1i64, v2i64, 1>;
defm : ExtPat<v1f64, v2f64, 1>;

//----------------------------------------------------------------------------
// AdvSIMD zip vector
//----------------------------------------------------------------------------

defm TRN1 : SIMDZipVector<0b010, "trn1", AArch64trn1>;
defm TRN2 : SIMDZipVector<0b110, "trn2", AArch64trn2>;
defm UZP1 : SIMDZipVector<0b001, "uzp1", AArch64uzp1>;
defm UZP2 : SIMDZipVector<0b101, "uzp2", AArch64uzp2>;
defm ZIP1 : SIMDZipVector<0b011, "zip1", AArch64zip1>;
defm ZIP2 : SIMDZipVector<0b111, "zip2", AArch64zip2>;

//----------------------------------------------------------------------------
// AdvSIMD TBL/TBX instructions
//----------------------------------------------------------------------------

defm TBL : SIMDTableLookup<    0, "tbl">;
defm TBX : SIMDTableLookupTied<1, "tbx">;

def : Pat<(v8i8 (int_aarch64_neon_tbl1 (v16i8 VecListOne128:$Rn), (v8i8 V64:$Ri))),
          (TBLv8i8One VecListOne128:$Rn, V64:$Ri)>;
def : Pat<(v16i8 (int_aarch64_neon_tbl1 (v16i8 V128:$Ri), (v16i8 V128:$Rn))),
          (TBLv16i8One V128:$Ri, V128:$Rn)>;

def : Pat<(v8i8 (int_aarch64_neon_tbx1 (v8i8 V64:$Rd),
                  (v16i8 VecListOne128:$Rn), (v8i8 V64:$Ri))),
          (TBXv8i8One V64:$Rd, VecListOne128:$Rn, V64:$Ri)>;
def : Pat<(v16i8 (int_aarch64_neon_tbx1 (v16i8 V128:$Rd),
                   (v16i8 V128:$Ri), (v16i8 V128:$Rn))),
          (TBXv16i8One V128:$Rd, V128:$Ri, V128:$Rn)>;


//----------------------------------------------------------------------------
// AdvSIMD scalar CPY instruction
//----------------------------------------------------------------------------

defm CPY : SIMDScalarCPY<"cpy">;

//----------------------------------------------------------------------------
// AdvSIMD scalar pairwise instructions
//----------------------------------------------------------------------------

defm ADDP    : SIMDPairwiseScalarD<0, 0b11011, "addp">;
defm FADDP   : SIMDFPPairwiseScalar<0, 0b01101, "faddp">;
defm FMAXNMP : SIMDFPPairwiseScalar<0, 0b01100, "fmaxnmp">;
defm FMAXP   : SIMDFPPairwiseScalar<0, 0b01111, "fmaxp">;
defm FMINNMP : SIMDFPPairwiseScalar<1, 0b01100, "fminnmp">;
defm FMINP   : SIMDFPPairwiseScalar<1, 0b01111, "fminp">;
def : Pat<(v2i64 (AArch64saddv V128:$Rn)),
          (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), (ADDPv2i64p V128:$Rn), dsub)>;
def : Pat<(v2i64 (AArch64uaddv V128:$Rn)),
          (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), (ADDPv2i64p V128:$Rn), dsub)>;
def : Pat<(f32 (int_aarch64_neon_faddv (v2f32 V64:$Rn))),
          (FADDPv2i32p V64:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_faddv (v4f32 V128:$Rn))),
          (FADDPv2i32p (EXTRACT_SUBREG (FADDPv4f32 V128:$Rn, V128:$Rn), dsub))>;
def : Pat<(f64 (int_aarch64_neon_faddv (v2f64 V128:$Rn))),
          (FADDPv2i64p V128:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_fmaxnmv (v2f32 V64:$Rn))),
          (FMAXNMPv2i32p V64:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_fmaxnmv (v2f64 V128:$Rn))),
          (FMAXNMPv2i64p V128:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_fmaxv (v2f32 V64:$Rn))),
          (FMAXPv2i32p V64:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_fmaxv (v2f64 V128:$Rn))),
          (FMAXPv2i64p V128:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_fminnmv (v2f32 V64:$Rn))),
          (FMINNMPv2i32p V64:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_fminnmv (v2f64 V128:$Rn))),
          (FMINNMPv2i64p V128:$Rn)>;
def : Pat<(f32 (int_aarch64_neon_fminv (v2f32 V64:$Rn))),
          (FMINPv2i32p V64:$Rn)>;
def : Pat<(f64 (int_aarch64_neon_fminv (v2f64 V128:$Rn))),
          (FMINPv2i64p V128:$Rn)>;

//----------------------------------------------------------------------------
// AdvSIMD INS/DUP instructions
//----------------------------------------------------------------------------

def DUPv8i8gpr  : SIMDDupFromMain<0, {?,?,?,?,1}, ".8b", v8i8, V64, GPR32>;
def DUPv16i8gpr : SIMDDupFromMain<1, {?,?,?,?,1}, ".16b", v16i8, V128, GPR32>;
def DUPv4i16gpr : SIMDDupFromMain<0, {?,?,?,1,0}, ".4h", v4i16, V64, GPR32>;
def DUPv8i16gpr : SIMDDupFromMain<1, {?,?,?,1,0}, ".8h", v8i16, V128, GPR32>;
def DUPv2i32gpr : SIMDDupFromMain<0, {?,?,1,0,0}, ".2s", v2i32, V64, GPR32>;
def DUPv4i32gpr : SIMDDupFromMain<1, {?,?,1,0,0}, ".4s", v4i32, V128, GPR32>;
def DUPv2i64gpr : SIMDDupFromMain<1, {?,1,0,0,0}, ".2d", v2i64, V128, GPR64>;

def DUPv2i64lane : SIMDDup64FromElement;
def DUPv2i32lane : SIMDDup32FromElement<0, ".2s", v2i32, V64>;
def DUPv4i32lane : SIMDDup32FromElement<1, ".4s", v4i32, V128>;
def DUPv4i16lane : SIMDDup16FromElement<0, ".4h", v4i16, V64>;
def DUPv8i16lane : SIMDDup16FromElement<1, ".8h", v8i16, V128>;
def DUPv8i8lane  : SIMDDup8FromElement <0, ".8b", v8i8, V64>;
def DUPv16i8lane : SIMDDup8FromElement <1, ".16b", v16i8, V128>;

// DUP from a 64-bit register to a 64-bit register is just a copy
def : Pat<(v1i64 (AArch64dup (i64 GPR64:$Rn))),
          (COPY_TO_REGCLASS GPR64:$Rn, FPR64)>;
def : Pat<(v1f64 (AArch64dup (f64 FPR64:$Rn))),
          (COPY_TO_REGCLASS FPR64:$Rn, FPR64)>;

def : Pat<(v2f32 (AArch64dup (f32 FPR32:$Rn))),
          (v2f32 (DUPv2i32lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rn, ssub),
            (i64 0)))>;
def : Pat<(v4f32 (AArch64dup (f32 FPR32:$Rn))),
          (v4f32 (DUPv4i32lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rn, ssub),
            (i64 0)))>;
def : Pat<(v2f64 (AArch64dup (f64 FPR64:$Rn))),
          (v2f64 (DUPv2i64lane
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$Rn, dsub),
            (i64 0)))>;
def : Pat<(v4f16 (AArch64dup (f16 FPR16:$Rn))),
          (v4f16 (DUPv4i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;
def : Pat<(v8f16 (AArch64dup (f16 FPR16:$Rn))),
          (v8f16 (DUPv8i16lane
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR16:$Rn, hsub),
            (i64 0)))>;

def : Pat<(v4f16 (AArch64duplane16 (v8f16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv4i16lane V128:$Rn, VectorIndexH:$imm)>;
def : Pat<(v8f16 (AArch64duplane16 (v8f16 V128:$Rn), VectorIndexH:$imm)),
          (DUPv8i16lane V128:$Rn, VectorIndexH:$imm)>;

def : Pat<(v2f32 (AArch64duplane32 (v4f32 V128:$Rn), VectorIndexS:$imm)),
          (DUPv2i32lane V128:$Rn, VectorIndexS:$imm)>;
def : Pat<(v4f32 (AArch64duplane32 (v4f32 V128:$Rn), VectorIndexS:$imm)),
         (DUPv4i32lane V128:$Rn, VectorIndexS:$imm)>;
def : Pat<(v2f64 (AArch64duplane64 (v2f64 V128:$Rn), VectorIndexD:$imm)),
          (DUPv2i64lane V128:$Rn, VectorIndexD:$imm)>;

// If there's an (AArch64dup (vector_extract ...) ...), we can use a duplane
// instruction even if the types don't match: we just have to remap the lane
// carefully. N.b. this trick only applies to truncations.
def VecIndex_x2 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(2 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;
def VecIndex_x4 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(4 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;
def VecIndex_x8 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(8 * N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;

multiclass DUPWithTruncPats<ValueType ResVT, ValueType Src64VT,
                            ValueType Src128VT, ValueType ScalVT,
                            Instruction DUP, SDNodeXForm IdxXFORM> {
  def : Pat<(ResVT (AArch64dup (ScalVT (vector_extract (Src128VT V128:$Rn),
                                                     imm:$idx)))),
            (DUP V128:$Rn, (IdxXFORM imm:$idx))>;

  def : Pat<(ResVT (AArch64dup (ScalVT (vector_extract (Src64VT V64:$Rn),
                                                     imm:$idx)))),
            (DUP (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), (IdxXFORM imm:$idx))>;
}

defm : DUPWithTruncPats<v8i8,   v4i16, v8i16, i32, DUPv8i8lane,  VecIndex_x2>;
defm : DUPWithTruncPats<v8i8,   v2i32, v4i32, i32, DUPv8i8lane,  VecIndex_x4>;
defm : DUPWithTruncPats<v4i16,  v2i32, v4i32, i32, DUPv4i16lane, VecIndex_x2>;

defm : DUPWithTruncPats<v16i8,  v4i16, v8i16, i32, DUPv16i8lane, VecIndex_x2>;
defm : DUPWithTruncPats<v16i8,  v2i32, v4i32, i32, DUPv16i8lane, VecIndex_x4>;
defm : DUPWithTruncPats<v8i16,  v2i32, v4i32, i32, DUPv8i16lane, VecIndex_x2>;

multiclass DUPWithTrunci64Pats<ValueType ResVT, Instruction DUP,
                               SDNodeXForm IdxXFORM> {
  def : Pat<(ResVT (AArch64dup (i32 (trunc (extractelt (v2i64 V128:$Rn),
                                                         imm:$idx))))),
            (DUP V128:$Rn, (IdxXFORM imm:$idx))>;

  def : Pat<(ResVT (AArch64dup (i32 (trunc (extractelt (v1i64 V64:$Rn),
                                                       imm:$idx))))),
            (DUP (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), (IdxXFORM imm:$idx))>;
}

defm : DUPWithTrunci64Pats<v8i8,  DUPv8i8lane,   VecIndex_x8>;
defm : DUPWithTrunci64Pats<v4i16, DUPv4i16lane,  VecIndex_x4>;
defm : DUPWithTrunci64Pats<v2i32, DUPv2i32lane,  VecIndex_x2>;

defm : DUPWithTrunci64Pats<v16i8, DUPv16i8lane, VecIndex_x8>;
defm : DUPWithTrunci64Pats<v8i16, DUPv8i16lane, VecIndex_x4>;
defm : DUPWithTrunci64Pats<v4i32, DUPv4i32lane, VecIndex_x2>;

// SMOV and UMOV definitions, with some extra patterns for convenience
defm SMOV : SMov;
defm UMOV : UMov;

def : Pat<(sext_inreg (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx), i8),
          (i32 (SMOVvi8to32 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx), i8),
          (i64 (SMOVvi8to64 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i32 (SMOVvi16to32 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i64 (SMOVvi16to64 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext_inreg (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),i16),
          (i32 (SMOVvi16to32 V128:$Rn, VectorIndexH:$idx))>;
def : Pat<(sext (i32 (vector_extract (v4i32 V128:$Rn), VectorIndexS:$idx))),
          (i64 (SMOVvi32to64 V128:$Rn, VectorIndexS:$idx))>;

def : Pat<(sext_inreg (i64 (anyext (i32 (vector_extract (v16i8 V128:$Rn),
            VectorIndexB:$idx)))), i8),
          (i64 (SMOVvi8to64 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(sext_inreg (i64 (anyext (i32 (vector_extract (v8i16 V128:$Rn),
            VectorIndexH:$idx)))), i16),
          (i64 (SMOVvi16to64 V128:$Rn, VectorIndexH:$idx))>;

// Extracting i8 or i16 elements will have the zero-extend transformed to
// an 'and' mask by type legalization since neither i8 nor i16 are legal types
// for AArch64. Match these patterns here since UMOV already zeroes out the high
// bits of the destination register.
def : Pat<(and (vector_extract (v16i8 V128:$Rn), VectorIndexB:$idx),
               (i32 0xff)),
          (i32 (UMOVvi8 V128:$Rn, VectorIndexB:$idx))>;
def : Pat<(and (vector_extract (v8i16 V128:$Rn), VectorIndexH:$idx),
               (i32 0xffff)),
          (i32 (UMOVvi16 V128:$Rn, VectorIndexH:$idx))>;

defm INS : SIMDIns;

def : Pat<(v16i8 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;
def : Pat<(v8i8 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;

def : Pat<(v8i16 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;
def : Pat<(v4i16 (scalar_to_vector GPR32:$Rn)),
          (SUBREG_TO_REG (i32 0),
                         (f32 (COPY_TO_REGCLASS GPR32:$Rn, FPR32)), ssub)>;

def : Pat<(v4f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v4f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v2i32 (scalar_to_vector (i32 FPR32:$Rn))),
            (v2i32 (INSERT_SUBREG (v2i32 (IMPLICIT_DEF)),
                                  (i32 FPR32:$Rn), ssub))>;
def : Pat<(v4i32 (scalar_to_vector (i32 FPR32:$Rn))),
            (v4i32 (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
                                  (i32 FPR32:$Rn), ssub))>;

def : Pat<(v2i64 (scalar_to_vector (i64 FPR64:$Rn))),
            (v2i64 (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                                  (i64 FPR64:$Rn), dsub))>;

def : Pat<(v4f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v4f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;
def : Pat<(v8f16 (scalar_to_vector (f16 FPR16:$Rn))),
          (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rn, hsub)>;

def : Pat<(v4f32 (scalar_to_vector (f32 FPR32:$Rn))),
          (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rn, ssub)>;
def : Pat<(v2f32 (scalar_to_vector (f32 FPR32:$Rn))),
          (INSERT_SUBREG (v2f32 (IMPLICIT_DEF)), FPR32:$Rn, ssub)>;

def : Pat<(v2f64 (scalar_to_vector (f64 FPR64:$Rn))),
          (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$Rn, dsub)>;

def : Pat<(v4f16 (vector_insert (v4f16 V64:$Rn),
            (f16 FPR16:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi16lane
              (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), V64:$Rn, dsub)),
              VectorIndexS:$imm,
              (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
              (i64 0)),
            dsub)>;

def : Pat<(v8f16 (vector_insert (v8f16 V128:$Rn),
            (f16 FPR16:$Rm), (i64 VectorIndexH:$imm))),
          (INSvi16lane
            V128:$Rn, VectorIndexH:$imm,
            (v8f16 (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR16:$Rm, hsub)),
            (i64 0))>;

def : Pat<(v2f32 (vector_insert (v2f32 V64:$Rn),
            (f32 FPR32:$Rm), (i64 VectorIndexS:$imm))),
          (EXTRACT_SUBREG
            (INSvi32lane
              (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), V64:$Rn, dsub)),
              VectorIndexS:$imm,
              (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rm, ssub)),
              (i64 0)),
            dsub)>;
def : Pat<(v4f32 (vector_insert (v4f32 V128:$Rn),
            (f32 FPR32:$Rm), (i64 VectorIndexS:$imm))),
          (INSvi32lane
            V128:$Rn, VectorIndexS:$imm,
            (v4f32 (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR32:$Rm, ssub)),
            (i64 0))>;
def : Pat<(v2f64 (vector_insert (v2f64 V128:$Rn),
            (f64 FPR64:$Rm), (i64 VectorIndexD:$imm))),
          (INSvi64lane
            V128:$Rn, VectorIndexD:$imm,
            (v2f64 (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$Rm, dsub)),
            (i64 0))>;

// Copy an element at a constant index in one vector into a constant indexed
// element of another.
// FIXME refactor to a shared class/dev parameterized on vector type, vector
// index type and INS extension
def : Pat<(v16i8 (int_aarch64_neon_vcopy_lane
                   (v16i8 V128:$Vd), VectorIndexB:$idx, (v16i8 V128:$Vs),
                   VectorIndexB:$idx2)),
          (v16i8 (INSvi8lane
                   V128:$Vd, VectorIndexB:$idx, V128:$Vs, VectorIndexB:$idx2)
          )>;
def : Pat<(v8i16 (int_aarch64_neon_vcopy_lane
                   (v8i16 V128:$Vd), VectorIndexH:$idx, (v8i16 V128:$Vs),
                   VectorIndexH:$idx2)),
          (v8i16 (INSvi16lane
                   V128:$Vd, VectorIndexH:$idx, V128:$Vs, VectorIndexH:$idx2)
          )>;
def : Pat<(v4i32 (int_aarch64_neon_vcopy_lane
                   (v4i32 V128:$Vd), VectorIndexS:$idx, (v4i32 V128:$Vs),
                   VectorIndexS:$idx2)),
          (v4i32 (INSvi32lane
                   V128:$Vd, VectorIndexS:$idx, V128:$Vs, VectorIndexS:$idx2)
          )>;
def : Pat<(v2i64 (int_aarch64_neon_vcopy_lane
                   (v2i64 V128:$Vd), VectorIndexD:$idx, (v2i64 V128:$Vs),
                   VectorIndexD:$idx2)),
          (v2i64 (INSvi64lane
                   V128:$Vd, VectorIndexD:$idx, V128:$Vs, VectorIndexD:$idx2)
          )>;

multiclass Neon_INS_elt_pattern<ValueType VT128, ValueType VT64,
                                ValueType VTScal, Instruction INS> {
  def : Pat<(VT128 (vector_insert V128:$src,
                        (VTScal (vector_extract (VT128 V128:$Rn), imm:$Immn)),
                        imm:$Immd)),
            (INS V128:$src, imm:$Immd, V128:$Rn, imm:$Immn)>;

  def : Pat<(VT128 (vector_insert V128:$src,
                        (VTScal (vector_extract (VT64 V64:$Rn), imm:$Immn)),
                        imm:$Immd)),
            (INS V128:$src, imm:$Immd,
                 (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), imm:$Immn)>;

  def : Pat<(VT64 (vector_insert V64:$src,
                        (VTScal (vector_extract (VT128 V128:$Rn), imm:$Immn)),
                        imm:$Immd)),
            (EXTRACT_SUBREG (INS (SUBREG_TO_REG (i64 0), V64:$src, dsub),
                                 imm:$Immd, V128:$Rn, imm:$Immn),
                            dsub)>;

  def : Pat<(VT64 (vector_insert V64:$src,
                        (VTScal (vector_extract (VT64 V64:$Rn), imm:$Immn)),
                        imm:$Immd)),
            (EXTRACT_SUBREG
                (INS (SUBREG_TO_REG (i64 0), V64:$src, dsub), imm:$Immd,
                     (SUBREG_TO_REG (i64 0), V64:$Rn, dsub), imm:$Immn),
                dsub)>;
}

defm : Neon_INS_elt_pattern<v8f16, v4f16, f16, INSvi16lane>;
defm : Neon_INS_elt_pattern<v4f32, v2f32, f32, INSvi32lane>;
defm : Neon_INS_elt_pattern<v2f64, v1f64, f64, INSvi64lane>;


// Floating point vector extractions are codegen'd as either a sequence of
// subregister extractions, or a MOV (aka CPY here, alias for DUP) if
// the lane number is anything other than zero.
def : Pat<(vector_extract (v2f64 V128:$Rn), 0),
          (f64 (EXTRACT_SUBREG V128:$Rn, dsub))>;
def : Pat<(vector_extract (v4f32 V128:$Rn), 0),
          (f32 (EXTRACT_SUBREG V128:$Rn, ssub))>;
def : Pat<(vector_extract (v8f16 V128:$Rn), 0),
          (f16 (EXTRACT_SUBREG V128:$Rn, hsub))>;

def : Pat<(vector_extract (v2f64 V128:$Rn), VectorIndexD:$idx),
          (f64 (CPYi64 V128:$Rn, VectorIndexD:$idx))>;
def : Pat<(vector_extract (v4f32 V128:$Rn), VectorIndexS:$idx),
          (f32 (CPYi32 V128:$Rn, VectorIndexS:$idx))>;
def : Pat<(vector_extract (v8f16 V128:$Rn), VectorIndexH:$idx),
          (f16 (CPYi16 V128:$Rn, VectorIndexH:$idx))>;

// All concat_vectors operations are canonicalised to act on i64 vectors for
// AArch64. In the general case we need an instruction, which had just as well be
// INS.
class ConcatPat<ValueType DstTy, ValueType SrcTy>
  : Pat<(DstTy (concat_vectors (SrcTy V64:$Rd), V64:$Rn)),
        (INSvi64lane (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub), 1,
                     (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rn, dsub), 0)>;

def : ConcatPat<v2i64, v1i64>;
def : ConcatPat<v2f64, v1f64>;
def : ConcatPat<v4i32, v2i32>;
def : ConcatPat<v4f32, v2f32>;
def : ConcatPat<v8i16, v4i16>;
def : ConcatPat<v8f16, v4f16>;
def : ConcatPat<v16i8, v8i8>;

// If the high lanes are undef, though, we can just ignore them:
class ConcatUndefPat<ValueType DstTy, ValueType SrcTy>
  : Pat<(DstTy (concat_vectors (SrcTy V64:$Rn), undef)),
        (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rn, dsub)>;

def : ConcatUndefPat<v2i64, v1i64>;
def : ConcatUndefPat<v2f64, v1f64>;
def : ConcatUndefPat<v4i32, v2i32>;
def : ConcatUndefPat<v4f32, v2f32>;
def : ConcatUndefPat<v8i16, v4i16>;
def : ConcatUndefPat<v16i8, v8i8>;

//----------------------------------------------------------------------------
// AdvSIMD across lanes instructions
//----------------------------------------------------------------------------

defm ADDV    : SIMDAcrossLanesBHS<0, 0b11011, "addv">;
defm SMAXV   : SIMDAcrossLanesBHS<0, 0b01010, "smaxv">;
defm SMINV   : SIMDAcrossLanesBHS<0, 0b11010, "sminv">;
defm UMAXV   : SIMDAcrossLanesBHS<1, 0b01010, "umaxv">;
defm UMINV   : SIMDAcrossLanesBHS<1, 0b11010, "uminv">;
defm SADDLV  : SIMDAcrossLanesHSD<0, 0b00011, "saddlv">;
defm UADDLV  : SIMDAcrossLanesHSD<1, 0b00011, "uaddlv">;
defm FMAXNMV : SIMDFPAcrossLanes<0b01100, 0, "fmaxnmv", int_aarch64_neon_fmaxnmv>;
defm FMAXV   : SIMDFPAcrossLanes<0b01111, 0, "fmaxv", int_aarch64_neon_fmaxv>;
defm FMINNMV : SIMDFPAcrossLanes<0b01100, 1, "fminnmv", int_aarch64_neon_fminnmv>;
defm FMINV   : SIMDFPAcrossLanes<0b01111, 1, "fminv", int_aarch64_neon_fminv>;

// Patterns for across-vector intrinsics, that have a node equivalent, that
// returns a vector (with only the low lane defined) instead of a scalar.
// In effect, opNode is the same as (scalar_to_vector (IntNode)).
multiclass SIMDAcrossLanesIntrinsic<string baseOpc,
                                    SDPatternOperator opNode> {
// If a lane instruction caught the vector_extract around opNode, we can
// directly match the latter to the instruction.
def : Pat<(v8i8 (opNode V64:$Rn)),
          (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub)>;
def : Pat<(v16i8 (opNode V128:$Rn)),
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub)>;
def : Pat<(v4i16 (opNode V64:$Rn)),
          (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub)>;
def : Pat<(v8i16 (opNode V128:$Rn)),
          (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub)>;
def : Pat<(v4i32 (opNode V128:$Rn)),
          (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), ssub)>;


// If none did, fallback to the explicit patterns, consuming the vector_extract.
def : Pat<(i32 (vector_extract (insert_subvector undef, (v8i8 (opNode V64:$Rn)),
            (i32 0)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v8i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn),
            bsub), ssub)>;
def : Pat<(i32 (vector_extract (v16i8 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn),
            bsub), ssub)>;
def : Pat<(i32 (vector_extract (insert_subvector undef,
            (v4i16 (opNode V64:$Rn)), (i32 0)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v4i16 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn),
            hsub), ssub)>;
def : Pat<(i32 (vector_extract (v8i16 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn),
            hsub), ssub)>;
def : Pat<(i32 (vector_extract (v4i32 (opNode V128:$Rn)), (i64 0))),
          (EXTRACT_SUBREG (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn),
            ssub), ssub)>;

}

multiclass SIMDAcrossLanesSignedIntrinsic<string baseOpc,
                                          SDPatternOperator opNode>
    : SIMDAcrossLanesIntrinsic<baseOpc, opNode> {
// If there is a sign extension after this intrinsic, consume it as smov already
// performed it
def : Pat<(i32 (sext_inreg (i32 (vector_extract (insert_subvector undef,
            (opNode (v8i8 V64:$Rn)), (i32 0)), (i64 0))), i8)),
          (i32 (SMOVvi8to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub),
            (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract
            (opNode (v16i8 V128:$Rn)), (i64 0))), i8)),
          (i32 (SMOVvi8to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
             (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub),
            (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract (insert_subvector undef,
            (opNode (v4i16 V64:$Rn)), (i32 0)), (i64 0))), i16)),
          (i32 (SMOVvi16to32
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub),
           (i64 0)))>;
def : Pat<(i32 (sext_inreg (i32 (vector_extract
            (opNode (v8i16 V128:$Rn)), (i64 0))), i16)),
          (i32 (SMOVvi16to32
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
             (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub),
            (i64 0)))>;
}

multiclass SIMDAcrossLanesUnsignedIntrinsic<string baseOpc,
                                            SDPatternOperator opNode>
    : SIMDAcrossLanesIntrinsic<baseOpc, opNode> {
// If there is a masking operation keeping only what has been actually
// generated, consume it.
def : Pat<(i32 (and (i32 (vector_extract (insert_subvector undef,
            (opNode (v8i8 V64:$Rn)), (i32 0)), (i64 0))), maski8_or_more)),
      (i32 (EXTRACT_SUBREG
        (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
          (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), bsub),
        ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (opNode (v16i8 V128:$Rn)), (i64 0))),
            maski8_or_more)),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), bsub),
          ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (insert_subvector undef,
            (opNode (v4i16 V64:$Rn)), (i32 0)), (i64 0))), maski16_or_more)),
          (i32 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), hsub),
            ssub))>;
def : Pat<(i32 (and (i32 (vector_extract (opNode (v8i16 V128:$Rn)), (i64 0))),
            maski16_or_more)),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), hsub),
          ssub))>;
}

defm : SIMDAcrossLanesSignedIntrinsic<"ADDV",  AArch64saddv>;
// vaddv_[su]32 is special; -> ADDP Vd.2S,Vn.2S,Vm.2S; return Vd.s[0];Vn==Vm
def : Pat<(v2i32 (AArch64saddv (v2i32 V64:$Rn))),
          (ADDPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"ADDV", AArch64uaddv>;
// vaddv_[su]32 is special; -> ADDP Vd.2S,Vn.2S,Vm.2S; return Vd.s[0];Vn==Vm
def : Pat<(v2i32 (AArch64uaddv (v2i32 V64:$Rn))),
          (ADDPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesSignedIntrinsic<"SMAXV", AArch64smaxv>;
def : Pat<(v2i32 (AArch64smaxv (v2i32 V64:$Rn))),
          (SMAXPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesSignedIntrinsic<"SMINV", AArch64sminv>;
def : Pat<(v2i32 (AArch64sminv (v2i32 V64:$Rn))),
          (SMINPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"UMAXV", AArch64umaxv>;
def : Pat<(v2i32 (AArch64umaxv (v2i32 V64:$Rn))),
          (UMAXPv2i32 V64:$Rn, V64:$Rn)>;

defm : SIMDAcrossLanesUnsignedIntrinsic<"UMINV", AArch64uminv>;
def : Pat<(v2i32 (AArch64uminv (v2i32 V64:$Rn))),
          (UMINPv2i32 V64:$Rn, V64:$Rn)>;

multiclass SIMDAcrossLanesSignedLongIntrinsic<string baseOpc, Intrinsic intOp> {
  def : Pat<(i32 (intOp (v8i8 V64:$Rn))),
        (i32 (SMOVvi16to32
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), hsub),
          (i64 0)))>;
def : Pat<(i32 (intOp (v16i8 V128:$Rn))),
        (i32 (SMOVvi16to32
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), hsub),
          (i64 0)))>;

def : Pat<(i32 (intOp (v4i16 V64:$Rn))),
          (i32 (EXTRACT_SUBREG
           (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), ssub),
           ssub))>;
def : Pat<(i32 (intOp (v8i16 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), ssub),
          ssub))>;

def : Pat<(i64 (intOp (v4i32 V128:$Rn))),
        (i64 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
           (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), dsub),
          dsub))>;
}

multiclass SIMDAcrossLanesUnsignedLongIntrinsic<string baseOpc,
                                                Intrinsic intOp> {
  def : Pat<(i32 (intOp (v8i8 V64:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i8v")) V64:$Rn), hsub),
          ssub))>;
def : Pat<(i32 (intOp (v16i8 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v16i8v")) V128:$Rn), hsub),
          ssub))>;

def : Pat<(i32 (intOp (v4i16 V64:$Rn))),
          (i32 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (!cast<Instruction>(!strconcat(baseOpc, "v4i16v")) V64:$Rn), ssub),
            ssub))>;
def : Pat<(i32 (intOp (v8i16 V128:$Rn))),
        (i32 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v8i16v")) V128:$Rn), ssub),
          ssub))>;

def : Pat<(i64 (intOp (v4i32 V128:$Rn))),
        (i64 (EXTRACT_SUBREG
          (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
            (!cast<Instruction>(!strconcat(baseOpc, "v4i32v")) V128:$Rn), dsub),
          dsub))>;
}

defm : SIMDAcrossLanesSignedLongIntrinsic<"SADDLV", int_aarch64_neon_saddlv>;
defm : SIMDAcrossLanesUnsignedLongIntrinsic<"UADDLV", int_aarch64_neon_uaddlv>;

// The vaddlv_s32 intrinsic gets mapped to SADDLP.
def : Pat<(i64 (int_aarch64_neon_saddlv (v2i32 V64:$Rn))),
          (i64 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (SADDLPv2i32_v1i64 V64:$Rn), dsub),
            dsub))>;
// The vaddlv_u32 intrinsic gets mapped to UADDLP.
def : Pat<(i64 (int_aarch64_neon_uaddlv (v2i32 V64:$Rn))),
          (i64 (EXTRACT_SUBREG
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)),
              (UADDLPv2i32_v1i64 V64:$Rn), dsub),
            dsub))>;

//------------------------------------------------------------------------------
// AdvSIMD modified immediate instructions
//------------------------------------------------------------------------------

// AdvSIMD BIC
defm BIC : SIMDModifiedImmVectorShiftTied<1, 0b11, 0b01, "bic", AArch64bici>;
// AdvSIMD ORR
defm ORR : SIMDModifiedImmVectorShiftTied<0, 0b11, 0b01, "orr", AArch64orri>;

def : InstAlias<"bic $Vd.4h, $imm", (BICv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.8h, $imm", (BICv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.2s, $imm", (BICv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic $Vd.4s, $imm", (BICv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"bic.4h $Vd, $imm", (BICv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic.8h $Vd, $imm", (BICv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"bic.2s $Vd, $imm", (BICv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"bic.4s $Vd, $imm", (BICv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"orr $Vd.4h, $imm", (ORRv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.8h, $imm", (ORRv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.2s, $imm", (ORRv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr $Vd.4s, $imm", (ORRv4i32 V128:$Vd, imm0_255:$imm, 0)>;

def : InstAlias<"orr.4h $Vd, $imm", (ORRv4i16 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr.8h $Vd, $imm", (ORRv8i16 V128:$Vd, imm0_255:$imm, 0)>;
def : InstAlias<"orr.2s $Vd, $imm", (ORRv2i32 V64:$Vd,  imm0_255:$imm, 0)>;
def : InstAlias<"orr.4s $Vd, $imm", (ORRv4i32 V128:$Vd, imm0_255:$imm, 0)>;

// AdvSIMD FMOV
def FMOVv2f64_ns : SIMDModifiedImmVectorNoShift<1, 1, 0, 0b1111, V128, fpimm8,
                                              "fmov", ".2d",
                       [(set (v2f64 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv2f32_ns : SIMDModifiedImmVectorNoShift<0, 0, 0, 0b1111, V64,  fpimm8,
                                              "fmov", ".2s",
                       [(set (v2f32 V64:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv4f32_ns : SIMDModifiedImmVectorNoShift<1, 0, 0, 0b1111, V128, fpimm8,
                                              "fmov", ".4s",
                       [(set (v4f32 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
let Predicates = [HasNEON, HasFullFP16] in {
def FMOVv4f16_ns : SIMDModifiedImmVectorNoShift<0, 0, 1, 0b1111, V64,  fpimm8,
                                              "fmov", ".4h",
                       [(set (v4f16 V64:$Rd), (AArch64fmov imm0_255:$imm8))]>;
def FMOVv8f16_ns : SIMDModifiedImmVectorNoShift<1, 0, 1, 0b1111, V128, fpimm8,
                                              "fmov", ".8h",
                       [(set (v8f16 V128:$Rd), (AArch64fmov imm0_255:$imm8))]>;
} // Predicates = [HasNEON, HasFullFP16]

// AdvSIMD MOVI

// EDIT byte mask: scalar
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def MOVID      : SIMDModifiedImmScalarNoShift<0, 1, 0b1110, "movi",
                    [(set FPR64:$Rd, simdimmtype10:$imm8)]>;
// The movi_edit node has the immediate value already encoded, so we use
// a plain imm0_255 here.
def : Pat<(f64 (AArch64movi_edit imm0_255:$shift)),
          (MOVID imm0_255:$shift)>;

// EDIT byte mask: 2d

// The movi_edit node has the immediate value already encoded, so we use
// a plain imm0_255 in the pattern
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def MOVIv2d_ns   : SIMDModifiedImmVectorNoShift<1, 1, 0, 0b1110, V128,
                                                simdimmtype10,
                                                "movi", ".2d",
                   [(set (v2i64 V128:$Rd), (AArch64movi_edit imm0_255:$imm8))]>;

def : Pat<(v2i64 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v4i32 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v8i16 immAllZerosV), (MOVIv2d_ns (i32 0))>;
def : Pat<(v16i8 immAllZerosV), (MOVIv2d_ns (i32 0))>;

def : Pat<(v2i64 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v4i32 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v8i16 immAllOnesV), (MOVIv2d_ns (i32 255))>;
def : Pat<(v16i8 immAllOnesV), (MOVIv2d_ns (i32 255))>;

// Set 64-bit vectors to all 0/1 by extracting from a 128-bit register as the
// extract is free and this gives better MachineCSE results.
def : Pat<(v1i64 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v2i32 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v4i16 immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;
def : Pat<(v8i8  immAllZerosV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 0)), dsub)>;

def : Pat<(v1i64 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v2i32 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v4i16 immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;
def : Pat<(v8i8  immAllOnesV), (EXTRACT_SUBREG (MOVIv2d_ns (i32 255)), dsub)>;

// EDIT per word & halfword: 2s, 4h, 4s, & 8h
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
defm MOVI      : SIMDModifiedImmVectorShift<0, 0b10, 0b00, "movi">;

def : InstAlias<"movi $Vd.4h, $imm", (MOVIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.8h, $imm", (MOVIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.2s, $imm", (MOVIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi $Vd.4s, $imm", (MOVIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : InstAlias<"movi.4h $Vd, $imm", (MOVIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.8h $Vd, $imm", (MOVIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.2s $Vd, $imm", (MOVIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"movi.4s $Vd, $imm", (MOVIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : Pat<(v2i32 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv2i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i32 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv4i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i16 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv4i16 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v8i16 (AArch64movi_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MOVIv8i16 imm0_255:$imm8, imm:$shift)>;

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
// EDIT per word: 2s & 4s with MSL shifter
def MOVIv2s_msl  : SIMDModifiedImmMoveMSL<0, 0, {1,1,0,?}, V64, "movi", ".2s",
                      [(set (v2i32 V64:$Rd),
                            (AArch64movi_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
def MOVIv4s_msl  : SIMDModifiedImmMoveMSL<1, 0, {1,1,0,?}, V128, "movi", ".4s",
                      [(set (v4i32 V128:$Rd),
                            (AArch64movi_msl imm0_255:$imm8, (i32 imm:$shift)))]>;

// Per byte: 8b & 16b
def MOVIv8b_ns   : SIMDModifiedImmVectorNoShift<0, 0, 0, 0b1110, V64,  imm0_255,
                                                 "movi", ".8b",
                       [(set (v8i8 V64:$Rd), (AArch64movi imm0_255:$imm8))]>;

def MOVIv16b_ns  : SIMDModifiedImmVectorNoShift<1, 0, 0, 0b1110, V128, imm0_255,
                                                 "movi", ".16b",
                       [(set (v16i8 V128:$Rd), (AArch64movi imm0_255:$imm8))]>;
}

// AdvSIMD MVNI

// EDIT per word & halfword: 2s, 4h, 4s, & 8h
let isReMaterializable = 1, isAsCheapAsAMove = 1 in
defm MVNI      : SIMDModifiedImmVectorShift<1, 0b10, 0b00, "mvni">;

def : InstAlias<"mvni $Vd.4h, $imm", (MVNIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.8h, $imm", (MVNIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.2s, $imm", (MVNIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni $Vd.4s, $imm", (MVNIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : InstAlias<"mvni.4h $Vd, $imm", (MVNIv4i16 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.8h $Vd, $imm", (MVNIv8i16 V128:$Vd, imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.2s $Vd, $imm", (MVNIv2i32 V64:$Vd,  imm0_255:$imm, 0), 0>;
def : InstAlias<"mvni.4s $Vd, $imm", (MVNIv4i32 V128:$Vd, imm0_255:$imm, 0), 0>;

def : Pat<(v2i32 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv2i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i32 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv4i32 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v4i16 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv4i16 imm0_255:$imm8, imm:$shift)>;
def : Pat<(v8i16 (AArch64mvni_shift imm0_255:$imm8, (i32 imm:$shift))),
          (MVNIv8i16 imm0_255:$imm8, imm:$shift)>;

// EDIT per word: 2s & 4s with MSL shifter
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def MVNIv2s_msl   : SIMDModifiedImmMoveMSL<0, 1, {1,1,0,?}, V64, "mvni", ".2s",
                      [(set (v2i32 V64:$Rd),
                            (AArch64mvni_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
def MVNIv4s_msl   : SIMDModifiedImmMoveMSL<1, 1, {1,1,0,?}, V128, "mvni", ".4s",
                      [(set (v4i32 V128:$Rd),
                            (AArch64mvni_msl imm0_255:$imm8, (i32 imm:$shift)))]>;
}

//----------------------------------------------------------------------------
// AdvSIMD indexed element
//----------------------------------------------------------------------------

let hasSideEffects = 0 in {
  defm FMLA  : SIMDFPIndexedTied<0, 0b0001, "fmla">;
  defm FMLS  : SIMDFPIndexedTied<0, 0b0101, "fmls">;
}

// NOTE: Operands are reordered in the FMLA/FMLS PatFrags because the
// instruction expects the addend first, while the intrinsic expects it last.

// On the other hand, there are quite a few valid combinatorial options due to
// the commutativity of multiplication and the fact that (-x) * y = x * (-y).
defm : SIMDFPIndexedTiedPatterns<"FMLA",
           TriOpFrag<(fma node:$RHS, node:$MHS, node:$LHS)>>;
defm : SIMDFPIndexedTiedPatterns<"FMLA",
           TriOpFrag<(fma node:$MHS, node:$RHS, node:$LHS)>>;

defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(fma node:$MHS, (fneg node:$RHS), node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(fma node:$RHS, (fneg node:$MHS), node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(fma (fneg node:$RHS), node:$MHS, node:$LHS)> >;
defm : SIMDFPIndexedTiedPatterns<"FMLS",
           TriOpFrag<(fma (fneg node:$MHS), node:$RHS, node:$LHS)> >;

multiclass FMLSIndexedAfterNegPatterns<SDPatternOperator OpNode> {
  // 3 variants for the .2s version: DUPLANE from 128-bit, DUPLANE from 64-bit
  // and DUP scalar.
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64duplane32 (v4f32 (fneg V128:$Rm)),
                                           VectorIndexS:$idx))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (v2f32 (AArch64duplane32
                                      (v4f32 (insert_subvector undef,
                                                 (v2f32 (fneg V64:$Rm)),
                                                 (i32 0))),
                                      VectorIndexS:$idx)))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn,
                               (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                               VectorIndexS:$idx)>;
  def : Pat<(v2f32 (OpNode (v2f32 V64:$Rd), (v2f32 V64:$Rn),
                           (AArch64dup (f32 (fneg FPR32Op:$Rm))))),
            (FMLSv2i32_indexed V64:$Rd, V64:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;

  // 3 variants for the .4s version: DUPLANE from 128-bit, DUPLANE from 64-bit
  // and DUP scalar.
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64duplane32 (v4f32 (fneg V128:$Rm)),
                                           VectorIndexS:$idx))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn, V128:$Rm,
                               VectorIndexS:$idx)>;
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (v4f32 (AArch64duplane32
                                      (v4f32 (insert_subvector undef,
                                                 (v2f32 (fneg V64:$Rm)),
                                                 (i32 0))),
                                      VectorIndexS:$idx)))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn,
                               (SUBREG_TO_REG (i32 0), V64:$Rm, dsub),
                               VectorIndexS:$idx)>;
  def : Pat<(v4f32 (OpNode (v4f32 V128:$Rd), (v4f32 V128:$Rn),
                           (AArch64dup (f32 (fneg FPR32Op:$Rm))))),
            (FMLSv4i32_indexed V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR32Op:$Rm, ssub), (i64 0))>;

  // 2 variants for the .2d version: DUPLANE from 128-bit, and DUP scalar
  // (DUPLANE from 64-bit would be trivial).
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64duplane64 (v2f64 (fneg V128:$Rm)),
                                           VectorIndexD:$idx))),
            (FMLSv2i64_indexed
                V128:$Rd, V128:$Rn, V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(v2f64 (OpNode (v2f64 V128:$Rd), (v2f64 V128:$Rn),
                           (AArch64dup (f64 (fneg FPR64Op:$Rm))))),
            (FMLSv2i64_indexed V128:$Rd, V128:$Rn,
                (SUBREG_TO_REG (i32 0), FPR64Op:$Rm, dsub), (i64 0))>;

  // 2 variants for 32-bit scalar version: extract from .2s or from .4s
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v4f32 (fneg V128:$Rm)),
                                         VectorIndexS:$idx))),
            (FMLSv1i32_indexed FPR32:$Rd, FPR32:$Rn,
                V128:$Rm, VectorIndexS:$idx)>;
  def : Pat<(f32 (OpNode (f32 FPR32:$Rd), (f32 FPR32:$Rn),
                         (vector_extract (v4f32 (insert_subvector undef,
                                                    (v2f32 (fneg V64:$Rm)),
                                                    (i32 0))),
                                         VectorIndexS:$idx))),
            (FMLSv1i32_indexed FPR32:$Rd, FPR32:$Rn,
                (SUBREG_TO_REG (i32 0), V64:$Rm, dsub), VectorIndexS:$idx)>;

  // 1 variant for 64-bit scalar version: extract from .1d or from .2d
  def : Pat<(f64 (OpNode (f64 FPR64:$Rd), (f64 FPR64:$Rn),
                         (vector_extract (v2f64 (fneg V128:$Rm)),
                                         VectorIndexS:$idx))),
            (FMLSv1i64_indexed FPR64:$Rd, FPR64:$Rn,
                V128:$Rm, VectorIndexS:$idx)>;
}

defm : FMLSIndexedAfterNegPatterns<
           TriOpFrag<(fma node:$RHS, node:$MHS, node:$LHS)> >;
defm : FMLSIndexedAfterNegPatterns<
           TriOpFrag<(fma node:$MHS, node:$RHS, node:$LHS)> >;

defm FMULX : SIMDFPIndexed<1, 0b1001, "fmulx", int_aarch64_neon_fmulx>;
defm FMUL  : SIMDFPIndexed<0, 0b1001, "fmul", fmul>;

def : Pat<(v2f32 (fmul V64:$Rn, (AArch64dup (f32 FPR32:$Rm)))),
          (FMULv2i32_indexed V64:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rm, ssub),
            (i64 0))>;
def : Pat<(v4f32 (fmul V128:$Rn, (AArch64dup (f32 FPR32:$Rm)))),
          (FMULv4i32_indexed V128:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR32:$Rm, ssub),
            (i64 0))>;
def : Pat<(v2f64 (fmul V128:$Rn, (AArch64dup (f64 FPR64:$Rm)))),
          (FMULv2i64_indexed V128:$Rn,
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$Rm, dsub),
            (i64 0))>;

defm SQDMULH : SIMDIndexedHS<0, 0b1100, "sqdmulh", int_aarch64_neon_sqdmulh>;
defm SQRDMULH : SIMDIndexedHS<0, 0b1101, "sqrdmulh", int_aarch64_neon_sqrdmulh>;
defm MLA   : SIMDVectorIndexedHSTied<1, 0b0000, "mla",
              TriOpFrag<(add node:$LHS, (mul node:$MHS, node:$RHS))>>;
defm MLS   : SIMDVectorIndexedHSTied<1, 0b0100, "mls",
              TriOpFrag<(sub node:$LHS, (mul node:$MHS, node:$RHS))>>;
defm MUL   : SIMDVectorIndexedHS<0, 0b1000, "mul", mul>;
defm SMLAL : SIMDVectorIndexedLongSDTied<0, 0b0010, "smlal",
    TriOpFrag<(add node:$LHS, (int_aarch64_neon_smull node:$MHS, node:$RHS))>>;
defm SMLSL : SIMDVectorIndexedLongSDTied<0, 0b0110, "smlsl",
    TriOpFrag<(sub node:$LHS, (int_aarch64_neon_smull node:$MHS, node:$RHS))>>;
defm SMULL : SIMDVectorIndexedLongSD<0, 0b1010, "smull",
                int_aarch64_neon_smull>;
defm SQDMLAL : SIMDIndexedLongSQDMLXSDTied<0, 0b0011, "sqdmlal",
                                           int_aarch64_neon_sqadd>;
defm SQDMLSL : SIMDIndexedLongSQDMLXSDTied<0, 0b0111, "sqdmlsl",
                                           int_aarch64_neon_sqsub>;
defm SQRDMLAH : SIMDIndexedSQRDMLxHSDTied<1, 0b1101, "sqrdmlah",
                                          int_aarch64_neon_sqadd>;
defm SQRDMLSH : SIMDIndexedSQRDMLxHSDTied<1, 0b1111, "sqrdmlsh",
                                          int_aarch64_neon_sqsub>;
defm SQDMULL : SIMDIndexedLongSD<0, 0b1011, "sqdmull", int_aarch64_neon_sqdmull>;
defm UMLAL   : SIMDVectorIndexedLongSDTied<1, 0b0010, "umlal",
    TriOpFrag<(add node:$LHS, (int_aarch64_neon_umull node:$MHS, node:$RHS))>>;
defm UMLSL   : SIMDVectorIndexedLongSDTied<1, 0b0110, "umlsl",
    TriOpFrag<(sub node:$LHS, (int_aarch64_neon_umull node:$MHS, node:$RHS))>>;
defm UMULL   : SIMDVectorIndexedLongSD<1, 0b1010, "umull",
                int_aarch64_neon_umull>;

// A scalar sqdmull with the second operand being a vector lane can be
// handled directly with the indexed instruction encoding.
def : Pat<(int_aarch64_neon_sqdmulls_scalar (i32 FPR32:$Rn),
                                          (vector_extract (v4i32 V128:$Vm),
                                                           VectorIndexS:$idx)),
          (SQDMULLv1i64_indexed FPR32:$Rn, V128:$Vm, VectorIndexS:$idx)>;

//----------------------------------------------------------------------------
// AdvSIMD scalar shift instructions
//----------------------------------------------------------------------------
defm FCVTZS : SIMDFPScalarRShift<0, 0b11111, "fcvtzs">;
defm FCVTZU : SIMDFPScalarRShift<1, 0b11111, "fcvtzu">;
defm SCVTF  : SIMDFPScalarRShift<0, 0b11100, "scvtf">;
defm UCVTF  : SIMDFPScalarRShift<1, 0b11100, "ucvtf">;
// Codegen patterns for the above. We don't put these directly on the
// instructions because TableGen's type inference can't handle the truth.
// Having the same base pattern for fp <--> int totally freaks it out.
def : Pat<(int_aarch64_neon_vcvtfp2fxs FPR32:$Rn, vecshiftR32:$imm),
          (FCVTZSs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfp2fxu FPR32:$Rn, vecshiftR32:$imm),
          (FCVTZUs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxs (f64 FPR64:$Rn), vecshiftR64:$imm)),
          (FCVTZSd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxu (f64 FPR64:$Rn), vecshiftR64:$imm)),
          (FCVTZUd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1i64 (int_aarch64_neon_vcvtfp2fxs (v1f64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (FCVTZSd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1i64 (int_aarch64_neon_vcvtfp2fxu (v1f64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (FCVTZUd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfxu2fp FPR32:$Rn, vecshiftR32:$imm),
          (UCVTFs FPR32:$Rn, vecshiftR32:$imm)>;
def : Pat<(f64 (int_aarch64_neon_vcvtfxu2fp (i64 FPR64:$Rn), vecshiftR64:$imm)),
          (UCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1f64 (int_aarch64_neon_vcvtfxs2fp (v1i64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (SCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(f64 (int_aarch64_neon_vcvtfxs2fp (i64 FPR64:$Rn), vecshiftR64:$imm)),
          (SCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(v1f64 (int_aarch64_neon_vcvtfxu2fp (v1i64 FPR64:$Rn),
                                            vecshiftR64:$imm)),
          (UCVTFd FPR64:$Rn, vecshiftR64:$imm)>;
def : Pat<(int_aarch64_neon_vcvtfxs2fp FPR32:$Rn, vecshiftR32:$imm),
          (SCVTFs FPR32:$Rn, vecshiftR32:$imm)>;

// Patterns for FP16 Instrinsics - requires reg copy to/from as i16s not supported.

def : Pat<(f16 (int_aarch64_neon_vcvtfxs2fp (i32 (sext_inreg FPR32:$Rn, i16)), vecshiftR16:$imm)),
          (SCVTFh (EXTRACT_SUBREG FPR32:$Rn, hsub), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxs2fp (i32 FPR32:$Rn), vecshiftR16:$imm)),
          (SCVTFh (EXTRACT_SUBREG FPR32:$Rn, hsub), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp
            (and FPR32:$Rn, (i32 65535)),
            vecshiftR16:$imm)),
          (UCVTFh (EXTRACT_SUBREG FPR32:$Rn, hsub), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp FPR32:$Rn, vecshiftR16:$imm)),
          (UCVTFh (EXTRACT_SUBREG FPR32:$Rn, hsub), vecshiftR16:$imm)>;
def : Pat<(f16 (int_aarch64_neon_vcvtfxu2fp (i64 FPR64:$Rn), vecshiftR16:$imm)),
          (UCVTFh (EXTRACT_SUBREG FPR64:$Rn, hsub), vecshiftR16:$imm)>;
def : Pat<(i32 (int_aarch64_neon_vcvtfp2fxs (f16 FPR16:$Rn), vecshiftR32:$imm)),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FCVTZSh FPR16:$Rn, vecshiftR32:$imm),
            hsub))>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxs (f16 FPR16:$Rn), vecshiftR64:$imm)),
          (i64 (INSERT_SUBREG
            (i64 (IMPLICIT_DEF)),
            (FCVTZSh FPR16:$Rn, vecshiftR64:$imm),
            hsub))>;
def : Pat<(i32 (int_aarch64_neon_vcvtfp2fxu (f16 FPR16:$Rn), vecshiftR32:$imm)),
          (i32 (INSERT_SUBREG
            (i32 (IMPLICIT_DEF)),
            (FCVTZUh FPR16:$Rn, vecshiftR32:$imm),
            hsub))>;
def : Pat<(i64 (int_aarch64_neon_vcvtfp2fxu (f16 FPR16:$Rn), vecshiftR64:$imm)),
          (i64 (INSERT_SUBREG
            (i64 (IMPLICIT_DEF)),
            (FCVTZUh FPR16:$Rn, vecshiftR64:$imm),
            hsub))>;

defm SHL      : SIMDScalarLShiftD<   0, 0b01010, "shl", AArch64vshl>;
defm SLI      : SIMDScalarLShiftDTied<1, 0b01010, "sli">;
defm SQRSHRN  : SIMDScalarRShiftBHS< 0, 0b10011, "sqrshrn",
                                     int_aarch64_neon_sqrshrn>;
defm SQRSHRUN : SIMDScalarRShiftBHS< 1, 0b10001, "sqrshrun",
                                     int_aarch64_neon_sqrshrun>;
defm SQSHLU   : SIMDScalarLShiftBHSD<1, 0b01100, "sqshlu", AArch64sqshlui>;
defm SQSHL    : SIMDScalarLShiftBHSD<0, 0b01110, "sqshl", AArch64sqshli>;
defm SQSHRN   : SIMDScalarRShiftBHS< 0, 0b10010, "sqshrn",
                                     int_aarch64_neon_sqshrn>;
defm SQSHRUN  : SIMDScalarRShiftBHS< 1, 0b10000, "sqshrun",
                                     int_aarch64_neon_sqshrun>;
defm SRI      : SIMDScalarRShiftDTied<   1, 0b01000, "sri">;
defm SRSHR    : SIMDScalarRShiftD<   0, 0b00100, "srshr", AArch64srshri>;
defm SRSRA    : SIMDScalarRShiftDTied<   0, 0b00110, "srsra",
    TriOpFrag<(add node:$LHS,
                   (AArch64srshri node:$MHS, node:$RHS))>>;
defm SSHR     : SIMDScalarRShiftD<   0, 0b00000, "sshr", AArch64vashr>;
defm SSRA     : SIMDScalarRShiftDTied<   0, 0b00010, "ssra",
    TriOpFrag<(add node:$LHS,
                   (AArch64vashr node:$MHS, node:$RHS))>>;
defm UQRSHRN  : SIMDScalarRShiftBHS< 1, 0b10011, "uqrshrn",
                                     int_aarch64_neon_uqrshrn>;
defm UQSHL    : SIMDScalarLShiftBHSD<1, 0b01110, "uqshl", AArch64uqshli>;
defm UQSHRN   : SIMDScalarRShiftBHS< 1, 0b10010, "uqshrn",
                                     int_aarch64_neon_uqshrn>;
defm URSHR    : SIMDScalarRShiftD<   1, 0b00100, "urshr", AArch64urshri>;
defm URSRA    : SIMDScalarRShiftDTied<   1, 0b00110, "ursra",
    TriOpFrag<(add node:$LHS,
                   (AArch64urshri node:$MHS, node:$RHS))>>;
defm USHR     : SIMDScalarRShiftD<   1, 0b00000, "ushr", AArch64vlshr>;
defm USRA     : SIMDScalarRShiftDTied<   1, 0b00010, "usra",
    TriOpFrag<(add node:$LHS,
                   (AArch64vlshr node:$MHS, node:$RHS))>>;

//----------------------------------------------------------------------------
// AdvSIMD vector shift instructions
//----------------------------------------------------------------------------
defm FCVTZS:SIMDVectorRShiftSD<0, 0b11111, "fcvtzs", int_aarch64_neon_vcvtfp2fxs>;
defm FCVTZU:SIMDVectorRShiftSD<1, 0b11111, "fcvtzu", int_aarch64_neon_vcvtfp2fxu>;
defm SCVTF: SIMDVectorRShiftToFP<0, 0b11100, "scvtf",
                                   int_aarch64_neon_vcvtfxs2fp>;
defm RSHRN   : SIMDVectorRShiftNarrowBHS<0, 0b10001, "rshrn",
                                         int_aarch64_neon_rshrn>;
defm SHL     : SIMDVectorLShiftBHSD<0, 0b01010, "shl", AArch64vshl>;
defm SHRN    : SIMDVectorRShiftNarrowBHS<0, 0b10000, "shrn",
                          BinOpFrag<(trunc (AArch64vashr node:$LHS, node:$RHS))>>;
defm SLI     : SIMDVectorLShiftBHSDTied<1, 0b01010, "sli", int_aarch64_neon_vsli>;
def : Pat<(v1i64 (int_aarch64_neon_vsli (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn),
                                      (i32 vecshiftL64:$imm))),
          (SLId FPR64:$Rd, FPR64:$Rn, vecshiftL64:$imm)>;
defm SQRSHRN : SIMDVectorRShiftNarrowBHS<0, 0b10011, "sqrshrn",
                                         int_aarch64_neon_sqrshrn>;
defm SQRSHRUN: SIMDVectorRShiftNarrowBHS<1, 0b10001, "sqrshrun",
                                         int_aarch64_neon_sqrshrun>;
defm SQSHLU : SIMDVectorLShiftBHSD<1, 0b01100, "sqshlu", AArch64sqshlui>;
defm SQSHL  : SIMDVectorLShiftBHSD<0, 0b01110, "sqshl", AArch64sqshli>;
defm SQSHRN  : SIMDVectorRShiftNarrowBHS<0, 0b10010, "sqshrn",
                                         int_aarch64_neon_sqshrn>;
defm SQSHRUN : SIMDVectorRShiftNarrowBHS<1, 0b10000, "sqshrun",
                                         int_aarch64_neon_sqshrun>;
defm SRI     : SIMDVectorRShiftBHSDTied<1, 0b01000, "sri", int_aarch64_neon_vsri>;
def : Pat<(v1i64 (int_aarch64_neon_vsri (v1i64 FPR64:$Rd), (v1i64 FPR64:$Rn),
                                      (i32 vecshiftR64:$imm))),
          (SRId FPR64:$Rd, FPR64:$Rn, vecshiftR64:$imm)>;
defm SRSHR   : SIMDVectorRShiftBHSD<0, 0b00100, "srshr", AArch64srshri>;
defm SRSRA   : SIMDVectorRShiftBHSDTied<0, 0b00110, "srsra",
                 TriOpFrag<(add node:$LHS,
                                (AArch64srshri node:$MHS, node:$RHS))> >;
defm SSHLL   : SIMDVectorLShiftLongBHSD<0, 0b10100, "sshll",
                BinOpFrag<(AArch64vshl (sext node:$LHS), node:$RHS)>>;

defm SSHR    : SIMDVectorRShiftBHSD<0, 0b00000, "sshr", AArch64vashr>;
defm SSRA    : SIMDVectorRShiftBHSDTied<0, 0b00010, "ssra",
                TriOpFrag<(add node:$LHS, (AArch64vashr node:$MHS, node:$RHS))>>;
defm UCVTF   : SIMDVectorRShiftToFP<1, 0b11100, "ucvtf",
                        int_aarch64_neon_vcvtfxu2fp>;
defm UQRSHRN : SIMDVectorRShiftNarrowBHS<1, 0b10011, "uqrshrn",
                                         int_aarch64_neon_uqrshrn>;
defm UQSHL   : SIMDVectorLShiftBHSD<1, 0b01110, "uqshl", AArch64uqshli>;
defm UQSHRN  : SIMDVectorRShiftNarrowBHS<1, 0b10010, "uqshrn",
                                         int_aarch64_neon_uqshrn>;
defm URSHR   : SIMDVectorRShiftBHSD<1, 0b00100, "urshr", AArch64urshri>;
defm URSRA   : SIMDVectorRShiftBHSDTied<1, 0b00110, "ursra",
                TriOpFrag<(add node:$LHS,
                               (AArch64urshri node:$MHS, node:$RHS))> >;
defm USHLL   : SIMDVectorLShiftLongBHSD<1, 0b10100, "ushll",
                BinOpFrag<(AArch64vshl (zext node:$LHS), node:$RHS)>>;
defm USHR    : SIMDVectorRShiftBHSD<1, 0b00000, "ushr", AArch64vlshr>;
defm USRA    : SIMDVectorRShiftBHSDTied<1, 0b00010, "usra",
                TriOpFrag<(add node:$LHS, (AArch64vlshr node:$MHS, node:$RHS))> >;

// SHRN patterns for when a logical right shift was used instead of arithmetic
// (the immediate guarantees no sign bits actually end up in the result so it
// doesn't matter).
def : Pat<(v8i8 (trunc (AArch64vlshr (v8i16 V128:$Rn), vecshiftR16Narrow:$imm))),
          (SHRNv8i8_shift V128:$Rn, vecshiftR16Narrow:$imm)>;
def : Pat<(v4i16 (trunc (AArch64vlshr (v4i32 V128:$Rn), vecshiftR32Narrow:$imm))),
          (SHRNv4i16_shift V128:$Rn, vecshiftR32Narrow:$imm)>;
def : Pat<(v2i32 (trunc (AArch64vlshr (v2i64 V128:$Rn), vecshiftR64Narrow:$imm))),
          (SHRNv2i32_shift V128:$Rn, vecshiftR64Narrow:$imm)>;

def : Pat<(v16i8 (concat_vectors (v8i8 V64:$Rd),
                                 (trunc (AArch64vlshr (v8i16 V128:$Rn),
                                                    vecshiftR16Narrow:$imm)))),
          (SHRNv16i8_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR16Narrow:$imm)>;
def : Pat<(v8i16 (concat_vectors (v4i16 V64:$Rd),
                                 (trunc (AArch64vlshr (v4i32 V128:$Rn),
                                                    vecshiftR32Narrow:$imm)))),
          (SHRNv8i16_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR32Narrow:$imm)>;
def : Pat<(v4i32 (concat_vectors (v2i32 V64:$Rd),
                                 (trunc (AArch64vlshr (v2i64 V128:$Rn),
                                                    vecshiftR64Narrow:$imm)))),
          (SHRNv4i32_shift (INSERT_SUBREG (IMPLICIT_DEF), V64:$Rd, dsub),
                           V128:$Rn, vecshiftR32Narrow:$imm)>;

// Vector sign and zero extensions are implemented with SSHLL and USSHLL.
// Anyexts are implemented as zexts.
def : Pat<(v8i16 (sext   (v8i8 V64:$Rn))),  (SSHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v8i16 (zext   (v8i8 V64:$Rn))),  (USHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v8i16 (anyext (v8i8 V64:$Rn))),  (USHLLv8i8_shift  V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (sext   (v4i16 V64:$Rn))), (SSHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (zext   (v4i16 V64:$Rn))), (USHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v4i32 (anyext (v4i16 V64:$Rn))), (USHLLv4i16_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (sext   (v2i32 V64:$Rn))), (SSHLLv2i32_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (zext   (v2i32 V64:$Rn))), (USHLLv2i32_shift V64:$Rn, (i32 0))>;
def : Pat<(v2i64 (anyext (v2i32 V64:$Rn))), (USHLLv2i32_shift V64:$Rn, (i32 0))>;
// Also match an extend from the upper half of a 128 bit source register.
def : Pat<(v8i16 (anyext (v8i8 (extract_subvector V128:$Rn, (i64 8)) ))),
          (USHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v8i16 (zext   (v8i8 (extract_subvector V128:$Rn, (i64 8)) ))),
          (USHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v8i16 (sext   (v8i8 (extract_subvector V128:$Rn, (i64 8)) ))),
          (SSHLLv16i8_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (anyext (v4i16 (extract_subvector V128:$Rn, (i64 4)) ))),
          (USHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (zext   (v4i16 (extract_subvector V128:$Rn, (i64 4)) ))),
          (USHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v4i32 (sext   (v4i16 (extract_subvector V128:$Rn, (i64 4)) ))),
          (SSHLLv8i16_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (anyext (v2i32 (extract_subvector V128:$Rn, (i64 2)) ))),
          (USHLLv4i32_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (zext   (v2i32 (extract_subvector V128:$Rn, (i64 2)) ))),
          (USHLLv4i32_shift V128:$Rn, (i32 0))>;
def : Pat<(v2i64 (sext   (v2i32 (extract_subvector V128:$Rn, (i64 2)) ))),
          (SSHLLv4i32_shift V128:$Rn, (i32 0))>;

// Vector shift sxtl aliases
def : InstAlias<"sxtl.8h $dst, $src1",
                (SSHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.8h, $src1.8b",
                (SSHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl.4s $dst, $src1",
                (SSHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.4s, $src1.4h",
                (SSHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl.2d $dst, $src1",
                (SSHLLv2i32_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"sxtl $dst.2d, $src1.2s",
                (SSHLLv2i32_shift V128:$dst, V64:$src1, 0)>;

// Vector shift sxtl2 aliases
def : InstAlias<"sxtl2.8h $dst, $src1",
                (SSHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.8h, $src1.16b",
                (SSHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2.4s $dst, $src1",
                (SSHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.4s, $src1.8h",
                (SSHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2.2d $dst, $src1",
                (SSHLLv4i32_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"sxtl2 $dst.2d, $src1.4s",
                (SSHLLv4i32_shift V128:$dst, V128:$src1, 0)>;

// Vector shift uxtl aliases
def : InstAlias<"uxtl.8h $dst, $src1",
                (USHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.8h, $src1.8b",
                (USHLLv8i8_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl.4s $dst, $src1",
                (USHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.4s, $src1.4h",
                (USHLLv4i16_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl.2d $dst, $src1",
                (USHLLv2i32_shift V128:$dst, V64:$src1, 0)>;
def : InstAlias<"uxtl $dst.2d, $src1.2s",
                (USHLLv2i32_shift V128:$dst, V64:$src1, 0)>;

// Vector shift uxtl2 aliases
def : InstAlias<"uxtl2.8h $dst, $src1",
                (USHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.8h, $src1.16b",
                (USHLLv16i8_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2.4s $dst, $src1",
                (USHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.4s, $src1.8h",
                (USHLLv8i16_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2.2d $dst, $src1",
                (USHLLv4i32_shift V128:$dst, V128:$src1, 0)>;
def : InstAlias<"uxtl2 $dst.2d, $src1.4s",
                (USHLLv4i32_shift V128:$dst, V128:$src1, 0)>;

// If an integer is about to be converted to a floating point value,
// just load it on the floating point unit.
// These patterns are more complex because floating point loads do not
// support sign extension.
// The sign extension has to be explicitly added and is only supported for
// one step: byte-to-half, half-to-word, word-to-doubleword.
// SCVTF GPR -> FPR is 9 cycles.
// SCVTF FPR -> FPR is 4 cyclces.
// (sign extension with lengthen) SXTL FPR -> FPR is 2 cycles.
// Therefore, we can do 2 sign extensions and one SCVTF FPR -> FPR
// and still being faster.
// However, this is not good for code size.
// 8-bits -> float. 2 sizes step-up.
class SExtLoadi8CVTf32Pat<dag addrmode, dag INST>
  : Pat<(f32 (sint_to_fp (i32 (sextloadi8 addrmode)))),
        (SCVTFv1i32 (f32 (EXTRACT_SUBREG
                            (SSHLLv4i16_shift
                              (f64
                                (EXTRACT_SUBREG
                                  (SSHLLv8i8_shift
                                    (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                        INST,
                                        bsub),
                                    0),
                                  dsub)),
                               0),
                             ssub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32]>;

def : SExtLoadi8CVTf32Pat<(ro8.Wpat GPR64sp:$Rn, GPR32:$Rm, ro8.Wext:$ext),
                          (LDRBroW  GPR64sp:$Rn, GPR32:$Rm, ro8.Wext:$ext)>;
def : SExtLoadi8CVTf32Pat<(ro8.Xpat GPR64sp:$Rn, GPR64:$Rm, ro8.Xext:$ext),
                          (LDRBroX  GPR64sp:$Rn, GPR64:$Rm, ro8.Xext:$ext)>;
def : SExtLoadi8CVTf32Pat<(am_indexed8 GPR64sp:$Rn, uimm12s1:$offset),
                          (LDRBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : SExtLoadi8CVTf32Pat<(am_unscaled8 GPR64sp:$Rn, simm9:$offset),
                          (LDURBi GPR64sp:$Rn, simm9:$offset)>;

// 16-bits -> float. 1 size step-up.
class SExtLoadi16CVTf32Pat<dag addrmode, dag INST>
  : Pat<(f32 (sint_to_fp (i32 (sextloadi16 addrmode)))),
        (SCVTFv1i32 (f32 (EXTRACT_SUBREG
                            (SSHLLv4i16_shift
                                (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                  INST,
                                  hsub),
                                0),
                            ssub)))>, Requires<[NotForCodeSize]>;

def : SExtLoadi16CVTf32Pat<(ro16.Wpat GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext),
                           (LDRHroW   GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext)>;
def : SExtLoadi16CVTf32Pat<(ro16.Xpat GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext),
                           (LDRHroX   GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext)>;
def : SExtLoadi16CVTf32Pat<(am_indexed16 GPR64sp:$Rn, uimm12s2:$offset),
                           (LDRHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : SExtLoadi16CVTf32Pat<(am_unscaled16 GPR64sp:$Rn, simm9:$offset),
                           (LDURHi GPR64sp:$Rn, simm9:$offset)>;

// 32-bits to 32-bits are handled in target specific dag combine:
// performIntToFpCombine.
// 64-bits integer to 32-bits floating point, not possible with
// SCVTF on floating point registers (both source and destination
// must have the same size).

// Here are the patterns for 8, 16, 32, and 64-bits to double.
// 8-bits -> double. 3 size step-up: give up.
// 16-bits -> double. 2 size step.
class SExtLoadi16CVTf64Pat<dag addrmode, dag INST>
  : Pat <(f64 (sint_to_fp (i32 (sextloadi16 addrmode)))),
           (SCVTFv1i64 (f64 (EXTRACT_SUBREG
                              (SSHLLv2i32_shift
                                 (f64
                                  (EXTRACT_SUBREG
                                    (SSHLLv4i16_shift
                                      (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                        INST,
                                        hsub),
                                     0),
                                   dsub)),
                               0),
                             dsub)))>,
    Requires<[NotForCodeSize, UseAlternateSExtLoadCVTF32]>;

def : SExtLoadi16CVTf64Pat<(ro16.Wpat GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext),
                           (LDRHroW GPR64sp:$Rn, GPR32:$Rm, ro16.Wext:$ext)>;
def : SExtLoadi16CVTf64Pat<(ro16.Xpat GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext),
                           (LDRHroX GPR64sp:$Rn, GPR64:$Rm, ro16.Xext:$ext)>;
def : SExtLoadi16CVTf64Pat<(am_indexed16 GPR64sp:$Rn, uimm12s2:$offset),
                           (LDRHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : SExtLoadi16CVTf64Pat<(am_unscaled16 GPR64sp:$Rn, simm9:$offset),
                           (LDURHi GPR64sp:$Rn, simm9:$offset)>;
// 32-bits -> double. 1 size step-up.
class SExtLoadi32CVTf64Pat<dag addrmode, dag INST>
  : Pat <(f64 (sint_to_fp (i32 (load addrmode)))),
           (SCVTFv1i64 (f64 (EXTRACT_SUBREG
                              (SSHLLv2i32_shift
                                (INSERT_SUBREG (f64 (IMPLICIT_DEF)),
                                  INST,
                                  ssub),
                               0),
                             dsub)))>, Requires<[NotForCodeSize]>;

def : SExtLoadi32CVTf64Pat<(ro32.Wpat GPR64sp:$Rn, GPR32:$Rm, ro32.Wext:$ext),
                           (LDRSroW GPR64sp:$Rn, GPR32:$Rm, ro32.Wext:$ext)>;
def : SExtLoadi32CVTf64Pat<(ro32.Xpat GPR64sp:$Rn, GPR64:$Rm, ro32.Xext:$ext),
                           (LDRSroX GPR64sp:$Rn, GPR64:$Rm, ro32.Xext:$ext)>;
def : SExtLoadi32CVTf64Pat<(am_indexed32 GPR64sp:$Rn, uimm12s4:$offset),
                           (LDRSui GPR64sp:$Rn, uimm12s4:$offset)>;
def : SExtLoadi32CVTf64Pat<(am_unscaled32 GPR64sp:$Rn, simm9:$offset),
                           (LDURSi GPR64sp:$Rn, simm9:$offset)>;

// 64-bits -> double are handled in target specific dag combine:
// performIntToFpCombine.


//----------------------------------------------------------------------------
// AdvSIMD Load-Store Structure
//----------------------------------------------------------------------------
defm LD1 : SIMDLd1Multiple<"ld1">;
defm LD2 : SIMDLd2Multiple<"ld2">;
defm LD3 : SIMDLd3Multiple<"ld3">;
defm LD4 : SIMDLd4Multiple<"ld4">;

defm ST1 : SIMDSt1Multiple<"st1">;
defm ST2 : SIMDSt2Multiple<"st2">;
defm ST3 : SIMDSt3Multiple<"st3">;
defm ST4 : SIMDSt4Multiple<"st4">;

class Ld1Pat<ValueType ty, Instruction INST>
  : Pat<(ty (load GPR64sp:$Rn)), (INST GPR64sp:$Rn)>;

def : Ld1Pat<v16i8, LD1Onev16b>;
def : Ld1Pat<v8i16, LD1Onev8h>;
def : Ld1Pat<v4i32, LD1Onev4s>;
def : Ld1Pat<v2i64, LD1Onev2d>;
def : Ld1Pat<v8i8,  LD1Onev8b>;
def : Ld1Pat<v4i16, LD1Onev4h>;
def : Ld1Pat<v2i32, LD1Onev2s>;
def : Ld1Pat<v1i64, LD1Onev1d>;

class St1Pat<ValueType ty, Instruction INST>
  : Pat<(store ty:$Vt, GPR64sp:$Rn),
        (INST ty:$Vt, GPR64sp:$Rn)>;

def : St1Pat<v16i8, ST1Onev16b>;
def : St1Pat<v8i16, ST1Onev8h>;
def : St1Pat<v4i32, ST1Onev4s>;
def : St1Pat<v2i64, ST1Onev2d>;
def : St1Pat<v8i8,  ST1Onev8b>;
def : St1Pat<v4i16, ST1Onev4h>;
def : St1Pat<v2i32, ST1Onev2s>;
def : St1Pat<v1i64, ST1Onev1d>;

//---
// Single-element
//---

defm LD1R          : SIMDLdR<0, 0b110, 0, "ld1r", "One", 1, 2, 4, 8>;
defm LD2R          : SIMDLdR<1, 0b110, 0, "ld2r", "Two", 2, 4, 8, 16>;
defm LD3R          : SIMDLdR<0, 0b111, 0, "ld3r", "Three", 3, 6, 12, 24>;
defm LD4R          : SIMDLdR<1, 0b111, 0, "ld4r", "Four", 4, 8, 16, 32>;
let mayLoad = 1, hasSideEffects = 0 in {
defm LD1 : SIMDLdSingleBTied<0, 0b000,       "ld1", VecListOneb,   GPR64pi1>;
defm LD1 : SIMDLdSingleHTied<0, 0b010, 0,    "ld1", VecListOneh,   GPR64pi2>;
defm LD1 : SIMDLdSingleSTied<0, 0b100, 0b00, "ld1", VecListOnes,   GPR64pi4>;
defm LD1 : SIMDLdSingleDTied<0, 0b100, 0b01, "ld1", VecListOned,   GPR64pi8>;
defm LD2 : SIMDLdSingleBTied<1, 0b000,       "ld2", VecListTwob,   GPR64pi2>;
defm LD2 : SIMDLdSingleHTied<1, 0b010, 0,    "ld2", VecListTwoh,   GPR64pi4>;
defm LD2 : SIMDLdSingleSTied<1, 0b100, 0b00, "ld2", VecListTwos,   GPR64pi8>;
defm LD2 : SIMDLdSingleDTied<1, 0b100, 0b01, "ld2", VecListTwod,   GPR64pi16>;
defm LD3 : SIMDLdSingleBTied<0, 0b001,       "ld3", VecListThreeb, GPR64pi3>;
defm LD3 : SIMDLdSingleHTied<0, 0b011, 0,    "ld3", VecListThreeh, GPR64pi6>;
defm LD3 : SIMDLdSingleSTied<0, 0b101, 0b00, "ld3", VecListThrees, GPR64pi12>;
defm LD3 : SIMDLdSingleDTied<0, 0b101, 0b01, "ld3", VecListThreed, GPR64pi24>;
defm LD4 : SIMDLdSingleBTied<1, 0b001,       "ld4", VecListFourb,  GPR64pi4>;
defm LD4 : SIMDLdSingleHTied<1, 0b011, 0,    "ld4", VecListFourh,  GPR64pi8>;
defm LD4 : SIMDLdSingleSTied<1, 0b101, 0b00, "ld4", VecListFours,  GPR64pi16>;
defm LD4 : SIMDLdSingleDTied<1, 0b101, 0b01, "ld4", VecListFourd,  GPR64pi32>;
}

def : Pat<(v8i8 (AArch64dup (i32 (extloadi8 GPR64sp:$Rn)))),
          (LD1Rv8b GPR64sp:$Rn)>;
def : Pat<(v16i8 (AArch64dup (i32 (extloadi8 GPR64sp:$Rn)))),
          (LD1Rv16b GPR64sp:$Rn)>;
def : Pat<(v4i16 (AArch64dup (i32 (extloadi16 GPR64sp:$Rn)))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8i16 (AArch64dup (i32 (extloadi16 GPR64sp:$Rn)))),
          (LD1Rv8h GPR64sp:$Rn)>;
def : Pat<(v2i32 (AArch64dup (i32 (load GPR64sp:$Rn)))),
          (LD1Rv2s GPR64sp:$Rn)>;
def : Pat<(v4i32 (AArch64dup (i32 (load GPR64sp:$Rn)))),
          (LD1Rv4s GPR64sp:$Rn)>;
def : Pat<(v2i64 (AArch64dup (i64 (load GPR64sp:$Rn)))),
          (LD1Rv2d GPR64sp:$Rn)>;
def : Pat<(v1i64 (AArch64dup (i64 (load GPR64sp:$Rn)))),
          (LD1Rv1d GPR64sp:$Rn)>;
// Grab the floating point version too
def : Pat<(v2f32 (AArch64dup (f32 (load GPR64sp:$Rn)))),
          (LD1Rv2s GPR64sp:$Rn)>;
def : Pat<(v4f32 (AArch64dup (f32 (load GPR64sp:$Rn)))),
          (LD1Rv4s GPR64sp:$Rn)>;
def : Pat<(v2f64 (AArch64dup (f64 (load GPR64sp:$Rn)))),
          (LD1Rv2d GPR64sp:$Rn)>;
def : Pat<(v1f64 (AArch64dup (f64 (load GPR64sp:$Rn)))),
          (LD1Rv1d GPR64sp:$Rn)>;
def : Pat<(v4f16 (AArch64dup (f16 (load GPR64sp:$Rn)))),
          (LD1Rv4h GPR64sp:$Rn)>;
def : Pat<(v8f16 (AArch64dup (f16 (load GPR64sp:$Rn)))),
          (LD1Rv8h GPR64sp:$Rn)>;

class Ld1Lane128Pat<SDPatternOperator scalar_load, Operand VecIndex,
                    ValueType VTy, ValueType STy, Instruction LD1>
  : Pat<(vector_insert (VTy VecListOne128:$Rd),
           (STy (scalar_load GPR64sp:$Rn)), VecIndex:$idx),
        (LD1 VecListOne128:$Rd, VecIndex:$idx, GPR64sp:$Rn)>;

def : Ld1Lane128Pat<extloadi8,  VectorIndexB, v16i8, i32, LD1i8>;
def : Ld1Lane128Pat<extloadi16, VectorIndexH, v8i16, i32, LD1i16>;
def : Ld1Lane128Pat<load,       VectorIndexS, v4i32, i32, LD1i32>;
def : Ld1Lane128Pat<load,       VectorIndexS, v4f32, f32, LD1i32>;
def : Ld1Lane128Pat<load,       VectorIndexD, v2i64, i64, LD1i64>;
def : Ld1Lane128Pat<load,       VectorIndexD, v2f64, f64, LD1i64>;
def : Ld1Lane128Pat<load,       VectorIndexH, v8f16, f16, LD1i16>;

class Ld1Lane64Pat<SDPatternOperator scalar_load, Operand VecIndex,
                   ValueType VTy, ValueType STy, Instruction LD1>
  : Pat<(vector_insert (VTy VecListOne64:$Rd),
           (STy (scalar_load GPR64sp:$Rn)), VecIndex:$idx),
        (EXTRACT_SUBREG
            (LD1 (SUBREG_TO_REG (i32 0), VecListOne64:$Rd, dsub),
                          VecIndex:$idx, GPR64sp:$Rn),
            dsub)>;

def : Ld1Lane64Pat<extloadi8,  VectorIndexB, v8i8,  i32, LD1i8>;
def : Ld1Lane64Pat<extloadi16, VectorIndexH, v4i16, i32, LD1i16>;
def : Ld1Lane64Pat<load,       VectorIndexS, v2i32, i32, LD1i32>;
def : Ld1Lane64Pat<load,       VectorIndexS, v2f32, f32, LD1i32>;
def : Ld1Lane64Pat<load,       VectorIndexH, v4f16, f16, LD1i16>;


defm LD1 : SIMDLdSt1SingleAliases<"ld1">;
defm LD2 : SIMDLdSt2SingleAliases<"ld2">;
defm LD3 : SIMDLdSt3SingleAliases<"ld3">;
defm LD4 : SIMDLdSt4SingleAliases<"ld4">;

// Stores
defm ST1 : SIMDStSingleB<0, 0b000,       "st1", VecListOneb, GPR64pi1>;
defm ST1 : SIMDStSingleH<0, 0b010, 0,    "st1", VecListOneh, GPR64pi2>;
defm ST1 : SIMDStSingleS<0, 0b100, 0b00, "st1", VecListOnes, GPR64pi4>;
defm ST1 : SIMDStSingleD<0, 0b100, 0b01, "st1", VecListOned, GPR64pi8>;

let AddedComplexity = 19 in
class St1Lane128Pat<SDPatternOperator scalar_store, Operand VecIndex,
                    ValueType VTy, ValueType STy, Instruction ST1>
  : Pat<(scalar_store
             (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
             GPR64sp:$Rn),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn)>;

def : St1Lane128Pat<truncstorei8,  VectorIndexB, v16i8, i32, ST1i8>;
def : St1Lane128Pat<truncstorei16, VectorIndexH, v8i16, i32, ST1i16>;
def : St1Lane128Pat<store,         VectorIndexS, v4i32, i32, ST1i32>;
def : St1Lane128Pat<store,         VectorIndexS, v4f32, f32, ST1i32>;
def : St1Lane128Pat<store,         VectorIndexD, v2i64, i64, ST1i64>;
def : St1Lane128Pat<store,         VectorIndexD, v2f64, f64, ST1i64>;
def : St1Lane128Pat<store,         VectorIndexH, v8f16, f16, ST1i16>;

let AddedComplexity = 19 in
class St1Lane64Pat<SDPatternOperator scalar_store, Operand VecIndex,
                   ValueType VTy, ValueType STy, Instruction ST1>
  : Pat<(scalar_store
             (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
             GPR64sp:$Rn),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn)>;

def : St1Lane64Pat<truncstorei8,  VectorIndexB, v8i8, i32, ST1i8>;
def : St1Lane64Pat<truncstorei16, VectorIndexH, v4i16, i32, ST1i16>;
def : St1Lane64Pat<store,         VectorIndexS, v2i32, i32, ST1i32>;
def : St1Lane64Pat<store,         VectorIndexS, v2f32, f32, ST1i32>;
def : St1Lane64Pat<store,         VectorIndexH, v4f16, f16, ST1i16>;

multiclass St1LanePost64Pat<SDPatternOperator scalar_store, Operand VecIndex,
                             ValueType VTy, ValueType STy, Instruction ST1,
                             int offset> {
  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, offset),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn, XZR)>;

  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne64:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, GPR64:$Rm),
        (ST1 (SUBREG_TO_REG (i32 0), VecListOne64:$Vt, dsub),
             VecIndex:$idx, GPR64sp:$Rn, $Rm)>;
}

defm : St1LanePost64Pat<post_truncsti8, VectorIndexB, v8i8, i32, ST1i8_POST, 1>;
defm : St1LanePost64Pat<post_truncsti16, VectorIndexH, v4i16, i32, ST1i16_POST,
                        2>;
defm : St1LanePost64Pat<post_store, VectorIndexS, v2i32, i32, ST1i32_POST, 4>;
defm : St1LanePost64Pat<post_store, VectorIndexS, v2f32, f32, ST1i32_POST, 4>;
defm : St1LanePost64Pat<post_store, VectorIndexD, v1i64, i64, ST1i64_POST, 8>;
defm : St1LanePost64Pat<post_store, VectorIndexD, v1f64, f64, ST1i64_POST, 8>;
defm : St1LanePost64Pat<post_store, VectorIndexH, v4f16, f16, ST1i16_POST, 2>;

multiclass St1LanePost128Pat<SDPatternOperator scalar_store, Operand VecIndex,
                             ValueType VTy, ValueType STy, Instruction ST1,
                             int offset> {
  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, offset),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn, XZR)>;

  def : Pat<(scalar_store
              (STy (vector_extract (VTy VecListOne128:$Vt), VecIndex:$idx)),
              GPR64sp:$Rn, GPR64:$Rm),
        (ST1 VecListOne128:$Vt, VecIndex:$idx, GPR64sp:$Rn, $Rm)>;
}

defm : St1LanePost128Pat<post_truncsti8, VectorIndexB, v16i8, i32, ST1i8_POST,
                         1>;
defm : St1LanePost128Pat<post_truncsti16, VectorIndexH, v8i16, i32, ST1i16_POST,
                         2>;
defm : St1LanePost128Pat<post_store, VectorIndexS, v4i32, i32, ST1i32_POST, 4>;
defm : St1LanePost128Pat<post_store, VectorIndexS, v4f32, f32, ST1i32_POST, 4>;
defm : St1LanePost128Pat<post_store, VectorIndexD, v2i64, i64, ST1i64_POST, 8>;
defm : St1LanePost128Pat<post_store, VectorIndexD, v2f64, f64, ST1i64_POST, 8>;
defm : St1LanePost128Pat<post_store, VectorIndexH, v8f16, f16, ST1i16_POST, 2>;

let mayStore = 1, hasSideEffects = 0 in {
defm ST2 : SIMDStSingleB<1, 0b000,       "st2", VecListTwob,   GPR64pi2>;
defm ST2 : SIMDStSingleH<1, 0b010, 0,    "st2", VecListTwoh,   GPR64pi4>;
defm ST2 : SIMDStSingleS<1, 0b100, 0b00, "st2", VecListTwos,   GPR64pi8>;
defm ST2 : SIMDStSingleD<1, 0b100, 0b01, "st2", VecListTwod,   GPR64pi16>;
defm ST3 : SIMDStSingleB<0, 0b001,       "st3", VecListThreeb, GPR64pi3>;
defm ST3 : SIMDStSingleH<0, 0b011, 0,    "st3", VecListThreeh, GPR64pi6>;
defm ST3 : SIMDStSingleS<0, 0b101, 0b00, "st3", VecListThrees, GPR64pi12>;
defm ST3 : SIMDStSingleD<0, 0b101, 0b01, "st3", VecListThreed, GPR64pi24>;
defm ST4 : SIMDStSingleB<1, 0b001,       "st4", VecListFourb,  GPR64pi4>;
defm ST4 : SIMDStSingleH<1, 0b011, 0,    "st4", VecListFourh,  GPR64pi8>;
defm ST4 : SIMDStSingleS<1, 0b101, 0b00, "st4", VecListFours,  GPR64pi16>;
defm ST4 : SIMDStSingleD<1, 0b101, 0b01, "st4", VecListFourd,  GPR64pi32>;
}

defm ST1 : SIMDLdSt1SingleAliases<"st1">;
defm ST2 : SIMDLdSt2SingleAliases<"st2">;
defm ST3 : SIMDLdSt3SingleAliases<"st3">;
defm ST4 : SIMDLdSt4SingleAliases<"st4">;

//----------------------------------------------------------------------------
// Crypto extensions
//----------------------------------------------------------------------------

let Predicates = [HasAES] in {
def AESErr   : AESTiedInst<0b0100, "aese",   int_aarch64_crypto_aese>;
def AESDrr   : AESTiedInst<0b0101, "aesd",   int_aarch64_crypto_aesd>;
def AESMCrr  : AESInst<    0b0110, "aesmc",  int_aarch64_crypto_aesmc>;
def AESIMCrr : AESInst<    0b0111, "aesimc", int_aarch64_crypto_aesimc>;
}

// Pseudo instructions for AESMCrr/AESIMCrr with a register constraint required
// for AES fusion on some CPUs.
let hasSideEffects = 0, mayStore = 0, mayLoad = 0 in {
def AESMCrrTied: Pseudo<(outs V128:$Rd), (ins V128:$Rn), [], "$Rn = $Rd">,
                        Sched<[WriteV]>;
def AESIMCrrTied: Pseudo<(outs V128:$Rd), (ins V128:$Rn), [], "$Rn = $Rd">,
                         Sched<[WriteV]>;
}

// Only use constrained versions of AES(I)MC instructions if they are paired with
// AESE/AESD.
def : Pat<(v16i8 (int_aarch64_crypto_aesmc
            (v16i8 (int_aarch64_crypto_aese (v16i8 V128:$src1),
                                            (v16i8 V128:$src2))))),
          (v16i8 (AESMCrrTied (v16i8 (AESErr (v16i8 V128:$src1),
                                             (v16i8 V128:$src2)))))>,
          Requires<[HasFuseAES]>;

def : Pat<(v16i8 (int_aarch64_crypto_aesimc
            (v16i8 (int_aarch64_crypto_aesd (v16i8 V128:$src1),
                                            (v16i8 V128:$src2))))),
          (v16i8 (AESIMCrrTied (v16i8 (AESDrr (v16i8 V128:$src1),
                                              (v16i8 V128:$src2)))))>,
          Requires<[HasFuseAES]>;

let Predicates = [HasSHA2] in {
def SHA1Crrr     : SHATiedInstQSV<0b000, "sha1c",   int_aarch64_crypto_sha1c>;
def SHA1Prrr     : SHATiedInstQSV<0b001, "sha1p",   int_aarch64_crypto_sha1p>;
def SHA1Mrrr     : SHATiedInstQSV<0b010, "sha1m",   int_aarch64_crypto_sha1m>;
def SHA1SU0rrr   : SHATiedInstVVV<0b011, "sha1su0", int_aarch64_crypto_sha1su0>;
def SHA256Hrrr   : SHATiedInstQQV<0b100, "sha256h", int_aarch64_crypto_sha256h>;
def SHA256H2rrr  : SHATiedInstQQV<0b101, "sha256h2",int_aarch64_crypto_sha256h2>;
def SHA256SU1rrr :SHATiedInstVVV<0b110, "sha256su1",int_aarch64_crypto_sha256su1>;

def SHA1Hrr     : SHAInstSS<    0b0000, "sha1h",    int_aarch64_crypto_sha1h>;
def SHA1SU1rr   : SHATiedInstVV<0b0001, "sha1su1",  int_aarch64_crypto_sha1su1>;
def SHA256SU0rr : SHATiedInstVV<0b0010, "sha256su0",int_aarch64_crypto_sha256su0>;
}

//----------------------------------------------------------------------------
// Compiler-pseudos
//----------------------------------------------------------------------------
// FIXME: Like for X86, these should go in their own separate .td file.

def def32 : PatLeaf<(i32 GPR32:$src), [{
  return isDef32(*N);
}]>;

// In the case of a 32-bit def that is known to implicitly zero-extend,
// we can use a SUBREG_TO_REG.
def : Pat<(i64 (zext def32:$src)), (SUBREG_TO_REG (i64 0), GPR32:$src, sub_32)>;

// For an anyext, we don't care what the high bits are, so we can perform an
// INSERT_SUBREF into an IMPLICIT_DEF.
def : Pat<(i64 (anyext GPR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$src, sub_32)>;

// When we need to explicitly zero-extend, we use a 32-bit MOV instruction and
// then assert the extension has happened.
def : Pat<(i64 (zext GPR32:$src)),
          (SUBREG_TO_REG (i32 0), (ORRWrs WZR, GPR32:$src, 0), sub_32)>;

// To sign extend, we use a signed bitfield move instruction (SBFM) on the
// containing super-reg.
def : Pat<(i64 (sext GPR32:$src)),
   (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$src, sub_32), 0, 31)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i32)), (SBFMXri GPR64:$src, 0, 31)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i16)), (SBFMXri GPR64:$src, 0, 15)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i8)),  (SBFMXri GPR64:$src, 0, 7)>;
def : Pat<(i64 (sext_inreg GPR64:$src, i1)),  (SBFMXri GPR64:$src, 0, 0)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i16)), (SBFMWri GPR32:$src, 0, 15)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i8)),  (SBFMWri GPR32:$src, 0, 7)>;
def : Pat<(i32 (sext_inreg GPR32:$src, i1)),  (SBFMWri GPR32:$src, 0, 0)>;

def : Pat<(shl (sext_inreg GPR32:$Rn, i8), (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, (i64 (i32shift_a       imm0_31:$imm)),
                              (i64 (i32shift_sext_i8 imm0_31:$imm)))>;
def : Pat<(shl (sext_inreg GPR64:$Rn, i8), (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, (i64 (i64shift_a imm0_63:$imm)),
                              (i64 (i64shift_sext_i8 imm0_63:$imm)))>;

def : Pat<(shl (sext_inreg GPR32:$Rn, i16), (i64 imm0_31:$imm)),
          (SBFMWri GPR32:$Rn, (i64 (i32shift_a        imm0_31:$imm)),
                              (i64 (i32shift_sext_i16 imm0_31:$imm)))>;
def : Pat<(shl (sext_inreg GPR64:$Rn, i16), (i64 imm0_63:$imm)),
          (SBFMXri GPR64:$Rn, (i64 (i64shift_a        imm0_63:$imm)),
                              (i64 (i64shift_sext_i16 imm0_63:$imm)))>;

def : Pat<(shl (i64 (sext GPR32:$Rn)), (i64 imm0_63:$imm)),
          (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$Rn, sub_32),
                   (i64 (i64shift_a        imm0_63:$imm)),
                   (i64 (i64shift_sext_i32 imm0_63:$imm)))>;

// sra patterns have an AddedComplexity of 10, so make sure we have a higher
// AddedComplexity for the following patterns since we want to match sext + sra
// patterns before we attempt to match a single sra node.
let AddedComplexity = 20 in {
// We support all sext + sra combinations which preserve at least one bit of the
// original value which is to be sign extended. E.g. we support shifts up to
// bitwidth-1 bits.
def : Pat<(sra (sext_inreg GPR32:$Rn, i8), (i64 imm0_7:$imm)),
          (SBFMWri GPR32:$Rn, (i64 imm0_7:$imm), 7)>;
def : Pat<(sra (sext_inreg GPR64:$Rn, i8), (i64 imm0_7:$imm)),
          (SBFMXri GPR64:$Rn, (i64 imm0_7:$imm), 7)>;

def : Pat<(sra (sext_inreg GPR32:$Rn, i16), (i64 imm0_15:$imm)),
          (SBFMWri GPR32:$Rn, (i64 imm0_15:$imm), 15)>;
def : Pat<(sra (sext_inreg GPR64:$Rn, i16), (i64 imm0_15:$imm)),
          (SBFMXri GPR64:$Rn, (i64 imm0_15:$imm), 15)>;

def : Pat<(sra (i64 (sext GPR32:$Rn)), (i64 imm0_31:$imm)),
          (SBFMXri (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$Rn, sub_32),
                   (i64 imm0_31:$imm), 31)>;
} // AddedComplexity = 20

// To truncate, we can simply extract from a subregister.
def : Pat<(i32 (trunc GPR64sp:$src)),
          (i32 (EXTRACT_SUBREG GPR64sp:$src, sub_32))>;

// __builtin_trap() uses the BRK instruction on AArch64.
def : Pat<(trap), (BRK 1)>;

// Multiply high patterns which multiply the lower subvector using smull/umull
// and the upper subvector with smull2/umull2. Then shuffle the high the high
// part of both results together.
def : Pat<(v16i8 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v16i8
           (SMULLv8i8_v8i16 (EXTRACT_SUBREG V128:$Rn, dsub),
                            (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv16i8_v8i16 V128:$Rn, V128:$Rm))>;
def : Pat<(v8i16 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v8i16
           (SMULLv4i16_v4i32 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv8i16_v4i32 V128:$Rn, V128:$Rm))>;
def : Pat<(v4i32 (mulhs V128:$Rn, V128:$Rm)),
          (UZP2v4i32
           (SMULLv2i32_v2i64 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (SMULLv4i32_v2i64 V128:$Rn, V128:$Rm))>;

def : Pat<(v16i8 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v16i8
           (UMULLv8i8_v8i16 (EXTRACT_SUBREG V128:$Rn, dsub),
                            (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv16i8_v8i16 V128:$Rn, V128:$Rm))>;
def : Pat<(v8i16 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v8i16
           (UMULLv4i16_v4i32 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv8i16_v4i32 V128:$Rn, V128:$Rm))>;
def : Pat<(v4i32 (mulhu V128:$Rn, V128:$Rm)),
          (UZP2v4i32
           (UMULLv2i32_v2i64 (EXTRACT_SUBREG V128:$Rn, dsub),
                             (EXTRACT_SUBREG V128:$Rm, dsub)),
           (UMULLv4i32_v2i64 V128:$Rn, V128:$Rm))>;

// Conversions within AdvSIMD types in the same register size are free.
// But because we need a consistent lane ordering, in big endian many
// conversions require one or more REV instructions.
//
// Consider a simple memory load followed by a bitconvert then a store.
//   v0 = load v2i32
//   v1 = BITCAST v2i32 v0 to v4i16
//        store v4i16 v2
//
// In big endian mode every memory access has an implicit byte swap. LDR and
// STR do a 64-bit byte swap, whereas LD1/ST1 do a byte swap per lane - that
// is, they treat the vector as a sequence of elements to be byte-swapped.
// The two pairs of instructions are fundamentally incompatible. We've decided
// to use LD1/ST1 only to simplify compiler implementation.
//
// LD1/ST1 perform the equivalent of a sequence of LDR/STR + REV. This makes
// the original code sequence:
//   v0 = load v2i32
//   v1 = REV v2i32                  (implicit)
//   v2 = BITCAST v2i32 v1 to v4i16
//   v3 = REV v4i16 v2               (implicit)
//        store v4i16 v3
//
// But this is now broken - the value stored is different to the value loaded
// due to lane reordering. To fix this, on every BITCAST we must perform two
// other REVs:
//   v0 = load v2i32
//   v1 = REV v2i32                  (implicit)
//   v2 = REV v2i32
//   v3 = BITCAST v2i32 v2 to v4i16
//   v4 = REV v4i16
//   v5 = REV v4i16 v4               (implicit)
//        store v4i16 v5
//
// This means an extra two instructions, but actually in most cases the two REV
// instructions can be combined into one. For example:
//   (REV64_2s (REV64_4h X)) === (REV32_4h X)
//
// There is also no 128-bit REV instruction. This must be synthesized with an
// EXT instruction.
//
// Most bitconverts require some sort of conversion. The only exceptions are:
//   a) Identity conversions -  vNfX <-> vNiX
//   b) Single-lane-to-scalar - v1fX <-> fX or v1iX <-> iX
//

// Natural vector casts (64 bit)
def : Pat<(v8i8 (AArch64NvCast (v2i32 FPR64:$src))), (v8i8 FPR64:$src)>;
def : Pat<(v4i16 (AArch64NvCast (v2i32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4f16 (AArch64NvCast (v2i32 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v2i32 (AArch64NvCast (v2i32 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2f32 (AArch64NvCast (v2i32 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v1i64 (AArch64NvCast (v2i32 FPR64:$src))), (v1i64 FPR64:$src)>;

def : Pat<(v8i8 (AArch64NvCast (v4i16 FPR64:$src))), (v8i8 FPR64:$src)>;
def : Pat<(v4i16 (AArch64NvCast (v4i16 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4f16 (AArch64NvCast (v4i16 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v2i32 (AArch64NvCast (v4i16 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v1i64 (AArch64NvCast (v4i16 FPR64:$src))), (v1i64 FPR64:$src)>;

def : Pat<(v8i8 (AArch64NvCast (v8i8 FPR64:$src))), (v8i8 FPR64:$src)>;
def : Pat<(v4i16 (AArch64NvCast (v8i8 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4f16 (AArch64NvCast (v8i8 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v2i32 (AArch64NvCast (v8i8 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2f32 (AArch64NvCast (v8i8 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v1i64 (AArch64NvCast (v8i8 FPR64:$src))), (v1i64 FPR64:$src)>;

def : Pat<(v8i8 (AArch64NvCast (f64 FPR64:$src))), (v8i8 FPR64:$src)>;
def : Pat<(v4i16 (AArch64NvCast (f64 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4f16 (AArch64NvCast (f64 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v2i32 (AArch64NvCast (f64 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2f32 (AArch64NvCast (f64 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v1i64 (AArch64NvCast (f64 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1f64 (AArch64NvCast (f64 FPR64:$src))), (v1f64 FPR64:$src)>;

def : Pat<(v8i8 (AArch64NvCast (v2f32 FPR64:$src))), (v8i8 FPR64:$src)>;
def : Pat<(v4i16 (AArch64NvCast (v2f32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v2i32 (AArch64NvCast (v2f32 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2f32 (AArch64NvCast (v2f32 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v1i64 (AArch64NvCast (v2f32 FPR64:$src))), (v1i64 FPR64:$src)>;

// Natural vector casts (128 bit)
def : Pat<(v16i8 (AArch64NvCast (v4i32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v4i32 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v4i32 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v4i32 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v4i32 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v4i32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v4i32 FPR128:$src))), (v2f64 FPR128:$src)>;

def : Pat<(v16i8 (AArch64NvCast (v8i16 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v8i16 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v8i16 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v8i16 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v8i16 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v8i16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v8i16 FPR128:$src))), (v2f64 FPR128:$src)>;

def : Pat<(v16i8 (AArch64NvCast (v16i8 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v16i8 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v16i8 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v16i8 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v16i8 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v16i8 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v16i8 FPR128:$src))), (v2f64 FPR128:$src)>;

def : Pat<(v16i8 (AArch64NvCast (v2i64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v2i64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v2i64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v2i64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v2i64 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v2i64 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v2i64 FPR128:$src))), (v2f64 FPR128:$src)>;

def : Pat<(v16i8 (AArch64NvCast (v4f32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v4f32 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v4f32 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v4f32 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v4f32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v4f32 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v4f32 FPR128:$src))), (v2f64 FPR128:$src)>;

def : Pat<(v16i8 (AArch64NvCast (v2f64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v8i16 (AArch64NvCast (v2f64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v4i32 (AArch64NvCast (v2f64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v2i64 (AArch64NvCast (v2f64 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2f64 (AArch64NvCast (v2f64 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v8f16 (AArch64NvCast (v2f64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v4f32 (AArch64NvCast (v2f64 FPR128:$src))), (v4f32 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i8  (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v4i16 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v2i32 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v4f16 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v2f32 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;

def : Pat<(i64 (bitconvert (v8i8  V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v4i16 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v2i32 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v4f16 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v2f32 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(i64 (bitconvert (v1f64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i8  (bitconvert GPR64:$Xn)),
                 (REV64v8i8 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v4i16 (bitconvert GPR64:$Xn)),
                 (REV64v4i16 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v2i32 (bitconvert GPR64:$Xn)),
                 (REV64v2i32 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v4f16 (bitconvert GPR64:$Xn)),
                 (REV64v4i16 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;
def : Pat<(v2f32 (bitconvert GPR64:$Xn)),
                 (REV64v2i32 (COPY_TO_REGCLASS GPR64:$Xn, FPR64))>;

def : Pat<(i64 (bitconvert (v8i8  V64:$Vn))),
          (REV64v8i8 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v4i16 V64:$Vn))),
          (REV64v4i16 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v2i32 V64:$Vn))),
          (REV64v2i32 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v4f16 V64:$Vn))),
          (REV64v4i16 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
def : Pat<(i64 (bitconvert (v2f32 V64:$Vn))),
          (REV64v2i32 (COPY_TO_REGCLASS V64:$Vn, GPR64))>;
}
def : Pat<(v1i64 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (bitconvert GPR64:$Xn)), (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(i64 (bitconvert (v1i64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;
def : Pat<(v1i64 (scalar_to_vector GPR64:$Xn)),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (scalar_to_vector GPR64:$Xn)),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(v1f64 (scalar_to_vector (f64 FPR64:$Xn))), (v1f64 FPR64:$Xn)>;

def : Pat<(f32 (bitconvert (i32 GPR32:$Xn))),
          (COPY_TO_REGCLASS GPR32:$Xn, FPR32)>;
def : Pat<(i32 (bitconvert (f32 FPR32:$Xn))),
          (COPY_TO_REGCLASS FPR32:$Xn, GPR32)>;
def : Pat<(f64 (bitconvert (i64 GPR64:$Xn))),
          (COPY_TO_REGCLASS GPR64:$Xn, FPR64)>;
def : Pat<(i64 (bitconvert (f64 FPR64:$Xn))),
          (COPY_TO_REGCLASS FPR64:$Xn, GPR64)>;
def : Pat<(i64 (bitconvert (v1f64 V64:$Vn))),
          (COPY_TO_REGCLASS V64:$Vn, GPR64)>;

let Predicates = [IsLE] in {
def : Pat<(v1i64 (bitconvert (v2i32 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v4i16 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v8i8  FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v4f16 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (v2f32 FPR64:$src))), (v1i64 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v1i64 (bitconvert (v2i32 FPR64:$src))),
                             (v1i64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v4i16 FPR64:$src))),
                             (v1i64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v8i8  FPR64:$src))),
                             (v1i64 (REV64v8i8 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v4f16 FPR64:$src))),
                             (v1i64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1i64 (bitconvert (v2f32 FPR64:$src))),
                             (v1i64 (REV64v2i32 FPR64:$src))>;
}
def : Pat<(v1i64 (bitconvert (v1f64 FPR64:$src))), (v1i64 FPR64:$src)>;
def : Pat<(v1i64 (bitconvert (f64   FPR64:$src))), (v1i64 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2i32 (bitconvert (v1i64 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v4i16 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v8i8  FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (f64   FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v1f64 FPR64:$src))), (v2i32 FPR64:$src)>;
def : Pat<(v2i32 (bitconvert (v4f16 FPR64:$src))), (v2i32 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2i32 (bitconvert (v1i64 FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v4i16 FPR64:$src))),
                             (v2i32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v8i8  FPR64:$src))),
                             (v2i32 (REV32v8i8 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (f64   FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v1f64 FPR64:$src))),
                             (v2i32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2i32 (bitconvert (v4f16 FPR64:$src))),
                             (v2i32 (REV32v4i16 FPR64:$src))>;
}
def : Pat<(v2i32 (bitconvert (v2f32 FPR64:$src))), (v2i32 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4i16 (bitconvert (v1i64 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v2i32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v8i8  FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (f64   FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v2f32 FPR64:$src))), (v4i16 FPR64:$src)>;
def : Pat<(v4i16 (bitconvert (v1f64 FPR64:$src))), (v4i16 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4i16 (bitconvert (v1i64 FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v2i32 FPR64:$src))),
                             (v4i16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v8i8  FPR64:$src))),
                             (v4i16 (REV16v8i8 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (f64   FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v2f32 FPR64:$src))),
                             (v4i16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4i16 (bitconvert (v1f64 FPR64:$src))),
                             (v4i16 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v4i16 (bitconvert (v4f16 FPR64:$src))), (v4i16 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4f16 (bitconvert (v1i64 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v2i32 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v8i8  FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (f64   FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v2f32 FPR64:$src))), (v4f16 FPR64:$src)>;
def : Pat<(v4f16 (bitconvert (v1f64 FPR64:$src))), (v4f16 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4f16 (bitconvert (v1i64 FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v2i32 FPR64:$src))),
                             (v4f16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v8i8  FPR64:$src))),
                             (v4f16 (REV16v8i8 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (f64   FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v2f32 FPR64:$src))),
                             (v4f16 (REV32v4i16 FPR64:$src))>;
def : Pat<(v4f16 (bitconvert (v1f64 FPR64:$src))),
                             (v4f16 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v4f16 (bitconvert (v4i16 FPR64:$src))), (v4f16 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i8  (bitconvert (v1i64 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v2i32 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v4i16 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (f64   FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v2f32 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v1f64 FPR64:$src))), (v8i8  FPR64:$src)>;
def : Pat<(v8i8  (bitconvert (v4f16 FPR64:$src))), (v8i8  FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i8  (bitconvert (v1i64 FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v2i32 FPR64:$src))),
                             (v8i8 (REV32v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v4i16 FPR64:$src))),
                             (v8i8 (REV16v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (f64   FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v2f32 FPR64:$src))),
                             (v8i8 (REV32v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v1f64 FPR64:$src))),
                             (v8i8 (REV64v8i8 FPR64:$src))>;
def : Pat<(v8i8  (bitconvert (v4f16 FPR64:$src))),
                             (v8i8 (REV16v8i8 FPR64:$src))>;
}

let Predicates = [IsLE] in {
def : Pat<(f64   (bitconvert (v2i32 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v4i16 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v2f32 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v8i8  FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v4f16 FPR64:$src))), (f64   FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(f64   (bitconvert (v2i32 FPR64:$src))),
                             (f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v4i16 FPR64:$src))),
                             (f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v2f32 FPR64:$src))),
                             (f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v8i8  FPR64:$src))),
                             (f64 (REV64v8i8 FPR64:$src))>;
def : Pat<(f64   (bitconvert (v4f16 FPR64:$src))),
                             (f64 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(f64   (bitconvert (v1i64 FPR64:$src))), (f64   FPR64:$src)>;
def : Pat<(f64   (bitconvert (v1f64 FPR64:$src))), (f64   FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v1f64 (bitconvert (v2i32 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v4i16 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v8i8  FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v2f32 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (v4f16 FPR64:$src))), (v1f64 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v1f64 (bitconvert (v2i32 FPR64:$src))),
                             (v1f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v4i16 FPR64:$src))),
                             (v1f64 (REV64v4i16 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v8i8  FPR64:$src))),
                             (v1f64 (REV64v8i8 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v2f32 FPR64:$src))),
                             (v1f64 (REV64v2i32 FPR64:$src))>;
def : Pat<(v1f64 (bitconvert (v4f16 FPR64:$src))),
                             (v1f64 (REV64v4i16 FPR64:$src))>;
}
def : Pat<(v1f64 (bitconvert (v1i64 FPR64:$src))), (v1f64 FPR64:$src)>;
def : Pat<(v1f64 (bitconvert (f64   FPR64:$src))), (v1f64 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2f32 (bitconvert (v1i64 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v4i16 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v8i8  FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v1f64 FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (f64   FPR64:$src))), (v2f32 FPR64:$src)>;
def : Pat<(v2f32 (bitconvert (v4f16 FPR64:$src))), (v2f32 FPR64:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2f32 (bitconvert (v1i64 FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v4i16 FPR64:$src))),
                             (v2f32 (REV32v4i16 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v8i8  FPR64:$src))),
                             (v2f32 (REV32v8i8 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v1f64 FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (f64   FPR64:$src))),
                             (v2f32 (REV64v2i32 FPR64:$src))>;
def : Pat<(v2f32 (bitconvert (v4f16 FPR64:$src))),
                             (v2f32 (REV32v4i16 FPR64:$src))>;
}
def : Pat<(v2f32 (bitconvert (v2i32 FPR64:$src))), (v2f32 FPR64:$src)>;

let Predicates = [IsLE] in {
def : Pat<(f128 (bitconvert (v2i64 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v4i32 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v8i16 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v2f64 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v4f32 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v8f16 FPR128:$src))), (f128 FPR128:$src)>;
def : Pat<(f128 (bitconvert (v16i8 FPR128:$src))), (f128 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(f128 (bitconvert (v2i64 FPR128:$src))),
                            (f128 (EXTv16i8 FPR128:$src, FPR128:$src, (i32 8)))>;
def : Pat<(f128 (bitconvert (v4i32 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                            (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v8i16 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                            (REV64v8i16 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v8f16 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                            (REV64v8i16 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v2f64 FPR128:$src))),
                            (f128 (EXTv16i8 FPR128:$src, FPR128:$src, (i32 8)))>;
def : Pat<(f128 (bitconvert (v4f32 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                            (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(f128 (bitconvert (v16i8 FPR128:$src))),
                            (f128 (EXTv16i8 (REV64v16i8 FPR128:$src),
                                            (REV64v16i8 FPR128:$src), (i32 8)))>;
}

let Predicates = [IsLE] in {
def : Pat<(v2f64 (bitconvert (f128  FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4i32 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8i16 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v8f16 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v16i8 FPR128:$src))), (v2f64 FPR128:$src)>;
def : Pat<(v2f64 (bitconvert (v4f32 FPR128:$src))), (v2f64 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2f64 (bitconvert (f128  FPR128:$src))),
                             (v2f64 (EXTv16i8 FPR128:$src,
                                              FPR128:$src, (i32 8)))>;
def : Pat<(v2f64 (bitconvert (v4i32 FPR128:$src))),
                             (v2f64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v8i16 FPR128:$src))),
                             (v2f64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v8f16 FPR128:$src))),
                             (v2f64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v16i8 FPR128:$src))),
                             (v2f64 (REV64v16i8 FPR128:$src))>;
def : Pat<(v2f64 (bitconvert (v4f32 FPR128:$src))),
                             (v2f64 (REV64v4i32 FPR128:$src))>;
}
def : Pat<(v2f64 (bitconvert (v2i64 FPR128:$src))), (v2f64 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4f32 (bitconvert (f128  FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8i16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v8f16 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v16i8 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2i64 FPR128:$src))), (v4f32 FPR128:$src)>;
def : Pat<(v4f32 (bitconvert (v2f64 FPR128:$src))), (v4f32 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4f32 (bitconvert (f128  FPR128:$src))),
                             (v4f32 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                    (REV64v4i32 FPR128:$src), (i32 8)))>;
def : Pat<(v4f32 (bitconvert (v8i16 FPR128:$src))),
                             (v4f32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v8f16 FPR128:$src))),
                             (v4f32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v16i8 FPR128:$src))),
                             (v4f32 (REV32v16i8 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v2i64 FPR128:$src))),
                             (v4f32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4f32 (bitconvert (v2f64 FPR128:$src))),
                             (v4f32 (REV64v4i32 FPR128:$src))>;
}
def : Pat<(v4f32 (bitconvert (v4i32 FPR128:$src))), (v4f32 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v2i64 (bitconvert (f128  FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4i32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8i16 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v16i8 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v4f32 FPR128:$src))), (v2i64 FPR128:$src)>;
def : Pat<(v2i64 (bitconvert (v8f16 FPR128:$src))), (v2i64 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v2i64 (bitconvert (f128  FPR128:$src))),
                             (v2i64 (EXTv16i8 FPR128:$src,
                                              FPR128:$src, (i32 8)))>;
def : Pat<(v2i64 (bitconvert (v4i32 FPR128:$src))),
                             (v2i64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v8i16 FPR128:$src))),
                             (v2i64 (REV64v8i16 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v16i8 FPR128:$src))),
                             (v2i64 (REV64v16i8 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v4f32 FPR128:$src))),
                             (v2i64 (REV64v4i32 FPR128:$src))>;
def : Pat<(v2i64 (bitconvert (v8f16 FPR128:$src))),
                             (v2i64 (REV64v8i16 FPR128:$src))>;
}
def : Pat<(v2i64 (bitconvert (v2f64 FPR128:$src))), (v2i64 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v4i32 (bitconvert (f128  FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2i64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8i16 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v16i8 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v2f64 FPR128:$src))), (v4i32 FPR128:$src)>;
def : Pat<(v4i32 (bitconvert (v8f16 FPR128:$src))), (v4i32 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v4i32 (bitconvert (f128  FPR128:$src))),
                             (v4i32 (EXTv16i8 (REV64v4i32 FPR128:$src),
                                              (REV64v4i32 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v4i32 (bitconvert (v2i64 FPR128:$src))),
                             (v4i32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v8i16 FPR128:$src))),
                             (v4i32 (REV32v8i16 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v16i8 FPR128:$src))),
                             (v4i32 (REV32v16i8 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v2f64 FPR128:$src))),
                             (v4i32 (REV64v4i32 FPR128:$src))>;
def : Pat<(v4i32 (bitconvert (v8f16 FPR128:$src))),
                             (v4i32 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v4i32 (bitconvert (v4f32 FPR128:$src))), (v4i32 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8i16 (bitconvert (f128  FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2i64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4i32 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v16i8 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v2f64 FPR128:$src))), (v8i16 FPR128:$src)>;
def : Pat<(v8i16 (bitconvert (v4f32 FPR128:$src))), (v8i16 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8i16 (bitconvert (f128  FPR128:$src))),
                             (v8i16 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                              (REV64v8i16 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v8i16 (bitconvert (v2i64 FPR128:$src))),
                             (v8i16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v4i32 FPR128:$src))),
                             (v8i16 (REV32v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v16i8 FPR128:$src))),
                             (v8i16 (REV16v16i8 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v2f64 FPR128:$src))),
                             (v8i16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8i16 (bitconvert (v4f32 FPR128:$src))),
                             (v8i16 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v8i16 (bitconvert (v8f16 FPR128:$src))), (v8i16 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v8f16 (bitconvert (f128  FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v2i64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v4i32 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v16i8 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v2f64 FPR128:$src))), (v8f16 FPR128:$src)>;
def : Pat<(v8f16 (bitconvert (v4f32 FPR128:$src))), (v8f16 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v8f16 (bitconvert (f128  FPR128:$src))),
                             (v8f16 (EXTv16i8 (REV64v8i16 FPR128:$src),
                                              (REV64v8i16 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v8f16 (bitconvert (v2i64 FPR128:$src))),
                             (v8f16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v4i32 FPR128:$src))),
                             (v8f16 (REV32v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v16i8 FPR128:$src))),
                             (v8f16 (REV16v16i8 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v2f64 FPR128:$src))),
                             (v8f16 (REV64v8i16 FPR128:$src))>;
def : Pat<(v8f16 (bitconvert (v4f32 FPR128:$src))),
                             (v8f16 (REV32v8i16 FPR128:$src))>;
}
def : Pat<(v8f16 (bitconvert (v8i16 FPR128:$src))), (v8f16 FPR128:$src)>;

let Predicates = [IsLE] in {
def : Pat<(v16i8 (bitconvert (f128  FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2i64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4i32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v8i16 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v2f64 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v4f32 FPR128:$src))), (v16i8 FPR128:$src)>;
def : Pat<(v16i8 (bitconvert (v8f16 FPR128:$src))), (v16i8 FPR128:$src)>;
}
let Predicates = [IsBE] in {
def : Pat<(v16i8 (bitconvert (f128  FPR128:$src))),
                             (v16i8 (EXTv16i8 (REV64v16i8 FPR128:$src),
                                              (REV64v16i8 FPR128:$src),
                                              (i32 8)))>;
def : Pat<(v16i8 (bitconvert (v2i64 FPR128:$src))),
                             (v16i8 (REV64v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v4i32 FPR128:$src))),
                             (v16i8 (REV32v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v8i16 FPR128:$src))),
                             (v16i8 (REV16v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v2f64 FPR128:$src))),
                             (v16i8 (REV64v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v4f32 FPR128:$src))),
                             (v16i8 (REV32v16i8 FPR128:$src))>;
def : Pat<(v16i8 (bitconvert (v8f16 FPR128:$src))),
                             (v16i8 (REV16v16i8 FPR128:$src))>;
}

def : Pat<(v4i16 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v8i8 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v2f32 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v4f16 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v2i32 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v1i64 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;
def : Pat<(v1f64 (extract_subvector V128:$Rn, (i64 0))),
           (EXTRACT_SUBREG V128:$Rn, dsub)>;

def : Pat<(v8i8 (extract_subvector (v16i8 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v4i16 (extract_subvector (v8i16 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v2i32 (extract_subvector (v4i32 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;
def : Pat<(v1i64 (extract_subvector (v2i64 FPR128:$Rn), (i64 1))),
          (EXTRACT_SUBREG (DUPv2i64lane FPR128:$Rn, 1), dsub)>;

// A 64-bit subvector insert to the first 128-bit vector position
// is a subregister copy that needs no instruction.
multiclass InsertSubvectorUndef<ValueType Ty> {
  def : Pat<(insert_subvector undef, (v1i64 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v1f64 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v2f64 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v2i32 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v4i32 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v2f32 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v4i16 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v8i16 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v4f16 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v8f16 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
  def : Pat<(insert_subvector undef, (v8i8 FPR64:$src), (Ty 0)),
            (INSERT_SUBREG (v16i8 (IMPLICIT_DEF)), FPR64:$src, dsub)>;
}

defm : InsertSubvectorUndef<i32>;
defm : InsertSubvectorUndef<i64>;

// Use pair-wise add instructions when summing up the lanes for v2f64, v2i64
// or v2f32.
def : Pat<(i64 (add (vector_extract (v2i64 FPR128:$Rn), (i64 0)),
                    (vector_extract (v2i64 FPR128:$Rn), (i64 1)))),
           (i64 (ADDPv2i64p (v2i64 FPR128:$Rn)))>;
def : Pat<(f64 (fadd (vector_extract (v2f64 FPR128:$Rn), (i64 0)),
                     (vector_extract (v2f64 FPR128:$Rn), (i64 1)))),
           (f64 (FADDPv2i64p (v2f64 FPR128:$Rn)))>;
    // vector_extract on 64-bit vectors gets promoted to a 128 bit vector,
    // so we match on v4f32 here, not v2f32. This will also catch adding
    // the low two lanes of a true v4f32 vector.
def : Pat<(fadd (vector_extract (v4f32 FPR128:$Rn), (i64 0)),
                (vector_extract (v4f32 FPR128:$Rn), (i64 1))),
          (f32 (FADDPv2i32p (EXTRACT_SUBREG FPR128:$Rn, dsub)))>;

// Scalar 64-bit shifts in FPR64 registers.
def : Pat<(i64 (int_aarch64_neon_sshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (SSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_ushl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (USHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_srshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (SRSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;
def : Pat<(i64 (int_aarch64_neon_urshl (i64 FPR64:$Rn), (i64 FPR64:$Rm))),
          (URSHLv1i64 FPR64:$Rn, FPR64:$Rm)>;

// Patterns for nontemporal/no-allocate stores.
// We have to resort to tricks to turn a single-input store into a store pair,
// because there is no single-input nontemporal store, only STNP.
let Predicates = [IsLE] in {
let AddedComplexity = 15 in {
class NTStore128Pat<ValueType VT> :
  Pat<(nontemporalstore (VT FPR128:$Rt),
        (am_indexed7s64 GPR64sp:$Rn, simm7s8:$offset)),
      (STNPDi (EXTRACT_SUBREG FPR128:$Rt, dsub),
              (CPYi64 FPR128:$Rt, (i64 1)),
              GPR64sp:$Rn, simm7s8:$offset)>;

def : NTStore128Pat<v2i64>;
def : NTStore128Pat<v4i32>;
def : NTStore128Pat<v8i16>;
def : NTStore128Pat<v16i8>;

class NTStore64Pat<ValueType VT> :
  Pat<(nontemporalstore (VT FPR64:$Rt),
        (am_indexed7s32 GPR64sp:$Rn, simm7s4:$offset)),
      (STNPSi (EXTRACT_SUBREG FPR64:$Rt, ssub),
              (CPYi32 (SUBREG_TO_REG (i64 0), FPR64:$Rt, dsub), (i64 1)),
              GPR64sp:$Rn, simm7s4:$offset)>;

// FIXME: Shouldn't v1f64 loads/stores be promoted to v1i64?
def : NTStore64Pat<v1f64>;
def : NTStore64Pat<v1i64>;
def : NTStore64Pat<v2i32>;
def : NTStore64Pat<v4i16>;
def : NTStore64Pat<v8i8>;

def : Pat<(nontemporalstore GPR64:$Rt,
            (am_indexed7s32 GPR64sp:$Rn, simm7s4:$offset)),
          (STNPWi (EXTRACT_SUBREG GPR64:$Rt, sub_32),
                  (EXTRACT_SUBREG (UBFMXri GPR64:$Rt, 32, 63), sub_32),
                  GPR64sp:$Rn, simm7s4:$offset)>;
} // AddedComplexity=10
} // Predicates = [IsLE]

// Tail call return handling. These are all compiler pseudo-instructions,
// so no encoding information or anything like that.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP] in {
  def TCRETURNdi : Pseudo<(outs), (ins i64imm:$dst, i32imm:$FPDiff), []>,
                   Sched<[WriteBrReg]>;
  def TCRETURNri : Pseudo<(outs), (ins tcGPR64:$dst, i32imm:$FPDiff), []>,
                   Sched<[WriteBrReg]>;
  // Indirect tail-call with any register allowed, used by MachineOutliner when
  // this is proven safe.
  // FIXME: If we have to add any more hacks like this, we should instead relax
  // some verifier checks for outlined functions.
  def TCRETURNriALL : Pseudo<(outs), (ins GPR64:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;
  // Indirect tail-call limited to only use registers (x16 and x17) which are
  // allowed to tail-call a "BTI c" instruction.
  def TCRETURNriBTI : Pseudo<(outs), (ins rtcGPR64:$dst, i32imm:$FPDiff), []>,
                      Sched<[WriteBrReg]>;
}

def : Pat<(AArch64tcret tcGPR64:$dst, (i32 timm:$FPDiff)),
          (TCRETURNri tcGPR64:$dst, imm:$FPDiff)>,
      Requires<[NotUseBTI]>;
def : Pat<(AArch64tcret rtcGPR64:$dst, (i32 timm:$FPDiff)),
          (TCRETURNriBTI rtcGPR64:$dst, imm:$FPDiff)>,
      Requires<[UseBTI]>;
def : Pat<(AArch64tcret tglobaladdr:$dst, (i32 timm:$FPDiff)),
          (TCRETURNdi texternalsym:$dst, imm:$FPDiff)>;
def : Pat<(AArch64tcret texternalsym:$dst, (i32 timm:$FPDiff)),
          (TCRETURNdi texternalsym:$dst, imm:$FPDiff)>;

def MOVMCSym : Pseudo<(outs GPR64:$dst), (ins i64imm:$sym), []>, Sched<[]>;
def : Pat<(i64 (AArch64LocalRecover mcsym:$sym)), (MOVMCSym mcsym:$sym)>;

include "AArch64InstrAtomics.td"
include "AArch64SVEInstrInfo.td"
