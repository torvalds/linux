// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ROHM Semiconductors
// ROHM BD9576MUF/BD9573MUF regulator driver

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd957x.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define BD957X_VOUTS1_VOLT	3300000
#define BD957X_VOUTS4_BASE_VOLT	1030000
#define BD957X_VOUTS34_NUM_VOLT	32

#define BD9576_THERM_IRQ_MASK_TW	BIT(5)
#define BD9576_xVD_IRQ_MASK_VOUTL1	BIT(5)
#define BD9576_UVD_IRQ_MASK_VOUTS1_OCW	BIT(6)
#define BD9576_xVD_IRQ_MASK_VOUT1TO4	0x0F

static const unsigned int vout1_volt_table[] = {
	5000000, 4900000, 4800000, 4700000, 4600000,
	4500000, 4500000, 4500000, 5000000, 5100000,
	5200000, 5300000, 5400000, 5500000, 5500000,
	5500000
};

static const unsigned int vout2_volt_table[] = {
	1800000, 1780000, 1760000, 1740000, 1720000,
	1700000, 1680000, 1660000, 1800000, 1820000,
	1840000, 1860000, 1880000, 1900000, 1920000,
	1940000
};

static const unsigned int voutl1_volt_table[] = {
	2500000, 2540000, 2580000, 2620000, 2660000,
	2700000, 2740000, 2780000, 2500000, 2460000,
	2420000, 2380000, 2340000, 2300000, 2260000,
	2220000
};

static const struct linear_range vout1_xvd_ranges[] = {
	REGULATOR_LINEAR_RANGE(225000, 0x01, 0x2b, 0),
	REGULATOR_LINEAR_RANGE(225000, 0x2c, 0x54, 5000),
	REGULATOR_LINEAR_RANGE(425000, 0x55, 0x7f, 0),
};

static const struct linear_range vout234_xvd_ranges[] = {
	REGULATOR_LINEAR_RANGE(17000, 0x01, 0x0f, 0),
	REGULATOR_LINEAR_RANGE(17000, 0x10, 0x6d, 1000),
	REGULATOR_LINEAR_RANGE(110000, 0x6e, 0x7f, 0),
};

static const struct linear_range voutL1_xvd_ranges[] = {
	REGULATOR_LINEAR_RANGE(34000, 0x01, 0x0f, 0),
	REGULATOR_LINEAR_RANGE(34000, 0x10, 0x6d, 2000),
	REGULATOR_LINEAR_RANGE(220000, 0x6e, 0x7f, 0),
};

static struct linear_range voutS1_ocw_ranges_internal[] = {
	REGULATOR_LINEAR_RANGE(200000, 0x01, 0x04, 0),
	REGULATOR_LINEAR_RANGE(250000, 0x05, 0x18, 50000),
	REGULATOR_LINEAR_RANGE(1200000, 0x19, 0x3f, 0),
};

static struct linear_range voutS1_ocw_ranges[] = {
	REGULATOR_LINEAR_RANGE(50000, 0x01, 0x04, 0),
	REGULATOR_LINEAR_RANGE(60000, 0x05, 0x18, 10000),
	REGULATOR_LINEAR_RANGE(250000, 0x19, 0x3f, 0),
};

static struct linear_range voutS1_ocp_ranges_internal[] = {
	REGULATOR_LINEAR_RANGE(300000, 0x01, 0x06, 0),
	REGULATOR_LINEAR_RANGE(350000, 0x7, 0x1b, 50000),
	REGULATOR_LINEAR_RANGE(1350000, 0x1c, 0x3f, 0),
};

static struct linear_range voutS1_ocp_ranges[] = {
	REGULATOR_LINEAR_RANGE(70000, 0x01, 0x06, 0),
	REGULATOR_LINEAR_RANGE(80000, 0x7, 0x1b, 10000),
	REGULATOR_LINEAR_RANGE(280000, 0x1c, 0x3f, 0),
};

struct bd957x_regulator_data {
	struct regulator_desc desc;
	int base_voltage;
	struct regulator_dev *rdev;
	int ovd_notif;
	int uvd_notif;
	int temp_notif;
	int ovd_err;
	int uvd_err;
	int temp_err;
	const struct linear_range *xvd_ranges;
	int num_xvd_ranges;
	bool oc_supported;
	unsigned int ovd_reg;
	unsigned int uvd_reg;
	unsigned int xvd_mask;
	unsigned int ocp_reg;
	unsigned int ocp_mask;
	unsigned int ocw_reg;
	unsigned int ocw_mask;
	unsigned int ocw_rfet;
};

#define BD9576_NUM_REGULATORS 6
#define BD9576_NUM_OVD_REGULATORS 5

struct bd957x_data {
	struct bd957x_regulator_data regulator_data[BD9576_NUM_REGULATORS];
	struct regmap *regmap;
	struct delayed_work therm_irq_suppress;
	struct delayed_work ovd_irq_suppress;
	struct delayed_work uvd_irq_suppress;
	unsigned int therm_irq;
	unsigned int ovd_irq;
	unsigned int uvd_irq;
	spinlock_t err_lock;
	int regulator_global_err;
};

