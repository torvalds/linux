/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_HELPERS_H_
#define _LENOVO_WMI_HELPERS_H_

#include <linux/types.h>

struct device;
struct notifier_block;
struct wmi_device;

struct wmi_method_args_32 {
	u32 arg0;
	u32 arg1;
};

struct dentry *lwmi_debugfs_create_dir(struct wmi_device *wdev);

enum lwmi_event_type {
	LWMI_GZ_GET_THERMAL_MODE = 0x01,
};

enum thermal_mode {
	LWMI_GZ_THERMAL_MODE_NONE =	   0x00,
	LWMI_GZ_THERMAL_MODE_QUIET =	   0x01,
	LWMI_GZ_THERMAL_MODE_BALANCED =	   0x02,
	LWMI_GZ_THERMAL_MODE_PERFORMANCE = 0x03,
	LWMI_GZ_THERMAL_MODE_EXTREME =	   0xE0, /* Ver 6+ */
	LWMI_GZ_THERMAL_MODE_CUSTOM =	   0xFF,
};

int lwmi_dev_evaluate_int(struct wmi_device *wdev, u8 instance, u32 method_id,
			  unsigned char *buf, size_t size, u32 *retval);

int lwmi_tm_register_notifier(struct notifier_block *nb);
int lwmi_tm_unregister_notifier(struct notifier_block *nb);
int devm_lwmi_tm_register_notifier(struct device *dev,
				   struct notifier_block *nb);
int lwmi_tm_notifier_call(enum thermal_mode *mode);

#endif /* !_LENOVO_WMI_HELPERS_H_ */
