// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Manager Core
 *
 *  Copyright (C) 2013-2015 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 *
 * With code from the mailing list:
 * Copyright (C) 2013 Xilinx, Inc.
 */
#include <linux/firmware.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>

static DEFINE_IDA(fpga_mgr_ida);
static struct class *fpga_mgr_class;

/**
 * fpga_image_info_alloc - Allocate a FPGA image info struct
 * @dev: owning device
 *
 * Return: struct fpga_image_info or NULL
 */
struct fpga_image_info *fpga_image_info_alloc(struct device *dev)
{
	struct fpga_image_info *info;

	get_device(dev);

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		put_device(dev);
		return NULL;
	}

	info->dev = dev;

	return info;
}
EXPORT_SYMBOL_GPL(fpga_image_info_alloc);

/**
 * fpga_image_info_free - Free a FPGA image info struct
 * @info: FPGA image info struct to free
 */
void fpga_image_info_free(struct fpga_image_info *info)
{
	struct device *dev;

	if (!info)
		return;

	dev = info->dev;
	if (info->firmware_name)
		devm_kfree(dev, info->firmware_name);

	devm_kfree(dev, info);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(fpga_image_info_free);

/*
 * Call the low level driver's write_init function.  This will do the
 * device-specific things to get the FPGA into the state where it is ready to
 * receive an FPGA image. The low level driver only gets to see the first
 * initial_header_size bytes in the buffer.
 */
static int fpga_mgr_write_init_buf(struct fpga_manager *mgr,
				   struct fpga_image_info *info,
				   const char *buf, size_t count)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE_INIT;
	if (!mgr->mops->initial_header_size)
		ret = mgr->mops->write_init(mgr, info, NULL, 0);
	else
		ret = mgr->mops->write_init(
		    mgr, info, buf, min(mgr->mops->initial_header_size, count));

	if (ret) {
		dev_err(&mgr->dev, "Error preparing FPGA for writing\n");
		mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
		return ret;
	}

	return 0;
}

static int fpga_mgr_write_init_sg(struct fpga_manager *mgr,
				  struct fpga_image_info *info,
				  struct sg_table *sgt)
{
	struct sg_mapping_iter miter;
	size_t len;
	char *buf;
	int ret;

	if (!mgr->mops->initial_header_size)
		return fpga_mgr_write_init_buf(mgr, info, NULL, 0);

	/*
	 * First try to use miter to map the first fragment to access the
	 * header, this is the typical path.
	 */
	sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
	if (sg_miter_next(&miter) &&
	    miter.length >= mgr->mops->initial_header_size) {
		ret = fpga_mgr_write_init_buf(mgr, info, miter.addr,
					      miter.length);
		sg_miter_stop(&miter);
		return ret;
	}
	sg_miter_stop(&miter);

	/* Otherwise copy the fragments into temporary memory. */
	buf = kmalloc(mgr->mops->initial_header_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = sg_copy_to_buffer(sgt->sgl, sgt->nents, buf,
				mgr->mops->initial_header_size);
	ret = fpga_mgr_write_init_buf(mgr, info, buf, len);

	kfree(buf);

	return ret;
}

/*
 * After all the FPGA image has been written, do the device specific steps to
 * finish and set the FPGA into operating mode.
 */
static int fpga_mgr_write_complete(struct fpga_manager *mgr,
				   struct fpga_image_info *info)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
	ret = mgr->mops->write_complete(mgr, info);
	if (ret) {
		dev_err(&mgr->dev, "Error after writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE_ERR;
		return ret;
	}
	mgr->state = FPGA_MGR_STATE_OPERATING;

	return 0;
}

/**
 * fpga_mgr_buf_load_sg - load fpga from image in buffer from a scatter list
 * @mgr:	fpga manager
 * @info:	fpga image specific information
 * @sgt:	scatterlist table
 *
 * Step the low level fpga manager through the device-specific steps of getting
 * an FPGA ready to be configured, writing the image to it, then doing whatever
 * post-configuration steps necessary.  This code assumes the caller got the
 * mgr pointer from of_fpga_mgr_get() or fpga_mgr_get() and checked that it is
 * not an error code.
 *
 * This is the preferred entry point for FPGA programming, it does not require
 * any contiguous kernel memory.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_buf_load_sg(struct fpga_manager *mgr,
				struct fpga_image_info *info,
				struct sg_table *sgt)
{
	int ret;

	ret = fpga_mgr_write_init_sg(mgr, info, sgt);
	if (ret)
		return ret;

	/* Write the FPGA image to the FPGA. */
	mgr->state = FPGA_MGR_STATE_WRITE;
	if (mgr->mops->write_sg) {
		ret = mgr->mops->write_sg(mgr, sgt);
	} else {
		struct sg_mapping_iter miter;

		sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
		while (sg_miter_next(&miter)) {
			ret = mgr->mops->write(mgr, miter.addr, miter.length);
			if (ret)
				break;
		}
		sg_miter_stop(&miter);
	}

