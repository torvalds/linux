//===-- SOPInstructions.td - SOP Instruction Defintions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def GPRIdxModeMatchClass : AsmOperandClass {
  let Name = "GPRIdxMode";
  let PredicateMethod = "isGPRIdxMode";
  let RenderMethod = "addImmOperands";
}

def GPRIdxMode : Operand<i32> {
  let PrintMethod = "printVGPRIndexMode";
  let ParserMatchClass = GPRIdxModeMatchClass;
  let OperandType = "OPERAND_IMMEDIATE";
}

class SOP_Pseudo<string opName, dag outs, dag ins, string asmOps,
                  list<dag> pattern=[]> :
    InstSI<outs, ins, "", pattern>,
    SIMCInstr<opName, SIEncodingFamily.NONE> {

  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let SubtargetPredicate = isGCN;

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  bits<1> has_sdst = 0;
}

//===----------------------------------------------------------------------===//
// SOP1 Instructions
//===----------------------------------------------------------------------===//

class SOP1_Pseudo <string opName, dag outs, dag ins,
                   string asmOps, list<dag> pattern=[]> :
  SOP_Pseudo<opName, outs, ins, asmOps, pattern> {

  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SALU = 1;
  let SOP1 = 1;
  let SchedRW = [WriteSALU];
  let Size = 4;
  let UseNamedOperandTable = 1;

  bits<1> has_src0 = 1;
  bits<1> has_sdst = 1;
}

class SOP1_Real<bits<8> op, SOP1_Pseudo ps> :
  InstSI <ps.OutOperandList, ps.InOperandList,
          ps.Mnemonic # " " # ps.AsmOperands, []>,
  Enc32 {

  let isPseudo = 0;
  let isCodeGenOnly = 0;
  let Size = 4;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;

  // encoding
  bits<7> sdst;
  bits<8> src0;

  let Inst{7-0} = !if(ps.has_src0, src0, ?);
  let Inst{15-8} = op;
  let Inst{22-16} = !if(ps.has_sdst, sdst, ?);
  let Inst{31-23} = 0x17d; //encoding;
}

class SOP1_32 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs SReg_32:$sdst), (ins SSrc_b32:$src0),
  "$sdst, $src0", pattern
>;

// 32-bit input, no output.
class SOP1_0_32 <string opName, list<dag> pattern = []> : SOP1_Pseudo <
  opName, (outs), (ins SSrc_b32:$src0),
  "$src0", pattern> {
  let has_sdst = 0;
}

class SOP1_0_32R <string opName, list<dag> pattern = []> : SOP1_Pseudo <
  opName, (outs), (ins SReg_32:$src0),
  "$src0", pattern> {
  let has_sdst = 0;
}

class SOP1_64 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs SReg_64:$sdst), (ins SSrc_b64:$src0),
  "$sdst, $src0", pattern
>;

// 64-bit input, 32-bit output.
class SOP1_32_64 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs SReg_32:$sdst), (ins SSrc_b64:$src0),
  "$sdst, $src0", pattern
>;

// 32-bit input, 64-bit output.
class SOP1_64_32 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs SReg_64:$sdst), (ins SSrc_b32:$src0),
  "$sdst, $src0", pattern
>;

// no input, 64-bit output.
class SOP1_64_0 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs SReg_64:$sdst), (ins), "$sdst", pattern> {
  let has_src0 = 0;
}

// 64-bit input, no output
class SOP1_1 <string opName, list<dag> pattern=[]> : SOP1_Pseudo <
  opName, (outs), (ins SReg_64:$src0), "$src0", pattern> {
  let has_sdst = 0;
}


let isMoveImm = 1 in {
  let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
    def S_MOV_B32 : SOP1_32 <"s_mov_b32">;
    def S_MOV_B64 : SOP1_64 <"s_mov_b64">;
  } // End isRematerializeable = 1

  let Uses = [SCC] in {
    def S_CMOV_B32 : SOP1_32 <"s_cmov_b32">;
    def S_CMOV_B64 : SOP1_64 <"s_cmov_b64">;
  } // End Uses = [SCC]
} // End isMoveImm = 1

let Defs = [SCC] in {
  def S_NOT_B32 : SOP1_32 <"s_not_b32",
    [(set i32:$sdst, (not i32:$src0))]
  >;

  def S_NOT_B64 : SOP1_64 <"s_not_b64",
    [(set i64:$sdst, (not i64:$src0))]
  >;
  def S_WQM_B32 : SOP1_32 <"s_wqm_b32">;
  def S_WQM_B64 : SOP1_64 <"s_wqm_b64",
    [(set i1:$sdst, (int_amdgcn_wqm_vote i1:$src0))]
  >;
} // End Defs = [SCC]


def S_BREV_B32 : SOP1_32 <"s_brev_b32",
  [(set i32:$sdst, (bitreverse i32:$src0))]
>;
def S_BREV_B64 : SOP1_64 <"s_brev_b64">;

let Defs = [SCC] in {
def S_BCNT0_I32_B32 : SOP1_32 <"s_bcnt0_i32_b32">;
def S_BCNT0_I32_B64 : SOP1_32_64 <"s_bcnt0_i32_b64">;
def S_BCNT1_I32_B32 : SOP1_32 <"s_bcnt1_i32_b32",
  [(set i32:$sdst, (ctpop i32:$src0))]
>;
def S_BCNT1_I32_B64 : SOP1_32_64 <"s_bcnt1_i32_b64">;
} // End Defs = [SCC]

def S_FF0_I32_B32 : SOP1_32 <"s_ff0_i32_b32">;
def S_FF0_I32_B64 : SOP1_32_64 <"s_ff0_i32_b64">;
def S_FF1_I32_B64 : SOP1_32_64 <"s_ff1_i32_b64">;

def S_FF1_I32_B32 : SOP1_32 <"s_ff1_i32_b32",
  [(set i32:$sdst, (AMDGPUffbl_b32 i32:$src0))]
>;

def S_FLBIT_I32_B32 : SOP1_32 <"s_flbit_i32_b32",
  [(set i32:$sdst, (AMDGPUffbh_u32 i32:$src0))]
>;

def S_FLBIT_I32_B64 : SOP1_32_64 <"s_flbit_i32_b64">;
def S_FLBIT_I32 : SOP1_32 <"s_flbit_i32",
  [(set i32:$sdst, (AMDGPUffbh_i32 i32:$src0))]
>;
def S_FLBIT_I32_I64 : SOP1_32_64 <"s_flbit_i32_i64">;
def S_SEXT_I32_I8 : SOP1_32 <"s_sext_i32_i8",
  [(set i32:$sdst, (sext_inreg i32:$src0, i8))]
>;
def S_SEXT_I32_I16 : SOP1_32 <"s_sext_i32_i16",
  [(set i32:$sdst, (sext_inreg i32:$src0, i16))]
>;

def S_BITSET0_B32 : SOP1_32    <"s_bitset0_b32">;
def S_BITSET0_B64 : SOP1_64_32 <"s_bitset0_b64">;
def S_BITSET1_B32 : SOP1_32    <"s_bitset1_b32">;
def S_BITSET1_B64 : SOP1_64_32 <"s_bitset1_b64">;
def S_GETPC_B64 : SOP1_64_0  <"s_getpc_b64",
  [(set i64:$sdst, (int_amdgcn_s_getpc))]
>;

let isTerminator = 1, isBarrier = 1, SchedRW = [WriteBranch] in {

let isBranch = 1, isIndirectBranch = 1 in {
def S_SETPC_B64 : SOP1_1  <"s_setpc_b64">;
} // End isBranch = 1, isIndirectBranch = 1

let isReturn = 1 in {
// Define variant marked as return rather than branch.
def S_SETPC_B64_return : SOP1_1<"", [(AMDGPUret_flag i64:$src0)]>;
}
} // End isTerminator = 1, isBarrier = 1

let isCall = 1 in {
def S_SWAPPC_B64 : SOP1_64 <"s_swappc_b64"
>;
}

def S_RFE_B64 : SOP1_1  <"s_rfe_b64">;

let hasSideEffects = 1, Uses = [EXEC], Defs = [EXEC, SCC] in {

def S_AND_SAVEEXEC_B64 : SOP1_64 <"s_and_saveexec_b64">;
def S_OR_SAVEEXEC_B64 : SOP1_64 <"s_or_saveexec_b64">;
def S_XOR_SAVEEXEC_B64 : SOP1_64 <"s_xor_saveexec_b64">;
def S_ANDN2_SAVEEXEC_B64 : SOP1_64 <"s_andn2_saveexec_b64">;
def S_ORN2_SAVEEXEC_B64 : SOP1_64 <"s_orn2_saveexec_b64">;
def S_NAND_SAVEEXEC_B64 : SOP1_64 <"s_nand_saveexec_b64">;
def S_NOR_SAVEEXEC_B64 : SOP1_64 <"s_nor_saveexec_b64">;
def S_XNOR_SAVEEXEC_B64 : SOP1_64 <"s_xnor_saveexec_b64">;

} // End hasSideEffects = 1, Uses = [EXEC], Defs = [EXEC, SCC]

def S_QUADMASK_B32 : SOP1_32 <"s_quadmask_b32">;
def S_QUADMASK_B64 : SOP1_64 <"s_quadmask_b64">;

