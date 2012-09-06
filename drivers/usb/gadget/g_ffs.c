/*
 * g_ffs.c -- user mode file system API for USB composite function controllers
 *
 * Copyright (C) 2010 Samsung Electronics
 * Author: Michal Nazarewicz <mina86@mina86.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "g_ffs: " fmt

#include <linux/module.h>
#include <linux/utsname.h>

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */

#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#if defined CONFIG_USB_FUNCTIONFS_ETH || defined CONFIG_USB_FUNCTIONFS_RNDIS
#  if defined USB_ETH_RNDIS
#    undef USB_ETH_RNDIS
#  endif
#  ifdef CONFIG_USB_FUNCTIONFS_RNDIS
#    define USB_ETH_RNDIS y
#  endif

#  include "f_ecm.c"
#  include "f_subset.c"
#  ifdef USB_ETH_RNDIS
#    include "f_rndis.c"
#    include "rndis.c"
#  endif
#  include "u_ether.c"

static u8 gfs_hostaddr[ETH_ALEN];
#  ifdef CONFIG_USB_FUNCTIONFS_ETH
static int eth_bind_config(struct usb_configuration *c, u8 ethaddr[ETH_ALEN]);
#  endif
#else
#  define gether_cleanup() do { } while (0)
#  define gether_setup(gadget, hostaddr)   ((int)0)
#  define gfs_hostaddr NULL
#endif

#include "f_fs.c"

#define DRIVER_NAME	"g_ffs"
#define DRIVER_DESC	"USB Function Filesystem"
#define DRIVER_VERSION	"24 Aug 2004"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");

#define GFS_VENDOR_ID	0x1d6b	/* Linux Foundation */
#define GFS_PRODUCT_ID	0x0105	/* FunctionFS Gadget */

#define GFS_MAX_DEVS	10

struct gfs_ffs_obj {
	const char *name;
	bool mounted;
	bool desc_ready;
	struct ffs_data *ffs_data;
};

static struct usb_device_descriptor gfs_dev_desc = {
	.bLength		= sizeof gfs_dev_desc,
	.bDescriptorType	= USB_DT_DEVICE,

	.bcdUSB			= cpu_to_le16(0x0200),
	.bDeviceClass		= USB_CLASS_PER_INTERFACE,

	.idVendor		= cpu_to_le16(GFS_VENDOR_ID),
	.idProduct		= cpu_to_le16(GFS_PRODUCT_ID),
};

static char *func_names[GFS_MAX_DEVS];
static unsigned int func_num;

module_param_named(bDeviceClass,    gfs_dev_desc.bDeviceClass,    byte,   0644);
MODULE_PARM_DESC(bDeviceClass, "USB Device class");
module_param_named(bDeviceSubClass, gfs_dev_desc.bDeviceSubClass, byte,   0644);
MODULE_PARM_DESC(bDeviceSubClass, "USB Device subclass");
module_param_named(bDeviceProtocol, gfs_dev_desc.bDeviceProtocol, byte,   0644);
MODULE_PARM_DESC(bDeviceProtocol, "USB Device protocol");
module_param_array_named(functions, func_names, charp, &func_num, 0);
MODULE_PARM_DESC(functions, "USB Functions list");

static const struct usb_descriptor_header *gfs_otg_desc[] = {
	(const struct usb_descriptor_header *)
	&(const struct usb_otg_descriptor) {
		.bLength		= sizeof(struct usb_otg_descriptor),
		.bDescriptorType	= USB_DT_OTG,

		/*
		 * REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ...
		 */
		.bmAttributes		= USB_OTG_SRP | USB_OTG_HNP,
	},

	NULL
};

/* String IDs are assigned dynamically */
static struct usb_string gfs_strings[] = {
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{ .s = "FunctionFS + RNDIS" },
#endif
#ifdef CONFIG_USB_FUNCTIONFS_ETH
	{ .s = "FunctionFS + ECM" },
#endif
#ifdef CONFIG_USB_FUNCTIONFS_GENERIC
	{ .s = "FunctionFS" },
#endif
	{  } /* end of list */
};

static struct usb_gadget_strings *gfs_dev_strings[] = {
	&(struct usb_gadget_strings) {
		.language	= 0x0409,	/* en-us */
		.strings	= gfs_strings,
	},
	NULL,
};

