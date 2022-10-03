/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pin controller and GPIO driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 */

#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/module.h>

struct meson_pinctrl;

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
	const void *data;
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
	MESON_REG_PULLEN,
	MESON_REG_PULL,
	MESON_REG_DIR,
	MESON_REG_OUT,
	MESON_REG_IN,
	MESON_REG_DS,
	MESON_NUM_REG,
};

/**
 * enum meson_pinconf_drv - value of drive-strength supported
 */
enum meson_pinconf_drv {
	MESON_PINCONF_DRV_500UA,
	MESON_PINCONF_DRV_2500UA,
	MESON_PINCONF_DRV_3000UA,
	MESON_PINCONF_DRV_4000UA,
};

/**
 * struct meson bank
 *
 * @name:	bank name
 * @first:	first pin of the bank
 * @last:	last pin of the bank
 * @irq:	hwirq base number of the bank
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
	int irq_first;
	int irq_last;
	struct meson_reg_desc regs[MESON_NUM_REG];
};

struct meson_pinctrl_data {
	const char *name;
	const struct pinctrl_pin_desc *pins;
	struct meson_pmx_group *groups;
	struct meson_pmx_func *funcs;
	unsigned int num_pins;
	unsigned int num_groups;
	unsigned int num_funcs;
	struct meson_bank *banks;
	unsigned int num_banks;
	const struct pinmux_ops *pmx_ops;
	void *pmx_data;
	int (*parse_dt)(struct meson_pinctrl *pc);
};

struct meson_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pcdev;
	struct pinctrl_desc desc;
	struct meson_pinctrl_data *data;
	struct regmap *reg_mux;
	struct regmap *reg_pullen;
	struct regmap *reg_pull;
	struct regmap *reg_gpio;
	struct regmap *reg_ds;
	struct gpio_chip chip;
	struct device_node *of_node;
};

#define FUNCTION(fn)							\
	{								\
		.name = #fn,						\
		.groups = fn ## _groups,				\
		.num_groups = ARRAY_SIZE(fn ## _groups),		\
	}

#define BANK_DS(n, f, l, fi, li, per, peb, pr, pb, dr, db, or, ob, ir, ib,     \
		dsr, dsb)                                                      \
	{								\
		.name		= n,					\
		.first		= f,					\
		.last		= l,					\
		.irq_first	= fi,					\
		.irq_last	= li,					\
		.regs = {						\
			[MESON_REG_PULLEN]	= { per, peb },		\
			[MESON_REG_PULL]	= { pr, pb },		\
			[MESON_REG_DIR]		= { dr, db },		\
			[MESON_REG_OUT]		= { or, ob },		\
			[MESON_REG_IN]		= { ir, ib },		\
			[MESON_REG_DS]		= { dsr, dsb },		\
		},							\
	 }

#define BANK(n, f, l, fi, li, per, peb, pr, pb, dr, db, or, ob, ir, ib) \
	BANK_DS(n, f, l, fi, li, per, peb, pr, pb, dr, db, or, ob, ir, ib, 0, 0)

#define MESON_PIN(x) PINCTRL_PIN(x, #x)

/* Common pmx functions */
int meson_pmx_get_funcs_count(struct pinctrl_dev *pcdev);
const char *meson_pmx_get_func_name(struct pinctrl_dev *pcdev,
				    unsigned selector);
int meson_pmx_get_groups(struct pinctrl_dev *pcdev,
			 unsigned selector,
			 const char * const **groups,
			 unsigned * const num_groups);

/* Common probe function */
int meson_pinctrl_probe(struct platform_device *pdev);
/* Common ao groups extra dt parse function for SoCs before g12a  */
int meson8_aobus_parse_dt_extra(struct meson_pinctrl *pc);
/* Common extra dt parse function for SoCs like A1  */
int meson_a1_parse_dt_extra(struct meson_pinctrl *pc);
