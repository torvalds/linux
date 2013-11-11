/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */

#include <linux/platform_device.h>
#include <linux/mdio-mux.h>
#include <linux/of_mdio.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/phy.h>

#define DRV_VERSION "1.0"
#define DRV_DESCRIPTION "MDIO bus multiplexer driver"

struct mdio_mux_child_bus;

struct mdio_mux_parent_bus {
	struct mii_bus *mii_bus;
	int current_child;
	int parent_id;
	void *switch_data;
	int (*switch_fn)(int current_child, int desired_child, void *data);

	/* List of our children linked through their next fields. */
	struct mdio_mux_child_bus *children;
};

struct mdio_mux_child_bus {
	struct mii_bus *mii_bus;
	struct mdio_mux_parent_bus *parent;
	struct mdio_mux_child_bus *next;
	int bus_number;
	int phy_irq[PHY_MAX_ADDR];
};

/*
 * The parent bus' lock is used to order access to the switch_fn.
 */
static int mdio_mux_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct mdio_mux_child_bus *cb = bus->priv;
	struct mdio_mux_parent_bus *pb = cb->parent;
	int r;

	/* In theory multiple mdio_mux could be stacked, thus creating
	 * more than a single level of nesting.  But in practice,
	 * SINGLE_DEPTH_NESTING will cover the vast majority of use
	 * cases.  We use it, instead of trying to handle the general
	 * case.
	 */
	mutex_lock_nested(&pb->mii_bus->mdio_lock, SINGLE_DEPTH_NESTING);
	r = pb->switch_fn(pb->current_child, cb->bus_number, pb->switch_data);
	if (r)
		goto out;

	pb->current_child = cb->bus_number;

	r = pb->mii_bus->read(pb->mii_bus, phy_id, regnum);
out:
	mutex_unlock(&pb->mii_bus->mdio_lock);

	return r;
}

/*
 * The parent bus' lock is used to order access to the switch_fn.
 */
static int mdio_mux_write(struct mii_bus *bus, int phy_id,
			  int regnum, u16 val)
{
	struct mdio_mux_child_bus *cb = bus->priv;
	struct mdio_mux_parent_bus *pb = cb->parent;

	int r;

	mutex_lock_nested(&pb->mii_bus->mdio_lock, SINGLE_DEPTH_NESTING);
	r = pb->switch_fn(pb->current_child, cb->bus_number, pb->switch_data);
	if (r)
		goto out;

	pb->current_child = cb->bus_number;

	r = pb->mii_bus->write(pb->mii_bus, phy_id, regnum, val);
out:
	mutex_unlock(&pb->mii_bus->mdio_lock);

	return r;
}

static int parent_count;

int mdio_mux_init(struct device *dev,
		  int (*switch_fn)(int cur, int desired, void *data),
		  void **mux_handle,
		  void *data)
{
	struct device_node *parent_bus_node;
	struct device_node *child_bus_node;
	int r, ret_val;
	struct mii_bus *parent_bus;
	struct mdio_mux_parent_bus *pb;
	struct mdio_mux_child_bus *cb;

	if (!dev->of_node)
		return -ENODEV;

	parent_bus_node = of_parse_phandle(dev->of_node, "mdio-parent-bus", 0);

	if (!parent_bus_node)
		return -ENODEV;

	parent_bus = of_mdio_find_bus(parent_bus_node);
	if (parent_bus == NULL) {
		ret_val = -EPROBE_DEFER;
		goto err_parent_bus;
	}

	pb = devm_kzalloc(dev, sizeof(*pb), GFP_KERNEL);
	if (pb == NULL) {
		ret_val = -ENOMEM;
		goto err_parent_bus;
	}

	pb->switch_data = data;
	pb->switch_fn = switch_fn;
	pb->current_child = -1;
	pb->parent_id = parent_count++;
	pb->mii_bus = parent_bus;

	ret_val = -ENODEV;
	for_each_available_child_of_node(dev->of_node, child_bus_node) {
		u32 v;

		r = of_property_read_u32(child_bus_node, "reg", &v);
		if (r)
			continue;

		cb = devm_kzalloc(dev, sizeof(*cb), GFP_KERNEL);
		if (cb == NULL) {
			dev_err(dev,
				"Error: Failed to allocate memory for child\n");
			ret_val = -ENOMEM;
			break;
		}
		cb->bus_number = v;
		cb->parent = pb;
		cb->mii_bus = mdiobus_alloc();
		cb->mii_bus->priv = cb;

		cb->mii_bus->irq = cb->phy_irq;
		cb->mii_bus->name = "mdio_mux";
		snprintf(cb->mii_bus->id, MII_BUS_ID_SIZE, "%x.%x",
			 pb->parent_id, v);
		cb->mii_bus->parent = dev;
		cb->mii_bus->read = mdio_mux_read;
		cb->mii_bus->write = mdio_mux_write;
		r = of_mdiobus_register(cb->mii_bus, child_bus_node);
		if (r) {
			mdiobus_free(cb->mii_bus);
			devm_kfree(dev, cb);
		} else {
			of_node_get(child_bus_node);
			cb->next = pb->children;
			pb->children = cb;
		}
	}
	if (pb->children) {
		*mux_handle = pb;
		dev_info(dev, "Version " DRV_VERSION "\n");
		return 0;
	}
err_parent_bus:
	of_node_put(parent_bus_node);
	return ret_val;
}
EXPORT_SYMBOL_GPL(mdio_mux_init);

void mdio_mux_uninit(void *mux_handle)
{
	struct mdio_mux_parent_bus *pb = mux_handle;
	struct mdio_mux_child_bus *cb = pb->children;

	while (cb) {
		mdiobus_unregister(cb->mii_bus);
		mdiobus_free(cb->mii_bus);
		cb = cb->next;
	}
}
EXPORT_SYMBOL_GPL(mdio_mux_uninit);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
