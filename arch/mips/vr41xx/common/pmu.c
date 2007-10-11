/*
 *  pmu.c, Power Management Unit routines for NEC VR4100 series.
 *
 *  Copyright (C) 2003-2007  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/system.h>

#define PMU_TYPE1_BASE	0x0b0000a0UL
#define PMU_TYPE1_SIZE	0x0eUL

#define PMU_TYPE2_BASE	0x0f0000c0UL
#define PMU_TYPE2_SIZE	0x10UL

#define PMUCNT2REG	0x06
 #define SOFTRST	0x0010

static void __iomem *pmu_base;

#define pmu_read(offset)		readw(pmu_base + (offset))
#define pmu_write(offset, value)	writew((value), pmu_base + (offset))

static void vr41xx_cpu_wait(void)
{
	local_irq_disable();
	if (!need_resched())
		/*
		 * "standby" sets IE bit of the CP0_STATUS to 1.
		 */
		__asm__("standby;\n");
	else
		local_irq_enable();
}

static inline void software_reset(void)
{
	uint16_t pmucnt2;

	switch (current_cpu_type()) {
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		pmucnt2 = pmu_read(PMUCNT2REG);
		pmucnt2 |= SOFTRST;
		pmu_write(PMUCNT2REG, pmucnt2);
		break;
	default:
		set_c0_status(ST0_BEV | ST0_ERL);
		change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
		flush_cache_all();
		write_c0_wired(0);
		__asm__("jr     %0"::"r"(0xbfc00000));
		break;
	}
}

static void vr41xx_restart(char *command)
{
	local_irq_disable();
	software_reset();
	while (1) ;
}

static void vr41xx_halt(void)
{
	local_irq_disable();
	printk(KERN_NOTICE "\nYou can turn off the power supply\n");
	__asm__("hibernate;\n");
}

static int __init vr41xx_pmu_init(void)
{
	unsigned long start, size;

	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121:
		start = PMU_TYPE1_BASE;
		size = PMU_TYPE1_SIZE;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		start = PMU_TYPE2_BASE;
		size = PMU_TYPE2_SIZE;
		break;
	default:
		printk("Unexpected CPU of NEC VR4100 series\n");
		return -ENODEV;
	}

	if (request_mem_region(start, size, "PMU") == NULL)
		return -EBUSY;

	pmu_base = ioremap(start, size);
	if (pmu_base == NULL) {
		release_mem_region(start, size);
		return -EBUSY;
	}

	cpu_wait = vr41xx_cpu_wait;
	_machine_restart = vr41xx_restart;
	_machine_halt = vr41xx_halt;
	pm_power_off = vr41xx_halt;

	return 0;
}

core_initcall(vr41xx_pmu_init);
