// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 * Author: Markus Gothe <markus.gothe@genexis.eu>
 */

#include "airoha-common.h"

/* MUX */
#define REG_GPIO_2ND_I2C_MODE			0x0214
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

#define REG_GPIO_SPI_CS1_MODE			0x0218
#define GPIO_MDC_IO_MASTER_MODE_MASK		BIT(22)
#define GPIO_PCM_SPI_CS4_MODE_MASK		BIT(21)
#define GPIO_PCM_SPI_CS3_MODE_MASK		BIT(20)
#define AN7583_GPIO_PCM_SPI_CS2_MODE_MASK	BIT(18)
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
#define AN7583_MDIO_0_GPIO_MODE_MASK		BIT(26)
#define AN7583_MDC_0_GPIO_MODE_MASK		BIT(25)
#define AN7583_UART_RXD_GPIO_MODE_MASK		BIT(24)
#define AN7583_UART_TXD_GPIO_MODE_MASK		BIT(23)
#define AN7583_SPI_MISO_GPIO_MODE_MASK		BIT(22)
#define AN7583_SPI_MOSI_GPIO_MODE_MASK		BIT(21)
#define AN7583_SPI_CS_GPIO_MODE_MASK		BIT(20)
#define AN7583_SPI_CLK_GPIO_MODE_MASK		BIT(19)
#define AN7583_I2C1_SDA_GPIO_MODE_MASK		BIT(18)
#define AN7583_I2C1_SCL_GPIO_MODE_MASK		BIT(17)
#define AN7583_I2C0_SDA_GPIO_MODE_MASK		BIT(16)
#define AN7583_I2C0_SCL_GPIO_MODE_MASK		BIT(15)
#define GPIO_PARALLEL_NAND_MODE_MASK		BIT(14)
#define GPIO_SGMII_MDIO_MODE_MASK		BIT(13)
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
#define AN7583_I2C1_SCL_E2_MASK			BIT(16)
#define AN7583_I2C1_SDA_E2_MASK			BIT(15)
#define SPI_MISO_E2_MASK			BIT(14)
#define SPI_MOSI_E2_MASK			BIT(13)
#define SPI_CLK_E2_MASK				BIT(12)
#define SPI_CS0_E2_MASK				BIT(11)
#define PCIE1_RESET_E2_MASK			BIT(9)
#define PCIE0_RESET_E2_MASK			BIT(8)
#define AN7583_MDIO_0_E2_MASK			BIT(5)
#define AN7583_MDC_0_E2_MASK			BIT(4)
#define UART1_RXD_E2_MASK			BIT(3)
#define UART1_TXD_E2_MASK			BIT(2)
#define I2C_SCL_E2_MASK				BIT(1)
#define I2C_SDA_E2_MASK				BIT(0)

#define REG_I2C_SDA_E4				0x0020
#define AN7583_I2C1_SCL_E4_MASK			BIT(16)
#define AN7583_I2C1_SDA_E4_MASK			BIT(15)
#define SPI_MISO_E4_MASK			BIT(14)
#define SPI_MOSI_E4_MASK			BIT(13)
#define SPI_CLK_E4_MASK				BIT(12)
#define SPI_CS0_E4_MASK				BIT(11)
#define PCIE1_RESET_E4_MASK			BIT(9)
#define PCIE0_RESET_E4_MASK			BIT(8)
#define AN7583_MDIO_0_E4_MASK			BIT(5)
#define AN7583_MDC_0_E4_MASK			BIT(4)
#define UART1_RXD_E4_MASK			BIT(3)
#define UART1_TXD_E4_MASK			BIT(2)
#define I2C_SCL_E4_MASK				BIT(1)
#define I2C_SDA_E4_MASK				BIT(0)

#define REG_GPIO_L_E2				0x0024
#define REG_GPIO_L_E4				0x0028
#define REG_GPIO_H_E2				0x002c
#define REG_GPIO_H_E4				0x0030

#define REG_I2C_SDA_PU				0x0044
#define AN7583_I2C1_SCL_PU_MASK			BIT(16)
#define AN7583_I2C1_SDA_PU_MASK			BIT(15)
#define SPI_MISO_PU_MASK			BIT(14)
#define SPI_MOSI_PU_MASK			BIT(13)
#define SPI_CLK_PU_MASK				BIT(12)
#define SPI_CS0_PU_MASK				BIT(11)
#define PCIE1_RESET_PU_MASK			BIT(9)
#define PCIE0_RESET_PU_MASK			BIT(8)
#define AN7583_MDIO_0_PU_MASK			BIT(5)
#define AN7583_MDC_0_PU_MASK			BIT(4)
#define UART1_RXD_PU_MASK			BIT(3)
#define UART1_TXD_PU_MASK			BIT(2)
#define I2C_SCL_PU_MASK				BIT(1)
#define I2C_SDA_PU_MASK				BIT(0)

#define REG_I2C_SDA_PD				0x0048
#define AN7583_I2C1_SCL_PD_MASK			BIT(16)
#define AN7583_I2C1_SDA_PD_MASK			BIT(15)
#define SPI_MISO_PD_MASK			BIT(14)
#define SPI_MOSI_PD_MASK			BIT(13)
#define SPI_CLK_PD_MASK				BIT(12)
#define SPI_CS0_PD_MASK				BIT(11)
#define PCIE1_RESET_PD_MASK			BIT(9)
#define PCIE0_RESET_PD_MASK			BIT(8)
#define AN7583_MDIO_0_PD_MASK			BIT(5)
#define AN7583_MDC_0_PD_MASK			BIT(4)
#define UART1_RXD_PD_MASK			BIT(3)
#define UART1_TXD_PD_MASK			BIT(2)
#define I2C_SCL_PD_MASK				BIT(1)
#define I2C_SDA_PD_MASK				BIT(0)

