// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 * Author: Markus Gothe <markus.gothe@genexis.eu>
 */

#include "airoha-common.h"

/* MUX */
#define REG_GPIO_2ND_I2C_MODE			0x0214
#define GPIO_MDC_IO_MASTER_MODE_MASK		BIT(14)
#define GPIO_I2C_MASTER_MODE_MODE		BIT(13)
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

#define REG_GPIO_SPI_CS1_MODE			0x0218
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
#define GPIO_SPI_CS4_MODE_MASK			BIT(3)
#define GPIO_SPI_CS3_MODE_MASK			BIT(2)
#define GPIO_SPI_CS2_MODE_MASK			BIT(1)
#define GPIO_SPI_CS1_MODE_MASK			BIT(0)

#define REG_GPIO_PON_MODE			0x021c
#define GPIO_PARALLEL_NAND_MODE_MASK		BIT(14)
#define GPIO_SGMII_MDIO_MODE_MASK		BIT(13)
#define GPIO_PCIE_RESET2_MASK			BIT(12)
#define SIPO_RCLK_MODE_MASK			BIT(11)
#define GPIO_PCIE_RESET1_MASK			BIT(10)
#define GPIO_PCIE_RESET0_MASK			BIT(9)
#define GPIO_UART5_MODE_MASK			BIT(8)
#define GPIO_UART4_MODE_MASK			BIT(7)
#define GPIO_HSUART_CTS_RTS_MODE_MASK		BIT(6)
#define GPIO_HSUART_MODE_MASK			BIT(5)
#define GPIO_UART2_CTS_RTS_MODE_MASK		BIT(4)
#define GPIO_UART2_MODE_MASK			BIT(3)
#define GPIO_SIPO_MODE_MASK			BIT(2)
#define GPIO_EMMC_MODE_MASK			BIT(1)
#define GPIO_PON_MODE_MASK			BIT(0)

#define REG_NPU_UART_EN				0x0224
#define JTAG_UDI_EN_MASK			BIT(4)
#define JTAG_DFD_EN_MASK			BIT(3)

#define REG_FORCE_GPIO_EN			0x0228
#define FORCE_GPIO_EN(n)			BIT(n)

/* LED MAP */
#define REG_LAN_LED0_MAPPING			0x027c
#define REG_LAN_LED1_MAPPING			0x0280

#define LAN4_LED_MAPPING_MASK			GENMASK(18, 16)
#define LAN4_PHY_LED_MAP(_n)			FIELD_PREP_CONST(LAN4_LED_MAPPING_MASK, (_n))

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
#define SPI_MISO_E2_MASK			BIT(14)
#define SPI_MOSI_E2_MASK			BIT(13)
#define SPI_CLK_E2_MASK				BIT(12)
#define SPI_CS0_E2_MASK				BIT(11)
#define PCIE2_RESET_E2_MASK			BIT(10)
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
#define PCIE2_RESET_E4_MASK			BIT(10)
#define PCIE1_RESET_E4_MASK			BIT(9)
#define PCIE0_RESET_E4_MASK			BIT(8)
#define UART1_RXD_E4_MASK			BIT(3)
#define UART1_TXD_E4_MASK			BIT(2)
#define I2C_SCL_E4_MASK				BIT(1)
#define I2C_SDA_E4_MASK				BIT(0)

#define REG_GPIO_L_E2				0x0024
#define REG_GPIO_L_E4				0x0028
#define REG_GPIO_H_E2				0x002c
#define REG_GPIO_H_E4				0x0030

#define REG_I2C_SDA_PU				0x0044
#define SPI_MISO_PU_MASK			BIT(14)
#define SPI_MOSI_PU_MASK			BIT(13)
#define SPI_CLK_PU_MASK				BIT(12)
#define SPI_CS0_PU_MASK				BIT(11)
#define PCIE2_RESET_PU_MASK			BIT(10)
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
#define PCIE2_RESET_PD_MASK			BIT(10)
#define PCIE1_RESET_PD_MASK			BIT(9)
#define PCIE0_RESET_PD_MASK			BIT(8)
#define UART1_RXD_PD_MASK			BIT(3)
#define UART1_TXD_PD_MASK			BIT(2)
#define I2C_SCL_PD_MASK				BIT(1)
#define I2C_SDA_PD_MASK				BIT(0)

#define REG_GPIO_L_PU				0x004c
#define REG_GPIO_L_PD				0x0050
#define REG_GPIO_H_PU				0x0054
#define REG_GPIO_H_PD				0x0058

#define REG_PCIE_RESET_OD			0x018c
#define PCIE2_RESET_OD_MASK			BIT(2)
#define PCIE1_RESET_OD_MASK			BIT(1)
#define PCIE0_RESET_OD_MASK			BIT(0)

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

