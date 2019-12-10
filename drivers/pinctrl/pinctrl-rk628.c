// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl driver for Rockchip RK628
 *
 * Copyright (c) 2019, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Weixin Zhou <zwx@rock-chips.com>
 *
 * Based on the pinctrl-rk805/pinctrl-rockchip driver
 */

#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/rk628.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define GPIO0_BASE	0xD0000
#define GPIO1_BASE	0xE0000
#define GPIO2_BASE	0xF0000
#define GPIO3_BASE	0x100000
#define GPIO_MAX_REGISTER	(GPIO3_BASE + GPIO_VER_ID)

/* GPIO control registers */
#define GPIO_SWPORT_DR_L	0x00
#define GPIO_SWPORT_DR_H	0x04
#define GPIO_SWPORT_DDR_L	0x08
#define GPIO_SWPORT_DDR_H	0x0c
#define GPIO_INTEN_L		0x10
#define GPIO_INTEN_H		0x14
#define GPIO_INTMASK_L		0x18
#define GPIO_INTMASK_H		0x1c
#define GPIO_INTTYPE_L		0x20
#define GPIO_INTTYPE_H		0x24
#define GPIO_INT_POLARITY_L	0x28
#define GPIO_INT_POLARITY_H	0x2c
#define GPIO_INT_BOTHEDGE_L	0x30
#define GPIO_INT_BOTHEDGE_H	0x34
#define GPIO_DEBOUNCE_L		0x38
#define GPIO_DEBOUNCE_H		0x3c
#define GPIO_DBCLK_DIV_EN_L	0x40
#define GPIO_DBCLK_DIV_EN_H	0x44
#define GPIO_INT_STATUS		0x50
#define GPIO_INT_RAWSTATUS	0x58
#define GPIO_PORTS_EOI_L	0x60
#define GPIO_PORTS_EOI_H	0x64
#define GPIO_EXT_PORT		0x70
#define GPIO_VER_ID		0x78

#define GPIO_REG_LOW		0x0
#define GPIO_REG_HIGH		0x1

/* GPIO control registers */
#define GPIO_INTMASK		0x34
#define GPIO_PORTS_EOI		0x4c

/* easy to map ioctrl reg, 0-159 used by rockchips pinctrl. */
#define PINBASE 384
#define BANK_OFFSET 32
#define GPIO0_PINBASE PINBASE
#define GPIO1_PINBASE (PINBASE + BANK_OFFSET)
#define GPIO2_PINBASE (PINBASE + 2 * BANK_OFFSET)
#define GPIO3_PINBASE (PINBASE + 3 * BANK_OFFSET)
/*for logic input select inside the chip*/
#define LOGIC_PINBASE (PINBASE + 4 * BANK_OFFSET)

#define IRQ_CHIP(fname)						\
	[IRQCHIP_##fname] = {					\
		.name = "rk628-"#fname,				\
		.irq_bus_lock		= rk628_irq_lock,	\
		.irq_bus_sync_unlock	= rk628_irq_sync_unlock,\
		.irq_disable		= rk628_irq_disable,	\
		.irq_enable		= rk628_irq_enable,	\
		.irq_set_type		= rk628_irq_set_type,	\
	}

struct rk628_pin_function {
	const char *name;
	const char **groups;
	unsigned int ngroups;
	int mux_option;
};

struct rk628_pin_group {
	const char *name;
	const unsigned int pins[1];
	unsigned int npins;
	int iomux_base;
};

struct rk628_pin_bank {
	char *name;
	u32 reg_base;
	u32 nr_pins;
	u32 pin_base;
	struct device_node *of_node;
	struct pinctrl_gpio_range grange;
	struct gpio_chip gpio_chip;
	struct clk *clk;
	struct rk628_pctrl_info *pci;

	struct irq_chip irq_chip;
	struct irq_domain *domain;
	int irq;
	struct mutex lock;
	unsigned int mask_regs[2];
	unsigned int polarity_regs[2];
	unsigned int level_regs[2];
	unsigned int bothedge_regs[2];
};

struct rk628_pctrl_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pinctrl_desc;
	const struct rk628_pin_function *functions;
	unsigned int num_functions;
	const struct rk628_pin_group *groups;
	int num_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;
	struct regmap *regmap;
	struct regmap *grf_regmap;
	struct rk628_pin_bank *pin_banks;
	u32 nr_banks;
};

enum rk628_pinmux_option {
	RK628_PINMUX_FUNC0,
	RK628_PINMUX_FUNC1,
};

#define RK628_PINCTRL_GROUP(a, b, c, d) { .name = a, .pins = b, .npins = c, .iomux_base = d}
#define RK628_PINCTRL_BANK(a, b, c, d) { .name = a, .reg_base = b, .nr_pins = c, .pin_base = d}

enum rk628_functions {
	RK628_MUX_GPIO,
};

static const char *gpio_groups[] = {
	"gpio0a0", "gpio0a1", "gpio0a2", "gpio0a3", "gpio0a4", "gpio0a5",
	"gpio0a6", "gpio0a7", "gpio0b0", "gpio0b1", "gpio0b2", "gpio0b3",
	"gpio1a0", "gpio1a1", "gpio1a2", "gpio1a3", "gpio1a4", "gpio1a5",
	"gpio1a6", "gpio1a7", "gpio1b0", "gpio1b1", "gpio1b2", "gpio1b3",
	"gpio1b4", "gpio1b5",
	"gpio2a0", "gpio2a1", "gpio2a2", "gpio2a3", "gpio2a4", "gpio2a5",
	"gpio2a6", "gpio2a7", "gpio2b0", "gpio2b1", "gpio2b2", "gpio2b3",
	"gpio2b4", "gpio2b5", "gpio2b6", "gpio2b7", "gpio2c0", "gpio2c1",
	"gpio2c3", "gpio2c4", "gpio2c5", "gpio2c6", "gpio2c7", "gpio1a0",
	"gpio3a1", "gpio3a2", "gpio3a3", "gpio3a4", "gpio3a5", "gpio3a6",
	"gpio3a7", "gpio3b0", "gpio3b1", "gpio3b2", "gpio3b3", "gpio3b4",
};

static struct rk628_pin_function rk628_functions[] = {
	{
		.name = "gpio",
		.groups = gpio_groups,
		.ngroups = ARRAY_SIZE(gpio_groups),
	},
};

enum {
	RK628_GPIO_HIGH_Z,
	RK628_GPIO_PULL_UP,
	RK628_GPIO_PULL_DOWN,
};

