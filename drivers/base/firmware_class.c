/*
 * firmware_class.c - Multi purpose firmware loading support
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * Please see Documentation/firmware_class/ for more information.
 *
 */

#include <linux/capability.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/kthread.h>

#include <linux/firmware.h>
#include "base.h"

#define to_dev(obj) container_of(obj, struct device, kobj)

MODULE_AUTHOR("Manuel Estrada Sainz");
MODULE_DESCRIPTION("Multi purpose firmware loading support");
MODULE_LICENSE("GPL");

enum {
	FW_STATUS_LOADING,
	FW_STATUS_DONE,
	FW_STATUS_ABORT,
};

static int loading_timeout = 60;	/* In seconds */

/* fw_lock could be moved to 'struct firmware_priv' but since it is just
 * guarding for corner cases a global lock should be OK */
static DEFINE_MUTEX(fw_lock);

struct firmware_priv {
	char fw_id[FIRMWARE_NAME_MAX];
	struct completion completion;
	struct bin_attribute attr_data;
	struct firmware *fw;
	unsigned long status;
	int alloc_size;
	struct timer_list timeout;
};

static void
fw_load_abort(struct firmware_priv *fw_priv)
{
	set_bit(FW_STATUS_ABORT, &fw_priv->status);
	wmb();
	complete(&fw_priv->completion);
}

static ssize_t
firmware_timeout_show(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", loading_timeout);
}

/**
 * firmware_timeout_store - set number of seconds to wait for firmware
 * @class: device class pointer
 * @buf: buffer to scan for timeout value
 * @count: number of bytes in @buf
 *
 *	Sets the number of seconds to wait for the firmware.  Once
 *	this expires an error will be returned to the driver and no
 *	firmware will be provided.
 *
 *	Note: zero means 'wait forever'.
 **/
static ssize_t
firmware_timeout_store(struct class *class, const char *buf, size_t count)
{
	loading_timeout = simple_strtol(buf, NULL, 10);
	if (loading_timeout < 0)
		loading_timeout = 0;
	return count;
}

static CLASS_ATTR(timeout, 0644, firmware_timeout_show, firmware_timeout_store);

static void fw_dev_release(struct device *dev);

static int firmware_uevent(struct device *dev, char **envp, int num_envp,
			   char *buffer, int buffer_size)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int i = 0, len = 0;

	if (add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &len,
			   "FIRMWARE=%s", fw_priv->fw_id))
		return -ENOMEM;
	if (add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &len,
			   "TIMEOUT=%i", loading_timeout))
		return -ENOMEM;
	envp[i] = NULL;

	return 0;
}

static struct class firmware_class = {
	.name		= "firmware",
	.dev_uevent	= firmware_uevent,
	.dev_release	= fw_dev_release,
};

static ssize_t firmware_loading_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int loading = test_bit(FW_STATUS_LOADING, &fw_priv->status);
	return sprintf(buf, "%d\n", loading);
}

/**
 * firmware_loading_store - set value in the 'loading' control file
 * @dev: device pointer
 * @attr: device attribute pointer
 * @buf: buffer to scan for loading control value
 * @count: number of bytes in @buf
 *
 *	The relevant values are:
 *
 *	 1: Start a load, discarding any previous partial load.
 *	 0: Conclude the load and hand the data to the driver code.
 *	-1: Conclude the load with an error and discard any written data.
 **/
static ssize_t firmware_loading_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int loading = simple_strtol(buf, NULL, 10);

	switch (loading) {
	case 1:
		mutex_lock(&fw_lock);
		if (!fw_priv->fw) {
			mutex_unlock(&fw_lock);
			break;
		}
		vfree(fw_priv->fw->data);
		fw_priv->fw->data = NULL;
		fw_priv->fw->size = 0;
		fw_priv->alloc_size = 0;
		set_bit(FW_STATUS_LOADING, &fw_priv->status);
		mutex_unlock(&fw_lock);
		break;
	case 0:
		if (test_bit(FW_STATUS_LOADING, &fw_priv->status)) {
			complete(&fw_priv->completion);
			clear_bit(FW_STATUS_LOADING, &fw_priv->status);
			break;
		}
		/* fallthrough */
	default:
		printk(KERN_ERR "%s: unexpected value (%d)\n", __FUNCTION__,
		       loading);
		/* fallthrough */
	case -1:
		fw_load_abort(fw_priv);
		break;
	}

	return count;
}

static DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

static ssize_t
firmware_data_read(struct kobject *kobj, struct bin_attribute *bin_attr,
		   char *buffer, loff_t offset, size_t count)
{
	struct device *dev = to_dev(kobj);
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	struct firmware *fw;
	ssize_t ret_count = count;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		ret_count = -ENODEV;
		goto out;
	}
	if (offset > fw->size) {
		ret_count = 0;
		goto out;
	}
	if (offset + ret_count > fw->size)
		ret_count = fw->size - offset;

	memcpy(buffer, fw->data + offset, ret_count);
out:
	mutex_unlock(&fw_lock);
	return ret_count;
}

static int
fw_realloc_buffer(struct firmware_priv *fw_priv, int min_size)
{
	u8 *new_data;
	int new_size = fw_priv->alloc_size;

	if (min_size <= fw_priv->alloc_size)
		return 0;

	new_size = ALIGN(min_size, PAGE_SIZE);
	new_data = vmalloc(new_size);
	if (!new_data) {
		printk(KERN_ERR "%s: unable to alloc buffer\n", __FUNCTION__);
		/* Make sure that we don't keep incomplete data */
		fw_load_abort(fw_priv);
		return -ENOMEM;
	}
	fw_priv->alloc_size = new_size;
	if (fw_priv->fw->data) {
		memcpy(new_data, fw_priv->fw->data, fw_priv->fw->size);
		vfree(fw_priv->fw->data);
	}
	fw_priv->fw->data = new_data;
	BUG_ON(min_size > fw_priv->alloc_size);
	return 0;
}

/**
 * firmware_data_write - write method for firmware
 * @kobj: kobject for the device
 * @bin_attr: bin_attr structure
 * @buffer: buffer being written
 * @offset: buffer offset for write in total data store area
 * @count: buffer size
 *
 *	Data written to the 'data' attribute will be later handed to
 *	the driver as a firmware image.
 **/
static ssize_t
firmware_data_write(struct kobject *kobj, struct bin_attribute *bin_attr,
		    char *buffer, loff_t offset, size_t count)
{
	struct device *dev = to_dev(kobj);
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	struct firmware *fw;
	ssize_t retval;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		retval = -ENODEV;
		goto out;
	}
	retval = fw_realloc_buffer(fw_priv, offset + count);
	if (retval)
		goto out;

	memcpy(fw->data + offset, buffer, count);

	fw->size = max_t(size_t, offset + count, fw->size);
	retval = count;
out:
	mutex_unlock(&fw_lock);
	return retval;
}

static struct bin_attribute firmware_attr_data_tmpl = {
	.attr = {.name = "data", .mode = 0644},
	.size = 0,
	.read = firmware_data_read,
	.write = firmware_data_write,
};

static void fw_dev_release(struct device *dev)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);

	kfree(fw_priv);
	kfree(dev);

	module_put(THIS_MODULE);
}

static void
firmware_class_timeout(u_long data)
{
	struct firmware_priv *fw_priv = (struct firmware_priv *) data;
	fw_load_abort(fw_priv);
}

static inline void fw_setup_device_id(struct device *f_dev, struct device *dev)
{
	/* XXX warning we should watch out for name collisions */
	strlcpy(f_dev->bus_id, dev->bus_id, BUS_ID_SIZE);
}

static int fw_register_device(struct device **dev_p, const char *fw_name,
			      struct device *device)
{
	int retval;
	struct firmware_priv *fw_priv = kzalloc(sizeof(*fw_priv),
						GFP_KERNEL);
	struct device *f_dev = kzalloc(sizeof(*f_dev), GFP_KERNEL);

