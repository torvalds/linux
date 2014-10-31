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
typedef struct {
	struct list_head entry;
	u32 busNo;
	u32 devNo;
	uuid_le devInstGuid;
	struct visorchipset_state state;
	struct visorchipset_channel_info chanInfo;
	u32 Reserved1;		/* control_vm_id */
	u64 Reserved2;
	u32 switchNo;		/* when devState.attached==1 */
	u32 internalPortNo;	/* when devState.attached==1 */
	struct controlvm_message_header pendingMsgHdr;	/* CONTROLVM_MESSAGE */
	/** For private use by the bus driver */
	void *bus_driver_context;

} VISORCHIPSET_DEVICE_INFO;

static inline VISORCHIPSET_DEVICE_INFO *
finddevice(struct list_head *list, u32 busNo, u32 devNo)
{
	VISORCHIPSET_DEVICE_INFO *p;

	list_for_each_entry(p, list, entry) {
		if (p->busNo == busNo && p->devNo == devNo)
			return p;
	}
	return NULL;
}

static inline void delbusdevices(struct list_head *list, u32 busNo)
{
	VISORCHIPSET_DEVICE_INFO *p, *tmp;

	list_for_each_entry_safe(p, tmp, list, entry) {
		if (p->busNo == busNo) {
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
typedef struct {
	struct list_head entry;
	u32 busNo;
	struct visorchipset_state state;
	struct visorchipset_channel_info chanInfo;
	uuid_le partitionGuid;
	u64 partitionHandle;
	u8 *name;		/* UTF8 */
	u8 *description;	/* UTF8 */
	u64 Reserved1;
	u32 Reserved2;
	MYPROCOBJECT *procObject;
	struct {
		u32 server:1;
		/* Add new fields above. */
		/* Remaining bits in this 32-bit word are unused. */
	} flags;
	struct controlvm_message_header pendingMsgHdr;	/* CONTROLVM MsgHdr */
	/** For private use by the bus driver */
	void *bus_driver_context;
	u64 devNo;

} VISORCHIPSET_BUS_INFO;

static inline VISORCHIPSET_BUS_INFO *
findbus(struct list_head *list, u32 busNo)
{
	VISORCHIPSET_BUS_INFO *p;

	list_for_each_entry(p, list, entry) {
		if (p->busNo == busNo)
			return p;
	}
	return NULL;
}

/** Attributes for a particular Supervisor switch.
 */
typedef struct {
	u32 switchNo;
	struct visorchipset_state state;
	uuid_le switchTypeGuid;
	u8 *authService1;
	u8 *authService2;
	u8 *authService3;
	u8 *securityContext;
	u64 Reserved;
	u32 Reserved2;		/* control_vm_id */
	struct device dev;
	BOOL dev_exists;
	struct controlvm_message_header pendingMsgHdr;

} VISORCHIPSET_SWITCH_INFO;

/** Attributes for a particular Supervisor external port, which is connected
 *  to a specific switch.
 */
typedef struct {
	u32 switchNo;
	u32 externalPortNo;
	struct visorchipset_state state;
	uuid_le networkZoneGuid;
	int pdPort;
	u8 *ip;
	u8 *ipNetmask;
	u8 *ipBroadcast;
	u8 *ipNetwork;
	u8 *ipGateway;
	u8 *ipDNS;
	u64 Reserved1;
	u32 Reserved2;		/* control_vm_id */
	struct device dev;
	BOOL dev_exists;
	struct controlvm_message_header pendingMsgHdr;

} VISORCHIPSET_EXTERNALPORT_INFO;

/** Attributes for a particular Supervisor internal port, which is how a
 *  device connects to a particular switch.
 */
typedef struct {
	u32 switchNo;
	u32 internalPortNo;
	struct visorchipset_state state;
	u32 busNo;		/* valid only when state.attached == 1 */
	u32 devNo;		/* valid only when state.attached == 1 */
	u64 Reserved1;
	u32 Reserved2;		/* CONTROLVM_ID */
	struct controlvm_message_header pendingMsgHdr;
	MYPROCOBJECT *procObject;

} VISORCHIPSET_INTERNALPORT_INFO;

/*  These functions will be called from within visorchipset when certain
 *  events happen.  (The implementation of these functions is outside of
 *  visorchipset.)
 */
typedef struct {
	void (*bus_create)(ulong busNo);
	void (*bus_destroy)(ulong busNo);
	void (*device_create)(ulong busNo, ulong devNo);
	void (*device_destroy)(ulong busNo, ulong devNo);
	void (*device_pause)(ulong busNo, ulong devNo);
	void (*device_resume)(ulong busNo, ulong devNo);
	int (*get_channel_info)(uuid_le typeGuid, ulong *minSize,
				 ulong *maxSize);
} VISORCHIPSET_BUSDEV_NOTIFIERS;

/*  These functions live inside visorchipset, and will be called to indicate
 *  responses to specific events (by code outside of visorchipset).
 *  For now, the value for each response is simply either:
 *       0 = it worked
 *      -1 = it failed
 */
typedef struct {
	void (*bus_create)(ulong busNo, int response);
	void (*bus_destroy)(ulong busNo, int response);
	void (*device_create)(ulong busNo, ulong devNo, int response);
	void (*device_destroy)(ulong busNo, ulong devNo, int response);
	void (*device_pause)(ulong busNo, ulong devNo, int response);
	void (*device_resume)(ulong busNo, ulong devNo, int response);
} VISORCHIPSET_BUSDEV_RESPONDERS;

/** Register functions (in the bus driver) to get called by visorchipset
 *  whenever a bus or device appears for which this service partition is
 *  to be the server for.  visorchipset will fill in <responders>, to
 *  indicate functions the bus driver should call to indicate message
 *  responses.
 */
void
visorchipset_register_busdev_client(VISORCHIPSET_BUSDEV_NOTIFIERS *notifiers,
				    VISORCHIPSET_BUSDEV_RESPONDERS *responders,
				    struct ultra_vbus_deviceinfo *driverInfo);

/** Register functions (in the bus driver) to get called by visorchipset
 *  whenever a bus or device appears for which this service partition is
 *  to be the client for.  visorchipset will fill in <responders>, to
 *  indicate functions the bus driver should call to indicate message
 *  responses.
 */
void
visorchipset_register_busdev_server(VISORCHIPSET_BUSDEV_NOTIFIERS *notifiers,
				    VISORCHIPSET_BUSDEV_RESPONDERS *responders,
				    struct ultra_vbus_deviceinfo *driverInfo);

typedef void (*SPARREPORTEVENT_COMPLETE_FUNC) (struct controlvm_message *msg,
					       int status);

void visorchipset_device_pause_response(ulong busNo, ulong devNo, int response);

BOOL visorchipset_get_bus_info(ulong busNo, VISORCHIPSET_BUS_INFO *busInfo);
BOOL visorchipset_get_device_info(ulong busNo, ulong devNo,
				  VISORCHIPSET_DEVICE_INFO *devInfo);
BOOL visorchipset_get_switch_info(ulong switchNo,
				  VISORCHIPSET_SWITCH_INFO *switchInfo);
BOOL visorchipset_get_externalport_info(ulong switchNo, ulong externalPortNo,
					VISORCHIPSET_EXTERNALPORT_INFO
					*externalPortInfo);
BOOL visorchipset_set_bus_context(ulong busNo, void *context);
BOOL visorchipset_set_device_context(ulong busNo, ulong devNo, void *context);
int visorchipset_chipset_ready(void);
int visorchipset_chipset_selftest(void);
int visorchipset_chipset_notready(void);
void visorchipset_controlvm_respond_reportEvent(struct controlvm_message *msg,
						void *payload);
void visorchipset_save_message(struct controlvm_message *msg,
			       enum crash_obj_type type);
void *visorchipset_cache_alloc(struct kmem_cache *pool,
			       BOOL ok_to_block, char *fn, int ln);
void visorchipset_cache_free(struct kmem_cache *pool, void *p,
			     char *fn, int ln);

#if defined(TRANSMITFILE_DEBUG) || defined(DEBUG)
#define DBG_GETFILE_PAYLOAD(msg, controlvm_header)      \
	LOGINF(msg,                                     \
	       (ulong)controlvm_header.PayloadVmOffset, \
	       (ulong)controlvm_header.PayloadMaxBytes)
#define DBG_GETFILE(fmt, ...)  LOGINF(fmt, ##__VA_ARGS__)
#define DBG_PUTFILE(fmt, ...)  LOGINF(fmt, ##__VA_ARGS__)
#else
#define DBG_GETFILE_PAYLOAD(msg, controlvm_header)
#define DBG_GETFILE(fmt, ...)
#define DBG_PUTFILE(fmt, ...)
#endif

#endif