let Uses = [M0] in {
def S_MOVRELS_B32 : SOP1_32 <"s_movrels_b32">;
def S_MOVRELS_B64 : SOP1_64 <"s_movrels_b64">;
def S_MOVRELD_B32 : SOP1_32 <"s_movreld_b32">;
def S_MOVRELD_B64 : SOP1_64 <"s_movreld_b64">;
} // End Uses = [M0]

def S_CBRANCH_JOIN : SOP1_0_32R <"s_cbranch_join">;
def S_MOV_REGRD_B32 : SOP1_32 <"s_mov_regrd_b32">;
let Defs = [SCC] in {
def S_ABS_I32 : SOP1_32 <"s_abs_i32">;
} // End Defs = [SCC]
def S_MOV_FED_B32 : SOP1_32 <"s_mov_fed_b32">;

let SubtargetPredicate = HasVGPRIndexMode in {
def S_SET_GPR_IDX_IDX : SOP1_0_32<"s_set_gpr_idx_idx"> {
  let Uses = [M0];
  let Defs = [M0];
}
}

let SubtargetPredicate = isGFX9 in {
  let hasSideEffects = 1, Defs = [EXEC, SCC], Uses = [EXEC] in {
    def S_ANDN1_SAVEEXEC_B64 : SOP1_64<"s_andn1_saveexec_b64">;
    def S_ORN1_SAVEEXEC_B64  : SOP1_64<"s_orn1_saveexec_b64">;
    def S_ANDN1_WREXEC_B64   : SOP1_64<"s_andn1_wrexec_b64">;
    def S_ANDN2_WREXEC_B64   : SOP1_64<"s_andn2_wrexec_b64">;
  } // End hasSideEffects = 1, Defs = [EXEC, SCC], Uses = [EXEC]

  def S_BITREPLICATE_B64_B32 : SOP1_64_32<"s_bitreplicate_b64_b32">;
} // End SubtargetPredicate = isGFX9

//===----------------------------------------------------------------------===//
// SOP2 Instructions
//===----------------------------------------------------------------------===//

class SOP2_Pseudo<string opName, dag outs, dag ins,
                  string asmOps, list<dag> pattern=[]> :
  SOP_Pseudo<opName, outs, ins, asmOps, pattern> {

  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SALU = 1;
  let SOP2 = 1;
  let SchedRW = [WriteSALU];
  let UseNamedOperandTable = 1;

  let has_sdst = 1;

  // Pseudo instructions have no encodings, but adding this field here allows
  // us to do:
  // let sdst = xxx in {
  // for multiclasses that include both real and pseudo instructions.
  // field bits<7> sdst = 0;
  // let Size = 4; // Do we need size here?
}

class SOP2_Real<bits<7> op, SOP_Pseudo ps> :
  InstSI <ps.OutOperandList, ps.InOperandList,
          ps.Mnemonic # " " # ps.AsmOperands, []>,
  Enc32 {
  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;

  // encoding
  bits<7> sdst;
  bits<8> src0;
  bits<8> src1;

  let Inst{7-0}   = src0;
  let Inst{15-8}  = src1;
  let Inst{22-16} = !if(ps.has_sdst, sdst, ?);
  let Inst{29-23} = op;
  let Inst{31-30} = 0x2; // encoding
}


class SOP2_32 <string opName, list<dag> pattern=[]> : SOP2_Pseudo <
  opName, (outs SReg_32:$sdst), (ins SSrc_b32:$src0, SSrc_b32:$src1),
  "$sdst, $src0, $src1", pattern
>;

class SOP2_64 <string opName, list<dag> pattern=[]> : SOP2_Pseudo <
  opName, (outs SReg_64:$sdst), (ins SSrc_b64:$src0, SSrc_b64:$src1),
  "$sdst, $src0, $src1", pattern
>;

class SOP2_64_32 <string opName, list<dag> pattern=[]> : SOP2_Pseudo <
  opName, (outs SReg_64:$sdst), (ins SSrc_b64:$src0, SSrc_b32:$src1),
  "$sdst, $src0, $src1", pattern
>;

class SOP2_64_32_32 <string opName, list<dag> pattern=[]> : SOP2_Pseudo <
  opName, (outs SReg_64:$sdst), (ins SSrc_b32:$src0, SSrc_b32:$src1),
  "$sdst, $src0, $src1", pattern
>;

class UniformUnaryFrag<SDPatternOperator Op> : PatFrag <
  (ops node:$src0),
  (Op $src0),
  [{ return !N->isDivergent(); }]
>;

class UniformBinFrag<SDPatternOperator Op> : PatFrag <
  (ops node:$src0, node:$src1),
  (Op $src0, $src1),
  [{ return !N->isDivergent(); }]
>;

let Defs = [SCC] in { // Carry out goes to SCC
let isCommutable = 1 in {
def S_ADD_U32 : SOP2_32 <"s_add_u32">;
def S_ADD_I32 : SOP2_32 <"s_add_i32",
  [(set i32:$sdst, (UniformBinFrag<add> SSrc_b32:$src0, SSrc_b32:$src1))]
>;
} // End isCommutable = 1

def S_SUB_U32 : SOP2_32 <"s_sub_u32">;
def S_SUB_I32 : SOP2_32 <"s_sub_i32",
  [(set i32:$sdst, (UniformBinFrag<sub> SSrc_b32:$src0, SSrc_b32:$src1))]
>;

let Uses = [SCC] in { // Carry in comes from SCC
let isCommutable = 1 in {
def S_ADDC_U32 : SOP2_32 <"s_addc_u32",
  [(set i32:$sdst, (UniformBinFrag<adde> (i32 SSrc_b32:$src0), (i32 SSrc_b32:$src1)))]>;
} // End isCommutable = 1

def S_SUBB_U32 : SOP2_32 <"s_subb_u32",
  [(set i32:$sdst, (UniformBinFrag<sube> (i32 SSrc_b32:$src0), (i32 SSrc_b32:$src1)))]>;
} // End Uses = [SCC]


let isCommutable = 1 in {
def S_MIN_I32 : SOP2_32 <"s_min_i32",
  [(set i32:$sdst, (UniformBinFrag<smin> i32:$src0, i32:$src1))]
>;
def S_MIN_U32 : SOP2_32 <"s_min_u32",
  [(set i32:$sdst, (UniformBinFrag<umin> i32:$src0, i32:$src1))]
>;
def S_MAX_I32 : SOP2_32 <"s_max_i32",
  [(set i32:$sdst, (UniformBinFrag<smax> i32:$src0, i32:$src1))]
>;
def S_MAX_U32 : SOP2_32 <"s_max_u32",
  [(set i32:$sdst, (UniformBinFrag<umax> i32:$src0, i32:$src1))]
>;
} // End isCommutable = 1
} // End Defs = [SCC]


let Uses = [SCC] in {
  def S_CSELECT_B32 : SOP2_32 <"s_cselect_b32">;
  def S_CSELECT_B64 : SOP2_64 <"s_cselect_b64">;
} // End Uses = [SCC]

let Defs = [SCC] in {
let isCommutable = 1 in {
def S_AND_B32 : SOP2_32 <"s_and_b32",
  [(set i32:$sdst, (UniformBinFrag<and> i32:$src0, i32:$src1))]
>;

def S_AND_B64 : SOP2_64 <"s_and_b64",
  [(set i64:$sdst, (UniformBinFrag<and> i64:$src0, i64:$src1))]
>;

def S_OR_B32 : SOP2_32 <"s_or_b32",
  [(set i32:$sdst, (UniformBinFrag<or> i32:$src0, i32:$src1))]
>;

def S_OR_B64 : SOP2_64 <"s_or_b64",
  [(set i64:$sdst, (UniformBinFrag<or> i64:$src0, i64:$src1))]
>;

def S_XOR_B32 : SOP2_32 <"s_xor_b32",
  [(set i32:$sdst, (UniformBinFrag<xor> i32:$src0, i32:$src1))]
>;

def S_XOR_B64 : SOP2_64 <"s_xor_b64",
  [(set i64:$sdst, (UniformBinFrag<xor> i64:$src0, i64:$src1))]
>;

def S_XNOR_B32 : SOP2_32 <"s_xnor_b32",
  [(set i32:$sdst, (not (xor_oneuse i32:$src0, i32:$src1)))]
>;

def S_XNOR_B64 : SOP2_64 <"s_xnor_b64",
  [(set i64:$sdst, (not (xor_oneuse i64:$src0, i64:$src1)))]
>;

def S_NAND_B32 : SOP2_32 <"s_nand_b32",
  [(set i32:$sdst, (not (and_oneuse i32:$src0, i32:$src1)))]
>;

def S_NAND_B64 : SOP2_64 <"s_nand_b64",
  [(set i64:$sdst, (not (and_oneuse i64:$src0, i64:$src1)))]
>;

def S_NOR_B32 : SOP2_32 <"s_nor_b32",
  [(set i32:$sdst, (not (or_oneuse i32:$src0, i32:$src1)))]
>;

def S_NOR_B64 : SOP2_64 <"s_nor_b64",
  [(set i64:$sdst, (not (or_oneuse i64:$src0, i64:$src1)))]
>;
} // End isCommutable = 1

def S_ANDN2_B32 : SOP2_32 <"s_andn2_b32",
  [(set i32:$sdst, (UniformBinFrag<and> i32:$src0, (UniformUnaryFrag<not> i32:$src1)))]
>;

def S_ANDN2_B64 : SOP2_64 <"s_andn2_b64",
  [(set i64:$sdst, (UniformBinFrag<and> i64:$src0, (UniformUnaryFrag<not> i64:$src1)))]
>;

def S_ORN2_B32 : SOP2_32 <"s_orn2_b32",
  [(set i32:$sdst, (UniformBinFrag<or> i32:$src0, (UniformUnaryFrag<not> i32:$src1)))]
>;

def S_ORN2_B64 : SOP2_64 <"s_orn2_b64",
  [(set i64:$sdst, (UniformBinFrag<or> i64:$src0, (UniformUnaryFrag<not> i64:$src1)))]
>;
} // End Defs = [SCC]

