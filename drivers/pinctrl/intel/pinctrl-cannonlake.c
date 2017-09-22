/*
 * Intel Cannon Lake PCH pinctrl/GPIO driver
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"

#define CNL_PAD_OWN	0x020
#define CNL_PADCFGLOCK	0x080
#define CNL_HOSTSW_OWN	0x0b0
#define CNL_GPI_IE	0x120

#define CNL_GPP(r, s, e)				\
	{						\
		.reg_num = (r),				\
		.base = (s),				\
		.size = ((e) - (s) + 1),		\
	}

#define CNL_COMMUNITY(b, s, e, g)			\
	{						\
		.barno = (b),				\
		.padown_offset = CNL_PAD_OWN,		\
		.padcfglock_offset = CNL_PADCFGLOCK,	\
		.hostown_offset = CNL_HOSTSW_OWN,	\
		.ie_offset = CNL_GPI_IE,		\
		.pin_base = (s),			\
		.npins = ((e) - (s) + 1),		\
		.gpps = (g),				\
		.ngpps = ARRAY_SIZE(g),			\
	}

/* Cannon Lake-H */
static const struct pinctrl_pin_desc cnlh_pins[] = {
	/* GPP_A */
	PINCTRL_PIN(0, "RCINB"),
	PINCTRL_PIN(1, "LAD_0"),
	PINCTRL_PIN(2, "LAD_1"),
	PINCTRL_PIN(3, "LAD_2"),
	PINCTRL_PIN(4, "LAD_3"),
	PINCTRL_PIN(5, "LFRAMEB"),
	PINCTRL_PIN(6, "SERIRQ"),
	PINCTRL_PIN(7, "PIRQAB"),
	PINCTRL_PIN(8, "CLKRUNB"),
	PINCTRL_PIN(9, "CLKOUT_LPC_0"),
	PINCTRL_PIN(10, "CLKOUT_LPC_1"),
	PINCTRL_PIN(11, "PMEB"),
	PINCTRL_PIN(12, "BM_BUSYB"),
	PINCTRL_PIN(13, "SUSWARNB_SUSPWRDNACK"),
	PINCTRL_PIN(14, "SUS_STATB"),
	PINCTRL_PIN(15, "SUSACKB"),
	PINCTRL_PIN(16, "CLKOUT_48"),
	PINCTRL_PIN(17, "SD_VDD1_PWR_EN_B"),
	PINCTRL_PIN(18, "ISH_GP_0"),
	PINCTRL_PIN(19, "ISH_GP_1"),
	PINCTRL_PIN(20, "ISH_GP_2"),
	PINCTRL_PIN(21, "ISH_GP_3"),
	PINCTRL_PIN(22, "ISH_GP_4"),
	PINCTRL_PIN(23, "ISH_GP_5"),
	PINCTRL_PIN(24, "ESPI_CLK_LOOPBK"),
	/* GPP_B */
	PINCTRL_PIN(25, "GSPI0_CS1B"),
	PINCTRL_PIN(26, "GSPI1_CS1B"),
	PINCTRL_PIN(27, "VRALERTB"),
	PINCTRL_PIN(28, "CPU_GP_2"),
	PINCTRL_PIN(29, "CPU_GP_3"),
	PINCTRL_PIN(30, "SRCCLKREQB_0"),
	PINCTRL_PIN(31, "SRCCLKREQB_1"),
	PINCTRL_PIN(32, "SRCCLKREQB_2"),
	PINCTRL_PIN(33, "SRCCLKREQB_3"),
	PINCTRL_PIN(34, "SRCCLKREQB_4"),
	PINCTRL_PIN(35, "SRCCLKREQB_5"),
	PINCTRL_PIN(36, "SSP_MCLK"),
	PINCTRL_PIN(37, "SLP_S0B"),
	PINCTRL_PIN(38, "PLTRSTB"),
	PINCTRL_PIN(39, "SPKR"),
	PINCTRL_PIN(40, "GSPI0_CS0B"),
	PINCTRL_PIN(41, "GSPI0_CLK"),
	PINCTRL_PIN(42, "GSPI0_MISO"),
	PINCTRL_PIN(43, "GSPI0_MOSI"),
	PINCTRL_PIN(44, "GSPI1_CS0B"),
	PINCTRL_PIN(45, "GSPI1_CLK"),
	PINCTRL_PIN(46, "GSPI1_MISO"),
	PINCTRL_PIN(47, "GSPI1_MOSI"),
	PINCTRL_PIN(48, "SML1ALERTB"),
	PINCTRL_PIN(49, "GSPI0_CLK_LOOPBK"),
	PINCTRL_PIN(50, "GSPI1_CLK_LOOPBK"),
	/* GPP_C */
	PINCTRL_PIN(51, "SMBCLK"),
	PINCTRL_PIN(52, "SMBDATA"),
	PINCTRL_PIN(53, "SMBALERTB"),
	PINCTRL_PIN(54, "SML0CLK"),
	PINCTRL_PIN(55, "SML0DATA"),
	PINCTRL_PIN(56, "SML0ALERTB"),
	PINCTRL_PIN(57, "SML1CLK"),
	PINCTRL_PIN(58, "SML1DATA"),
	PINCTRL_PIN(59, "UART0_RXD"),
	PINCTRL_PIN(60, "UART0_TXD"),
	PINCTRL_PIN(61, "UART0_RTSB"),
	PINCTRL_PIN(62, "UART0_CTSB"),
	PINCTRL_PIN(63, "UART1_RXD"),
	PINCTRL_PIN(64, "UART1_TXD"),
	PINCTRL_PIN(65, "UART1_RTSB"),
	PINCTRL_PIN(66, "UART1_CTSB"),
	PINCTRL_PIN(67, "I2C0_SDA"),
	PINCTRL_PIN(68, "I2C0_SCL"),
	PINCTRL_PIN(69, "I2C1_SDA"),
	PINCTRL_PIN(70, "I2C1_SCL"),
	PINCTRL_PIN(71, "UART2_RXD"),
	PINCTRL_PIN(72, "UART2_TXD"),
	PINCTRL_PIN(73, "UART2_RTSB"),
	PINCTRL_PIN(74, "UART2_CTSB"),
	/* GPP_D */
	PINCTRL_PIN(75, "SPI1_CSB"),
	PINCTRL_PIN(76, "SPI1_CLK"),
	PINCTRL_PIN(77, "SPI1_MISO_IO_1"),
	PINCTRL_PIN(78, "SPI1_MOSI_IO_0"),
	PINCTRL_PIN(79, "ISH_I2C2_SDA"),
	PINCTRL_PIN(80, "SSP2_SFRM"),
	PINCTRL_PIN(81, "SSP2_TXD"),
	PINCTRL_PIN(82, "SSP2_RXD"),
	PINCTRL_PIN(83, "SSP2_SCLK"),
	PINCTRL_PIN(84, "ISH_SPI_CSB"),
	PINCTRL_PIN(85, "ISH_SPI_CLK"),
	PINCTRL_PIN(86, "ISH_SPI_MISO"),
	PINCTRL_PIN(87, "ISH_SPI_MOSI"),
	PINCTRL_PIN(88, "ISH_UART0_RXD"),
	PINCTRL_PIN(89, "ISH_UART0_TXD"),
	PINCTRL_PIN(90, "ISH_UART0_RTSB"),
	PINCTRL_PIN(91, "ISH_UART0_CTSB"),
	PINCTRL_PIN(92, "DMIC_CLK_1"),
	PINCTRL_PIN(93, "DMIC_DATA_1"),
	PINCTRL_PIN(94, "DMIC_CLK_0"),
	PINCTRL_PIN(95, "DMIC_DATA_0"),
	PINCTRL_PIN(96, "SPI1_IO_2"),
	PINCTRL_PIN(97, "SPI1_IO_3"),
	PINCTRL_PIN(98, "ISH_I2C2_SCL"),
	/* GPP_G */
	PINCTRL_PIN(99, "SD3_CMD"),
	PINCTRL_PIN(100, "SD3_D0"),
	PINCTRL_PIN(101, "SD3_D1"),
	PINCTRL_PIN(102, "SD3_D2"),
	PINCTRL_PIN(103, "SD3_D3"),
	PINCTRL_PIN(104, "SD3_CDB"),
	PINCTRL_PIN(105, "SD3_CLK"),
	PINCTRL_PIN(106, "SD3_WP"),
	/* AZA */
	PINCTRL_PIN(107, "HDA_BCLK"),
	PINCTRL_PIN(108, "HDA_RSTB"),
	PINCTRL_PIN(109, "HDA_SYNC"),
	PINCTRL_PIN(110, "HDA_SDO"),
	PINCTRL_PIN(111, "HDA_SDI_0"),
	PINCTRL_PIN(112, "HDA_SDI_1"),
	PINCTRL_PIN(113, "SSP1_SFRM"),
	PINCTRL_PIN(114, "SSP1_TXD"),
	/* vGPIO */
	PINCTRL_PIN(115, "CNV_BTEN"),
	PINCTRL_PIN(116, "CNV_GNEN"),
	PINCTRL_PIN(117, "CNV_WFEN"),
	PINCTRL_PIN(118, "CNV_WCEN"),
	PINCTRL_PIN(119, "CNV_BT_HOST_WAKEB"),
	PINCTRL_PIN(120, "vCNV_GNSS_HOST_WAKEB"),
	PINCTRL_PIN(121, "vSD3_CD_B"),
	PINCTRL_PIN(122, "CNV_BT_IF_SELECT"),
	PINCTRL_PIN(123, "vCNV_BT_UART_TXD"),
	PINCTRL_PIN(124, "vCNV_BT_UART_RXD"),
	PINCTRL_PIN(125, "vCNV_BT_UART_CTS_B"),
	PINCTRL_PIN(126, "vCNV_BT_UART_RTS_B"),
	PINCTRL_PIN(127, "vCNV_MFUART1_TXD"),
	PINCTRL_PIN(128, "vCNV_MFUART1_RXD"),
	PINCTRL_PIN(129, "vCNV_MFUART1_CTS_B"),
	PINCTRL_PIN(130, "vCNV_MFUART1_RTS_B"),
	PINCTRL_PIN(131, "vCNV_GNSS_UART_TXD"),
	PINCTRL_PIN(132, "vCNV_GNSS_UART_RXD"),
	PINCTRL_PIN(133, "vCNV_GNSS_UART_CTS_B"),
	PINCTRL_PIN(134, "vCNV_GNSS_UART_RTS_B"),
	PINCTRL_PIN(135, "vUART0_TXD"),
	PINCTRL_PIN(136, "vUART0_RXD"),
	PINCTRL_PIN(137, "vUART0_CTS_B"),
	PINCTRL_PIN(138, "vUART0_RTSB"),
	PINCTRL_PIN(139, "vISH_UART0_TXD"),
	PINCTRL_PIN(140, "vISH_UART0_RXD"),
	PINCTRL_PIN(141, "vISH_UART0_CTS_B"),
	PINCTRL_PIN(142, "vISH_UART0_RTSB"),
	PINCTRL_PIN(143, "vISH_UART1_TXD"),
	PINCTRL_PIN(144, "vISH_UART1_RXD"),
	PINCTRL_PIN(145, "vISH_UART1_CTS_B"),
	PINCTRL_PIN(146, "vISH_UART1_RTS_B"),
	PINCTRL_PIN(147, "vCNV_BT_I2S_BCLK"),
	PINCTRL_PIN(148, "vCNV_BT_I2S_WS_SYNC"),
	PINCTRL_PIN(149, "vCNV_BT_I2S_SDO"),
	PINCTRL_PIN(150, "vCNV_BT_I2S_SDI"),
	PINCTRL_PIN(151, "vSSP2_SCLK"),
	PINCTRL_PIN(152, "vSSP2_SFRM"),
	PINCTRL_PIN(153, "vSSP2_TXD"),
	PINCTRL_PIN(154, "vSSP2_RXD"),
	/* GPP_K */
	PINCTRL_PIN(155, "FAN_TACH_0"),
	PINCTRL_PIN(156, "FAN_TACH_1"),
	PINCTRL_PIN(157, "FAN_TACH_2"),
	PINCTRL_PIN(158, "FAN_TACH_3"),
	PINCTRL_PIN(159, "FAN_TACH_4"),
	PINCTRL_PIN(160, "FAN_TACH_5"),
	PINCTRL_PIN(161, "FAN_TACH_6"),
	PINCTRL_PIN(162, "FAN_TACH_7"),
	PINCTRL_PIN(163, "FAN_PWM_0"),
	PINCTRL_PIN(164, "FAN_PWM_1"),
	PINCTRL_PIN(165, "FAN_PWM_2"),
	PINCTRL_PIN(166, "FAN_PWM_3"),
	PINCTRL_PIN(167, "GSXDOUT"),
	PINCTRL_PIN(168, "GSXSLOAD"),
	PINCTRL_PIN(169, "GSXDIN"),
	PINCTRL_PIN(170, "GSXSRESETB"),
	PINCTRL_PIN(171, "GSXCLK"),
	PINCTRL_PIN(172, "ADR_COMPLETE"),
	PINCTRL_PIN(173, "NMIB"),
	PINCTRL_PIN(174, "SMIB"),
	PINCTRL_PIN(175, "CORE_VID_0"),
	PINCTRL_PIN(176, "CORE_VID_1"),
	PINCTRL_PIN(177, "IMGCLKOUT_0"),
	PINCTRL_PIN(178, "IMGCLKOUT_1"),
	/* GPP_H */
	PINCTRL_PIN(179, "SRCCLKREQB_6"),
	PINCTRL_PIN(180, "SRCCLKREQB_7"),
	PINCTRL_PIN(181, "SRCCLKREQB_8"),
	PINCTRL_PIN(182, "SRCCLKREQB_9"),
	PINCTRL_PIN(183, "SRCCLKREQB_10"),
	PINCTRL_PIN(184, "SRCCLKREQB_11"),
	PINCTRL_PIN(185, "SRCCLKREQB_12"),
	PINCTRL_PIN(186, "SRCCLKREQB_13"),
	PINCTRL_PIN(187, "SRCCLKREQB_14"),
	PINCTRL_PIN(188, "SRCCLKREQB_15"),
	PINCTRL_PIN(189, "SML2CLK"),
	PINCTRL_PIN(190, "SML2DATA"),
	PINCTRL_PIN(191, "SML2ALERTB"),
	PINCTRL_PIN(192, "SML3CLK"),
	PINCTRL_PIN(193, "SML3DATA"),
	PINCTRL_PIN(194, "SML3ALERTB"),
	PINCTRL_PIN(195, "SML4CLK"),
	PINCTRL_PIN(196, "SML4DATA"),
	PINCTRL_PIN(197, "SML4ALERTB"),
	PINCTRL_PIN(198, "ISH_I2C0_SDA"),
	PINCTRL_PIN(199, "ISH_I2C0_SCL"),
	PINCTRL_PIN(200, "ISH_I2C1_SDA"),
	PINCTRL_PIN(201, "ISH_I2C1_SCL"),
	PINCTRL_PIN(202, "TIME_SYNC_0"),
	/* GPP_E */
	PINCTRL_PIN(203, "SATAXPCIE_0"),
	PINCTRL_PIN(204, "SATAXPCIE_1"),
	PINCTRL_PIN(205, "SATAXPCIE_2"),
	PINCTRL_PIN(206, "CPU_GP_0"),
	PINCTRL_PIN(207, "SATA_DEVSLP_0"),
	PINCTRL_PIN(208, "SATA_DEVSLP_1"),
	PINCTRL_PIN(209, "SATA_DEVSLP_2"),
	PINCTRL_PIN(210, "CPU_GP_1"),
	PINCTRL_PIN(211, "SATA_LEDB"),
	PINCTRL_PIN(212, "USB2_OCB_0"),
	PINCTRL_PIN(213, "USB2_OCB_1"),
	PINCTRL_PIN(214, "USB2_OCB_2"),
	PINCTRL_PIN(215, "USB2_OCB_3"),
	/* GPP_F */
	PINCTRL_PIN(216, "SATAXPCIE_3"),
	PINCTRL_PIN(217, "SATAXPCIE_4"),
	PINCTRL_PIN(218, "SATAXPCIE_5"),
	PINCTRL_PIN(219, "SATAXPCIE_6"),
	PINCTRL_PIN(220, "SATAXPCIE_7"),
	PINCTRL_PIN(221, "SATA_DEVSLP_3"),
	PINCTRL_PIN(222, "SATA_DEVSLP_4"),
	PINCTRL_PIN(223, "SATA_DEVSLP_5"),
	PINCTRL_PIN(224, "SATA_DEVSLP_6"),
	PINCTRL_PIN(225, "SATA_DEVSLP_7"),
	PINCTRL_PIN(226, "SATA_SCLOCK"),
	PINCTRL_PIN(227, "SATA_SLOAD"),
	PINCTRL_PIN(228, "SATA_SDATAOUT1"),
	PINCTRL_PIN(229, "SATA_SDATAOUT0"),
	PINCTRL_PIN(230, "EXT_PWR_GATEB"),
	PINCTRL_PIN(231, "USB2_OCB_4"),
	PINCTRL_PIN(232, "USB2_OCB_5"),
	PINCTRL_PIN(233, "USB2_OCB_6"),
	PINCTRL_PIN(234, "USB2_OCB_7"),
	PINCTRL_PIN(235, "L_VDDEN"),
	PINCTRL_PIN(236, "L_BKLTEN"),
	PINCTRL_PIN(237, "L_BKLTCTL"),
	PINCTRL_PIN(238, "DDPF_CTRLCLK"),
	PINCTRL_PIN(239, "DDPF_CTRLDATA"),
	/* SPI */
	PINCTRL_PIN(240, "SPI0_IO_2"),
	PINCTRL_PIN(241, "SPI0_IO_3"),
	PINCTRL_PIN(242, "SPI0_MOSI_IO_0"),
	PINCTRL_PIN(243, "SPI0_MISO_IO_1"),
	PINCTRL_PIN(244, "SPI0_TPM_CSB"),
	PINCTRL_PIN(245, "SPI0_FLASH_0_CSB"),
	PINCTRL_PIN(246, "SPI0_FLASH_1_CSB"),
	PINCTRL_PIN(247, "SPI0_CLK"),
	PINCTRL_PIN(248, "SPI0_CLK_LOOPBK"),
	/* CPU */
	PINCTRL_PIN(249, "HDACPU_SDI"),
	PINCTRL_PIN(250, "HDACPU_SDO"),
	PINCTRL_PIN(251, "HDACPU_SCLK"),
	PINCTRL_PIN(252, "PM_SYNC"),
	PINCTRL_PIN(253, "PECI"),
	PINCTRL_PIN(254, "CPUPWRGD"),
	PINCTRL_PIN(255, "THRMTRIPB"),
	PINCTRL_PIN(256, "PLTRST_CPUB"),
	PINCTRL_PIN(257, "PM_DOWN"),
	PINCTRL_PIN(258, "TRIGGER_IN"),
	PINCTRL_PIN(259, "TRIGGER_OUT"),
	/* JTAG */
	PINCTRL_PIN(260, "JTAG_TDO"),
	PINCTRL_PIN(261, "JTAGX"),
	PINCTRL_PIN(262, "PRDYB"),
	PINCTRL_PIN(263, "PREQB"),
	PINCTRL_PIN(264, "CPU_TRSTB"),
	PINCTRL_PIN(265, "JTAG_TDI"),
	PINCTRL_PIN(266, "JTAG_TMS"),
	PINCTRL_PIN(267, "JTAG_TCK"),
	PINCTRL_PIN(268, "ITP_PMODE"),
	/* GPP_I */
	PINCTRL_PIN(269, "DDSP_HPD_0"),
	PINCTRL_PIN(270, "DDSP_HPD_1"),
	PINCTRL_PIN(271, "DDSP_HPD_2"),
	PINCTRL_PIN(272, "DDSP_HPD_3"),
	PINCTRL_PIN(273, "EDP_HPD"),
	PINCTRL_PIN(274, "DDPB_CTRLCLK"),
	PINCTRL_PIN(275, "DDPB_CTRLDATA"),
	PINCTRL_PIN(276, "DDPC_CTRLCLK"),
	PINCTRL_PIN(277, "DDPC_CTRLDATA"),
	PINCTRL_PIN(278, "DDPD_CTRLCLK"),
	PINCTRL_PIN(279, "DDPD_CTRLDATA"),
	PINCTRL_PIN(280, "M2_SKT2_CFG_0"),
	PINCTRL_PIN(281, "M2_SKT2_CFG_1"),
	PINCTRL_PIN(282, "M2_SKT2_CFG_2"),
	PINCTRL_PIN(283, "M2_SKT2_CFG_3"),
	PINCTRL_PIN(284, "SYS_PWROK"),
	PINCTRL_PIN(285, "SYS_RESETB"),
	PINCTRL_PIN(286, "MLK_RSTB"),
	/* GPP_J */
	PINCTRL_PIN(287, "CNV_PA_BLANKING"),
	PINCTRL_PIN(288, "CNV_GNSS_FTA"),
	PINCTRL_PIN(289, "CNV_GNSS_SYSCK"),
	PINCTRL_PIN(290, "CNV_RF_RESET_B"),
	PINCTRL_PIN(291, "CNV_BRI_DT"),
	PINCTRL_PIN(292, "CNV_BRI_RSP"),
	PINCTRL_PIN(293, "CNV_RGI_DT"),
	PINCTRL_PIN(294, "CNV_RGI_RSP"),
	PINCTRL_PIN(295, "CNV_MFUART2_RXD"),
	PINCTRL_PIN(296, "CNV_MFUART2_TXD"),
	PINCTRL_PIN(297, "CNV_MODEM_CLKREQ"),
	PINCTRL_PIN(298, "A4WP_PRESENT"),
};

