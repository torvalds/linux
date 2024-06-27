// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 ROHM Semiconductors
// bd96801-regulator.c ROHM BD96801 regulator driver

/*
 * This version of the "BD86801 scalable PMIC"'s driver supports only very
 * basic set of the PMIC features. Most notably, there is no support for
 * the ERRB interrupt and the configurations which should be done when the
 * PMIC is in STBY mode.
 *
 * Supporting the ERRB interrupt would require dropping the regmap-IRQ
 * usage or working around (or accepting a presense of) a naming conflict
 * in debugFS IRQs.
 *
 * Being able to reliably do the configurations like changing the
 * regulator safety limits (like limits for the over/under -voltages, over
 * current, thermal protection) would require the configuring driver to be
 * synchronized with entity causing the PMIC state transitions. Eg, one
 * should be able to ensure the PMIC is in STBY state when the
 * configurations are applied to the hardware. How and when the PMIC state
 * transitions are to be done is likely to be very system specific, as will
 * be the need to configure these safety limits. Hence it's not simple to
 * come up with a generic solution.
 *
 * Users who require the ERRB handling and STBY state configurations can
 * have a look at the original RFC:
 * https://lore.kernel.org/all/cover.1712920132.git.mazziesaccount@gmail.com/
 * which implements a workaround to debugFS naming conflict and some of
 * the safety limit configurations - but leaves the state change handling
 * and synchronization to be implemented.
 *
 * It would be great to hear (and receive a patch!) if you implement the
 * STBY configuration support or a proper fix to the debugFS naming
 * conflict in your downstream driver ;)
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/mfd/rohm-bd96801.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/timer.h>

enum {
	BD96801_BUCK1,
	BD96801_BUCK2,
	BD96801_BUCK3,
	BD96801_BUCK4,
	BD96801_LDO5,
	BD96801_LDO6,
	BD96801_LDO7,
	BD96801_REGULATOR_AMOUNT,
};

enum {
	BD96801_PROT_OVP,
	BD96801_PROT_UVP,
	BD96801_PROT_OCP,
	BD96801_PROT_TEMP,
	BD96801_NUM_PROT,
};

#define BD96801_ALWAYS_ON_REG		0x3c
#define BD96801_REG_ENABLE		0x0b
#define BD96801_BUCK1_EN_MASK		BIT(0)
#define BD96801_BUCK2_EN_MASK		BIT(1)
#define BD96801_BUCK3_EN_MASK		BIT(2)
#define BD96801_BUCK4_EN_MASK		BIT(3)
#define BD96801_LDO5_EN_MASK		BIT(4)
#define BD96801_LDO6_EN_MASK		BIT(5)
#define BD96801_LDO7_EN_MASK		BIT(6)

#define BD96801_BUCK1_VSEL_REG		0x28
#define BD96801_BUCK2_VSEL_REG		0x29
#define BD96801_BUCK3_VSEL_REG		0x2a
#define BD96801_BUCK4_VSEL_REG		0x2b
#define BD96801_LDO5_VSEL_REG		0x25
#define BD96801_LDO6_VSEL_REG		0x26
#define BD96801_LDO7_VSEL_REG		0x27
#define BD96801_BUCK_VSEL_MASK		0x1F
#define BD96801_LDO_VSEL_MASK		0xff

#define BD96801_MASK_RAMP_DELAY		0xc0
#define BD96801_INT_VOUT_BASE_REG	0x21
#define BD96801_BUCK_INT_VOUT_MASK	0xff

#define BD96801_BUCK_VOLTS		256
#define BD96801_LDO_VOLTS		256

#define BD96801_OVP_MASK		0x03
#define BD96801_MASK_BUCK1_OVP_SHIFT	0x00
#define BD96801_MASK_BUCK2_OVP_SHIFT	0x02
#define BD96801_MASK_BUCK3_OVP_SHIFT	0x04
#define BD96801_MASK_BUCK4_OVP_SHIFT	0x06
#define BD96801_MASK_LDO5_OVP_SHIFT	0x00
#define BD96801_MASK_LDO6_OVP_SHIFT	0x02
#define BD96801_MASK_LDO7_OVP_SHIFT	0x04

#define BD96801_PROT_LIMIT_OCP_MIN	0x00
#define BD96801_PROT_LIMIT_LOW		0x01
#define BD96801_PROT_LIMIT_MID		0x02
#define BD96801_PROT_LIMIT_HI		0x03

#define BD96801_REG_BUCK1_OCP		0x32
#define BD96801_REG_BUCK2_OCP		0x32
#define BD96801_REG_BUCK3_OCP		0x33
#define BD96801_REG_BUCK4_OCP		0x33

#define BD96801_MASK_BUCK1_OCP_SHIFT	0x00
#define BD96801_MASK_BUCK2_OCP_SHIFT	0x04
#define BD96801_MASK_BUCK3_OCP_SHIFT	0x00
#define BD96801_MASK_BUCK4_OCP_SHIFT	0x04

#define BD96801_REG_LDO5_OCP		0x34
#define BD96801_REG_LDO6_OCP		0x34
#define BD96801_REG_LDO7_OCP		0x34

#define BD96801_MASK_LDO5_OCP_SHIFT	0x00
#define BD96801_MASK_LDO6_OCP_SHIFT	0x02
#define BD96801_MASK_LDO7_OCP_SHIFT	0x04

#define BD96801_MASK_SHD_INTB		BIT(7)
#define BD96801_INTB_FATAL		BIT(7)

#define BD96801_NUM_REGULATORS		7
#define BD96801_NUM_LDOS		4

/*
 * Ramp rates for bucks are controlled by bits [7:6] as follows:
 * 00 => 1 mV/uS
 * 01 => 5 mV/uS
 * 10 => 10 mV/uS
 * 11 => 20 mV/uS
 */
