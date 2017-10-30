/*
 * composite.c - infrastructure for Composite USB Gadgets
 *
 * Copyright (C) 2006-2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* #define VERBOSE_DEBUG */

#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/utsname.h>

#include <linux/usb/composite.h>
#include <linux/usb/otg.h>
#include <asm/unaligned.h>

#include "u_os_desc.h"

/**
 * struct usb_os_string - represents OS String to be reported by a gadget
 * @bLength: total length of the entire descritor, always 0x12
 * @bDescriptorType: USB_DT_STRING
 * @qwSignature: the OS String proper
 * @bMS_VendorCode: code used by the host for subsequent requests
 * @bPad: not used, must be zero
 */
struct usb_os_string {
	__u8	bLength;
	__u8	bDescriptorType;
	__u8	qwSignature[OS_STRING_QW_SIGN_LEN];
	__u8	bMS_VendorCode;
	__u8	bPad;
} __packed;

/*
 * The code in this file is utility code, used to build a gadget driver
 * from one or more "function" drivers, one or more "configuration"
 * objects, and a "usb_composite_driver" by gluing them together along
 * with the relevant device-wide data.
 */

static struct usb_gadget_strings **get_containers_gs(
		struct usb_gadget_string_container *uc)
{
	return (struct usb_gadget_strings **)uc->stash;
}

/**
 * function_descriptors() - get function descriptors for speed
 * @f: the function
 * @speed: the speed
 *
 * Returns the descriptors or NULL if not set.
 */
static struct usb_descriptor_header **
function_descriptors(struct usb_function *f,
		     enum usb_device_speed speed)
{
	struct usb_descriptor_header **descriptors;

	/*
	 * NOTE: we try to help gadget drivers which might not be setting
	 * max_speed appropriately.
	 */

	switch (speed) {
	case USB_SPEED_SUPER_PLUS:
		descriptors = f->ssp_descriptors;
		if (descriptors)
			break;
		/* FALLTHROUGH */
	case USB_SPEED_SUPER:
		descriptors = f->ss_descriptors;
		if (descriptors)
			break;
		/* FALLTHROUGH */
	case USB_SPEED_HIGH:
		descriptors = f->hs_descriptors;
		if (descriptors)
			break;
		/* FALLTHROUGH */
	default:
		descriptors = f->fs_descriptors;
	}

	/*
	 * if we can't find any descriptors at all, then this gadget deserves to
	 * Oops with a NULL pointer dereference
	 */

	return descriptors;
}

/**
 * next_ep_desc() - advance to the next EP descriptor
 * @t: currect pointer within descriptor array
 *
 * Return: next EP descriptor or NULL
 *
 * Iterate over @t until either EP descriptor found or
 * NULL (that indicates end of list) encountered
 */
static struct usb_descriptor_header**
next_ep_desc(struct usb_descriptor_header **t)
{
	for (; *t; t++) {
		if ((*t)->bDescriptorType == USB_DT_ENDPOINT)
			return t;
	}
	return NULL;
}

/*
 * for_each_ep_desc()- iterate over endpoint descriptors in the
 *		descriptors list
 * @start:	pointer within descriptor array.
 * @ep_desc:	endpoint descriptor to use as the loop cursor
 */
#define for_each_ep_desc(start, ep_desc) \
	for (ep_desc = next_ep_desc(start); \
	      ep_desc; ep_desc = next_ep_desc(ep_desc+1))

/**
 * config_ep_by_speed() - configures the given endpoint
 * according to gadget speed.
 * @g: pointer to the gadget
 * @f: usb function
 * @_ep: the endpoint to configure
 *
 * Return: error code, 0 on success
 *
 * This function chooses the right descriptors for a given
 * endpoint according to gadget speed and saves it in the
 * endpoint desc field. If the endpoint already has a descriptor
 * assigned to it - overwrites it with currently corresponding
 * descriptor. The endpoint maxpacket field is updated according
 * to the chosen descriptor.
 * Note: the supplied function should hold all the descriptors
 * for supported speeds
 */
int config_ep_by_speed(struct usb_gadget *g,
			struct usb_function *f,
			struct usb_ep *_ep)
{
	struct usb_composite_dev	*cdev = get_gadget_data(g);
	struct usb_endpoint_descriptor *chosen_desc = NULL;
	struct usb_descriptor_header **speed_desc = NULL;

	struct usb_ss_ep_comp_descriptor *comp_desc = NULL;
	int want_comp_desc = 0;

	struct usb_descriptor_header **d_spd; /* cursor for speed desc */

	if (!g || !f || !_ep)
		return -EIO;

	/* select desired speed */
	switch (g->speed) {
	case USB_SPEED_SUPER_PLUS:
		if (gadget_is_superspeed_plus(g)) {
			speed_desc = f->ssp_descriptors;
			want_comp_desc = 1;
			break;
		}
		/* else: Fall trough */
	case USB_SPEED_SUPER:
		if (gadget_is_superspeed(g)) {
			speed_desc = f->ss_descriptors;
			want_comp_desc = 1;
			break;
		}
		/* else: Fall trough */
	case USB_SPEED_HIGH:
		if (gadget_is_dualspeed(g)) {
			speed_desc = f->hs_descriptors;
			break;
		}
		/* else: fall through */
	default:
		speed_desc = f->fs_descriptors;
	}
	/* find descriptors */
	for_each_ep_desc(speed_desc, d_spd) {
		chosen_desc = (struct usb_endpoint_descriptor *)*d_spd;
		if (chosen_desc->bEndpointAddress == _ep->address)
			goto ep_found;
	}
	return -EIO;

ep_found:
	/* commit results */
	_ep->maxpacket = usb_endpoint_maxp(chosen_desc);
	_ep->desc = chosen_desc;
	_ep->comp_desc = NULL;
	_ep->maxburst = 0;
	_ep->mult = 1;

	if (g->speed == USB_SPEED_HIGH && (usb_endpoint_xfer_isoc(_ep->desc) ||
				usb_endpoint_xfer_int(_ep->desc)))
		_ep->mult = usb_endpoint_maxp_mult(_ep->desc);

	if (!want_comp_desc)
		return 0;

