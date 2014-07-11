/*
 * Copyright (C) 2012,2013 NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <soc/tegra/powergate.h>

#include "flowctrl.h"
#include "fuse.h"
#include "pm.h"
#include "pmc.h"
#include "sleep.h"

#define TEGRA_POWER_SYSCLK_POLARITY	(1 << 10)  /* sys clk polarity */
#define TEGRA_POWER_SYSCLK_OE		(1 << 11)  /* system clock enable */
#define TEGRA_POWER_EFFECT_LP0		(1 << 14)  /* LP0 when CPU pwr gated */
#define TEGRA_POWER_CPU_PWRREQ_POLARITY	(1 << 15)  /* CPU pwr req polarity */
#define TEGRA_POWER_CPU_PWRREQ_OE	(1 << 16)  /* CPU pwr req enable */

#define PMC_CTRL			0x0
#define PMC_CTRL_INTR_LOW		(1 << 17)
#define PMC_PWRGATE_TOGGLE		0x30
#define PMC_PWRGATE_TOGGLE_START	(1 << 8)
#define PMC_REMOVE_CLAMPING		0x34
#define PMC_PWRGATE_STATUS		0x38

#define PMC_SCRATCH0			0x50
#define PMC_SCRATCH0_MODE_RECOVERY	(1 << 31)
#define PMC_SCRATCH0_MODE_BOOTLOADER	(1 << 30)
#define PMC_SCRATCH0_MODE_RCM		(1 << 1)
#define PMC_SCRATCH0_MODE_MASK		(PMC_SCRATCH0_MODE_RECOVERY | \
					 PMC_SCRATCH0_MODE_BOOTLOADER | \
					 PMC_SCRATCH0_MODE_RCM)

#define PMC_CPUPWRGOOD_TIMER	0xc8
#define PMC_CPUPWROFF_TIMER	0xcc

static u8 tegra_cpu_domains[] = {
	0xFF,			/* not available for CPU0 */
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};
static DEFINE_SPINLOCK(tegra_powergate_lock);

static void __iomem *tegra_pmc_base;
static bool tegra_pmc_invert_interrupt;
static struct clk *tegra_pclk;

struct pmc_pm_data {
	u32 cpu_good_time;	/* CPU power good time in uS */
	u32 cpu_off_time;	/* CPU power off time in uS */
	u32 core_osc_time;	/* Core power good osc time in uS */
	u32 core_pmu_time;	/* Core power good pmu time in uS */
	u32 core_off_time;	/* Core power off time in uS */
	bool corereq_high;	/* Core power request active-high */
	bool sysclkreq_high;	/* System clock request active-high */
	bool combined_req;	/* Combined pwr req for CPU & Core */
	bool cpu_pwr_good_en;	/* CPU power good signal is enabled */
	u32 lp0_vec_phy_addr;	/* The phy addr of LP0 warm boot code */
	u32 lp0_vec_size;	/* The size of LP0 warm boot code */
	enum tegra_suspend_mode suspend_mode;
};
static struct pmc_pm_data pmc_pm_data;

static inline u32 tegra_pmc_readl(u32 reg)
{
	return readl(tegra_pmc_base + reg);
}

static inline void tegra_pmc_writel(u32 val, u32 reg)
{
	writel(val, tegra_pmc_base + reg);
}

static int tegra_pmc_get_cpu_powerdomain_id(int cpuid)
{
	if (cpuid <= 0 || cpuid >= num_possible_cpus())
		return -EINVAL;
	return tegra_cpu_domains[cpuid];
}

static bool tegra_pmc_powergate_is_powered(int id)
{
	return (tegra_pmc_readl(PMC_PWRGATE_STATUS) >> id) & 1;
}

static int tegra_pmc_powergate_set(int id, bool new_state)
{
	bool old_state;
	unsigned long flags;

	spin_lock_irqsave(&tegra_powergate_lock, flags);

	old_state = tegra_pmc_powergate_is_powered(id);
	WARN_ON(old_state == new_state);

	tegra_pmc_writel(PMC_PWRGATE_TOGGLE_START | id, PMC_PWRGATE_TOGGLE);

	spin_unlock_irqrestore(&tegra_powergate_lock, flags);

	return 0;
}

static int tegra_pmc_powergate_remove_clamping(int id)
{
	u32 mask;

	/*
	 * Tegra has a bug where PCIE and VDE clamping masks are
	 * swapped relatively to the partition ids.
	 */
	if (id ==  TEGRA_POWERGATE_VDEC)
		mask = (1 << TEGRA_POWERGATE_PCIE);
	else if	(id == TEGRA_POWERGATE_PCIE)
		mask = (1 << TEGRA_POWERGATE_VDEC);
	else
		mask = (1 << id);

	tegra_pmc_writel(mask, PMC_REMOVE_CLAMPING);

	return 0;
}

