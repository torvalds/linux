// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/util_macros.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mod_devicetable.h>

/* Register */
#define RTQ2208_REG_GLOBAL_INT1			0x12
#define RTQ2208_REG_FLT_RECORDBUCK_CB		0x18
#define RTQ2208_REG_GLOBAL_INT1_MASK		0x1D
#define RTQ2208_REG_FLT_MASKBUCK_CB		0x1F
#define RTQ2208_REG_BUCK_C_CFG0			0x32
#define RTQ2208_REG_BUCK_B_CFG0			0x42
#define RTQ2208_REG_BUCK_A_CFG0			0x52
#define RTQ2208_REG_BUCK_D_CFG0			0x62
#define RTQ2208_REG_BUCK_G_CFG0			0x72
#define RTQ2208_REG_BUCK_F_CFG0			0x82
#define RTQ2208_REG_BUCK_E_CFG0			0x92
#define RTQ2208_REG_BUCK_H_CFG0			0xA2
#define RTQ2208_REG_LDO1_CFG			0xB1
#define RTQ2208_REG_LDO2_CFG			0xC1
#define RTQ2208_REG_LDO_DVS_CTRL		0xD0
#define RTQ2208_REG_HIDDEN_BUCKPH		0x55
#define RTQ2208_REG_HIDDEN_LDOCFG0		0x8F
#define RTQ2208_REG_HIDDEN_LDOCFG1		0x96
#define RTQ2208_REG_HIDDEN0			0xFE
#define RTQ2208_REG_HIDDEN1			0xFF

/* Mask */
#define RTQ2208_BUCK_NR_MTP_SEL_MASK		GENMASK(7, 0)
#define RTQ2208_BUCK_EN_NR_MTP_SEL0_MASK	BIT(0)
#define RTQ2208_BUCK_EN_NR_MTP_SEL1_MASK	BIT(1)
#define RTQ2208_BUCK_RSPUP_MASK			GENMASK(6, 4)
#define RTQ2208_BUCK_RSPDN_MASK			GENMASK(2, 0)
#define RTQ2208_BUCK_NRMODE_MASK		BIT(5)
#define RTQ2208_BUCK_STRMODE_MASK		BIT(5)
#define RTQ2208_BUCK_EN_STR_MASK		BIT(0)
#define RTQ2208_LDO_EN_STR_MASK			BIT(7)
#define RTQ2208_EN_DIS_MASK			BIT(0)
#define RTQ2208_BUCK_RAMP_SEL_MASK		GENMASK(2, 0)
#define RTQ2208_HD_INT_MASK			BIT(0)
#define RTQ2208_LDO1_DISCHG_EN_MASK		BIT(4)
#define RTQ2208_LDO1_VOSEL_SD_MASK		BIT(5)
#define RTQ2208_LDO2_DISCHG_EN_MASK		BIT(6)
#define RTQ2208_LDO2_VOSEL_SD_MASK		BIT(7)
#define RTQ2208_MASK_BUCKPH_GROUP1		GENMASK(6, 4)
#define RTQ2208_MASK_BUCKPH_GROUP2		GENMASK(2, 0)
#define RTQ2208_MASK_LDO2_OPT0			BIT(7)
#define RTQ2208_MASK_LDO2_OPT1			BIT(7)
#define RTQ2208_MASK_LDO1_FIXED			BIT(6)

/* Size */
#define RTQ2208_VOUT_MAXNUM			256
#define RTQ2208_BUCK_NUM_IRQ_REGS		5
#define RTQ2208_STS_NUM_IRQ_REGS		2

/* Value */
#define RTQ2208_RAMP_VALUE_MIN_uV		500
#define RTQ2208_RAMP_VALUE_MAX_uV		16000

#define RTQ2208_BUCK_MASK(uv_irq, ov_irq)	(1 << ((uv_irq) % 8) | 1 << ((ov_irq) % 8))

enum {
	RTQ2208_BUCK_B = 0,
	RTQ2208_BUCK_C,
	RTQ2208_BUCK_D,
	RTQ2208_BUCK_A,
	RTQ2208_BUCK_F,
	RTQ2208_BUCK_G,
	RTQ2208_BUCK_H,
	RTQ2208_BUCK_E,
	RTQ2208_LDO2,
	RTQ2208_LDO1,
	RTQ2208_LDO_MAX,
};

enum {
	RTQ2208_AUTO_MODE = 0,
	RTQ2208_FCCM,
};

