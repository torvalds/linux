// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PHY package support
 */

#include <linux/of.h>
#include <linux/phy.h>

#include "phylib.h"
#include "phylib-internal.h"

/**
 * struct phy_package_shared - Shared information in PHY packages
 * @base_addr: Base PHY address of PHY package used to combine PHYs
 *   in one package and for offset calculation of phy_package_read/write
 * @np: Pointer to the Device Node if PHY package defined in DT
 * @refcnt: Number of PHYs connected to this shared data
 * @flags: Initialization of PHY package
 * @priv_size: Size of the shared private data @priv
 * @priv: Driver private data shared across a PHY package
 *
 * Represents a shared structure between different phydev's in the same
 * package, for example a quad PHY. See phy_package_join() and
 * phy_package_leave().
 */
struct phy_package_shared {
	u8 base_addr;
	/* With PHY package defined in DT this points to the PHY package node */
	struct device_node *np;
	refcount_t refcnt;
	unsigned long flags;
	size_t priv_size;

	/* private data pointer */
	/* note that this pointer is shared between different phydevs and
	 * the user has to take care of appropriate locking. It is allocated
	 * and freed automatically by phy_package_join() and
	 * phy_package_leave().
	 */
	void *priv;
};

struct device_node *phy_package_get_node(struct phy_device *phydev)
{
	return phydev->shared->np;
}
EXPORT_SYMBOL_GPL(phy_package_get_node);

void *phy_package_get_priv(struct phy_device *phydev)
{
	return phydev->shared->priv;
}
EXPORT_SYMBOL_GPL(phy_package_get_priv);

int phy_package_address(struct phy_device *phydev, unsigned int addr_offset)
{
	struct phy_package_shared *shared = phydev->shared;
	u8 base_addr = shared->base_addr;

	if (addr_offset >= PHY_MAX_ADDR - base_addr)
		return -EIO;

	/* we know that addr will be in the range 0..31 and thus the
	 * implicit cast to a signed int is not a problem.
	 */
	return base_addr + addr_offset;
}

int __phy_package_read(struct phy_device *phydev, unsigned int addr_offset,
		       u32 regnum)
{
	int addr = phy_package_address(phydev, addr_offset);

	if (addr < 0)
		return addr;

	return __mdiobus_read(phydev->mdio.bus, addr, regnum);
}
EXPORT_SYMBOL_GPL(__phy_package_read);

int __phy_package_write(struct phy_device *phydev, unsigned int addr_offset,
			u32 regnum, u16 val)
{
	int addr = phy_package_address(phydev, addr_offset);

	if (addr < 0)
		return addr;

	return __mdiobus_write(phydev->mdio.bus, addr, regnum, val);
}
EXPORT_SYMBOL_GPL(__phy_package_write);

static bool __phy_package_set_once(struct phy_device *phydev, unsigned int b)
{
	struct phy_package_shared *shared = phydev->shared;

	if (!shared)
		return false;

	return !test_and_set_bit(b, &shared->flags);
}

bool phy_package_init_once(struct phy_device *phydev)
{
	return __phy_package_set_once(phydev, 0);
}
EXPORT_SYMBOL_GPL(phy_package_init_once);

bool phy_package_probe_once(struct phy_device *phydev)
{
	return __phy_package_set_once(phydev, 1);
}
EXPORT_SYMBOL_GPL(phy_package_probe_once);

/**
 * phy_package_join - join a common PHY group
 * @phydev: target phy_device struct
 * @base_addr: cookie and base PHY address of PHY package for offset
 *   calculation of global register access
 * @priv_size: if non-zero allocate this amount of bytes for private data
 *
 * This joins a PHY group and provides a shared storage for all phydevs in
 * this group. This is intended to be used for packages which contain
 * more than one PHY, for example a quad PHY transceiver.
 *
 * The base_addr parameter serves as cookie which has to have the same values
 * for all members of one group and as the base PHY address of the PHY package
 * for offset calculation to access generic registers of a PHY package.
 * Usually, one of the PHY addresses of the different PHYs in the package
 * provides access to these global registers.
 * The address which is given here, will be used in the phy_package_read()
 * and phy_package_write() convenience functions as base and added to the
 * passed offset in those functions.
 *
 * This will set the shared pointer of the phydev to the shared storage.
 * If this is the first call for a this cookie the shared storage will be
 * allocated. If priv_size is non-zero, the given amount of bytes are
 * allocated for the priv member.
 *
 * Returns < 1 on error, 0 on success. Esp. calling phy_package_join()
 * with the same cookie but a different priv_size is an error.
 */
