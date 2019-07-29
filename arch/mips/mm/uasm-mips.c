/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * A small micro-assembler. It is intentionally kept simple, does only
 * support a subset of instructions, and does not try to hide pipeline
 * effects like branch delay slots.
 *
 * Copyright (C) 2004, 2005, 2006, 2008	 Thiemo Seufer
 * Copyright (C) 2005, 2007  Maciej W. Rozycki
 * Copyright (C) 2006  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2012, 2013  MIPS Technologies, Inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/inst.h>
#include <asm/elf.h>
#include <asm/bugs.h>
#include <asm/uasm.h>

#define RS_MASK		0x1f
#define RS_SH		21
#define RT_MASK		0x1f
#define RT_SH		16
#define SCIMM_MASK	0xfffff
#define SCIMM_SH	6

/* This macro sets the non-variable bits of an instruction. */
#define M(a, b, c, d, e, f)					\
	((a) << OP_SH						\
	 | (b) << RS_SH						\
	 | (c) << RT_SH						\
	 | (d) << RD_SH						\
	 | (e) << RE_SH						\
	 | (f) << FUNC_SH)

/* This macro sets the non-variable bits of an R6 instruction. */
#define M6(a, b, c, d, e)					\
	((a) << OP_SH						\
	 | (b) << RS_SH						\
	 | (c) << RT_SH						\
	 | (d) << SIMM9_SH					\
	 | (e) << FUNC_SH)

#include "uasm.c"

