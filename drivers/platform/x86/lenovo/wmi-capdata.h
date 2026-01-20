/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_CAPDATA_H_
#define _LENOVO_WMI_CAPDATA_H_

#include <linux/types.h>

struct component_match;
struct device;
struct cd_list;

struct capdata01 {
	u32 id;
	u32 supported;
	u32 default_value;
	u32 step;
	u32 min_value;
	u32 max_value;
};

struct lwmi_cd_binder {
	struct cd_list *cd01_list;
};

void lwmi_cd_match_add_all(struct device *master, struct component_match **matchptr);
int lwmi_cd01_get_data(struct cd_list *list, u32 attribute_id, struct capdata01 *output);

#endif /* !_LENOVO_WMI_CAPDATA_H_ */
