/*
 * drivers/soc/tegra/pmc.c
 *
 * Copyright (c) 2010 Google, Inc
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

#define pr_fmt(fmt) "tegra-pmc: " fmt

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reboot.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/pmc.h>

#define PMC_CNTRL			0x0
#define  PMC_CNTRL_INTR_POLARITY	BIT(17) /* inverts INTR polarity */
#define  PMC_CNTRL_CPU_PWRREQ_OE	BIT(16) /* CPU pwr req enable */
#define  PMC_CNTRL_CPU_PWRREQ_POLARITY	BIT(15) /* CPU pwr req polarity */
#define  PMC_CNTRL_SIDE_EFFECT_LP0	BIT(14) /* LP0 when CPU pwr gated */
#define  PMC_CNTRL_SYSCLK_OE		BIT(11) /* system clock enable */
#define  PMC_CNTRL_SYSCLK_POLARITY	BIT(10) /* sys clk polarity */
#define  PMC_CNTRL_MAIN_RST		BIT(4)

#define DPD_SAMPLE			0x020
#define  DPD_SAMPLE_ENABLE		BIT(0)
#define  DPD_SAMPLE_DISABLE		(0 << 0)

#define PWRGATE_TOGGLE			0x30
#define  PWRGATE_TOGGLE_START		BIT(8)

#define REMOVE_CLAMPING			0x34

#define PWRGATE_STATUS			0x38

#define PMC_PWR_DET			0x48

#define PMC_SCRATCH0			0x50
#define  PMC_SCRATCH0_MODE_RECOVERY	BIT(31)
#define  PMC_SCRATCH0_MODE_BOOTLOADER	BIT(30)
#define  PMC_SCRATCH0_MODE_RCM		BIT(1)
#define  PMC_SCRATCH0_MODE_MASK		(PMC_SCRATCH0_MODE_RECOVERY | \
					 PMC_SCRATCH0_MODE_BOOTLOADER | \
					 PMC_SCRATCH0_MODE_RCM)

#define PMC_CPUPWRGOOD_TIMER		0xc8
#define PMC_CPUPWROFF_TIMER		0xcc

#define PMC_PWR_DET_VALUE		0xe4

#define PMC_SCRATCH41			0x140

#define PMC_SENSOR_CTRL			0x1b0
#define  PMC_SENSOR_CTRL_SCRATCH_WRITE	BIT(2)
#define  PMC_SENSOR_CTRL_ENABLE_RST	BIT(1)

#define PMC_RST_STATUS			0x1b4
#define  PMC_RST_STATUS_POR		0
#define  PMC_RST_STATUS_WATCHDOG	1
#define  PMC_RST_STATUS_SENSOR		2
#define  PMC_RST_STATUS_SW_MAIN		3
#define  PMC_RST_STATUS_LP0		4
#define  PMC_RST_STATUS_AOTAG		5

#define IO_DPD_REQ			0x1b8
#define  IO_DPD_REQ_CODE_IDLE		(0U << 30)
#define  IO_DPD_REQ_CODE_OFF		(1U << 30)
#define  IO_DPD_REQ_CODE_ON		(2U << 30)
#define  IO_DPD_REQ_CODE_MASK		(3U << 30)

#define IO_DPD_STATUS			0x1bc
#define IO_DPD2_REQ			0x1c0
#define IO_DPD2_STATUS			0x1c4
#define SEL_DPD_TIM			0x1c8

#define PMC_SCRATCH54			0x258
#define  PMC_SCRATCH54_DATA_SHIFT	8
#define  PMC_SCRATCH54_ADDR_SHIFT	0

#define PMC_SCRATCH55			0x25c
#define  PMC_SCRATCH55_RESET_TEGRA	BIT(31)
#define  PMC_SCRATCH55_CNTRL_ID_SHIFT	27
#define  PMC_SCRATCH55_PINMUX_SHIFT	24
#define  PMC_SCRATCH55_16BITOP		BIT(15)
#define  PMC_SCRATCH55_CHECKSUM_SHIFT	16
#define  PMC_SCRATCH55_I2CSLV1_SHIFT	0

#define GPU_RG_CNTRL			0x2d4

struct tegra_powergate {
	struct generic_pm_domain genpd;
	struct tegra_pmc *pmc;
	unsigned int id;
	struct clk **clks;
	unsigned int num_clks;
	struct reset_control **resets;
	unsigned int num_resets;
};

struct tegra_io_pad_soc {
	enum tegra_io_pad id;
	unsigned int dpd;
	unsigned int voltage;
};

struct tegra_pmc_soc {
	unsigned int num_powergates;
	const char *const *powergates;
	unsigned int num_cpu_powergates;
	const u8 *cpu_powergates;

	bool has_tsense_reset;
	bool has_gpu_clamps;

	const struct tegra_io_pad_soc *io_pads;
	unsigned int num_io_pads;
};

/**
 * struct tegra_pmc - NVIDIA Tegra PMC
 * @dev: pointer to PMC device structure
 * @base: pointer to I/O remapped register region
 * @clk: pointer to pclk clock
 * @soc: pointer to SoC data structure
 * @debugfs: pointer to debugfs entry
 * @rate: currently configured rate of pclk
 * @suspend_mode: lowest suspend mode available
 * @cpu_good_time: CPU power good time (in microseconds)
 * @cpu_off_time: CPU power off time (in microsecends)
 * @core_osc_time: core power good OSC time (in microseconds)
 * @core_pmu_time: core power good PMU time (in microseconds)
 * @core_off_time: core power off time (in microseconds)
 * @corereq_high: core power request is active-high
 * @sysclkreq_high: system clock request is active-high
 * @combined_req: combined power request for CPU & core
 * @cpu_pwr_good_en: CPU power good signal is enabled
 * @lp0_vec_phys: physical base address of the LP0 warm boot code
 * @lp0_vec_size: size of the LP0 warm boot code
 * @powergates_available: Bitmap of available power gates
 * @powergates_lock: mutex for power gate register access
 */
struct tegra_pmc {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct dentry *debugfs;

	const struct tegra_pmc_soc *soc;

	unsigned long rate;

	enum tegra_suspend_mode suspend_mode;
	u32 cpu_good_time;
	u32 cpu_off_time;
	u32 core_osc_time;
	u32 core_pmu_time;
	u32 core_off_time;
	bool corereq_high;
	bool sysclkreq_high;
	bool combined_req;
	bool cpu_pwr_good_en;
	u32 lp0_vec_phys;
	u32 lp0_vec_size;
	DECLARE_BITMAP(powergates_available, TEGRA_POWERGATE_MAX);

