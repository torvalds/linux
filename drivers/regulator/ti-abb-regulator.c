// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instruments SoC Adaptive Body Bias(ABB) Regulator
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Mike Turquette <mturquette@ti.com>
 *
 * Copyright (C) 2012-2013 Texas Instruments, Inc.
 * Andrii Tseglytskyi <andrii.tseglytskyi@ti.com>
 * Nishanth Menon <nm@ti.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

/*
 * ABB LDO operating states:
 * NOMINAL_OPP:	bypasses the ABB LDO
 * FAST_OPP:	sets ABB LDO to Forward Body-Bias
 * SLOW_OPP:	sets ABB LDO to Reverse Body-Bias
 */
#define TI_ABB_NOMINAL_OPP	0
#define TI_ABB_FAST_OPP		1
#define TI_ABB_SLOW_OPP		3

/**
 * struct ti_abb_info - ABB information per voltage setting
 * @opp_sel:	one of TI_ABB macro
 * @vset:	(optional) vset value that LDOVBB needs to be overridden with.
 *
 * Array of per voltage entries organized in the same order as regulator_desc's
 * volt_table list. (selector is used to index from this array)
 */
struct ti_abb_info {
	u32 opp_sel;
	u32 vset;
};

/**
 * struct ti_abb_reg - Register description for ABB block
 * @setup_off:			setup register offset from base
 * @control_off:		control register offset from base
 * @sr2_wtcnt_value_mask:	setup register- sr2_wtcnt_value mask
 * @fbb_sel_mask:		setup register- FBB sel mask
 * @rbb_sel_mask:		setup register- RBB sel mask
 * @sr2_en_mask:		setup register- enable mask
 * @opp_change_mask:		control register - mask to trigger LDOVBB change
 * @opp_sel_mask:		control register - mask for mode to operate
 */
struct ti_abb_reg {
	u32 setup_off;
	u32 control_off;

	/* Setup register fields */
	u32 sr2_wtcnt_value_mask;
	u32 fbb_sel_mask;
	u32 rbb_sel_mask;
	u32 sr2_en_mask;

	/* Control register fields */
	u32 opp_change_mask;
	u32 opp_sel_mask;
};

/**
 * struct ti_abb - ABB instance data
 * @rdesc:			regulator descriptor
 * @clk:			clock(usually sysclk) supplying ABB block
 * @base:			base address of ABB block
 * @setup_reg:			setup register of ABB block
 * @control_reg:		control register of ABB block
 * @int_base:			interrupt register base address
 * @efuse_base:			(optional) efuse base address for ABB modes
 * @ldo_base:			(optional) LDOVBB vset override base address
 * @regs:			pointer to struct ti_abb_reg for ABB block
 * @txdone_mask:		mask on int_base for tranxdone interrupt
 * @ldovbb_override_mask:	mask to ldo_base for overriding default LDO VBB
 *				vset with value from efuse
 * @ldovbb_vset_mask:		mask to ldo_base for providing the VSET override
 * @info:			array to per voltage ABB configuration
 * @current_info_idx:		current index to info
 * @settling_time:		SoC specific settling time for LDO VBB
 */
struct ti_abb {
	struct regulator_desc rdesc;
	struct clk *clk;
	void __iomem *base;
	void __iomem *setup_reg;
	void __iomem *control_reg;
	void __iomem *int_base;
	void __iomem *efuse_base;
	void __iomem *ldo_base;

	const struct ti_abb_reg *regs;
	u32 txdone_mask;
	u32 ldovbb_override_mask;
	u32 ldovbb_vset_mask;

	struct ti_abb_info *info;
	int current_info_idx;

	u32 settling_time;
};

/**
 * ti_abb_rmw() - handy wrapper to set specific register bits
 * @mask:	mask for register field
 * @value:	value shifted to mask location and written
 * @reg:	register address
 *
 * Return: final register value (may be unused)
 */
static inline u32 ti_abb_rmw(u32 mask, u32 value, void __iomem *reg)
{
	u32 val;

	val = readl(reg);
	val &= ~mask;
	val |= (value << __ffs(mask)) & mask;
	writel(val, reg);

	return val;
}

/**
 * ti_abb_check_txdone() - handy wrapper to check ABB tranxdone status
 * @abb:	pointer to the abb instance
 *
 * Return: true or false
 */
