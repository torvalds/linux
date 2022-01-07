// SPDX-License-Identifier: GPL-2.0
/*
 * MFD core driver for Rockchip RK806
 *
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rk806.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>

#define TSD_TEMP_140	0x00
#define TSD_TEMP_160	0x01
#define VB_LO_ACT_SD	0x00
#define VB_LO_ACT_INT	0x01

static const struct reg_field rk806_reg_fields[] = {
	[POWER_EN0] = REG_FIELD(0x00, 0, 7),
	[POWER_EN1] = REG_FIELD(0x01, 0, 7),
	[POWER_EN2] = REG_FIELD(0x02, 0, 7),
	[POWER_EN3] = REG_FIELD(0x03, 0, 7),
	[POWER_EN4] = REG_FIELD(0x04, 0, 7),
	[POWER_EN5] = REG_FIELD(0x05, 0, 7),

	[BUCK4_EN_MASK] = REG_FIELD(0x00, 7, 7),
	[BUCK3_EN_MASK] = REG_FIELD(0x00, 6, 6),
	[BUCK2_EN_MASK] = REG_FIELD(0x00, 5, 5),
	[BUCK1_EN_MASK] = REG_FIELD(0x00, 4, 4),
	[BUCK4_EN] = REG_FIELD(0X00, 3, 3),
	[BUCK3_EN] = REG_FIELD(0X00, 2, 2),
	[BUCK2_EN] = REG_FIELD(0X00, 1, 1),
	[BUCK1_EN] = REG_FIELD(0X00, 0, 0),
	[BUCK8_EN_MASK] = REG_FIELD(0x01, 7, 7),
	[BUCK7_EN_MASK] = REG_FIELD(0x01, 6, 6),
	[BUCK6_EN_MASK] = REG_FIELD(0x01, 5, 5),
	[BUCK5_EN_MASK] = REG_FIELD(0x01, 4, 4),
	[BUCK8_EN] = REG_FIELD(0x01, 3, 3),
	[BUCK7_EN] = REG_FIELD(0x01, 2, 2),
	[BUCK6_EN] = REG_FIELD(0x01, 1, 1),
	[BUCK5_EN] = REG_FIELD(0x01, 0, 0),
	[BUCK10_EN_MASK] = REG_FIELD(0x02, 5, 5),
	[BUCK9_EN_MASK] = REG_FIELD(0x02, 4, 4),
	[BUCK10_EN] = REG_FIELD(0x02, 1, 1),
	[BUCK9_EN] = REG_FIELD(0x02, 0, 0),
	[NLDO4_EN_MASK] = REG_FIELD(0x03, 7, 7),
	[NLDO3_EN_MASK] = REG_FIELD(0x03, 6, 6),
	[NLDO2_EN_MASK] = REG_FIELD(0x03, 5, 5),
	[NLDO1_EN_MASK] = REG_FIELD(0x03, 4, 4),
	[NLDO4_EN] = REG_FIELD(0x03, 3, 3),
	[NLDO3_EN] = REG_FIELD(0x03, 2, 2),
	[NLDO2_EN] = REG_FIELD(0x03, 1, 1),
	[NLDO1_EN] = REG_FIELD(0x03, 0, 0),

	[PLDO3_EN_MASK] = REG_FIELD(0x04, 7, 7),
	[PLDO2_EN_MASK] = REG_FIELD(0x04, 6, 6),
	[PLDO1_EN_MASK] = REG_FIELD(0x04, 5, 5),
	[PLDO6_EN_MASK] = REG_FIELD(0x04, 4, 4),
	[PLDO3_EN] = REG_FIELD(0x04, 3, 3),
	[PLDO2_EN] = REG_FIELD(0x04, 2, 2),
	[PLDO1_EN] = REG_FIELD(0x04, 1, 1),
	[PLDO6_EN] = REG_FIELD(0x04, 0, 0),

	[NLDO5_EN_MASK] = REG_FIELD(0x05, 6, 6),
	[PLDO5_EN_MASK] = REG_FIELD(0x05, 5, 5),
	[PLDO4_EN_MASK] = REG_FIELD(0x05, 4, 4),
	[NLDO5_EN] = REG_FIELD(0x05, 2, 2),
	[PLDO5_EN] = REG_FIELD(0x05, 1, 1),
	[PLDO4_EN] = REG_FIELD(0x05, 0, 0),

	[BUCK8_SLP_EN] = REG_FIELD(0x06, 7, 7),
	[BUCK7_SLP_EN] = REG_FIELD(0x06, 6, 6),
	[BUCK6_SLP_EN] = REG_FIELD(0x06, 5, 5),
	[BUCK5_SLP_EN] = REG_FIELD(0x06, 4, 4),
	[BUCK4_SLP_EN] = REG_FIELD(0x06, 3, 3),
	[BUCK3_SLP_EN] = REG_FIELD(0x06, 2, 2),
	[BUCK2_SLP_EN] = REG_FIELD(0x06, 1, 1),
	[BUCK1_SLP_EN] = REG_FIELD(0x06, 0, 0),

	[BUCK10_SLP_EN] = REG_FIELD(0x07, 7, 7),
	[BUCK9_SLP_EN] = REG_FIELD(0x07, 6, 6),
	[NLDO5_SLP_EN] = REG_FIELD(0x07, 4, 4),
	[NLDO4_SLP_EN] = REG_FIELD(0x07, 3, 3),
	[NLDO3_SLP_EN] = REG_FIELD(0x07, 2, 2),
	[NLDO2_SLP_EN] = REG_FIELD(0x07, 1, 1),
	[NLDO1_SLP_EN] = REG_FIELD(0x07, 0, 0),

	[PLDO5_SLP_EN] = REG_FIELD(0x08, 5, 5),
	[PLDO4_SLP_EN] = REG_FIELD(0x08, 4, 4),
	[PLDO3_SLP_EN] = REG_FIELD(0x08, 3, 3),
	[PLDO2_SLP_EN] = REG_FIELD(0x08, 2, 2),
	[PLDO1_SLP_EN] = REG_FIELD(0x08, 1, 1),
	[PLDO6_SLP_EN] = REG_FIELD(0x08, 0, 0),

	[BUCK1_RATE] = REG_FIELD(0x10, 6, 7),
	[BUCK2_RATE] = REG_FIELD(0x11, 6, 7),
	[BUCK3_RATE] = REG_FIELD(0x12, 6, 7),
	[BUCK4_RATE] = REG_FIELD(0x13, 6, 7),
	[BUCK5_RATE] = REG_FIELD(0x14, 6, 7),
	[BUCK6_RATE] = REG_FIELD(0x15, 6, 7),
	[BUCK7_RATE] = REG_FIELD(0x16, 6, 7),
	[BUCK8_RATE] = REG_FIELD(0x17, 6, 7),
	[BUCK9_RATE] = REG_FIELD(0x18, 6, 7),
	[BUCK10_RATE] = REG_FIELD(0x19, 6, 7),

	[BUCK1_ON_VSEL] = REG_FIELD(0x1A, 0, 7),
	[BUCK2_ON_VSEL] = REG_FIELD(0x1B, 0, 7),
	[BUCK3_ON_VSEL] = REG_FIELD(0x1C, 0, 7),
	[BUCK4_ON_VSEL] = REG_FIELD(0x1D, 0, 7),
	[BUCK5_ON_VSEL] = REG_FIELD(0x1E, 0, 7),
	[BUCK6_ON_VSEL] = REG_FIELD(0x1F, 0, 7),
	[BUCK7_ON_VSEL] = REG_FIELD(0x20, 0, 7),
	[BUCK8_ON_VSEL] = REG_FIELD(0x21, 0, 7),
	[BUCK9_ON_VSEL] = REG_FIELD(0x22, 0, 7),
	[BUCK10_ON_VSEL] = REG_FIELD(0x23, 0, 7),

	[BUCK1_SLP_VSEL] = REG_FIELD(0x24, 0, 7),
	[BUCK2_SLP_VSEL] = REG_FIELD(0x25, 0, 7),
	[BUCK3_SLP_VSEL] = REG_FIELD(0x26, 0, 7),
	[BUCK4_SLP_VSEL] = REG_FIELD(0x27, 0, 7),
	[BUCK5_SLP_VSEL] = REG_FIELD(0x28, 0, 7),
	[BUCK6_SLP_VSEL] = REG_FIELD(0x29, 0, 7),
	[BUCK7_SLP_VSEL] = REG_FIELD(0x2A, 0, 7),
	[BUCK8_SLP_VSEL] = REG_FIELD(0x2B, 0, 7),
	[BUCK9_SLP_VSEL] = REG_FIELD(0x2C, 0, 7),
	[BUCK10_SLP_VSEL] = REG_FIELD(0x2D, 0, 7),

	[NLDO1_ON_VSEL] = REG_FIELD(0x43, 0, 7),
	[NLDO2_ON_VSEL] = REG_FIELD(0x44, 0, 7),
	[NLDO3_ON_VSEL] = REG_FIELD(0x45, 0, 7),
	[NLDO4_ON_VSEL] = REG_FIELD(0x46, 0, 7),
	[NLDO5_ON_VSEL] = REG_FIELD(0x47, 0, 7),
	[NLDO1_SLP_VSEL] = REG_FIELD(0x48, 0, 7),
	[NLDO2_SLP_VSEL] = REG_FIELD(0x49, 0, 7),
	[NLDO3_SLP_VSEL] = REG_FIELD(0x4A, 0, 7),
	[NLDO4_SLP_VSEL] = REG_FIELD(0x4B, 0, 7),
	[NLDO5_SLP_VSEL] = REG_FIELD(0x4C, 0, 7),

	[PLDO1_ON_VSEL] = REG_FIELD(0x4E, 0, 7),
	[PLDO2_ON_VSEL] = REG_FIELD(0x4F, 0, 7),
	[PLDO3_ON_VSEL] = REG_FIELD(0x50, 0, 7),
	[PLDO4_ON_VSEL] = REG_FIELD(0x51, 0, 7),
	[PLDO5_ON_VSEL] = REG_FIELD(0x52, 0, 7),
	[PLDO6_ON_VSEL] = REG_FIELD(0x53, 0, 7),

	[PLDO1_SLP_VSEL] = REG_FIELD(0x54, 0, 7),
	[PLDO2_SLP_VSEL] = REG_FIELD(0x55, 0, 7),
	[PLDO3_SLP_VSEL] = REG_FIELD(0x56, 0, 7),
	[PLDO4_SLP_VSEL] = REG_FIELD(0x57, 0, 7),
	[PLDO5_SLP_VSEL] = REG_FIELD(0x58, 0, 7),
	[PLDO6_SLP_VSEL] = REG_FIELD(0x59, 0, 7),

	[CHIP_NAME_H] = REG_FIELD(0x5A, 0, 7),
	[CHIP_NAME_L] = REG_FIELD(0x5B, 4, 7),
	[CHIP_VER] = REG_FIELD(0x5B, 0, 3),
	[OTP_VER] = REG_FIELD(0x5C, 0, 3),
	/* SYS_STS */
	[PWRON_STS] = REG_FIELD(0x5D, 7, 7),
	[VDC_STS] = REG_FIELD(0x5D, 6, 6),
	[VB_UV_STSS] = REG_FIELD(0x5D, 5, 5),
	[VB_LO_STS] = REG_FIELD(0x5D, 4, 4),
	[HOTDIE_STS] = REG_FIELD(0x5D, 3, 3),
	[TSD_STS] = REG_FIELD(0x5D, 2, 2),
	[VB_OV_STS] = REG_FIELD(0x5D, 0, 0),
	/* SYS_CFG0 */
	[VB_UV_DLY] = REG_FIELD(0x5E, 7, 7),
	[VB_UV_SEL] = REG_FIELD(0x5E, 4, 6),
	[VB_LO_ACT] = REG_FIELD(0x5E, 3, 3),
	[VB_LO_SEL] = REG_FIELD(0x5E, 0, 2),
	/* SYS_CFG1 */
	[ABNORDET_EN] = REG_FIELD(0x5F, 7, 7),
	[TSD_TEMP] = REG_FIELD(0x5F, 6, 6),
	[HOTDIE_TMP] = REG_FIELD(0x5F, 4, 5),
	[SYS_OV_SD_EN] = REG_FIELD(0x5F, 3, 3),
	[SYS_OV_SD_DLY_SEL] = REG_FIELD(0x5F, 2, 2),
	[DLY_ABN_SHORT] = REG_FIELD(0x5F, 0, 1),
	/* SYS_OPTION */
	[VCCXDET_DIS] = REG_FIELD(0x61, 4, 5),
	[OSC_TC] = REG_FIELD(0x61, 2, 3),
	[ENB2_2M] = REG_FIELD(0x61, 1, 1),
	[ENB_32K] = REG_FIELD(0x61, 0, 0),
	/* SLEEP_CONFIG0 */
	[PWRCTRL2_POL] = REG_FIELD(0x62, 7, 7),
	[PWRCTRL2_FUN] = REG_FIELD(0x62, 4, 6),
	[PWRCTRL1_POL] = REG_FIELD(0x62, 3, 3),
	[PWRCTRL1_FUN] = REG_FIELD(0x62, 0, 2),
	/* SLEEP_CONFIG1 */
	[PWRCTRL3_POL] = REG_FIELD(0x63, 3, 3),
	[PWRCTRL3_FUN] = REG_FIELD(0x63, 0, 2),
	/* SLEEP_VSEL_CTR_SEL0 */
	[BUCK4_VSEL_CTR_SEL] = REG_FIELD(0x65, 4, 5),
	[BUCK3_VSEL_CTR_SEL] = REG_FIELD(0x65, 0, 1),
	[BUCK2_VSEL_CTR_SEL] = REG_FIELD(0x64, 4, 5),
	[BUCK1_VSEL_CTR_SEL] = REG_FIELD(0x64, 0, 1),
	/* SLEEP_VSEL_CTR_SEL1 */
	[BUCK8_VSEL_CTR_SEL] = REG_FIELD(0x67, 4, 5),
	[BUCK7_VSEL_CTR_SEL] = REG_FIELD(0x67, 0, 1),
	[BUCK6_VSEL_CTR_SEL] = REG_FIELD(0x66, 4, 5),
	[BUCK5_VSEL_CTR_SEL] = REG_FIELD(0x66, 0, 1),
	/* SLEEP_VSEL_CTR_SEL2 */
	[NLDO2_VSEL_CTR_SEL] = REG_FIELD(0x69, 4, 5),
	[NLDO1_VSEL_CTR_SEL] = REG_FIELD(0x69, 0, 1),
	[BUCK10_VSEL_CTR_SEL] = REG_FIELD(0x68, 4, 5),
	[BUCK9_VSEL_CTR_SEL] = REG_FIELD(0x68, 0, 1),
	/* SLEEP_VSEL_CTR_SEL3 */
	[NLDO5_VSEL_CTR_SEL] = REG_FIELD(0x6b, 0, 1),
	[NLDO4_VSEL_CTR_SEL] = REG_FIELD(0x6a, 4, 5),
	[NLDO3_VSEL_CTR_SEL] = REG_FIELD(0x6a, 0, 1),
	/* SLEEP_VSEL_CTR_SEL4 */
	[PLDO4_VSEL_CTR_SEL] = REG_FIELD(0x6d, 4, 5),
	[PLDO3_VSEL_CTR_SEL] = REG_FIELD(0x6d, 0, 0),
	[PLDO2_VSEL_CTR_SEL] = REG_FIELD(0x6c, 4, 5),
	[PLDO1_VSEL_CTR_SEL] = REG_FIELD(0x6c, 0, 1),
	/* SLEEP_VSEL_CTR_SEL5 */
	[PLDO6_VSEL_CTR_SEL] = REG_FIELD(0x6e, 4, 5),
	[PLDO5_VSEL_CTR_SEL] = REG_FIELD(0x6e, 0, 1),
	/* DVS_CTRL_SEL0 */
	[BUCK4_DVS_CTR_SEL] = REG_FIELD(0x65, 6, 7),
	[BUCK3_DVS_CTR_SEL] = REG_FIELD(0x65, 2, 3),
	[BUCK2_DVS_CTR_SEL] = REG_FIELD(0x64, 6, 7),
	[BUCK1_DVS_CTR_SEL] = REG_FIELD(0x64, 2, 3),
	/* DVS_CTRL_SEL1*/
	[BUCK8_DVS_CTR_SEL] = REG_FIELD(0x67, 6, 7),
	[BUCK7_DVS_CTR_SEL] = REG_FIELD(0x67, 2, 3),
	[BUCK6_DVS_CTR_SEL] = REG_FIELD(0x66, 6, 7),
	[BUCK5_DVS_CTR_SEL] = REG_FIELD(0x66, 2, 3),
	/* DVS_CTRL_SEL2 */
	[NLDO2_DVS_CTR_SEL] = REG_FIELD(0x69, 6, 7),
	[NLDO1_DVS_CTR_SEL] = REG_FIELD(0x69, 2, 3),
	[BUCK10_DVS_CTR_SEL] = REG_FIELD(0x68, 6, 7),
	[BUCK9_DVS_CTR_SEL] = REG_FIELD(0x68, 2, 3),
	/* DVS_CTRL_SEL3 */
	[NLDO5_DVS_CTR_SEL] = REG_FIELD(0x6b, 2, 3),
	[NLDO4_DVS_CTR_SEL] = REG_FIELD(0x6a, 6, 7),
	[NLDO3_DVS_CTR_SEL] = REG_FIELD(0x6a, 2, 3),
	/* DVS_CTRL_SEL4 */
	[PLDO4_DVS_CTR_SEL] = REG_FIELD(0x6d, 6, 7),
	[PLDO3_DVS_CTR_SEL] = REG_FIELD(0x6d, 2, 3),
	[PLDO2_DVS_CTR_SEL] = REG_FIELD(0x6c, 6, 7),
	[PLDO1_DVS_CTR_SEL] = REG_FIELD(0x6c, 2, 3),
	/* DVS_CTRL_SEL5 */
	[PLDO6_DVS_CTR_SEL] = REG_FIELD(0x6e, 6, 7),
	[PLDO5_DVS_CTR_SEL] = REG_FIELD(0x6e, 2, 3),
	/* DVS_START_CTRL */
	[DVS_START3] = REG_FIELD(0x70, 2, 2),
	[DVS_START2] = REG_FIELD(0x70, 1, 1),
	[DVS_START1] = REG_FIELD(0x70, 0, 0),
	/* SLEEP_GPIO */
	[SLP3_DATA] = REG_FIELD(0x71, 6, 6),
	[SLP2_DATA] = REG_FIELD(0x71, 5, 5),
	[SLP1_DATA] = REG_FIELD(0x71, 4, 4),
	[SLP3_DR] = REG_FIELD(0x71, 2, 2),
	[SLP2_DR] = REG_FIELD(0x71, 1, 1),
	[SLP1_DR] = REG_FIELD(0x71, 0, 0),
	/* SYS_CFG3 */
	[RST_FUN] = REG_FIELD(0x72, 6, 7),
	[DEV_RST] = REG_FIELD(0x72, 5, 5),
	[DEV_SLP] = REG_FIELD(0x72, 4, 4),
	[SLAVE_RESTART_FUN] = REG_FIELD(0x72, 1, 1),
	[DEV_OFF] = REG_FIELD(0x72, 0, 0),
	[WDT_CLR] = REG_FIELD(0x73, 4, 4),
	[WDT_EN] = REG_FIELD(0x73, 3, 3),
	[WDT_SET] = REG_FIELD(0x73, 0, 3),
	[ON_SOURCE] = REG_FIELD(0x74, 0, 7),
	[OFF_SOURCE] = REG_FIELD(0x75, 0, 7),
	/* PWRON_KEY */
	[PWRON_ON_TIME] = REG_FIELD(0x76, 7, 7),
	[PWRON_LP_ACT] = REG_FIELD(0x76, 6, 6),
	[PWRON_LP_OFF_TIME] = REG_FIELD(0x76, 4, 5),
	[PWRON_LP_TM_SEL] = REG_FIELD(0x76, 2, 3),
	[PWRON_DB_SEL] = REG_FIELD(0x76, 0, 1),

	/* GPIO_INT_CONFIG */
	[INT_FUNCTION] = REG_FIELD(0x7b, 2, 2),
	[INT_POL] = REG_FIELD(0x7b, 1, 1),
	[INT_FC_EN] = REG_FIELD(0x7b, 0, 0),
	[BUCK9_RATE2] = REG_FIELD(0xEA, 0, 0),
	[BUCK10_RATE2] = REG_FIELD(0xEA, 1, 1),
	[LDO_RATE] = REG_FIELD(0xEA, 3, 5),
	[BUCK1_RATE2] = REG_FIELD(0xEB, 0, 0),
	[BUCK2_RATE2] = REG_FIELD(0xEB, 1, 1),
	[BUCK3_RATE2] = REG_FIELD(0xEB, 2, 2),
	[BUCK4_RATE2] = REG_FIELD(0xEB, 3, 3),
	[BUCK5_RATE2] = REG_FIELD(0xEB, 4, 4),
	[BUCK6_RATE2] = REG_FIELD(0xEB, 5, 5),
	[BUCK7_RATE2] = REG_FIELD(0xEB, 6, 6),
	[BUCK8_RATE2] = REG_FIELD(0xEB, 7, 7),
};

