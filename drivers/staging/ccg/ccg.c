/*
 * Configurable Composite Gadget
 *
 * Initially contributed as "Android Composite Gdaget" by:
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *         Benoit Goby <benoit@android.com>
 *
 * Tailoring it to become a generic Configurable Composite Gadget is
 *
 * Copyright (C) 2012 Samsung Electronics
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/platform_device.h>

#include <linux/usb/ch9.h>
#include "composite.h"
#include <linux/usb/gadget.h>

#include "gadget_chips.h"

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#include "composite.c"

#include "f_mass_storage.c"
#include "u_serial.c"
#include "f_acm.c"
#define USB_ETH_RNDIS y
#include "f_rndis.c"
#include "rndis.c"
#include "u_ether.c"
#include "f_fs.c"

MODULE_AUTHOR("Mike Lockwood, Andrzej Pietrasiewicz");
MODULE_DESCRIPTION("Configurable Composite USB Gadget");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static const char longname[] = "Configurable Composite Gadget";

/* Default vendor and product IDs, overridden by userspace */
#define VENDOR_ID		0x1d6b /* Linux Foundation */
#define PRODUCT_ID		0x0107
#define GFS_MAX_DEVS		10

struct ccg_usb_function {
	char *name;
	void *config;

	struct device *dev;
	char *dev_name;
	struct device_attribute **attributes;

	/* for ccg_dev.enabled_functions */
	struct list_head enabled_list;

	/* Optional: initialization during gadget bind */
	int (*init)(struct ccg_usb_function *, struct usb_composite_dev *);
	/* Optional: cleanup during gadget unbind */
	void (*cleanup)(struct ccg_usb_function *);

	int (*bind_config)(struct ccg_usb_function *,
			   struct usb_configuration *);

	/* Optional: called when the configuration is removed */
	void (*unbind_config)(struct ccg_usb_function *,
			      struct usb_configuration *);
	/* Optional: handle ctrl requests before the device is configured */
	int (*ctrlrequest)(struct ccg_usb_function *,
			   struct usb_composite_dev *,
			   const struct usb_ctrlrequest *);
};

struct ffs_obj {
	const char *name;
	bool mounted;
	bool desc_ready;
	bool used;
	struct ffs_data *ffs_data;
};

struct ccg_dev {
	struct ccg_usb_function **functions;
	struct list_head enabled_functions;
	struct usb_composite_dev *cdev;
	struct device *dev;

	bool enabled;
	struct mutex mutex;
	bool connected;
	bool sw_connected;
	struct work_struct work;

	unsigned int max_func_num;
	unsigned int func_num;
	struct ffs_obj ffs_tab[GFS_MAX_DEVS];
};

static struct class *ccg_class;
static struct ccg_dev *_ccg_dev;
static int ccg_bind_config(struct usb_configuration *c);
static void ccg_unbind_config(struct usb_configuration *c);

static char func_names_buf[256];

static struct usb_device_descriptor device_desc = {
	.bLength              = sizeof(device_desc),
	.bDescriptorType      = USB_DT_DEVICE,
	.bcdUSB               = __constant_cpu_to_le16(0x0200),
	.bDeviceClass         = USB_CLASS_PER_INTERFACE,
	.idVendor             = __constant_cpu_to_le16(VENDOR_ID),
	.idProduct            = __constant_cpu_to_le16(PRODUCT_ID),
	.bcdDevice            = __constant_cpu_to_le16(0xffff),
	.bNumConfigurations   = 1,
};

static struct usb_configuration ccg_config_driver = {
	.label		= "ccg",
	.unbind		= ccg_unbind_config,
	.bConfigurationValue = 1,
	.bmAttributes	= USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower	= 0xFA, /* 500ma */
};

static void ccg_work(struct work_struct *data)
{
	struct ccg_dev *dev = container_of(data, struct ccg_dev, work);
	struct usb_composite_dev *cdev = dev->cdev;
	static char *disconnected[2] = { "USB_STATE=DISCONNECTED", NULL };
	static char *connected[2]    = { "USB_STATE=CONNECTED", NULL };
	static char *configured[2]   = { "USB_STATE=CONFIGURED", NULL };
	char **uevent_envp = NULL;
	unsigned long flags;

	spin_lock_irqsave(&cdev->lock, flags);
	if (cdev->config)
		uevent_envp = configured;
	else if (dev->connected != dev->sw_connected)
		uevent_envp = dev->connected ? connected : disconnected;
	dev->sw_connected = dev->connected;
	spin_unlock_irqrestore(&cdev->lock, flags);

	if (uevent_envp) {
		kobject_uevent_env(&dev->dev->kobj, KOBJ_CHANGE, uevent_envp);
		pr_info("%s: sent uevent %s\n", __func__, uevent_envp[0]);
	} else {
		pr_info("%s: did not send uevent (%d %d %p)\n", __func__,
			 dev->connected, dev->sw_connected, cdev->config);
	}
}


