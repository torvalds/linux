// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 */

/*
 *  This header file is to be included by other kernel mode components that
 *  implement a particular kind of visor_device.  Each of these other kernel
 *  mode components is called a visor device driver.  Refer to visortemplate
 *  for a minimal sample visor device driver.
 *
 *  There should be nothing in this file that is private to the visorbus
 *  bus implementation itself.
 */

#ifndef __VISORBUS_H__
#define __VISORBUS_H__

#include <linux/device.h>

#include "visorchannel.h"

struct visorchipset_state {
	u32 created:1;
	u32 attached:1;
	u32 configured:1;
	u32 running:1;
	/* Remaining bits in this 32-bit word are reserved. */
};

/**
 * struct visor_device - A device type for things "plugged" into the visorbus
 *                       bus
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
 * @visordriver_callback_lock:	Used by the bus driver to lock when adding and
 *				removing devices.
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
 * @partition_guid:		Indicates client partion id. This should be the
 *				same across all visor_devices in the current
 *				guest. Private use by bus driver only.
 */
struct visor_device {
	struct visorchannel *visorchannel;
	guid_t channel_type_guid;
	/* These fields are for private use by the bus driver only. */
	struct device device;
	struct list_head list_all;
	struct timer_list timer;
	bool timer_active;
	bool being_removed;
	struct mutex visordriver_callback_lock; /* synchronize probe/remove */
	bool pausing;
	bool resuming;
	u32 chipset_bus_no;
	u32 chipset_dev_no;
	struct visorchipset_state state;
	guid_t inst;
	u8 *name;
	struct controlvm_message_header *pending_msg_hdr;
	void *vbus_hdr_info;
	guid_t partition_guid;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_bus_info;
};

#define to_visor_device(x) container_of(x, struct visor_device, device)

typedef void (*visorbus_state_complete_func) (struct visor_device *dev,
					      int status);

/*
 * This struct describes a specific visor channel, by providing its GUID, name,
 * and sizes.
 */
struct visor_channeltype_descriptor {
	const guid_t guid;
	const char *name;
	u64 min_bytes;
	u32 version;
};

/**
 * struct visor_driver - Information provided by each visor driver when it
 *                       registers with the visorbus driver
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

#define to_visor_driver(x) (container_of(x, struct visor_driver, driver))

int visor_check_channel(struct channel_header *ch, struct device *dev,
			const guid_t *expected_uuid, char *chname,
			u64 expected_min_bytes,	u32 expected_version,
			u64 expected_signature);

int visorbus_register_visor_driver(struct visor_driver *drv);
void visorbus_unregister_visor_driver(struct visor_driver *drv);
int visorbus_read_channel(struct visor_device *dev,
			  unsigned long offset, void *dest,
			  unsigned long nbytes);
int visorbus_write_channel(struct visor_device *dev,
			   unsigned long offset, void *src,
			   unsigned long nbytes);
int visorbus_enable_channel_interrupts(struct visor_device *dev);
void visorbus_disable_channel_interrupts(struct visor_device *dev);

int visorchannel_signalremove(struct visorchannel *channel, u32 queue,
			      void *msg);
int visorchannel_signalinsert(struct visorchannel *channel, u32 queue,
			      void *msg);
bool visorchannel_signalempty(struct visorchannel *channel, u32 queue);
const guid_t *visorchannel_get_guid(struct visorchannel *channel);

#define BUS_ROOT_DEVICE UINT_MAX
struct visor_device *visorbus_get_device_by_id(u32 bus_no, u32 dev_no,
					       struct visor_device *from);
#endif