static const unsigned int buck_ramp_table[] = { 1000, 5000, 10000, 20000 };

/*
 * This is a voltage range that get's appended to selected
 * bd96801_buck_init_volts value. The range from 0x0 to 0xF is actually
 * bd96801_buck_init_volts + 0 ... bd96801_buck_init_volts + 150mV
 * and the range from 0x10 to 0x1f is bd96801_buck_init_volts - 150mV ...
 * bd96801_buck_init_volts - 0. But as the members of linear_range
 * are all unsigned I will apply offset of -150 mV to value in
 * linear_range - which should increase these ranges with
 * 150 mV getting all the values to >= 0.
 */
static const struct linear_range bd96801_tune_volts[] = {
	REGULATOR_LINEAR_RANGE(150000, 0x00, 0xF, 10000),
	REGULATOR_LINEAR_RANGE(0, 0x10, 0x1F, 10000),
};

static const struct linear_range bd96801_buck_init_volts[] = {
	REGULATOR_LINEAR_RANGE(500000 - 150000, 0x00, 0xc8, 5000),
	REGULATOR_LINEAR_RANGE(1550000 - 150000, 0xc9, 0xec, 50000),
	REGULATOR_LINEAR_RANGE(3300000 - 150000, 0xed, 0xff, 0),
};

static const struct linear_range bd96801_ldo_int_volts[] = {
	REGULATOR_LINEAR_RANGE(300000, 0x00, 0x78, 25000),
	REGULATOR_LINEAR_RANGE(3300000, 0x79, 0xff, 0),
};

#define BD96801_LDO_SD_VOLT_MASK	0x1
#define BD96801_LDO_MODE_MASK		0x6
#define BD96801_LDO_MODE_INT		0x0
#define BD96801_LDO_MODE_SD		0x2
#define BD96801_LDO_MODE_DDR		0x4

static int ldo_ddr_volt_table[] = {500000, 300000};
static int ldo_sd_volt_table[] = {3300000, 1800000};

/* Constant IRQ initialization data (templates) */
struct bd96801_irqinfo {
	int type;
	struct regulator_irq_desc irq_desc;
	int err_cfg;
	int wrn_cfg;
	const char *irq_name;
};

