//===-- VOPCInstructions.td - Vector Instruction Defintions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Encodings
//===----------------------------------------------------------------------===//

class VOPCe <bits<8> op> : Enc32 {
  bits<9> src0;
  bits<8> src1;

  let Inst{8-0} = src0;
  let Inst{16-9} = src1;
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e;
}

class VOPC_SDWAe <bits<8> op, VOPProfile P> : VOP_SDWAe <P> {
  bits<8> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e; // encoding

  // VOPC disallows dst_sel and dst_unused as they have no effect on destination
  let Inst{42-40} = 0;
  let Inst{44-43} = 0;
}

class VOPC_SDWA9e <bits<8> op, VOPProfile P> : VOP_SDWA9Be <P> {
  bits<9> src1;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = !if(P.HasSrc1, src1{7-0}, 0);
  let Inst{24-17} = op;
  let Inst{31-25} = 0x3e; // encoding
  let Inst{63}    = !if(P.HasSrc1, src1{8}, 0); // src1_sgpr
}


//===----------------------------------------------------------------------===//
// VOPC classes
//===----------------------------------------------------------------------===//

// VOPC instructions are a special case because for the 32-bit
// encoding, we want to display the implicit vcc write as if it were
// an explicit $dst.
class VOPC_Profile<list<SchedReadWrite> sched, ValueType vt0, ValueType vt1 = vt0> :
  VOPProfile <[i1, vt0, vt1, untyped]> {
  let Asm32 = "vcc, $src0, $src1";
  // The destination for 32-bit encoding is implicit.
  let HasDst32 = 0;
  let Outs64 = (outs VOPDstS64:$sdst);
  list<SchedReadWrite> Schedule = sched;
}

class VOPC_Pseudo <string opName, VOPC_Profile P, list<dag> pattern=[]> :
  InstSI<(outs), P.Ins32, "", pattern>,
  VOP <opName>,
  SIMCInstr<opName#"_e32", SIEncodingFamily.NONE> {

  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let UseNamedOperandTable = 1;

  string Mnemonic = opName;
  string AsmOperands = P.Asm32;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;

  let VALU = 1;
  let VOPC = 1;
  let Uses = [EXEC];
  let Defs = [VCC];

  let SubtargetPredicate = isGCN;

  VOPProfile Pfl = P;
}

class VOPC_Real <VOPC_Pseudo ps, int EncodingFamily> :
  InstSI <ps.OutOperandList, ps.InOperandList, ps.PseudoInstr # " " # ps.AsmOperands, []>,
  SIMCInstr <ps.PseudoInstr, EncodingFamily> {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let Constraints     = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let Constraints        = ps.Constraints;
  let DisableEncoding    = ps.DisableEncoding;
  let TSFlags            = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let Uses                 = ps.Uses;
  let Defs                 = ps.Defs;
}

class VOPC_SDWA_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_SDWA_Pseudo <OpName, P, pattern> {
  let AsmMatchConverter = "cvtSdwaVOPC";
}

// This class is used only with VOPC instructions. Use $sdst for out operand
class VOPCInstAlias <VOP3_Pseudo ps, Instruction inst, VOPProfile p = ps.Pfl> :
  InstAlias <ps.OpName#" "#p.Asm32, (inst)>, PredicateControl {

  field bit isCompare;
  field bit isCommutable;

  let ResultInst =
    !if (p.HasDst32,
      !if (!eq(p.NumSrcArgs, 0),
        // 1 dst, 0 src
        (inst p.DstRC:$sdst),
      !if (!eq(p.NumSrcArgs, 1),
        // 1 dst, 1 src
        (inst p.DstRC:$sdst, p.Src0RC32:$src0),
      !if (!eq(p.NumSrcArgs, 2),
        // 1 dst, 2 src
        (inst p.DstRC:$sdst, p.Src0RC32:$src0, p.Src1RC32:$src1),
      // else - unreachable
        (inst)))),
    // else
      !if (!eq(p.NumSrcArgs, 2),
        // 0 dst, 2 src
        (inst p.Src0RC32:$src0, p.Src1RC32:$src1),
      !if (!eq(p.NumSrcArgs, 1),
        // 0 dst, 1 src
        (inst p.Src0RC32:$src1),
      // else
        // 0 dst, 0 src
        (inst))));

  let AsmVariantName = AMDGPUAsmVariants.Default;
  let SubtargetPredicate = AssemblerPredicate;
}

class getVOPCPat64 <PatLeaf cond, VOPProfile P> : LetDummies {
  list<dag> ret = !if(P.HasModifiers,
      [(set i1:$sdst,
        (setcc (P.Src0VT
                  !if(P.HasOMod,
                    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod),
                    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))),
               (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers)),
               cond))],
      [(set i1:$sdst, (setcc P.Src0VT:$src0, P.Src1VT:$src1, cond))]);
}


multiclass VOPC_Pseudos <string opName,
                         VOPC_Profile P,
                         PatLeaf cond = COND_NULL,
                         string revOp = opName,
                         bit DefExec = 0> {

  def _e32 : VOPC_Pseudo <opName, P>,
             Commutable_REV<revOp#"_e32", !eq(revOp, opName)> {
    let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
    let SchedRW = P.Schedule;
    let isConvergent = DefExec;
    let isCompare = 1;
    let isCommutable = 1;
  }

  def _e64 : VOP3_Pseudo<opName, P, getVOPCPat64<cond, P>.ret>,
    Commutable_REV<revOp#"_e64", !eq(revOp, opName)> {
    let Defs = !if(DefExec, [EXEC], []);
    let SchedRW = P.Schedule;
    let isCompare = 1;
    let isCommutable = 1;
  }

  def _sdwa : VOPC_SDWA_Pseudo <opName, P> {
    let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
    let SchedRW = P.Schedule;
    let isConvergent = DefExec;
    let isCompare = 1;
  }
}

def VOPC_I1_F16_F16 : VOPC_Profile<[Write32Bit], f16>;
def VOPC_I1_F32_F32 : VOPC_Profile<[Write32Bit], f32>;
def VOPC_I1_F64_F64 : VOPC_Profile<[WriteDoubleAdd], f64>;
def VOPC_I1_I16_I16 : VOPC_Profile<[Write32Bit], i16>;
def VOPC_I1_I32_I32 : VOPC_Profile<[Write32Bit], i32>;
def VOPC_I1_I64_I64 : VOPC_Profile<[Write64Bit], i64>;

multiclass VOPC_F16 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F16_F16, cond, revOp, 0>;

multiclass VOPC_F32 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F32_F32, cond, revOp, 0>;

multiclass VOPC_F64 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F64_F64, cond, revOp, 0>;

multiclass VOPC_I16 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I16_I16, cond, revOp, 0>;

multiclass VOPC_I32 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I32_I32, cond, revOp, 0>;

multiclass VOPC_I64 <string opName, PatLeaf cond = COND_NULL, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I64_I64, cond, revOp, 0>;

multiclass VOPCX_F16 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F16_F16, COND_NULL, revOp, 1>;

multiclass VOPCX_F32 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F32_F32, COND_NULL, revOp, 1>;

multiclass VOPCX_F64 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_F64_F64, COND_NULL, revOp, 1>;

multiclass VOPCX_I16 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I16_I16, COND_NULL, revOp, 1>;

multiclass VOPCX_I32 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I32_I32, COND_NULL, revOp, 1>;

multiclass VOPCX_I64 <string opName, string revOp = opName> :
  VOPC_Pseudos <opName, VOPC_I1_I64_I64, COND_NULL, revOp, 1>;


//===----------------------------------------------------------------------===//
// Compare instructions
//===----------------------------------------------------------------------===//

defm V_CMP_F_F32 : VOPC_F32 <"v_cmp_f_f32">;
defm V_CMP_LT_F32 : VOPC_F32 <"v_cmp_lt_f32", COND_OLT, "v_cmp_gt_f32">;
defm V_CMP_EQ_F32 : VOPC_F32 <"v_cmp_eq_f32", COND_OEQ>;
defm V_CMP_LE_F32 : VOPC_F32 <"v_cmp_le_f32", COND_OLE, "v_cmp_ge_f32">;
defm V_CMP_GT_F32 : VOPC_F32 <"v_cmp_gt_f32", COND_OGT>;
defm V_CMP_LG_F32 : VOPC_F32 <"v_cmp_lg_f32", COND_ONE>;
defm V_CMP_GE_F32 : VOPC_F32 <"v_cmp_ge_f32", COND_OGE>;
defm V_CMP_O_F32 : VOPC_F32 <"v_cmp_o_f32", COND_O>;
defm V_CMP_U_F32 : VOPC_F32 <"v_cmp_u_f32", COND_UO>;
defm V_CMP_NGE_F32 : VOPC_F32 <"v_cmp_nge_f32",  COND_ULT, "v_cmp_nle_f32">;
defm V_CMP_NLG_F32 : VOPC_F32 <"v_cmp_nlg_f32", COND_UEQ>;
defm V_CMP_NGT_F32 : VOPC_F32 <"v_cmp_ngt_f32", COND_ULE, "v_cmp_nlt_f32">;
defm V_CMP_NLE_F32 : VOPC_F32 <"v_cmp_nle_f32", COND_UGT>;
defm V_CMP_NEQ_F32 : VOPC_F32 <"v_cmp_neq_f32", COND_UNE>;
defm V_CMP_NLT_F32 : VOPC_F32 <"v_cmp_nlt_f32", COND_UGE>;
defm V_CMP_TRU_F32 : VOPC_F32 <"v_cmp_tru_f32">;

