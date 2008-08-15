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
# include "bitops.h"
#endif
#include <linux/types.h>
#include <asm/cpufeature.h>
#include <asm/processor-flags.h>
#include <asm/required-features.h>
#include <asm/msr-index.h>

struct cpu_features {
	int level;		/* Family, or 64 for x86-64 */
	int model;
	u32 flags[NCAPINTS];
};

static struct cpu_features cpu;
static u32 cpu_vendor[3];
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

static int has_fpu(void)
{
	u16 fcw = -1, fsw = -1;
	u32 cr0;

	asm("movl %%cr0,%0" : "=r" (cr0));
	if (cr0 & (X86_CR0_EM|X86_CR0_TS)) {
		cr0 &= ~(X86_CR0_EM|X86_CR0_TS);
		asm volatile("movl %0,%%cr0" : : "r" (cr0));
	}

	asm volatile("fninit ; fnstsw %0 ; fnstcw %1"
		     : "+m" (fsw), "+m" (fcw));

	return fsw == 0 && (fcw & 0x103f) == 0x003f;
}

static int has_eflag(u32 mask)
{
	u32 f0, f1;

	asm("pushfl ; "
	    "pushfl ; "
	    "popl %0 ; "
	    "movl %0,%1 ; "
	    "xorl %2,%1 ; "
	    "pushl %1 ; "
	    "popfl ; "
	    "pushfl ; "
	    "popl %1 ; "
	    "popfl"
	    : "=&r" (f0), "=&r" (f1)
	    : "ri" (mask));

	return !!((f0^f1) & mask);
}

static void get_flags(void)
{
	u32 max_intel_level, max_amd_level;
	u32 tfms;

	if (has_fpu())
		set_bit(X86_FEATURE_FPU, cpu.flags);

	if (has_eflag(X86_EFLAGS_ID)) {
		asm("cpuid"
		    : "=a" (max_intel_level),
		      "=b" (cpu_vendor[0]),
		      "=d" (cpu_vendor[1]),
		      "=c" (cpu_vendor[2])
		    : "a" (0));

		if (max_intel_level >= 0x00000001 &&
		    max_intel_level <= 0x0000ffff) {
			asm("cpuid"
			    : "=a" (tfms),
			      "=c" (cpu.flags[4]),
			      "=d" (cpu.flags[0])
			    : "a" (0x00000001)
			    : "ebx");
			cpu.level = (tfms >> 8) & 15;
			cpu.model = (tfms >> 4) & 15;
			if (cpu.level >= 6)
				cpu.model += ((tfms >> 16) & 0xf) << 4;
		}

		asm("cpuid"
		    : "=a" (max_amd_level)
		    : "a" (0x80000000)
		    : "ebx", "ecx", "edx");

		if (max_amd_level >= 0x80000001 &&
		    max_amd_level <= 0x8000ffff) {
			u32 eax = 0x80000001;
			asm("cpuid"
			    : "+a" (eax),
			      "=c" (cpu.flags[6]),
			      "=d" (cpu.flags[1])
			    : : "ebx");
		}
	}
}

/* Returns a bitmask of which words we have error bits in */
static int check_flags(void)
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

	get_flags();
	err = check_flags();

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

		get_flags();	/* Make sure it really did something */
		err = check_flags();
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
		err = check_flags();
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

		err = check_flags();
	}

	if (err_flags_ptr)
		*err_flags_ptr = err ? err_flags : NULL;
	if (cpu_level_ptr)
		*cpu_level_ptr = cpu.level;
	if (req_level_ptr)
		*req_level_ptr = req_level;

	return (cpu.level < req_level || err) ? -1 : 0;
}
