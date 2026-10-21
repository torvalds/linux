// SPDX-License-Identifier: GPL-2.0-only
/*
 * AN7563 SoC pinctrl data for the Airoha pinctrl driver (pinctrl-airoha.c).
 *
 * Sources: AN7563 Programming Guide chapter 2 (CHIP SCU, base 0x1fa20000),
 * chapter 17 (GPIO controller, base 0x1fbf0200) and AN7563PT datasheet
 * chapter 4.3 (pin sharing scheme).
 *
 * Pin numbering follows the GPIO line numbering of the SoC:
 *   pins  0-27 -> GPIO0-GPIO27 (dedicated GPIO pads)
 *   pins 28/29 -> PCIE_RESET0/PCIE_RESET1 pads (GPIO28/GPIO29)
 *   pins 30/31 -> I2C_SCL/I2C_SDA pads (GPIO30/GPIO31)
 *   pins 32-35 -> SPI_CLK/SPI_CS/SPI_MOSI/SPI_MISO pads (GPIO32-GPIO35)
 *   pins 36/37 -> UART_TXD/UART_RXD pads (GPIO36/GPIO37)
 *
 * Definitions marked "shared" are identical to the ones already present in
 * pinctrl-airoha.c (EN7581/AN7583) and must be dropped when this data is
 * merged there.
 */

#include "airoha-common.h"

/*
 * shared - named RG_SW_TOD_1PPS_MODE on AN7563. Only the LAN LED mode
 * bits and GSW_TOD_1PPS exist on AN7563 (no 2nd I2C, I2S or PON 1PPS).
 */
#define REG_GPIO_2ND_I2C_MODE			0x0214
#define GPIO_LAN3_LED1_MODE_MASK		BIT(10)
#define GPIO_LAN3_LED0_MODE_MASK		BIT(9)
#define GPIO_LAN2_LED1_MODE_MASK		BIT(8)
#define GPIO_LAN2_LED0_MODE_MASK		BIT(7)
#define GPIO_LAN1_LED1_MODE_MASK		BIT(6)
#define GPIO_LAN1_LED0_MODE_MASK		BIT(5)
#define GPIO_LAN0_LED1_MODE_MASK		BIT(4)
#define GPIO_LAN0_LED0_MODE_MASK		BIT(3)
#define GSW_TOD_1PPS_MODE_MASK			BIT(1)

/* shared */
#define REG_GPIO_SPI_CS1_MODE			0x0218
#define GPIO_PCM_SPI_CS4_MODE_MASK		BIT(21)
#define GPIO_PCM_SPI_CS3_MODE_MASK		BIT(20)
#define GPIO_PCM_SPI_CS2_MODE_MASK		BIT(18)
#define GPIO_PCM_SPI_CS1_MODE_MASK		BIT(17)
#define GPIO_PCM_SPI_MODE_MASK			BIT(16)
#define GPIO_PCM2_MODE_MASK			BIT(13)
#define GPIO_PCM1_MODE_MASK			BIT(12)
#define GPIO_PCM_INT_MODE_MASK			BIT(9)
#define GPIO_PCM_RESET_MODE_MASK		BIT(8)
#define GPIO_SPI_QUAD_MODE_MASK			BIT(4)
#define GPIO_SPI_CS4_MODE_MASK			BIT(3)
#define GPIO_SPI_CS3_MODE_MASK			BIT(2)
#define GPIO_SPI_CS2_MODE_MASK			BIT(1)
#define GPIO_SPI_CS1_MODE_MASK			BIT(0)

#define REG_GPIO_PON_MODE			0x021c
/*
 * AN7563 specific: route the standalone pads to their GPIO function.
 * 0: pad keeps its base function, 1: pad is GPIO<n>.
 */
