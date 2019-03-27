//=- Mips32r6InstrInfo.td - Mips32r6 Instruction Information -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips32r6 instructions.
//
//===----------------------------------------------------------------------===//

include "Mips32r6InstrFormats.td"

//===----------------------------------------------------------------------===//
//
// Mips profiles and nodes
//
//===----------------------------------------------------------------------===//

def SDT_MipsFSelect : SDTypeProfile<1, 3, [SDTCisFP<1>,
                                           SDTCisSameAs<0,2>,
                                           SDTCisSameAs<2,3>]>;

def MipsFSelect : SDNode<"MipsISD::FSELECT", SDT_MipsFSelect>;

//===----------------------------------------------------------------------===//
//
// Mips Operands
//
//===----------------------------------------------------------------------===//

// Notes about removals/changes from MIPS32r6:
// Reencoded: jr -> jalr
// Reencoded: jr.hb -> jalr.hb

def brtarget21 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget21OpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget21";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def brtarget26 : Operand<OtherVT> {
  let EncoderMethod = "getBranchTarget26OpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget26";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def jmpoffset16 : Operand<OtherVT> {
  let EncoderMethod = "getJumpOffset16OpValue";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

def calloffset16 : Operand<iPTR> {
  let EncoderMethod = "getJumpOffset16OpValue";
  let ParserMatchClass = MipsJumpTargetAsmOperand;
}

//===----------------------------------------------------------------------===//
//
// Instruction Encodings
//
//===----------------------------------------------------------------------===//

class ADDIUPC_ENC : PCREL19_FM<OPCODE2_ADDIUPC>;
class ALIGN_ENC  : SPECIAL3_ALIGN_FM<OPCODE6_ALIGN>;
class ALUIPC_ENC : PCREL16_FM<OPCODE5_ALUIPC>;
class AUI_ENC    : AUI_FM;
class AUIPC_ENC  : PCREL16_FM<OPCODE5_AUIPC>;

class BAL_ENC   : BAL_FM;
class BALC_ENC  : BRANCH_OFF26_FM<0b111010>;
class BC_ENC    : BRANCH_OFF26_FM<0b110010>;
class BEQC_ENC  : CMP_BRANCH_2R_OFF16_FM<OPGROUP_ADDI>,
                  DecodeDisambiguates<"AddiGroupBranch">;
class BEQZALC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_ADDI>,
                    DecodeDisambiguatedBy<"DaddiGroupBranch">;
class BNEC_ENC  : CMP_BRANCH_2R_OFF16_FM<OPGROUP_DADDI>,
                  DecodeDisambiguates<"DaddiGroupBranch">;
class BNEZALC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_DADDI>,
                    DecodeDisambiguatedBy<"DaddiGroupBranch">;

class BLTZC_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM<OPGROUP_BGTZL>,
                  DecodeDisambiguates<"BgtzlGroupBranch">;
class BGEC_ENC  : CMP_BRANCH_2R_OFF16_FM<OPGROUP_BLEZL>,
                  DecodeDisambiguatedBy<"BlezlGroupBranch">;
class BGEUC_ENC : CMP_BRANCH_2R_OFF16_FM<OPGROUP_BLEZ>,
                  DecodeDisambiguatedBy<"BlezGroupBranch">;
class BGEZC_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM<OPGROUP_BLEZL>,
                  DecodeDisambiguates<"BlezlGroupBranch">;
class BGTZALC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_BGTZ>,
                    DecodeDisambiguatedBy<"BgtzGroupBranch">;

class BLTC_ENC : CMP_BRANCH_2R_OFF16_FM<OPGROUP_BGTZL>,
                 DecodeDisambiguatedBy<"BgtzlGroupBranch">;
class BLTUC_ENC : CMP_BRANCH_2R_OFF16_FM<OPGROUP_BGTZ>,
                  DecodeDisambiguatedBy<"BgtzGroupBranch">;

class BLEZC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_BLEZL>,
                  DecodeDisambiguatedBy<"BlezlGroupBranch">;
class BLTZALC_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM<OPGROUP_BGTZ>,
                    DecodeDisambiguates<"BgtzGroupBranch">;
class BGTZC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_BGTZL>,
                  DecodeDisambiguatedBy<"BgtzlGroupBranch">;

class BEQZC_ENC : CMP_BRANCH_OFF21_FM<0b110110>;
class BGEZALC_ENC : CMP_BRANCH_1R_BOTH_OFF16_FM<OPGROUP_BLEZ>,
                    DecodeDisambiguates<"BlezGroupBranch">;
class BNEZC_ENC : CMP_BRANCH_OFF21_FM<0b111110>;

class BC1EQZ_ENC : COP1_BCCZ_FM<OPCODE5_BC1EQZ>;
class BC1NEZ_ENC : COP1_BCCZ_FM<OPCODE5_BC1NEZ>;
class BC2EQZ_ENC : COP2_BCCZ_FM<OPCODE5_BC2EQZ>;
class BC2NEZ_ENC : COP2_BCCZ_FM<OPCODE5_BC2NEZ>;

class DVP_ENC : COP0_EVP_DVP_FM<0b1>;
class EVP_ENC : COP0_EVP_DVP_FM<0b0>;

class JIALC_ENC : JMP_IDX_COMPACT_FM<0b111110>;
class JIC_ENC   : JMP_IDX_COMPACT_FM<0b110110>;
class JR_HB_R6_ENC : JR_HB_R6_FM<OPCODE6_JALR>;
class BITSWAP_ENC : SPECIAL3_2R_FM<OPCODE6_BITSWAP>;
class BLEZALC_ENC : CMP_BRANCH_1R_RT_OFF16_FM<OPGROUP_BLEZ>,
                    DecodeDisambiguatedBy<"BlezGroupBranch">;
class BNVC_ENC   : CMP_BRANCH_2R_OFF16_FM<OPGROUP_DADDI>,
                   DecodeDisambiguatedBy<"DaddiGroupBranch">;
class BOVC_ENC   : CMP_BRANCH_2R_OFF16_FM<OPGROUP_ADDI>,
                   DecodeDisambiguatedBy<"AddiGroupBranch">;
class DIV_ENC    : SPECIAL_3R_FM<0b00010, 0b011010>;
class DIVU_ENC   : SPECIAL_3R_FM<0b00010, 0b011011>;
class MOD_ENC    : SPECIAL_3R_FM<0b00011, 0b011010>;
class MODU_ENC   : SPECIAL_3R_FM<0b00011, 0b011011>;
class MUH_ENC    : SPECIAL_3R_FM<0b00011, 0b011000>;
class MUHU_ENC   : SPECIAL_3R_FM<0b00011, 0b011001>;
class MUL_R6_ENC : SPECIAL_3R_FM<0b00010, 0b011000>;
class MULU_ENC   : SPECIAL_3R_FM<0b00010, 0b011001>;

class MADDF_S_ENC  : COP1_3R_FM<0b011000, FIELD_FMT_S>;
class MADDF_D_ENC  : COP1_3R_FM<0b011000, FIELD_FMT_D>;
class MSUBF_S_ENC  : COP1_3R_FM<0b011001, FIELD_FMT_S>;
class MSUBF_D_ENC  : COP1_3R_FM<0b011001, FIELD_FMT_D>;

class SEL_D_ENC  : COP1_3R_FM<0b010000, FIELD_FMT_D>;
class SEL_S_ENC  : COP1_3R_FM<0b010000, FIELD_FMT_S>;

class SELEQZ_ENC : SPECIAL_3R_FM<0b00000, 0b110101>;
class SELNEZ_ENC : SPECIAL_3R_FM<0b00000, 0b110111>;

class LWPC_ENC   : PCREL19_FM<OPCODE2_LWPC>;
class LWUPC_ENC  : PCREL19_FM<OPCODE2_LWUPC>;

class MAX_S_ENC : COP1_3R_FM<0b011101, FIELD_FMT_S>;
class MAX_D_ENC : COP1_3R_FM<0b011101, FIELD_FMT_D>;
class MIN_S_ENC : COP1_3R_FM<0b011100, FIELD_FMT_S>;
class MIN_D_ENC : COP1_3R_FM<0b011100, FIELD_FMT_D>;

class MAXA_S_ENC : COP1_3R_FM<0b011111, FIELD_FMT_S>;
class MAXA_D_ENC : COP1_3R_FM<0b011111, FIELD_FMT_D>;
class MINA_S_ENC : COP1_3R_FM<0b011110, FIELD_FMT_S>;
class MINA_D_ENC : COP1_3R_FM<0b011110, FIELD_FMT_D>;

class SELEQZ_S_ENC : COP1_3R_FM<0b010100, FIELD_FMT_S>;
class SELEQZ_D_ENC : COP1_3R_FM<0b010100, FIELD_FMT_D>;
class SELNEZ_S_ENC : COP1_3R_FM<0b010111, FIELD_FMT_S>;
class SELNEZ_D_ENC : COP1_3R_FM<0b010111, FIELD_FMT_D>;

class RINT_S_ENC : COP1_2R_FM<0b011010, FIELD_FMT_S>;
class RINT_D_ENC : COP1_2R_FM<0b011010, FIELD_FMT_D>;
class CLASS_S_ENC : COP1_2R_FM<0b011011, FIELD_FMT_S>;
class CLASS_D_ENC : COP1_2R_FM<0b011011, FIELD_FMT_D>;

class CACHE_ENC : SPECIAL3_MEM_FM<OPCODE6_CACHE>;
class PREF_ENC : SPECIAL3_MEM_FM<OPCODE6_PREF>;

class LDC2_R6_ENC : COP2LDST_FM<OPCODE5_LDC2>;
class LWC2_R6_ENC : COP2LDST_FM<OPCODE5_LWC2>;
class SDC2_R6_ENC : COP2LDST_FM<OPCODE5_SDC2>;
class SWC2_R6_ENC : COP2LDST_FM<OPCODE5_SWC2>;

class LSA_R6_ENC : SPECIAL_LSA_FM<OPCODE6_LSA>;

class LL_R6_ENC : SPECIAL3_LL_SC_FM<OPCODE6_LL>;
class SC_R6_ENC : SPECIAL3_LL_SC_FM<OPCODE6_SC>;

class CLO_R6_ENC : SPECIAL_2R_FM<OPCODE6_CLO>;
class CLZ_R6_ENC : SPECIAL_2R_FM<OPCODE6_CLZ>;

class SDBBP_R6_ENC : SPECIAL_SDBBP_FM;

class CRC32B_ENC  : SPECIAL3_2R_SZ_CRC<0,0>;
class CRC32H_ENC  : SPECIAL3_2R_SZ_CRC<1,0>;
class CRC32W_ENC  : SPECIAL3_2R_SZ_CRC<2,0>;
class CRC32CB_ENC : SPECIAL3_2R_SZ_CRC<0,1>;
class CRC32CH_ENC : SPECIAL3_2R_SZ_CRC<1,1>;
class CRC32CW_ENC : SPECIAL3_2R_SZ_CRC<2,1>;

class GINVI_ENC : SPECIAL3_GINV<0>;
class GINVT_ENC : SPECIAL3_GINV<2>;

class SIGRIE_ENC : SIGRIE_FM;

//===----------------------------------------------------------------------===//
//
// Instruction Multiclasses
//
//===----------------------------------------------------------------------===//

class CMP_CONDN_DESC_BASE<string CondStr, string Typestr,
                          RegisterOperand FGROpnd,
                          InstrItinClass Itin,
                          SDPatternOperator Op = null_frag> {
  dag OutOperandList = (outs FGRCCOpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat("cmp.", CondStr, ".", Typestr, "\t$fd, $fs, $ft");
  list<dag> Pattern = [(set FGRCCOpnd:$fd, (Op FGROpnd:$fs, FGROpnd:$ft))];
  bit isCTI = 1;
  InstrItinClass Itinerary = Itin;
}

multiclass CMP_CC_M <FIELD_CMP_FORMAT Format, string Typestr,
                     RegisterOperand FGROpnd, InstrItinClass Itin>{
  let AdditionalPredicates = [NotInMicroMips] in {
    def CMP_F_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format, FIELD_CMP_COND_AF>,
                      CMP_CONDN_DESC_BASE<"af", Typestr, FGROpnd, Itin>,
                      MipsR6Arch<!strconcat("cmp.af.", Typestr)>,
                      ISA_MIPS32R6, HARDFLOAT;
    def CMP_UN_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format, FIELD_CMP_COND_UN>,
                       CMP_CONDN_DESC_BASE<"un", Typestr, FGROpnd, Itin, setuo>,
                       MipsR6Arch<!strconcat("cmp.un.", Typestr)>,
                       ISA_MIPS32R6, HARDFLOAT;
    def CMP_EQ_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format, FIELD_CMP_COND_EQ>,
                       CMP_CONDN_DESC_BASE<"eq", Typestr, FGROpnd, Itin,
                                           setoeq>,
                       MipsR6Arch<!strconcat("cmp.eq.", Typestr)>,
                       ISA_MIPS32R6, HARDFLOAT;
    def CMP_UEQ_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_UEQ>,
                        CMP_CONDN_DESC_BASE<"ueq", Typestr, FGROpnd, Itin,
                                            setueq>,
                        MipsR6Arch<!strconcat("cmp.ueq.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_LT_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format, FIELD_CMP_COND_LT>,
                       CMP_CONDN_DESC_BASE<"lt", Typestr, FGROpnd, Itin,
                                           setolt>,
                       MipsR6Arch<!strconcat("cmp.lt.", Typestr)>,
                       ISA_MIPS32R6, HARDFLOAT;
    def CMP_ULT_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_ULT>,
                        CMP_CONDN_DESC_BASE<"ult", Typestr, FGROpnd, Itin,
                                            setult>,
                        MipsR6Arch<!strconcat("cmp.ult.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_LE_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format, FIELD_CMP_COND_LE>,
                       CMP_CONDN_DESC_BASE<"le", Typestr, FGROpnd, Itin,
                                           setole>,
                       MipsR6Arch<!strconcat("cmp.le.", Typestr)>,
                       ISA_MIPS32R6, HARDFLOAT;
    def CMP_ULE_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_ULE>,
                        CMP_CONDN_DESC_BASE<"ule", Typestr, FGROpnd, Itin,
                                            setule>,
                        MipsR6Arch<!strconcat("cmp.ule.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SAF_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_SAF>,
                        CMP_CONDN_DESC_BASE<"saf", Typestr, FGROpnd, Itin>,
                        MipsR6Arch<!strconcat("cmp.saf.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SUN_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_SUN>,
                        CMP_CONDN_DESC_BASE<"sun", Typestr, FGROpnd, Itin>,
                        MipsR6Arch<!strconcat("cmp.sun.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SEQ_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_SEQ>,
                        CMP_CONDN_DESC_BASE<"seq", Typestr, FGROpnd, Itin>,
                        MipsR6Arch<!strconcat("cmp.seq.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SUEQ_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                      FIELD_CMP_COND_SUEQ>,
                         CMP_CONDN_DESC_BASE<"sueq", Typestr, FGROpnd, Itin>,
                         MipsR6Arch<!strconcat("cmp.sueq.", Typestr)>,
                         ISA_MIPS32R6, HARDFLOAT;
    def CMP_SLT_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_SLT>,
                        CMP_CONDN_DESC_BASE<"slt", Typestr, FGROpnd, Itin>,
                        MipsR6Arch<!strconcat("cmp.slt.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SULT_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                      FIELD_CMP_COND_SULT>,
                         CMP_CONDN_DESC_BASE<"sult", Typestr, FGROpnd, Itin>,
                         MipsR6Arch<!strconcat("cmp.sult.", Typestr)>,
                         ISA_MIPS32R6, HARDFLOAT;
    def CMP_SLE_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                     FIELD_CMP_COND_SLE>,
                        CMP_CONDN_DESC_BASE<"sle", Typestr, FGROpnd, Itin>,
                        MipsR6Arch<!strconcat("cmp.sle.", Typestr)>,
                        ISA_MIPS32R6, HARDFLOAT;
    def CMP_SULE_#NAME : R6MMR6Rel, COP1_CMP_CONDN_FM<Format,
                                                      FIELD_CMP_COND_SULE>,
                         CMP_CONDN_DESC_BASE<"sule", Typestr, FGROpnd, Itin>,
                         MipsR6Arch<!strconcat("cmp.sule.", Typestr)>,
                         ISA_MIPS32R6, HARDFLOAT;
  }
}

//===----------------------------------------------------------------------===//
//
// Instruction Descriptions
//
//===----------------------------------------------------------------------===//

class PCREL_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                      Operand ImmOpnd, InstrItinClass itin>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rs);
  dag InOperandList = (ins ImmOpnd:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rs, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class ADDIUPC_DESC : PCREL_DESC_BASE<"addiupc", GPR32Opnd, simm19_lsl2,
                                     II_ADDIUPC>;
class LWPC_DESC: PCREL_DESC_BASE<"lwpc", GPR32Opnd, simm19_lsl2, II_LWPC>;
class LWUPC_DESC: PCREL_DESC_BASE<"lwupc", GPR32Opnd, simm19_lsl2, II_LWUPC>;

class ALIGN_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                      Operand ImmOpnd, InstrItinClass itin>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, ImmOpnd:$bp);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt, $bp");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class ALIGN_DESC : ALIGN_DESC_BASE<"align", GPR32Opnd, uimm2, II_ALIGN>;

class ALUIPC_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       InstrItinClass itin = NoItinerary>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rs);
  dag InOperandList = (ins simm16:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rs, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class ALUIPC_DESC : ALUIPC_DESC_BASE<"aluipc", GPR32Opnd, II_ALUIPC>;
class AUIPC_DESC : ALUIPC_DESC_BASE<"auipc", GPR32Opnd, II_AUIPC>;

class AUI_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                    InstrItinClass itin = NoItinerary>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins GPROpnd:$rs, uimm16:$imm);
  string AsmString = !strconcat(instr_asm, "\t$rt, $rs, $imm");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class AUI_DESC : AUI_DESC_BASE<"aui", GPR32Opnd, II_AUI>;

class BRANCH_DESC_BASE {
  bit isBranch = 1;
  bit isTerminator = 1;
  bit hasDelaySlot = 0;
  bit isCTI = 1;
}

class BC_DESC_BASE<string instr_asm, DAGOperand opnd> : BRANCH_DESC_BASE,
    MipsR6Arch<instr_asm> {
  dag InOperandList = (ins opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$offset");
  bit isBarrier = 1;
  InstrItinClass Itinerary = II_BC;
  bit isCTI = 1;
}

class CMP_BC_DESC_BASE<string instr_asm, DAGOperand opnd,
                       RegisterOperand GPROpnd> : BRANCH_DESC_BASE,
                                                  MipsR6Arch<instr_asm> {
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rs, $rt, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCC;
  bit hasForbiddenSlot = 1;
  bit isCTI = 1;
}

class CMP_CBR_EQNE_Z_DESC_BASE<string instr_asm, DAGOperand opnd,
                               RegisterOperand GPROpnd>
    : BRANCH_DESC_BASE, MipsR6Arch<instr_asm> {
  dag InOperandList = (ins GPROpnd:$rs, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rs, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCZC;
  bit hasForbiddenSlot = 1;
  bit isCTI = 1;
}

class CMP_CBR_RT_Z_DESC_BASE<string instr_asm, DAGOperand opnd,
                             RegisterOperand GPROpnd>
    : BRANCH_DESC_BASE, MipsR6Arch<instr_asm> {
  dag InOperandList = (ins GPROpnd:$rt, opnd:$offset);
  dag OutOperandList = (outs);
  string AsmString = !strconcat(instr_asm, "\t$rt, $offset");
  list<Register> Defs = [AT];
  InstrItinClass Itinerary = II_BCCZC;
  bit hasForbiddenSlot = 1;
  bit isCTI = 1;
}

class BAL_DESC : BC_DESC_BASE<"bal", brtarget> {
  bit isCall = 1;
  bit hasDelaySlot = 1;
  list<Register> Defs = [RA];
  bit isCTI = 1;
}

class BALC_DESC : BC_DESC_BASE<"balc", brtarget26> {
  bit isCall = 1;
  list<Register> Defs = [RA];
  InstrItinClass Itinerary = II_BALC;
  bit isCTI = 1;
}

class BC_DESC : BC_DESC_BASE<"bc", brtarget26>;
class BGEC_DESC : CMP_BC_DESC_BASE<"bgec", brtarget, GPR32Opnd>;
class BGEUC_DESC : CMP_BC_DESC_BASE<"bgeuc", brtarget, GPR32Opnd>;
class BEQC_DESC : CMP_BC_DESC_BASE<"beqc", brtarget, GPR32Opnd>;
class BNEC_DESC : CMP_BC_DESC_BASE<"bnec", brtarget, GPR32Opnd>;

class BLTC_DESC : CMP_BC_DESC_BASE<"bltc", brtarget, GPR32Opnd>;
class BLTUC_DESC : CMP_BC_DESC_BASE<"bltuc", brtarget, GPR32Opnd>;

class BLTZC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bltzc", brtarget, GPR32Opnd>;
class BGEZC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bgezc", brtarget, GPR32Opnd>;

class BLEZC_DESC : CMP_CBR_RT_Z_DESC_BASE<"blezc", brtarget, GPR32Opnd>;
class BGTZC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bgtzc", brtarget, GPR32Opnd>;

class BEQZC_DESC : CMP_CBR_EQNE_Z_DESC_BASE<"beqzc", brtarget21, GPR32Opnd>;
class BNEZC_DESC : CMP_CBR_EQNE_Z_DESC_BASE<"bnezc", brtarget21, GPR32Opnd>;

class COP1_BCCZ_DESC_BASE<string instr_asm> : BRANCH_DESC_BASE {
  dag InOperandList = (ins FGR64Opnd:$ft, brtarget:$offset);
  dag OutOperandList = (outs);
  string AsmString = instr_asm;
  bit hasDelaySlot = 1;
  InstrItinClass Itinerary = II_BC1CCZ;
}

class BC1EQZ_DESC : COP1_BCCZ_DESC_BASE<"bc1eqz $ft, $offset">;
class BC1NEZ_DESC : COP1_BCCZ_DESC_BASE<"bc1nez $ft, $offset">;

class COP2_BCCZ_DESC_BASE<string instr_asm> : BRANCH_DESC_BASE {
  dag InOperandList = (ins COP2Opnd:$ct, brtarget:$offset);
  dag OutOperandList = (outs);
  string AsmString = instr_asm;
  bit hasDelaySlot = 1;
  bit isCTI = 1;
  InstrItinClass Itinerary = II_BC2CCZ;
}

class BC2EQZ_DESC : COP2_BCCZ_DESC_BASE<"bc2eqz $ct, $offset">;
class BC2NEZ_DESC : COP2_BCCZ_DESC_BASE<"bc2nez $ct, $offset">;

class BOVC_DESC   : CMP_BC_DESC_BASE<"bovc", brtarget, GPR32Opnd>;
class BNVC_DESC   : CMP_BC_DESC_BASE<"bnvc", brtarget, GPR32Opnd>;

class JMP_IDX_COMPACT_DESC_BASE<string opstr, DAGOperand opnd,
                                RegisterOperand GPROpnd,
                                InstrItinClass itin = NoItinerary>
    : MipsR6Arch<opstr> {
  dag InOperandList = (ins GPROpnd:$rt, opnd:$offset);
  string AsmString = !strconcat(opstr, "\t$rt, $offset");
  list<dag> Pattern = [];
  bit hasDelaySlot = 0;
  InstrItinClass Itinerary = itin;
  bit isCTI = 1;
  bit isBranch = 1;
  bit isIndirectBranch = 1;
}

class JIALC_DESC : JMP_IDX_COMPACT_DESC_BASE<"jialc", calloffset16,
                                             GPR32Opnd, II_JIALC> {
  bit isCall = 1;
  list<Register> Defs = [RA];
}

class JIC_DESC : JMP_IDX_COMPACT_DESC_BASE<"jic", jmpoffset16,
                                           GPR32Opnd, II_JIALC> {
  bit isBarrier = 1;
  bit isTerminator = 1;
  list<Register> Defs = [AT];
}

class JR_HB_R6_DESC : JR_HB_DESC_BASE<"jr.hb", GPR32Opnd> {
  bit isBranch = 1;
  bit isIndirectBranch = 1;
  bit hasDelaySlot = 1;
  bit isTerminator=1;
  bit isBarrier=1;
  bit isCTI = 1;
  InstrItinClass Itinerary = II_JR_HB;
}

class BITSWAP_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                        InstrItinClass itin>
    : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class BITSWAP_DESC : BITSWAP_DESC_BASE<"bitswap", GPR32Opnd, II_BITSWAP>;

class DIVMOD_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       InstrItinClass itin,
                       SDPatternOperator Op=null_frag>
    : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set GPROpnd:$rd, (Op GPROpnd:$rs, GPROpnd:$rt))];
  InstrItinClass Itinerary = itin;
  // This instruction doesn't trap division by zero itself. We must insert
  // teq instructions as well.
  bit usesCustomInserter = 1;
}

class DVPEVP_DESC_BASE<string instr_asm, InstrItinClass Itin>
    : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPR32Opnd:$rt);
  dag InOperandList = (ins);
  string AsmString = !strconcat(instr_asm, "\t$rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = Itin;
  bit hasUnModeledSideEffects = 1;
}

class DVP_DESC : DVPEVP_DESC_BASE<"dvp", II_DVP>;
class EVP_DESC : DVPEVP_DESC_BASE<"evp", II_EVP>;

class DIV_DESC  : DIVMOD_DESC_BASE<"div", GPR32Opnd, II_DIV, sdiv>;
class DIVU_DESC : DIVMOD_DESC_BASE<"divu", GPR32Opnd, II_DIVU, udiv>;
class MOD_DESC  : DIVMOD_DESC_BASE<"mod", GPR32Opnd, II_MOD, srem>;
class MODU_DESC : DIVMOD_DESC_BASE<"modu", GPR32Opnd, II_MODU, urem>;

class BEQZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"beqzalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BGEZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bgezalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BGTZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bgtzalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BLEZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"blezalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BLTZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bltzalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class BNEZALC_DESC : CMP_CBR_RT_Z_DESC_BASE<"bnezalc", brtarget, GPR32Opnd> {
  list<Register> Defs = [RA];
}

class MUL_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       InstrItinClass itin,
                       SDPatternOperator Op=null_frag> : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [(set GPROpnd:$rd, (Op GPROpnd:$rs, GPROpnd:$rt))];
  InstrItinClass Itinerary = itin;
}

class MUH_DESC    : MUL_R6_DESC_BASE<"muh", GPR32Opnd, II_MUH, mulhs>;
class MUHU_DESC   : MUL_R6_DESC_BASE<"muhu", GPR32Opnd, II_MUHU, mulhu>;
class MUL_R6_DESC : MUL_R6_DESC_BASE<"mul", GPR32Opnd, II_MUL, mul>;
class MULU_DESC   : MUL_R6_DESC_BASE<"mulu", GPR32Opnd, II_MULU>;

class COP1_SEL_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                         InstrItinClass itin> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGRCCOpnd:$fd_in, FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [(set FGROpnd:$fd, (select FGRCCOpnd:$fd_in,
                                                 FGROpnd:$ft,
                                                 FGROpnd:$fs))];
  string Constraints = "$fd_in = $fd";
  InstrItinClass Itinerary = itin;
}

class COP1_SEL_D_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                           InstrItinClass itin> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fd_in, FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [(set FGROpnd:$fd, (MipsFSelect FGROpnd:$fd_in,
                                                      FGROpnd:$ft,
                                                      FGROpnd:$fs))];
  string Constraints = "$fd_in = $fd";
  InstrItinClass Itinerary = itin;
}

class SEL_D_DESC : COP1_SEL_D_DESC_BASE<"sel.d", FGR64Opnd, II_SEL_D>,
                   MipsR6Arch<"sel.d">;
class SEL_S_DESC : COP1_SEL_DESC_BASE<"sel.s", FGR32Opnd, II_SEL_S>,
                   MipsR6Arch<"sel.s">;

class SELEQNE_Z_DESC_BASE<string instr_asm, RegisterOperand GPROpnd>
    : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_SELCCZ;
}

class SELEQZ_DESC : SELEQNE_Z_DESC_BASE<"seleqz", GPR32Opnd>;
class SELNEZ_DESC : SELEQNE_Z_DESC_BASE<"selnez", GPR32Opnd>;

class COP1_4R_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                        InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fd_in, FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [];
  string Constraints = "$fd_in = $fd";
  InstrItinClass Itinerary = itin;
}

