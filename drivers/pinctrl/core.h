/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Core private header for the pin control subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/types.h>

#include <linux/pinctrl/machine.h>

struct dentry;
struct device;
struct device_node;
struct module;

struct pinctrl;
struct pinctrl_desc;
struct pinctrl_gpio_range;
struct pinctrl_state;

/**
 * struct pinctrl_dev - pin control class device
 * @node: node to include this pin controller in the global pin controller list
 * @desc: the pin controller descriptor supplied when initializing this pin
 *	controller
 * @pin_desc_tree: each pin descriptor for this pin controller is stored in
 *	this radix tree
 * @pin_group_tree: optionally each pin group can be stored in this radix tree
 * @num_groups: optionally number of groups can be kept here
 * @pin_function_tree: optionally each function can be stored in this radix tree
 * @num_functions: optionally number of functions can be kept here
 * @gpio_ranges: a list of GPIO ranges that is handled by this pin controller,
 *	ranges are added to this list at runtime
 * @dev: the device entry for this pin controller
 * @owner: module providing the pin controller, used for refcounting
 * @driver_data: driver data for drivers registering to the pin controller
 *	subsystem
 * @p: result of pinctrl_get() for this device
 * @hog_default: default state for pins hogged by this device
 * @hog_sleep: sleep state for pins hogged by this device
 * @mutex: mutex taken on each pin controller specific action
 * @device_root: debugfs root for this device
 */
struct pinctrl_dev {
	struct list_head node;
	struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
#ifdef CONFIG_GENERIC_PINCTRL_GROUPS
	struct radix_tree_root pin_group_tree;
	unsigned int num_groups;
#endif
#ifdef CONFIG_GENERIC_PINMUX_FUNCTIONS
	struct radix_tree_root pin_function_tree;
	unsigned int num_functions;
#endif
	struct list_head gpio_ranges;
	struct device *dev;
	struct module *owner;
	void *driver_data;
	struct pinctrl *p;
	struct pinctrl_state *hog_default;
	struct pinctrl_state *hog_sleep;
	struct mutex mutex;
#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif
};

/**
 * struct pinctrl - per-device pin control state holder
 * @node: global list node
 * @dev: the device using this pin control handle
 * @states: a list of states for this device
 * @state: the current state
 * @dt_maps: the mapping table chunks dynamically parsed from device tree for
 *	this device, if any
 * @users: reference count
 */
struct pinctrl {
	struct list_head node;
	struct device *dev;
	struct list_head states;
	struct pinctrl_state *state;
	struct list_head dt_maps;
	struct kref users;
};

/**
 * struct pinctrl_state - a pinctrl state for a device
 * @node: list node for struct pinctrl's @states field
 * @name: the name of this state
 * @settings: a list of settings for this state
 */
struct pinctrl_state {
	struct list_head node;
	const char *name;
	struct list_head settings;
};

/**
 * struct pinctrl_setting_mux - setting data for MAP_TYPE_MUX_GROUP
 * @group: the group selector to program
 * @func: the function selector to program
 */
struct pinctrl_setting_mux {
	unsigned int group;
	unsigned int func;
};

/**
 * struct pinctrl_setting_configs - setting data for MAP_TYPE_CONFIGS_*
 * @group_or_pin: the group selector or pin ID to program
 * @configs: a pointer to an array of config parameters/values to program into
 *	hardware. Each individual pin controller defines the format and meaning
 *	of config parameters.
 * @num_configs: the number of entries in array @configs
 */
struct pinctrl_setting_configs {
	unsigned int group_or_pin;
	unsigned long *configs;
	unsigned int num_configs;
};

/**
 * struct pinctrl_setting - an individual mux or config setting
 * @node: list node for struct pinctrl_settings's @settings field
 * @type: the type of setting
 * @pctldev: pin control device handling to be programmed. Not used for
 *   PIN_MAP_TYPE_DUMMY_STATE.
 * @dev_name: the name of the device using this state
 * @data: Data specific to the setting type
 */
struct pinctrl_setting {
	struct list_head node;
	enum pinctrl_map_type type;
	struct pinctrl_dev *pctldev;
	const char *dev_name;
	union {
		struct pinctrl_setting_mux mux;
		struct pinctrl_setting_configs configs;
	} data;
};