struct rtq2208_regulator_desc {
	struct regulator_desc desc;
	unsigned int mtp_sel_reg;
	unsigned int mtp_sel_mask;
	unsigned int mode_reg;
	unsigned int mode_mask;
	unsigned int suspend_config_reg;
	unsigned int suspend_enable_mask;
	unsigned int suspend_mode_mask;
};

struct rtq2208_rdev_map {
	struct regulator_dev *rdev[RTQ2208_LDO_MAX];
	struct regmap *regmap;
	struct device *dev;
};

/* set Normal Auto/FCCM mode */
static int rtq2208_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	const struct rtq2208_regulator_desc *rdesc =
		(const struct rtq2208_regulator_desc *)rdev->desc;
	unsigned int val, shift;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = RTQ2208_AUTO_MODE;
		break;
	case REGULATOR_MODE_FAST:
		val = RTQ2208_FCCM;
		break;
	default:
		return -EINVAL;
	}

	shift = ffs(rdesc->mode_mask) - 1;
	return regmap_update_bits(rdev->regmap, rdesc->mode_reg,
				  rdesc->mode_mask, val << shift);
}

static unsigned int rtq2208_get_mode(struct regulator_dev *rdev)
{
	const struct rtq2208_regulator_desc *rdesc =
		(const struct rtq2208_regulator_desc *)rdev->desc;
	unsigned int mode_val;
	int ret;

	ret = regmap_read(rdev->regmap, rdesc->mode_reg, &mode_val);
	if (ret)
		return REGULATOR_MODE_INVALID;

	return (mode_val & rdesc->mode_mask) ? REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static int rtq2208_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	const struct regulator_desc *desc = rdev->desc;
	unsigned int sel = 0, val;

	ramp_delay = max(ramp_delay, RTQ2208_RAMP_VALUE_MIN_uV);
	ramp_delay = min(ramp_delay, RTQ2208_RAMP_VALUE_MAX_uV);

	ramp_delay /= RTQ2208_RAMP_VALUE_MIN_uV;

	/*
	 * fls(ramp_delay) - 1: doing LSB shift, let it starts from 0
	 *
	 * RTQ2208_BUCK_RAMP_SEL_MASK - sel: doing descending order shifting.
	 * Because the relation of seleltion and value is like that
	 *
	 * seletion: value
	 * 010: 16mv
	 * ...
	 * 111: 0.5mv
	 *
	 * For example, if I would like to select 16mv, the fls(ramp_delay) - 1 will be 0b010,
	 * and I need to use 0b111 - sel to do the shifting
	 */

	sel = fls(ramp_delay) - 1;
	sel = RTQ2208_BUCK_RAMP_SEL_MASK - sel;

	val = FIELD_PREP(RTQ2208_BUCK_RSPUP_MASK, sel) | FIELD_PREP(RTQ2208_BUCK_RSPDN_MASK, sel);

	return regmap_update_bits(rdev->regmap, desc->ramp_reg,
				  RTQ2208_BUCK_RSPUP_MASK | RTQ2208_BUCK_RSPDN_MASK, val);
}

static int rtq2208_set_suspend_enable(struct regulator_dev *rdev)
{
	const struct rtq2208_regulator_desc *rdesc =
		(const struct rtq2208_regulator_desc *)rdev->desc;

	return regmap_set_bits(rdev->regmap, rdesc->suspend_config_reg, rdesc->suspend_enable_mask);
}

static int rtq2208_set_suspend_disable(struct regulator_dev *rdev)
{
	const struct rtq2208_regulator_desc *rdesc =
		(const struct rtq2208_regulator_desc *)rdev->desc;

	return regmap_update_bits(rdev->regmap, rdesc->suspend_config_reg, rdesc->suspend_enable_mask, 0);
}

static int rtq2208_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	const struct rtq2208_regulator_desc *rdesc =
		(const struct rtq2208_regulator_desc *)rdev->desc;
	unsigned int val, shift;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = RTQ2208_AUTO_MODE;
		break;
	case REGULATOR_MODE_FAST:
		val = RTQ2208_FCCM;
		break;
	default:
		return -EINVAL;
	}

	shift = ffs(rdesc->suspend_mode_mask) - 1;

	return regmap_update_bits(rdev->regmap, rdesc->suspend_config_reg,
			rdesc->suspend_mode_mask, val << shift);
}