static struct resource rk806_pwrkey_resources[] = {
	DEFINE_RES_IRQ(RK806_IRQ_PWRON_FALL),
	DEFINE_RES_IRQ(RK806_IRQ_PWRON_RISE),
};

static const struct mfd_cell rk806_cells[] = {
	{ .name = "rk806-pinctrl", },
	{
		.name = "rk805-pwrkey",
		.num_resources = ARRAY_SIZE(rk806_pwrkey_resources),
		.resources = &rk806_pwrkey_resources[0],
	},
	{ .name = "rk806-regulator", },

};

static const struct regmap_irq rk806_irqs[] = {
	/* INT_STS0 IRQs */
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_FALL, 0, RK806_INT_STS_PWRON_FALL),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_RISE, 0, RK806_INT_STS_PWRON_RISE),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON, 0, RK806_INT_STS_PWRON),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_LP, 0, RK806_INT_STS_PWRON_LP),
	REGMAP_IRQ_REG(RK806_IRQ_HOTDIE, 0, RK806_INT_STS_HOTDIE),
	REGMAP_IRQ_REG(RK806_IRQ_VDC_RISE, 0, RK806_INT_STS_VDC_RISE),
	REGMAP_IRQ_REG(RK806_IRQ_VDC_FALL, 0, RK806_INT_STS_VDC_FALL),
	REGMAP_IRQ_REG(RK806_IRQ_VB_LO, 0, RK806_INT_STS_VB_LO),
	/* INT_STS1 IRQs */
	REGMAP_IRQ_REG(RK806_IRQ_REV0, 1, RK806_INT_STS_REV0),
	REGMAP_IRQ_REG(RK806_IRQ_REV1, 1, RK806_INT_STS_REV1),
	REGMAP_IRQ_REG(RK806_IRQ_REV2, 1, RK806_INT_STS_REV2),
	REGMAP_IRQ_REG(RK806_IRQ_CRC_ERROR, 1, RK806_INT_STS_CRC_ERROR),
	REGMAP_IRQ_REG(RK806_IRQ_SLP3_GPIO, 1, RK806_INT_STS_SLP3_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_SLP2_GPIO, 1, RK806_INT_STS_SLP2_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_SLP1_GPIO, 1, RK806_INT_STS_SLP1_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_WDT, 1, RK806_INT_STS_WDT),
};