/**
 * struct pin_desc - pin descriptor for each physical pin in the arch
 * @pctldev: corresponding pin control device
 * @name: a name for the pin, e.g. the name of the pin/pad/finger on a
 *	datasheet or such
 * @dynamic_name: if the name of this pin was dynamically allocated
 * @drv_data: driver-defined per-pin data. pinctrl core does not touch this
 * @mux_usecount: If zero, the pin is not claimed, and @owner should be NULL.
 *	If non-zero, this pin is claimed by @owner. This field is an integer
 *	rather than a boolean, since pinctrl_get() might process multiple
 *	mapping table entries that refer to, and hence claim, the same group
 *	or pin, and each of these will increment the @usecount.
 * @mux_owner: The name of device that called pinctrl_get().
 * @mux_setting: The most recent selected mux setting for this pin, if any.
 * @gpio_owner: If pinctrl_gpio_request() was called for this pin, this is
 *	the name of the GPIO that "owns" this pin.
 */
struct pin_desc {
	struct pinctrl_dev *pctldev;
	const char *name;
	bool dynamic_name;
	void *drv_data;
	/* These fields only added when supporting pinmux drivers */
#ifdef CONFIG_PINMUX
	unsigned int mux_usecount;
	const char *mux_owner;
	const struct pinctrl_setting_mux *mux_setting;
	const char *gpio_owner;
	struct mutex mux_lock;
#endif
};

/**
 * struct pinctrl_maps - a list item containing part of the mapping table
 * @node: mapping table list node
 * @maps: array of mapping table entries
 * @num_maps: the number of entries in @maps
 */
struct pinctrl_maps {
	struct list_head node;
	const struct pinctrl_map *maps;
	unsigned int num_maps;
};

#ifdef CONFIG_GENERIC_PINCTRL_GROUPS

#include <linux/pinctrl/pinctrl.h>

/**
 * struct group_desc - generic pin group descriptor
 * @grp: generic data of the pin group (name and pins)
 * @data: pin controller driver specific data
 */
struct group_desc {
	struct pingroup grp;
	void *data;
};

/* Convenient macro to define a generic pin group descriptor */
#define PINCTRL_GROUP_DESC(_name, _pins, _num_pins, _data)	\
(struct group_desc) {						\
	.grp = PINCTRL_PINGROUP(_name, _pins, _num_pins),	\
	.data = _data,						\
}

int pinctrl_generic_get_group_count(struct pinctrl_dev *pctldev);

const char *pinctrl_generic_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int group_selector);

int pinctrl_generic_get_group_pins(struct pinctrl_dev *pctldev,
				   unsigned int group_selector,
				   const unsigned int **pins,
				   unsigned int *npins);

struct group_desc *pinctrl_generic_get_group(struct pinctrl_dev *pctldev,
					     unsigned int group_selector);

int pinctrl_generic_add_group(struct pinctrl_dev *pctldev, const char *name,
			      const unsigned int *pins, int num_pins, void *data);

int pinctrl_generic_remove_group(struct pinctrl_dev *pctldev,
				 unsigned int group_selector);

#endif	/* CONFIG_GENERIC_PINCTRL_GROUPS */

struct pinctrl_dev *get_pinctrl_dev_from_devname(const char *dev_name);
struct pinctrl_dev *get_pinctrl_dev_from_of_node(struct device_node *np);
int pin_get_from_name(struct pinctrl_dev *pctldev, const char *name);
const char *pin_get_name(struct pinctrl_dev *pctldev, const unsigned int pin);
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group);

static inline struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev,
					    unsigned int pin)
{
	return radix_tree_lookup(&pctldev->pin_desc_tree, pin);
}

extern struct pinctrl_gpio_range *
pinctrl_find_gpio_range_from_pin_nolock(struct pinctrl_dev *pctldev,
					unsigned int pin);

extern int pinctrl_force_sleep(struct pinctrl_dev *pctldev);
extern int pinctrl_force_default(struct pinctrl_dev *pctldev);

extern struct mutex pinctrl_maps_mutex;
extern struct list_head pinctrl_maps;

#define for_each_pin_map(_maps_node_, _map_)						\
	list_for_each_entry(_maps_node_, &pinctrl_maps, node)				\
		for (unsigned int __i = 0;						\
		     __i < _maps_node_->num_maps && (_map_ = &_maps_node_->maps[__i]);	\
		     __i++)
