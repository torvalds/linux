/*
 * Driver for the ST Microelectronics SPEAr320 pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "pinctrl-spear3xx.h"

#define DRIVER_NAME "spear320-pinmux"

/* addresses */
#define PMX_CONFIG_REG			0x0C
#define MODE_CONFIG_REG			0x10
#define MODE_EXT_CONFIG_REG		0x18

/* modes */
#define AUTO_NET_SMII_MODE	(1 << 0)
#define AUTO_NET_MII_MODE	(1 << 1)
#define AUTO_EXP_MODE		(1 << 2)
#define SMALL_PRINTERS_MODE	(1 << 3)
#define EXTENDED_MODE		(1 << 4)

static struct spear_pmx_mode pmx_mode_auto_net_smii = {
	.name = "Automation Networking SMII mode",
	.mode = AUTO_NET_SMII_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x00000007,
	.val = 0x0,
};

static struct spear_pmx_mode pmx_mode_auto_net_mii = {
	.name = "Automation Networking MII mode",
	.mode = AUTO_NET_MII_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x00000007,
	.val = 0x1,
};

static struct spear_pmx_mode pmx_mode_auto_exp = {
	.name = "Automation Expanded mode",
	.mode = AUTO_EXP_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x00000007,
	.val = 0x2,
};

static struct spear_pmx_mode pmx_mode_small_printers = {
	.name = "Small Printers mode",
	.mode = SMALL_PRINTERS_MODE,
	.reg = MODE_CONFIG_REG,
	.mask = 0x00000007,
	.val = 0x3,
};

static struct spear_pmx_mode pmx_mode_extended = {
	.name = "extended mode",
	.mode = EXTENDED_MODE,
	.reg = MODE_EXT_CONFIG_REG,
	.mask = 0x00000001,
	.val = 0x1,
};

static struct spear_pmx_mode *spear320_pmx_modes[] = {
	&pmx_mode_auto_net_smii,
	&pmx_mode_auto_net_mii,
	&pmx_mode_auto_exp,
	&pmx_mode_small_printers,
	&pmx_mode_extended,
};

/* Extended mode registers and their offsets */
#define EXT_CTRL_REG				0x0018
	#define MII_MDIO_MASK			(1 << 4)
	#define MII_MDIO_10_11_VAL		0
	#define MII_MDIO_81_VAL			(1 << 4)
	#define EMI_FSMC_DYNAMIC_MUX_MASK	(1 << 5)
	#define MAC_MODE_MII			0
	#define MAC_MODE_RMII			1
	#define MAC_MODE_SMII			2
	#define MAC_MODE_SS_SMII		3
	#define MAC_MODE_MASK			0x3
	#define MAC1_MODE_SHIFT			16
	#define MAC2_MODE_SHIFT			18

#define IP_SEL_PAD_0_9_REG			0x00A4
	#define PMX_PL_0_1_MASK			(0x3F << 0)
	#define PMX_UART2_PL_0_1_VAL		0x0
	#define PMX_I2C2_PL_0_1_VAL		(0x4 | (0x4 << 3))

	#define PMX_PL_2_3_MASK			(0x3F << 6)
	#define PMX_I2C2_PL_2_3_VAL		0x0
	#define PMX_UART6_PL_2_3_VAL		((0x1 << 6) | (0x1 << 9))
	#define PMX_UART1_ENH_PL_2_3_VAL	((0x4 << 6) | (0x4 << 9))

	#define PMX_PL_4_5_MASK			(0x3F << 12)
	#define PMX_UART5_PL_4_5_VAL		((0x1 << 12) | (0x1 << 15))
	#define PMX_UART1_ENH_PL_4_5_VAL	((0x4 << 12) | (0x4 << 15))
	#define PMX_PL_5_MASK			(0x7 << 15)
	#define PMX_TOUCH_Y_PL_5_VAL		0x0

	#define PMX_PL_6_7_MASK			(0x3F << 18)
	#define PMX_PL_6_MASK			(0x7 << 18)
	#define PMX_PL_7_MASK			(0x7 << 21)
	#define PMX_UART4_PL_6_7_VAL		((0x1 << 18) | (0x1 << 21))
	#define PMX_PWM_3_PL_6_VAL		(0x2 << 18)
	#define PMX_PWM_2_PL_7_VAL		(0x2 << 21)
	#define PMX_UART1_ENH_PL_6_7_VAL	((0x4 << 18) | (0x4 << 21))

	#define PMX_PL_8_9_MASK			(0x3F << 24)
	#define PMX_UART3_PL_8_9_VAL		((0x1 << 24) | (0x1 << 27))
	#define PMX_PWM_0_1_PL_8_9_VAL		((0x2 << 24) | (0x2 << 27))
	#define PMX_I2C1_PL_8_9_VAL		((0x4 << 24) | (0x4 << 27))

#define IP_SEL_PAD_10_19_REG			0x00A8
	#define PMX_PL_10_11_MASK		(0x3F << 0)
	#define PMX_SMII_PL_10_11_VAL		0
	#define PMX_RMII_PL_10_11_VAL		((0x4 << 0) | (0x4 << 3))

	#define PMX_PL_12_MASK			(0x7 << 6)
	#define PMX_PWM3_PL_12_VAL		0
	#define PMX_SDHCI_CD_PL_12_VAL		(0x4 << 6)

	#define PMX_PL_13_14_MASK		(0x3F << 9)
	#define PMX_PL_13_MASK			(0x7 << 9)
	#define PMX_PL_14_MASK			(0x7 << 12)
	#define PMX_SSP2_PL_13_14_15_16_VAL	0
	#define PMX_UART4_PL_13_14_VAL		((0x1 << 9) | (0x1 << 12))
	#define PMX_RMII_PL_13_14_VAL		((0x4 << 9) | (0x4 << 12))
	#define PMX_PWM2_PL_13_VAL		(0x2 << 9)
	#define PMX_PWM1_PL_14_VAL		(0x2 << 12)

	#define PMX_PL_15_MASK			(0x7 << 15)
	#define PMX_PWM0_PL_15_VAL		(0x2 << 15)
	#define PMX_PL_15_16_MASK		(0x3F << 15)
	#define PMX_UART3_PL_15_16_VAL		((0x1 << 15) | (0x1 << 18))
	#define PMX_RMII_PL_15_16_VAL		((0x4 << 15) | (0x4 << 18))

	#define PMX_PL_17_18_MASK		(0x3F << 21)
	#define PMX_SSP1_PL_17_18_19_20_VAL	0
	#define PMX_RMII_PL_17_18_VAL		((0x4 << 21) | (0x4 << 24))

	#define PMX_PL_19_MASK			(0x7 << 27)
	#define PMX_I2C2_PL_19_VAL		(0x1 << 27)
	#define PMX_RMII_PL_19_VAL		(0x4 << 27)

#define IP_SEL_PAD_20_29_REG			0x00AC
	#define PMX_PL_20_MASK			(0x7 << 0)
	#define PMX_I2C2_PL_20_VAL		(0x1 << 0)
	#define PMX_RMII_PL_20_VAL		(0x4 << 0)

	#define PMX_PL_21_TO_27_MASK		(0x1FFFFF << 3)
	#define PMX_SMII_PL_21_TO_27_VAL	0
	#define PMX_RMII_PL_21_TO_27_VAL	((0x4 << 3) | (0x4 << 6) | (0x4 << 9) | (0x4 << 12) | (0x4 << 15) | (0x4 << 18) | (0x4 << 21))

	#define PMX_PL_28_29_MASK		(0x3F << 24)
	#define PMX_PL_28_MASK			(0x7 << 24)
	#define PMX_PL_29_MASK			(0x7 << 27)
	#define PMX_UART1_PL_28_29_VAL		0
	#define PMX_PWM_3_PL_28_VAL		(0x4 << 24)
	#define PMX_PWM_2_PL_29_VAL		(0x4 << 27)

#define IP_SEL_PAD_30_39_REG			0x00B0
	#define PMX_PL_30_31_MASK		(0x3F << 0)
	#define PMX_CAN1_PL_30_31_VAL		(0)
	#define PMX_PL_30_MASK			(0x7 << 0)
	#define PMX_PL_31_MASK			(0x7 << 3)
	#define PMX_PWM1_EXT_PL_30_VAL		(0x4 << 0)
	#define PMX_PWM0_EXT_PL_31_VAL		(0x4 << 3)
	#define PMX_UART1_ENH_PL_31_VAL		(0x3 << 3)

	#define PMX_PL_32_33_MASK		(0x3F << 6)
	#define PMX_CAN0_PL_32_33_VAL		0
	#define PMX_UART1_ENH_PL_32_33_VAL	((0x3 << 6) | (0x3 << 9))
	#define PMX_SSP2_PL_32_33_VAL		((0x4 << 6) | (0x4 << 9))

	#define PMX_PL_34_MASK			(0x7 << 12)
	#define PMX_PWM2_PL_34_VAL		0
	#define PMX_UART1_ENH_PL_34_VAL		(0x2 << 12)
	#define PMX_SSP2_PL_34_VAL		(0x4 << 12)

	#define PMX_PL_35_MASK			(0x7 << 15)
	#define PMX_I2S_REF_CLK_PL_35_VAL	0
	#define PMX_UART1_ENH_PL_35_VAL		(0x2 << 15)
	#define PMX_SSP2_PL_35_VAL		(0x4 << 15)

	#define PMX_PL_36_MASK			(0x7 << 18)
	#define PMX_TOUCH_X_PL_36_VAL		0
	#define PMX_UART1_ENH_PL_36_VAL		(0x2 << 18)
	#define PMX_SSP1_PL_36_VAL		(0x4 << 18)

	#define PMX_PL_37_38_MASK		(0x3F << 21)
	#define PMX_PWM0_1_PL_37_38_VAL		0
	#define PMX_UART5_PL_37_38_VAL		((0x2 << 21) | (0x2 << 24))
	#define PMX_SSP1_PL_37_38_VAL		((0x4 << 21) | (0x4 << 24))

	#define PMX_PL_39_MASK			(0x7 << 27)
	#define PMX_I2S_PL_39_VAL		0
	#define PMX_UART4_PL_39_VAL		(0x2 << 27)
	#define PMX_SSP1_PL_39_VAL		(0x4 << 27)

#define IP_SEL_PAD_40_49_REG			0x00B4
	#define PMX_PL_40_MASK			(0x7 << 0)
	#define PMX_I2S_PL_40_VAL		0
	#define PMX_UART4_PL_40_VAL		(0x2 << 0)
	#define PMX_PWM3_PL_40_VAL		(0x4 << 0)

	#define PMX_PL_41_42_MASK		(0x3F << 3)
	#define PMX_PL_41_MASK			(0x7 << 3)
	#define PMX_PL_42_MASK			(0x7 << 6)
	#define PMX_I2S_PL_41_42_VAL		0
	#define PMX_UART3_PL_41_42_VAL		((0x2 << 3) | (0x2 << 6))
	#define PMX_PWM2_PL_41_VAL		(0x4 << 3)
	#define PMX_PWM1_PL_42_VAL		(0x4 << 6)

	#define PMX_PL_43_MASK			(0x7 << 9)
	#define PMX_SDHCI_PL_43_VAL		0
	#define PMX_UART1_ENH_PL_43_VAL		(0x2 << 9)
	#define PMX_PWM0_PL_43_VAL		(0x4 << 9)

	#define PMX_PL_44_45_MASK		(0x3F << 12)
	#define PMX_SDHCI_PL_44_45_VAL	0
	#define PMX_UART1_ENH_PL_44_45_VAL	((0x2 << 12) | (0x2 << 15))
	#define PMX_SSP2_PL_44_45_VAL		((0x4 << 12) | (0x4 << 15))

	#define PMX_PL_46_47_MASK		(0x3F << 18)
	#define PMX_SDHCI_PL_46_47_VAL	0
	#define PMX_FSMC_EMI_PL_46_47_VAL	((0x2 << 18) | (0x2 << 21))
	#define PMX_SSP2_PL_46_47_VAL		((0x4 << 18) | (0x4 << 21))

	#define PMX_PL_48_49_MASK		(0x3F << 24)
	#define PMX_SDHCI_PL_48_49_VAL	0
	#define PMX_FSMC_EMI_PL_48_49_VAL	((0x2 << 24) | (0x2 << 27))
	#define PMX_SSP1_PL_48_49_VAL		((0x4 << 24) | (0x4 << 27))

#define IP_SEL_PAD_50_59_REG			0x00B8
	#define PMX_PL_50_51_MASK		(0x3F << 0)
	#define PMX_EMI_PL_50_51_VAL		((0x2 << 0) | (0x2 << 3))
	#define PMX_SSP1_PL_50_51_VAL		((0x4 << 0) | (0x4 << 3))
	#define PMX_PL_50_MASK			(0x7 << 0)
	#define PMX_PL_51_MASK			(0x7 << 3)
	#define PMX_SDHCI_PL_50_VAL		0
	#define PMX_SDHCI_CD_PL_51_VAL		0

	#define PMX_PL_52_53_MASK		(0x3F << 6)
	#define PMX_FSMC_PL_52_53_VAL		0
	#define PMX_EMI_PL_52_53_VAL		((0x2 << 6) | (0x2 << 9))
	#define PMX_UART3_PL_52_53_VAL		((0x4 << 6) | (0x4 << 9))

	#define PMX_PL_54_55_56_MASK		(0x1FF << 12)
	#define PMX_FSMC_EMI_PL_54_55_56_VAL	((0x2 << 12) | (0x2 << 15) | (0x2 << 18))

	#define PMX_PL_57_MASK			(0x7 << 21)
	#define PMX_FSMC_PL_57_VAL		0
	#define PMX_PWM3_PL_57_VAL		(0x4 << 21)

	#define PMX_PL_58_59_MASK		(0x3F << 24)
	#define PMX_PL_58_MASK			(0x7 << 24)
	#define PMX_PL_59_MASK			(0x7 << 27)
	#define PMX_FSMC_EMI_PL_58_59_VAL	((0x2 << 24) | (0x2 << 27))
	#define PMX_PWM2_PL_58_VAL		(0x4 << 24)
	#define PMX_PWM1_PL_59_VAL		(0x4 << 27)

