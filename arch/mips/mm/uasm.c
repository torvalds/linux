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
	SCIMM = 0x400,
	SIMM9 = 0x800,
};

#define OP_MASK		0x3f
#define OP_SH		26
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
#define SIMM9_SH	7
#define SIMM9_MASK	0x1ff

enum opcode {
	insn_addiu, insn_addu, insn_and, insn_andi, insn_bbit0, insn_bbit1,
	insn_beq, insn_beql, insn_bgez, insn_bgezl, insn_bgtz, insn_blez,
	insn_bltz, insn_bltzl, insn_bne, insn_break, insn_cache, insn_cfc1,
	insn_cfcmsa, insn_ctc1, insn_ctcmsa, insn_daddiu, insn_daddu, insn_ddivu,
	insn_di, insn_dins, insn_dinsm, insn_dinsu, insn_divu, insn_dmfc0,
	insn_dmtc0, insn_dmultu, insn_drotr, insn_drotr32, insn_dsbh, insn_dshd,
	insn_dsll, insn_dsll32, insn_dsllv, insn_dsra, insn_dsra32, insn_dsrav,
	insn_dsrl, insn_dsrl32, insn_dsrlv, insn_dsubu, insn_eret, insn_ext,
	insn_ins, insn_j, insn_jal, insn_jalr, insn_jr, insn_lb, insn_lbu,
	insn_ld, insn_lddir, insn_ldpte, insn_ldx, insn_lh, insn_lhu,
	insn_ll, insn_lld, insn_lui, insn_lw, insn_lwu, insn_lwx, insn_mfc0,
	insn_mfhc0, insn_mfhi, insn_mflo, insn_movn, insn_movz, insn_mtc0,
	insn_mthc0, insn_mthi, insn_mtlo, insn_mul, insn_multu, insn_nor,
	insn_or, insn_ori, insn_pref, insn_rfe, insn_rotr, insn_sb,
	insn_sc, insn_scd, insn_sd, insn_sh, insn_sll, insn_sllv,
	insn_slt, insn_slti, insn_sltiu, insn_sltu, insn_sra, insn_srl,
	insn_srlv, insn_subu, insn_sw, insn_sync, insn_syscall, insn_tlbp,
	insn_tlbr, insn_tlbwi, insn_tlbwr, insn_wait, insn_wsbh, insn_xor,
	insn_xori, insn_yield,
	insn_invalid /* insn_invalid must be last */
};

struct insn {
	u32 match;
	enum fields fields;
};

static inline u32 build_rs(u32 arg)
{
	WARN(arg & ~RS_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RS_MASK) << RS_SH;
}

static inline u32 build_rt(u32 arg)
{
	WARN(arg & ~RT_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RT_MASK) << RT_SH;
}

static inline u32 build_rd(u32 arg)
{
	WARN(arg & ~RD_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RD_MASK) << RD_SH;
}

static inline u32 build_re(u32 arg)
{
	WARN(arg & ~RE_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & RE_MASK) << RE_SH;
}

static inline u32 build_simm(s32 arg)
{
	WARN(arg > 0x7fff || arg < -0x8000,
	     KERN_WARNING "Micro-assembler field overflow\n");

	return arg & 0xffff;
}