class MADDF_S_DESC  : COP1_4R_DESC_BASE<"maddf.s", FGR32Opnd, II_MADDF_S>;
class MADDF_D_DESC  : COP1_4R_DESC_BASE<"maddf.d", FGR64Opnd, II_MADDF_D>;
class MSUBF_S_DESC  : COP1_4R_DESC_BASE<"msubf.s", FGR32Opnd, II_MSUBF_S>;
class MSUBF_D_DESC  : COP1_4R_DESC_BASE<"msubf.d", FGR64Opnd, II_MSUBF_D>;

class MAX_MIN_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                        InstrItinClass itin> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class MAX_S_DESC : MAX_MIN_DESC_BASE<"max.s", FGR32Opnd, II_MAX_S>;
class MAX_D_DESC : MAX_MIN_DESC_BASE<"max.d", FGR64Opnd, II_MAX_D>;
class MIN_S_DESC : MAX_MIN_DESC_BASE<"min.s", FGR32Opnd, II_MIN_S>;
class MIN_D_DESC : MAX_MIN_DESC_BASE<"min.d", FGR64Opnd, II_MIN_D>;

class MAXA_S_DESC : MAX_MIN_DESC_BASE<"maxa.s", FGR32Opnd, II_MAX_S>;
class MAXA_D_DESC : MAX_MIN_DESC_BASE<"maxa.d", FGR64Opnd, II_MAX_D>;
class MINA_S_DESC : MAX_MIN_DESC_BASE<"mina.s", FGR32Opnd, II_MIN_D>;
class MINA_D_DESC : MAX_MIN_DESC_BASE<"mina.d", FGR64Opnd, II_MIN_S>;