/*-------------------------------------------------------------------------*/
/* Supported functions initialization */

static struct ffs_obj *functionfs_find_dev(struct ccg_dev *dev,
					   const char *dev_name)
{
	int i;

	for (i = 0; i < dev->max_func_num; i++)
		if (strcmp(dev->ffs_tab[i].name, dev_name) == 0)
			return &dev->ffs_tab[i];

	return NULL;
}

static bool functionfs_all_ready(struct ccg_dev *dev)
{
	int i;

	for (i = 0; i < dev->max_func_num; i++)
		if (dev->ffs_tab[i].used && !dev->ffs_tab[i].desc_ready)
			return false;

	return true;
}

static int functionfs_ready_callback(struct ffs_data *ffs)
{
	struct ffs_obj *ffs_obj;
	int ret;

	mutex_lock(&_ccg_dev->mutex);

	ffs_obj = ffs->private_data;
	if (!ffs_obj) {
		ret = -EINVAL;
		goto done;
	}
	if (WARN_ON(ffs_obj->desc_ready)) {
		ret = -EBUSY;
		goto done;
	}
	ffs_obj->ffs_data = ffs;

	if (functionfs_all_ready(_ccg_dev)) {
		ret = -EBUSY;
		goto done;
	}
	ffs_obj->desc_ready = true;

done:
	mutex_unlock(&_ccg_dev->mutex);
	return ret;
}

static void reset_usb(struct ccg_dev *dev)
{
	/* Cancel pending control requests */
	usb_ep_dequeue(dev->cdev->gadget->ep0, dev->cdev->req);
	usb_remove_config(dev->cdev, &ccg_config_driver);
	dev->enabled = false;
	usb_gadget_disconnect(dev->cdev->gadget);
}

static void functionfs_closed_callback(struct ffs_data *ffs)
{
	struct ffs_obj *ffs_obj;

	mutex_lock(&_ccg_dev->mutex);

	ffs_obj = ffs->private_data;
	if (!ffs_obj)
		goto done;

	ffs_obj->desc_ready = false;

	if (_ccg_dev->enabled)
		reset_usb(_ccg_dev);

done:
	mutex_unlock(&_ccg_dev->mutex);
}

static void *functionfs_acquire_dev_callback(const char *dev_name)
{
	struct ffs_obj *ffs_dev;

	mutex_lock(&_ccg_dev->mutex);

	ffs_dev = functionfs_find_dev(_ccg_dev, dev_name);
	if (!ffs_dev) {
		ffs_dev = ERR_PTR(-ENODEV);
		goto done;
	}

	if (ffs_dev->mounted) {
		ffs_dev = ERR_PTR(-EBUSY);
		goto done;
	}
	ffs_dev->mounted = true;

done:
	mutex_unlock(&_ccg_dev->mutex);
	return ffs_dev;
}

static void functionfs_release_dev_callback(struct ffs_data *ffs_data)
{
	struct ffs_obj *ffs_dev;

	mutex_lock(&_ccg_dev->mutex);

	ffs_dev = ffs_data->private_data;
	if (ffs_dev)
		ffs_dev->mounted = false;

	mutex_unlock(&_ccg_dev->mutex);
}

static int functionfs_function_init(struct ccg_usb_function *f,
				struct usb_composite_dev *cdev)
{
	return functionfs_init();
}

static void functionfs_function_cleanup(struct ccg_usb_function *f)
{
	functionfs_cleanup();
}

static int functionfs_function_bind_config(struct ccg_usb_function *f,
					   struct usb_configuration *c)
{
	struct ccg_dev *dev = _ccg_dev;
	int i, ret;

	for (i = dev->max_func_num; i--; ) {
		if (!dev->ffs_tab[i].used)
			continue;
		ret = functionfs_bind(dev->ffs_tab[i].ffs_data, c->cdev);
		if (unlikely(ret < 0)) {
			while (++i < dev->max_func_num)
				functionfs_unbind(dev->ffs_tab[i].ffs_data);
			return ret;
		}
	}

