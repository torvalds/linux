/* visorbus.h
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

/*
 *  This header file is to be included by other kernel mode components that
 *  implement a particular kind of visor_device.  Each of these other kernel
 *  mode components is called a visor device driver.  Refer to visortemplate
 *  for a minimal sample visor device driver.
 *
 *  There should be nothing in this file that is private to the visorbus
 *  bus implementation itself.
 *
 */

#ifndef __VISORBUS_H__
#define __VISORBUS_H__

#include <linux/device.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/uuid.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "channel.h"

struct visor_driver;
struct visor_device;
extern struct bus_type visorbus_type;

typedef void (*visorbus_state_complete_func) (struct visor_device *dev,
					      int status);
struct visorchipset_state {
	u32 created:1;
	u32 attached:1;
	u32 configured:1;
	u32 running:1;
	/* Add new fields above. */
	/* Remaining bits in this 32-bit word are unused. */
};

/** This struct describes a specific Supervisor channel, by providing its
 *  GUID, name, and sizes.
 */
struct visor_channeltype_descriptor {
	const uuid_le guid;
	const char *name;
};

/**
 * struct visor_driver - Information provided by each visor driver when it
 * registers with the visorbus driver.
 * @name:		Name of the visor driver.
 * @owner:		The module owner.
 * @channel_types:	Types of channels handled by this driver, ending with
 *			a zero GUID. Our specialized BUS.match() method knows
 *			about this list, and uses it to determine whether this
 *			driver will in fact handle a new device that it has
 *			detected.
 * @probe:		Called when a new device comes online, by our probe()
 *			function specified by driver.probe() (triggered
 *			ultimately by some call to driver_register(),
 *			bus_add_driver(), or driver_attach()).
 * @remove:		Called when a new device is removed, by our remove()
 *			function specified by driver.remove() (triggered
 *			ultimately by some call to device_release_driver()).
 * @channel_interrupt:	Called periodically, whenever there is a possiblity
 *			that "something interesting" may have happened to the
 *			channel.
 * @pause:		Called to initiate a change of the device's state.  If
 *			the return valu`e is < 0, there was an error and the
 *			state transition will NOT occur.  If the return value
 *			is >= 0, then the state transition was INITIATED
 *			successfully, and complete_func() will be called (or
 *			was just called) with the final status when either the
 *			state transition fails or completes successfully.
 * @resume:		Behaves similar to pause.
 * @driver:		Private reference to the device driver. For use by bus
 *			driver only.
 */
struct visor_driver {
	const char *name;
	struct module *owner;
	struct visor_channeltype_descriptor *channel_types;
	int (*probe)(struct visor_device *dev);
	void (*remove)(struct visor_device *dev);
	void (*channel_interrupt)(struct visor_device *dev);
	int (*pause)(struct visor_device *dev,
		     visorbus_state_complete_func complete_func);
	int (*resume)(struct visor_device *dev,
		      visorbus_state_complete_func complete_func);

	/* These fields are for private use by the bus driver only. */
	struct device_driver driver;
};

#define to_visor_driver(x) ((x) ? \
	(container_of(x, struct visor_driver, driver)) : (NULL))

/**
 * struct visor_device - A device type for things "plugged" into the visorbus
 * bus
 * @visorchannel:		Points to the channel that the device is
 *				associated with.
 * @channel_type_guid:		Identifies the channel type to the bus driver.
 * @device:			Device struct meant for use by the bus driver
 *				only.
 * @list_all:			Used by the bus driver to enumerate devices.
 * @timer:		        Timer fired periodically to do interrupt-type
 *				activity.
 * @being_removed:		Indicates that the device is being removed from
 *				the bus. Private bus driver use only.
 * @visordriver_callback_lock:	Used by the bus driver to lock when handling
 *				channel events.
 * @pausing:			Indicates that a change towards a paused state.
 *				is in progress. Only modified by the bus driver.
 * @resuming:			Indicates that a change towards a running state
 *				is in progress. Only modified by the bus driver.
 * @chipset_bus_no:		Private field used by the bus driver.
 * @chipset_dev_no:		Private field used the bus driver.
 * @state:			Used to indicate the current state of the
 *				device.
 * @inst:			Unique GUID for this instance of the device.
 * @name:			Name of the device.
 * @pending_msg_hdr:		For private use by bus driver to respond to
 *				hypervisor requests.
 * @vbus_hdr_info:		A pointer to header info. Private use by bus
 *				driver.
 * @partition_uuid:		Indicates client partion id. This should be the
 *				same across all visor_devices in the current
 *				guest. Private use by bus driver only.
 */

struct visor_device {
	struct visorchannel *visorchannel;
	uuid_le channel_type_guid;
	/* These fields are for private use by the bus driver only. */
	struct device device;
	struct list_head list_all;
	struct timer_list timer;
	bool timer_active;
	bool being_removed;
	struct mutex visordriver_callback_lock;
	bool pausing;
	bool resuming;
	u32 chipset_bus_no;
	u32 chipset_dev_no;
	struct visorchipset_state state;
	uuid_le inst;
	u8 *name;
	struct controlvm_message_header *pending_msg_hdr;
	void *vbus_hdr_info;
	uuid_le partition_uuid;
};

#define to_visor_device(x) container_of(x, struct visor_device, device)

int visorbus_register_visor_driver(struct visor_driver *);
void visorbus_unregister_visor_driver(struct visor_driver *);
int visorbus_read_channel(struct visor_device *dev,
			  unsigned long offset, void *dest,
			  unsigned long nbytes);
int visorbus_write_channel(struct visor_device *dev,
			   unsigned long offset, void *src,
			   unsigned long nbytes);
void visorbus_enable_channel_interrupts(struct visor_device *dev);
void visorbus_disable_channel_interrupts(struct visor_device *dev);

/* Levels of severity for diagnostic events, in order from lowest severity to
 * highest (i.e. fatal errors are the most severe, and should always be logged,
 * but info events rarely need to be logged except during debugging). The
 * values DIAG_SEVERITY_ENUM_BEGIN and DIAG_SEVERITY_ENUM_END are not valid
 * severity values.  They exist merely to dilineate the list, so that future
 * additions won't require changes to the driver (i.e. when checking for
 * out-of-range severities in SetSeverity). The values DIAG_SEVERITY_OVERRIDE
 * and DIAG_SEVERITY_SHUTOFF are not valid severity values for logging events
 * but they are valid for controlling the amount of event data. Changes made
 * to the enum, need to be reflected in s-Par.
 */
enum diag_severity {
	DIAG_SEVERITY_VERBOSE = 0,
	DIAG_SEVERITY_INFO = 1,
	DIAG_SEVERITY_WARNING = 2,
	DIAG_SEVERITY_ERR = 3,
	DIAG_SEVERITY_PRINT = 4,
};

int visorchannel_signalremove(struct visorchannel *channel, u32 queue,
			      void *msg);
int visorchannel_signalinsert(struct visorchannel *channel, u32 queue,
			      void *msg);
bool visorchannel_signalempty(struct visorchannel *channel, u32 queue);
uuid_le visorchannel_get_uuid(struct visorchannel *channel);

#define BUS_ROOT_DEVICE		UINT_MAX
struct visor_device *visorbus_get_device_by_id(u32 bus_no, u32 dev_no,
					       struct visor_device *from);
#endif
