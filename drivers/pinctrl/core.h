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

#include <linux/pinctrl/pinconf.h>

struct pinctrl_gpio_range;

/**
 * struct pinctrl_dev - pin control class device
 * @node: node to include this pin controller in the global pin controller list
 * @desc: the pin controller descriptor supplied when initializing this pin
 *	controller
 * @pin_desc_tree: each pin descriptor for this pin controller is stored in
 *	this radix tree
 * @pin_desc_tree_lock: lock for the descriptor tree
 * @gpio_ranges: a list of GPIO ranges that is handled by this pin controller,
 *	ranges are added to this list at runtime
 * @gpio_ranges_lock: lock for the GPIO ranges list
 * @dev: the device entry for this pin controller
 * @owner: module providing the pin controller, used for refcounting
 * @driver_data: driver data for drivers registering to the pin controller
 *	subsystem
 * @pinmux_hogs_lock: lock for the pinmux hog list
 * @pinmux_hogs: list of pinmux maps hogged by this device
 */
struct pinctrl_dev {
	struct list_head node;
	struct pinctrl_desc *desc;
	struct radix_tree_root pin_desc_tree;
	spinlock_t pin_desc_tree_lock;
	struct list_head gpio_ranges;
	struct mutex gpio_ranges_lock;
	struct device *dev;
	struct module *owner;
	void *driver_data;
#ifdef CONFIG_PINMUX
	struct mutex pinmux_hogs_lock;
	struct list_head pinmux_hogs;
#endif
};

/**
 * struct pin_desc - pin descriptor for each physical pin in the arch
 * @pctldev: corresponding pin control device
 * @name: a name for the pin, e.g. the name of the pin/pad/finger on a
 *	datasheet or such
 * @dynamic_name: if the name of this pin was dynamically allocated
 * @lock: a lock to protect the descriptor structure
 * @mux_requested: whether the pin is already requested by pinmux or not
 * @mux_function: a named muxing function for the pin that will be passed to
 *	subdrivers and shown in debugfs etc
 */
struct pin_desc {
	struct pinctrl_dev *pctldev;
	const char *name;
	bool dynamic_name;
	spinlock_t lock;
	/* These fields only added when supporting pinmux drivers */
#ifdef CONFIG_PINMUX
	const char *mux_function;
#endif
};

struct pinctrl_dev *get_pinctrl_dev_from_dev(struct device *dev,
					     const char *dev_name);
struct pin_desc *pin_desc_get(struct pinctrl_dev *pctldev, unsigned int pin);
int pin_get_from_name(struct pinctrl_dev *pctldev, const char *name);
int pinctrl_get_device_gpio_range(unsigned gpio,
				  struct pinctrl_dev **outdev,
				  struct pinctrl_gpio_range **outrange);
int pinctrl_get_group_selector(struct pinctrl_dev *pctldev,
			       const char *pin_group);
