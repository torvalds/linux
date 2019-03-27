//===- MipsDSPInstrInfo.td - DSP ASE instructions -*- tablegen ------------*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips DSP ASE instructions.
//
//===----------------------------------------------------------------------===//

// ImmLeaf
def immZExt1 : ImmLeaf<i32, [{return isUInt<1>(Imm);}]>;
def immZExt2 : ImmLeaf<i32, [{return isUInt<2>(Imm);}]>;
def immZExt3 : ImmLeaf<i32, [{return isUInt<3>(Imm);}]>;
def immZExt4 : ImmLeaf<i32, [{return isUInt<4>(Imm);}]>;
def immZExt8 : ImmLeaf<i32, [{return isUInt<8>(Imm);}]>;
def immZExt10 : ImmLeaf<i32, [{return isUInt<10>(Imm);}]>;
def immSExt6 : ImmLeaf<i32, [{return isInt<6>(Imm);}]>;
def immSExt10 : ImmLeaf<i32, [{return isInt<10>(Imm);}]>;

// Mips-specific dsp nodes
def SDT_MipsExtr : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>,
                                        SDTCisVT<2, untyped>]>;
def SDT_MipsShilo : SDTypeProfile<1, 2, [SDTCisVT<0, untyped>,
                                         SDTCisSameAs<0, 2>, SDTCisVT<1, i32>]>;
def SDT_MipsDPA : SDTypeProfile<1, 3, [SDTCisVT<0, untyped>, SDTCisSameAs<0, 3>,
                                       SDTCisVT<1, i32>, SDTCisSameAs<1, 2>]>;
def SDT_MipsSHIFT_DSP : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0, 1>,
                                             SDTCisVT<2, i32>]>;

class MipsDSPBase<string Opc, SDTypeProfile Prof> :
  SDNode<!strconcat("MipsISD::", Opc), Prof>;

class MipsDSPSideEffectBase<string Opc, SDTypeProfile Prof> :
  SDNode<!strconcat("MipsISD::", Opc), Prof, [SDNPHasChain, SDNPSideEffect]>;

def MipsEXTP : MipsDSPSideEffectBase<"EXTP", SDT_MipsExtr>;
def MipsEXTPDP : MipsDSPSideEffectBase<"EXTPDP", SDT_MipsExtr>;
def MipsEXTR_S_H : MipsDSPSideEffectBase<"EXTR_S_H", SDT_MipsExtr>;
def MipsEXTR_W : MipsDSPSideEffectBase<"EXTR_W", SDT_MipsExtr>;
def MipsEXTR_R_W : MipsDSPSideEffectBase<"EXTR_R_W", SDT_MipsExtr>;
def MipsEXTR_RS_W : MipsDSPSideEffectBase<"EXTR_RS_W", SDT_MipsExtr>;

def MipsSHILO : MipsDSPBase<"SHILO", SDT_MipsShilo>;
def MipsMTHLIP : MipsDSPSideEffectBase<"MTHLIP", SDT_MipsShilo>;

def MipsMULSAQ_S_W_PH : MipsDSPSideEffectBase<"MULSAQ_S_W_PH", SDT_MipsDPA>;
def MipsMAQ_S_W_PHL : MipsDSPSideEffectBase<"MAQ_S_W_PHL", SDT_MipsDPA>;
def MipsMAQ_S_W_PHR : MipsDSPSideEffectBase<"MAQ_S_W_PHR", SDT_MipsDPA>;
def MipsMAQ_SA_W_PHL : MipsDSPSideEffectBase<"MAQ_SA_W_PHL", SDT_MipsDPA>;
def MipsMAQ_SA_W_PHR : MipsDSPSideEffectBase<"MAQ_SA_W_PHR", SDT_MipsDPA>;

def MipsDPAU_H_QBL : MipsDSPBase<"DPAU_H_QBL", SDT_MipsDPA>;
def MipsDPAU_H_QBR : MipsDSPBase<"DPAU_H_QBR", SDT_MipsDPA>;
def MipsDPSU_H_QBL : MipsDSPBase<"DPSU_H_QBL", SDT_MipsDPA>;
def MipsDPSU_H_QBR : MipsDSPBase<"DPSU_H_QBR", SDT_MipsDPA>;
def MipsDPAQ_S_W_PH : MipsDSPSideEffectBase<"DPAQ_S_W_PH", SDT_MipsDPA>;
def MipsDPSQ_S_W_PH : MipsDSPSideEffectBase<"DPSQ_S_W_PH", SDT_MipsDPA>;
def MipsDPAQ_SA_L_W : MipsDSPSideEffectBase<"DPAQ_SA_L_W", SDT_MipsDPA>;
def MipsDPSQ_SA_L_W : MipsDSPSideEffectBase<"DPSQ_SA_L_W", SDT_MipsDPA>;

def MipsDPA_W_PH : MipsDSPBase<"DPA_W_PH", SDT_MipsDPA>;
def MipsDPS_W_PH : MipsDSPBase<"DPS_W_PH", SDT_MipsDPA>;
def MipsDPAQX_S_W_PH : MipsDSPSideEffectBase<"DPAQX_S_W_PH", SDT_MipsDPA>;
def MipsDPAQX_SA_W_PH : MipsDSPSideEffectBase<"DPAQX_SA_W_PH", SDT_MipsDPA>;
def MipsDPAX_W_PH : MipsDSPBase<"DPAX_W_PH", SDT_MipsDPA>;
def MipsDPSX_W_PH : MipsDSPBase<"DPSX_W_PH", SDT_MipsDPA>;
def MipsDPSQX_S_W_PH : MipsDSPSideEffectBase<"DPSQX_S_W_PH", SDT_MipsDPA>;
def MipsDPSQX_SA_W_PH : MipsDSPSideEffectBase<"DPSQX_SA_W_PH", SDT_MipsDPA>;
def MipsMULSA_W_PH : MipsDSPBase<"MULSA_W_PH", SDT_MipsDPA>;

def MipsMULT : MipsDSPBase<"MULT", SDT_MipsDPA>;
def MipsMULTU : MipsDSPBase<"MULTU", SDT_MipsDPA>;
def MipsMADD_DSP : MipsDSPBase<"MADD_DSP", SDT_MipsDPA>;
def MipsMADDU_DSP : MipsDSPBase<"MADDU_DSP", SDT_MipsDPA>;
def MipsMSUB_DSP : MipsDSPBase<"MSUB_DSP", SDT_MipsDPA>;
def MipsMSUBU_DSP : MipsDSPBase<"MSUBU_DSP", SDT_MipsDPA>;
def MipsSHLL_DSP : MipsDSPBase<"SHLL_DSP", SDT_MipsSHIFT_DSP>;
def MipsSHRA_DSP : MipsDSPBase<"SHRA_DSP", SDT_MipsSHIFT_DSP>;
def MipsSHRL_DSP : MipsDSPBase<"SHRL_DSP", SDT_MipsSHIFT_DSP>;
def MipsSETCC_DSP : MipsDSPBase<"SETCC_DSP", SDTSetCC>;
def MipsSELECT_CC_DSP : MipsDSPBase<"SELECT_CC_DSP", SDTSelectCC>;

// Flags.
class Uses<list<Register> Regs> {
  list<Register> Uses = Regs;
}

class Defs<list<Register> Regs> {
  list<Register> Defs = Regs;
}

// Instruction encoding.
class ADDU_QB_ENC : ADDU_QB_FMT<0b00000>;
class ADDU_S_QB_ENC : ADDU_QB_FMT<0b00100>;
class SUBU_QB_ENC : ADDU_QB_FMT<0b00001>;
class SUBU_S_QB_ENC : ADDU_QB_FMT<0b00101>;
class ADDQ_PH_ENC : ADDU_QB_FMT<0b01010>;
class ADDQ_S_PH_ENC : ADDU_QB_FMT<0b01110>;
class SUBQ_PH_ENC : ADDU_QB_FMT<0b01011>;
class SUBQ_S_PH_ENC : ADDU_QB_FMT<0b01111>;
class ADDQ_S_W_ENC : ADDU_QB_FMT<0b10110>;
class SUBQ_S_W_ENC : ADDU_QB_FMT<0b10111>;
class ADDSC_ENC : ADDU_QB_FMT<0b10000>;
class ADDWC_ENC : ADDU_QB_FMT<0b10001>;
class MODSUB_ENC : ADDU_QB_FMT<0b10010>;
class RADDU_W_QB_ENC : RADDU_W_QB_FMT<0b10100>;
class ABSQ_S_PH_ENC : ABSQ_S_PH_R2_FMT<0b01001>;
class ABSQ_S_W_ENC : ABSQ_S_PH_R2_FMT<0b10001>;
class PRECRQ_QB_PH_ENC : CMP_EQ_QB_R3_FMT<0b01100>;
class PRECRQ_PH_W_ENC : CMP_EQ_QB_R3_FMT<0b10100>;
class PRECRQ_RS_PH_W_ENC : CMP_EQ_QB_R3_FMT<0b10101>;
class PRECRQU_S_QB_PH_ENC : CMP_EQ_QB_R3_FMT<0b01111>;
class PRECEQ_W_PHL_ENC : ABSQ_S_PH_R2_FMT<0b01100>;
class PRECEQ_W_PHR_ENC : ABSQ_S_PH_R2_FMT<0b01101>;
class PRECEQU_PH_QBL_ENC : ABSQ_S_PH_R2_FMT<0b00100>;
class PRECEQU_PH_QBR_ENC : ABSQ_S_PH_R2_FMT<0b00101>;
class PRECEQU_PH_QBLA_ENC : ABSQ_S_PH_R2_FMT<0b00110>;
class PRECEQU_PH_QBRA_ENC : ABSQ_S_PH_R2_FMT<0b00111>;
class PRECEU_PH_QBL_ENC : ABSQ_S_PH_R2_FMT<0b11100>;
class PRECEU_PH_QBR_ENC : ABSQ_S_PH_R2_FMT<0b11101>;
class PRECEU_PH_QBLA_ENC : ABSQ_S_PH_R2_FMT<0b11110>;
class PRECEU_PH_QBRA_ENC : ABSQ_S_PH_R2_FMT<0b11111>;
class SHLL_QB_ENC : SHLL_QB_FMT<0b00000>;
class SHLLV_QB_ENC : SHLL_QB_FMT<0b00010>;
class SHRL_QB_ENC : SHLL_QB_FMT<0b00001>;
class SHRLV_QB_ENC : SHLL_QB_FMT<0b00011>;
class SHLL_PH_ENC : SHLL_QB_FMT<0b01000>;
class SHLLV_PH_ENC : SHLL_QB_FMT<0b01010>;
class SHLL_S_PH_ENC : SHLL_QB_FMT<0b01100>;
class SHLLV_S_PH_ENC : SHLL_QB_FMT<0b01110>;
class SHRA_PH_ENC : SHLL_QB_FMT<0b01001>;
class SHRAV_PH_ENC : SHLL_QB_FMT<0b01011>;
class SHRA_R_PH_ENC : SHLL_QB_FMT<0b01101>;
class SHRAV_R_PH_ENC : SHLL_QB_FMT<0b01111>;
class SHLL_S_W_ENC : SHLL_QB_FMT<0b10100>;
class SHLLV_S_W_ENC : SHLL_QB_FMT<0b10110>;
class SHRA_R_W_ENC : SHLL_QB_FMT<0b10101>;
class SHRAV_R_W_ENC : SHLL_QB_FMT<0b10111>;
class MULEU_S_PH_QBL_ENC : ADDU_QB_FMT<0b00110>;
class MULEU_S_PH_QBR_ENC : ADDU_QB_FMT<0b00111>;
class MULEQ_S_W_PHL_ENC : ADDU_QB_FMT<0b11100>;
class MULEQ_S_W_PHR_ENC : ADDU_QB_FMT<0b11101>;
class MULQ_RS_PH_ENC : ADDU_QB_FMT<0b11111>;
class MULSAQ_S_W_PH_ENC : DPA_W_PH_FMT<0b00110>;
class MAQ_S_W_PHL_ENC : DPA_W_PH_FMT<0b10100>;
class MAQ_S_W_PHR_ENC : DPA_W_PH_FMT<0b10110>;
class MAQ_SA_W_PHL_ENC : DPA_W_PH_FMT<0b10000>;
class MAQ_SA_W_PHR_ENC : DPA_W_PH_FMT<0b10010>;
class MFHI_ENC : MFHI_FMT<0b010000>;
class MFLO_ENC : MFHI_FMT<0b010010>;
class MTHI_ENC : MTHI_FMT<0b010001>;
class MTLO_ENC : MTHI_FMT<0b010011>;
class DPAU_H_QBL_ENC : DPA_W_PH_FMT<0b00011>;
class DPAU_H_QBR_ENC : DPA_W_PH_FMT<0b00111>;
class DPSU_H_QBL_ENC : DPA_W_PH_FMT<0b01011>;
class DPSU_H_QBR_ENC : DPA_W_PH_FMT<0b01111>;
class DPAQ_S_W_PH_ENC : DPA_W_PH_FMT<0b00100>;
class DPSQ_S_W_PH_ENC : DPA_W_PH_FMT<0b00101>;
class DPAQ_SA_L_W_ENC : DPA_W_PH_FMT<0b01100>;
class DPSQ_SA_L_W_ENC : DPA_W_PH_FMT<0b01101>;
class MULT_DSP_ENC : MULT_FMT<0b000000, 0b011000>;
class MULTU_DSP_ENC : MULT_FMT<0b000000, 0b011001>;
class MADD_DSP_ENC : MULT_FMT<0b011100, 0b000000>;
class MADDU_DSP_ENC : MULT_FMT<0b011100, 0b000001>;
class MSUB_DSP_ENC : MULT_FMT<0b011100, 0b000100>;
class MSUBU_DSP_ENC : MULT_FMT<0b011100, 0b000101>;
class CMPU_EQ_QB_ENC : CMP_EQ_QB_R2_FMT<0b00000>;
class CMPU_LT_QB_ENC : CMP_EQ_QB_R2_FMT<0b00001>;
class CMPU_LE_QB_ENC : CMP_EQ_QB_R2_FMT<0b00010>;
class CMPGU_EQ_QB_ENC : CMP_EQ_QB_R3_FMT<0b00100>;
class CMPGU_LT_QB_ENC : CMP_EQ_QB_R3_FMT<0b00101>;
class CMPGU_LE_QB_ENC : CMP_EQ_QB_R3_FMT<0b00110>;
class CMP_EQ_PH_ENC : CMP_EQ_QB_R2_FMT<0b01000>;
class CMP_LT_PH_ENC : CMP_EQ_QB_R2_FMT<0b01001>;
class CMP_LE_PH_ENC : CMP_EQ_QB_R2_FMT<0b01010>;
class BITREV_ENC : ABSQ_S_PH_R2_FMT<0b11011>;
class PACKRL_PH_ENC : CMP_EQ_QB_R3_FMT<0b01110>;
class REPL_QB_ENC : REPL_FMT<0b00010>;
class REPL_PH_ENC : REPL_FMT<0b01010>;
class REPLV_QB_ENC : ABSQ_S_PH_R2_FMT<0b00011>;
class REPLV_PH_ENC : ABSQ_S_PH_R2_FMT<0b01011>;
class PICK_QB_ENC : CMP_EQ_QB_R3_FMT<0b00011>;
class PICK_PH_ENC : CMP_EQ_QB_R3_FMT<0b01011>;
class LWX_ENC : LX_FMT<0b00000>;
class LHX_ENC : LX_FMT<0b00100>;
class LBUX_ENC : LX_FMT<0b00110>;
class BPOSGE32_ENC : BPOSGE32_FMT<0b11100>;
class INSV_ENC : INSV_FMT<0b001100>;