int phy_package_join(struct phy_device *phydev, int base_addr, size_t priv_size)
{
	struct mii_bus *bus = phydev->mdio.bus;
	struct phy_package_shared *shared;
	int ret;

	if (base_addr < 0 || base_addr >= PHY_MAX_ADDR)
		return -EINVAL;

	mutex_lock(&bus->shared_lock);
	shared = bus->shared[base_addr];
	if (!shared) {
		ret = -ENOMEM;
		shared = kzalloc(sizeof(*shared), GFP_KERNEL);
		if (!shared)
			goto err_unlock;
		if (priv_size) {
			shared->priv = kzalloc(priv_size, GFP_KERNEL);
			if (!shared->priv)
				goto err_free;
			shared->priv_size = priv_size;
		}
		shared->base_addr = base_addr;
		shared->np = NULL;
		refcount_set(&shared->refcnt, 1);
		bus->shared[base_addr] = shared;
	} else {
		ret = -EINVAL;
		if (priv_size && priv_size != shared->priv_size)
			goto err_unlock;
		refcount_inc(&shared->refcnt);
	}
	mutex_unlock(&bus->shared_lock);

	phydev->shared = shared;

	return 0;

err_free:
	kfree(shared);
err_unlock:
	mutex_unlock(&bus->shared_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(phy_package_join);

/**
 * of_phy_package_join - join a common PHY group in PHY package
 * @phydev: target phy_device struct
 * @priv_size: if non-zero allocate this amount of bytes for private data
 *
 * This is a variant of phy_package_join for PHY package defined in DT.
 *
 * The parent node of the @phydev is checked as a valid PHY package node
 * structure (by matching the node name "ethernet-phy-package") and the
 * base_addr for the PHY package is passed to phy_package_join.
 *
 * With this configuration the shared struct will also have the np value
 * filled to use additional DT defined properties in PHY specific
 * probe_once and config_init_once PHY package OPs.
 *
 * Returns < 0 on error, 0 on success. Esp. calling phy_package_join()
 * with the same cookie but a different priv_size is an error. Or a parent
 * node is not detected or is not valid or doesn't match the expected node
 * name for PHY package.
 */
int of_phy_package_join(struct phy_device *phydev, size_t priv_size)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct device_node *package_node;
	u32 base_addr;
	int ret;

	if (!node)
		return -EINVAL;

	package_node = of_get_parent(node);
	if (!package_node)
		return -EINVAL;

	if (!of_node_name_eq(package_node, "ethernet-phy-package")) {
		ret = -EINVAL;
		goto exit;
	}

	if (of_property_read_u32(package_node, "reg", &base_addr)) {
		ret = -EINVAL;
		goto exit;
	}

	ret = phy_package_join(phydev, base_addr, priv_size);
	if (ret)
		goto exit;

	phydev->shared->np = package_node;

	return 0;
exit:
	of_node_put(package_node);
	return ret;
}
EXPORT_SYMBOL_GPL(of_phy_package_join);

/**
 * phy_package_leave - leave a common PHY group
 * @phydev: target phy_device struct
 *
 * This leaves a PHY group created by phy_package_join(). If this phydev
 * was the last user of the shared data between the group, this data is
 * freed. Resets the phydev->shared pointer to NULL.
 */
void phy_package_leave(struct phy_device *phydev)
{
	struct phy_package_shared *shared = phydev->shared;
	struct mii_bus *bus = phydev->mdio.bus;

	if (!shared)
		return;

	/* Decrease the node refcount on leave if present */
	if (shared->np)
		of_node_put(shared->np);

	if (refcount_dec_and_mutex_lock(&shared->refcnt, &bus->shared_lock)) {
		bus->shared[shared->base_addr] = NULL;
		mutex_unlock(&bus->shared_lock);
		kfree(shared->priv);
		kfree(shared);
	}

	phydev->shared = NULL;
}
EXPORT_SYMBOL_GPL(phy_package_leave);

static void devm_phy_package_leave(struct device *dev, void *res)
{
	phy_package_leave(*(struct phy_device **)res);
}

/**
 * devm_phy_package_join - resource managed phy_package_join()
 * @dev: device that is registering this PHY package
 * @phydev: target phy_device struct
 * @base_addr: cookie and base PHY address of PHY package for offset
 *   calculation of global register access
 * @priv_size: if non-zero allocate this amount of bytes for private data
 *
 * Managed phy_package_join(). Shared storage fetched by this function,
 * phy_package_leave() is automatically called on driver detach. See
 * phy_package_join() for more information.
 */
int devm_phy_package_join(struct device *dev, struct phy_device *phydev,
			  int base_addr, size_t priv_size)
{
	struct phy_device **ptr;
	int ret;

	ptr = devres_alloc(devm_phy_package_leave, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = phy_package_join(phydev, base_addr, priv_size);

	if (!ret) {
		*ptr = phydev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_phy_package_join);

/**
 * devm_of_phy_package_join - resource managed of_phy_package_join()
 * @dev: device that is registering this PHY package
 * @phydev: target phy_device struct
 * @priv_size: if non-zero allocate this amount of bytes for private data
 *
 * Managed of_phy_package_join(). Shared storage fetched by this function,
 * phy_package_leave() is automatically called on driver detach. See
 * of_phy_package_join() for more information.
 */
int devm_of_phy_package_join(struct device *dev, struct phy_device *phydev,
			     size_t priv_size)
{
	struct phy_device **ptr;
	int ret;

	ptr = devres_alloc(devm_phy_package_leave, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = of_phy_package_join(phydev, priv_size);

	if (!ret) {
		*ptr = phydev;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_of_phy_package_join);
