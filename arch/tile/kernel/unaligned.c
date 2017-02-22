/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * A code-rewriter that handles unaligned exception.
 */

#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/extable.h>
#include <linux/compat.h>
#include <linux/prctl.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <arch/abi.h>
#include <arch/spr_def.h>
#include <arch/opcode.h>


/*
 * This file handles unaligned exception for tile-Gx. The tilepro's unaligned
 * exception is supported out of single_step.c
 */

int unaligned_printk;

static int __init setup_unaligned_printk(char *str)
{
	long val;
	if (kstrtol(str, 0, &val) != 0)
		return 0;
	unaligned_printk = val;
	pr_info("Printk for each unaligned data accesses is %s\n",
		unaligned_printk ? "enabled" : "disabled");
	return 1;
}
__setup("unaligned_printk=", setup_unaligned_printk);

unsigned int unaligned_fixup_count;

#ifdef __tilegx__

/*
 * Unalign data jit fixup code fragement. Reserved space is 128 bytes.
 * The 1st 64-bit word saves fault PC address, 2nd word is the fault
 * instruction bundle followed by 14 JIT bundles.
 */

struct unaligned_jit_fragment {
	unsigned long       pc;
	tilegx_bundle_bits  bundle;
	tilegx_bundle_bits  insn[14];
};

/*
 * Check if a nop or fnop at bundle's pipeline X0.
 */

static bool is_bundle_x0_nop(tilegx_bundle_bits bundle)
{
	return (((get_UnaryOpcodeExtension_X0(bundle) ==
		  NOP_UNARY_OPCODE_X0) &&
		 (get_RRROpcodeExtension_X0(bundle) ==
		  UNARY_RRR_0_OPCODE_X0) &&
		 (get_Opcode_X0(bundle) ==
		  RRR_0_OPCODE_X0)) ||
		((get_UnaryOpcodeExtension_X0(bundle) ==
		  FNOP_UNARY_OPCODE_X0) &&
		 (get_RRROpcodeExtension_X0(bundle) ==
		  UNARY_RRR_0_OPCODE_X0) &&
		 (get_Opcode_X0(bundle) ==
		  RRR_0_OPCODE_X0)));
}

/*
 * Check if nop or fnop at bundle's pipeline X1.
 */

static bool is_bundle_x1_nop(tilegx_bundle_bits bundle)
{
	return (((get_UnaryOpcodeExtension_X1(bundle) ==
		  NOP_UNARY_OPCODE_X1) &&
		 (get_RRROpcodeExtension_X1(bundle) ==
		  UNARY_RRR_0_OPCODE_X1) &&
		 (get_Opcode_X1(bundle) ==
		  RRR_0_OPCODE_X1)) ||
		((get_UnaryOpcodeExtension_X1(bundle) ==
		  FNOP_UNARY_OPCODE_X1) &&
		 (get_RRROpcodeExtension_X1(bundle) ==
		  UNARY_RRR_0_OPCODE_X1) &&
		 (get_Opcode_X1(bundle) ==
		  RRR_0_OPCODE_X1)));
}

/*
 * Check if nop or fnop at bundle's Y0 pipeline.
 */

static bool is_bundle_y0_nop(tilegx_bundle_bits bundle)
{
	return (((get_UnaryOpcodeExtension_Y0(bundle) ==
		  NOP_UNARY_OPCODE_Y0) &&
		 (get_RRROpcodeExtension_Y0(bundle) ==
		  UNARY_RRR_1_OPCODE_Y0) &&
		 (get_Opcode_Y0(bundle) ==
		  RRR_1_OPCODE_Y0)) ||
		((get_UnaryOpcodeExtension_Y0(bundle) ==
		  FNOP_UNARY_OPCODE_Y0) &&
		 (get_RRROpcodeExtension_Y0(bundle) ==
		  UNARY_RRR_1_OPCODE_Y0) &&
		 (get_Opcode_Y0(bundle) ==
		  RRR_1_OPCODE_Y0)));
}

/*
 * Check if nop or fnop at bundle's pipeline Y1.
 */

static bool is_bundle_y1_nop(tilegx_bundle_bits bundle)
{
	return (((get_UnaryOpcodeExtension_Y1(bundle) ==
		  NOP_UNARY_OPCODE_Y1) &&
		 (get_RRROpcodeExtension_Y1(bundle) ==
		  UNARY_RRR_1_OPCODE_Y1) &&
		 (get_Opcode_Y1(bundle) ==
		  RRR_1_OPCODE_Y1)) ||
		((get_UnaryOpcodeExtension_Y1(bundle) ==
		  FNOP_UNARY_OPCODE_Y1) &&
		 (get_RRROpcodeExtension_Y1(bundle) ==
		  UNARY_RRR_1_OPCODE_Y1) &&
		 (get_Opcode_Y1(bundle) ==
		  RRR_1_OPCODE_Y1)));
}

/*
 * Test if a bundle's y0 and y1 pipelines are both nop or fnop.
 */

static bool is_y0_y1_nop(tilegx_bundle_bits bundle)
{
	return is_bundle_y0_nop(bundle) && is_bundle_y1_nop(bundle);
}

/*
 * Test if a bundle's x0 and x1 pipelines are both nop or fnop.
 */

static bool is_x0_x1_nop(tilegx_bundle_bits bundle)
{
	return is_bundle_x0_nop(bundle) && is_bundle_x1_nop(bundle);
}

/*
 * Find the destination, source registers of fault unalign access instruction
 * at X1 or Y2. Also, allocate up to 3 scratch registers clob1, clob2 and
 * clob3, which are guaranteed different from any register used in the fault
 * bundle. r_alias is used to return if the other instructions other than the
 * unalign load/store shares same register with ra, rb and rd.
 */