#define BD96801_IRQINFO(_type, _name, _irqoff_ms, _irqname)	\
{								\
	.type = (_type),					\
	.err_cfg = -1,						\
	.wrn_cfg = -1,						\
	.irq_name = (_irqname),					\
	.irq_desc = {						\
		.name = (_name),				\
		.irq_off_ms = (_irqoff_ms),			\
		.map_event = regulator_irq_map_event_simple,	\
	},							\
}

static const struct bd96801_irqinfo buck1_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck1-over-curr-h", 500,
			"bd96801-buck1-overcurr-h"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck1-over-curr-l", 500,
			"bd96801-buck1-overcurr-l"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck1-over-curr-n", 500,
			"bd96801-buck1-overcurr-n"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "buck1-over-voltage", 500,
			"bd96801-buck1-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "buck1-under-voltage", 500,
			"bd96801-buck1-undervolt"),
	BD96801_IRQINFO(BD96801_PROT_TEMP, "buck1-over-temp", 500,
			"bd96801-buck1-thermal")
};

static const struct bd96801_irqinfo buck2_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck2-over-curr-h", 500,
			"bd96801-buck2-overcurr-h"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck2-over-curr-l", 500,
			"bd96801-buck2-overcurr-l"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck2-over-curr-n", 500,
			"bd96801-buck2-overcurr-n"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "buck2-over-voltage", 500,
			"bd96801-buck2-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "buck2-under-voltage", 500,
			"bd96801-buck2-undervolt"),
	BD96801_IRQINFO(BD96801_PROT_TEMP, "buck2-over-temp", 500,
			"bd96801-buck2-thermal")
};

static const struct bd96801_irqinfo buck3_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck3-over-curr-h", 500,
			"bd96801-buck3-overcurr-h"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck3-over-curr-l", 500,
			"bd96801-buck3-overcurr-l"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck3-over-curr-n", 500,
			"bd96801-buck3-overcurr-n"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "buck3-over-voltage", 500,
			"bd96801-buck3-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "buck3-under-voltage", 500,
			"bd96801-buck3-undervolt"),
	BD96801_IRQINFO(BD96801_PROT_TEMP, "buck3-over-temp", 500,
			"bd96801-buck3-thermal")
};

static const struct bd96801_irqinfo buck4_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck4-over-curr-h", 500,
			"bd96801-buck4-overcurr-h"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck4-over-curr-l", 500,
			"bd96801-buck4-overcurr-l"),
	BD96801_IRQINFO(BD96801_PROT_OCP, "buck4-over-curr-n", 500,
			"bd96801-buck4-overcurr-n"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "buck4-over-voltage", 500,
			"bd96801-buck4-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "buck4-under-voltage", 500,
			"bd96801-buck4-undervolt"),
	BD96801_IRQINFO(BD96801_PROT_TEMP, "buck4-over-temp", 500,
			"bd96801-buck4-thermal")
};

static const struct bd96801_irqinfo ldo5_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "ldo5-overcurr", 500,
			"bd96801-ldo5-overcurr"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "ldo5-over-voltage", 500,
			"bd96801-ldo5-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "ldo5-under-voltage", 500,
			"bd96801-ldo5-undervolt"),
};

static const struct bd96801_irqinfo ldo6_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "ldo6-overcurr", 500,
			"bd96801-ldo6-overcurr"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "ldo6-over-voltage", 500,
			"bd96801-ldo6-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "ldo6-under-voltage", 500,
			"bd96801-ldo6-undervolt"),
};

static const struct bd96801_irqinfo ldo7_irqinfo[] = {
	BD96801_IRQINFO(BD96801_PROT_OCP, "ldo7-overcurr", 500,
			"bd96801-ldo7-overcurr"),
	BD96801_IRQINFO(BD96801_PROT_OVP, "ldo7-over-voltage", 500,
			"bd96801-ldo7-overvolt"),
	BD96801_IRQINFO(BD96801_PROT_UVP, "ldo7-under-voltage", 500,
			"bd96801-ldo7-undervolt"),
};

