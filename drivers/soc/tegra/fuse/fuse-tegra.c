// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
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
#ifdef CONFIG_ARCH_TEGRA_234_SOC
	{ .compatible = "nvidia,tegra234-efuse", .data = &tegra234_fuse_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_194_SOC
	{ .compatible = "nvidia,tegra194-efuse", .data = &tegra194_fuse_soc },
#endif
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

static int tegra_fuse_read(void *priv, unsigned int offset, void *value,
			   size_t bytes)
{
	unsigned int count = bytes / 4, i;
	struct tegra_fuse *fuse = priv;
	u32 *buffer = value;

	for (i = 0; i < count; i++)
		buffer[i] = fuse->read(fuse, offset + i * 4);

	return 0;
}

static const struct nvmem_cell_info tegra_fuse_cells[] = {
	{
		.name = "tsensor-cpu1",
		.offset = 0x084,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu2",
		.offset = 0x088,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu0",
		.offset = 0x098,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration",
		.offset = 0x0f0,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-cpu3",
		.offset = 0x12c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "sata-calibration",
		.offset = 0x124,
		.bytes = 1,
		.bit_offset = 0,
		.nbits = 2,
	}, {
		.name = "tsensor-gpu",
		.offset = 0x154,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem0",
		.offset = 0x158,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-mem1",
		.offset = 0x15c,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-pllx",
		.offset = 0x160,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-common",
		.offset = 0x180,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gcplex-config-fuse",
		.offset = 0x1c8,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "tsensor-realignment",
		.offset = 0x1fc,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "gpu-calibration",
		.offset = 0x204,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "xusb-pad-calibration-ext",
		.offset = 0x250,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "pdi0",
		.offset = 0x300,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	}, {
		.name = "pdi1",
		.offset = 0x304,
		.bytes = 4,
		.bit_offset = 0,
		.nbits = 32,
	},
};

static void tegra_fuse_restore(void *base)
{
	fuse->base = (void __iomem *)base;
	fuse->clk = NULL;
}

static int tegra_fuse_probe(struct platform_device *pdev)
{
	void __iomem *base = fuse->base;
	struct nvmem_config nvmem;
	struct resource *res;
	int err;

	err = devm_add_action(&pdev->dev, tegra_fuse_restore, (void __force *)base);
	if (err)
		return err;

	/* take over the memory region from the early initialization */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fuse->phys = res->start;
	fuse->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fuse->base)) {
		err = PTR_ERR(fuse->base);
		return err;
	}

	fuse->clk = devm_clk_get(&pdev->dev, "fuse");
	if (IS_ERR(fuse->clk)) {
		if (PTR_ERR(fuse->clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get FUSE clock: %ld",
				PTR_ERR(fuse->clk));

		return PTR_ERR(fuse->clk);
	}

	platform_set_drvdata(pdev, fuse);
	fuse->dev = &pdev->dev;

	err = devm_pm_runtime_enable(&pdev->dev);
	if (err)
		return err;

	if (fuse->soc->probe) {
		err = fuse->soc->probe(fuse);
		if (err < 0)
			return err;
	}

	memset(&nvmem, 0, sizeof(nvmem));
	nvmem.dev = &pdev->dev;
	nvmem.name = "fuse";
	nvmem.id = -1;
	nvmem.owner = THIS_MODULE;
	nvmem.cells = tegra_fuse_cells;
	nvmem.ncells = ARRAY_SIZE(tegra_fuse_cells);
	nvmem.type = NVMEM_TYPE_OTP;
	nvmem.read_only = true;
	nvmem.root_only = true;
	nvmem.reg_read = tegra_fuse_read;
	nvmem.size = fuse->soc->info->size;
	nvmem.word_size = 4;
	nvmem.stride = 4;
	nvmem.priv = fuse;

	fuse->nvmem = devm_nvmem_register(&pdev->dev, &nvmem);
	if (IS_ERR(fuse->nvmem)) {
		err = PTR_ERR(fuse->nvmem);
		dev_err(&pdev->dev, "failed to register NVMEM device: %d\n",
			err);
		return err;
	}

	fuse->rst = devm_reset_control_get_optional(&pdev->dev, "fuse");
	if (IS_ERR(fuse->rst)) {
		err = PTR_ERR(fuse->rst);
		dev_err(&pdev->dev, "failed to get FUSE reset: %pe\n",
			fuse->rst);
		return err;
	}

	/*
	 * FUSE clock is enabled at a boot time, hence this resume/suspend
	 * disables the clock besides the h/w resetting.
	 */
	err = pm_runtime_resume_and_get(&pdev->dev);
	if (err)
		return err;

	err = reset_control_reset(fuse->rst);
	pm_runtime_put(&pdev->dev);

	if (err < 0) {
		dev_err(&pdev->dev, "failed to reset FUSE: %d\n", err);
		return err;
	}

	/* release the early I/O memory mapping */
	iounmap(base);

	return 0;
}