static int bd957x_vout34_list_voltage(struct regulator_dev *rdev,
				      unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	int multiplier = selector & desc->vsel_mask & 0x7f;
	int tune;

	/* VOUT3 and 4 has 10mV step */
	tune = multiplier * 10000;

	if (!(selector & 0x80))
		return desc->fixed_uV - tune;

	return desc->fixed_uV + tune;
}

static int bd957x_list_voltage(struct regulator_dev *rdev,
			       unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	int index = selector & desc->vsel_mask & 0x7f;

	if (!(selector & 0x80))
		index += desc->n_voltages/2;

	if (index >= desc->n_voltages)
		return -EINVAL;

	return desc->volt_table[index];
}

static void bd9576_fill_ovd_flags(struct bd957x_regulator_data *data,
				  bool warn)
{
	if (warn) {
		data->ovd_notif = REGULATOR_EVENT_OVER_VOLTAGE_WARN;
		data->ovd_err = REGULATOR_ERROR_OVER_VOLTAGE_WARN;
	} else {
		data->ovd_notif = REGULATOR_EVENT_REGULATION_OUT;
		data->ovd_err = REGULATOR_ERROR_REGULATION_OUT;
	}
}

static void bd9576_fill_ocp_flags(struct bd957x_regulator_data *data,
				  bool warn)
{
	if (warn) {
		data->uvd_notif = REGULATOR_EVENT_OVER_CURRENT_WARN;
		data->uvd_err = REGULATOR_ERROR_OVER_CURRENT_WARN;
	} else {
		data->uvd_notif = REGULATOR_EVENT_OVER_CURRENT;
		data->uvd_err = REGULATOR_ERROR_OVER_CURRENT;
	}
}

static void bd9576_fill_uvd_flags(struct bd957x_regulator_data *data,
				  bool warn)
{
	if (warn) {
		data->uvd_notif = REGULATOR_EVENT_UNDER_VOLTAGE_WARN;
		data->uvd_err = REGULATOR_ERROR_UNDER_VOLTAGE_WARN;
	} else {
		data->uvd_notif = REGULATOR_EVENT_UNDER_VOLTAGE;
		data->uvd_err = REGULATOR_ERROR_UNDER_VOLTAGE;
	}
}

static void bd9576_fill_temp_flags(struct bd957x_regulator_data *data,
				   bool enable, bool warn)
{
	if (!enable) {
		data->temp_notif = 0;
		data->temp_err = 0;
	} else if (warn) {
		data->temp_notif = REGULATOR_EVENT_OVER_TEMP_WARN;
		data->temp_err = REGULATOR_ERROR_OVER_TEMP_WARN;
	} else {
		data->temp_notif = REGULATOR_EVENT_OVER_TEMP;
		data->temp_err = REGULATOR_ERROR_OVER_TEMP;
	}
}

static int bd9576_set_limit(const struct linear_range *r, int num_ranges,
			    struct regmap *regmap, int reg, int mask, int lim)
{
	int ret;
	bool found;
	int sel = 0;

	if (lim) {

		ret = linear_range_get_selector_low_array(r, num_ranges,
							  lim, &sel, &found);
		if (ret)
			return ret;

		if (!found)
			dev_warn(regmap_get_device(regmap),
				 "limit %d out of range. Setting lower\n",
				 lim);
	}

	return regmap_update_bits(regmap, reg, mask, sel);
}

static bool check_ocp_flag_mismatch(struct regulator_dev *rdev, int severity,
				    struct bd957x_regulator_data *r)
{
	if ((severity == REGULATOR_SEVERITY_ERR &&
	    r->uvd_notif != REGULATOR_EVENT_OVER_CURRENT) ||
	    (severity == REGULATOR_SEVERITY_WARN &&
	    r->uvd_notif != REGULATOR_EVENT_OVER_CURRENT_WARN)) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't support both OCP WARN and ERR\n");
		/* Do not overwrite ERR config with WARN */
		if (severity == REGULATOR_SEVERITY_WARN)
			return true;

		bd9576_fill_ocp_flags(r, 0);
	}

	return false;
}

static bool check_uvd_flag_mismatch(struct regulator_dev *rdev, int severity,
				    struct bd957x_regulator_data *r)
{
	if ((severity == REGULATOR_SEVERITY_ERR &&
	     r->uvd_notif != REGULATOR_EVENT_UNDER_VOLTAGE) ||
	     (severity == REGULATOR_SEVERITY_WARN &&
	     r->uvd_notif != REGULATOR_EVENT_UNDER_VOLTAGE_WARN)) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't support both UVD WARN and ERR\n");
		if (severity == REGULATOR_SEVERITY_WARN)
			return true;

		bd9576_fill_uvd_flags(r, 0);
	}

	return false;
}

