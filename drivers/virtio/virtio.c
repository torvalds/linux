// SPDX-License-Identifier: GPL-2.0-only
#include <linux/virtio.h>
#include <linux/spinlock.h>
#include <linux/virtio_config.h>
#include <linux/virtio_anchor.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <uapi/linux/virtio_ids.h>

/* Unique numbering for virtio devices. */
static DEFINE_IDA(virtio_index_ida);

static ssize_t device_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sysfs_emit(buf, "0x%04x\n", dev->id.device);
}
static DEVICE_ATTR_RO(device);

static ssize_t vendor_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sysfs_emit(buf, "0x%04x\n", dev->id.vendor);
}
static DEVICE_ATTR_RO(vendor);

static ssize_t status_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sysfs_emit(buf, "0x%08x\n", dev->config->get_status(dev));
}
static DEVICE_ATTR_RO(status);

static ssize_t modalias_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	return sysfs_emit(buf, "virtio:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t features_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	unsigned int i;
	ssize_t len = 0;

	/* We actually represent this as a bitstring, as it could be
	 * arbitrary length in future. */
	for (i = 0; i < VIRTIO_FEATURES_MAX; i++)
		len += sysfs_emit_at(buf, len, "%c",
			       __virtio_test_bit(dev, i) ? '1' : '0');
	len += sysfs_emit_at(buf, len, "\n");
	return len;
}
static DEVICE_ATTR_RO(features);

static struct attribute *virtio_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_status.attr,
	&dev_attr_modalias.attr,
	&dev_attr_features.attr,
	NULL,
};
ATTRIBUTE_GROUPS(virtio_dev);

static inline int virtio_id_match(const struct virtio_device *dev,
				  const struct virtio_device_id *id)
{
	if (id->device != dev->id.device && id->device != VIRTIO_DEV_ANY_ID)
		return 0;

	return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/* This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call virtio_dev_probe(). */
static int virtio_dev_match(struct device *_dv, const struct device_driver *_dr)
{
	unsigned int i;
	struct virtio_device *dev = dev_to_virtio(_dv);
	const struct virtio_device_id *ids;

	ids = drv_to_virtio(_dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (virtio_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int virtio_uevent(const struct device *_dv, struct kobj_uevent_env *env)
{
	const struct virtio_device *dev = dev_to_virtio(_dv);

	return add_uevent_var(env, "MODALIAS=virtio:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit)
{
	unsigned int i;
	struct virtio_driver *drv = drv_to_virtio(vdev->dev.driver);

	for (i = 0; i < drv->feature_table_size; i++)
		if (drv->feature_table[i] == fbit)
			return;

	if (drv->feature_table_legacy) {
		for (i = 0; i < drv->feature_table_size_legacy; i++)
			if (drv->feature_table_legacy[i] == fbit)
				return;
	}

	BUG();
}
EXPORT_SYMBOL_GPL(virtio_check_driver_offered_feature);

static void __virtio_config_changed(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	if (!dev->config_core_enabled || dev->config_driver_disabled)
		dev->config_change_pending = true;
	else if (drv && drv->config_changed) {
		drv->config_changed(dev);
		dev->config_change_pending = false;
	}
}

void virtio_config_changed(struct virtio_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->config_lock, flags);
	__virtio_config_changed(dev);
	spin_unlock_irqrestore(&dev->config_lock, flags);
}
EXPORT_SYMBOL_GPL(virtio_config_changed);

/**
 * virtio_config_driver_disable - disable config change reporting by drivers
 * @dev: the device to disable
 *
 * This is only allowed to be called by a driver and disabling can't
 * be nested.
 */
void virtio_config_driver_disable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_driver_disabled = true;
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_driver_disable);

/**
 * virtio_config_driver_enable - enable config change reporting by drivers
 * @dev: the device to enable
 *
 * This is only allowed to be called by a driver and enabling can't
 * be nested.
 */
void virtio_config_driver_enable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_driver_disabled = false;
	if (dev->config_change_pending)
		__virtio_config_changed(dev);
	spin_unlock_irq(&dev->config_lock);
}
EXPORT_SYMBOL_GPL(virtio_config_driver_enable);

static void virtio_config_core_disable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_core_enabled = false;
	spin_unlock_irq(&dev->config_lock);
}

static void virtio_config_core_enable(struct virtio_device *dev)
{
	spin_lock_irq(&dev->config_lock);
	dev->config_core_enabled = true;
	if (dev->config_change_pending)
		__virtio_config_changed(dev);
	spin_unlock_irq(&dev->config_lock);
}

void virtio_add_status(struct virtio_device *dev, unsigned int status)
{
	might_sleep();
	dev->config->set_status(dev, dev->config->get_status(dev) | status);
}
EXPORT_SYMBOL_GPL(virtio_add_status);

/* Do some validation, then set FEATURES_OK */
static int virtio_features_ok(struct virtio_device *dev)
{
	unsigned int status;

	might_sleep();

	if (virtio_check_mem_acc_cb(dev)) {
		if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1)) {
			dev_warn(&dev->dev,
				 "device must provide VIRTIO_F_VERSION_1\n");
			return -ENODEV;
		}

		if (!virtio_has_feature(dev, VIRTIO_F_ACCESS_PLATFORM)) {
			dev_warn(&dev->dev,
				 "device must provide VIRTIO_F_ACCESS_PLATFORM\n");
			return -ENODEV;
		}
	}

	if (!virtio_has_feature(dev, VIRTIO_F_VERSION_1))
		return 0;

	virtio_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
	status = dev->config->get_status(dev);
	if (!(status & VIRTIO_CONFIG_S_FEATURES_OK)) {
		dev_err(&dev->dev, "virtio: device refuses features: %x\n",
			status);
		return -ENODEV;
	}
	return 0;
}

