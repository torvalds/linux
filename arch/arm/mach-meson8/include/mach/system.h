/*
 *  arch/arm/mach-meson/include/mach/system.h
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
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
#include <plat/io.h>
#include <mach/hardware.h>
#include <mach/register.h>
#include <plat/cpu.h>

static inline void arch_idle(void)
{
    /*
     * This should do all the clock switching
     * and wait for interrupt tricks
     */
    cpu_do_idle();
}
#define  DUAL_CORE_RESET		  (3<<24)
static inline void arch_reset(char mode, const char *cmd)
{
    WRITE_MPEG_REG(VENC_VDAC_SETTING, 0xf);
    WRITE_MPEG_REG(WATCHDOG_RESET, 0);
	if (IS_MESON_M8_CPU)
		WRITE_MPEG_REG(WATCHDOG_TC, DUAL_CORE_RESET|(1<<22) | 100);
	else if (IS_MESON_M8M2_CPU)
		WRITE_MPEG_REG(WATCHDOG_TC, DUAL_CORE_RESET|(1<<19) | 100);
    while(1)
        arch_idle();
}

#endif