static struct pinctrl_pin_desc en7581_pinctrl_pins[] = {
	PINCTRL_PIN(0, "uart1_txd"),
	PINCTRL_PIN(1, "uart1_rxd"),
	PINCTRL_PIN(2, "i2c_scl"),
	PINCTRL_PIN(3, "i2c_sda"),
	PINCTRL_PIN(4, "spi_cs0"),
	PINCTRL_PIN(5, "spi_clk"),
	PINCTRL_PIN(6, "spi_mosi"),
	PINCTRL_PIN(7, "spi_miso"),
	PINCTRL_PIN(13, "gpio0"),
	PINCTRL_PIN(14, "gpio1"),
	PINCTRL_PIN(15, "gpio2"),
	PINCTRL_PIN(16, "gpio3"),
	PINCTRL_PIN(17, "gpio4"),
	PINCTRL_PIN(18, "gpio5"),
	PINCTRL_PIN(19, "gpio6"),
	PINCTRL_PIN(20, "gpio7"),
	PINCTRL_PIN(21, "gpio8"),
	PINCTRL_PIN(22, "gpio9"),
	PINCTRL_PIN(23, "gpio10"),
	PINCTRL_PIN(24, "gpio11"),
	PINCTRL_PIN(25, "gpio12"),
	PINCTRL_PIN(26, "gpio13"),
	PINCTRL_PIN(27, "gpio14"),
	PINCTRL_PIN(28, "gpio15"),
	PINCTRL_PIN(29, "gpio16"),
	PINCTRL_PIN(30, "gpio17"),
	PINCTRL_PIN(31, "gpio18"),
	PINCTRL_PIN(32, "gpio19"),
	PINCTRL_PIN(33, "gpio20"),
	PINCTRL_PIN(34, "gpio21"),
	PINCTRL_PIN(35, "gpio22"),
	PINCTRL_PIN(36, "gpio23"),
	PINCTRL_PIN(37, "gpio24"),
	PINCTRL_PIN(38, "gpio25"),
	PINCTRL_PIN(39, "gpio26"),
	PINCTRL_PIN(40, "gpio27"),
	PINCTRL_PIN(41, "gpio28"),
	PINCTRL_PIN(42, "gpio29"),
	PINCTRL_PIN(43, "gpio30"),
	PINCTRL_PIN(44, "gpio31"),
	PINCTRL_PIN(45, "gpio32"),
	PINCTRL_PIN(46, "gpio33"),
	PINCTRL_PIN(47, "gpio34"),
	PINCTRL_PIN(48, "gpio35"),
	PINCTRL_PIN(49, "gpio36"),
	PINCTRL_PIN(50, "gpio37"),
	PINCTRL_PIN(51, "gpio38"),
	PINCTRL_PIN(52, "gpio39"),
	PINCTRL_PIN(53, "gpio40"),
	PINCTRL_PIN(54, "gpio41"),
	PINCTRL_PIN(55, "gpio42"),
	PINCTRL_PIN(56, "gpio43"),
	PINCTRL_PIN(57, "gpio44"),
	PINCTRL_PIN(58, "gpio45"),
	PINCTRL_PIN(59, "gpio46"),
	PINCTRL_PIN(60, "pcie_reset0"),
	PINCTRL_PIN(61, "pcie_reset1"),
	PINCTRL_PIN(62, "pcie_reset2"),
};

