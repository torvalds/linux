/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Synthesize TLB refill handlers at runtime.
 *
 * Copyright (C) 2004,2005 by Thiemo Seufer
 * Copyright (C) 2005  Maciej W. Rozycki
 */

#include <stdarg.h>

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/inst.h>
#include <asm/elf.h>
#include <asm/smp.h>
#include <asm/war.h>

/* #define DEBUG_TLB */

static __init int __attribute__((unused)) r45k_bvahwbug(void)
{
	/* XXX: We should probe for the presence of this bug, but we don't. */
	return 0;
}

static __init int __attribute__((unused)) r4k_250MHZhwbug(void)
{
	/* XXX: We should probe for the presence of this bug, but we don't. */
	return 0;
}

static __init int __attribute__((unused)) bcm1250_m3_war(void)
{
	return BCM1250_M3_WAR;
}

static __init int __attribute__((unused)) r10000_llsc_war(void)
{
	return R10000_LLSC_WAR;
}

/*
 * A little micro-assembler, intended for TLB refill handler
 * synthesizing. It is intentionally kept simple, does only support
 * a subset of instructions, and does not try to hide pipeline effects
 * like branch delay slots.
 */

enum fields
{
	RS = 0x001,
	RT = 0x002,
	RD = 0x004,
	RE = 0x008,
	SIMM = 0x010,
	UIMM = 0x020,
	BIMM = 0x040,
	JIMM = 0x080,
	FUNC = 0x100,
};

#define OP_MASK		0x2f
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
#define FUNC_MASK	0x2f
#define FUNC_SH		0