// Use added complexity so these patterns are preferred to the VALU patterns.
let AddedComplexity = 1 in {

let Defs = [SCC] in {
// TODO: b64 versions require VOP3 change since v_lshlrev_b64 is VOP3
def S_LSHL_B32 : SOP2_32 <"s_lshl_b32",
  [(set i32:$sdst, (UniformBinFrag<shl> i32:$src0, i32:$src1))]
>;
def S_LSHL_B64 : SOP2_64_32 <"s_lshl_b64",
  [(set i64:$sdst, (UniformBinFrag<shl> i64:$src0, i32:$src1))]
>;
def S_LSHR_B32 : SOP2_32 <"s_lshr_b32",
  [(set i32:$sdst, (UniformBinFrag<srl> i32:$src0, i32:$src1))]
>;
def S_LSHR_B64 : SOP2_64_32 <"s_lshr_b64",
  [(set i64:$sdst, (UniformBinFrag<srl> i64:$src0, i32:$src1))]
>;
def S_ASHR_I32 : SOP2_32 <"s_ashr_i32",
  [(set i32:$sdst, (UniformBinFrag<sra> i32:$src0, i32:$src1))]
>;
def S_ASHR_I64 : SOP2_64_32 <"s_ashr_i64",
  [(set i64:$sdst, (UniformBinFrag<sra> i64:$src0, i32:$src1))]
>;
} // End Defs = [SCC]

def S_BFM_B32 : SOP2_32 <"s_bfm_b32",
  [(set i32:$sdst, (UniformBinFrag<AMDGPUbfm> i32:$src0, i32:$src1))]>;
def S_BFM_B64 : SOP2_64_32_32 <"s_bfm_b64">;

// TODO: S_MUL_I32 require V_MUL_LO_I32 from VOP3 change
def S_MUL_I32 : SOP2_32 <"s_mul_i32",
  [(set i32:$sdst, (mul i32:$src0, i32:$src1))]> {
  let isCommutable = 1;
}

} // End AddedComplexity = 1

let Defs = [SCC] in {
def S_BFE_U32 : SOP2_32 <"s_bfe_u32">;
def S_BFE_I32 : SOP2_32 <"s_bfe_i32">;
def S_BFE_U64 : SOP2_64_32 <"s_bfe_u64">;
def S_BFE_I64 : SOP2_64_32 <"s_bfe_i64">;
} // End Defs = [SCC]

def S_CBRANCH_G_FORK : SOP2_Pseudo <
  "s_cbranch_g_fork", (outs),
  (ins SCSrc_b64:$src0, SCSrc_b64:$src1),
  "$src0, $src1"
> {
  let has_sdst = 0;
}

let Defs = [SCC] in {
def S_ABSDIFF_I32 : SOP2_32 <"s_absdiff_i32">;
} // End Defs = [SCC]

let SubtargetPredicate = isVI in {
  def S_RFE_RESTORE_B64 : SOP2_Pseudo <
    "s_rfe_restore_b64", (outs),
    (ins SSrc_b64:$src0, SSrc_b32:$src1),
    "$src0, $src1"
  > {
    let hasSideEffects = 1;
    let has_sdst = 0;
  }
}

let SubtargetPredicate = isGFX9 in {
  def S_PACK_LL_B32_B16 : SOP2_32<"s_pack_ll_b32_b16">;
  def S_PACK_LH_B32_B16 : SOP2_32<"s_pack_lh_b32_b16">;
  def S_PACK_HH_B32_B16 : SOP2_32<"s_pack_hh_b32_b16">;

  let Defs = [SCC] in {
    def S_LSHL1_ADD_U32 : SOP2_32<"s_lshl1_add_u32">;
    def S_LSHL2_ADD_U32 : SOP2_32<"s_lshl2_add_u32">;
    def S_LSHL3_ADD_U32 : SOP2_32<"s_lshl3_add_u32">;
    def S_LSHL4_ADD_U32 : SOP2_32<"s_lshl4_add_u32">;
  } // End Defs = [SCC]

  def S_MUL_HI_U32 : SOP2_32<"s_mul_hi_u32">;
  def S_MUL_HI_I32 : SOP2_32<"s_mul_hi_i32">;
}

//===----------------------------------------------------------------------===//
// SOPK Instructions
//===----------------------------------------------------------------------===//

class SOPK_Pseudo <string opName, dag outs, dag ins,
                   string asmOps, list<dag> pattern=[]> :
  InstSI <outs, ins, "", pattern>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let SubtargetPredicate = isGCN;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SALU = 1;
  let SOPK = 1;
  let SchedRW = [WriteSALU];
  let UseNamedOperandTable = 1;
  string Mnemonic = opName;
  string AsmOperands = asmOps;

  bits<1> has_sdst = 1;
}

class SOPK_Real<bits<5> op, SOPK_Pseudo ps> :
  InstSI <ps.OutOperandList, ps.InOperandList,
          ps.Mnemonic # " " # ps.AsmOperands, []> {
  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let DisableEncoding    = ps.DisableEncoding;
  let Constraints        = ps.Constraints;

  // encoding
  bits<7>  sdst;
  bits<16> simm16;
  bits<32> imm;
}

class SOPK_Real32<bits<5> op, SOPK_Pseudo ps> :
  SOPK_Real <op, ps>,
  Enc32 {
  let Inst{15-0}  = simm16;
  let Inst{22-16} = !if(ps.has_sdst, sdst, ?);
  let Inst{27-23} = op;
  let Inst{31-28} = 0xb; //encoding
}

class SOPK_Real64<bits<5> op, SOPK_Pseudo ps> :
  SOPK_Real<op, ps>,
  Enc64 {
  let Inst{15-0}  = simm16;
  let Inst{22-16} = !if(ps.has_sdst, sdst, ?);
  let Inst{27-23} = op;
  let Inst{31-28} = 0xb; //encoding
  let Inst{63-32} = imm;
}

class SOPKInstTable <bit is_sopk, string cmpOp = ""> {
  bit IsSOPK = is_sopk;
  string BaseCmpOp = cmpOp;
}

class SOPK_32 <string opName, list<dag> pattern=[]> : SOPK_Pseudo <
  opName,
  (outs SReg_32:$sdst),
  (ins s16imm:$simm16),
  "$sdst, $simm16",
  pattern>;

class SOPK_SCC <string opName, string base_op, bit isSignExt> : SOPK_Pseudo <
  opName,
  (outs),
  !if(isSignExt,
      (ins SReg_32:$sdst, s16imm:$simm16),
      (ins SReg_32:$sdst, u16imm:$simm16)),
  "$sdst, $simm16", []>,
  SOPKInstTable<1, base_op>{
  let Defs = [SCC];
}

class SOPK_32TIE <string opName, list<dag> pattern=[]> : SOPK_Pseudo <
  opName,
  (outs SReg_32:$sdst),
  (ins SReg_32:$src0, s16imm:$simm16),
  "$sdst, $simm16",
  pattern
>;

let isReMaterializable = 1, isMoveImm = 1 in {
def S_MOVK_I32 : SOPK_32 <"s_movk_i32">;
} // End isReMaterializable = 1
let Uses = [SCC] in {
def S_CMOVK_I32 : SOPK_32 <"s_cmovk_i32">;
}

let isCompare = 1 in {

// This instruction is disabled for now until we can figure out how to teach
// the instruction selector to correctly use the  S_CMP* vs V_CMP*
// instructions.
//
// When this instruction is enabled the code generator sometimes produces this
// invalid sequence:
//
// SCC = S_CMPK_EQ_I32 SGPR0, imm
// VCC = COPY SCC
// VGPR0 = V_CNDMASK VCC, VGPR0, VGPR1
//
// def S_CMPK_EQ_I32 : SOPK_SCC <"s_cmpk_eq_i32",
//   [(set i1:$dst, (setcc i32:$src0, imm:$src1, SETEQ))]
// >;

def S_CMPK_EQ_I32 : SOPK_SCC <"s_cmpk_eq_i32", "s_cmp_eq_i32", 1>;
def S_CMPK_LG_I32 : SOPK_SCC <"s_cmpk_lg_i32", "s_cmp_lg_i32", 1>;
def S_CMPK_GT_I32 : SOPK_SCC <"s_cmpk_gt_i32", "s_cmp_gt_i32", 1>;
def S_CMPK_GE_I32 : SOPK_SCC <"s_cmpk_ge_i32", "s_cmp_ge_i32", 1>;
def S_CMPK_LT_I32 : SOPK_SCC <"s_cmpk_lt_i32", "s_cmp_lt_i32", 1>;
def S_CMPK_LE_I32 : SOPK_SCC <"s_cmpk_le_i32", "s_cmp_le_i32", 1>;

let SOPKZext = 1 in {
def S_CMPK_EQ_U32 : SOPK_SCC <"s_cmpk_eq_u32", "s_cmp_eq_u32", 0>;
def S_CMPK_LG_U32 : SOPK_SCC <"s_cmpk_lg_u32", "s_cmp_lg_u32", 0>;
def S_CMPK_GT_U32 : SOPK_SCC <"s_cmpk_gt_u32", "s_cmp_gt_u32", 0>;
def S_CMPK_GE_U32 : SOPK_SCC <"s_cmpk_ge_u32", "s_cmp_ge_u32", 0>;
def S_CMPK_LT_U32 : SOPK_SCC <"s_cmpk_lt_u32", "s_cmp_lt_u32", 0>;
def S_CMPK_LE_U32 : SOPK_SCC <"s_cmpk_le_u32", "s_cmp_le_u32", 0>;
} // End SOPKZext = 1
} // End isCompare = 1