class SELEQNEZ_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                         InstrItinClass itin> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fs, FGROpnd:$ft);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs, $ft");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class SELEQZ_S_DESC : SELEQNEZ_DESC_BASE<"seleqz.s", FGR32Opnd, II_SELCCZ_S>,
                      MipsR6Arch<"seleqz.s">;
class SELEQZ_D_DESC : SELEQNEZ_DESC_BASE<"seleqz.d", FGR64Opnd, II_SELCCZ_D>,
                      MipsR6Arch<"seleqz.d">;
class SELNEZ_S_DESC : SELEQNEZ_DESC_BASE<"selnez.s", FGR32Opnd, II_SELCCZ_S>,
                      MipsR6Arch<"selnez.s">;
class SELNEZ_D_DESC : SELEQNEZ_DESC_BASE<"selnez.d", FGR64Opnd, II_SELCCZ_D>,
                      MipsR6Arch<"selnez.d">;

class CLASS_RINT_DESC_BASE<string instr_asm, RegisterOperand FGROpnd,
                           InstrItinClass itin> {
  dag OutOperandList = (outs FGROpnd:$fd);
  dag InOperandList = (ins FGROpnd:$fs);
  string AsmString = !strconcat(instr_asm, "\t$fd, $fs");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class RINT_S_DESC : CLASS_RINT_DESC_BASE<"rint.s", FGR32Opnd, II_RINT_S>;
class RINT_D_DESC : CLASS_RINT_DESC_BASE<"rint.d", FGR64Opnd, II_RINT_D>;
class CLASS_S_DESC : CLASS_RINT_DESC_BASE<"class.s", FGR32Opnd, II_CLASS_S>;
class CLASS_D_DESC : CLASS_RINT_DESC_BASE<"class.d", FGR64Opnd, II_CLASS_D>;

class CACHE_HINT_DESC<string instr_asm, Operand MemOpnd,
                      RegisterOperand GPROpnd, InstrItinClass itin>
                     : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins MemOpnd:$addr, uimm5:$hint);
  string AsmString = !strconcat(instr_asm, "\t$hint, $addr");
  list<dag> Pattern = [];
  string DecoderMethod = "DecodeCacheeOp_CacheOpR6";
  InstrItinClass Itinerary = itin;
}