static inline bool ti_abb_check_txdone(const struct ti_abb *abb)
{
	return !!(readl(abb->int_base) & abb->txdone_mask);
}

/**
 * ti_abb_clear_txdone() - handy wrapper to clear ABB tranxdone status
 * @abb:	pointer to the abb instance
 */
static inline void ti_abb_clear_txdone(const struct ti_abb *abb)
{
	writel(abb->txdone_mask, abb->int_base);
};

/**
 * ti_abb_wait_txdone() - waits for ABB tranxdone event
 * @dev:	device
 * @abb:	pointer to the abb instance
 *
 * Return: 0 on success or -ETIMEDOUT if the event is not cleared on time.
 */
static int ti_abb_wait_txdone(struct device *dev, struct ti_abb *abb)
{
	int timeout = 0;
	bool status;

	while (timeout++ <= abb->settling_time) {
		status = ti_abb_check_txdone(abb);
		if (status)
			return 0;

		udelay(1);
	}

	dev_warn_ratelimited(dev, "%s:TRANXDONE timeout(%duS) int=0x%08x\n",
			     __func__, timeout, readl(abb->int_base));
	return -ETIMEDOUT;
}

/**
 * ti_abb_clear_all_txdone() - clears ABB tranxdone event
 * @dev:	device
 * @abb:	pointer to the abb instance
 *
 * Return: 0 on success or -ETIMEDOUT if the event is not cleared on time.
 */
static int ti_abb_clear_all_txdone(struct device *dev, const struct ti_abb *abb)
{
	int timeout = 0;
	bool status;

	while (timeout++ <= abb->settling_time) {
		ti_abb_clear_txdone(abb);

		status = ti_abb_check_txdone(abb);
		if (!status)
			return 0;

		udelay(1);
	}

	dev_warn_ratelimited(dev, "%s:TRANXDONE timeout(%duS) int=0x%08x\n",
			     __func__, timeout, readl(abb->int_base));
	return -ETIMEDOUT;
}

/**
 * ti_abb_program_ldovbb() - program LDOVBB register for override value
 * @dev:	device
 * @abb:	pointer to the abb instance
 * @info:	ABB info to program
 */
static void ti_abb_program_ldovbb(struct device *dev, const struct ti_abb *abb,
				  struct ti_abb_info *info)
{
	u32 val;

	val = readl(abb->ldo_base);
	/* clear up previous values */
	val &= ~(abb->ldovbb_override_mask | abb->ldovbb_vset_mask);

	switch (info->opp_sel) {
	case TI_ABB_SLOW_OPP:
	case TI_ABB_FAST_OPP:
		val |= abb->ldovbb_override_mask;
		val |= info->vset << __ffs(abb->ldovbb_vset_mask);
		break;
	}

	writel(val, abb->ldo_base);
}

/**
 * ti_abb_set_opp() - Setup ABB and LDO VBB for required bias
 * @rdev:	regulator device
 * @abb:	pointer to the abb instance
 * @info:	ABB info to program
 *
 * Return: 0 on success or appropriate error value when fails
 */
static int ti_abb_set_opp(struct regulator_dev *rdev, struct ti_abb *abb,
			  struct ti_abb_info *info)
{
	const struct ti_abb_reg *regs = abb->regs;
	struct device *dev = &rdev->dev;
	int ret;

	ret = ti_abb_clear_all_txdone(dev, abb);
	if (ret)
		goto out;

	ti_abb_rmw(regs->fbb_sel_mask | regs->rbb_sel_mask, 0, abb->setup_reg);

	switch (info->opp_sel) {
	case TI_ABB_SLOW_OPP:
		ti_abb_rmw(regs->rbb_sel_mask, 1, abb->setup_reg);
		break;
	case TI_ABB_FAST_OPP:
		ti_abb_rmw(regs->fbb_sel_mask, 1, abb->setup_reg);
		break;
	}

	/* program next state of ABB ldo */
	ti_abb_rmw(regs->opp_sel_mask, info->opp_sel, abb->control_reg);

	/*
	 * program LDO VBB vset override if needed for !bypass mode
	 * XXX: Do not switch sequence - for !bypass, LDO override reset *must*
	 * be performed *before* switch to bias mode else VBB glitches.
	 */
	if (abb->ldo_base && info->opp_sel != TI_ABB_NOMINAL_OPP)
		ti_abb_program_ldovbb(dev, abb, info);

