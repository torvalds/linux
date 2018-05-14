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
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <linux/platform_data/ti-sysc.h>

#include <dt-bindings/bus/ti-sysc.h>

static const char * const reg_names[] = { "rev", "sysc", "syss", };

enum sysc_clocks {
	SYSC_FCK,
	SYSC_ICK,
	SYSC_OPTFCK0,
	SYSC_OPTFCK1,
	SYSC_OPTFCK2,
	SYSC_OPTFCK3,
	SYSC_OPTFCK4,
	SYSC_OPTFCK5,
	SYSC_OPTFCK6,
	SYSC_OPTFCK7,
	SYSC_MAX_CLOCKS,
};

static const char * const clock_names[SYSC_ICK + 1] = { "fck", "ick", };

#define SYSC_IDLEMODE_MASK		3
#define SYSC_CLOCKACTIVITY_MASK		3

/**
 * struct sysc - TI sysc interconnect target module registers and capabilities
 * @dev: struct device pointer
 * @module_pa: physical address of the interconnect target module
 * @module_size: size of the interconnect target module
 * @module_va: virtual address of the interconnect target module
 * @offsets: register offsets from module base
 * @clocks: clocks used by the interconnect target module
 * @clock_roles: clock role names for the found clocks
 * @nr_clocks: number of clocks used by the interconnect target module
 * @legacy_mode: configured for legacy mode if set
 * @cap: interconnect target module capabilities
 * @cfg: interconnect target module configuration
 * @name: name if available
 * @revision: interconnect target module revision
 * @needs_resume: runtime resume needed on resume from suspend
 */
struct sysc {
	struct device *dev;
	u64 module_pa;
	u32 module_size;
	void __iomem *module_va;
	int offsets[SYSC_MAX_REGS];
	struct clk **clocks;
	const char **clock_roles;
	int nr_clocks;
	struct reset_control *rsts;
	const char *legacy_mode;
	const struct sysc_capabilities *cap;
	struct sysc_config cfg;
	struct ti_sysc_cookie cookie;
	const char *name;
	u32 revision;
	bool enabled;
	bool needs_resume;
	bool child_needs_resume;
	struct delayed_work idle_work;
};

static u32 sysc_read(struct sysc *ddata, int offset)
{
	if (ddata->cfg.quirks & SYSC_QUIRK_16BIT) {
		u32 val;

		val = readw_relaxed(ddata->module_va + offset);
		val |= (readw_relaxed(ddata->module_va + offset + 4) << 16);

		return val;
	}

	return readl_relaxed(ddata->module_va + offset);
}

static bool sysc_opt_clks_needed(struct sysc *ddata)
{
	return !!(ddata->cfg.quirks & SYSC_QUIRK_OPT_CLKS_NEEDED);
}

static u32 sysc_read_revision(struct sysc *ddata)
{
	int offset = ddata->offsets[SYSC_REVISION];

	if (offset < 0)
		return 0;

	return sysc_read(ddata, offset);
}

static int sysc_get_one_clock(struct sysc *ddata, const char *name)
{
	int error, i, index = -ENODEV;

	if (!strncmp(clock_names[SYSC_FCK], name, 3))
		index = SYSC_FCK;
	else if (!strncmp(clock_names[SYSC_ICK], name, 3))
		index = SYSC_ICK;

	if (index < 0) {
		for (i = SYSC_OPTFCK0; i < SYSC_MAX_CLOCKS; i++) {
			if (!clock_names[i]) {
				index = i;
				break;
			}
		}
	}

	if (index < 0) {
		dev_err(ddata->dev, "clock %s not added\n", name);
		return index;
	}

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
	struct device_node *np = ddata->dev->of_node;
	struct property *prop;
	const char *name;
	int nr_fck = 0, nr_ick = 0, i, error = 0;

	ddata->clock_roles = devm_kzalloc(ddata->dev,
					  sizeof(*ddata->clock_roles) *
					  SYSC_MAX_CLOCKS,
					  GFP_KERNEL);
	if (!ddata->clock_roles)
		return -ENOMEM;

	of_property_for_each_string(np, "clock-names", prop, name) {
		if (!strncmp(clock_names[SYSC_FCK], name, 3))
			nr_fck++;
		if (!strncmp(clock_names[SYSC_ICK], name, 3))
			nr_ick++;
		ddata->clock_roles[ddata->nr_clocks] = name;
		ddata->nr_clocks++;
	}

	if (ddata->nr_clocks < 1)
		return 0;

	if (ddata->nr_clocks > SYSC_MAX_CLOCKS) {
		dev_err(ddata->dev, "too many clocks for %pOF\n", np);

		return -EINVAL;
	}

	if (nr_fck > 1 || nr_ick > 1) {
		dev_err(ddata->dev, "max one fck and ick for %pOF\n", np);

		return -EINVAL;
	}

	ddata->clocks = devm_kzalloc(ddata->dev,
				     sizeof(*ddata->clocks) * ddata->nr_clocks,
				     GFP_KERNEL);
	if (!ddata->clocks)
		return -ENOMEM;

	for (i = 0; i < ddata->nr_clocks; i++) {
		error = sysc_get_one_clock(ddata, ddata->clock_roles[i]);
		if (error && error != -ENOENT)
			return error;
	}

	return 0;
}

