// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 * Author: Markus Gothe <markus.gothe@genexis.eu>
 * Author: Matheus Sampaio Queiroga <srherobrine20@gmail.com>
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@iopsys.eu>
 */
#include "airoha-common.h"

/* MUX */
#define REG_GPIO_2ND_I2C_MODE			0x0210
#define GPIO_I2S_MODE_MASK			BIT(12)
#define GPIO_I2C_SLAVE_MODE_MODE		BIT(11)
#define GPIO_LAN3_LED1_MODE_MASK		BIT(10)
#define GPIO_LAN3_LED0_MODE_MASK		BIT(9)
#define GPIO_LAN2_LED1_MODE_MASK		BIT(8)
#define GPIO_LAN2_LED0_MODE_MASK		BIT(7)
#define GPIO_LAN1_LED1_MODE_MASK		BIT(6)
#define GPIO_LAN1_LED0_MODE_MASK		BIT(5)
#define GPIO_LAN0_LED1_MODE_MASK		BIT(4)
#define GPIO_LAN0_LED0_MODE_MASK		BIT(3)
#define PON_TOD_1PPS_MODE_MASK			BIT(2)
#define GSW_TOD_1PPS_MODE_MASK			BIT(1)
#define GPIO_2ND_I2C_MODE_MASK			BIT(0)

#define REG_GPIO_SPI_CS1_MODE			0x0214
#define GPIO_PCM_SPI_CS4_MODE_MASK		BIT(21)
#define GPIO_PCM_SPI_CS3_MODE_MASK		BIT(20)
#define GPIO_PCM_SPI_CS2_MODE_P156_MASK		BIT(19)
#define GPIO_PCM_SPI_CS2_MODE_P128_MASK		BIT(18)
#define GPIO_PCM_SPI_CS1_MODE_MASK		BIT(17)
#define GPIO_PCM_SPI_MODE_MASK			BIT(16)
#define GPIO_PCM2_MODE_MASK			BIT(13)
#define GPIO_PCM1_MODE_MASK			BIT(12)
#define GPIO_PCM_INT_MODE_MASK			BIT(9)
#define GPIO_PCM_RESET_MODE_MASK		BIT(8)
#define GPIO_SPI_QUAD_MODE_MASK			BIT(4)
#define GPIO_SPI_CS1_MODE_MASK			BIT(0)

#define REG_GPIO_PON_MODE			0x0218
#define GPIO_SGMII_MDIO_MODE_MASK		BIT(13)
#define SIPO_RCLK_MODE_MASK			BIT(11)
#define GPIO_PCIE_RESET1_MASK			BIT(10)
#define GPIO_PCIE_RESET0_MASK			BIT(9)
#define GPIO_UART2_MODE_MASK			BIT(3)
#define GPIO_SIPO_MODE_MASK			BIT(2)
#define GPIO_PON_MODE_MASK			BIT(0)

#define REG_NPU_UART_EN				0x0220
#define JTAG_UDI_EN_MASK			BIT(4)
#define JTAG_DFD_EN_MASK			BIT(3)
#define NPU_UART_EN_MASK			BIT(2)

#define REG_FORCE_GPIO_EN			0x0224
#define FORCE_GPIO_EN(n)			BIT(n)

/* LED MAP */
#define REG_LAN_LED0_MAPPING			0x0278
#define REG_LAN_LED1_MAPPING			0x027c

#define LAN3_LED_MAPPING_MASK			GENMASK(14, 12)
#define LAN3_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN3_LED_MAPPING_MASK, (_n))

#define LAN2_LED_MAPPING_MASK			GENMASK(10, 8)
#define LAN2_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN2_LED_MAPPING_MASK, (_n))

#define LAN1_LED_MAPPING_MASK			GENMASK(6, 4)
#define LAN1_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN1_LED_MAPPING_MASK, (_n))

#define LAN0_LED_MAPPING_MASK			GENMASK(2, 0)
#define LAN0_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN0_LED_MAPPING_MASK, (_n))

/* CONF */
#define REG_I2C_SDA_E2				0x001c
#define SPI_MISO_E2_MASK			BIT(13)
#define SPI_MOSI_E2_MASK			BIT(12)
#define SPI_CLK_E2_MASK				BIT(11)
#define SPI_CS0_E2_MASK				BIT(10)
#define PCIE1_RESET_E2_MASK			BIT(9)
#define PCIE0_RESET_E2_MASK			BIT(8)
#define UART1_RXD_E2_MASK			BIT(3)
#define UART1_TXD_E2_MASK			BIT(2)
#define I2C_SCL_E2_MASK				BIT(1)
#define I2C_SDA_E2_MASK				BIT(0)