defm V_CMPX_F_F32 : VOPCX_F32 <"v_cmpx_f_f32">;
defm V_CMPX_LT_F32 : VOPCX_F32 <"v_cmpx_lt_f32", "v_cmpx_gt_f32">;
defm V_CMPX_EQ_F32 : VOPCX_F32 <"v_cmpx_eq_f32">;
defm V_CMPX_LE_F32 : VOPCX_F32 <"v_cmpx_le_f32", "v_cmpx_ge_f32">;
defm V_CMPX_GT_F32 : VOPCX_F32 <"v_cmpx_gt_f32">;
defm V_CMPX_LG_F32 : VOPCX_F32 <"v_cmpx_lg_f32">;
defm V_CMPX_GE_F32 : VOPCX_F32 <"v_cmpx_ge_f32">;
defm V_CMPX_O_F32 : VOPCX_F32 <"v_cmpx_o_f32">;
defm V_CMPX_U_F32 : VOPCX_F32 <"v_cmpx_u_f32">;
defm V_CMPX_NGE_F32 : VOPCX_F32 <"v_cmpx_nge_f32", "v_cmpx_nle_f32">;
defm V_CMPX_NLG_F32 : VOPCX_F32 <"v_cmpx_nlg_f32">;
defm V_CMPX_NGT_F32 : VOPCX_F32 <"v_cmpx_ngt_f32", "v_cmpx_nlt_f32">;
defm V_CMPX_NLE_F32 : VOPCX_F32 <"v_cmpx_nle_f32">;
defm V_CMPX_NEQ_F32 : VOPCX_F32 <"v_cmpx_neq_f32">;
defm V_CMPX_NLT_F32 : VOPCX_F32 <"v_cmpx_nlt_f32">;
defm V_CMPX_TRU_F32 : VOPCX_F32 <"v_cmpx_tru_f32">;

defm V_CMP_F_F64 : VOPC_F64 <"v_cmp_f_f64">;
defm V_CMP_LT_F64 : VOPC_F64 <"v_cmp_lt_f64", COND_OLT, "v_cmp_gt_f64">;
defm V_CMP_EQ_F64 : VOPC_F64 <"v_cmp_eq_f64", COND_OEQ>;
defm V_CMP_LE_F64 : VOPC_F64 <"v_cmp_le_f64", COND_OLE, "v_cmp_ge_f64">;
defm V_CMP_GT_F64 : VOPC_F64 <"v_cmp_gt_f64", COND_OGT>;
defm V_CMP_LG_F64 : VOPC_F64 <"v_cmp_lg_f64", COND_ONE>;
defm V_CMP_GE_F64 : VOPC_F64 <"v_cmp_ge_f64", COND_OGE>;
defm V_CMP_O_F64 : VOPC_F64 <"v_cmp_o_f64", COND_O>;
defm V_CMP_U_F64 : VOPC_F64 <"v_cmp_u_f64", COND_UO>;
defm V_CMP_NGE_F64 : VOPC_F64 <"v_cmp_nge_f64", COND_ULT, "v_cmp_nle_f64">;
defm V_CMP_NLG_F64 : VOPC_F64 <"v_cmp_nlg_f64", COND_UEQ>;
defm V_CMP_NGT_F64 : VOPC_F64 <"v_cmp_ngt_f64", COND_ULE, "v_cmp_nlt_f64">;
defm V_CMP_NLE_F64 : VOPC_F64 <"v_cmp_nle_f64", COND_UGT>;
defm V_CMP_NEQ_F64 : VOPC_F64 <"v_cmp_neq_f64", COND_UNE>;
defm V_CMP_NLT_F64 : VOPC_F64 <"v_cmp_nlt_f64", COND_UGE>;
defm V_CMP_TRU_F64 : VOPC_F64 <"v_cmp_tru_f64">;

defm V_CMPX_F_F64 : VOPCX_F64 <"v_cmpx_f_f64">;
defm V_CMPX_LT_F64 : VOPCX_F64 <"v_cmpx_lt_f64", "v_cmpx_gt_f64">;
defm V_CMPX_EQ_F64 : VOPCX_F64 <"v_cmpx_eq_f64">;
defm V_CMPX_LE_F64 : VOPCX_F64 <"v_cmpx_le_f64", "v_cmpx_ge_f64">;
defm V_CMPX_GT_F64 : VOPCX_F64 <"v_cmpx_gt_f64">;
defm V_CMPX_LG_F64 : VOPCX_F64 <"v_cmpx_lg_f64">;
defm V_CMPX_GE_F64 : VOPCX_F64 <"v_cmpx_ge_f64">;
defm V_CMPX_O_F64 : VOPCX_F64 <"v_cmpx_o_f64">;
defm V_CMPX_U_F64 : VOPCX_F64 <"v_cmpx_u_f64">;
defm V_CMPX_NGE_F64 : VOPCX_F64 <"v_cmpx_nge_f64", "v_cmpx_nle_f64">;
defm V_CMPX_NLG_F64 : VOPCX_F64 <"v_cmpx_nlg_f64">;
defm V_CMPX_NGT_F64 : VOPCX_F64 <"v_cmpx_ngt_f64", "v_cmpx_nlt_f64">;
defm V_CMPX_NLE_F64 : VOPCX_F64 <"v_cmpx_nle_f64">;
defm V_CMPX_NEQ_F64 : VOPCX_F64 <"v_cmpx_neq_f64">;
defm V_CMPX_NLT_F64 : VOPCX_F64 <"v_cmpx_nlt_f64">;
defm V_CMPX_TRU_F64 : VOPCX_F64 <"v_cmpx_tru_f64">;

let SubtargetPredicate = isSICI in {

defm V_CMPS_F_F32 : VOPC_F32 <"v_cmps_f_f32">;
defm V_CMPS_LT_F32 : VOPC_F32 <"v_cmps_lt_f32", COND_NULL, "v_cmps_gt_f32">;
defm V_CMPS_EQ_F32 : VOPC_F32 <"v_cmps_eq_f32">;
defm V_CMPS_LE_F32 : VOPC_F32 <"v_cmps_le_f32", COND_NULL, "v_cmps_ge_f32">;
defm V_CMPS_GT_F32 : VOPC_F32 <"v_cmps_gt_f32">;
defm V_CMPS_LG_F32 : VOPC_F32 <"v_cmps_lg_f32">;
defm V_CMPS_GE_F32 : VOPC_F32 <"v_cmps_ge_f32">;
defm V_CMPS_O_F32 : VOPC_F32 <"v_cmps_o_f32">;
defm V_CMPS_U_F32 : VOPC_F32 <"v_cmps_u_f32">;
defm V_CMPS_NGE_F32 : VOPC_F32 <"v_cmps_nge_f32", COND_NULL, "v_cmps_nle_f32">;
defm V_CMPS_NLG_F32 : VOPC_F32 <"v_cmps_nlg_f32">;
defm V_CMPS_NGT_F32 : VOPC_F32 <"v_cmps_ngt_f32", COND_NULL, "v_cmps_nlt_f32">;
defm V_CMPS_NLE_F32 : VOPC_F32 <"v_cmps_nle_f32">;
defm V_CMPS_NEQ_F32 : VOPC_F32 <"v_cmps_neq_f32">;
defm V_CMPS_NLT_F32 : VOPC_F32 <"v_cmps_nlt_f32">;
defm V_CMPS_TRU_F32 : VOPC_F32 <"v_cmps_tru_f32">;

defm V_CMPSX_F_F32 : VOPCX_F32 <"v_cmpsx_f_f32">;
defm V_CMPSX_LT_F32 : VOPCX_F32 <"v_cmpsx_lt_f32", "v_cmpsx_gt_f32">;
defm V_CMPSX_EQ_F32 : VOPCX_F32 <"v_cmpsx_eq_f32">;
defm V_CMPSX_LE_F32 : VOPCX_F32 <"v_cmpsx_le_f32", "v_cmpsx_ge_f32">;
defm V_CMPSX_GT_F32 : VOPCX_F32 <"v_cmpsx_gt_f32">;
defm V_CMPSX_LG_F32 : VOPCX_F32 <"v_cmpsx_lg_f32">;
defm V_CMPSX_GE_F32 : VOPCX_F32 <"v_cmpsx_ge_f32">;
defm V_CMPSX_O_F32 : VOPCX_F32 <"v_cmpsx_o_f32">;
defm V_CMPSX_U_F32 : VOPCX_F32 <"v_cmpsx_u_f32">;
defm V_CMPSX_NGE_F32 : VOPCX_F32 <"v_cmpsx_nge_f32", "v_cmpsx_nle_f32">;
defm V_CMPSX_NLG_F32 : VOPCX_F32 <"v_cmpsx_nlg_f32">;
defm V_CMPSX_NGT_F32 : VOPCX_F32 <"v_cmpsx_ngt_f32", "v_cmpsx_nlt_f32">;
defm V_CMPSX_NLE_F32 : VOPCX_F32 <"v_cmpsx_nle_f32">;
defm V_CMPSX_NEQ_F32 : VOPCX_F32 <"v_cmpsx_neq_f32">;
defm V_CMPSX_NLT_F32 : VOPCX_F32 <"v_cmpsx_nlt_f32">;
defm V_CMPSX_TRU_F32 : VOPCX_F32 <"v_cmpsx_tru_f32">;

defm V_CMPS_F_F64 : VOPC_F64 <"v_cmps_f_f64">;
defm V_CMPS_LT_F64 : VOPC_F64 <"v_cmps_lt_f64", COND_NULL, "v_cmps_gt_f64">;
defm V_CMPS_EQ_F64 : VOPC_F64 <"v_cmps_eq_f64">;
defm V_CMPS_LE_F64 : VOPC_F64 <"v_cmps_le_f64", COND_NULL, "v_cmps_ge_f64">;
defm V_CMPS_GT_F64 : VOPC_F64 <"v_cmps_gt_f64">;
defm V_CMPS_LG_F64 : VOPC_F64 <"v_cmps_lg_f64">;
defm V_CMPS_GE_F64 : VOPC_F64 <"v_cmps_ge_f64">;
defm V_CMPS_O_F64 : VOPC_F64 <"v_cmps_o_f64">;
defm V_CMPS_U_F64 : VOPC_F64 <"v_cmps_u_f64">;
defm V_CMPS_NGE_F64 : VOPC_F64 <"v_cmps_nge_f64", COND_NULL, "v_cmps_nle_f64">;
defm V_CMPS_NLG_F64 : VOPC_F64 <"v_cmps_nlg_f64">;
defm V_CMPS_NGT_F64 : VOPC_F64 <"v_cmps_ngt_f64", COND_NULL, "v_cmps_nlt_f64">;
defm V_CMPS_NLE_F64 : VOPC_F64 <"v_cmps_nle_f64">;
defm V_CMPS_NEQ_F64 : VOPC_F64 <"v_cmps_neq_f64">;
defm V_CMPS_NLT_F64 : VOPC_F64 <"v_cmps_nlt_f64">;
defm V_CMPS_TRU_F64 : VOPC_F64 <"v_cmps_tru_f64">;

defm V_CMPSX_F_F64 : VOPCX_F64 <"v_cmpsx_f_f64">;
defm V_CMPSX_LT_F64 : VOPCX_F64 <"v_cmpsx_lt_f64", "v_cmpsx_gt_f64">;
defm V_CMPSX_EQ_F64 : VOPCX_F64 <"v_cmpsx_eq_f64">;
defm V_CMPSX_LE_F64 : VOPCX_F64 <"v_cmpsx_le_f64", "v_cmpsx_ge_f64">;
defm V_CMPSX_GT_F64 : VOPCX_F64 <"v_cmpsx_gt_f64">;
defm V_CMPSX_LG_F64 : VOPCX_F64 <"v_cmpsx_lg_f64">;
defm V_CMPSX_GE_F64 : VOPCX_F64 <"v_cmpsx_ge_f64">;
defm V_CMPSX_O_F64 : VOPCX_F64 <"v_cmpsx_o_f64">;
defm V_CMPSX_U_F64 : VOPCX_F64 <"v_cmpsx_u_f64">;
defm V_CMPSX_NGE_F64 : VOPCX_F64 <"v_cmpsx_nge_f64", "v_cmpsx_nle_f64">;
defm V_CMPSX_NLG_F64 : VOPCX_F64 <"v_cmpsx_nlg_f64">;
defm V_CMPSX_NGT_F64 : VOPCX_F64 <"v_cmpsx_ngt_f64", "v_cmpsx_nlt_f64">;
defm V_CMPSX_NLE_F64 : VOPCX_F64 <"v_cmpsx_nle_f64">;
defm V_CMPSX_NEQ_F64 : VOPCX_F64 <"v_cmpsx_neq_f64">;
defm V_CMPSX_NLT_F64 : VOPCX_F64 <"v_cmpsx_nlt_f64">;
defm V_CMPSX_TRU_F64 : VOPCX_F64 <"v_cmpsx_tru_f64">;

} // End SubtargetPredicate = isSICI

