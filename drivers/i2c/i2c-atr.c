// SPDX-License-Identifier: GPL-2.0
/*
 * I2C Address Translator
 *
 * Copyright (c) 2019,2022 Luca Ceresoli <luca@lucaceresoli.net>
 * Copyright (c) 2022,2023 Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 *
 * Originally based on i2c-mux.c
 */

#include <linux/i2c-atr.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/lockdep.h>

#define ATR_MAX_ADAPTERS 100	/* Just a sanity limit */
#define ATR_MAX_SYMLINK_LEN 11	/* Longest name is 10 chars: "channel-99" */

/**
 * struct i2c_atr_alias_pair - Holds the alias assigned to a client address.
 * @node:   List node
 * @addr:   Address of the client on the child bus.
 * @alias:  I2C alias address assigned by the driver.
 *          This is the address that will be used to issue I2C transactions
 *          on the parent (physical) bus.
 * @fixed:  Alias pair cannot be replaced during dynamic address attachment.
 *          This flag is necessary for situations where a single I2C transaction
 *          contains more distinct target addresses than the ATR channel can handle.
 *          It marks addresses that have already been attached to an alias so
 *          that their alias pair is not evicted by a subsequent address in the same
 *          transaction.
 *
 */
struct i2c_atr_alias_pair {
	struct list_head node;
	bool fixed;
	u16 addr;
	u16 alias;
};

/**
 * struct i2c_atr_alias_pool - Pool of client aliases available for an ATR.
 * @size:     Total number of aliases
 * @shared:   Indicates if this alias pool is shared by multiple channels
 *
 * @lock:     Lock protecting @aliases and @use_mask
 * @aliases:  Array of aliases, must hold exactly @size elements
 * @use_mask: Mask of used aliases
 */
struct i2c_atr_alias_pool {
	size_t size;
	bool shared;

	/* Protects aliases and use_mask */
	spinlock_t lock;
	u16 *aliases;
	unsigned long *use_mask;
};

/**
 * struct i2c_atr_chan - Data for a channel.
 * @adap:            The &struct i2c_adapter for the channel
 * @atr:             The parent I2C ATR
 * @chan_id:         The ID of this channel
 * @alias_pairs_lock: Mutex protecting @alias_pairs
 * @alias_pairs_lock_key: Lock key for @alias_pairs_lock
 * @alias_pairs:     List of @struct i2c_atr_alias_pair containing the
 *                   assigned aliases
 * @alias_pool:      Pool of available client aliases
 *
 * @orig_addrs_lock: Mutex protecting @orig_addrs
 * @orig_addrs_lock_key: Lock key for @orig_addrs_lock
 * @orig_addrs:      Buffer used to store the original addresses during transmit
 * @orig_addrs_size: Size of @orig_addrs
 */
struct i2c_atr_chan {
	struct i2c_adapter adap;
	struct i2c_atr *atr;
	u32 chan_id;

	/* Lock alias_pairs during attach/detach */
	struct mutex alias_pairs_lock;
	struct lock_class_key alias_pairs_lock_key;
	struct list_head alias_pairs;
	struct i2c_atr_alias_pool *alias_pool;

	/* Lock orig_addrs during xfer */
	struct mutex orig_addrs_lock;
	struct lock_class_key orig_addrs_lock_key;
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
 * @lock_key:  Lock key for @lock
 * @max_adapters: Maximum number of adapters this I2C ATR can have
 * @flags:     Flags for ATR
 * @alias_pool: Optional common pool of available client aliases
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
	struct lock_class_key lock_key;
	int max_adapters;
	u32 flags;

	struct i2c_atr_alias_pool *alias_pool;

	struct notifier_block i2c_nb;

	struct i2c_adapter *adapter[] __counted_by(max_adapters);
};

static struct i2c_atr_alias_pool *i2c_atr_alloc_alias_pool(size_t num_aliases, bool shared)
{
	struct i2c_atr_alias_pool *alias_pool;
	int ret;

	alias_pool = kzalloc(sizeof(*alias_pool), GFP_KERNEL);
	if (!alias_pool)
		return ERR_PTR(-ENOMEM);

	alias_pool->size = num_aliases;

	alias_pool->aliases = kcalloc(num_aliases, sizeof(*alias_pool->aliases), GFP_KERNEL);
	if (!alias_pool->aliases) {
		ret = -ENOMEM;
		goto err_free_alias_pool;
	}

	alias_pool->use_mask = bitmap_zalloc(num_aliases, GFP_KERNEL);
	if (!alias_pool->use_mask) {
		ret = -ENOMEM;
		goto err_free_aliases;
	}