struct bd96801_irq_desc {
	struct bd96801_irqinfo *irqinfo;
	int num_irqs;
};

struct bd96801_regulator_data {
	struct regulator_desc desc;
	const struct linear_range *init_ranges;
	int num_ranges;
	struct bd96801_irq_desc irq_desc;
	int initial_voltage;
	int ldo_vol_lvl;
	int ldo_errs;
};

struct bd96801_pmic_data {
	struct bd96801_regulator_data regulator_data[BD96801_NUM_REGULATORS];
	struct regmap *regmap;
	int fatal_ind;
};

static int ldo_map_notif(int irq, struct regulator_irq_data *rid,
			 unsigned long *dev_mask)
{
	int i;

	for (i = 0; i < rid->num_states; i++) {
		struct bd96801_regulator_data *rdata;
		struct regulator_dev *rdev;

		rdev = rid->states[i].rdev;
		rdata = container_of(rdev->desc, struct bd96801_regulator_data,
				     desc);
		rid->states[i].notifs = regulator_err2notif(rdata->ldo_errs);
		rid->states[i].errors = rdata->ldo_errs;
		*dev_mask |= BIT(i);
	}
	return 0;
}

static int bd96801_list_voltage_lr(struct regulator_dev *rdev,
				   unsigned int selector)
{
	int voltage;
	struct bd96801_regulator_data *data;

	data = container_of(rdev->desc, struct bd96801_regulator_data, desc);

	/*
	 * The BD096801 has voltage setting in two registers. One giving the
	 * "initial voltage" (can be changed only when regulator is disabled.
	 * This driver caches the value and sets it only at startup. The other
	 * register is voltage tuning value which applies -150 mV ... +150 mV
	 * offset to the voltage.
	 *
	 * Note that the cached initial voltage stored in regulator data is
	 * 'scaled down' by the 150 mV so that all of our tuning values are
	 * >= 0. This is done because the linear_ranges uses unsigned values.
	 *
	 * As a result, we increase the tuning voltage which we get based on
	 * the selector by the stored initial_voltage.
	 */
	voltage = regulator_list_voltage_linear_range(rdev, selector);
	if (voltage < 0)
		return voltage;

	return voltage + data->initial_voltage;
}


static const struct regulator_ops bd96801_ldo_table_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops bd96801_buck_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = bd96801_list_voltage_lr,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};

static const struct regulator_ops bd96801_ldo_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static int buck_get_initial_voltage(struct regmap *regmap, struct device *dev,
				    struct bd96801_regulator_data *data)
{
	int ret = 0, sel, initial_uv;
	int reg = BD96801_INT_VOUT_BASE_REG + data->desc.id;

	if (data->num_ranges) {
		ret = regmap_read(regmap, reg, &sel);
		sel &= BD96801_BUCK_INT_VOUT_MASK;

		ret = linear_range_get_value_array(data->init_ranges,
						   data->num_ranges, sel,
						   &initial_uv);
		if (ret)
			return ret;

		data->initial_voltage = initial_uv;
		dev_dbg(dev, "Tune-scaled initial voltage %u\n",
			data->initial_voltage);
	}

	return 0;
}

static int get_ldo_initial_voltage(struct regmap *regmap,
				   struct device *dev,
				   struct bd96801_regulator_data *data)
{
	int ret;
	int cfgreg;

	ret = regmap_read(regmap, data->ldo_vol_lvl, &cfgreg);
	if (ret)
		return ret;

	switch (cfgreg & BD96801_LDO_MODE_MASK) {
	case BD96801_LDO_MODE_DDR:
		data->desc.volt_table = ldo_ddr_volt_table;
		data->desc.n_voltages = ARRAY_SIZE(ldo_ddr_volt_table);
		break;
	case BD96801_LDO_MODE_SD:
		data->desc.volt_table = ldo_sd_volt_table;
		data->desc.n_voltages = ARRAY_SIZE(ldo_sd_volt_table);
		break;
	default:
		dev_info(dev, "Leaving LDO to normal mode");
		return 0;
	}

