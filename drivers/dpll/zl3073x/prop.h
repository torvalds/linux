/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_PROP_H
#define _ZL3073X_PROP_H

#include <linux/dpll.h>

#include "core.h"

struct fwnode_handle;

/**
 * struct zl3073x_pin_props - pin properties
 * @fwnode: pin firmware node
 * @dpll_props: DPLL core pin properties
 * @package_label: pin package label
 * @esync_control: embedded sync support
 */
struct zl3073x_pin_props {
	struct fwnode_handle		*fwnode;
	struct dpll_pin_properties	dpll_props;
	char				package_label[8];
	bool				esync_control;
};

enum dpll_type zl3073x_prop_dpll_type_get(struct zl3073x_dev *zldev, u8 index);

struct zl3073x_pin_props *zl3073x_pin_props_get(struct zl3073x_dev *zldev,
						enum dpll_pin_direction dir,
						u8 index);

void zl3073x_pin_props_put(struct zl3073x_pin_props *props);

#endif /* _ZL3073X_PROP_H */
