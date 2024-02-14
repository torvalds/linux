// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for regulators in MAX5970 and MAX5978 IC
 *
 * Copyright (c) 2022 9elements GmbH
 *
 * Author: Patrick Rudolph <patrick.rudolph@9elements.com>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>

#include <linux/mfd/max5970.h>

struct max5970_regulator {
	int num_switches, mon_rng, irng, shunt_micro_ohms, lim_uA;
	struct regmap *regmap;
};

enum max597x_regulator_id {
	MAX597X_SW0,
	MAX597X_SW1,
};

static int max5970_read_adc(struct regmap *regmap, int reg, long *val)
{
	u8 reg_data[2];
	int ret;

	ret = regmap_bulk_read(regmap, reg, &reg_data[0], 2);
	if (ret < 0)
		return ret;

	*val = (reg_data[0] << 2) | (reg_data[1] & 3);

	return 0;
}

static int max5970_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct regulator_dev **rdevs = dev_get_drvdata(dev);
	struct max5970_regulator *ddata = rdev_get_drvdata(rdevs[channel]);
	struct regmap *regmap = ddata->regmap;
	int ret;

	switch (type) {
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			ret = max5970_read_adc(regmap, MAX5970_REG_CURRENT_H(channel), val);
			if (ret < 0)
				return ret;
			/*
			 * Calculate current from ADC value, IRNG range & shunt resistor value.
			 * ddata->irng holds the voltage corresponding to the maximum value the
			 * 10-bit ADC can measure.
			 * To obtain the output, multiply the ADC value by the IRNG range (in
			 * millivolts) and then divide it by the maximum value of the 10-bit ADC.
			 */
			*val = (*val * ddata->irng) >> 10;
			/* Convert the voltage meansurement across shunt resistor to current */
			*val = (*val * 1000) / ddata->shunt_micro_ohms;
			return 0;
		default:
			return -EOPNOTSUPP;
		}

	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			ret = max5970_read_adc(regmap, MAX5970_REG_VOLTAGE_H(channel), val);
			if (ret < 0)
				return ret;
			/*
			 * Calculate voltage from ADC value and MON range.
			 * ddata->mon_rng holds the voltage corresponding to the maximum value the
			 * 10-bit ADC can measure.
			 * To obtain the output, multiply the ADC value by the MON range (in
			 * microvolts) and then divide it by the maximum value of the 10-bit ADC.
			 */
			*val = mul_u64_u32_shr(*val, ddata->mon_rng, 10);
			/* uV to mV */
			*val = *val / 1000;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max5970_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	struct regulator_dev **rdevs = (struct regulator_dev **)data;
	struct max5970_regulator *ddata;

	if (channel >= MAX5970_NUM_SWITCHES || !rdevs[channel])
		return 0;

	ddata = rdev_get_drvdata(rdevs[channel]);

	if (channel >= ddata->num_switches)
		return 0;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			/* Current measurement requires knowledge of the shunt resistor value. */
			if (ddata->shunt_micro_ohms)
				return 0444;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_ops max5970_hwmon_ops = {
	.is_visible = max5970_is_visible,
	.read = max5970_read,
};

static const struct hwmon_channel_info *max5970_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT, HWMON_I_INPUT),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT, HWMON_C_INPUT),
	NULL
};

static const struct hwmon_chip_info max5970_chip_info = {
	.ops = &max5970_hwmon_ops,
	.info = max5970_info,
};

static int max597x_uvp_ovp_check_mode(struct regulator_dev *rdev, int severity)
{
	int ret, reg;

	/* Status1 register contains the soft strap values sampled at POR */
	ret = regmap_read(rdev->regmap, MAX5970_REG_STATUS1, &reg);
	if (ret)
		return ret;

	/* Check soft straps match requested mode */
	if (severity == REGULATOR_SEVERITY_PROT) {
		if (STATUS1_PROT(reg) != STATUS1_PROT_SHUTDOWN)
			return -EOPNOTSUPP;

		return 0;
	}
	if (STATUS1_PROT(reg) == STATUS1_PROT_SHUTDOWN)
		return -EOPNOTSUPP;

	return 0;
}

