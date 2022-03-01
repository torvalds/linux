// SPDX-License-Identifier: GPL-2.0
/*
 * Intel BXT WhiskeyCove PMIC operation region driver
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include "intel_pmic.h"

#define WHISKEY_COVE_ALRT_HIGH_BIT_MASK 0x0F
#define WHISKEY_COVE_ADC_HIGH_BIT(x)	(((x & 0x0F) << 8))
#define WHISKEY_COVE_ADC_CURSRC(x)	(((x & 0xF0) >> 4))
#define VR_MODE_DISABLED        0
#define VR_MODE_AUTO            BIT(0)
#define VR_MODE_NORMAL          BIT(1)
#define VR_MODE_SWITCH          BIT(2)
#define VR_MODE_ECO             (BIT(0)|BIT(1))
#define VSWITCH2_OUTPUT         BIT(5)
#define VSWITCH1_OUTPUT         BIT(4)
#define VUSBPHY_CHARGE          BIT(1)

static struct pmic_table power_table[] = {
	{
		.address = 0x0,
		.reg = 0x63,
		.bit = VR_MODE_AUTO,
	}, /* VDD1 -> VDD1CNT */
	{
		.address = 0x04,
		.reg = 0x65,
		.bit = VR_MODE_AUTO,
	}, /* VDD2 -> VDD2CNT */
	{
		.address = 0x08,
		.reg = 0x67,
		.bit = VR_MODE_AUTO,
	}, /* VDD3 -> VDD3CNT */
	{
		.address = 0x0c,
		.reg = 0x6d,
		.bit = VR_MODE_AUTO,
	}, /* VLFX -> VFLEXCNT */
	{
		.address = 0x10,
		.reg = 0x6f,
		.bit = VR_MODE_NORMAL,
	}, /* VP1A -> VPROG1ACNT */
	{
		.address = 0x14,
		.reg = 0x70,
		.bit = VR_MODE_NORMAL,
	}, /* VP1B -> VPROG1BCNT */
	{
		.address = 0x18,
		.reg = 0x71,
		.bit = VR_MODE_NORMAL,
	}, /* VP1C -> VPROG1CCNT */
	{
		.address = 0x1c,
		.reg = 0x72,
		.bit = VR_MODE_NORMAL,
	}, /* VP1D -> VPROG1DCNT */
	{
		.address = 0x20,
		.reg = 0x73,
		.bit = VR_MODE_NORMAL,
	}, /* VP2A -> VPROG2ACNT */
	{
		.address = 0x24,
		.reg = 0x74,
		.bit = VR_MODE_NORMAL,
	}, /* VP2B -> VPROG2BCNT */
	{
		.address = 0x28,
		.reg = 0x75,
		.bit = VR_MODE_NORMAL,
	}, /* VP2C -> VPROG2CCNT */
	{
		.address = 0x2c,
		.reg = 0x76,
		.bit = VR_MODE_NORMAL,
	}, /* VP3A -> VPROG3ACNT */
	{
		.address = 0x30,
		.reg = 0x77,
		.bit = VR_MODE_NORMAL,
	}, /* VP3B -> VPROG3BCNT */
	{
		.address = 0x34,
		.reg = 0x78,
		.bit = VSWITCH2_OUTPUT,
	}, /* VSW2 -> VLD0CNT Bit 5*/
	{
		.address = 0x38,
		.reg = 0x78,
		.bit = VSWITCH1_OUTPUT,
	}, /* VSW1 -> VLD0CNT Bit 4 */
	{
		.address = 0x3c,
		.reg = 0x78,
		.bit = VUSBPHY_CHARGE,
	}, /* VUPY -> VLDOCNT Bit 1 */
	{
		.address = 0x40,
		.reg = 0x7b,
		.bit = VR_MODE_NORMAL,
	}, /* VRSO -> VREFSOCCNT*/
	{
		.address = 0x44,
		.reg = 0xA0,
		.bit = VR_MODE_NORMAL,
	}, /* VP1E -> VPROG1ECNT */
	{
		.address = 0x48,
		.reg = 0xA1,
		.bit = VR_MODE_NORMAL,
	}, /* VP1F -> VPROG1FCNT */
	{
		.address = 0x4c,
		.reg = 0xA2,
		.bit = VR_MODE_NORMAL,
	}, /* VP2D -> VPROG2DCNT */
	{
		.address = 0x50,
		.reg = 0xA3,
		.bit = VR_MODE_NORMAL,
	}, /* VP4A -> VPROG4ACNT */
	{
		.address = 0x54,
		.reg = 0xA4,
		.bit = VR_MODE_NORMAL,
	}, /* VP4B -> VPROG4BCNT */
	{
		.address = 0x58,
		.reg = 0xA5,
		.bit = VR_MODE_NORMAL,
	}, /* VP4C -> VPROG4CCNT */
	{
		.address = 0x5c,
		.reg = 0xA6,
		.bit = VR_MODE_NORMAL,
	}, /* VP4D -> VPROG4DCNT */
	{
		.address = 0x60,
		.reg = 0xA7,
		.bit = VR_MODE_NORMAL,
	}, /* VP5A -> VPROG5ACNT */
	{
		.address = 0x64,
		.reg = 0xA8,
		.bit = VR_MODE_NORMAL,
	}, /* VP5B -> VPROG5BCNT */
	{
		.address = 0x68,
		.reg = 0xA9,
		.bit = VR_MODE_NORMAL,
	}, /* VP6A -> VPROG6ACNT */
	{
		.address = 0x6c,
		.reg = 0xAA,
		.bit = VR_MODE_NORMAL,
	}, /* VP6B -> VPROG6BCNT */
	{
		.address = 0x70,
		.reg = 0x36,
		.bit = BIT(2),
	}, /* SDWN_N -> MODEMCTRL Bit 2 */
	{
		.address = 0x74,
		.reg = 0x36,
		.bit = BIT(0),
	} /* MOFF -> MODEMCTRL Bit 0 */
};

