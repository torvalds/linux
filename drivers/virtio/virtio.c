#include <linux/virtio.h>
#include <linux/spinlock.h>
#include <linux/virtio_config.h>

static ssize_t device_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	return sprintf(buf, "%hu", dev->id.device);
}
static ssize_t vendor_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	return sprintf(buf, "%hu", dev->id.vendor);
}
static ssize_t status_show(struct device *_d,
			   struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	return sprintf(buf, "0x%08x", dev->config->get_status(dev));
}
static ssize_t modalias_show(struct device *_d,
			     struct device_attribute *attr, char *buf)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);

	return sprintf(buf, "virtio:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
static struct device_attribute virtio_dev_attrs[] = {
	__ATTR_RO(device),
	__ATTR_RO(vendor),
	__ATTR_RO(status),
	__ATTR_RO(modalias),
	__ATTR_NULL
};

static inline int virtio_id_match(const struct virtio_device *dev,
				  const struct virtio_device_id *id)
{
	if (id->device != dev->id.device)
		return 0;

	return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor != dev->id.vendor;
}

/* This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call virtio_dev_probe(). */
static int virtio_dev_match(struct device *_dv, struct device_driver *_dr)
{
	unsigned int i;
	struct virtio_device *dev = container_of(_dv,struct virtio_device,dev);
	const struct virtio_device_id *ids;

	ids = container_of(_dr, struct virtio_driver, driver)->id_table;
	for (i = 0; ids[i].device; i++)
		if (virtio_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int virtio_uevent(struct device *_dv, struct kobj_uevent_env *env)
{
	struct virtio_device *dev = container_of(_dv,struct virtio_device,dev);

	return add_uevent_var(env, "MODALIAS=virtio:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

static struct bus_type virtio_bus = {
	.name  = "virtio",
	.match = virtio_dev_match,
	.dev_attrs = virtio_dev_attrs,
	.uevent = virtio_uevent,
};

static void add_status(struct virtio_device *dev, unsigned status)
{
	dev->config->set_status(dev, dev->config->get_status(dev) | status);
}

void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit)
{
	unsigned int i;
	struct virtio_driver *drv = container_of(vdev->dev.driver,
						 struct virtio_driver, driver);

	for (i = 0; i < drv->feature_table_size; i++)
		if (drv->feature_table[i] == fbit)
			return;
	BUG();
}
EXPORT_SYMBOL_GPL(virtio_check_driver_offered_feature);

static int virtio_dev_probe(struct device *_d)
{
	int err, i;
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	struct virtio_driver *drv = container_of(dev->dev.driver,
						 struct virtio_driver, driver);
	u32 device_features;

	/* We have a driver! */
	add_status(dev, VIRTIO_CONFIG_S_DRIVER);

	/* Figure out what features the device supports. */
	device_features = dev->config->get_features(dev);

	/* Features supported by both device and driver into dev->features. */
	memset(dev->features, 0, sizeof(dev->features));
	for (i = 0; i < drv->feature_table_size; i++) {
		unsigned int f = drv->feature_table[i];
		BUG_ON(f >= 32);
		if (device_features & (1 << f))
			set_bit(f, dev->features);
	}

	err = drv->probe(dev);
	if (err)
		add_status(dev, VIRTIO_CONFIG_S_FAILED);
	else {
		add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);
		/* They should never have set feature bits beyond 32 */
		dev->config->set_features(dev, dev->features[0]);
	}
	return err;
}

static int virtio_dev_remove(struct device *_d)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	struct virtio_driver *drv = container_of(dev->dev.driver,
						 struct virtio_driver, driver);

	drv->remove(dev);

	/* Driver should have reset device. */
	BUG_ON(dev->config->get_status(dev));

	/* Acknowledge the device's existence again. */
	add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	return 0;
}

int register_virtio_driver(struct virtio_driver *driver)
{
	/* Catch this early. */
	BUG_ON(driver->feature_table_size && !driver->feature_table);
	driver->driver.bus = &virtio_bus;
	driver->driver.probe = virtio_dev_probe;
	driver->driver.remove = virtio_dev_remove;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(register_virtio_driver);

void unregister_virtio_driver(struct virtio_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(unregister_virtio_driver);

int register_virtio_device(struct virtio_device *dev)
{
	int err;

	dev->dev.bus = &virtio_bus;
	sprintf(dev->dev.bus_id, "%u", dev->index);

	/* We always start by resetting the device, in case a previous
	 * driver messed it up.  This also tests that code path a little. */
	dev->config->reset(dev);

	/* Acknowledge that we've seen the device. */
	add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	/* device_register() causes the bus infrastructure to look for a
	 * matching driver. */
	err = device_register(&dev->dev);
	if (err)
		add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;
}
EXPORT_SYMBOL_GPL(register_virtio_device);

void unregister_virtio_device(struct virtio_device *dev)
{
	device_unregister(&dev->dev);
}
EXPORT_SYMBOL_GPL(unregister_virtio_device);

static int virtio_init(void)
{
	if (bus_register(&virtio_bus) != 0)
		panic("virtio bus registration failed");
	return 0;
}

static void __exit virtio_exit(void)
{
	bus_unregister(&virtio_bus);
}
core_initcall(virtio_init);
module_exit(virtio_exit);

MODULE_LICENSE("GPL");
