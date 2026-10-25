// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 *
 * EcoNet EN7528 SoC pinctrl driver.
 *
 * Sources: EN7528 External Programming Guide V1.3 chapter 1.2.2/1.3.2
 * (CHIP SCU, base 0x1fa20000), chapter 12 (GPIO controller, base
 * 0x1fbf0200) and EN7528DU/EN7528HU datasheets chapter 4.3 (pin sharing
 * schemes; both packages use the same GPIO mapping).
 *
 * Unlike the later SoCs, all IOMUX mode bits live in a single register
 * (RG_PON_I2C_MODE at 0x15c) plus the spill-over bits in IOMUX Control 3
 * (RG_FORCE_GPIO22_EN at 0x224). EJTAG has its own enable register at
 * 0x1ac.
 *
 * Pin numbering follows the GPIO line numbering of the SoC:
 *   pins  0-11 -> GPIO0-GPIO11 (dedicated GPIO pads)
 *   pins 12-15 -> ZSYNC1/ZCLK1/ZMOSI1/ZMISO1 pads (GPIO12-GPIO15)
 *   pins 16-21 -> PON I/O pads (GPIO16-GPIO21, GPIO21 = PON_RX_DIS)
 *   pins 22-39 -> GPIO22-GPIO39 (dedicated GPIO pads)
 *   pins 40/41 -> PCIE_RESET0/PCIE_RESET1 pads (GPIO40/GPIO41)
 * Pins 42-49 are the standalone pads (I2C, UART, SPI). They have no
 * GPIO function and are only referenced by the pinconf tables.
 *
 * Note: the RG_PON_I2C_MODE register description in the programming
 * guide carries stale "GPIO30/GPIO31" text for the PCIE_RESET pads; the
 * EN7528 datasheets (table 4-10) map them to GPIO40/GPIO41.
 *
 * The PCM SPI chip select register bit names are offset by two against
 * the datasheet signal names: rg_gpio_pcm_spi_cs3_mode drives
 * PCM_SPI_CS1 (spelled out in datasheet table 4-6).
 */
#include "airoha-common.h"

/* MUX */
#define REG_PON_I2C_MODE			0x015c
#define SIPO_RCLK_MODE_MASK		BIT(30)
/*
 * A set PCIE_RESET{0,1}_GPIO bit routes the pad to its GPIO function
 * (GPIO40/GPIO41), a cleared bit selects the PCIe reset function.
 */
#define GPIO_PCIE_RESET1_MASK		BIT(29)	/* GPIO41 */
#define GPIO_PCIE_RESET0_MASK		BIT(28)	/* GPIO40 */
#define GPIO_SPI_QUAD_MODE_MASK		BIT(27)
#define GPIO_UART3_MODE_MASK		BIT(26)
#define GPIO_UART2_CTS_RTS_MODE_MASK	BIT(25)
#define GPIO_UART2_MODE_MASK		BIT(24)
#define GPIO_SIPO_MODE_MASK		BIT(23)
#define GPIO_PON_MODE_MASK		BIT(21)
#define GPIO_PCM2_MODE_MASK		BIT(20)
#define GPIO_PCM1_MODE_MASK		BIT(19)
#define GPIO_PCM_SPI_MODE_MASK		BIT(18)	/* RG_GPIO_SPI2_MODE */
#define GPIO_PCM_INT_MODE_MASK		BIT(17)
#define GPIO_PCM_RESET_MODE_MASK		BIT(16)
#define GPIO_SPI_CS1_MODE_MASK		BIT(15)
#define GPIO_PCM_SPI_CS1_MODE_MASK	BIT(14)	/* RG ..._CS3 */
#define GPIO_LAN3_LED1_MODE_MASK		BIT(11)
#define GPIO_LAN3_LED0_MODE_MASK		BIT(10)
#define GPIO_LAN2_LED1_MODE_MASK		BIT(9)
#define GPIO_LAN2_LED0_MODE_MASK		BIT(8)
#define GPIO_LAN1_LED1_MODE_MASK		BIT(7)
#define GPIO_LAN1_LED0_MODE_MASK		BIT(6)
#define GPIO_LAN0_LED1_MODE_MASK		BIT(5)
#define GPIO_LAN0_LED0_MODE_MASK		BIT(4)
#define PON_TOD_1PPS_MODE_MASK		BIT(2)
#define GSW_TOD_1PPS_MODE_MASK		BIT(1)

#define REG_FORCE_GPIO22_EN		0x0224
#define GPIO_PCM_SPI_CS7_MODE_MASK	BIT(15)	/* RG ..._CS9 */
#define GPIO_PCM_SPI_CS6_MODE_MASK	BIT(14)	/* RG ..._CS8 */
#define GPIO_PCM_SPI_CS5_MODE_MASK	BIT(13)	/* RG ..._CS7 */
#define GPIO_PCM_SPI_CS4_MODE_MASK	BIT(12)	/* RG ..._CS6 */
#define GPIO_PCM_SPI_CS3_MODE_MASK	BIT(11)	/* RG ..._CS5 */
#define GPIO_PCM_SPI_CS2_MODE_MASK	BIT(10)	/* RG ..._CS4 */
#define GPIO_PNAND_MODE_MASK		BIT(9)	/* RG_GPIO_NFD_MODE */

#define REG_CPU_EJTAG_EN			0x01ac
#define CPU_EJTAG_EN_MASK		BIT(0)

/* LED MAP */
#define REG_LAN_LED0_MAPPING		0x01b0
#define REG_LAN_LED1_MAPPING		0x01b4

/* shared */
#define LAN3_LED_MAPPING_MASK			GENMASK(14, 12)
#define LAN3_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN3_LED_MAPPING_MASK, (_n))

