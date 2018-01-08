/*
 * ti-sysc.c - Texas Instruments sysc interconnect target driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

enum sysc_registers {
	SYSC_REVISION,
	SYSC_SYSCONFIG,
	SYSC_SYSSTATUS,
	SYSC_MAX_REGS,
};

static const char * const reg_names[] = { "rev", "sysc", "syss", };

enum sysc_clocks {
	SYSC_FCK,
	SYSC_ICK,
	SYSC_MAX_CLOCKS,
};

static const char * const clock_names[] = { "fck", "ick", };

/**
 * struct sysc - TI sysc interconnect target module registers and capabilities
 * @dev: struct device pointer
 * @module_pa: physical address of the interconnect target module
 * @module_size: size of the interconnect target module
 * @module_va: virtual address of the interconnect target module
 * @offsets: register offsets from module base
 * @clocks: clocks used by the interconnect target module
 * @legacy_mode: configured for legacy mode if set
 */
struct sysc {
	struct device *dev;
	u64 module_pa;
	u32 module_size;
	void __iomem *module_va;
	int offsets[SYSC_MAX_REGS];
	struct clk *clocks[SYSC_MAX_CLOCKS];
	const char *legacy_mode;
};

static u32 sysc_read_revision(struct sysc *ddata)
{
	return readl_relaxed(ddata->module_va +
			     ddata->offsets[SYSC_REVISION]);
}

static int sysc_get_one_clock(struct sysc *ddata,
			      enum sysc_clocks index)
{
	const char *name;
	int error;

	switch (index) {
	case SYSC_FCK:
		break;
	case SYSC_ICK:
		break;
	default:
		return -EINVAL;
	}
	name = clock_names[index];

	ddata->clocks[index] = devm_clk_get(ddata->dev, name);
	if (IS_ERR(ddata->clocks[index])) {
		if (PTR_ERR(ddata->clocks[index]) == -ENOENT)
			return 0;

		dev_err(ddata->dev, "clock get error for %s: %li\n",
			name, PTR_ERR(ddata->clocks[index]));

		return PTR_ERR(ddata->clocks[index]);
	}

	error = clk_prepare(ddata->clocks[index]);
	if (error) {
		dev_err(ddata->dev, "clock prepare error for %s: %i\n",
			name, error);

		return error;
	}

	return 0;
}

static int sysc_get_clocks(struct sysc *ddata)
{
	int i, error;

	if (ddata->legacy_mode)
		return 0;

	for (i = 0; i < SYSC_MAX_CLOCKS; i++) {
		error = sysc_get_one_clock(ddata, i);
		if (error && error != -ENOENT)
			return error;
	}

	return 0;
}

/**
 * sysc_parse_and_check_child_range - parses module IO region from ranges
 * @ddata: device driver data
 *
 * In general we only need rev, syss, and sysc registers and not the whole
 * module range. But we do want the offsets for these registers from the
 * module base. This allows us to check them against the legacy hwmod
 * platform data. Let's also check the ranges are configured properly.
 */
static int sysc_parse_and_check_child_range(struct sysc *ddata)
{
	struct device_node *np = ddata->dev->of_node;
	const __be32 *ranges;
	u32 nr_addr, nr_size;
	int len, error;

	ranges = of_get_property(np, "ranges", &len);
	if (!ranges) {
		dev_err(ddata->dev, "missing ranges for %pOF\n", np);

		return -ENOENT;
	}

	len /= sizeof(*ranges);

	if (len < 3) {
		dev_err(ddata->dev, "incomplete ranges for %pOF\n", np);

		return -EINVAL;
	}

	error = of_property_read_u32(np, "#address-cells", &nr_addr);
	if (error)
		return -ENOENT;

	error = of_property_read_u32(np, "#size-cells", &nr_size);
	if (error)
		return -ENOENT;

	if (nr_addr != 1 || nr_size != 1) {
		dev_err(ddata->dev, "invalid ranges for %pOF\n", np);

		return -EINVAL;
	}

	ranges++;
	ddata->module_pa = of_translate_address(np, ranges++);
	ddata->module_size = be32_to_cpup(ranges);

	dev_dbg(ddata->dev, "interconnect target 0x%llx size 0x%x for %pOF\n",
		ddata->module_pa, ddata->module_size, np);

	return 0;
}

/**
 * sysc_check_one_child - check child configuration
 * @ddata: device driver data
 * @np: child device node
 *
 * Let's avoid messy situations where we have new interconnect target
 * node but children have "ti,hwmods". These belong to the interconnect
 * target node and are managed by this driver.
 */
static int sysc_check_one_child(struct sysc *ddata,
				struct device_node *np)
{
	const char *name;

	name = of_get_property(np, "ti,hwmods", NULL);
	if (name)
		dev_warn(ddata->dev, "really a child ti,hwmods property?");

