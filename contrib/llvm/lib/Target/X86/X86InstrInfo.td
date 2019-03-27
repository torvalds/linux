//===-- X86InstrInfo.td - Main X86 Instruction Definition --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 instruction set, defining the instructions, and
// properties of the instructions which are needed for code generation, machine
// code emission, and analysis.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// X86 specific DAG Nodes.
//

def SDTX86CmpTest : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisSameAs<1, 2>]>;

def SDTX86Cmps : SDTypeProfile<1, 3, [SDTCisFP<0>, SDTCisSameAs<1, 2>, SDTCisVT<3, i8>]>;
//def SDTX86Cmpss : SDTypeProfile<1, 3, [SDTCisVT<0, f32>, SDTCisSameAs<1, 2>, SDTCisVT<3, i8>]>;

def SDTX86Cmov    : SDTypeProfile<1, 4,
                                  [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>,
                                   SDTCisVT<3, i8>, SDTCisVT<4, i32>]>;

// Unary and binary operator instructions that set EFLAGS as a side-effect.
def SDTUnaryArithWithFlags : SDTypeProfile<2, 1,
                                           [SDTCisSameAs<0, 2>,
                                            SDTCisInt<0>, SDTCisVT<1, i32>]>;

def SDTBinaryArithWithFlags : SDTypeProfile<2, 2,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>, SDTCisVT<1, i32>]>;

// SDTBinaryArithWithFlagsInOut - RES1, EFLAGS = op LHS, RHS, EFLAGS
def SDTBinaryArithWithFlagsInOut : SDTypeProfile<2, 3,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<4, i32>]>;
// RES1, RES2, FLAGS = op LHS, RHS
def SDT2ResultBinaryArithWithFlags : SDTypeProfile<3, 2,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>, SDTCisVT<1, i32>]>;
def SDTX86BrCond  : SDTypeProfile<0, 3,
                                  [SDTCisVT<0, OtherVT>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;

def SDTX86SetCC   : SDTypeProfile<1, 2,
                                  [SDTCisVT<0, i8>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;
def SDTX86SetCC_C : SDTypeProfile<1, 2,
                                  [SDTCisInt<0>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;

def SDTX86sahf : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVT<1, i8>]>;

def SDTX86rdrand : SDTypeProfile<2, 0, [SDTCisInt<0>, SDTCisVT<1, i32>]>;

def SDTX86cas : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisInt<1>,
                                     SDTCisVT<2, i8>]>;
def SDTX86caspair : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDTX86caspairSaveEbx8 : SDTypeProfile<1, 3,
                                          [SDTCisVT<0, i32>, SDTCisPtrTy<1>,
                                          SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
def SDTX86caspairSaveRbx16 : SDTypeProfile<1, 3,
                                           [SDTCisVT<0, i64>, SDTCisPtrTy<1>,
                                           SDTCisVT<2, i64>, SDTCisVT<3, i64>]>;

def SDTLockBinaryArithWithFlags : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                                       SDTCisPtrTy<1>,
                                                       SDTCisInt<2>]>;

def SDTLockUnaryArithWithFlags : SDTypeProfile<1, 1, [SDTCisVT<0, i32>,
                                                      SDTCisPtrTy<1>]>;

def SDTX86Ret     : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;

def SDT_X86CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                          SDTCisVT<1, i32>]>;
def SDT_X86CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>,
                                        SDTCisVT<1, i32>]>;

def SDT_X86Call   : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def SDT_X86NtBrind : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def SDT_X86VASTART_SAVE_XMM_REGS : SDTypeProfile<0, -1, [SDTCisVT<0, i8>,
                                                         SDTCisVT<1, iPTR>,
                                                         SDTCisVT<2, iPTR>]>;

def SDT_X86VAARG_64 : SDTypeProfile<1, -1, [SDTCisPtrTy<0>,
                                            SDTCisPtrTy<1>,
                                            SDTCisVT<2, i32>,
                                            SDTCisVT<3, i8>,
                                            SDTCisVT<4, i32>]>;

def SDTX86RepStr  : SDTypeProfile<0, 1, [SDTCisVT<0, OtherVT>]>;

def SDTX86Void    : SDTypeProfile<0, 0, []>;

def SDTX86Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

def SDT_X86TLSADDR : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TLSBASEADDR : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TLSCALL : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86WIN_ALLOCA : SDTypeProfile<0, 1, [SDTCisVT<0, iPTR>]>;

def SDT_X86SEG_ALLOCA : SDTypeProfile<1, 1, [SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;

def SDT_X86EHRET : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TCRET : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisVT<1, i32>]>;

def SDT_X86MEMBARRIER : SDTypeProfile<0, 0, []>;

def X86MemBarrier : SDNode<"X86ISD::MEMBARRIER", SDT_X86MEMBARRIER,
                            [SDNPHasChain,SDNPSideEffect]>;
def X86MFence : SDNode<"X86ISD::MFENCE", SDT_X86MEMBARRIER,
                        [SDNPHasChain]>;


def X86bsf     : SDNode<"X86ISD::BSF",      SDTUnaryArithWithFlags>;
def X86bsr     : SDNode<"X86ISD::BSR",      SDTUnaryArithWithFlags>;
def X86shld    : SDNode<"X86ISD::SHLD",     SDTIntShiftDOp>;
def X86shrd    : SDNode<"X86ISD::SHRD",     SDTIntShiftDOp>;

def X86cmp     : SDNode<"X86ISD::CMP" ,     SDTX86CmpTest>;
def X86bt      : SDNode<"X86ISD::BT",       SDTX86CmpTest>;

def X86cmov    : SDNode<"X86ISD::CMOV",     SDTX86Cmov>;
def X86brcond  : SDNode<"X86ISD::BRCOND",   SDTX86BrCond,
                        [SDNPHasChain]>;
def X86setcc   : SDNode<"X86ISD::SETCC",    SDTX86SetCC>;
def X86setcc_c : SDNode<"X86ISD::SETCC_CARRY", SDTX86SetCC_C>;

def X86sahf    : SDNode<"X86ISD::SAHF",     SDTX86sahf>;

def X86rdrand  : SDNode<"X86ISD::RDRAND",   SDTX86rdrand,
                        [SDNPHasChain, SDNPSideEffect]>;

def X86rdseed  : SDNode<"X86ISD::RDSEED",   SDTX86rdrand,
                        [SDNPHasChain, SDNPSideEffect]>;

def X86cas : SDNode<"X86ISD::LCMPXCHG_DAG", SDTX86cas,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;
def X86cas8 : SDNode<"X86ISD::LCMPXCHG8_DAG", SDTX86caspair,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;
def X86cas16 : SDNode<"X86ISD::LCMPXCHG16_DAG", SDTX86caspair,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;
def X86cas8save_ebx : SDNode<"X86ISD::LCMPXCHG8_SAVE_EBX_DAG",
                                SDTX86caspairSaveEbx8,
                                [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                                SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def X86cas16save_rbx : SDNode<"X86ISD::LCMPXCHG16_SAVE_RBX_DAG",
                                SDTX86caspairSaveRbx16,
                                [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                                SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;

def X86retflag : SDNode<"X86ISD::RET_FLAG", SDTX86Ret,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def X86iret : SDNode<"X86ISD::IRET", SDTX86Ret,
                        [SDNPHasChain, SDNPOptInGlue]>;

def X86vastart_save_xmm_regs :
                 SDNode<"X86ISD::VASTART_SAVE_XMM_REGS",
                        SDT_X86VASTART_SAVE_XMM_REGS,
                        [SDNPHasChain, SDNPVariadic]>;
def X86vaarg64 :
                 SDNode<"X86ISD::VAARG_64", SDT_X86VAARG_64,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                         SDNPMemOperand]>;
def X86callseq_start :
                 SDNode<"ISD::CALLSEQ_START", SDT_X86CallSeqStart,
                        [SDNPHasChain, SDNPOutGlue]>;
def X86callseq_end :
                 SDNode<"ISD::CALLSEQ_END",   SDT_X86CallSeqEnd,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86call    : SDNode<"X86ISD::CALL",     SDT_X86Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                         SDNPVariadic]>;

def X86NoTrackCall : SDNode<"X86ISD::NT_CALL", SDT_X86Call,
                            [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                             SDNPVariadic]>;
def X86NoTrackBrind : SDNode<"X86ISD::NT_BRIND", SDT_X86NtBrind,
                             [SDNPHasChain]>;

def X86rep_stos: SDNode<"X86ISD::REP_STOS", SDTX86RepStr,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore]>;
def X86rep_movs: SDNode<"X86ISD::REP_MOVS", SDTX86RepStr,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad]>;

def X86rdtsc   : SDNode<"X86ISD::RDTSC_DAG", SDTX86Void,
                        [SDNPHasChain, SDNPOutGlue, SDNPSideEffect]>;
def X86rdtscp  : SDNode<"X86ISD::RDTSCP_DAG", SDTX86Void,
                        [SDNPHasChain, SDNPOutGlue, SDNPSideEffect]>;
def X86rdpmc   : SDNode<"X86ISD::RDPMC_DAG", SDTX86Void,
                        [SDNPHasChain, SDNPOutGlue, SDNPSideEffect]>;

def X86Wrapper    : SDNode<"X86ISD::Wrapper",     SDTX86Wrapper>;
def X86WrapperRIP : SDNode<"X86ISD::WrapperRIP",  SDTX86Wrapper>;

def X86RecoverFrameAlloc : SDNode<"ISD::LOCAL_RECOVER",
                                  SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                       SDTCisInt<1>]>>;

def X86tlsaddr : SDNode<"X86ISD::TLSADDR", SDT_X86TLSADDR,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86tlsbaseaddr : SDNode<"X86ISD::TLSBASEADDR", SDT_X86TLSBASEADDR,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86ehret : SDNode<"X86ISD::EH_RETURN", SDT_X86EHRET,
                        [SDNPHasChain]>;

def X86eh_sjlj_setjmp  : SDNode<"X86ISD::EH_SJLJ_SETJMP",
                                SDTypeProfile<1, 1, [SDTCisInt<0>,
                                                     SDTCisPtrTy<1>]>,
                                [SDNPHasChain, SDNPSideEffect]>;
def X86eh_sjlj_longjmp : SDNode<"X86ISD::EH_SJLJ_LONGJMP",
                                SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPSideEffect]>;
def X86eh_sjlj_setup_dispatch : SDNode<"X86ISD::EH_SJLJ_SETUP_DISPATCH",
                                       SDTypeProfile<0, 0, []>,
                                       [SDNPHasChain, SDNPSideEffect]>;

def X86tcret : SDNode<"X86ISD::TC_RETURN", SDT_X86TCRET,
                        [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def X86add_flag  : SDNode<"X86ISD::ADD",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86sub_flag  : SDNode<"X86ISD::SUB",  SDTBinaryArithWithFlags>;
def X86smul_flag : SDNode<"X86ISD::SMUL", SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86umul_flag : SDNode<"X86ISD::UMUL", SDT2ResultBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86adc_flag  : SDNode<"X86ISD::ADC",  SDTBinaryArithWithFlagsInOut>;
def X86sbb_flag  : SDNode<"X86ISD::SBB",  SDTBinaryArithWithFlagsInOut>;

def X86or_flag   : SDNode<"X86ISD::OR",   SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86xor_flag  : SDNode<"X86ISD::XOR",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86and_flag  : SDNode<"X86ISD::AND",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;

def X86lock_add  : SDNode<"X86ISD::LADD",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_sub  : SDNode<"X86ISD::LSUB",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_or  : SDNode<"X86ISD::LOR",  SDTLockBinaryArithWithFlags,
                         [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                          SDNPMemOperand]>;
def X86lock_xor  : SDNode<"X86ISD::LXOR",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_and  : SDNode<"X86ISD::LAND",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;

def X86bextr  : SDNode<"X86ISD::BEXTR",  SDTIntBinOp>;

def X86bzhi   : SDNode<"X86ISD::BZHI",   SDTIntBinOp>;

def X86mul_imm : SDNode<"X86ISD::MUL_IMM", SDTIntBinOp>;

def X86WinAlloca : SDNode<"X86ISD::WIN_ALLOCA", SDT_X86WIN_ALLOCA,
                          [SDNPHasChain, SDNPOutGlue]>;

def X86SegAlloca : SDNode<"X86ISD::SEG_ALLOCA", SDT_X86SEG_ALLOCA,
                          [SDNPHasChain]>;

def X86TLSCall : SDNode<"X86ISD::TLSCALL", SDT_X86TLSCALL,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86lwpins : SDNode<"X86ISD::LWPINS",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPSideEffect]>;

def X86umwait : SDNode<"X86ISD::UMWAIT",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPSideEffect]>;

def X86tpause : SDNode<"X86ISD::TPAUSE",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPSideEffect]>;

//===----------------------------------------------------------------------===//
// X86 Operand Definitions.
//

// A version of ptr_rc which excludes SP, ESP, and RSP. This is used for
// the index operand of an address, to conform to x86 encoding restrictions.
def ptr_rc_nosp : PointerLikeRegClass<1>;

// *mem - Operand definitions for the funky X86 addressing mode operands.
//
def X86MemAsmOperand : AsmOperandClass {
 let Name = "Mem";
}
let RenderMethod = "addMemOperands", SuperClasses = [X86MemAsmOperand] in {
  def X86Mem8AsmOperand   : AsmOperandClass { let Name = "Mem8"; }
  def X86Mem16AsmOperand  : AsmOperandClass { let Name = "Mem16"; }
  def X86Mem32AsmOperand  : AsmOperandClass { let Name = "Mem32"; }
  def X86Mem64AsmOperand  : AsmOperandClass { let Name = "Mem64"; }
  def X86Mem80AsmOperand  : AsmOperandClass { let Name = "Mem80"; }
  def X86Mem128AsmOperand : AsmOperandClass { let Name = "Mem128"; }
  def X86Mem256AsmOperand : AsmOperandClass { let Name = "Mem256"; }
  def X86Mem512AsmOperand : AsmOperandClass { let Name = "Mem512"; }
  // Gather mem operands
  def X86Mem64_RC128Operand  : AsmOperandClass { let Name = "Mem64_RC128"; }
  def X86Mem128_RC128Operand : AsmOperandClass { let Name = "Mem128_RC128"; }
  def X86Mem256_RC128Operand : AsmOperandClass { let Name = "Mem256_RC128"; }
  def X86Mem128_RC256Operand : AsmOperandClass { let Name = "Mem128_RC256"; }
  def X86Mem256_RC256Operand : AsmOperandClass { let Name = "Mem256_RC256"; }

  def X86Mem64_RC128XOperand  : AsmOperandClass { let Name = "Mem64_RC128X"; }
  def X86Mem128_RC128XOperand : AsmOperandClass { let Name = "Mem128_RC128X"; }
  def X86Mem256_RC128XOperand : AsmOperandClass { let Name = "Mem256_RC128X"; }
  def X86Mem128_RC256XOperand : AsmOperandClass { let Name = "Mem128_RC256X"; }
  def X86Mem256_RC256XOperand : AsmOperandClass { let Name = "Mem256_RC256X"; }
  def X86Mem512_RC256XOperand : AsmOperandClass { let Name = "Mem512_RC256X"; }
  def X86Mem256_RC512Operand  : AsmOperandClass { let Name = "Mem256_RC512"; }
  def X86Mem512_RC512Operand  : AsmOperandClass { let Name = "Mem512_RC512"; }
}

def X86AbsMemAsmOperand : AsmOperandClass {
  let Name = "AbsMem";
  let SuperClasses = [X86MemAsmOperand];
}

class X86MemOperand<string printMethod,
          AsmOperandClass parserMatchClass = X86MemAsmOperand> : Operand<iPTR> {
  let PrintMethod = printMethod;
  let MIOperandInfo = (ops ptr_rc, i8imm, ptr_rc_nosp, i32imm, SEGMENT_REG);
  let ParserMatchClass = parserMatchClass;
  let OperandType = "OPERAND_MEMORY";
}

// Gather mem operands
class X86VMemOperand<RegisterClass RC, string printMethod,
                     AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops ptr_rc, i8imm, RC, i32imm, SEGMENT_REG);
}

def anymem : X86MemOperand<"printanymem">;

// FIXME: Right now we allow any size during parsing, but we might want to
// restrict to only unsized memory.
def opaquemem : X86MemOperand<"printopaquemem">;

def i8mem   : X86MemOperand<"printi8mem",   X86Mem8AsmOperand>;
def i16mem  : X86MemOperand<"printi16mem",  X86Mem16AsmOperand>;
def i32mem  : X86MemOperand<"printi32mem",  X86Mem32AsmOperand>;
def i64mem  : X86MemOperand<"printi64mem",  X86Mem64AsmOperand>;
def i128mem : X86MemOperand<"printi128mem", X86Mem128AsmOperand>;
def i256mem : X86MemOperand<"printi256mem", X86Mem256AsmOperand>;
def i512mem : X86MemOperand<"printi512mem", X86Mem512AsmOperand>;
def f32mem  : X86MemOperand<"printf32mem",  X86Mem32AsmOperand>;
def f64mem  : X86MemOperand<"printf64mem",  X86Mem64AsmOperand>;
def f80mem  : X86MemOperand<"printf80mem",  X86Mem80AsmOperand>;
def f128mem : X86MemOperand<"printf128mem", X86Mem128AsmOperand>;
def f256mem : X86MemOperand<"printf256mem", X86Mem256AsmOperand>;
def f512mem : X86MemOperand<"printf512mem", X86Mem512AsmOperand>;

def v512mem : X86VMemOperand<VR512, "printf512mem", X86Mem512AsmOperand>;

// Gather mem operands
def vx64mem  : X86VMemOperand<VR128,  "printi64mem",  X86Mem64_RC128Operand>;
def vx128mem : X86VMemOperand<VR128,  "printi128mem", X86Mem128_RC128Operand>;
def vx256mem : X86VMemOperand<VR128,  "printi256mem", X86Mem256_RC128Operand>;
def vy128mem : X86VMemOperand<VR256,  "printi128mem", X86Mem128_RC256Operand>;
def vy256mem : X86VMemOperand<VR256,  "printi256mem", X86Mem256_RC256Operand>;

def vx64xmem  : X86VMemOperand<VR128X, "printi64mem",  X86Mem64_RC128XOperand>;
def vx128xmem : X86VMemOperand<VR128X, "printi128mem", X86Mem128_RC128XOperand>;
def vx256xmem : X86VMemOperand<VR128X, "printi256mem", X86Mem256_RC128XOperand>;
def vy128xmem : X86VMemOperand<VR256X, "printi128mem", X86Mem128_RC256XOperand>;
def vy256xmem : X86VMemOperand<VR256X, "printi256mem", X86Mem256_RC256XOperand>;
def vy512xmem : X86VMemOperand<VR256X, "printi512mem", X86Mem512_RC256XOperand>;
def vz256mem  : X86VMemOperand<VR512,  "printi256mem", X86Mem256_RC512Operand>;
def vz512mem  : X86VMemOperand<VR512,  "printi512mem", X86Mem512_RC512Operand>;

// A version of i8mem for use on x86-64 and x32 that uses a NOREX GPR instead
// of a plain GPR, so that it doesn't potentially require a REX prefix.
def ptr_rc_norex : PointerLikeRegClass<2>;
def ptr_rc_norex_nosp : PointerLikeRegClass<3>;

def i8mem_NOREX : Operand<iPTR> {
  let PrintMethod = "printi8mem";
  let MIOperandInfo = (ops ptr_rc_norex, i8imm, ptr_rc_norex_nosp, i32imm,
                       SEGMENT_REG);
  let ParserMatchClass = X86Mem8AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

// GPRs available for tailcall.
// It represents GR32_TC, GR64_TC or GR64_TCW64.
def ptr_rc_tailcall : PointerLikeRegClass<4>;

// Special i32mem for addresses of load folding tail calls. These are not
// allowed to use callee-saved registers since they must be scheduled
// after callee-saved register are popped.
def i32mem_TC : Operand<i32> {
  let PrintMethod = "printi32mem";
  let MIOperandInfo = (ops ptr_rc_tailcall, i8imm, ptr_rc_tailcall,
                       i32imm, SEGMENT_REG);
  let ParserMatchClass = X86Mem32AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

// Special i64mem for addresses of load folding tail calls. These are not
// allowed to use callee-saved registers since they must be scheduled
// after callee-saved register are popped.
def i64mem_TC : Operand<i64> {
  let PrintMethod = "printi64mem";
  let MIOperandInfo = (ops ptr_rc_tailcall, i8imm,
                       ptr_rc_tailcall, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86Mem64AsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

let OperandType = "OPERAND_PCREL",
    ParserMatchClass = X86AbsMemAsmOperand,
    PrintMethod = "printPCRelImm" in {
def i32imm_pcrel : Operand<i32>;
def i16imm_pcrel : Operand<i16>;

// Branch targets have OtherVT type and print as pc-relative values.
def brtarget : Operand<OtherVT>;
def brtarget8 : Operand<OtherVT>;

}

// Special parser to detect 16-bit mode to select 16-bit displacement.
def X86AbsMem16AsmOperand : AsmOperandClass {
  let Name = "AbsMem16";
  let RenderMethod = "addAbsMemOperands";
  let SuperClasses = [X86AbsMemAsmOperand];
}

// Branch targets have OtherVT type and print as pc-relative values.
let OperandType = "OPERAND_PCREL",
    PrintMethod = "printPCRelImm" in {
let ParserMatchClass = X86AbsMem16AsmOperand in
  def brtarget16 : Operand<OtherVT>;
let ParserMatchClass = X86AbsMemAsmOperand in
  def brtarget32 : Operand<OtherVT>;
}

let RenderMethod = "addSrcIdxOperands" in {
  def X86SrcIdx8Operand : AsmOperandClass {
    let Name = "SrcIdx8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86SrcIdx16Operand : AsmOperandClass {
    let Name = "SrcIdx16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86SrcIdx32Operand : AsmOperandClass {
    let Name = "SrcIdx32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86SrcIdx64Operand : AsmOperandClass {
    let Name = "SrcIdx64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
} // RenderMethod = "addSrcIdxOperands"

let RenderMethod = "addDstIdxOperands" in {
 def X86DstIdx8Operand : AsmOperandClass {
   let Name = "DstIdx8";
   let SuperClasses = [X86Mem8AsmOperand];
 }
 def X86DstIdx16Operand : AsmOperandClass {
   let Name = "DstIdx16";
   let SuperClasses = [X86Mem16AsmOperand];
 }
 def X86DstIdx32Operand : AsmOperandClass {
   let Name = "DstIdx32";
   let SuperClasses = [X86Mem32AsmOperand];
 }
 def X86DstIdx64Operand : AsmOperandClass {
   let Name = "DstIdx64";
   let SuperClasses = [X86Mem64AsmOperand];
 }
} // RenderMethod = "addDstIdxOperands"

let RenderMethod = "addMemOffsOperands" in {
  def X86MemOffs16_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs16_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs16_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs16_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs32_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs32_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs32_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs32_64AsmOperand : AsmOperandClass {
    let Name = "MemOffs32_64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
  def X86MemOffs64_8AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_8";
    let SuperClasses = [X86Mem8AsmOperand];
  }
  def X86MemOffs64_16AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_16";
    let SuperClasses = [X86Mem16AsmOperand];
  }
  def X86MemOffs64_32AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_32";
    let SuperClasses = [X86Mem32AsmOperand];
  }
  def X86MemOffs64_64AsmOperand : AsmOperandClass {
    let Name = "MemOffs64_64";
    let SuperClasses = [X86Mem64AsmOperand];
  }
} // RenderMethod = "addMemOffsOperands"

class X86SrcIdxOperand<string printMethod, AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops ptr_rc, SEGMENT_REG);
}

class X86DstIdxOperand<string printMethod, AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops ptr_rc);
}

