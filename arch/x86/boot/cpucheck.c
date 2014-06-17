/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Check for obligatory CPU features and abort if the features are not
 * present.  This code should be compilable as 16-, 32- or 64-bit
 * code, so be very careful with types and inline assembly.
 *
 * This code should not contain any messages; that requires an
 * additional wrapper.
 *
 * As written, this code is not safe for inclusion into the kernel
 * proper (after FPU initialization, in particular).
 */

#ifdef _SETUP
# include "boot.h"
#endif
#include <linux/types.h>
#include <asm/processor-flags.h>
#include <asm/required-features.h>
#include <asm/msr-index.h>
#include "string.h"

static u32 err_flags[NCAPINTS];

static const int req_level = CONFIG_X86_MINIMUM_CPU_FAMILY;

static const u32 req_flags[NCAPINTS] =
{
	REQUIRED_MASK0,
	REQUIRED_MASK1,
	0, /* REQUIRED_MASK2 not implemented in this file */
	0, /* REQUIRED_MASK3 not implemented in this file */
	REQUIRED_MASK4,
	0, /* REQUIRED_MASK5 not implemented in this file */
	REQUIRED_MASK6,
	0, /* REQUIRED_MASK7 not implemented in this file */
};

#define A32(a, b, c, d) (((d) << 24)+((c) << 16)+((b) << 8)+(a))

static int is_amd(void)
{
	return cpu_vendor[0] == A32('A', 'u', 't', 'h') &&
	       cpu_vendor[1] == A32('e', 'n', 't', 'i') &&
	       cpu_vendor[2] == A32('c', 'A', 'M', 'D');
}

static int is_centaur(void)
{
	return cpu_vendor[0] == A32('C', 'e', 'n', 't') &&
	       cpu_vendor[1] == A32('a', 'u', 'r', 'H') &&
	       cpu_vendor[2] == A32('a', 'u', 'l', 's');
}

static int is_transmeta(void)
{
	return cpu_vendor[0] == A32('G', 'e', 'n', 'u') &&
	       cpu_vendor[1] == A32('i', 'n', 'e', 'T') &&
	       cpu_vendor[2] == A32('M', 'x', '8', '6');
}

static int is_intel(void)
{
	return cpu_vendor[0] == A32('G', 'e', 'n', 'u') &&
	       cpu_vendor[1] == A32('i', 'n', 'e', 'I') &&
	       cpu_vendor[2] == A32('n', 't', 'e', 'l');
}

/* Returns a bitmask of which words we have error bits in */
static int check_cpuflags(void)
{
	u32 err;
	int i;

	err = 0;
	for (i = 0; i < NCAPINTS; i++) {
		err_flags[i] = req_flags[i] & ~cpu.flags[i];
		if (err_flags[i])
			err |= 1 << i;
	}

	return err;
}

/*
 * Returns -1 on error.
 *
 * *cpu_level is set to the current CPU level; *req_level to the required
 * level.  x86-64 is considered level 64 for this purpose.
 *
 * *err_flags_ptr is set to the flags error array if there are flags missing.
 */
int check_cpu(int *cpu_level_ptr, int *req_level_ptr, u32 **err_flags_ptr)
{
	int err;

	memset(&cpu.flags, 0, sizeof cpu.flags);
	cpu.level = 3;

	if (has_eflag(X86_EFLAGS_AC))
		cpu.level = 4;

	get_cpuflags();
	err = check_cpuflags();

	if (test_bit(X86_FEATURE_LM, cpu.flags))
		cpu.level = 64;

	if (err == 0x01 &&
	    !(err_flags[0] &
	      ~((1 << X86_FEATURE_XMM)|(1 << X86_FEATURE_XMM2))) &&
	    is_amd()) {
		/* If this is an AMD and we're only missing SSE+SSE2, try to
		   turn them on */

		u32 ecx = MSR_K7_HWCR;
		u32 eax, edx;

		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
		eax &= ~(1 << 15);
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		get_cpuflags();	/* Make sure it really did something */
		err = check_cpuflags();
	} else if (err == 0x01 &&
		   !(err_flags[0] & ~(1 << X86_FEATURE_CX8)) &&
		   is_centaur() && cpu.model >= 6) {
		/* If this is a VIA C3, we might have to enable CX8
		   explicitly */

		u32 ecx = MSR_VIA_FCR;
		u32 eax, edx;

		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
		eax |= (1<<1)|(1<<7);
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		set_bit(X86_FEATURE_CX8, cpu.flags);
		err = check_cpuflags();
	} else if (err == 0x01 && is_transmeta()) {
		/* Transmeta might have masked feature bits in word 0 */

		u32 ecx = 0x80860004;
		u32 eax, edx;
		u32 level = 1;

		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
		asm("wrmsr" : : "a" (~0), "d" (edx), "c" (ecx));
		asm("cpuid"
		    : "+a" (level), "=d" (cpu.flags[0])
		    : : "ecx", "ebx");
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		err = check_cpuflags();
	} else if (err == 0x01 &&
		   !(err_flags[0] & ~(1 << X86_FEATURE_PAE)) &&
		   is_intel() && cpu.level == 6 &&
		   (cpu.model == 9 || cpu.model == 13)) {
		/* PAE is disabled on this Pentium M but can be forced */
		if (cmdline_find_option_bool("forcepae")) {
			puts("WARNING: Forcing PAE in CPU flags\n");
			set_bit(X86_FEATURE_PAE, cpu.flags);
			err = check_cpuflags();
		}
		else {
			puts("WARNING: PAE disabled. Use parameter 'forcepae' to enable at your own risk!\n");
		}
	}

	if (err_flags_ptr)
		*err_flags_ptr = err ? err_flags : NULL;
	if (cpu_level_ptr)
		*cpu_level_ptr = cpu.level;
	if (req_level_ptr)
		*req_level_ptr = req_level;

	return (cpu.level < req_level || err) ? -1 : 0;
}
