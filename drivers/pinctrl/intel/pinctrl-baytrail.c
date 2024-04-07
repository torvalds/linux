// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl GPIO driver for Intel Baytrail
 *
 * Copyright (c) 2012-2013, Intel Corporation
 * Author: Mathias Nyman <mathias.nyman@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/string_helpers.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "pinctrl-intel.h"

/* memory mapped register offsets */
#define BYT_CONF0_REG		0x000
#define BYT_CONF1_REG		0x004
#define BYT_VAL_REG		0x008
#define BYT_DFT_REG		0x00c
#define BYT_INT_STAT_REG	0x800
#define BYT_DIRECT_IRQ_REG	0x980
#define BYT_DEBOUNCE_REG	0x9d0

/* BYT_CONF0_REG register bits */
#define BYT_IODEN		BIT(31)
#define BYT_DIRECT_IRQ_EN	BIT(27)
#define BYT_TRIG_MASK		GENMASK(26, 24)
#define BYT_TRIG_NEG		BIT(26)
#define BYT_TRIG_POS		BIT(25)
#define BYT_TRIG_LVL		BIT(24)
#define BYT_DEBOUNCE_EN		BIT(20)
#define BYT_GLITCH_FILTER_EN	BIT(19)
#define BYT_GLITCH_F_SLOW_CLK	BIT(17)
#define BYT_GLITCH_F_FAST_CLK	BIT(16)
#define BYT_PULL_STR_SHIFT	9
#define BYT_PULL_STR_MASK	GENMASK(10, 9)
#define BYT_PULL_STR_2K		(0 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_10K	(1 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_20K	(2 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_STR_40K	(3 << BYT_PULL_STR_SHIFT)
#define BYT_PULL_ASSIGN_MASK	GENMASK(8, 7)
#define BYT_PULL_ASSIGN_DOWN	BIT(8)
#define BYT_PULL_ASSIGN_UP	BIT(7)
#define BYT_PIN_MUX		GENMASK(2, 0)

/* BYT_VAL_REG register bits */
#define BYT_DIR_MASK		GENMASK(2, 1)
#define BYT_INPUT_EN		BIT(2)  /* 0: input enabled (active low)*/
#define BYT_OUTPUT_EN		BIT(1)  /* 0: output enabled (active low)*/
#define BYT_LEVEL		BIT(0)

#define BYT_CONF0_RESTORE_MASK	(BYT_DIRECT_IRQ_EN | BYT_TRIG_MASK | BYT_PIN_MUX)
#define BYT_VAL_RESTORE_MASK	(BYT_DIR_MASK | BYT_LEVEL)

/* BYT_DEBOUNCE_REG bits */
#define BYT_DEBOUNCE_PULSE_MASK		GENMASK(2, 0)
#define BYT_DEBOUNCE_PULSE_375US	1
#define BYT_DEBOUNCE_PULSE_750US	2
#define BYT_DEBOUNCE_PULSE_1500US	3
#define BYT_DEBOUNCE_PULSE_3MS		4
#define BYT_DEBOUNCE_PULSE_6MS		5
#define BYT_DEBOUNCE_PULSE_12MS		6
#define BYT_DEBOUNCE_PULSE_24MS		7

#define BYT_NGPIO_SCORE		102
#define BYT_NGPIO_NCORE		28
#define BYT_NGPIO_SUS		44

#define BYT_SCORE_ACPI_UID	"1"
#define BYT_NCORE_ACPI_UID	"2"
#define BYT_SUS_ACPI_UID	"3"

/*
 * This is the function value most pins have for GPIO muxing. If the value
 * differs from the default one, it must be explicitly mentioned. Otherwise, the
 * pin control implementation will set the muxing value to default GPIO if it
 * does not find a match for the requested function.
 */
#define BYT_DEFAULT_GPIO_MUX	0
#define BYT_ALTER_GPIO_MUX	1

struct intel_pad_context {
	u32 conf0;
	u32 val;
};

#define COMMUNITY(p, n, map)		\
	{				\
		.pin_base	= (p),	\
		.npins		= (n),	\
		.pad_map	= (map),\
	}

/* SCORE pins, aka GPIOC_<pin_no> or GPIO_S0_SC[<pin_no>] */
static const struct pinctrl_pin_desc byt_score_pins[] = {
	PINCTRL_PIN(0, "SATA_GP0"),
	PINCTRL_PIN(1, "SATA_GP1"),
	PINCTRL_PIN(2, "SATA_LED#"),
	PINCTRL_PIN(3, "PCIE_CLKREQ0"),
	PINCTRL_PIN(4, "PCIE_CLKREQ1"),
	PINCTRL_PIN(5, "PCIE_CLKREQ2"),
	PINCTRL_PIN(6, "PCIE_CLKREQ3"),
	PINCTRL_PIN(7, "SD3_WP"),
	PINCTRL_PIN(8, "HDA_RST"),
	PINCTRL_PIN(9, "HDA_SYNC"),
	PINCTRL_PIN(10, "HDA_CLK"),
	PINCTRL_PIN(11, "HDA_SDO"),
	PINCTRL_PIN(12, "HDA_SDI0"),
	PINCTRL_PIN(13, "HDA_SDI1"),
	PINCTRL_PIN(14, "GPIO_S0_SC14"),
	PINCTRL_PIN(15, "GPIO_S0_SC15"),
	PINCTRL_PIN(16, "MMC1_CLK"),
	PINCTRL_PIN(17, "MMC1_D0"),
	PINCTRL_PIN(18, "MMC1_D1"),
	PINCTRL_PIN(19, "MMC1_D2"),
	PINCTRL_PIN(20, "MMC1_D3"),
	PINCTRL_PIN(21, "MMC1_D4"),
	PINCTRL_PIN(22, "MMC1_D5"),
	PINCTRL_PIN(23, "MMC1_D6"),
	PINCTRL_PIN(24, "MMC1_D7"),
	PINCTRL_PIN(25, "MMC1_CMD"),
	PINCTRL_PIN(26, "MMC1_RST"),
	PINCTRL_PIN(27, "SD2_CLK"),
	PINCTRL_PIN(28, "SD2_D0"),
	PINCTRL_PIN(29, "SD2_D1"),
	PINCTRL_PIN(30, "SD2_D2"),
	PINCTRL_PIN(31, "SD2_D3_CD"),
	PINCTRL_PIN(32, "SD2_CMD"),
	PINCTRL_PIN(33, "SD3_CLK"),
	PINCTRL_PIN(34, "SD3_D0"),
	PINCTRL_PIN(35, "SD3_D1"),
	PINCTRL_PIN(36, "SD3_D2"),
	PINCTRL_PIN(37, "SD3_D3"),
	PINCTRL_PIN(38, "SD3_CD"),
	PINCTRL_PIN(39, "SD3_CMD"),
	PINCTRL_PIN(40, "SD3_1P8EN"),
	PINCTRL_PIN(41, "SD3_PWREN#"),
	PINCTRL_PIN(42, "ILB_LPC_AD0"),
	PINCTRL_PIN(43, "ILB_LPC_AD1"),
	PINCTRL_PIN(44, "ILB_LPC_AD2"),
	PINCTRL_PIN(45, "ILB_LPC_AD3"),
	PINCTRL_PIN(46, "ILB_LPC_FRAME"),
	PINCTRL_PIN(47, "ILB_LPC_CLK0"),
	PINCTRL_PIN(48, "ILB_LPC_CLK1"),
	PINCTRL_PIN(49, "ILB_LPC_CLKRUN"),
	PINCTRL_PIN(50, "ILB_LPC_SERIRQ"),
	PINCTRL_PIN(51, "PCU_SMB_DATA"),
	PINCTRL_PIN(52, "PCU_SMB_CLK"),
	PINCTRL_PIN(53, "PCU_SMB_ALERT"),
	PINCTRL_PIN(54, "ILB_8254_SPKR"),
	PINCTRL_PIN(55, "GPIO_S0_SC55"),
	PINCTRL_PIN(56, "GPIO_S0_SC56"),
	PINCTRL_PIN(57, "GPIO_S0_SC57"),
	PINCTRL_PIN(58, "GPIO_S0_SC58"),
	PINCTRL_PIN(59, "GPIO_S0_SC59"),
	PINCTRL_PIN(60, "GPIO_S0_SC60"),
	PINCTRL_PIN(61, "GPIO_S0_SC61"),
	PINCTRL_PIN(62, "LPE_I2S2_CLK"),
	PINCTRL_PIN(63, "LPE_I2S2_FRM"),
	PINCTRL_PIN(64, "LPE_I2S2_DATAIN"),
	PINCTRL_PIN(65, "LPE_I2S2_DATAOUT"),
	PINCTRL_PIN(66, "SIO_SPI_CS"),
	PINCTRL_PIN(67, "SIO_SPI_MISO"),
	PINCTRL_PIN(68, "SIO_SPI_MOSI"),
	PINCTRL_PIN(69, "SIO_SPI_CLK"),
	PINCTRL_PIN(70, "SIO_UART1_RXD"),
	PINCTRL_PIN(71, "SIO_UART1_TXD"),
	PINCTRL_PIN(72, "SIO_UART1_RTS"),
	PINCTRL_PIN(73, "SIO_UART1_CTS"),
	PINCTRL_PIN(74, "SIO_UART2_RXD"),
	PINCTRL_PIN(75, "SIO_UART2_TXD"),
	PINCTRL_PIN(76, "SIO_UART2_RTS"),
	PINCTRL_PIN(77, "SIO_UART2_CTS"),
	PINCTRL_PIN(78, "SIO_I2C0_DATA"),
	PINCTRL_PIN(79, "SIO_I2C0_CLK"),
	PINCTRL_PIN(80, "SIO_I2C1_DATA"),
	PINCTRL_PIN(81, "SIO_I2C1_CLK"),
	PINCTRL_PIN(82, "SIO_I2C2_DATA"),
	PINCTRL_PIN(83, "SIO_I2C2_CLK"),
	PINCTRL_PIN(84, "SIO_I2C3_DATA"),
	PINCTRL_PIN(85, "SIO_I2C3_CLK"),
	PINCTRL_PIN(86, "SIO_I2C4_DATA"),
	PINCTRL_PIN(87, "SIO_I2C4_CLK"),
	PINCTRL_PIN(88, "SIO_I2C5_DATA"),
	PINCTRL_PIN(89, "SIO_I2C5_CLK"),
	PINCTRL_PIN(90, "SIO_I2C6_DATA"),
	PINCTRL_PIN(91, "SIO_I2C6_CLK"),
	PINCTRL_PIN(92, "GPIO_S0_SC92"),
	PINCTRL_PIN(93, "GPIO_S0_SC93"),
	PINCTRL_PIN(94, "SIO_PWM0"),
	PINCTRL_PIN(95, "SIO_PWM1"),
	PINCTRL_PIN(96, "PMC_PLT_CLK0"),
	PINCTRL_PIN(97, "PMC_PLT_CLK1"),
	PINCTRL_PIN(98, "PMC_PLT_CLK2"),
	PINCTRL_PIN(99, "PMC_PLT_CLK3"),
	PINCTRL_PIN(100, "PMC_PLT_CLK4"),
	PINCTRL_PIN(101, "PMC_PLT_CLK5"),
};

