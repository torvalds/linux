//=- AArch64SVEInstrInfo.td -  AArch64 SVE Instructions -*- tablegen -*-----=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AArch64 Scalable Vector Extension (SVE) Instruction definitions.
//
//===----------------------------------------------------------------------===//

let Predicates = [HasSVE] in {

  def RDFFR_PPz  : sve_int_rdffr_pred<0b0, "rdffr">;
  def RDFFRS_PPz : sve_int_rdffr_pred<0b1, "rdffrs">;
  def RDFFR_P    : sve_int_rdffr_unpred<"rdffr">;
  def SETFFR     : sve_int_setffr<"setffr">;
  def WRFFR      : sve_int_wrffr<"wrffr">;

  defm ADD_ZZZ   : sve_int_bin_cons_arit_0<0b000, "add">;
  defm SUB_ZZZ   : sve_int_bin_cons_arit_0<0b001, "sub">;
  defm SQADD_ZZZ : sve_int_bin_cons_arit_0<0b100, "sqadd">;
  defm UQADD_ZZZ : sve_int_bin_cons_arit_0<0b101, "uqadd">;
  defm SQSUB_ZZZ : sve_int_bin_cons_arit_0<0b110, "sqsub">;
  defm UQSUB_ZZZ : sve_int_bin_cons_arit_0<0b111, "uqsub">;

  def AND_ZZZ : sve_int_bin_cons_log<0b00, "and">;
  def ORR_ZZZ : sve_int_bin_cons_log<0b01, "orr">;
  def EOR_ZZZ : sve_int_bin_cons_log<0b10, "eor">;
  def BIC_ZZZ : sve_int_bin_cons_log<0b11, "bic">;

  defm ADD_ZPmZ   : sve_int_bin_pred_arit_0<0b000, "add">;
  defm SUB_ZPmZ   : sve_int_bin_pred_arit_0<0b001, "sub">;
  defm SUBR_ZPmZ  : sve_int_bin_pred_arit_0<0b011, "subr">;

  defm ORR_ZPmZ : sve_int_bin_pred_log<0b000, "orr">;
  defm EOR_ZPmZ : sve_int_bin_pred_log<0b001, "eor">;
  defm AND_ZPmZ : sve_int_bin_pred_log<0b010, "and">;
  defm BIC_ZPmZ : sve_int_bin_pred_log<0b011, "bic">;

  defm ADD_ZI   : sve_int_arith_imm0<0b000, "add">;
  defm SUB_ZI   : sve_int_arith_imm0<0b001, "sub">;
  defm SUBR_ZI  : sve_int_arith_imm0<0b011, "subr">;
  defm SQADD_ZI : sve_int_arith_imm0<0b100, "sqadd">;
  defm UQADD_ZI : sve_int_arith_imm0<0b101, "uqadd">;
  defm SQSUB_ZI : sve_int_arith_imm0<0b110, "sqsub">;
  defm UQSUB_ZI : sve_int_arith_imm0<0b111, "uqsub">;

  defm MAD_ZPmZZ : sve_int_mladdsub_vvv_pred<0b0, "mad">;
  defm MSB_ZPmZZ : sve_int_mladdsub_vvv_pred<0b1, "msb">;
  defm MLA_ZPmZZ : sve_int_mlas_vvv_pred<0b0, "mla">;
  defm MLS_ZPmZZ : sve_int_mlas_vvv_pred<0b1, "mls">;

  // SVE predicated integer reductions.
  defm SADDV_VPZ : sve_int_reduce_0_saddv<0b000, "saddv">;
  defm UADDV_VPZ : sve_int_reduce_0_uaddv<0b001, "uaddv">;
  defm SMAXV_VPZ : sve_int_reduce_1<0b000, "smaxv">;
  defm UMAXV_VPZ : sve_int_reduce_1<0b001, "umaxv">;
  defm SMINV_VPZ : sve_int_reduce_1<0b010, "sminv">;
  defm UMINV_VPZ : sve_int_reduce_1<0b011, "uminv">;
  defm ORV_VPZ   : sve_int_reduce_2<0b000, "orv">;
  defm EORV_VPZ  : sve_int_reduce_2<0b001, "eorv">;
  defm ANDV_VPZ  : sve_int_reduce_2<0b010, "andv">;

  defm ORR_ZI : sve_int_log_imm<0b00, "orr", "orn">;
  defm EOR_ZI : sve_int_log_imm<0b01, "eor", "eon">;
  defm AND_ZI : sve_int_log_imm<0b10, "and", "bic">;

  defm SMAX_ZI   : sve_int_arith_imm1<0b00, "smax", simm8>;
  defm SMIN_ZI   : sve_int_arith_imm1<0b10, "smin", simm8>;
  defm UMAX_ZI   : sve_int_arith_imm1<0b01, "umax", imm0_255>;
  defm UMIN_ZI   : sve_int_arith_imm1<0b11, "umin", imm0_255>;

  defm MUL_ZI    : sve_int_arith_imm2<"mul">;
  defm MUL_ZPmZ   : sve_int_bin_pred_arit_2<0b000, "mul">;
  defm SMULH_ZPmZ : sve_int_bin_pred_arit_2<0b010, "smulh">;
  defm UMULH_ZPmZ : sve_int_bin_pred_arit_2<0b011, "umulh">;

  defm SDIV_ZPmZ  : sve_int_bin_pred_arit_2_div<0b100, "sdiv">;
  defm UDIV_ZPmZ  : sve_int_bin_pred_arit_2_div<0b101, "udiv">;
  defm SDIVR_ZPmZ : sve_int_bin_pred_arit_2_div<0b110, "sdivr">;
  defm UDIVR_ZPmZ : sve_int_bin_pred_arit_2_div<0b111, "udivr">;

  defm SDOT_ZZZ : sve_intx_dot<0b0, "sdot">;
  defm UDOT_ZZZ : sve_intx_dot<0b1, "udot">;

  defm SDOT_ZZZI : sve_intx_dot_by_indexed_elem<0b0, "sdot">;
  defm UDOT_ZZZI : sve_intx_dot_by_indexed_elem<0b1, "udot">;

  defm SXTB_ZPmZ : sve_int_un_pred_arit_0_h<0b000, "sxtb">;
  defm UXTB_ZPmZ : sve_int_un_pred_arit_0_h<0b001, "uxtb">;
  defm SXTH_ZPmZ : sve_int_un_pred_arit_0_w<0b010, "sxth">;
  defm UXTH_ZPmZ : sve_int_un_pred_arit_0_w<0b011, "uxth">;
  defm SXTW_ZPmZ : sve_int_un_pred_arit_0_d<0b100, "sxtw">;
  defm UXTW_ZPmZ : sve_int_un_pred_arit_0_d<0b101, "uxtw">;
  defm ABS_ZPmZ  : sve_int_un_pred_arit_0<  0b110, "abs">;
  defm NEG_ZPmZ  : sve_int_un_pred_arit_0<  0b111, "neg">;

  defm CLS_ZPmZ  : sve_int_un_pred_arit_1<   0b000, "cls">;
  defm CLZ_ZPmZ  : sve_int_un_pred_arit_1<   0b001, "clz">;
  defm CNT_ZPmZ  : sve_int_un_pred_arit_1<   0b010, "cnt">;
  defm CNOT_ZPmZ : sve_int_un_pred_arit_1<   0b011, "cnot">;
  defm NOT_ZPmZ  : sve_int_un_pred_arit_1<   0b110, "not">;
  defm FABS_ZPmZ : sve_int_un_pred_arit_1_fp<0b100, "fabs">;
  defm FNEG_ZPmZ : sve_int_un_pred_arit_1_fp<0b101, "fneg">;

  defm SMAX_ZPmZ : sve_int_bin_pred_arit_1<0b000, "smax">;
  defm UMAX_ZPmZ : sve_int_bin_pred_arit_1<0b001, "umax">;
  defm SMIN_ZPmZ : sve_int_bin_pred_arit_1<0b010, "smin">;
  defm UMIN_ZPmZ : sve_int_bin_pred_arit_1<0b011, "umin">;
  defm SABD_ZPmZ : sve_int_bin_pred_arit_1<0b100, "sabd">;
  defm UABD_ZPmZ : sve_int_bin_pred_arit_1<0b101, "uabd">;

  defm FRECPE_ZZ  : sve_fp_2op_u_zd<0b110, "frecpe">;
  defm FRSQRTE_ZZ : sve_fp_2op_u_zd<0b111, "frsqrte">;

  defm FADD_ZPmI    : sve_fp_2op_i_p_zds<0b000, "fadd", sve_fpimm_half_one>;
  defm FSUB_ZPmI    : sve_fp_2op_i_p_zds<0b001, "fsub", sve_fpimm_half_one>;
  defm FMUL_ZPmI    : sve_fp_2op_i_p_zds<0b010, "fmul", sve_fpimm_half_two>;
  defm FSUBR_ZPmI   : sve_fp_2op_i_p_zds<0b011, "fsubr", sve_fpimm_half_one>;
  defm FMAXNM_ZPmI  : sve_fp_2op_i_p_zds<0b100, "fmaxnm", sve_fpimm_zero_one>;
  defm FMINNM_ZPmI  : sve_fp_2op_i_p_zds<0b101, "fminnm", sve_fpimm_zero_one>;
  defm FMAX_ZPmI    : sve_fp_2op_i_p_zds<0b110, "fmax", sve_fpimm_zero_one>;
  defm FMIN_ZPmI    : sve_fp_2op_i_p_zds<0b111, "fmin", sve_fpimm_zero_one>;

  defm FADD_ZPmZ   : sve_fp_2op_p_zds<0b0000, "fadd">;
  defm FSUB_ZPmZ   : sve_fp_2op_p_zds<0b0001, "fsub">;
  defm FMUL_ZPmZ   : sve_fp_2op_p_zds<0b0010, "fmul">;
  defm FSUBR_ZPmZ  : sve_fp_2op_p_zds<0b0011, "fsubr">;
  defm FMAXNM_ZPmZ : sve_fp_2op_p_zds<0b0100, "fmaxnm">;
  defm FMINNM_ZPmZ : sve_fp_2op_p_zds<0b0101, "fminnm">;
  defm FMAX_ZPmZ   : sve_fp_2op_p_zds<0b0110, "fmax">;
  defm FMIN_ZPmZ   : sve_fp_2op_p_zds<0b0111, "fmin">;
  defm FABD_ZPmZ   : sve_fp_2op_p_zds<0b1000, "fabd">;
  defm FSCALE_ZPmZ : sve_fp_2op_p_zds<0b1001, "fscale">;
  defm FMULX_ZPmZ  : sve_fp_2op_p_zds<0b1010, "fmulx">;
  defm FDIVR_ZPmZ  : sve_fp_2op_p_zds<0b1100, "fdivr">;
  defm FDIV_ZPmZ   : sve_fp_2op_p_zds<0b1101, "fdiv">;

  defm FADD_ZZZ    : sve_fp_3op_u_zd<0b000, "fadd">;
  defm FSUB_ZZZ    : sve_fp_3op_u_zd<0b001, "fsub">;
  defm FMUL_ZZZ    : sve_fp_3op_u_zd<0b010, "fmul">;
  defm FTSMUL_ZZZ  : sve_fp_3op_u_zd<0b011, "ftsmul">;
  defm FRECPS_ZZZ  : sve_fp_3op_u_zd<0b110, "frecps">;
  defm FRSQRTS_ZZZ : sve_fp_3op_u_zd<0b111, "frsqrts">;

  defm FTSSEL_ZZZ : sve_int_bin_cons_misc_0_b<"ftssel">;

  defm FCADD_ZPmZ : sve_fp_fcadd<"fcadd">;
  defm FCMLA_ZPmZZ : sve_fp_fcmla<"fcmla">;

  defm FMLA_ZPmZZ  : sve_fp_3op_p_zds_a<0b00, "fmla">;
  defm FMLS_ZPmZZ  : sve_fp_3op_p_zds_a<0b01, "fmls">;
  defm FNMLA_ZPmZZ : sve_fp_3op_p_zds_a<0b10, "fnmla">;
  defm FNMLS_ZPmZZ : sve_fp_3op_p_zds_a<0b11, "fnmls">;

  defm FMAD_ZPmZZ  : sve_fp_3op_p_zds_b<0b00, "fmad">;
  defm FMSB_ZPmZZ  : sve_fp_3op_p_zds_b<0b01, "fmsb">;
  defm FNMAD_ZPmZZ : sve_fp_3op_p_zds_b<0b10, "fnmad">;
  defm FNMSB_ZPmZZ : sve_fp_3op_p_zds_b<0b11, "fnmsb">;

  defm FTMAD_ZZI : sve_fp_ftmad<"ftmad">;

  defm FMLA_ZZZI : sve_fp_fma_by_indexed_elem<0b0, "fmla">;
  defm FMLS_ZZZI : sve_fp_fma_by_indexed_elem<0b1, "fmls">;

  defm FCMLA_ZZZI : sve_fp_fcmla_by_indexed_elem<"fcmla">;
  defm FMUL_ZZZI   : sve_fp_fmul_by_indexed_elem<"fmul">;

  // SVE floating point reductions.
  defm FADDA_VPZ   : sve_fp_2op_p_vd<0b000, "fadda">;
  defm FADDV_VPZ   : sve_fp_fast_red<0b000, "faddv">;
  defm FMAXNMV_VPZ : sve_fp_fast_red<0b100, "fmaxnmv">;
  defm FMINNMV_VPZ : sve_fp_fast_red<0b101, "fminnmv">;
  defm FMAXV_VPZ   : sve_fp_fast_red<0b110, "fmaxv">;
  defm FMINV_VPZ   : sve_fp_fast_red<0b111, "fminv">;

  // Splat immediate (unpredicated)
  defm DUP_ZI   : sve_int_dup_imm<"dup">;
  defm FDUP_ZI  : sve_int_dup_fpimm<"fdup">;
  defm DUPM_ZI : sve_int_dup_mask_imm<"dupm">;

  // Splat immediate (predicated)
  defm CPY_ZPmI  : sve_int_dup_imm_pred_merge<"cpy">;
  defm CPY_ZPzI  : sve_int_dup_imm_pred_zero<"cpy">;
  defm FCPY_ZPmI : sve_int_dup_fpimm_pred<"fcpy">;

  // Splat scalar register (unpredicated, GPR or vector + element index)
  defm DUP_ZR  : sve_int_perm_dup_r<"dup">;
  defm DUP_ZZI : sve_int_perm_dup_i<"dup">;

  // Splat scalar register (predicated)
  defm CPY_ZPmR : sve_int_perm_cpy_r<"cpy">;
  defm CPY_ZPmV : sve_int_perm_cpy_v<"cpy">;

  // Select elements from either vector (predicated)
  defm SEL_ZPZZ    : sve_int_sel_vvv<"sel">;

  defm SPLICE_ZPZ : sve_int_perm_splice<"splice">;
  defm COMPACT_ZPZ : sve_int_perm_compact<"compact">;
  defm INSR_ZR : sve_int_perm_insrs<"insr">;
  defm INSR_ZV : sve_int_perm_insrv<"insr">;
  def  EXT_ZZI : sve_int_perm_extract_i<"ext">;

  defm RBIT_ZPmZ : sve_int_perm_rev_rbit<"rbit">;
  defm REVB_ZPmZ : sve_int_perm_rev_revb<"revb">;
  defm REVH_ZPmZ : sve_int_perm_rev_revh<"revh">;
  defm REVW_ZPmZ : sve_int_perm_rev_revw<"revw">;

  defm REV_PP : sve_int_perm_reverse_p<"rev">;
  defm REV_ZZ : sve_int_perm_reverse_z<"rev">;

  defm SUNPKLO_ZZ : sve_int_perm_unpk<0b00, "sunpklo">;
  defm SUNPKHI_ZZ : sve_int_perm_unpk<0b01, "sunpkhi">;
  defm UUNPKLO_ZZ : sve_int_perm_unpk<0b10, "uunpklo">;
  defm UUNPKHI_ZZ : sve_int_perm_unpk<0b11, "uunpkhi">;

  def  PUNPKLO_PP : sve_int_perm_punpk<0b0, "punpklo">;
  def  PUNPKHI_PP : sve_int_perm_punpk<0b1, "punpkhi">;

  defm MOVPRFX_ZPzZ : sve_int_movprfx_pred_zero<0b000, "movprfx">;
  defm MOVPRFX_ZPmZ : sve_int_movprfx_pred_merge<0b001, "movprfx">;
  def MOVPRFX_ZZ : sve_int_bin_cons_misc_0_c<0b00000001, "movprfx", ZPRAny>;
  def FEXPA_ZZ_H : sve_int_bin_cons_misc_0_c<0b01000000, "fexpa", ZPR16>;
  def FEXPA_ZZ_S : sve_int_bin_cons_misc_0_c<0b10000000, "fexpa", ZPR32>;
  def FEXPA_ZZ_D : sve_int_bin_cons_misc_0_c<0b11000000, "fexpa", ZPR64>;

  def BRKPA_PPzPP  : sve_int_brkp<0b00, "brkpa">;
  def BRKPAS_PPzPP : sve_int_brkp<0b10, "brkpas">;
  def BRKPB_PPzPP  : sve_int_brkp<0b01, "brkpb">;
  def BRKPBS_PPzPP : sve_int_brkp<0b11, "brkpbs">;

  def BRKN_PPzP    : sve_int_brkn<0b0, "brkn">;
  def BRKNS_PPzP   : sve_int_brkn<0b1, "brkns">;

  defm BRKA_PPzP  : sve_int_break_z<0b000, "brka">;
  defm BRKA_PPmP  : sve_int_break_m<0b001, "brka">;
  defm BRKAS_PPzP : sve_int_break_z<0b010, "brkas">;
  defm BRKB_PPzP  : sve_int_break_z<0b100, "brkb">;
  defm BRKB_PPmP  : sve_int_break_m<0b101, "brkb">;
  defm BRKBS_PPzP : sve_int_break_z<0b110, "brkbs">;

  def PTEST_PP : sve_int_ptest<0b010000, "ptest">;
  def PFALSE   : sve_int_pfalse<0b000000, "pfalse">;
  defm PFIRST  : sve_int_pfirst<0b00000, "pfirst">;
  defm PNEXT   : sve_int_pnext<0b00110, "pnext">;

  def AND_PPzPP   : sve_int_pred_log<0b0000, "and">;
  def BIC_PPzPP   : sve_int_pred_log<0b0001, "bic">;
  def EOR_PPzPP   : sve_int_pred_log<0b0010, "eor">;
  def SEL_PPPP    : sve_int_pred_log<0b0011, "sel">;
  def ANDS_PPzPP  : sve_int_pred_log<0b0100, "ands">;
  def BICS_PPzPP  : sve_int_pred_log<0b0101, "bics">;
  def EORS_PPzPP  : sve_int_pred_log<0b0110, "eors">;
  def ORR_PPzPP   : sve_int_pred_log<0b1000, "orr">;
  def ORN_PPzPP   : sve_int_pred_log<0b1001, "orn">;
  def NOR_PPzPP   : sve_int_pred_log<0b1010, "nor">;
  def NAND_PPzPP  : sve_int_pred_log<0b1011, "nand">;
  def ORRS_PPzPP  : sve_int_pred_log<0b1100, "orrs">;
  def ORNS_PPzPP  : sve_int_pred_log<0b1101, "orns">;
  def NORS_PPzPP  : sve_int_pred_log<0b1110, "nors">;
  def NANDS_PPzPP : sve_int_pred_log<0b1111, "nands">;

  defm CLASTA_RPZ : sve_int_perm_clast_rz<0, "clasta">;
  defm CLASTB_RPZ : sve_int_perm_clast_rz<1, "clastb">;
  defm CLASTA_VPZ : sve_int_perm_clast_vz<0, "clasta">;
  defm CLASTB_VPZ : sve_int_perm_clast_vz<1, "clastb">;
  defm CLASTA_ZPZ : sve_int_perm_clast_zz<0, "clasta">;
  defm CLASTB_ZPZ : sve_int_perm_clast_zz<1, "clastb">;

  defm LASTA_RPZ : sve_int_perm_last_r<0, "lasta">;
  defm LASTB_RPZ : sve_int_perm_last_r<1, "lastb">;
  defm LASTA_VPZ : sve_int_perm_last_v<0, "lasta">;
  defm LASTB_VPZ : sve_int_perm_last_v<1, "lastb">;

  // continuous load with reg+immediate
  defm LD1B_IMM    : sve_mem_cld_si<0b0000, "ld1b",  Z_b, ZPR8>;
  defm LD1B_H_IMM  : sve_mem_cld_si<0b0001, "ld1b",  Z_h, ZPR16>;
  defm LD1B_S_IMM  : sve_mem_cld_si<0b0010, "ld1b",  Z_s, ZPR32>;
  defm LD1B_D_IMM  : sve_mem_cld_si<0b0011, "ld1b",  Z_d, ZPR64>;
  defm LD1SW_D_IMM : sve_mem_cld_si<0b0100, "ld1sw", Z_d, ZPR64>;
  defm LD1H_IMM    : sve_mem_cld_si<0b0101, "ld1h",  Z_h, ZPR16>;
  defm LD1H_S_IMM  : sve_mem_cld_si<0b0110, "ld1h",  Z_s, ZPR32>;
  defm LD1H_D_IMM  : sve_mem_cld_si<0b0111, "ld1h",  Z_d, ZPR64>;
  defm LD1SH_D_IMM : sve_mem_cld_si<0b1000, "ld1sh", Z_d, ZPR64>;
  defm LD1SH_S_IMM : sve_mem_cld_si<0b1001, "ld1sh", Z_s, ZPR32>;
  defm LD1W_IMM    : sve_mem_cld_si<0b1010, "ld1w",  Z_s, ZPR32>;
  defm LD1W_D_IMM  : sve_mem_cld_si<0b1011, "ld1w",  Z_d, ZPR64>;
  defm LD1SB_D_IMM : sve_mem_cld_si<0b1100, "ld1sb", Z_d, ZPR64>;
  defm LD1SB_S_IMM : sve_mem_cld_si<0b1101, "ld1sb", Z_s, ZPR32>;
  defm LD1SB_H_IMM : sve_mem_cld_si<0b1110, "ld1sb", Z_h, ZPR16>;
  defm LD1D_IMM    : sve_mem_cld_si<0b1111, "ld1d",  Z_d, ZPR64>;

  // LD1R loads (splat scalar to vector)
  defm LD1RB_IMM    : sve_mem_ld_dup<0b00, 0b00, "ld1rb",  Z_b, ZPR8,  uimm6s1>;
  defm LD1RB_H_IMM  : sve_mem_ld_dup<0b00, 0b01, "ld1rb",  Z_h, ZPR16, uimm6s1>;
  defm LD1RB_S_IMM  : sve_mem_ld_dup<0b00, 0b10, "ld1rb",  Z_s, ZPR32, uimm6s1>;
  defm LD1RB_D_IMM  : sve_mem_ld_dup<0b00, 0b11, "ld1rb",  Z_d, ZPR64, uimm6s1>;
  defm LD1RSW_IMM   : sve_mem_ld_dup<0b01, 0b00, "ld1rsw", Z_d, ZPR64, uimm6s4>;
  defm LD1RH_IMM    : sve_mem_ld_dup<0b01, 0b01, "ld1rh",  Z_h, ZPR16, uimm6s2>;
  defm LD1RH_S_IMM  : sve_mem_ld_dup<0b01, 0b10, "ld1rh",  Z_s, ZPR32, uimm6s2>;
  defm LD1RH_D_IMM  : sve_mem_ld_dup<0b01, 0b11, "ld1rh",  Z_d, ZPR64, uimm6s2>;
  defm LD1RSH_D_IMM : sve_mem_ld_dup<0b10, 0b00, "ld1rsh", Z_d, ZPR64, uimm6s2>;
  defm LD1RSH_S_IMM : sve_mem_ld_dup<0b10, 0b01, "ld1rsh", Z_s, ZPR32, uimm6s2>;
  defm LD1RW_IMM    : sve_mem_ld_dup<0b10, 0b10, "ld1rw",  Z_s, ZPR32, uimm6s4>;
  defm LD1RW_D_IMM  : sve_mem_ld_dup<0b10, 0b11, "ld1rw",  Z_d, ZPR64, uimm6s4>;
  defm LD1RSB_D_IMM : sve_mem_ld_dup<0b11, 0b00, "ld1rsb", Z_d, ZPR64, uimm6s1>;
  defm LD1RSB_S_IMM : sve_mem_ld_dup<0b11, 0b01, "ld1rsb", Z_s, ZPR32, uimm6s1>;
  defm LD1RSB_H_IMM : sve_mem_ld_dup<0b11, 0b10, "ld1rsb", Z_h, ZPR16, uimm6s1>;
  defm LD1RD_IMM    : sve_mem_ld_dup<0b11, 0b11, "ld1rd",  Z_d, ZPR64, uimm6s8>;

  // LD1RQ loads (load quadword-vector and splat to scalable vector)
  defm LD1RQ_B_IMM  : sve_mem_ldqr_si<0b00, "ld1rqb", Z_b, ZPR8>;
  defm LD1RQ_H_IMM  : sve_mem_ldqr_si<0b01, "ld1rqh", Z_h, ZPR16>;
  defm LD1RQ_W_IMM  : sve_mem_ldqr_si<0b10, "ld1rqw", Z_s, ZPR32>;
  defm LD1RQ_D_IMM  : sve_mem_ldqr_si<0b11, "ld1rqd", Z_d, ZPR64>;
  defm LD1RQ_B      : sve_mem_ldqr_ss<0b00, "ld1rqb", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LD1RQ_H      : sve_mem_ldqr_ss<0b01, "ld1rqh", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LD1RQ_W      : sve_mem_ldqr_ss<0b10, "ld1rqw", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LD1RQ_D      : sve_mem_ldqr_ss<0b11, "ld1rqd", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // continuous load with reg+reg addressing.
  defm LD1B    : sve_mem_cld_ss<0b0000, "ld1b",  Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LD1B_H  : sve_mem_cld_ss<0b0001, "ld1b",  Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm LD1B_S  : sve_mem_cld_ss<0b0010, "ld1b",  Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm LD1B_D  : sve_mem_cld_ss<0b0011, "ld1b",  Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm LD1SW_D : sve_mem_cld_ss<0b0100, "ld1sw", Z_d, ZPR64, GPR64NoXZRshifted32>;
  defm LD1H    : sve_mem_cld_ss<0b0101, "ld1h",  Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LD1H_S  : sve_mem_cld_ss<0b0110, "ld1h",  Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm LD1H_D  : sve_mem_cld_ss<0b0111, "ld1h",  Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm LD1SH_D : sve_mem_cld_ss<0b1000, "ld1sh", Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm LD1SH_S : sve_mem_cld_ss<0b1001, "ld1sh", Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm LD1W    : sve_mem_cld_ss<0b1010, "ld1w",  Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LD1W_D  : sve_mem_cld_ss<0b1011, "ld1w",  Z_d, ZPR64, GPR64NoXZRshifted32>;
  defm LD1SB_D : sve_mem_cld_ss<0b1100, "ld1sb", Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm LD1SB_S : sve_mem_cld_ss<0b1101, "ld1sb", Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm LD1SB_H : sve_mem_cld_ss<0b1110, "ld1sb", Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm LD1D    : sve_mem_cld_ss<0b1111, "ld1d",  Z_d, ZPR64, GPR64NoXZRshifted64>;

  // non-faulting continuous load with reg+immediate
  defm LDNF1B_IMM    : sve_mem_cldnf_si<0b0000, "ldnf1b",  Z_b, ZPR8>;
  defm LDNF1B_H_IMM  : sve_mem_cldnf_si<0b0001, "ldnf1b",  Z_h, ZPR16>;
  defm LDNF1B_S_IMM  : sve_mem_cldnf_si<0b0010, "ldnf1b",  Z_s, ZPR32>;
  defm LDNF1B_D_IMM  : sve_mem_cldnf_si<0b0011, "ldnf1b",  Z_d, ZPR64>;
  defm LDNF1SW_D_IMM : sve_mem_cldnf_si<0b0100, "ldnf1sw", Z_d, ZPR64>;
  defm LDNF1H_IMM    : sve_mem_cldnf_si<0b0101, "ldnf1h",  Z_h, ZPR16>;
  defm LDNF1H_S_IMM  : sve_mem_cldnf_si<0b0110, "ldnf1h",  Z_s, ZPR32>;
  defm LDNF1H_D_IMM  : sve_mem_cldnf_si<0b0111, "ldnf1h",  Z_d, ZPR64>;
  defm LDNF1SH_D_IMM : sve_mem_cldnf_si<0b1000, "ldnf1sh", Z_d, ZPR64>;
  defm LDNF1SH_S_IMM : sve_mem_cldnf_si<0b1001, "ldnf1sh", Z_s, ZPR32>;
  defm LDNF1W_IMM    : sve_mem_cldnf_si<0b1010, "ldnf1w",  Z_s, ZPR32>;
  defm LDNF1W_D_IMM  : sve_mem_cldnf_si<0b1011, "ldnf1w",  Z_d, ZPR64>;
  defm LDNF1SB_D_IMM : sve_mem_cldnf_si<0b1100, "ldnf1sb", Z_d, ZPR64>;
  defm LDNF1SB_S_IMM : sve_mem_cldnf_si<0b1101, "ldnf1sb", Z_s, ZPR32>;
  defm LDNF1SB_H_IMM : sve_mem_cldnf_si<0b1110, "ldnf1sb", Z_h, ZPR16>;
  defm LDNF1D_IMM    : sve_mem_cldnf_si<0b1111, "ldnf1d",  Z_d, ZPR64>;

  // First-faulting loads with reg+reg addressing.
  defm LDFF1B    : sve_mem_cldff_ss<0b0000, "ldff1b",  Z_b, ZPR8,  GPR64shifted8>;
  defm LDFF1B_H  : sve_mem_cldff_ss<0b0001, "ldff1b",  Z_h, ZPR16, GPR64shifted8>;
  defm LDFF1B_S  : sve_mem_cldff_ss<0b0010, "ldff1b",  Z_s, ZPR32, GPR64shifted8>;
  defm LDFF1B_D  : sve_mem_cldff_ss<0b0011, "ldff1b",  Z_d, ZPR64, GPR64shifted8>;
  defm LDFF1SW_D : sve_mem_cldff_ss<0b0100, "ldff1sw", Z_d, ZPR64, GPR64shifted32>;
  defm LDFF1H    : sve_mem_cldff_ss<0b0101, "ldff1h",  Z_h, ZPR16, GPR64shifted16>;
  defm LDFF1H_S  : sve_mem_cldff_ss<0b0110, "ldff1h",  Z_s, ZPR32, GPR64shifted16>;
  defm LDFF1H_D  : sve_mem_cldff_ss<0b0111, "ldff1h",  Z_d, ZPR64, GPR64shifted16>;
  defm LDFF1SH_D : sve_mem_cldff_ss<0b1000, "ldff1sh", Z_d, ZPR64, GPR64shifted16>;
  defm LDFF1SH_S : sve_mem_cldff_ss<0b1001, "ldff1sh", Z_s, ZPR32, GPR64shifted16>;
  defm LDFF1W    : sve_mem_cldff_ss<0b1010, "ldff1w",  Z_s, ZPR32, GPR64shifted32>;
  defm LDFF1W_D  : sve_mem_cldff_ss<0b1011, "ldff1w",  Z_d, ZPR64, GPR64shifted32>;
  defm LDFF1SB_D : sve_mem_cldff_ss<0b1100, "ldff1sb", Z_d, ZPR64, GPR64shifted8>;
  defm LDFF1SB_S : sve_mem_cldff_ss<0b1101, "ldff1sb", Z_s, ZPR32, GPR64shifted8>;
  defm LDFF1SB_H : sve_mem_cldff_ss<0b1110, "ldff1sb", Z_h, ZPR16, GPR64shifted8>;
  defm LDFF1D    : sve_mem_cldff_ss<0b1111, "ldff1d",  Z_d, ZPR64, GPR64shifted64>;

  // LD(2|3|4) structured loads with reg+immediate
  defm LD2B_IMM : sve_mem_eld_si<0b00, 0b01, ZZ_b,   "ld2b", simm4s2>;
  defm LD3B_IMM : sve_mem_eld_si<0b00, 0b10, ZZZ_b,  "ld3b", simm4s3>;
  defm LD4B_IMM : sve_mem_eld_si<0b00, 0b11, ZZZZ_b, "ld4b", simm4s4>;
  defm LD2H_IMM : sve_mem_eld_si<0b01, 0b01, ZZ_h,   "ld2h", simm4s2>;
  defm LD3H_IMM : sve_mem_eld_si<0b01, 0b10, ZZZ_h,  "ld3h", simm4s3>;
  defm LD4H_IMM : sve_mem_eld_si<0b01, 0b11, ZZZZ_h, "ld4h", simm4s4>;
  defm LD2W_IMM : sve_mem_eld_si<0b10, 0b01, ZZ_s,   "ld2w", simm4s2>;
  defm LD3W_IMM : sve_mem_eld_si<0b10, 0b10, ZZZ_s,  "ld3w", simm4s3>;
  defm LD4W_IMM : sve_mem_eld_si<0b10, 0b11, ZZZZ_s, "ld4w", simm4s4>;
  defm LD2D_IMM : sve_mem_eld_si<0b11, 0b01, ZZ_d,   "ld2d", simm4s2>;
  defm LD3D_IMM : sve_mem_eld_si<0b11, 0b10, ZZZ_d,  "ld3d", simm4s3>;
  defm LD4D_IMM : sve_mem_eld_si<0b11, 0b11, ZZZZ_d, "ld4d", simm4s4>;

  // LD(2|3|4) structured loads (register + register)
  def LD2B : sve_mem_eld_ss<0b00, 0b01, ZZ_b,   "ld2b", GPR64NoXZRshifted8>;
  def LD3B : sve_mem_eld_ss<0b00, 0b10, ZZZ_b,  "ld3b", GPR64NoXZRshifted8>;
  def LD4B : sve_mem_eld_ss<0b00, 0b11, ZZZZ_b, "ld4b", GPR64NoXZRshifted8>;
  def LD2H : sve_mem_eld_ss<0b01, 0b01, ZZ_h,   "ld2h", GPR64NoXZRshifted16>;
  def LD3H : sve_mem_eld_ss<0b01, 0b10, ZZZ_h,  "ld3h", GPR64NoXZRshifted16>;
  def LD4H : sve_mem_eld_ss<0b01, 0b11, ZZZZ_h, "ld4h", GPR64NoXZRshifted16>;
  def LD2W : sve_mem_eld_ss<0b10, 0b01, ZZ_s,   "ld2w", GPR64NoXZRshifted32>;
  def LD3W : sve_mem_eld_ss<0b10, 0b10, ZZZ_s,  "ld3w", GPR64NoXZRshifted32>;
  def LD4W : sve_mem_eld_ss<0b10, 0b11, ZZZZ_s, "ld4w", GPR64NoXZRshifted32>;
  def LD2D : sve_mem_eld_ss<0b11, 0b01, ZZ_d,   "ld2d", GPR64NoXZRshifted64>;
  def LD3D : sve_mem_eld_ss<0b11, 0b10, ZZZ_d,  "ld3d", GPR64NoXZRshifted64>;
  def LD4D : sve_mem_eld_ss<0b11, 0b11, ZZZZ_d, "ld4d", GPR64NoXZRshifted64>;

  // Gathers using unscaled 32-bit offsets, e.g.
  //    ld1h z0.s, p0/z, [x0, z0.s, uxtw]
  defm GLD1SB_S   : sve_mem_32b_gld_vs_32_unscaled<0b0000, "ld1sb",   ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only>;
  defm GLDFF1SB_S : sve_mem_32b_gld_vs_32_unscaled<0b0001, "ldff1sb", ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only>;
  defm GLD1B_S    : sve_mem_32b_gld_vs_32_unscaled<0b0010, "ld1b",    ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only>;
  defm GLDFF1B_S  : sve_mem_32b_gld_vs_32_unscaled<0b0011, "ldff1b",  ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only>;
  defm GLD1SH_S   : sve_mem_32b_gld_vs_32_unscaled<0b0100, "ld1sh",   ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm GLDFF1SH_S : sve_mem_32b_gld_vs_32_unscaled<0b0101, "ldff1sh", ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm GLD1H_S    : sve_mem_32b_gld_vs_32_unscaled<0b0110, "ld1h",    ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm GLDFF1H_S  : sve_mem_32b_gld_vs_32_unscaled<0b0111, "ldff1h",  ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm GLD1W      : sve_mem_32b_gld_vs_32_unscaled<0b1010, "ld1w",    ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm GLDFF1W    : sve_mem_32b_gld_vs_32_unscaled<0b1011, "ldff1w",  ZPR32ExtSXTW8, ZPR32ExtUXTW8>;

  // Gathers using scaled 32-bit offsets, e.g.
  //    ld1h z0.s, p0/z, [x0, z0.s, uxtw #1]
  defm GLD1SH_S   : sve_mem_32b_gld_sv_32_scaled<0b0100, "ld1sh",   ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm GLDFF1SH_S : sve_mem_32b_gld_sv_32_scaled<0b0101, "ldff1sh", ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm GLD1H_S    : sve_mem_32b_gld_sv_32_scaled<0b0110, "ld1h",    ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm GLDFF1H_S  : sve_mem_32b_gld_sv_32_scaled<0b0111, "ldff1h",  ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm GLD1W      : sve_mem_32b_gld_sv_32_scaled<0b1010, "ld1w",    ZPR32ExtSXTW32, ZPR32ExtUXTW32>;
  defm GLDFF1W    : sve_mem_32b_gld_sv_32_scaled<0b1011, "ldff1w",  ZPR32ExtSXTW32, ZPR32ExtUXTW32>;

  // Gathers using scaled 32-bit pointers with offset, e.g.
  //    ld1h z0.s, p0/z, [z0.s, #16]
  defm GLD1SB_S   : sve_mem_32b_gld_vi_32_ptrs<0b0000, "ld1sb",   imm0_31>;
  defm GLDFF1SB_S : sve_mem_32b_gld_vi_32_ptrs<0b0001, "ldff1sb", imm0_31>;
  defm GLD1B_S    : sve_mem_32b_gld_vi_32_ptrs<0b0010, "ld1b",    imm0_31>;
  defm GLDFF1B_S  : sve_mem_32b_gld_vi_32_ptrs<0b0011, "ldff1b",  imm0_31>;
  defm GLD1SH_S   : sve_mem_32b_gld_vi_32_ptrs<0b0100, "ld1sh",   uimm5s2>;
  defm GLDFF1SH_S : sve_mem_32b_gld_vi_32_ptrs<0b0101, "ldff1sh", uimm5s2>;
  defm GLD1H_S    : sve_mem_32b_gld_vi_32_ptrs<0b0110, "ld1h",    uimm5s2>;
  defm GLDFF1H_S  : sve_mem_32b_gld_vi_32_ptrs<0b0111, "ldff1h",  uimm5s2>;
  defm GLD1W      : sve_mem_32b_gld_vi_32_ptrs<0b1010, "ld1w",    uimm5s4>;
  defm GLDFF1W    : sve_mem_32b_gld_vi_32_ptrs<0b1011, "ldff1w",  uimm5s4>;

  // Gathers using scaled 64-bit pointers with offset, e.g.
  //    ld1h z0.d, p0/z, [z0.d, #16]
  defm GLD1SB_D   : sve_mem_64b_gld_vi_64_ptrs<0b0000, "ld1sb",   imm0_31>;
  defm GLDFF1SB_D : sve_mem_64b_gld_vi_64_ptrs<0b0001, "ldff1sb", imm0_31>;
  defm GLD1B_D    : sve_mem_64b_gld_vi_64_ptrs<0b0010, "ld1b",    imm0_31>;
  defm GLDFF1B_D  : sve_mem_64b_gld_vi_64_ptrs<0b0011, "ldff1b",  imm0_31>;
  defm GLD1SH_D   : sve_mem_64b_gld_vi_64_ptrs<0b0100, "ld1sh",   uimm5s2>;
  defm GLDFF1SH_D : sve_mem_64b_gld_vi_64_ptrs<0b0101, "ldff1sh", uimm5s2>;
  defm GLD1H_D    : sve_mem_64b_gld_vi_64_ptrs<0b0110, "ld1h",    uimm5s2>;
  defm GLDFF1H_D  : sve_mem_64b_gld_vi_64_ptrs<0b0111, "ldff1h",  uimm5s2>;
  defm GLD1SW_D   : sve_mem_64b_gld_vi_64_ptrs<0b1000, "ld1sw",   uimm5s4>;
  defm GLDFF1SW_D : sve_mem_64b_gld_vi_64_ptrs<0b1001, "ldff1sw", uimm5s4>;
  defm GLD1W_D    : sve_mem_64b_gld_vi_64_ptrs<0b1010, "ld1w",    uimm5s4>;
  defm GLDFF1W_D  : sve_mem_64b_gld_vi_64_ptrs<0b1011, "ldff1w",  uimm5s4>;
  defm GLD1D      : sve_mem_64b_gld_vi_64_ptrs<0b1110, "ld1d",    uimm5s8>;
  defm GLDFF1D    : sve_mem_64b_gld_vi_64_ptrs<0b1111, "ldff1d",  uimm5s8>;

  // Gathers using unscaled 64-bit offsets, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d]
  defm GLD1SB_D   : sve_mem_64b_gld_vs2_64_unscaled<0b0000, "ld1sb">;
  defm GLDFF1SB_D : sve_mem_64b_gld_vs2_64_unscaled<0b0001, "ldff1sb">;
  defm GLD1B_D    : sve_mem_64b_gld_vs2_64_unscaled<0b0010, "ld1b">;
  defm GLDFF1B_D  : sve_mem_64b_gld_vs2_64_unscaled<0b0011, "ldff1b">;
  defm GLD1SH_D   : sve_mem_64b_gld_vs2_64_unscaled<0b0100, "ld1sh">;
  defm GLDFF1SH_D : sve_mem_64b_gld_vs2_64_unscaled<0b0101, "ldff1sh">;
  defm GLD1H_D    : sve_mem_64b_gld_vs2_64_unscaled<0b0110, "ld1h">;
  defm GLDFF1H_D  : sve_mem_64b_gld_vs2_64_unscaled<0b0111, "ldff1h">;
  defm GLD1SW_D   : sve_mem_64b_gld_vs2_64_unscaled<0b1000, "ld1sw">;
  defm GLDFF1SW_D : sve_mem_64b_gld_vs2_64_unscaled<0b1001, "ldff1sw">;
  defm GLD1W_D    : sve_mem_64b_gld_vs2_64_unscaled<0b1010, "ld1w">;
  defm GLDFF1W_D  : sve_mem_64b_gld_vs2_64_unscaled<0b1011, "ldff1w">;
  defm GLD1D      : sve_mem_64b_gld_vs2_64_unscaled<0b1110, "ld1d">;
  defm GLDFF1D    : sve_mem_64b_gld_vs2_64_unscaled<0b1111, "ldff1d">;

  // Gathers using scaled 64-bit offsets, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, lsl #1]
  defm GLD1SH_D   : sve_mem_64b_gld_sv2_64_scaled<0b0100, "ld1sh",   ZPR64ExtLSL16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_sv2_64_scaled<0b0101, "ldff1sh", ZPR64ExtLSL16>;
  defm GLD1H_D    : sve_mem_64b_gld_sv2_64_scaled<0b0110, "ld1h",    ZPR64ExtLSL16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_sv2_64_scaled<0b0111, "ldff1h",  ZPR64ExtLSL16>;
  defm GLD1SW_D   : sve_mem_64b_gld_sv2_64_scaled<0b1000, "ld1sw",   ZPR64ExtLSL32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_sv2_64_scaled<0b1001, "ldff1sw", ZPR64ExtLSL32>;
  defm GLD1W_D    : sve_mem_64b_gld_sv2_64_scaled<0b1010, "ld1w",    ZPR64ExtLSL32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_sv2_64_scaled<0b1011, "ldff1w",  ZPR64ExtLSL32>;
  defm GLD1D      : sve_mem_64b_gld_sv2_64_scaled<0b1110, "ld1d",    ZPR64ExtLSL64>;
  defm GLDFF1D    : sve_mem_64b_gld_sv2_64_scaled<0b1111, "ldff1d",  ZPR64ExtLSL64>;

  // Gathers using unscaled 32-bit offsets unpacked in 64-bits elements, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, uxtw]
  defm GLD1SB_D   : sve_mem_64b_gld_vs_32_unscaled<0b0000, "ld1sb",   ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm GLDFF1SB_D : sve_mem_64b_gld_vs_32_unscaled<0b0001, "ldff1sb", ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm GLD1B_D    : sve_mem_64b_gld_vs_32_unscaled<0b0010, "ld1b",    ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm GLDFF1B_D  : sve_mem_64b_gld_vs_32_unscaled<0b0011, "ldff1b",  ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm GLD1SH_D   : sve_mem_64b_gld_vs_32_unscaled<0b0100, "ld1sh",   ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLDFF1SH_D : sve_mem_64b_gld_vs_32_unscaled<0b0101, "ldff1sh", ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLD1H_D    : sve_mem_64b_gld_vs_32_unscaled<0b0110, "ld1h",    ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLDFF1H_D  : sve_mem_64b_gld_vs_32_unscaled<0b0111, "ldff1h",  ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLD1SW_D   : sve_mem_64b_gld_vs_32_unscaled<0b1000, "ld1sw",   ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLDFF1SW_D : sve_mem_64b_gld_vs_32_unscaled<0b1001, "ldff1sw", ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLD1W_D    : sve_mem_64b_gld_vs_32_unscaled<0b1010, "ld1w",    ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLDFF1W_D  : sve_mem_64b_gld_vs_32_unscaled<0b1011, "ldff1w",  ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLD1D      : sve_mem_64b_gld_vs_32_unscaled<0b1110, "ld1d",    ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm GLDFF1D    : sve_mem_64b_gld_vs_32_unscaled<0b1111, "ldff1d",  ZPR64ExtSXTW8, ZPR64ExtUXTW8>;

  // Gathers using scaled 32-bit offsets unpacked in 64-bits elements, e.g.
  //    ld1h z0.d, p0/z, [x0, z0.d, uxtw #1]
  defm GLD1SH_D   : sve_mem_64b_gld_sv_32_scaled<0b0100, "ld1sh",  ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm GLDFF1SH_D : sve_mem_64b_gld_sv_32_scaled<0b0101, "ldff1sh",ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm GLD1H_D    : sve_mem_64b_gld_sv_32_scaled<0b0110, "ld1h",   ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm GLDFF1H_D  : sve_mem_64b_gld_sv_32_scaled<0b0111, "ldff1h", ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm GLD1SW_D   : sve_mem_64b_gld_sv_32_scaled<0b1000, "ld1sw",  ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm GLDFF1SW_D : sve_mem_64b_gld_sv_32_scaled<0b1001, "ldff1sw",ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm GLD1W_D    : sve_mem_64b_gld_sv_32_scaled<0b1010, "ld1w",   ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm GLDFF1W_D  : sve_mem_64b_gld_sv_32_scaled<0b1011, "ldff1w", ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm GLD1D      : sve_mem_64b_gld_sv_32_scaled<0b1110, "ld1d",   ZPR64ExtSXTW64, ZPR64ExtUXTW64>;
  defm GLDFF1D    : sve_mem_64b_gld_sv_32_scaled<0b1111, "ldff1d", ZPR64ExtSXTW64, ZPR64ExtUXTW64>;

  // Non-temporal contiguous loads (register + immediate)
  defm LDNT1B_ZRI : sve_mem_cldnt_si<0b00, "ldnt1b", Z_b, ZPR8>;
  defm LDNT1H_ZRI : sve_mem_cldnt_si<0b01, "ldnt1h", Z_h, ZPR16>;
  defm LDNT1W_ZRI : sve_mem_cldnt_si<0b10, "ldnt1w", Z_s, ZPR32>;
  defm LDNT1D_ZRI : sve_mem_cldnt_si<0b11, "ldnt1d", Z_d, ZPR64>;

  // Non-temporal contiguous loads (register + register)
  defm LDNT1B_ZRR : sve_mem_cldnt_ss<0b00, "ldnt1b", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm LDNT1H_ZRR : sve_mem_cldnt_ss<0b01, "ldnt1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm LDNT1W_ZRR : sve_mem_cldnt_ss<0b10, "ldnt1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm LDNT1D_ZRR : sve_mem_cldnt_ss<0b11, "ldnt1d", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // contiguous store with immediates
  defm ST1B_IMM   : sve_mem_cst_si<0b00, 0b00, "st1b", Z_b, ZPR8>;
  defm ST1B_H_IMM : sve_mem_cst_si<0b00, 0b01, "st1b", Z_h, ZPR16>;
  defm ST1B_S_IMM : sve_mem_cst_si<0b00, 0b10, "st1b", Z_s, ZPR32>;
  defm ST1B_D_IMM : sve_mem_cst_si<0b00, 0b11, "st1b", Z_d, ZPR64>;
  defm ST1H_IMM   : sve_mem_cst_si<0b01, 0b01, "st1h", Z_h, ZPR16>;
  defm ST1H_S_IMM : sve_mem_cst_si<0b01, 0b10, "st1h", Z_s, ZPR32>;
  defm ST1H_D_IMM : sve_mem_cst_si<0b01, 0b11, "st1h", Z_d, ZPR64>;
  defm ST1W_IMM   : sve_mem_cst_si<0b10, 0b10, "st1w", Z_s, ZPR32>;
  defm ST1W_D_IMM : sve_mem_cst_si<0b10, 0b11, "st1w", Z_d, ZPR64>;
  defm ST1D_IMM   : sve_mem_cst_si<0b11, 0b11, "st1d", Z_d, ZPR64>;

  // contiguous store with reg+reg addressing.
  defm ST1B   : sve_mem_cst_ss<0b0000, "st1b", Z_b, ZPR8,  GPR64NoXZRshifted8>;
  defm ST1B_H : sve_mem_cst_ss<0b0001, "st1b", Z_h, ZPR16, GPR64NoXZRshifted8>;
  defm ST1B_S : sve_mem_cst_ss<0b0010, "st1b", Z_s, ZPR32, GPR64NoXZRshifted8>;
  defm ST1B_D : sve_mem_cst_ss<0b0011, "st1b", Z_d, ZPR64, GPR64NoXZRshifted8>;
  defm ST1H   : sve_mem_cst_ss<0b0101, "st1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm ST1H_S : sve_mem_cst_ss<0b0110, "st1h", Z_s, ZPR32, GPR64NoXZRshifted16>;
  defm ST1H_D : sve_mem_cst_ss<0b0111, "st1h", Z_d, ZPR64, GPR64NoXZRshifted16>;
  defm ST1W   : sve_mem_cst_ss<0b1010, "st1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm ST1W_D : sve_mem_cst_ss<0b1011, "st1w", Z_d, ZPR64, GPR64NoXZRshifted32>;
  defm ST1D   : sve_mem_cst_ss<0b1111, "st1d", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // Scatters using unscaled 32-bit offsets, e.g.
  //    st1h z0.s, p0, [x0, z0.s, uxtw]
  // and unpacked:
  //    st1h z0.d, p0, [x0, z0.d, uxtw]
  defm SST1B_D : sve_mem_sst_sv_32_unscaled<0b000, "st1b", Z_d, ZPR64, ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm SST1B_S : sve_mem_sst_sv_32_unscaled<0b001, "st1b", Z_s, ZPR32, ZPR32ExtSXTW8Only, ZPR32ExtUXTW8Only>;
  defm SST1H_D : sve_mem_sst_sv_32_unscaled<0b010, "st1h", Z_d, ZPR64, ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm SST1H_S : sve_mem_sst_sv_32_unscaled<0b011, "st1h", Z_s, ZPR32, ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm SST1W_D : sve_mem_sst_sv_32_unscaled<0b100, "st1w", Z_d, ZPR64, ZPR64ExtSXTW8, ZPR64ExtUXTW8>;
  defm SST1W   : sve_mem_sst_sv_32_unscaled<0b101, "st1w", Z_s, ZPR32, ZPR32ExtSXTW8, ZPR32ExtUXTW8>;
  defm SST1D   : sve_mem_sst_sv_32_unscaled<0b110, "st1d", Z_d, ZPR64, ZPR64ExtSXTW8, ZPR64ExtUXTW8>;

  // Scatters using scaled 32-bit offsets, e.g.
  //    st1h z0.s, p0, [x0, z0.s, uxtw #1]
  // and unpacked:
  //    st1h z0.d, p0, [x0, z0.d, uxtw #1]
  defm SST1H_D : sve_mem_sst_sv_32_scaled<0b010, "st1h", Z_d, ZPR64, ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm SST1H_S : sve_mem_sst_sv_32_scaled<0b011, "st1h", Z_s, ZPR32, ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm SST1W_D : sve_mem_sst_sv_32_scaled<0b100, "st1w", Z_d, ZPR64, ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm SST1W   : sve_mem_sst_sv_32_scaled<0b101, "st1w", Z_s, ZPR32, ZPR32ExtSXTW32, ZPR32ExtUXTW32>;
  defm SST1D   : sve_mem_sst_sv_32_scaled<0b110, "st1d", Z_d, ZPR64, ZPR64ExtSXTW64, ZPR64ExtUXTW64>;

  // Scatters using 32/64-bit pointers with offset, e.g.
  //    st1h z0.s, p0, [z0.s, #16]
  //    st1h z0.d, p0, [z0.d, #16]
  defm SST1B_D : sve_mem_sst_vi_ptrs<0b000, "st1b", Z_d, ZPR64, imm0_31>;
  defm SST1B_S : sve_mem_sst_vi_ptrs<0b001, "st1b", Z_s, ZPR32, imm0_31>;
  defm SST1H_D : sve_mem_sst_vi_ptrs<0b010, "st1h", Z_d, ZPR64, uimm5s2>;
  defm SST1H_S : sve_mem_sst_vi_ptrs<0b011, "st1h", Z_s, ZPR32, uimm5s2>;
  defm SST1W_D : sve_mem_sst_vi_ptrs<0b100, "st1w", Z_d, ZPR64, uimm5s4>;
  defm SST1W   : sve_mem_sst_vi_ptrs<0b101, "st1w", Z_s, ZPR32, uimm5s4>;
  defm SST1D   : sve_mem_sst_vi_ptrs<0b110, "st1d", Z_d, ZPR64, uimm5s8>;

  // Scatters using unscaled 64-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d]
  defm SST1B_D : sve_mem_sst_sv_64_unscaled<0b00, "st1b">;
  defm SST1H_D : sve_mem_sst_sv_64_unscaled<0b01, "st1h">;
  defm SST1W_D : sve_mem_sst_sv_64_unscaled<0b10, "st1w">;
  defm SST1D   : sve_mem_sst_sv_64_unscaled<0b11, "st1d">;

  // Scatters using scaled 64-bit offsets, e.g.
  //    st1h z0.d, p0, [x0, z0.d, lsl #1]
  defm SST1H_D_SCALED : sve_mem_sst_sv_64_scaled<0b01, "st1h", ZPR64ExtLSL16>;
  defm SST1W_D_SCALED : sve_mem_sst_sv_64_scaled<0b10, "st1w", ZPR64ExtLSL32>;
  defm SST1D_SCALED   : sve_mem_sst_sv_64_scaled<0b11, "st1d", ZPR64ExtLSL64>;

  // ST(2|3|4) structured stores (register + immediate)
  defm ST2B_IMM : sve_mem_est_si<0b00, 0b01, ZZ_b,   "st2b", simm4s2>;
  defm ST3B_IMM : sve_mem_est_si<0b00, 0b10, ZZZ_b,  "st3b", simm4s3>;
  defm ST4B_IMM : sve_mem_est_si<0b00, 0b11, ZZZZ_b, "st4b", simm4s4>;
  defm ST2H_IMM : sve_mem_est_si<0b01, 0b01, ZZ_h,   "st2h", simm4s2>;
  defm ST3H_IMM : sve_mem_est_si<0b01, 0b10, ZZZ_h,  "st3h", simm4s3>;
  defm ST4H_IMM : sve_mem_est_si<0b01, 0b11, ZZZZ_h, "st4h", simm4s4>;
  defm ST2W_IMM : sve_mem_est_si<0b10, 0b01, ZZ_s,   "st2w", simm4s2>;
  defm ST3W_IMM : sve_mem_est_si<0b10, 0b10, ZZZ_s,  "st3w", simm4s3>;
  defm ST4W_IMM : sve_mem_est_si<0b10, 0b11, ZZZZ_s, "st4w", simm4s4>;
  defm ST2D_IMM : sve_mem_est_si<0b11, 0b01, ZZ_d,   "st2d", simm4s2>;
  defm ST3D_IMM : sve_mem_est_si<0b11, 0b10, ZZZ_d,  "st3d", simm4s3>;
  defm ST4D_IMM : sve_mem_est_si<0b11, 0b11, ZZZZ_d, "st4d", simm4s4>;

  // ST(2|3|4) structured stores (register + register)
  def ST2B : sve_mem_est_ss<0b00, 0b01, ZZ_b,   "st2b", GPR64NoXZRshifted8>;
  def ST3B : sve_mem_est_ss<0b00, 0b10, ZZZ_b,  "st3b", GPR64NoXZRshifted8>;
  def ST4B : sve_mem_est_ss<0b00, 0b11, ZZZZ_b, "st4b", GPR64NoXZRshifted8>;
  def ST2H : sve_mem_est_ss<0b01, 0b01, ZZ_h,   "st2h", GPR64NoXZRshifted16>;
  def ST3H : sve_mem_est_ss<0b01, 0b10, ZZZ_h,  "st3h", GPR64NoXZRshifted16>;
  def ST4H : sve_mem_est_ss<0b01, 0b11, ZZZZ_h, "st4h", GPR64NoXZRshifted16>;
  def ST2W : sve_mem_est_ss<0b10, 0b01, ZZ_s,   "st2w", GPR64NoXZRshifted32>;
  def ST3W : sve_mem_est_ss<0b10, 0b10, ZZZ_s,  "st3w", GPR64NoXZRshifted32>;
  def ST4W : sve_mem_est_ss<0b10, 0b11, ZZZZ_s, "st4w", GPR64NoXZRshifted32>;
  def ST2D : sve_mem_est_ss<0b11, 0b01, ZZ_d,   "st2d", GPR64NoXZRshifted64>;
  def ST3D : sve_mem_est_ss<0b11, 0b10, ZZZ_d,  "st3d", GPR64NoXZRshifted64>;
  def ST4D : sve_mem_est_ss<0b11, 0b11, ZZZZ_d, "st4d", GPR64NoXZRshifted64>;

  // Non-temporal contiguous stores (register + immediate)
  defm STNT1B_ZRI : sve_mem_cstnt_si<0b00, "stnt1b", Z_b, ZPR8>;
  defm STNT1H_ZRI : sve_mem_cstnt_si<0b01, "stnt1h", Z_h, ZPR16>;
  defm STNT1W_ZRI : sve_mem_cstnt_si<0b10, "stnt1w", Z_s, ZPR32>;
  defm STNT1D_ZRI : sve_mem_cstnt_si<0b11, "stnt1d", Z_d, ZPR64>;

  // Non-temporal contiguous stores (register + register)
  defm STNT1B_ZRR : sve_mem_cstnt_ss<0b00, "stnt1b", Z_b, ZPR8, GPR64NoXZRshifted8>;
  defm STNT1H_ZRR : sve_mem_cstnt_ss<0b01, "stnt1h", Z_h, ZPR16, GPR64NoXZRshifted16>;
  defm STNT1W_ZRR : sve_mem_cstnt_ss<0b10, "stnt1w", Z_s, ZPR32, GPR64NoXZRshifted32>;
  defm STNT1D_ZRR : sve_mem_cstnt_ss<0b11, "stnt1d", Z_d, ZPR64, GPR64NoXZRshifted64>;

  // Fill/Spill
  defm LDR_ZXI : sve_mem_z_fill<"ldr">;
  defm LDR_PXI : sve_mem_p_fill<"ldr">;
  defm STR_ZXI : sve_mem_z_spill<"str">;
  defm STR_PXI : sve_mem_p_spill<"str">;

  // Contiguous prefetch (register + immediate)
  defm PRFB_PRI : sve_mem_prfm_si<0b00, "prfb">;
  defm PRFH_PRI : sve_mem_prfm_si<0b01, "prfh">;
  defm PRFW_PRI : sve_mem_prfm_si<0b10, "prfw">;
  defm PRFD_PRI : sve_mem_prfm_si<0b11, "prfd">;

  // Contiguous prefetch (register + register)
  def PRFB_PRR : sve_mem_prfm_ss<0b001, "prfb", GPR64NoXZRshifted8>;
  def PRFH_PRR : sve_mem_prfm_ss<0b011, "prfh", GPR64NoXZRshifted16>;
  def PRFS_PRR : sve_mem_prfm_ss<0b101, "prfw", GPR64NoXZRshifted32>;
  def PRFD_PRR : sve_mem_prfm_ss<0b111, "prfd", GPR64NoXZRshifted64>;

  // Gather prefetch using scaled 32-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.s, uxtw #1]
  defm PRFB_S : sve_mem_32b_prfm_sv_scaled<0b00, "prfb", ZPR32ExtSXTW8Only,  ZPR32ExtUXTW8Only>;
  defm PRFH_S : sve_mem_32b_prfm_sv_scaled<0b01, "prfh", ZPR32ExtSXTW16, ZPR32ExtUXTW16>;
  defm PRFW_S : sve_mem_32b_prfm_sv_scaled<0b10, "prfw", ZPR32ExtSXTW32, ZPR32ExtUXTW32>;
  defm PRFD_S : sve_mem_32b_prfm_sv_scaled<0b11, "prfd", ZPR32ExtSXTW64, ZPR32ExtUXTW64>;

  // Gather prefetch using unpacked, scaled 32-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.d, uxtw #1]
  defm PRFB_D : sve_mem_64b_prfm_sv_ext_scaled<0b00, "prfb", ZPR64ExtSXTW8Only, ZPR64ExtUXTW8Only>;
  defm PRFH_D : sve_mem_64b_prfm_sv_ext_scaled<0b01, "prfh", ZPR64ExtSXTW16, ZPR64ExtUXTW16>;
  defm PRFW_D : sve_mem_64b_prfm_sv_ext_scaled<0b10, "prfw", ZPR64ExtSXTW32, ZPR64ExtUXTW32>;
  defm PRFD_D : sve_mem_64b_prfm_sv_ext_scaled<0b11, "prfd", ZPR64ExtSXTW64, ZPR64ExtUXTW64>;

  // Gather prefetch using scaled 64-bit offsets, e.g.
  //    prfh pldl1keep, p0, [x0, z0.d, lsl #1]
  defm PRFB_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b00, "prfb", ZPR64ExtLSL8>;
  defm PRFH_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b01, "prfh", ZPR64ExtLSL16>;
  defm PRFW_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b10, "prfw", ZPR64ExtLSL32>;
  defm PRFD_D_SCALED : sve_mem_64b_prfm_sv_lsl_scaled<0b11, "prfd", ZPR64ExtLSL64>;

  // Gather prefetch using 32/64-bit pointers with offset, e.g.
  //    prfh pldl1keep, p0, [z0.s, #16]
  //    prfh pldl1keep, p0, [z0.d, #16]
  defm PRFB_S_PZI : sve_mem_32b_prfm_vi<0b00, "prfb", imm0_31>;
  defm PRFH_S_PZI : sve_mem_32b_prfm_vi<0b01, "prfh", uimm5s2>;
  defm PRFW_S_PZI : sve_mem_32b_prfm_vi<0b10, "prfw", uimm5s4>;
  defm PRFD_S_PZI : sve_mem_32b_prfm_vi<0b11, "prfd", uimm5s8>;

  defm PRFB_D_PZI : sve_mem_64b_prfm_vi<0b00, "prfb", imm0_31>;
  defm PRFH_D_PZI : sve_mem_64b_prfm_vi<0b01, "prfh", uimm5s2>;
  defm PRFW_D_PZI : sve_mem_64b_prfm_vi<0b10, "prfw", uimm5s4>;
  defm PRFD_D_PZI : sve_mem_64b_prfm_vi<0b11, "prfd", uimm5s8>;

  defm ADR_SXTW_ZZZ_D : sve_int_bin_cons_misc_0_a_sxtw<0b00, "adr">;
  defm ADR_UXTW_ZZZ_D : sve_int_bin_cons_misc_0_a_uxtw<0b01, "adr">;
  defm ADR_LSL_ZZZ_S  : sve_int_bin_cons_misc_0_a_32_lsl<0b10, "adr">;
  defm ADR_LSL_ZZZ_D  : sve_int_bin_cons_misc_0_a_64_lsl<0b11, "adr">;

  defm TBL_ZZZ  : sve_int_perm_tbl<"tbl">;

  defm ZIP1_ZZZ : sve_int_perm_bin_perm_zz<0b000, "zip1">;
  defm ZIP2_ZZZ : sve_int_perm_bin_perm_zz<0b001, "zip2">;
  defm UZP1_ZZZ : sve_int_perm_bin_perm_zz<0b010, "uzp1">;
  defm UZP2_ZZZ : sve_int_perm_bin_perm_zz<0b011, "uzp2">;
  defm TRN1_ZZZ : sve_int_perm_bin_perm_zz<0b100, "trn1">;
  defm TRN2_ZZZ : sve_int_perm_bin_perm_zz<0b101, "trn2">;

  defm ZIP1_PPP : sve_int_perm_bin_perm_pp<0b000, "zip1">;
  defm ZIP2_PPP : sve_int_perm_bin_perm_pp<0b001, "zip2">;
  defm UZP1_PPP : sve_int_perm_bin_perm_pp<0b010, "uzp1">;
  defm UZP2_PPP : sve_int_perm_bin_perm_pp<0b011, "uzp2">;
  defm TRN1_PPP : sve_int_perm_bin_perm_pp<0b100, "trn1">;
  defm TRN2_PPP : sve_int_perm_bin_perm_pp<0b101, "trn2">;

  defm CMPHS_PPzZZ : sve_int_cmp_0<0b000, "cmphs">;
  defm CMPHI_PPzZZ : sve_int_cmp_0<0b001, "cmphi">;
  defm CMPGE_PPzZZ : sve_int_cmp_0<0b100, "cmpge">;
  defm CMPGT_PPzZZ : sve_int_cmp_0<0b101, "cmpgt">;
  defm CMPEQ_PPzZZ : sve_int_cmp_0<0b110, "cmpeq">;
  defm CMPNE_PPzZZ : sve_int_cmp_0<0b111, "cmpne">;

  defm CMPEQ_WIDE_PPzZZ : sve_int_cmp_0_wide<0b010, "cmpeq">;
  defm CMPNE_WIDE_PPzZZ : sve_int_cmp_0_wide<0b011, "cmpne">;
  defm CMPGE_WIDE_PPzZZ : sve_int_cmp_1_wide<0b000, "cmpge">;
  defm CMPGT_WIDE_PPzZZ : sve_int_cmp_1_wide<0b001, "cmpgt">;
  defm CMPLT_WIDE_PPzZZ : sve_int_cmp_1_wide<0b010, "cmplt">;
  defm CMPLE_WIDE_PPzZZ : sve_int_cmp_1_wide<0b011, "cmple">;
  defm CMPHS_WIDE_PPzZZ : sve_int_cmp_1_wide<0b100, "cmphs">;
  defm CMPHI_WIDE_PPzZZ : sve_int_cmp_1_wide<0b101, "cmphi">;
  defm CMPLO_WIDE_PPzZZ : sve_int_cmp_1_wide<0b110, "cmplo">;
  defm CMPLS_WIDE_PPzZZ : sve_int_cmp_1_wide<0b111, "cmpls">;

  defm CMPGE_PPzZI : sve_int_scmp_vi<0b000, "cmpge">;
  defm CMPGT_PPzZI : sve_int_scmp_vi<0b001, "cmpgt">;
  defm CMPLT_PPzZI : sve_int_scmp_vi<0b010, "cmplt">;
  defm CMPLE_PPzZI : sve_int_scmp_vi<0b011, "cmple">;
  defm CMPEQ_PPzZI : sve_int_scmp_vi<0b100, "cmpeq">;
  defm CMPNE_PPzZI : sve_int_scmp_vi<0b101, "cmpne">;
  defm CMPHS_PPzZI : sve_int_ucmp_vi<0b00, "cmphs">;
  defm CMPHI_PPzZI : sve_int_ucmp_vi<0b01, "cmphi">;
  defm CMPLO_PPzZI : sve_int_ucmp_vi<0b10, "cmplo">;
  defm CMPLS_PPzZI : sve_int_ucmp_vi<0b11, "cmpls">;

  defm FCMGE_PPzZZ : sve_fp_3op_p_pd<0b000, "fcmge">;
  defm FCMGT_PPzZZ : sve_fp_3op_p_pd<0b001, "fcmgt">;
  defm FCMEQ_PPzZZ : sve_fp_3op_p_pd<0b010, "fcmeq">;
  defm FCMNE_PPzZZ : sve_fp_3op_p_pd<0b011, "fcmne">;
  defm FCMUO_PPzZZ : sve_fp_3op_p_pd<0b100, "fcmuo">;
  defm FACGE_PPzZZ : sve_fp_3op_p_pd<0b101, "facge">;
  defm FACGT_PPzZZ : sve_fp_3op_p_pd<0b111, "facgt">;

  defm FCMGE_PPzZ0 : sve_fp_2op_p_pd<0b000, "fcmge">;
  defm FCMGT_PPzZ0 : sve_fp_2op_p_pd<0b001, "fcmgt">;
  defm FCMLT_PPzZ0 : sve_fp_2op_p_pd<0b010, "fcmlt">;
  defm FCMLE_PPzZ0 : sve_fp_2op_p_pd<0b011, "fcmle">;
  defm FCMEQ_PPzZ0 : sve_fp_2op_p_pd<0b100, "fcmeq">;
  defm FCMNE_PPzZ0 : sve_fp_2op_p_pd<0b110, "fcmne">;

  defm WHILELT_PWW : sve_int_while4_rr<0b010, "whilelt">;
  defm WHILELE_PWW : sve_int_while4_rr<0b011, "whilele">;
  defm WHILELO_PWW : sve_int_while4_rr<0b110, "whilelo">;
  defm WHILELS_PWW : sve_int_while4_rr<0b111, "whilels">;

  defm WHILELT_PXX : sve_int_while8_rr<0b010, "whilelt">;
  defm WHILELE_PXX : sve_int_while8_rr<0b011, "whilele">;
  defm WHILELO_PXX : sve_int_while8_rr<0b110, "whilelo">;
  defm WHILELS_PXX : sve_int_while8_rr<0b111, "whilels">;

  def CTERMEQ_WW : sve_int_cterm<0b0, 0b0, "ctermeq", GPR32>;
  def CTERMNE_WW : sve_int_cterm<0b0, 0b1, "ctermne", GPR32>;
  def CTERMEQ_XX : sve_int_cterm<0b1, 0b0, "ctermeq", GPR64>;
  def CTERMNE_XX : sve_int_cterm<0b1, 0b1, "ctermne", GPR64>;

  def RDVLI_XI  : sve_int_read_vl_a<0b0, 0b11111, "rdvl">;
  def ADDVL_XXI : sve_int_arith_vl<0b0, "addvl">;
  def ADDPL_XXI : sve_int_arith_vl<0b1, "addpl">;

  defm CNTB_XPiI : sve_int_count<0b000, "cntb">;
  defm CNTH_XPiI : sve_int_count<0b010, "cnth">;
  defm CNTW_XPiI : sve_int_count<0b100, "cntw">;
  defm CNTD_XPiI : sve_int_count<0b110, "cntd">;
  defm CNTP_XPP : sve_int_pcount_pred<0b0000, "cntp">;

  defm INCB_XPiI : sve_int_pred_pattern_a<0b000, "incb">;
  defm DECB_XPiI : sve_int_pred_pattern_a<0b001, "decb">;
  defm INCH_XPiI : sve_int_pred_pattern_a<0b010, "inch">;
  defm DECH_XPiI : sve_int_pred_pattern_a<0b011, "dech">;
  defm INCW_XPiI : sve_int_pred_pattern_a<0b100, "incw">;
  defm DECW_XPiI : sve_int_pred_pattern_a<0b101, "decw">;
  defm INCD_XPiI : sve_int_pred_pattern_a<0b110, "incd">;
  defm DECD_XPiI : sve_int_pred_pattern_a<0b111, "decd">;

  defm SQINCB_XPiWdI : sve_int_pred_pattern_b_s32<0b00000, "sqincb">;
  defm UQINCB_WPiI   : sve_int_pred_pattern_b_u32<0b00001, "uqincb">;
  defm SQDECB_XPiWdI : sve_int_pred_pattern_b_s32<0b00010, "sqdecb">;
  defm UQDECB_WPiI   : sve_int_pred_pattern_b_u32<0b00011, "uqdecb">;
  defm SQINCB_XPiI   : sve_int_pred_pattern_b_x64<0b00100, "sqincb">;
  defm UQINCB_XPiI   : sve_int_pred_pattern_b_x64<0b00101, "uqincb">;
  defm SQDECB_XPiI   : sve_int_pred_pattern_b_x64<0b00110, "sqdecb">;
  defm UQDECB_XPiI   : sve_int_pred_pattern_b_x64<0b00111, "uqdecb">;

  defm SQINCH_XPiWdI : sve_int_pred_pattern_b_s32<0b01000, "sqinch">;
  defm UQINCH_WPiI   : sve_int_pred_pattern_b_u32<0b01001, "uqinch">;
  defm SQDECH_XPiWdI : sve_int_pred_pattern_b_s32<0b01010, "sqdech">;
  defm UQDECH_WPiI   : sve_int_pred_pattern_b_u32<0b01011, "uqdech">;
  defm SQINCH_XPiI   : sve_int_pred_pattern_b_x64<0b01100, "sqinch">;
  defm UQINCH_XPiI   : sve_int_pred_pattern_b_x64<0b01101, "uqinch">;
  defm SQDECH_XPiI   : sve_int_pred_pattern_b_x64<0b01110, "sqdech">;
  defm UQDECH_XPiI   : sve_int_pred_pattern_b_x64<0b01111, "uqdech">;

  defm SQINCW_XPiWdI : sve_int_pred_pattern_b_s32<0b10000, "sqincw">;
  defm UQINCW_WPiI   : sve_int_pred_pattern_b_u32<0b10001, "uqincw">;
  defm SQDECW_XPiWdI : sve_int_pred_pattern_b_s32<0b10010, "sqdecw">;
  defm UQDECW_WPiI   : sve_int_pred_pattern_b_u32<0b10011, "uqdecw">;
  defm SQINCW_XPiI   : sve_int_pred_pattern_b_x64<0b10100, "sqincw">;
  defm UQINCW_XPiI   : sve_int_pred_pattern_b_x64<0b10101, "uqincw">;
  defm SQDECW_XPiI   : sve_int_pred_pattern_b_x64<0b10110, "sqdecw">;
  defm UQDECW_XPiI   : sve_int_pred_pattern_b_x64<0b10111, "uqdecw">;

  defm SQINCD_XPiWdI : sve_int_pred_pattern_b_s32<0b11000, "sqincd">;
  defm UQINCD_WPiI   : sve_int_pred_pattern_b_u32<0b11001, "uqincd">;
  defm SQDECD_XPiWdI : sve_int_pred_pattern_b_s32<0b11010, "sqdecd">;
  defm UQDECD_WPiI   : sve_int_pred_pattern_b_u32<0b11011, "uqdecd">;
  defm SQINCD_XPiI   : sve_int_pred_pattern_b_x64<0b11100, "sqincd">;
  defm UQINCD_XPiI   : sve_int_pred_pattern_b_x64<0b11101, "uqincd">;
  defm SQDECD_XPiI   : sve_int_pred_pattern_b_x64<0b11110, "sqdecd">;
  defm UQDECD_XPiI   : sve_int_pred_pattern_b_x64<0b11111, "uqdecd">;

  defm SQINCH_ZPiI : sve_int_countvlv<0b01000, "sqinch", ZPR16>;
  defm UQINCH_ZPiI : sve_int_countvlv<0b01001, "uqinch", ZPR16>;
  defm SQDECH_ZPiI : sve_int_countvlv<0b01010, "sqdech", ZPR16>;
  defm UQDECH_ZPiI : sve_int_countvlv<0b01011, "uqdech", ZPR16>;
  defm INCH_ZPiI   : sve_int_countvlv<0b01100, "inch",   ZPR16>;
  defm DECH_ZPiI   : sve_int_countvlv<0b01101, "dech",   ZPR16>;
  defm SQINCW_ZPiI : sve_int_countvlv<0b10000, "sqincw", ZPR32>;
  defm UQINCW_ZPiI : sve_int_countvlv<0b10001, "uqincw", ZPR32>;
  defm SQDECW_ZPiI : sve_int_countvlv<0b10010, "sqdecw", ZPR32>;
  defm UQDECW_ZPiI : sve_int_countvlv<0b10011, "uqdecw", ZPR32>;
  defm INCW_ZPiI   : sve_int_countvlv<0b10100, "incw",   ZPR32>;
  defm DECW_ZPiI   : sve_int_countvlv<0b10101, "decw",   ZPR32>;
  defm SQINCD_ZPiI : sve_int_countvlv<0b11000, "sqincd", ZPR64>;
  defm UQINCD_ZPiI : sve_int_countvlv<0b11001, "uqincd", ZPR64>;
  defm SQDECD_ZPiI : sve_int_countvlv<0b11010, "sqdecd", ZPR64>;
  defm UQDECD_ZPiI : sve_int_countvlv<0b11011, "uqdecd", ZPR64>;
  defm INCD_ZPiI   : sve_int_countvlv<0b11100, "incd",   ZPR64>;
  defm DECD_ZPiI   : sve_int_countvlv<0b11101, "decd",   ZPR64>;

  defm SQINCP_XPWd : sve_int_count_r_s32<0b00000, "sqincp">;
  defm SQINCP_XP   : sve_int_count_r_x64<0b00010, "sqincp">;
  defm UQINCP_WP   : sve_int_count_r_u32<0b00100, "uqincp">;
  defm UQINCP_XP   : sve_int_count_r_x64<0b00110, "uqincp">;
  defm SQDECP_XPWd : sve_int_count_r_s32<0b01000, "sqdecp">;
  defm SQDECP_XP   : sve_int_count_r_x64<0b01010, "sqdecp">;
  defm UQDECP_WP   : sve_int_count_r_u32<0b01100, "uqdecp">;
  defm UQDECP_XP   : sve_int_count_r_x64<0b01110, "uqdecp">;
  defm INCP_XP     : sve_int_count_r_x64<0b10000, "incp">;
  defm DECP_XP     : sve_int_count_r_x64<0b10100, "decp">;

  defm SQINCP_ZP   : sve_int_count_v<0b00000, "sqincp">;
  defm UQINCP_ZP   : sve_int_count_v<0b00100, "uqincp">;
  defm SQDECP_ZP   : sve_int_count_v<0b01000, "sqdecp">;
  defm UQDECP_ZP   : sve_int_count_v<0b01100, "uqdecp">;
  defm INCP_ZP     : sve_int_count_v<0b10000, "incp">;
  defm DECP_ZP     : sve_int_count_v<0b10100, "decp">;

  defm INDEX_RR : sve_int_index_rr<"index">;
  defm INDEX_IR : sve_int_index_ir<"index">;
  defm INDEX_RI : sve_int_index_ri<"index">;
  defm INDEX_II : sve_int_index_ii<"index">;

  // Unpredicated shifts
  defm ASR_ZZI : sve_int_bin_cons_shift_imm_right<0b00, "asr">;
  defm LSR_ZZI : sve_int_bin_cons_shift_imm_right<0b01, "lsr">;
  defm LSL_ZZI : sve_int_bin_cons_shift_imm_left< 0b11, "lsl">;

  defm ASR_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b00, "asr">;
  defm LSR_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b01, "lsr">;
  defm LSL_WIDE_ZZZ : sve_int_bin_cons_shift_wide<0b11, "lsl">;

  // Predicated shifts
  defm ASR_ZPmI  : sve_int_bin_pred_shift_imm_right<0b000, "asr">;
  defm LSR_ZPmI  : sve_int_bin_pred_shift_imm_right<0b001, "lsr">;
  defm LSL_ZPmI  : sve_int_bin_pred_shift_imm_left< 0b011, "lsl">;
  defm ASRD_ZPmI : sve_int_bin_pred_shift_imm_right<0b100, "asrd">;

  defm ASR_ZPmZ  : sve_int_bin_pred_shift<0b000, "asr">;
  defm LSR_ZPmZ  : sve_int_bin_pred_shift<0b001, "lsr">;
  defm LSL_ZPmZ  : sve_int_bin_pred_shift<0b011, "lsl">;
  defm ASRR_ZPmZ : sve_int_bin_pred_shift<0b100, "asrr">;
  defm LSRR_ZPmZ : sve_int_bin_pred_shift<0b101, "lsrr">;
  defm LSLR_ZPmZ : sve_int_bin_pred_shift<0b111, "lslr">;

  defm ASR_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b000, "asr">;
  defm LSR_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b001, "lsr">;
  defm LSL_WIDE_ZPmZ : sve_int_bin_pred_shift_wide<0b011, "lsl">;

  def FCVT_ZPmZ_StoH   : sve_fp_2op_p_zd<0b1001000, "fcvt",   ZPR32, ZPR16, ElementSizeS>;
  def FCVT_ZPmZ_HtoS   : sve_fp_2op_p_zd<0b1001001, "fcvt",   ZPR16, ZPR32, ElementSizeS>;
  def SCVTF_ZPmZ_HtoH  : sve_fp_2op_p_zd<0b0110010, "scvtf",  ZPR16, ZPR16, ElementSizeH>;
  def SCVTF_ZPmZ_StoS  : sve_fp_2op_p_zd<0b1010100, "scvtf",  ZPR32, ZPR32, ElementSizeS>;
  def UCVTF_ZPmZ_StoS  : sve_fp_2op_p_zd<0b1010101, "ucvtf",  ZPR32, ZPR32, ElementSizeS>;
  def UCVTF_ZPmZ_HtoH  : sve_fp_2op_p_zd<0b0110011, "ucvtf",  ZPR16, ZPR16, ElementSizeH>;
  def FCVTZS_ZPmZ_HtoH : sve_fp_2op_p_zd<0b0111010, "fcvtzs", ZPR16, ZPR16, ElementSizeH>;
  def FCVTZS_ZPmZ_StoS : sve_fp_2op_p_zd<0b1011100, "fcvtzs", ZPR32, ZPR32, ElementSizeS>;
  def FCVTZU_ZPmZ_HtoH : sve_fp_2op_p_zd<0b0111011, "fcvtzu", ZPR16, ZPR16, ElementSizeH>;
  def FCVTZU_ZPmZ_StoS : sve_fp_2op_p_zd<0b1011101, "fcvtzu", ZPR32, ZPR32, ElementSizeS>;
  def FCVT_ZPmZ_DtoH   : sve_fp_2op_p_zd<0b1101000, "fcvt",   ZPR64, ZPR16, ElementSizeD>;
  def FCVT_ZPmZ_HtoD   : sve_fp_2op_p_zd<0b1101001, "fcvt",   ZPR16, ZPR64, ElementSizeD>;
  def FCVT_ZPmZ_DtoS   : sve_fp_2op_p_zd<0b1101010, "fcvt",   ZPR64, ZPR32, ElementSizeD>;
  def FCVT_ZPmZ_StoD   : sve_fp_2op_p_zd<0b1101011, "fcvt",   ZPR32, ZPR64, ElementSizeD>;
  def SCVTF_ZPmZ_StoD  : sve_fp_2op_p_zd<0b1110000, "scvtf",  ZPR32, ZPR64, ElementSizeD>;
  def UCVTF_ZPmZ_StoD  : sve_fp_2op_p_zd<0b1110001, "ucvtf",  ZPR32, ZPR64, ElementSizeD>;
  def UCVTF_ZPmZ_StoH  : sve_fp_2op_p_zd<0b0110101, "ucvtf",  ZPR32, ZPR16, ElementSizeS>;
  def SCVTF_ZPmZ_DtoS  : sve_fp_2op_p_zd<0b1110100, "scvtf",  ZPR64, ZPR32, ElementSizeD>;
  def SCVTF_ZPmZ_StoH  : sve_fp_2op_p_zd<0b0110100, "scvtf",  ZPR32, ZPR16, ElementSizeS>;
  def SCVTF_ZPmZ_DtoH  : sve_fp_2op_p_zd<0b0110110, "scvtf",  ZPR64, ZPR16, ElementSizeD>;
  def UCVTF_ZPmZ_DtoS  : sve_fp_2op_p_zd<0b1110101, "ucvtf",  ZPR64, ZPR32, ElementSizeD>;
  def UCVTF_ZPmZ_DtoH  : sve_fp_2op_p_zd<0b0110111, "ucvtf",  ZPR64, ZPR16, ElementSizeD>;
  def SCVTF_ZPmZ_DtoD  : sve_fp_2op_p_zd<0b1110110, "scvtf",  ZPR64, ZPR64, ElementSizeD>;
  def UCVTF_ZPmZ_DtoD  : sve_fp_2op_p_zd<0b1110111, "ucvtf",  ZPR64, ZPR64, ElementSizeD>;
  def FCVTZS_ZPmZ_DtoS : sve_fp_2op_p_zd<0b1111000, "fcvtzs", ZPR64, ZPR32, ElementSizeD>;
  def FCVTZU_ZPmZ_DtoS : sve_fp_2op_p_zd<0b1111001, "fcvtzu", ZPR64, ZPR32, ElementSizeD>;
  def FCVTZS_ZPmZ_StoD : sve_fp_2op_p_zd<0b1111100, "fcvtzs", ZPR32, ZPR64, ElementSizeD>;
  def FCVTZS_ZPmZ_HtoS : sve_fp_2op_p_zd<0b0111100, "fcvtzs", ZPR16, ZPR32, ElementSizeS>;
  def FCVTZS_ZPmZ_HtoD : sve_fp_2op_p_zd<0b0111110, "fcvtzs", ZPR16, ZPR64, ElementSizeD>;
  def FCVTZU_ZPmZ_HtoS : sve_fp_2op_p_zd<0b0111101, "fcvtzu", ZPR16, ZPR32, ElementSizeS>;
  def FCVTZU_ZPmZ_HtoD : sve_fp_2op_p_zd<0b0111111, "fcvtzu", ZPR16, ZPR64, ElementSizeD>;
  def FCVTZU_ZPmZ_StoD : sve_fp_2op_p_zd<0b1111101, "fcvtzu", ZPR32, ZPR64, ElementSizeD>;
  def FCVTZS_ZPmZ_DtoD : sve_fp_2op_p_zd<0b1111110, "fcvtzs", ZPR64, ZPR64, ElementSizeD>;
  def FCVTZU_ZPmZ_DtoD : sve_fp_2op_p_zd<0b1111111, "fcvtzu", ZPR64, ZPR64, ElementSizeD>;

  defm FRINTN_ZPmZ : sve_fp_2op_p_zd_HSD<0b00000, "frintn">;
  defm FRINTP_ZPmZ : sve_fp_2op_p_zd_HSD<0b00001, "frintp">;
  defm FRINTM_ZPmZ : sve_fp_2op_p_zd_HSD<0b00010, "frintm">;
  defm FRINTZ_ZPmZ : sve_fp_2op_p_zd_HSD<0b00011, "frintz">;
  defm FRINTA_ZPmZ : sve_fp_2op_p_zd_HSD<0b00100, "frinta">;
  defm FRINTX_ZPmZ : sve_fp_2op_p_zd_HSD<0b00110, "frintx">;
  defm FRINTI_ZPmZ : sve_fp_2op_p_zd_HSD<0b00111, "frinti">;
  defm FRECPX_ZPmZ : sve_fp_2op_p_zd_HSD<0b01100, "frecpx">;
  defm FSQRT_ZPmZ  : sve_fp_2op_p_zd_HSD<0b01101, "fsqrt">;

  // InstAliases
  def : InstAlias<"mov $Zd, $Zn",
                  (ORR_ZZZ ZPR64:$Zd, ZPR64:$Zn, ZPR64:$Zn), 1>;
  def : InstAlias<"mov $Pd, $Pg/m, $Pn",
                  (SEL_PPPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pd), 1>;
  def : InstAlias<"mov $Pd, $Pn",
                  (ORR_PPzPP PPR8:$Pd, PPR8:$Pn, PPR8:$Pn, PPR8:$Pn), 1>;
  def : InstAlias<"mov $Pd, $Pg/z, $Pn",
                  (AND_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pn), 1>;

  def : InstAlias<"movs $Pd, $Pn",
                  (ORRS_PPzPP PPR8:$Pd, PPR8:$Pn, PPR8:$Pn, PPR8:$Pn), 1>;
  def : InstAlias<"movs $Pd, $Pg/z, $Pn",
                  (ANDS_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPR8:$Pn), 1>;

  def : InstAlias<"not $Pd, $Pg/z, $Pn",
                  (EOR_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPRAny:$Pg), 1>;

  def : InstAlias<"nots $Pd, $Pg/z, $Pn",
                  (EORS_PPzPP PPR8:$Pd, PPRAny:$Pg, PPR8:$Pn, PPRAny:$Pg), 1>;

  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmple $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmplo $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHI_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmpls $Zd, $Pg/z, $Zm, $Zn",
                  (CMPHS_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_B PPR8:$Zd, PPR3bAny:$Pg, ZPR8:$Zn, ZPR8:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"cmplt $Zd, $Pg/z, $Zm, $Zn",
                  (CMPGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"facle $Zd, $Pg/z, $Zm, $Zn",
                  (FACGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"faclt $Zd, $Pg/z, $Zm, $Zn",
                  (FACGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"fcmle $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGE_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;

  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_H PPR16:$Zd, PPR3bAny:$Pg, ZPR16:$Zn, ZPR16:$Zm), 0>;
  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_S PPR32:$Zd, PPR3bAny:$Pg, ZPR32:$Zn, ZPR32:$Zm), 0>;
  def : InstAlias<"fcmlt $Zd, $Pg/z, $Zm, $Zn",
                  (FCMGT_PPzZZ_D PPR64:$Zd, PPR3bAny:$Pg, ZPR64:$Zn, ZPR64:$Zm), 0>;
}
