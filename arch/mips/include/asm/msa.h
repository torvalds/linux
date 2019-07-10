/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */
#ifndef _ASM_MSA_H
#define _ASM_MSA_H

#include <asm/mipsregs.h>

#ifndef __ASSEMBLY__

#include <asm/inst.h>

extern void _save_msa(struct task_struct *);
extern void _restore_msa(struct task_struct *);
extern void _init_msa_upper(void);

extern void read_msa_wr_b(unsigned idx, union fpureg *to);
extern void read_msa_wr_h(unsigned idx, union fpureg *to);
extern void read_msa_wr_w(unsigned idx, union fpureg *to);
extern void read_msa_wr_d(unsigned idx, union fpureg *to);

/**
 * read_msa_wr() - Read a single MSA vector register
 * @idx:	The index of the vector register to read
 * @to:		The FPU register union to store the registers value in
 * @fmt:	The format of the data in the vector register
 *
 * Read the value of MSA vector register idx into the FPU register
 * union to, using the format fmt.
 */
static inline void read_msa_wr(unsigned idx, union fpureg *to,
			       enum msa_2b_fmt fmt)
{
	switch (fmt) {
	case msa_fmt_b:
		read_msa_wr_b(idx, to);
		break;

	case msa_fmt_h:
		read_msa_wr_h(idx, to);
		break;

	case msa_fmt_w:
		read_msa_wr_w(idx, to);
		break;

	case msa_fmt_d:
		read_msa_wr_d(idx, to);
		break;

	default:
		BUG();
	}
}

extern void write_msa_wr_b(unsigned idx, union fpureg *from);
extern void write_msa_wr_h(unsigned idx, union fpureg *from);
extern void write_msa_wr_w(unsigned idx, union fpureg *from);
extern void write_msa_wr_d(unsigned idx, union fpureg *from);

/**
 * write_msa_wr() - Write a single MSA vector register
 * @idx:	The index of the vector register to write
 * @from:	The FPU register union to take the registers value from
 * @fmt:	The format of the data in the vector register
 *
 * Write the value from the FPU register union from into MSA vector
 * register idx, using the format fmt.
 */
static inline void write_msa_wr(unsigned idx, union fpureg *from,
				enum msa_2b_fmt fmt)
{
	switch (fmt) {
	case msa_fmt_b:
		write_msa_wr_b(idx, from);
		break;

	case msa_fmt_h:
		write_msa_wr_h(idx, from);
		break;

	case msa_fmt_w:
		write_msa_wr_w(idx, from);
		break;

	case msa_fmt_d:
		write_msa_wr_d(idx, from);
		break;

	default:
		BUG();
	}
}

static inline void enable_msa(void)
{
	if (cpu_has_msa) {
		set_c0_config5(MIPS_CONF5_MSAEN);
		enable_fpu_hazard();
	}
}

static inline void disable_msa(void)
{
	if (cpu_has_msa) {
		clear_c0_config5(MIPS_CONF5_MSAEN);
		disable_fpu_hazard();
	}
}

static inline int is_msa_enabled(void)
{
	if (!cpu_has_msa)
		return 0;

	return read_c0_config5() & MIPS_CONF5_MSAEN;
}

static inline int thread_msa_context_live(void)
{
	/*
	 * Check cpu_has_msa only if it's a constant. This will allow the
	 * compiler to optimise out code for CPUs without MSA without adding
	 * an extra redundant check for CPUs with MSA.
	 */
	if (__builtin_constant_p(cpu_has_msa) && !cpu_has_msa)
		return 0;

	return test_thread_flag(TIF_MSA_CTX_LIVE);
}

static inline void save_msa(struct task_struct *t)
{
	if (cpu_has_msa)
		_save_msa(t);
}

static inline void restore_msa(struct task_struct *t)
{
	if (cpu_has_msa)
		_restore_msa(t);
}

static inline void init_msa_upper(void)
{
	/*
	 * Check cpu_has_msa only if it's a constant. This will allow the
	 * compiler to optimise out code for CPUs without MSA without adding
	 * an extra redundant check for CPUs with MSA.
	 */
	if (__builtin_constant_p(cpu_has_msa) && !cpu_has_msa)
		return;

	_init_msa_upper();
}

#ifndef TOOLCHAIN_SUPPORTS_MSA
/*
 * Define assembler macros using .word for the c[ft]cmsa instructions in order
 * to allow compilation with toolchains that do not support MSA. Once all
 * toolchains in use support MSA these can be removed.
 */
_ASM_MACRO_2R(cfcmsa, rd, cs,
	_ASM_INSN_IF_MIPS(0x787e0019 | __cs << 11 | __rd << 6)
	_ASM_INSN32_IF_MM(0x587e0016 | __cs << 11 | __rd << 6));
_ASM_MACRO_2R(ctcmsa, cd, rs,
	_ASM_INSN_IF_MIPS(0x783e0019 | __rs << 11 | __cd << 6)
	_ASM_INSN32_IF_MM(0x583e0016 | __rs << 11 | __cd << 6));
#define _ASM_SET_MSA ""
#else /* TOOLCHAIN_SUPPORTS_MSA */
#define _ASM_SET_MSA ".set\tfp=64\n\t"				\
		     ".set\tmsa\n\t"
#endif

#define __BUILD_MSA_CTL_REG(name, cs)				\
static inline unsigned int read_msa_##name(void)		\
{								\
	unsigned int reg;					\
	__asm__ __volatile__(					\
	"	.set	push\n"					\
	_ASM_SET_MSA						\
	"	cfcmsa	%0, $" #cs "\n"				\
	"	.set	pop\n"					\
	: "=r"(reg));						\
	return reg;						\
}								\
								\