#define LAN2_LED_MAPPING_MASK			GENMASK(10, 8)
#define LAN2_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN2_LED_MAPPING_MASK, (_n))

#define LAN1_LED_MAPPING_MASK			GENMASK(6, 4)
#define LAN1_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN1_LED_MAPPING_MASK, (_n))

#define LAN0_LED_MAPPING_MASK			GENMASK(2, 0)
#define LAN0_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN0_LED_MAPPING_MASK, (_n))

/*
 * CONF.
 * The EN7528 documentation names the two drive strength stages E4/E8;
 * they fill the DRIVE_E2/DRIVE_E4 pinconf slots. All conf register
 * offsets differ from the later SoCs.
 */
#define REG_I2C_SDA_E2			0x0014	/* RG_I2C_SDA_E4 */
#define REG_I2C_SDA_E4			0x0018	/* RG_I2C_SDA_E8 */
#define REG_GPIO_L_E2			0x001c	/* RG_GPIO_L_E4 */
#define REG_GPIO_H_E2			0x0020	/* RG_GPIO_H_E4 */
#define REG_GPIO_L_E4			0x0024	/* RG_GPIO_L_E8 */
#define REG_GPIO_H_E4			0x0028	/* RG_GPIO_H_E8 */

#define REG_I2C_SDA_PU			0x003c
#define REG_I2C_SDA_PD			0x0040
#define REG_GPIO_L_PU			0x0044
#define REG_GPIO_H_PU			0x0048
#define REG_GPIO_L_PD			0x004c
#define REG_GPIO_H_PD			0x0050

/* standalone IO conf bit layout, common to E4/E8/PU/PD registers */
#define SPI_MISO_CONF_MASK		BIT(13)
#define SPI_MOSI_CONF_MASK		BIT(12)
#define SPI_CLK_CONF_MASK		BIT(11)
#define SPI_CS0_CONF_MASK		BIT(10)
#define PCIE1_RESET_CONF_MASK		BIT(9)
#define PCIE0_RESET_CONF_MASK		BIT(8)
#define UART_RXD_CONF_MASK		BIT(3)
#define UART_TXD_CONF_MASK		BIT(2)
#define I2C_SCL_CONF_MASK		BIT(1)
#define I2C_SDA_CONF_MASK		BIT(0)

/*
 * PWM MODE CONF - the flash mode registers match EN7580: EXT bit 16
 * maps to GPIO32 and EXT bits 17+ to GPIO37+. GPIO33-GPIO36 have no
 * flash mode configuration bit.
 */
#define REG_GPIO_FLASH_MODE_CFG			0x0034
#define GPIO15_FLASH_MODE_CFG			BIT(15)
#define GPIO14_FLASH_MODE_CFG			BIT(14)
#define GPIO13_FLASH_MODE_CFG			BIT(13)
#define GPIO12_FLASH_MODE_CFG			BIT(12)
#define GPIO11_FLASH_MODE_CFG			BIT(11)
#define GPIO10_FLASH_MODE_CFG			BIT(10)
#define GPIO9_FLASH_MODE_CFG			BIT(9)
#define GPIO8_FLASH_MODE_CFG			BIT(8)
#define GPIO7_FLASH_MODE_CFG			BIT(7)
#define GPIO6_FLASH_MODE_CFG			BIT(6)
#define GPIO5_FLASH_MODE_CFG			BIT(5)
#define GPIO4_FLASH_MODE_CFG			BIT(4)
#define GPIO3_FLASH_MODE_CFG			BIT(3)
#define GPIO2_FLASH_MODE_CFG			BIT(2)
#define GPIO1_FLASH_MODE_CFG			BIT(1)
#define GPIO0_FLASH_MODE_CFG			BIT(0)

#define REG_GPIO_FLASH_MODE_CFG_EXT		0x0068
#define GPIO41_FLASH_MODE_CFG			BIT(21)
#define GPIO40_FLASH_MODE_CFG			BIT(20)
#define GPIO39_FLASH_MODE_CFG			BIT(19)
#define GPIO38_FLASH_MODE_CFG			BIT(18)
#define GPIO37_FLASH_MODE_CFG			BIT(17)
#define GPIO32_FLASH_MODE_CFG			BIT(16)	/* GPIO36 on EN7581 */
#define GPIO31_FLASH_MODE_CFG			BIT(15)
#define GPIO30_FLASH_MODE_CFG			BIT(14)
#define GPIO29_FLASH_MODE_CFG			BIT(13)
#define GPIO28_FLASH_MODE_CFG			BIT(12)
#define GPIO27_FLASH_MODE_CFG			BIT(11)
#define GPIO26_FLASH_MODE_CFG			BIT(10)
#define GPIO25_FLASH_MODE_CFG			BIT(9)
#define GPIO24_FLASH_MODE_CFG			BIT(8)
#define GPIO23_FLASH_MODE_CFG			BIT(7)
#define GPIO22_FLASH_MODE_CFG			BIT(6)
#define GPIO21_FLASH_MODE_CFG			BIT(5)
#define GPIO20_FLASH_MODE_CFG			BIT(4)
#define GPIO19_FLASH_MODE_CFG			BIT(3)
#define GPIO18_FLASH_MODE_CFG			BIT(2)
#define GPIO17_FLASH_MODE_CFG			BIT(1)
#define GPIO16_FLASH_MODE_CFG			BIT(0)