/**
 * sysc_init_resets - reset module on init
 * @ddata: device driver data
 *
 * A module can have both OCP softreset control and external rstctrl.
 * If more complicated rstctrl resets are needed, please handle these
 * directly from the child device driver and map only the module reset
 * for the parent interconnect target module device.
 *
 * Automatic reset of the module on init can be skipped with the
 * "ti,no-reset-on-init" device tree property.
 */
static int sysc_init_resets(struct sysc *ddata)
{
	int error;

	ddata->rsts =
		devm_reset_control_array_get_optional_exclusive(ddata->dev);
	if (IS_ERR(ddata->rsts))
		return PTR_ERR(ddata->rsts);

	if (ddata->cfg.quirks & SYSC_QUIRK_NO_RESET_ON_INIT)
		goto deassert;

	error = reset_control_assert(ddata->rsts);
	if (error)
		return error;

deassert:
	error = reset_control_deassert(ddata->rsts);
	if (error)
		return error;

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

	return 0;
}

static struct device_node *stdout_path;

static void sysc_init_stdout_path(struct sysc *ddata)
{
	struct device_node *np = NULL;
	const char *uart;

	if (IS_ERR(stdout_path))
		return;

	if (stdout_path)
		return;

	np = of_find_node_by_path("/chosen");
	if (!np)
		goto err;

	uart = of_get_property(np, "stdout-path", NULL);
	if (!uart)
		goto err;

	np = of_find_node_by_path(uart);
	if (!np)
		goto err;

	stdout_path = np;

	return;

err:
	stdout_path = ERR_PTR(-ENODEV);
}

static void sysc_check_quirk_stdout(struct sysc *ddata,
				    struct device_node *np)
{
	sysc_init_stdout_path(ddata);
	if (np != stdout_path)
		return;

	ddata->cfg.quirks |= SYSC_QUIRK_NO_IDLE_ON_INIT |
				SYSC_QUIRK_NO_RESET_ON_INIT;
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

	sysc_check_quirk_stdout(ddata, np);

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

/*
 * So far only I2C uses 16-bit read access with clockactivity with revision
 * in two registers with stride of 4. We can detect this based on the rev
 * register size to configure things far enough to be able to properly read
 * the revision register.
 */
static void sysc_check_quirk_16bit(struct sysc *ddata, struct resource *res)
{
	if (resource_size(res) == 8)
		ddata->cfg.quirks |= SYSC_QUIRK_16BIT | SYSC_QUIRK_USE_CLOCKACT;
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
		ddata->offsets[reg] = -ENODEV;

		return 0;
	}

	ddata->offsets[reg] = res->start - ddata->module_pa;
	if (reg == SYSC_REVISION)
		sysc_check_quirk_16bit(ddata, res);

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
	int len;

	if (ddata->offsets[SYSC_REVISION] < 0)
		return sprintf(bufp, ":NA");

	len = sprintf(bufp, ":%08x", ddata->revision);

	return len;
}

static int sysc_show_reg(struct sysc *ddata,
			 char *bufp, enum sysc_registers reg)
{
	if (ddata->offsets[reg] < 0)
		return sprintf(bufp, ":NA");

	return sprintf(bufp, ":%x", ddata->offsets[reg]);
}

static int sysc_show_name(char *bufp, struct sysc *ddata)
{
	if (!ddata->name)
		return 0;

	return sprintf(bufp, ":%s", ddata->name);
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
	bufp += sysc_show_name(bufp, ddata);

	dev_dbg(ddata->dev, "%llx:%x%s\n",
		ddata->module_pa, ddata->module_size,
		buf);
}

static int __maybe_unused sysc_runtime_suspend(struct device *dev)
{
	struct ti_sysc_platform_data *pdata;
	struct sysc *ddata;
	int error = 0, i;

	ddata = dev_get_drvdata(dev);

	if (!ddata->enabled)
		return 0;

	if (ddata->legacy_mode) {
		pdata = dev_get_platdata(ddata->dev);
		if (!pdata)
			return 0;

		if (!pdata->idle_module)
			return -ENODEV;

		error = pdata->idle_module(dev, &ddata->cookie);
		if (error)
			dev_err(dev, "%s: could not idle: %i\n",
				__func__, error);

		goto idled;
	}

	for (i = 0; i < ddata->nr_clocks; i++) {
		if (IS_ERR_OR_NULL(ddata->clocks[i]))
			continue;

		if (i >= SYSC_OPTFCK0 && !sysc_opt_clks_needed(ddata))
			break;

		clk_disable(ddata->clocks[i]);
	}

idled:
	ddata->enabled = false;

	return error;
}

static int __maybe_unused sysc_runtime_resume(struct device *dev)
{
	struct ti_sysc_platform_data *pdata;
	struct sysc *ddata;
	int error = 0, i;

	ddata = dev_get_drvdata(dev);

	if (ddata->enabled)
		return 0;

	if (ddata->legacy_mode) {
		pdata = dev_get_platdata(ddata->dev);
		if (!pdata)
			return 0;

		if (!pdata->enable_module)
			return -ENODEV;

		error = pdata->enable_module(dev, &ddata->cookie);
		if (error)
			dev_err(dev, "%s: could not enable: %i\n",
				__func__, error);

		goto awake;
	}

	for (i = 0; i < ddata->nr_clocks; i++) {
		if (IS_ERR_OR_NULL(ddata->clocks[i]))
			continue;

		if (i >= SYSC_OPTFCK0 && !sysc_opt_clks_needed(ddata))
			break;

		error = clk_enable(ddata->clocks[i]);
		if (error)
			return error;
	}

awake:
	ddata->enabled = true;

	return error;
}

