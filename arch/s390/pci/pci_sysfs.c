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

static void recover_callback(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = get_zdev(pdev);
	int ret;

	pci_stop_and_remove_bus_device(pdev);
	ret = zpci_disable_device(zdev);
	if (ret)
		return;

	ret = zpci_enable_device(zdev);
	if (ret)
		return;

	pci_rescan_bus(zdev->bus);
}

static ssize_t store_recover(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int rc = device_schedule_callback(dev, recover_callback);
	return rc ? rc : count;
}
static DEVICE_ATTR(recover, S_IWUSR, NULL, store_recover);

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
