// SPDX-License-Identifier: GPL-2.0+
/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (mina86@mina86.com)
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "u_serial.h"
#if defined USB_ETH_RNDIS
#  undef USB_ETH_RNDIS
#endif
#ifdef CONFIG_USB_G_MULTI_RNDIS
#  define USB_ETH_RNDIS y
#endif


#define DRIVER_DESC		"Multifunction Composite Gadget"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");


#include "f_mass_storage.h"

#include "u_ecm.h"
#ifdef USB_ETH_RNDIS
#  include "u_rndis.h"
#  include "rndis.h"
#endif
#include "u_ether.h"

USB_GADGET_COMPOSITE_OPTIONS();

USB_ETHERNET_MODULE_PARAMETERS();

/***************************** Device Descriptor ****************************/

#define MULTI_VENDOR_NUM	0x1d6b	/* Linux Foundation */
#define MULTI_PRODUCT_NUM	0x0104	/* Multifunction Composite Gadget */


enum {
	__MULTI_NO_CONFIG,
#ifdef CONFIG_USB_G_MULTI_RNDIS
	MULTI_RNDIS_CONFIG_NUM,
#endif
#ifdef CONFIG_USB_G_MULTI_CDC
	MULTI_CDC_CONFIG_NUM,
#endif
};


static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */

	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
};

static const struct usb_descriptor_header *otg_desc[2];

enum {
	MULTI_STRING_RNDIS_CONFIG_IDX = USB_GADGET_FIRST_AVAIL_IDX,
	MULTI_STRING_CDC_CONFIG_IDX,
};

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	[MULTI_STRING_RNDIS_CONFIG_IDX].s = "Multifunction with RNDIS",
	[MULTI_STRING_CDC_CONFIG_IDX].s   = "Multifunction with CDC ECM",
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};




/****************************** Configurations ******************************/

static struct fsg_module_parameters fsg_mod_data = { .stall = 1 };
#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static unsigned int fsg_num_buffers = CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS;

#else

/*
 * Number of buffers we will use.
 * 2 is usually enough for good buffering pipeline
 */
#define fsg_num_buffers	CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS

#endif /* CONFIG_USB_GADGET_DEBUG_FILES */

FSG_MODULE_PARAMETERS(/* no prefix */, fsg_mod_data);

static struct usb_function_instance *fi_acm;
static struct usb_function_instance *fi_msg;

/********** RNDIS **********/

#ifdef USB_ETH_RNDIS
static struct usb_function_instance *fi_rndis;
static struct usb_function *f_acm_rndis;
static struct usb_function *f_rndis;
static struct usb_function *f_msg_rndis;

static int rndis_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	f_rndis = usb_get_function(fi_rndis);
	if (IS_ERR(f_rndis))
		return PTR_ERR(f_rndis);

	ret = usb_add_function(c, f_rndis);
	if (ret < 0)
		goto err_func_rndis;

	f_acm_rndis = usb_get_function(fi_acm);
	if (IS_ERR(f_acm_rndis)) {
		ret = PTR_ERR(f_acm_rndis);
		goto err_func_acm;
	}

	ret = usb_add_function(c, f_acm_rndis);
	if (ret)
		goto err_conf;

	f_msg_rndis = usb_get_function(fi_msg);
	if (IS_ERR(f_msg_rndis)) {
		ret = PTR_ERR(f_msg_rndis);
		goto err_fsg;
	}

	ret = usb_add_function(c, f_msg_rndis);
	if (ret)
		goto err_run;

	return 0;
err_run:
	usb_put_function(f_msg_rndis);
err_fsg:
	usb_remove_function(c, f_acm_rndis);
err_conf:
	usb_put_function(f_acm_rndis);
err_func_acm:
	usb_remove_function(c, f_rndis);
err_func_rndis:
	usb_put_function(f_rndis);
	return ret;
}

static __ref int rndis_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
		.bConfigurationValue	= MULTI_RNDIS_CONFIG_NUM,
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label          = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].s;
	config.iConfiguration = strings_dev[MULTI_STRING_RNDIS_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, rndis_do_config);
}