#define IP_SEL_PAD_60_69_REG			0x00BC
	#define PMX_PL_60_MASK			(0x7 << 0)
	#define PMX_FSMC_PL_60_VAL		0
	#define PMX_PWM0_PL_60_VAL		(0x4 << 0)

	#define PMX_PL_61_TO_64_MASK		(0xFFF << 3)
	#define PMX_FSMC_PL_61_TO_64_VAL	((0x2 << 3) | (0x2 << 6) | (0x2 << 9) | (0x2 << 12))
	#define PMX_SSP2_PL_61_TO_64_VAL	((0x4 << 3) | (0x4 << 6) | (0x4 << 9) | (0x4 << 12))

	#define PMX_PL_65_TO_68_MASK		(0xFFF << 15)
	#define PMX_FSMC_PL_65_TO_68_VAL	((0x2 << 15) | (0x2 << 18) | (0x2 << 21) | (0x2 << 24))
	#define PMX_SSP1_PL_65_TO_68_VAL	((0x4 << 15) | (0x4 << 18) | (0x4 << 21) | (0x4 << 24))

	#define PMX_PL_69_MASK			(0x7 << 27)
	#define PMX_CLCD_PL_69_VAL		(0)
	#define PMX_EMI_PL_69_VAL		(0x2 << 27)
	#define PMX_SPP_PL_69_VAL		(0x3 << 27)
	#define PMX_UART5_PL_69_VAL		(0x4 << 27)

#define IP_SEL_PAD_70_79_REG			0x00C0
	#define PMX_PL_70_MASK			(0x7 << 0)
	#define PMX_CLCD_PL_70_VAL		(0)
	#define PMX_FSMC_EMI_PL_70_VAL		(0x2 << 0)
	#define PMX_SPP_PL_70_VAL		(0x3 << 0)
	#define PMX_UART5_PL_70_VAL		(0x4 << 0)

	#define PMX_PL_71_72_MASK		(0x3F << 3)
	#define PMX_CLCD_PL_71_72_VAL		(0)
	#define PMX_FSMC_EMI_PL_71_72_VAL	((0x2 << 3) | (0x2 << 6))
	#define PMX_SPP_PL_71_72_VAL		((0x3 << 3) | (0x3 << 6))
	#define PMX_UART4_PL_71_72_VAL		((0x4 << 3) | (0x4 << 6))

	#define PMX_PL_73_MASK			(0x7 << 9)
	#define PMX_CLCD_PL_73_VAL		(0)
	#define PMX_FSMC_EMI_PL_73_VAL		(0x2 << 9)
	#define PMX_SPP_PL_73_VAL		(0x3 << 9)
	#define PMX_UART3_PL_73_VAL		(0x4 << 9)

	#define PMX_PL_74_MASK			(0x7 << 12)
	#define PMX_CLCD_PL_74_VAL		(0)
	#define PMX_EMI_PL_74_VAL		(0x2 << 12)
	#define PMX_SPP_PL_74_VAL		(0x3 << 12)
	#define PMX_UART3_PL_74_VAL		(0x4 << 12)

	#define PMX_PL_75_76_MASK		(0x3F << 15)
	#define PMX_CLCD_PL_75_76_VAL		(0)
	#define PMX_EMI_PL_75_76_VAL		((0x2 << 15) | (0x2 << 18))
	#define PMX_SPP_PL_75_76_VAL		((0x3 << 15) | (0x3 << 18))
	#define PMX_I2C2_PL_75_76_VAL		((0x4 << 15) | (0x4 << 18))

	#define PMX_PL_77_78_79_MASK		(0x1FF << 21)
	#define PMX_CLCD_PL_77_78_79_VAL	(0)
	#define PMX_EMI_PL_77_78_79_VAL		((0x2 << 21) | (0x2 << 24) | (0x2 << 27))
	#define PMX_SPP_PL_77_78_79_VAL		((0x3 << 21) | (0x3 << 24) | (0x3 << 27))
	#define PMX_RS485_PL_77_78_79_VAL	((0x4 << 21) | (0x4 << 24) | (0x4 << 27))

#define IP_SEL_PAD_80_89_REG			0x00C4
	#define PMX_PL_80_TO_85_MASK		(0x3FFFF << 0)
	#define PMX_CLCD_PL_80_TO_85_VAL	0
	#define PMX_MII2_PL_80_TO_85_VAL	((0x1 << 0) | (0x1 << 3) | (0x1 << 6) | (0x1 << 9) | (0x1 << 12) | (0x1 << 15))
	#define PMX_EMI_PL_80_TO_85_VAL		((0x2 << 0) | (0x2 << 3) | (0x2 << 6) | (0x2 << 9) | (0x2 << 12) | (0x2 << 15))
	#define PMX_SPP_PL_80_TO_85_VAL		((0x3 << 0) | (0x3 << 3) | (0x3 << 6) | (0x3 << 9) | (0x3 << 12) | (0x3 << 15))
	#define PMX_UART1_ENH_PL_80_TO_85_VAL	((0x4 << 0) | (0x4 << 3) | (0x4 << 6) | (0x4 << 9) | (0x4 << 12) | (0x4 << 15))

	#define PMX_PL_86_87_MASK		(0x3F << 18)
	#define PMX_PL_86_MASK			(0x7 << 18)
	#define PMX_PL_87_MASK			(0x7 << 21)
	#define PMX_CLCD_PL_86_87_VAL		0
	#define PMX_MII2_PL_86_87_VAL		((0x1 << 18) | (0x1 << 21))
	#define PMX_EMI_PL_86_87_VAL		((0x2 << 18) | (0x2 << 21))
	#define PMX_PWM3_PL_86_VAL		(0x4 << 18)
	#define PMX_PWM2_PL_87_VAL		(0x4 << 21)

	#define PMX_PL_88_89_MASK		(0x3F << 24)
	#define PMX_CLCD_PL_88_89_VAL		0
	#define PMX_MII2_PL_88_89_VAL		((0x1 << 24) | (0x1 << 27))
	#define PMX_EMI_PL_88_89_VAL		((0x2 << 24) | (0x2 << 27))
	#define PMX_UART6_PL_88_89_VAL		((0x3 << 24) | (0x3 << 27))
	#define PMX_PWM0_1_PL_88_89_VAL		((0x4 << 24) | (0x4 << 27))

#define IP_SEL_PAD_90_99_REG			0x00C8
	#define PMX_PL_90_91_MASK		(0x3F << 0)
	#define PMX_CLCD_PL_90_91_VAL		0
	#define PMX_MII2_PL_90_91_VAL		((0x1 << 0) | (0x1 << 3))
	#define PMX_EMI1_PL_90_91_VAL		((0x2 << 0) | (0x2 << 3))
	#define PMX_UART5_PL_90_91_VAL		((0x3 << 0) | (0x3 << 3))
	#define PMX_SSP2_PL_90_91_VAL		((0x4 << 0) | (0x4 << 3))

	#define PMX_PL_92_93_MASK		(0x3F << 6)
	#define PMX_CLCD_PL_92_93_VAL		0
	#define PMX_MII2_PL_92_93_VAL		((0x1 << 6) | (0x1 << 9))
	#define PMX_EMI1_PL_92_93_VAL		((0x2 << 6) | (0x2 << 9))
	#define PMX_UART4_PL_92_93_VAL		((0x3 << 6) | (0x3 << 9))
	#define PMX_SSP2_PL_92_93_VAL		((0x4 << 6) | (0x4 << 9))

	#define PMX_PL_94_95_MASK		(0x3F << 12)
	#define PMX_CLCD_PL_94_95_VAL		0
	#define PMX_MII2_PL_94_95_VAL		((0x1 << 12) | (0x1 << 15))
	#define PMX_EMI1_PL_94_95_VAL		((0x2 << 12) | (0x2 << 15))
	#define PMX_UART3_PL_94_95_VAL		((0x3 << 12) | (0x3 << 15))
	#define PMX_SSP1_PL_94_95_VAL		((0x4 << 12) | (0x4 << 15))

	#define PMX_PL_96_97_MASK		(0x3F << 18)
	#define PMX_CLCD_PL_96_97_VAL		0
	#define PMX_MII2_PL_96_97_VAL		((0x1 << 18) | (0x1 << 21))
	#define PMX_EMI1_PL_96_97_VAL		((0x2 << 18) | (0x2 << 21))
	#define PMX_I2C2_PL_96_97_VAL		((0x3 << 18) | (0x3 << 21))
	#define PMX_SSP1_PL_96_97_VAL		((0x4 << 18) | (0x4 << 21))

	#define PMX_PL_98_MASK			(0x7 << 24)
	#define PMX_CLCD_PL_98_VAL		0
	#define PMX_I2C1_PL_98_VAL		(0x2 << 24)
	#define PMX_UART3_PL_98_VAL		(0x4 << 24)

	#define PMX_PL_99_MASK			(0x7 << 27)
	#define PMX_SDHCI_PL_99_VAL		0
	#define PMX_I2C1_PL_99_VAL		(0x2 << 27)
	#define PMX_UART3_PL_99_VAL		(0x4 << 27)

#define IP_SEL_MIX_PAD_REG			0x00CC
	#define PMX_PL_100_101_MASK		(0x3F << 0)
	#define PMX_SDHCI_PL_100_101_VAL	0
	#define PMX_UART4_PL_100_101_VAL	((0x4 << 0) | (0x4 << 3))

	#define PMX_SSP1_PORT_SEL_MASK		(0x7 << 8)
	#define PMX_SSP1_PORT_94_TO_97_VAL	0
	#define PMX_SSP1_PORT_65_TO_68_VAL	(0x1 << 8)
	#define PMX_SSP1_PORT_48_TO_51_VAL	(0x2 << 8)
	#define PMX_SSP1_PORT_36_TO_39_VAL	(0x3 << 8)
	#define PMX_SSP1_PORT_17_TO_20_VAL	(0x4 << 8)

	#define PMX_SSP2_PORT_SEL_MASK		(0x7 << 11)
	#define PMX_SSP2_PORT_90_TO_93_VAL	0
	#define PMX_SSP2_PORT_61_TO_64_VAL	(0x1 << 11)
	#define PMX_SSP2_PORT_44_TO_47_VAL	(0x2 << 11)
	#define PMX_SSP2_PORT_32_TO_35_VAL	(0x3 << 11)
	#define PMX_SSP2_PORT_13_TO_16_VAL	(0x4 << 11)

	#define PMX_UART1_ENH_PORT_SEL_MASK		(0x3 << 14)
	#define PMX_UART1_ENH_PORT_81_TO_85_VAL		0
	#define PMX_UART1_ENH_PORT_44_45_34_36_VAL	(0x1 << 14)
	#define PMX_UART1_ENH_PORT_32_TO_34_36_VAL	(0x2 << 14)
	#define PMX_UART1_ENH_PORT_3_TO_5_7_VAL		(0x3 << 14)

	#define PMX_UART3_PORT_SEL_MASK		(0x7 << 16)
	#define PMX_UART3_PORT_94_VAL		0
	#define PMX_UART3_PORT_73_VAL		(0x1 << 16)
	#define PMX_UART3_PORT_52_VAL		(0x2 << 16)
	#define PMX_UART3_PORT_41_VAL		(0x3 << 16)
	#define PMX_UART3_PORT_15_VAL		(0x4 << 16)
	#define PMX_UART3_PORT_8_VAL		(0x5 << 16)
	#define PMX_UART3_PORT_99_VAL		(0x6 << 16)

	#define PMX_UART4_PORT_SEL_MASK		(0x7 << 19)
	#define PMX_UART4_PORT_92_VAL		0
	#define PMX_UART4_PORT_71_VAL		(0x1 << 19)
	#define PMX_UART4_PORT_39_VAL		(0x2 << 19)
	#define PMX_UART4_PORT_13_VAL		(0x3 << 19)
	#define PMX_UART4_PORT_6_VAL		(0x4 << 19)
	#define PMX_UART4_PORT_101_VAL		(0x5 << 19)

	#define PMX_UART5_PORT_SEL_MASK		(0x3 << 22)
	#define PMX_UART5_PORT_90_VAL		0
	#define PMX_UART5_PORT_69_VAL		(0x1 << 22)
	#define PMX_UART5_PORT_37_VAL		(0x2 << 22)
	#define PMX_UART5_PORT_4_VAL		(0x3 << 22)

	#define PMX_UART6_PORT_SEL_MASK		(0x1 << 24)
	#define PMX_UART6_PORT_88_VAL		0
	#define PMX_UART6_PORT_2_VAL		(0x1 << 24)

	#define PMX_I2C1_PORT_SEL_MASK		(0x1 << 25)
	#define PMX_I2C1_PORT_8_9_VAL		0
	#define PMX_I2C1_PORT_98_99_VAL		(0x1 << 25)

	#define PMX_I2C2_PORT_SEL_MASK		(0x3 << 26)
	#define PMX_I2C2_PORT_96_97_VAL		0
	#define PMX_I2C2_PORT_75_76_VAL		(0x1 << 26)
	#define PMX_I2C2_PORT_19_20_VAL		(0x2 << 26)
	#define PMX_I2C2_PORT_2_3_VAL		(0x3 << 26)
	#define PMX_I2C2_PORT_0_1_VAL		(0x4 << 26)

	#define PMX_SDHCI_CD_PORT_SEL_MASK	(0x1 << 29)
	#define PMX_SDHCI_CD_PORT_12_VAL	0
	#define PMX_SDHCI_CD_PORT_51_VAL	(0x1 << 29)

/* Pad multiplexing for CLCD device */
static const unsigned clcd_pins[] = { 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
	79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
	97 };