	/* SD or DDR mode => override default ops */
	data->desc.ops = &bd96801_ldo_table_ops,
	data->desc.vsel_mask = 1;
	data->desc.vsel_reg = data->ldo_vol_lvl;

	return 0;
}

static int get_initial_voltage(struct device *dev, struct regmap *regmap,
			struct bd96801_regulator_data *data)
{
	/* BUCK */
	if (data->desc.id <= BD96801_BUCK4)
		return buck_get_initial_voltage(regmap, dev, data);

	/* LDO */
	return get_ldo_initial_voltage(regmap, dev, data);
}

static int bd96801_walk_regulator_dt(struct device *dev, struct regmap *regmap,
				     struct bd96801_regulator_data *data,
				     int num)
{
	int i, ret;
	struct device_node *np;
	struct device_node *nproot = dev->parent->of_node;

	nproot = of_get_child_by_name(nproot, "regulators");
	if (!nproot) {
		dev_err(dev, "failed to find regulators node\n");
		return -ENODEV;
	}
	for_each_child_of_node(nproot, np)
		for (i = 0; i < num; i++) {
			if (!of_node_name_eq(np, data[i].desc.of_match))
				continue;
			/*
			 * If STBY configs are supported, we must pass node
			 * here to extract the initial voltages from the DT.
			 * Thus we do the initial voltage getting in this
			 * loop.
			 */
			ret = get_initial_voltage(dev, regmap, &data[i]);
			if (ret) {
				dev_err(dev,
					"Initializing voltages for %s failed\n",
					data[i].desc.name);
				of_node_put(np);
				of_node_put(nproot);

				return ret;
			}
			if (of_property_read_bool(np, "rohm,keep-on-stby")) {
				ret = regmap_set_bits(regmap,
						      BD96801_ALWAYS_ON_REG,
						      1 << data[i].desc.id);
				if (ret) {
					dev_err(dev,
						"failed to set %s on-at-stby\n",
						data[i].desc.name);
					of_node_put(np);
					of_node_put(nproot);

					return ret;
				}
			}
		}
	of_node_put(nproot);

	return 0;
}

/*
 * Template for regulator data. Probe will allocate dynamic / driver instance
 * struct so we should be on a safe side even if there were multiple PMICs to
 * control. Note that there is a plan to allow multiple PMICs to be used so
 * systems can scale better. I am however still slightly unsure how the
 * multi-PMIC case will be handled. I don't know if the processor will have I2C
 * acces to all of the PMICs or only the first one. I'd guess there will be
 * access provided to all PMICs for voltage scaling - but the errors will only
 * be informed via the master PMIC. Eg, we should prepare to support multiple
 * driver instances - either with or without the IRQs... Well, let's first
 * just support the simple and clear single-PMIC setup and ponder the multi PMIC
 * case later. What we can easly do for preparing is to not use static global
 * data for regulators though.
 */
