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

#include "periodic_work.h"
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

/** Information provided by each visor driver when it registers with the
 *  visorbus driver.
 */
struct visor_driver {
	const char *name;
	const char *version;
	const char *vertag;
	const char *build_date;
	const char *build_time;
	struct module *owner;

	/** Types of channels handled by this driver, ending with 0 GUID.
	 *  Our specialized BUS.match() method knows about this list, and
	 *  uses it to determine whether this driver will in fact handle a
	 *  new device that it has detected.
	 */
	struct visor_channeltype_descriptor *channel_types;

	/** Called when a new device comes online, by our probe() function
	 *  specified by driver.probe() (triggered ultimately by some call
	 *  to driver_register() / bus_add_driver() / driver_attach()).
	 */
	int (*probe)(struct visor_device *dev);

	/** Called when a new device is removed, by our remove() function
	 *  specified by driver.remove() (triggered ultimately by some call
	 *  to device_release_driver()).
	 */
	void (*remove)(struct visor_device *dev);

	/** Called periodically, whenever there is a possibility that
	 *  "something interesting" may have happened to the channel state.
	 */
	void (*channel_interrupt)(struct visor_device *dev);

	/** Called to initiate a change of the device's state.  If the return
	 *  valu`e is < 0, there was an error and the state transition will NOT
	 *  occur.  If the return value is >= 0, then the state transition was
	 *  INITIATED successfully, and complete_func() will be called (or was
	 *  just called) with the final status when either the state transition
	 *  fails or completes successfully.
	 */
	int (*pause)(struct visor_device *dev,
		     visorbus_state_complete_func complete_func);
	int (*resume)(struct visor_device *dev,
		      visorbus_state_complete_func complete_func);

	/** These fields are for private use by the bus driver only. */
	struct device_driver driver;
	struct driver_attribute version_attr;
};

#define to_visor_driver(x) ((x) ? \
	(container_of(x, struct visor_driver, driver)) : (NULL))

/** A device type for things "plugged" into the visorbus bus */

struct visor_device {
	/** visor driver can use the visorchannel member with the functions
	 *  defined in visorchannel.h to access the channel
	 */
	struct visorchannel *visorchannel;
	uuid_le channel_type_guid;
	u64 channel_bytes;

	/** These fields are for private use by the bus driver only.
	 *  A notable exception is that the visor driver can use
	 *  visor_get_drvdata() and visor_set_drvdata() to retrieve or stash
	 *  private visor driver specific data within the device member.
	 */
	struct device device;
	struct list_head list_all;
	struct periodic_work *periodic_work;
	bool being_removed;
	bool responded_to_device_create;
	struct kobject kobjdevmajorminor; /* visorbus<x>/dev<y>/devmajorminor/*/
	struct {
		int major, minor;
		void *attr;	/* private use by devmajorminor_attr.c you can
				   * change this constant to whatever you
				   * want; */
	} devnodes[5];
	/* the code will detect and behave appropriately) */
	struct semaphore visordriver_callback_lock;
	bool pausing;
	bool resuming;
	u32 chipset_bus_no;
	u32 chipset_dev_no;
	struct visorchipset_state state;
	uuid_le type;
	uuid_le inst;
	u8 *name;
	u8 *description;
	struct controlvm_message_header *pending_msg_hdr;
	void *vbus_hdr_info;
	u32 switch_no;
	u32 internal_port_no;
	uuid_le partition_uuid;
};

#define to_visor_device(x) container_of(x, struct visor_device, device)

#ifndef STANDALONE_CLIENT
int visorbus_register_visor_driver(struct visor_driver *);
void visorbus_unregister_visor_driver(struct visor_driver *);
int visorbus_read_channel(struct visor_device *dev,
			  unsigned long offset, void *dest,
			  unsigned long nbytes);
int visorbus_write_channel(struct visor_device *dev,
			   unsigned long offset, void *src,
			   unsigned long nbytes);
int visorbus_clear_channel(struct visor_device *dev,
			   unsigned long offset, u8 ch, unsigned long nbytes);
int visorbus_registerdevnode(struct visor_device *dev,
			     const char *name, int major, int minor);
void visorbus_enable_channel_interrupts(struct visor_device *dev);
void visorbus_disable_channel_interrupts(struct visor_device *dev);
#endif

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
int visorchannel_clear(struct visorchannel *channel, ulong offset,
		       u8 ch, ulong nbytes);
bool visorchannel_signalremove(struct visorchannel *channel, u32 queue,
			       void *msg);
bool visorchannel_signalinsert(struct visorchannel *channel, u32 queue,
			       void *msg);
int visorchannel_signalqueue_slots_avail(struct visorchannel *channel,
					 u32 queue);
int visorchannel_signalqueue_max_slots(struct visorchannel *channel, u32 queue);
u64 visorchannel_get_physaddr(struct visorchannel *channel);
ulong visorchannel_get_nbytes(struct visorchannel *channel);
char *visorchannel_id(struct visorchannel *channel, char *s);
char *visorchannel_zoneid(struct visorchannel *channel, char *s);
u64 visorchannel_get_clientpartition(struct visorchannel *channel);
int visorchannel_set_clientpartition(struct visorchannel *channel,
				     u64 partition_handle);
uuid_le visorchannel_get_uuid(struct visorchannel *channel);
char *visorchannel_uuid_id(uuid_le *guid, char *s);
void visorchannel_debug(struct visorchannel *channel, int num_queues,
			struct seq_file *seq, u32 off);
void __iomem *visorchannel_get_header(struct visorchannel *channel);

#define BUS_ROOT_DEVICE		UINT_MAX
struct visor_device *visorbus_get_device_by_id(u32 bus_no, u32 dev_no,
					       struct visor_device *from);
#endif
