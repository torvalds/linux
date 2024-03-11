// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Elkhart Lake PCH pinctrl/GPIO driver
 *
 * Copyright (C) 2019, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"

#define EHL_PAD_OWN	0x020
#define EHL_PADCFGLOCK	0x080
#define EHL_HOSTSW_OWN	0x0b0
#define EHL_GPI_IS	0x100
#define EHL_GPI_IE	0x120

#define EHL_GPP(r, s, e)				\
	{						\
		.reg_num = (r),				\
		.base = (s),				\
		.size = ((e) - (s) + 1),		\
	}

#define EHL_COMMUNITY(b, s, e, g)			\
	INTEL_COMMUNITY_GPPS(b, s, e, g, EHL)

/* Elkhart Lake */
static const struct pinctrl_pin_desc ehl_community0_pins[] = {
	/* GPP_B */
	PINCTRL_PIN(0, "CORE_VID_0"),
	PINCTRL_PIN(1, "CORE_VID_1"),
	PINCTRL_PIN(2, "VRALERTB"),
	PINCTRL_PIN(3, "CPU_GP_2"),
	PINCTRL_PIN(4, "CPU_GP_3"),
	PINCTRL_PIN(5, "OSE_I2C0_SCLK"),
	PINCTRL_PIN(6, "OSE_I2C0_SDAT"),
	PINCTRL_PIN(7, "OSE_I2C1_SCLK"),
	PINCTRL_PIN(8, "OSE_I2C1_SDAT"),
	PINCTRL_PIN(9, "I2C5_SDA"),
	PINCTRL_PIN(10, "I2C5_SCL"),
	PINCTRL_PIN(11, "PMCALERTB"),
	PINCTRL_PIN(12, "SLP_S0B"),
	PINCTRL_PIN(13, "PLTRSTB"),
	PINCTRL_PIN(14, "SPKR"),
	PINCTRL_PIN(15, "GSPI0_CS0B"),
	PINCTRL_PIN(16, "GSPI0_CLK"),
	PINCTRL_PIN(17, "GSPI0_MISO"),
	PINCTRL_PIN(18, "GSPI0_MOSI"),
	PINCTRL_PIN(19, "GSPI1_CS0B"),
	PINCTRL_PIN(20, "GSPI1_CLK"),
	PINCTRL_PIN(21, "GSPI1_MISO"),
	PINCTRL_PIN(22, "GSPI1_MOSI"),
	PINCTRL_PIN(23, "GPPC_B_23"),
	PINCTRL_PIN(24, "GSPI0_CLK_LOOPBK"),
	PINCTRL_PIN(25, "GSPI1_CLK_LOOPBK"),
	/* GPP_T */
	PINCTRL_PIN(26, "OSE_QEPA_2"),
	PINCTRL_PIN(27, "OSE_QEPB_2"),
	PINCTRL_PIN(28, "OSE_QEPI_2"),
	PINCTRL_PIN(29, "GPPC_T_3"),
	PINCTRL_PIN(30, "RGMII0_INT"),
	PINCTRL_PIN(31, "RGMII0_RESETB"),
	PINCTRL_PIN(32, "RGMII0_AUXTS"),
	PINCTRL_PIN(33, "RGMII0_PPS"),
	PINCTRL_PIN(34, "USB2_OCB_2"),
	PINCTRL_PIN(35, "OSE_HSUART2_EN"),
	PINCTRL_PIN(36, "OSE_HSUART2_RE"),
	PINCTRL_PIN(37, "USB2_OCB_3"),
	PINCTRL_PIN(38, "OSE_UART2_RXD"),
	PINCTRL_PIN(39, "OSE_UART2_TXD"),
	PINCTRL_PIN(40, "OSE_UART2_RTSB"),
	PINCTRL_PIN(41, "OSE_UART2_CTSB"),
	/* GPP_G */
	PINCTRL_PIN(42, "SD3_CMD"),
	PINCTRL_PIN(43, "SD3_D0"),
	PINCTRL_PIN(44, "SD3_D1"),
	PINCTRL_PIN(45, "SD3_D2"),
	PINCTRL_PIN(46, "SD3_D3"),
	PINCTRL_PIN(47, "SD3_CDB"),
	PINCTRL_PIN(48, "SD3_CLK"),
	PINCTRL_PIN(49, "I2S2_SCLK"),
	PINCTRL_PIN(50, "I2S2_SFRM"),
	PINCTRL_PIN(51, "I2S2_TXD"),
	PINCTRL_PIN(52, "I2S2_RXD"),
	PINCTRL_PIN(53, "I2S3_SCLK"),
	PINCTRL_PIN(54, "I2S3_SFRM"),
	PINCTRL_PIN(55, "I2S3_TXD"),
	PINCTRL_PIN(56, "I2S3_RXD"),
	PINCTRL_PIN(57, "ESPI_IO_0"),
	PINCTRL_PIN(58, "ESPI_IO_1"),
	PINCTRL_PIN(59, "ESPI_IO_2"),
	PINCTRL_PIN(60, "ESPI_IO_3"),
	PINCTRL_PIN(61, "I2S1_SCLK"),
	PINCTRL_PIN(62, "ESPI_CSB"),
	PINCTRL_PIN(63, "ESPI_CLK"),
	PINCTRL_PIN(64, "ESPI_RESETB"),
	PINCTRL_PIN(65, "SD3_WP"),
	PINCTRL_PIN(66, "ESPI_CLK_LOOPBK"),
};