let Defs = [SCC], isCommutable = 1, DisableEncoding = "$src0",
    Constraints = "$sdst = $src0" in {
  def S_ADDK_I32 : SOPK_32TIE <"s_addk_i32">;
  def S_MULK_I32 : SOPK_32TIE <"s_mulk_i32">;
}

def S_CBRANCH_I_FORK : SOPK_Pseudo <
  "s_cbranch_i_fork",
  (outs), (ins SReg_64:$sdst, s16imm:$simm16),
  "$sdst, $simm16"
>;

let mayLoad = 1 in {
def S_GETREG_B32 : SOPK_Pseudo <
  "s_getreg_b32",
  (outs SReg_32:$sdst), (ins hwreg:$simm16),
  "$sdst, $simm16"
>;
}

let hasSideEffects = 1 in {

def S_SETREG_B32 : SOPK_Pseudo <
  "s_setreg_b32",
  (outs), (ins SReg_32:$sdst, hwreg:$simm16),
  "$simm16, $sdst",
  [(AMDGPUsetreg i32:$sdst, (i16 timm:$simm16))]
>;

// FIXME: Not on SI?
//def S_GETREG_REGRD_B32 : SOPK_32 <sopk<0x14, 0x13>, "s_getreg_regrd_b32">;

def S_SETREG_IMM32_B32 : SOPK_Pseudo <
  "s_setreg_imm32_b32",
  (outs), (ins i32imm:$imm, hwreg:$simm16),
  "$simm16, $imm"> {
  let Size = 8; // Unlike every other SOPK instruction.
  let has_sdst = 0;
}

} // End hasSideEffects = 1

let SubtargetPredicate = isGFX9 in {
  def S_CALL_B64 : SOPK_Pseudo<
      "s_call_b64",
      (outs SReg_64:$sdst),
      (ins s16imm:$simm16),
      "$sdst, $simm16"> {
    let isCall = 1;
  }
}

//===----------------------------------------------------------------------===//
// SOPC Instructions
//===----------------------------------------------------------------------===//

class SOPCe <bits<7> op> : Enc32 {
  bits<8> src0;
  bits<8> src1;

  let Inst{7-0} = src0;
  let Inst{15-8} = src1;
  let Inst{22-16} = op;
  let Inst{31-23} = 0x17e;
}

class SOPC <bits<7> op, dag outs, dag ins, string asm,
            list<dag> pattern = []> :
  InstSI<outs, ins, asm, pattern>, SOPCe <op> {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SALU = 1;
  let SOPC = 1;
  let isCodeGenOnly = 0;
  let Defs = [SCC];
  let SchedRW = [WriteSALU];
  let UseNamedOperandTable = 1;
  let SubtargetPredicate = isGCN;
}

class SOPC_Base <bits<7> op, RegisterOperand rc0, RegisterOperand rc1,
                 string opName, list<dag> pattern = []> : SOPC <
  op, (outs), (ins rc0:$src0, rc1:$src1),
  opName#" $src0, $src1", pattern > {
  let Defs = [SCC];
}
class SOPC_Helper <bits<7> op, RegisterOperand rc, ValueType vt,
                    string opName, PatLeaf cond> : SOPC_Base <
  op, rc, rc, opName,
  [(set SCC, (si_setcc_uniform vt:$src0, vt:$src1, cond))] > {
}

class SOPC_CMP_32<bits<7> op, string opName,
                  PatLeaf cond = COND_NULL, string revOp = opName>
  : SOPC_Helper<op, SSrc_b32, i32, opName, cond>,
    Commutable_REV<revOp, !eq(revOp, opName)>,
    SOPKInstTable<0, opName> {
  let isCompare = 1;
  let isCommutable = 1;
}

class SOPC_CMP_64<bits<7> op, string opName,
                  PatLeaf cond = COND_NULL, string revOp = opName>
  : SOPC_Helper<op, SSrc_b64, i64, opName, cond>,
    Commutable_REV<revOp, !eq(revOp, opName)> {
  let isCompare = 1;
  let isCommutable = 1;
}

class SOPC_32<bits<7> op, string opName, list<dag> pattern = []>
  : SOPC_Base<op, SSrc_b32, SSrc_b32, opName, pattern>;

class SOPC_64_32<bits<7> op, string opName, list<dag> pattern = []>
  : SOPC_Base<op, SSrc_b64, SSrc_b32, opName, pattern>;

def S_CMP_EQ_I32 : SOPC_CMP_32 <0x00, "s_cmp_eq_i32">;
def S_CMP_LG_I32 : SOPC_CMP_32 <0x01, "s_cmp_lg_i32">;
def S_CMP_GT_I32 : SOPC_CMP_32 <0x02, "s_cmp_gt_i32", COND_SGT>;
def S_CMP_GE_I32 : SOPC_CMP_32 <0x03, "s_cmp_ge_i32", COND_SGE>;
def S_CMP_LT_I32 : SOPC_CMP_32 <0x04, "s_cmp_lt_i32", COND_SLT, "s_cmp_gt_i32">;
def S_CMP_LE_I32 : SOPC_CMP_32 <0x05, "s_cmp_le_i32", COND_SLE, "s_cmp_ge_i32">;
def S_CMP_EQ_U32 : SOPC_CMP_32 <0x06, "s_cmp_eq_u32", COND_EQ>;
def S_CMP_LG_U32 : SOPC_CMP_32 <0x07, "s_cmp_lg_u32", COND_NE>;
def S_CMP_GT_U32 : SOPC_CMP_32 <0x08, "s_cmp_gt_u32", COND_UGT>;
def S_CMP_GE_U32 : SOPC_CMP_32 <0x09, "s_cmp_ge_u32", COND_UGE>;
def S_CMP_LT_U32 : SOPC_CMP_32 <0x0a, "s_cmp_lt_u32", COND_ULT, "s_cmp_gt_u32">;
def S_CMP_LE_U32 : SOPC_CMP_32 <0x0b, "s_cmp_le_u32", COND_ULE, "s_cmp_ge_u32">;

def S_BITCMP0_B32 : SOPC_32 <0x0c, "s_bitcmp0_b32">;
def S_BITCMP1_B32 : SOPC_32 <0x0d, "s_bitcmp1_b32">;
def S_BITCMP0_B64 : SOPC_64_32 <0x0e, "s_bitcmp0_b64">;
def S_BITCMP1_B64 : SOPC_64_32 <0x0f, "s_bitcmp1_b64">;
def S_SETVSKIP : SOPC_32 <0x10, "s_setvskip">;

let SubtargetPredicate = isVI in {
def S_CMP_EQ_U64 : SOPC_CMP_64 <0x12, "s_cmp_eq_u64", COND_EQ>;
def S_CMP_LG_U64 : SOPC_CMP_64 <0x13, "s_cmp_lg_u64", COND_NE>;
}

let SubtargetPredicate = HasVGPRIndexMode in {
def S_SET_GPR_IDX_ON : SOPC <0x11,
  (outs),
  (ins SSrc_b32:$src0, GPRIdxMode:$src1),
  "s_set_gpr_idx_on $src0,$src1"> {
  let Defs = [M0]; // No scc def
  let Uses = [M0]; // Other bits of m0 unmodified.
  let hasSideEffects = 1; // Sets mode.gpr_idx_en
  let FixedSize = 1;
}
}

//===----------------------------------------------------------------------===//
// SOPP Instructions
//===----------------------------------------------------------------------===//

class SOPPe <bits<7> op> : Enc32 {
  bits <16> simm16;

  let Inst{15-0} = simm16;
  let Inst{22-16} = op;
  let Inst{31-23} = 0x17f; // encoding
}

class SOPP <bits<7> op, dag ins, string asm, list<dag> pattern = []> :
  InstSI <(outs), ins, asm, pattern >, SOPPe <op> {

  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SALU = 1;
  let SOPP = 1;
  let Size = 4;
  let SchedRW = [WriteSALU];

  let UseNamedOperandTable = 1;
  let SubtargetPredicate = isGCN;
}


def S_NOP : SOPP <0x00000000, (ins i16imm:$simm16), "s_nop $simm16">;

