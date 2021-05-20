// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/regmap.h>

static int regmap_mdio_read(void *context, unsigned int reg, unsigned int *val)
{
	struct mdio_device *mdio_dev = context;
	int ret;

	ret = mdiobus_read(mdio_dev->bus, mdio_dev->addr, reg);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;
	return 0;
}

static int regmap_mdio_write(void *context, unsigned int reg, unsigned int val)
{
	struct mdio_device *mdio_dev = context;

	return mdiobus_write(mdio_dev->bus, mdio_dev->addr, reg, val);
}

static const struct regmap_bus regmap_mdio_bus = {
	.reg_write = regmap_mdio_write,
	.reg_read = regmap_mdio_read,
};

struct regmap *__regmap_init_mdio(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	if (config->reg_bits != 5 || config->val_bits != 16)
		return ERR_PTR(-EOPNOTSUPP);

	return __regmap_init(&mdio_dev->dev, &regmap_mdio_bus, mdio_dev, config,
		lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_mdio);

struct regmap *__devm_regmap_init_mdio(struct mdio_device *mdio_dev,
	const struct regmap_config *config, struct lock_class_key *lock_key,
	const char *lock_name)
{
	if (config->reg_bits != 5 || config->val_bits != 16)
		return ERR_PTR(-EOPNOTSUPP);

	return __devm_regmap_init(&mdio_dev->dev, &regmap_mdio_bus, mdio_dev,
		config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_mdio);

MODULE_AUTHOR("Sander Vanheule <sander@svanheule.net>");
MODULE_DESCRIPTION("Regmap MDIO Module");
MODULE_LICENSE("GPL v2");