static const struct regulator_ops rtq2208_regulator_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_mode = rtq2208_set_mode,
	.get_mode = rtq2208_get_mode,
	.set_ramp_delay = rtq2208_set_ramp_delay,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_suspend_enable = rtq2208_set_suspend_enable,
	.set_suspend_disable = rtq2208_set_suspend_disable,
	.set_suspend_mode = rtq2208_set_suspend_mode,
};

static const struct regulator_ops rtq2208_regulator_ldo_fix_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_suspend_enable = rtq2208_set_suspend_enable,
	.set_suspend_disable = rtq2208_set_suspend_disable,
};

static const struct regulator_ops rtq2208_regulator_ldo_adj_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_suspend_enable = rtq2208_set_suspend_enable,
	.set_suspend_disable = rtq2208_set_suspend_disable,
};

static const unsigned int rtq2208_ldo_volt_table[] = {
	1800000,
	3300000,
};

static unsigned int rtq2208_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RTQ2208_AUTO_MODE:
		return REGULATOR_MODE_NORMAL;
	case RTQ2208_FCCM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int rtq2208_init_irq_mask(struct rtq2208_rdev_map *rdev_map, unsigned int *buck_masks)
{
	unsigned char buck_clr_masks[5] = {0x33, 0x33, 0x33, 0x33, 0x33},
		      sts_clr_masks[2] = {0xE7, 0xF7}, sts_masks[2] = {0xE6, 0xF6};
	int ret;

	/* write clear all buck irq once */
	ret = regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_FLT_RECORDBUCK_CB, buck_clr_masks, 5);
	if (ret)
		return dev_err_probe(rdev_map->dev, ret, "Failed to clr buck irqs\n");

	/* write clear general irq once */
	ret = regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_GLOBAL_INT1, sts_clr_masks, 2);
	if (ret)
		return dev_err_probe(rdev_map->dev, ret, "Failed to clr general irqs\n");

	/* unmask buck ov/uv irq */
	ret = regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_FLT_MASKBUCK_CB, buck_masks, 5);
	if (ret)
		return dev_err_probe(rdev_map->dev, ret, "Failed to unmask buck irqs\n");

	/* unmask needed general irq */
	return regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_GLOBAL_INT1_MASK, sts_masks, 2);
}

static irqreturn_t rtq2208_irq_handler(int irqno, void *devid)
{
	unsigned char buck_flags[RTQ2208_BUCK_NUM_IRQ_REGS], sts_flags[RTQ2208_STS_NUM_IRQ_REGS];
	int ret = 0, i, uv_bit, ov_bit;
	struct rtq2208_rdev_map *rdev_map = devid;
	struct regulator_dev *rdev;

	if (!rdev_map)
		return IRQ_NONE;

	/* read irq event */
	ret = regmap_bulk_read(rdev_map->regmap, RTQ2208_REG_FLT_RECORDBUCK_CB,
				buck_flags, ARRAY_SIZE(buck_flags));
	if (ret)
		return IRQ_NONE;

	ret = regmap_bulk_read(rdev_map->regmap, RTQ2208_REG_GLOBAL_INT1,
				sts_flags, ARRAY_SIZE(sts_flags));
	if (ret)
		return IRQ_NONE;

	/* clear irq event */
	ret = regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_FLT_RECORDBUCK_CB,
				buck_flags, ARRAY_SIZE(buck_flags));
	if (ret)
		return IRQ_NONE;

	ret = regmap_bulk_write(rdev_map->regmap, RTQ2208_REG_GLOBAL_INT1,
				sts_flags, ARRAY_SIZE(sts_flags));
	if (ret)
		return IRQ_NONE;

	for (i = 0; i < RTQ2208_LDO_MAX; i++) {
		if (!rdev_map->rdev[i])
			continue;

		rdev = rdev_map->rdev[i];
		/* uv irq */
		uv_bit = (i & 1) ? 4 : 0;
		if (buck_flags[i >> 1] & (1 << uv_bit))
			regulator_notifier_call_chain(rdev,
					REGULATOR_EVENT_UNDER_VOLTAGE, NULL);
		/* ov irq */
		ov_bit = uv_bit + 1;
		if (buck_flags[i >> 1] & (1 << ov_bit))
			regulator_notifier_call_chain(rdev,
					REGULATOR_EVENT_REGULATION_OUT, NULL);

		/* hd irq */
		if (sts_flags[1] & RTQ2208_HD_INT_MASK)
			regulator_notifier_call_chain(rdev,
					REGULATOR_EVENT_OVER_TEMP, NULL);
	}

	return IRQ_HANDLED;
}

