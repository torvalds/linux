/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *   K. Y. Srinivasan <kys@microsoft.com>
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
};

struct hv_context {
	/* We only support running on top of Hyper-V
	 * So at this point this really can only contain the Hyper-V ID
	 */
	u64 guestid;

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

extern void hv_synic_enable_regs(unsigned int cpu);
extern int hv_synic_init(unsigned int cpu);

extern void hv_synic_disable_regs(unsigned int cpu);
extern int hv_synic_cleanup(unsigned int cpu);

/* Interface */

void hv_ringbuffer_pre_init(struct vmbus_channel *channel);

int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info,
		       struct page *pages, u32 pagecnt);

void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int hv_ringbuffer_write(struct vmbus_channel *channel,
			const struct kvec *kv_list, u32 kv_count);

int hv_ringbuffer_read(struct vmbus_channel *channel,
		       void *buffer, u32 buflen, u32 *buffer_actual_len,
		       u64 *requestid, bool raw);

/*
 * The Maximum number of channels (16348) is determined by the size of the
 * interrupt page, which is HV_HYP_PAGE_SIZE. 1/2 of HV_HYP_PAGE_SIZE is to
 * send endpoint interrupts, and the other is to receive endpoint interrupts.
 */
#define MAX_NUM_CHANNELS	((HV_HYP_PAGE_SIZE >> 1) << 3)

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

	/*
	 * The number of sub-channels and hv_sock channels that should be
	 * cleaned up upon suspend: sub-channels will be re-created upon
	 * resume, and hv_sock channels should not survive suspend.
	 */
	atomic_t nr_chan_close_on_suspend;
	/*
	 * vmbus_bus_suspend() waits for "nr_chan_close_on_suspend" to
	 * drop to zero.
	 */
	struct completion ready_for_suspend_event;

	/*
	 * The number of primary channels that should be "fixed up"
	 * upon resume: these channels are re-offered upon resume, and some
	 * fields of the channel offers (i.e. child_relid and connection_id)
	 * can change, so the old offermsg must be fixed up, before the resume
	 * callbacks of the VSC drivers start to further touch the channels.
	 */
	atomic_t nr_chan_fixup_on_resume;
	/*
	 * vmbus_bus_resume() waits for "nr_chan_fixup_on_resume" to
	 * drop to zero.
	 */
	struct completion ready_for_resume_event;
};


struct vmbus_msginfo {
	/* Bookkeeping stuff */
	struct list_head msglist_entry;

	/* The message itself */
	unsigned char msg[];
};


extern struct vmbus_connection vmbus_connection;

int vmbus_negotiate_version(struct vmbus_channel_msginfo *msginfo, u32 version);

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
	u32 min_payload_len;
};

extern const struct vmbus_channel_message_table_entry
	channel_message_table[CHANNELMSG_COUNT];


/* General vmbus interface */

struct hv_device *vmbus_device_create(const guid_t *type,
				      const guid_t *instance,
				      struct vmbus_channel *channel);

int vmbus_device_register(struct hv_device *child_device_obj);
void vmbus_device_unregister(struct hv_device *device_obj);
int vmbus_add_channel_kobj(struct hv_device *device_obj,
			   struct vmbus_channel *channel);

void vmbus_remove_channel_attr_group(struct vmbus_channel *channel);

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
int hv_kvp_pre_suspend(void);
int hv_kvp_pre_resume(void);
void hv_kvp_onchannelcallback(void *context);

int hv_vss_init(struct hv_util_service *srv);
void hv_vss_deinit(void);
int hv_vss_pre_suspend(void);
int hv_vss_pre_resume(void);
void hv_vss_onchannelcallback(void *context);

int hv_fcopy_init(struct hv_util_service *srv);
void hv_fcopy_deinit(void);
int hv_fcopy_pre_suspend(void);
int hv_fcopy_pre_resume(void);
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

enum delay {
	INTERRUPT_DELAY = 0,
	MESSAGE_DELAY   = 1,
};

#ifdef CONFIG_HYPERV_TESTING

int hv_debug_add_dev_dir(struct hv_device *dev);
void hv_debug_rm_dev_dir(struct hv_device *dev);
void hv_debug_rm_all_dir(void);
int hv_debug_init(void);
void hv_debug_delay_test(struct vmbus_channel *channel, enum delay delay_type);

#else /* CONFIG_HYPERV_TESTING */

static inline void hv_debug_rm_dev_dir(struct hv_device *dev) {};
static inline void hv_debug_rm_all_dir(void) {};
static inline void hv_debug_delay_test(struct vmbus_channel *channel,
				       enum delay delay_type) {};
static inline int hv_debug_init(void)
{
	return -1;
}

static inline int hv_debug_add_dev_dir(struct hv_device *dev)
{
	return -1;
}

#endif /* CONFIG_HYPERV_TESTING */

#endif /* _HYPERV_VMBUS_H */