bool tegra_pmc_cpu_is_powered(int cpuid)
{
	int id;

	id = tegra_pmc_get_cpu_powerdomain_id(cpuid);
	if (id < 0)
		return false;
	return tegra_pmc_powergate_is_powered(id);
}

int tegra_pmc_cpu_power_on(int cpuid)
{
	int id;

	id = tegra_pmc_get_cpu_powerdomain_id(cpuid);
	if (id < 0)
		return id;
	return tegra_pmc_powergate_set(id, true);
}

int tegra_pmc_cpu_remove_clamping(int cpuid)
{
	int id;

	id = tegra_pmc_get_cpu_powerdomain_id(cpuid);
	if (id < 0)
		return id;
	return tegra_pmc_powergate_remove_clamping(id);
}

void tegra_pmc_restart(enum reboot_mode mode, const char *cmd)
{
	u32 val;

	val = tegra_pmc_readl(PMC_SCRATCH0);
	val &= ~PMC_SCRATCH0_MODE_MASK;

	if (cmd) {
		if (strcmp(cmd, "recovery") == 0)
			val |= PMC_SCRATCH0_MODE_RECOVERY;

		if (strcmp(cmd, "bootloader") == 0)
			val |= PMC_SCRATCH0_MODE_BOOTLOADER;

		if (strcmp(cmd, "forced-recovery") == 0)
			val |= PMC_SCRATCH0_MODE_RCM;
	}

	tegra_pmc_writel(val, PMC_SCRATCH0);

	val = tegra_pmc_readl(0);
	val |= 0x10;
	tegra_pmc_writel(val, 0);
}

#ifdef CONFIG_PM_SLEEP
static void set_power_timers(u32 us_on, u32 us_off, unsigned long rate)
{
	unsigned long long ticks;
	unsigned long long pclk;
	static unsigned long tegra_last_pclk;

	if (WARN_ON_ONCE(rate <= 0))
		pclk = 100000000;
	else
		pclk = rate;

	if ((rate != tegra_last_pclk)) {
		ticks = (us_on * pclk) + 999999ull;
		do_div(ticks, 1000000);
		tegra_pmc_writel((unsigned long)ticks, PMC_CPUPWRGOOD_TIMER);

		ticks = (us_off * pclk) + 999999ull;
		do_div(ticks, 1000000);
		tegra_pmc_writel((unsigned long)ticks, PMC_CPUPWROFF_TIMER);
		wmb();
	}
	tegra_last_pclk = pclk;
}

enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void)
{
	return pmc_pm_data.suspend_mode;
}

void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode)
{
	if (mode < TEGRA_SUSPEND_NONE || mode >= TEGRA_MAX_SUSPEND_MODE)
		return;

	pmc_pm_data.suspend_mode = mode;
}

void tegra_pmc_suspend(void)
{
	tegra_pmc_writel(virt_to_phys(tegra_resume), PMC_SCRATCH41);
}

void tegra_pmc_resume(void)
{
	tegra_pmc_writel(0x0, PMC_SCRATCH41);
}

void tegra_pmc_pm_set(enum tegra_suspend_mode mode)
{
	u32 reg, csr_reg;
	unsigned long rate = 0;

	reg = tegra_pmc_readl(PMC_CTRL);
	reg |= TEGRA_POWER_CPU_PWRREQ_OE;
	reg &= ~TEGRA_POWER_EFFECT_LP0;

	switch (tegra_chip_id) {
	case TEGRA20:
	case TEGRA30:
		break;
	default:
		/* Turn off CRAIL */
		csr_reg = flowctrl_read_cpu_csr(0);
		csr_reg &= ~FLOW_CTRL_CSR_ENABLE_EXT_MASK;
		csr_reg |= FLOW_CTRL_CSR_ENABLE_EXT_CRAIL;
		flowctrl_write_cpu_csr(0, csr_reg);
		break;
	}

	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		rate = 32768;
		break;
	case TEGRA_SUSPEND_LP2:
		rate = clk_get_rate(tegra_pclk);
		break;
	default:
		break;
	}

	set_power_timers(pmc_pm_data.cpu_good_time, pmc_pm_data.cpu_off_time,
			 rate);

	tegra_pmc_writel(reg, PMC_CTRL);
}