def srcidx8  : X86SrcIdxOperand<"printSrcIdx8",  X86SrcIdx8Operand>;
def srcidx16 : X86SrcIdxOperand<"printSrcIdx16", X86SrcIdx16Operand>;
def srcidx32 : X86SrcIdxOperand<"printSrcIdx32", X86SrcIdx32Operand>;
def srcidx64 : X86SrcIdxOperand<"printSrcIdx64", X86SrcIdx64Operand>;
def dstidx8  : X86DstIdxOperand<"printDstIdx8",  X86DstIdx8Operand>;
def dstidx16 : X86DstIdxOperand<"printDstIdx16", X86DstIdx16Operand>;
def dstidx32 : X86DstIdxOperand<"printDstIdx32", X86DstIdx32Operand>;
def dstidx64 : X86DstIdxOperand<"printDstIdx64", X86DstIdx64Operand>;

class X86MemOffsOperand<Operand immOperand, string printMethod,
                        AsmOperandClass parserMatchClass>
    : X86MemOperand<printMethod, parserMatchClass> {
  let MIOperandInfo = (ops immOperand, SEGMENT_REG);
}

def offset16_8  : X86MemOffsOperand<i16imm, "printMemOffs8",
                                    X86MemOffs16_8AsmOperand>;
def offset16_16 : X86MemOffsOperand<i16imm, "printMemOffs16",
                                    X86MemOffs16_16AsmOperand>;
def offset16_32 : X86MemOffsOperand<i16imm, "printMemOffs32",
                                    X86MemOffs16_32AsmOperand>;
def offset32_8  : X86MemOffsOperand<i32imm, "printMemOffs8",
                                    X86MemOffs32_8AsmOperand>;
def offset32_16 : X86MemOffsOperand<i32imm, "printMemOffs16",
                                    X86MemOffs32_16AsmOperand>;
def offset32_32 : X86MemOffsOperand<i32imm, "printMemOffs32",
                                    X86MemOffs32_32AsmOperand>;
def offset32_64 : X86MemOffsOperand<i32imm, "printMemOffs64",
                                    X86MemOffs32_64AsmOperand>;
def offset64_8  : X86MemOffsOperand<i64imm, "printMemOffs8",
                                    X86MemOffs64_8AsmOperand>;
def offset64_16 : X86MemOffsOperand<i64imm, "printMemOffs16",
                                    X86MemOffs64_16AsmOperand>;
def offset64_32 : X86MemOffsOperand<i64imm, "printMemOffs32",
                                    X86MemOffs64_32AsmOperand>;
def offset64_64 : X86MemOffsOperand<i64imm, "printMemOffs64",
                                    X86MemOffs64_64AsmOperand>;

def SSECC : Operand<i8> {
  let PrintMethod = "printSSEAVXCC";
  let OperandType = "OPERAND_IMMEDIATE";
}

def AVXCC : Operand<i8> {
  let PrintMethod = "printSSEAVXCC";
  let OperandType = "OPERAND_IMMEDIATE";
}

def AVX512ICC : Operand<i8> {
  let PrintMethod = "printSSEAVXCC";
  let OperandType = "OPERAND_IMMEDIATE";
}

def XOPCC : Operand<i8> {
  let PrintMethod = "printXOPCC";
  let OperandType = "OPERAND_IMMEDIATE";
}

class ImmSExtAsmOperandClass : AsmOperandClass {
  let SuperClasses = [ImmAsmOperand];
  let RenderMethod = "addImmOperands";
}

def X86GR32orGR64AsmOperand : AsmOperandClass {
  let Name = "GR32orGR64";
}

def GR32orGR64 : RegisterOperand<GR32> {
  let ParserMatchClass = X86GR32orGR64AsmOperand;
}
def AVX512RCOperand : AsmOperandClass {
  let Name = "AVX512RC";
}
def AVX512RC : Operand<i32> {
  let PrintMethod = "printRoundingControl";
  let OperandType = "OPERAND_IMMEDIATE";
  let ParserMatchClass = AVX512RCOperand;
}

// Sign-extended immediate classes. We don't need to define the full lattice
// here because there is no instruction with an ambiguity between ImmSExti64i32
// and ImmSExti32i8.
//
// The strange ranges come from the fact that the assembler always works with
// 64-bit immediates, but for a 16-bit target value we want to accept both "-1"
// (which will be a -1ULL), and "0xFF" (-1 in 16-bits).

// [0, 0x7FFFFFFF]                                            |
//   [0xFFFFFFFF80000000, 0xFFFFFFFFFFFFFFFF]
def ImmSExti64i32AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti64i32";
}

// [0, 0x0000007F] | [0x000000000000FF80, 0x000000000000FFFF] |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti16i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti16i8";
  let SuperClasses = [ImmSExti64i32AsmOperand];
}

// [0, 0x0000007F] | [0x00000000FFFFFF80, 0x00000000FFFFFFFF] |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti32i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti32i8";
}

// [0, 0x0000007F]                                            |
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmSExti64i8AsmOperand : ImmSExtAsmOperandClass {
  let Name = "ImmSExti64i8";
  let SuperClasses = [ImmSExti16i8AsmOperand, ImmSExti32i8AsmOperand,
                      ImmSExti64i32AsmOperand];
}

// Unsigned immediate used by SSE/AVX instructions
// [0, 0xFF]
//   [0xFFFFFFFFFFFFFF80, 0xFFFFFFFFFFFFFFFF]
def ImmUnsignedi8AsmOperand : AsmOperandClass {
  let Name = "ImmUnsignedi8";
  let RenderMethod = "addImmOperands";
}

// A couple of more descriptive operand definitions.
// 16-bits but only 8 bits are significant.
def i16i8imm  : Operand<i16> {
  let ParserMatchClass = ImmSExti16i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}
