/* vbushelper.h
 *
 * Copyright (C) 2011 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VBUSHELPER_H__
#define __VBUSHELPER_H__

/* TARGET_HOSTNAME specified as -DTARGET_HOSTNAME=\"thename\" on the
 * command line
 */

#define TARGET_HOSTNAME "linuxguest"

static inline void bus_device_info_init(
		struct ultra_vbus_deviceinfo *bus_device_info_ptr,
		const char *dev_type, const char *drv_name,
		const char *ver, const char *ver_tag)
{
	memset(bus_device_info_ptr, 0, sizeof(struct ultra_vbus_deviceinfo));
	snprintf(bus_device_info_ptr->devtype,
		 sizeof(bus_device_info_ptr->devtype),
		 "%s", (dev_type) ? dev_type : "unknownType");
	snprintf(bus_device_info_ptr->drvname,
		 sizeof(bus_device_info_ptr->drvname),
		 "%s", (drv_name) ? drv_name : "unknownDriver");
	snprintf(bus_device_info_ptr->infostrs,
		 sizeof(bus_device_info_ptr->infostrs), "%s\t%s\t%s",
		 (ver) ? ver : "unknownVer",
		 (ver_tag) ? ver_tag : "unknownVerTag",
		 TARGET_HOSTNAME);
}

#endif