enum opcode {
	insn_invalid,
	insn_addu, insn_addiu, insn_and, insn_andi, insn_beq,
	insn_beql, insn_bgez, insn_bgezl, insn_bltz, insn_bltzl,
	insn_bne, insn_daddu, insn_daddiu, insn_dmfc0, insn_dmtc0,
	insn_dsll, insn_dsll32, insn_dsra, insn_dsrl,
	insn_dsubu, insn_eret, insn_j, insn_jal, insn_jr, insn_ld,
	insn_ll, insn_lld, insn_lui, insn_lw, insn_mfc0, insn_mtc0,
	insn_ori, insn_rfe, insn_sc, insn_scd, insn_sd, insn_sll,
	insn_sra, insn_srl, insn_subu, insn_sw, insn_tlbp, insn_tlbwi,
	insn_tlbwr, insn_xor, insn_xori
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

static __initdata struct insn insn_table[] = {
	{ insn_addiu, M(addiu_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_addu, M(spec_op,0,0,0,0,addu_op), RS | RT | RD },
	{ insn_and, M(spec_op,0,0,0,0,and_op), RS | RT | RD },
	{ insn_andi, M(andi_op,0,0,0,0,0), RS | RT | UIMM },
	{ insn_beq, M(beq_op,0,0,0,0,0), RS | RT | BIMM },
	{ insn_beql, M(beql_op,0,0,0,0,0), RS | RT | BIMM },
	{ insn_bgez, M(bcond_op,0,bgez_op,0,0,0), RS | BIMM },
	{ insn_bgezl, M(bcond_op,0,bgezl_op,0,0,0), RS | BIMM },
	{ insn_bltz, M(bcond_op,0,bltz_op,0,0,0), RS | BIMM },
	{ insn_bltzl, M(bcond_op,0,bltzl_op,0,0,0), RS | BIMM },
	{ insn_bne, M(bne_op,0,0,0,0,0), RS | RT | BIMM },
	{ insn_daddiu, M(daddiu_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_daddu, M(spec_op,0,0,0,0,daddu_op), RS | RT | RD },
	{ insn_dmfc0, M(cop0_op,dmfc_op,0,0,0,0), RT | RD },
	{ insn_dmtc0, M(cop0_op,dmtc_op,0,0,0,0), RT | RD },
	{ insn_dsll, M(spec_op,0,0,0,0,dsll_op), RT | RD | RE },
	{ insn_dsll32, M(spec_op,0,0,0,0,dsll32_op), RT | RD | RE },
	{ insn_dsra, M(spec_op,0,0,0,0,dsra_op), RT | RD | RE },
	{ insn_dsrl, M(spec_op,0,0,0,0,dsrl_op), RT | RD | RE },
	{ insn_dsubu, M(spec_op,0,0,0,0,dsubu_op), RS | RT | RD },
	{ insn_eret, M(cop0_op,cop_op,0,0,0,eret_op), 0 },
	{ insn_j, M(j_op,0,0,0,0,0), JIMM },
	{ insn_jal, M(jal_op,0,0,0,0,0), JIMM },
	{ insn_jr, M(spec_op,0,0,0,0,jr_op), RS },
	{ insn_ld, M(ld_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_ll, M(ll_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_lld, M(lld_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_lui, M(lui_op,0,0,0,0,0), RT | SIMM },
	{ insn_lw, M(lw_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_mfc0, M(cop0_op,mfc_op,0,0,0,0), RT | RD },
	{ insn_mtc0, M(cop0_op,mtc_op,0,0,0,0), RT | RD },
	{ insn_ori, M(ori_op,0,0,0,0,0), RS | RT | UIMM },
	{ insn_rfe, M(cop0_op,cop_op,0,0,0,rfe_op), 0 },
	{ insn_sc, M(sc_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_scd, M(scd_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_sd, M(sd_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_sll, M(spec_op,0,0,0,0,sll_op), RT | RD | RE },
	{ insn_sra, M(spec_op,0,0,0,0,sra_op), RT | RD | RE },
	{ insn_srl, M(spec_op,0,0,0,0,srl_op), RT | RD | RE },
	{ insn_subu, M(spec_op,0,0,0,0,subu_op), RS | RT | RD },
	{ insn_sw, M(sw_op,0,0,0,0,0), RS | RT | SIMM },
	{ insn_tlbp, M(cop0_op,cop_op,0,0,0,tlbp_op), 0 },
	{ insn_tlbwi, M(cop0_op,cop_op,0,0,0,tlbwi_op), 0 },
	{ insn_tlbwr, M(cop0_op,cop_op,0,0,0,tlbwr_op), 0 },
	{ insn_xor, M(spec_op,0,0,0,0,xor_op), RS | RT | RD },
	{ insn_xori, M(xori_op,0,0,0,0,0), RS | RT | UIMM },
	{ insn_invalid, 0, 0 }
};

#undef M

static __init u32 build_rs(u32 arg)
{
	if (arg & ~RS_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return (arg & RS_MASK) << RS_SH;
}

static __init u32 build_rt(u32 arg)
{
	if (arg & ~RT_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return (arg & RT_MASK) << RT_SH;
}

static __init u32 build_rd(u32 arg)
{
	if (arg & ~RD_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return (arg & RD_MASK) << RD_SH;
}

static __init u32 build_re(u32 arg)
{
	if (arg & ~RE_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return (arg & RE_MASK) << RE_SH;
}

static __init u32 build_simm(s32 arg)
{
	if (arg > 0x7fff || arg < -0x8000)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return arg & 0xffff;
}

static __init u32 build_uimm(u32 arg)
{
	if (arg & ~IMM_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return arg & IMM_MASK;
}

static __init u32 build_bimm(s32 arg)
{
	if (arg > 0x1ffff || arg < -0x20000)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	if (arg & 0x3)
		printk(KERN_WARNING "Invalid TLB synthesizer branch target\n");

	return ((arg < 0) ? (1 << 15) : 0) | ((arg >> 2) & 0x7fff);
}

static __init u32 build_jimm(u32 arg)
{
	if (arg & ~((JIMM_MASK) << 2))
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return (arg >> 2) & JIMM_MASK;
}

static __init u32 build_func(u32 arg)
{
	if (arg & ~FUNC_MASK)
		printk(KERN_WARNING "TLB synthesizer field overflow\n");

	return arg & FUNC_MASK;
}

/*
 * The order of opcode arguments is implicitly left to right,
 * starting with RS and ending with FUNC or IMM.
 */
static void __init build_insn(u32 **buf, enum opcode opc, ...)
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

	if (!ip)
		panic("Unsupported TLB synthesizer instruction %d", opc);

	op = ip->match;
	va_start(ap, opc);
	if (ip->fields & RS) op |= build_rs(va_arg(ap, u32));
	if (ip->fields & RT) op |= build_rt(va_arg(ap, u32));
	if (ip->fields & RD) op |= build_rd(va_arg(ap, u32));
	if (ip->fields & RE) op |= build_re(va_arg(ap, u32));
	if (ip->fields & SIMM) op |= build_simm(va_arg(ap, s32));
	if (ip->fields & UIMM) op |= build_uimm(va_arg(ap, u32));
	if (ip->fields & BIMM) op |= build_bimm(va_arg(ap, s32));
	if (ip->fields & JIMM) op |= build_jimm(va_arg(ap, u32));
	if (ip->fields & FUNC) op |= build_func(va_arg(ap, u32));
	va_end(ap);

	**buf = op;
	(*buf)++;
}

#define I_u1u2u3(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b, unsigned int c)			\
	{							\
		build_insn(buf, insn##op, a, b, c);		\
	}

#define I_u2u1u3(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b, unsigned int c)			\
	{							\
		build_insn(buf, insn##op, b, a, c);		\
	}

#define I_u3u1u2(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b, unsigned int c)			\
	{							\
		build_insn(buf, insn##op, b, c, a);		\
	}

#define I_u1u2s3(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b, signed int c)			\
	{							\
		build_insn(buf, insn##op, a, b, c);		\
	}

#define I_u2s3u1(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	signed int b, unsigned int c)			\
	{							\
		build_insn(buf, insn##op, c, a, b);		\
	}

#define I_u2u1s3(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b, signed int c)			\
	{							\
		build_insn(buf, insn##op, b, a, c);		\
	}

#define I_u1u2(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	unsigned int b)					\
	{							\
		build_insn(buf, insn##op, a, b);		\
	}

#define I_u1s2(op)						\
	static inline void i##op(u32 **buf, unsigned int a,	\
	 	signed int b)					\
	{							\
		build_insn(buf, insn##op, a, b);		\
	}

#define I_u1(op)						\
	static inline void i##op(u32 **buf, unsigned int a)	\
	{							\
		build_insn(buf, insn##op, a);			\
	}

#define I_0(op)							\
	static inline void i##op(u32 **buf)			\
	{							\
		build_insn(buf, insn##op);			\
	}

I_u2u1s3(_addiu);
I_u3u1u2(_addu);
I_u2u1u3(_andi);
I_u3u1u2(_and);
I_u1u2s3(_beq);
I_u1u2s3(_beql);
I_u1s2(_bgez);
I_u1s2(_bgezl);
I_u1s2(_bltz);
I_u1s2(_bltzl);
I_u1u2s3(_bne);
I_u1u2(_dmfc0);
I_u1u2(_dmtc0);
I_u2u1s3(_daddiu);
I_u3u1u2(_daddu);
I_u2u1u3(_dsll);
I_u2u1u3(_dsll32);
I_u2u1u3(_dsra);
I_u2u1u3(_dsrl);
I_u3u1u2(_dsubu);
I_0(_eret);
I_u1(_j);
I_u1(_jal);
I_u1(_jr);
I_u2s3u1(_ld);
I_u2s3u1(_ll);
I_u2s3u1(_lld);
I_u1s2(_lui);
I_u2s3u1(_lw);
I_u1u2(_mfc0);
I_u1u2(_mtc0);
I_u2u1u3(_ori);
I_0(_rfe);
I_u2s3u1(_sc);
I_u2s3u1(_scd);
I_u2s3u1(_sd);
I_u2u1u3(_sll);
I_u2u1u3(_sra);
I_u2u1u3(_srl);
I_u3u1u2(_subu);
I_u2s3u1(_sw);
I_0(_tlbp);
I_0(_tlbwi);
I_0(_tlbwr);
I_u3u1u2(_xor)
I_u2u1u3(_xori);

/*
 * handling labels
 */

enum label_id {
	label_invalid,
	label_second_part,
	label_leave,
	label_vmalloc,
	label_vmalloc_done,
	label_tlbw_hazard,
	label_split,
	label_nopage_tlbl,
	label_nopage_tlbs,
	label_nopage_tlbm,
	label_smp_pgtable_change,
	label_r3000_write_probe_fail,
};

struct label {
	u32 *addr;
	enum label_id lab;
};

static __init void build_label(struct label **lab, u32 *addr,
			       enum label_id l)
{
	(*lab)->addr = addr;
	(*lab)->lab = l;
	(*lab)++;
}

#define L_LA(lb)						\
	static inline void l##lb(struct label **lab, u32 *addr) \
	{							\
		build_label(lab, addr, label##lb);		\
	}

L_LA(_second_part)
L_LA(_leave)
L_LA(_vmalloc)
L_LA(_vmalloc_done)
L_LA(_tlbw_hazard)
L_LA(_split)
L_LA(_nopage_tlbl)
L_LA(_nopage_tlbs)
L_LA(_nopage_tlbm)
L_LA(_smp_pgtable_change)
L_LA(_r3000_write_probe_fail)

/* convenience macros for instructions */
#ifdef CONFIG_64BIT
# define i_LW(buf, rs, rt, off) i_ld(buf, rs, rt, off)
# define i_SW(buf, rs, rt, off) i_sd(buf, rs, rt, off)
# define i_SLL(buf, rs, rt, sh) i_dsll(buf, rs, rt, sh)
# define i_SRA(buf, rs, rt, sh) i_dsra(buf, rs, rt, sh)
# define i_SRL(buf, rs, rt, sh) i_dsrl(buf, rs, rt, sh)
# define i_MFC0(buf, rt, rd) i_dmfc0(buf, rt, rd)
# define i_MTC0(buf, rt, rd) i_dmtc0(buf, rt, rd)
# define i_ADDIU(buf, rs, rt, val) i_daddiu(buf, rs, rt, val)
# define i_ADDU(buf, rs, rt, rd) i_daddu(buf, rs, rt, rd)
# define i_SUBU(buf, rs, rt, rd) i_dsubu(buf, rs, rt, rd)
# define i_LL(buf, rs, rt, off) i_lld(buf, rs, rt, off)
# define i_SC(buf, rs, rt, off) i_scd(buf, rs, rt, off)
#else
# define i_LW(buf, rs, rt, off) i_lw(buf, rs, rt, off)
# define i_SW(buf, rs, rt, off) i_sw(buf, rs, rt, off)
# define i_SLL(buf, rs, rt, sh) i_sll(buf, rs, rt, sh)
# define i_SRA(buf, rs, rt, sh) i_sra(buf, rs, rt, sh)
# define i_SRL(buf, rs, rt, sh) i_srl(buf, rs, rt, sh)
# define i_MFC0(buf, rt, rd) i_mfc0(buf, rt, rd)
# define i_MTC0(buf, rt, rd) i_mtc0(buf, rt, rd)
# define i_ADDIU(buf, rs, rt, val) i_addiu(buf, rs, rt, val)
# define i_ADDU(buf, rs, rt, rd) i_addu(buf, rs, rt, rd)
# define i_SUBU(buf, rs, rt, rd) i_subu(buf, rs, rt, rd)
# define i_LL(buf, rs, rt, off) i_ll(buf, rs, rt, off)
# define i_SC(buf, rs, rt, off) i_sc(buf, rs, rt, off)
#endif

#define i_b(buf, off) i_beq(buf, 0, 0, off)
#define i_beqz(buf, rs, off) i_beq(buf, rs, 0, off)
#define i_beqzl(buf, rs, off) i_beql(buf, rs, 0, off)
#define i_bnez(buf, rs, off) i_bne(buf, rs, 0, off)
#define i_bnezl(buf, rs, off) i_bnel(buf, rs, 0, off)
#define i_move(buf, a, b) i_ADDU(buf, a, 0, b)
#define i_nop(buf) i_sll(buf, 0, 0, 0)
#define i_ssnop(buf) i_sll(buf, 0, 0, 1)
#define i_ehb(buf) i_sll(buf, 0, 0, 3)

#ifdef CONFIG_64BIT
static __init int __attribute__((unused)) in_compat_space_p(long addr)
{
	/* Is this address in 32bit compat space? */
	return (((addr) & 0xffffffff00000000L) == 0xffffffff00000000L);
}

static __init int __attribute__((unused)) rel_highest(long val)
{
	return ((((val + 0x800080008000L) >> 48) & 0xffff) ^ 0x8000) - 0x8000;
}

static __init int __attribute__((unused)) rel_higher(long val)
{
	return ((((val + 0x80008000L) >> 32) & 0xffff) ^ 0x8000) - 0x8000;
}
#endif

static __init int rel_hi(long val)
{
	return ((((val + 0x8000L) >> 16) & 0xffff) ^ 0x8000) - 0x8000;
}

static __init int rel_lo(long val)
{
	return ((val & 0xffff) ^ 0x8000) - 0x8000;
}

static __init void i_LA_mostly(u32 **buf, unsigned int rs, long addr)
{
#ifdef CONFIG_64BIT
	if (!in_compat_space_p(addr)) {
		i_lui(buf, rs, rel_highest(addr));
		if (rel_higher(addr))
			i_daddiu(buf, rs, rs, rel_higher(addr));
		if (rel_hi(addr)) {
			i_dsll(buf, rs, rs, 16);
			i_daddiu(buf, rs, rs, rel_hi(addr));
			i_dsll(buf, rs, rs, 16);
		} else
			i_dsll32(buf, rs, rs, 0);
	} else
#endif
		i_lui(buf, rs, rel_hi(addr));
}

static __init void __attribute__((unused)) i_LA(u32 **buf, unsigned int rs,
						long addr)
{
	i_LA_mostly(buf, rs, addr);
	if (rel_lo(addr))
		i_ADDIU(buf, rs, rs, rel_lo(addr));
}

/*
 * handle relocations
 */

struct reloc {
	u32 *addr;
	unsigned int type;
	enum label_id lab;
};

static __init void r_mips_pc16(struct reloc **rel, u32 *addr,
			       enum label_id l)
{
	(*rel)->addr = addr;
	(*rel)->type = R_MIPS_PC16;
	(*rel)->lab = l;
	(*rel)++;
}

static inline void __resolve_relocs(struct reloc *rel, struct label *lab)
{
	long laddr = (long)lab->addr;
	long raddr = (long)rel->addr;

	switch (rel->type) {
	case R_MIPS_PC16:
		*rel->addr |= build_bimm(laddr - (raddr + 4));
		break;

	default:
		panic("Unsupported TLB synthesizer relocation %d",
		      rel->type);
	}
}

static __init void resolve_relocs(struct reloc *rel, struct label *lab)
{
	struct label *l;

	for (; rel->lab != label_invalid; rel++)
		for (l = lab; l->lab != label_invalid; l++)
			if (rel->lab == l->lab)
				__resolve_relocs(rel, l);
}

static __init void move_relocs(struct reloc *rel, u32 *first, u32 *end,
			       long off)
{
	for (; rel->lab != label_invalid; rel++)
		if (rel->addr >= first && rel->addr < end)
			rel->addr += off;
}

static __init void move_labels(struct label *lab, u32 *first, u32 *end,
			       long off)
{
	for (; lab->lab != label_invalid; lab++)
		if (lab->addr >= first && lab->addr < end)
			lab->addr += off;
}

static __init void copy_handler(struct reloc *rel, struct label *lab,
				u32 *first, u32 *end, u32 *target)
{
	long off = (long)(target - first);

	memcpy(target, first, (end - first) * sizeof(u32));

	move_relocs(rel, first, end, off);
	move_labels(lab, first, end, off);
}

static __init int __attribute__((unused)) insn_has_bdelay(struct reloc *rel,
							  u32 *addr)
{
	for (; rel->lab != label_invalid; rel++) {
		if (rel->addr == addr
		    && (rel->type == R_MIPS_PC16
			|| rel->type == R_MIPS_26))
			return 1;
	}

	return 0;
}

/* convenience functions for labeled branches */
static void __attribute__((unused)) il_bltz(u32 **p, struct reloc **r,
					    unsigned int reg, enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_bltz(p, reg, 0);
}

static void __attribute__((unused)) il_b(u32 **p, struct reloc **r,
					 enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_b(p, 0);
}

static void il_beqz(u32 **p, struct reloc **r, unsigned int reg,
		    enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_beqz(p, reg, 0);
}

static void __attribute__((unused))
il_beqzl(u32 **p, struct reloc **r, unsigned int reg, enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_beqzl(p, reg, 0);
}

static void il_bnez(u32 **p, struct reloc **r, unsigned int reg,
		    enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_bnez(p, reg, 0);
}

static void il_bgezl(u32 **p, struct reloc **r, unsigned int reg,
		     enum label_id l)
{
	r_mips_pc16(r, *p, l);
	i_bgezl(p, reg, 0);
}

/* The only general purpose registers allowed in TLB handlers. */
#define K0		26
#define K1		27

/* Some CP0 registers */
#define C0_INDEX	0
#define C0_ENTRYLO0	2
#define C0_ENTRYLO1	3
#define C0_CONTEXT	4
#define C0_BADVADDR	8
#define C0_ENTRYHI	10
#define C0_EPC		14
#define C0_XCONTEXT	20

#ifdef CONFIG_64BIT
# define GET_CONTEXT(buf, reg) i_MFC0(buf, reg, C0_XCONTEXT)
#else
# define GET_CONTEXT(buf, reg) i_MFC0(buf, reg, C0_CONTEXT)
#endif

/* The worst case length of the handler is around 18 instructions for
 * R3000-style TLBs and up to 63 instructions for R4000-style TLBs.
 * Maximum space available is 32 instructions for R3000 and 64
 * instructions for R4000.
 *
 * We deliberately chose a buffer size of 128, so we won't scribble
 * over anything important on overflow before we panic.
 */
static __initdata u32 tlb_handler[128];

/* simply assume worst case size for labels and relocs */
static __initdata struct label labels[128];
static __initdata struct reloc relocs[128];

/*
 * The R3000 TLB handler is simple.
 */
static void __init build_r3000_tlb_refill_handler(void)
{
	long pgdc = (long)pgd_current;
	u32 *p;

	memset(tlb_handler, 0, sizeof(tlb_handler));
	p = tlb_handler;

	i_mfc0(&p, K0, C0_BADVADDR);
	i_lui(&p, K1, rel_hi(pgdc)); /* cp0 delay */
	i_lw(&p, K1, rel_lo(pgdc), K1);
	i_srl(&p, K0, K0, 22); /* load delay */
	i_sll(&p, K0, K0, 2);
	i_addu(&p, K1, K1, K0);
	i_mfc0(&p, K0, C0_CONTEXT);
	i_lw(&p, K1, 0, K1); /* cp0 delay */
	i_andi(&p, K0, K0, 0xffc); /* load delay */
	i_addu(&p, K1, K1, K0);
	i_lw(&p, K0, 0, K1);
	i_nop(&p); /* load delay */
	i_mtc0(&p, K0, C0_ENTRYLO0);
	i_mfc0(&p, K1, C0_EPC); /* cp0 delay */
	i_tlbwr(&p); /* cp0 delay */
	i_jr(&p, K1);
	i_rfe(&p); /* branch delay */

	if (p > tlb_handler + 32)
		panic("TLB refill handler space exceeded");

	printk("Synthesized TLB refill handler (%u instructions).\n",
	       (unsigned int)(p - tlb_handler));
#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - tlb_handler); i++)
			printk("%08x\n", tlb_handler[i]);
	}
#endif

	memcpy((void *)CAC_BASE, tlb_handler, 0x80);
}

/*
 * The R4000 TLB handler is much more complicated. We have two
 * consecutive handler areas with 32 instructions space each.
 * Since they aren't used at the same time, we can overflow in the
 * other one.To keep things simple, we first assume linear space,
 * then we relocate it to the final handler layout as needed.
 */
static __initdata u32 final_handler[64];

/*
 * Hazards
 *
 * From the IDT errata for the QED RM5230 (Nevada), processor revision 1.0:
 * 2. A timing hazard exists for the TLBP instruction.
 *
 *      stalling_instruction
 *      TLBP
 *
 * The JTLB is being read for the TLBP throughout the stall generated by the
 * previous instruction. This is not really correct as the stalling instruction
 * can modify the address used to access the JTLB.  The failure symptom is that
 * the TLBP instruction will use an address created for the stalling instruction
 * and not the address held in C0_ENHI and thus report the wrong results.
 *
 * The software work-around is to not allow the instruction preceding the TLBP
 * to stall - make it an NOP or some other instruction guaranteed not to stall.
 *
 * Errata 2 will not be fixed.  This errata is also on the R5000.
 *
 * As if we MIPS hackers wouldn't know how to nop pipelines happy ...
 */
static __init void __attribute__((unused)) build_tlb_probe_entry(u32 **p)
{
	switch (current_cpu_data.cputype) {
	/* Found by experiment: R4600 v2.0 needs this, too.  */
	case CPU_R4600:
	case CPU_R5000:
	case CPU_R5000A:
	case CPU_NEVADA:
		i_nop(p);
		i_tlbp(p);
		break;

	default:
		i_tlbp(p);
		break;
	}
}

/*
 * Write random or indexed TLB entry, and care about the hazards from
 * the preceeding mtc0 and for the following eret.
 */
enum tlb_write_entry { tlb_random, tlb_indexed };

static __init void build_tlb_write_entry(u32 **p, struct label **l,
					 struct reloc **r,
					 enum tlb_write_entry wmode)
{
	void(*tlbw)(u32 **) = NULL;

	switch (wmode) {
	case tlb_random: tlbw = i_tlbwr; break;
	case tlb_indexed: tlbw = i_tlbwi; break;
	}

	switch (current_cpu_data.cputype) {
	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		/*
		 * This branch uses up a mtc0 hazard nop slot and saves
		 * two nops after the tlbw instruction.
		 */
		il_bgezl(p, r, 0, label_tlbw_hazard);
		tlbw(p);
		l_tlbw_hazard(l, *p);
		i_nop(p);
		break;

	case CPU_R4600:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_R5000A:
		i_nop(p);
		tlbw(p);
		i_nop(p);
		break;

	case CPU_R4300:
	case CPU_5KC:
	case CPU_TX49XX:
	case CPU_AU1000:
	case CPU_AU1100:
	case CPU_AU1500:
	case CPU_AU1550:
	case CPU_AU1200:
	case CPU_PR4450:
		i_nop(p);
		tlbw(p);
		break;

	case CPU_R10000:
	case CPU_R12000:
	case CPU_4KC:
	case CPU_SB1:
	case CPU_SB1A:
	case CPU_4KSC:
	case CPU_20KC:
	case CPU_25KF:
		tlbw(p);
		break;

	case CPU_NEVADA:
		i_nop(p); /* QED specifies 2 nops hazard */
		/*
		 * This branch uses up a mtc0 hazard nop slot and saves
		 * a nop after the tlbw instruction.
		 */
		il_bgezl(p, r, 0, label_tlbw_hazard);
		tlbw(p);
		l_tlbw_hazard(l, *p);
		break;

	case CPU_RM7000:
		i_nop(p);
		i_nop(p);
		i_nop(p);
		i_nop(p);
		tlbw(p);
		break;

	case CPU_4KEC:
	case CPU_24K:
	case CPU_34K:
		i_ehb(p);
		tlbw(p);
		break;

	case CPU_RM9000:
		/*
		 * When the JTLB is updated by tlbwi or tlbwr, a subsequent
		 * use of the JTLB for instructions should not occur for 4
		 * cpu cycles and use for data translations should not occur
		 * for 3 cpu cycles.
		 */
		i_ssnop(p);
		i_ssnop(p);
		i_ssnop(p);
		i_ssnop(p);
		tlbw(p);
		i_ssnop(p);
		i_ssnop(p);
		i_ssnop(p);
		i_ssnop(p);
		break;

	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4181:
	case CPU_VR4181A:
		i_nop(p);
		i_nop(p);
		tlbw(p);
		i_nop(p);
		i_nop(p);
		break;

	case CPU_VR4131:
	case CPU_VR4133:
	case CPU_R5432:
		i_nop(p);
		i_nop(p);
		tlbw(p);
		break;

	default:
		panic("No TLB refill handler yet (CPU type: %d)",
		      current_cpu_data.cputype);
		break;
	}
}

#ifdef CONFIG_64BIT
/*
 * TMP and PTR are scratch.
 * TMP will be clobbered, PTR will hold the pmd entry.
 */
static __init void
build_get_pmde64(u32 **p, struct label **l, struct reloc **r,
		 unsigned int tmp, unsigned int ptr)
{
	long pgdc = (long)pgd_current;

	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	i_dmfc0(p, tmp, C0_BADVADDR);
	il_bltz(p, r, tmp, label_vmalloc);
	/* No i_nop needed here, since the next insn doesn't touch TMP. */

#ifdef CONFIG_SMP
# ifdef CONFIG_BUILD_ELF64
	/*
	 * 64 bit SMP running in XKPHYS has smp_processor_id() << 3
	 * stored in CONTEXT.
	 */
	i_dmfc0(p, ptr, C0_CONTEXT);
	i_dsrl(p, ptr, ptr, 23);
	i_LA_mostly(p, tmp, pgdc);
	i_daddu(p, ptr, ptr, tmp);
	i_dmfc0(p, tmp, C0_BADVADDR);
	i_ld(p, ptr, rel_lo(pgdc), ptr);
# else
	/*
	 * 64 bit SMP running in compat space has the lower part of
	 * &pgd_current[smp_processor_id()] stored in CONTEXT.
	 */
	if (!in_compat_space_p(pgdc))
		panic("Invalid page directory address!");

	i_dmfc0(p, ptr, C0_CONTEXT);
	i_dsra(p, ptr, ptr, 23);
	i_ld(p, ptr, 0, ptr);
# endif
#else
	i_LA_mostly(p, ptr, pgdc);
	i_ld(p, ptr, rel_lo(pgdc), ptr);
#endif

	l_vmalloc_done(l, *p);
	i_dsrl(p, tmp, tmp, PGDIR_SHIFT-3); /* get pgd offset in bytes */
	i_andi(p, tmp, tmp, (PTRS_PER_PGD - 1)<<3);
	i_daddu(p, ptr, ptr, tmp); /* add in pgd offset */
	i_dmfc0(p, tmp, C0_BADVADDR); /* get faulting address */
	i_ld(p, ptr, 0, ptr); /* get pmd pointer */
	i_dsrl(p, tmp, tmp, PMD_SHIFT-3); /* get pmd offset in bytes */
	i_andi(p, tmp, tmp, (PTRS_PER_PMD - 1)<<3);
	i_daddu(p, ptr, ptr, tmp); /* add in pmd offset */
}

/*
 * BVADDR is the faulting address, PTR is scratch.
 * PTR will hold the pgd for vmalloc.
 */
static __init void
build_get_pgd_vmalloc64(u32 **p, struct label **l, struct reloc **r,
			unsigned int bvaddr, unsigned int ptr)
{
	long swpd = (long)swapper_pg_dir;

	l_vmalloc(l, *p);
	i_LA(p, ptr, VMALLOC_START);
	i_dsubu(p, bvaddr, bvaddr, ptr);

	if (in_compat_space_p(swpd) && !rel_lo(swpd)) {
		il_b(p, r, label_vmalloc_done);
		i_lui(p, ptr, rel_hi(swpd));
	} else {
		i_LA_mostly(p, ptr, swpd);
		il_b(p, r, label_vmalloc_done);
		i_daddiu(p, ptr, ptr, rel_lo(swpd));
	}
}

#else /* !CONFIG_64BIT */

/*
 * TMP and PTR are scratch.
 * TMP will be clobbered, PTR will hold the pgd entry.
 */
static __init void __attribute__((unused))
build_get_pgde32(u32 **p, unsigned int tmp, unsigned int ptr)
{
	long pgdc = (long)pgd_current;

	/* 32 bit SMP has smp_processor_id() stored in CONTEXT. */
#ifdef CONFIG_SMP
	i_mfc0(p, ptr, C0_CONTEXT);
	i_LA_mostly(p, tmp, pgdc);
	i_srl(p, ptr, ptr, 23);
	i_addu(p, ptr, tmp, ptr);
#else
	i_LA_mostly(p, ptr, pgdc);
#endif
	i_mfc0(p, tmp, C0_BADVADDR); /* get faulting address */
	i_lw(p, ptr, rel_lo(pgdc), ptr);
	i_srl(p, tmp, tmp, PGDIR_SHIFT); /* get pgd only bits */
	i_sll(p, tmp, tmp, PGD_T_LOG2);
	i_addu(p, ptr, ptr, tmp); /* add in pgd offset */
}

#endif /* !CONFIG_64BIT */

static __init void build_adjust_context(u32 **p, unsigned int ctx)
{
	unsigned int shift = 4 - (PTE_T_LOG2 + 1);
	unsigned int mask = (PTRS_PER_PTE / 2 - 1) << (PTE_T_LOG2 + 1);

	switch (current_cpu_data.cputype) {
	case CPU_VR41XX:
	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4181:
	case CPU_VR4181A:
	case CPU_VR4133:
		shift += 2;
		break;

	default:
		break;
	}

	if (shift)
		i_SRL(p, ctx, ctx, shift);
	i_andi(p, ctx, ctx, mask);
}

static __init void build_get_ptep(u32 **p, unsigned int tmp, unsigned int ptr)
{
	/*
	 * Bug workaround for the Nevada. It seems as if under certain
	 * circumstances the move from cp0_context might produce a
	 * bogus result when the mfc0 instruction and its consumer are
	 * in a different cacheline or a load instruction, probably any
	 * memory reference, is between them.
	 */
	switch (current_cpu_data.cputype) {
	case CPU_NEVADA:
		i_LW(p, ptr, 0, ptr);
		GET_CONTEXT(p, tmp); /* get context reg */
		break;

	default:
		GET_CONTEXT(p, tmp); /* get context reg */
		i_LW(p, ptr, 0, ptr);
		break;
	}

	build_adjust_context(p, tmp);
	i_ADDU(p, ptr, ptr, tmp); /* add in offset */
}

static __init void build_update_entries(u32 **p, unsigned int tmp,
					unsigned int ptep)
{
	/*
	 * 64bit address support (36bit on a 32bit CPU) in a 32bit
	 * Kernel is a special case. Only a few CPUs use it.
	 */
#ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits) {
		i_ld(p, tmp, 0, ptep); /* get even pte */
		i_ld(p, ptep, sizeof(pte_t), ptep); /* get odd pte */
		i_dsrl(p, tmp, tmp, 6); /* convert to entrylo0 */
		i_mtc0(p, tmp, C0_ENTRYLO0); /* load it */
		i_dsrl(p, ptep, ptep, 6); /* convert to entrylo1 */
		i_mtc0(p, ptep, C0_ENTRYLO1); /* load it */
	} else {
		int pte_off_even = sizeof(pte_t) / 2;
		int pte_off_odd = pte_off_even + sizeof(pte_t);

		/* The pte entries are pre-shifted */
		i_lw(p, tmp, pte_off_even, ptep); /* get even pte */
		i_mtc0(p, tmp, C0_ENTRYLO0); /* load it */
		i_lw(p, ptep, pte_off_odd, ptep); /* get odd pte */
		i_mtc0(p, ptep, C0_ENTRYLO1); /* load it */
	}
#else
	i_LW(p, tmp, 0, ptep); /* get even pte */
	i_LW(p, ptep, sizeof(pte_t), ptep); /* get odd pte */
	if (r45k_bvahwbug())
		build_tlb_probe_entry(p);
	i_SRL(p, tmp, tmp, 6); /* convert to entrylo0 */
	if (r4k_250MHZhwbug())
		i_mtc0(p, 0, C0_ENTRYLO0);
	i_mtc0(p, tmp, C0_ENTRYLO0); /* load it */
	i_SRL(p, ptep, ptep, 6); /* convert to entrylo1 */
	if (r45k_bvahwbug())
		i_mfc0(p, tmp, C0_INDEX);
	if (r4k_250MHZhwbug())
		i_mtc0(p, 0, C0_ENTRYLO1);
	i_mtc0(p, ptep, C0_ENTRYLO1); /* load it */
#endif
}

static void __init build_r4000_tlb_refill_handler(void)
{
	u32 *p = tlb_handler;
	struct label *l = labels;
	struct reloc *r = relocs;
	u32 *f;
	unsigned int final_len;

	memset(tlb_handler, 0, sizeof(tlb_handler));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));
	memset(final_handler, 0, sizeof(final_handler));

	/*
	 * create the plain linear handler
	 */
	if (bcm1250_m3_war()) {
		i_MFC0(&p, K0, C0_BADVADDR);
		i_MFC0(&p, K1, C0_ENTRYHI);
		i_xor(&p, K0, K0, K1);
		i_SRL(&p, K0, K0, PAGE_SHIFT + 1);
		il_bnez(&p, &r, K0, label_leave);
		/* No need for i_nop */
	}

#ifdef CONFIG_64BIT
	build_get_pmde64(&p, &l, &r, K0, K1); /* get pmd in K1 */
#else
	build_get_pgde32(&p, K0, K1); /* get pgd in K1 */
#endif

	build_get_ptep(&p, K0, K1);
	build_update_entries(&p, K0, K1);
	build_tlb_write_entry(&p, &l, &r, tlb_random);
	l_leave(&l, p);
	i_eret(&p); /* return from trap */

#ifdef CONFIG_64BIT
	build_get_pgd_vmalloc64(&p, &l, &r, K0, K1);
#endif

	/*
	 * Overflow check: For the 64bit handler, we need at least one
	 * free instruction slot for the wrap-around branch. In worst
	 * case, if the intended insertion point is a delay slot, we
	 * need three, with the the second nop'ed and the third being
	 * unused.
	 */
#ifdef CONFIG_32BIT
	if ((p - tlb_handler) > 64)
		panic("TLB refill handler space exceeded");
#else
	if (((p - tlb_handler) > 63)
	    || (((p - tlb_handler) > 61)
		&& insn_has_bdelay(relocs, tlb_handler + 29)))
		panic("TLB refill handler space exceeded");
#endif

	/*
	 * Now fold the handler in the TLB refill handler space.
	 */
#ifdef CONFIG_32BIT
	f = final_handler;
	/* Simplest case, just copy the handler. */
	copy_handler(relocs, labels, tlb_handler, p, f);
	final_len = p - tlb_handler;
#else /* CONFIG_64BIT */
	f = final_handler + 32;
	if ((p - tlb_handler) <= 32) {
		/* Just copy the handler. */
		copy_handler(relocs, labels, tlb_handler, p, f);
		final_len = p - tlb_handler;
	} else {
		u32 *split = tlb_handler + 30;

		/*
		 * Find the split point.
		 */
		if (insn_has_bdelay(relocs, split - 1))
			split--;

		/* Copy first part of the handler. */
		copy_handler(relocs, labels, tlb_handler, split, f);
		f += split - tlb_handler;

		/* Insert branch. */
		l_split(&l, final_handler);
		il_b(&f, &r, label_split);
		if (insn_has_bdelay(relocs, split))
			i_nop(&f);
		else {
			copy_handler(relocs, labels, split, split + 1, f);
			move_labels(labels, f, f + 1, -1);
			f++;
			split++;
		}

		/* Copy the rest of the handler. */
		copy_handler(relocs, labels, split, p, final_handler);
		final_len = (f - (final_handler + 32)) + (p - split);
	}
#endif /* CONFIG_64BIT */

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB refill handler (%u instructions).\n",
	       final_len);

#ifdef DEBUG_TLB
	{
		int i;

		f = final_handler;
#ifdef CONFIG_64BIT
		if (final_len > 32)
			final_len = 64;
		else
			f = final_handler + 32;
#endif /* CONFIG_64BIT */
		for (i = 0; i < final_len; i++)
			printk("%08x\n", f[i]);
	}
#endif

	memcpy((void *)CAC_BASE, final_handler, 0x100);
}

/*
 * TLB load/store/modify handlers.
 *
 * Only the fastpath gets synthesized at runtime, the slowpath for
 * do_page_fault remains normal asm.
 */
extern void tlb_do_page_fault_0(void);
extern void tlb_do_page_fault_1(void);

#define __tlb_handler_align \
	__attribute__((__aligned__(1 << CONFIG_MIPS_L1_CACHE_SHIFT)))

/*
 * 128 instructions for the fastpath handler is generous and should
 * never be exceeded.
 */
#define FASTPATH_SIZE 128

u32 __tlb_handler_align handle_tlbl[FASTPATH_SIZE];
u32 __tlb_handler_align handle_tlbs[FASTPATH_SIZE];
u32 __tlb_handler_align handle_tlbm[FASTPATH_SIZE];

static void __init
iPTE_LW(u32 **p, struct label **l, unsigned int pte, unsigned int ptr)
{
#ifdef CONFIG_SMP
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		i_lld(p, pte, 0, ptr);
	else
# endif
		i_LL(p, pte, 0, ptr);
#else
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		i_ld(p, pte, 0, ptr);
	else
# endif
		i_LW(p, pte, 0, ptr);
#endif
}

static void __init
iPTE_SW(u32 **p, struct reloc **r, unsigned int pte, unsigned int ptr,
	unsigned int mode)
{
#ifdef CONFIG_64BIT_PHYS_ADDR
	unsigned int hwmode = mode & (_PAGE_VALID | _PAGE_DIRTY);
#endif

	i_ori(p, pte, pte, mode);
#ifdef CONFIG_SMP
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		i_scd(p, pte, 0, ptr);
	else
# endif
		i_SC(p, pte, 0, ptr);

	if (r10000_llsc_war())
		il_beqzl(p, r, pte, label_smp_pgtable_change);
	else
		il_beqz(p, r, pte, label_smp_pgtable_change);

# ifdef CONFIG_64BIT_PHYS_ADDR
	if (!cpu_has_64bits) {
		/* no i_nop needed */
		i_ll(p, pte, sizeof(pte_t) / 2, ptr);
		i_ori(p, pte, pte, hwmode);
		i_sc(p, pte, sizeof(pte_t) / 2, ptr);
		il_beqz(p, r, pte, label_smp_pgtable_change);
		/* no i_nop needed */
		i_lw(p, pte, 0, ptr);
	} else
		i_nop(p);
# else
	i_nop(p);
# endif
#else
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		i_sd(p, pte, 0, ptr);
	else
# endif
		i_SW(p, pte, 0, ptr);

# ifdef CONFIG_64BIT_PHYS_ADDR
	if (!cpu_has_64bits) {
		i_lw(p, pte, sizeof(pte_t) / 2, ptr);
		i_ori(p, pte, pte, hwmode);
		i_sw(p, pte, sizeof(pte_t) / 2, ptr);
		i_lw(p, pte, 0, ptr);
	}
# endif
#endif
}

/*
 * Check if PTE is present, if not then jump to LABEL. PTR points to
 * the page table where this PTE is located, PTE will be re-loaded
 * with it's original value.
 */
static void __init
build_pte_present(u32 **p, struct label **l, struct reloc **r,
		  unsigned int pte, unsigned int ptr, enum label_id lid)
{
	i_andi(p, pte, pte, _PAGE_PRESENT | _PAGE_READ);
	i_xori(p, pte, pte, _PAGE_PRESENT | _PAGE_READ);
	il_bnez(p, r, pte, lid);
	iPTE_LW(p, l, pte, ptr);
}

/* Make PTE valid, store result in PTR. */
static void __init
build_make_valid(u32 **p, struct reloc **r, unsigned int pte,
		 unsigned int ptr)
{
	unsigned int mode = _PAGE_VALID | _PAGE_ACCESSED;

	iPTE_SW(p, r, pte, ptr, mode);
}

/*
 * Check if PTE can be written to, if not branch to LABEL. Regardless
 * restore PTE with value from PTR when done.
 */
static void __init
build_pte_writable(u32 **p, struct label **l, struct reloc **r,
		   unsigned int pte, unsigned int ptr, enum label_id lid)
{
	i_andi(p, pte, pte, _PAGE_PRESENT | _PAGE_WRITE);
	i_xori(p, pte, pte, _PAGE_PRESENT | _PAGE_WRITE);
	il_bnez(p, r, pte, lid);
	iPTE_LW(p, l, pte, ptr);
}

/* Make PTE writable, update software status bits as well, then store
 * at PTR.
 */
static void __init
build_make_write(u32 **p, struct reloc **r, unsigned int pte,
		 unsigned int ptr)
{
	unsigned int mode = (_PAGE_ACCESSED | _PAGE_MODIFIED | _PAGE_VALID
			     | _PAGE_DIRTY);

	iPTE_SW(p, r, pte, ptr, mode);
}

/*
 * Check if PTE can be modified, if not branch to LABEL. Regardless
 * restore PTE with value from PTR when done.
 */
static void __init
build_pte_modifiable(u32 **p, struct label **l, struct reloc **r,
		     unsigned int pte, unsigned int ptr, enum label_id lid)
{
	i_andi(p, pte, pte, _PAGE_WRITE);
	il_beqz(p, r, pte, lid);
	iPTE_LW(p, l, pte, ptr);
}

/*
 * R3000 style TLB load/store/modify handlers.
 */

/*
 * This places the pte into ENTRYLO0 and writes it with tlbwi.
 * Then it returns.
 */
static void __init
build_r3000_pte_reload_tlbwi(u32 **p, unsigned int pte, unsigned int tmp)
{
	i_mtc0(p, pte, C0_ENTRYLO0); /* cp0 delay */
	i_mfc0(p, tmp, C0_EPC); /* cp0 delay */
	i_tlbwi(p);
	i_jr(p, tmp);
	i_rfe(p); /* branch delay */
}

/*
 * This places the pte into ENTRYLO0 and writes it with tlbwi
 * or tlbwr as appropriate.  This is because the index register
 * may have the probe fail bit set as a result of a trap on a
 * kseg2 access, i.e. without refill.  Then it returns.
 */
static void __init
build_r3000_tlb_reload_write(u32 **p, struct label **l, struct reloc **r,
			     unsigned int pte, unsigned int tmp)
{
	i_mfc0(p, tmp, C0_INDEX);
	i_mtc0(p, pte, C0_ENTRYLO0); /* cp0 delay */
	il_bltz(p, r, tmp, label_r3000_write_probe_fail); /* cp0 delay */
	i_mfc0(p, tmp, C0_EPC); /* branch delay */
	i_tlbwi(p); /* cp0 delay */
	i_jr(p, tmp);
	i_rfe(p); /* branch delay */
	l_r3000_write_probe_fail(l, *p);
	i_tlbwr(p); /* cp0 delay */
	i_jr(p, tmp);
	i_rfe(p); /* branch delay */
}

static void __init
build_r3000_tlbchange_handler_head(u32 **p, unsigned int pte,
				   unsigned int ptr)
{
	long pgdc = (long)pgd_current;

	i_mfc0(p, pte, C0_BADVADDR);
	i_lui(p, ptr, rel_hi(pgdc)); /* cp0 delay */
	i_lw(p, ptr, rel_lo(pgdc), ptr);
	i_srl(p, pte, pte, 22); /* load delay */
	i_sll(p, pte, pte, 2);
	i_addu(p, ptr, ptr, pte);
	i_mfc0(p, pte, C0_CONTEXT);
	i_lw(p, ptr, 0, ptr); /* cp0 delay */
	i_andi(p, pte, pte, 0xffc); /* load delay */
	i_addu(p, ptr, ptr, pte);
	i_lw(p, pte, 0, ptr);
	i_tlbp(p); /* load delay */
}

static void __init build_r3000_tlb_load_handler(void)
{
	u32 *p = handle_tlbl;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbl, 0, sizeof(handle_tlbl));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_present(&p, &l, &r, K0, K1, label_nopage_tlbl);
	i_nop(&p); /* load delay */
	build_make_valid(&p, &r, K0, K1);
	build_r3000_tlb_reload_write(&p, &l, &r, K0, K1);

	l_nopage_tlbl(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_0 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbl) > FASTPATH_SIZE)
		panic("TLB load handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB load handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbl));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbl); i++)
			printk("%08x\n", handle_tlbl[i]);
	}
#endif
}

static void __init build_r3000_tlb_store_handler(void)
{
	u32 *p = handle_tlbs;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbs, 0, sizeof(handle_tlbs));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_writable(&p, &l, &r, K0, K1, label_nopage_tlbs);
	i_nop(&p); /* load delay */
	build_make_write(&p, &r, K0, K1);
	build_r3000_tlb_reload_write(&p, &l, &r, K0, K1);

	l_nopage_tlbs(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbs) > FASTPATH_SIZE)
		panic("TLB store handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB store handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbs));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbs); i++)
			printk("%08x\n", handle_tlbs[i]);
	}
#endif
}

static void __init build_r3000_tlb_modify_handler(void)
{
	u32 *p = handle_tlbm;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbm, 0, sizeof(handle_tlbm));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_modifiable(&p, &l, &r, K0, K1, label_nopage_tlbm);
	i_nop(&p); /* load delay */
	build_make_write(&p, &r, K0, K1);
	build_r3000_pte_reload_tlbwi(&p, K0, K1);

	l_nopage_tlbm(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbm) > FASTPATH_SIZE)
		panic("TLB modify handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB modify handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbm));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbm); i++)
			printk("%08x\n", handle_tlbm[i]);
	}
#endif
}

/*
 * R4000 style TLB load/store/modify handlers.
 */
static void __init
build_r4000_tlbchange_handler_head(u32 **p, struct label **l,
				   struct reloc **r, unsigned int pte,
				   unsigned int ptr)
{
#ifdef CONFIG_64BIT
	build_get_pmde64(p, l, r, pte, ptr); /* get pmd in ptr */
#else
	build_get_pgde32(p, pte, ptr); /* get pgd in ptr */
#endif

	i_MFC0(p, pte, C0_BADVADDR);
	i_LW(p, ptr, 0, ptr);
	i_SRL(p, pte, pte, PAGE_SHIFT + PTE_ORDER - PTE_T_LOG2);
	i_andi(p, pte, pte, (PTRS_PER_PTE - 1) << PTE_T_LOG2);
	i_ADDU(p, ptr, ptr, pte);

#ifdef CONFIG_SMP
	l_smp_pgtable_change(l, *p);
# endif
	iPTE_LW(p, l, pte, ptr); /* get even pte */
	build_tlb_probe_entry(p);
}

static void __init
build_r4000_tlbchange_handler_tail(u32 **p, struct label **l,
				   struct reloc **r, unsigned int tmp,
				   unsigned int ptr)
{
	i_ori(p, ptr, ptr, sizeof(pte_t));
	i_xori(p, ptr, ptr, sizeof(pte_t));
	build_update_entries(p, tmp, ptr);
	build_tlb_write_entry(p, l, r, tlb_indexed);
	l_leave(l, *p);
	i_eret(p); /* return from trap */

#ifdef CONFIG_64BIT
	build_get_pgd_vmalloc64(p, l, r, tmp, ptr);
#endif
}

static void __init build_r4000_tlb_load_handler(void)
{
	u32 *p = handle_tlbl;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbl, 0, sizeof(handle_tlbl));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	if (bcm1250_m3_war()) {
		i_MFC0(&p, K0, C0_BADVADDR);
		i_MFC0(&p, K1, C0_ENTRYHI);
		i_xor(&p, K0, K0, K1);
		i_SRL(&p, K0, K0, PAGE_SHIFT + 1);
		il_bnez(&p, &r, K0, label_leave);
		/* No need for i_nop */
	}

	build_r4000_tlbchange_handler_head(&p, &l, &r, K0, K1);
	build_pte_present(&p, &l, &r, K0, K1, label_nopage_tlbl);
	build_make_valid(&p, &r, K0, K1);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, K0, K1);

	l_nopage_tlbl(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_0 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbl) > FASTPATH_SIZE)
		panic("TLB load handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB load handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbl));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbl); i++)
			printk("%08x\n", handle_tlbl[i]);
	}
#endif
}

static void __init build_r4000_tlb_store_handler(void)
{
	u32 *p = handle_tlbs;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbs, 0, sizeof(handle_tlbs));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r4000_tlbchange_handler_head(&p, &l, &r, K0, K1);
	build_pte_writable(&p, &l, &r, K0, K1, label_nopage_tlbs);
	build_make_write(&p, &r, K0, K1);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, K0, K1);

	l_nopage_tlbs(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbs) > FASTPATH_SIZE)
		panic("TLB store handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB store handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbs));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbs); i++)
			printk("%08x\n", handle_tlbs[i]);
	}
#endif
}

static void __init build_r4000_tlb_modify_handler(void)
{
	u32 *p = handle_tlbm;
	struct label *l = labels;
	struct reloc *r = relocs;

	memset(handle_tlbm, 0, sizeof(handle_tlbm));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r4000_tlbchange_handler_head(&p, &l, &r, K0, K1);
	build_pte_modifiable(&p, &l, &r, K0, K1, label_nopage_tlbm);
	/* Present and writable bits set, set accessed and dirty bits. */
	build_make_write(&p, &r, K0, K1);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, K0, K1);

	l_nopage_tlbm(&l, p);
	i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	i_nop(&p);

	if ((p - handle_tlbm) > FASTPATH_SIZE)
		panic("TLB modify handler fastpath space exceeded");

	resolve_relocs(relocs, labels);
	printk("Synthesized TLB modify handler fastpath (%u instructions).\n",
	       (unsigned int)(p - handle_tlbm));

#ifdef DEBUG_TLB
	{
		int i;

		for (i = 0; i < (p - handle_tlbm); i++)
			printk("%08x\n", handle_tlbm[i]);
	}
#endif
}

void __init build_tlb_refill_handler(void)
{
	/*
	 * The refill handler is generated per-CPU, multi-node systems
	 * may have local storage for it. The other handlers are only
	 * needed once.
	 */
	static int run_once = 0;

	switch (current_cpu_data.cputype) {
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3081E:
	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:
		build_r3000_tlb_refill_handler();
		if (!run_once) {
			build_r3000_tlb_load_handler();
			build_r3000_tlb_store_handler();
			build_r3000_tlb_modify_handler();
			run_once++;
		}
		break;

	case CPU_R6000:
	case CPU_R6000A:
		panic("No R6000 TLB refill handler yet");
		break;

	case CPU_R8000:
		panic("No R8000 TLB refill handler yet");
		break;

	default:
		build_r4000_tlb_refill_handler();
		if (!run_once) {
			build_r4000_tlb_load_handler();
			build_r4000_tlb_store_handler();
			build_r4000_tlb_modify_handler();
			run_once++;
		}
	}
}

void __init flush_tlb_handlers(void)
{
	flush_icache_range((unsigned long)handle_tlbl,
			   (unsigned long)handle_tlbl + sizeof(handle_tlbl));
	flush_icache_range((unsigned long)handle_tlbs,
			   (unsigned long)handle_tlbs + sizeof(handle_tlbs));
	flush_icache_range((unsigned long)handle_tlbm,
			   (unsigned long)handle_tlbm + sizeof(handle_tlbm));
}
