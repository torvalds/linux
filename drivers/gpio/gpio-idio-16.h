/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 William Breathitt Gray */
#ifndef _IDIO_16_H_
#define _IDIO_16_H_

struct device;
struct regmap;
struct regmap_irq;

/**
 * struct idio_16_regmap_config - Configuration for the IDIO-16 register map
 * @parent:		parent device
 * @map:		regmap for the IDIO-16 device
 * @regmap_irqs:	descriptors for individual IRQs
 * @num_regmap_irqs:	number of IRQ descriptors
 * @irq:		IRQ number for the IDIO-16 device
 * @no_status:		device has no status register
 * @filters:		device has input filters
 */
struct idio_16_regmap_config {
	struct device *parent;
	struct regmap *map;
	const struct regmap_irq *regmap_irqs;
	int num_regmap_irqs;
	unsigned int irq;
	bool no_status;
	bool filters;
};

int devm_idio_16_regmap_register(struct device *dev, const struct idio_16_regmap_config *config);

#endif /* _IDIO_16_H_ */