static const unsigned int byt_score_pins_map[BYT_NGPIO_SCORE] = {
	85, 89, 93, 96, 99, 102, 98, 101, 34, 37,
	36, 38, 39, 35, 40, 84, 62, 61, 64, 59,
	54, 56, 60, 55, 63, 57, 51, 50, 53, 47,
	52, 49, 48, 43, 46, 41, 45, 42, 58, 44,
	95, 105, 70, 68, 67, 66, 69, 71, 65, 72,
	86, 90, 88, 92, 103, 77, 79, 83, 78, 81,
	80, 82, 13, 12, 15, 14, 17, 18, 19, 16,
	2, 1, 0, 4, 6, 7, 9, 8, 33, 32,
	31, 30, 29, 27, 25, 28, 26, 23, 21, 20,
	24, 22, 5, 3, 10, 11, 106, 87, 91, 104,
	97, 100,
};

/* SCORE groups */
static const unsigned int byt_score_uart1_pins[] = { 70, 71, 72, 73 };
static const unsigned int byt_score_uart2_pins[] = { 74, 75, 76, 77 };

static const unsigned int byt_score_pwm0_pins[] = { 94 };
static const unsigned int byt_score_pwm1_pins[] = { 95 };

static const unsigned int byt_score_sio_spi_pins[] = { 66, 67, 68, 69 };

static const unsigned int byt_score_i2c5_pins[] = { 88, 89 };
static const unsigned int byt_score_i2c6_pins[] = { 90, 91 };
static const unsigned int byt_score_i2c4_pins[] = { 86, 87 };
static const unsigned int byt_score_i2c3_pins[] = { 84, 85 };
static const unsigned int byt_score_i2c2_pins[] = { 82, 83 };
static const unsigned int byt_score_i2c1_pins[] = { 80, 81 };
static const unsigned int byt_score_i2c0_pins[] = { 78, 79 };

static const unsigned int byt_score_ssp0_pins[] = { 8, 9, 10, 11 };
static const unsigned int byt_score_ssp1_pins[] = { 12, 13, 14, 15 };
static const unsigned int byt_score_ssp2_pins[] = { 62, 63, 64, 65 };

