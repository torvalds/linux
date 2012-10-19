/*
 * pin-controller/pin-mux/pin-config/gpio-driver for Samsung's SoC's.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PINCTRL_SAMSUNG_H
#define __PINCTRL_SAMSUNG_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>

/* register offsets within a pin bank */
#define DAT_REG		0x4
#define PUD_REG		0x8
#define DRV_REG		0xC
#define CONPDN_REG	0x10
#define PUDPDN_REG	0x14

/* pinmux function number for pin as gpio output line */
#define FUNC_OUTPUT	0x1

/**
 * enum pincfg_type - possible pin configuration types supported.
 * @PINCFG_TYPE_PUD: Pull up/down configuration.
 * @PINCFG_TYPE_DRV: Drive strength configuration.
 * @PINCFG_TYPE_CON_PDN: Pin function in power down mode.
 * @PINCFG_TYPE_PUD_PDN: Pull up/down configuration in power down mode.
 */
enum pincfg_type {
	PINCFG_TYPE_PUD,
	PINCFG_TYPE_DRV,
	PINCFG_TYPE_CON_PDN,
	PINCFG_TYPE_PUD_PDN,
};

/*
 * pin configuration (pull up/down and drive strength) type and its value are
 * packed together into a 16-bits. The upper 8-bits represent the configuration
 * type and the lower 8-bits hold the value of the configuration type.
 */
#define PINCFG_TYPE_MASK		0xFF
#define PINCFG_VALUE_SHIFT		8
#define PINCFG_VALUE_MASK		(0xFF << PINCFG_VALUE_SHIFT)
#define PINCFG_PACK(type, value)	(((value) << PINCFG_VALUE_SHIFT) | type)
#define PINCFG_UNPACK_TYPE(cfg)		((cfg) & PINCFG_TYPE_MASK)
#define PINCFG_UNPACK_VALUE(cfg)	(((cfg) & PINCFG_VALUE_MASK) >> \
						PINCFG_VALUE_SHIFT)
/**
 * enum eint_type - possible external interrupt types.
 * @EINT_TYPE_NONE: bank does not support external interrupts
 * @EINT_TYPE_GPIO: bank supportes external gpio interrupts
 * @EINT_TYPE_WKUP: bank supportes external wakeup interrupts
 *
 * Samsung GPIO controller groups all the available pins into banks. The pins
 * in a pin bank can support external gpio interrupts or external wakeup
 * interrupts or no interrupts at all. From a software perspective, the only
 * difference between external gpio and external wakeup interrupts is that
 * the wakeup interrupts can additionally wakeup the system if it is in
 * suspended state.
 */
enum eint_type {
	EINT_TYPE_NONE,
	EINT_TYPE_GPIO,
	EINT_TYPE_WKUP,
};

/* maximum length of a pin in pin descriptor (example: "gpa0-0") */
#define PIN_NAME_LENGTH	10

#define PIN_GROUP(n, p, f)				\
	{						\
		.name		= n,			\
		.pins		= p,			\
		.num_pins	= ARRAY_SIZE(p),	\
		.func		= f			\
	}

#define PMX_FUNC(n, g)					\
	{						\
		.name		= n,			\
		.groups		= g,			\
		.num_groups	= ARRAY_SIZE(g),	\
	}

struct samsung_pinctrl_drv_data;

/**
 * struct samsung_pin_bank: represent a controller pin-bank.
 * @reg_offset: starting offset of the pin-bank registers.
 * @pin_base: starting pin number of the bank.
 * @nr_pins: number of pins included in this bank.
 * @func_width: width of the function selector bit field.
 * @pud_width: width of the pin pull up/down selector bit field.
 * @drv_width: width of the pin driver strength selector bit field.
 * @conpdn_width: width of the sleep mode function selector bin field.
 * @pudpdn_width: width of the sleep mode pull up/down selector bit field.
 * @eint_type: type of the external interrupt supported by the bank.
 * @irq_base: starting controller local irq number of the bank.
 * @name: name to be prefixed for each pin in this pin bank.
 */