#define REG_I2C_SDA_E4				0x0020
#define SPI_MISO_E4_MASK			BIT(13)
#define SPI_MOSI_E4_MASK			BIT(12)
#define SPI_CLK_E4_MASK				BIT(11)
#define SPI_CS0_E4_MASK				BIT(10)
#define PCIE1_RESET_E4_MASK			BIT(9)
#define PCIE0_RESET_E4_MASK			BIT(8)
#define UART1_RXD_E4_MASK			BIT(3)
#define UART1_TXD_E4_MASK			BIT(2)
#define I2C_SCL_E4_MASK				BIT(1)
#define I2C_SDA_E4_MASK				BIT(0)

#define REG_GPIO_L_E2				0x0024
#define REG_GPIO_L_E4				0x0028

#define REG_I2C_SDA_PU				0x0044
#define SPI_MISO_PU_MASK			BIT(13)
#define SPI_MOSI_PU_MASK			BIT(12)
#define SPI_CLK_PU_MASK				BIT(11)
#define SPI_CS0_PU_MASK				BIT(10)
#define PCIE1_RESET_PU_MASK			BIT(9)
#define PCIE0_RESET_PU_MASK			BIT(8)
#define UART1_RXD_PU_MASK			BIT(3)
#define UART1_TXD_PU_MASK			BIT(2)
#define I2C_SCL_PU_MASK				BIT(1)
#define I2C_SDA_PU_MASK				BIT(0)

#define REG_I2C_SDA_PD				0x0048
#define SPI_MISO_PD_MASK			BIT(13)
#define SPI_MOSI_PD_MASK			BIT(12)
#define SPI_CLK_PD_MASK				BIT(11)
#define SPI_CS0_PD_MASK				BIT(10)
#define PCIE1_RESET_PD_MASK			BIT(9)
#define PCIE0_RESET_PD_MASK			BIT(8)
#define UART1_RXD_PD_MASK			BIT(3)
#define UART1_TXD_PD_MASK			BIT(2)
#define I2C_SCL_PD_MASK				BIT(1)
#define I2C_SDA_PD_MASK				BIT(0)

#define REG_GPIO_L_PU				0x004c
#define REG_GPIO_L_PD				0x0050

/* PWM MODE CONF */
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

/* PWM MODE CONF EXT */
#define REG_GPIO_FLASH_MODE_CFG_EXT		0x0068
#define GPIO51_FLASH_MODE_CFG			BIT(31)
#define GPIO50_FLASH_MODE_CFG			BIT(30)
#define GPIO49_FLASH_MODE_CFG			BIT(29)
#define GPIO48_FLASH_MODE_CFG			BIT(28)
#define GPIO47_FLASH_MODE_CFG			BIT(27)
#define GPIO46_FLASH_MODE_CFG			BIT(26)
#define GPIO45_FLASH_MODE_CFG			BIT(25)
#define GPIO44_FLASH_MODE_CFG			BIT(24)
#define GPIO43_FLASH_MODE_CFG			BIT(23)
#define GPIO42_FLASH_MODE_CFG			BIT(22)
#define GPIO41_FLASH_MODE_CFG			BIT(21)
#define GPIO40_FLASH_MODE_CFG			BIT(20)
#define GPIO39_FLASH_MODE_CFG			BIT(19)
#define GPIO38_FLASH_MODE_CFG			BIT(18)
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

