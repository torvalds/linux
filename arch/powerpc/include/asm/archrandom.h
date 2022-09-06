/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_ARCHRANDOM_H
#define _ASM_POWERPC_ARCHRANDOM_H

static inline size_t __must_check arch_get_random_longs(unsigned long *v, size_t max_longs)
{
	return 0;
}

size_t __must_check arch_get_random_seed_longs(unsigned long *v, size_t max_longs);

#ifdef CONFIG_PPC_POWERNV
int pnv_get_random_long(unsigned long *v);
#endif

#endif /* _ASM_POWERPC_ARCHRANDOM_H */