static struct spear_muxreg clcd_muxreg[] = {
	{
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_69_MASK,
		.val = PMX_CLCD_PL_69_VAL,
	}, {
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_70_MASK | PMX_PL_71_72_MASK | PMX_PL_73_MASK |
			PMX_PL_74_MASK | PMX_PL_75_76_MASK |
			PMX_PL_77_78_79_MASK,
		.val = PMX_CLCD_PL_70_VAL | PMX_CLCD_PL_71_72_VAL |
			PMX_CLCD_PL_73_VAL | PMX_CLCD_PL_74_VAL |
			PMX_CLCD_PL_75_76_VAL | PMX_CLCD_PL_77_78_79_VAL,
	}, {
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_80_TO_85_MASK | PMX_PL_86_87_MASK |
			PMX_PL_88_89_MASK,
		.val = PMX_CLCD_PL_80_TO_85_VAL | PMX_CLCD_PL_86_87_VAL |
			PMX_CLCD_PL_88_89_VAL,
	}, {
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_90_91_MASK | PMX_PL_92_93_MASK |
			PMX_PL_94_95_MASK | PMX_PL_96_97_MASK | PMX_PL_98_MASK,
		.val = PMX_CLCD_PL_90_91_VAL | PMX_CLCD_PL_92_93_VAL |
			PMX_CLCD_PL_94_95_VAL | PMX_CLCD_PL_96_97_VAL |
			PMX_CLCD_PL_98_VAL,
	},
};

static struct spear_modemux clcd_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = clcd_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_muxreg),
	},
};

static struct spear_pingroup clcd_pingroup = {
	.name = "clcd_grp",
	.pins = clcd_pins,
	.npins = ARRAY_SIZE(clcd_pins),
	.modemuxs = clcd_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_modemux),
};

static const char *const clcd_grps[] = { "clcd_grp" };
static struct spear_function clcd_function = {
	.name = "clcd",
	.groups = clcd_grps,
	.ngroups = ARRAY_SIZE(clcd_grps),
};

/* Pad multiplexing for EMI (Parallel NOR flash) device */
static const unsigned emi_pins[] = { 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,
	75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92,
	93, 94, 95, 96, 97 };
static struct spear_muxreg emi_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_muxreg emi_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_46_47_MASK | PMX_PL_48_49_MASK,
		.val = PMX_FSMC_EMI_PL_46_47_VAL | PMX_FSMC_EMI_PL_48_49_VAL,
	}, {
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_50_51_MASK | PMX_PL_52_53_MASK |
			PMX_PL_54_55_56_MASK | PMX_PL_58_59_MASK,
		.val = PMX_EMI_PL_50_51_VAL | PMX_EMI_PL_52_53_VAL |
			PMX_FSMC_EMI_PL_54_55_56_VAL |
			PMX_FSMC_EMI_PL_58_59_VAL,
	}, {
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_69_MASK,
		.val = PMX_EMI_PL_69_VAL,
	}, {
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_70_MASK | PMX_PL_71_72_MASK | PMX_PL_73_MASK |
			PMX_PL_74_MASK | PMX_PL_75_76_MASK |
			PMX_PL_77_78_79_MASK,
		.val = PMX_FSMC_EMI_PL_70_VAL | PMX_FSMC_EMI_PL_71_72_VAL |
			PMX_FSMC_EMI_PL_73_VAL | PMX_EMI_PL_74_VAL |
			PMX_EMI_PL_75_76_VAL | PMX_EMI_PL_77_78_79_VAL,
	}, {
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_80_TO_85_MASK | PMX_PL_86_87_MASK |
			PMX_PL_88_89_MASK,
		.val = PMX_EMI_PL_80_TO_85_VAL | PMX_EMI_PL_86_87_VAL |
			PMX_EMI_PL_88_89_VAL,
	}, {
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_90_91_MASK | PMX_PL_92_93_MASK |
			PMX_PL_94_95_MASK | PMX_PL_96_97_MASK,
		.val = PMX_EMI1_PL_90_91_VAL | PMX_EMI1_PL_92_93_VAL |
			PMX_EMI1_PL_94_95_VAL | PMX_EMI1_PL_96_97_VAL,
	}, {
		.reg = EXT_CTRL_REG,
		.mask = EMI_FSMC_DYNAMIC_MUX_MASK,
		.val = EMI_FSMC_DYNAMIC_MUX_MASK,
	},
};

static struct spear_modemux emi_modemux[] = {
	{
		.modes = AUTO_EXP_MODE | EXTENDED_MODE,
		.muxregs = emi_muxreg,
		.nmuxregs = ARRAY_SIZE(emi_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = emi_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(emi_ext_muxreg),
	},
};

static struct spear_pingroup emi_pingroup = {
	.name = "emi_grp",
	.pins = emi_pins,
	.npins = ARRAY_SIZE(emi_pins),
	.modemuxs = emi_modemux,
	.nmodemuxs = ARRAY_SIZE(emi_modemux),
};

static const char *const emi_grps[] = { "emi_grp" };
static struct spear_function emi_function = {
	.name = "emi",
	.groups = emi_grps,
	.ngroups = ARRAY_SIZE(emi_grps),
};

/* Pad multiplexing for FSMC (NAND flash) device */
static const unsigned fsmc_8bit_pins[] = { 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, 64, 65, 66, 67, 68 };
static struct spear_muxreg fsmc_8bit_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_52_53_MASK | PMX_PL_54_55_56_MASK |
			PMX_PL_57_MASK | PMX_PL_58_59_MASK,
		.val = PMX_FSMC_PL_52_53_VAL | PMX_FSMC_EMI_PL_54_55_56_VAL |
			PMX_FSMC_PL_57_VAL | PMX_FSMC_EMI_PL_58_59_VAL,
	}, {
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_60_MASK | PMX_PL_61_TO_64_MASK |
			PMX_PL_65_TO_68_MASK,
		.val = PMX_FSMC_PL_60_VAL | PMX_FSMC_PL_61_TO_64_VAL |
			PMX_FSMC_PL_65_TO_68_VAL,
	}, {
		.reg = EXT_CTRL_REG,
		.mask = EMI_FSMC_DYNAMIC_MUX_MASK,
		.val = EMI_FSMC_DYNAMIC_MUX_MASK,
	},
};

static struct spear_modemux fsmc_8bit_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = fsmc_8bit_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_8bit_muxreg),
	},
};

static struct spear_pingroup fsmc_8bit_pingroup = {
	.name = "fsmc_8bit_grp",
	.pins = fsmc_8bit_pins,
	.npins = ARRAY_SIZE(fsmc_8bit_pins),
	.modemuxs = fsmc_8bit_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_8bit_modemux),
};

static const unsigned fsmc_16bit_pins[] = { 46, 47, 48, 49, 52, 53, 54, 55, 56,
	57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73 };
static struct spear_muxreg fsmc_16bit_autoexp_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_muxreg fsmc_16bit_muxreg[] = {
	{
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_46_47_MASK | PMX_PL_48_49_MASK,
		.val = PMX_FSMC_EMI_PL_46_47_VAL | PMX_FSMC_EMI_PL_48_49_VAL,
	}, {
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_70_MASK | PMX_PL_71_72_MASK | PMX_PL_73_MASK,
		.val = PMX_FSMC_EMI_PL_70_VAL | PMX_FSMC_EMI_PL_71_72_VAL |
			PMX_FSMC_EMI_PL_73_VAL,
	}
};

static struct spear_modemux fsmc_16bit_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = fsmc_8bit_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_8bit_muxreg),
	}, {
		.modes = AUTO_EXP_MODE | EXTENDED_MODE,
		.muxregs = fsmc_16bit_autoexp_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_16bit_autoexp_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = fsmc_16bit_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_16bit_muxreg),
	},
};

static struct spear_pingroup fsmc_16bit_pingroup = {
	.name = "fsmc_16bit_grp",
	.pins = fsmc_16bit_pins,
	.npins = ARRAY_SIZE(fsmc_16bit_pins),
	.modemuxs = fsmc_16bit_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_16bit_modemux),
};

static const char *const fsmc_grps[] = { "fsmc_8bit_grp", "fsmc_16bit_grp" };
static struct spear_function fsmc_function = {
	.name = "fsmc",
	.groups = fsmc_grps,
	.ngroups = ARRAY_SIZE(fsmc_grps),
};

/* Pad multiplexing for SPP device */
static const unsigned spp_pins[] = { 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	80, 81, 82, 83, 84, 85 };
static struct spear_muxreg spp_muxreg[] = {
	{
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_69_MASK,
		.val = PMX_SPP_PL_69_VAL,
	}, {
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_70_MASK | PMX_PL_71_72_MASK | PMX_PL_73_MASK |
			PMX_PL_74_MASK | PMX_PL_75_76_MASK |
			PMX_PL_77_78_79_MASK,
		.val = PMX_SPP_PL_70_VAL | PMX_SPP_PL_71_72_VAL |
			PMX_SPP_PL_73_VAL | PMX_SPP_PL_74_VAL |
			PMX_SPP_PL_75_76_VAL | PMX_SPP_PL_77_78_79_VAL,
	}, {
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_80_TO_85_MASK,
		.val = PMX_SPP_PL_80_TO_85_VAL,
	},
};

static struct spear_modemux spp_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = spp_muxreg,
		.nmuxregs = ARRAY_SIZE(spp_muxreg),
	},
};

static struct spear_pingroup spp_pingroup = {
	.name = "spp_grp",
	.pins = spp_pins,
	.npins = ARRAY_SIZE(spp_pins),
	.modemuxs = spp_modemux,
	.nmodemuxs = ARRAY_SIZE(spp_modemux),
};

static const char *const spp_grps[] = { "spp_grp" };
static struct spear_function spp_function = {
	.name = "spp",
	.groups = spp_grps,
	.ngroups = ARRAY_SIZE(spp_grps),
};

/* Pad multiplexing for SDHCI device */
static const unsigned sdhci_led_pins[] = { 34 };
static struct spear_muxreg sdhci_led_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_muxreg sdhci_led_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_34_MASK,
		.val = PMX_PWM2_PL_34_VAL,
	},
};

static struct spear_modemux sdhci_led_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | EXTENDED_MODE,
		.muxregs = sdhci_led_muxreg,
		.nmuxregs = ARRAY_SIZE(sdhci_led_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = sdhci_led_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(sdhci_led_ext_muxreg),
	},
};

static struct spear_pingroup sdhci_led_pingroup = {
	.name = "sdhci_led_grp",
	.pins = sdhci_led_pins,
	.npins = ARRAY_SIZE(sdhci_led_pins),
	.modemuxs = sdhci_led_modemux,
	.nmodemuxs = ARRAY_SIZE(sdhci_led_modemux),
};

static const unsigned sdhci_cd_12_pins[] = { 12, 43, 44, 45, 46, 47, 48, 49,
	50};
static const unsigned sdhci_cd_51_pins[] = { 43, 44, 45, 46, 47, 48, 49, 50, 51
};
static struct spear_muxreg sdhci_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	},
};

static struct spear_muxreg sdhci_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_43_MASK | PMX_PL_44_45_MASK | PMX_PL_46_47_MASK |
			PMX_PL_48_49_MASK,
		.val = PMX_SDHCI_PL_43_VAL | PMX_SDHCI_PL_44_45_VAL |
			PMX_SDHCI_PL_46_47_VAL | PMX_SDHCI_PL_48_49_VAL,
	}, {
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_50_MASK,
		.val = PMX_SDHCI_PL_50_VAL,
	}, {
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_99_MASK,
		.val = PMX_SDHCI_PL_99_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_PL_100_101_MASK,
		.val = PMX_SDHCI_PL_100_101_VAL,
	},
};

static struct spear_muxreg sdhci_cd_12_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_12_MASK,
		.val = PMX_SDHCI_CD_PL_12_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SDHCI_CD_PORT_SEL_MASK,
		.val = PMX_SDHCI_CD_PORT_12_VAL,
	},
};

static struct spear_muxreg sdhci_cd_51_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_51_MASK,
		.val = PMX_SDHCI_CD_PL_51_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SDHCI_CD_PORT_SEL_MASK,
		.val = PMX_SDHCI_CD_PORT_51_VAL,
	},
};

#define pmx_sdhci_common_modemux					\
	{								\
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE |	\
			SMALL_PRINTERS_MODE | EXTENDED_MODE,		\
		.muxregs = sdhci_muxreg,				\
		.nmuxregs = ARRAY_SIZE(sdhci_muxreg),			\
	}, {								\
		.modes = EXTENDED_MODE,					\
		.muxregs = sdhci_ext_muxreg,				\
		.nmuxregs = ARRAY_SIZE(sdhci_ext_muxreg),		\
	}

static struct spear_modemux sdhci_modemux[][3] = {
	{
		/* select pin 12 for cd */
		pmx_sdhci_common_modemux,
		{
			.modes = EXTENDED_MODE,
			.muxregs = sdhci_cd_12_muxreg,
			.nmuxregs = ARRAY_SIZE(sdhci_cd_12_muxreg),
		},
	}, {
		/* select pin 51 for cd */
		pmx_sdhci_common_modemux,
		{
			.modes = EXTENDED_MODE,
			.muxregs = sdhci_cd_51_muxreg,
			.nmuxregs = ARRAY_SIZE(sdhci_cd_51_muxreg),
		},
	}
};

static struct spear_pingroup sdhci_pingroup[] = {
	{
		.name = "sdhci_cd_12_grp",
		.pins = sdhci_cd_12_pins,
		.npins = ARRAY_SIZE(sdhci_cd_12_pins),
		.modemuxs = sdhci_modemux[0],
		.nmodemuxs = ARRAY_SIZE(sdhci_modemux[0]),
	}, {
		.name = "sdhci_cd_51_grp",
		.pins = sdhci_cd_51_pins,
		.npins = ARRAY_SIZE(sdhci_cd_51_pins),
		.modemuxs = sdhci_modemux[1],
		.nmodemuxs = ARRAY_SIZE(sdhci_modemux[1]),
	},
};