	/* Initiate ABB ldo change */
	ti_abb_rmw(regs->opp_change_mask, 1, abb->control_reg);

	/* Wait for ABB LDO to complete transition to new Bias setting */
	ret = ti_abb_wait_txdone(dev, abb);
	if (ret)
		goto out;

	ret = ti_abb_clear_all_txdone(dev, abb);
	if (ret)
		goto out;

	/*
	 * Reset LDO VBB vset override bypass mode
	 * XXX: Do not switch sequence - for bypass, LDO override reset *must*
	 * be performed *after* switch to bypass else VBB glitches.
	 */
	if (abb->ldo_base && info->opp_sel == TI_ABB_NOMINAL_OPP)
		ti_abb_program_ldovbb(dev, abb, info);

out:
	return ret;
}

/**
 * ti_abb_set_voltage_sel() - regulator accessor function to set ABB LDO
 * @rdev:	regulator device
 * @sel:	selector to index into required ABB LDO settings (maps to
 *		regulator descriptor's volt_table)
 *
 * Return: 0 on success or appropriate error value when fails
 */
static int ti_abb_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	const struct regulator_desc *desc = rdev->desc;
	struct ti_abb *abb = rdev_get_drvdata(rdev);
	struct device *dev = &rdev->dev;
	struct ti_abb_info *info, *oinfo;
	int ret = 0;

	if (!abb) {
		dev_err_ratelimited(dev, "%s: No regulator drvdata\n",
				    __func__);
		return -ENODEV;
	}

	if (!desc->n_voltages || !abb->info) {
		dev_err_ratelimited(dev,
				    "%s: No valid voltage table entries?\n",
				    __func__);
		return -EINVAL;
	}

	if (sel >= desc->n_voltages) {
		dev_err(dev, "%s: sel idx(%d) >= n_voltages(%d)\n", __func__,
			sel, desc->n_voltages);
		return -EINVAL;
	}

	/* If we are in the same index as we were, nothing to do here! */
	if (sel == abb->current_info_idx) {
		dev_dbg(dev, "%s: Already at sel=%d\n", __func__, sel);
		return ret;
	}

	info = &abb->info[sel];
	/*
	 * When Linux kernel is starting up, we aren't sure of the
	 * Bias configuration that bootloader has configured.
	 * So, we get to know the actual setting the first time
	 * we are asked to transition.
	 */
	if (abb->current_info_idx == -EINVAL)
		goto just_set_abb;

	/* If data is exactly the same, then just update index, no change */
	oinfo = &abb->info[abb->current_info_idx];
	if (!memcmp(info, oinfo, sizeof(*info))) {
		dev_dbg(dev, "%s: Same data new idx=%d, old idx=%d\n", __func__,
			sel, abb->current_info_idx);
		goto out;
	}

just_set_abb:
	ret = ti_abb_set_opp(rdev, abb, info);

out:
	if (!ret)
		abb->current_info_idx = sel;
	else
		dev_err_ratelimited(dev,
				    "%s: Volt[%d] idx[%d] mode[%d] Fail(%d)\n",
				    __func__, desc->volt_table[sel], sel,
				    info->opp_sel, ret);
	return ret;
}

/**
 * ti_abb_get_voltage_sel() - Regulator accessor to get current ABB LDO setting
 * @rdev:	regulator device
 *
 * Return: 0 on success or appropriate error value when fails
 */
static int ti_abb_get_voltage_sel(struct regulator_dev *rdev)
{
	const struct regulator_desc *desc = rdev->desc;
	struct ti_abb *abb = rdev_get_drvdata(rdev);
	struct device *dev = &rdev->dev;

	if (!abb) {
		dev_err_ratelimited(dev, "%s: No regulator drvdata\n",
				    __func__);
		return -ENODEV;
	}

	if (!desc->n_voltages || !abb->info) {
		dev_err_ratelimited(dev,
				    "%s: No valid voltage table entries?\n",
				    __func__);
		return -EINVAL;
	}

	if (abb->current_info_idx >= (int)desc->n_voltages) {
		dev_err(dev, "%s: Corrupted data? idx(%d) >= n_voltages(%d)\n",
			__func__, abb->current_info_idx, desc->n_voltages);
		return -EINVAL;
	}

	return abb->current_info_idx;
}