#define BUCK_INFO(_name, _id)						\
{									\
	.name = _name,							\
	.base = RTQ2208_REG_BUCK_##_id##_CFG0,				\
	.enable_reg = BUCK_RG_SHIFT(RTQ2208_REG_BUCK_##_id##_CFG0, 2),	\
	.dis_reg = RTQ2208_REG_BUCK_##_id##_CFG0,			\
}

#define LDO_INFO(_name, _id)						\
{									\
	.name = _name,							\
	.base = RTQ2208_REG_LDO##_id##_CFG,				\
	.enable_reg = RTQ2208_REG_LDO##_id##_CFG,			\
	.dis_mask = RTQ2208_LDO##_id##_DISCHG_EN_MASK,			\
	.dis_on = RTQ2208_LDO##_id##_DISCHG_EN_MASK,			\
	.vsel_mask = RTQ2208_LDO##_id##_VOSEL_SD_MASK,			\
}

#define BUCK_RG_SHIFT(_base, _shift)	(_base + _shift)
#define	VSEL_SHIFT(_sel)	(_sel ? 3 : 1)
#define MTP_SEL_MASK(_sel)	RTQ2208_BUCK_EN_NR_MTP_SEL##_sel##_MASK

static const struct linear_range rtq2208_vout_range[] = {
	REGULATOR_LINEAR_RANGE(400000, 0, 180, 5000),
	REGULATOR_LINEAR_RANGE(1310000, 181, 255, 10000),
};

static void rtq2208_init_regulator_desc(struct rtq2208_regulator_desc *rdesc, int mtp_sel, int idx,
					unsigned int ldo1_fixed, unsigned int ldo2_fixed)
{
	struct regulator_desc *desc;
	unsigned int fixed_uV;
	static const struct {
		char *name;
		int base;
		int enable_reg;
		int dis_reg;
		int dis_mask;
		int dis_on;
		int vsel_mask;
	} regulator_info[] = {
		BUCK_INFO("buck-b", B),
		BUCK_INFO("buck-c", C),
		BUCK_INFO("buck-d", D),
		BUCK_INFO("buck-a", A),
		BUCK_INFO("buck-f", F),
		BUCK_INFO("buck-g", G),
		BUCK_INFO("buck-h", H),
		BUCK_INFO("buck-e", E),
		LDO_INFO("ldo2", 2),
		LDO_INFO("ldo1", 1),
	}, *curr_info;

	curr_info = regulator_info + idx;
	desc = &rdesc->desc;
	desc->name = curr_info->name;
	desc->of_match = of_match_ptr(curr_info->name);
	desc->regulators_node = of_match_ptr("regulators");
	desc->id = idx;
	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->enable_mask = mtp_sel ? MTP_SEL_MASK(1) : MTP_SEL_MASK(0);
	desc->enable_reg = curr_info->enable_reg;
	desc->active_discharge_off = 0;

	rdesc->mode_mask = RTQ2208_BUCK_NRMODE_MASK;

	switch (idx) {
	case RTQ2208_BUCK_B ... RTQ2208_BUCK_E:
		/* init buck desc */
		desc->ops = &rtq2208_regulator_buck_ops;
		desc->vsel_reg = curr_info->base + VSEL_SHIFT(mtp_sel);
		desc->vsel_mask = RTQ2208_BUCK_NR_MTP_SEL_MASK;
		desc->n_voltages = RTQ2208_VOUT_MAXNUM;
		desc->linear_ranges = rtq2208_vout_range;
		desc->n_linear_ranges = ARRAY_SIZE(rtq2208_vout_range);
		desc->ramp_reg = BUCK_RG_SHIFT(curr_info->base, 5);
		desc->of_map_mode = rtq2208_of_map_mode;
		desc->active_discharge_reg = curr_info->dis_reg;
		desc->active_discharge_on = RTQ2208_EN_DIS_MASK;
		desc->active_discharge_mask = RTQ2208_EN_DIS_MASK;

		rdesc->mode_reg = BUCK_RG_SHIFT(curr_info->base, 2);
		rdesc->suspend_config_reg = BUCK_RG_SHIFT(curr_info->base, 4);
		rdesc->suspend_enable_mask = RTQ2208_BUCK_EN_STR_MASK;
		rdesc->suspend_mode_mask = RTQ2208_BUCK_STRMODE_MASK;
		break;
	default:
		fixed_uV = idx == RTQ2208_LDO2 ? ldo2_fixed : ldo1_fixed;
		if (fixed_uV) {
			desc->n_voltages = 1;
			desc->fixed_uV = fixed_uV;
			desc->ops = &rtq2208_regulator_ldo_fix_ops;
		} else {
			desc->n_voltages = ARRAY_SIZE(rtq2208_ldo_volt_table);
			desc->volt_table = rtq2208_ldo_volt_table;
			desc->ops = &rtq2208_regulator_ldo_adj_ops;
		}

		/* init ldo desc */
		desc->active_discharge_reg = RTQ2208_REG_LDO_DVS_CTRL;
		desc->active_discharge_on = curr_info->dis_on;
		desc->active_discharge_mask = curr_info->dis_mask;
		desc->vsel_reg = RTQ2208_REG_LDO_DVS_CTRL;
		desc->vsel_mask = curr_info->vsel_mask;

		rdesc->suspend_config_reg = curr_info->base;
		rdesc->suspend_enable_mask = RTQ2208_LDO_EN_STR_MASK;
		break;
	}
}

static int rtq2208_parse_regulator_dt_data(int n_regulator, const unsigned int *regulator_idx_table,
		struct rtq2208_regulator_desc *rdesc[RTQ2208_LDO_MAX], struct device *dev,
		unsigned int ldo1_fixed, unsigned int ldo2_fixed)
{
	int mtp_sel, i, idx;

	/* get mtp_sel0 or mtp_sel1 */
	mtp_sel = device_property_read_bool(dev, "richtek,mtp-sel-high");

	for (i = 0; i < n_regulator; i++) {
		idx = regulator_idx_table[i];

		rdesc[i] = devm_kcalloc(dev, 1, sizeof(*rdesc[0]), GFP_KERNEL);
		if (!rdesc[i])
			return -ENOMEM;

		rtq2208_init_regulator_desc(rdesc[i], mtp_sel, idx, ldo1_fixed, ldo2_fixed);
	}

	return 0;

}

static int rtq2208_regulator_check(struct device *dev, int *num, int *regulator_idx_table,
				   unsigned int *buck_masks, unsigned int *ldo1_fixed_uV,
				   unsigned int *ldo2_fixed_uV)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);
	bool rtq2208_used_table[RTQ2208_LDO_MAX] = {0};
	u8 entry_key[] = { 0x69, 0x01 };
	unsigned int buck_phase, ldo_cfg0, ldo_cfg1;
	int i, ret;
	u8 mask;

	ret = regmap_raw_write(regmap, RTQ2208_REG_HIDDEN0, entry_key, ARRAY_SIZE(entry_key));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enter hidden page\n");

	ret = regmap_read(regmap, RTQ2208_REG_HIDDEN_BUCKPH, &buck_phase);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read buck phase configuration\n");

	ret = regmap_read(regmap, RTQ2208_REG_HIDDEN_LDOCFG0, &ldo_cfg0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read ldo cfg0\n");

	ret = regmap_read(regmap, RTQ2208_REG_HIDDEN_LDOCFG1, &ldo_cfg1);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read ldo cfg1\n");

	ret = regmap_write(regmap, RTQ2208_REG_HIDDEN1, 0x00);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to exit hidden page\n");

	dev_info(dev, "BUCK Phase 0x%x\n", buck_phase);
	/*
	 * Use buck phase configuration to assign used table mask
	 *                                 GROUP1       GROUP2
	 * 0      -> 2P + 2P                BC           FG
	 * 1      -> 2P + 1P + 1P           BCA          FGE
	 * 2      -> 1P + 1P + 1P + 1P      BCDA         FGHE
	 * 3      -> 3P + 1P                BC           FG
	 * others -> 4P                     C            G
	 */
	switch (FIELD_GET(RTQ2208_MASK_BUCKPH_GROUP1, buck_phase)) {
	case 2:
		rtq2208_used_table[RTQ2208_BUCK_D] = true;
		fallthrough;
	case 1:
		rtq2208_used_table[RTQ2208_BUCK_A] = true;
		fallthrough;
	case 0:
	case 3:
		rtq2208_used_table[RTQ2208_BUCK_B] = true;
		fallthrough;
	default:
		rtq2208_used_table[RTQ2208_BUCK_C] = true;
		break;
	}

	switch (FIELD_GET(RTQ2208_MASK_BUCKPH_GROUP2, buck_phase)) {
	case 2:
		rtq2208_used_table[RTQ2208_BUCK_H] = true;
		fallthrough;
	case 1:
		rtq2208_used_table[RTQ2208_BUCK_E] = true;
		fallthrough;
	case 0:
	case 3:
		rtq2208_used_table[RTQ2208_BUCK_F] = true;
		fallthrough;
	default:
		rtq2208_used_table[RTQ2208_BUCK_G] = true;
		break;
	}

	*ldo1_fixed_uV = FIELD_GET(RTQ2208_MASK_LDO1_FIXED, ldo_cfg1) ? 1200000 : 0;

	if (!FIELD_GET(RTQ2208_MASK_LDO2_OPT0, ldo_cfg0) &&
	    !FIELD_GET(RTQ2208_MASK_LDO2_OPT1, ldo_cfg1))
		*ldo2_fixed_uV = 0;
	else if (FIELD_GET(RTQ2208_MASK_LDO2_OPT1, ldo_cfg1))
		*ldo2_fixed_uV = 900000;
	else
		*ldo2_fixed_uV = 1200000;

	/* By default, LDO1 & LDO2 are always used */
	rtq2208_used_table[RTQ2208_LDO1] = rtq2208_used_table[RTQ2208_LDO2] = true;

	for (i = 0; i < RTQ2208_LDO_MAX; i++) {
		if (!rtq2208_used_table[i])
			continue;

		regulator_idx_table[(*num)++] = i;

		mask = RTQ2208_BUCK_MASK(4 * i, 4 * i + 1);
		buck_masks[i >> 1] &= ~mask;
	}

	return 0;
}

