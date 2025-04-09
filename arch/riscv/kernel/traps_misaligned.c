// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/irq.h>
#include <linux/stringify.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/csr.h>
#include <asm/entry-common.h>
#include <asm/hwprobe.h>
#include <asm/cpufeature.h>
#include <asm/vector.h>

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

#ifdef CONFIG_FPU

#define FP_GET_RD(insn)		(insn >> 7 & 0x1F)

extern void put_f32_reg(unsigned long fp_reg, unsigned long value);

static int set_f32_rd(unsigned long insn, struct pt_regs *regs,
		      unsigned long val)
{
	unsigned long fp_reg = FP_GET_RD(insn);

	put_f32_reg(fp_reg, val);
	regs->status |= SR_FS_DIRTY;

	return 0;
}

extern void put_f64_reg(unsigned long fp_reg, unsigned long value);

static int set_f64_rd(unsigned long insn, struct pt_regs *regs, u64 val)
{
	unsigned long fp_reg = FP_GET_RD(insn);
	unsigned long value;

#if __riscv_xlen == 32
	value = (unsigned long) &val;
#else
	value = val;
#endif
	put_f64_reg(fp_reg, value);
	regs->status |= SR_FS_DIRTY;

	return 0;
}

#if __riscv_xlen == 32
extern void get_f64_reg(unsigned long fp_reg, u64 *value);

static u64 get_f64_rs(unsigned long insn, u8 fp_reg_offset,
		      struct pt_regs *regs)
{
	unsigned long fp_reg = (insn >> fp_reg_offset) & 0x1F;
	u64 val;

	get_f64_reg(fp_reg, &val);
	regs->status |= SR_FS_DIRTY;

	return val;
}
#else

extern unsigned long get_f64_reg(unsigned long fp_reg);

static unsigned long get_f64_rs(unsigned long insn, u8 fp_reg_offset,
				struct pt_regs *regs)
{
	unsigned long fp_reg = (insn >> fp_reg_offset) & 0x1F;
	unsigned long val;

	val = get_f64_reg(fp_reg);
	regs->status |= SR_FS_DIRTY;

	return val;
}

#endif

extern unsigned long get_f32_reg(unsigned long fp_reg);

static unsigned long get_f32_rs(unsigned long insn, u8 fp_reg_offset,
				struct pt_regs *regs)
{
	unsigned long fp_reg = (insn >> fp_reg_offset) & 0x1F;
	unsigned long val;

	val = get_f32_reg(fp_reg);
	regs->status |= SR_FS_DIRTY;

	return val;
}

#else /* CONFIG_FPU */
static void set_f32_rd(unsigned long insn, struct pt_regs *regs,
		       unsigned long val) {}

static void set_f64_rd(unsigned long insn, struct pt_regs *regs, u64 val) {}

static unsigned long get_f64_rs(unsigned long insn, u8 fp_reg_offset,
				struct pt_regs *regs)
{
	return 0;
}

static unsigned long get_f32_rs(unsigned long insn, u8 fp_reg_offset,
				struct pt_regs *regs)
{
	return 0;
}

#endif

#define GET_F64_RS2(insn, regs) (get_f64_rs(insn, 20, regs))
#define GET_F64_RS2C(insn, regs) (get_f64_rs(insn, 2, regs))
#define GET_F64_RS2S(insn, regs) (get_f64_rs(RVC_RS2S(insn), 0, regs))

#define GET_F32_RS2(insn, regs) (get_f32_rs(insn, 20, regs))
#define GET_F32_RS2C(insn, regs) (get_f32_rs(insn, 2, regs))
#define GET_F32_RS2S(insn, regs) (get_f32_rs(RVC_RS2S(insn), 0, regs))

#define __read_insn(regs, insn, insn_addr, type)	\
({							\
	int __ret;					\
							\
	if (user_mode(regs)) {				\
		__ret = __get_user(insn, (type __user *) insn_addr); \
	} else {					\
		insn = *(type *)insn_addr;		\
		__ret = 0;				\
	}						\
							\
	__ret;						\
})

static inline int get_insn(struct pt_regs *regs, ulong epc, ulong *r_insn)
{
	ulong insn = 0;

	if (epc & 0x2) {
		ulong tmp = 0;

		if (__read_insn(regs, insn, epc, u16))
			return -EFAULT;
		/* __get_user() uses regular "lw" which sign extend the loaded
		 * value make sure to clear higher order bits in case we "or" it
		 * below with the upper 16 bits half.
		 */
		insn &= GENMASK(15, 0);
		if ((insn & __INSN_LENGTH_MASK) != __INSN_LENGTH_32) {
			*r_insn = insn;
			return 0;
		}
		epc += sizeof(u16);
		if (__read_insn(regs, tmp, epc, u16))
			return -EFAULT;
		*r_insn = (tmp << 16) | insn;

		return 0;
	} else {
		if (__read_insn(regs, insn, epc, u32))
			return -EFAULT;
		if ((insn & __INSN_LENGTH_MASK) == __INSN_LENGTH_32) {
			*r_insn = insn;
			return 0;
		}
		insn &= GENMASK(15, 0);
		*r_insn = insn;

		return 0;
	}
}

