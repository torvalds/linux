/* uislib.c
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

/* @ALL_INSPECTED */
#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/highmem.h>
#ifdef CONFIG_MODVERSIONS
#include <config/modversions.h>
#endif
#include <linux/module.h>
#include <linux/debugfs.h>

#include <linux/types.h>
#include <linux/uuid.h>

#include <linux/version.h>
#include "diagnostics/appos_subsystems.h"
#include "uisutils.h"
#include "vbuschannel.h"

#include <linux/proc_fs.h>
#include <linux/uaccess.h>	/* for copy_from_user */
#include <linux/ctype.h>	/* for toupper */
#include <linux/list.h>

#include "sparstop.h"
#include "visorchipset.h"
#include "version.h"
#include "guestlinuxdebug.h"

#define SET_PROC_OWNER(x, y)

#define POLLJIFFIES_NORMAL 1
/* Choose whether or not you want to wakeup the request-polling thread
 * after an IO termination:
 * this is shorter than using __FILE__ (full path name) in
 * debug/info/error messages
 */
#define CURRENT_FILE_PC UISLIB_PC_uislib_c
#define __MYFILE__ "uislib.c"

/* global function pointers that act as callback functions into virtpcimod */
int (*virt_control_chan_func)(struct guest_msgs *);

static int debug_buf_valid;
static char *debug_buf;	/* Note this MUST be global,
					 * because the contents must */
static unsigned int chipset_inited;

#define WAIT_ON_CALLBACK(handle)	\
	do {			\
		if (handle)		\
			break;		\
		UIS_THREAD_WAIT;	\
	} while (1)

static struct bus_info *bus_list;
static rwlock_t bus_list_lock;
static int bus_list_count;	/* number of buses in the list */
static int max_bus_count;		/* maximum number of buses expected */
static u64 phys_data_chan;
static int platform_no;

static struct uisthread_info incoming_ti;
static BOOL incoming_started = FALSE;
static LIST_HEAD(poll_dev_chan);
static unsigned long long tot_moved_to_tail_cnt;
static unsigned long long tot_wait_cnt;
static unsigned long long tot_wakeup_cnt;
static unsigned long long tot_schedule_cnt;
static int en_smart_wakeup = 1;
static DEFINE_SEMAPHORE(poll_dev_lock);	/* unlocked */
static DECLARE_WAIT_QUEUE_HEAD(poll_dev_wake_q);
static int poll_dev_start;

#define CALLHOME_PROC_ENTRY_FN "callhome"
#define CALLHOME_THROTTLED_PROC_ENTRY_FN "callhome_throttled"

#define DIR_DEBUGFS_ENTRY "uislib"
static struct dentry *dir_debugfs;

#define PLATFORMNUMBER_DEBUGFS_ENTRY_FN "platform"
static struct dentry *platformnumber_debugfs_read;

#define CYCLES_BEFORE_WAIT_DEBUGFS_ENTRY_FN "cycles_before_wait"
static struct dentry *cycles_before_wait_debugfs_read;

#define SMART_WAKEUP_DEBUGFS_ENTRY_FN "smart_wakeup"
static struct dentry *smart_wakeup_debugfs_entry;

#define INFO_DEBUGFS_ENTRY_FN "info"
static struct dentry *info_debugfs_entry;

static unsigned long long cycles_before_wait, wait_cycles;

/*****************************************************/
/* local functions                                   */
/*****************************************************/

static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset);
static const struct file_operations debugfs_info_fops = {
	.read = info_debugfs_read,
};

static void
init_msg_header(struct controlvm_message *msg, u32 id, uint rsp, uint svr)
{
	memset(msg, 0, sizeof(struct controlvm_message));
	msg->hdr.id = id;
	msg->hdr.flags.response_expected = rsp;
	msg->hdr.flags.server = svr;
}

static __iomem void *init_vbus_channel(u64 ch_addr, u32 ch_bytes)
{
	void __iomem *ch = uislib_ioremap_cache(ch_addr, ch_bytes);

	if (!ch)
		return NULL;

	if (!SPAR_VBUS_CHANNEL_OK_CLIENT(ch)) {
		uislib_iounmap(ch);
		return NULL;
	}
	return ch;
}