static const unsigned int byt_score_sdcard_pins[] = {
	7, 33, 34, 35, 36, 37, 38, 39, 40, 41,
};
static const unsigned int byt_score_sdcard_mux_values[] = {
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const unsigned int byt_score_sdio_pins[] = { 27, 28, 29, 30, 31, 32 };

static const unsigned int byt_score_emmc_pins[] = {
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
};

static const unsigned int byt_score_ilb_lpc_pins[] = {
	42, 43, 44, 45, 46, 47, 48, 49, 50,
};

static const unsigned int byt_score_sata_pins[] = { 0, 1, 2 };

static const unsigned int byt_score_plt_clk0_pins[] = { 96 };
static const unsigned int byt_score_plt_clk1_pins[] = { 97 };
static const unsigned int byt_score_plt_clk2_pins[] = { 98 };
static const unsigned int byt_score_plt_clk3_pins[] = { 99 };
static const unsigned int byt_score_plt_clk4_pins[] = { 100 };
static const unsigned int byt_score_plt_clk5_pins[] = { 101 };

static const unsigned int byt_score_smbus_pins[] = { 51, 52, 53 };

static const struct intel_pingroup byt_score_groups[] = {
	PIN_GROUP_GPIO("uart1_grp", byt_score_uart1_pins, 1),
	PIN_GROUP_GPIO("uart2_grp", byt_score_uart2_pins, 1),
	PIN_GROUP_GPIO("pwm0_grp", byt_score_pwm0_pins, 1),
	PIN_GROUP_GPIO("pwm1_grp", byt_score_pwm1_pins, 1),
	PIN_GROUP_GPIO("ssp2_grp", byt_score_ssp2_pins, 1),
	PIN_GROUP_GPIO("sio_spi_grp", byt_score_sio_spi_pins, 1),
	PIN_GROUP_GPIO("i2c5_grp", byt_score_i2c5_pins, 1),
	PIN_GROUP_GPIO("i2c6_grp", byt_score_i2c6_pins, 1),
	PIN_GROUP_GPIO("i2c4_grp", byt_score_i2c4_pins, 1),
	PIN_GROUP_GPIO("i2c3_grp", byt_score_i2c3_pins, 1),
	PIN_GROUP_GPIO("i2c2_grp", byt_score_i2c2_pins, 1),
	PIN_GROUP_GPIO("i2c1_grp", byt_score_i2c1_pins, 1),
	PIN_GROUP_GPIO("i2c0_grp", byt_score_i2c0_pins, 1),
	PIN_GROUP_GPIO("ssp0_grp", byt_score_ssp0_pins, 1),
	PIN_GROUP_GPIO("ssp1_grp", byt_score_ssp1_pins, 1),
	PIN_GROUP_GPIO("sdcard_grp", byt_score_sdcard_pins, byt_score_sdcard_mux_values),
	PIN_GROUP_GPIO("sdio_grp", byt_score_sdio_pins, 1),
	PIN_GROUP_GPIO("emmc_grp", byt_score_emmc_pins, 1),
	PIN_GROUP_GPIO("lpc_grp", byt_score_ilb_lpc_pins, 1),
	PIN_GROUP_GPIO("sata_grp", byt_score_sata_pins, 1),
	PIN_GROUP_GPIO("plt_clk0_grp", byt_score_plt_clk0_pins, 1),
	PIN_GROUP_GPIO("plt_clk1_grp", byt_score_plt_clk1_pins, 1),
	PIN_GROUP_GPIO("plt_clk2_grp", byt_score_plt_clk2_pins, 1),
	PIN_GROUP_GPIO("plt_clk3_grp", byt_score_plt_clk3_pins, 1),
	PIN_GROUP_GPIO("plt_clk4_grp", byt_score_plt_clk4_pins, 1),
	PIN_GROUP_GPIO("plt_clk5_grp", byt_score_plt_clk5_pins, 1),
	PIN_GROUP_GPIO("smbus_grp", byt_score_smbus_pins, 1),
};

static const char * const byt_score_uart_groups[] = {
	"uart1_grp", "uart2_grp",
};
static const char * const byt_score_pwm_groups[] = {
	"pwm0_grp", "pwm1_grp",
};
static const char * const byt_score_ssp_groups[] = {
	"ssp0_grp", "ssp1_grp", "ssp2_grp",
};
static const char * const byt_score_spi_groups[] = { "sio_spi_grp" };
static const char * const byt_score_i2c_groups[] = {
	"i2c0_grp", "i2c1_grp", "i2c2_grp", "i2c3_grp", "i2c4_grp", "i2c5_grp",
	"i2c6_grp",
};
static const char * const byt_score_sdcard_groups[] = { "sdcard_grp" };
static const char * const byt_score_sdio_groups[] = { "sdio_grp" };
static const char * const byt_score_emmc_groups[] = { "emmc_grp" };
static const char * const byt_score_lpc_groups[] = { "lpc_grp" };
static const char * const byt_score_sata_groups[] = { "sata_grp" };
static const char * const byt_score_plt_clk_groups[] = {
	"plt_clk0_grp", "plt_clk1_grp", "plt_clk2_grp", "plt_clk3_grp",
	"plt_clk4_grp", "plt_clk5_grp",
};
static const char * const byt_score_smbus_groups[] = { "smbus_grp" };
static const char * const byt_score_gpio_groups[] = {
	"uart1_grp_gpio", "uart2_grp_gpio", "pwm0_grp_gpio",
	"pwm1_grp_gpio", "ssp0_grp_gpio", "ssp1_grp_gpio", "ssp2_grp_gpio",
	"sio_spi_grp_gpio", "i2c0_grp_gpio", "i2c1_grp_gpio", "i2c2_grp_gpio",
	"i2c3_grp_gpio", "i2c4_grp_gpio", "i2c5_grp_gpio", "i2c6_grp_gpio",
	"sdcard_grp_gpio", "sdio_grp_gpio", "emmc_grp_gpio", "lpc_grp_gpio",
	"sata_grp_gpio", "plt_clk0_grp_gpio", "plt_clk1_grp_gpio",
	"plt_clk2_grp_gpio", "plt_clk3_grp_gpio", "plt_clk4_grp_gpio",
	"plt_clk5_grp_gpio", "smbus_grp_gpio",
};

static const struct intel_function byt_score_functions[] = {
	FUNCTION("uart", byt_score_uart_groups),
	FUNCTION("pwm", byt_score_pwm_groups),
	FUNCTION("ssp", byt_score_ssp_groups),
	FUNCTION("spi", byt_score_spi_groups),
	FUNCTION("i2c", byt_score_i2c_groups),
	FUNCTION("sdcard", byt_score_sdcard_groups),
	FUNCTION("sdio", byt_score_sdio_groups),
	FUNCTION("emmc", byt_score_emmc_groups),
	FUNCTION("lpc", byt_score_lpc_groups),
	FUNCTION("sata", byt_score_sata_groups),
	FUNCTION("plt_clk", byt_score_plt_clk_groups),
	FUNCTION("smbus", byt_score_smbus_groups),
	FUNCTION("gpio", byt_score_gpio_groups),
};

static const struct intel_community byt_score_communities[] = {
	COMMUNITY(0, BYT_NGPIO_SCORE, byt_score_pins_map),
};

static const struct intel_pinctrl_soc_data byt_score_soc_data = {
	.uid		= BYT_SCORE_ACPI_UID,
	.pins		= byt_score_pins,
	.npins		= ARRAY_SIZE(byt_score_pins),
	.groups		= byt_score_groups,
	.ngroups	= ARRAY_SIZE(byt_score_groups),
	.functions	= byt_score_functions,
	.nfunctions	= ARRAY_SIZE(byt_score_functions),
	.communities	= byt_score_communities,
	.ncommunities	= ARRAY_SIZE(byt_score_communities),
};

/* SUS pins, aka GPIOS_<pin_no> or GPIO_S5[<pin_no>]  */
static const struct pinctrl_pin_desc byt_sus_pins[] = {
	PINCTRL_PIN(0, "GPIO_S50"),
	PINCTRL_PIN(1, "GPIO_S51"),
	PINCTRL_PIN(2, "GPIO_S52"),
	PINCTRL_PIN(3, "GPIO_S53"),
	PINCTRL_PIN(4, "GPIO_S54"),
	PINCTRL_PIN(5, "GPIO_S55"),
	PINCTRL_PIN(6, "GPIO_S56"),
	PINCTRL_PIN(7, "GPIO_S57"),
	PINCTRL_PIN(8, "GPIO_S58"),
	PINCTRL_PIN(9, "GPIO_S59"),
	PINCTRL_PIN(10, "GPIO_S510"),
	PINCTRL_PIN(11, "PMC_SUSPWRDNACK"),
	PINCTRL_PIN(12, "PMC_SUSCLK0"),
	PINCTRL_PIN(13, "GPIO_S513"),
	PINCTRL_PIN(14, "USB_ULPI_RST"),
	PINCTRL_PIN(15, "PMC_WAKE_PCIE0#"),
	PINCTRL_PIN(16, "PMC_PWRBTN"),
	PINCTRL_PIN(17, "GPIO_S517"),
	PINCTRL_PIN(18, "PMC_SUS_STAT"),
	PINCTRL_PIN(19, "USB_OC0"),
	PINCTRL_PIN(20, "USB_OC1"),
	PINCTRL_PIN(21, "PCU_SPI_CS1"),
	PINCTRL_PIN(22, "GPIO_S522"),
	PINCTRL_PIN(23, "GPIO_S523"),
	PINCTRL_PIN(24, "GPIO_S524"),
	PINCTRL_PIN(25, "GPIO_S525"),
	PINCTRL_PIN(26, "GPIO_S526"),
	PINCTRL_PIN(27, "GPIO_S527"),
	PINCTRL_PIN(28, "GPIO_S528"),
	PINCTRL_PIN(29, "GPIO_S529"),
	PINCTRL_PIN(30, "GPIO_S530"),
	PINCTRL_PIN(31, "USB_ULPI_CLK"),
	PINCTRL_PIN(32, "USB_ULPI_DATA0"),
	PINCTRL_PIN(33, "USB_ULPI_DATA1"),
	PINCTRL_PIN(34, "USB_ULPI_DATA2"),
	PINCTRL_PIN(35, "USB_ULPI_DATA3"),
	PINCTRL_PIN(36, "USB_ULPI_DATA4"),
	PINCTRL_PIN(37, "USB_ULPI_DATA5"),
	PINCTRL_PIN(38, "USB_ULPI_DATA6"),
	PINCTRL_PIN(39, "USB_ULPI_DATA7"),
	PINCTRL_PIN(40, "USB_ULPI_DIR"),
	PINCTRL_PIN(41, "USB_ULPI_NXT"),
	PINCTRL_PIN(42, "USB_ULPI_STP"),
	PINCTRL_PIN(43, "USB_ULPI_REFCLK"),
};

static const unsigned int byt_sus_pins_map[BYT_NGPIO_SUS] = {
	29, 33, 30, 31, 32, 34, 36, 35, 38, 37,
	18, 7, 11, 20, 17, 1, 8, 10, 19, 12,
	0, 2, 23, 39, 28, 27, 22, 21, 24, 25,
	26, 51, 56, 54, 49, 55, 48, 57, 50, 58,
	52, 53, 59, 40,
};

static const unsigned int byt_sus_usb_over_current_pins[] = { 19, 20 };
static const unsigned int byt_sus_usb_over_current_mode_values[] = { 0, 0 };
static const unsigned int byt_sus_usb_over_current_gpio_mode_values[] = { 1, 1 };

static const unsigned int byt_sus_usb_ulpi_pins[] = {
	14, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
};
static const unsigned int byt_sus_usb_ulpi_mode_values[] = {
	2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static const unsigned int byt_sus_usb_ulpi_gpio_mode_values[] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const unsigned int byt_sus_pcu_spi_pins[] = { 21 };
static const unsigned int byt_sus_pcu_spi_mode_values[] = { 0 };
static const unsigned int byt_sus_pcu_spi_gpio_mode_values[] = { 1 };

static const unsigned int byt_sus_pmu_clk1_pins[] = { 5 };
static const unsigned int byt_sus_pmu_clk2_pins[] = { 6 };

static const struct intel_pingroup byt_sus_groups[] = {
	PIN_GROUP("usb_oc_grp", byt_sus_usb_over_current_pins, byt_sus_usb_over_current_mode_values),
	PIN_GROUP("usb_ulpi_grp", byt_sus_usb_ulpi_pins, byt_sus_usb_ulpi_mode_values),
	PIN_GROUP("pcu_spi_grp", byt_sus_pcu_spi_pins, byt_sus_pcu_spi_mode_values),
	PIN_GROUP("usb_oc_grp_gpio", byt_sus_usb_over_current_pins, byt_sus_usb_over_current_gpio_mode_values),
	PIN_GROUP("usb_ulpi_grp_gpio", byt_sus_usb_ulpi_pins, byt_sus_usb_ulpi_gpio_mode_values),
	PIN_GROUP("pcu_spi_grp_gpio", byt_sus_pcu_spi_pins, byt_sus_pcu_spi_gpio_mode_values),
	PIN_GROUP_GPIO("pmu_clk1_grp", byt_sus_pmu_clk1_pins, 1),
	PIN_GROUP_GPIO("pmu_clk2_grp", byt_sus_pmu_clk2_pins, 1),
};

static const char * const byt_sus_usb_groups[] = {
	"usb_oc_grp", "usb_ulpi_grp",
};
static const char * const byt_sus_spi_groups[] = { "pcu_spi_grp" };
static const char * const byt_sus_pmu_clk_groups[] = {
	"pmu_clk1_grp", "pmu_clk2_grp",
};
static const char * const byt_sus_gpio_groups[] = {
	"usb_oc_grp_gpio", "usb_ulpi_grp_gpio", "pcu_spi_grp_gpio",
	"pmu_clk1_grp_gpio", "pmu_clk2_grp_gpio",
};

static const struct intel_function byt_sus_functions[] = {
	FUNCTION("usb", byt_sus_usb_groups),
	FUNCTION("spi", byt_sus_spi_groups),
	FUNCTION("gpio", byt_sus_gpio_groups),
	FUNCTION("pmu_clk", byt_sus_pmu_clk_groups),
};

static const struct intel_community byt_sus_communities[] = {
	COMMUNITY(0, BYT_NGPIO_SUS, byt_sus_pins_map),
};

static const struct intel_pinctrl_soc_data byt_sus_soc_data = {
	.uid		= BYT_SUS_ACPI_UID,
	.pins		= byt_sus_pins,
	.npins		= ARRAY_SIZE(byt_sus_pins),
	.groups		= byt_sus_groups,
	.ngroups	= ARRAY_SIZE(byt_sus_groups),
	.functions	= byt_sus_functions,
	.nfunctions	= ARRAY_SIZE(byt_sus_functions),
	.communities	= byt_sus_communities,
	.ncommunities	= ARRAY_SIZE(byt_sus_communities),
};

static const struct pinctrl_pin_desc byt_ncore_pins[] = {
	PINCTRL_PIN(0, "HV_DDI0_HPD"),
	PINCTRL_PIN(1, "HV_DDI0_DDC_SDA"),
	PINCTRL_PIN(2, "HV_DDI0_DDC_SCL"),
	PINCTRL_PIN(3, "PANEL0_VDDEN"),
	PINCTRL_PIN(4, "PANEL0_BKLTEN"),
	PINCTRL_PIN(5, "PANEL0_BKLTCTL"),
	PINCTRL_PIN(6, "HV_DDI1_HPD"),
	PINCTRL_PIN(7, "HV_DDI1_DDC_SDA"),
	PINCTRL_PIN(8, "HV_DDI1_DDC_SCL"),
	PINCTRL_PIN(9, "PANEL1_VDDEN"),
	PINCTRL_PIN(10, "PANEL1_BKLTEN"),
	PINCTRL_PIN(11, "PANEL1_BKLTCTL"),
	PINCTRL_PIN(12, "GP_INTD_DSI_TE1"),
	PINCTRL_PIN(13, "HV_DDI2_DDC_SDA"),
	PINCTRL_PIN(14, "HV_DDI2_DDC_SCL"),
	PINCTRL_PIN(15, "GP_CAMERASB00"),
	PINCTRL_PIN(16, "GP_CAMERASB01"),
	PINCTRL_PIN(17, "GP_CAMERASB02"),
	PINCTRL_PIN(18, "GP_CAMERASB03"),
	PINCTRL_PIN(19, "GP_CAMERASB04"),
	PINCTRL_PIN(20, "GP_CAMERASB05"),
	PINCTRL_PIN(21, "GP_CAMERASB06"),
	PINCTRL_PIN(22, "GP_CAMERASB07"),
	PINCTRL_PIN(23, "GP_CAMERASB08"),
	PINCTRL_PIN(24, "GP_CAMERASB09"),
	PINCTRL_PIN(25, "GP_CAMERASB10"),
	PINCTRL_PIN(26, "GP_CAMERASB11"),
	PINCTRL_PIN(27, "GP_INTD_DSI_TE2"),
};

static const unsigned int byt_ncore_pins_map[BYT_NGPIO_NCORE] = {
	19, 18, 17, 20, 21, 22, 24, 25, 23, 16,
	14, 15, 12, 26, 27, 1, 4, 8, 11, 0,
	3, 6, 10, 13, 2, 5, 9, 7,
};

static const struct intel_community byt_ncore_communities[] = {
	COMMUNITY(0, BYT_NGPIO_NCORE, byt_ncore_pins_map),
};

static const struct intel_pinctrl_soc_data byt_ncore_soc_data = {
	.uid		= BYT_NCORE_ACPI_UID,
	.pins		= byt_ncore_pins,
	.npins		= ARRAY_SIZE(byt_ncore_pins),
	.communities	= byt_ncore_communities,
	.ncommunities	= ARRAY_SIZE(byt_ncore_communities),
};

static const struct intel_pinctrl_soc_data *byt_soc_data[] = {
	&byt_score_soc_data,
	&byt_sus_soc_data,
	&byt_ncore_soc_data,
	NULL
};

static DEFINE_RAW_SPINLOCK(byt_lock);

static void __iomem *byt_gpio_reg(struct intel_pinctrl *vg, unsigned int offset,
				  int reg)
{
	struct intel_community *comm = intel_get_community(vg, offset);
	u32 reg_offset;

	if (!comm)
		return NULL;

	offset -= comm->pin_base;
	switch (reg) {
	case BYT_INT_STAT_REG:
		reg_offset = (offset / 32) * 4;
		break;
	case BYT_DEBOUNCE_REG:
		reg_offset = 0;
		break;
	default:
		reg_offset = comm->pad_map[offset] * 16;
		break;
	}

	return comm->pad_regs + reg_offset + reg;
}

static const struct pinctrl_ops byt_pinctrl_ops = {
	.get_groups_count	= intel_get_groups_count,
	.get_group_name		= intel_get_group_name,
	.get_group_pins		= intel_get_group_pins,
};

static void byt_set_group_simple_mux(struct intel_pinctrl *vg,
				     const struct intel_pingroup group,
				     unsigned int func)
{
	int i;

	guard(raw_spinlock_irqsave)(&byt_lock);

	for (i = 0; i < group.grp.npins; i++) {
		void __iomem *padcfg0;
		u32 value;

		padcfg0 = byt_gpio_reg(vg, group.grp.pins[i], BYT_CONF0_REG);
		if (!padcfg0) {
			dev_warn(vg->dev, "Group %s, pin %i not muxed (can't retrieve CONF0)\n",
				 group.grp.name, i);
			continue;
		}

		value = readl(padcfg0);
		value &= ~BYT_PIN_MUX;
		value |= func;
		writel(value, padcfg0);
	}
}

static void byt_set_group_mixed_mux(struct intel_pinctrl *vg,
				    const struct intel_pingroup group,
				    const unsigned int *func)
{
	int i;

	guard(raw_spinlock_irqsave)(&byt_lock);

	for (i = 0; i < group.grp.npins; i++) {
		void __iomem *padcfg0;
		u32 value;

		padcfg0 = byt_gpio_reg(vg, group.grp.pins[i], BYT_CONF0_REG);
		if (!padcfg0) {
			dev_warn(vg->dev, "Group %s, pin %i not muxed (can't retrieve CONF0)\n",
				 group.grp.name, i);
			continue;
		}

		value = readl(padcfg0);
		value &= ~BYT_PIN_MUX;
		value |= func[i];
		writel(value, padcfg0);
	}
}

static int byt_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
		       unsigned int group_selector)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctldev);
	const struct intel_function func = vg->soc->functions[func_selector];
	const struct intel_pingroup group = vg->soc->groups[group_selector];

	if (group.modes)
		byt_set_group_mixed_mux(vg, group, group.modes);
	else if (!strcmp(func.func.name, "gpio"))
		byt_set_group_simple_mux(vg, group, BYT_DEFAULT_GPIO_MUX);
	else
		byt_set_group_simple_mux(vg, group, group.mode);

	return 0;
}