/**
 * ti_abb_init_timings() - setup ABB clock timing for the current platform
 * @dev:	device
 * @abb:	pointer to the abb instance
 *
 * Return: 0 if timing is updated, else returns error result.
 */
static int ti_abb_init_timings(struct device *dev, struct ti_abb *abb)
{
	u32 clock_cycles;
	u32 clk_rate, sr2_wt_cnt_val, cycle_rate;
	const struct ti_abb_reg *regs = abb->regs;
	int ret;
	char *pname = "ti,settling-time";

	/* read device tree properties */
	ret = of_property_read_u32(dev->of_node, pname, &abb->settling_time);
	if (ret) {
		dev_err(dev, "Unable to get property '%s'(%d)\n", pname, ret);
		return ret;
	}

	/* ABB LDO cannot be settle in 0 time */
	if (!abb->settling_time) {
		dev_err(dev, "Invalid property:'%s' set as 0!\n", pname);
		return -EINVAL;
	}

	pname = "ti,clock-cycles";
	ret = of_property_read_u32(dev->of_node, pname, &clock_cycles);
	if (ret) {
		dev_err(dev, "Unable to get property '%s'(%d)\n", pname, ret);
		return ret;
	}
	/* ABB LDO cannot be settle in 0 clock cycles */
	if (!clock_cycles) {
		dev_err(dev, "Invalid property:'%s' set as 0!\n", pname);
		return -EINVAL;
	}

	abb->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(abb->clk)) {
		ret = PTR_ERR(abb->clk);
		dev_err(dev, "%s: Unable to get clk(%d)\n", __func__, ret);
		return ret;
	}

	/*
	 * SR2_WTCNT_VALUE is the settling time for the ABB ldo after a
	 * transition and must be programmed with the correct time at boot.
	 * The value programmed into the register is the number of SYS_CLK
	 * clock cycles that match a given wall time profiled for the ldo.
	 * This value depends on:
	 * settling time of ldo in micro-seconds (varies per OMAP family)
	 * # of clock cycles per SYS_CLK period (varies per OMAP family)
	 * the SYS_CLK frequency in MHz (varies per board)
	 * The formula is:
	 *
	 *                      ldo settling time (in micro-seconds)
	 * SR2_WTCNT_VALUE = ------------------------------------------
	 *                   (# system clock cycles) * (sys_clk period)
	 *
	 * Put another way:
	 *
	 * SR2_WTCNT_VALUE = settling time / (# SYS_CLK cycles / SYS_CLK rate))
	 *
	 * To avoid dividing by zero multiply both "# clock cycles" and
	 * "settling time" by 10 such that the final result is the one we want.
	 */

	/* Convert SYS_CLK rate to MHz & prevent divide by zero */
	clk_rate = DIV_ROUND_CLOSEST(clk_get_rate(abb->clk), 1000000);

	/* Calculate cycle rate */
	cycle_rate = DIV_ROUND_CLOSEST(clock_cycles * 10, clk_rate);

	/* Calculate SR2_WTCNT_VALUE */
	sr2_wt_cnt_val = DIV_ROUND_CLOSEST(abb->settling_time * 10, cycle_rate);

	dev_dbg(dev, "%s: Clk_rate=%ld, sr2_cnt=0x%08x\n", __func__,
		clk_get_rate(abb->clk), sr2_wt_cnt_val);

	ti_abb_rmw(regs->sr2_wtcnt_value_mask, sr2_wt_cnt_val, abb->setup_reg);

	return 0;
}

/**
 * ti_abb_init_table() - Initialize ABB table from device tree
 * @dev:	device
 * @abb:	pointer to the abb instance
 * @rinit_data:	regulator initdata
 *
 * Return: 0 on success or appropriate error value when fails
 */
static int ti_abb_init_table(struct device *dev, struct ti_abb *abb,
			     struct regulator_init_data *rinit_data)
{
	struct ti_abb_info *info;
	const u32 num_values = 6;
	char *pname = "ti,abb_info";
	u32 i;
	unsigned int *volt_table;
	int num_entries, min_uV = INT_MAX, max_uV = 0;
	struct regulation_constraints *c = &rinit_data->constraints;

