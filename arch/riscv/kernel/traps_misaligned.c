// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/stringify.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/csr.h>

#define INSN_MATCH_LB			0x3
#define INSN_MASK_LB			0x707f
#define INSN_MATCH_LH			0x1003
#define INSN_MASK_LH			0x707f
#define INSN_MATCH_LW			0x2003
#define INSN_MASK_LW			0x707f
#define INSN_MATCH_LD			0x3003
#define INSN_MASK_LD			0x707f
#define INSN_MATCH_LBU			0x4003
#define INSN_MASK_LBU			0x707f
#define INSN_MATCH_LHU			0x5003
#define INSN_MASK_LHU			0x707f
#define INSN_MATCH_LWU			0x6003
#define INSN_MASK_LWU			0x707f
#define INSN_MATCH_SB			0x23
#define INSN_MASK_SB			0x707f
#define INSN_MATCH_SH			0x1023
#define INSN_MASK_SH			0x707f
#define INSN_MATCH_SW			0x2023
#define INSN_MASK_SW			0x707f
#define INSN_MATCH_SD			0x3023
#define INSN_MASK_SD			0x707f

#define INSN_MATCH_FLW			0x2007
#define INSN_MASK_FLW			0x707f
#define INSN_MATCH_FLD			0x3007
#define INSN_MASK_FLD			0x707f
#define INSN_MATCH_FLQ			0x4007
#define INSN_MASK_FLQ			0x707f
#define INSN_MATCH_FSW			0x2027
#define INSN_MASK_FSW			0x707f
#define INSN_MATCH_FSD			0x3027
#define INSN_MASK_FSD			0x707f
#define INSN_MATCH_FSQ			0x4027
#define INSN_MASK_FSQ			0x707f

#define INSN_MATCH_C_LD			0x6000
#define INSN_MASK_C_LD			0xe003
#define INSN_MATCH_C_SD			0xe000
#define INSN_MASK_C_SD			0xe003
#define INSN_MATCH_C_LW			0x4000
#define INSN_MASK_C_LW			0xe003
#define INSN_MATCH_C_SW			0xc000
#define INSN_MASK_C_SW			0xe003
#define INSN_MATCH_C_LDSP		0x6002
#define INSN_MASK_C_LDSP		0xe003
#define INSN_MATCH_C_SDSP		0xe002
#define INSN_MASK_C_SDSP		0xe003
#define INSN_MATCH_C_LWSP		0x4002
#define INSN_MASK_C_LWSP		0xe003
#define INSN_MATCH_C_SWSP		0xc002
#define INSN_MASK_C_SWSP		0xe003

#define INSN_MATCH_C_FLD		0x2000
#define INSN_MASK_C_FLD			0xe003
#define INSN_MATCH_C_FLW		0x6000
#define INSN_MASK_C_FLW			0xe003
#define INSN_MATCH_C_FSD		0xa000
#define INSN_MASK_C_FSD			0xe003
#define INSN_MATCH_C_FSW		0xe000
#define INSN_MASK_C_FSW			0xe003
#define INSN_MATCH_C_FLDSP		0x2002
#define INSN_MASK_C_FLDSP		0xe003
#define INSN_MATCH_C_FSDSP		0xa002
#define INSN_MASK_C_FSDSP		0xe003
#define INSN_MATCH_C_FLWSP		0x6002
#define INSN_MASK_C_FLWSP		0xe003
#define INSN_MATCH_C_FSWSP		0xe002
#define INSN_MASK_C_FSWSP		0xe003

#define INSN_LEN(insn)			((((insn) & 0x3) < 0x3) ? 2 : 4)

#if defined(CONFIG_64BIT)
#define LOG_REGBYTES			3
#define XLEN				64
#else
#define LOG_REGBYTES			2
#define XLEN				32
#endif
#define REGBYTES			(1 << LOG_REGBYTES)
#define XLEN_MINUS_16			((XLEN) - 16)

#define SH_RD				7
#define SH_RS1				15
#define SH_RS2				20
#define SH_RS2C				2

#define RV_X(x, s, n)			(((x) >> (s)) & ((1 << (n)) - 1))
#define RVC_LW_IMM(x)			((RV_X(x, 6, 1) << 2) | \
					 (RV_X(x, 10, 3) << 3) | \
					 (RV_X(x, 5, 1) << 6))
#define RVC_LD_IMM(x)			((RV_X(x, 10, 3) << 3) | \
					 (RV_X(x, 5, 2) << 6))
#define RVC_LWSP_IMM(x)			((RV_X(x, 4, 3) << 2) | \
					 (RV_X(x, 12, 1) << 5) | \
					 (RV_X(x, 2, 2) << 6))
#define RVC_LDSP_IMM(x)			((RV_X(x, 5, 2) << 3) | \
					 (RV_X(x, 12, 1) << 5) | \
					 (RV_X(x, 2, 3) << 6))