static int max597x_set_vp(struct regulator_dev *rdev, int lim_uV, int severity,
			  bool enable, bool overvoltage)
{
	int off_h, off_l, reg, ret;
	struct max5970_regulator *data = rdev_get_drvdata(rdev);
	int channel = rdev_get_id(rdev);

	if (overvoltage) {
		if (severity == REGULATOR_SEVERITY_WARN) {
			off_h = MAX5970_REG_CH_OV_WARN_H(channel);
			off_l = MAX5970_REG_CH_OV_WARN_L(channel);
		} else {
			off_h = MAX5970_REG_CH_OV_CRIT_H(channel);
			off_l = MAX5970_REG_CH_OV_CRIT_L(channel);
		}
	} else {
		if (severity == REGULATOR_SEVERITY_WARN) {
			off_h = MAX5970_REG_CH_UV_WARN_H(channel);
			off_l = MAX5970_REG_CH_UV_WARN_L(channel);
		} else {
			off_h = MAX5970_REG_CH_UV_CRIT_H(channel);
			off_l = MAX5970_REG_CH_UV_CRIT_L(channel);
		}
	}

	if (enable)
		/* reg = ADC_MASK * (lim_uV / 1000000) / (data->mon_rng / 1000000) */
		reg = ADC_MASK * lim_uV / data->mon_rng;
	else
		reg = 0;

	ret = regmap_write(rdev->regmap, off_h, MAX5970_VAL2REG_H(reg));
	if (ret)
		return ret;

	ret = regmap_write(rdev->regmap, off_l, MAX5970_VAL2REG_L(reg));
	if (ret)
		return ret;

	return 0;
}

static int max597x_set_uvp(struct regulator_dev *rdev, int lim_uV, int severity,
			   bool enable)
{
	int ret;

	/*
	 * MAX5970 has enable control as a special value in limit reg. Can't
	 * set limit but keep feature disabled or enable W/O given limit.
	 */
	if ((lim_uV && !enable) || (!lim_uV && enable))
		return -EINVAL;

	ret = max597x_uvp_ovp_check_mode(rdev, severity);
	if (ret)
		return ret;

	return max597x_set_vp(rdev, lim_uV, severity, enable, false);
}

static int max597x_set_ovp(struct regulator_dev *rdev, int lim_uV, int severity,
			   bool enable)
{
	int ret;

	/*
	 * MAX5970 has enable control as a special value in limit reg. Can't
	 * set limit but keep feature disabled or enable W/O given limit.
	 */
	if ((lim_uV && !enable) || (!lim_uV && enable))
		return -EINVAL;

	ret = max597x_uvp_ovp_check_mode(rdev, severity);
	if (ret)
		return ret;

	return max597x_set_vp(rdev, lim_uV, severity, enable, true);
}

static int max597x_set_ocp(struct regulator_dev *rdev, int lim_uA,
			   int severity, bool enable)
{
	int val, reg;
	unsigned int vthst, vthfst;

	struct max5970_regulator *data = rdev_get_drvdata(rdev);
	int rdev_id = rdev_get_id(rdev);
	/*
	 * MAX5970 doesn't has enable control for ocp.
	 * If limit is specified but enable is not set then hold the value in
	 * variable & later use it when ocp needs to be enabled.
	 */
	if (lim_uA != 0 && lim_uA != data->lim_uA)
		data->lim_uA = lim_uA;

	if (severity != REGULATOR_SEVERITY_PROT)
		return -EINVAL;

	if (enable) {

		/* Calc Vtrip threshold in uV. */
		vthst =
		    div_u64(mul_u32_u32(data->shunt_micro_ohms, data->lim_uA),
			    1000000);

		/*
		 * As recommended in datasheed, add 20% margin to avoid
		 * spurious event & passive component tolerance.
		 */
		vthst = div_u64(mul_u32_u32(vthst, 120), 100);

		/* Calc fast Vtrip threshold in uV */
		vthfst = vthst * (MAX5970_FAST2SLOW_RATIO / 100);

		if (vthfst > data->irng) {
			dev_err(&rdev->dev, "Current limit out of range\n");
			return -EINVAL;
		}
		/* Fast trip threshold to be programmed */
		val = div_u64(mul_u32_u32(0xFF, vthfst), data->irng);
	} else
		/*
		 * Since there is no option to disable ocp, set limit to max
		 * value
		 */
		val = 0xFF;

	reg = MAX5970_REG_DAC_FAST(rdev_id);

	return regmap_write(rdev->regmap, reg, val);
}