let SubtargetPredicate = Has16BitInsts in {

defm V_CMP_F_F16    : VOPC_F16 <"v_cmp_f_f16">;
defm V_CMP_LT_F16   : VOPC_F16 <"v_cmp_lt_f16", COND_OLT, "v_cmp_gt_f16">;
defm V_CMP_EQ_F16   : VOPC_F16 <"v_cmp_eq_f16", COND_OEQ>;
defm V_CMP_LE_F16   : VOPC_F16 <"v_cmp_le_f16", COND_OLE, "v_cmp_ge_f16">;
defm V_CMP_GT_F16   : VOPC_F16 <"v_cmp_gt_f16", COND_OGT>;
defm V_CMP_LG_F16   : VOPC_F16 <"v_cmp_lg_f16", COND_ONE>;
defm V_CMP_GE_F16   : VOPC_F16 <"v_cmp_ge_f16", COND_OGE>;
defm V_CMP_O_F16    : VOPC_F16 <"v_cmp_o_f16", COND_O>;
defm V_CMP_U_F16    : VOPC_F16 <"v_cmp_u_f16", COND_UO>;
defm V_CMP_NGE_F16  : VOPC_F16 <"v_cmp_nge_f16", COND_ULT, "v_cmp_nle_f16">;
defm V_CMP_NLG_F16  : VOPC_F16 <"v_cmp_nlg_f16", COND_UEQ>;
defm V_CMP_NGT_F16  : VOPC_F16 <"v_cmp_ngt_f16", COND_ULE, "v_cmp_nlt_f16">;
defm V_CMP_NLE_F16  : VOPC_F16 <"v_cmp_nle_f16", COND_UGT>;
defm V_CMP_NEQ_F16  : VOPC_F16 <"v_cmp_neq_f16", COND_UNE>;
defm V_CMP_NLT_F16  : VOPC_F16 <"v_cmp_nlt_f16", COND_UGE>;
defm V_CMP_TRU_F16  : VOPC_F16 <"v_cmp_tru_f16">;

defm V_CMPX_F_F16   : VOPCX_F16 <"v_cmpx_f_f16">;
defm V_CMPX_LT_F16  : VOPCX_F16 <"v_cmpx_lt_f16", "v_cmpx_gt_f16">;
defm V_CMPX_EQ_F16  : VOPCX_F16 <"v_cmpx_eq_f16">;
defm V_CMPX_LE_F16  : VOPCX_F16 <"v_cmpx_le_f16", "v_cmpx_ge_f16">;
defm V_CMPX_GT_F16  : VOPCX_F16 <"v_cmpx_gt_f16">;
defm V_CMPX_LG_F16  : VOPCX_F16 <"v_cmpx_lg_f16">;
defm V_CMPX_GE_F16  : VOPCX_F16 <"v_cmpx_ge_f16">;
defm V_CMPX_O_F16   : VOPCX_F16 <"v_cmpx_o_f16">;
defm V_CMPX_U_F16   : VOPCX_F16 <"v_cmpx_u_f16">;
defm V_CMPX_NGE_F16 : VOPCX_F16 <"v_cmpx_nge_f16", "v_cmpx_nle_f16">;
defm V_CMPX_NLG_F16 : VOPCX_F16 <"v_cmpx_nlg_f16">;
defm V_CMPX_NGT_F16 : VOPCX_F16 <"v_cmpx_ngt_f16", "v_cmpx_nlt_f16">;
defm V_CMPX_NLE_F16 : VOPCX_F16 <"v_cmpx_nle_f16">;
defm V_CMPX_NEQ_F16 : VOPCX_F16 <"v_cmpx_neq_f16">;
defm V_CMPX_NLT_F16 : VOPCX_F16 <"v_cmpx_nlt_f16">;
defm V_CMPX_TRU_F16 : VOPCX_F16 <"v_cmpx_tru_f16">;

defm V_CMP_F_I16 : VOPC_I16 <"v_cmp_f_i16">;
defm V_CMP_LT_I16 : VOPC_I16 <"v_cmp_lt_i16", COND_SLT, "v_cmp_gt_i16">;
defm V_CMP_EQ_I16 : VOPC_I16 <"v_cmp_eq_i16">;
defm V_CMP_LE_I16 : VOPC_I16 <"v_cmp_le_i16", COND_SLE, "v_cmp_ge_i16">;
defm V_CMP_GT_I16 : VOPC_I16 <"v_cmp_gt_i16", COND_SGT>;
defm V_CMP_NE_I16 : VOPC_I16 <"v_cmp_ne_i16">;
defm V_CMP_GE_I16 : VOPC_I16 <"v_cmp_ge_i16", COND_SGE>;
defm V_CMP_T_I16 : VOPC_I16 <"v_cmp_t_i16">;

defm V_CMP_F_U16 : VOPC_I16 <"v_cmp_f_u16">;
defm V_CMP_LT_U16 : VOPC_I16 <"v_cmp_lt_u16", COND_ULT, "v_cmp_gt_u16">;
defm V_CMP_EQ_U16 : VOPC_I16 <"v_cmp_eq_u16", COND_EQ>;
defm V_CMP_LE_U16 : VOPC_I16 <"v_cmp_le_u16", COND_ULE, "v_cmp_ge_u16">;
defm V_CMP_GT_U16 : VOPC_I16 <"v_cmp_gt_u16", COND_UGT>;
defm V_CMP_NE_U16 : VOPC_I16 <"v_cmp_ne_u16", COND_NE>;
defm V_CMP_GE_U16 : VOPC_I16 <"v_cmp_ge_u16", COND_UGE>;
defm V_CMP_T_U16 : VOPC_I16 <"v_cmp_t_u16">;

defm V_CMPX_F_I16 : VOPCX_I16 <"v_cmpx_f_i16">;
defm V_CMPX_LT_I16 : VOPCX_I16 <"v_cmpx_lt_i16", "v_cmpx_gt_i16">;
defm V_CMPX_EQ_I16 : VOPCX_I16 <"v_cmpx_eq_i16">;
defm V_CMPX_LE_I16 : VOPCX_I16 <"v_cmpx_le_i16", "v_cmpx_ge_i16">;
defm V_CMPX_GT_I16 : VOPCX_I16 <"v_cmpx_gt_i16">;
defm V_CMPX_NE_I16 : VOPCX_I16 <"v_cmpx_ne_i16">;
defm V_CMPX_GE_I16 : VOPCX_I16 <"v_cmpx_ge_i16">;
defm V_CMPX_T_I16 : VOPCX_I16 <"v_cmpx_t_i16">;
defm V_CMPX_F_U16 : VOPCX_I16 <"v_cmpx_f_u16">;

defm V_CMPX_LT_U16 : VOPCX_I16 <"v_cmpx_lt_u16", "v_cmpx_gt_u16">;
defm V_CMPX_EQ_U16 : VOPCX_I16 <"v_cmpx_eq_u16">;
defm V_CMPX_LE_U16 : VOPCX_I16 <"v_cmpx_le_u16", "v_cmpx_ge_u16">;
defm V_CMPX_GT_U16 : VOPCX_I16 <"v_cmpx_gt_u16">;
defm V_CMPX_NE_U16 : VOPCX_I16 <"v_cmpx_ne_u16">;
defm V_CMPX_GE_U16 : VOPCX_I16 <"v_cmpx_ge_u16">;
defm V_CMPX_T_U16 : VOPCX_I16 <"v_cmpx_t_u16">;

} // End SubtargetPredicate = Has16BitInsts