struct gfs_configuration {
	struct usb_configuration c;
	int (*eth)(struct usb_configuration *c, u8 *ethaddr);
} gfs_configurations[] = {
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{
		.eth		= rndis_bind_config,
	},
#endif

#ifdef CONFIG_USB_FUNCTIONFS_ETH
	{
		.eth		= eth_bind_config,
	},
#endif

#ifdef CONFIG_USB_FUNCTIONFS_GENERIC
	{
	},
#endif
};

static int gfs_bind(struct usb_composite_dev *cdev);
static int gfs_unbind(struct usb_composite_dev *cdev);
static int gfs_do_config(struct usb_configuration *c);

static __refdata struct usb_composite_driver gfs_driver = {
	.name		= DRIVER_NAME,
	.dev		= &gfs_dev_desc,
	.strings	= gfs_dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.unbind		= gfs_unbind,
	.iProduct	= DRIVER_DESC,
};

static DEFINE_MUTEX(gfs_lock);
static unsigned int missing_funcs;
static bool gfs_ether_setup;
static bool gfs_registered;
static bool gfs_single_func;
static struct gfs_ffs_obj *ffs_tab;

static int __init gfs_init(void)
{
	int i;

	ENTER();

	if (!func_num) {
		gfs_single_func = true;
		func_num = 1;
	}

	ffs_tab = kcalloc(func_num, sizeof *ffs_tab, GFP_KERNEL);
	if (!ffs_tab)
		return -ENOMEM;

	if (!gfs_single_func)
		for (i = 0; i < func_num; i++)
			ffs_tab[i].name = func_names[i];

	missing_funcs = func_num;

	return functionfs_init();
}
module_init(gfs_init);

static void __exit gfs_exit(void)
{
	ENTER();
	mutex_lock(&gfs_lock);

	if (gfs_registered)
		usb_composite_unregister(&gfs_driver);
	gfs_registered = false;

	functionfs_cleanup();

	mutex_unlock(&gfs_lock);
	kfree(ffs_tab);
}
module_exit(gfs_exit);

static struct gfs_ffs_obj *gfs_find_dev(const char *dev_name)
{
	int i;

	ENTER();

	if (gfs_single_func)
		return &ffs_tab[0];

	for (i = 0; i < func_num; i++)
		if (strcmp(ffs_tab[i].name, dev_name) == 0)
			return &ffs_tab[i];

	return NULL;
}

static int functionfs_ready_callback(struct ffs_data *ffs)
{
	struct gfs_ffs_obj *ffs_obj;
	int ret;

	ENTER();
	mutex_lock(&gfs_lock);

	ffs_obj = ffs->private_data;
	if (!ffs_obj) {
		ret = -EINVAL;
		goto done;
	}

	if (WARN_ON(ffs_obj->desc_ready)) {
		ret = -EBUSY;
		goto done;
	}
	ffs_obj->desc_ready = true;
	ffs_obj->ffs_data = ffs;

	if (--missing_funcs) {
		ret = 0;
		goto done;
	}

	if (gfs_registered) {
		ret = -EBUSY;
		goto done;
	}
	gfs_registered = true;

	ret = usb_composite_probe(&gfs_driver, gfs_bind);
	if (unlikely(ret < 0))
		gfs_registered = false;

done:
	mutex_unlock(&gfs_lock);
	return ret;
}

static void functionfs_closed_callback(struct ffs_data *ffs)
{
	struct gfs_ffs_obj *ffs_obj;

	ENTER();
	mutex_lock(&gfs_lock);

	ffs_obj = ffs->private_data;
	if (!ffs_obj)
		goto done;

	ffs_obj->desc_ready = false;
	missing_funcs++;

	if (gfs_registered)
		usb_composite_unregister(&gfs_driver);
	gfs_registered = false;

done:
	mutex_unlock(&gfs_lock);
}

static void *functionfs_acquire_dev_callback(const char *dev_name)
{
	struct gfs_ffs_obj *ffs_dev;

	ENTER();
	mutex_lock(&gfs_lock);

	ffs_dev = gfs_find_dev(dev_name);
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
	mutex_unlock(&gfs_lock);
	return ffs_dev;
}

