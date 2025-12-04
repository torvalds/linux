// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2024 MediaTek Inc.
// Copyright (c) 2025 Collabora Ltd
//                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6363-regulator.h>
#include <linux/regulator/of_regulator.h>

#define MT6363_REGULATOR_MODE_NORMAL	0
#define MT6363_REGULATOR_MODE_FCCM	1
#define MT6363_REGULATOR_MODE_LP	2
#define MT6363_REGULATOR_MODE_ULP	3

#define EN_SET_OFFSET			0x1
#define EN_CLR_OFFSET			0x2
#define OP_CFG_OFFSET			0x5

#define NORMAL_OP_CFG			0x10
#define NORMAL_OP_EN			0x800000

#define OC_IRQ_ENABLE_DELAY_MS		10

/* Unlock keys for TMA and BUCK_TOP */
#define MT6363_TMA_UNLOCK_VALUE		0x9c9c
#define MT6363_BUCK_TOP_UNLOCK_VALUE	0x5543

enum {
	MT6363_ID_VBUCK1,
	MT6363_ID_VBUCK2,
	MT6363_ID_VBUCK3,
	MT6363_ID_VBUCK4,
	MT6363_ID_VBUCK5,
	MT6363_ID_VBUCK6,
	MT6363_ID_VBUCK7,
	MT6363_ID_VS1,
	MT6363_ID_VS2,
	MT6363_ID_VS3,
	MT6363_ID_VA12_1,
	MT6363_ID_VA12_2,
	MT6363_ID_VA15,
	MT6363_ID_VAUX18,
	MT6363_ID_VCN13,
	MT6363_ID_VCN15,
	MT6363_ID_VEMC,
	MT6363_ID_VIO075,
	MT6363_ID_VIO18,
	MT6363_ID_VM18,
	MT6363_ID_VSRAM_APU,
	MT6363_ID_VSRAM_CPUB,
	MT6363_ID_VSRAM_CPUM,
	MT6363_ID_VSRAM_CPUL,
	MT6363_ID_VSRAM_DIGRF,
	MT6363_ID_VSRAM_MDFE,
	MT6363_ID_VSRAM_MODEM,
	MT6363_ID_VRF09,
	MT6363_ID_VRF12,
	MT6363_ID_VRF13,
	MT6363_ID_VRF18,
	MT6363_ID_VRFIO18,
	MT6363_ID_VTREF18,
	MT6363_ID_VUFS12,
	MT6363_ID_VUFS18,
};

/**
 * struct mt6363_regulator_info - MT6363 regulators information
 * @desc: Regulator description structure
 * @lp_mode_reg: Low Power mode register (normal/idle)
 * @lp_mode_mask: Low Power mode regulator mask
 * @hw_lp_mode_reg: Hardware voted Low Power mode register (normal/idle)
 * @hw_lp_mode_mask: Hardware voted Low Power mode regulator mask
 * @modeset_reg: AUTO/PWM mode register
 * @modeset_mask: AUTO/PWM regulator mask
 * @lp_imax_uA: Maximum load current (microamps), for Low Power mode only
 * @op_en_reg: Operation mode enablement register
 * @orig_op_en: Backup of a regulator's operation mode enablement register
 * @orig_op_cfg: Backup of a regulator's operation mode configuration register
 * @oc_work: Delayed work for enabling overcurrent IRQ
 * @hwirq: PMIC-Internal HW Interrupt for overcurrent event
 * @virq: Mapped Interrupt for overcurrent event
 */
struct mt6363_regulator_info {
	struct regulator_desc desc;
	u16 lp_mode_reg;
	u16 lp_mode_mask;
	u16 hw_lp_mode_reg;
	u16 hw_lp_mode_mask;
	u16 modeset_reg;
	u16 modeset_mask;
	int lp_imax_uA;
	u16 op_en_reg;
	u32 orig_op_en;
	u8 orig_op_cfg;
	struct delayed_work oc_work;
	u8 hwirq;
	int virq;
};

#define MT6363_BUCK(match, vreg, min, max, step, en_reg, lp_reg,	\
		    mset_reg, ocp_intn)					\