static struct regmap_irq_chip rk806_irq_chip = {
	.name = "rk806",
	.irqs = rk806_irqs,
	.num_irqs = ARRAY_SIZE(rk806_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.mask_base = RK806_INT_MSK0,
	.status_base = RK806_INT_STS0,
	.ack_base = RK806_INT_STS0,
	.init_ack_masked = true,
};

static const struct regmap_range rk806_yes_ranges[] = {
	/* regmap_reg_range(RK806_INT_STS0, RK806_GPIO_INT_CONFIG), */
	regmap_reg_range(RK806_POWER_EN0, RK806_POWER_EN5),
	regmap_reg_range(0x70, 0x7a),
};

static const struct regmap_access_table rk806_volatile_table = {
	.yes_ranges = rk806_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk806_yes_ranges),
};

const struct regmap_config rk806_regmap_config_spi = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &rk806_volatile_table,
};
EXPORT_SYMBOL_GPL(rk806_regmap_config_spi);

static struct kobject *rk806_kobj[2];
static struct rk806 *rk806_master;
static struct rk806 *rk806_slaver;

static ssize_t rk806_master_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	u32 input[2], addr, data;
	struct rk806 *rk806;
	char cmd;
	int ret;

	ret = sscanf(buf, "%c ", &cmd);
	if (ret != 1) {
		pr_err("Unknown command\n");
		goto out;
	}

	rk806 = rk806_master;
	if (!rk806) {
		pr_err("error! rk806 master is NULL\n");
		return 0;
	}

	switch (cmd) {
	case 'w':
		ret = sscanf(buf, "%c %x %x", &cmd, &input[0], &input[1]);
		if (ret != 3) {
			pr_err("error! cmd format: echo w [addr] [value]\n");
			goto out;
		};

		addr = input[0] & 0xff;
		data = input[1] & 0xff;
		pr_info("cmd : %c %x %x\n\n", cmd, input[0], input[1]);

		regmap_write(rk806->regmap, addr, data);
		regmap_read(rk806->regmap, addr, &data);
		pr_info("new: %x %x\n", addr, data);
		break;
	case 'r':
		ret = sscanf(buf, "%c %x ", &cmd, &input[0]);
		if (ret != 2) {
			pr_err("error! cmd format: echo r [addr]\n");
			goto out;
		};

		pr_info("cmd : %c %x\n\n", cmd, input[0]);
		addr = input[0] & 0xff;

		regmap_read(rk806->regmap, addr, &data);
		pr_info("%x %x\n", input[0], data);
		break;
	default:
		pr_err("Unknown command\n");
		break;
	}

