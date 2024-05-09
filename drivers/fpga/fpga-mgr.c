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
static const struct class fpga_mgr_class;

struct fpga_mgr_devres {
	struct fpga_manager *mgr;
};

static inline void fpga_mgr_fpga_remove(struct fpga_manager *mgr)
{
	if (mgr->mops->fpga_remove)
		mgr->mops->fpga_remove(mgr);
}

static inline enum fpga_mgr_states fpga_mgr_state(struct fpga_manager *mgr)
{
	if (mgr->mops->state)
		return  mgr->mops->state(mgr);
	return FPGA_MGR_STATE_UNKNOWN;
}

static inline u64 fpga_mgr_status(struct fpga_manager *mgr)
{
	if (mgr->mops->status)
		return mgr->mops->status(mgr);
	return 0;
}

static inline int fpga_mgr_write(struct fpga_manager *mgr, const char *buf, size_t count)
{
	if (mgr->mops->write)
		return  mgr->mops->write(mgr, buf, count);
	return -EOPNOTSUPP;
}

/*
 * After all the FPGA image has been written, do the device specific steps to
 * finish and set the FPGA into operating mode.
 */
static inline int fpga_mgr_write_complete(struct fpga_manager *mgr,
					  struct fpga_image_info *info)
{
	int ret = 0;

	mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
	if (mgr->mops->write_complete)
		ret = mgr->mops->write_complete(mgr, info);
	if (ret) {
		dev_err(&mgr->dev, "Error after writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE_ERR;
		return ret;
	}
	mgr->state = FPGA_MGR_STATE_OPERATING;

	return 0;
}

static inline int fpga_mgr_parse_header(struct fpga_manager *mgr,
					struct fpga_image_info *info,
					const char *buf, size_t count)
{
	if (mgr->mops->parse_header)
		return mgr->mops->parse_header(mgr, info, buf, count);
	return 0;
}

static inline int fpga_mgr_write_init(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      const char *buf, size_t count)
{
	if (mgr->mops->write_init)
		return  mgr->mops->write_init(mgr, info, buf, count);
	return 0;
}

static inline int fpga_mgr_write_sg(struct fpga_manager *mgr,
				    struct sg_table *sgt)
{
	if (mgr->mops->write_sg)
		return  mgr->mops->write_sg(mgr, sgt);
	return -EOPNOTSUPP;
}

/**
 * fpga_image_info_alloc - Allocate an FPGA image info struct
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
 * fpga_image_info_free - Free an FPGA image info struct
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
 * Call the low level driver's parse_header function with entire FPGA image
 * buffer on the input. This will set info->header_size and info->data_size.
 */
static int fpga_mgr_parse_header_mapped(struct fpga_manager *mgr,
					struct fpga_image_info *info,
					const char *buf, size_t count)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_PARSE_HEADER;
	ret = fpga_mgr_parse_header(mgr, info, buf, count);

	if (info->header_size + info->data_size > count) {
		dev_err(&mgr->dev, "Bitstream data outruns FPGA image\n");
		ret = -EINVAL;
	}

	if (ret) {
		dev_err(&mgr->dev, "Error while parsing FPGA image header\n");
		mgr->state = FPGA_MGR_STATE_PARSE_HEADER_ERR;
	}

	return ret;
}

/*
 * Call the low level driver's parse_header function with first fragment of
 * scattered FPGA image on the input. If header fits first fragment,
 * parse_header will set info->header_size and info->data_size. If it is not,
 * parse_header will set desired size to info->header_size and -EAGAIN will be
 * returned.
 */
static int fpga_mgr_parse_header_sg_first(struct fpga_manager *mgr,
					  struct fpga_image_info *info,
					  struct sg_table *sgt)
{
	struct sg_mapping_iter miter;
	int ret;

	mgr->state = FPGA_MGR_STATE_PARSE_HEADER;

	sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
	if (sg_miter_next(&miter) &&
	    miter.length >= info->header_size)
		ret = fpga_mgr_parse_header(mgr, info, miter.addr, miter.length);
	else
		ret = -EAGAIN;
	sg_miter_stop(&miter);

	if (ret && ret != -EAGAIN) {
		dev_err(&mgr->dev, "Error while parsing FPGA image header\n");
		mgr->state = FPGA_MGR_STATE_PARSE_HEADER_ERR;
	}

	return ret;
}