static int
create_bus(struct controlvm_message *msg, char *buf)
{
	u32 bus_no, dev_count;
	struct bus_info *tmp, *bus;
	size_t size;

	if (max_bus_count == bus_list_count) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, max_bus_count,
				 POSTCODE_SEVERITY_ERR);
		return CONTROLVM_RESP_ERROR_MAX_BUSES;
	}

	bus_no = msg->cmd.create_bus.bus_no;
	dev_count = msg->cmd.create_bus.dev_count;

	POSTCODE_LINUX_4(BUS_CREATE_ENTRY_PC, bus_no, dev_count,
			 POSTCODE_SEVERITY_INFO);

	size =
	    sizeof(struct bus_info) +
	    (dev_count * sizeof(struct device_info *));
	bus = kzalloc(size, GFP_ATOMIC);
	if (!bus) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
	}

	/* Currently by default, the bus Number is the GuestHandle.
	 * Configure Bus message can override this.
	 */
	if (msg->hdr.flags.test_message) {
		/* This implies we're the IOVM so set guest handle to 0... */
		bus->guest_handle = 0;
		bus->bus_no = bus_no;
		bus->local_vnic = 1;
	} else {
		bus->bus_no = bus_no;
		bus->guest_handle = bus_no;
	}
	sprintf(bus->name, "%d", (int)bus->bus_no);
	bus->device_count = dev_count;
	bus->device =
	    (struct device_info **)((char *)bus + sizeof(struct bus_info));
	bus->bus_inst_uuid = msg->cmd.create_bus.bus_inst_uuid;
	bus->bus_channel_bytes = 0;
	bus->bus_channel = NULL;

	/* add bus to our bus list - but check for duplicates first */
	read_lock(&bus_list_lock);
	for (tmp = bus_list; tmp; tmp = tmp->next) {
		if (tmp->bus_no == bus->bus_no)
			break;
	}
	read_unlock(&bus_list_lock);
	if (tmp) {
		/* found a bus already in the list with same bus_no -
		 * reject add
		 */
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus->bus_no,
				 POSTCODE_SEVERITY_ERR);
		kfree(bus);
		return CONTROLVM_RESP_ERROR_ALREADY_DONE;
	}
	if ((msg->cmd.create_bus.channel_addr != 0) &&
	    (msg->cmd.create_bus.channel_bytes != 0)) {
		bus->bus_channel_bytes = msg->cmd.create_bus.channel_bytes;
		bus->bus_channel =
		    init_vbus_channel(msg->cmd.create_bus.channel_addr,
				      msg->cmd.create_bus.channel_bytes);
	}
	/* the msg is bound for virtpci; send guest_msgs struct to callback */
	if (!msg->hdr.flags.server) {
		struct guest_msgs cmd;

		cmd.msgtype = GUEST_ADD_VBUS;
		cmd.add_vbus.bus_no = bus_no;
		cmd.add_vbus.chanptr = bus->bus_channel;
		cmd.add_vbus.dev_count = dev_count;
		cmd.add_vbus.bus_uuid = msg->cmd.create_bus.bus_data_type_uuid;
		cmd.add_vbus.instance_uuid = msg->cmd.create_bus.bus_inst_uuid;
		if (!virt_control_chan_func) {
			POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus->bus_no,
					 POSTCODE_SEVERITY_ERR);
			kfree(bus);
			return CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;
		}
		if (!virt_control_chan_func(&cmd)) {
			POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus->bus_no,
					 POSTCODE_SEVERITY_ERR);
			kfree(bus);
			return
			    CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;
		}
	}

	/* add bus at the head of our list */
	write_lock(&bus_list_lock);
	if (!bus_list) {
		bus_list = bus;
	} else {
		bus->next = bus_list;
		bus_list = bus;
	}
	bus_list_count++;
	write_unlock(&bus_list_lock);

	POSTCODE_LINUX_3(BUS_CREATE_EXIT_PC, bus->bus_no,
			 POSTCODE_SEVERITY_INFO);
	return CONTROLVM_RESP_SUCCESS;
}

static int
destroy_bus(struct controlvm_message *msg, char *buf)
{
	int i;
	struct bus_info *bus, *prev = NULL;
	struct guest_msgs cmd;
	u32 bus_no;

	bus_no = msg->cmd.destroy_bus.bus_no;

	read_lock(&bus_list_lock);

	bus = bus_list;
	while (bus) {
		if (bus->bus_no == bus_no)
			break;
		prev = bus;
		bus = bus->next;
	}

	if (!bus) {
		read_unlock(&bus_list_lock);
		return CONTROLVM_RESP_ERROR_ALREADY_DONE;
	}

	/* verify that this bus has no devices. */
	for (i = 0; i < bus->device_count; i++) {
		if (bus->device[i] != NULL) {
			read_unlock(&bus_list_lock);
			return CONTROLVM_RESP_ERROR_BUS_DEVICE_ATTACHED;
		}
	}
	read_unlock(&bus_list_lock);

	if (msg->hdr.flags.server)
		goto remove;

	/* client messages require us to call the virtpci callback associated
	   with this bus. */
	cmd.msgtype = GUEST_DEL_VBUS;
	cmd.del_vbus.bus_no = bus_no;
	if (!virt_control_chan_func)
		return CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;

	if (!virt_control_chan_func(&cmd))
		return CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;

	/* finally, remove the bus from the list */
remove:
	write_lock(&bus_list_lock);
	if (prev)	/* not at head */
		prev->next = bus->next;
	else
		bus_list = bus->next;
	bus_list_count--;
	write_unlock(&bus_list_lock);

	if (bus->bus_channel) {
		uislib_iounmap(bus->bus_channel);
		bus->bus_channel = NULL;
	}

	kfree(bus);
	return CONTROLVM_RESP_SUCCESS;
}