#define UART_RXD_GPIO_MODE_MASK			BIT(22)	/* GPIO37 */
#define UART_TXD_GPIO_MODE_MASK			BIT(21)	/* GPIO36 */
#define SPI_MISO_GPIO_MODE_MASK			BIT(20)	/* GPIO35 */
#define SPI_MOSI_GPIO_MODE_MASK			BIT(19)	/* GPIO34 */
#define SPI_CS_GPIO_MODE_MASK			BIT(18)	/* GPIO33 */
#define SPI_CLK_GPIO_MODE_MASK			BIT(17)	/* GPIO32 */
#define I2C_SDA_GPIO_MODE_MASK			BIT(16)	/* GPIO31 */
#define I2C_SCL_GPIO_MODE_MASK			BIT(15)	/* GPIO30 */
/* shared */
#define GPIO_PARALLEL_NAND_MODE_MASK		BIT(14)
#define GPIO_SGMII_MDIO_MODE_MASK		BIT(13)
#define SIPO_RCLK_MODE_MASK			BIT(11)
/*
 * Note: on AN7563 GPIO_PCIE_RESET{0,1} select the GPIO function of the
 * PCIE_RESET pads (0: PCIe reset, 1: GPIO28/GPIO29).
 */
#define GPIO_PCIE_RESET1_MASK			BIT(10)	/* GPIO29 */
#define GPIO_PCIE_RESET0_MASK			BIT(9)	/* GPIO28 */
#define GPIO_HSUART_CTS_RTS_MODE_MASK		BIT(6)
#define GPIO_HSUART_MODE_MASK			BIT(5)
#define GPIO_SIPO_MODE_MASK			BIT(2)
#define GPIO_PON_MODE_MASK			BIT(0)

/* shared */
#define REG_NPU_UART_EN				0x0224
#define JTAG_UDI_EN_MASK			BIT(4)
#define JTAG_DFD_EN_MASK			BIT(3)

/* LED MAP - shared */
#define REG_LAN_LED0_MAPPING			0x027c
#define REG_LAN_LED1_MAPPING			0x0280

#define LAN3_LED_MAPPING_MASK			GENMASK(14, 12)
#define LAN3_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN3_LED_MAPPING_MASK, (_n))

#define LAN2_LED_MAPPING_MASK			GENMASK(10, 8)
#define LAN2_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN2_LED_MAPPING_MASK, (_n))

#define LAN1_LED_MAPPING_MASK			GENMASK(6, 4)
#define LAN1_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN1_LED_MAPPING_MASK, (_n))

#define LAN0_LED_MAPPING_MASK			GENMASK(2, 0)
#define LAN0_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN0_LED_MAPPING_MASK, (_n))

/*
 * CONF - shared.
 * The AN7563 standalone IO conf registers use the same bit layout as
 * EN7581 (UART1_TXD/RXD are named UART_TXD/RXD on AN7563).
 */
#define REG_I2C_SDA_E2				0x001c
#define SPI_MISO_E2_MASK			BIT(14)
#define SPI_MOSI_E2_MASK			BIT(13)
#define SPI_CLK_E2_MASK				BIT(12)
#define SPI_CS0_E2_MASK				BIT(11)
#define PCIE1_RESET_E2_MASK			BIT(9)
#define PCIE0_RESET_E2_MASK			BIT(8)
#define UART1_RXD_E2_MASK			BIT(3)
#define UART1_TXD_E2_MASK			BIT(2)
#define I2C_SCL_E2_MASK				BIT(1)
#define I2C_SDA_E2_MASK				BIT(0)

#define REG_I2C_SDA_E4				0x0020
#define SPI_MISO_E4_MASK			BIT(14)
#define SPI_MOSI_E4_MASK			BIT(13)
#define SPI_CLK_E4_MASK				BIT(12)
#define SPI_CS0_E4_MASK				BIT(11)
#define PCIE1_RESET_E4_MASK			BIT(9)
#define PCIE0_RESET_E4_MASK			BIT(8)
#define UART1_RXD_E4_MASK			BIT(3)
#define UART1_TXD_E4_MASK			BIT(2)
#define I2C_SCL_E4_MASK				BIT(1)
#define I2C_SDA_E4_MASK				BIT(0)

#define REG_GPIO_L_E2				0x0024
#define REG_GPIO_L_E4				0x0028

#define REG_I2C_SDA_PU				0x0044
#define SPI_MISO_PU_MASK			BIT(14)
#define SPI_MOSI_PU_MASK			BIT(13)
#define SPI_CLK_PU_MASK				BIT(12)
#define SPI_CS0_PU_MASK				BIT(11)
#define PCIE1_RESET_PU_MASK			BIT(9)
#define PCIE0_RESET_PU_MASK			BIT(8)
#define UART1_RXD_PU_MASK			BIT(3)
#define UART1_TXD_PU_MASK			BIT(2)
#define I2C_SCL_PU_MASK				BIT(1)
#define I2C_SDA_PU_MASK				BIT(0)