	/*
	 * Companion descriptor should follow EP descriptor
	 * USB 3.0 spec, #9.6.7
	 */
	comp_desc = (struct usb_ss_ep_comp_descriptor *)*(++d_spd);
	if (!comp_desc ||
	    (comp_desc->bDescriptorType != USB_DT_SS_ENDPOINT_COMP))
		return -EIO;
	_ep->comp_desc = comp_desc;
	if (g->speed >= USB_SPEED_SUPER) {
		switch (usb_endpoint_type(_ep->desc)) {
		case USB_ENDPOINT_XFER_ISOC:
			/* mult: bits 1:0 of bmAttributes */
			_ep->mult = (comp_desc->bmAttributes & 0x3) + 1;
		case USB_ENDPOINT_XFER_BULK:
		case USB_ENDPOINT_XFER_INT:
			_ep->maxburst = comp_desc->bMaxBurst + 1;
			break;
		default:
			if (comp_desc->bMaxBurst != 0)
				ERROR(cdev, "ep0 bMaxBurst must be 0\n");
			_ep->maxburst = 1;
			break;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(config_ep_by_speed);

/**
 * usb_add_function() - add a function to a configuration
 * @config: the configuration
 * @function: the function being added
 * Context: single threaded during gadget setup
 *
 * After initialization, each configuration must have one or more
 * functions added to it.  Adding a function involves calling its @bind()
 * method to allocate resources such as interface and string identifiers
 * and endpoints.
 *
 * This function returns the value of the function's bind(), which is
 * zero for success else a negative errno value.
 */
int usb_add_function(struct usb_configuration *config,
		struct usb_function *function)
{
	int	value = -EINVAL;

	DBG(config->cdev, "adding '%s'/%p to config '%s'/%p\n",
			function->name, function,
			config->label, config);

	if (!function->set_alt || !function->disable)
		goto done;

	function->config = config;
	list_add_tail(&function->list, &config->functions);

	if (function->bind_deactivated) {
		value = usb_function_deactivate(function);
		if (value)
			goto done;
	}

	/* REVISIT *require* function->bind? */
	if (function->bind) {
		value = function->bind(config, function);
		if (value < 0) {
			list_del(&function->list);
			function->config = NULL;
		}
	} else
		value = 0;

	/* We allow configurations that don't work at both speeds.
	 * If we run into a lowspeed Linux system, treat it the same
	 * as full speed ... it's the function drivers that will need
	 * to avoid bulk and ISO transfers.
	 */
	if (!config->fullspeed && function->fs_descriptors)
		config->fullspeed = true;
	if (!config->highspeed && function->hs_descriptors)
		config->highspeed = true;
	if (!config->superspeed && function->ss_descriptors)
		config->superspeed = true;
	if (!config->superspeed_plus && function->ssp_descriptors)
		config->superspeed_plus = true;

done:
	if (value)
		DBG(config->cdev, "adding '%s'/%p --> %d\n",
				function->name, function, value);
	return value;
}
EXPORT_SYMBOL_GPL(usb_add_function);

void usb_remove_function(struct usb_configuration *c, struct usb_function *f)
{
	if (f->disable)
		f->disable(f);

	bitmap_zero(f->endpoints, 32);
	list_del(&f->list);
	if (f->unbind)
		f->unbind(c, f);

	if (f->bind_deactivated)
		usb_function_activate(f);
}
EXPORT_SYMBOL_GPL(usb_remove_function);

/**
 * usb_function_deactivate - prevent function and gadget enumeration
 * @function: the function that isn't yet ready to respond
 *
 * Blocks response of the gadget driver to host enumeration by
 * preventing the data line pullup from being activated.  This is
 * normally called during @bind() processing to change from the
 * initial "ready to respond" state, or when a required resource
 * becomes available.
 *
 * For example, drivers that serve as a passthrough to a userspace
 * daemon can block enumeration unless that daemon (such as an OBEX,
 * MTP, or print server) is ready to handle host requests.
 *
 * Not all systems support software control of their USB peripheral
 * data pullups.
 *
 * Returns zero on success, else negative errno.
 */
int usb_function_deactivate(struct usb_function *function)
{
	struct usb_composite_dev	*cdev = function->config->cdev;
	unsigned long			flags;
	int				status = 0;

	spin_lock_irqsave(&cdev->lock, flags);

	if (cdev->deactivations == 0)
		status = usb_gadget_deactivate(cdev->gadget);
	if (status == 0)
		cdev->deactivations++;

	spin_unlock_irqrestore(&cdev->lock, flags);
	return status;
}
EXPORT_SYMBOL_GPL(usb_function_deactivate);

/**
 * usb_function_activate - allow function and gadget enumeration
 * @function: function on which usb_function_activate() was called
 *
 * Reverses effect of usb_function_deactivate().  If no more functions
 * are delaying their activation, the gadget driver will respond to
 * host enumeration procedures.
 *
 * Returns zero on success, else negative errno.
 */
int usb_function_activate(struct usb_function *function)
{
	struct usb_composite_dev	*cdev = function->config->cdev;
	unsigned long			flags;
	int				status = 0;

	spin_lock_irqsave(&cdev->lock, flags);

	if (WARN_ON(cdev->deactivations == 0))
		status = -EINVAL;
	else {
		cdev->deactivations--;
		if (cdev->deactivations == 0)
			status = usb_gadget_activate(cdev->gadget);
	}

	spin_unlock_irqrestore(&cdev->lock, flags);
	return status;
}
EXPORT_SYMBOL_GPL(usb_function_activate);

/**
 * usb_interface_id() - allocate an unused interface ID
 * @config: configuration associated with the interface
 * @function: function handling the interface
 * Context: single threaded during gadget setup
 *
 * usb_interface_id() is called from usb_function.bind() callbacks to
 * allocate new interface IDs.  The function driver will then store that
 * ID in interface, association, CDC union, and other descriptors.  It
 * will also handle any control requests targeted at that interface,
 * particularly changing its altsetting via set_alt().  There may
 * also be class-specific or vendor-specific requests to handle.
 *
 * All interface identifier should be allocated using this routine, to
 * ensure that for example different functions don't wrongly assign
 * different meanings to the same identifier.  Note that since interface
 * identifiers are configuration-specific, functions used in more than
 * one configuration (or more than once in a given configuration) need
 * multiple versions of the relevant descriptors.
 *
 * Returns the interface ID which was allocated; or -ENODEV if no
 * more interface IDs can be allocated.
 */
int usb_interface_id(struct usb_configuration *config,
		struct usb_function *function)
{
	unsigned id = config->next_interface_id;

	if (id < MAX_CONFIG_INTERFACES) {
		config->interface[id] = function;
		config->next_interface_id = id + 1;
		return id;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(usb_interface_id);

static u8 encode_bMaxPower(enum usb_device_speed speed,
		struct usb_configuration *c)
{
	unsigned val;

	if (c->MaxPower)
		val = c->MaxPower;
	else
		val = CONFIG_USB_GADGET_VBUS_DRAW;
	if (!val)
		return 0;
	switch (speed) {
	case USB_SPEED_SUPER:
		return DIV_ROUND_UP(val, 8);
	default:
		return DIV_ROUND_UP(val, 2);
	}
}

static int config_buf(struct usb_configuration *config,
		enum usb_device_speed speed, void *buf, u8 type)
{
	struct usb_config_descriptor	*c = buf;
	void				*next = buf + USB_DT_CONFIG_SIZE;
	int				len;
	struct usb_function		*f;
	int				status;

	len = USB_COMP_EP0_BUFSIZ - USB_DT_CONFIG_SIZE;
	/* write the config descriptor */
	c = buf;
	c->bLength = USB_DT_CONFIG_SIZE;
	c->bDescriptorType = type;
	/* wTotalLength is written later */
	c->bNumInterfaces = config->next_interface_id;
	c->bConfigurationValue = config->bConfigurationValue;
	c->iConfiguration = config->iConfiguration;
	c->bmAttributes = USB_CONFIG_ATT_ONE | config->bmAttributes;
	c->bMaxPower = encode_bMaxPower(speed, config);

	/* There may be e.g. OTG descriptors */
	if (config->descriptors) {
		status = usb_descriptor_fillbuf(next, len,
				config->descriptors);
		if (status < 0)
			return status;
		len -= status;
		next += status;
	}

	/* add each function's descriptors */
	list_for_each_entry(f, &config->functions, list) {
		struct usb_descriptor_header **descriptors;

		descriptors = function_descriptors(f, speed);
		if (!descriptors)
			continue;
		status = usb_descriptor_fillbuf(next, len,
			(const struct usb_descriptor_header **) descriptors);
		if (status < 0)
			return status;
		len -= status;
		next += status;
	}

	len = next - buf;
	c->wTotalLength = cpu_to_le16(len);
	return len;
}

static int config_desc(struct usb_composite_dev *cdev, unsigned w_value)
{
	struct usb_gadget		*gadget = cdev->gadget;
	struct usb_configuration	*c;
	struct list_head		*pos;
	u8				type = w_value >> 8;
	enum usb_device_speed		speed = USB_SPEED_UNKNOWN;

	if (gadget->speed >= USB_SPEED_SUPER)
		speed = gadget->speed;
	else if (gadget_is_dualspeed(gadget)) {
		int	hs = 0;
		if (gadget->speed == USB_SPEED_HIGH)
			hs = 1;
		if (type == USB_DT_OTHER_SPEED_CONFIG)
			hs = !hs;
		if (hs)
			speed = USB_SPEED_HIGH;

	}

	/* This is a lookup by config *INDEX* */
	w_value &= 0xff;

	pos = &cdev->configs;
	c = cdev->os_desc_config;
	if (c)
		goto check_config;

	while ((pos = pos->next) !=  &cdev->configs) {
		c = list_entry(pos, typeof(*c), list);

		/* skip OS Descriptors config which is handled separately */
		if (c == cdev->os_desc_config)
			continue;

check_config:
		/* ignore configs that won't work at this speed */
		switch (speed) {
		case USB_SPEED_SUPER_PLUS:
			if (!c->superspeed_plus)
				continue;
			break;
		case USB_SPEED_SUPER:
			if (!c->superspeed)
				continue;
			break;
		case USB_SPEED_HIGH:
			if (!c->highspeed)
				continue;
			break;
		default:
			if (!c->fullspeed)
				continue;
		}

		if (w_value == 0)
			return config_buf(c, speed, cdev->req->buf, type);
		w_value--;
	}
	return -EINVAL;
}

static int count_configs(struct usb_composite_dev *cdev, unsigned type)
{
	struct usb_gadget		*gadget = cdev->gadget;
	struct usb_configuration	*c;
	unsigned			count = 0;
	int				hs = 0;
	int				ss = 0;
	int				ssp = 0;

	if (gadget_is_dualspeed(gadget)) {
		if (gadget->speed == USB_SPEED_HIGH)
			hs = 1;
		if (gadget->speed == USB_SPEED_SUPER)
			ss = 1;
		if (gadget->speed == USB_SPEED_SUPER_PLUS)
			ssp = 1;
		if (type == USB_DT_DEVICE_QUALIFIER)
			hs = !hs;
	}
	list_for_each_entry(c, &cdev->configs, list) {
		/* ignore configs that won't work at this speed */
		if (ssp) {
			if (!c->superspeed_plus)
				continue;
		} else if (ss) {
			if (!c->superspeed)
				continue;
		} else if (hs) {
			if (!c->highspeed)
				continue;
		} else {
			if (!c->fullspeed)
				continue;
		}
		count++;
	}
	return count;
}

/**
 * bos_desc() - prepares the BOS descriptor.
 * @cdev: pointer to usb_composite device to generate the bos
 *	descriptor for
 *
 * This function generates the BOS (Binary Device Object)
 * descriptor and its device capabilities descriptors. The BOS
 * descriptor should be supported by a SuperSpeed device.
 */
static int bos_desc(struct usb_composite_dev *cdev)
{
	struct usb_ext_cap_descriptor	*usb_ext;
	struct usb_dcd_config_params	dcd_config_params;
	struct usb_bos_descriptor	*bos = cdev->req->buf;

	bos->bLength = USB_DT_BOS_SIZE;
	bos->bDescriptorType = USB_DT_BOS;

	bos->wTotalLength = cpu_to_le16(USB_DT_BOS_SIZE);
	bos->bNumDeviceCaps = 0;

	/*
	 * A SuperSpeed device shall include the USB2.0 extension descriptor
	 * and shall support LPM when operating in USB2.0 HS mode.
	 */
	usb_ext = cdev->req->buf + le16_to_cpu(bos->wTotalLength);
	bos->bNumDeviceCaps++;
	le16_add_cpu(&bos->wTotalLength, USB_DT_USB_EXT_CAP_SIZE);
	usb_ext->bLength = USB_DT_USB_EXT_CAP_SIZE;
	usb_ext->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
	usb_ext->bDevCapabilityType = USB_CAP_TYPE_EXT;
	usb_ext->bmAttributes = cpu_to_le32(USB_LPM_SUPPORT | USB_BESL_SUPPORT);

	/*
	 * The Superspeed USB Capability descriptor shall be implemented by all
	 * SuperSpeed devices.
	 */
	if (gadget_is_superspeed(cdev->gadget)) {
		struct usb_ss_cap_descriptor *ss_cap;

		ss_cap = cdev->req->buf + le16_to_cpu(bos->wTotalLength);
		bos->bNumDeviceCaps++;
		le16_add_cpu(&bos->wTotalLength, USB_DT_USB_SS_CAP_SIZE);
		ss_cap->bLength = USB_DT_USB_SS_CAP_SIZE;
		ss_cap->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
		ss_cap->bDevCapabilityType = USB_SS_CAP_TYPE;
		ss_cap->bmAttributes = 0; /* LTM is not supported yet */
		ss_cap->wSpeedSupported = cpu_to_le16(USB_LOW_SPEED_OPERATION |
						      USB_FULL_SPEED_OPERATION |
						      USB_HIGH_SPEED_OPERATION |
						      USB_5GBPS_OPERATION);
		ss_cap->bFunctionalitySupport = USB_LOW_SPEED_OPERATION;

		/* Get Controller configuration */
		if (cdev->gadget->ops->get_config_params) {
			cdev->gadget->ops->get_config_params(
				&dcd_config_params);
		} else {
			dcd_config_params.bU1devExitLat =
				USB_DEFAULT_U1_DEV_EXIT_LAT;
			dcd_config_params.bU2DevExitLat =
				cpu_to_le16(USB_DEFAULT_U2_DEV_EXIT_LAT);
		}
		ss_cap->bU1devExitLat = dcd_config_params.bU1devExitLat;
		ss_cap->bU2DevExitLat = dcd_config_params.bU2DevExitLat;
	}

	/* The SuperSpeedPlus USB Device Capability descriptor */
	if (gadget_is_superspeed_plus(cdev->gadget)) {
		struct usb_ssp_cap_descriptor *ssp_cap;

		ssp_cap = cdev->req->buf + le16_to_cpu(bos->wTotalLength);
		bos->bNumDeviceCaps++;

		/*
		 * Report typical values.
		 */

		le16_add_cpu(&bos->wTotalLength, USB_DT_USB_SSP_CAP_SIZE(1));
		ssp_cap->bLength = USB_DT_USB_SSP_CAP_SIZE(1);
		ssp_cap->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
		ssp_cap->bDevCapabilityType = USB_SSP_CAP_TYPE;
		ssp_cap->bReserved = 0;
		ssp_cap->wReserved = 0;

		/* SSAC = 1 (2 attributes) */
		ssp_cap->bmAttributes = cpu_to_le32(1);

		/* Min RX/TX Lane Count = 1 */
		ssp_cap->wFunctionalitySupport =
			cpu_to_le16((1 << 8) | (1 << 12));

		/*
		 * bmSublinkSpeedAttr[0]:
		 *   ST  = Symmetric, RX
		 *   LSE =  3 (Gbps)
		 *   LP  =  1 (SuperSpeedPlus)
		 *   LSM = 10 (10 Gbps)
		 */
		ssp_cap->bmSublinkSpeedAttr[0] =
			cpu_to_le32((3 << 4) | (1 << 14) | (0xa << 16));
		/*
		 * bmSublinkSpeedAttr[1] =
		 *   ST  = Symmetric, TX
		 *   LSE =  3 (Gbps)
		 *   LP  =  1 (SuperSpeedPlus)
		 *   LSM = 10 (10 Gbps)
		 */
		ssp_cap->bmSublinkSpeedAttr[1] =
			cpu_to_le32((3 << 4) | (1 << 14) |
				    (0xa << 16) | (1 << 7));
	}

	return le16_to_cpu(bos->wTotalLength);
}

static void device_qual(struct usb_composite_dev *cdev)
{
	struct usb_qualifier_descriptor	*qual = cdev->req->buf;

	qual->bLength = sizeof(*qual);
	qual->bDescriptorType = USB_DT_DEVICE_QUALIFIER;
	/* POLICY: same bcdUSB and device type info at both speeds */
	qual->bcdUSB = cdev->desc.bcdUSB;
	qual->bDeviceClass = cdev->desc.bDeviceClass;
	qual->bDeviceSubClass = cdev->desc.bDeviceSubClass;
	qual->bDeviceProtocol = cdev->desc.bDeviceProtocol;
	/* ASSUME same EP0 fifo size at both speeds */
	qual->bMaxPacketSize0 = cdev->gadget->ep0->maxpacket;
	qual->bNumConfigurations = count_configs(cdev, USB_DT_DEVICE_QUALIFIER);
	qual->bRESERVED = 0;
}

/*-------------------------------------------------------------------------*/

static void reset_config(struct usb_composite_dev *cdev)
{
	struct usb_function		*f;

	DBG(cdev, "reset config\n");

	list_for_each_entry(f, &cdev->config->functions, list) {
		if (f->disable)
			f->disable(f);

		bitmap_zero(f->endpoints, 32);
	}
	cdev->config = NULL;
	cdev->delayed_status = 0;
}

static int set_config(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl, unsigned number)
{
	struct usb_gadget	*gadget = cdev->gadget;
	struct usb_configuration *c = NULL;
	int			result = -EINVAL;
	unsigned		power = gadget_is_otg(gadget) ? 8 : 100;
	int			tmp;

	if (number) {
		list_for_each_entry(c, &cdev->configs, list) {
			if (c->bConfigurationValue == number) {
				/*
				 * We disable the FDs of the previous
				 * configuration only if the new configuration
				 * is a valid one
				 */
				if (cdev->config)
					reset_config(cdev);
				result = 0;
				break;
			}
		}
		if (result < 0)
			goto done;
	} else { /* Zero configuration value - need to reset the config */
		if (cdev->config)
			reset_config(cdev);
		result = 0;
	}

	INFO(cdev, "%s config #%d: %s\n",
	     usb_speed_string(gadget->speed),
	     number, c ? c->label : "unconfigured");

	if (!c)
		goto done;

	usb_gadget_set_state(gadget, USB_STATE_CONFIGURED);
	cdev->config = c;

	/* Initialize all interfaces by setting them to altsetting zero. */
	for (tmp = 0; tmp < MAX_CONFIG_INTERFACES; tmp++) {
		struct usb_function	*f = c->interface[tmp];
		struct usb_descriptor_header **descriptors;

		if (!f)
			break;

		/*
		 * Record which endpoints are used by the function. This is used
		 * to dispatch control requests targeted at that endpoint to the
		 * function's setup callback instead of the current
		 * configuration's setup callback.
		 */
		descriptors = function_descriptors(f, gadget->speed);

		for (; *descriptors; ++descriptors) {
			struct usb_endpoint_descriptor *ep;
			int addr;

			if ((*descriptors)->bDescriptorType != USB_DT_ENDPOINT)
				continue;

			ep = (struct usb_endpoint_descriptor *)*descriptors;
			addr = ((ep->bEndpointAddress & 0x80) >> 3)
			     |  (ep->bEndpointAddress & 0x0f);
			set_bit(addr, f->endpoints);
		}

		result = f->set_alt(f, tmp, 0);
		if (result < 0) {
			DBG(cdev, "interface %d (%s/%p) alt 0 --> %d\n",
					tmp, f->name, f, result);

			reset_config(cdev);
			goto done;
		}

		if (result == USB_GADGET_DELAYED_STATUS) {
			DBG(cdev,
			 "%s: interface %d (%s) requested delayed status\n",
					__func__, tmp, f->name);
			cdev->delayed_status++;
			DBG(cdev, "delayed_status count %d\n",
					cdev->delayed_status);
		}
	}

	/* when we return, be sure our power usage is valid */
	power = c->MaxPower ? c->MaxPower : CONFIG_USB_GADGET_VBUS_DRAW;
done:
	usb_gadget_vbus_draw(gadget, power);
	if (result >= 0 && cdev->delayed_status)
		result = USB_GADGET_DELAYED_STATUS;
	return result;
}

int usb_add_config_only(struct usb_composite_dev *cdev,
		struct usb_configuration *config)
{
	struct usb_configuration *c;

	if (!config->bConfigurationValue)
		return -EINVAL;

	/* Prevent duplicate configuration identifiers */
	list_for_each_entry(c, &cdev->configs, list) {
		if (c->bConfigurationValue == config->bConfigurationValue)
			return -EBUSY;
	}

	config->cdev = cdev;
	list_add_tail(&config->list, &cdev->configs);

	INIT_LIST_HEAD(&config->functions);
	config->next_interface_id = 0;
	memset(config->interface, 0, sizeof(config->interface));

	return 0;
}
EXPORT_SYMBOL_GPL(usb_add_config_only);

/**
 * usb_add_config() - add a configuration to a device.
 * @cdev: wraps the USB gadget
 * @config: the configuration, with bConfigurationValue assigned
 * @bind: the configuration's bind function
 * Context: single threaded during gadget setup
 *
 * One of the main tasks of a composite @bind() routine is to
 * add each of the configurations it supports, using this routine.
 *
 * This function returns the value of the configuration's @bind(), which
 * is zero for success else a negative errno value.  Binding configurations
 * assigns global resources including string IDs, and per-configuration
 * resources such as interface IDs and endpoints.
 */
int usb_add_config(struct usb_composite_dev *cdev,
		struct usb_configuration *config,
		int (*bind)(struct usb_configuration *))
{
	int				status = -EINVAL;

	if (!bind)
		goto done;

	DBG(cdev, "adding config #%u '%s'/%p\n",
			config->bConfigurationValue,
			config->label, config);

	status = usb_add_config_only(cdev, config);
	if (status)
		goto done;

	status = bind(config);
	if (status < 0) {
		while (!list_empty(&config->functions)) {
			struct usb_function		*f;

			f = list_first_entry(&config->functions,
					struct usb_function, list);
			list_del(&f->list);
			if (f->unbind) {
				DBG(cdev, "unbind function '%s'/%p\n",
					f->name, f);
				f->unbind(config, f);
				/* may free memory for "f" */
			}
		}
		list_del(&config->list);
		config->cdev = NULL;
	} else {
		unsigned	i;

		DBG(cdev, "cfg %d/%p speeds:%s%s%s%s\n",
			config->bConfigurationValue, config,
			config->superspeed_plus ? " superplus" : "",
			config->superspeed ? " super" : "",
			config->highspeed ? " high" : "",
			config->fullspeed
				? (gadget_is_dualspeed(cdev->gadget)
					? " full"
					: " full/low")
				: "");

		for (i = 0; i < MAX_CONFIG_INTERFACES; i++) {
			struct usb_function	*f = config->interface[i];

			if (!f)
				continue;
			DBG(cdev, "  interface %d = %s/%p\n",
				i, f->name, f);
		}
	}

	/* set_alt(), or next bind(), sets up ep->claimed as needed */
	usb_ep_autoconfig_reset(cdev->gadget);

done:
	if (status)
		DBG(cdev, "added config '%s'/%u --> %d\n", config->label,
				config->bConfigurationValue, status);
	return status;
}
EXPORT_SYMBOL_GPL(usb_add_config);

static void remove_config(struct usb_composite_dev *cdev,
			      struct usb_configuration *config)
{
	while (!list_empty(&config->functions)) {
		struct usb_function		*f;

		f = list_first_entry(&config->functions,
				struct usb_function, list);

		usb_remove_function(config, f);
	}
	list_del(&config->list);
	if (config->unbind) {
		DBG(cdev, "unbind config '%s'/%p\n", config->label, config);
		config->unbind(config);
			/* may free memory for "c" */
	}
}

/**
 * usb_remove_config() - remove a configuration from a device.
 * @cdev: wraps the USB gadget
 * @config: the configuration
 *
 * Drivers must call usb_gadget_disconnect before calling this function
 * to disconnect the device from the host and make sure the host will not
 * try to enumerate the device while we are changing the config list.
 */
void usb_remove_config(struct usb_composite_dev *cdev,
		      struct usb_configuration *config)
{
	unsigned long flags;

	spin_lock_irqsave(&cdev->lock, flags);

	if (cdev->config == config)
		reset_config(cdev);

	spin_unlock_irqrestore(&cdev->lock, flags);

	remove_config(cdev, config);
}

/*-------------------------------------------------------------------------*/

/* We support strings in multiple languages ... string descriptor zero
 * says which languages are supported.  The typical case will be that
 * only one language (probably English) is used, with i18n handled on
 * the host side.
 */

static void collect_langs(struct usb_gadget_strings **sp, __le16 *buf)
{
	const struct usb_gadget_strings	*s;
	__le16				language;
	__le16				*tmp;

	while (*sp) {
		s = *sp;
		language = cpu_to_le16(s->language);
		for (tmp = buf; *tmp && tmp < &buf[126]; tmp++) {
			if (*tmp == language)
				goto repeat;
		}
		*tmp++ = language;
repeat:
		sp++;
	}
}

static int lookup_string(
	struct usb_gadget_strings	**sp,
	void				*buf,
	u16				language,
	int				id
)
{
	struct usb_gadget_strings	*s;
	int				value;

	while (*sp) {
		s = *sp++;
		if (s->language != language)
			continue;
		value = usb_gadget_get_string(s, id, buf);
		if (value > 0)
			return value;
	}
	return -EINVAL;
}

static int get_string(struct usb_composite_dev *cdev,
		void *buf, u16 language, int id)
{
	struct usb_composite_driver	*composite = cdev->driver;
	struct usb_gadget_string_container *uc;
	struct usb_configuration	*c;
	struct usb_function		*f;
	int				len;

	/* Yes, not only is USB's i18n support probably more than most
	 * folk will ever care about ... also, it's all supported here.
	 * (Except for UTF8 support for Unicode's "Astral Planes".)
	 */

	/* 0 == report all available language codes */
	if (id == 0) {
		struct usb_string_descriptor	*s = buf;
		struct usb_gadget_strings	**sp;

		memset(s, 0, 256);
		s->bDescriptorType = USB_DT_STRING;

		sp = composite->strings;
		if (sp)
			collect_langs(sp, s->wData);

		list_for_each_entry(c, &cdev->configs, list) {
			sp = c->strings;
			if (sp)
				collect_langs(sp, s->wData);

			list_for_each_entry(f, &c->functions, list) {
				sp = f->strings;
				if (sp)
					collect_langs(sp, s->wData);
			}
		}
		list_for_each_entry(uc, &cdev->gstrings, list) {
			struct usb_gadget_strings **sp;

			sp = get_containers_gs(uc);
			collect_langs(sp, s->wData);
		}

		for (len = 0; len <= 126 && s->wData[len]; len++)
			continue;
		if (!len)
			return -EINVAL;

		s->bLength = 2 * (len + 1);
		return s->bLength;
	}

	if (cdev->use_os_string && language == 0 && id == OS_STRING_IDX) {
		struct usb_os_string *b = buf;
		b->bLength = sizeof(*b);
		b->bDescriptorType = USB_DT_STRING;
		compiletime_assert(
			sizeof(b->qwSignature) == sizeof(cdev->qw_sign),
			"qwSignature size must be equal to qw_sign");
		memcpy(&b->qwSignature, cdev->qw_sign, sizeof(b->qwSignature));
		b->bMS_VendorCode = cdev->b_vendor_code;
		b->bPad = 0;
		return sizeof(*b);
	}

	list_for_each_entry(uc, &cdev->gstrings, list) {
		struct usb_gadget_strings **sp;

		sp = get_containers_gs(uc);
		len = lookup_string(sp, buf, language, id);
		if (len > 0)
			return len;
	}

	/* String IDs are device-scoped, so we look up each string
	 * table we're told about.  These lookups are infrequent;
	 * simpler-is-better here.
	 */
	if (composite->strings) {
		len = lookup_string(composite->strings, buf, language, id);
		if (len > 0)
			return len;
	}
	list_for_each_entry(c, &cdev->configs, list) {
		if (c->strings) {
			len = lookup_string(c->strings, buf, language, id);
			if (len > 0)
				return len;
		}
		list_for_each_entry(f, &c->functions, list) {
			if (!f->strings)
				continue;
			len = lookup_string(f->strings, buf, language, id);
			if (len > 0)
				return len;
		}
	}
	return -EINVAL;
}

/**
 * usb_string_id() - allocate an unused string ID
 * @cdev: the device whose string descriptor IDs are being allocated
 * Context: single threaded during gadget setup
 *
 * @usb_string_id() is called from bind() callbacks to allocate
 * string IDs.  Drivers for functions, configurations, or gadgets will
 * then store that ID in the appropriate descriptors and string table.
 *
 * All string identifier should be allocated using this,
 * @usb_string_ids_tab() or @usb_string_ids_n() routine, to ensure
 * that for example different functions don't wrongly assign different
 * meanings to the same identifier.
 */
int usb_string_id(struct usb_composite_dev *cdev)
{
	if (cdev->next_string_id < 254) {
		/* string id 0 is reserved by USB spec for list of
		 * supported languages */
		/* 255 reserved as well? -- mina86 */
		cdev->next_string_id++;
		return cdev->next_string_id;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(usb_string_id);

/**
 * usb_string_ids() - allocate unused string IDs in batch
 * @cdev: the device whose string descriptor IDs are being allocated
 * @str: an array of usb_string objects to assign numbers to
 * Context: single threaded during gadget setup
 *
 * @usb_string_ids() is called from bind() callbacks to allocate
 * string IDs.  Drivers for functions, configurations, or gadgets will
 * then copy IDs from the string table to the appropriate descriptors
 * and string table for other languages.
 *
 * All string identifier should be allocated using this,
 * @usb_string_id() or @usb_string_ids_n() routine, to ensure that for
 * example different functions don't wrongly assign different meanings
 * to the same identifier.
 */
int usb_string_ids_tab(struct usb_composite_dev *cdev, struct usb_string *str)
{
	int next = cdev->next_string_id;

	for (; str->s; ++str) {
		if (unlikely(next >= 254))
			return -ENODEV;
		str->id = ++next;
	}

	cdev->next_string_id = next;

	return 0;
}
EXPORT_SYMBOL_GPL(usb_string_ids_tab);

static struct usb_gadget_string_container *copy_gadget_strings(
		struct usb_gadget_strings **sp, unsigned n_gstrings,
		unsigned n_strings)
{
	struct usb_gadget_string_container *uc;
	struct usb_gadget_strings **gs_array;
	struct usb_gadget_strings *gs;
	struct usb_string *s;
	unsigned mem;
	unsigned n_gs;
	unsigned n_s;
	void *stash;

	mem = sizeof(*uc);
	mem += sizeof(void *) * (n_gstrings + 1);
	mem += sizeof(struct usb_gadget_strings) * n_gstrings;
	mem += sizeof(struct usb_string) * (n_strings + 1) * (n_gstrings);
	uc = kmalloc(mem, GFP_KERNEL);
	if (!uc)
		return ERR_PTR(-ENOMEM);
	gs_array = get_containers_gs(uc);
	stash = uc->stash;
	stash += sizeof(void *) * (n_gstrings + 1);
	for (n_gs = 0; n_gs < n_gstrings; n_gs++) {
		struct usb_string *org_s;

		gs_array[n_gs] = stash;
		gs = gs_array[n_gs];
		stash += sizeof(struct usb_gadget_strings);
		gs->language = sp[n_gs]->language;
		gs->strings = stash;
		org_s = sp[n_gs]->strings;

		for (n_s = 0; n_s < n_strings; n_s++) {
			s = stash;
			stash += sizeof(struct usb_string);
			if (org_s->s)
				s->s = org_s->s;
			else
				s->s = "";
			org_s++;
		}
		s = stash;
		s->s = NULL;
		stash += sizeof(struct usb_string);

	}
	gs_array[n_gs] = NULL;
	return uc;
}

/**
 * usb_gstrings_attach() - attach gadget strings to a cdev and assign ids
 * @cdev: the device whose string descriptor IDs are being allocated
 * and attached.
 * @sp: an array of usb_gadget_strings to attach.
 * @n_strings: number of entries in each usb_strings array (sp[]->strings)
 *
 * This function will create a deep copy of usb_gadget_strings and usb_string
 * and attach it to the cdev. The actual string (usb_string.s) will not be
 * copied but only a referenced will be made. The struct usb_gadget_strings
 * array may contain multiple languages and should be NULL terminated.
 * The ->language pointer of each struct usb_gadget_strings has to contain the
 * same amount of entries.
 * For instance: sp[0] is en-US, sp[1] is es-ES. It is expected that the first
 * usb_string entry of es-ES contains the translation of the first usb_string
 * entry of en-US. Therefore both entries become the same id assign.
 */
struct usb_string *usb_gstrings_attach(struct usb_composite_dev *cdev,
		struct usb_gadget_strings **sp, unsigned n_strings)
{
	struct usb_gadget_string_container *uc;
	struct usb_gadget_strings **n_gs;
	unsigned n_gstrings = 0;
	unsigned i;
	int ret;

	for (i = 0; sp[i]; i++)
		n_gstrings++;

	if (!n_gstrings)
		return ERR_PTR(-EINVAL);

	uc = copy_gadget_strings(sp, n_gstrings, n_strings);
	if (IS_ERR(uc))
		return ERR_CAST(uc);

	n_gs = get_containers_gs(uc);
	ret = usb_string_ids_tab(cdev, n_gs[0]->strings);
	if (ret)
		goto err;

	for (i = 1; i < n_gstrings; i++) {
		struct usb_string *m_s;
		struct usb_string *s;
		unsigned n;

		m_s = n_gs[0]->strings;
		s = n_gs[i]->strings;
		for (n = 0; n < n_strings; n++) {
			s->id = m_s->id;
			s++;
			m_s++;
		}
	}
	list_add_tail(&uc->list, &cdev->gstrings);
	return n_gs[0]->strings;
err:
	kfree(uc);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(usb_gstrings_attach);

/**
 * usb_string_ids_n() - allocate unused string IDs in batch
 * @c: the device whose string descriptor IDs are being allocated
 * @n: number of string IDs to allocate
 * Context: single threaded during gadget setup
 *
 * Returns the first requested ID.  This ID and next @n-1 IDs are now
 * valid IDs.  At least provided that @n is non-zero because if it
 * is, returns last requested ID which is now very useful information.
 *
 * @usb_string_ids_n() is called from bind() callbacks to allocate
 * string IDs.  Drivers for functions, configurations, or gadgets will
 * then store that ID in the appropriate descriptors and string table.
 *
 * All string identifier should be allocated using this,
 * @usb_string_id() or @usb_string_ids_n() routine, to ensure that for
 * example different functions don't wrongly assign different meanings
 * to the same identifier.
 */
int usb_string_ids_n(struct usb_composite_dev *c, unsigned n)
{
	unsigned next = c->next_string_id;
	if (unlikely(n > 254 || (unsigned)next + n > 254))
		return -ENODEV;
	c->next_string_id += n;
	return next + 1;
}
EXPORT_SYMBOL_GPL(usb_string_ids_n);

/*-------------------------------------------------------------------------*/

static void composite_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_composite_dev *cdev;

	if (req->status || req->actual != req->length)
		DBG((struct usb_composite_dev *) ep->driver_data,
				"setup complete --> %d, %d/%d\n",
				req->status, req->actual, req->length);

	/*
	 * REVIST The same ep0 requests are shared with function drivers
	 * so they don't have to maintain the same ->complete() stubs.
	 *
	 * Because of that, we need to check for the validity of ->context
	 * here, even though we know we've set it to something useful.
	 */
	if (!req->context)
		return;

	cdev = req->context;

	if (cdev->req == req)
		cdev->setup_pending = false;
	else if (cdev->os_desc_req == req)
		cdev->os_desc_pending = false;
	else
		WARN(1, "unknown request %p\n", req);
}

static int composite_ep0_queue(struct usb_composite_dev *cdev,
		struct usb_request *req, gfp_t gfp_flags)
{
	int ret;

	ret = usb_ep_queue(cdev->gadget->ep0, req, gfp_flags);
	if (ret == 0) {
		if (cdev->req == req)
			cdev->setup_pending = true;
		else if (cdev->os_desc_req == req)
			cdev->os_desc_pending = true;
		else
			WARN(1, "unknown request %p\n", req);
	}

	return ret;
}

static int count_ext_compat(struct usb_configuration *c)
{
	int i, res;

	res = 0;
	for (i = 0; i < c->next_interface_id; ++i) {
		struct usb_function *f;
		int j;

		f = c->interface[i];
		for (j = 0; j < f->os_desc_n; ++j) {
			struct usb_os_desc *d;

			if (i != f->os_desc_table[j].if_id)
				continue;
			d = f->os_desc_table[j].os_desc;
			if (d && d->ext_compat_id)
				++res;
		}
	}
	BUG_ON(res > 255);
	return res;
}

static void fill_ext_compat(struct usb_configuration *c, u8 *buf)
{
	int i, count;

	count = 16;
	for (i = 0; i < c->next_interface_id; ++i) {
		struct usb_function *f;
		int j;

		f = c->interface[i];
		for (j = 0; j < f->os_desc_n; ++j) {
			struct usb_os_desc *d;

			if (i != f->os_desc_table[j].if_id)
				continue;
			d = f->os_desc_table[j].os_desc;
			if (d && d->ext_compat_id) {
				*buf++ = i;
				*buf++ = 0x01;
				memcpy(buf, d->ext_compat_id, 16);
				buf += 22;
			} else {
				++buf;
				*buf = 0x01;
				buf += 23;
			}
			count += 24;
			if (count >= 4096)
				return;
		}
	}
}

static int count_ext_prop(struct usb_configuration *c, int interface)
{
	struct usb_function *f;
	int j;

	f = c->interface[interface];
	for (j = 0; j < f->os_desc_n; ++j) {
		struct usb_os_desc *d;

		if (interface != f->os_desc_table[j].if_id)
			continue;
		d = f->os_desc_table[j].os_desc;
		if (d && d->ext_compat_id)
			return d->ext_prop_count;
	}
	return 0;
}

static int len_ext_prop(struct usb_configuration *c, int interface)
{
	struct usb_function *f;
	struct usb_os_desc *d;
	int j, res;

	res = 10; /* header length */
	f = c->interface[interface];
	for (j = 0; j < f->os_desc_n; ++j) {
		if (interface != f->os_desc_table[j].if_id)
			continue;
		d = f->os_desc_table[j].os_desc;
		if (d)
			return min(res + d->ext_prop_len, 4096);
	}
	return res;
}

static int fill_ext_prop(struct usb_configuration *c, int interface, u8 *buf)
{
	struct usb_function *f;
	struct usb_os_desc *d;
	struct usb_os_desc_ext_prop *ext_prop;
	int j, count, n, ret;
	u8 *start = buf;

	f = c->interface[interface];
	for (j = 0; j < f->os_desc_n; ++j) {
		if (interface != f->os_desc_table[j].if_id)
			continue;
		d = f->os_desc_table[j].os_desc;
		if (d)
			list_for_each_entry(ext_prop, &d->ext_prop, entry) {
				/* 4kB minus header length */
				n = buf - start;
				if (n >= 4086)
					return 0;

				count = ext_prop->data_len +
					ext_prop->name_len + 14;
				if (count > 4086 - n)
					return -EINVAL;
				usb_ext_prop_put_size(buf, count);
				usb_ext_prop_put_type(buf, ext_prop->type);
				ret = usb_ext_prop_put_name(buf, ext_prop->name,
							    ext_prop->name_len);
				if (ret < 0)
					return ret;
				switch (ext_prop->type) {
				case USB_EXT_PROP_UNICODE:
				case USB_EXT_PROP_UNICODE_ENV:
				case USB_EXT_PROP_UNICODE_LINK:
					usb_ext_prop_put_unicode(buf, ret,
							 ext_prop->data,
							 ext_prop->data_len);
					break;
				case USB_EXT_PROP_BINARY:
					usb_ext_prop_put_binary(buf, ret,
							ext_prop->data,
							ext_prop->data_len);
					break;
				case USB_EXT_PROP_LE32:
					/* not implemented */
				case USB_EXT_PROP_BE32:
					/* not implemented */
				default:
					return -EINVAL;
				}
				buf += count;
			}
	}

	return 0;
}

/*
 * The setup() callback implements all the ep0 functionality that's
 * not handled lower down, in hardware or the hardware driver(like
 * device and endpoint feature flags, and their status).  It's all
 * housekeeping for the gadget function we're implementing.  Most of
 * the work is in config and function specific setup.
 */
int
composite_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	struct usb_request		*req = cdev->req;
	int				value = -EOPNOTSUPP;
	int				status = 0;
	u16				w_index = le16_to_cpu(ctrl->wIndex);
	u8				intf = w_index & 0xFF;
	u16				w_value = le16_to_cpu(ctrl->wValue);
	u16				w_length = le16_to_cpu(ctrl->wLength);
	struct usb_function		*f = NULL;
	u8				endp;

	/* partial re-init of the response message; the function or the
	 * gadget might need to intercept e.g. a control-OUT completion
	 * when we delegate to it.
	 */
	req->zero = 0;
	req->context = cdev;
	req->complete = composite_setup_complete;
	req->length = 0;
	gadget->ep0->driver_data = cdev;

	/*
	 * Don't let non-standard requests match any of the cases below
	 * by accident.
	 */
	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD)
		goto unknown;

	switch (ctrl->bRequest) {

	/* we handle all standard USB descriptors */
	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unknown;
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			cdev->desc.bNumConfigurations =
				count_configs(cdev, USB_DT_DEVICE);
			cdev->desc.bMaxPacketSize0 =
				cdev->gadget->ep0->maxpacket;
			if (gadget_is_superspeed(gadget)) {
				if (gadget->speed >= USB_SPEED_SUPER) {
					cdev->desc.bcdUSB = cpu_to_le16(0x0310);
					cdev->desc.bMaxPacketSize0 = 9;
				} else {
					cdev->desc.bcdUSB = cpu_to_le16(0x0210);
				}
			} else {
				if (gadget->lpm_capable)
					cdev->desc.bcdUSB = cpu_to_le16(0x0201);
				else
					cdev->desc.bcdUSB = cpu_to_le16(0x0200);
			}

			value = min(w_length, (u16) sizeof cdev->desc);
			memcpy(req->buf, &cdev->desc, value);
			break;
		case USB_DT_DEVICE_QUALIFIER:
			if (!gadget_is_dualspeed(gadget) ||
			    gadget->speed >= USB_SPEED_SUPER)
				break;
			device_qual(cdev);
			value = min_t(int, w_length,
				sizeof(struct usb_qualifier_descriptor));
			break;
		case USB_DT_OTHER_SPEED_CONFIG:
			if (!gadget_is_dualspeed(gadget) ||
			    gadget->speed >= USB_SPEED_SUPER)
				break;
			/* FALLTHROUGH */
		case USB_DT_CONFIG:
			value = config_desc(cdev, w_value);
			if (value >= 0)
				value = min(w_length, (u16) value);
			break;
		case USB_DT_STRING:
			value = get_string(cdev, req->buf,
					w_index, w_value & 0xff);
			if (value >= 0)
				value = min(w_length, (u16) value);
			break;
		case USB_DT_BOS:
			if (gadget_is_superspeed(gadget) ||
			    gadget->lpm_capable) {
				value = bos_desc(cdev);
				value = min(w_length, (u16) value);
			}
			break;
		case USB_DT_OTG:
			if (gadget_is_otg(gadget)) {
				struct usb_configuration *config;
				int otg_desc_len = 0;

				if (cdev->config)
					config = cdev->config;
				else
					config = list_first_entry(
							&cdev->configs,
						struct usb_configuration, list);
				if (!config)
					goto done;

				if (gadget->otg_caps &&
					(gadget->otg_caps->otg_rev >= 0x0200))
					otg_desc_len += sizeof(
						struct usb_otg20_descriptor);
				else
					otg_desc_len += sizeof(
						struct usb_otg_descriptor);

				value = min_t(int, w_length, otg_desc_len);
				memcpy(req->buf, config->descriptors[0], value);
			}
			break;
		}
		break;

	/* any number of configs can work */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			goto unknown;
		if (gadget_is_otg(gadget)) {
			if (gadget->a_hnp_support)
				DBG(cdev, "HNP available\n");
			else if (gadget->a_alt_hnp_support)
				DBG(cdev, "HNP on another port\n");
			else
				VDBG(cdev, "HNP inactive\n");
		}
		spin_lock(&cdev->lock);
		value = set_config(cdev, ctrl, w_value);
		spin_unlock(&cdev->lock);
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unknown;
		if (cdev->config)
			*(u8 *)req->buf = cdev->config->bConfigurationValue;
		else
			*(u8 *)req->buf = 0;
		value = min(w_length, (u16) 1);
		break;

	/* function drivers must handle get/set altsetting */
	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE)
			goto unknown;
		if (!cdev->config || intf >= MAX_CONFIG_INTERFACES)
			break;
		f = cdev->config->interface[intf];
		if (!f)
			break;

		/*
		 * If there's no get_alt() method, we know only altsetting zero
		 * works. There is no need to check if set_alt() is not NULL
		 * as we check this in usb_add_function().
		 */
		if (w_value && !f->get_alt)
			break;
		value = f->set_alt(f, w_index, w_value);
		if (value == USB_GADGET_DELAYED_STATUS) {
			DBG(cdev,
			 "%s: interface %d (%s) requested delayed status\n",
					__func__, intf, f->name);
			cdev->delayed_status++;
			DBG(cdev, "delayed_status count %d\n",
					cdev->delayed_status);
		}
		break;
	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE))
			goto unknown;
		if (!cdev->config || intf >= MAX_CONFIG_INTERFACES)
			break;
		f = cdev->config->interface[intf];
		if (!f)
			break;
		/* lots of interfaces only need altsetting zero... */
		value = f->get_alt ? f->get_alt(f, w_index) : 0;
		if (value < 0)
			break;
		*((u8 *)req->buf) = value;
		value = min(w_length, (u16) 1);
		break;
	case USB_REQ_GET_STATUS:
		if (gadget_is_otg(gadget) && gadget->hnp_polling_support &&
						(w_index == OTG_STS_SELECTOR)) {
			if (ctrl->bRequestType != (USB_DIR_IN |
							USB_RECIP_DEVICE))
				goto unknown;
			*((u8 *)req->buf) = gadget->host_request_flag;
			value = 1;
			break;
		}

		/*
		 * USB 3.0 additions:
		 * Function driver should handle get_status request. If such cb
		 * wasn't supplied we respond with default value = 0
		 * Note: function driver should supply such cb only for the
		 * first interface of the function
		 */
		if (!gadget_is_superspeed(gadget))
			goto unknown;
		if (ctrl->bRequestType != (USB_DIR_IN | USB_RECIP_INTERFACE))
			goto unknown;
		value = 2;	/* This is the length of the get_status reply */
		put_unaligned_le16(0, req->buf);
		if (!cdev->config || intf >= MAX_CONFIG_INTERFACES)
			break;
		f = cdev->config->interface[intf];
		if (!f)
			break;
		status = f->get_status ? f->get_status(f) : 0;
		if (status < 0)
			break;
		put_unaligned_le16(status & 0x0000ffff, req->buf);
		break;
	/*
	 * Function drivers should handle SetFeature/ClearFeature
	 * (FUNCTION_SUSPEND) request. function_suspend cb should be supplied
	 * only for the first interface of the function
	 */
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		if (!gadget_is_superspeed(gadget))
			goto unknown;
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_RECIP_INTERFACE))
			goto unknown;
		switch (w_value) {
		case USB_INTRF_FUNC_SUSPEND:
			if (!cdev->config || intf >= MAX_CONFIG_INTERFACES)
				break;
			f = cdev->config->interface[intf];
			if (!f)
				break;
			value = 0;
			if (f->func_suspend)
				value = f->func_suspend(f, w_index >> 8);
			if (value < 0) {
				ERROR(cdev,
				      "func_suspend() returned error %d\n",
				      value);
				value = 0;
			}
			break;
		}
		break;
	default:
unknown:
		/*
		 * OS descriptors handling
		 */
		if (cdev->use_os_string && cdev->os_desc_config &&
		    (ctrl->bRequestType & USB_TYPE_VENDOR) &&
		    ctrl->bRequest == cdev->b_vendor_code) {
			struct usb_request		*req;
			struct usb_configuration	*os_desc_cfg;
			u8				*buf;
			int				interface;
			int				count = 0;

			req = cdev->os_desc_req;
			req->context = cdev;
			req->complete = composite_setup_complete;
			buf = req->buf;
			os_desc_cfg = cdev->os_desc_config;
			memset(buf, 0, w_length);
			buf[5] = 0x01;
			switch (ctrl->bRequestType & USB_RECIP_MASK) {
			case USB_RECIP_DEVICE:
				if (w_index != 0x4 || (w_value >> 8))
					break;
				buf[6] = w_index;
				if (w_length == 0x10) {
					/* Number of ext compat interfaces */
					count = count_ext_compat(os_desc_cfg);
					buf[8] = count;
					count *= 24; /* 24 B/ext compat desc */
					count += 16; /* header */
					put_unaligned_le32(count, buf);
					value = w_length;
				} else {
					/* "extended compatibility ID"s */
					count = count_ext_compat(os_desc_cfg);
					buf[8] = count;
					count *= 24; /* 24 B/ext compat desc */
					count += 16; /* header */
					put_unaligned_le32(count, buf);
					buf += 16;
					fill_ext_compat(os_desc_cfg, buf);
					value = w_length;
				}
				break;
			case USB_RECIP_INTERFACE:
				if (w_index != 0x5 || (w_value >> 8))
					break;
				interface = w_value & 0xFF;
				buf[6] = w_index;
				if (w_length == 0x0A) {
					count = count_ext_prop(os_desc_cfg,
						interface);
					put_unaligned_le16(count, buf + 8);
					count = len_ext_prop(os_desc_cfg,
						interface);
					put_unaligned_le32(count, buf);

					value = w_length;
				} else {
					count = count_ext_prop(os_desc_cfg,
						interface);
					put_unaligned_le16(count, buf + 8);
					count = len_ext_prop(os_desc_cfg,
						interface);
					put_unaligned_le32(count, buf);
					buf += 10;
					value = fill_ext_prop(os_desc_cfg,
							      interface, buf);
					if (value < 0)
						return value;

					value = w_length;
				}
				break;
			}

			if (value >= 0) {
				req->length = value;
				req->context = cdev;
				req->zero = value < w_length;
				value = composite_ep0_queue(cdev, req,
							    GFP_ATOMIC);
				if (value < 0) {
					DBG(cdev, "ep_queue --> %d\n", value);
					req->status = 0;
					composite_setup_complete(gadget->ep0,
								 req);
				}
			}
			return value;
		}

