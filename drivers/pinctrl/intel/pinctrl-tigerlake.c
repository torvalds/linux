// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Tiger Lake PCH pinctrl/GPIO driver
 *
 * Copyright (C) 2019, Intel Corporation
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"

#define TGL_PAD_OWN	0x020
#define TGL_PADCFGLOCK	0x080
#define TGL_HOSTSW_OWN	0x0b0
#define TGL_GPI_IS	0x100
#define TGL_GPI_IE	0x120

#define TGL_GPP(r, s, e)				\
	{						\
		.reg_num = (r),				\
		.base = (s),				\
		.size = ((e) - (s) + 1),		\
	}

#define TGL_COMMUNITY(s, e, g)				\
	{						\
		.padown_offset = TGL_PAD_OWN,		\
		.padcfglock_offset = TGL_PADCFGLOCK,	\
		.hostown_offset = TGL_HOSTSW_OWN,	\
		.is_offset = TGL_GPI_IS,		\
		.ie_offset = TGL_GPI_IE,		\
		.pin_base = (s),			\
		.npins = ((e) - (s) + 1),		\
		.gpps = (g),				\
		.ngpps = ARRAY_SIZE(g),			\
	}

/* Tiger Lake-LP */
static const struct pinctrl_pin_desc tgllp_community0_pins[] = {
	/* GPP_B */
	PINCTRL_PIN(0, "CORE_VID_0"),
	PINCTRL_PIN(1, "CORE_VID_1"),
	PINCTRL_PIN(2, "VRALERTB"),
	PINCTRL_PIN(3, "CPU_GP_2"),
	PINCTRL_PIN(4, "CPU_GP_3"),
	PINCTRL_PIN(5, "ISH_I2C0_SDA"),
	PINCTRL_PIN(6, "ISH_I2C0_SCL"),
	PINCTRL_PIN(7, "ISH_I2C1_SDA"),
	PINCTRL_PIN(8, "ISH_I2C1_SCL"),
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
	PINCTRL_PIN(23, "SML1ALERTB"),
	PINCTRL_PIN(24, "GSPI0_CLK_LOOPBK"),
	PINCTRL_PIN(25, "GSPI1_CLK_LOOPBK"),
	/* GPP_T */
	PINCTRL_PIN(26, "I2C6_SDA"),
	PINCTRL_PIN(27, "I2C6_SCL"),
	PINCTRL_PIN(28, "I2C7_SDA"),
	PINCTRL_PIN(29, "I2C7_SCL"),
	PINCTRL_PIN(30, "UART4_RXD"),
	PINCTRL_PIN(31, "UART4_TXD"),
	PINCTRL_PIN(32, "UART4_RTSB"),
	PINCTRL_PIN(33, "UART4_CTSB"),
	PINCTRL_PIN(34, "UART5_RXD"),
	PINCTRL_PIN(35, "UART5_TXD"),
	PINCTRL_PIN(36, "UART5_RTSB"),
	PINCTRL_PIN(37, "UART5_CTSB"),
	PINCTRL_PIN(38, "UART6_RXD"),
	PINCTRL_PIN(39, "UART6_TXD"),
	PINCTRL_PIN(40, "UART6_RTSB"),
	PINCTRL_PIN(41, "UART6_CTSB"),
	/* GPP_A */
	PINCTRL_PIN(42, "ESPI_IO_0"),
	PINCTRL_PIN(43, "ESPI_IO_1"),
	PINCTRL_PIN(44, "ESPI_IO_2"),
	PINCTRL_PIN(45, "ESPI_IO_3"),
	PINCTRL_PIN(46, "ESPI_CSB"),
	PINCTRL_PIN(47, "ESPI_CLK"),
	PINCTRL_PIN(48, "ESPI_RESETB"),
	PINCTRL_PIN(49, "I2S2_SCLK"),
	PINCTRL_PIN(50, "I2S2_SFRM"),
	PINCTRL_PIN(51, "I2S2_TXD"),
	PINCTRL_PIN(52, "I2S2_RXD"),
	PINCTRL_PIN(53, "PMC_I2C_SDA"),
	PINCTRL_PIN(54, "SATAXPCIE_1"),
	PINCTRL_PIN(55, "PMC_I2C_SCL"),
	PINCTRL_PIN(56, "USB2_OCB_1"),
	PINCTRL_PIN(57, "USB2_OCB_2"),
	PINCTRL_PIN(58, "USB2_OCB_3"),
	PINCTRL_PIN(59, "DDSP_HPD_C"),
	PINCTRL_PIN(60, "DDSP_HPD_B"),
	PINCTRL_PIN(61, "DDSP_HPD_1"),
	PINCTRL_PIN(62, "DDSP_HPD_2"),
	PINCTRL_PIN(63, "GPPC_A_21"),
	PINCTRL_PIN(64, "GPPC_A_22"),
	PINCTRL_PIN(65, "I2S1_SCLK"),
	PINCTRL_PIN(66, "ESPI_CLK_LOOPBK"),
};