static int create_device(struct controlvm_message *msg, char *buf)
{
	struct device_info *dev;
	struct bus_info *bus;
	struct guest_msgs cmd;
	u32 bus_no, dev_no;
	int result = CONTROLVM_RESP_SUCCESS;
	u64 min_size = MIN_IO_CHANNEL_SIZE;
	struct req_handler_info *req_handler;

	bus_no = msg->cmd.create_device.bus_no;
	dev_no = msg->cmd.create_device.dev_no;

	POSTCODE_LINUX_4(DEVICE_CREATE_ENTRY_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);

	dev = kzalloc(sizeof(*dev), GFP_ATOMIC);
	if (!dev) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
	}

	dev->channel_uuid = msg->cmd.create_device.data_type_uuid;
	dev->intr = msg->cmd.create_device.intr;
	dev->channel_addr = msg->cmd.create_device.channel_addr;
	dev->bus_no = bus_no;
	dev->dev_no = dev_no;
	sema_init(&dev->interrupt_callback_lock, 1);	/* unlocked */
	sprintf(dev->devid, "vbus%u:dev%u", (unsigned)bus_no, (unsigned)dev_no);
	/* map the channel memory for the device. */
	if (msg->hdr.flags.test_message) {
		dev->chanptr = (void __iomem *)__va(dev->channel_addr);
	} else {
		req_handler = req_handler_find(dev->channel_uuid);
		if (req_handler)
			/* generic service handler registered for this
			 * channel
			 */
			min_size = req_handler->min_channel_bytes;
		if (min_size > msg->cmd.create_device.channel_bytes) {
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			result = CONTROLVM_RESP_ERROR_CHANNEL_SIZE_TOO_SMALL;
			goto cleanup;
		}
		dev->chanptr =
		    uislib_ioremap_cache(dev->channel_addr,
					 msg->cmd.create_device.channel_bytes);
		if (!dev->chanptr) {
			result = CONTROLVM_RESP_ERROR_IOREMAP_FAILED;
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			goto cleanup;
		}
	}
	dev->instance_uuid = msg->cmd.create_device.dev_inst_uuid;
	dev->channel_bytes = msg->cmd.create_device.channel_bytes;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (bus->bus_no != bus_no)
			continue;
		/* make sure the device number is valid */
		if (dev_no >= bus->device_count) {
			result = CONTROLVM_RESP_ERROR_MAX_DEVICES;
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			read_unlock(&bus_list_lock);
			goto cleanup;
		}
		/* make sure this device is not already set */
		if (bus->device[dev_no]) {
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC,
					 dev_no, bus_no,
					 POSTCODE_SEVERITY_ERR);
			result = CONTROLVM_RESP_ERROR_ALREADY_DONE;
			read_unlock(&bus_list_lock);
			goto cleanup;
		}
		read_unlock(&bus_list_lock);
		/* the msg is bound for virtpci; send
		 * guest_msgs struct to callback
		 */
		if (msg->hdr.flags.server) {
			bus->device[dev_no] = dev;
			POSTCODE_LINUX_4(DEVICE_CREATE_SUCCESS_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_INFO);
			return CONTROLVM_RESP_SUCCESS;
		}
		if (uuid_le_cmp(dev->channel_uuid,
				spar_vhba_channel_protocol_uuid) == 0) {
			wait_for_valid_guid(&((struct channel_header __iomem *)
					    (dev->chanptr))->chtype);
			if (!SPAR_VHBA_CHANNEL_OK_CLIENT(dev->chanptr)) {
				POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC,
						 dev_no, bus_no,
						 POSTCODE_SEVERITY_ERR);
				result = CONTROLVM_RESP_ERROR_CHANNEL_INVALID;
				goto cleanup;
			}
			cmd.msgtype = GUEST_ADD_VHBA;
			cmd.add_vhba.chanptr = dev->chanptr;
			cmd.add_vhba.bus_no = bus_no;
			cmd.add_vhba.device_no = dev_no;
			cmd.add_vhba.instance_uuid = dev->instance_uuid;
			cmd.add_vhba.intr = dev->intr;
		} else if (uuid_le_cmp(dev->channel_uuid,
				       spar_vnic_channel_protocol_uuid) == 0) {
			wait_for_valid_guid(&((struct channel_header __iomem *)
					    (dev->chanptr))->chtype);
			if (!SPAR_VNIC_CHANNEL_OK_CLIENT(dev->chanptr)) {
				POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC,
						 dev_no, bus_no,
						 POSTCODE_SEVERITY_ERR);
				result = CONTROLVM_RESP_ERROR_CHANNEL_INVALID;
				goto cleanup;
			}
			cmd.msgtype = GUEST_ADD_VNIC;
			cmd.add_vnic.chanptr = dev->chanptr;
			cmd.add_vnic.bus_no = bus_no;
			cmd.add_vnic.device_no = dev_no;
			cmd.add_vnic.instance_uuid = dev->instance_uuid;
			cmd.add_vhba.intr = dev->intr;
		} else {
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			result = CONTROLVM_RESP_ERROR_CHANNEL_TYPE_UNKNOWN;
			goto cleanup;
		}

		if (!virt_control_chan_func) {
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			result = CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;
			goto cleanup;
		}

		if (!virt_control_chan_func(&cmd)) {
			POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no,
					 bus_no, POSTCODE_SEVERITY_ERR);
			result =
			     CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;
			goto cleanup;
		}

		bus->device[dev_no] = dev;
		POSTCODE_LINUX_4(DEVICE_CREATE_SUCCESS_PC, dev_no,
				 bus_no, POSTCODE_SEVERITY_INFO);
		return CONTROLVM_RESP_SUCCESS;
	}
	read_unlock(&bus_list_lock);

	POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_ERR);
	result = CONTROLVM_RESP_ERROR_BUS_INVALID;

cleanup:
	if (!msg->hdr.flags.test_message) {
		uislib_iounmap(dev->chanptr);
		dev->chanptr = NULL;
	}

	kfree(dev);
	return result;
}