class EXTP_ENC : EXTR_W_TY1_FMT<0b00010>;
class EXTPV_ENC : EXTR_W_TY1_FMT<0b00011>;
class EXTPDP_ENC : EXTR_W_TY1_FMT<0b01010>;
class EXTPDPV_ENC : EXTR_W_TY1_FMT<0b01011>;
class EXTR_W_ENC : EXTR_W_TY1_FMT<0b00000>;
class EXTRV_W_ENC : EXTR_W_TY1_FMT<0b00001>;
class EXTR_R_W_ENC : EXTR_W_TY1_FMT<0b00100>;
class EXTRV_R_W_ENC : EXTR_W_TY1_FMT<0b00101>;
class EXTR_RS_W_ENC : EXTR_W_TY1_FMT<0b00110>;
class EXTRV_RS_W_ENC : EXTR_W_TY1_FMT<0b00111>;
class EXTR_S_H_ENC : EXTR_W_TY1_FMT<0b01110>;
class EXTRV_S_H_ENC : EXTR_W_TY1_FMT<0b01111>;
class SHILO_ENC : SHILO_R1_FMT<0b11010>;
class SHILOV_ENC : SHILO_R2_FMT<0b11011>;
class MTHLIP_ENC : SHILO_R2_FMT<0b11111>;

class RDDSP_ENC : RDDSP_FMT<0b10010>;
class WRDSP_ENC : WRDSP_FMT<0b10011>;
class ADDU_PH_ENC : ADDU_QB_FMT<0b01000>;
class ADDU_S_PH_ENC : ADDU_QB_FMT<0b01100>;
class SUBU_PH_ENC : ADDU_QB_FMT<0b01001>;
class SUBU_S_PH_ENC : ADDU_QB_FMT<0b01101>;
class CMPGDU_EQ_QB_ENC : CMP_EQ_QB_R3_FMT<0b11000>;
class CMPGDU_LT_QB_ENC : CMP_EQ_QB_R3_FMT<0b11001>;
class CMPGDU_LE_QB_ENC : CMP_EQ_QB_R3_FMT<0b11010>;
class ABSQ_S_QB_ENC : ABSQ_S_PH_R2_FMT<0b00001>;
class ADDUH_QB_ENC : ADDUH_QB_FMT<0b00000>;
class ADDUH_R_QB_ENC : ADDUH_QB_FMT<0b00010>;
class SUBUH_QB_ENC : ADDUH_QB_FMT<0b00001>;
class SUBUH_R_QB_ENC : ADDUH_QB_FMT<0b00011>;
class ADDQH_PH_ENC : ADDUH_QB_FMT<0b01000>;
class ADDQH_R_PH_ENC : ADDUH_QB_FMT<0b01010>;
class SUBQH_PH_ENC : ADDUH_QB_FMT<0b01001>;
class SUBQH_R_PH_ENC : ADDUH_QB_FMT<0b01011>;
class ADDQH_W_ENC : ADDUH_QB_FMT<0b10000>;
class ADDQH_R_W_ENC : ADDUH_QB_FMT<0b10010>;
class SUBQH_W_ENC : ADDUH_QB_FMT<0b10001>;
class SUBQH_R_W_ENC : ADDUH_QB_FMT<0b10011>;
class MUL_PH_ENC : ADDUH_QB_FMT<0b01100>;
class MUL_S_PH_ENC : ADDUH_QB_FMT<0b01110>;
class MULQ_S_W_ENC : ADDUH_QB_FMT<0b10110>;
class MULQ_RS_W_ENC : ADDUH_QB_FMT<0b10111>;
class MULQ_S_PH_ENC : ADDU_QB_FMT<0b11110>;
class DPA_W_PH_ENC : DPA_W_PH_FMT<0b00000>;
class DPS_W_PH_ENC : DPA_W_PH_FMT<0b00001>;
class DPAQX_S_W_PH_ENC : DPA_W_PH_FMT<0b11000>;
class DPAQX_SA_W_PH_ENC : DPA_W_PH_FMT<0b11010>;
class DPAX_W_PH_ENC : DPA_W_PH_FMT<0b01000>;
class DPSX_W_PH_ENC : DPA_W_PH_FMT<0b01001>;
class DPSQX_S_W_PH_ENC : DPA_W_PH_FMT<0b11001>;
class DPSQX_SA_W_PH_ENC : DPA_W_PH_FMT<0b11011>;
class MULSA_W_PH_ENC : DPA_W_PH_FMT<0b00010>;
class PRECR_QB_PH_ENC : CMP_EQ_QB_R3_FMT<0b01101>;
class PRECR_SRA_PH_W_ENC : PRECR_SRA_PH_W_FMT<0b11110>;
class PRECR_SRA_R_PH_W_ENC : PRECR_SRA_PH_W_FMT<0b11111>;
class SHRA_QB_ENC : SHLL_QB_FMT<0b00100>;
class SHRAV_QB_ENC : SHLL_QB_FMT<0b00110>;
class SHRA_R_QB_ENC : SHLL_QB_FMT<0b00101>;
class SHRAV_R_QB_ENC : SHLL_QB_FMT<0b00111>;
class SHRL_PH_ENC : SHLL_QB_FMT<0b11001>;
class SHRLV_PH_ENC : SHLL_QB_FMT<0b11011>;
class APPEND_ENC : APPEND_FMT<0b00000>;
class BALIGN_ENC : APPEND_FMT<0b10000>;
class PREPEND_ENC : APPEND_FMT<0b00001>;

// Instruction desc.
class ADDU_QB_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                        InstrItinClass itin, RegisterOperand ROD,
                        RegisterOperand ROS,  RegisterOperand ROT = ROS> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROS:$rs, ROT:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set ROD:$rd, (OpNode ROS:$rs, ROT:$rt))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class RADDU_W_QB_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           InstrItinClass itin, RegisterOperand ROD,
                           RegisterOperand ROS = ROD> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROS:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs");
  list<dag> Pattern = [(set ROD:$rd, (OpNode ROS:$rs))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class CMP_EQ_QB_R2_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                             InstrItinClass itin, RegisterOperand ROS,
                             RegisterOperand ROT = ROS> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins ROS:$rs, ROT:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rs, $rt");
  list<dag> Pattern = [(OpNode ROS:$rs, ROT:$rt)];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class CMP_EQ_QB_R3_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                             InstrItinClass itin, RegisterOperand ROD,
                             RegisterOperand ROS,  RegisterOperand ROT = ROS> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROS:$rs, ROT:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set ROD:$rd, (OpNode ROS:$rs, ROT:$rt))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class PRECR_SRA_PH_W_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                               InstrItinClass itin, RegisterOperand ROT,
                               RegisterOperand ROS = ROT> {
  dag OutOperandList = (outs ROT:$rt);
  dag InOperandList = (ins ROS:$rs, uimm5:$sa, ROS:$src);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $sa");
  list<dag> Pattern = [(set ROT:$rt, (OpNode ROS:$src, ROS:$rs, immZExt5:$sa))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$src = $rt";
  string BaseOpcode = instr_asm;
}

class ABSQ_S_PH_R2_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                             InstrItinClass itin, RegisterOperand ROD,
                             RegisterOperand ROT = ROD> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROT:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt");
  list<dag> Pattern = [(set ROD:$rd, (OpNode ROT:$rt))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class REPL_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                     Operand ImmOp, ImmLeaf immPat, InstrItinClass itin,
                     RegisterOperand RO> {
  dag OutOperandList = (outs RO:$rd);
  dag InOperandList = (ins ImmOp:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rd, $imm");
  list<dag> Pattern = [(set RO:$rd, (OpNode immPat:$imm))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class SHLL_QB_R3_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           InstrItinClass itin, RegisterOperand RO> {
  dag OutOperandList = (outs RO:$rd);
  dag InOperandList =  (ins RO:$rt, GPR32Opnd:$rs_sa);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt, $rs_sa");
  list<dag> Pattern = [(set RO:$rd, (OpNode RO:$rt, GPR32Opnd:$rs_sa))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class SHLL_QB_R2_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           SDPatternOperator ImmPat, InstrItinClass itin,
                           RegisterOperand RO, Operand ImmOpnd> {
  dag OutOperandList = (outs RO:$rd);
  dag InOperandList = (ins RO:$rt, ImmOpnd:$rs_sa);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt, $rs_sa");
  list<dag> Pattern = [(set RO:$rd, (OpNode RO:$rt, ImmPat:$rs_sa))];
  InstrItinClass Itinerary = itin;
  bit hasSideEffects = 1;
  string BaseOpcode = instr_asm;
}

class LX_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                   InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins PtrRC:$base, PtrRC:$index);
  string AsmString = !strconcat(instr_asm, "\t$rd, ${index}(${base})");
  list<dag> Pattern = [(set GPR32Opnd:$rd, (OpNode iPTR:$base, iPTR:$index))];
  InstrItinClass Itinerary = itin;
  bit mayLoad = 1;
  string BaseOpcode = instr_asm;
}

class ADDUH_QB_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                         InstrItinClass itin, RegisterOperand ROD,
                         RegisterOperand ROS = ROD,  RegisterOperand ROT = ROD> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROS:$rs, ROT:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set ROD:$rd, (OpNode ROS:$rs, ROT:$rt))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class APPEND_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                       Operand ImmOp, SDPatternOperator Imm, InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$rs, ImmOp:$sa, GPR32Opnd:$src);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $sa");
  list<dag> Pattern =  [(set GPR32Opnd:$rt,
                        (OpNode GPR32Opnd:$src, GPR32Opnd:$rs, Imm:$sa))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$src = $rt";
  string BaseOpcode = instr_asm;
}

class EXTR_W_TY1_R2_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                              InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins ACC64DSPOpnd:$ac, GPR32Opnd:$shift_rs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $ac, $shift_rs");
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class EXTR_W_TY1_R1_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                              InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins ACC64DSPOpnd:$ac, uimm5:$shift_rs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $ac, $shift_rs");
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
}

class SHILO_R1_DESC_BASE<string instr_asm, SDPatternOperator OpNode> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins simm6:$shift, ACC64DSPOpnd:$acin);
  string AsmString = !strconcat(instr_asm, "\t$ac, $shift");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac,
                        (OpNode immSExt6:$shift, ACC64DSPOpnd:$acin))];
  string Constraints = "$acin = $ac";
  string BaseOpcode = instr_asm;
}