static const struct intel_padgroup tgllp_community0_gpps[] = {
	TGL_GPP(0, 0, 25),	/* GPP_B */
	TGL_GPP(1, 26, 41),	/* GPP_T */
	TGL_GPP(2, 42, 66),	/* GPP_A */
};

static const struct intel_community tgllp_community0[] = {
	TGL_COMMUNITY(0, 66, tgllp_community0_gpps),
};

static const struct intel_pinctrl_soc_data tgllp_community0_soc_data = {
	.uid = "0",
	.pins = tgllp_community0_pins,
	.npins = ARRAY_SIZE(tgllp_community0_pins),
	.communities = tgllp_community0,
	.ncommunities = ARRAY_SIZE(tgllp_community0),
};

static const struct pinctrl_pin_desc tgllp_community1_pins[] = {
	/* GPP_S */
	PINCTRL_PIN(0, "SNDW0_CLK"),
	PINCTRL_PIN(1, "SNDW0_DATA"),
	PINCTRL_PIN(2, "SNDW1_CLK"),
	PINCTRL_PIN(3, "SNDW1_DATA"),
	PINCTRL_PIN(4, "SNDW2_CLK"),
	PINCTRL_PIN(5, "SNDW2_DATA"),
	PINCTRL_PIN(6, "SNDW3_CLK"),
	PINCTRL_PIN(7, "SNDW3_DATA"),
	/* GPP_H */
	PINCTRL_PIN(8, "GPPC_H_0"),
	PINCTRL_PIN(9, "GPPC_H_1"),
	PINCTRL_PIN(10, "GPPC_H_2"),
	PINCTRL_PIN(11, "SX_EXIT_HOLDOFFB"),
	PINCTRL_PIN(12, "I2C2_SDA"),
	PINCTRL_PIN(13, "I2C2_SCL"),
	PINCTRL_PIN(14, "I2C3_SDA"),
	PINCTRL_PIN(15, "I2C3_SCL"),
	PINCTRL_PIN(16, "I2C4_SDA"),
	PINCTRL_PIN(17, "I2C4_SCL"),
	PINCTRL_PIN(18, "SRCCLKREQB_4"),
	PINCTRL_PIN(19, "SRCCLKREQB_5"),
	PINCTRL_PIN(20, "M2_SKT2_CFG_0"),
	PINCTRL_PIN(21, "M2_SKT2_CFG_1"),
	PINCTRL_PIN(22, "M2_SKT2_CFG_2"),
	PINCTRL_PIN(23, "M2_SKT2_CFG_3"),
	PINCTRL_PIN(24, "DDPB_CTRLCLK"),
	PINCTRL_PIN(25, "DDPB_CTRLDATA"),
	PINCTRL_PIN(26, "CPU_C10_GATEB"),
	PINCTRL_PIN(27, "TIME_SYNC_0"),
	PINCTRL_PIN(28, "IMGCLKOUT_1"),
	PINCTRL_PIN(29, "IMGCLKOUT_2"),
	PINCTRL_PIN(30, "IMGCLKOUT_3"),
	PINCTRL_PIN(31, "IMGCLKOUT_4"),
	/* GPP_D */
	PINCTRL_PIN(32, "ISH_GP_0"),
	PINCTRL_PIN(33, "ISH_GP_1"),
	PINCTRL_PIN(34, "ISH_GP_2"),
	PINCTRL_PIN(35, "ISH_GP_3"),
	PINCTRL_PIN(36, "IMGCLKOUT_0"),
	PINCTRL_PIN(37, "SRCCLKREQB_0"),
	PINCTRL_PIN(38, "SRCCLKREQB_1"),
	PINCTRL_PIN(39, "SRCCLKREQB_2"),
	PINCTRL_PIN(40, "SRCCLKREQB_3"),
	PINCTRL_PIN(41, "ISH_SPI_CSB"),
	PINCTRL_PIN(42, "ISH_SPI_CLK"),
	PINCTRL_PIN(43, "ISH_SPI_MISO"),
	PINCTRL_PIN(44, "ISH_SPI_MOSI"),
	PINCTRL_PIN(45, "ISH_UART0_RXD"),
	PINCTRL_PIN(46, "ISH_UART0_TXD"),
	PINCTRL_PIN(47, "ISH_UART0_RTSB"),
	PINCTRL_PIN(48, "ISH_UART0_CTSB"),
	PINCTRL_PIN(49, "ISH_GP_4"),
	PINCTRL_PIN(50, "ISH_GP_5"),
	PINCTRL_PIN(51, "I2S_MCLK1_OUT"),
	PINCTRL_PIN(52, "GSPI2_CLK_LOOPBK"),
	/* GPP_U */
	PINCTRL_PIN(53, "UART3_RXD"),
	PINCTRL_PIN(54, "UART3_TXD"),
	PINCTRL_PIN(55, "UART3_RTSB"),
	PINCTRL_PIN(56, "UART3_CTSB"),
	PINCTRL_PIN(57, "GSPI3_CS0B"),
	PINCTRL_PIN(58, "GSPI3_CLK"),
	PINCTRL_PIN(59, "GSPI3_MISO"),
	PINCTRL_PIN(60, "GSPI3_MOSI"),
	PINCTRL_PIN(61, "GSPI4_CS0B"),
	PINCTRL_PIN(62, "GSPI4_CLK"),
	PINCTRL_PIN(63, "GSPI4_MISO"),
	PINCTRL_PIN(64, "GSPI4_MOSI"),
	PINCTRL_PIN(65, "GSPI5_CS0B"),
	PINCTRL_PIN(66, "GSPI5_CLK"),
	PINCTRL_PIN(67, "GSPI5_MISO"),
	PINCTRL_PIN(68, "GSPI5_MOSI"),
	PINCTRL_PIN(69, "GSPI6_CS0B"),
	PINCTRL_PIN(70, "GSPI6_CLK"),
	PINCTRL_PIN(71, "GSPI6_MISO"),
	PINCTRL_PIN(72, "GSPI6_MOSI"),
	PINCTRL_PIN(73, "GSPI3_CLK_LOOPBK"),
	PINCTRL_PIN(74, "GSPI4_CLK_LOOPBK"),
	PINCTRL_PIN(75, "GSPI5_CLK_LOOPBK"),
	PINCTRL_PIN(76, "GSPI6_CLK_LOOPBK"),
	/* vGPIO */
	PINCTRL_PIN(77, "CNV_BTEN"),
	PINCTRL_PIN(78, "CNV_BT_HOST_WAKEB"),
	PINCTRL_PIN(79, "CNV_BT_IF_SELECT"),
	PINCTRL_PIN(80, "vCNV_BT_UART_TXD"),
	PINCTRL_PIN(81, "vCNV_BT_UART_RXD"),
	PINCTRL_PIN(82, "vCNV_BT_UART_CTS_B"),
	PINCTRL_PIN(83, "vCNV_BT_UART_RTS_B"),
	PINCTRL_PIN(84, "vCNV_MFUART1_TXD"),
	PINCTRL_PIN(85, "vCNV_MFUART1_RXD"),
	PINCTRL_PIN(86, "vCNV_MFUART1_CTS_B"),
	PINCTRL_PIN(87, "vCNV_MFUART1_RTS_B"),
	PINCTRL_PIN(88, "vUART0_TXD"),
	PINCTRL_PIN(89, "vUART0_RXD"),
	PINCTRL_PIN(90, "vUART0_CTS_B"),
	PINCTRL_PIN(91, "vUART0_RTS_B"),
	PINCTRL_PIN(92, "vISH_UART0_TXD"),
	PINCTRL_PIN(93, "vISH_UART0_RXD"),
	PINCTRL_PIN(94, "vISH_UART0_CTS_B"),
	PINCTRL_PIN(95, "vISH_UART0_RTS_B"),
	PINCTRL_PIN(96, "vCNV_BT_I2S_BCLK"),
	PINCTRL_PIN(97, "vCNV_BT_I2S_WS_SYNC"),
	PINCTRL_PIN(98, "vCNV_BT_I2S_SDO"),
	PINCTRL_PIN(99, "vCNV_BT_I2S_SDI"),
	PINCTRL_PIN(100, "vI2S2_SCLK"),
	PINCTRL_PIN(101, "vI2S2_SFRM"),
	PINCTRL_PIN(102, "vI2S2_TXD"),
	PINCTRL_PIN(103, "vI2S2_RXD"),
};

