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

static int virtio_dev_probe(struct device *_d)
{
	int err;
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	struct virtio_driver *drv = container_of(dev->dev.driver,
						 struct virtio_driver, driver);

	add_status(dev, VIRTIO_CONFIG_S_DRIVER);
	err = drv->probe(dev);
	if (err)
		add_status(dev, VIRTIO_CONFIG_S_FAILED);
	else
		add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);
	return err;
}

static int virtio_dev_remove(struct device *_d)
{
	struct virtio_device *dev = container_of(_d,struct virtio_device,dev);
	struct virtio_driver *drv = container_of(dev->dev.driver,
						 struct virtio_driver, driver);

	dev->config->set_status(dev, dev->config->get_status(dev)
				& ~VIRTIO_CONFIG_S_DRIVER);
	drv->remove(dev);
	return 0;
}

int register_virtio_driver(struct virtio_driver *driver)
{
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

int __virtio_config_val(struct virtio_device *vdev,
			u8 type, void *val, size_t size)
{
	void *token;
	unsigned int len;

	token = vdev->config->find(vdev, type, &len);
	if (!token)
		return -ENOENT;

	if (len != size)
		return -EIO;

	vdev->config->get(vdev, token, val, size);
	return 0;
}
EXPORT_SYMBOL_GPL(__virtio_config_val);

int virtio_use_bit(struct virtio_device *vdev,
		   void *token, unsigned int len, unsigned int bitnum)
{
	unsigned long bits[16];

	/* This makes it convenient to pass-through find() results. */
	if (!token)
		return 0;

	/* bit not in range of this bitfield? */
	if (bitnum * 8 >= len / 2)
		return 0;

	/* Giant feature bitfields are silly. */
	BUG_ON(len > sizeof(bits));
	vdev->config->get(vdev, token, bits, len);

	if (!test_bit(bitnum, bits))
		return 0;

	/* Set acknowledge bit, and write it back. */
	set_bit(bitnum + len * 8 / 2, bits);
	vdev->config->set(vdev, token, bits, len);
	return 1;
}
EXPORT_SYMBOL_GPL(virtio_use_bit);

static int virtio_init(void)
{
	if (bus_register(&virtio_bus) != 0)
		panic("virtio bus registration failed");
	return 0;
}
core_initcall(virtio_init);