let isTerminator = 1 in {

def S_ENDPGM : SOPP <0x00000001, (ins), "s_endpgm",
  [(AMDGPUendpgm)]> {
  let simm16 = 0;
  let isBarrier = 1;
  let isReturn = 1;
}

let SubtargetPredicate = isVI in {
def S_ENDPGM_SAVED : SOPP <0x0000001B, (ins), "s_endpgm_saved"> {
  let simm16 = 0;
  let isBarrier = 1;
  let isReturn = 1;
}
}

let SubtargetPredicate = isGFX9 in {
  let isBarrier = 1, isReturn = 1, simm16 = 0 in {
    def S_ENDPGM_ORDERED_PS_DONE :
      SOPP<0x01e, (ins), "s_endpgm_ordered_ps_done">;
  } // End isBarrier = 1, isReturn = 1, simm16 = 0
} // End SubtargetPredicate = isGFX9

let isBranch = 1, SchedRW = [WriteBranch] in {
def S_BRANCH : SOPP <
  0x00000002, (ins sopp_brtarget:$simm16), "s_branch $simm16",
  [(br bb:$simm16)]> {
  let isBarrier = 1;
}

let Uses = [SCC] in {
def S_CBRANCH_SCC0 : SOPP <
  0x00000004, (ins sopp_brtarget:$simm16),
  "s_cbranch_scc0 $simm16"
>;
def S_CBRANCH_SCC1 : SOPP <
  0x00000005, (ins sopp_brtarget:$simm16),
  "s_cbranch_scc1 $simm16"
>;
} // End Uses = [SCC]

let Uses = [VCC] in {
def S_CBRANCH_VCCZ : SOPP <
  0x00000006, (ins sopp_brtarget:$simm16),
  "s_cbranch_vccz $simm16"
>;
def S_CBRANCH_VCCNZ : SOPP <
  0x00000007, (ins sopp_brtarget:$simm16),
  "s_cbranch_vccnz $simm16"
>;
} // End Uses = [VCC]

let Uses = [EXEC] in {
def S_CBRANCH_EXECZ : SOPP <
  0x00000008, (ins sopp_brtarget:$simm16),
  "s_cbranch_execz $simm16"
>;
def S_CBRANCH_EXECNZ : SOPP <
  0x00000009, (ins sopp_brtarget:$simm16),
  "s_cbranch_execnz $simm16"
>;
} // End Uses = [EXEC]

def S_CBRANCH_CDBGSYS : SOPP <
  0x00000017, (ins sopp_brtarget:$simm16),
  "s_cbranch_cdbgsys $simm16"
>;

def S_CBRANCH_CDBGSYS_AND_USER : SOPP <
  0x0000001A, (ins sopp_brtarget:$simm16),
  "s_cbranch_cdbgsys_and_user $simm16"
>;

def S_CBRANCH_CDBGSYS_OR_USER : SOPP <
  0x00000019, (ins sopp_brtarget:$simm16),
  "s_cbranch_cdbgsys_or_user $simm16"
>;

def S_CBRANCH_CDBGUSER : SOPP <
  0x00000018, (ins sopp_brtarget:$simm16),
  "s_cbranch_cdbguser $simm16"
>;

} // End isBranch = 1
} // End isTerminator = 1

let hasSideEffects = 1 in {
def S_BARRIER : SOPP <0x0000000a, (ins), "s_barrier",
  [(int_amdgcn_s_barrier)]> {
  let SchedRW = [WriteBarrier];
  let simm16 = 0;
  let mayLoad = 1;
  let mayStore = 1;
  let isConvergent = 1;
}

let SubtargetPredicate = isVI in {
def S_WAKEUP : SOPP <0x00000003, (ins), "s_wakeup"> {
  let simm16 = 0;
  let mayLoad = 1;
  let mayStore = 1;
}
}

let mayLoad = 1, mayStore = 1, hasSideEffects = 1 in
def S_WAITCNT : SOPP <0x0000000c, (ins WAIT_FLAG:$simm16), "s_waitcnt $simm16">;
def S_SETHALT : SOPP <0x0000000d, (ins i16imm:$simm16), "s_sethalt $simm16">;
def S_SETKILL : SOPP <0x0000000b, (ins i16imm:$simm16), "s_setkill $simm16">;

// On SI the documentation says sleep for approximately 64 * low 2
// bits, consistent with the reported maximum of 448. On VI the
// maximum reported is 960 cycles, so 960 / 64 = 15 max, so is the
// maximum really 15 on VI?
def S_SLEEP : SOPP <0x0000000e, (ins i32imm:$simm16),
  "s_sleep $simm16", [(int_amdgcn_s_sleep SIMM16bit:$simm16)]> {
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}

def S_SETPRIO : SOPP <0x0000000f, (ins i16imm:$simm16), "s_setprio $simm16">;

let Uses = [EXEC, M0] in {
// FIXME: Should this be mayLoad+mayStore?
def S_SENDMSG : SOPP <0x00000010, (ins SendMsgImm:$simm16), "s_sendmsg $simm16",
  [(AMDGPUsendmsg (i32 imm:$simm16))]
>;

def S_SENDMSGHALT : SOPP <0x00000011, (ins SendMsgImm:$simm16), "s_sendmsghalt $simm16",
  [(AMDGPUsendmsghalt (i32 imm:$simm16))]
>;
} // End Uses = [EXEC, M0]

def S_TRAP : SOPP <0x00000012, (ins i16imm:$simm16), "s_trap $simm16">;
def S_ICACHE_INV : SOPP <0x00000013, (ins), "s_icache_inv"> {
  let simm16 = 0;
}
def S_INCPERFLEVEL : SOPP <0x00000014, (ins i32imm:$simm16), "s_incperflevel $simm16",
  [(int_amdgcn_s_incperflevel SIMM16bit:$simm16)]> {
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}
def S_DECPERFLEVEL : SOPP <0x00000015, (ins i32imm:$simm16), "s_decperflevel $simm16",
  [(int_amdgcn_s_decperflevel SIMM16bit:$simm16)]> {
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
}
def S_TTRACEDATA : SOPP <0x00000016, (ins), "s_ttracedata"> {
  let simm16 = 0;
}

let SubtargetPredicate = HasVGPRIndexMode in {
def S_SET_GPR_IDX_OFF : SOPP<0x1c, (ins), "s_set_gpr_idx_off"> {
  let simm16 = 0;
}
}
} // End hasSideEffects

let SubtargetPredicate = HasVGPRIndexMode in {
def S_SET_GPR_IDX_MODE : SOPP<0x1d, (ins GPRIdxMode:$simm16),
  "s_set_gpr_idx_mode$simm16"> {
  let Defs = [M0];
}
}

//===----------------------------------------------------------------------===//
// S_GETREG_B32 Intrinsic Pattern.
//===----------------------------------------------------------------------===//
def : GCNPat <
  (int_amdgcn_s_getreg imm:$simm16),
  (S_GETREG_B32 (as_i16imm $simm16))
>;

//===----------------------------------------------------------------------===//
// SOP1 Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (i64 (ctpop i64:$src)),
    (i64 (REG_SEQUENCE SReg_64,
     (i32 (COPY_TO_REGCLASS (S_BCNT1_I32_B64 $src), SReg_32)), sub0,
     (S_MOV_B32 (i32 0)), sub1))
>;

def : GCNPat <
  (i32 (smax i32:$x, (i32 (ineg i32:$x)))),
  (S_ABS_I32 $x)
>;

def : GCNPat <
  (i16 imm:$imm),
  (S_MOV_B32 imm:$imm)
>;

// Same as a 32-bit inreg
def : GCNPat<
  (i32 (sext i16:$src)),
  (S_SEXT_I32_I16 $src)
>;


//===----------------------------------------------------------------------===//
// SOP2 Patterns
//===----------------------------------------------------------------------===//

// V_ADD_I32_e32/S_ADD_U32 produces carry in VCC/SCC. For the vector
// case, the sgpr-copies pass will fix this to use the vector version.
def : GCNPat <
  (i32 (addc i32:$src0, i32:$src1)),
  (S_ADD_U32 $src0, $src1)
>;

// FIXME: We need to use COPY_TO_REGCLASS to work-around the fact that
// REG_SEQUENCE patterns don't support instructions with multiple
// outputs.
def : GCNPat<
  (i64 (zext i16:$src)),
    (REG_SEQUENCE SReg_64,
      (i32 (COPY_TO_REGCLASS (S_AND_B32 $src, (S_MOV_B32 (i32 0xffff))), SGPR_32)), sub0,
      (S_MOV_B32 (i32 0)), sub1)
>;

def : GCNPat <
  (i64 (sext i16:$src)),
    (REG_SEQUENCE SReg_64, (i32 (S_SEXT_I32_I16 $src)), sub0,
    (i32 (COPY_TO_REGCLASS (S_ASHR_I32 (i32 (S_SEXT_I32_I16 $src)), (S_MOV_B32 (i32 31))), SGPR_32)), sub1)
>;

def : GCNPat<
  (i32 (zext i16:$src)),
  (S_AND_B32 (S_MOV_B32 (i32 0xffff)), $src)
>;



//===----------------------------------------------------------------------===//
// SOPP Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (int_amdgcn_s_waitcnt i32:$simm16),
  (S_WAITCNT (as_i16imm $simm16))
>;


//===----------------------------------------------------------------------===//
// Real target instructions, move this to the appropriate subtarget TD file
//===----------------------------------------------------------------------===//

class Select_si<string opName> :
  SIMCInstr<opName, SIEncodingFamily.SI> {
  list<Predicate> AssemblerPredicates = [isSICI];
  string DecoderNamespace = "SICI";
}

class SOP1_Real_si<bits<8> op, SOP1_Pseudo ps> :
  SOP1_Real<op, ps>,
  Select_si<ps.Mnemonic>;

class SOP2_Real_si<bits<7> op, SOP2_Pseudo ps> :
  SOP2_Real<op, ps>,
  Select_si<ps.Mnemonic>;

class SOPK_Real_si<bits<5> op, SOPK_Pseudo ps> :
  SOPK_Real32<op, ps>,
  Select_si<ps.Mnemonic>;