static const struct intel_padgroup tgllp_community1_gpps[] = {
	TGL_GPP(0, 0, 7),	/* GPP_S */
	TGL_GPP(1, 8, 31),	/* GPP_H */
	TGL_GPP(2, 32, 52),	/* GPP_D */
	TGL_GPP(3, 53, 76),	/* GPP_U */
	TGL_GPP(4, 77, 103),	/* vGPIO */
};

static const struct intel_community tgllp_community1[] = {
	TGL_COMMUNITY(0, 103, tgllp_community1_gpps),
};

static const struct intel_pinctrl_soc_data tgllp_community1_soc_data = {
	.uid = "1",
	.pins = tgllp_community1_pins,
	.npins = ARRAY_SIZE(tgllp_community1_pins),
	.communities = tgllp_community1,
	.ncommunities = ARRAY_SIZE(tgllp_community1),
};

static const struct pinctrl_pin_desc tgllp_community4_pins[] = {
	/* GPP_C */
	PINCTRL_PIN(0, "SMBCLK"),
	PINCTRL_PIN(1, "SMBDATA"),
	PINCTRL_PIN(2, "SMBALERTB"),
	PINCTRL_PIN(3, "SML0CLK"),
	PINCTRL_PIN(4, "SML0DATA"),
	PINCTRL_PIN(5, "SML0ALERTB"),
	PINCTRL_PIN(6, "SML1CLK"),
	PINCTRL_PIN(7, "SML1DATA"),
	PINCTRL_PIN(8, "UART0_RXD"),
	PINCTRL_PIN(9, "UART0_TXD"),
	PINCTRL_PIN(10, "UART0_RTSB"),
	PINCTRL_PIN(11, "UART0_CTSB"),
	PINCTRL_PIN(12, "UART1_RXD"),
	PINCTRL_PIN(13, "UART1_TXD"),
	PINCTRL_PIN(14, "UART1_RTSB"),
	PINCTRL_PIN(15, "UART1_CTSB"),
	PINCTRL_PIN(16, "I2C0_SDA"),
	PINCTRL_PIN(17, "I2C0_SCL"),
	PINCTRL_PIN(18, "I2C1_SDA"),
	PINCTRL_PIN(19, "I2C1_SCL"),
	PINCTRL_PIN(20, "UART2_RXD"),
	PINCTRL_PIN(21, "UART2_TXD"),
	PINCTRL_PIN(22, "UART2_RTSB"),
	PINCTRL_PIN(23, "UART2_CTSB"),
	/* GPP_F */
	PINCTRL_PIN(24, "CNV_BRI_DT"),
	PINCTRL_PIN(25, "CNV_BRI_RSP"),
	PINCTRL_PIN(26, "CNV_RGI_DT"),
	PINCTRL_PIN(27, "CNV_RGI_RSP"),
	PINCTRL_PIN(28, "CNV_RF_RESET_B"),
	PINCTRL_PIN(29, "GPPC_F_5"),
	PINCTRL_PIN(30, "CNV_PA_BLANKING"),
	PINCTRL_PIN(31, "GPPC_F_7"),
	PINCTRL_PIN(32, "I2S_MCLK2_INOUT"),
	PINCTRL_PIN(33, "BOOTMPC"),
	PINCTRL_PIN(34, "GPPC_F_10"),
	PINCTRL_PIN(35, "GPPC_F_11"),
	PINCTRL_PIN(36, "GSXDOUT"),
	PINCTRL_PIN(37, "GSXSLOAD"),
	PINCTRL_PIN(38, "GSXDIN"),
	PINCTRL_PIN(39, "GSXSRESETB"),
	PINCTRL_PIN(40, "GSXCLK"),
	PINCTRL_PIN(41, "GMII_MDC"),
	PINCTRL_PIN(42, "GMII_MDIO"),
	PINCTRL_PIN(43, "SRCCLKREQB_6"),
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
	PINCTRL_PIN(55, "SATAXPCIE_0"),
	PINCTRL_PIN(56, "SPI1_IO_2"),
	PINCTRL_PIN(57, "SPI1_IO_3"),
	PINCTRL_PIN(58, "CPU_GP_0"),
	PINCTRL_PIN(59, "SATA_DEVSLP_0"),
	PINCTRL_PIN(60, "SATA_DEVSLP_1"),
	PINCTRL_PIN(61, "GPPC_E_6"),
	PINCTRL_PIN(62, "CPU_GP_1"),
	PINCTRL_PIN(63, "SPI1_CS1B"),
	PINCTRL_PIN(64, "USB2_OCB_0"),
	PINCTRL_PIN(65, "SPI1_CSB"),
	PINCTRL_PIN(66, "SPI1_CLK"),
	PINCTRL_PIN(67, "SPI1_MISO_IO_1"),
	PINCTRL_PIN(68, "SPI1_MOSI_IO_0"),
	PINCTRL_PIN(69, "DDSP_HPD_A"),
	PINCTRL_PIN(70, "ISH_GP_6"),
	PINCTRL_PIN(71, "ISH_GP_7"),
	PINCTRL_PIN(72, "GPPC_E_17"),
	PINCTRL_PIN(73, "DDP1_CTRLCLK"),
	PINCTRL_PIN(74, "DDP1_CTRLDATA"),
	PINCTRL_PIN(75, "DDP2_CTRLCLK"),
	PINCTRL_PIN(76, "DDP2_CTRLDATA"),
	PINCTRL_PIN(77, "DDPA_CTRLCLK"),
	PINCTRL_PIN(78, "DDPA_CTRLDATA"),
	PINCTRL_PIN(79, "SPI1_CLK_LOOPBK"),
	/* JTAG */
	PINCTRL_PIN(80, "JTAG_TDO"),
	PINCTRL_PIN(81, "JTAGX"),
	PINCTRL_PIN(82, "PRDYB"),
	PINCTRL_PIN(83, "PREQB"),
	PINCTRL_PIN(84, "CPU_TRSTB"),
	PINCTRL_PIN(85, "JTAG_TDI"),
	PINCTRL_PIN(86, "JTAG_TMS"),
	PINCTRL_PIN(87, "JTAG_TCK"),
	PINCTRL_PIN(88, "DBG_PMODE"),
};