static const struct intel_padgroup cnlh_community0_gpps[] = {
	CNL_GPP(0, 0, 24),	/* GPP_A */
	CNL_GPP(1, 25, 50),	/* GPP_B */
};

static const struct intel_padgroup cnlh_community1_gpps[] = {
	CNL_GPP(0, 51, 74),	/* GPP_C */
	CNL_GPP(1, 75, 98),	/* GPP_D */
	CNL_GPP(2, 99, 106),	/* GPP_G */
	CNL_GPP(3, 107, 114),	/* AZA */
	CNL_GPP(4, 115, 146),	/* vGPIO_0 */
	CNL_GPP(5, 147, 154),	/* vGPIO_1 */
};

static const struct intel_padgroup cnlh_community3_gpps[] = {
	CNL_GPP(0, 155, 178),	/* GPP_K */
	CNL_GPP(1, 179, 202),	/* GPP_H */
	CNL_GPP(2, 203, 215),	/* GPP_E */
	CNL_GPP(3, 216, 239),	/* GPP_F */
	CNL_GPP(4, 240, 248),	/* SPI */
};

static const struct intel_padgroup cnlh_community4_gpps[] = {
	CNL_GPP(0, 249, 259),	/* CPU */
	CNL_GPP(1, 260, 268),	/* JTAG */
	CNL_GPP(2, 269, 286),	/* GPP_I */
	CNL_GPP(3, 287, 298),	/* GPP_J */
};

