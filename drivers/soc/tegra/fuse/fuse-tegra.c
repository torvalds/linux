/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

static u32 (*fuse_readl)(const unsigned int offset);
static int fuse_size;
struct tegra_sku_info tegra_sku_info;

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A01]     = "A01",
	[TEGRA_REVISION_A02]     = "A02",
	[TEGRA_REVISION_A03]     = "A03",
	[TEGRA_REVISION_A03p]    = "A03 prime",
	[TEGRA_REVISION_A04]     = "A04",
};

static u8 fuse_readb(const unsigned int offset)
{
	u32 val;

	val = fuse_readl(round_down(offset, 4));
	val >>= (offset % 4) * 8;
	val &= 0xff;

	return val;
}

static ssize_t fuse_read(struct file *fd, struct kobject *kobj,
			struct bin_attribute *attr, char *buf,
			loff_t pos, size_t size)
{
	int i;

	if (pos < 0 || pos >= fuse_size)
		return 0;

	if (size > fuse_size - pos)
		size = fuse_size - pos;

	for (i = 0; i < size; i++)
		buf[i] = fuse_readb(pos + i);

	return i;
}

static struct bin_attribute fuse_bin_attr = {
	.attr = { .name = "fuse", .mode = S_IRUGO, },
	.read = fuse_read,
};

static const struct of_device_id car_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-car", },
	{ .compatible = "nvidia,tegra30-car", },
	{ .compatible = "nvidia,tegra114-car", },
	{ .compatible = "nvidia,tegra124-car", },
	{},
};

static void tegra_enable_fuse_clk(void __iomem *base)
{
	u32 reg;

	reg = readl_relaxed(base + 0x48);
	reg |= 1 << 28;
	writel(reg, base + 0x48);

	/*
	 * Enable FUSE clock. This needs to be hardcoded because the clock
	 * subsystem is not active during early boot.
	 */
	reg = readl(base + 0x14);
	reg |= 1 << 7;
	writel(reg, base + 0x14);
}

int tegra_fuse_readl(unsigned long offset, u32 *value)
{
	if (!fuse_readl)
		return -EPROBE_DEFER;

	*value = fuse_readl(offset);

	return 0;
}
EXPORT_SYMBOL(tegra_fuse_readl);

int tegra_fuse_create_sysfs(struct device *dev, int size,
		     u32 (*readl)(const unsigned int offset))
{
	if (fuse_size)
		return -ENODEV;

	fuse_bin_attr.size = size;
	fuse_bin_attr.read = fuse_read;

	fuse_size = size;
	fuse_readl = readl;

	return device_create_bin_file(dev, &fuse_bin_attr);
}

void __init tegra_init_fuse(void)
{
	struct device_node *np;
	void __iomem *car_base;

	tegra_init_apbmisc();

	np = of_find_matching_node(NULL, car_match);
	car_base = of_iomap(np, 0);
	if (car_base) {
		tegra_enable_fuse_clk(car_base);
		iounmap(car_base);
	} else {
		pr_err("Could not enable fuse clk. ioremap tegra car failed.\n");
		return;
	}

	if (tegra_get_chip_id() == TEGRA20)
		tegra20_init_fuse_early();
	else
		tegra30_init_fuse_early();

	pr_info("Tegra Revision: %s SKU: %d CPU Process: %d Core Process: %d\n",
		tegra_revision_name[tegra_sku_info.revision],
		tegra_sku_info.sku_id, tegra_sku_info.cpu_process_id,
		tegra_sku_info.core_process_id);
	pr_debug("Tegra CPU Speedo ID %d, Soc Speedo ID %d\n",
		tegra_sku_info.cpu_speedo_id, tegra_sku_info.soc_speedo_id);
}
