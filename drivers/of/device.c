#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>

#include <asm/errno.h>

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
	return of_match_node(matches, dev->node);
}
EXPORT_SYMBOL(of_match_device);

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
EXPORT_SYMBOL(of_dev_get);

void of_dev_put(struct of_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL(of_dev_put);

static ssize_t dev_show_devspec(struct device *dev,
				struct device_attribute *attr, char *buf)
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
	of_node_put(ofdev->node);
	kfree(ofdev);
}
EXPORT_SYMBOL(of_release_dev);

int of_device_register(struct of_device *ofdev)
{
	int rc;

	BUG_ON(ofdev->node == NULL);

	rc = device_register(&ofdev->dev);
	if (rc)
		return rc;

	rc = device_create_file(&ofdev->dev, &dev_attr_devspec);
	if (rc)
		device_unregister(&ofdev->dev);

	return rc;
}
EXPORT_SYMBOL(of_device_register);

void of_device_unregister(struct of_device *ofdev)
{
	device_remove_file(&ofdev->dev, &dev_attr_devspec);
	device_unregister(&ofdev->dev);
}
EXPORT_SYMBOL(of_device_unregister);
