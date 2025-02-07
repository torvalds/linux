/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Alienware WMI special features driver
 *
 * Copyright (C) 2014 Dell Inc <Dell.Client.Kernel@dell.com>
 * Copyright (C) 2024 Kurt Borja <kuurtb@gmail.com>
 */

#ifndef _ALIENWARE_WMI_H_
#define _ALIENWARE_WMI_H_

#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/wmi.h>

#define LEGACY_CONTROL_GUID		"A90597CE-A997-11DA-B012-B622A1EF5492"
#define LEGACY_POWER_CONTROL_GUID	"A80593CE-A997-11DA-B012-B622A1EF5492"
#define WMAX_CONTROL_GUID		"A70591CE-A997-11DA-B012-B622A1EF5492"

enum INTERFACE_FLAGS {
	LEGACY,
	WMAX,
};

enum LEGACY_CONTROL_STATES {
	LEGACY_RUNNING = 1,
	LEGACY_BOOTING = 0,
	LEGACY_SUSPEND = 3,
};

enum WMAX_CONTROL_STATES {
	WMAX_RUNNING = 0xFF,
	WMAX_BOOTING = 0,
	WMAX_SUSPEND = 3,
};

struct alienfx_quirks {
	u8 num_zones;
	bool hdmi_mux;
	bool amplifier;
	bool deepslp;
};

struct color_platform {
	u8 blue;
	u8 green;
	u8 red;
} __packed;

struct alienfx_priv {
	struct platform_device *pdev;
	struct led_classdev global_led;
	struct color_platform colors[4];
	u8 global_brightness;
	u8 lighting_control_state;
};

struct alienfx_ops {
	int (*upd_led)(struct alienfx_priv *priv, struct wmi_device *wdev,
		       u8 location);
	int (*upd_brightness)(struct alienfx_priv *priv, struct wmi_device *wdev,
			      u8 brightness);
};

struct alienfx_platdata {
	struct wmi_device *wdev;
	struct alienfx_ops ops;
};

extern u8 alienware_interface;
extern struct alienfx_quirks *alienfx;

int alienware_wmi_command(struct wmi_device *wdev, u32 method_id,
			  void *in_args, size_t in_size, u32 *out_data);

int alienware_alienfx_setup(struct alienfx_platdata *pdata);

#if IS_ENABLED(CONFIG_ALIENWARE_WMI_LEGACY)
int __init alienware_legacy_wmi_init(void);
void __exit alienware_legacy_wmi_exit(void);
#else
static inline int alienware_legacy_wmi_init(void)
{
	return -ENODEV;
}

static inline void alienware_legacy_wmi_exit(void)
{
}
#endif

#if IS_ENABLED(CONFIG_ALIENWARE_WMI_WMAX)
extern const struct attribute_group wmax_hdmi_attribute_group;
extern const struct attribute_group wmax_amplifier_attribute_group;
extern const struct attribute_group wmax_deepsleep_attribute_group;

#define WMAX_DEV_GROUPS		&wmax_hdmi_attribute_group,		\
				&wmax_amplifier_attribute_group,	\
				&wmax_deepsleep_attribute_group,

int __init alienware_wmax_wmi_init(void);
void __exit alienware_wmax_wmi_exit(void);
#else
#define WMAX_DEV_GROUPS

static inline int alienware_wmax_wmi_init(void)
{
	return -ENODEV;
}


static inline void alienware_wmax_wmi_exit(void)
{
}
#endif

#endif
