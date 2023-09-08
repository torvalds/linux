/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 William Breathitt Gray */
#ifndef _I8255_H_
#define _I8255_H_

struct device;
struct irq_domain;
struct regmap;

#define i8255_volatile_regmap_range(_base) regmap_reg_range(_base, _base + 0x2)

/**
 * struct i8255_regmap_config - Configuration for the register map of an i8255
 * @parent:	parent device
 * @map:	regmap for the i8255
 * @num_ppi:	number of i8255 Programmable Peripheral Interface
 * @names:	(optional) array of names for gpios
 * @domain:	(optional) IRQ domain if the controller is interrupt-capable
 *
 * Note: The regmap is expected to have cache enabled and i8255 control
 * registers not marked as volatile.
 */
struct i8255_regmap_config {
	struct device *parent;
	struct regmap *map;
	int num_ppi;
	const char *const *names;
	struct irq_domain *domain;
};

int devm_i8255_regmap_register(struct device *dev,
			       const struct i8255_regmap_config *config);

#endif /* _I8255_H_ */