	/*
	 * Each abb_info is a set of n-tuple, where n is num_values, consisting
	 * of voltage and a set of detection logic for ABB information for that
	 * voltage to apply.
	 */
	num_entries = of_property_count_u32_elems(dev->of_node, pname);
	if (num_entries < 0) {
		dev_err(dev, "No '%s' property?\n", pname);
		return num_entries;
	}

	if (!num_entries || (num_entries % num_values)) {
		dev_err(dev, "All '%s' list entries need %d vals\n", pname,
			num_values);
		return -EINVAL;
	}
	num_entries /= num_values;

	info = devm_kcalloc(dev, num_entries, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	abb->info = info;

	volt_table = devm_kcalloc(dev, num_entries, sizeof(unsigned int),
				  GFP_KERNEL);
	if (!volt_table)
		return -ENOMEM;

	abb->rdesc.n_voltages = num_entries;
	abb->rdesc.volt_table = volt_table;
	/* We do not know where the OPP voltage is at the moment */
	abb->current_info_idx = -EINVAL;

	for (i = 0; i < num_entries; i++, info++, volt_table++) {
		u32 efuse_offset, rbb_mask, fbb_mask, vset_mask;
		u32 efuse_val;

		/* NOTE: num_values should equal to entries picked up here */
		of_property_read_u32_index(dev->of_node, pname, i * num_values,
					   volt_table);
		of_property_read_u32_index(dev->of_node, pname,
					   i * num_values + 1, &info->opp_sel);
		of_property_read_u32_index(dev->of_node, pname,
					   i * num_values + 2, &efuse_offset);
		of_property_read_u32_index(dev->of_node, pname,
					   i * num_values + 3, &rbb_mask);
		of_property_read_u32_index(dev->of_node, pname,
					   i * num_values + 4, &fbb_mask);
		of_property_read_u32_index(dev->of_node, pname,
					   i * num_values + 5, &vset_mask);

		dev_dbg(dev,
			"[%d]v=%d ABB=%d ef=0x%x rbb=0x%x fbb=0x%x vset=0x%x\n",
			i, *volt_table, info->opp_sel, efuse_offset, rbb_mask,
			fbb_mask, vset_mask);

		/* Find min/max for voltage set */
		if (min_uV > *volt_table)
			min_uV = *volt_table;
		if (max_uV < *volt_table)
			max_uV = *volt_table;

		if (!abb->efuse_base) {
			/* Ignore invalid data, but warn to help cleanup */
			if (efuse_offset || rbb_mask || fbb_mask || vset_mask)
				dev_err(dev, "prop '%s': v=%d,bad efuse/mask\n",
					pname, *volt_table);
			goto check_abb;
		}

		efuse_val = readl(abb->efuse_base + efuse_offset);

		/* Use ABB recommendation from Efuse */
		if (efuse_val & rbb_mask)
			info->opp_sel = TI_ABB_SLOW_OPP;
		else if (efuse_val & fbb_mask)
			info->opp_sel = TI_ABB_FAST_OPP;
		else if (rbb_mask || fbb_mask)
			info->opp_sel = TI_ABB_NOMINAL_OPP;

		dev_dbg(dev,
			"[%d]v=%d efusev=0x%x final ABB=%d\n",
			i, *volt_table, efuse_val, info->opp_sel);

		/* Use recommended Vset bits from Efuse */
		if (!abb->ldo_base) {
			if (vset_mask)
				dev_err(dev, "prop'%s':v=%d vst=%x LDO base?\n",
					pname, *volt_table, vset_mask);
			continue;
		}
		info->vset = (efuse_val & vset_mask) >> __ffs(vset_mask);
		dev_dbg(dev, "[%d]v=%d vset=%x\n", i, *volt_table, info->vset);
check_abb:
		switch (info->opp_sel) {
		case TI_ABB_NOMINAL_OPP:
		case TI_ABB_FAST_OPP:
		case TI_ABB_SLOW_OPP:
			/* Valid values */
			break;
		default:
			dev_err(dev, "%s:[%d]v=%d, ABB=%d is invalid! Abort!\n",
				__func__, i, *volt_table, info->opp_sel);
			return -EINVAL;
		}
	}

	/* Setup the min/max voltage constraints from the supported list */
	c->min_uV = min_uV;
	c->max_uV = max_uV;

	return 0;
}

static const struct regulator_ops ti_abb_reg_ops = {
	.list_voltage = regulator_list_voltage_table,

	.set_voltage_sel = ti_abb_set_voltage_sel,
	.get_voltage_sel = ti_abb_get_voltage_sel,
};

/* Default ABB block offsets, IF this changes in future, create new one */
static const struct ti_abb_reg abb_regs_v1 = {
	/* WARNING: registers are wrongly documented in TRM */
	.setup_off		= 0x04,
	.control_off		= 0x00,

	.sr2_wtcnt_value_mask	= (0xff << 8),
	.fbb_sel_mask		= (0x01 << 2),
	.rbb_sel_mask		= (0x01 << 1),
	.sr2_en_mask		= (0x01 << 0),

	.opp_change_mask	= (0x01 << 2),
	.opp_sel_mask		= (0x03 << 0),
};

static const struct ti_abb_reg abb_regs_v2 = {
	.setup_off		= 0x00,
	.control_off		= 0x04,

	.sr2_wtcnt_value_mask	= (0xff << 8),
	.fbb_sel_mask		= (0x01 << 2),
	.rbb_sel_mask		= (0x01 << 1),
	.sr2_en_mask		= (0x01 << 0),

	.opp_change_mask	= (0x01 << 2),
	.opp_sel_mask		= (0x03 << 0),
};

static const struct ti_abb_reg abb_regs_generic = {
	.sr2_wtcnt_value_mask	= (0xff << 8),
	.fbb_sel_mask		= (0x01 << 2),
	.rbb_sel_mask		= (0x01 << 1),
	.sr2_en_mask		= (0x01 << 0),

	.opp_change_mask	= (0x01 << 2),
	.opp_sel_mask		= (0x03 << 0),
};

static const struct of_device_id ti_abb_of_match[] = {
	{.compatible = "ti,abb-v1", .data = &abb_regs_v1},
	{.compatible = "ti,abb-v2", .data = &abb_regs_v2},
	{.compatible = "ti,abb-v3", .data = &abb_regs_generic},
	{ },
};

MODULE_DEVICE_TABLE(of, ti_abb_of_match);

/**
 * ti_abb_probe() - Initialize an ABB ldo instance
 * @pdev: ABB platform device
 *
 * Initializes an individual ABB LDO for required Body-Bias. ABB is used to
 * additional bias supply to SoC modules for power savings or mandatory stability
 * configuration at certain Operating Performance Points(OPPs).
 *
 * Return: 0 on success or appropriate error value when fails
 */
static int ti_abb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct resource *res;
	struct ti_abb *abb;
	struct regulator_init_data *initdata = NULL;
	struct regulator_dev *rdev = NULL;
	struct regulator_desc *desc;
	struct regulation_constraints *c;
	struct regulator_config config = { };
	char *pname;
	int ret = 0;