static int max597x_get_status(struct regulator_dev *rdev)
{
	int val, ret;

	ret = regmap_read(rdev->regmap, MAX5970_REG_STATUS3, &val);
	if (ret)
		return ret;

	if (val & MAX5970_STATUS3_ALERT)
		return REGULATOR_STATUS_ERROR;

	ret = regulator_is_enabled_regmap(rdev);
	if (ret < 0)
		return ret;

	if (ret)
		return REGULATOR_STATUS_ON;

	return REGULATOR_STATUS_OFF;
}

static const struct regulator_ops max597x_switch_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = max597x_get_status,
	.set_over_voltage_protection = max597x_set_ovp,
	.set_under_voltage_protection = max597x_set_uvp,
	.set_over_current_protection = max597x_set_ocp,
};

static int max597x_dt_parse(struct device_node *np,
			    const struct regulator_desc *desc,
			    struct regulator_config *cfg)
{
	struct max5970_regulator *data = cfg->driver_data;
	int ret = 0;

	ret =
	    of_property_read_u32(np, "shunt-resistor-micro-ohms",
				 &data->shunt_micro_ohms);
	if (ret < 0)
		dev_err(cfg->dev,
			"property 'shunt-resistor-micro-ohms' not found, err %d\n",
			ret);
	return ret;

}

#define MAX597X_SWITCH(_ID, _ereg, _chan, _supply) {     \
	.name            = #_ID,                         \
	.of_match        = of_match_ptr(#_ID),           \
	.ops             = &max597x_switch_ops,          \
	.regulators_node = of_match_ptr("regulators"),   \
	.type            = REGULATOR_VOLTAGE,            \
	.id              = MAX597X_##_ID,                \
	.owner           = THIS_MODULE,                  \
	.supply_name     = _supply,                      \
	.enable_reg      = _ereg,                        \
	.enable_mask     = CHXEN((_chan)),               \
	.of_parse_cb	 = max597x_dt_parse,		 \
}

static const struct regulator_desc regulators[] = {
	MAX597X_SWITCH(SW0, MAX5970_REG_CHXEN, 0, "vss1"),
	MAX597X_SWITCH(SW1, MAX5970_REG_CHXEN, 1, "vss2"),
};

static int max597x_regmap_read_clear(struct regmap *map, unsigned int reg,
				     unsigned int *val)
{
	int ret;

	ret = regmap_read(map, reg, val);
	if (ret)
		return ret;

	if (*val)
		return regmap_write(map, reg, 0);

	return 0;
}