static const struct intel_padgroup ehl_community0_gpps[] = {
	EHL_GPP(0, 0, 25),	/* GPP_B */
	EHL_GPP(1, 26, 41),	/* GPP_T */
	EHL_GPP(2, 42, 66),	/* GPP_G */
};

static const struct intel_community ehl_community0[] = {
	EHL_COMMUNITY(0, 0, 66, ehl_community0_gpps),
};

static const struct intel_pinctrl_soc_data ehl_community0_soc_data = {
	.uid = "0",
	.pins = ehl_community0_pins,
	.npins = ARRAY_SIZE(ehl_community0_pins),
	.communities = ehl_community0,
	.ncommunities = ARRAY_SIZE(ehl_community0),
};

static const struct pinctrl_pin_desc ehl_community1_pins[] = {
	/* GPP_V */
	PINCTRL_PIN(0, "EMMC_CMD"),
	PINCTRL_PIN(1, "EMMC_DATA0"),
	PINCTRL_PIN(2, "EMMC_DATA1"),
	PINCTRL_PIN(3, "EMMC_DATA2"),
	PINCTRL_PIN(4, "EMMC_DATA3"),
	PINCTRL_PIN(5, "EMMC_DATA4"),
	PINCTRL_PIN(6, "EMMC_DATA5"),
	PINCTRL_PIN(7, "EMMC_DATA6"),
	PINCTRL_PIN(8, "EMMC_DATA7"),
	PINCTRL_PIN(9, "EMMC_RCLK"),
	PINCTRL_PIN(10, "EMMC_CLK"),
	PINCTRL_PIN(11, "EMMC_RESETB"),
	PINCTRL_PIN(12, "OSE_TGPIO0"),
	PINCTRL_PIN(13, "OSE_TGPIO1"),
	PINCTRL_PIN(14, "OSE_TGPIO2"),
	PINCTRL_PIN(15, "OSE_TGPIO3"),
	/* GPP_H */
	PINCTRL_PIN(16, "RGMII1_INT"),
	PINCTRL_PIN(17, "RGMII1_RESETB"),
	PINCTRL_PIN(18, "RGMII1_AUXTS"),
	PINCTRL_PIN(19, "RGMII1_PPS"),
	PINCTRL_PIN(20, "I2C2_SDA"),
	PINCTRL_PIN(21, "I2C2_SCL"),
	PINCTRL_PIN(22, "I2C3_SDA"),
	PINCTRL_PIN(23, "I2C3_SCL"),
	PINCTRL_PIN(24, "I2C4_SDA"),
	PINCTRL_PIN(25, "I2C4_SCL"),
	PINCTRL_PIN(26, "SRCCLKREQB_4"),
	PINCTRL_PIN(27, "SRCCLKREQB_5"),
	PINCTRL_PIN(28, "OSE_UART1_RXD"),
	PINCTRL_PIN(29, "OSE_UART1_TXD"),
	PINCTRL_PIN(30, "GPPC_H_14"),
	PINCTRL_PIN(31, "OSE_UART1_CTSB"),
	PINCTRL_PIN(32, "PCIE_LNK_DOWN"),
	PINCTRL_PIN(33, "SD_PWR_EN_B"),
	PINCTRL_PIN(34, "CPU_C10_GATEB"),
	PINCTRL_PIN(35, "GPPC_H_19"),
	PINCTRL_PIN(36, "OSE_PWM7"),
	PINCTRL_PIN(37, "OSE_HSUART1_DE"),
	PINCTRL_PIN(38, "OSE_HSUART1_RE"),
	PINCTRL_PIN(39, "OSE_HSUART1_EN"),
	/* GPP_D */
	PINCTRL_PIN(40, "OSE_QEPA_0"),
	PINCTRL_PIN(41, "OSE_QEPB_0"),
	PINCTRL_PIN(42, "OSE_QEPI_0"),
	PINCTRL_PIN(43, "OSE_PWM6"),
	PINCTRL_PIN(44, "OSE_PWM2"),
	PINCTRL_PIN(45, "SRCCLKREQB_0"),
	PINCTRL_PIN(46, "SRCCLKREQB_1"),
	PINCTRL_PIN(47, "SRCCLKREQB_2"),
	PINCTRL_PIN(48, "SRCCLKREQB_3"),
	PINCTRL_PIN(49, "OSE_SPI0_CSB"),
	PINCTRL_PIN(50, "OSE_SPI0_SCLK"),
	PINCTRL_PIN(51, "OSE_SPI0_MISO"),
	PINCTRL_PIN(52, "OSE_SPI0_MOSI"),
	PINCTRL_PIN(53, "OSE_QEPA_1"),
	PINCTRL_PIN(54, "OSE_QEPB_1"),
	PINCTRL_PIN(55, "OSE_PWM3"),
	PINCTRL_PIN(56, "OSE_QEPI_1"),
	PINCTRL_PIN(57, "OSE_PWM4"),
	PINCTRL_PIN(58, "OSE_PWM5"),
	PINCTRL_PIN(59, "I2S_MCLK1_OUT"),
	PINCTRL_PIN(60, "GSPI2_CLK_LOOPBK"),
	/* GPP_U */
	PINCTRL_PIN(61, "RGMII2_INT"),
	PINCTRL_PIN(62, "RGMII2_RESETB"),
	PINCTRL_PIN(63, "RGMII2_PPS"),
	PINCTRL_PIN(64, "RGMII2_AUXTS"),
	PINCTRL_PIN(65, "ISI_SPIM_CS"),
	PINCTRL_PIN(66, "ISI_SPIM_SCLK"),
	PINCTRL_PIN(67, "ISI_SPIM_MISO"),
	PINCTRL_PIN(68, "OSE_QEPA_3"),
	PINCTRL_PIN(69, "ISI_SPIS_CS"),
	PINCTRL_PIN(70, "ISI_SPIS_SCLK"),
	PINCTRL_PIN(71, "ISI_SPIS_MISO"),
	PINCTRL_PIN(72, "OSE_QEPB_3"),
	PINCTRL_PIN(73, "ISI_CHX_OKNOK_0"),
	PINCTRL_PIN(74, "ISI_CHX_OKNOK_1"),
	PINCTRL_PIN(75, "ISI_CHX_RLY_SWTCH"),
	PINCTRL_PIN(76, "ISI_CHX_PMIC_EN"),
	PINCTRL_PIN(77, "ISI_OKNOK_0"),
	PINCTRL_PIN(78, "ISI_OKNOK_1"),
	PINCTRL_PIN(79, "ISI_ALERT"),
	PINCTRL_PIN(80, "OSE_QEPI_3"),
	PINCTRL_PIN(81, "GSPI3_CLK_LOOPBK"),
	PINCTRL_PIN(82, "GSPI4_CLK_LOOPBK"),
	PINCTRL_PIN(83, "GSPI5_CLK_LOOPBK"),
	PINCTRL_PIN(84, "GSPI6_CLK_LOOPBK"),
	/* vGPIO */
	PINCTRL_PIN(85, "CNV_BTEN"),
	PINCTRL_PIN(86, "CNV_BT_HOST_WAKEB"),
	PINCTRL_PIN(87, "CNV_BT_IF_SELECT"),
	PINCTRL_PIN(88, "vCNV_BT_UART_TXD"),
	PINCTRL_PIN(89, "vCNV_BT_UART_RXD"),
	PINCTRL_PIN(90, "vCNV_BT_UART_CTS_B"),
	PINCTRL_PIN(91, "vCNV_BT_UART_RTS_B"),
	PINCTRL_PIN(92, "vCNV_MFUART1_TXD"),
	PINCTRL_PIN(93, "vCNV_MFUART1_RXD"),
	PINCTRL_PIN(94, "vCNV_MFUART1_CTS_B"),
	PINCTRL_PIN(95, "vCNV_MFUART1_RTS_B"),
	PINCTRL_PIN(96, "vUART0_TXD"),
	PINCTRL_PIN(97, "vUART0_RXD"),
	PINCTRL_PIN(98, "vUART0_CTS_B"),
	PINCTRL_PIN(99, "vUART0_RTS_B"),
	PINCTRL_PIN(100, "vOSE_UART0_TXD"),
	PINCTRL_PIN(101, "vOSE_UART0_RXD"),
	PINCTRL_PIN(102, "vOSE_UART0_CTS_B"),
	PINCTRL_PIN(103, "vOSE_UART0_RTS_B"),
	PINCTRL_PIN(104, "vCNV_BT_I2S_BCLK"),
	PINCTRL_PIN(105, "vCNV_BT_I2S_WS_SYNC"),
	PINCTRL_PIN(106, "vCNV_BT_I2S_SDO"),
	PINCTRL_PIN(107, "vCNV_BT_I2S_SDI"),
	PINCTRL_PIN(108, "vI2S2_SCLK"),
	PINCTRL_PIN(109, "vI2S2_SFRM"),
	PINCTRL_PIN(110, "vI2S2_TXD"),
	PINCTRL_PIN(111, "vI2S2_RXD"),
	PINCTRL_PIN(112, "vSD3_CD_B"),
};