	if (ret) {
		dev_err(&mgr->dev, "Error while writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	return fpga_mgr_write_complete(mgr, info);
}

static int fpga_mgr_buf_load_mapped(struct fpga_manager *mgr,
				    struct fpga_image_info *info,
				    const char *buf, size_t count)
{
	int ret;

	ret = fpga_mgr_write_init_buf(mgr, info, buf, count);
	if (ret)
		return ret;

	/*
	 * Write the FPGA image to the FPGA.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE;
	ret = mgr->mops->write(mgr, buf, count);
	if (ret) {
		dev_err(&mgr->dev, "Error while writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	return fpga_mgr_write_complete(mgr, info);
}

/**
 * fpga_mgr_buf_load - load fpga from image in buffer
 * @mgr:	fpga manager
 * @info:	fpga image info
 * @buf:	buffer contain fpga image
 * @count:	byte count of buf
 *
 * Step the low level fpga manager through the device-specific steps of getting
 * an FPGA ready to be configured, writing the image to it, then doing whatever
 * post-configuration steps necessary.  This code assumes the caller got the
 * mgr pointer from of_fpga_mgr_get() and checked that it is not an error code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_buf_load(struct fpga_manager *mgr,
			     struct fpga_image_info *info,
			     const char *buf, size_t count)
{
	struct page **pages;
	struct sg_table sgt;
	const void *p;
	int nr_pages;
	int index;
	int rc;

	/*
	 * This is just a fast path if the caller has already created a
	 * contiguous kernel buffer and the driver doesn't require SG, non-SG
	 * drivers will still work on the slow path.
	 */
	if (mgr->mops->write)
		return fpga_mgr_buf_load_mapped(mgr, info, buf, count);

	/*
	 * Convert the linear kernel pointer into a sg_table of pages for use
	 * by the driver.
	 */
	nr_pages = DIV_ROUND_UP((unsigned long)buf + count, PAGE_SIZE) -
		   (unsigned long)buf / PAGE_SIZE;
	pages = kmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	p = buf - offset_in_page(buf);
	for (index = 0; index < nr_pages; index++) {
		if (is_vmalloc_addr(p))
			pages[index] = vmalloc_to_page(p);
		else
			pages[index] = kmap_to_page((void *)p);
		if (!pages[index]) {
			kfree(pages);
			return -EFAULT;
		}
		p += PAGE_SIZE;
	}

	/*
	 * The temporary pages list is used to code share the merging algorithm
	 * in sg_alloc_table_from_pages
	 */
	rc = sg_alloc_table_from_pages(&sgt, pages, index, offset_in_page(buf),
				       count, GFP_KERNEL);
	kfree(pages);
	if (rc)
		return rc;

	rc = fpga_mgr_buf_load_sg(mgr, info, &sgt);
	sg_free_table(&sgt);

	return rc;
}

/**
 * fpga_mgr_firmware_load - request firmware and load to fpga
 * @mgr:	fpga manager
 * @info:	fpga image specific information
 * @image_name:	name of image file on the firmware search path
 *
 * Request an FPGA image using the firmware class, then write out to the FPGA.
 * Update the state before each step to provide info on what step failed if
 * there is a failure.  This code assumes the caller got the mgr pointer
 * from of_fpga_mgr_get() or fpga_mgr_get() and checked that it is not an error
 * code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int fpga_mgr_firmware_load(struct fpga_manager *mgr,
				  struct fpga_image_info *info,
				  const char *image_name)
{
	struct device *dev = &mgr->dev;
	const struct firmware *fw;
	int ret;

	dev_info(dev, "writing %s to %s\n", image_name, mgr->name);

	mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ;

	ret = request_firmware(&fw, image_name, dev);
	if (ret) {
		mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ_ERR;
		dev_err(dev, "Error requesting firmware %s\n", image_name);
		return ret;
	}

	ret = fpga_mgr_buf_load(mgr, info, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

/**
 * fpga_mgr_load - load FPGA from scatter/gather table, buffer, or firmware
 * @mgr:	fpga manager
 * @info:	fpga image information.
 *
 * Load the FPGA from an image which is indicated in @info.  If successful, the
 * FPGA ends up in operating mode.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_load(struct fpga_manager *mgr, struct fpga_image_info *info)
{
	if (info->sgt)
		return fpga_mgr_buf_load_sg(mgr, info, info->sgt);
	if (info->buf && info->count)
		return fpga_mgr_buf_load(mgr, info, info->buf, info->count);
	if (info->firmware_name)
		return fpga_mgr_firmware_load(mgr, info, info->firmware_name);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fpga_mgr_load);

static const char * const state_str[] = {
	[FPGA_MGR_STATE_UNKNOWN] =		"unknown",
	[FPGA_MGR_STATE_POWER_OFF] =		"power off",
	[FPGA_MGR_STATE_POWER_UP] =		"power up",
	[FPGA_MGR_STATE_RESET] =		"reset",

	/* requesting FPGA image from firmware */
	[FPGA_MGR_STATE_FIRMWARE_REQ] =		"firmware request",
	[FPGA_MGR_STATE_FIRMWARE_REQ_ERR] =	"firmware request error",

	/* Preparing FPGA to receive image */
	[FPGA_MGR_STATE_WRITE_INIT] =		"write init",
	[FPGA_MGR_STATE_WRITE_INIT_ERR] =	"write init error",

	/* Writing image to FPGA */
	[FPGA_MGR_STATE_WRITE] =		"write",
	[FPGA_MGR_STATE_WRITE_ERR] =		"write error",

	/* Finishing configuration after image has been written */
	[FPGA_MGR_STATE_WRITE_COMPLETE] =	"write complete",
	[FPGA_MGR_STATE_WRITE_COMPLETE_ERR] =	"write complete error",

	/* FPGA reports to be in normal operating mode */
	[FPGA_MGR_STATE_OPERATING] =		"operating",
};

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%s\n", mgr->name);
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%s\n", state_str[mgr->state]);
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	u64 status;
	int len = 0;

