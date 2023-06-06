// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/stddef.h>

struct mdiobus_devres {
	struct mii_bus *mii;
};

static void devm_mdiobus_free(struct device *dev, void *this)
{
	struct mdiobus_devres *dr = this;

	mdiobus_free(dr->mii);
}

/**
 * devm_mdiobus_alloc_size - Resource-managed mdiobus_alloc_size()
 * @dev:		Device to allocate mii_bus for
 * @sizeof_priv:	Space to allocate for private structure
 *
 * Managed mdiobus_alloc_size. mii_bus allocated with this function is
 * automatically freed on driver detach.
 *
 * RETURNS:
 * Pointer to allocated mii_bus on success, NULL on out-of-memory error.
 */
struct mii_bus *devm_mdiobus_alloc_size(struct device *dev, int sizeof_priv)
{
	struct mdiobus_devres *dr;

	dr = devres_alloc(devm_mdiobus_free, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return NULL;

	dr->mii = mdiobus_alloc_size(sizeof_priv);
	if (!dr->mii) {
		devres_free(dr);
		return NULL;
	}

	devres_add(dev, dr);
	return dr->mii;
}
EXPORT_SYMBOL(devm_mdiobus_alloc_size);

static void devm_mdiobus_unregister(struct device *dev, void *this)
{
	struct mdiobus_devres *dr = this;

	mdiobus_unregister(dr->mii);
}

static int mdiobus_devres_match(struct device *dev,
				void *this, void *match_data)
{
	struct mdiobus_devres *res = this;
	struct mii_bus *mii = match_data;

	return mii == res->mii;
}

/**
 * __devm_mdiobus_register - Resource-managed variant of mdiobus_register()
 * @dev:	Device to register mii_bus for
 * @bus:	MII bus structure to register
 * @owner:	Owning module
 *
 * Returns 0 on success, negative error number on failure.
 */
int __devm_mdiobus_register(struct device *dev, struct mii_bus *bus,
			    struct module *owner)
{
	struct mdiobus_devres *dr;
	int ret;

	if (WARN_ON(!devres_find(dev, devm_mdiobus_free,
				 mdiobus_devres_match, bus)))
		return -EINVAL;

	dr = devres_alloc(devm_mdiobus_unregister, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = __mdiobus_register(bus, owner);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	dr->mii = bus;
	devres_add(dev, dr);
	return 0;
}
EXPORT_SYMBOL(__devm_mdiobus_register);

#if IS_ENABLED(CONFIG_OF_MDIO)
/**
 * __devm_of_mdiobus_register - Resource managed variant of of_mdiobus_register()
 * @dev:	Device to register mii_bus for
 * @mdio:	MII bus structure to register
 * @np:		Device node to parse
 * @owner:	Owning module
 */
int __devm_of_mdiobus_register(struct device *dev, struct mii_bus *mdio,
			       struct device_node *np, struct module *owner)
{
	struct mdiobus_devres *dr;
	int ret;

	if (WARN_ON(!devres_find(dev, devm_mdiobus_free,
				 mdiobus_devres_match, mdio)))
		return -EINVAL;

	dr = devres_alloc(devm_mdiobus_unregister, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = __of_mdiobus_register(mdio, np, owner);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	dr->mii = mdio;
	devres_add(dev, dr);
	return 0;
}
EXPORT_SYMBOL(__devm_of_mdiobus_register);
#endif /* CONFIG_OF_MDIO */

MODULE_LICENSE("GPL");
