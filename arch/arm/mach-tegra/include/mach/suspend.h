/*
 * arch/arm/mach-tegra/include/mach/suspend.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#ifndef _MACH_TEGRA_SUSPEND_H_
#define _MACH_TEGRA_SUSPEND_H_

enum tegra_suspend_mode {
	TEGRA_SUSPEND_NONE = 0,
	TEGRA_SUSPEND_LP2,	/* CPU voltage off */
	TEGRA_SUSPEND_LP1,	/* CPU voltage off, DRAM self-refresh */
	TEGRA_SUSPEND_LP0,	/* CPU + core voltage off, DRAM self-refresh */
	TEGRA_MAX_SUSPEND_MODE,
};

struct tegra_suspend_platform_data {
	unsigned long cpu_timer;   /* CPU power good time in us,  LP2/LP1 */
	unsigned long cpu_off_timer;	/* CPU power off time us, LP2/LP1 */
	unsigned long core_timer;  /* core power good time in ticks,  LP0 */
	unsigned long core_off_timer;	/* core power off time ticks, LP0 */
	unsigned long wake_enb;    /* mask of enabled wake pads */
	unsigned long wake_high;   /* high-level-triggered wake pads */
	unsigned long wake_low;    /* low-level-triggered wake pads */
	unsigned long wake_any;    /* any-edge-triggered wake pads */
	bool corereq_high;         /* Core power request active-high */
	bool sysclkreq_high;       /* System clock request is active-high */
	bool separate_req;         /* Core & CPU power request are separate */
	enum tegra_suspend_mode suspend_mode;
};

unsigned long tegra_cpu_power_good_time(void);
unsigned long tegra_cpu_power_off_time(void);
enum tegra_suspend_mode tegra_get_suspend_mode(void);

void __tegra_lp1_reset(void);
void __tegra_iram_end(void);

void lp0_suspend_init(void);

void tegra_pinmux_suspend(void);
void tegra_irq_suspend(void);
void tegra_gpio_suspend(void);
void tegra_clk_suspend(void);
void tegra_dma_suspend(void);
void tegra_timer_suspend(void);

void tegra_pinmux_resume(void);
void tegra_irq_resume(void);
void tegra_gpio_resume(void);
void tegra_clk_resume(void);
void tegra_dma_resume(void);
void tegra_timer_resume(void);

int tegra_irq_to_wake(int irq);
int tegra_wake_to_irq(int wake);

int tegra_set_lp0_wake(int irq, int enable);
int tegra_set_lp0_wake_type(int irq, int flow_type);
int tegra_set_lp1_wake(int irq, int enable);
void tegra_set_lp0_wake_pads(u32 wake_enb, u32 wake_level, u32 wake_any);

void __init tegra_init_suspend(struct tegra_suspend_platform_data *plat);

#endif /* _MACH_TEGRA_SUSPEND_H_ */