static const char *const sdhci_grps[] = { "sdhci_cd_12_grp", "sdhci_cd_51_grp",
	"sdhci_led_grp" };

static struct spear_function sdhci_function = {
	.name = "sdhci",
	.groups = sdhci_grps,
	.ngroups = ARRAY_SIZE(sdhci_grps),
};

/* Pad multiplexing for I2S device */
static const unsigned i2s_pins[] = { 35, 39, 40, 41, 42 };
static struct spear_muxreg i2s_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	}, {
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_muxreg i2s_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_35_MASK | PMX_PL_39_MASK,
		.val = PMX_I2S_REF_CLK_PL_35_VAL | PMX_I2S_PL_39_VAL,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_40_MASK | PMX_PL_41_42_MASK,
		.val = PMX_I2S_PL_40_VAL | PMX_I2S_PL_41_42_VAL,
	},
};

static struct spear_modemux i2s_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | EXTENDED_MODE,
		.muxregs = i2s_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = i2s_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s_ext_muxreg),
	},
};

static struct spear_pingroup i2s_pingroup = {
	.name = "i2s_grp",
	.pins = i2s_pins,
	.npins = ARRAY_SIZE(i2s_pins),
	.modemuxs = i2s_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s_modemux),
};

static const char *const i2s_grps[] = { "i2s_grp" };
static struct spear_function i2s_function = {
	.name = "i2s",
	.groups = i2s_grps,
	.ngroups = ARRAY_SIZE(i2s_grps),
};

/* Pad multiplexing for UART1 device */
static const unsigned uart1_pins[] = { 28, 29 };
static struct spear_muxreg uart1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN0_MASK | PMX_GPIO_PIN1_MASK,
		.val = 0,
	},
};

static struct spear_muxreg uart1_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_28_29_MASK,
		.val = PMX_UART1_PL_28_29_VAL,
	},
};

static struct spear_modemux uart1_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE
			| SMALL_PRINTERS_MODE | EXTENDED_MODE,
		.muxregs = uart1_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = uart1_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_ext_muxreg),
	},
};

static struct spear_pingroup uart1_pingroup = {
	.name = "uart1_grp",
	.pins = uart1_pins,
	.npins = ARRAY_SIZE(uart1_pins),
	.modemuxs = uart1_modemux,
	.nmodemuxs = ARRAY_SIZE(uart1_modemux),
};

static const char *const uart1_grps[] = { "uart1_grp" };
static struct spear_function uart1_function = {
	.name = "uart1",
	.groups = uart1_grps,
	.ngroups = ARRAY_SIZE(uart1_grps),
};

/* Pad multiplexing for UART1 Modem device */
static const unsigned uart1_modem_2_to_7_pins[] = { 2, 3, 4, 5, 6, 7 };
static const unsigned uart1_modem_31_to_36_pins[] = { 31, 32, 33, 34, 35, 36 };
static const unsigned uart1_modem_34_to_45_pins[] = { 34, 35, 36, 43, 44, 45 };
static const unsigned uart1_modem_80_to_85_pins[] = { 80, 81, 82, 83, 84, 85 };

static struct spear_muxreg uart1_modem_ext_2_to_7_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MASK | PMX_I2C_MASK | PMX_SSP_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_2_3_MASK | PMX_PL_6_7_MASK,
		.val = PMX_UART1_ENH_PL_2_3_VAL | PMX_UART1_ENH_PL_4_5_VAL |
			PMX_UART1_ENH_PL_6_7_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART1_ENH_PORT_SEL_MASK,
		.val = PMX_UART1_ENH_PORT_3_TO_5_7_VAL,
	},
};

static struct spear_muxreg uart1_modem_31_to_36_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN3_MASK | PMX_GPIO_PIN4_MASK |
			PMX_GPIO_PIN5_MASK | PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_muxreg uart1_modem_ext_31_to_36_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_31_MASK | PMX_PL_32_33_MASK | PMX_PL_34_MASK |
			PMX_PL_35_MASK | PMX_PL_36_MASK,
		.val = PMX_UART1_ENH_PL_31_VAL | PMX_UART1_ENH_PL_32_33_VAL |
			PMX_UART1_ENH_PL_34_VAL | PMX_UART1_ENH_PL_35_VAL |
			PMX_UART1_ENH_PL_36_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART1_ENH_PORT_SEL_MASK,
		.val = PMX_UART1_ENH_PORT_32_TO_34_36_VAL,
	},
};

static struct spear_muxreg uart1_modem_34_to_45_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK |
			PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_muxreg uart1_modem_ext_34_to_45_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_34_MASK | PMX_PL_35_MASK | PMX_PL_36_MASK,
		.val = PMX_UART1_ENH_PL_34_VAL | PMX_UART1_ENH_PL_35_VAL |
			PMX_UART1_ENH_PL_36_VAL,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_43_MASK | PMX_PL_44_45_MASK,
		.val = PMX_UART1_ENH_PL_43_VAL | PMX_UART1_ENH_PL_44_45_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART1_ENH_PORT_SEL_MASK,
		.val = PMX_UART1_ENH_PORT_44_45_34_36_VAL,
	},
};

static struct spear_muxreg uart1_modem_ext_80_to_85_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_80_TO_85_MASK,
		.val = PMX_UART1_ENH_PL_80_TO_85_VAL,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_43_MASK | PMX_PL_44_45_MASK,
		.val = PMX_UART1_ENH_PL_43_VAL | PMX_UART1_ENH_PL_44_45_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART1_ENH_PORT_SEL_MASK,
		.val = PMX_UART1_ENH_PORT_81_TO_85_VAL,
	},
};

static struct spear_modemux uart1_modem_2_to_7_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = uart1_modem_ext_2_to_7_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_ext_2_to_7_muxreg),
	},
};

static struct spear_modemux uart1_modem_31_to_36_modemux[] = {
	{
		.modes = SMALL_PRINTERS_MODE | EXTENDED_MODE,
		.muxregs = uart1_modem_31_to_36_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_31_to_36_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = uart1_modem_ext_31_to_36_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_ext_31_to_36_muxreg),
	},
};

static struct spear_modemux uart1_modem_34_to_45_modemux[] = {
	{
		.modes = AUTO_EXP_MODE | EXTENDED_MODE,
		.muxregs = uart1_modem_34_to_45_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_34_to_45_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = uart1_modem_ext_34_to_45_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_ext_34_to_45_muxreg),
	},
};

static struct spear_modemux uart1_modem_80_to_85_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = uart1_modem_ext_80_to_85_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_modem_ext_80_to_85_muxreg),
	},
};

static struct spear_pingroup uart1_modem_pingroup[] = {
	{
		.name = "uart1_modem_2_to_7_grp",
		.pins = uart1_modem_2_to_7_pins,
		.npins = ARRAY_SIZE(uart1_modem_2_to_7_pins),
		.modemuxs = uart1_modem_2_to_7_modemux,
		.nmodemuxs = ARRAY_SIZE(uart1_modem_2_to_7_modemux),
	}, {
		.name = "uart1_modem_31_to_36_grp",
		.pins = uart1_modem_31_to_36_pins,
		.npins = ARRAY_SIZE(uart1_modem_31_to_36_pins),
		.modemuxs = uart1_modem_31_to_36_modemux,
		.nmodemuxs = ARRAY_SIZE(uart1_modem_31_to_36_modemux),
	}, {
		.name = "uart1_modem_34_to_45_grp",
		.pins = uart1_modem_34_to_45_pins,
		.npins = ARRAY_SIZE(uart1_modem_34_to_45_pins),
		.modemuxs = uart1_modem_34_to_45_modemux,
		.nmodemuxs = ARRAY_SIZE(uart1_modem_34_to_45_modemux),
	}, {
		.name = "uart1_modem_80_to_85_grp",
		.pins = uart1_modem_80_to_85_pins,
		.npins = ARRAY_SIZE(uart1_modem_80_to_85_pins),
		.modemuxs = uart1_modem_80_to_85_modemux,
		.nmodemuxs = ARRAY_SIZE(uart1_modem_80_to_85_modemux),
	},
};

static const char *const uart1_modem_grps[] = { "uart1_modem_2_to_7_grp",
	"uart1_modem_31_to_36_grp", "uart1_modem_34_to_45_grp",
	"uart1_modem_80_to_85_grp" };
static struct spear_function uart1_modem_function = {
	.name = "uart1_modem",
	.groups = uart1_modem_grps,
	.ngroups = ARRAY_SIZE(uart1_modem_grps),
};

/* Pad multiplexing for UART2 device */
static const unsigned uart2_pins[] = { 0, 1 };
static struct spear_muxreg uart2_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_FIRDA_MASK,
		.val = 0,
	},
};

static struct spear_muxreg uart2_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_0_1_MASK,
		.val = PMX_UART2_PL_0_1_VAL,
	},
};

static struct spear_modemux uart2_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE
			| SMALL_PRINTERS_MODE | EXTENDED_MODE,
		.muxregs = uart2_muxreg,
		.nmuxregs = ARRAY_SIZE(uart2_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = uart2_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(uart2_ext_muxreg),
	},
};

static struct spear_pingroup uart2_pingroup = {
	.name = "uart2_grp",
	.pins = uart2_pins,
	.npins = ARRAY_SIZE(uart2_pins),
	.modemuxs = uart2_modemux,
	.nmodemuxs = ARRAY_SIZE(uart2_modemux),
};

static const char *const uart2_grps[] = { "uart2_grp" };
static struct spear_function uart2_function = {
	.name = "uart2",
	.groups = uart2_grps,
	.ngroups = ARRAY_SIZE(uart2_grps),
};

/* Pad multiplexing for uart3 device */
static const unsigned uart3_pins[][2] = { { 8, 9 }, { 15, 16 }, { 41, 42 },
	{ 52, 53 }, { 73, 74 }, { 94, 95 }, { 98, 99 } };

static struct spear_muxreg uart3_ext_8_9_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_8_9_MASK,
		.val = PMX_UART3_PL_8_9_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_8_VAL,
	},
};

static struct spear_muxreg uart3_ext_15_16_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_15_16_MASK,
		.val = PMX_UART3_PL_15_16_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_15_VAL,
	},
};

static struct spear_muxreg uart3_ext_41_42_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_41_42_MASK,
		.val = PMX_UART3_PL_41_42_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_41_VAL,
	},
};

static struct spear_muxreg uart3_ext_52_53_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_52_53_MASK,
		.val = PMX_UART3_PL_52_53_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_52_VAL,
	},
};

static struct spear_muxreg uart3_ext_73_74_muxreg[] = {
	{
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_73_MASK | PMX_PL_74_MASK,
		.val = PMX_UART3_PL_73_VAL | PMX_UART3_PL_74_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_73_VAL,
	},
};

static struct spear_muxreg uart3_ext_94_95_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_94_95_MASK,
		.val = PMX_UART3_PL_94_95_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_94_VAL,
	},
};

static struct spear_muxreg uart3_ext_98_99_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_98_MASK | PMX_PL_99_MASK,
		.val = PMX_UART3_PL_98_VAL | PMX_UART3_PL_99_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART3_PORT_SEL_MASK,
		.val = PMX_UART3_PORT_99_VAL,
	},
};

static struct spear_modemux uart3_modemux[][1] = {
	{
		/* Select signals on pins 8_9 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_8_9_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_8_9_muxreg),
		},
	}, {
		/* Select signals on pins 15_16 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_15_16_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_15_16_muxreg),
		},
	}, {
		/* Select signals on pins 41_42 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_41_42_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_41_42_muxreg),
		},
	}, {
		/* Select signals on pins 52_53 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_52_53_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_52_53_muxreg),
		},
	}, {
		/* Select signals on pins 73_74 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_73_74_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_73_74_muxreg),
		},
	}, {
		/* Select signals on pins 94_95 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_94_95_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_94_95_muxreg),
		},
	}, {
		/* Select signals on pins 98_99 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart3_ext_98_99_muxreg,
			.nmuxregs = ARRAY_SIZE(uart3_ext_98_99_muxreg),
		},
	},
};

static struct spear_pingroup uart3_pingroup[] = {
	{
		.name = "uart3_8_9_grp",
		.pins = uart3_pins[0],
		.npins = ARRAY_SIZE(uart3_pins[0]),
		.modemuxs = uart3_modemux[0],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[0]),
	}, {
		.name = "uart3_15_16_grp",
		.pins = uart3_pins[1],
		.npins = ARRAY_SIZE(uart3_pins[1]),
		.modemuxs = uart3_modemux[1],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[1]),
	}, {
		.name = "uart3_41_42_grp",
		.pins = uart3_pins[2],
		.npins = ARRAY_SIZE(uart3_pins[2]),
		.modemuxs = uart3_modemux[2],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[2]),
	}, {
		.name = "uart3_52_53_grp",
		.pins = uart3_pins[3],
		.npins = ARRAY_SIZE(uart3_pins[3]),
		.modemuxs = uart3_modemux[3],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[3]),
	}, {
		.name = "uart3_73_74_grp",
		.pins = uart3_pins[4],
		.npins = ARRAY_SIZE(uart3_pins[4]),
		.modemuxs = uart3_modemux[4],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[4]),
	}, {
		.name = "uart3_94_95_grp",
		.pins = uart3_pins[5],
		.npins = ARRAY_SIZE(uart3_pins[5]),
		.modemuxs = uart3_modemux[5],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[5]),
	}, {
		.name = "uart3_98_99_grp",
		.pins = uart3_pins[6],
		.npins = ARRAY_SIZE(uart3_pins[6]),
		.modemuxs = uart3_modemux[6],
		.nmodemuxs = ARRAY_SIZE(uart3_modemux[6]),
	},
};

static const char *const uart3_grps[] = { "uart3_8_9_grp", "uart3_15_16_grp",
	"uart3_41_42_grp", "uart3_52_53_grp", "uart3_73_74_grp",
	"uart3_94_95_grp", "uart3_98_99_grp" };

static struct spear_function uart3_function = {
	.name = "uart3",
	.groups = uart3_grps,
	.ngroups = ARRAY_SIZE(uart3_grps),
};

/* Pad multiplexing for uart4 device */
static const unsigned uart4_pins[][2] = { { 6, 7 }, { 13, 14 }, { 39, 40 },
	{ 71, 72 }, { 92, 93 }, { 100, 101 } };

static struct spear_muxreg uart4_ext_6_7_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_6_7_MASK,
		.val = PMX_UART4_PL_6_7_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PORT_6_VAL,
	},
};

