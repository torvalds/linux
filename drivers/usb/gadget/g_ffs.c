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
/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#if defined CONFIG_USB_FUNCTIONFS_ETH || defined CONFIG_USB_FUNCTIONFS_RNDIS
#include <linux/netdevice.h>

#  if defined USB_ETH_RNDIS
#    undef USB_ETH_RNDIS
#  endif
#  ifdef CONFIG_USB_FUNCTIONFS_RNDIS
#    define USB_ETH_RNDIS y
#  endif

#  include "u_ecm.h"
#  include "u_gether.h"
#  ifdef USB_ETH_RNDIS
#    include "u_rndis.h"
#    include "rndis.h"
#  endif
#  include "u_ether.h"

USB_ETHERNET_MODULE_PARAMETERS();

#  ifdef CONFIG_USB_FUNCTIONFS_ETH
static int eth_bind_config(struct usb_configuration *c);
static struct usb_function_instance *fi_ecm;
static struct usb_function *f_ecm;
static struct usb_function_instance *fi_geth;
static struct usb_function *f_geth;
#  endif
#  ifdef CONFIG_USB_FUNCTIONFS_RNDIS
static int bind_rndis_config(struct usb_configuration *c);
static struct usb_function_instance *fi_rndis;
static struct usb_function *f_rndis;
#  endif
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

USB_GADGET_COMPOSITE_OPTIONS();

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
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
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
	int (*eth)(struct usb_configuration *c);
} gfs_configurations[] = {
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{
		.eth		= bind_rndis_config,
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
	.bind		= gfs_bind,
	.unbind		= gfs_unbind,
};

static DEFINE_MUTEX(gfs_lock);
static unsigned int missing_funcs;
static bool gfs_registered;
static bool gfs_single_func;
static struct ffs_dev *ffs_tab;

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

static struct ffs_dev *gfs_find_dev(const char *dev_name)
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
	struct ffs_dev *ffs_obj;
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

	ret = usb_composite_probe(&gfs_driver);
	if (unlikely(ret < 0))
		gfs_registered = false;

done:
	mutex_unlock(&gfs_lock);
	return ret;
}

static void functionfs_closed_callback(struct ffs_data *ffs)
{
	struct ffs_dev *ffs_obj;

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
	struct ffs_dev *ffs_dev;

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
	struct ffs_dev *ffs_dev;

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
#if defined CONFIG_USB_FUNCTIONFS_ETH || defined CONFIG_USB_FUNCTIONFS_RNDIS
	struct net_device *net;
#endif
	int ret, i;

	ENTER();

	if (missing_funcs)
		return -ENODEV;
#if defined CONFIG_USB_FUNCTIONFS_ETH
	if (can_support_ecm(cdev->gadget)) {
		struct f_ecm_opts *ecm_opts;

		fi_ecm = usb_get_function_instance("ecm");
		if (IS_ERR(fi_ecm))
			return PTR_ERR(fi_ecm);
		ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);
		net = ecm_opts->net;
	} else {
		struct f_gether_opts *geth_opts;

		fi_geth = usb_get_function_instance("geth");
		if (IS_ERR(fi_geth))
			return PTR_ERR(fi_geth);
		geth_opts = container_of(fi_geth, struct f_gether_opts,
					 func_inst);
		net = geth_opts->net;
	}
#endif

#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{
		struct f_rndis_opts *rndis_opts;

		fi_rndis = usb_get_function_instance("rndis");
		if (IS_ERR(fi_rndis)) {
			ret = PTR_ERR(fi_rndis);
			goto error;
		}
		rndis_opts = container_of(fi_rndis, struct f_rndis_opts,
					  func_inst);
#ifndef CONFIG_USB_FUNCTIONFS_ETH
		net = rndis_opts->net;
#endif
	}
#endif

#if defined CONFIG_USB_FUNCTIONFS_ETH || defined CONFIG_USB_FUNCTIONFS_RNDIS
	gether_set_qmult(net, qmult);
	if (!gether_set_host_addr(net, host_addr))
		pr_info("using host ethernet address: %s", host_addr);
	if (!gether_set_dev_addr(net, dev_addr))
		pr_info("using self ethernet address: %s", dev_addr);
#endif