static const struct insn insn_table[insn_invalid] = {
	[insn_addiu]	= {M(addiu_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_addu]	= {M(spec_op, 0, 0, 0, 0, addu_op), RS | RT | RD},
	[insn_and]	= {M(spec_op, 0, 0, 0, 0, and_op), RS | RT | RD},
	[insn_andi]	= {M(andi_op, 0, 0, 0, 0, 0), RS | RT | UIMM},
	[insn_bbit0]	= {M(lwc2_op, 0, 0, 0, 0, 0), RS | RT | BIMM},
	[insn_bbit1]	= {M(swc2_op, 0, 0, 0, 0, 0), RS | RT | BIMM},
	[insn_beq]	= {M(beq_op, 0, 0, 0, 0, 0), RS | RT | BIMM},
	[insn_beql]	= {M(beql_op, 0, 0, 0, 0, 0), RS | RT | BIMM},
	[insn_bgez]	= {M(bcond_op, 0, bgez_op, 0, 0, 0), RS | BIMM},
	[insn_bgezl]	= {M(bcond_op, 0, bgezl_op, 0, 0, 0), RS | BIMM},
	[insn_bgtz]	= {M(bgtz_op, 0, 0, 0, 0, 0), RS | BIMM},
	[insn_blez]	= {M(blez_op, 0, 0, 0, 0, 0), RS | BIMM},
	[insn_bltz]	= {M(bcond_op, 0, bltz_op, 0, 0, 0), RS | BIMM},
	[insn_bltzl]	= {M(bcond_op, 0, bltzl_op, 0, 0, 0), RS | BIMM},
	[insn_bne]	= {M(bne_op, 0, 0, 0, 0, 0), RS | RT | BIMM},
	[insn_break]	= {M(spec_op, 0, 0, 0, 0, break_op), SCIMM},
#ifndef CONFIG_CPU_MIPSR6
	[insn_cache]	= {M(cache_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
#else
	[insn_cache]	= {M6(spec3_op, 0, 0, 0, cache6_op),  RS | RT | SIMM9},
#endif
	[insn_cfc1]	= {M(cop1_op, cfc_op, 0, 0, 0, 0), RT | RD},
	[insn_cfcmsa]	= {M(msa_op, 0, msa_cfc_op, 0, 0, msa_elm_op), RD | RE},
	[insn_ctc1]	= {M(cop1_op, ctc_op, 0, 0, 0, 0), RT | RD},
	[insn_ctcmsa]	= {M(msa_op, 0, msa_ctc_op, 0, 0, msa_elm_op), RD | RE},
	[insn_daddiu]	= {M(daddiu_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_daddu]	= {M(spec_op, 0, 0, 0, 0, daddu_op), RS | RT | RD},
	[insn_ddivu]	= {M(spec_op, 0, 0, 0, 0, ddivu_op), RS | RT},
	[insn_di]	= {M(cop0_op, mfmc0_op, 0, 12, 0, 0), RT},
	[insn_dins]	= {M(spec3_op, 0, 0, 0, 0, dins_op), RS | RT | RD | RE},
	[insn_dinsm]	= {M(spec3_op, 0, 0, 0, 0, dinsm_op), RS | RT | RD | RE},
	[insn_dinsu]	= {M(spec3_op, 0, 0, 0, 0, dinsu_op), RS | RT | RD | RE},
	[insn_divu]	= {M(spec_op, 0, 0, 0, 0, divu_op), RS | RT},
	[insn_dmfc0]	= {M(cop0_op, dmfc_op, 0, 0, 0, 0), RT | RD | SET},
	[insn_dmtc0]	= {M(cop0_op, dmtc_op, 0, 0, 0, 0), RT | RD | SET},
	[insn_dmultu]	= {M(spec_op, 0, 0, 0, 0, dmultu_op), RS | RT},
	[insn_drotr]	= {M(spec_op, 1, 0, 0, 0, dsrl_op), RT | RD | RE},
	[insn_drotr32]	= {M(spec_op, 1, 0, 0, 0, dsrl32_op), RT | RD | RE},
	[insn_dsbh]	= {M(spec3_op, 0, 0, 0, dsbh_op, dbshfl_op), RT | RD},
	[insn_dshd]	= {M(spec3_op, 0, 0, 0, dshd_op, dbshfl_op), RT | RD},
	[insn_dsll]	= {M(spec_op, 0, 0, 0, 0, dsll_op), RT | RD | RE},
	[insn_dsll32]	= {M(spec_op, 0, 0, 0, 0, dsll32_op), RT | RD | RE},
	[insn_dsllv]	= {M(spec_op, 0, 0, 0, 0, dsllv_op),  RS | RT | RD},
	[insn_dsra]	= {M(spec_op, 0, 0, 0, 0, dsra_op), RT | RD | RE},
	[insn_dsra32]	= {M(spec_op, 0, 0, 0, 0, dsra32_op), RT | RD | RE},
	[insn_dsrav]	= {M(spec_op, 0, 0, 0, 0, dsrav_op),  RS | RT | RD},
	[insn_dsrl]	= {M(spec_op, 0, 0, 0, 0, dsrl_op), RT | RD | RE},
	[insn_dsrl32]	= {M(spec_op, 0, 0, 0, 0, dsrl32_op), RT | RD | RE},
	[insn_dsrlv]	= {M(spec_op, 0, 0, 0, 0, dsrlv_op),  RS | RT | RD},
	[insn_dsubu]	= {M(spec_op, 0, 0, 0, 0, dsubu_op), RS | RT | RD},
	[insn_eret]	= {M(cop0_op, cop_op, 0, 0, 0, eret_op),  0},
	[insn_ext]	= {M(spec3_op, 0, 0, 0, 0, ext_op), RS | RT | RD | RE},
	[insn_ins]	= {M(spec3_op, 0, 0, 0, 0, ins_op), RS | RT | RD | RE},
	[insn_j]	= {M(j_op, 0, 0, 0, 0, 0),  JIMM},
	[insn_jal]	= {M(jal_op, 0, 0, 0, 0, 0),	JIMM},
	[insn_jalr]	= {M(spec_op, 0, 0, 0, 0, jalr_op), RS | RD},
#ifndef CONFIG_CPU_MIPSR6
	[insn_jr]	= {M(spec_op, 0, 0, 0, 0, jr_op),  RS},
#else
	[insn_jr]	= {M(spec_op, 0, 0, 0, 0, jalr_op),  RS},
#endif
	[insn_lb]	= {M(lb_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_lbu]	= {M(lbu_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_ld]	= {M(ld_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_lddir]	= {M(lwc2_op, 0, 0, 0, lddir_op, mult_op), RS | RT | RD},
	[insn_ldpte]	= {M(lwc2_op, 0, 0, 0, ldpte_op, mult_op), RS | RD},
	[insn_ldx]	= {M(spec3_op, 0, 0, 0, ldx_op, lx_op), RS | RT | RD},
	[insn_lh]	= {M(lh_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_lhu]	= {M(lhu_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
#ifndef CONFIG_CPU_MIPSR6
	[insn_ll]	= {M(ll_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_lld]	= {M(lld_op, 0, 0, 0, 0, 0),	RS | RT | SIMM},
#else
	[insn_ll]	= {M6(spec3_op, 0, 0, 0, ll6_op),  RS | RT | SIMM9},
	[insn_lld]	= {M6(spec3_op, 0, 0, 0, lld6_op),  RS | RT | SIMM9},
#endif
	[insn_lui]	= {M(lui_op, 0, 0, 0, 0, 0),	RT | SIMM},
	[insn_lw]	= {M(lw_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_lwu]	= {M(lwu_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_lwx]	= {M(spec3_op, 0, 0, 0, lwx_op, lx_op), RS | RT | RD},
	[insn_mfc0]	= {M(cop0_op, mfc_op, 0, 0, 0, 0),  RT | RD | SET},
	[insn_mfhc0]	= {M(cop0_op, mfhc0_op, 0, 0, 0, 0),  RT | RD | SET},
	[insn_mfhi]	= {M(spec_op, 0, 0, 0, 0, mfhi_op), RD},
	[insn_mflo]	= {M(spec_op, 0, 0, 0, 0, mflo_op), RD},
	[insn_movn]	= {M(spec_op, 0, 0, 0, 0, movn_op), RS | RT | RD},
	[insn_movz]	= {M(spec_op, 0, 0, 0, 0, movz_op), RS | RT | RD},
	[insn_mtc0]	= {M(cop0_op, mtc_op, 0, 0, 0, 0),  RT | RD | SET},
	[insn_mthc0]	= {M(cop0_op, mthc0_op, 0, 0, 0, 0),  RT | RD | SET},
	[insn_mthi]	= {M(spec_op, 0, 0, 0, 0, mthi_op), RS},
	[insn_mtlo]	= {M(spec_op, 0, 0, 0, 0, mtlo_op), RS},
#ifndef CONFIG_CPU_MIPSR6
	[insn_mul]	= {M(spec2_op, 0, 0, 0, 0, mul_op), RS | RT | RD},
#else
	[insn_mul]	= {M(spec_op, 0, 0, 0, mult_mul_op, mult_op), RS | RT | RD},
#endif
	[insn_multu]	= {M(spec_op, 0, 0, 0, 0, multu_op), RS | RT},
	[insn_nor]	= {M(spec_op, 0, 0, 0, 0, nor_op),  RS | RT | RD},
	[insn_or]	= {M(spec_op, 0, 0, 0, 0, or_op),  RS | RT | RD},
	[insn_ori]	= {M(ori_op, 0, 0, 0, 0, 0),	RS | RT | UIMM},
#ifndef CONFIG_CPU_MIPSR6
	[insn_pref]	= {M(pref_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
#else
	[insn_pref]	= {M6(spec3_op, 0, 0, 0, pref6_op),  RS | RT | SIMM9},
#endif
	[insn_rfe]	= {M(cop0_op, cop_op, 0, 0, 0, rfe_op),  0},
	[insn_rotr]	= {M(spec_op, 1, 0, 0, 0, srl_op),  RT | RD | RE},
	[insn_sb]	= {M(sb_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
#ifndef CONFIG_CPU_MIPSR6
	[insn_sc]	= {M(sc_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_scd]	= {M(scd_op, 0, 0, 0, 0, 0),	RS | RT | SIMM},
#else
	[insn_sc]	= {M6(spec3_op, 0, 0, 0, sc6_op),  RS | RT | SIMM9},
	[insn_scd]	= {M6(spec3_op, 0, 0, 0, scd6_op),  RS | RT | SIMM9},
#endif
	[insn_sd]	= {M(sd_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_sh]	= {M(sh_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_sll]	= {M(spec_op, 0, 0, 0, 0, sll_op),  RT | RD | RE},
	[insn_sllv]	= {M(spec_op, 0, 0, 0, 0, sllv_op),  RS | RT | RD},
	[insn_slt]	= {M(spec_op, 0, 0, 0, 0, slt_op),  RS | RT | RD},
	[insn_slti]	= {M(slti_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_sltiu]	= {M(sltiu_op, 0, 0, 0, 0, 0), RS | RT | SIMM},
	[insn_sltu]	= {M(spec_op, 0, 0, 0, 0, sltu_op), RS | RT | RD},
	[insn_sra]	= {M(spec_op, 0, 0, 0, 0, sra_op),  RT | RD | RE},
	[insn_srav]	= {M(spec_op, 0, 0, 0, 0, srav_op), RS | RT | RD},
	[insn_srl]	= {M(spec_op, 0, 0, 0, 0, srl_op),  RT | RD | RE},
	[insn_srlv]	= {M(spec_op, 0, 0, 0, 0, srlv_op),  RS | RT | RD},
	[insn_subu]	= {M(spec_op, 0, 0, 0, 0, subu_op),	RS | RT | RD},
	[insn_sw]	= {M(sw_op, 0, 0, 0, 0, 0),  RS | RT | SIMM},
	[insn_sync]	= {M(spec_op, 0, 0, 0, 0, sync_op), RE},
	[insn_syscall]	= {M(spec_op, 0, 0, 0, 0, syscall_op), SCIMM},
	[insn_tlbp]	= {M(cop0_op, cop_op, 0, 0, 0, tlbp_op),  0},
	[insn_tlbr]	= {M(cop0_op, cop_op, 0, 0, 0, tlbr_op),  0},
	[insn_tlbwi]	= {M(cop0_op, cop_op, 0, 0, 0, tlbwi_op),  0},
	[insn_tlbwr]	= {M(cop0_op, cop_op, 0, 0, 0, tlbwr_op),  0},
	[insn_wait]	= {M(cop0_op, cop_op, 0, 0, 0, wait_op), SCIMM},
	[insn_wsbh]	= {M(spec3_op, 0, 0, 0, wsbh_op, bshfl_op), RT | RD},
	[insn_xor]	= {M(spec_op, 0, 0, 0, 0, xor_op),  RS | RT | RD},
	[insn_xori]	= {M(xori_op, 0, 0, 0, 0, 0),  RS | RT | UIMM},
	[insn_yield]	= {M(spec3_op, 0, 0, 0, 0, yield_op), RS | RD},
};

#undef M

static inline u32 build_bimm(s32 arg)
{
	WARN(arg > 0x1ffff || arg < -0x20000,
	     KERN_WARNING "Micro-assembler field overflow\n");

	WARN(arg & 0x3, KERN_WARNING "Invalid micro-assembler branch target\n");

	return ((arg < 0) ? (1 << 15) : 0) | ((arg >> 2) & 0x7fff);
}

static inline u32 build_jimm(u32 arg)
{
	WARN(arg & ~(JIMM_MASK << 2),
	     KERN_WARNING "Micro-assembler field overflow\n");

	return (arg >> 2) & JIMM_MASK;
}

/*
 * The order of opcode arguments is implicitly left to right,
 * starting with RS and ending with FUNC or IMM.
 */
static void build_insn(u32 **buf, enum opcode opc, ...)
{
	const struct insn *ip;
	va_list ap;
	u32 op;

	if (opc < 0 || opc >= insn_invalid ||
	    (opc == insn_daddiu && r4k_daddiu_bug()) ||
	    (insn_table[opc].match == 0 && insn_table[opc].fields == 0))
		panic("Unsupported Micro-assembler instruction %d", opc);

	ip = &insn_table[opc];

	op = ip->match;
	va_start(ap, opc);
	if (ip->fields & RS)
		op |= build_rs(va_arg(ap, u32));
	if (ip->fields & RT)
		op |= build_rt(va_arg(ap, u32));
	if (ip->fields & RD)
		op |= build_rd(va_arg(ap, u32));
	if (ip->fields & RE)
		op |= build_re(va_arg(ap, u32));
	if (ip->fields & SIMM)
		op |= build_simm(va_arg(ap, s32));
	if (ip->fields & UIMM)
		op |= build_uimm(va_arg(ap, u32));
	if (ip->fields & BIMM)
		op |= build_bimm(va_arg(ap, s32));
	if (ip->fields & JIMM)
		op |= build_jimm(va_arg(ap, u32));
	if (ip->fields & FUNC)
		op |= build_func(va_arg(ap, u32));
	if (ip->fields & SET)
		op |= build_set(va_arg(ap, u32));
	if (ip->fields & SCIMM)
		op |= build_scimm(va_arg(ap, u32));
	if (ip->fields & SIMM9)
		op |= build_scimm9(va_arg(ap, u32));
	va_end(ap);

	**buf = op;
	(*buf)++;
}

static inline void
__resolve_relocs(struct uasm_reloc *rel, struct uasm_label *lab)
{
	long laddr = (long)lab->addr;
	long raddr = (long)rel->addr;

	switch (rel->type) {
	case R_MIPS_PC16:
		*rel->addr |= build_bimm(laddr - (raddr + 4));
		break;

	default:
		panic("Unsupported Micro-assembler relocation %d",
		      rel->type);
	}
}
