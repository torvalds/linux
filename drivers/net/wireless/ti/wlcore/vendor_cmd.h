/*
 * This file is part of wlcore
 *
 * Copyright (C) 2014 Texas Instruments. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __WLCORE_VENDOR_H__
#define __WLCORE_VENDOR_H__

#define TI_OUI	0x080028

enum wlcore_vendor_commands {
	WLCORE_VENDOR_CMD_SMART_CONFIG_START,
	WLCORE_VENDOR_CMD_SMART_CONFIG_STOP,
	WLCORE_VENDOR_CMD_SMART_CONFIG_SET_GROUP_KEY,

	NUM_WLCORE_VENDOR_CMD,
	MAX_WLCORE_VENDOR_CMD = NUM_WLCORE_VENDOR_CMD - 1
};

enum wlcore_vendor_attributes {
	WLCORE_VENDOR_ATTR_FREQ,
	WLCORE_VENDOR_ATTR_PSK,
	WLCORE_VENDOR_ATTR_SSID,
	WLCORE_VENDOR_ATTR_GROUP_ID,
	WLCORE_VENDOR_ATTR_GROUP_KEY,

	NUM_WLCORE_VENDOR_ATTR,
	MAX_WLCORE_VENDOR_ATTR = NUM_WLCORE_VENDOR_ATTR - 1
};

#endif /* __WLCORE_VENDOR_H__ */
