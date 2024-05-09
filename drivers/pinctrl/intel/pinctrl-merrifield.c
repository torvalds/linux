// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Merrifield SoC pinctrl driver
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-intel.h"
#include "pinctrl-tangier.h"

static const struct pinctrl_pin_desc mrfld_pins[] = {
	/* Family 0: OCP2SSC (0 pins) */
	/* Family 1: ULPI (13 pins) */
	PINCTRL_PIN(0, "ULPI_CLK"),
	PINCTRL_PIN(1, "ULPI_D0"),
	PINCTRL_PIN(2, "ULPI_D1"),
	PINCTRL_PIN(3, "ULPI_D2"),
	PINCTRL_PIN(4, "ULPI_D3"),
	PINCTRL_PIN(5, "ULPI_D4"),
	PINCTRL_PIN(6, "ULPI_D5"),
	PINCTRL_PIN(7, "ULPI_D6"),
	PINCTRL_PIN(8, "ULPI_D7"),
	PINCTRL_PIN(9, "ULPI_DIR"),
	PINCTRL_PIN(10, "ULPI_NXT"),
	PINCTRL_PIN(11, "ULPI_REFCLK"),
	PINCTRL_PIN(12, "ULPI_STP"),
	/* Family 2: eMMC (24 pins) */
	PINCTRL_PIN(13, "EMMC_CLK"),
	PINCTRL_PIN(14, "EMMC_CMD"),
	PINCTRL_PIN(15, "EMMC_D0"),
	PINCTRL_PIN(16, "EMMC_D1"),
	PINCTRL_PIN(17, "EMMC_D2"),
	PINCTRL_PIN(18, "EMMC_D3"),
	PINCTRL_PIN(19, "EMMC_D4"),
	PINCTRL_PIN(20, "EMMC_D5"),
	PINCTRL_PIN(21, "EMMC_D6"),
	PINCTRL_PIN(22, "EMMC_D7"),
	PINCTRL_PIN(23, "EMMC_RST_N"),
	PINCTRL_PIN(24, "GP154"),
	PINCTRL_PIN(25, "GP155"),
	PINCTRL_PIN(26, "GP156"),
	PINCTRL_PIN(27, "GP157"),
	PINCTRL_PIN(28, "GP158"),
	PINCTRL_PIN(29, "GP159"),
	PINCTRL_PIN(30, "GP160"),
	PINCTRL_PIN(31, "GP161"),
	PINCTRL_PIN(32, "GP162"),
	PINCTRL_PIN(33, "GP163"),
	PINCTRL_PIN(34, "GP97"),
	PINCTRL_PIN(35, "GP14"),
	PINCTRL_PIN(36, "GP15"),
	/* Family 3: SDIO (20 pins) */
	PINCTRL_PIN(37, "GP77_SD_CD"),
	PINCTRL_PIN(38, "GP78_SD_CLK"),
	PINCTRL_PIN(39, "GP79_SD_CMD"),
	PINCTRL_PIN(40, "GP80_SD_D0"),
	PINCTRL_PIN(41, "GP81_SD_D1"),
	PINCTRL_PIN(42, "GP82_SD_D2"),
	PINCTRL_PIN(43, "GP83_SD_D3"),
	PINCTRL_PIN(44, "GP84_SD_LS_CLK_FB"),
	PINCTRL_PIN(45, "GP85_SD_LS_CMD_DIR"),
	PINCTRL_PIN(46, "GP86_SD_LS_D_DIR"),
	PINCTRL_PIN(47, "GP88_SD_LS_SEL"),
	PINCTRL_PIN(48, "GP87_SD_PD"),
	PINCTRL_PIN(49, "GP89_SD_WP"),
	PINCTRL_PIN(50, "GP90_SDIO_CLK"),
	PINCTRL_PIN(51, "GP91_SDIO_CMD"),
	PINCTRL_PIN(52, "GP92_SDIO_D0"),
	PINCTRL_PIN(53, "GP93_SDIO_D1"),
	PINCTRL_PIN(54, "GP94_SDIO_D2"),
	PINCTRL_PIN(55, "GP95_SDIO_D3"),
	PINCTRL_PIN(56, "GP96_SDIO_PD"),
	/* Family 4: HSI (8 pins) */
	PINCTRL_PIN(57, "HSI_ACDATA"),
	PINCTRL_PIN(58, "HSI_ACFLAG"),
	PINCTRL_PIN(59, "HSI_ACREADY"),
	PINCTRL_PIN(60, "HSI_ACWAKE"),
	PINCTRL_PIN(61, "HSI_CADATA"),
	PINCTRL_PIN(62, "HSI_CAFLAG"),
	PINCTRL_PIN(63, "HSI_CAREADY"),
	PINCTRL_PIN(64, "HSI_CAWAKE"),
	/* Family 5: SSP Audio (14 pins) */
	PINCTRL_PIN(65, "GP70"),
	PINCTRL_PIN(66, "GP71"),
	PINCTRL_PIN(67, "GP32_I2S_0_CLK"),
	PINCTRL_PIN(68, "GP33_I2S_0_FS"),
	PINCTRL_PIN(69, "GP34_I2S_0_RXD"),
	PINCTRL_PIN(70, "GP35_I2S_0_TXD"),
	PINCTRL_PIN(71, "GP36_I2S_1_CLK"),
	PINCTRL_PIN(72, "GP37_I2S_1_FS"),
	PINCTRL_PIN(73, "GP38_I2S_1_RXD"),
	PINCTRL_PIN(74, "GP39_I2S_1_TXD"),
	PINCTRL_PIN(75, "GP40_I2S_2_CLK"),
	PINCTRL_PIN(76, "GP41_I2S_2_FS"),
	PINCTRL_PIN(77, "GP42_I2S_2_RXD"),
	PINCTRL_PIN(78, "GP43_I2S_2_TXD"),
	/* Family 6: GP SSP (22 pins) */
	PINCTRL_PIN(79, "GP120_SPI_0_CLK"),
	PINCTRL_PIN(80, "GP121_SPI_0_SS"),
	PINCTRL_PIN(81, "GP122_SPI_0_RXD"),
	PINCTRL_PIN(82, "GP123_SPI_0_TXD"),
	PINCTRL_PIN(83, "GP102_SPI_1_CLK"),
	PINCTRL_PIN(84, "GP103_SPI_1_SS0"),
	PINCTRL_PIN(85, "GP104_SPI_1_SS1"),
	PINCTRL_PIN(86, "GP105_SPI_1_SS2"),
	PINCTRL_PIN(87, "GP106_SPI_1_SS3"),
	PINCTRL_PIN(88, "GP107_SPI_1_RXD"),
	PINCTRL_PIN(89, "GP108_SPI_1_TXD"),
	PINCTRL_PIN(90, "GP109_SPI_2_CLK"),
	PINCTRL_PIN(91, "GP110_SPI_2_SS0"),
	PINCTRL_PIN(92, "GP111_SPI_2_SS1"),
	PINCTRL_PIN(93, "GP112_SPI_2_SS2"),
	PINCTRL_PIN(94, "GP113_SPI_2_SS3"),
	PINCTRL_PIN(95, "GP114_SPI_2_RXD"),
	PINCTRL_PIN(96, "GP115_SPI_2_TXD"),
	PINCTRL_PIN(97, "GP116_SPI_3_CLK"),
	PINCTRL_PIN(98, "GP117_SPI_3_SS"),
	PINCTRL_PIN(99, "GP118_SPI_3_RXD"),
	PINCTRL_PIN(100, "GP119_SPI_3_TXD"),
	/* Family 7: I2C (14 pins) */
	PINCTRL_PIN(101, "GP19_I2C_1_SCL"),
	PINCTRL_PIN(102, "GP20_I2C_1_SDA"),
	PINCTRL_PIN(103, "GP21_I2C_2_SCL"),
	PINCTRL_PIN(104, "GP22_I2C_2_SDA"),
	PINCTRL_PIN(105, "GP17_I2C_3_SCL_HDMI"),
	PINCTRL_PIN(106, "GP18_I2C_3_SDA_HDMI"),
	PINCTRL_PIN(107, "GP23_I2C_4_SCL"),
	PINCTRL_PIN(108, "GP24_I2C_4_SDA"),
	PINCTRL_PIN(109, "GP25_I2C_5_SCL"),
	PINCTRL_PIN(110, "GP26_I2C_5_SDA"),
	PINCTRL_PIN(111, "GP27_I2C_6_SCL"),
	PINCTRL_PIN(112, "GP28_I2C_6_SDA"),
	PINCTRL_PIN(113, "GP29_I2C_7_SCL"),
	PINCTRL_PIN(114, "GP30_I2C_7_SDA"),
	/* Family 8: UART (12 pins) */
	PINCTRL_PIN(115, "GP124_UART_0_CTS"),
	PINCTRL_PIN(116, "GP125_UART_0_RTS"),
	PINCTRL_PIN(117, "GP126_UART_0_RX"),
	PINCTRL_PIN(118, "GP127_UART_0_TX"),
	PINCTRL_PIN(119, "GP128_UART_1_CTS"),
	PINCTRL_PIN(120, "GP129_UART_1_RTS"),
	PINCTRL_PIN(121, "GP130_UART_1_RX"),
	PINCTRL_PIN(122, "GP131_UART_1_TX"),
	PINCTRL_PIN(123, "GP132_UART_2_CTS"),
	PINCTRL_PIN(124, "GP133_UART_2_RTS"),
	PINCTRL_PIN(125, "GP134_UART_2_RX"),
	PINCTRL_PIN(126, "GP135_UART_2_TX"),
	/* Family 9: GPIO South (19 pins) */
	PINCTRL_PIN(127, "GP177"),
	PINCTRL_PIN(128, "GP178"),
	PINCTRL_PIN(129, "GP179"),
	PINCTRL_PIN(130, "GP180"),
	PINCTRL_PIN(131, "GP181"),
	PINCTRL_PIN(132, "GP182_PWM2"),
	PINCTRL_PIN(133, "GP183_PWM3"),
	PINCTRL_PIN(134, "GP184"),
	PINCTRL_PIN(135, "GP185"),
	PINCTRL_PIN(136, "GP186"),
	PINCTRL_PIN(137, "GP187"),
	PINCTRL_PIN(138, "GP188"),
	PINCTRL_PIN(139, "GP189"),
	PINCTRL_PIN(140, "GP64_FAST_INT0"),
	PINCTRL_PIN(141, "GP65_FAST_INT1"),
	PINCTRL_PIN(142, "GP66_FAST_INT2"),
	PINCTRL_PIN(143, "GP67_FAST_INT3"),
	PINCTRL_PIN(144, "GP12_PWM0"),
	PINCTRL_PIN(145, "GP13_PWM1"),
	/* Family 10: Camera Sideband (12 pins) */
	PINCTRL_PIN(146, "GP0"),
	PINCTRL_PIN(147, "GP1"),
	PINCTRL_PIN(148, "GP2"),
	PINCTRL_PIN(149, "GP3"),
	PINCTRL_PIN(150, "GP4"),
	PINCTRL_PIN(151, "GP5"),
	PINCTRL_PIN(152, "GP6"),
	PINCTRL_PIN(153, "GP7"),
	PINCTRL_PIN(154, "GP8"),
	PINCTRL_PIN(155, "GP9"),
	PINCTRL_PIN(156, "GP10"),
	PINCTRL_PIN(157, "GP11"),
	/* Family 11: Clock (22 pins) */
	PINCTRL_PIN(158, "GP137"),
	PINCTRL_PIN(159, "GP138"),
	PINCTRL_PIN(160, "GP139"),
	PINCTRL_PIN(161, "GP140"),
	PINCTRL_PIN(162, "GP141"),
	PINCTRL_PIN(163, "GP142"),
	PINCTRL_PIN(164, "GP16_HDMI_HPD"),
	PINCTRL_PIN(165, "GP68_DSI_A_TE"),
	PINCTRL_PIN(166, "GP69_DSI_C_TE"),
	PINCTRL_PIN(167, "OSC_CLK_CTRL0"),
	PINCTRL_PIN(168, "OSC_CLK_CTRL1"),
	PINCTRL_PIN(169, "OSC_CLK0"),
	PINCTRL_PIN(170, "OSC_CLK1"),
	PINCTRL_PIN(171, "OSC_CLK2"),
	PINCTRL_PIN(172, "OSC_CLK3"),
	PINCTRL_PIN(173, "OSC_CLK4"),
	PINCTRL_PIN(174, "RESETOUT"),
	PINCTRL_PIN(175, "PMODE"),
	PINCTRL_PIN(176, "PRDY"),
	PINCTRL_PIN(177, "PREQ"),
	PINCTRL_PIN(178, "GP190"),
	PINCTRL_PIN(179, "GP191"),
	/* Family 12: MSIC (15 pins) */
	PINCTRL_PIN(180, "I2C_0_SCL"),
	PINCTRL_PIN(181, "I2C_0_SDA"),
	PINCTRL_PIN(182, "IERR"),
	PINCTRL_PIN(183, "JTAG_TCK"),
	PINCTRL_PIN(184, "JTAG_TDI"),
	PINCTRL_PIN(185, "JTAG_TDO"),
	PINCTRL_PIN(186, "JTAG_TMS"),
	PINCTRL_PIN(187, "JTAG_TRST"),
	PINCTRL_PIN(188, "PROCHOT"),
	PINCTRL_PIN(189, "RTC_CLK"),
	PINCTRL_PIN(190, "SVID_ALERT"),
	PINCTRL_PIN(191, "SVID_CLK"),
	PINCTRL_PIN(192, "SVID_D"),
	PINCTRL_PIN(193, "THERMTRIP"),
	PINCTRL_PIN(194, "STANDBY"),
	/* Family 13: Keyboard (20 pins) */
	PINCTRL_PIN(195, "GP44"),
	PINCTRL_PIN(196, "GP45"),
	PINCTRL_PIN(197, "GP46"),
	PINCTRL_PIN(198, "GP47"),
	PINCTRL_PIN(199, "GP48"),
	PINCTRL_PIN(200, "GP49"),
	PINCTRL_PIN(201, "GP50"),
	PINCTRL_PIN(202, "GP51"),
	PINCTRL_PIN(203, "GP52"),
	PINCTRL_PIN(204, "GP53"),
	PINCTRL_PIN(205, "GP54"),
	PINCTRL_PIN(206, "GP55"),
	PINCTRL_PIN(207, "GP56"),
	PINCTRL_PIN(208, "GP57"),
	PINCTRL_PIN(209, "GP58"),
	PINCTRL_PIN(210, "GP59"),
	PINCTRL_PIN(211, "GP60"),
	PINCTRL_PIN(212, "GP61"),
	PINCTRL_PIN(213, "GP62"),
	PINCTRL_PIN(214, "GP63"),
	/* Family 14: GPIO North (13 pins) */
	PINCTRL_PIN(215, "GP164"),
	PINCTRL_PIN(216, "GP165"),
	PINCTRL_PIN(217, "GP166"),
	PINCTRL_PIN(218, "GP167"),
	PINCTRL_PIN(219, "GP168_MJTAG_TCK"),
	PINCTRL_PIN(220, "GP169_MJTAG_TDI"),
	PINCTRL_PIN(221, "GP170_MJTAG_TDO"),
	PINCTRL_PIN(222, "GP171_MJTAG_TMS"),
	PINCTRL_PIN(223, "GP172_MJTAG_TRST"),
	PINCTRL_PIN(224, "GP173"),
	PINCTRL_PIN(225, "GP174"),
	PINCTRL_PIN(226, "GP175"),
	PINCTRL_PIN(227, "GP176"),
	/* Family 15: PTI (5 pins) */
	PINCTRL_PIN(228, "GP72_PTI_CLK"),
	PINCTRL_PIN(229, "GP73_PTI_D0"),
	PINCTRL_PIN(230, "GP74_PTI_D1"),
	PINCTRL_PIN(231, "GP75_PTI_D2"),
	PINCTRL_PIN(232, "GP76_PTI_D3"),
	/* Family 16: USB3 (0 pins) */
	/* Family 17: HSIC (0 pins) */
	/* Family 18: Broadcast (0 pins) */
};