#ifdef CONFIG_PM_SLEEP
static int sysc_suspend(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = dev_get_drvdata(dev);

	if (ddata->cfg.quirks & (SYSC_QUIRK_RESOURCE_PROVIDER |
				 SYSC_QUIRK_LEGACY_IDLE))
		return 0;

	if (!ddata->enabled)
		return 0;

	dev_dbg(ddata->dev, "%s %s\n", __func__,
		ddata->name ? ddata->name : "");

	error = pm_runtime_put_sync_suspend(dev);
	if (error < 0) {
		dev_warn(ddata->dev, "%s not idle %i %s\n",
			 __func__, error,
			 ddata->name ? ddata->name : "");

		return 0;
	}

	ddata->needs_resume = true;

	return 0;
}

static int sysc_resume(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = dev_get_drvdata(dev);

	if (ddata->cfg.quirks & (SYSC_QUIRK_RESOURCE_PROVIDER |
				 SYSC_QUIRK_LEGACY_IDLE))
		return 0;

	if (ddata->needs_resume) {
		dev_dbg(ddata->dev, "%s %s\n", __func__,
			ddata->name ? ddata->name : "");

		error = pm_runtime_get_sync(dev);
		if (error < 0) {
			dev_err(ddata->dev, "%s  error %i %s\n",
				__func__, error,
				 ddata->name ? ddata->name : "");

			return error;
		}

		ddata->needs_resume = false;
	}

	return 0;
}

static int sysc_noirq_suspend(struct device *dev)
{
	struct sysc *ddata;

	ddata = dev_get_drvdata(dev);

	if (ddata->cfg.quirks & SYSC_QUIRK_LEGACY_IDLE)
		return 0;

	if (!(ddata->cfg.quirks & SYSC_QUIRK_RESOURCE_PROVIDER))
		return 0;

	if (!ddata->enabled)
		return 0;

	dev_dbg(ddata->dev, "%s %s\n", __func__,
		ddata->name ? ddata->name : "");

	ddata->needs_resume = true;

	return sysc_runtime_suspend(dev);
}

static int sysc_noirq_resume(struct device *dev)
{
	struct sysc *ddata;

	ddata = dev_get_drvdata(dev);

	if (ddata->cfg.quirks & SYSC_QUIRK_LEGACY_IDLE)
		return 0;

	if (!(ddata->cfg.quirks & SYSC_QUIRK_RESOURCE_PROVIDER))
		return 0;

	if (ddata->needs_resume) {
		dev_dbg(ddata->dev, "%s %s\n", __func__,
			ddata->name ? ddata->name : "");

		ddata->needs_resume = false;

		return sysc_runtime_resume(dev);
	}

	return 0;
}
#endif

static const struct dev_pm_ops sysc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sysc_suspend, sysc_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(sysc_noirq_suspend, sysc_noirq_resume)
	SET_RUNTIME_PM_OPS(sysc_runtime_suspend,
			   sysc_runtime_resume,
			   NULL)
};

/* Module revision register based quirks */
struct sysc_revision_quirk {
	const char *name;
	u32 base;
	int rev_offset;
	int sysc_offset;
	int syss_offset;
	u32 revision;
	u32 revision_mask;
	u32 quirks;
};

#define SYSC_QUIRK(optname, optbase, optrev, optsysc, optsyss,		\
		   optrev_val, optrevmask, optquirkmask)		\
	{								\
		.name = (optname),					\
		.base = (optbase),					\
		.rev_offset = (optrev),					\
		.sysc_offset = (optsysc),				\
		.syss_offset = (optsyss),				\
		.revision = (optrev_val),				\
		.revision_mask = (optrevmask),				\
		.quirks = (optquirkmask),				\
	}

