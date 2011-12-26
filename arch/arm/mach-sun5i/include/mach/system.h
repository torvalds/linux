/*
 * arch/arch/mach-sun5i/include/mach/system.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * core header
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
 
#ifndef __SW_SYSTEM_H
#define __SW_SYSTEM_H


#include <linux/io.h>
#include <asm/proc-fns.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/delay.h>


extern struct sysdev_class sw_sysclass;
extern unsigned long fb_start;
extern unsigned long fb_size;
extern unsigned long g2d_start;
extern unsigned long g2d_size;

static inline void arch_idle(void)
{
	cpu_do_idle();
}


static inline void arch_reset(char mode, const char *cmd)
{
	/* use watch-dog to reset system */
	#define WATCH_DOG_CTRL_REG  (SW_VA_TIMERC_IO_BASE + 0x0094)
	*(volatile unsigned int *)WATCH_DOG_CTRL_REG = 0;
	__delay(100000);
	*(volatile unsigned int *)WATCH_DOG_CTRL_REG = 3;
	while(1);
}

enum sw_ic_ver {
	MAGIC_VER_A = 0,
	MAGIC_VER_B,
	MAGIC_VER_C
};

enum sw_ic_ver sw_get_ic_ver(void);

#endif