union reg_data {
	u8 data_bytes[8];
	ulong data_ulong;
	u64 data_u64;
};

/* sysctl hooks */
int unaligned_enabled __read_mostly = 1;	/* Enabled by default */

#ifdef CONFIG_RISCV_VECTOR_MISALIGNED
static int handle_vector_misaligned_load(struct pt_regs *regs)
{
	unsigned long epc = regs->epc;
	unsigned long insn;

	if (get_insn(regs, epc, &insn))
		return -1;

	/* Only return 0 when in check_vector_unaligned_access_emulated */
	if (*this_cpu_ptr(&vector_misaligned_access) == RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN) {
		*this_cpu_ptr(&vector_misaligned_access) = RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED;
		regs->epc = epc + INSN_LEN(insn);
		return 0;
	}

	/* If vector instruction we don't emulate it yet */
	regs->epc = epc;
	return -1;
}
#else
static int handle_vector_misaligned_load(struct pt_regs *regs)
{
	return -1;
}
#endif

static int handle_scalar_misaligned_load(struct pt_regs *regs)
{
	union reg_data val;
	unsigned long epc = regs->epc;
	unsigned long insn;
	unsigned long addr = regs->badaddr;
	int fp = 0, shift = 0, len = 0;

	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, addr);

#ifdef CONFIG_RISCV_PROBE_UNALIGNED_ACCESS
	*this_cpu_ptr(&misaligned_access_speed) = RISCV_HWPROBE_MISALIGNED_SCALAR_EMULATED;
#endif

	if (!unaligned_enabled)
		return -1;

	if (user_mode(regs) && (current->thread.align_ctl & PR_UNALIGN_SIGBUS))
		return -1;

	if (get_insn(regs, epc, &insn))
		return -1;

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

	if (!IS_ENABLED(CONFIG_FPU) && fp)
		return -EOPNOTSUPP;

	val.data_u64 = 0;
	if (user_mode(regs)) {
		if (copy_from_user(&val, (u8 __user *)addr, len))
			return -1;
	} else {
		memcpy(&val, (u8 *)addr, len);
	}

	if (!fp)
		SET_RD(insn, regs, val.data_ulong << shift >> shift);
	else if (len == 8)
		set_f64_rd(insn, regs, val.data_u64);
	else
		set_f32_rd(insn, regs, val.data_ulong);

	regs->epc = epc + INSN_LEN(insn);

	return 0;
}

static int handle_scalar_misaligned_store(struct pt_regs *regs)
{
	union reg_data val;
	unsigned long epc = regs->epc;
	unsigned long insn;
	unsigned long addr = regs->badaddr;
	int len = 0, fp = 0;

	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, addr);

	if (!unaligned_enabled)
		return -1;

	if (user_mode(regs) && (current->thread.align_ctl & PR_UNALIGN_SIGBUS))
		return -1;

	if (get_insn(regs, epc, &insn))
		return -1;

	regs->epc = 0;

	val.data_ulong = GET_RS2(insn, regs);

	if ((insn & INSN_MASK_SW) == INSN_MATCH_SW) {
		len = 4;
#if defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_SD) == INSN_MATCH_SD) {
		len = 8;
#endif
	} else if ((insn & INSN_MASK_FSD) == INSN_MATCH_FSD) {
		fp = 1;
		len = 8;
		val.data_u64 = GET_F64_RS2(insn, regs);
	} else if ((insn & INSN_MASK_FSW) == INSN_MATCH_FSW) {
		fp = 1;
		len = 4;
		val.data_ulong = GET_F32_RS2(insn, regs);
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
	} else if ((insn & INSN_MASK_C_FSD) == INSN_MATCH_C_FSD) {
		fp = 1;
		len = 8;
		val.data_u64 = GET_F64_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_FSDSP) == INSN_MATCH_C_FSDSP) {
		fp = 1;
		len = 8;
		val.data_u64 = GET_F64_RS2C(insn, regs);
#if !defined(CONFIG_64BIT)
	} else if ((insn & INSN_MASK_C_FSW) == INSN_MATCH_C_FSW) {
		fp = 1;
		len = 4;
		val.data_ulong = GET_F32_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_FSWSP) == INSN_MATCH_C_FSWSP) {
		fp = 1;
		len = 4;
		val.data_ulong = GET_F32_RS2C(insn, regs);
#endif
	} else {
		regs->epc = epc;
		return -1;
	}

	if (!IS_ENABLED(CONFIG_FPU) && fp)
		return -EOPNOTSUPP;

	if (user_mode(regs)) {
		if (copy_to_user((u8 __user *)addr, &val, len))
			return -1;
	} else {
		memcpy((u8 *)addr, &val, len);
	}

	regs->epc = epc + INSN_LEN(insn);

	return 0;
}