#define REG_I2C_SDA_PD				0x0048
#define SPI_MISO_PD_MASK			BIT(14)
#define SPI_MOSI_PD_MASK			BIT(13)
#define SPI_CLK_PD_MASK				BIT(12)
#define SPI_CS0_PD_MASK				BIT(11)
#define PCIE1_RESET_PD_MASK			BIT(9)
#define PCIE0_RESET_PD_MASK			BIT(8)
#define UART1_RXD_PD_MASK			BIT(3)
#define UART1_TXD_PD_MASK			BIT(2)
#define I2C_SCL_PD_MASK				BIT(1)
#define I2C_SDA_PD_MASK				BIT(0)

#define REG_GPIO_L_PU				0x004c
#define REG_GPIO_L_PD				0x0050

#define REG_PCIE_RESET_OD			0x018c
#define PCIE1_RESET_OD_MASK			BIT(1)
#define PCIE0_RESET_OD_MASK			BIT(0)

/*
 * PWM MODE CONF - shared.
 * The AN7563 GPIO flash mode registers use the same layout as EN7581:
 * REG_GPIO_FLASH_MODE_CFG covers GPIO0-15, REG_GPIO_FLASH_MODE_CFG_EXT
 * covers GPIO16-31 (bits 0-15) and GPIO36+ (bits 16+). The SPI pads
 * (GPIO32-35) have no flash mode configuration bit.
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
#define GPIO37_FLASH_MODE_CFG			BIT(17)
#define GPIO36_FLASH_MODE_CFG			BIT(16)
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

#define AIROHA_PINCTRL_GPIO(gpio, mux_val)			\
	{							\
		.name = (gpio),					\
		.regmap[0] = {					\
			AIROHA_FUNC_MUX,			\
			REG_GPIO_PON_MODE,			\
			(mux_val),				\
			(mux_val)				\
		},						\
		.regmap_size = 1,				\
	}

#define AIROHA_PINCTRL_GPIO_EXT(gpio, mux_val, smux_val)	\
	{							\
		.name = (gpio),					\
		.regmap[0] = {					\
			AIROHA_FUNC_PWM_EXT_MUX,		\
			REG_GPIO_FLASH_MODE_CFG_EXT,		\
			(mux_val),				\
			0					\
		},						\
		.regmap[1] = {					\
			AIROHA_FUNC_MUX,			\
			REG_GPIO_PON_MODE,			\
			(smux_val),				\
			(smux_val)				\
		},						\
		.regmap_size = 2,				\
	}

/* PWM */
#define AIROHA_PINCTRL_PWM(gpio, mux_val)			\
	{							\
		.name = (gpio),					\
		.regmap[0] = {					\
			AIROHA_FUNC_PWM_MUX,			\
			REG_GPIO_FLASH_MODE_CFG,		\
			(mux_val),				\
			(mux_val)				\
		},						\
		.regmap_size = 1,				\
	}

#define AIROHA_PINCTRL_PWM_EXT(gpio, mux_val)			\
	{							\
		.name = (gpio),					\
		.regmap[0] = {					\
			AIROHA_FUNC_PWM_EXT_MUX,		\
			REG_GPIO_FLASH_MODE_CFG_EXT,		\
			(mux_val),				\
			(mux_val)				\
		},						\
		.regmap_size = 1,				\
	}

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
			REG_GPIO_PON_MODE,			\
			(smux_val),				\
			(smux_val)				\
		},						\
		.regmap_size = 2,				\
	}

#define AIROHA_PINCTRL_PHY_LED0(gpio, mux_val, map_mask, map_val)	\
	{								\
		.name = (gpio),						\
		.regmap[0] = {						\
			AIROHA_FUNC_MUX,				\
			REG_GPIO_2ND_I2C_MODE,				\
			(mux_val),					\
			(mux_val),					\
		},							\
		.regmap[1] = {						\
			AIROHA_FUNC_MUX,				\
			REG_LAN_LED0_MAPPING,				\
			(map_mask),					\
			(map_val),					\
		},							\
		.regmap_size = 2,					\
	}