defm V_CMP_F_I32 : VOPC_I32 <"v_cmp_f_i32">;
defm V_CMP_LT_I32 : VOPC_I32 <"v_cmp_lt_i32", COND_SLT, "v_cmp_gt_i32">;
defm V_CMP_EQ_I32 : VOPC_I32 <"v_cmp_eq_i32">;
defm V_CMP_LE_I32 : VOPC_I32 <"v_cmp_le_i32", COND_SLE, "v_cmp_ge_i32">;
defm V_CMP_GT_I32 : VOPC_I32 <"v_cmp_gt_i32", COND_SGT>;
defm V_CMP_NE_I32 : VOPC_I32 <"v_cmp_ne_i32">;
defm V_CMP_GE_I32 : VOPC_I32 <"v_cmp_ge_i32", COND_SGE>;
defm V_CMP_T_I32 : VOPC_I32 <"v_cmp_t_i32">;

defm V_CMPX_F_I32 : VOPCX_I32 <"v_cmpx_f_i32">;
defm V_CMPX_LT_I32 : VOPCX_I32 <"v_cmpx_lt_i32", "v_cmpx_gt_i32">;
defm V_CMPX_EQ_I32 : VOPCX_I32 <"v_cmpx_eq_i32">;
defm V_CMPX_LE_I32 : VOPCX_I32 <"v_cmpx_le_i32", "v_cmpx_ge_i32">;
defm V_CMPX_GT_I32 : VOPCX_I32 <"v_cmpx_gt_i32">;
defm V_CMPX_NE_I32 : VOPCX_I32 <"v_cmpx_ne_i32">;
defm V_CMPX_GE_I32 : VOPCX_I32 <"v_cmpx_ge_i32">;
defm V_CMPX_T_I32 : VOPCX_I32 <"v_cmpx_t_i32">;

defm V_CMP_F_I64 : VOPC_I64 <"v_cmp_f_i64">;
defm V_CMP_LT_I64 : VOPC_I64 <"v_cmp_lt_i64", COND_SLT, "v_cmp_gt_i64">;
defm V_CMP_EQ_I64 : VOPC_I64 <"v_cmp_eq_i64">;
defm V_CMP_LE_I64 : VOPC_I64 <"v_cmp_le_i64", COND_SLE, "v_cmp_ge_i64">;
defm V_CMP_GT_I64 : VOPC_I64 <"v_cmp_gt_i64", COND_SGT>;
defm V_CMP_NE_I64 : VOPC_I64 <"v_cmp_ne_i64">;
defm V_CMP_GE_I64 : VOPC_I64 <"v_cmp_ge_i64", COND_SGE>;
defm V_CMP_T_I64 : VOPC_I64 <"v_cmp_t_i64">;

defm V_CMPX_F_I64 : VOPCX_I64 <"v_cmpx_f_i64">;
defm V_CMPX_LT_I64 : VOPCX_I64 <"v_cmpx_lt_i64", "v_cmpx_gt_i64">;
defm V_CMPX_EQ_I64 : VOPCX_I64 <"v_cmpx_eq_i64">;
defm V_CMPX_LE_I64 : VOPCX_I64 <"v_cmpx_le_i64", "v_cmpx_ge_i64">;
defm V_CMPX_GT_I64 : VOPCX_I64 <"v_cmpx_gt_i64">;
defm V_CMPX_NE_I64 : VOPCX_I64 <"v_cmpx_ne_i64">;
defm V_CMPX_GE_I64 : VOPCX_I64 <"v_cmpx_ge_i64">;
defm V_CMPX_T_I64 : VOPCX_I64 <"v_cmpx_t_i64">;

defm V_CMP_F_U32 : VOPC_I32 <"v_cmp_f_u32">;
defm V_CMP_LT_U32 : VOPC_I32 <"v_cmp_lt_u32", COND_ULT, "v_cmp_gt_u32">;
defm V_CMP_EQ_U32 : VOPC_I32 <"v_cmp_eq_u32", COND_EQ>;
defm V_CMP_LE_U32 : VOPC_I32 <"v_cmp_le_u32", COND_ULE, "v_cmp_ge_u32">;
defm V_CMP_GT_U32 : VOPC_I32 <"v_cmp_gt_u32", COND_UGT>;
defm V_CMP_NE_U32 : VOPC_I32 <"v_cmp_ne_u32", COND_NE>;
defm V_CMP_GE_U32 : VOPC_I32 <"v_cmp_ge_u32", COND_UGE>;
defm V_CMP_T_U32 : VOPC_I32 <"v_cmp_t_u32">;

defm V_CMPX_F_U32 : VOPCX_I32 <"v_cmpx_f_u32">;
defm V_CMPX_LT_U32 : VOPCX_I32 <"v_cmpx_lt_u32", "v_cmpx_gt_u32">;
defm V_CMPX_EQ_U32 : VOPCX_I32 <"v_cmpx_eq_u32">;
defm V_CMPX_LE_U32 : VOPCX_I32 <"v_cmpx_le_u32", "v_cmpx_le_u32">;
defm V_CMPX_GT_U32 : VOPCX_I32 <"v_cmpx_gt_u32">;
defm V_CMPX_NE_U32 : VOPCX_I32 <"v_cmpx_ne_u32">;
defm V_CMPX_GE_U32 : VOPCX_I32 <"v_cmpx_ge_u32">;
defm V_CMPX_T_U32 : VOPCX_I32 <"v_cmpx_t_u32">;

defm V_CMP_F_U64 : VOPC_I64 <"v_cmp_f_u64">;
defm V_CMP_LT_U64 : VOPC_I64 <"v_cmp_lt_u64", COND_ULT, "v_cmp_gt_u64">;
defm V_CMP_EQ_U64 : VOPC_I64 <"v_cmp_eq_u64", COND_EQ>;
defm V_CMP_LE_U64 : VOPC_I64 <"v_cmp_le_u64", COND_ULE, "v_cmp_ge_u64">;
defm V_CMP_GT_U64 : VOPC_I64 <"v_cmp_gt_u64", COND_UGT>;
defm V_CMP_NE_U64 : VOPC_I64 <"v_cmp_ne_u64", COND_NE>;
defm V_CMP_GE_U64 : VOPC_I64 <"v_cmp_ge_u64", COND_UGE>;
defm V_CMP_T_U64 : VOPC_I64 <"v_cmp_t_u64">;

defm V_CMPX_F_U64 : VOPCX_I64 <"v_cmpx_f_u64">;
defm V_CMPX_LT_U64 : VOPCX_I64 <"v_cmpx_lt_u64", "v_cmpx_gt_u64">;
defm V_CMPX_EQ_U64 : VOPCX_I64 <"v_cmpx_eq_u64">;
defm V_CMPX_LE_U64 : VOPCX_I64 <"v_cmpx_le_u64", "v_cmpx_ge_u64">;
defm V_CMPX_GT_U64 : VOPCX_I64 <"v_cmpx_gt_u64">;
defm V_CMPX_NE_U64 : VOPCX_I64 <"v_cmpx_ne_u64">;
defm V_CMPX_GE_U64 : VOPCX_I64 <"v_cmpx_ge_u64">;
defm V_CMPX_T_U64 : VOPCX_I64 <"v_cmpx_t_u64">;

//===----------------------------------------------------------------------===//
// Class instructions
//===----------------------------------------------------------------------===//

class VOPC_Class_Profile<list<SchedReadWrite> sched, ValueType vt> :
  VOPC_Profile<sched, vt, i32> {
  let Ins64 = (ins Src0Mod:$src0_modifiers, Src0RC64:$src0, Src1RC64:$src1);
  let Asm64 = "$sdst, $src0_modifiers, $src1";

  let InsSDWA = (ins Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     Src1ModSDWA:$src1_modifiers, Src1SDWA:$src1,
                     clampmod:$clamp, src0_sel:$src0_sel, src1_sel:$src1_sel);

  let AsmSDWA = " vcc, $src0_modifiers, $src1_modifiers$clamp $src0_sel $src1_sel";
  let HasSrc1Mods = 0;
  let HasClamp = 0;
  let HasOMod = 0;
}

class getVOPCClassPat64 <VOPProfile P> {
  list<dag> ret =
    [(set i1:$sdst,
      (AMDGPUfp_class
        (P.Src0VT (VOP3Mods0Clamp0OMod P.Src0VT:$src0, i32:$src0_modifiers)),
        P.Src1VT:$src1))];
}

// Special case for class instructions which only have modifiers on
// the 1st source operand.
multiclass VOPC_Class_Pseudos <string opName, VOPC_Profile p, bit DefExec> {
  def _e32 : VOPC_Pseudo <opName, p> {
    let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
    let SchedRW = p.Schedule;
    let isConvergent = DefExec;
  }

  def _e64 : VOP3_Pseudo<opName, p, getVOPCClassPat64<p>.ret> {
    let Defs = !if(DefExec, [EXEC], []);
    let SchedRW = p.Schedule;
  }

  def _sdwa : VOPC_SDWA_Pseudo <opName, p> {
    let Defs = !if(DefExec, [VCC, EXEC], [VCC]);
    let SchedRW = p.Schedule;
    let isConvergent = DefExec;
  }
}

def VOPC_I1_F16_I32 : VOPC_Class_Profile<[Write32Bit], f16>;
def VOPC_I1_F32_I32 : VOPC_Class_Profile<[Write32Bit], f32>;
def VOPC_I1_F64_I32 : VOPC_Class_Profile<[WriteDoubleAdd], f64>;

multiclass VOPC_CLASS_F16 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F16_I32, 0>;

multiclass VOPCX_CLASS_F16 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F16_I32, 1>;

multiclass VOPC_CLASS_F32 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F32_I32, 0>;

multiclass VOPCX_CLASS_F32 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F32_I32, 1>;

multiclass VOPC_CLASS_F64 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F64_I32, 0>;

multiclass VOPCX_CLASS_F64 <string opName> :
  VOPC_Class_Pseudos <opName, VOPC_I1_F64_I32, 1>;

defm V_CMP_CLASS_F32 : VOPC_CLASS_F32 <"v_cmp_class_f32">;
defm V_CMPX_CLASS_F32 : VOPCX_CLASS_F32 <"v_cmpx_class_f32">;
defm V_CMP_CLASS_F64 : VOPC_CLASS_F64 <"v_cmp_class_f64">;
defm V_CMPX_CLASS_F64 : VOPCX_CLASS_F64 <"v_cmpx_class_f64">;
defm V_CMP_CLASS_F16  : VOPC_CLASS_F16 <"v_cmp_class_f16">;
defm V_CMPX_CLASS_F16 : VOPCX_CLASS_F16 <"v_cmpx_class_f16">;