	struct mutex powergates_lock;
};

static struct tegra_pmc *pmc = &(struct tegra_pmc) {
	.base = NULL,
	.suspend_mode = TEGRA_SUSPEND_NONE,
};

static inline struct tegra_powergate *
to_powergate(struct generic_pm_domain *domain)
{
	return container_of(domain, struct tegra_powergate, genpd);
}

static u32 tegra_pmc_readl(unsigned long offset)
{
	return readl(pmc->base + offset);
}

static void tegra_pmc_writel(u32 value, unsigned long offset)
{
	writel(value, pmc->base + offset);
}

static inline bool tegra_powergate_state(int id)
{
	if (id == TEGRA_POWERGATE_3D && pmc->soc->has_gpu_clamps)
		return (tegra_pmc_readl(GPU_RG_CNTRL) & 0x1) == 0;
	else
		return (tegra_pmc_readl(PWRGATE_STATUS) & BIT(id)) != 0;
}

static inline bool tegra_powergate_is_valid(int id)
{
	return (pmc->soc && pmc->soc->powergates[id]);
}

static inline bool tegra_powergate_is_available(int id)
{
	return test_bit(id, pmc->powergates_available);
}

static int tegra_powergate_lookup(struct tegra_pmc *pmc, const char *name)
{
	unsigned int i;

	if (!pmc || !pmc->soc || !name)
		return -EINVAL;

	for (i = 0; i < pmc->soc->num_powergates; i++) {
		if (!tegra_powergate_is_valid(i))
			continue;

		if (!strcmp(name, pmc->soc->powergates[i]))
			return i;
	}

	return -ENODEV;
}

/**
 * tegra_powergate_set() - set the state of a partition
 * @id: partition ID
 * @new_state: new state of the partition
 */
static int tegra_powergate_set(unsigned int id, bool new_state)
{
	bool status;
	int err;

	if (id == TEGRA_POWERGATE_3D && pmc->soc->has_gpu_clamps)
		return -EINVAL;

	mutex_lock(&pmc->powergates_lock);

	if (tegra_powergate_state(id) == new_state) {
		mutex_unlock(&pmc->powergates_lock);
		return 0;
	}

	tegra_pmc_writel(PWRGATE_TOGGLE_START | id, PWRGATE_TOGGLE);

	err = readx_poll_timeout(tegra_powergate_state, id, status,
				 status == new_state, 10, 100000);

	mutex_unlock(&pmc->powergates_lock);

	return err;
}

static int __tegra_powergate_remove_clamping(unsigned int id)
{
	u32 mask;

	mutex_lock(&pmc->powergates_lock);

	/*
	 * On Tegra124 and later, the clamps for the GPU are controlled by a
	 * separate register (with different semantics).
	 */
	if (id == TEGRA_POWERGATE_3D) {
		if (pmc->soc->has_gpu_clamps) {
			tegra_pmc_writel(0, GPU_RG_CNTRL);
			goto out;
		}
	}

	/*
	 * Tegra 2 has a bug where PCIE and VDE clamping masks are
	 * swapped relatively to the partition ids
	 */
	if (id == TEGRA_POWERGATE_VDEC)
		mask = (1 << TEGRA_POWERGATE_PCIE);
	else if (id == TEGRA_POWERGATE_PCIE)
		mask = (1 << TEGRA_POWERGATE_VDEC);
	else
		mask = (1 << id);

	tegra_pmc_writel(mask, REMOVE_CLAMPING);

out:
	mutex_unlock(&pmc->powergates_lock);

	return 0;
}

static void tegra_powergate_disable_clocks(struct tegra_powergate *pg)
{
	unsigned int i;

	for (i = 0; i < pg->num_clks; i++)
		clk_disable_unprepare(pg->clks[i]);
}

static int tegra_powergate_enable_clocks(struct tegra_powergate *pg)
{
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_clks; i++) {
		err = clk_prepare_enable(pg->clks[i]);
		if (err)
			goto out;
	}

	return 0;

out:
	while (i--)
		clk_disable_unprepare(pg->clks[i]);

	return err;
}

static int tegra_powergate_reset_assert(struct tegra_powergate *pg)
{
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_resets; i++) {
		err = reset_control_assert(pg->resets[i]);
		if (err)
			return err;
	}

	return 0;
}

static int tegra_powergate_reset_deassert(struct tegra_powergate *pg)
{
	unsigned int i;
	int err;

	for (i = 0; i < pg->num_resets; i++) {
		err = reset_control_deassert(pg->resets[i]);
		if (err)
			return err;
	}

	return 0;
}

static int tegra_powergate_power_up(struct tegra_powergate *pg,
				    bool disable_clocks)
{
	int err;

	err = tegra_powergate_reset_assert(pg);
	if (err)
		return err;

	usleep_range(10, 20);

	err = tegra_powergate_set(pg->id, true);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	err = tegra_powergate_enable_clocks(pg);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	err = __tegra_powergate_remove_clamping(pg->id);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	err = tegra_powergate_reset_deassert(pg);
	if (err)
		goto powergate_off;

	usleep_range(10, 20);

	if (disable_clocks)
		tegra_powergate_disable_clocks(pg);

	return 0;

disable_clks:
	tegra_powergate_disable_clocks(pg);
	usleep_range(10, 20);

powergate_off:
	tegra_powergate_set(pg->id, false);

	return err;
}

static int tegra_powergate_power_down(struct tegra_powergate *pg)
{
	int err;

	err = tegra_powergate_enable_clocks(pg);
	if (err)
		return err;

	usleep_range(10, 20);

	err = tegra_powergate_reset_assert(pg);
	if (err)
		goto disable_clks;

	usleep_range(10, 20);

	tegra_powergate_disable_clocks(pg);

	usleep_range(10, 20);

	err = tegra_powergate_set(pg->id, false);
	if (err)
		goto assert_resets;

	return 0;

assert_resets:
	tegra_powergate_enable_clocks(pg);
	usleep_range(10, 20);
	tegra_powergate_reset_deassert(pg);
	usleep_range(10, 20);

disable_clks:
	tegra_powergate_disable_clocks(pg);

	return err;
}

static int tegra_genpd_power_on(struct generic_pm_domain *domain)
{
	struct tegra_powergate *pg = to_powergate(domain);
	int err;

	err = tegra_powergate_power_up(pg, true);
	if (err)
		pr_err("failed to turn on PM domain %s: %d\n", pg->genpd.name,
		       err);

	return err;
}