static const struct pinctrl_pin_desc pinctrl_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),	/* PCM_INT */
	PINCTRL_PIN(2, "gpio2"),	/* PCM_RESET */
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),	/* ZSYNC1 */
	PINCTRL_PIN(13, "gpio13"),	/* ZCLK1 */
	PINCTRL_PIN(14, "gpio14"),	/* ZMOSI1 */
	PINCTRL_PIN(15, "gpio15"),	/* ZMISO1 */
	PINCTRL_PIN(16, "gpio16"),	/* PON_TX_DIS */
	PINCTRL_PIN(17, "gpio17"),	/* PON_TX_FAULT */
	PINCTRL_PIN(18, "gpio18"),	/* PON_TX_SD */
	PINCTRL_PIN(19, "gpio19"),	/* PON_RX_SD */
	PINCTRL_PIN(20, "gpio20"),	/* PON_BURST_EN */
	PINCTRL_PIN(21, "gpio21"),	/* PON_RX_DIS */
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
	PINCTRL_PIN(32, "gpio32"),
	PINCTRL_PIN(33, "gpio33"),
	PINCTRL_PIN(34, "gpio34"),
	PINCTRL_PIN(35, "gpio35"),
	PINCTRL_PIN(36, "gpio36"),
	PINCTRL_PIN(37, "gpio37"),
	PINCTRL_PIN(38, "gpio38"),
	PINCTRL_PIN(39, "gpio39"),
	PINCTRL_PIN(40, "pcie_reset0"),	/* GPIO40 */
	PINCTRL_PIN(41, "pcie_reset1"),	/* GPIO41 */
	/* standalone pads, pinconf only */
	PINCTRL_PIN(42, "i2c_sda"),
	PINCTRL_PIN(43, "i2c_scl"),
	PINCTRL_PIN(44, "uart_txd"),
	PINCTRL_PIN(45, "uart_rxd"),
	PINCTRL_PIN(46, "spi_cs0"),
	PINCTRL_PIN(47, "spi_clk"),
	PINCTRL_PIN(48, "spi_mosi"),
	PINCTRL_PIN(49, "spi_miso"),
};

static const int pon_pins[] = { 17, 18, 19, 20 };
static const int sipo_pins[] = { 8, 11 };
static const int sipo_rclk_pins[] = { 8, 9, 11 };
static const int uart2_pins[] = { 28, 29 };
static const int uart2_cts_rts_pins[] = { 12, 13 };
static const int uart3_pins[] = { 38, 39 };
static const int ejtag_pins[] = { 31, 33, 35, 37, 39 };
static const int pcm1_pins[] = { 12, 13, 14, 15 };
static const int pcm2_pins[] = { 24, 25, 26, 27 };
static const int spi_quad_pins[] = { 10, 23 };
static const int spi_cs1_pins[] = { 9 };
static const int pcm_spi_pins[] = { 4, 5, 6, 7 };
static const int pcm_spi_int_pins[] = { 1 };
static const int pcm_spi_rst_pins[] = { 2 };
static const int pcm_spi_cs1_pins[] = { 3 };
static const int pcm_spi_cs2_pins[] = { 10 };
static const int pcm_spi_cs3_pins[] = { 23 };
static const int pcm_spi_cs4_pins[] = { 21 };
static const int pcm_spi_cs5_pins[] = { 9 };
static const int pcm_spi_cs6_pins[] = { 28 };
static const int pcm_spi_cs7_pins[] = { 29 };
static const int pnand_pins[] = { 3, 8, 10, 11, 21, 22, 23, 24, 25, 26, 27,
				  28, 29, 46, 47, 48, 49 };
static const int gpio0_pins[] = { 0 };
static const int gpio1_pins[] = { 1 };
static const int gpio2_pins[] = { 2 };
static const int gpio3_pins[] = { 3 };
static const int gpio4_pins[] = { 4 };
static const int gpio5_pins[] = { 5 };
static const int gpio6_pins[] = { 6 };
static const int gpio7_pins[] = { 7 };
static const int gpio8_pins[] = { 8 };
static const int gpio9_pins[] = { 9 };
static const int gpio10_pins[] = { 10 };
static const int gpio11_pins[] = { 11 };
static const int gpio12_pins[] = { 12 };
static const int gpio13_pins[] = { 13 };
static const int gpio14_pins[] = { 14 };
static const int gpio15_pins[] = { 15 };
static const int gpio16_pins[] = { 16 };
static const int gpio17_pins[] = { 17 };
static const int gpio18_pins[] = { 18 };
static const int gpio19_pins[] = { 19 };
static const int gpio20_pins[] = { 20 };
static const int gpio21_pins[] = { 21 };
static const int gpio22_pins[] = { 22 };
static const int gpio23_pins[] = { 23 };
static const int gpio24_pins[] = { 24 };
static const int gpio25_pins[] = { 25 };
static const int gpio26_pins[] = { 26 };
static const int gpio27_pins[] = { 27 };
static const int gpio28_pins[] = { 28 };
static const int gpio29_pins[] = { 29 };
static const int gpio30_pins[] = { 30 };
static const int gpio31_pins[] = { 31 };
static const int gpio32_pins[] = { 32 };
static const int gpio33_pins[] = { 33 };
static const int gpio34_pins[] = { 34 };
static const int gpio35_pins[] = { 35 };
static const int gpio36_pins[] = { 36 };
static const int gpio37_pins[] = { 37 };
static const int gpio38_pins[] = { 38 };
static const int gpio39_pins[] = { 39 };
static const int gpio40_pins[] = { 40 };
static const int gpio41_pins[] = { 41 };
static const int pcie_reset0_pins[] = { 40 };
static const int pcie_reset1_pins[] = { 41 };