static const unsigned int cnlh_spi0_pins[] = { 40, 41, 42, 43 };
static const unsigned int cnlh_spi1_pins[] = { 44, 45, 46, 47 };
static const unsigned int cnlh_spi2_pins[] = { 84, 85, 86, 87 };

static const unsigned int cnlh_uart0_pins[] = { 59, 60, 61, 62 };
static const unsigned int cnlh_uart1_pins[] = { 63, 64, 65, 66 };
static const unsigned int cnlh_uart2_pins[] = { 71, 72, 73, 74 };

static const unsigned int cnlh_i2c0_pins[] = { 67, 68 };
static const unsigned int cnlh_i2c1_pins[] = { 69, 70 };
static const unsigned int cnlh_i2c2_pins[] = { 88, 89 };
static const unsigned int cnlh_i2c3_pins[] = { 79, 98 };

static const struct intel_pingroup cnlh_groups[] = {
	PIN_GROUP("spi0_grp", cnlh_spi0_pins, 1),
	PIN_GROUP("spi1_grp", cnlh_spi1_pins, 1),
	PIN_GROUP("spi2_grp", cnlh_spi2_pins, 3),
	PIN_GROUP("uart0_grp", cnlh_uart0_pins, 1),
	PIN_GROUP("uart1_grp", cnlh_uart1_pins, 1),
	PIN_GROUP("uart2_grp", cnlh_uart2_pins, 1),
	PIN_GROUP("i2c0_grp", cnlh_i2c0_pins, 1),
	PIN_GROUP("i2c1_grp", cnlh_i2c1_pins, 1),
	PIN_GROUP("i2c2_grp", cnlh_i2c2_pins, 3),
	PIN_GROUP("i2c3_grp", cnlh_i2c3_pins, 2),
};