static int tegra_genpd_power_off(struct generic_pm_domain *domain)
{
	struct tegra_powergate *pg = to_powergate(domain);
	int err;

	err = tegra_powergate_power_down(pg);
	if (err)
		pr_err("failed to turn off PM domain %s: %d\n",
		       pg->genpd.name, err);

	return err;
}

/**
 * tegra_powergate_power_on() - power on partition
 * @id: partition ID
 */
int tegra_powergate_power_on(unsigned int id)
{
	if (!tegra_powergate_is_available(id))
		return -EINVAL;

	return tegra_powergate_set(id, true);
}

/**
 * tegra_powergate_power_off() - power off partition
 * @id: partition ID
 */
int tegra_powergate_power_off(unsigned int id)
{
	if (!tegra_powergate_is_available(id))
		return -EINVAL;

	return tegra_powergate_set(id, false);
}
EXPORT_SYMBOL(tegra_powergate_power_off);

/**
 * tegra_powergate_is_powered() - check if partition is powered
 * @id: partition ID
 */
int tegra_powergate_is_powered(unsigned int id)
{
	int status;

	if (!tegra_powergate_is_valid(id))
		return -EINVAL;

	mutex_lock(&pmc->powergates_lock);
	status = tegra_powergate_state(id);
	mutex_unlock(&pmc->powergates_lock);

	return status;
}

/**
 * tegra_powergate_remove_clamping() - remove power clamps for partition
 * @id: partition ID
 */
int tegra_powergate_remove_clamping(unsigned int id)
{
	if (!tegra_powergate_is_available(id))
		return -EINVAL;

	return __tegra_powergate_remove_clamping(id);
}
EXPORT_SYMBOL(tegra_powergate_remove_clamping);

/**
 * tegra_powergate_sequence_power_up() - power up partition
 * @id: partition ID
 * @clk: clock for partition
 * @rst: reset for partition
 *
 * Must be called with clk disabled, and returns with clk enabled.
 */
int tegra_powergate_sequence_power_up(unsigned int id, struct clk *clk,
				      struct reset_control *rst)
{
	struct tegra_powergate pg;
	int err;

	if (!tegra_powergate_is_available(id))
		return -EINVAL;

	pg.id = id;
	pg.clks = &clk;
	pg.num_clks = 1;
	pg.resets = &rst;
	pg.num_resets = 1;

	err = tegra_powergate_power_up(&pg, false);
	if (err)
		pr_err("failed to turn on partition %d: %d\n", id, err);

	return err;
}
EXPORT_SYMBOL(tegra_powergate_sequence_power_up);

#ifdef CONFIG_SMP
/**
 * tegra_get_cpu_powergate_id() - convert from CPU ID to partition ID
 * @cpuid: CPU partition ID
 *
 * Returns the partition ID corresponding to the CPU partition ID or a
 * negative error code on failure.
 */
static int tegra_get_cpu_powergate_id(unsigned int cpuid)
{
	if (pmc->soc && cpuid < pmc->soc->num_cpu_powergates)
		return pmc->soc->cpu_powergates[cpuid];

	return -EINVAL;
}

/**
 * tegra_pmc_cpu_is_powered() - check if CPU partition is powered
 * @cpuid: CPU partition ID
 */
bool tegra_pmc_cpu_is_powered(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(cpuid);
	if (id < 0)
		return false;

	return tegra_powergate_is_powered(id);
}

/**
 * tegra_pmc_cpu_power_on() - power on CPU partition
 * @cpuid: CPU partition ID
 */
int tegra_pmc_cpu_power_on(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(cpuid);
	if (id < 0)
		return id;

	return tegra_powergate_set(id, true);
}

/**
 * tegra_pmc_cpu_remove_clamping() - remove power clamps for CPU partition
 * @cpuid: CPU partition ID
 */
int tegra_pmc_cpu_remove_clamping(unsigned int cpuid)
{
	int id;

	id = tegra_get_cpu_powergate_id(cpuid);
	if (id < 0)
		return id;

	return tegra_powergate_remove_clamping(id);
}
#endif /* CONFIG_SMP */

static int tegra_pmc_restart_notify(struct notifier_block *this,
				    unsigned long action, void *data)
{
	const char *cmd = data;
	u32 value;

	value = tegra_pmc_readl(PMC_SCRATCH0);
	value &= ~PMC_SCRATCH0_MODE_MASK;

	if (cmd) {
		if (strcmp(cmd, "recovery") == 0)
			value |= PMC_SCRATCH0_MODE_RECOVERY;

		if (strcmp(cmd, "bootloader") == 0)
			value |= PMC_SCRATCH0_MODE_BOOTLOADER;

		if (strcmp(cmd, "forced-recovery") == 0)
			value |= PMC_SCRATCH0_MODE_RCM;
	}

	tegra_pmc_writel(value, PMC_SCRATCH0);

	/* reset everything but PMC_SCRATCH0 and PMC_RST_STATUS */
	value = tegra_pmc_readl(PMC_CNTRL);
	value |= PMC_CNTRL_MAIN_RST;
	tegra_pmc_writel(value, PMC_CNTRL);

	return NOTIFY_DONE;
}

static struct notifier_block tegra_pmc_restart_handler = {
	.notifier_call = tegra_pmc_restart_notify,
	.priority = 128,
};

static int powergate_show(struct seq_file *s, void *data)
{
	unsigned int i;
	int status;

	seq_printf(s, " powergate powered\n");
	seq_printf(s, "------------------\n");

	for (i = 0; i < pmc->soc->num_powergates; i++) {
		status = tegra_powergate_is_powered(i);
		if (status < 0)
			continue;

		seq_printf(s, " %9s %7s\n", pmc->soc->powergates[i],
			   status ? "yes" : "no");
	}

	return 0;
}

static int powergate_open(struct inode *inode, struct file *file)
{
	return single_open(file, powergate_show, inode->i_private);
}