static struct pinctrl_pin_desc pinctrl_pins[] = {
	PINCTRL_PIN(2, "i2c_sda"),
	PINCTRL_PIN(3, "i2c_scl"),
	PINCTRL_PIN(4, "spi_cs0"),
	PINCTRL_PIN(5, "spi_clk"),
	PINCTRL_PIN(6, "spi_mosi"),
	PINCTRL_PIN(7, "spi_miso"),
	PINCTRL_PIN(8, "uart1_txd"),
	PINCTRL_PIN(9, "uart1_rxd"),
	PINCTRL_PIN(12, "gpio0"),
	PINCTRL_PIN(13, "gpio1"),
	PINCTRL_PIN(14, "gpio2"),
	PINCTRL_PIN(15, "gpio3"),
	PINCTRL_PIN(16, "gpio4"),
	PINCTRL_PIN(17, "gpio5"),
	PINCTRL_PIN(18, "gpio6"),
	PINCTRL_PIN(19, "gpio7"),
	PINCTRL_PIN(20, "gpio8"),
	PINCTRL_PIN(21, "gpio9"),
	PINCTRL_PIN(22, "gpio10"),
	PINCTRL_PIN(23, "gpio11"),
	PINCTRL_PIN(24, "gpio12"),
	PINCTRL_PIN(25, "gpio13"),
	PINCTRL_PIN(26, "gpio14"),
	PINCTRL_PIN(27, "gpio15"),
	PINCTRL_PIN(28, "gpio16"),
	PINCTRL_PIN(29, "gpio17"),
	PINCTRL_PIN(30, "gpio18"),
	PINCTRL_PIN(31, "gpio19"),
	PINCTRL_PIN(32, "gpio20"),
	PINCTRL_PIN(33, "gpio21"),
	PINCTRL_PIN(34, "gpio22"),
	PINCTRL_PIN(35, "gpio23"),
	PINCTRL_PIN(36, "gpio24"),
	PINCTRL_PIN(37, "gpio25"),
	PINCTRL_PIN(38, "gpio26"),
	PINCTRL_PIN(39, "gpio27"),
	PINCTRL_PIN(40, "pcie_reset0"),
	PINCTRL_PIN(41, "pcie_reset1"),
};

static const int pon_pins[] = { 28, 29, 30, 31, 32, 33 };
static const int pon_tod_1pps_pins[] = { 21 };
static const int gsw_tod_1pps_pins[] = { 21 };
static const int sipo_pins[] = { 13, 38 };
static const int sipo_rclk_pins[] = { 13, 30, 38 };
static const int mdio_pins[] = { 20, 21 };
static const int uart2_pins[] = { 20, 21 };
static const int npu_uart_pins[] = { 13, 38 };
static const int i2c0_pins[] = { 2, 3 };
static const int i2c1_pins[] = { 14, 15 };
static const int jtag_udi_pins[] = { 34, 35, 36, 37, 38 };
static const int jtag_dfd_pins[] = { 34, 35, 36, 37, 38 };
static const int i2s_pins[] = { 16, 17, 18, 19 };
static const int pcm1_pins[] = { 24, 25, 26, 27 };
static const int pcm2_pins[] = { 16, 17, 18, 19 };
static const int spi_pins[] = { 4, 5, 6, 7 };
static const int spi_quad_pins[] = { 14, 15 };
static const int spi_cs1_pins[] = { 21 };
static const int pcm_spi_pins[] = { 16, 17, 18, 19, 24, 25, 26, 27 };
static const int pcm_spi_int_pins[] = { 15 };
static const int pcm_spi_rst_pins[] = { 14 };
static const int pcm_spi_cs1_pins[] = { 22 };
static const int pcm_spi_cs2_p128_pins[] = { 39 };
static const int pcm_spi_cs2_p156_pins[] = { 39 };
static const int pcm_spi_cs3_pins[] = { 20 };
static const int pcm_spi_cs4_pins[] = { 23 };
static const int gpio0_pins[] = { 12 };
static const int gpio1_pins[] = { 13 };
static const int gpio2_pins[] = { 14 };
static const int gpio3_pins[] = { 15 };
static const int gpio4_pins[] = { 16 };
static const int gpio5_pins[] = { 17 };
static const int gpio6_pins[] = { 18 };
static const int gpio7_pins[] = { 19 };
static const int gpio8_pins[] = { 20 };
static const int gpio9_pins[] = { 21 };
static const int gpio10_pins[] = { 22 };
static const int gpio11_pins[] = { 23 };
static const int gpio12_pins[] = { 24 };
static const int gpio13_pins[] = { 25 };
static const int gpio14_pins[] = { 26 };
static const int gpio15_pins[] = { 27 };
static const int gpio16_pins[] = { 28 };
static const int gpio17_pins[] = { 29 };
static const int gpio18_pins[] = { 30 };
static const int gpio19_pins[] = { 31 };
static const int gpio20_pins[] = { 32 };
static const int gpio21_pins[] = { 33 };
static const int gpio22_pins[] = { 34 };
static const int gpio23_pins[] = { 35 };
static const int gpio24_pins[] = { 36 };
static const int gpio25_pins[] = { 37 };
static const int gpio26_pins[] = { 38 };
static const int gpio27_pins[] = { 39 };
static const int gpio28_pins[] = { 40 };
static const int gpio29_pins[] = { 41 };
static const int pcie_reset0_pins[] = { 40 };
static const int pcie_reset1_pins[] = { 41 };

