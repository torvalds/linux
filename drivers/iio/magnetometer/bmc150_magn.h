/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BMC150_MAGN_H_
#define _BMC150_MAGN_H_

extern const struct regmap_config bmc150_magn_regmap_config;
extern const struct dev_pm_ops bmc150_magn_pm_ops;

int bmc150_magn_probe(struct device *dev, struct regmap *regmap, int irq,
		      const char *name);
void bmc150_magn_remove(struct device *dev);

#endif /* _BMC150_MAGN_H_ */
