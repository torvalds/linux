// SPDX-License-Identifier: GPL-2.0
/*
 * Provides code common for host and device side USB.
 *
 * If either host side (ie. CONFIG_USB=y) or device side USB stack
 * (ie. CONFIG_USB_GADGET=y) is compiled in the kernel, this module is
 * compiled-in as well.  Otherwise, if either of the two stacks is
 * compiled as module, this file is compiled as module as well.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>
#include "common.h"

static const char *const ep_type_names[] = {
	[USB_ENDPOINT_XFER_CONTROL] = "ctrl",
	[USB_ENDPOINT_XFER_ISOC] = "isoc",
	[USB_ENDPOINT_XFER_BULK] = "bulk",
	[USB_ENDPOINT_XFER_INT] = "intr",
};

/**
 * usb_ep_type_string() - Returns human readable-name of the endpoint type.
 * @ep_type: The endpoint type to return human-readable name for.  If it's not
 *   any of the types: USB_ENDPOINT_XFER_{CONTROL, ISOC, BULK, INT},
 *   usually got by usb_endpoint_type(), the string 'unknown' will be returned.
 */
const char *usb_ep_type_string(int ep_type)
{
	if (ep_type < 0 || ep_type >= ARRAY_SIZE(ep_type_names))
		return "unknown";

	return ep_type_names[ep_type];
}
EXPORT_SYMBOL_GPL(usb_ep_type_string);

const char *usb_otg_state_string(enum usb_otg_state state)
{
	static const char *const names[] = {
		[OTG_STATE_A_IDLE] = "a_idle",
		[OTG_STATE_A_WAIT_VRISE] = "a_wait_vrise",
		[OTG_STATE_A_WAIT_BCON] = "a_wait_bcon",
		[OTG_STATE_A_HOST] = "a_host",
		[OTG_STATE_A_SUSPEND] = "a_suspend",
		[OTG_STATE_A_PERIPHERAL] = "a_peripheral",
		[OTG_STATE_A_WAIT_VFALL] = "a_wait_vfall",
		[OTG_STATE_A_VBUS_ERR] = "a_vbus_err",
		[OTG_STATE_B_IDLE] = "b_idle",
		[OTG_STATE_B_SRP_INIT] = "b_srp_init",
		[OTG_STATE_B_PERIPHERAL] = "b_peripheral",
		[OTG_STATE_B_WAIT_ACON] = "b_wait_acon",
		[OTG_STATE_B_HOST] = "b_host",
	};

	if (state < 0 || state >= ARRAY_SIZE(names))
		return "UNDEFINED";

	return names[state];
}
EXPORT_SYMBOL_GPL(usb_otg_state_string);

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
	[USB_SPEED_WIRELESS] = "wireless",
	[USB_SPEED_SUPER] = "super-speed",
	[USB_SPEED_SUPER_PLUS] = "super-speed-plus",
};

static const char *const ssp_rate[] = {
	[USB_SSP_GEN_UNKNOWN] = "UNKNOWN",
	[USB_SSP_GEN_2x1] = "super-speed-plus-gen2x1",
	[USB_SSP_GEN_1x2] = "super-speed-plus-gen1x2",
	[USB_SSP_GEN_2x2] = "super-speed-plus-gen2x2",
};

/**
 * usb_speed_string() - Returns human readable-name of the speed.
 * @speed: The speed to return human-readable name for.  If it's not
 *   any of the speeds defined in usb_device_speed enum, string for
 *   USB_SPEED_UNKNOWN will be returned.
 */
const char *usb_speed_string(enum usb_device_speed speed)
{
	if (speed < 0 || speed >= ARRAY_SIZE(speed_names))
		speed = USB_SPEED_UNKNOWN;
	return speed_names[speed];
}
EXPORT_SYMBOL_GPL(usb_speed_string);

/**
 * usb_get_maximum_speed - Get maximum requested speed for a given USB
 * controller.
 * @dev: Pointer to the given USB controller device
 *
 * The function gets the maximum speed string from property "maximum-speed",
 * and returns the corresponding enum usb_device_speed.
 */
enum usb_device_speed usb_get_maximum_speed(struct device *dev)
{
	const char *p = "maximum-speed";
	int ret;

	ret = device_property_match_property_string(dev, p, ssp_rate, ARRAY_SIZE(ssp_rate));
	if (ret > 0)
		return USB_SPEED_SUPER_PLUS;

	ret = device_property_match_property_string(dev, p, speed_names, ARRAY_SIZE(speed_names));
	if (ret > 0)
		return ret;

	return USB_SPEED_UNKNOWN;
}
EXPORT_SYMBOL_GPL(usb_get_maximum_speed);

/**
 * usb_get_maximum_ssp_rate - Get the signaling rate generation and lane count
 *	of a SuperSpeed Plus capable device.
 * @dev: Pointer to the given USB controller device
 *
 * If the string from "maximum-speed" property is super-speed-plus-genXxY where
 * 'X' is the generation number and 'Y' is the number of lanes, then this
 * function returns the corresponding enum usb_ssp_rate.
 */