#define RVC_SWSP_IMM(x)			((RV_X(x, 9, 4) << 2) | \
					 (RV_X(x, 7, 2) << 6))
#define RVC_SDSP_IMM(x)			((RV_X(x, 10, 3) << 3) | \
					 (RV_X(x, 7, 3) << 6))
#define RVC_RS1S(insn)			(8 + RV_X(insn, SH_RD, 3))
#define RVC_RS2S(insn)			(8 + RV_X(insn, SH_RS2C, 3))
#define RVC_RS2(insn)			RV_X(insn, SH_RS2C, 5)

#define SHIFT_RIGHT(x, y)		\
	((y) < 0 ? ((x) << -(y)) : ((x) >> (y)))

#define REG_MASK			\
	((1 << (5 + LOG_REGBYTES)) - (1 << LOG_REGBYTES))

#define REG_OFFSET(insn, pos)		\
	(SHIFT_RIGHT((insn), (pos) - LOG_REGBYTES) & REG_MASK)

#define REG_PTR(insn, pos, regs)	\
	(ulong *)((ulong)(regs) + REG_OFFSET(insn, pos))

#define GET_RM(insn)			(((insn) >> 12) & 7)

#define GET_RS1(insn, regs)		(*REG_PTR(insn, SH_RS1, regs))
#define GET_RS2(insn, regs)		(*REG_PTR(insn, SH_RS2, regs))
#define GET_RS1S(insn, regs)		(*REG_PTR(RVC_RS1S(insn), 0, regs))
#define GET_RS2S(insn, regs)		(*REG_PTR(RVC_RS2S(insn), 0, regs))
#define GET_RS2C(insn, regs)		(*REG_PTR(insn, SH_RS2C, regs))
#define GET_SP(regs)			(*REG_PTR(2, 0, regs))
#define SET_RD(insn, regs, val)		(*REG_PTR(insn, SH_RD, regs) = (val))
#define IMM_I(insn)			((s32)(insn) >> 20)
#define IMM_S(insn)			(((s32)(insn) >> 25 << 5) | \
					 (s32)(((insn) >> 7) & 0x1f))
#define MASK_FUNCT3			0x7000

#define GET_PRECISION(insn) (((insn) >> 25) & 3)
#define GET_RM(insn) (((insn) >> 12) & 7)
#define PRECISION_S 0
#define PRECISION_D 1

#define DECLARE_UNPRIVILEGED_LOAD_FUNCTION(type, insn)			\
static inline type load_##type(const type *addr)			\
{									\
	type val;							\
	asm (#insn " %0, %1"						\
	: "=&r" (val) : "m" (*addr));					\
	return val;							\
}

#define DECLARE_UNPRIVILEGED_STORE_FUNCTION(type, insn)			\
static inline void store_##type(type *addr, type val)			\
{									\
	asm volatile (#insn " %0, %1\n"					\
	: : "r" (val), "m" (*addr));					\
}

DECLARE_UNPRIVILEGED_LOAD_FUNCTION(u8, lbu)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(u16, lhu)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(s8, lb)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(s16, lh)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(s32, lw)
DECLARE_UNPRIVILEGED_STORE_FUNCTION(u8, sb)
DECLARE_UNPRIVILEGED_STORE_FUNCTION(u16, sh)
DECLARE_UNPRIVILEGED_STORE_FUNCTION(u32, sw)
#if defined(CONFIG_64BIT)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(u32, lwu)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(u64, ld)
DECLARE_UNPRIVILEGED_STORE_FUNCTION(u64, sd)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(ulong, ld)
#else
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(u32, lw)
DECLARE_UNPRIVILEGED_LOAD_FUNCTION(ulong, lw)

static inline u64 load_u64(const u64 *addr)
{
	return load_u32((u32 *)addr)
		+ ((u64)load_u32((u32 *)addr + 1) << 32);
}

static inline void store_u64(u64 *addr, u64 val)
{
	store_u32((u32 *)addr, val);
	store_u32((u32 *)addr + 1, val >> 32);
}
#endif

static inline ulong get_insn(ulong mepc)
{
	register ulong __mepc asm ("a2") = mepc;
	ulong val, rvc_mask = 3, tmp;

	asm ("and %[tmp], %[addr], 2\n"
		"bnez %[tmp], 1f\n"
#if defined(CONFIG_64BIT)
		__stringify(LWU) " %[insn], (%[addr])\n"
#else
		__stringify(LW) " %[insn], (%[addr])\n"
#endif
		"and %[tmp], %[insn], %[rvc_mask]\n"
		"beq %[tmp], %[rvc_mask], 2f\n"
		"sll %[insn], %[insn], %[xlen_minus_16]\n"
		"srl %[insn], %[insn], %[xlen_minus_16]\n"
		"j 2f\n"
		"1:\n"
		"lhu %[insn], (%[addr])\n"
		"and %[tmp], %[insn], %[rvc_mask]\n"
		"bne %[tmp], %[rvc_mask], 2f\n"
		"lhu %[tmp], 2(%[addr])\n"
		"sll %[tmp], %[tmp], 16\n"
		"add %[insn], %[insn], %[tmp]\n"
		"2:"
	: [insn] "=&r" (val), [tmp] "=&r" (tmp)
	: [addr] "r" (__mepc), [rvc_mask] "r" (rvc_mask),
	  [xlen_minus_16] "i" (XLEN_MINUS_16));

	return val;
}