static const char * const cnlh_spi0_groups[] = { "spi0_grp" };
static const char * const cnlh_spi1_groups[] = { "spi1_grp" };
static const char * const cnlh_spi2_groups[] = { "spi2_grp" };
static const char * const cnlh_uart0_groups[] = { "uart0_grp" };
static const char * const cnlh_uart1_groups[] = { "uart1_grp" };
static const char * const cnlh_uart2_groups[] = { "uart2_grp" };
static const char * const cnlh_i2c0_groups[] = { "i2c0_grp" };
static const char * const cnlh_i2c1_groups[] = { "i2c1_grp" };
static const char * const cnlh_i2c2_groups[] = { "i2c2_grp" };
static const char * const cnlh_i2c3_groups[] = { "i2c3_grp" };

static const struct intel_function cnlh_functions[] = {
	FUNCTION("spi0", cnlh_spi0_groups),
	FUNCTION("spi1", cnlh_spi1_groups),
	FUNCTION("spi2", cnlh_spi2_groups),
	FUNCTION("uart0", cnlh_uart0_groups),
	FUNCTION("uart1", cnlh_uart1_groups),
	FUNCTION("uart2", cnlh_uart2_groups),
	FUNCTION("i2c0", cnlh_i2c0_groups),
	FUNCTION("i2c1", cnlh_i2c1_groups),
	FUNCTION("i2c2", cnlh_i2c2_groups),
	FUNCTION("i2c3", cnlh_i2c3_groups),
};

