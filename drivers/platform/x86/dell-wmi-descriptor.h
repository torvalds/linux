/*
 *  Dell WMI descriptor driver
 *
 *  Copyright (c) 2017 Dell Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef _DELL_WMI_DESCRIPTOR_H_
#define _DELL_WMI_DESCRIPTOR_H_

#include <linux/wmi.h>

#define DELL_WMI_DESCRIPTOR_GUID "8D9DDCBC-A997-11DA-B012-B622A1EF5492"

bool dell_wmi_get_interface_version(u32 *version);
bool dell_wmi_get_size(u32 *size);

#endif