// 32-bits but only 8 bits are significant.
def i32i8imm  : Operand<i32> {
  let ParserMatchClass = ImmSExti32i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bits but only 32 bits are significant.
def i64i32imm  : Operand<i64> {
  let ParserMatchClass = ImmSExti64i32AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bits but only 8 bits are significant.
def i64i8imm   : Operand<i64> {
  let ParserMatchClass = ImmSExti64i8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// Unsigned 8-bit immediate used by SSE/AVX instructions.
def u8imm : Operand<i8> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 32-bit immediate but only 8-bits are significant and they are unsigned.
// Used by some SSE/AVX instructions that use intrinsics.
def i32u8imm : Operand<i32> {
  let PrintMethod = "printU8Imm";
  let ParserMatchClass = ImmUnsignedi8AsmOperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// 64-bits but only 32 bits are significant, and those bits are treated as being
// pc relative.
def i64i32imm_pcrel : Operand<i64> {
  let PrintMethod = "printPCRelImm";
  let ParserMatchClass = X86AbsMemAsmOperand;
  let OperandType = "OPERAND_PCREL";
}

def lea64_32mem : Operand<i32> {
  let PrintMethod = "printanymem";
  let MIOperandInfo = (ops GR64, i8imm, GR64_NOSP, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86MemAsmOperand;
}

// Memory operands that use 64-bit pointers in both ILP32 and LP64.
def lea64mem : Operand<i64> {
  let PrintMethod = "printanymem";
  let MIOperandInfo = (ops GR64, i8imm, GR64_NOSP, i32imm, SEGMENT_REG);
  let ParserMatchClass = X86MemAsmOperand;
}


//===----------------------------------------------------------------------===//
// X86 Complex Pattern Definitions.
//

// Define X86-specific addressing mode.
def addr      : ComplexPattern<iPTR, 5, "selectAddr", [], [SDNPWantParent]>;
def lea32addr : ComplexPattern<i32, 5, "selectLEAAddr",
                               [add, sub, mul, X86mul_imm, shl, or, frameindex],
                               []>;
// In 64-bit mode 32-bit LEAs can use RIP-relative addressing.
def lea64_32addr : ComplexPattern<i32, 5, "selectLEA64_32Addr",
                                  [add, sub, mul, X86mul_imm, shl, or,
                                   frameindex, X86WrapperRIP],
                                  []>;

def tls32addr : ComplexPattern<i32, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def tls32baseaddr : ComplexPattern<i32, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def lea64addr : ComplexPattern<i64, 5, "selectLEAAddr",
                        [add, sub, mul, X86mul_imm, shl, or, frameindex,
                         X86WrapperRIP], []>;

def tls64addr : ComplexPattern<i64, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def tls64baseaddr : ComplexPattern<i64, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def vectoraddr : ComplexPattern<iPTR, 5, "selectVectorAddr", [],[SDNPWantParent]>;

// A relocatable immediate is either an immediate operand or an operand that can
// be relocated by the linker to an immediate, such as a regular symbol in
// non-PIC code.
def relocImm : ComplexPattern<iAny, 1, "selectRelocImm", [imm, X86Wrapper], [],
                              0>;

//===----------------------------------------------------------------------===//
// X86 Instruction Predicate Definitions.
def TruePredicate : Predicate<"true">;

def HasCMov      : Predicate<"Subtarget->hasCMov()">;
def NoCMov       : Predicate<"!Subtarget->hasCMov()">;

def HasMMX       : Predicate<"Subtarget->hasMMX()">;
def Has3DNow     : Predicate<"Subtarget->has3DNow()">;
def Has3DNowA    : Predicate<"Subtarget->has3DNowA()">;
def HasSSE1      : Predicate<"Subtarget->hasSSE1()">;
def UseSSE1      : Predicate<"Subtarget->hasSSE1() && !Subtarget->hasAVX()">;
def HasSSE2      : Predicate<"Subtarget->hasSSE2()">;
def UseSSE2      : Predicate<"Subtarget->hasSSE2() && !Subtarget->hasAVX()">;
def HasSSE3      : Predicate<"Subtarget->hasSSE3()">;
def UseSSE3      : Predicate<"Subtarget->hasSSE3() && !Subtarget->hasAVX()">;
def HasSSSE3     : Predicate<"Subtarget->hasSSSE3()">;
def UseSSSE3     : Predicate<"Subtarget->hasSSSE3() && !Subtarget->hasAVX()">;
def HasSSE41     : Predicate<"Subtarget->hasSSE41()">;
def NoSSE41      : Predicate<"!Subtarget->hasSSE41()">;
def UseSSE41     : Predicate<"Subtarget->hasSSE41() && !Subtarget->hasAVX()">;
def HasSSE42     : Predicate<"Subtarget->hasSSE42()">;
def UseSSE42     : Predicate<"Subtarget->hasSSE42() && !Subtarget->hasAVX()">;
def HasSSE4A     : Predicate<"Subtarget->hasSSE4A()">;
def NoAVX        : Predicate<"!Subtarget->hasAVX()">;
def HasAVX       : Predicate<"Subtarget->hasAVX()">;
def HasAVX2      : Predicate<"Subtarget->hasAVX2()">;
def HasAVX1Only  : Predicate<"Subtarget->hasAVX() && !Subtarget->hasAVX2()">;
def HasAVX512    : Predicate<"Subtarget->hasAVX512()">;
def UseAVX       : Predicate<"Subtarget->hasAVX() && !Subtarget->hasAVX512()">;
def UseAVX2      : Predicate<"Subtarget->hasAVX2() && !Subtarget->hasAVX512()">;
def NoAVX512     : Predicate<"!Subtarget->hasAVX512()">;
def HasCDI       : Predicate<"Subtarget->hasCDI()">;
def HasVPOPCNTDQ : Predicate<"Subtarget->hasVPOPCNTDQ()">;
def HasPFI       : Predicate<"Subtarget->hasPFI()">;
def HasERI       : Predicate<"Subtarget->hasERI()">;
def HasDQI       : Predicate<"Subtarget->hasDQI()">;
def NoDQI        : Predicate<"!Subtarget->hasDQI()">;
def HasBWI       : Predicate<"Subtarget->hasBWI()">;
def NoBWI        : Predicate<"!Subtarget->hasBWI()">;
def HasVLX       : Predicate<"Subtarget->hasVLX()">;
def NoVLX        : Predicate<"!Subtarget->hasVLX()">;
def NoVLX_Or_NoBWI : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasBWI()">;
def NoVLX_Or_NoDQI : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasDQI()">;
def PKU        : Predicate<"Subtarget->hasPKU()">;
def HasVNNI    : Predicate<"Subtarget->hasVNNI()">;

def HasBITALG    : Predicate<"Subtarget->hasBITALG()">;
def HasPOPCNT    : Predicate<"Subtarget->hasPOPCNT()">;
def HasAES       : Predicate<"Subtarget->hasAES()">;
def HasVAES      : Predicate<"Subtarget->hasVAES()">;
def NoVLX_Or_NoVAES : Predicate<"!Subtarget->hasVLX() || !Subtarget->hasVAES()">;
def HasFXSR      : Predicate<"Subtarget->hasFXSR()">;
def HasXSAVE     : Predicate<"Subtarget->hasXSAVE()">;
def HasXSAVEOPT  : Predicate<"Subtarget->hasXSAVEOPT()">;
def HasXSAVEC    : Predicate<"Subtarget->hasXSAVEC()">;
def HasXSAVES    : Predicate<"Subtarget->hasXSAVES()">;
def HasPCLMUL    : Predicate<"Subtarget->hasPCLMUL()">;
def NoVLX_Or_NoVPCLMULQDQ :
                    Predicate<"!Subtarget->hasVLX() || !Subtarget->hasVPCLMULQDQ()">;
def HasVPCLMULQDQ : Predicate<"Subtarget->hasVPCLMULQDQ()">;
def HasGFNI      : Predicate<"Subtarget->hasGFNI()">;
def HasFMA       : Predicate<"Subtarget->hasFMA()">;
def HasFMA4      : Predicate<"Subtarget->hasFMA4()">;
def NoFMA4       : Predicate<"!Subtarget->hasFMA4()">;
def HasXOP       : Predicate<"Subtarget->hasXOP()">;
def HasTBM       : Predicate<"Subtarget->hasTBM()">;
def NoTBM        : Predicate<"!Subtarget->hasTBM()">;
def HasLWP       : Predicate<"Subtarget->hasLWP()">;
def HasMOVBE     : Predicate<"Subtarget->hasMOVBE()">;
def HasRDRAND    : Predicate<"Subtarget->hasRDRAND()">;
def HasF16C      : Predicate<"Subtarget->hasF16C()">;
def HasFSGSBase  : Predicate<"Subtarget->hasFSGSBase()">;
def HasLZCNT     : Predicate<"Subtarget->hasLZCNT()">;
def HasBMI       : Predicate<"Subtarget->hasBMI()">;
def HasBMI2      : Predicate<"Subtarget->hasBMI2()">;
def NoBMI2       : Predicate<"!Subtarget->hasBMI2()">;
def HasVBMI      : Predicate<"Subtarget->hasVBMI()">;
def HasVBMI2     : Predicate<"Subtarget->hasVBMI2()">;
def HasIFMA      : Predicate<"Subtarget->hasIFMA()">;
def HasRTM       : Predicate<"Subtarget->hasRTM()">;
def HasADX       : Predicate<"Subtarget->hasADX()">;
def HasSHA       : Predicate<"Subtarget->hasSHA()">;
def HasSGX       : Predicate<"Subtarget->hasSGX()">;
def HasPRFCHW    : Predicate<"Subtarget->hasPRFCHW()">;
def HasRDSEED    : Predicate<"Subtarget->hasRDSEED()">;
def HasSSEPrefetch : Predicate<"Subtarget->hasSSEPrefetch()">;
def NoSSEPrefetch : Predicate<"!Subtarget->hasSSEPrefetch()">;
def HasPrefetchW : Predicate<"Subtarget->hasPRFCHW()">;
def HasPREFETCHWT1 : Predicate<"Subtarget->hasPREFETCHWT1()">;
def HasLAHFSAHF  : Predicate<"Subtarget->hasLAHFSAHF()">;
def HasMWAITX    : Predicate<"Subtarget->hasMWAITX()">;
def HasCLZERO    : Predicate<"Subtarget->hasCLZERO()">;
def HasCLDEMOTE  : Predicate<"Subtarget->hasCLDEMOTE()">;
def HasMOVDIRI   : Predicate<"Subtarget->hasMOVDIRI()">;
def HasMOVDIR64B : Predicate<"Subtarget->hasMOVDIR64B()">;
def HasPTWRITE   : Predicate<"Subtarget->hasPTWRITE()">;
def FPStackf32   : Predicate<"!Subtarget->hasSSE1()">;
def FPStackf64   : Predicate<"!Subtarget->hasSSE2()">;
def HasMPX       : Predicate<"Subtarget->hasMPX()">;
def HasSHSTK     : Predicate<"Subtarget->hasSHSTK()">;
def HasCLFLUSHOPT : Predicate<"Subtarget->hasCLFLUSHOPT()">;
def HasCLWB      : Predicate<"Subtarget->hasCLWB()">;
def HasWBNOINVD  : Predicate<"Subtarget->hasWBNOINVD()">;
def HasRDPID     : Predicate<"Subtarget->hasRDPID()">;
def HasWAITPKG   : Predicate<"Subtarget->hasWAITPKG()">;
def HasINVPCID   : Predicate<"Subtarget->hasINVPCID()">;
def HasCmpxchg16b: Predicate<"Subtarget->hasCmpxchg16b()">;
def HasPCONFIG   : Predicate<"Subtarget->hasPCONFIG()">;
def Not64BitMode : Predicate<"!Subtarget->is64Bit()">,
                             AssemblerPredicate<"!Mode64Bit", "Not 64-bit mode">;
def In64BitMode  : Predicate<"Subtarget->is64Bit()">,
                             AssemblerPredicate<"Mode64Bit", "64-bit mode">;
def IsLP64  : Predicate<"Subtarget->isTarget64BitLP64()">;
def NotLP64 : Predicate<"!Subtarget->isTarget64BitLP64()">;
def In16BitMode  : Predicate<"Subtarget->is16Bit()">,
                             AssemblerPredicate<"Mode16Bit", "16-bit mode">;
def Not16BitMode : Predicate<"!Subtarget->is16Bit()">,
                             AssemblerPredicate<"!Mode16Bit", "Not 16-bit mode">;
def In32BitMode  : Predicate<"Subtarget->is32Bit()">,
                             AssemblerPredicate<"Mode32Bit", "32-bit mode">;
def IsWin64      : Predicate<"Subtarget->isTargetWin64()">;
def NotWin64     : Predicate<"!Subtarget->isTargetWin64()">;
def NotWin64WithoutFP : Predicate<"!Subtarget->isTargetWin64() ||"
                                  "Subtarget->getFrameLowering()->hasFP(*MF)"> {
  let RecomputePerFunction = 1;
}
def IsPS4        : Predicate<"Subtarget->isTargetPS4()">;
def NotPS4       : Predicate<"!Subtarget->isTargetPS4()">;
def IsNaCl       : Predicate<"Subtarget->isTargetNaCl()">;
def NotNaCl      : Predicate<"!Subtarget->isTargetNaCl()">;
def SmallCode    : Predicate<"TM.getCodeModel() == CodeModel::Small">;
def KernelCode   : Predicate<"TM.getCodeModel() == CodeModel::Kernel">;
def NearData     : Predicate<"TM.getCodeModel() == CodeModel::Small ||"
                             "TM.getCodeModel() == CodeModel::Kernel">;
def IsNotPIC     : Predicate<"!TM.isPositionIndependent()">;

// We could compute these on a per-module basis but doing so requires accessing
// the Function object through the <Target>Subtarget and objections were raised
// to that (see post-commit review comments for r301750).
let RecomputePerFunction = 1 in {
  def OptForSize   : Predicate<"MF->getFunction().optForSize()">;
  def OptForMinSize : Predicate<"MF->getFunction().optForMinSize()">;
  def OptForSpeed  : Predicate<"!MF->getFunction().optForSize()">;
  def UseIncDec : Predicate<"!Subtarget->slowIncDec() || "
                            "MF->getFunction().optForSize()">;
  def NoSSE41_Or_OptForSize : Predicate<"MF->getFunction().optForSize() || "
                                        "!Subtarget->hasSSE41()">;
}

def CallImmAddr  : Predicate<"Subtarget->isLegalToCallImmediateAddr()">;
def FavorMemIndirectCall  : Predicate<"!Subtarget->slowTwoMemOps()">;
def HasFastMem32 : Predicate<"!Subtarget->isUnalignedMem32Slow()">;
def HasFastLZCNT : Predicate<"Subtarget->hasFastLZCNT()">;
def HasFastSHLDRotate : Predicate<"Subtarget->hasFastSHLDRotate()">;
def HasERMSB : Predicate<"Subtarget->hasERMSB()">;
def HasMFence    : Predicate<"Subtarget->hasMFence()">;
def UseRetpolineIndirectCalls : Predicate<"Subtarget->useRetpolineIndirectCalls()">;
def NotUseRetpolineIndirectCalls : Predicate<"!Subtarget->useRetpolineIndirectCalls()">;

//===----------------------------------------------------------------------===//
// X86 Instruction Format Definitions.
//

include "X86InstrFormats.td"

//===----------------------------------------------------------------------===//
// Pattern fragments.
//

// X86 specific condition code. These correspond to CondCode in
// X86InstrInfo.h. They must be kept in synch.
def X86_COND_A   : PatLeaf<(i8 0)>;  // alt. COND_NBE
def X86_COND_AE  : PatLeaf<(i8 1)>;  // alt. COND_NC
def X86_COND_B   : PatLeaf<(i8 2)>;  // alt. COND_C
def X86_COND_BE  : PatLeaf<(i8 3)>;  // alt. COND_NA
def X86_COND_E   : PatLeaf<(i8 4)>;  // alt. COND_Z
def X86_COND_G   : PatLeaf<(i8 5)>;  // alt. COND_NLE
def X86_COND_GE  : PatLeaf<(i8 6)>;  // alt. COND_NL
def X86_COND_L   : PatLeaf<(i8 7)>;  // alt. COND_NGE
def X86_COND_LE  : PatLeaf<(i8 8)>;  // alt. COND_NG
def X86_COND_NE  : PatLeaf<(i8 9)>;  // alt. COND_NZ
def X86_COND_NO  : PatLeaf<(i8 10)>;
def X86_COND_NP  : PatLeaf<(i8 11)>; // alt. COND_PO
def X86_COND_NS  : PatLeaf<(i8 12)>;
def X86_COND_O   : PatLeaf<(i8 13)>;
def X86_COND_P   : PatLeaf<(i8 14)>; // alt. COND_PE
def X86_COND_S   : PatLeaf<(i8 15)>;

def i16immSExt8  : ImmLeaf<i16, [{ return isInt<8>(Imm); }]>;
def i32immSExt8  : ImmLeaf<i32, [{ return isInt<8>(Imm); }]>;
def i64immSExt8  : ImmLeaf<i64, [{ return isInt<8>(Imm); }]>;
def i64immSExt32 : ImmLeaf<i64, [{ return isInt<32>(Imm); }]>;

// FIXME: Ideally we would just replace the above i*immSExt* matchers with
// relocImm-based matchers, but then FastISel would be unable to use them.
def i64relocImmSExt8 : PatLeaf<(i64 relocImm), [{
  return isSExtRelocImm<8>(N);
}]>;
def i64relocImmSExt32 : PatLeaf<(i64 relocImm), [{
  return isSExtRelocImm<32>(N);
}]>;

// If we have multiple users of an immediate, it's much smaller to reuse
// the register, rather than encode the immediate in every instruction.
// This has the risk of increasing register pressure from stretched live
// ranges, however, the immediates should be trivial to rematerialize by
// the RA in the event of high register pressure.
// TODO : This is currently enabled for stores and binary ops. There are more
// cases for which this can be enabled, though this catches the bulk of the
// issues.
// TODO2 : This should really also be enabled under O2, but there's currently
// an issue with RA where we don't pull the constants into their users
// when we rematerialize them. I'll follow-up on enabling O2 after we fix that
// issue.
// TODO3 : This is currently limited to single basic blocks (DAG creation
// pulls block immediates to the top and merges them if necessary).
// Eventually, it would be nice to allow ConstantHoisting to merge constants
// globally for potentially added savings.
//
def imm8_su : PatLeaf<(i8 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def imm16_su : PatLeaf<(i16 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def imm32_su : PatLeaf<(i32 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64immSExt32_su : PatLeaf<(i64immSExt32), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

def i16immSExt8_su : PatLeaf<(i16immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i32immSExt8_su : PatLeaf<(i32immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64immSExt8_su : PatLeaf<(i64immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

def i64relocImmSExt8_su : PatLeaf<(i64relocImmSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64relocImmSExt32_su : PatLeaf<(i64relocImmSExt32), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

// i64immZExt32 predicate - True if the 64-bit immediate fits in a 32-bit
// unsigned field.
def i64immZExt32 : ImmLeaf<i64, [{ return isUInt<32>(Imm); }]>;

def i64immZExt32SExt8 : ImmLeaf<i64, [{
  return isUInt<32>(Imm) && isInt<8>(static_cast<int32_t>(Imm));
}]>;

// Helper fragments for loads.

// It's safe to fold a zextload/extload from i1 as a regular i8 load. The
// upper bits are guaranteed to be zero and we were going to emit a MOV8rm
// which might get folded during peephole anyway.
def loadi8 : PatFrag<(ops node:$ptr), (i8 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  return ExtType == ISD::NON_EXTLOAD || ExtType == ISD::EXTLOAD ||
         ExtType == ISD::ZEXTLOAD;
}]>;

// It's always safe to treat a anyext i16 load as a i32 load if the i16 is
// known to be 32-bit aligned or better. Ditto for i8 to i16.
def loadi16 : PatFrag<(ops node:$ptr), (i16 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD)
    return LD->getAlignment() >= 2 && !LD->isVolatile();
  return false;
}]>;

def loadi32 : PatFrag<(ops node:$ptr), (i32 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD)
    return LD->getAlignment() >= 4 && !LD->isVolatile();
  return false;
}]>;

def loadi64  : PatFrag<(ops node:$ptr), (i64 (load node:$ptr))>;
def loadf32  : PatFrag<(ops node:$ptr), (f32 (load node:$ptr))>;
def loadf64  : PatFrag<(ops node:$ptr), (f64 (load node:$ptr))>;
def loadf80  : PatFrag<(ops node:$ptr), (f80 (load node:$ptr))>;
def loadf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr))>;
def alignedloadf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr)), [{
  LoadSDNode *Ld = cast<LoadSDNode>(N);
  return Ld->getAlignment() >= Ld->getMemoryVT().getStoreSize();
}]>;
def memopf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr)), [{
  LoadSDNode *Ld = cast<LoadSDNode>(N);
  return Subtarget->hasSSEUnalignedMem() ||
         Ld->getAlignment() >= Ld->getMemoryVT().getStoreSize();
}]>;

def sextloadi16i8  : PatFrag<(ops node:$ptr), (i16 (sextloadi8 node:$ptr))>;
def sextloadi32i8  : PatFrag<(ops node:$ptr), (i32 (sextloadi8 node:$ptr))>;
def sextloadi32i16 : PatFrag<(ops node:$ptr), (i32 (sextloadi16 node:$ptr))>;
def sextloadi64i8  : PatFrag<(ops node:$ptr), (i64 (sextloadi8 node:$ptr))>;
def sextloadi64i16 : PatFrag<(ops node:$ptr), (i64 (sextloadi16 node:$ptr))>;
def sextloadi64i32 : PatFrag<(ops node:$ptr), (i64 (sextloadi32 node:$ptr))>;

def zextloadi8i1   : PatFrag<(ops node:$ptr), (i8  (zextloadi1 node:$ptr))>;
def zextloadi16i1  : PatFrag<(ops node:$ptr), (i16 (zextloadi1 node:$ptr))>;
def zextloadi32i1  : PatFrag<(ops node:$ptr), (i32 (zextloadi1 node:$ptr))>;
def zextloadi16i8  : PatFrag<(ops node:$ptr), (i16 (zextloadi8 node:$ptr))>;
def zextloadi32i8  : PatFrag<(ops node:$ptr), (i32 (zextloadi8 node:$ptr))>;
def zextloadi32i16 : PatFrag<(ops node:$ptr), (i32 (zextloadi16 node:$ptr))>;
def zextloadi64i1  : PatFrag<(ops node:$ptr), (i64 (zextloadi1 node:$ptr))>;
def zextloadi64i8  : PatFrag<(ops node:$ptr), (i64 (zextloadi8 node:$ptr))>;
def zextloadi64i16 : PatFrag<(ops node:$ptr), (i64 (zextloadi16 node:$ptr))>;
def zextloadi64i32 : PatFrag<(ops node:$ptr), (i64 (zextloadi32 node:$ptr))>;

def extloadi8i1    : PatFrag<(ops node:$ptr), (i8  (extloadi1 node:$ptr))>;
def extloadi16i1   : PatFrag<(ops node:$ptr), (i16 (extloadi1 node:$ptr))>;
def extloadi32i1   : PatFrag<(ops node:$ptr), (i32 (extloadi1 node:$ptr))>;
def extloadi16i8   : PatFrag<(ops node:$ptr), (i16 (extloadi8 node:$ptr))>;
def extloadi32i8   : PatFrag<(ops node:$ptr), (i32 (extloadi8 node:$ptr))>;
def extloadi32i16  : PatFrag<(ops node:$ptr), (i32 (extloadi16 node:$ptr))>;
def extloadi64i1   : PatFrag<(ops node:$ptr), (i64 (extloadi1 node:$ptr))>;
def extloadi64i8   : PatFrag<(ops node:$ptr), (i64 (extloadi8 node:$ptr))>;
def extloadi64i16  : PatFrag<(ops node:$ptr), (i64 (extloadi16 node:$ptr))>;
def extloadi64i32  : PatFrag<(ops node:$ptr), (i64 (extloadi32 node:$ptr))>;


// An 'and' node with a single use.
def and_su : PatFrag<(ops node:$lhs, node:$rhs), (and node:$lhs, node:$rhs), [{
  return N->hasOneUse();
}]>;
// An 'srl' node with a single use.
def srl_su : PatFrag<(ops node:$lhs, node:$rhs), (srl node:$lhs, node:$rhs), [{
  return N->hasOneUse();
}]>;
// An 'trunc' node with a single use.
def trunc_su : PatFrag<(ops node:$src), (trunc node:$src), [{
  return N->hasOneUse();
}]>;

//===----------------------------------------------------------------------===//
// Instruction list.
//

// Nop
let hasSideEffects = 0, SchedRW = [WriteNop] in {
  def NOOP : I<0x90, RawFrm, (outs), (ins), "nop", []>;
  def NOOPW : I<0x1f, MRMXm, (outs), (ins i16mem:$zero),
                "nop{w}\t$zero", []>, TB, OpSize16, NotMemoryFoldable;
  def NOOPL : I<0x1f, MRMXm, (outs), (ins i32mem:$zero),
                "nop{l}\t$zero", []>, TB, OpSize32, NotMemoryFoldable;
  def NOOPQ : RI<0x1f, MRMXm, (outs), (ins i64mem:$zero),
                "nop{q}\t$zero", []>, TB, NotMemoryFoldable,
                Requires<[In64BitMode]>;
  // Also allow register so we can assemble/disassemble
  def NOOPWr : I<0x1f, MRMXr, (outs), (ins GR16:$zero),
                 "nop{w}\t$zero", []>, TB, OpSize16, NotMemoryFoldable;
  def NOOPLr : I<0x1f, MRMXr, (outs), (ins GR32:$zero),
                 "nop{l}\t$zero", []>, TB, OpSize32, NotMemoryFoldable;
  def NOOPQr : RI<0x1f, MRMXr, (outs), (ins GR64:$zero),
                  "nop{q}\t$zero", []>, TB, NotMemoryFoldable,
                  Requires<[In64BitMode]>;
}


// Constructing a stack frame.
def ENTER : Ii16<0xC8, RawFrmImm8, (outs), (ins i16imm:$len, i8imm:$lvl),
                 "enter\t$len, $lvl", []>, Sched<[WriteMicrocoded]>;

let SchedRW = [WriteALU] in {
let Defs = [EBP, ESP], Uses = [EBP, ESP], mayLoad = 1, hasSideEffects=0 in
def LEAVE    : I<0xC9, RawFrm, (outs), (ins), "leave", []>,
                 Requires<[Not64BitMode]>;

let Defs = [RBP,RSP], Uses = [RBP,RSP], mayLoad = 1, hasSideEffects = 0 in
def LEAVE64  : I<0xC9, RawFrm, (outs), (ins), "leave", []>,
                 Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
//  Miscellaneous Instructions.
//

let isBarrier = 1, hasSideEffects = 1, usesCustomInserter = 1,
    SchedRW = [WriteSystem] in
  def Int_eh_sjlj_setup_dispatch
    : PseudoI<(outs), (ins), [(X86eh_sjlj_setup_dispatch)]>;

let Defs = [ESP], Uses = [ESP], hasSideEffects=0 in {
let mayLoad = 1, SchedRW = [WriteLoad] in {
def POP16r  : I<0x58, AddRegFrm, (outs GR16:$reg), (ins), "pop{w}\t$reg", []>,
                OpSize16;
def POP32r  : I<0x58, AddRegFrm, (outs GR32:$reg), (ins), "pop{l}\t$reg", []>,
                OpSize32, Requires<[Not64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def POP16rmr: I<0x8F, MRM0r, (outs GR16:$reg), (ins), "pop{w}\t$reg", []>,
                OpSize16, NotMemoryFoldable;
def POP32rmr: I<0x8F, MRM0r, (outs GR32:$reg), (ins), "pop{l}\t$reg", []>,
                OpSize32, Requires<[Not64BitMode]>, NotMemoryFoldable;
} // isCodeGenOnly = 1, ForceDisassemble = 1
} // mayLoad, SchedRW
let mayStore = 1, mayLoad = 1, SchedRW = [WriteCopy] in {
def POP16rmm: I<0x8F, MRM0m, (outs), (ins i16mem:$dst), "pop{w}\t$dst", []>,
                OpSize16;
def POP32rmm: I<0x8F, MRM0m, (outs), (ins i32mem:$dst), "pop{l}\t$dst", []>,
                OpSize32, Requires<[Not64BitMode]>;
} // mayStore, mayLoad, SchedRW

let mayStore = 1, SchedRW = [WriteStore] in {
def PUSH16r  : I<0x50, AddRegFrm, (outs), (ins GR16:$reg), "push{w}\t$reg",[]>,
                 OpSize16;
def PUSH32r  : I<0x50, AddRegFrm, (outs), (ins GR32:$reg), "push{l}\t$reg",[]>,
                 OpSize32, Requires<[Not64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def PUSH16rmr: I<0xFF, MRM6r, (outs), (ins GR16:$reg), "push{w}\t$reg",[]>,
                 OpSize16, NotMemoryFoldable;
def PUSH32rmr: I<0xFF, MRM6r, (outs), (ins GR32:$reg), "push{l}\t$reg",[]>,
                 OpSize32, Requires<[Not64BitMode]>, NotMemoryFoldable;
} // isCodeGenOnly = 1, ForceDisassemble = 1

def PUSH16i8 : Ii8<0x6a, RawFrm, (outs), (ins i16i8imm:$imm),
                   "push{w}\t$imm", []>, OpSize16;
def PUSHi16  : Ii16<0x68, RawFrm, (outs), (ins i16imm:$imm),
                   "push{w}\t$imm", []>, OpSize16;

def PUSH32i8 : Ii8<0x6a, RawFrm, (outs), (ins i32i8imm:$imm),
                   "push{l}\t$imm", []>, OpSize32,
                   Requires<[Not64BitMode]>;
def PUSHi32  : Ii32<0x68, RawFrm, (outs), (ins i32imm:$imm),
                   "push{l}\t$imm", []>, OpSize32,
                   Requires<[Not64BitMode]>;
} // mayStore, SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in {
def PUSH16rmm: I<0xFF, MRM6m, (outs), (ins i16mem:$src), "push{w}\t$src", []>,
                 OpSize16;
def PUSH32rmm: I<0xFF, MRM6m, (outs), (ins i32mem:$src), "push{l}\t$src", []>,
                 OpSize32, Requires<[Not64BitMode]>;
} // mayLoad, mayStore, SchedRW

}

let mayLoad = 1, mayStore = 1, usesCustomInserter = 1,
    SchedRW = [WriteRMW], Defs = [ESP] in {
  let Uses = [ESP] in
  def RDFLAGS32 : PseudoI<(outs GR32:$dst), (ins),
                   [(set GR32:$dst, (int_x86_flags_read_u32))]>,
                Requires<[Not64BitMode]>;

  let Uses = [RSP] in
  def RDFLAGS64 : PseudoI<(outs GR64:$dst), (ins),
                   [(set GR64:$dst, (int_x86_flags_read_u64))]>,
                Requires<[In64BitMode]>;
}

let mayLoad = 1, mayStore = 1, usesCustomInserter = 1,
    SchedRW = [WriteRMW] in {
  let Defs = [ESP, EFLAGS, DF], Uses = [ESP] in
  def WRFLAGS32 : PseudoI<(outs), (ins GR32:$src),
                   [(int_x86_flags_write_u32 GR32:$src)]>,
                Requires<[Not64BitMode]>;

  let Defs = [RSP, EFLAGS, DF], Uses = [RSP] in
  def WRFLAGS64 : PseudoI<(outs), (ins GR64:$src),
                   [(int_x86_flags_write_u64 GR64:$src)]>,
                Requires<[In64BitMode]>;
}

let Defs = [ESP, EFLAGS, DF], Uses = [ESP], mayLoad = 1, hasSideEffects=0,
    SchedRW = [WriteLoad] in {
def POPF16   : I<0x9D, RawFrm, (outs), (ins), "popf{w}", []>, OpSize16;
def POPF32   : I<0x9D, RawFrm, (outs), (ins), "popf{l|d}", []>, OpSize32,
                 Requires<[Not64BitMode]>;
}

let Defs = [ESP], Uses = [ESP, EFLAGS, DF], mayStore = 1, hasSideEffects=0,
    SchedRW = [WriteStore] in {
def PUSHF16  : I<0x9C, RawFrm, (outs), (ins), "pushf{w}", []>, OpSize16;
def PUSHF32  : I<0x9C, RawFrm, (outs), (ins), "pushf{l|d}", []>, OpSize32,
                 Requires<[Not64BitMode]>;
}

let Defs = [RSP], Uses = [RSP], hasSideEffects=0 in {
let mayLoad = 1, SchedRW = [WriteLoad] in {
def POP64r   : I<0x58, AddRegFrm, (outs GR64:$reg), (ins), "pop{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def POP64rmr: I<0x8F, MRM0r, (outs GR64:$reg), (ins), "pop{q}\t$reg", []>,
                OpSize32, Requires<[In64BitMode]>, NotMemoryFoldable;
} // isCodeGenOnly = 1, ForceDisassemble = 1
} // mayLoad, SchedRW
let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in
def POP64rmm: I<0x8F, MRM0m, (outs), (ins i64mem:$dst), "pop{q}\t$dst", []>,
                OpSize32, Requires<[In64BitMode]>;
let mayStore = 1, SchedRW = [WriteStore] in {
def PUSH64r  : I<0x50, AddRegFrm, (outs), (ins GR64:$reg), "push{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def PUSH64rmr: I<0xFF, MRM6r, (outs), (ins GR64:$reg), "push{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>, NotMemoryFoldable;
} // isCodeGenOnly = 1, ForceDisassemble = 1
} // mayStore, SchedRW
let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in {
def PUSH64rmm: I<0xFF, MRM6m, (outs), (ins i64mem:$src), "push{q}\t$src", []>,
                 OpSize32, Requires<[In64BitMode]>;
} // mayLoad, mayStore, SchedRW
}

let Defs = [RSP], Uses = [RSP], hasSideEffects = 0, mayStore = 1,
    SchedRW = [WriteStore] in {
def PUSH64i8   : Ii8<0x6a, RawFrm, (outs), (ins i64i8imm:$imm),
                    "push{q}\t$imm", []>, OpSize32,
                    Requires<[In64BitMode]>;
def PUSH64i32  : Ii32S<0x68, RawFrm, (outs), (ins i64i32imm:$imm),
                    "push{q}\t$imm", []>, OpSize32,
                    Requires<[In64BitMode]>;
}

let Defs = [RSP, EFLAGS, DF], Uses = [RSP], mayLoad = 1, hasSideEffects=0 in
def POPF64   : I<0x9D, RawFrm, (outs), (ins), "popfq", []>,
               OpSize32, Requires<[In64BitMode]>, Sched<[WriteLoad]>;
let Defs = [RSP], Uses = [RSP, EFLAGS, DF], mayStore = 1, hasSideEffects=0 in
def PUSHF64    : I<0x9C, RawFrm, (outs), (ins), "pushfq", []>,
                 OpSize32, Requires<[In64BitMode]>, Sched<[WriteStore]>;

let Defs = [EDI, ESI, EBP, EBX, EDX, ECX, EAX, ESP], Uses = [ESP],
    mayLoad = 1, hasSideEffects = 0, SchedRW = [WriteLoad] in {
def POPA32   : I<0x61, RawFrm, (outs), (ins), "popal", []>,
               OpSize32, Requires<[Not64BitMode]>;
def POPA16   : I<0x61, RawFrm, (outs), (ins), "popaw", []>,
               OpSize16, Requires<[Not64BitMode]>;
}
let Defs = [ESP], Uses = [EDI, ESI, EBP, EBX, EDX, ECX, EAX, ESP],
    mayStore = 1, hasSideEffects = 0, SchedRW = [WriteStore] in {
def PUSHA32  : I<0x60, RawFrm, (outs), (ins), "pushal", []>,
               OpSize32, Requires<[Not64BitMode]>;
def PUSHA16  : I<0x60, RawFrm, (outs), (ins), "pushaw", []>,
               OpSize16, Requires<[Not64BitMode]>;
}

let Constraints = "$src = $dst", SchedRW = [WriteBSWAP32] in {
// This instruction is a consequence of BSWAP32r observing operand size. The
// encoding is valid, but the behavior is undefined.
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
def BSWAP16r_BAD : I<0xC8, AddRegFrm, (outs GR16:$dst), (ins GR16:$src),
                     "bswap{w}\t$dst", []>, OpSize16, TB;
// GR32 = bswap GR32
def BSWAP32r : I<0xC8, AddRegFrm, (outs GR32:$dst), (ins GR32:$src),
                 "bswap{l}\t$dst",
                 [(set GR32:$dst, (bswap GR32:$src))]>, OpSize32, TB;

let SchedRW = [WriteBSWAP64] in
def BSWAP64r : RI<0xC8, AddRegFrm, (outs GR64:$dst), (ins GR64:$src),
                  "bswap{q}\t$dst",
                  [(set GR64:$dst, (bswap GR64:$src))]>, TB;
} // Constraints = "$src = $dst", SchedRW