	if (!mgr->mops->status)
		return -ENOENT;

	status = mgr->mops->status(mgr);

	if (status & FPGA_MGR_STATUS_OPERATION_ERR)
		len += sprintf(buf + len, "reconfig operation error\n");
	if (status & FPGA_MGR_STATUS_CRC_ERR)
		len += sprintf(buf + len, "reconfig CRC error\n");
	if (status & FPGA_MGR_STATUS_INCOMPATIBLE_IMAGE_ERR)
		len += sprintf(buf + len, "reconfig incompatible image\n");
	if (status & FPGA_MGR_STATUS_IP_PROTOCOL_ERR)
		len += sprintf(buf + len, "reconfig IP protocol error\n");
	if (status & FPGA_MGR_STATUS_FIFO_OVERFLOW_ERR)
		len += sprintf(buf + len, "reconfig fifo overflow error\n");

	return len;
}

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RO(status);

static struct attribute *fpga_mgr_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_mgr);

static struct fpga_manager *__fpga_mgr_get(struct device *dev)
{
	struct fpga_manager *mgr;

	mgr = to_fpga_manager(dev);

	if (!try_module_get(dev->parent->driver->owner))
		goto err_dev;

	return mgr;

err_dev:
	put_device(dev);
	return ERR_PTR(-ENODEV);
}

static int fpga_mgr_dev_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

/**
 * fpga_mgr_get - Given a device, get a reference to a fpga mgr.
 * @dev:	parent device that fpga mgr was registered with
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *fpga_mgr_get(struct device *dev)
{
	struct device *mgr_dev = class_find_device(fpga_mgr_class, NULL, dev,
						   fpga_mgr_dev_match);
	if (!mgr_dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(mgr_dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_get);

static int fpga_mgr_of_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

/**
 * of_fpga_mgr_get - Given a device node, get a reference to a fpga mgr.
 *
 * @node:	device node
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *of_fpga_mgr_get(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device(fpga_mgr_class, NULL, node,
				fpga_mgr_of_node_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(dev);
}
EXPORT_SYMBOL_GPL(of_fpga_mgr_get);

/**
 * fpga_mgr_put - release a reference to a fpga manager
 * @mgr:	fpga manager structure
 */
