/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_H
#define VDO_H

#include <linux/atomic.h>
#include <linux/blk_types.h>
#include <linux/completion.h>
#include <linux/dm-kcopyd.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "admin-state.h"
#include "encodings.h"
#include "funnel-workqueue.h"
#include "packer.h"
#include "physical-zone.h"
#include "statistics.h"
#include "thread-registry.h"
#include "types.h"

enum notifier_state {
	/* Notifications are allowed but not in progress */
	MAY_NOTIFY,
	/* A notification is in progress */
	NOTIFYING,
	/* Notifications are not allowed */
	MAY_NOT_NOTIFY,
	/* A notification has completed */
	NOTIFIED,
};

/**
 * typedef vdo_read_only_notification_fn - A function to notify a listener that the VDO has gone
 *                                         read-only.
 * @listener: The object to notify.
 * @parent: The completion to notify in order to acknowledge the notification.
 */
typedef void (*vdo_read_only_notification_fn)(void *listener, struct vdo_completion *parent);

/*
 * An object to be notified when the VDO enters read-only mode
 */
struct read_only_listener {
	/* The listener */
	void *listener;
	/* The method to call to notify the listener */
	vdo_read_only_notification_fn notify;
	/* A pointer to the next listener */
	struct read_only_listener *next;
};

struct vdo_thread {
	struct vdo *vdo;
	thread_id_t thread_id;
	struct vdo_work_queue *queue;
	/*
	 * Each thread maintains its own notion of whether the VDO is read-only so that the
	 * read-only state can be checked from any base thread without worrying about
	 * synchronization or thread safety. This does mean that knowledge of the VDO going
	 * read-only does not occur simultaneously across the VDO's threads, but that does not seem
	 * to cause any problems.
	 */
	bool is_read_only;
	/*
	 * A list of objects waiting to be notified on this thread that the VDO has entered
	 * read-only mode.
	 */
	struct read_only_listener *listeners;
	struct registered_thread allocating_thread;
};

/* Keep struct bio statistics atomically */
struct atomic_bio_stats {
	atomic64_t read; /* Number of not REQ_WRITE bios */
	atomic64_t write; /* Number of REQ_WRITE bios */
	atomic64_t discard; /* Number of REQ_DISCARD bios */
	atomic64_t flush; /* Number of REQ_FLUSH bios */
	atomic64_t empty_flush; /* Number of REQ_PREFLUSH bios without data */
	atomic64_t fua; /* Number of REQ_FUA bios */
};

/* Counters are atomic since updates can arrive concurrently from arbitrary threads. */
struct atomic_statistics {
	atomic64_t bios_submitted;
	atomic64_t bios_completed;
	atomic64_t flush_out;
	atomic64_t invalid_advice_pbn_count;
	atomic64_t no_space_error_count;
	atomic64_t read_only_error_count;
	struct atomic_bio_stats bios_in;
	struct atomic_bio_stats bios_in_partial;
	struct atomic_bio_stats bios_out;
	struct atomic_bio_stats bios_out_completed;
	struct atomic_bio_stats bios_acknowledged;
	struct atomic_bio_stats bios_acknowledged_partial;
	struct atomic_bio_stats bios_meta;
	struct atomic_bio_stats bios_meta_completed;
	struct atomic_bio_stats bios_journal;
	struct atomic_bio_stats bios_journal_completed;
	struct atomic_bio_stats bios_page_cache;
	struct atomic_bio_stats bios_page_cache_completed;
};

struct read_only_notifier {
	/* The completion for entering read-only mode */
	struct vdo_completion completion;
	/* A completion waiting for notifications to be drained or enabled */
	struct vdo_completion *waiter;
	/* Lock to protect the next two fields */
	spinlock_t lock;
	/* The code of the error which put the VDO into read-only mode */
	int read_only_error;
	/* The current state of the notifier (values described above) */
	enum notifier_state state;
};