class SHILO_R2_DESC_BASE<string instr_asm, SDPatternOperator OpNode> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs, ACC64DSPOpnd:$acin);
  string AsmString = !strconcat(instr_asm, "\t$ac, $rs");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac,
                        (OpNode GPR32Opnd:$rs, ACC64DSPOpnd:$acin))];
  string Constraints = "$acin = $ac";
  string BaseOpcode = instr_asm;
}

class MTHLIP_DESC_BASE<string instr_asm, SDPatternOperator OpNode> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs, ACC64DSPOpnd:$acin);
  string AsmString = !strconcat(instr_asm, "\t$rs, $ac");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac,
                        (OpNode GPR32Opnd:$rs, ACC64DSPOpnd:$acin))];
  string Constraints = "$acin = $ac";
  string BaseOpcode = instr_asm;
}

class RDDSP_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                      InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins uimm10:$mask);
  string AsmString = !strconcat(instr_asm, "\t$rd, $mask");
  list<dag> Pattern = [(set GPR32Opnd:$rd, (OpNode immZExt10:$mask))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
  bit isMoveReg = 1;
}

class WRDSP_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                      InstrItinClass itin> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins GPR32Opnd:$rs, uimm10:$mask);
  string AsmString = !strconcat(instr_asm, "\t$rs, $mask");
  list<dag> Pattern = [(OpNode GPR32Opnd:$rs, immZExt10:$mask)];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
  bit isMoveReg = 1;
}

class DPA_W_PH_DESC_BASE<string instr_asm, SDPatternOperator OpNode> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs, GPR32Opnd:$rt, ACC64DSPOpnd:$acin);
  string AsmString = !strconcat(instr_asm, "\t$ac, $rs, $rt");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac,
                        (OpNode GPR32Opnd:$rs, GPR32Opnd:$rt, ACC64DSPOpnd:$acin))];
  string Constraints = "$acin = $ac";
  string BaseOpcode = instr_asm;
}

class MULT_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                     InstrItinClass itin> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs, GPR32Opnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$ac, $rs, $rt");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac, (OpNode GPR32Opnd:$rs, GPR32Opnd:$rt))];
  InstrItinClass Itinerary = itin;
  bit isCommutable = 1;
  string BaseOpcode = instr_asm;
}

class MADD_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                     InstrItinClass itin> {
  dag OutOperandList = (outs ACC64DSPOpnd:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs, GPR32Opnd:$rt, ACC64DSPOpnd:$acin);
  string AsmString = !strconcat(instr_asm, "\t$ac, $rs, $rt");
  list<dag> Pattern = [(set ACC64DSPOpnd:$ac,
                        (OpNode GPR32Opnd:$rs, GPR32Opnd:$rt, ACC64DSPOpnd:$acin))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$acin = $ac";
  string BaseOpcode = instr_asm;
}

class MFHI_DESC_BASE<string instr_asm, RegisterOperand RO, SDNode OpNode,
                     InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins RO:$ac);
  string AsmString = !strconcat(instr_asm, "\t$rd, $ac");
  list<dag> Pattern = [(set GPR32Opnd:$rd, (OpNode RO:$ac))];
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
  bit isMoveReg = 1;
}

class MTHI_DESC_BASE<string instr_asm, RegisterOperand RO, InstrItinClass itin> {
  dag OutOperandList = (outs RO:$ac);
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rs, $ac");
  InstrItinClass Itinerary = itin;
  string BaseOpcode = instr_asm;
  bit isMoveReg = 1;
}

class BPOSGE32_PSEUDO_DESC_BASE<SDPatternOperator OpNode, InstrItinClass itin> :
  MipsPseudo<(outs GPR32Opnd:$dst), (ins), [(set GPR32Opnd:$dst, (OpNode))]> {
  bit usesCustomInserter = 1;
}

class BPOSGE32_DESC_BASE<string instr_asm, DAGOperand opnd,
                         InstrItinClass itin> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins opnd:$offset);
  string AsmString = !strconcat(instr_asm, "\t$offset");
  InstrItinClass Itinerary = itin;
  bit isBranch = 1;
  bit isTerminator = 1;
  bit hasDelaySlot = 1;
  string BaseOpcode = instr_asm;
}

class INSV_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                     InstrItinClass itin> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins GPR32Opnd:$src, GPR32Opnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs");
  list<dag> Pattern = [(set GPR32Opnd:$rt, (OpNode GPR32Opnd:$src, GPR32Opnd:$rs))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$src = $rt";
  string BaseOpcode = instr_asm;
}

//===----------------------------------------------------------------------===//
// MIPS DSP Rev 1
//===----------------------------------------------------------------------===//

// Addition/subtraction
class ADDU_QB_DESC : ADDU_QB_DESC_BASE<"addu.qb", null_frag, NoItinerary,
                                       DSPROpnd, DSPROpnd>, IsCommutable,
                     Defs<[DSPOutFlag20]>;

class ADDU_S_QB_DESC : ADDU_QB_DESC_BASE<"addu_s.qb", int_mips_addu_s_qb,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       IsCommutable, Defs<[DSPOutFlag20]>;

class SUBU_QB_DESC : ADDU_QB_DESC_BASE<"subu.qb", null_frag, NoItinerary,
                                       DSPROpnd, DSPROpnd>,
                     Defs<[DSPOutFlag20]>;

class SUBU_S_QB_DESC : ADDU_QB_DESC_BASE<"subu_s.qb", int_mips_subu_s_qb,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       Defs<[DSPOutFlag20]>;

class ADDQ_PH_DESC : ADDU_QB_DESC_BASE<"addq.ph", null_frag, NoItinerary,
                                       DSPROpnd, DSPROpnd>, IsCommutable,
                     Defs<[DSPOutFlag20]>;

class ADDQ_S_PH_DESC : ADDU_QB_DESC_BASE<"addq_s.ph", int_mips_addq_s_ph,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       IsCommutable, Defs<[DSPOutFlag20]>;

class SUBQ_PH_DESC : ADDU_QB_DESC_BASE<"subq.ph", null_frag, NoItinerary,
                                       DSPROpnd, DSPROpnd>,
                     Defs<[DSPOutFlag20]>;

class SUBQ_S_PH_DESC : ADDU_QB_DESC_BASE<"subq_s.ph", int_mips_subq_s_ph,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       Defs<[DSPOutFlag20]>;

class ADDQ_S_W_DESC : ADDU_QB_DESC_BASE<"addq_s.w", int_mips_addq_s_w,
                                        NoItinerary, GPR32Opnd, GPR32Opnd>,
                      IsCommutable, Defs<[DSPOutFlag20]>;

class SUBQ_S_W_DESC : ADDU_QB_DESC_BASE<"subq_s.w", int_mips_subq_s_w,
                                        NoItinerary, GPR32Opnd, GPR32Opnd>,
                      Defs<[DSPOutFlag20]>;

class ADDSC_DESC : ADDU_QB_DESC_BASE<"addsc", null_frag, NoItinerary,
                                     GPR32Opnd, GPR32Opnd>, IsCommutable,
                   Defs<[DSPCarry]>;

class ADDWC_DESC : ADDU_QB_DESC_BASE<"addwc", null_frag, NoItinerary,
                                     GPR32Opnd, GPR32Opnd>,
                   IsCommutable, Uses<[DSPCarry]>, Defs<[DSPOutFlag20]>;

class MODSUB_DESC : ADDU_QB_DESC_BASE<"modsub", int_mips_modsub, NoItinerary,
                                      GPR32Opnd, GPR32Opnd>;

class RADDU_W_QB_DESC : RADDU_W_QB_DESC_BASE<"raddu.w.qb", int_mips_raddu_w_qb,
                                             NoItinerary, GPR32Opnd, DSPROpnd>;

// Absolute value
class ABSQ_S_PH_DESC : ABSQ_S_PH_R2_DESC_BASE<"absq_s.ph", int_mips_absq_s_ph,
                                              NoItinerary, DSPROpnd>,
                       Defs<[DSPOutFlag20]>;

class ABSQ_S_W_DESC : ABSQ_S_PH_R2_DESC_BASE<"absq_s.w", int_mips_absq_s_w,
                                             NoItinerary, GPR32Opnd>,
                      Defs<[DSPOutFlag20]>;

// Precision reduce/expand
class PRECRQ_QB_PH_DESC : CMP_EQ_QB_R3_DESC_BASE<"precrq.qb.ph",
                                                 int_mips_precrq_qb_ph,
                                                 NoItinerary, DSPROpnd, DSPROpnd>;

class PRECRQ_PH_W_DESC : CMP_EQ_QB_R3_DESC_BASE<"precrq.ph.w",
                                                int_mips_precrq_ph_w,
                                                NoItinerary, DSPROpnd, GPR32Opnd>;

class PRECRQ_RS_PH_W_DESC : CMP_EQ_QB_R3_DESC_BASE<"precrq_rs.ph.w",
                                                   int_mips_precrq_rs_ph_w,
                                                   NoItinerary, DSPROpnd,
                                                   GPR32Opnd>,
                            Defs<[DSPOutFlag22]>;

class PRECRQU_S_QB_PH_DESC : CMP_EQ_QB_R3_DESC_BASE<"precrqu_s.qb.ph",
                                                    int_mips_precrqu_s_qb_ph,
                                                    NoItinerary, DSPROpnd,
                                                    DSPROpnd>,
                             Defs<[DSPOutFlag22]>;

class PRECEQ_W_PHL_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceq.w.phl",
                                                 int_mips_preceq_w_phl,
                                                 NoItinerary, GPR32Opnd, DSPROpnd>;

class PRECEQ_W_PHR_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceq.w.phr",
                                                 int_mips_preceq_w_phr,
                                                 NoItinerary, GPR32Opnd, DSPROpnd>;

class PRECEQU_PH_QBL_DESC : ABSQ_S_PH_R2_DESC_BASE<"precequ.ph.qbl",
                                                   int_mips_precequ_ph_qbl,
                                                   NoItinerary, DSPROpnd>;

class PRECEQU_PH_QBR_DESC : ABSQ_S_PH_R2_DESC_BASE<"precequ.ph.qbr",
                                                   int_mips_precequ_ph_qbr,
                                                   NoItinerary, DSPROpnd>;