/**
 * virtio_reset_device - quiesce device for removal
 * @dev: the device to reset
 *
 * Prevents device from sending interrupts and accessing memory.
 *
 * Generally used for cleanup during driver / device removal.
 *
 * Once this has been invoked, caller must ensure that
 * virtqueue_notify / virtqueue_kick are not in progress.
 *
 * Note: this guarantees that vq callbacks are not in progress, however caller
 * is responsible for preventing access from other contexts, such as a system
 * call/workqueue/bh.  Invoking virtio_break_device then flushing any such
 * contexts is one way to handle that.
 * */
void virtio_reset_device(struct virtio_device *dev)
{
#ifdef CONFIG_VIRTIO_HARDEN_NOTIFICATION
	/*
	 * The below virtio_synchronize_cbs() guarantees that any
	 * interrupt for this line arriving after
	 * virtio_synchronize_vqs() has completed is guaranteed to see
	 * vq->broken as true.
	 */
	virtio_break_device(dev);
	virtio_synchronize_cbs(dev);
#endif

	dev->config->reset(dev);
}
EXPORT_SYMBOL_GPL(virtio_reset_device);

static int virtio_dev_probe(struct device *_d)
{
	int err, i;
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	u64 device_features[VIRTIO_FEATURES_DWORDS];
	u64 driver_features[VIRTIO_FEATURES_DWORDS];
	u64 driver_features_legacy;

	/* We have a driver! */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	/* Figure out what features the device supports. */
	virtio_get_features(dev, device_features);

	/* Figure out what features the driver supports. */
	virtio_features_zero(driver_features);
	for (i = 0; i < drv->feature_table_size; i++) {
		unsigned int f = drv->feature_table[i];
		if (!WARN_ON_ONCE(f >= VIRTIO_FEATURES_MAX))
			virtio_features_set_bit(driver_features, f);
	}

	/* Some drivers have a separate feature table for virtio v1.0 */
	if (drv->feature_table_legacy) {
		driver_features_legacy = 0;
		for (i = 0; i < drv->feature_table_size_legacy; i++) {
			unsigned int f = drv->feature_table_legacy[i];
			if (!WARN_ON_ONCE(f >= 64))
				driver_features_legacy |= (1ULL << f);
		}
	} else {
		driver_features_legacy = driver_features[0];
	}

	if (virtio_features_test_bit(device_features, VIRTIO_F_VERSION_1)) {
		for (i = 0; i < VIRTIO_FEATURES_DWORDS; ++i)
			dev->features_array[i] = driver_features[i] &
						 device_features[i];
	} else {
		virtio_features_from_u64(dev->features_array,
					 driver_features_legacy &
					 device_features[0]);
	}

	/* When debugging, user may filter some features by hand. */
	virtio_debug_device_filter_features(dev);

	/* Transport features always preserved to pass to finalize_features. */
	for (i = VIRTIO_TRANSPORT_F_START; i < VIRTIO_TRANSPORT_F_END; i++)
		if (virtio_features_test_bit(device_features, i))
			__virtio_set_bit(dev, i);

	err = dev->config->finalize_features(dev);
	if (err)
		goto err;

	if (drv->validate) {
		u64 features[VIRTIO_FEATURES_DWORDS];

		virtio_features_copy(features, dev->features_array);
		err = drv->validate(dev);
		if (err)
			goto err;

		/* Did validation change any features? Then write them again. */
		if (!virtio_features_equal(features, dev->features_array)) {
			err = dev->config->finalize_features(dev);
			if (err)
				goto err;
		}
	}

	err = virtio_features_ok(dev);
	if (err)
		goto err;

	err = drv->probe(dev);
	if (err)
		goto err;

	/* If probe didn't do it, mark device DRIVER_OK ourselves. */
	if (!(dev->config->get_status(dev) & VIRTIO_CONFIG_S_DRIVER_OK))
		virtio_device_ready(dev);

	if (drv->scan)
		drv->scan(dev);

	virtio_config_core_enable(dev);

	return 0;

err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;

}

