// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/pci.h>
#include <cxlmem.h>
#include "trace.h"
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

static char *cxl_memdev_devnode(const struct device *dev, umode_t *mode, kuid_t *uid,
				kgid_t *gid)
{
	return kasprintf(GFP_KERNEL, "cxl/%s", dev_name(dev));
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	return sysfs_emit(buf, "%.16s\n", mds->firmware_version);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t payload_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	return sysfs_emit(buf, "%zu\n", mds->payload_size);
}
static DEVICE_ATTR_RO(payload_max);

static ssize_t label_storage_size_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlds);

	return sysfs_emit(buf, "%zu\n", mds->lsa_size);
}
static DEVICE_ATTR_RO(label_storage_size);

static ssize_t ram_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = resource_size(&cxlds->ram_res);

	return sysfs_emit(buf, "%#llx\n", len);
}

static struct device_attribute dev_attr_ram_size =
	__ATTR(size, 0444, ram_size_show, NULL);

static ssize_t pmem_size_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	unsigned long long len = resource_size(&cxlds->pmem_res);

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

static ssize_t numa_node_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", dev_to_node(dev));
}
static DEVICE_ATTR_RO(numa_node);

static int cxl_get_poison_by_memdev(struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	u64 offset, length;
	int rc = 0;

	/* CXL 3.0 Spec 8.2.9.8.4.1 Separate pmem and ram poison requests */
	if (resource_size(&cxlds->pmem_res)) {
		offset = cxlds->pmem_res.start;
		length = resource_size(&cxlds->pmem_res);
		rc = cxl_mem_get_poison(cxlmd, offset, length, NULL);
		if (rc)
			return rc;
	}
	if (resource_size(&cxlds->ram_res)) {
		offset = cxlds->ram_res.start;
		length = resource_size(&cxlds->ram_res);
		rc = cxl_mem_get_poison(cxlmd, offset, length, NULL);
		/*
		 * Invalid Physical Address is not an error for
		 * volatile addresses. Device support is optional.
		 */
		if (rc == -EFAULT)
			rc = 0;
	}
	return rc;
}

int cxl_trigger_poison_list(struct cxl_memdev *cxlmd)
{
	struct cxl_port *port;
	int rc;

	port = dev_get_drvdata(&cxlmd->dev);
	if (!port || !is_cxl_endpoint(port))
		return -EINVAL;

	rc = down_read_interruptible(&cxl_dpa_rwsem);
	if (rc)
		return rc;

	if (port->commit_end == -1) {
		/* No regions mapped to this memdev */
		rc = cxl_get_poison_by_memdev(cxlmd);
	} else {
		/* Regions mapped, collect poison by endpoint */
		rc =  cxl_get_poison_by_endpoint(port);
	}
	up_read(&cxl_dpa_rwsem);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_trigger_poison_list, CXL);

struct cxl_dpa_to_region_context {
	struct cxl_region *cxlr;
	u64 dpa;
};

static int __cxl_dpa_to_region(struct device *dev, void *arg)
{
	struct cxl_dpa_to_region_context *ctx = arg;
	struct cxl_endpoint_decoder *cxled;
	u64 dpa = ctx->dpa;

	if (!is_endpoint_decoder(dev))
		return 0;

	cxled = to_cxl_endpoint_decoder(dev);
	if (!cxled->dpa_res || !resource_size(cxled->dpa_res))
		return 0;

	if (dpa > cxled->dpa_res->end || dpa < cxled->dpa_res->start)
		return 0;

	dev_dbg(dev, "dpa:0x%llx mapped in region:%s\n", dpa,
		dev_name(&cxled->cxld.region->dev));

	ctx->cxlr = cxled->cxld.region;

	return 1;
}

static struct cxl_region *cxl_dpa_to_region(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_dpa_to_region_context ctx;
	struct cxl_port *port;

	ctx = (struct cxl_dpa_to_region_context) {
		.dpa = dpa,
	};
	port = dev_get_drvdata(&cxlmd->dev);
	if (port && is_cxl_endpoint(port) && port->commit_end != -1)
		device_for_each_child(&port->dev, &ctx, __cxl_dpa_to_region);

	return ctx.cxlr;
}

static int cxl_validate_poison_dpa(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	if (!resource_size(&cxlds->dpa_res)) {
		dev_dbg(cxlds->dev, "device has no dpa resource\n");
		return -EINVAL;
	}
	if (dpa < cxlds->dpa_res.start || dpa > cxlds->dpa_res.end) {
		dev_dbg(cxlds->dev, "dpa:0x%llx not in resource:%pR\n",
			dpa, &cxlds->dpa_res);
		return -EINVAL;
	}
	if (!IS_ALIGNED(dpa, 64)) {
		dev_dbg(cxlds->dev, "dpa:0x%llx is not 64-byte aligned\n", dpa);
		return -EINVAL;
	}

	return 0;
}

int cxl_inject_poison(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_mbox_inject_poison inject;
	struct cxl_poison_record record;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_region *cxlr;
	int rc;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	rc = down_read_interruptible(&cxl_dpa_rwsem);
	if (rc)
		return rc;

	rc = cxl_validate_poison_dpa(cxlmd, dpa);
	if (rc)
		goto out;

	inject.address = cpu_to_le64(dpa);
	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_INJECT_POISON,
		.size_in = sizeof(inject),
		.payload_in = &inject,
	};
	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc)
		goto out;

	cxlr = cxl_dpa_to_region(cxlmd, dpa);
	if (cxlr)
		dev_warn_once(mds->cxlds.dev,
			      "poison inject dpa:%#llx region: %s\n", dpa,
			      dev_name(&cxlr->dev));

	record = (struct cxl_poison_record) {
		.address = cpu_to_le64(dpa),
		.length = cpu_to_le32(1),
	};
	trace_cxl_poison(cxlmd, cxlr, &record, 0, 0, CXL_POISON_TRACE_INJECT);
