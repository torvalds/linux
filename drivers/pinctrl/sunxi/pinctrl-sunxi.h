/*
 * Allwinner A1X SoCs pinctrl driver.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PINCTRL_SUNXI_H
#define __PINCTRL_SUNXI_H

#include <linux/kernel.h>
#include <linux/spinlock.h>

#define PA_BASE	0
#define PB_BASE	32
#define PC_BASE	64
#define PD_BASE	96
#define PE_BASE	128
#define PF_BASE	160
#define PG_BASE	192
#define PH_BASE	224
#define PI_BASE	256
#define PL_BASE	352
#define PM_BASE	384

#define SUNXI_PINCTRL_PIN(bank, pin)		\
	PINCTRL_PIN(P ## bank ## _BASE + (pin), "P" #bank #pin)

#define SUNXI_PIN_NAME_MAX_LEN	5

#define BANK_MEM_SIZE		0x24
#define MUX_REGS_OFFSET		0x0
#define DATA_REGS_OFFSET	0x10
#define DLEVEL_REGS_OFFSET	0x14
#define PULL_REGS_OFFSET	0x1c

#define PINS_PER_BANK		32
#define MUX_PINS_PER_REG	8
#define MUX_PINS_BITS		4
#define MUX_PINS_MASK		0x0f
#define DATA_PINS_PER_REG	32
#define DATA_PINS_BITS		1
#define DATA_PINS_MASK		0x01
#define DLEVEL_PINS_PER_REG	16
#define DLEVEL_PINS_BITS	2
#define DLEVEL_PINS_MASK	0x03
#define PULL_PINS_PER_REG	16
#define PULL_PINS_BITS		2
#define PULL_PINS_MASK		0x03

#define SUNXI_IRQ_NUMBER	32

#define IRQ_CFG_REG		0x200
#define IRQ_CFG_IRQ_PER_REG		8
#define IRQ_CFG_IRQ_BITS		4
#define IRQ_CFG_IRQ_MASK		((1 << IRQ_CFG_IRQ_BITS) - 1)
#define IRQ_CTRL_REG		0x210
#define IRQ_CTRL_IRQ_PER_REG		32
#define IRQ_CTRL_IRQ_BITS		1
#define IRQ_CTRL_IRQ_MASK		((1 << IRQ_CTRL_IRQ_BITS) - 1)
#define IRQ_STATUS_REG		0x214
#define IRQ_STATUS_IRQ_PER_REG		32
#define IRQ_STATUS_IRQ_BITS		1
#define IRQ_STATUS_IRQ_MASK		((1 << IRQ_STATUS_IRQ_BITS) - 1)

#define IRQ_EDGE_RISING		0x00
#define IRQ_EDGE_FALLING	0x01
#define IRQ_LEVEL_HIGH		0x02
#define IRQ_LEVEL_LOW		0x03
#define IRQ_EDGE_BOTH		0x04

struct sunxi_desc_function {
	const char	*name;
	u8		muxval;
	u8		irqnum;
};

struct sunxi_desc_pin {
	struct pinctrl_pin_desc		pin;
	struct sunxi_desc_function	*functions;
};

struct sunxi_pinctrl_desc {
	const struct sunxi_desc_pin	*pins;
	int				npins;
	unsigned			pin_base;
};

struct sunxi_pinctrl_function {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

struct sunxi_pinctrl_group {
	const char	*name;
	unsigned long	config;
	unsigned	pin;
};

struct sunxi_pinctrl {
	void __iomem			*membase;
	struct gpio_chip		*chip;
	const struct sunxi_pinctrl_desc	*desc;
	struct device			*dev;
	struct irq_domain		*domain;
	struct sunxi_pinctrl_function	*functions;
	unsigned			nfunctions;
	struct sunxi_pinctrl_group	*groups;
	unsigned			ngroups;
	int				irq;
	int				irq_array[SUNXI_IRQ_NUMBER];
	spinlock_t			lock;
	struct pinctrl_dev		*pctl_dev;
};

#define SUNXI_PIN(_pin, ...)					\
	{							\
		.pin = _pin,					\
		.functions = (struct sunxi_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define SUNXI_FUNCTION(_val, _name)				\
	{							\
		.name = _name,					\
		.muxval = _val,					\
	}

#define SUNXI_FUNCTION_IRQ(_val, _irq)				\
	{							\
		.name = "irq",					\
		.muxval = _val,					\
		.irqnum = _irq,					\
	}

/*
 * The sunXi PIO registers are organized as is:
 * 0x00 - 0x0c	Muxing values.
 *		8 pins per register, each pin having a 4bits value
 * 0x10		Pin values
 *		32 bits per register, each pin corresponding to one bit
 * 0x14 - 0x18	Drive level
 *		16 pins per register, each pin having a 2bits value
 * 0x1c - 0x20	Pull-Up values
 *		16 pins per register, each pin having a 2bits value
 *
 * This is for the first bank. Each bank will have the same layout,
 * with an offset being a multiple of 0x24.
 *
 * The following functions calculate from the pin number the register
 * and the bit offset that we should access.
 */