static const struct regmap_config rtq2208_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};

static int rtq2208_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	struct rtq2208_regulator_desc *rdesc[RTQ2208_LDO_MAX];
	struct regulator_dev *rdev;
	struct regulator_config cfg = {};
	struct rtq2208_rdev_map *rdev_map;
	int i, ret = 0, idx, n_regulator = 0;
	unsigned int regulator_idx_table[RTQ2208_LDO_MAX],
		     buck_masks[RTQ2208_BUCK_NUM_IRQ_REGS] = {0x33, 0x33, 0x33, 0x33, 0x33};
	unsigned int ldo1_fixed_uV, ldo2_fixed_uV;

	rdev_map = devm_kzalloc(dev, sizeof(struct rtq2208_rdev_map), GFP_KERNEL);
	if (!rdev_map)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(i2c, &rtq2208_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to allocate regmap\n");

	/* get needed regulator */
	ret = rtq2208_regulator_check(dev, &n_regulator, regulator_idx_table, buck_masks,
				      &ldo1_fixed_uV, &ldo2_fixed_uV);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to check used regulators\n");

	rdev_map->regmap = regmap;
	rdev_map->dev = dev;

	cfg.dev = dev;

	/* init regulator desc */
	ret = rtq2208_parse_regulator_dt_data(n_regulator, regulator_idx_table, rdesc, dev,
					      ldo1_fixed_uV, ldo2_fixed_uV);
	if (ret)
		return ret;

	for (i = 0; i < n_regulator; i++) {
		idx = regulator_idx_table[i];

		/* register regulator */
		rdev = devm_regulator_register(dev, &rdesc[i]->desc, &cfg);
		if (IS_ERR(rdev))
			return PTR_ERR(rdev);

		rdev_map->rdev[idx] = rdev;
	}

	/* init interrupt mask */
	ret = rtq2208_init_irq_mask(rdev_map, buck_masks);
	if (ret)
		return ret;

	/* register interrupt */
	return devm_request_threaded_irq(dev, i2c->irq, NULL, rtq2208_irq_handler,
					IRQF_ONESHOT, dev_name(dev), rdev_map);
}

static const struct of_device_id rtq2208_device_tables[] = {
	{ .compatible = "richtek,rtq2208" },
	{}
};
MODULE_DEVICE_TABLE(of, rtq2208_device_tables);

static struct i2c_driver rtq2208_driver = {
	.driver = {
		.name = "rtq2208",
		.of_match_table = rtq2208_device_tables,
	},
	.probe = rtq2208_probe,
};
module_i2c_driver(rtq2208_driver);

MODULE_AUTHOR("Alina Yu <alina_yu@richtek.com>");
MODULE_DESCRIPTION("Richtek RTQ2208 Regulator Driver");
MODULE_LICENSE("GPL");
