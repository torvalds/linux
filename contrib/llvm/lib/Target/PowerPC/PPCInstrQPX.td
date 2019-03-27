//===- PPCInstrQPX.td - The PowerPC QPX Extension --*- tablegen -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file describes the QPX extension to the PowerPC instruction set.
// Reference:
// Book Q: QPX Architecture Definition. IBM (as updated in) 2011.
//
//===----------------------------------------------------------------------===//

def PPCRegQFRCAsmOperand : AsmOperandClass {
  let Name = "RegQFRC"; let PredicateMethod = "isRegNumber";
}
def qfrc : RegisterOperand<QFRC> {
  let ParserMatchClass = PPCRegQFRCAsmOperand;
}
def PPCRegQSRCAsmOperand : AsmOperandClass {
  let Name = "RegQSRC"; let PredicateMethod = "isRegNumber";
}
def qsrc : RegisterOperand<QSRC> {
  let ParserMatchClass = PPCRegQSRCAsmOperand;
}
def PPCRegQBRCAsmOperand : AsmOperandClass {
  let Name = "RegQBRC"; let PredicateMethod = "isRegNumber";
}
def qbrc : RegisterOperand<QBRC> {
  let ParserMatchClass = PPCRegQBRCAsmOperand;
}

//===----------------------------------------------------------------------===//
// Helpers for defining instructions that directly correspond to intrinsics.

// QPXA1_Int - A AForm_1 intrinsic definition.
class QPXA1_Int<bits<6> opcode, bits<5> xo, string opc, Intrinsic IntID>
  : AForm_1<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
              !strconcat(opc, " $FRT, $FRA, $FRC, $FRB"), IIC_FPFused,
                       [(set v4f64:$FRT, (IntID v4f64:$FRA, v4f64:$FRB, v4f64:$FRC))]>;
// QPXA1s_Int - A AForm_1 intrinsic definition (simple instructions).
class QPXA1s_Int<bits<6> opcode, bits<5> xo, string opc, Intrinsic IntID>
  : AForm_1<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
              !strconcat(opc, " $FRT, $FRA, $FRC, $FRB"), IIC_VecPerm,
                       [(set v4f64:$FRT, (IntID v4f64:$FRA, v4f64:$FRB, v4f64:$FRC))]>;
// QPXA2_Int - A AForm_2 intrinsic definition.
class QPXA2_Int<bits<6> opcode, bits<5> xo, string opc, Intrinsic IntID>
  : AForm_2<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
              !strconcat(opc, " $FRT, $FRA, $FRB"), IIC_FPGeneral,
                       [(set v4f64:$FRT, (IntID v4f64:$FRA, v4f64:$FRB))]>;
// QPXA3_Int - A AForm_3 intrinsic definition.
class QPXA3_Int<bits<6> opcode, bits<5> xo, string opc, Intrinsic IntID>
  : AForm_3<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRC),
              !strconcat(opc, " $FRT, $FRA, $FRC"), IIC_FPGeneral,
                       [(set v4f64:$FRT, (IntID v4f64:$FRA, v4f64:$FRC))]>;
// QPXA4_Int - A AForm_4a intrinsic definition.
class QPXA4_Int<bits<6> opcode, bits<5> xo, string opc, Intrinsic IntID>
  : AForm_4a<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRB),
              !strconcat(opc, " $FRT, $FRB"), IIC_FPGeneral,
                       [(set v4f64:$FRT, (IntID v4f64:$FRB))]>;
// QPXX18_Int - A XForm_18 intrinsic definition.
class QPXX18_Int<bits<6> opcode, bits<10> xo, string opc, Intrinsic IntID>
  : XForm_18<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
              !strconcat(opc, " $FRT, $FRA, $FRB"), IIC_FPCompare,
                       [(set v4f64:$FRT, (IntID v4f64:$FRA, v4f64:$FRB))]>;
// QPXX19_Int - A XForm_19 intrinsic definition.
class QPXX19_Int<bits<6> opcode, bits<10> xo, string opc, Intrinsic IntID>
  : XForm_19<opcode, xo, (outs qfrc:$FRT), (ins qfrc:$FRB),
              !strconcat(opc, " $FRT, $FRB"), IIC_FPGeneral,
                       [(set v4f64:$FRT, (IntID v4f64:$FRB))]>;

//===----------------------------------------------------------------------===//
// Pattern Frags.

def extloadv4f32 : PatFrag<(ops node:$ptr), (extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v4f32;
}]>;

def truncstorev4f32 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getMemoryVT() == MVT::v4f32;
}]>;
def pre_truncstv4f32 : PatFrag<(ops node:$val, node:$base, node:$offset),
                               (pre_truncst node:$val,
                                            node:$base, node:$offset), [{
  return cast<StoreSDNode>(N)->getMemoryVT() == MVT::v4f32;
}]>;

def fround_inexact : PatFrag<(ops node:$val), (fpround node:$val), [{
  return cast<ConstantSDNode>(N->getOperand(1))->getZExtValue() == 0;
}]>;

def fround_exact : PatFrag<(ops node:$val), (fpround node:$val), [{
  return cast<ConstantSDNode>(N->getOperand(1))->getZExtValue() == 1;
}]>;

let FastIselShouldIgnore = 1 in // FastIsel should ignore all u12 instrs.
  def u12 : ImmLeaf<i32, [{ return (Imm & 0xFFF) == Imm; }]>;

//===----------------------------------------------------------------------===//
// Instruction Definitions.

