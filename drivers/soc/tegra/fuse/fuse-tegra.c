// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <soc/tegra/common.h>
#include <soc/tegra/fuse.h>

#include "fuse.h"

struct tegra_sku_info tegra_sku_info;
EXPORT_SYMBOL(tegra_sku_info);

static const char *tegra_revision_name[TEGRA_REVISION_MAX] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A01]     = "A01",
	[TEGRA_REVISION_A02]     = "A02",
	[TEGRA_REVISION_A03]     = "A03",
	[TEGRA_REVISION_A03p]    = "A03 prime",
	[TEGRA_REVISION_A04]     = "A04",
};

static u8 fuse_readb(struct tegra_fuse *fuse, unsigned int offset)
{
	u32 val;

	val = fuse->read(fuse, round_down(offset, 4));
	val >>= (offset % 4) * 8;
	val &= 0xff;

	return val;
}

static ssize_t fuse_read(struct file *fd, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf,
			 loff_t pos, size_t size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tegra_fuse *fuse = dev_get_drvdata(dev);
	int i;

	if (pos < 0 || pos >= attr->size)
		return 0;

	if (size > attr->size - pos)
		size = attr->size - pos;

	for (i = 0; i < size; i++)
		buf[i] = fuse_readb(fuse, pos + i);

	return i;
}

static struct bin_attribute fuse_bin_attr = {
	.attr = { .name = "fuse", .mode = S_IRUGO, },
	.read = fuse_read,
};

static int tegra_fuse_create_sysfs(struct device *dev, unsigned int size,
				   const struct tegra_fuse_info *info)
{
	fuse_bin_attr.size = size;

	return device_create_bin_file(dev, &fuse_bin_attr);
}

static const struct of_device_id car_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-car", },
	{ .compatible = "nvidia,tegra30-car", },
	{ .compatible = "nvidia,tegra114-car", },
	{ .compatible = "nvidia,tegra124-car", },
	{ .compatible = "nvidia,tegra132-car", },
	{ .compatible = "nvidia,tegra210-car", },
	{},
};

static struct tegra_fuse *fuse = &(struct tegra_fuse) {
	.base = NULL,
	.soc = NULL,
};

