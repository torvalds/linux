/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/pci.h>

static ssize_t show_fid(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct zpci_dev *zdev = get_zdev(to_pci_dev(dev));

	return sprintf(buf, "0x%08x\n", zdev->fid);
}
static DEVICE_ATTR(function_id, S_IRUGO, show_fid, NULL);

static ssize_t show_fh(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct zpci_dev *zdev = get_zdev(to_pci_dev(dev));

	return sprintf(buf, "0x%08x\n", zdev->fh);
}
static DEVICE_ATTR(function_handle, S_IRUGO, show_fh, NULL);

static ssize_t show_pchid(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct zpci_dev *zdev = get_zdev(to_pci_dev(dev));

	return sprintf(buf, "0x%04x\n", zdev->pchid);
}
static DEVICE_ATTR(pchid, S_IRUGO, show_pchid, NULL);

static ssize_t show_pfgid(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct zpci_dev *zdev = get_zdev(to_pci_dev(dev));

	return sprintf(buf, "0x%02x\n", zdev->pfgid);
}
static DEVICE_ATTR(pfgid, S_IRUGO, show_pfgid, NULL);

static ssize_t store_recover(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = get_zdev(pdev);
	int ret;

	if (!device_remove_file_self(dev, attr))
		return count;

	pci_stop_and_remove_bus_device(pdev);
	ret = zpci_disable_device(zdev);
	if (ret)
		return ret;

	ret = zpci_enable_device(zdev);
	if (ret)
		return ret;

	pci_rescan_bus(zdev->bus);
	return count;
}
static DEVICE_ATTR(recover, S_IWUSR, NULL, store_recover);

static struct attribute *zpci_dev_attrs[] = {
	&dev_attr_function_id.attr,
	&dev_attr_function_handle.attr,
	&dev_attr_pchid.attr,
	&dev_attr_pfgid.attr,
	&dev_attr_recover.attr,
	NULL,
};
static struct attribute_group zpci_attr_group = {
	.attrs = zpci_dev_attrs,
};
const struct attribute_group *zpci_attr_groups[] = {
	&zpci_attr_group,
	NULL,
};