#define AIROHA_PINCTRL_PHY_LED1(gpio, mux_val, map_mask, map_val)	\
	{								\
		.name = (gpio),						\
		.regmap[0] = {						\
			AIROHA_FUNC_MUX,				\
			REG_GPIO_2ND_I2C_MODE,				\
			(mux_val),					\
			(mux_val),					\
		},							\
		.regmap[1] = {						\
			AIROHA_FUNC_MUX,				\
			REG_LAN_LED1_MAPPING,				\
			(map_mask),					\
			(map_val),					\
		},							\
		.regmap_size = 2,					\
	}

static const struct pinctrl_pin_desc pinctrl_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "pcie_reset0"),	/* GPIO28 */
	PINCTRL_PIN(29, "pcie_reset1"),	/* GPIO29 */
	PINCTRL_PIN(30, "i2c_scl"),	/* GPIO30 */
	PINCTRL_PIN(31, "i2c_sda"),	/* GPIO31 */
	PINCTRL_PIN(32, "spi_clk"),	/* GPIO32 */
	PINCTRL_PIN(33, "spi_cs"),	/* GPIO33 */
	PINCTRL_PIN(34, "spi_mosi"),	/* GPIO34 */
	PINCTRL_PIN(35, "spi_miso"),	/* GPIO35 */
	PINCTRL_PIN(36, "uart_txd"),	/* GPIO36 */
	PINCTRL_PIN(37, "uart_rxd"),	/* GPIO37 */
};

static const int pon_pins[] = { 14, 15, 16, 17, 18, 19 };
static const int sipo_pins[] = { 20, 21 };
static const int sipo_rclk_pins[] = { 20, 21, 26 };
static const int mdio_pins[] = { 30, 31 };
static const int hsuart_pins[] = { 16, 17 };
static const int hsuart_cts_rts_pins[] = { 14, 15 };
static const int i2c_pins[] = { 30, 31 };
static const int jtag_udi_pins[] = { 7, 8, 9, 10, 11 };
static const int jtag_dfd_pins[] = { 7, 8, 9, 10, 11 };
static const int pcm1_pins[] = { 22, 23, 24, 25 };
static const int pcm2_pins[] = { 1, 2, 3, 4 };
static const int spi_pins[] = { 32, 33, 34, 35 };
static const int spi_quad_pins[] = { 2, 3 };
static const int spi_cs1_pins[] = { 4 };
static const int pnand_pins[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 27, 32, 33, 34, 35
};
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
static const int pcie_reset0_pins[] = { 28 };
static const int pcie_reset1_pins[] = { 29 };

static const struct pingroup pinctrl_groups[] = {
	PINCTRL_PIN_GROUP("pon", pon),
	PINCTRL_PIN_GROUP("sipo", sipo),
	PINCTRL_PIN_GROUP("sipo_rclk", sipo_rclk),
	PINCTRL_PIN_GROUP("mdio", mdio),
	PINCTRL_PIN_GROUP("hsuart", hsuart),
	PINCTRL_PIN_GROUP("hsuart_cts_rts", hsuart_cts_rts),
	PINCTRL_PIN_GROUP("i2c", i2c),
	PINCTRL_PIN_GROUP("jtag_udi", jtag_udi),
	PINCTRL_PIN_GROUP("jtag_dfd", jtag_dfd),
	PINCTRL_PIN_GROUP("pcm1", pcm1),
	PINCTRL_PIN_GROUP("pcm2", pcm2),
	PINCTRL_PIN_GROUP("spi", spi),
	PINCTRL_PIN_GROUP("spi_quad", spi_quad),
	PINCTRL_PIN_GROUP("spi_cs1", spi_cs1),
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
	PINCTRL_PIN_GROUP("pcie_reset0", pcie_reset0),
	PINCTRL_PIN_GROUP("pcie_reset1", pcie_reset1),
};