static const struct intel_community cnlh_communities[] = {
	CNL_COMMUNITY(0, 0, 50, cnlh_community0_gpps),
	CNL_COMMUNITY(1, 51, 154, cnlh_community1_gpps),
	/*
	 * ACPI MMIO resources are returned in reverse order for
	 * communities 3 and 4.
	 */
	CNL_COMMUNITY(3, 155, 248, cnlh_community3_gpps),
	CNL_COMMUNITY(2, 249, 298, cnlh_community4_gpps),
};

static const struct intel_pinctrl_soc_data cnlh_soc_data = {
	.pins = cnlh_pins,
	.npins = ARRAY_SIZE(cnlh_pins),
	.groups = cnlh_groups,
	.ngroups = ARRAY_SIZE(cnlh_groups),
	.functions = cnlh_functions,
	.nfunctions = ARRAY_SIZE(cnlh_functions),
	.communities = cnlh_communities,
	.ncommunities = ARRAY_SIZE(cnlh_communities),
};

/* Cannon Lake-LP */
static const struct pinctrl_pin_desc cnllp_pins[] = {
	/* GPP_A */
	PINCTRL_PIN(0, "RCINB"),
	PINCTRL_PIN(1, "LAD_0"),
	PINCTRL_PIN(2, "LAD_1"),
	PINCTRL_PIN(3, "LAD_2"),
	PINCTRL_PIN(4, "LAD_3"),
	PINCTRL_PIN(5, "LFRAMEB"),
	PINCTRL_PIN(6, "SERIRQ"),
	PINCTRL_PIN(7, "PIRQAB"),
	PINCTRL_PIN(8, "CLKRUNB"),
	PINCTRL_PIN(9, "CLKOUT_LPC_0"),
	PINCTRL_PIN(10, "CLKOUT_LPC_1"),
	PINCTRL_PIN(11, "PMEB"),
	PINCTRL_PIN(12, "BM_BUSYB"),
	PINCTRL_PIN(13, "SUSWARNB_SUSPWRDNACK"),
	PINCTRL_PIN(14, "SUS_STATB"),
	PINCTRL_PIN(15, "SUSACKB"),
	PINCTRL_PIN(16, "SD_1P8_SEL"),
	PINCTRL_PIN(17, "SD_PWR_EN_B"),
	PINCTRL_PIN(18, "ISH_GP_0"),
	PINCTRL_PIN(19, "ISH_GP_1"),
	PINCTRL_PIN(20, "ISH_GP_2"),
	PINCTRL_PIN(21, "ISH_GP_3"),
	PINCTRL_PIN(22, "ISH_GP_4"),
	PINCTRL_PIN(23, "ISH_GP_5"),
	PINCTRL_PIN(24, "ESPI_CLK_LOOPBK"),
	/* GPP_B */
	PINCTRL_PIN(25, "CORE_VID_0"),
	PINCTRL_PIN(26, "CORE_VID_1"),
	PINCTRL_PIN(27, "VRALERTB"),
	PINCTRL_PIN(28, "CPU_GP_2"),
	PINCTRL_PIN(29, "CPU_GP_3"),
	PINCTRL_PIN(30, "SRCCLKREQB_0"),
	PINCTRL_PIN(31, "SRCCLKREQB_1"),
	PINCTRL_PIN(32, "SRCCLKREQB_2"),
	PINCTRL_PIN(33, "SRCCLKREQB_3"),
	PINCTRL_PIN(34, "SRCCLKREQB_4"),
	PINCTRL_PIN(35, "SRCCLKREQB_5"),
	PINCTRL_PIN(36, "EXT_PWR_GATEB"),
	PINCTRL_PIN(37, "SLP_S0B"),
	PINCTRL_PIN(38, "PLTRSTB"),
	PINCTRL_PIN(39, "SPKR"),
	PINCTRL_PIN(40, "GSPI0_CS0B"),
	PINCTRL_PIN(41, "GSPI0_CLK"),
	PINCTRL_PIN(42, "GSPI0_MISO"),
	PINCTRL_PIN(43, "GSPI0_MOSI"),
	PINCTRL_PIN(44, "GSPI1_CS0B"),
	PINCTRL_PIN(45, "GSPI1_CLK"),
	PINCTRL_PIN(46, "GSPI1_MISO"),
	PINCTRL_PIN(47, "GSPI1_MOSI"),
	PINCTRL_PIN(48, "SML1ALERTB"),
	PINCTRL_PIN(49, "GSPI0_CLK_LOOPBK"),
	PINCTRL_PIN(50, "GSPI1_CLK_LOOPBK"),
	/* GPP_G */
	PINCTRL_PIN(51, "SD3_CMD"),
	PINCTRL_PIN(52, "SD3_D0_SD4_RCLK_P"),
	PINCTRL_PIN(53, "SD3_D1_SD4_RCLK_N"),
	PINCTRL_PIN(54, "SD3_D2"),
	PINCTRL_PIN(55, "SD3_D3"),
	PINCTRL_PIN(56, "SD3_CDB"),
	PINCTRL_PIN(57, "SD3_CLK"),
	PINCTRL_PIN(58, "SD3_WP"),
	/* SPI */
	PINCTRL_PIN(59, "SPI0_IO_2"),
	PINCTRL_PIN(60, "SPI0_IO_3"),
	PINCTRL_PIN(61, "SPI0_MOSI_IO_0"),
	PINCTRL_PIN(62, "SPI0_MISO_IO_1"),
	PINCTRL_PIN(63, "SPI0_TPM_CSB"),
	PINCTRL_PIN(64, "SPI0_FLASH_0_CSB"),
	PINCTRL_PIN(65, "SPI0_FLASH_1_CSB"),
	PINCTRL_PIN(66, "SPI0_CLK"),
	PINCTRL_PIN(67, "SPI0_CLK_LOOPBK"),
	/* GPP_D */
	PINCTRL_PIN(68, "SPI1_CSB"),
	PINCTRL_PIN(69, "SPI1_CLK"),
	PINCTRL_PIN(70, "SPI1_MISO_IO_1"),
	PINCTRL_PIN(71, "SPI1_MOSI_IO_0"),
	PINCTRL_PIN(72, "IMGCLKOUT_0"),
	PINCTRL_PIN(73, "ISH_I2C0_SDA"),
	PINCTRL_PIN(74, "ISH_I2C0_SCL"),
	PINCTRL_PIN(75, "ISH_I2C1_SDA"),
	PINCTRL_PIN(76, "ISH_I2C1_SCL"),
	PINCTRL_PIN(77, "ISH_SPI_CSB"),
	PINCTRL_PIN(78, "ISH_SPI_CLK"),
	PINCTRL_PIN(79, "ISH_SPI_MISO"),
	PINCTRL_PIN(80, "ISH_SPI_MOSI"),
	PINCTRL_PIN(81, "ISH_UART0_RXD"),
	PINCTRL_PIN(82, "ISH_UART0_TXD"),
	PINCTRL_PIN(83, "ISH_UART0_RTSB"),
	PINCTRL_PIN(84, "ISH_UART0_CTSB"),
	PINCTRL_PIN(85, "DMIC_CLK_1"),
	PINCTRL_PIN(86, "DMIC_DATA_1"),
	PINCTRL_PIN(87, "DMIC_CLK_0"),
	PINCTRL_PIN(88, "DMIC_DATA_0"),
	PINCTRL_PIN(89, "SPI1_IO_2"),
	PINCTRL_PIN(90, "SPI1_IO_3"),
	PINCTRL_PIN(91, "SSP_MCLK"),
	PINCTRL_PIN(92, "GSPI2_CLK_LOOPBK"),
	/* GPP_F */
	PINCTRL_PIN(93, "CNV_GNSS_PA_BLANKING"),
	PINCTRL_PIN(94, "CNV_GNSS_FTA"),
	PINCTRL_PIN(95, "CNV_GNSS_SYSCK"),
	PINCTRL_PIN(96, "EMMC_HIP_MON"),
	PINCTRL_PIN(97, "CNV_BRI_DT"),
	PINCTRL_PIN(98, "CNV_BRI_RSP"),
	PINCTRL_PIN(99, "CNV_RGI_DT"),
	PINCTRL_PIN(100, "CNV_RGI_RSP"),
	PINCTRL_PIN(101, "CNV_MFUART2_RXD"),
	PINCTRL_PIN(102, "CNV_MFUART2_TXD"),
	PINCTRL_PIN(103, "GPP_F_10"),
	PINCTRL_PIN(104, "EMMC_CMD"),
	PINCTRL_PIN(105, "EMMC_DATA_0"),
	PINCTRL_PIN(106, "EMMC_DATA_1"),
	PINCTRL_PIN(107, "EMMC_DATA_2"),
	PINCTRL_PIN(108, "EMMC_DATA_3"),
	PINCTRL_PIN(109, "EMMC_DATA_4"),
	PINCTRL_PIN(110, "EMMC_DATA_5"),
	PINCTRL_PIN(111, "EMMC_DATA_6"),
	PINCTRL_PIN(112, "EMMC_DATA_7"),
	PINCTRL_PIN(113, "EMMC_RCLK"),
	PINCTRL_PIN(114, "EMMC_CLK"),
	PINCTRL_PIN(115, "EMMC_RESETB"),
	PINCTRL_PIN(116, "A4WP_PRESENT"),
	/* GPP_H */
	PINCTRL_PIN(117, "SSP2_SCLK"),
	PINCTRL_PIN(118, "SSP2_SFRM"),
	PINCTRL_PIN(119, "SSP2_TXD"),
	PINCTRL_PIN(120, "SSP2_RXD"),
	PINCTRL_PIN(121, "I2C2_SDA"),
	PINCTRL_PIN(122, "I2C2_SCL"),
	PINCTRL_PIN(123, "I2C3_SDA"),
	PINCTRL_PIN(124, "I2C3_SCL"),
	PINCTRL_PIN(125, "I2C4_SDA"),
	PINCTRL_PIN(126, "I2C4_SCL"),
	PINCTRL_PIN(127, "I2C5_SDA"),
	PINCTRL_PIN(128, "I2C5_SCL"),
	PINCTRL_PIN(129, "M2_SKT2_CFG_0"),
	PINCTRL_PIN(130, "M2_SKT2_CFG_1"),
	PINCTRL_PIN(131, "M2_SKT2_CFG_2"),
	PINCTRL_PIN(132, "M2_SKT2_CFG_3"),
	PINCTRL_PIN(133, "DDPF_CTRLCLK"),
	PINCTRL_PIN(134, "DDPF_CTRLDATA"),
	PINCTRL_PIN(135, "CPU_VCCIO_PWR_GATEB"),
	PINCTRL_PIN(136, "TIMESYNC_0"),
	PINCTRL_PIN(137, "IMGCLKOUT_1"),
	PINCTRL_PIN(138, "GPPC_H_21"),
	PINCTRL_PIN(139, "GPPC_H_22"),
	PINCTRL_PIN(140, "GPPC_H_23"),
	/* vGPIO */
	PINCTRL_PIN(141, "CNV_BTEN"),
	PINCTRL_PIN(142, "CNV_GNEN"),
	PINCTRL_PIN(143, "CNV_WFEN"),
	PINCTRL_PIN(144, "CNV_WCEN"),
	PINCTRL_PIN(145, "CNV_BT_HOST_WAKEB"),
	PINCTRL_PIN(146, "CNV_BT_IF_SELECT"),
	PINCTRL_PIN(147, "vCNV_BT_UART_TXD"),
	PINCTRL_PIN(148, "vCNV_BT_UART_RXD"),
	PINCTRL_PIN(149, "vCNV_BT_UART_CTS_B"),
	PINCTRL_PIN(150, "vCNV_BT_UART_RTS_B"),
	PINCTRL_PIN(151, "vCNV_MFUART1_TXD"),
	PINCTRL_PIN(152, "vCNV_MFUART1_RXD"),
	PINCTRL_PIN(153, "vCNV_MFUART1_CTS_B"),
	PINCTRL_PIN(154, "vCNV_MFUART1_RTS_B"),
	PINCTRL_PIN(155, "vCNV_GNSS_UART_TXD"),
	PINCTRL_PIN(156, "vCNV_GNSS_UART_RXD"),
	PINCTRL_PIN(157, "vCNV_GNSS_UART_CTS_B"),
	PINCTRL_PIN(158, "vCNV_GNSS_UART_RTS_B"),
	PINCTRL_PIN(159, "vUART0_TXD"),
	PINCTRL_PIN(160, "vUART0_RXD"),
	PINCTRL_PIN(161, "vUART0_CTS_B"),
	PINCTRL_PIN(162, "vUART0_RTS_B"),
	PINCTRL_PIN(163, "vISH_UART0_TXD"),
	PINCTRL_PIN(164, "vISH_UART0_RXD"),
	PINCTRL_PIN(165, "vISH_UART0_CTS_B"),
	PINCTRL_PIN(166, "vISH_UART0_RTS_B"),
	PINCTRL_PIN(167, "vISH_UART1_TXD"),
	PINCTRL_PIN(168, "vISH_UART1_RXD"),
	PINCTRL_PIN(169, "vISH_UART1_CTS_B"),
	PINCTRL_PIN(170, "vISH_UART1_RTS_B"),
	PINCTRL_PIN(171, "vCNV_BT_I2S_BCLK"),
	PINCTRL_PIN(172, "vCNV_BT_I2S_WS_SYNC"),
	PINCTRL_PIN(173, "vCNV_BT_I2S_SDO"),
	PINCTRL_PIN(174, "vCNV_BT_I2S_SDI"),
	PINCTRL_PIN(175, "vSSP2_SCLK"),
	PINCTRL_PIN(176, "vSSP2_SFRM"),
	PINCTRL_PIN(177, "vSSP2_TXD"),
	PINCTRL_PIN(178, "vSSP2_RXD"),
	PINCTRL_PIN(179, "vCNV_GNSS_HOST_WAKEB"),
	PINCTRL_PIN(180, "vSD3_CD_B"),
	/* GPP_C */
	PINCTRL_PIN(181, "SMBCLK"),
	PINCTRL_PIN(182, "SMBDATA"),
	PINCTRL_PIN(183, "SMBALERTB"),
	PINCTRL_PIN(184, "SML0CLK"),
	PINCTRL_PIN(185, "SML0DATA"),
	PINCTRL_PIN(186, "SML0ALERTB"),
	PINCTRL_PIN(187, "SML1CLK"),
	PINCTRL_PIN(188, "SML1DATA"),
	PINCTRL_PIN(189, "UART0_RXD"),
	PINCTRL_PIN(190, "UART0_TXD"),
	PINCTRL_PIN(191, "UART0_RTSB"),
	PINCTRL_PIN(192, "UART0_CTSB"),
	PINCTRL_PIN(193, "UART1_RXD"),
	PINCTRL_PIN(194, "UART1_TXD"),
	PINCTRL_PIN(195, "UART1_RTSB"),
	PINCTRL_PIN(196, "UART1_CTSB"),
	PINCTRL_PIN(197, "I2C0_SDA"),
	PINCTRL_PIN(198, "I2C0_SCL"),
	PINCTRL_PIN(199, "I2C1_SDA"),
	PINCTRL_PIN(200, "I2C1_SCL"),
	PINCTRL_PIN(201, "UART2_RXD"),
	PINCTRL_PIN(202, "UART2_TXD"),
	PINCTRL_PIN(203, "UART2_RTSB"),
	PINCTRL_PIN(204, "UART2_CTSB"),
	/* GPP_E */
	PINCTRL_PIN(205, "SATAXPCIE_0"),
	PINCTRL_PIN(206, "SATAXPCIE_1"),
	PINCTRL_PIN(207, "SATAXPCIE_2"),
	PINCTRL_PIN(208, "CPU_GP_0"),
	PINCTRL_PIN(209, "SATA_DEVSLP_0"),
	PINCTRL_PIN(210, "SATA_DEVSLP_1"),
	PINCTRL_PIN(211, "SATA_DEVSLP_2"),
	PINCTRL_PIN(212, "CPU_GP_1"),
	PINCTRL_PIN(213, "SATA_LEDB"),
	PINCTRL_PIN(214, "USB2_OCB_0"),
	PINCTRL_PIN(215, "USB2_OCB_1"),
	PINCTRL_PIN(216, "USB2_OCB_2"),
	PINCTRL_PIN(217, "USB2_OCB_3"),
	PINCTRL_PIN(218, "DDSP_HPD_0"),
	PINCTRL_PIN(219, "DDSP_HPD_1"),
	PINCTRL_PIN(220, "DDSP_HPD_2"),
	PINCTRL_PIN(221, "DDSP_HPD_3"),
	PINCTRL_PIN(222, "EDP_HPD"),
	PINCTRL_PIN(223, "DDPB_CTRLCLK"),
	PINCTRL_PIN(224, "DDPB_CTRLDATA"),
	PINCTRL_PIN(225, "DDPC_CTRLCLK"),
	PINCTRL_PIN(226, "DDPC_CTRLDATA"),
	PINCTRL_PIN(227, "DDPD_CTRLCLK"),
	PINCTRL_PIN(228, "DDPD_CTRLDATA"),
	/* JTAG */
	PINCTRL_PIN(229, "JTAG_TDO"),
	PINCTRL_PIN(230, "JTAGX"),
	PINCTRL_PIN(231, "PRDYB"),
	PINCTRL_PIN(232, "PREQB"),
	PINCTRL_PIN(233, "CPU_TRSTB"),
	PINCTRL_PIN(234, "JTAG_TDI"),
	PINCTRL_PIN(235, "JTAG_TMS"),
	PINCTRL_PIN(236, "JTAG_TCK"),
	PINCTRL_PIN(237, "ITP_PMODE"),
	/* HVCMOS */
	PINCTRL_PIN(238, "L_BKLTEN"),
	PINCTRL_PIN(239, "L_BKLTCTL"),
	PINCTRL_PIN(240, "L_VDDEN"),
	PINCTRL_PIN(241, "SYS_PWROK"),
	PINCTRL_PIN(242, "SYS_RESETB"),
	PINCTRL_PIN(243, "MLK_RSTB"),
};

