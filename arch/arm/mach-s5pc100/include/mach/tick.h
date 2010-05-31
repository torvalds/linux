/* linux/arch/arm/mach-s5pc100/include/mach/tick.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * S3C64XX - Timer tick support definitions
 *
 * Based on mach-s3c6400/include/mach/tick.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_TICK_H
#define __ASM_ARCH_TICK_H __FILE__

/* note, the timer interrutps turn up in 2 places, the vic and then
 * the timer block. We take the VIC as the base at the moment.
 */
static inline u32 s3c24xx_ostimer_pending(void)
{
	u32 pend = __raw_readl(VA_VIC0 + VIC_RAW_STATUS);
	return pend & (1 << (IRQ_TIMER4_VIC - S5P_IRQ_VIC0(0)));
}

#define TICK_MAX	(0xffffffff)

#endif /* __ASM_ARCH_TICK_H */
