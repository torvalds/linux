// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 * Author: Markus Gothe <markus.gothe@genexis.eu>
 */

#include <dt-bindings/pinctrl/mt65xx.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define PINCTRL_PIN_GROUP(id)						\
	PINCTRL_PINGROUP(#id, id##_pins, ARRAY_SIZE(id##_pins))

#define PINCTRL_FUNC_DESC(id)						\
	{								\
		.desc = PINCTRL_PINFUNCTION(#id, id##_groups,		\
					    ARRAY_SIZE(id##_groups)),	\
		.groups = id##_func_group,				\
		.group_size = ARRAY_SIZE(id##_func_group),		\
	}

#define PINCTRL_CONF_DESC(p, offset, mask)				\
	{								\
		.pin = p,						\
		.reg = { offset, mask },				\
	}

/* MUX */
#define REG_GPIO_2ND_I2C_MODE			0x0214
#define GPIO_MDC_IO_MASTER_MODE_MODE		BIT(14)
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

/* GPIOs */
#define REG_GPIO_CTRL				0x0000
#define REG_GPIO_DATA				0x0004
#define REG_GPIO_INT				0x0008
#define REG_GPIO_INT_EDGE			0x000c
#define REG_GPIO_INT_LEVEL			0x0010
#define REG_GPIO_OE				0x0014
#define REG_GPIO_CTRL1				0x0020

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

#define REG_GPIO_CTRL2				0x0060
#define REG_GPIO_CTRL3				0x0064

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

#define REG_GPIO_DATA1				0x0070
#define REG_GPIO_OE1				0x0078
#define REG_GPIO_INT1				0x007c
#define REG_GPIO_INT_EDGE1			0x0080
#define REG_GPIO_INT_EDGE2			0x0084
#define REG_GPIO_INT_EDGE3			0x0088
#define REG_GPIO_INT_LEVEL1			0x008c
#define REG_GPIO_INT_LEVEL2			0x0090
#define REG_GPIO_INT_LEVEL3			0x0094

#define AIROHA_NUM_PINS				64
#define AIROHA_PIN_BANK_SIZE			(AIROHA_NUM_PINS / 2)
#define AIROHA_REG_GPIOCTRL_NUM_PIN		(AIROHA_NUM_PINS / 4)

static const u32 gpio_data_regs[] = {
	REG_GPIO_DATA,
	REG_GPIO_DATA1
};

static const u32 gpio_out_regs[] = {
	REG_GPIO_OE,
	REG_GPIO_OE1
};

static const u32 gpio_dir_regs[] = {
	REG_GPIO_CTRL,
	REG_GPIO_CTRL1,
	REG_GPIO_CTRL2,
	REG_GPIO_CTRL3
};

static const u32 irq_status_regs[] = {
	REG_GPIO_INT,
	REG_GPIO_INT1
};

static const u32 irq_level_regs[] = {
	REG_GPIO_INT_LEVEL,
	REG_GPIO_INT_LEVEL1,
	REG_GPIO_INT_LEVEL2,
	REG_GPIO_INT_LEVEL3
};

static const u32 irq_edge_regs[] = {
	REG_GPIO_INT_EDGE,
	REG_GPIO_INT_EDGE1,
	REG_GPIO_INT_EDGE2,
	REG_GPIO_INT_EDGE3
};

struct airoha_pinctrl_reg {
	u32 offset;
	u32 mask;
};

enum airoha_pinctrl_mux_func {
	AIROHA_FUNC_MUX,
	AIROHA_FUNC_PWM_MUX,
	AIROHA_FUNC_PWM_EXT_MUX,
};

struct airoha_pinctrl_func_group {
	const char *name;
	struct {
		enum airoha_pinctrl_mux_func mux;
		u32 offset;
		u32 mask;
		u32 val;
	} regmap[2];
	int regmap_size;
};

struct airoha_pinctrl_func {
	const struct pinfunction desc;
	const struct airoha_pinctrl_func_group *groups;
	u8 group_size;
};

struct airoha_pinctrl_conf {
	u32 pin;
	struct airoha_pinctrl_reg reg;
};

struct airoha_pinctrl_gpiochip {
	struct gpio_chip chip;

	/* gpio */
	const u32 *data;
	const u32 *dir;
	const u32 *out;
	/* irq */
	const u32 *status;
	const u32 *level;
	const u32 *edge;

	u32 irq_type[AIROHA_NUM_PINS];
};

struct airoha_pinctrl {
	struct pinctrl_dev *ctrl;

	struct regmap *chip_scu;
	struct regmap *regmap;

	struct airoha_pinctrl_gpiochip gpiochip;
};

static struct pinctrl_pin_desc airoha_pinctrl_pins[] = {
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
	PINCTRL_PIN(61, "pcie_reset0"),
	PINCTRL_PIN(62, "pcie_reset1"),
	PINCTRL_PIN(63, "pcie_reset2"),
};

static const int pon_pins[] = { 49, 50, 51, 52, 53, 54 };
static const int pon_tod_1pps_pins[] = { 46 };
static const int gsw_tod_1pps_pins[] = { 46 };
static const int sipo_pins[] = { 16, 17 };
static const int sipo_rclk_pins[] = { 16, 17, 43 };
static const int mdio_pins[] = { 14, 15 };
static const int uart2_pins[] = { 48, 55 };
static const int uart2_cts_rts_pins[] = { 46, 47 };
static const int hsuart_pins[] = { 28, 29 };
static const int hsuart_cts_rts_pins[] = { 26, 27 };
static const int uart4_pins[] = { 38, 39 };
static const int uart5_pins[] = { 18, 19 };
static const int i2c0_pins[] = { 2, 3 };
static const int i2c1_pins[] = { 14, 15 };
static const int jtag_udi_pins[] = { 16, 17, 18, 19, 20 };
static const int jtag_dfd_pins[] = { 16, 17, 18, 19, 20 };
static const int i2s_pins[] = { 26, 27, 28, 29 };
static const int pcm1_pins[] = { 22, 23, 24, 25 };
static const int pcm2_pins[] = { 18, 19, 20, 21 };
static const int spi_quad_pins[] = { 32, 33 };
static const int spi_pins[] = { 4, 5, 6, 7 };
static const int spi_cs1_pins[] = { 34 };
static const int pcm_spi_pins[] = { 18, 19, 20, 21, 22, 23, 24, 25 };
static const int pcm_spi_int_pins[] = { 14 };
static const int pcm_spi_rst_pins[] = { 15 };
static const int pcm_spi_cs1_pins[] = { 43 };
static const int pcm_spi_cs2_pins[] = { 40 };
static const int pcm_spi_cs2_p128_pins[] = { 40 };
static const int pcm_spi_cs2_p156_pins[] = { 40 };
static const int pcm_spi_cs3_pins[] = { 41 };
static const int pcm_spi_cs4_pins[] = { 42 };
static const int emmc_pins[] = { 4, 5, 6, 30, 31, 32, 33, 34, 35, 36, 37 };
static const int pnand_pins[] = { 4, 5, 6, 7, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42 };
static const int gpio0_pins[] = { 13 };
static const int gpio1_pins[] = { 14 };
static const int gpio2_pins[] = { 15 };
static const int gpio3_pins[] = { 16 };
static const int gpio4_pins[] = { 17 };
static const int gpio5_pins[] = { 18 };
static const int gpio6_pins[] = { 19 };
static const int gpio7_pins[] = { 20 };
static const int gpio8_pins[] = { 21 };
static const int gpio9_pins[] = { 22 };
static const int gpio10_pins[] = { 23 };
static const int gpio11_pins[] = { 24 };
static const int gpio12_pins[] = { 25 };
static const int gpio13_pins[] = { 26 };
static const int gpio14_pins[] = { 27 };
static const int gpio15_pins[] = { 28 };
static const int gpio16_pins[] = { 29 };
static const int gpio17_pins[] = { 30 };
static const int gpio18_pins[] = { 31 };
static const int gpio19_pins[] = { 32 };
static const int gpio20_pins[] = { 33 };
static const int gpio21_pins[] = { 34 };
static const int gpio22_pins[] = { 35 };
static const int gpio23_pins[] = { 36 };
static const int gpio24_pins[] = { 37 };
static const int gpio25_pins[] = { 38 };
static const int gpio26_pins[] = { 39 };
static const int gpio27_pins[] = { 40 };
static const int gpio28_pins[] = { 41 };
static const int gpio29_pins[] = { 42 };
static const int gpio30_pins[] = { 43 };
static const int gpio31_pins[] = { 44 };
static const int gpio33_pins[] = { 46 };
static const int gpio34_pins[] = { 47 };
static const int gpio35_pins[] = { 48 };
static const int gpio36_pins[] = { 49 };
static const int gpio37_pins[] = { 50 };
static const int gpio38_pins[] = { 51 };
static const int gpio39_pins[] = { 52 };
static const int gpio40_pins[] = { 53 };
static const int gpio41_pins[] = { 54 };
static const int gpio42_pins[] = { 55 };
static const int gpio43_pins[] = { 56 };
static const int gpio44_pins[] = { 57 };
static const int gpio45_pins[] = { 58 };
static const int gpio46_pins[] = { 59 };
static const int pcie_reset0_pins[] = { 61 };
static const int pcie_reset1_pins[] = { 62 };
static const int pcie_reset2_pins[] = { 63 };

static const struct pingroup airoha_pinctrl_groups[] = {
	PINCTRL_PIN_GROUP(pon),
	PINCTRL_PIN_GROUP(pon_tod_1pps),
	PINCTRL_PIN_GROUP(gsw_tod_1pps),
	PINCTRL_PIN_GROUP(sipo),
	PINCTRL_PIN_GROUP(sipo_rclk),
	PINCTRL_PIN_GROUP(mdio),
	PINCTRL_PIN_GROUP(uart2),
	PINCTRL_PIN_GROUP(uart2_cts_rts),
	PINCTRL_PIN_GROUP(hsuart),
	PINCTRL_PIN_GROUP(hsuart_cts_rts),
	PINCTRL_PIN_GROUP(uart4),
	PINCTRL_PIN_GROUP(uart5),
	PINCTRL_PIN_GROUP(i2c0),
	PINCTRL_PIN_GROUP(i2c1),
	PINCTRL_PIN_GROUP(jtag_udi),
	PINCTRL_PIN_GROUP(jtag_dfd),
	PINCTRL_PIN_GROUP(i2s),
	PINCTRL_PIN_GROUP(pcm1),
	PINCTRL_PIN_GROUP(pcm2),
	PINCTRL_PIN_GROUP(spi),
	PINCTRL_PIN_GROUP(spi_quad),
	PINCTRL_PIN_GROUP(spi_cs1),
	PINCTRL_PIN_GROUP(pcm_spi),
	PINCTRL_PIN_GROUP(pcm_spi_int),
	PINCTRL_PIN_GROUP(pcm_spi_rst),
	PINCTRL_PIN_GROUP(pcm_spi_cs1),
	PINCTRL_PIN_GROUP(pcm_spi_cs2_p128),
	PINCTRL_PIN_GROUP(pcm_spi_cs2_p156),
	PINCTRL_PIN_GROUP(pcm_spi_cs2),
	PINCTRL_PIN_GROUP(pcm_spi_cs3),
	PINCTRL_PIN_GROUP(pcm_spi_cs4),
	PINCTRL_PIN_GROUP(emmc),
	PINCTRL_PIN_GROUP(pnand),
	PINCTRL_PIN_GROUP(gpio0),
	PINCTRL_PIN_GROUP(gpio1),
	PINCTRL_PIN_GROUP(gpio2),
	PINCTRL_PIN_GROUP(gpio3),
	PINCTRL_PIN_GROUP(gpio4),
	PINCTRL_PIN_GROUP(gpio5),
	PINCTRL_PIN_GROUP(gpio6),
	PINCTRL_PIN_GROUP(gpio7),
	PINCTRL_PIN_GROUP(gpio8),
	PINCTRL_PIN_GROUP(gpio9),
	PINCTRL_PIN_GROUP(gpio10),
	PINCTRL_PIN_GROUP(gpio11),
	PINCTRL_PIN_GROUP(gpio12),
	PINCTRL_PIN_GROUP(gpio13),
	PINCTRL_PIN_GROUP(gpio14),
	PINCTRL_PIN_GROUP(gpio15),
	PINCTRL_PIN_GROUP(gpio16),
	PINCTRL_PIN_GROUP(gpio17),
	PINCTRL_PIN_GROUP(gpio18),
	PINCTRL_PIN_GROUP(gpio19),
	PINCTRL_PIN_GROUP(gpio20),
	PINCTRL_PIN_GROUP(gpio21),
	PINCTRL_PIN_GROUP(gpio22),
	PINCTRL_PIN_GROUP(gpio23),
	PINCTRL_PIN_GROUP(gpio24),
	PINCTRL_PIN_GROUP(gpio25),
	PINCTRL_PIN_GROUP(gpio26),
	PINCTRL_PIN_GROUP(gpio27),
	PINCTRL_PIN_GROUP(gpio28),
	PINCTRL_PIN_GROUP(gpio29),
	PINCTRL_PIN_GROUP(gpio30),
	PINCTRL_PIN_GROUP(gpio31),
	PINCTRL_PIN_GROUP(gpio33),
	PINCTRL_PIN_GROUP(gpio34),
	PINCTRL_PIN_GROUP(gpio35),
	PINCTRL_PIN_GROUP(gpio36),
	PINCTRL_PIN_GROUP(gpio37),
	PINCTRL_PIN_GROUP(gpio38),
	PINCTRL_PIN_GROUP(gpio39),
	PINCTRL_PIN_GROUP(gpio40),
	PINCTRL_PIN_GROUP(gpio41),
	PINCTRL_PIN_GROUP(gpio42),
	PINCTRL_PIN_GROUP(gpio43),
	PINCTRL_PIN_GROUP(gpio44),
	PINCTRL_PIN_GROUP(gpio45),
	PINCTRL_PIN_GROUP(gpio46),
	PINCTRL_PIN_GROUP(pcie_reset0),
	PINCTRL_PIN_GROUP(pcie_reset1),
	PINCTRL_PIN_GROUP(pcie_reset2),
};

static const char *const pon_groups[] = { "pon" };
static const char *const tod_1pps_groups[] = { "pon_tod_1pps", "gsw_tod_1pps" };
static const char *const sipo_groups[] = { "sipo", "sipo_rclk" };
static const char *const mdio_groups[] = { "mdio" };
static const char *const uart_groups[] = { "uart2", "uart2_cts_rts", "hsuart",
					   "hsuart_cts_rts", "uart4",
					   "uart5" };
static const char *const i2c_groups[] = { "i2c1" };
static const char *const jtag_groups[] = { "jtag_udi", "jtag_dfd" };
static const char *const pcm_groups[] = { "pcm1", "pcm2" };
static const char *const spi_groups[] = { "spi_quad", "spi_cs1" };
static const char *const pcm_spi_groups[] = { "pcm_spi", "pcm_spi_int",
					      "pcm_spi_rst", "pcm_spi_cs1",
					      "pcm_spi_cs2_p156",
					      "pcm_spi_cs2_p128",
					      "pcm_spi_cs3", "pcm_spi_cs4" };
static const char *const i2s_groups[] = { "i2s" };
static const char *const emmc_groups[] = { "emmc" };
static const char *const pnand_groups[] = { "pnand" };
static const char *const pcie_reset_groups[] = { "pcie_reset0", "pcie_reset1",
						 "pcie_reset2" };
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
					  "gpio36", "gpio37",
					  "gpio38", "gpio39",
					  "gpio40", "gpio41",
					  "gpio42", "gpio43",
					  "gpio44", "gpio45",
					  "gpio46", "gpio47" };
static const char *const phy1_led0_groups[] = { "gpio33", "gpio34",
						"gpio35", "gpio42" };
static const char *const phy2_led0_groups[] = { "gpio33", "gpio34",
						"gpio35", "gpio42" };
static const char *const phy3_led0_groups[] = { "gpio33", "gpio34",
						"gpio35", "gpio42" };
static const char *const phy4_led0_groups[] = { "gpio33", "gpio34",
						"gpio35", "gpio42" };
static const char *const phy1_led1_groups[] = { "gpio43", "gpio44",
						"gpio45", "gpio46" };
static const char *const phy2_led1_groups[] = { "gpio43", "gpio44",
						"gpio45", "gpio46" };
static const char *const phy3_led1_groups[] = { "gpio43", "gpio44",
						"gpio45", "gpio46" };
static const char *const phy4_led1_groups[] = { "gpio43", "gpio44",
						"gpio45", "gpio46" };

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
			GPIO_MDC_IO_MASTER_MODE_MODE,
			GPIO_MDC_IO_MASTER_MODE_MODE
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

static const struct airoha_pinctrl_func_group pcie_reset_func_group[] = {
	{
		.name = "pcie_reset0",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET0_MASK,
			GPIO_PCIE_RESET0_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcie_reset1",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET1_MASK,
			GPIO_PCIE_RESET1_MASK
		},
		.regmap_size = 1,
	}, {
		.name = "pcie_reset2",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_PON_MODE,
			GPIO_PCIE_RESET2_MASK,
			GPIO_PCIE_RESET2_MASK
		},
		.regmap_size = 1,
	},
};

/* PWM */
static const struct airoha_pinctrl_func_group pwm_func_group[] = {
	{
		.name = "gpio0",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO0_FLASH_MODE_CFG,
			GPIO0_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio1",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO1_FLASH_MODE_CFG,
			GPIO1_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio2",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO2_FLASH_MODE_CFG,
			GPIO2_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio3",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO3_FLASH_MODE_CFG,
			GPIO3_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio4",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO4_FLASH_MODE_CFG,
			GPIO4_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio5",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO5_FLASH_MODE_CFG,
			GPIO5_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio6",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO6_FLASH_MODE_CFG,
			GPIO6_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio7",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO7_FLASH_MODE_CFG,
			GPIO7_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio8",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO8_FLASH_MODE_CFG,
			GPIO8_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio9",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO9_FLASH_MODE_CFG,
			GPIO9_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio10",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO10_FLASH_MODE_CFG,
			GPIO10_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio11",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO11_FLASH_MODE_CFG,
			GPIO11_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio12",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO12_FLASH_MODE_CFG,
			GPIO12_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio13",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO13_FLASH_MODE_CFG,
			GPIO13_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio14",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO14_FLASH_MODE_CFG,
			GPIO14_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio15",
		.regmap[0] = {
			AIROHA_FUNC_PWM_MUX,
			REG_GPIO_FLASH_MODE_CFG,
			GPIO15_FLASH_MODE_CFG,
			GPIO15_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio16",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO16_FLASH_MODE_CFG,
			GPIO16_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio17",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO17_FLASH_MODE_CFG,
			GPIO17_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio18",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO18_FLASH_MODE_CFG,
			GPIO18_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio19",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO19_FLASH_MODE_CFG,
			GPIO19_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio20",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO20_FLASH_MODE_CFG,
			GPIO20_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio21",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO21_FLASH_MODE_CFG,
			GPIO21_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio22",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO22_FLASH_MODE_CFG,
			GPIO22_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio23",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO23_FLASH_MODE_CFG,
			GPIO23_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio24",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO24_FLASH_MODE_CFG,
			GPIO24_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio25",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO25_FLASH_MODE_CFG,
			GPIO25_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio26",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO26_FLASH_MODE_CFG,
			GPIO26_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio27",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO27_FLASH_MODE_CFG,
			GPIO27_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio28",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO28_FLASH_MODE_CFG,
			GPIO28_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio29",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO29_FLASH_MODE_CFG,
			GPIO29_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio30",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO30_FLASH_MODE_CFG,
			GPIO30_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio31",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO31_FLASH_MODE_CFG,
			GPIO31_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio36",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO36_FLASH_MODE_CFG,
			GPIO36_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio37",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO37_FLASH_MODE_CFG,
			GPIO37_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio38",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO38_FLASH_MODE_CFG,
			GPIO38_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio39",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO39_FLASH_MODE_CFG,
			GPIO39_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio40",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO40_FLASH_MODE_CFG,
			GPIO40_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio41",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO41_FLASH_MODE_CFG,
			GPIO41_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio42",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO42_FLASH_MODE_CFG,
			GPIO42_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio43",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO43_FLASH_MODE_CFG,
			GPIO43_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio44",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO44_FLASH_MODE_CFG,
			GPIO44_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio45",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO45_FLASH_MODE_CFG,
			GPIO45_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio46",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO46_FLASH_MODE_CFG,
			GPIO46_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	}, {
		.name = "gpio47",
		.regmap[0] = {
			AIROHA_FUNC_PWM_EXT_MUX,
			REG_GPIO_FLASH_MODE_CFG_EXT,
			GPIO47_FLASH_MODE_CFG,
			GPIO47_FLASH_MODE_CFG
		},
		.regmap_size = 1,
	},
};

static const struct airoha_pinctrl_func_group phy1_led0_func_group[] = {
	{
		.name = "gpio33",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED0_MODE_MASK,
			GPIO_LAN0_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio34",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED0_MODE_MASK,
			GPIO_LAN1_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio35",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED0_MODE_MASK,
			GPIO_LAN2_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio42",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED0_MODE_MASK,
			GPIO_LAN3_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy2_led0_func_group[] = {
	{
		.name = "gpio33",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED0_MODE_MASK,
			GPIO_LAN0_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio34",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED0_MODE_MASK,
			GPIO_LAN1_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio35",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED0_MODE_MASK,
			GPIO_LAN2_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio42",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED0_MODE_MASK,
			GPIO_LAN3_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy3_led0_func_group[] = {
	{
		.name = "gpio33",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED0_MODE_MASK,
			GPIO_LAN0_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio34",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED0_MODE_MASK,
			GPIO_LAN1_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio35",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED0_MODE_MASK,
			GPIO_LAN2_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio42",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED0_MODE_MASK,
			GPIO_LAN3_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy4_led0_func_group[] = {
	{
		.name = "gpio33",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED0_MODE_MASK,
			GPIO_LAN0_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio34",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED0_MODE_MASK,
			GPIO_LAN1_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio35",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED0_MODE_MASK,
			GPIO_LAN2_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio42",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED0_MODE_MASK,
			GPIO_LAN3_LED0_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED0_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy1_led1_func_group[] = {
	{
		.name = "gpio43",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED1_MODE_MASK,
			GPIO_LAN0_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio44",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED1_MODE_MASK,
			GPIO_LAN1_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio45",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED1_MODE_MASK,
			GPIO_LAN2_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio46",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED1_MODE_MASK,
			GPIO_LAN3_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(0)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy2_led1_func_group[] = {
	{
		.name = "gpio43",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED1_MODE_MASK,
			GPIO_LAN0_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio44",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED1_MODE_MASK,
			GPIO_LAN1_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio45",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED1_MODE_MASK,
			GPIO_LAN2_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio46",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED1_MODE_MASK,
			GPIO_LAN3_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(1)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy3_led1_func_group[] = {
	{
		.name = "gpio43",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED1_MODE_MASK,
			GPIO_LAN0_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio44",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED1_MODE_MASK,
			GPIO_LAN1_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio45",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED1_MODE_MASK,
			GPIO_LAN2_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio46",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED1_MODE_MASK,
			GPIO_LAN3_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(2)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func_group phy4_led1_func_group[] = {
	{
		.name = "gpio43",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN0_LED1_MODE_MASK,
			GPIO_LAN0_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN0_LED_MAPPING_MASK,
			LAN0_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio44",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN1_LED1_MODE_MASK,
			GPIO_LAN1_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN1_LED_MAPPING_MASK,
			LAN1_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio45",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN2_LED1_MODE_MASK,
			GPIO_LAN2_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN2_LED_MAPPING_MASK,
			LAN2_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	}, {
		.name = "gpio46",
		.regmap[0] = {
			AIROHA_FUNC_MUX,
			REG_GPIO_2ND_I2C_MODE,
			GPIO_LAN3_LED1_MODE_MASK,
			GPIO_LAN3_LED1_MODE_MASK
		},
		.regmap[1] = {
			AIROHA_FUNC_MUX,
			REG_LAN_LED1_MAPPING,
			LAN3_LED_MAPPING_MASK,
			LAN3_PHY_LED_MAP(3)
		},
		.regmap_size = 2,
	},
};

static const struct airoha_pinctrl_func airoha_pinctrl_funcs[] = {
	PINCTRL_FUNC_DESC(pon),
	PINCTRL_FUNC_DESC(tod_1pps),
	PINCTRL_FUNC_DESC(sipo),
	PINCTRL_FUNC_DESC(mdio),
	PINCTRL_FUNC_DESC(uart),
	PINCTRL_FUNC_DESC(i2c),
	PINCTRL_FUNC_DESC(jtag),
	PINCTRL_FUNC_DESC(pcm),
	PINCTRL_FUNC_DESC(spi),
	PINCTRL_FUNC_DESC(pcm_spi),
	PINCTRL_FUNC_DESC(i2s),
	PINCTRL_FUNC_DESC(emmc),
	PINCTRL_FUNC_DESC(pnand),
	PINCTRL_FUNC_DESC(pcie_reset),
	PINCTRL_FUNC_DESC(pwm),
	PINCTRL_FUNC_DESC(phy1_led0),
	PINCTRL_FUNC_DESC(phy2_led0),
	PINCTRL_FUNC_DESC(phy3_led0),
	PINCTRL_FUNC_DESC(phy4_led0),
	PINCTRL_FUNC_DESC(phy1_led1),
	PINCTRL_FUNC_DESC(phy2_led1),
	PINCTRL_FUNC_DESC(phy3_led1),
	PINCTRL_FUNC_DESC(phy4_led1),
};

static const struct airoha_pinctrl_conf airoha_pinctrl_pullup_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_PU, UART1_TXD_PU_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_PU, UART1_RXD_PU_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PU, I2C_SDA_PU_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PU, I2C_SCL_PU_MASK),
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
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PU, BIT(18)),
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
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_PU, PCIE0_RESET_PU_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_PU, PCIE1_RESET_PU_MASK),
	PINCTRL_CONF_DESC(63, REG_I2C_SDA_PU, PCIE2_RESET_PU_MASK),
};

static const struct airoha_pinctrl_conf airoha_pinctrl_pulldown_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_PD, UART1_TXD_PD_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_PD, UART1_RXD_PD_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_PD, I2C_SDA_PD_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_PD, I2C_SCL_PD_MASK),
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
	PINCTRL_CONF_DESC(32, REG_GPIO_L_PD, BIT(18)),
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
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_PD, PCIE0_RESET_PD_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_PD, PCIE1_RESET_PD_MASK),
	PINCTRL_CONF_DESC(63, REG_I2C_SDA_PD, PCIE2_RESET_PD_MASK),
};

static const struct airoha_pinctrl_conf airoha_pinctrl_drive_e2_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_E2, UART1_TXD_E2_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_E2, UART1_RXD_E2_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E2, I2C_SDA_E2_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E2, I2C_SCL_E2_MASK),
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
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E2, BIT(18)),
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
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_E2, PCIE0_RESET_E2_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_E2, PCIE1_RESET_E2_MASK),
	PINCTRL_CONF_DESC(63, REG_I2C_SDA_E2, PCIE2_RESET_E2_MASK),
};

static const struct airoha_pinctrl_conf airoha_pinctrl_drive_e4_conf[] = {
	PINCTRL_CONF_DESC(0, REG_I2C_SDA_E4, UART1_TXD_E4_MASK),
	PINCTRL_CONF_DESC(1, REG_I2C_SDA_E4, UART1_RXD_E4_MASK),
	PINCTRL_CONF_DESC(2, REG_I2C_SDA_E4, I2C_SDA_E4_MASK),
	PINCTRL_CONF_DESC(3, REG_I2C_SDA_E4, I2C_SCL_E4_MASK),
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
	PINCTRL_CONF_DESC(32, REG_GPIO_L_E4, BIT(18)),
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
	PINCTRL_CONF_DESC(61, REG_I2C_SDA_E4, PCIE0_RESET_E4_MASK),
	PINCTRL_CONF_DESC(62, REG_I2C_SDA_E4, PCIE1_RESET_E4_MASK),
	PINCTRL_CONF_DESC(63, REG_I2C_SDA_E4, PCIE2_RESET_E4_MASK),
};

static const struct airoha_pinctrl_conf airoha_pinctrl_pcie_rst_od_conf[] = {
	PINCTRL_CONF_DESC(61, REG_PCIE_RESET_OD, PCIE0_RESET_OD_MASK),
	PINCTRL_CONF_DESC(62, REG_PCIE_RESET_OD, PCIE1_RESET_OD_MASK),
	PINCTRL_CONF_DESC(63, REG_PCIE_RESET_OD, PCIE2_RESET_OD_MASK),
};

static int airoha_convert_pin_to_reg_offset(struct pinctrl_dev *pctrl_dev,
					    struct pinctrl_gpio_range *range,
					    int pin)
{
	if (!range)
		range = pinctrl_find_gpio_range_from_pin_nolock(pctrl_dev,
								pin);
	if (!range)
		return -EINVAL;

	return pin - range->pin_base;
}

/* gpio callbacks */
static int airoha_gpio_set(struct gpio_chip *chip, unsigned int gpio,
			   int value)
{
	struct airoha_pinctrl *pinctrl = gpiochip_get_data(chip);
	u32 offset = gpio % AIROHA_PIN_BANK_SIZE;
	u8 index = gpio / AIROHA_PIN_BANK_SIZE;

	return regmap_update_bits(pinctrl->regmap,
				  pinctrl->gpiochip.data[index],
				  BIT(offset), value ? BIT(offset) : 0);
}

static int airoha_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct airoha_pinctrl *pinctrl = gpiochip_get_data(chip);
	u32 val, pin = gpio % AIROHA_PIN_BANK_SIZE;
	u8 index = gpio / AIROHA_PIN_BANK_SIZE;
	int err;

	err = regmap_read(pinctrl->regmap,
			  pinctrl->gpiochip.data[index], &val);

	return err ? err : !!(val & BIT(pin));
}

static int airoha_gpio_direction_output(struct gpio_chip *chip,
					unsigned int gpio, int value)
{
	int err;

	err = pinctrl_gpio_direction_output(chip, gpio);
	if (err)
		return err;

	return airoha_gpio_set(chip, gpio, value);
}

/* irq callbacks */
static void airoha_irq_unmask(struct irq_data *data)
{
	u8 offset = data->hwirq % AIROHA_REG_GPIOCTRL_NUM_PIN;
	u8 index = data->hwirq / AIROHA_REG_GPIOCTRL_NUM_PIN;
	u32 mask = GENMASK(2 * offset + 1, 2 * offset);
	struct airoha_pinctrl_gpiochip *gpiochip;
	struct airoha_pinctrl *pinctrl;
	u32 val = BIT(2 * offset);

	gpiochip = irq_data_get_irq_chip_data(data);
	if (WARN_ON_ONCE(data->hwirq >= ARRAY_SIZE(gpiochip->irq_type)))
		return;

	pinctrl = container_of(gpiochip, struct airoha_pinctrl, gpiochip);
	switch (gpiochip->irq_type[data->hwirq]) {
	case IRQ_TYPE_LEVEL_LOW:
		val = val << 1;
		fallthrough;
	case IRQ_TYPE_LEVEL_HIGH:
		regmap_update_bits(pinctrl->regmap, gpiochip->level[index],
				   mask, val);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val = val << 1;
		fallthrough;
	case IRQ_TYPE_EDGE_RISING:
		regmap_update_bits(pinctrl->regmap, gpiochip->edge[index],
				   mask, val);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		regmap_set_bits(pinctrl->regmap, gpiochip->edge[index], mask);
		break;
	default:
		break;
	}
}

static void airoha_irq_mask(struct irq_data *data)
{
	u8 offset = data->hwirq % AIROHA_REG_GPIOCTRL_NUM_PIN;
	u8 index = data->hwirq / AIROHA_REG_GPIOCTRL_NUM_PIN;
	u32 mask = GENMASK(2 * offset + 1, 2 * offset);
	struct airoha_pinctrl_gpiochip *gpiochip;
	struct airoha_pinctrl *pinctrl;

	gpiochip = irq_data_get_irq_chip_data(data);
	pinctrl = container_of(gpiochip, struct airoha_pinctrl, gpiochip);

	regmap_clear_bits(pinctrl->regmap, gpiochip->level[index], mask);
	regmap_clear_bits(pinctrl->regmap, gpiochip->edge[index], mask);
}

static int airoha_irq_type(struct irq_data *data, unsigned int type)
{
	struct airoha_pinctrl_gpiochip *gpiochip;

	gpiochip = irq_data_get_irq_chip_data(data);
	if (data->hwirq >= ARRAY_SIZE(gpiochip->irq_type))
		return -EINVAL;

	if (type == IRQ_TYPE_PROBE) {
		if (gpiochip->irq_type[data->hwirq])
			return 0;

		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}
	gpiochip->irq_type[data->hwirq] = type & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static irqreturn_t airoha_irq_handler(int irq, void *data)
{
	struct airoha_pinctrl *pinctrl = data;
	bool handled = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(irq_status_regs); i++) {
		struct gpio_irq_chip *girq = &pinctrl->gpiochip.chip.irq;
		u32 regmap;
		unsigned long status;
		int irq;

		if (regmap_read(pinctrl->regmap, pinctrl->gpiochip.status[i],
				&regmap))
			continue;

		status = regmap;
		for_each_set_bit(irq, &status, AIROHA_PIN_BANK_SIZE) {
			u32 offset = irq + i * AIROHA_PIN_BANK_SIZE;

			generic_handle_irq(irq_find_mapping(girq->domain,
							    offset));
			regmap_write(pinctrl->regmap,
				     pinctrl->gpiochip.status[i], BIT(irq));
		}
		handled |= !!status;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static const struct irq_chip airoha_gpio_irq_chip = {
	.name = "airoha-gpio-irq",
	.irq_unmask = airoha_irq_unmask,
	.irq_mask = airoha_irq_mask,
	.irq_mask_ack = airoha_irq_mask,
	.irq_set_type = airoha_irq_type,
	.flags = IRQCHIP_SET_TYPE_MASKED | IRQCHIP_IMMUTABLE,
};

static int airoha_pinctrl_add_gpiochip(struct airoha_pinctrl *pinctrl,
				       struct platform_device *pdev)
{
	struct airoha_pinctrl_gpiochip *chip = &pinctrl->gpiochip;
	struct gpio_chip *gc = &chip->chip;
	struct gpio_irq_chip *girq = &gc->irq;
	struct device *dev = &pdev->dev;
	int irq, err;

	chip->data = gpio_data_regs;
	chip->dir = gpio_dir_regs;
	chip->out = gpio_out_regs;
	chip->status = irq_status_regs;
	chip->level = irq_level_regs;
	chip->edge = irq_edge_regs;

	gc->parent = dev;
	gc->label = dev_name(dev);
	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->direction_input = pinctrl_gpio_direction_input;
	gc->direction_output = airoha_gpio_direction_output;
	gc->set = airoha_gpio_set;
	gc->get = airoha_gpio_get;
	gc->base = -1;
	gc->ngpio = AIROHA_NUM_PINS;

	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	gpio_irq_chip_set_chip(girq, &airoha_gpio_irq_chip);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(dev, irq, airoha_irq_handler, IRQF_SHARED,
			       dev_name(dev), pinctrl);
	if (err) {
		dev_err(dev, "error requesting irq %d: %d\n", irq, err);
		return err;
	}

	return devm_gpiochip_add_data(dev, gc, pinctrl);
}

/* pinmux callbacks */
static int airoha_pinmux_set_mux(struct pinctrl_dev *pctrl_dev,
				 unsigned int selector,
				 unsigned int group)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct airoha_pinctrl_func *func;
	const struct function_desc *desc;
	struct group_desc *grp;
	int i;

	desc = pinmux_generic_get_function(pctrl_dev, selector);
	if (!desc)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctrl_dev, group);
	if (!grp)
		return -EINVAL;

	dev_dbg(pctrl_dev->dev, "enable function %s group %s\n",
		desc->func->name, grp->grp.name);

	func = desc->data;
	for (i = 0; i < func->group_size; i++) {
		const struct airoha_pinctrl_func_group *group;
		int j;

		group = &func->groups[i];
		if (strcmp(group->name, grp->grp.name))
			continue;

		for (j = 0; j < group->regmap_size; j++) {
			switch (group->regmap[j].mux) {
			case AIROHA_FUNC_PWM_EXT_MUX:
			case AIROHA_FUNC_PWM_MUX:
				regmap_update_bits(pinctrl->regmap,
						   group->regmap[j].offset,
						   group->regmap[j].mask,
						   group->regmap[j].val);
				break;
			default:
				regmap_update_bits(pinctrl->chip_scu,
						   group->regmap[j].offset,
						   group->regmap[j].mask,
						   group->regmap[j].val);
				break;
			}
		}
		return 0;
	}

	return -EINVAL;
}

static int airoha_pinmux_set_direction(struct pinctrl_dev *pctrl_dev,
				       struct pinctrl_gpio_range *range,
				       unsigned int p, bool input)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 mask, index;
	int err, pin;

	pin = airoha_convert_pin_to_reg_offset(pctrl_dev, range, p);
	if (pin < 0)
		return pin;

	/* set output enable */
	mask = BIT(pin % AIROHA_PIN_BANK_SIZE);
	index = pin / AIROHA_PIN_BANK_SIZE;
	err = regmap_update_bits(pinctrl->regmap, pinctrl->gpiochip.out[index],
				 mask, !input ? mask : 0);
	if (err)
		return err;

	/* set direction */
	mask = BIT(2 * (pin % AIROHA_REG_GPIOCTRL_NUM_PIN));
	index = pin / AIROHA_REG_GPIOCTRL_NUM_PIN;
	return regmap_update_bits(pinctrl->regmap,
				  pinctrl->gpiochip.dir[index], mask,
				  !input ? mask : 0);
}

static const struct pinmux_ops airoha_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.gpio_set_direction = airoha_pinmux_set_direction,
	.set_mux = airoha_pinmux_set_mux,
	.strict = true,
};

/* pinconf callbacks */
static const struct airoha_pinctrl_reg *
airoha_pinctrl_get_conf_reg(const struct airoha_pinctrl_conf *conf,
			    int conf_size, int pin)
{
	int i;

	for (i = 0; i < conf_size; i++) {
		if (conf[i].pin == pin)
			return &conf[i].reg;
	}

	return NULL;
}

static int airoha_pinctrl_get_conf(struct airoha_pinctrl *pinctrl,
				   const struct airoha_pinctrl_conf *conf,
				   int conf_size, int pin, u32 *val)
{
	const struct airoha_pinctrl_reg *reg;

	reg = airoha_pinctrl_get_conf_reg(conf, conf_size, pin);
	if (!reg)
		return -EINVAL;

	if (regmap_read(pinctrl->chip_scu, reg->offset, val))
		return -EINVAL;

	*val = (*val & reg->mask) >> __ffs(reg->mask);

	return 0;
}

static int airoha_pinctrl_set_conf(struct airoha_pinctrl *pinctrl,
				   const struct airoha_pinctrl_conf *conf,
				   int conf_size, int pin, u32 val)
{
	const struct airoha_pinctrl_reg *reg = NULL;

	reg = airoha_pinctrl_get_conf_reg(conf, conf_size, pin);
	if (!reg)
		return -EINVAL;


	if (regmap_update_bits(pinctrl->chip_scu, reg->offset, reg->mask,
			       val << __ffs(reg->mask)))
		return -EINVAL;

	return 0;
}

#define airoha_pinctrl_get_pullup_conf(pinctrl, pin, val)			\
	airoha_pinctrl_get_conf((pinctrl), airoha_pinctrl_pullup_conf,		\
				ARRAY_SIZE(airoha_pinctrl_pullup_conf),		\
				(pin), (val))
#define airoha_pinctrl_get_pulldown_conf(pinctrl, pin, val)			\
	airoha_pinctrl_get_conf((pinctrl), airoha_pinctrl_pulldown_conf,	\
				ARRAY_SIZE(airoha_pinctrl_pulldown_conf),	\
				(pin), (val))
#define airoha_pinctrl_get_drive_e2_conf(pinctrl, pin, val)			\
	airoha_pinctrl_get_conf((pinctrl), airoha_pinctrl_drive_e2_conf,	\
				ARRAY_SIZE(airoha_pinctrl_drive_e2_conf),	\
				(pin), (val))
#define airoha_pinctrl_get_drive_e4_conf(pinctrl, pin, val)			\
	airoha_pinctrl_get_conf((pinctrl), airoha_pinctrl_drive_e4_conf,	\
				ARRAY_SIZE(airoha_pinctrl_drive_e4_conf),	\
				(pin), (val))
#define airoha_pinctrl_get_pcie_rst_od_conf(pinctrl, pin, val)			\
	airoha_pinctrl_get_conf((pinctrl), airoha_pinctrl_pcie_rst_od_conf,	\
				ARRAY_SIZE(airoha_pinctrl_pcie_rst_od_conf),	\
				(pin), (val))
#define airoha_pinctrl_set_pullup_conf(pinctrl, pin, val)			\
	airoha_pinctrl_set_conf((pinctrl), airoha_pinctrl_pullup_conf,		\
				ARRAY_SIZE(airoha_pinctrl_pullup_conf),		\
				(pin), (val))
#define airoha_pinctrl_set_pulldown_conf(pinctrl, pin, val)			\
	airoha_pinctrl_set_conf((pinctrl), airoha_pinctrl_pulldown_conf,	\
				ARRAY_SIZE(airoha_pinctrl_pulldown_conf),	\
				(pin), (val))
#define airoha_pinctrl_set_drive_e2_conf(pinctrl, pin, val)			\
	airoha_pinctrl_set_conf((pinctrl), airoha_pinctrl_drive_e2_conf,	\
				ARRAY_SIZE(airoha_pinctrl_drive_e2_conf),	\
				(pin), (val))
#define airoha_pinctrl_set_drive_e4_conf(pinctrl, pin, val)			\
	airoha_pinctrl_set_conf((pinctrl), airoha_pinctrl_drive_e4_conf,	\
				ARRAY_SIZE(airoha_pinctrl_drive_e4_conf),	\
				(pin), (val))
#define airoha_pinctrl_set_pcie_rst_od_conf(pinctrl, pin, val)			\
	airoha_pinctrl_set_conf((pinctrl), airoha_pinctrl_pcie_rst_od_conf,	\
				ARRAY_SIZE(airoha_pinctrl_pcie_rst_od_conf),	\
				(pin), (val))

static int airoha_pinconf_get_direction(struct pinctrl_dev *pctrl_dev, u32 p)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 val, mask;
	int err, pin;
	u8 index;

	pin = airoha_convert_pin_to_reg_offset(pctrl_dev, NULL, p);
	if (pin < 0)
		return pin;

	index = pin / AIROHA_REG_GPIOCTRL_NUM_PIN;
	err = regmap_read(pinctrl->regmap, pinctrl->gpiochip.dir[index], &val);
	if (err)
		return err;

	mask = BIT(2 * (pin % AIROHA_REG_GPIOCTRL_NUM_PIN));
	return val & mask ? PIN_CONFIG_OUTPUT_ENABLE : PIN_CONFIG_INPUT_ENABLE;
}

static int airoha_pinconf_get(struct pinctrl_dev *pctrl_dev,
			      unsigned int pin, unsigned long *config)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP: {
		u32 pull_up, pull_down;

		if (airoha_pinctrl_get_pullup_conf(pinctrl, pin, &pull_up) ||
		    airoha_pinctrl_get_pulldown_conf(pinctrl, pin, &pull_down))
			return -EINVAL;

		if (param == PIN_CONFIG_BIAS_PULL_UP &&
		    !(pull_up && !pull_down))
			return -EINVAL;
		else if (param == PIN_CONFIG_BIAS_PULL_DOWN &&
			 !(pull_down && !pull_up))
			return -EINVAL;
		else if (pull_up || pull_down)
			return -EINVAL;

		arg = 1;
		break;
	}
	case PIN_CONFIG_DRIVE_STRENGTH: {
		u32 e2, e4;

		if (airoha_pinctrl_get_drive_e2_conf(pinctrl, pin, &e2) ||
		    airoha_pinctrl_get_drive_e4_conf(pinctrl, pin, &e4))
			return -EINVAL;

		arg = e4 << 1 | e2;
		break;
	}
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (airoha_pinctrl_get_pcie_rst_od_conf(pinctrl, pin, &arg))
			return -EINVAL;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
	case PIN_CONFIG_INPUT_ENABLE:
		arg = airoha_pinconf_get_direction(pctrl_dev, pin);
		if (arg != param)
			return -EINVAL;

		arg = 1;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int airoha_pinconf_set_pin_value(struct pinctrl_dev *pctrl_dev,
					unsigned int p, bool value)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	int pin;

	pin = airoha_convert_pin_to_reg_offset(pctrl_dev, NULL, p);
	if (pin < 0)
		return pin;

	return airoha_gpio_set(&pinctrl->gpiochip.chip, pin, value);
}