enum usb_ssp_rate usb_get_maximum_ssp_rate(struct device *dev)
{
	const char *maximum_speed;
	int ret;

	ret = device_property_read_string(dev, "maximum-speed", &maximum_speed);
	if (ret < 0)
		return USB_SSP_GEN_UNKNOWN;

	ret = match_string(ssp_rate, ARRAY_SIZE(ssp_rate), maximum_speed);
	return (ret < 0) ? USB_SSP_GEN_UNKNOWN : ret;
}
EXPORT_SYMBOL_GPL(usb_get_maximum_ssp_rate);

/**
 * usb_state_string - Returns human readable name for the state.
 * @state: The state to return a human-readable name for. If it's not
 *	any of the states devices in usb_device_state_string enum,
 *	the string UNKNOWN will be returned.
 */
const char *usb_state_string(enum usb_device_state state)
{
	static const char *const names[] = {
		[USB_STATE_NOTATTACHED] = "not attached",
		[USB_STATE_ATTACHED] = "attached",
		[USB_STATE_POWERED] = "powered",
		[USB_STATE_RECONNECTING] = "reconnecting",
		[USB_STATE_UNAUTHENTICATED] = "unauthenticated",
		[USB_STATE_DEFAULT] = "default",
		[USB_STATE_ADDRESS] = "addressed",
		[USB_STATE_CONFIGURED] = "configured",
		[USB_STATE_SUSPENDED] = "suspended",
	};

	if (state < 0 || state >= ARRAY_SIZE(names))
		return "UNKNOWN";

	return names[state];
}
EXPORT_SYMBOL_GPL(usb_state_string);

static const char *const usb_dr_modes[] = {
	[USB_DR_MODE_UNKNOWN]		= "",
	[USB_DR_MODE_HOST]		= "host",
	[USB_DR_MODE_PERIPHERAL]	= "peripheral",
	[USB_DR_MODE_OTG]		= "otg",
};

static enum usb_dr_mode usb_get_dr_mode_from_string(const char *str)
{
	int ret;

	ret = match_string(usb_dr_modes, ARRAY_SIZE(usb_dr_modes), str);
	return (ret < 0) ? USB_DR_MODE_UNKNOWN : ret;
}

enum usb_dr_mode usb_get_dr_mode(struct device *dev)
{
	const char *dr_mode;
	int err;

	err = device_property_read_string(dev, "dr_mode", &dr_mode);
	if (err < 0)
		return USB_DR_MODE_UNKNOWN;

	return usb_get_dr_mode_from_string(dr_mode);
}
EXPORT_SYMBOL_GPL(usb_get_dr_mode);

/**
 * usb_get_role_switch_default_mode - Get default mode for given device
 * @dev: Pointer to the given device
 *
 * The function gets string from property 'role-switch-default-mode',
 * and returns the corresponding enum usb_dr_mode.
 */
enum usb_dr_mode usb_get_role_switch_default_mode(struct device *dev)
{
	const char *str;
	int ret;

	ret = device_property_read_string(dev, "role-switch-default-mode", &str);
	if (ret < 0)
		return USB_DR_MODE_UNKNOWN;

	return usb_get_dr_mode_from_string(str);
}
EXPORT_SYMBOL_GPL(usb_get_role_switch_default_mode);

/**
 * usb_decode_interval - Decode bInterval into the time expressed in 1us unit
 * @epd: The descriptor of the endpoint
 * @speed: The speed that the endpoint works as
 *
 * Function returns the interval expressed in 1us unit for servicing
 * endpoint for data transfers.
 */
unsigned int usb_decode_interval(const struct usb_endpoint_descriptor *epd,
				 enum usb_device_speed speed)
{
	unsigned int interval = 0;

	switch (usb_endpoint_type(epd)) {
	case USB_ENDPOINT_XFER_CONTROL:
		/* uframes per NAK */
		if (speed == USB_SPEED_HIGH)
			interval = epd->bInterval;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		interval = 1 << (epd->bInterval - 1);
		break;
	case USB_ENDPOINT_XFER_BULK:
		/* uframes per NAK */
		if (speed == USB_SPEED_HIGH && usb_endpoint_dir_out(epd))
			interval = epd->bInterval;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (speed >= USB_SPEED_HIGH)
			interval = 1 << (epd->bInterval - 1);
		else
			interval = epd->bInterval;
		break;
	}

	interval *= (speed >= USB_SPEED_HIGH) ? 125 : 1000;

	return interval;
}
EXPORT_SYMBOL_GPL(usb_decode_interval);

#ifdef CONFIG_OF
/**
 * of_usb_get_dr_mode_by_phy - Get dual role mode for the controller device
 * which is associated with the given phy device_node
 * @np:	Pointer to the given phy device_node
 * @arg0: phandle args[0] for phy's with #phy-cells >= 1, or -1 for
 *        phys which do not have phy-cells
 *
 * In dts a usb controller associates with phy devices.  The function gets
 * the string from property 'dr_mode' of the controller associated with the
 * given phy device node, and returns the correspondig enum usb_dr_mode.
 */
