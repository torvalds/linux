// SPDX-License-Identifier: GPL-2.0
/*
 * I2C Address Translator
 *
 * Copyright (c) 2019,2022 Luca Ceresoli <luca@lucaceresoli.net>
 * Copyright (c) 2022,2023 Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 *
 * Originally based on i2c-mux.c
 */

#include <linux/fwnode.h>
#include <linux/i2c-atr.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define ATR_MAX_ADAPTERS 100	/* Just a sanity limit */
#define ATR_MAX_SYMLINK_LEN 11	/* Longest name is 10 chars: "channel-99" */

/**
 * struct i2c_atr_alias_pair - Holds the alias assigned to a client.
 * @node:   List node
 * @client: Pointer to the client on the child bus
 * @alias:  I2C alias address assigned by the driver.
 *          This is the address that will be used to issue I2C transactions
 *          on the parent (physical) bus.
 */
struct i2c_atr_alias_pair {
	struct list_head node;
	const struct i2c_client *client;
	u16 alias;
};

/**
 * struct i2c_atr_chan - Data for a channel.
 * @adap:            The &struct i2c_adapter for the channel
 * @atr:             The parent I2C ATR
 * @chan_id:         The ID of this channel
 * @alias_list:      List of @struct i2c_atr_alias_pair containing the
 *                   assigned aliases
 * @orig_addrs_lock: Mutex protecting @orig_addrs
 * @orig_addrs:      Buffer used to store the original addresses during transmit
 * @orig_addrs_size: Size of @orig_addrs
 */
struct i2c_atr_chan {
	struct i2c_adapter adap;
	struct i2c_atr *atr;
	u32 chan_id;

	struct list_head alias_list;

	/* Lock orig_addrs during xfer */
	struct mutex orig_addrs_lock;
	u16 *orig_addrs;
	unsigned int orig_addrs_size;
};

/**
 * struct i2c_atr - The I2C ATR instance
 * @parent:    The parent &struct i2c_adapter
 * @dev:       The device that owns the I2C ATR instance
 * @ops:       &struct i2c_atr_ops
 * @priv:      Private driver data, set with i2c_atr_set_driver_data()
 * @algo:      The &struct i2c_algorithm for adapters
 * @lock:      Lock for the I2C bus segment (see &struct i2c_lock_operations)
 * @max_adapters: Maximum number of adapters this I2C ATR can have
 * @num_aliases: Number of aliases in the aliases array
 * @aliases:   The aliases array
 * @alias_mask_lock: Lock protecting alias_use_mask
 * @alias_use_mask: Bitmask for used aliases in aliases array
 * @i2c_nb:    Notifier for remote client add & del events
 * @adapter:   Array of adapters
 */
struct i2c_atr {
	struct i2c_adapter *parent;
	struct device *dev;
	const struct i2c_atr_ops *ops;

	void *priv;

	struct i2c_algorithm algo;
	/* lock for the I2C bus segment (see struct i2c_lock_operations) */
	struct mutex lock;
	int max_adapters;

	size_t num_aliases;
	const u16 *aliases;
	/* Protects alias_use_mask */
	spinlock_t alias_mask_lock;
	unsigned long *alias_use_mask;

	struct notifier_block i2c_nb;

	struct i2c_adapter *adapter[] __counted_by(max_adapters);
};

static struct i2c_atr_alias_pair *
i2c_atr_find_mapping_by_client(const struct list_head *list,
			       const struct i2c_client *client)
{
	struct i2c_atr_alias_pair *c2a;

	list_for_each_entry(c2a, list, node) {
		if (c2a->client == client)
			return c2a;
	}

	return NULL;
}

static struct i2c_atr_alias_pair *
i2c_atr_find_mapping_by_addr(const struct list_head *list, u16 phys_addr)
{
	struct i2c_atr_alias_pair *c2a;

	list_for_each_entry(c2a, list, node) {
		if (c2a->client->addr == phys_addr)
			return c2a;
	}

	return NULL;
}

/*
 * Replace all message addresses with their aliases, saving the original
 * addresses.
 *
 * This function is internal for use in i2c_atr_master_xfer(). It must be
 * followed by i2c_atr_unmap_msgs() to restore the original addresses.
 */
