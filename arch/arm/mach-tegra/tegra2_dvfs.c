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
#include <linux/string.h>

#include "clock.h"
#include "dvfs.h"
#include "fuse.h"

#define CORE_REGULATOR "vdd_core"
#define CPU_REGULATOR "vdd_cpu"

static const int core_millivolts[MAX_DVFS_FREQS] =
	{950, 1000, 1100, 1200, 1275};
static const int cpu_millivolts[MAX_DVFS_FREQS] =
	{750, 775, 800, 825, 875,  900,  925,  975,  1000, 1050, 1100};
static int cpu_core_millivolts[MAX_DVFS_FREQS];

#define CORE_MAX_MILLIVOLTS 1275
#define CPU_MAX_MILLIVOLTS 1100

#define KHZ 1000
#define MHZ 1000000

#define CPU_DVFS(_clk_name, _process_id, _mult, _freqs...)		\
	{						\
		.clk_name	= _clk_name,		\
		.reg_id		= CORE_REGULATOR,	\
		.cpu		= false,		\
		.process_id	= _process_id,		\
		.freqs		= {_freqs},		\
		.freqs_mult	= _mult,		\
		.auto_dvfs	= true,			\
		.higher		= true,			\
		.max_millivolts = CORE_MAX_MILLIVOLTS   \
	},						\
	{						\
		.clk_name	= _clk_name,		\
		.reg_id		= CPU_REGULATOR,	\
		.cpu		= true,			\
		.process_id	= _process_id,		\
		.freqs		= {_freqs},		\
		.freqs_mult	= _mult,		\
		.auto_dvfs	= true,			\
		.max_millivolts = CPU_MAX_MILLIVOLTS    \
	}

#define CORE_DVFS(_clk_name, _auto, _mult, _freqs...)	\
	{						\
		.clk_name	= _clk_name,		\
		.reg_id		= CORE_REGULATOR,	\
		.process_id	= -1,			\
		.freqs		= {_freqs},		\
		.freqs_mult	= _mult,		\
		.auto_dvfs	= _auto,		\
		.max_millivolts = CORE_MAX_MILLIVOLTS   \
	}

static struct dvfs dvfs_init[] = {
	/* Cpu voltages (mV):   750, 775, 800, 825, 875, 900, 925, 975, 1000, 1050, 1100 */
	CPU_DVFS("cpu", 0, MHZ, 314, 314, 314, 456, 456, 608, 608, 760, 817,  912,  1000),
	CPU_DVFS("cpu", 1, MHZ, 314, 314, 314, 456, 456, 618, 618, 770, 827,  922,  1000),
	CPU_DVFS("cpu", 2, MHZ, 494, 675, 675, 675, 817, 817, 922, 1000),
	CPU_DVFS("cpu", 3, MHZ, 730, 760, 845, 845, 1000),

	/* Core voltages (mV):       950,    1000,   1100,   1200,   1275 */
	CORE_DVFS("sdmmc1",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc2",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc3",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("sdmmc4",  1, KHZ, 44000,  52000,  52000,  52000,  52000),
	CORE_DVFS("ndflash", 1, KHZ, 130000, 150000, 158000, 164000, 164000),
	CORE_DVFS("nor",     1, KHZ, 0,      92000,  92000,  92000,  92000),
	CORE_DVFS("ide",     1, KHZ, 0,      0,      100000, 100000, 100000),
	CORE_DVFS("mipi",    1, KHZ, 0,      40000,  40000,  40000, 60000),
	CORE_DVFS("usbd",    1, KHZ, 0,      0,      480000, 480000, 480000),
	CORE_DVFS("usb2",    1, KHZ, 0,      0,      480000, 480000, 480000),
	CORE_DVFS("usb3",    1, KHZ, 0,      0,      480000, 480000, 480000),
	CORE_DVFS("pcie",    1, KHZ, 0,      0,      0,      250000, 250000),
	CORE_DVFS("dsi",     1, KHZ, 100000, 100000, 100000, 500000, 500000),
	CORE_DVFS("tvo",     1, KHZ, 0,      0,      0,      250000, 250000),
	CORE_DVFS("hdmi",    1, KHZ, 0,      0,      0,      148500, 148500),

	/*
	 * The clock rate for the display controllers that determines the
	 * necessary core voltage depends on a divider that is internal
	 * to the display block.  Disable auto-dvfs on the display clocks,
	 * and let the display driver call tegra_dvfs_set_rate manually
	 */
	CORE_DVFS("disp1",   0, KHZ, 158000, 158000, 190000, 190000, 190000),
	CORE_DVFS("disp2",   0, KHZ, 158000, 158000, 190000, 190000, 190000),

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

void tegra2_init_dvfs(void)
{
	int i;
	struct clk *c;
	struct dvfs *d;
	int process_id;
	int ret;

	int cpu_process_id = tegra_cpu_process_id();
	int core_process_id = tegra_core_process_id();

	/*
	 * VDD_CORE must always be at least 50 mV higher than VDD_CPU
	 * Fill out cpu_core_millivolts based on cpu_millivolts
	 */
	for (i = 0; i < ARRAY_SIZE(cpu_millivolts); i++)
		if (cpu_millivolts[i])
			cpu_core_millivolts[i] = cpu_millivolts[i] + 50;

	for (i = 0; i < ARRAY_SIZE(dvfs_init); i++) {
		d = &dvfs_init[i];

		process_id = d->cpu ? cpu_process_id : core_process_id;
		if (d->process_id != -1 && d->process_id != process_id) {
			pr_debug("tegra_dvfs: rejected %s %d, process_id %d\n",
				d->clk_name, d->process_id, process_id);
			continue;
		}

		c = tegra_get_clock_by_name(d->clk_name);

		if (!c) {
			pr_debug("tegra_dvfs: no clock found for %s\n",
				d->clk_name);
			continue;
		}

		if (d->cpu)
			memcpy(d->millivolts, cpu_millivolts,
				sizeof(cpu_millivolts));
		else if (!strcmp(d->clk_name, "cpu"))
			memcpy(d->millivolts, cpu_core_millivolts,
				sizeof(cpu_core_millivolts));
		else
			memcpy(d->millivolts, core_millivolts,
				sizeof(core_millivolts));

		ret = tegra_enable_dvfs_on_clk(c, d);
		if (ret)
			pr_err("tegra_dvfs: failed to enable dvfs on %s\n",
				c->name);
	}
}
