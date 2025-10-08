/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_EVENTS_H_
#define _LENOVO_WMI_EVENTS_H_

struct device;
struct notifier_block;

enum lwmi_events_type {
	LWMI_EVENT_THERMAL_MODE = 1,
};

int lwmi_events_register_notifier(struct notifier_block *nb);
int lwmi_events_unregister_notifier(struct notifier_block *nb);
int devm_lwmi_events_register_notifier(struct device *dev,
				       struct notifier_block *nb);

#endif /* !_LENOVO_WMI_EVENTS_H_ */