static const int en7581_pon_pins[] = { 49, 50, 51, 52, 53, 54 };
static const int en7581_pon_tod_1pps_pins[] = { 46 };
static const int en7581_gsw_tod_1pps_pins[] = { 46 };
static const int en7581_sipo_pins[] = { 16, 17 };
static const int en7581_sipo_rclk_pins[] = { 16, 17, 43 };
static const int en7581_mdio_pins[] = { 14, 15 };
static const int en7581_uart2_pins[] = { 48, 55 };
static const int en7581_uart2_cts_rts_pins[] = { 46, 47 };
static const int en7581_hsuart_pins[] = { 28, 29 };
static const int en7581_hsuart_cts_rts_pins[] = { 26, 27 };
static const int en7581_uart4_pins[] = { 38, 39 };
static const int en7581_uart5_pins[] = { 18, 19 };
static const int en7581_i2c0_pins[] = { 2, 3 };
static const int en7581_i2c1_pins[] = { 14, 15 };
static const int en7581_jtag_udi_pins[] = { 16, 17, 18, 19, 20 };
static const int en7581_jtag_dfd_pins[] = { 16, 17, 18, 19, 20 };
static const int en7581_i2s_pins[] = { 26, 27, 28, 29 };
static const int en7581_pcm1_pins[] = { 22, 23, 24, 25 };
static const int en7581_pcm2_pins[] = { 18, 19, 20, 21 };
static const int en7581_spi_quad_pins[] = { 32, 33 };
static const int en7581_spi_pins[] = { 4, 5, 6, 7 };
static const int en7581_spi_cs1_pins[] = { 34 };
static const int en7581_pcm_spi_pins[] = { 18, 19, 20, 21, 22, 23, 24, 25 };
static const int en7581_pcm_spi_int_pins[] = { 14 };
static const int en7581_pcm_spi_rst_pins[] = { 15 };
static const int en7581_pcm_spi_cs1_pins[] = { 43 };
static const int en7581_pcm_spi_cs2_pins[] = { 40 };
static const int en7581_pcm_spi_cs2_p128_pins[] = { 40 };
static const int en7581_pcm_spi_cs2_p156_pins[] = { 40 };
static const int en7581_pcm_spi_cs3_pins[] = { 41 };
static const int en7581_pcm_spi_cs4_pins[] = { 42 };
static const int en7581_emmc_pins[] = {
	4, 5, 6, 30, 31, 32, 33, 34, 35, 36, 37
};
static const int en7581_pnand_pins[] = {
	4, 5, 6, 7, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42
};
static const int en7581_gpio0_pins[] = { 13 };
static const int en7581_gpio1_pins[] = { 14 };
static const int en7581_gpio2_pins[] = { 15 };
static const int en7581_gpio3_pins[] = { 16 };
static const int en7581_gpio4_pins[] = { 17 };
static const int en7581_gpio5_pins[] = { 18 };
static const int en7581_gpio6_pins[] = { 19 };
static const int en7581_gpio7_pins[] = { 20 };
static const int en7581_gpio8_pins[] = { 21 };
static const int en7581_gpio9_pins[] = { 22 };
static const int en7581_gpio10_pins[] = { 23 };
static const int en7581_gpio11_pins[] = { 24 };
static const int en7581_gpio12_pins[] = { 25 };
static const int en7581_gpio13_pins[] = { 26 };
static const int en7581_gpio14_pins[] = { 27 };
static const int en7581_gpio15_pins[] = { 28 };
static const int en7581_gpio16_pins[] = { 29 };
static const int en7581_gpio17_pins[] = { 30 };
static const int en7581_gpio18_pins[] = { 31 };
static const int en7581_gpio19_pins[] = { 32 };
static const int en7581_gpio20_pins[] = { 33 };
static const int en7581_gpio21_pins[] = { 34 };
static const int en7581_gpio22_pins[] = { 35 };
static const int en7581_gpio23_pins[] = { 36 };
static const int en7581_gpio24_pins[] = { 37 };
static const int en7581_gpio25_pins[] = { 38 };
static const int en7581_gpio26_pins[] = { 39 };
static const int en7581_gpio27_pins[] = { 40 };
static const int en7581_gpio28_pins[] = { 41 };
static const int en7581_gpio29_pins[] = { 42 };
static const int en7581_gpio30_pins[] = { 43 };
static const int en7581_gpio31_pins[] = { 44 };
static const int en7581_gpio32_pins[] = { 45 };
static const int en7581_gpio33_pins[] = { 46 };
static const int en7581_gpio34_pins[] = { 47 };
static const int en7581_gpio35_pins[] = { 48 };
static const int en7581_gpio36_pins[] = { 49 };
static const int en7581_gpio37_pins[] = { 50 };
static const int en7581_gpio38_pins[] = { 51 };
static const int en7581_gpio39_pins[] = { 52 };
static const int en7581_gpio40_pins[] = { 53 };
static const int en7581_gpio41_pins[] = { 54 };
static const int en7581_gpio42_pins[] = { 55 };
static const int en7581_gpio43_pins[] = { 56 };
static const int en7581_gpio44_pins[] = { 57 };
static const int en7581_gpio45_pins[] = { 58 };
static const int en7581_gpio46_pins[] = { 59 };
static const int en7581_gpio47_pins[] = { 60 };
static const int en7581_gpio48_pins[] = { 61 };
static const int en7581_gpio49_pins[] = { 62 };
static const int en7581_pcie_reset0_pins[] = { 60 };
static const int en7581_pcie_reset1_pins[] = { 61 };
static const int en7581_pcie_reset2_pins[] = { 62 };