static const struct pingroup pinctrl_groups[] = {
	PINCTRL_PIN_GROUP("pon", pon),
	PINCTRL_PIN_GROUP("pon_tod_1pps", pon_tod_1pps),
	PINCTRL_PIN_GROUP("gsw_tod_1pps", gsw_tod_1pps),
	PINCTRL_PIN_GROUP("sipo", sipo),
	PINCTRL_PIN_GROUP("sipo_rclk", sipo_rclk),
	PINCTRL_PIN_GROUP("mdio", mdio),
	PINCTRL_PIN_GROUP("uart2", uart2),
	PINCTRL_PIN_GROUP("npu_uart", npu_uart),
	PINCTRL_PIN_GROUP("i2c0", i2c0),
	PINCTRL_PIN_GROUP("i2c1", i2c1),
	PINCTRL_PIN_GROUP("jtag_udi", jtag_udi),
	PINCTRL_PIN_GROUP("jtag_dfd", jtag_dfd),
	PINCTRL_PIN_GROUP("i2s", i2s),
	PINCTRL_PIN_GROUP("pcm1", pcm1),
	PINCTRL_PIN_GROUP("pcm2", pcm2),
	PINCTRL_PIN_GROUP("spi", spi),
	PINCTRL_PIN_GROUP("spi_quad", spi_quad),
	PINCTRL_PIN_GROUP("spi_cs1", spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi", pcm_spi),
	PINCTRL_PIN_GROUP("pcm_spi_int", pcm_spi_int),
	PINCTRL_PIN_GROUP("pcm_spi_rst", pcm_spi_rst),
	PINCTRL_PIN_GROUP("pcm_spi_cs1", pcm_spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi_cs2_p128", pcm_spi_cs2_p128),
	PINCTRL_PIN_GROUP("pcm_spi_cs2_p156", pcm_spi_cs2_p156),
	PINCTRL_PIN_GROUP("pcm_spi_cs3", pcm_spi_cs3),
	PINCTRL_PIN_GROUP("pcm_spi_cs4", pcm_spi_cs4),
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
	PINCTRL_PIN_GROUP("pcie_reset0", pcie_reset0),
	PINCTRL_PIN_GROUP("pcie_reset1", pcie_reset1),
};

static const char *const pon_groups[] = { "pon" };
static const char *const tod_1pps_groups[] = {
	"pon_tod_1pps", "gsw_tod_1pps"
};
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const mdio_groups[] = { "mdio" };
static const char *const uart_groups[] = { "uart2", "npu_uart" };
static const char *const i2c_groups[] = { "i2c1" };
static const char *const jtag_groups[] = { "jtag_udi", "jtag_dfd" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const pcm_spi_groups[] = {
	"pcm_spi", "pcm_spi_int", "pcm_spi_rst", "pcm_spi_cs1",
	"pcm_spi_cs2_p156", "pcm_spi_cs2_p128", "pcm_spi_cs3", "pcm_spi_cs4"
};
static const char *const i2s_groups[] = { "i2s" };
static const char *const gpio_groups[] = { "gpio28", "gpio29" };
static const char *const pcie_reset_groups[] = {
	"pcie_reset0", "pcie_reset1"
};
static const char *const pwm_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",  "gpio4",  "gpio5",
	"gpio6",  "gpio7",  "gpio8",  "gpio9",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29"
};
static const char *const phy1_led0_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};
static const char *const phy2_led0_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};
static const char *const phy3_led0_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};
static const char *const phy4_led0_groups[] = {
	"gpio22", "gpio23", "gpio24", "gpio25"
};
static const char *const phy1_led1_groups[] = {
	"gpio7", "gpio6", "gpio5", "gpio4"
};
static const char *const phy2_led1_groups[] = {
	"gpio7", "gpio6", "gpio5", "gpio4"
};
static const char *const phy3_led1_groups[] = {
	"gpio7", "gpio6", "gpio5", "gpio4"
};
static const char *const phy4_led1_groups[] = {
	"gpio7", "gpio6", "gpio5", "gpio4"
};

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

static const struct airoha_pinctrl_func_group tod_1pps_func_group[] = {
	{
		.name = "pon_tod_1pps",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			PON_TOD_1PPS_MODE_MASK,
			PON_TOD_1PPS_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "gsw_tod_1pps",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GSW_TOD_1PPS_MODE_MASK,
			GSW_TOD_1PPS_MODE_MASK
		},
		.regmap_size = 1,
	},
};

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
		.name = "uart2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_UART2_MODE_MASK,
			GPIO_UART2_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "npu_uart",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_NPU_UART_EN,
			NPU_UART_EN_MASK,
			NPU_UART_EN_MASK
		},
		.regmap_size = 1,
	}
};

