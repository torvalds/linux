/* arch/arm/mach-msm/idle.c
 *
 * Idle processing for MSM7K - work around bugs with SWFI.
 *
 * Copyright (c) 2007 QUALCOMM Incorporated.
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <asm/system.h>

static void msm_idle(void)
{
#ifdef CONFIG_MSM7X00A_IDLE
	asm volatile (

	"mrc     p15, 0, r1, c1, c0, 0    /* read current CR    */ \n\t"
	"bic     r0, r1, #(1 << 2)        /* clear dcache bit   */ \n\t"
	"bic     r0, r0, #(1 << 12)       /* clear icache bit   */ \n\t"
	"mcr     p15, 0, r0, c1, c0, 0    /* disable d/i cache  */ \n\t"

	"mov     r0, #0                   /* prepare wfi value  */ \n\t"
	"mcr     p15, 0, r0, c7, c10, 0   /* flush the cache    */ \n\t"
	"mcr     p15, 0, r0, c7, c10, 4   /* memory barrier     */ \n\t"
	"mcr     p15, 0, r0, c7, c0, 4    /* wait for interrupt */ \n\t"

	"mcr     p15, 0, r1, c1, c0, 0    /* restore d/i cache  */ \n\t"

	: : : "r0","r1" );
#endif
}

static int __init msm_idle_init(void)
{
	arm_pm_idle = msm_idle;
	return 0;
}

arch_initcall(msm_idle_init);
