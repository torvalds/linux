/*
 * AXP20x regulators driver.
 *
 * Copyright (C) 2013 Carlo Caione <carlo@caione.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define AXP20X_GPIO0_FUNC_MASK		GENMASK(3, 0)
#define AXP20X_GPIO1_FUNC_MASK		GENMASK(3, 0)

#define AXP20X_IO_ENABLED		0x03
#define AXP20X_IO_DISABLED		0x07

#define AXP20X_WORKMODE_DCDC2_MASK	BIT_MASK(2)
#define AXP20X_WORKMODE_DCDC3_MASK	BIT_MASK(1)

#define AXP20X_FREQ_DCDC_MASK		GENMASK(3, 0)

#define AXP20X_VBUS_IPSOUT_MGMT_MASK	BIT_MASK(2)

#define AXP20X_DCDC2_V_OUT_MASK		GENMASK(5, 0)
#define AXP20X_DCDC3_V_OUT_MASK		GENMASK(7, 0)
#define AXP20X_LDO24_V_OUT_MASK		GENMASK(7, 4)
#define AXP20X_LDO3_V_OUT_MASK		GENMASK(6, 0)
#define AXP20X_LDO5_V_OUT_MASK		GENMASK(7, 4)

#define AXP20X_PWR_OUT_EXTEN_MASK	BIT_MASK(0)
#define AXP20X_PWR_OUT_DCDC3_MASK	BIT_MASK(1)
#define AXP20X_PWR_OUT_LDO2_MASK	BIT_MASK(2)
#define AXP20X_PWR_OUT_LDO4_MASK	BIT_MASK(3)
#define AXP20X_PWR_OUT_DCDC2_MASK	BIT_MASK(4)
#define AXP20X_PWR_OUT_LDO3_MASK	BIT_MASK(6)

#define AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_RATE_MASK	BIT_MASK(0)
#define AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_RATE(x) \
	((x) << 0)
#define AXP20X_DCDC2_LDO3_V_RAMP_LDO3_RATE_MASK		BIT_MASK(1)
#define AXP20X_DCDC2_LDO3_V_RAMP_LDO3_RATE(x) \
	((x) << 1)
#define AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_EN_MASK		BIT_MASK(2)
#define AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_EN		BIT(2)
#define AXP20X_DCDC2_LDO3_V_RAMP_LDO3_EN_MASK		BIT_MASK(3)
#define AXP20X_DCDC2_LDO3_V_RAMP_LDO3_EN		BIT(3)

#define AXP20X_LDO4_V_OUT_1250mV_START	0x0
#define AXP20X_LDO4_V_OUT_1250mV_STEPS	0
#define AXP20X_LDO4_V_OUT_1250mV_END	\
	(AXP20X_LDO4_V_OUT_1250mV_START + AXP20X_LDO4_V_OUT_1250mV_STEPS)
#define AXP20X_LDO4_V_OUT_1300mV_START	0x1
#define AXP20X_LDO4_V_OUT_1300mV_STEPS	7
#define AXP20X_LDO4_V_OUT_1300mV_END	\
	(AXP20X_LDO4_V_OUT_1300mV_START + AXP20X_LDO4_V_OUT_1300mV_STEPS)
#define AXP20X_LDO4_V_OUT_2500mV_START	0x9
#define AXP20X_LDO4_V_OUT_2500mV_STEPS	0
#define AXP20X_LDO4_V_OUT_2500mV_END	\
	(AXP20X_LDO4_V_OUT_2500mV_START + AXP20X_LDO4_V_OUT_2500mV_STEPS)
#define AXP20X_LDO4_V_OUT_2700mV_START	0xa
#define AXP20X_LDO4_V_OUT_2700mV_STEPS	1
#define AXP20X_LDO4_V_OUT_2700mV_END	\
	(AXP20X_LDO4_V_OUT_2700mV_START + AXP20X_LDO4_V_OUT_2700mV_STEPS)
#define AXP20X_LDO4_V_OUT_3000mV_START	0xc
#define AXP20X_LDO4_V_OUT_3000mV_STEPS	3
#define AXP20X_LDO4_V_OUT_3000mV_END	\
	(AXP20X_LDO4_V_OUT_3000mV_START + AXP20X_LDO4_V_OUT_3000mV_STEPS)
#define AXP20X_LDO4_V_OUT_NUM_VOLTAGES	16

#define AXP22X_IO_ENABLED		0x03
#define AXP22X_IO_DISABLED		0x04

#define AXP22X_WORKMODE_DCDCX_MASK(x)	BIT_MASK(x)

#define AXP22X_MISC_N_VBUSEN_FUNC	BIT(4)

#define AXP22X_DCDC1_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DCDC2_V_OUT_MASK		GENMASK(5, 0)
#define AXP22X_DCDC3_V_OUT_MASK		GENMASK(5, 0)
#define AXP22X_DCDC4_V_OUT_MASK		GENMASK(5, 0)
#define AXP22X_DCDC5_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DC5LDO_V_OUT_MASK	GENMASK(2, 0)
#define AXP22X_ALDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_ALDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_ALDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DLDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DLDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DLDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_DLDO4_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_ELDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_ELDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_ELDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP22X_LDO_IO0_V_OUT_MASK	GENMASK(4, 0)
#define AXP22X_LDO_IO1_V_OUT_MASK	GENMASK(4, 0)

#define AXP22X_PWR_OUT_DC5LDO_MASK	BIT_MASK(0)
#define AXP22X_PWR_OUT_DCDC1_MASK	BIT_MASK(1)
#define AXP22X_PWR_OUT_DCDC2_MASK	BIT_MASK(2)
#define AXP22X_PWR_OUT_DCDC3_MASK	BIT_MASK(3)
#define AXP22X_PWR_OUT_DCDC4_MASK	BIT_MASK(4)
#define AXP22X_PWR_OUT_DCDC5_MASK	BIT_MASK(5)
#define AXP22X_PWR_OUT_ALDO1_MASK	BIT_MASK(6)
#define AXP22X_PWR_OUT_ALDO2_MASK	BIT_MASK(7)

#define AXP22X_PWR_OUT_SW_MASK		BIT_MASK(6)
#define AXP22X_PWR_OUT_DC1SW_MASK	BIT_MASK(7)

#define AXP22X_PWR_OUT_ELDO1_MASK	BIT_MASK(0)
#define AXP22X_PWR_OUT_ELDO2_MASK	BIT_MASK(1)
#define AXP22X_PWR_OUT_ELDO3_MASK	BIT_MASK(2)
#define AXP22X_PWR_OUT_DLDO1_MASK	BIT_MASK(3)
#define AXP22X_PWR_OUT_DLDO2_MASK	BIT_MASK(4)
#define AXP22X_PWR_OUT_DLDO3_MASK	BIT_MASK(5)
#define AXP22X_PWR_OUT_DLDO4_MASK	BIT_MASK(6)
#define AXP22X_PWR_OUT_ALDO3_MASK	BIT_MASK(7)

#define AXP803_PWR_OUT_DCDC1_MASK	BIT_MASK(0)
#define AXP803_PWR_OUT_DCDC2_MASK	BIT_MASK(1)
#define AXP803_PWR_OUT_DCDC3_MASK	BIT_MASK(2)
#define AXP803_PWR_OUT_DCDC4_MASK	BIT_MASK(3)
#define AXP803_PWR_OUT_DCDC5_MASK	BIT_MASK(4)
#define AXP803_PWR_OUT_DCDC6_MASK	BIT_MASK(5)

#define AXP803_PWR_OUT_FLDO1_MASK	BIT_MASK(2)
#define AXP803_PWR_OUT_FLDO2_MASK	BIT_MASK(3)

#define AXP803_DCDC1_V_OUT_MASK		GENMASK(4, 0)
#define AXP803_DCDC2_V_OUT_MASK		GENMASK(6, 0)
#define AXP803_DCDC3_V_OUT_MASK		GENMASK(6, 0)
#define AXP803_DCDC4_V_OUT_MASK		GENMASK(6, 0)
#define AXP803_DCDC5_V_OUT_MASK		GENMASK(6, 0)
#define AXP803_DCDC6_V_OUT_MASK		GENMASK(6, 0)

#define AXP803_FLDO1_V_OUT_MASK		GENMASK(3, 0)
#define AXP803_FLDO2_V_OUT_MASK		GENMASK(3, 0)

#define AXP803_DCDC23_POLYPHASE_DUAL	BIT(6)
#define AXP803_DCDC56_POLYPHASE_DUAL	BIT(5)

#define AXP803_DCDC234_500mV_START	0x00
#define AXP803_DCDC234_500mV_STEPS	70
#define AXP803_DCDC234_500mV_END	\
	(AXP803_DCDC234_500mV_START + AXP803_DCDC234_500mV_STEPS)
#define AXP803_DCDC234_1220mV_START	0x47
#define AXP803_DCDC234_1220mV_STEPS	4
#define AXP803_DCDC234_1220mV_END	\
	(AXP803_DCDC234_1220mV_START + AXP803_DCDC234_1220mV_STEPS)
#define AXP803_DCDC234_NUM_VOLTAGES	76

#define AXP803_DCDC5_800mV_START	0x00
#define AXP803_DCDC5_800mV_STEPS	32
#define AXP803_DCDC5_800mV_END		\
	(AXP803_DCDC5_800mV_START + AXP803_DCDC5_800mV_STEPS)
#define AXP803_DCDC5_1140mV_START	0x21
#define AXP803_DCDC5_1140mV_STEPS	35
#define AXP803_DCDC5_1140mV_END		\
	(AXP803_DCDC5_1140mV_START + AXP803_DCDC5_1140mV_STEPS)
#define AXP803_DCDC5_NUM_VOLTAGES	68

#define AXP803_DCDC6_600mV_START	0x00
#define AXP803_DCDC6_600mV_STEPS	50
#define AXP803_DCDC6_600mV_END		\
	(AXP803_DCDC6_600mV_START + AXP803_DCDC6_600mV_STEPS)
#define AXP803_DCDC6_1120mV_START	0x33
#define AXP803_DCDC6_1120mV_STEPS	14
#define AXP803_DCDC6_1120mV_END		\
	(AXP803_DCDC6_1120mV_START + AXP803_DCDC6_1120mV_STEPS)
#define AXP803_DCDC6_NUM_VOLTAGES	72

#define AXP803_DLDO2_700mV_START	0x00
#define AXP803_DLDO2_700mV_STEPS	26
#define AXP803_DLDO2_700mV_END		\
	(AXP803_DLDO2_700mV_START + AXP803_DLDO2_700mV_STEPS)
#define AXP803_DLDO2_3400mV_START	0x1b
#define AXP803_DLDO2_3400mV_STEPS	4
#define AXP803_DLDO2_3400mV_END		\
	(AXP803_DLDO2_3400mV_START + AXP803_DLDO2_3400mV_STEPS)
#define AXP803_DLDO2_NUM_VOLTAGES	32

#define AXP806_DCDCA_V_CTRL_MASK	GENMASK(6, 0)
#define AXP806_DCDCB_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_DCDCC_V_CTRL_MASK	GENMASK(6, 0)
#define AXP806_DCDCD_V_CTRL_MASK	GENMASK(5, 0)
#define AXP806_DCDCE_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_ALDO1_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_ALDO2_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_ALDO3_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_BLDO1_V_CTRL_MASK	GENMASK(3, 0)
#define AXP806_BLDO2_V_CTRL_MASK	GENMASK(3, 0)
#define AXP806_BLDO3_V_CTRL_MASK	GENMASK(3, 0)
#define AXP806_BLDO4_V_CTRL_MASK	GENMASK(3, 0)
#define AXP806_CLDO1_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_CLDO2_V_CTRL_MASK	GENMASK(4, 0)
#define AXP806_CLDO3_V_CTRL_MASK	GENMASK(4, 0)

#define AXP806_PWR_OUT_DCDCA_MASK	BIT_MASK(0)
#define AXP806_PWR_OUT_DCDCB_MASK	BIT_MASK(1)
#define AXP806_PWR_OUT_DCDCC_MASK	BIT_MASK(2)
#define AXP806_PWR_OUT_DCDCD_MASK	BIT_MASK(3)
#define AXP806_PWR_OUT_DCDCE_MASK	BIT_MASK(4)
#define AXP806_PWR_OUT_ALDO1_MASK	BIT_MASK(5)
#define AXP806_PWR_OUT_ALDO2_MASK	BIT_MASK(6)
#define AXP806_PWR_OUT_ALDO3_MASK	BIT_MASK(7)
#define AXP806_PWR_OUT_BLDO1_MASK	BIT_MASK(0)
#define AXP806_PWR_OUT_BLDO2_MASK	BIT_MASK(1)
#define AXP806_PWR_OUT_BLDO3_MASK	BIT_MASK(2)
#define AXP806_PWR_OUT_BLDO4_MASK	BIT_MASK(3)
#define AXP806_PWR_OUT_CLDO1_MASK	BIT_MASK(4)
#define AXP806_PWR_OUT_CLDO2_MASK	BIT_MASK(5)
#define AXP806_PWR_OUT_CLDO3_MASK	BIT_MASK(6)
#define AXP806_PWR_OUT_SW_MASK		BIT_MASK(7)

#define AXP806_DCDCAB_POLYPHASE_DUAL	0x40
#define AXP806_DCDCABC_POLYPHASE_TRI	0x80
#define AXP806_DCDCABC_POLYPHASE_MASK	GENMASK(7, 6)

#define AXP806_DCDCDE_POLYPHASE_DUAL	BIT(5)

#define AXP806_DCDCA_600mV_START	0x00
#define AXP806_DCDCA_600mV_STEPS	50
#define AXP806_DCDCA_600mV_END		\
	(AXP806_DCDCA_600mV_START + AXP806_DCDCA_600mV_STEPS)
#define AXP806_DCDCA_1120mV_START	0x33
#define AXP806_DCDCA_1120mV_STEPS	14
#define AXP806_DCDCA_1120mV_END		\
	(AXP806_DCDCA_1120mV_START + AXP806_DCDCA_1120mV_STEPS)
#define AXP806_DCDCA_NUM_VOLTAGES	72

#define AXP806_DCDCD_600mV_START	0x00
#define AXP806_DCDCD_600mV_STEPS	45
#define AXP806_DCDCD_600mV_END		\
	(AXP806_DCDCD_600mV_START + AXP806_DCDCD_600mV_STEPS)
#define AXP806_DCDCD_1600mV_START	0x2e
#define AXP806_DCDCD_1600mV_STEPS	17
#define AXP806_DCDCD_1600mV_END		\
	(AXP806_DCDCD_1600mV_START + AXP806_DCDCD_1600mV_STEPS)
#define AXP806_DCDCD_NUM_VOLTAGES	64

#define AXP809_DCDC4_600mV_START	0x00
#define AXP809_DCDC4_600mV_STEPS	47
#define AXP809_DCDC4_600mV_END		\
	(AXP809_DCDC4_600mV_START + AXP809_DCDC4_600mV_STEPS)
#define AXP809_DCDC4_1800mV_START	0x30
#define AXP809_DCDC4_1800mV_STEPS	8
#define AXP809_DCDC4_1800mV_END		\
	(AXP809_DCDC4_1800mV_START + AXP809_DCDC4_1800mV_STEPS)
#define AXP809_DCDC4_NUM_VOLTAGES	57

#define AXP813_DCDC7_V_OUT_MASK		GENMASK(6, 0)

#define AXP813_PWR_OUT_DCDC7_MASK	BIT_MASK(6)

#define AXP_DESC_IO(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		    _vmask, _ereg, _emask, _enable_val, _disable_val)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.enable_val	= (_enable_val),				\
		.disable_val	= (_disable_val),				\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		 _vmask, _ereg, _emask) 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC_SW(_family, _id, _match, _supply, _ereg, _emask)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.owner		= THIS_MODULE,					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops_sw,				\
	}

#define AXP_DESC_FIXED(_family, _id, _match, _supply, _volt)			\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= 1,						\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_volt) * 1000,				\
		.ops		= &axp20x_ops_fixed				\
	}

#define AXP_DESC_RANGES(_family, _id, _match, _supply, _ranges, _n_voltages,	\
			_vreg, _vmask, _ereg, _emask)				\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (_n_voltages),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.linear_ranges	= (_ranges),					\
		.n_linear_ranges = ARRAY_SIZE(_ranges),				\
		.ops		= &axp20x_ops_range,				\
	}

static const int axp209_dcdc2_ldo3_slew_rates[] = {
	1600,
	 800,
};

static int axp20x_set_ramp_delay(struct regulator_dev *rdev, int ramp)
{
	struct axp20x_dev *axp20x = rdev_get_drvdata(rdev);
	const struct regulator_desc *desc = rdev->desc;
	u8 reg, mask, enable, cfg = 0xff;
	const int *slew_rates;
	int rate_count = 0;

	if (!rdev)
		return -EINVAL;

	switch (axp20x->variant) {
	case AXP209_ID:
		if (desc->id == AXP20X_DCDC2) {
			rate_count = ARRAY_SIZE(axp209_dcdc2_ldo3_slew_rates);
			reg = AXP20X_DCDC2_LDO3_V_RAMP;
			mask = AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_RATE_MASK |
			       AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_EN_MASK;
			enable = (ramp > 0) ?
				 AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_EN :
				 !AXP20X_DCDC2_LDO3_V_RAMP_DCDC2_EN;
			break;
		}

		if (desc->id == AXP20X_LDO3) {
			slew_rates = axp209_dcdc2_ldo3_slew_rates;
			rate_count = ARRAY_SIZE(axp209_dcdc2_ldo3_slew_rates);
			reg = AXP20X_DCDC2_LDO3_V_RAMP;
			mask = AXP20X_DCDC2_LDO3_V_RAMP_LDO3_RATE_MASK |
			       AXP20X_DCDC2_LDO3_V_RAMP_LDO3_EN_MASK;
			enable = (ramp > 0) ?
				 AXP20X_DCDC2_LDO3_V_RAMP_LDO3_EN :
				 !AXP20X_DCDC2_LDO3_V_RAMP_LDO3_EN;
			break;
		}

		if (rate_count > 0)
			break;

		/* fall through */
	default:
		/* Not supported for this regulator */
		return -ENOTSUPP;
	}

	if (ramp == 0) {
		cfg = enable;
	} else {
		int i;

		for (i = 0; i < rate_count; i++) {
			if (ramp <= slew_rates[i])
				cfg = AXP20X_DCDC2_LDO3_V_RAMP_LDO3_RATE(i);
			else
				break;
		}

		if (cfg == 0xff) {
			dev_err(axp20x->dev, "unsupported ramp value %d", ramp);
			return -EINVAL;
		}

		cfg |= enable;
	}

	return regmap_update_bits(axp20x->regmap, reg, mask, cfg);
}

