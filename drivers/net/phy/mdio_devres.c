// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/phy.h>

/**
 * __devm_mdiobus_register - Resource-managed variant of mdiobus_register()
 * @bus:	MII bus structure to register
 * @owner:	Owning module
 *
 * Returns 0 on success, negative error number on failure.
 */
int __devm_mdiobus_register(struct mii_bus *bus, struct module *owner)
{
	int ret;

	if (!bus->is_managed)
		return -EPERM;

	ret = __mdiobus_register(bus, owner);
	if (!ret)
		bus->is_managed_registered = 1;

	return ret;
}
EXPORT_SYMBOL(__devm_mdiobus_register);