static inline u32 sunxi_mux_reg(u16 pin)
{
	u8 bank = pin / PINS_PER_BANK;
	u32 offset = bank * BANK_MEM_SIZE;
	offset += MUX_REGS_OFFSET;
	offset += pin % PINS_PER_BANK / MUX_PINS_PER_REG * 0x04;
	return round_down(offset, 4);
}

static inline u32 sunxi_mux_offset(u16 pin)
{
	u32 pin_num = pin % MUX_PINS_PER_REG;
	return pin_num * MUX_PINS_BITS;
}

static inline u32 sunxi_data_reg(u16 pin)
{
	u8 bank = pin / PINS_PER_BANK;
	u32 offset = bank * BANK_MEM_SIZE;
	offset += DATA_REGS_OFFSET;
	offset += pin % PINS_PER_BANK / DATA_PINS_PER_REG * 0x04;
	return round_down(offset, 4);
}

static inline u32 sunxi_data_offset(u16 pin)
{
	u32 pin_num = pin % DATA_PINS_PER_REG;
	return pin_num * DATA_PINS_BITS;
}

static inline u32 sunxi_dlevel_reg(u16 pin)
{
	u8 bank = pin / PINS_PER_BANK;
	u32 offset = bank * BANK_MEM_SIZE;
	offset += DLEVEL_REGS_OFFSET;
	offset += pin % PINS_PER_BANK / DLEVEL_PINS_PER_REG * 0x04;
	return round_down(offset, 4);
}

static inline u32 sunxi_dlevel_offset(u16 pin)
{
	u32 pin_num = pin % DLEVEL_PINS_PER_REG;
	return pin_num * DLEVEL_PINS_BITS;
}

static inline u32 sunxi_pull_reg(u16 pin)
{
	u8 bank = pin / PINS_PER_BANK;
	u32 offset = bank * BANK_MEM_SIZE;
	offset += PULL_REGS_OFFSET;
	offset += pin % PINS_PER_BANK / PULL_PINS_PER_REG * 0x04;
	return round_down(offset, 4);
}

static inline u32 sunxi_pull_offset(u16 pin)
{
	u32 pin_num = pin % PULL_PINS_PER_REG;
	return pin_num * PULL_PINS_BITS;
}

static inline u32 sunxi_irq_cfg_reg(u16 irq)
{
	u8 reg = irq / IRQ_CFG_IRQ_PER_REG * 0x04;
	return reg + IRQ_CFG_REG;
}

static inline u32 sunxi_irq_cfg_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_CFG_IRQ_PER_REG;
	return irq_num * IRQ_CFG_IRQ_BITS;
}

static inline u32 sunxi_irq_ctrl_reg(u16 irq)
{
	u8 reg = irq / IRQ_CTRL_IRQ_PER_REG * 0x04;
	return reg + IRQ_CTRL_REG;
}

static inline u32 sunxi_irq_ctrl_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_CTRL_IRQ_PER_REG;
	return irq_num * IRQ_CTRL_IRQ_BITS;
}

static inline u32 sunxi_irq_status_reg(u16 irq)
{
	u8 reg = irq / IRQ_STATUS_IRQ_PER_REG * 0x04;
	return reg + IRQ_STATUS_REG;
}

static inline u32 sunxi_irq_status_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_STATUS_IRQ_PER_REG;
	return irq_num * IRQ_STATUS_IRQ_BITS;
}

int sunxi_pinctrl_init(struct platform_device *pdev,
		       const struct sunxi_pinctrl_desc *desc);

#endif /* __PINCTRL_SUNXI_H */
