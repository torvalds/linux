#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <asm/errno.h>
#include <asm/of_device.h>

/**
 * of_match_device - Tell if an of_device structure has a matching
 * of_match structure
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an of_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					const struct of_device *dev)
{
	if (!dev->node)
		return NULL;
	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= dev->node->name
				&& !strcmp(matches->name, dev->node->name);
		if (matches->type[0])
			match &= dev->node->type
				&& !strcmp(matches->type, dev->node->type);
		if (matches->compatible[0])
			match &= device_is_compatible(dev->node,
				matches->compatible);
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}

static int of_platform_bus_match(struct device *dev, struct device_driver *drv)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * of_drv = to_of_platform_driver(drv);
	const struct of_device_id * matches = of_drv->match_table;

	if (!matches)
		return 0;

	return of_match_device(matches, of_dev) != NULL;
}

struct of_device *of_dev_get(struct of_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_of_device(tmp);
	else
		return NULL;
}

void of_dev_put(struct of_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}


static int of_device_probe(struct device *dev)
{
	int error = -ENODEV;
	struct of_platform_driver *drv;
	struct of_device *of_dev;
	const struct of_device_id *match;

	drv = to_of_platform_driver(dev->driver);
	of_dev = to_of_device(dev);

	if (!drv->probe)
		return error;

	of_dev_get(of_dev);

	match = of_match_device(drv->match_table, of_dev);
	if (match)
		error = drv->probe(of_dev, match);
	if (error)
		of_dev_put(of_dev);

	return error;
}

static int of_device_remove(struct device *dev)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(of_dev);
	return 0;
}

static int of_device_suspend(struct device *dev, pm_message_t state)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->suspend)
		error = drv->suspend(of_dev, state);
	return error;
}

static int of_device_resume(struct device * dev)
{
	struct of_device * of_dev = to_of_device(dev);
	struct of_platform_driver * drv = to_of_platform_driver(dev->driver);
	int error = 0;

	if (dev->driver && drv->resume)
		error = drv->resume(of_dev);
	return error;
}

struct bus_type of_platform_bus_type = {
       .name	= "of_platform",
       .match	= of_platform_bus_match,
       .suspend	= of_device_suspend,
       .resume	= of_device_resume,
};

static int __init of_bus_driver_init(void)
{
	return bus_register(&of_platform_bus_type);
}

postcore_initcall(of_bus_driver_init);

int of_register_driver(struct of_platform_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &of_platform_bus_type;
	drv->driver.probe = of_device_probe;
	drv->driver.remove = of_device_remove;

	/* register with core */
	count = driver_register(&drv->driver);
	return count ? count : 1;
}

void of_unregister_driver(struct of_platform_driver *drv)
{
	driver_unregister(&drv->driver);
}


static ssize_t dev_show_devspec(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct of_device *ofdev;

	ofdev = to_of_device(dev);
	return sprintf(buf, "%s", ofdev->node->full_name);
}

static DEVICE_ATTR(devspec, S_IRUGO, dev_show_devspec, NULL);

/**
 * of_release_dev - free an of device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this of device are
 * done.
 */
void of_release_dev(struct device *dev)
{
	struct of_device *ofdev;

        ofdev = to_of_device(dev);
	kfree(ofdev);
}

int of_device_register(struct of_device *ofdev)
{
	int rc;
	struct of_device **odprop;

	BUG_ON(ofdev->node == NULL);

	odprop = (struct of_device **)get_property(ofdev->node, "linux,device", NULL);
	if (!odprop) {
		struct property *new_prop;
	
		new_prop = kmalloc(sizeof(struct property) + sizeof(struct of_device *),
			GFP_KERNEL);
		if (new_prop == NULL)
			return -ENOMEM;
		new_prop->name = "linux,device";
		new_prop->length = sizeof(sizeof(struct of_device *));
		new_prop->value = (unsigned char *)&new_prop[1];
		odprop = (struct of_device **)new_prop->value;
		*odprop = NULL;
		prom_add_property(ofdev->node, new_prop);
	}
	*odprop = ofdev;

	rc = device_register(&ofdev->dev);
	if (rc)
		return rc;

	device_create_file(&ofdev->dev, &dev_attr_devspec);

	return 0;
}

void of_device_unregister(struct of_device *ofdev)
{
	struct of_device **odprop;

	device_remove_file(&ofdev->dev, &dev_attr_devspec);

	odprop = (struct of_device **)get_property(ofdev->node, "linux,device", NULL);
	if (odprop)
		*odprop = NULL;

	device_unregister(&ofdev->dev);
}

struct of_device* of_platform_device_create(struct device_node *np,
					    const char *bus_id,
					    struct device *parent)
{
	struct of_device *dev;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));

	dev->node = np;
	dev->dma_mask = 0xffffffffUL;
	dev->dev.dma_mask = &dev->dma_mask;
	dev->dev.parent = parent;
	dev->dev.bus = &of_platform_bus_type;
	dev->dev.release = of_release_dev;

	strlcpy(dev->dev.bus_id, bus_id, BUS_ID_SIZE);

	if (of_device_register(dev) != 0) {
		kfree(dev);
		return NULL;
	}

	return dev;
}


EXPORT_SYMBOL(of_match_device);
EXPORT_SYMBOL(of_platform_bus_type);
EXPORT_SYMBOL(of_register_driver);
EXPORT_SYMBOL(of_unregister_driver);
EXPORT_SYMBOL(of_device_register);
EXPORT_SYMBOL(of_device_unregister);
EXPORT_SYMBOL(of_dev_get);
EXPORT_SYMBOL(of_dev_put);
EXPORT_SYMBOL(of_platform_device_create);
EXPORT_SYMBOL(of_release_dev);
