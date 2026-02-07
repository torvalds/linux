/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_CAPDATA_H_
#define _LENOVO_WMI_CAPDATA_H_

#include <linux/bits.h>
#include <linux/types.h>

#define LWMI_SUPP_VALID		BIT(0)
#define LWMI_SUPP_GET		BIT(1)
#define LWMI_SUPP_SET		BIT(2)

#define LWMI_ATTR_DEV_ID_MASK	GENMASK(31, 24)
#define LWMI_ATTR_FEAT_ID_MASK	GENMASK(23, 16)
#define LWMI_ATTR_MODE_ID_MASK	GENMASK(15, 8)
#define LWMI_ATTR_TYPE_ID_MASK	GENMASK(7, 0)

#define LWMI_DEVICE_ID_FAN	0x04

struct component_match;
struct device;
struct cd_list;

struct capdata00 {
	u32 id;
	u32 supported;
	u32 default_value;
};

struct capdata01 {
	u32 id;
	u32 supported;
	u32 default_value;
	u32 step;
	u32 min_value;
	u32 max_value;
};

struct capdata_fan {
	u32 id;
	u32 min_rpm;
	u32 max_rpm;
};

typedef void (*cd_list_cb_t)(struct device *master_dev, struct cd_list *cd_list);

struct lwmi_cd_binder {
	struct cd_list *cd00_list;
	struct cd_list *cd01_list;
	/*
	 * May be called during or after the bind callback.
	 * Will be called with NULL if capdata_fan does not exist.
	 * The pointer is only valid in the callback; never keep it for later use!
	 */
	cd_list_cb_t cd_fan_list_cb;
};

void lwmi_cd_match_add_all(struct device *master, struct component_match **matchptr);
int lwmi_cd00_get_data(struct cd_list *list, u32 attribute_id, struct capdata00 *output);
int lwmi_cd01_get_data(struct cd_list *list, u32 attribute_id, struct capdata01 *output);
int lwmi_cd_fan_get_data(struct cd_list *list, u32 attribute_id, struct capdata_fan *output);

#endif /* !_LENOVO_WMI_CAPDATA_H_ */