#define REG_GPIO_L_PU				0x004c
#define REG_GPIO_L_PD				0x0050
#define REG_GPIO_H_PU				0x0054
#define REG_GPIO_H_PD				0x0058

#define REG_PCIE_RESET_OD			0x018c
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

static struct pinctrl_pin_desc an7583_pinctrl_pins[] = {
	PINCTRL_PIN(2, "gpio0"),
	PINCTRL_PIN(3, "gpio1"),
	PINCTRL_PIN(4, "gpio2"),
	PINCTRL_PIN(5, "gpio3"),
	PINCTRL_PIN(6, "gpio4"),
	PINCTRL_PIN(7, "gpio5"),
	PINCTRL_PIN(8, "gpio6"),
	PINCTRL_PIN(9, "gpio7"),
	PINCTRL_PIN(10, "gpio8"),
	PINCTRL_PIN(11, "gpio9"),
	PINCTRL_PIN(12, "gpio10"),
	PINCTRL_PIN(13, "gpio11"),
	PINCTRL_PIN(14, "gpio12"),
	PINCTRL_PIN(15, "gpio13"),
	PINCTRL_PIN(16, "gpio14"),
	PINCTRL_PIN(17, "gpio15"),
	PINCTRL_PIN(18, "gpio16"),
	PINCTRL_PIN(19, "gpio17"),
	PINCTRL_PIN(20, "gpio18"),
	PINCTRL_PIN(21, "gpio19"),
	PINCTRL_PIN(22, "gpio20"),
	PINCTRL_PIN(23, "gpio21"),
	PINCTRL_PIN(24, "gpio22"),
	PINCTRL_PIN(25, "gpio23"),
	PINCTRL_PIN(26, "gpio24"),
	PINCTRL_PIN(27, "gpio25"),
	PINCTRL_PIN(28, "gpio26"),
	PINCTRL_PIN(29, "gpio27"),
	PINCTRL_PIN(30, "gpio28"),
	PINCTRL_PIN(31, "gpio29"),
	PINCTRL_PIN(32, "gpio30"),
	PINCTRL_PIN(33, "gpio31"),
	PINCTRL_PIN(34, "gpio32"),
	PINCTRL_PIN(35, "gpio33"),
	PINCTRL_PIN(36, "gpio34"),
	PINCTRL_PIN(37, "gpio35"),
	PINCTRL_PIN(38, "gpio36"),
	PINCTRL_PIN(39, "gpio37"),
	PINCTRL_PIN(40, "gpio38"),
	PINCTRL_PIN(41, "i2c0_scl"),
	PINCTRL_PIN(42, "i2c0_sda"),
	PINCTRL_PIN(43, "i2c1_scl"),
	PINCTRL_PIN(44, "i2c1_sda"),
	PINCTRL_PIN(45, "spi_clk"),
	PINCTRL_PIN(46, "spi_cs"),
	PINCTRL_PIN(47, "spi_mosi"),
	PINCTRL_PIN(48, "spi_miso"),
	PINCTRL_PIN(49, "uart_txd"),
	PINCTRL_PIN(50, "uart_rxd"),
	PINCTRL_PIN(51, "pcie_reset0"),
	PINCTRL_PIN(52, "pcie_reset1"),
	PINCTRL_PIN(53, "mdc_0"),
	PINCTRL_PIN(54, "mdio_0"),
};