//===----------------------------------------------------------------------===//
// V_ICMPIntrinsic Pattern.
//===----------------------------------------------------------------------===//

class ICMP_Pattern <PatLeaf cond, Instruction inst, ValueType vt> : GCNPat <
  (AMDGPUsetcc vt:$src0, vt:$src1, cond),
  (inst $src0, $src1)
>;

def : ICMP_Pattern <COND_EQ, V_CMP_EQ_U32_e64, i32>;
def : ICMP_Pattern <COND_NE, V_CMP_NE_U32_e64, i32>;
def : ICMP_Pattern <COND_UGT, V_CMP_GT_U32_e64, i32>;
def : ICMP_Pattern <COND_UGE, V_CMP_GE_U32_e64, i32>;
def : ICMP_Pattern <COND_ULT, V_CMP_LT_U32_e64, i32>;
def : ICMP_Pattern <COND_ULE, V_CMP_LE_U32_e64, i32>;
def : ICMP_Pattern <COND_SGT, V_CMP_GT_I32_e64, i32>;
def : ICMP_Pattern <COND_SGE, V_CMP_GE_I32_e64, i32>;
def : ICMP_Pattern <COND_SLT, V_CMP_LT_I32_e64, i32>;
def : ICMP_Pattern <COND_SLE, V_CMP_LE_I32_e64, i32>;

def : ICMP_Pattern <COND_EQ, V_CMP_EQ_U64_e64, i64>;
def : ICMP_Pattern <COND_NE, V_CMP_NE_U64_e64, i64>;
def : ICMP_Pattern <COND_UGT, V_CMP_GT_U64_e64, i64>;
def : ICMP_Pattern <COND_UGE, V_CMP_GE_U64_e64, i64>;
def : ICMP_Pattern <COND_ULT, V_CMP_LT_U64_e64, i64>;
def : ICMP_Pattern <COND_ULE, V_CMP_LE_U64_e64, i64>;
def : ICMP_Pattern <COND_SGT, V_CMP_GT_I64_e64, i64>;
def : ICMP_Pattern <COND_SGE, V_CMP_GE_I64_e64, i64>;
def : ICMP_Pattern <COND_SLT, V_CMP_LT_I64_e64, i64>;
def : ICMP_Pattern <COND_SLE, V_CMP_LE_I64_e64, i64>;

def : ICMP_Pattern <COND_EQ, V_CMP_EQ_U16_e64, i16>;
def : ICMP_Pattern <COND_NE, V_CMP_NE_U16_e64, i16>;
def : ICMP_Pattern <COND_UGT, V_CMP_GT_U16_e64, i16>;
def : ICMP_Pattern <COND_UGE, V_CMP_GE_U16_e64, i16>;
def : ICMP_Pattern <COND_ULT, V_CMP_LT_U16_e64, i16>;
def : ICMP_Pattern <COND_ULE, V_CMP_LE_U16_e64, i16>;
def : ICMP_Pattern <COND_SGT, V_CMP_GT_I16_e64, i16>;
def : ICMP_Pattern <COND_SGE, V_CMP_GE_I16_e64, i16>;
def : ICMP_Pattern <COND_SLT, V_CMP_LT_I16_e64, i16>;
def : ICMP_Pattern <COND_SLE, V_CMP_LE_I16_e64, i16>;

class FCMP_Pattern <PatLeaf cond, Instruction inst, ValueType vt> : GCNPat <
  (i64 (AMDGPUsetcc (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)),
                   (vt (VOP3Mods vt:$src1, i32:$src1_modifiers)), cond)),
  (inst $src0_modifiers, $src0, $src1_modifiers, $src1,
        DSTCLAMP.NONE)
>;

def : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F32_e64, f32>;
def : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F32_e64, f32>;
def : FCMP_Pattern <COND_OGT, V_CMP_GT_F32_e64, f32>;
def : FCMP_Pattern <COND_OGE, V_CMP_GE_F32_e64, f32>;
def : FCMP_Pattern <COND_OLT, V_CMP_LT_F32_e64, f32>;
def : FCMP_Pattern <COND_OLE, V_CMP_LE_F32_e64, f32>;

def : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F64_e64, f64>;
def : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F64_e64, f64>;
def : FCMP_Pattern <COND_OGT, V_CMP_GT_F64_e64, f64>;
def : FCMP_Pattern <COND_OGE, V_CMP_GE_F64_e64, f64>;
def : FCMP_Pattern <COND_OLT, V_CMP_LT_F64_e64, f64>;
def : FCMP_Pattern <COND_OLE, V_CMP_LE_F64_e64, f64>;

def : FCMP_Pattern <COND_OEQ, V_CMP_EQ_F16_e64, f16>;
def : FCMP_Pattern <COND_ONE, V_CMP_NEQ_F16_e64, f16>;
def : FCMP_Pattern <COND_OGT, V_CMP_GT_F16_e64, f16>;
def : FCMP_Pattern <COND_OGE, V_CMP_GE_F16_e64, f16>;
def : FCMP_Pattern <COND_OLT, V_CMP_LT_F16_e64, f16>;
def : FCMP_Pattern <COND_OLE, V_CMP_LE_F16_e64, f16>;


def : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F32_e64, f32>;
def : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F32_e64, f32>;
def : FCMP_Pattern <COND_UGT, V_CMP_NLE_F32_e64, f32>;
def : FCMP_Pattern <COND_UGE, V_CMP_NLT_F32_e64, f32>;
def : FCMP_Pattern <COND_ULT, V_CMP_NGE_F32_e64, f32>;
def : FCMP_Pattern <COND_ULE, V_CMP_NGT_F32_e64, f32>;

def : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F64_e64, f64>;
def : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F64_e64, f64>;
def : FCMP_Pattern <COND_UGT, V_CMP_NLE_F64_e64, f64>;
def : FCMP_Pattern <COND_UGE, V_CMP_NLT_F64_e64, f64>;
def : FCMP_Pattern <COND_ULT, V_CMP_NGE_F64_e64, f64>;
def : FCMP_Pattern <COND_ULE, V_CMP_NGT_F64_e64, f64>;

def : FCMP_Pattern <COND_UEQ, V_CMP_NLG_F16_e64, f16>;
def : FCMP_Pattern <COND_UNE, V_CMP_NEQ_F16_e64, f16>;
def : FCMP_Pattern <COND_UGT, V_CMP_NLE_F16_e64, f16>;
def : FCMP_Pattern <COND_UGE, V_CMP_NLT_F16_e64, f16>;
def : FCMP_Pattern <COND_ULT, V_CMP_NGE_F16_e64, f16>;
def : FCMP_Pattern <COND_ULE, V_CMP_NGT_F16_e64, f16>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SI
//===----------------------------------------------------------------------===//

multiclass VOPC_Real_si <bits<9> op> {
  let AssemblerPredicates = [isSICI], DecoderNamespace = "SICI" in {
    def _e32_si :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOPCe<op{7-0}>;

    def _e64_si :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3a_si <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3
      // Differs from VOP3e by destination name (sdst) as VOPC doesn't have vector dst
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }
  }
  def : VOPCInstAlias <!cast<VOP3_Pseudo>(NAME#"_e64"),
                       !cast<Instruction>(NAME#"_e32_si")> {
    let AssemblerPredicate = isSICI;
  }
}

defm V_CMP_F_F32     : VOPC_Real_si <0x0>;
defm V_CMP_LT_F32    : VOPC_Real_si <0x1>;
defm V_CMP_EQ_F32    : VOPC_Real_si <0x2>;
defm V_CMP_LE_F32    : VOPC_Real_si <0x3>;
defm V_CMP_GT_F32    : VOPC_Real_si <0x4>;
defm V_CMP_LG_F32    : VOPC_Real_si <0x5>;
defm V_CMP_GE_F32    : VOPC_Real_si <0x6>;
defm V_CMP_O_F32     : VOPC_Real_si <0x7>;
defm V_CMP_U_F32     : VOPC_Real_si <0x8>;
defm V_CMP_NGE_F32   : VOPC_Real_si <0x9>;
defm V_CMP_NLG_F32   : VOPC_Real_si <0xa>;
defm V_CMP_NGT_F32   : VOPC_Real_si <0xb>;
defm V_CMP_NLE_F32   : VOPC_Real_si <0xc>;
defm V_CMP_NEQ_F32   : VOPC_Real_si <0xd>;
defm V_CMP_NLT_F32   : VOPC_Real_si <0xe>;
defm V_CMP_TRU_F32   : VOPC_Real_si <0xf>;

defm V_CMPX_F_F32    : VOPC_Real_si <0x10>;
defm V_CMPX_LT_F32   : VOPC_Real_si <0x11>;
defm V_CMPX_EQ_F32   : VOPC_Real_si <0x12>;
defm V_CMPX_LE_F32   : VOPC_Real_si <0x13>;
defm V_CMPX_GT_F32   : VOPC_Real_si <0x14>;
defm V_CMPX_LG_F32   : VOPC_Real_si <0x15>;
defm V_CMPX_GE_F32   : VOPC_Real_si <0x16>;
defm V_CMPX_O_F32    : VOPC_Real_si <0x17>;
defm V_CMPX_U_F32    : VOPC_Real_si <0x18>;
defm V_CMPX_NGE_F32  : VOPC_Real_si <0x19>;
defm V_CMPX_NLG_F32  : VOPC_Real_si <0x1a>;
defm V_CMPX_NGT_F32  : VOPC_Real_si <0x1b>;
defm V_CMPX_NLE_F32  : VOPC_Real_si <0x1c>;
defm V_CMPX_NEQ_F32  : VOPC_Real_si <0x1d>;
defm V_CMPX_NLT_F32  : VOPC_Real_si <0x1e>;
defm V_CMPX_TRU_F32  : VOPC_Real_si <0x1f>;

