/*
 * Pin controller and GPIO driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>
#include <linux/types.h>

/**
 * struct meson_pmx_group - a pinmux group
 *
 * @name:	group name
 * @pins:	pins in the group
 * @num_pins:	number of pins in the group
 * @is_gpio:	whether the group is a single GPIO group
 * @reg:	register offset for the group in the domain mux registers
 * @bit		bit index enabling the group
 * @domain:	index of the domain this group belongs to
 */
struct meson_pmx_group {
	const char *name;
	const unsigned int *pins;
	unsigned int num_pins;
	bool is_gpio;
	unsigned int reg;
	unsigned int bit;
};

/**
 * struct meson_pmx_func - a pinmux function
 *
 * @name:	function name
 * @groups:	groups in the function
 * @num_groups:	number of groups in the function
 */
struct meson_pmx_func {
	const char *name;
	const char * const *groups;
	unsigned int num_groups;
};

/**
 * struct meson_reg_desc - a register descriptor
 *
 * @reg:	register offset in the regmap
 * @bit:	bit index in register
 *
 * The structure describes the information needed to control pull,
 * pull-enable, direction, etc. for a single pin
 */
struct meson_reg_desc {
	unsigned int reg;
	unsigned int bit;
};

/**
 * enum meson_reg_type - type of registers encoded in @meson_reg_desc
 */
enum meson_reg_type {
	REG_PULLEN,
	REG_PULL,
	REG_DIR,
	REG_OUT,
	REG_IN,
	NUM_REG,
};

/**
 * struct meson bank
 *
 * @name:	bank name
 * @first:	first pin of the bank
 * @last:	last pin of the bank
 * @regs:	array of register descriptors
 *
 * A bank represents a set of pins controlled by a contiguous set of
 * bits in the domain registers. The structure specifies which bits in
 * the regmap control the different functionalities. Each member of
 * the @regs array refers to the first pin of the bank.
 */
struct meson_bank {
	const char *name;
	unsigned int first;
	unsigned int last;
	struct meson_reg_desc regs[NUM_REG];
};

/**
 * struct meson_domain_data - domain platform data
 *
 * @name:	name of the domain
 * @banks:	set of banks belonging to the domain
 * @num_banks:	number of banks in the domain
 */
struct meson_domain_data {
	const char *name;
	struct meson_bank *banks;
	unsigned int num_banks;
	unsigned int pin_base;
	unsigned int num_pins;
};

/**
 * struct meson_domain
 *
 * @reg_mux:	registers for mux settings
 * @reg_pullen:	registers for pull-enable settings
 * @reg_pull:	registers for pull settings
 * @reg_gpio:	registers for gpio settings
 * @chip:	gpio chip associated with the domain
 * @data;	platform data for the domain
 * @node:	device tree node for the domain
 *
 * A domain represents a set of banks controlled by the same set of
 * registers.
 */
struct meson_domain {
	struct regmap *reg_mux;
	struct regmap *reg_pullen;
	struct regmap *reg_pull;
	struct regmap *reg_gpio;

	struct gpio_chip chip;
	struct meson_domain_data *data;
	struct device_node *of_node;
};

struct meson_pinctrl_data {
	const struct pinctrl_pin_desc *pins;
	struct meson_pmx_group *groups;
	struct meson_pmx_func *funcs;
	struct meson_domain_data *domain_data;
	unsigned int num_pins;
	unsigned int num_groups;
	unsigned int num_funcs;
};

struct meson_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pcdev;
	struct pinctrl_desc desc;
	struct meson_pinctrl_data *data;
	struct meson_domain *domain;
};

#define PIN(x, b)	(b + x)

#define GROUP(grp, r, b)						\
	{								\
		.name = #grp,						\
		.pins = grp ## _pins,					\
		.num_pins = ARRAY_SIZE(grp ## _pins),			\
		.reg = r,						\
		.bit = b,						\
	 }

#define GPIO_GROUP(gpio, b)						\
	{								\
		.name = #gpio,						\
		.pins = (const unsigned int[]){ PIN(gpio, b) },		\
		.num_pins = 1,						\
		.is_gpio = true,					\
	 }

#define FUNCTION(fn)							\
	{								\
		.name = #fn,						\
		.groups = fn ## _groups,				\
		.num_groups = ARRAY_SIZE(fn ## _groups),		\
	}

#define BANK(n, f, l, per, peb, pr, pb, dr, db, or, ob, ir, ib)		\
	{								\
		.name	= n,						\
		.first	= f,						\
		.last	= l,						\
		.regs	= {						\
			[REG_PULLEN]	= { per, peb },			\
			[REG_PULL]	= { pr, pb },			\
			[REG_DIR]	= { dr, db },			\
			[REG_OUT]	= { or, ob },			\
			[REG_IN]	= { ir, ib },			\
		},							\
	 }

#define MESON_PIN(x, b) PINCTRL_PIN(PIN(x, b), #x)

extern struct meson_pinctrl_data meson8_cbus_pinctrl_data;
extern struct meson_pinctrl_data meson8_aobus_pinctrl_data;
extern struct meson_pinctrl_data meson8b_cbus_pinctrl_data;
extern struct meson_pinctrl_data meson8b_aobus_pinctrl_data;
extern struct meson_pinctrl_data meson_gxbb_periphs_pinctrl_data;
extern struct meson_pinctrl_data meson_gxbb_aobus_pinctrl_data;