static int i2c_atr_map_msgs(struct i2c_atr_chan *chan, struct i2c_msg *msgs,
			    int num)
{
	struct i2c_atr *atr = chan->atr;
	static struct i2c_atr_alias_pair *c2a;
	int i;

	/* Ensure we have enough room to save the original addresses */
	if (unlikely(chan->orig_addrs_size < num)) {
		u16 *new_buf;

		/* We don't care about old data, hence no realloc() */
		new_buf = kmalloc_array(num, sizeof(*new_buf), GFP_KERNEL);
		if (!new_buf)
			return -ENOMEM;

		kfree(chan->orig_addrs);
		chan->orig_addrs = new_buf;
		chan->orig_addrs_size = num;
	}

	for (i = 0; i < num; i++) {
		chan->orig_addrs[i] = msgs[i].addr;

		c2a = i2c_atr_find_mapping_by_addr(&chan->alias_list,
						   msgs[i].addr);
		if (!c2a) {
			dev_err(atr->dev, "client 0x%02x not mapped!\n",
				msgs[i].addr);

			while (i--)
				msgs[i].addr = chan->orig_addrs[i];

			return -ENXIO;
		}

		msgs[i].addr = c2a->alias;
	}

	return 0;
}

/*
 * Restore all message address aliases with the original addresses. This
 * function is internal for use in i2c_atr_master_xfer() and for this reason it
 * needs no null and size checks on orig_addr.
 *
 * @see i2c_atr_map_msgs()
 */
static void i2c_atr_unmap_msgs(struct i2c_atr_chan *chan, struct i2c_msg *msgs,
			       int num)
{
	int i;

	for (i = 0; i < num; i++)
		msgs[i].addr = chan->orig_addrs[i];
}

static int i2c_atr_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			       int num)
{
	struct i2c_atr_chan *chan = adap->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_adapter *parent = atr->parent;
	int ret;

	/* Translate addresses */
	mutex_lock(&chan->orig_addrs_lock);

	ret = i2c_atr_map_msgs(chan, msgs, num);
	if (ret < 0)
		goto err_unlock;

	/* Perform the transfer */
	ret = i2c_transfer(parent, msgs, num);

	/* Restore addresses */
	i2c_atr_unmap_msgs(chan, msgs, num);

err_unlock:
	mutex_unlock(&chan->orig_addrs_lock);

	return ret;
}

static int i2c_atr_smbus_xfer(struct i2c_adapter *adap, u16 addr,
			      unsigned short flags, char read_write, u8 command,
			      int size, union i2c_smbus_data *data)
{
	struct i2c_atr_chan *chan = adap->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_adapter *parent = atr->parent;
	struct i2c_atr_alias_pair *c2a;

	c2a = i2c_atr_find_mapping_by_addr(&chan->alias_list, addr);
	if (!c2a) {
		dev_err(atr->dev, "client 0x%02x not mapped!\n", addr);
		return -ENXIO;
	}

	return i2c_smbus_xfer(parent, c2a->alias, flags, read_write, command,
			      size, data);
}

static u32 i2c_atr_functionality(struct i2c_adapter *adap)
{
	struct i2c_atr_chan *chan = adap->algo_data;
	struct i2c_adapter *parent = chan->atr->parent;

	return parent->algo->functionality(parent);
}

static void i2c_atr_lock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;

	mutex_lock(&atr->lock);
}

static int i2c_atr_trylock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;

	return mutex_trylock(&atr->lock);
}

static void i2c_atr_unlock_bus(struct i2c_adapter *adapter, unsigned int flags)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;

	mutex_unlock(&atr->lock);
}

static const struct i2c_lock_operations i2c_atr_lock_ops = {
	.lock_bus =    i2c_atr_lock_bus,
	.trylock_bus = i2c_atr_trylock_bus,
	.unlock_bus =  i2c_atr_unlock_bus,
};

static int i2c_atr_reserve_alias(struct i2c_atr *atr)
{
	unsigned long idx;

	spin_lock(&atr->alias_mask_lock);

	idx = find_first_zero_bit(atr->alias_use_mask, atr->num_aliases);
	if (idx >= atr->num_aliases) {
		spin_unlock(&atr->alias_mask_lock);
		dev_err(atr->dev, "failed to find a free alias\n");
		return -EBUSY;
	}

	set_bit(idx, atr->alias_use_mask);

	spin_unlock(&atr->alias_mask_lock);

	return atr->aliases[idx];
}

