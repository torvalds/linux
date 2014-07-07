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
zpci_attr(vfn, "0x%04x\n", vfn);
zpci_attr(pft, "0x%02x\n", pft);
zpci_attr(uid, "0x%x\n", uid);
zpci_attr(segment0, "0x%02x\n", pfip[0]);
zpci_attr(segment1, "0x%02x\n", pfip[1]);
zpci_attr(segment2, "0x%02x\n", pfip[2]);
zpci_attr(segment3, "0x%02x\n", pfip[3]);

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

static ssize_t util_string_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr, char *buf,
				loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
	struct zpci_dev *zdev = get_zdev(pdev);

	return memory_read_from_buffer(buf, count, &off, zdev->util_str,
				       sizeof(zdev->util_str));
}
static BIN_ATTR_RO(util_string, CLP_UTIL_STR_LEN);
static struct bin_attribute *zpci_bin_attrs[] = {
	&bin_attr_util_string,
	NULL,
};

static struct attribute *zpci_dev_attrs[] = {
	&dev_attr_function_id.attr,
	&dev_attr_function_handle.attr,
	&dev_attr_pchid.attr,
	&dev_attr_pfgid.attr,
	&dev_attr_pft.attr,
	&dev_attr_vfn.attr,
	&dev_attr_uid.attr,
	&dev_attr_recover.attr,
	NULL,
};
static struct attribute_group zpci_attr_group = {
	.attrs = zpci_dev_attrs,
	.bin_attrs = zpci_bin_attrs,
};

static struct attribute *pfip_attrs[] = {
	&dev_attr_segment0.attr,
	&dev_attr_segment1.attr,
	&dev_attr_segment2.attr,
	&dev_attr_segment3.attr,
	NULL,
};
static struct attribute_group pfip_attr_group = {
	.name = "pfip",
	.attrs = pfip_attrs,
};

const struct attribute_group *zpci_attr_groups[] = {
	&zpci_attr_group,
	&pfip_attr_group,
	NULL,
};