static const struct pingroup en7581_pinctrl_groups[] = {
	PINCTRL_PIN_GROUP("pon", en7581_pon),
	PINCTRL_PIN_GROUP("pon_tod_1pps", en7581_pon_tod_1pps),
	PINCTRL_PIN_GROUP("gsw_tod_1pps", en7581_gsw_tod_1pps),
	PINCTRL_PIN_GROUP("sipo", en7581_sipo),
	PINCTRL_PIN_GROUP("sipo_rclk", en7581_sipo_rclk),
	PINCTRL_PIN_GROUP("mdio", en7581_mdio),
	PINCTRL_PIN_GROUP("uart2", en7581_uart2),
	PINCTRL_PIN_GROUP("uart2_cts_rts", en7581_uart2_cts_rts),
	PINCTRL_PIN_GROUP("hsuart", en7581_hsuart),
	PINCTRL_PIN_GROUP("hsuart_cts_rts", en7581_hsuart_cts_rts),
	PINCTRL_PIN_GROUP("uart4", en7581_uart4),
	PINCTRL_PIN_GROUP("uart5", en7581_uart5),
	PINCTRL_PIN_GROUP("i2c0", en7581_i2c0),
	PINCTRL_PIN_GROUP("i2c1", en7581_i2c1),
	PINCTRL_PIN_GROUP("jtag_udi", en7581_jtag_udi),
	PINCTRL_PIN_GROUP("jtag_dfd", en7581_jtag_dfd),
	PINCTRL_PIN_GROUP("i2s", en7581_i2s),
	PINCTRL_PIN_GROUP("pcm1", en7581_pcm1),
	PINCTRL_PIN_GROUP("pcm2", en7581_pcm2),
	PINCTRL_PIN_GROUP("spi", en7581_spi),
	PINCTRL_PIN_GROUP("spi_quad", en7581_spi_quad),
	PINCTRL_PIN_GROUP("spi_cs1", en7581_spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi", en7581_pcm_spi),
	PINCTRL_PIN_GROUP("pcm_spi_int", en7581_pcm_spi_int),
	PINCTRL_PIN_GROUP("pcm_spi_rst", en7581_pcm_spi_rst),
	PINCTRL_PIN_GROUP("pcm_spi_cs1", en7581_pcm_spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi_cs2_p128", en7581_pcm_spi_cs2_p128),
	PINCTRL_PIN_GROUP("pcm_spi_cs2_p156", en7581_pcm_spi_cs2_p156),
	PINCTRL_PIN_GROUP("pcm_spi_cs2", en7581_pcm_spi_cs2),
	PINCTRL_PIN_GROUP("pcm_spi_cs3", en7581_pcm_spi_cs3),
	PINCTRL_PIN_GROUP("pcm_spi_cs4", en7581_pcm_spi_cs4),
	PINCTRL_PIN_GROUP("emmc", en7581_emmc),
	PINCTRL_PIN_GROUP("pnand", en7581_pnand),
	PINCTRL_PIN_GROUP("gpio0", en7581_gpio0),
	PINCTRL_PIN_GROUP("gpio1", en7581_gpio1),
	PINCTRL_PIN_GROUP("gpio2", en7581_gpio2),
	PINCTRL_PIN_GROUP("gpio3", en7581_gpio3),
	PINCTRL_PIN_GROUP("gpio4", en7581_gpio4),
	PINCTRL_PIN_GROUP("gpio5", en7581_gpio5),
	PINCTRL_PIN_GROUP("gpio6", en7581_gpio6),
	PINCTRL_PIN_GROUP("gpio7", en7581_gpio7),
	PINCTRL_PIN_GROUP("gpio8", en7581_gpio8),
	PINCTRL_PIN_GROUP("gpio9", en7581_gpio9),
	PINCTRL_PIN_GROUP("gpio10", en7581_gpio10),
	PINCTRL_PIN_GROUP("gpio11", en7581_gpio11),
	PINCTRL_PIN_GROUP("gpio12", en7581_gpio12),
	PINCTRL_PIN_GROUP("gpio13", en7581_gpio13),
	PINCTRL_PIN_GROUP("gpio14", en7581_gpio14),
	PINCTRL_PIN_GROUP("gpio15", en7581_gpio15),
	PINCTRL_PIN_GROUP("gpio16", en7581_gpio16),
	PINCTRL_PIN_GROUP("gpio17", en7581_gpio17),
	PINCTRL_PIN_GROUP("gpio18", en7581_gpio18),
	PINCTRL_PIN_GROUP("gpio19", en7581_gpio19),
	PINCTRL_PIN_GROUP("gpio20", en7581_gpio20),
	PINCTRL_PIN_GROUP("gpio21", en7581_gpio21),
	PINCTRL_PIN_GROUP("gpio22", en7581_gpio22),
	PINCTRL_PIN_GROUP("gpio23", en7581_gpio23),
	PINCTRL_PIN_GROUP("gpio24", en7581_gpio24),
	PINCTRL_PIN_GROUP("gpio25", en7581_gpio25),
	PINCTRL_PIN_GROUP("gpio26", en7581_gpio26),
	PINCTRL_PIN_GROUP("gpio27", en7581_gpio27),
	PINCTRL_PIN_GROUP("gpio28", en7581_gpio28),
	PINCTRL_PIN_GROUP("gpio29", en7581_gpio29),
	PINCTRL_PIN_GROUP("gpio30", en7581_gpio30),
	PINCTRL_PIN_GROUP("gpio31", en7581_gpio31),
	PINCTRL_PIN_GROUP("gpio32", en7581_gpio32),
	PINCTRL_PIN_GROUP("gpio33", en7581_gpio33),
	PINCTRL_PIN_GROUP("gpio34", en7581_gpio34),
	PINCTRL_PIN_GROUP("gpio35", en7581_gpio35),
	PINCTRL_PIN_GROUP("gpio36", en7581_gpio36),
	PINCTRL_PIN_GROUP("gpio37", en7581_gpio37),
	PINCTRL_PIN_GROUP("gpio38", en7581_gpio38),
	PINCTRL_PIN_GROUP("gpio39", en7581_gpio39),
	PINCTRL_PIN_GROUP("gpio40", en7581_gpio40),
	PINCTRL_PIN_GROUP("gpio41", en7581_gpio41),
	PINCTRL_PIN_GROUP("gpio42", en7581_gpio42),
	PINCTRL_PIN_GROUP("gpio43", en7581_gpio43),
	PINCTRL_PIN_GROUP("gpio44", en7581_gpio44),
	PINCTRL_PIN_GROUP("gpio45", en7581_gpio45),
	PINCTRL_PIN_GROUP("gpio46", en7581_gpio46),
	PINCTRL_PIN_GROUP("gpio47", en7581_gpio47),
	PINCTRL_PIN_GROUP("gpio48", en7581_gpio48),
	PINCTRL_PIN_GROUP("gpio49", en7581_gpio49),
	PINCTRL_PIN_GROUP("pcie_reset0", en7581_pcie_reset0),
	PINCTRL_PIN_GROUP("pcie_reset1", en7581_pcie_reset1),
	PINCTRL_PIN_GROUP("pcie_reset2", en7581_pcie_reset2),
};

static const char *const pon_groups[] = { "pon" };
static const char *const tod_1pps_groups[] = {
	"pon_tod_1pps", "gsw_tod_1pps"
};
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const mdio_groups[] = { "mdio" };
static const char *const uart_groups[] = {
	"uart2", "uart2_cts_rts", "hsuart", "hsuart_cts_rts",
	"uart4", "uart5"
};
static const char *const i2c_groups[] = { "i2c1" };
static const char *const jtag_groups[] = { "jtag_udi", "jtag_dfd" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const pcm_spi_groups[] = {
	"pcm_spi", "pcm_spi_int", "pcm_spi_rst", "pcm_spi_cs1",
	"pcm_spi_cs2_p156", "pcm_spi_cs2_p128", "pcm_spi_cs3",
	"pcm_spi_cs4"
};
static const char *const i2s_groups[] = { "i2s" };
static const char *const emmc_groups[] = { "emmc" };
static const char *const pnand_groups[] = { "pnand" };
static const char *const gpio_groups[] = { "gpio47", "gpio48", "gpio49" };
static const char *const pcie_reset_groups[] = {
	"pcie_reset0", "pcie_reset1", "pcie_reset2"
};
static const char *const pwm_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",  "gpio4",  "gpio5",
	"gpio6",  "gpio7",  "gpio8",  "gpio9",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio36", "gpio37", "gpio38", "gpio39",
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44", "gpio45",
	"gpio46", "gpio47", "gpio48", "gpio49"
};
static const char *const phy1_led0_groups[] = {
	"gpio33", "gpio34", "gpio35", "gpio42"
};
static const char *const phy2_led0_groups[] = {
	"gpio33", "gpio34", "gpio35", "gpio42"
};
static const char *const phy3_led0_groups[] = {
	"gpio33", "gpio34", "gpio35", "gpio42"
};
static const char *const phy4_led0_groups[] = {
	"gpio33", "gpio34", "gpio35", "gpio42"
};
static const char *const phy1_led1_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46"
};
static const char *const phy2_led1_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46"
};
static const char *const phy3_led1_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46"
};
static const char *const phy4_led1_groups[] = {
	"gpio43", "gpio44", "gpio45", "gpio46"
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
			REG_GPIO_2ND_I2C_MODE,
			GPIO_MDC_IO_MASTER_MODE_MASK,
			GPIO_MDC_IO_MASTER_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_FORCE_GPIO_EN,
			FORCE_GPIO_EN(1) | FORCE_GPIO_EN(2),
			FORCE_GPIO_EN(1) | FORCE_GPIO_EN(2)
		},
		.regmap_size = 2,
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
		.name = "uart2_cts_rts",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_UART2_MODE_MASK | GPIO_UART2_CTS_RTS_MODE_MASK,
			GPIO_UART2_MODE_MASK | GPIO_UART2_CTS_RTS_MODE_MASK
		},
		.regmap_size = 1,
	}, {
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
	}, {
		.name = "uart4",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_UART4_MODE_MASK,
			GPIO_UART4_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "uart5",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_UART5_MODE_MASK,
			GPIO_UART5_MODE_MASK
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group i2c_func_group[] = {
	{
		.name = "i2c1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_2ND_I2C_MODE_MASK | GPIO_I2C_MASTER_MODE_MODE,
			GPIO_2ND_I2C_MODE_MASK | GPIO_I2C_MASTER_MODE_MODE
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
	}, {
		.name = "spi_cs2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_SPI_CS2_MODE_MASK,
			GPIO_SPI_CS2_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "spi_cs3",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_SPI_CS3_MODE_MASK,
			GPIO_SPI_CS3_MODE_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "spi_cs4",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			GPIO_SPI_CS4_MODE_MASK,
			GPIO_SPI_CS4_MODE_MASK
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

static const struct airoha_pinctrl_func_group emmc_func_group[] = {
	{
		.name = "emmc",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_EMMC_MODE_MASK,
			GPIO_EMMC_MODE_MASK
		},
		.regmap_size = 1,
	},
};

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
	AIROHA_PINCTRL_GPIO_EXT("gpio47", GPIO47_FLASH_MODE_CFG,
				GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio48", GPIO48_FLASH_MODE_CFG,
				GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio49", GPIO49_FLASH_MODE_CFG,
				GPIO_PCIE_RESET2_MASK),
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
	}, {
		.name = "pcie_reset2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET2_MASK,
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
	AIROHA_PINCTRL_PWM_EXT("gpio28", GPIO28_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio29", GPIO29_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio30", GPIO30_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio31", GPIO31_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio36", GPIO36_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio37", GPIO37_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio38", GPIO38_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio39", GPIO39_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio40", GPIO40_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio41", GPIO41_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio42", GPIO42_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio43", GPIO43_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio44", GPIO44_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio45", GPIO45_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT("gpio46", GPIO46_FLASH_MODE_CFG),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio47", GPIO47_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio48", GPIO48_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio49", GPIO49_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET2_MASK),
};

static const struct airoha_pinctrl_func_group phy1_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio33", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio34", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio35", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio42", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio33", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio34", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio35", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio42", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio33", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio34", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio35", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio42", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio33", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio34", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio35", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio42", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func_group phy1_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio43", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio44", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio45", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio46", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group phy2_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio43", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio44", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio45", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio46", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group phy3_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio43", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio44", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio45", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio46", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group phy4_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio43", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio44", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio45", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio46", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func en7581_pinctrl_funcs[] = {
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
	PINCTRL_FUNC_DESC("emmc", emmc),
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

static const struct airoha_pinctrl_conf en7581_pinctrl_pullup_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_PU, UART1_TXD_PU_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_PU, UART1_RXD_PU_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PU, I2C_SCL_PU_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PU, I2C_SDA_PU_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_PU, SPI_CS0_PU_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_PU, SPI_CLK_PU_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_PU, SPI_MOSI_PU_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_PU, SPI_MISO_PU_MASK),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PU, BIT(0)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PU, BIT(1)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PU, BIT(2)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PU, BIT(3)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PU, BIT(4)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PU, BIT(5)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PU, BIT(6)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PU, BIT(7)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PU, BIT(8)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PU, BIT(9)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PU, BIT(10)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PU, BIT(11)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PU, BIT(12)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PU, BIT(13)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PU, BIT(14)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PU, BIT(15)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PU, BIT(16)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PU, BIT(17)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PU, BIT(18)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PU, BIT(19)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PU, BIT(20)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_PU, BIT(21)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_PU, BIT(22)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_PU, BIT(23)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_PU, BIT(24)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_PU, BIT(25)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_PU, BIT(26)),
	PINCTRL_CONF_DESC(40, REG_GPIO_L_PU, BIT(27)),
	PINCTRL_CONF_DESC(41, REG_GPIO_L_PU, BIT(28)),
	PINCTRL_CONF_DESC(42, REG_GPIO_L_PU, BIT(29)),
	PINCTRL_CONF_DESC(43, REG_GPIO_L_PU, BIT(30)),
	PINCTRL_CONF_DESC(44, REG_GPIO_L_PU, BIT(31)),
	PINCTRL_CONF_DESC(45, REG_GPIO_H_PU, BIT(0)),
	PINCTRL_CONF_DESC(46, REG_GPIO_H_PU, BIT(1)),
	PINCTRL_CONF_DESC(47, REG_GPIO_H_PU, BIT(2)),
	PINCTRL_CONF_DESC(48, REG_GPIO_H_PU, BIT(3)),
	PINCTRL_CONF_DESC(49, REG_GPIO_H_PU, BIT(4)),
	PINCTRL_CONF_DESC(50, REG_GPIO_H_PU, BIT(5)),
	PINCTRL_CONF_DESC(51, REG_GPIO_H_PU, BIT(6)),
	PINCTRL_CONF_DESC(52, REG_GPIO_H_PU, BIT(7)),
	PINCTRL_CONF_DESC(53, REG_GPIO_H_PU, BIT(8)),
	PINCTRL_CONF_DESC(54, REG_GPIO_H_PU, BIT(9)),
	PINCTRL_CONF_DESC(55, REG_GPIO_H_PU, BIT(10)),
	PINCTRL_CONF_DESC(56, REG_GPIO_H_PU, BIT(11)),
	PINCTRL_CONF_DESC(57, REG_GPIO_H_PU, BIT(12)),
	PINCTRL_CONF_DESC(58, REG_GPIO_H_PU, BIT(13)),
	PINCTRL_CONF_DESC(59, REG_GPIO_H_PU, BIT(14)),
	PINCTRL_CONF_DESC(60, REG_I2C_SDA_PU, PCIE0_RESET_PU_MASK),
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_PU, PCIE1_RESET_PU_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_PU, PCIE2_RESET_PU_MASK),
};

