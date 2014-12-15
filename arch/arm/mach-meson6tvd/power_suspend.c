/*
 * arch/arm/mach-meson6tv/power_suspend.c
 *
 * Copyright (C) 2011-2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <asm/memory.h>
#include <asm/cacheflush.h>

#include <plat/io.h>


//appf functions
#define APPF_INITIALIZE             0
#define APPF_POWER_DOWN_CPU         1
#define APPF_POWER_UP_CPUS          2
//appf flags
#define APPF_SAVE_PMU          (1<<0)
#define APPF_SAVE_TIMERS       (1<<1)
#define APPF_SAVE_VFP          (1<<2)
#define APPF_SAVE_DEBUG        (1<<3)
#define APPF_SAVE_L2           (1<<4)

#ifdef CONFIG_HARDWARE_WATCHDOG
void disable_watchdog(void)
{
	printk(KERN_INFO "** disable watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);
	aml_clr_reg32_mask(P_WATCHDOG_TC,(1 << WATCHDOG_ENABLE_BIT));
}
void enable_watchdog(void)
{
	printk(KERN_INFO "** enable watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);
	aml_write_reg32(P_WATCHDOG_TC, 1 << WATCHDOG_ENABLE_BIT | 0x1FFFFF);//about 20sec

	aml_write_reg32(P_AO_RTI_STATUS_REG1, MESON_NORMAL_BOOT);
}
void reset_watchdog(void)
{
	//printk(KERN_INFO "** reset watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);
}
#endif /* CONFIG_HARDWARE_WATCHDOG */

int meson_power_suspend(void)
{
	static int test_flag = 0;
	unsigned addr;
	unsigned p_addr;
	void	(*pwrtest_entry)(unsigned,unsigned,unsigned,unsigned);

	flush_cache_all();

	addr = 0x9FF04400;
	p_addr = (unsigned)__phys_to_virt(addr);
	pwrtest_entry = (void (*)(unsigned,unsigned,unsigned,unsigned))p_addr;
	if(test_flag != 1234){
		test_flag = 1234;
		printk("initial appf\n");
		pwrtest_entry(APPF_INITIALIZE,0,0,0);
	}
#ifdef CONFIG_SUSPEND_WATCHDOG
	disable_watchdog();
#endif
	printk("power down cpu --\n");
	pwrtest_entry(APPF_POWER_DOWN_CPU,0,0,APPF_SAVE_PMU|APPF_SAVE_VFP|APPF_SAVE_L2);
#ifdef CONFIG_SUSPEND_WATCHDOG
	enable_watchdog();
#endif
	return 0;
}
