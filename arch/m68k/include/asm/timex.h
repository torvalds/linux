/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/asm-m68k/timex.h
 *
 * m68k architecture timex specifications
 */
#ifndef _ASMm68K_TIMEX_H
#define _ASMm68K_TIMEX_H

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

extern unsigned long (*mach_random_get_entropy)(void);

static inline unsigned long random_get_entropy(void)
{
	if (mach_random_get_entropy)
		return mach_random_get_entropy();
	return random_get_entropy_fallback();
}
#define random_get_entropy	random_get_entropy

#endif