class PRECEQU_PH_QBLA_DESC : ABSQ_S_PH_R2_DESC_BASE<"precequ.ph.qbla",
                                                    int_mips_precequ_ph_qbla,
                                                    NoItinerary, DSPROpnd>;

class PRECEQU_PH_QBRA_DESC : ABSQ_S_PH_R2_DESC_BASE<"precequ.ph.qbra",
                                                    int_mips_precequ_ph_qbra,
                                                    NoItinerary, DSPROpnd>;

class PRECEU_PH_QBL_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceu.ph.qbl",
                                                  int_mips_preceu_ph_qbl,
                                                  NoItinerary, DSPROpnd>;

class PRECEU_PH_QBR_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceu.ph.qbr",
                                                  int_mips_preceu_ph_qbr,
                                                  NoItinerary, DSPROpnd>;

class PRECEU_PH_QBLA_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceu.ph.qbla",
                                                   int_mips_preceu_ph_qbla,
                                                   NoItinerary, DSPROpnd>;

class PRECEU_PH_QBRA_DESC : ABSQ_S_PH_R2_DESC_BASE<"preceu.ph.qbra",
                                                   int_mips_preceu_ph_qbra,
                                                   NoItinerary, DSPROpnd>;

// Shift
class SHLL_QB_DESC : SHLL_QB_R2_DESC_BASE<"shll.qb", null_frag, immZExt3,
                                          NoItinerary, DSPROpnd, uimm3>,
                     Defs<[DSPOutFlag22]>;

class SHLLV_QB_DESC : SHLL_QB_R3_DESC_BASE<"shllv.qb", int_mips_shll_qb,
                                           NoItinerary, DSPROpnd>,
                      Defs<[DSPOutFlag22]>;

class SHRL_QB_DESC : SHLL_QB_R2_DESC_BASE<"shrl.qb", null_frag, immZExt3,
                                          NoItinerary, DSPROpnd, uimm3>;

class SHRLV_QB_DESC : SHLL_QB_R3_DESC_BASE<"shrlv.qb", int_mips_shrl_qb,
                                           NoItinerary, DSPROpnd>;

class SHLL_PH_DESC : SHLL_QB_R2_DESC_BASE<"shll.ph", null_frag, immZExt4,
                                          NoItinerary, DSPROpnd, uimm4>,
                     Defs<[DSPOutFlag22]>;

class SHLLV_PH_DESC : SHLL_QB_R3_DESC_BASE<"shllv.ph", int_mips_shll_ph,
                                           NoItinerary, DSPROpnd>,
                      Defs<[DSPOutFlag22]>;

class SHLL_S_PH_DESC : SHLL_QB_R2_DESC_BASE<"shll_s.ph", int_mips_shll_s_ph,
                                            immZExt4, NoItinerary, DSPROpnd,
                                            uimm4>,
                       Defs<[DSPOutFlag22]>;

class SHLLV_S_PH_DESC : SHLL_QB_R3_DESC_BASE<"shllv_s.ph", int_mips_shll_s_ph,
                                             NoItinerary, DSPROpnd>,
                        Defs<[DSPOutFlag22]>;

class SHRA_PH_DESC : SHLL_QB_R2_DESC_BASE<"shra.ph", null_frag, immZExt4,
                                          NoItinerary, DSPROpnd, uimm4>;

class SHRAV_PH_DESC : SHLL_QB_R3_DESC_BASE<"shrav.ph", int_mips_shra_ph,
                                           NoItinerary, DSPROpnd>;

class SHRA_R_PH_DESC : SHLL_QB_R2_DESC_BASE<"shra_r.ph", int_mips_shra_r_ph,
                                            immZExt4, NoItinerary, DSPROpnd,
                                            uimm4>;

class SHRAV_R_PH_DESC : SHLL_QB_R3_DESC_BASE<"shrav_r.ph", int_mips_shra_r_ph,
                                             NoItinerary, DSPROpnd>;

class SHLL_S_W_DESC : SHLL_QB_R2_DESC_BASE<"shll_s.w", int_mips_shll_s_w,
                                           immZExt5, NoItinerary, GPR32Opnd,
                                           uimm5>,
                      Defs<[DSPOutFlag22]>;

class SHLLV_S_W_DESC : SHLL_QB_R3_DESC_BASE<"shllv_s.w", int_mips_shll_s_w,
                                            NoItinerary, GPR32Opnd>,
                       Defs<[DSPOutFlag22]>;

class SHRA_R_W_DESC : SHLL_QB_R2_DESC_BASE<"shra_r.w", int_mips_shra_r_w,
                                           immZExt5, NoItinerary, GPR32Opnd,
                                           uimm5>;

class SHRAV_R_W_DESC : SHLL_QB_R3_DESC_BASE<"shrav_r.w", int_mips_shra_r_w,
                                            NoItinerary, GPR32Opnd>;

// Multiplication
class MULEU_S_PH_QBL_DESC : ADDU_QB_DESC_BASE<"muleu_s.ph.qbl",
                                              int_mips_muleu_s_ph_qbl,
                                              NoItinerary, DSPROpnd, DSPROpnd>,
                            Defs<[DSPOutFlag21]>;

class MULEU_S_PH_QBR_DESC : ADDU_QB_DESC_BASE<"muleu_s.ph.qbr",
                                              int_mips_muleu_s_ph_qbr,
                                              NoItinerary, DSPROpnd, DSPROpnd>,
                            Defs<[DSPOutFlag21]>;

class MULEQ_S_W_PHL_DESC : ADDU_QB_DESC_BASE<"muleq_s.w.phl",
                                             int_mips_muleq_s_w_phl,
                                             NoItinerary, GPR32Opnd, DSPROpnd>,
                           IsCommutable, Defs<[DSPOutFlag21]>;

class MULEQ_S_W_PHR_DESC : ADDU_QB_DESC_BASE<"muleq_s.w.phr",
                                             int_mips_muleq_s_w_phr,
                                             NoItinerary, GPR32Opnd, DSPROpnd>,
                           IsCommutable, Defs<[DSPOutFlag21]>;

class MULQ_RS_PH_DESC : ADDU_QB_DESC_BASE<"mulq_rs.ph", int_mips_mulq_rs_ph,
                                          NoItinerary, DSPROpnd, DSPROpnd>,
                        IsCommutable, Defs<[DSPOutFlag21]>;

class MULSAQ_S_W_PH_DESC : DPA_W_PH_DESC_BASE<"mulsaq_s.w.ph",
                                              MipsMULSAQ_S_W_PH>,
                           Defs<[DSPOutFlag16_19]>;

class MAQ_S_W_PHL_DESC : DPA_W_PH_DESC_BASE<"maq_s.w.phl", MipsMAQ_S_W_PHL>,
                         Defs<[DSPOutFlag16_19]>;

class MAQ_S_W_PHR_DESC : DPA_W_PH_DESC_BASE<"maq_s.w.phr", MipsMAQ_S_W_PHR>,
                         Defs<[DSPOutFlag16_19]>;

class MAQ_SA_W_PHL_DESC : DPA_W_PH_DESC_BASE<"maq_sa.w.phl", MipsMAQ_SA_W_PHL>,
                          Defs<[DSPOutFlag16_19]>;

class MAQ_SA_W_PHR_DESC : DPA_W_PH_DESC_BASE<"maq_sa.w.phr", MipsMAQ_SA_W_PHR>,
                          Defs<[DSPOutFlag16_19]>;

// Move from/to hi/lo.
class MFHI_DESC : MFHI_DESC_BASE<"mfhi", ACC64DSPOpnd, MipsMFHI, NoItinerary>;
class MFLO_DESC : MFHI_DESC_BASE<"mflo", ACC64DSPOpnd, MipsMFLO, NoItinerary>;
class MTHI_DESC : MTHI_DESC_BASE<"mthi", HI32DSPOpnd, NoItinerary>;
class MTLO_DESC : MTHI_DESC_BASE<"mtlo", LO32DSPOpnd, NoItinerary>;

// Dot product with accumulate/subtract
class DPAU_H_QBL_DESC : DPA_W_PH_DESC_BASE<"dpau.h.qbl", MipsDPAU_H_QBL>;

class DPAU_H_QBR_DESC : DPA_W_PH_DESC_BASE<"dpau.h.qbr", MipsDPAU_H_QBR>;

class DPSU_H_QBL_DESC : DPA_W_PH_DESC_BASE<"dpsu.h.qbl", MipsDPSU_H_QBL>;

class DPSU_H_QBR_DESC : DPA_W_PH_DESC_BASE<"dpsu.h.qbr", MipsDPSU_H_QBR>;

class DPAQ_S_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpaq_s.w.ph", MipsDPAQ_S_W_PH>,
                         Defs<[DSPOutFlag16_19]>;

class DPSQ_S_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpsq_s.w.ph", MipsDPSQ_S_W_PH>,
                         Defs<[DSPOutFlag16_19]>;

class DPAQ_SA_L_W_DESC : DPA_W_PH_DESC_BASE<"dpaq_sa.l.w", MipsDPAQ_SA_L_W>,
                         Defs<[DSPOutFlag16_19]>;

class DPSQ_SA_L_W_DESC : DPA_W_PH_DESC_BASE<"dpsq_sa.l.w", MipsDPSQ_SA_L_W>,
                         Defs<[DSPOutFlag16_19]>;

class MULT_DSP_DESC  : MULT_DESC_BASE<"mult", MipsMult, NoItinerary>;
class MULTU_DSP_DESC : MULT_DESC_BASE<"multu", MipsMultu, NoItinerary>;
class MADD_DSP_DESC  : MADD_DESC_BASE<"madd", MipsMAdd, NoItinerary>;
class MADDU_DSP_DESC : MADD_DESC_BASE<"maddu", MipsMAddu, NoItinerary>;
class MSUB_DSP_DESC  : MADD_DESC_BASE<"msub", MipsMSub, NoItinerary>;
class MSUBU_DSP_DESC : MADD_DESC_BASE<"msubu", MipsMSubu, NoItinerary>;

// Comparison
class CMPU_EQ_QB_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmpu.eq.qb",
                                               int_mips_cmpu_eq_qb, NoItinerary,
                                               DSPROpnd>,
                        IsCommutable, Defs<[DSPCCond]>;

class CMPU_LT_QB_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmpu.lt.qb",
                                               int_mips_cmpu_lt_qb, NoItinerary,
                                               DSPROpnd>, Defs<[DSPCCond]>;

class CMPU_LE_QB_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmpu.le.qb",
                                               int_mips_cmpu_le_qb, NoItinerary,
                                               DSPROpnd>, Defs<[DSPCCond]>;

class CMPGU_EQ_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgu.eq.qb",
                                                int_mips_cmpgu_eq_qb,
                                                NoItinerary, GPR32Opnd, DSPROpnd>,
                         IsCommutable;

class CMPGU_LT_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgu.lt.qb",
                                                int_mips_cmpgu_lt_qb,
                                                NoItinerary, GPR32Opnd, DSPROpnd>;

class CMPGU_LE_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgu.le.qb",
                                                int_mips_cmpgu_le_qb,
                                                NoItinerary, GPR32Opnd, DSPROpnd>;

class CMP_EQ_PH_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmp.eq.ph", int_mips_cmp_eq_ph,
                                              NoItinerary, DSPROpnd>,
                       IsCommutable, Defs<[DSPCCond]>;

class CMP_LT_PH_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmp.lt.ph", int_mips_cmp_lt_ph,
                                              NoItinerary, DSPROpnd>,
                       Defs<[DSPCCond]>;

class CMP_LE_PH_DESC : CMP_EQ_QB_R2_DESC_BASE<"cmp.le.ph", int_mips_cmp_le_ph,
                                              NoItinerary, DSPROpnd>,
                       Defs<[DSPCCond]>;

// Misc
class BITREV_DESC : ABSQ_S_PH_R2_DESC_BASE<"bitrev", int_mips_bitrev,
                                           NoItinerary, GPR32Opnd>;

class PACKRL_PH_DESC : CMP_EQ_QB_R3_DESC_BASE<"packrl.ph", int_mips_packrl_ph,
                                              NoItinerary, DSPROpnd, DSPROpnd>;