static const struct intel_padgroup tgllp_community4_gpps[] = {
	TGL_GPP(0, 0, 23),	/* GPP_C */
	TGL_GPP(1, 24, 48),	/* GPP_F */
	TGL_GPP(2, 49, 54),	/* HVCMOS */
	TGL_GPP(3, 55, 79),	/* GPP_E */
	TGL_GPP(4, 80, 88),	/* JTAG */
};

static const struct intel_community tgllp_community4[] = {
	TGL_COMMUNITY(0, 88, tgllp_community4_gpps),
};

static const struct intel_pinctrl_soc_data tgllp_community4_soc_data = {
	.uid = "4",
	.pins = tgllp_community4_pins,
	.npins = ARRAY_SIZE(tgllp_community4_pins),
	.communities = tgllp_community4,
	.ncommunities = ARRAY_SIZE(tgllp_community4),
};

static const struct pinctrl_pin_desc tgllp_community5_pins[] = {
	/* GPP_R */
	PINCTRL_PIN(0, "HDA_BCLK"),
	PINCTRL_PIN(1, "HDA_SYNC"),
	PINCTRL_PIN(2, "HDA_SDO"),
	PINCTRL_PIN(3, "HDA_SDI_0"),
	PINCTRL_PIN(4, "HDA_RSTB"),
	PINCTRL_PIN(5, "HDA_SDI_1"),
	PINCTRL_PIN(6, "GPP_R_6"),
	PINCTRL_PIN(7, "GPP_R_7"),
	/* SPI */
	PINCTRL_PIN(8, "SPI0_IO_2"),
	PINCTRL_PIN(9, "SPI0_IO_3"),
	PINCTRL_PIN(10, "SPI0_MOSI_IO_0"),
	PINCTRL_PIN(11, "SPI0_MISO_IO_1"),
	PINCTRL_PIN(12, "SPI0_TPM_CSB"),
	PINCTRL_PIN(13, "SPI0_FLASH_0_CSB"),
	PINCTRL_PIN(14, "SPI0_FLASH_1_CSB"),
	PINCTRL_PIN(15, "SPI0_CLK"),
	PINCTRL_PIN(16, "SPI0_CLK_LOOPBK"),
};

