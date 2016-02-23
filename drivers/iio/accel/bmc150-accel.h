#ifndef _BMC150_ACCEL_H_
#define _BMC150_ACCEL_H_

struct regmap;

enum {
	bmc150,
	bmi055,
	bma255,
	bma250e,
	bma222e,
	bma280,
};

int bmc150_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    const char *name, bool block_supported);
int bmc150_accel_core_remove(struct device *dev);
extern const struct dev_pm_ops bmc150_accel_pm_ops;

#endif  /* _BMC150_ACCEL_H_ */