class REPL_QB_DESC : REPL_DESC_BASE<"repl.qb", int_mips_repl_qb, uimm8,
                                    immZExt8, NoItinerary, DSPROpnd>;

class REPL_PH_DESC : REPL_DESC_BASE<"repl.ph", int_mips_repl_ph, simm10,
                                    immSExt10, NoItinerary, DSPROpnd>;

class REPLV_QB_DESC : ABSQ_S_PH_R2_DESC_BASE<"replv.qb", int_mips_repl_qb,
                                             NoItinerary, DSPROpnd, GPR32Opnd>;

class REPLV_PH_DESC : ABSQ_S_PH_R2_DESC_BASE<"replv.ph", int_mips_repl_ph,
                                             NoItinerary, DSPROpnd, GPR32Opnd>;

class PICK_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"pick.qb", int_mips_pick_qb,
                                            NoItinerary, DSPROpnd, DSPROpnd>,
                     Uses<[DSPCCond]>;

class PICK_PH_DESC : CMP_EQ_QB_R3_DESC_BASE<"pick.ph", int_mips_pick_ph,
                                            NoItinerary, DSPROpnd, DSPROpnd>,
                     Uses<[DSPCCond]>;

class LWX_DESC : LX_DESC_BASE<"lwx", int_mips_lwx, NoItinerary>;

class LHX_DESC : LX_DESC_BASE<"lhx", int_mips_lhx, NoItinerary>;

class LBUX_DESC : LX_DESC_BASE<"lbux", int_mips_lbux, NoItinerary>;

class BPOSGE32_DESC : BPOSGE32_DESC_BASE<"bposge32", brtarget, NoItinerary>;

// Extr
class EXTP_DESC : EXTR_W_TY1_R1_DESC_BASE<"extp", MipsEXTP, NoItinerary>,
                  Uses<[DSPPos]>, Defs<[DSPEFI]>;

class EXTPV_DESC : EXTR_W_TY1_R2_DESC_BASE<"extpv", MipsEXTP, NoItinerary>,
                   Uses<[DSPPos]>, Defs<[DSPEFI]>;

class EXTPDP_DESC : EXTR_W_TY1_R1_DESC_BASE<"extpdp", MipsEXTPDP, NoItinerary>,
                    Uses<[DSPPos]>, Defs<[DSPPos, DSPEFI]>;

class EXTPDPV_DESC : EXTR_W_TY1_R2_DESC_BASE<"extpdpv", MipsEXTPDP,
                                             NoItinerary>,
                     Uses<[DSPPos]>, Defs<[DSPPos, DSPEFI]>;

class EXTR_W_DESC : EXTR_W_TY1_R1_DESC_BASE<"extr.w", MipsEXTR_W, NoItinerary>,
                    Defs<[DSPOutFlag23]>;

class EXTRV_W_DESC : EXTR_W_TY1_R2_DESC_BASE<"extrv.w", MipsEXTR_W,
                                             NoItinerary>, Defs<[DSPOutFlag23]>;

class EXTR_R_W_DESC : EXTR_W_TY1_R1_DESC_BASE<"extr_r.w", MipsEXTR_R_W,
                                              NoItinerary>,
                      Defs<[DSPOutFlag23]>;

class EXTRV_R_W_DESC : EXTR_W_TY1_R2_DESC_BASE<"extrv_r.w", MipsEXTR_R_W,
                                               NoItinerary>,
                       Defs<[DSPOutFlag23]>;

class EXTR_RS_W_DESC : EXTR_W_TY1_R1_DESC_BASE<"extr_rs.w", MipsEXTR_RS_W,
                                               NoItinerary>,
                       Defs<[DSPOutFlag23]>;

class EXTRV_RS_W_DESC : EXTR_W_TY1_R2_DESC_BASE<"extrv_rs.w", MipsEXTR_RS_W,
                                                NoItinerary>,
                        Defs<[DSPOutFlag23]>;

class EXTR_S_H_DESC : EXTR_W_TY1_R1_DESC_BASE<"extr_s.h", MipsEXTR_S_H,
                                              NoItinerary>,
                      Defs<[DSPOutFlag23]>;

class EXTRV_S_H_DESC : EXTR_W_TY1_R2_DESC_BASE<"extrv_s.h", MipsEXTR_S_H,
                                               NoItinerary>,
                       Defs<[DSPOutFlag23]>;

class SHILO_DESC : SHILO_R1_DESC_BASE<"shilo", MipsSHILO>;

class SHILOV_DESC : SHILO_R2_DESC_BASE<"shilov", MipsSHILO>;

class MTHLIP_DESC : MTHLIP_DESC_BASE<"mthlip", MipsMTHLIP>, Defs<[DSPPos]>;

class RDDSP_DESC : RDDSP_DESC_BASE<"rddsp", int_mips_rddsp, NoItinerary>;

class WRDSP_DESC : WRDSP_DESC_BASE<"wrdsp", int_mips_wrdsp, NoItinerary>;

class INSV_DESC : INSV_DESC_BASE<"insv", int_mips_insv, NoItinerary>,
                  Uses<[DSPPos, DSPSCount]>;

//===----------------------------------------------------------------------===//
// MIPS DSP Rev 2
// Addition/subtraction
class ADDU_PH_DESC : ADDU_QB_DESC_BASE<"addu.ph", int_mips_addu_ph, NoItinerary,
                                       DSPROpnd, DSPROpnd>, IsCommutable,
                     Defs<[DSPOutFlag20]>;

class ADDU_S_PH_DESC : ADDU_QB_DESC_BASE<"addu_s.ph", int_mips_addu_s_ph,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       IsCommutable, Defs<[DSPOutFlag20]>;

class SUBU_PH_DESC : ADDU_QB_DESC_BASE<"subu.ph", int_mips_subu_ph, NoItinerary,
                                       DSPROpnd, DSPROpnd>,
                     Defs<[DSPOutFlag20]>;

class SUBU_S_PH_DESC : ADDU_QB_DESC_BASE<"subu_s.ph", int_mips_subu_s_ph,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       Defs<[DSPOutFlag20]>;

class ADDUH_QB_DESC : ADDUH_QB_DESC_BASE<"adduh.qb", int_mips_adduh_qb,
                                         NoItinerary, DSPROpnd>, IsCommutable;

class ADDUH_R_QB_DESC : ADDUH_QB_DESC_BASE<"adduh_r.qb", int_mips_adduh_r_qb,
                                           NoItinerary, DSPROpnd>, IsCommutable;

class SUBUH_QB_DESC : ADDUH_QB_DESC_BASE<"subuh.qb", int_mips_subuh_qb,
                                         NoItinerary, DSPROpnd>;

class SUBUH_R_QB_DESC : ADDUH_QB_DESC_BASE<"subuh_r.qb", int_mips_subuh_r_qb,
                                           NoItinerary, DSPROpnd>;

class ADDQH_PH_DESC : ADDUH_QB_DESC_BASE<"addqh.ph", int_mips_addqh_ph,
                                         NoItinerary, DSPROpnd>, IsCommutable;

class ADDQH_R_PH_DESC : ADDUH_QB_DESC_BASE<"addqh_r.ph", int_mips_addqh_r_ph,
                                           NoItinerary, DSPROpnd>, IsCommutable;

class SUBQH_PH_DESC : ADDUH_QB_DESC_BASE<"subqh.ph", int_mips_subqh_ph,
                                         NoItinerary, DSPROpnd>;

class SUBQH_R_PH_DESC : ADDUH_QB_DESC_BASE<"subqh_r.ph", int_mips_subqh_r_ph,
                                           NoItinerary, DSPROpnd>;

class ADDQH_W_DESC : ADDUH_QB_DESC_BASE<"addqh.w", int_mips_addqh_w,
                                        NoItinerary, GPR32Opnd>, IsCommutable;

class ADDQH_R_W_DESC : ADDUH_QB_DESC_BASE<"addqh_r.w", int_mips_addqh_r_w,
                                          NoItinerary, GPR32Opnd>, IsCommutable;

class SUBQH_W_DESC : ADDUH_QB_DESC_BASE<"subqh.w", int_mips_subqh_w,
                                        NoItinerary, GPR32Opnd>;

class SUBQH_R_W_DESC : ADDUH_QB_DESC_BASE<"subqh_r.w", int_mips_subqh_r_w,
                                          NoItinerary, GPR32Opnd>;

// Comparison
class CMPGDU_EQ_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgdu.eq.qb",
                                                 int_mips_cmpgdu_eq_qb,
                                                 NoItinerary, GPR32Opnd, DSPROpnd>,
                          IsCommutable, Defs<[DSPCCond]>;

class CMPGDU_LT_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgdu.lt.qb",
                                                 int_mips_cmpgdu_lt_qb,
                                                 NoItinerary, GPR32Opnd, DSPROpnd>,
                          Defs<[DSPCCond]>;

class CMPGDU_LE_QB_DESC : CMP_EQ_QB_R3_DESC_BASE<"cmpgdu.le.qb",
                                                 int_mips_cmpgdu_le_qb,
                                                 NoItinerary, GPR32Opnd, DSPROpnd>,
                          Defs<[DSPCCond]>;

// Absolute
class ABSQ_S_QB_DESC : ABSQ_S_PH_R2_DESC_BASE<"absq_s.qb", int_mips_absq_s_qb,
                                              NoItinerary, DSPROpnd>,
                       Defs<[DSPOutFlag20]>;

// Multiplication
class MUL_PH_DESC : ADDUH_QB_DESC_BASE<"mul.ph", null_frag, NoItinerary,
                                       DSPROpnd>, IsCommutable,
                    Defs<[DSPOutFlag21]>;

class MUL_S_PH_DESC : ADDUH_QB_DESC_BASE<"mul_s.ph", int_mips_mul_s_ph,
                                         NoItinerary, DSPROpnd>, IsCommutable,
                      Defs<[DSPOutFlag21]>;

class MULQ_S_W_DESC : ADDUH_QB_DESC_BASE<"mulq_s.w", int_mips_mulq_s_w,
                                         NoItinerary, GPR32Opnd>, IsCommutable,
                      Defs<[DSPOutFlag21]>;

class MULQ_RS_W_DESC : ADDUH_QB_DESC_BASE<"mulq_rs.w", int_mips_mulq_rs_w,
                                          NoItinerary, GPR32Opnd>, IsCommutable,
                       Defs<[DSPOutFlag21]>;

class MULQ_S_PH_DESC : ADDU_QB_DESC_BASE<"mulq_s.ph", int_mips_mulq_s_ph,
                                         NoItinerary, DSPROpnd, DSPROpnd>,
                       IsCommutable, Defs<[DSPOutFlag21]>;

// Dot product with accumulate/subtract
class DPA_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpa.w.ph", MipsDPA_W_PH>;

class DPS_W_PH_DESC : DPA_W_PH_DESC_BASE<"dps.w.ph", MipsDPS_W_PH>;

class DPAQX_S_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpaqx_s.w.ph", MipsDPAQX_S_W_PH>,
                          Defs<[DSPOutFlag16_19]>;

class DPAQX_SA_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpaqx_sa.w.ph",
                                              MipsDPAQX_SA_W_PH>,
                           Defs<[DSPOutFlag16_19]>;

class DPAX_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpax.w.ph", MipsDPAX_W_PH>;

class DPSX_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpsx.w.ph", MipsDPSX_W_PH>;

class DPSQX_S_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpsqx_s.w.ph", MipsDPSQX_S_W_PH>,
                          Defs<[DSPOutFlag16_19]>;

class DPSQX_SA_W_PH_DESC : DPA_W_PH_DESC_BASE<"dpsqx_sa.w.ph",
                                              MipsDPSQX_SA_W_PH>,
                           Defs<[DSPOutFlag16_19]>;

class MULSA_W_PH_DESC : DPA_W_PH_DESC_BASE<"mulsa.w.ph", MipsMULSA_W_PH>;

// Precision reduce/expand
class PRECR_QB_PH_DESC : CMP_EQ_QB_R3_DESC_BASE<"precr.qb.ph",
                                                int_mips_precr_qb_ph,
                                                NoItinerary, DSPROpnd, DSPROpnd>;