void fpga_mgr_put(struct fpga_manager *mgr)
{
	module_put(mgr->dev.parent->driver->owner);
	put_device(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_put);

/**
 * fpga_mgr_lock - Lock FPGA manager for exclusive use
 * @mgr:	fpga manager
 *
 * Given a pointer to FPGA Manager (from fpga_mgr_get() or
 * of_fpga_mgr_put()) attempt to get the mutex. The user should call
 * fpga_mgr_lock() and verify that it returns 0 before attempting to
 * program the FPGA.  Likewise, the user should call fpga_mgr_unlock
 * when done programming the FPGA.
 *
 * Return: 0 for success or -EBUSY
 */
int fpga_mgr_lock(struct fpga_manager *mgr)
{
	if (!mutex_trylock(&mgr->ref_mutex)) {
		dev_err(&mgr->dev, "FPGA manager is in use.\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_mgr_lock);

/**
 * fpga_mgr_unlock - Unlock FPGA manager after done programming
 * @mgr:	fpga manager
 */
void fpga_mgr_unlock(struct fpga_manager *mgr)
{
	mutex_unlock(&mgr->ref_mutex);
}
EXPORT_SYMBOL_GPL(fpga_mgr_unlock);

/**
 * fpga_mgr_create - create and initialize a FPGA manager struct
 * @dev:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * Return: pointer to struct fpga_manager or NULL
 */
struct fpga_manager *fpga_mgr_create(struct device *dev, const char *name,
				     const struct fpga_manager_ops *mops,
				     void *priv)
{
	struct fpga_manager *mgr;
	int id, ret;

	if (!mops || !mops->write_complete || !mops->state ||
	    !mops->write_init || (!mops->write && !mops->write_sg) ||
	    (mops->write && mops->write_sg)) {
		dev_err(dev, "Attempt to register without fpga_manager_ops\n");
		return NULL;
	}

	if (!name || !strlen(name)) {
		dev_err(dev, "Attempt to register with no name!\n");
		return NULL;
	}

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	id = ida_simple_get(&fpga_mgr_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto error_kfree;
	}

	mutex_init(&mgr->ref_mutex);

	mgr->name = name;
	mgr->mops = mops;
	mgr->priv = priv;

	device_initialize(&mgr->dev);
	mgr->dev.class = fpga_mgr_class;
	mgr->dev.groups = mops->groups;
	mgr->dev.parent = dev;
	mgr->dev.of_node = dev->of_node;
	mgr->dev.id = id;

	ret = dev_set_name(&mgr->dev, "fpga%d", id);
	if (ret)
		goto error_device;

	return mgr;

error_device:
	ida_simple_remove(&fpga_mgr_ida, id);
error_kfree:
	kfree(mgr);

	return NULL;
}
EXPORT_SYMBOL_GPL(fpga_mgr_create);

/**
 * fpga_mgr_free - deallocate a FPGA manager
 * @mgr:	fpga manager struct created by fpga_mgr_create
 */
void fpga_mgr_free(struct fpga_manager *mgr)
{
	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);
	kfree(mgr);
}
EXPORT_SYMBOL_GPL(fpga_mgr_free);

/**
 * fpga_mgr_register - register a FPGA manager
 * @mgr:	fpga manager struct created by fpga_mgr_create
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_register(struct fpga_manager *mgr)
{
	int ret;

	/*
	 * Initialize framework state by requesting low level driver read state
	 * from device.  FPGA may be in reset mode or may have been programmed
	 * by bootloader or EEPROM.
	 */
	mgr->state = mgr->mops->state(mgr);

	ret = device_add(&mgr->dev);
	if (ret)
		goto error_device;

	dev_info(&mgr->dev, "%s registered\n", mgr->name);

	return 0;

error_device:
	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_register);

/**
 * fpga_mgr_unregister - unregister and free a FPGA manager
 * @mgr:	fpga manager struct
 */
void fpga_mgr_unregister(struct fpga_manager *mgr)
{
	dev_info(&mgr->dev, "%s %s\n", __func__, mgr->name);

	/*
	 * If the low level driver provides a method for putting fpga into
	 * a desired state upon unregister, do it.
	 */
	if (mgr->mops->fpga_remove)
		mgr->mops->fpga_remove(mgr);

	device_unregister(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_unregister);

static void fpga_mgr_dev_release(struct device *dev)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	fpga_mgr_free(mgr);
}

static int __init fpga_mgr_class_init(void)
{
	pr_info("FPGA manager framework\n");

	fpga_mgr_class = class_create(THIS_MODULE, "fpga_manager");
	if (IS_ERR(fpga_mgr_class))
		return PTR_ERR(fpga_mgr_class);

	fpga_mgr_class->dev_groups = fpga_mgr_groups;
	fpga_mgr_class->dev_release = fpga_mgr_dev_release;

	return 0;
}

static void __exit fpga_mgr_class_exit(void)
{
	class_destroy(fpga_mgr_class);
	ida_destroy(&fpga_mgr_ida);
}

MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_DESCRIPTION("FPGA manager framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_class_init);
module_exit(fpga_mgr_class_exit);