defm V_CMP_F_F64     : VOPC_Real_si <0x20>;
defm V_CMP_LT_F64    : VOPC_Real_si <0x21>;
defm V_CMP_EQ_F64    : VOPC_Real_si <0x22>;
defm V_CMP_LE_F64    : VOPC_Real_si <0x23>;
defm V_CMP_GT_F64    : VOPC_Real_si <0x24>;
defm V_CMP_LG_F64    : VOPC_Real_si <0x25>;
defm V_CMP_GE_F64    : VOPC_Real_si <0x26>;
defm V_CMP_O_F64     : VOPC_Real_si <0x27>;
defm V_CMP_U_F64     : VOPC_Real_si <0x28>;
defm V_CMP_NGE_F64   : VOPC_Real_si <0x29>;
defm V_CMP_NLG_F64   : VOPC_Real_si <0x2a>;
defm V_CMP_NGT_F64   : VOPC_Real_si <0x2b>;
defm V_CMP_NLE_F64   : VOPC_Real_si <0x2c>;
defm V_CMP_NEQ_F64   : VOPC_Real_si <0x2d>;
defm V_CMP_NLT_F64   : VOPC_Real_si <0x2e>;
defm V_CMP_TRU_F64   : VOPC_Real_si <0x2f>;

defm V_CMPX_F_F64    : VOPC_Real_si <0x30>;
defm V_CMPX_LT_F64   : VOPC_Real_si <0x31>;
defm V_CMPX_EQ_F64   : VOPC_Real_si <0x32>;
defm V_CMPX_LE_F64   : VOPC_Real_si <0x33>;
defm V_CMPX_GT_F64   : VOPC_Real_si <0x34>;
defm V_CMPX_LG_F64   : VOPC_Real_si <0x35>;
defm V_CMPX_GE_F64   : VOPC_Real_si <0x36>;
defm V_CMPX_O_F64    : VOPC_Real_si <0x37>;
defm V_CMPX_U_F64    : VOPC_Real_si <0x38>;
defm V_CMPX_NGE_F64  : VOPC_Real_si <0x39>;
defm V_CMPX_NLG_F64  : VOPC_Real_si <0x3a>;
defm V_CMPX_NGT_F64  : VOPC_Real_si <0x3b>;
defm V_CMPX_NLE_F64  : VOPC_Real_si <0x3c>;
defm V_CMPX_NEQ_F64  : VOPC_Real_si <0x3d>;
defm V_CMPX_NLT_F64  : VOPC_Real_si <0x3e>;
defm V_CMPX_TRU_F64  : VOPC_Real_si <0x3f>;

defm V_CMPS_F_F32    : VOPC_Real_si <0x40>;
defm V_CMPS_LT_F32   : VOPC_Real_si <0x41>;
defm V_CMPS_EQ_F32   : VOPC_Real_si <0x42>;
defm V_CMPS_LE_F32   : VOPC_Real_si <0x43>;
defm V_CMPS_GT_F32   : VOPC_Real_si <0x44>;
defm V_CMPS_LG_F32   : VOPC_Real_si <0x45>;
defm V_CMPS_GE_F32   : VOPC_Real_si <0x46>;
defm V_CMPS_O_F32    : VOPC_Real_si <0x47>;
defm V_CMPS_U_F32    : VOPC_Real_si <0x48>;
defm V_CMPS_NGE_F32  : VOPC_Real_si <0x49>;
defm V_CMPS_NLG_F32  : VOPC_Real_si <0x4a>;
defm V_CMPS_NGT_F32  : VOPC_Real_si <0x4b>;
defm V_CMPS_NLE_F32  : VOPC_Real_si <0x4c>;
defm V_CMPS_NEQ_F32  : VOPC_Real_si <0x4d>;
defm V_CMPS_NLT_F32  : VOPC_Real_si <0x4e>;
defm V_CMPS_TRU_F32  : VOPC_Real_si <0x4f>;

defm V_CMPSX_F_F32   : VOPC_Real_si <0x50>;
defm V_CMPSX_LT_F32  : VOPC_Real_si <0x51>;
defm V_CMPSX_EQ_F32  : VOPC_Real_si <0x52>;
defm V_CMPSX_LE_F32  : VOPC_Real_si <0x53>;
defm V_CMPSX_GT_F32  : VOPC_Real_si <0x54>;
defm V_CMPSX_LG_F32  : VOPC_Real_si <0x55>;
defm V_CMPSX_GE_F32  : VOPC_Real_si <0x56>;
defm V_CMPSX_O_F32   : VOPC_Real_si <0x57>;
defm V_CMPSX_U_F32   : VOPC_Real_si <0x58>;
defm V_CMPSX_NGE_F32 : VOPC_Real_si <0x59>;
defm V_CMPSX_NLG_F32 : VOPC_Real_si <0x5a>;
defm V_CMPSX_NGT_F32 : VOPC_Real_si <0x5b>;
defm V_CMPSX_NLE_F32 : VOPC_Real_si <0x5c>;
defm V_CMPSX_NEQ_F32 : VOPC_Real_si <0x5d>;
defm V_CMPSX_NLT_F32 : VOPC_Real_si <0x5e>;
defm V_CMPSX_TRU_F32 : VOPC_Real_si <0x5f>;

defm V_CMPS_F_F64    : VOPC_Real_si <0x60>;
defm V_CMPS_LT_F64   : VOPC_Real_si <0x61>;
defm V_CMPS_EQ_F64   : VOPC_Real_si <0x62>;
defm V_CMPS_LE_F64   : VOPC_Real_si <0x63>;
defm V_CMPS_GT_F64   : VOPC_Real_si <0x64>;
defm V_CMPS_LG_F64   : VOPC_Real_si <0x65>;
defm V_CMPS_GE_F64   : VOPC_Real_si <0x66>;
defm V_CMPS_O_F64    : VOPC_Real_si <0x67>;
defm V_CMPS_U_F64    : VOPC_Real_si <0x68>;
defm V_CMPS_NGE_F64  : VOPC_Real_si <0x69>;
defm V_CMPS_NLG_F64  : VOPC_Real_si <0x6a>;
defm V_CMPS_NGT_F64  : VOPC_Real_si <0x6b>;
defm V_CMPS_NLE_F64  : VOPC_Real_si <0x6c>;
defm V_CMPS_NEQ_F64  : VOPC_Real_si <0x6d>;
defm V_CMPS_NLT_F64  : VOPC_Real_si <0x6e>;
defm V_CMPS_TRU_F64  : VOPC_Real_si <0x6f>;

defm V_CMPSX_F_F64   : VOPC_Real_si <0x70>;
defm V_CMPSX_LT_F64  : VOPC_Real_si <0x71>;
defm V_CMPSX_EQ_F64  : VOPC_Real_si <0x72>;
defm V_CMPSX_LE_F64  : VOPC_Real_si <0x73>;
defm V_CMPSX_GT_F64  : VOPC_Real_si <0x74>;
defm V_CMPSX_LG_F64  : VOPC_Real_si <0x75>;
defm V_CMPSX_GE_F64  : VOPC_Real_si <0x76>;
defm V_CMPSX_O_F64   : VOPC_Real_si <0x77>;
defm V_CMPSX_U_F64   : VOPC_Real_si <0x78>;
defm V_CMPSX_NGE_F64 : VOPC_Real_si <0x79>;
defm V_CMPSX_NLG_F64 : VOPC_Real_si <0x7a>;
defm V_CMPSX_NGT_F64 : VOPC_Real_si <0x7b>;
defm V_CMPSX_NLE_F64 : VOPC_Real_si <0x7c>;
defm V_CMPSX_NEQ_F64 : VOPC_Real_si <0x7d>;
defm V_CMPSX_NLT_F64 : VOPC_Real_si <0x7e>;
defm V_CMPSX_TRU_F64 : VOPC_Real_si <0x7f>;

defm V_CMP_F_I32     : VOPC_Real_si <0x80>;
defm V_CMP_LT_I32    : VOPC_Real_si <0x81>;
defm V_CMP_EQ_I32    : VOPC_Real_si <0x82>;
defm V_CMP_LE_I32    : VOPC_Real_si <0x83>;
defm V_CMP_GT_I32    : VOPC_Real_si <0x84>;
defm V_CMP_NE_I32    : VOPC_Real_si <0x85>;
defm V_CMP_GE_I32    : VOPC_Real_si <0x86>;
defm V_CMP_T_I32     : VOPC_Real_si <0x87>;

defm V_CMPX_F_I32    : VOPC_Real_si <0x90>;
defm V_CMPX_LT_I32   : VOPC_Real_si <0x91>;
defm V_CMPX_EQ_I32   : VOPC_Real_si <0x92>;
defm V_CMPX_LE_I32   : VOPC_Real_si <0x93>;
defm V_CMPX_GT_I32   : VOPC_Real_si <0x94>;
defm V_CMPX_NE_I32   : VOPC_Real_si <0x95>;
defm V_CMPX_GE_I32   : VOPC_Real_si <0x96>;
defm V_CMPX_T_I32    : VOPC_Real_si <0x97>;

defm V_CMP_F_I64     : VOPC_Real_si <0xa0>;
defm V_CMP_LT_I64    : VOPC_Real_si <0xa1>;
defm V_CMP_EQ_I64    : VOPC_Real_si <0xa2>;
defm V_CMP_LE_I64    : VOPC_Real_si <0xa3>;
defm V_CMP_GT_I64    : VOPC_Real_si <0xa4>;
defm V_CMP_NE_I64    : VOPC_Real_si <0xa5>;
defm V_CMP_GE_I64    : VOPC_Real_si <0xa6>;
defm V_CMP_T_I64     : VOPC_Real_si <0xa7>;

defm V_CMPX_F_I64    : VOPC_Real_si <0xb0>;
defm V_CMPX_LT_I64   : VOPC_Real_si <0xb1>;
defm V_CMPX_EQ_I64   : VOPC_Real_si <0xb2>;
defm V_CMPX_LE_I64   : VOPC_Real_si <0xb3>;
defm V_CMPX_GT_I64   : VOPC_Real_si <0xb4>;
defm V_CMPX_NE_I64   : VOPC_Real_si <0xb5>;
defm V_CMPX_GE_I64   : VOPC_Real_si <0xb6>;
defm V_CMPX_T_I64    : VOPC_Real_si <0xb7>;

