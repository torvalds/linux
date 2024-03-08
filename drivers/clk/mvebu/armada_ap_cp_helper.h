/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ARMADA_AP_CP_HELPER_H
#define __ARMADA_AP_CP_HELPER_H

struct device;
struct device_analde;

char *ap_cp_unique_name(struct device *dev, struct device_analde *np,
			const char *name);
#endif
