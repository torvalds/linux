/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * A small micro-assembler. It is intentionally kept simple, does only
 * support a subset of instructions, and does not try to hide pipeline
 * effects like branch delay slots.
 *
 * Copyright (C) 2004, 2005, 2006, 2008  Thiemo Seufer
 * Copyright (C) 2005, 2007  Maciej W. Rozycki
 * Copyright (C) 2006  Ralf Baechle (ralf@linux-mips.org)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/inst.h>
#include <asm/elf.h>
#include <asm/bugs.h>
#include <asm/uasm.h>

enum fields {
	RS = 0x001,
	RT = 0x002,
	RD = 0x004,
	RE = 0x008,
	SIMM = 0x010,
	UIMM = 0x020,
	BIMM = 0x040,
	JIMM = 0x080,
	FUNC = 0x100,
	SET = 0x200,
	SCIMM = 0x400
};

#define OP_MASK		0x3f
#define OP_SH		26
#define RS_MASK		0x1f
#define RS_SH		21
#define RT_MASK		0x1f
#define RT_SH		16
#define RD_MASK		0x1f
#define RD_SH		11
#define RE_MASK		0x1f
#define RE_SH		6
#define IMM_MASK	0xffff
#define IMM_SH		0
#define JIMM_MASK	0x3ffffff
#define JIMM_SH		0
#define FUNC_MASK	0x3f
#define FUNC_SH		0
#define SET_MASK	0x7
#define SET_SH		0
#define SCIMM_MASK	0xfffff
#define SCIMM_SH	6

enum opcode {
	insn_invalid,
	insn_addu, insn_addiu, insn_and, insn_andi, insn_beq,
	insn_beql, insn_bgez, insn_bgezl, insn_bltz, insn_bltzl,
	insn_bne, insn_cache, insn_daddu, insn_daddiu, insn_dmfc0,
	insn_dmtc0, insn_dsll, insn_dsll32, insn_dsra, insn_dsrl,
	insn_dsrl32, insn_drotr, insn_drotr32, insn_dsubu, insn_eret,
	insn_j, insn_jal, insn_jr, insn_ld, insn_ll, insn_lld,
	insn_lui, insn_lw, insn_mfc0, insn_mtc0, insn_or, insn_ori,
	insn_pref, insn_rfe, insn_sc, insn_scd, insn_sd, insn_sll,
	insn_sra, insn_srl, insn_rotr, insn_subu, insn_sw, insn_tlbp,
	insn_tlbr, insn_tlbwi, insn_tlbwr, insn_xor, insn_xori,
	insn_dins, insn_syscall, insn_bbit0, insn_bbit1
};

struct insn {
	enum opcode opcode;
	u32 match;
	enum fields fields;
};

/* This macro sets the non-variable bits of an instruction. */
#define M(a, b, c, d, e, f)					\
	((a) << OP_SH						\
	 | (b) << RS_SH						\
	 | (c) << RT_SH						\
	 | (d) << RD_SH						\
	 | (e) << RE_SH						\
	 | (f) << FUNC_SH)