static const struct airoha_pinctrl_conf en7581_pinctrl_pulldown_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_PD, UART1_TXD_PD_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_PD, UART1_RXD_PD_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PD, I2C_SCL_PD_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PD, I2C_SDA_PD_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_PD, SPI_CS0_PD_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_PD, SPI_CLK_PD_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_PD, SPI_MOSI_PD_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_PD, SPI_MISO_PD_MASK),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PD, BIT(0)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PD, BIT(1)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PD, BIT(2)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PD, BIT(3)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PD, BIT(4)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PD, BIT(5)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PD, BIT(6)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PD, BIT(7)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PD, BIT(8)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PD, BIT(9)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PD, BIT(10)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PD, BIT(11)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PD, BIT(12)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PD, BIT(13)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PD, BIT(14)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PD, BIT(15)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PD, BIT(16)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PD, BIT(17)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PD, BIT(18)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PD, BIT(19)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PD, BIT(20)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_PD, BIT(21)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_PD, BIT(22)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_PD, BIT(23)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_PD, BIT(24)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_PD, BIT(25)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_PD, BIT(26)),
	PINCTRL_CONF_DESC(40, REG_GPIO_L_PD, BIT(27)),
	PINCTRL_CONF_DESC(41, REG_GPIO_L_PD, BIT(28)),
	PINCTRL_CONF_DESC(42, REG_GPIO_L_PD, BIT(29)),
	PINCTRL_CONF_DESC(43, REG_GPIO_L_PD, BIT(30)),
	PINCTRL_CONF_DESC(44, REG_GPIO_L_PD, BIT(31)),
	PINCTRL_CONF_DESC(45, REG_GPIO_H_PD, BIT(0)),
	PINCTRL_CONF_DESC(46, REG_GPIO_H_PD, BIT(1)),
	PINCTRL_CONF_DESC(47, REG_GPIO_H_PD, BIT(2)),
	PINCTRL_CONF_DESC(48, REG_GPIO_H_PD, BIT(3)),
	PINCTRL_CONF_DESC(49, REG_GPIO_H_PD, BIT(4)),
	PINCTRL_CONF_DESC(50, REG_GPIO_H_PD, BIT(5)),
	PINCTRL_CONF_DESC(51, REG_GPIO_H_PD, BIT(6)),
	PINCTRL_CONF_DESC(52, REG_GPIO_H_PD, BIT(7)),
	PINCTRL_CONF_DESC(53, REG_GPIO_H_PD, BIT(8)),
	PINCTRL_CONF_DESC(54, REG_GPIO_H_PD, BIT(9)),
	PINCTRL_CONF_DESC(55, REG_GPIO_H_PD, BIT(10)),
	PINCTRL_CONF_DESC(56, REG_GPIO_H_PD, BIT(11)),
	PINCTRL_CONF_DESC(57, REG_GPIO_H_PD, BIT(12)),
	PINCTRL_CONF_DESC(58, REG_GPIO_H_PD, BIT(13)),
	PINCTRL_CONF_DESC(59, REG_GPIO_H_PD, BIT(14)),
	PINCTRL_CONF_DESC(60, REG_I2C_SDA_PD, PCIE0_RESET_PD_MASK),
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_PD, PCIE1_RESET_PD_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_PD, PCIE2_RESET_PD_MASK),
};