static struct pmic_table thermal_table[] = {
	{
		.address = 0x00,
		.reg = 0x4F39
	},
	{
		.address = 0x04,
		.reg = 0x4F24
	},
	{
		.address = 0x08,
		.reg = 0x4F26
	},
	{
		.address = 0x0c,
		.reg = 0x4F3B
	},
	{
		.address = 0x10,
		.reg = 0x4F28
	},
	{
		.address = 0x14,
		.reg = 0x4F2A
	},
	{
		.address = 0x18,
		.reg = 0x4F3D
	},
	{
		.address = 0x1c,
		.reg = 0x4F2C
	},
	{
		.address = 0x20,
		.reg = 0x4F2E
	},
	{
		.address = 0x24,
		.reg = 0x4F3F
	},
	{
		.address = 0x28,
		.reg = 0x4F30
	},
	{
		.address = 0x30,
		.reg = 0x4F41
	},
	{
		.address = 0x34,
		.reg = 0x4F32
	},
	{
		.address = 0x3c,
		.reg = 0x4F43
	},
	{
		.address = 0x40,
		.reg = 0x4F34
	},
	{
		.address = 0x48,
		.reg = 0x4F6A,
		.bit = 0,
	},
	{
		.address = 0x4C,
		.reg = 0x4F6A,
		.bit = 1
	},
	{
		.address = 0x50,
		.reg = 0x4F6A,
		.bit = 2
	},
	{
		.address = 0x54,
		.reg = 0x4F6A,
		.bit = 4
	},
	{
		.address = 0x58,
		.reg = 0x4F6A,
		.bit = 5
	},
	{
		.address = 0x5C,
		.reg = 0x4F6A,
		.bit = 3
	},
};

static int intel_bxtwc_pmic_get_power(struct regmap *regmap, int reg,
		int bit, u64 *value)
{
	int data;

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	*value = (data & bit) ? 1 : 0;
	return 0;
}

static int intel_bxtwc_pmic_update_power(struct regmap *regmap, int reg,
		int bit, bool on)
{
	u8 val, mask = bit;

	if (on)
		val = 0xFF;
	else
		val = 0x0;

	return regmap_update_bits(regmap, reg, mask, val);
}