		VDBG(cdev,
			"non-core control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);

		/* functions always handle their interfaces and endpoints...
		 * punt other recipients (other, WUSB, ...) to the current
		 * configuration code.
		 */
		if (cdev->config) {
			list_for_each_entry(f, &cdev->config->functions, list)
				if (f->req_match &&
				    f->req_match(f, ctrl, false))
					goto try_fun_setup;
		} else {
			struct usb_configuration *c;
			list_for_each_entry(c, &cdev->configs, list)
				list_for_each_entry(f, &c->functions, list)
					if (f->req_match &&
					    f->req_match(f, ctrl, true))
						goto try_fun_setup;
		}
		f = NULL;

		switch (ctrl->bRequestType & USB_RECIP_MASK) {
		case USB_RECIP_INTERFACE:
			if (!cdev->config || intf >= MAX_CONFIG_INTERFACES)
				break;
			f = cdev->config->interface[intf];
			break;

		case USB_RECIP_ENDPOINT:
			if (!cdev->config)
				break;
			endp = ((w_index & 0x80) >> 3) | (w_index & 0x0f);
			list_for_each_entry(f, &cdev->config->functions, list) {
				if (test_bit(endp, f->endpoints))
					break;
			}
			if (&f->list == &cdev->config->functions)
				f = NULL;
			break;
		}
try_fun_setup:
		if (f && f->setup)
			value = f->setup(f, ctrl);
		else {
			struct usb_configuration	*c;

			c = cdev->config;
			if (!c)
				goto done;

			/* try current config's setup */
			if (c->setup) {
				value = c->setup(c, ctrl);
				goto done;
			}

			/* try the only function in the current config */
			if (!list_is_singular(&c->functions))
				goto done;
			f = list_first_entry(&c->functions, struct usb_function,
					     list);
			if (f->setup)
				value = f->setup(f, ctrl);
		}

		goto done;
	}