static const struct file_operations powergate_fops = {
	.open = powergate_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int tegra_powergate_debugfs_init(void)
{
	pmc->debugfs = debugfs_create_file("powergate", S_IRUGO, NULL, NULL,
					   &powergate_fops);
	if (!pmc->debugfs)
		return -ENOMEM;

	return 0;
}

static int tegra_powergate_of_get_clks(struct tegra_powergate *pg,
				       struct device_node *np)
{
	struct clk *clk;
	unsigned int i, count;
	int err;

	count = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (count == 0)
		return -ENODEV;

	pg->clks = kcalloc(count, sizeof(clk), GFP_KERNEL);
	if (!pg->clks)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		pg->clks[i] = of_clk_get(np, i);
		if (IS_ERR(pg->clks[i])) {
			err = PTR_ERR(pg->clks[i]);
			goto err;
		}
	}

	pg->num_clks = count;

	return 0;

err:
	while (i--)
		clk_put(pg->clks[i]);

	kfree(pg->clks);

	return err;
}

static int tegra_powergate_of_get_resets(struct tegra_powergate *pg,
					 struct device_node *np, bool off)
{
	struct reset_control *rst;
	unsigned int i, count;
	int err;

	count = of_count_phandle_with_args(np, "resets", "#reset-cells");
	if (count == 0)
		return -ENODEV;

	pg->resets = kcalloc(count, sizeof(rst), GFP_KERNEL);
	if (!pg->resets)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		pg->resets[i] = of_reset_control_get_by_index(np, i);
		if (IS_ERR(pg->resets[i])) {
			err = PTR_ERR(pg->resets[i]);
			goto error;
		}

		if (off)
			err = reset_control_assert(pg->resets[i]);
		else
			err = reset_control_deassert(pg->resets[i]);

		if (err) {
			reset_control_put(pg->resets[i]);
			goto error;
		}
	}

	pg->num_resets = count;

	return 0;

error:
	while (i--)
		reset_control_put(pg->resets[i]);

	kfree(pg->resets);

	return err;
}

static void tegra_powergate_add(struct tegra_pmc *pmc, struct device_node *np)
{
	struct tegra_powergate *pg;
	int id, err;
	bool off;

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return;

	id = tegra_powergate_lookup(pmc, np->name);
	if (id < 0) {
		pr_err("powergate lookup failed for %s: %d\n", np->name, id);
		goto free_mem;
	}

	/*
	 * Clear the bit for this powergate so it cannot be managed
	 * directly via the legacy APIs for controlling powergates.
	 */
	clear_bit(id, pmc->powergates_available);

	pg->id = id;
	pg->genpd.name = np->name;
	pg->genpd.power_off = tegra_genpd_power_off;
	pg->genpd.power_on = tegra_genpd_power_on;
	pg->pmc = pmc;

	off = !tegra_powergate_is_powered(pg->id);

	err = tegra_powergate_of_get_clks(pg, np);
	if (err < 0) {
		pr_err("failed to get clocks for %s: %d\n", np->name, err);
		goto set_available;
	}

	err = tegra_powergate_of_get_resets(pg, np, off);
	if (err < 0) {
		pr_err("failed to get resets for %s: %d\n", np->name, err);
		goto remove_clks;
	}

	if (!IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS)) {
		if (off)
			WARN_ON(tegra_powergate_power_up(pg, true));

		goto remove_resets;
	}

	/*
	 * FIXME: If XHCI is enabled for Tegra, then power-up the XUSB
	 * host and super-speed partitions. Once the XHCI driver
	 * manages the partitions itself this code can be removed. Note
	 * that we don't register these partitions with the genpd core
	 * to avoid it from powering down the partitions as they appear
	 * to be unused.
	 */
	if (IS_ENABLED(CONFIG_USB_XHCI_TEGRA) &&
	    (id == TEGRA_POWERGATE_XUSBA || id == TEGRA_POWERGATE_XUSBC)) {
		if (off)
			WARN_ON(tegra_powergate_power_up(pg, true));

		goto remove_resets;
	}

	err = pm_genpd_init(&pg->genpd, NULL, off);
	if (err < 0) {
		pr_err("failed to initialise PM domain %s: %d\n", np->name,
		       err);
		goto remove_resets;
	}

	err = of_genpd_add_provider_simple(np, &pg->genpd);
	if (err < 0) {
		pr_err("failed to add PM domain provider for %s: %d\n",
		       np->name, err);
		goto remove_genpd;
	}

	pr_debug("added PM domain %s\n", pg->genpd.name);

	return;

remove_genpd:
	pm_genpd_remove(&pg->genpd);

remove_resets:
	while (pg->num_resets--)
		reset_control_put(pg->resets[pg->num_resets]);

	kfree(pg->resets);

remove_clks:
	while (pg->num_clks--)
		clk_put(pg->clks[pg->num_clks]);

	kfree(pg->clks);

set_available:
	set_bit(id, pmc->powergates_available);

free_mem:
	kfree(pg);
}

static void tegra_powergate_init(struct tegra_pmc *pmc,
				 struct device_node *parent)
{
	struct device_node *np, *child;
	unsigned int i;

	/* Create a bitmap of the available and valid partitions */
	for (i = 0; i < pmc->soc->num_powergates; i++)
		if (pmc->soc->powergates[i])
			set_bit(i, pmc->powergates_available);

	np = of_get_child_by_name(parent, "powergates");
	if (!np)
		return;

	for_each_child_of_node(np, child) {
		tegra_powergate_add(pmc, child);
		of_node_put(child);
	}

	of_node_put(np);
}

static const struct tegra_io_pad_soc *
tegra_io_pad_find(struct tegra_pmc *pmc, enum tegra_io_pad id)
{
	unsigned int i;

	for (i = 0; i < pmc->soc->num_io_pads; i++)
		if (pmc->soc->io_pads[i].id == id)
			return &pmc->soc->io_pads[i];

	return NULL;
}

static int tegra_io_pad_prepare(enum tegra_io_pad id, unsigned long *request,
				unsigned long *status, u32 *mask)
{
	const struct tegra_io_pad_soc *pad;
	unsigned long rate, value;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad) {
		pr_err("invalid I/O pad ID %u\n", id);
		return -ENOENT;
	}

	if (pad->dpd == UINT_MAX)
		return -ENOTSUPP;

	*mask = BIT(pad->dpd % 32);

	if (pad->dpd < 32) {
		*status = IO_DPD_STATUS;
		*request = IO_DPD_REQ;
	} else {
		*status = IO_DPD2_STATUS;
		*request = IO_DPD2_REQ;
	}

	rate = clk_get_rate(pmc->clk);
	if (!rate) {
		pr_err("failed to get clock rate\n");
		return -ENODEV;
	}

	tegra_pmc_writel(DPD_SAMPLE_ENABLE, DPD_SAMPLE);

	/* must be at least 200 ns, in APB (PCLK) clock cycles */
	value = DIV_ROUND_UP(1000000000, rate);
	value = DIV_ROUND_UP(200, value);
	tegra_pmc_writel(value, SEL_DPD_TIM);

	return 0;
}