class CACHE_DESC : CACHE_HINT_DESC<"cache", mem_simm9, GPR32Opnd, II_CACHE>;
class PREF_DESC : CACHE_HINT_DESC<"pref", mem_simm9, GPR32Opnd, II_PREF>;

class COP2LD_DESC_BASE<string instr_asm, RegisterOperand COPOpnd,
                       InstrItinClass itin> {
  dag OutOperandList = (outs COPOpnd:$rt);
  dag InOperandList = (ins mem_simm11:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  bit mayLoad = 1;
  string DecoderMethod = "DecodeFMemCop2R6";
  InstrItinClass Itinerary = itin;
}

class LDC2_R6_DESC : COP2LD_DESC_BASE<"ldc2", COP2Opnd, II_LDC2>;
class LWC2_R6_DESC : COP2LD_DESC_BASE<"lwc2", COP2Opnd, II_LWC2>;

class COP2ST_DESC_BASE<string instr_asm, RegisterOperand COPOpnd,
                       InstrItinClass itin> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins COPOpnd:$rt, mem_simm11:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  bit mayStore = 1;
  string DecoderMethod = "DecodeFMemCop2R6";
  InstrItinClass Itinerary = itin;
}

class SDC2_R6_DESC : COP2ST_DESC_BASE<"sdc2", COP2Opnd, II_SDC2>;
class SWC2_R6_DESC : COP2ST_DESC_BASE<"swc2", COP2Opnd, II_SWC2>;