	for (i = dev->max_func_num; i--; ) {
		if (!dev->ffs_tab[i].used)
			continue;
		ret = functionfs_bind_config(c->cdev, c,
					     dev->ffs_tab[i].ffs_data);
		if (unlikely(ret < 0))
			return ret;
	}

	return 0;
}

static void functionfs_function_unbind_config(struct ccg_usb_function *f,
					      struct usb_configuration *c)
{
	struct ccg_dev *dev = _ccg_dev;
	int i;

	for (i = dev->max_func_num; i--; )
		if (dev->ffs_tab[i].ffs_data)
			functionfs_unbind(dev->ffs_tab[i].ffs_data);
}

static ssize_t functionfs_user_functions_show(struct device *_dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct ccg_dev *dev = _ccg_dev;
	char *buff = buf;
	int i;

	mutex_lock(&dev->mutex);

	for (i = 0; i < dev->max_func_num; i++)
		buff += snprintf(buff, PAGE_SIZE + buf - buff, "%s,",
				 dev->ffs_tab[i].name);

	mutex_unlock(&dev->mutex);

	if (buff != buf)
		*(buff - 1) = '\n';
	return buff - buf;
}

static ssize_t functionfs_user_functions_store(struct device *_dev,
					       struct device_attribute *attr,
					       const char *buff, size_t size)
{
	struct ccg_dev *dev = _ccg_dev;
	char *name, *b;
	ssize_t ret = size;
	int i;

	buff = skip_spaces(buff);
	if (!*buff)
		return -EINVAL;

	mutex_lock(&dev->mutex);

	if (dev->enabled) {
		ret = -EBUSY;
		goto end;
	}

	for (i = 0; i < dev->max_func_num; i++)
		if (dev->ffs_tab[i].mounted) {
			ret = -EBUSY;
			goto end;
		}

	strlcpy(func_names_buf, buff, sizeof(func_names_buf));
	b = strim(func_names_buf);

	/* replace the list of functions */
	dev->max_func_num = 0;
	while (b) {
		name = strsep(&b, ",");
		if (dev->max_func_num == GFS_MAX_DEVS) {
			ret = -ENOSPC;
			goto end;
		}
		if (functionfs_find_dev(dev, name)) {
			ret = -EEXIST;
			continue;
		}
		dev->ffs_tab[dev->max_func_num++].name = name;
	}

end:
	mutex_unlock(&dev->mutex);
	return ret;
}

static DEVICE_ATTR(user_functions, S_IRUGO | S_IWUSR,
		   functionfs_user_functions_show,
		   functionfs_user_functions_store);

static ssize_t functionfs_max_user_functions_show(struct device *_dev,
						  struct device_attribute *attr,
						  char *buf)
{
	return sprintf(buf, "%d", GFS_MAX_DEVS);
}

static DEVICE_ATTR(max_user_functions, S_IRUGO,
		   functionfs_max_user_functions_show, NULL);

static struct device_attribute *functionfs_function_attributes[] = {
	&dev_attr_user_functions,
	&dev_attr_max_user_functions,
	NULL
};

static struct ccg_usb_function functionfs_function = {
	.name		= "fs",
	.init		= functionfs_function_init,
	.cleanup	= functionfs_function_cleanup,
	.bind_config	= functionfs_function_bind_config,
	.unbind_config  = functionfs_function_unbind_config,
	.attributes	= functionfs_function_attributes,
};

#define MAX_ACM_INSTANCES 4
struct acm_function_config {
	int instances;
};

static int
acm_function_init(struct ccg_usb_function *f, struct usb_composite_dev *cdev)
{
	f->config = kzalloc(sizeof(struct acm_function_config), GFP_KERNEL);
	if (!f->config)
		return -ENOMEM;

	return gserial_setup(cdev->gadget, MAX_ACM_INSTANCES);
}

static void acm_function_cleanup(struct ccg_usb_function *f)
{
	gserial_cleanup();
	kfree(f->config);
	f->config = NULL;
}

static int
acm_function_bind_config(struct ccg_usb_function *f,
		struct usb_configuration *c)
{
	int i;
	int ret = 0;
	struct acm_function_config *config = f->config;