static const struct intel_padgroup ehl_community1_gpps[] = {
	EHL_GPP(0, 0, 15),	/* GPP_V */
	EHL_GPP(1, 16, 39),	/* GPP_H */
	EHL_GPP(2, 40, 60),	/* GPP_D */
	EHL_GPP(3, 61, 84),	/* GPP_U */
	EHL_GPP(4, 85, 112),	/* vGPIO */
};

static const struct intel_community ehl_community1[] = {
	EHL_COMMUNITY(0, 0, 112, ehl_community1_gpps),
};

static const struct intel_pinctrl_soc_data ehl_community1_soc_data = {
	.uid = "1",
	.pins = ehl_community1_pins,
	.npins = ARRAY_SIZE(ehl_community1_pins),
	.communities = ehl_community1,
	.ncommunities = ARRAY_SIZE(ehl_community1),
};

static const struct pinctrl_pin_desc ehl_community3_pins[] = {
	/* CPU */
	PINCTRL_PIN(0, "HDACPU_SDI"),
	PINCTRL_PIN(1, "HDACPU_SDO"),
	PINCTRL_PIN(2, "HDACPU_BCLK"),
	PINCTRL_PIN(3, "PM_SYNC"),
	PINCTRL_PIN(4, "PECI"),
	PINCTRL_PIN(5, "CPUPWRGD"),
	PINCTRL_PIN(6, "THRMTRIPB"),
	PINCTRL_PIN(7, "PLTRST_CPUB"),
	PINCTRL_PIN(8, "PM_DOWN"),
	PINCTRL_PIN(9, "TRIGGER_IN"),
	PINCTRL_PIN(10, "TRIGGER_OUT"),
	PINCTRL_PIN(11, "UFS_RESETB"),
	PINCTRL_PIN(12, "CLKOUT_CPURTC"),
	PINCTRL_PIN(13, "VCCST_OVERRIDE"),
	PINCTRL_PIN(14, "C10_WAKE"),
	PINCTRL_PIN(15, "PROCHOTB"),
	PINCTRL_PIN(16, "CATERRB"),
	/* GPP_S */
	PINCTRL_PIN(17, "UFS_REF_CLK_0"),
	PINCTRL_PIN(18, "UFS_REF_CLK_1"),
	/* GPP_A */
	PINCTRL_PIN(19, "RGMII0_TXDATA_3"),
	PINCTRL_PIN(20, "RGMII0_TXDATA_2"),
	PINCTRL_PIN(21, "RGMII0_TXDATA_1"),
	PINCTRL_PIN(22, "RGMII0_TXDATA_0"),
	PINCTRL_PIN(23, "RGMII0_TXCLK"),
	PINCTRL_PIN(24, "RGMII0_TXCTL"),
	PINCTRL_PIN(25, "RGMII0_RXCLK"),
	PINCTRL_PIN(26, "RGMII0_RXDATA_3"),
	PINCTRL_PIN(27, "RGMII0_RXDATA_2"),
	PINCTRL_PIN(28, "RGMII0_RXDATA_1"),
	PINCTRL_PIN(29, "RGMII0_RXDATA_0"),
	PINCTRL_PIN(30, "RGMII1_TXDATA_3"),
	PINCTRL_PIN(31, "RGMII1_TXDATA_2"),
	PINCTRL_PIN(32, "RGMII1_TXDATA_1"),
	PINCTRL_PIN(33, "RGMII1_TXDATA_0"),
	PINCTRL_PIN(34, "RGMII1_TXCLK"),
	PINCTRL_PIN(35, "RGMII1_TXCTL"),
	PINCTRL_PIN(36, "RGMII1_RXCLK"),
	PINCTRL_PIN(37, "RGMII1_RXCTL"),
	PINCTRL_PIN(38, "RGMII1_RXDATA_3"),
	PINCTRL_PIN(39, "RGMII1_RXDATA_2"),
	PINCTRL_PIN(40, "RGMII1_RXDATA_1"),
	PINCTRL_PIN(41, "RGMII1_RXDATA_0"),
	PINCTRL_PIN(42, "RGMII0_RXCTL"),
	/* vGPIO_3 */
	PINCTRL_PIN(43, "ESPI_USB_OCB_0"),
	PINCTRL_PIN(44, "ESPI_USB_OCB_1"),
	PINCTRL_PIN(45, "ESPI_USB_OCB_2"),
	PINCTRL_PIN(46, "ESPI_USB_OCB_3"),
};