static const struct sysc_revision_quirk sysc_revision_quirks[] = {
	/* These need to use noirq_suspend */
	SYSC_QUIRK("control", 0, 0, 0x10, -1, 0x40000900, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("i2c", 0, 0, 0x10, 0x90, 0x5040000a, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("mcspi", 0, 0, 0x10, -1, 0x40300a0b, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("prcm", 0, 0, -1, -1, 0x40000100, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("ocp2scp", 0, 0, 0x10, 0x14, 0x50060005, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("padconf", 0, 0, 0x10, -1, 0x4fff0800, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("scm", 0, 0, 0x10, -1, 0x40000900, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("scrm", 0, 0, -1, -1, 0x00000010, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),
	SYSC_QUIRK("sdma", 0, 0, 0x2c, 0x28, 0x00010900, 0xffffffff,
		   SYSC_QUIRK_RESOURCE_PROVIDER),

	/* These drivers need to be fixed to not use pm_runtime_irq_safe() */
	SYSC_QUIRK("gpio", 0, 0, 0x10, 0x114, 0x50600801, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE | SYSC_QUIRK_OPT_CLKS_IN_RESET),
	SYSC_QUIRK("mmu", 0, 0, 0x10, 0x14, 0x00000020, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("mmu", 0, 0, 0x10, 0x14, 0x00000030, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("sham", 0, 0x100, 0x110, 0x114, 0x40000c03, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("smartreflex", 0, -1, 0x24, -1, 0x00000000, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("smartreflex", 0, -1, 0x38, -1, 0x00000000, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("timer", 0, 0, 0x10, 0x14, 0x00000015, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	/* Some timers on omap4 and later */
	SYSC_QUIRK("timer", 0, 0, 0x10, -1, 0x4fff1301, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("uart", 0, 0x50, 0x54, 0x58, 0x00000052, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	/* Uarts on omap4 and later */
	SYSC_QUIRK("uart", 0, 0x50, 0x54, 0x58, 0x50411e03, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),

	/* These devices don't yet suspend properly without legacy setting */
	SYSC_QUIRK("sdio", 0, 0, 0x10, -1, 0x40202301, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("wdt", 0, 0, 0x10, 0x14, 0x502a0500, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),
	SYSC_QUIRK("wdt", 0, 0, 0x10, 0x14, 0x502a0d00, 0xffffffff,
		   SYSC_QUIRK_LEGACY_IDLE),

#ifdef DEBUG
	SYSC_QUIRK("aess", 0, 0, 0x10, -1, 0x40000000, 0xffffffff, 0),
	SYSC_QUIRK("gpu", 0, 0x1fc00, 0x1fc10, -1, 0, 0, 0),
	SYSC_QUIRK("hdq1w", 0, 0, 0x14, 0x18, 0x00000006, 0xffffffff, 0),
	SYSC_QUIRK("hsi", 0, 0, 0x10, 0x14, 0x50043101, 0xffffffff, 0),
	SYSC_QUIRK("iss", 0, 0, 0x10, -1, 0x40000101, 0xffffffff, 0),
	SYSC_QUIRK("mcasp", 0, 0, 0x4, -1, 0x44306302, 0xffffffff, 0),
	SYSC_QUIRK("mcbsp", 0, -1, 0x8c, -1, 0, 0, 0),
	SYSC_QUIRK("mailbox", 0, 0, 0x10, -1, 0x00000400, 0xffffffff, 0),
	SYSC_QUIRK("slimbus", 0, 0, 0x10, -1, 0x40000902, 0xffffffff, 0),
	SYSC_QUIRK("slimbus", 0, 0, 0x10, -1, 0x40002903, 0xffffffff, 0),
	SYSC_QUIRK("spinlock", 0, 0, 0x10, -1, 0x50020000, 0xffffffff, 0),
	SYSC_QUIRK("usbhstll", 0, 0, 0x10, 0x14, 0x00000004, 0xffffffff, 0),
	SYSC_QUIRK("usb_host_hs", 0, 0, 0x10, 0x14, 0x50700100, 0xffffffff, 0),
	SYSC_QUIRK("usb_otg_hs", 0, 0x400, 0x404, 0x408, 0x00000050,
		   0xffffffff, 0),
#endif
};

static void sysc_init_revision_quirks(struct sysc *ddata)
{
	const struct sysc_revision_quirk *q;
	int i;

	for (i = 0; i < ARRAY_SIZE(sysc_revision_quirks); i++) {
		q = &sysc_revision_quirks[i];

		if (q->base && q->base != ddata->module_pa)
			continue;

		if (q->rev_offset >= 0 &&
		    q->rev_offset != ddata->offsets[SYSC_REVISION])
			continue;

		if (q->sysc_offset >= 0 &&
		    q->sysc_offset != ddata->offsets[SYSC_SYSCONFIG])
			continue;

		if (q->syss_offset >= 0 &&
		    q->syss_offset != ddata->offsets[SYSC_SYSSTATUS])
			continue;

		if (q->revision == ddata->revision ||
		    (q->revision & q->revision_mask) ==
		    (ddata->revision & q->revision_mask)) {
			ddata->name = q->name;
			ddata->cfg.quirks |= q->quirks;
		}
	}
}

/* At this point the module is configured enough to read the revision */
static int sysc_init_module(struct sysc *ddata)
{
	int error;

	if (ddata->cfg.quirks & SYSC_QUIRK_NO_IDLE_ON_INIT) {
		ddata->revision = sysc_read_revision(ddata);
		goto rev_quirks;
	}

	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);

		return 0;
	}

	ddata->revision = sysc_read_revision(ddata);
	pm_runtime_put_sync(ddata->dev);

rev_quirks:
	sysc_init_revision_quirks(ddata);

	return 0;
}

static int sysc_init_sysc_mask(struct sysc *ddata)
{
	struct device_node *np = ddata->dev->of_node;
	int error;
	u32 val;

	error = of_property_read_u32(np, "ti,sysc-mask", &val);
	if (error)
		return 0;

	if (val)
		ddata->cfg.sysc_val = val & ddata->cap->sysc_mask;
	else
		ddata->cfg.sysc_val = ddata->cap->sysc_mask;

	return 0;
}

static int sysc_init_idlemode(struct sysc *ddata, u8 *idlemodes,
			      const char *name)
{
	struct device_node *np = ddata->dev->of_node;
	struct property *prop;
	const __be32 *p;
	u32 val;

	of_property_for_each_u32(np, name, prop, p, val) {
		if (val >= SYSC_NR_IDLEMODES) {
			dev_err(ddata->dev, "invalid idlemode: %i\n", val);
			return -EINVAL;
		}
		*idlemodes |=  (1 << val);
	}

	return 0;
}

static int sysc_init_idlemodes(struct sysc *ddata)
{
	int error;

	error = sysc_init_idlemode(ddata, &ddata->cfg.midlemodes,
				   "ti,sysc-midle");
	if (error)
		return error;

	error = sysc_init_idlemode(ddata, &ddata->cfg.sidlemodes,
				   "ti,sysc-sidle");
	if (error)
		return error;

	return 0;
}

/*
 * Only some devices on omap4 and later have SYSCONFIG reset done
 * bit. We can detect this if there is no SYSSTATUS at all, or the
 * SYSTATUS bit 0 is not used. Note that some SYSSTATUS registers
 * have multiple bits for the child devices like OHCI and EHCI.
 * Depends on SYSC being parsed first.
 */
static int sysc_init_syss_mask(struct sysc *ddata)
{
	struct device_node *np = ddata->dev->of_node;
	int error;
	u32 val;

	error = of_property_read_u32(np, "ti,syss-mask", &val);
	if (error) {
		if ((ddata->cap->type == TI_SYSC_OMAP4 ||
		     ddata->cap->type == TI_SYSC_OMAP4_TIMER) &&
		    (ddata->cfg.sysc_val & SYSC_OMAP4_SOFTRESET))
			ddata->cfg.quirks |= SYSC_QUIRK_RESET_STATUS;

		return 0;
	}

	if (!(val & 1) && (ddata->cfg.sysc_val & SYSC_OMAP4_SOFTRESET))
		ddata->cfg.quirks |= SYSC_QUIRK_RESET_STATUS;

	ddata->cfg.syss_mask = val;

	return 0;
}

/*
 * Many child device drivers need to have fck and opt clocks available
 * to get the clock rate for device internal configuration etc.
 */
static int sysc_child_add_named_clock(struct sysc *ddata,
				      struct device *child,
				      const char *name)
{
	struct clk *clk;
	struct clk_lookup *l;
	int error = 0;

	if (!name)
		return 0;

	clk = clk_get(child, name);
	if (!IS_ERR(clk)) {
		clk_put(clk);

		return -EEXIST;
	}

	clk = clk_get(ddata->dev, name);
	if (IS_ERR(clk))
		return -ENODEV;

	l = clkdev_create(clk, name, dev_name(child));
	if (!l)
		error = -ENOMEM;

	clk_put(clk);

	return error;
}

static int sysc_child_add_clocks(struct sysc *ddata,
				 struct device *child)
{
	int i, error;

	for (i = 0; i < ddata->nr_clocks; i++) {
		error = sysc_child_add_named_clock(ddata,
						   child,
						   ddata->clock_roles[i]);
		if (error && error != -EEXIST) {
			dev_err(ddata->dev, "could not add child clock %s: %i\n",
				ddata->clock_roles[i], error);

			return error;
		}
	}

	return 0;
}

static struct device_type sysc_device_type = {
};

static struct sysc *sysc_child_to_parent(struct device *dev)
{
	struct device *parent = dev->parent;

	if (!parent || parent->type != &sysc_device_type)
		return NULL;

	return dev_get_drvdata(parent);
}

static int __maybe_unused sysc_child_runtime_suspend(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = sysc_child_to_parent(dev);

	error = pm_generic_runtime_suspend(dev);
	if (error)
		return error;

	if (!ddata->enabled)
		return 0;

	return sysc_runtime_suspend(ddata->dev);
}

static int __maybe_unused sysc_child_runtime_resume(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = sysc_child_to_parent(dev);

	if (!ddata->enabled) {
		error = sysc_runtime_resume(ddata->dev);
		if (error < 0)
			dev_err(ddata->dev,
				"%s error: %i\n", __func__, error);
	}

	return pm_generic_runtime_resume(dev);
}

#ifdef CONFIG_PM_SLEEP
static int sysc_child_suspend_noirq(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = sysc_child_to_parent(dev);

	dev_dbg(ddata->dev, "%s %s\n", __func__,
		ddata->name ? ddata->name : "");

	error = pm_generic_suspend_noirq(dev);
	if (error) {
		dev_err(dev, "%s error at %i: %i\n",
			__func__, __LINE__, error);

		return error;
	}

	if (!pm_runtime_status_suspended(dev)) {
		error = pm_generic_runtime_suspend(dev);
		if (error) {
			dev_err(dev, "%s error at %i: %i\n",
				__func__, __LINE__, error);

			return error;
		}

		error = sysc_runtime_suspend(ddata->dev);
		if (error) {
			dev_err(dev, "%s error at %i: %i\n",
				__func__, __LINE__, error);

			return error;
		}

		ddata->child_needs_resume = true;
	}

	return 0;
}

static int sysc_child_resume_noirq(struct device *dev)
{
	struct sysc *ddata;
	int error;

	ddata = sysc_child_to_parent(dev);

	dev_dbg(ddata->dev, "%s %s\n", __func__,
		ddata->name ? ddata->name : "");

	if (ddata->child_needs_resume) {
		ddata->child_needs_resume = false;

		error = sysc_runtime_resume(ddata->dev);
		if (error)
			dev_err(ddata->dev,
				"%s runtime resume error: %i\n",
				__func__, error);

		error = pm_generic_runtime_resume(dev);
		if (error)
			dev_err(ddata->dev,
				"%s generic runtime resume: %i\n",
				__func__, error);
	}

	return pm_generic_resume_noirq(dev);
}
#endif

struct dev_pm_domain sysc_child_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(sysc_child_runtime_suspend,
				   sysc_child_runtime_resume,
				   NULL)
		USE_PLATFORM_PM_SLEEP_OPS
		SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(sysc_child_suspend_noirq,
					      sysc_child_resume_noirq)
	}
};

/**
 * sysc_legacy_idle_quirk - handle children in omap_device compatible way
 * @ddata: device driver data
 * @child: child device driver
 *
 * Allow idle for child devices as done with _od_runtime_suspend().
 * Otherwise many child devices will not idle because of the permanent
 * parent usecount set in pm_runtime_irq_safe().
 *
 * Note that the long term solution is to just modify the child device
 * drivers to not set pm_runtime_irq_safe() and then this can be just
 * dropped.
 */
static void sysc_legacy_idle_quirk(struct sysc *ddata, struct device *child)
{
	if (!ddata->legacy_mode)
		return;

	if (ddata->cfg.quirks & SYSC_QUIRK_LEGACY_IDLE)
		dev_pm_domain_set(child, &sysc_child_pm_domain);
}

static int sysc_notifier_call(struct notifier_block *nb,
			      unsigned long event, void *device)
{
	struct device *dev = device;
	struct sysc *ddata;
	int error;

	ddata = sysc_child_to_parent(dev);
	if (!ddata)
		return NOTIFY_DONE;

	switch (event) {
	case BUS_NOTIFY_ADD_DEVICE:
		error = sysc_child_add_clocks(ddata, dev);
		if (error)
			return error;
		sysc_legacy_idle_quirk(ddata, dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block sysc_nb = {
	.notifier_call = sysc_notifier_call,
};

/* Device tree configured quirks */
struct sysc_dts_quirk {
	const char *name;
	u32 mask;
};

static const struct sysc_dts_quirk sysc_dts_quirks[] = {
	{ .name = "ti,no-idle-on-init",
	  .mask = SYSC_QUIRK_NO_IDLE_ON_INIT, },
	{ .name = "ti,no-reset-on-init",
	  .mask = SYSC_QUIRK_NO_RESET_ON_INIT, },
};

static int sysc_init_dts_quirks(struct sysc *ddata)
{
	struct device_node *np = ddata->dev->of_node;
	const struct property *prop;
	int i, len, error;
	u32 val;

	ddata->legacy_mode = of_get_property(np, "ti,hwmods", NULL);

	for (i = 0; i < ARRAY_SIZE(sysc_dts_quirks); i++) {
		prop = of_get_property(np, sysc_dts_quirks[i].name, &len);
		if (!prop)
			continue;

		ddata->cfg.quirks |= sysc_dts_quirks[i].mask;
	}

	error = of_property_read_u32(np, "ti,sysc-delay-us", &val);
	if (!error) {
		if (val > 255) {
			dev_warn(ddata->dev, "bad ti,sysc-delay-us: %i\n",
				 val);
		}

		ddata->cfg.srst_udelay = (u8)val;
	}

	return 0;
}

static void sysc_unprepare(struct sysc *ddata)
{
	int i;

	for (i = 0; i < SYSC_MAX_CLOCKS; i++) {
		if (!IS_ERR_OR_NULL(ddata->clocks[i]))
			clk_unprepare(ddata->clocks[i]);
	}
}

/*
 * Common sysc register bits found on omap2, also known as type1
 */
static const struct sysc_regbits sysc_regbits_omap2 = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = 12,
	.sidle_shift = 3,
	.clkact_shift = 8,
	.emufree_shift = 5,
	.enwkup_shift = 2,
	.srst_shift = 1,
	.autoidle_shift = 0,
};

static const struct sysc_capabilities sysc_omap2 = {
	.type = TI_SYSC_OMAP2,
	.sysc_mask = SYSC_OMAP2_CLOCKACTIVITY | SYSC_OMAP2_EMUFREE |
		     SYSC_OMAP2_ENAWAKEUP | SYSC_OMAP2_SOFTRESET |
		     SYSC_OMAP2_AUTOIDLE,
	.regbits = &sysc_regbits_omap2,
};

/* All omap2 and 3 timers, and timers 1, 2 & 10 on omap 4 and 5 */
static const struct sysc_capabilities sysc_omap2_timer = {
	.type = TI_SYSC_OMAP2_TIMER,
	.sysc_mask = SYSC_OMAP2_CLOCKACTIVITY | SYSC_OMAP2_EMUFREE |
		     SYSC_OMAP2_ENAWAKEUP | SYSC_OMAP2_SOFTRESET |
		     SYSC_OMAP2_AUTOIDLE,
	.regbits = &sysc_regbits_omap2,
	.mod_quirks = SYSC_QUIRK_USE_CLOCKACT,
};

/*
 * SHAM2 (SHA1/MD5) sysc found on omap3, a variant of sysc_regbits_omap2
 * with different sidle position
 */
static const struct sysc_regbits sysc_regbits_omap3_sham = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = 4,
	.clkact_shift = -ENODEV,
	.enwkup_shift = -ENODEV,
	.srst_shift = 1,
	.autoidle_shift = 0,
	.emufree_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap3_sham = {
	.type = TI_SYSC_OMAP3_SHAM,
	.sysc_mask = SYSC_OMAP2_SOFTRESET | SYSC_OMAP2_AUTOIDLE,
	.regbits = &sysc_regbits_omap3_sham,
};

/*
 * AES register bits found on omap3 and later, a variant of
 * sysc_regbits_omap2 with different sidle position
 */
static const struct sysc_regbits sysc_regbits_omap3_aes = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = 6,
	.clkact_shift = -ENODEV,
	.enwkup_shift = -ENODEV,
	.srst_shift = 1,
	.autoidle_shift = 0,
	.emufree_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap3_aes = {
	.type = TI_SYSC_OMAP3_AES,
	.sysc_mask = SYSC_OMAP2_SOFTRESET | SYSC_OMAP2_AUTOIDLE,
	.regbits = &sysc_regbits_omap3_aes,
};

/*
 * Common sysc register bits found on omap4, also known as type2
 */
static const struct sysc_regbits sysc_regbits_omap4 = {
	.dmadisable_shift = 16,
	.midle_shift = 4,
	.sidle_shift = 2,
	.clkact_shift = -ENODEV,
	.enwkup_shift = -ENODEV,
	.emufree_shift = 1,
	.srst_shift = 0,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap4 = {
	.type = TI_SYSC_OMAP4,
	.sysc_mask = SYSC_OMAP4_DMADISABLE | SYSC_OMAP4_FREEEMU |
		     SYSC_OMAP4_SOFTRESET,
	.regbits = &sysc_regbits_omap4,
};

static const struct sysc_capabilities sysc_omap4_timer = {
	.type = TI_SYSC_OMAP4_TIMER,
	.sysc_mask = SYSC_OMAP4_DMADISABLE | SYSC_OMAP4_FREEEMU |
		     SYSC_OMAP4_SOFTRESET,
	.regbits = &sysc_regbits_omap4,
};

/*
 * Common sysc register bits found on omap4, also known as type3
 */
static const struct sysc_regbits sysc_regbits_omap4_simple = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = 2,
	.sidle_shift = 0,
	.clkact_shift = -ENODEV,
	.enwkup_shift = -ENODEV,
	.srst_shift = -ENODEV,
	.emufree_shift = -ENODEV,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap4_simple = {
	.type = TI_SYSC_OMAP4_SIMPLE,
	.regbits = &sysc_regbits_omap4_simple,
};

/*
 * SmartReflex sysc found on omap34xx
 */
static const struct sysc_regbits sysc_regbits_omap34xx_sr = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = -ENODEV,
	.clkact_shift = 20,
	.enwkup_shift = -ENODEV,
	.srst_shift = -ENODEV,
	.emufree_shift = -ENODEV,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_34xx_sr = {
	.type = TI_SYSC_OMAP34XX_SR,
	.sysc_mask = SYSC_OMAP2_CLOCKACTIVITY,
	.regbits = &sysc_regbits_omap34xx_sr,
	.mod_quirks = SYSC_QUIRK_USE_CLOCKACT | SYSC_QUIRK_UNCACHED |
		      SYSC_QUIRK_LEGACY_IDLE,
};

/*
 * SmartReflex sysc found on omap36xx and later
 */
static const struct sysc_regbits sysc_regbits_omap36xx_sr = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = 24,
	.clkact_shift = -ENODEV,
	.enwkup_shift = 26,
	.srst_shift = -ENODEV,
	.emufree_shift = -ENODEV,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_36xx_sr = {
	.type = TI_SYSC_OMAP36XX_SR,
	.sysc_mask = SYSC_OMAP3_SR_ENAWAKEUP,
	.regbits = &sysc_regbits_omap36xx_sr,
	.mod_quirks = SYSC_QUIRK_UNCACHED | SYSC_QUIRK_LEGACY_IDLE,
};

static const struct sysc_capabilities sysc_omap4_sr = {
	.type = TI_SYSC_OMAP4_SR,
	.regbits = &sysc_regbits_omap36xx_sr,
	.mod_quirks = SYSC_QUIRK_LEGACY_IDLE,
};

/*
 * McASP register bits found on omap4 and later
 */
static const struct sysc_regbits sysc_regbits_omap4_mcasp = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = 0,
	.clkact_shift = -ENODEV,
	.enwkup_shift = -ENODEV,
	.srst_shift = -ENODEV,
	.emufree_shift = -ENODEV,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap4_mcasp = {
	.type = TI_SYSC_OMAP4_MCASP,
	.regbits = &sysc_regbits_omap4_mcasp,
};

/*
 * FS USB host found on omap4 and later
 */
static const struct sysc_regbits sysc_regbits_omap4_usb_host_fs = {
	.dmadisable_shift = -ENODEV,
	.midle_shift = -ENODEV,
	.sidle_shift = 24,
	.clkact_shift = -ENODEV,
	.enwkup_shift = 26,
	.srst_shift = -ENODEV,
	.emufree_shift = -ENODEV,
	.autoidle_shift = -ENODEV,
};

static const struct sysc_capabilities sysc_omap4_usb_host_fs = {
	.type = TI_SYSC_OMAP4_USB_HOST_FS,
	.sysc_mask = SYSC_OMAP2_ENAWAKEUP,
	.regbits = &sysc_regbits_omap4_usb_host_fs,
};

static int sysc_init_pdata(struct sysc *ddata)
{
	struct ti_sysc_platform_data *pdata = dev_get_platdata(ddata->dev);
	struct ti_sysc_module_data mdata;
	int error = 0;

	if (!pdata || !ddata->legacy_mode)
		return 0;

	mdata.name = ddata->legacy_mode;
	mdata.module_pa = ddata->module_pa;
	mdata.module_size = ddata->module_size;
	mdata.offsets = ddata->offsets;
	mdata.nr_offsets = SYSC_MAX_REGS;
	mdata.cap = ddata->cap;
	mdata.cfg = &ddata->cfg;

	if (!pdata->init_module)
		return -ENODEV;

	error = pdata->init_module(ddata->dev, &mdata, &ddata->cookie);
	if (error == -EEXIST)
		error = 0;

	return error;
}

static int sysc_init_match(struct sysc *ddata)
{
	const struct sysc_capabilities *cap;

	cap = of_device_get_match_data(ddata->dev);
	if (!cap)
		return -EINVAL;

	ddata->cap = cap;
	if (ddata->cap)
		ddata->cfg.quirks |= ddata->cap->mod_quirks;

	return 0;
}

static void ti_sysc_idle(struct work_struct *work)
{
	struct sysc *ddata;

	ddata = container_of(work, struct sysc, idle_work.work);

	if (pm_runtime_active(ddata->dev))
		pm_runtime_put_sync(ddata->dev);
}

static const struct of_device_id sysc_match_table[] = {
	{ .compatible = "simple-bus", },
	{ /* sentinel */ },
};

static int sysc_probe(struct platform_device *pdev)
{
	struct ti_sysc_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct sysc *ddata;
	int error;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	platform_set_drvdata(pdev, ddata);

	error = sysc_init_match(ddata);
	if (error)
		return error;

	error = sysc_init_dts_quirks(ddata);
	if (error)
		goto unprepare;

	error = sysc_get_clocks(ddata);
	if (error)
		return error;

	error = sysc_map_and_check_registers(ddata);
	if (error)
		goto unprepare;

	error = sysc_init_sysc_mask(ddata);
	if (error)
		goto unprepare;

	error = sysc_init_idlemodes(ddata);
	if (error)
		goto unprepare;

	error = sysc_init_syss_mask(ddata);
	if (error)
		goto unprepare;

	error = sysc_init_pdata(ddata);
	if (error)
		goto unprepare;

	error = sysc_init_resets(ddata);
	if (error)
		return error;

	pm_runtime_enable(ddata->dev);
	error = sysc_init_module(ddata);
	if (error)
		goto unprepare;

	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);
		pm_runtime_disable(ddata->dev);
		goto unprepare;
	}

	sysc_show_registers(ddata);

	ddata->dev->type = &sysc_device_type;
	error = of_platform_populate(ddata->dev->of_node, sysc_match_table,
				     pdata ? pdata->auxdata : NULL,
				     ddata->dev);
	if (error)
		goto err;

	INIT_DELAYED_WORK(&ddata->idle_work, ti_sysc_idle);

	/* At least earlycon won't survive without deferred idle */
	if (ddata->cfg.quirks & (SYSC_QUIRK_NO_IDLE_ON_INIT |
				 SYSC_QUIRK_NO_RESET_ON_INIT)) {
		schedule_delayed_work(&ddata->idle_work, 3000);
	} else {
		pm_runtime_put(&pdev->dev);
	}

	if (!of_get_available_child_count(ddata->dev->of_node))
		reset_control_assert(ddata->rsts);

	return 0;

err:
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

	cancel_delayed_work_sync(&ddata->idle_work);

	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);
		pm_runtime_disable(ddata->dev);
		goto unprepare;
	}

	of_platform_depopulate(&pdev->dev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(ddata->rsts);

unprepare:
	sysc_unprepare(ddata);

	return 0;
}

static const struct of_device_id sysc_match[] = {
	{ .compatible = "ti,sysc-omap2", .data = &sysc_omap2, },
	{ .compatible = "ti,sysc-omap2-timer", .data = &sysc_omap2_timer, },
	{ .compatible = "ti,sysc-omap4", .data = &sysc_omap4, },
	{ .compatible = "ti,sysc-omap4-timer", .data = &sysc_omap4_timer, },
	{ .compatible = "ti,sysc-omap4-simple", .data = &sysc_omap4_simple, },
	{ .compatible = "ti,sysc-omap3430-sr", .data = &sysc_34xx_sr, },
	{ .compatible = "ti,sysc-omap3630-sr", .data = &sysc_36xx_sr, },
	{ .compatible = "ti,sysc-omap4-sr", .data = &sysc_omap4_sr, },
	{ .compatible = "ti,sysc-omap3-sham", .data = &sysc_omap3_sham, },
	{ .compatible = "ti,sysc-omap-aes", .data = &sysc_omap3_aes, },
	{ .compatible = "ti,sysc-mcasp", .data = &sysc_omap4_mcasp, },
	{ .compatible = "ti,sysc-usb-host-fs",
	  .data = &sysc_omap4_usb_host_fs, },
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

static int __init sysc_init(void)
{
	bus_register_notifier(&platform_bus_type, &sysc_nb);

	return platform_driver_register(&sysc_driver);
}
module_init(sysc_init);

static void __exit sysc_exit(void)
{
	bus_unregister_notifier(&platform_bus_type, &sysc_nb);
	platform_driver_unregister(&sysc_driver);
}
module_exit(sysc_exit);

MODULE_DESCRIPTION("TI sysc interconnect target driver");
MODULE_LICENSE("GPL v2");
