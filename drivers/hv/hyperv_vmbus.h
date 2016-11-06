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

/*
 * Timeout for services such as KVP and fcopy.
 */
#define HV_UTIL_TIMEOUT 30

/*
 * Timeout for guest-host handshake for services.
 */
#define HV_UTIL_NEGO_TIMEOUT 60

/*
 * The below CPUID leaves are present if VersionAndFeatures.HypervisorPresent
 * is set by CPUID(HVCPUID_VERSION_FEATURES).
 */
enum hv_cpuid_function {
	HVCPUID_VERSION_FEATURES		= 0x00000001,
	HVCPUID_VENDOR_MAXFUNCTION		= 0x40000000,
	HVCPUID_INTERFACE			= 0x40000001,

	/*
	 * The remaining functions depend on the value of
	 * HVCPUID_INTERFACE
	 */
	HVCPUID_VERSION			= 0x40000002,
	HVCPUID_FEATURES			= 0x40000003,
	HVCPUID_ENLIGHTENMENT_INFO	= 0x40000004,
	HVCPUID_IMPLEMENTATION_LIMITS		= 0x40000005,
};

#define  HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE   0x400

#define HV_X64_MSR_CRASH_P0   0x40000100
#define HV_X64_MSR_CRASH_P1   0x40000101
#define HV_X64_MSR_CRASH_P2   0x40000102
#define HV_X64_MSR_CRASH_P3   0x40000103
#define HV_X64_MSR_CRASH_P4   0x40000104
#define HV_X64_MSR_CRASH_CTL  0x40000105

#define HV_CRASH_CTL_CRASH_NOTIFY (1ULL << 63)

/* Define version of the synthetic interrupt controller. */
#define HV_SYNIC_VERSION		(1)

#define HV_ANY_VP			(0xFFFFFFFF)

/* Define synthetic interrupt controller flag constants. */
#define HV_EVENT_FLAGS_COUNT		(256 * 8)
#define HV_EVENT_FLAGS_BYTE_COUNT	(256)
#define HV_EVENT_FLAGS_DWORD_COUNT	(256 / sizeof(u32))

/* Define invalid partition identifier. */
#define HV_PARTITION_ID_INVALID		((u64)0x0)

/* Define port type. */
enum hv_port_type {
	HVPORT_MSG	= 1,
	HVPORT_EVENT		= 2,
	HVPORT_MONITOR	= 3
};

/* Define port information structure. */
struct hv_port_info {
	enum hv_port_type port_type;
	u32 padding;
	union {
		struct {
			u32 target_sint;
			u32 target_vp;
			u64 rsvdz;
		} message_port_info;
		struct {
			u32 target_sint;
			u32 target_vp;
			u16 base_flag_number;
			u16 flag_count;
			u32 rsvdz;
		} event_port_info;
		struct {
			u64 monitor_address;
			u64 rsvdz;
		} monitor_port_info;
	};
};

struct hv_connection_info {
	enum hv_port_type port_type;
	u32 padding;
	union {
		struct {
			u64 rsvdz;
		} message_connection_info;
		struct {
			u64 rsvdz;
		} event_connection_info;
		struct {
			u64 monitor_address;
		} monitor_connection_info;
	};
};

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

/* Define the number of message buffers associated with each port. */
#define HV_PORT_MESSAGE_BUFFER_COUNT	(16)

/* Define the synthetic interrupt controller event flags format. */
union hv_synic_event_flags {
	u8 flags8[HV_EVENT_FLAGS_BYTE_COUNT];
	u32 flags32[HV_EVENT_FLAGS_DWORD_COUNT];
};