	alias_pool->shared = shared;

	spin_lock_init(&alias_pool->lock);

	return alias_pool;

err_free_aliases:
	kfree(alias_pool->aliases);
err_free_alias_pool:
	kfree(alias_pool);
	return ERR_PTR(ret);
}

static void i2c_atr_free_alias_pool(struct i2c_atr_alias_pool *alias_pool)
{
	bitmap_free(alias_pool->use_mask);
	kfree(alias_pool->aliases);
	kfree(alias_pool);
}

/* Must be called with alias_pairs_lock held */
static struct i2c_atr_alias_pair *i2c_atr_create_c2a(struct i2c_atr_chan *chan,
						     u16 alias, u16 addr)
{
	struct i2c_atr_alias_pair *c2a;

	lockdep_assert_held(&chan->alias_pairs_lock);

	c2a = kzalloc(sizeof(*c2a), GFP_KERNEL);
	if (!c2a)
		return NULL;

	c2a->addr = addr;
	c2a->alias = alias;

	list_add(&c2a->node, &chan->alias_pairs);

	return c2a;
}

/* Must be called with alias_pairs_lock held */
static void i2c_atr_destroy_c2a(struct i2c_atr_alias_pair **pc2a)
{
	list_del(&(*pc2a)->node);
	kfree(*pc2a);
	*pc2a = NULL;
}

static int i2c_atr_reserve_alias(struct i2c_atr_alias_pool *alias_pool)
{
	unsigned long idx;
	u16 alias;

	spin_lock(&alias_pool->lock);

	idx = find_first_zero_bit(alias_pool->use_mask, alias_pool->size);
	if (idx >= alias_pool->size) {
		spin_unlock(&alias_pool->lock);
		return -EBUSY;
	}

	set_bit(idx, alias_pool->use_mask);

	alias = alias_pool->aliases[idx];

	spin_unlock(&alias_pool->lock);
	return alias;
}

static void i2c_atr_release_alias(struct i2c_atr_alias_pool *alias_pool, u16 alias)
{
	unsigned int idx;

	spin_lock(&alias_pool->lock);

	for (idx = 0; idx < alias_pool->size; ++idx) {
		if (alias_pool->aliases[idx] == alias) {
			clear_bit(idx, alias_pool->use_mask);
			spin_unlock(&alias_pool->lock);
			return;
		}
	}

	spin_unlock(&alias_pool->lock);
}

static struct i2c_atr_alias_pair *
i2c_atr_find_mapping_by_addr(struct i2c_atr_chan *chan, u16 addr)
{
	struct i2c_atr_alias_pair *c2a;

	lockdep_assert_held(&chan->alias_pairs_lock);

	list_for_each_entry(c2a, &chan->alias_pairs, node) {
		if (c2a->addr == addr)
			return c2a;
	}

	return NULL;
}

static struct i2c_atr_alias_pair *
i2c_atr_create_mapping_by_addr(struct i2c_atr_chan *chan, u16 addr)
{
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;
	u16 alias;
	int ret;

	lockdep_assert_held(&chan->alias_pairs_lock);

	ret = i2c_atr_reserve_alias(chan->alias_pool);
	if (ret < 0)
		return NULL;

	alias = ret;

	c2a = i2c_atr_create_c2a(chan, alias, addr);
	if (!c2a)
		goto err_release_alias;

	ret = atr->ops->attach_addr(atr, chan->chan_id, c2a->addr, c2a->alias);
	if (ret) {
		dev_err(atr->dev, "failed to attach 0x%02x on channel %d: err %d\n",
			addr, chan->chan_id, ret);
		goto err_del_c2a;
	}

	return c2a;

err_del_c2a:
	i2c_atr_destroy_c2a(&c2a);
err_release_alias:
	i2c_atr_release_alias(chan->alias_pool, alias);
	return NULL;
}

static struct i2c_atr_alias_pair *
i2c_atr_replace_mapping_by_addr(struct i2c_atr_chan *chan, u16 addr)
{
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;
	struct list_head *alias_pairs;
	bool found = false;
	u16 alias;
	int ret;

	lockdep_assert_held(&chan->alias_pairs_lock);

	alias_pairs = &chan->alias_pairs;

	if (unlikely(list_empty(alias_pairs)))
		return NULL;

	list_for_each_entry_reverse(c2a, alias_pairs, node) {
		if (!c2a->fixed) {
			found = true;
			break;
		}
	}

	if (!found)
		return NULL;

	atr->ops->detach_addr(atr, chan->chan_id, c2a->addr);
	c2a->addr = addr;

	list_move(&c2a->node, alias_pairs);

	alias = c2a->alias;

	ret = atr->ops->attach_addr(atr, chan->chan_id, c2a->addr, c2a->alias);
	if (ret) {
		dev_err(atr->dev, "failed to attach 0x%02x on channel %d: err %d\n",
			addr, chan->chan_id, ret);
		i2c_atr_destroy_c2a(&c2a);
		i2c_atr_release_alias(chan->alias_pool, alias);
		return NULL;
	}

	return c2a;
}

