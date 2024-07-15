/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel interface for the RISCV arch_random_* functions
 *
 * Copyright (c) 2023 Rivos Inc.
 *
 */

#ifndef ASM_RISCV_ARCHRANDOM_H
#define ASM_RISCV_ARCHRANDOM_H

#include <asm/csr.h>
#include <asm/processor.h>

#define SEED_RETRY_LOOPS 100

static inline bool __must_check csr_seed_long(unsigned long *v)
{
	unsigned int retry = SEED_RETRY_LOOPS, valid_seeds = 0;
	const int needed_seeds = sizeof(long) / sizeof(u16);
	u16 *entropy = (u16 *)v;

	do {
		/*
		 * The SEED CSR must be accessed with a read-write instruction.
		 */
		unsigned long csr_seed = csr_swap(CSR_SEED, 0);
		unsigned long opst = csr_seed & SEED_OPST_MASK;

		switch (opst) {
		case SEED_OPST_ES16:
			entropy[valid_seeds++] = csr_seed & SEED_ENTROPY_MASK;
			if (valid_seeds == needed_seeds)
				return true;
			break;

		case SEED_OPST_DEAD:
			pr_err_once("archrandom: Unrecoverable error\n");
			return false;

		case SEED_OPST_BIST:
		case SEED_OPST_WAIT:
		default:
			cpu_relax();
			continue;
		}
	} while (--retry);

	return false;
}

static inline size_t __must_check arch_get_random_longs(unsigned long *v, size_t max_longs)
{
	return 0;
}

static inline size_t __must_check arch_get_random_seed_longs(unsigned long *v, size_t max_longs)
{
	if (!max_longs)
		return 0;

	/*
	 * If Zkr is supported and csr_seed_long succeeds, we return one long
	 * worth of entropy.
	 */
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZKR) && csr_seed_long(v))
		return 1;

	return 0;
}

#endif /* ASM_RISCV_ARCHRANDOM_H */