static u32 byt_get_gpio_mux(struct intel_pinctrl *vg, unsigned int offset)
{
	/* SCORE pin 92-93 */
	if (!strcmp(vg->soc->uid, BYT_SCORE_ACPI_UID) &&
	    offset >= 92 && offset <= 93)
		return BYT_ALTER_GPIO_MUX;

	/* SUS pin 11-21 */
	if (!strcmp(vg->soc->uid, BYT_SUS_ACPI_UID) &&
	    offset >= 11 && offset <= 21)
		return BYT_ALTER_GPIO_MUX;

	return BYT_DEFAULT_GPIO_MUX;
}

static void byt_gpio_clear_triggering(struct intel_pinctrl *vg, unsigned int offset)
{
	void __iomem *reg = byt_gpio_reg(vg, offset, BYT_CONF0_REG);
	u32 value;

	guard(raw_spinlock_irqsave)(&byt_lock);

	value = readl(reg);

	/* Do not clear direct-irq enabled IRQs (from gpio_disable_free) */
	if (!(value & BYT_DIRECT_IRQ_EN))
		value &= ~(BYT_TRIG_POS | BYT_TRIG_NEG | BYT_TRIG_LVL);

	writel(value, reg);
}

static int byt_gpio_request_enable(struct pinctrl_dev *pctl_dev,
				   struct pinctrl_gpio_range *range,
				   unsigned int offset)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctl_dev);
	void __iomem *reg = byt_gpio_reg(vg, offset, BYT_CONF0_REG);
	u32 value, gpio_mux;

	guard(raw_spinlock_irqsave)(&byt_lock);

	/*
	 * In most cases, func pin mux 000 means GPIO function.
	 * But, some pins may have func pin mux 001 represents
	 * GPIO function.
	 *
	 * Because there are devices out there where some pins were not
	 * configured correctly we allow changing the mux value from
	 * request (but print out warning about that).
	 */
	value = readl(reg) & BYT_PIN_MUX;
	gpio_mux = byt_get_gpio_mux(vg, offset);
	if (gpio_mux == value)
		return 0;

	value = readl(reg) & ~BYT_PIN_MUX;
	value |= gpio_mux;
	writel(value, reg);

	dev_warn(vg->dev, FW_BUG "Pin %i: forcibly re-configured as GPIO\n", offset);

	return 0;
}