out:
	return count;
}

static ssize_t rk806_slaver_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	u32 input[2], addr, data;
	struct rk806 *rk806;
	char cmd;
	int ret;

	ret = sscanf(buf, "%c ", &cmd);
	if (ret != 1) {
		pr_err("Unknown command\n");
		goto out;
	}

	rk806 = rk806_slaver;
	if (!rk806) {
		pr_err("error! rk806 slaver is NULL\n");
		return 0;
	}

	switch (cmd) {
	case 'w':
		ret = sscanf(buf, "%c %x %x", &cmd, &input[0], &input[1]);
		if (ret != 3) {
			pr_err("error! cmd format: echo w [addr] [value]\n");
			goto out;
		};

		addr = input[0] & 0xff;
		data = input[1] & 0xff;
		pr_info("cmd : %c %x %x\n\n", cmd, input[0], input[1]);

		regmap_write(rk806->regmap, addr, data);
		regmap_read(rk806->regmap, addr, &data);
		pr_info("new: %x %x\n", addr, data);
		break;
	case 'r':
		ret = sscanf(buf, "%c %x ", &cmd, &input[0]);
		if (ret != 2) {
			pr_err("error! cmd format: echo r [addr]\n");
			goto out;
		};
		pr_info("cmd : %c %x\n\n", cmd, input[0]);

		addr = input[0] & 0xff;
		regmap_read(rk806->regmap, addr, &data);
		pr_info("%x %x\n", input[0], data);
		break;
	default:
		pr_err("Unknown command\n");
		break;
	}