/* shared */
static const char *const pon_groups[] = { "pon" };
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const mdio_groups[] = { "mdio" };
static const char *const uart_groups[] = {
	"hsuart", "hsuart_cts_rts"
};
static const char *const jtag_groups[] = { "jtag_udi", "jtag_dfd" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const pnand_groups[] = { "pnand" };
static const char *const gpio_groups[] = {
	"gpio28", "gpio29", "gpio30", "gpio31", "gpio32",
	"gpio33", "gpio34", "gpio35", "gpio36", "gpio37"
};
static const char *const pcie_reset_groups[] = {
	"pcie_reset0", "pcie_reset1"
};
static const char *const pwm_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",  "gpio4",  "gpio5",
	"gpio6",  "gpio7",  "gpio8",  "gpio9",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio36", "gpio37"
};
static const char *const phy1_led0_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const phy2_led0_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const phy3_led0_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const phy4_led0_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const phy1_led1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};
static const char *const phy2_led1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};
static const char *const phy3_led1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};
static const char *const phy4_led1_groups[] = {
	"gpio4", "gpio5", "gpio6", "gpio7"
};

/* shared */
static const struct airoha_pinctrl_func_group pon_func_group[] = {
	{
		.name = "pon",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PON_MODE_MASK,
			GPIO_PON_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/* shared */
static const struct airoha_pinctrl_func_group sipo_func_group[] = {
	{
		.name = "sipo",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK,
			GPIO_SIPO_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "sipo_rclk",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK,
			GPIO_SIPO_MODE_MASK | SIPO_RCLK_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/*
 * On AN7563 the SMI master (MDC/MDIO) is shared with the I2C_SCL/I2C_SDA
 * pads and selected via the SGMII MDIO mode bit. I2C is the default pad
 * function (no mux bit).
 */
static const struct airoha_pinctrl_func_group mdio_func_group[] = {
	{
		.name = "mdio",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_SGMII_MDIO_MODE_MASK,
			GPIO_SGMII_MDIO_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group uart_func_group[] = {
	{
		.name = "hsuart",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_HSUART_MODE_MASK | GPIO_HSUART_CTS_RTS_MODE_MASK,
			GPIO_HSUART_MODE_MASK
		},
		.regmap_size = 1,
	},
	{
		.name = "hsuart_cts_rts",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_HSUART_MODE_MASK | GPIO_HSUART_CTS_RTS_MODE_MASK,
			GPIO_HSUART_MODE_MASK | GPIO_HSUART_CTS_RTS_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/* shared */
static const struct airoha_pinctrl_func_group jtag_func_group[] = {
	{
		.name = "jtag_udi",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_NPU_UART_EN,
			JTAG_UDI_EN_MASK,
			JTAG_UDI_EN_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "jtag_dfd",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_NPU_UART_EN,
			JTAG_DFD_EN_MASK,
			JTAG_DFD_EN_MASK
		},
		.regmap_size = 1,
	},
};

/* shared */
static const struct airoha_pinctrl_func_group pcm_func_group[] = {
	{
		.name = "pcm1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM1_MODE_MASK,
			GPIO_PCM1_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM2_MODE_MASK,
			GPIO_PCM2_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/* shared */
static const struct airoha_pinctrl_func_group spi_func_group[] = {
	{
		.name = "spi_quad",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_SPI_QUAD_MODE_MASK,
			GPIO_SPI_QUAD_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "spi_cs1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_SPI_CS1_MODE_MASK,
			GPIO_SPI_CS1_MODE_MASK
		},
		.regmap_size = 1,
	},
};

/* shared */
static const struct airoha_pinctrl_func_group pnand_func_group[] = {
	{
		.name = "pnand",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PARALLEL_NAND_MODE_MASK,
			GPIO_PARALLEL_NAND_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group gpio_func_group[] = {
	AIROHA_PINCTRL_GPIO_EXT("gpio28", GPIO28_FLASH_MODE_CFG,
				GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio29", GPIO29_FLASH_MODE_CFG,
				GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio30", GPIO30_FLASH_MODE_CFG,
				I2C_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio31", GPIO31_FLASH_MODE_CFG,
				I2C_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO("gpio32", SPI_CLK_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO("gpio33", SPI_CS_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO("gpio34", SPI_MOSI_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO("gpio35", SPI_MISO_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio36", GPIO36_FLASH_MODE_CFG,
				UART_TXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio37", GPIO37_FLASH_MODE_CFG,
				UART_RXD_GPIO_MODE_MASK),
};

/*
 * On AN7563 a set GPIO_PCIE_RESET{0,1} bit routes the pad to its GPIO
 * function, so the PCIe reset function must clear it.
 */
static const struct airoha_pinctrl_func_group pcie_reset_func_group[] = {
	{
		.name = "pcie_reset0",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET0_MASK,
			0
		},
		.regmap_size = 1,
	}, {
		.name = "pcie_reset1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET1_MASK,
			0
		},
		.regmap_size = 1,
	},
};

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
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio28", GPIO28_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio29", GPIO29_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio30", GPIO30_FLASH_MODE_CFG,
				   I2C_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio31", GPIO31_FLASH_MODE_CFG,
				   I2C_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio36", GPIO36_FLASH_MODE_CFG,
				   UART_TXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio37", GPIO37_FLASH_MODE_CFG,
				   UART_RXD_GPIO_MODE_MASK),
};

/*
 * LED pad mapping (datasheet table 4-9):
 * GPIO8: LAN0_LED0, GPIO9: LAN1_LED0, GPIO10: LAN2_LED0, GPIO11: LAN3_LED0
 * GPIO7: LAN0_LED1, GPIO6: LAN1_LED1, GPIO5: LAN2_LED1, GPIO4: LAN3_LED1
 */
static const struct airoha_pinctrl_func_group phy1_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio8", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio9", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio10", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio11", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio8", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio9", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio10", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio11", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio8", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio9", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio10", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio11", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio8", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio9", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio10", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio11", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func_group phy1_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func pinctrl_funcs[] = {
	PINCTRL_FUNC_DESC("pon", pon),
	PINCTRL_FUNC_DESC("sipo", sipo),
	PINCTRL_FUNC_DESC("mdio", mdio),
	PINCTRL_FUNC_DESC("uart", uart),
	PINCTRL_FUNC_DESC("jtag", jtag),
	PINCTRL_FUNC_DESC("pcm", pcm),
	PINCTRL_FUNC_DESC("spi", spi),
	PINCTRL_FUNC_DESC("pnand", pnand),
	PINCTRL_FUNC_DESC("gpio", gpio),
	PINCTRL_FUNC_DESC("pcie_reset", pcie_reset),
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
	PINCTRL_CONF_DESC(28, REG_I2C_SDA_PU, PCIE0_RESET_PU_MASK),
	PINCTRL_CONF_DESC(29, REG_I2C_SDA_PU, PCIE1_RESET_PU_MASK),
	PINCTRL_CONF_DESC(30, REG_I2C_SDA_PU, I2C_SCL_PU_MASK),
	PINCTRL_CONF_DESC(31, REG_I2C_SDA_PU, I2C_SDA_PU_MASK),
	PINCTRL_CONF_DESC(32, REG_I2C_SDA_PU, SPI_CLK_PU_MASK),
	PINCTRL_CONF_DESC(33, REG_I2C_SDA_PU, SPI_CS0_PU_MASK),
	PINCTRL_CONF_DESC(34, REG_I2C_SDA_PU, SPI_MOSI_PU_MASK),
	PINCTRL_CONF_DESC(35, REG_I2C_SDA_PU, SPI_MISO_PU_MASK),
	PINCTRL_CONF_DESC(36, REG_I2C_SDA_PU, UART1_TXD_PU_MASK),
	PINCTRL_CONF_DESC(37, REG_I2C_SDA_PU, UART1_RXD_PU_MASK),
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
	PINCTRL_CONF_DESC(28, REG_I2C_SDA_PD, PCIE0_RESET_PD_MASK),
	PINCTRL_CONF_DESC(29, REG_I2C_SDA_PD, PCIE1_RESET_PD_MASK),
	PINCTRL_CONF_DESC(30, REG_I2C_SDA_PD, I2C_SCL_PD_MASK),
	PINCTRL_CONF_DESC(31, REG_I2C_SDA_PD, I2C_SDA_PD_MASK),
	PINCTRL_CONF_DESC(32, REG_I2C_SDA_PD, SPI_CLK_PD_MASK),
	PINCTRL_CONF_DESC(33, REG_I2C_SDA_PD, SPI_CS0_PD_MASK),
	PINCTRL_CONF_DESC(34, REG_I2C_SDA_PD, SPI_MOSI_PD_MASK),
	PINCTRL_CONF_DESC(35, REG_I2C_SDA_PD, SPI_MISO_PD_MASK),
	PINCTRL_CONF_DESC(36, REG_I2C_SDA_PD, UART1_TXD_PD_MASK),
	PINCTRL_CONF_DESC(37, REG_I2C_SDA_PD, UART1_RXD_PD_MASK),
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
	PINCTRL_CONF_DESC(28, REG_I2C_SDA_E2, PCIE0_RESET_E2_MASK),
	PINCTRL_CONF_DESC(29, REG_I2C_SDA_E2, PCIE1_RESET_E2_MASK),
	PINCTRL_CONF_DESC(30, REG_I2C_SDA_E2, I2C_SCL_E2_MASK),
	PINCTRL_CONF_DESC(31, REG_I2C_SDA_E2, I2C_SDA_E2_MASK),
	PINCTRL_CONF_DESC(32, REG_I2C_SDA_E2, SPI_CLK_E2_MASK),
	PINCTRL_CONF_DESC(33, REG_I2C_SDA_E2, SPI_CS0_E2_MASK),
	PINCTRL_CONF_DESC(34, REG_I2C_SDA_E2, SPI_MOSI_E2_MASK),
	PINCTRL_CONF_DESC(35, REG_I2C_SDA_E2, SPI_MISO_E2_MASK),
	PINCTRL_CONF_DESC(36, REG_I2C_SDA_E2, UART1_TXD_E2_MASK),
	PINCTRL_CONF_DESC(37, REG_I2C_SDA_E2, UART1_RXD_E2_MASK),
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
	PINCTRL_CONF_DESC(28, REG_I2C_SDA_E4, PCIE0_RESET_E4_MASK),
	PINCTRL_CONF_DESC(29, REG_I2C_SDA_E4, PCIE1_RESET_E4_MASK),
	PINCTRL_CONF_DESC(30, REG_I2C_SDA_E4, I2C_SCL_E4_MASK),
	PINCTRL_CONF_DESC(31, REG_I2C_SDA_E4, I2C_SDA_E4_MASK),
	PINCTRL_CONF_DESC(32, REG_I2C_SDA_E4, SPI_CLK_E4_MASK),
	PINCTRL_CONF_DESC(33, REG_I2C_SDA_E4, SPI_CS0_E4_MASK),
	PINCTRL_CONF_DESC(34, REG_I2C_SDA_E4, SPI_MOSI_E4_MASK),
	PINCTRL_CONF_DESC(35, REG_I2C_SDA_E4, SPI_MISO_E4_MASK),
	PINCTRL_CONF_DESC(36, REG_I2C_SDA_E4, UART1_TXD_E4_MASK),
	PINCTRL_CONF_DESC(37, REG_I2C_SDA_E4, UART1_RXD_E4_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_pcie_rst_od_conf[] = {
	PINCTRL_CONF_DESC(28, REG_PCIE_RESET_OD, PCIE0_RESET_OD_MASK),
	PINCTRL_CONF_DESC(29, REG_PCIE_RESET_OD, PCIE1_RESET_OD_MASK),
};

static const struct airoha_pinctrl_match_data pinctrl_match_data = {
	.chip_scu_compatible = "airoha,en7581-chip-scu",
	.pinctrl_name = KBUILD_MODNAME,
	.pinctrl_owner = THIS_MODULE,
	.pins = pinctrl_pins,
	.num_pins = ARRAY_SIZE(pinctrl_pins),
	.grps = pinctrl_groups,
	.num_grps = ARRAY_SIZE(pinctrl_groups),
	.funcs = pinctrl_funcs,
	.num_funcs = ARRAY_SIZE(pinctrl_funcs),
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
		[AIROHA_PINCTRL_CONFS_PCIE_RST_OD] = {
			.confs = pinctrl_pcie_rst_od_conf,
			.num_confs = ARRAY_SIZE(pinctrl_pcie_rst_od_conf),
		},
	},
};

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "airoha,an7563-pinctrl", .data = &pinctrl_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha-an7563",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_DESCRIPTION("Pinctrl driver for Airoha AN7563 SoC");