static int axp20x_regulator_enable_regmap(struct regulator_dev *rdev)
{
	struct axp20x_dev *axp20x = rdev_get_drvdata(rdev);
	const struct regulator_desc *desc = rdev->desc;

	if (!rdev)
		return -EINVAL;

	switch (axp20x->variant) {
	case AXP209_ID:
		if ((desc->id == AXP20X_LDO3) &&
		    rdev->constraints && rdev->constraints->soft_start) {
			int v_out;
			int ret;

			/*
			 * On some boards, the LDO3 can be overloaded when
			 * turning on, causing the entire PMIC to shutdown
			 * without warning. Turning it on at the minimal voltage
			 * and then setting the voltage to the requested value
			 * works reliably.
			 */
			if (regulator_is_enabled_regmap(rdev))
				break;

			v_out = regulator_get_voltage_sel_regmap(rdev);
			if (v_out < 0)
				return v_out;

			if (v_out == 0)
				break;

			ret = regulator_set_voltage_sel_regmap(rdev, 0x00);
			/*
			 * A small pause is needed between
			 * setting the voltage and enabling the LDO to give the
			 * internal state machine time to process the request.
			 */
			usleep_range(1000, 5000);
			ret |= regulator_enable_regmap(rdev);
			ret |= regulator_set_voltage_sel_regmap(rdev, v_out);

			return ret;
		}
		break;
	default:
		/* No quirks */
		break;
	}

	return regulator_enable_regmap(rdev);
};

