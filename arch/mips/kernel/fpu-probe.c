// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Processor capabilities determination functions.
 *
 * Copyright (C) xxxx  the Anonymous
 * Copyright (C) 1994 - 2006 Ralf Baechle
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 * Copyright (C) 2001, 2004, 2011, 2012	 MIPS Technologies, Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/cpu-type.h>
#include <asm/elf.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>

#include "fpu-probe.h"

/*
 * Get the FPU Implementation/Revision.
 */
static inline unsigned long cpu_get_fpu_id(void)
{
	unsigned long tmp, fpu_id;

	tmp = read_c0_status();
	__enable_fpu(FPU_AS_IS);
	fpu_id = read_32bit_cp1_register(CP1_REVISION);
	write_c0_status(tmp);
	return fpu_id;
}

/*
 * Check if the CPU has an external FPU.
 */
int __cpu_has_fpu(void)
{
	return (cpu_get_fpu_id() & FPIR_IMP_MASK) != FPIR_IMP_NONE;
}

/*
 * Determine the FCSR mask for FPU hardware.
 */
static inline void cpu_set_fpu_fcsr_mask(struct cpuinfo_mips *c)
{
	unsigned long sr, mask, fcsr, fcsr0, fcsr1;

	fcsr = c->fpu_csr31;
	mask = FPU_CSR_ALL_X | FPU_CSR_ALL_E | FPU_CSR_ALL_S | FPU_CSR_RM;

	sr = read_c0_status();
	__enable_fpu(FPU_AS_IS);

	fcsr0 = fcsr & mask;
	write_32bit_cp1_register(CP1_STATUS, fcsr0);
	fcsr0 = read_32bit_cp1_register(CP1_STATUS);

	fcsr1 = fcsr | ~mask;
	write_32bit_cp1_register(CP1_STATUS, fcsr1);
	fcsr1 = read_32bit_cp1_register(CP1_STATUS);

	write_32bit_cp1_register(CP1_STATUS, fcsr);

	write_c0_status(sr);

	c->fpu_msk31 = ~(fcsr0 ^ fcsr1) & ~mask;
}

/*
 * Determine the IEEE 754 NaN encodings and ABS.fmt/NEG.fmt execution modes
 * supported by FPU hardware.
 */
static void cpu_set_fpu_2008(struct cpuinfo_mips *c)
{
	if (c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1 |
			    MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
			    MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
			    MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6)) {
		unsigned long sr, fir, fcsr, fcsr0, fcsr1;

		sr = read_c0_status();
		__enable_fpu(FPU_AS_IS);

		fir = read_32bit_cp1_register(CP1_REVISION);
		if (fir & MIPS_FPIR_HAS2008) {
			fcsr = read_32bit_cp1_register(CP1_STATUS);

			/*
			 * MAC2008 toolchain never landed in real world, so
			 * we're only testing whether it can be disabled and
			 *  don't try to enabled it.
			 */
			fcsr0 = fcsr & ~(FPU_CSR_ABS2008 | FPU_CSR_NAN2008 |
					 FPU_CSR_MAC2008);
			write_32bit_cp1_register(CP1_STATUS, fcsr0);
			fcsr0 = read_32bit_cp1_register(CP1_STATUS);

			fcsr1 = fcsr | FPU_CSR_ABS2008 | FPU_CSR_NAN2008;
			write_32bit_cp1_register(CP1_STATUS, fcsr1);
			fcsr1 = read_32bit_cp1_register(CP1_STATUS);

			write_32bit_cp1_register(CP1_STATUS, fcsr);

			if (c->isa_level & (MIPS_CPU_ISA_M32R2 |
					    MIPS_CPU_ISA_M64R2)) {
				/*
				 * The bit for MAC2008 might be reused by R6
				 * in future, so we only test for R2-R5.
				 */
				if (fcsr0 & FPU_CSR_MAC2008)
					c->options |= MIPS_CPU_MAC_2008_ONLY;
			}

			if (!(fcsr0 & FPU_CSR_NAN2008))
				c->options |= MIPS_CPU_NAN_LEGACY;
			if (fcsr1 & FPU_CSR_NAN2008)
				c->options |= MIPS_CPU_NAN_2008;

			if ((fcsr0 ^ fcsr1) & FPU_CSR_ABS2008)
				c->fpu_msk31 &= ~FPU_CSR_ABS2008;
			else
				c->fpu_csr31 |= fcsr & FPU_CSR_ABS2008;

			if ((fcsr0 ^ fcsr1) & FPU_CSR_NAN2008)
				c->fpu_msk31 &= ~FPU_CSR_NAN2008;
			else
				c->fpu_csr31 |= fcsr & FPU_CSR_NAN2008;
		} else {
			c->options |= MIPS_CPU_NAN_LEGACY;
		}

		write_c0_status(sr);
	} else {
		c->options |= MIPS_CPU_NAN_LEGACY;
	}
}

/*
 * IEEE 754 conformance mode to use.  Affects the NaN encoding and the
 * ABS.fmt/NEG.fmt execution mode.
 */
static enum { STRICT, LEGACY, STD2008, RELAXED } ieee754 = STRICT;

/*
 * Set the IEEE 754 NaN encodings and the ABS.fmt/NEG.fmt execution modes
 * to support by the FPU emulator according to the IEEE 754 conformance
 * mode selected.  Note that "relaxed" straps the emulator so that it
 * allows 2008-NaN binaries even for legacy processors.
 */