/*
 * The thread ID returned when the current thread is not a vdo thread, or can not be determined
 * (usually due to being at interrupt context).
 */
#define VDO_INVALID_THREAD_ID ((thread_id_t) -1)

struct thread_config {
	zone_count_t logical_zone_count;
	zone_count_t physical_zone_count;
	zone_count_t hash_zone_count;
	thread_count_t bio_thread_count;
	thread_count_t thread_count;
	thread_id_t admin_thread;
	thread_id_t journal_thread;
	thread_id_t packer_thread;
	thread_id_t dedupe_thread;
	thread_id_t bio_ack_thread;
	thread_id_t cpu_thread;
	thread_id_t *logical_threads;
	thread_id_t *physical_threads;
	thread_id_t *hash_zone_threads;
	thread_id_t *bio_threads;
};

struct thread_count_config;

struct vdo_super_block {
	/* The vio for reading and writing the super block to disk */
	struct vio vio;
	/* A buffer to hold the super block */
	u8 *buffer;
	/* Whether this super block may not be written */
	bool unwritable;
};

struct data_vio_pool;

struct vdo_administrator {
	struct vdo_completion completion;
	struct admin_state state;
	atomic_t busy;
	u32 phase;
	struct completion callback_sync;
};

struct vdo {
	char thread_name_prefix[MAX_VDO_WORK_QUEUE_NAME_LEN];
	struct vdo_thread *threads;
	vdo_action_fn action;
	struct vdo_completion *completion;
	struct vio_tracer *vio_tracer;

	/* The atomic version of the state of this vdo */
	atomic_t state;
	/* The full state of all components */
	struct vdo_component_states states;
	/*
	 * A counter value to attach to thread names and log messages to identify the individual
	 * device.
	 */
	unsigned int instance;
	/* The read-only notifier */
	struct read_only_notifier read_only_notifier;
	/* The load-time configuration of this vdo */
	struct device_config *device_config;
	/* The thread mapping */
	struct thread_config thread_config;

	/* The super block */
	struct vdo_super_block super_block;

	/* The partitioning of the underlying storage */
	struct layout layout;
	struct layout next_layout;
	struct dm_kcopyd_client *partition_copier;

	/* The block map */
	struct block_map *block_map;

	/* The journal for block map recovery */
	struct recovery_journal *recovery_journal;

	/* The slab depot */
	struct slab_depot *depot;

	/* The compressed-block packer */
	struct packer *packer;
	/* Whether incoming data should be compressed */
	bool compressing;

	/* The handler for flush requests */
	struct flusher *flusher;

	/* The state the vdo was in when loaded (primarily for unit tests) */
	enum vdo_state load_state;

	/* The logical zones of this vdo */
	struct logical_zones *logical_zones;

	/* The physical zones of this vdo */
	struct physical_zones *physical_zones;

	/* The hash lock zones of this vdo */
	struct hash_zones *hash_zones;

	/* Bio submission manager used for sending bios to the storage device. */
	struct io_submitter *io_submitter;

	/* The pool of data_vios for servicing incoming bios */
	struct data_vio_pool *data_vio_pool;

	/* The manager for administrative operations */
	struct vdo_administrator admin;

	/* Flags controlling administrative operations */
	const struct admin_state_code *suspend_type;
	bool allocations_allowed;
	bool dump_on_shutdown;
	atomic_t processing_message;

	/*
	 * Statistics
	 * Atomic stats counters
	 */
	struct atomic_statistics stats;
	/* Used to gather statistics without allocating memory */
	struct vdo_statistics stats_buffer;
	/* Protects the stats_buffer */
	struct mutex stats_mutex;

	/* A list of all device_configs referencing this vdo */
	struct list_head device_config_list;

	/* This VDO's list entry for the device registry */
	struct list_head registration;

	/* Underlying block device info. */
	u64 starting_sector_offset;
	struct volume_geometry geometry;