#else

static __ref int rndis_config_register(struct usb_composite_dev *cdev)
{
	return 0;
}

#endif


/********** CDC ECM **********/

#ifdef CONFIG_USB_G_MULTI_CDC
static struct usb_function_instance *fi_ecm;
static struct usb_function *f_acm_multi;
static struct usb_function *f_ecm;
static struct usb_function *f_msg_multi;

static int cdc_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	f_ecm = usb_get_function(fi_ecm);
	if (IS_ERR(f_ecm))
		return PTR_ERR(f_ecm);

	ret = usb_add_function(c, f_ecm);
	if (ret < 0)
		goto err_func_ecm;

	/* implicit port_num is zero */
	f_acm_multi = usb_get_function(fi_acm);
	if (IS_ERR(f_acm_multi)) {
		ret = PTR_ERR(f_acm_multi);
		goto err_func_acm;
	}

	ret = usb_add_function(c, f_acm_multi);
	if (ret)
		goto err_conf;

	f_msg_multi = usb_get_function(fi_msg);
	if (IS_ERR(f_msg_multi)) {
		ret = PTR_ERR(f_msg_multi);
		goto err_fsg;
	}

	ret = usb_add_function(c, f_msg_multi);
	if (ret)
		goto err_run;

	return 0;
err_run:
	usb_put_function(f_msg_multi);
err_fsg:
	usb_remove_function(c, f_acm_multi);
err_conf:
	usb_put_function(f_acm_multi);
err_func_acm:
	usb_remove_function(c, f_ecm);
err_func_ecm:
	usb_put_function(f_ecm);
	return ret;
}

static __ref int cdc_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
		.bConfigurationValue	= MULTI_CDC_CONFIG_NUM,
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label          = strings_dev[MULTI_STRING_CDC_CONFIG_IDX].s;
	config.iConfiguration = strings_dev[MULTI_STRING_CDC_CONFIG_IDX].id;

	return usb_add_config(cdev, &config, cdc_do_config);
}

#else

static __ref int cdc_config_register(struct usb_composite_dev *cdev)
{
	return 0;
}

#endif



/****************************** Gadget Bind ******************************/

static int __ref multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
#ifdef CONFIG_USB_G_MULTI_CDC
	struct f_ecm_opts *ecm_opts;
#endif
#ifdef USB_ETH_RNDIS
	struct f_rndis_opts *rndis_opts;
#endif
	struct fsg_opts *fsg_opts;
	struct fsg_config config;
	int status;

	if (!can_support_ecm(cdev->gadget)) {
		dev_err(&gadget->dev, "controller '%s' not usable\n",
			gadget->name);
		return -EINVAL;
	}

#ifdef CONFIG_USB_G_MULTI_CDC
	fi_ecm = usb_get_function_instance("ecm");
	if (IS_ERR(fi_ecm))
		return PTR_ERR(fi_ecm);

	ecm_opts = container_of(fi_ecm, struct f_ecm_opts, func_inst);

	gether_set_qmult(ecm_opts->net, qmult);
	if (!gether_set_host_addr(ecm_opts->net, host_addr))
		pr_info("using host ethernet address: %s", host_addr);
	if (!gether_set_dev_addr(ecm_opts->net, dev_addr))
		pr_info("using self ethernet address: %s", dev_addr);
#endif

#ifdef USB_ETH_RNDIS
	fi_rndis = usb_get_function_instance("rndis");
	if (IS_ERR(fi_rndis)) {
		status = PTR_ERR(fi_rndis);
		goto fail;
	}

	rndis_opts = container_of(fi_rndis, struct f_rndis_opts, func_inst);

	gether_set_qmult(rndis_opts->net, qmult);
	if (!gether_set_host_addr(rndis_opts->net, host_addr))
		pr_info("using host ethernet address: %s", host_addr);
	if (!gether_set_dev_addr(rndis_opts->net, dev_addr))
		pr_info("using self ethernet address: %s", dev_addr);
#endif

