/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright(c) 2019 Intel Corporation.
 */

#ifndef __PINCTRL_EQUILIBRIUM_H
#define __PINCTRL_EQUILIBRIUM_H

/* PINPAD register offset */
#define REG_PMX_BASE	0x0	/* Port Multiplexer Control Register */
#define REG_PUEN	0x80	/* PULL UP Enable Register */
#define REG_PDEN	0x84	/* PULL DOWN Enable Register */
#define REG_SRC		0x88	/* Slew Rate Control Register */
#define REG_DCC0	0x8C	/* Drive Current Control Register 0 */
#define REG_DCC1	0x90	/* Drive Current Control Register 1 */
#define REG_OD		0x94	/* Open Drain Enable Register */
#define REG_AVAIL	0x98	/* Pad Control Availability Register */
#define DRV_CUR_PINS	16	/* Drive Current pin number per register */
#define REG_DRCC(x)	(REG_DCC0 + (x) * 4) /* Driver current macro */

/* GPIO register offset */
#define GPIO_OUT	0x0	/* Data Output Register */
#define GPIO_IN		0x4	/* Data Input Register */
#define GPIO_DIR	0x8	/* Direction Register */
#define GPIO_EXINTCR0	0x18	/* External Interrupt Control Register 0 */
#define GPIO_EXINTCR1	0x1C	/* External Interrupt Control Register 1 */
#define GPIO_IRNCR	0x20	/* IRN Capture Register */
#define GPIO_IRNICR	0x24	/* IRN Interrupt Control Register */
#define GPIO_IRNEN	0x28	/* IRN Interrupt Enable Register */
#define GPIO_IRNCFG	0x2C	/* IRN Interrupt Configuration Register */
#define GPIO_IRNRNSET	0x30	/* IRN Interrupt Enable Set Register */
#define GPIO_IRNENCLR	0x34	/* IRN Interrupt Enable Clear Register */
#define GPIO_OUTSET	0x40	/* Output Set Register */
#define GPIO_OUTCLR	0x44	/* Output Clear Register */
#define GPIO_DIRSET	0x48	/* Direction Set Register */
#define GPIO_DIRCLR	0x4C	/* Direction Clear Register */

/* parse given pin's driver current value */
#define PARSE_DRV_CURRENT(val, pin) (((val) >> ((pin) * 2)) & 0x3)

#define GPIO_EDGE_TRIG		0
#define GPIO_LEVEL_TRIG		1
#define GPIO_SINGLE_EDGE	0
#define GPIO_BOTH_EDGE		1
#define GPIO_POSITIVE_TRIG	0
#define GPIO_NEGATIVE_TRIG	1

#define EQBR_GPIO_MODE		0

typedef enum {
	OP_COUNT_NR_FUNCS,
	OP_ADD_FUNCS,
	OP_COUNT_NR_FUNC_GRPS,
	OP_ADD_FUNC_GRPS,
	OP_NONE,
} funcs_util_ops;

/**
 * struct gpio_irq_type: gpio irq configuration
 * @trig_type: level trigger or edge trigger
 * @edge_type: sigle edge or both edge
 * @logic_type: positive trigger or negative trigger
 */
struct gpio_irq_type {
	unsigned int trig_type;
	unsigned int edge_type;
	unsigned int logic_type;
};

/**
 * struct eqbr_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @nr_groups: number of groups included in @groups.
 */
struct eqbr_pmx_func {
	const char		*name;
	const char		**groups;
	unsigned int		nr_groups;
};

/**
 * struct eqbr_pin_bank: represent a pin bank.
 * @membase: base address of the pin bank register.
 * @id: bank id, to idenify the unique bank.
 * @pin_base: starting pin number of the pin bank.
 * @nr_pins: number of the pins of the pin bank.
 * @aval_pinmap: available pin bitmap of the pin bank.
 */
struct eqbr_pin_bank {
	void __iomem		*membase;
	unsigned int		id;
	unsigned int		pin_base;
	unsigned int		nr_pins;
	u32			aval_pinmap;
};

/**
 * struct eqbr_gpio_ctrl: represent a gpio controller.
 * @node: device node of gpio controller.
 * @bank: pointer to corresponding pin bank.
 * @membase: base address of the gpio controller.
 * @chip: gpio chip.
 * @ic:   irq chip.
 * @name: gpio chip name.
 * @virq: irq number of the gpio chip to parent's irq domain.
 * @lock: spin lock to protect gpio register write.
 */
struct eqbr_gpio_ctrl {
	struct device_node	*node;
	struct eqbr_pin_bank	*bank;
	void __iomem		*membase;
	struct gpio_chip	chip;
	struct irq_chip		ic;
	const char		*name;
	unsigned int		virq;
	raw_spinlock_t		lock; /* protect gpio register */
};

/**
 * struct eqbr_pinctrl_drv_data:
 * @dev: device instance representing the controller.
 * @pctl_desc: pin controller descriptor.
 * @pctl_dev: pin control class device
 * @membase: base address of pin controller
 * @pin_banks: list of pin banks of the driver.
 * @nr_banks: number of pin banks.
 * @gpio_ctrls: list of gpio controllers.
 * @nr_gpio_ctrls: number of gpio controllers.
 * @lock: protect pinctrl register write
 */
struct eqbr_pinctrl_drv_data {
	struct device			*dev;
	struct pinctrl_desc		pctl_desc;
	struct pinctrl_dev		*pctl_dev;
	void __iomem			*membase;
	struct eqbr_pin_bank		*pin_banks;
	unsigned int			nr_banks;
	struct eqbr_gpio_ctrl		*gpio_ctrls;
	unsigned int			nr_gpio_ctrls;
	raw_spinlock_t			lock; /* protect pinpad register */
};

#endif /* __PINCTRL_EQUILIBRIUM_H */
