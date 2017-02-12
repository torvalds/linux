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
#include <linux/atomic.h>
#include <linux/hyperv.h>
#include <linux/interrupt.h>

/*
 * Timeout for services such as KVP and fcopy.
 */
#define HV_UTIL_TIMEOUT 30

/*
 * Timeout for guest-host handshake for services.
 */
#define HV_UTIL_NEGO_TIMEOUT 55

/* Define synthetic interrupt controller flag constants. */
#define HV_EVENT_FLAGS_COUNT		(256 * 8)
#define HV_EVENT_FLAGS_LONG_COUNT	(256 / sizeof(unsigned long))

/*
 * Timer configuration register.
 */
union hv_timer_config {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 periodic:1;
		u64 lazy:1;
		u64 auto_enable:1;
		u64 reserved_z0:12;
		u64 sintx:4;
		u64 reserved_z1:44;
	};
};


/* Define the synthetic interrupt controller event flags format. */
union hv_synic_event_flags {
	unsigned long flags[HV_EVENT_FLAGS_LONG_COUNT];
};

/* Define SynIC control register. */
union hv_synic_scontrol {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:63;
	};
};

/* Define synthetic interrupt source. */
union hv_synic_sint {
	u64 as_uint64;
	struct {
		u64 vector:8;
		u64 reserved1:8;
		u64 masked:1;
		u64 auto_eoi:1;
		u64 reserved2:46;
	};
};

/* Define the format of the SIMP register */
union hv_synic_simp {
	u64 as_uint64;
	struct {
		u64 simp_enabled:1;
		u64 preserved:11;
		u64 base_simp_gpa:52;
	};
};

/* Define the format of the SIEFP register */
union hv_synic_siefp {
	u64 as_uint64;
	struct {
		u64 siefp_enabled:1;
		u64 preserved:11;
		u64 base_siefp_gpa:52;
	};
};

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

	bool synic_initialized;

	struct hv_per_cpu_context __percpu *cpu_context;

	/*
	 * Hypervisor's notion of virtual processor ID is different from
	 * Linux' notion of CPU ID. This information can only be retrieved
	 * in the context of the calling CPU. Setup a map for easy access
	 * to this information:
	 *
	 * vp_index[a] is the Hyper-V's processor ID corresponding to
	 * Linux cpuid 'a'.
	 */
	u32 vp_index[NR_CPUS];

	/*
	 * To manage allocations in a NUMA node.
	 * Array indexed by numa node ID.
	 */
	struct cpumask *hv_numa_map;
};

extern struct hv_context hv_context;

struct hv_ring_buffer_debug_info {
	u32 current_interrupt_mask;
	u32 current_read_index;
	u32 current_write_index;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};

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
			struct kvec *kv_list,
			u32 kv_count, bool lock);

int hv_ringbuffer_read(struct vmbus_channel *channel,
		       void *buffer, u32 buflen, u32 *buffer_actual_len,
		       u64 *requestid, bool raw);

void hv_ringbuffer_get_debuginfo(struct hv_ring_buffer_info *ring_info,
			    struct hv_ring_buffer_debug_info *debug_info);

void hv_begin_read(struct hv_ring_buffer_info *rbi);

u32 hv_end_read(struct hv_ring_buffer_info *rbi);

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

	struct workqueue_struct *work_queue;
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

extern struct vmbus_channel_message_table_entry
	channel_message_table[CHANNELMSG_COUNT];


/* General vmbus interface */

struct hv_device *vmbus_device_create(const uuid_le *type,
				      const uuid_le *instance,
				      struct vmbus_channel *channel);

int vmbus_device_register(struct hv_device *child_device_obj);
void vmbus_device_unregister(struct hv_device *device_obj);

struct vmbus_channel *relid2channel(u32 relid);

void vmbus_free_channels(void);

/* Connection interface */

int vmbus_connect(void);
void vmbus_disconnect(void);

int vmbus_post_msg(void *buffer, size_t buflen, bool can_sleep);

void vmbus_on_event(unsigned long data);
void vmbus_on_msg_dpc(unsigned long data);

int hv_kvp_init(struct hv_util_service *);
void hv_kvp_deinit(void);
void hv_kvp_onchannelcallback(void *);

int hv_vss_init(struct hv_util_service *);
void hv_vss_deinit(void);
void hv_vss_onchannelcallback(void *);

int hv_fcopy_init(struct hv_util_service *);
void hv_fcopy_deinit(void);
void hv_fcopy_onchannelcallback(void *);
void vmbus_initiate_unload(bool crash);

static inline void hv_poll_channel(struct vmbus_channel *channel,
				   void (*cb)(void *))
{
	if (!channel)
		return;

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