static const int an7583_pon_pins[] = { 15, 16, 17, 18, 19, 20 };
static const int an7583_pon_tod_1pps_pins[] = { 32 };
static const int an7583_gsw_tod_1pps_pins[] = { 32 };
static const int an7583_sipo_pins[] = { 34, 35 };
static const int an7583_sipo_rclk_pins[] = { 34, 35, 33 };
static const int an7583_mdio_pins[] = { 53, 54 };
static const int an7583_mdio1_pins[] = { 43, 44 };
static const int an7583_uart2_pins[] = { 34, 35 };
static const int an7583_uart2_cts_rts_pins[] = { 32, 33 };
static const int an7583_hsuart_pins[] = { 30, 31 };
static const int an7583_hsuart_cts_rts_pins[] = { 28, 29 };
static const int an7583_npu_uart_pins[] = { 7, 8 };
static const int an7583_uart4_pins[] = { 7, 8 };
static const int an7583_uart5_pins[] = { 23, 24 };
static const int an7583_i2c0_pins[] = { 41, 42 };
static const int an7583_i2c1_pins[] = { 43, 44 };
static const int an7583_jtag_udi_pins[] = { 23, 24, 22, 25, 26 };
static const int an7583_jtag_dfd_pins[] = { 23, 24, 22, 25, 26 };
static const int an7583_pcm1_pins[] = { 10, 11, 12, 13, 14 };
static const int an7583_pcm2_pins[] = { 28, 29, 30, 31, 24 };
static const int an7583_spi_pins[] = { 45, 46, 47, 48 };
static const int an7583_spi_quad_pins[] = { 25, 26 };
static const int an7583_spi_cs1_pins[] = { 27 };
static const int an7583_pcm_spi_pins[] = { 28, 29, 30, 31, 10, 11, 12, 13 };
static const int an7583_pcm_spi_rst_pins[] = { 14 };
static const int an7583_pcm_spi_cs1_pins[] = { 24 };
static const int an7583_emmc_pins[] = {
	7, 8, 9, 22, 23, 24, 25, 26, 45, 46, 47
};
static const int an7583_pnand_pins[] = {
	7, 8, 9, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 45, 46, 47, 48
};
static const int an7583_gpio0_pins[] = { 2 };
static const int an7583_gpio1_pins[] = { 3 };
static const int an7583_gpio2_pins[] = { 4 };
static const int an7583_gpio3_pins[] = { 5 };
static const int an7583_gpio4_pins[] = { 6 };
static const int an7583_gpio5_pins[] = { 7 };
static const int an7583_gpio6_pins[] = { 8 };
static const int an7583_gpio7_pins[] = { 9 };
static const int an7583_gpio8_pins[] = { 10 };
static const int an7583_gpio9_pins[] = { 11 };
static const int an7583_gpio10_pins[] = { 12 };
static const int an7583_gpio11_pins[] = { 13 };
static const int an7583_gpio12_pins[] = { 14 };
static const int an7583_gpio13_pins[] = { 15 };
static const int an7583_gpio14_pins[] = { 16 };
static const int an7583_gpio15_pins[] = { 17 };
static const int an7583_gpio16_pins[] = { 18 };
static const int an7583_gpio17_pins[] = { 19 };
static const int an7583_gpio18_pins[] = { 20 };
static const int an7583_gpio19_pins[] = { 21 };
static const int an7583_gpio20_pins[] = { 22 };
static const int an7583_gpio21_pins[] = { 23 };
static const int an7583_gpio22_pins[] = { 24 };
static const int an7583_gpio23_pins[] = { 25 };
static const int an7583_gpio24_pins[] = { 26 };
static const int an7583_gpio25_pins[] = { 27 };
static const int an7583_gpio26_pins[] = { 28 };
static const int an7583_gpio27_pins[] = { 29 };
static const int an7583_gpio28_pins[] = { 30 };
static const int an7583_gpio29_pins[] = { 31 };
static const int an7583_gpio30_pins[] = { 32 };
static const int an7583_gpio31_pins[] = { 33 };
static const int an7583_gpio32_pins[] = { 34 };
static const int an7583_gpio33_pins[] = { 35 };
static const int an7583_gpio34_pins[] = { 36 };
static const int an7583_gpio35_pins[] = { 37 };
static const int an7583_gpio36_pins[] = { 38 };
static const int an7583_gpio37_pins[] = { 39 };
static const int an7583_gpio38_pins[] = { 40 };
static const int an7583_gpio39_pins[] = { 41 };
static const int an7583_gpio40_pins[] = { 42 };
static const int an7583_gpio41_pins[] = { 43 };
static const int an7583_gpio42_pins[] = { 44 };
static const int an7583_gpio43_pins[] = { 45 };
static const int an7583_gpio44_pins[] = { 46 };
static const int an7583_gpio45_pins[] = { 47 };
static const int an7583_gpio46_pins[] = { 48 };
static const int an7583_gpio47_pins[] = { 49 };
static const int an7583_gpio48_pins[] = { 50 };
static const int an7583_gpio49_pins[] = { 51 };
static const int an7583_gpio50_pins[] = { 52 };
static const int an7583_gpio51_pins[] = { 53 };
static const int an7583_gpio52_pins[] = { 54 };
static const int an7583_pcie_reset0_pins[] = { 51 };
static const int an7583_pcie_reset1_pins[] = { 52 };