	/* N blobs of context data for LZ4 code, one per CPU thread. */
	char **compression_context;
};

/**
 * vdo_uses_bio_ack_queue() - Indicate whether the vdo is configured to use a separate work queue
 *                            for acknowledging received and processed bios.
 * @vdo: The vdo.
 *
 * Note that this directly controls the handling of write operations, but the compile-time flag
 * VDO_USE_BIO_ACK_QUEUE_FOR_READ is also checked for read operations.
 *
 * Return: Whether a bio-acknowledgement work queue is in use.
 */
static inline bool vdo_uses_bio_ack_queue(struct vdo *vdo)
{
	return vdo->device_config->thread_counts.bio_ack_threads > 0;
}

/**
 * typedef vdo_filter_fn - Method type for vdo matching methods.
 *
 * A filter function returns false if the vdo doesn't match.
 */
typedef bool (*vdo_filter_fn)(struct vdo *vdo, const void *context);

void vdo_initialize_device_registry_once(void);
struct vdo * __must_check vdo_find_matching(vdo_filter_fn filter, const void *context);

int __must_check vdo_make_thread(struct vdo *vdo, thread_id_t thread_id,
				 const struct vdo_work_queue_type *type,
				 unsigned int queue_count, void *contexts[]);

static inline int __must_check vdo_make_default_thread(struct vdo *vdo,
						       thread_id_t thread_id)
{
	return vdo_make_thread(vdo, thread_id, NULL, 1, NULL);
}

int __must_check vdo_make(unsigned int instance, struct device_config *config,
			  char **reason, struct vdo **vdo_ptr);

void vdo_destroy(struct vdo *vdo);

void vdo_load_super_block(struct vdo *vdo, struct vdo_completion *parent);

struct block_device * __must_check vdo_get_backing_device(const struct vdo *vdo);

const char * __must_check vdo_get_device_name(const struct dm_target *target);

int __must_check vdo_synchronous_flush(struct vdo *vdo);

const struct admin_state_code * __must_check vdo_get_admin_state(const struct vdo *vdo);

bool vdo_set_compressing(struct vdo *vdo, bool enable);

bool vdo_get_compressing(struct vdo *vdo);

void vdo_fetch_statistics(struct vdo *vdo, struct vdo_statistics *stats);

thread_id_t vdo_get_callback_thread_id(void);

enum vdo_state __must_check vdo_get_state(const struct vdo *vdo);

void vdo_set_state(struct vdo *vdo, enum vdo_state state);

void vdo_save_components(struct vdo *vdo, struct vdo_completion *parent);

int vdo_register_read_only_listener(struct vdo *vdo, void *listener,
				    vdo_read_only_notification_fn notification,
				    thread_id_t thread_id);

int vdo_enable_read_only_entry(struct vdo *vdo);

void vdo_wait_until_not_entering_read_only_mode(struct vdo_completion *parent);

void vdo_allow_read_only_mode_entry(struct vdo_completion *parent);

void vdo_enter_read_only_mode(struct vdo *vdo, int error_code);

bool __must_check vdo_is_read_only(struct vdo *vdo);

bool __must_check vdo_in_read_only_mode(const struct vdo *vdo);

bool __must_check vdo_in_recovery_mode(const struct vdo *vdo);

void vdo_enter_recovery_mode(struct vdo *vdo);

void vdo_assert_on_admin_thread(const struct vdo *vdo, const char *name);

void vdo_assert_on_logical_zone_thread(const struct vdo *vdo, zone_count_t logical_zone,
				       const char *name);

void vdo_assert_on_physical_zone_thread(const struct vdo *vdo, zone_count_t physical_zone,
					const char *name);

int __must_check vdo_get_physical_zone(const struct vdo *vdo, physical_block_number_t pbn,
				       struct physical_zone **zone_ptr);

void vdo_dump_status(const struct vdo *vdo);

#endif /* VDO_H */