static int tegra_io_pad_poll(unsigned long offset, u32 mask,
			     u32 val, unsigned long timeout)
{
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_after(timeout, jiffies)) {
		value = tegra_pmc_readl(offset);
		if ((value & mask) == val)
			return 0;

		usleep_range(250, 1000);
	}

	return -ETIMEDOUT;
}

static void tegra_io_pad_unprepare(void)
{
	tegra_pmc_writel(DPD_SAMPLE_DISABLE, DPD_SAMPLE);
}

/**
 * tegra_io_pad_power_enable() - enable power to I/O pad
 * @id: Tegra I/O pad ID for which to enable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra_io_pad_power_enable(enum tegra_io_pad id)
{
	unsigned long request, status;
	u32 mask;
	int err;

	mutex_lock(&pmc->powergates_lock);

	err = tegra_io_pad_prepare(id, &request, &status, &mask);
	if (err < 0) {
		pr_err("failed to prepare I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_pmc_writel(IO_DPD_REQ_CODE_OFF | mask, request);

	err = tegra_io_pad_poll(status, mask, 0, 250);
	if (err < 0) {
		pr_err("failed to enable I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_io_pad_unprepare();

unlock:
	mutex_unlock(&pmc->powergates_lock);
	return err;
}
EXPORT_SYMBOL(tegra_io_pad_power_enable);

/**
 * tegra_io_pad_power_disable() - disable power to I/O pad
 * @id: Tegra I/O pad ID for which to disable power
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int tegra_io_pad_power_disable(enum tegra_io_pad id)
{
	unsigned long request, status;
	u32 mask;
	int err;

	mutex_lock(&pmc->powergates_lock);

	err = tegra_io_pad_prepare(id, &request, &status, &mask);
	if (err < 0) {
		pr_err("failed to prepare I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_pmc_writel(IO_DPD_REQ_CODE_ON | mask, request);

	err = tegra_io_pad_poll(status, mask, mask, 250);
	if (err < 0) {
		pr_err("failed to disable I/O pad: %d\n", err);
		goto unlock;
	}

	tegra_io_pad_unprepare();

unlock:
	mutex_unlock(&pmc->powergates_lock);
	return err;
}
EXPORT_SYMBOL(tegra_io_pad_power_disable);

int tegra_io_pad_set_voltage(enum tegra_io_pad id,
			     enum tegra_io_pad_voltage voltage)
{
	const struct tegra_io_pad_soc *pad;
	u32 value;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad)
		return -ENOENT;

	if (pad->voltage == UINT_MAX)
		return -ENOTSUPP;

	mutex_lock(&pmc->powergates_lock);

	/* write-enable PMC_PWR_DET_VALUE[pad->voltage] */
	value = tegra_pmc_readl(PMC_PWR_DET);
	value |= BIT(pad->voltage);
	tegra_pmc_writel(value, PMC_PWR_DET);

	/* update I/O voltage */
	value = tegra_pmc_readl(PMC_PWR_DET_VALUE);

	if (voltage == TEGRA_IO_PAD_1800000UV)
		value &= ~BIT(pad->voltage);
	else
		value |= BIT(pad->voltage);

	tegra_pmc_writel(value, PMC_PWR_DET_VALUE);

	mutex_unlock(&pmc->powergates_lock);

	usleep_range(100, 250);

	return 0;
}
EXPORT_SYMBOL(tegra_io_pad_set_voltage);

int tegra_io_pad_get_voltage(enum tegra_io_pad id)
{
	const struct tegra_io_pad_soc *pad;
	u32 value;

	pad = tegra_io_pad_find(pmc, id);
	if (!pad)
		return -ENOENT;

	if (pad->voltage == UINT_MAX)
		return -ENOTSUPP;

	value = tegra_pmc_readl(PMC_PWR_DET_VALUE);

	if ((value & BIT(pad->voltage)) == 0)
		return TEGRA_IO_PAD_1800000UV;

	return TEGRA_IO_PAD_3300000UV;
}
EXPORT_SYMBOL(tegra_io_pad_get_voltage);

/**
 * tegra_io_rail_power_on() - enable power to I/O rail
 * @id: Tegra I/O pad ID for which to enable power
 *
 * See also: tegra_io_pad_power_enable()
 */
int tegra_io_rail_power_on(unsigned int id)
{
	return tegra_io_pad_power_enable(id);
}
EXPORT_SYMBOL(tegra_io_rail_power_on);

/**
 * tegra_io_rail_power_off() - disable power to I/O rail
 * @id: Tegra I/O pad ID for which to disable power
 *
 * See also: tegra_io_pad_power_disable()
 */
int tegra_io_rail_power_off(unsigned int id)
{
	return tegra_io_pad_power_disable(id);
}
EXPORT_SYMBOL(tegra_io_rail_power_off);

#ifdef CONFIG_PM_SLEEP
enum tegra_suspend_mode tegra_pmc_get_suspend_mode(void)
{
	return pmc->suspend_mode;
}

void tegra_pmc_set_suspend_mode(enum tegra_suspend_mode mode)
{
	if (mode < TEGRA_SUSPEND_NONE || mode >= TEGRA_MAX_SUSPEND_MODE)
		return;

	pmc->suspend_mode = mode;
}

void tegra_pmc_enter_suspend_mode(enum tegra_suspend_mode mode)
{
	unsigned long long rate = 0;
	u32 value;

	switch (mode) {
	case TEGRA_SUSPEND_LP1:
		rate = 32768;
		break;

	case TEGRA_SUSPEND_LP2:
		rate = clk_get_rate(pmc->clk);
		break;

	default:
		break;
	}

	if (WARN_ON_ONCE(rate == 0))
		rate = 100000000;

	if (rate != pmc->rate) {
		u64 ticks;

		ticks = pmc->cpu_good_time * rate + USEC_PER_SEC - 1;
		do_div(ticks, USEC_PER_SEC);
		tegra_pmc_writel(ticks, PMC_CPUPWRGOOD_TIMER);

		ticks = pmc->cpu_off_time * rate + USEC_PER_SEC - 1;
		do_div(ticks, USEC_PER_SEC);
		tegra_pmc_writel(ticks, PMC_CPUPWROFF_TIMER);

		wmb();

		pmc->rate = rate;
	}

	value = tegra_pmc_readl(PMC_CNTRL);
	value &= ~PMC_CNTRL_SIDE_EFFECT_LP0;
	value |= PMC_CNTRL_CPU_PWRREQ_OE;
	tegra_pmc_writel(value, PMC_CNTRL);
}
#endif

