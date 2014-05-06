/* vbushelper.h
 *
 * Copyright © 2011 - 2013 UNISYS CORPORATION
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

static inline void
BusDeviceInfo_Init(ULTRA_VBUS_DEVICEINFO *pBusDeviceInfo,
		   const char *deviceType, const char *driverName,
		   const char *ver, const char *verTag,
		   const char *buildDate, const char *buildTime)
{
	memset(pBusDeviceInfo, 0, sizeof(ULTRA_VBUS_DEVICEINFO));
	snprintf(pBusDeviceInfo->devType, sizeof(pBusDeviceInfo->devType),
		 "%s", (deviceType) ? deviceType : "unknownType");
	snprintf(pBusDeviceInfo->drvName, sizeof(pBusDeviceInfo->drvName),
		 "%s", (driverName) ? driverName : "unknownDriver");
	snprintf(pBusDeviceInfo->infoStrings,
		 sizeof(pBusDeviceInfo->infoStrings), "%s\t%s\t%s %s\t%s",
		 (ver) ? ver : "unknownVer",
		 (verTag) ? verTag : "unknownVerTag",
		 (buildDate) ? buildDate : "noBuildDate",
		 (buildTime) ? buildTime : "nobuildTime", TARGET_HOSTNAME);
}

#endif