	for (i = 0; i < config->instances; i++) {
		ret = acm_bind_config(c, i);
		if (ret) {
			pr_err("Could not bind acm%u config\n", i);
			break;
		}
	}

	return ret;
}

static ssize_t acm_instances_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct acm_function_config *config = f->config;
	return sprintf(buf, "%d\n", config->instances);
}

static ssize_t acm_instances_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct acm_function_config *config = f->config;
	int value;
	int ret = 0;

	ret = kstrtoint(buf, 10, &value);
	if (ret)
		return ret;

	if (value > MAX_ACM_INSTANCES)
		return -EINVAL;

	config->instances = value;

	return size;
}

static DEVICE_ATTR(instances, S_IRUGO | S_IWUSR, acm_instances_show,
						 acm_instances_store);
static struct device_attribute *acm_function_attributes[] = {
	&dev_attr_instances,
	NULL
};

static struct ccg_usb_function acm_function = {
	.name		= "acm",
	.init		= acm_function_init,
	.cleanup	= acm_function_cleanup,
	.bind_config	= acm_function_bind_config,
	.attributes	= acm_function_attributes,
};

struct rndis_function_config {
	u8      ethaddr[ETH_ALEN];
	u32     vendorID;
	char	manufacturer[256];
	/* "Wireless" RNDIS; auto-detected by Windows */
	bool	wceis;
};

static int rndis_function_init(struct ccg_usb_function *f,
			       struct usb_composite_dev *cdev)
{
	f->config = kzalloc(sizeof(struct rndis_function_config), GFP_KERNEL);
	if (!f->config)
		return -ENOMEM;
	return 0;
}

static void rndis_function_cleanup(struct ccg_usb_function *f)
{
	kfree(f->config);
	f->config = NULL;
}

static int rndis_function_bind_config(struct ccg_usb_function *f,
				      struct usb_configuration *c)
{
	int ret;
	struct rndis_function_config *rndis = f->config;

	if (!rndis) {
		pr_err("%s: rndis_pdata\n", __func__);
		return -1;
	}

	pr_info("%s MAC: %pM\n", __func__, rndis->ethaddr);

	ret = gether_setup_name(c->cdev->gadget, rndis->ethaddr, "rndis");
	if (ret) {
		pr_err("%s: gether_setup failed\n", __func__);
		return ret;
	}

	if (rndis->wceis) {
		/* "Wireless" RNDIS; auto-detected by Windows */
		rndis_iad_descriptor.bFunctionClass =
						USB_CLASS_WIRELESS_CONTROLLER;
		rndis_iad_descriptor.bFunctionSubClass = 0x01;
		rndis_iad_descriptor.bFunctionProtocol = 0x03;
		rndis_control_intf.bInterfaceClass =
						USB_CLASS_WIRELESS_CONTROLLER;
		rndis_control_intf.bInterfaceSubClass =	 0x01;
		rndis_control_intf.bInterfaceProtocol =	 0x03;
	}

	return rndis_bind_config_vendor(c, rndis->ethaddr, rndis->vendorID,
					   rndis->manufacturer);
}

static void rndis_function_unbind_config(struct ccg_usb_function *f,
						struct usb_configuration *c)
{
	gether_cleanup();
}

static ssize_t rndis_manufacturer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;
	return sprintf(buf, "%s\n", config->manufacturer);
}

static ssize_t rndis_manufacturer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;

	if (size >= sizeof(config->manufacturer))
		return -EINVAL;
	memcpy(config->manufacturer, buf, size);
	config->manufacturer[size] = 0;

	return size;
}

static DEVICE_ATTR(manufacturer, S_IRUGO | S_IWUSR, rndis_manufacturer_show,
						    rndis_manufacturer_store);

static ssize_t rndis_wceis_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;
	return sprintf(buf, "%d\n", config->wceis);
}

static ssize_t rndis_wceis_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;
	int value;
	int ret;

	ret = kstrtoint(buf, 10, &value);
	if (ret)
		return ret;

	config->wceis = value;

	return size;
}

static DEVICE_ATTR(wceis, S_IRUGO | S_IWUSR, rndis_wceis_show,
					     rndis_wceis_store);

static ssize_t rndis_ethaddr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *rndis = f->config;
	return sprintf(buf, "%pM\n", rndis->ethaddr);
}

