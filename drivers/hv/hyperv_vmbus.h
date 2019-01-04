/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
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
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */

#ifndef _HYPERV_VMBUS_H
#define _HYPERV_VMBUS_H

#include <linux/list.h>
#include <asm/sync_bitops.h>
#include <asm/hyperv-tlfs.h>
#include <linux/atomic.h>
#include <linux/hyperv.h>
#include <linux/interrupt.h>

#include "hv_trace.h"

/*
 * Timeout for services such as KVP and fcopy.
 */
#define HV_UTIL_TIMEOUT 30

/*
 * Timeout for guest-host handshake for services.
 */
#define HV_UTIL_NEGO_TIMEOUT 55


/* Definitions for the monitored notification facility */
union hv_monitor_trigger_group {
	u64 as_uint64;
	struct {
		u32 pending;
		u32 armed;
	};
};

struct hv_monitor_parameter {
	union hv_connection_id connectionid;
	u16 flagnumber;
	u16 rsvdz;
};

union hv_monitor_trigger_state {
	u32 asu32;

	struct {
		u32 group_enable:4;
		u32 rsvdz:28;
	};
};

/* struct hv_monitor_page Layout */
/* ------------------------------------------------------ */
/* | 0   | TriggerState (4 bytes) | Rsvd1 (4 bytes)     | */
/* | 8   | TriggerGroup[0]                              | */
/* | 10  | TriggerGroup[1]                              | */
/* | 18  | TriggerGroup[2]                              | */
/* | 20  | TriggerGroup[3]                              | */
/* | 28  | Rsvd2[0]                                     | */
/* | 30  | Rsvd2[1]                                     | */
/* | 38  | Rsvd2[2]                                     | */
/* | 40  | NextCheckTime[0][0]    | NextCheckTime[0][1] | */
/* | ...                                                | */
/* | 240 | Latency[0][0..3]                             | */
/* | 340 | Rsvz3[0]                                     | */
/* | 440 | Parameter[0][0]                              | */
/* | 448 | Parameter[0][1]                              | */
/* | ...                                                | */
/* | 840 | Rsvd4[0]                                     | */
/* ------------------------------------------------------ */
struct hv_monitor_page {
	union hv_monitor_trigger_state trigger_state;
	u32 rsvdz1;

	union hv_monitor_trigger_group trigger_group[4];
	u64 rsvdz2[3];

	s32 next_checktime[4][32];

	u16 latency[4][32];
	u64 rsvdz3[32];

	struct hv_monitor_parameter parameter[4][32];

	u8 rsvdz4[1984];
};

#define HV_HYPERCALL_PARAM_ALIGN	sizeof(u64)

/* Definition of the hv_post_message hypercall input structure. */
struct hv_input_post_message {
	union hv_connection_id connectionid;
	u32 reserved;
	u32 message_type;
	u32 payload_size;
	u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
};


enum {
	VMBUS_MESSAGE_CONNECTION_ID	= 1,
	VMBUS_MESSAGE_CONNECTION_ID_4	= 4,
	VMBUS_MESSAGE_PORT_ID		= 1,
	VMBUS_EVENT_CONNECTION_ID	= 2,
	VMBUS_EVENT_PORT_ID		= 2,
	VMBUS_MONITOR_CONNECTION_ID	= 3,
	VMBUS_MONITOR_PORT_ID		= 3,
	VMBUS_MESSAGE_SINT		= 2,
};

/*
 * Per cpu state for channel handling
 */
struct hv_per_cpu_context {
	void *synic_message_page;
	void *synic_event_page;
	/*
	 * buffer to post messages to the host.
	 */
	void *post_msg_page;

	/*
	 * Starting with win8, we can take channel interrupts on any CPU;
	 * we will manage the tasklet that handles events messages on a per CPU
	 * basis.
	 */
	struct tasklet_struct msg_dpc;

	/*
	 * To optimize the mapping of relid to channel, maintain
	 * per-cpu list of the channels based on their CPU affinity.
	 */
	struct list_head chan_list;
	struct clock_event_device *clk_evt;
};

struct hv_context {
	/* We only support running on top of Hyper-V
	 * So at this point this really can only contain the Hyper-V ID
	 */
	u64 guestid;

	void *tsc_page;

	struct hv_per_cpu_context __percpu *cpu_context;

	/*
	 * To manage allocations in a NUMA node.
	 * Array indexed by numa node ID.
	 */
	struct cpumask *hv_numa_map;
};

extern struct hv_context hv_context;

/* Hv Interface */

extern int hv_init(void);