static struct spear_muxreg uart4_ext_13_14_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_13_14_MASK,
		.val = PMX_UART4_PL_13_14_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PORT_13_VAL,
	},
};

static struct spear_muxreg uart4_ext_39_40_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_39_MASK,
		.val = PMX_UART4_PL_39_VAL,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_40_MASK,
		.val = PMX_UART4_PL_40_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PORT_39_VAL,
	},
};

static struct spear_muxreg uart4_ext_71_72_muxreg[] = {
	{
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_71_72_MASK,
		.val = PMX_UART4_PL_71_72_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PORT_71_VAL,
	},
};

static struct spear_muxreg uart4_ext_92_93_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_92_93_MASK,
		.val = PMX_UART4_PL_92_93_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PORT_92_VAL,
	},
};

static struct spear_muxreg uart4_ext_100_101_muxreg[] = {
	{
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_PL_100_101_MASK |
			PMX_UART4_PORT_SEL_MASK,
		.val = PMX_UART4_PL_100_101_VAL |
			PMX_UART4_PORT_101_VAL,
	},
};

static struct spear_modemux uart4_modemux[][1] = {
	{
		/* Select signals on pins 6_7 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_6_7_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_6_7_muxreg),
		},
	}, {
		/* Select signals on pins 13_14 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_13_14_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_13_14_muxreg),
		},
	}, {
		/* Select signals on pins 39_40 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_39_40_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_39_40_muxreg),
		},
	}, {
		/* Select signals on pins 71_72 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_71_72_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_71_72_muxreg),
		},
	}, {
		/* Select signals on pins 92_93 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_92_93_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_92_93_muxreg),
		},
	}, {
		/* Select signals on pins 100_101_ */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart4_ext_100_101_muxreg,
			.nmuxregs = ARRAY_SIZE(uart4_ext_100_101_muxreg),
		},
	},
};

static struct spear_pingroup uart4_pingroup[] = {
	{
		.name = "uart4_6_7_grp",
		.pins = uart4_pins[0],
		.npins = ARRAY_SIZE(uart4_pins[0]),
		.modemuxs = uart4_modemux[0],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[0]),
	}, {
		.name = "uart4_13_14_grp",
		.pins = uart4_pins[1],
		.npins = ARRAY_SIZE(uart4_pins[1]),
		.modemuxs = uart4_modemux[1],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[1]),
	}, {
		.name = "uart4_39_40_grp",
		.pins = uart4_pins[2],
		.npins = ARRAY_SIZE(uart4_pins[2]),
		.modemuxs = uart4_modemux[2],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[2]),
	}, {
		.name = "uart4_71_72_grp",
		.pins = uart4_pins[3],
		.npins = ARRAY_SIZE(uart4_pins[3]),
		.modemuxs = uart4_modemux[3],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[3]),
	}, {
		.name = "uart4_92_93_grp",
		.pins = uart4_pins[4],
		.npins = ARRAY_SIZE(uart4_pins[4]),
		.modemuxs = uart4_modemux[4],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[4]),
	}, {
		.name = "uart4_100_101_grp",
		.pins = uart4_pins[5],
		.npins = ARRAY_SIZE(uart4_pins[5]),
		.modemuxs = uart4_modemux[5],
		.nmodemuxs = ARRAY_SIZE(uart4_modemux[5]),
	},
};

static const char *const uart4_grps[] = { "uart4_6_7_grp", "uart4_13_14_grp",
	"uart4_39_40_grp", "uart4_71_72_grp", "uart4_92_93_grp",
	"uart4_100_101_grp" };

static struct spear_function uart4_function = {
	.name = "uart4",
	.groups = uart4_grps,
	.ngroups = ARRAY_SIZE(uart4_grps),
};

/* Pad multiplexing for uart5 device */
static const unsigned uart5_pins[][2] = { { 4, 5 }, { 37, 38 }, { 69, 70 },
	{ 90, 91 } };

static struct spear_muxreg uart5_ext_4_5_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_I2C_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_4_5_MASK,
		.val = PMX_UART5_PL_4_5_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART5_PORT_SEL_MASK,
		.val = PMX_UART5_PORT_4_VAL,
	},
};

static struct spear_muxreg uart5_ext_37_38_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_37_38_MASK,
		.val = PMX_UART5_PL_37_38_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART5_PORT_SEL_MASK,
		.val = PMX_UART5_PORT_37_VAL,
	},
};

static struct spear_muxreg uart5_ext_69_70_muxreg[] = {
	{
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_69_MASK,
		.val = PMX_UART5_PL_69_VAL,
	}, {
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_70_MASK,
		.val = PMX_UART5_PL_70_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART5_PORT_SEL_MASK,
		.val = PMX_UART5_PORT_69_VAL,
	},
};

static struct spear_muxreg uart5_ext_90_91_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_90_91_MASK,
		.val = PMX_UART5_PL_90_91_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART5_PORT_SEL_MASK,
		.val = PMX_UART5_PORT_90_VAL,
	},
};

static struct spear_modemux uart5_modemux[][1] = {
	{
		/* Select signals on pins 4_5 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart5_ext_4_5_muxreg,
			.nmuxregs = ARRAY_SIZE(uart5_ext_4_5_muxreg),
		},
	}, {
		/* Select signals on pins 37_38 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart5_ext_37_38_muxreg,
			.nmuxregs = ARRAY_SIZE(uart5_ext_37_38_muxreg),
		},
	}, {
		/* Select signals on pins 69_70 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart5_ext_69_70_muxreg,
			.nmuxregs = ARRAY_SIZE(uart5_ext_69_70_muxreg),
		},
	}, {
		/* Select signals on pins 90_91 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart5_ext_90_91_muxreg,
			.nmuxregs = ARRAY_SIZE(uart5_ext_90_91_muxreg),
		},
	},
};

static struct spear_pingroup uart5_pingroup[] = {
	{
		.name = "uart5_4_5_grp",
		.pins = uart5_pins[0],
		.npins = ARRAY_SIZE(uart5_pins[0]),
		.modemuxs = uart5_modemux[0],
		.nmodemuxs = ARRAY_SIZE(uart5_modemux[0]),
	}, {
		.name = "uart5_37_38_grp",
		.pins = uart5_pins[1],
		.npins = ARRAY_SIZE(uart5_pins[1]),
		.modemuxs = uart5_modemux[1],
		.nmodemuxs = ARRAY_SIZE(uart5_modemux[1]),
	}, {
		.name = "uart5_69_70_grp",
		.pins = uart5_pins[2],
		.npins = ARRAY_SIZE(uart5_pins[2]),
		.modemuxs = uart5_modemux[2],
		.nmodemuxs = ARRAY_SIZE(uart5_modemux[2]),
	}, {
		.name = "uart5_90_91_grp",
		.pins = uart5_pins[3],
		.npins = ARRAY_SIZE(uart5_pins[3]),
		.modemuxs = uart5_modemux[3],
		.nmodemuxs = ARRAY_SIZE(uart5_modemux[3]),
	},
};

static const char *const uart5_grps[] = { "uart5_4_5_grp", "uart5_37_38_grp",
	"uart5_69_70_grp", "uart5_90_91_grp" };
static struct spear_function uart5_function = {
	.name = "uart5",
	.groups = uart5_grps,
	.ngroups = ARRAY_SIZE(uart5_grps),
};

/* Pad multiplexing for uart6 device */
static const unsigned uart6_pins[][2] = { { 2, 3 }, { 88, 89 } };
static struct spear_muxreg uart6_ext_2_3_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_2_3_MASK,
		.val = PMX_UART6_PL_2_3_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART6_PORT_SEL_MASK,
		.val = PMX_UART6_PORT_2_VAL,
	},
};

static struct spear_muxreg uart6_ext_88_89_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_88_89_MASK,
		.val = PMX_UART6_PL_88_89_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_UART6_PORT_SEL_MASK,
		.val = PMX_UART6_PORT_88_VAL,
	},
};

static struct spear_modemux uart6_modemux[][1] = {
	{
		/* Select signals on pins 2_3 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart6_ext_2_3_muxreg,
			.nmuxregs = ARRAY_SIZE(uart6_ext_2_3_muxreg),
		},
	}, {
		/* Select signals on pins 88_89 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = uart6_ext_88_89_muxreg,
			.nmuxregs = ARRAY_SIZE(uart6_ext_88_89_muxreg),
		},
	},
};

static struct spear_pingroup uart6_pingroup[] = {
	{
		.name = "uart6_2_3_grp",
		.pins = uart6_pins[0],
		.npins = ARRAY_SIZE(uart6_pins[0]),
		.modemuxs = uart6_modemux[0],
		.nmodemuxs = ARRAY_SIZE(uart6_modemux[0]),
	}, {
		.name = "uart6_88_89_grp",
		.pins = uart6_pins[1],
		.npins = ARRAY_SIZE(uart6_pins[1]),
		.modemuxs = uart6_modemux[1],
		.nmodemuxs = ARRAY_SIZE(uart6_modemux[1]),
	},
};

static const char *const uart6_grps[] = { "uart6_2_3_grp", "uart6_88_89_grp" };
static struct spear_function uart6_function = {
	.name = "uart6",
	.groups = uart6_grps,
	.ngroups = ARRAY_SIZE(uart6_grps),
};

/* UART - RS485 pmx */
static const unsigned rs485_pins[] = { 77, 78, 79 };
static struct spear_muxreg rs485_muxreg[] = {
	{
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_77_78_79_MASK,
		.val = PMX_RS485_PL_77_78_79_VAL,
	},
};

static struct spear_modemux rs485_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = rs485_muxreg,
		.nmuxregs = ARRAY_SIZE(rs485_muxreg),
	},
};

static struct spear_pingroup rs485_pingroup = {
	.name = "rs485_grp",
	.pins = rs485_pins,
	.npins = ARRAY_SIZE(rs485_pins),
	.modemuxs = rs485_modemux,
	.nmodemuxs = ARRAY_SIZE(rs485_modemux),
};

static const char *const rs485_grps[] = { "rs485_grp" };
static struct spear_function rs485_function = {
	.name = "rs485",
	.groups = rs485_grps,
	.ngroups = ARRAY_SIZE(rs485_grps),
};

/* Pad multiplexing for Touchscreen device */
static const unsigned touchscreen_pins[] = { 5, 36 };
static struct spear_muxreg touchscreen_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_I2C_MASK | PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_muxreg touchscreen_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_5_MASK,
		.val = PMX_TOUCH_Y_PL_5_VAL,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_36_MASK,
		.val = PMX_TOUCH_X_PL_36_VAL,
	},
};

static struct spear_modemux touchscreen_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | EXTENDED_MODE,
		.muxregs = touchscreen_muxreg,
		.nmuxregs = ARRAY_SIZE(touchscreen_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = touchscreen_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(touchscreen_ext_muxreg),
	},
};

static struct spear_pingroup touchscreen_pingroup = {
	.name = "touchscreen_grp",
	.pins = touchscreen_pins,
	.npins = ARRAY_SIZE(touchscreen_pins),
	.modemuxs = touchscreen_modemux,
	.nmodemuxs = ARRAY_SIZE(touchscreen_modemux),
};

static const char *const touchscreen_grps[] = { "touchscreen_grp" };
static struct spear_function touchscreen_function = {
	.name = "touchscreen",
	.groups = touchscreen_grps,
	.ngroups = ARRAY_SIZE(touchscreen_grps),
};

/* Pad multiplexing for CAN device */
static const unsigned can0_pins[] = { 32, 33 };
static struct spear_muxreg can0_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN4_MASK | PMX_GPIO_PIN5_MASK,
		.val = 0,
	},
};

static struct spear_muxreg can0_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_32_33_MASK,
		.val = PMX_CAN0_PL_32_33_VAL,
	},
};

static struct spear_modemux can0_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE
			| EXTENDED_MODE,
		.muxregs = can0_muxreg,
		.nmuxregs = ARRAY_SIZE(can0_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = can0_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(can0_ext_muxreg),
	},
};

static struct spear_pingroup can0_pingroup = {
	.name = "can0_grp",
	.pins = can0_pins,
	.npins = ARRAY_SIZE(can0_pins),
	.modemuxs = can0_modemux,
	.nmodemuxs = ARRAY_SIZE(can0_modemux),
};

static const char *const can0_grps[] = { "can0_grp" };
static struct spear_function can0_function = {
	.name = "can0",
	.groups = can0_grps,
	.ngroups = ARRAY_SIZE(can0_grps),
};

static const unsigned can1_pins[] = { 30, 31 };
static struct spear_muxreg can1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK,
		.val = 0,
	},
};

static struct spear_muxreg can1_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_30_31_MASK,
		.val = PMX_CAN1_PL_30_31_VAL,
	},
};

static struct spear_modemux can1_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | AUTO_EXP_MODE
			| EXTENDED_MODE,
		.muxregs = can1_muxreg,
		.nmuxregs = ARRAY_SIZE(can1_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = can1_ext_muxreg,
		.nmuxregs = ARRAY_SIZE(can1_ext_muxreg),
	},
};

static struct spear_pingroup can1_pingroup = {
	.name = "can1_grp",
	.pins = can1_pins,
	.npins = ARRAY_SIZE(can1_pins),
	.modemuxs = can1_modemux,
	.nmodemuxs = ARRAY_SIZE(can1_modemux),
};

