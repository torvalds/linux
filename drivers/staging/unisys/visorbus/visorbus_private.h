/* visorbus_private.h
 *
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VISORBUS_PRIVATE_H__
#define __VISORBUS_PRIVATE_H__

#include <linux/uuid.h>
#include <linux/utsname.h>

#include "controlvmchannel.h"
#include "vbuschannel.h"

/* TARGET_HOSTNAME specified as -DTARGET_HOSTNAME=\"thename\" on the
 * command line
 */

static inline void bus_device_info_init(
		struct ultra_vbus_deviceinfo *bus_device_info_ptr,
		const char *dev_type, const char *drv_name)
{
	memset(bus_device_info_ptr, 0, sizeof(struct ultra_vbus_deviceinfo));
	snprintf(bus_device_info_ptr->devtype,
		 sizeof(bus_device_info_ptr->devtype),
		 "%s", (dev_type) ? dev_type : "unknownType");
	snprintf(bus_device_info_ptr->drvname,
		 sizeof(bus_device_info_ptr->drvname),
		 "%s", (drv_name) ? drv_name : "unknownDriver");
	snprintf(bus_device_info_ptr->infostrs,
		 sizeof(bus_device_info_ptr->infostrs), "kernel ver. %s",
		 utsname()->release);
}

void chipset_bus_create(struct visor_device *bus_info);
void chipset_bus_destroy(struct visor_device *bus_info);
void chipset_device_create(struct visor_device *dev_info);
void chipset_device_destroy(struct visor_device *dev_info);
void chipset_device_pause(struct visor_device *dev_info);
void chipset_device_resume(struct visor_device *dev_info);

void bus_create_response(struct visor_device *p, int response);
void bus_destroy_response(struct visor_device *p, int response);
void device_create_response(struct visor_device *p, int response);
void device_destroy_response(struct visor_device *p, int response);
void device_resume_response(struct visor_device *p, int response);
void device_pause_response(struct visor_device *p, int response);

int visorbus_init(void);
void visorbus_exit(void);

/* visorchannel access functions */

struct visorchannel *visorchannel_create(u64 physaddr,
					 unsigned long channel_bytes,
					 gfp_t gfp, uuid_le guid);
struct visorchannel *visorchannel_create_with_lock(u64 physaddr,
						   unsigned long channel_bytes,
						   gfp_t gfp, uuid_le guid);
void visorchannel_destroy(struct visorchannel *channel);
int visorchannel_read(struct visorchannel *channel, ulong offset,
		      void *local, ulong nbytes);
int visorchannel_write(struct visorchannel *channel, ulong offset,
		       void *local, ulong nbytes);
u64 visorchannel_get_physaddr(struct visorchannel *channel);
ulong visorchannel_get_nbytes(struct visorchannel *channel);
char *visorchannel_id(struct visorchannel *channel, char *s);
char *visorchannel_zoneid(struct visorchannel *channel, char *s);
u64 visorchannel_get_clientpartition(struct visorchannel *channel);
int visorchannel_set_clientpartition(struct visorchannel *channel,
				     u64 partition_handle);
char *visorchannel_uuid_id(uuid_le *guid, char *s);
void __iomem *visorchannel_get_header(struct visorchannel *channel);
#endif
