#ifndef BMG160_H_
#define BMG160_H_

extern const struct dev_pm_ops bmg160_pm_ops;

int bmg160_core_probe(struct device *dev, struct regmap *regmap, int irq,
		      const char *name);
void bmg160_core_remove(struct device *dev);

#endif  /* BMG160_H_ */