static int pause_device(struct controlvm_message *msg)
{
	u32 bus_no, dev_no;
	struct bus_info *bus;
	struct device_info *dev;
	struct guest_msgs cmd;
	int retval = CONTROLVM_RESP_SUCCESS;

	bus_no = msg->cmd.device_change_state.bus_no;
	dev_no = msg->cmd.device_change_state.dev_no;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (bus->bus_no == bus_no) {
			/* make sure the device number is valid */
			if (dev_no >= bus->device_count) {
				retval = CONTROLVM_RESP_ERROR_DEVICE_INVALID;
			} else {
				/* make sure this device exists */
				dev = bus->device[dev_no];
				if (!dev) {
					retval =
					  CONTROLVM_RESP_ERROR_ALREADY_DONE;
				}
			}
			break;
		}
	}
	if (!bus)
		retval = CONTROLVM_RESP_ERROR_BUS_INVALID;

	read_unlock(&bus_list_lock);
	if (retval == CONTROLVM_RESP_SUCCESS) {
		/* the msg is bound for virtpci; send
		 * guest_msgs struct to callback
		 */
		if (uuid_le_cmp(dev->channel_uuid,
				spar_vhba_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_PAUSE_VHBA;
			cmd.pause_vhba.chanptr = dev->chanptr;
		} else if (uuid_le_cmp(dev->channel_uuid,
				       spar_vnic_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_PAUSE_VNIC;
			cmd.pause_vnic.chanptr = dev->chanptr;
		} else {
			return CONTROLVM_RESP_ERROR_CHANNEL_TYPE_UNKNOWN;
		}
		if (!virt_control_chan_func)
			return CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;
		if (!virt_control_chan_func(&cmd)) {
			return
			  CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;
		}
	}
	return retval;
}

static int resume_device(struct controlvm_message *msg)
{
	u32 bus_no, dev_no;
	struct bus_info *bus;
	struct device_info *dev;
	struct guest_msgs cmd;
	int retval = CONTROLVM_RESP_SUCCESS;

	bus_no = msg->cmd.device_change_state.bus_no;
	dev_no = msg->cmd.device_change_state.dev_no;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (bus->bus_no == bus_no) {
			/* make sure the device number is valid */
			if (dev_no >= bus->device_count) {
				retval = CONTROLVM_RESP_ERROR_DEVICE_INVALID;
			} else {
				/* make sure this device exists */
				dev = bus->device[dev_no];
				if (!dev) {
					retval =
					  CONTROLVM_RESP_ERROR_ALREADY_DONE;
				}
			}
			break;
		}
	}

	if (!bus)
		retval = CONTROLVM_RESP_ERROR_BUS_INVALID;

	read_unlock(&bus_list_lock);
	/* the msg is bound for virtpci; send
	 * guest_msgs struct to callback
	 */
	if (retval == CONTROLVM_RESP_SUCCESS) {
		if (uuid_le_cmp(dev->channel_uuid,
				spar_vhba_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_RESUME_VHBA;
			cmd.resume_vhba.chanptr = dev->chanptr;
		} else if (uuid_le_cmp(dev->channel_uuid,
				       spar_vnic_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_RESUME_VNIC;
			cmd.resume_vnic.chanptr = dev->chanptr;
		} else {
			return CONTROLVM_RESP_ERROR_CHANNEL_TYPE_UNKNOWN;
		}
		if (!virt_control_chan_func)
			return CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;
		if (!virt_control_chan_func(&cmd)) {
			return
			  CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;
		}
	}
	return retval;
}

static int destroy_device(struct controlvm_message *msg, char *buf)
{
	u32 bus_no, dev_no;
	struct bus_info *bus;
	struct device_info *dev;
	struct guest_msgs cmd;
	int retval = CONTROLVM_RESP_SUCCESS;

	bus_no = msg->cmd.destroy_device.bus_no;
	dev_no = msg->cmd.destroy_device.bus_no;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (bus->bus_no == bus_no) {
			/* make sure the device number is valid */
			if (dev_no >= bus->device_count) {
				retval = CONTROLVM_RESP_ERROR_DEVICE_INVALID;
			} else {
				/* make sure this device exists */
				dev = bus->device[dev_no];
				if (!dev) {
					retval =
					     CONTROLVM_RESP_ERROR_ALREADY_DONE;
				}
			}
			break;
		}
	}

	if (!bus)
		retval = CONTROLVM_RESP_ERROR_BUS_INVALID;
	read_unlock(&bus_list_lock);
	if (retval == CONTROLVM_RESP_SUCCESS) {
		/* the msg is bound for virtpci; send
		 * guest_msgs struct to callback
		 */
		if (uuid_le_cmp(dev->channel_uuid,
				spar_vhba_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_DEL_VHBA;
			cmd.del_vhba.chanptr = dev->chanptr;
		} else if (uuid_le_cmp(dev->channel_uuid,
				       spar_vnic_channel_protocol_uuid) == 0) {
			cmd.msgtype = GUEST_DEL_VNIC;
			cmd.del_vnic.chanptr = dev->chanptr;
		} else {
			return
			    CONTROLVM_RESP_ERROR_CHANNEL_TYPE_UNKNOWN;
		}
		if (!virt_control_chan_func) {
			return
			    CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_FAILURE;
		}
		if (!virt_control_chan_func(&cmd)) {
			return
			    CONTROLVM_RESP_ERROR_VIRTPCI_DRIVER_CALLBACK_ERROR;
		}
/* you must disable channel interrupts BEFORE you unmap the channel,
 * because if you unmap first, there may still be some activity going
 * on which accesses the channel and you will get a "unable to handle
 * kernel paging request"
 */
		if (dev->polling)
			uislib_disable_channel_interrupts(bus_no, dev_no);
		/* unmap the channel memory for the device. */
		if (!msg->hdr.flags.test_message)
			uislib_iounmap(dev->chanptr);
		kfree(dev);
		bus->device[dev_no] = NULL;
	}
	return retval;
}

static int
init_chipset(struct controlvm_message *msg, char *buf)
{
	POSTCODE_LINUX_2(CHIPSET_INIT_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	max_bus_count = msg->cmd.init_chipset.bus_count;
	platform_no = msg->cmd.init_chipset.platform_number;
	phys_data_chan = 0;

	/* We need to make sure we have our functions registered
	* before processing messages.  If we are a test vehicle the
	* test_message for init_chipset will be set.  We can ignore the
	* waits for the callbacks, since this will be manually entered
	* from a user.  If no test_message is set, we will wait for the
	* functions.
	*/
	if (!msg->hdr.flags.test_message)
		WAIT_ON_CALLBACK(virt_control_chan_func);

	chipset_inited = 1;
	POSTCODE_LINUX_2(CHIPSET_INIT_EXIT_PC, POSTCODE_SEVERITY_INFO);

	return CONTROLVM_RESP_SUCCESS;
}

static int delete_bus_glue(u32 bus_no)
{
	struct controlvm_message msg;

	init_msg_header(&msg, CONTROLVM_BUS_DESTROY, 0, 0);
	msg.cmd.destroy_bus.bus_no = bus_no;
	if (destroy_bus(&msg, NULL) != CONTROLVM_RESP_SUCCESS)
		return 0;
	return 1;
}

static int delete_device_glue(u32 bus_no, u32 dev_no)
{
	struct controlvm_message msg;

	init_msg_header(&msg, CONTROLVM_DEVICE_DESTROY, 0, 0);
	msg.cmd.destroy_device.bus_no = bus_no;
	msg.cmd.destroy_device.dev_no = dev_no;
	if (destroy_device(&msg, NULL) != CONTROLVM_RESP_SUCCESS)
		return 0;
	return 1;
}

int
uislib_client_inject_add_bus(u32 bus_no, uuid_le inst_uuid,
			     u64 channel_addr, ulong n_channel_bytes)
{
	struct controlvm_message msg;

	/* step 0: init the chipset */
	POSTCODE_LINUX_3(CHIPSET_INIT_ENTRY_PC, bus_no, POSTCODE_SEVERITY_INFO);

	if (!chipset_inited) {
		/* step: initialize the chipset */
		init_msg_header(&msg, CONTROLVM_CHIPSET_INIT, 0, 0);
		/* this change is needed so that console will come up
		* OK even when the bus 0 create comes in late.  If the
		* bus 0 create is the first create, then the add_vnic
		* will work fine, but if the bus 0 create arrives
		* after number 4, then the add_vnic will fail, and the
		* ultraboot will fail.
		*/
		msg.cmd.init_chipset.bus_count = 23;
		msg.cmd.init_chipset.switch_count = 0;
		if (init_chipset(&msg, NULL) != CONTROLVM_RESP_SUCCESS)
			return 0;
		POSTCODE_LINUX_3(CHIPSET_INIT_EXIT_PC, bus_no,
				 POSTCODE_SEVERITY_INFO);
	}

	/* step 1: create a bus */
	POSTCODE_LINUX_3(BUS_CREATE_ENTRY_PC, bus_no,
			 POSTCODE_SEVERITY_WARNING);
	init_msg_header(&msg, CONTROLVM_BUS_CREATE, 0, 0);
	msg.cmd.create_bus.bus_no = bus_no;
	msg.cmd.create_bus.dev_count = 23;	/* devNo+1; */
	msg.cmd.create_bus.channel_addr = channel_addr;
	msg.cmd.create_bus.channel_bytes = n_channel_bytes;
	if (create_bus(&msg, NULL) != CONTROLVM_RESP_SUCCESS) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return 0;
	}
	POSTCODE_LINUX_3(BUS_CREATE_EXIT_PC, bus_no, POSTCODE_SEVERITY_INFO);

	return 1;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_add_bus);

int
uislib_client_inject_del_bus(u32 bus_no)
{
	return delete_bus_glue(bus_no);
}
EXPORT_SYMBOL_GPL(uislib_client_inject_del_bus);

int
uislib_client_inject_pause_vhba(u32 bus_no, u32 dev_no)
{
	struct controlvm_message msg;
	int rc;

	init_msg_header(&msg, CONTROLVM_DEVICE_CHANGESTATE, 0, 0);
	msg.cmd.device_change_state.bus_no = bus_no;
	msg.cmd.device_change_state.dev_no = dev_no;
	msg.cmd.device_change_state.state = segment_state_standby;
	rc = pause_device(&msg);
	if (rc != CONTROLVM_RESP_SUCCESS)
		return rc;
	return 0;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_pause_vhba);

int
uislib_client_inject_resume_vhba(u32 bus_no, u32 dev_no)
{
	struct controlvm_message msg;
	int rc;

	init_msg_header(&msg, CONTROLVM_DEVICE_CHANGESTATE, 0, 0);
	msg.cmd.device_change_state.bus_no = bus_no;
	msg.cmd.device_change_state.dev_no = dev_no;
	msg.cmd.device_change_state.state = segment_state_running;
	rc = resume_device(&msg);
	if (rc != CONTROLVM_RESP_SUCCESS)
		return rc;
	return 0;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_resume_vhba);

int
uislib_client_inject_add_vhba(u32 bus_no, u32 dev_no,
			      u64 phys_chan_addr, u32 chan_bytes,
			      int is_test_addr, uuid_le inst_uuid,
			      struct irq_info *intr)
{
	struct controlvm_message msg;

	/* chipset init'ed with bus bus has been previously created -
	* Verify it still exists step 2: create the VHBA device on the
	* bus
	*/
	POSTCODE_LINUX_4(VHBA_CREATE_ENTRY_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);

	init_msg_header(&msg, CONTROLVM_DEVICE_CREATE, 0, 0);
	if (is_test_addr)
		/* signify that the physical channel address does NOT
		 * need to be ioremap()ed
		 */
		msg.hdr.flags.test_message = 1;
	msg.cmd.create_device.bus_no = bus_no;
	msg.cmd.create_device.dev_no = dev_no;
	msg.cmd.create_device.dev_inst_uuid = inst_uuid;
	if (intr)
		msg.cmd.create_device.intr = *intr;
	else
		memset(&msg.cmd.create_device.intr, 0,
		       sizeof(struct irq_info));
	msg.cmd.create_device.channel_addr = phys_chan_addr;
	if (chan_bytes < MIN_IO_CHANNEL_SIZE) {
		POSTCODE_LINUX_4(VHBA_CREATE_FAILURE_PC, chan_bytes,
				 MIN_IO_CHANNEL_SIZE, POSTCODE_SEVERITY_ERR);
		return 0;
	}
	msg.cmd.create_device.channel_bytes = chan_bytes;
	msg.cmd.create_device.data_type_uuid = spar_vhba_channel_protocol_uuid;
	if (create_device(&msg, NULL) != CONTROLVM_RESP_SUCCESS) {
		POSTCODE_LINUX_4(VHBA_CREATE_FAILURE_PC, dev_no, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return 0;
	}
	POSTCODE_LINUX_4(VHBA_CREATE_SUCCESS_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);
	return 1;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_add_vhba);

int
uislib_client_inject_del_vhba(u32 bus_no, u32 dev_no)
{
	return delete_device_glue(bus_no, dev_no);
}
EXPORT_SYMBOL_GPL(uislib_client_inject_del_vhba);

int
uislib_client_inject_add_vnic(u32 bus_no, u32 dev_no,
			      u64 phys_chan_addr, u32 chan_bytes,
			      int is_test_addr, uuid_le inst_uuid,
			      struct irq_info *intr)
{
	struct controlvm_message msg;

	/* chipset init'ed with bus bus has been previously created -
	* Verify it still exists step 2: create the VNIC device on the
	* bus
	*/
	POSTCODE_LINUX_4(VNIC_CREATE_ENTRY_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);

	init_msg_header(&msg, CONTROLVM_DEVICE_CREATE, 0, 0);
	if (is_test_addr)
		/* signify that the physical channel address does NOT
		 * need to be ioremap()ed
		 */
		msg.hdr.flags.test_message = 1;
	msg.cmd.create_device.bus_no = bus_no;
	msg.cmd.create_device.dev_no = dev_no;
	msg.cmd.create_device.dev_inst_uuid = inst_uuid;
	if (intr)
		msg.cmd.create_device.intr = *intr;
	else
		memset(&msg.cmd.create_device.intr, 0,
		       sizeof(struct irq_info));
	msg.cmd.create_device.channel_addr = phys_chan_addr;
	if (chan_bytes < MIN_IO_CHANNEL_SIZE) {
		POSTCODE_LINUX_4(VNIC_CREATE_FAILURE_PC, chan_bytes,
				 MIN_IO_CHANNEL_SIZE, POSTCODE_SEVERITY_ERR);
		return 0;
	}
	msg.cmd.create_device.channel_bytes = chan_bytes;
	msg.cmd.create_device.data_type_uuid = spar_vnic_channel_protocol_uuid;
	if (create_device(&msg, NULL) != CONTROLVM_RESP_SUCCESS) {
		POSTCODE_LINUX_4(VNIC_CREATE_FAILURE_PC, dev_no, bus_no,
				 POSTCODE_SEVERITY_ERR);
		return 0;
	}

	POSTCODE_LINUX_4(VNIC_CREATE_SUCCESS_PC, dev_no, bus_no,
			 POSTCODE_SEVERITY_INFO);
	return 1;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_add_vnic);

int
uislib_client_inject_pause_vnic(u32 bus_no, u32 dev_no)
{
	struct controlvm_message msg;
	int rc;

	init_msg_header(&msg, CONTROLVM_DEVICE_CHANGESTATE, 0, 0);
	msg.cmd.device_change_state.bus_no = bus_no;
	msg.cmd.device_change_state.dev_no = dev_no;
	msg.cmd.device_change_state.state = segment_state_standby;
	rc = pause_device(&msg);
	if (rc != CONTROLVM_RESP_SUCCESS)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_pause_vnic);

int
uislib_client_inject_resume_vnic(u32 bus_no, u32 dev_no)
{
	struct controlvm_message msg;
	int rc;

	init_msg_header(&msg, CONTROLVM_DEVICE_CHANGESTATE, 0, 0);
	msg.cmd.device_change_state.bus_no = bus_no;
	msg.cmd.device_change_state.dev_no = dev_no;
	msg.cmd.device_change_state.state = segment_state_running;
	rc = resume_device(&msg);
	if (rc != CONTROLVM_RESP_SUCCESS)
		return -1;
	return 0;
}
EXPORT_SYMBOL_GPL(uislib_client_inject_resume_vnic);

int
uislib_client_inject_del_vnic(u32 bus_no, u32 dev_no)
{
	return delete_device_glue(bus_no, dev_no);
}
EXPORT_SYMBOL_GPL(uislib_client_inject_del_vnic);

void *
uislib_cache_alloc(struct kmem_cache *cur_pool, char *fn, int ln)
{
	/* __GFP_NORETRY means "ok to fail", meaning kmalloc() can
	* return NULL.  If you do NOT specify __GFP_NORETRY, Linux
	* will go to extreme measures to get memory for you (like,
	* invoke oom killer), which will probably cripple the system.
	*/
	void *p = kmem_cache_alloc(cur_pool, GFP_ATOMIC | __GFP_NORETRY);

	if (p == NULL)
		return NULL;
	return p;
}
EXPORT_SYMBOL_GPL(uislib_cache_alloc);

void
uislib_cache_free(struct kmem_cache *cur_pool, void *p, char *fn, int ln)
{
	if (p == NULL)
		return;
	kmem_cache_free(cur_pool, p);
}
EXPORT_SYMBOL_GPL(uislib_cache_free);

/*****************************************************/
/* proc filesystem callback functions                */
/*****************************************************/

#define PLINE(...) uisutil_add_proc_line_ex(&tot, buff, \
					       buff_len, __VA_ARGS__)

static int
info_debugfs_read_helper(char **buff, int *buff_len)
{
	int i, tot = 0;
	struct bus_info *bus;

	if (PLINE("\nBuses:\n") < 0)
		goto err_done;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (PLINE("    bus=0x%p, busNo=%d, deviceCount=%d\n",
			  bus, bus->bus_no, bus->device_count) < 0)
			goto err_done_unlock;

		if (PLINE("        Devices:\n") < 0)
			goto err_done_unlock;

		for (i = 0; i < bus->device_count; i++) {
			if (bus->device[i]) {
				if (PLINE("            busNo %d, device[%i]: 0x%p, chanptr=0x%p, swtch=0x%p\n",
					  bus->bus_no, i, bus->device[i],
					  bus->device[i]->chanptr,
					  bus->device[i]->swtch) < 0)
					goto err_done_unlock;

				if (PLINE("            first_busy_cnt=%llu, moved_to_tail_cnt=%llu, last_on_list_cnt=%llu\n",
					  bus->device[i]->first_busy_cnt,
					  bus->device[i]->moved_to_tail_cnt,
					  bus->device[i]->last_on_list_cnt) < 0)
					goto err_done_unlock;
			}
		}
	}
	read_unlock(&bus_list_lock);

	if (PLINE("UisUtils_Registered_Services: %d\n",
		  atomic_read(&uisutils_registered_services)) < 0)
		goto err_done;
	if (PLINE("cycles_before_wait %llu wait_cycles:%llu\n",
		  cycles_before_wait, wait_cycles) < 0)
			goto err_done;
	if (PLINE("tot_wakeup_cnt %llu:tot_wait_cnt %llu:tot_schedule_cnt %llu\n",
		  tot_wakeup_cnt, tot_wait_cnt, tot_schedule_cnt) < 0)
			goto err_done;
	if (PLINE("en_smart_wakeup %d\n", en_smart_wakeup) < 0)
			goto err_done;
	if (PLINE("tot_moved_to_tail_cnt %llu\n", tot_moved_to_tail_cnt) < 0)
			goto err_done;

	return tot;

err_done_unlock:
	read_unlock(&bus_list_lock);
err_done:
	return -1;
}

static ssize_t info_debugfs_read(struct file *file, char __user *buf,
				 size_t len, loff_t *offset)
{
	char *temp;
	int total_bytes = 0;
	int remaining_bytes = PROC_READ_BUFFER_SIZE;

/* *start = buf; */
	if (debug_buf == NULL) {
		debug_buf = vmalloc(PROC_READ_BUFFER_SIZE);

		if (debug_buf == NULL)
			return -ENOMEM;
	}

	temp = debug_buf;

	if ((*offset == 0) || (!debug_buf_valid)) {
		/* if the read fails, then -1 will be returned */
		total_bytes = info_debugfs_read_helper(&temp, &remaining_bytes);
		debug_buf_valid = 1;
	} else {
		total_bytes = strlen(debug_buf);
	}

	return simple_read_from_buffer(buf, len, offset,
				       debug_buf, total_bytes);
}

static struct device_info *find_dev(u32 bus_no, u32 dev_no)
{
	struct bus_info *bus;
	struct device_info *dev = NULL;

	read_lock(&bus_list_lock);
	for (bus = bus_list; bus; bus = bus->next) {
		if (bus->bus_no == bus_no) {
			/* make sure the device number is valid */
			if (dev_no >= bus->device_count)
				break;
			dev = bus->device[dev_no];
			break;
		}
	}
	read_unlock(&bus_list_lock);
	return dev;
}

/*  This thread calls the "interrupt" function for each device that has
 *  enabled such using uislib_enable_channel_interrupts().  The "interrupt"
 *  function typically reads and processes the devices's channel input
 *  queue.  This thread repeatedly does this, until the thread is told to stop
 *  (via uisthread_stop()).  Sleeping rules:
 *  - If we have called the "interrupt" function for all devices, and all of
 *    them have reported "nothing processed" (returned 0), then we will go to
 *    sleep for a maximum of POLLJIFFIES_NORMAL jiffies.
 *  - If anyone calls uislib_force_channel_interrupt(), the above jiffy
 *    sleep will be interrupted, and we will resume calling the "interrupt"
 *    function for all devices.
 *  - The list of devices is dynamically re-ordered in order to
 *    attempt to preserve fairness.  Whenever we spin thru the list of
 *    devices and call the dev->interrupt() function, if we find
 *    devices which report that there is still more work to do, the
 *    the first such device we find is moved to the end of the device
 *    list.  This ensures that extremely busy devices don't starve out
 *    less-busy ones.
 *
 */
static int process_incoming(void *v)
{
	unsigned long long cur_cycles, old_cycles, idle_cycles, delta_cycles;
	struct list_head *new_tail = NULL;
	int i;

	UIS_DAEMONIZE("dev_incoming");
	for (i = 0; i < 16; i++) {
		old_cycles = get_cycles();
		wait_event_timeout(poll_dev_wake_q,
				   0, POLLJIFFIES_NORMAL);
		cur_cycles = get_cycles();
		if (wait_cycles == 0) {
			wait_cycles = (cur_cycles - old_cycles);
		} else {
			if (wait_cycles < (cur_cycles - old_cycles))
				wait_cycles = (cur_cycles - old_cycles);
		}
	}
	cycles_before_wait = wait_cycles;
	idle_cycles = 0;
	poll_dev_start = 0;
	while (1) {
		struct list_head *lelt, *tmp;
		struct device_info *dev = NULL;

		/* poll each channel for input */
		down(&poll_dev_lock);
		new_tail = NULL;
		list_for_each_safe(lelt, tmp, &poll_dev_chan) {
			int rc = 0;

			dev = list_entry(lelt, struct device_info,
					 list_polling_device_channels);
			down(&dev->interrupt_callback_lock);
			if (dev->interrupt)
				rc = dev->interrupt(dev->interrupt_context);
			else
				continue;
			up(&dev->interrupt_callback_lock);
			if (rc) {
				/* dev->interrupt returned, but there
				* is still more work to do.
				* Reschedule work to occur as soon as
				* possible. */
				idle_cycles = 0;
				if (new_tail == NULL) {
					dev->first_busy_cnt++;
					if (!
					    (list_is_last
					     (lelt,
					      &poll_dev_chan))) {
						new_tail = lelt;
						dev->moved_to_tail_cnt++;
					} else {
						dev->last_on_list_cnt++;
					}
				}
			}
			if (kthread_should_stop())
				break;
		}
		if (new_tail != NULL) {
			tot_moved_to_tail_cnt++;
			list_move_tail(new_tail, &poll_dev_chan);
		}
		up(&poll_dev_lock);
		cur_cycles = get_cycles();
		delta_cycles = cur_cycles - old_cycles;
		old_cycles = cur_cycles;

		/* At this point, we have scanned thru all of the
		* channels, and at least one of the following is true:
		* - there is no input waiting on any of the channels
		* - we have received a signal to stop this thread
		*/
		if (kthread_should_stop())
			break;
		if (en_smart_wakeup == 0xFF)
			break;
		/* wait for POLLJIFFIES_NORMAL jiffies, or until
		* someone wakes up poll_dev_wake_q,
		* whichever comes first only do a wait when we have
		* been idle for cycles_before_wait cycles.
		*/
		if (idle_cycles > cycles_before_wait) {
			poll_dev_start = 0;
			tot_wait_cnt++;
			wait_event_timeout(poll_dev_wake_q,
					   poll_dev_start,
					   POLLJIFFIES_NORMAL);
			poll_dev_start = 1;
		} else {
			tot_schedule_cnt++;
			schedule();
			idle_cycles = idle_cycles + delta_cycles;
		}
	}
	complete_and_exit(&incoming_ti.has_stopped, 0);
}

static BOOL
initialize_incoming_thread(void)
{
	if (incoming_started)
		return TRUE;
	if (!uisthread_start(&incoming_ti,
			     &process_incoming, NULL, "dev_incoming")) {
		return FALSE;
	}
	incoming_started = TRUE;
	return TRUE;
}

/*  Add a new device/channel to the list being processed by
 *  process_incoming().
 *  <interrupt> - indicates the function to call periodically.
 *  <interrupt_context> - indicates the data to pass to the <interrupt>
 *                        function.
 */
void
uislib_enable_channel_interrupts(u32 bus_no, u32 dev_no,
				 int (*interrupt)(void *),
				 void *interrupt_context)
{
	struct device_info *dev;

	dev = find_dev(bus_no, dev_no);
	if (!dev)
		return;

	down(&poll_dev_lock);
	initialize_incoming_thread();
	dev->interrupt = interrupt;
	dev->interrupt_context = interrupt_context;
	dev->polling = TRUE;
	list_add_tail(&dev->list_polling_device_channels,
		      &poll_dev_chan);
	up(&poll_dev_lock);
}
EXPORT_SYMBOL_GPL(uislib_enable_channel_interrupts);

/*  Remove a device/channel from the list being processed by
 *  process_incoming().
 */
void
uislib_disable_channel_interrupts(u32 bus_no, u32 dev_no)
{
	struct device_info *dev;

	dev = find_dev(bus_no, dev_no);
	if (!dev)
		return;
	down(&poll_dev_lock);
	list_del(&dev->list_polling_device_channels);
	dev->polling = FALSE;
	dev->interrupt = NULL;
	up(&poll_dev_lock);
}
EXPORT_SYMBOL_GPL(uislib_disable_channel_interrupts);

static void
do_wakeup_polling_device_channels(struct work_struct *dummy)
{
	if (!poll_dev_start) {
		poll_dev_start = 1;
		wake_up(&poll_dev_wake_q);
	}
}

static DECLARE_WORK(work_wakeup_polling_device_channels,
		    do_wakeup_polling_device_channels);

/*  Call this function when you want to send a hint to process_incoming() that
 *  your device might have more requests.
 */
void
uislib_force_channel_interrupt(u32 bus_no, u32 dev_no)
{
	if (en_smart_wakeup == 0)
		return;
	if (poll_dev_start)
		return;
	/* The point of using schedule_work() instead of just doing
	 * the work inline is to force a slight delay before waking up
	 * the process_incoming() thread.
	 */
	tot_wakeup_cnt++;
	schedule_work(&work_wakeup_polling_device_channels);
}
EXPORT_SYMBOL_GPL(uislib_force_channel_interrupt);

/*****************************************************/
/* Module Init & Exit functions                      */
/*****************************************************/

static int __init
uislib_mod_init(void)
{
	if (!unisys_spar_platform)
		return -ENODEV;

	/* initialize global pointers to NULL */
	bus_list = NULL;
	bus_list_count = 0;
	max_bus_count = 0;
	rwlock_init(&bus_list_lock);
	virt_control_chan_func = NULL;

	/* Issue VMCALL_GET_CONTROLVM_ADDR to get CtrlChanPhysAddr and
	 * then map this physical address to a virtual address. */
	POSTCODE_LINUX_2(DRIVER_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	dir_debugfs = debugfs_create_dir(DIR_DEBUGFS_ENTRY, NULL);
	if (dir_debugfs) {
		info_debugfs_entry = debugfs_create_file(
			INFO_DEBUGFS_ENTRY_FN, 0444, dir_debugfs, NULL,
			&debugfs_info_fops);

		platformnumber_debugfs_read = debugfs_create_u32(
			PLATFORMNUMBER_DEBUGFS_ENTRY_FN, 0444, dir_debugfs,
			&platform_no);

		cycles_before_wait_debugfs_read = debugfs_create_u64(
			CYCLES_BEFORE_WAIT_DEBUGFS_ENTRY_FN, 0666, dir_debugfs,
			&cycles_before_wait);

		smart_wakeup_debugfs_entry = debugfs_create_bool(
			SMART_WAKEUP_DEBUGFS_ENTRY_FN, 0666, dir_debugfs,
			&en_smart_wakeup);
	}

	POSTCODE_LINUX_3(DRIVER_EXIT_PC, 0, POSTCODE_SEVERITY_INFO);
	return 0;
}

static void __exit
uislib_mod_exit(void)
{
	if (debug_buf) {
		vfree(debug_buf);
		debug_buf = NULL;
	}

	debugfs_remove(info_debugfs_entry);
	debugfs_remove(smart_wakeup_debugfs_entry);
	debugfs_remove(cycles_before_wait_debugfs_read);
	debugfs_remove(platformnumber_debugfs_read);
	debugfs_remove(dir_debugfs);
}

module_init(uislib_mod_init);
module_exit(uislib_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Usha Srinivasan");
MODULE_ALIAS("uislib");
  /* this is extracted during depmod and kept in modules.dep */
