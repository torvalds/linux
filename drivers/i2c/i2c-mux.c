/*
 * Multiplexed I2C bus driver.
 *
 * Copyright (c) 2008-2009 Rodolfo Giometti <giometti@linux.it>
 * Copyright (c) 2008-2009 Eurotech S.p.A. <info@eurotech.it>
 * Copyright (c) 2009-2010 NSN GmbH & Co KG <michael.lawnick.ext@nsn.com>
 *
 * Simplifies access to complex multiplexed I2C bus topologies, by presenting
 * each multiplexed bus segment as an additional I2C adapter.
 * Supports multi-level mux'ing (mux behind a mux).
 *
 * Based on:
 *	i2c-virt.c from Kumar Gala <galak@kernel.crashing.org>
 *	i2c-virtual.c from Ken Harrenstien, Copyright (c) 2004 Google, Inc.
 *	i2c-virtual.c from Brian Kuschak <bkuschak@yahoo.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

/* multiplexer per channel data */
struct i2c_mux_priv {
	struct i2c_adapter adap;
	struct i2c_algorithm algo;

	struct i2c_adapter *parent;
	struct device *mux_dev;
	void *mux_priv;
	u32 chan_id;

	int (*select)(struct i2c_adapter *, void *mux_priv, u32 chan_id);
	int (*deselect)(struct i2c_adapter *, void *mux_priv, u32 chan_id);
};

static int i2c_mux_master_xfer(struct i2c_adapter *adap,
			       struct i2c_msg msgs[], int num)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_adapter *parent = priv->parent;
	int ret;

	/* Switch to the right mux port and perform the transfer. */

	ret = priv->select(parent, priv->mux_priv, priv->chan_id);
	if (ret >= 0)
		ret = __i2c_transfer(parent, msgs, num);
	if (priv->deselect)
		priv->deselect(parent, priv->mux_priv, priv->chan_id);

	return ret;
}

static int i2c_mux_smbus_xfer(struct i2c_adapter *adap,
			      u16 addr, unsigned short flags,
			      char read_write, u8 command,
			      int size, union i2c_smbus_data *data)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_adapter *parent = priv->parent;
	int ret;

	/* Select the right mux port and perform the transfer. */

	ret = priv->select(parent, priv->mux_priv, priv->chan_id);
	if (ret >= 0)
		ret = parent->algo->smbus_xfer(parent, addr, flags,
					read_write, command, size, data);
	if (priv->deselect)
		priv->deselect(parent, priv->mux_priv, priv->chan_id);

	return ret;
}

/* Return the parent's functionality */
static u32 i2c_mux_functionality(struct i2c_adapter *adap)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_adapter *parent = priv->parent;

	return parent->algo->functionality(parent);
}

/* Return all parent classes, merged */
static unsigned int i2c_mux_parent_classes(struct i2c_adapter *parent)
{
	unsigned int class = 0;

	do {
		class |= parent->class;
		parent = i2c_parent_is_i2c_adapter(parent);
	} while (parent);

	return class;
}

struct i2c_adapter *i2c_add_mux_adapter(struct i2c_adapter *parent,
				struct device *mux_dev,
				void *mux_priv, u32 force_nr, u32 chan_id,
				unsigned int class,
				int (*select) (struct i2c_adapter *,
					       void *, u32),
				int (*deselect) (struct i2c_adapter *,
						 void *, u32))
{
	struct i2c_mux_priv *priv;
	char symlink_name[20];
	int ret;

	priv = kzalloc(sizeof(struct i2c_mux_priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	/* Set up private adapter data */
	priv->parent = parent;
	priv->mux_dev = mux_dev;
	priv->mux_priv = mux_priv;
	priv->chan_id = chan_id;
	priv->select = select;
	priv->deselect = deselect;

	/* Need to do algo dynamically because we don't know ahead
	 * of time what sort of physical adapter we'll be dealing with.
	 */
	if (parent->algo->master_xfer)
		priv->algo.master_xfer = i2c_mux_master_xfer;
	if (parent->algo->smbus_xfer)
		priv->algo.smbus_xfer = i2c_mux_smbus_xfer;
	priv->algo.functionality = i2c_mux_functionality;

	/* Now fill out new adapter structure */
	snprintf(priv->adap.name, sizeof(priv->adap.name),
		 "i2c-%d-mux (chan_id %d)", i2c_adapter_id(parent), chan_id);
	priv->adap.owner = THIS_MODULE;
	priv->adap.algo = &priv->algo;
	priv->adap.algo_data = priv;
	priv->adap.dev.parent = &parent->dev;
	priv->adap.retries = parent->retries;
	priv->adap.timeout = parent->timeout;
	priv->adap.quirks = parent->quirks;

	/* Sanity check on class */
	if (i2c_mux_parent_classes(parent) & class)
		dev_err(&parent->dev,
			"Segment %d behind mux can't share classes with ancestors\n",
			chan_id);
	else
		priv->adap.class = class;

	/*
	 * Try to populate the mux adapter's of_node, expands to
	 * nothing if !CONFIG_OF.
	 */
	if (mux_dev->of_node) {
		struct device_node *child;
		u32 reg;

		for_each_child_of_node(mux_dev->of_node, child) {
			ret = of_property_read_u32(child, "reg", &reg);
			if (ret)
				continue;
			if (chan_id == reg) {
				priv->adap.dev.of_node = child;
				break;
			}
		}
	}

	/*
	 * Associate the mux channel with an ACPI node.
	 */
	if (has_acpi_companion(mux_dev))
		acpi_preset_companion(&priv->adap.dev, ACPI_COMPANION(mux_dev),
				      chan_id);

	if (force_nr) {
		priv->adap.nr = force_nr;
		ret = i2c_add_numbered_adapter(&priv->adap);
	} else {
		ret = i2c_add_adapter(&priv->adap);
	}
	if (ret < 0) {
		dev_err(&parent->dev,
			"failed to add mux-adapter (error=%d)\n",
			ret);
		kfree(priv);
		return NULL;
	}

	WARN(sysfs_create_link(&priv->adap.dev.kobj, &mux_dev->kobj, "mux_device"),
			       "can't create symlink to mux device\n");

	snprintf(symlink_name, sizeof(symlink_name), "channel-%u", chan_id);
	WARN(sysfs_create_link(&mux_dev->kobj, &priv->adap.dev.kobj, symlink_name),
			       "can't create symlink for channel %u\n", chan_id);
	dev_info(&parent->dev, "Added multiplexed i2c bus %d\n",
		 i2c_adapter_id(&priv->adap));

	return &priv->adap;
}
EXPORT_SYMBOL_GPL(i2c_add_mux_adapter);

void i2c_del_mux_adapter(struct i2c_adapter *adap)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	char symlink_name[20];

	snprintf(symlink_name, sizeof(symlink_name), "channel-%u", priv->chan_id);
	sysfs_remove_link(&priv->mux_dev->kobj, symlink_name);

	sysfs_remove_link(&priv->adap.dev.kobj, "mux_device");
	i2c_del_adapter(adap);
	kfree(priv);
}
EXPORT_SYMBOL_GPL(i2c_del_mux_adapter);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("I2C driver for multiplexed I2C busses");
MODULE_LICENSE("GPL v2");