static const struct airoha_pinctrl_conf en7581_pinctrl_drive_e2_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_E2, UART1_TXD_E2_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_E2, UART1_RXD_E2_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E2, I2C_SCL_E2_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E2, I2C_SDA_E2_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_E2, SPI_CS0_E2_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_E2, SPI_CLK_E2_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_E2, SPI_MOSI_E2_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_E2, SPI_MISO_E2_MASK),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E2, BIT(0)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E2, BIT(1)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E2, BIT(2)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E2, BIT(3)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E2, BIT(4)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E2, BIT(5)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E2, BIT(6)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E2, BIT(7)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E2, BIT(8)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E2, BIT(9)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E2, BIT(10)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E2, BIT(11)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E2, BIT(12)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E2, BIT(13)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E2, BIT(14)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E2, BIT(15)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E2, BIT(16)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E2, BIT(17)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E2, BIT(18)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E2, BIT(19)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E2, BIT(20)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_E2, BIT(21)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_E2, BIT(22)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_E2, BIT(23)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_E2, BIT(24)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_E2, BIT(25)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_E2, BIT(26)),
	PINCTRL_CONF_DESC(40, REG_GPIO_L_E2, BIT(27)),
	PINCTRL_CONF_DESC(41, REG_GPIO_L_E2, BIT(28)),
	PINCTRL_CONF_DESC(42, REG_GPIO_L_E2, BIT(29)),
	PINCTRL_CONF_DESC(43, REG_GPIO_L_E2, BIT(30)),
	PINCTRL_CONF_DESC(44, REG_GPIO_L_E2, BIT(31)),
	PINCTRL_CONF_DESC(45, REG_GPIO_H_E2, BIT(0)),
	PINCTRL_CONF_DESC(46, REG_GPIO_H_E2, BIT(1)),
	PINCTRL_CONF_DESC(47, REG_GPIO_H_E2, BIT(2)),
	PINCTRL_CONF_DESC(48, REG_GPIO_H_E2, BIT(3)),
	PINCTRL_CONF_DESC(49, REG_GPIO_H_E2, BIT(4)),
	PINCTRL_CONF_DESC(50, REG_GPIO_H_E2, BIT(5)),
	PINCTRL_CONF_DESC(51, REG_GPIO_H_E2, BIT(6)),
	PINCTRL_CONF_DESC(52, REG_GPIO_H_E2, BIT(7)),
	PINCTRL_CONF_DESC(53, REG_GPIO_H_E2, BIT(8)),
	PINCTRL_CONF_DESC(54, REG_GPIO_H_E2, BIT(9)),
	PINCTRL_CONF_DESC(55, REG_GPIO_H_E2, BIT(10)),
	PINCTRL_CONF_DESC(56, REG_GPIO_H_E2, BIT(11)),
	PINCTRL_CONF_DESC(57, REG_GPIO_H_E2, BIT(12)),
	PINCTRL_CONF_DESC(58, REG_GPIO_H_E2, BIT(13)),
	PINCTRL_CONF_DESC(59, REG_GPIO_H_E2, BIT(14)),
	PINCTRL_CONF_DESC(60, REG_I2C_SDA_E2, PCIE0_RESET_E2_MASK),
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_E2, PCIE1_RESET_E2_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_E2, PCIE2_RESET_E2_MASK),
};