static void i2c_atr_release_alias(struct i2c_atr *atr, u16 alias)
{
	unsigned int idx;

	spin_lock(&atr->alias_mask_lock);

	for (idx = 0; idx < atr->num_aliases; ++idx) {
		if (atr->aliases[idx] == alias) {
			clear_bit(idx, atr->alias_use_mask);
			spin_unlock(&atr->alias_mask_lock);
			return;
		}
	}

	spin_unlock(&atr->alias_mask_lock);

	 /* This should never happen */
	dev_warn(atr->dev, "Unable to find mapped alias\n");
}

static int i2c_atr_attach_client(struct i2c_adapter *adapter,
				 const struct i2c_client *client)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;
	u16 alias;
	int ret;

	ret = i2c_atr_reserve_alias(atr);
	if (ret < 0)
		return ret;

	alias = ret;

	c2a = kzalloc(sizeof(*c2a), GFP_KERNEL);
	if (!c2a) {
		ret = -ENOMEM;
		goto err_release_alias;
	}

	ret = atr->ops->attach_client(atr, chan->chan_id, client, alias);
	if (ret)
		goto err_free;

	dev_dbg(atr->dev, "chan%u: client 0x%02x mapped at alias 0x%02x (%s)\n",
		chan->chan_id, client->addr, alias, client->name);

	c2a->client = client;
	c2a->alias = alias;
	list_add(&c2a->node, &chan->alias_list);

	return 0;

err_free:
	kfree(c2a);
err_release_alias:
	i2c_atr_release_alias(atr, alias);

	return ret;
}

static void i2c_atr_detach_client(struct i2c_adapter *adapter,
				  const struct i2c_client *client)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;

	atr->ops->detach_client(atr, chan->chan_id, client);

	c2a = i2c_atr_find_mapping_by_client(&chan->alias_list, client);
	if (!c2a) {
		 /* This should never happen */
		dev_warn(atr->dev, "Unable to find address mapping\n");
		return;
	}

	i2c_atr_release_alias(atr, c2a->alias);

	dev_dbg(atr->dev,
		"chan%u: client 0x%02x unmapped from alias 0x%02x (%s)\n",
		chan->chan_id, client->addr, c2a->alias, client->name);

	list_del(&c2a->node);
	kfree(c2a);
}