static const struct intel_padgroup tgllp_community5_gpps[] = {
	TGL_GPP(0, 0, 7),	/* GPP_R */
	TGL_GPP(1, 8, 16),	/* SPI */
};

static const struct intel_community tgllp_community5[] = {
	TGL_COMMUNITY(0, 16, tgllp_community5_gpps),
};

static const struct intel_pinctrl_soc_data tgllp_community5_soc_data = {
	.uid = "5",
	.pins = tgllp_community5_pins,
	.npins = ARRAY_SIZE(tgllp_community5_pins),
	.communities = tgllp_community5,
	.ncommunities = ARRAY_SIZE(tgllp_community5),
};

static const struct intel_pinctrl_soc_data *tgllp_soc_data_array[] = {
	&tgllp_community0_soc_data,
	&tgllp_community1_soc_data,
	&tgllp_community4_soc_data,
	&tgllp_community5_soc_data,
	NULL
};

static const struct acpi_device_id tgl_pinctrl_acpi_match[] = {
	{ "INT34C5", (kernel_ulong_t)tgllp_soc_data_array },
	{ }
};
MODULE_DEVICE_TABLE(acpi, tgl_pinctrl_acpi_match);

static INTEL_PINCTRL_PM_OPS(tgl_pinctrl_pm_ops);

static struct platform_driver tgl_pinctrl_driver = {
	.probe = intel_pinctrl_probe_by_uid,
	.driver = {
		.name = "tigerlake-pinctrl",
		.acpi_match_table = tgl_pinctrl_acpi_match,
		.pm = &tgl_pinctrl_pm_ops,
	},
};

module_platform_driver(tgl_pinctrl_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel Tiger Lake PCH pinctrl/GPIO driver");
MODULE_LICENSE("GPL v2");