static ssize_t rndis_ethaddr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *rndis = f->config;
	unsigned char tmp[6];

	if (sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		   tmp + 0, tmp + 1, tmp + 2, tmp + 3, tmp + 4, tmp + 5) !=
	    ETH_ALEN)
		return -EINVAL;

	memcpy(rndis->ethaddr, tmp, ETH_ALEN);

	return ETH_ALEN;

}

static DEVICE_ATTR(ethaddr, S_IRUGO | S_IWUSR, rndis_ethaddr_show,
					       rndis_ethaddr_store);

static ssize_t rndis_vendorID_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;
	return sprintf(buf, "%04x\n", config->vendorID);
}

static ssize_t rndis_vendorID_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ccg_usb_function *f = dev_get_drvdata(dev);
	struct rndis_function_config *config = f->config;
	int value;
	int ret;

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return ret;

	config->vendorID = value;

	return size;
}

static DEVICE_ATTR(vendorID, S_IRUGO | S_IWUSR, rndis_vendorID_show,
						rndis_vendorID_store);

static struct device_attribute *rndis_function_attributes[] = {
	&dev_attr_manufacturer,
	&dev_attr_wceis,
	&dev_attr_ethaddr,
	&dev_attr_vendorID,
	NULL
};

static struct ccg_usb_function rndis_function = {
	.name		= "rndis",
	.init		= rndis_function_init,
	.cleanup	= rndis_function_cleanup,
	.bind_config	= rndis_function_bind_config,
	.unbind_config	= rndis_function_unbind_config,
	.attributes	= rndis_function_attributes,
};

static int mass_storage_function_init(struct ccg_usb_function *f,
					struct usb_composite_dev *cdev)
{
	struct fsg_config fsg;
	struct fsg_common *common;
	int err;

	memset(&fsg, 0, sizeof(fsg));
	fsg.nluns = 1;
	fsg.luns[0].removable = 1;
	fsg.vendor_name = iManufacturer;
	fsg.product_name = iProduct;

	common = fsg_common_init(NULL, cdev, &fsg);
	if (IS_ERR(common))
		return PTR_ERR(common);

	err = sysfs_create_link(&f->dev->kobj,
				&common->luns[0].dev.kobj,
				"lun");
	if (err) {
		fsg_common_put(common);
		return err;
	}

	f->config = common;
	return 0;
}

static void mass_storage_function_cleanup(struct ccg_usb_function *f)
{
	fsg_common_put(f->config);
	f->config = NULL;
}

static int mass_storage_function_bind_config(struct ccg_usb_function *f,
					     struct usb_configuration *c)
{
	struct fsg_common *common = f->config;
	return fsg_bind_config(c->cdev, c, common);
}

static struct ccg_usb_function mass_storage_function = {
	.name		= "mass_storage",
	.init		= mass_storage_function_init,
	.cleanup	= mass_storage_function_cleanup,
	.bind_config	= mass_storage_function_bind_config,
};

static struct ccg_usb_function *supported_functions[] = {
	&functionfs_function,
	&acm_function,
	&rndis_function,
	&mass_storage_function,
	NULL
};


static int ccg_init_functions(struct ccg_usb_function **functions,
				  struct usb_composite_dev *cdev)
{
	struct ccg_dev *dev = _ccg_dev;
	struct ccg_usb_function *f;
	struct device_attribute **attrs;
	struct device_attribute *attr;
	int err;
	int index = 0;

	for (; (f = *functions++); index++) {
		f->dev_name = kasprintf(GFP_KERNEL, "f_%s", f->name);
		if (!f->dev_name) {
			pr_err("%s: Failed to alloc name %s", __func__,
			       f->name);
			err = -ENOMEM;
			goto err_alloc;
		}
		f->dev = device_create(ccg_class, dev->dev,
				MKDEV(0, index), f, f->dev_name);
		if (IS_ERR(f->dev)) {
			pr_err("%s: Failed to create dev %s", __func__,
							f->dev_name);
			err = PTR_ERR(f->dev);
			f->dev = NULL;
			goto err_create;
		}

		if (f->init) {
			err = f->init(f, cdev);
			if (err) {
				pr_err("%s: Failed to init %s", __func__,
								f->name);
				goto err_out;
			}
		}

		attrs = f->attributes;
		if (attrs) {
			while ((attr = *attrs++) && !err)
				err = device_create_file(f->dev, attr);
		}
		if (err) {
			pr_err("%s: Failed to create function %s attributes",
					__func__, f->name);
			goto err_uninit;
		}
	}
	return 0;

err_uninit:
	if (f->cleanup)
		f->cleanup(f);
err_out:
	device_destroy(ccg_class, f->dev->devt);
	f->dev = NULL;
err_create:
	kfree(f->dev_name);
err_alloc:
	return err;
}