static void byt_gpio_disable_free(struct pinctrl_dev *pctl_dev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctl_dev);

	byt_gpio_clear_triggering(vg, offset);
}

static void byt_gpio_direct_irq_check(struct intel_pinctrl *vg,
				      unsigned int offset)
{
	void __iomem *conf_reg = byt_gpio_reg(vg, offset, BYT_CONF0_REG);

	/*
	 * Before making any direction modifications, do a check if gpio is set
	 * for direct IRQ. On Bay Trail, setting GPIO to output does not make
	 * sense, so let's at least inform the caller before they shoot
	 * themselves in the foot.
	 */
	if (readl(conf_reg) & BYT_DIRECT_IRQ_EN)
		dev_info_once(vg->dev,
			      "Potential Error: Pin %i: forcibly set GPIO with DIRECT_IRQ_EN to output\n",
			      offset);
}

static int byt_gpio_set_direction(struct pinctrl_dev *pctl_dev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset,
				  bool input)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctl_dev);
	void __iomem *val_reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	u32 value;

	guard(raw_spinlock_irqsave)(&byt_lock);

	value = readl(val_reg);
	value &= ~BYT_DIR_MASK;
	if (input)
		value |= BYT_OUTPUT_EN;
	else
		byt_gpio_direct_irq_check(vg, offset);

	writel(value, val_reg);

	return 0;
}

static const struct pinmux_ops byt_pinmux_ops = {
	.get_functions_count	= intel_get_functions_count,
	.get_function_name	= intel_get_function_name,
	.get_function_groups	= intel_get_function_groups,
	.set_mux		= byt_set_mux,
	.gpio_request_enable	= byt_gpio_request_enable,
	.gpio_disable_free	= byt_gpio_disable_free,
	.gpio_set_direction	= byt_gpio_set_direction,
};

static void byt_get_pull_strength(u32 reg, u16 *strength)
{
	switch (reg & BYT_PULL_STR_MASK) {
	case BYT_PULL_STR_2K:
		*strength = 2000;
		break;
	case BYT_PULL_STR_10K:
		*strength = 10000;
		break;
	case BYT_PULL_STR_20K:
		*strength = 20000;
		break;
	case BYT_PULL_STR_40K:
		*strength = 40000;
		break;
	}
}

