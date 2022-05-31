/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_CORE_H
#define VCHIQ_CORE_H

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/kref.h>
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <linux/raspberrypi/vchiq.h>

#include "vchiq_cfg.h"

/* Do this so that we can test-build the code on non-rpi systems */
#if IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE)

#else

#ifndef dsb
#define dsb(a)
#endif

#endif	/* IS_ENABLED(CONFIG_RASPBERRYPI_FIRMWARE) */

#define VCHIQ_SERVICE_HANDLE_INVALID 0

#define VCHIQ_SLOT_SIZE     4096
#define VCHIQ_MAX_MSG_SIZE  (VCHIQ_SLOT_SIZE - sizeof(struct vchiq_header))

/* Run time control of log level, based on KERN_XXX level. */
#define VCHIQ_LOG_DEFAULT  4
#define VCHIQ_LOG_ERROR    3
#define VCHIQ_LOG_WARNING  4
#define VCHIQ_LOG_INFO     6
#define VCHIQ_LOG_TRACE    7

#define VCHIQ_LOG_PREFIX   KERN_INFO "vchiq: "

#ifndef vchiq_log_error
#define vchiq_log_error(cat, fmt, ...) \
	do { if (cat >= VCHIQ_LOG_ERROR) \
		printk(VCHIQ_LOG_PREFIX fmt "\n", ##__VA_ARGS__); } while (0)
#endif
#ifndef vchiq_log_warning
#define vchiq_log_warning(cat, fmt, ...) \
	do { if (cat >= VCHIQ_LOG_WARNING) \
		 printk(VCHIQ_LOG_PREFIX fmt "\n", ##__VA_ARGS__); } while (0)
#endif
#ifndef vchiq_log_info
#define vchiq_log_info(cat, fmt, ...) \
	do { if (cat >= VCHIQ_LOG_INFO) \
		printk(VCHIQ_LOG_PREFIX fmt "\n", ##__VA_ARGS__); } while (0)
#endif
#ifndef vchiq_log_trace
#define vchiq_log_trace(cat, fmt, ...) \
	do { if (cat >= VCHIQ_LOG_TRACE) \
		printk(VCHIQ_LOG_PREFIX fmt "\n", ##__VA_ARGS__); } while (0)
#endif

#define vchiq_loud_error(...) \
	vchiq_log_error(vchiq_core_log_level, "===== " __VA_ARGS__)

#define VCHIQ_SLOT_MASK        (VCHIQ_SLOT_SIZE - 1)
#define VCHIQ_SLOT_QUEUE_MASK  (VCHIQ_MAX_SLOTS_PER_SIDE - 1)
#define VCHIQ_SLOT_ZERO_SLOTS  DIV_ROUND_UP(sizeof(struct vchiq_slot_zero), \
					    VCHIQ_SLOT_SIZE)

#define VCHIQ_FOURCC_AS_4CHARS(fourcc)	\
	((fourcc) >> 24) & 0xff, \
	((fourcc) >> 16) & 0xff, \
	((fourcc) >>  8) & 0xff, \
	(fourcc) & 0xff

#define BITSET_SIZE(b)        ((b + 31) >> 5)
#define BITSET_WORD(b)        (b >> 5)
#define BITSET_BIT(b)         (1 << (b & 31))
#define BITSET_IS_SET(bs, b)  (bs[BITSET_WORD(b)] & BITSET_BIT(b))
#define BITSET_SET(bs, b)     (bs[BITSET_WORD(b)] |= BITSET_BIT(b))
#define BITSET_CLR(bs, b)     (bs[BITSET_WORD(b)] &= ~BITSET_BIT(b))

enum {
	DEBUG_ENTRIES,
#if VCHIQ_ENABLE_DEBUG
	DEBUG_SLOT_HANDLER_COUNT,
	DEBUG_SLOT_HANDLER_LINE,
	DEBUG_PARSE_LINE,
	DEBUG_PARSE_HEADER,
	DEBUG_PARSE_MSGID,
	DEBUG_AWAIT_COMPLETION_LINE,
	DEBUG_DEQUEUE_MESSAGE_LINE,
	DEBUG_SERVICE_CALLBACK_LINE,
	DEBUG_MSG_QUEUE_FULL_COUNT,
	DEBUG_COMPLETION_QUEUE_FULL_COUNT,
#endif
	DEBUG_MAX
};

#if VCHIQ_ENABLE_DEBUG

#define DEBUG_INITIALISE(local) int *debug_ptr = (local)->debug
#define DEBUG_TRACE(d) \
	do { debug_ptr[DEBUG_ ## d] = __LINE__; dsb(sy); } while (0)
#define DEBUG_VALUE(d, v) \
	do { debug_ptr[DEBUG_ ## d] = (v); dsb(sy); } while (0)
#define DEBUG_COUNT(d) \
	do { debug_ptr[DEBUG_ ## d]++; dsb(sy); } while (0)

#else /* VCHIQ_ENABLE_DEBUG */

#define DEBUG_INITIALISE(local)
#define DEBUG_TRACE(d)
#define DEBUG_VALUE(d, v)
#define DEBUG_COUNT(d)

#endif /* VCHIQ_ENABLE_DEBUG */

enum vchiq_connstate {
	VCHIQ_CONNSTATE_DISCONNECTED,
	VCHIQ_CONNSTATE_CONNECTING,
	VCHIQ_CONNSTATE_CONNECTED,
	VCHIQ_CONNSTATE_PAUSING,
	VCHIQ_CONNSTATE_PAUSE_SENT,
	VCHIQ_CONNSTATE_PAUSED,
	VCHIQ_CONNSTATE_RESUMING,
	VCHIQ_CONNSTATE_PAUSE_TIMEOUT,
	VCHIQ_CONNSTATE_RESUME_TIMEOUT
};

enum {
	VCHIQ_SRVSTATE_FREE,
	VCHIQ_SRVSTATE_HIDDEN,
	VCHIQ_SRVSTATE_LISTENING,
	VCHIQ_SRVSTATE_OPENING,
	VCHIQ_SRVSTATE_OPEN,
	VCHIQ_SRVSTATE_OPENSYNC,
	VCHIQ_SRVSTATE_CLOSESENT,
	VCHIQ_SRVSTATE_CLOSERECVD,
	VCHIQ_SRVSTATE_CLOSEWAIT,
	VCHIQ_SRVSTATE_CLOSED
};

enum vchiq_bulk_dir {
	VCHIQ_BULK_TRANSMIT,
	VCHIQ_BULK_RECEIVE
};

struct vchiq_bulk {
	short mode;
	short dir;
	void *userdata;
	dma_addr_t data;
	int size;
	void *remote_data;
	int remote_size;
	int actual;
};

struct vchiq_bulk_queue {
	int local_insert;  /* Where to insert the next local bulk */
	int remote_insert; /* Where to insert the next remote bulk (master) */
	int process;       /* Bulk to transfer next */
	int remote_notify; /* Bulk to notify the remote client of next (mstr) */
	int remove;        /* Bulk to notify the local client of, and remove, next */
	struct vchiq_bulk bulks[VCHIQ_NUM_SERVICE_BULKS];
};

struct remote_event {
	int armed;
	int fired;
	u32 __unused;
};

struct opaque_platform_state;

struct vchiq_slot {
	char data[VCHIQ_SLOT_SIZE];
};

struct vchiq_slot_info {
	/* Use two counters rather than one to avoid the need for a mutex. */
	short use_count;
	short release_count;
};

struct vchiq_service {
	struct vchiq_service_base base;
	unsigned int handle;
	struct kref ref_count;
	struct rcu_head rcu;
	int srvstate;
	void (*userdata_term)(void *userdata);
	unsigned int localport;
	unsigned int remoteport;
	int public_fourcc;
	int client_id;
	char auto_close;
	char sync;
	char closing;
	char trace;
	atomic_t poll_flags;
	short version;
	short version_min;
	short peer_version;

	struct vchiq_state *state;
	struct vchiq_instance *instance;

	int service_use_count;

	struct vchiq_bulk_queue bulk_tx;
	struct vchiq_bulk_queue bulk_rx;

	struct completion remove_event;
	struct completion bulk_remove_event;
	struct mutex bulk_mutex;

	struct service_stats_struct {
		int quota_stalls;
		int slot_stalls;
		int bulk_stalls;
		int error_count;
		int ctrl_tx_count;
		int ctrl_rx_count;
		int bulk_tx_count;
		int bulk_rx_count;
		int bulk_aborted_count;
		u64 ctrl_tx_bytes;
		u64 ctrl_rx_bytes;
		u64 bulk_tx_bytes;
		u64 bulk_rx_bytes;
	} stats;

	int msg_queue_read;
	int msg_queue_write;
	struct completion msg_queue_pop;
	struct completion msg_queue_push;
	struct vchiq_header *msg_queue[VCHIQ_MAX_SLOTS];
};

/*
 * The quota information is outside struct vchiq_service so that it can
 * be statically allocated, since for accounting reasons a service's slot
 * usage is carried over between users of the same port number.
 */
struct vchiq_service_quota {
	unsigned short slot_quota;
	unsigned short slot_use_count;
	unsigned short message_quota;
	unsigned short message_use_count;
	struct completion quota_event;
	int previous_tx_index;
};

struct vchiq_shared_state {
	/* A non-zero value here indicates that the content is valid. */
	int initialised;

	/* The first and last (inclusive) slots allocated to the owner. */
	int slot_first;
	int slot_last;

	/* The slot allocated to synchronous messages from the owner. */
	int slot_sync;

	/*
	 * Signalling this event indicates that owner's slot handler thread
	 * should run.
	 */
	struct remote_event trigger;

	/*
	 * Indicates the byte position within the stream where the next message
	 * will be written. The least significant bits are an index into the
	 * slot. The next bits are the index of the slot in slot_queue.
	 */
	int tx_pos;

	/* This event should be signalled when a slot is recycled. */
	struct remote_event recycle;

	/* The slot_queue index where the next recycled slot will be written. */
	int slot_queue_recycle;

	/* This event should be signalled when a synchronous message is sent. */
	struct remote_event sync_trigger;

	/*
	 * This event should be signalled when a synchronous message has been
	 * released.
	 */
	struct remote_event sync_release;

	/* A circular buffer of slot indexes. */
	int slot_queue[VCHIQ_MAX_SLOTS_PER_SIDE];

	/* Debugging state */
	int debug[DEBUG_MAX];
};

struct vchiq_slot_zero {
	int magic;
	short version;
	short version_min;
	int slot_zero_size;
	int slot_size;
	int max_slots;
	int max_slots_per_side;
	int platform_data[2];
	struct vchiq_shared_state master;
	struct vchiq_shared_state slave;
	struct vchiq_slot_info slots[VCHIQ_MAX_SLOTS];
};

struct vchiq_state {
	int id;
	int initialised;
	enum vchiq_connstate conn_state;
	short version_common;

	struct vchiq_shared_state *local;
	struct vchiq_shared_state *remote;
	struct vchiq_slot *slot_data;

	unsigned short default_slot_quota;
	unsigned short default_message_quota;

	/* Event indicating connect message received */
	struct completion connect;

	/* Mutex protecting services */
	struct mutex mutex;
	struct vchiq_instance **instance;

	/* Processes incoming messages */
	struct task_struct *slot_handler_thread;

	/* Processes recycled slots */
	struct task_struct *recycle_thread;

	/* Processes synchronous messages */
	struct task_struct *sync_thread;

	/* Local implementation of the trigger remote event */
	wait_queue_head_t trigger_event;

	/* Local implementation of the recycle remote event */
	wait_queue_head_t recycle_event;

	/* Local implementation of the sync trigger remote event */
	wait_queue_head_t sync_trigger_event;

	/* Local implementation of the sync release remote event */
	wait_queue_head_t sync_release_event;

	char *tx_data;
	char *rx_data;
	struct vchiq_slot_info *rx_info;

	struct mutex slot_mutex;

	struct mutex recycle_mutex;

	struct mutex sync_mutex;

	struct mutex bulk_transfer_mutex;

	/*
	 * Indicates the byte position within the stream from where the next
	 * message will be read. The least significant bits are an index into
	 * the slot.The next bits are the index of the slot in
	 * remote->slot_queue.
	 */
	int rx_pos;

	/*
	 * A cached copy of local->tx_pos. Only write to local->tx_pos, and read
	 * from remote->tx_pos.
	 */
	int local_tx_pos;

	/* The slot_queue index of the slot to become available next. */
	int slot_queue_available;

	/* A flag to indicate if any poll has been requested */
	int poll_needed;

	/* Ths index of the previous slot used for data messages. */
	int previous_data_index;

	/* The number of slots occupied by data messages. */
	unsigned short data_use_count;

	/* The maximum number of slots to be occupied by data messages. */
	unsigned short data_quota;

	/* An array of bit sets indicating which services must be polled. */
	atomic_t poll_services[BITSET_SIZE(VCHIQ_MAX_SERVICES)];

	/* The number of the first unused service */
	int unused_service;

	/* Signalled when a free slot becomes available. */
	struct completion slot_available_event;

	struct completion slot_remove_event;

	/* Signalled when a free data slot becomes available. */
	struct completion data_quota_event;

	struct state_stats_struct {
		int slot_stalls;
		int data_stalls;
		int ctrl_tx_count;
		int ctrl_rx_count;
		int error_count;
	} stats;

	struct vchiq_service __rcu *services[VCHIQ_MAX_SERVICES];
	struct vchiq_service_quota service_quotas[VCHIQ_MAX_SERVICES];
	struct vchiq_slot_info slot_info[VCHIQ_MAX_SLOTS];

	struct opaque_platform_state *platform_state;
};

struct bulk_waiter {
	struct vchiq_bulk *bulk;
	struct completion event;
	int actual;
};

struct vchiq_config {
	unsigned int max_msg_size;
	unsigned int bulk_threshold;	/* The message size above which it
					 * is better to use a bulk transfer
					 * (<= max_msg_size)
					 */
	unsigned int max_outstanding_bulks;
	unsigned int max_services;
	short version;      /* The version of VCHIQ */
	short version_min;  /* The minimum compatible version of VCHIQ */
};

extern spinlock_t bulk_waiter_spinlock;

extern int vchiq_core_log_level;
extern int vchiq_core_msg_log_level;
extern int vchiq_sync_log_level;

extern struct vchiq_state *vchiq_states[VCHIQ_MAX_STATES];

extern const char *
get_conn_state_name(enum vchiq_connstate conn_state);

extern struct vchiq_slot_zero *
vchiq_init_slots(void *mem_base, int mem_size);

extern int
vchiq_init_state(struct vchiq_state *state, struct vchiq_slot_zero *slot_zero);

extern enum vchiq_status
vchiq_connect_internal(struct vchiq_state *state, struct vchiq_instance *instance);

struct vchiq_service *
vchiq_add_service_internal(struct vchiq_state *state,
			   const struct vchiq_service_params_kernel *params,
			   int srvstate, struct vchiq_instance *instance,
			   void (*userdata_term)(void *userdata));

extern enum vchiq_status
vchiq_open_service_internal(struct vchiq_service *service, int client_id);

extern enum vchiq_status
vchiq_close_service_internal(struct vchiq_service *service, int close_recvd);

extern void
vchiq_terminate_service_internal(struct vchiq_service *service);

extern void
vchiq_free_service_internal(struct vchiq_service *service);

extern void
vchiq_shutdown_internal(struct vchiq_state *state, struct vchiq_instance *instance);

extern void
remote_event_pollall(struct vchiq_state *state);

extern enum vchiq_status
vchiq_bulk_transfer(unsigned int handle, void *offset, void __user *uoffset,
		    int size, void *userdata, enum vchiq_bulk_mode mode,
		    enum vchiq_bulk_dir dir);

extern int
vchiq_dump_state(void *dump_context, struct vchiq_state *state);

extern int
vchiq_dump_service_state(void *dump_context, struct vchiq_service *service);

extern void
vchiq_loud_error_header(void);

extern void
vchiq_loud_error_footer(void);

extern void
request_poll(struct vchiq_state *state, struct vchiq_service *service,
	     int poll_type);

static inline struct vchiq_service *
handle_to_service(unsigned int handle)
{
	int idx = handle & (VCHIQ_MAX_SERVICES - 1);
	struct vchiq_state *state = vchiq_states[(handle / VCHIQ_MAX_SERVICES) &
		(VCHIQ_MAX_STATES - 1)];

	if (!state)
		return NULL;
	return rcu_dereference(state->services[idx]);
}

extern struct vchiq_service *
find_service_by_handle(unsigned int handle);

extern struct vchiq_service *
find_service_by_port(struct vchiq_state *state, unsigned int localport);

extern struct vchiq_service *
find_service_for_instance(struct vchiq_instance *instance, unsigned int handle);

extern struct vchiq_service *
find_closed_service_for_instance(struct vchiq_instance *instance, unsigned int handle);

extern struct vchiq_service *
__next_service_by_instance(struct vchiq_state *state,
			   struct vchiq_instance *instance,
			   int *pidx);

extern struct vchiq_service *
next_service_by_instance(struct vchiq_state *state,
			 struct vchiq_instance *instance,
			 int *pidx);

extern void
vchiq_service_get(struct vchiq_service *service);

extern void
vchiq_service_put(struct vchiq_service *service);

extern enum vchiq_status
vchiq_queue_message(unsigned int handle,
		    ssize_t (*copy_callback)(void *context, void *dest,
					     size_t offset, size_t maxsize),
		    void *context,
		    size_t size);

int vchiq_prepare_bulk_data(struct vchiq_bulk *bulk, void *offset, void __user *uoffset,
			    int size, int dir);

void vchiq_complete_bulk(struct vchiq_bulk *bulk);

void remote_event_signal(struct remote_event *event);

int vchiq_dump(void *dump_context, const char *str, int len);

int vchiq_dump_platform_state(void *dump_context);

int vchiq_dump_platform_instances(void *dump_context);

int vchiq_dump_platform_service_state(void *dump_context, struct vchiq_service *service);

int vchiq_use_service_internal(struct vchiq_service *service);

int vchiq_release_service_internal(struct vchiq_service *service);

void vchiq_on_remote_use(struct vchiq_state *state);

void vchiq_on_remote_release(struct vchiq_state *state);

int vchiq_platform_init_state(struct vchiq_state *state);

enum vchiq_status vchiq_check_service(struct vchiq_service *service);

void vchiq_on_remote_use_active(struct vchiq_state *state);

enum vchiq_status vchiq_send_remote_use(struct vchiq_state *state);

enum vchiq_status vchiq_send_remote_use_active(struct vchiq_state *state);

void vchiq_platform_conn_state_changed(struct vchiq_state *state,
				       enum vchiq_connstate oldstate,
				  enum vchiq_connstate newstate);

void vchiq_set_conn_state(struct vchiq_state *state, enum vchiq_connstate newstate);

void vchiq_log_dump_mem(const char *label, u32 addr, const void *void_mem, size_t num_bytes);

enum vchiq_status vchiq_remove_service(unsigned int service);

int vchiq_get_client_id(unsigned int service);

void vchiq_get_config(struct vchiq_config *config);

int vchiq_set_service_option(unsigned int service, enum vchiq_service_option option, int value);

#endif
