/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Copyright (C) 2025 Derek J. Clark <derekjohn.clark@gmail.com> */

#ifndef _LENOVO_WMI_GAMEZONE_H_
#define _LENOVO_WMI_GAMEZONE_H_

enum gamezone_events_type {
	LWMI_GZ_GET_THERMAL_MODE = 1,
};

enum thermal_mode {
	LWMI_GZ_THERMAL_MODE_QUIET =	   0x01,
	LWMI_GZ_THERMAL_MODE_BALANCED =	   0x02,
	LWMI_GZ_THERMAL_MODE_PERFORMANCE = 0x03,
	LWMI_GZ_THERMAL_MODE_EXTREME =	   0xE0, /* Ver 6+ */
	LWMI_GZ_THERMAL_MODE_CUSTOM =	   0xFF,
};

#endif /* !_LENOVO_WMI_GAMEZONE_H_ */