[MT6363_ID_##vreg] = {							\
	.desc = {							\
		.name = match,						\
		.supply_name = "vsys-"match,				\
		.of_match = of_match_ptr(match),			\
		.ops = &mt6363_vreg_setclr_ops,				\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6363_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = (max - min) / step + 1,			\
		.min_uV = min,						\
		.uV_step = step,					\
		.enable_reg = en_reg,					\
		.enable_mask = BIT(MT6363_RG_BUCK_##vreg##_EN_BIT),	\
		.vsel_reg = MT6363_RG_BUCK_##vreg##_VOSEL_ADDR,		\
		.vsel_mask = MT6363_RG_BUCK_##vreg##_VOSEL_MASK,	\
		.of_map_mode = mt6363_map_mode,				\
	},								\
	.lp_mode_reg = lp_reg,						\
	.lp_mode_mask = BIT(MT6363_RG_BUCK_##vreg##_LP_BIT),		\
	.hw_lp_mode_reg = MT6363_BUCK_##vreg##_HW_LP_MODE,		\
	.hw_lp_mode_mask = 0xc,						\
	.modeset_reg = mset_reg,					\
	.modeset_mask = BIT(MT6363_RG_##vreg##_FCCM_BIT),		\
	.lp_imax_uA = 100000,						\
	.op_en_reg = MT6363_BUCK_##vreg##_OP_EN_0,			\
	.hwirq = ocp_intn,						\
}

#define MT6363_LDO_LINEAR_OPS(match, vreg, in_sup, vops, min, max,	\
			      step, buck_reg, ocp_intn)			\
[MT6363_ID_##vreg] = {							\
	.desc = {							\
		.name = match,						\
		.supply_name = in_sup,					\
		.of_match = of_match_ptr(match),			\
		.ops = &vops,						\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6363_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = (max - min) / step + 1,			\
		.min_uV = min,						\
		.uV_step = step,					\
		.enable_reg = MT6363_RG_##buck_reg##_EN_ADDR,		\
		.enable_mask = BIT(MT6363_RG_LDO_##vreg##_EN_BIT),	\
		.vsel_reg = MT6363_RG_LDO_##vreg##_VOSEL_ADDR,		\
		.vsel_mask = MT6363_RG_LDO_##vreg##_VOSEL_MASK,		\
		.of_map_mode = mt6363_map_mode,				\
	},								\
	.lp_mode_reg = MT6363_RG_##buck_reg##_LP_ADDR,			\
	.lp_mode_mask = BIT(MT6363_RG_LDO_##vreg##_LP_BIT),		\
	.hw_lp_mode_reg = MT6363_LDO_##vreg##_HW_LP_MODE,		\
	.hw_lp_mode_mask = 0x4,						\
	.hwirq = ocp_intn,						\
}

#define MT6363_LDO_L_SC(match, vreg, inp, min, max, step, buck_reg,	\
			ocp_intn)					\
	MT6363_LDO_LINEAR_OPS(match, vreg, inp, mt6363_vreg_setclr_ops,	\
			      min, max, step, buck_reg, ocp_intn)

#define MT6363_LDO_L(match, vreg, inp, min, max, step, buck_reg,	\
		     ocp_intn)						\
	MT6363_LDO_LINEAR_OPS(match, vreg, inp, mt6363_ldo_linear_ops,	\
			      min, max, step, buck_reg, ocp_intn)

#define MT6363_LDO_LINEAR_CAL_OPS(match, vreg, in_sup, vops, vrnum,	\
				  ocp_intn)				\
[MT6363_ID_##vreg] = {							\
	.desc = {							\
		.name = match,						\
		.supply_name = in_sup,					\
		.of_match = of_match_ptr(match),			\
		.ops = &vops,						\
		.type = REGULATOR_VOLTAGE,				\
		.id = MT6363_ID_##vreg,					\
		.owner = THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_ranges##vrnum) * 11,	\
		.linear_ranges = ldo_volt_ranges##vrnum,		\
		.n_linear_ranges = ARRAY_SIZE(ldo_volt_ranges##vrnum),	\
		.linear_range_selectors_bitfield = ldos_cal_selectors,	\
		.enable_reg = MT6363_RG_LDO_##vreg##_ADDR,		\
		.enable_mask = BIT(MT6363_RG_LDO_##vreg##_EN_BIT),	\
		.vsel_reg = MT6363_RG_##vreg##_VOCAL_ADDR,		\
		.vsel_mask = MT6363_RG_##vreg##_VOCAL_MASK,		\
		.vsel_range_reg = MT6363_RG_##vreg##_VOSEL_ADDR,	\
		.vsel_range_mask = MT6363_RG_##vreg##_VOSEL_MASK,	\
		.of_map_mode = mt6363_map_mode,				\
	},								\
	.lp_mode_reg = MT6363_RG_LDO_##vreg##_ADDR,			\
	.lp_mode_mask = BIT(MT6363_RG_LDO_##vreg##_LP_BIT),		\
	.hw_lp_mode_reg = MT6363_LDO_##vreg##_HW_LP_MODE,		\
	.hw_lp_mode_mask = 0x4,						\
	.lp_imax_uA = 10000,						\
	.op_en_reg = MT6363_LDO_##vreg##_OP_EN0,			\
	.hwirq = ocp_intn,						\
}

#define MT6363_LDO_VT(match, vreg, inp, vranges_num, ocp_intn)		\
	MT6363_LDO_LINEAR_CAL_OPS(match, vreg, inp, mt6363_ldo_vtable_ops,\
				  vranges_num, ocp_intn)

static const unsigned int ldos_cal_selectors[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static const struct linear_range ldo_volt_ranges0[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3400000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3500000, 0, 10, 10000)
};

static const struct linear_range ldo_volt_ranges1[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1810000, 0, 10, 10000)
};

static const struct linear_range ldo_volt_ranges2[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2400000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(3300000, 0, 10, 10000)
};

static const struct linear_range ldo_volt_ranges3[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1200000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1300000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1400000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1600000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(1900000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2000000, 0, 10, 10000),
	REGULATOR_LINEAR_RANGE(2100000, 0, 10, 10000)
};

static const struct linear_range ldo_volt_ranges4[] = {
	REGULATOR_LINEAR_RANGE(550000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(650000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(700000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(750000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(800000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(900000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(950000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1000000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1050000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1100000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1150000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1700000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1750000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1800000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(1850000, 0, 10, 5000)
};

static const struct linear_range ldo_volt_ranges5[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(650000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(700000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(750000, 0, 10, 5000),
	REGULATOR_LINEAR_RANGE(800000, 0, 10, 5000)
};

static int mt6363_vreg_enable_setclr(struct regulator_dev *rdev)
{
	return regmap_write(rdev->regmap, rdev->desc->enable_reg + EN_SET_OFFSET,
			    rdev->desc->enable_mask);
}

static int mt6363_vreg_disable_setclr(struct regulator_dev *rdev)
{
	return regmap_write(rdev->regmap, rdev->desc->enable_reg + EN_CLR_OFFSET,
			    rdev->desc->enable_mask);
}

static inline unsigned int mt6363_map_mode(unsigned int mode)
{
	switch (mode) {
	case MT6363_REGULATOR_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	case MT6363_REGULATOR_MODE_FCCM:
		return REGULATOR_MODE_FAST;
	case MT6363_REGULATOR_MODE_LP:
		return REGULATOR_MODE_IDLE;
	case MT6363_REGULATOR_MODE_ULP:
		return REGULATOR_MODE_STANDBY;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static unsigned int mt6363_regulator_get_mode(struct regulator_dev *rdev)
{
	struct mt6363_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	if (info->modeset_reg) {
		ret = regmap_read(rdev->regmap, info->modeset_reg, &val);
		if (ret) {
			dev_err(&rdev->dev, "Failed to get mt6363 mode: %d\n", ret);
			return ret;
		}

		if (val & info->modeset_mask)
			return REGULATOR_MODE_FAST;
	} else {
		val = 0;
	}

	ret = regmap_read(rdev->regmap, info->hw_lp_mode_reg, &val);
	val &= info->hw_lp_mode_mask;

	if (ret) {
		dev_err(&rdev->dev, "Failed to get lp mode: %d\n", ret);
		return ret;
	}

	if (val)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int mt6363_buck_unlock(struct regmap *map, bool unlock)
{
	u16 buf = unlock ? MT6363_BUCK_TOP_UNLOCK_VALUE : 0;

	return regmap_bulk_write(map, MT6363_BUCK_TOP_KEY_PROT_LO, &buf, sizeof(buf));
}

static int mt6363_regulator_set_mode(struct regulator_dev *rdev,
				     unsigned int mode)
{
	struct mt6363_regulator_info *info = rdev_get_drvdata(rdev);
	struct regmap *regmap = rdev->regmap;
	int cur_mode, ret;

	if (!info->modeset_reg && mode == REGULATOR_MODE_FAST)
		return -EOPNOTSUPP;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		ret = mt6363_buck_unlock(regmap, true);
		if (ret)
			break;

		ret = regmap_set_bits(regmap, info->modeset_reg, info->modeset_mask);

		mt6363_buck_unlock(regmap, false);
		break;
	case REGULATOR_MODE_NORMAL:
		cur_mode = mt6363_regulator_get_mode(rdev);
		if (cur_mode < 0) {
			ret = cur_mode;
			break;
		}

		if (cur_mode == REGULATOR_MODE_FAST) {
			ret = mt6363_buck_unlock(regmap, true);
			if (ret)
				break;

			ret = regmap_clear_bits(regmap, info->modeset_reg, info->modeset_mask);

			mt6363_buck_unlock(regmap, false);
			break;
		} else if (cur_mode == REGULATOR_MODE_IDLE) {
			ret = regmap_clear_bits(regmap, info->lp_mode_reg, info->lp_mode_mask);
			if (ret == 0)
				usleep_range(100, 200);
		} else {
			ret = 0;
		}
		break;
	case REGULATOR_MODE_IDLE:
		ret = regmap_set_bits(regmap, info->lp_mode_reg, info->lp_mode_mask);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret) {
		dev_err(&rdev->dev, "Failed to set mode %u: %d\n", mode, ret);
		return ret;
	}

	return 0;
}

static int mt6363_regulator_set_load(struct regulator_dev *rdev, int load_uA)
{
	struct mt6363_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int opmode_cfg, opmode_en;
	int i, ret;

	if (!info->lp_imax_uA)
		return -EINVAL;

	if (load_uA >= info->lp_imax_uA) {
		ret = mt6363_regulator_set_mode(rdev, REGULATOR_MODE_NORMAL);
		if (ret)
			return ret;

		opmode_cfg = NORMAL_OP_CFG;
		opmode_en = NORMAL_OP_EN;
	} else {
		opmode_cfg = info->orig_op_cfg;
		opmode_en = info->orig_op_en;
	}

	ret = regmap_write(rdev->regmap, info->op_en_reg + OP_CFG_OFFSET, opmode_cfg);
	if (ret)
		return ret;

	for (i = 0; i < 3; i++) {
		ret = regmap_write(rdev->regmap, info->op_en_reg + i,
				   (opmode_en >> (i * 8)) & GENMASK(7, 0));
		if (ret)
			return ret;
	}

	return 0;
}

static int mt6363_vemc_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	const u16 tma_unlock_key = MT6363_TMA_UNLOCK_VALUE;
	const struct regulator_desc *rdesc = rdev->desc;
	struct regmap *regmap = rdev->regmap;
	unsigned int range, val;
	int i, ret;
	u16 mask;

	for (i = 0; i < rdesc->n_linear_ranges; i++) {
		const struct linear_range *r = &rdesc->linear_ranges[i];
		unsigned int voltages_in_range = linear_range_values_in_range(r);

		if (sel < voltages_in_range)
			break;
		sel -= voltages_in_range;
	}

	if (i == rdesc->n_linear_ranges)
		return -EINVAL;

	ret = regmap_read(rdev->regmap, MT6363_TOP_TRAP, &val);
	if (ret)
		return ret;

	if (val > 1)
		return -EINVAL;

	/* Unlock TMA for writing */
	ret = regmap_bulk_write(rdev->regmap, MT6363_TOP_TMA_KEY_L,
				&tma_unlock_key, sizeof(tma_unlock_key));
	if (ret)
		return ret;

	/* If HW trapping value is 1, use VEMC_VOSEL_1 instead of VEMC_VOSEL_0 */
	if (val == 1) {
		mask = MT6363_RG_VEMC_VOSEL_1_MASK;
		sel = FIELD_PREP(MT6363_RG_VEMC_VOSEL_1_MASK, sel);
	} else {
		mask = rdesc->vsel_mask;
	}

	sel <<= ffs(rdesc->vsel_mask) - 1;
	sel += rdesc->linear_ranges[i].min_sel;

	range = rdesc->linear_range_selectors_bitfield[i];
	range <<= ffs(rdesc->vsel_range_mask) - 1;

	/* Write to the vreg calibration register for voltage finetuning */
	ret = regmap_update_bits(regmap, rdesc->vsel_range_reg,
				 rdesc->vsel_range_mask, range);
	if (ret)
		goto lock_tma;

	/* Function must return the result of this write operation */
	ret = regmap_update_bits(regmap, rdesc->vsel_reg, mask, sel);

lock_tma:
	/* Unconditionally re-lock TMA */
	val = 0;
	regmap_bulk_write(rdev->regmap, MT6363_TOP_TMA_KEY_L, &val, 2);

	return ret;
}

static int mt6363_vemc_get_voltage_sel(struct regulator_dev *rdev)
{
	const struct regulator_desc *rdesc = rdev->desc;
	unsigned int vosel, trap, calsel;
	int vcal, vsel, range, ret;

	ret = regmap_read(rdev->regmap, rdesc->vsel_reg, &vosel);
	if (ret)
		return ret;

	ret = regmap_read(rdev->regmap, rdesc->vsel_range_reg, &calsel);
	if (ret)
		return ret;

	calsel &= rdesc->vsel_range_mask;
	for (range = 0; range < rdesc->n_linear_ranges; range++)
		if (rdesc->linear_range_selectors_bitfield[range] != calsel)
			break;

	if (range == rdesc->n_linear_ranges)
		return -EINVAL;

	ret = regmap_read(rdev->regmap, MT6363_TOP_TRAP, &trap);
	if (ret)
		return ret;

	/* If HW trapping value is 1, use VEMC_VOSEL_1 instead of VEMC_VOSEL_0 */
	if (trap > 1)
		return -EINVAL;
	else if (trap == 1)
		vsel = FIELD_GET(MT6363_RG_VEMC_VOSEL_1_MASK, vosel);
	else
		vsel = vosel & rdesc->vsel_mask;

	vcal = linear_range_values_in_range_array(rdesc->linear_ranges, range);

	return vsel + vcal;
}

static int mt6363_va15_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	struct regmap *regmap = rdev->regmap;
	int ret;

	ret = mt6363_buck_unlock(regmap, true);
	if (ret)
		return ret;

	ret = regulator_set_voltage_sel_pickable_regmap(rdev, sel);
	if (ret)
		goto va15_unlock;

	ret = regmap_update_bits(regmap, MT6363_RG_BUCK_EFUSE_RSV1,
				 MT6363_RG_BUCK_EFUSE_RSV1_MASK, sel);
	if (ret)
		goto va15_unlock;

va15_unlock:
	mt6363_buck_unlock(rdev->regmap, false);
	return ret;
}

static void mt6363_oc_irq_enable_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mt6363_regulator_info *info =
		container_of(dwork, struct mt6363_regulator_info, oc_work);

	enable_irq(info->virq);
}

static irqreturn_t mt6363_oc_isr(int irq, void *data)
{
	struct regulator_dev *rdev = (struct regulator_dev *)data;
	struct mt6363_regulator_info *info = rdev_get_drvdata(rdev);

	disable_irq_nosync(info->virq);

	if (regulator_is_enabled_regmap(rdev))
		regulator_notifier_call_chain(rdev, REGULATOR_EVENT_OVER_CURRENT, NULL);

	schedule_delayed_work(&info->oc_work, msecs_to_jiffies(OC_IRQ_ENABLE_DELAY_MS));

	return IRQ_HANDLED;
}

static int mt6363_set_ocp(struct regulator_dev *rdev, int lim, int severity, bool enable)
{
	struct mt6363_regulator_info *info = rdev_get_drvdata(rdev);

	/* MT6363 supports only enabling protection and does not support limits */
	if (lim || severity != REGULATOR_SEVERITY_PROT || !enable)
		return -EOPNOTSUPP;

	/* If there is no OCP interrupt, there's nothing to set */
	if (info->virq <= 0)
		return -EOPNOTSUPP;

	return devm_request_threaded_irq(&rdev->dev, info->virq, NULL,
					 mt6363_oc_isr, IRQF_ONESHOT,
					 info->desc.name, rdev);
}

static const struct regulator_ops mt6363_vreg_setclr_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = mt6363_vreg_enable_setclr,
	.disable = mt6363_vreg_disable_setclr,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6363_regulator_set_mode,
	.get_mode = mt6363_regulator_get_mode,
	.set_load = mt6363_regulator_set_load,
	.set_over_current_protection = mt6363_set_ocp,
};

static const struct regulator_ops mt6363_ldo_linear_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6363_regulator_set_mode,
	.get_mode = mt6363_regulator_get_mode,
	.set_over_current_protection = mt6363_set_ocp,
};

static const struct regulator_ops mt6363_ldo_vtable_ops = {
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.map_voltage = regulator_map_voltage_pickable_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_pickable_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6363_regulator_set_mode,
	.get_mode = mt6363_regulator_get_mode,
	.set_load = mt6363_regulator_set_load,
	.set_over_current_protection = mt6363_set_ocp,
};

static const struct regulator_ops mt6363_ldo_vemc_ops = {
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.map_voltage = regulator_map_voltage_pickable_linear_range,
	.set_voltage_sel = mt6363_vemc_set_voltage_sel,
	.get_voltage_sel = mt6363_vemc_get_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6363_regulator_set_mode,
	.get_mode = mt6363_regulator_get_mode,
	.set_load = mt6363_regulator_set_load,
	.set_over_current_protection = mt6363_set_ocp,
};

static const struct regulator_ops mt6363_ldo_va15_ops = {
	.list_voltage = regulator_list_voltage_pickable_linear_range,
	.map_voltage = regulator_map_voltage_pickable_linear_range,
	.set_voltage_sel = mt6363_va15_set_voltage_sel,
	.get_voltage_sel = regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mt6363_regulator_set_mode,
	.get_mode = mt6363_regulator_get_mode,
	.set_load = mt6363_regulator_set_load,
	.set_over_current_protection = mt6363_set_ocp,
};

/* The array is indexed by id(MT6363_ID_XXX) */
static struct mt6363_regulator_info mt6363_regulators[] = {
	MT6363_BUCK("vbuck1", VBUCK1, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_FCCM_ADDR, 1),
	MT6363_BUCK("vbuck2", VBUCK2, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_FCCM_ADDR, 2),
	MT6363_BUCK("vbuck3", VBUCK3, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_FCCM_ADDR, 3),
	MT6363_BUCK("vbuck4", VBUCK4, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_1_FCCM_ADDR, 4),
	MT6363_BUCK("vbuck5", VBUCK5, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_1_FCCM_ADDR, 5),
	MT6363_BUCK("vbuck6", VBUCK6, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_1_FCCM_ADDR, 6),
	MT6363_BUCK("vbuck7", VBUCK7, 0, 1193750, 6250, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_1_FCCM_ADDR, 7),
	MT6363_BUCK("vs1", VS1, 0, 2200000, 12500, MT6363_RG_BUCK1_EN_ADDR,
		    MT6363_RG_BUCK1_LP_ADDR, MT6363_RG_VS1_FCCM_ADDR, 8),
	MT6363_BUCK("vs2", VS2, 0, 1600000, 12500, MT6363_RG_BUCK0_EN_ADDR,
		    MT6363_RG_BUCK0_LP_ADDR, MT6363_RG_BUCK0_FCCM_ADDR, 0),
	MT6363_BUCK("vs3", VS3, 0, 1193750, 6250, MT6363_RG_BUCK1_EN_ADDR,
		    MT6363_RG_BUCK1_LP_ADDR, MT6363_RG_VS3_FCCM_ADDR, 9),
	MT6363_LDO_VT("va12-1", VA12_1, "vs2-ldo2", 3, 37),
	MT6363_LDO_VT("va12-2", VA12_2, "vs2-ldo2", 3, 38),
	MT6363_LDO_LINEAR_CAL_OPS("va15", VA15, "vs1-ldo1", mt6363_ldo_va15_ops, 3, 39),
	MT6363_LDO_VT("vaux18", VAUX18, "vsys-ldo1", 2, 31),
	MT6363_LDO_VT("vcn13", VCN13, "vs2-ldo2", 1, 17),
	MT6363_LDO_VT("vcn15", VCN15, "vs1-ldo2", 3, 16),
	MT6363_LDO_LINEAR_CAL_OPS("vemc", VEMC, "vsys-ldo1", mt6363_ldo_vemc_ops, 0, 32),
	MT6363_LDO_VT("vio0p75", VIO075, "vs1-ldo1", 5, 36),
	MT6363_LDO_VT("vio18", VIO18, "vs1-ldo2", 3, 35),
	MT6363_LDO_VT("vm18", VM18, "vs1-ldo1", 4, 40),
	MT6363_LDO_L("vsram-apu", VSRAM_APU, "vs3-ldo1", 400000, 1193750, 6250, BUCK1, 30),
	MT6363_LDO_L("vsram-cpub", VSRAM_CPUB, "vs2-ldo1", 400000, 1193750, 6250, BUCK1, 27),
	MT6363_LDO_L("vsram-cpum", VSRAM_CPUM, "vs2-ldo1", 400000, 1193750, 6250, BUCK1, 28),
	MT6363_LDO_L("vsram-cpul", VSRAM_CPUL, "vs2-ldo2", 400000, 1193750, 6250, BUCK1, 29),
	MT6363_LDO_L_SC("vsram-digrf", VSRAM_DIGRF, "vs3-ldo1", 400000, 1193750, 6250, BUCK1, 23),
	MT6363_LDO_L_SC("vsram-mdfe", VSRAM_MDFE, "vs3-ldo1", 400000, 1193750, 6250, BUCK1, 24),
	MT6363_LDO_L_SC("vsram-modem", VSRAM_MODEM, "vs3-ldo2", 400000, 1193750, 6250, BUCK1, 25),
	MT6363_LDO_VT("vrf0p9", VRF09, "vs3-ldo2", 1, 18),
	MT6363_LDO_VT("vrf12", VRF12, "vs2-ldo1", 3, 19),
	MT6363_LDO_VT("vrf13", VRF13, "vs2-ldo1", 1, 20),
	MT6363_LDO_VT("vrf18", VRF18, "vs1-ldo1", 3, 21),
	MT6363_LDO_VT("vrf-io18", VRFIO18, "vs1-ldo1", 3, 22),
	MT6363_LDO_VT("vtref18", VTREF18, "vsys-ldo1", 2, 26),
	MT6363_LDO_VT("vufs12", VUFS12, "vs2-ldo1", 4, 33),
	MT6363_LDO_VT("vufs18", VUFS18, "vs1-ldo2", 3, 34),
};

static int mt6363_backup_op_setting(struct regmap *map, struct mt6363_regulator_info *info)
{
	unsigned int i, val;
	int ret;

	ret = regmap_read(map, info->op_en_reg + OP_CFG_OFFSET, &val);
	if (ret)
		return ret;

	info->orig_op_cfg = val;

	for (i = 0; i < 3; i++) {
		ret = regmap_read(map, info->op_en_reg + i, &val);
		if (ret)
			return ret;

		info->orig_op_en |= val << (i * 8);
	}

	return 0;
}

static void mt6363_irq_remove(void *data)
{
	int *virq = data;

	irq_dispose_mapping(*virq);
}

static void mt6363_spmi_remove(void *data)
{
	struct spmi_device *sdev = data;

	spmi_device_remove(sdev);
};

static struct regmap *mt6363_spmi_register_regmap(struct device *dev)
{
	struct regmap_config mt6363_regmap_config = {
		.reg_bits = 16,
		.val_bits = 16,
		.max_register = 0x1f90,
		.fast_io = true,
	};
	struct spmi_device *sdev, *sparent;
	u32 base;
	int ret;

	if (!dev->parent)
		return ERR_PTR(-ENODEV);

	ret = device_property_read_u32(dev, "reg", &base);
	if (ret)
		return ERR_PTR(ret);

	sparent = to_spmi_device(dev->parent);
	if (!sparent)
		return ERR_PTR(-ENODEV);

	sdev = spmi_device_alloc(sparent->ctrl);
	if (!sdev)
		return ERR_PTR(-ENODEV);

	sdev->usid = sparent->usid;
	dev_set_name(&sdev->dev, "%d-%02x-regulator", sdev->ctrl->nr, sdev->usid);
	ret = device_add(&sdev->dev);
	if (ret) {
		put_device(&sdev->dev);
		return ERR_PTR(ret);
	};

	ret = devm_add_action_or_reset(dev, mt6363_spmi_remove, sdev);
	if (ret)
		return ERR_PTR(ret);

	mt6363_regmap_config.reg_base = base;

	return devm_regmap_init_spmi_ext(sdev, &mt6363_regmap_config);
}

static int mt6363_regulator_probe(struct platform_device *pdev)
{
	struct device_node *interrupt_parent;
	struct regulator_config config = {};
	struct mt6363_regulator_info *info;
	struct device *dev = &pdev->dev;
	struct regulator_dev *rdev;
	struct irq_domain *domain;
	struct irq_fwspec fwspec;
	struct spmi_device *sdev;
	int i, ret;

	config.regmap = mt6363_spmi_register_regmap(dev);
	if (IS_ERR(config.regmap))
		return dev_err_probe(dev, PTR_ERR(config.regmap),
				     "Cannot get regmap\n");
	config.dev = dev;
	sdev = to_spmi_device(dev->parent);

	interrupt_parent = of_irq_find_parent(dev->of_node);
	if (!interrupt_parent)
		return dev_err_probe(dev, -EINVAL, "Cannot find IRQ parent\n");

	domain = irq_find_host(interrupt_parent);
	of_node_put(interrupt_parent);
	fwspec.fwnode = domain->fwnode;

	fwspec.param_count = 3;
	fwspec.param[0] = sdev->usid;
	fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;

	for (i = 0; i < ARRAY_SIZE(mt6363_regulators); i++) {
		info = &mt6363_regulators[i];

		fwspec.param[1] = info->hwirq;
		info->virq = irq_create_fwspec_mapping(&fwspec);
		if (!info->virq)
			return dev_err_probe(dev, -EINVAL,
					     "Failed to map IRQ%d\n", info->hwirq);

		ret = devm_add_action_or_reset(dev, mt6363_irq_remove, &info->virq);
		if (ret) {
			irq_dispose_mapping(info->hwirq);
			return ret;
		}

		config.driver_data = info;
		INIT_DELAYED_WORK(&info->oc_work, mt6363_oc_irq_enable_work);

		rdev = devm_regulator_register(dev, &info->desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "failed to register %s\n", info->desc.name);

		if (info->lp_imax_uA) {
			ret = mt6363_backup_op_setting(config.regmap, info);
			if (ret) {
				dev_warn(dev, "Failed to backup op_setting for %s\n",
					 info->desc.name);
				info->lp_imax_uA = 0;
			}
		}
	}

	return 0;
}

static const struct of_device_id mt6363_regulator_match[] = {
	{ .compatible = "mediatek,mt6363-regulator" },
	{ /* sentinel */ }
};

static struct platform_driver mt6363_regulator_driver = {
	.driver = {
		.name = "mt6363-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = mt6363_regulator_match,
	},
	.probe = mt6363_regulator_probe,
};
module_platform_driver(mt6363_regulator_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6363 PMIC");
MODULE_LICENSE("GPL");