static int tegra_pmc_parse_dt(struct tegra_pmc *pmc, struct device_node *np)
{
	u32 value, values[2];

	if (of_property_read_u32(np, "nvidia,suspend-mode", &value)) {
	} else {
		switch (value) {
		case 0:
			pmc->suspend_mode = TEGRA_SUSPEND_LP0;
			break;

		case 1:
			pmc->suspend_mode = TEGRA_SUSPEND_LP1;
			break;

		case 2:
			pmc->suspend_mode = TEGRA_SUSPEND_LP2;
			break;

		default:
			pmc->suspend_mode = TEGRA_SUSPEND_NONE;
			break;
		}
	}

	pmc->suspend_mode = tegra_pm_validate_suspend_mode(pmc->suspend_mode);

	if (of_property_read_u32(np, "nvidia,cpu-pwr-good-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->cpu_good_time = value;

	if (of_property_read_u32(np, "nvidia,cpu-pwr-off-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->cpu_off_time = value;

	if (of_property_read_u32_array(np, "nvidia,core-pwr-good-time",
				       values, ARRAY_SIZE(values)))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->core_osc_time = values[0];
	pmc->core_pmu_time = values[1];

	if (of_property_read_u32(np, "nvidia,core-pwr-off-time", &value))
		pmc->suspend_mode = TEGRA_SUSPEND_NONE;

	pmc->core_off_time = value;

	pmc->corereq_high = of_property_read_bool(np,
				"nvidia,core-power-req-active-high");

	pmc->sysclkreq_high = of_property_read_bool(np,
				"nvidia,sys-clock-req-active-high");

	pmc->combined_req = of_property_read_bool(np,
				"nvidia,combined-power-req");

	pmc->cpu_pwr_good_en = of_property_read_bool(np,
				"nvidia,cpu-pwr-good-en");

	if (of_property_read_u32_array(np, "nvidia,lp0-vec", values,
				       ARRAY_SIZE(values)))
		if (pmc->suspend_mode == TEGRA_SUSPEND_LP0)
			pmc->suspend_mode = TEGRA_SUSPEND_LP1;

	pmc->lp0_vec_phys = values[0];
	pmc->lp0_vec_size = values[1];

	return 0;
}

static void tegra_pmc_init(struct tegra_pmc *pmc)
{
	u32 value;

	/* Always enable CPU power request */
	value = tegra_pmc_readl(PMC_CNTRL);
	value |= PMC_CNTRL_CPU_PWRREQ_OE;
	tegra_pmc_writel(value, PMC_CNTRL);

	value = tegra_pmc_readl(PMC_CNTRL);

	if (pmc->sysclkreq_high)
		value &= ~PMC_CNTRL_SYSCLK_POLARITY;
	else
		value |= PMC_CNTRL_SYSCLK_POLARITY;

	/* configure the output polarity while the request is tristated */
	tegra_pmc_writel(value, PMC_CNTRL);

	/* now enable the request */
	value = tegra_pmc_readl(PMC_CNTRL);
	value |= PMC_CNTRL_SYSCLK_OE;
	tegra_pmc_writel(value, PMC_CNTRL);
}

static void tegra_pmc_init_tsense_reset(struct tegra_pmc *pmc)
{
	static const char disabled[] = "emergency thermal reset disabled";
	u32 pmu_addr, ctrl_id, reg_addr, reg_data, pinmux;
	struct device *dev = pmc->dev;
	struct device_node *np;
	u32 value, checksum;

	if (!pmc->soc->has_tsense_reset)
		return;

	np = of_find_node_by_name(pmc->dev->of_node, "i2c-thermtrip");
	if (!np) {
		dev_warn(dev, "i2c-thermtrip node not found, %s.\n", disabled);
		return;
	}

	if (of_property_read_u32(np, "nvidia,i2c-controller-id", &ctrl_id)) {
		dev_err(dev, "I2C controller ID missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,bus-addr", &pmu_addr)) {
		dev_err(dev, "nvidia,bus-addr missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,reg-addr", &reg_addr)) {
		dev_err(dev, "nvidia,reg-addr missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,reg-data", &reg_data)) {
		dev_err(dev, "nvidia,reg-data missing, %s.\n", disabled);
		goto out;
	}

	if (of_property_read_u32(np, "nvidia,pinmux-id", &pinmux))
		pinmux = 0;

	value = tegra_pmc_readl(PMC_SENSOR_CTRL);
	value |= PMC_SENSOR_CTRL_SCRATCH_WRITE;
	tegra_pmc_writel(value, PMC_SENSOR_CTRL);

	value = (reg_data << PMC_SCRATCH54_DATA_SHIFT) |
		(reg_addr << PMC_SCRATCH54_ADDR_SHIFT);
	tegra_pmc_writel(value, PMC_SCRATCH54);

	value = PMC_SCRATCH55_RESET_TEGRA;
	value |= ctrl_id << PMC_SCRATCH55_CNTRL_ID_SHIFT;
	value |= pinmux << PMC_SCRATCH55_PINMUX_SHIFT;
	value |= pmu_addr << PMC_SCRATCH55_I2CSLV1_SHIFT;

	/*
	 * Calculate checksum of SCRATCH54, SCRATCH55 fields. Bits 23:16 will
	 * contain the checksum and are currently zero, so they are not added.
	 */
	checksum = reg_addr + reg_data + (value & 0xff) + ((value >> 8) & 0xff)
		+ ((value >> 24) & 0xff);
	checksum &= 0xff;
	checksum = 0x100 - checksum;

	value |= checksum << PMC_SCRATCH55_CHECKSUM_SHIFT;

	tegra_pmc_writel(value, PMC_SCRATCH55);

	value = tegra_pmc_readl(PMC_SENSOR_CTRL);
	value |= PMC_SENSOR_CTRL_ENABLE_RST;
	tegra_pmc_writel(value, PMC_SENSOR_CTRL);

	dev_info(pmc->dev, "emergency thermal reset enabled\n");

out:
	of_node_put(np);
}

static int tegra_pmc_probe(struct platform_device *pdev)
{
	void __iomem *base;
	struct resource *res;
	int err;

	/*
	 * Early initialisation should have configured an initial
	 * register mapping and setup the soc data pointer. If these
	 * are not valid then something went badly wrong!
	 */
	if (WARN_ON(!pmc->base || !pmc->soc))
		return -ENODEV;

	err = tegra_pmc_parse_dt(pmc, pdev->dev.of_node);
	if (err < 0)
		return err;

	/* take over the memory region from the early initialization */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pmc->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pmc->clk)) {
		err = PTR_ERR(pmc->clk);
		dev_err(&pdev->dev, "failed to get pclk: %d\n", err);
		return err;
	}

	pmc->dev = &pdev->dev;

	tegra_pmc_init(pmc);

	tegra_pmc_init_tsense_reset(pmc);

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_powergate_debugfs_init();
		if (err < 0)
			return err;
	}

	err = register_restart_handler(&tegra_pmc_restart_handler);
	if (err) {
		debugfs_remove(pmc->debugfs);
		dev_err(&pdev->dev, "unable to register restart handler, %d\n",
			err);
		return err;
	}

	mutex_lock(&pmc->powergates_lock);
	iounmap(pmc->base);
	pmc->base = base;
	mutex_unlock(&pmc->powergates_lock);

	return 0;
}

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM)
static int tegra_pmc_suspend(struct device *dev)
{
	tegra_pmc_writel(virt_to_phys(tegra_resume), PMC_SCRATCH41);

	return 0;
}