// Bit scan instructions.
let Defs = [EFLAGS] in {
def BSF16rr  : I<0xBC, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                 "bsf{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsf GR16:$src))]>,
                  PS, OpSize16, Sched<[WriteBSF]>;
def BSF16rm  : I<0xBC, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                 "bsf{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsf (loadi16 addr:$src)))]>,
                 PS, OpSize16, Sched<[WriteBSFLd]>;
def BSF32rr  : I<0xBC, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                 "bsf{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsf GR32:$src))]>,
                 PS, OpSize32, Sched<[WriteBSF]>;
def BSF32rm  : I<0xBC, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                 "bsf{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsf (loadi32 addr:$src)))]>,
                 PS, OpSize32, Sched<[WriteBSFLd]>;
def BSF64rr  : RI<0xBC, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                  "bsf{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsf GR64:$src))]>,
                  PS, Sched<[WriteBSF]>;
def BSF64rm  : RI<0xBC, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                  "bsf{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsf (loadi64 addr:$src)))]>,
                  PS, Sched<[WriteBSFLd]>;

def BSR16rr  : I<0xBD, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                 "bsr{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsr GR16:$src))]>,
                 PS, OpSize16, Sched<[WriteBSR]>;
def BSR16rm  : I<0xBD, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                 "bsr{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsr (loadi16 addr:$src)))]>,
                 PS, OpSize16, Sched<[WriteBSRLd]>;
def BSR32rr  : I<0xBD, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                 "bsr{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsr GR32:$src))]>,
                 PS, OpSize32, Sched<[WriteBSR]>;
def BSR32rm  : I<0xBD, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                 "bsr{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsr (loadi32 addr:$src)))]>,
                 PS, OpSize32, Sched<[WriteBSRLd]>;
def BSR64rr  : RI<0xBD, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                  "bsr{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsr GR64:$src))]>,
                  PS, Sched<[WriteBSR]>;
def BSR64rm  : RI<0xBD, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                  "bsr{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsr (loadi64 addr:$src)))]>,
                  PS, Sched<[WriteBSRLd]>;
} // Defs = [EFLAGS]

let SchedRW = [WriteMicrocoded] in {
let Defs = [EDI,ESI], Uses = [EDI,ESI,DF] in {
def MOVSB : I<0xA4, RawFrmDstSrc, (outs), (ins dstidx8:$dst, srcidx8:$src),
              "movsb\t{$src, $dst|$dst, $src}", []>;
def MOVSW : I<0xA5, RawFrmDstSrc, (outs), (ins dstidx16:$dst, srcidx16:$src),
              "movsw\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOVSL : I<0xA5, RawFrmDstSrc, (outs), (ins dstidx32:$dst, srcidx32:$src),
              "movs{l|d}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOVSQ : RI<0xA5, RawFrmDstSrc, (outs), (ins dstidx64:$dst, srcidx64:$src),
               "movsq\t{$src, $dst|$dst, $src}", []>,
               Requires<[In64BitMode]>;
}

let Defs = [EDI], Uses = [AL,EDI,DF] in
def STOSB : I<0xAA, RawFrmDst, (outs), (ins dstidx8:$dst),
              "stosb\t{%al, $dst|$dst, al}", []>;
let Defs = [EDI], Uses = [AX,EDI,DF] in
def STOSW : I<0xAB, RawFrmDst, (outs), (ins dstidx16:$dst),
              "stosw\t{%ax, $dst|$dst, ax}", []>, OpSize16;
let Defs = [EDI], Uses = [EAX,EDI,DF] in
def STOSL : I<0xAB, RawFrmDst, (outs), (ins dstidx32:$dst),
              "stos{l|d}\t{%eax, $dst|$dst, eax}", []>, OpSize32;
let Defs = [RDI], Uses = [RAX,RDI,DF] in
def STOSQ : RI<0xAB, RawFrmDst, (outs), (ins dstidx64:$dst),
               "stosq\t{%rax, $dst|$dst, rax}", []>,
               Requires<[In64BitMode]>;

let Defs = [EDI,EFLAGS], Uses = [AL,EDI,DF] in
def SCASB : I<0xAE, RawFrmDst, (outs), (ins dstidx8:$dst),
              "scasb\t{$dst, %al|al, $dst}", []>;
let Defs = [EDI,EFLAGS], Uses = [AX,EDI,DF] in
def SCASW : I<0xAF, RawFrmDst, (outs), (ins dstidx16:$dst),
              "scasw\t{$dst, %ax|ax, $dst}", []>, OpSize16;
let Defs = [EDI,EFLAGS], Uses = [EAX,EDI,DF] in
def SCASL : I<0xAF, RawFrmDst, (outs), (ins dstidx32:$dst),
              "scas{l|d}\t{$dst, %eax|eax, $dst}", []>, OpSize32;
let Defs = [EDI,EFLAGS], Uses = [RAX,EDI,DF] in
def SCASQ : RI<0xAF, RawFrmDst, (outs), (ins dstidx64:$dst),
               "scasq\t{$dst, %rax|rax, $dst}", []>,
               Requires<[In64BitMode]>;

let Defs = [EDI,ESI,EFLAGS], Uses = [EDI,ESI,DF] in {
def CMPSB : I<0xA6, RawFrmDstSrc, (outs), (ins dstidx8:$dst, srcidx8:$src),
              "cmpsb\t{$dst, $src|$src, $dst}", []>;
def CMPSW : I<0xA7, RawFrmDstSrc, (outs), (ins dstidx16:$dst, srcidx16:$src),
              "cmpsw\t{$dst, $src|$src, $dst}", []>, OpSize16;
def CMPSL : I<0xA7, RawFrmDstSrc, (outs), (ins dstidx32:$dst, srcidx32:$src),
              "cmps{l|d}\t{$dst, $src|$src, $dst}", []>, OpSize32;
def CMPSQ : RI<0xA7, RawFrmDstSrc, (outs), (ins dstidx64:$dst, srcidx64:$src),
               "cmpsq\t{$dst, $src|$src, $dst}", []>,
               Requires<[In64BitMode]>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
//  Move Instructions.
//
let SchedRW = [WriteMove] in {
let hasSideEffects = 0, isMoveReg = 1 in {
def MOV8rr  : I<0x88, MRMDestReg, (outs GR8 :$dst), (ins GR8 :$src),
                "mov{b}\t{$src, $dst|$dst, $src}", []>;
def MOV16rr : I<0x89, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32rr : I<0x89, MRMDestReg, (outs GR32:$dst), (ins GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOV64rr : RI<0x89, MRMDestReg, (outs GR64:$dst), (ins GR64:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}", []>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def MOV8ri  : Ii8 <0xB0, AddRegFrm, (outs GR8 :$dst), (ins i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}",
                   [(set GR8:$dst, imm:$src)]>;
def MOV16ri : Ii16<0xB8, AddRegFrm, (outs GR16:$dst), (ins i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}",
                   [(set GR16:$dst, imm:$src)]>, OpSize16;
def MOV32ri : Ii32<0xB8, AddRegFrm, (outs GR32:$dst), (ins i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}",
                   [(set GR32:$dst, relocImm:$src)]>, OpSize32;
def MOV64ri32 : RIi32S<0xC7, MRM0r, (outs GR64:$dst), (ins i64i32imm:$src),
                       "mov{q}\t{$src, $dst|$dst, $src}",
                       [(set GR64:$dst, i64immSExt32:$src)]>;
}
let isReMaterializable = 1, isMoveImm = 1 in {
def MOV64ri : RIi64<0xB8, AddRegFrm, (outs GR64:$dst), (ins i64imm:$src),
                    "movabs{q}\t{$src, $dst|$dst, $src}",
                    [(set GR64:$dst, relocImm:$src)]>;
}

// Longer forms that use a ModR/M byte. Needed for disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in {
def MOV8ri_alt  : Ii8 <0xC6, MRM0r, (outs GR8 :$dst), (ins i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}", []>,
                   FoldGenData<"MOV8ri">;
def MOV16ri_alt : Ii16<0xC7, MRM0r, (outs GR16:$dst), (ins i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16,
                   FoldGenData<"MOV16ri">;
def MOV32ri_alt : Ii32<0xC7, MRM0r, (outs GR32:$dst), (ins i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32,
                   FoldGenData<"MOV32ri">;
}
} // SchedRW

let SchedRW = [WriteStore] in {
def MOV8mi  : Ii8 <0xC6, MRM0m, (outs), (ins i8mem :$dst, i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}",
                   [(store (i8 imm8_su:$src), addr:$dst)]>;
def MOV16mi : Ii16<0xC7, MRM0m, (outs), (ins i16mem:$dst, i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}",
                   [(store (i16 imm16_su:$src), addr:$dst)]>, OpSize16;
def MOV32mi : Ii32<0xC7, MRM0m, (outs), (ins i32mem:$dst, i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}",
                   [(store (i32 imm32_su:$src), addr:$dst)]>, OpSize32;
def MOV64mi32 : RIi32S<0xC7, MRM0m, (outs), (ins i64mem:$dst, i64i32imm:$src),
                       "mov{q}\t{$src, $dst|$dst, $src}",
                       [(store i64immSExt32_su:$src, addr:$dst)]>,
                       Requires<[In64BitMode]>;
} // SchedRW

let hasSideEffects = 0 in {

/// Memory offset versions of moves. The immediate is an address mode sized
/// offset from the segment base.
let SchedRW = [WriteALU] in {
let mayLoad = 1 in {
let Defs = [AL] in
def MOV8ao32 : Ii32<0xA0, RawFrmMemOffs, (outs), (ins offset32_8:$src),
                    "mov{b}\t{$src, %al|al, $src}", []>,
                    AdSize32;
let Defs = [AX] in
def MOV16ao32 : Ii32<0xA1, RawFrmMemOffs, (outs), (ins offset32_16:$src),
                     "mov{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize32;
let Defs = [EAX] in
def MOV32ao32 : Ii32<0xA1, RawFrmMemOffs, (outs), (ins offset32_32:$src),
                     "mov{l}\t{$src, %eax|eax, $src}", []>,
                     OpSize32, AdSize32;
let Defs = [RAX] in
def MOV64ao32 : RIi32<0xA1, RawFrmMemOffs, (outs), (ins offset32_64:$src),
                      "mov{q}\t{$src, %rax|rax, $src}", []>,
                      AdSize32;

let Defs = [AL] in
def MOV8ao16 : Ii16<0xA0, RawFrmMemOffs, (outs), (ins offset16_8:$src),
                    "mov{b}\t{$src, %al|al, $src}", []>, AdSize16;
let Defs = [AX] in
def MOV16ao16 : Ii16<0xA1, RawFrmMemOffs, (outs), (ins offset16_16:$src),
                     "mov{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize16;
let Defs = [EAX] in
def MOV32ao16 : Ii16<0xA1, RawFrmMemOffs, (outs), (ins offset16_32:$src),
                     "mov{l}\t{$src, %eax|eax, $src}", []>,
                     AdSize16, OpSize32;
} // mayLoad
let mayStore = 1 in {
let Uses = [AL] in
def MOV8o32a : Ii32<0xA2, RawFrmMemOffs, (outs), (ins offset32_8:$dst),
                    "mov{b}\t{%al, $dst|$dst, al}", []>, AdSize32;
let Uses = [AX] in
def MOV16o32a : Ii32<0xA3, RawFrmMemOffs, (outs), (ins offset32_16:$dst),
                     "mov{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize32;
let Uses = [EAX] in
def MOV32o32a : Ii32<0xA3, RawFrmMemOffs, (outs), (ins offset32_32:$dst),
                     "mov{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize32;
let Uses = [RAX] in
def MOV64o32a : RIi32<0xA3, RawFrmMemOffs, (outs), (ins offset32_64:$dst),
                      "mov{q}\t{%rax, $dst|$dst, rax}", []>,
                      AdSize32;

let Uses = [AL] in
def MOV8o16a : Ii16<0xA2, RawFrmMemOffs, (outs), (ins offset16_8:$dst),
                    "mov{b}\t{%al, $dst|$dst, al}", []>, AdSize16;
let Uses = [AX] in
def MOV16o16a : Ii16<0xA3, RawFrmMemOffs, (outs), (ins offset16_16:$dst),
                     "mov{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize16;
let Uses = [EAX] in
def MOV32o16a : Ii16<0xA3, RawFrmMemOffs, (outs), (ins offset16_32:$dst),
                     "mov{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize16;
} // mayStore

// These forms all have full 64-bit absolute addresses in their instructions
// and use the movabs mnemonic to indicate this specific form.
let mayLoad = 1 in {
let Defs = [AL] in
def MOV8ao64 : Ii64<0xA0, RawFrmMemOffs, (outs), (ins offset64_8:$src),
                    "movabs{b}\t{$src, %al|al, $src}", []>,
                    AdSize64;
let Defs = [AX] in
def MOV16ao64 : Ii64<0xA1, RawFrmMemOffs, (outs), (ins offset64_16:$src),
                     "movabs{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize64;
let Defs = [EAX] in
def MOV32ao64 : Ii64<0xA1, RawFrmMemOffs, (outs), (ins offset64_32:$src),
                     "movabs{l}\t{$src, %eax|eax, $src}", []>,
                     OpSize32, AdSize64;
let Defs = [RAX] in
def MOV64ao64 : RIi64<0xA1, RawFrmMemOffs, (outs), (ins offset64_64:$src),
                     "movabs{q}\t{$src, %rax|rax, $src}", []>,
                     AdSize64;
} // mayLoad

let mayStore = 1 in {
let Uses = [AL] in
def MOV8o64a : Ii64<0xA2, RawFrmMemOffs, (outs), (ins offset64_8:$dst),
                    "movabs{b}\t{%al, $dst|$dst, al}", []>,
                    AdSize64;
let Uses = [AX] in
def MOV16o64a : Ii64<0xA3, RawFrmMemOffs, (outs), (ins offset64_16:$dst),
                     "movabs{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize64;
let Uses = [EAX] in
def MOV32o64a : Ii64<0xA3, RawFrmMemOffs, (outs), (ins offset64_32:$dst),
                     "movabs{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize64;
let Uses = [RAX] in
def MOV64o64a : RIi64<0xA3, RawFrmMemOffs, (outs), (ins offset64_64:$dst),
                     "movabs{q}\t{%rax, $dst|$dst, rax}", []>,
                     AdSize64;
} // mayStore
} // SchedRW
} // hasSideEffects = 0

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0,
    SchedRW = [WriteMove], isMoveReg = 1 in {
def MOV8rr_REV : I<0x8A, MRMSrcReg, (outs GR8:$dst), (ins GR8:$src),
                   "mov{b}\t{$src, $dst|$dst, $src}", []>,
                   FoldGenData<"MOV8rr">;
def MOV16rr_REV : I<0x8B, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                    "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16,
                    FoldGenData<"MOV16rr">;
def MOV32rr_REV : I<0x8B, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                    "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32,
                    FoldGenData<"MOV32rr">;
def MOV64rr_REV : RI<0x8B, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                     "mov{q}\t{$src, $dst|$dst, $src}", []>,
                     FoldGenData<"MOV64rr">;
}

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"mov{b}.s\t{$src, $dst|$dst, $src}",
                (MOV8rr_REV GR8:$dst, GR8:$src), 0>;
def : InstAlias<"mov{w}.s\t{$src, $dst|$dst, $src}",
                (MOV16rr_REV GR16:$dst, GR16:$src), 0>;
def : InstAlias<"mov{l}.s\t{$src, $dst|$dst, $src}",
                (MOV32rr_REV GR32:$dst, GR32:$src), 0>;
def : InstAlias<"mov{q}.s\t{$src, $dst|$dst, $src}",
                (MOV64rr_REV GR64:$dst, GR64:$src), 0>;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV8rr_REV GR8:$dst, GR8:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV16rr_REV GR16:$dst, GR16:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV32rr_REV GR32:$dst, GR32:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV64rr_REV GR64:$dst, GR64:$src), 0, "att">;

let canFoldAsLoad = 1, isReMaterializable = 1, SchedRW = [WriteLoad] in {
def MOV8rm  : I<0x8A, MRMSrcMem, (outs GR8 :$dst), (ins i8mem :$src),
                "mov{b}\t{$src, $dst|$dst, $src}",
                [(set GR8:$dst, (loadi8 addr:$src))]>;
def MOV16rm : I<0x8B, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                "mov{w}\t{$src, $dst|$dst, $src}",
                [(set GR16:$dst, (loadi16 addr:$src))]>, OpSize16;
def MOV32rm : I<0x8B, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                "mov{l}\t{$src, $dst|$dst, $src}",
                [(set GR32:$dst, (loadi32 addr:$src))]>, OpSize32;
def MOV64rm : RI<0x8B, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}",
                 [(set GR64:$dst, (load addr:$src))]>;
}

let SchedRW = [WriteStore] in {
def MOV8mr  : I<0x88, MRMDestMem, (outs), (ins i8mem :$dst, GR8 :$src),
                "mov{b}\t{$src, $dst|$dst, $src}",
                [(store GR8:$src, addr:$dst)]>;
def MOV16mr : I<0x89, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                "mov{w}\t{$src, $dst|$dst, $src}",
                [(store GR16:$src, addr:$dst)]>, OpSize16;
def MOV32mr : I<0x89, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}",
                [(store GR32:$src, addr:$dst)]>, OpSize32;
def MOV64mr : RI<0x89, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}",
                 [(store GR64:$src, addr:$dst)]>;
} // SchedRW

