/* visorchipset.h
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

#ifndef __VISORCHIPSET_H__
#define __VISORCHIPSET_H__

#include <linux/uuid.h>

#include "channel.h"
#include "controlvmchannel.h"
#include "vbusdeviceinfo.h"
#include "vbushelper.h"

struct visorchannel;

enum visorchipset_addresstype {
	/** address is guest physical, but outside of the physical memory
	 *  region that is controlled by the running OS (this is the normal
	 *  address type for Supervisor channels)
	 */
	ADDRTYPE_LOCALPHYSICAL,

	/** address is guest physical, and withIN the confines of the
	 *  physical memory controlled by the running OS.
	 */
	ADDRTYPE_LOCALTEST,
};

/** Attributes for a particular Supervisor device.
 *  Any visorchipset client can query these attributes using
 *  visorchipset_get_client_device_info() or
 *  visorchipset_get_server_device_info().
 */
struct visorchipset_device_info {
	struct list_head entry;
	u32 bus_no;
	u32 dev_no;
	uuid_le dev_inst_uuid;
	struct visorchipset_state state;
	struct visorchannel *visorchannel;
	uuid_le channel_type_guid;
	u32 reserved1;		/* control_vm_id */
	u64 reserved2;
	u32 switch_no;		/* when devState.attached==1 */
	u32 internal_port_no;	/* when devState.attached==1 */
	struct controlvm_message_header *pending_msg_hdr;/* CONTROLVM_MESSAGE */
	/** For private use by the bus driver */
	void *bus_driver_context;
};

/** Attributes for a particular Supervisor bus.
 *  (For a service partition acting as the server for buses/devices, there
 *  is a 1-to-1 relationship between busses and guest partitions.)
 *  Any visorchipset client can query these attributes using
 *  visorchipset_get_client_bus_info() or visorchipset_get_bus_info().
 */
struct visorchipset_bus_info {
	struct list_head entry;
	u32 bus_no;
	struct visorchipset_state state;
	struct visorchannel *visorchannel;
	uuid_le partition_uuid;
	u64 partition_handle;
	u8 *name;		/* UTF8 */
	u8 *description;	/* UTF8 */
	u64 reserved1;
	u32 reserved2;
	struct {
		u32 server:1;
		/* Add new fields above. */
		/* Remaining bits in this 32-bit word are unused. */
	} flags;
	struct controlvm_message_header *pending_msg_hdr;/* CONTROLVM MsgHdr */
	/** For private use by the bus driver */
	void *bus_driver_context;
};

/*  These functions will be called from within visorchipset when certain
 *  events happen.  (The implementation of these functions is outside of
 *  visorchipset.)
 */
struct visorchipset_busdev_notifiers {
	void (*bus_create)(struct visorchipset_bus_info *bus_info);
	void (*bus_destroy)(struct visorchipset_bus_info *bus_info);
	void (*device_create)(struct visorchipset_device_info *bus_info);
	void (*device_destroy)(struct visorchipset_device_info *bus_info);
	void (*device_pause)(struct visorchipset_device_info *bus_info);
	void (*device_resume)(struct visorchipset_device_info *bus_info);
};

/*  These functions live inside visorchipset, and will be called to indicate
 *  responses to specific events (by code outside of visorchipset).
 *  For now, the value for each response is simply either:
 *       0 = it worked
 *      -1 = it failed
 */
struct visorchipset_busdev_responders {
	void (*bus_create)(struct visorchipset_bus_info *p, int response);
	void (*bus_destroy)(struct visorchipset_bus_info *p, int response);
	void (*device_create)(struct visorchipset_device_info *p, int response);
	void (*device_destroy)(struct visorchipset_device_info *p,
			       int response);
	void (*device_pause)(struct visorchipset_device_info *p, int response);
	void (*device_resume)(struct visorchipset_device_info *p, int response);
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

bool visorchipset_get_bus_info(u32 bus_no,
			       struct visorchipset_bus_info *bus_info);
bool visorchipset_get_device_info(u32 bus_no, u32 dev_no,
				  struct visorchipset_device_info *dev_info);
bool visorchipset_set_bus_context(struct visorchipset_bus_info *bus_info,
				  void *context);

/* visorbus init and exit functions */
int visorbus_init(void);
void visorbus_exit(void);
#endif