static void find_regs(tilegx_bundle_bits bundle, uint64_t *rd, uint64_t *ra,
		      uint64_t *rb, uint64_t *clob1, uint64_t *clob2,
		      uint64_t *clob3, bool *r_alias)
{
	int i;
	uint64_t reg;
	uint64_t reg_map = 0, alias_reg_map = 0, map;
	bool alias = false;

	/*
	 * Parse fault bundle, find potential used registers and mark
	 * corresponding bits in reg_map and alias_map. These 2 bit maps
	 * are used to find the scratch registers and determine if there
	 * is register alias.
	 */
	if (bundle & TILEGX_BUNDLE_MODE_MASK) {  /* Y Mode Bundle. */

		reg = get_SrcA_Y2(bundle);
		reg_map |= 1ULL << reg;
		*ra = reg;
		reg = get_SrcBDest_Y2(bundle);
		reg_map |= 1ULL << reg;

		if (rd) {
			/* Load. */
			*rd = reg;
			alias_reg_map = (1ULL << *rd) | (1ULL << *ra);
		} else {
			/* Store. */
			*rb = reg;
			alias_reg_map = (1ULL << *ra) | (1ULL << *rb);
		}

		if (!is_bundle_y1_nop(bundle)) {
			reg = get_SrcA_Y1(bundle);
			reg_map |= (1ULL << reg);
			map = (1ULL << reg);

			reg = get_SrcB_Y1(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			reg = get_Dest_Y1(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			if (map & alias_reg_map)
				alias = true;
		}

		if (!is_bundle_y0_nop(bundle)) {
			reg = get_SrcA_Y0(bundle);
			reg_map |= (1ULL << reg);
			map = (1ULL << reg);

			reg = get_SrcB_Y0(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			reg = get_Dest_Y0(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			if (map & alias_reg_map)
				alias = true;
		}
	} else	{ /* X Mode Bundle. */

		reg = get_SrcA_X1(bundle);
		reg_map |= (1ULL << reg);
		*ra = reg;
		if (rd)	{
			/* Load. */
			reg = get_Dest_X1(bundle);
			reg_map |= (1ULL << reg);
			*rd = reg;
			alias_reg_map = (1ULL << *rd) | (1ULL << *ra);
		} else {
			/* Store. */
			reg = get_SrcB_X1(bundle);
			reg_map |= (1ULL << reg);
			*rb = reg;
			alias_reg_map = (1ULL << *ra) | (1ULL << *rb);
		}

		if (!is_bundle_x0_nop(bundle)) {
			reg = get_SrcA_X0(bundle);
			reg_map |= (1ULL << reg);
			map = (1ULL << reg);

			reg = get_SrcB_X0(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			reg = get_Dest_X0(bundle);
			reg_map |= (1ULL << reg);
			map |= (1ULL << reg);

			if (map & alias_reg_map)
				alias = true;
		}
	}

	/*
	 * "alias" indicates if the unalign access registers have collision
	 * with others in the same bundle. We jsut simply test all register
	 * operands case (RRR), ignored the case with immidate. If a bundle
	 * has no register alias, we may do fixup in a simple or fast manner.
	 * So if an immidata field happens to hit with a register, we may end
	 * up fall back to the generic handling.
	 */

	*r_alias = alias;

	/* Flip bits on reg_map. */
	reg_map ^= -1ULL;

	/* Scan reg_map lower 54(TREG_SP) bits to find 3 set bits. */
	for (i = 0; i < TREG_SP; i++) {
		if (reg_map & (0x1ULL << i)) {
			if (*clob1 == -1) {
				*clob1 = i;
			} else if (*clob2 == -1) {
				*clob2 = i;
			} else if (*clob3 == -1) {
				*clob3 = i;
				return;
			}
		}
	}
}

/*
 * Sanity check for register ra, rb, rd, clob1/2/3. Return true if any of them
 * is unexpected.
 */

static bool check_regs(uint64_t rd, uint64_t ra, uint64_t rb,
		       uint64_t clob1, uint64_t clob2,  uint64_t clob3)
{
	bool unexpected = false;
	if ((ra >= 56) && (ra != TREG_ZERO))
		unexpected = true;

	if ((clob1 >= 56) || (clob2 >= 56) || (clob3 >= 56))
		unexpected = true;

	if (rd != -1) {
		if ((rd >= 56) && (rd != TREG_ZERO))
			unexpected = true;
	} else {
		if ((rb >= 56) && (rb != TREG_ZERO))
			unexpected = true;
	}
	return unexpected;
}


#define  GX_INSN_X0_MASK   ((1ULL << 31) - 1)
#define  GX_INSN_X1_MASK   (((1ULL << 31) - 1) << 31)
#define  GX_INSN_Y0_MASK   ((0xFULL << 27) | (0xFFFFFULL))
#define  GX_INSN_Y1_MASK   (GX_INSN_Y0_MASK << 31)
#define  GX_INSN_Y2_MASK   ((0x7FULL << 51) | (0x7FULL << 20))

#ifdef __LITTLE_ENDIAN
#define  GX_INSN_BSWAP(_bundle_)    (_bundle_)
#else
#define  GX_INSN_BSWAP(_bundle_)    swab64(_bundle_)
#endif /* __LITTLE_ENDIAN */

/*
 * __JIT_CODE(.) creates template bundles in .rodata.unalign_data section.
 * The corresponding static function jix_x#_###(.) generates partial or
 * whole bundle based on the template and given arguments.
 */

#define __JIT_CODE(_X_)						\
	asm (".pushsection .rodata.unalign_data, \"a\"\n"	\
	     _X_"\n"						\
	     ".popsection\n")

__JIT_CODE("__unalign_jit_x1_mtspr:   {mtspr 0,  r0}");
static tilegx_bundle_bits jit_x1_mtspr(int spr, int reg)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_mtspr;
	return (GX_INSN_BSWAP(__unalign_jit_x1_mtspr) & GX_INSN_X1_MASK) |
		create_MT_Imm14_X1(spr) | create_SrcA_X1(reg);
}

__JIT_CODE("__unalign_jit_x1_mfspr:   {mfspr r0, 0}");
static tilegx_bundle_bits  jit_x1_mfspr(int reg, int spr)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_mfspr;
	return (GX_INSN_BSWAP(__unalign_jit_x1_mfspr) & GX_INSN_X1_MASK) |
		create_MF_Imm14_X1(spr) | create_Dest_X1(reg);
}

__JIT_CODE("__unalign_jit_x0_addi:   {addi  r0, r0, 0; iret}");
static tilegx_bundle_bits  jit_x0_addi(int rd, int ra, int imm8)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_addi;
	return (GX_INSN_BSWAP(__unalign_jit_x0_addi) & GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_Imm8_X0(imm8);
}

__JIT_CODE("__unalign_jit_x1_ldna:   {ldna  r0, r0}");
static tilegx_bundle_bits  jit_x1_ldna(int rd, int ra)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_ldna;
	return (GX_INSN_BSWAP(__unalign_jit_x1_ldna) &  GX_INSN_X1_MASK) |
		create_Dest_X1(rd) | create_SrcA_X1(ra);
}

__JIT_CODE("__unalign_jit_x0_dblalign:   {dblalign r0, r0 ,r0}");
static tilegx_bundle_bits  jit_x0_dblalign(int rd, int ra, int rb)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_dblalign;
	return (GX_INSN_BSWAP(__unalign_jit_x0_dblalign) & GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_SrcB_X0(rb);
}

__JIT_CODE("__unalign_jit_x1_iret:   {iret}");
static tilegx_bundle_bits  jit_x1_iret(void)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_iret;
	return GX_INSN_BSWAP(__unalign_jit_x1_iret) & GX_INSN_X1_MASK;
}

__JIT_CODE("__unalign_jit_x01_fnop:   {fnop;fnop}");
static tilegx_bundle_bits  jit_x0_fnop(void)
{
	extern  tilegx_bundle_bits __unalign_jit_x01_fnop;
	return GX_INSN_BSWAP(__unalign_jit_x01_fnop) & GX_INSN_X0_MASK;
}

static tilegx_bundle_bits  jit_x1_fnop(void)
{
	extern  tilegx_bundle_bits __unalign_jit_x01_fnop;
	return GX_INSN_BSWAP(__unalign_jit_x01_fnop) & GX_INSN_X1_MASK;
}

__JIT_CODE("__unalign_jit_y2_dummy:   {fnop; fnop; ld zero, sp}");
static tilegx_bundle_bits  jit_y2_dummy(void)
{
	extern  tilegx_bundle_bits __unalign_jit_y2_dummy;
	return GX_INSN_BSWAP(__unalign_jit_y2_dummy) & GX_INSN_Y2_MASK;
}

static tilegx_bundle_bits  jit_y1_fnop(void)
{
	extern  tilegx_bundle_bits __unalign_jit_y2_dummy;
	return GX_INSN_BSWAP(__unalign_jit_y2_dummy) & GX_INSN_Y1_MASK;
}

__JIT_CODE("__unalign_jit_x1_st1_add:  {st1_add r1, r0, 0}");
static tilegx_bundle_bits  jit_x1_st1_add(int ra, int rb, int imm8)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_st1_add;
	return (GX_INSN_BSWAP(__unalign_jit_x1_st1_add) &
		(~create_SrcA_X1(-1)) &
		GX_INSN_X1_MASK) | create_SrcA_X1(ra) |
		create_SrcB_X1(rb) | create_Dest_Imm8_X1(imm8);
}