static int __maybe_unused tegra_fuse_runtime_resume(struct device *dev)
{
	int err;

	err = clk_prepare_enable(fuse->clk);
	if (err < 0) {
		dev_err(dev, "failed to enable FUSE clock: %d\n", err);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra_fuse_runtime_suspend(struct device *dev)
{
	clk_disable_unprepare(fuse->clk);

	return 0;
}

static int __maybe_unused tegra_fuse_suspend(struct device *dev)
{
	int ret;

	/*
	 * Critical for RAM re-repair operation, which must occur on resume
	 * from LP1 system suspend and as part of CCPLEX cluster switching.
	 */
	if (fuse->soc->clk_suspend_on)
		ret = pm_runtime_resume_and_get(dev);
	else
		ret = pm_runtime_force_suspend(dev);

	return ret;
}

static int __maybe_unused tegra_fuse_resume(struct device *dev)
{
	int ret = 0;

	if (fuse->soc->clk_suspend_on)
		pm_runtime_put(dev);
	else
		ret = pm_runtime_force_resume(dev);

	return ret;
}

static const struct dev_pm_ops tegra_fuse_pm = {
	SET_RUNTIME_PM_OPS(tegra_fuse_runtime_suspend, tegra_fuse_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_fuse_suspend, tegra_fuse_resume)
};

static struct platform_driver tegra_fuse_driver = {
	.driver = {
		.name = "tegra-fuse",
		.of_match_table = tegra_fuse_match,
		.pm = &tegra_fuse_pm,
		.suppress_bind_attrs = true,
	},
	.probe = tegra_fuse_probe,
};
builtin_platform_driver(tegra_fuse_driver);

u32 __init tegra_fuse_read_spare(unsigned int spare)
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

static ssize_t major_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", tegra_get_major_rev());
}

static DEVICE_ATTR_RO(major);

static ssize_t minor_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", tegra_get_minor_rev());
}

static DEVICE_ATTR_RO(minor);

static struct attribute *tegra_soc_attr[] = {
	&dev_attr_major.attr,
	&dev_attr_minor.attr,
	NULL,
};

const struct attribute_group tegra_soc_attr_group = {
	.attrs = tegra_soc_attr,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
static ssize_t platform_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	/*
	 * Displays the value in the 'pre_si_platform' field of the HIDREV
	 * register for Tegra194 devices. A value of 0 indicates that the
	 * platform type is silicon and all other non-zero values indicate
	 * the type of simulation platform is being used.
	 */
	return sprintf(buf, "%d\n", tegra_get_platform());
}

static DEVICE_ATTR_RO(platform);

static struct attribute *tegra194_soc_attr[] = {
	&dev_attr_major.attr,
	&dev_attr_minor.attr,
	&dev_attr_platform.attr,
	NULL,
};

const struct attribute_group tegra194_soc_attr_group = {
	.attrs = tegra194_soc_attr,
};
#endif

struct device * __init tegra_soc_device_register(void)
{
	struct soc_device_attribute *attr;
	struct soc_device *dev;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return NULL;

	attr->family = kasprintf(GFP_KERNEL, "Tegra");
	attr->revision = kasprintf(GFP_KERNEL, "%s",
		tegra_revision_name[tegra_sku_info.revision]);
	attr->soc_id = kasprintf(GFP_KERNEL, "%u", tegra_get_chip_id());
	attr->custom_attr_group = fuse->soc->soc_attr_group;

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

	fuse->base = ioremap(regs.start, resource_size(&regs));
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

	if (fuse->soc->lookups) {
		size_t size = sizeof(*fuse->lookups) * fuse->soc->num_lookups;

		fuse->lookups = kmemdup(fuse->soc->lookups, size, GFP_KERNEL);
		if (fuse->lookups)
			nvmem_add_cell_lookups(fuse->lookups, fuse->soc->num_lookups);
	}

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
