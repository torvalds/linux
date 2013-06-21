/*
 *  arch/arm/mach-sun7i/include/mach/system.h
 *
 *  Copyright (C) 2012-2016 Allwinner Limited
 *  liugang (liugang@allwinnertech.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <linux/io.h>
#include <asm/proc-fns.h>
#include <mach/timer.h>
#include <asm/delay.h>
#include <linux/printk.h>

static inline void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks
	 */
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
	pr_err("%s: to check\n", __func__);

	/* use watch-dog to reset system */
	*(volatile unsigned int *)WDOG_MODE_REG = 0;
	__delay(100000);
	*(volatile unsigned int *)WDOG_MODE_REG |= 2;
	while(1) {
		__delay(100);
		*(volatile unsigned int *)WDOG_MODE_REG |= 1;
	}
}

/* to fix, 2013-1-15 */
enum sw_ic_ver {
	MAGIC_VER_NULL      = 0,
	MAGIC_VER_UNKNOWN   = 1,
	MAGIC_VER_A13A      = 0xA13A,
	MAGIC_VER_A13B      = 0xA13B,
	MAGIC_VER_A12A      = 0xA12A,
	MAGIC_VER_A12B      = 0xA12B,
	MAGIC_VER_A10SA     = 0xA10A,
	MAGIC_VER_A10SB     = 0xA10B,
};

struct sw_chip_id
{
	unsigned int sid_rkey0;
	unsigned int sid_rkey1;
	unsigned int sid_rkey2;
	unsigned int sid_rkey3;
};

int sw_get_chip_id(struct sw_chip_id *);
enum sw_ic_ver sw_get_ic_ver(void);

#endif