/* Define the synthetic interrupt flags page layout. */
struct hv_synic_event_flags_page {
	union hv_synic_event_flags sintevent_flags[HV_SYNIC_SINT_COUNT];
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

/* Definition of the hv_post_message hypercall input structure. */
struct hv_input_post_message {
	union hv_connection_id connectionid;
	u32 reserved;
	u32 message_type;
	u32 payload_size;
	u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
};

/*
 * Versioning definitions used for guests reporting themselves to the
 * hypervisor, and visa versa.
 */

/* Version info reported by guest OS's */
enum hv_guest_os_vendor {
	HVGUESTOS_VENDOR_MICROSOFT	= 0x0001
};

enum hv_guest_os_microsoft_ids {
	HVGUESTOS_MICROSOFT_UNDEFINED	= 0x00,
	HVGUESTOS_MICROSOFT_MSDOS		= 0x01,
	HVGUESTOS_MICROSOFT_WINDOWS3X	= 0x02,
	HVGUESTOS_MICROSOFT_WINDOWS9X	= 0x03,
	HVGUESTOS_MICROSOFT_WINDOWSNT	= 0x04,
	HVGUESTOS_MICROSOFT_WINDOWSCE	= 0x05
};

/*
 * Declare the MSR used to identify the guest OS.
 */
#define HV_X64_MSR_GUEST_OS_ID	0x40000000

union hv_x64_msr_guest_os_id_contents {
	u64 as_uint64;
	struct {
		u64 build_number:16;
		u64 service_version:8; /* Service Pack, etc. */
		u64 minor_version:8;
		u64 major_version:8;
		u64 os_id:8; /* enum hv_guest_os_microsoft_ids (if Vendor=MS) */
		u64 vendor_id:16; /* enum hv_guest_os_vendor */
	};
};

/*
 * Declare the MSR used to setup pages used to communicate with the hypervisor.
 */
#define HV_X64_MSR_HYPERCALL	0x40000001

union hv_x64_msr_hypercall_contents {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:11;
		u64 guest_physical_address:52;
	};
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

/* #defines */

#define HV_PRESENT_BIT			0x80000000

/*
 * The guest OS needs to register the guest ID with the hypervisor.
 * The guest ID is a 64 bit entity and the structure of this ID is
 * specified in the Hyper-V specification:
 *
 * http://msdn.microsoft.com/en-us/library/windows/hardware/ff542653%28v=vs.85%29.aspx
 *
 * While the current guideline does not specify how Linux guest ID(s)
 * need to be generated, our plan is to publish the guidelines for
 * Linux and other guest operating systems that currently are hosted
 * on Hyper-V. The implementation here conforms to this yet
 * unpublished guidelines.
 *
 *
 * Bit(s)
 * 63 - Indicates if the OS is Open Source or not; 1 is Open Source
 * 62:56 - Os Type; Linux is 0x100
 * 55:48 - Distro specific identification
 * 47:16 - Linux kernel version number
 * 15:0  - Distro specific identification
 *
 *
 */

#define HV_LINUX_VENDOR_ID		0x8100

/*
 * Generate the guest ID based on the guideline described above.
 */

static inline  __u64 generate_guest_id(__u8 d_info1, __u32 kernel_version,
					__u16 d_info2)
{
	__u64 guest_id = 0;

	guest_id = (((__u64)HV_LINUX_VENDOR_ID) << 48);
	guest_id |= (((__u64)(d_info1)) << 48);
	guest_id |= (((__u64)(kernel_version)) << 16);
	guest_id |= ((__u64)(d_info2));

	return guest_id;
}


#define HV_CPU_POWER_MANAGEMENT		(1 << 0)
#define HV_RECOMMENDATIONS_MAX		4

#define HV_X64_MAX			5
#define HV_CAPS_MAX			8


#define HV_HYPERCALL_PARAM_ALIGN	sizeof(u64)


/* Service definitions */

#define HV_SERVICE_PARENT_PORT				(0)
#define HV_SERVICE_PARENT_CONNECTION			(0)

#define HV_SERVICE_CONNECT_RESPONSE_SUCCESS		(0)
#define HV_SERVICE_CONNECT_RESPONSE_INVALID_PARAMETER	(1)
#define HV_SERVICE_CONNECT_RESPONSE_UNKNOWN_SERVICE	(2)
#define HV_SERVICE_CONNECT_RESPONSE_CONNECTION_REJECTED	(3)

#define HV_SERVICE_CONNECT_REQUEST_MESSAGE_ID		(1)
#define HV_SERVICE_CONNECT_RESPONSE_MESSAGE_ID		(2)
#define HV_SERVICE_DISCONNECT_REQUEST_MESSAGE_ID	(3)
#define HV_SERVICE_DISCONNECT_RESPONSE_MESSAGE_ID	(4)
#define HV_SERVICE_MAX_MESSAGE_ID				(4)

#define HV_SERVICE_PROTOCOL_VERSION (0x0010)
#define HV_CONNECT_PAYLOAD_BYTE_COUNT 64

/* #define VMBUS_REVISION_NUMBER	6 */

/* Our local vmbus's port and connection id. Anything >0 is fine */
/* #define VMBUS_PORT_ID		11 */

/* 628180B8-308D-4c5e-B7DB-1BEB62E62EF4 */
static const uuid_le VMBUS_SERVICE_ID = {
	.b = {
		0xb8, 0x80, 0x81, 0x62, 0x8d, 0x30, 0x5e, 0x4c,
		0xb7, 0xdb, 0x1b, 0xeb, 0x62, 0xe6, 0x2e, 0xf4
	},
};



struct hv_context {
	/* We only support running on top of Hyper-V
	* So at this point this really can only contain the Hyper-V ID
	*/
	u64 guestid;

