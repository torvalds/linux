/*
 * Author: <linux@telechips.com>
 * Created: June 10, 2008
 * Description: LINUX SYSTEM FUNCTIONS for TCC83x
 *
 * Copyright (C) 2008-2009 Telechips
 *
 * Licensed under the terms of the GPL v2.
 *
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>

extern void plat_tcc_reboot(void);

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
	plat_tcc_reboot();
}

#endif