class LSA_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       Operand ImmOpnd, InstrItinClass itin>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt, ImmOpnd:$imm2);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt, $imm2");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class LSA_R6_DESC : LSA_R6_DESC_BASE<"lsa", GPR32Opnd, uimm2_plus1, II_LSA>;

class LL_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                      Operand MemOpnd, InstrItinClass itin>
      : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rt);
  dag InOperandList = (ins MemOpnd:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  bit mayLoad = 1;
  InstrItinClass Itinerary = itin;
}

class LL_R6_DESC : LL_R6_DESC_BASE<"ll", GPR32Opnd, mem_simm9, II_LL>;

class SC_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                      InstrItinClass itin> {
  dag OutOperandList = (outs GPROpnd:$dst);
  dag InOperandList = (ins GPROpnd:$rt, mem_simm9:$addr);
  string AsmString = !strconcat(instr_asm, "\t$rt, $addr");
  list<dag> Pattern = [];
  bit mayStore = 1;
  string Constraints = "$rt = $dst";
  InstrItinClass Itinerary = itin;
}

class SC_R6_DESC : SC_R6_DESC_BASE<"sc", GPR32Opnd, II_SC>;

class CLO_CLZ_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                           InstrItinClass itin>
    : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs");
  InstrItinClass Itinerary = itin;
}

class CLO_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       InstrItinClass itin> :
    CLO_CLZ_R6_DESC_BASE<instr_asm, GPROpnd, itin> {
  list<dag> Pattern = [(set GPROpnd:$rd, (ctlz (not GPROpnd:$rs)))];
}

class CLZ_R6_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                       InstrItinClass itin> :
    CLO_CLZ_R6_DESC_BASE<instr_asm, GPROpnd, itin> {
  list<dag> Pattern = [(set GPROpnd:$rd, (ctlz GPROpnd:$rs))];
}

class CLO_R6_DESC : CLO_R6_DESC_BASE<"clo", GPR32Opnd, II_CLO>;
class CLZ_R6_DESC : CLZ_R6_DESC_BASE<"clz", GPR32Opnd, II_CLZ>;

class SDBBP_R6_DESC {
  dag OutOperandList = (outs);
  dag InOperandList = (ins uimm20:$code_);
  string AsmString = "sdbbp\t$code_";
  list<dag> Pattern = [];
  bit isCTI = 1;
  InstrItinClass Itinerary = II_SDBBP;
}

class CRC_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                    InstrItinClass itin> : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs GPROpnd:$rd);
  dag InOperandList = (ins GPROpnd:$rs, GPROpnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
}

class CRC32B_DESC : CRC_DESC_BASE<"crc32b", GPR32Opnd, II_CRC32B>;
class CRC32H_DESC : CRC_DESC_BASE<"crc32h", GPR32Opnd, II_CRC32H>;
class CRC32W_DESC : CRC_DESC_BASE<"crc32w", GPR32Opnd, II_CRC32W>;
class CRC32CB_DESC : CRC_DESC_BASE<"crc32cb", GPR32Opnd, II_CRC32CB>;
class CRC32CH_DESC : CRC_DESC_BASE<"crc32ch", GPR32Opnd, II_CRC32CH>;
class CRC32CW_DESC : CRC_DESC_BASE<"crc32cw", GPR32Opnd, II_CRC32CW>;

