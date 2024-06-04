/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ENS160_H_
#define ENS160_H_

int devm_ens160_core_probe(struct device *dev, struct regmap *regmap, int irq,
			   const char *name);

extern const struct dev_pm_ops ens160_pm_ops;

#endif