enum {
	RK628_GPIO0_A0 = GPIO0_PINBASE,
	RK628_GPIO0_A1,
	RK628_GPIO0_A2,
	RK628_GPIO0_A3,
	RK628_GPIO0_A4,
	RK628_GPIO0_A5,
	RK628_GPIO0_A6,
	RK628_GPIO0_A7,
	RK628_GPIO0_B0,
	RK628_GPIO0_B1,
	RK628_GPIO0_B2,
	RK628_GPIO0_B3,
	RK628_GPIO1_A0 = GPIO1_PINBASE,
	RK628_GPIO1_A1,
	RK628_GPIO1_A2,
	RK628_GPIO1_A3,
	RK628_GPIO1_A4,
	RK628_GPIO1_A5,
	RK628_GPIO1_A6,
	RK628_GPIO1_A7,
	RK628_GPIO1_B0,
	RK628_GPIO1_B1,
	RK628_GPIO1_B2,
	RK628_GPIO1_B3,
	RK628_GPIO1_B4,
	RK628_GPIO1_B5,
	RK628_GPIO2_A0 = GPIO2_PINBASE,
	RK628_GPIO2_A1,
	RK628_GPIO2_A2,
	RK628_GPIO2_A3,
	RK628_GPIO2_A4,
	RK628_GPIO2_A5,
	RK628_GPIO2_A6,
	RK628_GPIO2_A7,
	RK628_GPIO2_B0,
	RK628_GPIO2_B1,
	RK628_GPIO2_B2,
	RK628_GPIO2_B3,
	RK628_GPIO2_B4,
	RK628_GPIO2_B5,
	RK628_GPIO2_B6,
	RK628_GPIO2_B7,
	RK628_GPIO2_C0,
	RK628_GPIO2_C1,
	RK628_GPIO2_C2,
	RK628_GPIO2_C3,
	RK628_GPIO2_C4,
	RK628_GPIO2_C5,
	RK628_GPIO2_C6,
	RK628_GPIO2_C7,
	RK628_GPIO3_A0 = GPIO3_PINBASE,
	RK628_GPIO3_A1,
	RK628_GPIO3_A2,
	RK628_GPIO3_A3,
	RK628_GPIO3_A4,
	RK628_GPIO3_A5,
	RK628_GPIO3_A6,
	RK628_GPIO3_A7,
	RK628_GPIO3_B0,
	RK628_GPIO3_B1,
	RK628_GPIO3_B2,
	RK628_GPIO3_B3,
	RK628_GPIO3_B4,
	RK628_I2SM_SCK = (LOGIC_PINBASE + 2),
	RK628_I2SM_D,
	RK628_I2SM_LR,
	RK628_RXDDC_SCL,
	RK628_RXDDC_SDA,
	RK628_HDMIRX_CE,
};

static struct pinctrl_pin_desc rk628_pins_desc[] = {
	PINCTRL_PIN(RK628_GPIO0_A0, "gpio0a0"),
	PINCTRL_PIN(RK628_GPIO0_A1, "gpio0a1"),
	PINCTRL_PIN(RK628_GPIO0_A2, "gpio0a2"),
	PINCTRL_PIN(RK628_GPIO0_A3, "gpio0a3"),
	PINCTRL_PIN(RK628_GPIO0_A4, "gpio0a4"),
	PINCTRL_PIN(RK628_GPIO0_A5, "gpio0a5"),
	PINCTRL_PIN(RK628_GPIO0_A6, "gpio0a6"),
	PINCTRL_PIN(RK628_GPIO0_A7, "gpio0a7"),
	PINCTRL_PIN(RK628_GPIO0_B0, "gpio0b0"),
	PINCTRL_PIN(RK628_GPIO0_B1, "gpio0b1"),
	PINCTRL_PIN(RK628_GPIO0_B2, "gpio0b2"),
	PINCTRL_PIN(RK628_GPIO0_B3, "gpio0b3"),

	PINCTRL_PIN(RK628_GPIO1_A0, "gpio1a0"),
	PINCTRL_PIN(RK628_GPIO1_A1, "gpio1a1"),
	PINCTRL_PIN(RK628_GPIO1_A2, "gpio1a2"),
	PINCTRL_PIN(RK628_GPIO1_A3, "gpio1a3"),
	PINCTRL_PIN(RK628_GPIO1_A4, "gpio1a4"),
	PINCTRL_PIN(RK628_GPIO1_A5, "gpio1a5"),
	PINCTRL_PIN(RK628_GPIO1_A6, "gpio1a6"),
	PINCTRL_PIN(RK628_GPIO1_A7, "gpio1a7"),
	PINCTRL_PIN(RK628_GPIO1_B0, "gpio1b0"),
	PINCTRL_PIN(RK628_GPIO1_B1, "gpio1b1"),
	PINCTRL_PIN(RK628_GPIO1_B2, "gpio1b2"),
	PINCTRL_PIN(RK628_GPIO1_B3, "gpio1b3"),
	PINCTRL_PIN(RK628_GPIO1_B4, "gpio1b4"),
	PINCTRL_PIN(RK628_GPIO1_B5, "gpio1b5"),

	PINCTRL_PIN(RK628_GPIO2_A0, "gpio2a0"),
	PINCTRL_PIN(RK628_GPIO2_A1, "gpio2a1"),
	PINCTRL_PIN(RK628_GPIO2_A2, "gpio2a2"),
	PINCTRL_PIN(RK628_GPIO2_A3, "gpio2a3"),
	PINCTRL_PIN(RK628_GPIO2_A4, "gpio2a4"),
	PINCTRL_PIN(RK628_GPIO2_A5, "gpio2a5"),
	PINCTRL_PIN(RK628_GPIO2_A6, "gpio2a6"),
	PINCTRL_PIN(RK628_GPIO2_A7, "gpio2a7"),
	PINCTRL_PIN(RK628_GPIO2_B0, "gpio2b0"),
	PINCTRL_PIN(RK628_GPIO2_B1, "gpio2b1"),
	PINCTRL_PIN(RK628_GPIO2_B2, "gpio2b2"),
	PINCTRL_PIN(RK628_GPIO2_B3, "gpio2b3"),
	PINCTRL_PIN(RK628_GPIO2_B4, "gpio2b4"),
	PINCTRL_PIN(RK628_GPIO2_B5, "gpio2b5"),
	PINCTRL_PIN(RK628_GPIO2_B6, "gpio2b6"),
	PINCTRL_PIN(RK628_GPIO2_B7, "gpio2b7"),
	PINCTRL_PIN(RK628_GPIO2_C0, "gpio2c0"),
	PINCTRL_PIN(RK628_GPIO2_C1, "gpio2c1"),
	PINCTRL_PIN(RK628_GPIO2_C2, "gpio2c2"),
	PINCTRL_PIN(RK628_GPIO2_C3, "gpio2c3"),
	PINCTRL_PIN(RK628_GPIO2_C4, "gpio2c4"),
	PINCTRL_PIN(RK628_GPIO2_C5, "gpio2c5"),
	PINCTRL_PIN(RK628_GPIO2_C6, "gpio2c6"),
	PINCTRL_PIN(RK628_GPIO2_C7, "gpio2c7"),

	PINCTRL_PIN(RK628_GPIO3_A0, "gpio3a0"),
	PINCTRL_PIN(RK628_GPIO3_A1, "gpio3a1"),
	PINCTRL_PIN(RK628_GPIO3_A2, "gpio3a2"),
	PINCTRL_PIN(RK628_GPIO3_A3, "gpio3a3"),
	PINCTRL_PIN(RK628_GPIO3_A4, "gpio3a4"),
	PINCTRL_PIN(RK628_GPIO3_A5, "gpio3a5"),
	PINCTRL_PIN(RK628_GPIO3_A6, "gpio3a6"),
	PINCTRL_PIN(RK628_GPIO3_A7, "gpio3a7"),
	PINCTRL_PIN(RK628_GPIO3_B0, "gpio3b0"),
	PINCTRL_PIN(RK628_GPIO3_B1, "gpio3b1"),
	PINCTRL_PIN(RK628_GPIO3_B2, "gpio3b2"),
	PINCTRL_PIN(RK628_GPIO3_B3, "gpio3b3"),
	PINCTRL_PIN(RK628_GPIO3_B4, "gpio3b4"),

