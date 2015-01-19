/*
 * arch/arm/mach-meson6tv/include/mach/gpio.h
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_MESON6TV_PM_H
#define __MACH_MESON6TV_PM_H

/*
 * Caution: Assembly code in sleep.S makes assumtion on the order
 * of the members of this structure.
 */
struct meson_pm_config {
	void __iomem *pctl_reg_base;
	void __iomem *mmc_reg_base;
	void __iomem *hiu_reg_base;
	unsigned power_key;
	unsigned ddr_clk;
	void __iomem *ddr_reg_backup;
	unsigned core_voltage_adjust;
	int sleepcount;
	void (*set_vccx2)(int power_on);
	void (*set_exgpio_early_suspend)(int power_on);
	void (*set_pinmux)(int power_on);
};

extern unsigned int meson_cpu_suspend_sz;
extern void meson_cpu_suspend(struct meson_pm_config *);
extern void power_gate_switch(int flag);
extern void clk_switch(int flag);
//extern void pll_switch(int flag);
extern void early_power_gate_switch(int flag);
extern void early_clk_switch(int flag);
//extern void early_pll_switch(int flag);
#ifdef CONFIG_MESON_SUSPEND
extern int meson_power_suspend(void);
#endif /*CONFIG_MESON_SUSPEND*/

#endif // __MACH_MESON6TV_PM_H