enum usb_dr_mode of_usb_get_dr_mode_by_phy(struct device_node *np, int arg0)
{
	struct device_node *controller;
	struct of_phandle_args args;
	const char *dr_mode;
	int index;
	int err;

	for_each_node_with_property(controller, "phys") {
		if (!of_device_is_available(controller))
			continue;
		index = 0;
		do {
			if (arg0 == -1) {
				args.np = of_parse_phandle(controller, "phys",
							index);
				args.args_count = 0;
			} else {
				err = of_parse_phandle_with_args(controller,
							"phys", "#phy-cells",
							index, &args);
				if (err)
					break;
			}

			of_node_put(args.np);
			if (args.np == np && (args.args_count == 0 ||
					      args.args[0] == arg0))
				goto finish;
			index++;
		} while (args.np);
	}

finish:
	err = of_property_read_string(controller, "dr_mode", &dr_mode);
	of_node_put(controller);

	if (err < 0)
		return USB_DR_MODE_UNKNOWN;

	return usb_get_dr_mode_from_string(dr_mode);
}
EXPORT_SYMBOL_GPL(of_usb_get_dr_mode_by_phy);

/**
 * of_usb_host_tpl_support - to get if Targeted Peripheral List is supported
 * for given targeted hosts (non-PC hosts)
 * @np: Pointer to the given device_node
 *
 * The function gets if the targeted hosts support TPL or not
 */
bool of_usb_host_tpl_support(struct device_node *np)
{
	return of_property_read_bool(np, "tpl-support");
}
EXPORT_SYMBOL_GPL(of_usb_host_tpl_support);

/**
 * of_usb_update_otg_caps - to update usb otg capabilities according to
 * the passed properties in DT.
 * @np: Pointer to the given device_node
 * @otg_caps: Pointer to the target usb_otg_caps to be set
 *
 * The function updates the otg capabilities
 */
int of_usb_update_otg_caps(struct device_node *np,
			struct usb_otg_caps *otg_caps)
{
	u32 otg_rev;

	if (!otg_caps)
		return -EINVAL;

	if (!of_property_read_u32(np, "otg-rev", &otg_rev)) {
		switch (otg_rev) {
		case 0x0100:
		case 0x0120:
		case 0x0130:
		case 0x0200:
			/* Choose the lesser one if it's already been set */
			if (otg_caps->otg_rev)
				otg_caps->otg_rev = min_t(u16, otg_rev,
							otg_caps->otg_rev);
			else
				otg_caps->otg_rev = otg_rev;
			break;
		default:
			pr_err("%pOF: unsupported otg-rev: 0x%x\n",
						np, otg_rev);
			return -EINVAL;
		}
	} else {
		/*
		 * otg-rev is mandatory for otg properties, if not passed
		 * we set it to be 0 and assume it's a legacy otg device.
		 * Non-dt platform can set it afterwards.
		 */
		otg_caps->otg_rev = 0;
	}

	if (of_property_read_bool(np, "hnp-disable"))
		otg_caps->hnp_support = false;
	if (of_property_read_bool(np, "srp-disable"))
		otg_caps->srp_support = false;
	if (of_property_read_bool(np, "adp-disable") ||
				(otg_caps->otg_rev < 0x0200))
		otg_caps->adp_support = false;

	return 0;
}
EXPORT_SYMBOL_GPL(of_usb_update_otg_caps);

/**
 * usb_of_get_companion_dev - Find the companion device
 * @dev: the device pointer to find a companion
 *
 * Find the companion device from platform bus.
 *
 * Takes a reference to the returned struct device which needs to be dropped
 * after use.
 *
 * Return: On success, a pointer to the companion device, %NULL on failure.
 */
struct device *usb_of_get_companion_dev(struct device *dev)
{
	struct device_node *node;
	struct platform_device *pdev = NULL;

	node = of_parse_phandle(dev->of_node, "companion", 0);
	if (node)
		pdev = of_find_device_by_node(node);

	of_node_put(node);

	return pdev ? &pdev->dev : NULL;
}
EXPORT_SYMBOL_GPL(usb_of_get_companion_dev);
#endif

struct dentry *usb_debug_root;
EXPORT_SYMBOL_GPL(usb_debug_root);

static int __init usb_common_init(void)
{
	usb_debug_root = debugfs_create_dir("usb", NULL);
	ledtrig_usb_init();
	return 0;
}

static void __exit usb_common_exit(void)
{
	ledtrig_usb_exit();
	debugfs_remove_recursive(usb_debug_root);
}

subsys_initcall(usb_common_init);
module_exit(usb_common_exit);

MODULE_DESCRIPTION("Common code for host and device side USB");
MODULE_LICENSE("GPL");