#if (defined CONFIG_USB_G_MULTI_CDC && defined USB_ETH_RNDIS)
	/*
	 * If both ecm and rndis are selected then:
	 *	1) rndis borrows the net interface from ecm
	 *	2) since the interface is shared it must not be bound
	 *	twice - in ecm's _and_ rndis' binds, so do it here.
	 */
	gether_set_gadget(ecm_opts->net, cdev->gadget);
	status = gether_register_netdev(ecm_opts->net);
	if (status)
		goto fail0;

	rndis_borrow_net(fi_rndis, ecm_opts->net);
	ecm_opts->bound = true;
#endif

	/* set up serial link layer */
	fi_acm = usb_get_function_instance("acm");
	if (IS_ERR(fi_acm)) {
		status = PTR_ERR(fi_acm);
		goto fail0;
	}

	/* set up mass storage function */
	fi_msg = usb_get_function_instance("mass_storage");
	if (IS_ERR(fi_msg)) {
		status = PTR_ERR(fi_msg);
		goto fail1;
	}
	fsg_config_from_params(&config, &fsg_mod_data, fsg_num_buffers);
	fsg_opts = fsg_opts_from_func_inst(fi_msg);

	fsg_opts->no_configfs = true;
	status = fsg_common_set_num_buffers(fsg_opts->common, fsg_num_buffers);
	if (status)
		goto fail2;

	status = fsg_common_set_cdev(fsg_opts->common, cdev, config.can_stall);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_sysfs(fsg_opts->common, true);
	status = fsg_common_create_luns(fsg_opts->common, &config);
	if (status)
		goto fail_set_cdev;

	fsg_common_set_inquiry_string(fsg_opts->common, config.vendor_name,
				      config.product_name);

	/* allocate string IDs */
	status = usb_string_ids_tab(cdev, strings_dev);
	if (unlikely(status < 0))
		goto fail_string_ids;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	if (gadget_is_otg(gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(gadget);
		if (!usb_desc)
			goto fail_string_ids;
		usb_otg_descriptor_init(gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	/* register configurations */
	status = rndis_config_register(cdev);
	if (unlikely(status < 0))
		goto fail_otg_desc;

	status = cdc_config_register(cdev);
	if (unlikely(status < 0))
		goto fail_otg_desc;
	usb_composite_overwrite_options(cdev, &coverwrite);

	/* we're done */
	dev_info(&gadget->dev, DRIVER_DESC "\n");
	return 0;


	/* error recovery */
fail_otg_desc:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
fail_string_ids:
	fsg_common_remove_luns(fsg_opts->common);
fail_set_cdev:
	fsg_common_free_buffers(fsg_opts->common);
fail2:
	usb_put_function_instance(fi_msg);
fail1:
	usb_put_function_instance(fi_acm);
fail0:
#ifdef USB_ETH_RNDIS
	usb_put_function_instance(fi_rndis);
fail:
#endif
#ifdef CONFIG_USB_G_MULTI_CDC
	usb_put_function_instance(fi_ecm);
#endif
	return status;
}

static int multi_unbind(struct usb_composite_dev *cdev)
{
#ifdef CONFIG_USB_G_MULTI_CDC
	usb_put_function(f_msg_multi);
#endif
#ifdef USB_ETH_RNDIS
	usb_put_function(f_msg_rndis);
#endif
	usb_put_function_instance(fi_msg);
#ifdef CONFIG_USB_G_MULTI_CDC
	usb_put_function(f_acm_multi);
#endif
#ifdef USB_ETH_RNDIS
	usb_put_function(f_acm_rndis);
#endif
	usb_put_function_instance(fi_acm);
#ifdef USB_ETH_RNDIS
	usb_put_function(f_rndis);
	usb_put_function_instance(fi_rndis);
#endif
#ifdef CONFIG_USB_G_MULTI_CDC
	usb_put_function(f_ecm);
	usb_put_function_instance(fi_ecm);
#endif
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}


/****************************** Some noise ******************************/


static struct usb_composite_driver multi_driver = {
	.name		= "g_multi",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_SUPER,
	.bind		= multi_bind,
	.unbind		= multi_unbind,
	.needs_serial	= 1,
};

module_usb_composite_driver(multi_driver);