def S_MOV_B32_si           : SOP1_Real_si <0x03, S_MOV_B32>;
def S_MOV_B64_si           : SOP1_Real_si <0x04, S_MOV_B64>;
def S_CMOV_B32_si          : SOP1_Real_si <0x05, S_CMOV_B32>;
def S_CMOV_B64_si          : SOP1_Real_si <0x06, S_CMOV_B64>;
def S_NOT_B32_si           : SOP1_Real_si <0x07, S_NOT_B32>;
def S_NOT_B64_si           : SOP1_Real_si <0x08, S_NOT_B64>;
def S_WQM_B32_si           : SOP1_Real_si <0x09, S_WQM_B32>;
def S_WQM_B64_si           : SOP1_Real_si <0x0a, S_WQM_B64>;
def S_BREV_B32_si          : SOP1_Real_si <0x0b, S_BREV_B32>;
def S_BREV_B64_si          : SOP1_Real_si <0x0c, S_BREV_B64>;
def S_BCNT0_I32_B32_si     : SOP1_Real_si <0x0d, S_BCNT0_I32_B32>;
def S_BCNT0_I32_B64_si     : SOP1_Real_si <0x0e, S_BCNT0_I32_B64>;
def S_BCNT1_I32_B32_si     : SOP1_Real_si <0x0f, S_BCNT1_I32_B32>;
def S_BCNT1_I32_B64_si     : SOP1_Real_si <0x10, S_BCNT1_I32_B64>;
def S_FF0_I32_B32_si       : SOP1_Real_si <0x11, S_FF0_I32_B32>;
def S_FF0_I32_B64_si       : SOP1_Real_si <0x12, S_FF0_I32_B64>;
def S_FF1_I32_B32_si       : SOP1_Real_si <0x13, S_FF1_I32_B32>;
def S_FF1_I32_B64_si       : SOP1_Real_si <0x14, S_FF1_I32_B64>;
def S_FLBIT_I32_B32_si     : SOP1_Real_si <0x15, S_FLBIT_I32_B32>;
def S_FLBIT_I32_B64_si     : SOP1_Real_si <0x16, S_FLBIT_I32_B64>;
def S_FLBIT_I32_si         : SOP1_Real_si <0x17, S_FLBIT_I32>;
def S_FLBIT_I32_I64_si     : SOP1_Real_si <0x18, S_FLBIT_I32_I64>;
def S_SEXT_I32_I8_si       : SOP1_Real_si <0x19, S_SEXT_I32_I8>;
def S_SEXT_I32_I16_si      : SOP1_Real_si <0x1a, S_SEXT_I32_I16>;
def S_BITSET0_B32_si       : SOP1_Real_si <0x1b, S_BITSET0_B32>;
def S_BITSET0_B64_si       : SOP1_Real_si <0x1c, S_BITSET0_B64>;
def S_BITSET1_B32_si       : SOP1_Real_si <0x1d, S_BITSET1_B32>;
def S_BITSET1_B64_si       : SOP1_Real_si <0x1e, S_BITSET1_B64>;
def S_GETPC_B64_si         : SOP1_Real_si <0x1f, S_GETPC_B64>;
def S_SETPC_B64_si         : SOP1_Real_si <0x20, S_SETPC_B64>;
def S_SWAPPC_B64_si        : SOP1_Real_si <0x21, S_SWAPPC_B64>;
def S_RFE_B64_si           : SOP1_Real_si <0x22, S_RFE_B64>;
def S_AND_SAVEEXEC_B64_si  : SOP1_Real_si <0x24, S_AND_SAVEEXEC_B64>;
def S_OR_SAVEEXEC_B64_si   : SOP1_Real_si <0x25, S_OR_SAVEEXEC_B64>;
def S_XOR_SAVEEXEC_B64_si  : SOP1_Real_si <0x26, S_XOR_SAVEEXEC_B64>;
def S_ANDN2_SAVEEXEC_B64_si: SOP1_Real_si <0x27, S_ANDN2_SAVEEXEC_B64>;
def S_ORN2_SAVEEXEC_B64_si : SOP1_Real_si <0x28, S_ORN2_SAVEEXEC_B64>;
def S_NAND_SAVEEXEC_B64_si : SOP1_Real_si <0x29, S_NAND_SAVEEXEC_B64>;
def S_NOR_SAVEEXEC_B64_si  : SOP1_Real_si <0x2a, S_NOR_SAVEEXEC_B64>;
def S_XNOR_SAVEEXEC_B64_si : SOP1_Real_si <0x2b, S_XNOR_SAVEEXEC_B64>;
def S_QUADMASK_B32_si      : SOP1_Real_si <0x2c, S_QUADMASK_B32>;
def S_QUADMASK_B64_si      : SOP1_Real_si <0x2d, S_QUADMASK_B64>;
def S_MOVRELS_B32_si       : SOP1_Real_si <0x2e, S_MOVRELS_B32>;
def S_MOVRELS_B64_si       : SOP1_Real_si <0x2f, S_MOVRELS_B64>;
def S_MOVRELD_B32_si       : SOP1_Real_si <0x30, S_MOVRELD_B32>;
def S_MOVRELD_B64_si       : SOP1_Real_si <0x31, S_MOVRELD_B64>;
def S_CBRANCH_JOIN_si      : SOP1_Real_si <0x32, S_CBRANCH_JOIN>;
def S_MOV_REGRD_B32_si     : SOP1_Real_si <0x33, S_MOV_REGRD_B32>;
def S_ABS_I32_si           : SOP1_Real_si <0x34, S_ABS_I32>;
def S_MOV_FED_B32_si       : SOP1_Real_si <0x35, S_MOV_FED_B32>;

def S_ADD_U32_si           : SOP2_Real_si <0x00, S_ADD_U32>;
def S_ADD_I32_si           : SOP2_Real_si <0x02, S_ADD_I32>;
def S_SUB_U32_si           : SOP2_Real_si <0x01, S_SUB_U32>;
def S_SUB_I32_si           : SOP2_Real_si <0x03, S_SUB_I32>;
def S_ADDC_U32_si          : SOP2_Real_si <0x04, S_ADDC_U32>;
def S_SUBB_U32_si          : SOP2_Real_si <0x05, S_SUBB_U32>;
def S_MIN_I32_si           : SOP2_Real_si <0x06, S_MIN_I32>;
def S_MIN_U32_si           : SOP2_Real_si <0x07, S_MIN_U32>;
def S_MAX_I32_si           : SOP2_Real_si <0x08, S_MAX_I32>;
def S_MAX_U32_si           : SOP2_Real_si <0x09, S_MAX_U32>;
def S_CSELECT_B32_si       : SOP2_Real_si <0x0a, S_CSELECT_B32>;
def S_CSELECT_B64_si       : SOP2_Real_si <0x0b, S_CSELECT_B64>;
def S_AND_B32_si           : SOP2_Real_si <0x0e, S_AND_B32>;
def S_AND_B64_si           : SOP2_Real_si <0x0f, S_AND_B64>;
def S_OR_B32_si            : SOP2_Real_si <0x10, S_OR_B32>;
def S_OR_B64_si            : SOP2_Real_si <0x11, S_OR_B64>;
def S_XOR_B32_si           : SOP2_Real_si <0x12, S_XOR_B32>;
def S_XOR_B64_si           : SOP2_Real_si <0x13, S_XOR_B64>;
def S_ANDN2_B32_si         : SOP2_Real_si <0x14, S_ANDN2_B32>;
def S_ANDN2_B64_si         : SOP2_Real_si <0x15, S_ANDN2_B64>;
def S_ORN2_B32_si          : SOP2_Real_si <0x16, S_ORN2_B32>;
def S_ORN2_B64_si          : SOP2_Real_si <0x17, S_ORN2_B64>;
def S_NAND_B32_si          : SOP2_Real_si <0x18, S_NAND_B32>;
def S_NAND_B64_si          : SOP2_Real_si <0x19, S_NAND_B64>;
def S_NOR_B32_si           : SOP2_Real_si <0x1a, S_NOR_B32>;
def S_NOR_B64_si           : SOP2_Real_si <0x1b, S_NOR_B64>;
def S_XNOR_B32_si          : SOP2_Real_si <0x1c, S_XNOR_B32>;
def S_XNOR_B64_si          : SOP2_Real_si <0x1d, S_XNOR_B64>;
def S_LSHL_B32_si          : SOP2_Real_si <0x1e, S_LSHL_B32>;
def S_LSHL_B64_si          : SOP2_Real_si <0x1f, S_LSHL_B64>;
def S_LSHR_B32_si          : SOP2_Real_si <0x20, S_LSHR_B32>;
def S_LSHR_B64_si          : SOP2_Real_si <0x21, S_LSHR_B64>;
def S_ASHR_I32_si          : SOP2_Real_si <0x22, S_ASHR_I32>;
def S_ASHR_I64_si          : SOP2_Real_si <0x23, S_ASHR_I64>;
def S_BFM_B32_si           : SOP2_Real_si <0x24, S_BFM_B32>;
def S_BFM_B64_si           : SOP2_Real_si <0x25, S_BFM_B64>;
def S_MUL_I32_si           : SOP2_Real_si <0x26, S_MUL_I32>;
def S_BFE_U32_si           : SOP2_Real_si <0x27, S_BFE_U32>;
def S_BFE_I32_si           : SOP2_Real_si <0x28, S_BFE_I32>;
def S_BFE_U64_si           : SOP2_Real_si <0x29, S_BFE_U64>;
def S_BFE_I64_si           : SOP2_Real_si <0x2a, S_BFE_I64>;
def S_CBRANCH_G_FORK_si    : SOP2_Real_si <0x2b, S_CBRANCH_G_FORK>;
def S_ABSDIFF_I32_si       : SOP2_Real_si <0x2c, S_ABSDIFF_I32>;