static bool check_ovd_flag_mismatch(struct regulator_dev *rdev, int severity,
				    struct bd957x_regulator_data *r)
{
	if ((severity == REGULATOR_SEVERITY_ERR &&
	     r->ovd_notif != REGULATOR_EVENT_REGULATION_OUT) ||
	     (severity == REGULATOR_SEVERITY_WARN &&
	     r->ovd_notif != REGULATOR_EVENT_OVER_VOLTAGE_WARN)) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't support both OVD WARN and ERR\n");
		if (severity == REGULATOR_SEVERITY_WARN)
			return true;

		bd9576_fill_ovd_flags(r, 0);
	}

	return false;
}

static bool check_temp_flag_mismatch(struct regulator_dev *rdev, int severity,
				    struct bd957x_regulator_data *r)
{
	if ((severity == REGULATOR_SEVERITY_ERR &&
	     r->temp_notif != REGULATOR_EVENT_OVER_TEMP) ||
	     (severity == REGULATOR_SEVERITY_WARN &&
	     r->temp_notif != REGULATOR_EVENT_OVER_TEMP_WARN)) {
		dev_warn(rdev_get_dev(rdev),
			 "Can't support both thermal WARN and ERR\n");
		if (severity == REGULATOR_SEVERITY_WARN)
			return true;
	}

	return false;
}

static int bd9576_set_ocp(struct regulator_dev *rdev, int lim_uA, int severity,
			  bool enable)
{
	struct bd957x_data *d;
	struct bd957x_regulator_data *r;
	int reg, mask;
	int Vfet, rfet;
	const struct linear_range *range;
	int num_ranges;

	if ((lim_uA && !enable) || (!lim_uA && enable))
		return -EINVAL;

	r = container_of(rdev->desc, struct bd957x_regulator_data, desc);
	if (!r->oc_supported)
		return -EINVAL;

	d = rdev_get_drvdata(rdev);

	if (severity == REGULATOR_SEVERITY_PROT) {
		reg = r->ocp_reg;
		mask = r->ocp_mask;
		if (r->ocw_rfet) {
			range = voutS1_ocp_ranges;
			num_ranges = ARRAY_SIZE(voutS1_ocp_ranges);
			rfet = r->ocw_rfet / 1000;
		} else {
			range = voutS1_ocp_ranges_internal;
			num_ranges = ARRAY_SIZE(voutS1_ocp_ranges_internal);
			/* Internal values are already micro-amperes */
			rfet = 1000;
		}
	} else {
		reg = r->ocw_reg;
		mask = r->ocw_mask;

		if (r->ocw_rfet) {
			range = voutS1_ocw_ranges;
			num_ranges = ARRAY_SIZE(voutS1_ocw_ranges);
			rfet = r->ocw_rfet / 1000;
		} else {
			range = voutS1_ocw_ranges_internal;
			num_ranges = ARRAY_SIZE(voutS1_ocw_ranges_internal);
			/* Internal values are already micro-amperes */
			rfet = 1000;
		}

		/* We abuse uvd fields for OCW on VoutS1 */
		if (r->uvd_notif) {
			/*
			 * If both warning and error are requested, prioritize
			 * ERROR configuration
			 */
			if (check_ocp_flag_mismatch(rdev, severity, r))
				return 0;
		} else {
			bool warn = severity == REGULATOR_SEVERITY_WARN;

			bd9576_fill_ocp_flags(r, warn);
		}
	}

	/*
	 * limits are given in uA, rfet is mOhm
	 * Divide lim_uA by 1000 to get Vfet in uV.
	 * (We expect both Rfet and limit uA to be magnitude of hundreds of
	 * milli Amperes & milli Ohms => we should still have decent accuracy)
	 */
	Vfet = lim_uA/1000 * rfet;

	return bd9576_set_limit(range, num_ranges, d->regmap,
				reg, mask, Vfet);
}

static int bd9576_set_uvp(struct regulator_dev *rdev, int lim_uV, int severity,
			  bool enable)
{
	struct bd957x_data *d;
	struct bd957x_regulator_data *r;
	int mask, reg;

	if (severity == REGULATOR_SEVERITY_PROT) {
		if (!enable || lim_uV)
			return -EINVAL;
		return 0;
	}

	/*
	 * BD9576 has enable control as a special value in limit reg. Can't
	 * set limit but keep feature disabled or enable W/O given limit.
	 */
	if ((lim_uV && !enable) || (!lim_uV && enable))
		return -EINVAL;

	r = container_of(rdev->desc, struct bd957x_regulator_data, desc);
	d = rdev_get_drvdata(rdev);

	mask = r->xvd_mask;
	reg = r->uvd_reg;
	/*
	 * Check that there is no mismatch for what the detection IRQs are to
	 * be used.
	 */
	if (r->uvd_notif) {
		if (check_uvd_flag_mismatch(rdev, severity, r))
			return 0;
	} else {
		bd9576_fill_uvd_flags(r, severity == REGULATOR_SEVERITY_WARN);
	}

	return bd9576_set_limit(r->xvd_ranges, r->num_xvd_ranges, d->regmap,
				reg, mask, lim_uV);
}