static int tegra_pmc_resume(struct device *dev)
{
	tegra_pmc_writel(0x0, PMC_SCRATCH41);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tegra_pmc_pm_ops, tegra_pmc_suspend, tegra_pmc_resume);

#endif

static const char * const tegra20_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "cpu",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_L2] = "l2",
	[TEGRA_POWERGATE_MPE] = "mpe",
};

static const struct tegra_pmc_soc tegra20_pmc_soc = {
	.num_powergates = ARRAY_SIZE(tegra20_powergates),
	.powergates = tegra20_powergates,
	.num_cpu_powergates = 0,
	.cpu_powergates = NULL,
	.has_tsense_reset = false,
	.has_gpu_clamps = false,
};

static const char * const tegra30_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "cpu0",
	[TEGRA_POWERGATE_3D] = "3d0",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_L2] = "l2",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_3D1] = "3d1",
};

static const u8 tegra30_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const struct tegra_pmc_soc tegra30_pmc_soc = {
	.num_powergates = ARRAY_SIZE(tegra30_powergates),
	.powergates = tegra30_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra30_cpu_powergates),
	.cpu_powergates = tegra30_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = false,
};

static const char * const tegra114_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_C1NC] = "c1nc",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
};

static const u8 tegra114_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const struct tegra_pmc_soc tegra114_pmc_soc = {
	.num_powergates = ARRAY_SIZE(tegra114_powergates),
	.powergates = tegra114_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra114_cpu_powergates),
	.cpu_powergates = tegra114_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = false,
};

static const char * const tegra124_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_VDEC] = "vdec",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_HEG] = "heg",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CELP] = "celp",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_C1NC] = "c1nc",
	[TEGRA_POWERGATE_SOR] = "sor",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
	[TEGRA_POWERGATE_VIC] = "vic",
	[TEGRA_POWERGATE_IRAM] = "iram",
};

static const u8 tegra124_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const struct tegra_io_pad_soc tegra124_io_pads[] = {
	{ .id = TEGRA_IO_PAD_AUDIO, .dpd = 17, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_BB, .dpd = 15, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CAM, .dpd = 36, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_COMP, .dpd = 22, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIA, .dpd = 0, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIB, .dpd = 1, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIE, .dpd = 44, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSI, .dpd = 2, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSIB, .dpd = 39, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSIC, .dpd = 40, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSID, .dpd = 41, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_HDMI, .dpd = 28, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_HSIC, .dpd = 19, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_HV, .dpd = 38, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_LVDS, .dpd = 57, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_MIPI_BIAS, .dpd = 3, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_NAND, .dpd = 13, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_BIAS, .dpd = 4, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CLK1, .dpd = 5, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CLK2, .dpd = 6, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CNTRL, .dpd = 32, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_SDMMC1, .dpd = 33, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_SDMMC3, .dpd = 34, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_SDMMC4, .dpd = 35, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_SYS_DDC, .dpd = 58, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_UART, .dpd = 14, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB0, .dpd = 9, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB1, .dpd = 10, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB2, .dpd = 11, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB_BIAS, .dpd = 12, .voltage = UINT_MAX },
};

static const struct tegra_pmc_soc tegra124_pmc_soc = {
	.num_powergates = ARRAY_SIZE(tegra124_powergates),
	.powergates = tegra124_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra124_cpu_powergates),
	.cpu_powergates = tegra124_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = true,
	.num_io_pads = ARRAY_SIZE(tegra124_io_pads),
	.io_pads = tegra124_io_pads,
};

static const char * const tegra210_powergates[] = {
	[TEGRA_POWERGATE_CPU] = "crail",
	[TEGRA_POWERGATE_3D] = "3d",
	[TEGRA_POWERGATE_VENC] = "venc",
	[TEGRA_POWERGATE_PCIE] = "pcie",
	[TEGRA_POWERGATE_MPE] = "mpe",
	[TEGRA_POWERGATE_SATA] = "sata",
	[TEGRA_POWERGATE_CPU1] = "cpu1",
	[TEGRA_POWERGATE_CPU2] = "cpu2",
	[TEGRA_POWERGATE_CPU3] = "cpu3",
	[TEGRA_POWERGATE_CPU0] = "cpu0",
	[TEGRA_POWERGATE_C0NC] = "c0nc",
	[TEGRA_POWERGATE_SOR] = "sor",
	[TEGRA_POWERGATE_DIS] = "dis",
	[TEGRA_POWERGATE_DISB] = "disb",
	[TEGRA_POWERGATE_XUSBA] = "xusba",
	[TEGRA_POWERGATE_XUSBB] = "xusbb",
	[TEGRA_POWERGATE_XUSBC] = "xusbc",
	[TEGRA_POWERGATE_VIC] = "vic",
	[TEGRA_POWERGATE_IRAM] = "iram",
	[TEGRA_POWERGATE_NVDEC] = "nvdec",
	[TEGRA_POWERGATE_NVJPG] = "nvjpg",
	[TEGRA_POWERGATE_AUD] = "aud",
	[TEGRA_POWERGATE_DFD] = "dfd",
	[TEGRA_POWERGATE_VE2] = "ve2",
};