__JIT_CODE("__unalign_jit_x1_st:  {crc32_8 r1, r0, r0; st  r0, r0}");
static tilegx_bundle_bits  jit_x1_st(int ra, int rb)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_st;
	return (GX_INSN_BSWAP(__unalign_jit_x1_st) & GX_INSN_X1_MASK) |
		create_SrcA_X1(ra) | create_SrcB_X1(rb);
}

__JIT_CODE("__unalign_jit_x1_st_add:  {st_add  r1, r0, 0}");
static tilegx_bundle_bits  jit_x1_st_add(int ra, int rb, int imm8)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_st_add;
	return (GX_INSN_BSWAP(__unalign_jit_x1_st_add) &
		(~create_SrcA_X1(-1)) &
		GX_INSN_X1_MASK) | create_SrcA_X1(ra) |
		create_SrcB_X1(rb) | create_Dest_Imm8_X1(imm8);
}

__JIT_CODE("__unalign_jit_x1_ld:  {crc32_8 r1, r0, r0; ld  r0, r0}");
static tilegx_bundle_bits  jit_x1_ld(int rd, int ra)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_ld;
	return (GX_INSN_BSWAP(__unalign_jit_x1_ld) & GX_INSN_X1_MASK) |
		create_Dest_X1(rd) | create_SrcA_X1(ra);
}

__JIT_CODE("__unalign_jit_x1_ld_add:  {ld_add  r1, r0, 0}");
static tilegx_bundle_bits  jit_x1_ld_add(int rd, int ra, int imm8)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_ld_add;
	return (GX_INSN_BSWAP(__unalign_jit_x1_ld_add) &
		(~create_Dest_X1(-1)) &
		GX_INSN_X1_MASK) | create_Dest_X1(rd) |
		create_SrcA_X1(ra) | create_Imm8_X1(imm8);
}

__JIT_CODE("__unalign_jit_x0_bfexts:  {bfexts r0, r0, 0, 0}");
static tilegx_bundle_bits  jit_x0_bfexts(int rd, int ra, int bfs, int bfe)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_bfexts;
	return (GX_INSN_BSWAP(__unalign_jit_x0_bfexts) &
		GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_BFStart_X0(bfs) | create_BFEnd_X0(bfe);
}

__JIT_CODE("__unalign_jit_x0_bfextu:  {bfextu r0, r0, 0, 0}");
static tilegx_bundle_bits  jit_x0_bfextu(int rd, int ra, int bfs, int bfe)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_bfextu;
	return (GX_INSN_BSWAP(__unalign_jit_x0_bfextu) &
		GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_BFStart_X0(bfs) | create_BFEnd_X0(bfe);
}

__JIT_CODE("__unalign_jit_x1_addi:  {bfextu r1, r1, 0, 0; addi r0, r0, 0}");
static tilegx_bundle_bits  jit_x1_addi(int rd, int ra, int imm8)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_addi;
	return (GX_INSN_BSWAP(__unalign_jit_x1_addi) & GX_INSN_X1_MASK) |
		create_Dest_X1(rd) | create_SrcA_X1(ra) |
		create_Imm8_X1(imm8);
}

__JIT_CODE("__unalign_jit_x0_shrui:  {shrui r0, r0, 0; iret}");
static tilegx_bundle_bits  jit_x0_shrui(int rd, int ra, int imm6)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_shrui;
	return (GX_INSN_BSWAP(__unalign_jit_x0_shrui) &
		GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_ShAmt_X0(imm6);
}

__JIT_CODE("__unalign_jit_x0_rotli:  {rotli r0, r0, 0; iret}");
static tilegx_bundle_bits  jit_x0_rotli(int rd, int ra, int imm6)
{
	extern  tilegx_bundle_bits __unalign_jit_x0_rotli;
	return (GX_INSN_BSWAP(__unalign_jit_x0_rotli) &
		GX_INSN_X0_MASK) |
		create_Dest_X0(rd) | create_SrcA_X0(ra) |
		create_ShAmt_X0(imm6);
}

__JIT_CODE("__unalign_jit_x1_bnezt:  {bnezt r0, __unalign_jit_x1_bnezt}");
static tilegx_bundle_bits  jit_x1_bnezt(int ra, int broff)
{
	extern  tilegx_bundle_bits __unalign_jit_x1_bnezt;
	return (GX_INSN_BSWAP(__unalign_jit_x1_bnezt) &
		GX_INSN_X1_MASK) |
		create_SrcA_X1(ra) | create_BrOff_X1(broff);
}

#undef __JIT_CODE

/*
 * This function generates unalign fixup JIT.
 *
 * We first find unalign load/store instruction's destination, source
 * registers: ra, rb and rd. and 3 scratch registers by calling
 * find_regs(...). 3 scratch clobbers should not alias with any register
 * used in the fault bundle. Then analyze the fault bundle to determine
 * if it's a load or store, operand width, branch or address increment etc.
 * At last generated JIT is copied into JIT code area in user space.
 */