static const struct regulator_ops axp20x_ops_fixed = {
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops axp20x_ops_range = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops axp20x_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= axp20x_regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_ramp_delay		= axp20x_set_ramp_delay,
};

static const struct regulator_ops axp20x_ops_sw = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_linear_range axp20x_ldo4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1250000,
			       AXP20X_LDO4_V_OUT_1250mV_START,
			       AXP20X_LDO4_V_OUT_1250mV_END,
			       0),
	REGULATOR_LINEAR_RANGE(1300000,
			       AXP20X_LDO4_V_OUT_1300mV_START,
			       AXP20X_LDO4_V_OUT_1300mV_END,
			       100000),
	REGULATOR_LINEAR_RANGE(2500000,
			       AXP20X_LDO4_V_OUT_2500mV_START,
			       AXP20X_LDO4_V_OUT_2500mV_END,
			       0),
	REGULATOR_LINEAR_RANGE(2700000,
			       AXP20X_LDO4_V_OUT_2700mV_START,
			       AXP20X_LDO4_V_OUT_2700mV_END,
			       100000),
	REGULATOR_LINEAR_RANGE(3000000,
			       AXP20X_LDO4_V_OUT_3000mV_START,
			       AXP20X_LDO4_V_OUT_3000mV_END,
			       100000),
};