static int airoha_pinconf_set(struct pinctrl_dev *pctrl_dev,
			      unsigned int pin, unsigned long *configs,
			      unsigned int num_configs)
{
	struct airoha_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	int i;

	for (i = 0; i < num_configs; i++) {
		u32 param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			airoha_pinctrl_set_pulldown_conf(pinctrl, pin, 0);
			airoha_pinctrl_set_pullup_conf(pinctrl, pin, 0);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			airoha_pinctrl_set_pulldown_conf(pinctrl, pin, 0);
			airoha_pinctrl_set_pullup_conf(pinctrl, pin, 1);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			airoha_pinctrl_set_pulldown_conf(pinctrl, pin, 1);
			airoha_pinctrl_set_pullup_conf(pinctrl, pin, 0);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH: {
			u32 e2 = 0, e4 = 0;

			switch (arg) {
			case MTK_DRIVE_2mA:
				break;
			case MTK_DRIVE_4mA:
				e2 = 1;
				break;
			case MTK_DRIVE_6mA:
				e4 = 1;
				break;
			case MTK_DRIVE_8mA:
				e2 = 1;
				e4 = 1;
				break;
			default:
				return -EINVAL;
			}

			airoha_pinctrl_set_drive_e2_conf(pinctrl, pin, e2);
			airoha_pinctrl_set_drive_e4_conf(pinctrl, pin, e4);
			break;
		}
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			airoha_pinctrl_set_pcie_rst_od_conf(pinctrl, pin, !!arg);
			break;
		case PIN_CONFIG_OUTPUT_ENABLE:
		case PIN_CONFIG_INPUT_ENABLE:
		case PIN_CONFIG_LEVEL: {
			bool input = param == PIN_CONFIG_INPUT_ENABLE;
			int err;

			err = airoha_pinmux_set_direction(pctrl_dev, NULL, pin,
							  input);
			if (err)
				return err;

			if (param == PIN_CONFIG_LEVEL) {
				err = airoha_pinconf_set_pin_value(pctrl_dev,
								   pin, !!arg);
				if (err)
					return err;
			}
			break;
		}
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int airoha_pinconf_group_get(struct pinctrl_dev *pctrl_dev,
				    unsigned int group, unsigned long *config)
{
	u32 cur_config = 0;
	int i;

	for (i = 0; i < airoha_pinctrl_groups[group].npins; i++) {
		if (airoha_pinconf_get(pctrl_dev,
				       airoha_pinctrl_groups[group].pins[i],
				       config))
			return -ENOTSUPP;

		if (i && cur_config != *config)
			return -ENOTSUPP;

		cur_config = *config;
	}

	return 0;
}

static int airoha_pinconf_group_set(struct pinctrl_dev *pctrl_dev,
				    unsigned int group, unsigned long *configs,
				    unsigned int num_configs)
{
	int i;

	for (i = 0; i < airoha_pinctrl_groups[group].npins; i++) {
		int err;

		err = airoha_pinconf_set(pctrl_dev,
					 airoha_pinctrl_groups[group].pins[i],
					 configs, num_configs);
		if (err)
			return err;
	}

	return 0;
}

static const struct pinconf_ops airoha_confops = {
	.is_generic = true,
	.pin_config_get = airoha_pinconf_get,
	.pin_config_set = airoha_pinconf_set,
	.pin_config_group_get = airoha_pinconf_group_get,
	.pin_config_group_set = airoha_pinconf_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static const struct pinctrl_ops airoha_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinctrl_desc airoha_pinctrl_desc = {
	.name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.pctlops = &airoha_pctlops,
	.pmxops = &airoha_pmxops,
	.confops = &airoha_confops,
	.pins = airoha_pinctrl_pins,
	.npins = ARRAY_SIZE(airoha_pinctrl_pins),
};

static int airoha_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct airoha_pinctrl *pinctrl;
	struct regmap *map;
	int err, i;

	pinctrl = devm_kzalloc(dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	pinctrl->regmap = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pinctrl->regmap))
		return PTR_ERR(pinctrl->regmap);

	map = syscon_regmap_lookup_by_compatible("airoha,en7581-chip-scu");
	if (IS_ERR(map))
		return PTR_ERR(map);

	pinctrl->chip_scu = map;

	err = devm_pinctrl_register_and_init(dev, &airoha_pinctrl_desc,
					     pinctrl, &pinctrl->ctrl);
	if (err)
		return err;

	/* build pin groups */
	for (i = 0; i < ARRAY_SIZE(airoha_pinctrl_groups); i++) {
		const struct pingroup *grp = &airoha_pinctrl_groups[i];

		err = pinctrl_generic_add_group(pinctrl->ctrl, grp->name,
						grp->pins, grp->npins,
						(void *)grp);
		if (err < 0) {
			dev_err(&pdev->dev, "Failed to register group %s\n",
				grp->name);
			return err;
		}
	}

	/* build functions */
	for (i = 0; i < ARRAY_SIZE(airoha_pinctrl_funcs); i++) {
		const struct airoha_pinctrl_func *func;

		func = &airoha_pinctrl_funcs[i];
		err = pinmux_generic_add_pinfunction(pinctrl->ctrl,
						     &func->desc,
						     (void *)func);
		if (err < 0) {
			dev_err(dev, "Failed to register function %s\n",
				func->desc.name);
			return err;
		}
	}

	err = pinctrl_enable(pinctrl->ctrl);
	if (err)
		return err;

	/* build gpio-chip */
	return airoha_pinctrl_add_gpiochip(pinctrl, pdev);
}

static const struct of_device_id airoha_pinctrl_of_match[] = {
	{ .compatible = "airoha,en7581-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, airoha_pinctrl_of_match);

static struct platform_driver airoha_pinctrl_driver = {
	.probe = airoha_pinctrl_probe,
	.driver = {
		.name = "pinctrl-airoha",
		.of_match_table = airoha_pinctrl_of_match,
	},
};
module_platform_driver(airoha_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Markus Gothe <markus.gothe@genexis.eu>");
MODULE_DESCRIPTION("Pinctrl driver for Airoha SoC");
