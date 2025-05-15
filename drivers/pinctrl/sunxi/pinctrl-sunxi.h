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
#define PJ_BASE	288
#define PK_BASE	320
#define PL_BASE	352
#define PM_BASE	384
#define PN_BASE	416

/* maximum number of banks per controller (PA -> PK) */
#define SUNXI_PINCTRL_MAX_BANKS	11

#define SUNXI_PINCTRL_PIN(bank, pin)		\
	PINCTRL_PIN(P ## bank ## _BASE + (pin), "P" #bank #pin)

#define SUNXI_PIN_NAME_MAX_LEN	5

#define BANK_MEM_SIZE		0x24
#define MUX_REGS_OFFSET		0x0
#define MUX_FIELD_WIDTH		4
#define DATA_REGS_OFFSET	0x10
#define DATA_FIELD_WIDTH	1
#define DLEVEL_REGS_OFFSET	0x14
#define DLEVEL_FIELD_WIDTH	2
#define PULL_REGS_OFFSET	0x1c
#define PULL_FIELD_WIDTH	2

#define D1_BANK_MEM_SIZE	0x30
#define D1_DLEVEL_FIELD_WIDTH	4
#define D1_PULL_REGS_OFFSET	0x24

#define PINS_PER_BANK		32

#define IRQ_PER_BANK		32

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

#define IRQ_DEBOUNCE_REG	0x218

#define IRQ_MEM_SIZE		0x20

#define IRQ_EDGE_RISING		0x00
#define IRQ_EDGE_FALLING	0x01
#define IRQ_LEVEL_HIGH		0x02
#define IRQ_LEVEL_LOW		0x03
#define IRQ_EDGE_BOTH		0x04

#define GRP_CFG_REG		0x300

#define IO_BIAS_MASK		GENMASK(3, 0)

#define SUN4I_FUNC_INPUT	0
#define SUN4I_FUNC_IRQ		6

#define SUNXI_PINCTRL_VARIANT_MASK	GENMASK(7, 0)
#define SUNXI_PINCTRL_NEW_REG_LAYOUT	BIT(8)
#define SUNXI_PINCTRL_PORTF_SWITCH	BIT(9)
#define SUNXI_PINCTRL_ELEVEN_BANKS	BIT(10)

#define PIO_POW_MOD_SEL_REG		0x340
#define PIO_11B_POW_MOD_SEL_REG		0x380
#define PIO_POW_MOD_CTL_OFS		0x004

#define PIO_BANK_K_OFFSET		0x500

enum sunxi_desc_bias_voltage {
	BIAS_VOLTAGE_NONE,
	/*
	 * Bias voltage configuration is done through
	 * Pn_GRP_CONFIG registers, as seen on A80 SoC.
	 */
	BIAS_VOLTAGE_GRP_CONFIG,
	/*
	 * Bias voltage is set through PIO_POW_MOD_SEL_REG
	 * register, as seen on H6 SoC, for example.
	 */
	BIAS_VOLTAGE_PIO_POW_MODE_SEL,
	/*
	 * Bias voltage is set through PIO_POW_MOD_SEL_REG
	 * and PIO_POW_MOD_CTL_REG register, as seen on
	 * A100 and D1 SoC, for example.
	 */
	BIAS_VOLTAGE_PIO_POW_MODE_CTL,
};

struct sunxi_desc_function {
	unsigned long	variant;
	const char	*name;
	u8		muxval;
	u8		irqbank;
	u8		irqnum;
};

struct sunxi_desc_pin {
	struct pinctrl_pin_desc		pin;
	unsigned long			variant;
	struct sunxi_desc_function	*functions;
};

struct sunxi_pinctrl_desc {
	const struct sunxi_desc_pin	*pins;
	int				npins;
	unsigned			pin_base;
	unsigned			irq_banks;
	const unsigned int		*irq_bank_map;
	bool				irq_read_needs_mux;
	bool				disable_strict_mode;
	enum sunxi_desc_bias_voltage	io_bias_cfg_variant;
};

struct sunxi_pinctrl_function {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

struct sunxi_pinctrl_group {
	const char	*name;
	unsigned	pin;
};

struct sunxi_pinctrl_regulator {
	struct regulator	*regulator;
	refcount_t		refcount;
};

struct sunxi_pinctrl {
	void __iomem			*membase;
	struct gpio_chip		*chip;
	const struct sunxi_pinctrl_desc	*desc;
	struct device			*dev;
	struct sunxi_pinctrl_regulator	regulators[11];
	struct irq_domain		*domain;
	struct sunxi_pinctrl_function	*functions;
	unsigned			nfunctions;
	struct sunxi_pinctrl_group	*groups;
	unsigned			ngroups;
	int				*irq;
	unsigned			*irq_array;
	raw_spinlock_t			lock;
	struct pinctrl_dev		*pctl_dev;
	unsigned long			variant;
	u32				bank_mem_size;
	u32				pull_regs_offset;
	u32				dlevel_field_width;
	u32				pow_mod_sel_offset;
};

#define SUNXI_PIN(_pin, ...)					\
	{							\
		.pin = _pin,					\
		.functions = (struct sunxi_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define SUNXI_PIN_VARIANT(_pin, _variant, ...)			\
	{							\
		.pin = _pin,					\
		.variant = _variant,				\
		.functions = (struct sunxi_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define SUNXI_FUNCTION(_val, _name)				\
	{							\
		.name = _name,					\
		.muxval = _val,					\
	}

#define SUNXI_FUNCTION_VARIANT(_val, _name, _variant)		\
	{							\
		.name = _name,					\
		.muxval = _val,					\
		.variant = _variant,				\
	}

#define SUNXI_FUNCTION_IRQ(_val, _irq)				\
	{							\
		.name = "irq",					\
		.muxval = _val,					\
		.irqnum = _irq,					\
	}

#define SUNXI_FUNCTION_IRQ_BANK(_val, _bank, _irq)		\
	{							\
		.name = "irq",					\
		.muxval = _val,					\
		.irqbank = _bank,				\
		.irqnum = _irq,					\
	}

static inline u32 sunxi_irq_hw_bank_num(const struct sunxi_pinctrl_desc *desc, u8 bank)
{
	if (!desc->irq_bank_map)
		return bank;
	else
		return desc->irq_bank_map[bank];
}

static inline u32 sunxi_irq_cfg_reg(const struct sunxi_pinctrl_desc *desc,
				    u16 irq)
{
	u8 bank = irq / IRQ_PER_BANK;
	u8 reg = (irq % IRQ_PER_BANK) / IRQ_CFG_IRQ_PER_REG * 0x04;

	return IRQ_CFG_REG +
	       sunxi_irq_hw_bank_num(desc, bank) * IRQ_MEM_SIZE + reg;
}

static inline u32 sunxi_irq_cfg_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_CFG_IRQ_PER_REG;
	return irq_num * IRQ_CFG_IRQ_BITS;
}

static inline u32 sunxi_irq_ctrl_reg_from_bank(const struct sunxi_pinctrl_desc *desc, u8 bank)
{
	return IRQ_CTRL_REG + sunxi_irq_hw_bank_num(desc, bank) * IRQ_MEM_SIZE;
}

static inline u32 sunxi_irq_ctrl_reg(const struct sunxi_pinctrl_desc *desc,
				     u16 irq)
{
	u8 bank = irq / IRQ_PER_BANK;

	return sunxi_irq_ctrl_reg_from_bank(desc, bank);
}

static inline u32 sunxi_irq_ctrl_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_CTRL_IRQ_PER_REG;
	return irq_num * IRQ_CTRL_IRQ_BITS;
}

static inline u32 sunxi_irq_debounce_reg_from_bank(const struct sunxi_pinctrl_desc *desc, u8 bank)
{
	return IRQ_DEBOUNCE_REG +
	       sunxi_irq_hw_bank_num(desc, bank) * IRQ_MEM_SIZE;
}

static inline u32 sunxi_irq_status_reg_from_bank(const struct sunxi_pinctrl_desc *desc, u8 bank)
{
	return IRQ_STATUS_REG +
	       sunxi_irq_hw_bank_num(desc, bank) * IRQ_MEM_SIZE;
}

static inline u32 sunxi_irq_status_reg(const struct sunxi_pinctrl_desc *desc,
				       u16 irq)
{
	u8 bank = irq / IRQ_PER_BANK;

	return sunxi_irq_status_reg_from_bank(desc, bank);
}

static inline u32 sunxi_irq_status_offset(u16 irq)
{
	u32 irq_num = irq % IRQ_STATUS_IRQ_PER_REG;
	return irq_num * IRQ_STATUS_IRQ_BITS;
}

static inline u32 sunxi_grp_config_reg(u16 pin)
{
	u8 bank = pin / PINS_PER_BANK;

	return GRP_CFG_REG + bank * 0x4;
}

int sunxi_pinctrl_init_with_flags(struct platform_device *pdev,
				  const struct sunxi_pinctrl_desc *desc,
				  unsigned long flags);

#define sunxi_pinctrl_init(_dev, _desc) \
	sunxi_pinctrl_init_with_flags(_dev, _desc, 0)

int sunxi_pinctrl_dt_table_init(struct platform_device *pdev,
				const u8 *pins_per_bank,
				const u8 *irq_bank_muxes,
				struct sunxi_pinctrl_desc *desc,
				unsigned long flags);

#endif /* __PINCTRL_SUNXI_H */
