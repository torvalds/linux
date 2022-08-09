// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Thunder Bay SOC pinctrl/GPIO driver
 *
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"
#include "pinmux.h"

/* Bit 0:2 and 4:6 should be used for mode selection */
#define THB_GPIO_PINMUX_MODE_0			0x00
#define THB_GPIO_PINMUX_MODE_1			0x11
#define THB_GPIO_PINMUX_MODE_2			0x22
#define THB_GPIO_PINMUX_MODE_3			0x33
#define THB_GPIO_PINMUX_MODE_4			0x44

#define THB_GPIO_PORT_SELECT_MASK		BIT(8)
#define THB_GPIO_PAD_DIRECTION_MASK		BIT(10)
#define THB_GPIO_SPU_MASK			BIT(11)
#define THB_GPIO_PULL_ENABLE_MASK		BIT(12)
#define THB_GPIO_PULL_UP_MASK			BIT(13)
#define THB_GPIO_PULL_DOWN_MASK			BIT(14)
#define THB_GPIO_ENAQ_MASK			BIT(15)
/* bit 16-19: Drive Strength for the Pad */
#define THB_GPIO_DRIVE_STRENGTH_MASK		(0xF0000)
#define THB_GPIO_SLEW_RATE_MASK			BIT(20)
#define THB_GPIO_SCHMITT_TRIGGER_MASK		BIT(21)

#define THB_GPIO_REG_OFFSET(pin_num)			((pin_num) * (0x4))
#define THB_MAX_MODE_SUPPORTED				(5u)
#define THB_MAX_NPINS_SUPPORTED				(67u)

/* store Pin status */
static u32 thb_pinx_status[THB_MAX_NPINS_SUPPORTED];

struct thunderbay_mux_desc {
	u8 mode;
	const char *name;
};

#define THUNDERBAY_PIN_DESC(pin_number, pin_name, ...) {        \
	.number = pin_number,                           \
	.name = pin_name,                               \
	.drv_data = &(struct thunderbay_mux_desc[]) {   \
			__VA_ARGS__, { } },             \
}

#define THUNDERBAY_MUX(pin_mode, pin_function) {                \
	.mode = pin_mode,                               \
	.name = pin_function,                           \
}

struct thunderbay_pin_soc {
	const struct pinctrl_pin_desc           *pins;
	unsigned int                            npins;
};

/**
 * struct thunderbay_pinctrl - Intel Thunderbay pinctrl structure
 * @pctrl: Pointer to the pin controller device
 * @base0: First register base address
 * @dev: Pointer to the device structure
 * @chip: GPIO chip used by this pin controller
 * @soc: Pin control configuration data based on SoC
 * @ngroups: Number of pin groups available
 * @nfuncs: Number of pin functions available
 */
struct thunderbay_pinctrl {
	struct pinctrl_dev              *pctrl;
	void __iomem                    *base0;
	struct device                   *dev;
	struct gpio_chip                chip;
	const struct thunderbay_pin_soc *soc;
	unsigned int                    ngroups;
	unsigned int                    nfuncs;
};

