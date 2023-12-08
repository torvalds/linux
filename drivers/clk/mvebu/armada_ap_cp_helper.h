/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ARMADA_AP_CP_HELPER_H
#define __ARMADA_AP_CP_HELPER_H

struct device;
struct device_node;

char *ap_cp_unique_name(struct device *dev, struct device_node *np,
			const char *name);
#endif