defm V_CMP_F_U32     : VOPC_Real_si <0xc0>;
defm V_CMP_LT_U32    : VOPC_Real_si <0xc1>;
defm V_CMP_EQ_U32    : VOPC_Real_si <0xc2>;
defm V_CMP_LE_U32    : VOPC_Real_si <0xc3>;
defm V_CMP_GT_U32    : VOPC_Real_si <0xc4>;
defm V_CMP_NE_U32    : VOPC_Real_si <0xc5>;
defm V_CMP_GE_U32    : VOPC_Real_si <0xc6>;
defm V_CMP_T_U32     : VOPC_Real_si <0xc7>;

defm V_CMPX_F_U32    : VOPC_Real_si <0xd0>;
defm V_CMPX_LT_U32   : VOPC_Real_si <0xd1>;
defm V_CMPX_EQ_U32   : VOPC_Real_si <0xd2>;
defm V_CMPX_LE_U32   : VOPC_Real_si <0xd3>;
defm V_CMPX_GT_U32   : VOPC_Real_si <0xd4>;
defm V_CMPX_NE_U32   : VOPC_Real_si <0xd5>;
defm V_CMPX_GE_U32   : VOPC_Real_si <0xd6>;
defm V_CMPX_T_U32    : VOPC_Real_si <0xd7>;

defm V_CMP_F_U64     : VOPC_Real_si <0xe0>;
defm V_CMP_LT_U64    : VOPC_Real_si <0xe1>;
defm V_CMP_EQ_U64    : VOPC_Real_si <0xe2>;
defm V_CMP_LE_U64    : VOPC_Real_si <0xe3>;
defm V_CMP_GT_U64    : VOPC_Real_si <0xe4>;
defm V_CMP_NE_U64    : VOPC_Real_si <0xe5>;
defm V_CMP_GE_U64    : VOPC_Real_si <0xe6>;
defm V_CMP_T_U64     : VOPC_Real_si <0xe7>;

defm V_CMPX_F_U64    : VOPC_Real_si <0xf0>;
defm V_CMPX_LT_U64   : VOPC_Real_si <0xf1>;
defm V_CMPX_EQ_U64   : VOPC_Real_si <0xf2>;
defm V_CMPX_LE_U64   : VOPC_Real_si <0xf3>;
defm V_CMPX_GT_U64   : VOPC_Real_si <0xf4>;
defm V_CMPX_NE_U64   : VOPC_Real_si <0xf5>;
defm V_CMPX_GE_U64   : VOPC_Real_si <0xf6>;
defm V_CMPX_T_U64    : VOPC_Real_si <0xf7>;

defm V_CMP_CLASS_F32  : VOPC_Real_si <0x88>;
defm V_CMPX_CLASS_F32 : VOPC_Real_si <0x98>;
defm V_CMP_CLASS_F64  : VOPC_Real_si <0xa8>;
defm V_CMPX_CLASS_F64 : VOPC_Real_si <0xb8>;

//===----------------------------------------------------------------------===//
// VI
//===----------------------------------------------------------------------===//