static void virtio_dev_remove(struct device *_d)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	virtio_config_core_disable(dev);

	drv->remove(dev);

	/* Driver should have reset device. */
	WARN_ON_ONCE(dev->config->get_status(dev));

	/* Acknowledge the device's existence again. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	of_node_put(dev->dev.of_node);
}

/*
 * virtio_irq_get_affinity - get IRQ affinity mask for device
 * @_d: ptr to dev structure
 * @irq_vec: interrupt vector number
 *
 * Return the CPU affinity mask for @_d and @irq_vec.
 */
static const struct cpumask *virtio_irq_get_affinity(struct device *_d,
						     unsigned int irq_vec)
{
	struct virtio_device *dev = dev_to_virtio(_d);

	if (!dev->config->get_vq_affinity)
		return NULL;

	return dev->config->get_vq_affinity(dev, irq_vec);
}

static void virtio_dev_shutdown(struct device *_d)
{
	struct virtio_device *dev = dev_to_virtio(_d);
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	/*
	 * Stop accesses to or from the device.
	 * We only need to do it if there's a driver - no accesses otherwise.
	 */
	if (!drv)
		return;

	/* If the driver has its own shutdown method, use that. */
	if (drv->shutdown) {
		drv->shutdown(dev);
		return;
	}

	/*
	 * Some devices get wedged if you kick them after they are
	 * reset. Mark all vqs as broken to make sure we don't.
	 */
	virtio_break_device(dev);
	/*
	 * Guarantee that any callback will see vq->broken as true.
	 */
	virtio_synchronize_cbs(dev);
	/*
	 * As IOMMUs are reset on shutdown, this will block device access to memory.
	 * Some devices get wedged if this happens, so reset to make sure it does not.
	 */
	dev->config->reset(dev);
}

static const struct bus_type virtio_bus = {
	.name  = "virtio",
	.match = virtio_dev_match,
	.dev_groups = virtio_dev_groups,
	.uevent = virtio_uevent,
	.probe = virtio_dev_probe,
	.remove = virtio_dev_remove,
	.irq_get_affinity = virtio_irq_get_affinity,
	.shutdown = virtio_dev_shutdown,
};