static struct i2c_atr_alias_pair *
i2c_atr_get_mapping_by_addr(struct i2c_atr_chan *chan, u16 addr)
{
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;

	c2a = i2c_atr_find_mapping_by_addr(chan, addr);
	if (c2a)
		return c2a;

	if (atr->flags & I2C_ATR_F_STATIC)
		return NULL;

	c2a = i2c_atr_create_mapping_by_addr(chan, addr);
	if (c2a)
		return c2a;

	return i2c_atr_replace_mapping_by_addr(chan, addr);
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
	int i, ret = 0;

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

	mutex_lock(&chan->alias_pairs_lock);

	for (i = 0; i < num; i++) {
		chan->orig_addrs[i] = msgs[i].addr;

		c2a = i2c_atr_get_mapping_by_addr(chan, msgs[i].addr);

		if (!c2a) {
			if (atr->flags & I2C_ATR_F_PASSTHROUGH)
				continue;

			dev_err(atr->dev, "client 0x%02x not mapped!\n",
				msgs[i].addr);

			while (i--)
				msgs[i].addr = chan->orig_addrs[i];

			ret = -ENXIO;
			goto out_unlock;
		}

		// Prevent c2a from being overwritten by another client in this transaction
		c2a->fixed = true;

		msgs[i].addr = c2a->alias;
	}

out_unlock:
	mutex_unlock(&chan->alias_pairs_lock);
	return ret;
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
	struct i2c_atr_alias_pair *c2a;
	int i;

	for (i = 0; i < num; i++)
		msgs[i].addr = chan->orig_addrs[i];

	mutex_lock(&chan->alias_pairs_lock);

	if (unlikely(list_empty(&chan->alias_pairs)))
		goto out_unlock;

	// unfix c2a entries so that subsequent transfers can reuse their aliases
	list_for_each_entry(c2a, &chan->alias_pairs, node) {
		c2a->fixed = false;
	}

out_unlock:
	mutex_unlock(&chan->alias_pairs_lock);
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
	u16 alias;

	mutex_lock(&chan->alias_pairs_lock);

	c2a = i2c_atr_get_mapping_by_addr(chan, addr);

	if (!c2a && !(atr->flags & I2C_ATR_F_PASSTHROUGH)) {
		dev_err(atr->dev, "client 0x%02x not mapped!\n", addr);
		mutex_unlock(&chan->alias_pairs_lock);
		return -ENXIO;
	}

	alias = c2a ? c2a->alias : addr;

	mutex_unlock(&chan->alias_pairs_lock);

	return i2c_smbus_xfer(parent, alias, flags, read_write, command,
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

static int i2c_atr_attach_addr(struct i2c_adapter *adapter,
			       u16 addr)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;
	int ret = 0;

	mutex_lock(&chan->alias_pairs_lock);

	c2a = i2c_atr_create_mapping_by_addr(chan, addr);
	if (!c2a && !(atr->flags & I2C_ATR_F_STATIC))
		c2a = i2c_atr_replace_mapping_by_addr(chan, addr);

	if (!c2a) {
		dev_err(atr->dev, "failed to find a free alias\n");
		ret = -EBUSY;
		goto out_unlock;
	}

	dev_dbg(atr->dev, "chan%u: using alias 0x%02x for addr 0x%02x\n",
		chan->chan_id, c2a->alias, addr);

out_unlock:
	mutex_unlock(&chan->alias_pairs_lock);
	return ret;
}

static void i2c_atr_detach_addr(struct i2c_adapter *adapter,
				u16 addr)
{
	struct i2c_atr_chan *chan = adapter->algo_data;
	struct i2c_atr *atr = chan->atr;
	struct i2c_atr_alias_pair *c2a;

	atr->ops->detach_addr(atr, chan->chan_id, addr);

	mutex_lock(&chan->alias_pairs_lock);

	c2a = i2c_atr_find_mapping_by_addr(chan, addr);
	if (!c2a) {
		mutex_unlock(&chan->alias_pairs_lock);
		return;
	}

	i2c_atr_release_alias(chan->alias_pool, c2a->alias);

	dev_dbg(atr->dev,
		"chan%u: detached alias 0x%02x from addr 0x%02x\n",
		chan->chan_id, c2a->alias, addr);

	i2c_atr_destroy_c2a(&c2a);

	mutex_unlock(&chan->alias_pairs_lock);
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
		ret = i2c_atr_attach_addr(client->adapter, client->addr);
		if (ret)
			dev_err(atr->dev,
				"Failed to attach remote client '%s': %d\n",
				dev_name(dev), ret);
		break;

	case BUS_NOTIFY_REMOVED_DEVICE:
		i2c_atr_detach_addr(client->adapter, client->addr);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static int i2c_atr_parse_alias_pool(struct i2c_atr *atr)
{
	struct i2c_atr_alias_pool *alias_pool;
	struct device *dev = atr->dev;
	size_t num_aliases;
	unsigned int i;
	u32 *aliases32;
	int ret;

	if (!fwnode_property_present(dev_fwnode(dev), "i2c-alias-pool")) {
		num_aliases = 0;
	} else {
		ret = fwnode_property_count_u32(dev_fwnode(dev), "i2c-alias-pool");
		if (ret < 0) {
			dev_err(dev, "Failed to count 'i2c-alias-pool' property: %d\n",
				ret);
			return ret;
		}

		num_aliases = ret;
	}

	alias_pool = i2c_atr_alloc_alias_pool(num_aliases, true);
	if (IS_ERR(alias_pool)) {
		ret = PTR_ERR(alias_pool);
		dev_err(dev, "Failed to allocate alias pool, err %d\n", ret);
		return ret;
	}

	atr->alias_pool = alias_pool;

	if (!alias_pool->size)
		return 0;

	aliases32 = kcalloc(num_aliases, sizeof(*aliases32), GFP_KERNEL);
	if (!aliases32) {
		ret = -ENOMEM;
		goto err_free_alias_pool;
	}

	ret = fwnode_property_read_u32_array(dev_fwnode(dev), "i2c-alias-pool",
					     aliases32, num_aliases);
	if (ret < 0) {
		dev_err(dev, "Failed to read 'i2c-alias-pool' property: %d\n",
			ret);
		goto err_free_aliases32;
	}

	for (i = 0; i < num_aliases; i++) {
		if (!(aliases32[i] & 0xffff0000)) {
			alias_pool->aliases[i] = aliases32[i];
			continue;
		}

		dev_err(dev, "Failed to parse 'i2c-alias-pool' property: I2C flags are not supported\n");
		ret = -EINVAL;
		goto err_free_aliases32;
	}

	kfree(aliases32);

	dev_dbg(dev, "i2c-alias-pool has %zu aliases\n", alias_pool->size);

	return 0;

err_free_aliases32:
	kfree(aliases32);
err_free_alias_pool:
	i2c_atr_free_alias_pool(alias_pool);
	return ret;
}

struct i2c_atr *i2c_atr_new(struct i2c_adapter *parent, struct device *dev,
			    const struct i2c_atr_ops *ops, int max_adapters,
			    u32 flags)
{
	struct i2c_atr *atr;
	int ret;

	if (max_adapters > ATR_MAX_ADAPTERS)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->attach_addr || !ops->detach_addr)
		return ERR_PTR(-EINVAL);

	atr = kzalloc(struct_size(atr, adapter, max_adapters), GFP_KERNEL);
	if (!atr)
		return ERR_PTR(-ENOMEM);

	lockdep_register_key(&atr->lock_key);
	mutex_init_with_key(&atr->lock, &atr->lock_key);

	atr->parent = parent;
	atr->dev = dev;
	atr->ops = ops;
	atr->max_adapters = max_adapters;
	atr->flags = flags;

	if (parent->algo->master_xfer)
		atr->algo.xfer = i2c_atr_master_xfer;
	if (parent->algo->smbus_xfer)
		atr->algo.smbus_xfer = i2c_atr_smbus_xfer;
	atr->algo.functionality = i2c_atr_functionality;

	ret = i2c_atr_parse_alias_pool(atr);
	if (ret)
		goto err_destroy_mutex;

	atr->i2c_nb.notifier_call = i2c_atr_bus_notifier_call;
	ret = bus_register_notifier(&i2c_bus_type, &atr->i2c_nb);
	if (ret)
		goto err_free_alias_pool;

	return atr;

err_free_alias_pool:
	i2c_atr_free_alias_pool(atr->alias_pool);
err_destroy_mutex:
	mutex_destroy(&atr->lock);
	lockdep_unregister_key(&atr->lock_key);
	kfree(atr);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_new, "I2C_ATR");

void i2c_atr_delete(struct i2c_atr *atr)
{
	unsigned int i;

	for (i = 0; i < atr->max_adapters; ++i)
		WARN_ON(atr->adapter[i]);

	bus_unregister_notifier(&i2c_bus_type, &atr->i2c_nb);
	i2c_atr_free_alias_pool(atr->alias_pool);
	mutex_destroy(&atr->lock);
	lockdep_unregister_key(&atr->lock_key);
	kfree(atr);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_delete, "I2C_ATR");

int i2c_atr_add_adapter(struct i2c_atr *atr, struct i2c_atr_adap_desc *desc)
{
	struct fwnode_handle *bus_handle = desc->bus_handle;
	struct i2c_adapter *parent = atr->parent;
	char symlink_name[ATR_MAX_SYMLINK_LEN];
	struct device *dev = atr->dev;
	u32 chan_id = desc->chan_id;
	struct i2c_atr_chan *chan;
	int ret, idx;

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

	if (!desc->parent)
		desc->parent = dev;

	chan->atr = atr;
	chan->chan_id = chan_id;
	INIT_LIST_HEAD(&chan->alias_pairs);
	lockdep_register_key(&chan->alias_pairs_lock_key);
	lockdep_register_key(&chan->orig_addrs_lock_key);
	mutex_init_with_key(&chan->alias_pairs_lock, &chan->alias_pairs_lock_key);
	mutex_init_with_key(&chan->orig_addrs_lock, &chan->orig_addrs_lock_key);

	snprintf(chan->adap.name, sizeof(chan->adap.name), "i2c-%d-atr-%d",
		 i2c_adapter_id(parent), chan_id);
	chan->adap.owner = THIS_MODULE;
	chan->adap.algo = &atr->algo;
	chan->adap.algo_data = chan;
	chan->adap.dev.parent = desc->parent;
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

	if (desc->num_aliases > 0) {
		chan->alias_pool = i2c_atr_alloc_alias_pool(desc->num_aliases, false);
		if (IS_ERR(chan->alias_pool)) {
			ret = PTR_ERR(chan->alias_pool);
			goto err_fwnode_put;
		}

		for (idx = 0; idx < desc->num_aliases; idx++)
			chan->alias_pool->aliases[idx] = desc->aliases[idx];
	} else {
		chan->alias_pool = atr->alias_pool;
	}

	atr->adapter[chan_id] = &chan->adap;

	ret = i2c_add_adapter(&chan->adap);
	if (ret) {
		dev_err(dev, "failed to add atr-adapter %u (error=%d)\n",
			chan_id, ret);
		goto err_free_alias_pool;
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

err_free_alias_pool:
	if (!chan->alias_pool->shared)
		i2c_atr_free_alias_pool(chan->alias_pool);
err_fwnode_put:
	fwnode_handle_put(dev_fwnode(&chan->adap.dev));
	mutex_destroy(&chan->orig_addrs_lock);
	mutex_destroy(&chan->alias_pairs_lock);
	lockdep_unregister_key(&chan->orig_addrs_lock_key);
	lockdep_unregister_key(&chan->alias_pairs_lock_key);
	kfree(chan);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_add_adapter, "I2C_ATR");

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

	if (!chan->alias_pool->shared)
		i2c_atr_free_alias_pool(chan->alias_pool);

	atr->adapter[chan_id] = NULL;

	fwnode_handle_put(fwnode);
	mutex_destroy(&chan->orig_addrs_lock);
	mutex_destroy(&chan->alias_pairs_lock);
	lockdep_unregister_key(&chan->orig_addrs_lock_key);
	lockdep_unregister_key(&chan->alias_pairs_lock_key);
	kfree(chan->orig_addrs);
	kfree(chan);
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_del_adapter, "I2C_ATR");

void i2c_atr_set_driver_data(struct i2c_atr *atr, void *data)
{
	atr->priv = data;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_set_driver_data, "I2C_ATR");

void *i2c_atr_get_driver_data(struct i2c_atr *atr)
{
	return atr->priv;
}
EXPORT_SYMBOL_NS_GPL(i2c_atr_get_driver_data, "I2C_ATR");

MODULE_AUTHOR("Luca Ceresoli <luca.ceresoli@bootlin.com>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>");
MODULE_DESCRIPTION("I2C Address Translator");
MODULE_LICENSE("GPL");