	PINCTRL_PIN(RK628_I2SM_SCK, "i2sm_sck"),
	PINCTRL_PIN(RK628_I2SM_D, "i2sm_d"),
	PINCTRL_PIN(RK628_I2SM_LR, "i2sm_lr"),
	PINCTRL_PIN(RK628_RXDDC_SCL, "rxddc_scl"),
	PINCTRL_PIN(RK628_RXDDC_SDA, "rxddc_sda"),
	PINCTRL_PIN(RK628_HDMIRX_CE, "hdmirx_cec"),
};

static const struct rk628_pin_group rk628_pin_groups[] = {
	RK628_PINCTRL_GROUP("gpio0a0", { RK628_GPIO0_A0 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a1", { RK628_GPIO0_A1 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a2", { RK628_GPIO0_A2 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a3", { RK628_GPIO0_A3 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a4", { RK628_GPIO0_A4 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a5", { RK628_GPIO0_A5 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a6", { RK628_GPIO0_A6 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0a7", { RK628_GPIO0_A7 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0b0", { RK628_GPIO0_B0 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0b1", { RK628_GPIO0_B1 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0b2", { RK628_GPIO0_B2 }, 1, GRF_GPIO0AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio0b3", { RK628_GPIO0_B3 }, 1, GRF_GPIO0AB_SEL_CON),

	RK628_PINCTRL_GROUP("gpio1a0", { RK628_GPIO1_A0 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a1", { RK628_GPIO1_A1 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a2", { RK628_GPIO1_A2 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a3", { RK628_GPIO1_A3 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a4", { RK628_GPIO1_A4 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a5", { RK628_GPIO1_A5 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a6", { RK628_GPIO1_A6 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1a7", { RK628_GPIO1_A7 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b0", { RK628_GPIO1_B0 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b1", { RK628_GPIO1_B1 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b2", { RK628_GPIO1_B2 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b3", { RK628_GPIO1_B3 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b4", { RK628_GPIO1_B4 }, 1, GRF_GPIO1AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio1b5", { RK628_GPIO1_B5 }, 1, GRF_GPIO1AB_SEL_CON),

	RK628_PINCTRL_GROUP("gpio2a0", { RK628_GPIO2_A0 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a1", { RK628_GPIO2_A1 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a2", { RK628_GPIO2_A2 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a3", { RK628_GPIO2_A3 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a4", { RK628_GPIO2_A4 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a5", { RK628_GPIO2_A5 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a6", { RK628_GPIO2_A6 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2a7", { RK628_GPIO2_A7 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b0", { RK628_GPIO2_B0 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b1", { RK628_GPIO2_B1 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b2", { RK628_GPIO2_B2 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b3", { RK628_GPIO2_B3 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b4", { RK628_GPIO2_B4 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b5", { RK628_GPIO2_B5 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b6", { RK628_GPIO2_B6 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2b7", { RK628_GPIO2_B7 }, 1, GRF_GPIO2AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c0", { RK628_GPIO2_C0 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c1", { RK628_GPIO2_C1 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c2", { RK628_GPIO2_C2 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c3", { RK628_GPIO2_C3 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c4", { RK628_GPIO2_C4 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c5", { RK628_GPIO2_C5 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c6", { RK628_GPIO2_C6 }, 1, GRF_GPIO2C_SEL_CON),
	RK628_PINCTRL_GROUP("gpio2c7", { RK628_GPIO2_C7 }, 1, GRF_GPIO2C_SEL_CON),

	RK628_PINCTRL_GROUP("gpio3a0", { RK628_GPIO3_A0 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a1", { RK628_GPIO3_A1 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a2", { RK628_GPIO3_A2 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a3", { RK628_GPIO3_A3 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a4", { RK628_GPIO3_A4 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a5", { RK628_GPIO3_A5 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a6", { RK628_GPIO3_A6 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3a7", { RK628_GPIO3_A7 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3b0", { RK628_GPIO3_B0 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3b1", { RK628_GPIO3_B1 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3b2", { RK628_GPIO3_B2 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3b3", { RK628_GPIO3_B3 }, 1, GRF_GPIO3AB_SEL_CON),
	RK628_PINCTRL_GROUP("gpio3b4", { RK628_GPIO3_B4 }, 1, GRF_GPIO3AB_SEL_CON),

	RK628_PINCTRL_GROUP("i2sm_sck", { RK628_I2SM_SCK }, 1, GRF_SYSTEM_CON3),
	RK628_PINCTRL_GROUP("i2sm_d", { RK628_I2SM_D }, 1, GRF_SYSTEM_CON3),
	RK628_PINCTRL_GROUP("i2sm_lr", { RK628_I2SM_LR }, 1, GRF_SYSTEM_CON3),
	RK628_PINCTRL_GROUP("rxddc_scl", { RK628_RXDDC_SCL }, 1, GRF_SYSTEM_CON3),
	RK628_PINCTRL_GROUP("rxddc_sda", { RK628_RXDDC_SDA }, 1, GRF_SYSTEM_CON3),
	RK628_PINCTRL_GROUP("hdmirx_cec", { RK628_HDMIRX_CE }, 1, GRF_SYSTEM_CON3),
};

static struct rk628_pin_bank rk628_pin_banks[] = {
	RK628_PINCTRL_BANK("rk628-gpio0", GPIO0_BASE, 12, GPIO0_PINBASE),
	RK628_PINCTRL_BANK("rk628-gpio1", GPIO1_BASE, 14, GPIO1_PINBASE),
	RK628_PINCTRL_BANK("rk628-gpio2", GPIO2_BASE, 24, GPIO2_PINBASE),
	RK628_PINCTRL_BANK("rk628-gpio3", GPIO3_BASE, 13, GPIO3_PINBASE),
};

/* generic gpio chip */
static int rk628_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rk628_pin_bank *bank = gpiochip_get_data(chip);
	struct rk628_pctrl_info *pci = bank->pci;
	int data_reg, val, ret;

	data_reg = bank->reg_base + GPIO_EXT_PORT;

	clk_enable(bank->clk);
	ret = regmap_read(pci->regmap, data_reg, &val);
	if (ret)
		dev_err(pci->dev, "%s: regmap read failed!\n", __func__);
	clk_disable(bank->clk);

	val >>= offset;
	val &= 1;
	dev_dbg(pci->dev, "%s bank->name=%s dir_reg=0x%x offset=%x value=%x\n",
		__func__, bank->name, data_reg, offset, val);

	return val;
}

static void rk628_gpio_set(struct gpio_chip *chip,
			   unsigned int offset,
			   int value)
{
	struct rk628_pin_bank *bank = gpiochip_get_data(chip);
	struct rk628_pctrl_info *pci = bank->pci;
	int data_reg, val, ret;

	if (offset / 16) {
		data_reg = bank->reg_base + GPIO_SWPORT_DR_H;
		offset -= 16;
	} else {
		data_reg = bank->reg_base + GPIO_SWPORT_DR_L;
	}
	if (value)
		val = BIT(offset + 16) | BIT(offset);
	else
		val = BIT(offset + 16) | (0xffff & ~BIT(offset));

	clk_enable(bank->clk);
	ret = regmap_write(pci->regmap, data_reg, val);
	if (ret)
		pr_err("%s: regmap write failed! bank->name=%s data_reg=0x%x offset=%d\n",
		       __func__, bank->name, data_reg, offset);
	clk_disable(bank->clk);
}

static int rk628_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int rk628_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset,
				       int value)
{
	rk628_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int rk628_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rk628_pin_bank *bank = gpiochip_get_data(chip);
	struct rk628_pctrl_info *pci = bank->pci;
	int dir_reg, val, ret;

	if (offset / 16) {
		dir_reg = bank->reg_base + GPIO_SWPORT_DDR_H;
		offset -= 16;
	} else {
		dir_reg = bank->reg_base + GPIO_SWPORT_DDR_L;
	}

	clk_enable(bank->clk);
	ret = regmap_read(pci->regmap, dir_reg, &val);
	if (ret)
		dev_err(pci->dev, "%s: regmap read failed!\n", __func__);
	clk_disable(bank->clk);

	val = BIT(offset) & val;

	return !val;
}

static int rk628_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct rk628_pin_bank *bank = gpiochip_get_data(gc);
	int virq;

	if (!bank->domain)
		return -ENXIO;

	virq = irq_create_mapping(bank->domain, offset);
	if (!virq)
		pr_err("map interruptr fail, bank->irq=%d\n", bank->irq);

	return (virq) ? : -ENXIO;
}

static struct gpio_chip rk628_gpiolib_chip = {
	.label			= "rk628-gpio",
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.get_direction		= rk628_gpio_get_direction,
	.get			= rk628_gpio_get,
	.set			= rk628_gpio_set,
	.direction_input	= rk628_gpio_direction_input,
	.direction_output	= rk628_gpio_direction_output,
	.to_irq			= rk628_gpio_to_irq,
	.can_sleep              = true,
	.base			= -1,
	.owner			= THIS_MODULE,
};

/* generic pinctrl */
static int rk628_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_groups;
}

static const char *rk628_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->groups[group].name;
}

static int rk628_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*pins = pci->groups[group].pins;
	*num_pins = pci->groups[group].npins;

	return 0;
}

static const struct pinctrl_ops rk628_pinctrl_ops = {
	.get_groups_count = rk628_pinctrl_get_groups_count,
	.get_group_name = rk628_pinctrl_get_group_name,
	.get_group_pins = rk628_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int rk628_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_functions;
}

static const char *rk628_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned int function)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->functions[function].name;
}

static int rk628_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 const char *const **groups,
					 unsigned int *const num_groups)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*groups = pci->functions[function].groups;
	*num_groups = pci->functions[function].ngroups;

	return 0;
}

static int rk628_calc_mux_offset(struct rk628_pctrl_info *pci, int mux, int reg, int offset)
{
	int val = 0, orig;

	switch (reg) {
	case GRF_SYSTEM_CON3:
		regmap_read(pci->grf_regmap, reg, &orig);
		if (mux)
			val = BIT(offset) | orig;
		else
			val = ~BIT(offset) & orig;
		break;
	case GRF_GPIO0AB_SEL_CON:
		if (offset >= 4 && offset < 8) {
			offset += offset - 4;
			val = 0x3 << (offset + 16) | (mux ? BIT(offset) : 0);
		} else if (offset > 7) {
			offset += 4;
			val = BIT(offset + 16) | (mux ? BIT(offset) : 0);
		} else {
			val = BIT(offset + 16) | (mux ? BIT(offset) : 0);
		}
		break;
	case GRF_GPIO1AB_SEL_CON:
		if (offset == 13)
			offset++;
		if (offset > 11)
			val = 0x3 << (offset + 16) | (mux ? BIT(offset) : 0);
		else
			val = BIT(offset + 16) | (mux ? BIT(offset) : 0);
		break;
	case GRF_GPIO2AB_SEL_CON:
		val = BIT(offset + 16) | (mux ? BIT(offset) : 0);
		break;
	case GRF_GPIO2C_SEL_CON:
		offset -= 16;
		val = 0x3 << ((offset*2) + 16) | (mux ? BIT(offset*2) : 0);
		break;
	case GRF_GPIO3AB_SEL_CON:
		if (offset > 11)
			val = 0x3 << (offset + 16) | (mux ? BIT(offset) : 0);
		else
			val = BIT(offset + 16) | (mux ? BIT(offset) : 0);
		break;
	default:
		break;
	}

	return val;
}

static int rk628_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	int ret, val;
	int mux = pci->functions[func_selector].mux_option;
	int offset = pci->groups[group_selector].pins[0] % BANK_OFFSET;
	int reg = pci->groups[group_selector].iomux_base;

	dev_dbg(pci->dev, "functions[%d]:%s mux=%s\n",
		func_selector, pci->functions[func_selector].name,
		mux ? "func" : "gpio");

	val = rk628_calc_mux_offset(pci, mux, reg, offset);

	dev_dbg(pci->dev, "groups[%d]:%s pin-number=%d reg=0x%x write-val=0x%8x\n",
		group_selector,
		pci->groups[group_selector].name,
		pci->groups[group_selector].pins[0],
		reg, val);

	ret = regmap_write(pci->grf_regmap, reg, val);
	if (ret)
		dev_err(pci->dev, "%s regmap write failed!\n", __func__);

	return ret;
}

static int rk628_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int offset, bool input)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip;
	struct rk628_pin_bank *bank;
	int pin_offset, dir_reg, val, ret;

	chip = range->gc;
	bank = gpiochip_get_data(chip);
	pin_offset = offset - bank->pin_base;

	if (pin_offset / 16) {
		dir_reg = bank->reg_base + GPIO_SWPORT_DDR_H;
		pin_offset -= 16;
	} else {
		dir_reg = bank->reg_base + GPIO_SWPORT_DDR_L;
	}
	if (input)
		val = BIT(pin_offset + 16) | (0xffff & ~BIT(pin_offset));
	else
		val = BIT(pin_offset + 16) | BIT(pin_offset);

	clk_enable(bank->clk);
	ret = regmap_write(pci->regmap, dir_reg, val);
	if (ret)
		dev_err(pci->dev, "regmap update failed!\n");
	clk_disable(bank->clk);

	return 0;
}

static const struct pinmux_ops rk628_pinmux_ops = {
	.get_functions_count	= rk628_pinctrl_get_funcs_count,
	.get_function_name	= rk628_pinctrl_get_func_name,
	.get_function_groups	= rk628_pinctrl_get_func_groups,
	.set_mux		= rk628_pinctrl_set_mux,
	.gpio_set_direction	= rk628_pmx_gpio_set_direction,
};

static int rk628_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned int pin, unsigned long *config)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg = 0;

	pr_err("no support %s\n", __func__);

	switch (param) {
	case PIN_CONFIG_OUTPUT:
		break;
	default:
		dev_err(pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, (u16)arg);

	return 0;
}

static struct rk628_pin_bank *rk628_pin_to_bank(struct rk628_pctrl_info *pci, int pin, int *index)
{
	int banks[] = { GPIO0_PINBASE,
			GPIO1_PINBASE,
			GPIO2_PINBASE,
			GPIO3_PINBASE };
	int i;

	struct rk628_pin_bank *bank;

	for (i = 3; i >= 0; i--) {
		if (pin >= banks[i]) {
			bank = pci->pin_banks + i;
			break;
		}
	}

	if (i < 0) {
		dev_err(pci->dev, "pin%u is invalid pin number!\n", pin);
		return NULL;
	}

	if (index)
		*index = i;

	return bank;
}

static int rk628_set_slew_rate(struct rk628_pctrl_info *pci, int pin, int speed)
{
	int gpio = pin - PINBASE;
	/* gpio0b_sl(0-3) gpio1b_sl(0-3 -5) gpio3a_sl(4-7)*/
	char valid_gpio[] = {
		GPIO0_PINBASE - PINBASE + 8,
		GPIO0_PINBASE - PINBASE + 9,
		GPIO0_PINBASE - PINBASE + 10,
		GPIO0_PINBASE - PINBASE + 11,
		GPIO1_PINBASE - PINBASE + 8,
		GPIO1_PINBASE - PINBASE + 9,
		GPIO1_PINBASE - PINBASE + 10,
		GPIO1_PINBASE - PINBASE + 11,
		GPIO1_PINBASE - PINBASE + 12,
		GPIO1_PINBASE - PINBASE + 13,
		-1, -1,
		GPIO3_PINBASE - PINBASE + 4,
		GPIO3_PINBASE - PINBASE + 5,
		GPIO3_PINBASE - PINBASE + 6,
		GPIO3_PINBASE - PINBASE + 7
	};


	int val, ret, offset = 0xff;
	u32 i;

	for (i = 0; i < sizeof(valid_gpio); i++) {
		if (gpio == valid_gpio[i]) {
			offset = i;
			break;
		}
	}

	if (offset == 0xff) {
		dev_err(pci->dev, "pin%u don't support set slew rate\n", pin);
		return -EINVAL;
	}

	if (speed)
		val = BIT(offset + 16) | BIT(offset);
	else
		val = BIT(offset + 16);

	dev_dbg(pci->dev, " offset=%d 0x%x\n", offset, val);

	ret = regmap_write(pci->grf_regmap, GRF_GPIO_SR_CON, val);
	if (ret)
		dev_err(pci->dev, "%s:regmap write failed! pin%u\n",
			__func__, pin);

	return ret;
}

static int rk628_calc_pull_reg_and_value(struct rk628_pctrl_info *pci,
					 int pin,
					 int pull,
					 int *reg,
					 int *val)
{
	int gpio2_regs[] = { GRF_GPIO2A_P_CON, GRF_GPIO2B_P_CON, GRF_GPIO2C_P_CON };
	int gpio3_regs[] = { GRF_GPIO3A_P_CON, GRF_GPIO3B_P_CON };
	int valid_pinnum[] = { 8, 8, 24, 13 };
	int offset, i;
	struct rk628_pin_bank *bank;

	bank = rk628_pin_to_bank(pci, pin, &i);
	if (!bank) {
		dev_err(pci->dev, "pin%u is invalid\n", pin);
		return -EINVAL;
	}
	offset = pin - bank->pin_base;

	switch (bank->pin_base) {
	case GPIO0_PINBASE:
		if (pull == RK628_GPIO_PULL_UP) {
			dev_err(pci->dev, "pin%u don't support pull up!\n",
				pin);
			return -EINVAL;
		}

		if (offset == 2) {
			dev_err(pci->dev, "pin%u don't support pull!\n",
				pin);
			return -EINVAL;
		}

		if (offset < valid_pinnum[i]) {
			*val = 0x3 << (2 * offset + 16) | pull << (2 * offset);
			*reg = GRF_GPIO0A_P_CON;
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	case GPIO1_PINBASE:
		if (pull == RK628_GPIO_PULL_UP) {
			dev_err(pci->dev, "pin%u don't support pull up!\n",
				pin);
			return -EINVAL;
		}

		if (offset == 2) {
			dev_err(pci->dev, "pin%u don't support pull!\n",
				pin);
			return -EINVAL;
		}

		if (offset < valid_pinnum[i]) {
			*val = 0x3 << (2 * offset + 16) | pull << (2 * offset);
			*reg = GRF_GPIO1A_P_CON;
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	case GPIO2_PINBASE:
		if (pull == RK628_GPIO_PULL_UP)
			pull = RK628_GPIO_PULL_DOWN;
		else if (pull == RK628_GPIO_PULL_DOWN)
			pull = RK628_GPIO_PULL_UP;

		if (offset < valid_pinnum[i]) {
			*reg = gpio2_regs[offset / 8];
			offset = offset % 8;
			*val = 0x3 << (2 * offset + 16) | pull << (2 * offset);
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	case GPIO3_PINBASE:
		if (pull == RK628_GPIO_PULL_UP && (offset == 2 || offset == 11 || offset == 12)) {
			dev_err(pci->dev, "pin%u don't support pull up!\n",
				pin);
			return -EINVAL;
		} else if (pull == RK628_GPIO_PULL_DOWN && (offset == 9 || offset == 10)) {
			dev_err(pci->dev, "pin%u don't support pull down!\n",
				pin);
			return -EINVAL;
		}

		if (offset == 0 || offset == 1 || offset == 3 || offset == 8) {
			if (pull == RK628_GPIO_PULL_UP)
				pull = RK628_GPIO_PULL_DOWN;
			else if (pull == RK628_GPIO_PULL_DOWN)
				pull = RK628_GPIO_PULL_UP;
		}

		if ((offset > 7 && offset < valid_pinnum[i]) || offset < 4) {
			*reg = gpio3_regs[offset / 8];
			offset = offset % 8;
			*val = 0x3 << (2 * offset + 16) | pull << (2 * offset);
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int rk628_calc_strength_reg_and_value(struct rk628_pctrl_info *pci,
					 int pin,
					 int strength,
					 int *reg,
					 int *val)
{
	int valid_pinnum[] = { 8, 8, 24, 9 };
	int gpio_regs[][6] = {
		{	GRF_GPIO0B_D_CON
		},
		{	GRF_GPIO1B_D_CON
		},
		{
			GRF_GPIO2A_D0_CON, GRF_GPIO2A_D1_CON,
			GRF_GPIO2B_D0_CON, GRF_GPIO2B_D1_CON,
			GRF_GPIO2C_D0_CON, GRF_GPIO2C_D1_CON
		},
		{
			GRF_GPIO3A_D0_CON, GRF_GPIO3A_D1_CON,
			GRF_GPIO3B_D_CON
		}
	};
	int offset, i;
	struct rk628_pin_bank *bank;

	bank = rk628_pin_to_bank(pci, pin, &i);
	if (!bank) {
		dev_err(pci->dev, "pin%u is invalid\n", pin);
		return -EINVAL;
	}
	offset = pin - bank->pin_base;

	switch (bank->pin_base) {
	case GPIO0_PINBASE:
	case GPIO1_PINBASE:
		if (offset < valid_pinnum[i]) {
			dev_err(pci->dev, "pin%u don't support driver strength settings!\n",
				pin);
			return -EINVAL;
		}

		offset -= valid_pinnum[i];

		*val = 0x3 << (2 * offset + 16) | strength << (2 * offset);
		*reg = gpio_regs[i][0];
		dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
		return 0;
	case GPIO2_PINBASE:
	case GPIO3_PINBASE:
		if (offset < valid_pinnum[i]) {
			*reg = gpio_regs[i][offset / 4];
			offset = offset % 4;
			*val = 0x7 << (4 * offset + 16) | strength << (4 * offset);
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int rk628_calc_schmitt_reg_and_value(struct rk628_pctrl_info *pci,
					 int pin,
					 int enable,
					 int *reg,
					 int *val)
{
	int gpio2_regs[] = {GRF_GPIO2A_SMT, GRF_GPIO2B_SMT, GRF_GPIO2C_SMT};
	int gpio3_reg = GRF_GPIO3AB_SMT;
	int valid_pinnum[] = { 0, 0, 24, 9 };
	int offset, i;
	struct rk628_pin_bank *bank;

	bank = rk628_pin_to_bank(pci, pin, &i);
	if (!bank) {
		dev_err(pci->dev, "pin%u is invalid\n", pin);
		return -EINVAL;
	}
	offset = pin - bank->pin_base;

	switch (bank->pin_base) {
	case GPIO0_PINBASE:
	case GPIO1_PINBASE:
		break;
	case GPIO2_PINBASE:
		if (offset < valid_pinnum[i]) {
			*reg = gpio2_regs[offset / 8];
			offset = offset % 8;
			*val = BIT(offset + 16) | enable << (offset);
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	case GPIO3_PINBASE:
		if (offset == 0 || offset == 1 || offset == 3 || offset == 8) {
			*reg = gpio3_reg;
			*val = BIT(offset + 16) | enable << (offset);
			dev_dbg(pci->dev, "pin%u reg=0x%8x val=0x%8x\n",
				pin, *reg, *val);
			return 0;
		}
		break;
	default:
		break;
	}

	dev_err(pci->dev, "pin%u don't support schmitt settings!\n",
			pin);

	return -ENOTSUPP;
}

static int rk628_set_pull(struct rk628_pctrl_info *pci, int pin, int pull)
{
	int ret, reg, val;

	ret = rk628_calc_pull_reg_and_value(pci, pin, pull, &reg, &val);
	if (ret) {
		dev_err(pci->dev, "pin%u can not find reg or not support!\n", pin);
		return ret;
	}

	ret = regmap_write(pci->grf_regmap, reg, val);

	if (ret)
		dev_err(pci->dev, "%s:regmap write failed! pin%u\n",
			__func__, pin);

	return ret;
}

static int rk628_set_drive_perpin(struct rk628_pctrl_info *pci, int pin, int strength)
{
	int ret, reg, val;

	ret = rk628_calc_strength_reg_and_value(pci, pin, strength, &reg, &val);
	if (ret) {
		dev_err(pci->dev, "pin%u can not find reg or not support!\n", pin);
		return ret;
	}

	ret = regmap_write(pci->grf_regmap, reg, val);

	if (ret)
		dev_err(pci->dev, "%s:regmap write failed! pin%u\n",
			__func__, pin);

	return ret;
}

static int rk628_set_schmitt(struct rk628_pctrl_info *pci, int pin, int enable)
{
	int ret, reg, val;

	ret = rk628_calc_schmitt_reg_and_value(pci, pin, enable, &reg, &val);
	if (ret) {
		dev_err(pci->dev, "pin%u can not find reg or not support!\n", pin);
		return ret;
	}

	ret = regmap_write(pci->grf_regmap, reg, val);

	if (ret)
		dev_err(pci->dev, "%s:regmap write failed! pin%u\n",
			__func__, pin);

	return ret;
}

static int rk628_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned int pin, unsigned long *configs,
			     unsigned int num_configs)
{
	struct rk628_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 i, arg = 0;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			rk628_set_drive_perpin(pci, pin, arg);
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			rk628_set_pull(pci, pin, RK628_GPIO_HIGH_Z);
			break;
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
			dev_err(pci->dev,
				"PIN_CONFIG_BIAS_PULL_PIN_DEFAULT not supported\n");
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			rk628_set_pull(pci, pin, RK628_GPIO_PULL_UP);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			rk628_set_pull(pci, pin, RK628_GPIO_PULL_DOWN);
			break;
		case PIN_CONFIG_SLEW_RATE:
			rk628_set_slew_rate(pci, pin, arg);
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			rk628_set_schmitt(pci, pin, arg);
			break;
		case PIN_CONFIG_OUTPUT:
		{
			struct rk628_pin_bank *bank;

			bank = rk628_pin_to_bank(pci, pin, NULL);
			if (!bank) {
				dev_err(pci->dev, "pin%u is invalid\n", pin);
				return -EINVAL;
			}
			rk628_gpio_direction_output(&bank->gpio_chip, pin - bank->pin_base, arg);
			break;
		}
		default:
			dev_err(pci->dev, "Properties not supported\n");
			return 0;
		}
	}

	return 0;
}

static const struct pinconf_ops rk628_pinconf_ops = {
	.pin_config_get = rk628_pinconf_get,
	.pin_config_set = rk628_pinconf_set,
};

static struct pinctrl_desc rk628_pinctrl_desc = {
	.name = "rk628-pinctrl",
	.pctlops = &rk628_pinctrl_ops,
	.pmxops = &rk628_pinmux_ops,
	.confops = &rk628_pinconf_ops,
	.owner = THIS_MODULE,
};

static int rk628_pinctrl_create_function(struct device *dev,
					 struct rk628_pctrl_info *pci,
					 struct device_node *func_np,
					 struct rk628_pin_function *func)
{
	int npins;
	int ret;
	int i;
	char *func_sel[6] = {
		"vop_dclk0", "i2sm0_input", "rxdcc_input0", "hdmirx_cec0",
		"force_jtag_dis", "uart_iomux_dis",
	};

	if (of_property_read_string(func_np, "function", &func->name))
		return -1;

	func->mux_option = RK628_PINMUX_FUNC1;
	/* for signals input select */
	for (i = 0; i < 6; i++) {
		if (!strcmp(func_sel[i], func->name))
			func->mux_option = RK628_PINMUX_FUNC0;
	}

	dev_dbg(dev, "%s func->name=%s\n", __func__, func->name);
	npins = of_property_count_strings(func_np, "pins");
	if (npins < 1) {
		dev_err(dev, "invalid pin list in %s node", func_np->name);
		return -EINVAL;
	}

	func->groups = devm_kzalloc(dev, npins * sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for (i = 0; i < npins; ++i) {
		const char *gname;

		ret = of_property_read_string_index(func_np,
						    "pins", i, &gname);
		if (ret) {
			dev_err(dev,
				"failed to read pin name %d from %s node\n",
				i, func_np->name);
			return ret;
		}
		dev_dbg(dev, "%s func->groups[%d]=%s\n", __func__, i, gname);

		func->groups[i] = gname;
	}

	func->ngroups = npins;
	return 0;
}

static int rk628_pinctrl_parse_gpiobank(struct device *dev,
					struct rk628_pctrl_info *pci)
{
	struct device_node *dev_np = dev->of_node;
	struct device_node *cfg_np;
	struct rk628_pin_bank *bank;
	u32 i, count = 0;

	for_each_child_of_node(dev_np, cfg_np) {
		if (of_get_child_count(cfg_np))
			continue;
		if (!of_find_property(cfg_np, "gpio-controller", NULL))
			continue;
		bank = pci->pin_banks;
		for (i = 0; i < pci->nr_banks; ++i, ++bank) {
			if (strcmp(bank->name, cfg_np->name))
				continue;
			bank->of_node = cfg_np;
			count++;
			bank->clk = devm_get_clk_from_child(dev,
							    bank->of_node,
							    "pclk");
			if (IS_ERR(bank->clk)) {
				dev_err(dev, "bank->clk get error %ld\n",
					PTR_ERR(bank->clk));
				return PTR_ERR(bank->clk);
			}
			clk_prepare(bank->clk);
			break;
		}
		if (count == pci->nr_banks)
			break;
	}

	return 0;
}

static struct rk628_pin_function *
rk628_pinctrl_create_functions(struct device *dev,
			       struct rk628_pctrl_info *pci,
			       unsigned int *cnt)
{
	struct rk628_pin_function *functions, *func;
	struct device_node *dev_np = dev->of_node;
	struct device_node *cfg_np;
	unsigned int func_cnt = 0;
	const char *func_name;
	int ret;

	/*
	 * Iterate over all the child nodes of the pin controller node
	 * and create pin groups and pin function lists.
	 */
	for_each_child_of_node(dev_np, cfg_np) {
		if (!of_get_child_count(cfg_np)) {
			if (!of_find_property(cfg_np, "function", NULL))
				continue;
			if (!of_property_read_string(cfg_np,
						     "function", &func_name)) {
				if (!strncmp("gpio", func_name, 4))
					continue;
			}
			dev_dbg(dev, "%s: count=%d %s\n",
				__func__, func_cnt, func_name);
			++func_cnt;
			continue;
		}
	}

	++func_cnt;
	dev_dbg(dev, "total_count=%d, count %d for gpio function.\n",
		func_cnt, func_cnt - 1);
	functions = devm_kzalloc(dev, func_cnt * sizeof(*functions),
				 GFP_KERNEL);
	if (!functions)
		return ERR_PTR(-ENOMEM);

	func = functions;

	/*
	 * Iterate over all the child nodes of the pin controller node
	 * and create pin groups and pin function lists.
	 */
	func_cnt = 0;
	for_each_child_of_node(dev_np, cfg_np) {
		if (!of_get_child_count(cfg_np)) {
			if (!of_property_read_string(cfg_np,
						     "function", &func_name)) {
				if (!strncmp("gpio", func_name, 4))
					continue;
			}
			ret = rk628_pinctrl_create_function(dev, pci,
							    cfg_np, func);
			if (!ret) {
				++func;
				++func_cnt;
			}
			continue;
		}
	}

	/* init gpio func */
	*(func) = rk628_functions[RK628_MUX_GPIO];
	func->mux_option = RK628_PINMUX_FUNC0;

	dev_dbg(dev, "count %d is for %s function\n", func_cnt, func->name);
	++func;
	++func_cnt;
	*cnt = func_cnt;
	return functions;
}

static int rk628_pinctrl_parse_dt(struct platform_device *pdev,
				  struct rk628_pctrl_info *pci)
{
	struct device *dev = &pdev->dev;
	struct rk628_pin_function *functions;
	unsigned int func_cnt = 0;
	int ret;

	ret = rk628_pinctrl_parse_gpiobank(dev, pci);
	if (ret)
		return ret;

	functions = rk628_pinctrl_create_functions(dev, pci, &func_cnt);
	if (IS_ERR(functions)) {
		dev_err(dev, "failed to parse pin functions\n");
		return PTR_ERR(functions);
	}

	pci->functions = functions;
	pci->num_functions = func_cnt;
	return 0;
}

static const struct regmap_range rk628_pinctrl_readable_ranges[] = {
	regmap_reg_range(GPIO0_BASE, GPIO0_BASE + GPIO_VER_ID),
	regmap_reg_range(GPIO1_BASE, GPIO1_BASE + GPIO_VER_ID),
	regmap_reg_range(GPIO2_BASE, GPIO2_BASE + GPIO_VER_ID),
	regmap_reg_range(GPIO3_BASE, GPIO3_BASE + GPIO_VER_ID),
};

static const struct regmap_access_table rk628_pinctrl_readable_table = {
	.yes_ranges     = rk628_pinctrl_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_pinctrl_readable_ranges),
};

static const struct regmap_config rk628_pinctrl_regmap_config = {
	.name = "rk628-pinctrl",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = GPIO_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_pinctrl_readable_table,
};

static void rk628_irq_enable(struct irq_data *d)
{
	struct rk628_pin_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned long hwirq = d->hwirq;
	u32 offset;

	if (hwirq / 16) {
		hwirq = hwirq - 16;
		offset = GPIO_REG_HIGH;
	} else {
		offset = GPIO_REG_LOW;
	}

	bank->mask_regs[offset] |= BIT(hwirq);
}

static void rk628_irq_disable(struct irq_data *d)
{
	struct rk628_pin_bank *bank = irq_data_get_irq_chip_data(d);
	unsigned long hwirq = d->hwirq;
	u32 offset;

	if (hwirq / 16) {
		hwirq = hwirq - 16;
		offset = GPIO_REG_HIGH;
	} else {
		offset = GPIO_REG_LOW;
	}

	bank->mask_regs[offset] &= ~BIT(hwirq);
}

static int rk628_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct rk628_pin_bank *bank = irq_data_get_irq_chip_data(d);
	struct rk628_pctrl_info *pci = bank->pci;
	unsigned long hwirq = d->hwirq;
	u32 offset;

	if (hwirq / 16) {
		hwirq = hwirq - 16;
		offset = GPIO_REG_HIGH;
	} else {
		offset = GPIO_REG_LOW;
	}

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		bank->bothedge_regs[offset] |= BIT(hwirq);
		break;
	case IRQ_TYPE_EDGE_RISING:
		bank->bothedge_regs[offset] &= ~BIT(hwirq);
		bank->level_regs[offset] |= BIT(hwirq);
		bank->polarity_regs[offset] |= BIT(hwirq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		bank->bothedge_regs[offset] &= ~BIT(hwirq);
		bank->level_regs[offset] |= BIT(hwirq);
		bank->polarity_regs[offset] &= ~BIT(hwirq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		bank->bothedge_regs[offset] &= ~BIT(hwirq);
		bank->level_regs[offset] &= ~BIT(hwirq);
		bank->polarity_regs[offset] |= BIT(hwirq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		bank->bothedge_regs[offset] &= ~BIT(hwirq);
		bank->level_regs[offset] &= ~BIT(hwirq);
		bank->polarity_regs[offset] &= ~BIT(hwirq);
		break;
	default:
		dev_err(pci->dev, "irq type invalid!\n");
		return -EINVAL;
	}

	return 0;
}

static void rk628_irq_lock(struct irq_data *d)
{
	struct rk628_pin_bank *bank = irq_data_get_irq_chip_data(d);

	mutex_lock(&bank->lock);
	clk_enable(bank->clk);
}

static void rk628_irq_sync_unlock(struct irq_data *d)
{
	struct rk628_pin_bank *bank = irq_data_get_irq_chip_data(d);
	struct rk628_pctrl_info *pci = bank->pci;
	int ret;
	unsigned long hwirq = d->hwirq;
	u32 offset, inten, level, polarity, bothedge;

	if (hwirq / 16) {
		hwirq = hwirq - 16;
		offset = GPIO_REG_HIGH;
	} else {
		offset = GPIO_REG_LOW;
	}

	inten = (bank->reg_base + GPIO_INTEN_L + ((offset) * 4));
	level = (bank->reg_base + GPIO_INTTYPE_L + ((offset) * 4));
	polarity = (bank->reg_base + GPIO_INT_POLARITY_L + ((offset) * 4));
	bothedge = (bank->reg_base + GPIO_INT_BOTHEDGE_L + ((offset) * 4));

	ret = regmap_write(pci->regmap, level,
			   bank->level_regs[offset] | BIT(hwirq + 16));
	if (ret)
		dev_err(pci->dev, "regmap read failed! reg=0x%x irq=%d\n",
			level, d->irq);

	ret = regmap_write(pci->regmap, polarity,
			   bank->polarity_regs[offset] | BIT(hwirq + 16));
	if (ret)
		dev_err(pci->dev, "regmap read failed! reg=0x%x irq=%d\n",
			polarity, d->irq);

	ret = regmap_write(pci->regmap, bothedge,
			   bank->bothedge_regs[offset] | BIT(hwirq + 16));
	if (ret)
		dev_err(pci->dev, "regmap read failed! reg=0x%x irq=%d\n",
			bothedge, d->irq);

	ret = regmap_write(pci->regmap, inten,
			   bank->mask_regs[offset] | BIT(hwirq + 16));
	if (ret)
		dev_err(pci->dev, "regmap read failed! reg=0x%x irq=%d\n",
			inten, d->irq);

	clk_disable(bank->clk);
	mutex_unlock(&bank->lock);
}

enum rk628_irqchip {
	IRQCHIP_gpio0,
	IRQCHIP_gpio1,
	IRQCHIP_gpio2,
	IRQCHIP_gpio3,
};

static const struct irq_chip rk628_irq_chip[] = {
	IRQ_CHIP(gpio0),
	IRQ_CHIP(gpio1),
	IRQ_CHIP(gpio2),
	IRQ_CHIP(gpio3),
};

static int rk628_irq_map(struct irq_domain *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct rk628_pin_bank *bank = h->host_data;

	irq_set_chip_data(virq, bank);
	irq_set_chip(virq, &bank->irq_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops rk628_domain_ops = {
	.map	= rk628_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static irqreturn_t rk628_irq_demux_thread(int irq, void *d)
{
	struct rk628_pin_bank *bank = d;
	struct rk628_pctrl_info *pci = bank->pci;
	int ret;
	u32 pend, low_bit, high_bit;

	clk_enable(bank->clk);

	ret = regmap_read(pci->regmap, bank->reg_base + GPIO_INT_STATUS, &pend);
	if (ret)
		dev_err(pci->dev, "regmap read failed! line=%d\n", __LINE__);

	low_bit = pend & 0x0000ffff;
	high_bit = (pend >> 16);
	ret = regmap_write(pci->regmap, bank->reg_base + GPIO_PORTS_EOI_L,
			   (low_bit << 16) | low_bit);
	if (ret)
		dev_err(pci->dev, "regmap read failed! line=%d\n", __LINE__);

	ret = regmap_write(pci->regmap, bank->reg_base + GPIO_PORTS_EOI_H,
			   (high_bit << 16) | high_bit);
	if (ret)
		dev_err(pci->dev, "regmap read failed! line=%d\n", __LINE__);

	while (pend) {
		unsigned int irq, virq;

		irq = __ffs(pend);
		pend &= ~BIT(irq);
		virq = irq_linear_revmap(bank->domain, irq);

		if (!virq) {
			dev_err(pci->dev, "unmapped irq %d\n", irq);
			continue;
		}

		handle_nested_irq(virq);
	}
	clk_disable(bank->clk);

	return IRQ_HANDLED;
}

static int rk628_interrupts_register(struct platform_device *pdev,
				     struct rk628_pctrl_info *pci)
{
	struct rk628_pin_bank *bank = pci->pin_banks;
	int ret;
	u32 i;

	for (i = 0; i < pci->nr_banks; ++i, ++bank) {
		mutex_init(&bank->lock);
		ret = clk_enable(bank->clk);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable clock for bank %s\n",
				bank->name);
			continue;
		}

		bank->irq = platform_get_irq(pdev, i);
		bank->irq_chip = rk628_irq_chip[i];
		bank->domain = irq_domain_add_linear(bank->of_node,
						     bank->nr_pins,
						     &rk628_domain_ops,
						     bank);
		if (!bank->domain) {
			dev_warn(&pdev->dev,
				 "could not initialize irq domain for bank %s\n",
				 bank->name);
			clk_disable(bank->clk);
			continue;
		}

		ret = request_threaded_irq(bank->irq, NULL,
					   rk628_irq_demux_thread,
					   IRQF_ONESHOT,
					   bank->name, bank);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"Failed to request IRQ %d for %s: %d\n",
				bank->irq, bank->name, ret);
		}

		clk_disable(bank->clk);
	}

	return 0;
}

static int rk628_gpiolib_register(struct platform_device *pdev,
				  struct rk628_pctrl_info *pci)
{
	struct rk628_pin_bank *bank = pci->pin_banks;
	struct gpio_chip *gc;
	int ret, i;

	for (i = 0; i < pci->nr_banks; ++i, ++bank) {
		bank->gpio_chip = rk628_gpiolib_chip;

		gc = &bank->gpio_chip;
		gc->base = bank->pin_base;
		gc->ngpio = bank->nr_pins;
		gc->parent = &pdev->dev;
		gc->of_node = bank->of_node;
		gc->label = bank->name;

		ret = gpiochip_add_data(gc, bank);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to register gpio_chip %s, error code: %d\n",
				gc->label, ret);
			goto fail;
		}
	}
	return 0;

fail:
	for (--i, --bank; i >= 0; --i, --bank)
		gpiochip_remove(&bank->gpio_chip);

	return ret;
}

static int rk628_pinctrl_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_pctrl_info *pci;
	int ret;
	u32 bank;
	struct rk628_pin_bank *pin_bank;

	pci = devm_kzalloc(&pdev->dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = &pdev->dev;
	pci->grf_regmap = rk628->grf;

	pci->pinctrl_desc = rk628_pinctrl_desc;
	pci->groups = rk628_pin_groups;
	pci->num_groups = ARRAY_SIZE(rk628_pin_groups);
	pci->pinctrl_desc.pins = rk628_pins_desc;
	pci->pinctrl_desc.npins = ARRAY_SIZE(rk628_pins_desc);
	pci->pin_banks = rk628_pin_banks;
	pci->nr_banks = ARRAY_SIZE(rk628_pin_banks),

	platform_set_drvdata(pdev, pci);

	ret = rk628_pinctrl_parse_dt(pdev, pci);
	if (ret < 0)
		return ret;

	pci->regmap = devm_regmap_init_i2c(rk628->client,
					   &rk628_pinctrl_regmap_config);
	if (IS_ERR(pci->regmap)) {
		ret = PTR_ERR(pci->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* Add gpiochip */
	ret = rk628_gpiolib_register(pdev, pci);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't add gpiochip\n");
		return ret;
	}

	/* Add pinctrl */
	pci->pctl = devm_pinctrl_register(&pdev->dev, &pci->pinctrl_desc, pci);
	if (IS_ERR(pci->pctl)) {
		dev_err(&pdev->dev, "Couldn't add pinctrl\n");
		return PTR_ERR(pci->pctl);
	}

	for (bank = 0; bank < pci->nr_banks; ++bank) {
		pin_bank = &pci->pin_banks[bank];
		pin_bank->pci = pci;
		pin_bank->grange.name = pin_bank->name;
		pin_bank->grange.id = bank;
		pin_bank->grange.pin_base = pin_bank->pin_base;
		pin_bank->grange.base = pin_bank->gpio_chip.base;
		pin_bank->grange.npins = pin_bank->gpio_chip.ngpio;
		pin_bank->grange.gc = &pin_bank->gpio_chip;
		pinctrl_add_gpio_range(pci->pctl, &pin_bank->grange);
	}

	rk628_interrupts_register(pdev, pci);

	return 0;
}

static const struct of_device_id rk628_pinctrl_dt_match[] = {
	{ .compatible = "rockchip,rk628-pinctrl" },
	{},
};

MODULE_DEVICE_TABLE(of, rk628_pinctrl_dt_match);

static struct platform_driver rk628_pinctrl_driver = {
	.probe = rk628_pinctrl_probe,
	.driver = {
		.name = "rk628-pinctrl",
		.of_match_table = of_match_ptr(rk628_pinctrl_dt_match),
	},
};

module_platform_driver(rk628_pinctrl_driver);

MODULE_DESCRIPTION("RK628 pin control and GPIO driver");
MODULE_AUTHOR("Weixin Zhou <zwx@rock-chips.com>");
MODULE_LICENSE("GPL v2");