static int max597x_irq_handler(int irq, struct regulator_irq_data *rid,
			       unsigned long *dev_mask)
{
	struct regulator_err_state *stat;
	struct max5970_regulator *d = (struct max5970_regulator *)rid->data;
	int val, ret, i;

	ret = max597x_regmap_read_clear(d->regmap, MAX5970_REG_FAULT0, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	*dev_mask = 0;
	for (i = 0; i < d->num_switches; i++) {
		stat = &rid->states[i];
		stat->notifs = 0;
		stat->errors = 0;
	}

	for (i = 0; i < d->num_switches; i++) {
		stat = &rid->states[i];

		if (val & UV_STATUS_CRIT(i)) {
			*dev_mask |= 1 << i;
			stat->notifs |= REGULATOR_EVENT_UNDER_VOLTAGE;
			stat->errors |= REGULATOR_ERROR_UNDER_VOLTAGE;
		} else if (val & UV_STATUS_WARN(i)) {
			*dev_mask |= 1 << i;
			stat->notifs |= REGULATOR_EVENT_UNDER_VOLTAGE_WARN;
			stat->errors |= REGULATOR_ERROR_UNDER_VOLTAGE_WARN;
		}
	}

	ret = max597x_regmap_read_clear(d->regmap, MAX5970_REG_FAULT1, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	for (i = 0; i < d->num_switches; i++) {
		stat = &rid->states[i];

		if (val & OV_STATUS_CRIT(i)) {
			*dev_mask |= 1 << i;
			stat->notifs |= REGULATOR_EVENT_REGULATION_OUT;
			stat->errors |= REGULATOR_ERROR_REGULATION_OUT;
		} else if (val & OV_STATUS_WARN(i)) {
			*dev_mask |= 1 << i;
			stat->notifs |= REGULATOR_EVENT_OVER_VOLTAGE_WARN;
			stat->errors |= REGULATOR_ERROR_OVER_VOLTAGE_WARN;
		}
	}

	ret = max597x_regmap_read_clear(d->regmap, MAX5970_REG_FAULT2, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	for (i = 0; i < d->num_switches; i++) {
		stat = &rid->states[i];

		if (val & OC_STATUS_WARN(i)) {
			*dev_mask |= 1 << i;
			stat->notifs |= REGULATOR_EVENT_OVER_CURRENT_WARN;
			stat->errors |= REGULATOR_ERROR_OVER_CURRENT_WARN;
		}
	}

	ret = regmap_read(d->regmap, MAX5970_REG_STATUS0, &val);
	if (ret)
		return REGULATOR_FAILED_RETRY;

	for (i = 0; i < d->num_switches; i++) {
		stat = &rid->states[i];

		if ((val & MAX5970_CB_IFAULTF(i))
		    || (val & MAX5970_CB_IFAULTS(i))) {
			*dev_mask |= 1 << i;
			stat->notifs |=
			    REGULATOR_EVENT_OVER_CURRENT |
			    REGULATOR_EVENT_DISABLE;
			stat->errors |=
			    REGULATOR_ERROR_OVER_CURRENT | REGULATOR_ERROR_FAIL;

			/* Clear the sub-IRQ status */
			regulator_disable_regmap(stat->rdev);
		}
	}
	return 0;
}

static int max597x_adc_range(struct regmap *regmap, const int ch,
			     u32 *irng, u32 *mon_rng)
{
	unsigned int reg;
	int ret;

	/* Decode current ADC range */
	ret = regmap_read(regmap, MAX5970_REG_STATUS2, &reg);
	if (ret)
		return ret;
	switch (MAX5970_IRNG(reg, ch)) {
	case 0:
		*irng = 100000;	/* 100 mV */
		break;
	case 1:
		*irng = 50000;	/* 50 mV */
		break;
	case 2:
		*irng = 25000;	/* 25 mV */
		break;
	default:
		return -EINVAL;
	}

	/* Decode current voltage monitor range */
	ret = regmap_read(regmap, MAX5970_REG_MON_RANGE, &reg);
	if (ret)
		return ret;

	*mon_rng = MAX5970_MON_MAX_RANGE_UV >> MAX5970_MON(reg, ch);

	return 0;
}

static int max597x_setup_irq(struct device *dev,
			     int irq,
			     struct regulator_dev *rdevs[MAX5970_NUM_SWITCHES],
			     int num_switches, struct max5970_regulator *data)
{
	struct regulator_irq_desc max597x_notif = {
		.name = "max597x-irq",
		.map_event = max597x_irq_handler,
		.data = data,
	};
	int errs = REGULATOR_ERROR_UNDER_VOLTAGE |
	    REGULATOR_ERROR_UNDER_VOLTAGE_WARN |
	    REGULATOR_ERROR_OVER_VOLTAGE_WARN |
	    REGULATOR_ERROR_REGULATION_OUT |
	    REGULATOR_ERROR_OVER_CURRENT |
	    REGULATOR_ERROR_OVER_CURRENT_WARN | REGULATOR_ERROR_FAIL;
	void *irq_helper;

	/* Register notifiers - can fail if IRQ is not given */
	irq_helper = devm_regulator_irq_helper(dev, &max597x_notif,
					       irq, 0, errs, NULL,
					       &rdevs[0], num_switches);
	if (IS_ERR(irq_helper)) {
		if (PTR_ERR(irq_helper) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(dev, "IRQ disabled %pe\n", irq_helper);
	}

	return 0;
}

static int max597x_regulator_probe(struct platform_device *pdev)
{
	struct max5970_data *max597x;
	struct regmap *regmap = dev_get_regmap(pdev->dev.parent, NULL);
	struct max5970_regulator *data;
	struct i2c_client *i2c = to_i2c_client(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct regulator_dev **rdevs = NULL;
	struct device *hwmon_dev;
	int num_switches;
	int ret, i;

	if (!regmap)
		return -EPROBE_DEFER;

	max597x = devm_kzalloc(&i2c->dev, sizeof(struct max5970_data), GFP_KERNEL);
	if (!max597x)
		return -ENOMEM;

	rdevs = devm_kcalloc(&i2c->dev, MAX5970_NUM_SWITCHES, sizeof(struct regulator_dev *),
			     GFP_KERNEL);
	if (!rdevs)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max597x);

	if (of_device_is_compatible(i2c->dev.of_node, "maxim,max5978"))
		max597x->num_switches = MAX5978_NUM_SWITCHES;
	else if (of_device_is_compatible(i2c->dev.of_node, "maxim,max5970"))
		max597x->num_switches = MAX5970_NUM_SWITCHES;
	else
		return -ENODEV;

	num_switches = max597x->num_switches;

	for (i = 0; i < num_switches; i++) {
		data =
		    devm_kzalloc(&i2c->dev, sizeof(struct max5970_regulator),
				 GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		data->num_switches = num_switches;
		data->regmap = regmap;

		ret = max597x_adc_range(regmap, i, &max597x->irng[i], &max597x->mon_rng[i]);
		if (ret < 0)
			return ret;

		data->irng = max597x->irng[i];
		data->mon_rng = max597x->mon_rng[i];

		config.dev = &i2c->dev;
		config.driver_data = (void *)data;
		config.regmap = data->regmap;
		rdev = devm_regulator_register(&i2c->dev,
					       &regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&i2c->dev, "failed to register regulator %s\n",
				regulators[i].name);
			return PTR_ERR(rdev);
		}
		rdevs[i] = rdev;
		max597x->shunt_micro_ohms[i] = data->shunt_micro_ohms;
	}

	if (IS_REACHABLE(CONFIG_HWMON)) {
		hwmon_dev = devm_hwmon_device_register_with_info(&i2c->dev, "max5970", rdevs,
								 &max5970_chip_info, NULL);
		if (IS_ERR(hwmon_dev)) {
			return dev_err_probe(&i2c->dev, PTR_ERR(hwmon_dev),
					     "Unable to register hwmon device\n");
		}
	}

	if (i2c->irq) {
		ret =
		    max597x_setup_irq(&i2c->dev, i2c->irq, rdevs, num_switches,
				      data);
		if (ret) {
			dev_err(&i2c->dev, "IRQ setup failed");
			return ret;
		}
	}

	return ret;
}

static struct platform_driver max597x_regulator_driver = {
	.driver = {
		.name = "max5970-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = max597x_regulator_probe,
};

module_platform_driver(max597x_regulator_driver);


MODULE_AUTHOR("Patrick Rudolph <patrick.rudolph@9elements.com>");
MODULE_DESCRIPTION("MAX5970_hot-swap controller driver");
MODULE_LICENSE("GPL v2");
