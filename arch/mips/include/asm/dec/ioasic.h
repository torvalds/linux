/*
 *	include/asm-mips/dec/ioasic.h
 *
 *	DEC I/O ASIC access operations.
 *
 *	Copyright (C) 2000, 2002, 2003  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_DEC_IOASIC_H
#define __ASM_DEC_IOASIC_H

#include <linux/spinlock.h>
#include <linux/types.h>

extern spinlock_t ioasic_ssr_lock;

extern volatile u32 *ioasic_base;

static inline void ioasic_write(unsigned int reg, u32 v)
{
	ioasic_base[reg / 4] = v;
}

static inline u32 ioasic_read(unsigned int reg)
{
	return ioasic_base[reg / 4];
}

extern void clear_ioasic_dma_irq(unsigned int irq);

extern void init_ioasic_irqs(int base);

extern int dec_ioasic_clocksource_init(void);

#endif /* __ASM_DEC_IOASIC_H */