static const struct regulator_desc axp20x_regulators[] = {
	AXP_DESC(AXP20X, DCDC2, "dcdc2", "vin2", 700, 2275, 25,
		 AXP20X_DCDC2_V_OUT, AXP20X_DCDC2_V_OUT_MASK,
		 AXP20X_PWR_OUT_CTRL, AXP20X_PWR_OUT_DCDC2_MASK),
	AXP_DESC(AXP20X, DCDC3, "dcdc3", "vin3", 700, 3500, 25,
		 AXP20X_DCDC3_V_OUT, AXP20X_DCDC3_V_OUT_MASK,
		 AXP20X_PWR_OUT_CTRL, AXP20X_PWR_OUT_DCDC3_MASK),
	AXP_DESC_FIXED(AXP20X, LDO1, "ldo1", "acin", 1300),
	AXP_DESC(AXP20X, LDO2, "ldo2", "ldo24in", 1800, 3300, 100,
		 AXP20X_LDO24_V_OUT, AXP20X_LDO24_V_OUT_MASK,
		 AXP20X_PWR_OUT_CTRL, AXP20X_PWR_OUT_LDO2_MASK),
	AXP_DESC(AXP20X, LDO3, "ldo3", "ldo3in", 700, 3500, 25,
		 AXP20X_LDO3_V_OUT, AXP20X_LDO3_V_OUT_MASK,
		 AXP20X_PWR_OUT_CTRL, AXP20X_PWR_OUT_LDO3_MASK),
	AXP_DESC_RANGES(AXP20X, LDO4, "ldo4", "ldo24in",
			axp20x_ldo4_ranges, AXP20X_LDO4_V_OUT_NUM_VOLTAGES,
			AXP20X_LDO24_V_OUT, AXP20X_LDO24_V_OUT_MASK,
			AXP20X_PWR_OUT_CTRL, AXP20X_PWR_OUT_LDO4_MASK),
	AXP_DESC_IO(AXP20X, LDO5, "ldo5", "ldo5in", 1800, 3300, 100,
		    AXP20X_LDO5_V_OUT, AXP20X_LDO5_V_OUT_MASK,
		    AXP20X_GPIO0_CTRL, AXP20X_GPIO0_FUNC_MASK,
		    AXP20X_IO_ENABLED, AXP20X_IO_DISABLED),
};

static const struct regulator_desc axp22x_regulators[] = {
	AXP_DESC(AXP22X, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, AXP22X_DCDC1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC1_MASK),
	AXP_DESC(AXP22X, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, AXP22X_DCDC2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC2_MASK),
	AXP_DESC(AXP22X, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, AXP22X_DCDC3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC3_MASK),
	AXP_DESC(AXP22X, DCDC4, "dcdc4", "vin4", 600, 1540, 20,
		 AXP22X_DCDC4_V_OUT, AXP22X_DCDC4_V_OUT,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC4_MASK),
	AXP_DESC(AXP22X, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, AXP22X_DCDC5_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC5_MASK),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP22X, DC1SW, "dc1sw", NULL,
		    AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DC1SW_MASK),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP22X, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, AXP22X_DC5LDO_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DC5LDO_MASK),
	AXP_DESC(AXP22X, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, AXP22X_ALDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_ALDO1_MASK),
	AXP_DESC(AXP22X, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, AXP22X_ALDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_ALDO2_MASK),
	AXP_DESC(AXP22X, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, AXP22X_ALDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP22X_PWR_OUT_ALDO3_MASK),
	AXP_DESC(AXP22X, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO1_V_OUT, AXP22X_DLDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO1_MASK),
	AXP_DESC(AXP22X, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, AXP22X_PWR_OUT_DLDO2_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO2_MASK),
	AXP_DESC(AXP22X, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO3_V_OUT, AXP22X_DLDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO3_MASK),
	AXP_DESC(AXP22X, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO4_V_OUT, AXP22X_DLDO4_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO4_MASK),
	AXP_DESC(AXP22X, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, AXP22X_ELDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO1_MASK),
	AXP_DESC(AXP22X, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, AXP22X_ELDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO1_MASK),
	AXP_DESC(AXP22X, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, AXP22X_ELDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO3_MASK),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, AXP22X_LDO_IO0_V_OUT_MASK,
		    AXP20X_GPIO0_CTRL, AXP20X_GPIO0_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, AXP22X_LDO_IO1_V_OUT_MASK,
		    AXP20X_GPIO1_CTRL, AXP20X_GPIO1_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP22X, RTC_LDO, "rtc_ldo", "ips", 3000),
};

static const struct regulator_desc axp22x_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbus",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP20X_VBUS_IPSOUT_MGMT,
	.enable_mask	= AXP20X_VBUS_IPSOUT_MGMT_MASK,
	.ops		= &axp20x_ops_sw,
};

/* DCDC ranges shared with AXP813 */
static const struct regulator_linear_range axp803_dcdc234_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000,
			       AXP803_DCDC234_500mV_START,
			       AXP803_DCDC234_500mV_END,
			       10000),
	REGULATOR_LINEAR_RANGE(1220000,
			       AXP803_DCDC234_1220mV_START,
			       AXP803_DCDC234_1220mV_END,
			       20000),
};

