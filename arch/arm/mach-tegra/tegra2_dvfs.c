/*
 * arch/arm/mach-tegra/tegra2_dvfs.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>

#include "clock.h"
#include "dvfs.h"
#include "fuse.h"

#ifdef CONFIG_TEGRA_CORE_DVFS
static bool tegra_dvfs_core_disabled;
#else
static bool tegra_dvfs_core_disabled = true;
#endif
#ifdef CONFIG_TEGRA_CPU_DVFS
static bool tegra_dvfs_cpu_disabled;
#else
static bool tegra_dvfs_cpu_disabled = true;
#endif

static const int core_millivolts[MAX_DVFS_FREQS] =
	{950, 1000, 1100, 1200, 1275};
static const int cpu_millivolts[MAX_DVFS_FREQS] =
	{750, 775, 800, 825, 875,  900,  925,  975,  1000, 1050, 1100};

#define KHZ 1000
#define MHZ 1000000

static struct dvfs_rail tegra2_dvfs_rail_vdd_cpu = {
	.reg_id = "vdd_cpu",
	.max_millivolts = 1100,
	.min_millivolts = 750,
	.nominal_millivolts = 1100,
};

static struct dvfs_rail tegra2_dvfs_rail_vdd_core = {
	.reg_id = "vdd_core",
	.max_millivolts = 1275,
	.min_millivolts = 950,
	.nominal_millivolts = 1200,
	.step = 150, /* step vdd_core by 150 mV to allow vdd_aon to follow */
};

static struct dvfs_rail tegra2_dvfs_rail_vdd_aon = {
	.reg_id = "vdd_aon",
	.max_millivolts = 1275,
	.min_millivolts = 950,
	.nominal_millivolts = 1200,
#ifndef CONFIG_TEGRA_CORE_DVFS
	.disabled = true,
#endif
};

/* vdd_core and vdd_aon must be 50 mV higher than vdd_cpu */
static int tegra2_dvfs_rel_vdd_cpu_vdd_core(struct dvfs_rail *vdd_cpu,
	struct dvfs_rail *vdd_core)
{
	if (vdd_cpu->new_millivolts > vdd_cpu->millivolts &&
	    vdd_core->new_millivolts < vdd_cpu->new_millivolts + 50)
		return vdd_cpu->new_millivolts + 50;

	if (vdd_core->new_millivolts < vdd_cpu->millivolts + 50)
		return vdd_cpu->millivolts + 50;

	return vdd_core->new_millivolts;
}

/* vdd_aon must be within 170 mV of vdd_core */
static int tegra2_dvfs_rel_vdd_core_vdd_aon(struct dvfs_rail *vdd_core,
	struct dvfs_rail *vdd_aon)
{
	BUG_ON(abs(vdd_aon->millivolts - vdd_core->millivolts) >
		vdd_aon->step);
	return vdd_core->millivolts;
}

static struct dvfs_relationship tegra2_dvfs_relationships[] = {
	{
		/* vdd_core must be 50 mV higher than vdd_cpu */
		.from = &tegra2_dvfs_rail_vdd_cpu,
		.to = &tegra2_dvfs_rail_vdd_core,
		.solve = tegra2_dvfs_rel_vdd_cpu_vdd_core,
	},
	{
		/* vdd_aon must be 50 mV higher than vdd_cpu */
		.from = &tegra2_dvfs_rail_vdd_cpu,
		.to = &tegra2_dvfs_rail_vdd_aon,
		.solve = tegra2_dvfs_rel_vdd_cpu_vdd_core,
	},
	{
		/* vdd_aon must be within 170 mV of vdd_core */
		.from = &tegra2_dvfs_rail_vdd_core,
		.to = &tegra2_dvfs_rail_vdd_aon,
		.solve = tegra2_dvfs_rel_vdd_core_vdd_aon,
	},
};

static struct dvfs_rail *tegra2_dvfs_rails[] = {
	&tegra2_dvfs_rail_vdd_cpu,
	&tegra2_dvfs_rail_vdd_core,
	&tegra2_dvfs_rail_vdd_aon,
};