static const unsigned int mrfld_sdio_pins[] = { 50, 51, 52, 53, 54, 55, 56 };
static const unsigned int mrfld_i2s2_pins[] = { 75, 76, 77, 78 };
static const unsigned int mrfld_spi5_pins[] = { 90, 91, 92, 93, 94, 95, 96 };
static const unsigned int mrfld_uart0_pins[] = { 115, 116, 117, 118 };
static const unsigned int mrfld_uart1_pins[] = { 119, 120, 121, 122 };
static const unsigned int mrfld_uart2_pins[] = { 123, 124, 125, 126 };
static const unsigned int mrfld_pwm0_pins[] = { 144 };
static const unsigned int mrfld_pwm1_pins[] = { 145 };
static const unsigned int mrfld_pwm2_pins[] = { 132 };
static const unsigned int mrfld_pwm3_pins[] = { 133 };

static const struct intel_pingroup mrfld_groups[] = {
	PIN_GROUP("sdio_grp", mrfld_sdio_pins, 1),
	PIN_GROUP("i2s2_grp", mrfld_i2s2_pins, 1),
	PIN_GROUP("spi5_grp", mrfld_spi5_pins, 1),
	PIN_GROUP("uart0_grp", mrfld_uart0_pins, 1),
	PIN_GROUP("uart1_grp", mrfld_uart1_pins, 1),
	PIN_GROUP("uart2_grp", mrfld_uart2_pins, 1),
	PIN_GROUP("pwm0_grp", mrfld_pwm0_pins, 1),
	PIN_GROUP("pwm1_grp", mrfld_pwm1_pins, 1),
	PIN_GROUP("pwm2_grp", mrfld_pwm2_pins, 1),
	PIN_GROUP("pwm3_grp", mrfld_pwm3_pins, 1),
};