static const struct of_device_id tegra_fuse_match[] = {
#ifdef CONFIG_ARCH_TEGRA_186_SOC
	{ .compatible = "nvidia,tegra186-efuse", .data = &tegra186_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_210_SOC
	{ .compatible = "nvidia,tegra210-efuse", .data = &tegra210_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_132_SOC
	{ .compatible = "nvidia,tegra132-efuse", .data = &tegra124_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_124_SOC
	{ .compatible = "nvidia,tegra124-efuse", .data = &tegra124_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_114_SOC
	{ .compatible = "nvidia,tegra114-efuse", .data = &tegra114_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	{ .compatible = "nvidia,tegra30-efuse", .data = &tegra30_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	{ .compatible = "nvidia,tegra20-efuse", .data = &tegra20_fuse_soc },
#endif
	{ /* sentinel */ }
};

static int tegra_fuse_probe(struct platform_device *pdev)
{
	void __iomem *base = fuse->base;
	struct resource *res;
	int err;

	/* take over the memory region from the early initialization */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fuse->phys = res->start;
	fuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fuse->base)) {
		err = PTR_ERR(fuse->base);
		fuse->base = base;
		return err;
	}

	fuse->clk = devm_clk_get(&pdev->dev, "fuse");
	if (IS_ERR(fuse->clk)) {
		if (PTR_ERR(fuse->clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get FUSE clock: %ld",
				PTR_ERR(fuse->clk));

		fuse->base = base;
		return PTR_ERR(fuse->clk);
	}

	platform_set_drvdata(pdev, fuse);
	fuse->dev = &pdev->dev;

	if (fuse->soc->probe) {
		err = fuse->soc->probe(fuse);
		if (err < 0) {
			fuse->base = base;
			return err;
		}
	}

	if (tegra_fuse_create_sysfs(&pdev->dev, fuse->soc->info->size,
				    fuse->soc->info))
		return -ENODEV;

	/* release the early I/O memory mapping */
	iounmap(base);

	return 0;
}

static struct platform_driver tegra_fuse_driver = {
	.driver = {
		.name = "tegra-fuse",
		.of_match_table = tegra_fuse_match,
		.suppress_bind_attrs = true,
	},
	.probe = tegra_fuse_probe,
};
builtin_platform_driver(tegra_fuse_driver);

bool __init tegra_fuse_read_spare(unsigned int spare)
{
	unsigned int offset = fuse->soc->info->spare + spare * 4;

	return fuse->read_early(fuse, offset) & 1;
}

u32 __init tegra_fuse_read_early(unsigned int offset)
{
	return fuse->read_early(fuse, offset);
}

int tegra_fuse_readl(unsigned long offset, u32 *value)
{
	if (!fuse->read || !fuse->clk)
		return -EPROBE_DEFER;

	if (IS_ERR(fuse->clk))
		return PTR_ERR(fuse->clk);

	*value = fuse->read(fuse, offset);

	return 0;
}
EXPORT_SYMBOL(tegra_fuse_readl);

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

struct device * __init tegra_soc_device_register(void)
{
	struct soc_device_attribute *attr;
	struct soc_device *dev;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return NULL;

	attr->family = kasprintf(GFP_KERNEL, "Tegra");
	attr->revision = kasprintf(GFP_KERNEL, "%d", tegra_sku_info.revision);
	attr->soc_id = kasprintf(GFP_KERNEL, "%u", tegra_get_chip_id());

	dev = soc_device_register(attr);
	if (IS_ERR(dev)) {
		kfree(attr->soc_id);
		kfree(attr->revision);
		kfree(attr->family);
		kfree(attr);
		return ERR_CAST(dev);
	}

	return soc_device_to_device(dev);
}

static int __init tegra_init_fuse(void)
{
	const struct of_device_id *match;
	struct device_node *np;
	struct resource regs;

	tegra_init_apbmisc();

	np = of_find_matching_node_and_match(NULL, tegra_fuse_match, &match);
	if (!np) {
		/*
		 * Fall back to legacy initialization for 32-bit ARM only. All
		 * 64-bit ARM device tree files for Tegra are required to have
		 * a FUSE node.
		 *
		 * This is for backwards-compatibility with old device trees
		 * that didn't contain a FUSE node.
		 */
		if (IS_ENABLED(CONFIG_ARM) && soc_is_tegra()) {
			u8 chip = tegra_get_chip_id();

			regs.start = 0x7000f800;
			regs.end = 0x7000fbff;
			regs.flags = IORESOURCE_MEM;

			switch (chip) {
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
			case TEGRA20:
				fuse->soc = &tegra20_fuse_soc;
				break;
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
			case TEGRA30:
				fuse->soc = &tegra30_fuse_soc;
				break;
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
			case TEGRA114:
				fuse->soc = &tegra114_fuse_soc;
				break;
#endif

#ifdef CONFIG_ARCH_TEGRA_124_SOC
			case TEGRA124:
				fuse->soc = &tegra124_fuse_soc;
				break;
#endif

			default:
				pr_warn("Unsupported SoC: %02x\n", chip);
				break;
			}
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
			pr_err("failed to get FUSE register\n");
			return -ENXIO;
		}

		fuse->soc = match->data;
	}

	np = of_find_matching_node(NULL, car_match);
	if (np) {
		void __iomem *base = of_iomap(np, 0);
		if (base) {
			tegra_enable_fuse_clk(base);
			iounmap(base);
		} else {
			pr_err("failed to map clock registers\n");
			return -ENXIO;
		}
	}

	fuse->base = ioremap_nocache(regs.start, resource_size(&regs));
	if (!fuse->base) {
		pr_err("failed to map FUSE registers\n");
		return -ENXIO;
	}

	fuse->soc->init(fuse);

	pr_info("Tegra Revision: %s SKU: %d CPU Process: %d SoC Process: %d\n",
		tegra_revision_name[tegra_sku_info.revision],
		tegra_sku_info.sku_id, tegra_sku_info.cpu_process_id,
		tegra_sku_info.soc_process_id);
	pr_debug("Tegra CPU Speedo ID %d, SoC Speedo ID %d\n",
		 tegra_sku_info.cpu_speedo_id, tegra_sku_info.soc_speedo_id);


	return 0;
}
early_initcall(tegra_init_fuse);

#ifdef CONFIG_ARM64
static int __init tegra_init_soc(void)
{
	struct device_node *np;
	struct device *soc;

	/* make sure we're running on Tegra */
	np = of_find_matching_node(NULL, tegra_fuse_match);
	if (!np)
		return 0;

	of_node_put(np);

	soc = tegra_soc_device_register();
	if (IS_ERR(soc)) {
		pr_err("failed to register SoC device: %ld\n", PTR_ERR(soc));
		return PTR_ERR(soc);
	}

	return 0;
}
device_initcall(tegra_init_soc);
#endif