static const struct intel_padgroup ehl_community3_gpps[] = {
	EHL_GPP(0, 0, 16),	/* CPU */
	EHL_GPP(1, 17, 18),	/* GPP_S */
	EHL_GPP(2, 19, 42),	/* GPP_A */
	EHL_GPP(3, 43, 46),	/* vGPIO_3 */
};

static const struct intel_community ehl_community3[] = {
	EHL_COMMUNITY(0, 0, 46, ehl_community3_gpps),
};

static const struct intel_pinctrl_soc_data ehl_community3_soc_data = {
	.uid = "3",
	.pins = ehl_community3_pins,
	.npins = ARRAY_SIZE(ehl_community3_pins),
	.communities = ehl_community3,
	.ncommunities = ARRAY_SIZE(ehl_community3),
};

static const struct pinctrl_pin_desc ehl_community4_pins[] = {
	/* GPP_C */
	PINCTRL_PIN(0, "SMBCLK"),
	PINCTRL_PIN(1, "SMBDATA"),
	PINCTRL_PIN(2, "OSE_PWM0"),
	PINCTRL_PIN(3, "RGMII0_MDC"),
	PINCTRL_PIN(4, "RGMII0_MDIO"),
	PINCTRL_PIN(5, "OSE_PWM1"),
	PINCTRL_PIN(6, "RGMII1_MDC"),
	PINCTRL_PIN(7, "RGMII1_MDIO"),
	PINCTRL_PIN(8, "OSE_TGPIO4"),
	PINCTRL_PIN(9, "OSE_HSUART0_EN"),
	PINCTRL_PIN(10, "OSE_TGPIO5"),
	PINCTRL_PIN(11, "OSE_HSUART0_RE"),
	PINCTRL_PIN(12, "OSE_UART0_RXD"),
	PINCTRL_PIN(13, "OSE_UART0_TXD"),
	PINCTRL_PIN(14, "OSE_UART0_RTSB"),
	PINCTRL_PIN(15, "OSE_UART0_CTSB"),
	PINCTRL_PIN(16, "RGMII2_MDIO"),
	PINCTRL_PIN(17, "RGMII2_MDC"),
	PINCTRL_PIN(18, "OSE_I2C4_SDAT"),
	PINCTRL_PIN(19, "OSE_I2C4_SCLK"),
	PINCTRL_PIN(20, "OSE_UART4_RXD"),
	PINCTRL_PIN(21, "OSE_UART4_TXD"),
	PINCTRL_PIN(22, "OSE_UART4_RTSB"),
	PINCTRL_PIN(23, "OSE_UART4_CTSB"),
	/* GPP_F */
	PINCTRL_PIN(24, "CNV_BRI_DT"),
	PINCTRL_PIN(25, "CNV_BRI_RSP"),
	PINCTRL_PIN(26, "CNV_RGI_DT"),
	PINCTRL_PIN(27, "CNV_RGI_RSP"),
	PINCTRL_PIN(28, "CNV_RF_RESET_B"),
	PINCTRL_PIN(29, "EMMC_HIP_MON"),
	PINCTRL_PIN(30, "CNV_PA_BLANKING"),
	PINCTRL_PIN(31, "OSE_I2S1_SCLK"),
	PINCTRL_PIN(32, "I2S_MCLK2_INOUT"),
	PINCTRL_PIN(33, "BOOTMPC"),
	PINCTRL_PIN(34, "OSE_I2S1_SFRM"),
	PINCTRL_PIN(35, "GPPC_F_11"),
	PINCTRL_PIN(36, "GSXDOUT"),
	PINCTRL_PIN(37, "GSXSLOAD"),
	PINCTRL_PIN(38, "GSXDIN"),
	PINCTRL_PIN(39, "GSXSRESETB"),
	PINCTRL_PIN(40, "GSXCLK"),
	PINCTRL_PIN(41, "GPPC_F_17"),
	PINCTRL_PIN(42, "OSE_I2S1_TXD"),
	PINCTRL_PIN(43, "OSE_I2S1_RXD"),
	PINCTRL_PIN(44, "EXT_PWR_GATEB"),
	PINCTRL_PIN(45, "EXT_PWR_GATE2B"),
	PINCTRL_PIN(46, "VNN_CTRL"),
	PINCTRL_PIN(47, "V1P05_CTRL"),
	PINCTRL_PIN(48, "GPPF_CLK_LOOPBACK"),
	/* HVCMOS */
	PINCTRL_PIN(49, "L_BKLTEN"),
	PINCTRL_PIN(50, "L_BKLTCTL"),
	PINCTRL_PIN(51, "L_VDDEN"),
	PINCTRL_PIN(52, "SYS_PWROK"),
	PINCTRL_PIN(53, "SYS_RESETB"),
	PINCTRL_PIN(54, "MLK_RSTB"),
	/* GPP_E */
	PINCTRL_PIN(55, "SATA_LEDB"),
	PINCTRL_PIN(56, "GPPC_E_1"),
	PINCTRL_PIN(57, "GPPC_E_2"),
	PINCTRL_PIN(58, "DDSP_HPD_B"),
	PINCTRL_PIN(59, "SATA_DEVSLP_0"),
	PINCTRL_PIN(60, "DDPB_CTRLDATA"),
	PINCTRL_PIN(61, "GPPC_E_6"),
	PINCTRL_PIN(62, "DDPB_CTRLCLK"),
	PINCTRL_PIN(63, "GPPC_E_8"),
	PINCTRL_PIN(64, "USB2_OCB_0"),
	PINCTRL_PIN(65, "GPPC_E_10"),
	PINCTRL_PIN(66, "GPPC_E_11"),
	PINCTRL_PIN(67, "GPPC_E_12"),
	PINCTRL_PIN(68, "GPPC_E_13"),
	PINCTRL_PIN(69, "DDSP_HPD_A"),
	PINCTRL_PIN(70, "OSE_I2S0_RXD"),
	PINCTRL_PIN(71, "OSE_I2S0_TXD"),
	PINCTRL_PIN(72, "DDSP_HPD_C"),
	PINCTRL_PIN(73, "DDPA_CTRLDATA"),
	PINCTRL_PIN(74, "DDPA_CTRLCLK"),
	PINCTRL_PIN(75, "OSE_I2S0_SCLK"),
	PINCTRL_PIN(76, "OSE_I2S0_SFRM"),
	PINCTRL_PIN(77, "DDPC_CTRLDATA"),
	PINCTRL_PIN(78, "DDPC_CTRLCLK"),
	PINCTRL_PIN(79, "SPI1_CLK_LOOPBK"),
};

