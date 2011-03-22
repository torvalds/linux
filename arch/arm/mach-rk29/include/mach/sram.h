/*
 * Copyright (C) 2008-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * TCM memory handling for ARM systems
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 */

#ifndef __ARCH_ARM_MACH_RK29_SRAM_H
#define __ARCH_ARM_MACH_RK29_SRAM_H
#ifdef CONFIG_ARCH_RK29

/* Tag variables with this */
#define __sramdata __section(.sram.data)
/* Tag constants with this */
#define __sramconst __section(.sram.rodata)
/* Tag functions inside SRAM called from outside SRAM with this */
#define __sramfunc __attribute__((long_call)) __section(.sram.text) noinline
/* Tag function inside SRAM called from inside SRAM  with this */
#define __sramlocalfunc __section(.sram.text)

void __init rk29_sram_init(void);

static inline unsigned long ddr_save_sp(unsigned long new_sp)
{
	unsigned long old_sp;

	asm volatile ("mov %0, sp" : "=r" (old_sp));
	asm volatile ("mov sp, %0" :: "r" (new_sp));
	return old_sp;
}

// save_sp 必须定义为全局变量
#define DDR_SAVE_SP(save_sp)		do { save_sp = ddr_save_sp((SRAM_DATA_END&(~7))); } while (0)
#define DDR_RESTORE_SP(save_sp)		do { ddr_save_sp(save_sp); } while (0)

#endif
#endif