	/* respond with data transfer before status phase? */
	if (value >= 0 && value != USB_GADGET_DELAYED_STATUS) {
		req->length = value;
		req->context = cdev;
		req->zero = value < w_length;
		value = composite_ep0_queue(cdev, req, GFP_ATOMIC);
		if (value < 0) {
			DBG(cdev, "ep_queue --> %d\n", value);
			req->status = 0;
			composite_setup_complete(gadget->ep0, req);
		}
	} else if (value == USB_GADGET_DELAYED_STATUS && w_length != 0) {
		WARN(cdev,
			"%s: Delayed status not supported for w_length != 0",
			__func__);
	}

done:
	/* device either stalls (value < 0) or reports success */
	return value;
}

void composite_disconnect(struct usb_gadget *gadget)
{
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	unsigned long			flags;

	/* REVISIT:  should we have config and device level
	 * disconnect callbacks?
	 */
	spin_lock_irqsave(&cdev->lock, flags);
	if (cdev->config)
		reset_config(cdev);
	if (cdev->driver->disconnect)
		cdev->driver->disconnect(cdev);
	spin_unlock_irqrestore(&cdev->lock, flags);
}

/*-------------------------------------------------------------------------*/

static ssize_t suspended_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct usb_gadget *gadget = dev_to_usb_gadget(dev);
	struct usb_composite_dev *cdev = get_gadget_data(gadget);

	return sprintf(buf, "%d\n", cdev->suspended);
}
static DEVICE_ATTR_RO(suspended);