int __register_virtio_driver(struct virtio_driver *driver, struct module *owner)
{
	/* Catch this early. */
	BUG_ON(driver->feature_table_size && !driver->feature_table);
	driver->driver.bus = &virtio_bus;
	driver->driver.owner = owner;

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(__register_virtio_driver);

void unregister_virtio_driver(struct virtio_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(unregister_virtio_driver);

static int virtio_device_of_init(struct virtio_device *dev)
{
	struct device_node *np, *pnode = dev_of_node(dev->dev.parent);
	char compat[] = "virtio,deviceXXXXXXXX";
	int ret, count;

	if (!pnode)
		return 0;

	count = of_get_available_child_count(pnode);
	if (!count)
		return 0;

	/* There can be only 1 child node */
	if (WARN_ON(count > 1))
		return -EINVAL;

	np = of_get_next_available_child(pnode, NULL);
	if (WARN_ON(!np))
		return -ENODEV;

	ret = snprintf(compat, sizeof(compat), "virtio,device%x", dev->id.device);
	BUG_ON(ret >= sizeof(compat));

	/*
	 * On powerpc/pseries virtio devices are PCI devices so PCI
	 * vendor/device ids play the role of the "compatible" property.
	 * Simply don't init of_node in this case.
	 */
	if (!of_device_is_compatible(np, compat)) {
		ret = 0;
		goto out;
	}

	dev->dev.of_node = np;
	return 0;

out:
	of_node_put(np);
	return ret;
}

/**
 * register_virtio_device - register virtio device
 * @dev        : virtio device to be registered
 *
 * On error, the caller must call put_device on &@dev->dev (and not kfree),
 * as another code path may have obtained a reference to @dev.
 *
 * Returns: 0 on success, -error on failure
 */
int register_virtio_device(struct virtio_device *dev)
{
	int err;

	dev->dev.bus = &virtio_bus;
	device_initialize(&dev->dev);

	/* Assign a unique device index and hence name. */
	err = ida_alloc(&virtio_index_ida, GFP_KERNEL);
	if (err < 0)
		goto out;

	dev->index = err;
	err = dev_set_name(&dev->dev, "virtio%u", dev->index);
	if (err)
		goto out_ida_remove;

	err = virtio_device_of_init(dev);
	if (err)
		goto out_ida_remove;

	spin_lock_init(&dev->config_lock);
	dev->config_driver_disabled = false;
	dev->config_core_enabled = false;
	dev->config_change_pending = false;

	INIT_LIST_HEAD(&dev->vqs);
	spin_lock_init(&dev->vqs_list_lock);

	/* We always start by resetting the device, in case a previous
	 * driver messed it up.  This also tests that code path a little. */
	virtio_reset_device(dev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	virtio_debug_device_init(dev);

	/*
	 * device_add() causes the bus infrastructure to look for a matching
	 * driver.
	 */
	err = device_add(&dev->dev);
	if (err)
		goto out_of_node_put;

	return 0;

out_of_node_put:
	of_node_put(dev->dev.of_node);
out_ida_remove:
	ida_free(&virtio_index_ida, dev->index);
out:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(register_virtio_device);

bool is_virtio_device(struct device *dev)
{
	return dev->bus == &virtio_bus;
}
EXPORT_SYMBOL_GPL(is_virtio_device);

void unregister_virtio_device(struct virtio_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	virtio_debug_device_exit(dev);
	ida_free(&virtio_index_ida, index);
}
EXPORT_SYMBOL_GPL(unregister_virtio_device);

static int virtio_device_restore_priv(struct virtio_device *dev, bool restore)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	int ret;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up. */
	virtio_reset_device(dev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	/* Maybe driver failed before freeze.
	 * Restore the failed status, for debugging. */
	if (dev->failed)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);

	if (!drv)
		return 0;

	/* We have a driver! */
	virtio_add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	ret = dev->config->finalize_features(dev);
	if (ret)
		goto err;

	ret = virtio_features_ok(dev);
	if (ret)
		goto err;

	if (restore) {
		if (drv->restore) {
			ret = drv->restore(dev);
			if (ret)
				goto err;
		}
	} else {
		ret = drv->reset_done(dev);
		if (ret)
			goto err;
	}

	/* If restore didn't do it, mark device DRIVER_OK ourselves. */
	if (!(dev->config->get_status(dev) & VIRTIO_CONFIG_S_DRIVER_OK))
		virtio_device_ready(dev);

	virtio_config_core_enable(dev);

	return 0;

err:
	virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
int virtio_device_freeze(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	int ret;

	virtio_config_core_disable(dev);

	dev->failed = dev->config->get_status(dev) & VIRTIO_CONFIG_S_FAILED;

	if (drv && drv->freeze) {
		ret = drv->freeze(dev);
		if (ret) {
			virtio_config_core_enable(dev);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_device_freeze);

int virtio_device_restore(struct virtio_device *dev)
{
	return virtio_device_restore_priv(dev, true);
}
EXPORT_SYMBOL_GPL(virtio_device_restore);
#endif

int virtio_device_reset_prepare(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);
	int ret;

	if (!drv || !drv->reset_prepare)
		return -EOPNOTSUPP;

	virtio_config_core_disable(dev);

	dev->failed = dev->config->get_status(dev) & VIRTIO_CONFIG_S_FAILED;

	ret = drv->reset_prepare(dev);
	if (ret) {
		virtio_config_core_enable(dev);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_device_reset_prepare);

int virtio_device_reset_done(struct virtio_device *dev)
{
	struct virtio_driver *drv = drv_to_virtio(dev->dev.driver);

	if (!drv || !drv->reset_done)
		return -EOPNOTSUPP;

	return virtio_device_restore_priv(dev, false);
}
EXPORT_SYMBOL_GPL(virtio_device_reset_done);

static int virtio_init(void)
{
	BUILD_BUG_ON(offsetof(struct virtio_device, features) !=
		     offsetof(struct virtio_device, features_array[0]));

	if (bus_register(&virtio_bus) != 0)
		panic("virtio bus registration failed");
	virtio_debug_init();
	return 0;
}

static void __exit virtio_exit(void)
{
	virtio_debug_exit();
	bus_unregister(&virtio_bus);
	ida_destroy(&virtio_index_ida);
}
core_initcall(virtio_init);
module_exit(virtio_exit);

MODULE_DESCRIPTION("Virtio core interface");
MODULE_LICENSE("GPL");
