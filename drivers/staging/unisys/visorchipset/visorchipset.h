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

#include "timskmod.h"
#include "channel.h"
#include "controlvmchannel.h"
#include "parser.h"
#include "procobjecttree.h"
#include "vbusdeviceinfo.h"
#include "vbushelper.h"

/** Describes the state from the perspective of which controlvm messages have
 *  been received for a bus or device.
 */
struct visorchipset_state {
	u32 created:1;
	u32 attached:1;
	u32 configured:1;
	u32 running:1;
	/* Add new fields above. */
	/* Remaining bits in this 32-bit word are unused. */
};

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

enum crash_obj_type {
	CRASH_DEV,
	CRASH_BUS,
};

/** Attributes for a particular Supervisor channel.
 */
struct visorchipset_channel_info {
	enum visorchipset_addresstype addr_type;
	HOSTADDRESS channel_addr;
	struct irq_info intr;
	u64 n_channel_bytes;
	uuid_le channel_type_uuid;
	uuid_le channel_inst_uuid;

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
	struct visorchipset_channel_info chan_info;
	u32 reserved1;		/* control_vm_id */
	u64 reserved2;
	u32 switch_no;		/* when devState.attached==1 */
	u32 internal_port_no;	/* when devState.attached==1 */
	struct controlvm_message_header pending_msg_hdr;/* CONTROLVM_MESSAGE */
	/** For private use by the bus driver */
	void *bus_driver_context;

};

static inline struct visorchipset_device_info *finddevice(
		struct list_head *list, u32 bus_no, u32 dev_no)
{
	struct visorchipset_device_info *p;

	list_for_each_entry(p, list, entry) {
		if (p->bus_no == bus_no && p->dev_no == dev_no)
			return p;
	}
	return NULL;
}

static inline void delbusdevices(struct list_head *list, u32 bus_no)
{
	struct visorchipset_device_info *p, *tmp;

	list_for_each_entry_safe(p, tmp, list, entry) {
		if (p->bus_no == bus_no) {
			list_del(&p->entry);
			kfree(p);
		}
	}
}

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
	struct visorchipset_channel_info chan_info;
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
	struct controlvm_message_header pending_msg_hdr;/* CONTROLVM MsgHdr */
	/** For private use by the bus driver */
	void *bus_driver_context;
	u32 dev_no;
};

static inline struct visorchipset_bus_info *
findbus(struct list_head *list, u32 bus_no)
{
	struct visorchipset_bus_info *p;

	list_for_each_entry(p, list, entry) {
		if (p->bus_no == bus_no)
			return p;
	}
	return NULL;
}

/*  These functions will be called from within visorchipset when certain
 *  events happen.  (The implementation of these functions is outside of
 *  visorchipset.)
 */
struct visorchipset_busdev_notifiers {
	void (*bus_create)(u32 bus_no);
	void (*bus_destroy)(u32 bus_no);
	void (*device_create)(u32 bus_no, u32 dev_no);
	void (*device_destroy)(u32 bus_no, u32 dev_no);
	void (*device_pause)(u32 bus_no, u32 dev_no);
	void (*device_resume)(u32 bus_no, u32 dev_no);
};

/*  These functions live inside visorchipset, and will be called to indicate
 *  responses to specific events (by code outside of visorchipset).
 *  For now, the value for each response is simply either:
 *       0 = it worked
 *      -1 = it failed
 */
struct visorchipset_busdev_responders {
	void (*bus_create)(u32 bus_no, int response);
	void (*bus_destroy)(u32 bus_no, int response);
	void (*device_create)(u32 bus_no, u32 dev_no, int response);
	void (*device_destroy)(u32 bus_no, u32 dev_no, int response);
	void (*device_pause)(u32 bus_no, u32 dev_no, int response);
	void (*device_resume)(u32 bus_no, u32 dev_no, int response);
};

/** Register functions (in the bus driver) to get called by visorchipset
 *  whenever a bus or device appears for which this service partition is
 *  to be the server for.  visorchipset will fill in <responders>, to
 *  indicate functions the bus driver should call to indicate message
 *  responses.
 */
void
visorchipset_register_busdev_client(
			struct visorchipset_busdev_notifiers *notifiers,
			struct visorchipset_busdev_responders *responders,
			struct ultra_vbus_deviceinfo *driver_info);

/** Register functions (in the bus driver) to get called by visorchipset
 *  whenever a bus or device appears for which this service partition is
 *  to be the client for.  visorchipset will fill in <responders>, to
 *  indicate functions the bus driver should call to indicate message
 *  responses.
 */
void
visorchipset_register_busdev_server(
			struct visorchipset_busdev_notifiers *notifiers,
			struct visorchipset_busdev_responders *responders,
			struct ultra_vbus_deviceinfo *driver_info);

typedef void (*SPARREPORTEVENT_COMPLETE_FUNC) (struct controlvm_message *msg,
					       int status);

void visorchipset_device_pause_response(u32 bus_no, u32 dev_no, int response);

bool visorchipset_get_bus_info(u32 bus_no,
			       struct visorchipset_bus_info *bus_info);
bool visorchipset_get_device_info(u32 bus_no, u32 dev_no,
				  struct visorchipset_device_info *dev_info);
bool visorchipset_set_bus_context(u32 bus_no, void *context);
bool visorchipset_set_device_context(u32 bus_no, u32 dev_no, void *context);
int visorchipset_chipset_ready(void);
int visorchipset_chipset_selftest(void);
int visorchipset_chipset_notready(void);
void visorchipset_save_message(struct controlvm_message *msg,
			       enum crash_obj_type type);
void *visorchipset_cache_alloc(struct kmem_cache *pool,
			       bool ok_to_block, char *fn, int ln);
void visorchipset_cache_free(struct kmem_cache *pool, void *p,
			     char *fn, int ln);

#endif
