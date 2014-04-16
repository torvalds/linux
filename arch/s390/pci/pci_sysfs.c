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

#define zpci_attr(name, fmt, member)					\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct zpci_dev *zdev = get_zdev(to_pci_dev(dev));		\
									\
	return sprintf(buf, fmt, zdev->member);				\
}									\
static DEVICE_ATTR_RO(name)

zpci_attr(function_id, "0x%08x\n", fid);
zpci_attr(function_handle, "0x%08x\n", fh);
zpci_attr(pchid, "0x%04x\n", pchid);
zpci_attr(pfgid, "0x%02x\n", pfgid);

static ssize_t recover_store(struct device *dev, struct device_attribute *attr,
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
static DEVICE_ATTR_WO(recover);

static struct device_attribute *zpci_dev_attrs[] = {
	&dev_attr_function_id,
	&dev_attr_function_handle,
	&dev_attr_pchid,
	&dev_attr_pfgid,
	&dev_attr_recover,
	NULL,
};

int zpci_sysfs_add_device(struct device *dev)
{
	int i, rc = 0;

	for (i = 0; zpci_dev_attrs[i]; i++) {
		rc = device_create_file(dev, zpci_dev_attrs[i]);
		if (rc)
			goto error;
	}
	return 0;

error:
	while (--i >= 0)
		device_remove_file(dev, zpci_dev_attrs[i]);
	return rc;
}

void zpci_sysfs_remove_device(struct device *dev)
{
	int i;

	for (i = 0; zpci_dev_attrs[i]; i++)
		device_remove_file(dev, zpci_dev_attrs[i]);
}
