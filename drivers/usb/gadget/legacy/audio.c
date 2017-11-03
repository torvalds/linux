// SPDX-License-Identifier: GPL-2.0+
/*
 * audio.c -- Audio gadget driver
 *
 * Copyright (C) 2008 Bryan Wu <cooloney@kernel.org>
 * Copyright (C) 2008 Analog Devices, Inc
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb/composite.h>

#define DRIVER_DESC		"Linux USB Audio Gadget"
#define DRIVER_VERSION		"Feb 2, 2012"

USB_GADGET_COMPOSITE_OPTIONS();

#ifndef CONFIG_GADGET_UAC1
#include "u_uac2.h"

/* Playback(USB-IN) Default Stereo - Fl/Fr */
static int p_chmask = UAC2_DEF_PCHMASK;
module_param(p_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(p_chmask, "Playback Channel Mask");

/* Playback Default 48 KHz */
static int p_srate = UAC2_DEF_PSRATE;
module_param(p_srate, uint, S_IRUGO);
MODULE_PARM_DESC(p_srate, "Playback Sampling Rate");

/* Playback Default 16bits/sample */
static int p_ssize = UAC2_DEF_PSSIZE;
module_param(p_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(p_ssize, "Playback Sample Size(bytes)");

/* Capture(USB-OUT) Default Stereo - Fl/Fr */
static int c_chmask = UAC2_DEF_CCHMASK;
module_param(c_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(c_chmask, "Capture Channel Mask");

/* Capture Default 64 KHz */
static int c_srate = UAC2_DEF_CSRATE;
module_param(c_srate, uint, S_IRUGO);
MODULE_PARM_DESC(c_srate, "Capture Sampling Rate");

/* Capture Default 16bits/sample */
static int c_ssize = UAC2_DEF_CSSIZE;
module_param(c_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(c_ssize, "Capture Sample Size(bytes)");
#else
#ifndef CONFIG_GADGET_UAC1_LEGACY
#include "u_uac1.h"

/* Playback(USB-IN) Default Stereo - Fl/Fr */
static int p_chmask = UAC1_DEF_PCHMASK;
module_param(p_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(p_chmask, "Playback Channel Mask");

/* Playback Default 48 KHz */
static int p_srate = UAC1_DEF_PSRATE;
module_param(p_srate, uint, S_IRUGO);
MODULE_PARM_DESC(p_srate, "Playback Sampling Rate");

/* Playback Default 16bits/sample */
static int p_ssize = UAC1_DEF_PSSIZE;
module_param(p_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(p_ssize, "Playback Sample Size(bytes)");

/* Capture(USB-OUT) Default Stereo - Fl/Fr */
static int c_chmask = UAC1_DEF_CCHMASK;
module_param(c_chmask, uint, S_IRUGO);
MODULE_PARM_DESC(c_chmask, "Capture Channel Mask");

/* Capture Default 48 KHz */
static int c_srate = UAC1_DEF_CSRATE;
module_param(c_srate, uint, S_IRUGO);
MODULE_PARM_DESC(c_srate, "Capture Sampling Rate");

/* Capture Default 16bits/sample */
static int c_ssize = UAC1_DEF_CSSIZE;
module_param(c_ssize, uint, S_IRUGO);
MODULE_PARM_DESC(c_ssize, "Capture Sample Size(bytes)");
#else /* CONFIG_GADGET_UAC1_LEGACY */
#include "u_uac1_legacy.h"

static char *fn_play = FILE_PCM_PLAYBACK;
module_param(fn_play, charp, S_IRUGO);
MODULE_PARM_DESC(fn_play, "Playback PCM device file name");

static char *fn_cap = FILE_PCM_CAPTURE;
module_param(fn_cap, charp, S_IRUGO);
MODULE_PARM_DESC(fn_cap, "Capture PCM device file name");

static char *fn_cntl = FILE_CONTROL;
module_param(fn_cntl, charp, S_IRUGO);
MODULE_PARM_DESC(fn_cntl, "Control device file name");

static int req_buf_size = UAC1_OUT_EP_MAX_PACKET_SIZE;
module_param(req_buf_size, int, S_IRUGO);
MODULE_PARM_DESC(req_buf_size, "ISO OUT endpoint request buffer size");

static int req_count = UAC1_REQ_COUNT;
module_param(req_count, int, S_IRUGO);
MODULE_PARM_DESC(req_count, "ISO OUT endpoint request count");

static int audio_buf_size = UAC1_AUDIO_BUF_SIZE;
module_param(audio_buf_size, int, S_IRUGO);
MODULE_PARM_DESC(audio_buf_size, "Audio buffer size");
#endif /* CONFIG_GADGET_UAC1_LEGACY */
#endif

/* string IDs are assigned dynamically */

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language = 0x0409,	/* en-us */
	.strings = strings_dev,
};

static struct usb_gadget_strings *audio_strings[] = {
	&stringtab_dev,
	NULL,
};

#ifndef CONFIG_GADGET_UAC1
static struct usb_function_instance *fi_uac2;
static struct usb_function *f_uac2;
#else
static struct usb_function_instance *fi_uac1;
static struct usb_function *f_uac1;
#endif

/*-------------------------------------------------------------------------*/

/* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */

/* Thanks to Linux Foundation for donating this product ID. */
#define AUDIO_VENDOR_NUM		0x1d6b	/* Linux Foundation */
#define AUDIO_PRODUCT_NUM		0x0101	/* Linux-USB Audio Gadget */

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */

#ifdef CONFIG_GADGET_UAC1_LEGACY
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
#else
	.bDeviceClass =		USB_CLASS_MISC,
	.bDeviceSubClass =	0x02,
	.bDeviceProtocol =	0x01,
#endif
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id defaults change according to what configs
	 * we support.  (As does bNumConfigurations.)  These values can
	 * also be overridden by module parameters.
	 */
	.idVendor =		cpu_to_le16(AUDIO_VENDOR_NUM),
	.idProduct =		cpu_to_le16(AUDIO_PRODUCT_NUM),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations =	1,
};

static const struct usb_descriptor_header *otg_desc[2];

/*-------------------------------------------------------------------------*/

static int audio_do_config(struct usb_configuration *c)
{
	int status;

	/* FIXME alloc iConfiguration string, set it in c->strings */

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

#ifdef CONFIG_GADGET_UAC1
	f_uac1 = usb_get_function(fi_uac1);
	if (IS_ERR(f_uac1)) {
		status = PTR_ERR(f_uac1);
		return status;
	}

	status = usb_add_function(c, f_uac1);
	if (status < 0) {
		usb_put_function(f_uac1);
		return status;
	}
#else
	f_uac2 = usb_get_function(fi_uac2);
	if (IS_ERR(f_uac2)) {
		status = PTR_ERR(f_uac2);
		return status;
	}

	status = usb_add_function(c, f_uac2);
	if (status < 0) {
		usb_put_function(f_uac2);
		return status;
	}
#endif

	return 0;
}

static struct usb_configuration audio_config_driver = {
	.label			= DRIVER_DESC,
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};

/*-------------------------------------------------------------------------*/

static int audio_bind(struct usb_composite_dev *cdev)
{
#ifndef CONFIG_GADGET_UAC1
	struct f_uac2_opts	*uac2_opts;
#else
#ifndef CONFIG_GADGET_UAC1_LEGACY
	struct f_uac1_opts	*uac1_opts;
#else
	struct f_uac1_legacy_opts	*uac1_opts;
#endif
#endif
	int			status;

#ifndef CONFIG_GADGET_UAC1
	fi_uac2 = usb_get_function_instance("uac2");
	if (IS_ERR(fi_uac2))
		return PTR_ERR(fi_uac2);
#else
#ifndef CONFIG_GADGET_UAC1_LEGACY
	fi_uac1 = usb_get_function_instance("uac1");
#else
	fi_uac1 = usb_get_function_instance("uac1_legacy");
#endif
	if (IS_ERR(fi_uac1))
		return PTR_ERR(fi_uac1);
#endif

#ifndef CONFIG_GADGET_UAC1
	uac2_opts = container_of(fi_uac2, struct f_uac2_opts, func_inst);
	uac2_opts->p_chmask = p_chmask;
	uac2_opts->p_srate = p_srate;
	uac2_opts->p_ssize = p_ssize;
	uac2_opts->c_chmask = c_chmask;
	uac2_opts->c_srate = c_srate;
	uac2_opts->c_ssize = c_ssize;
	uac2_opts->req_number = UAC2_DEF_REQ_NUM;
#else
#ifndef CONFIG_GADGET_UAC1_LEGACY
	uac1_opts = container_of(fi_uac1, struct f_uac1_opts, func_inst);
	uac1_opts->p_chmask = p_chmask;
	uac1_opts->p_srate = p_srate;
	uac1_opts->p_ssize = p_ssize;
	uac1_opts->c_chmask = c_chmask;
	uac1_opts->c_srate = c_srate;
	uac1_opts->c_ssize = c_ssize;
	uac1_opts->req_number = UAC1_DEF_REQ_NUM;
#else /* CONFIG_GADGET_UAC1_LEGACY */
	uac1_opts = container_of(fi_uac1, struct f_uac1_legacy_opts, func_inst);
	uac1_opts->fn_play = fn_play;
	uac1_opts->fn_cap = fn_cap;
	uac1_opts->fn_cntl = fn_cntl;
	uac1_opts->req_buf_size = req_buf_size;
	uac1_opts->req_count = req_count;
	uac1_opts->audio_buf_size = audio_buf_size;
#endif /* CONFIG_GADGET_UAC1_LEGACY */
#endif

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

	if (gadget_is_otg(cdev->gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(cdev->gadget);
		if (!usb_desc)
			goto fail;
		usb_otg_descriptor_init(cdev->gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	status = usb_add_config(cdev, &audio_config_driver, audio_do_config);
	if (status < 0)
		goto fail_otg_desc;
	usb_composite_overwrite_options(cdev, &coverwrite);

	INFO(cdev, "%s, version: %s\n", DRIVER_DESC, DRIVER_VERSION);
	return 0;

fail_otg_desc:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
fail:
#ifndef CONFIG_GADGET_UAC1
	usb_put_function_instance(fi_uac2);
#else
	usb_put_function_instance(fi_uac1);
#endif
	return status;
}

static int audio_unbind(struct usb_composite_dev *cdev)
{
#ifdef CONFIG_GADGET_UAC1
	if (!IS_ERR_OR_NULL(f_uac1))
		usb_put_function(f_uac1);
	if (!IS_ERR_OR_NULL(fi_uac1))
		usb_put_function_instance(fi_uac1);
#else
	if (!IS_ERR_OR_NULL(f_uac2))
		usb_put_function(f_uac2);
	if (!IS_ERR_OR_NULL(fi_uac2))
		usb_put_function_instance(fi_uac2);
#endif
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}

static struct usb_composite_driver audio_driver = {
	.name		= "g_audio",
	.dev		= &device_desc,
	.strings	= audio_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= audio_bind,
	.unbind		= audio_unbind,
};

module_usb_composite_driver(audio_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Bryan Wu <cooloney@kernel.org>");
MODULE_LICENSE("GPL");

