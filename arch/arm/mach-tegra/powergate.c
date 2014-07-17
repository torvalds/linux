/*
 * drivers/powergate/tegra-powergate.c
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

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <soc/tegra/fuse.h>
#include <soc/tegra/powergate.h>

#include "iomap.h"

#define DPD_SAMPLE		0x020
#define  DPD_SAMPLE_ENABLE	(1 << 0)
#define  DPD_SAMPLE_DISABLE	(0 << 0)

#define PWRGATE_TOGGLE		0x30
#define  PWRGATE_TOGGLE_START	(1 << 8)

#define REMOVE_CLAMPING		0x34

#define PWRGATE_STATUS		0x38

#define IO_DPD_REQ		0x1b8
#define  IO_DPD_REQ_CODE_IDLE	(0 << 30)
#define  IO_DPD_REQ_CODE_OFF	(1 << 30)
#define  IO_DPD_REQ_CODE_ON	(2 << 30)
#define  IO_DPD_REQ_CODE_MASK	(3 << 30)

#define IO_DPD_STATUS		0x1bc
#define IO_DPD2_REQ		0x1c0
#define IO_DPD2_STATUS		0x1c4
#define SEL_DPD_TIM		0x1c8

#define GPU_RG_CNTRL		0x2d4

static int tegra_num_powerdomains;
static int tegra_num_cpu_domains;
static const u8 *tegra_cpu_domains;

static const u8 tegra30_cpu_domains[] = {
	TEGRA_POWERGATE_CPU,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const u8 tegra114_cpu_domains[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static const u8 tegra124_cpu_domains[] = {
	TEGRA_POWERGATE_CPU0,
	TEGRA_POWERGATE_CPU1,
	TEGRA_POWERGATE_CPU2,
	TEGRA_POWERGATE_CPU3,
};

static DEFINE_SPINLOCK(tegra_powergate_lock);

static void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);

static u32 pmc_read(unsigned long reg)
{
	return readl(pmc + reg);
}

static void pmc_write(u32 val, unsigned long reg)
{
	writel(val, pmc + reg);
}

static int tegra_powergate_set(int id, bool new_state)
{
	bool status;
	unsigned long flags;

	spin_lock_irqsave(&tegra_powergate_lock, flags);

	status = pmc_read(PWRGATE_STATUS) & (1 << id);

	if (status == new_state) {
		spin_unlock_irqrestore(&tegra_powergate_lock, flags);
		return 0;
	}

	pmc_write(PWRGATE_TOGGLE_START | id, PWRGATE_TOGGLE);

	spin_unlock_irqrestore(&tegra_powergate_lock, flags);

	return 0;
}

int tegra_powergate_power_on(int id)
{
	if (id < 0 || id >= tegra_num_powerdomains)
		return -EINVAL;

	return tegra_powergate_set(id, true);
}

int tegra_powergate_power_off(int id)
{
	if (id < 0 || id >= tegra_num_powerdomains)
		return -EINVAL;

	return tegra_powergate_set(id, false);
}
EXPORT_SYMBOL(tegra_powergate_power_off);

int tegra_powergate_is_powered(int id)
{
	u32 status;

	if (id < 0 || id >= tegra_num_powerdomains)
		return -EINVAL;

	status = pmc_read(PWRGATE_STATUS) & (1 << id);
	return !!status;
}

int tegra_powergate_remove_clamping(int id)
{
	u32 mask;

	if (id < 0 || id >= tegra_num_powerdomains)
		return -EINVAL;

	/*
	 * The Tegra124 GPU has a separate register (with different semantics)
	 * to remove clamps.
	 */
	if (tegra_get_chip_id() == TEGRA124) {
		if (id == TEGRA_POWERGATE_3D) {
			pmc_write(0, GPU_RG_CNTRL);
			return 0;
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

	pmc_write(mask, REMOVE_CLAMPING);

	return 0;
}
EXPORT_SYMBOL(tegra_powergate_remove_clamping);

/* Must be called with clk disabled, and returns with clk enabled */
int tegra_powergate_sequence_power_up(int id, struct clk *clk,
					struct reset_control *rst)
{
	int ret;

	reset_control_assert(rst);

	ret = tegra_powergate_power_on(id);
	if (ret)
		goto err_power;

	ret = clk_prepare_enable(clk);
	if (ret)
		goto err_clk;

	udelay(10);

	ret = tegra_powergate_remove_clamping(id);
	if (ret)
		goto err_clamp;

	udelay(10);
	reset_control_deassert(rst);

	return 0;

err_clamp:
	clk_disable_unprepare(clk);
err_clk:
	tegra_powergate_power_off(id);
err_power:
	return ret;
}
EXPORT_SYMBOL(tegra_powergate_sequence_power_up);

int tegra_cpu_powergate_id(int cpuid)
{
	if (cpuid > 0 && cpuid < tegra_num_cpu_domains)
		return tegra_cpu_domains[cpuid];

	return -EINVAL;
}

int __init tegra_powergate_init(void)
{
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		tegra_num_powerdomains = 7;
		break;
	case TEGRA30:
		tegra_num_powerdomains = 14;
		tegra_num_cpu_domains = 4;
		tegra_cpu_domains = tegra30_cpu_domains;
		break;
	case TEGRA114:
		tegra_num_powerdomains = 23;
		tegra_num_cpu_domains = 4;
		tegra_cpu_domains = tegra114_cpu_domains;
		break;
	case TEGRA124:
		tegra_num_powerdomains = 25;
		tegra_num_cpu_domains = 4;
		tegra_cpu_domains = tegra124_cpu_domains;
		break;
	default:
		/* Unknown Tegra variant. Disable powergating */
		tegra_num_powerdomains = 0;
		break;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static const char * const *powergate_name;

static const char * const powergate_name_t20[] = {
	[TEGRA_POWERGATE_CPU]	= "cpu",
	[TEGRA_POWERGATE_3D]	= "3d",
	[TEGRA_POWERGATE_VENC]	= "venc",
	[TEGRA_POWERGATE_VDEC]	= "vdec",
	[TEGRA_POWERGATE_PCIE]	= "pcie",
	[TEGRA_POWERGATE_L2]	= "l2",
	[TEGRA_POWERGATE_MPE]	= "mpe",
};

static const char * const powergate_name_t30[] = {
	[TEGRA_POWERGATE_CPU]	= "cpu0",
	[TEGRA_POWERGATE_3D]	= "3d0",
	[TEGRA_POWERGATE_VENC]	= "venc",
	[TEGRA_POWERGATE_VDEC]	= "vdec",
	[TEGRA_POWERGATE_PCIE]	= "pcie",
	[TEGRA_POWERGATE_L2]	= "l2",
	[TEGRA_POWERGATE_MPE]	= "mpe",
	[TEGRA_POWERGATE_HEG]	= "heg",
	[TEGRA_POWERGATE_SATA]	= "sata",
	[TEGRA_POWERGATE_CPU1]	= "cpu1",
	[TEGRA_POWERGATE_CPU2]	= "cpu2",
	[TEGRA_POWERGATE_CPU3]	= "cpu3",
	[TEGRA_POWERGATE_CELP]	= "celp",
	[TEGRA_POWERGATE_3D1]	= "3d1",
};

static const char * const powergate_name_t114[] = {
	[TEGRA_POWERGATE_CPU]	= "crail",
	[TEGRA_POWERGATE_3D]	= "3d",
	[TEGRA_POWERGATE_VENC]	= "venc",
	[TEGRA_POWERGATE_VDEC]	= "vdec",
	[TEGRA_POWERGATE_MPE]	= "mpe",
	[TEGRA_POWERGATE_HEG]	= "heg",
	[TEGRA_POWERGATE_CPU1]	= "cpu1",
	[TEGRA_POWERGATE_CPU2]	= "cpu2",
	[TEGRA_POWERGATE_CPU3]	= "cpu3",
	[TEGRA_POWERGATE_CELP]	= "celp",
	[TEGRA_POWERGATE_CPU0]	= "cpu0",
	[TEGRA_POWERGATE_C0NC]	= "c0nc",
	[TEGRA_POWERGATE_C1NC]	= "c1nc",
	[TEGRA_POWERGATE_DIS]	= "dis",
	[TEGRA_POWERGATE_DISB]	= "disb",
	[TEGRA_POWERGATE_XUSBA]	= "xusba",
	[TEGRA_POWERGATE_XUSBB]	= "xusbb",
	[TEGRA_POWERGATE_XUSBC]	= "xusbc",
};

static const char * const powergate_name_t124[] = {
	[TEGRA_POWERGATE_CPU]	= "crail",
	[TEGRA_POWERGATE_3D]	= "3d",
	[TEGRA_POWERGATE_VENC]	= "venc",
	[TEGRA_POWERGATE_PCIE]	= "pcie",
	[TEGRA_POWERGATE_VDEC]	= "vdec",
	[TEGRA_POWERGATE_L2]	= "l2",
	[TEGRA_POWERGATE_MPE]	= "mpe",
	[TEGRA_POWERGATE_HEG]	= "heg",
	[TEGRA_POWERGATE_SATA]	= "sata",
	[TEGRA_POWERGATE_CPU1]	= "cpu1",
	[TEGRA_POWERGATE_CPU2]	= "cpu2",
	[TEGRA_POWERGATE_CPU3]	= "cpu3",
	[TEGRA_POWERGATE_CELP]	= "celp",
	[TEGRA_POWERGATE_CPU0]	= "cpu0",
	[TEGRA_POWERGATE_C0NC]	= "c0nc",
	[TEGRA_POWERGATE_C1NC]	= "c1nc",
	[TEGRA_POWERGATE_SOR]	= "sor",
	[TEGRA_POWERGATE_DIS]	= "dis",
	[TEGRA_POWERGATE_DISB]	= "disb",
	[TEGRA_POWERGATE_XUSBA]	= "xusba",
	[TEGRA_POWERGATE_XUSBB]	= "xusbb",
	[TEGRA_POWERGATE_XUSBC]	= "xusbc",
	[TEGRA_POWERGATE_VIC]	= "vic",
	[TEGRA_POWERGATE_IRAM]	= "iram",
};

static int powergate_show(struct seq_file *s, void *data)
{
	int i;

	seq_printf(s, " powergate powered\n");
	seq_printf(s, "------------------\n");

	for (i = 0; i < tegra_num_powerdomains; i++) {
		if (!powergate_name[i])
			continue;

		seq_printf(s, " %9s %7s\n", powergate_name[i],
			tegra_powergate_is_powered(i) ? "yes" : "no");
	}

	return 0;
}

static int powergate_open(struct inode *inode, struct file *file)
{
	return single_open(file, powergate_show, inode->i_private);
}

static const struct file_operations powergate_fops = {
	.open		= powergate_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init tegra_powergate_debugfs_init(void)
{
	struct dentry *d;

	switch (tegra_get_chip_id()) {
	case TEGRA20:
		powergate_name = powergate_name_t20;
		break;
	case TEGRA30:
		powergate_name = powergate_name_t30;
		break;
	case TEGRA114:
		powergate_name = powergate_name_t114;
		break;
	case TEGRA124:
		powergate_name = powergate_name_t124;
		break;
	}

	if (powergate_name) {
		d = debugfs_create_file("powergate", S_IRUGO, NULL, NULL,
			&powergate_fops);
		if (!d)
			return -ENOMEM;
	}

	return 0;
}

#endif

static int tegra_io_rail_prepare(int id, unsigned long *request,
				 unsigned long *status, unsigned int *bit)
{
	unsigned long rate, value;
	struct clk *clk;

	*bit = id % 32;

	/*
	 * There are two sets of 30 bits to select IO rails, but bits 30 and
	 * 31 are control bits rather than IO rail selection bits.
	 */
	if (id > 63 || *bit == 30 || *bit == 31)
		return -EINVAL;

	if (id < 32) {
		*status = IO_DPD_STATUS;
		*request = IO_DPD_REQ;
	} else {
		*status = IO_DPD2_STATUS;
		*request = IO_DPD2_REQ;
	}

	clk = clk_get_sys(NULL, "pclk");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rate = clk_get_rate(clk);
	clk_put(clk);

	pmc_write(DPD_SAMPLE_ENABLE, DPD_SAMPLE);

	/* must be at least 200 ns, in APB (PCLK) clock cycles */
	value = DIV_ROUND_UP(1000000000, rate);
	value = DIV_ROUND_UP(200, value);
	pmc_write(value, SEL_DPD_TIM);

	return 0;
}

static int tegra_io_rail_poll(unsigned long offset, unsigned long mask,
			      unsigned long val, unsigned long timeout)
{
	unsigned long value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_after(timeout, jiffies)) {
		value = pmc_read(offset);
		if ((value & mask) == val)
			return 0;

		usleep_range(250, 1000);
	}

	return -ETIMEDOUT;
}

static void tegra_io_rail_unprepare(void)
{
	pmc_write(DPD_SAMPLE_DISABLE, DPD_SAMPLE);
}

int tegra_io_rail_power_on(int id)
{
	unsigned long request, status, value;
	unsigned int bit, mask;
	int err;

	err = tegra_io_rail_prepare(id, &request, &status, &bit);
	if (err < 0)
		return err;

	mask = 1 << bit;

	value = pmc_read(request);
	value |= mask;
	value &= ~IO_DPD_REQ_CODE_MASK;
	value |= IO_DPD_REQ_CODE_OFF;
	pmc_write(value, request);

	err = tegra_io_rail_poll(status, mask, 0, 250);
	if (err < 0)
		return err;

	tegra_io_rail_unprepare();

	return 0;
}
EXPORT_SYMBOL(tegra_io_rail_power_on);

int tegra_io_rail_power_off(int id)
{
	unsigned long request, status, value;
	unsigned int bit, mask;
	int err;

	err = tegra_io_rail_prepare(id, &request, &status, &bit);
	if (err < 0)
		return err;

	mask = 1 << bit;

	value = pmc_read(request);
	value |= mask;
	value &= ~IO_DPD_REQ_CODE_MASK;
	value |= IO_DPD_REQ_CODE_ON;
	pmc_write(value, request);

	err = tegra_io_rail_poll(status, mask, mask, 250);
	if (err < 0)
		return err;

	tegra_io_rail_unprepare();

	return 0;
}
EXPORT_SYMBOL(tegra_io_rail_power_off);