/*
 * Copy scattered FPGA image fragments to temporary buffer and call the
 * low level driver's parse_header function. This should be called after
 * fpga_mgr_parse_header_sg_first() returned -EAGAIN. In case of success,
 * pointer to the newly allocated image header copy will be returned and
 * its size will be set into *ret_size. Returned buffer needs to be freed.
 */
static void *fpga_mgr_parse_header_sg(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      struct sg_table *sgt, size_t *ret_size)
{
	size_t len, new_header_size, header_size = 0;
	char *new_buf, *buf = NULL;
	int ret;

	do {
		new_header_size = info->header_size;
		if (new_header_size <= header_size) {
			dev_err(&mgr->dev, "Requested invalid header size\n");
			ret = -EFAULT;
			break;
		}

		new_buf = krealloc(buf, new_header_size, GFP_KERNEL);
		if (!new_buf) {
			ret = -ENOMEM;
			break;
		}

		buf = new_buf;

		len = sg_pcopy_to_buffer(sgt->sgl, sgt->nents,
					 buf + header_size,
					 new_header_size - header_size,
					 header_size);
		if (len != new_header_size - header_size) {
			ret = -EFAULT;
			break;
		}

		header_size = new_header_size;
		ret = fpga_mgr_parse_header(mgr, info, buf, header_size);
	} while (ret == -EAGAIN);

	if (ret) {
		dev_err(&mgr->dev, "Error while parsing FPGA image header\n");
		mgr->state = FPGA_MGR_STATE_PARSE_HEADER_ERR;
		kfree(buf);
		buf = ERR_PTR(ret);
	}

	*ret_size = header_size;

	return buf;
}

/*
 * Call the low level driver's write_init function. This will do the
 * device-specific things to get the FPGA into the state where it is ready to
 * receive an FPGA image. The low level driver gets to see at least first
 * info->header_size bytes in the buffer. If info->header_size is 0,
 * write_init will not get any bytes of image buffer.
 */
static int fpga_mgr_write_init_buf(struct fpga_manager *mgr,
				   struct fpga_image_info *info,
				   const char *buf, size_t count)
{
	size_t header_size = info->header_size;
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE_INIT;

	if (header_size > count)
		ret = -EINVAL;
	else if (!header_size)
		ret = fpga_mgr_write_init(mgr, info, NULL, 0);
	else
		ret = fpga_mgr_write_init(mgr, info, buf, count);

	if (ret) {
		dev_err(&mgr->dev, "Error preparing FPGA for writing\n");
		mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
		return ret;
	}

	return 0;
}

static int fpga_mgr_prepare_sg(struct fpga_manager *mgr,
			       struct fpga_image_info *info,
			       struct sg_table *sgt)
{
	struct sg_mapping_iter miter;
	size_t len;
	char *buf;
	int ret;

	/* Short path. Low level driver don't care about image header. */
	if (!mgr->mops->initial_header_size && !mgr->mops->parse_header)
		return fpga_mgr_write_init_buf(mgr, info, NULL, 0);

