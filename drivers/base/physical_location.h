/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device physical location support
 *
 * Author: Won Chung <wonchung@google.com>
 */

#include <linux/device.h>

#ifdef CONFIG_ACPI
extern bool dev_add_physical_location(struct device *dev);
extern const struct attribute_group dev_attr_physical_location_group;
#else
static inline bool dev_add_physical_location(struct device *dev) { return false; };
static const struct attribute_group dev_attr_physical_location_group = {};
#endif