static const char *const can1_grps[] = { "can1_grp" };
static struct spear_function can1_function = {
	.name = "can1",
	.groups = can1_grps,
	.ngroups = ARRAY_SIZE(can1_grps),
};

/* Pad multiplexing for PWM0_1 device */
static const unsigned pwm0_1_pins[][2] = { { 37, 38 }, { 14, 15 }, { 8, 9 },
	{ 30, 31 }, { 42, 43 }, { 59, 60 }, { 88, 89 } };

static struct spear_muxreg pwm0_1_pin_8_9_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_8_9_MASK,
		.val = PMX_PWM_0_1_PL_8_9_VAL,
	},
};

static struct spear_muxreg pwm0_1_autoexpsmallpri_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg pwm0_1_pin_14_15_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_14_MASK | PMX_PL_15_MASK,
		.val = PMX_PWM1_PL_14_VAL | PMX_PWM0_PL_15_VAL,
	},
};

static struct spear_muxreg pwm0_1_pin_30_31_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN2_MASK | PMX_GPIO_PIN3_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_30_MASK | PMX_PL_31_MASK,
		.val = PMX_PWM1_EXT_PL_30_VAL | PMX_PWM0_EXT_PL_31_VAL,
	},
};

static struct spear_muxreg pwm0_1_net_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	},
};

static struct spear_muxreg pwm0_1_pin_37_38_muxreg[] = {
	{
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_37_38_MASK,
		.val = PMX_PWM0_1_PL_37_38_VAL,
	},
};

static struct spear_muxreg pwm0_1_pin_42_43_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK | PMX_TIMER_0_1_MASK ,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_42_MASK | PMX_PL_43_MASK,
		.val = PMX_PWM1_PL_42_VAL |
			PMX_PWM0_PL_43_VAL,
	},
};

static struct spear_muxreg pwm0_1_pin_59_60_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_59_MASK,
		.val = PMX_PWM1_PL_59_VAL,
	}, {
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_60_MASK,
		.val = PMX_PWM0_PL_60_VAL,
	},
};

static struct spear_muxreg pwm0_1_pin_88_89_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_88_89_MASK,
		.val = PMX_PWM0_1_PL_88_89_VAL,
	},
};

static struct spear_modemux pwm0_1_pin_8_9_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_8_9_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_8_9_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_14_15_modemux[] = {
	{
		.modes = AUTO_EXP_MODE | SMALL_PRINTERS_MODE | EXTENDED_MODE,
		.muxregs = pwm0_1_autoexpsmallpri_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_autoexpsmallpri_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_14_15_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_14_15_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_30_31_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_30_31_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_30_31_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_37_38_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | EXTENDED_MODE,
		.muxregs = pwm0_1_net_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_net_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_37_38_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_37_38_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_42_43_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_42_43_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_42_43_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_59_60_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_59_60_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_59_60_muxreg),
	},
};

static struct spear_modemux pwm0_1_pin_88_89_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm0_1_pin_88_89_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_1_pin_88_89_muxreg),
	},
};

static struct spear_pingroup pwm0_1_pingroup[] = {
	{
		.name = "pwm0_1_pin_8_9_grp",
		.pins = pwm0_1_pins[0],
		.npins = ARRAY_SIZE(pwm0_1_pins[0]),
		.modemuxs = pwm0_1_pin_8_9_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_8_9_modemux),
	}, {
		.name = "pwm0_1_pin_14_15_grp",
		.pins = pwm0_1_pins[1],
		.npins = ARRAY_SIZE(pwm0_1_pins[1]),
		.modemuxs = pwm0_1_pin_14_15_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_14_15_modemux),
	}, {
		.name = "pwm0_1_pin_30_31_grp",
		.pins = pwm0_1_pins[2],
		.npins = ARRAY_SIZE(pwm0_1_pins[2]),
		.modemuxs = pwm0_1_pin_30_31_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_30_31_modemux),
	}, {
		.name = "pwm0_1_pin_37_38_grp",
		.pins = pwm0_1_pins[3],
		.npins = ARRAY_SIZE(pwm0_1_pins[3]),
		.modemuxs = pwm0_1_pin_37_38_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_37_38_modemux),
	}, {
		.name = "pwm0_1_pin_42_43_grp",
		.pins = pwm0_1_pins[4],
		.npins = ARRAY_SIZE(pwm0_1_pins[4]),
		.modemuxs = pwm0_1_pin_42_43_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_42_43_modemux),
	}, {
		.name = "pwm0_1_pin_59_60_grp",
		.pins = pwm0_1_pins[5],
		.npins = ARRAY_SIZE(pwm0_1_pins[5]),
		.modemuxs = pwm0_1_pin_59_60_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_59_60_modemux),
	}, {
		.name = "pwm0_1_pin_88_89_grp",
		.pins = pwm0_1_pins[6],
		.npins = ARRAY_SIZE(pwm0_1_pins[6]),
		.modemuxs = pwm0_1_pin_88_89_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm0_1_pin_88_89_modemux),
	},
};

static const char *const pwm0_1_grps[] = { "pwm0_1_pin_8_9_grp",
	"pwm0_1_pin_14_15_grp", "pwm0_1_pin_30_31_grp", "pwm0_1_pin_37_38_grp",
	"pwm0_1_pin_42_43_grp", "pwm0_1_pin_59_60_grp", "pwm0_1_pin_88_89_grp"
};

static struct spear_function pwm0_1_function = {
	.name = "pwm0_1",
	.groups = pwm0_1_grps,
	.ngroups = ARRAY_SIZE(pwm0_1_grps),
};

/* Pad multiplexing for PWM2 device */
static const unsigned pwm2_pins[][1] = { { 7 }, { 13 }, { 29 }, { 34 }, { 41 },
	{ 58 }, { 87 } };
static struct spear_muxreg pwm2_net_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	},
};

static struct spear_muxreg pwm2_pin_7_muxreg[] = {
	{
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_7_MASK,
		.val = PMX_PWM_2_PL_7_VAL,
	},
};

static struct spear_muxreg pwm2_autoexpsmallpri_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg pwm2_pin_13_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_13_MASK,
		.val = PMX_PWM2_PL_13_VAL,
	},
};

static struct spear_muxreg pwm2_pin_29_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN1_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_29_MASK,
		.val = PMX_PWM_2_PL_29_VAL,
	},
};

static struct spear_muxreg pwm2_pin_34_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	}, {
		.reg = MODE_CONFIG_REG,
		.mask = PMX_PWM_MASK,
		.val = PMX_PWM_MASK,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_34_MASK,
		.val = PMX_PWM2_PL_34_VAL,
	},
};

static struct spear_muxreg pwm2_pin_41_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_41_MASK,
		.val = PMX_PWM2_PL_41_VAL,
	},
};

static struct spear_muxreg pwm2_pin_58_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_58_MASK,
		.val = PMX_PWM2_PL_58_VAL,
	},
};

static struct spear_muxreg pwm2_pin_87_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_87_MASK,
		.val = PMX_PWM2_PL_87_VAL,
	},
};

static struct spear_modemux pwm2_pin_7_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | AUTO_NET_MII_MODE | EXTENDED_MODE,
		.muxregs = pwm2_net_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_net_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_7_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_7_muxreg),
	},
};
static struct spear_modemux pwm2_pin_13_modemux[] = {
	{
		.modes = AUTO_EXP_MODE | SMALL_PRINTERS_MODE | EXTENDED_MODE,
		.muxregs = pwm2_autoexpsmallpri_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_autoexpsmallpri_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_13_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_13_muxreg),
	},
};
static struct spear_modemux pwm2_pin_29_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_29_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_29_muxreg),
	},
};
static struct spear_modemux pwm2_pin_34_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_34_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_34_muxreg),
	},
};

static struct spear_modemux pwm2_pin_41_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_41_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_41_muxreg),
	},
};

static struct spear_modemux pwm2_pin_58_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_58_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_58_muxreg),
	},
};

static struct spear_modemux pwm2_pin_87_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm2_pin_87_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_pin_87_muxreg),
	},
};

static struct spear_pingroup pwm2_pingroup[] = {
	{
		.name = "pwm2_pin_7_grp",
		.pins = pwm2_pins[0],
		.npins = ARRAY_SIZE(pwm2_pins[0]),
		.modemuxs = pwm2_pin_7_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_7_modemux),
	}, {
		.name = "pwm2_pin_13_grp",
		.pins = pwm2_pins[1],
		.npins = ARRAY_SIZE(pwm2_pins[1]),
		.modemuxs = pwm2_pin_13_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_13_modemux),
	}, {
		.name = "pwm2_pin_29_grp",
		.pins = pwm2_pins[2],
		.npins = ARRAY_SIZE(pwm2_pins[2]),
		.modemuxs = pwm2_pin_29_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_29_modemux),
	}, {
		.name = "pwm2_pin_34_grp",
		.pins = pwm2_pins[3],
		.npins = ARRAY_SIZE(pwm2_pins[3]),
		.modemuxs = pwm2_pin_34_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_34_modemux),
	}, {
		.name = "pwm2_pin_41_grp",
		.pins = pwm2_pins[4],
		.npins = ARRAY_SIZE(pwm2_pins[4]),
		.modemuxs = pwm2_pin_41_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_41_modemux),
	}, {
		.name = "pwm2_pin_58_grp",
		.pins = pwm2_pins[5],
		.npins = ARRAY_SIZE(pwm2_pins[5]),
		.modemuxs = pwm2_pin_58_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_58_modemux),
	}, {
		.name = "pwm2_pin_87_grp",
		.pins = pwm2_pins[6],
		.npins = ARRAY_SIZE(pwm2_pins[6]),
		.modemuxs = pwm2_pin_87_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm2_pin_87_modemux),
	},
};

static const char *const pwm2_grps[] = { "pwm2_pin_7_grp", "pwm2_pin_13_grp",
	"pwm2_pin_29_grp", "pwm2_pin_34_grp", "pwm2_pin_41_grp",
	"pwm2_pin_58_grp", "pwm2_pin_87_grp" };
static struct spear_function pwm2_function = {
	.name = "pwm2",
	.groups = pwm2_grps,
	.ngroups = ARRAY_SIZE(pwm2_grps),
};

/* Pad multiplexing for PWM3 device */
static const unsigned pwm3_pins[][1] = { { 6 }, { 12 }, { 28 }, { 40 }, { 57 },
	{ 86 } };
static struct spear_muxreg pwm3_pin_6_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_6_MASK,
		.val = PMX_PWM_3_PL_6_VAL,
	},
};

static struct spear_muxreg pwm3_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg pwm3_pin_12_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_12_MASK,
		.val = PMX_PWM3_PL_12_VAL,
	},
};

static struct spear_muxreg pwm3_pin_28_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_GPIO_PIN0_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_28_MASK,
		.val = PMX_PWM_3_PL_28_VAL,
	},
};

static struct spear_muxreg pwm3_pin_40_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_40_MASK,
		.val = PMX_PWM3_PL_40_VAL,
	},
};

static struct spear_muxreg pwm3_pin_57_muxreg[] = {
	{
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_57_MASK,
		.val = PMX_PWM3_PL_57_VAL,
	},
};

static struct spear_muxreg pwm3_pin_86_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_86_MASK,
		.val = PMX_PWM3_PL_86_VAL,
	},
};

static struct spear_modemux pwm3_pin_6_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_6_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_6_muxreg),
	},
};

static struct spear_modemux pwm3_pin_12_modemux[] = {
	{
		.modes = AUTO_EXP_MODE | SMALL_PRINTERS_MODE |
			AUTO_NET_SMII_MODE | EXTENDED_MODE,
		.muxregs = pwm3_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_12_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_12_muxreg),
	},
};

static struct spear_modemux pwm3_pin_28_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_28_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_28_muxreg),
	},
};

static struct spear_modemux pwm3_pin_40_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_40_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_40_muxreg),
	},
};

static struct spear_modemux pwm3_pin_57_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_57_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_57_muxreg),
	},
};

static struct spear_modemux pwm3_pin_86_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = pwm3_pin_86_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_pin_86_muxreg),
	},
};

static struct spear_pingroup pwm3_pingroup[] = {
	{
		.name = "pwm3_pin_6_grp",
		.pins = pwm3_pins[0],
		.npins = ARRAY_SIZE(pwm3_pins[0]),
		.modemuxs = pwm3_pin_6_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_6_modemux),
	}, {
		.name = "pwm3_pin_12_grp",
		.pins = pwm3_pins[1],
		.npins = ARRAY_SIZE(pwm3_pins[1]),
		.modemuxs = pwm3_pin_12_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_12_modemux),
	}, {
		.name = "pwm3_pin_28_grp",
		.pins = pwm3_pins[2],
		.npins = ARRAY_SIZE(pwm3_pins[2]),
		.modemuxs = pwm3_pin_28_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_28_modemux),
	}, {
		.name = "pwm3_pin_40_grp",
		.pins = pwm3_pins[3],
		.npins = ARRAY_SIZE(pwm3_pins[3]),
		.modemuxs = pwm3_pin_40_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_40_modemux),
	}, {
		.name = "pwm3_pin_57_grp",
		.pins = pwm3_pins[4],
		.npins = ARRAY_SIZE(pwm3_pins[4]),
		.modemuxs = pwm3_pin_57_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_57_modemux),
	}, {
		.name = "pwm3_pin_86_grp",
		.pins = pwm3_pins[5],
		.npins = ARRAY_SIZE(pwm3_pins[5]),
		.modemuxs = pwm3_pin_86_modemux,
		.nmodemuxs = ARRAY_SIZE(pwm3_pin_86_modemux),
	},
};

static const char *const pwm3_grps[] = { "pwm3_pin_6_grp", "pwm3_pin_12_grp",
	"pwm3_pin_28_grp", "pwm3_pin_40_grp", "pwm3_pin_57_grp",
	"pwm3_pin_86_grp" };
static struct spear_function pwm3_function = {
	.name = "pwm3",
	.groups = pwm3_grps,
	.ngroups = ARRAY_SIZE(pwm3_grps),
};

