/*
 * FPGA Manager Core
 *
 *  Copyright (C) 2013-2015 Altera Corporation
 *
 * With code from the mailing list:
 * Copyright (C) 2013 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/firmware.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/slab.h>

static DEFINE_IDA(fpga_mgr_ida);
static struct class *fpga_mgr_class;

/**
 * fpga_mgr_buf_load - load fpga from image in buffer
 * @mgr:	fpga manager
 * @info:	fpga image specific information
 * @buf:	buffer contain fpga image
 * @count:	byte count of buf
 *
 * Step the low level fpga manager through the device-specific steps of getting
 * an FPGA ready to be configured, writing the image to it, then doing whatever
 * post-configuration steps necessary.  This code assumes the caller got the
 * mgr pointer from of_fpga_mgr_get() or fpga_mgr_get() and checked that it is
 * not an error code.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_buf_load(struct fpga_manager *mgr, struct fpga_image_info *info,
		      const char *buf, size_t count)
{
	struct device *dev = &mgr->dev;
	int ret;

	/*
	 * Call the low level driver's write_init function.  This will do the
	 * device-specific things to get the FPGA into the state where it is
	 * ready to receive an FPGA image. The low level driver only gets to
	 * see the first initial_header_size bytes in the buffer.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE_INIT;
	ret = mgr->mops->write_init(mgr, info, buf,
				    min(mgr->mops->initial_header_size, count));
	if (ret) {
		dev_err(dev, "Error preparing FPGA for writing\n");
		mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
		return ret;
	}

	/*
	 * Write the FPGA image to the FPGA.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE;
	ret = mgr->mops->write(mgr, buf, count);
	if (ret) {
		dev_err(dev, "Error while writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	/*
	 * After all the FPGA image has been written, do the device specific
	 * steps to finish and set the FPGA into operating mode.
	 */
	mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
	ret = mgr->mops->write_complete(mgr, info);
	if (ret) {
		dev_err(dev, "Error after writing image data to FPGA\n");
		mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE_ERR;
		return ret;
	}
	mgr->state = FPGA_MGR_STATE_OPERATING;

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_mgr_buf_load);

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
int fpga_mgr_firmware_load(struct fpga_manager *mgr,
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
EXPORT_SYMBOL_GPL(fpga_mgr_firmware_load);

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

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RO(state);

static struct attribute *fpga_mgr_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_mgr);

struct fpga_manager *__fpga_mgr_get(struct device *dev)
{
	struct fpga_manager *mgr;
	int ret = -ENODEV;

	mgr = to_fpga_manager(dev);
	if (!mgr)
		goto err_dev;

	/* Get exclusive use of fpga manager */
	if (!mutex_trylock(&mgr->ref_mutex)) {
		ret = -EBUSY;
		goto err_dev;
	}

	if (!try_module_get(dev->parent->driver->owner))
		goto err_ll_mod;

	return mgr;

err_ll_mod:
	mutex_unlock(&mgr->ref_mutex);
err_dev:
	put_device(dev);
	return ERR_PTR(ret);
}

static int fpga_mgr_dev_match(struct device *dev, const void *data)
{
	return dev->parent == data;
}

/**
 * fpga_mgr_get - get an exclusive reference to a fpga mgr
 * @dev:	parent device that fpga mgr was registered with
 *
 * Given a device, get an exclusive reference to a fpga mgr.
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
 * of_fpga_mgr_get - get an exclusive reference to a fpga mgr
 * @node:	device node
 *
 * Given a device node, get an exclusive reference to a fpga mgr.
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
	mutex_unlock(&mgr->ref_mutex);
	put_device(&mgr->dev);
}
EXPORT_SYMBOL_GPL(fpga_mgr_put);

/**
 * fpga_mgr_register - register a low level fpga manager driver
 * @dev:	fpga manager device from pdev
 * @name:	fpga manager name
 * @mops:	pointer to structure of fpga manager ops
 * @priv:	fpga manager private data
 *
 * Return: 0 on success, negative error code otherwise.
 */
int fpga_mgr_register(struct device *dev, const char *name,
		      const struct fpga_manager_ops *mops,
		      void *priv)
{
	struct fpga_manager *mgr;
	int id, ret;

	if (!mops || !mops->write_init || !mops->write ||
	    !mops->write_complete || !mops->state) {
		dev_err(dev, "Attempt to register without fpga_manager_ops\n");
		return -EINVAL;
	}

	if (!name || !strlen(name)) {
		dev_err(dev, "Attempt to register with no name!\n");
		return -EINVAL;
	}

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;

	id = ida_simple_get(&fpga_mgr_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto error_kfree;
	}

	mutex_init(&mgr->ref_mutex);

	mgr->name = name;
	mgr->mops = mops;
	mgr->priv = priv;

	/*
	 * Initialize framework state by requesting low level driver read state
	 * from device.  FPGA may be in reset mode or may have been programmed
	 * by bootloader or EEPROM.
	 */
	mgr->state = mgr->mops->state(mgr);

	device_initialize(&mgr->dev);
	mgr->dev.class = fpga_mgr_class;
	mgr->dev.parent = dev;
	mgr->dev.of_node = dev->of_node;
	mgr->dev.id = id;
	dev_set_drvdata(dev, mgr);

	ret = dev_set_name(&mgr->dev, "fpga%d", id);
	if (ret)
		goto error_device;

	ret = device_add(&mgr->dev);
	if (ret)
		goto error_device;

	dev_info(&mgr->dev, "%s registered\n", mgr->name);

	return 0;

error_device:
	ida_simple_remove(&fpga_mgr_ida, id);
error_kfree:
	kfree(mgr);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_register);

/**
 * fpga_mgr_unregister - unregister a low level fpga manager driver
 * @dev:	fpga manager device from pdev
 */
void fpga_mgr_unregister(struct device *dev)
{
	struct fpga_manager *mgr = dev_get_drvdata(dev);

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

	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);
	kfree(mgr);
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

MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_DESCRIPTION("FPGA manager framework");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_class_init);
module_exit(fpga_mgr_class_exit);