static int byt_set_pull_strength(u32 *reg, u16 strength)
{
	*reg &= ~BYT_PULL_STR_MASK;

	switch (strength) {
	case 1: /* Set default strength value in case none is given */
	case 2000:
		*reg |= BYT_PULL_STR_2K;
		break;
	case 10000:
		*reg |= BYT_PULL_STR_10K;
		break;
	case 20000:
		*reg |= BYT_PULL_STR_20K;
		break;
	case 40000:
		*reg |= BYT_PULL_STR_40K;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void byt_gpio_force_input_mode(struct intel_pinctrl *vg, unsigned int offset)
{
	void __iomem *reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	u32 value;

	value = readl(reg);
	if (!(value & BYT_INPUT_EN))
		return;

	/*
	 * Pull assignment is only applicable in input mode. If
	 * chip is not in input mode, set it and warn about it.
	 */
	value &= ~BYT_INPUT_EN;
	writel(value, reg);
	dev_warn(vg->dev, "Pin %i: forcibly set to input mode\n", offset);
}

static int byt_pin_config_get(struct pinctrl_dev *pctl_dev, unsigned int offset,
			      unsigned long *config)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctl_dev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	void __iomem *conf_reg = byt_gpio_reg(vg, offset, BYT_CONF0_REG);
	void __iomem *val_reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	void __iomem *db_reg = byt_gpio_reg(vg, offset, BYT_DEBOUNCE_REG);
	u32 conf, pull, val, debounce;
	u16 arg = 0;

	scoped_guard(raw_spinlock_irqsave, &byt_lock) {
		conf = readl(conf_reg);
		val = readl(val_reg);
	}

	pull = conf & BYT_PULL_ASSIGN_MASK;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		/* Pull assignment is only applicable in input mode */
		if ((val & BYT_INPUT_EN) || pull != BYT_PULL_ASSIGN_DOWN)
			return -EINVAL;

		byt_get_pull_strength(conf, &arg);

		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		/* Pull assignment is only applicable in input mode */
		if ((val & BYT_INPUT_EN) || pull != BYT_PULL_ASSIGN_UP)
			return -EINVAL;

		byt_get_pull_strength(conf, &arg);

		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		if (!(conf & BYT_DEBOUNCE_EN))
			return -EINVAL;

		scoped_guard(raw_spinlock_irqsave, &byt_lock)
			debounce = readl(db_reg);

		switch (debounce & BYT_DEBOUNCE_PULSE_MASK) {
		case BYT_DEBOUNCE_PULSE_375US:
			arg = 375;
			break;
		case BYT_DEBOUNCE_PULSE_750US:
			arg = 750;
			break;
		case BYT_DEBOUNCE_PULSE_1500US:
			arg = 1500;
			break;
		case BYT_DEBOUNCE_PULSE_3MS:
			arg = 3000;
			break;
		case BYT_DEBOUNCE_PULSE_6MS:
			arg = 6000;
			break;
		case BYT_DEBOUNCE_PULSE_12MS:
			arg = 12000;
			break;
		case BYT_DEBOUNCE_PULSE_24MS:
			arg = 24000;
			break;
		default:
			return -EINVAL;
		}

		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int byt_pin_config_set(struct pinctrl_dev *pctl_dev,
			      unsigned int offset,
			      unsigned long *configs,
			      unsigned int num_configs)
{
	struct intel_pinctrl *vg = pinctrl_dev_get_drvdata(pctl_dev);
	void __iomem *conf_reg = byt_gpio_reg(vg, offset, BYT_CONF0_REG);
	void __iomem *db_reg = byt_gpio_reg(vg, offset, BYT_DEBOUNCE_REG);
	u32 conf, db_pulse, debounce;
	enum pin_config_param param;
	int i, ret;
	u32 arg;

	guard(raw_spinlock_irqsave)(&byt_lock);

	conf = readl(conf_reg);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			conf &= ~BYT_PULL_ASSIGN_MASK;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			byt_gpio_force_input_mode(vg, offset);

			conf &= ~BYT_PULL_ASSIGN_MASK;
			conf |= BYT_PULL_ASSIGN_DOWN;
			ret = byt_set_pull_strength(&conf, arg);
			if (ret)
				return ret;

			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			byt_gpio_force_input_mode(vg, offset);

			conf &= ~BYT_PULL_ASSIGN_MASK;
			conf |= BYT_PULL_ASSIGN_UP;
			ret = byt_set_pull_strength(&conf, arg);
			if (ret)
				return ret;

			break;
		case PIN_CONFIG_INPUT_DEBOUNCE:
			switch (arg) {
			case 0:
				db_pulse = 0;
				break;
			case 375:
				db_pulse = BYT_DEBOUNCE_PULSE_375US;
				break;
			case 750:
				db_pulse = BYT_DEBOUNCE_PULSE_750US;
				break;
			case 1500:
				db_pulse = BYT_DEBOUNCE_PULSE_1500US;
				break;
			case 3000:
				db_pulse = BYT_DEBOUNCE_PULSE_3MS;
				break;
			case 6000:
				db_pulse = BYT_DEBOUNCE_PULSE_6MS;
				break;
			case 12000:
				db_pulse = BYT_DEBOUNCE_PULSE_12MS;
				break;
			case 24000:
				db_pulse = BYT_DEBOUNCE_PULSE_24MS;
				break;
			default:
				return -EINVAL;
			}

			if (db_pulse) {
				debounce = readl(db_reg);
				debounce = (debounce & ~BYT_DEBOUNCE_PULSE_MASK) | db_pulse;
				writel(debounce, db_reg);

				conf |= BYT_DEBOUNCE_EN;
			} else {
				conf &= ~BYT_DEBOUNCE_EN;
			}

			break;
		default:
			return -ENOTSUPP;
		}
	}

	writel(conf, conf_reg);

	return 0;
}

static const struct pinconf_ops byt_pinconf_ops = {
	.is_generic	= true,
	.pin_config_get	= byt_pin_config_get,
	.pin_config_set	= byt_pin_config_set,
};

static const struct pinctrl_desc byt_pinctrl_desc = {
	.pctlops	= &byt_pinctrl_ops,
	.pmxops		= &byt_pinmux_ops,
	.confops	= &byt_pinconf_ops,
	.owner		= THIS_MODULE,
};

static int byt_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	u32 val;

	scoped_guard(raw_spinlock_irqsave, &byt_lock)
		val = readl(reg);

	return !!(val & BYT_LEVEL);
}

static void byt_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 old_val;

	reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	if (!reg)
		return;

	guard(raw_spinlock_irqsave)(&byt_lock);

	old_val = readl(reg);
	if (value)
		writel(old_val | BYT_LEVEL, reg);
	else
		writel(old_val & ~BYT_LEVEL, reg);
}

static int byt_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 value;

	reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	if (!reg)
		return -EINVAL;

	scoped_guard(raw_spinlock_irqsave, &byt_lock)
		value = readl(reg);

	if (!(value & BYT_OUTPUT_EN))
		return GPIO_LINE_DIRECTION_OUT;
	if (!(value & BYT_INPUT_EN))
		return GPIO_LINE_DIRECTION_IN;

	return -EINVAL;
}

static int byt_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *val_reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	u32 reg;

	guard(raw_spinlock_irqsave)(&byt_lock);

	reg = readl(val_reg);
	reg &= ~BYT_DIR_MASK;
	reg |= BYT_OUTPUT_EN;
	writel(reg, val_reg);

	return 0;
}

/*
 * Note despite the temptation this MUST NOT be converted into a call to
 * pinctrl_gpio_direction_output() + byt_gpio_set() that does not work this
 * MUST be done as a single BYT_VAL_REG register write.
 * See the commit message of the commit adding this comment for details.
 */
static int byt_gpio_direction_output(struct gpio_chip *chip,
				     unsigned int offset, int value)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *val_reg = byt_gpio_reg(vg, offset, BYT_VAL_REG);
	u32 reg;

	guard(raw_spinlock_irqsave)(&byt_lock);

	byt_gpio_direct_irq_check(vg, offset);

	reg = readl(val_reg);
	reg &= ~BYT_DIR_MASK;
	if (value)
		reg |= BYT_LEVEL;
	else
		reg &= ~BYT_LEVEL;

	writel(reg, val_reg);

	return 0;
}

static void byt_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	int i;
	u32 conf0, val;

	for (i = 0; i < vg->soc->npins; i++) {
		const struct intel_community *comm;
		void __iomem *conf_reg, *val_reg;
		const char *pull_str = NULL;
		const char *pull = NULL;
		unsigned int pin;

		pin = vg->soc->pins[i].number;

		conf_reg = byt_gpio_reg(vg, pin, BYT_CONF0_REG);
		if (!conf_reg) {
			seq_printf(s, "Pin %i: can't retrieve CONF0\n", pin);
			continue;
		}

		val_reg = byt_gpio_reg(vg, pin, BYT_VAL_REG);
		if (!val_reg) {
			seq_printf(s, "Pin %i: can't retrieve VAL\n", pin);
			continue;
		}

		scoped_guard(raw_spinlock_irqsave, &byt_lock) {
			conf0 = readl(conf_reg);
			val = readl(val_reg);
		}

		comm = intel_get_community(vg, pin);
		if (!comm) {
			seq_printf(s, "Pin %i: can't retrieve community\n", pin);
			continue;
		}

		char *label __free(kfree) = gpiochip_dup_line_label(chip, i);
		if (IS_ERR(label))
			continue;

		switch (conf0 & BYT_PULL_ASSIGN_MASK) {
		case BYT_PULL_ASSIGN_UP:
			pull = "up";
			break;
		case BYT_PULL_ASSIGN_DOWN:
			pull = "down";
			break;
		}

		switch (conf0 & BYT_PULL_STR_MASK) {
		case BYT_PULL_STR_2K:
			pull_str = "2k";
			break;
		case BYT_PULL_STR_10K:
			pull_str = "10k";
			break;
		case BYT_PULL_STR_20K:
			pull_str = "20k";
			break;
		case BYT_PULL_STR_40K:
			pull_str = "40k";
			break;
		}

		seq_printf(s,
			   " gpio-%-3d (%-20.20s) %s %s %s pad-%-3d offset:0x%03x mux:%d %s%s%s",
			   pin,
			   label ?: "Unrequested",
			   val & BYT_INPUT_EN ? "  " : "in",
			   val & BYT_OUTPUT_EN ? "   " : "out",
			   str_hi_lo(val & BYT_LEVEL),
			   comm->pad_map[i], comm->pad_map[i] * 16,
			   conf0 & 0x7,
			   conf0 & BYT_TRIG_NEG ? " fall" : "     ",
			   conf0 & BYT_TRIG_POS ? " rise" : "     ",
			   conf0 & BYT_TRIG_LVL ? " level" : "      ");

		if (pull && pull_str)
			seq_printf(s, " %-4s %-3s", pull, pull_str);
		else
			seq_puts(s, "          ");

		if (conf0 & BYT_IODEN)
			seq_puts(s, " open-drain");

		seq_puts(s, "\n");
	}
}