static inline u32 build_uimm(u32 arg)
{
	WARN(arg & ~IMM_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return arg & IMM_MASK;
}

static inline u32 build_scimm(u32 arg)
{
	WARN(arg & ~SCIMM_MASK,
	     KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & SCIMM_MASK) << SCIMM_SH;
}

static inline u32 build_scimm9(s32 arg)
{
	WARN((arg > 0xff || arg < -0x100),
	       KERN_WARNING "Micro-assembler field overflow\n");

	return (arg & SIMM9_MASK) << SIMM9_SH;
}

static inline u32 build_func(u32 arg)
{
	WARN(arg & ~FUNC_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return arg & FUNC_MASK;
}

static inline u32 build_set(u32 arg)
{
	WARN(arg & ~SET_MASK, KERN_WARNING "Micro-assembler field overflow\n");

	return arg & SET_MASK;
}

static void build_insn(u32 **buf, enum opcode opc, ...);

#define I_u1u2u3(op)					\
Ip_u1u2u3(op)						\
{							\
	build_insn(buf, insn##op, a, b, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_s3s1s2(op)					\
Ip_s3s1s2(op)						\
{							\
	build_insn(buf, insn##op, b, c, a);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1u3(op)					\
Ip_u2u1u3(op)						\
{							\
	build_insn(buf, insn##op, b, a, c);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u3u2u1(op)					\
Ip_u3u2u1(op)						\
{							\
	build_insn(buf, insn##op, c, b, a);		\
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

#define I_u2u1msb32u3(op)				\
Ip_u2u1msbu3(op)					\
{							\
	build_insn(buf, insn##op, b, a, c+d-33, c);	\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1msb32msb3(op)				\
Ip_u2u1msbu3(op)					\
{							\
	build_insn(buf, insn##op, b, a, c+d-33, c-32);	\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1msbdu3(op)				\
Ip_u2u1msbu3(op)					\
{							\
	build_insn(buf, insn##op, b, a, d-1, c);	\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u1u2(op)					\
Ip_u1u2(op)						\
{							\
	build_insn(buf, insn##op, a, b);		\
}							\
UASM_EXPORT_SYMBOL(uasm_i##op);

#define I_u2u1(op)					\
Ip_u1u2(op)						\
{							\
	build_insn(buf, insn##op, b, a);		\
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
I_u1s2(_bgtz)
I_u1s2(_blez)
I_u1s2(_bltz)
I_u1s2(_bltzl)
I_u1u2s3(_bne)
I_u1(_break)
I_u2s3u1(_cache)
I_u1u2(_cfc1)
I_u2u1(_cfcmsa)
I_u1u2(_ctc1)
I_u2u1(_ctcmsa)
I_u1u2(_ddivu)
I_u1u2u3(_dmfc0)
I_u1u2u3(_dmtc0)
I_u1u2(_dmultu)
I_u2u1s3(_daddiu)
I_u3u1u2(_daddu)
I_u1(_di);
I_u1u2(_divu)
I_u2u1(_dsbh);
I_u2u1(_dshd);
I_u2u1u3(_dsll)
I_u2u1u3(_dsll32)
I_u3u2u1(_dsllv)
I_u2u1u3(_dsra)
I_u2u1u3(_dsra32)
I_u3u2u1(_dsrav)
I_u2u1u3(_dsrl)
I_u2u1u3(_dsrl32)
I_u3u2u1(_dsrlv)
I_u2u1u3(_drotr)
I_u2u1u3(_drotr32)
I_u3u1u2(_dsubu)
I_0(_eret)
I_u2u1msbdu3(_ext)
I_u2u1msbu3(_ins)
I_u1(_j)
I_u1(_jal)
I_u2u1(_jalr)
I_u1(_jr)
I_u2s3u1(_lb)
I_u2s3u1(_lbu)
I_u2s3u1(_ld)
I_u2s3u1(_lh)
I_u2s3u1(_lhu)
I_u2s3u1(_ll)
I_u2s3u1(_lld)
I_u1s2(_lui)
I_u2s3u1(_lw)
I_u2s3u1(_lwu)
I_u1u2u3(_mfc0)
I_u1u2u3(_mfhc0)
I_u3u1u2(_movn)
I_u3u1u2(_movz)
I_u1(_mfhi)
I_u1(_mflo)
I_u1u2u3(_mtc0)
I_u1u2u3(_mthc0)
I_u1(_mthi)
I_u1(_mtlo)
I_u3u1u2(_mul)
I_u1u2(_multu)
I_u3u1u2(_nor)
I_u3u1u2(_or)
I_u2u1u3(_ori)
I_0(_rfe)
I_u2s3u1(_sb)
I_u2s3u1(_sc)
I_u2s3u1(_scd)
I_u2s3u1(_sd)
I_u2s3u1(_sh)
I_u2u1u3(_sll)
I_u3u2u1(_sllv)
I_s3s1s2(_slt)
I_u2u1s3(_slti)
I_u2u1s3(_sltiu)
I_u3u1u2(_sltu)
I_u2u1u3(_sra)
I_u2u1u3(_srl)
I_u3u2u1(_srlv)
I_u2u1u3(_rotr)
I_u3u1u2(_subu)
I_u2s3u1(_sw)
I_u1(_sync)
I_0(_tlbp)
I_0(_tlbr)
I_0(_tlbwi)
I_0(_tlbwr)
I_u1(_wait);
I_u2u1(_wsbh)
I_u3u1u2(_xor)
I_u2u1u3(_xori)
I_u2u1(_yield)
I_u2u1msbu3(_dins);
I_u2u1msb32u3(_dinsm);
I_u2u1msb32msb3(_dinsu);
I_u1(_syscall);
I_u1u2s3(_bbit0);
I_u1u2s3(_bbit1);
I_u3u1u2(_lwx)
I_u3u1u2(_ldx)
I_u1u2(_ldpte)
I_u2u1u3(_lddir)

#ifdef CONFIG_CPU_CAVIUM_OCTEON
#include <asm/octeon/octeon.h>
void uasm_i_pref(u32 **buf, unsigned int a, signed int b,
			    unsigned int c)
{
	if (CAVIUM_OCTEON_DCACHE_PREFETCH_WAR && a <= 24 && a != 5)
		/*
		 * As per erratum Core-14449, replace prefetches 0-4,
		 * 6-24 with 'pref 28'.
		 */
		build_insn(buf, insn_pref, c, 28, b);
	else
		build_insn(buf, insn_pref, c, a, b);
}
UASM_EXPORT_SYMBOL(uasm_i_pref);
#else
I_u2s3u1(_pref)
#endif

/* Handle labels. */
void uasm_build_label(struct uasm_label **lab, u32 *addr, int lid)
{
	(*lab)->addr = addr;
	(*lab)->lab = lid;
	(*lab)++;
}
UASM_EXPORT_SYMBOL(uasm_build_label);

int uasm_in_compat_space_p(long addr)
{
	/* Is this address in 32bit compat space? */
	return addr == (int)addr;
}
UASM_EXPORT_SYMBOL(uasm_in_compat_space_p);

static int uasm_rel_highest(long val)
{
#ifdef CONFIG_64BIT
	return ((((val + 0x800080008000L) >> 48) & 0xffff) ^ 0x8000) - 0x8000;
#else
	return 0;
#endif
}

static int uasm_rel_higher(long val)
{
#ifdef CONFIG_64BIT
	return ((((val + 0x80008000L) >> 32) & 0xffff) ^ 0x8000) - 0x8000;
#else
	return 0;
#endif
}

int uasm_rel_hi(long val)
{
	return ((((val + 0x8000L) >> 16) & 0xffff) ^ 0x8000) - 0x8000;
}
UASM_EXPORT_SYMBOL(uasm_rel_hi);

int uasm_rel_lo(long val)
{
	return ((val & 0xffff) ^ 0x8000) - 0x8000;
}
UASM_EXPORT_SYMBOL(uasm_rel_lo);

void UASM_i_LA_mostly(u32 **buf, unsigned int rs, long addr)
{
	if (!uasm_in_compat_space_p(addr)) {
		uasm_i_lui(buf, rs, uasm_rel_highest(addr));
		if (uasm_rel_higher(addr))
			uasm_i_daddiu(buf, rs, rs, uasm_rel_higher(addr));
		if (uasm_rel_hi(addr)) {
			uasm_i_dsll(buf, rs, rs, 16);
			uasm_i_daddiu(buf, rs, rs,
					uasm_rel_hi(addr));
			uasm_i_dsll(buf, rs, rs, 16);
		} else
			uasm_i_dsll32(buf, rs, rs, 0);
	} else
		uasm_i_lui(buf, rs, uasm_rel_hi(addr));
}
UASM_EXPORT_SYMBOL(UASM_i_LA_mostly);

void UASM_i_LA(u32 **buf, unsigned int rs, long addr)
{
	UASM_i_LA_mostly(buf, rs, addr);
	if (uasm_rel_lo(addr)) {
		if (!uasm_in_compat_space_p(addr))
			uasm_i_daddiu(buf, rs, rs,
					uasm_rel_lo(addr));
		else
			uasm_i_addiu(buf, rs, rs,
					uasm_rel_lo(addr));
	}
}
UASM_EXPORT_SYMBOL(UASM_i_LA);

/* Handle relocations. */
void uasm_r_mips_pc16(struct uasm_reloc **rel, u32 *addr, int lid)
{
	(*rel)->addr = addr;
	(*rel)->type = R_MIPS_PC16;
	(*rel)->lab = lid;
	(*rel)++;
}
UASM_EXPORT_SYMBOL(uasm_r_mips_pc16);

static inline void __resolve_relocs(struct uasm_reloc *rel,
				    struct uasm_label *lab);

void uasm_resolve_relocs(struct uasm_reloc *rel,
				  struct uasm_label *lab)
{
	struct uasm_label *l;

	for (; rel->lab != UASM_LABEL_INVALID; rel++)
		for (l = lab; l->lab != UASM_LABEL_INVALID; l++)
			if (rel->lab == l->lab)
				__resolve_relocs(rel, l);
}
UASM_EXPORT_SYMBOL(uasm_resolve_relocs);

void uasm_move_relocs(struct uasm_reloc *rel, u32 *first, u32 *end,
			       long off)
{
	for (; rel->lab != UASM_LABEL_INVALID; rel++)
		if (rel->addr >= first && rel->addr < end)
			rel->addr += off;
}
UASM_EXPORT_SYMBOL(uasm_move_relocs);

void uasm_move_labels(struct uasm_label *lab, u32 *first, u32 *end,
			       long off)
{
	for (; lab->lab != UASM_LABEL_INVALID; lab++)
		if (lab->addr >= first && lab->addr < end)
			lab->addr += off;
}
UASM_EXPORT_SYMBOL(uasm_move_labels);

void uasm_copy_handler(struct uasm_reloc *rel, struct uasm_label *lab,
				u32 *first, u32 *end, u32 *target)
{
	long off = (long)(target - first);

	memcpy(target, first, (end - first) * sizeof(u32));

	uasm_move_relocs(rel, first, end, off);
	uasm_move_labels(lab, first, end, off);
}
UASM_EXPORT_SYMBOL(uasm_copy_handler);

int uasm_insn_has_bdelay(struct uasm_reloc *rel, u32 *addr)
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
void uasm_il_bltz(u32 **p, struct uasm_reloc **r, unsigned int reg,
			   int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bltz(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bltz);

void uasm_il_b(u32 **p, struct uasm_reloc **r, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_b(p, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_b);

void uasm_il_beq(u32 **p, struct uasm_reloc **r, unsigned int r1,
			  unsigned int r2, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_beq(p, r1, r2, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_beq);

void uasm_il_beqz(u32 **p, struct uasm_reloc **r, unsigned int reg,
			   int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_beqz(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_beqz);

void uasm_il_beqzl(u32 **p, struct uasm_reloc **r, unsigned int reg,
			    int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_beqzl(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_beqzl);

void uasm_il_bne(u32 **p, struct uasm_reloc **r, unsigned int reg1,
			  unsigned int reg2, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bne(p, reg1, reg2, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bne);

void uasm_il_bnez(u32 **p, struct uasm_reloc **r, unsigned int reg,
			   int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bnez(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bnez);

void uasm_il_bgezl(u32 **p, struct uasm_reloc **r, unsigned int reg,
			    int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bgezl(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bgezl);

void uasm_il_bgez(u32 **p, struct uasm_reloc **r, unsigned int reg,
			   int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bgez(p, reg, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bgez);

void uasm_il_bbit0(u32 **p, struct uasm_reloc **r, unsigned int reg,
			    unsigned int bit, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bbit0(p, reg, bit, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bbit0);

void uasm_il_bbit1(u32 **p, struct uasm_reloc **r, unsigned int reg,
			    unsigned int bit, int lid)
{
	uasm_r_mips_pc16(r, *p, lid);
	uasm_i_bbit1(p, reg, bit, 0);
}
UASM_EXPORT_SYMBOL(uasm_il_bbit1);