out:
	return count;
}

static struct device_attribute rk806_master_attrs =
		__ATTR(debug, 0200, NULL, rk806_master_store);

static struct device_attribute rk806_slaver_attrs =
		__ATTR(debug, 0200, NULL, rk806_slaver_store);

int rk806_field_read(struct rk806 *rk806,
		     enum rk806_fields field_id)
{
	int ret;
	int val;

	ret = regmap_field_read(rk806->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}
EXPORT_SYMBOL_GPL(rk806_field_read);

int rk806_field_write(struct rk806 *rk806,
		      enum rk806_fields field_id,
		      unsigned int val)
{
	return regmap_field_write(rk806->rmap_fields[field_id], val);
}
EXPORT_SYMBOL_GPL(rk806_field_write);

static void rk806_irq_init(struct rk806 *rk806)
{
	/* INT pin polarity  active low */
	rk806_field_write(rk806, INT_POL, RK806_INT_POL_LOW);
}

static int rk806_pinctrl_init(struct rk806 *rk806)
{
	struct device *dev = rk806->dev;

	rk806->pins = devm_kzalloc(dev,
				   sizeof(struct rk806_pin_info),
				   GFP_KERNEL);
	if (!rk806->pins)
		return -ENOMEM;

	rk806->pins->p = devm_pinctrl_get(dev);
	if (IS_ERR(rk806->pins->p)) {
		rk806->pins->p = NULL;
		dev_err(dev, "no pinctrl handle\n");
		return 0;
	}

	rk806->pins->default_st = pinctrl_lookup_state(rk806->pins->p,
						       PINCTRL_STATE_DEFAULT);

	if (IS_ERR(rk806->pins->default_st))
		dev_err(dev, "no default pinctrl state\n");

	rk806->pins->power_off = pinctrl_lookup_state(rk806->pins->p,
						      "pmic-power-off");
	if (IS_ERR(rk806->pins->power_off)) {
		rk806->pins->power_off = NULL;
		dev_err(dev, "no power-off pinctrl state\n");
	}

	rk806->pins->sleep = pinctrl_lookup_state(rk806->pins->p,
						  "pmic-sleep");
	if (IS_ERR(rk806->pins->sleep)) {
		rk806->pins->sleep = NULL;
		dev_err(dev, "no sleep-setting state\n");
	}

	rk806->pins->reset = pinctrl_lookup_state(rk806->pins->p,
						  "pmic-reset");
	if (IS_ERR(rk806->pins->reset)) {
		rk806->pins->reset = NULL;
		dev_err(dev, "no reset-setting pinctrl state\n");
	}

	rk806->pins->dvs = pinctrl_lookup_state(rk806->pins->p,
						"pmic-dvs");
	if (IS_ERR(rk806->pins->dvs)) {
		rk806->pins->dvs = NULL;
		dev_err(dev, "no dvs-setting pinctrl state\n");
	}

	return 0;
}

static irqreturn_t rk806_vb_low_irq(int irq, void *rk806)
{
	return IRQ_HANDLED;
}

static int rk806_low_power_irqs(struct rk806 *rk806)
{
	struct rk806_platform_data *pdata;
	int ret, vb_lo_irq;

	pdata = rk806->pdata;

	if (!pdata->low_voltage_threshold)
		return 0;

	rk806_field_write(rk806, VB_LO_ACT, VB_LO_ACT_INT);

	rk806_field_write(rk806, VB_LO_SEL,
			  (pdata->low_voltage_threshold - 2800) / 100);

	vb_lo_irq = regmap_irq_get_virq(rk806->irq_data, RK806_IRQ_VB_LO);
	if (vb_lo_irq < 0) {
		dev_err(rk806->dev, "vb_lo_irq request failed!\n");
		return vb_lo_irq;
	}

	ret = devm_request_threaded_irq(rk806->dev, vb_lo_irq,
					NULL,
					rk806_vb_low_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"rk806_vb_low", rk806);
	if (ret) {
		dev_err(rk806->dev, "vb_lo_irq request failed!\n");
		return ret;
	}

	rk806->vb_lo_irq = vb_lo_irq;
	disable_irq(rk806->vb_lo_irq);
	enable_irq_wake(vb_lo_irq);

	return 0;
}

static int rk806_parse_dt(struct rk806 *rk806)
{
	struct rk806_platform_data *pdata;
	struct device *dev = rk806->dev;
	int rst_fun;
	int ret;

	pdata = rk806->pdata;

	pdata->shutdown_voltage_threshold = 2700;
	pdata->shutdown_temperture_threshold = 160;
	pdata->hotdie_temperture_threshold = 115;
	pdata->force_shutdown_enable = 1;

	ret = device_property_read_u32(dev,
				       "low_voltage_threshold",
				       &pdata->low_voltage_threshold);
	if (ret < 0) {
		pdata->low_voltage_threshold = 0;
		dev_info(dev, "low_voltage_threshold missing!\n");
	} else {
		if ((pdata->low_voltage_threshold > 3500) ||
		    (pdata->low_voltage_threshold < 2800)) {
			dev_err(dev, "low_voltage_threshold out [2800 3500]!\n");
			pdata->low_voltage_threshold = 2800;
		}
	}
	ret = device_property_read_u32(dev,
				       "shutdown_voltage_threshold",
				       &pdata->shutdown_voltage_threshold);
	if (ret < 0) {
		pdata->force_shutdown_enable = 0;
		dev_info(dev, "shutdown_voltage_threshold missing!\n");
	}

	if ((pdata->shutdown_voltage_threshold > 3400) ||
	    (pdata->shutdown_voltage_threshold < 2700)) {
		dev_err(dev, "shutdown_voltage_threshold out [2700 3400]!\n");
		pdata->shutdown_voltage_threshold = 2700;
	}

	ret = device_property_read_u32(dev,
				       "shutdown_temperture_threshold",
				       &pdata->shutdown_temperture_threshold);
	if (ret < 0)
		dev_info(dev, "shutdown_temperture_threshold missing!\n");

	ret = device_property_read_u32(dev,
				       "hotdie_temperture_threshold",
				       &pdata->hotdie_temperture_threshold);
	if (ret < 0)
		dev_info(dev, "hotdie_temperture_threshold missing!\n");

	ret = device_property_read_u32(dev, "pmic-reset-func", &rst_fun);
	if (ret < 0) {
		dev_info(dev, "pmic-reset-func missing!\n");
		rk806_field_write(rk806, RST_FUN, 0x00);
	} else
		rk806_field_write(rk806, RST_FUN, rst_fun);

	/* PWRON_ON_TIME: 0:500mS; 1:20mS */
	if (device_property_read_bool(dev, "pwron-on-time-500ms"))
		rk806_field_write(rk806, PWRON_ON_TIME, 0x00);

	return 0;
}

static int rk806_init(struct rk806 *rk806)
{
	struct rk806_platform_data *pdata;
	int vb_uv_sel;

	pdata = rk806->pdata;

	if (pdata->force_shutdown_enable) {
		if (pdata->shutdown_voltage_threshold <= 2700)
			vb_uv_sel = VB_UV_SEL_2700;
		else
			vb_uv_sel = (pdata->shutdown_voltage_threshold - 2700) / 100;

		rk806_field_write(rk806, VB_UV_SEL, vb_uv_sel);
	}

	if (pdata->hotdie_temperture_threshold >= 160)
		rk806_field_write(rk806, TSD_TEMP, TSD_TEMP_160);

	/* When the slave chip goes through a shutdown process, it will automatically trigger a restart */
	rk806_field_write(rk806, SLAVE_RESTART_FUN, 0x01);
	/* Digital output 2MHz clock force enable */
	rk806_field_write(rk806, ENB2_2M, 0x01);

	rk806_low_power_irqs(rk806);

	return 0;
}

int rk806_device_init(struct rk806 *rk806)
{
	struct device_node *np = rk806->dev->of_node;
	struct rk806_platform_data *pdata;
	int name_h, name_l, chip_ver, otp_ver;
	int on_source, off_source;
	int ret;
	int i;

	pdata = devm_kzalloc(rk806->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	rk806->pdata = pdata;

	for (i = 0; i < ARRAY_SIZE(rk806_reg_fields); i++) {
		const struct reg_field *reg_fields = rk806_reg_fields;

		rk806->rmap_fields[i] =
			devm_regmap_field_alloc(rk806->dev,
						rk806->regmap,
						reg_fields[i]);
		if (IS_ERR(rk806->rmap_fields[i])) {
			dev_err(rk806->dev, "cannot allocate regmap field\n");
			return PTR_ERR(rk806->rmap_fields[i]);
		}
	}

	name_h = rk806_field_read(rk806, CHIP_NAME_H);
	name_l = rk806_field_read(rk806, CHIP_NAME_L);
	chip_ver = rk806_field_read(rk806, CHIP_VER);
	otp_ver = rk806_field_read(rk806, OTP_VER);
	dev_info(rk806->dev, "chip id: RK%x%x,ver:0x%x, 0x%x\n",
		 name_h, name_l, chip_ver, otp_ver);
	if (chip_ver == VERSION_AB)
		rk806_field_write(rk806, ABNORDET_EN, 0x01);

	on_source = rk806_field_read(rk806, ON_SOURCE);
	off_source = rk806_field_read(rk806, OFF_SOURCE);
	dev_info(rk806->dev, "ON: 0x%x OFF:0x%x\n", on_source, off_source);

	rk806_parse_dt(rk806);

	rk806_irq_init(rk806);
	ret = devm_regmap_add_irq_chip(rk806->dev,
				       rk806->regmap,
				       rk806->irq,
				       IRQF_ONESHOT | IRQF_SHARED,
				       0,
				       &rk806_irq_chip,
				       &rk806->irq_data);
	if (ret) {
		dev_err(rk806->dev, "Failed to add IRQ chip: err = %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(rk806->dev,
				   PLATFORM_DEVID_AUTO,
				   rk806_cells,
				   ARRAY_SIZE(rk806_cells),
				   NULL,
				   0,
				   regmap_irq_get_domain(rk806->irq_data));
	if (ret < 0) {
		dev_err(rk806->dev, "mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	rk806_pinctrl_init(rk806);
	rk806_init(rk806);

	if (strcmp(np->name, "rk806slave")) {
		rk806_kobj[0] = kobject_create_and_add(np->name, NULL);
		if (rk806_kobj[0]) {
			ret = sysfs_create_file(rk806_kobj[0], &rk806_master_attrs.attr);
			if (ret)
				dev_err(rk806->dev, "create %s sysfs error\n", np->name);
			else
				rk806_master = rk806;
		}
	} else {
		rk806_kobj[1] = kobject_create_and_add(np->name, NULL);
		if (rk806_kobj[1]) {
			ret = sysfs_create_file(rk806_kobj[1], &rk806_slaver_attrs.attr);
			if (ret)
				dev_err(rk806->dev, "create %s sysfs error\n", np->name);
			else
				rk806_slaver = rk806;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk806_device_init);

int rk806_device_exit(struct rk806 *rk806)
{
	struct device_node *np = rk806->dev->of_node;

	if (strcmp(np->name, "rk806slave")) {
		if (rk806_kobj[0]) {
			sysfs_remove_file(rk806_kobj[0], &rk806_master_attrs.attr);
			kobject_put(rk806_kobj[0]);
		}
	} else {
		if (rk806_kobj[1]) {
			sysfs_remove_file(rk806_kobj[1], &rk806_slaver_attrs.attr);
			kobject_put(rk806_kobj[1]);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk806_device_exit);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("rk806 MFD Driver");
MODULE_LICENSE("GPL v2");