static void __composite_unbind(struct usb_gadget *gadget, bool unbind_driver)
{
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	struct usb_gadget_strings	*gstr = cdev->driver->strings[0];
	struct usb_string		*dev_str = gstr->strings;

	/* composite_disconnect() must already have been called
	 * by the underlying peripheral controller driver!
	 * so there's no i/o concurrency that could affect the
	 * state protected by cdev->lock.
	 */
	WARN_ON(cdev->config);

	while (!list_empty(&cdev->configs)) {
		struct usb_configuration	*c;
		c = list_first_entry(&cdev->configs,
				struct usb_configuration, list);
		remove_config(cdev, c);
	}
	if (cdev->driver->unbind && unbind_driver)
		cdev->driver->unbind(cdev);

	composite_dev_cleanup(cdev);

	if (dev_str[USB_GADGET_MANUFACTURER_IDX].s == cdev->def_manufacturer)
		dev_str[USB_GADGET_MANUFACTURER_IDX].s = "";

	kfree(cdev->def_manufacturer);
	kfree(cdev);
	set_gadget_data(gadget, NULL);
}

static void composite_unbind(struct usb_gadget *gadget)
{
	__composite_unbind(gadget, true);
}

static void update_unchanged_dev_desc(struct usb_device_descriptor *new,
		const struct usb_device_descriptor *old)
{
	__le16 idVendor;
	__le16 idProduct;
	__le16 bcdDevice;
	u8 iSerialNumber;
	u8 iManufacturer;
	u8 iProduct;

	/*
	 * these variables may have been set in
	 * usb_composite_overwrite_options()
	 */
	idVendor = new->idVendor;
	idProduct = new->idProduct;
	bcdDevice = new->bcdDevice;
	iSerialNumber = new->iSerialNumber;
	iManufacturer = new->iManufacturer;
	iProduct = new->iProduct;

	*new = *old;
	if (idVendor)
		new->idVendor = idVendor;
	if (idProduct)
		new->idProduct = idProduct;
	if (bcdDevice)
		new->bcdDevice = bcdDevice;
	else
		new->bcdDevice = cpu_to_le16(get_default_bcdDevice());
	if (iSerialNumber)
		new->iSerialNumber = iSerialNumber;
	if (iManufacturer)
		new->iManufacturer = iManufacturer;
	if (iProduct)
		new->iProduct = iProduct;
}