static const struct regulator_linear_range axp803_dcdc5_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000,
			       AXP803_DCDC5_800mV_START,
			       AXP803_DCDC5_800mV_END,
			       10000),
	REGULATOR_LINEAR_RANGE(1140000,
			       AXP803_DCDC5_1140mV_START,
			       AXP803_DCDC5_1140mV_END,
			       20000),
};

static const struct regulator_linear_range axp803_dcdc6_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000,
			       AXP803_DCDC6_600mV_START,
			       AXP803_DCDC6_600mV_END,
			       10000),
	REGULATOR_LINEAR_RANGE(1120000,
			       AXP803_DCDC6_1120mV_START,
			       AXP803_DCDC6_1120mV_END,
			       20000),
};

/* AXP806's CLDO2 and AXP809's DLDO1 share the same range */
static const struct regulator_linear_range axp803_dldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000,
			       AXP803_DLDO2_700mV_START,
			       AXP803_DLDO2_700mV_END,
			       100000),
	REGULATOR_LINEAR_RANGE(3400000,
			       AXP803_DLDO2_3400mV_START,
			       AXP803_DLDO2_3400mV_END,
			       200000),
};

static const struct regulator_desc axp803_regulators[] = {
	AXP_DESC(AXP803, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP803_DCDC1_V_OUT, AXP803_DCDC1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC1_MASK),
	AXP_DESC_RANGES(AXP803, DCDC2, "dcdc2", "vin2",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC2_V_OUT, AXP803_DCDC2_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC2_MASK),
	AXP_DESC_RANGES(AXP803, DCDC3, "dcdc3", "vin3",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC3_V_OUT, AXP803_DCDC3_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC3_MASK),
	AXP_DESC_RANGES(AXP803, DCDC4, "dcdc4", "vin4",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC4_V_OUT, AXP803_DCDC4_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC4_MASK),
	AXP_DESC_RANGES(AXP803, DCDC5, "dcdc5", "vin5",
			axp803_dcdc5_ranges, AXP803_DCDC5_NUM_VOLTAGES,
			AXP803_DCDC5_V_OUT, AXP803_DCDC5_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC5_MASK),
	AXP_DESC_RANGES(AXP803, DCDC6, "dcdc6", "vin6",
			axp803_dcdc6_ranges, AXP803_DCDC6_NUM_VOLTAGES,
			AXP803_DCDC6_V_OUT, AXP803_DCDC6_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC6_MASK),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP803, DC1SW, "dc1sw", NULL,
		    AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DC1SW_MASK),
	AXP_DESC(AXP803, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, AXP22X_ALDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO1_MASK),
	AXP_DESC(AXP803, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, AXP22X_ALDO2_V_OUT,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO2_MASK),
	AXP_DESC(AXP803, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, AXP22X_ALDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO3_MASK),
	AXP_DESC(AXP803, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO1_V_OUT, AXP22X_DLDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO1_MASK),
	AXP_DESC_RANGES(AXP803, DLDO2, "dldo2", "dldoin",
			axp803_dldo2_ranges, AXP803_DLDO2_NUM_VOLTAGES,
			AXP22X_DLDO2_V_OUT, AXP22X_DLDO2_V_OUT,
			AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO2_MASK),
	AXP_DESC(AXP803, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO3_V_OUT, AXP22X_DLDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO3_MASK),
	AXP_DESC(AXP803, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO4_V_OUT, AXP22X_DLDO4_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO4_MASK),
	AXP_DESC(AXP803, ELDO1, "eldo1", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO1_V_OUT, AXP22X_ELDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO1_MASK),
	AXP_DESC(AXP803, ELDO2, "eldo2", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO2_V_OUT, AXP22X_ELDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO2_MASK),
	AXP_DESC(AXP803, ELDO3, "eldo3", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO3_V_OUT, AXP22X_ELDO3_V_OUT,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO3_MASK),
	AXP_DESC(AXP803, FLDO1, "fldo1", "fldoin", 700, 1450, 50,
		 AXP803_FLDO1_V_OUT, AXP803_FLDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP803_PWR_OUT_FLDO1_MASK),
	AXP_DESC(AXP803, FLDO2, "fldo2", "fldoin", 700, 1450, 50,
		 AXP803_FLDO2_V_OUT, AXP803_FLDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP803_PWR_OUT_FLDO2_MASK),
	AXP_DESC_IO(AXP803, LDO_IO0, "ldo-io0", "ips", 700, 3300, 100,
		    AXP22X_LDO_IO0_V_OUT, AXP22X_LDO_IO0_V_OUT_MASK,
		    AXP20X_GPIO0_CTRL, AXP20X_GPIO0_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_IO(AXP803, LDO_IO1, "ldo-io1", "ips", 700, 3300, 100,
		    AXP22X_LDO_IO1_V_OUT, AXP22X_LDO_IO1_V_OUT_MASK,
		    AXP20X_GPIO1_CTRL, AXP20X_GPIO1_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP803, RTC_LDO, "rtc-ldo", "ips", 3000),
};

static const struct regulator_linear_range axp806_dcdca_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000,
			       AXP806_DCDCA_600mV_START,
			       AXP806_DCDCA_600mV_END,
			       10000),
	REGULATOR_LINEAR_RANGE(1120000,
			       AXP806_DCDCA_1120mV_START,
			       AXP806_DCDCA_1120mV_END,
			       20000),
};

static const struct regulator_linear_range axp806_dcdcd_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000,
			       AXP806_DCDCD_600mV_START,
			       AXP806_DCDCD_600mV_END,
			       20000),
	REGULATOR_LINEAR_RANGE(1600000,
			       AXP806_DCDCD_600mV_START,
			       AXP806_DCDCD_600mV_END,
			       100000),
};

