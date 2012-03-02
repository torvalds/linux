/*
 * Core private header for the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/pinctrl/pinconf.h>

struct pinctrl_gpio_range;

/**
 * struct pinctrl_dev - pin control class device
 * @node: node to include this pin controller in the global pin controller list
 * @desc: the pin controller descriptor supplied when initializing this pin
 *	controller
 * @pin_desc_tree: each pin descriptor for this pin controller is stored in
 *	this radix tree
 * @gpio_ranges: a list of GPIO ranges that is handled by this pin controller,
 *	ranges are added to this list at runtime
 * @dev: the device entry for this pin controller
 * @owner: module providing the pin controller, used for refcounting
 * @driver_data: driver data for drivers registering to the pin controller
 *	subsystem
 * @p: result of pinctrl_get() for this device
 * @device_root: debugfs root for this device
 */
struct pinctrl_dev {
	struct list_head node;
	struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
	struct list_head gpio_ranges;
	struct device *dev;
	struct module *owner;
	void *driver_data;
	struct pinctrl *p;
#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif
};

/**
 * struct pinctrl - per-device pin control state holder
 * @node: global list node
 * @dev: the device using this pin control handle
 * @state: the state name passed to pinctrl_get()
 * @usecount: the number of active users of this pin controller setting, used
 *	to keep track of nested use cases
 * @settings: a list of settings for this device/state
 */
struct pinctrl {
	struct list_head node;
	struct device *dev;
	const char *state;
	unsigned usecount;
	struct list_head settings;
};

/**
 * struct pinctrl_setting - an individual mux setting
 * @node: list node for struct pinctrl's @settings field
 * @pctldev: pin control device handling to be programmed
 * @group_selector: the group selector to program
 * @func_selector: the function selector to program
 */
struct pinctrl_setting {
	struct list_head node;
	struct pinctrl_dev *pctldev;
	unsigned group_selector;
	unsigned func_selector;
};

/**
 * struct pin_desc - pin descriptor for each physical pin in the arch
 * @pctldev: corresponding pin control device
 * @name: a name for the pin, e.g. the name of the pin/pad/finger on a
 *	datasheet or such
 * @dynamic_name: if the name of this pin was dynamically allocated
 * @usecount: If zero, the pin is not claimed, and @owner should be NULL.
 *	If non-zero, this pin is claimed by @owner. This field is an integer
 *	rather than a boolean, since pinctrl_get() might process multiple
 *	mapping table entries that refer to, and hence claim, the same group
 *	or pin, and each of these will increment the @usecount.
 * @owner: The name of the entity owning the pin. Typically, this is the name
 *	of the device that called pinctrl_get(). Alternatively, it may be the
 *	name of the GPIO passed to pinctrl_request_gpio().
 */
struct pin_desc {
	struct pinctrl_dev *pctldev;
	const char *name;
	bool dynamic_name;
	/* These fields only added when supporting pinmux drivers */
#ifdef CONFIG_PINMUX
	unsigned usecount;
	const char *owner;
#endif
};

struct pinctrl_dev *get_pinctrl_dev_from_devname(const char *dev_name);
int pin_get_from_name(struct pinctrl_dev *pctldev, const char *name);
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group);

static inline struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev,
					    unsigned int pin)
{
	return radix_tree_lookup(&pctldev->pin_desc_tree, pin);
}

extern struct mutex pinctrl_mutex;
