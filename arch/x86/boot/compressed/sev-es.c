// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

/*
 * misc.h needs to be first because it knows how to include the other kernel
 * headers in the pre-decompression code in a way that does not break
 * compilation.
 */
#include "misc.h"

#include <asm/sev-es.h>
#include <asm/msr-index.h>
#include <asm/ptrace.h>
#include <asm/svm.h>

static inline u64 sev_es_rd_ghcb_msr(void)
{
	unsigned long low, high;

	asm volatile("rdmsr" : "=a" (low), "=d" (high) :
			"c" (MSR_AMD64_SEV_ES_GHCB));

	return ((high << 32) | low);
}

static inline void sev_es_wr_ghcb_msr(u64 val)
{
	u32 low, high;

	low  = val & 0xffffffffUL;
	high = val >> 32;

	asm volatile("wrmsr" : : "c" (MSR_AMD64_SEV_ES_GHCB),
			"a"(low), "d" (high) : "memory");
}

#undef __init
#define __init

/* Include code for early handlers */
#include "../../kernel/sev-es-shared.c"