class PRECR_SRA_PH_W_DESC : PRECR_SRA_PH_W_DESC_BASE<"precr_sra.ph.w",
                                                     int_mips_precr_sra_ph_w,
                                                     NoItinerary, DSPROpnd,
                                                     GPR32Opnd>;

class PRECR_SRA_R_PH_W_DESC : PRECR_SRA_PH_W_DESC_BASE<"precr_sra_r.ph.w",
                                                      int_mips_precr_sra_r_ph_w,
                                                       NoItinerary, DSPROpnd,
                                                       GPR32Opnd>;

// Shift
class SHRA_QB_DESC : SHLL_QB_R2_DESC_BASE<"shra.qb", null_frag, immZExt3,
                                          NoItinerary, DSPROpnd, uimm3>;

class SHRAV_QB_DESC : SHLL_QB_R3_DESC_BASE<"shrav.qb", int_mips_shra_qb,
                                           NoItinerary, DSPROpnd>;

class SHRA_R_QB_DESC : SHLL_QB_R2_DESC_BASE<"shra_r.qb", int_mips_shra_r_qb,
                                            immZExt3, NoItinerary, DSPROpnd,
                                            uimm3>;

class SHRAV_R_QB_DESC : SHLL_QB_R3_DESC_BASE<"shrav_r.qb", int_mips_shra_r_qb,
                                             NoItinerary, DSPROpnd>;

class SHRL_PH_DESC : SHLL_QB_R2_DESC_BASE<"shrl.ph", null_frag, immZExt4,
                                          NoItinerary, DSPROpnd, uimm4>;

class SHRLV_PH_DESC : SHLL_QB_R3_DESC_BASE<"shrlv.ph", int_mips_shrl_ph,
                                           NoItinerary, DSPROpnd>;

// Misc
class APPEND_DESC : APPEND_DESC_BASE<"append", int_mips_append, uimm5, immZExt5,
                                     NoItinerary>;

class BALIGN_DESC : APPEND_DESC_BASE<"balign", int_mips_balign, uimm2, immZExt2,
                                     NoItinerary>;

class PREPEND_DESC : APPEND_DESC_BASE<"prepend", int_mips_prepend, uimm5,
                                      immZExt5, NoItinerary>;

// Pseudos.
def BPOSGE32_PSEUDO : BPOSGE32_PSEUDO_DESC_BASE<int_mips_bposge32,
                                                NoItinerary>, Uses<[DSPPos]>;

// Instruction defs.
// MIPS DSP Rev 1
def ADDU_QB : DspMMRel, ADDU_QB_ENC, ADDU_QB_DESC;
def ADDU_S_QB : DspMMRel, ADDU_S_QB_ENC, ADDU_S_QB_DESC;
def SUBU_QB : DspMMRel, SUBU_QB_ENC, SUBU_QB_DESC;
def SUBU_S_QB : DspMMRel, SUBU_S_QB_ENC, SUBU_S_QB_DESC;
def ADDQ_PH : DspMMRel, ADDQ_PH_ENC, ADDQ_PH_DESC;
def ADDQ_S_PH : DspMMRel, ADDQ_S_PH_ENC, ADDQ_S_PH_DESC;
def SUBQ_PH : DspMMRel, SUBQ_PH_ENC, SUBQ_PH_DESC;
def SUBQ_S_PH : DspMMRel, SUBQ_S_PH_ENC, SUBQ_S_PH_DESC;
def ADDQ_S_W : DspMMRel, ADDQ_S_W_ENC, ADDQ_S_W_DESC;
def SUBQ_S_W : DspMMRel, SUBQ_S_W_ENC, SUBQ_S_W_DESC;
def ADDSC : DspMMRel, ADDSC_ENC, ADDSC_DESC;
def ADDWC : DspMMRel, ADDWC_ENC, ADDWC_DESC;
def MODSUB : DspMMRel, MODSUB_ENC, MODSUB_DESC;
def RADDU_W_QB : DspMMRel, RADDU_W_QB_ENC, RADDU_W_QB_DESC;
def ABSQ_S_PH : DspMMRel, ABSQ_S_PH_ENC, ABSQ_S_PH_DESC;
def ABSQ_S_W : DspMMRel, ABSQ_S_W_ENC, ABSQ_S_W_DESC;
def PRECRQ_QB_PH : DspMMRel, PRECRQ_QB_PH_ENC, PRECRQ_QB_PH_DESC;
def PRECRQ_PH_W : DspMMRel, PRECRQ_PH_W_ENC, PRECRQ_PH_W_DESC;
def PRECRQ_RS_PH_W : DspMMRel, PRECRQ_RS_PH_W_ENC, PRECRQ_RS_PH_W_DESC;
def PRECRQU_S_QB_PH : DspMMRel, PRECRQU_S_QB_PH_ENC, PRECRQU_S_QB_PH_DESC;
def PRECEQ_W_PHL : DspMMRel, PRECEQ_W_PHL_ENC, PRECEQ_W_PHL_DESC;
def PRECEQ_W_PHR : DspMMRel, PRECEQ_W_PHR_ENC, PRECEQ_W_PHR_DESC;
def PRECEQU_PH_QBL : DspMMRel, PRECEQU_PH_QBL_ENC, PRECEQU_PH_QBL_DESC;
def PRECEQU_PH_QBR : DspMMRel, PRECEQU_PH_QBR_ENC, PRECEQU_PH_QBR_DESC;
def PRECEQU_PH_QBLA : DspMMRel, PRECEQU_PH_QBLA_ENC, PRECEQU_PH_QBLA_DESC;
def PRECEQU_PH_QBRA : DspMMRel, PRECEQU_PH_QBRA_ENC, PRECEQU_PH_QBRA_DESC;
def PRECEU_PH_QBL : DspMMRel, PRECEU_PH_QBL_ENC, PRECEU_PH_QBL_DESC;
def PRECEU_PH_QBR : DspMMRel, PRECEU_PH_QBR_ENC, PRECEU_PH_QBR_DESC;
def PRECEU_PH_QBLA : DspMMRel, PRECEU_PH_QBLA_ENC, PRECEU_PH_QBLA_DESC;
def PRECEU_PH_QBRA : DspMMRel, PRECEU_PH_QBRA_ENC, PRECEU_PH_QBRA_DESC;
def SHLL_QB : DspMMRel, SHLL_QB_ENC, SHLL_QB_DESC;
def SHLLV_QB : DspMMRel, SHLLV_QB_ENC, SHLLV_QB_DESC;
def SHRL_QB : DspMMRel, SHRL_QB_ENC, SHRL_QB_DESC;
def SHRLV_QB : DspMMRel, SHRLV_QB_ENC, SHRLV_QB_DESC;
def SHLL_PH : DspMMRel, SHLL_PH_ENC, SHLL_PH_DESC;
def SHLLV_PH : DspMMRel, SHLLV_PH_ENC, SHLLV_PH_DESC;
def SHLL_S_PH : DspMMRel, SHLL_S_PH_ENC, SHLL_S_PH_DESC;
def SHLLV_S_PH : DspMMRel, SHLLV_S_PH_ENC, SHLLV_S_PH_DESC;
def SHRA_PH : DspMMRel, SHRA_PH_ENC, SHRA_PH_DESC;
def SHRAV_PH : DspMMRel, SHRAV_PH_ENC, SHRAV_PH_DESC;
def SHRA_R_PH : DspMMRel, SHRA_R_PH_ENC, SHRA_R_PH_DESC;
def SHRAV_R_PH : DspMMRel, SHRAV_R_PH_ENC, SHRAV_R_PH_DESC;
def SHLL_S_W : DspMMRel, SHLL_S_W_ENC, SHLL_S_W_DESC;
def SHLLV_S_W : DspMMRel, SHLLV_S_W_ENC, SHLLV_S_W_DESC;
def SHRA_R_W : DspMMRel, SHRA_R_W_ENC, SHRA_R_W_DESC;
def SHRAV_R_W : DspMMRel, SHRAV_R_W_ENC, SHRAV_R_W_DESC;
def MULEU_S_PH_QBL : DspMMRel, MULEU_S_PH_QBL_ENC, MULEU_S_PH_QBL_DESC;
def MULEU_S_PH_QBR : DspMMRel, MULEU_S_PH_QBR_ENC, MULEU_S_PH_QBR_DESC;
def MULEQ_S_W_PHL : DspMMRel, MULEQ_S_W_PHL_ENC, MULEQ_S_W_PHL_DESC;
def MULEQ_S_W_PHR : DspMMRel, MULEQ_S_W_PHR_ENC, MULEQ_S_W_PHR_DESC;
def MULQ_RS_PH : DspMMRel, MULQ_RS_PH_ENC, MULQ_RS_PH_DESC;
def MULSAQ_S_W_PH : DspMMRel, MULSAQ_S_W_PH_ENC, MULSAQ_S_W_PH_DESC;
def MAQ_S_W_PHL : DspMMRel, MAQ_S_W_PHL_ENC, MAQ_S_W_PHL_DESC;
def MAQ_S_W_PHR : DspMMRel, MAQ_S_W_PHR_ENC, MAQ_S_W_PHR_DESC;
def MAQ_SA_W_PHL : DspMMRel, MAQ_SA_W_PHL_ENC, MAQ_SA_W_PHL_DESC;
def MAQ_SA_W_PHR : DspMMRel, MAQ_SA_W_PHR_ENC, MAQ_SA_W_PHR_DESC;
def MFHI_DSP : DspMMRel, MFHI_ENC, MFHI_DESC;
def MFLO_DSP : DspMMRel, MFLO_ENC, MFLO_DESC;
def MTHI_DSP : DspMMRel, MTHI_ENC, MTHI_DESC;
def MTLO_DSP : DspMMRel, MTLO_ENC, MTLO_DESC;
def DPAU_H_QBL : DspMMRel, DPAU_H_QBL_ENC, DPAU_H_QBL_DESC;
def DPAU_H_QBR : DspMMRel, DPAU_H_QBR_ENC, DPAU_H_QBR_DESC;
def DPSU_H_QBL : DspMMRel, DPSU_H_QBL_ENC, DPSU_H_QBL_DESC;
def DPSU_H_QBR : DspMMRel, DPSU_H_QBR_ENC, DPSU_H_QBR_DESC;
def DPAQ_S_W_PH : DspMMRel, DPAQ_S_W_PH_ENC, DPAQ_S_W_PH_DESC;
def DPSQ_S_W_PH : DspMMRel, DPSQ_S_W_PH_ENC, DPSQ_S_W_PH_DESC;
def DPAQ_SA_L_W : DspMMRel, DPAQ_SA_L_W_ENC, DPAQ_SA_L_W_DESC;
def DPSQ_SA_L_W : DspMMRel, DPSQ_SA_L_W_ENC, DPSQ_SA_L_W_DESC;
def MULT_DSP : DspMMRel, MULT_DSP_ENC, MULT_DSP_DESC;
def MULTU_DSP : DspMMRel, MULTU_DSP_ENC, MULTU_DSP_DESC;
def MADD_DSP : DspMMRel, MADD_DSP_ENC, MADD_DSP_DESC;
def MADDU_DSP : DspMMRel, MADDU_DSP_ENC, MADDU_DSP_DESC;
def MSUB_DSP : DspMMRel, MSUB_DSP_ENC, MSUB_DSP_DESC;
def MSUBU_DSP : DspMMRel, MSUBU_DSP_ENC, MSUBU_DSP_DESC;
def CMPU_EQ_QB : DspMMRel, CMPU_EQ_QB_ENC, CMPU_EQ_QB_DESC;
def CMPU_LT_QB : DspMMRel, CMPU_LT_QB_ENC, CMPU_LT_QB_DESC;
def CMPU_LE_QB : DspMMRel, CMPU_LE_QB_ENC, CMPU_LE_QB_DESC;
def CMPGU_EQ_QB : DspMMRel, CMPGU_EQ_QB_ENC, CMPGU_EQ_QB_DESC;
def CMPGU_LT_QB : DspMMRel, CMPGU_LT_QB_ENC, CMPGU_LT_QB_DESC;
def CMPGU_LE_QB : DspMMRel, CMPGU_LE_QB_ENC, CMPGU_LE_QB_DESC;
def CMP_EQ_PH : DspMMRel, CMP_EQ_PH_ENC, CMP_EQ_PH_DESC;
def CMP_LT_PH : DspMMRel, CMP_LT_PH_ENC, CMP_LT_PH_DESC;
def CMP_LE_PH : DspMMRel, CMP_LE_PH_ENC, CMP_LE_PH_DESC;
def BITREV : DspMMRel, BITREV_ENC, BITREV_DESC;
def PACKRL_PH : DspMMRel, PACKRL_PH_ENC, PACKRL_PH_DESC;
def REPL_QB : DspMMRel, REPL_QB_ENC, REPL_QB_DESC;
def REPL_PH : DspMMRel, REPL_PH_ENC, REPL_PH_DESC;
def REPLV_QB : DspMMRel, REPLV_QB_ENC, REPLV_QB_DESC;
def REPLV_PH : DspMMRel, REPLV_PH_ENC, REPLV_PH_DESC;
def PICK_QB : DspMMRel, PICK_QB_ENC, PICK_QB_DESC;
def PICK_PH : DspMMRel, PICK_PH_ENC, PICK_PH_DESC;
def LWX : DspMMRel, LWX_ENC, LWX_DESC;
def LHX : DspMMRel, LHX_ENC, LHX_DESC;
def LBUX : DspMMRel, LBUX_ENC, LBUX_DESC;
let AdditionalPredicates = [NotInMicroMips] in {
  def BPOSGE32 : DspMMRel, BPOSGE32_ENC, BPOSGE32_DESC;
}
def INSV : DspMMRel, INSV_ENC, INSV_DESC;
def EXTP : DspMMRel, EXTP_ENC, EXTP_DESC;
def EXTPV : DspMMRel, EXTPV_ENC, EXTPV_DESC;
def EXTPDP : DspMMRel, EXTPDP_ENC, EXTPDP_DESC;
def EXTPDPV : DspMMRel, EXTPDPV_ENC, EXTPDPV_DESC;
def EXTR_W : DspMMRel, EXTR_W_ENC, EXTR_W_DESC;
def EXTRV_W : DspMMRel, EXTRV_W_ENC, EXTRV_W_DESC;
def EXTR_R_W : DspMMRel, EXTR_R_W_ENC, EXTR_R_W_DESC;
def EXTRV_R_W : DspMMRel, EXTRV_R_W_ENC, EXTRV_R_W_DESC;
def EXTR_RS_W : DspMMRel, EXTR_RS_W_ENC, EXTR_RS_W_DESC;
def EXTRV_RS_W : DspMMRel, EXTRV_RS_W_ENC, EXTRV_RS_W_DESC;
def EXTR_S_H : DspMMRel, EXTR_S_H_ENC, EXTR_S_H_DESC;
def EXTRV_S_H : DspMMRel, EXTRV_S_H_ENC, EXTRV_S_H_DESC;
def SHILO : DspMMRel, SHILO_ENC, SHILO_DESC;
def SHILOV : DspMMRel, SHILOV_ENC, SHILOV_DESC;
def MTHLIP : DspMMRel, MTHLIP_ENC, MTHLIP_DESC;
def RDDSP : DspMMRel, RDDSP_ENC, RDDSP_DESC;
let AdditionalPredicates = [NotInMicroMips] in {
  def WRDSP : WRDSP_ENC, WRDSP_DESC;
}