static const struct gpio_chip byt_gpio_chip = {
	.owner			= THIS_MODULE,
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.get_direction		= byt_gpio_get_direction,
	.direction_input	= byt_gpio_direction_input,
	.direction_output	= byt_gpio_direction_output,
	.get			= byt_gpio_get,
	.set			= byt_gpio_set,
	.set_config		= gpiochip_generic_config,
	.dbg_show		= byt_gpio_dbg_show,
};

static void byt_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *vg = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	void __iomem *reg;

	reg = byt_gpio_reg(vg, hwirq, BYT_INT_STAT_REG);
	if (!reg)
		return;

	guard(raw_spinlock)(&byt_lock);

	writel(BIT(hwirq % 32), reg);
}

static void byt_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *vg = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	byt_gpio_clear_triggering(vg, hwirq);
	gpiochip_disable_irq(gc, hwirq);
}

static void byt_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_pinctrl *vg = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	void __iomem *reg;
	u32 value;

	gpiochip_enable_irq(gc, hwirq);

	reg = byt_gpio_reg(vg, hwirq, BYT_CONF0_REG);
	if (!reg)
		return;

	guard(raw_spinlock_irqsave)(&byt_lock);

	value = readl(reg);

	switch (irqd_get_trigger_type(d)) {
	case IRQ_TYPE_LEVEL_HIGH:
		value |= BYT_TRIG_LVL;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		value |= BYT_TRIG_POS;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		value |= BYT_TRIG_LVL;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
		value |= BYT_TRIG_NEG;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		value |= (BYT_TRIG_NEG | BYT_TRIG_POS);
		break;
	}

	writel(value, reg);
}

static int byt_irq_type(struct irq_data *d, unsigned int type)
{
	struct intel_pinctrl *vg = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	void __iomem *reg;
	u32 value;

	reg = byt_gpio_reg(vg, hwirq, BYT_CONF0_REG);
	if (!reg)
		return -EINVAL;

	guard(raw_spinlock_irqsave)(&byt_lock);

	value = readl(reg);

	WARN(value & BYT_DIRECT_IRQ_EN,
	     "Bad pad config for IO mode, force DIRECT_IRQ_EN bit clearing");

	/* For level trigges the BYT_TRIG_POS and BYT_TRIG_NEG bits
	 * are used to indicate high and low level triggering
	 */
	value &= ~(BYT_DIRECT_IRQ_EN | BYT_TRIG_POS | BYT_TRIG_NEG |
		   BYT_TRIG_LVL);
	/* Enable glitch filtering */
	value |= BYT_GLITCH_FILTER_EN | BYT_GLITCH_F_SLOW_CLK |
		 BYT_GLITCH_F_FAST_CLK;

	writel(value, reg);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static const struct irq_chip byt_gpio_irq_chip = {
	.name		= "BYT-GPIO",
	.irq_ack	= byt_irq_ack,
	.irq_mask	= byt_irq_mask,
	.irq_unmask	= byt_irq_unmask,
	.irq_set_type	= byt_irq_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_SET_TYPE_MASKED | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void byt_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct intel_pinctrl *vg = gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	u32 base, pin;
	void __iomem *reg;
	unsigned long pending;

	/* check from GPIO controller which pin triggered the interrupt */
	for (base = 0; base < vg->chip.ngpio; base += 32) {
		reg = byt_gpio_reg(vg, base, BYT_INT_STAT_REG);

		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve INT_STAT%u\n", base / 32, base);
			continue;
		}

		scoped_guard(raw_spinlock, &byt_lock)
			pending = readl(reg);
		for_each_set_bit(pin, &pending, 32)
			generic_handle_domain_irq(vg->chip.irq.domain, base + pin);
	}
	chip->irq_eoi(data);
}

static bool byt_direct_irq_sanity_check(struct intel_pinctrl *vg, int pin, u32 conf0)
{
	int direct_irq, ioapic_direct_irq_base;
	u8 *match, direct_irq_mux[16];
	u32 trig;

	memcpy_fromio(direct_irq_mux, vg->communities->pad_regs + BYT_DIRECT_IRQ_REG,
		      sizeof(direct_irq_mux));
	match = memchr(direct_irq_mux, pin, sizeof(direct_irq_mux));
	if (!match) {
		dev_warn(vg->dev, FW_BUG "Pin %i: DIRECT_IRQ_EN set but no IRQ assigned, clearing\n", pin);
		return false;
	}

	direct_irq = match - direct_irq_mux;
	/* Base IO-APIC pin numbers come from atom-e3800-family-datasheet.pdf */
	ioapic_direct_irq_base = (vg->communities->npins == BYT_NGPIO_SCORE) ? 51 : 67;
	dev_dbg(vg->dev, "Pin %i: uses direct IRQ %d (IO-APIC %d)\n", pin,
		direct_irq, direct_irq + ioapic_direct_irq_base);

	/*
	 * Testing has shown that the way direct IRQs work is that the combination of the
	 * direct-irq-en flag and the direct IRQ mux connect the output of the GPIO's IRQ
	 * trigger block, which normally sets the status flag in the IRQ status reg at
	 * 0x800, to one of the IO-APIC pins according to the mux registers.
	 *
	 * This means that:
	 * 1. The TRIG_MASK bits must be set to configure the GPIO's IRQ trigger block
	 * 2. The TRIG_LVL bit *must* be set, so that the GPIO's input value is directly
	 *    passed (1:1 or inverted) to the IO-APIC pin, if TRIG_LVL is not set,
	 *    selecting edge mode operation then on the first edge the IO-APIC pin goes
	 *    high, but since no write-to-clear write will be done to the IRQ status reg
	 *    at 0x800, the detected edge condition will never get cleared.
	 */
	trig = conf0 & BYT_TRIG_MASK;
	if (trig != (BYT_TRIG_POS | BYT_TRIG_LVL) &&
	    trig != (BYT_TRIG_NEG | BYT_TRIG_LVL)) {
		dev_warn(vg->dev,
			 FW_BUG "Pin %i: DIRECT_IRQ_EN set without trigger (CONF0: %#08x), clearing\n",
			 pin, conf0);
		return false;
	}

	return true;
}

static void byt_init_irq_valid_mask(struct gpio_chip *chip,
				    unsigned long *valid_mask,
				    unsigned int ngpios)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 value;
	int i;

	/*
	 * Clear interrupt triggers for all pins that are GPIOs and
	 * do not use direct IRQ mode. This will prevent spurious
	 * interrupts from misconfigured pins.
	 */
	for (i = 0; i < vg->soc->npins; i++) {
		unsigned int pin = vg->soc->pins[i].number;

		reg = byt_gpio_reg(vg, pin, BYT_CONF0_REG);
		if (!reg) {
			dev_warn(vg->dev, "Pin %i: could not retrieve CONF0\n", i);
			continue;
		}

		value = readl(reg);
		if (value & BYT_DIRECT_IRQ_EN) {
			if (byt_direct_irq_sanity_check(vg, i, value)) {
				clear_bit(i, valid_mask);
			} else {
				value &= ~(BYT_DIRECT_IRQ_EN | BYT_TRIG_POS |
					   BYT_TRIG_NEG | BYT_TRIG_LVL);
				writel(value, reg);
			}
		} else if ((value & BYT_PIN_MUX) == byt_get_gpio_mux(vg, i)) {
			byt_gpio_clear_triggering(vg, i);
			dev_dbg(vg->dev, "disabling GPIO %d\n", i);
		}
	}
}