static const char * const mrfld_sdio_groups[] = { "sdio_grp" };
static const char * const mrfld_i2s2_groups[] = { "i2s2_grp" };
static const char * const mrfld_spi5_groups[] = { "spi5_grp" };
static const char * const mrfld_uart0_groups[] = { "uart0_grp" };
static const char * const mrfld_uart1_groups[] = { "uart1_grp" };
static const char * const mrfld_uart2_groups[] = { "uart2_grp" };
static const char * const mrfld_pwm0_groups[] = { "pwm0_grp" };
static const char * const mrfld_pwm1_groups[] = { "pwm1_grp" };
static const char * const mrfld_pwm2_groups[] = { "pwm2_grp" };
static const char * const mrfld_pwm3_groups[] = { "pwm3_grp" };

static const struct intel_function mrfld_functions[] = {
	FUNCTION("sdio", mrfld_sdio_groups),
	FUNCTION("i2s2", mrfld_i2s2_groups),
	FUNCTION("spi5", mrfld_spi5_groups),
	FUNCTION("uart0", mrfld_uart0_groups),
	FUNCTION("uart1", mrfld_uart1_groups),
	FUNCTION("uart2", mrfld_uart2_groups),
	FUNCTION("pwm0", mrfld_pwm0_groups),
	FUNCTION("pwm1", mrfld_pwm1_groups),
	FUNCTION("pwm2", mrfld_pwm2_groups),
	FUNCTION("pwm3", mrfld_pwm3_groups),
};