struct samsung_pin_bank {
	u32		pctl_offset;
	u32		pin_base;
	u8		nr_pins;
	u8		func_width;
	u8		pud_width;
	u8		drv_width;
	u8		conpdn_width;
	u8		pudpdn_width;
	enum eint_type	eint_type;
	u32		irq_base;
	char		*name;
};

/**
 * struct samsung_pin_ctrl: represent a pin controller.
 * @pin_banks: list of pin banks included in this controller.
 * @nr_banks: number of pin banks.
 * @base: starting system wide pin number.
 * @nr_pins: number of pins supported by the controller.
 * @nr_gint: number of external gpio interrupts supported.
 * @nr_wint: number of external wakeup interrupts supported.
 * @geint_con: offset of the ext-gpio controller registers.
 * @geint_mask: offset of the ext-gpio interrupt mask registers.
 * @geint_pend: offset of the ext-gpio interrupt pending registers.
 * @weint_con: offset of the ext-wakeup controller registers.
 * @weint_mask: offset of the ext-wakeup interrupt mask registers.
 * @weint_pend: offset of the ext-wakeup interrupt pending registers.
 * @svc: offset of the interrupt service register.
 * @eint_gpio_init: platform specific callback to setup the external gpio
 *	interrupts for the controller.
 * @eint_wkup_init: platform specific callback to setup the external wakeup
 *	interrupts for the controller.
 * @label: for debug information.
 */
struct samsung_pin_ctrl {
	struct samsung_pin_bank	*pin_banks;
	u32		nr_banks;

	u32		base;
	u32		nr_pins;
	u32		nr_gint;
	u32		nr_wint;

	u32		geint_con;
	u32		geint_mask;
	u32		geint_pend;

	u32		weint_con;
	u32		weint_mask;
	u32		weint_pend;

	u32		svc;

	int		(*eint_gpio_init)(struct samsung_pinctrl_drv_data *);
	int		(*eint_wkup_init)(struct samsung_pinctrl_drv_data *);
	char		*label;
};

/**
 * struct samsung_pinctrl_drv_data: wrapper for holding driver data together.
 * @virt_base: register base address of the controller.
 * @dev: device instance representing the controller.
 * @irq: interrpt number used by the controller to notify gpio interrupts.
 * @ctrl: pin controller instance managed by the driver.
 * @pctl: pin controller descriptor registered with the pinctrl subsystem.
 * @pctl_dev: cookie representing pinctrl device instance.
 * @pin_groups: list of pin groups available to the driver.
 * @nr_groups: number of such pin groups.
 * @pmx_functions: list of pin functions available to the driver.
 * @nr_function: number of such pin functions.
 * @gc: gpio_chip instance registered with gpiolib.
 * @grange: linux gpio pin range supported by this controller.
 */
struct samsung_pinctrl_drv_data {
	void __iomem			*virt_base;
	struct device			*dev;
	int				irq;

	struct samsung_pin_ctrl		*ctrl;
	struct pinctrl_desc		pctl;
	struct pinctrl_dev		*pctl_dev;

	const struct samsung_pin_group	*pin_groups;
	unsigned int			nr_groups;
	const struct samsung_pmx_func	*pmx_functions;
	unsigned int			nr_functions;

	struct irq_domain		*gpio_irqd;
	struct irq_domain		*wkup_irqd;

	struct gpio_chip		*gc;
	struct pinctrl_gpio_range	grange;
};

/**
 * struct samsung_pin_group: represent group of pins of a pinmux function.
 * @name: name of the pin group, used to lookup the group.
 * @pins: the pins included in this group.
 * @num_pins: number of pins included in this group.
 * @func: the function number to be programmed when selected.
 */
struct samsung_pin_group {
	const char		*name;
	const unsigned int	*pins;
	u8			num_pins;
	u8			func;
};

/**
 * struct samsung_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @num_groups: number of groups included in @groups.
 */
struct samsung_pmx_func {
	const char		*name;
	const char		**groups;
	u8			num_groups;
};

/* list of all exported SoC specific data */
extern struct samsung_pin_ctrl exynos4210_pin_ctrl[];

#endif /* __PINCTRL_SAMSUNG_H */