static const unsigned int cnllp_spi0_pins[] = { 40, 41, 42, 43, 7 };
static const unsigned int cnllp_spi0_modes[] = { 1, 1, 1, 1, 2 };
static const unsigned int cnllp_spi1_pins[] = { 44, 45, 46, 47, 11 };
static const unsigned int cnllp_spi1_modes[] = { 1, 1, 1, 1, 2 };
static const unsigned int cnllp_spi2_pins[] = { 77, 78, 79, 80, 83 };
static const unsigned int cnllp_spi2_modes[] = { 3, 3, 3, 3, 2 };

static const unsigned int cnllp_i2c0_pins[] = { 197, 198 };
static const unsigned int cnllp_i2c1_pins[] = { 199, 200 };
static const unsigned int cnllp_i2c2_pins[] = { 121, 122 };
static const unsigned int cnllp_i2c3_pins[] = { 123, 124 };
static const unsigned int cnllp_i2c4_pins[] = { 125, 126 };
static const unsigned int cnllp_i2c5_pins[] = { 127, 128 };

static const unsigned int cnllp_uart0_pins[] = { 189, 190, 191, 192 };
static const unsigned int cnllp_uart1_pins[] = { 193, 194, 195, 196 };
static const unsigned int cnllp_uart2_pins[] = { 201, 202, 203, 204 };