static const struct intel_padgroup ehl_community4_gpps[] = {
	EHL_GPP(0, 0, 23),	/* GPP_C */
	EHL_GPP(1, 24, 48),	/* GPP_F */
	EHL_GPP(2, 49, 54),	/* HVCMOS */
	EHL_GPP(3, 55, 79),	/* GPP_E */
};

static const struct intel_community ehl_community4[] = {
	EHL_COMMUNITY(0, 0, 79, ehl_community4_gpps),
};

static const struct intel_pinctrl_soc_data ehl_community4_soc_data = {
	.uid = "4",
	.pins = ehl_community4_pins,
	.npins = ARRAY_SIZE(ehl_community4_pins),
	.communities = ehl_community4,
	.ncommunities = ARRAY_SIZE(ehl_community4),
};

static const struct pinctrl_pin_desc ehl_community5_pins[] = {
	/* GPP_R */
	PINCTRL_PIN(0, "HDA_BCLK"),
	PINCTRL_PIN(1, "HDA_SYNC"),
	PINCTRL_PIN(2, "HDA_SDO"),
	PINCTRL_PIN(3, "HDA_SDI_0"),
	PINCTRL_PIN(4, "HDA_RSTB"),
	PINCTRL_PIN(5, "HDA_SDI_1"),
	PINCTRL_PIN(6, "GPP_R_6"),
	PINCTRL_PIN(7, "GPP_R_7"),
};