/* Pad multiplexing for SSP1 device */
static const unsigned ssp1_pins[][2] = { { 17, 20 }, { 36, 39 }, { 48, 51 },
	{ 65, 68 }, { 94, 97 } };
static struct spear_muxreg ssp1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg ssp1_ext_17_20_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_17_18_MASK | PMX_PL_19_MASK,
		.val = PMX_SSP1_PL_17_18_19_20_VAL,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_20_MASK,
		.val = PMX_SSP1_PL_17_18_19_20_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP1_PORT_SEL_MASK,
		.val = PMX_SSP1_PORT_17_TO_20_VAL,
	},
};

static struct spear_muxreg ssp1_ext_36_39_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MODEM_MASK | PMX_SSP_CS_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_36_MASK | PMX_PL_37_38_MASK | PMX_PL_39_MASK,
		.val = PMX_SSP1_PL_36_VAL | PMX_SSP1_PL_37_38_VAL |
			PMX_SSP1_PL_39_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP1_PORT_SEL_MASK,
		.val = PMX_SSP1_PORT_36_TO_39_VAL,
	},
};

static struct spear_muxreg ssp1_ext_48_51_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_48_49_MASK,
		.val = PMX_SSP1_PL_48_49_VAL,
	}, {
		.reg = IP_SEL_PAD_50_59_REG,
		.mask = PMX_PL_50_51_MASK,
		.val = PMX_SSP1_PL_50_51_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP1_PORT_SEL_MASK,
		.val = PMX_SSP1_PORT_48_TO_51_VAL,
	},
};

static struct spear_muxreg ssp1_ext_65_68_muxreg[] = {
	{
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_65_TO_68_MASK,
		.val = PMX_SSP1_PL_65_TO_68_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP1_PORT_SEL_MASK,
		.val = PMX_SSP1_PORT_65_TO_68_VAL,
	},
};

static struct spear_muxreg ssp1_ext_94_97_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_94_95_MASK | PMX_PL_96_97_MASK,
		.val = PMX_SSP1_PL_94_95_VAL | PMX_SSP1_PL_96_97_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP1_PORT_SEL_MASK,
		.val = PMX_SSP1_PORT_94_TO_97_VAL,
	},
};

static struct spear_modemux ssp1_17_20_modemux[] = {
	{
		.modes = SMALL_PRINTERS_MODE | AUTO_NET_SMII_MODE |
			EXTENDED_MODE,
		.muxregs = ssp1_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = ssp1_ext_17_20_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_ext_17_20_muxreg),
	},
};

static struct spear_modemux ssp1_36_39_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp1_ext_36_39_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_ext_36_39_muxreg),
	},
};

static struct spear_modemux ssp1_48_51_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp1_ext_48_51_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_ext_48_51_muxreg),
	},
};
static struct spear_modemux ssp1_65_68_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp1_ext_65_68_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_ext_65_68_muxreg),
	},
};

static struct spear_modemux ssp1_94_97_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp1_ext_94_97_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_ext_94_97_muxreg),
	},
};

static struct spear_pingroup ssp1_pingroup[] = {
	{
		.name = "ssp1_17_20_grp",
		.pins = ssp1_pins[0],
		.npins = ARRAY_SIZE(ssp1_pins[0]),
		.modemuxs = ssp1_17_20_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp1_17_20_modemux),
	}, {
		.name = "ssp1_36_39_grp",
		.pins = ssp1_pins[1],
		.npins = ARRAY_SIZE(ssp1_pins[1]),
		.modemuxs = ssp1_36_39_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp1_36_39_modemux),
	}, {
		.name = "ssp1_48_51_grp",
		.pins = ssp1_pins[2],
		.npins = ARRAY_SIZE(ssp1_pins[2]),
		.modemuxs = ssp1_48_51_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp1_48_51_modemux),
	}, {
		.name = "ssp1_65_68_grp",
		.pins = ssp1_pins[3],
		.npins = ARRAY_SIZE(ssp1_pins[3]),
		.modemuxs = ssp1_65_68_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp1_65_68_modemux),
	}, {
		.name = "ssp1_94_97_grp",
		.pins = ssp1_pins[4],
		.npins = ARRAY_SIZE(ssp1_pins[4]),
		.modemuxs = ssp1_94_97_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp1_94_97_modemux),
	},
};

static const char *const ssp1_grps[] = { "ssp1_17_20_grp", "ssp1_36_39_grp",
	"ssp1_48_51_grp", "ssp1_65_68_grp", "ssp1_94_97_grp"
};
static struct spear_function ssp1_function = {
	.name = "ssp1",
	.groups = ssp1_grps,
	.ngroups = ARRAY_SIZE(ssp1_grps),
};

/* Pad multiplexing for SSP2 device */
static const unsigned ssp2_pins[][2] = { { 13, 16 }, { 32, 35 }, { 44, 47 },
	{ 61, 64 }, { 90, 93 } };
static struct spear_muxreg ssp2_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg ssp2_ext_13_16_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_13_14_MASK | PMX_PL_15_16_MASK,
		.val = PMX_SSP2_PL_13_14_15_16_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP2_PORT_SEL_MASK,
		.val = PMX_SSP2_PORT_13_TO_16_VAL,
	},
};

static struct spear_muxreg ssp2_ext_32_35_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK | PMX_GPIO_PIN4_MASK |
			PMX_GPIO_PIN5_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_30_39_REG,
		.mask = PMX_PL_32_33_MASK | PMX_PL_34_MASK | PMX_PL_35_MASK,
		.val = PMX_SSP2_PL_32_33_VAL | PMX_SSP2_PL_34_VAL |
			PMX_SSP2_PL_35_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP2_PORT_SEL_MASK,
		.val = PMX_SSP2_PORT_32_TO_35_VAL,
	},
};

static struct spear_muxreg ssp2_ext_44_47_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_TIMER_0_1_MASK | PMX_TIMER_2_3_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_40_49_REG,
		.mask = PMX_PL_44_45_MASK | PMX_PL_46_47_MASK,
		.val = PMX_SSP2_PL_44_45_VAL | PMX_SSP2_PL_46_47_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP2_PORT_SEL_MASK,
		.val = PMX_SSP2_PORT_44_TO_47_VAL,
	},
};

static struct spear_muxreg ssp2_ext_61_64_muxreg[] = {
	{
		.reg = IP_SEL_PAD_60_69_REG,
		.mask = PMX_PL_61_TO_64_MASK,
		.val = PMX_SSP2_PL_61_TO_64_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP2_PORT_SEL_MASK,
		.val = PMX_SSP2_PORT_61_TO_64_VAL,
	},
};

static struct spear_muxreg ssp2_ext_90_93_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_90_91_MASK | PMX_PL_92_93_MASK,
		.val = PMX_SSP2_PL_90_91_VAL | PMX_SSP2_PL_92_93_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_SSP2_PORT_SEL_MASK,
		.val = PMX_SSP2_PORT_90_TO_93_VAL,
	},
};

static struct spear_modemux ssp2_13_16_modemux[] = {
	{
		.modes = AUTO_NET_SMII_MODE | EXTENDED_MODE,
		.muxregs = ssp2_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_muxreg),
	}, {
		.modes = EXTENDED_MODE,
		.muxregs = ssp2_ext_13_16_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_ext_13_16_muxreg),
	},
};

static struct spear_modemux ssp2_32_35_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp2_ext_32_35_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_ext_32_35_muxreg),
	},
};

static struct spear_modemux ssp2_44_47_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp2_ext_44_47_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_ext_44_47_muxreg),
	},
};

static struct spear_modemux ssp2_61_64_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp2_ext_61_64_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_ext_61_64_muxreg),
	},
};

static struct spear_modemux ssp2_90_93_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = ssp2_ext_90_93_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp2_ext_90_93_muxreg),
	},
};

static struct spear_pingroup ssp2_pingroup[] = {
	{
		.name = "ssp2_13_16_grp",
		.pins = ssp2_pins[0],
		.npins = ARRAY_SIZE(ssp2_pins[0]),
		.modemuxs = ssp2_13_16_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp2_13_16_modemux),
	}, {
		.name = "ssp2_32_35_grp",
		.pins = ssp2_pins[1],
		.npins = ARRAY_SIZE(ssp2_pins[1]),
		.modemuxs = ssp2_32_35_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp2_32_35_modemux),
	}, {
		.name = "ssp2_44_47_grp",
		.pins = ssp2_pins[2],
		.npins = ARRAY_SIZE(ssp2_pins[2]),
		.modemuxs = ssp2_44_47_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp2_44_47_modemux),
	}, {
		.name = "ssp2_61_64_grp",
		.pins = ssp2_pins[3],
		.npins = ARRAY_SIZE(ssp2_pins[3]),
		.modemuxs = ssp2_61_64_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp2_61_64_modemux),
	}, {
		.name = "ssp2_90_93_grp",
		.pins = ssp2_pins[4],
		.npins = ARRAY_SIZE(ssp2_pins[4]),
		.modemuxs = ssp2_90_93_modemux,
		.nmodemuxs = ARRAY_SIZE(ssp2_90_93_modemux),
	},
};

static const char *const ssp2_grps[] = { "ssp2_13_16_grp", "ssp2_32_35_grp",
	"ssp2_44_47_grp", "ssp2_61_64_grp", "ssp2_90_93_grp" };
static struct spear_function ssp2_function = {
	.name = "ssp2",
	.groups = ssp2_grps,
	.ngroups = ARRAY_SIZE(ssp2_grps),
};

/* Pad multiplexing for cadence mii2 as mii device */
static const unsigned mii2_pins[] = { 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
	90, 91, 92, 93, 94, 95, 96, 97 };
static struct spear_muxreg mii2_muxreg[] = {
	{
		.reg = IP_SEL_PAD_80_89_REG,
		.mask = PMX_PL_80_TO_85_MASK | PMX_PL_86_87_MASK |
			PMX_PL_88_89_MASK,
		.val = PMX_MII2_PL_80_TO_85_VAL | PMX_MII2_PL_86_87_VAL |
			PMX_MII2_PL_88_89_VAL,
	}, {
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_90_91_MASK | PMX_PL_92_93_MASK |
			PMX_PL_94_95_MASK | PMX_PL_96_97_MASK,
		.val = PMX_MII2_PL_90_91_VAL | PMX_MII2_PL_92_93_VAL |
			PMX_MII2_PL_94_95_VAL | PMX_MII2_PL_96_97_VAL,
	}, {
		.reg = EXT_CTRL_REG,
		.mask = (MAC_MODE_MASK << MAC2_MODE_SHIFT) |
			(MAC_MODE_MASK << MAC1_MODE_SHIFT) |
			MII_MDIO_MASK,
		.val = (MAC_MODE_MII << MAC2_MODE_SHIFT) |
			(MAC_MODE_MII << MAC1_MODE_SHIFT) |
			MII_MDIO_81_VAL,
	},
};

static struct spear_modemux mii2_modemux[] = {
	{
		.modes = EXTENDED_MODE,
		.muxregs = mii2_muxreg,
		.nmuxregs = ARRAY_SIZE(mii2_muxreg),
	},
};

static struct spear_pingroup mii2_pingroup = {
	.name = "mii2_grp",
	.pins = mii2_pins,
	.npins = ARRAY_SIZE(mii2_pins),
	.modemuxs = mii2_modemux,
	.nmodemuxs = ARRAY_SIZE(mii2_modemux),
};

static const char *const mii2_grps[] = { "mii2_grp" };
static struct spear_function mii2_function = {
	.name = "mii2",
	.groups = mii2_grps,
	.ngroups = ARRAY_SIZE(mii2_grps),
};

/* Pad multiplexing for cadence mii 1_2 as smii or rmii device */
static const unsigned rmii0_1_pins[] = { 10, 11, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27 };
static const unsigned smii0_1_pins[] = { 10, 11, 21, 22, 23, 24, 25, 26, 27 };
static struct spear_muxreg mii0_1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	},
};

static struct spear_muxreg smii0_1_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_10_11_MASK,
		.val = PMX_SMII_PL_10_11_VAL,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_21_TO_27_MASK,
		.val = PMX_SMII_PL_21_TO_27_VAL,
	}, {
		.reg = EXT_CTRL_REG,
		.mask = (MAC_MODE_MASK << MAC2_MODE_SHIFT) |
			(MAC_MODE_MASK << MAC1_MODE_SHIFT) |
			MII_MDIO_MASK,
		.val = (MAC_MODE_SMII << MAC2_MODE_SHIFT)
			| (MAC_MODE_SMII << MAC1_MODE_SHIFT)
			| MII_MDIO_10_11_VAL,
	},
};

static struct spear_muxreg rmii0_1_ext_muxreg[] = {
	{
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_10_11_MASK | PMX_PL_13_14_MASK |
			PMX_PL_15_16_MASK | PMX_PL_17_18_MASK | PMX_PL_19_MASK,
		.val = PMX_RMII_PL_10_11_VAL | PMX_RMII_PL_13_14_VAL |
			PMX_RMII_PL_15_16_VAL | PMX_RMII_PL_17_18_VAL |
			PMX_RMII_PL_19_VAL,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_20_MASK | PMX_PL_21_TO_27_MASK,
		.val = PMX_RMII_PL_20_VAL | PMX_RMII_PL_21_TO_27_VAL,
	}, {
		.reg = EXT_CTRL_REG,
		.mask = (MAC_MODE_MASK << MAC2_MODE_SHIFT) |
			(MAC_MODE_MASK << MAC1_MODE_SHIFT) |
			MII_MDIO_MASK,
		.val = (MAC_MODE_RMII << MAC2_MODE_SHIFT)
			| (MAC_MODE_RMII << MAC1_MODE_SHIFT)
			| MII_MDIO_10_11_VAL,
	},
};

