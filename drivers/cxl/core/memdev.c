// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include "core.h"

static DECLARE_RWSEM(cxl_memdev_rwsem);

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
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%.16s\n", cxlds->firmware_version);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t payload_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%zu\n", cxlds->payload_size);
}
static DEVICE_ATTR_RO(payload_max);

static ssize_t label_storage_size_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%zu\n", cxlds->lsa_size);
}
static DEVICE_ATTR_RO(label_storage_size);

static ssize_t ram_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = range_len(&cxlds->ram_range);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_ram_size =
	__ATTR(size, 0444, ram_size_show, NULL);

static ssize_t pmem_size_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = range_len(&cxlds->pmem_range);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_pmem_size =
	__ATTR(size, 0444, pmem_size_show, NULL);

static ssize_t serial_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	return sysfs_emit(buf, "%#llx\n", cxlds->serial);
}
static DEVICE_ATTR_RO(serial);

static struct attribute *cxl_memdev_attributes[] = {
	&dev_attr_serial.attr,
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

/**
 * set_exclusive_cxl_commands() - atomically disable user cxl commands
 * @cxlds: The device state to operate on
 * @cmds: bitmap of commands to mark exclusive
 *
 * Grab the cxl_memdev_rwsem in write mode to flush in-flight
 * invocations of the ioctl path and then disable future execution of
 * commands with the command ids set in @cmds.
 */
void set_exclusive_cxl_commands(struct cxl_dev_state *cxlds, unsigned long *cmds)
{
	down_write(&cxl_memdev_rwsem);
	bitmap_or(cxlds->exclusive_cmds, cxlds->exclusive_cmds, cmds,
		  CXL_MEM_COMMAND_ID_MAX);
	up_write(&cxl_memdev_rwsem);
}
EXPORT_SYMBOL_NS_GPL(set_exclusive_cxl_commands, CXL);

/**
 * clear_exclusive_cxl_commands() - atomically enable user cxl commands
 * @cxlds: The device state to modify
 * @cmds: bitmap of commands to mark available for userspace
 */
void clear_exclusive_cxl_commands(struct cxl_dev_state *cxlds, unsigned long *cmds)
{
	down_write(&cxl_memdev_rwsem);
	bitmap_andnot(cxlds->exclusive_cmds, cxlds->exclusive_cmds, cmds,
		      CXL_MEM_COMMAND_ID_MAX);
	up_write(&cxl_memdev_rwsem);
}
EXPORT_SYMBOL_NS_GPL(clear_exclusive_cxl_commands, CXL);

static void cxl_memdev_shutdown(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);

	down_write(&cxl_memdev_rwsem);
	cxlmd->cxlds = NULL;
	up_write(&cxl_memdev_rwsem);
}

static void cxl_memdev_unregister(void *_cxlmd)
{
	struct cxl_memdev *cxlmd = _cxlmd;
	struct device *dev = &cxlmd->dev;

	cxl_memdev_shutdown(dev);
	cdev_device_del(&cxlmd->cdev, dev);
	put_device(dev);
}

static struct cxl_memdev *cxl_memdev_alloc(struct cxl_dev_state *cxlds,
					   const struct file_operations *fops)
{
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
	dev->parent = cxlds->dev;
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

static long __cxl_memdev_ioctl(struct cxl_memdev *cxlmd, unsigned int cmd,
			       unsigned long arg)
{
	switch (cmd) {
	case CXL_MEM_QUERY_COMMANDS:
		return cxl_query_cmd(cxlmd, (void __user *)arg);
	case CXL_MEM_SEND_COMMAND:
		return cxl_send_cmd(cxlmd, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static long cxl_memdev_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct cxl_memdev *cxlmd = file->private_data;
	int rc = -ENXIO;

	down_read(&cxl_memdev_rwsem);
	if (cxlmd->cxlds)
		rc = __cxl_memdev_ioctl(cxlmd, cmd, arg);
	up_read(&cxl_memdev_rwsem);

	return rc;
}

static int cxl_memdev_open(struct inode *inode, struct file *file)
{
	struct cxl_memdev *cxlmd =
		container_of(inode->i_cdev, typeof(*cxlmd), cdev);

	get_device(&cxlmd->dev);
	file->private_data = cxlmd;

	return 0;
}

static int cxl_memdev_release_file(struct inode *inode, struct file *file)
{
	struct cxl_memdev *cxlmd =
		container_of(inode->i_cdev, typeof(*cxlmd), cdev);

	put_device(&cxlmd->dev);

	return 0;
}

static const struct file_operations cxl_memdev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cxl_memdev_ioctl,
	.open = cxl_memdev_open,
	.release = cxl_memdev_release_file,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

struct cxl_memdev *devm_cxl_add_memdev(struct cxl_dev_state *cxlds)
{
	struct cxl_memdev *cxlmd;
	struct device *dev;
	struct cdev *cdev;
	int rc;

	cxlmd = cxl_memdev_alloc(cxlds, &cxl_memdev_fops);
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
	cxlmd->cxlds = cxlds;

	cdev = &cxlmd->cdev;
	rc = cdev_device_add(cdev, dev);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(cxlds->dev, cxl_memdev_unregister, cxlmd);
	if (rc)
		return ERR_PTR(rc);
	return cxlmd;

err:
	/*
	 * The cdev was briefly live, shutdown any ioctl operations that
	 * saw that state.
	 */
	cxl_memdev_shutdown(dev);
	put_device(dev);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_add_memdev, CXL);

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
