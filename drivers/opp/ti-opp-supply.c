// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2017 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon <nm@ti.com>
 *	Dave Gerlach <d-gerlach@ti.com>
 *
 * TI OPP supply driver that provides override into the regulator control
 * for generic opp core to handle devices with ABB regulator and/or
 * SmartReflex Class0.
 */
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/**
 * struct ti_opp_supply_optimum_voltage_table - optimized voltage table
 * @reference_uv:	reference voltage (usually Nominal voltage)
 * @optimized_uv:	Optimized voltage from efuse
 */
struct ti_opp_supply_optimum_voltage_table {
	unsigned int reference_uv;
	unsigned int optimized_uv;
};

/**
 * struct ti_opp_supply_data - OMAP specific opp supply data
 * @vdd_table:	Optimized voltage mapping table
 * @num_vdd_table: number of entries in vdd_table
 * @vdd_absolute_max_voltage_uv: absolute maximum voltage in UV for the supply
 */
struct ti_opp_supply_data {
	struct ti_opp_supply_optimum_voltage_table *vdd_table;
	u32 num_vdd_table;
	u32 vdd_absolute_max_voltage_uv;
};

static struct ti_opp_supply_data opp_data;

/**
 * struct ti_opp_supply_of_data - device tree match data
 * @flags:	specific type of opp supply
 * @efuse_voltage_mask: mask required for efuse register representing voltage
 * @efuse_voltage_uv: Are the efuse entries in micro-volts? if not, assume
 *		milli-volts.
 */
struct ti_opp_supply_of_data {
#define OPPDM_EFUSE_CLASS0_OPTIMIZED_VOLTAGE	BIT(1)
#define OPPDM_HAS_NO_ABB			BIT(2)
	const u8 flags;
	const u32 efuse_voltage_mask;
	const bool efuse_voltage_uv;
};

/**
 * _store_optimized_voltages() - store optimized voltages
 * @dev:	ti opp supply device for which we need to store info
 * @data:	data specific to the device
 *
 * Picks up efuse based optimized voltages for VDD unique per device and
 * stores it in internal data structure for use during transition requests.
 *
 * Return: If successful, 0, else appropriate error value.
 */
static int _store_optimized_voltages(struct device *dev,
				     struct ti_opp_supply_data *data)
{
	void __iomem *base;
	struct property *prop;
	struct resource *res;
	const __be32 *val;
	int proplen, i;
	int ret = 0;
	struct ti_opp_supply_optimum_voltage_table *table;
	const struct ti_opp_supply_of_data *of_data = dev_get_drvdata(dev);

	/* pick up Efuse based voltages */
	res = platform_get_resource(to_platform_device(dev), IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Unable to get IO resource\n");
		ret = -ENODEV;
		goto out_map;
	}

	base = ioremap_nocache(res->start, resource_size(res));
	if (!base) {
		dev_err(dev, "Unable to map Efuse registers\n");
		ret = -ENOMEM;
		goto out_map;
	}

	/* Fetch efuse-settings. */
	prop = of_find_property(dev->of_node, "ti,efuse-settings", NULL);
	if (!prop) {
		dev_err(dev, "No 'ti,efuse-settings' property found\n");
		ret = -EINVAL;
		goto out;
	}

	proplen = prop->length / sizeof(int);
	data->num_vdd_table = proplen / 2;
	/* Verify for corrupted OPP entries in dt */
	if (data->num_vdd_table * 2 * sizeof(int) != prop->length) {
		dev_err(dev, "Invalid 'ti,efuse-settings'\n");
		ret = -EINVAL;
		goto out;
	}

	ret = of_property_read_u32(dev->of_node, "ti,absolute-max-voltage-uv",
				   &data->vdd_absolute_max_voltage_uv);
	if (ret) {
		dev_err(dev, "ti,absolute-max-voltage-uv is missing\n");
		ret = -EINVAL;
		goto out;
	}

	table = kcalloc(data->num_vdd_table, sizeof(*data->vdd_table),
			GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto out;
	}
	data->vdd_table = table;

	val = prop->value;
	for (i = 0; i < data->num_vdd_table; i++, table++) {
		u32 efuse_offset;
		u32 tmp;

		table->reference_uv = be32_to_cpup(val++);
		efuse_offset = be32_to_cpup(val++);

		tmp = readl(base + efuse_offset);
		tmp &= of_data->efuse_voltage_mask;
		tmp >>= __ffs(of_data->efuse_voltage_mask);

		table->optimized_uv = of_data->efuse_voltage_uv ? tmp :
					tmp * 1000;

		dev_dbg(dev, "[%d] efuse=0x%08x volt_table=%d vset=%d\n",
			i, efuse_offset, table->reference_uv,
			table->optimized_uv);

		/*
		 * Some older samples might not have optimized efuse
		 * Use reference voltage for those - just add debug message
		 * for them.
		 */
		if (!table->optimized_uv) {
			dev_dbg(dev, "[%d] efuse=0x%08x volt_table=%d:vset0\n",
				i, efuse_offset, table->reference_uv);
			table->optimized_uv = table->reference_uv;
		}
	}
out:
	iounmap(base);
out_map:
	return ret;
}