static struct spear_modemux mii0_1_modemux[][2] = {
	{
		/* configure as smii */
		{
			.modes = AUTO_NET_SMII_MODE | AUTO_EXP_MODE |
				SMALL_PRINTERS_MODE | EXTENDED_MODE,
			.muxregs = mii0_1_muxreg,
			.nmuxregs = ARRAY_SIZE(mii0_1_muxreg),
		}, {
			.modes = EXTENDED_MODE,
			.muxregs = smii0_1_ext_muxreg,
			.nmuxregs = ARRAY_SIZE(smii0_1_ext_muxreg),
		},
	}, {
		/* configure as rmii */
		{
			.modes = AUTO_NET_SMII_MODE | AUTO_EXP_MODE |
				SMALL_PRINTERS_MODE | EXTENDED_MODE,
			.muxregs = mii0_1_muxreg,
			.nmuxregs = ARRAY_SIZE(mii0_1_muxreg),
		}, {
			.modes = EXTENDED_MODE,
			.muxregs = rmii0_1_ext_muxreg,
			.nmuxregs = ARRAY_SIZE(rmii0_1_ext_muxreg),
		},
	},
};

static struct spear_pingroup mii0_1_pingroup[] = {
	{
		.name = "smii0_1_grp",
		.pins = smii0_1_pins,
		.npins = ARRAY_SIZE(smii0_1_pins),
		.modemuxs = mii0_1_modemux[0],
		.nmodemuxs = ARRAY_SIZE(mii0_1_modemux[0]),
	}, {
		.name = "rmii0_1_grp",
		.pins = rmii0_1_pins,
		.npins = ARRAY_SIZE(rmii0_1_pins),
		.modemuxs = mii0_1_modemux[1],
		.nmodemuxs = ARRAY_SIZE(mii0_1_modemux[1]),
	},
};

static const char *const mii0_1_grps[] = { "smii0_1_grp", "rmii0_1_grp" };
static struct spear_function mii0_1_function = {
	.name = "mii0_1",
	.groups = mii0_1_grps,
	.ngroups = ARRAY_SIZE(mii0_1_grps),
};

/* Pad multiplexing for i2c1 device */
static const unsigned i2c1_pins[][2] = { { 8, 9 }, { 98, 99 } };
static struct spear_muxreg i2c1_ext_8_9_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_SSP_CS_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_8_9_MASK,
		.val = PMX_I2C1_PL_8_9_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C1_PORT_SEL_MASK,
		.val = PMX_I2C1_PORT_8_9_VAL,
	},
};

static struct spear_muxreg i2c1_ext_98_99_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_98_MASK | PMX_PL_99_MASK,
		.val = PMX_I2C1_PL_98_VAL | PMX_I2C1_PL_99_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C1_PORT_SEL_MASK,
		.val = PMX_I2C1_PORT_98_99_VAL,
	},
};

static struct spear_modemux i2c1_modemux[][1] = {
	{
		/* Select signals on pins 8-9 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c1_ext_8_9_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c1_ext_8_9_muxreg),
		},
	}, {
		/* Select signals on pins 98-99 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c1_ext_98_99_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c1_ext_98_99_muxreg),
		},
	},
};

static struct spear_pingroup i2c1_pingroup[] = {
	{
		.name = "i2c1_8_9_grp",
		.pins = i2c1_pins[0],
		.npins = ARRAY_SIZE(i2c1_pins[0]),
		.modemuxs = i2c1_modemux[0],
		.nmodemuxs = ARRAY_SIZE(i2c1_modemux[0]),
	}, {
		.name = "i2c1_98_99_grp",
		.pins = i2c1_pins[1],
		.npins = ARRAY_SIZE(i2c1_pins[1]),
		.modemuxs = i2c1_modemux[1],
		.nmodemuxs = ARRAY_SIZE(i2c1_modemux[1]),
	},
};

static const char *const i2c1_grps[] = { "i2c1_8_9_grp", "i2c1_98_99_grp" };
static struct spear_function i2c1_function = {
	.name = "i2c1",
	.groups = i2c1_grps,
	.ngroups = ARRAY_SIZE(i2c1_grps),
};

/* Pad multiplexing for i2c2 device */
static const unsigned i2c2_pins[][2] = { { 0, 1 }, { 2, 3 }, { 19, 20 },
	{ 75, 76 }, { 96, 97 } };
static struct spear_muxreg i2c2_ext_0_1_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_FIRDA_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_0_1_MASK,
		.val = PMX_I2C2_PL_0_1_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C2_PORT_SEL_MASK,
		.val = PMX_I2C2_PORT_0_1_VAL,
	},
};

static struct spear_muxreg i2c2_ext_2_3_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_UART0_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_0_9_REG,
		.mask = PMX_PL_2_3_MASK,
		.val = PMX_I2C2_PL_2_3_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C2_PORT_SEL_MASK,
		.val = PMX_I2C2_PORT_2_3_VAL,
	},
};

static struct spear_muxreg i2c2_ext_19_20_muxreg[] = {
	{
		.reg = PMX_CONFIG_REG,
		.mask = PMX_MII_MASK,
		.val = 0,
	}, {
		.reg = IP_SEL_PAD_10_19_REG,
		.mask = PMX_PL_19_MASK,
		.val = PMX_I2C2_PL_19_VAL,
	}, {
		.reg = IP_SEL_PAD_20_29_REG,
		.mask = PMX_PL_20_MASK,
		.val = PMX_I2C2_PL_20_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C2_PORT_SEL_MASK,
		.val = PMX_I2C2_PORT_19_20_VAL,
	},
};

static struct spear_muxreg i2c2_ext_75_76_muxreg[] = {
	{
		.reg = IP_SEL_PAD_70_79_REG,
		.mask = PMX_PL_75_76_MASK,
		.val = PMX_I2C2_PL_75_76_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C2_PORT_SEL_MASK,
		.val = PMX_I2C2_PORT_75_76_VAL,
	},
};

static struct spear_muxreg i2c2_ext_96_97_muxreg[] = {
	{
		.reg = IP_SEL_PAD_90_99_REG,
		.mask = PMX_PL_96_97_MASK,
		.val = PMX_I2C2_PL_96_97_VAL,
	}, {
		.reg = IP_SEL_MIX_PAD_REG,
		.mask = PMX_I2C2_PORT_SEL_MASK,
		.val = PMX_I2C2_PORT_96_97_VAL,
	},
};

static struct spear_modemux i2c2_modemux[][1] = {
	{
		/* Select signals on pins 0_1 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c2_ext_0_1_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c2_ext_0_1_muxreg),
		},
	}, {
		/* Select signals on pins 2_3 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c2_ext_2_3_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c2_ext_2_3_muxreg),
		},
	}, {
		/* Select signals on pins 19_20 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c2_ext_19_20_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c2_ext_19_20_muxreg),
		},
	}, {
		/* Select signals on pins 75_76 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c2_ext_75_76_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c2_ext_75_76_muxreg),
		},
	}, {
		/* Select signals on pins 96_97 */
		{
			.modes = EXTENDED_MODE,
			.muxregs = i2c2_ext_96_97_muxreg,
			.nmuxregs = ARRAY_SIZE(i2c2_ext_96_97_muxreg),
		},
	},
};

static struct spear_pingroup i2c2_pingroup[] = {
	{
		.name = "i2c2_0_1_grp",
		.pins = i2c2_pins[0],
		.npins = ARRAY_SIZE(i2c2_pins[0]),
		.modemuxs = i2c2_modemux[0],
		.nmodemuxs = ARRAY_SIZE(i2c2_modemux[0]),
	}, {
		.name = "i2c2_2_3_grp",
		.pins = i2c2_pins[1],
		.npins = ARRAY_SIZE(i2c2_pins[1]),
		.modemuxs = i2c2_modemux[1],
		.nmodemuxs = ARRAY_SIZE(i2c2_modemux[1]),
	}, {
		.name = "i2c2_19_20_grp",
		.pins = i2c2_pins[2],
		.npins = ARRAY_SIZE(i2c2_pins[2]),
		.modemuxs = i2c2_modemux[2],
		.nmodemuxs = ARRAY_SIZE(i2c2_modemux[2]),
	}, {
		.name = "i2c2_75_76_grp",
		.pins = i2c2_pins[3],
		.npins = ARRAY_SIZE(i2c2_pins[3]),
		.modemuxs = i2c2_modemux[3],
		.nmodemuxs = ARRAY_SIZE(i2c2_modemux[3]),
	}, {
		.name = "i2c2_96_97_grp",
		.pins = i2c2_pins[4],
		.npins = ARRAY_SIZE(i2c2_pins[4]),
		.modemuxs = i2c2_modemux[4],
		.nmodemuxs = ARRAY_SIZE(i2c2_modemux[4]),
	},
};

static const char *const i2c2_grps[] = { "i2c2_0_1_grp", "i2c2_2_3_grp",
	"i2c2_19_20_grp", "i2c2_75_76_grp", "i2c2_96_97_grp" };
static struct spear_function i2c2_function = {
	.name = "i2c2",
	.groups = i2c2_grps,
	.ngroups = ARRAY_SIZE(i2c2_grps),
};

/* pingroups */
static struct spear_pingroup *spear320_pingroups[] = {
	SPEAR3XX_COMMON_PINGROUPS,
	&clcd_pingroup,
	&emi_pingroup,
	&fsmc_8bit_pingroup,
	&fsmc_16bit_pingroup,
	&spp_pingroup,
	&sdhci_led_pingroup,
	&sdhci_pingroup[0],
	&sdhci_pingroup[1],
	&i2s_pingroup,
	&uart1_pingroup,
	&uart1_modem_pingroup[0],
	&uart1_modem_pingroup[1],
	&uart1_modem_pingroup[2],
	&uart1_modem_pingroup[3],
	&uart2_pingroup,
	&uart3_pingroup[0],
	&uart3_pingroup[1],
	&uart3_pingroup[2],
	&uart3_pingroup[3],
	&uart3_pingroup[4],
	&uart3_pingroup[5],
	&uart3_pingroup[6],
	&uart4_pingroup[0],
	&uart4_pingroup[1],
	&uart4_pingroup[2],
	&uart4_pingroup[3],
	&uart4_pingroup[4],
	&uart4_pingroup[5],
	&uart5_pingroup[0],
	&uart5_pingroup[1],
	&uart5_pingroup[2],
	&uart5_pingroup[3],
	&uart6_pingroup[0],
	&uart6_pingroup[1],
	&rs485_pingroup,
	&touchscreen_pingroup,
	&can0_pingroup,
	&can1_pingroup,
	&pwm0_1_pingroup[0],
	&pwm0_1_pingroup[1],
	&pwm0_1_pingroup[2],
	&pwm0_1_pingroup[3],
	&pwm0_1_pingroup[4],
	&pwm0_1_pingroup[5],
	&pwm0_1_pingroup[6],
	&pwm2_pingroup[0],
	&pwm2_pingroup[1],
	&pwm2_pingroup[2],
	&pwm2_pingroup[3],
	&pwm2_pingroup[4],
	&pwm2_pingroup[5],
	&pwm2_pingroup[6],
	&pwm3_pingroup[0],
	&pwm3_pingroup[1],
	&pwm3_pingroup[2],
	&pwm3_pingroup[3],
	&pwm3_pingroup[4],
	&pwm3_pingroup[5],
	&ssp1_pingroup[0],
	&ssp1_pingroup[1],
	&ssp1_pingroup[2],
	&ssp1_pingroup[3],
	&ssp1_pingroup[4],
	&ssp2_pingroup[0],
	&ssp2_pingroup[1],
	&ssp2_pingroup[2],
	&ssp2_pingroup[3],
	&ssp2_pingroup[4],
	&mii2_pingroup,
	&mii0_1_pingroup[0],
	&mii0_1_pingroup[1],
	&i2c1_pingroup[0],
	&i2c1_pingroup[1],
	&i2c2_pingroup[0],
	&i2c2_pingroup[1],
	&i2c2_pingroup[2],
	&i2c2_pingroup[3],
	&i2c2_pingroup[4],
};

/* functions */
static struct spear_function *spear320_functions[] = {
	SPEAR3XX_COMMON_FUNCTIONS,
	&clcd_function,
	&emi_function,
	&fsmc_function,
	&spp_function,
	&sdhci_function,
	&i2s_function,
	&uart1_function,
	&uart1_modem_function,
	&uart2_function,
	&uart3_function,
	&uart4_function,
	&uart5_function,
	&uart6_function,
	&rs485_function,
	&touchscreen_function,
	&can0_function,
	&can1_function,
	&pwm0_1_function,
	&pwm2_function,
	&pwm3_function,
	&ssp1_function,
	&ssp2_function,
	&mii2_function,
	&mii0_1_function,
	&i2c1_function,
	&i2c2_function,
};

static const struct of_device_id spear320_pinctrl_of_match[] = {
	{
		.compatible = "st,spear320-pinmux",
	},
	{},
};

static int spear320_pinctrl_probe(struct platform_device *pdev)
{
	int ret;

	spear3xx_machdata.groups = spear320_pingroups;
	spear3xx_machdata.ngroups = ARRAY_SIZE(spear320_pingroups);
	spear3xx_machdata.functions = spear320_functions;
	spear3xx_machdata.nfunctions = ARRAY_SIZE(spear320_functions);

	spear3xx_machdata.modes_supported = true;
	spear3xx_machdata.pmx_modes = spear320_pmx_modes;
	spear3xx_machdata.npmx_modes = ARRAY_SIZE(spear320_pmx_modes);

	pmx_init_addr(&spear3xx_machdata, PMX_CONFIG_REG);
	pmx_init_gpio_pingroup_addr(spear3xx_machdata.gpio_pingroups,
			spear3xx_machdata.ngpio_pingroups, PMX_CONFIG_REG);

	ret = spear_pinctrl_probe(pdev, &spear3xx_machdata);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver spear320_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = spear320_pinctrl_of_match,
	},
	.probe = spear320_pinctrl_probe,
};

static int __init spear320_pinctrl_init(void)
{
	return platform_driver_register(&spear320_pinctrl_driver);
}
arch_initcall(spear320_pinctrl_init);