static const struct regulator_desc axp806_regulators[] = {
	AXP_DESC_RANGES(AXP806, DCDCA, "dcdca", "vina",
			axp806_dcdca_ranges, AXP806_DCDCA_NUM_VOLTAGES,
			AXP806_DCDCA_V_CTRL, AXP806_DCDCA_V_CTRL_MASK,
			AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_DCDCA_MASK),
	AXP_DESC(AXP806, DCDCB, "dcdcb", "vinb", 1000, 2550, 50,
		 AXP806_DCDCB_V_CTRL, AXP806_DCDCB_V_CTRL,
		 AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_DCDCB_MASK),
	AXP_DESC_RANGES(AXP806, DCDCC, "dcdcc", "vinc",
			axp806_dcdca_ranges, AXP806_DCDCA_NUM_VOLTAGES,
			AXP806_DCDCC_V_CTRL, AXP806_DCDCC_V_CTRL_MASK,
			AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_DCDCC_MASK),
	AXP_DESC_RANGES(AXP806, DCDCD, "dcdcd", "vind",
			axp806_dcdcd_ranges, AXP806_DCDCD_NUM_VOLTAGES,
			AXP806_DCDCD_V_CTRL, AXP806_DCDCD_V_CTRL_MASK,
			AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_DCDCD_MASK),
	AXP_DESC(AXP806, DCDCE, "dcdce", "vine", 1100, 3400, 100,
		 AXP806_DCDCE_V_CTRL, AXP806_DCDCE_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_DCDCE_MASK),
	AXP_DESC(AXP806, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP806_ALDO1_V_CTRL, AXP806_ALDO1_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_ALDO1_MASK),
	AXP_DESC(AXP806, ALDO2, "aldo2", "aldoin", 700, 3400, 100,
		 AXP806_ALDO2_V_CTRL, AXP806_ALDO2_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_ALDO2_MASK),
	AXP_DESC(AXP806, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP806_ALDO3_V_CTRL, AXP806_ALDO3_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL1, AXP806_PWR_OUT_ALDO3_MASK),
	AXP_DESC(AXP806, BLDO1, "bldo1", "bldoin", 700, 1900, 100,
		 AXP806_BLDO1_V_CTRL, AXP806_BLDO1_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_BLDO1_MASK),
	AXP_DESC(AXP806, BLDO2, "bldo2", "bldoin", 700, 1900, 100,
		 AXP806_BLDO2_V_CTRL, AXP806_BLDO2_V_CTRL,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_BLDO2_MASK),
	AXP_DESC(AXP806, BLDO3, "bldo3", "bldoin", 700, 1900, 100,
		 AXP806_BLDO3_V_CTRL, AXP806_BLDO3_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_BLDO3_MASK),
	AXP_DESC(AXP806, BLDO4, "bldo4", "bldoin", 700, 1900, 100,
		 AXP806_BLDO4_V_CTRL, AXP806_BLDO4_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_BLDO4_MASK),
	AXP_DESC(AXP806, CLDO1, "cldo1", "cldoin", 700, 3300, 100,
		 AXP806_CLDO1_V_CTRL, AXP806_CLDO1_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_CLDO1_MASK),
	AXP_DESC_RANGES(AXP806, CLDO2, "cldo2", "cldoin",
			axp803_dldo2_ranges, AXP803_DLDO2_NUM_VOLTAGES,
			AXP806_CLDO2_V_CTRL, AXP806_CLDO2_V_CTRL_MASK,
			AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_CLDO2_MASK),
	AXP_DESC(AXP806, CLDO3, "cldo3", "cldoin", 700, 3300, 100,
		 AXP806_CLDO3_V_CTRL, AXP806_CLDO3_V_CTRL_MASK,
		 AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_CLDO3_MASK),
	AXP_DESC_SW(AXP806, SW, "sw", "swin",
		    AXP806_PWR_OUT_CTRL2, AXP806_PWR_OUT_SW_MASK),
};

static const struct regulator_linear_range axp809_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000,
			       AXP809_DCDC4_600mV_START,
			       AXP809_DCDC4_600mV_END,
			       20000),
	REGULATOR_LINEAR_RANGE(1800000,
			       AXP809_DCDC4_1800mV_START,
			       AXP809_DCDC4_1800mV_END,
			       100000),
};

static const struct regulator_desc axp809_regulators[] = {
	AXP_DESC(AXP809, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, AXP22X_DCDC1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC1_MASK),
	AXP_DESC(AXP809, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, AXP22X_DCDC2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC2_MASK),
	AXP_DESC(AXP809, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, AXP22X_DCDC3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC3_MASK),
	AXP_DESC_RANGES(AXP809, DCDC4, "dcdc4", "vin4",
			axp809_dcdc4_ranges, AXP809_DCDC4_NUM_VOLTAGES,
			AXP22X_DCDC4_V_OUT, AXP22X_DCDC4_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC4_MASK),
	AXP_DESC(AXP809, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, AXP22X_DCDC5_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DCDC5_MASK),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP809, DC1SW, "dc1sw", NULL,
		    AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DC1SW_MASK),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP809, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, AXP22X_DC5LDO_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_DC5LDO_MASK),
	AXP_DESC(AXP809, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, AXP22X_ALDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_ALDO1_MASK),
	AXP_DESC(AXP809, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, AXP22X_ALDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP22X_PWR_OUT_ALDO2_MASK),
	AXP_DESC(AXP809, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, AXP22X_ALDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ALDO3_MASK),
	AXP_DESC_RANGES(AXP809, DLDO1, "dldo1", "dldoin",
			axp803_dldo2_ranges, AXP803_DLDO2_NUM_VOLTAGES,
			AXP22X_DLDO1_V_OUT, AXP22X_DLDO1_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO1_MASK),
	AXP_DESC(AXP809, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, AXP22X_DLDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO2_MASK),
	AXP_DESC(AXP809, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, AXP22X_ELDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO1_MASK),
	AXP_DESC(AXP809, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, AXP22X_ELDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO2_MASK),
	AXP_DESC(AXP809, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, AXP22X_ELDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO3_MASK),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, AXP22X_LDO_IO0_V_OUT_MASK,
		    AXP20X_GPIO0_CTRL, AXP20X_GPIO0_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, AXP22X_LDO_IO1_V_OUT_MASK,
		    AXP20X_GPIO1_CTRL, AXP20X_GPIO1_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP809, RTC_LDO, "rtc_ldo", "ips", 1800),
	AXP_DESC_SW(AXP809, SW, "sw", "swin",
		    AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_SW_MASK),
};