static void ccg_cleanup_functions(struct ccg_usb_function **functions)
{
	struct ccg_usb_function *f;

	while (*functions) {
		f = *functions++;

		if (f->dev) {
			if (f->cleanup)
				f->cleanup(f);
			device_destroy(ccg_class, f->dev->devt);
			kfree(f->dev_name);
		}
	}
}

static int ccg_bind_enabled_functions(struct ccg_dev *dev,
				      struct usb_configuration *c)
{
	struct ccg_usb_function *f;
	int ret;

	list_for_each_entry(f, &dev->enabled_functions, enabled_list) {
		ret = f->bind_config(f, c);
		if (ret) {
			pr_err("%s: %s failed", __func__, f->name);
			return ret;
		}
	}
	return 0;
}

static void ccg_unbind_enabled_functions(struct ccg_dev *dev,
					 struct usb_configuration *c)
{
	struct ccg_usb_function *f;

	list_for_each_entry(f, &dev->enabled_functions, enabled_list)
		if (f->unbind_config)
			f->unbind_config(f, c);
}

static int ccg_enable_function(struct ccg_dev *dev, char *name)
{
	struct ccg_usb_function **functions = dev->functions;
	struct ccg_usb_function *f;
	while ((f = *functions++)) {
		if (!strcmp(name, f->name)) {
			list_add_tail(&f->enabled_list,
						&dev->enabled_functions);
			return 0;
		}
	}
	return -EINVAL;
}

/*-------------------------------------------------------------------------*/
/* /sys/class/ccg_usb/ccg%d/ interface */

static ssize_t
functions_show(struct device *pdev, struct device_attribute *attr, char *buf)
{
	struct ccg_dev *dev = dev_get_drvdata(pdev);
	struct ccg_usb_function *f;
	char *buff = buf;
	int i;

	mutex_lock(&dev->mutex);

	list_for_each_entry(f, &dev->enabled_functions, enabled_list)
		buff += sprintf(buff, "%s,", f->name);
	for (i = 0; i < dev->max_func_num; i++)
		if (dev->ffs_tab[i].used)
			buff += sprintf(buff, "%s", dev->ffs_tab[i].name);

	mutex_unlock(&dev->mutex);

	if (buff != buf)
		*(buff-1) = '\n';
	return buff - buf;
}