static
void jit_bundle_gen(struct pt_regs *regs, tilegx_bundle_bits bundle,
		    int align_ctl)
{
	struct thread_info *info = current_thread_info();
	struct unaligned_jit_fragment frag;
	struct unaligned_jit_fragment *jit_code_area;
	tilegx_bundle_bits bundle_2 = 0;
	/* If bundle_2_enable = false, bundle_2 is fnop/nop operation. */
	bool     bundle_2_enable = true;
	uint64_t ra = -1, rb = -1, rd = -1, clob1 = -1, clob2 = -1, clob3 = -1;
	/*
	 * Indicate if the unalign access
	 * instruction's registers hit with
	 * others in the same bundle.
	 */
	bool     alias = false;
	bool     load_n_store = true;
	bool     load_store_signed = false;
	unsigned int  load_store_size = 8;
	bool     y1_br = false;  /* True, for a branch in same bundle at Y1.*/
	int      y1_br_reg = 0;
	/* True for link operation. i.e. jalr or lnk at Y1 */
	bool     y1_lr = false;
	int      y1_lr_reg = 0;
	bool     x1_add = false;/* True, for load/store ADD instruction at X1*/
	int      x1_add_imm8 = 0;
	bool     unexpected = false;
	int      n = 0, k;

	jit_code_area =
		(struct unaligned_jit_fragment *)(info->unalign_jit_base);

	memset((void *)&frag, 0, sizeof(frag));

	/* 0: X mode, Otherwise: Y mode. */
	if (bundle & TILEGX_BUNDLE_MODE_MASK) {
		unsigned int mod, opcode;

		if (get_Opcode_Y1(bundle) == RRR_1_OPCODE_Y1 &&
		    get_RRROpcodeExtension_Y1(bundle) ==
		    UNARY_RRR_1_OPCODE_Y1) {

			opcode = get_UnaryOpcodeExtension_Y1(bundle);

			/*
			 * Test "jalr", "jalrp", "jr", "jrp" instruction at Y1
			 * pipeline.
			 */
			switch (opcode) {
			case JALR_UNARY_OPCODE_Y1:
			case JALRP_UNARY_OPCODE_Y1:
				y1_lr = true;
				y1_lr_reg = 55; /* Link register. */
				/* FALLTHROUGH */
			case JR_UNARY_OPCODE_Y1:
			case JRP_UNARY_OPCODE_Y1:
				y1_br = true;
				y1_br_reg = get_SrcA_Y1(bundle);
				break;
			case LNK_UNARY_OPCODE_Y1:
				/* "lnk" at Y1 pipeline. */
				y1_lr = true;
				y1_lr_reg = get_Dest_Y1(bundle);
				break;
			}
		}

		opcode = get_Opcode_Y2(bundle);
		mod = get_Mode(bundle);

		/*
		 *  bundle_2 is bundle after making Y2 as a dummy operation
		 *  - ld zero, sp
		 */
		bundle_2 = (bundle & (~GX_INSN_Y2_MASK)) | jit_y2_dummy();

		/* Make Y1 as fnop if Y1 is a branch or lnk operation. */
		if (y1_br || y1_lr) {
			bundle_2 &= ~(GX_INSN_Y1_MASK);
			bundle_2 |= jit_y1_fnop();
		}

		if (is_y0_y1_nop(bundle_2))
			bundle_2_enable = false;

		if (mod == MODE_OPCODE_YC2) {
			/* Store. */
			load_n_store = false;
			load_store_size = 1 << opcode;
			load_store_signed = false;
			find_regs(bundle, 0, &ra, &rb, &clob1, &clob2,
				  &clob3, &alias);
			if (load_store_size > 8)
				unexpected = true;
		} else {
			/* Load. */
			load_n_store = true;
			if (mod == MODE_OPCODE_YB2) {
				switch (opcode) {
				case LD_OPCODE_Y2:
					load_store_signed = false;
					load_store_size = 8;
					break;
				case LD4S_OPCODE_Y2:
					load_store_signed = true;
					load_store_size = 4;
					break;
				case LD4U_OPCODE_Y2:
					load_store_signed = false;
					load_store_size = 4;
					break;
				default:
					unexpected = true;
				}
			} else if (mod == MODE_OPCODE_YA2) {
				if (opcode == LD2S_OPCODE_Y2) {
					load_store_signed = true;
					load_store_size = 2;
				} else if (opcode == LD2U_OPCODE_Y2) {
					load_store_signed = false;
					load_store_size = 2;
				} else
					unexpected = true;
			} else
				unexpected = true;
			find_regs(bundle, &rd, &ra, &rb, &clob1, &clob2,
				  &clob3, &alias);
		}
	} else {
		unsigned int opcode;

		/* bundle_2 is bundle after making X1 as "fnop". */
		bundle_2 = (bundle & (~GX_INSN_X1_MASK)) | jit_x1_fnop();

		if (is_x0_x1_nop(bundle_2))
			bundle_2_enable = false;

		if (get_Opcode_X1(bundle) == RRR_0_OPCODE_X1) {
			opcode = get_UnaryOpcodeExtension_X1(bundle);

			if (get_RRROpcodeExtension_X1(bundle) ==
			    UNARY_RRR_0_OPCODE_X1) {
				load_n_store = true;
				find_regs(bundle, &rd, &ra, &rb, &clob1,
					  &clob2, &clob3, &alias);

				switch (opcode) {
				case LD_UNARY_OPCODE_X1:
					load_store_signed = false;
					load_store_size = 8;
					break;
				case LD4S_UNARY_OPCODE_X1:
					load_store_signed = true;
					/* FALLTHROUGH */
				case LD4U_UNARY_OPCODE_X1:
					load_store_size = 4;
					break;

				case LD2S_UNARY_OPCODE_X1:
					load_store_signed = true;
					/* FALLTHROUGH */
				case LD2U_UNARY_OPCODE_X1:
					load_store_size = 2;
					break;
				default:
					unexpected = true;
				}
			} else {
				load_n_store = false;
				load_store_signed = false;
				find_regs(bundle, 0, &ra, &rb,
					  &clob1, &clob2, &clob3,
					  &alias);

				opcode = get_RRROpcodeExtension_X1(bundle);
				switch (opcode)	{
				case ST_RRR_0_OPCODE_X1:
					load_store_size = 8;
					break;
				case ST4_RRR_0_OPCODE_X1:
					load_store_size = 4;
					break;
				case ST2_RRR_0_OPCODE_X1:
					load_store_size = 2;
					break;
				default:
					unexpected = true;
				}
			}
		} else if (get_Opcode_X1(bundle) == IMM8_OPCODE_X1) {
			load_n_store = true;
			opcode = get_Imm8OpcodeExtension_X1(bundle);
			switch (opcode)	{
			case LD_ADD_IMM8_OPCODE_X1:
				load_store_size = 8;
				break;

			case LD4S_ADD_IMM8_OPCODE_X1:
				load_store_signed = true;
				/* FALLTHROUGH */
			case LD4U_ADD_IMM8_OPCODE_X1:
				load_store_size = 4;
				break;

			case LD2S_ADD_IMM8_OPCODE_X1:
				load_store_signed = true;
				/* FALLTHROUGH */
			case LD2U_ADD_IMM8_OPCODE_X1:
				load_store_size = 2;
				break;

			case ST_ADD_IMM8_OPCODE_X1:
				load_n_store = false;
				load_store_size = 8;
				break;
			case ST4_ADD_IMM8_OPCODE_X1:
				load_n_store = false;
				load_store_size = 4;
				break;
			case ST2_ADD_IMM8_OPCODE_X1:
				load_n_store = false;
				load_store_size = 2;
				break;
			default:
				unexpected = true;
			}

			if (!unexpected) {
				x1_add = true;
				if (load_n_store)
					x1_add_imm8 = get_Imm8_X1(bundle);
				else
					x1_add_imm8 = get_Dest_Imm8_X1(bundle);
			}

			find_regs(bundle, load_n_store ? (&rd) : NULL,
				  &ra, &rb, &clob1, &clob2, &clob3, &alias);
		} else
			unexpected = true;
	}

	/*
	 * Some sanity check for register numbers extracted from fault bundle.
	 */
	if (check_regs(rd, ra, rb, clob1, clob2, clob3) == true)
		unexpected = true;

	/* Give warning if register ra has an aligned address. */
	if (!unexpected)
		WARN_ON(!((load_store_size - 1) & (regs->regs[ra])));


	/*
	 * Fault came from kernel space, here we only need take care of
	 * unaligned "get_user/put_user" macros defined in "uaccess.h".
	 * Basically, we will handle bundle like this:
	 * {ld/2u/4s rd, ra; movei rx, 0} or {st/2/4 ra, rb; movei rx, 0}
	 * (Refer to file "arch/tile/include/asm/uaccess.h" for details).
	 * For either load or store, byte-wise operation is performed by calling
	 * get_user() or put_user(). If the macro returns non-zero value,
	 * set the value to rx, otherwise set zero to rx. Finally make pc point
	 * to next bundle and return.
	 */

	if (EX1_PL(regs->ex1) != USER_PL) {

		unsigned long rx = 0;
		unsigned long x = 0, ret = 0;

		if (y1_br || y1_lr || x1_add ||
		    (load_store_signed !=
		     (load_n_store && load_store_size == 4))) {
			/* No branch, link, wrong sign-ext or load/store add. */
			unexpected = true;
		} else if (!unexpected) {
			if (bundle & TILEGX_BUNDLE_MODE_MASK) {
				/*
				 * Fault bundle is Y mode.
				 * Check if the Y1 and Y0 is the form of
				 * { movei rx, 0; nop/fnop }, if yes,
				 * find the rx.
				 */

				if ((get_Opcode_Y1(bundle) == ADDI_OPCODE_Y1)
				    && (get_SrcA_Y1(bundle) == TREG_ZERO) &&
				    (get_Imm8_Y1(bundle) == 0) &&
				    is_bundle_y0_nop(bundle)) {
					rx = get_Dest_Y1(bundle);
				} else if ((get_Opcode_Y0(bundle) ==
					    ADDI_OPCODE_Y0) &&
					   (get_SrcA_Y0(bundle) == TREG_ZERO) &&
					   (get_Imm8_Y0(bundle) == 0) &&
					   is_bundle_y1_nop(bundle)) {
					rx = get_Dest_Y0(bundle);
				} else {
					unexpected = true;
				}
			} else {
				/*
				 * Fault bundle is X mode.
				 * Check if the X0 is 'movei rx, 0',
				 * if yes, find the rx.
				 */

				if ((get_Opcode_X0(bundle) == IMM8_OPCODE_X0)
				    && (get_Imm8OpcodeExtension_X0(bundle) ==
					ADDI_IMM8_OPCODE_X0) &&
				    (get_SrcA_X0(bundle) == TREG_ZERO) &&
				    (get_Imm8_X0(bundle) == 0)) {
					rx = get_Dest_X0(bundle);
				} else {
					unexpected = true;
				}
			}

			/* rx should be less than 56. */
			if (!unexpected && (rx >= 56))
				unexpected = true;
		}

		if (!search_exception_tables(regs->pc)) {
			/* No fixup in the exception tables for the pc. */
			unexpected = true;
		}

		if (unexpected) {
			/* Unexpected unalign kernel fault. */
			struct task_struct *tsk = validate_current();

			bust_spinlocks(1);

			show_regs(regs);

			if (unlikely(tsk->pid < 2)) {
				panic("Kernel unalign fault running %s!",
				      tsk->pid ? "init" : "the idle task");
			}
#ifdef SUPPORT_DIE
			die("Oops", regs);
#endif
			bust_spinlocks(1);

			do_group_exit(SIGKILL);

		} else {
			unsigned long i, b = 0;
			unsigned char *ptr =
				(unsigned char *)regs->regs[ra];
			if (load_n_store) {
				/* handle get_user(x, ptr) */
				for (i = 0; i < load_store_size; i++) {
					ret = get_user(b, ptr++);
					if (!ret) {
						/* Success! update x. */
#ifdef __LITTLE_ENDIAN
						x |= (b << (8 * i));
#else
						x <<= 8;
						x |= b;
#endif /* __LITTLE_ENDIAN */
					} else {
						x = 0;
						break;
					}
				}

				/* Sign-extend 4-byte loads. */
				if (load_store_size == 4)
					x = (long)(int)x;

				/* Set register rd. */
				regs->regs[rd] = x;

				/* Set register rx. */
				regs->regs[rx] = ret;

				/* Bump pc. */
				regs->pc += 8;

			} else {
				/* Handle put_user(x, ptr) */
				x = regs->regs[rb];
#ifdef __LITTLE_ENDIAN
				b = x;
#else
				/*
				 * Swap x in order to store x from low
				 * to high memory same as the
				 * little-endian case.
				 */
				switch (load_store_size) {
				case 8:
					b = swab64(x);
					break;
				case 4:
					b = swab32(x);
					break;
				case 2:
					b = swab16(x);
					break;
				}
#endif /* __LITTLE_ENDIAN */
				for (i = 0; i < load_store_size; i++) {
					ret = put_user(b, ptr++);
					if (ret)
						break;
					/* Success! shift 1 byte. */
					b >>= 8;
				}
				/* Set register rx. */
				regs->regs[rx] = ret;

				/* Bump pc. */
				regs->pc += 8;
			}
		}

		unaligned_fixup_count++;

		if (unaligned_printk) {
			pr_info("%s/%d - Unalign fixup for kernel access to userspace %lx\n",
				current->comm, current->pid, regs->regs[ra]);
		}

		/* Done! Return to the exception handler. */
		return;
	}

	if ((align_ctl == 0) || unexpected) {
		siginfo_t info = {
			.si_signo = SIGBUS,
			.si_code = BUS_ADRALN,
			.si_addr = (unsigned char __user *)0
		};
		if (unaligned_printk)
			pr_info("Unalign bundle: unexp @%llx, %llx\n",
				(unsigned long long)regs->pc,
				(unsigned long long)bundle);

		if (ra < 56) {
			unsigned long uaa = (unsigned long)regs->regs[ra];
			/* Set bus Address. */
			info.si_addr = (unsigned char __user *)uaa;
		}

		unaligned_fixup_count++;

		trace_unhandled_signal("unaligned fixup trap", regs,
				       (unsigned long)info.si_addr, SIGBUS);
		force_sig_info(info.si_signo, &info, current);
		return;
	}

#ifdef __LITTLE_ENDIAN
#define UA_FIXUP_ADDR_DELTA          1
#define UA_FIXUP_BFEXT_START(_B_)    0
#define UA_FIXUP_BFEXT_END(_B_)     (8 * (_B_) - 1)
#else /* __BIG_ENDIAN */
#define UA_FIXUP_ADDR_DELTA          -1
#define UA_FIXUP_BFEXT_START(_B_)   (64 - 8 * (_B_))
#define UA_FIXUP_BFEXT_END(_B_)      63
#endif /* __LITTLE_ENDIAN */



	if ((ra != rb) && (rd != TREG_SP) && !alias &&
	    !y1_br && !y1_lr && !x1_add) {
		/*
		 * Simple case: ra != rb and no register alias found,
		 * and no branch or link. This will be the majority.
		 * We can do a little better for simplae case than the
		 * generic scheme below.
		 */
		if (!load_n_store) {
			/*
			 * Simple store: ra != rb, no need for scratch register.
			 * Just store and rotate to right bytewise.
			 */
#ifdef __BIG_ENDIAN
			frag.insn[n++] =
				jit_x0_addi(ra, ra, load_store_size - 1) |
				jit_x1_fnop();
#endif /* __BIG_ENDIAN */
			for (k = 0; k < load_store_size; k++) {
				/* Store a byte. */
				frag.insn[n++] =
					jit_x0_rotli(rb, rb, 56) |
					jit_x1_st1_add(ra, rb,
						       UA_FIXUP_ADDR_DELTA);
			}
#ifdef __BIG_ENDIAN
			frag.insn[n] = jit_x1_addi(ra, ra, 1);
#else
			frag.insn[n] = jit_x1_addi(ra, ra,
						   -1 * load_store_size);
#endif /* __LITTLE_ENDIAN */

			if (load_store_size == 8) {
				frag.insn[n] |= jit_x0_fnop();
			} else if (load_store_size == 4) {
				frag.insn[n] |= jit_x0_rotli(rb, rb, 32);
			} else { /* = 2 */
				frag.insn[n] |= jit_x0_rotli(rb, rb, 16);
			}
			n++;
			if (bundle_2_enable)
				frag.insn[n++] = bundle_2;
			frag.insn[n++] = jit_x0_fnop() | jit_x1_iret();
		} else {
			if (rd == ra) {
				/* Use two clobber registers: clob1/2. */
				frag.insn[n++] =
					jit_x0_addi(TREG_SP, TREG_SP, -16) |
					jit_x1_fnop();
				frag.insn[n++] =
					jit_x0_addi(clob1, ra, 7) |
					jit_x1_st_add(TREG_SP, clob1, -8);
				frag.insn[n++] =
					jit_x0_addi(clob2, ra, 0) |
					jit_x1_st(TREG_SP, clob2);
				frag.insn[n++] =
					jit_x0_fnop() |
					jit_x1_ldna(rd, ra);
				frag.insn[n++] =
					jit_x0_fnop() |
					jit_x1_ldna(clob1, clob1);
				/*
				 * Note: we must make sure that rd must not
				 * be sp. Recover clob1/2 from stack.
				 */
				frag.insn[n++] =
					jit_x0_dblalign(rd, clob1, clob2) |
					jit_x1_ld_add(clob2, TREG_SP, 8);
				frag.insn[n++] =
					jit_x0_fnop() |
					jit_x1_ld_add(clob1, TREG_SP, 16);
			} else {
				/* Use one clobber register: clob1 only. */
				frag.insn[n++] =
					jit_x0_addi(TREG_SP, TREG_SP, -16) |
					jit_x1_fnop();
				frag.insn[n++] =
					jit_x0_addi(clob1, ra, 7) |
					jit_x1_st(TREG_SP, clob1);
				frag.insn[n++] =
					jit_x0_fnop() |
					jit_x1_ldna(rd, ra);
				frag.insn[n++] =
					jit_x0_fnop() |
					jit_x1_ldna(clob1, clob1);
				/*
				 * Note: we must make sure that rd must not
				 * be sp. Recover clob1 from stack.
				 */
				frag.insn[n++] =
					jit_x0_dblalign(rd, clob1, ra) |
					jit_x1_ld_add(clob1, TREG_SP, 16);
			}

			if (bundle_2_enable)
				frag.insn[n++] = bundle_2;
			/*
			 * For non 8-byte load, extract corresponding bytes and
			 * signed extension.
			 */
			if (load_store_size == 4) {
				if (load_store_signed)
					frag.insn[n++] =
						jit_x0_bfexts(
							rd, rd,
							UA_FIXUP_BFEXT_START(4),
							UA_FIXUP_BFEXT_END(4)) |
						jit_x1_fnop();
				else
					frag.insn[n++] =
						jit_x0_bfextu(
							rd, rd,
							UA_FIXUP_BFEXT_START(4),
							UA_FIXUP_BFEXT_END(4)) |
						jit_x1_fnop();
			} else if (load_store_size == 2) {
				if (load_store_signed)
					frag.insn[n++] =
						jit_x0_bfexts(
							rd, rd,
							UA_FIXUP_BFEXT_START(2),
							UA_FIXUP_BFEXT_END(2)) |
						jit_x1_fnop();
				else
					frag.insn[n++] =
						jit_x0_bfextu(
							rd, rd,
							UA_FIXUP_BFEXT_START(2),
							UA_FIXUP_BFEXT_END(2)) |
						jit_x1_fnop();
			}

			frag.insn[n++] =
				jit_x0_fnop()  |
				jit_x1_iret();
		}
	} else if (!load_n_store) {

		/*
		 * Generic memory store cases: use 3 clobber registers.
		 *
		 * Alloc space for saveing clob2,1,3 on user's stack.
		 * register clob3 points to where clob2 saved, followed by
		 * clob1 and 3 from high to low memory.
		 */
		frag.insn[n++] =
			jit_x0_addi(TREG_SP, TREG_SP, -32)    |
			jit_x1_fnop();
		frag.insn[n++] =
			jit_x0_addi(clob3, TREG_SP, 16)  |
			jit_x1_st_add(TREG_SP, clob3, 8);
#ifdef __LITTLE_ENDIAN
		frag.insn[n++] =
			jit_x0_addi(clob1, ra, 0)   |
			jit_x1_st_add(TREG_SP, clob1, 8);
#else
		frag.insn[n++] =
			jit_x0_addi(clob1, ra, load_store_size - 1)   |
			jit_x1_st_add(TREG_SP, clob1, 8);
#endif
		if (load_store_size == 8) {
			/*
			 * We save one byte a time, not for fast, but compact
			 * code. After each store, data source register shift
			 * right one byte. unchanged after 8 stores.
			 */
			frag.insn[n++] =
				jit_x0_addi(clob2, TREG_ZERO, 7)     |
				jit_x1_st_add(TREG_SP, clob2, 16);
			frag.insn[n++] =
				jit_x0_rotli(rb, rb, 56)      |
				jit_x1_st1_add(clob1, rb, UA_FIXUP_ADDR_DELTA);
			frag.insn[n++] =
				jit_x0_addi(clob2, clob2, -1) |
				jit_x1_bnezt(clob2, -1);
			frag.insn[n++] =
				jit_x0_fnop()                 |
				jit_x1_addi(clob2, y1_br_reg, 0);
		} else if (load_store_size == 4) {
			frag.insn[n++] =
				jit_x0_addi(clob2, TREG_ZERO, 3)     |
				jit_x1_st_add(TREG_SP, clob2, 16);
			frag.insn[n++] =
				jit_x0_rotli(rb, rb, 56)      |
				jit_x1_st1_add(clob1, rb, UA_FIXUP_ADDR_DELTA);
			frag.insn[n++] =
				jit_x0_addi(clob2, clob2, -1) |
				jit_x1_bnezt(clob2, -1);
			/*
			 * same as 8-byte case, but need shift another 4
			 * byte to recover rb for 4-byte store.
			 */
			frag.insn[n++] = jit_x0_rotli(rb, rb, 32)      |
				jit_x1_addi(clob2, y1_br_reg, 0);
		} else { /* =2 */
			frag.insn[n++] =
				jit_x0_addi(clob2, rb, 0)     |
				jit_x1_st_add(TREG_SP, clob2, 16);
			for (k = 0; k < 2; k++) {
				frag.insn[n++] =
					jit_x0_shrui(rb, rb, 8)  |
					jit_x1_st1_add(clob1, rb,
						       UA_FIXUP_ADDR_DELTA);
			}
			frag.insn[n++] =
				jit_x0_addi(rb, clob2, 0)       |
				jit_x1_addi(clob2, y1_br_reg, 0);
		}

		if (bundle_2_enable)
			frag.insn[n++] = bundle_2;

		if (y1_lr) {
			frag.insn[n++] =
				jit_x0_fnop()                    |
				jit_x1_mfspr(y1_lr_reg,
					     SPR_EX_CONTEXT_0_0);
		}
		if (y1_br) {
			frag.insn[n++] =
				jit_x0_fnop()                    |
				jit_x1_mtspr(SPR_EX_CONTEXT_0_0,
					     clob2);
		}
		if (x1_add) {
			frag.insn[n++] =
				jit_x0_addi(ra, ra, x1_add_imm8) |
				jit_x1_ld_add(clob2, clob3, -8);
		} else {
			frag.insn[n++] =
				jit_x0_fnop()                    |
				jit_x1_ld_add(clob2, clob3, -8);
		}
		frag.insn[n++] =
			jit_x0_fnop()   |
			jit_x1_ld_add(clob1, clob3, -8);
		frag.insn[n++] = jit_x0_fnop()   | jit_x1_ld(clob3, clob3);
		frag.insn[n++] = jit_x0_fnop()   | jit_x1_iret();

	} else {
		/*
		 * Generic memory load cases.
		 *
		 * Alloc space for saveing clob1,2,3 on user's stack.
		 * register clob3 points to where clob1 saved, followed
		 * by clob2 and 3 from high to low memory.
		 */

		frag.insn[n++] =
			jit_x0_addi(TREG_SP, TREG_SP, -32) |
			jit_x1_fnop();
		frag.insn[n++] =
			jit_x0_addi(clob3, TREG_SP, 16) |
			jit_x1_st_add(TREG_SP, clob3, 8);
		frag.insn[n++] =
			jit_x0_addi(clob2, ra, 0) |
			jit_x1_st_add(TREG_SP, clob2, 8);

		if (y1_br) {
			frag.insn[n++] =
				jit_x0_addi(clob1, y1_br_reg, 0) |
				jit_x1_st_add(TREG_SP, clob1, 16);
		} else {
			frag.insn[n++] =
				jit_x0_fnop() |
				jit_x1_st_add(TREG_SP, clob1, 16);
		}

		if (bundle_2_enable)
			frag.insn[n++] = bundle_2;

		if (y1_lr) {
			frag.insn[n++] =
				jit_x0_fnop()  |
				jit_x1_mfspr(y1_lr_reg,
					     SPR_EX_CONTEXT_0_0);
		}

		if (y1_br) {
			frag.insn[n++] =
				jit_x0_fnop() |
				jit_x1_mtspr(SPR_EX_CONTEXT_0_0,
					     clob1);
		}

		frag.insn[n++] =
			jit_x0_addi(clob1, clob2, 7)      |
			jit_x1_ldna(rd, clob2);
		frag.insn[n++] =
			jit_x0_fnop()                     |
			jit_x1_ldna(clob1, clob1);
		frag.insn[n++] =
			jit_x0_dblalign(rd, clob1, clob2) |
			jit_x1_ld_add(clob1, clob3, -8);
		if (x1_add) {
			frag.insn[n++] =
				jit_x0_addi(ra, ra, x1_add_imm8) |
				jit_x1_ld_add(clob2, clob3, -8);
		} else {
			frag.insn[n++] =
				jit_x0_fnop()  |
				jit_x1_ld_add(clob2, clob3, -8);
		}

		frag.insn[n++] =
			jit_x0_fnop() |
			jit_x1_ld(clob3, clob3);

		if (load_store_size == 4) {
			if (load_store_signed)
				frag.insn[n++] =
					jit_x0_bfexts(
						rd, rd,
						UA_FIXUP_BFEXT_START(4),
						UA_FIXUP_BFEXT_END(4)) |
					jit_x1_fnop();
			else
				frag.insn[n++] =
					jit_x0_bfextu(
						rd, rd,
						UA_FIXUP_BFEXT_START(4),
						UA_FIXUP_BFEXT_END(4)) |
					jit_x1_fnop();
		} else if (load_store_size == 2) {
			if (load_store_signed)
				frag.insn[n++] =
					jit_x0_bfexts(
						rd, rd,
						UA_FIXUP_BFEXT_START(2),
						UA_FIXUP_BFEXT_END(2)) |
					jit_x1_fnop();
			else
				frag.insn[n++] =
					jit_x0_bfextu(
						rd, rd,
						UA_FIXUP_BFEXT_START(2),
						UA_FIXUP_BFEXT_END(2)) |
					jit_x1_fnop();
		}

		frag.insn[n++] = jit_x0_fnop() | jit_x1_iret();
	}

	/* Max JIT bundle count is 14. */
	WARN_ON(n > 14);

	if (!unexpected) {
		int status = 0;
		int idx = (regs->pc >> 3) &
			((1ULL << (PAGE_SHIFT - UNALIGN_JIT_SHIFT)) - 1);

		frag.pc = regs->pc;
		frag.bundle = bundle;

		if (unaligned_printk) {
			pr_info("%s/%d, Unalign fixup: pc=%lx bundle=%lx %d %d %d %d %d %d %d %d\n",
				current->comm, current->pid,
				(unsigned long)frag.pc,
				(unsigned long)frag.bundle,
				(int)alias, (int)rd, (int)ra,
				(int)rb, (int)bundle_2_enable,
				(int)y1_lr, (int)y1_br, (int)x1_add);

			for (k = 0; k < n; k += 2)
				pr_info("[%d] %016llx %016llx\n",
					k, (unsigned long long)frag.insn[k],
					(unsigned long long)frag.insn[k+1]);
		}

		/* Swap bundle byte order for big endian sys. */
#ifdef __BIG_ENDIAN
		frag.bundle = GX_INSN_BSWAP(frag.bundle);
		for (k = 0; k < n; k++)
			frag.insn[k] = GX_INSN_BSWAP(frag.insn[k]);
#endif /* __BIG_ENDIAN */

		status = copy_to_user((void __user *)&jit_code_area[idx],
				      &frag, sizeof(frag));
		if (status) {
			/* Fail to copy JIT into user land. send SIGSEGV. */
			siginfo_t info = {
				.si_signo = SIGSEGV,
				.si_code = SEGV_MAPERR,
				.si_addr = (void __user *)&jit_code_area[idx]
			};

			pr_warn("Unalign fixup: pid=%d %s jit_code_area=%llx\n",
				current->pid, current->comm,
				(unsigned long long)&jit_code_area[idx]);

			trace_unhandled_signal("segfault in unalign fixup",
					       regs,
					       (unsigned long)info.si_addr,
					       SIGSEGV);
			force_sig_info(info.si_signo, &info, current);
			return;
		}


		/* Do a cheaper increment, not accurate. */
		unaligned_fixup_count++;
		__flush_icache_range((unsigned long)&jit_code_area[idx],
				     (unsigned long)&jit_code_area[idx] +
				     sizeof(frag));

		/* Setup SPR_EX_CONTEXT_0_0/1 for returning to user program.*/
		__insn_mtspr(SPR_EX_CONTEXT_0_0, regs->pc + 8);
		__insn_mtspr(SPR_EX_CONTEXT_0_1, PL_ICS_EX1(USER_PL, 0));

		/* Modify pc at the start of new JIT. */
		regs->pc = (unsigned long)&jit_code_area[idx].insn[0];
		/* Set ICS in SPR_EX_CONTEXT_K_1. */
		regs->ex1 = PL_ICS_EX1(USER_PL, 1);
	}
}