multiclass VOPC_Real_vi <bits<10> op> {
  let AssemblerPredicates = [isVI], DecoderNamespace = "VI" in {
    def _e32_vi :
      VOPC_Real<!cast<VOPC_Pseudo>(NAME#"_e32"), SIEncodingFamily.VI>,
      VOPCe<op{7-0}>;

    def _e64_vi :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
      VOP3a_vi <op, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl> {
      // Encoding used for VOPC instructions encoded as VOP3
      // Differs from VOP3e by destination name (sdst) as VOPC doesn't have vector dst
      bits<8> sdst;
      let Inst{7-0} = sdst;
    }
  }

  def _sdwa_vi :
    VOP_SDWA_Real <!cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOPC_SDWAe <op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOPC_SDWA9e <op{7-0}, !cast<VOPC_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  def : VOPCInstAlias <!cast<VOP3_Pseudo>(NAME#"_e64"),
                       !cast<Instruction>(NAME#"_e32_vi")> {
    let AssemblerPredicate = isVI;
  }
}

defm V_CMP_CLASS_F32  : VOPC_Real_vi <0x10>;
defm V_CMPX_CLASS_F32 : VOPC_Real_vi <0x11>;
defm V_CMP_CLASS_F64  : VOPC_Real_vi <0x12>;
defm V_CMPX_CLASS_F64 : VOPC_Real_vi <0x13>;
defm V_CMP_CLASS_F16  : VOPC_Real_vi <0x14>;
defm V_CMPX_CLASS_F16 : VOPC_Real_vi <0x15>;

defm V_CMP_F_F16      : VOPC_Real_vi <0x20>;
defm V_CMP_LT_F16     : VOPC_Real_vi <0x21>;
defm V_CMP_EQ_F16     : VOPC_Real_vi <0x22>;
defm V_CMP_LE_F16     : VOPC_Real_vi <0x23>;
defm V_CMP_GT_F16     : VOPC_Real_vi <0x24>;
defm V_CMP_LG_F16     : VOPC_Real_vi <0x25>;
defm V_CMP_GE_F16     : VOPC_Real_vi <0x26>;
defm V_CMP_O_F16      : VOPC_Real_vi <0x27>;
defm V_CMP_U_F16      : VOPC_Real_vi <0x28>;
defm V_CMP_NGE_F16    : VOPC_Real_vi <0x29>;
defm V_CMP_NLG_F16    : VOPC_Real_vi <0x2a>;
defm V_CMP_NGT_F16    : VOPC_Real_vi <0x2b>;
defm V_CMP_NLE_F16    : VOPC_Real_vi <0x2c>;
defm V_CMP_NEQ_F16    : VOPC_Real_vi <0x2d>;
defm V_CMP_NLT_F16    : VOPC_Real_vi <0x2e>;
defm V_CMP_TRU_F16    : VOPC_Real_vi <0x2f>;

defm V_CMPX_F_F16     : VOPC_Real_vi <0x30>;
defm V_CMPX_LT_F16    : VOPC_Real_vi <0x31>;
defm V_CMPX_EQ_F16    : VOPC_Real_vi <0x32>;
defm V_CMPX_LE_F16    : VOPC_Real_vi <0x33>;
defm V_CMPX_GT_F16    : VOPC_Real_vi <0x34>;
defm V_CMPX_LG_F16    : VOPC_Real_vi <0x35>;
defm V_CMPX_GE_F16    : VOPC_Real_vi <0x36>;
defm V_CMPX_O_F16     : VOPC_Real_vi <0x37>;
defm V_CMPX_U_F16     : VOPC_Real_vi <0x38>;
defm V_CMPX_NGE_F16   : VOPC_Real_vi <0x39>;
defm V_CMPX_NLG_F16   : VOPC_Real_vi <0x3a>;
defm V_CMPX_NGT_F16   : VOPC_Real_vi <0x3b>;
defm V_CMPX_NLE_F16   : VOPC_Real_vi <0x3c>;
defm V_CMPX_NEQ_F16   : VOPC_Real_vi <0x3d>;
defm V_CMPX_NLT_F16   : VOPC_Real_vi <0x3e>;
defm V_CMPX_TRU_F16   : VOPC_Real_vi <0x3f>;

defm V_CMP_F_F32      : VOPC_Real_vi <0x40>;
defm V_CMP_LT_F32     : VOPC_Real_vi <0x41>;
defm V_CMP_EQ_F32     : VOPC_Real_vi <0x42>;
defm V_CMP_LE_F32     : VOPC_Real_vi <0x43>;
defm V_CMP_GT_F32     : VOPC_Real_vi <0x44>;
defm V_CMP_LG_F32     : VOPC_Real_vi <0x45>;
defm V_CMP_GE_F32     : VOPC_Real_vi <0x46>;
defm V_CMP_O_F32      : VOPC_Real_vi <0x47>;
defm V_CMP_U_F32      : VOPC_Real_vi <0x48>;
defm V_CMP_NGE_F32    : VOPC_Real_vi <0x49>;
defm V_CMP_NLG_F32    : VOPC_Real_vi <0x4a>;
defm V_CMP_NGT_F32    : VOPC_Real_vi <0x4b>;
defm V_CMP_NLE_F32    : VOPC_Real_vi <0x4c>;
defm V_CMP_NEQ_F32    : VOPC_Real_vi <0x4d>;
defm V_CMP_NLT_F32    : VOPC_Real_vi <0x4e>;
defm V_CMP_TRU_F32    : VOPC_Real_vi <0x4f>;

defm V_CMPX_F_F32     : VOPC_Real_vi <0x50>;
defm V_CMPX_LT_F32    : VOPC_Real_vi <0x51>;
defm V_CMPX_EQ_F32    : VOPC_Real_vi <0x52>;
defm V_CMPX_LE_F32    : VOPC_Real_vi <0x53>;
defm V_CMPX_GT_F32    : VOPC_Real_vi <0x54>;
defm V_CMPX_LG_F32    : VOPC_Real_vi <0x55>;
defm V_CMPX_GE_F32    : VOPC_Real_vi <0x56>;
defm V_CMPX_O_F32     : VOPC_Real_vi <0x57>;
defm V_CMPX_U_F32     : VOPC_Real_vi <0x58>;
defm V_CMPX_NGE_F32   : VOPC_Real_vi <0x59>;
defm V_CMPX_NLG_F32   : VOPC_Real_vi <0x5a>;
defm V_CMPX_NGT_F32   : VOPC_Real_vi <0x5b>;
defm V_CMPX_NLE_F32   : VOPC_Real_vi <0x5c>;
defm V_CMPX_NEQ_F32   : VOPC_Real_vi <0x5d>;
defm V_CMPX_NLT_F32   : VOPC_Real_vi <0x5e>;
defm V_CMPX_TRU_F32   : VOPC_Real_vi <0x5f>;

defm V_CMP_F_F64      : VOPC_Real_vi <0x60>;
defm V_CMP_LT_F64     : VOPC_Real_vi <0x61>;
defm V_CMP_EQ_F64     : VOPC_Real_vi <0x62>;
defm V_CMP_LE_F64     : VOPC_Real_vi <0x63>;
defm V_CMP_GT_F64     : VOPC_Real_vi <0x64>;
defm V_CMP_LG_F64     : VOPC_Real_vi <0x65>;
defm V_CMP_GE_F64     : VOPC_Real_vi <0x66>;
defm V_CMP_O_F64      : VOPC_Real_vi <0x67>;
defm V_CMP_U_F64      : VOPC_Real_vi <0x68>;
defm V_CMP_NGE_F64    : VOPC_Real_vi <0x69>;
defm V_CMP_NLG_F64    : VOPC_Real_vi <0x6a>;
defm V_CMP_NGT_F64    : VOPC_Real_vi <0x6b>;
defm V_CMP_NLE_F64    : VOPC_Real_vi <0x6c>;
defm V_CMP_NEQ_F64    : VOPC_Real_vi <0x6d>;
defm V_CMP_NLT_F64    : VOPC_Real_vi <0x6e>;
defm V_CMP_TRU_F64    : VOPC_Real_vi <0x6f>;

defm V_CMPX_F_F64     : VOPC_Real_vi <0x70>;
defm V_CMPX_LT_F64    : VOPC_Real_vi <0x71>;
defm V_CMPX_EQ_F64    : VOPC_Real_vi <0x72>;
defm V_CMPX_LE_F64    : VOPC_Real_vi <0x73>;
defm V_CMPX_GT_F64    : VOPC_Real_vi <0x74>;
defm V_CMPX_LG_F64    : VOPC_Real_vi <0x75>;
defm V_CMPX_GE_F64    : VOPC_Real_vi <0x76>;
defm V_CMPX_O_F64     : VOPC_Real_vi <0x77>;
defm V_CMPX_U_F64     : VOPC_Real_vi <0x78>;
defm V_CMPX_NGE_F64   : VOPC_Real_vi <0x79>;
defm V_CMPX_NLG_F64   : VOPC_Real_vi <0x7a>;
defm V_CMPX_NGT_F64   : VOPC_Real_vi <0x7b>;
defm V_CMPX_NLE_F64   : VOPC_Real_vi <0x7c>;
defm V_CMPX_NEQ_F64   : VOPC_Real_vi <0x7d>;
defm V_CMPX_NLT_F64   : VOPC_Real_vi <0x7e>;
defm V_CMPX_TRU_F64   : VOPC_Real_vi <0x7f>;

defm V_CMP_F_I16      : VOPC_Real_vi <0xa0>;
defm V_CMP_LT_I16     : VOPC_Real_vi <0xa1>;
defm V_CMP_EQ_I16     : VOPC_Real_vi <0xa2>;
defm V_CMP_LE_I16     : VOPC_Real_vi <0xa3>;
defm V_CMP_GT_I16     : VOPC_Real_vi <0xa4>;
defm V_CMP_NE_I16     : VOPC_Real_vi <0xa5>;
defm V_CMP_GE_I16     : VOPC_Real_vi <0xa6>;
defm V_CMP_T_I16      : VOPC_Real_vi <0xa7>;

defm V_CMP_F_U16      : VOPC_Real_vi <0xa8>;
defm V_CMP_LT_U16     : VOPC_Real_vi <0xa9>;
defm V_CMP_EQ_U16     : VOPC_Real_vi <0xaa>;
defm V_CMP_LE_U16     : VOPC_Real_vi <0xab>;
defm V_CMP_GT_U16     : VOPC_Real_vi <0xac>;
defm V_CMP_NE_U16     : VOPC_Real_vi <0xad>;
defm V_CMP_GE_U16     : VOPC_Real_vi <0xae>;
defm V_CMP_T_U16      : VOPC_Real_vi <0xaf>;

defm V_CMPX_F_I16 : VOPC_Real_vi <0xb0>;
defm V_CMPX_LT_I16 : VOPC_Real_vi <0xb1>;
defm V_CMPX_EQ_I16 : VOPC_Real_vi <0xb2>;
defm V_CMPX_LE_I16 : VOPC_Real_vi <0xb3>;
defm V_CMPX_GT_I16 : VOPC_Real_vi <0xb4>;
defm V_CMPX_NE_I16 : VOPC_Real_vi <0xb5>;
defm V_CMPX_GE_I16 : VOPC_Real_vi <0xb6>;
defm V_CMPX_T_I16 : VOPC_Real_vi <0xb7>;

defm V_CMPX_F_U16 : VOPC_Real_vi <0xb8>;
defm V_CMPX_LT_U16 : VOPC_Real_vi <0xb9>;
defm V_CMPX_EQ_U16 : VOPC_Real_vi <0xba>;
defm V_CMPX_LE_U16 : VOPC_Real_vi <0xbb>;
defm V_CMPX_GT_U16 : VOPC_Real_vi <0xbc>;
defm V_CMPX_NE_U16 : VOPC_Real_vi <0xbd>;
defm V_CMPX_GE_U16 : VOPC_Real_vi <0xbe>;
defm V_CMPX_T_U16 : VOPC_Real_vi <0xbf>;

defm V_CMP_F_I32      : VOPC_Real_vi <0xc0>;
defm V_CMP_LT_I32     : VOPC_Real_vi <0xc1>;
defm V_CMP_EQ_I32     : VOPC_Real_vi <0xc2>;
defm V_CMP_LE_I32     : VOPC_Real_vi <0xc3>;
defm V_CMP_GT_I32     : VOPC_Real_vi <0xc4>;
defm V_CMP_NE_I32     : VOPC_Real_vi <0xc5>;
defm V_CMP_GE_I32     : VOPC_Real_vi <0xc6>;
defm V_CMP_T_I32      : VOPC_Real_vi <0xc7>;

defm V_CMPX_F_I32     : VOPC_Real_vi <0xd0>;
defm V_CMPX_LT_I32    : VOPC_Real_vi <0xd1>;
defm V_CMPX_EQ_I32    : VOPC_Real_vi <0xd2>;
defm V_CMPX_LE_I32    : VOPC_Real_vi <0xd3>;
defm V_CMPX_GT_I32    : VOPC_Real_vi <0xd4>;
defm V_CMPX_NE_I32    : VOPC_Real_vi <0xd5>;
defm V_CMPX_GE_I32    : VOPC_Real_vi <0xd6>;
defm V_CMPX_T_I32     : VOPC_Real_vi <0xd7>;

defm V_CMP_F_I64      : VOPC_Real_vi <0xe0>;
defm V_CMP_LT_I64     : VOPC_Real_vi <0xe1>;
defm V_CMP_EQ_I64     : VOPC_Real_vi <0xe2>;
defm V_CMP_LE_I64     : VOPC_Real_vi <0xe3>;
defm V_CMP_GT_I64     : VOPC_Real_vi <0xe4>;
defm V_CMP_NE_I64     : VOPC_Real_vi <0xe5>;
defm V_CMP_GE_I64     : VOPC_Real_vi <0xe6>;
defm V_CMP_T_I64      : VOPC_Real_vi <0xe7>;

defm V_CMPX_F_I64     : VOPC_Real_vi <0xf0>;
defm V_CMPX_LT_I64    : VOPC_Real_vi <0xf1>;
defm V_CMPX_EQ_I64    : VOPC_Real_vi <0xf2>;
defm V_CMPX_LE_I64    : VOPC_Real_vi <0xf3>;
defm V_CMPX_GT_I64    : VOPC_Real_vi <0xf4>;
defm V_CMPX_NE_I64    : VOPC_Real_vi <0xf5>;
defm V_CMPX_GE_I64    : VOPC_Real_vi <0xf6>;
defm V_CMPX_T_I64     : VOPC_Real_vi <0xf7>;

defm V_CMP_F_U32      : VOPC_Real_vi <0xc8>;
defm V_CMP_LT_U32     : VOPC_Real_vi <0xc9>;
defm V_CMP_EQ_U32     : VOPC_Real_vi <0xca>;
defm V_CMP_LE_U32     : VOPC_Real_vi <0xcb>;
defm V_CMP_GT_U32     : VOPC_Real_vi <0xcc>;
defm V_CMP_NE_U32     : VOPC_Real_vi <0xcd>;
defm V_CMP_GE_U32     : VOPC_Real_vi <0xce>;
defm V_CMP_T_U32      : VOPC_Real_vi <0xcf>;

defm V_CMPX_F_U32     : VOPC_Real_vi <0xd8>;
defm V_CMPX_LT_U32    : VOPC_Real_vi <0xd9>;
defm V_CMPX_EQ_U32    : VOPC_Real_vi <0xda>;
defm V_CMPX_LE_U32    : VOPC_Real_vi <0xdb>;
defm V_CMPX_GT_U32    : VOPC_Real_vi <0xdc>;
defm V_CMPX_NE_U32    : VOPC_Real_vi <0xdd>;
defm V_CMPX_GE_U32    : VOPC_Real_vi <0xde>;
defm V_CMPX_T_U32     : VOPC_Real_vi <0xdf>;

defm V_CMP_F_U64      : VOPC_Real_vi <0xe8>;
defm V_CMP_LT_U64     : VOPC_Real_vi <0xe9>;
defm V_CMP_EQ_U64     : VOPC_Real_vi <0xea>;
defm V_CMP_LE_U64     : VOPC_Real_vi <0xeb>;
defm V_CMP_GT_U64     : VOPC_Real_vi <0xec>;
defm V_CMP_NE_U64     : VOPC_Real_vi <0xed>;
defm V_CMP_GE_U64     : VOPC_Real_vi <0xee>;
defm V_CMP_T_U64      : VOPC_Real_vi <0xef>;

defm V_CMPX_F_U64     : VOPC_Real_vi <0xf8>;
defm V_CMPX_LT_U64    : VOPC_Real_vi <0xf9>;
defm V_CMPX_EQ_U64    : VOPC_Real_vi <0xfa>;
defm V_CMPX_LE_U64    : VOPC_Real_vi <0xfb>;
defm V_CMPX_GT_U64    : VOPC_Real_vi <0xfc>;
defm V_CMPX_NE_U64    : VOPC_Real_vi <0xfd>;
defm V_CMPX_GE_U64    : VOPC_Real_vi <0xfe>;
defm V_CMPX_T_U64     : VOPC_Real_vi <0xff>;