static const struct airoha_pinctrl_conf en7581_pinctrl_drive_e4_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_E4, UART1_TXD_E4_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_E4, UART1_RXD_E4_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E4, I2C_SCL_E4_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E4, I2C_SDA_E4_MASK),
	PINCTRL_CONF_DESC(4, REG_I2C_SDA_E4, SPI_CS0_E4_MASK),
	PINCTRL_CONF_DESC(5, REG_I2C_SDA_E4, SPI_CLK_E4_MASK),
	PINCTRL_CONF_DESC(6, REG_I2C_SDA_E4, SPI_MOSI_E4_MASK),
	PINCTRL_CONF_DESC(7, REG_I2C_SDA_E4, SPI_MISO_E4_MASK),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E4, BIT(0)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E4, BIT(1)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E4, BIT(2)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E4, BIT(3)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E4, BIT(4)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E4, BIT(5)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E4, BIT(6)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E4, BIT(7)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E4, BIT(8)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E4, BIT(9)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E4, BIT(10)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E4, BIT(11)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E4, BIT(12)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E4, BIT(13)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E4, BIT(14)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E4, BIT(15)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E4, BIT(16)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E4, BIT(17)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E4, BIT(18)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E4, BIT(19)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E4, BIT(20)),
	PINCTRL_CONF_DESC(34, REG_GPIO_L_E4, BIT(21)),
	PINCTRL_CONF_DESC(35, REG_GPIO_L_E4, BIT(22)),
	PINCTRL_CONF_DESC(36, REG_GPIO_L_E4, BIT(23)),
	PINCTRL_CONF_DESC(37, REG_GPIO_L_E4, BIT(24)),
	PINCTRL_CONF_DESC(38, REG_GPIO_L_E4, BIT(25)),
	PINCTRL_CONF_DESC(39, REG_GPIO_L_E4, BIT(26)),
	PINCTRL_CONF_DESC(40, REG_GPIO_L_E4, BIT(27)),
	PINCTRL_CONF_DESC(41, REG_GPIO_L_E4, BIT(28)),
	PINCTRL_CONF_DESC(42, REG_GPIO_L_E4, BIT(29)),
	PINCTRL_CONF_DESC(43, REG_GPIO_L_E4, BIT(30)),
	PINCTRL_CONF_DESC(44, REG_GPIO_L_E4, BIT(31)),
	PINCTRL_CONF_DESC(45, REG_GPIO_H_E4, BIT(0)),
	PINCTRL_CONF_DESC(46, REG_GPIO_H_E4, BIT(1)),
	PINCTRL_CONF_DESC(47, REG_GPIO_H_E4, BIT(2)),
	PINCTRL_CONF_DESC(48, REG_GPIO_H_E4, BIT(3)),
	PINCTRL_CONF_DESC(49, REG_GPIO_H_E4, BIT(4)),
	PINCTRL_CONF_DESC(50, REG_GPIO_H_E4, BIT(5)),
	PINCTRL_CONF_DESC(51, REG_GPIO_H_E4, BIT(6)),
	PINCTRL_CONF_DESC(52, REG_GPIO_H_E4, BIT(7)),
	PINCTRL_CONF_DESC(53, REG_GPIO_H_E4, BIT(8)),
	PINCTRL_CONF_DESC(54, REG_GPIO_H_E4, BIT(9)),
	PINCTRL_CONF_DESC(55, REG_GPIO_H_E4, BIT(10)),
	PINCTRL_CONF_DESC(56, REG_GPIO_H_E4, BIT(11)),
	PINCTRL_CONF_DESC(57, REG_GPIO_H_E4, BIT(12)),
	PINCTRL_CONF_DESC(58, REG_GPIO_H_E4, BIT(13)),
	PINCTRL_CONF_DESC(59, REG_GPIO_H_E4, BIT(14)),
	PINCTRL_CONF_DESC(60, REG_I2C_SDA_E4, PCIE0_RESET_E4_MASK),
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_E4, PCIE1_RESET_E4_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_E4, PCIE2_RESET_E4_MASK),
};

