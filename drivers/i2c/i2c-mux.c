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
	struct i2c_mux_core *muxc;
	u32 chan_id;
};

static int __i2c_mux_master_xfer(struct i2c_adapter *adap,
				 struct i2c_msg msgs[], int num)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_mux_core *muxc = priv->muxc;
	struct i2c_adapter *parent = muxc->parent;
	int ret;

	/* Switch to the right mux port and perform the transfer. */

	ret = muxc->select(muxc, priv->chan_id);
	if (ret >= 0)
		ret = __i2c_transfer(parent, msgs, num);
	if (muxc->deselect)
		muxc->deselect(muxc, priv->chan_id);

	return ret;
}

static int i2c_mux_master_xfer(struct i2c_adapter *adap,
			       struct i2c_msg msgs[], int num)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_mux_core *muxc = priv->muxc;
	struct i2c_adapter *parent = muxc->parent;
	int ret;

	/* Switch to the right mux port and perform the transfer. */

	ret = muxc->select(muxc, priv->chan_id);
	if (ret >= 0)
		ret = i2c_transfer(parent, msgs, num);
	if (muxc->deselect)
		muxc->deselect(muxc, priv->chan_id);

	return ret;
}

static int __i2c_mux_smbus_xfer(struct i2c_adapter *adap,
				u16 addr, unsigned short flags,
				char read_write, u8 command,
				int size, union i2c_smbus_data *data)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_mux_core *muxc = priv->muxc;
	struct i2c_adapter *parent = muxc->parent;
	int ret;

	/* Select the right mux port and perform the transfer. */

	ret = muxc->select(muxc, priv->chan_id);
	if (ret >= 0)
		ret = parent->algo->smbus_xfer(parent, addr, flags,
					read_write, command, size, data);
	if (muxc->deselect)
		muxc->deselect(muxc, priv->chan_id);

	return ret;
}

static int i2c_mux_smbus_xfer(struct i2c_adapter *adap,
			      u16 addr, unsigned short flags,
			      char read_write, u8 command,
			      int size, union i2c_smbus_data *data)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_mux_core *muxc = priv->muxc;
	struct i2c_adapter *parent = muxc->parent;
	int ret;

	/* Select the right mux port and perform the transfer. */

	ret = muxc->select(muxc, priv->chan_id);
	if (ret >= 0)
		ret = i2c_smbus_xfer(parent, addr, flags,
				     read_write, command, size, data);
	if (muxc->deselect)
		muxc->deselect(muxc, priv->chan_id);

	return ret;
}

/* Return the parent's functionality */
static u32 i2c_mux_functionality(struct i2c_adapter *adap)
{
	struct i2c_mux_priv *priv = adap->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

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

static void i2c_mux_lock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	rt_mutex_lock(&parent->mux_lock);
	if (!(flags & I2C_LOCK_ROOT_ADAPTER))
		return;
	i2c_lock_bus(parent, flags);
}

static int i2c_mux_trylock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	if (!rt_mutex_trylock(&parent->mux_lock))
		return 0;	/* mux_lock not locked, failure */
	if (!(flags & I2C_LOCK_ROOT_ADAPTER))
		return 1;	/* we only want mux_lock, success */
	if (i2c_trylock_bus(parent, flags))
		return 1;	/* parent locked too, success */
	rt_mutex_unlock(&parent->mux_lock);
	return 0;		/* parent not locked, failure */
}

static void i2c_mux_unlock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	if (flags & I2C_LOCK_ROOT_ADAPTER)
		i2c_unlock_bus(parent, flags);
	rt_mutex_unlock(&parent->mux_lock);
}

static void i2c_parent_lock_bus(struct i2c_adapter *adapter,
				unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	rt_mutex_lock(&parent->mux_lock);
	i2c_lock_bus(parent, flags);
}

static int i2c_parent_trylock_bus(struct i2c_adapter *adapter,
				  unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	if (!rt_mutex_trylock(&parent->mux_lock))
		return 0;	/* mux_lock not locked, failure */
	if (i2c_trylock_bus(parent, flags))
		return 1;	/* parent locked too, success */
	rt_mutex_unlock(&parent->mux_lock);
	return 0;		/* parent not locked, failure */
}

static void i2c_parent_unlock_bus(struct i2c_adapter *adapter,
				  unsigned int flags)
{
	struct i2c_mux_priv *priv = adapter->algo_data;
	struct i2c_adapter *parent = priv->muxc->parent;

	i2c_unlock_bus(parent, flags);
	rt_mutex_unlock(&parent->mux_lock);
}

struct i2c_adapter *i2c_root_adapter(struct device *dev)
{
	struct device *i2c;
	struct i2c_adapter *i2c_root;

	/*
	 * Walk up the device tree to find an i2c adapter, indicating
	 * that this is an i2c client device. Check all ancestors to
	 * handle mfd devices etc.
	 */
	for (i2c = dev; i2c; i2c = i2c->parent) {
		if (i2c->type == &i2c_adapter_type)
			break;
	}
	if (!i2c)
		return NULL;