	*dev_p = NULL;

	if (!fw_priv || !f_dev) {
		printk(KERN_ERR "%s: kmalloc failed\n", __FUNCTION__);
		retval = -ENOMEM;
		goto error_kfree;
	}

	init_completion(&fw_priv->completion);
	fw_priv->attr_data = firmware_attr_data_tmpl;
	strlcpy(fw_priv->fw_id, fw_name, FIRMWARE_NAME_MAX);

	fw_priv->timeout.function = firmware_class_timeout;
	fw_priv->timeout.data = (u_long) fw_priv;
	init_timer(&fw_priv->timeout);

	fw_setup_device_id(f_dev, device);
	f_dev->parent = device;
	f_dev->class = &firmware_class;
	dev_set_drvdata(f_dev, fw_priv);
	f_dev->uevent_suppress = 1;
	retval = device_register(f_dev);
	if (retval) {
		printk(KERN_ERR "%s: device_register failed\n",
		       __FUNCTION__);
		goto error_kfree;
	}
	*dev_p = f_dev;
	return 0;

error_kfree:
	kfree(fw_priv);
	kfree(f_dev);
	return retval;
}

static int fw_setup_device(struct firmware *fw, struct device **dev_p,
			   const char *fw_name, struct device *device,
			   int uevent)
{
	struct device *f_dev;
	struct firmware_priv *fw_priv;
	int retval;

	*dev_p = NULL;
	retval = fw_register_device(&f_dev, fw_name, device);
	if (retval)
		goto out;

	/* Need to pin this module until class device is destroyed */
	__module_get(THIS_MODULE);

	fw_priv = dev_get_drvdata(f_dev);

	fw_priv->fw = fw;
	retval = sysfs_create_bin_file(&f_dev->kobj, &fw_priv->attr_data);
	if (retval) {
		printk(KERN_ERR "%s: sysfs_create_bin_file failed\n",
		       __FUNCTION__);
		goto error_unreg;
	}

	retval = device_create_file(f_dev, &dev_attr_loading);
	if (retval) {
		printk(KERN_ERR "%s: device_create_file failed\n",
		       __FUNCTION__);
		goto error_unreg;
	}

	if (uevent)
		f_dev->uevent_suppress = 0;
	*dev_p = f_dev;
	goto out;

error_unreg:
	device_unregister(f_dev);
out:
	return retval;
}

static int
_request_firmware(const struct firmware **firmware_p, const char *name,
		 struct device *device, int uevent)
{
	struct device *f_dev;
	struct firmware_priv *fw_priv;
	struct firmware *firmware;
	int retval;

	if (!firmware_p)
		return -EINVAL;

	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware) {
		printk(KERN_ERR "%s: kmalloc(struct firmware) failed\n",
		       __FUNCTION__);
		retval = -ENOMEM;
		goto out;
	}

	retval = fw_setup_device(firmware, &f_dev, name, device, uevent);
	if (retval)
		goto error_kfree_fw;

	fw_priv = dev_get_drvdata(f_dev);

	if (uevent) {
		if (loading_timeout > 0) {
			fw_priv->timeout.expires = jiffies + loading_timeout * HZ;
			add_timer(&fw_priv->timeout);
		}

		kobject_uevent(&f_dev->kobj, KOBJ_ADD);
		wait_for_completion(&fw_priv->completion);
		set_bit(FW_STATUS_DONE, &fw_priv->status);
		del_timer_sync(&fw_priv->timeout);
	} else
		wait_for_completion(&fw_priv->completion);

	mutex_lock(&fw_lock);
	if (!fw_priv->fw->size || test_bit(FW_STATUS_ABORT, &fw_priv->status)) {
		retval = -ENOENT;
		release_firmware(fw_priv->fw);
		*firmware_p = NULL;
	}
	fw_priv->fw = NULL;
	mutex_unlock(&fw_lock);
	device_unregister(f_dev);
	goto out;

error_kfree_fw:
	kfree(firmware);
	*firmware_p = NULL;