int composite_dev_prepare(struct usb_composite_driver *composite,
		struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int ret = -ENOMEM;

	/* preallocate control response and buffer */
	cdev->req = usb_ep_alloc_request(gadget->ep0, GFP_KERNEL);
	if (!cdev->req)
		return -ENOMEM;

	cdev->req->buf = kmalloc(USB_COMP_EP0_BUFSIZ, GFP_KERNEL);
	if (!cdev->req->buf)
		goto fail;

	ret = device_create_file(&gadget->dev, &dev_attr_suspended);
	if (ret)
		goto fail_dev;

	cdev->req->complete = composite_setup_complete;
	cdev->req->context = cdev;
	gadget->ep0->driver_data = cdev;

	cdev->driver = composite;

	/*
	 * As per USB compliance update, a device that is actively drawing
	 * more than 100mA from USB must report itself as bus-powered in
	 * the GetStatus(DEVICE) call.
	 */
	if (CONFIG_USB_GADGET_VBUS_DRAW <= USB_SELF_POWER_VBUS_MAX_DRAW)
		usb_gadget_set_selfpowered(gadget);

	/* interface and string IDs start at zero via kzalloc.
	 * we force endpoints to start unassigned; few controller
	 * drivers will zero ep->driver_data.
	 */
	usb_ep_autoconfig_reset(gadget);
	return 0;
fail_dev:
	kfree(cdev->req->buf);
fail:
	usb_ep_free_request(gadget->ep0, cdev->req);
	cdev->req = NULL;
	return ret;
}