// MIPS DSP Rev 2
def ADDU_PH : DspMMRel, ADDU_PH_ENC, ADDU_PH_DESC, ISA_DSPR2;
def ADDU_S_PH : DspMMRel, ADDU_S_PH_ENC, ADDU_S_PH_DESC, ISA_DSPR2;
def SUBU_PH : DspMMRel, SUBU_PH_ENC, SUBU_PH_DESC, ISA_DSPR2;
def SUBU_S_PH : DspMMRel, SUBU_S_PH_ENC, SUBU_S_PH_DESC, ISA_DSPR2;
def CMPGDU_EQ_QB : DspMMRel, CMPGDU_EQ_QB_ENC, CMPGDU_EQ_QB_DESC, ISA_DSPR2;
def CMPGDU_LT_QB : DspMMRel, CMPGDU_LT_QB_ENC, CMPGDU_LT_QB_DESC, ISA_DSPR2;
def CMPGDU_LE_QB : DspMMRel, CMPGDU_LE_QB_ENC, CMPGDU_LE_QB_DESC, ISA_DSPR2;
def ABSQ_S_QB : DspMMRel, ABSQ_S_QB_ENC, ABSQ_S_QB_DESC, ISA_DSPR2;
def ADDUH_QB : DspMMRel, ADDUH_QB_ENC, ADDUH_QB_DESC, ISA_DSPR2;
def ADDUH_R_QB : DspMMRel, ADDUH_R_QB_ENC, ADDUH_R_QB_DESC, ISA_DSPR2;
def SUBUH_QB : DspMMRel, SUBUH_QB_ENC, SUBUH_QB_DESC, ISA_DSPR2;
def SUBUH_R_QB : DspMMRel, SUBUH_R_QB_ENC, SUBUH_R_QB_DESC, ISA_DSPR2;
def ADDQH_PH : DspMMRel, ADDQH_PH_ENC, ADDQH_PH_DESC, ISA_DSPR2;
def ADDQH_R_PH : DspMMRel, ADDQH_R_PH_ENC, ADDQH_R_PH_DESC, ISA_DSPR2;
def SUBQH_PH : DspMMRel, SUBQH_PH_ENC, SUBQH_PH_DESC, ISA_DSPR2;
def SUBQH_R_PH : DspMMRel, SUBQH_R_PH_ENC, SUBQH_R_PH_DESC, ISA_DSPR2;
def ADDQH_W : DspMMRel, ADDQH_W_ENC, ADDQH_W_DESC, ISA_DSPR2;
def ADDQH_R_W : DspMMRel, ADDQH_R_W_ENC, ADDQH_R_W_DESC, ISA_DSPR2;
def SUBQH_W : DspMMRel, SUBQH_W_ENC, SUBQH_W_DESC, ISA_DSPR2;
def SUBQH_R_W : DspMMRel, SUBQH_R_W_ENC, SUBQH_R_W_DESC, ISA_DSPR2;
def MUL_PH : DspMMRel, MUL_PH_ENC, MUL_PH_DESC, ISA_DSPR2;
def MUL_S_PH : DspMMRel, MUL_S_PH_ENC, MUL_S_PH_DESC, ISA_DSPR2;
def MULQ_S_W : DspMMRel, MULQ_S_W_ENC, MULQ_S_W_DESC, ISA_DSPR2;
def MULQ_RS_W : DspMMRel, MULQ_RS_W_ENC, MULQ_RS_W_DESC, ISA_DSPR2;
def MULQ_S_PH : DspMMRel, MULQ_S_PH_ENC, MULQ_S_PH_DESC, ISA_DSPR2;
def DPA_W_PH : DspMMRel, DPA_W_PH_ENC, DPA_W_PH_DESC, ISA_DSPR2;
def DPS_W_PH : DspMMRel, DPS_W_PH_ENC, DPS_W_PH_DESC, ISA_DSPR2;
def DPAQX_S_W_PH : DspMMRel, DPAQX_S_W_PH_ENC, DPAQX_S_W_PH_DESC, ISA_DSPR2;
def DPAQX_SA_W_PH : DspMMRel, DPAQX_SA_W_PH_ENC, DPAQX_SA_W_PH_DESC, ISA_DSPR2;
def DPAX_W_PH : DspMMRel, DPAX_W_PH_ENC, DPAX_W_PH_DESC, ISA_DSPR2;
def DPSX_W_PH : DspMMRel, DPSX_W_PH_ENC, DPSX_W_PH_DESC, ISA_DSPR2;
def DPSQX_S_W_PH : DspMMRel, DPSQX_S_W_PH_ENC, DPSQX_S_W_PH_DESC, ISA_DSPR2;
def DPSQX_SA_W_PH : DspMMRel, DPSQX_SA_W_PH_ENC, DPSQX_SA_W_PH_DESC, ISA_DSPR2;
def MULSA_W_PH : DspMMRel, MULSA_W_PH_ENC, MULSA_W_PH_DESC, ISA_DSPR2;
def PRECR_QB_PH : DspMMRel, PRECR_QB_PH_ENC, PRECR_QB_PH_DESC, ISA_DSPR2;
def PRECR_SRA_PH_W : DspMMRel, PRECR_SRA_PH_W_ENC, PRECR_SRA_PH_W_DESC, ISA_DSPR2;
def PRECR_SRA_R_PH_W : DspMMRel, PRECR_SRA_R_PH_W_ENC, PRECR_SRA_R_PH_W_DESC, ISA_DSPR2;
def SHRA_QB : DspMMRel, SHRA_QB_ENC, SHRA_QB_DESC, ISA_DSPR2;
def SHRAV_QB : DspMMRel, SHRAV_QB_ENC, SHRAV_QB_DESC, ISA_DSPR2;
def SHRA_R_QB : DspMMRel, SHRA_R_QB_ENC, SHRA_R_QB_DESC, ISA_DSPR2;
def SHRAV_R_QB : DspMMRel, SHRAV_R_QB_ENC, SHRAV_R_QB_DESC, ISA_DSPR2;
def SHRL_PH : DspMMRel, SHRL_PH_ENC, SHRL_PH_DESC, ISA_DSPR2;
def SHRLV_PH : DspMMRel, SHRLV_PH_ENC, SHRLV_PH_DESC, ISA_DSPR2;
def APPEND : DspMMRel, APPEND_ENC, APPEND_DESC, ISA_DSPR2;
def BALIGN : DspMMRel, BALIGN_ENC, BALIGN_DESC, ISA_DSPR2;
def PREPEND : DspMMRel, PREPEND_ENC, PREPEND_DESC, ISA_DSPR2;

// Pseudos.
let isPseudo = 1, isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in {
  // Pseudo instructions for loading and storing accumulator registers.
  def LOAD_ACC64DSP  : Load<"", ACC64DSPOpnd>;
  def STORE_ACC64DSP : Store<"", ACC64DSPOpnd>;

  // Pseudos for loading and storing ccond field of DSP control register.
  def LOAD_CCOND_DSP  : Load<"load_ccond_dsp", DSPCC>;
  def STORE_CCOND_DSP : Store<"store_ccond_dsp", DSPCC>;
}

let DecoderNamespace = "MipsDSP", Arch = "dsp",
    ASEPredicate = [HasDSP] in {
  def LWDSP : Load<"lw", DSPROpnd, null_frag, II_LW>, DspMMRel, LW_FM<0x23>;
  def SWDSP : Store<"sw", DSPROpnd, null_frag, II_SW>, DspMMRel, LW_FM<0x2b>;
}

// Pseudo CMP and PICK instructions.
class PseudoCMP<Instruction RealInst> :
  PseudoDSP<(outs DSPCC:$cmp), (ins DSPROpnd:$rs, DSPROpnd:$rt), []>,
  PseudoInstExpansion<(RealInst DSPROpnd:$rs, DSPROpnd:$rt)>, NeverHasSideEffects;

class PseudoPICK<Instruction RealInst> :
  PseudoDSP<(outs DSPROpnd:$rd), (ins DSPCC:$cmp, DSPROpnd:$rs, DSPROpnd:$rt), []>,
  PseudoInstExpansion<(RealInst DSPROpnd:$rd, DSPROpnd:$rs, DSPROpnd:$rt)>,
  NeverHasSideEffects;

def PseudoCMP_EQ_PH : PseudoCMP<CMP_EQ_PH>;
def PseudoCMP_LT_PH : PseudoCMP<CMP_LT_PH>;
def PseudoCMP_LE_PH : PseudoCMP<CMP_LE_PH>;
def PseudoCMPU_EQ_QB : PseudoCMP<CMPU_EQ_QB>;
def PseudoCMPU_LT_QB : PseudoCMP<CMPU_LT_QB>;
def PseudoCMPU_LE_QB : PseudoCMP<CMPU_LE_QB>;

def PseudoPICK_PH : PseudoPICK<PICK_PH>;
def PseudoPICK_QB : PseudoPICK<PICK_QB>;