static const struct airoha_pinctrl_conf en7581_pinctrl_pcie_rst_od_conf[] = {
	PINCTRL_CONF_DESC(60, REG_PCIE_RESET_OD, PCIE0_RESET_OD_MASK),
	PINCTRL_CONF_DESC(61, REG_PCIE_RESET_OD, PCIE1_RESET_OD_MASK),
	PINCTRL_CONF_DESC(62, REG_PCIE_RESET_OD, PCIE2_RESET_OD_MASK),
};

static const struct airoha_pinctrl_match_data en7581_pinctrl_match_data = {
	.pinctrl_name = KBUILD_MODNAME,
	.pinctrl_owner = THIS_MODULE,
	.pins = en7581_pinctrl_pins,
	.num_pins = ARRAY_SIZE(en7581_pinctrl_pins),
	.grps = en7581_pinctrl_groups,
	.num_grps = ARRAY_SIZE(en7581_pinctrl_groups),
	.funcs = en7581_pinctrl_funcs,
	.num_funcs = ARRAY_SIZE(en7581_pinctrl_funcs),
	.confs_info = {
		[AIROHA_PINCTRL_CONFS_PULLUP] = {
			.confs = en7581_pinctrl_pullup_conf,
			.num_confs = ARRAY_SIZE(en7581_pinctrl_pullup_conf),
		},
		[AIROHA_PINCTRL_CONFS_PULLDOWN] = {
			.confs = en7581_pinctrl_pulldown_conf,
			.num_confs = ARRAY_SIZE(en7581_pinctrl_pulldown_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E2] = {
			.confs = en7581_pinctrl_drive_e2_conf,
			.num_confs = ARRAY_SIZE(en7581_pinctrl_drive_e2_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E4] = {
			.confs = en7581_pinctrl_drive_e4_conf,
			.num_confs = ARRAY_SIZE(en7581_pinctrl_drive_e4_conf),
		},
		[AIROHA_PINCTRL_CONFS_PCIE_RST_OD] = {
			.confs = en7581_pinctrl_pcie_rst_od_conf,
			.num_confs = ARRAY_SIZE(en7581_pinctrl_pcie_rst_od_conf),
		},
	},
};

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "airoha,en7581-pinctrl", .data = &en7581_pinctrl_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha-an7581",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_DESCRIPTION("Pinctrl driver for Airoha AN7581 SoC");
