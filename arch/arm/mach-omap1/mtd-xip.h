/*
 * MTD primitives for XIP support. Architecture specific functions.
 *
 * Do not include this file directly. It's included from linux/mtd/xip.h
 *
 * Author: Vladimir Barinov <vbarinov@embeddedalley.com>
 *
 * (c) 2005 MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is
 * licensed "as is" without any warranty of any kind, whether express or
 * implied.
 */

#ifndef __ARCH_OMAP_MTD_XIP_H__
#define __ARCH_OMAP_MTD_XIP_H__

#include "hardware.h"
#include <linux/soc/ti/omap1-io.h>
#define OMAP_MPU_TIMER_BASE	(0xfffec500)
#define OMAP_MPU_TIMER_OFFSET	0x100

typedef struct {
	u32 cntl;			/* CNTL_TIMER, R/W */
	u32 load_tim;			/* LOAD_TIM,   W */
	u32 read_tim;			/* READ_TIM,   R */
} xip_omap_mpu_timer_regs_t;

#define xip_omap_mpu_timer_base(n)					\
((volatile xip_omap_mpu_timer_regs_t*)OMAP1_IO_ADDRESS(OMAP_MPU_TIMER_BASE +	\
	(n)*OMAP_MPU_TIMER_OFFSET))

static inline unsigned long xip_omap_mpu_timer_read(int nr)
{
	volatile xip_omap_mpu_timer_regs_t* timer = xip_omap_mpu_timer_base(nr);
	return timer->read_tim;
}

#define xip_irqpending()	\
	(omap_readl(OMAP_IH1_ITR) & ~omap_readl(OMAP_IH1_MIR))
#define xip_currtime()		(~xip_omap_mpu_timer_read(0))

/*
 * It's permitted to do approximation for xip_elapsed_since macro
 * (see linux/mtd/xip.h)
 */

#ifdef CONFIG_MACH_OMAP_PERSEUS2
#define xip_elapsed_since(x)	(signed)((~xip_omap_mpu_timer_read(0) - (x)) / 7)
#else
#define xip_elapsed_since(x)	(signed)((~xip_omap_mpu_timer_read(0) - (x)) / 6)
#endif

/*
 * xip_cpu_idle() is used when waiting for a delay equal or larger than
 * the system timer tick period.  This should put the CPU into idle mode
 * to save power and to be woken up only when some interrupts are pending.
 * As above, this should not rely upon standard kernel code.
 */

#define xip_cpu_idle()  asm volatile ("mcr p15, 0, %0, c7, c0, 4" :: "r" (1))

#endif /* __ARCH_OMAP_MTD_XIP_H__ */