static const struct bd96801_pmic_data bd96801_data = {
	.regulator_data = {
	{
		.desc = {
			.name = "buck1",
			.of_match = of_match_ptr("buck1"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_BUCK1,
			.ops = &bd96801_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_tune_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_tune_volts),
			.n_voltages = BD96801_BUCK_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_BUCK1_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_BUCK1_VSEL_REG,
			.vsel_mask = BD96801_BUCK_VSEL_MASK,
			.ramp_reg = BD96801_BUCK1_VSEL_REG,
			.ramp_mask = BD96801_MASK_RAMP_DELAY,
			.ramp_delay_table = &buck_ramp_table[0],
			.n_ramp_values = ARRAY_SIZE(buck_ramp_table),
			.owner = THIS_MODULE,
		},
		.init_ranges = bd96801_buck_init_volts,
		.num_ranges = ARRAY_SIZE(bd96801_buck_init_volts),
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&buck1_irqinfo[0],
			.num_irqs = ARRAY_SIZE(buck1_irqinfo),
		},
	}, {
		.desc = {
			.name = "buck2",
			.of_match = of_match_ptr("buck2"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_BUCK2,
			.ops = &bd96801_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_tune_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_tune_volts),
			.n_voltages = BD96801_BUCK_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_BUCK2_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_BUCK2_VSEL_REG,
			.vsel_mask = BD96801_BUCK_VSEL_MASK,
			.ramp_reg = BD96801_BUCK2_VSEL_REG,
			.ramp_mask = BD96801_MASK_RAMP_DELAY,
			.ramp_delay_table = &buck_ramp_table[0],
			.n_ramp_values = ARRAY_SIZE(buck_ramp_table),
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&buck2_irqinfo[0],
			.num_irqs = ARRAY_SIZE(buck2_irqinfo),
		},
		.init_ranges = bd96801_buck_init_volts,
		.num_ranges = ARRAY_SIZE(bd96801_buck_init_volts),
	}, {
		.desc = {
			.name = "buck3",
			.of_match = of_match_ptr("buck3"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_BUCK3,
			.ops = &bd96801_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_tune_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_tune_volts),
			.n_voltages = BD96801_BUCK_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_BUCK3_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_BUCK3_VSEL_REG,
			.vsel_mask = BD96801_BUCK_VSEL_MASK,
			.ramp_reg = BD96801_BUCK3_VSEL_REG,
			.ramp_mask = BD96801_MASK_RAMP_DELAY,
			.ramp_delay_table = &buck_ramp_table[0],
			.n_ramp_values = ARRAY_SIZE(buck_ramp_table),
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&buck3_irqinfo[0],
			.num_irqs = ARRAY_SIZE(buck3_irqinfo),
		},
		.init_ranges = bd96801_buck_init_volts,
		.num_ranges = ARRAY_SIZE(bd96801_buck_init_volts),
	}, {
		.desc = {
			.name = "buck4",
			.of_match = of_match_ptr("buck4"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_BUCK4,
			.ops = &bd96801_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_tune_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_tune_volts),
			.n_voltages = BD96801_BUCK_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_BUCK4_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_BUCK4_VSEL_REG,
			.vsel_mask = BD96801_BUCK_VSEL_MASK,
			.ramp_reg = BD96801_BUCK4_VSEL_REG,
			.ramp_mask = BD96801_MASK_RAMP_DELAY,
			.ramp_delay_table = &buck_ramp_table[0],
			.n_ramp_values = ARRAY_SIZE(buck_ramp_table),
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&buck4_irqinfo[0],
			.num_irqs = ARRAY_SIZE(buck4_irqinfo),
		},
		.init_ranges = bd96801_buck_init_volts,
		.num_ranges = ARRAY_SIZE(bd96801_buck_init_volts),
	}, {
		.desc = {
			.name = "ldo5",
			.of_match = of_match_ptr("ldo5"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_LDO5,
			.ops = &bd96801_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_ldo_int_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_ldo_int_volts),
			.n_voltages = BD96801_LDO_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_LDO5_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_LDO5_VSEL_REG,
			.vsel_mask = BD96801_LDO_VSEL_MASK,
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&ldo5_irqinfo[0],
			.num_irqs = ARRAY_SIZE(ldo5_irqinfo),
		},
		.ldo_vol_lvl = BD96801_LDO5_VOL_LVL_REG,
	}, {
		.desc = {
			.name = "ldo6",
			.of_match = of_match_ptr("ldo6"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_LDO6,
			.ops = &bd96801_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_ldo_int_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_ldo_int_volts),
			.n_voltages = BD96801_LDO_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_LDO6_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_LDO6_VSEL_REG,
			.vsel_mask = BD96801_LDO_VSEL_MASK,
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&ldo6_irqinfo[0],
			.num_irqs = ARRAY_SIZE(ldo6_irqinfo),
		},
		.ldo_vol_lvl = BD96801_LDO6_VOL_LVL_REG,
	}, {
		.desc = {
			.name = "ldo7",
			.of_match = of_match_ptr("ldo7"),
			.regulators_node = of_match_ptr("regulators"),
			.id = BD96801_LDO7,
			.ops = &bd96801_ldo_ops,
			.type = REGULATOR_VOLTAGE,
			.linear_ranges = bd96801_ldo_int_volts,
			.n_linear_ranges = ARRAY_SIZE(bd96801_ldo_int_volts),
			.n_voltages = BD96801_LDO_VOLTS,
			.enable_reg = BD96801_REG_ENABLE,
			.enable_mask = BD96801_LDO7_EN_MASK,
			.enable_is_inverted = true,
			.vsel_reg = BD96801_LDO7_VSEL_REG,
			.vsel_mask = BD96801_LDO_VSEL_MASK,
			.owner = THIS_MODULE,
		},
		.irq_desc = {
			.irqinfo = (struct bd96801_irqinfo *)&ldo7_irqinfo[0],
			.num_irqs = ARRAY_SIZE(ldo7_irqinfo),
		},
		.ldo_vol_lvl = BD96801_LDO7_VOL_LVL_REG,
	},
	},
};