static int i2c_atr_bus_notifier_call(struct notifier_block *nb,
				     unsigned long event, void *device)
{
	struct i2c_atr *atr = container_of(nb, struct i2c_atr, i2c_nb);
	struct device *dev = device;
	struct i2c_client *client;
	u32 chan_id;
	int ret;

	client = i2c_verify_client(dev);
	if (!client)
		return NOTIFY_DONE;

	/* Is the client in one of our adapters? */
	for (chan_id = 0; chan_id < atr->max_adapters; ++chan_id) {
		if (client->adapter == atr->adapter[chan_id])
			break;
	}

	if (chan_id == atr->max_adapters)
		return NOTIFY_DONE;

	switch (event) {
	case BUS_NOTIFY_ADD_DEVICE:
		ret = i2c_atr_attach_client(client->adapter, client);
		if (ret)
			dev_err(atr->dev,
				"Failed to attach remote client '%s': %d\n",
				dev_name(dev), ret);
		break;

	case BUS_NOTIFY_DEL_DEVICE:
		i2c_atr_detach_client(client->adapter, client);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static int i2c_atr_parse_alias_pool(struct i2c_atr *atr)
{
	struct device *dev = atr->dev;
	unsigned long *alias_use_mask;
	size_t num_aliases;
	unsigned int i;
	u32 *aliases32;
	u16 *aliases16;
	int ret;

	ret = fwnode_property_count_u32(dev_fwnode(dev), "i2c-alias-pool");
	if (ret < 0) {
		dev_err(dev, "Failed to count 'i2c-alias-pool' property: %d\n",
			ret);
		return ret;
	}

	num_aliases = ret;

	if (!num_aliases)
		return 0;

	aliases32 = kcalloc(num_aliases, sizeof(*aliases32), GFP_KERNEL);
	if (!aliases32)
		return -ENOMEM;

	ret = fwnode_property_read_u32_array(dev_fwnode(dev), "i2c-alias-pool",
					     aliases32, num_aliases);
	if (ret < 0) {
		dev_err(dev, "Failed to read 'i2c-alias-pool' property: %d\n",
			ret);
		goto err_free_aliases32;
	}

	aliases16 = kcalloc(num_aliases, sizeof(*aliases16), GFP_KERNEL);
	if (!aliases16) {
		ret = -ENOMEM;
		goto err_free_aliases32;
	}

	for (i = 0; i < num_aliases; i++) {
		if (!(aliases32[i] & 0xffff0000)) {
			aliases16[i] = aliases32[i];
			continue;
		}

		dev_err(dev, "Failed to parse 'i2c-alias-pool' property: I2C flags are not supported\n");
		ret = -EINVAL;
		goto err_free_aliases16;
	}

	alias_use_mask = bitmap_zalloc(num_aliases, GFP_KERNEL);
	if (!alias_use_mask) {
		ret = -ENOMEM;
		goto err_free_aliases16;
	}

	kfree(aliases32);

	atr->num_aliases = num_aliases;
	atr->aliases = aliases16;
	atr->alias_use_mask = alias_use_mask;

	dev_dbg(dev, "i2c-alias-pool has %zu aliases", atr->num_aliases);

	return 0;

err_free_aliases16:
	kfree(aliases16);
err_free_aliases32:
	kfree(aliases32);
	return ret;
}

struct i2c_atr *i2c_atr_new(struct i2c_adapter *parent, struct device *dev,
			    const struct i2c_atr_ops *ops, int max_adapters)
{
	struct i2c_atr *atr;
	int ret;

	if (max_adapters > ATR_MAX_ADAPTERS)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->attach_client || !ops->detach_client)
		return ERR_PTR(-EINVAL);

	atr = kzalloc(struct_size(atr, adapter, max_adapters), GFP_KERNEL);
	if (!atr)
		return ERR_PTR(-ENOMEM);

	mutex_init(&atr->lock);
	spin_lock_init(&atr->alias_mask_lock);

	atr->parent = parent;
	atr->dev = dev;
	atr->ops = ops;
	atr->max_adapters = max_adapters;

	if (parent->algo->master_xfer)
		atr->algo.master_xfer = i2c_atr_master_xfer;
	if (parent->algo->smbus_xfer)
		atr->algo.smbus_xfer = i2c_atr_smbus_xfer;
	atr->algo.functionality = i2c_atr_functionality;

	ret = i2c_atr_parse_alias_pool(atr);
	if (ret)
		goto err_destroy_mutex;

	atr->i2c_nb.notifier_call = i2c_atr_bus_notifier_call;
	ret = bus_register_notifier(&i2c_bus_type, &atr->i2c_nb);
	if (ret)
		goto err_free_aliases;

	return atr;

err_free_aliases:
	bitmap_free(atr->alias_use_mask);
	kfree(atr->aliases);
err_destroy_mutex:
	mutex_destroy(&atr->lock);
	kfree(atr);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_new, I2C_ATR);

void i2c_atr_delete(struct i2c_atr *atr)
{
	unsigned int i;

	for (i = 0; i < atr->max_adapters; ++i)
		WARN_ON(atr->adapter[i]);

	bus_unregister_notifier(&i2c_bus_type, &atr->i2c_nb);
	bitmap_free(atr->alias_use_mask);
	kfree(atr->aliases);
	mutex_destroy(&atr->lock);
	kfree(atr);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_delete, I2C_ATR);

int i2c_atr_add_adapter(struct i2c_atr *atr, u32 chan_id,
			struct device *adapter_parent,
			struct fwnode_handle *bus_handle)
{
	struct i2c_adapter *parent = atr->parent;
	struct device *dev = atr->dev;
	struct i2c_atr_chan *chan;
	char symlink_name[ATR_MAX_SYMLINK_LEN];
	int ret;

	if (chan_id >= atr->max_adapters) {
		dev_err(dev, "No room for more i2c-atr adapters\n");
		return -EINVAL;
	}

	if (atr->adapter[chan_id]) {
		dev_err(dev, "Adapter %d already present\n", chan_id);
		return -EEXIST;
	}

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	if (!adapter_parent)
		adapter_parent = dev;

	chan->atr = atr;
	chan->chan_id = chan_id;
	INIT_LIST_HEAD(&chan->alias_list);
	mutex_init(&chan->orig_addrs_lock);

	snprintf(chan->adap.name, sizeof(chan->adap.name), "i2c-%d-atr-%d",
		 i2c_adapter_id(parent), chan_id);
	chan->adap.owner = THIS_MODULE;
	chan->adap.algo = &atr->algo;
	chan->adap.algo_data = chan;
	chan->adap.dev.parent = adapter_parent;
	chan->adap.retries = parent->retries;
	chan->adap.timeout = parent->timeout;
	chan->adap.quirks = parent->quirks;
	chan->adap.lock_ops = &i2c_atr_lock_ops;

	if (bus_handle) {
		device_set_node(&chan->adap.dev, fwnode_handle_get(bus_handle));
	} else {
		struct fwnode_handle *atr_node;
		struct fwnode_handle *child;
		u32 reg;

		atr_node = device_get_named_child_node(dev, "i2c-atr");

		fwnode_for_each_child_node(atr_node, child) {
			ret = fwnode_property_read_u32(child, "reg", &reg);
			if (ret)
				continue;
			if (chan_id == reg)
				break;
		}

		device_set_node(&chan->adap.dev, child);
		fwnode_handle_put(atr_node);
	}

	atr->adapter[chan_id] = &chan->adap;

	ret = i2c_add_adapter(&chan->adap);
	if (ret) {
		dev_err(dev, "failed to add atr-adapter %u (error=%d)\n",
			chan_id, ret);
		goto err_fwnode_put;
	}

	snprintf(symlink_name, sizeof(symlink_name), "channel-%u",
		 chan->chan_id);

	ret = sysfs_create_link(&chan->adap.dev.kobj, &dev->kobj, "atr_device");
	if (ret)
		dev_warn(dev, "can't create symlink to atr device\n");
	ret = sysfs_create_link(&dev->kobj, &chan->adap.dev.kobj, symlink_name);
	if (ret)
		dev_warn(dev, "can't create symlink for channel %u\n", chan_id);

	dev_dbg(dev, "Added ATR child bus %d\n", i2c_adapter_id(&chan->adap));

	return 0;

err_fwnode_put:
	fwnode_handle_put(dev_fwnode(&chan->adap.dev));
	mutex_destroy(&chan->orig_addrs_lock);
	kfree(chan);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_add_adapter, I2C_ATR);

void i2c_atr_del_adapter(struct i2c_atr *atr, u32 chan_id)
{
	char symlink_name[ATR_MAX_SYMLINK_LEN];
	struct i2c_adapter *adap;
	struct i2c_atr_chan *chan;
	struct fwnode_handle *fwnode;
	struct device *dev = atr->dev;

	adap = atr->adapter[chan_id];
	if (!adap)
		return;

	chan = adap->algo_data;
	fwnode = dev_fwnode(&adap->dev);

	dev_dbg(dev, "Removing ATR child bus %d\n", i2c_adapter_id(adap));

	snprintf(symlink_name, sizeof(symlink_name), "channel-%u",
		 chan->chan_id);
	sysfs_remove_link(&dev->kobj, symlink_name);
	sysfs_remove_link(&chan->adap.dev.kobj, "atr_device");

	i2c_del_adapter(adap);

	atr->adapter[chan_id] = NULL;

	fwnode_handle_put(fwnode);
	mutex_destroy(&chan->orig_addrs_lock);
	kfree(chan->orig_addrs);
	kfree(chan);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_del_adapter, I2C_ATR);

void i2c_atr_set_driver_data(struct i2c_atr *atr, void *data)
{
	atr->priv = data;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_set_driver_data, I2C_ATR);

void *i2c_atr_get_driver_data(struct i2c_atr *atr)
{
	return atr->priv;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_get_driver_data, I2C_ATR);

MODULE_AUTHOR("Luca Ceresoli <luca.ceresoli@bootlin.com>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>");
MODULE_DESCRIPTION("I2C Address Translator");
MODULE_LICENSE("GPL");