static int bd9576_set_ovp(struct regulator_dev *rdev, int lim_uV, int severity,
			  bool enable)
{
	struct bd957x_data *d;
	struct bd957x_regulator_data *r;
	int mask, reg;

	if (severity == REGULATOR_SEVERITY_PROT) {
		if (!enable || lim_uV)
			return -EINVAL;
		return 0;
	}

	/*
	 * BD9576 has enable control as a special value in limit reg. Can't
	 * set limit but keep feature disabled or enable W/O given limit.
	 */
	if ((lim_uV && !enable) || (!lim_uV && enable))
		return -EINVAL;

	r = container_of(rdev->desc, struct bd957x_regulator_data, desc);
	d = rdev_get_drvdata(rdev);

	mask = r->xvd_mask;
	reg = r->ovd_reg;
	/*
	 * Check that there is no mismatch for what the detection IRQs are to
	 * be used.
	 */
	if (r->ovd_notif) {
		if (check_ovd_flag_mismatch(rdev, severity, r))
			return 0;
	} else {
		bd9576_fill_ovd_flags(r, severity == REGULATOR_SEVERITY_WARN);
	}

	return bd9576_set_limit(r->xvd_ranges, r->num_xvd_ranges, d->regmap,
				reg, mask, lim_uV);
}


static int bd9576_set_tw(struct regulator_dev *rdev, int lim, int severity,
			  bool enable)
{
	struct bd957x_data *d;
	struct bd957x_regulator_data *r;
	int i;

	/*
	 * BD9576MUF has fixed temperature limits
	 * The detection can only be enabled/disabled
	 */
	if (lim)
		return -EINVAL;

	/* Protection can't be disabled */
	if (severity == REGULATOR_SEVERITY_PROT) {
		if (!enable)
			return -EINVAL;
		else
			return 0;
	}

	r = container_of(rdev->desc, struct bd957x_regulator_data, desc);
	d = rdev_get_drvdata(rdev);

	/*
	 * Check that there is no mismatch for what the detection IRQs are to
	 * be used.
	 */
	if (r->temp_notif)
		if (check_temp_flag_mismatch(rdev, severity, r))
			return 0;

	bd9576_fill_temp_flags(r, enable, severity == REGULATOR_SEVERITY_WARN);

	if (enable)
		return regmap_update_bits(d->regmap, BD957X_REG_INT_THERM_MASK,
					 BD9576_THERM_IRQ_MASK_TW, 0);

	/*
	 * If any of the regulators is interested in thermal warning we keep IRQ
	 * enabled.
	 */
	for (i = 0; i < BD9576_NUM_REGULATORS; i++)
		if (d->regulator_data[i].temp_notif)
			return 0;

	return regmap_update_bits(d->regmap, BD957X_REG_INT_THERM_MASK,
				  BD9576_THERM_IRQ_MASK_TW,
				  BD9576_THERM_IRQ_MASK_TW);
}

static const struct regulator_ops bd9573_vout34_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_vout34_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd9576_vout34_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_vout34_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_over_voltage_protection = bd9576_set_ovp,
	.set_under_voltage_protection = bd9576_set_uvp,
	.set_thermal_protection = bd9576_set_tw,
};

static const struct regulator_ops bd9573_vouts1_regulator_ops = {
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops bd9576_vouts1_regulator_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.set_over_current_protection = bd9576_set_ocp,
};

static const struct regulator_ops bd9573_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd9576_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd957x_list_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_over_voltage_protection = bd9576_set_ovp,
	.set_under_voltage_protection = bd9576_set_uvp,
	.set_thermal_protection = bd9576_set_tw,
};

static const struct regulator_ops  *bd9573_ops_arr[] = {
	[BD957X_VD50]	= &bd9573_ops,
	[BD957X_VD18]	= &bd9573_ops,
	[BD957X_VDDDR]	= &bd9573_vout34_ops,
	[BD957X_VD10]	= &bd9573_vout34_ops,
	[BD957X_VOUTL1]	= &bd9573_ops,
	[BD957X_VOUTS1]	= &bd9573_vouts1_regulator_ops,
};

static const struct regulator_ops  *bd9576_ops_arr[] = {
	[BD957X_VD50]	= &bd9576_ops,
	[BD957X_VD18]	= &bd9576_ops,
	[BD957X_VDDDR]	= &bd9576_vout34_ops,
	[BD957X_VD10]	= &bd9576_vout34_ops,
	[BD957X_VOUTL1]	= &bd9576_ops,
	[BD957X_VOUTS1]	= &bd9576_vouts1_regulator_ops,
};

static int vouts1_get_fet_res(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *cfg)
{
	struct bd957x_regulator_data *data;
	int ret;
	u32 uohms;

	data = container_of(desc, struct bd957x_regulator_data, desc);

	ret = of_property_read_u32(np, "rohm,ocw-fet-ron-micro-ohms", &uohms);
	if (ret) {
		if (ret != -EINVAL)
			return ret;

		return 0;
	}
	data->ocw_rfet = uohms;
	return 0;
}

