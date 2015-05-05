/* visorbus_private.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
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

#ifndef __VISORBUS_PRIVATE_H__
#define __VISORBUS_PRIVATE_H__

#include "timskmod.h"
#include "visorbus.h"
#include "visorchipset.h"
#include "visorbus.h"
#include "version.h"
#include "vbuschannel.h"

/* module parameters */
extern int visorbus_debug;
extern int visorbus_forcematch;
extern int visorbus_forcenomatch;
#define MAXDEVICETEST 4
extern int visorbus_devicetest;
extern int visorbus_debugref;
extern int visorbus_serialloopbacktest;
#define SERIALLOOPBACKCHANADDR (100 * 1024 * 1024)

/** This is the private data that we store for each bus device instance.
 */
struct visorbus_devdata {
	int devno;		/* this is the chipset busNo */
	struct list_head list_all;
	struct device *dev;
	struct kobject kobj;
	struct visorchannel *chan;	/* channel area for bus itself */
	bool vbus_valid;
	struct spar_vbus_headerinfo vbus_hdr_info;
};

#endif