union reg_data {
	u8 data_bytes[8];
	ulong data_ulong;
	u64 data_u64;
};

int handle_misaligned_load(struct pt_regs *regs)
{
	union reg_data val;
	unsigned long epc = regs->epc;
	unsigned long insn = get_insn(epc);
	unsigned long addr = csr_read(mtval);
	int i, fp = 0, shift = 0, len = 0;

	regs->epc = 0;

	if ((insn & INSN_MASK_LW) == INSN_MATCH_LW) {
		len = 4;
		shift = 8 * (sizeof(unsigned long) - len);
#if defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_LD) == INSN_MATCH_LD) {
		len = 8;
		shift = 8 * (sizeof(unsigned long) - len);
	} else if ((insn & INSN_MASK_LWU) == INSN_MATCH_LWU) {
		len = 4;
#endif
	} else if ((insn & INSN_MASK_FLD) == INSN_MATCH_FLD) {
		fp = 1;
		len = 8;
	} else if ((insn & INSN_MASK_FLW) == INSN_MATCH_FLW) {
		fp = 1;
		len = 4;
	} else if ((insn & INSN_MASK_LH) == INSN_MATCH_LH) {
		len = 2;
		shift = 8 * (sizeof(unsigned long) - len);
	} else if ((insn & INSN_MASK_LHU) == INSN_MATCH_LHU) {
		len = 2;
#if defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_C_LD) == INSN_MATCH_C_LD) {
		len = 8;
		shift = 8 * (sizeof(unsigned long) - len);
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_LDSP) == INSN_MATCH_C_LDSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 8;
		shift = 8 * (sizeof(unsigned long) - len);
#endif
	} else if ((insn & INSN_MASK_C_LW) == INSN_MATCH_C_LW) {
		len = 4;
		shift = 8 * (sizeof(unsigned long) - len);
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_LWSP) == INSN_MATCH_C_LWSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		len = 4;
		shift = 8 * (sizeof(unsigned long) - len);
	} else if ((insn & INSN_MASK_C_FLD) == INSN_MATCH_C_FLD) {
		fp = 1;
		len = 8;
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_FLDSP) == INSN_MATCH_C_FLDSP) {
		fp = 1;
		len = 8;
#if defined(CONFIG_32BIT)
	} else if ((insn & INSN_MASK_C_FLW) == INSN_MATCH_C_FLW) {
		fp = 1;
		len = 4;
		insn = RVC_RS2S(insn) << SH_RD;
	} else if ((insn & INSN_MASK_C_FLWSP) == INSN_MATCH_C_FLWSP) {
		fp = 1;
		len = 4;
#endif
	} else {
		regs->epc = epc;
		return -1;
	}

	val.data_u64 = 0;
	for (i = 0; i < len; i++)
		val.data_bytes[i] = load_u8((void *)(addr + i));

	if (fp)
		return -1;
	SET_RD(insn, regs, val.data_ulong << shift >> shift);

	regs->epc = epc + INSN_LEN(insn);

	return 0;
}

int handle_misaligned_store(struct pt_regs *regs)
{
	union reg_data val;
	unsigned long epc = regs->epc;
	unsigned long insn = get_insn(epc);
	unsigned long addr = csr_read(mtval);
	int i, len = 0;

	regs->epc = 0;

	val.data_ulong = GET_RS2(insn, regs);

	if ((insn & INSN_MASK_SW) == INSN_MATCH_SW) {
		len = 4;
#if defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_SD) == INSN_MATCH_SD) {
		len = 8;
#endif
	} else if ((insn & INSN_MASK_SH) == INSN_MATCH_SH) {
		len = 2;
#if defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_C_SD) == INSN_MATCH_C_SD) {
		len = 8;
		val.data_ulong = GET_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_SDSP) == INSN_MATCH_C_SDSP) {
		len = 8;
		val.data_ulong = GET_RS2C(insn, regs);
#endif
	} else if ((insn & INSN_MASK_C_SW) == INSN_MATCH_C_SW) {
		len = 4;
		val.data_ulong = GET_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_SWSP) == INSN_MATCH_C_SWSP) {
		len = 4;
		val.data_ulong = GET_RS2C(insn, regs);
	} else {
		regs->epc = epc;
		return -1;
	}

	for (i = 0; i < len; i++)
		store_u8((void *)(addr + i), val.data_bytes[i]);

	regs->epc = epc + INSN_LEN(insn);

	return 0;
}