static int initialize_pmic_data(struct device *dev,
				struct bd96801_pmic_data *pdata)
{
	int r, i;

	/*
	 * Allocate and initialize IRQ data for all of the regulators. We
	 * wish to modify IRQ information independently for each driver
	 * instance.
	 */
	for (r = 0; r < BD96801_NUM_REGULATORS; r++) {
		const struct bd96801_irqinfo *template;
		struct bd96801_irqinfo *new;
		int num_infos;

		template = pdata->regulator_data[r].irq_desc.irqinfo;
		num_infos = pdata->regulator_data[r].irq_desc.num_irqs;

		new = devm_kcalloc(dev, num_infos, sizeof(*new), GFP_KERNEL);
		if (!new)
			return -ENOMEM;

		pdata->regulator_data[r].irq_desc.irqinfo = new;

		for (i = 0; i < num_infos; i++)
			new[i] = template[i];
	}

	return 0;
}

static int bd96801_rdev_intb_irqs(struct platform_device *pdev,
				  struct bd96801_pmic_data *pdata,
				  struct bd96801_irqinfo *iinfo,
				  struct regulator_dev *rdev)
{
	struct regulator_dev *rdev_arr[1];
	void *retp;
	int err = 0;
	int irq;
	int err_flags[] = {
		[BD96801_PROT_OVP] = REGULATOR_ERROR_REGULATION_OUT,
		[BD96801_PROT_UVP] = REGULATOR_ERROR_UNDER_VOLTAGE,
		[BD96801_PROT_OCP] = REGULATOR_ERROR_OVER_CURRENT,
		[BD96801_PROT_TEMP] = REGULATOR_ERROR_OVER_TEMP,

	};
	int wrn_flags[] = {
		[BD96801_PROT_OVP] = REGULATOR_ERROR_OVER_VOLTAGE_WARN,
		[BD96801_PROT_UVP] = REGULATOR_ERROR_UNDER_VOLTAGE_WARN,
		[BD96801_PROT_OCP] = REGULATOR_ERROR_OVER_CURRENT_WARN,
		[BD96801_PROT_TEMP] = REGULATOR_ERROR_OVER_TEMP_WARN,
	};

	/*
	 * Don't install IRQ handler if both error and warning
	 * notifications are explicitly disabled
	 */
	if (!iinfo->err_cfg && !iinfo->wrn_cfg)
		return 0;

	if (WARN_ON(iinfo->type >= BD96801_NUM_PROT))
		return -EINVAL;

	if (iinfo->err_cfg)
		err = err_flags[iinfo->type];
	else if (iinfo->wrn_cfg)
		err = wrn_flags[iinfo->type];