static const u8 tegra210_cpu_powergates[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const struct tegra_io_pad_soc tegra210_io_pads[] = {
	{ .id = TEGRA_IO_PAD_AUDIO, .dpd = 17, .voltage = 5 },
	{ .id = TEGRA_IO_PAD_AUDIO_HV, .dpd = 61, .voltage = 18 },
	{ .id = TEGRA_IO_PAD_CAM, .dpd = 36, .voltage = 10 },
	{ .id = TEGRA_IO_PAD_CSIA, .dpd = 0, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIB, .dpd = 1, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIC, .dpd = 42, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSID, .dpd = 43, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIE, .dpd = 44, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_CSIF, .dpd = 45, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DBG, .dpd = 25, .voltage = 19 },
	{ .id = TEGRA_IO_PAD_DEBUG_NONAO, .dpd = 26, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DMIC, .dpd = 50, .voltage = 20 },
	{ .id = TEGRA_IO_PAD_DP, .dpd = 51, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSI, .dpd = 2, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSIB, .dpd = 39, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSIC, .dpd = 40, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_DSID, .dpd = 41, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_EMMC, .dpd = 35, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_EMMC2, .dpd = 37, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_GPIO, .dpd = 27, .voltage = 21 },
	{ .id = TEGRA_IO_PAD_HDMI, .dpd = 28, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_HSIC, .dpd = 19, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_LVDS, .dpd = 57, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_MIPI_BIAS, .dpd = 3, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_BIAS, .dpd = 4, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CLK1, .dpd = 5, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CLK2, .dpd = 6, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_PEX_CNTRL, .dpd = UINT_MAX, .voltage = 11 },
	{ .id = TEGRA_IO_PAD_SDMMC1, .dpd = 33, .voltage = 12 },
	{ .id = TEGRA_IO_PAD_SDMMC3, .dpd = 34, .voltage = 13 },
	{ .id = TEGRA_IO_PAD_SPI, .dpd = 46, .voltage = 22 },
	{ .id = TEGRA_IO_PAD_SPI_HV, .dpd = 47, .voltage = 23 },
	{ .id = TEGRA_IO_PAD_UART, .dpd = 14, .voltage = 2 },
	{ .id = TEGRA_IO_PAD_USB0, .dpd = 9, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB1, .dpd = 10, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB2, .dpd = 11, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB3, .dpd = 18, .voltage = UINT_MAX },
	{ .id = TEGRA_IO_PAD_USB_BIAS, .dpd = 12, .voltage = UINT_MAX },
};

static const struct tegra_pmc_soc tegra210_pmc_soc = {
	.num_powergates = ARRAY_SIZE(tegra210_powergates),
	.powergates = tegra210_powergates,
	.num_cpu_powergates = ARRAY_SIZE(tegra210_cpu_powergates),
	.cpu_powergates = tegra210_cpu_powergates,
	.has_tsense_reset = true,
	.has_gpu_clamps = true,
	.num_io_pads = ARRAY_SIZE(tegra210_io_pads),
	.io_pads = tegra210_io_pads,
};

static const struct of_device_id tegra_pmc_match[] = {
	{ .compatible = "nvidia,tegra210-pmc", .data = &tegra210_pmc_soc },
	{ .compatible = "nvidia,tegra132-pmc", .data = &tegra124_pmc_soc },
	{ .compatible = "nvidia,tegra124-pmc", .data = &tegra124_pmc_soc },
	{ .compatible = "nvidia,tegra114-pmc", .data = &tegra114_pmc_soc },
	{ .compatible = "nvidia,tegra30-pmc", .data = &tegra30_pmc_soc },
	{ .compatible = "nvidia,tegra20-pmc", .data = &tegra20_pmc_soc },
	{ }
};

static struct platform_driver tegra_pmc_driver = {
	.driver = {
		.name = "tegra-pmc",
		.suppress_bind_attrs = true,
		.of_match_table = tegra_pmc_match,
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_ARM)
		.pm = &tegra_pmc_pm_ops,
#endif
	},
	.probe = tegra_pmc_probe,
};
builtin_platform_driver(tegra_pmc_driver);

/*
 * Early initialization to allow access to registers in the very early boot
 * process.
 */
static int __init tegra_pmc_early_init(void)
{
	const struct of_device_id *match;
	struct device_node *np;
	struct resource regs;
	bool invert;
	u32 value;

	mutex_init(&pmc->powergates_lock);

	np = of_find_matching_node_and_match(NULL, tegra_pmc_match, &match);
	if (!np) {
		/*
		 * Fall back to legacy initialization for 32-bit ARM only. All
		 * 64-bit ARM device tree files for Tegra are required to have
		 * a PMC node.
		 *
		 * This is for backwards-compatibility with old device trees
		 * that didn't contain a PMC node. Note that in this case the
		 * SoC data can't be matched and therefore powergating is
		 * disabled.
		 */
		if (IS_ENABLED(CONFIG_ARM) && soc_is_tegra()) {
			pr_warn("DT node not found, powergating disabled\n");

			regs.start = 0x7000e400;
			regs.end = 0x7000e7ff;
			regs.flags = IORESOURCE_MEM;

			pr_warn("Using memory region %pR\n", &regs);
		} else {
			/*
			 * At this point we're not running on Tegra, so play
			 * nice with multi-platform kernels.
			 */
			return 0;
		}
	} else {
		/*
		 * Extract information from the device tree if we've found a
		 * matching node.
		 */
		if (of_address_to_resource(np, 0, &regs) < 0) {
			pr_err("failed to get PMC registers\n");
			of_node_put(np);
			return -ENXIO;
		}
	}

	pmc->base = ioremap_nocache(regs.start, resource_size(&regs));
	if (!pmc->base) {
		pr_err("failed to map PMC registers\n");
		of_node_put(np);
		return -ENXIO;
	}

	if (np) {
		pmc->soc = match->data;

		tegra_powergate_init(pmc, np);

		/*
		 * Invert the interrupt polarity if a PMC device tree node
		 * exists and contains the nvidia,invert-interrupt property.
		 */
		invert = of_property_read_bool(np, "nvidia,invert-interrupt");

		value = tegra_pmc_readl(PMC_CNTRL);

		if (invert)
			value |= PMC_CNTRL_INTR_POLARITY;
		else
			value &= ~PMC_CNTRL_INTR_POLARITY;

		tegra_pmc_writel(value, PMC_CNTRL);

		of_node_put(np);
	}

	return 0;
}
early_initcall(tegra_pmc_early_init);