static struct insn insn_table[] __uasminitdata = {
	{ insn_addiu, M(addiu_op, 0, 0, 0, 0, 0), RS | RT | SIMM },
	{ insn_addu, M(spec_op, 0, 0, 0, 0, addu_op), RS | RT | RD },
	{ insn_and, M(spec_op, 0, 0, 0, 0, and_op), RS | RT | RD },
	{ insn_andi, M(andi_op, 0, 0, 0, 0, 0), RS | RT | UIMM },
	{ insn_beq, M(beq_op, 0, 0, 0, 0, 0), RS | RT | BIMM },
	{ insn_beql, M(beql_op, 0, 0, 0, 0, 0), RS | RT | BIMM },
	{ insn_bgez, M(bcond_op, 0, bgez_op, 0, 0, 0), RS | BIMM },
	{ insn_bgezl, M(bcond_op, 0, bgezl_op, 0, 0, 0), RS | BIMM },
	{ insn_bltz, M(bcond_op, 0, bltz_op, 0, 0, 0), RS | BIMM },
	{ insn_bltzl, M(bcond_op, 0, bltzl_op, 0, 0, 0), RS | BIMM },
	{ insn_bne, M(bne_op, 0, 0, 0, 0, 0), RS | RT | BIMM },
	{ insn_cache,  M(cache_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_daddiu, M(daddiu_op, 0, 0, 0, 0, 0), RS | RT | SIMM },
	{ insn_daddu, M(spec_op, 0, 0, 0, 0, daddu_op), RS | RT | RD },
	{ insn_dmfc0, M(cop0_op, dmfc_op, 0, 0, 0, 0), RT | RD | SET},
	{ insn_dmtc0, M(cop0_op, dmtc_op, 0, 0, 0, 0), RT | RD | SET},
	{ insn_dsll, M(spec_op, 0, 0, 0, 0, dsll_op), RT | RD | RE },
	{ insn_dsll32, M(spec_op, 0, 0, 0, 0, dsll32_op), RT | RD | RE },
	{ insn_dsra, M(spec_op, 0, 0, 0, 0, dsra_op), RT | RD | RE },
	{ insn_dsrl, M(spec_op, 0, 0, 0, 0, dsrl_op), RT | RD | RE },
	{ insn_dsrl32, M(spec_op, 0, 0, 0, 0, dsrl32_op), RT | RD | RE },
	{ insn_drotr, M(spec_op, 1, 0, 0, 0, dsrl_op), RT | RD | RE },
	{ insn_drotr32, M(spec_op, 1, 0, 0, 0, dsrl32_op), RT | RD | RE },
	{ insn_dsubu, M(spec_op, 0, 0, 0, 0, dsubu_op), RS | RT | RD },
	{ insn_eret,  M(cop0_op, cop_op, 0, 0, 0, eret_op),  0 },
	{ insn_j,  M(j_op, 0, 0, 0, 0, 0),  JIMM },
	{ insn_jal,  M(jal_op, 0, 0, 0, 0, 0),  JIMM },
	{ insn_jr,  M(spec_op, 0, 0, 0, 0, jr_op),  RS },
	{ insn_ld,  M(ld_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_ll,  M(ll_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_lld,  M(lld_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_lui,  M(lui_op, 0, 0, 0, 0, 0),  RT | SIMM },
	{ insn_lw,  M(lw_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_mfc0,  M(cop0_op, mfc_op, 0, 0, 0, 0),  RT | RD | SET},
	{ insn_mtc0,  M(cop0_op, mtc_op, 0, 0, 0, 0),  RT | RD | SET},
	{ insn_or,  M(spec_op, 0, 0, 0, 0, or_op),  RS | RT | RD },
	{ insn_ori,  M(ori_op, 0, 0, 0, 0, 0),  RS | RT | UIMM },
	{ insn_pref,  M(pref_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_rfe,  M(cop0_op, cop_op, 0, 0, 0, rfe_op),  0 },
	{ insn_sc,  M(sc_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_scd,  M(scd_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_sd,  M(sd_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_sll,  M(spec_op, 0, 0, 0, 0, sll_op),  RT | RD | RE },
	{ insn_sra,  M(spec_op, 0, 0, 0, 0, sra_op),  RT | RD | RE },
	{ insn_srl,  M(spec_op, 0, 0, 0, 0, srl_op),  RT | RD | RE },
	{ insn_rotr,  M(spec_op, 1, 0, 0, 0, srl_op),  RT | RD | RE },
	{ insn_subu,  M(spec_op, 0, 0, 0, 0, subu_op),  RS | RT | RD },
	{ insn_sw,  M(sw_op, 0, 0, 0, 0, 0),  RS | RT | SIMM },
	{ insn_tlbp,  M(cop0_op, cop_op, 0, 0, 0, tlbp_op),  0 },
	{ insn_tlbr,  M(cop0_op, cop_op, 0, 0, 0, tlbr_op),  0 },
	{ insn_tlbwi,  M(cop0_op, cop_op, 0, 0, 0, tlbwi_op),  0 },
	{ insn_tlbwr,  M(cop0_op, cop_op, 0, 0, 0, tlbwr_op),  0 },
	{ insn_xor,  M(spec_op, 0, 0, 0, 0, xor_op),  RS | RT | RD },
	{ insn_xori,  M(xori_op, 0, 0, 0, 0, 0),  RS | RT | UIMM },
	{ insn_dins, M(spec3_op, 0, 0, 0, 0, dins_op), RS | RT | RD | RE },
	{ insn_syscall, M(spec_op, 0, 0, 0, 0, syscall_op), SCIMM},
	{ insn_bbit0, M(lwc2_op, 0, 0, 0, 0, 0), RS | RT | BIMM },
	{ insn_bbit1, M(swc2_op, 0, 0, 0, 0, 0), RS | RT | BIMM },
	{ insn_invalid, 0, 0 }
};

#undef M

static inline __uasminit u32 build_rs(u32 arg)
{
	if (arg & ~RS_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RS_MASK) << RS_SH;
}

static inline __uasminit u32 build_rt(u32 arg)
{
	if (arg & ~RT_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RT_MASK) << RT_SH;
}

static inline __uasminit u32 build_rd(u32 arg)
{
	if (arg & ~RD_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RD_MASK) << RD_SH;
}

static inline __uasminit u32 build_re(u32 arg)
{
	if (arg & ~RE_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RE_MASK) << RE_SH;
}

static inline __uasminit u32 build_simm(s32 arg)
{
	if (arg > 0x7fff || arg < -0x8000)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return arg & 0xffff;
}

static inline __uasminit u32 build_uimm(u32 arg)
{
	if (arg & ~IMM_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return arg & IMM_MASK;
}

static inline __uasminit u32 build_bimm(s32 arg)
{
	if (arg > 0x1ffff || arg < -0x20000)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	if (arg & 0x3)
		printk(KERN_WARNING "Invalid micro-assembler branch target\n");

	return ((arg < 0) ? (1 << 15) : 0) | ((arg >> 2) & 0x7fff);
}

static inline __uasminit u32 build_jimm(u32 arg)
{
	if (arg & ~((JIMM_MASK) << 2))
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg >> 2) & JIMM_MASK;
}

static inline __uasminit u32 build_scimm(u32 arg)
{
	if (arg & ~SCIMM_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & SCIMM_MASK) << SCIMM_SH;
}

static inline __uasminit u32 build_func(u32 arg)
{
	if (arg & ~FUNC_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return arg & FUNC_MASK;
}

static inline __uasminit u32 build_set(u32 arg)
{
	if (arg & ~SET_MASK)
		printk(KERN_WARNING "Micro-assembler field overflow\n");

	return arg & SET_MASK;
}

/*
 * The order of opcode arguments is implicitly left to right,
 * starting with RS and ending with FUNC or IMM.
 */
static void __uasminit build_insn(u32 **buf, enum opcode opc, ...)
{
	struct insn *ip = NULL;
	unsigned int i;
	va_list ap;
	u32 op;

	for (i = 0; insn_table[i].opcode != insn_invalid; i++)
		if (insn_table[i].opcode == opc) {
			ip = &insn_table[i];
			break;
		}

	if (!ip || (opc == insn_daddiu && r4k_daddiu_bug()))
		panic("Unsupported Micro-assembler instruction %d", opc);

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
	va_end(ap);

	**buf = op;
	(*buf)++;
}

#define I_u1u2u3(op)					\
Ip_u1u2u3(op)						\
{							\
	build_insn(buf, insn##op, a, b, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1u3(op)					\
Ip_u2u1u3(op)						\
{							\
	build_insn(buf, insn##op, b, a, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u3u1u2(op)					\
Ip_u3u1u2(op)						\
{							\
	build_insn(buf, insn##op, b, c, a);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u1u2s3(op)					\
Ip_u1u2s3(op)						\
{							\
	build_insn(buf, insn##op, a, b, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2s3u1(op)					\
Ip_u2s3u1(op)						\
{							\
	build_insn(buf, insn##op, c, a, b);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1s3(op)					\
Ip_u2u1s3(op)						\
{							\
	build_insn(buf, insn##op, b, a, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1msbu3(op)					\
Ip_u2u1msbu3(op)					\
{							\
	build_insn(buf, insn##op, b, a, c+d-1, c);	\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u1u2(op)					\
Ip_u1u2(op)						\
{							\
	build_insn(buf, insn##op, a, b);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u1s2(op)					\
Ip_u1s2(op)						\
{							\
	build_insn(buf, insn##op, a, b);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u1(op)					\
Ip_u1(op)						\
{							\
	build_insn(buf, insn##op, a);			\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_0(op)						\
Ip_0(op)						\
{							\
	build_insn(buf, insn##op);			\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

I_u2u1s3(_addiu)
I_u3u1u2(_addu)
I_u2u1u3(_andi)
I_u3u1u2(_and)
I_u1u2s3(_beq)
I_u1u2s3(_beql)
I_u1s2(_bgez)
I_u1s2(_bgezl)
I_u1s2(_bltz)
I_u1s2(_bltzl)
I_u1u2s3(_bne)
I_u2s3u1(_cache)
I_u1u2u3(_dmfc0)
I_u1u2u3(_dmtc0)
I_u2u1s3(_daddiu)
I_u3u1u2(_daddu)
I_u2u1u3(_dsll)
I_u2u1u3(_dsll32)
I_u2u1u3(_dsra)
I_u2u1u3(_dsrl)
I_u2u1u3(_dsrl32)
I_u2u1u3(_drotr)
I_u2u1u3(_drotr32)
I_u3u1u2(_dsubu)
I_0(_eret)
I_u1(_j)
I_u1(_jal)
I_u1(_jr)
I_u2s3u1(_ld)
I_u2s3u1(_ll)
I_u2s3u1(_lld)
I_u1s2(_lui)
I_u2s3u1(_lw)
I_u1u2u3(_mfc0)
I_u1u2u3(_mtc0)
I_u2u1u3(_ori)
I_u3u1u2(_or)
I_u2s3u1(_pref)
I_0(_rfe)
I_u2s3u1(_sc)
I_u2s3u1(_scd)
I_u2s3u1(_sd)
I_u2u1u3(_sll)
I_u2u1u3(_sra)
I_u2u1u3(_srl)
I_u2u1u3(_rotr)
I_u3u1u2(_subu)
I_u2s3u1(_sw)
I_0(_tlbp)
I_0(_tlbr)
I_0(_tlbwi)
I_0(_tlbwr)
I_u3u1u2(_xor)
I_u2u1u3(_xori)
I_u2u1msbu3(_dins);
I_u1(_syscall);
I_u1u2s3(_bbit0);
I_u1u2s3(_bbit1);

/* Handle labels. */
void __uasminit uasm_build_label(struct uasm_label **lab, u32 *addr, int lid)
{
	(*lab)->addr = addr;
	(*lab)->lab = lid;
	(*lab)++;
}
UASM_EXPORT_SYMBOL(uasm_build_label);

int __uasminit uasm_in_compat_space_p(long addr)
{
	/* Is this address in 32bit compat space? */
#ifdef CONFIG_64BIT
	return (((addr) & 0xffffffff00000000L) == 0xffffffff00000000L);
#else
	return 1;
#endif
}
UASM_EXPORT_SYMBOL(uasm_in_compat_space_p);

static int __uasminit uasm_rel_highest(long val)
{
#ifdef CONFIG_64BIT
	return ((((val + 0x800080008000L) >> 48) & 0xffff) ^ 0x8000) - 0x8000;
#else
	return 0;
#endif
}

static int __uasminit uasm_rel_higher(long val)
{
#ifdef CONFIG_64BIT
	return ((((val + 0x80008000L) >> 32) & 0xffff) ^ 0x8000) - 0x8000;
#else
	return 0;
#endif
}

int __uasminit uasm_rel_hi(long val)
{
	return ((((val + 0x8000L) >> 16) & 0xffff) ^ 0x8000) - 0x8000;
}
UASM_EXPORT_SYMBOL(uasm_rel_hi);

int __uasminit uasm_rel_lo(long val)
{
	return ((val & 0xffff) ^ 0x8000) - 0x8000;
}
UASM_EXPORT_SYMBOL(uasm_rel_lo);

void __uasminit UASM_i_LA_mostly(u32 **buf, unsigned int rs, long addr)
{
	if (!uasm_in_compat_space_p(addr)) {
		uasm_i_lui(buf, rs, uasm_rel_highest(addr));
		if (uasm_rel_higher(addr))
			uasm_i_daddiu(buf, rs, rs, uasm_rel_higher(addr));
		if (uasm_rel_hi(addr)) {
			uasm_i_dsll(buf, rs, rs, 16);
			uasm_i_daddiu(buf, rs, rs, uasm_rel_hi(addr));
			uasm_i_dsll(buf, rs, rs, 16);
		} else
			uasm_i_dsll32(buf, rs, rs, 0);
	} else
		uasm_i_lui(buf, rs, uasm_rel_hi(addr));
}
UASM_EXPORT_SYMBOL(UASM_i_LA_mostly);

void __uasminit UASM_i_LA(u32 **buf, unsigned int rs, long addr)
{
	UASM_i_LA_mostly(buf, rs, addr);
	if (uasm_rel_lo(addr)) {
		if (!uasm_in_compat_space_p(addr))
			uasm_i_daddiu(buf, rs, rs, uasm_rel_lo(addr));
		else
			uasm_i_addiu(buf, rs, rs, uasm_rel_lo(addr));
	}
}
UASM_EXPORT_SYMBOL(UASM_i_LA);

/* Handle relocations. */
void __uasminit
uasm_r_mips_pc16(struct uasm_reloc **rel, u32 *addr, int lid)
{
	(*rel)->addr = addr;
	(*rel)->type = R_MIPS_PC16;
	(*rel)->lab = lid;
	(*rel)++;
}
UASM_EXPORT_SYMBOL(uasm_r_mips_pc16);

static inline void __uasminit
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

void __uasminit
uasm_resolve_relocs(struct uasm_reloc *rel, struct uasm_label *lab)
{
	struct uasm_label *l;

	for (; rel->lab != UASM_LABEL_INVALID; rel++)
		for (l = lab; l->lab != UASM_LABEL_INVALID; l++)
			if (rel->lab == l->lab)
				__resolve_relocs(rel, l);
}
UASM_EXPORT_SYMBOL(uasm_resolve_relocs);

void __uasminit
uasm_move_relocs(struct uasm_reloc *rel, u32 *first, u32 *end, long off)
{
	for (; rel->lab != UASM_LABEL_INVALID; rel++)
		if (rel->addr >= first && rel->addr < end)
			rel->addr += off;
}
UASM_EXPORT_SYMBOL(uasm_move_relocs);

void __uasminit
uasm_move_labels(struct uasm_label *lab, u32 *first, u32 *end, long off)
{
	for (; lab->lab != UASM_LABEL_INVALID; lab++)
		if (lab->addr >= first && lab->addr < end)
			lab->addr += off;
}
UASM_EXPORT_SYMBOL(uasm_move_labels);

void __uasminit
uasm_copy_handler(struct uasm_reloc *rel, struct uasm_label *lab, u32 *first,
		  u32 *end, u32 *target)
{
	long off = (long)(target - first);

	memcpy(target, first, (end - first) * sizeof(u32));

	uasm_move_relocs(rel, first, end, off);
	uasm_move_labels(lab, first, end, off);
}
UASM_EXPORT_SYMBOL(uasm_copy_handler);

int __uasminit uasm_insn_has_bdelay(struct uasm_reloc *rel, u32 *addr)
{
	for (; rel->lab != UASM_LABEL_INVALID; rel++) {
		if (rel->addr == addr
		    && (rel->type == R_MIPS_PC16
			|| rel->type == R_MIPS_26))
			return 1;
	}

	return 0;
}
UASM_EXPORT_SYMBOL(uasm_insn_has_bdelay);

/* Convenience functions for labeled branches. */
void __uasminit
uasm_il_bltz(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bltz(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bltz);

void __uasminit
uasm_il_b(u32 **p, struct uasm_reloc **r, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_b(p, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_b);

void __uasminit
uasm_il_beqz(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_beqz(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_beqz);

void __uasminit
uasm_il_beqzl(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_beqzl(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_beqzl);

void __uasminit
uasm_il_bne(u32 **p, struct uasm_reloc **r, unsigned int reg1,
	unsigned int reg2, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bne(p, reg1, reg2, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bne);

void __uasminit
uasm_il_bnez(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bnez(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bnez);

void __uasminit
uasm_il_bgezl(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bgezl(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bgezl);

void __uasminit
uasm_il_bgez(u32 **p, struct uasm_reloc **r, unsigned int reg, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bgez(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bgez);

void __uasminit
uasm_il_bbit0(u32 **p, struct uasm_reloc **r, unsigned int reg,
	      unsigned int bit, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bbit0(p, reg, bit, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bbit0);

void __uasminit
uasm_il_bbit1(u32 **p, struct uasm_reloc **r, unsigned int reg,
	      unsigned int bit, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bbit1(p, reg, bit, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bbit1);