	iinfo->irq_desc.data = pdata;
	irq = platform_get_irq_byname(pdev, iinfo->irq_name);
	if (irq < 0)
		return irq;
	/* Find notifications for this IRQ (WARN/ERR) */

	rdev_arr[0] = rdev;
	retp = devm_regulator_irq_helper(&pdev->dev,
					 &iinfo->irq_desc, irq,
					 0, err, NULL, rdev_arr,
					 1);
	if (IS_ERR(retp))
		return PTR_ERR(retp);

	return 0;
}



static int bd96801_probe(struct platform_device *pdev)
{
	struct regulator_dev *ldo_errs_rdev_arr[BD96801_NUM_LDOS];
	struct bd96801_regulator_data *rdesc;
	struct regulator_config config = {};
	int ldo_errs_arr[BD96801_NUM_LDOS];
	struct bd96801_pmic_data *pdata;
	int temp_notif_ldos = 0;
	struct device *parent;
	int i, ret;
	void *retp;

	parent = pdev->dev.parent;

	pdata = devm_kmemdup(&pdev->dev, &bd96801_data, sizeof(bd96801_data),
			     GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (initialize_pmic_data(&pdev->dev, pdata))
		return -ENOMEM;

	pdata->regmap = dev_get_regmap(parent, NULL);
	if (!pdata->regmap) {
		dev_err(&pdev->dev, "No register map found\n");
		return -ENODEV;
	}

	rdesc = &pdata->regulator_data[0];

	config.driver_data = pdata;
	config.regmap = pdata->regmap;
	config.dev = parent;

	ret = bd96801_walk_regulator_dt(&pdev->dev, pdata->regmap, rdesc,
					BD96801_NUM_REGULATORS);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(pdata->regulator_data); i++) {
		struct regulator_dev *rdev;
		struct bd96801_irq_desc *idesc = &rdesc[i].irq_desc;
		int j;

		rdev = devm_regulator_register(&pdev->dev,
					       &rdesc[i].desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"failed to register %s regulator\n",
				rdesc[i].desc.name);
			return PTR_ERR(rdev);
		}
		/*
		 * LDOs don't have own temperature monitoring. If temperature
		 * notification was requested for this LDO from DT then we will
		 * add the regulator to be notified if central IC temperature
		 * exceeds threshold.
		 */
		if (rdesc[i].ldo_errs) {
			ldo_errs_rdev_arr[temp_notif_ldos] = rdev;
			ldo_errs_arr[temp_notif_ldos] = rdesc[i].ldo_errs;
			temp_notif_ldos++;
		}
		if (!idesc)
			continue;

		/* Register INTB handlers for configured protections */
		for (j = 0; j < idesc->num_irqs; j++) {
			ret = bd96801_rdev_intb_irqs(pdev, pdata,
						     &idesc->irqinfo[j], rdev);
			if (ret)
				return ret;
		}
	}
	if (temp_notif_ldos) {
		int irq;
		struct regulator_irq_desc tw_desc = {
			.name = "bd96801-core-thermal",
			.irq_off_ms = 500,
			.map_event = ldo_map_notif,
		};

		irq = platform_get_irq_byname(pdev, "bd96801-core-thermal");
		if (irq < 0)
			return irq;

		retp = devm_regulator_irq_helper(&pdev->dev, &tw_desc, irq, 0,
						 0, &ldo_errs_arr[0],
						 &ldo_errs_rdev_arr[0],
						 temp_notif_ldos);
		if (IS_ERR(retp))
			return PTR_ERR(retp);
	}

	return 0;
}

static const struct platform_device_id bd96801_pmic_id[] = {
	{ "bd96801-regulator", },
	{ }
};
MODULE_DEVICE_TABLE(platform, bd96801_pmic_id);

static struct platform_driver bd96801_regulator = {
	.driver = {
		.name = "bd96801-pmic"
	},
	.probe = bd96801_probe,
	.id_table = bd96801_pmic_id,
};

module_platform_driver(bd96801_regulator);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD96801 voltage regulator driver");
MODULE_LICENSE("GPL");