// Versions of MOV8rr, MOV8mr, and MOV8rm that use i8mem_NOREX and GR8_NOREX so
// that they can be used for copying and storing h registers, which can't be
// encoded when a REX prefix is present.
let isCodeGenOnly = 1 in {
let hasSideEffects = 0, isMoveReg = 1 in
def MOV8rr_NOREX : I<0x88, MRMDestReg,
                     (outs GR8_NOREX:$dst), (ins GR8_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                   Sched<[WriteMove]>;
let mayStore = 1, hasSideEffects = 0 in
def MOV8mr_NOREX : I<0x88, MRMDestMem,
                     (outs), (ins i8mem_NOREX:$dst, GR8_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                     Sched<[WriteStore]>;
let mayLoad = 1, hasSideEffects = 0,
    canFoldAsLoad = 1, isReMaterializable = 1 in
def MOV8rm_NOREX : I<0x8A, MRMSrcMem,
                     (outs GR8_NOREX:$dst), (ins i8mem_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                     Sched<[WriteLoad]>;
}


// Condition code ops, incl. set if equal/not equal/...
let SchedRW = [WriteLAHFSAHF] in {
let Defs = [EFLAGS], Uses = [AH] in
def SAHF     : I<0x9E, RawFrm, (outs),  (ins), "sahf",
                 [(set EFLAGS, (X86sahf AH))]>,
                 Requires<[HasLAHFSAHF]>;
let Defs = [AH], Uses = [EFLAGS], hasSideEffects = 0 in
def LAHF     : I<0x9F, RawFrm, (outs),  (ins), "lahf", []>,  // AH = flags
               Requires<[HasLAHFSAHF]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Bit tests instructions: BT, BTS, BTR, BTC.

let Defs = [EFLAGS] in {
let SchedRW = [WriteBitTest] in {
def BT16rr : I<0xA3, MRMDestReg, (outs), (ins GR16:$src1, GR16:$src2),
               "bt{w}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR16:$src1, GR16:$src2))]>,
               OpSize16, TB, NotMemoryFoldable;
def BT32rr : I<0xA3, MRMDestReg, (outs), (ins GR32:$src1, GR32:$src2),
               "bt{l}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR32:$src1, GR32:$src2))]>,
               OpSize32, TB, NotMemoryFoldable;
def BT64rr : RI<0xA3, MRMDestReg, (outs), (ins GR64:$src1, GR64:$src2),
               "bt{q}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR64:$src1, GR64:$src2))]>, TB,
               NotMemoryFoldable;
} // SchedRW

// Unlike with the register+register form, the memory+register form of the
// bt instruction does not ignore the high bits of the index. From ISel's
// perspective, this is pretty bizarre. Make these instructions disassembly
// only for now. These instructions are also slow on modern CPUs so that's
// another reason to avoid generating them.

let mayLoad = 1, hasSideEffects = 0, SchedRW = [WriteBitTestRegLd] in {
  def BT16mr : I<0xA3, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                 "bt{w}\t{$src2, $src1|$src1, $src2}",
                 []>, OpSize16, TB, NotMemoryFoldable;
  def BT32mr : I<0xA3, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                 "bt{l}\t{$src2, $src1|$src1, $src2}",
                 []>, OpSize32, TB, NotMemoryFoldable;
  def BT64mr : RI<0xA3, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "bt{q}\t{$src2, $src1|$src1, $src2}",
                  []>, TB, NotMemoryFoldable;
}

let SchedRW = [WriteBitTest] in {
def BT16ri8 : Ii8<0xBA, MRM4r, (outs), (ins GR16:$src1, i16i8imm:$src2),
                "bt{w}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR16:$src1, i16immSExt8:$src2))]>,
                OpSize16, TB;
def BT32ri8 : Ii8<0xBA, MRM4r, (outs), (ins GR32:$src1, i32i8imm:$src2),
                "bt{l}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR32:$src1, i32immSExt8:$src2))]>,
                OpSize32, TB;
def BT64ri8 : RIi8<0xBA, MRM4r, (outs), (ins GR64:$src1, i64i8imm:$src2),
                "bt{q}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR64:$src1, i64immSExt8:$src2))]>, TB;
} // SchedRW

// Note that these instructions aren't slow because that only applies when the
// other operand is in a register. When it's an immediate, bt is still fast.
let SchedRW = [WriteBitTestImmLd] in {
def BT16mi8 : Ii8<0xBA, MRM4m, (outs), (ins i16mem:$src1, i16i8imm:$src2),
                  "bt{w}\t{$src2, $src1|$src1, $src2}",
                  [(set EFLAGS, (X86bt (loadi16 addr:$src1),
                                       i16immSExt8:$src2))]>,
                  OpSize16, TB;
def BT32mi8 : Ii8<0xBA, MRM4m, (outs), (ins i32mem:$src1, i32i8imm:$src2),
                  "bt{l}\t{$src2, $src1|$src1, $src2}",
                  [(set EFLAGS, (X86bt (loadi32 addr:$src1),
                                       i32immSExt8:$src2))]>,
                  OpSize32, TB;
def BT64mi8 : RIi8<0xBA, MRM4m, (outs), (ins i64mem:$src1, i64i8imm:$src2),
                "bt{q}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt (loadi64 addr:$src1),
                                     i64immSExt8:$src2))]>, TB,
                Requires<[In64BitMode]>;
} // SchedRW

let hasSideEffects = 0 in {
let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTC16rr : I<0xBB, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "btc{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB, NotMemoryFoldable;
def BTC32rr : I<0xBB, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "btc{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB, NotMemoryFoldable;
def BTC64rr : RI<0xBB, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                 "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                 NotMemoryFoldable;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTC16mr : I<0xBB, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                "btc{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB, NotMemoryFoldable;
def BTC32mr : I<0xBB, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                "btc{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB, NotMemoryFoldable;
def BTC64mr : RI<0xBB, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                 NotMemoryFoldable;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTC16ri8 : Ii8<0xBA, MRM7r, (outs GR16:$dst), (ins GR16:$src1, i16i8imm:$src2),
                    "btc{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTC32ri8 : Ii8<0xBA, MRM7r, (outs GR32:$dst), (ins GR32:$src1, i32i8imm:$src2),
                    "btc{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTC64ri8 : RIi8<0xBA, MRM7r, (outs GR64:$dst), (ins GR64:$src1, i64i8imm:$src2),
                    "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTC16mi8 : Ii8<0xBA, MRM7m, (outs), (ins i16mem:$src1, i16i8imm:$src2),
                    "btc{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTC32mi8 : Ii8<0xBA, MRM7m, (outs), (ins i32mem:$src1, i32i8imm:$src2),
                    "btc{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTC64mi8 : RIi8<0xBA, MRM7m, (outs), (ins i64mem:$src1, i64i8imm:$src2),
                    "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTR16rr : I<0xB3, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB, NotMemoryFoldable;
def BTR32rr : I<0xB3, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB, NotMemoryFoldable;
def BTR64rr : RI<0xB3, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                 "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                 NotMemoryFoldable;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTR16mr : I<0xB3, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB, NotMemoryFoldable;
def BTR32mr : I<0xB3, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB, NotMemoryFoldable;
def BTR64mr : RI<0xB3, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                 NotMemoryFoldable;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTR16ri8 : Ii8<0xBA, MRM6r, (outs GR16:$dst), (ins GR16:$src1, i16i8imm:$src2),
                    "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize16, TB;
def BTR32ri8 : Ii8<0xBA, MRM6r, (outs GR32:$dst), (ins GR32:$src1, i32i8imm:$src2),
                    "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize32, TB;
def BTR64ri8 : RIi8<0xBA, MRM6r, (outs GR64:$dst), (ins GR64:$src1, i64i8imm:$src2),
                    "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTR16mi8 : Ii8<0xBA, MRM6m, (outs), (ins i16mem:$src1, i16i8imm:$src2),
                    "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize16, TB;
def BTR32mi8 : Ii8<0xBA, MRM6m, (outs), (ins i32mem:$src1, i32i8imm:$src2),
                    "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize32, TB;
def BTR64mi8 : RIi8<0xBA, MRM6m, (outs), (ins i64mem:$src1, i64i8imm:$src2),
                    "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTS16rr : I<0xAB, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "bts{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB, NotMemoryFoldable;
def BTS32rr : I<0xAB, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "bts{l}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize32, TB, NotMemoryFoldable;
def BTS64rr : RI<0xAB, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
               "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
               NotMemoryFoldable;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTS16mr : I<0xAB, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
              "bts{w}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize16, TB, NotMemoryFoldable;
def BTS32mr : I<0xAB, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
              "bts{l}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize32, TB, NotMemoryFoldable;
def BTS64mr : RI<0xAB, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                 NotMemoryFoldable;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTS16ri8 : Ii8<0xBA, MRM5r, (outs GR16:$dst), (ins GR16:$src1, i16i8imm:$src2),
                    "bts{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTS32ri8 : Ii8<0xBA, MRM5r, (outs GR32:$dst), (ins GR32:$src1, i32i8imm:$src2),
                    "bts{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTS64ri8 : RIi8<0xBA, MRM5r, (outs GR64:$dst), (ins GR64:$src1, i64i8imm:$src2),
                    "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTS16mi8 : Ii8<0xBA, MRM5m, (outs), (ins i16mem:$src1, i16i8imm:$src2),
                    "bts{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTS32mi8 : Ii8<0xBA, MRM5m, (outs), (ins i32mem:$src1, i32i8imm:$src2),
                    "bts{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTS64mi8 : RIi8<0xBA, MRM5m, (outs), (ins i64mem:$src1, i64i8imm:$src2),
                    "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}
} // hasSideEffects = 0
} // Defs = [EFLAGS]


//===----------------------------------------------------------------------===//
// Atomic support
//

// Atomic swap. These are just normal xchg instructions. But since a memory
// operand is referenced, the atomicity is ensured.
multiclass ATOMIC_SWAP<bits<8> opc8, bits<8> opc, string mnemonic, string frag> {
  let Constraints = "$val = $dst", SchedRW = [WriteALULd, WriteRMW] in {
    def NAME#8rm  : I<opc8, MRMSrcMem, (outs GR8:$dst),
                      (ins GR8:$val, i8mem:$ptr),
                      !strconcat(mnemonic, "{b}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR8:$dst,
                         (!cast<PatFrag>(frag # "_8") addr:$ptr, GR8:$val))]>;
    def NAME#16rm : I<opc, MRMSrcMem, (outs GR16:$dst),
                      (ins GR16:$val, i16mem:$ptr),
                      !strconcat(mnemonic, "{w}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR16:$dst,
                         (!cast<PatFrag>(frag # "_16") addr:$ptr, GR16:$val))]>,
                      OpSize16;
    def NAME#32rm : I<opc, MRMSrcMem, (outs GR32:$dst),
                      (ins GR32:$val, i32mem:$ptr),
                      !strconcat(mnemonic, "{l}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR32:$dst,
                         (!cast<PatFrag>(frag # "_32") addr:$ptr, GR32:$val))]>,
                      OpSize32;
    def NAME#64rm : RI<opc, MRMSrcMem, (outs GR64:$dst),
                       (ins GR64:$val, i64mem:$ptr),
                       !strconcat(mnemonic, "{q}\t{$val, $ptr|$ptr, $val}"),
                       [(set
                         GR64:$dst,
                         (!cast<PatFrag>(frag # "_64") addr:$ptr, GR64:$val))]>;
  }
}

defm XCHG    : ATOMIC_SWAP<0x86, 0x87, "xchg", "atomic_swap">, NotMemoryFoldable;

// Swap between registers.
let SchedRW = [WriteXCHG] in {
let Constraints = "$src1 = $dst1, $src2 = $dst2", hasSideEffects = 0 in {
def XCHG8rr : I<0x86, MRMSrcReg, (outs GR8:$dst1, GR8:$dst2),
                (ins GR8:$src1, GR8:$src2),
                "xchg{b}\t{$src2, $src1|$src1, $src2}", []>, NotMemoryFoldable;
def XCHG16rr : I<0x87, MRMSrcReg, (outs GR16:$dst1, GR16:$dst2),
                 (ins GR16:$src1, GR16:$src2),
                 "xchg{w}\t{$src2, $src1|$src1, $src2}", []>,
                 OpSize16, NotMemoryFoldable;
def XCHG32rr : I<0x87, MRMSrcReg, (outs GR32:$dst1, GR32:$dst2),
                 (ins GR32:$src1, GR32:$src2),
                 "xchg{l}\t{$src2, $src1|$src1, $src2}", []>,
                 OpSize32, NotMemoryFoldable;
def XCHG64rr : RI<0x87, MRMSrcReg, (outs GR64:$dst1, GR64:$dst2),
                  (ins GR64:$src1 ,GR64:$src2),
                  "xchg{q}\t{$src2, $src1|$src1, $src2}", []>, NotMemoryFoldable;
}

// Swap between EAX and other registers.
let Constraints = "$src = $dst", hasSideEffects = 0 in {
let Uses = [AX], Defs = [AX] in
def XCHG16ar : I<0x90, AddRegFrm, (outs GR16:$dst), (ins GR16:$src),
                  "xchg{w}\t{$src, %ax|ax, $src}", []>, OpSize16;
let Uses = [EAX], Defs = [EAX] in
def XCHG32ar : I<0x90, AddRegFrm, (outs GR32:$dst), (ins GR32:$src),
                  "xchg{l}\t{$src, %eax|eax, $src}", []>, OpSize32;
let Uses = [RAX], Defs = [RAX] in
def XCHG64ar : RI<0x90, AddRegFrm, (outs GR64:$dst), (ins GR64:$src),
                  "xchg{q}\t{$src, %rax|rax, $src}", []>;
}
} // SchedRW

let hasSideEffects = 0, Constraints = "$src1 = $dst1, $src2 = $dst2",
    Defs = [EFLAGS], SchedRW = [WriteXCHG] in {
def XADD8rr : I<0xC0, MRMDestReg, (outs GR8:$dst1, GR8:$dst2),
                (ins GR8:$src1, GR8:$src2),
                "xadd{b}\t{$src2, $src1|$src1, $src2}", []>, TB;
def XADD16rr : I<0xC1, MRMDestReg, (outs GR16:$dst1, GR16:$dst2),
                 (ins GR16:$src1, GR16:$src2),
                 "xadd{w}\t{$src2, $src1|$src1, $src2}", []>, TB, OpSize16;
def XADD32rr : I<0xC1, MRMDestReg, (outs GR32:$dst1, GR32:$dst2),
                  (ins GR32:$src1, GR32:$src2),
                 "xadd{l}\t{$src2, $src1|$src1, $src2}", []>, TB, OpSize32;
def XADD64rr : RI<0xC1, MRMDestReg, (outs GR64:$dst1, GR64:$dst2),
                  (ins GR64:$src1, GR64:$src2),
                  "xadd{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, hasSideEffects = 0, Constraints = "$val = $dst",
    Defs = [EFLAGS], SchedRW = [WriteALULd, WriteRMW] in {
def XADD8rm   : I<0xC0, MRMSrcMem, (outs GR8:$dst),
                  (ins GR8:$val, i8mem:$ptr),
                 "xadd{b}\t{$val, $ptr|$ptr, $val}", []>, TB;
def XADD16rm  : I<0xC1, MRMSrcMem, (outs GR16:$dst),
                  (ins GR16:$val, i16mem:$ptr),
                 "xadd{w}\t{$val, $ptr|$ptr, $val}", []>, TB,
                 OpSize16;
def XADD32rm  : I<0xC1, MRMSrcMem, (outs GR32:$dst),
                  (ins GR32:$val, i32mem:$ptr),
                 "xadd{l}\t{$val, $ptr|$ptr, $val}", []>, TB,
                 OpSize32;
def XADD64rm  : RI<0xC1, MRMSrcMem, (outs GR64:$dst),
                   (ins GR64:$val, i64mem:$ptr),
                   "xadd{q}\t{$val, $ptr|$ptr, $val}", []>, TB;

}

let SchedRW = [WriteCMPXCHG], hasSideEffects = 0 in {
let Defs = [AL, EFLAGS], Uses = [AL] in
def CMPXCHG8rr : I<0xB0, MRMDestReg, (outs GR8:$dst), (ins GR8:$src),
                   "cmpxchg{b}\t{$src, $dst|$dst, $src}", []>, TB,
                   NotMemoryFoldable;
let Defs = [AX, EFLAGS], Uses = [AX] in
def CMPXCHG16rr : I<0xB1, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                    "cmpxchg{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16,
                    NotMemoryFoldable;
let Defs = [EAX, EFLAGS], Uses = [EAX] in
def CMPXCHG32rr  : I<0xB1, MRMDestReg, (outs GR32:$dst), (ins GR32:$src),
                     "cmpxchg{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32,
                     NotMemoryFoldable;
let Defs = [RAX, EFLAGS], Uses = [RAX] in
def CMPXCHG64rr  : RI<0xB1, MRMDestReg, (outs GR64:$dst), (ins GR64:$src),
                      "cmpxchg{q}\t{$src, $dst|$dst, $src}", []>, TB,
                      NotMemoryFoldable;
} // SchedRW, hasSideEffects

let SchedRW = [WriteCMPXCHGRMW], mayLoad = 1, mayStore = 1,
    hasSideEffects = 0 in {
let Defs = [AL, EFLAGS], Uses = [AL] in
def CMPXCHG8rm   : I<0xB0, MRMDestMem, (outs), (ins i8mem:$dst, GR8:$src),
                     "cmpxchg{b}\t{$src, $dst|$dst, $src}", []>, TB,
                     NotMemoryFoldable;
let Defs = [AX, EFLAGS], Uses = [AX] in
def CMPXCHG16rm  : I<0xB1, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                     "cmpxchg{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16,
                     NotMemoryFoldable;
let Defs = [EAX, EFLAGS], Uses = [EAX] in
def CMPXCHG32rm  : I<0xB1, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                     "cmpxchg{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32,
                     NotMemoryFoldable;
let Defs = [RAX, EFLAGS], Uses = [RAX] in
def CMPXCHG64rm  : RI<0xB1, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                      "cmpxchg{q}\t{$src, $dst|$dst, $src}", []>, TB,
                      NotMemoryFoldable;

let Defs = [EAX, EDX, EFLAGS], Uses = [EAX, EBX, ECX, EDX] in
def CMPXCHG8B : I<0xC7, MRM1m, (outs), (ins i64mem:$dst),
                  "cmpxchg8b\t$dst", []>, TB;

let Defs = [RAX, RDX, EFLAGS], Uses = [RAX, RBX, RCX, RDX] in
def CMPXCHG16B : RI<0xC7, MRM1m, (outs), (ins i128mem:$dst),
                    "cmpxchg16b\t$dst", []>,
                    TB, Requires<[HasCmpxchg16b, In64BitMode]>;
} // SchedRW, mayLoad, mayStore, hasSideEffects


// Lock instruction prefix
let SchedRW = [WriteMicrocoded] in
def LOCK_PREFIX : I<0xF0, RawFrm, (outs),  (ins), "lock", []>;

let SchedRW = [WriteNop] in {

// Rex64 instruction prefix
def REX64_PREFIX : I<0x48, RawFrm, (outs),  (ins), "rex64", []>,
                     Requires<[In64BitMode]>;

// Data16 instruction prefix
def DATA16_PREFIX : I<0x66, RawFrm, (outs),  (ins), "data16", []>;
} // SchedRW

// Repeat string operation instruction prefixes
let Defs = [ECX], Uses = [ECX,DF], SchedRW = [WriteMicrocoded] in {
// Repeat (used with INS, OUTS, MOVS, LODS and STOS)
def REP_PREFIX : I<0xF3, RawFrm, (outs),  (ins), "rep", []>;
// Repeat while not equal (used with CMPS and SCAS)
def REPNE_PREFIX : I<0xF2, RawFrm, (outs),  (ins), "repne", []>;
}

// String manipulation instructions
let SchedRW = [WriteMicrocoded] in {
let Defs = [AL,ESI], Uses = [ESI,DF] in
def LODSB : I<0xAC, RawFrmSrc, (outs), (ins srcidx8:$src),
              "lodsb\t{$src, %al|al, $src}", []>;
let Defs = [AX,ESI], Uses = [ESI,DF] in
def LODSW : I<0xAD, RawFrmSrc, (outs), (ins srcidx16:$src),
              "lodsw\t{$src, %ax|ax, $src}", []>, OpSize16;
let Defs = [EAX,ESI], Uses = [ESI,DF] in
def LODSL : I<0xAD, RawFrmSrc, (outs), (ins srcidx32:$src),
              "lods{l|d}\t{$src, %eax|eax, $src}", []>, OpSize32;
let Defs = [RAX,ESI], Uses = [ESI,DF] in
def LODSQ : RI<0xAD, RawFrmSrc, (outs), (ins srcidx64:$src),
               "lodsq\t{$src, %rax|rax, $src}", []>,
               Requires<[In64BitMode]>;
}

let SchedRW = [WriteSystem] in {
let Defs = [ESI], Uses = [DX,ESI,DF] in {
def OUTSB : I<0x6E, RawFrmSrc, (outs), (ins srcidx8:$src),
             "outsb\t{$src, %dx|dx, $src}", []>;
def OUTSW : I<0x6F, RawFrmSrc, (outs), (ins srcidx16:$src),
              "outsw\t{$src, %dx|dx, $src}", []>, OpSize16;
def OUTSL : I<0x6F, RawFrmSrc, (outs), (ins srcidx32:$src),
              "outs{l|d}\t{$src, %dx|dx, $src}", []>, OpSize32;
}

let Defs = [EDI], Uses = [DX,EDI,DF] in {
def INSB : I<0x6C, RawFrmDst, (outs), (ins dstidx8:$dst),
             "insb\t{%dx, $dst|$dst, dx}", []>;
def INSW : I<0x6D, RawFrmDst, (outs), (ins dstidx16:$dst),
             "insw\t{%dx, $dst|$dst, dx}", []>,  OpSize16;
def INSL : I<0x6D, RawFrmDst, (outs), (ins dstidx32:$dst),
             "ins{l|d}\t{%dx, $dst|$dst, dx}", []>, OpSize32;
}
}

// EFLAGS management instructions.
let SchedRW = [WriteALU], Defs = [EFLAGS], Uses = [EFLAGS] in {
def CLC : I<0xF8, RawFrm, (outs), (ins), "clc", []>;
def STC : I<0xF9, RawFrm, (outs), (ins), "stc", []>;
def CMC : I<0xF5, RawFrm, (outs), (ins), "cmc", []>;
}

// DF management instructions.
let SchedRW = [WriteALU], Defs = [DF] in {
def CLD : I<0xFC, RawFrm, (outs), (ins), "cld", []>;
def STD : I<0xFD, RawFrm, (outs), (ins), "std", []>;
}

// Table lookup instructions
let Uses = [AL,EBX], Defs = [AL], hasSideEffects = 0, mayLoad = 1 in
def XLAT : I<0xD7, RawFrm, (outs), (ins), "xlatb", []>, Sched<[WriteLoad]>;

let SchedRW = [WriteMicrocoded] in {
// ASCII Adjust After Addition
let Uses = [AL,EFLAGS], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAA : I<0x37, RawFrm, (outs), (ins), "aaa", []>,
            Requires<[Not64BitMode]>;

// ASCII Adjust AX Before Division
let Uses = [AX], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAD8i8 : Ii8<0xD5, RawFrm, (outs), (ins i8imm:$src),
                 "aad\t$src", []>, Requires<[Not64BitMode]>;

// ASCII Adjust AX After Multiply
let Uses = [AL], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAM8i8 : Ii8<0xD4, RawFrm, (outs), (ins i8imm:$src),
                 "aam\t$src", []>, Requires<[Not64BitMode]>;

// ASCII Adjust AL After Subtraction - sets
let Uses = [AL,EFLAGS], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAS : I<0x3F, RawFrm, (outs), (ins), "aas", []>,
            Requires<[Not64BitMode]>;

// Decimal Adjust AL after Addition
let Uses = [AL,EFLAGS], Defs = [AL,EFLAGS], hasSideEffects = 0 in
def DAA : I<0x27, RawFrm, (outs), (ins), "daa", []>,
            Requires<[Not64BitMode]>;

// Decimal Adjust AL after Subtraction
let Uses = [AL,EFLAGS], Defs = [AL,EFLAGS], hasSideEffects = 0 in
def DAS : I<0x2F, RawFrm, (outs), (ins), "das", []>,
            Requires<[Not64BitMode]>;
} // SchedRW

let SchedRW = [WriteSystem] in {
// Check Array Index Against Bounds
// Note: "bound" does not have reversed operands in at&t syntax.
def BOUNDS16rm : I<0x62, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                   "bound\t$dst, $src", []>, OpSize16,
                   Requires<[Not64BitMode]>;
def BOUNDS32rm : I<0x62, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                   "bound\t$dst, $src", []>, OpSize32,
                   Requires<[Not64BitMode]>;

// Adjust RPL Field of Segment Selector
def ARPL16rr : I<0x63, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                 "arpl\t{$src, $dst|$dst, $src}", []>,
                 Requires<[Not64BitMode]>, NotMemoryFoldable;
let mayStore = 1 in
def ARPL16mr : I<0x63, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                 "arpl\t{$src, $dst|$dst, $src}", []>,
                 Requires<[Not64BitMode]>, NotMemoryFoldable;
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVBE Instructions
//
let Predicates = [HasMOVBE] in {
  let SchedRW = [WriteALULd] in {
  def MOVBE16rm : I<0xF0, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                    "movbe{w}\t{$src, $dst|$dst, $src}",
                    [(set GR16:$dst, (bswap (loadi16 addr:$src)))]>,
                    OpSize16, T8PS;
  def MOVBE32rm : I<0xF0, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                    "movbe{l}\t{$src, $dst|$dst, $src}",
                    [(set GR32:$dst, (bswap (loadi32 addr:$src)))]>,
                    OpSize32, T8PS;
  def MOVBE64rm : RI<0xF0, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                     "movbe{q}\t{$src, $dst|$dst, $src}",
                     [(set GR64:$dst, (bswap (loadi64 addr:$src)))]>,
                     T8PS;
  }
  let SchedRW = [WriteStore] in {
  def MOVBE16mr : I<0xF1, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                    "movbe{w}\t{$src, $dst|$dst, $src}",
                    [(store (bswap GR16:$src), addr:$dst)]>,
                    OpSize16, T8PS;
  def MOVBE32mr : I<0xF1, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                    "movbe{l}\t{$src, $dst|$dst, $src}",
                    [(store (bswap GR32:$src), addr:$dst)]>,
                    OpSize32, T8PS;
  def MOVBE64mr : RI<0xF1, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                     "movbe{q}\t{$src, $dst|$dst, $src}",
                     [(store (bswap GR64:$src), addr:$dst)]>,
                     T8PS;
  }
}

//===----------------------------------------------------------------------===//
// RDRAND Instruction
//
let Predicates = [HasRDRAND], Defs = [EFLAGS], SchedRW = [WriteSystem] in {
  def RDRAND16r : I<0xC7, MRM6r, (outs GR16:$dst), (ins),
                    "rdrand{w}\t$dst", [(set GR16:$dst, EFLAGS, (X86rdrand))]>,
                    OpSize16, PS;
  def RDRAND32r : I<0xC7, MRM6r, (outs GR32:$dst), (ins),
                    "rdrand{l}\t$dst", [(set GR32:$dst, EFLAGS, (X86rdrand))]>,
                    OpSize32, PS;
  def RDRAND64r : RI<0xC7, MRM6r, (outs GR64:$dst), (ins),
                     "rdrand{q}\t$dst", [(set GR64:$dst, EFLAGS, (X86rdrand))]>,
                     PS;
}

//===----------------------------------------------------------------------===//
// RDSEED Instruction
//
let Predicates = [HasRDSEED], Defs = [EFLAGS], SchedRW = [WriteSystem] in {
  def RDSEED16r : I<0xC7, MRM7r, (outs GR16:$dst), (ins), "rdseed{w}\t$dst",
                    [(set GR16:$dst, EFLAGS, (X86rdseed))]>, OpSize16, PS;
  def RDSEED32r : I<0xC7, MRM7r, (outs GR32:$dst), (ins), "rdseed{l}\t$dst",
                    [(set GR32:$dst, EFLAGS, (X86rdseed))]>, OpSize32, PS;
  def RDSEED64r : RI<0xC7, MRM7r, (outs GR64:$dst), (ins), "rdseed{q}\t$dst",
                     [(set GR64:$dst, EFLAGS, (X86rdseed))]>, PS;
}

//===----------------------------------------------------------------------===//
// LZCNT Instruction
//
let Predicates = [HasLZCNT], Defs = [EFLAGS] in {
  def LZCNT16rr : I<0xBD, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                    "lzcnt{w}\t{$src, $dst|$dst, $src}",
                    [(set GR16:$dst, (ctlz GR16:$src)), (implicit EFLAGS)]>,
                    XS, OpSize16, Sched<[WriteLZCNT]>;
  def LZCNT16rm : I<0xBD, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                    "lzcnt{w}\t{$src, $dst|$dst, $src}",
                    [(set GR16:$dst, (ctlz (loadi16 addr:$src))),
                     (implicit EFLAGS)]>, XS, OpSize16, Sched<[WriteLZCNTLd]>;

  def LZCNT32rr : I<0xBD, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                    "lzcnt{l}\t{$src, $dst|$dst, $src}",
                    [(set GR32:$dst, (ctlz GR32:$src)), (implicit EFLAGS)]>,
                    XS, OpSize32, Sched<[WriteLZCNT]>;
  def LZCNT32rm : I<0xBD, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                    "lzcnt{l}\t{$src, $dst|$dst, $src}",
                    [(set GR32:$dst, (ctlz (loadi32 addr:$src))),
                     (implicit EFLAGS)]>, XS, OpSize32, Sched<[WriteLZCNTLd]>;

  def LZCNT64rr : RI<0xBD, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                     "lzcnt{q}\t{$src, $dst|$dst, $src}",
                     [(set GR64:$dst, (ctlz GR64:$src)), (implicit EFLAGS)]>,
                     XS, Sched<[WriteLZCNT]>;
  def LZCNT64rm : RI<0xBD, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                     "lzcnt{q}\t{$src, $dst|$dst, $src}",
                     [(set GR64:$dst, (ctlz (loadi64 addr:$src))),
                      (implicit EFLAGS)]>, XS, Sched<[WriteLZCNTLd]>;
}

//===----------------------------------------------------------------------===//
// BMI Instructions
//
let Predicates = [HasBMI], Defs = [EFLAGS] in {
  def TZCNT16rr : I<0xBC, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                    "tzcnt{w}\t{$src, $dst|$dst, $src}",
                    [(set GR16:$dst, (cttz GR16:$src)), (implicit EFLAGS)]>,
                    XS, OpSize16, Sched<[WriteTZCNT]>;
  def TZCNT16rm : I<0xBC, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                    "tzcnt{w}\t{$src, $dst|$dst, $src}",
                    [(set GR16:$dst, (cttz (loadi16 addr:$src))),
                     (implicit EFLAGS)]>, XS, OpSize16, Sched<[WriteTZCNTLd]>;

  def TZCNT32rr : I<0xBC, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                    "tzcnt{l}\t{$src, $dst|$dst, $src}",
                    [(set GR32:$dst, (cttz GR32:$src)), (implicit EFLAGS)]>,
                    XS, OpSize32, Sched<[WriteTZCNT]>;
  def TZCNT32rm : I<0xBC, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                    "tzcnt{l}\t{$src, $dst|$dst, $src}",
                    [(set GR32:$dst, (cttz (loadi32 addr:$src))),
                     (implicit EFLAGS)]>, XS, OpSize32, Sched<[WriteTZCNTLd]>;

  def TZCNT64rr : RI<0xBC, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                     "tzcnt{q}\t{$src, $dst|$dst, $src}",
                     [(set GR64:$dst, (cttz GR64:$src)), (implicit EFLAGS)]>,
                     XS, Sched<[WriteTZCNT]>;
  def TZCNT64rm : RI<0xBC, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                     "tzcnt{q}\t{$src, $dst|$dst, $src}",
                     [(set GR64:$dst, (cttz (loadi64 addr:$src))),
                      (implicit EFLAGS)]>, XS, Sched<[WriteTZCNTLd]>;
}

multiclass bmi_bls<string mnemonic, Format RegMRM, Format MemMRM,
                  RegisterClass RC, X86MemOperand x86memop> {
let hasSideEffects = 0 in {
  def rr : I<0xF3, RegMRM, (outs RC:$dst), (ins RC:$src),
             !strconcat(mnemonic, "\t{$src, $dst|$dst, $src}"), []>,
             T8PS, VEX_4V, Sched<[WriteBLS]>;
  let mayLoad = 1 in
  def rm : I<0xF3, MemMRM, (outs RC:$dst), (ins x86memop:$src),
             !strconcat(mnemonic, "\t{$src, $dst|$dst, $src}"), []>,
             T8PS, VEX_4V, Sched<[WriteBLS.Folded]>;
}
}

let Predicates = [HasBMI], Defs = [EFLAGS] in {
  defm BLSR32 : bmi_bls<"blsr{l}", MRM1r, MRM1m, GR32, i32mem>;
  defm BLSR64 : bmi_bls<"blsr{q}", MRM1r, MRM1m, GR64, i64mem>, VEX_W;
  defm BLSMSK32 : bmi_bls<"blsmsk{l}", MRM2r, MRM2m, GR32, i32mem>;
  defm BLSMSK64 : bmi_bls<"blsmsk{q}", MRM2r, MRM2m, GR64, i64mem>, VEX_W;
  defm BLSI32 : bmi_bls<"blsi{l}", MRM3r, MRM3m, GR32, i32mem>;
  defm BLSI64 : bmi_bls<"blsi{q}", MRM3r, MRM3m, GR64, i64mem>, VEX_W;
}

//===----------------------------------------------------------------------===//
// Pattern fragments to auto generate BMI instructions.
//===----------------------------------------------------------------------===//

def or_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                           (X86or_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def xor_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                            (X86xor_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

let Predicates = [HasBMI] in {
  // FIXME: patterns for the load versions are not implemented
  def : Pat<(and GR32:$src, (add GR32:$src, -1)),
            (BLSR32rr GR32:$src)>;
  def : Pat<(and GR64:$src, (add GR64:$src, -1)),
            (BLSR64rr GR64:$src)>;

  def : Pat<(xor GR32:$src, (add GR32:$src, -1)),
            (BLSMSK32rr GR32:$src)>;
  def : Pat<(xor GR64:$src, (add GR64:$src, -1)),
            (BLSMSK64rr GR64:$src)>;

  def : Pat<(and GR32:$src, (ineg GR32:$src)),
            (BLSI32rr GR32:$src)>;
  def : Pat<(and GR64:$src, (ineg GR64:$src)),
            (BLSI64rr GR64:$src)>;

  // Versions to match flag producing ops.
  // X86and_flag nodes are rarely created. Those should use CMP+AND. We do
  // TESTrr matching in PostProcessISelDAG to allow BLSR/BLSI to be formed.
  def : Pat<(xor_flag_nocf GR32:$src, (add GR32:$src, -1)),
            (BLSMSK32rr GR32:$src)>;
  def : Pat<(xor_flag_nocf GR64:$src, (add GR64:$src, -1)),
            (BLSMSK64rr GR64:$src)>;
}

multiclass bmi_bextr<bits<8> opc, string mnemonic, RegisterClass RC,
                     X86MemOperand x86memop, SDNode OpNode,
                     PatFrag ld_frag, X86FoldableSchedWrite Sched> {
  def rr : I<opc, MRMSrcReg4VOp3, (outs RC:$dst), (ins RC:$src1, RC:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (OpNode RC:$src1, RC:$src2)), (implicit EFLAGS)]>,
             T8PS, VEX, Sched<[Sched]>;
  def rm : I<opc, MRMSrcMem4VOp3, (outs RC:$dst), (ins x86memop:$src1, RC:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (OpNode (ld_frag addr:$src1), RC:$src2)),
              (implicit EFLAGS)]>, T8PS, VEX,
             Sched<[Sched.Folded,
                    // x86memop:$src1
                    ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                    ReadDefault,
                    // RC:$src2
                    Sched.ReadAfterFold]>;
}

let Predicates = [HasBMI], Defs = [EFLAGS] in {
  defm BEXTR32 : bmi_bextr<0xF7, "bextr{l}", GR32, i32mem,
                           X86bextr, loadi32, WriteBEXTR>;
  defm BEXTR64 : bmi_bextr<0xF7, "bextr{q}", GR64, i64mem,
                           X86bextr, loadi64, WriteBEXTR>, VEX_W;
}

multiclass bmi_bzhi<bits<8> opc, string mnemonic, RegisterClass RC,
                    X86MemOperand x86memop, Intrinsic Int,
                    PatFrag ld_frag, X86FoldableSchedWrite Sched> {
  def rr : I<opc, MRMSrcReg4VOp3, (outs RC:$dst), (ins RC:$src1, RC:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (Int RC:$src1, RC:$src2)), (implicit EFLAGS)]>,
             T8PS, VEX, Sched<[Sched]>;
  def rm : I<opc, MRMSrcMem4VOp3, (outs RC:$dst), (ins x86memop:$src1, RC:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (Int (ld_frag addr:$src1), RC:$src2)),
              (implicit EFLAGS)]>, T8PS, VEX,
             Sched<[Sched.Folded,
                    // x86memop:$src1
                    ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                    ReadDefault,
                    // RC:$src2
                    Sched.ReadAfterFold]>;
}

let Predicates = [HasBMI2], Defs = [EFLAGS] in {
  defm BZHI32 : bmi_bzhi<0xF5, "bzhi{l}", GR32, i32mem,
                         X86bzhi, loadi32, WriteBZHI>;
  defm BZHI64 : bmi_bzhi<0xF5, "bzhi{q}", GR64, i64mem,
                         X86bzhi, loadi64, WriteBZHI>, VEX_W;
}

def CountTrailingOnes : SDNodeXForm<imm, [{
  // Count the trailing ones in the immediate.
  return getI8Imm(countTrailingOnes(N->getZExtValue()), SDLoc(N));
}]>;

def BEXTRMaskXForm : SDNodeXForm<imm, [{
  unsigned Length = countTrailingOnes(N->getZExtValue());
  return getI32Imm(Length << 8, SDLoc(N));
}]>;

def AndMask64 : ImmLeaf<i64, [{
  return isMask_64(Imm) && !isUInt<32>(Imm);
}]>;

// Use BEXTR for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasBMI, NoBMI2, NoTBM] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BEXTR64rr GR64:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BEXTR64rm addr:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
}

// Use BZHI for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasBMI2, NoTBM] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BZHI64rr GR64:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BZHI64rm addr:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
}

multiclass bmi_pdep_pext<string mnemonic, RegisterClass RC,
                         X86MemOperand x86memop, Intrinsic Int,
                         PatFrag ld_frag> {
  def rr : I<0xF5, MRMSrcReg, (outs RC:$dst), (ins RC:$src1, RC:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (Int RC:$src1, RC:$src2))]>,
             VEX_4V, Sched<[WriteALU]>;
  def rm : I<0xF5, MRMSrcMem, (outs RC:$dst), (ins RC:$src1, x86memop:$src2),
             !strconcat(mnemonic, "\t{$src2, $src1, $dst|$dst, $src1, $src2}"),
             [(set RC:$dst, (Int RC:$src1, (ld_frag addr:$src2)))]>,
             VEX_4V, Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>;
}

let Predicates = [HasBMI2] in {
  defm PDEP32 : bmi_pdep_pext<"pdep{l}", GR32, i32mem,
                               int_x86_bmi_pdep_32, loadi32>, T8XD;
  defm PDEP64 : bmi_pdep_pext<"pdep{q}", GR64, i64mem,
                               int_x86_bmi_pdep_64, loadi64>, T8XD, VEX_W;
  defm PEXT32 : bmi_pdep_pext<"pext{l}", GR32, i32mem,
                               int_x86_bmi_pext_32, loadi32>, T8XS;
  defm PEXT64 : bmi_pdep_pext<"pext{q}", GR64, i64mem,
                               int_x86_bmi_pext_64, loadi64>, T8XS, VEX_W;
}

//===----------------------------------------------------------------------===//
// TBM Instructions
//
let Predicates = [HasTBM], Defs = [EFLAGS] in {

multiclass tbm_ternary_imm<bits<8> opc, RegisterClass RC, string OpcodeStr,
                           X86MemOperand x86memop, PatFrag ld_frag,
                           SDNode OpNode, Operand immtype,
                           SDPatternOperator immoperator,
                           X86FoldableSchedWrite Sched> {
  def ri : Ii32<opc,  MRMSrcReg, (outs RC:$dst), (ins RC:$src1, immtype:$cntl),
                !strconcat(OpcodeStr,
                           "\t{$cntl, $src1, $dst|$dst, $src1, $cntl}"),
                [(set RC:$dst, (OpNode RC:$src1, immoperator:$cntl))]>,
                XOP, XOPA, Sched<[Sched]>;
  def mi : Ii32<opc,  MRMSrcMem, (outs RC:$dst),
                (ins x86memop:$src1, immtype:$cntl),
                !strconcat(OpcodeStr,
                           "\t{$cntl, $src1, $dst|$dst, $src1, $cntl}"),
                [(set RC:$dst, (OpNode (ld_frag addr:$src1), immoperator:$cntl))]>,
                XOP, XOPA, Sched<[Sched.Folded]>;
}

defm BEXTRI32 : tbm_ternary_imm<0x10, GR32, "bextr{l}", i32mem, loadi32,
                                X86bextr, i32imm, imm, WriteBEXTR>;
let ImmT = Imm32S in
defm BEXTRI64 : tbm_ternary_imm<0x10, GR64, "bextr{q}", i64mem, loadi64,
                                X86bextr, i64i32imm,
                                i64immSExt32, WriteBEXTR>, VEX_W;

multiclass tbm_binary_rm<bits<8> opc, Format FormReg, Format FormMem,
                         RegisterClass RC, string OpcodeStr,
                         X86MemOperand x86memop, X86FoldableSchedWrite Sched> {
let hasSideEffects = 0 in {
  def rr : I<opc,  FormReg, (outs RC:$dst), (ins RC:$src),
             !strconcat(OpcodeStr,"\t{$src, $dst|$dst, $src}"), []>,
             XOP_4V, XOP9, Sched<[Sched]>;
  let mayLoad = 1 in
  def rm : I<opc,  FormMem, (outs RC:$dst), (ins x86memop:$src),
             !strconcat(OpcodeStr,"\t{$src, $dst|$dst, $src}"), []>,
             XOP_4V, XOP9, Sched<[Sched.Folded]>;
}
}

multiclass tbm_binary_intr<bits<8> opc, string OpcodeStr,
                           X86FoldableSchedWrite Sched,
                           Format FormReg, Format FormMem> {
  defm NAME#32 : tbm_binary_rm<opc, FormReg, FormMem, GR32, OpcodeStr#"{l}",
                               i32mem, Sched>;
  defm NAME#64 : tbm_binary_rm<opc, FormReg, FormMem, GR64, OpcodeStr#"{q}",
                               i64mem, Sched>, VEX_W;
}

defm BLCFILL : tbm_binary_intr<0x01, "blcfill", WriteALU, MRM1r, MRM1m>;
defm BLCI    : tbm_binary_intr<0x02, "blci", WriteALU, MRM6r, MRM6m>;
defm BLCIC   : tbm_binary_intr<0x01, "blcic", WriteALU, MRM5r, MRM5m>;
defm BLCMSK  : tbm_binary_intr<0x02, "blcmsk", WriteALU, MRM1r, MRM1m>;
defm BLCS    : tbm_binary_intr<0x01, "blcs", WriteALU, MRM3r, MRM3m>;
defm BLSFILL : tbm_binary_intr<0x01, "blsfill", WriteALU, MRM2r, MRM2m>;
defm BLSIC   : tbm_binary_intr<0x01, "blsic", WriteALU, MRM6r, MRM6m>;
defm T1MSKC  : tbm_binary_intr<0x01, "t1mskc", WriteALU, MRM7r, MRM7m>;
defm TZMSK   : tbm_binary_intr<0x01, "tzmsk", WriteALU, MRM4r, MRM4m>;
} // HasTBM, EFLAGS

// Use BEXTRI for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasTBM] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BEXTRI64ri GR64:$src, (BEXTRMaskXForm imm:$mask))>;

  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BEXTRI64mi addr:$src, (BEXTRMaskXForm imm:$mask))>;
}

//===----------------------------------------------------------------------===//
// Lightweight Profiling Instructions

let Predicates = [HasLWP], SchedRW = [WriteSystem] in {

def LLWPCB : I<0x12, MRM0r, (outs), (ins GR32:$src), "llwpcb\t$src",
               [(int_x86_llwpcb GR32:$src)]>, XOP, XOP9;
def SLWPCB : I<0x12, MRM1r, (outs GR32:$dst), (ins), "slwpcb\t$dst",
               [(set GR32:$dst, (int_x86_slwpcb))]>, XOP, XOP9;

def LLWPCB64 : I<0x12, MRM0r, (outs), (ins GR64:$src), "llwpcb\t$src",
                 [(int_x86_llwpcb GR64:$src)]>, XOP, XOP9, VEX_W;
def SLWPCB64 : I<0x12, MRM1r, (outs GR64:$dst), (ins), "slwpcb\t$dst",
                 [(set GR64:$dst, (int_x86_slwpcb))]>, XOP, XOP9, VEX_W;

multiclass lwpins_intr<RegisterClass RC> {
  def rri : Ii32<0x12, MRM0r, (outs), (ins RC:$src0, GR32:$src1, i32imm:$cntl),
                 "lwpins\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(set EFLAGS, (X86lwpins RC:$src0, GR32:$src1, imm:$cntl))]>,
                 XOP_4V, XOPA;
  let mayLoad = 1 in
  def rmi : Ii32<0x12, MRM0m, (outs), (ins RC:$src0, i32mem:$src1, i32imm:$cntl),
                 "lwpins\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(set EFLAGS, (X86lwpins RC:$src0, (loadi32 addr:$src1), imm:$cntl))]>,
                 XOP_4V, XOPA;
}

let Defs = [EFLAGS] in {
  defm LWPINS32 : lwpins_intr<GR32>;
  defm LWPINS64 : lwpins_intr<GR64>, VEX_W;
} // EFLAGS

multiclass lwpval_intr<RegisterClass RC, Intrinsic Int> {
  def rri : Ii32<0x12, MRM1r, (outs), (ins RC:$src0, GR32:$src1, i32imm:$cntl),
                 "lwpval\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(Int RC:$src0, GR32:$src1, imm:$cntl)]>, XOP_4V, XOPA;
  let mayLoad = 1 in
  def rmi : Ii32<0x12, MRM1m, (outs), (ins RC:$src0, i32mem:$src1, i32imm:$cntl),
                 "lwpval\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(Int RC:$src0, (loadi32 addr:$src1), imm:$cntl)]>,
                 XOP_4V, XOPA;
}

defm LWPVAL32 : lwpval_intr<GR32, int_x86_lwpval32>;
defm LWPVAL64 : lwpval_intr<GR64, int_x86_lwpval64>, VEX_W;

} // HasLWP, SchedRW

//===----------------------------------------------------------------------===//
// MONITORX/MWAITX Instructions
//
let SchedRW = [ WriteSystem ] in {
  let usesCustomInserter = 1 in {
    def MONITORX : PseudoI<(outs), (ins i32mem:$src1, GR32:$src2, GR32:$src3),
                           [(int_x86_monitorx addr:$src1, GR32:$src2, GR32:$src3)]>,
                   Requires<[ HasMWAITX ]>;
  }

  let Uses = [ EAX, ECX, EDX ] in {
    def MONITORXrrr : I<0x01, MRM_FA, (outs), (ins), "monitorx", []>,
                      TB, Requires<[ HasMWAITX ]>;
  }

  let Uses = [ ECX, EAX, EBX ] in {
    def MWAITXrrr : I<0x01, MRM_FB, (outs), (ins), "mwaitx",
                    [(int_x86_mwaitx ECX, EAX, EBX)]>,
                    TB, Requires<[ HasMWAITX ]>;
  }
} // SchedRW

def : InstAlias<"mwaitx\t{%eax, %ecx, %ebx|ebx, ecx, eax}", (MWAITXrrr)>,
      Requires<[ Not64BitMode ]>;
def : InstAlias<"mwaitx\t{%rax, %rcx, %rbx|rbx, rcx, rax}", (MWAITXrrr)>,
      Requires<[ In64BitMode ]>;

def : InstAlias<"monitorx\t{%eax, %ecx, %edx|edx, ecx, eax}", (MONITORXrrr)>,
      Requires<[ Not64BitMode ]>;
def : InstAlias<"monitorx\t{%rax, %rcx, %rdx|rdx, rcx, rax}", (MONITORXrrr)>,
      Requires<[ In64BitMode ]>;

//===----------------------------------------------------------------------===//
// WAITPKG Instructions
//
let SchedRW = [WriteSystem] in {
  def UMONITOR16 : I<0xAE, MRM6r, (outs), (ins GR16:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR16:$src)]>,
                     XS, AdSize16, Requires<[HasWAITPKG, Not64BitMode]>;
  def UMONITOR32 : I<0xAE, MRM6r, (outs), (ins GR32:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR32:$src)]>,
                     XS, AdSize32, Requires<[HasWAITPKG]>;
  def UMONITOR64 : I<0xAE, MRM6r, (outs), (ins GR64:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR64:$src)]>,
                     XS, AdSize64, Requires<[HasWAITPKG, In64BitMode]>;
  let Uses = [EAX, EDX], Defs = [EFLAGS] in {
    def UMWAIT : I<0xAE, MRM6r,
                     (outs), (ins GR32orGR64:$src), "umwait\t$src",
                     [(set EFLAGS, (X86umwait GR32orGR64:$src, EDX, EAX))]>,
                     XD, Requires<[HasWAITPKG]>;
    def TPAUSE : I<0xAE, MRM6r,
                     (outs), (ins GR32orGR64:$src), "tpause\t$src",
                     [(set EFLAGS, (X86tpause GR32orGR64:$src, EDX, EAX))]>,
                     PD, Requires<[HasWAITPKG]>, NotMemoryFoldable;
  }
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVDIRI - Move doubleword/quadword as direct store
//
let SchedRW = [WriteStore] in {
def MOVDIRI32 : I<0xF9, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                  "movdiri\t{$src, $dst|$dst, $src}",
                  [(int_x86_directstore32 addr:$dst, GR32:$src)]>,
                 T8, Requires<[HasMOVDIRI]>;
def MOVDIRI64 : RI<0xF9, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                   "movdiri\t{$src, $dst|$dst, $src}",
                   [(int_x86_directstore64 addr:$dst, GR64:$src)]>,
                  T8, Requires<[In64BitMode, HasMOVDIRI]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVDIR64B - Move 64 bytes as direct store
//
let SchedRW = [WriteStore] in {
def MOVDIR64B16 : I<0xF8, MRMSrcMem, (outs), (ins GR16:$dst, i512mem:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}", []>,
                   T8PD, AdSize16, Requires<[HasMOVDIR64B, Not64BitMode]>;
def MOVDIR64B32 : I<0xF8, MRMSrcMem, (outs), (ins GR32:$dst, i512mem:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}",
                    [(int_x86_movdir64b GR32:$dst, addr:$src)]>,
                   T8PD, AdSize32, Requires<[HasMOVDIR64B]>;
def MOVDIR64B64 : I<0xF8, MRMSrcMem, (outs), (ins GR64:$dst, i512mem:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}",
                    [(int_x86_movdir64b GR64:$dst, addr:$src)]>,
                   T8PD, AdSize64, Requires<[HasMOVDIR64B, In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// CLZERO Instruction
//
let SchedRW = [WriteSystem] in {
  let Uses = [EAX] in
  def CLZEROr : I<0x01, MRM_FC, (outs), (ins), "clzero", []>,
                TB, Requires<[HasCLZERO]>;

  let usesCustomInserter = 1 in {
  def CLZERO : PseudoI<(outs), (ins i32mem:$src1),
                       [(int_x86_clzero addr:$src1)]>, Requires<[HasCLZERO]>;
  }
} // SchedRW

def : InstAlias<"clzero\t{%eax|eax}", (CLZEROr)>, Requires<[Not64BitMode]>;
def : InstAlias<"clzero\t{%rax|rax}", (CLZEROr)>, Requires<[In64BitMode]>;

//===----------------------------------------------------------------------===//
// Pattern fragments to auto generate TBM instructions.
//===----------------------------------------------------------------------===//

let Predicates = [HasTBM] in {
  // FIXME: patterns for the load versions are not implemented
  def : Pat<(and GR32:$src, (add GR32:$src, 1)),
            (BLCFILL32rr GR32:$src)>;
  def : Pat<(and GR64:$src, (add GR64:$src, 1)),
            (BLCFILL64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (not (add GR32:$src, 1))),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (not (add GR64:$src, 1))),
            (BLCI64rr GR64:$src)>;

  // Extra patterns because opt can optimize the above patterns to this.
  def : Pat<(or GR32:$src, (sub -2, GR32:$src)),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (sub -2, GR64:$src)),
            (BLCI64rr GR64:$src)>;

  def : Pat<(and (not GR32:$src), (add GR32:$src, 1)),
            (BLCIC32rr GR32:$src)>;
  def : Pat<(and (not GR64:$src), (add GR64:$src, 1)),
            (BLCIC64rr GR64:$src)>;

  def : Pat<(xor GR32:$src, (add GR32:$src, 1)),
            (BLCMSK32rr GR32:$src)>;
  def : Pat<(xor GR64:$src, (add GR64:$src, 1)),
            (BLCMSK64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (add GR32:$src, 1)),
            (BLCS32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (add GR64:$src, 1)),
            (BLCS64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (add GR32:$src, -1)),
            (BLSFILL32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (add GR64:$src, -1)),
            (BLSFILL64rr GR64:$src)>;

  def : Pat<(or (not GR32:$src), (add GR32:$src, -1)),
            (BLSIC32rr GR32:$src)>;
  def : Pat<(or (not GR64:$src), (add GR64:$src, -1)),
            (BLSIC64rr GR64:$src)>;

  def : Pat<(or (not GR32:$src), (add GR32:$src, 1)),
            (T1MSKC32rr GR32:$src)>;
  def : Pat<(or (not GR64:$src), (add GR64:$src, 1)),
            (T1MSKC64rr GR64:$src)>;

  def : Pat<(and (not GR32:$src), (add GR32:$src, -1)),
            (TZMSK32rr GR32:$src)>;
  def : Pat<(and (not GR64:$src), (add GR64:$src, -1)),
            (TZMSK64rr GR64:$src)>;

  // Patterns to match flag producing ops.
  // X86and_flag nodes are rarely created. Those should use CMP+AND. We do
  // TESTrr matching in PostProcessISelDAG to allow BLSR/BLSI to be formed.
  def : Pat<(or_flag_nocf GR32:$src, (not (add GR32:$src, 1))),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (not (add GR64:$src, 1))),
            (BLCI64rr GR64:$src)>;

  // Extra patterns because opt can optimize the above patterns to this.
  def : Pat<(or_flag_nocf GR32:$src, (sub -2, GR32:$src)),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (sub -2, GR64:$src)),
            (BLCI64rr GR64:$src)>;

  def : Pat<(xor_flag_nocf GR32:$src, (add GR32:$src, 1)),
            (BLCMSK32rr GR32:$src)>;
  def : Pat<(xor_flag_nocf GR64:$src, (add GR64:$src, 1)),
            (BLCMSK64rr GR64:$src)>;

  def : Pat<(or_flag_nocf GR32:$src, (add GR32:$src, 1)),
            (BLCS32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (add GR64:$src, 1)),
            (BLCS64rr GR64:$src)>;

  def : Pat<(or_flag_nocf GR32:$src, (add GR32:$src, -1)),
            (BLSFILL32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (add GR64:$src, -1)),
            (BLSFILL64rr GR64:$src)>;

  def : Pat<(or_flag_nocf (not GR32:$src), (add GR32:$src, -1)),
            (BLSIC32rr GR32:$src)>;
  def : Pat<(or_flag_nocf (not GR64:$src), (add GR64:$src, -1)),
            (BLSIC64rr GR64:$src)>;

  def : Pat<(or_flag_nocf (not GR32:$src), (add GR32:$src, 1)),
            (T1MSKC32rr GR32:$src)>;
  def : Pat<(or_flag_nocf (not GR64:$src), (add GR64:$src, 1)),
            (T1MSKC64rr GR64:$src)>;
} // HasTBM

//===----------------------------------------------------------------------===//
// Memory Instructions
//

let Predicates = [HasCLFLUSHOPT], SchedRW = [WriteLoad] in
def CLFLUSHOPT : I<0xAE, MRM7m, (outs), (ins i8mem:$src),
                   "clflushopt\t$src", [(int_x86_clflushopt addr:$src)]>, PD;

let Predicates = [HasCLWB], SchedRW = [WriteLoad] in
def CLWB       : I<0xAE, MRM6m, (outs), (ins i8mem:$src), "clwb\t$src",
                   [(int_x86_clwb addr:$src)]>, PD, NotMemoryFoldable;

let Predicates = [HasCLDEMOTE], SchedRW = [WriteLoad] in
def CLDEMOTE : I<0x1C, MRM0m, (outs), (ins i8mem:$src), "cldemote\t$src",
                   [(int_x86_cldemote addr:$src)]>, TB;

//===----------------------------------------------------------------------===//
// Subsystems.
//===----------------------------------------------------------------------===//

include "X86InstrArithmetic.td"
include "X86InstrCMovSetCC.td"
include "X86InstrExtension.td"
include "X86InstrControl.td"
include "X86InstrShiftRotate.td"

// X87 Floating Point Stack.
include "X86InstrFPStack.td"

// SIMD support (SSE, MMX and AVX)
include "X86InstrFragmentsSIMD.td"

// FMA - Fused Multiply-Add support (requires FMA)
include "X86InstrFMA.td"

// XOP
include "X86InstrXOP.td"

// SSE, MMX and 3DNow! vector support.
include "X86InstrSSE.td"
include "X86InstrAVX512.td"
include "X86InstrMMX.td"
include "X86Instr3DNow.td"

// MPX instructions
include "X86InstrMPX.td"

include "X86InstrVMX.td"
include "X86InstrSVM.td"

include "X86InstrTSX.td"
include "X86InstrSGX.td"

// System instructions.
include "X86InstrSystem.td"

// Compiler Pseudo Instructions and Pat Patterns
include "X86InstrCompiler.td"
include "X86InstrVecCompiler.td"

//===----------------------------------------------------------------------===//
// Assembler Mnemonic Aliases
//===----------------------------------------------------------------------===//

def : MnemonicAlias<"call", "callw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"call", "calll", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"call", "callq", "att">, Requires<[In64BitMode]>;

def : MnemonicAlias<"cbw",  "cbtw", "att">;
def : MnemonicAlias<"cwde", "cwtl", "att">;
def : MnemonicAlias<"cwd",  "cwtd", "att">;
def : MnemonicAlias<"cdq",  "cltd", "att">;
def : MnemonicAlias<"cdqe", "cltq", "att">;
def : MnemonicAlias<"cqo",  "cqto", "att">;

// In 64-bit mode lret maps to lretl; it is not ambiguous with lretq.
def : MnemonicAlias<"lret", "lretw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lret", "lretl", "att">, Requires<[Not16BitMode]>;

def : MnemonicAlias<"leavel", "leave", "att">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"leaveq", "leave", "att">, Requires<[In64BitMode]>;

def : MnemonicAlias<"loopz",  "loope">;
def : MnemonicAlias<"loopnz", "loopne">;

def : MnemonicAlias<"pop",   "popw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pop",   "popl",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pop",   "popq",  "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popf",  "popfw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popf",  "popfl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"popf",  "popfq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popf",  "popfq", "intel">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popfd", "popfl", "att">;
def : MnemonicAlias<"popfw", "popf",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"popfw", "popf",  "intel">, Requires<[In64BitMode]>;

// FIXME: This is wrong for "push reg".  "push %bx" should turn into pushw in
// all modes.  However: "push (addr)" and "push $42" should default to
// pushl/pushq depending on the current mode.  Similar for "pop %bx"
def : MnemonicAlias<"push",   "pushw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"push",   "pushl",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"push",   "pushq",  "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushf",  "pushfw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pushf",  "pushfl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pushf",  "pushfq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushf",  "pushfq", "intel">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushfd", "pushfl", "att">;
def : MnemonicAlias<"pushfw", "pushf",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pushfw", "pushf",  "intel">, Requires<[In64BitMode]>;

def : MnemonicAlias<"popad",  "popal",  "intel">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"pushad", "pushal", "intel">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"popa",   "popaw",  "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pusha",  "pushaw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popa",   "popal",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pusha",  "pushal", "intel">, Requires<[In32BitMode]>;

def : MnemonicAlias<"popa",   "popaw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pusha",  "pushaw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popa",   "popal",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pusha",  "pushal", "att">, Requires<[In32BitMode]>;

def : MnemonicAlias<"repe",  "rep">;
def : MnemonicAlias<"repz",  "rep">;
def : MnemonicAlias<"repnz", "repne">;

def : MnemonicAlias<"ret", "retw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"ret", "retl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"ret", "retq", "att">, Requires<[In64BitMode]>;

// Apply 'ret' behavior to 'retn'
def : MnemonicAlias<"retn", "retw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"retn", "retl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"retn", "retq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"retn", "ret", "intel">;

def : MnemonicAlias<"sal", "shl", "intel">;
def : MnemonicAlias<"salb", "shlb", "att">;
def : MnemonicAlias<"salw", "shlw", "att">;
def : MnemonicAlias<"sall", "shll", "att">;
def : MnemonicAlias<"salq", "shlq", "att">;

def : MnemonicAlias<"smovb", "movsb", "att">;
def : MnemonicAlias<"smovw", "movsw", "att">;
def : MnemonicAlias<"smovl", "movsl", "att">;
def : MnemonicAlias<"smovq", "movsq", "att">;

def : MnemonicAlias<"ud2a",  "ud2",  "att">;
def : MnemonicAlias<"verrw", "verr", "att">;

// MS recognizes 'xacquire'/'xrelease' as 'acquire'/'release'
def : MnemonicAlias<"acquire", "xacquire", "intel">;
def : MnemonicAlias<"release", "xrelease", "intel">;

// System instruction aliases.
def : MnemonicAlias<"iret",    "iretw",    "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"iret",    "iretl",    "att">, Requires<[Not16BitMode]>;
def : MnemonicAlias<"sysret",  "sysretl",  "att">;
def : MnemonicAlias<"sysexit", "sysexitl", "att">;

def : MnemonicAlias<"lgdt", "lgdtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"lidt", "lidtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lidt", "lidtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lidt", "lidtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"sidt", "sidtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sidt", "sidtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sidt", "sidtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lidt", "lidtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lidt", "lidtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sidt", "sidtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sidt", "sidtd", "intel">, Requires<[In32BitMode]>;


// Floating point stack aliases.
def : MnemonicAlias<"fcmovz",   "fcmove",   "att">;
def : MnemonicAlias<"fcmova",   "fcmovnbe", "att">;
def : MnemonicAlias<"fcmovnae", "fcmovb",   "att">;
def : MnemonicAlias<"fcmovna",  "fcmovbe",  "att">;
def : MnemonicAlias<"fcmovae",  "fcmovnb",  "att">;
def : MnemonicAlias<"fcomip",   "fcompi">;
def : MnemonicAlias<"fildq",    "fildll",   "att">;
def : MnemonicAlias<"fistpq",   "fistpll",  "att">;
def : MnemonicAlias<"fisttpq",  "fisttpll", "att">;
def : MnemonicAlias<"fldcww",   "fldcw",    "att">;
def : MnemonicAlias<"fnstcww",  "fnstcw",   "att">;
def : MnemonicAlias<"fnstsww",  "fnstsw",   "att">;
def : MnemonicAlias<"fucomip",  "fucompi">;
def : MnemonicAlias<"fwait",    "wait">;

def : MnemonicAlias<"fxsaveq",   "fxsave64",   "att">;
def : MnemonicAlias<"fxrstorq",  "fxrstor64",  "att">;
def : MnemonicAlias<"xsaveq",    "xsave64",    "att">;
def : MnemonicAlias<"xrstorq",   "xrstor64",   "att">;
def : MnemonicAlias<"xsaveoptq", "xsaveopt64", "att">;
def : MnemonicAlias<"xrstorsq",  "xrstors64",  "att">;
def : MnemonicAlias<"xsavecq",   "xsavec64",   "att">;
def : MnemonicAlias<"xsavesq",   "xsaves64",   "att">;

class CondCodeAlias<string Prefix,string Suffix, string OldCond, string NewCond,
                    string VariantName>
  : MnemonicAlias<!strconcat(Prefix, OldCond, Suffix),
                  !strconcat(Prefix, NewCond, Suffix), VariantName>;

/// IntegerCondCodeMnemonicAlias - This multiclass defines a bunch of
/// MnemonicAlias's that canonicalize the condition code in a mnemonic, for
/// example "setz" -> "sete".
multiclass IntegerCondCodeMnemonicAlias<string Prefix, string Suffix,
                                        string V = ""> {
  def C   : CondCodeAlias<Prefix, Suffix, "c",   "b",  V>; // setc   -> setb
  def Z   : CondCodeAlias<Prefix, Suffix, "z" ,  "e",  V>; // setz   -> sete
  def NA  : CondCodeAlias<Prefix, Suffix, "na",  "be", V>; // setna  -> setbe
  def NB  : CondCodeAlias<Prefix, Suffix, "nb",  "ae", V>; // setnb  -> setae
  def NC  : CondCodeAlias<Prefix, Suffix, "nc",  "ae", V>; // setnc  -> setae
  def NG  : CondCodeAlias<Prefix, Suffix, "ng",  "le", V>; // setng  -> setle
  def NL  : CondCodeAlias<Prefix, Suffix, "nl",  "ge", V>; // setnl  -> setge
  def NZ  : CondCodeAlias<Prefix, Suffix, "nz",  "ne", V>; // setnz  -> setne
  def PE  : CondCodeAlias<Prefix, Suffix, "pe",  "p",  V>; // setpe  -> setp
  def PO  : CondCodeAlias<Prefix, Suffix, "po",  "np", V>; // setpo  -> setnp

  def NAE : CondCodeAlias<Prefix, Suffix, "nae", "b",  V>; // setnae -> setb
  def NBE : CondCodeAlias<Prefix, Suffix, "nbe", "a",  V>; // setnbe -> seta
  def NGE : CondCodeAlias<Prefix, Suffix, "nge", "l",  V>; // setnge -> setl
  def NLE : CondCodeAlias<Prefix, Suffix, "nle", "g",  V>; // setnle -> setg
}

// Aliases for set<CC>
defm : IntegerCondCodeMnemonicAlias<"set", "">;
// Aliases for j<CC>
defm : IntegerCondCodeMnemonicAlias<"j", "">;
// Aliases for cmov<CC>{w,l,q}
defm : IntegerCondCodeMnemonicAlias<"cmov", "w", "att">;
defm : IntegerCondCodeMnemonicAlias<"cmov", "l", "att">;
defm : IntegerCondCodeMnemonicAlias<"cmov", "q", "att">;
// No size suffix for intel-style asm.
defm : IntegerCondCodeMnemonicAlias<"cmov", "", "intel">;


//===----------------------------------------------------------------------===//
// Assembler Instruction Aliases
//===----------------------------------------------------------------------===//

// aad/aam default to base 10 if no operand is specified.
def : InstAlias<"aad", (AAD8i8 10)>, Requires<[Not64BitMode]>;
def : InstAlias<"aam", (AAM8i8 10)>, Requires<[Not64BitMode]>;

// Disambiguate the mem/imm form of bt-without-a-suffix as btl.
// Likewise for btc/btr/bts.
def : InstAlias<"bt\t{$imm, $mem|$mem, $imm}",
                (BT32mi8 i32mem:$mem, i32i8imm:$imm), 0, "att">;
def : InstAlias<"btc\t{$imm, $mem|$mem, $imm}",
                (BTC32mi8 i32mem:$mem, i32i8imm:$imm), 0, "att">;
def : InstAlias<"btr\t{$imm, $mem|$mem, $imm}",
                (BTR32mi8 i32mem:$mem, i32i8imm:$imm), 0, "att">;
def : InstAlias<"bts\t{$imm, $mem|$mem, $imm}",
                (BTS32mi8 i32mem:$mem, i32i8imm:$imm), 0, "att">;

// clr aliases.
def : InstAlias<"clr{b}\t$reg", (XOR8rr  GR8 :$reg, GR8 :$reg), 0>;
def : InstAlias<"clr{w}\t$reg", (XOR16rr GR16:$reg, GR16:$reg), 0>;
def : InstAlias<"clr{l}\t$reg", (XOR32rr GR32:$reg, GR32:$reg), 0>;
def : InstAlias<"clr{q}\t$reg", (XOR64rr GR64:$reg, GR64:$reg), 0>;

// lods aliases. Accept the destination being omitted because it's implicit
// in the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"lodsb\t$src", (LODSB srcidx8:$src),  0>;
def : InstAlias<"lodsw\t$src", (LODSW srcidx16:$src), 0>;
def : InstAlias<"lods{l|d}\t$src", (LODSL srcidx32:$src), 0>;
def : InstAlias<"lodsq\t$src", (LODSQ srcidx64:$src), 0>, Requires<[In64BitMode]>;
def : InstAlias<"lods\t{$src, %al|al, $src}", (LODSB srcidx8:$src),  0>;
def : InstAlias<"lods\t{$src, %ax|ax, $src}", (LODSW srcidx16:$src), 0>;
def : InstAlias<"lods\t{$src, %eax|eax, $src}", (LODSL srcidx32:$src), 0>;
def : InstAlias<"lods\t{$src, %rax|rax, $src}", (LODSQ srcidx64:$src), 0>, Requires<[In64BitMode]>;
def : InstAlias<"lods\t$src", (LODSB srcidx8:$src),  0, "intel">;
def : InstAlias<"lods\t$src", (LODSW srcidx16:$src), 0, "intel">;
def : InstAlias<"lods\t$src", (LODSL srcidx32:$src), 0, "intel">;
def : InstAlias<"lods\t$src", (LODSQ srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;


// stos aliases. Accept the source being omitted because it's implicit in
// the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the source.
def : InstAlias<"stosb\t$dst", (STOSB dstidx8:$dst),  0>;
def : InstAlias<"stosw\t$dst", (STOSW dstidx16:$dst), 0>;
def : InstAlias<"stos{l|d}\t$dst", (STOSL dstidx32:$dst), 0>;
def : InstAlias<"stosq\t$dst", (STOSQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"stos\t{%al, $dst|$dst, al}", (STOSB dstidx8:$dst),  0>;
def : InstAlias<"stos\t{%ax, $dst|$dst, ax}", (STOSW dstidx16:$dst), 0>;
def : InstAlias<"stos\t{%eax, $dst|$dst, eax}", (STOSL dstidx32:$dst), 0>;
def : InstAlias<"stos\t{%rax, $dst|$dst, rax}", (STOSQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"stos\t$dst", (STOSB dstidx8:$dst),  0, "intel">;
def : InstAlias<"stos\t$dst", (STOSW dstidx16:$dst), 0, "intel">;
def : InstAlias<"stos\t$dst", (STOSL dstidx32:$dst), 0, "intel">;
def : InstAlias<"stos\t$dst", (STOSQ dstidx64:$dst), 0, "intel">, Requires<[In64BitMode]>;


// scas aliases. Accept the destination being omitted because it's implicit
// in the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"scasb\t$dst", (SCASB dstidx8:$dst),  0>;
def : InstAlias<"scasw\t$dst", (SCASW dstidx16:$dst), 0>;
def : InstAlias<"scas{l|d}\t$dst", (SCASL dstidx32:$dst), 0>;
def : InstAlias<"scasq\t$dst", (SCASQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"scas\t{$dst, %al|al, $dst}", (SCASB dstidx8:$dst),  0>;
def : InstAlias<"scas\t{$dst, %ax|ax, $dst}", (SCASW dstidx16:$dst), 0>;
def : InstAlias<"scas\t{$dst, %eax|eax, $dst}", (SCASL dstidx32:$dst), 0>;
def : InstAlias<"scas\t{$dst, %rax|rax, $dst}", (SCASQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"scas\t$dst", (SCASB dstidx8:$dst),  0, "intel">;
def : InstAlias<"scas\t$dst", (SCASW dstidx16:$dst), 0, "intel">;
def : InstAlias<"scas\t$dst", (SCASL dstidx32:$dst), 0, "intel">;
def : InstAlias<"scas\t$dst", (SCASQ dstidx64:$dst), 0, "intel">, Requires<[In64BitMode]>;

// cmps aliases. Mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSB dstidx8:$dst, srcidx8:$src),   0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSW dstidx16:$dst, srcidx16:$src), 0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSL dstidx32:$dst, srcidx32:$src), 0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSQ dstidx64:$dst, srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;

// movs aliases. Mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSB dstidx8:$dst, srcidx8:$src),   0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSW dstidx16:$dst, srcidx16:$src), 0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSL dstidx32:$dst, srcidx32:$src), 0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSQ dstidx64:$dst, srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;

// div and idiv aliases for explicit A register.
def : InstAlias<"div{b}\t{$src, %al|al, $src}", (DIV8r  GR8 :$src)>;
def : InstAlias<"div{w}\t{$src, %ax|ax, $src}", (DIV16r GR16:$src)>;
def : InstAlias<"div{l}\t{$src, %eax|eax, $src}", (DIV32r GR32:$src)>;
def : InstAlias<"div{q}\t{$src, %rax|rax, $src}", (DIV64r GR64:$src)>;
def : InstAlias<"div{b}\t{$src, %al|al, $src}", (DIV8m  i8mem :$src)>;
def : InstAlias<"div{w}\t{$src, %ax|ax, $src}", (DIV16m i16mem:$src)>;
def : InstAlias<"div{l}\t{$src, %eax|eax, $src}", (DIV32m i32mem:$src)>;
def : InstAlias<"div{q}\t{$src, %rax|rax, $src}", (DIV64m i64mem:$src)>;
def : InstAlias<"idiv{b}\t{$src, %al|al, $src}", (IDIV8r  GR8 :$src)>;
def : InstAlias<"idiv{w}\t{$src, %ax|ax, $src}", (IDIV16r GR16:$src)>;
def : InstAlias<"idiv{l}\t{$src, %eax|eax, $src}", (IDIV32r GR32:$src)>;
def : InstAlias<"idiv{q}\t{$src, %rax|rax, $src}", (IDIV64r GR64:$src)>;
def : InstAlias<"idiv{b}\t{$src, %al|al, $src}", (IDIV8m  i8mem :$src)>;
def : InstAlias<"idiv{w}\t{$src, %ax|ax, $src}", (IDIV16m i16mem:$src)>;
def : InstAlias<"idiv{l}\t{$src, %eax|eax, $src}", (IDIV32m i32mem:$src)>;
def : InstAlias<"idiv{q}\t{$src, %rax|rax, $src}", (IDIV64m i64mem:$src)>;



// Various unary fpstack operations default to operating on ST1.
// For example, "fxch" -> "fxch %st(1)"
def : InstAlias<"faddp",        (ADD_FPrST0  ST1), 0>;
def:  InstAlias<"fadd",         (ADD_FPrST0  ST1), 0>;
def : InstAlias<"fsub{|r}p",    (SUBR_FPrST0 ST1), 0>;
def : InstAlias<"fsub{r|}p",    (SUB_FPrST0  ST1), 0>;
def : InstAlias<"fmul",         (MUL_FPrST0  ST1), 0>;
def : InstAlias<"fmulp",        (MUL_FPrST0  ST1), 0>;
def : InstAlias<"fdiv{|r}p",    (DIVR_FPrST0 ST1), 0>;
def : InstAlias<"fdiv{r|}p",    (DIV_FPrST0  ST1), 0>;
def : InstAlias<"fxch",         (XCH_F       ST1), 0>;
def : InstAlias<"fcom",         (COM_FST0r   ST1), 0>;
def : InstAlias<"fcomp",        (COMP_FST0r  ST1), 0>;
def : InstAlias<"fcomi",        (COM_FIr     ST1), 0>;
def : InstAlias<"fcompi",       (COM_FIPr    ST1), 0>;
def : InstAlias<"fucom",        (UCOM_Fr     ST1), 0>;
def : InstAlias<"fucomp",       (UCOM_FPr    ST1), 0>;
def : InstAlias<"fucomi",       (UCOM_FIr    ST1), 0>;
def : InstAlias<"fucompi",      (UCOM_FIPr   ST1), 0>;

// Handle fmul/fadd/fsub/fdiv instructions with explicitly written st(0) op.
// For example, "fadd %st(4), %st(0)" -> "fadd %st(4)".  We also disambiguate
// instructions like "fadd %st(0), %st(0)" as "fadd %st(0)" for consistency with
// gas.
multiclass FpUnaryAlias<string Mnemonic, Instruction Inst, bit EmitAlias = 1> {
 def : InstAlias<!strconcat(Mnemonic, "\t$op"),
                 (Inst RSTi:$op), EmitAlias>;
 def : InstAlias<!strconcat(Mnemonic, "\t{%st, %st|st, st}"),
                 (Inst ST0), EmitAlias>;
}

defm : FpUnaryAlias<"fadd",   ADD_FST0r, 0>;
defm : FpUnaryAlias<"faddp",  ADD_FPrST0, 0>;
defm : FpUnaryAlias<"fsub",   SUB_FST0r, 0>;
defm : FpUnaryAlias<"fsub{|r}p",  SUBR_FPrST0, 0>;
defm : FpUnaryAlias<"fsubr",  SUBR_FST0r, 0>;
defm : FpUnaryAlias<"fsub{r|}p", SUB_FPrST0, 0>;
defm : FpUnaryAlias<"fmul",   MUL_FST0r, 0>;
defm : FpUnaryAlias<"fmulp",  MUL_FPrST0, 0>;
defm : FpUnaryAlias<"fdiv",   DIV_FST0r, 0>;
defm : FpUnaryAlias<"fdiv{|r}p",  DIVR_FPrST0, 0>;
defm : FpUnaryAlias<"fdivr",  DIVR_FST0r, 0>;
defm : FpUnaryAlias<"fdiv{r|}p", DIV_FPrST0, 0>;
defm : FpUnaryAlias<"fcomi",   COM_FIr, 0>;
defm : FpUnaryAlias<"fucomi",  UCOM_FIr, 0>;
defm : FpUnaryAlias<"fcompi",   COM_FIPr, 0>;
defm : FpUnaryAlias<"fucompi",  UCOM_FIPr, 0>;


// Handle "f{mulp,addp} $op, %st(0)" the same as "f{mulp,addp} $op", since they
// commute.  We also allow fdiv[r]p/fsubrp even though they don't commute,
// solely because gas supports it.
def : InstAlias<"faddp\t{$op, %st|st, $op}", (ADD_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fmulp\t{$op, %st|st, $op}", (MUL_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fsub{|r}p\t{$op, %st|st, $op}", (SUBR_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fsub{r|}p\t{$op, %st|st, $op}", (SUB_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fdiv{|r}p\t{$op, %st|st, $op}", (DIVR_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fdiv{r|}p\t{$op, %st|st, $op}", (DIV_FPrST0 RSTi:$op), 0>;

def : InstAlias<"fnstsw"     , (FNSTSW16r), 0>;

// lcall and ljmp aliases.  This seems to be an odd mapping in 64-bit mode, but
// this is compatible with what GAS does.
def : InstAlias<"lcall\t$seg, $off", (FARCALL32i i32imm:$off, i16imm:$seg), 0>, Requires<[In32BitMode]>;
def : InstAlias<"ljmp\t$seg, $off",  (FARJMP32i  i32imm:$off, i16imm:$seg), 0>, Requires<[In32BitMode]>;
def : InstAlias<"lcall\t{*}$dst",    (FARCALL32m opaquemem:$dst), 0>, Requires<[Not16BitMode]>;
def : InstAlias<"ljmp\t{*}$dst",     (FARJMP32m  opaquemem:$dst), 0>, Requires<[Not16BitMode]>;
def : InstAlias<"lcall\t$seg, $off", (FARCALL16i i16imm:$off, i16imm:$seg), 0>, Requires<[In16BitMode]>;
def : InstAlias<"ljmp\t$seg, $off",  (FARJMP16i  i16imm:$off, i16imm:$seg), 0>, Requires<[In16BitMode]>;
def : InstAlias<"lcall\t{*}$dst",    (FARCALL16m opaquemem:$dst), 0>, Requires<[In16BitMode]>;
def : InstAlias<"ljmp\t{*}$dst",     (FARJMP16m  opaquemem:$dst), 0>, Requires<[In16BitMode]>;

def : InstAlias<"jmp\t{*}$dst",      (JMP64m  i64mem:$dst), 0, "att">, Requires<[In64BitMode]>;
def : InstAlias<"jmp\t{*}$dst",      (JMP32m  i32mem:$dst), 0, "att">, Requires<[In32BitMode]>;
def : InstAlias<"jmp\t{*}$dst",      (JMP16m  i16mem:$dst), 0, "att">, Requires<[In16BitMode]>;


// "imul <imm>, B" is an alias for "imul <imm>, B, B".
def : InstAlias<"imul{w}\t{$imm, $r|$r, $imm}", (IMUL16rri  GR16:$r, GR16:$r, i16imm:$imm), 0>;
def : InstAlias<"imul{w}\t{$imm, $r|$r, $imm}", (IMUL16rri8 GR16:$r, GR16:$r, i16i8imm:$imm), 0>;
def : InstAlias<"imul{l}\t{$imm, $r|$r, $imm}", (IMUL32rri  GR32:$r, GR32:$r, i32imm:$imm), 0>;
def : InstAlias<"imul{l}\t{$imm, $r|$r, $imm}", (IMUL32rri8 GR32:$r, GR32:$r, i32i8imm:$imm), 0>;
def : InstAlias<"imul{q}\t{$imm, $r|$r, $imm}", (IMUL64rri32 GR64:$r, GR64:$r, i64i32imm:$imm), 0>;
def : InstAlias<"imul{q}\t{$imm, $r|$r, $imm}", (IMUL64rri8 GR64:$r, GR64:$r, i64i8imm:$imm), 0>;

// ins aliases. Accept the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSB dstidx8:$dst),  0, "intel">;
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSW dstidx16:$dst), 0, "intel">;
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSL dstidx32:$dst), 0, "intel">;

// outs aliases. Accept the mnemonic suffix being omitted because it's implicit
// in the source.
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSB srcidx8:$src),  0, "intel">;
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSW srcidx16:$src), 0, "intel">;
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSL srcidx32:$src), 0, "intel">;

// inb %dx -> inb %al, %dx
def : InstAlias<"inb\t{%dx|dx}", (IN8rr), 0>;
def : InstAlias<"inw\t{%dx|dx}", (IN16rr), 0>;
def : InstAlias<"inl\t{%dx|dx}", (IN32rr), 0>;
def : InstAlias<"inb\t$port", (IN8ri u8imm:$port), 0>;
def : InstAlias<"inw\t$port", (IN16ri u8imm:$port), 0>;
def : InstAlias<"inl\t$port", (IN32ri u8imm:$port), 0>;


// jmp and call aliases for lcall and ljmp.  jmp $42,$5 -> ljmp
def : InstAlias<"call\t$seg, $off",  (FARCALL16i i16imm:$off, i16imm:$seg)>, Requires<[In16BitMode]>;
def : InstAlias<"jmp\t$seg, $off",   (FARJMP16i  i16imm:$off, i16imm:$seg)>, Requires<[In16BitMode]>;
def : InstAlias<"call\t$seg, $off",  (FARCALL32i i32imm:$off, i16imm:$seg)>, Requires<[In32BitMode]>;
def : InstAlias<"jmp\t$seg, $off",   (FARJMP32i  i32imm:$off, i16imm:$seg)>, Requires<[In32BitMode]>;
def : InstAlias<"callw\t$seg, $off", (FARCALL16i i16imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"jmpw\t$seg, $off",  (FARJMP16i  i16imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"calll\t$seg, $off", (FARCALL32i i32imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"jmpl\t$seg, $off",  (FARJMP32i  i32imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;

// Match 'movq <largeimm>, <reg>' as an alias for movabsq.
def : InstAlias<"mov{q}\t{$imm, $reg|$reg, $imm}", (MOV64ri GR64:$reg, i64imm:$imm), 0>;

// Match 'movd GR64, MMX' as an alias for movq to be compatible with gas,
// which supports this due to an old AMD documentation bug when 64-bit mode was
// created.
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MMX_MOVD64to64rr VR64:$dst, GR64:$src), 0>;
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MMX_MOVD64from64rr GR64:$dst, VR64:$src), 0>;

// movsx aliases
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX16rr8 GR16:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX16rm8 GR16:$dst, i8mem:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX32rr8 GR32:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX32rr16 GR32:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr8 GR64:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr16 GR64:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr32 GR64:$dst, GR32:$src), 0, "att">;

// movzx aliases
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX16rr8 GR16:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX16rm8 GR16:$dst, i8mem:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX32rr8 GR32:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX32rr16 GR32:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX64rr8 GR64:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX64rr16 GR64:$dst, GR16:$src), 0, "att">;
// Note: No GR32->GR64 movzx form.

// outb %dx -> outb %al, %dx
def : InstAlias<"outb\t{%dx|dx}", (OUT8rr), 0>;
def : InstAlias<"outw\t{%dx|dx}", (OUT16rr), 0>;
def : InstAlias<"outl\t{%dx|dx}", (OUT32rr), 0>;
def : InstAlias<"outb\t$port", (OUT8ir u8imm:$port), 0>;
def : InstAlias<"outw\t$port", (OUT16ir u8imm:$port), 0>;
def : InstAlias<"outl\t$port", (OUT32ir u8imm:$port), 0>;

// 'sldt <mem>' can be encoded with either sldtw or sldtq with the same
// effect (both store to a 16-bit mem).  Force to sldtw to avoid ambiguity
// errors, since its encoding is the most compact.
def : InstAlias<"sldt $mem", (SLDT16m i16mem:$mem), 0>;

// shld/shrd op,op -> shld op, op, CL
def : InstAlias<"shld{w}\t{$r2, $r1|$r1, $r2}", (SHLD16rrCL GR16:$r1, GR16:$r2), 0>;
def : InstAlias<"shld{l}\t{$r2, $r1|$r1, $r2}", (SHLD32rrCL GR32:$r1, GR32:$r2), 0>;
def : InstAlias<"shld{q}\t{$r2, $r1|$r1, $r2}", (SHLD64rrCL GR64:$r1, GR64:$r2), 0>;
def : InstAlias<"shrd{w}\t{$r2, $r1|$r1, $r2}", (SHRD16rrCL GR16:$r1, GR16:$r2), 0>;
def : InstAlias<"shrd{l}\t{$r2, $r1|$r1, $r2}", (SHRD32rrCL GR32:$r1, GR32:$r2), 0>;
def : InstAlias<"shrd{q}\t{$r2, $r1|$r1, $r2}", (SHRD64rrCL GR64:$r1, GR64:$r2), 0>;

def : InstAlias<"shld{w}\t{$reg, $mem|$mem, $reg}", (SHLD16mrCL i16mem:$mem, GR16:$reg), 0>;
def : InstAlias<"shld{l}\t{$reg, $mem|$mem, $reg}", (SHLD32mrCL i32mem:$mem, GR32:$reg), 0>;
def : InstAlias<"shld{q}\t{$reg, $mem|$mem, $reg}", (SHLD64mrCL i64mem:$mem, GR64:$reg), 0>;
def : InstAlias<"shrd{w}\t{$reg, $mem|$mem, $reg}", (SHRD16mrCL i16mem:$mem, GR16:$reg), 0>;
def : InstAlias<"shrd{l}\t{$reg, $mem|$mem, $reg}", (SHRD32mrCL i32mem:$mem, GR32:$reg), 0>;
def : InstAlias<"shrd{q}\t{$reg, $mem|$mem, $reg}", (SHRD64mrCL i64mem:$mem, GR64:$reg), 0>;

/*  FIXME: This is disabled because the asm matcher is currently incapable of
 *  matching a fixed immediate like $1.
// "shl X, $1" is an alias for "shl X".
multiclass ShiftRotateByOneAlias<string Mnemonic, string Opc> {
 def : InstAlias<!strconcat(Mnemonic, "b $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "8r1")) GR8:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "w $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "16r1")) GR16:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "l $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "32r1")) GR32:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "q $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "64r1")) GR64:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "b $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "8m1")) i8mem:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "w $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "16m1")) i16mem:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "l $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "32m1")) i32mem:$op)>;
 def : InstAlias<!strconcat(Mnemonic, "q $op, $$1"),
                 (!cast<Instruction>(!strconcat(Opc, "64m1")) i64mem:$op)>;
}

defm : ShiftRotateByOneAlias<"rcl", "RCL">;
defm : ShiftRotateByOneAlias<"rcr", "RCR">;
defm : ShiftRotateByOneAlias<"rol", "ROL">;
defm : ShiftRotateByOneAlias<"ror", "ROR">;
FIXME */

// test: We accept "testX <reg>, <mem>" and "testX <mem>, <reg>" as synonyms.
def : InstAlias<"test{b}\t{$mem, $val|$val, $mem}",
                (TEST8mr  i8mem :$mem, GR8 :$val), 0>;
def : InstAlias<"test{w}\t{$mem, $val|$val, $mem}",
                (TEST16mr i16mem:$mem, GR16:$val), 0>;
def : InstAlias<"test{l}\t{$mem, $val|$val, $mem}",
                (TEST32mr i32mem:$mem, GR32:$val), 0>;
def : InstAlias<"test{q}\t{$mem, $val|$val, $mem}",
                (TEST64mr i64mem:$mem, GR64:$val), 0>;

// xchg: We accept "xchgX <reg>, <mem>" and "xchgX <mem>, <reg>" as synonyms.
def : InstAlias<"xchg{b}\t{$mem, $val|$val, $mem}",
                (XCHG8rm  GR8 :$val, i8mem :$mem), 0>;
def : InstAlias<"xchg{w}\t{$mem, $val|$val, $mem}",
                (XCHG16rm GR16:$val, i16mem:$mem), 0>;
def : InstAlias<"xchg{l}\t{$mem, $val|$val, $mem}",
                (XCHG32rm GR32:$val, i32mem:$mem), 0>;
def : InstAlias<"xchg{q}\t{$mem, $val|$val, $mem}",
                (XCHG64rm GR64:$val, i64mem:$mem), 0>;

// xchg: We accept "xchgX <reg>, %eax" and "xchgX %eax, <reg>" as synonyms.
def : InstAlias<"xchg{w}\t{%ax, $src|$src, ax}", (XCHG16ar GR16:$src), 0>;
def : InstAlias<"xchg{l}\t{%eax, $src|$src, eax}", (XCHG32ar GR32:$src), 0>;
def : InstAlias<"xchg{q}\t{%rax, $src|$src, rax}", (XCHG64ar GR64:$src), 0>;

// In 64-bit mode, xchg %eax, %eax can't be encoded with the 0x90 opcode we
// would get by default because it's defined as NOP. But xchg %eax, %eax implies
// implicit zeroing of the upper 32 bits. So alias to the longer encoding.
def : InstAlias<"xchg{l}\t{%eax, %eax|eax, eax}",
                (XCHG32rr EAX, EAX), 0>, Requires<[In64BitMode]>;

// xchg %rax, %rax is a nop in x86-64 and can be encoded as such. Without this
// we emit an unneeded REX.w prefix.
def : InstAlias<"xchg{q}\t{%rax, %rax|rax, rax}", (NOOP), 0>;

// These aliases exist to get the parser to prioritize matching 8-bit
// immediate encodings over matching the implicit ax/eax/rax encodings. By
// explicitly mentioning the A register here, these entries will be ordered
// first due to the more explicit immediate type.
def : InstAlias<"adc{w}\t{$imm, %ax|ax, $imm}", (ADC16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"add{w}\t{$imm, %ax|ax, $imm}", (ADD16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"and{w}\t{$imm, %ax|ax, $imm}", (AND16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"cmp{w}\t{$imm, %ax|ax, $imm}", (CMP16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"or{w}\t{$imm, %ax|ax, $imm}",  (OR16ri8 AX,  i16i8imm:$imm), 0>;
def : InstAlias<"sbb{w}\t{$imm, %ax|ax, $imm}", (SBB16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"sub{w}\t{$imm, %ax|ax, $imm}", (SUB16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"xor{w}\t{$imm, %ax|ax, $imm}", (XOR16ri8 AX, i16i8imm:$imm), 0>;

def : InstAlias<"adc{l}\t{$imm, %eax|eax, $imm}", (ADC32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"add{l}\t{$imm, %eax|eax, $imm}", (ADD32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"and{l}\t{$imm, %eax|eax, $imm}", (AND32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"cmp{l}\t{$imm, %eax|eax, $imm}", (CMP32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"or{l}\t{$imm, %eax|eax, $imm}",  (OR32ri8 EAX,  i32i8imm:$imm), 0>;
def : InstAlias<"sbb{l}\t{$imm, %eax|eax, $imm}", (SBB32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"sub{l}\t{$imm, %eax|eax, $imm}", (SUB32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"xor{l}\t{$imm, %eax|eax, $imm}", (XOR32ri8 EAX, i32i8imm:$imm), 0>;

def : InstAlias<"adc{q}\t{$imm, %rax|rax, $imm}", (ADC64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"add{q}\t{$imm, %rax|rax, $imm}", (ADD64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"and{q}\t{$imm, %rax|rax, $imm}", (AND64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"cmp{q}\t{$imm, %rax|rax, $imm}", (CMP64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"or{q}\t{$imm, %rax|rax, $imm}",  (OR64ri8 RAX,  i64i8imm:$imm), 0>;
def : InstAlias<"sbb{q}\t{$imm, %rax|rax, $imm}", (SBB64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"sub{q}\t{$imm, %rax|rax, $imm}", (SUB64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"xor{q}\t{$imm, %rax|rax, $imm}", (XOR64ri8 RAX, i64i8imm:$imm), 0>;
