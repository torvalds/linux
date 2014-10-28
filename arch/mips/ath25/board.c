/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 FON Technology, SL.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006-2009 Felix Fietkau <nbd@openwrt.org>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/irq_cpu.h>
#include <asm/reboot.h>
#include <asm/bootinfo.h>
#include <asm/time.h>

static void ath25_halt(void)
{
	local_irq_disable();
	unreachable();
}

void __init plat_mem_setup(void)
{
	_machine_halt = ath25_halt;
	pm_power_off = ath25_halt;

	/* Disable data watchpoints */
	write_c0_watchlo0(0);
}

asmlinkage void plat_irq_dispatch(void)
{
}

void __init plat_time_init(void)
{
}

unsigned int __cpuinit get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

void __init arch_init_irq(void)
{
	clear_c0_status(ST0_IM);
	mips_cpu_irq_init();
}
