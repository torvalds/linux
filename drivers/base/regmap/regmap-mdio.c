// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define REGVAL_MASK		GENMASK(15, 0)
#define REGNUM_C22_MASK		GENMASK(4, 0)
/* Clause-45 mask includes the device type (5 bit) and actual register number (16 bit) */
#define REGNUM_C45_MASK		GENMASK(20, 0)

static int regmap_mdio_c22_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdio_dev = context;
	int ret;

	if (unlikely(reg & ~REGNUM_C22_MASK))
		return -ENXIO;

	ret = mdiodev_read(mdio_dev, reg);
	if (ret < 0)
		return ret;

	*val = ret & REGVAL_MASK;

	return 0;
}

static int regmap_mdio_c22_write(void *context, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdio_dev = context;

	if (unlikely(reg & ~REGNUM_C22_MASK))
		return -ENXIO;

	return mdiodev_write(mdio_dev, reg, val);
}

static const struct regmap_bus regmap_mdio_c22_bus = {
	.reg_write = regmap_mdio_c22_write,
	.reg_read = regmap_mdio_c22_read,
};

static int regmap_mdio_c45_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdio_dev = context;
	unsigned int devad;
	int ret;

	if (unlikely(reg & ~REGNUM_C45_MASK))
		return -ENXIO;

	devad = reg >> REGMAP_MDIO_C45_DEVAD_SHIFT;
	reg = reg & REGMAP_MDIO_C45_REGNUM_MASK;

	ret = mdiodev_c45_read(mdio_dev, devad, reg);
	if (ret < 0)
		return ret;

	*val = ret & REGVAL_MASK;

	return 0;
}

static int regmap_mdio_c45_write(void *context, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdio_dev = context;
	unsigned int devad;

	if (unlikely(reg & ~REGNUM_C45_MASK))
		return -ENXIO;

	devad = reg >> REGMAP_MDIO_C45_DEVAD_SHIFT;
	reg = reg & REGMAP_MDIO_C45_REGNUM_MASK;

	return mdiodev_c45_write(mdio_dev, devad, reg, val);
}

static const struct regmap_bus regmap_mdio_c45_bus = {
	.reg_write = regmap_mdio_c45_write,
	.reg_read = regmap_mdio_c45_read,
};

struct regmap *__regmap_init_mdio(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	const struct regmap_bus *bus;

	if (config->reg_bits == 5 && config->val_bits == 16)
		bus = &regmap_mdio_c22_bus;
	else if (config->reg_bits == 21 && config->val_bits == 16)
		bus = &regmap_mdio_c45_bus;
	else
		return ERR_PTR(-EOPNOTSUPP);

	return __regmap_init(&mdio_dev->dev, bus, mdio_dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_mdio);

struct regmap *__devm_regmap_init_mdio(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	const struct regmap_bus *bus;

	if (config->reg_bits == 5 && config->val_bits == 16)
		bus = &regmap_mdio_c22_bus;
	else if (config->reg_bits == 21 && config->val_bits == 16)
		bus = &regmap_mdio_c45_bus;
	else
		return ERR_PTR(-EOPNOTSUPP);

	return __devm_regmap_init(&mdio_dev->dev, bus, mdio_dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_mdio);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Regmap MDIO Module");
MODULE_LICENSE("GPL v2");