class GINV_DESC_BASE<string instr_asm, RegisterOperand GPROpnd,
                     InstrItinClass itin> : MipsR6Arch<instr_asm> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins GPROpnd:$rs, uimm2:$type_);
  string AsmString = !strconcat(instr_asm, "\t$rs, $type_");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = itin;
  bit hasSideEffects = 1;
}

class GINVI_DESC : GINV_DESC_BASE<"ginvi", GPR32Opnd, II_GINVI> {
  dag InOperandList = (ins GPR32Opnd:$rs);
  string AsmString = "ginvi\t$rs";
}
class GINVT_DESC : GINV_DESC_BASE<"ginvt", GPR32Opnd, II_GINVT>;

class SIGRIE_DESC {
  dag OutOperandList = (outs);
  dag InOperandList = (ins uimm16:$code_);
  string AsmString = "sigrie\t$code_";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = II_SIGRIE;
}

//===----------------------------------------------------------------------===//
//
// Instruction Definitions
//
//===----------------------------------------------------------------------===//

def ADDIUPC : R6MMR6Rel, ADDIUPC_ENC, ADDIUPC_DESC, ISA_MIPS32R6;
def ALIGN : R6MMR6Rel, ALIGN_ENC, ALIGN_DESC, ISA_MIPS32R6;
def ALUIPC : R6MMR6Rel, ALUIPC_ENC, ALUIPC_DESC, ISA_MIPS32R6;
def AUI : R6MMR6Rel, AUI_ENC, AUI_DESC, ISA_MIPS32R6;
def AUIPC : R6MMR6Rel, AUIPC_ENC, AUIPC_DESC, ISA_MIPS32R6;
def BAL : BAL_ENC, BAL_DESC, ISA_MIPS32R6;
def BALC : R6MMR6Rel, BALC_ENC, BALC_DESC, ISA_MIPS32R6;
let AdditionalPredicates = [NotInMicroMips] in {
  def BC1EQZ : BC1EQZ_ENC, BC1EQZ_DESC, ISA_MIPS32R6, HARDFLOAT;
  def BC1NEZ : BC1NEZ_ENC, BC1NEZ_DESC, ISA_MIPS32R6, HARDFLOAT;
  def BC2EQZ : BC2EQZ_ENC, BC2EQZ_DESC, ISA_MIPS32R6;
  def BC2NEZ : BC2NEZ_ENC, BC2NEZ_DESC, ISA_MIPS32R6;
  def BC : R6MMR6Rel, BC_ENC, BC_DESC, ISA_MIPS32R6;
  def BEQC : R6MMR6Rel, BEQC_ENC, BEQC_DESC, ISA_MIPS32R6;
  def BEQZALC : R6MMR6Rel, BEQZALC_ENC, BEQZALC_DESC, ISA_MIPS32R6;
  def BEQZC : R6MMR6Rel, BEQZC_ENC, BEQZC_DESC, ISA_MIPS32R6;
  def BGEC : R6MMR6Rel, BGEC_ENC, BGEC_DESC, ISA_MIPS32R6;
  def BGEUC : R6MMR6Rel, BGEUC_ENC, BGEUC_DESC, ISA_MIPS32R6;
  def BGEZALC : R6MMR6Rel, BGEZALC_ENC, BGEZALC_DESC, ISA_MIPS32R6;
  def BGEZC : R6MMR6Rel, BGEZC_ENC, BGEZC_DESC, ISA_MIPS32R6;
  def BGTZALC : R6MMR6Rel, BGTZALC_ENC, BGTZALC_DESC, ISA_MIPS32R6;
  def BGTZC : R6MMR6Rel, BGTZC_ENC, BGTZC_DESC, ISA_MIPS32R6;
}
def BITSWAP : R6MMR6Rel, BITSWAP_ENC, BITSWAP_DESC, ISA_MIPS32R6;
let AdditionalPredicates = [NotInMicroMips] in {
  def BLEZALC : R6MMR6Rel, BLEZALC_ENC, BLEZALC_DESC, ISA_MIPS32R6;
  def BLEZC : R6MMR6Rel, BLEZC_ENC, BLEZC_DESC, ISA_MIPS32R6;
  def BLTC : R6MMR6Rel, BLTC_ENC, BLTC_DESC, ISA_MIPS32R6;
  def BLTUC : R6MMR6Rel, BLTUC_ENC, BLTUC_DESC, ISA_MIPS32R6;
  def BLTZALC : R6MMR6Rel, BLTZALC_ENC, BLTZALC_DESC, ISA_MIPS32R6;
  def BLTZC : R6MMR6Rel, BLTZC_ENC, BLTZC_DESC, ISA_MIPS32R6;
  def BNEC : R6MMR6Rel, BNEC_ENC, BNEC_DESC, ISA_MIPS32R6;
  def BNEZALC : R6MMR6Rel, BNEZALC_ENC, BNEZALC_DESC, ISA_MIPS32R6;
  def BNEZC : R6MMR6Rel, BNEZC_ENC, BNEZC_DESC, ISA_MIPS32R6;
  def BNVC : R6MMR6Rel, BNVC_ENC, BNVC_DESC, ISA_MIPS32R6;
  def BOVC : R6MMR6Rel, BOVC_ENC, BOVC_DESC, ISA_MIPS32R6;
  def CACHE_R6 : R6MMR6Rel, CACHE_ENC, CACHE_DESC, ISA_MIPS32R6;
  def CLASS_D : CLASS_D_ENC, CLASS_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def CLASS_S : CLASS_S_ENC, CLASS_S_DESC, ISA_MIPS32R6, HARDFLOAT;
}
def CLO_R6 : R6MMR6Rel, CLO_R6_ENC, CLO_R6_DESC, ISA_MIPS32R6;
def CLZ_R6 : R6MMR6Rel, CLZ_R6_ENC, CLZ_R6_DESC, ISA_MIPS32R6;
defm S : CMP_CC_M<FIELD_CMP_FORMAT_S, "s", FGR32Opnd, II_CMP_CC_S>;
defm D : CMP_CC_M<FIELD_CMP_FORMAT_D, "d", FGR64Opnd, II_CMP_CC_D>;
let AdditionalPredicates = [NotInMicroMips] in {
  def DIV : R6MMR6Rel, DIV_ENC, DIV_DESC, ISA_MIPS32R6;
  def DIVU : R6MMR6Rel, DIVU_ENC, DIVU_DESC, ISA_MIPS32R6;
}

def DVP : R6MMR6Rel, DVP_ENC, DVP_DESC, ISA_MIPS32R6;
def EVP : R6MMR6Rel, EVP_ENC, EVP_DESC, ISA_MIPS32R6;