static const struct pingroup an7583_pinctrl_groups[] = {
	PINCTRL_PIN_GROUP("pon", an7583_pon),
	PINCTRL_PIN_GROUP("pon_tod_1pps", an7583_pon_tod_1pps),
	PINCTRL_PIN_GROUP("gsw_tod_1pps", an7583_gsw_tod_1pps),
	PINCTRL_PIN_GROUP("sipo", an7583_sipo),
	PINCTRL_PIN_GROUP("sipo_rclk", an7583_sipo_rclk),
	PINCTRL_PIN_GROUP("mdio", an7583_mdio),
	PINCTRL_PIN_GROUP("mdio1", an7583_mdio1),
	PINCTRL_PIN_GROUP("uart2", an7583_uart2),
	PINCTRL_PIN_GROUP("uart2_cts_rts", an7583_uart2_cts_rts),
	PINCTRL_PIN_GROUP("hsuart", an7583_hsuart),
	PINCTRL_PIN_GROUP("hsuart_cts_rts", an7583_hsuart_cts_rts),
	PINCTRL_PIN_GROUP("npu_uart", an7583_npu_uart),
	PINCTRL_PIN_GROUP("uart4", an7583_uart4),
	PINCTRL_PIN_GROUP("uart5", an7583_uart5),
	PINCTRL_PIN_GROUP("i2c0", an7583_i2c0),
	PINCTRL_PIN_GROUP("i2c1", an7583_i2c1),
	PINCTRL_PIN_GROUP("jtag_udi", an7583_jtag_udi),
	PINCTRL_PIN_GROUP("jtag_dfd", an7583_jtag_dfd),
	PINCTRL_PIN_GROUP("pcm1", an7583_pcm1),
	PINCTRL_PIN_GROUP("pcm2", an7583_pcm2),
	PINCTRL_PIN_GROUP("spi", an7583_spi),
	PINCTRL_PIN_GROUP("spi_quad", an7583_spi_quad),
	PINCTRL_PIN_GROUP("spi_cs1", an7583_spi_cs1),
	PINCTRL_PIN_GROUP("pcm_spi", an7583_pcm_spi),
	PINCTRL_PIN_GROUP("pcm_spi_rst", an7583_pcm_spi_rst),
	PINCTRL_PIN_GROUP("pcm_spi_cs1", an7583_pcm_spi_cs1),
	PINCTRL_PIN_GROUP("emmc", an7583_emmc),
	PINCTRL_PIN_GROUP("pnand", an7583_pnand),
	PINCTRL_PIN_GROUP("gpio0", an7583_gpio0),
	PINCTRL_PIN_GROUP("gpio1", an7583_gpio1),
	PINCTRL_PIN_GROUP("gpio2", an7583_gpio2),
	PINCTRL_PIN_GROUP("gpio3", an7583_gpio3),
	PINCTRL_PIN_GROUP("gpio4", an7583_gpio4),
	PINCTRL_PIN_GROUP("gpio5", an7583_gpio5),
	PINCTRL_PIN_GROUP("gpio6", an7583_gpio6),
	PINCTRL_PIN_GROUP("gpio7", an7583_gpio7),
	PINCTRL_PIN_GROUP("gpio8", an7583_gpio8),
	PINCTRL_PIN_GROUP("gpio9", an7583_gpio9),
	PINCTRL_PIN_GROUP("gpio10", an7583_gpio10),
	PINCTRL_PIN_GROUP("gpio11", an7583_gpio11),
	PINCTRL_PIN_GROUP("gpio12", an7583_gpio12),
	PINCTRL_PIN_GROUP("gpio13", an7583_gpio13),
	PINCTRL_PIN_GROUP("gpio14", an7583_gpio14),
	PINCTRL_PIN_GROUP("gpio15", an7583_gpio15),
	PINCTRL_PIN_GROUP("gpio16", an7583_gpio16),
	PINCTRL_PIN_GROUP("gpio17", an7583_gpio17),
	PINCTRL_PIN_GROUP("gpio18", an7583_gpio18),
	PINCTRL_PIN_GROUP("gpio19", an7583_gpio19),
	PINCTRL_PIN_GROUP("gpio20", an7583_gpio20),
	PINCTRL_PIN_GROUP("gpio21", an7583_gpio21),
	PINCTRL_PIN_GROUP("gpio22", an7583_gpio22),
	PINCTRL_PIN_GROUP("gpio23", an7583_gpio23),
	PINCTRL_PIN_GROUP("gpio24", an7583_gpio24),
	PINCTRL_PIN_GROUP("gpio25", an7583_gpio25),
	PINCTRL_PIN_GROUP("gpio26", an7583_gpio26),
	PINCTRL_PIN_GROUP("gpio27", an7583_gpio27),
	PINCTRL_PIN_GROUP("gpio28", an7583_gpio28),
	PINCTRL_PIN_GROUP("gpio29", an7583_gpio29),
	PINCTRL_PIN_GROUP("gpio30", an7583_gpio30),
	PINCTRL_PIN_GROUP("gpio31", an7583_gpio31),
	PINCTRL_PIN_GROUP("gpio32", an7583_gpio32),
	PINCTRL_PIN_GROUP("gpio33", an7583_gpio33),
	PINCTRL_PIN_GROUP("gpio34", an7583_gpio34),
	PINCTRL_PIN_GROUP("gpio35", an7583_gpio35),
	PINCTRL_PIN_GROUP("gpio36", an7583_gpio36),
	PINCTRL_PIN_GROUP("gpio37", an7583_gpio37),
	PINCTRL_PIN_GROUP("gpio38", an7583_gpio38),
	PINCTRL_PIN_GROUP("gpio39", an7583_gpio39),
	PINCTRL_PIN_GROUP("gpio40", an7583_gpio40),
	PINCTRL_PIN_GROUP("gpio41", an7583_gpio41),
	PINCTRL_PIN_GROUP("gpio42", an7583_gpio42),
	PINCTRL_PIN_GROUP("gpio43", an7583_gpio43),
	PINCTRL_PIN_GROUP("gpio44", an7583_gpio44),
	PINCTRL_PIN_GROUP("gpio45", an7583_gpio45),
	PINCTRL_PIN_GROUP("gpio46", an7583_gpio46),
	PINCTRL_PIN_GROUP("gpio47", an7583_gpio47),
	PINCTRL_PIN_GROUP("gpio48", an7583_gpio48),
	PINCTRL_PIN_GROUP("gpio49", an7583_gpio49),
	PINCTRL_PIN_GROUP("gpio50", an7583_gpio50),
	PINCTRL_PIN_GROUP("gpio51", an7583_gpio51),
	PINCTRL_PIN_GROUP("gpio52", an7583_gpio52),
	PINCTRL_PIN_GROUP("pcie_reset0", an7583_pcie_reset0),
	PINCTRL_PIN_GROUP("pcie_reset1", an7583_pcie_reset1),
};