static int byt_gpio_irq_init_hw(struct gpio_chip *chip)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	void __iomem *reg;
	u32 base, value;

	/* clear interrupt status trigger registers */
	for (base = 0; base < vg->soc->npins; base += 32) {
		reg = byt_gpio_reg(vg, base, BYT_INT_STAT_REG);

		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve INT_STAT%u\n", base / 32, base);
			continue;
		}

		writel(0xffffffff, reg);
		/* make sure trigger bits are cleared, if not then a pin
		   might be misconfigured in bios */
		value = readl(reg);
		if (value)
			dev_err(vg->dev,
				"GPIO interrupt error, pins misconfigured. INT_STAT%u: %#08x\n",
				base / 32, value);
	}

	return 0;
}

static int byt_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct intel_pinctrl *vg = gpiochip_get_data(chip);
	struct device *dev = vg->dev;
	int ret;

	ret = gpiochip_add_pin_range(chip, dev_name(dev), 0, 0, vg->soc->npins);
	if (ret)
		dev_err(dev, "failed to add GPIO pin range\n");

	return ret;
}

static int byt_gpio_probe(struct intel_pinctrl *vg)
{
	struct platform_device *pdev = to_platform_device(vg->dev);
	struct gpio_chip *gc;
	int irq, ret;

	/* Set up gpio chip */
	vg->chip	= byt_gpio_chip;
	gc		= &vg->chip;
	gc->label	= dev_name(vg->dev);
	gc->base	= -1;
	gc->can_sleep	= false;
	gc->add_pin_ranges = byt_gpio_add_pin_ranges;
	gc->parent	= vg->dev;
	gc->ngpio	= vg->soc->npins;

#ifdef CONFIG_PM_SLEEP
	vg->context.pads = devm_kcalloc(vg->dev, gc->ngpio, sizeof(*vg->context.pads),
					GFP_KERNEL);
	if (!vg->context.pads)
		return -ENOMEM;
#endif

	/* set up interrupts  */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		struct gpio_irq_chip *girq;

		girq = &gc->irq;
		gpio_irq_chip_set_chip(girq, &byt_gpio_irq_chip);
		girq->init_hw = byt_gpio_irq_init_hw;
		girq->init_valid_mask = byt_init_irq_valid_mask;
		girq->parent_handler = byt_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(vg->dev, girq->num_parents,
					     sizeof(*girq->parents), GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
	}

	ret = devm_gpiochip_add_data(vg->dev, gc, vg);
	if (ret) {
		dev_err(vg->dev, "failed adding byt-gpio chip\n");
		return ret;
	}

	return ret;
}

static int byt_set_soc_data(struct intel_pinctrl *vg,
			    const struct intel_pinctrl_soc_data *soc)
{
	struct platform_device *pdev = to_platform_device(vg->dev);
	int i;

	vg->soc = soc;

	vg->ncommunities = vg->soc->ncommunities;
	vg->communities = devm_kcalloc(vg->dev, vg->ncommunities,
				       sizeof(*vg->communities), GFP_KERNEL);
	if (!vg->communities)
		return -ENOMEM;

	for (i = 0; i < vg->soc->ncommunities; i++) {
		struct intel_community *comm = vg->communities + i;

		*comm = vg->soc->communities[i];

		comm->pad_regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(comm->pad_regs))
			return PTR_ERR(comm->pad_regs);
	}

	return 0;
}

static const struct acpi_device_id byt_gpio_acpi_match[] = {
	{ "INT33B2", (kernel_ulong_t)byt_soc_data },
	{ "INT33FC", (kernel_ulong_t)byt_soc_data },
	{ }
};

static int byt_pinctrl_probe(struct platform_device *pdev)
{
	const struct intel_pinctrl_soc_data *soc_data;
	struct device *dev = &pdev->dev;
	struct intel_pinctrl *vg;
	int ret;

	soc_data = intel_pinctrl_get_soc_data(pdev);
	if (IS_ERR(soc_data))
		return PTR_ERR(soc_data);

	vg = devm_kzalloc(dev, sizeof(*vg), GFP_KERNEL);
	if (!vg)
		return -ENOMEM;

	vg->dev = dev;
	ret = byt_set_soc_data(vg, soc_data);
	if (ret) {
		dev_err(dev, "failed to set soc data\n");
		return ret;
	}

	vg->pctldesc		= byt_pinctrl_desc;
	vg->pctldesc.name	= dev_name(dev);
	vg->pctldesc.pins	= vg->soc->pins;
	vg->pctldesc.npins	= vg->soc->npins;

	vg->pctldev = devm_pinctrl_register(dev, &vg->pctldesc, vg);
	if (IS_ERR(vg->pctldev)) {
		dev_err(dev, "failed to register pinctrl driver\n");
		return PTR_ERR(vg->pctldev);
	}

	ret = byt_gpio_probe(vg);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, vg);

	return 0;
}

static int byt_gpio_suspend(struct device *dev)
{
	struct intel_pinctrl *vg = dev_get_drvdata(dev);
	int i;

	guard(raw_spinlock_irqsave)(&byt_lock);

	for (i = 0; i < vg->soc->npins; i++) {
		void __iomem *reg;
		u32 value;
		unsigned int pin = vg->soc->pins[i].number;

		reg = byt_gpio_reg(vg, pin, BYT_CONF0_REG);
		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve CONF0\n", i);
			continue;
		}
		value = readl(reg) & BYT_CONF0_RESTORE_MASK;
		vg->context.pads[i].conf0 = value;

		reg = byt_gpio_reg(vg, pin, BYT_VAL_REG);
		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve VAL\n", i);
			continue;
		}
		value = readl(reg) & BYT_VAL_RESTORE_MASK;
		vg->context.pads[i].val = value;
	}

	return 0;
}

static int byt_gpio_resume(struct device *dev)
{
	struct intel_pinctrl *vg = dev_get_drvdata(dev);
	int i;

	guard(raw_spinlock_irqsave)(&byt_lock);

	for (i = 0; i < vg->soc->npins; i++) {
		void __iomem *reg;
		u32 value;
		unsigned int pin = vg->soc->pins[i].number;

		reg = byt_gpio_reg(vg, pin, BYT_CONF0_REG);
		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve CONF0\n", i);
			continue;
		}
		value = readl(reg);
		if ((value & BYT_CONF0_RESTORE_MASK) !=
		     vg->context.pads[i].conf0) {
			value &= ~BYT_CONF0_RESTORE_MASK;
			value |= vg->context.pads[i].conf0;
			writel(value, reg);
			dev_info(dev, "restored pin %d CONF0 %#08x", i, value);
		}

		reg = byt_gpio_reg(vg, pin, BYT_VAL_REG);
		if (!reg) {
			dev_warn(vg->dev, "Pin %i: can't retrieve VAL\n", i);
			continue;
		}
		value = readl(reg);
		if ((value & BYT_VAL_RESTORE_MASK) !=
		     vg->context.pads[i].val) {
			u32 v;

			v = value & ~BYT_VAL_RESTORE_MASK;
			v |= vg->context.pads[i].val;
			if (v != value) {
				writel(v, reg);
				dev_dbg(dev, "restored pin %d VAL %#08x\n", i, v);
			}
		}
	}

	return 0;
}

static const struct dev_pm_ops byt_gpio_pm_ops = {
	LATE_SYSTEM_SLEEP_PM_OPS(byt_gpio_suspend, byt_gpio_resume)
};

static struct platform_driver byt_gpio_driver = {
	.probe          = byt_pinctrl_probe,
	.driver         = {
		.name			= "byt_gpio",
		.pm			= pm_sleep_ptr(&byt_gpio_pm_ops),
		.acpi_match_table	= byt_gpio_acpi_match,
		.suppress_bind_attrs	= true,
	},
};

static int __init byt_gpio_init(void)
{
	return platform_driver_register(&byt_gpio_driver);
}
subsys_initcall(byt_gpio_init);

MODULE_IMPORT_NS(PINCTRL_INTEL);