def JIALC : R6MMR6Rel, JIALC_ENC, JIALC_DESC, ISA_MIPS32R6;
def JIC : R6MMR6Rel, JIC_ENC, JIC_DESC, ISA_MIPS32R6;
def JR_HB_R6 : JR_HB_R6_ENC, JR_HB_R6_DESC, ISA_MIPS32R6;
let AdditionalPredicates = [NotInMicroMips] in {
  def LDC2_R6 : LDC2_R6_ENC, LDC2_R6_DESC, ISA_MIPS32R6;
  def LL_R6 : LL_R6_ENC, LL_R6_DESC, PTR_32, ISA_MIPS32R6;
}
def LSA_R6 : R6MMR6Rel, LSA_R6_ENC, LSA_R6_DESC, ISA_MIPS32R6;
let AdditionalPredicates = [NotInMicroMips] in {
  def LWC2_R6 : LWC2_R6_ENC, LWC2_R6_DESC, ISA_MIPS32R6;
}
def LWPC : R6MMR6Rel, LWPC_ENC, LWPC_DESC, ISA_MIPS32R6;
let AdditionalPredicates = [NotInMicroMips] in {
  def LWUPC : R6MMR6Rel, LWUPC_ENC, LWUPC_DESC, ISA_MIPS32R6;
  def MADDF_S : MADDF_S_ENC, MADDF_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MADDF_D : MADDF_D_ENC, MADDF_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MAXA_D : MAXA_D_ENC, MAXA_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MAXA_S : MAXA_S_ENC, MAXA_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MAX_D : MAX_D_ENC, MAX_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MAX_S : MAX_S_ENC, MAX_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MINA_D : MINA_D_ENC, MINA_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MINA_S : MINA_S_ENC, MINA_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MIN_D : MIN_D_ENC, MIN_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MIN_S : MIN_S_ENC, MIN_S_DESC, ISA_MIPS32R6, HARDFLOAT;

  def MOD : R6MMR6Rel, MOD_ENC, MOD_DESC, ISA_MIPS32R6;
  def MODU : R6MMR6Rel, MODU_ENC, MODU_DESC, ISA_MIPS32R6;

  def MSUBF_S : MSUBF_S_ENC, MSUBF_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def MSUBF_D : MSUBF_D_ENC, MSUBF_D_DESC, ISA_MIPS32R6, HARDFLOAT;

  def MUH    : R6MMR6Rel, MUH_ENC, MUH_DESC, ISA_MIPS32R6;
  def MUHU   : R6MMR6Rel, MUHU_ENC, MUHU_DESC, ISA_MIPS32R6;
  def MUL_R6 : R6MMR6Rel, MUL_R6_ENC, MUL_R6_DESC, ISA_MIPS32R6;
  def MULU   : R6MMR6Rel, MULU_ENC, MULU_DESC, ISA_MIPS32R6;
}
def NAL; // BAL with rd=0
let AdditionalPredicates = [NotInMicroMips] in {
  def PREF_R6 : R6MMR6Rel, PREF_ENC, PREF_DESC, ISA_MIPS32R6;
  def RINT_D : RINT_D_ENC, RINT_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def RINT_S : RINT_S_ENC, RINT_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def SC_R6 : SC_R6_ENC, SC_R6_DESC, PTR_32, ISA_MIPS32R6;
  def SDBBP_R6 : SDBBP_R6_ENC, SDBBP_R6_DESC, ISA_MIPS32R6;
  def SELEQZ : R6MMR6Rel, SELEQZ_ENC, SELEQZ_DESC, ISA_MIPS32R6, GPR_32;
  def SELNEZ : R6MMR6Rel, SELNEZ_ENC, SELNEZ_DESC, ISA_MIPS32R6, GPR_32;
  def SELEQZ_D : R6MMR6Rel, SELEQZ_D_ENC, SELEQZ_D_DESC, ISA_MIPS32R6,
                 HARDFLOAT;
  def SELEQZ_S : R6MMR6Rel, SELEQZ_S_ENC, SELEQZ_S_DESC, ISA_MIPS32R6,
                 HARDFLOAT;
  def SELNEZ_D : R6MMR6Rel, SELNEZ_D_ENC, SELNEZ_D_DESC, ISA_MIPS32R6,
                 HARDFLOAT;
  def SELNEZ_S : R6MMR6Rel, SELNEZ_S_ENC, SELNEZ_S_DESC, ISA_MIPS32R6,
                 HARDFLOAT;
  def SEL_D : R6MMR6Rel, SEL_D_ENC, SEL_D_DESC, ISA_MIPS32R6, HARDFLOAT;
  def SEL_S : R6MMR6Rel, SEL_S_ENC, SEL_S_DESC, ISA_MIPS32R6, HARDFLOAT;
  def SDC2_R6 : SDC2_R6_ENC, SDC2_R6_DESC, ISA_MIPS32R6;
  def SWC2_R6 : SWC2_R6_ENC, SWC2_R6_DESC, ISA_MIPS32R6;
  def SIGRIE : SIGRIE_ENC, SIGRIE_DESC, ISA_MIPS32R6;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def CRC32B : R6MMR6Rel, CRC32B_ENC, CRC32B_DESC, ISA_MIPS32R6, ASE_CRC;
  def CRC32H : R6MMR6Rel, CRC32H_ENC, CRC32H_DESC, ISA_MIPS32R6, ASE_CRC;
  def CRC32W : R6MMR6Rel, CRC32W_ENC, CRC32W_DESC, ISA_MIPS32R6, ASE_CRC;
  def CRC32CB : R6MMR6Rel, CRC32CB_ENC, CRC32CB_DESC, ISA_MIPS32R6, ASE_CRC;
  def CRC32CH : R6MMR6Rel, CRC32CH_ENC, CRC32CH_DESC, ISA_MIPS32R6, ASE_CRC;
  def CRC32CW : R6MMR6Rel, CRC32CW_ENC, CRC32CW_DESC, ISA_MIPS32R6, ASE_CRC;
}

let AdditionalPredicates = [NotInMicroMips] in {
  def GINVI : R6MMR6Rel, GINVI_ENC, GINVI_DESC, ISA_MIPS32R6, ASE_GINV;
  def GINVT : R6MMR6Rel, GINVT_ENC, GINVT_DESC, ISA_MIPS32R6, ASE_GINV;
}

//===----------------------------------------------------------------------===//
//
// Instruction Aliases
//
//===----------------------------------------------------------------------===//

def : MipsInstAlias<"dvp", (DVP ZERO), 0>, ISA_MIPS32R6;
def : MipsInstAlias<"evp", (EVP ZERO), 0>, ISA_MIPS32R6;

let AdditionalPredicates = [NotInMicroMips] in {
def : MipsInstAlias<"sdbbp", (SDBBP_R6 0)>, ISA_MIPS32R6;
def : MipsInstAlias<"sigrie", (SIGRIE 0)>, ISA_MIPS32R6;
def : MipsInstAlias<"jr $rs", (JALR ZERO, GPR32Opnd:$rs), 1>, ISA_MIPS32R6, GPR_32;
}

def : MipsInstAlias<"jrc $rs", (JIC GPR32Opnd:$rs, 0), 1>, ISA_MIPS32R6, GPR_32;

let AdditionalPredicates = [NotInMicroMips] in {
def : MipsInstAlias<"jalrc $rs", (JIALC GPR32Opnd:$rs, 0), 1>, ISA_MIPS32R6, GPR_32;
}

def : MipsInstAlias<"div $rs, $rt", (DIV GPR32Opnd:$rs, GPR32Opnd:$rs,
                                         GPR32Opnd:$rt)>, ISA_MIPS32R6;
def : MipsInstAlias<"divu $rs, $rt", (DIVU GPR32Opnd:$rs, GPR32Opnd:$rs,
                                           GPR32Opnd:$rt)>, ISA_MIPS32R6;

def : MipsInstAlias<"lapc $rd, $imm",
                    (ADDIUPC GPR32Opnd:$rd, simm19_lsl2:$imm)>, ISA_MIPS32R6;

//===----------------------------------------------------------------------===//
//
// Patterns and Pseudo Instructions
//
//===----------------------------------------------------------------------===//