out:
	up_read(&cxl_dpa_rwsem);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_inject_poison, CXL);

int cxl_clear_poison(struct cxl_memdev *cxlmd, u64 dpa)
{
	struct cxl_memdev_state *mds = to_cxl_memdev_state(cxlmd->cxlds);
	struct cxl_mbox_clear_poison clear;
	struct cxl_poison_record record;
	struct cxl_mbox_cmd mbox_cmd;
	struct cxl_region *cxlr;
	int rc;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	rc = down_read_interruptible(&cxl_dpa_rwsem);
	if (rc)
		return rc;

	rc = cxl_validate_poison_dpa(cxlmd, dpa);
	if (rc)
		goto out;

	/*
	 * In CXL 3.0 Spec 8.2.9.8.4.3, the Clear Poison mailbox command
	 * is defined to accept 64 bytes of write-data, along with the
	 * address to clear. This driver uses zeroes as write-data.
	 */
	clear = (struct cxl_mbox_clear_poison) {
		.address = cpu_to_le64(dpa)
	};

	mbox_cmd = (struct cxl_mbox_cmd) {
		.opcode = CXL_MBOX_OP_CLEAR_POISON,
		.size_in = sizeof(clear),
		.payload_in = &clear,
	};

	rc = cxl_internal_send_cmd(mds, &mbox_cmd);
	if (rc)
		goto out;

	cxlr = cxl_dpa_to_region(cxlmd, dpa);
	if (cxlr)
		dev_warn_once(mds->cxlds.dev,
			      "poison clear dpa:%#llx region: %s\n", dpa,
			      dev_name(&cxlr->dev));

	record = (struct cxl_poison_record) {
		.address = cpu_to_le64(dpa),
		.length = cpu_to_le32(1),
	};
	trace_cxl_poison(cxlmd, cxlr, &record, 0, 0, CXL_POISON_TRACE_CLEAR);
out:
	up_read(&cxl_dpa_rwsem);

	return rc;
}
EXPORT_SYMBOL_NS_GPL(cxl_clear_poison, CXL);

static struct attribute *cxl_memdev_attributes[] = {
	&dev_attr_serial.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_payload_max.attr,
	&dev_attr_label_storage_size.attr,
	&dev_attr_numa_node.attr,
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

static umode_t cxl_memdev_visible(struct kobject *kobj, struct attribute *a,
				  int n)
{
	if (!IS_ENABLED(CONFIG_NUMA) && a == &dev_attr_numa_node.attr)
		return 0;
	return a->mode;
}

static struct attribute_group cxl_memdev_attribute_group = {
	.attrs = cxl_memdev_attributes,
	.is_visible = cxl_memdev_visible,
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

bool is_cxl_memdev(const struct device *dev)
{
	return dev->type == &cxl_memdev_type;
}
EXPORT_SYMBOL_NS_GPL(is_cxl_memdev, CXL);

/**
 * set_exclusive_cxl_commands() - atomically disable user cxl commands
 * @mds: The device state to operate on
 * @cmds: bitmap of commands to mark exclusive
 *
 * Grab the cxl_memdev_rwsem in write mode to flush in-flight
 * invocations of the ioctl path and then disable future execution of
 * commands with the command ids set in @cmds.
 */
void set_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				unsigned long *cmds)
{
	down_write(&cxl_memdev_rwsem);
	bitmap_or(mds->exclusive_cmds, mds->exclusive_cmds, cmds,
		  CXL_MEM_COMMAND_ID_MAX);
	up_write(&cxl_memdev_rwsem);
}
EXPORT_SYMBOL_NS_GPL(set_exclusive_cxl_commands, CXL);

/**
 * clear_exclusive_cxl_commands() - atomically enable user cxl commands
 * @mds: The device state to modify
 * @cmds: bitmap of commands to mark available for userspace
 */
void clear_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				  unsigned long *cmds)
{
	down_write(&cxl_memdev_rwsem);
	bitmap_andnot(mds->exclusive_cmds, mds->exclusive_cmds, cmds,
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

static void detach_memdev(struct work_struct *work)
{
	struct cxl_memdev *cxlmd;

	cxlmd = container_of(work, typeof(*cxlmd), detach_work);
	device_release_driver(&cxlmd->dev);
	put_device(&cxlmd->dev);
}

static struct lock_class_key cxl_memdev_key;

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

	rc = ida_alloc_max(&cxl_memdev_ida, CXL_MEM_MAX_DEVS - 1, GFP_KERNEL);
	if (rc < 0)
		goto err;
	cxlmd->id = rc;
	cxlmd->depth = -1;

	dev = &cxlmd->dev;
	device_initialize(dev);
	lockdep_set_class(&dev->mutex, &cxl_memdev_key);
	dev->parent = cxlds->dev;
	dev->bus = &cxl_bus_type;
	dev->devt = MKDEV(cxl_mem_major, cxlmd->id);
	dev->type = &cxl_memdev_type;
	device_set_pm_not_required(dev);
	INIT_WORK(&cxlmd->detach_work, detach_memdev);

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
	cxlds->cxlmd = cxlmd;

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
