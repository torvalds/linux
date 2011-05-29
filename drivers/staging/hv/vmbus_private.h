/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _VMBUS_PRIVATE_H_
#define _VMBUS_PRIVATE_H_

#include "hv.h"
#include "vmbus_api.h"
#include "channel.h"
#include "channel_mgmt.h"
#include "ring_buffer.h"
#include <linux/list.h>
#include <asm/sync_bitops.h>


/*
 * Maximum channels is determined by the size of the interrupt page
 * which is PAGE_SIZE. 1/2 of PAGE_SIZE is for send endpoint interrupt
 * and the other is receive endpoint interrupt
 */
#define MAX_NUM_CHANNELS	((PAGE_SIZE >> 1) << 3)	/* 16348 channels */

/* The value here must be in multiple of 32 */
/* TODO: Need to make this configurable */
#define MAX_NUM_CHANNELS_SUPPORTED	256


enum vmbus_connect_state {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	DISCONNECTING
};

#define MAX_SIZE_CHANNEL_MESSAGE	HV_MESSAGE_PAYLOAD_BYTE_COUNT

struct vmbus_connection {
	enum vmbus_connect_state conn_state;

	atomic_t next_gpadl_handle;

	/*
	 * Represents channel interrupts. Each bit position represents a
	 * channel.  When a channel sends an interrupt via VMBUS, it finds its
	 * bit in the sendInterruptPage, set it and calls Hv to generate a port
	 * event. The other end receives the port event and parse the
	 * recvInterruptPage to see which bit is set
	 */
	void *int_page;
	void *send_int_page;
	void *recv_int_page;

	/*
	 * 2 pages - 1st page for parent->child notification and 2nd
	 * is child->parent notification
	 */
	void *monitor_pages;
	struct list_head chn_msg_list;
	spinlock_t channelmsg_lock;

	/* List of channels */
	struct list_head chn_list;
	spinlock_t channel_lock;

	struct workqueue_struct *work_queue;
};


struct vmbus_msginfo {
	/* Bookkeeping stuff */
	struct list_head msglist_entry;

	/* Synchronize the request/response if needed */
	int wait_condition;
	wait_queue_head_t  wait_event;

	/* The message itself */
	unsigned char msg[0];
};


extern struct vmbus_connection vmbus_connection;

/* General vmbus interface */

struct hv_device *vmbus_child_device_create(struct hv_guid *type,
					 struct hv_guid *instance,
					 struct vmbus_channel *channel);

int vmbus_child_device_register(struct hv_device *child_device_obj);
void vmbus_child_device_unregister(struct hv_device *device_obj);

/* static void */
/* VmbusChildDeviceDestroy( */
/* struct hv_device *); */

struct vmbus_channel *relid2channel(u32 relid);


/* Connection interface */

int vmbus_connect(void);

int vmbus_disconnect(void);

int vmbus_post_msg(void *buffer, size_t buflen);

int vmbus_set_event(u32 child_relid);

void vmbus_on_event(unsigned long data);


#endif /* _VMBUS_PRIVATE_H_ */