	match = of_match_device(ti_abb_of_match, dev);
	if (!match) {
		/* We do not expect this to happen */
		dev_err(dev, "%s: Unable to match device\n", __func__);
		return -ENODEV;
	}
	if (!match->data) {
		dev_err(dev, "%s: Bad data in match\n", __func__);
		return -EINVAL;
	}

	abb = devm_kzalloc(dev, sizeof(struct ti_abb), GFP_KERNEL);
	if (!abb)
		return -ENOMEM;
	abb->regs = match->data;

	/* Map ABB resources */
	if (abb->regs->setup_off || abb->regs->control_off) {
		abb->base = devm_platform_ioremap_resource_byname(pdev, "base-address");
		if (IS_ERR(abb->base))
			return PTR_ERR(abb->base);

		abb->setup_reg = abb->base + abb->regs->setup_off;
		abb->control_reg = abb->base + abb->regs->control_off;

	} else {
		abb->control_reg = devm_platform_ioremap_resource_byname(pdev, "control-address");
		if (IS_ERR(abb->control_reg))
			return PTR_ERR(abb->control_reg);

		abb->setup_reg = devm_platform_ioremap_resource_byname(pdev, "setup-address");
		if (IS_ERR(abb->setup_reg))
			return PTR_ERR(abb->setup_reg);
	}