	/*
	 * First try to use miter to map the first fragment to access the
	 * header, this is the typical path.
	 */
	ret = fpga_mgr_parse_header_sg_first(mgr, info, sgt);
	/* If 0, header fits first fragment, call write_init on it */
	if (!ret) {
		sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);
		if (sg_miter_next(&miter)) {
			ret = fpga_mgr_write_init_buf(mgr, info, miter.addr,
						      miter.length);
			sg_miter_stop(&miter);
			return ret;
		}
		sg_miter_stop(&miter);
	/*
	 * If -EAGAIN, more sg buffer is needed,
	 * otherwise an error has occurred.
	 */
	} else if (ret != -EAGAIN) {
		return ret;
	}

	/*
	 * Copy the fragments into temporary memory.
	 * Copying is done inside fpga_mgr_parse_header_sg().
	 */
	buf = fpga_mgr_parse_header_sg(mgr, info, sgt, &len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = fpga_mgr_write_init_buf(mgr, info, buf, len);

	kfree(buf);

	return ret;
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

	ret = fpga_mgr_prepare_sg(mgr, info, sgt);
	if (ret)
		return ret;

	/* Write the FPGA image to the FPGA. */
	mgr->state = FPGA_MGR_STATE_WRITE;
	if (mgr->mops->write_sg) {
		ret = fpga_mgr_write_sg(mgr, sgt);
	} else {
		size_t length, count = 0, data_size = info->data_size;
		struct sg_mapping_iter miter;

		sg_miter_start(&miter, sgt->sgl, sgt->nents, SG_MITER_FROM_SG);

		if (mgr->mops->skip_header &&
		    !sg_miter_skip(&miter, info->header_size)) {
			ret = -EINVAL;
			goto out;
		}

		while (sg_miter_next(&miter)) {
			if (data_size)
				length = min(miter.length, data_size - count);
			else
				length = miter.length;

			ret = fpga_mgr_write(mgr, miter.addr, length);
			if (ret)
				break;

			count += length;
			if (data_size && count >= data_size)
				break;
		}
		sg_miter_stop(&miter);
	}

out:
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

	ret = fpga_mgr_parse_header_mapped(mgr, info, buf, count);
	if (ret)
		return ret;

	ret = fpga_mgr_write_init_buf(mgr, info, buf, count);
	if (ret)
		return ret;

	if (mgr->mops->skip_header) {
		buf += info->header_size;
		count -= info->header_size;
	}

	if (info->data_size)
		count = info->data_size;

	/*
	 * Write the FPGA image to the FPGA.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE;
	ret = fpga_mgr_write(mgr, buf, count);
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
	info->header_size = mgr->mops->initial_header_size;

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

	/* Parse FPGA image header */
	[FPGA_MGR_STATE_PARSE_HEADER] =		"parse header",
	[FPGA_MGR_STATE_PARSE_HEADER_ERR] =	"parse header error",

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

	status = fpga_mgr_status(mgr);

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
 * fpga_mgr_get - Given a device, get a reference to an fpga mgr.
 * @dev:	parent device that fpga mgr was registered with
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *fpga_mgr_get(struct device *dev)
{
	struct device *mgr_dev = class_find_device(&fpga_mgr_class, NULL, dev,
						   fpga_mgr_dev_match);
	if (!mgr_dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(mgr_dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_get);

/**
 * of_fpga_mgr_get - Given a device node, get a reference to an fpga mgr.
 *
 * @node:	device node
 *
 * Return: fpga manager struct or IS_ERR() condition containing error code.
 */
struct fpga_manager *of_fpga_mgr_get(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device_by_of_node(&fpga_mgr_class, node);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return __fpga_mgr_get(dev);
}
EXPORT_SYMBOL_GPL(of_fpga_mgr_get);

/**
 * fpga_mgr_put - release a reference to an fpga manager
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
 * fpga_mgr_register_full - create and register an FPGA Manager device
 * @parent:	fpga manager device from pdev
 * @info:	parameters for fpga manager
 *
 * The caller of this function is responsible for calling fpga_mgr_unregister().
 * Using devm_fpga_mgr_register_full() instead is recommended.
 *
 * Return: pointer to struct fpga_manager pointer or ERR_PTR()
 */
struct fpga_manager *
fpga_mgr_register_full(struct device *parent, const struct fpga_manager_info *info)
{
	const struct fpga_manager_ops *mops = info->mops;
	struct fpga_manager *mgr;
	int id, ret;

	if (!mops) {
		dev_err(parent, "Attempt to register without fpga_manager_ops\n");
		return ERR_PTR(-EINVAL);
	}

	if (!info->name || !strlen(info->name)) {
		dev_err(parent, "Attempt to register with no name!\n");
		return ERR_PTR(-EINVAL);
	}

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&fpga_mgr_ida, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto error_kfree;
	}

	mutex_init(&mgr->ref_mutex);

	mgr->name = info->name;
	mgr->mops = info->mops;
	mgr->priv = info->priv;
	mgr->compat_id = info->compat_id;

	mgr->dev.class = &fpga_mgr_class;
	mgr->dev.groups = mops->groups;
	mgr->dev.parent = parent;
	mgr->dev.of_node = parent->of_node;
	mgr->dev.id = id;

	ret = dev_set_name(&mgr->dev, "fpga%d", id);
	if (ret)
		goto error_device;

	/*
	 * Initialize framework state by requesting low level driver read state
	 * from device.  FPGA may be in reset mode or may have been programmed
	 * by bootloader or EEPROM.
	 */
	mgr->state = fpga_mgr_state(mgr);

	ret = device_register(&mgr->dev);
	if (ret) {
		put_device(&mgr->dev);
		return ERR_PTR(ret);
	}

	return mgr;

error_device:
	ida_free(&fpga_mgr_ida, id);
error_kfree:
	kfree(mgr);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(fpga_mgr_register_full);

/**
 * fpga_mgr_register - create and register an FPGA Manager device
 * @parent:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * The caller of this function is responsible for calling fpga_mgr_unregister().
 * Using devm_fpga_mgr_register() instead is recommended. This simple
 * version of the register function should be sufficient for most users. The
 * fpga_mgr_register_full() function is available for users that need to pass
 * additional, optional parameters.
 *
 * Return: pointer to struct fpga_manager pointer or ERR_PTR()
 */
struct fpga_manager *
fpga_mgr_register(struct device *parent, const char *name,
		  const struct fpga_manager_ops *mops, void *priv)
{
	struct fpga_manager_info info = { 0 };

	info.name = name;
	info.mops = mops;
	info.priv = priv;

	return fpga_mgr_register_full(parent, &info);
}
EXPORT_SYMBOL_GPL(fpga_mgr_register);

/**
 * fpga_mgr_unregister - unregister an FPGA manager
 * @mgr: fpga manager struct
 *
 * This function is intended for use in an FPGA manager driver's remove function.
 */
void fpga_mgr_unregister(struct fpga_manager *mgr)
{
	dev_info(&mgr->dev, "%s %s\n", __func__, mgr->name);

	/*
	 * If the low level driver provides a method for putting fpga into
	 * a desired state upon unregister, do it.
	 */
	fpga_mgr_fpga_remove(mgr);

	device_unregister(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_unregister);

static void devm_fpga_mgr_unregister(struct device *dev, void *res)
{
	struct fpga_mgr_devres *dr = res;

	fpga_mgr_unregister(dr->mgr);
}

/**
 * devm_fpga_mgr_register_full - resource managed variant of fpga_mgr_register()
 * @parent:	fpga manager device from pdev
 * @info:	parameters for fpga manager
 *
 * Return:  fpga manager pointer on success, negative error code otherwise.
 *
 * This is the devres variant of fpga_mgr_register_full() for which the unregister
 * function will be called automatically when the managing device is detached.
 */
struct fpga_manager *
devm_fpga_mgr_register_full(struct device *parent, const struct fpga_manager_info *info)
{
	struct fpga_mgr_devres *dr;
	struct fpga_manager *mgr;

	dr = devres_alloc(devm_fpga_mgr_unregister, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return ERR_PTR(-ENOMEM);

	mgr = fpga_mgr_register_full(parent, info);
	if (IS_ERR(mgr)) {
		devres_free(dr);
		return mgr;
	}

	dr->mgr = mgr;
	devres_add(parent, dr);

	return mgr;
}
EXPORT_SYMBOL_GPL(devm_fpga_mgr_register_full);

/**
 * devm_fpga_mgr_register - resource managed variant of fpga_mgr_register()
 * @parent:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * Return:  fpga manager pointer on success, negative error code otherwise.
 *
 * This is the devres variant of fpga_mgr_register() for which the
 * unregister function will be called automatically when the managing
 * device is detached.
 */
struct fpga_manager *
devm_fpga_mgr_register(struct device *parent, const char *name,
		       const struct fpga_manager_ops *mops, void *priv)
{
	struct fpga_manager_info info = { 0 };

	info.name = name;
	info.mops = mops;
	info.priv = priv;

	return devm_fpga_mgr_register_full(parent, &info);
}
EXPORT_SYMBOL_GPL(devm_fpga_mgr_register);

static void fpga_mgr_dev_release(struct device *dev)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	ida_free(&fpga_mgr_ida, mgr->dev.id);
	kfree(mgr);
}

static const struct class fpga_mgr_class = {
	.name = "fpga_manager",
	.dev_groups = fpga_mgr_groups,
	.dev_release = fpga_mgr_dev_release,
};

static int __init fpga_mgr_class_init(void)
{
	pr_info("FPGA manager framework\n");

	return class_register(&fpga_mgr_class);
}

static void __exit fpga_mgr_class_exit(void)
{
	class_unregister(&fpga_mgr_class);
	ida_destroy(&fpga_mgr_ida);
}

MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_DESCRIPTION("FPGA manager framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_class_init);
module_exit(fpga_mgr_class_exit);
