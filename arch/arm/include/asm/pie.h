/*
 *  arch/arm/include/asm/pie.h
 *
 *  Copyright 2013 Texas Instruments, Inc
 *	Russ Dill <russ.dill@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASMARM_PIE_H
#define _ASMARM_PIE_H

#include <linux/pie.h>

#ifdef CONFIG_PIE
extern void __pie_relocate(void);
extern void __pie___pie_relocate(void);

#define pie_relocate_from_pie()						\
	__asm__ __volatile__("bl __pie_relocate\n"			\
	: : : "cc", "memory", "lr", "r4", "r5", "r6", "r7", "r8", "r9");

static inline void pie_relocate_from_kern(struct pie_chunk *chunk)
{
	void (*fn)(void) = fn_to_pie(chunk, &__pie___pie_relocate);
	__asm__ __volatile__("" : : : "cc", "memory", "r4", "r5", "r6",
				"r7", "r8", "r9");
	fn();
}
#else

#define pie_relocate_from_pie() do {} while(0)

static inline void pie_relocate_from_kern(struct pie_chunk *chunk)
{
}

#endif

#endif