static const struct regulator_desc axp813_regulators[] = {
	AXP_DESC(AXP813, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP803_DCDC1_V_OUT, AXP803_DCDC1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC1_MASK),
	AXP_DESC_RANGES(AXP813, DCDC2, "dcdc2", "vin2",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC2_V_OUT, AXP803_DCDC2_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC2_MASK),
	AXP_DESC_RANGES(AXP813, DCDC3, "dcdc3", "vin3",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC3_V_OUT, AXP803_DCDC3_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC3_MASK),
	AXP_DESC_RANGES(AXP813, DCDC4, "dcdc4", "vin4",
			axp803_dcdc234_ranges, AXP803_DCDC234_NUM_VOLTAGES,
			AXP803_DCDC4_V_OUT, AXP803_DCDC4_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC4_MASK),
	AXP_DESC_RANGES(AXP813, DCDC5, "dcdc5", "vin5",
			axp803_dcdc5_ranges, AXP803_DCDC5_NUM_VOLTAGES,
			AXP803_DCDC5_V_OUT, AXP803_DCDC5_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC5_MASK),
	AXP_DESC_RANGES(AXP813, DCDC6, "dcdc6", "vin6",
			axp803_dcdc6_ranges, AXP803_DCDC6_NUM_VOLTAGES,
			AXP803_DCDC6_V_OUT, AXP803_DCDC6_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP803_PWR_OUT_DCDC6_MASK),
	AXP_DESC_RANGES(AXP813, DCDC7, "dcdc7", "vin7",
			axp803_dcdc6_ranges, AXP803_DCDC6_NUM_VOLTAGES,
			AXP813_DCDC7_V_OUT, AXP813_DCDC7_V_OUT_MASK,
			AXP22X_PWR_OUT_CTRL1, AXP813_PWR_OUT_DCDC7_MASK),
	AXP_DESC(AXP813, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, AXP22X_ALDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO1_MASK),
	AXP_DESC(AXP813, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, AXP22X_ALDO2_V_OUT,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO2_MASK),
	AXP_DESC(AXP813, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, AXP22X_ALDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP806_PWR_OUT_ALDO3_MASK),
	AXP_DESC(AXP813, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO1_V_OUT, AXP22X_DLDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO1_MASK),
	AXP_DESC_RANGES(AXP813, DLDO2, "dldo2", "dldoin",
			axp803_dldo2_ranges, AXP803_DLDO2_NUM_VOLTAGES,
			AXP22X_DLDO2_V_OUT, AXP22X_DLDO2_V_OUT,
			AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO2_MASK),
	AXP_DESC(AXP813, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO3_V_OUT, AXP22X_DLDO3_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO3_MASK),
	AXP_DESC(AXP813, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO4_V_OUT, AXP22X_DLDO4_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DLDO4_MASK),
	AXP_DESC(AXP813, ELDO1, "eldo1", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO1_V_OUT, AXP22X_ELDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO1_MASK),
	AXP_DESC(AXP813, ELDO2, "eldo2", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO2_V_OUT, AXP22X_ELDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO2_MASK),
	AXP_DESC(AXP813, ELDO3, "eldo3", "eldoin", 700, 1900, 50,
		 AXP22X_ELDO3_V_OUT, AXP22X_ELDO3_V_OUT,
		 AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_ELDO3_MASK),
	/* to do / check ... */
	AXP_DESC(AXP813, FLDO1, "fldo1", "fldoin", 700, 1450, 50,
		 AXP803_FLDO1_V_OUT, AXP803_FLDO1_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP803_PWR_OUT_FLDO1_MASK),
	AXP_DESC(AXP813, FLDO2, "fldo2", "fldoin", 700, 1450, 50,
		 AXP803_FLDO2_V_OUT, AXP803_FLDO2_V_OUT_MASK,
		 AXP22X_PWR_OUT_CTRL3, AXP803_PWR_OUT_FLDO2_MASK),
	/*
	 * TODO: FLDO3 = {DCDC5, FLDOIN} / 2
	 *
	 * This means FLDO3 effectively switches supplies at runtime,
	 * something the regulator subsystem does not support.
	 */
	AXP_DESC_FIXED(AXP813, RTC_LDO, "rtc-ldo", "ips", 1800),
	AXP_DESC_IO(AXP813, LDO_IO0, "ldo-io0", "ips", 700, 3300, 100,
		    AXP22X_LDO_IO0_V_OUT, AXP22X_LDO_IO0_V_OUT_MASK,
		    AXP20X_GPIO0_CTRL, AXP20X_GPIO0_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_IO(AXP813, LDO_IO1, "ldo-io1", "ips", 700, 3300, 100,
		    AXP22X_LDO_IO1_V_OUT, AXP22X_LDO_IO1_V_OUT_MASK,
		    AXP20X_GPIO1_CTRL, AXP20X_GPIO1_FUNC_MASK,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_SW(AXP813, SW, "sw", "swin",
		    AXP22X_PWR_OUT_CTRL2, AXP22X_PWR_OUT_DC1SW_MASK),
};

static int axp20x_set_dcdc_freq(struct platform_device *pdev, u32 dcdcfreq)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	unsigned int reg = AXP20X_DCDC_FREQ;
	u32 min, max, def, step;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		min = 750;
		max = 1875;
		def = 1500;
		step = 75;
		break;
	case AXP803_ID:
	case AXP813_ID:
		/*
		 * AXP803/AXP813 DCDC work frequency setting has the same
		 * range and step as AXP22X, but at a different register.
		 * (See include/linux/mfd/axp20x.h)
		 */
		reg = AXP803_DCDC_FREQ_CTRL;
		/* Fall through to the check below.*/
	case AXP806_ID:
		/*
		 * AXP806 also have DCDC work frequency setting register at a
		 * different position.
		 */
		if (axp20x->variant == AXP806_ID)
			reg = AXP806_DCDC_FREQ_CTRL;
		/* Fall through */
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		min = 1800;
		max = 4050;
		def = 3000;
		step = 150;
		break;
	default:
		dev_err(&pdev->dev,
			"Setting DCDC frequency for unsupported AXP variant\n");
		return -EINVAL;
	}

	if (dcdcfreq == 0)
		dcdcfreq = def;

	if (dcdcfreq < min) {
		dcdcfreq = min;
		dev_warn(&pdev->dev, "DCDC frequency too low. Set to %ukHz\n",
			 min);
	}

	if (dcdcfreq > max) {
		dcdcfreq = max;
		dev_warn(&pdev->dev, "DCDC frequency too high. Set to %ukHz\n",
			 max);
	}

	dcdcfreq = (dcdcfreq - min) / step;

	return regmap_update_bits(axp20x->regmap, reg,
				  AXP20X_FREQ_DCDC_MASK, dcdcfreq);
}

static int axp20x_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;
	u32 dcdcfreq = 0;

	np = of_node_get(pdev->dev.parent->of_node);
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulators node not found\n");
	} else {
		of_property_read_u32(regulators, "x-powers,dcdc-freq", &dcdcfreq);
		ret = axp20x_set_dcdc_freq(pdev, dcdcfreq);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error setting dcdc frequency: %d\n", ret);
			return ret;
		}

		of_node_put(regulators);
	}

	return 0;
}

static int axp20x_set_dcdc_workmode(struct regulator_dev *rdev, int id, u32 workmode)
{
	struct axp20x_dev *axp20x = rdev_get_drvdata(rdev);
	unsigned int reg = AXP20X_DCDC_MODE;
	unsigned int mask;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		if ((id != AXP20X_DCDC2) && (id != AXP20X_DCDC3))
			return -EINVAL;

		mask = AXP20X_WORKMODE_DCDC2_MASK;
		if (id == AXP20X_DCDC3)
			mask = AXP20X_WORKMODE_DCDC3_MASK;

		workmode <<= ffs(mask) - 1;
		break;

	case AXP806_ID:
		reg = AXP806_DCDC_MODE_CTRL2;
		/*
		 * AXP806 DCDC regulator IDs have the same range as AXP22X.
		 * Fall through to the check below.
		 * (See include/linux/mfd/axp20x.h)
		 */
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		if (id < AXP22X_DCDC1 || id > AXP22X_DCDC5)
			return -EINVAL;

		mask = AXP22X_WORKMODE_DCDCX_MASK(id - AXP22X_DCDC1);
		workmode <<= id - AXP22X_DCDC1;
		break;

	case AXP803_ID:
		if (id < AXP803_DCDC1 || id > AXP803_DCDC6)
			return -EINVAL;

		mask = AXP22X_WORKMODE_DCDCX_MASK(id - AXP803_DCDC1);
		workmode <<= id - AXP803_DCDC1;
		break;

	case AXP813_ID:
		if (id < AXP813_DCDC1 || id > AXP813_DCDC7)
			return -EINVAL;

		mask = AXP22X_WORKMODE_DCDCX_MASK(id - AXP813_DCDC1);
		workmode <<= id - AXP813_DCDC1;
		break;

	default:
		/* should not happen */
		WARN_ON(1);
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, reg, mask, workmode);
}