def HasQPX : Predicate<"PPCSubTarget->hasQPX()">;
let Predicates = [HasQPX] in {
let DecoderNamespace = "QPX" in {
let hasSideEffects = 0 in { // QPX instructions don't have side effects.
let Uses = [RM] in {
  // Add Instructions
  let isCommutable = 1 in {
    def QVFADD : AForm_2<4, 21,
                        (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                        "qvfadd $FRT, $FRA, $FRB", IIC_FPGeneral,
                        [(set v4f64:$FRT, (fadd v4f64:$FRA, v4f64:$FRB))]>;
    let isCodeGenOnly = 1 in
      def QVFADDS : QPXA2_Int<0, 21, "qvfadds", int_ppc_qpx_qvfadds>;
    def QVFADDSs : AForm_2<0, 21,
                          (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                          "qvfadds $FRT, $FRA, $FRB", IIC_FPGeneral,
                          [(set v4f32:$FRT, (fadd v4f32:$FRA, v4f32:$FRB))]>;
  }
  def QVFSUB : AForm_2<4, 20,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                      "qvfsub $FRT, $FRA, $FRB", IIC_FPGeneral,
                      [(set v4f64:$FRT, (fsub v4f64:$FRA, v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFSUBS : QPXA2_Int<0, 20, "qvfsubs", int_ppc_qpx_qvfsubs>;
  def QVFSUBSs : AForm_2<0, 20,
                        (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                        "qvfsubs $FRT, $FRA, $FRB", IIC_FPGeneral,
                        [(set v4f32:$FRT, (fsub v4f32:$FRA, v4f32:$FRB))]>;

  // Estimate Instructions
  def QVFRE : AForm_4a<4, 24, (outs qfrc:$FRT), (ins qfrc:$FRB),
                       "qvfre $FRT, $FRB", IIC_FPGeneral,
                       [(set v4f64:$FRT, (PPCfre v4f64:$FRB))]>;
  def QVFRES : QPXA4_Int<0, 24, "qvfres", int_ppc_qpx_qvfres>;
  let isCodeGenOnly = 1 in
  def QVFRESs : AForm_4a<0, 24, (outs qsrc:$FRT), (ins qsrc:$FRB),
                         "qvfres $FRT, $FRB", IIC_FPGeneral,
                         [(set v4f32:$FRT, (PPCfre v4f32:$FRB))]>;

  def QVFRSQRTE : AForm_4a<4, 26, (outs qfrc:$FRT), (ins qfrc:$FRB),
                           "qvfrsqrte $FRT, $FRB", IIC_FPGeneral,
                           [(set v4f64:$FRT, (PPCfrsqrte v4f64:$FRB))]>;
  def QVFRSQRTES : QPXA4_Int<0, 26, "qvfrsqrtes", int_ppc_qpx_qvfrsqrtes>;
  let isCodeGenOnly = 1 in
  def QVFRSQRTESs : AForm_4a<0, 26, (outs qsrc:$FRT), (ins qsrc:$FRB),
                             "qvfrsqrtes $FRT, $FRB", IIC_FPGeneral,
                             [(set v4f32:$FRT, (PPCfrsqrte v4f32:$FRB))]>;

  // Multiply Instructions
  let isCommutable = 1 in {
    def QVFMUL : AForm_3<4, 25,
                        (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRC),
                        "qvfmul $FRT, $FRA, $FRC", IIC_FPGeneral,
                        [(set v4f64:$FRT, (fmul v4f64:$FRA, v4f64:$FRC))]>;
    let isCodeGenOnly = 1 in
      def QVFMULS : QPXA3_Int<0, 25, "qvfmuls", int_ppc_qpx_qvfmuls>;
    def QVFMULSs : AForm_3<0, 25,
                          (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRC),
                          "qvfmuls $FRT, $FRA, $FRC", IIC_FPGeneral,
                          [(set v4f32:$FRT, (fmul v4f32:$FRA, v4f32:$FRC))]>;
  }
  def QVFXMUL : QPXA3_Int<4, 17, "qvfxmul", int_ppc_qpx_qvfxmul>;
  def QVFXMULS : QPXA3_Int<0, 17, "qvfxmuls", int_ppc_qpx_qvfxmuls>;

  // Multiply-add instructions
  def QVFMADD : AForm_1<4, 29,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                      "qvfmadd $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f64:$FRT, (fma v4f64:$FRA, v4f64:$FRC, v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFMADDS : QPXA1_Int<0, 29, "qvfmadds", int_ppc_qpx_qvfmadds>;
  def QVFMADDSs : AForm_1<0, 29,
                        (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, qsrc:$FRC),
                        "qvfmadds $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                        [(set v4f32:$FRT, (fma v4f32:$FRA, v4f32:$FRC, v4f32:$FRB))]>;
  def QVFNMADD : AForm_1<4, 31,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                      "qvfnmadd $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f64:$FRT, (fneg (fma v4f64:$FRA, v4f64:$FRC,
                                                   v4f64:$FRB)))]>;
  let isCodeGenOnly = 1 in
    def QVFNMADDS : QPXA1_Int<0, 31, "qvfnmadds", int_ppc_qpx_qvfnmadds>;
  def QVFNMADDSs : AForm_1<0, 31,
                        (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, qsrc:$FRC),
                        "qvfnmadds $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                        [(set v4f32:$FRT, (fneg (fma v4f32:$FRA, v4f32:$FRC,
                                                     v4f32:$FRB)))]>;
  def QVFMSUB : AForm_1<4, 28,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                      "qvfmsub $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f64:$FRT, (fma v4f64:$FRA, v4f64:$FRC,
                                             (fneg v4f64:$FRB)))]>;
  let isCodeGenOnly = 1 in
    def QVFMSUBS : QPXA1_Int<0, 28, "qvfmsubs", int_ppc_qpx_qvfmsubs>;
  def QVFMSUBSs : AForm_1<0, 28,
                      (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, qsrc:$FRC),
                      "qvfmsubs $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f32:$FRT, (fma v4f32:$FRA, v4f32:$FRC,
                                             (fneg v4f32:$FRB)))]>;
  def QVFNMSUB : AForm_1<4, 30,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                      "qvfnmsub $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f64:$FRT, (fneg (fma v4f64:$FRA, v4f64:$FRC,
                                              (fneg v4f64:$FRB))))]>;
  let isCodeGenOnly = 1 in
    def QVFNMSUBS : QPXA1_Int<0, 30, "qvfnmsubs", int_ppc_qpx_qvfnmsubs>;
  def QVFNMSUBSs : AForm_1<0, 30,
                      (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, qsrc:$FRC),
                      "qvfnmsubs $FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set v4f32:$FRT, (fneg (fma v4f32:$FRA, v4f32:$FRC,
                                              (fneg v4f32:$FRB))))]>;
  def QVFXMADD : QPXA1_Int<4, 9, "qvfxmadd", int_ppc_qpx_qvfxmadd>;
  def QVFXMADDS : QPXA1_Int<0, 9, "qvfxmadds", int_ppc_qpx_qvfxmadds>;
  def QVFXXNPMADD : QPXA1_Int<4, 11, "qvfxxnpmadd", int_ppc_qpx_qvfxxnpmadd>;
  def QVFXXNPMADDS : QPXA1_Int<0, 11, "qvfxxnpmadds", int_ppc_qpx_qvfxxnpmadds>;
  def QVFXXCPNMADD : QPXA1_Int<4, 3, "qvfxxcpnmadd", int_ppc_qpx_qvfxxcpnmadd>;
  def QVFXXCPNMADDS : QPXA1_Int<0, 3, "qvfxxcpnmadds", int_ppc_qpx_qvfxxcpnmadds>;
  def QVFXXMADD : QPXA1_Int<4, 1, "qvfxxmadd", int_ppc_qpx_qvfxxmadd>;
  def QVFXXMADDS : QPXA1_Int<0, 1, "qvfxxmadds", int_ppc_qpx_qvfxxmadds>;

  // Select Instruction
  let isCodeGenOnly = 1 in
    def QVFSEL : QPXA1s_Int<4, 23, "qvfsel", int_ppc_qpx_qvfsel>;
  def QVFSELb : AForm_1<4, 23, (outs qfrc:$FRT),
                        (ins qbrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                        "qvfsel $FRT, $FRA, $FRC, $FRB", IIC_VecPerm,
                        [(set v4f64:$FRT, (vselect v4i1:$FRA,
                                                   v4f64:$FRC, v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
  def QVFSELbs : AForm_1<4, 23, (outs qsrc:$FRT),
                        (ins qbrc:$FRA, qsrc:$FRB, qsrc:$FRC),
                        "qvfsel $FRT, $FRA, $FRC, $FRB", IIC_VecPerm,
                        [(set v4f32:$FRT, (vselect v4i1:$FRA,
                                                   v4f32:$FRC, v4f32:$FRB))]>;
  let isCodeGenOnly = 1 in
  def QVFSELbb: AForm_1<4, 23, (outs qbrc:$FRT),
                        (ins qbrc:$FRA, qbrc:$FRB, qbrc:$FRC),
                        "qvfsel $FRT, $FRA, $FRC, $FRB", IIC_VecPerm,
                        [(set v4i1:$FRT, (vselect v4i1:$FRA,
                                                  v4i1:$FRC, v4i1:$FRB))]>;

  // SELECT_CC_* - Used to implement the SELECT_CC DAG operation.  Expanded after
  // instruction selection into a branch sequence.
  def SELECT_CC_QFRC: PPCCustomInserterPseudo<(outs qfrc:$dst), (ins crrc:$cond, qfrc:$T, qfrc:$F,
                              i32imm:$BROPC), "#SELECT_CC_QFRC",
                              []>;
  def SELECT_CC_QSRC: PPCCustomInserterPseudo<(outs qsrc:$dst), (ins crrc:$cond, qsrc:$T, qsrc:$F,
                              i32imm:$BROPC), "#SELECT_CC_QSRC",
                              []>;
  def SELECT_CC_QBRC: PPCCustomInserterPseudo<(outs qbrc:$dst), (ins crrc:$cond, qbrc:$T, qbrc:$F,
                              i32imm:$BROPC), "#SELECT_CC_QBRC",
                              []>;

  // SELECT_* pseudo instructions, like SELECT_CC_* but taking condition
  // register bit directly.
  def SELECT_QFRC: PPCCustomInserterPseudo<(outs qfrc:$dst), (ins crbitrc:$cond,
                          qfrc:$T, qfrc:$F), "#SELECT_QFRC",
                          [(set v4f64:$dst,
                                (select i1:$cond, v4f64:$T, v4f64:$F))]>;
  def SELECT_QSRC: PPCCustomInserterPseudo<(outs qsrc:$dst), (ins crbitrc:$cond,
                          qsrc:$T, qsrc:$F), "#SELECT_QSRC",
                          [(set v4f32:$dst,
                                (select i1:$cond, v4f32:$T, v4f32:$F))]>;
  def SELECT_QBRC: PPCCustomInserterPseudo<(outs qbrc:$dst), (ins crbitrc:$cond,
                          qbrc:$T, qbrc:$F), "#SELECT_QBRC",
                          [(set v4i1:$dst,
                                (select i1:$cond, v4i1:$T, v4i1:$F))]>;

  // Convert and Round Instructions
  def QVFCTID : QPXX19_Int<4, 814, "qvfctid", int_ppc_qpx_qvfctid>;
  let isCodeGenOnly = 1 in
    def QVFCTIDb : XForm_19<4, 814, (outs qbrc:$FRT), (ins qbrc:$FRB),
                            "qvfctid $FRT, $FRB", IIC_FPGeneral, []>;

  def QVFCTIDU : QPXX19_Int<4, 942, "qvfctidu", int_ppc_qpx_qvfctidu>;
  def QVFCTIDZ : QPXX19_Int<4, 815, "qvfctidz", int_ppc_qpx_qvfctidz>;
  def QVFCTIDUZ : QPXX19_Int<4, 943, "qvfctiduz", int_ppc_qpx_qvfctiduz>;
  def QVFCTIW : QPXX19_Int<4, 14, "qvfctiw", int_ppc_qpx_qvfctiw>;
  def QVFCTIWU : QPXX19_Int<4, 142, "qvfctiwu", int_ppc_qpx_qvfctiwu>;
  def QVFCTIWZ : QPXX19_Int<4, 15, "qvfctiwz", int_ppc_qpx_qvfctiwz>;
  def QVFCTIWUZ : QPXX19_Int<4, 143, "qvfctiwuz", int_ppc_qpx_qvfctiwuz>;
  def QVFCFID : QPXX19_Int<4, 846, "qvfcfid", int_ppc_qpx_qvfcfid>;
  let isCodeGenOnly = 1 in
    def QVFCFIDb : XForm_19<4, 846, (outs qbrc:$FRT), (ins qbrc:$FRB),
                            "qvfcfid $FRT, $FRB", IIC_FPGeneral, []>;

  def QVFCFIDU : QPXX19_Int<4, 974, "qvfcfidu", int_ppc_qpx_qvfcfidu>;
  def QVFCFIDS : QPXX19_Int<0, 846, "qvfcfids", int_ppc_qpx_qvfcfids>;
  def QVFCFIDUS : QPXX19_Int<0, 974, "qvfcfidus", int_ppc_qpx_qvfcfidus>;

  let isCodeGenOnly = 1 in
    def QVFRSP : QPXX19_Int<4, 12, "qvfrsp", int_ppc_qpx_qvfrsp>;
  def QVFRSPs : XForm_19<4, 12,
                      (outs qsrc:$FRT), (ins qfrc:$FRB),
                      "qvfrsp $FRT, $FRB", IIC_FPGeneral,
                      [(set v4f32:$FRT, (fround_inexact v4f64:$FRB))]>;

  def QVFRIZ : XForm_19<4, 424, (outs qfrc:$FRT), (ins qfrc:$FRB),
                        "qvfriz $FRT, $FRB", IIC_FPGeneral,
                        [(set v4f64:$FRT, (ftrunc v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFRIZs : XForm_19<4, 424, (outs qsrc:$FRT), (ins qsrc:$FRB),
                           "qvfriz $FRT, $FRB", IIC_FPGeneral,
                           [(set v4f32:$FRT, (ftrunc v4f32:$FRB))]>;

  def QVFRIN : XForm_19<4, 392, (outs qfrc:$FRT), (ins qfrc:$FRB),
                        "qvfrin $FRT, $FRB", IIC_FPGeneral,
                        [(set v4f64:$FRT, (fround v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFRINs : XForm_19<4, 392, (outs qsrc:$FRT), (ins qsrc:$FRB),
                           "qvfrin $FRT, $FRB", IIC_FPGeneral,
                           [(set v4f32:$FRT, (fround v4f32:$FRB))]>;

  def QVFRIP : XForm_19<4, 456, (outs qfrc:$FRT), (ins qfrc:$FRB),
                        "qvfrip $FRT, $FRB", IIC_FPGeneral,
                        [(set v4f64:$FRT, (fceil v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFRIPs : XForm_19<4, 456, (outs qsrc:$FRT), (ins qsrc:$FRB),
                           "qvfrip $FRT, $FRB", IIC_FPGeneral,
                           [(set v4f32:$FRT, (fceil v4f32:$FRB))]>;

  def QVFRIM : XForm_19<4, 488, (outs qfrc:$FRT), (ins qfrc:$FRB),
                        "qvfrim $FRT, $FRB", IIC_FPGeneral,
                        [(set v4f64:$FRT, (ffloor v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFRIMs : XForm_19<4, 488, (outs qsrc:$FRT), (ins qsrc:$FRB),
                           "qvfrim $FRT, $FRB", IIC_FPGeneral,
                           [(set v4f32:$FRT, (ffloor v4f32:$FRB))]>;

  // Move Instructions
  def QVFMR : XForm_19<4, 72,
                      (outs qfrc:$FRT), (ins qfrc:$FRB),
                      "qvfmr $FRT, $FRB", IIC_VecPerm,
                      [/* (set v4f64:$FRT, v4f64:$FRB) */]>;
  let isCodeGenOnly = 1 in {
    def QVFMRs : XForm_19<4, 72,
                         (outs qsrc:$FRT), (ins qsrc:$FRB),
                         "qvfmr $FRT, $FRB", IIC_VecPerm,
                         [/* (set v4f32:$FRT, v4f32:$FRB) */]>;
    def QVFMRb : XForm_19<4, 72,
                         (outs qbrc:$FRT), (ins qbrc:$FRB),
                         "qvfmr $FRT, $FRB", IIC_VecPerm,
                         [/* (set v4i1:$FRT, v4i1:$FRB) */]>;
  }
  def QVFNEG : XForm_19<4, 40,
                      (outs qfrc:$FRT), (ins qfrc:$FRB),
                      "qvfneg $FRT, $FRB", IIC_VecPerm,
                      [(set v4f64:$FRT, (fneg v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFNEGs : XForm_19<4, 40,
                         (outs qsrc:$FRT), (ins qsrc:$FRB),
                         "qvfneg $FRT, $FRB", IIC_VecPerm,
                         [(set v4f32:$FRT, (fneg v4f32:$FRB))]>;
  def QVFABS : XForm_19<4, 264,
                      (outs qfrc:$FRT), (ins qfrc:$FRB),
                      "qvfabs $FRT, $FRB", IIC_VecPerm,
                      [(set v4f64:$FRT, (fabs v4f64:$FRB))]>;
  let isCodeGenOnly = 1 in
    def QVFABSs : XForm_19<4, 264,
                         (outs qsrc:$FRT), (ins qsrc:$FRB),
                         "qvfabs $FRT, $FRB", IIC_VecPerm,
                         [(set v4f32:$FRT, (fabs v4f32:$FRB))]>;
  def QVFNABS : XForm_19<4, 136,
                      (outs qfrc:$FRT), (ins qfrc:$FRB),
                      "qvfnabs $FRT, $FRB", IIC_VecPerm,
                      [(set v4f64:$FRT, (fneg (fabs v4f64:$FRB)))]>;
  let isCodeGenOnly = 1 in
    def QVFNABSs : XForm_19<4, 136,
                         (outs qsrc:$FRT), (ins qsrc:$FRB),
                         "qvfnabs $FRT, $FRB", IIC_VecPerm,
                         [(set v4f32:$FRT, (fneg (fabs v4f32:$FRB)))]>;
  def QVFCPSGN : XForm_18<4, 8,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                      "qvfcpsgn $FRT, $FRA, $FRB", IIC_VecPerm,
                      [(set v4f64:$FRT, (fcopysign v4f64:$FRB, v4f64:$FRA))]>;
  let isCodeGenOnly = 1 in
    def QVFCPSGNs : XForm_18<4, 8,
                         (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                         "qvfcpsgn $FRT, $FRA, $FRB", IIC_VecPerm,
                         [(set v4f32:$FRT, (fcopysign v4f32:$FRB, v4f32:$FRA))]>;

  def QVALIGNI : Z23Form_1<4, 5,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, u2imm:$idx),
                      "qvaligni $FRT, $FRA, $FRB, $idx", IIC_VecPerm,
                      [(set v4f64:$FRT,
                            (PPCqvaligni v4f64:$FRA, v4f64:$FRB,
                                         (i32 imm:$idx)))]>;
  let isCodeGenOnly = 1 in
     def QVALIGNIs : Z23Form_1<4, 5,
                         (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, u2imm:$idx),
                         "qvaligni $FRT, $FRA, $FRB, $idx", IIC_VecPerm,
                         [(set v4f32:$FRT,
                               (PPCqvaligni v4f32:$FRA, v4f32:$FRB,
                                            (i32 imm:$idx)))]>;
  let isCodeGenOnly = 1 in
     def QVALIGNIb : Z23Form_1<4, 5,
                         (outs qbrc:$FRT), (ins qbrc:$FRA, qbrc:$FRB, u2imm:$idx),
                         "qvaligni $FRT, $FRA, $FRB, $idx", IIC_VecPerm,
                         [(set v4i1:$FRT,
                               (PPCqvaligni v4i1:$FRA, v4i1:$FRB,
                                            (i32 imm:$idx)))]>;

  def QVESPLATI : Z23Form_2<4, 37,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, u2imm:$idx),
                      "qvesplati $FRT, $FRA, $idx", IIC_VecPerm,
                      [(set v4f64:$FRT,
                            (PPCqvesplati v4f64:$FRA, (i32 imm:$idx)))]>;
  let isCodeGenOnly = 1 in
     def QVESPLATIs : Z23Form_2<4, 37,
                         (outs qsrc:$FRT), (ins qsrc:$FRA, u2imm:$idx),
                         "qvesplati $FRT, $FRA, $idx", IIC_VecPerm,
                         [(set v4f32:$FRT,
                               (PPCqvesplati v4f32:$FRA, (i32 imm:$idx)))]>;
  let isCodeGenOnly = 1 in
     def QVESPLATIb : Z23Form_2<4, 37,
                         (outs qbrc:$FRT), (ins qbrc:$FRA, u2imm:$idx),
                         "qvesplati $FRT, $FRA, $idx", IIC_VecPerm,
                         [(set v4i1:$FRT,
                               (PPCqvesplati v4i1:$FRA, (i32 imm:$idx)))]>;

  def QVFPERM : AForm_1<4, 6,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, qfrc:$FRC),
                      "qvfperm $FRT, $FRA, $FRB, $FRC", IIC_VecPerm,
                      [(set v4f64:$FRT,
                            (PPCqvfperm v4f64:$FRA, v4f64:$FRB, v4f64:$FRC))]>;
  let isCodeGenOnly = 1 in
     def QVFPERMs : AForm_1<4, 6,
                         (outs qsrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB, qfrc:$FRC),
                         "qvfperm $FRT, $FRA, $FRB, $FRC", IIC_VecPerm,
                         [(set v4f32:$FRT,
                               (PPCqvfperm v4f32:$FRA, v4f32:$FRB, v4f64:$FRC))]>;

  let isReMaterializable = 1, isAsCheapAsAMove = 1 in
  def QVGPCI : Z23Form_3<4, 133,
                      (outs qfrc:$FRT), (ins u12imm:$idx),
                      "qvgpci $FRT, $idx", IIC_VecPerm,
                      [(set v4f64:$FRT, (PPCqvgpci (u12:$idx)))]>;

  // Compare Instruction
  let isCodeGenOnly = 1 in
    def QVFTSTNAN : QPXX18_Int<4, 64, "qvftstnan", int_ppc_qpx_qvftstnan>;
  def QVFTSTNANb : XForm_18<4, 64, (outs qbrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                           "qvftstnan $FRT, $FRA, $FRB", IIC_FPCompare,
                           [(set v4i1:$FRT,
                                 (setcc v4f64:$FRA, v4f64:$FRB, SETUO))]>;
  let isCodeGenOnly = 1 in
  def QVFTSTNANbs : XForm_18<4, 64, (outs qbrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                            "qvftstnan $FRT, $FRA, $FRB", IIC_FPCompare,
                            [(set v4i1:$FRT,
                                  (setcc v4f32:$FRA, v4f32:$FRB, SETUO))]>;
  let isCodeGenOnly = 1 in
    def QVFCMPLT : QPXX18_Int<4, 96, "qvfcmplt", int_ppc_qpx_qvfcmplt>;
  def QVFCMPLTb : XForm_18<4, 96, (outs qbrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                           "qvfcmplt $FRT, $FRA, $FRB", IIC_FPCompare,
                           [(set v4i1:$FRT,
                                 (setcc v4f64:$FRA, v4f64:$FRB, SETOLT))]>;
  let isCodeGenOnly = 1 in
  def QVFCMPLTbs : XForm_18<4, 96, (outs qbrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                            "qvfcmplt $FRT, $FRA, $FRB", IIC_FPCompare,
                            [(set v4i1:$FRT,
                                  (setcc v4f32:$FRA, v4f32:$FRB, SETOLT))]>;
  let isCodeGenOnly = 1 in
    def QVFCMPGT : QPXX18_Int<4, 32, "qvfcmpgt", int_ppc_qpx_qvfcmpgt>;
  def QVFCMPGTb : XForm_18<4, 32, (outs qbrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                           "qvfcmpgt $FRT, $FRA, $FRB", IIC_FPCompare,
                           [(set v4i1:$FRT,
                                 (setcc v4f64:$FRA, v4f64:$FRB, SETOGT))]>;
  let isCodeGenOnly = 1 in
  def QVFCMPGTbs : XForm_18<4, 32, (outs qbrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                            "qvfcmpgt $FRT, $FRA, $FRB", IIC_FPCompare,
                            [(set v4i1:$FRT,
                                  (setcc v4f32:$FRA, v4f32:$FRB, SETOGT))]>;
  let isCodeGenOnly = 1 in
    def QVFCMPEQ : QPXX18_Int<4, 0, "qvfcmpeq", int_ppc_qpx_qvfcmpeq>;
  def QVFCMPEQb : XForm_18<4, 0, (outs qbrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB),
                           "qvfcmpeq $FRT, $FRA, $FRB", IIC_FPCompare,
                           [(set v4i1:$FRT,
                                 (setcc v4f64:$FRA, v4f64:$FRB, SETOEQ))]>;
  let isCodeGenOnly = 1 in
  def QVFCMPEQbs : XForm_18<4, 0, (outs qbrc:$FRT), (ins qsrc:$FRA, qsrc:$FRB),
                            "qvfcmpeq $FRT, $FRA, $FRB", IIC_FPCompare,
                            [(set v4i1:$FRT,
                                  (setcc v4f32:$FRA, v4f32:$FRB, SETOEQ))]>;

  let isCodeGenOnly = 1 in
  def QVFLOGICAL : XForm_20<4, 4,
                      (outs qfrc:$FRT), (ins qfrc:$FRA, qfrc:$FRB, u12imm:$tttt),
                      "qvflogical $FRT, $FRA, $FRB, $tttt", IIC_VecPerm, []>;
  def QVFLOGICALb : XForm_20<4, 4,
                      (outs qbrc:$FRT), (ins qbrc:$FRA, qbrc:$FRB, u12imm:$tttt),
                      "qvflogical $FRT, $FRA, $FRB, $tttt", IIC_VecPerm, []>;
  let isCodeGenOnly = 1 in
  def QVFLOGICALs : XForm_20<4, 4,
                      (outs qbrc:$FRT), (ins qbrc:$FRA, qbrc:$FRB, u12imm:$tttt),
                      "qvflogical $FRT, $FRA, $FRB, $tttt", IIC_VecPerm, []>;

  // Load indexed instructions
  let mayLoad = 1 in {
    def QVLFDX : XForm_1_memOp<31, 583,
                              (outs qfrc:$FRT), (ins memrr:$src),
                              "qvlfdx $FRT, $src", IIC_LdStLFD,
                              [(set v4f64:$FRT, (load xoaddr:$src))]>;
    let isCodeGenOnly = 1 in
    def QVLFDXb : XForm_1_memOp<31, 583,
                                (outs qbrc:$FRT), (ins memrr:$src),
                                "qvlfdx $FRT, $src", IIC_LdStLFD, []>;

    let RC = 1 in
    def QVLFDXA : XForm_1<31, 583,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfdxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFDUX : XForm_1<31, 615,
                        (outs qfrc:$FRT, ptr_rc_nor0:$ea_result),
                        (ins memrr:$src),
                        "qvlfdux $FRT, $src", IIC_LdStLFDU, []>,
                        RegConstraint<"$src.ptrreg = $ea_result">,
                        NoEncode<"$ea_result">;
    let RC = 1 in
    def QVLFDUXA : XForm_1<31, 615,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfduxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFSX : XForm_1_memOp<31, 519,
                              (outs qfrc:$FRT), (ins memrr:$src),
                              "qvlfsx $FRT, $src", IIC_LdStLFD,
                              [(set v4f64:$FRT, (extloadv4f32 xoaddr:$src))]>;

    let isCodeGenOnly = 1 in
    def QVLFSXb : XForm_1<31, 519,
                        (outs qbrc:$FRT), (ins memrr:$src),
                        "qvlfsx $FRT, $src", IIC_LdStLFD,
                        [(set v4i1:$FRT, (PPCqvlfsb xoaddr:$src))]>;
    let isCodeGenOnly = 1 in
    def QVLFSXs : XForm_1_memOp<31, 519,
                                (outs qsrc:$FRT), (ins memrr:$src),
                                "qvlfsx $FRT, $src", IIC_LdStLFD,
                                [(set v4f32:$FRT, (load xoaddr:$src))]>;

    let RC = 1 in
    def QVLFSXA : XForm_1<31, 519,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfsxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFSUX : XForm_1<31, 551,
                        (outs qsrc:$FRT, ptr_rc_nor0:$ea_result),
                        (ins memrr:$src),
                        "qvlfsux $FRT, $src", IIC_LdStLFDU, []>,
                        RegConstraint<"$src.ptrreg = $ea_result">,
                        NoEncode<"$ea_result">;

    let RC = 1 in
    def QVLFSUXA : XForm_1<31, 551,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfsuxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFCDX : XForm_1<31, 71,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcdx $FRT, $src", IIC_LdStLFD, []>;
    let RC = 1 in
    def QVLFCDXA : XForm_1<31, 71,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcdxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFCDUX : XForm_1<31, 103,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcdux $FRT, $src", IIC_LdStLFD, []>;
    let RC = 1 in
    def QVLFCDUXA : XForm_1<31, 103,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcduxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFCSX : XForm_1<31, 7,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcsx $FRT, $src", IIC_LdStLFD, []>;
    let isCodeGenOnly = 1 in
    def QVLFCSXs : XForm_1<31, 7,
                         (outs qsrc:$FRT), (ins memrr:$src),
                         "qvlfcsx $FRT, $src", IIC_LdStLFD, []>;

    let RC = 1 in
    def QVLFCSXA : XForm_1<31, 7,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcsxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFCSUX : XForm_1<31, 39,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcsux $FRT, $src", IIC_LdStLFD, []>;
    let RC = 1 in
    def QVLFCSUXA : XForm_1<31, 39,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfcsuxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFIWAX : XForm_1<31, 871,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfiwax $FRT, $src", IIC_LdStLFD, []>;
    let RC = 1 in
    def QVLFIWAXA : XForm_1<31, 871,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfiwaxa $FRT, $src", IIC_LdStLFD, []>;

    def QVLFIWZX : XForm_1<31, 839,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfiwzx $FRT, $src", IIC_LdStLFD, []>;
    let RC = 1 in
    def QVLFIWZXA : XForm_1<31, 839,
                        (outs qfrc:$FRT), (ins memrr:$src),
                        "qvlfiwzxa $FRT, $src", IIC_LdStLFD, []>;
  }


  def QVLPCLDX : XForm_1<31, 582,
                      (outs qfrc:$FRT), (ins memrr:$src),
                      "qvlpcldx $FRT, $src", IIC_LdStLFD, []>;
  def QVLPCLSX : XForm_1<31, 518,
                      (outs qfrc:$FRT), (ins memrr:$src),
                      "qvlpclsx $FRT, $src", IIC_LdStLFD, []>;
  let isCodeGenOnly = 1 in
    def QVLPCLSXint : XForm_11<31, 518,
                              (outs qfrc:$FRT), (ins G8RC:$src),
                              "qvlpclsx $FRT, 0, $src", IIC_LdStLFD, []>;
  def QVLPCRDX : XForm_1<31, 70,
                      (outs qfrc:$FRT), (ins memrr:$src),
                      "qvlpcrdx $FRT, $src", IIC_LdStLFD, []>;
  def QVLPCRSX : XForm_1<31, 6,
                      (outs qfrc:$FRT), (ins memrr:$src),
                      "qvlpcrsx $FRT, $src", IIC_LdStLFD, []>;

  // Store indexed instructions
  let mayStore = 1 in {
    def QVSTFDX : XForm_8_memOp<31, 711,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfdx $FRT, $dst", IIC_LdStSTFD,
                        [(store qfrc:$FRT, xoaddr:$dst)]>;
    let isCodeGenOnly = 1 in
    def QVSTFDXb : XForm_8_memOp<31, 711,
                        (outs), (ins qbrc:$FRT, memrr:$dst),
                        "qvstfdx $FRT, $dst", IIC_LdStSTFD, []>;

    let RC = 1 in
    def QVSTFDXA : XForm_8<31, 711,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfdxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFDUX : XForm_8<31, 743, (outs ptr_rc_nor0:$ea_res),
                           (ins qfrc:$FRT, memrr:$dst),
                           "qvstfdux $FRT, $dst", IIC_LdStSTFDU, []>,
                           RegConstraint<"$dst.ptrreg = $ea_res">,
                           NoEncode<"$ea_res">;

    let RC = 1 in
    def QVSTFDUXA : XForm_8<31, 743,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfduxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFDXI : XForm_8<31, 709,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfdxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFDXIA : XForm_8<31, 709,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfdxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFDUXI : XForm_8<31, 741,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfduxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFDUXIA : XForm_8<31, 741,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfduxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFSX : XForm_8_memOp<31, 647,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsx $FRT, $dst", IIC_LdStSTFD,
                        [(truncstorev4f32 qfrc:$FRT, xoaddr:$dst)]>;
    let isCodeGenOnly = 1 in
    def QVSTFSXs : XForm_8_memOp<31, 647,
                         (outs), (ins qsrc:$FRT, memrr:$dst),
                         "qvstfsx $FRT, $dst", IIC_LdStSTFD,
                         [(store qsrc:$FRT, xoaddr:$dst)]>;

    let RC = 1 in
    def QVSTFSXA : XForm_8<31, 647,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFSUX : XForm_8<31, 679, (outs ptr_rc_nor0:$ea_res),
                           (ins qsrc:$FRT, memrr:$dst),
                           "qvstfsux $FRT, $dst", IIC_LdStSTFDU, []>,
                           RegConstraint<"$dst.ptrreg = $ea_res">,
                           NoEncode<"$ea_res">;
    let isCodeGenOnly = 1 in
    def QVSTFSUXs: XForm_8<31, 679, (outs ptr_rc_nor0:$ea_res),
                           (ins qfrc:$FRT, memrr:$dst),
                           "qvstfsux $FRT, $dst", IIC_LdStSTFDU, []>,
                           RegConstraint<"$dst.ptrreg = $ea_res">,
                           NoEncode<"$ea_res">;

    let RC = 1 in
    def QVSTFSUXA : XForm_8<31, 679,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsuxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFSXI : XForm_8<31, 645,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFSXIA : XForm_8<31, 645,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFSUXI : XForm_8<31, 677,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsuxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFSUXIA : XForm_8<31, 677,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfsuxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCDX : XForm_8<31, 199,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcdx $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCDXA : XForm_8<31, 199,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcdxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCSX : XForm_8<31, 135,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsx $FRT, $dst", IIC_LdStSTFD, []>;
    let isCodeGenOnly = 1 in
    def QVSTFCSXs : XForm_8<31, 135,
                         (outs), (ins qsrc:$FRT, memrr:$dst),
                         "qvstfcsx $FRT, $dst", IIC_LdStSTFD, []>;

    let RC = 1 in
    def QVSTFCSXA : XForm_8<31, 135,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCDUX : XForm_8<31, 231,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcdux $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCDUXA : XForm_8<31, 231,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcduxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCSUX : XForm_8<31, 167,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsux $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCSUXA : XForm_8<31, 167,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsuxa $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCDXI : XForm_8<31, 197,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcdxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCDXIA : XForm_8<31, 197,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcdxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCSXI : XForm_8<31, 133,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCSXIA : XForm_8<31, 133,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCDUXI : XForm_8<31, 229,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcduxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCDUXIA : XForm_8<31, 229,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcduxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFCSUXI : XForm_8<31, 165,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsuxi $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFCSUXIA : XForm_8<31, 165,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfcsuxia $FRT, $dst", IIC_LdStSTFD, []>;

    def QVSTFIWX : XForm_8<31, 967,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfiwx $FRT, $dst", IIC_LdStSTFD, []>;
    let RC = 1 in
    def QVSTFIWXA : XForm_8<31, 967,
                        (outs), (ins qfrc:$FRT, memrr:$dst),
                        "qvstfiwxa $FRT, $dst", IIC_LdStSTFD, []>;
  }
}

} // neverHasSideEffects
}

def : InstAlias<"qvfclr $FRT",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRT, qbrc:$FRT, 0)>;
def : InstAlias<"qvfand $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 1)>;
def : InstAlias<"qvfandc $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 4)>;
def : InstAlias<"qvfctfb $FRT, $FRA",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRA, 5)>;
def : InstAlias<"qvfxor $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 6)>;
def : InstAlias<"qvfor $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 7)>;
def : InstAlias<"qvfnor $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 8)>;
def : InstAlias<"qvfequ $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 9)>;
def : InstAlias<"qvfnot $FRT, $FRA",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRA, 10)>;
def : InstAlias<"qvforc $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 13)>;
def : InstAlias<"qvfnand $FRT, $FRA, $FRB",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRA, qbrc:$FRB, 14)>;
def : InstAlias<"qvfset $FRT",
                (QVFLOGICALb qbrc:$FRT, qbrc:$FRT, qbrc:$FRT, 15)>;

//===----------------------------------------------------------------------===//
// Additional QPX Patterns
//

def : Pat<(v4f64 (scalar_to_vector f64:$A)),
          (INSERT_SUBREG (v4f64 (IMPLICIT_DEF)), $A, sub_64)>;
def : Pat<(v4f32 (scalar_to_vector f32:$A)),
          (INSERT_SUBREG (v4f32 (IMPLICIT_DEF)), $A, sub_64)>;

def : Pat<(f64 (extractelt v4f64:$S, 0)),
          (EXTRACT_SUBREG $S, sub_64)>;
def : Pat<(f32 (extractelt v4f32:$S, 0)),
          (EXTRACT_SUBREG $S, sub_64)>;

def : Pat<(f64 (extractelt v4f64:$S, 1)),
          (EXTRACT_SUBREG (QVESPLATI $S, 1), sub_64)>;
def : Pat<(f64 (extractelt v4f64:$S, 2)),
          (EXTRACT_SUBREG (QVESPLATI $S, 2), sub_64)>;
def : Pat<(f64 (extractelt v4f64:$S, 3)),
          (EXTRACT_SUBREG (QVESPLATI $S, 3), sub_64)>;

def : Pat<(f32 (extractelt v4f32:$S, 1)),
          (EXTRACT_SUBREG (QVESPLATIs $S, 1), sub_64)>;
def : Pat<(f32 (extractelt v4f32:$S, 2)),
          (EXTRACT_SUBREG (QVESPLATIs $S, 2), sub_64)>;
def : Pat<(f32 (extractelt v4f32:$S, 3)),
          (EXTRACT_SUBREG (QVESPLATIs $S, 3), sub_64)>;

def : Pat<(f64 (extractelt v4f64:$S, i64:$F)),
          (EXTRACT_SUBREG (QVFPERM $S, $S,
                                   (QVLPCLSXint (RLDICR $F, 2,
                                                        /* 63-2 = */ 61))),
                          sub_64)>;
def : Pat<(f32 (extractelt v4f32:$S, i64:$F)),
          (EXTRACT_SUBREG (QVFPERMs $S, $S,
                                    (QVLPCLSXint (RLDICR $F, 2,
                                                         /* 63-2 = */ 61))),
                          sub_64)>;

def : Pat<(int_ppc_qpx_qvfperm v4f64:$A, v4f64:$B, v4f64:$C),
          (QVFPERM $A, $B, $C)>;

def : Pat<(int_ppc_qpx_qvfcpsgn v4f64:$A, v4f64:$B),
          (QVFCPSGN $A, $B)>;

// FCOPYSIGN's operand types need not agree.
def : Pat<(fcopysign v4f64:$frB, v4f32:$frA),
          (QVFCPSGN (COPY_TO_REGCLASS $frA, QFRC), $frB)>;
def : Pat<(fcopysign QSRC:$frB, QFRC:$frA),
          (QVFCPSGNs (COPY_TO_REGCLASS $frA, QSRC), $frB)>;

def : Pat<(int_ppc_qpx_qvfneg v4f64:$A), (QVFNEG $A)>;
def : Pat<(int_ppc_qpx_qvfabs v4f64:$A), (QVFABS $A)>;
def : Pat<(int_ppc_qpx_qvfnabs v4f64:$A), (QVFNABS $A)>;

def : Pat<(int_ppc_qpx_qvfriz v4f64:$A), (QVFRIZ $A)>;
def : Pat<(int_ppc_qpx_qvfrin v4f64:$A), (QVFRIN $A)>;
def : Pat<(int_ppc_qpx_qvfrip v4f64:$A), (QVFRIP $A)>;
def : Pat<(int_ppc_qpx_qvfrim v4f64:$A), (QVFRIM $A)>;

def : Pat<(int_ppc_qpx_qvfre v4f64:$A), (QVFRE $A)>;
def : Pat<(int_ppc_qpx_qvfrsqrte v4f64:$A), (QVFRSQRTE $A)>;

def : Pat<(int_ppc_qpx_qvfadd v4f64:$A, v4f64:$B),
          (QVFADD $A, $B)>;
def : Pat<(int_ppc_qpx_qvfsub v4f64:$A, v4f64:$B),
          (QVFSUB $A, $B)>;
def : Pat<(int_ppc_qpx_qvfmul v4f64:$A, v4f64:$B),
          (QVFMUL $A, $B)>;

// Additional QVFNMSUB patterns: -a*c + b == -(a*c - b)
def : Pat<(fma (fneg v4f64:$A), v4f64:$C, v4f64:$B),
          (QVFNMSUB $A, $B, $C)>;
def : Pat<(fma v4f64:$A, (fneg v4f64:$C), v4f64:$B),
          (QVFNMSUB $A, $B, $C)>;
def : Pat<(fma (fneg v4f32:$A), v4f32:$C, v4f32:$B),
          (QVFNMSUBSs $A, $B, $C)>;
def : Pat<(fma v4f32:$A, (fneg v4f32:$C), v4f32:$B),
          (QVFNMSUBSs $A, $B, $C)>;

def : Pat<(int_ppc_qpx_qvfmadd v4f64:$A, v4f64:$B, v4f64:$C),
          (QVFMADD $A, $B, $C)>;
def : Pat<(int_ppc_qpx_qvfnmadd v4f64:$A, v4f64:$B, v4f64:$C),
          (QVFNMADD $A, $B, $C)>;
def : Pat<(int_ppc_qpx_qvfmsub v4f64:$A, v4f64:$B, v4f64:$C),
          (QVFMSUB $A, $B, $C)>;
def : Pat<(int_ppc_qpx_qvfnmsub v4f64:$A, v4f64:$B, v4f64:$C),
          (QVFNMSUB $A, $B, $C)>;

def : Pat<(int_ppc_qpx_qvlfd xoaddr:$src),
          (QVLFDX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfda xoaddr:$src),
          (QVLFDXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfs xoaddr:$src),
          (QVLFSX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfsa xoaddr:$src),
          (QVLFSXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfcda xoaddr:$src),
          (QVLFCDXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfcd xoaddr:$src),
          (QVLFCDX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfcsa xoaddr:$src),
          (QVLFCSXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfcs xoaddr:$src),
          (QVLFCSX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfda xoaddr:$src),
          (QVLFDXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfiwaa xoaddr:$src),
          (QVLFIWAXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfiwa xoaddr:$src),
          (QVLFIWAX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfiwza xoaddr:$src),
          (QVLFIWZXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfiwz xoaddr:$src),
          (QVLFIWZX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlfsa xoaddr:$src),
          (QVLFSXA xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlpcld xoaddr:$src),
          (QVLPCLDX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlpcls xoaddr:$src),
          (QVLPCLSX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlpcrd xoaddr:$src),
          (QVLPCRDX xoaddr:$src)>;
def : Pat<(int_ppc_qpx_qvlpcrs xoaddr:$src),
          (QVLPCRSX xoaddr:$src)>;

def : Pat<(int_ppc_qpx_qvstfd v4f64:$T, xoaddr:$dst),
          (QVSTFDX $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfs v4f64:$T, xoaddr:$dst),
          (QVSTFSX $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfcda v4f64:$T, xoaddr:$dst),
          (QVSTFCDXA $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfcd v4f64:$T, xoaddr:$dst),
          (QVSTFCDX $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfcsa v4f64:$T, xoaddr:$dst),
          (QVSTFCSXA $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfcs v4f64:$T, xoaddr:$dst),
          (QVSTFCSX $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfda v4f64:$T, xoaddr:$dst),
          (QVSTFDXA $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfiwa v4f64:$T, xoaddr:$dst),
          (QVSTFIWXA $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfiw v4f64:$T, xoaddr:$dst),
          (QVSTFIWX $T, xoaddr:$dst)>;
def : Pat<(int_ppc_qpx_qvstfsa v4f64:$T, xoaddr:$dst),
          (QVSTFSXA $T, xoaddr:$dst)>;

def : Pat<(pre_store v4f64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (QVSTFDUX $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_store v4f32:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (QVSTFSUX $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncstv4f32 v4f64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (QVSTFSUXs $rS, $ptrreg, $ptroff)>;

def : Pat<(int_ppc_qpx_qvflogical  v4f64:$A, v4f64:$B, (i32 imm:$idx)),
          (QVFLOGICAL $A, $B, imm:$idx)>;
def : Pat<(int_ppc_qpx_qvgpci (u12:$idx)),
          (QVGPCI imm:$idx)>;

def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETOGE),
          (QVFLOGICALb (QVFCMPLTb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETOLE),
          (QVFLOGICALb (QVFCMPGTb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETONE),
          (QVFLOGICALb (QVFCMPEQb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETO),
          (QVFLOGICALb (QVFTSTNANb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETUEQ),
          (QVFLOGICALb (QVFCMPEQb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETUGT),
          (QVFLOGICALb (QVFCMPGTb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETUGE),
          (QVFLOGICALb (QVFTSTNANb $FRA, $FRB),
                       (QVFCMPLTb $FRA, $FRB), (i32 13))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETULT),
          (QVFLOGICALb (QVFCMPLTb $FRA, $FRB),
                       (QVFTSTNANb $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETULE),
          (QVFLOGICALb (QVFTSTNANb $FRA, $FRB),
                       (QVFCMPGTb $FRA, $FRB), (i32 13))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETUNE),
          (QVFLOGICALb (QVFTSTNANb $FRA, $FRB),
                       (QVFCMPEQb $FRA, $FRB), (i32 13))>;

def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETEQ),
          (QVFCMPEQb $FRA, $FRB)>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETGT),
          (QVFCMPGTb $FRA, $FRB)>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETGE),
          (QVFLOGICALb (QVFCMPLTb $FRA, $FRB),
                       (QVFCMPLTb $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETLT),
          (QVFCMPLTb $FRA, $FRB)>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETLE),
          (QVFLOGICALb (QVFCMPGTb $FRA, $FRB),
                       (QVFCMPGTb $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f64:$FRA, v4f64:$FRB, SETNE),
          (QVFLOGICALb (QVFCMPEQb $FRA, $FRB),
                       (QVFCMPEQb $FRA, $FRB), (i32 10))>;

def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETOGE),
          (QVFLOGICALb (QVFCMPLTbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETOLE),
          (QVFLOGICALb (QVFCMPGTbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETONE),
          (QVFLOGICALb (QVFCMPEQbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 8))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETO),
          (QVFLOGICALb (QVFTSTNANbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETUEQ),
          (QVFLOGICALb (QVFCMPEQbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETUGT),
          (QVFLOGICALb (QVFCMPGTbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETUGE),
          (QVFLOGICALb (QVFTSTNANbs $FRA, $FRB),
                       (QVFCMPLTbs $FRA, $FRB), (i32 13))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETULT),
          (QVFLOGICALb (QVFCMPLTbs $FRA, $FRB),
                       (QVFTSTNANbs $FRA, $FRB), (i32 7))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETULE),
          (QVFLOGICALb (QVFTSTNANbs $FRA, $FRB),
                       (QVFCMPGTbs $FRA, $FRB), (i32 13))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETUNE),
          (QVFLOGICALb (QVFTSTNANbs $FRA, $FRB),
                       (QVFCMPEQbs $FRA, $FRB), (i32 13))>;

def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETEQ),
          (QVFCMPEQbs $FRA, $FRB)>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETGT),
          (QVFCMPGTbs $FRA, $FRB)>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETGE),
          (QVFLOGICALb (QVFCMPLTbs $FRA, $FRB),
                       (QVFCMPLTbs $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETLT),
          (QVFCMPLTbs $FRA, $FRB)>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETLE),
          (QVFLOGICALb (QVFCMPGTbs $FRA, $FRB),
                       (QVFCMPGTbs $FRA, $FRB), (i32 10))>;
def : Pat<(setcc v4f32:$FRA, v4f32:$FRB, SETNE),
          (QVFLOGICALb (QVFCMPEQbs $FRA, $FRB),
                       (QVFCMPEQbs $FRA, $FRB), (i32 10))>;

def : Pat<(and v4i1:$FRA, (not v4i1:$FRB)),
          (QVFLOGICALb $FRA, $FRB, (i32 4))>;
def : Pat<(not (or v4i1:$FRA, v4i1:$FRB)),
          (QVFLOGICALb $FRA, $FRB, (i32 8))>;
def : Pat<(not (xor v4i1:$FRA, v4i1:$FRB)),
          (QVFLOGICALb $FRA, $FRB, (i32 9))>;
def : Pat<(or v4i1:$FRA, (not v4i1:$FRB)),
          (QVFLOGICALb $FRA, $FRB, (i32 13))>;
def : Pat<(not (and v4i1:$FRA, v4i1:$FRB)),
          (QVFLOGICALb $FRA, $FRB, (i32 14))>;

def : Pat<(and v4i1:$FRA, v4i1:$FRB),
          (QVFLOGICALb $FRA, $FRB, (i32 1))>;
def : Pat<(or v4i1:$FRA, v4i1:$FRB),
          (QVFLOGICALb $FRA, $FRB, (i32 7))>;
def : Pat<(xor v4i1:$FRA, v4i1:$FRB),
          (QVFLOGICALb $FRA, $FRB, (i32 6))>;
def : Pat<(not v4i1:$FRA),
          (QVFLOGICALb $FRA, $FRA, (i32 10))>;

def : Pat<(v4f64 (fpextend v4f32:$src)),
          (COPY_TO_REGCLASS $src, QFRC)>;

def : Pat<(v4f32 (fround_exact v4f64:$src)),
          (COPY_TO_REGCLASS $src, QSRC)>;

// Extract the underlying floating-point values from the
// QPX (-1.0, 1.0) boolean representation.
def : Pat<(v4f64 (PPCqbflt v4i1:$src)),
          (COPY_TO_REGCLASS $src, QFRC)>;

def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETLT)),
          (SELECT_QFRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETULT)),
          (SELECT_QFRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETLE)),
          (SELECT_QFRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETULE)),
          (SELECT_QFRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETEQ)),
          (SELECT_QFRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETGE)),
          (SELECT_QFRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETUGE)),
          (SELECT_QFRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETGT)),
          (SELECT_QFRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETUGT)),
          (SELECT_QFRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f64 (selectcc i1:$lhs, i1:$rhs, v4f64:$tval, v4f64:$fval, SETNE)),
          (SELECT_QFRC (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETLT)),
          (SELECT_QSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETULT)),
          (SELECT_QSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETLE)),
          (SELECT_QSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETULE)),
          (SELECT_QSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETEQ)),
          (SELECT_QSRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETGE)),
          (SELECT_QSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETUGE)),
          (SELECT_QSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETGT)),
          (SELECT_QSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETUGT)),
          (SELECT_QSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4f32 (selectcc i1:$lhs, i1:$rhs, v4f32:$tval, v4f32:$fval, SETNE)),
          (SELECT_QSRC (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETLT)),
          (SELECT_QBRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETULT)),
          (SELECT_QBRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETLE)),
          (SELECT_QBRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETULE)),
          (SELECT_QBRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETEQ)),
          (SELECT_QBRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETGE)),
          (SELECT_QBRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETUGE)),
          (SELECT_QBRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETGT)),
          (SELECT_QBRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETUGT)),
          (SELECT_QBRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i1 (selectcc i1:$lhs, i1:$rhs, v4i1:$tval, v4i1:$fval, SETNE)),
          (SELECT_QBRC (CRXOR $lhs, $rhs), $tval, $fval)>;

} // end HasQPX

