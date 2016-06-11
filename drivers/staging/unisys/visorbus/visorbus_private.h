/* visorchipset.h
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

#ifndef __VISORCHIPSET_H__
#define __VISORCHIPSET_H__

#include <linux/uuid.h>

#include "controlvmchannel.h"
#include "vbusdeviceinfo.h"
#include "vbushelper.h"

/*  These functions will be called from within visorchipset when certain
 *  events happen.  (The implementation of these functions is outside of
 *  visorchipset.)
 */
struct visorchipset_busdev_notifiers {
	void (*bus_create)(struct visor_device *bus_info);
	void (*bus_destroy)(struct visor_device *bus_info);
	void (*device_create)(struct visor_device *bus_info);
	void (*device_destroy)(struct visor_device *bus_info);
	void (*device_pause)(struct visor_device *bus_info);
	void (*device_resume)(struct visor_device *bus_info);
};

/*  These functions live inside visorchipset, and will be called to indicate
 *  responses to specific events (by code outside of visorchipset).
 *  For now, the value for each response is simply either:
 *       0 = it worked
 *      -1 = it failed
 */
struct visorchipset_busdev_responders {
	void (*bus_create)(struct visor_device *p, int response);
	void (*bus_destroy)(struct visor_device *p, int response);
	void (*device_create)(struct visor_device *p, int response);
	void (*device_destroy)(struct visor_device *p, int response);
	void (*device_pause)(struct visor_device *p, int response);
	void (*device_resume)(struct visor_device *p, int response);
};

/** Register functions (in the bus driver) to get called by visorchipset
 *  whenever a bus or device appears for which this guest is to be the
 *  client for.  visorchipset will fill in <responders>, to indicate
 *  functions the bus driver should call to indicate message responses.
 */
void
visorchipset_register_busdev(
			struct visorchipset_busdev_notifiers *notifiers,
			struct visorchipset_busdev_responders *responders,
			struct ultra_vbus_deviceinfo *driver_info);

/* visorbus init and exit functions */
int visorbus_init(void);
void visorbus_exit(void);

/* Visorchannel access functions */

/* Note that for visorchannel_create()
 * <channel_bytes> and <guid> arguments may be 0 if we are a channel CLIENT.
 * In this case, the values can simply be read from the channel header.
 */
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