	void *hypercall_page;
	void *tsc_page;

	bool synic_initialized;

	void *synic_message_page[NR_CPUS];
	void *synic_event_page[NR_CPUS];
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
	 * Starting with win8, we can take channel interrupts on any CPU;
	 * we will manage the tasklet that handles events messages on a per CPU
	 * basis.
	 */
	struct tasklet_struct *event_dpc[NR_CPUS];
	struct tasklet_struct *msg_dpc[NR_CPUS];
	/*
	 * To optimize the mapping of relid to channel, maintain
	 * per-cpu list of the channels based on their CPU affinity.
	 */
	struct list_head percpu_list[NR_CPUS];
	/*
	 * buffer to post messages to the host.
	 */
	void *post_msg_page[NR_CPUS];
	/*
	 * Support PV clockevent device.
	 */
	struct clock_event_device *clk_evt[NR_CPUS];
	/*
	 * To manage allocations in a NUMA node.
	 * Array indexed by numa node ID.
	 */
	struct cpumask *hv_numa_map;
};

extern struct hv_context hv_context;

struct ms_hyperv_tsc_page {
	volatile u32 tsc_sequence;
	u32 reserved1;
	volatile u64 tsc_scale;
	volatile s64 tsc_offset;
	u64 reserved2[509];
};

struct hv_ring_buffer_debug_info {
	u32 current_interrupt_mask;
	u32 current_read_index;
	u32 current_write_index;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};

/* Hv Interface */

extern int hv_init(void);

extern void hv_cleanup(bool crash);

extern int hv_post_message(union hv_connection_id connection_id,
			 enum hv_message_type message_type,
			 void *payload, size_t payload_size);

extern int hv_synic_alloc(void);

extern void hv_synic_free(void);

extern void hv_synic_init(void *irqarg);

extern void hv_synic_cleanup(void *arg);

extern void hv_synic_clockevents_cleanup(void);

/*
 * Host version information.
 */
extern unsigned int host_info_eax;
extern unsigned int host_info_ebx;
extern unsigned int host_info_ecx;
extern unsigned int host_info_edx;

/* Interface */


int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info,
		       struct page *pages, u32 pagecnt);

void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int hv_ringbuffer_write(struct vmbus_channel *channel,
		    struct kvec *kv_list,
		    u32 kv_count, bool lock,
		    bool kick_q);

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

/* Free the message slot and signal end-of-message if required */
static inline void vmbus_signal_eom(struct hv_message *msg, u32 old_msg_type)
{
	/*
	 * On crash we're reading some other CPU's message page and we need
	 * to be careful: this other CPU may already had cleared the header
	 * and the host may already had delivered some other message there.
	 * In case we blindly write msg->header.message_type we're going
	 * to lose it. We can still lose a message of the same type but
	 * we count on the fact that there can only be one
	 * CHANNELMSG_UNLOAD_RESPONSE and we don't care about other messages
	 * on crash.
	 */
	if (cmpxchg(&msg->header.message_type, old_msg_type,
		    HVMSG_NONE) != old_msg_type)
		return;

	/*
	 * Make sure the write to MessageType (ie set to
	 * HVMSG_NONE) happens before we read the
	 * MessagePending and EOMing. Otherwise, the EOMing
	 * will not deliver any more messages since there is
	 * no empty slot
	 */
	mb();

	if (msg->header.message_flags.msg_pending) {
		/*
		 * This will cause message queue rescan to
		 * possibly deliver another msg from the
		 * hypervisor
		 */
		wrmsrl(HV_X64_MSR_EOM, 0);
	}
}

/* General vmbus interface */

struct hv_device *vmbus_device_create(const uuid_le *type,
				      const uuid_le *instance,
				      struct vmbus_channel *channel);

int vmbus_device_register(struct hv_device *child_device_obj);
void vmbus_device_unregister(struct hv_device *device_obj);

/* static void */
/* VmbusChildDeviceDestroy( */
/* struct hv_device *); */

struct vmbus_channel *relid2channel(u32 relid);

void vmbus_free_channels(void);

/* Connection interface */

int vmbus_connect(void);
void vmbus_disconnect(void);

int vmbus_post_msg(void *buffer, size_t buflen);

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