let Predicates = [HasQPX, NoNaNsFPMath] in {
def : Pat<(fminnum v4f64:$FRA, v4f64:$FRB),
          (QVFSELb (QVFCMPLTb $FRA, $FRB), $FRB, $FRA)>;
def : Pat<(fmaxnum v4f64:$FRA, v4f64:$FRB),
          (QVFSELb (QVFCMPGTb $FRA, $FRB), $FRB, $FRA)>;

def : Pat<(fminnum v4f32:$FRA, v4f32:$FRB),
          (QVFSELbs (QVFCMPLTbs $FRA, $FRB), $FRB, $FRA)>;
def : Pat<(fmaxnum v4f32:$FRA, v4f32:$FRB),
          (QVFSELbs (QVFCMPGTbs $FRA, $FRB), $FRB, $FRA)>;
}

let Predicates = [HasQPX, NaNsFPMath] in {
// When either of these operands is NaN, we should return the other operand.
// QVFCMPLT/QVFCMPGT return false is either operand is NaN, which means we need
// to explicitly or with a NaN test on the second operand.
def : Pat<(fminnum v4f64:$FRA, v4f64:$FRB),
          (QVFSELb (QVFLOGICALb (QVFCMPLTb $FRA, $FRB),
                                (QVFTSTNANb $FRB, $FRB), (i32 7)),
                   $FRB, $FRA)>;
def : Pat<(fmaxnum v4f64:$FRA, v4f64:$FRB),
          (QVFSELb (QVFLOGICALb (QVFCMPGTb $FRA, $FRB),
                                (QVFTSTNANb $FRB, $FRB), (i32 7)),
                   $FRB, $FRA)>;

def : Pat<(fminnum v4f32:$FRA, v4f32:$FRB),
          (QVFSELbs (QVFLOGICALb (QVFCMPLTbs $FRA, $FRB),
                                 (QVFTSTNANbs $FRB, $FRB), (i32 7)),
                   $FRB, $FRA)>;
def : Pat<(fmaxnum v4f32:$FRA, v4f32:$FRB),
          (QVFSELbs (QVFLOGICALb (QVFCMPGTbs $FRA, $FRB),
                                 (QVFTSTNANbs $FRB, $FRB), (i32 7)),
                   $FRB, $FRA)>;
}