static const struct intel_pingroup cnllp_groups[] = {
	PIN_GROUP("spi0_grp", cnllp_spi0_pins, cnllp_spi0_modes),
	PIN_GROUP("spi1_grp", cnllp_spi1_pins, cnllp_spi1_modes),
	PIN_GROUP("spi2_grp", cnllp_spi2_pins, cnllp_spi2_modes),
	PIN_GROUP("i2c0_grp", cnllp_i2c0_pins, 1),
	PIN_GROUP("i2c1_grp", cnllp_i2c1_pins, 1),
	PIN_GROUP("i2c2_grp", cnllp_i2c2_pins, 1),
	PIN_GROUP("i2c3_grp", cnllp_i2c3_pins, 1),
	PIN_GROUP("i2c4_grp", cnllp_i2c4_pins, 1),
	PIN_GROUP("i2c5_grp", cnllp_i2c5_pins, 1),
	PIN_GROUP("uart0_grp", cnllp_uart0_pins, 1),
	PIN_GROUP("uart1_grp", cnllp_uart1_pins, 1),
	PIN_GROUP("uart2_grp", cnllp_uart2_pins, 1),
};

static const char * const cnllp_spi0_groups[] = { "spi0_grp" };
static const char * const cnllp_spi1_groups[] = { "spi1_grp" };
static const char * const cnllp_spi2_groups[] = { "spi2_grp" };
static const char * const cnllp_i2c0_groups[] = { "i2c0_grp" };
static const char * const cnllp_i2c1_groups[] = { "i2c1_grp" };
static const char * const cnllp_i2c2_groups[] = { "i2c2_grp" };
static const char * const cnllp_i2c3_groups[] = { "i2c3_grp" };
static const char * const cnllp_i2c4_groups[] = { "i2c4_grp" };
static const char * const cnllp_i2c5_groups[] = { "i2c5_grp" };
static const char * const cnllp_uart0_groups[] = { "uart0_grp" };
static const char * const cnllp_uart1_groups[] = { "uart1_grp" };
static const char * const cnllp_uart2_groups[] = { "uart2_grp" };