static ssize_t
functions_store(struct device *pdev, struct device_attribute *attr,
			       const char *buff, size_t size)
{
	struct ccg_dev *dev = dev_get_drvdata(pdev);
	char *name;
	char buf[256], *b;
	int err, i;
	bool functionfs_enabled;

	buff = skip_spaces(buff);
	if (!*buff)
		return -EINVAL;

	mutex_lock(&dev->mutex);

	if (dev->enabled) {
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

	INIT_LIST_HEAD(&dev->enabled_functions);
	functionfs_enabled = false;
	for (i = 0; i < dev->max_func_num; i++)
		dev->ffs_tab[i].used = false;

	strlcpy(buf, buff, sizeof(buf));
	b = strim(buf);

	while (b) {
		struct ffs_obj *user_func;

		name = strsep(&b, ",");
		/* handle FunctionFS implicitly */
		if (!strcmp(name, functionfs_function.name)) {
			pr_err("ccg_usb: Cannot explicitly enable '%s'", name);
			continue;
		}
		user_func = functionfs_find_dev(dev, name);
		if (user_func)
			name = functionfs_function.name;
		err = 0;
		if (!user_func || !functionfs_enabled)
			err = ccg_enable_function(dev, name);
		if (err)
			pr_err("ccg_usb: Cannot enable '%s'", name);
		else if (user_func) {
			user_func->used = true;
			dev->func_num++;
			functionfs_enabled = true;
		}
	}

	mutex_unlock(&dev->mutex);

	return size;
}

static ssize_t enable_show(struct device *pdev, struct device_attribute *attr,
			   char *buf)
{
	struct ccg_dev *dev = dev_get_drvdata(pdev);
	return sprintf(buf, "%d\n", dev->enabled);
}

static ssize_t enable_store(struct device *pdev, struct device_attribute *attr,
			    const char *buff, size_t size)
{
	struct ccg_dev *dev = dev_get_drvdata(pdev);
	struct usb_composite_dev *cdev = dev->cdev;
	int enabled = 0;

	mutex_lock(&dev->mutex);
	sscanf(buff, "%d", &enabled);
	if (enabled && dev->func_num && !functionfs_all_ready(dev)) {
		mutex_unlock(&dev->mutex);
		return -ENODEV;
	}

	if (enabled && !dev->enabled) {
		int ret;

		cdev->next_string_id = 0;
		/*
		 * Update values in composite driver's copy of
		 * device descriptor.
		 */
		cdev->desc.bDeviceClass = device_desc.bDeviceClass;
		cdev->desc.bDeviceSubClass = device_desc.bDeviceSubClass;
		cdev->desc.bDeviceProtocol = device_desc.bDeviceProtocol;
		cdev->desc.idVendor = idVendor;
		cdev->desc.idProduct = idProduct;
		cdev->desc.bcdDevice = bcdDevice;

		usb_add_config(cdev, &ccg_config_driver, ccg_bind_config);
		dev->enabled = true;
		ret = usb_gadget_connect(cdev->gadget);
		if (ret) {
			dev->enabled = false;
			usb_remove_config(cdev, &ccg_config_driver);
		}
	} else if (!enabled && dev->enabled) {
		reset_usb(dev);
	} else {
		pr_err("ccg_usb: already %s\n",
			dev->enabled ? "enabled" : "disabled");
	}

	mutex_unlock(&dev->mutex);
	return size;
}

static ssize_t state_show(struct device *pdev, struct device_attribute *attr,
			   char *buf)
{
	struct ccg_dev *dev = dev_get_drvdata(pdev);
	struct usb_composite_dev *cdev = dev->cdev;
	char *state = "DISCONNECTED";
	unsigned long flags;

	if (!cdev)
		goto out;

	spin_lock_irqsave(&cdev->lock, flags);
	if (cdev->config)
		state = "CONFIGURED";
	else if (dev->connected)
		state = "CONNECTED";
	spin_unlock_irqrestore(&cdev->lock, flags);
out:
	return sprintf(buf, "%s\n", state);
}

#define DESCRIPTOR_ATTR(field, format_string)				\
static ssize_t								\
field ## _show(struct device *dev, struct device_attribute *attr,	\
		char *buf)						\
{									\
	return sprintf(buf, format_string, device_desc.field);		\
}									\
static ssize_t								\
field ## _store(struct device *dev, struct device_attribute *attr,	\
		const char *buf, size_t size)				\
{									\
	int value;							\
	if (sscanf(buf, format_string, &value) == 1) {			\
		device_desc.field = value;				\
		return size;						\
	}								\
	return -1;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, field ## _show, field ## _store);

DESCRIPTOR_ATTR(bDeviceClass, "%d\n")
DESCRIPTOR_ATTR(bDeviceSubClass, "%d\n")
DESCRIPTOR_ATTR(bDeviceProtocol, "%d\n")

static DEVICE_ATTR(functions, S_IRUGO | S_IWUSR, functions_show,
						 functions_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, enable_show, enable_store);
static DEVICE_ATTR(state, S_IRUGO, state_show, NULL);

static struct device_attribute *ccg_usb_attributes[] = {
	&dev_attr_bDeviceClass,
	&dev_attr_bDeviceSubClass,
	&dev_attr_bDeviceProtocol,
	&dev_attr_functions,
	&dev_attr_enable,
	&dev_attr_state,
	NULL
};

/*-------------------------------------------------------------------------*/
/* Composite driver */

static int ccg_bind_config(struct usb_configuration *c)
{
	struct ccg_dev *dev = _ccg_dev;
	return ccg_bind_enabled_functions(dev, c);
}

static void ccg_unbind_config(struct usb_configuration *c)
{
	struct ccg_dev *dev = _ccg_dev;

	ccg_unbind_enabled_functions(dev, c);

	usb_ep_autoconfig_reset(dev->cdev->gadget);
}

static int ccg_bind(struct usb_composite_dev *cdev)
{
	struct ccg_dev *dev = _ccg_dev;
	struct usb_gadget	*gadget = cdev->gadget;
	int			gcnum, ret;

	/*
	 * Start disconnected. Userspace will connect the gadget once
	 * it is done configuring the functions.
	 */
	usb_gadget_disconnect(gadget);

	ret = ccg_init_functions(dev->functions, cdev);
	if (ret)
		return ret;

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
	else {
		pr_warn("%s: controller '%s' not recognized\n",
			longname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16(0x9999);
	}

	usb_gadget_set_selfpowered(gadget);
	dev->cdev = cdev;

	return 0;
}

static int ccg_usb_unbind(struct usb_composite_dev *cdev)
{
	struct ccg_dev *dev = _ccg_dev;

	cancel_work_sync(&dev->work);
	ccg_cleanup_functions(dev->functions);
	return 0;
}

static struct usb_composite_driver ccg_usb_driver = {
	.name		= "configurable_usb",
	.dev		= &device_desc,
	.bind		= ccg_bind,
	.unbind		= ccg_usb_unbind,
	.needs_serial	= true,
	.iManufacturer	= "Linux Foundation",
	.iProduct	= longname,
	.iSerialNumber	= "1234567890123456",
};

static int ccg_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *c)
{
	struct ccg_dev		*dev = _ccg_dev;
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	struct usb_request		*req = cdev->req;
	struct ccg_usb_function	*f;
	int value = -EOPNOTSUPP;
	unsigned long flags;

	req->zero = 0;
	req->complete = composite_setup_complete;
	req->length = 0;
	gadget->ep0->driver_data = cdev;

	list_for_each_entry(f, &dev->enabled_functions, enabled_list) {
		if (f->ctrlrequest) {
			value = f->ctrlrequest(f, cdev, c);
			if (value >= 0)
				break;
		}
	}

	if (value < 0)
		value = composite_setup(gadget, c);

	spin_lock_irqsave(&cdev->lock, flags);
	if (!dev->connected) {
		dev->connected = 1;
		schedule_work(&dev->work);
	} else if (c->bRequest == USB_REQ_SET_CONFIGURATION &&
						cdev->config) {
		schedule_work(&dev->work);
	}
	spin_unlock_irqrestore(&cdev->lock, flags);

	return value;
}

static void ccg_disconnect(struct usb_gadget *gadget)
{
	struct ccg_dev *dev = _ccg_dev;
	struct usb_composite_dev *cdev = get_gadget_data(gadget);
	unsigned long flags;

	composite_disconnect(gadget);

	spin_lock_irqsave(&cdev->lock, flags);
	dev->connected = 0;
	schedule_work(&dev->work);
	spin_unlock_irqrestore(&cdev->lock, flags);
}

static int ccg_create_device(struct ccg_dev *dev)
{
	struct device_attribute **attrs = ccg_usb_attributes;
	struct device_attribute *attr;
	int err;

	dev->dev = device_create(ccg_class, NULL, MKDEV(0, 0), NULL, "ccg0");
	if (IS_ERR(dev->dev))
		return PTR_ERR(dev->dev);

	dev_set_drvdata(dev->dev, dev);

	while ((attr = *attrs++)) {
		err = device_create_file(dev->dev, attr);
		if (err) {
			device_destroy(ccg_class, dev->dev->devt);
			return err;
		}
	}
	return 0;
}


static int __init ccg_init(void)
{
	struct ccg_dev *dev;
	int err;

	ccg_class = class_create(THIS_MODULE, "ccg_usb");
	if (IS_ERR(ccg_class))
		return PTR_ERR(ccg_class);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		class_destroy(ccg_class);
		return -ENOMEM;
	}

	dev->functions = supported_functions;
	INIT_LIST_HEAD(&dev->enabled_functions);
	INIT_WORK(&dev->work, ccg_work);
	mutex_init(&dev->mutex);

	err = ccg_create_device(dev);
	if (err) {
		class_destroy(ccg_class);
		kfree(dev);
		return err;
	}

	_ccg_dev = dev;

	/* Override composite driver functions */
	composite_driver.setup = ccg_setup;
	composite_driver.disconnect = ccg_disconnect;

	err = usb_composite_probe(&ccg_usb_driver);
	if (err) {
		class_destroy(ccg_class);
		kfree(dev);
	}

	return err;
}
module_init(ccg_init);

static void __exit ccg_exit(void)
{
	usb_composite_unregister(&ccg_usb_driver);
	class_destroy(ccg_class);
	kfree(_ccg_dev);
	_ccg_dev = NULL;
}
module_exit(ccg_exit);