static struct bd957x_data bd957x_regulators = {
	.regulator_data = {
		{
			.desc = {
				.name = "VD50",
				.of_match = of_match_ptr("regulator-vd50"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VD50,
				.type = REGULATOR_VOLTAGE,
				.volt_table = &vout1_volt_table[0],
				.n_voltages = ARRAY_SIZE(vout1_volt_table),
				.vsel_reg = BD957X_REG_VOUT1_TUNE,
				.vsel_mask = BD957X_MASK_VOUT1_TUNE,
				.enable_reg = BD957X_REG_POW_TRIGGER1,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
			},
			.xvd_ranges = vout1_xvd_ranges,
			.num_xvd_ranges = ARRAY_SIZE(vout1_xvd_ranges),
			.ovd_reg = BD9576_REG_VOUT1_OVD,
			.uvd_reg = BD9576_REG_VOUT1_UVD,
			.xvd_mask = BD9576_MASK_XVD,
		},
		{
			.desc = {
				.name = "VD18",
				.of_match = of_match_ptr("regulator-vd18"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VD18,
				.type = REGULATOR_VOLTAGE,
				.volt_table = &vout2_volt_table[0],
				.n_voltages = ARRAY_SIZE(vout2_volt_table),
				.vsel_reg = BD957X_REG_VOUT2_TUNE,
				.vsel_mask = BD957X_MASK_VOUT2_TUNE,
				.enable_reg = BD957X_REG_POW_TRIGGER2,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
			},
			.xvd_ranges = vout234_xvd_ranges,
			.num_xvd_ranges = ARRAY_SIZE(vout234_xvd_ranges),
			.ovd_reg = BD9576_REG_VOUT2_OVD,
			.uvd_reg = BD9576_REG_VOUT2_UVD,
			.xvd_mask = BD9576_MASK_XVD,
		},
		{
			.desc = {
				.name = "VDDDR",
				.of_match = of_match_ptr("regulator-vdddr"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VDDDR,
				.type = REGULATOR_VOLTAGE,
				.n_voltages = BD957X_VOUTS34_NUM_VOLT,
				.vsel_reg = BD957X_REG_VOUT3_TUNE,
				.vsel_mask = BD957X_MASK_VOUT3_TUNE,
				.enable_reg = BD957X_REG_POW_TRIGGER3,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
			},
			.ovd_reg = BD9576_REG_VOUT3_OVD,
			.uvd_reg = BD9576_REG_VOUT3_UVD,
			.xvd_mask = BD9576_MASK_XVD,
			.xvd_ranges = vout234_xvd_ranges,
			.num_xvd_ranges = ARRAY_SIZE(vout234_xvd_ranges),
		},
		{
			.desc = {
				.name = "VD10",
				.of_match = of_match_ptr("regulator-vd10"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VD10,
				.type = REGULATOR_VOLTAGE,
				.fixed_uV = BD957X_VOUTS4_BASE_VOLT,
				.n_voltages = BD957X_VOUTS34_NUM_VOLT,
				.vsel_reg = BD957X_REG_VOUT4_TUNE,
				.vsel_mask = BD957X_MASK_VOUT4_TUNE,
				.enable_reg = BD957X_REG_POW_TRIGGER4,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
			},
			.xvd_ranges = vout234_xvd_ranges,
			.num_xvd_ranges = ARRAY_SIZE(vout234_xvd_ranges),
			.ovd_reg = BD9576_REG_VOUT4_OVD,
			.uvd_reg = BD9576_REG_VOUT4_UVD,
			.xvd_mask = BD9576_MASK_XVD,
		},
		{
			.desc = {
				.name = "VOUTL1",
				.of_match = of_match_ptr("regulator-voutl1"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VOUTL1,
				.type = REGULATOR_VOLTAGE,
				.volt_table = &voutl1_volt_table[0],
				.n_voltages = ARRAY_SIZE(voutl1_volt_table),
				.vsel_reg = BD957X_REG_VOUTL1_TUNE,
				.vsel_mask = BD957X_MASK_VOUTL1_TUNE,
				.enable_reg = BD957X_REG_POW_TRIGGERL1,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
			},
			.xvd_ranges = voutL1_xvd_ranges,
			.num_xvd_ranges = ARRAY_SIZE(voutL1_xvd_ranges),
			.ovd_reg = BD9576_REG_VOUTL1_OVD,
			.uvd_reg = BD9576_REG_VOUTL1_UVD,
			.xvd_mask = BD9576_MASK_XVD,
		},
		{
			.desc = {
				.name = "VOUTS1",
				.of_match = of_match_ptr("regulator-vouts1"),
				.regulators_node = of_match_ptr("regulators"),
				.id = BD957X_VOUTS1,
				.type = REGULATOR_VOLTAGE,
				.n_voltages = 1,
				.fixed_uV = BD957X_VOUTS1_VOLT,
				.enable_reg = BD957X_REG_POW_TRIGGERS1,
				.enable_mask = BD957X_REGULATOR_EN_MASK,
				.enable_val = BD957X_REGULATOR_DIS_VAL,
				.enable_is_inverted = true,
				.owner = THIS_MODULE,
				.of_parse_cb = vouts1_get_fet_res,
			},
			.oc_supported = true,
			.ocw_reg = BD9576_REG_VOUT1S_OCW,
			.ocw_mask = BD9576_MASK_VOUT1S_OCW,
			.ocp_reg = BD9576_REG_VOUT1S_OCP,
			.ocp_mask = BD9576_MASK_VOUT1S_OCP,
		},
	},
};

static int bd9576_renable(struct regulator_irq_data *rid, int reg, int mask)
{
	int val, ret;
	struct bd957x_data *d = (struct bd957x_data *)rid->data;

	ret = regmap_read(d->regmap, reg, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	if (rid->opaque && rid->opaque == (val & mask)) {
		/*
		 * It seems we stil have same status. Ack and return
		 * information that we are still out of limits and core
		 * should not enable IRQ
		 */
		regmap_write(d->regmap, reg, mask & val);
		return REGULATOR_ERROR_ON;
	}
	rid->opaque = 0;
	/*
	 * Status was changed. Either prolem was solved or we have new issues.
	 * Let's re-enable IRQs and be prepared to report problems again
	 */
	return REGULATOR_ERROR_CLEARED;
}

static int bd9576_uvd_renable(struct regulator_irq_data *rid)
{
	return bd9576_renable(rid, BD957X_REG_INT_UVD_STAT, UVD_IRQ_VALID_MASK);
}

static int bd9576_ovd_renable(struct regulator_irq_data *rid)
{
	return bd9576_renable(rid, BD957X_REG_INT_OVD_STAT, OVD_IRQ_VALID_MASK);
}

static int bd9576_temp_renable(struct regulator_irq_data *rid)
{
	return bd9576_renable(rid, BD957X_REG_INT_THERM_STAT,
			      BD9576_THERM_IRQ_MASK_TW);
}

static int bd9576_uvd_handler(int irq, struct regulator_irq_data *rid,
			      unsigned long *dev_mask)
{
	int val, ret, i;
	struct bd957x_data *d = (struct bd957x_data *)rid->data;

	ret = regmap_read(d->regmap, BD957X_REG_INT_UVD_STAT, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	*dev_mask = 0;

	rid->opaque = val & UVD_IRQ_VALID_MASK;

	/*
	 * Go through the set status bits and report either error or warning
	 * to the notifier depending on what was flagged in DT
	 */
	*dev_mask = val & BD9576_xVD_IRQ_MASK_VOUT1TO4;
	/* There is 1 bit gap in register after Vout1 .. Vout4 statuses */
	*dev_mask |= ((val & BD9576_xVD_IRQ_MASK_VOUTL1) >> 1);
	/*
	 * We (ab)use the uvd for OCW notification. DT parsing should
	 * have added correct OCW flag to uvd_notif and uvd_err for S1
	 */
	*dev_mask |= ((val & BD9576_UVD_IRQ_MASK_VOUTS1_OCW) >> 1);

	for_each_set_bit(i, dev_mask, 6) {
		struct bd957x_regulator_data *rdata;
		struct regulator_err_state *stat;

		rdata = &d->regulator_data[i];
		stat  = &rid->states[i];

		stat->notifs	= rdata->uvd_notif;
		stat->errors	= rdata->uvd_err;
	}

	ret = regmap_write(d->regmap, BD957X_REG_INT_UVD_STAT,
			   UVD_IRQ_VALID_MASK & val);

	return 0;
}

static int bd9576_ovd_handler(int irq, struct regulator_irq_data *rid,
			      unsigned long *dev_mask)
{
	int val, ret, i;
	struct bd957x_data *d = (struct bd957x_data *)rid->data;

	ret = regmap_read(d->regmap, BD957X_REG_INT_OVD_STAT, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	rid->opaque = val & OVD_IRQ_VALID_MASK;
	*dev_mask = 0;

	if (!(val & OVD_IRQ_VALID_MASK))
		return 0;

	*dev_mask = val & BD9576_xVD_IRQ_MASK_VOUT1TO4;
	/* There is 1 bit gap in register after Vout1 .. Vout4 statuses */
	*dev_mask |= ((val & BD9576_xVD_IRQ_MASK_VOUTL1) >> 1);

	for_each_set_bit(i, dev_mask, 5) {
		struct bd957x_regulator_data *rdata;
		struct regulator_err_state *stat;

		rdata = &d->regulator_data[i];
		stat  = &rid->states[i];

		stat->notifs	= rdata->ovd_notif;
		stat->errors	= rdata->ovd_err;
	}

	/* Clear the sub-IRQ status */
	regmap_write(d->regmap, BD957X_REG_INT_OVD_STAT,
		     OVD_IRQ_VALID_MASK & val);

	return 0;
}

#define BD9576_DEV_MASK_ALL_REGULATORS 0x3F

static int bd9576_thermal_handler(int irq, struct regulator_irq_data *rid,
				  unsigned long *dev_mask)
{
	int val, ret, i;
	struct bd957x_data *d = (struct bd957x_data *)rid->data;

	ret = regmap_read(d->regmap, BD957X_REG_INT_THERM_STAT, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	if (!(val & BD9576_THERM_IRQ_MASK_TW)) {
		*dev_mask = 0;
		return 0;
	}

	*dev_mask = BD9576_DEV_MASK_ALL_REGULATORS;

	for (i = 0; i < BD9576_NUM_REGULATORS; i++) {
		struct bd957x_regulator_data *rdata;
		struct regulator_err_state *stat;

		rdata = &d->regulator_data[i];
		stat  = &rid->states[i];

		stat->notifs	= rdata->temp_notif;
		stat->errors	= rdata->temp_err;
	}

	/* Clear the sub-IRQ status */
	regmap_write(d->regmap, BD957X_REG_INT_THERM_STAT,
		     BD9576_THERM_IRQ_MASK_TW);

	return 0;
}

static int bd957x_probe(struct platform_device *pdev)
{
	int i;
	unsigned int num_reg_data;
	bool vout_mode, ddr_sel, may_have_irqs = false;
	struct regmap *regmap;
	struct bd957x_data *ic_data;
	struct regulator_config config = { 0 };
	/* All regulators are related to UVD and thermal IRQs... */
	struct regulator_dev *rdevs[BD9576_NUM_REGULATORS];
	/* ...But VoutS1 is not flagged by OVD IRQ */
	struct regulator_dev *ovd_devs[BD9576_NUM_OVD_REGULATORS];
	static const struct regulator_irq_desc bd9576_notif_uvd = {
		.name = "bd9576-uvd",
		.irq_off_ms = 1000,
		.map_event = bd9576_uvd_handler,
		.renable = bd9576_uvd_renable,
		.data = &bd957x_regulators,
	};
	static const struct regulator_irq_desc bd9576_notif_ovd = {
		.name = "bd9576-ovd",
		.irq_off_ms = 1000,
		.map_event = bd9576_ovd_handler,
		.renable = bd9576_ovd_renable,
		.data = &bd957x_regulators,
	};
	static const struct regulator_irq_desc bd9576_notif_temp = {
		.name = "bd9576-temp",
		.irq_off_ms = 1000,
		.map_event = bd9576_thermal_handler,
		.renable = bd9576_temp_renable,
		.data = &bd957x_regulators,
	};
	enum rohm_chip_type chip = platform_get_device_id(pdev)->driver_data;

	num_reg_data = ARRAY_SIZE(bd957x_regulators.regulator_data);

	ic_data = &bd957x_regulators;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "No regmap\n");
		return -EINVAL;
	}

	ic_data->regmap = regmap;
	vout_mode = device_property_read_bool(pdev->dev.parent,
					      "rohm,vout1-en-low");
	if (vout_mode) {
		struct gpio_desc *en;

		dev_dbg(&pdev->dev, "GPIO controlled mode\n");

		/* VOUT1 enable state judged by VOUT1_EN pin */
		/* See if we have GPIO defined */
		en = devm_fwnode_gpiod_get(&pdev->dev,
					   dev_fwnode(pdev->dev.parent),
					   "rohm,vout1-en", GPIOD_OUT_LOW,
					   "vout1-en");
		if (!IS_ERR(en)) {
			/* VOUT1_OPS gpio ctrl */
			/*
			 * Regulator core prioritizes the ena_gpio over
			 * enable/disable/is_enabled callbacks so no need to
			 * clear them. We can still use same ops
			 */
			config.ena_gpiod = en;
		} else {
			/*
			 * In theory it is possible someone wants to set
			 * vout1-en LOW during OTP loading and set VOUT1 to be
			 * controlled by GPIO - but control the GPIO from some
			 * where else than this driver. For that to work we
			 * should unset the is_enabled callback here.
			 *
			 * I believe such case where rohm,vout1-en-low is set
			 * and vout1-en-gpios is not is likely to be a
			 * misconfiguration. So let's just err out for now.
			 */
			dev_err(&pdev->dev,
				"Failed to get VOUT1 control GPIO\n");
			return PTR_ERR(en);
		}
	}

	/*
	 * If more than one PMIC needs to be controlled by same processor then
	 * allocate the regulator data array here and use bd9576_regulators as
	 * template. At the moment I see no such use-case so I spare some
	 * bytes and use bd9576_regulators directly for non-constant configs
	 * like DDR voltage selection.
	 */
	platform_set_drvdata(pdev, ic_data);
	ddr_sel = device_property_read_bool(pdev->dev.parent,
					    "rohm,ddr-sel-low");
	if (ddr_sel)
		ic_data->regulator_data[2].desc.fixed_uV = 1350000;
	else
		ic_data->regulator_data[2].desc.fixed_uV = 1500000;

	switch (chip) {
	case ROHM_CHIP_TYPE_BD9576:
		may_have_irqs = true;
		dev_dbg(&pdev->dev, "Found BD9576MUF\n");
		break;
	case ROHM_CHIP_TYPE_BD9573:
		dev_dbg(&pdev->dev, "Found BD9573MUF\n");
		break;
	default:
		dev_err(&pdev->dev, "Unsupported chip type\n");
		return -EINVAL;
	}

	for (i = 0; i < num_reg_data; i++) {
		struct regulator_desc *d;

		d = &ic_data->regulator_data[i].desc;


		if (may_have_irqs) {
			if (d->id >= ARRAY_SIZE(bd9576_ops_arr))
				return -EINVAL;

			d->ops = bd9576_ops_arr[d->id];
		} else {
			if (d->id >= ARRAY_SIZE(bd9573_ops_arr))
				return -EINVAL;

			d->ops = bd9573_ops_arr[d->id];
		}
	}

	config.dev = pdev->dev.parent;
	config.regmap = regmap;
	config.driver_data = ic_data;

	for (i = 0; i < num_reg_data; i++) {

		struct bd957x_regulator_data *r = &ic_data->regulator_data[i];
		const struct regulator_desc *desc = &r->desc;

		r->rdev = devm_regulator_register(&pdev->dev, desc,
							   &config);
		if (IS_ERR(r->rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				desc->name);
			return PTR_ERR(r->rdev);
		}
		/*
		 * Clear the VOUT1 GPIO setting - rest of the regulators do not
		 * support GPIO control
		 */
		config.ena_gpiod = NULL;

		if (!may_have_irqs)
			continue;

		rdevs[i] = r->rdev;
		if (i < BD957X_VOUTS1)
			ovd_devs[i] = r->rdev;
	}
	if (may_have_irqs) {
		void *ret;
		/*
		 * We can add both the possible error and warning flags here
		 * because the core uses these only for status clearing and
		 * if we use warnings - errors are always clear and the other
		 * way around. We can also add CURRENT flag for all regulators
		 * because it is never set if it is not supported. Same applies
		 * to setting UVD for VoutS1 - it is not accidentally cleared
		 * as it is never set.
		 */
		int uvd_errs = REGULATOR_ERROR_UNDER_VOLTAGE |
			       REGULATOR_ERROR_UNDER_VOLTAGE_WARN |
			       REGULATOR_ERROR_OVER_CURRENT |
			       REGULATOR_ERROR_OVER_CURRENT_WARN;
		int ovd_errs = REGULATOR_ERROR_OVER_VOLTAGE_WARN |
			       REGULATOR_ERROR_REGULATION_OUT;
		int temp_errs = REGULATOR_ERROR_OVER_TEMP |
				REGULATOR_ERROR_OVER_TEMP_WARN;
		int irq;

		irq = platform_get_irq_byname(pdev, "bd9576-uvd");

		/* Register notifiers - can fail if IRQ is not given */
		ret = devm_regulator_irq_helper(&pdev->dev, &bd9576_notif_uvd,
						irq, 0, uvd_errs, NULL,
						&rdevs[0],
						BD9576_NUM_REGULATORS);
		if (IS_ERR(ret)) {
			if (PTR_ERR(ret) == -EPROBE_DEFER)
				return -EPROBE_DEFER;

			dev_warn(&pdev->dev, "UVD disabled %pe\n", ret);
		}

		irq = platform_get_irq_byname(pdev, "bd9576-ovd");

		ret = devm_regulator_irq_helper(&pdev->dev, &bd9576_notif_ovd,
						irq, 0, ovd_errs, NULL,
						&ovd_devs[0],
						BD9576_NUM_OVD_REGULATORS);
		if (IS_ERR(ret)) {
			if (PTR_ERR(ret) == -EPROBE_DEFER)
				return -EPROBE_DEFER;

			dev_warn(&pdev->dev, "OVD disabled %pe\n", ret);
		}
		irq = platform_get_irq_byname(pdev, "bd9576-temp");

		ret = devm_regulator_irq_helper(&pdev->dev, &bd9576_notif_temp,
						irq, 0, temp_errs, NULL,
						&rdevs[0],
						BD9576_NUM_REGULATORS);
		if (IS_ERR(ret)) {
			if (PTR_ERR(ret) == -EPROBE_DEFER)
				return -EPROBE_DEFER;

			dev_warn(&pdev->dev, "Thermal warning disabled %pe\n",
				 ret);
		}
	}
	return 0;
}

static const struct platform_device_id bd957x_pmic_id[] = {
	{ "bd9573-regulator", ROHM_CHIP_TYPE_BD9573 },
	{ "bd9576-regulator", ROHM_CHIP_TYPE_BD9576 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd957x_pmic_id);

static struct platform_driver bd957x_regulator = {
	.driver = {
		.name = "bd957x-pmic",
	},
	.probe = bd957x_probe,
	.id_table = bd957x_pmic_id,
};

module_platform_driver(bd957x_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9576/BD9573 voltage regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd957x-pmic");