static const struct pingroup pinctrl_groups[] = {
	PINCTRL_PIN_GROUP("pon", pon),
	PINCTRL_PIN_GROUP("sipo", sipo),
	PINCTRL_PIN_GROUP("sipo_rclk", sipo_rclk),
	PINCTRL_PIN_GROUP("uart2", uart2),
	PINCTRL_PIN_GROUP("uart2_cts_rts", uart2_cts_rts),
	PINCTRL_PIN_GROUP("uart3", uart3),
	PINCTRL_PIN_GROUP("ejtag", ejtag),
	PINCTRL_PIN_GROUP("pcm1", pcm1),
	PINCTRL_PIN_GROUP("pcm2", pcm2),
	PINCTRL_PIN_GROUP("spi_quad", spi_quad),
	PINCTRL_PIN_GROUP("spi_cs1", spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi", pcm_spi),
	PINCTRL_PIN_GROUP("pcm_spi_int", pcm_spi_int),
	PINCTRL_PIN_GROUP("pcm_spi_rst", pcm_spi_rst),
	PINCTRL_PIN_GROUP("pcm_spi_cs1", pcm_spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi_cs2", pcm_spi_cs2),
	PINCTRL_PIN_GROUP("pcm_spi_cs3", pcm_spi_cs3),
	PINCTRL_PIN_GROUP("pcm_spi_cs4", pcm_spi_cs4),
	PINCTRL_PIN_GROUP("pcm_spi_cs5", pcm_spi_cs5),
	PINCTRL_PIN_GROUP("pcm_spi_cs6", pcm_spi_cs6),
	PINCTRL_PIN_GROUP("pcm_spi_cs7", pcm_spi_cs7),
	PINCTRL_PIN_GROUP("pnand", pnand),
	PINCTRL_PIN_GROUP("gpio0", gpio0),
	PINCTRL_PIN_GROUP("gpio1", gpio1),
	PINCTRL_PIN_GROUP("gpio2", gpio2),
	PINCTRL_PIN_GROUP("gpio3", gpio3),
	PINCTRL_PIN_GROUP("gpio4", gpio4),
	PINCTRL_PIN_GROUP("gpio5", gpio5),
	PINCTRL_PIN_GROUP("gpio6", gpio6),
	PINCTRL_PIN_GROUP("gpio7", gpio7),
	PINCTRL_PIN_GROUP("gpio8", gpio8),
	PINCTRL_PIN_GROUP("gpio9", gpio9),
	PINCTRL_PIN_GROUP("gpio10", gpio10),
	PINCTRL_PIN_GROUP("gpio11", gpio11),
	PINCTRL_PIN_GROUP("gpio12", gpio12),
	PINCTRL_PIN_GROUP("gpio13", gpio13),
	PINCTRL_PIN_GROUP("gpio14", gpio14),
	PINCTRL_PIN_GROUP("gpio15", gpio15),
	PINCTRL_PIN_GROUP("gpio16", gpio16),
	PINCTRL_PIN_GROUP("gpio17", gpio17),
	PINCTRL_PIN_GROUP("gpio18", gpio18),
	PINCTRL_PIN_GROUP("gpio19", gpio19),
	PINCTRL_PIN_GROUP("gpio20", gpio20),
	PINCTRL_PIN_GROUP("gpio21", gpio21),
	PINCTRL_PIN_GROUP("gpio22", gpio22),
	PINCTRL_PIN_GROUP("gpio23", gpio23),
	PINCTRL_PIN_GROUP("gpio24", gpio24),
	PINCTRL_PIN_GROUP("gpio25", gpio25),
	PINCTRL_PIN_GROUP("gpio26", gpio26),
	PINCTRL_PIN_GROUP("gpio27", gpio27),
	PINCTRL_PIN_GROUP("gpio28", gpio28),
	PINCTRL_PIN_GROUP("gpio29", gpio29),
	PINCTRL_PIN_GROUP("gpio30", gpio30),
	PINCTRL_PIN_GROUP("gpio31", gpio31),
	PINCTRL_PIN_GROUP("gpio32", gpio32),
	PINCTRL_PIN_GROUP("gpio33", gpio33),
	PINCTRL_PIN_GROUP("gpio34", gpio34),
	PINCTRL_PIN_GROUP("gpio35", gpio35),
	PINCTRL_PIN_GROUP("gpio36", gpio36),
	PINCTRL_PIN_GROUP("gpio37", gpio37),
	PINCTRL_PIN_GROUP("gpio38", gpio38),
	PINCTRL_PIN_GROUP("gpio39", gpio39),
	PINCTRL_PIN_GROUP("gpio40", gpio40),
	PINCTRL_PIN_GROUP("gpio41", gpio41),
	PINCTRL_PIN_GROUP("pcie_reset0", pcie_reset0),
	PINCTRL_PIN_GROUP("pcie_reset1", pcie_reset1),
};

static const char *const pon_groups[] = { "pon" };
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const uart_groups[] = { "uart2", "uart2_cts_rts",
						  "uart3" };
static const char *const jtag_groups[] = { "ejtag" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const pcm_spi_groups[] = { "pcm_spi", "pcm_spi_int",
						     "pcm_spi_rst",
						     "pcm_spi_cs1",
						     "pcm_spi_cs2",
						     "pcm_spi_cs3",
						     "pcm_spi_cs4",
						     "pcm_spi_cs5",
						     "pcm_spi_cs6",
						     "pcm_spi_cs7" };
static const char *const pnand_groups[] = { "pnand" };
static const char *const pcie_reset_groups[] = { "pcie_reset0",
							"pcie_reset1" };
static const char *const gpio_groups[] = { "gpio40", "gpio41" };
static const char *const pwm_groups[] = { "gpio0", "gpio1",
						 "gpio2", "gpio3",
						 "gpio4", "gpio5",
						 "gpio6", "gpio7",
						 "gpio8", "gpio9",
						 "gpio10", "gpio11",
						 "gpio12", "gpio13",
						 "gpio14", "gpio15",
						 "gpio16", "gpio17",
						 "gpio18", "gpio19",
						 "gpio20", "gpio21",
						 "gpio22", "gpio23",
						 "gpio24", "gpio25",
						 "gpio26", "gpio27",
						 "gpio28", "gpio29",
						 "gpio30", "gpio31",
						 "gpio32", "gpio37",
						 "gpio38", "gpio39",
						 "gpio40", "gpio41" };
static const char *const phy1_led0_groups[] = { "gpio30", "gpio32",
						       "gpio34", "gpio36" };
static const char *const phy2_led0_groups[] = { "gpio30", "gpio32",
						       "gpio34", "gpio36" };
static const char *const phy3_led0_groups[] = { "gpio30", "gpio32",
						       "gpio34", "gpio36" };
static const char *const phy4_led0_groups[] = { "gpio30", "gpio32",
						       "gpio34", "gpio36" };
static const char *const phy1_led1_groups[] = { "gpio31", "gpio33",
						       "gpio35", "gpio37" };
static const char *const phy2_led1_groups[] = { "gpio31", "gpio33",
						       "gpio35", "gpio37" };
static const char *const phy3_led1_groups[] = { "gpio31", "gpio33",
						       "gpio35", "gpio37" };
static const char *const phy4_led1_groups[] = { "gpio31", "gpio33",
						       "gpio35", "gpio37" };

static const struct airoha_pinctrl_func_group pon_func_group[] = {
	{
		.name = "pon",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PON_MODE_MASK,
			GPIO_PON_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group sipo_func_group[] = {
	{
		.name = "sipo",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK,
			GPIO_SIPO_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "sipo_rclk",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group uart_func_group[] = {
	{
		.name = "uart2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_UART2_MODE_MASK |
			GPIO_UART2_CTS_RTS_MODE_MASK,
			GPIO_UART2_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "uart2_cts_rts",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_UART2_MODE_MASK |
			GPIO_UART2_CTS_RTS_MODE_MASK,
			GPIO_UART2_MODE_MASK |
			GPIO_UART2_CTS_RTS_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "uart3",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_UART3_MODE_MASK,
			GPIO_UART3_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group jtag_func_group[] = {
	{
		.name = "ejtag",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_CPU_EJTAG_EN,
			CPU_EJTAG_EN_MASK,
			CPU_EJTAG_EN_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group pcm_func_group[] = {
	{
		.name = "pcm1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM1_MODE_MASK,
			GPIO_PCM1_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM2_MODE_MASK,
			GPIO_PCM2_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group spi_func_group[] = {
	{
		.name = "spi_quad",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_SPI_QUAD_MODE_MASK,
			GPIO_SPI_QUAD_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "spi_cs1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_SPI_CS1_MODE_MASK,
			GPIO_SPI_CS1_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group pcm_spi_func_group[] = {
	{
		.name = "pcm_spi",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM_SPI_MODE_MASK,
			GPIO_PCM_SPI_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_int",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM_INT_MODE_MASK,
			GPIO_PCM_INT_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_rst",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM_RESET_MODE_MASK,
			GPIO_PCM_RESET_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCM_SPI_CS1_MODE_MASK,
			GPIO_PCM_SPI_CS1_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS2_MODE_MASK,
			GPIO_PCM_SPI_CS2_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs3",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS3_MODE_MASK,
			GPIO_PCM_SPI_CS3_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs4",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS4_MODE_MASK,
			GPIO_PCM_SPI_CS4_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs5",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS5_MODE_MASK,
			GPIO_PCM_SPI_CS5_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs6",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS6_MODE_MASK,
			GPIO_PCM_SPI_CS6_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs7",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PCM_SPI_CS7_MODE_MASK,
			GPIO_PCM_SPI_CS7_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group pnand_func_group[] = {
	{
		.name = "pnand",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO22_EN,
			GPIO_PNAND_MODE_MASK,
			GPIO_PNAND_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/*
 * On EN7528 a set PCIE_RESET{0,1}_GPIO bit routes the pad to its GPIO
 * function, so the PCIe reset function must clear it.
 */
static const struct airoha_pinctrl_func_group pcie_reset_func_group[] = {
	{
		.name = "pcie_reset0",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCIE_RESET0_MASK,
			0
		},
		.regmap_size = 1,
	}, {
		.name = "pcie_reset1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_PON_I2C_MODE,
			GPIO_PCIE_RESET1_MASK,
			0
		},
		.regmap_size = 1,
	},
};

/* GPIO */
#define AIROHA_PINCTRL_GPIO(gpio, mux_val)		\
	{						\
		.name = (gpio),				\
		.regmap[0] = {				\
			AIROHA_FUNC_MUX,		\
			REG_PON_I2C_MODE,	\
			(mux_val),			\
			(mux_val)			\
		},					\
		.regmap_size = 1,			\
	}						\

static const struct airoha_pinctrl_func_group gpio_func_group[] = {
	AIROHA_PINCTRL_GPIO("gpio40", GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_GPIO("gpio41", GPIO_PCIE_RESET1_MASK),
};

/* PWM - AIROHA_PINCTRL_PWM and AIROHA_PINCTRL_PWM_EXT are shared */
#define AIROHA_PINCTRL_PWM(gpio, mux_val)		\
	{						\
		.name = (gpio),				\
		.regmap[0] = {				\
			AIROHA_FUNC_PWM_MUX,		\
			REG_GPIO_FLASH_MODE_CFG,	\
			(mux_val),			\
			(mux_val)			\
		},					\
		.regmap_size = 1,			\
	}						\

#define AIROHA_PINCTRL_PWM_EXT(gpio, mux_val)		\
	{						\
		.name = (gpio),				\
		.regmap[0] = {				\
			AIROHA_FUNC_PWM_EXT_MUX,	\
			REG_GPIO_FLASH_MODE_CFG_EXT,	\
			(mux_val),			\
			(mux_val)			\
		},					\
		.regmap_size = 1,			\
	}						\

/*
 * PWM on the PCIE_RESET pads additionally requires the pad to be
 * routed to its GPIO function via the IOMUX.
 */
#define AIROHA_PINCTRL_PWM_EXT_SEC(gpio, mux_val, smux_val)	\
	{							\
		.name = (gpio),					\
		.regmap[0] = {					\
			AIROHA_FUNC_PWM_EXT_MUX,		\
			REG_GPIO_FLASH_MODE_CFG_EXT,		\
			(mux_val),				\
			(mux_val)				\
		},						\
		.regmap[1] = {					\
			AIROHA_FUNC_MUX,			\
			REG_PON_I2C_MODE,		\
			(smux_val),				\
			(smux_val)				\
		},						\
		.regmap_size = 2,				\
	}

static const struct airoha_pinctrl_func_group pwm_func_group[] = {
	AIROHA_PINCTRL_PWM("gpio0", GPIO0_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio1", GPIO1_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio2", GPIO2_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio3", GPIO3_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio4", GPIO4_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio5", GPIO5_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio6", GPIO6_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio7", GPIO7_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio8", GPIO8_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio9", GPIO9_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio10", GPIO10_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio11", GPIO11_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio12", GPIO12_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio13", GPIO13_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio14", GPIO14_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM("gpio15", GPIO15_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio16", GPIO16_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio17", GPIO17_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio18", GPIO18_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio19", GPIO19_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio20", GPIO20_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio21", GPIO21_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio22", GPIO22_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio23", GPIO23_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio24", GPIO24_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio25", GPIO25_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio26", GPIO26_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio27", GPIO27_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio28", GPIO28_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio29", GPIO29_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio30", GPIO30_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio31", GPIO31_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio32", GPIO32_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio37", GPIO37_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio38", GPIO38_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio39", GPIO39_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio40", GPIO40_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio41", GPIO41_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET1_MASK),
};

#define PINCTRL_PHY_LED0(gpio, mux_val, map_mask, map_val)	\
	{								\
		.name = (gpio),						\
		.regmap[0] = {						\
			AIROHA_FUNC_MUX,				\
			REG_PON_I2C_MODE,			\
			(mux_val),					\
			(mux_val),					\
		},							\
		.regmap[1] = {						\
			AIROHA_FUNC_MUX,				\
			REG_LAN_LED0_MAPPING,			\
			(map_mask),					\
			(map_val),					\
		},							\
		.regmap_size = 2,					\
	}

#define PINCTRL_PHY_LED1(gpio, mux_val, map_mask, map_val)	\
	{								\
		.name = (gpio),						\
		.regmap[0] = {						\
			AIROHA_FUNC_MUX,				\
			REG_PON_I2C_MODE,			\
			(mux_val),					\
			(mux_val),					\
		},							\
		.regmap[1] = {						\
			AIROHA_FUNC_MUX,				\
			REG_LAN_LED1_MAPPING,			\
			(map_mask),					\
			(map_val),					\
		},							\
		.regmap_size = 2,					\
	}

/*
 * LED pad mapping (datasheet table 4-12):
 * GPIO30: LAN0_LED0, GPIO32: LAN1_LED0, GPIO34: LAN2_LED0, GPIO36: LAN3_LED0
 * GPIO31: LAN0_LED1, GPIO33: LAN1_LED1, GPIO35: LAN2_LED1, GPIO37: LAN3_LED1
 */
static const struct airoha_pinctrl_func_group phy1_led0_func_group[] = {
	PINCTRL_PHY_LED0("gpio30", GPIO_LAN0_LED0_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED0("gpio32", GPIO_LAN1_LED0_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED0("gpio34", GPIO_LAN2_LED0_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED0("gpio36", GPIO_LAN3_LED0_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led0_func_group[] = {
	PINCTRL_PHY_LED0("gpio30", GPIO_LAN0_LED0_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED0("gpio32", GPIO_LAN1_LED0_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED0("gpio34", GPIO_LAN2_LED0_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED0("gpio36", GPIO_LAN3_LED0_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led0_func_group[] = {
	PINCTRL_PHY_LED0("gpio30", GPIO_LAN0_LED0_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED0("gpio32", GPIO_LAN1_LED0_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED0("gpio34", GPIO_LAN2_LED0_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED0("gpio36", GPIO_LAN3_LED0_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led0_func_group[] = {
	PINCTRL_PHY_LED0("gpio30", GPIO_LAN0_LED0_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED0("gpio32", GPIO_LAN1_LED0_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED0("gpio34", GPIO_LAN2_LED0_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED0("gpio36", GPIO_LAN3_LED0_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func_group phy1_led1_func_group[] = {
	PINCTRL_PHY_LED1("gpio31", GPIO_LAN0_LED1_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED1("gpio33", GPIO_LAN1_LED1_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED1("gpio35", GPIO_LAN2_LED1_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	PINCTRL_PHY_LED1("gpio37", GPIO_LAN3_LED1_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led1_func_group[] = {
	PINCTRL_PHY_LED1("gpio31", GPIO_LAN0_LED1_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED1("gpio33", GPIO_LAN1_LED1_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED1("gpio35", GPIO_LAN2_LED1_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	PINCTRL_PHY_LED1("gpio37", GPIO_LAN3_LED1_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led1_func_group[] = {
	PINCTRL_PHY_LED1("gpio31", GPIO_LAN0_LED1_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED1("gpio33", GPIO_LAN1_LED1_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED1("gpio35", GPIO_LAN2_LED1_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	PINCTRL_PHY_LED1("gpio37", GPIO_LAN3_LED1_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led1_func_group[] = {
	PINCTRL_PHY_LED1("gpio31", GPIO_LAN0_LED1_MODE_MASK,
			 LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED1("gpio33", GPIO_LAN1_LED1_MODE_MASK,
			 LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED1("gpio35", GPIO_LAN2_LED1_MODE_MASK,
			 LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	PINCTRL_PHY_LED1("gpio37", GPIO_LAN3_LED1_MODE_MASK,
			 LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func pinctrl_funcs[] = {
	PINCTRL_FUNC_DESC("pon", pon),
	PINCTRL_FUNC_DESC("sipo", sipo),
	PINCTRL_FUNC_DESC("uart", uart),
	PINCTRL_FUNC_DESC("jtag", jtag),
	PINCTRL_FUNC_DESC("pcm", pcm),
	PINCTRL_FUNC_DESC("spi", spi),
	PINCTRL_FUNC_DESC("pcm_spi", pcm_spi),
	PINCTRL_FUNC_DESC("pnand", pnand),
	PINCTRL_FUNC_DESC("pcie_reset", pcie_reset),
	PINCTRL_FUNC_DESC("gpio", gpio),
	PINCTRL_FUNC_DESC("pwm", pwm),
	PINCTRL_FUNC_DESC("phy1_led0", phy1_led0),
	PINCTRL_FUNC_DESC("phy2_led0", phy2_led0),
	PINCTRL_FUNC_DESC("phy3_led0", phy3_led0),
	PINCTRL_FUNC_DESC("phy4_led0", phy4_led0),
	PINCTRL_FUNC_DESC("phy1_led1", phy1_led1),
	PINCTRL_FUNC_DESC("phy2_led1", phy2_led1),
	PINCTRL_FUNC_DESC("phy3_led1", phy3_led1),
	PINCTRL_FUNC_DESC("phy4_led1", phy4_led1),
};

static const struct airoha_pinctrl_conf pinctrl_pullup_conf[] = {
	PINCTRL_CONF_DESC(0, REG_GPIO_L_PU, BIT(0)),
	PINCTRL_CONF_DESC(1, REG_GPIO_L_PU, BIT(1)),
	PINCTRL_CONF_DESC(2, REG_GPIO_L_PU, BIT(2)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_PU, BIT(3)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_PU, BIT(4)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_PU, BIT(5)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_PU, BIT(6)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_PU, BIT(7)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_PU, BIT(8)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_PU, BIT(9)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_PU, BIT(10)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_PU, BIT(11)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PU, BIT(12)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PU, BIT(13)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PU, BIT(14)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PU, BIT(15)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PU, BIT(16)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PU, BIT(17)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PU, BIT(18)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PU, BIT(19)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PU, BIT(20)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PU, BIT(21)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PU, BIT(22)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PU, BIT(23)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PU, BIT(24)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PU, BIT(25)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PU, BIT(26)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PU, BIT(27)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PU, BIT(28)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PU, BIT(29)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PU, BIT(30)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PU, BIT(31)),
	PINCTRL_CONF_DESC(32, REG_GPIO_H_PU, BIT(0)),
	PINCTRL_CONF_DESC(33, REG_GPIO_H_PU, BIT(1)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_PU, BIT(2)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_PU, BIT(3)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_PU, BIT(4)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_PU, BIT(5)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_PU, BIT(6)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_PU, BIT(7)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_PU, PCIE0_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PU, PCIE1_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_PU, I2C_SDA_CONF_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_PU, I2C_SCL_CONF_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_PU, UART_TXD_CONF_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_PU, UART_RXD_CONF_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_PU, SPI_CS0_CONF_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_PU, SPI_CLK_CONF_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_PU, SPI_MOSI_CONF_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_PU, SPI_MISO_CONF_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_pulldown_conf[] = {
	PINCTRL_CONF_DESC(0, REG_GPIO_L_PD, BIT(0)),
	PINCTRL_CONF_DESC(1, REG_GPIO_L_PD, BIT(1)),
	PINCTRL_CONF_DESC(2, REG_GPIO_L_PD, BIT(2)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_PD, BIT(3)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_PD, BIT(4)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_PD, BIT(5)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_PD, BIT(6)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_PD, BIT(7)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_PD, BIT(8)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_PD, BIT(9)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_PD, BIT(10)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_PD, BIT(11)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PD, BIT(12)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PD, BIT(13)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PD, BIT(14)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PD, BIT(15)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PD, BIT(16)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PD, BIT(17)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PD, BIT(18)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PD, BIT(19)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PD, BIT(20)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PD, BIT(21)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PD, BIT(22)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PD, BIT(23)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PD, BIT(24)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PD, BIT(25)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PD, BIT(26)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PD, BIT(27)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PD, BIT(28)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PD, BIT(29)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PD, BIT(30)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PD, BIT(31)),
	PINCTRL_CONF_DESC(32, REG_GPIO_H_PD, BIT(0)),
	PINCTRL_CONF_DESC(33, REG_GPIO_H_PD, BIT(1)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_PD, BIT(2)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_PD, BIT(3)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_PD, BIT(4)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_PD, BIT(5)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_PD, BIT(6)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_PD, BIT(7)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_PD, PCIE0_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PD, PCIE1_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_PD, I2C_SDA_CONF_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_PD, I2C_SCL_CONF_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_PD, UART_TXD_CONF_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_PD, UART_RXD_CONF_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_PD, SPI_CS0_CONF_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_PD, SPI_CLK_CONF_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_PD, SPI_MOSI_CONF_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_PD, SPI_MISO_CONF_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_drive_e2_conf[] = {
	PINCTRL_CONF_DESC(0, REG_GPIO_L_E2, BIT(0)),
	PINCTRL_CONF_DESC(1, REG_GPIO_L_E2, BIT(1)),
	PINCTRL_CONF_DESC(2, REG_GPIO_L_E2, BIT(2)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_E2, BIT(3)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_E2, BIT(4)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_E2, BIT(5)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_E2, BIT(6)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_E2, BIT(7)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_E2, BIT(8)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_E2, BIT(9)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_E2, BIT(10)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_E2, BIT(11)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E2, BIT(12)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E2, BIT(13)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E2, BIT(14)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E2, BIT(15)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E2, BIT(16)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E2, BIT(17)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E2, BIT(18)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E2, BIT(19)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E2, BIT(20)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E2, BIT(21)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E2, BIT(22)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E2, BIT(23)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E2, BIT(24)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E2, BIT(25)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E2, BIT(26)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E2, BIT(27)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E2, BIT(28)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E2, BIT(29)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E2, BIT(30)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E2, BIT(31)),
	PINCTRL_CONF_DESC(32, REG_GPIO_H_E2, BIT(0)),
	PINCTRL_CONF_DESC(33, REG_GPIO_H_E2, BIT(1)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_E2, BIT(2)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_E2, BIT(3)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_E2, BIT(4)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_E2, BIT(5)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_E2, BIT(6)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_E2, BIT(7)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_E2, PCIE0_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E2, PCIE1_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_E2, I2C_SDA_CONF_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_E2, I2C_SCL_CONF_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_E2, UART_TXD_CONF_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_E2, UART_RXD_CONF_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_E2, SPI_CS0_CONF_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_E2, SPI_CLK_CONF_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_E2, SPI_MOSI_CONF_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_E2, SPI_MISO_CONF_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_drive_e4_conf[] = {
	PINCTRL_CONF_DESC(0, REG_GPIO_L_E4, BIT(0)),
	PINCTRL_CONF_DESC(1, REG_GPIO_L_E4, BIT(1)),
	PINCTRL_CONF_DESC(2, REG_GPIO_L_E4, BIT(2)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_E4, BIT(3)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_E4, BIT(4)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_E4, BIT(5)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_E4, BIT(6)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_E4, BIT(7)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_E4, BIT(8)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_E4, BIT(9)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_E4, BIT(10)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_E4, BIT(11)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E4, BIT(12)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E4, BIT(13)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E4, BIT(14)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E4, BIT(15)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E4, BIT(16)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E4, BIT(17)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E4, BIT(18)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E4, BIT(19)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E4, BIT(20)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E4, BIT(21)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E4, BIT(22)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E4, BIT(23)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E4, BIT(24)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E4, BIT(25)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E4, BIT(26)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E4, BIT(27)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E4, BIT(28)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E4, BIT(29)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E4, BIT(30)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E4, BIT(31)),
	PINCTRL_CONF_DESC(32, REG_GPIO_H_E4, BIT(0)),
	PINCTRL_CONF_DESC(33, REG_GPIO_H_E4, BIT(1)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_E4, BIT(2)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_E4, BIT(3)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_E4, BIT(4)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_E4, BIT(5)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_E4, BIT(6)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_E4, BIT(7)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_E4, PCIE0_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E4, PCIE1_RESET_CONF_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_E4, I2C_SDA_CONF_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_E4, I2C_SCL_CONF_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_E4, UART_TXD_CONF_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_E4, UART_RXD_CONF_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_E4, SPI_CS0_CONF_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_E4, SPI_CLK_CONF_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_E4, SPI_MOSI_CONF_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_E4, SPI_MISO_CONF_MASK),
};

static const struct airoha_pinctrl_match_data pinctrl_match_data = {
	.chip_scu_compatible = "econet,en7528-chip-scu",
	.pinctrl_name = KBUILD_MODNAME,
	.pinctrl_owner = THIS_MODULE,
	.pins = pinctrl_pins,
	.num_pins = ARRAY_SIZE(pinctrl_pins),
	.grps = pinctrl_groups,
	.num_grps = ARRAY_SIZE(pinctrl_groups),
	.funcs = pinctrl_funcs,
	.num_funcs = ARRAY_SIZE(pinctrl_funcs),
	/* only GPIO0-GPIO15 can raise an interrupt */
	.num_irq_pins = 16,
	.confs_info = {
		[AIROHA_PINCTRL_CONFS_PULLUP] = {
			.confs = pinctrl_pullup_conf,
			.num_confs = ARRAY_SIZE(pinctrl_pullup_conf),
		},
		[AIROHA_PINCTRL_CONFS_PULLDOWN] = {
			.confs = pinctrl_pulldown_conf,
			.num_confs = ARRAY_SIZE(pinctrl_pulldown_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E2] = {
			.confs = pinctrl_drive_e2_conf,
			.num_confs = ARRAY_SIZE(pinctrl_drive_e2_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E4] = {
			.confs = pinctrl_drive_e4_conf,
			.num_confs = ARRAY_SIZE(pinctrl_drive_e4_conf),
		},
		/* no PCIe reset open drain register on EN7528 */
	},
};

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "econet,en7528-pinctrl", .data = &pinctrl_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha-en7528",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_DESCRIPTION("Pinctrl driver for EcoNet EN7528 SoC");
