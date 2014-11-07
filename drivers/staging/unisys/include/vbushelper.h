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

#include "vbusdeviceinfo.h"

/* TARGET_HOSTNAME specified as -DTARGET_HOSTNAME=\"thename\" on the
 * command line */

#define TARGET_HOSTNAME "linuxguest"

static inline void bus_device_info_init(
		ULTRA_VBUS_DEVICEINFO * bus_device_info_ptr,
		const char *dev_type, const char *drv_name,
		const char *ver, const char *ver_tag)
{
	memset(bus_device_info_ptr, 0, sizeof(ULTRA_VBUS_DEVICEINFO));
	snprintf(bus_device_info_ptr->devType,
		 sizeof(bus_device_info_ptr->devType),
		 "%s", (dev_type) ? dev_type : "unknownType");
	snprintf(bus_device_info_ptr->drvName,
		 sizeof(bus_device_info_ptr->drvName),
		 "%s", (drv_name) ? drv_name : "unknownDriver");
	snprintf(bus_device_info_ptr->infoStrings,
		 sizeof(bus_device_info_ptr->infoStrings), "%s\t%s\t%s",
		 (ver) ? ver : "unknownVer",
		 (ver_tag) ? ver_tag : "unknownVerTag",
		 TARGET_HOSTNAME);
}

#endif