def S_MOVK_I32_si          : SOPK_Real_si <0x00, S_MOVK_I32>;
def S_CMOVK_I32_si         : SOPK_Real_si <0x02, S_CMOVK_I32>;
def S_CMPK_EQ_I32_si       : SOPK_Real_si <0x03, S_CMPK_EQ_I32>;
def S_CMPK_LG_I32_si       : SOPK_Real_si <0x04, S_CMPK_LG_I32>;
def S_CMPK_GT_I32_si       : SOPK_Real_si <0x05, S_CMPK_GT_I32>;
def S_CMPK_GE_I32_si       : SOPK_Real_si <0x06, S_CMPK_GE_I32>;
def S_CMPK_LT_I32_si       : SOPK_Real_si <0x07, S_CMPK_LT_I32>;
def S_CMPK_LE_I32_si       : SOPK_Real_si <0x08, S_CMPK_LE_I32>;
def S_CMPK_EQ_U32_si       : SOPK_Real_si <0x09, S_CMPK_EQ_U32>;
def S_CMPK_LG_U32_si       : SOPK_Real_si <0x0a, S_CMPK_LG_U32>;
def S_CMPK_GT_U32_si       : SOPK_Real_si <0x0b, S_CMPK_GT_U32>;
def S_CMPK_GE_U32_si       : SOPK_Real_si <0x0c, S_CMPK_GE_U32>;
def S_CMPK_LT_U32_si       : SOPK_Real_si <0x0d, S_CMPK_LT_U32>;
def S_CMPK_LE_U32_si       : SOPK_Real_si <0x0e, S_CMPK_LE_U32>;
def S_ADDK_I32_si          : SOPK_Real_si <0x0f, S_ADDK_I32>;
def S_MULK_I32_si          : SOPK_Real_si <0x10, S_MULK_I32>;
def S_CBRANCH_I_FORK_si    : SOPK_Real_si <0x11, S_CBRANCH_I_FORK>;
def S_GETREG_B32_si        : SOPK_Real_si <0x12, S_GETREG_B32>;
def S_SETREG_B32_si        : SOPK_Real_si <0x13, S_SETREG_B32>;
//def S_GETREG_REGRD_B32_si  : SOPK_Real_si <0x14, S_GETREG_REGRD_B32>; // see pseudo for comments
def S_SETREG_IMM32_B32_si  : SOPK_Real64<0x15, S_SETREG_IMM32_B32>,
                             Select_si<S_SETREG_IMM32_B32.Mnemonic>;


class Select_vi<string opName> :
  SIMCInstr<opName, SIEncodingFamily.VI> {
  list<Predicate> AssemblerPredicates = [isVI];
  string DecoderNamespace = "VI";
}

class SOP1_Real_vi<bits<8> op, SOP1_Pseudo ps> :
  SOP1_Real<op, ps>,
  Select_vi<ps.Mnemonic>;


class SOP2_Real_vi<bits<7> op, SOP2_Pseudo ps> :
  SOP2_Real<op, ps>,
  Select_vi<ps.Mnemonic>;

class SOPK_Real_vi<bits<5> op, SOPK_Pseudo ps> :
  SOPK_Real32<op, ps>,
  Select_vi<ps.Mnemonic>;

def S_MOV_B32_vi           : SOP1_Real_vi <0x00, S_MOV_B32>;
def S_MOV_B64_vi           : SOP1_Real_vi <0x01, S_MOV_B64>;
def S_CMOV_B32_vi          : SOP1_Real_vi <0x02, S_CMOV_B32>;
def S_CMOV_B64_vi          : SOP1_Real_vi <0x03, S_CMOV_B64>;
def S_NOT_B32_vi           : SOP1_Real_vi <0x04, S_NOT_B32>;
def S_NOT_B64_vi           : SOP1_Real_vi <0x05, S_NOT_B64>;
def S_WQM_B32_vi           : SOP1_Real_vi <0x06, S_WQM_B32>;
def S_WQM_B64_vi           : SOP1_Real_vi <0x07, S_WQM_B64>;
def S_BREV_B32_vi          : SOP1_Real_vi <0x08, S_BREV_B32>;
def S_BREV_B64_vi          : SOP1_Real_vi <0x09, S_BREV_B64>;
def S_BCNT0_I32_B32_vi     : SOP1_Real_vi <0x0a, S_BCNT0_I32_B32>;
def S_BCNT0_I32_B64_vi     : SOP1_Real_vi <0x0b, S_BCNT0_I32_B64>;
def S_BCNT1_I32_B32_vi     : SOP1_Real_vi <0x0c, S_BCNT1_I32_B32>;
def S_BCNT1_I32_B64_vi     : SOP1_Real_vi <0x0d, S_BCNT1_I32_B64>;
def S_FF0_I32_B32_vi       : SOP1_Real_vi <0x0e, S_FF0_I32_B32>;
def S_FF0_I32_B64_vi       : SOP1_Real_vi <0x0f, S_FF0_I32_B64>;
def S_FF1_I32_B32_vi       : SOP1_Real_vi <0x10, S_FF1_I32_B32>;
def S_FF1_I32_B64_vi       : SOP1_Real_vi <0x11, S_FF1_I32_B64>;
def S_FLBIT_I32_B32_vi     : SOP1_Real_vi <0x12, S_FLBIT_I32_B32>;
def S_FLBIT_I32_B64_vi     : SOP1_Real_vi <0x13, S_FLBIT_I32_B64>;
def S_FLBIT_I32_vi         : SOP1_Real_vi <0x14, S_FLBIT_I32>;
def S_FLBIT_I32_I64_vi     : SOP1_Real_vi <0x15, S_FLBIT_I32_I64>;
def S_SEXT_I32_I8_vi       : SOP1_Real_vi <0x16, S_SEXT_I32_I8>;
def S_SEXT_I32_I16_vi      : SOP1_Real_vi <0x17, S_SEXT_I32_I16>;
def S_BITSET0_B32_vi       : SOP1_Real_vi <0x18, S_BITSET0_B32>;
def S_BITSET0_B64_vi       : SOP1_Real_vi <0x19, S_BITSET0_B64>;
def S_BITSET1_B32_vi       : SOP1_Real_vi <0x1a, S_BITSET1_B32>;
def S_BITSET1_B64_vi       : SOP1_Real_vi <0x1b, S_BITSET1_B64>;
def S_GETPC_B64_vi         : SOP1_Real_vi <0x1c, S_GETPC_B64>;
def S_SETPC_B64_vi         : SOP1_Real_vi <0x1d, S_SETPC_B64>;
def S_SWAPPC_B64_vi        : SOP1_Real_vi <0x1e, S_SWAPPC_B64>;
def S_RFE_B64_vi           : SOP1_Real_vi <0x1f, S_RFE_B64>;
def S_AND_SAVEEXEC_B64_vi  : SOP1_Real_vi <0x20, S_AND_SAVEEXEC_B64>;
def S_OR_SAVEEXEC_B64_vi   : SOP1_Real_vi <0x21, S_OR_SAVEEXEC_B64>;
def S_XOR_SAVEEXEC_B64_vi  : SOP1_Real_vi <0x22, S_XOR_SAVEEXEC_B64>;
def S_ANDN2_SAVEEXEC_B64_vi: SOP1_Real_vi <0x23, S_ANDN2_SAVEEXEC_B64>;
def S_ORN2_SAVEEXEC_B64_vi : SOP1_Real_vi <0x24, S_ORN2_SAVEEXEC_B64>;
def S_NAND_SAVEEXEC_B64_vi : SOP1_Real_vi <0x25, S_NAND_SAVEEXEC_B64>;
def S_NOR_SAVEEXEC_B64_vi  : SOP1_Real_vi <0x26, S_NOR_SAVEEXEC_B64>;
def S_XNOR_SAVEEXEC_B64_vi : SOP1_Real_vi <0x27, S_XNOR_SAVEEXEC_B64>;
def S_QUADMASK_B32_vi      : SOP1_Real_vi <0x28, S_QUADMASK_B32>;
def S_QUADMASK_B64_vi      : SOP1_Real_vi <0x29, S_QUADMASK_B64>;
def S_MOVRELS_B32_vi       : SOP1_Real_vi <0x2a, S_MOVRELS_B32>;
def S_MOVRELS_B64_vi       : SOP1_Real_vi <0x2b, S_MOVRELS_B64>;
def S_MOVRELD_B32_vi       : SOP1_Real_vi <0x2c, S_MOVRELD_B32>;
def S_MOVRELD_B64_vi       : SOP1_Real_vi <0x2d, S_MOVRELD_B64>;
def S_CBRANCH_JOIN_vi      : SOP1_Real_vi <0x2e, S_CBRANCH_JOIN>;
def S_MOV_REGRD_B32_vi     : SOP1_Real_vi <0x2f, S_MOV_REGRD_B32>;
def S_ABS_I32_vi           : SOP1_Real_vi <0x30, S_ABS_I32>;
def S_MOV_FED_B32_vi       : SOP1_Real_vi <0x31, S_MOV_FED_B32>;
def S_SET_GPR_IDX_IDX_vi   : SOP1_Real_vi <0x32, S_SET_GPR_IDX_IDX>;