#define CPU_DVFS(_clk_name, _process_id, _mult, _freqs...)	\
	{							\
		.clk_name	= _clk_name,			\
		.cpu_process_id	= _process_id,			\
		.freqs		= {_freqs},			\
		.freqs_mult	= _mult,			\
		.millivolts	= cpu_millivolts,		\
		.auto_dvfs	= true,				\
		.dvfs_rail	= &tegra2_dvfs_rail_vdd_cpu,	\
	}

#define CORE_DVFS(_clk_name, _auto, _mult, _freqs...)		\
	{							\
		.clk_name	= _clk_name,			\
		.cpu_process_id = -1,				\
		.freqs		= {_freqs},			\
		.freqs_mult	= _mult,			\
		.millivolts	= core_millivolts,		\
		.auto_dvfs	= _auto,			\
		.dvfs_rail	= &tegra2_dvfs_rail_vdd_core,	\
	}

static struct dvfs dvfs_init[] = {
	/* Cpu voltages (mV):   750, 775, 800, 825, 875, 900, 925, 975, 1000, 1050, 1100 */
	CPU_DVFS("cpu", 0, MHZ, 314, 314, 314, 456, 456, 608, 608, 760, 817,  912,  1000),
	CPU_DVFS("cpu", 1, MHZ, 314, 314, 314, 456, 456, 618, 618, 770, 827,  922,  1000),
	CPU_DVFS("cpu", 2, MHZ, 494, 675, 675, 675, 817, 817, 922, 1000),
	CPU_DVFS("cpu", 3, MHZ, 730, 760, 845, 845, 1000),