static const struct airoha_pinctrl_func_group i2c_func_group[] = {
	{
		.name = "i2c1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_2ND_I2C_MODE_MASK,
			GPIO_2ND_I2C_MODE_MASK
		},
		.regmap_size = 1,
	},
};

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

static const struct airoha_pinctrl_func_group pcm_spi_func_group[] = {
	{
		.name = "pcm_spi",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_MODE_MASK,
			GPIO_PCM_SPI_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_int",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_INT_MODE_MASK,
			GPIO_PCM_INT_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_rst",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_RESET_MODE_MASK,
			GPIO_PCM_RESET_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_CS1_MODE_MASK,
			GPIO_PCM_SPI_CS1_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs2_p128",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_CS2_MODE_P128_MASK,
			GPIO_PCM_SPI_CS2_MODE_P128_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs2_p156",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_CS2_MODE_P156_MASK,
			GPIO_PCM_SPI_CS2_MODE_P156_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs3",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_CS3_MODE_MASK,
			GPIO_PCM_SPI_CS3_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcm_spi_cs4",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_PCM_SPI_CS4_MODE_MASK,
			GPIO_PCM_SPI_CS4_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group i2s_func_group[] = {
	{
		.name = "i2s",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_I2S_MODE_MASK,
			GPIO_I2S_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group gpio_func_group[] = {
	AIROHA_PINCTRL_GPIO_EXT("gpio28", GPIO28_FLASH_MODE_CFG,
				GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio29", GPIO29_FLASH_MODE_CFG,
				GPIO_PCIE_RESET1_MASK),
};

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
};

static const struct airoha_pinctrl_func_group phy1_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio22", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio23", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio24", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio25", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio22", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio23", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio24", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio25", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio22", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio23", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio24", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio25", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio22", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio23", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio24", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio25", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func_group phy1_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio7", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio6", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio5", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio4", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func pinctrl_funcs[] = {
	PINCTRL_FUNC_DESC("pon", pon),
	PINCTRL_FUNC_DESC("tod_1pps", tod_1pps),
	PINCTRL_FUNC_DESC("sipo", sipo),
	PINCTRL_FUNC_DESC("mdio", mdio),
	PINCTRL_FUNC_DESC("uart", uart),
	PINCTRL_FUNC_DESC("i2c", i2c),
	PINCTRL_FUNC_DESC("jtag", jtag),
	PINCTRL_FUNC_DESC("pcm", pcm),
	PINCTRL_FUNC_DESC("spi", spi),
	PINCTRL_FUNC_DESC("pcm_spi", pcm_spi),
	PINCTRL_FUNC_DESC("i2s", i2s),
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
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PU, I2C_SDA_PU_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PU, I2C_SCL_PU_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_PU, SPI_CS0_PU_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_PU, SPI_CLK_PU_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_PU, SPI_MOSI_PU_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_PU, SPI_MISO_PU_MASK),
	PINCTRL_CONF_DESC(8, REG_I2C_SDA_PU, UART1_TXD_PU_MASK),
	PINCTRL_CONF_DESC(9, REG_I2C_SDA_PU, UART1_RXD_PU_MASK),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PU, BIT(0)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PU, BIT(1)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PU, BIT(2)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PU, BIT(3)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PU, BIT(4)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PU, BIT(5)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PU, BIT(6)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PU, BIT(7)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PU, BIT(8)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PU, BIT(9)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PU, BIT(10)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PU, BIT(11)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PU, BIT(12)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PU, BIT(13)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PU, BIT(14)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PU, BIT(15)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PU, BIT(16)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PU, BIT(17)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PU, BIT(18)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PU, BIT(19)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PU, BIT(20)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PU, BIT(21)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_PU, BIT(22)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_PU, BIT(23)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_PU, BIT(24)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_PU, BIT(25)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_PU, BIT(26)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_PU, BIT(27)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_PU, PCIE0_RESET_PU_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PU, PCIE1_RESET_PU_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_pulldown_conf[] = {
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PD, I2C_SDA_PD_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PD, I2C_SCL_PD_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_PD, SPI_CS0_PD_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_PD, SPI_CLK_PD_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_PD, SPI_MOSI_PD_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_PD, SPI_MISO_PD_MASK),
	PINCTRL_CONF_DESC(8, REG_I2C_SDA_PD, UART1_TXD_PD_MASK),
	PINCTRL_CONF_DESC(9, REG_I2C_SDA_PD, UART1_RXD_PD_MASK),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PD, BIT(0)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PD, BIT(1)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PD, BIT(2)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PD, BIT(3)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PD, BIT(4)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PD, BIT(5)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PD, BIT(6)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PD, BIT(7)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PD, BIT(8)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PD, BIT(9)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PD, BIT(10)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PD, BIT(11)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PD, BIT(12)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PD, BIT(13)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PD, BIT(14)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PD, BIT(15)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PD, BIT(16)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PD, BIT(17)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PD, BIT(18)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PD, BIT(19)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PD, BIT(20)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PD, BIT(21)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_PD, BIT(22)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_PD, BIT(23)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_PD, BIT(24)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_PD, BIT(25)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_PD, BIT(26)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_PD, BIT(27)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_PD, PCIE0_RESET_PD_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PD, PCIE1_RESET_PD_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_drive_e2_conf[] = {
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E2, I2C_SDA_E2_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E2, I2C_SCL_E2_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_E2, SPI_CS0_E2_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_E2, SPI_CLK_E2_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_E2, SPI_MOSI_E2_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_E2, SPI_MISO_E2_MASK),
	PINCTRL_CONF_DESC(8, REG_I2C_SDA_E2, UART1_TXD_E2_MASK),
	PINCTRL_CONF_DESC(9, REG_I2C_SDA_E2, UART1_RXD_E2_MASK),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E2, BIT(0)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E2, BIT(1)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E2, BIT(2)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E2, BIT(3)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E2, BIT(4)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E2, BIT(5)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E2, BIT(6)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E2, BIT(7)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E2, BIT(8)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E2, BIT(9)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E2, BIT(10)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E2, BIT(11)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E2, BIT(12)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E2, BIT(13)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E2, BIT(14)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E2, BIT(15)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E2, BIT(16)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E2, BIT(17)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E2, BIT(18)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E2, BIT(19)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E2, BIT(20)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E2, BIT(21)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_E2, BIT(22)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_E2, BIT(23)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_E2, BIT(24)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_E2, BIT(25)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_E2, BIT(26)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_E2, BIT(27)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_E2, PCIE0_RESET_E2_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E2, PCIE1_RESET_E2_MASK),
};

static const struct airoha_pinctrl_conf pinctrl_drive_e4_conf[] = {
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E4, I2C_SDA_E4_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E4, I2C_SCL_E4_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_E4, SPI_CS0_E4_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_E4, SPI_CLK_E4_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_E4, SPI_MOSI_E4_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_E4, SPI_MISO_E4_MASK),
	PINCTRL_CONF_DESC(8, REG_I2C_SDA_E4, UART1_TXD_E4_MASK),
	PINCTRL_CONF_DESC(9, REG_I2C_SDA_E4, UART1_RXD_E4_MASK),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E4, BIT(0)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E4, BIT(1)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E4, BIT(2)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E4, BIT(3)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E4, BIT(4)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E4, BIT(5)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E4, BIT(6)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E4, BIT(7)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E4, BIT(8)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E4, BIT(9)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E4, BIT(10)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E4, BIT(11)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E4, BIT(12)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E4, BIT(13)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E4, BIT(14)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E4, BIT(15)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E4, BIT(16)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E4, BIT(17)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E4, BIT(18)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E4, BIT(19)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E4, BIT(20)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E4, BIT(21)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_E4, BIT(22)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_E4, BIT(23)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_E4, BIT(24)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_E4, BIT(25)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_E4, BIT(26)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_E4, BIT(27)),
	PINCTRL_CONF_DESC(40, REG_I2C_SDA_E4, PCIE0_RESET_E4_MASK),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E4, PCIE1_RESET_E4_MASK),
};

static const struct airoha_pinctrl_match_data pinctrl_match_data = {
	.chip_scu_compatible = "airoha,en7523-chip-scu",
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
	},
};

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "airoha,en7523-pinctrl", .data = &pinctrl_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha-en7523",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_AUTHOR("Matheus Sampaio Queiroga <srherobrine20@gmail.com>");
MODULE_AUTHOR("Mikhail Kshevetskiy <mikhail.kshevetskiy@iopsys.eu>");
MODULE_DESCRIPTION("Pinctrl driver for Airoha EN7523 SoC");