def S_ADD_U32_vi           : SOP2_Real_vi <0x00, S_ADD_U32>;
def S_ADD_I32_vi           : SOP2_Real_vi <0x02, S_ADD_I32>;
def S_SUB_U32_vi           : SOP2_Real_vi <0x01, S_SUB_U32>;
def S_SUB_I32_vi           : SOP2_Real_vi <0x03, S_SUB_I32>;
def S_ADDC_U32_vi          : SOP2_Real_vi <0x04, S_ADDC_U32>;
def S_SUBB_U32_vi          : SOP2_Real_vi <0x05, S_SUBB_U32>;
def S_MIN_I32_vi           : SOP2_Real_vi <0x06, S_MIN_I32>;
def S_MIN_U32_vi           : SOP2_Real_vi <0x07, S_MIN_U32>;
def S_MAX_I32_vi           : SOP2_Real_vi <0x08, S_MAX_I32>;
def S_MAX_U32_vi           : SOP2_Real_vi <0x09, S_MAX_U32>;
def S_CSELECT_B32_vi       : SOP2_Real_vi <0x0a, S_CSELECT_B32>;
def S_CSELECT_B64_vi       : SOP2_Real_vi <0x0b, S_CSELECT_B64>;
def S_AND_B32_vi           : SOP2_Real_vi <0x0c, S_AND_B32>;
def S_AND_B64_vi           : SOP2_Real_vi <0x0d, S_AND_B64>;
def S_OR_B32_vi            : SOP2_Real_vi <0x0e, S_OR_B32>;
def S_OR_B64_vi            : SOP2_Real_vi <0x0f, S_OR_B64>;
def S_XOR_B32_vi           : SOP2_Real_vi <0x10, S_XOR_B32>;
def S_XOR_B64_vi           : SOP2_Real_vi <0x11, S_XOR_B64>;
def S_ANDN2_B32_vi         : SOP2_Real_vi <0x12, S_ANDN2_B32>;
def S_ANDN2_B64_vi         : SOP2_Real_vi <0x13, S_ANDN2_B64>;
def S_ORN2_B32_vi          : SOP2_Real_vi <0x14, S_ORN2_B32>;
def S_ORN2_B64_vi          : SOP2_Real_vi <0x15, S_ORN2_B64>;
def S_NAND_B32_vi          : SOP2_Real_vi <0x16, S_NAND_B32>;
def S_NAND_B64_vi          : SOP2_Real_vi <0x17, S_NAND_B64>;
def S_NOR_B32_vi           : SOP2_Real_vi <0x18, S_NOR_B32>;
def S_NOR_B64_vi           : SOP2_Real_vi <0x19, S_NOR_B64>;
def S_XNOR_B32_vi          : SOP2_Real_vi <0x1a, S_XNOR_B32>;
def S_XNOR_B64_vi          : SOP2_Real_vi <0x1b, S_XNOR_B64>;
def S_LSHL_B32_vi          : SOP2_Real_vi <0x1c, S_LSHL_B32>;
def S_LSHL_B64_vi          : SOP2_Real_vi <0x1d, S_LSHL_B64>;
def S_LSHR_B32_vi          : SOP2_Real_vi <0x1e, S_LSHR_B32>;
def S_LSHR_B64_vi          : SOP2_Real_vi <0x1f, S_LSHR_B64>;
def S_ASHR_I32_vi          : SOP2_Real_vi <0x20, S_ASHR_I32>;
def S_ASHR_I64_vi          : SOP2_Real_vi <0x21, S_ASHR_I64>;
def S_BFM_B32_vi           : SOP2_Real_vi <0x22, S_BFM_B32>;
def S_BFM_B64_vi           : SOP2_Real_vi <0x23, S_BFM_B64>;
def S_MUL_I32_vi           : SOP2_Real_vi <0x24, S_MUL_I32>;
def S_BFE_U32_vi           : SOP2_Real_vi <0x25, S_BFE_U32>;
def S_BFE_I32_vi           : SOP2_Real_vi <0x26, S_BFE_I32>;
def S_BFE_U64_vi           : SOP2_Real_vi <0x27, S_BFE_U64>;
def S_BFE_I64_vi           : SOP2_Real_vi <0x28, S_BFE_I64>;
def S_CBRANCH_G_FORK_vi    : SOP2_Real_vi <0x29, S_CBRANCH_G_FORK>;
def S_ABSDIFF_I32_vi       : SOP2_Real_vi <0x2a, S_ABSDIFF_I32>;
def S_PACK_LL_B32_B16_vi   : SOP2_Real_vi <0x32, S_PACK_LL_B32_B16>;
def S_PACK_LH_B32_B16_vi   : SOP2_Real_vi <0x33, S_PACK_LH_B32_B16>;
def S_PACK_HH_B32_B16_vi   : SOP2_Real_vi <0x34, S_PACK_HH_B32_B16>;
def S_RFE_RESTORE_B64_vi   : SOP2_Real_vi <0x2b, S_RFE_RESTORE_B64>;

def S_MOVK_I32_vi          : SOPK_Real_vi <0x00, S_MOVK_I32>;
def S_CMOVK_I32_vi         : SOPK_Real_vi <0x01, S_CMOVK_I32>;
def S_CMPK_EQ_I32_vi       : SOPK_Real_vi <0x02, S_CMPK_EQ_I32>;
def S_CMPK_LG_I32_vi       : SOPK_Real_vi <0x03, S_CMPK_LG_I32>;
def S_CMPK_GT_I32_vi       : SOPK_Real_vi <0x04, S_CMPK_GT_I32>;
def S_CMPK_GE_I32_vi       : SOPK_Real_vi <0x05, S_CMPK_GE_I32>;
def S_CMPK_LT_I32_vi       : SOPK_Real_vi <0x06, S_CMPK_LT_I32>;
def S_CMPK_LE_I32_vi       : SOPK_Real_vi <0x07, S_CMPK_LE_I32>;
def S_CMPK_EQ_U32_vi       : SOPK_Real_vi <0x08, S_CMPK_EQ_U32>;
def S_CMPK_LG_U32_vi       : SOPK_Real_vi <0x09, S_CMPK_LG_U32>;
def S_CMPK_GT_U32_vi       : SOPK_Real_vi <0x0A, S_CMPK_GT_U32>;
def S_CMPK_GE_U32_vi       : SOPK_Real_vi <0x0B, S_CMPK_GE_U32>;
def S_CMPK_LT_U32_vi       : SOPK_Real_vi <0x0C, S_CMPK_LT_U32>;
def S_CMPK_LE_U32_vi       : SOPK_Real_vi <0x0D, S_CMPK_LE_U32>;
def S_ADDK_I32_vi          : SOPK_Real_vi <0x0E, S_ADDK_I32>;
def S_MULK_I32_vi          : SOPK_Real_vi <0x0F, S_MULK_I32>;
def S_CBRANCH_I_FORK_vi    : SOPK_Real_vi <0x10, S_CBRANCH_I_FORK>;
def S_GETREG_B32_vi        : SOPK_Real_vi <0x11, S_GETREG_B32>;
def S_SETREG_B32_vi        : SOPK_Real_vi <0x12, S_SETREG_B32>;
//def S_GETREG_REGRD_B32_vi  : SOPK_Real_vi <0x13, S_GETREG_REGRD_B32>; // see pseudo for comments
def S_SETREG_IMM32_B32_vi  : SOPK_Real64<0x14, S_SETREG_IMM32_B32>,
                             Select_vi<S_SETREG_IMM32_B32.Mnemonic>;

def S_CALL_B64_vi          : SOPK_Real_vi <0x15, S_CALL_B64>;

//===----------------------------------------------------------------------===//
// SOP1 - GFX9.
//===----------------------------------------------------------------------===//

def S_ANDN1_SAVEEXEC_B64_vi   : SOP1_Real_vi<0x33, S_ANDN1_SAVEEXEC_B64>;
def S_ORN1_SAVEEXEC_B64_vi    : SOP1_Real_vi<0x34, S_ORN1_SAVEEXEC_B64>;
def S_ANDN1_WREXEC_B64_vi     : SOP1_Real_vi<0x35, S_ANDN1_WREXEC_B64>;
def S_ANDN2_WREXEC_B64_vi     : SOP1_Real_vi<0x36, S_ANDN2_WREXEC_B64>;
def S_BITREPLICATE_B64_B32_vi : SOP1_Real_vi<0x37, S_BITREPLICATE_B64_B32>;

//===----------------------------------------------------------------------===//
// SOP2 - GFX9.
//===----------------------------------------------------------------------===//

def S_LSHL1_ADD_U32_vi   : SOP2_Real_vi<0x2e, S_LSHL1_ADD_U32>;
def S_LSHL2_ADD_U32_vi   : SOP2_Real_vi<0x2f, S_LSHL2_ADD_U32>;
def S_LSHL3_ADD_U32_vi   : SOP2_Real_vi<0x30, S_LSHL3_ADD_U32>;
def S_LSHL4_ADD_U32_vi   : SOP2_Real_vi<0x31, S_LSHL4_ADD_U32>;
def S_MUL_HI_U32_vi      : SOP2_Real_vi<0x2c, S_MUL_HI_U32>;
def S_MUL_HI_I32_vi      : SOP2_Real_vi<0x2d, S_MUL_HI_I32>;