static void functionfs_release_dev_callback(struct ffs_data *ffs_data)
{
	struct gfs_ffs_obj *ffs_dev;

	ENTER();
	mutex_lock(&gfs_lock);

	ffs_dev = ffs_data->private_data;
	if (ffs_dev)
		ffs_dev->mounted = false;

	mutex_unlock(&gfs_lock);
}

/*
 * It is assumed that gfs_bind is called from a context where gfs_lock is held
 */
static int gfs_bind(struct usb_composite_dev *cdev)
{
	int ret, i;

	ENTER();

	if (missing_funcs)
		return -ENODEV;

	ret = gether_setup(cdev->gadget, gfs_hostaddr);
	if (unlikely(ret < 0))
		goto error_quick;
	gfs_ether_setup = true;

	ret = usb_string_ids_tab(cdev, gfs_strings);
	if (unlikely(ret < 0))
		goto error;

	for (i = func_num; --i; ) {
		ret = functionfs_bind(ffs_tab[i].ffs_data, cdev);
		if (unlikely(ret < 0)) {
			while (++i < func_num)
				functionfs_unbind(ffs_tab[i].ffs_data);
			goto error;
		}
	}

	for (i = 0; i < ARRAY_SIZE(gfs_configurations); ++i) {
		struct gfs_configuration *c = gfs_configurations + i;

		c->c.label			= gfs_strings[i].s;
		c->c.iConfiguration		= gfs_strings[i].id;
		c->c.bConfigurationValue	= 1 + i;
		c->c.bmAttributes		= USB_CONFIG_ATT_SELFPOWER;

		ret = usb_add_config(cdev, &c->c, gfs_do_config);
		if (unlikely(ret < 0))
			goto error_unbind;
	}

	return 0;

error_unbind:
	for (i = 0; i < func_num; i++)
		functionfs_unbind(ffs_tab[i].ffs_data);
error:
	gether_cleanup();
error_quick:
	gfs_ether_setup = false;
	return ret;
}

/*
 * It is assumed that gfs_unbind is called from a context where gfs_lock is held
 */
static int gfs_unbind(struct usb_composite_dev *cdev)
{
	int i;

	ENTER();

	/*
	 * We may have been called in an error recovery from
	 * composite_bind() after gfs_unbind() failure so we need to
	 * check if gfs_ffs_data is not NULL since gfs_bind() handles
	 * all error recovery itself.  I'd rather we werent called
	 * from composite on orror recovery, but what you're gonna
	 * do...?
	 */
	if (gfs_ether_setup)
		gether_cleanup();
	gfs_ether_setup = false;

	for (i = func_num; --i; )
		if (ffs_tab[i].ffs_data)
			functionfs_unbind(ffs_tab[i].ffs_data);

	return 0;
}

/*
 * It is assumed that gfs_do_config is called from a context where
 * gfs_lock is held
 */
static int gfs_do_config(struct usb_configuration *c)
{
	struct gfs_configuration *gc =
		container_of(c, struct gfs_configuration, c);
	int i;
	int ret;

	if (missing_funcs)
		return -ENODEV;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = gfs_otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	if (gc->eth) {
		ret = gc->eth(c, gfs_hostaddr);
		if (unlikely(ret < 0))
			return ret;
	}

	for (i = 0; i < func_num; i++) {
		ret = functionfs_bind_config(c->cdev, c, ffs_tab[i].ffs_data);
		if (unlikely(ret < 0))
			return ret;
	}

	/*
	 * After previous do_configs there may be some invalid
	 * pointers in c->interface array.  This happens every time
	 * a user space function with fewer interfaces than a user
	 * space function that was run before the new one is run.  The
	 * compasit's set_config() assumes that if there is no more
	 * then MAX_CONFIG_INTERFACES interfaces in a configuration
	 * then there is a NULL pointer after the last interface in
	 * c->interface array.  We need to make sure this is true.
	 */
	if (c->next_interface_id < ARRAY_SIZE(c->interface))
		c->interface[c->next_interface_id] = NULL;

	return 0;
}

#ifdef CONFIG_USB_FUNCTIONFS_ETH

static int eth_bind_config(struct usb_configuration *c, u8 ethaddr[ETH_ALEN])
{
	return can_support_ecm(c->cdev->gadget)
		? ecm_bind_config(c, ethaddr)
		: geth_bind_config(c, ethaddr);
}

#endif