/**
 * _free_optimized_voltages() - free resources for optvoltages
 * @dev:	device for which we need to free info
 * @data:	data specific to the device
 */
static void _free_optimized_voltages(struct device *dev,
				     struct ti_opp_supply_data *data)
{
	kfree(data->vdd_table);
	data->vdd_table = NULL;
	data->num_vdd_table = 0;
}

/**
 * _get_optimal_vdd_voltage() - Finds optimal voltage for the supply
 * @dev:	device for which we need to find info
 * @data:	data specific to the device
 * @reference_uv:	reference voltage (OPP voltage) for which we need value
 *
 * Return: if a match is found, return optimized voltage, else return
 * reference_uv, also return reference_uv if no optimization is needed.
 */
static int _get_optimal_vdd_voltage(struct device *dev,
				    struct ti_opp_supply_data *data,
				    int reference_uv)
{
	int i;
	struct ti_opp_supply_optimum_voltage_table *table;

	if (!data->num_vdd_table)
		return reference_uv;

	table = data->vdd_table;
	if (!table)
		return -EINVAL;

	/* Find a exact match - this list is usually very small */
	for (i = 0; i < data->num_vdd_table; i++, table++)
		if (table->reference_uv == reference_uv)
			return table->optimized_uv;

	/* IF things are screwed up, we'd make a mess on console.. ratelimit */
	dev_err_ratelimited(dev, "%s: Failed optimized voltage match for %d\n",
			    __func__, reference_uv);
	return reference_uv;
}

static int _opp_set_voltage(struct device *dev,
			    struct dev_pm_opp_supply *supply,
			    int new_target_uv, struct regulator *reg,
			    char *reg_name)
{
	int ret;
	unsigned long vdd_uv, uv_max;

	if (new_target_uv)
		vdd_uv = new_target_uv;
	else
		vdd_uv = supply->u_volt;

	/*
	 * If we do have an absolute max voltage specified, then we should
	 * use that voltage instead to allow for cases where the voltage rails
	 * are ganged (example if we set the max for an opp as 1.12v, and
	 * the absolute max is 1.5v, for another rail to get 1.25v, it cannot
	 * be achieved if the regulator is constrainted to max of 1.12v, even
	 * if it can function at 1.25v
	 */
	if (opp_data.vdd_absolute_max_voltage_uv)
		uv_max = opp_data.vdd_absolute_max_voltage_uv;
	else
		uv_max = supply->u_volt_max;

	if (vdd_uv > uv_max ||
	    vdd_uv < supply->u_volt_min ||
	    supply->u_volt_min > uv_max) {
		dev_warn(dev,
			 "Invalid range voltages [Min:%lu target:%lu Max:%lu]\n",
			 supply->u_volt_min, vdd_uv, uv_max);
		return -EINVAL;
	}

	dev_dbg(dev, "%s scaling to %luuV[min %luuV max %luuV]\n", reg_name,
		vdd_uv, supply->u_volt_min,
		uv_max);

	ret = regulator_set_voltage_triplet(reg,
					    supply->u_volt_min,
					    vdd_uv,
					    uv_max);
	if (ret) {
		dev_err(dev, "%s failed for %luuV[min %luuV max %luuV]\n",
			reg_name, vdd_uv, supply->u_volt_min,
			uv_max);
		return ret;
	}

	return 0;
}

/**
 * ti_opp_supply_set_opp() - do the opp supply transition
 * @data:	information on regulators and new and old opps provided by
 *		opp core to use in transition
 *
 * Return: If successful, 0, else appropriate error value.
 */