static const struct intel_padgroup ehl_community5_gpps[] = {
	EHL_GPP(0, 0, 7),	/* GPP_R */
};

static const struct intel_community ehl_community5[] = {
	EHL_COMMUNITY(0, 0, 7, ehl_community5_gpps),
};

static const struct intel_pinctrl_soc_data ehl_community5_soc_data = {
	.uid = "5",
	.pins = ehl_community5_pins,
	.npins = ARRAY_SIZE(ehl_community5_pins),
	.communities = ehl_community5,
	.ncommunities = ARRAY_SIZE(ehl_community5),
};

static const struct intel_pinctrl_soc_data *ehl_soc_data_array[] = {
	&ehl_community0_soc_data,
	&ehl_community1_soc_data,
	&ehl_community3_soc_data,
	&ehl_community4_soc_data,
	&ehl_community5_soc_data,
	NULL
};

static const struct acpi_device_id ehl_pinctrl_acpi_match[] = {
	{ "INTC1020", (kernel_ulong_t)ehl_soc_data_array },
	{ }
};
MODULE_DEVICE_TABLE(acpi, ehl_pinctrl_acpi_match);

static struct platform_driver ehl_pinctrl_driver = {
	.probe = intel_pinctrl_probe_by_uid,
	.driver = {
		.name = "elkhartlake-pinctrl",
		.acpi_match_table = ehl_pinctrl_acpi_match,
		.pm = pm_sleep_ptr(&intel_pinctrl_pm_ops),
	},
};
module_platform_driver(ehl_pinctrl_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel Elkhart Lake PCH pinctrl/GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(PINCTRL_INTEL);