static inline void write_msa_##name(unsigned int val)		\
{								\
	__asm__ __volatile__(					\
	"	.set	push\n"					\
	_ASM_SET_MSA						\
	"	ctcmsa	$" #cs ", %0\n"				\
	"	.set	pop\n"					\
	: : "r"(val));						\
}

__BUILD_MSA_CTL_REG(ir, 0)
__BUILD_MSA_CTL_REG(csr, 1)
__BUILD_MSA_CTL_REG(access, 2)
__BUILD_MSA_CTL_REG(save, 3)
__BUILD_MSA_CTL_REG(modify, 4)
__BUILD_MSA_CTL_REG(request, 5)
__BUILD_MSA_CTL_REG(map, 6)
__BUILD_MSA_CTL_REG(unmap, 7)

#endif /* !__ASSEMBLY__ */

#define MSA_IR		0
#define MSA_CSR		1
#define MSA_ACCESS	2
#define MSA_SAVE	3
#define MSA_MODIFY	4
#define MSA_REQUEST	5
#define MSA_MAP		6
#define MSA_UNMAP	7

/* MSA Implementation Register (MSAIR) */
#define MSA_IR_REVB		0
#define MSA_IR_REVF		(_ULCAST_(0xff) << MSA_IR_REVB)
#define MSA_IR_PROCB		8
#define MSA_IR_PROCF		(_ULCAST_(0xff) << MSA_IR_PROCB)
#define MSA_IR_WRPB		16
#define MSA_IR_WRPF		(_ULCAST_(0x1) << MSA_IR_WRPB)

/* MSA Control & Status Register (MSACSR) */
#define MSA_CSR_RMB		0
#define MSA_CSR_RMF		(_ULCAST_(0x3) << MSA_CSR_RMB)
#define MSA_CSR_RM_NEAREST	0
#define MSA_CSR_RM_TO_ZERO	1
#define MSA_CSR_RM_TO_POS	2
#define MSA_CSR_RM_TO_NEG	3
#define MSA_CSR_FLAGSB		2
#define MSA_CSR_FLAGSF		(_ULCAST_(0x1f) << MSA_CSR_FLAGSB)
#define MSA_CSR_FLAGS_IB	2
#define MSA_CSR_FLAGS_IF	(_ULCAST_(0x1) << MSA_CSR_FLAGS_IB)
#define MSA_CSR_FLAGS_UB	3
#define MSA_CSR_FLAGS_UF	(_ULCAST_(0x1) << MSA_CSR_FLAGS_UB)
#define MSA_CSR_FLAGS_OB	4
#define MSA_CSR_FLAGS_OF	(_ULCAST_(0x1) << MSA_CSR_FLAGS_OB)
#define MSA_CSR_FLAGS_ZB	5
#define MSA_CSR_FLAGS_ZF	(_ULCAST_(0x1) << MSA_CSR_FLAGS_ZB)
#define MSA_CSR_FLAGS_VB	6
#define MSA_CSR_FLAGS_VF	(_ULCAST_(0x1) << MSA_CSR_FLAGS_VB)
#define MSA_CSR_ENABLESB	7
#define MSA_CSR_ENABLESF	(_ULCAST_(0x1f) << MSA_CSR_ENABLESB)
#define MSA_CSR_ENABLES_IB	7
#define MSA_CSR_ENABLES_IF	(_ULCAST_(0x1) << MSA_CSR_ENABLES_IB)
#define MSA_CSR_ENABLES_UB	8
#define MSA_CSR_ENABLES_UF	(_ULCAST_(0x1) << MSA_CSR_ENABLES_UB)
#define MSA_CSR_ENABLES_OB	9
#define MSA_CSR_ENABLES_OF	(_ULCAST_(0x1) << MSA_CSR_ENABLES_OB)
#define MSA_CSR_ENABLES_ZB	10
#define MSA_CSR_ENABLES_ZF	(_ULCAST_(0x1) << MSA_CSR_ENABLES_ZB)
#define MSA_CSR_ENABLES_VB	11
#define MSA_CSR_ENABLES_VF	(_ULCAST_(0x1) << MSA_CSR_ENABLES_VB)
#define MSA_CSR_CAUSEB		12
#define MSA_CSR_CAUSEF		(_ULCAST_(0x3f) << MSA_CSR_CAUSEB)
#define MSA_CSR_CAUSE_IB	12
#define MSA_CSR_CAUSE_IF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_IB)
#define MSA_CSR_CAUSE_UB	13
#define MSA_CSR_CAUSE_UF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_UB)
#define MSA_CSR_CAUSE_OB	14
#define MSA_CSR_CAUSE_OF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_OB)
#define MSA_CSR_CAUSE_ZB	15
#define MSA_CSR_CAUSE_ZF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_ZB)
#define MSA_CSR_CAUSE_VB	16
#define MSA_CSR_CAUSE_VF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_VB)
#define MSA_CSR_CAUSE_EB	17
#define MSA_CSR_CAUSE_EF	(_ULCAST_(0x1) << MSA_CSR_CAUSE_EB)
#define MSA_CSR_NXB		18
#define MSA_CSR_NXF		(_ULCAST_(0x1) << MSA_CSR_NXB)
#define MSA_CSR_FSB		24
#define MSA_CSR_FSF		(_ULCAST_(0x1) << MSA_CSR_FSB)

#endif /* _ASM_MSA_H */