void tegra_pmc_suspend_init(void)
{
	u32 reg;

	/* Always enable CPU power request */
	reg = tegra_pmc_readl(PMC_CTRL);
	reg |= TEGRA_POWER_CPU_PWRREQ_OE;
	tegra_pmc_writel(reg, PMC_CTRL);

	reg = tegra_pmc_readl(PMC_CTRL);

	if (!pmc_pm_data.sysclkreq_high)
		reg |= TEGRA_POWER_SYSCLK_POLARITY;
	else
		reg &= ~TEGRA_POWER_SYSCLK_POLARITY;

	/* configure the output polarity while the request is tristated */
	tegra_pmc_writel(reg, PMC_CTRL);

	/* now enable the request */
	reg |= TEGRA_POWER_SYSCLK_OE;
	tegra_pmc_writel(reg, PMC_CTRL);
}
#endif

static const struct of_device_id matches[] __initconst = {
	{ .compatible = "nvidia,tegra124-pmc" },
	{ .compatible = "nvidia,tegra114-pmc" },
	{ .compatible = "nvidia,tegra30-pmc" },
	{ .compatible = "nvidia,tegra20-pmc" },
	{ }
};

void __init tegra_pmc_init_irq(void)
{
	struct device_node *np;
	u32 val;

	np = of_find_matching_node(NULL, matches);
	BUG_ON(!np);

	tegra_pmc_base = of_iomap(np, 0);

	tegra_pmc_invert_interrupt = of_property_read_bool(np,
				     "nvidia,invert-interrupt");

	val = tegra_pmc_readl(PMC_CTRL);
	if (tegra_pmc_invert_interrupt)
		val |= PMC_CTRL_INTR_LOW;
	else
		val &= ~PMC_CTRL_INTR_LOW;
	tegra_pmc_writel(val, PMC_CTRL);
}

void __init tegra_pmc_init(void)
{
	struct device_node *np;
	u32 prop;
	enum tegra_suspend_mode suspend_mode;
	u32 core_good_time[2] = {0, 0};
	u32 lp0_vec[2] = {0, 0};

	np = of_find_matching_node(NULL, matches);
	BUG_ON(!np);

	tegra_pclk = of_clk_get_by_name(np, "pclk");
	WARN_ON(IS_ERR(tegra_pclk));

	/* Grabbing the power management configurations */
	if (of_property_read_u32(np, "nvidia,suspend-mode", &prop)) {
		suspend_mode = TEGRA_SUSPEND_NONE;
	} else {
		switch (prop) {
		case 0:
			suspend_mode = TEGRA_SUSPEND_LP0;
			break;
		case 1:
			suspend_mode = TEGRA_SUSPEND_LP1;
			break;
		case 2:
			suspend_mode = TEGRA_SUSPEND_LP2;
			break;
		default:
			suspend_mode = TEGRA_SUSPEND_NONE;
			break;
		}
	}
	suspend_mode = tegra_pm_validate_suspend_mode(suspend_mode);

	if (of_property_read_u32(np, "nvidia,cpu-pwr-good-time", &prop))
		suspend_mode = TEGRA_SUSPEND_NONE;
	pmc_pm_data.cpu_good_time = prop;

	if (of_property_read_u32(np, "nvidia,cpu-pwr-off-time", &prop))
		suspend_mode = TEGRA_SUSPEND_NONE;
	pmc_pm_data.cpu_off_time = prop;

	if (of_property_read_u32_array(np, "nvidia,core-pwr-good-time",
			core_good_time, ARRAY_SIZE(core_good_time)))
		suspend_mode = TEGRA_SUSPEND_NONE;
	pmc_pm_data.core_osc_time = core_good_time[0];
	pmc_pm_data.core_pmu_time = core_good_time[1];

	if (of_property_read_u32(np, "nvidia,core-pwr-off-time",
				 &prop))
		suspend_mode = TEGRA_SUSPEND_NONE;
	pmc_pm_data.core_off_time = prop;

	pmc_pm_data.corereq_high = of_property_read_bool(np,
				"nvidia,core-power-req-active-high");

	pmc_pm_data.sysclkreq_high = of_property_read_bool(np,
				"nvidia,sys-clock-req-active-high");

	pmc_pm_data.combined_req = of_property_read_bool(np,
				"nvidia,combined-power-req");

	pmc_pm_data.cpu_pwr_good_en = of_property_read_bool(np,
				"nvidia,cpu-pwr-good-en");

	if (of_property_read_u32_array(np, "nvidia,lp0-vec", lp0_vec,
				       ARRAY_SIZE(lp0_vec)))
		if (suspend_mode == TEGRA_SUSPEND_LP0)
			suspend_mode = TEGRA_SUSPEND_LP1;

	pmc_pm_data.lp0_vec_phy_addr = lp0_vec[0];
	pmc_pm_data.lp0_vec_size = lp0_vec[1];

	pmc_pm_data.suspend_mode = suspend_mode;
}