out:
	return retval;
}

/**
 * request_firmware: - send firmware request and wait for it
 * @firmware_p: pointer to firmware image
 * @name: name of firmware file
 * @device: device for which firmware is being loaded
 *
 *      @firmware_p will be used to return a firmware image by the name
 *      of @name for device @device.
 *
 *      Should be called from user context where sleeping is allowed.
 *
 *      @name will be used as $FIRMWARE in the uevent environment and
 *      should be distinctive enough not to be confused with any other
 *      firmware image for this or any other device.
 **/
int
request_firmware(const struct firmware **firmware_p, const char *name,
                 struct device *device)
{
        int uevent = 1;
        return _request_firmware(firmware_p, name, device, uevent);
}

/**
 * release_firmware: - release the resource associated with a firmware image
 * @fw: firmware resource to release
 **/
void
release_firmware(const struct firmware *fw)
{
	if (fw) {
		vfree(fw->data);
		kfree(fw);
	}
}

/* Async support */
struct firmware_work {
	struct work_struct work;
	struct module *module;
	const char *name;
	struct device *device;
	void *context;
	void (*cont)(const struct firmware *fw, void *context);
	int uevent;
};

static int
request_firmware_work_func(void *arg)
{
	struct firmware_work *fw_work = arg;
	const struct firmware *fw;
	int ret;
	if (!arg) {
		WARN_ON(1);
		return 0;
	}
	ret = _request_firmware(&fw, fw_work->name, fw_work->device,
		fw_work->uevent);
	if (ret < 0)
		fw_work->cont(NULL, fw_work->context);
	else {
		fw_work->cont(fw, fw_work->context);
		release_firmware(fw);
	}
	module_put(fw_work->module);
	kfree(fw_work);
	return ret;
}

/**
 * request_firmware_nowait: asynchronous version of request_firmware
 * @module: module requesting the firmware
 * @uevent: sends uevent to copy the firmware image if this flag
 *	is non-zero else the firmware copy must be done manually.
 * @name: name of firmware file
 * @device: device for which firmware is being loaded
 * @context: will be passed over to @cont, and
 *	@fw may be %NULL if firmware request fails.
 * @cont: function will be called asynchronously when the firmware
 *	request is over.
 *
 *	Asynchronous variant of request_firmware() for contexts where
 *	it is not possible to sleep.
 **/
int
request_firmware_nowait(
	struct module *module, int uevent,
	const char *name, struct device *device, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	struct task_struct *task;
	struct firmware_work *fw_work = kmalloc(sizeof (struct firmware_work),
						GFP_ATOMIC);

	if (!fw_work)
		return -ENOMEM;
	if (!try_module_get(module)) {
		kfree(fw_work);
		return -EFAULT;
	}

	*fw_work = (struct firmware_work) {
		.module = module,
		.name = name,
		.device = device,
		.context = context,
		.cont = cont,
		.uevent = uevent,
	};

	task = kthread_run(request_firmware_work_func, fw_work,
			    "firmware/%s", name);

	if (IS_ERR(task)) {
		fw_work->cont(NULL, fw_work->context);
		module_put(fw_work->module);
		kfree(fw_work);
		return PTR_ERR(task);
	}
	return 0;
}

static int __init
firmware_class_init(void)
{
	int error;
	error = class_register(&firmware_class);
	if (error) {
		printk(KERN_ERR "%s: class_register failed\n", __FUNCTION__);
		return error;
	}
	error = class_create_file(&firmware_class, &class_attr_timeout);
	if (error) {
		printk(KERN_ERR "%s: class_create_file failed\n",
		       __FUNCTION__);
		class_unregister(&firmware_class);
	}
	return error;

}
static void __exit
firmware_class_exit(void)
{
	class_unregister(&firmware_class);
}

fs_initcall(firmware_class_init);
module_exit(firmware_class_exit);

EXPORT_SYMBOL(release_firmware);
EXPORT_SYMBOL(request_firmware);
EXPORT_SYMBOL(request_firmware_nowait);