extern int hv_post_message(union hv_connection_id connection_id,
			 enum hv_message_type message_type,
			 void *payload, size_t payload_size);

extern int hv_synic_alloc(void);

extern void hv_synic_free(void);

extern int hv_synic_init(unsigned int cpu);

extern int hv_synic_cleanup(unsigned int cpu);

extern void hv_synic_clockevents_cleanup(void);

/* Interface */


int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info,
		       struct page *pages, u32 pagecnt);

void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int hv_ringbuffer_write(struct vmbus_channel *channel,
			const struct kvec *kv_list, u32 kv_count);

int hv_ringbuffer_read(struct vmbus_channel *channel,
		       void *buffer, u32 buflen, u32 *buffer_actual_len,
		       u64 *requestid, bool raw);

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
	/*
	 * CPU on which the initial host contact was made.
	 */
	int connect_cpu;

	u32 msg_conn_id;

	atomic_t offer_in_progress;

	enum vmbus_connect_state conn_state;

	atomic_t next_gpadl_handle;

	struct completion  unload_event;
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
	struct hv_monitor_page *monitor_pages[2];
	struct list_head chn_msg_list;
	spinlock_t channelmsg_lock;

	/* List of channels */
	struct list_head chn_list;
	struct mutex channel_mutex;

	/*
	 * An offer message is handled first on the work_queue, and then
	 * is further handled on handle_primary_chan_wq or
	 * handle_sub_chan_wq.
	 */
	struct workqueue_struct *work_queue;
	struct workqueue_struct *handle_primary_chan_wq;
	struct workqueue_struct *handle_sub_chan_wq;
};


struct vmbus_msginfo {
	/* Bookkeeping stuff */
	struct list_head msglist_entry;

	/* The message itself */
	unsigned char msg[0];
};


extern struct vmbus_connection vmbus_connection;

static inline void vmbus_send_interrupt(u32 relid)
{
	sync_set_bit(relid, vmbus_connection.send_int_page);
}

enum vmbus_message_handler_type {
	/* The related handler can sleep. */
	VMHT_BLOCKING = 0,

	/* The related handler must NOT sleep. */
	VMHT_NON_BLOCKING = 1,
};

struct vmbus_channel_message_table_entry {
	enum vmbus_channel_message_type message_type;
	enum vmbus_message_handler_type handler_type;
	void (*message_handler)(struct vmbus_channel_message_header *msg);
};

extern const struct vmbus_channel_message_table_entry
	channel_message_table[CHANNELMSG_COUNT];


/* General vmbus interface */

struct hv_device *vmbus_device_create(const uuid_le *type,
				      const uuid_le *instance,
				      struct vmbus_channel *channel);

int vmbus_device_register(struct hv_device *child_device_obj);
void vmbus_device_unregister(struct hv_device *device_obj);
int vmbus_add_channel_kobj(struct hv_device *device_obj,
			   struct vmbus_channel *channel);

struct vmbus_channel *relid2channel(u32 relid);

void vmbus_free_channels(void);

/* Connection interface */

int vmbus_connect(void);
void vmbus_disconnect(void);

int vmbus_post_msg(void *buffer, size_t buflen, bool can_sleep);

void vmbus_on_event(unsigned long data);
void vmbus_on_msg_dpc(unsigned long data);

int hv_kvp_init(struct hv_util_service *srv);
void hv_kvp_deinit(void);
void hv_kvp_onchannelcallback(void *context);

int hv_vss_init(struct hv_util_service *srv);
void hv_vss_deinit(void);
void hv_vss_onchannelcallback(void *context);

int hv_fcopy_init(struct hv_util_service *srv);
void hv_fcopy_deinit(void);
void hv_fcopy_onchannelcallback(void *context);
void vmbus_initiate_unload(bool crash);

static inline void hv_poll_channel(struct vmbus_channel *channel,
				   void (*cb)(void *))
{
	if (!channel)
		return;

	if (in_interrupt() && (channel->target_cpu == smp_processor_id())) {
		cb(channel);
		return;
	}
	smp_call_function_single(channel->target_cpu, cb, channel, true);
}

enum hvutil_device_state {
	HVUTIL_DEVICE_INIT = 0,  /* driver is loaded, waiting for userspace */
	HVUTIL_READY,            /* userspace is registered */
	HVUTIL_HOSTMSG_RECEIVED, /* message from the host was received */
	HVUTIL_USERSPACE_REQ,    /* request to userspace was sent */
	HVUTIL_USERSPACE_RECV,   /* reply from userspace was received */
	HVUTIL_DEVICE_DYING,     /* driver unload is in progress */
};

#endif /* _HYPERV_VMBUS_H */
