/*
 *  pmu.c, Power Management Unit routines for NEC VR4100 series.
 *
 *  Copyright (C) 2003-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/system.h>

#define PMUCNT2REG	KSEG1ADDR(0x0f0000c6)
 #define SOFTRST	0x0010

static inline void software_reset(void)
{
	uint16_t val;

	switch (current_cpu_data.cputype) {
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		val = readw(PMUCNT2REG);
		val |= SOFTRST;
		writew(val, PMUCNT2REG);
		break;
	default:
		break;
	}
}

static void vr41xx_restart(char *command)
{
	local_irq_disable();
	software_reset();
	printk(KERN_NOTICE "\nYou can reset your system\n");
	while (1) ;
}

static void vr41xx_halt(void)
{
	local_irq_disable();
	printk(KERN_NOTICE "\nYou can turn off the power supply\n");
	while (1) ;
}

static void vr41xx_power_off(void)
{
	local_irq_disable();
	printk(KERN_NOTICE "\nYou can turn off the power supply\n");
	while (1) ;
}

static int __init vr41xx_pmu_init(void)
{
	_machine_restart = vr41xx_restart;
	_machine_halt = vr41xx_halt;
	_machine_power_off = vr41xx_power_off;

	return 0;
}

early_initcall(vr41xx_pmu_init);
