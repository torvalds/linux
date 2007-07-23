#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/lguest_bus.h>
#include <asm/io.h>

static ssize_t type_show(struct device *_dev,
                         struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hu", lguest_devices[dev->index].type);
}
static ssize_t features_show(struct device *_dev,
                             struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hx", lguest_devices[dev->index].features);
}
static ssize_t pfn_show(struct device *_dev,
			 struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%u", lguest_devices[dev->index].pfn);
}
static ssize_t status_show(struct device *_dev,
                           struct device_attribute *attr, char *buf)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	return sprintf(buf, "%hx", lguest_devices[dev->index].status);
}
static ssize_t status_store(struct device *_dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	if (sscanf(buf, "%hi", &lguest_devices[dev->index].status) != 1)
		return -EINVAL;
	return count;
}
static struct device_attribute lguest_dev_attrs[] = {
	__ATTR_RO(type),
	__ATTR_RO(features),
	__ATTR_RO(pfn),
	__ATTR(status, 0644, status_show, status_store),
	__ATTR_NULL
};

static int lguest_dev_match(struct device *_dev, struct device_driver *_drv)
{
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	struct lguest_driver *drv = container_of(_drv,struct lguest_driver,drv);

	return (drv->device_type == lguest_devices[dev->index].type);
}

struct lguest_bus {
	struct bus_type bus;
	struct device dev;
};

static struct lguest_bus lguest_bus = {
	.bus = {
		.name  = "lguest",
		.match = lguest_dev_match,
		.dev_attrs = lguest_dev_attrs,
	},
	.dev = {
		.parent = NULL,
		.bus_id = "lguest",
	}
};

static int lguest_dev_probe(struct device *_dev)
{
	int ret;
	struct lguest_device *dev = container_of(_dev,struct lguest_device,dev);
	struct lguest_driver *drv = container_of(dev->dev.driver,
						struct lguest_driver, drv);

	lguest_devices[dev->index].status |= LGUEST_DEVICE_S_DRIVER;
	ret = drv->probe(dev);
	if (ret == 0)
		lguest_devices[dev->index].status |= LGUEST_DEVICE_S_DRIVER_OK;
	return ret;
}

int register_lguest_driver(struct lguest_driver *drv)
{
	if (!lguest_devices)
		return 0;

	drv->drv.bus = &lguest_bus.bus;
	drv->drv.name = drv->name;
	drv->drv.owner = drv->owner;
	drv->drv.probe = lguest_dev_probe;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(register_lguest_driver);

static void add_lguest_device(unsigned int index)
{
	struct lguest_device *new;

	lguest_devices[index].status |= LGUEST_DEVICE_S_ACKNOWLEDGE;
	new = kmalloc(sizeof(struct lguest_device), GFP_KERNEL);
	if (!new) {
		printk(KERN_EMERG "Cannot allocate lguest device %u\n", index);
		lguest_devices[index].status |= LGUEST_DEVICE_S_FAILED;
		return;
	}

	new->index = index;
	new->private = NULL;
	memset(&new->dev, 0, sizeof(new->dev));
	new->dev.parent = &lguest_bus.dev;
	new->dev.bus = &lguest_bus.bus;
	sprintf(new->dev.bus_id, "%u", index);
	if (device_register(&new->dev) != 0) {
		printk(KERN_EMERG "Cannot register lguest device %u\n", index);
		lguest_devices[index].status |= LGUEST_DEVICE_S_FAILED;
		kfree(new);
	}
}

static void scan_devices(void)
{
	unsigned int i;

	for (i = 0; i < LGUEST_MAX_DEVICES; i++)
		if (lguest_devices[i].type)
			add_lguest_device(i);
}

static int __init lguest_bus_init(void)
{
	if (strcmp(paravirt_ops.name, "lguest") != 0)
		return 0;

	/* Devices are in page above top of "normal" mem. */
	lguest_devices = lguest_map(max_pfn<<PAGE_SHIFT, 1);

	if (bus_register(&lguest_bus.bus) != 0
	    || device_register(&lguest_bus.dev) != 0)
		panic("lguest bus registration failed");

	scan_devices();
	return 0;
}
postcore_initcall(lguest_bus_init);