	/* Core voltages (mV):       950,    1000,   1100,   1200,   1275 */
	CORE_DVFS("emc",     1, KHZ, 57000,  333000, 333000, 666000, 666000),

#if 0
	/*
	 * The sdhci core calls the clock ops with a spinlock held, which
	 * conflicts with the sleeping dvfs api.
	 * For now, boards must ensure that the core voltage does not drop
	 * below 1V, or that the sdmmc busses are set to 44 MHz or less.
	 */
	CORE_DVFS("sdmmc1",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc2",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc3",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc4",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
#endif

	CORE_DVFS("ndflash", 1, KHZ, 130000, 150000, 158000, 164000, 164000),
	CORE_DVFS("nor",     1, KHZ, 0,      92000,  92000,  92000,  92000),
	CORE_DVFS("ide",     1, KHZ, 0,      0,      100000, 100000, 100000),
	CORE_DVFS("mipi",    1, KHZ, 0,      40000,  40000,  40000, 60000),
	CORE_DVFS("usbd",    1, KHZ, 0,      0,      0,      480000, 480000),
	CORE_DVFS("usb2",    1, KHZ, 0,      0,      0,      480000, 480000),
	CORE_DVFS("usb3",    1, KHZ, 0,      0,      0,      480000, 480000),
	CORE_DVFS("pcie",    1, KHZ, 0,      0,      0,      250000, 250000),
	CORE_DVFS("dsi",     1, KHZ, 100000, 100000, 100000, 500000, 500000),
	CORE_DVFS("tvo",     1, KHZ, 0,      0,      0,      250000, 250000),

	/*
	 * The clock rate for the display controllers that determines the
	 * necessary core voltage depends on a divider that is internal
	 * to the display block.  Disable auto-dvfs on the display clocks,
	 * and let the display driver call tegra_dvfs_set_rate manually
	 */
	CORE_DVFS("disp1",   0, KHZ, 158000, 158000, 190000, 190000, 190000),
	CORE_DVFS("disp2",   0, KHZ, 158000, 158000, 190000, 190000, 190000),
	CORE_DVFS("hdmi",    0, KHZ, 0,      0,      0,      148500, 148500),

	/*
	 * These clocks technically depend on the core process id,
	 * but just use the worst case value for now
	 */
	CORE_DVFS("host1x",  1, KHZ, 104500, 133000, 166000, 166000, 166000),
	CORE_DVFS("epp",     1, KHZ, 133000, 171000, 247000, 300000, 300000),
	CORE_DVFS("2d",      1, KHZ, 133000, 171000, 247000, 300000, 300000),
	CORE_DVFS("3d",      1, KHZ, 114000, 161500, 247000, 300000, 300000),
	CORE_DVFS("mpe",     1, KHZ, 104500, 152000, 228000, 250000, 250000),
	CORE_DVFS("vi",      1, KHZ, 85000,  100000, 150000, 150000, 150000),
	CORE_DVFS("sclk",    1, KHZ, 95000,  133000, 190000, 250000, 250000),
	CORE_DVFS("vde",     1, KHZ, 95000,  123500, 209000, 250000, 250000),
	/* What is this? */
	CORE_DVFS("NVRM_DEVID_CLK_SRC", 1, MHZ, 480, 600, 800, 1067, 1067),
};

int tegra_dvfs_disable_core_set(const char *arg, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (tegra_dvfs_core_disabled)
		tegra_dvfs_rail_disable(&tegra2_dvfs_rail_vdd_core);
	else
		tegra_dvfs_rail_enable(&tegra2_dvfs_rail_vdd_core);

	return 0;
}

int tegra_dvfs_disable_cpu_set(const char *arg, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(arg, kp);
	if (ret)
		return ret;

	if (tegra_dvfs_cpu_disabled)
		tegra_dvfs_rail_disable(&tegra2_dvfs_rail_vdd_cpu);
	else
		tegra_dvfs_rail_enable(&tegra2_dvfs_rail_vdd_cpu);

	return 0;
}

int tegra_dvfs_disable_get(char *buffer, const struct kernel_param *kp)
{
	return param_get_bool(buffer, kp);
}

static struct kernel_param_ops tegra_dvfs_disable_core_ops = {
	.set = tegra_dvfs_disable_core_set,
	.get = tegra_dvfs_disable_get,
};

static struct kernel_param_ops tegra_dvfs_disable_cpu_ops = {
	.set = tegra_dvfs_disable_cpu_set,
	.get = tegra_dvfs_disable_get,
};

module_param_cb(disable_core, &tegra_dvfs_disable_core_ops,
	&tegra_dvfs_core_disabled, 0644);
module_param_cb(disable_cpu, &tegra_dvfs_disable_cpu_ops,
	&tegra_dvfs_cpu_disabled, 0644);

void __init tegra2_init_dvfs(void)
{
	int i;
	struct clk *c;
	struct dvfs *d;
	int ret;
	int cpu_process_id = tegra_cpu_process_id();

	tegra_dvfs_init_rails(tegra2_dvfs_rails, ARRAY_SIZE(tegra2_dvfs_rails));
	tegra_dvfs_add_relationships(tegra2_dvfs_relationships,
		ARRAY_SIZE(tegra2_dvfs_relationships));
	/*
	 * VDD_CORE must always be at least 50 mV higher than VDD_CPU
	 * Fill out cpu_core_millivolts based on cpu_millivolts
	 */
	for (i = 0; i < ARRAY_SIZE(dvfs_init); i++) {
		d = &dvfs_init[i];

		if (d->cpu_process_id != -1 &&
				d->cpu_process_id != cpu_process_id)
			continue;

		c = tegra_get_clock_by_name(d->clk_name);

		if (!c) {
			pr_debug("tegra_dvfs: no clock found for %s\n",
				d->clk_name);
			continue;
		}

		ret = tegra_enable_dvfs_on_clk(c, d);
		if (ret)
			pr_err("tegra_dvfs: failed to enable dvfs on %s\n",
				c->name);
	}

	if (tegra_dvfs_core_disabled)
		tegra_dvfs_rail_disable(&tegra2_dvfs_rail_vdd_core);

	if (tegra_dvfs_cpu_disabled)
		tegra_dvfs_rail_disable(&tegra2_dvfs_rail_vdd_cpu);
}
