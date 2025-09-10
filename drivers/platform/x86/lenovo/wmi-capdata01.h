/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_CAPDATA01_H_
#define _LENOVO_WMI_CAPDATA01_H_

#include <linux/types.h>

struct device;
struct cd01_list;

struct capdata01 {
	u32 id;
	u32 supported;
	u32 default_value;
	u32 step;
	u32 min_value;
	u32 max_value;
};

int lwmi_cd01_get_data(struct cd01_list *list, u32 attribute_id, struct capdata01 *output);
int lwmi_cd01_match(struct device *dev, void *data);

#endif /* !_LENOVO_WMI_CAPDATA01_H_ */