/*
 * This function checks whether a regulator is part of a poly-phase
 * output setup based on the registers settings. Returns true if it is.
 */
static bool axp20x_is_polyphase_slave(struct axp20x_dev *axp20x, int id)
{
	u32 reg = 0;

	/*
	 * Currently in our supported AXP variants, only AXP803, AXP806,
	 * and AXP813 have polyphase regulators.
	 */
	switch (axp20x->variant) {
	case AXP803_ID:
	case AXP813_ID:
		regmap_read(axp20x->regmap, AXP803_POLYPHASE_CTRL, &reg);

		switch (id) {
		case AXP803_DCDC3:
			return !!(reg & AXP803_DCDC23_POLYPHASE_DUAL);
		case AXP803_DCDC6:
			return !!(reg & AXP803_DCDC56_POLYPHASE_DUAL);
		}
		break;

	case AXP806_ID:
		regmap_read(axp20x->regmap, AXP806_DCDC_MODE_CTRL2, &reg);

		switch (id) {
		case AXP806_DCDCB:
			return (((reg & AXP806_DCDCABC_POLYPHASE_MASK) ==
				AXP806_DCDCAB_POLYPHASE_DUAL) ||
				((reg & AXP806_DCDCABC_POLYPHASE_MASK) ==
				AXP806_DCDCABC_POLYPHASE_TRI));
		case AXP806_DCDCC:
			return ((reg & AXP806_DCDCABC_POLYPHASE_MASK) ==
				AXP806_DCDCABC_POLYPHASE_TRI);
		case AXP806_DCDCE:
			return !!(reg & AXP806_DCDCDE_POLYPHASE_DUAL);
		}
		break;

	default:
		return false;
	}

	return false;
}

static int axp20x_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *regulators;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
		.regmap = axp20x->regmap,
		.driver_data = axp20x,
	};
	int ret, i, nregulators;
	u32 workmode;
	const char *dcdc1_name = axp22x_regulators[AXP22X_DCDC1].name;
	const char *dcdc5_name = axp22x_regulators[AXP22X_DCDC5].name;
	bool drivevbus = false;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		regulators = axp20x_regulators;
		nregulators = AXP20X_REG_ID_MAX;
		break;
	case AXP221_ID:
	case AXP223_ID:
		regulators = axp22x_regulators;
		nregulators = AXP22X_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	case AXP803_ID:
		regulators = axp803_regulators;
		nregulators = AXP803_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	case AXP806_ID:
		regulators = axp806_regulators;
		nregulators = AXP806_REG_ID_MAX;
		break;
	case AXP809_ID:
		regulators = axp809_regulators;
		nregulators = AXP809_REG_ID_MAX;
		break;
	case AXP813_ID:
		regulators = axp813_regulators;
		nregulators = AXP813_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	default:
		dev_err(&pdev->dev, "Unsupported AXP variant: %ld\n",
			axp20x->variant);
		return -EINVAL;
	}

	/* This only sets the dcdc freq. Ignore any errors */
	axp20x_regulator_parse_dt(pdev);

	for (i = 0; i < nregulators; i++) {
		const struct regulator_desc *desc = &regulators[i];
		struct regulator_desc *new_desc;

		/*
		 * If this regulator is a slave in a poly-phase setup,
		 * skip it, as its controls are bound to the master
		 * regulator and won't work.
		 */
		if (axp20x_is_polyphase_slave(axp20x, i))
			continue;

		/* Support for AXP813's FLDO3 is not implemented */
		if (axp20x->variant == AXP813_ID && i == AXP813_FLDO3)
			continue;

		/*
		 * Regulators DC1SW and DC5LDO are connected internally,
		 * so we have to handle their supply names separately.
		 *
		 * We always register the regulators in proper sequence,
		 * so the supply names are correctly read. See the last
		 * part of this loop to see where we save the DT defined
		 * name.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DC1SW) ||
		    (regulators == axp803_regulators && i == AXP803_DC1SW) ||
		    (regulators == axp809_regulators && i == AXP809_DC1SW)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			if (!new_desc)
				return -ENOMEM;

			*new_desc = regulators[i];
			new_desc->supply_name = dcdc1_name;
			desc = new_desc;
		}

		if ((regulators == axp22x_regulators && i == AXP22X_DC5LDO) ||
		    (regulators == axp809_regulators && i == AXP809_DC5LDO)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			if (!new_desc)
				return -ENOMEM;

			*new_desc = regulators[i];
			new_desc->supply_name = dcdc5_name;
			desc = new_desc;
		}

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %s\n",
				regulators[i].name);

			return PTR_ERR(rdev);
		}

		ret = of_property_read_u32(rdev->dev.of_node,
					   "x-powers,dcdc-workmode",
					   &workmode);
		if (!ret) {
			if (axp20x_set_dcdc_workmode(rdev, i, workmode))
				dev_err(&pdev->dev, "Failed to set workmode on %s\n",
					rdev->desc->name);
		}

		/*
		 * Save AXP22X DCDC1 / DCDC5 regulator names for later.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DCDC1) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC1))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc1_name);

		if ((regulators == axp22x_regulators && i == AXP22X_DCDC5) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC5))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc5_name);
	}

	if (drivevbus) {
		/* Change N_VBUSEN sense pin to DRIVEVBUS output pin */
		regmap_update_bits(axp20x->regmap, AXP20X_OVER_TMP,
				   AXP22X_MISC_N_VBUSEN_FUNC, 0);
		rdev = devm_regulator_register(&pdev->dev,
					       &axp22x_drivevbus_regulator,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register drivevbus\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver axp20x_regulator_driver = {
	.probe	= axp20x_regulator_probe,
	.driver	= {
		.name		= "axp20x-regulator",
	},
};

module_platform_driver(axp20x_regulator_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Regulator Driver for AXP20X PMIC");
MODULE_ALIAS("platform:axp20x-regulator");