	return 0;
}

static int sysc_check_children(struct sysc *ddata)
{
	struct device_node *child;
	int error;

	for_each_child_of_node(ddata->dev->of_node, child) {
		error = sysc_check_one_child(ddata, child);
		if (error)
			return error;
	}

	return 0;
}

/**
 * sysc_parse_one - parses the interconnect target module registers
 * @ddata: device driver data
 * @reg: register to parse
 */
static int sysc_parse_one(struct sysc *ddata, enum sysc_registers reg)
{
	struct resource *res;
	const char *name;

	switch (reg) {
	case SYSC_REVISION:
	case SYSC_SYSCONFIG:
	case SYSC_SYSSTATUS:
		name = reg_names[reg];
		break;
	default:
		return -EINVAL;
	}

	res = platform_get_resource_byname(to_platform_device(ddata->dev),
					   IORESOURCE_MEM, name);
	if (!res) {
		dev_dbg(ddata->dev, "has no %s register\n", name);
		ddata->offsets[reg] = -ENODEV;

		return 0;
	}

	ddata->offsets[reg] = res->start - ddata->module_pa;

	return 0;
}

static int sysc_parse_registers(struct sysc *ddata)
{
	int i, error;

	for (i = 0; i < SYSC_MAX_REGS; i++) {
		error = sysc_parse_one(ddata, i);
		if (error)
			return error;
	}

	return 0;
}

/**
 * sysc_check_registers - check for misconfigured register overlaps
 * @ddata: device driver data
 */
static int sysc_check_registers(struct sysc *ddata)
{
	int i, j, nr_regs = 0, nr_matches = 0;

	for (i = 0; i < SYSC_MAX_REGS; i++) {
		if (ddata->offsets[i] < 0)
			continue;

		if (ddata->offsets[i] > (ddata->module_size - 4)) {
			dev_err(ddata->dev, "register outside module range");

				return -EINVAL;
		}

		for (j = 0; j < SYSC_MAX_REGS; j++) {
			if (ddata->offsets[j] < 0)
				continue;

			if (ddata->offsets[i] == ddata->offsets[j])
				nr_matches++;
		}
		nr_regs++;
	}

	if (nr_regs < 1) {
		dev_err(ddata->dev, "missing registers\n");

		return -EINVAL;
	}

	if (nr_matches > nr_regs) {
		dev_err(ddata->dev, "overlapping registers: (%i/%i)",
			nr_regs, nr_matches);

		return -EINVAL;
	}

	return 0;
}

/**
 * syc_ioremap - ioremap register space for the interconnect target module
 * @ddata: deviec driver data
 *
 * Note that the interconnect target module registers can be anywhere
 * within the first child device address space. For example, SGX has
 * them at offset 0x1fc00 in the 32MB module address space. We just
 * what we need around the interconnect target module registers.
 */
static int sysc_ioremap(struct sysc *ddata)
{
	u32 size = 0;

	if (ddata->offsets[SYSC_SYSSTATUS] >= 0)
		size = ddata->offsets[SYSC_SYSSTATUS];
	else if (ddata->offsets[SYSC_SYSCONFIG] >= 0)
		size = ddata->offsets[SYSC_SYSCONFIG];
	else if (ddata->offsets[SYSC_REVISION] >= 0)
		size = ddata->offsets[SYSC_REVISION];
	else
		return -EINVAL;

	size &= 0xfff00;
	size += SZ_256;

	ddata->module_va = devm_ioremap(ddata->dev,
					ddata->module_pa,
					size);
	if (!ddata->module_va)
		return -EIO;

	return 0;
}

/**
 * sysc_map_and_check_registers - ioremap and check device registers
 * @ddata: device driver data
 */
static int sysc_map_and_check_registers(struct sysc *ddata)
{
	int error;

	error = sysc_parse_and_check_child_range(ddata);
	if (error)
		return error;

	error = sysc_check_children(ddata);
	if (error)
		return error;

	error = sysc_parse_registers(ddata);
	if (error)
		return error;

	error = sysc_ioremap(ddata);
	if (error)
		return error;

	error = sysc_check_registers(ddata);
	if (error)
		return error;

	return 0;
}

/**
 * sysc_show_rev - read and show interconnect target module revision
 * @bufp: buffer to print the information to
 * @ddata: device driver data
 */
static int sysc_show_rev(char *bufp, struct sysc *ddata)
{
	int error, len;

	if (ddata->offsets[SYSC_REVISION] < 0)
		return sprintf(bufp, ":NA");

	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);

		return 0;
	}

	len = sprintf(bufp, ":%08x", sysc_read_revision(ddata));

	pm_runtime_mark_last_busy(ddata->dev);
	pm_runtime_put_autosuspend(ddata->dev);

	return len;
}