int composite_os_desc_req_prepare(struct usb_composite_dev *cdev,
				  struct usb_ep *ep0)
{
	int ret = 0;

	cdev->os_desc_req = usb_ep_alloc_request(ep0, GFP_KERNEL);
	if (!cdev->os_desc_req) {
		ret = -ENOMEM;
		goto end;
	}

	/* OS feature descriptor length <= 4kB */
	cdev->os_desc_req->buf = kmalloc(4096, GFP_KERNEL);
	if (!cdev->os_desc_req->buf) {
		ret = -ENOMEM;
		usb_ep_free_request(ep0, cdev->os_desc_req);
		goto end;
	}
	cdev->os_desc_req->context = cdev;
	cdev->os_desc_req->complete = composite_setup_complete;
end:
	return ret;
}

void composite_dev_cleanup(struct usb_composite_dev *cdev)
{
	struct usb_gadget_string_container *uc, *tmp;

	list_for_each_entry_safe(uc, tmp, &cdev->gstrings, list) {
		list_del(&uc->list);
		kfree(uc);
	}
	if (cdev->os_desc_req) {
		if (cdev->os_desc_pending)
			usb_ep_dequeue(cdev->gadget->ep0, cdev->os_desc_req);

		kfree(cdev->os_desc_req->buf);
		usb_ep_free_request(cdev->gadget->ep0, cdev->os_desc_req);
	}
	if (cdev->req) {
		if (cdev->setup_pending)
			usb_ep_dequeue(cdev->gadget->ep0, cdev->req);

		kfree(cdev->req->buf);
		usb_ep_free_request(cdev->gadget->ep0, cdev->req);
	}
	cdev->next_string_id = 0;
	device_remove_file(&cdev->gadget->dev, &dev_attr_suspended);
}