static const char *const pon_groups[] = { "pon" };
static const char *const tod_1pps_groups[] = {
	"pon_tod_1pps", "gsw_tod_1pps"
};
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const an7583_mdio_groups[] = { "mdio" };
static const char *const uart_groups[] = {
	"uart2", "uart2_cts_rts", "hsuart", "hsuart_cts_rts",
	"uart4", "uart5"
};
static const char *const an7583_i2c_groups[] = { "i2c0", "i2c1" };
static const char *const jtag_groups[] = { "jtag_udi", "jtag_dfd" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const an7583_pcm_spi_groups[] = {
	"pcm_spi", "pcm_spi_rst", "pcm_spi_cs1"
};
static const char *const emmc_groups[] = { "emmc" };
static const char *const pnand_groups[] = { "pnand" };
static const char *const an7583_gpio_groups[] = {
	"gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44", "gpio45", "gpio46", "gpio47", "gpio48",
	"gpio49", "gpio50", "gpio51", "gpio52"
};
static const char *const an7583_pcie_reset_groups[] = {
	"pcie_reset0", "pcie_reset1"
};
static const char *const an7583_pwm_groups[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",  "gpio4",  "gpio5",
	"gpio6",  "gpio7",  "gpio8",  "gpio9",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15", "gpio16", "gpio17",
	"gpio18", "gpio19", "gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio36", "gpio37", "gpio38", "gpio39",
	"gpio40", "gpio41", "gpio42", "gpio43", "gpio44", "gpio45",
	"gpio46", "gpio47", "gpio48", "gpio49", "gpio50", "gpio51"
};
static const char *const an7583_phy1_led0_groups[] = {
	"gpio1", "gpio2", "gpio3", "gpio4"
};
static const char *const an7583_phy2_led0_groups[] = {
	"gpio1", "gpio2", "gpio3", "gpio4"
};
static const char *const an7583_phy3_led0_groups[] = {
	"gpio1", "gpio2", "gpio3", "gpio4"
};
static const char *const an7583_phy4_led0_groups[] = {
	"gpio1", "gpio2", "gpio3", "gpio4"
};
static const char *const an7583_phy1_led1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const an7583_phy2_led1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const an7583_phy3_led1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
};
static const char *const an7583_phy4_led1_groups[] = {
	"gpio8", "gpio9", "gpio10", "gpio11"
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

static const struct airoha_pinctrl_func_group an7583_mdio_func_group[] = {
	{
		.name = "mdio",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			AN7583_MDC_0_GPIO_MODE_MASK | AN7583_MDIO_0_GPIO_MODE_MASK,
			0
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

static const struct airoha_pinctrl_func_group an7583_i2c_func_group[] = {
	{
		.name = "i2c0",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			AN7583_I2C0_SCL_GPIO_MODE_MASK | AN7583_I2C0_SDA_GPIO_MODE_MASK,
			0
		},
		.regmap_size = 1,
	}, {
		.name = "i2c1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			AN7583_I2C1_SCL_GPIO_MODE_MASK | AN7583_I2C1_SDA_GPIO_MODE_MASK,
			0
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

static const struct airoha_pinctrl_func_group an7583_pcm_spi_func_group[] = {
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
		.name = "pcm_spi_cs2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_SPI_CS1_MODE,
			AN7583_GPIO_PCM_SPI_CS2_MODE_MASK,
			AN7583_GPIO_PCM_SPI_CS2_MODE_MASK
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

static const struct airoha_pinctrl_func_group an7583_gpio_func_group[] = {
	AIROHA_PINCTRL_GPIO_EXT("gpio39", GPIO39_FLASH_MODE_CFG,
				AN7583_I2C0_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio40", GPIO40_FLASH_MODE_CFG,
				AN7583_I2C0_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio41", GPIO41_FLASH_MODE_CFG,
				AN7583_I2C1_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio42", GPIO42_FLASH_MODE_CFG,
				AN7583_I2C1_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio43", GPIO43_FLASH_MODE_CFG,
				AN7583_SPI_CLK_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio44", GPIO44_FLASH_MODE_CFG,
				AN7583_SPI_CS_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio45", GPIO45_FLASH_MODE_CFG,
				AN7583_SPI_MOSI_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio46", GPIO46_FLASH_MODE_CFG,
				AN7583_SPI_MISO_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio47", GPIO47_FLASH_MODE_CFG,
				AN7583_UART_TXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio48", GPIO48_FLASH_MODE_CFG,
				AN7583_UART_RXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio49", GPIO49_FLASH_MODE_CFG,
				GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio50", GPIO50_FLASH_MODE_CFG,
				GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_GPIO_EXT("gpio51", GPIO51_FLASH_MODE_CFG,
				AN7583_MDC_0_GPIO_MODE_MASK),
	AIROHA_PINCTRL_GPIO("gpio52", AN7583_MDIO_0_GPIO_MODE_MASK),
};

static const struct airoha_pinctrl_func_group an7583_pcie_reset_func_group[] = {
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

static const struct airoha_pinctrl_func_group an7583_pwm_func_group[] = {
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
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio39", GPIO39_FLASH_MODE_CFG,
				   AN7583_I2C0_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio40", GPIO40_FLASH_MODE_CFG,
				   AN7583_I2C0_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio41", GPIO41_FLASH_MODE_CFG,
				   AN7583_I2C1_SCL_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio42", GPIO42_FLASH_MODE_CFG,
				   AN7583_I2C1_SDA_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio43", GPIO43_FLASH_MODE_CFG,
				   AN7583_SPI_CLK_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio44", GPIO44_FLASH_MODE_CFG,
				   AN7583_SPI_CS_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio45", GPIO45_FLASH_MODE_CFG,
				   AN7583_SPI_MOSI_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio46", GPIO46_FLASH_MODE_CFG,
				   AN7583_SPI_MISO_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio47", GPIO47_FLASH_MODE_CFG,
				   AN7583_UART_TXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio48", GPIO48_FLASH_MODE_CFG,
				   AN7583_UART_RXD_GPIO_MODE_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio49", GPIO49_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET0_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio50", GPIO50_FLASH_MODE_CFG,
				   GPIO_PCIE_RESET1_MASK),
	AIROHA_PINCTRL_PWM_EXT_SEC("gpio51", GPIO51_FLASH_MODE_CFG,
				   AN7583_MDC_0_GPIO_MODE_MASK),
};

static const struct airoha_pinctrl_func_group an7583_phy1_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio1", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio2", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio3", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED0("gpio4", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group an7583_phy2_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio1", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio2", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio3", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED0("gpio4", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group an7583_phy3_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio1", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio2", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio3", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED0("gpio4", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group an7583_phy4_led0_func_group[] = {
	AIROHA_PINCTRL_PHY_LED0("gpio1", GPIO_LAN0_LED0_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio2", GPIO_LAN1_LED0_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio3", GPIO_LAN2_LED0_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED0("gpio4", GPIO_LAN3_LED0_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func_group an7583_phy1_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio8", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio9", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio10", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(0)),
	AIROHA_PINCTRL_PHY_LED1("gpio11", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(0)),
};

static const struct airoha_pinctrl_func_group an7583_phy2_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio8", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio9", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio10", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(1)),
	AIROHA_PINCTRL_PHY_LED1("gpio11", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(1)),
};

static const struct airoha_pinctrl_func_group an7583_phy3_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio8", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio9", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio10", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(2)),
	AIROHA_PINCTRL_PHY_LED1("gpio11", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(2)),
};

static const struct airoha_pinctrl_func_group an7583_phy4_led1_func_group[] = {
	AIROHA_PINCTRL_PHY_LED1("gpio8", GPIO_LAN0_LED1_MODE_MASK,
				LAN0_LED_MAPPING_MASK, LAN0_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio9", GPIO_LAN1_LED1_MODE_MASK,
				LAN1_LED_MAPPING_MASK, LAN1_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio10", GPIO_LAN2_LED1_MODE_MASK,
				LAN2_LED_MAPPING_MASK, LAN2_PHY_LED_MAP(3)),
	AIROHA_PINCTRL_PHY_LED1("gpio11", GPIO_LAN3_LED1_MODE_MASK,
				LAN3_LED_MAPPING_MASK, LAN3_PHY_LED_MAP(3)),
};

static const struct airoha_pinctrl_func an7583_pinctrl_funcs[] = {
	PINCTRL_FUNC_DESC("pon", pon),
	PINCTRL_FUNC_DESC("tod_1pps", tod_1pps),
	PINCTRL_FUNC_DESC("sipo", sipo),
	PINCTRL_FUNC_DESC("mdio", an7583_mdio),
	PINCTRL_FUNC_DESC("uart", uart),
	PINCTRL_FUNC_DESC("i2c", an7583_i2c),
	PINCTRL_FUNC_DESC("jtag", jtag),
	PINCTRL_FUNC_DESC("pcm", pcm),
	PINCTRL_FUNC_DESC("spi", spi),
	PINCTRL_FUNC_DESC("pcm_spi", an7583_pcm_spi),
	PINCTRL_FUNC_DESC("emmc", emmc),
	PINCTRL_FUNC_DESC("pnand", pnand),
	PINCTRL_FUNC_DESC("gpio", an7583_gpio),
	PINCTRL_FUNC_DESC("pcie_reset", an7583_pcie_reset),
	PINCTRL_FUNC_DESC("pwm", an7583_pwm),
	PINCTRL_FUNC_DESC("phy1_led0", an7583_phy1_led0),
	PINCTRL_FUNC_DESC("phy2_led0", an7583_phy2_led0),
	PINCTRL_FUNC_DESC("phy3_led0", an7583_phy3_led0),
	PINCTRL_FUNC_DESC("phy4_led0", an7583_phy4_led0),
	PINCTRL_FUNC_DESC("phy1_led1", an7583_phy1_led1),
	PINCTRL_FUNC_DESC("phy2_led1", an7583_phy2_led1),
	PINCTRL_FUNC_DESC("phy3_led1", an7583_phy3_led1),
	PINCTRL_FUNC_DESC("phy4_led1", an7583_phy4_led1),
};

static const struct airoha_pinctrl_conf an7583_pinctrl_pullup_conf[] = {
	PINCTRL_CONF_DESC(2, REG_GPIO_L_PU, BIT(0)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_PU, BIT(1)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_PU, BIT(2)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_PU, BIT(3)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_PU, BIT(4)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_PU, BIT(5)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_PU, BIT(6)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_PU, BIT(7)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_PU, BIT(8)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_PU, BIT(9)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PU, BIT(10)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PU, BIT(11)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PU, BIT(12)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PU, BIT(13)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PU, BIT(14)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PU, BIT(15)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PU, BIT(16)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PU, BIT(17)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PU, BIT(18)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PU, BIT(19)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PU, BIT(20)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PU, BIT(21)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PU, BIT(22)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PU, BIT(23)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PU, BIT(24)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PU, BIT(25)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PU, BIT(26)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PU, BIT(27)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PU, BIT(28)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PU, BIT(29)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PU, BIT(30)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PU, BIT(31)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_PU, BIT(0)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_PU, BIT(1)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_PU, BIT(2)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_PU, BIT(3)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_PU, BIT(4)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_PU, BIT(5)),
	PINCTRL_CONF_DESC(40, REG_GPIO_H_PU, BIT(6)),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PU, I2C_SCL_PU_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_PU, I2C_SDA_PU_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_PU, AN7583_I2C1_SCL_PU_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_PU, AN7583_I2C1_SDA_PU_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_PU, SPI_CLK_PU_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_PU, SPI_CS0_PU_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_PU, SPI_MOSI_PU_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_PU, SPI_MISO_PU_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_PU, UART1_TXD_PU_MASK),
	PINCTRL_CONF_DESC(50, REG_I2C_SDA_PU, UART1_RXD_PU_MASK),
	PINCTRL_CONF_DESC(51, REG_I2C_SDA_PU, PCIE0_RESET_PU_MASK),
	PINCTRL_CONF_DESC(52, REG_I2C_SDA_PU, PCIE1_RESET_PU_MASK),
	PINCTRL_CONF_DESC(53, REG_I2C_SDA_PU, AN7583_MDC_0_PU_MASK),
	PINCTRL_CONF_DESC(54, REG_I2C_SDA_PU, AN7583_MDIO_0_PU_MASK),
};

static const struct airoha_pinctrl_conf an7583_pinctrl_pulldown_conf[] = {
	PINCTRL_CONF_DESC(2, REG_GPIO_L_PD, BIT(0)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_PD, BIT(1)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_PD, BIT(2)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_PD, BIT(3)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_PD, BIT(4)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_PD, BIT(5)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_PD, BIT(6)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_PD, BIT(7)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_PD, BIT(8)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_PD, BIT(9)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_PD, BIT(10)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_PD, BIT(11)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_PD, BIT(12)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_PD, BIT(13)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_PD, BIT(14)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_PD, BIT(15)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_PD, BIT(16)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_PD, BIT(17)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_PD, BIT(18)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_PD, BIT(19)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_PD, BIT(20)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_PD, BIT(21)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_PD, BIT(22)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_PD, BIT(23)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_PD, BIT(24)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_PD, BIT(25)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_PD, BIT(26)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_PD, BIT(27)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_PD, BIT(28)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_PD, BIT(29)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PD, BIT(30)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_PD, BIT(31)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_PD, BIT(0)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_PD, BIT(1)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_PD, BIT(2)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_PD, BIT(3)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_PD, BIT(4)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_PD, BIT(5)),
	PINCTRL_CONF_DESC(40, REG_GPIO_H_PD, BIT(6)),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_PD, I2C_SCL_PD_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_PD, I2C_SDA_PD_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_PD, AN7583_I2C1_SCL_PD_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_PD, AN7583_I2C1_SDA_PD_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_PD, SPI_CLK_PD_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_PD, SPI_CS0_PD_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_PD, SPI_MOSI_PD_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_PD, SPI_MISO_PD_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_PD, UART1_TXD_PD_MASK),
	PINCTRL_CONF_DESC(50, REG_I2C_SDA_PD, UART1_RXD_PD_MASK),
	PINCTRL_CONF_DESC(51, REG_I2C_SDA_PD, PCIE0_RESET_PD_MASK),
	PINCTRL_CONF_DESC(52, REG_I2C_SDA_PD, PCIE1_RESET_PD_MASK),
	PINCTRL_CONF_DESC(53, REG_I2C_SDA_PD, AN7583_MDC_0_PD_MASK),
	PINCTRL_CONF_DESC(54, REG_I2C_SDA_PD, AN7583_MDIO_0_PD_MASK),
};

static const struct airoha_pinctrl_conf an7583_pinctrl_drive_e2_conf[] = {
	PINCTRL_CONF_DESC(2, REG_GPIO_L_E2, BIT(0)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_E2, BIT(1)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_E2, BIT(2)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_E2, BIT(3)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_E2, BIT(4)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_E2, BIT(5)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_E2, BIT(6)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_E2, BIT(7)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_E2, BIT(8)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_E2, BIT(9)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E2, BIT(10)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E2, BIT(11)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E2, BIT(12)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E2, BIT(13)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E2, BIT(14)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E2, BIT(15)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E2, BIT(16)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E2, BIT(17)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E2, BIT(18)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E2, BIT(19)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E2, BIT(20)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E2, BIT(21)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E2, BIT(22)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E2, BIT(23)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E2, BIT(24)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E2, BIT(25)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E2, BIT(26)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E2, BIT(27)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E2, BIT(28)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E2, BIT(29)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E2, BIT(30)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E2, BIT(31)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_E2, BIT(0)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_E2, BIT(1)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_E2, BIT(2)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_E2, BIT(3)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_E2, BIT(4)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_E2, BIT(5)),
	PINCTRL_CONF_DESC(40, REG_GPIO_H_E2, BIT(6)),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E2, I2C_SCL_E2_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_E2, I2C_SDA_E2_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_E2, AN7583_I2C1_SCL_E2_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_E2, AN7583_I2C1_SDA_E2_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_E2, SPI_CLK_E2_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_E2, SPI_CS0_E2_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_E2, SPI_MOSI_E2_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_E2, SPI_MISO_E2_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_E2, UART1_TXD_E2_MASK),
	PINCTRL_CONF_DESC(50, REG_I2C_SDA_E2, UART1_RXD_E2_MASK),
	PINCTRL_CONF_DESC(51, REG_I2C_SDA_E2, PCIE0_RESET_E2_MASK),
	PINCTRL_CONF_DESC(52, REG_I2C_SDA_E2, PCIE1_RESET_E2_MASK),
	PINCTRL_CONF_DESC(53, REG_I2C_SDA_E2, AN7583_MDC_0_E2_MASK),
	PINCTRL_CONF_DESC(54, REG_I2C_SDA_E2, AN7583_MDIO_0_E2_MASK),
};

static const struct airoha_pinctrl_conf an7583_pinctrl_drive_e4_conf[] = {
	PINCTRL_CONF_DESC(2, REG_GPIO_L_E4, BIT(0)),
	PINCTRL_CONF_DESC(3, REG_GPIO_L_E4, BIT(1)),
	PINCTRL_CONF_DESC(4, REG_GPIO_L_E4, BIT(2)),
	PINCTRL_CONF_DESC(5, REG_GPIO_L_E4, BIT(3)),
	PINCTRL_CONF_DESC(6, REG_GPIO_L_E4, BIT(4)),
	PINCTRL_CONF_DESC(7, REG_GPIO_L_E4, BIT(5)),
	PINCTRL_CONF_DESC(8, REG_GPIO_L_E4, BIT(6)),
	PINCTRL_CONF_DESC(9, REG_GPIO_L_E4, BIT(7)),
	PINCTRL_CONF_DESC(10, REG_GPIO_L_E4, BIT(8)),
	PINCTRL_CONF_DESC(11, REG_GPIO_L_E4, BIT(9)),
	PINCTRL_CONF_DESC(12, REG_GPIO_L_E4, BIT(10)),
	PINCTRL_CONF_DESC(13, REG_GPIO_L_E4, BIT(11)),
	PINCTRL_CONF_DESC(14, REG_GPIO_L_E4, BIT(12)),
	PINCTRL_CONF_DESC(15, REG_GPIO_L_E4, BIT(13)),
	PINCTRL_CONF_DESC(16, REG_GPIO_L_E4, BIT(14)),
	PINCTRL_CONF_DESC(17, REG_GPIO_L_E4, BIT(15)),
	PINCTRL_CONF_DESC(18, REG_GPIO_L_E4, BIT(16)),
	PINCTRL_CONF_DESC(19, REG_GPIO_L_E4, BIT(17)),
	PINCTRL_CONF_DESC(20, REG_GPIO_L_E4, BIT(18)),
	PINCTRL_CONF_DESC(21, REG_GPIO_L_E4, BIT(19)),
	PINCTRL_CONF_DESC(22, REG_GPIO_L_E4, BIT(20)),
	PINCTRL_CONF_DESC(23, REG_GPIO_L_E4, BIT(21)),
	PINCTRL_CONF_DESC(24, REG_GPIO_L_E4, BIT(22)),
	PINCTRL_CONF_DESC(25, REG_GPIO_L_E4, BIT(23)),
	PINCTRL_CONF_DESC(26, REG_GPIO_L_E4, BIT(24)),
	PINCTRL_CONF_DESC(27, REG_GPIO_L_E4, BIT(25)),
	PINCTRL_CONF_DESC(28, REG_GPIO_L_E4, BIT(26)),
	PINCTRL_CONF_DESC(29, REG_GPIO_L_E4, BIT(27)),
	PINCTRL_CONF_DESC(30, REG_GPIO_L_E4, BIT(28)),
	PINCTRL_CONF_DESC(31, REG_GPIO_L_E4, BIT(29)),
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E4, BIT(30)),
	PINCTRL_CONF_DESC(33, REG_GPIO_L_E4, BIT(31)),
	PINCTRL_CONF_DESC(34, REG_GPIO_H_E4, BIT(0)),
	PINCTRL_CONF_DESC(35, REG_GPIO_H_E4, BIT(1)),
	PINCTRL_CONF_DESC(36, REG_GPIO_H_E4, BIT(2)),
	PINCTRL_CONF_DESC(37, REG_GPIO_H_E4, BIT(3)),
	PINCTRL_CONF_DESC(38, REG_GPIO_H_E4, BIT(4)),
	PINCTRL_CONF_DESC(39, REG_GPIO_H_E4, BIT(5)),
	PINCTRL_CONF_DESC(40, REG_GPIO_H_E4, BIT(6)),
	PINCTRL_CONF_DESC(41, REG_I2C_SDA_E4, I2C_SCL_E4_MASK),
	PINCTRL_CONF_DESC(42, REG_I2C_SDA_E4, I2C_SDA_E4_MASK),
	PINCTRL_CONF_DESC(43, REG_I2C_SDA_E4, AN7583_I2C1_SCL_E4_MASK),
	PINCTRL_CONF_DESC(44, REG_I2C_SDA_E4, AN7583_I2C1_SDA_E4_MASK),
	PINCTRL_CONF_DESC(45, REG_I2C_SDA_E4, SPI_CLK_E4_MASK),
	PINCTRL_CONF_DESC(46, REG_I2C_SDA_E4, SPI_CS0_E4_MASK),
	PINCTRL_CONF_DESC(47, REG_I2C_SDA_E4, SPI_MOSI_E4_MASK),
	PINCTRL_CONF_DESC(48, REG_I2C_SDA_E4, SPI_MISO_E4_MASK),
	PINCTRL_CONF_DESC(49, REG_I2C_SDA_E4, UART1_TXD_E4_MASK),
	PINCTRL_CONF_DESC(50, REG_I2C_SDA_E4, UART1_RXD_E4_MASK),
	PINCTRL_CONF_DESC(51, REG_I2C_SDA_E4, PCIE0_RESET_E4_MASK),
	PINCTRL_CONF_DESC(52, REG_I2C_SDA_E4, PCIE1_RESET_E4_MASK),
	PINCTRL_CONF_DESC(53, REG_I2C_SDA_E4, AN7583_MDC_0_E4_MASK),
	PINCTRL_CONF_DESC(54, REG_I2C_SDA_E4, AN7583_MDIO_0_E4_MASK),
};

static const struct airoha_pinctrl_conf an7583_pinctrl_pcie_rst_od_conf[] = {
	PINCTRL_CONF_DESC(51, REG_PCIE_RESET_OD, PCIE0_RESET_OD_MASK),
	PINCTRL_CONF_DESC(52, REG_PCIE_RESET_OD, PCIE1_RESET_OD_MASK),
};

static const struct airoha_pinctrl_match_data an7583_pinctrl_match_data = {
	.pinctrl_name = KBUILD_MODNAME,
	.pinctrl_owner = THIS_MODULE,
	.pins = an7583_pinctrl_pins,
	.num_pins = ARRAY_SIZE(an7583_pinctrl_pins),
	.grps = an7583_pinctrl_groups,
	.num_grps = ARRAY_SIZE(an7583_pinctrl_groups),
	.funcs = an7583_pinctrl_funcs,
	.num_funcs = ARRAY_SIZE(an7583_pinctrl_funcs),
	.confs_info = {
		[AIROHA_PINCTRL_CONFS_PULLUP] = {
			.confs = an7583_pinctrl_pullup_conf,
			.num_confs = ARRAY_SIZE(an7583_pinctrl_pullup_conf),
		},
		[AIROHA_PINCTRL_CONFS_PULLDOWN] = {
			.confs = an7583_pinctrl_pulldown_conf,
			.num_confs = ARRAY_SIZE(an7583_pinctrl_pulldown_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E2] = {
			.confs = an7583_pinctrl_drive_e2_conf,
			.num_confs = ARRAY_SIZE(an7583_pinctrl_drive_e2_conf),
		},
		[AIROHA_PINCTRL_CONFS_DRIVE_E4] = {
			.confs = an7583_pinctrl_drive_e4_conf,
			.num_confs = ARRAY_SIZE(an7583_pinctrl_drive_e4_conf),
		},
		[AIROHA_PINCTRL_CONFS_PCIE_RST_OD] = {
			.confs = an7583_pinctrl_pcie_rst_od_conf,
			.num_confs = ARRAY_SIZE(an7583_pinctrl_pcie_rst_od_conf),
		},
	},
};

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "airoha,an7583-pinctrl", .data = &an7583_pinctrl_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha-an7583",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_DESCRIPTION("Pinctrl driver for Airoha AN7583 SoC");