static const struct intel_function cnllp_functions[] = {
	FUNCTION("spi0", cnllp_spi0_groups),
	FUNCTION("spi1", cnllp_spi1_groups),
	FUNCTION("spi2", cnllp_spi2_groups),
	FUNCTION("i2c0", cnllp_i2c0_groups),
	FUNCTION("i2c1", cnllp_i2c1_groups),
	FUNCTION("i2c2", cnllp_i2c2_groups),
	FUNCTION("i2c3", cnllp_i2c3_groups),
	FUNCTION("i2c4", cnllp_i2c4_groups),
	FUNCTION("i2c5", cnllp_i2c5_groups),
	FUNCTION("uart0", cnllp_uart0_groups),
	FUNCTION("uart1", cnllp_uart1_groups),
	FUNCTION("uart2", cnllp_uart2_groups),
};

static const struct intel_padgroup cnllp_community0_gpps[] = {
	CNL_GPP(0, 0, 24),	/* GPP_A */
	CNL_GPP(1, 25, 50),	/* GPP_B */
	CNL_GPP(2, 51, 58),	/* GPP_G */
	CNL_GPP(3, 59, 67),	/* SPI */
};

static const struct intel_padgroup cnllp_community1_gpps[] = {
	CNL_GPP(0, 68, 92),	/* GPP_D */
	CNL_GPP(1, 93, 116),	/* GPP_F */
	CNL_GPP(2, 117, 140),	/* GPP_H */
	CNL_GPP(3, 141, 172),	/* vGPIO */
	CNL_GPP(4, 173, 180),	/* vGPIO */
};

static const struct intel_padgroup cnllp_community4_gpps[] = {
	CNL_GPP(0, 181, 204),	/* GPP_C */
	CNL_GPP(1, 205, 228),	/* GPP_E */
	CNL_GPP(2, 229, 237),	/* JTAG */
	CNL_GPP(3, 238, 243),	/* HVCMOS */
};

static const struct intel_community cnllp_communities[] = {
	CNL_COMMUNITY(0, 0, 67, cnllp_community0_gpps),
	CNL_COMMUNITY(1, 68, 180, cnllp_community1_gpps),
	CNL_COMMUNITY(2, 181, 243, cnllp_community4_gpps),
};

static const struct intel_pinctrl_soc_data cnllp_soc_data = {
	.pins = cnllp_pins,
	.npins = ARRAY_SIZE(cnllp_pins),
	.groups = cnllp_groups,
	.ngroups = ARRAY_SIZE(cnllp_groups),
	.functions = cnllp_functions,
	.nfunctions = ARRAY_SIZE(cnllp_functions),
	.communities = cnllp_communities,
	.ncommunities = ARRAY_SIZE(cnllp_communities),
};

static const struct acpi_device_id cnl_pinctrl_acpi_match[] = {
	{ "INT3450", (kernel_ulong_t)&cnlh_soc_data },
	{ "INT34BB", (kernel_ulong_t)&cnllp_soc_data },
	{ },
};
MODULE_DEVICE_TABLE(acpi, cnl_pinctrl_acpi_match);

static int cnl_pinctrl_probe(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data *soc_data;
	const struct acpi_device_id *id;

	id = acpi_match_device(cnl_pinctrl_acpi_match, &pdev->dev);
	if (!id || !id->driver_data)
		return -ENODEV;

	soc_data = (const struct intel_pinctrl_soc_data *)id->driver_data;
	return intel_pinctrl_probe(pdev, soc_data);
}

static const struct dev_pm_ops cnl_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(intel_pinctrl_suspend,
				     intel_pinctrl_resume)
};

static struct platform_driver cnl_pinctrl_driver = {
	.probe = cnl_pinctrl_probe,
	.driver = {
		.name = "cannonlake-pinctrl",
		.acpi_match_table = cnl_pinctrl_acpi_match,
		.pm = &cnl_pinctrl_pm_ops,
	},
};

module_platform_driver(cnl_pinctrl_driver);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel Cannon Lake PCH pinctrl/GPIO driver");
MODULE_LICENSE("GPL v2");