#if defined CONFIG_USB_FUNCTIONFS_RNDIS && defined CONFIG_USB_FUNCTIONFS_ETH
	gether_set_gadget(net, cdev->gadget);
	ret = gether_register_netdev(net);
	if (ret)
		goto error_rndis;

	if (can_support_ecm(cdev->gadget)) {
		struct f_ecm_opts *ecm_opts;

		ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);
		ecm_opts->bound = true;
	} else {
		struct f_gether_opts *geth_opts;

		geth_opts = container_of(fi_geth, struct f_gether_opts,
					 func_inst);
		geth_opts->bound = true;
	}

	rndis_borrow_net(fi_rndis, net);
#endif

	ret = usb_string_ids_tab(cdev, gfs_strings);
	if (unlikely(ret < 0))
		goto error_rndis;
	gfs_dev_desc.iProduct = gfs_strings[USB_GADGET_PRODUCT_IDX].id;

	for (i = func_num; i--; ) {
		ret = functionfs_bind(ffs_tab[i].ffs_data, cdev);
		if (unlikely(ret < 0)) {
			while (++i < func_num)
				functionfs_unbind(ffs_tab[i].ffs_data);
			goto error_rndis;
		}
	}

	for (i = 0; i < ARRAY_SIZE(gfs_configurations); ++i) {
		struct gfs_configuration *c = gfs_configurations + i;
		int sid = USB_GADGET_FIRST_AVAIL_IDX + i;

		c->c.label			= gfs_strings[sid].s;
		c->c.iConfiguration		= gfs_strings[sid].id;
		c->c.bConfigurationValue	= 1 + i;
		c->c.bmAttributes		= USB_CONFIG_ATT_SELFPOWER;

		ret = usb_add_config(cdev, &c->c, gfs_do_config);
		if (unlikely(ret < 0))
			goto error_unbind;
	}
	usb_composite_overwrite_options(cdev, &coverwrite);
	return 0;

error_unbind:
	for (i = 0; i < func_num; i++)
		functionfs_unbind(ffs_tab[i].ffs_data);
error_rndis:
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	usb_put_function_instance(fi_rndis);
error:
#endif
#if defined CONFIG_USB_FUNCTIONFS_ETH
	if (can_support_ecm(cdev->gadget))
		usb_put_function_instance(fi_ecm);
	else
		usb_put_function_instance(fi_geth);
#endif
	return ret;
}

/*
 * It is assumed that gfs_unbind is called from a context where gfs_lock is held
 */
static int gfs_unbind(struct usb_composite_dev *cdev)
{
	int i;

	ENTER();


#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	usb_put_function(f_rndis);
	usb_put_function_instance(fi_rndis);
#endif

#if defined CONFIG_USB_FUNCTIONFS_ETH
	if (can_support_ecm(cdev->gadget)) {
		usb_put_function(f_ecm);
		usb_put_function_instance(fi_ecm);
	} else {
		usb_put_function(f_geth);
		usb_put_function_instance(fi_geth);
	}
#endif

	/*
	 * We may have been called in an error recovery from
	 * composite_bind() after gfs_unbind() failure so we need to
	 * check if instance's ffs_data is not NULL since gfs_bind() handles
	 * all error recovery itself.  I'd rather we werent called
	 * from composite on orror recovery, but what you're gonna
	 * do...?
	 */
	for (i = func_num; i--; )
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
		ret = gc->eth(c);
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

static int eth_bind_config(struct usb_configuration *c)
{
	int status = 0;

	if (can_support_ecm(c->cdev->gadget)) {
		f_ecm = usb_get_function(fi_ecm);
		if (IS_ERR(f_ecm))
			return PTR_ERR(f_ecm);

		status = usb_add_function(c, f_ecm);
		if (status < 0)
			usb_put_function(f_ecm);

	} else {
		f_geth = usb_get_function(fi_geth);
		if (IS_ERR(f_geth))
			return PTR_ERR(f_geth);

		status = usb_add_function(c, f_geth);
		if (status < 0)
			usb_put_function(f_geth);
	}
	return status;
}

#endif

#ifdef CONFIG_USB_FUNCTIONFS_RNDIS

static int bind_rndis_config(struct usb_configuration *c)
{
	int status = 0;

	f_rndis = usb_get_function(fi_rndis);
	if (IS_ERR(f_rndis))
		return PTR_ERR(f_rndis);

	status = usb_add_function(c, f_rndis);
	if (status < 0)
		usb_put_function(f_rndis);

	return status;
}

#endif