static int composite_bind(struct usb_gadget *gadget,
		struct usb_gadget_driver *gdriver)
{
	struct usb_composite_dev	*cdev;
	struct usb_composite_driver	*composite = to_cdriver(gdriver);
	int				status = -ENOMEM;

	cdev = kzalloc(sizeof *cdev, GFP_KERNEL);
	if (!cdev)
		return status;

	spin_lock_init(&cdev->lock);
	cdev->gadget = gadget;
	set_gadget_data(gadget, cdev);
	INIT_LIST_HEAD(&cdev->configs);
	INIT_LIST_HEAD(&cdev->gstrings);

	status = composite_dev_prepare(composite, cdev);
	if (status)
		goto fail;

	/* composite gadget needs to assign strings for whole device (like
	 * serial number), register function drivers, potentially update
	 * power state and consumption, etc
	 */
	status = composite->bind(cdev);
	if (status < 0)
		goto fail;

	if (cdev->use_os_string) {
		status = composite_os_desc_req_prepare(cdev, gadget->ep0);
		if (status)
			goto fail;
	}

	update_unchanged_dev_desc(&cdev->desc, composite->dev);

	/* has userspace failed to provide a serial number? */
	if (composite->needs_serial && !cdev->desc.iSerialNumber)
		WARNING(cdev, "userspace failed to provide iSerialNumber\n");

	INFO(cdev, "%s ready\n", composite->name);
	return 0;

fail:
	__composite_unbind(gadget, false);
	return status;
}

/*-------------------------------------------------------------------------*/

void composite_suspend(struct usb_gadget *gadget)
{
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	struct usb_function		*f;

	/* REVISIT:  should we have config level
	 * suspend/resume callbacks?
	 */
	DBG(cdev, "suspend\n");
	if (cdev->config) {
		list_for_each_entry(f, &cdev->config->functions, list) {
			if (f->suspend)
				f->suspend(f);
		}
	}
	if (cdev->driver->suspend)
		cdev->driver->suspend(cdev);

	cdev->suspended = 1;

	usb_gadget_vbus_draw(gadget, 2);
}

void composite_resume(struct usb_gadget *gadget)
{
	struct usb_composite_dev	*cdev = get_gadget_data(gadget);
	struct usb_function		*f;
	u16				maxpower;

	/* REVISIT:  should we have config level
	 * suspend/resume callbacks?
	 */
	DBG(cdev, "resume\n");
	if (cdev->driver->resume)
		cdev->driver->resume(cdev);
	if (cdev->config) {
		list_for_each_entry(f, &cdev->config->functions, list) {
			if (f->resume)
				f->resume(f);
		}

		maxpower = cdev->config->MaxPower;

		usb_gadget_vbus_draw(gadget, maxpower ?
			maxpower : CONFIG_USB_GADGET_VBUS_DRAW);
	}

	cdev->suspended = 0;
}

/*-------------------------------------------------------------------------*/

static const struct usb_gadget_driver composite_driver_template = {
	.bind		= composite_bind,
	.unbind		= composite_unbind,

	.setup		= composite_setup,
	.reset		= composite_disconnect,
	.disconnect	= composite_disconnect,

	.suspend	= composite_suspend,
	.resume		= composite_resume,

	.driver	= {
		.owner		= THIS_MODULE,
	},
};

/**
 * usb_composite_probe() - register a composite driver
 * @driver: the driver to register
 *
 * Context: single threaded during gadget setup
 *
 * This function is used to register drivers using the composite driver
 * framework.  The return value is zero, or a negative errno value.
 * Those values normally come from the driver's @bind method, which does
 * all the work of setting up the driver to match the hardware.
 *
 * On successful return, the gadget is ready to respond to requests from
 * the host, unless one of its components invokes usb_gadget_disconnect()
 * while it was binding.  That would usually be done in order to wait for
 * some userspace participation.
 */
int usb_composite_probe(struct usb_composite_driver *driver)
{
	struct usb_gadget_driver *gadget_driver;

	if (!driver || !driver->dev || !driver->bind)
		return -EINVAL;

	if (!driver->name)
		driver->name = "composite";

	driver->gadget_driver = composite_driver_template;
	gadget_driver = &driver->gadget_driver;

	gadget_driver->function =  (char *) driver->name;
	gadget_driver->driver.name = driver->name;
	gadget_driver->max_speed = driver->max_speed;

	return usb_gadget_probe_driver(gadget_driver);
}
EXPORT_SYMBOL_GPL(usb_composite_probe);

/**
 * usb_composite_unregister() - unregister a composite driver
 * @driver: the driver to unregister
 *
 * This function is used to unregister drivers using the composite
 * driver framework.
 */
void usb_composite_unregister(struct usb_composite_driver *driver)
{
	usb_gadget_unregister_driver(&driver->gadget_driver);
}
EXPORT_SYMBOL_GPL(usb_composite_unregister);

/**
 * usb_composite_setup_continue() - Continue with the control transfer
 * @cdev: the composite device who's control transfer was kept waiting
 *
 * This function must be called by the USB function driver to continue
 * with the control transfer's data/status stage in case it had requested to
 * delay the data/status stages. A USB function's setup handler (e.g. set_alt())
 * can request the composite framework to delay the setup request's data/status
 * stages by returning USB_GADGET_DELAYED_STATUS.
 */
void usb_composite_setup_continue(struct usb_composite_dev *cdev)
{
	int			value;
	struct usb_request	*req = cdev->req;
	unsigned long		flags;

	DBG(cdev, "%s\n", __func__);
	spin_lock_irqsave(&cdev->lock, flags);

	if (cdev->delayed_status == 0) {
		WARN(cdev, "%s: Unexpected call\n", __func__);

	} else if (--cdev->delayed_status == 0) {
		DBG(cdev, "%s: Completing delayed status\n", __func__);
		req->length = 0;
		req->context = cdev;
		value = composite_ep0_queue(cdev, req, GFP_ATOMIC);
		if (value < 0) {
			DBG(cdev, "ep_queue --> %d\n", value);
			req->status = 0;
			composite_setup_complete(cdev->gadget->ep0, req);
		}
	}

	spin_unlock_irqrestore(&cdev->lock, flags);
}
EXPORT_SYMBOL_GPL(usb_composite_setup_continue);

static char *composite_default_mfr(struct usb_gadget *gadget)
{
	return kasprintf(GFP_KERNEL, "%s %s with %s", init_utsname()->sysname,
			 init_utsname()->release, gadget->name);
}

void usb_composite_overwrite_options(struct usb_composite_dev *cdev,
		struct usb_composite_overwrite *covr)
{
	struct usb_device_descriptor	*desc = &cdev->desc;
	struct usb_gadget_strings	*gstr = cdev->driver->strings[0];
	struct usb_string		*dev_str = gstr->strings;

	if (covr->idVendor)
		desc->idVendor = cpu_to_le16(covr->idVendor);

	if (covr->idProduct)
		desc->idProduct = cpu_to_le16(covr->idProduct);

	if (covr->bcdDevice)
		desc->bcdDevice = cpu_to_le16(covr->bcdDevice);

	if (covr->serial_number) {
		desc->iSerialNumber = dev_str[USB_GADGET_SERIAL_IDX].id;
		dev_str[USB_GADGET_SERIAL_IDX].s = covr->serial_number;
	}
	if (covr->manufacturer) {
		desc->iManufacturer = dev_str[USB_GADGET_MANUFACTURER_IDX].id;
		dev_str[USB_GADGET_MANUFACTURER_IDX].s = covr->manufacturer;

	} else if (!strlen(dev_str[USB_GADGET_MANUFACTURER_IDX].s)) {
		desc->iManufacturer = dev_str[USB_GADGET_MANUFACTURER_IDX].id;
		cdev->def_manufacturer = composite_default_mfr(cdev->gadget);
		dev_str[USB_GADGET_MANUFACTURER_IDX].s = cdev->def_manufacturer;
	}

	if (covr->product) {
		desc->iProduct = dev_str[USB_GADGET_PRODUCT_IDX].id;
		dev_str[USB_GADGET_PRODUCT_IDX].s = covr->product;
	}
}
EXPORT_SYMBOL_GPL(usb_composite_overwrite_options);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Brownell");