// comparisons supported via another comparison
multiclass Cmp_Pats<ValueType VT, Instruction NOROp, Register ZEROReg> {
def : MipsPat<(setone VT:$lhs, VT:$rhs),
      (NOROp (!cast<Instruction>("CMP_UEQ_"#NAME) VT:$lhs, VT:$rhs), ZEROReg)>;
def : MipsPat<(seto VT:$lhs, VT:$rhs),
      (NOROp (!cast<Instruction>("CMP_UN_"#NAME) VT:$lhs, VT:$rhs), ZEROReg)>;
def : MipsPat<(setune VT:$lhs, VT:$rhs),
      (NOROp (!cast<Instruction>("CMP_EQ_"#NAME) VT:$lhs, VT:$rhs), ZEROReg)>;
def : MipsPat<(seteq VT:$lhs, VT:$rhs),
      (!cast<Instruction>("CMP_EQ_"#NAME) VT:$lhs, VT:$rhs)>;
def : MipsPat<(setgt VT:$lhs, VT:$rhs),
      (!cast<Instruction>("CMP_LE_"#NAME) VT:$rhs, VT:$lhs)>;
def : MipsPat<(setge VT:$lhs, VT:$rhs),
      (!cast<Instruction>("CMP_LT_"#NAME) VT:$rhs, VT:$lhs)>;
def : MipsPat<(setlt VT:$lhs, VT:$rhs),
      (!cast<Instruction>("CMP_LT_"#NAME) VT:$lhs, VT:$rhs)>;
def : MipsPat<(setle VT:$lhs, VT:$rhs),
      (!cast<Instruction>("CMP_LE_"#NAME) VT:$lhs, VT:$rhs)>;
def : MipsPat<(setne VT:$lhs, VT:$rhs),
      (NOROp (!cast<Instruction>("CMP_EQ_"#NAME) VT:$lhs, VT:$rhs), ZEROReg)>;
}

let AdditionalPredicates = [NotInMicroMips] in {
  defm S : Cmp_Pats<f32, NOR, ZERO>, ISA_MIPS32R6;
  defm D : Cmp_Pats<f64, NOR, ZERO>, ISA_MIPS32R6;
}

// i32 selects
multiclass SelectInt_Pats<ValueType RC, Instruction OROp, Instruction XORiOp,
                          Instruction SLTiOp, Instruction SLTiuOp,
                          Instruction SELEQZOp, Instruction SELNEZOp,
                          SDPatternOperator imm_type, ValueType Opg> {
// reg, immz
def : MipsPat<(select (Opg (seteq RC:$cond, immz)), RC:$t, RC:$f),
              (OROp (SELEQZOp RC:$t, RC:$cond), (SELNEZOp RC:$f, RC:$cond))>;
def : MipsPat<(select (Opg (setne RC:$cond, immz)), RC:$t, RC:$f),
              (OROp (SELNEZOp RC:$t, RC:$cond), (SELEQZOp RC:$f, RC:$cond))>;

// reg, immZExt16[_64]
def : MipsPat<(select (Opg (seteq RC:$cond, imm_type:$imm)), RC:$t, RC:$f),
              (OROp (SELEQZOp RC:$t, (XORiOp RC:$cond, imm_type:$imm)),
                    (SELNEZOp RC:$f, (XORiOp RC:$cond, imm_type:$imm)))>;
def : MipsPat<(select (Opg (setne RC:$cond, imm_type:$imm)), RC:$t, RC:$f),
              (OROp (SELNEZOp RC:$t, (XORiOp RC:$cond, imm_type:$imm)),
                    (SELEQZOp RC:$f, (XORiOp RC:$cond, imm_type:$imm)))>;

// reg, immSExt16Plus1
def : MipsPat<(select (Opg (setgt RC:$cond, immSExt16Plus1:$imm)), RC:$t, RC:$f),
              (OROp (SELEQZOp RC:$t, (SLTiOp RC:$cond, (Plus1 imm:$imm))),
                    (SELNEZOp RC:$f, (SLTiOp RC:$cond, (Plus1 imm:$imm))))>;
def : MipsPat<(select (Opg (setugt RC:$cond, immSExt16Plus1:$imm)), RC:$t, RC:$f),
              (OROp (SELEQZOp RC:$t, (SLTiuOp RC:$cond, (Plus1 imm:$imm))),
                    (SELNEZOp RC:$f, (SLTiuOp RC:$cond, (Plus1 imm:$imm))))>;

def : MipsPat<(select (Opg (seteq RC:$cond, immz)), RC:$t, immz),
              (SELEQZOp RC:$t, RC:$cond)>;
def : MipsPat<(select (Opg (setne RC:$cond, immz)), RC:$t, immz),
              (SELNEZOp RC:$t, RC:$cond)>;
def : MipsPat<(select (Opg (seteq RC:$cond, immz)), immz, RC:$f),
              (SELNEZOp RC:$f, RC:$cond)>;
def : MipsPat<(select (Opg (setne RC:$cond, immz)), immz, RC:$f),
              (SELEQZOp RC:$f, RC:$cond)>;
}

let AdditionalPredicates = [NotInMicroMips] in {
defm : SelectInt_Pats<i32, OR, XORi, SLTi, SLTiu, SELEQZ, SELNEZ,
                      immZExt16, i32>, ISA_MIPS32R6;

def : MipsPat<(select i32:$cond, i32:$t, i32:$f),
              (OR (SELNEZ i32:$t, i32:$cond),
                  (SELEQZ i32:$f, i32:$cond))>,
              ISA_MIPS32R6;
def : MipsPat<(select i32:$cond, i32:$t, immz),
              (SELNEZ i32:$t, i32:$cond)>,
              ISA_MIPS32R6;
def : MipsPat<(select i32:$cond, immz, i32:$f),
              (SELEQZ i32:$f, i32:$cond)>,
              ISA_MIPS32R6;
}

// Pseudo instructions
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, hasDelaySlot = 1,
    hasExtraSrcRegAllocReq = 1, isCTI = 1, Defs = [AT], hasPostISelHook = 1 in {
  class TailCallRegR6<Instruction JumpInst, Register RT, RegisterOperand RO> :
    PseudoSE<(outs), (ins RO:$rs), [(MipsTailCall RO:$rs)], II_JR>,
    PseudoInstExpansion<(JumpInst RT:$rt, RO:$rs)>;
}

class PseudoIndirectBranchBaseR6<Instruction JumpInst, Register RT,
                                 RegisterOperand RO> :
    MipsPseudo<(outs), (ins RO:$rs), [(brind RO:$rs)],
               II_IndirectBranchPseudo>,
    PseudoInstExpansion<(JumpInst RT:$rt, RO:$rs)> {
  let isTerminator=1;
  let isBarrier=1;
  let hasDelaySlot = 1;
  let isBranch = 1;
  let isIndirectBranch = 1;
  bit isCTI = 1;
}


let AdditionalPredicates = [NotInMips16Mode, NotInMicroMips,
                            NoIndirectJumpGuards] in {
  def TAILCALLR6REG : TailCallRegR6<JALR, ZERO, GPR32Opnd>, ISA_MIPS32R6;
  def PseudoIndirectBranchR6 : PseudoIndirectBranchBaseR6<JALR, ZERO,
                                                          GPR32Opnd>,
                               ISA_MIPS32R6;
}

let AdditionalPredicates = [NotInMips16Mode, NotInMicroMips,
                            UseIndirectJumpsHazard] in {
  def TAILCALLHBR6REG : TailCallReg<JR_HB_R6, GPR32Opnd>, ISA_MIPS32R6;
  def PseudoIndrectHazardBranchR6 : PseudoIndirectBranchBase<JR_HB_R6,
                                                             GPR32Opnd>,
                                    ISA_MIPS32R6;
}