static void cpu_set_nofpu_2008(struct cpuinfo_mips *c)
{
	c->options &= ~(MIPS_CPU_NAN_2008 | MIPS_CPU_NAN_LEGACY);
	c->fpu_csr31 &= ~(FPU_CSR_ABS2008 | FPU_CSR_NAN2008);
	c->fpu_msk31 &= ~(FPU_CSR_ABS2008 | FPU_CSR_NAN2008);

	switch (ieee754) {
	case STRICT:
		if (c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1 |
				    MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
				    MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
				    MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6)) {
			c->options |= MIPS_CPU_NAN_2008 | MIPS_CPU_NAN_LEGACY;
		} else {
			c->options |= MIPS_CPU_NAN_LEGACY;
			c->fpu_msk31 |= FPU_CSR_ABS2008 | FPU_CSR_NAN2008;
		}
		break;
	case LEGACY:
		c->options |= MIPS_CPU_NAN_LEGACY;
		c->fpu_msk31 |= FPU_CSR_ABS2008 | FPU_CSR_NAN2008;
		break;
	case STD2008:
		c->options |= MIPS_CPU_NAN_2008;
		c->fpu_csr31 |= FPU_CSR_ABS2008 | FPU_CSR_NAN2008;
		c->fpu_msk31 |= FPU_CSR_ABS2008 | FPU_CSR_NAN2008;
		break;
	case RELAXED:
		c->options |= MIPS_CPU_NAN_2008 | MIPS_CPU_NAN_LEGACY;
		break;
	}
}

/*
 * Override the IEEE 754 NaN encoding and ABS.fmt/NEG.fmt execution mode
 * according to the "ieee754=" parameter.
 */
static void cpu_set_nan_2008(struct cpuinfo_mips *c)
{
	switch (ieee754) {
	case STRICT:
		mips_use_nan_legacy = !!cpu_has_nan_legacy;
		mips_use_nan_2008 = !!cpu_has_nan_2008;
		break;
	case LEGACY:
		mips_use_nan_legacy = !!cpu_has_nan_legacy;
		mips_use_nan_2008 = !cpu_has_nan_legacy;
		break;
	case STD2008:
		mips_use_nan_legacy = !cpu_has_nan_2008;
		mips_use_nan_2008 = !!cpu_has_nan_2008;
		break;
	case RELAXED:
		mips_use_nan_legacy = true;
		mips_use_nan_2008 = true;
		break;
	}
}

/*
 * IEEE 754 NaN encoding and ABS.fmt/NEG.fmt execution mode override
 * settings:
 *
 * strict:  accept binaries that request a NaN encoding supported by the FPU
 * legacy:  only accept legacy-NaN binaries
 * 2008:    only accept 2008-NaN binaries
 * relaxed: accept any binaries regardless of whether supported by the FPU
 */
static int __init ieee754_setup(char *s)
{
	if (!s)
		return -1;
	else if (!strcmp(s, "strict"))
		ieee754 = STRICT;
	else if (!strcmp(s, "legacy"))
		ieee754 = LEGACY;
	else if (!strcmp(s, "2008"))
		ieee754 = STD2008;
	else if (!strcmp(s, "relaxed"))
		ieee754 = RELAXED;
	else
		return -1;

	if (!(boot_cpu_data.options & MIPS_CPU_FPU))
		cpu_set_nofpu_2008(&boot_cpu_data);
	cpu_set_nan_2008(&boot_cpu_data);

	return 0;
}

early_param("ieee754", ieee754_setup);

/*
 * Set the FIR feature flags for the FPU emulator.
 */
static void cpu_set_nofpu_id(struct cpuinfo_mips *c)
{
	u32 value;

	value = 0;
	if (c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1 |
			    MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
			    MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
			    MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6))
		value |= MIPS_FPIR_D | MIPS_FPIR_S;
	if (c->isa_level & (MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
			    MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
			    MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6))
		value |= MIPS_FPIR_F64 | MIPS_FPIR_L | MIPS_FPIR_W;
	if (c->options & MIPS_CPU_NAN_2008)
		value |= MIPS_FPIR_HAS2008;
	c->fpu_id = value;
}

/* Determined FPU emulator mask to use for the boot CPU with "nofpu".  */
static unsigned int mips_nofpu_msk31;

/*
 * Set options for FPU hardware.
 */
void cpu_set_fpu_opts(struct cpuinfo_mips *c)
{
	c->fpu_id = cpu_get_fpu_id();
	mips_nofpu_msk31 = c->fpu_msk31;

	if (c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1 |
			    MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
			    MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
			    MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6)) {
		if (c->fpu_id & MIPS_FPIR_3D)
			c->ases |= MIPS_ASE_MIPS3D;
		if (c->fpu_id & MIPS_FPIR_UFRP)
			c->options |= MIPS_CPU_UFR;
		if (c->fpu_id & MIPS_FPIR_FREP)
			c->options |= MIPS_CPU_FRE;
	}

	cpu_set_fpu_fcsr_mask(c);
	cpu_set_fpu_2008(c);
	cpu_set_nan_2008(c);
}

/*
 * Set options for the FPU emulator.
 */
void cpu_set_nofpu_opts(struct cpuinfo_mips *c)
{
	c->options &= ~MIPS_CPU_FPU;
	c->fpu_msk31 = mips_nofpu_msk31;

	cpu_set_nofpu_2008(c);
	cpu_set_nan_2008(c);
	cpu_set_nofpu_id(c);
}

int mips_fpu_disabled;

static int __init fpu_disable(char *s)
{
	cpu_set_nofpu_opts(&boot_cpu_data);
	mips_fpu_disabled = 1;

	return 1;
}

__setup("nofpu", fpu_disable);