def PseudoMTLOHI_DSP : PseudoMTLOHI<ACC64DSP, GPR32>;

// Patterns.
class DSPPat<dag pattern, dag result, Predicate pred = HasDSP> :
  Pat<pattern, result>, Requires<[pred]>;

class BitconvertPat<ValueType DstVT, ValueType SrcVT, RegisterClass DstRC,
                    RegisterClass SrcRC> :
   DSPPat<(DstVT (bitconvert (SrcVT SrcRC:$src))),
          (COPY_TO_REGCLASS SrcRC:$src, DstRC)>;

def : BitconvertPat<i32, v2i16, GPR32, DSPR>;
def : BitconvertPat<i32, v4i8, GPR32, DSPR>;
def : BitconvertPat<v2i16, i32, DSPR, GPR32>;
def : BitconvertPat<v4i8, i32, DSPR, GPR32>;
def : BitconvertPat<f32, v2i16, FGR32, DSPR>;
def : BitconvertPat<f32, v4i8, FGR32, DSPR>;
def : BitconvertPat<v2i16, f32, DSPR, FGR32>;
def : BitconvertPat<v4i8, f32, DSPR, FGR32>;

def : DSPPat<(v2i16 (load addr:$a)),
             (v2i16 (COPY_TO_REGCLASS (LW addr:$a), DSPR))>;
def : DSPPat<(v4i8 (load addr:$a)),
             (v4i8 (COPY_TO_REGCLASS (LW addr:$a), DSPR))>;
def : DSPPat<(store (v2i16 DSPR:$val), addr:$a),
             (SW (COPY_TO_REGCLASS DSPR:$val, GPR32), addr:$a)>;
def : DSPPat<(store (v4i8 DSPR:$val), addr:$a),
             (SW (COPY_TO_REGCLASS DSPR:$val, GPR32), addr:$a)>;

// Binary operations.
class DSPBinPat<Instruction Inst, ValueType ValTy, SDPatternOperator Node,
                Predicate Pred = HasDSP> :
  DSPPat<(Node ValTy:$a, ValTy:$b), (Inst ValTy:$a, ValTy:$b), Pred>;

def : DSPBinPat<ADDQ_PH, v2i16, int_mips_addq_ph>;
def : DSPBinPat<ADDQ_PH, v2i16, add>;
def : DSPBinPat<SUBQ_PH, v2i16, int_mips_subq_ph>;
def : DSPBinPat<SUBQ_PH, v2i16, sub>;
def : DSPBinPat<MUL_PH, v2i16, int_mips_mul_ph, HasDSPR2>;
def : DSPBinPat<MUL_PH, v2i16, mul, HasDSPR2>;
def : DSPBinPat<ADDU_QB, v4i8, int_mips_addu_qb>;
def : DSPBinPat<ADDU_QB, v4i8, add>;
def : DSPBinPat<SUBU_QB, v4i8, int_mips_subu_qb>;
def : DSPBinPat<SUBU_QB, v4i8, sub>;
def : DSPBinPat<ADDSC, i32, int_mips_addsc>;
def : DSPBinPat<ADDSC, i32, addc>;
def : DSPBinPat<ADDWC, i32, int_mips_addwc>;
def : DSPBinPat<ADDWC, i32, adde>;

// Shift immediate patterns.
class DSPShiftPat<Instruction Inst, ValueType ValTy, SDPatternOperator Node,
                  SDPatternOperator Imm, Predicate Pred = HasDSP> :
  DSPPat<(Node ValTy:$a, Imm:$shamt), (Inst ValTy:$a, Imm:$shamt), Pred>;

def : DSPShiftPat<SHLL_PH, v2i16, MipsSHLL_DSP, imm>;
def : DSPShiftPat<SHRA_PH, v2i16, MipsSHRA_DSP, imm>;
def : DSPShiftPat<SHRL_PH, v2i16, MipsSHRL_DSP, imm, HasDSPR2>;
def : DSPShiftPat<SHLL_PH, v2i16, int_mips_shll_ph, immZExt4>;
def : DSPShiftPat<SHRA_PH, v2i16, int_mips_shra_ph, immZExt4>;
def : DSPShiftPat<SHRL_PH, v2i16, int_mips_shrl_ph, immZExt4, HasDSPR2>;
def : DSPShiftPat<SHLL_QB, v4i8, MipsSHLL_DSP, imm>;
def : DSPShiftPat<SHRA_QB, v4i8, MipsSHRA_DSP, imm, HasDSPR2>;
def : DSPShiftPat<SHRL_QB, v4i8, MipsSHRL_DSP, imm>;
def : DSPShiftPat<SHLL_QB, v4i8, int_mips_shll_qb, immZExt3>;
def : DSPShiftPat<SHRA_QB, v4i8, int_mips_shra_qb, immZExt3, HasDSPR2>;
def : DSPShiftPat<SHRL_QB, v4i8, int_mips_shrl_qb, immZExt3>;

// SETCC/SELECT_CC patterns.
class DSPSetCCPat<Instruction Cmp, Instruction Pick, ValueType ValTy,
                  CondCode CC> :
  DSPPat<(ValTy (MipsSETCC_DSP ValTy:$a, ValTy:$b, CC)),
         (ValTy (Pick (ValTy (Cmp ValTy:$a, ValTy:$b)),
                      (ValTy (COPY_TO_REGCLASS (ADDiu ZERO, -1), DSPR)),
                      (ValTy ZERO)))>;

class DSPSetCCPatInv<Instruction Cmp, Instruction Pick, ValueType ValTy,
                     CondCode CC> :
  DSPPat<(ValTy (MipsSETCC_DSP ValTy:$a, ValTy:$b, CC)),
         (ValTy (Pick (ValTy (Cmp ValTy:$a, ValTy:$b)),
                      (ValTy ZERO),
                      (ValTy (COPY_TO_REGCLASS (ADDiu ZERO, -1), DSPR))))>;

class DSPSelectCCPat<Instruction Cmp, Instruction Pick, ValueType ValTy,
                     CondCode CC> :
  DSPPat<(ValTy (MipsSELECT_CC_DSP ValTy:$a, ValTy:$b, ValTy:$c, ValTy:$d, CC)),
         (ValTy (Pick (ValTy (Cmp ValTy:$a, ValTy:$b)), $c, $d))>;

class DSPSelectCCPatInv<Instruction Cmp, Instruction Pick, ValueType ValTy,
                        CondCode CC> :
  DSPPat<(ValTy (MipsSELECT_CC_DSP ValTy:$a, ValTy:$b, ValTy:$c, ValTy:$d, CC)),
         (ValTy (Pick (ValTy (Cmp ValTy:$a, ValTy:$b)), $d, $c))>;

def : DSPSetCCPat<PseudoCMP_EQ_PH, PseudoPICK_PH, v2i16, SETEQ>;
def : DSPSetCCPat<PseudoCMP_LT_PH, PseudoPICK_PH, v2i16, SETLT>;
def : DSPSetCCPat<PseudoCMP_LE_PH, PseudoPICK_PH, v2i16, SETLE>;
def : DSPSetCCPatInv<PseudoCMP_EQ_PH, PseudoPICK_PH, v2i16, SETNE>;
def : DSPSetCCPatInv<PseudoCMP_LT_PH, PseudoPICK_PH, v2i16, SETGE>;
def : DSPSetCCPatInv<PseudoCMP_LE_PH, PseudoPICK_PH, v2i16, SETGT>;
def : DSPSetCCPat<PseudoCMPU_EQ_QB, PseudoPICK_QB, v4i8, SETEQ>;
def : DSPSetCCPat<PseudoCMPU_LT_QB, PseudoPICK_QB, v4i8, SETULT>;
def : DSPSetCCPat<PseudoCMPU_LE_QB, PseudoPICK_QB, v4i8, SETULE>;
def : DSPSetCCPatInv<PseudoCMPU_EQ_QB, PseudoPICK_QB, v4i8, SETNE>;
def : DSPSetCCPatInv<PseudoCMPU_LT_QB, PseudoPICK_QB, v4i8, SETUGE>;
def : DSPSetCCPatInv<PseudoCMPU_LE_QB, PseudoPICK_QB, v4i8, SETUGT>;

def : DSPSelectCCPat<PseudoCMP_EQ_PH, PseudoPICK_PH, v2i16, SETEQ>;
def : DSPSelectCCPat<PseudoCMP_LT_PH, PseudoPICK_PH, v2i16, SETLT>;
def : DSPSelectCCPat<PseudoCMP_LE_PH, PseudoPICK_PH, v2i16, SETLE>;
def : DSPSelectCCPatInv<PseudoCMP_EQ_PH, PseudoPICK_PH, v2i16, SETNE>;
def : DSPSelectCCPatInv<PseudoCMP_LT_PH, PseudoPICK_PH, v2i16, SETGE>;
def : DSPSelectCCPatInv<PseudoCMP_LE_PH, PseudoPICK_PH, v2i16, SETGT>;
def : DSPSelectCCPat<PseudoCMPU_EQ_QB, PseudoPICK_QB, v4i8, SETEQ>;
def : DSPSelectCCPat<PseudoCMPU_LT_QB, PseudoPICK_QB, v4i8, SETULT>;
def : DSPSelectCCPat<PseudoCMPU_LE_QB, PseudoPICK_QB, v4i8, SETULE>;
def : DSPSelectCCPatInv<PseudoCMPU_EQ_QB, PseudoPICK_QB, v4i8, SETNE>;
def : DSPSelectCCPatInv<PseudoCMPU_LT_QB, PseudoPICK_QB, v4i8, SETUGE>;
def : DSPSelectCCPatInv<PseudoCMPU_LE_QB, PseudoPICK_QB, v4i8, SETUGT>;

// Extr patterns.
class EXTR_W_TY1_R2_Pat<SDPatternOperator OpNode, Instruction Instr> :
  DSPPat<(i32 (OpNode GPR32:$rs, ACC64DSP:$ac)),
         (Instr ACC64DSP:$ac, GPR32:$rs)>;

class EXTR_W_TY1_R1_Pat<SDPatternOperator OpNode, Instruction Instr> :
  DSPPat<(i32 (OpNode immZExt5:$shift, ACC64DSP:$ac)),
         (Instr ACC64DSP:$ac, immZExt5:$shift)>;

def : EXTR_W_TY1_R1_Pat<MipsEXTP, EXTP>;
def : EXTR_W_TY1_R2_Pat<MipsEXTP, EXTPV>;
def : EXTR_W_TY1_R1_Pat<MipsEXTPDP, EXTPDP>;
def : EXTR_W_TY1_R2_Pat<MipsEXTPDP, EXTPDPV>;
def : EXTR_W_TY1_R1_Pat<MipsEXTR_W, EXTR_W>;
def : EXTR_W_TY1_R2_Pat<MipsEXTR_W, EXTRV_W>;
def : EXTR_W_TY1_R1_Pat<MipsEXTR_R_W, EXTR_R_W>;
def : EXTR_W_TY1_R2_Pat<MipsEXTR_R_W, EXTRV_R_W>;
def : EXTR_W_TY1_R1_Pat<MipsEXTR_RS_W, EXTR_RS_W>;
def : EXTR_W_TY1_R2_Pat<MipsEXTR_RS_W, EXTRV_RS_W>;
def : EXTR_W_TY1_R1_Pat<MipsEXTR_S_H, EXTR_S_H>;
def : EXTR_W_TY1_R2_Pat<MipsEXTR_S_H, EXTRV_S_H>;

// Indexed load patterns.
class IndexedLoadPat<SDPatternOperator LoadNode, Instruction Instr> :
  DSPPat<(i32 (LoadNode (add i32:$base, i32:$index))),
         (Instr i32:$base, i32:$index)>;

let AddedComplexity = 20 in {
  def : IndexedLoadPat<zextloadi8, LBUX>;
  def : IndexedLoadPat<sextloadi16, LHX>;
  def : IndexedLoadPat<load, LWX>;
}

// Instruction alias.
let AdditionalPredicates = [NotInMicroMips] in {
  def : DSPInstAlias<"wrdsp $rt", (WRDSP GPR32Opnd:$rt, 0x1F), 1>;
}
