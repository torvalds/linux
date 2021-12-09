// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include "core.h"

/*
 * An entire PCI topology full of devices should be enough for any
 * config
 */
#define CXL_MEM_MAX_DEVS 65536

static int cxl_mem_major;
static DEFINE_IDA(cxl_memdev_ida);

static void cxl_memdev_release(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);

	ida_free(&cxl_memdev_ida, cxlmd->id);
	kfree(cxlmd);
}

static char *cxl_memdev_devnode(struct device *dev, umode_t *mode, kuid_t *uid,
				kgid_t *gid)
{
	return kasprintf(GFP_KERNEL, "cxl/%s", dev_name(dev));
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_mem *cxlm = cxlmd->cxlm;

	return sysfs_emit(buf, "%.16s\n", cxlm->firmware_version);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t payload_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_mem *cxlm = cxlmd->cxlm;

	return sysfs_emit(buf, "%zu\n", cxlm->payload_size);
}
static DEVICE_ATTR_RO(payload_max);

static ssize_t label_storage_size_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_mem *cxlm = cxlmd->cxlm;

	return sysfs_emit(buf, "%zu\n", cxlm->lsa_size);
}
static DEVICE_ATTR_RO(label_storage_size);

static ssize_t ram_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_mem *cxlm = cxlmd->cxlm;
	unsigned long long len = range_len(&cxlm->ram_range);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_ram_size =
	__ATTR(size, 0444, ram_size_show, NULL);

static ssize_t pmem_size_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_mem *cxlm = cxlmd->cxlm;
	unsigned long long len = range_len(&cxlm->pmem_range);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_pmem_size =
	__ATTR(size, 0444, pmem_size_show, NULL);

static struct attribute *cxl_memdev_attributes[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_payload_max.attr,
	&dev_attr_label_storage_size.attr,
	NULL,
};

static struct attribute *cxl_memdev_pmem_attributes[] = {
	&dev_attr_pmem_size.attr,
	NULL,
};

static struct attribute *cxl_memdev_ram_attributes[] = {
	&dev_attr_ram_size.attr,
	NULL,
};

static struct attribute_group cxl_memdev_attribute_group = {
	.attrs = cxl_memdev_attributes,
};

static struct attribute_group cxl_memdev_ram_attribute_group = {
	.name = "ram",
	.attrs = cxl_memdev_ram_attributes,
};

static struct attribute_group cxl_memdev_pmem_attribute_group = {
	.name = "pmem",
	.attrs = cxl_memdev_pmem_attributes,
};

static const struct attribute_group *cxl_memdev_attribute_groups[] = {
	&cxl_memdev_attribute_group,
	&cxl_memdev_ram_attribute_group,
	&cxl_memdev_pmem_attribute_group,
	NULL,
};

static const struct device_type cxl_memdev_type = {
	.name = "cxl_memdev",
	.release = cxl_memdev_release,
	.devnode = cxl_memdev_devnode,
	.groups = cxl_memdev_attribute_groups,
};

static void cxl_memdev_unregister(void *_cxlmd)
{
	struct cxl_memdev *cxlmd = _cxlmd;
	struct device *dev = &cxlmd->dev;
	struct cdev *cdev = &cxlmd->cdev;
	const struct cdevm_file_operations *cdevm_fops;

	cdevm_fops = container_of(cdev->ops, typeof(*cdevm_fops), fops);
	cdevm_fops->shutdown(dev);

	cdev_device_del(&cxlmd->cdev, dev);
	put_device(dev);
}

static struct cxl_memdev *cxl_memdev_alloc(struct cxl_mem *cxlm,
					   const struct file_operations *fops)
{
	struct pci_dev *pdev = cxlm->pdev;
	struct cxl_memdev *cxlmd;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	cxlmd = kzalloc(sizeof(*cxlmd), GFP_KERNEL);
	if (!cxlmd)
		return ERR_PTR(-ENOMEM);

	rc = ida_alloc_range(&cxl_memdev_ida, 0, CXL_MEM_MAX_DEVS, GFP_KERNEL);
	if (rc < 0)
		goto err;
	cxlmd->id = rc;

	dev = &cxlmd->dev;
	device_initialize(dev);
	dev->parent = &pdev->dev;
	dev->bus = &cxl_bus_type;
	dev->devt = MKDEV(cxl_mem_major, cxlmd->id);
	dev->type = &cxl_memdev_type;
	device_set_pm_not_required(dev);

	cdev = &cxlmd->cdev;
	cdev_init(cdev, fops);
	return cxlmd;

err:
	kfree(cxlmd);
	return ERR_PTR(rc);
}

struct cxl_memdev *
devm_cxl_add_memdev(struct device *host, struct cxl_mem *cxlm,
		    const struct cdevm_file_operations *cdevm_fops)
{
	struct cxl_memdev *cxlmd;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	cxlmd = cxl_memdev_alloc(cxlm, &cdevm_fops->fops);
	if (IS_ERR(cxlmd))
		return cxlmd;

	dev = &cxlmd->dev;
	rc = dev_set_name(dev, "mem%d", cxlmd->id);
	if (rc)
		goto err;

	/*
	 * Activate ioctl operations, no cxl_memdev_rwsem manipulation
	 * needed as this is ordered with cdev_add() publishing the device.
	 */
	cxlmd->cxlm = cxlm;

	cdev = &cxlmd->cdev;
	rc = cdev_device_add(cdev, dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(host, cxl_memdev_unregister, cxlmd);
	if (rc)
		return ERR_PTR(rc);
	return cxlmd;

err:
	/*
	 * The cdev was briefly live, shutdown any ioctl operations that
	 * saw that state.
	 */
	cdevm_fops->shutdown(dev);
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_cxl_add_memdev);

__init int cxl_memdev_init(void)
{
	dev_t devt;
	int rc;

	rc = alloc_chrdev_region(&devt, 0, CXL_MEM_MAX_DEVS, "cxl");
	if (rc)
		return rc;

	cxl_mem_major = MAJOR(devt);

	return 0;
}

void cxl_memdev_exit(void)
{
	unregister_chrdev_region(MKDEV(cxl_mem_major, 0), CXL_MEM_MAX_DEVS);
}