static int sysc_show_reg(struct sysc *ddata,
			 char *bufp, enum sysc_registers reg)
{
	if (ddata->offsets[reg] < 0)
		return sprintf(bufp, ":NA");

	return sprintf(bufp, ":%x", ddata->offsets[reg]);
}

/**
 * sysc_show_registers - show information about interconnect target module
 * @ddata: device driver data
 */
static void sysc_show_registers(struct sysc *ddata)
{
	char buf[128];
	char *bufp = buf;
	int i;

	for (i = 0; i < SYSC_MAX_REGS; i++)
		bufp += sysc_show_reg(ddata, bufp, i);

	bufp += sysc_show_rev(bufp, ddata);

	dev_dbg(ddata->dev, "%llx:%x%s\n",
		ddata->module_pa, ddata->module_size,
		buf);
}

static int __maybe_unused sysc_runtime_suspend(struct device *dev)
{
	struct sysc *ddata;
	int i;

	ddata = dev_get_drvdata(dev);

	if (ddata->legacy_mode)
		return 0;

	for (i = 0; i < SYSC_MAX_CLOCKS; i++) {
		if (IS_ERR_OR_NULL(ddata->clocks[i]))
			continue;
		clk_disable(ddata->clocks[i]);
	}

	return 0;
}

static int __maybe_unused sysc_runtime_resume(struct device *dev)
{
	struct sysc *ddata;
	int i, error;

	ddata = dev_get_drvdata(dev);

	if (ddata->legacy_mode)
		return 0;

	for (i = 0; i < SYSC_MAX_CLOCKS; i++) {
		if (IS_ERR_OR_NULL(ddata->clocks[i]))
			continue;
		error = clk_enable(ddata->clocks[i]);
		if (error)
			return error;
	}

	return 0;
}

static const struct dev_pm_ops sysc_pm_ops = {
	SET_RUNTIME_PM_OPS(sysc_runtime_suspend,
			   sysc_runtime_resume,
			   NULL)
};

static void sysc_unprepare(struct sysc *ddata)
{
	int i;

	for (i = 0; i < SYSC_MAX_CLOCKS; i++) {
		if (!IS_ERR_OR_NULL(ddata->clocks[i]))
			clk_unprepare(ddata->clocks[i]);
	}
}

static int sysc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sysc *ddata;
	int error;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	ddata->legacy_mode = of_get_property(np, "ti,hwmods", NULL);

	error = sysc_get_clocks(ddata);
	if (error)
		return error;

	error = sysc_map_and_check_registers(ddata);
	if (error)
		goto unprepare;

	platform_set_drvdata(pdev, ddata);

	pm_runtime_enable(ddata->dev);
	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);
		pm_runtime_disable(ddata->dev);
		goto unprepare;
	}

	pm_runtime_use_autosuspend(ddata->dev);

	sysc_show_registers(ddata);

	error = of_platform_populate(ddata->dev->of_node,
				     NULL, NULL, ddata->dev);
	if (error)
		goto err;

	pm_runtime_mark_last_busy(ddata->dev);
	pm_runtime_put_autosuspend(ddata->dev);

	return 0;

err:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
unprepare:
	sysc_unprepare(ddata);

	return error;
}

static int sysc_remove(struct platform_device *pdev)
{
	struct sysc *ddata = platform_get_drvdata(pdev);
	int error;

	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);
		pm_runtime_disable(ddata->dev);
		goto unprepare;
	}

	of_platform_depopulate(&pdev->dev);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

unprepare:
	sysc_unprepare(ddata);

	return 0;
}

static const struct of_device_id sysc_match[] = {
	{ .compatible = "ti,sysc-omap2" },
	{ .compatible = "ti,sysc-omap4" },
	{ .compatible = "ti,sysc-omap4-simple" },
	{ .compatible = "ti,sysc-omap3430-sr" },
	{ .compatible = "ti,sysc-omap3630-sr" },
	{ .compatible = "ti,sysc-omap4-sr" },
	{ .compatible = "ti,sysc-omap3-sham" },
	{ .compatible = "ti,sysc-omap-aes" },
	{ .compatible = "ti,sysc-mcasp" },
	{ .compatible = "ti,sysc-usb-host-fs" },
	{  },
};
MODULE_DEVICE_TABLE(of, sysc_match);

static struct platform_driver sysc_driver = {
	.probe		= sysc_probe,
	.remove		= sysc_remove,
	.driver         = {
		.name   = "ti-sysc",
		.of_match_table	= sysc_match,
		.pm = &sysc_pm_ops,
	},
};
module_platform_driver(sysc_driver);

MODULE_DESCRIPTION("TI sysc interconnect target driver");
MODULE_LICENSE("GPL v2");