/*
 * C function to generate unalign data JIT. Called from unalign data
 * interrupt handler.
 *
 * First check if unalign fix is disabled or exception did not not come from
 * user space or sp register points to unalign address, if true, generate a
 * SIGBUS. Then map a page into user space as JIT area if it is not mapped
 * yet. Genenerate JIT code by calling jit_bundle_gen(). After that return
 * back to exception handler.
 *
 * The exception handler will "iret" to new generated JIT code after
 * restoring caller saved registers. In theory, the JIT code will perform
 * another "iret" to resume user's program.
 */

void do_unaligned(struct pt_regs *regs, int vecnum)
{
	tilegx_bundle_bits __user  *pc;
	tilegx_bundle_bits bundle;
	struct thread_info *info = current_thread_info();
	int align_ctl;

	/* Checks the per-process unaligned JIT flags */
	align_ctl = unaligned_fixup;
	switch (task_thread_info(current)->align_ctl) {
	case PR_UNALIGN_NOPRINT:
		align_ctl = 1;
		break;
	case PR_UNALIGN_SIGBUS:
		align_ctl = 0;
		break;
	}

	/* Enable iterrupt in order to access user land. */
	local_irq_enable();

	/*
	 * The fault came from kernel space. Two choices:
	 * (a) unaligned_fixup < 1, we will first call get/put_user fixup
	 *     to return -EFAULT. If no fixup, simply panic the kernel.
	 * (b) unaligned_fixup >=1, we will try to fix the unaligned access
	 *     if it was triggered by get_user/put_user() macros. Panic the
	 *     kernel if it is not fixable.
	 */

	if (EX1_PL(regs->ex1) != USER_PL) {

		if (align_ctl < 1) {
			unaligned_fixup_count++;
			/* If exception came from kernel, try fix it up. */
			if (fixup_exception(regs)) {
				if (unaligned_printk)
					pr_info("Unalign fixup: %d %llx @%llx\n",
						(int)unaligned_fixup,
						(unsigned long long)regs->ex1,
						(unsigned long long)regs->pc);
			} else {
				/* Not fixable. Go panic. */
				panic("Unalign exception in Kernel. pc=%lx",
				      regs->pc);
			}
		} else {
			/*
			 * Try to fix the exception. If we can't, panic the
			 * kernel.
			 */
			bundle = GX_INSN_BSWAP(
				*((tilegx_bundle_bits *)(regs->pc)));
			jit_bundle_gen(regs, bundle, align_ctl);
		}
		return;
	}

	/*
	 * Fault came from user with ICS or stack is not aligned.
	 * If so, we will trigger SIGBUS.
	 */
	if ((regs->sp & 0x7) || (regs->ex1) || (align_ctl < 0)) {
		siginfo_t info = {
			.si_signo = SIGBUS,
			.si_code = BUS_ADRALN,
			.si_addr = (unsigned char __user *)0
		};

		if (unaligned_printk)
			pr_info("Unalign fixup: %d %llx @%llx\n",
				(int)unaligned_fixup,
				(unsigned long long)regs->ex1,
				(unsigned long long)regs->pc);

		unaligned_fixup_count++;

		trace_unhandled_signal("unaligned fixup trap", regs, 0, SIGBUS);
		force_sig_info(info.si_signo, &info, current);
		return;
	}


	/* Read the bundle caused the exception! */
	pc = (tilegx_bundle_bits __user *)(regs->pc);
	if (get_user(bundle, pc) != 0) {
		/* Probably never be here since pc is valid user address.*/
		siginfo_t info = {
			.si_signo = SIGSEGV,
			.si_code = SEGV_MAPERR,
			.si_addr = (void __user *)pc
		};
		pr_err("Couldn't read instruction at %p trying to step\n", pc);
		trace_unhandled_signal("segfault in unalign fixup", regs,
				       (unsigned long)info.si_addr, SIGSEGV);
		force_sig_info(info.si_signo, &info, current);
		return;
	}

	if (!info->unalign_jit_base) {
		void __user *user_page;

		/*
		 * Allocate a page in userland.
		 * For 64-bit processes we try to place the mapping far
		 * from anything else that might be going on (specifically
		 * 64 GB below the top of the user address space).  If it
		 * happens not to be possible to put it there, it's OK;
		 * the kernel will choose another location and we'll
		 * remember it for later.
		 */
		if (is_compat_task())
			user_page = NULL;
		else
			user_page = (void __user *)(TASK_SIZE - (1UL << 36)) +
				(current->pid << PAGE_SHIFT);

		user_page = (void __user *) vm_mmap(NULL,
						    (unsigned long)user_page,
						    PAGE_SIZE,
						    PROT_EXEC | PROT_READ |
						    PROT_WRITE,
#ifdef CONFIG_HOMECACHE
						    MAP_CACHE_HOME_TASK |
#endif
						    MAP_PRIVATE |
						    MAP_ANONYMOUS,
						    0);

		if (IS_ERR((void __force *)user_page)) {
			pr_err("Out of kernel pages trying do_mmap\n");
			return;
		}

		/* Save the address in the thread_info struct */
		info->unalign_jit_base = user_page;
		if (unaligned_printk)
			pr_info("Unalign bundle: %d:%d, allocate page @%llx\n",
				raw_smp_processor_id(), current->pid,
				(unsigned long long)user_page);
	}

	/* Generate unalign JIT */
	jit_bundle_gen(regs, GX_INSN_BSWAP(bundle), align_ctl);
}

#endif /* __tilegx__ */