static const struct tng_family mrfld_families[] = {
	TNG_FAMILY(1, 0, 12),
	TNG_FAMILY(2, 13, 36),
	TNG_FAMILY(3, 37, 56),
	TNG_FAMILY(4, 57, 64),
	TNG_FAMILY(5, 65, 78),
	TNG_FAMILY(6, 79, 100),
	TNG_FAMILY_PROTECTED(7, 101, 114),
	TNG_FAMILY(8, 115, 126),
	TNG_FAMILY(9, 127, 145),
	TNG_FAMILY(10, 146, 157),
	TNG_FAMILY(11, 158, 179),
	TNG_FAMILY_PROTECTED(12, 180, 194),
	TNG_FAMILY(13, 195, 214),
	TNG_FAMILY(14, 215, 227),
	TNG_FAMILY(15, 228, 232),
};

static const struct tng_pinctrl mrfld_soc_data = {
	.pins = mrfld_pins,
	.npins = ARRAY_SIZE(mrfld_pins),
	.groups = mrfld_groups,
	.ngroups = ARRAY_SIZE(mrfld_groups),
	.families = mrfld_families,
	.nfamilies = ARRAY_SIZE(mrfld_families),
	.functions = mrfld_functions,
	.nfunctions = ARRAY_SIZE(mrfld_functions),
};

static const struct acpi_device_id mrfld_acpi_table[] = {
	{ "INTC1002", (kernel_ulong_t)&mrfld_soc_data },
	{ }
};
MODULE_DEVICE_TABLE(acpi, mrfld_acpi_table);

static struct platform_driver mrfld_pinctrl_driver = {
	.probe = devm_tng_pinctrl_probe,
	.driver = {
		.name = "pinctrl-merrifield",
		.acpi_match_table = mrfld_acpi_table,
	},
};

static int __init mrfld_pinctrl_init(void)
{
	return platform_driver_register(&mrfld_pinctrl_driver);
}
subsys_initcall(mrfld_pinctrl_init);

static void __exit mrfld_pinctrl_exit(void)
{
	platform_driver_unregister(&mrfld_pinctrl_driver);
}
module_exit(mrfld_pinctrl_exit);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel Merrifield SoC pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pinctrl-merrifield");
MODULE_IMPORT_NS(PINCTRL_TANGIER);