static int ti_opp_supply_set_opp(struct dev_pm_set_opp_data *data)
{
	struct dev_pm_opp_supply *old_supply_vdd = &data->old_opp.supplies[0];
	struct dev_pm_opp_supply *old_supply_vbb = &data->old_opp.supplies[1];
	struct dev_pm_opp_supply *new_supply_vdd = &data->new_opp.supplies[0];
	struct dev_pm_opp_supply *new_supply_vbb = &data->new_opp.supplies[1];
	struct device *dev = data->dev;
	unsigned long old_freq = data->old_opp.rate, freq = data->new_opp.rate;
	struct clk *clk = data->clk;
	struct regulator *vdd_reg = data->regulators[0];
	struct regulator *vbb_reg = data->regulators[1];
	int vdd_uv;
	int ret;

	vdd_uv = _get_optimal_vdd_voltage(dev, &opp_data,
					  new_supply_vbb->u_volt);

	/* Scaling up? Scale voltage before frequency */
	if (freq > old_freq) {
		ret = _opp_set_voltage(dev, new_supply_vdd, vdd_uv, vdd_reg,
				       "vdd");
		if (ret)
			goto restore_voltage;

		ret = _opp_set_voltage(dev, new_supply_vbb, 0, vbb_reg, "vbb");
		if (ret)
			goto restore_voltage;
	}

	/* Change frequency */
	dev_dbg(dev, "%s: switching OPP: %lu Hz --> %lu Hz\n",
		__func__, old_freq, freq);

	ret = clk_set_rate(clk, freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clock rate: %d\n", __func__,
			ret);
		goto restore_voltage;
	}

	/* Scaling down? Scale voltage after frequency */
	if (freq < old_freq) {
		ret = _opp_set_voltage(dev, new_supply_vbb, 0, vbb_reg, "vbb");
		if (ret)
			goto restore_freq;

		ret = _opp_set_voltage(dev, new_supply_vdd, vdd_uv, vdd_reg,
				       "vdd");
		if (ret)
			goto restore_freq;
	}

	return 0;

restore_freq:
	ret = clk_set_rate(clk, old_freq);
	if (ret)
		dev_err(dev, "%s: failed to restore old-freq (%lu Hz)\n",
			__func__, old_freq);
restore_voltage:
	/* This shouldn't harm even if the voltages weren't updated earlier */
	if (old_supply_vdd->u_volt) {
		ret = _opp_set_voltage(dev, old_supply_vbb, 0, vbb_reg, "vbb");
		if (ret)
			return ret;

		ret = _opp_set_voltage(dev, old_supply_vdd, 0, vdd_reg,
				       "vdd");
		if (ret)
			return ret;
	}

	return ret;
}

static const struct ti_opp_supply_of_data omap_generic_of_data = {
};

static const struct ti_opp_supply_of_data omap_omap5_of_data = {
	.flags = OPPDM_EFUSE_CLASS0_OPTIMIZED_VOLTAGE,
	.efuse_voltage_mask = 0xFFF,
	.efuse_voltage_uv = false,
};

static const struct ti_opp_supply_of_data omap_omap5core_of_data = {
	.flags = OPPDM_EFUSE_CLASS0_OPTIMIZED_VOLTAGE | OPPDM_HAS_NO_ABB,
	.efuse_voltage_mask = 0xFFF,
	.efuse_voltage_uv = false,
};

static const struct of_device_id ti_opp_supply_of_match[] = {
	{.compatible = "ti,omap-opp-supply", .data = &omap_generic_of_data},
	{.compatible = "ti,omap5-opp-supply", .data = &omap_omap5_of_data},
	{.compatible = "ti,omap5-core-opp-supply",
	 .data = &omap_omap5core_of_data},
	{},
};
MODULE_DEVICE_TABLE(of, ti_opp_supply_of_match);

static int ti_opp_supply_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *cpu_dev = get_cpu_device(0);
	const struct of_device_id *match;
	const struct ti_opp_supply_of_data *of_data;
	int ret = 0;

	match = of_match_device(ti_opp_supply_of_match, dev);
	if (!match) {
		/* We do not expect this to happen */
		dev_err(dev, "%s: Unable to match device\n", __func__);
		return -ENODEV;
	}
	if (!match->data) {
		/* Again, unlikely.. but mistakes do happen */
		dev_err(dev, "%s: Bad data in match\n", __func__);
		return -EINVAL;
	}
	of_data = match->data;

	dev_set_drvdata(dev, (void *)of_data);

	/* If we need optimized voltage */
	if (of_data->flags & OPPDM_EFUSE_CLASS0_OPTIMIZED_VOLTAGE) {
		ret = _store_optimized_voltages(dev, &opp_data);
		if (ret)
			return ret;
	}

	ret = PTR_ERR_OR_ZERO(dev_pm_opp_register_set_opp_helper(cpu_dev,
								 ti_opp_supply_set_opp));
	if (ret)
		_free_optimized_voltages(dev, &opp_data);

	return ret;
}

static struct platform_driver ti_opp_supply_driver = {
	.probe = ti_opp_supply_probe,
	.driver = {
		   .name = "ti_opp_supply",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(ti_opp_supply_of_match),
		   },
};
module_platform_driver(ti_opp_supply_driver);

MODULE_DESCRIPTION("Texas Instruments OMAP OPP Supply driver");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_LICENSE("GPL v2");