	/* Continue up the tree to find the root i2c adapter */
	i2c_root = to_i2c_adapter(i2c);
	while (i2c_parent_is_i2c_adapter(i2c_root))
		i2c_root = i2c_parent_is_i2c_adapter(i2c_root);

	return i2c_root;
}
EXPORT_SYMBOL_GPL(i2c_root_adapter);

struct i2c_mux_core *i2c_mux_alloc(struct i2c_adapter *parent,
				   struct device *dev, int max_adapters,
				   int sizeof_priv, u32 flags,
				   int (*select)(struct i2c_mux_core *, u32),
				   int (*deselect)(struct i2c_mux_core *, u32))
{
	struct i2c_mux_core *muxc;

	muxc = devm_kzalloc(dev, sizeof(*muxc)
			    + max_adapters * sizeof(muxc->adapter[0])
			    + sizeof_priv, GFP_KERNEL);
	if (!muxc)
		return NULL;
	if (sizeof_priv)
		muxc->priv = &muxc->adapter[max_adapters];

	muxc->parent = parent;
	muxc->dev = dev;
	if (flags & I2C_MUX_LOCKED)
		muxc->mux_locked = true;
	muxc->select = select;
	muxc->deselect = deselect;
	muxc->max_adapters = max_adapters;

	return muxc;
}
EXPORT_SYMBOL_GPL(i2c_mux_alloc);

int i2c_mux_add_adapter(struct i2c_mux_core *muxc,
			u32 force_nr, u32 chan_id,
			unsigned int class)
{
	struct i2c_adapter *parent = muxc->parent;
	struct i2c_mux_priv *priv;
	char symlink_name[20];
	int ret;

	if (muxc->num_adapters >= muxc->max_adapters) {
		dev_err(muxc->dev, "No room for more i2c-mux adapters\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Set up private adapter data */
	priv->muxc = muxc;
	priv->chan_id = chan_id;

	/* Need to do algo dynamically because we don't know ahead
	 * of time what sort of physical adapter we'll be dealing with.
	 */
	if (parent->algo->master_xfer) {
		if (muxc->mux_locked)
			priv->algo.master_xfer = i2c_mux_master_xfer;
		else
			priv->algo.master_xfer = __i2c_mux_master_xfer;
	}
	if (parent->algo->smbus_xfer) {
		if (muxc->mux_locked)
			priv->algo.smbus_xfer = i2c_mux_smbus_xfer;
		else
			priv->algo.smbus_xfer = __i2c_mux_smbus_xfer;
	}
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
	if (muxc->mux_locked) {
		priv->adap.lock_bus = i2c_mux_lock_bus;
		priv->adap.trylock_bus = i2c_mux_trylock_bus;
		priv->adap.unlock_bus = i2c_mux_unlock_bus;
	} else {
		priv->adap.lock_bus = i2c_parent_lock_bus;
		priv->adap.trylock_bus = i2c_parent_trylock_bus;
		priv->adap.unlock_bus = i2c_parent_unlock_bus;
	}

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
	if (muxc->dev->of_node) {
		struct device_node *child;
		u32 reg;

		for_each_child_of_node(muxc->dev->of_node, child) {
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
	if (has_acpi_companion(muxc->dev))
		acpi_preset_companion(&priv->adap.dev,
				      ACPI_COMPANION(muxc->dev),
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
		return ret;
	}

	WARN(sysfs_create_link(&priv->adap.dev.kobj, &muxc->dev->kobj,
			       "mux_device"),
	     "can't create symlink to mux device\n");

	snprintf(symlink_name, sizeof(symlink_name), "channel-%u", chan_id);
	WARN(sysfs_create_link(&muxc->dev->kobj, &priv->adap.dev.kobj,
			       symlink_name),
	     "can't create symlink for channel %u\n", chan_id);
	dev_info(&parent->dev, "Added multiplexed i2c bus %d\n",
		 i2c_adapter_id(&priv->adap));

	muxc->adapter[muxc->num_adapters++] = &priv->adap;
	return 0;
}
EXPORT_SYMBOL_GPL(i2c_mux_add_adapter);

void i2c_mux_del_adapters(struct i2c_mux_core *muxc)
{
	char symlink_name[20];

	while (muxc->num_adapters) {
		struct i2c_adapter *adap = muxc->adapter[--muxc->num_adapters];
		struct i2c_mux_priv *priv = adap->algo_data;

		muxc->adapter[muxc->num_adapters] = NULL;

		snprintf(symlink_name, sizeof(symlink_name),
			 "channel-%u", priv->chan_id);
		sysfs_remove_link(&muxc->dev->kobj, symlink_name);

		sysfs_remove_link(&priv->adap.dev.kobj, "mux_device");
		i2c_del_adapter(adap);
		kfree(priv);
	}
}
EXPORT_SYMBOL_GPL(i2c_mux_del_adapters);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("I2C driver for multiplexed I2C busses");
MODULE_LICENSE("GPL v2");