static int intel_bxtwc_pmic_get_raw_temp(struct regmap *regmap, int reg)
{
	unsigned int val, adc_val, reg_val;
	u8 temp_l, temp_h, cursrc;
	unsigned long rlsb;
	static const unsigned long rlsb_array[] = {
		0, 260420, 130210, 65100, 32550, 16280,
		8140, 4070, 2030, 0, 260420, 130210 };

	if (regmap_read(regmap, reg, &val))
		return -EIO;
	temp_l = (u8) val;

	if (regmap_read(regmap, (reg - 1), &val))
		return -EIO;
	temp_h = (u8) val;

	reg_val = temp_l | WHISKEY_COVE_ADC_HIGH_BIT(temp_h);
	cursrc = WHISKEY_COVE_ADC_CURSRC(temp_h);
	rlsb = rlsb_array[cursrc];
	adc_val = reg_val * rlsb / 1000;

	return adc_val;
}

static int
intel_bxtwc_pmic_update_aux(struct regmap *regmap, int reg, int raw)
{
	u32 bsr_num;
	u16 resi_val, count = 0, thrsh = 0;
	u8 alrt_h, alrt_l, cursel = 0;

	bsr_num = raw;
	bsr_num /= (1 << 5);

	count = fls(bsr_num) - 1;

	cursel = clamp_t(s8, (count - 7), 0, 7);
	thrsh = raw / (1 << (4 + cursel));

	resi_val = (cursel << 9) | thrsh;
	alrt_h = (resi_val >> 8) & WHISKEY_COVE_ALRT_HIGH_BIT_MASK;
	if (regmap_update_bits(regmap,
				reg - 1,
				WHISKEY_COVE_ALRT_HIGH_BIT_MASK,
				alrt_h))
		return -EIO;

	alrt_l = (u8)resi_val;
	return regmap_write(regmap, reg, alrt_l);
}

static int
intel_bxtwc_pmic_get_policy(struct regmap *regmap, int reg, int bit, u64 *value)
{
	u8 mask = BIT(bit);
	unsigned int val;

	if (regmap_read(regmap, reg, &val))
		return -EIO;

	*value = (val & mask) >> bit;
	return 0;
}

static int
intel_bxtwc_pmic_update_policy(struct regmap *regmap,
				int reg, int bit, int enable)
{
	u8 mask = BIT(bit), val = enable << bit;

	return regmap_update_bits(regmap, reg, mask, val);
}

static const struct intel_pmic_opregion_data intel_bxtwc_pmic_opregion_data = {
	.get_power      = intel_bxtwc_pmic_get_power,
	.update_power   = intel_bxtwc_pmic_update_power,
	.get_raw_temp   = intel_bxtwc_pmic_get_raw_temp,
	.update_aux     = intel_bxtwc_pmic_update_aux,
	.get_policy     = intel_bxtwc_pmic_get_policy,
	.update_policy  = intel_bxtwc_pmic_update_policy,
	.lpat_raw_to_temp = acpi_lpat_raw_to_temp,
	.power_table      = power_table,
	.power_table_count = ARRAY_SIZE(power_table),
	.thermal_table     = thermal_table,
	.thermal_table_count = ARRAY_SIZE(thermal_table),
};

static int intel_bxtwc_pmic_opregion_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);

	return intel_pmic_install_opregion_handler(&pdev->dev,
			ACPI_HANDLE(pdev->dev.parent),
			pmic->regmap,
			&intel_bxtwc_pmic_opregion_data);
}

static const struct platform_device_id bxt_wc_opregion_id_table[] = {
	{ .name = "bxt_wcove_region" },
	{},
};

static struct platform_driver intel_bxtwc_pmic_opregion_driver = {
	.probe = intel_bxtwc_pmic_opregion_probe,
	.driver = {
		.name = "bxt_whiskey_cove_pmic",
	},
	.id_table = bxt_wc_opregion_id_table,
};
builtin_platform_driver(intel_bxtwc_pmic_opregion_driver);