static const struct pinctrl_pin_desc thunderbay_pins[] = {
	THUNDERBAY_PIN_DESC(0, "GPIO0",
			    THUNDERBAY_MUX(0X0, "I2C0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(1, "GPIO1",
			    THUNDERBAY_MUX(0X0, "I2C0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(2, "GPIO2",
			    THUNDERBAY_MUX(0X0, "I2C1_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(3, "GPIO3",
			    THUNDERBAY_MUX(0X0, "I2C1_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(4, "GPIO4",
			    THUNDERBAY_MUX(0X0, "I2C2_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(5, "GPIO5",
			    THUNDERBAY_MUX(0X0, "I2C2_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(6, "GPIO6",
			    THUNDERBAY_MUX(0X0, "I2C3_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(7, "GPIO7",
			    THUNDERBAY_MUX(0X0, "I2C3_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(8, "GPIO8",
			    THUNDERBAY_MUX(0X0, "I2C4_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(9, "GPIO9",
			    THUNDERBAY_MUX(0X0, "I2C4_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(10, "GPIO10",
			    THUNDERBAY_MUX(0X0, "UART0_M0"),
			    THUNDERBAY_MUX(0X1, "RT0_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(11, "GPIO11",
			    THUNDERBAY_MUX(0X0, "UART0_M0"),
			    THUNDERBAY_MUX(0X1, "RT0_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(12, "GPIO12",
			    THUNDERBAY_MUX(0X0, "UART0_M0"),
			    THUNDERBAY_MUX(0X1, "RT1_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(13, "GPIO13",
			    THUNDERBAY_MUX(0X0, "UART0_M0"),
			    THUNDERBAY_MUX(0X1, "RT1_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(14, "GPIO14",
			    THUNDERBAY_MUX(0X0, "UART1_M0"),
			    THUNDERBAY_MUX(0X1, "RT2_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "TRIGGER_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(15, "GPIO15",
			    THUNDERBAY_MUX(0X0, "UART1_M0"),
			    THUNDERBAY_MUX(0X1, "RT2_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "TRIGGER_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(16, "GPIO16",
			    THUNDERBAY_MUX(0X0, "UART1_M0"),
			    THUNDERBAY_MUX(0X1, "RT3_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(17, "GPIO17",
			    THUNDERBAY_MUX(0X0, "UART1_M0"),
			    THUNDERBAY_MUX(0X1, "RT3_DSU_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(18, "GPIO18",
			    THUNDERBAY_MUX(0X0, "SPI0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(19, "GPIO19",
			    THUNDERBAY_MUX(0X0, "SPI0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(20, "GPIO20",
			    THUNDERBAY_MUX(0X0, "SPI0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_TRACE_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(21, "GPIO21",
			    THUNDERBAY_MUX(0X0, "SPI0_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_TRACE_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(22, "GPIO22",
			    THUNDERBAY_MUX(0X0, "SPI1_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M0"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(23, "GPIO23",
			    THUNDERBAY_MUX(0X0, "SPI1_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(24, "GPIO24",
			    THUNDERBAY_MUX(0X0, "SPI1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_TRACE_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(25, "GPIO25",
			    THUNDERBAY_MUX(0X0, "SPI1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_TRACE_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(26, "GPIO26",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(27, "GPIO27",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(28, "GPIO28",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(29, "GPIO29",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(30, "GPIO30",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(31, "GPIO31",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(32, "GPIO32",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(33, "GPIO33",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(34, "GPIO34",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DIG_VIEW_0"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(35, "GPIO35",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DIG_VIEW_1"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(36, "GPIO36",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_0"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(37, "GPIO37",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_1"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(38, "GPIO38",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_2"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(39, "GPIO39",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(40, "GPIO40",
			    THUNDERBAY_MUX(0X0, "ETHER0_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(41, "GPIO41",
			    THUNDERBAY_MUX(0X0, "POWER_INTERRUPT_MAX_PLATFORM_POWER_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(42, "GPIO42",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(43, "GPIO43",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(44, "GPIO44",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(45, "GPIO45",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(46, "GPIO46",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(47, "GPIO47",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(48, "GPIO48",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(49, "GPIO49",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DEBUG_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(50, "GPIO50",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DIG_VIEW_0"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(51, "GPIO51",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "DIG_VIEW_1"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(52, "GPIO52",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_0"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(53, "GPIO53",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_1"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(54, "GPIO54",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_2"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(55, "GPIO55",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "CPR_IO_OUT_CLK_3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(56, "GPIO56",
			    THUNDERBAY_MUX(0X0, "ETHER1_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "POWER_INTERRUPT_ICCMAX_VDDD_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(57, "GPIO57",
			    THUNDERBAY_MUX(0X0, "POWER_INTERRUPT_ICCMAX_VPU_M0"),
			    THUNDERBAY_MUX(0X1, "TPIU_DATA_M1"),
			    THUNDERBAY_MUX(0X2, "TPIU_DATA_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(58, "GPIO58",
			    THUNDERBAY_MUX(0X0, "THERMTRIP_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(59, "GPIO59",
			    THUNDERBAY_MUX(0X0, "THERMTRIP_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(60, "GPIO60",
			    THUNDERBAY_MUX(0X0, "SMBUS_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(61, "GPIO61",
			    THUNDERBAY_MUX(0X0, "SMBUS_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "POWER_INTERRUPT_ICCMAX_VDDD_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(62, "GPIO62",
			    THUNDERBAY_MUX(0X0, "PLATFORM_RESET_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(63, "GPIO63",
			    THUNDERBAY_MUX(0X0, "PLATFORM_RESET_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(64, "GPIO64",
			    THUNDERBAY_MUX(0X0, "PLATFORM_SHUTDOWN_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(65, "GPIO65",
			    THUNDERBAY_MUX(0X0, "PLATFORM_SHUTDOWN_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
	THUNDERBAY_PIN_DESC(66, "GPIO66",
			    THUNDERBAY_MUX(0X0, "POWER_INTERRUPT_ICCMAX_MEDIA_M0"),
			    THUNDERBAY_MUX(0X1, "EMPTY_M1"),
			    THUNDERBAY_MUX(0X2, "EMPTY_M2"),
			    THUNDERBAY_MUX(0X3, "EMPTY_M3"),
			    THUNDERBAY_MUX(0X4, "GPIO_M4")),
};

static const struct thunderbay_pin_soc thunderbay_data = {
	.pins	= thunderbay_pins,
	.npins  = ARRAY_SIZE(thunderbay_pins),
};

static u32 thb_gpio_read_reg(struct gpio_chip *chip, unsigned int pinnr)
{
	struct thunderbay_pinctrl *tpc = gpiochip_get_data(chip);

	return readl(tpc->base0 + THB_GPIO_REG_OFFSET(pinnr));
}

static u32 thb_gpio_write_reg(struct gpio_chip *chip, unsigned int pinnr, u32 value)
{
	struct thunderbay_pinctrl *tpc = gpiochip_get_data(chip);

	writel(value, (tpc->base0 + THB_GPIO_REG_OFFSET(pinnr)));
	return 0;
}

static int thb_read_gpio_data(struct gpio_chip *chip, unsigned int offset, unsigned int pad_dir)
{
	int data_offset;
	u32 data_reg;

	/* as per GPIO Spec = pad_dir 0:input, 1:output */
	data_offset = 0x2000u + (offset / 32);
	if (!pad_dir)
		data_offset += 4;
	data_reg = thb_gpio_read_reg(chip, data_offset);

	return data_reg & BIT(offset % 32);
}

static int thb_write_gpio_data(struct gpio_chip *chip, unsigned int offset, unsigned int value)
{
	int data_offset;
	u32 data_reg;

	data_offset = 0x2000u + (offset / 32);

	data_reg = thb_gpio_read_reg(chip, data_offset);

	if (value > 0)
		data_reg |= BIT(offset % 32);
	else
		data_reg &= ~BIT(offset % 32);

	return thb_gpio_write_reg(chip, data_offset, data_reg);
}

static int thunderbay_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	u32 reg = thb_gpio_read_reg(chip, offset);

	/* Return direction only if configured as GPIO else negative error */
	if (reg & THB_GPIO_PORT_SELECT_MASK)
		return !(reg & THB_GPIO_PAD_DIRECTION_MASK);
	return -EINVAL;
}

static int thunderbay_gpio_set_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	u32 reg = thb_gpio_read_reg(chip, offset);

	/* set pin as input only if it is GPIO else error */
	if (reg & THB_GPIO_PORT_SELECT_MASK) {
		reg &= (~THB_GPIO_PAD_DIRECTION_MASK);
		thb_gpio_write_reg(chip, offset, reg);
		return 0;
	}
	return -EINVAL;
}

static void thunderbay_gpio_set_value(struct gpio_chip *chip, unsigned int offset, int value)
{
	u32 reg = thb_gpio_read_reg(chip, offset);

	/* update pin value only if it is GPIO-output else error */
	if ((reg & THB_GPIO_PORT_SELECT_MASK) && (reg & THB_GPIO_PAD_DIRECTION_MASK))
		thb_write_gpio_data(chip, offset, value);
}

static int thunderbay_gpio_set_direction_output(struct gpio_chip *chip,
						unsigned int offset, int value)
{
	u32 reg = thb_gpio_read_reg(chip, offset);

	/* set pin as output only if it is GPIO else error */
	if (reg & THB_GPIO_PORT_SELECT_MASK) {
		reg |= THB_GPIO_PAD_DIRECTION_MASK;
		thb_gpio_write_reg(chip, offset, reg);
		thunderbay_gpio_set_value(chip, offset, value);
		return 0;
	}
	return -EINVAL;
}

static int thunderbay_gpio_get_value(struct gpio_chip *chip, unsigned int offset)
{
	u32 reg = thb_gpio_read_reg(chip, offset);
	int gpio_dir = 0;

	/* Read pin value only if it is GPIO else error */
	if (reg & THB_GPIO_PORT_SELECT_MASK) {
		/* 0=in, 1=out */
		gpio_dir = (reg & THB_GPIO_PAD_DIRECTION_MASK) > 0;

		/* Returns negative value when pin is configured as PORT */
		return thb_read_gpio_data(chip, offset, gpio_dir);
	}
	return -EINVAL;
}

static int thunderbay_gpiochip_probe(struct thunderbay_pinctrl *tpc)
{
	struct gpio_chip *chip = &tpc->chip;
	int ret;

	chip->label		= dev_name(tpc->dev);
	chip->parent		= tpc->dev;
	chip->request		= gpiochip_generic_request;
	chip->free		= gpiochip_generic_free;
	chip->get_direction	= thunderbay_gpio_get_direction;
	chip->direction_input	= thunderbay_gpio_set_direction_input;
	chip->direction_output  = thunderbay_gpio_set_direction_output;
	chip->get		= thunderbay_gpio_get_value;
	chip->set               = thunderbay_gpio_set_value;
	chip->set_config	= gpiochip_generic_config;
	/* identifies the first GPIO number handled by this chip; or,
	 * if negative during registration, requests dynamic ID allocation.
	 * Please pass -1 as base to let gpiolib select the chip base in all possible cases.
	 * We want to get rid of the static GPIO number space in the long run.
	 */
	chip->base		= -1;
	/* Number of GPIOs handled by this controller; the last GPIO handled is (base + ngpio - 1)*/
	chip->ngpio		= THB_MAX_NPINS_SUPPORTED;

	/* Register/add Thunder Bay GPIO chip with Linux framework */
	ret = gpiochip_add_data(chip, tpc);
	if (ret)
		dev_err(tpc->dev, "Failed to add gpiochip\n");
	return ret;
}

static int thunderbay_request_gpio(struct pinctrl_dev *pctldev,
				   struct pinctrl_gpio_range *range,
				   unsigned int pin)
{
	struct thunderbay_pinctrl *tpc = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &tpc->chip;
	u32 reg = 0;

	if (thb_pinx_status[pin] == 0u) {
		reg = thb_gpio_read_reg(chip, pin);
		/* Updates PIN configuration as GPIO and sets GPIO to MODE-4*/
		reg |= (THB_GPIO_PORT_SELECT_MASK | THB_GPIO_PINMUX_MODE_4);
		thb_gpio_write_reg(chip, pin, reg);

		/* update pin status as busy */
		thb_pinx_status[pin] = 1u;

		return 0;
	}
	return -EINVAL;
}

static void thunderbay_free_gpio(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned int pin)
{
	struct thunderbay_pinctrl *tpc = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &tpc->chip;
	u32 reg = 0;

	if (thb_pinx_status[pin] == 1u) {
		reg = thb_gpio_read_reg(chip, pin);

		/* Updates PIN configuration from GPIO to PORT */
		reg &= (~THB_GPIO_PORT_SELECT_MASK);

		/* Change Port/gpio mode to default mode-0 */
		reg &= (~THB_GPIO_PINMUX_MODE_4);

		thb_gpio_write_reg(chip, pin, reg);

		/* update pin status as free */
		thb_pinx_status[pin] = 0u;
	}
}

static int thb_pinctrl_set_mux(struct pinctrl_dev *pctldev,
			       unsigned int func_select, unsigned int group_select)
{
	struct thunderbay_pinctrl *tpc = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &tpc->chip;
	struct function_desc *function;
	unsigned int i, pin_mode;
	struct group_desc *group;
	int ret = -EINVAL;
	u32 reg = 0u;

	group = pinctrl_generic_get_group(pctldev, group_select);
	if (!group)
		return -EINVAL;

	function = pinmux_generic_get_function(pctldev, func_select);
	if (!function)
		return -EINVAL;

	pin_mode = *(unsigned int *)(function->data);

	/* Change modes for pins in the selected group */
	for (i = 0; i < group->num_pins; i++) {
		reg = thb_gpio_read_reg(chip, group->pins[i]);

		switch (pin_mode) {
		case 0u:
			reg |= THB_GPIO_PINMUX_MODE_0;
			break;
		case 1u:
			reg |= THB_GPIO_PINMUX_MODE_1;
			break;
		case 2u:
			reg |= THB_GPIO_PINMUX_MODE_2;
			break;
		case 3u:
			reg |= THB_GPIO_PINMUX_MODE_3;
			break;
		case 4u:
			reg |= THB_GPIO_PINMUX_MODE_4;
			break;
		default:
			return -EINVAL;
		}

		ret = thb_gpio_write_reg(chip, group->pins[i], reg);
		if (~ret) {
			/* update pin status as busy */
			thb_pinx_status[group->pins[i]] = 1u;
		}
	}
	return ret;
}

static int thunderbay_build_groups(struct thunderbay_pinctrl *tpc)
{
	struct group_desc *thunderbay_groups;
	int i;

	tpc->ngroups = tpc->soc->npins;
	thunderbay_groups = devm_kcalloc(tpc->dev, tpc->ngroups,
					 sizeof(*thunderbay_groups), GFP_KERNEL);
	if (!thunderbay_groups)
		return -ENOMEM;

	for (i = 0; i < tpc->ngroups; i++) {
		struct group_desc *group = thunderbay_groups + i;
		const struct pinctrl_pin_desc *pin_info = thunderbay_pins + i;

		group->name = pin_info->name;
		group->pins = (int *)&pin_info->number;
		pinctrl_generic_add_group(tpc->pctrl, group->name,
					  group->pins, 1, NULL);
	}
	return 0;
}

static int thunderbay_add_functions(struct thunderbay_pinctrl *tpc, struct function_desc *funcs)
{
	int i;

	/* Assign the groups for each function */
	for (i = 0; i < tpc->nfuncs; i++) {
		struct function_desc *func = &funcs[i];
		const char **group_names;
		unsigned int grp_idx = 0;
		int j;

		group_names = devm_kcalloc(tpc->dev, func->num_group_names,
					   sizeof(*group_names), GFP_KERNEL);
		if (!group_names)
			return -ENOMEM;

		for (j = 0; j < tpc->soc->npins; j++) {
			const struct pinctrl_pin_desc *pin_info = &thunderbay_pins[j];
			struct thunderbay_mux_desc *pin_mux;

			for (pin_mux = pin_info->drv_data; pin_mux->name; pin_mux++) {
				if (!strcmp(pin_mux->name, func->name))
					group_names[grp_idx++] = pin_info->name;
			}
		}

		func->group_names = group_names;
	}

	/* Add all functions */
	for (i = 0; i < tpc->nfuncs; i++) {
		pinmux_generic_add_function(tpc->pctrl,
					    funcs[i].name,
					    funcs[i].group_names,
					    funcs[i].num_group_names,
					    funcs[i].data);
	}
	kfree(funcs);
	return 0;
}

static int thunderbay_build_functions(struct thunderbay_pinctrl *tpc)
{
	struct function_desc *thunderbay_funcs;
	void *ptr;
	int pin;

	/*
	 * Allocate maximum possible number of functions. Assume every pin
	 * being part of 8 (hw maximum) globally unique muxes.
	 */
	tpc->nfuncs = 0;
	thunderbay_funcs = kcalloc(tpc->soc->npins * 8,
				   sizeof(*thunderbay_funcs), GFP_KERNEL);
	if (!thunderbay_funcs)
		return -ENOMEM;

	/* Setup 1 function for each unique mux */
	for (pin = 0; pin < tpc->soc->npins; pin++) {
		const struct pinctrl_pin_desc *pin_info = thunderbay_pins + pin;
		struct thunderbay_mux_desc *pin_mux;

		for (pin_mux = pin_info->drv_data; pin_mux->name; pin_mux++) {
			struct function_desc *func;

			/* Check if we already have function for this mux */
			for (func = thunderbay_funcs; func->name; func++) {
				if (!strcmp(pin_mux->name, func->name)) {
					func->num_group_names++;
					break;
				}
			}

			if (!func->name) {
				func->name = pin_mux->name;
				func->num_group_names = 1;
				func->data = (int *)&pin_mux->mode;
				tpc->nfuncs++;
			}
		}
	}

	/* Reallocate memory based on actual number of functions */
	ptr = krealloc(thunderbay_funcs,
		       tpc->nfuncs * sizeof(*thunderbay_funcs), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	thunderbay_funcs = ptr;
	return thunderbay_add_functions(tpc, thunderbay_funcs);
}

static int thunderbay_pinconf_set_tristate(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg |= THB_GPIO_ENAQ_MASK;
	else
		reg &= ~THB_GPIO_ENAQ_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_tristate(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = (reg & THB_GPIO_ENAQ_MASK) > 0;

	return 0;
}

static int thunderbay_pinconf_set_pulldown(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg |= THB_GPIO_PULL_DOWN_MASK;
	else
		reg &= ~THB_GPIO_PULL_DOWN_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_pulldown(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg = 0;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_PULL_DOWN_MASK) > 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_set_pullup(struct thunderbay_pinctrl *tpc,
					 unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg &= ~THB_GPIO_PULL_UP_MASK;
	else
		reg |= THB_GPIO_PULL_UP_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_pullup(struct thunderbay_pinctrl *tpc,
					 unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_PULL_UP_MASK) == 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_set_opendrain(struct thunderbay_pinctrl *tpc,
					    unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg &= ~THB_GPIO_PULL_ENABLE_MASK;
	else
		reg |= THB_GPIO_PULL_ENABLE_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_opendrain(struct thunderbay_pinctrl *tpc,
					    unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_PULL_ENABLE_MASK) == 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_set_pushpull(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg |= THB_GPIO_PULL_ENABLE_MASK;
	else
		reg &= ~THB_GPIO_PULL_ENABLE_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_pushpull(struct thunderbay_pinctrl *tpc,
					   unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_PULL_ENABLE_MASK) > 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_set_drivestrength(struct thunderbay_pinctrl *tpc,
						unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);

	/* Drive Strength: 0x0 to 0xF */
	if (config <= 0xF) {
		reg = (reg | config);
		return thb_gpio_write_reg(chip, pin, reg);
	}

	return -EINVAL;
}

static int thunderbay_pinconf_get_drivestrength(struct thunderbay_pinctrl *tpc,
						unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	reg = (reg & THB_GPIO_DRIVE_STRENGTH_MASK) >> 16;
	*config = (reg > 0) ? reg : 0;

	return 0;
}

static int thunderbay_pinconf_set_schmitt(struct thunderbay_pinctrl *tpc,
					  unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg |= THB_GPIO_SCHMITT_TRIGGER_MASK;
	else
		reg &= ~THB_GPIO_SCHMITT_TRIGGER_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_schmitt(struct thunderbay_pinctrl *tpc,
					  unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_SCHMITT_TRIGGER_MASK) > 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_set_slew_rate(struct thunderbay_pinctrl *tpc,
					    unsigned int pin, u32 config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg = 0;

	reg = thb_gpio_read_reg(chip, pin);
	if (config > 0)
		reg |= THB_GPIO_SLEW_RATE_MASK;
	else
		reg &= ~THB_GPIO_SLEW_RATE_MASK;

	return thb_gpio_write_reg(chip, pin, reg);
}

static int thunderbay_pinconf_get_slew_rate(struct thunderbay_pinctrl *tpc,
					    unsigned int pin, u32 *config)
{
	struct gpio_chip *chip = &tpc->chip;
	u32 reg;

	reg = thb_gpio_read_reg(chip, pin);
	*config = ((reg & THB_GPIO_SLEW_RATE_MASK) > 0) ? 1 : 0;

	return 0;
}

static int thunderbay_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
				  unsigned long *config)
{
	struct thunderbay_pinctrl *tpc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		ret = thunderbay_pinconf_get_tristate(tpc, pin, &arg);
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = thunderbay_pinconf_get_pulldown(tpc, pin, &arg);
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ret = thunderbay_pinconf_get_pullup(tpc, pin, &arg);
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		ret = thunderbay_pinconf_get_opendrain(tpc, pin, &arg);
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		ret = thunderbay_pinconf_get_pushpull(tpc, pin, &arg);
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = thunderbay_pinconf_get_drivestrength(tpc, pin, &arg);
		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		ret = thunderbay_pinconf_get_schmitt(tpc, pin, &arg);
		break;

	case PIN_CONFIG_SLEW_RATE:
		ret = thunderbay_pinconf_get_slew_rate(tpc, pin, &arg);
		break;

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return ret;
}

static int thunderbay_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				  unsigned long *configs, unsigned int num_configs)
{
	struct thunderbay_pinctrl *tpc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int pinconf;
	int ret = 0;
	u32 arg;

	for (pinconf = 0; pinconf < num_configs; pinconf++) {
		param = pinconf_to_config_param(configs[pinconf]);
		arg = pinconf_to_config_argument(configs[pinconf]);

		switch (param) {
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			ret = thunderbay_pinconf_set_tristate(tpc, pin, arg);
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = thunderbay_pinconf_set_pulldown(tpc, pin, arg);
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = thunderbay_pinconf_set_pullup(tpc, pin, arg);
			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			ret = thunderbay_pinconf_set_opendrain(tpc, pin, arg);
			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			ret = thunderbay_pinconf_set_pushpull(tpc, pin, arg);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = thunderbay_pinconf_set_drivestrength(tpc, pin, arg);
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			ret = thunderbay_pinconf_set_schmitt(tpc, pin, arg);
			break;

		case PIN_CONFIG_SLEW_RATE:
			ret = thunderbay_pinconf_set_slew_rate(tpc, pin, arg);
			break;

		default:
			return -ENOTSUPP;
		}
	}
	return ret;
}

static const struct pinctrl_ops thunderbay_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name   = pinctrl_generic_get_group_name,
	.get_group_pins   = pinctrl_generic_get_group_pins,
	.dt_node_to_map   = pinconf_generic_dt_node_to_map_all,
	.dt_free_map	  = pinconf_generic_dt_free_map,
};

static const struct pinmux_ops thunderbay_pmxops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= thb_pinctrl_set_mux,
	.gpio_request_enable	= thunderbay_request_gpio,
	.gpio_disable_free	= thunderbay_free_gpio,
};

static const struct pinconf_ops thunderbay_confops = {
	.is_generic		= true,
	.pin_config_get		= thunderbay_pinconf_get,
	.pin_config_set		= thunderbay_pinconf_set,
};

static struct pinctrl_desc thunderbay_pinctrl_desc = {
	.name		= "thunderbay-pinmux",
	.pctlops	= &thunderbay_pctlops,
	.pmxops		= &thunderbay_pmxops,
	.confops	= &thunderbay_confops,
	.owner		= THIS_MODULE,
};

static const struct of_device_id thunderbay_pinctrl_match[] = {
	{
		.compatible = "intel,thunderbay-pinctrl",
		.data = &thunderbay_data
	},
	{}
};

static int thunderbay_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct thunderbay_pinctrl *tpc;
	struct resource *iomem;
	int ret;

	of_id = of_match_node(thunderbay_pinctrl_match, pdev->dev.of_node);
	if (!of_id)
		return -ENODEV;

	tpc = devm_kzalloc(dev, sizeof(*tpc), GFP_KERNEL);
	if (!tpc)
		return -ENOMEM;

	tpc->dev = dev;
	tpc->soc = of_id->data;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -ENXIO;

	tpc->base0 =  devm_ioremap_resource(dev, iomem);
	if (IS_ERR(tpc->base0))
		return PTR_ERR(tpc->base0);

	thunderbay_pinctrl_desc.pins = tpc->soc->pins;
	thunderbay_pinctrl_desc.npins = tpc->soc->npins;

	/* Register pinctrl */
	tpc->pctrl = devm_pinctrl_register(dev, &thunderbay_pinctrl_desc, tpc);
	if (IS_ERR(tpc->pctrl))
		return PTR_ERR(tpc->pctrl);

	/* Setup pinmux groups */
	ret = thunderbay_build_groups(tpc);
	if (ret)
		return ret;

	/* Setup pinmux functions */
	ret = thunderbay_build_functions(tpc);
	if (ret)
		return ret;

	/* Setup GPIO */
	ret = thunderbay_gpiochip_probe(tpc);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, tpc);

	return 0;
}

static int thunderbay_pinctrl_remove(struct platform_device *pdev)
{
	/* thunderbay_pinctrl_remove function to clear the assigned memory */
	return 0;
}

static struct platform_driver thunderbay_pinctrl_driver = {
	.driver = {
		.name = "thunderbay-pinctrl",
		.of_match_table = thunderbay_pinctrl_match,
	},
	.probe = thunderbay_pinctrl_probe,
	.remove = thunderbay_pinctrl_remove,
};

builtin_platform_driver(thunderbay_pinctrl_driver);

MODULE_AUTHOR("Lakshmi Sowjanya D <lakshmi.sowjanya.d@intel.com>");
MODULE_AUTHOR("Kiran Kumar S <kiran.kumar1.s@intel.com>");
MODULE_DESCRIPTION("Intel Thunder Bay Pinctrl/GPIO Driver");
MODULE_LICENSE("GPL v2");