	pname = "int-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return -ENODEV;
	}
	/*
	 * The MPU interrupt status register (PRM_IRQSTATUS_MPU) is
	 * shared between regulator-abb-{ivahd,dspeve,gpu} driver
	 * instances. Therefore use devm_ioremap() rather than
	 * devm_platform_ioremap_resource_byname() to avoid busy
	 * resource region conflicts.
	 */
	abb->int_base = devm_ioremap(dev, res->start,
					     resource_size(res));
	if (!abb->int_base) {
		dev_err(dev, "Unable to map '%s'\n", pname);
		return -ENOMEM;
	}

	/* Map Optional resources */
	pname = "efuse-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_dbg(dev, "Missing '%s' IO resource\n", pname);
		ret = -ENODEV;
		goto skip_opt;
	}

	/*
	 * We may have shared efuse register offsets which are read-only
	 * between domains
	 */
	abb->efuse_base = devm_ioremap(dev, res->start,
					       resource_size(res));
	if (!abb->efuse_base) {
		dev_err(dev, "Unable to map '%s'\n", pname);
		return -ENOMEM;
	}

	pname = "ldo-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_dbg(dev, "Missing '%s' IO resource\n", pname);
		ret = -ENODEV;
		goto skip_opt;
	}
	abb->ldo_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(abb->ldo_base))
		return PTR_ERR(abb->ldo_base);

	/* IF ldo_base is set, the following are mandatory */
	pname = "ti,ldovbb-override-mask";
	ret =
	    of_property_read_u32(pdev->dev.of_node, pname,
				 &abb->ldovbb_override_mask);
	if (ret) {
		dev_err(dev, "Missing '%s' (%d)\n", pname, ret);
		return ret;
	}
	if (!abb->ldovbb_override_mask) {
		dev_err(dev, "Invalid property:'%s' set as 0!\n", pname);
		return -EINVAL;
	}

	pname = "ti,ldovbb-vset-mask";
	ret =
	    of_property_read_u32(pdev->dev.of_node, pname,
				 &abb->ldovbb_vset_mask);
	if (ret) {
		dev_err(dev, "Missing '%s' (%d)\n", pname, ret);
		return ret;
	}
	if (!abb->ldovbb_vset_mask) {
		dev_err(dev, "Invalid property:'%s' set as 0!\n", pname);
		return -EINVAL;
	}

skip_opt:
	pname = "ti,tranxdone-status-mask";
	ret =
	    of_property_read_u32(pdev->dev.of_node, pname,
				 &abb->txdone_mask);
	if (ret) {
		dev_err(dev, "Missing '%s' (%d)\n", pname, ret);
		return ret;
	}
	if (!abb->txdone_mask) {
		dev_err(dev, "Invalid property:'%s' set as 0!\n", pname);
		return -EINVAL;
	}

	initdata = of_get_regulator_init_data(dev, pdev->dev.of_node,
					      &abb->rdesc);
	if (!initdata) {
		dev_err(dev, "%s: Unable to alloc regulator init data\n",
			__func__);
		return -ENOMEM;
	}

	/* init ABB opp_sel table */
	ret = ti_abb_init_table(dev, abb, initdata);
	if (ret)
		return ret;

	/* init ABB timing */
	ret = ti_abb_init_timings(dev, abb);
	if (ret)
		return ret;

	desc = &abb->rdesc;
	desc->name = dev_name(dev);
	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->ops = &ti_abb_reg_ops;

	c = &initdata->constraints;
	if (desc->n_voltages > 1)
		c->valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE;
	c->always_on = true;

	config.dev = dev;
	config.init_data = initdata;
	config.driver_data = abb;
	config.of_node = pdev->dev.of_node;

	rdev = devm_regulator_register(dev, desc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "%s: failed to register regulator(%d)\n",
			__func__, ret);
		return ret;
	}
	platform_set_drvdata(pdev, rdev);

	/* Enable the ldo if not already done by bootloader */
	ti_abb_rmw(abb->regs->sr2_en_mask, 1, abb->setup_reg);

	return 0;
}

MODULE_ALIAS("platform:ti_abb");

static struct platform_driver ti_abb_driver = {
	.probe = ti_abb_probe,
	.driver = {
		   .name = "ti_abb",
		   .of_match_table = of_match_ptr(ti_abb_of_match),
		   },
};
module_platform_driver(ti_abb_driver);

MODULE_DESCRIPTION("Texas Instruments ABB LDO regulator driver");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_LICENSE("GPL v2");