int handle_misaligned_load(struct pt_regs *regs)
{
	unsigned long epc = regs->epc;
	unsigned long insn;

	if (IS_ENABLED(CONFIG_RISCV_VECTOR_MISALIGNED)) {
		if (get_insn(regs, epc, &insn))
			return -1;

		if (insn_is_vector(insn))
			return handle_vector_misaligned_load(regs);
	}

	if (IS_ENABLED(CONFIG_RISCV_SCALAR_MISALIGNED))
		return handle_scalar_misaligned_load(regs);

	return -1;
}

int handle_misaligned_store(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_RISCV_SCALAR_MISALIGNED))
		return handle_scalar_misaligned_store(regs);

	return -1;
}

#ifdef CONFIG_RISCV_VECTOR_MISALIGNED
void check_vector_unaligned_access_emulated(struct work_struct *work __always_unused)
{
	long *mas_ptr = this_cpu_ptr(&vector_misaligned_access);
	unsigned long tmp_var;

	*mas_ptr = RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;

	kernel_vector_begin();
	/*
	 * In pre-13.0.0 versions of GCC, vector registers cannot appear in
	 * the clobber list. This inline asm clobbers v0, but since we do not
	 * currently build the kernel with V enabled, the v0 clobber arg is not
	 * needed (as the compiler will not emit vector code itself). If the kernel
	 * is changed to build with V enabled, the clobber arg will need to be
	 * added here.
	 */
	__asm__ __volatile__ (
		".balign 4\n\t"
		".option push\n\t"
		".option arch, +zve32x\n\t"
		"       vsetivli zero, 1, e16, m1, ta, ma\n\t"	// Vectors of 16b
		"       vle16.v v0, (%[ptr])\n\t"		// Load bytes
		".option pop\n\t"
		: : [ptr] "r" ((u8 *)&tmp_var + 1));
	kernel_vector_end();
}

bool __init check_vector_unaligned_access_emulated_all_cpus(void)
{
	int cpu;

	schedule_on_each_cpu(check_vector_unaligned_access_emulated);

	for_each_online_cpu(cpu)
		if (per_cpu(vector_misaligned_access, cpu)
		    == RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN)
			return false;

	return true;
}
#else
bool __init check_vector_unaligned_access_emulated_all_cpus(void)
{
	return false;
}
#endif

#ifdef CONFIG_RISCV_SCALAR_MISALIGNED

static bool unaligned_ctl __read_mostly;

void check_unaligned_access_emulated(struct work_struct *work __always_unused)
{
	int cpu = smp_processor_id();
	long *mas_ptr = per_cpu_ptr(&misaligned_access_speed, cpu);
	unsigned long tmp_var, tmp_val;

	*mas_ptr = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;

	__asm__ __volatile__ (
		"       "REG_L" %[tmp], 1(%[ptr])\n"
		: [tmp] "=r" (tmp_val) : [ptr] "r" (&tmp_var) : "memory");

	/*
	 * If unaligned_ctl is already set, this means that we detected that all
	 * CPUS uses emulated misaligned access at boot time. If that changed
	 * when hotplugging the new cpu, this is something we don't handle.
	 */
	if (unlikely(unaligned_ctl && (*mas_ptr != RISCV_HWPROBE_MISALIGNED_SCALAR_EMULATED))) {
		pr_crit("CPU misaligned accesses non homogeneous (expected all emulated)\n");
		while (true)
			cpu_relax();
	}
}

bool __init check_unaligned_access_emulated_all_cpus(void)
{
	int cpu;

	/*
	 * We can only support PR_UNALIGN controls if all CPUs have misaligned
	 * accesses emulated since tasks requesting such control can run on any
	 * CPU.
	 */
	schedule_on_each_cpu(check_unaligned_access_emulated);

	for_each_online_cpu(cpu)
		if (per_cpu(misaligned_access_speed, cpu)
		    != RISCV_HWPROBE_MISALIGNED_SCALAR_EMULATED)
			return false;

	unaligned_ctl = true;
	return true;
}

bool unaligned_ctl_available(void)
{
	return unaligned_ctl;
}
#else
bool __init check_unaligned_access_emulated_all_cpus(void)
{
	return false;
}
#endif
