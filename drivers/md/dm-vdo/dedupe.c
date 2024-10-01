// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

/**
 * DOC:
 *
 * Hash Locks:
 *
 * A hash_lock controls and coordinates writing, index access, and dedupe among groups of data_vios
 * concurrently writing identical blocks, allowing them to deduplicate not only against advice but
 * also against each other. This saves on index queries and allows those data_vios to concurrently
 * deduplicate against a single block instead of being serialized through a PBN read lock. Only one
 * index query is needed for each hash_lock, instead of one for every data_vio.
 *
 * Hash_locks are assigned to hash_zones by computing a modulus on the hash itself. Each hash_zone
 * has a single dedicated queue and thread for performing all operations on the hash_locks assigned
 * to that zone. The concurrency guarantees of this single-threaded model allow the code to omit
 * more fine-grained locking for the hash_lock structures.
 *
 * A hash_lock acts like a state machine perhaps more than as a lock. Other than the starting and
 * ending states INITIALIZING and BYPASSING, every state represents and is held for the duration of
 * an asynchronous operation. All state transitions are performed on the thread of the hash_zone
 * containing the lock. An asynchronous operation is almost always performed upon entering a state,
 * and the callback from that operation triggers exiting the state and entering a new state.
 *
 * In all states except DEDUPING, there is a single data_vio, called the lock agent, performing the
 * asynchronous operations on behalf of the lock. The agent will change during the lifetime of the
 * lock if the lock is shared by more than one data_vio. data_vios waiting to deduplicate are kept
 * on a wait queue. Viewed a different way, the agent holds the lock exclusively until the lock
 * enters the DEDUPING state, at which point it becomes a shared lock that all the waiters (and any
 * new data_vios that arrive) use to share a PBN lock. In state DEDUPING, there is no agent. When
 * the last data_vio in the lock calls back in DEDUPING, it becomes the agent and the lock becomes
 * exclusive again. New data_vios that arrive in the lock will also go on the wait queue.
 *
 * The existence of lock waiters is a key factor controlling which state the lock transitions to
 * next. When the lock is new or has waiters, it will always try to reach DEDUPING, and when it
 * doesn't, it will try to clean up and exit.
 *
 * Deduping requires holding a PBN lock on a block that is known to contain data identical to the
 * data_vios in the lock, so the lock will send the agent to the duplicate zone to acquire the PBN
 * lock (LOCKING), to the kernel I/O threads to read and verify the data (VERIFYING), or to write a
 * new copy of the data to a full data block or a slot in a compressed block (WRITING).
 *
 * Cleaning up consists of updating the index when the data location is different from the initial
 * index query (UPDATING, triggered by stale advice, compression, and rollover), releasing the PBN
 * lock on the duplicate block (UNLOCKING), and if the agent is the last data_vio referencing the
 * lock, releasing the hash_lock itself back to the hash zone (BYPASSING).
 *
 * The shortest sequence of states is for non-concurrent writes of new data:
 *   INITIALIZING -> QUERYING -> WRITING -> BYPASSING
 * This sequence is short because no PBN read lock or index update is needed.
 *
 * Non-concurrent, finding valid advice looks like this (endpoints elided):
 *   -> QUERYING -> LOCKING -> VERIFYING -> DEDUPING -> UNLOCKING ->
 * Or with stale advice (endpoints elided):
 *   -> QUERYING -> LOCKING -> VERIFYING -> UNLOCKING -> WRITING -> UPDATING ->
 *
 * When there are not enough available reference count increments available on a PBN for a data_vio
 * to deduplicate, a new lock is forked and the excess waiters roll over to the new lock (which
 * goes directly to WRITING). The new lock takes the place of the old lock in the lock map so new
 * data_vios will be directed to it. The two locks will proceed independently, but only the new
 * lock will have the right to update the index (unless it also forks).
 *
 * Since rollover happens in a lock instance, once a valid data location has been selected, it will
 * not change. QUERYING and WRITING are only performed once per lock lifetime. All other
 * non-endpoint states can be re-entered.
 *
 * The function names in this module follow a convention referencing the states and transitions in
 * the state machine. For example, for the LOCKING state, there are start_locking() and
 * finish_locking() functions.  start_locking() is invoked by the finish function of the state (or
 * states) that transition to LOCKING. It performs the actual lock state change and must be invoked
 * on the hash zone thread.  finish_locking() is called by (or continued via callback from) the
 * code actually obtaining the lock. It does any bookkeeping or decision-making required and
 * invokes the appropriate start function of the state being transitioned to after LOCKING.
 *
 * ----------------------------------------------------------------------
 *
 * Index Queries:
 *
 * A query to the UDS index is handled asynchronously by the index's threads. When the query is
 * complete, a callback supplied with the query will be called from one of the those threads. Under
 * heavy system load, the index may be slower to respond than is desirable for reasonable I/O
 * throughput. Since deduplication of writes is not necessary for correct operation of a VDO
 * device, it is acceptable to timeout out slow index queries and proceed to fulfill a write
 * request without deduplicating. However, because the uds_request struct itself is supplied by the
 * caller, we can not simply reuse a uds_request object which we have chosen to timeout. Hence,
 * each hash_zone maintains a pool of dedupe_contexts which each contain a uds_request along with a
 * reference to the data_vio on behalf of which they are performing a query.
 *
 * When a hash_lock needs to query the index, it attempts to acquire an unused dedupe_context from
 * its hash_zone's pool. If one is available, that context is prepared, associated with the
 * hash_lock's agent, added to the list of pending contexts, and then sent to the index. The
 * context's state will be transitioned from DEDUPE_CONTEXT_IDLE to DEDUPE_CONTEXT_PENDING. If all
 * goes well, the dedupe callback will be called by the index which will change the context's state
 * to DEDUPE_CONTEXT_COMPLETE, and the associated data_vio will be enqueued to run back in the hash
 * zone where the query results will be processed and the context will be put back in the idle
 * state and returned to the hash_zone's available list.
 *
 * The first time an index query is launched from a given hash_zone, a timer is started. When the
 * timer fires, the hash_zone's completion is enqueued to run in the hash_zone where the zone's
 * pending list will be searched for any contexts in the pending state which have been running for
 * too long. Those contexts are transitioned to the DEDUPE_CONTEXT_TIMED_OUT state and moved to the
 * zone's timed_out list where they won't be examined again if there is a subsequent time out). The
 * data_vios associated with timed out contexts are sent to continue processing their write
 * operation without deduplicating. The timer is also restarted.
 *
 * When the dedupe callback is run for a context which is in the timed out state, that context is
 * moved to the DEDUPE_CONTEXT_TIMED_OUT_COMPLETE state. No other action need be taken as the
 * associated data_vios have already been dispatched.
 *
 * If a hash_lock needs a dedupe context, and the available list is empty, the timed_out list will
 * be searched for any contexts which are timed out and complete. One of these will be used
 * immediately, and the rest will be returned to the available list and marked idle.
 */

#include "dedupe.h"

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/ratelimit.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"
#include "string-utils.h"

#include "indexer.h"

#include "action-manager.h"
#include "admin-state.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "int-map.h"
#include "io-submitter.h"
#include "packer.h"
#include "physical-zone.h"
#include "slab-depot.h"
#include "statistics.h"
#include "types.h"
#include "vdo.h"
#include "wait-queue.h"

#define DEDUPE_QUERY_TIMER_IDLE 0
#define DEDUPE_QUERY_TIMER_RUNNING 1
#define DEDUPE_QUERY_TIMER_FIRED 2

enum dedupe_context_state {
	DEDUPE_CONTEXT_IDLE,
	DEDUPE_CONTEXT_PENDING,
	DEDUPE_CONTEXT_TIMED_OUT,
	DEDUPE_CONTEXT_COMPLETE,
	DEDUPE_CONTEXT_TIMED_OUT_COMPLETE,
};

/* Possible index states: closed, opened, or transitioning between those two. */
enum index_state {
	IS_CLOSED,
	IS_CHANGING,
	IS_OPENED,
};

static const char *CLOSED = "closed";
static const char *CLOSING = "closing";
static const char *ERROR = "error";
static const char *OFFLINE = "offline";
static const char *ONLINE = "online";
static const char *OPENING = "opening";
static const char *SUSPENDED = "suspended";
static const char *UNKNOWN = "unknown";

/* Version 2 uses the kernel space UDS index and is limited to 16 bytes */
#define UDS_ADVICE_VERSION 2
/* version byte + state byte + 64-bit little-endian PBN */
#define UDS_ADVICE_SIZE (1 + 1 + sizeof(u64))

enum hash_lock_state {
	/* State for locks that are not in use or are being initialized. */
	VDO_HASH_LOCK_INITIALIZING,

	/* This is the sequence of states typically used on the non-dedupe path. */
	VDO_HASH_LOCK_QUERYING,
	VDO_HASH_LOCK_WRITING,
	VDO_HASH_LOCK_UPDATING,

	/* The remaining states are typically used on the dedupe path in this order. */
	VDO_HASH_LOCK_LOCKING,
	VDO_HASH_LOCK_VERIFYING,
	VDO_HASH_LOCK_DEDUPING,
	VDO_HASH_LOCK_UNLOCKING,

	/*
	 * Terminal state for locks returning to the pool. Must be last both because it's the final
	 * state, and also because it's used to count the states.
	 */
	VDO_HASH_LOCK_BYPASSING,
};

static const char * const LOCK_STATE_NAMES[] = {
	[VDO_HASH_LOCK_BYPASSING] = "BYPASSING",
	[VDO_HASH_LOCK_DEDUPING] = "DEDUPING",
	[VDO_HASH_LOCK_INITIALIZING] = "INITIALIZING",
	[VDO_HASH_LOCK_LOCKING] = "LOCKING",
	[VDO_HASH_LOCK_QUERYING] = "QUERYING",
	[VDO_HASH_LOCK_UNLOCKING] = "UNLOCKING",
	[VDO_HASH_LOCK_UPDATING] = "UPDATING",
	[VDO_HASH_LOCK_VERIFYING] = "VERIFYING",
	[VDO_HASH_LOCK_WRITING] = "WRITING",
};

struct hash_lock {
	/* The block hash covered by this lock */
	struct uds_record_name hash;

	/* When the lock is unused, this list entry allows the lock to be pooled */
	struct list_head pool_node;

	/*
	 * A list containing the data VIOs sharing this lock, all having the same record name and
	 * data block contents, linked by their hash_lock_node fields.
	 */
	struct list_head duplicate_ring;

	/* The number of data_vios sharing this lock instance */
	data_vio_count_t reference_count;

	/* The maximum value of reference_count in the lifetime of this lock */
	data_vio_count_t max_references;

	/* The current state of this lock */
	enum hash_lock_state state;

	/* True if the UDS index should be updated with new advice */
	bool update_advice;

	/* True if the advice has been verified to be a true duplicate */
	bool verified;

	/* True if the lock has already accounted for an initial verification */
	bool verify_counted;

	/* True if this lock is registered in the lock map (cleared on rollover) */
	bool registered;

	/*
	 * If verified is false, this is the location of a possible duplicate. If verified is true,
	 * it is the verified location of a true duplicate.
	 */
	struct zoned_pbn duplicate;

	/* The PBN lock on the block containing the duplicate data */
	struct pbn_lock *duplicate_lock;

	/* The data_vio designated to act on behalf of the lock */
	struct data_vio *agent;

	/*
	 * Other data_vios with data identical to the agent who are currently waiting for the agent
	 * to get the information they all need to deduplicate--either against each other, or
	 * against an existing duplicate on disk.
	 */
	struct vdo_wait_queue waiters;
};

#define LOCK_POOL_CAPACITY MAXIMUM_VDO_USER_VIOS

struct hash_zones {
	struct action_manager *manager;
	struct uds_parameters parameters;
	struct uds_index_session *index_session;
	struct ratelimit_state ratelimiter;
	atomic64_t timeouts;
	atomic64_t dedupe_context_busy;

	/* This spinlock protects the state fields and the starting of dedupe requests. */
	spinlock_t lock;

	/* The fields in the next block are all protected by the lock */
	struct vdo_completion completion;
	enum index_state index_state;
	enum index_state index_target;
	struct admin_state state;
	bool changing;
	bool create_flag;
	bool dedupe_flag;
	bool error_flag;
	u64 reported_timeouts;

	/* The number of zones */
	zone_count_t zone_count;
	/* The hash zones themselves */
	struct hash_zone zones[];
};

/* These are in milliseconds. */
unsigned int vdo_dedupe_index_timeout_interval = 5000;
unsigned int vdo_dedupe_index_min_timer_interval = 100;
/* Same two variables, in jiffies for easier consumption. */
static u64 vdo_dedupe_index_timeout_jiffies;
static u64 vdo_dedupe_index_min_timer_jiffies;

static inline struct hash_zone *as_hash_zone(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_HASH_ZONE_COMPLETION);
	return container_of(completion, struct hash_zone, completion);
}

static inline struct hash_zones *as_hash_zones(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_HASH_ZONES_COMPLETION);
	return container_of(completion, struct hash_zones, completion);
}

static inline void assert_in_hash_zone(struct hash_zone *zone, const char *name)
{
	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == zone->thread_id),
			    "%s called on hash zone thread", name);
}

static inline bool change_context_state(struct dedupe_context *context, int old, int new)
{
	return (atomic_cmpxchg(&context->state, old, new) == old);
}

static inline bool change_timer_state(struct hash_zone *zone, int old, int new)
{
	return (atomic_cmpxchg(&zone->timer_state, old, new) == old);
}

/**
 * return_hash_lock_to_pool() - (Re)initialize a hash lock and return it to its pool.
 * @zone: The zone from which the lock was borrowed.
 * @lock: The lock that is no longer in use.
 */
static void return_hash_lock_to_pool(struct hash_zone *zone, struct hash_lock *lock)
{
	memset(lock, 0, sizeof(*lock));
	INIT_LIST_HEAD(&lock->pool_node);
	INIT_LIST_HEAD(&lock->duplicate_ring);
	vdo_waitq_init(&lock->waiters);
	list_add_tail(&lock->pool_node, &zone->lock_pool);
}

/**
 * vdo_get_duplicate_lock() - Get the PBN lock on the duplicate data location for a data_vio from
 *                            the hash_lock the data_vio holds (if there is one).
 * @data_vio: The data_vio to query.
 *
 * Return: The PBN lock on the data_vio's duplicate location.
 */
struct pbn_lock *vdo_get_duplicate_lock(struct data_vio *data_vio)
{
	if (data_vio->hash_lock == NULL)
		return NULL;

	return data_vio->hash_lock->duplicate_lock;
}

/**
 * hash_lock_key() - Return hash_lock's record name as a hash code.
 * @lock: The hash lock.
 *
 * Return: The key to use for the int map.
 */
static inline u64 hash_lock_key(struct hash_lock *lock)
{
	return get_unaligned_le64(&lock->hash.name);
}

/**
 * get_hash_lock_state_name() - Get the string representation of a hash lock state.
 * @state: The hash lock state.
 *
 * Return: The short string representing the state
 */
static const char *get_hash_lock_state_name(enum hash_lock_state state)
{
	/* Catch if a state has been added without updating the name array. */
	BUILD_BUG_ON((VDO_HASH_LOCK_BYPASSING + 1) != ARRAY_SIZE(LOCK_STATE_NAMES));
	return (state < ARRAY_SIZE(LOCK_STATE_NAMES)) ? LOCK_STATE_NAMES[state] : "INVALID";
}

/**
 * assert_hash_lock_agent() - Assert that a data_vio is the agent of its hash lock, and that this
 *                            is being called in the hash zone.
 * @data_vio: The data_vio expected to be the lock agent.
 * @where: A string describing the function making the assertion.
 */
static void assert_hash_lock_agent(struct data_vio *data_vio, const char *where)
{
	/* Not safe to access the agent field except from the hash zone. */
	assert_data_vio_in_hash_zone(data_vio);
	VDO_ASSERT_LOG_ONLY(data_vio == data_vio->hash_lock->agent,
			    "%s must be for the hash lock agent", where);
}

/**
 * set_duplicate_lock() - Set the duplicate lock held by a hash lock. May only be called in the
 *                        physical zone of the PBN lock.
 * @hash_lock: The hash lock to update.
 * @pbn_lock: The PBN read lock to use as the duplicate lock.
 */
static void set_duplicate_lock(struct hash_lock *hash_lock, struct pbn_lock *pbn_lock)
{
	VDO_ASSERT_LOG_ONLY((hash_lock->duplicate_lock == NULL),
			    "hash lock must not already hold a duplicate lock");
	pbn_lock->holder_count += 1;
	hash_lock->duplicate_lock = pbn_lock;
}

/**
 * dequeue_lock_waiter() - Remove the first data_vio from the lock's waitq and return it.
 * @lock: The lock containing the wait queue.
 *
 * Return: The first (oldest) waiter in the queue, or NULL if the queue is empty.
 */
static inline struct data_vio *dequeue_lock_waiter(struct hash_lock *lock)
{
	return vdo_waiter_as_data_vio(vdo_waitq_dequeue_waiter(&lock->waiters));
}

/**
 * set_hash_lock() - Set, change, or clear the hash lock a data_vio is using.
 * @data_vio: The data_vio to update.
 * @new_lock: The hash lock the data_vio is joining.
 *
 * Updates the hash lock (or locks) to reflect the change in membership.
 */
static void set_hash_lock(struct data_vio *data_vio, struct hash_lock *new_lock)
{
	struct hash_lock *old_lock = data_vio->hash_lock;

	if (old_lock != NULL) {
		VDO_ASSERT_LOG_ONLY(data_vio->hash_zone != NULL,
				    "must have a hash zone when holding a hash lock");
		VDO_ASSERT_LOG_ONLY(!list_empty(&data_vio->hash_lock_entry),
				    "must be on a hash lock ring when holding a hash lock");
		VDO_ASSERT_LOG_ONLY(old_lock->reference_count > 0,
				    "hash lock reference must be counted");

		if ((old_lock->state != VDO_HASH_LOCK_BYPASSING) &&
		    (old_lock->state != VDO_HASH_LOCK_UNLOCKING)) {
			/*
			 * If the reference count goes to zero in a non-terminal state, we're most
			 * likely leaking this lock.
			 */
			VDO_ASSERT_LOG_ONLY(old_lock->reference_count > 1,
					    "hash locks should only become unreferenced in a terminal state, not state %s",
					    get_hash_lock_state_name(old_lock->state));
		}

		list_del_init(&data_vio->hash_lock_entry);
		old_lock->reference_count -= 1;

		data_vio->hash_lock = NULL;
	}

	if (new_lock != NULL) {
		/*
		 * Keep all data_vios sharing the lock on a ring since they can complete in any
		 * order and we'll always need a pointer to one to compare data.
		 */
		list_move_tail(&data_vio->hash_lock_entry, &new_lock->duplicate_ring);
		new_lock->reference_count += 1;
		if (new_lock->max_references < new_lock->reference_count)
			new_lock->max_references = new_lock->reference_count;

		data_vio->hash_lock = new_lock;
	}
}

/* There are loops in the state diagram, so some forward decl's are needed. */
static void start_deduping(struct hash_lock *lock, struct data_vio *agent,
			   bool agent_is_done);
static void start_locking(struct hash_lock *lock, struct data_vio *agent);
static void start_writing(struct hash_lock *lock, struct data_vio *agent);
static void unlock_duplicate_pbn(struct vdo_completion *completion);
static void transfer_allocation_lock(struct data_vio *data_vio);

/**
 * exit_hash_lock() - Bottleneck for data_vios that have written or deduplicated and that are no
 *                    longer needed to be an agent for the hash lock.
 * @data_vio: The data_vio to complete and send to be cleaned up.
 */
static void exit_hash_lock(struct data_vio *data_vio)
{
	/* Release the hash lock now, saving a thread transition in cleanup. */
	vdo_release_hash_lock(data_vio);

	/* Complete the data_vio and start the clean-up path to release any locks it still holds. */
	data_vio->vio.completion.callback = complete_data_vio;

	continue_data_vio(data_vio);
}

/**
 * set_duplicate_location() - Set the location of the duplicate block for data_vio, updating the
 *                            is_duplicate and duplicate fields from a zoned_pbn.
 * @data_vio: The data_vio to modify.
 * @source: The location of the duplicate.
 */
static void set_duplicate_location(struct data_vio *data_vio,
				   const struct zoned_pbn source)
{
	data_vio->is_duplicate = (source.pbn != VDO_ZERO_BLOCK);
	data_vio->duplicate = source;
}

/**
 * retire_lock_agent() - Retire the active lock agent, replacing it with the first lock waiter, and
 *                       make the retired agent exit the hash lock.
 * @lock: The hash lock to update.
 *
 * Return: The new lock agent (which will be NULL if there was no waiter)
 */
static struct data_vio *retire_lock_agent(struct hash_lock *lock)
{
	struct data_vio *old_agent = lock->agent;
	struct data_vio *new_agent = dequeue_lock_waiter(lock);

	lock->agent = new_agent;
	exit_hash_lock(old_agent);
	if (new_agent != NULL)
		set_duplicate_location(new_agent, lock->duplicate);
	return new_agent;
}

/**
 * wait_on_hash_lock() - Add a data_vio to the lock's queue of waiters.
 * @lock: The hash lock on which to wait.
 * @data_vio: The data_vio to add to the queue.
 */
static void wait_on_hash_lock(struct hash_lock *lock, struct data_vio *data_vio)
{
	vdo_waitq_enqueue_waiter(&lock->waiters, &data_vio->waiter);

	/*
	 * Make sure the agent doesn't block indefinitely in the packer since it now has at least
	 * one other data_vio waiting on it.
	 */
	if ((lock->state != VDO_HASH_LOCK_WRITING) || !cancel_data_vio_compression(lock->agent))
		return;

	/*
	 * Even though we're waiting, we also have to send ourselves as a one-way message to the
	 * packer to ensure the agent continues executing. This is safe because
	 * cancel_vio_compression() guarantees the agent won't continue executing until this
	 * message arrives in the packer, and because the wait queue link isn't used for sending
	 * the message.
	 */
	data_vio->compression.lock_holder = lock->agent;
	launch_data_vio_packer_callback(data_vio, vdo_remove_lock_holder_from_packer);
}

/**
 * abort_waiter() - waiter_callback_fn function that shunts waiters to write their blocks without
 *                  optimization.
 * @waiter: The data_vio's waiter link.
 * @context: Not used.
 */
static void abort_waiter(struct vdo_waiter *waiter, void *context __always_unused)
{
	write_data_vio(vdo_waiter_as_data_vio(waiter));
}

/**
 * start_bypassing() - Stop using the hash lock.
 * @lock: The hash lock.
 * @agent: The data_vio acting as the agent for the lock.
 *
 * Stops using the hash lock. This is the final transition for hash locks which did not get an
 * error.
 */
static void start_bypassing(struct hash_lock *lock, struct data_vio *agent)
{
	lock->state = VDO_HASH_LOCK_BYPASSING;
	exit_hash_lock(agent);
}

void vdo_clean_failed_hash_lock(struct data_vio *data_vio)
{
	struct hash_lock *lock = data_vio->hash_lock;

	if (lock->state == VDO_HASH_LOCK_BYPASSING) {
		exit_hash_lock(data_vio);
		return;
	}

	if (lock->agent == NULL) {
		lock->agent = data_vio;
	} else if (data_vio != lock->agent) {
		exit_hash_lock(data_vio);
		return;
	}

	lock->state = VDO_HASH_LOCK_BYPASSING;

	/* Ensure we don't attempt to update advice when cleaning up. */
	lock->update_advice = false;

	vdo_waitq_notify_all_waiters(&lock->waiters, abort_waiter, NULL);

	if (lock->duplicate_lock != NULL) {
		/* The agent must reference the duplicate zone to launch it. */
		data_vio->duplicate = lock->duplicate;
		launch_data_vio_duplicate_zone_callback(data_vio, unlock_duplicate_pbn);
		return;
	}

	lock->agent = NULL;
	data_vio->is_duplicate = false;
	exit_hash_lock(data_vio);
}

/**
 * finish_unlocking() - Handle the result of the agent for the lock releasing a read lock on
 *                      duplicate candidate.
 * @completion: The completion of the data_vio acting as the lock's agent.
 *
 * This continuation is registered in unlock_duplicate_pbn().
 */
static void finish_unlocking(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_hash_lock_agent(agent, __func__);

	VDO_ASSERT_LOG_ONLY(lock->duplicate_lock == NULL,
			    "must have released the duplicate lock for the hash lock");

	if (!lock->verified) {
		/*
		 * UNLOCKING -> WRITING transition: The lock we released was on an unverified
		 * block, so it must have been a lock on advice we were verifying, not on a
		 * location that was used for deduplication. Go write (or compress) the block to
		 * get a location to dedupe against.
		 */
		start_writing(lock, agent);
		return;
	}

	/*
	 * With the lock released, the verified duplicate block may already have changed and will
	 * need to be re-verified if a waiter arrived.
	 */
	lock->verified = false;

	if (vdo_waitq_has_waiters(&lock->waiters)) {
		/*
		 * UNLOCKING -> LOCKING transition: A new data_vio entered the hash lock while the
		 * agent was releasing the PBN lock. The current agent exits and the waiter has to
		 * re-lock and re-verify the duplicate location.
		 *
		 * TODO: If we used the current agent to re-acquire the PBN lock we wouldn't need
		 * to re-verify.
		 */
		agent = retire_lock_agent(lock);
		start_locking(lock, agent);
		return;
	}

	/*
	 * UNLOCKING -> BYPASSING transition: The agent is done with the lock and no other
	 * data_vios reference it, so remove it from the lock map and return it to the pool.
	 */
	start_bypassing(lock, agent);
}

/**
 * unlock_duplicate_pbn() - Release a read lock on the PBN of the block that may or may not have
 *                          contained duplicate data.
 * @completion: The completion of the data_vio acting as the lock's agent.
 *
 * This continuation is launched by start_unlocking(), and calls back to finish_unlocking() on the
 * hash zone thread.
 */
static void unlock_duplicate_pbn(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_data_vio_in_duplicate_zone(agent);
	VDO_ASSERT_LOG_ONLY(lock->duplicate_lock != NULL,
			    "must have a duplicate lock to release");

	vdo_release_physical_zone_pbn_lock(agent->duplicate.zone, agent->duplicate.pbn,
					   vdo_forget(lock->duplicate_lock));
	if (lock->state == VDO_HASH_LOCK_BYPASSING) {
		complete_data_vio(completion);
		return;
	}

	launch_data_vio_hash_zone_callback(agent, finish_unlocking);
}

/**
 * start_unlocking() - Release a read lock on the PBN of the block that may or may not have
 *                     contained duplicate data.
 * @lock: The hash lock.
 * @agent: The data_vio currently acting as the agent for the lock.
 */
static void start_unlocking(struct hash_lock *lock, struct data_vio *agent)
{
	lock->state = VDO_HASH_LOCK_UNLOCKING;
	launch_data_vio_duplicate_zone_callback(agent, unlock_duplicate_pbn);
}

static void release_context(struct dedupe_context *context)
{
	struct hash_zone *zone = context->zone;

	WRITE_ONCE(zone->active, zone->active - 1);
	list_move(&context->list_entry, &zone->available);
}

static void process_update_result(struct data_vio *agent)
{
	struct dedupe_context *context = agent->dedupe_context;

	if ((context == NULL) ||
	    !change_context_state(context, DEDUPE_CONTEXT_COMPLETE, DEDUPE_CONTEXT_IDLE))
		return;

	agent->dedupe_context = NULL;
	release_context(context);
}

/**
 * finish_updating() - Process the result of a UDS update performed by the agent for the lock.
 * @completion: The completion of the data_vio that performed the update
 *
 * This continuation is registered in start_querying().
 */
static void finish_updating(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_hash_lock_agent(agent, __func__);

	process_update_result(agent);

	/*
	 * UDS was updated successfully, so don't update again unless the duplicate location
	 * changes due to rollover.
	 */
	lock->update_advice = false;

	if (vdo_waitq_has_waiters(&lock->waiters)) {
		/*
		 * UPDATING -> DEDUPING transition: A new data_vio arrived during the UDS update.
		 * Send it on the verified dedupe path. The agent is done with the lock, but the
		 * lock may still need to use it to clean up after rollover.
		 */
		start_deduping(lock, agent, true);
		return;
	}

	if (lock->duplicate_lock != NULL) {
		/*
		 * UPDATING -> UNLOCKING transition: No one is waiting to dedupe, but we hold a
		 * duplicate PBN lock, so go release it.
		 */
		start_unlocking(lock, agent);
		return;
	}

	/*
	 * UPDATING -> BYPASSING transition: No one is waiting to dedupe and there's no lock to
	 * release.
	 */
	start_bypassing(lock, agent);
}

static void query_index(struct data_vio *data_vio, enum uds_request_type operation);

/**
 * start_updating() - Continue deduplication with the last step, updating UDS with the location of
 *                    the duplicate that should be returned as advice in the future.
 * @lock: The hash lock.
 * @agent: The data_vio currently acting as the agent for the lock.
 */
static void start_updating(struct hash_lock *lock, struct data_vio *agent)
{
	lock->state = VDO_HASH_LOCK_UPDATING;

	VDO_ASSERT_LOG_ONLY(lock->verified, "new advice should have been verified");
	VDO_ASSERT_LOG_ONLY(lock->update_advice, "should only update advice if needed");

	agent->last_async_operation = VIO_ASYNC_OP_UPDATE_DEDUPE_INDEX;
	set_data_vio_hash_zone_callback(agent, finish_updating);
	query_index(agent, UDS_UPDATE);
}

/**
 * finish_deduping() - Handle a data_vio that has finished deduplicating against the block locked
 *                     by the hash lock.
 * @lock: The hash lock.
 * @data_vio: The lock holder that has finished deduplicating.
 *
 * If there are other data_vios still sharing the lock, this will just release the data_vio's share
 * of the lock and finish processing the data_vio. If this is the last data_vio holding the lock,
 * this makes the data_vio the lock agent and uses it to advance the state of the lock so it can
 * eventually be released.
 */
static void finish_deduping(struct hash_lock *lock, struct data_vio *data_vio)
{
	struct data_vio *agent = data_vio;

	VDO_ASSERT_LOG_ONLY(lock->agent == NULL, "shouldn't have an agent in DEDUPING");
	VDO_ASSERT_LOG_ONLY(!vdo_waitq_has_waiters(&lock->waiters),
			    "shouldn't have any lock waiters in DEDUPING");

	/* Just release the lock reference if other data_vios are still deduping. */
	if (lock->reference_count > 1) {
		exit_hash_lock(data_vio);
		return;
	}

	/* The hash lock must have an agent for all other lock states. */
	lock->agent = agent;
	if (lock->update_advice) {
		/*
		 * DEDUPING -> UPDATING transition: The location of the duplicate block changed
		 * since the initial UDS query because of compression, rollover, or because the
		 * query agent didn't have an allocation. The UDS update was delayed in case there
		 * was another change in location, but with only this data_vio using the hash lock,
		 * it's time to update the advice.
		 */
		start_updating(lock, agent);
	} else {
		/*
		 * DEDUPING -> UNLOCKING transition: Release the PBN read lock on the duplicate
		 * location so the hash lock itself can be released (contingent on no new data_vios
		 * arriving in the lock before the agent returns).
		 */
		start_unlocking(lock, agent);
	}
}

/**
 * acquire_lock() - Get the lock for a record name.
 * @zone: The zone responsible for the hash.
 * @hash: The hash to lock.
 * @replace_lock: If non-NULL, the lock already registered for the hash which should be replaced by
 *                the new lock.
 * @lock_ptr: A pointer to receive the hash lock.
 *
 * Gets the lock for the hash (record name) of the data in a data_vio, or if one does not exist (or
 * if we are explicitly rolling over), initialize a new lock for the hash and register it in the
 * zone. This must only be called in the correct thread for the zone.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check acquire_lock(struct hash_zone *zone,
				     const struct uds_record_name *hash,
				     struct hash_lock *replace_lock,
				     struct hash_lock **lock_ptr)
{
	struct hash_lock *lock, *new_lock;
	int result;

	/*
	 * Borrow and prepare a lock from the pool so we don't have to do two int_map accesses
	 * in the common case of no lock contention.
	 */
	result = VDO_ASSERT(!list_empty(&zone->lock_pool),
			    "never need to wait for a free hash lock");
	if (result != VDO_SUCCESS)
		return result;

	new_lock = list_entry(zone->lock_pool.prev, struct hash_lock, pool_node);
	list_del_init(&new_lock->pool_node);

	/*
	 * Fill in the hash of the new lock so we can map it, since we have to use the hash as the
	 * map key.
	 */
	new_lock->hash = *hash;

	result = vdo_int_map_put(zone->hash_lock_map, hash_lock_key(new_lock),
				 new_lock, (replace_lock != NULL), (void **) &lock);
	if (result != VDO_SUCCESS) {
		return_hash_lock_to_pool(zone, vdo_forget(new_lock));
		return result;
	}

	if (replace_lock != NULL) {
		/* On mismatch put the old lock back and return a severe error */
		VDO_ASSERT_LOG_ONLY(lock == replace_lock,
				    "old lock must have been in the lock map");
		/* TODO: Check earlier and bail out? */
		VDO_ASSERT_LOG_ONLY(replace_lock->registered,
				    "old lock must have been marked registered");
		replace_lock->registered = false;
	}

	if (lock == replace_lock) {
		lock = new_lock;
		lock->registered = true;
	} else {
		/* There's already a lock for the hash, so we don't need the borrowed lock. */
		return_hash_lock_to_pool(zone, vdo_forget(new_lock));
	}

	*lock_ptr = lock;
	return VDO_SUCCESS;
}

/**
 * enter_forked_lock() - Bind the data_vio to a new hash lock.
 *
 * Implements waiter_callback_fn. Binds the data_vio that was waiting to a new hash lock and waits
 * on that lock.
 */
static void enter_forked_lock(struct vdo_waiter *waiter, void *context)
{
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);
	struct hash_lock *new_lock = context;

	set_hash_lock(data_vio, new_lock);
	wait_on_hash_lock(new_lock, data_vio);
}

/**
 * fork_hash_lock() - Fork a hash lock because it has run out of increments on the duplicate PBN.
 * @old_lock: The hash lock to fork.
 * @new_agent: The data_vio that will be the agent for the new lock.
 *
 * Transfers the new agent and any lock waiters to a new hash lock instance which takes the place
 * of the old lock in the lock map. The old lock remains active, but will not update advice.
 */
static void fork_hash_lock(struct hash_lock *old_lock, struct data_vio *new_agent)
{
	struct hash_lock *new_lock;
	int result;

	result = acquire_lock(new_agent->hash_zone, &new_agent->record_name, old_lock,
			      &new_lock);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(new_agent, result);
		return;
	}

	/*
	 * Only one of the two locks should update UDS. The old lock is out of references, so it
	 * would be poor dedupe advice in the short term.
	 */
	old_lock->update_advice = false;
	new_lock->update_advice = true;

	set_hash_lock(new_agent, new_lock);
	new_lock->agent = new_agent;

	vdo_waitq_notify_all_waiters(&old_lock->waiters, enter_forked_lock, new_lock);

	new_agent->is_duplicate = false;
	start_writing(new_lock, new_agent);
}

/**
 * launch_dedupe() - Reserve a reference count increment for a data_vio and launch it on the dedupe
 *                   path.
 * @lock: The hash lock.
 * @data_vio: The data_vio to deduplicate using the hash lock.
 * @has_claim: true if the data_vio already has claimed an increment from the duplicate lock.
 *
 * If no increments are available, this will roll over to a new hash lock and launch the data_vio
 * as the writing agent for that lock.
 */
static void launch_dedupe(struct hash_lock *lock, struct data_vio *data_vio,
			  bool has_claim)
{
	if (!has_claim && !vdo_claim_pbn_lock_increment(lock->duplicate_lock)) {
		/* Out of increments, so must roll over to a new lock. */
		fork_hash_lock(lock, data_vio);
		return;
	}

	/* Deduplicate against the lock's verified location. */
	set_duplicate_location(data_vio, lock->duplicate);
	data_vio->new_mapped = data_vio->duplicate;
	update_metadata_for_data_vio_write(data_vio, lock->duplicate_lock);
}

/**
 * start_deduping() - Enter the hash lock state where data_vios deduplicate in parallel against a
 *                    true copy of their data on disk.
 * @lock: The hash lock.
 * @agent: The data_vio acting as the agent for the lock.
 * @agent_is_done: true only if the agent has already written or deduplicated against its data.
 *
 * If the agent itself needs to deduplicate, an increment for it must already have been claimed
 * from the duplicate lock, ensuring the hash lock will still have a data_vio holding it.
 */
static void start_deduping(struct hash_lock *lock, struct data_vio *agent,
			   bool agent_is_done)
{
	lock->state = VDO_HASH_LOCK_DEDUPING;

	/*
	 * We don't take the downgraded allocation lock from the agent unless we actually need to
	 * deduplicate against it.
	 */
	if (lock->duplicate_lock == NULL) {
		VDO_ASSERT_LOG_ONLY(!vdo_is_state_compressed(agent->new_mapped.state),
				    "compression must have shared a lock");
		VDO_ASSERT_LOG_ONLY(agent_is_done,
				    "agent must have written the new duplicate");
		transfer_allocation_lock(agent);
	}

	VDO_ASSERT_LOG_ONLY(vdo_is_pbn_read_lock(lock->duplicate_lock),
			    "duplicate_lock must be a PBN read lock");

	/*
	 * This state is not like any of the other states. There is no designated agent--the agent
	 * transitioning to this state and all the waiters will be launched to deduplicate in
	 * parallel.
	 */
	lock->agent = NULL;

	/*
	 * Launch the agent (if not already deduplicated) and as many lock waiters as we have
	 * available increments for on the dedupe path. If we run out of increments, rollover will
	 * be triggered and the remaining waiters will be transferred to the new lock.
	 */
	if (!agent_is_done) {
		launch_dedupe(lock, agent, true);
		agent = NULL;
	}
	while (vdo_waitq_has_waiters(&lock->waiters))
		launch_dedupe(lock, dequeue_lock_waiter(lock), false);

	if (agent_is_done) {
		/*
		 * In the degenerate case where all the waiters rolled over to a new lock, this
		 * will continue to use the old agent to clean up this lock, and otherwise it just
		 * lets the agent exit the lock.
		 */
		finish_deduping(lock, agent);
	}
}

/**
 * increment_stat() - Increment a statistic counter in a non-atomic yet thread-safe manner.
 * @stat: The statistic field to increment.
 */
static inline void increment_stat(u64 *stat)
{
	/*
	 * Must only be mutated on the hash zone thread. Prevents any compiler shenanigans from
	 * affecting other threads reading stats.
	 */
	WRITE_ONCE(*stat, *stat + 1);
}

/**
 * finish_verifying() - Handle the result of the agent for the lock comparing its data to the
 *                      duplicate candidate.
 * @completion: The completion of the data_vio used to verify dedupe
 *
 * This continuation is registered in start_verifying().
 */
static void finish_verifying(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_hash_lock_agent(agent, __func__);

	lock->verified = agent->is_duplicate;

	/*
	 * Only count the result of the initial verification of the advice as valid or stale, and
	 * not any re-verifications due to PBN lock releases.
	 */
	if (!lock->verify_counted) {
		lock->verify_counted = true;
		if (lock->verified)
			increment_stat(&agent->hash_zone->statistics.dedupe_advice_valid);
		else
			increment_stat(&agent->hash_zone->statistics.dedupe_advice_stale);
	}

	/*
	 * Even if the block is a verified duplicate, we can't start to deduplicate unless we can
	 * claim a reference count increment for the agent.
	 */
	if (lock->verified && !vdo_claim_pbn_lock_increment(lock->duplicate_lock)) {
		agent->is_duplicate = false;
		lock->verified = false;
	}

	if (lock->verified) {
		/*
		 * VERIFYING -> DEDUPING transition: The advice is for a true duplicate, so start
		 * deduplicating against it, if references are available.
		 */
		start_deduping(lock, agent, false);
	} else {
		/*
		 * VERIFYING -> UNLOCKING transition: Either the verify failed or we'd try to
		 * dedupe and roll over immediately, which would fail because it would leave the
		 * lock without an agent to release the PBN lock. In both cases, the data will have
		 * to be written or compressed, but first the advice PBN must be unlocked by the
		 * VERIFYING agent.
		 */
		lock->update_advice = true;
		start_unlocking(lock, agent);
	}
}

static bool blocks_equal(char *block1, char *block2)
{
	int i;

	for (i = 0; i < VDO_BLOCK_SIZE; i += sizeof(u64)) {
		if (*((u64 *) &block1[i]) != *((u64 *) &block2[i]))
			return false;
	}

	return true;
}

static void verify_callback(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);

	agent->is_duplicate = blocks_equal(agent->vio.data, agent->scratch_block);
	launch_data_vio_hash_zone_callback(agent, finish_verifying);
}

static void uncompress_and_verify(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	int result;

	result = uncompress_data_vio(agent, agent->duplicate.state,
				     agent->scratch_block);
	if (result == VDO_SUCCESS) {
		verify_callback(completion);
		return;
	}

	agent->is_duplicate = false;
	launch_data_vio_hash_zone_callback(agent, finish_verifying);
}

static void verify_endio(struct bio *bio)
{
	struct data_vio *agent = vio_as_data_vio(bio->bi_private);
	int result = blk_status_to_errno(bio->bi_status);

	vdo_count_completed_bios(bio);
	if (result != VDO_SUCCESS) {
		agent->is_duplicate = false;
		launch_data_vio_hash_zone_callback(agent, finish_verifying);
		return;
	}

	if (vdo_is_state_compressed(agent->duplicate.state)) {
		launch_data_vio_cpu_callback(agent, uncompress_and_verify,
					     CPU_Q_COMPRESS_BLOCK_PRIORITY);
		return;
	}

	launch_data_vio_cpu_callback(agent, verify_callback,
				     CPU_Q_COMPLETE_READ_PRIORITY);
}

/**
 * start_verifying() - Begin the data verification phase.
 * @lock: The hash lock (must be LOCKING).
 * @agent: The data_vio to use to read and compare candidate data.
 *
 * Continue the deduplication path for a hash lock by using the agent to read (and possibly
 * decompress) the data at the candidate duplicate location, comparing it to the data in the agent
 * to verify that the candidate is identical to all the data_vios sharing the hash. If so, it can
 * be deduplicated against, otherwise a data_vio allocation will have to be written to and used for
 * dedupe.
 */
static void start_verifying(struct hash_lock *lock, struct data_vio *agent)
{
	int result;
	struct vio *vio = &agent->vio;
	char *buffer = (vdo_is_state_compressed(agent->duplicate.state) ?
			(char *) agent->compression.block :
			agent->scratch_block);

	lock->state = VDO_HASH_LOCK_VERIFYING;
	VDO_ASSERT_LOG_ONLY(!lock->verified, "hash lock only verifies advice once");

	agent->last_async_operation = VIO_ASYNC_OP_VERIFY_DUPLICATION;
	result = vio_reset_bio(vio, buffer, verify_endio, REQ_OP_READ,
			       agent->duplicate.pbn);
	if (result != VDO_SUCCESS) {
		set_data_vio_hash_zone_callback(agent, finish_verifying);
		continue_data_vio_with_error(agent, result);
		return;
	}

	set_data_vio_bio_zone_callback(agent, vdo_submit_vio);
	vdo_launch_completion_with_priority(&vio->completion, BIO_Q_VERIFY_PRIORITY);
}

/**
 * finish_locking() - Handle the result of the agent for the lock attempting to obtain a PBN read
 *                    lock on the candidate duplicate block.
 * @completion: The completion of the data_vio that attempted to get the read lock.
 *
 * This continuation is registered in lock_duplicate_pbn().
 */
static void finish_locking(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_hash_lock_agent(agent, __func__);

	if (!agent->is_duplicate) {
		VDO_ASSERT_LOG_ONLY(lock->duplicate_lock == NULL,
				    "must not hold duplicate_lock if not flagged as a duplicate");
		/*
		 * LOCKING -> WRITING transition: The advice block is being modified or has no
		 * available references, so try to write or compress the data, remembering to
		 * update UDS later with the new advice.
		 */
		increment_stat(&agent->hash_zone->statistics.dedupe_advice_stale);
		lock->update_advice = true;
		start_writing(lock, agent);
		return;
	}

	VDO_ASSERT_LOG_ONLY(lock->duplicate_lock != NULL,
			    "must hold duplicate_lock if flagged as a duplicate");

	if (!lock->verified) {
		/*
		 * LOCKING -> VERIFYING transition: Continue on the unverified dedupe path, reading
		 * the candidate duplicate and comparing it to the agent's data to decide whether
		 * it is a true duplicate or stale advice.
		 */
		start_verifying(lock, agent);
		return;
	}

	if (!vdo_claim_pbn_lock_increment(lock->duplicate_lock)) {
		/*
		 * LOCKING -> UNLOCKING transition: The verified block was re-locked, but has no
		 * available increments left. Must first release the useless PBN read lock before
		 * rolling over to a new copy of the block.
		 */
		agent->is_duplicate = false;
		lock->verified = false;
		lock->update_advice = true;
		start_unlocking(lock, agent);
		return;
	}

	/*
	 * LOCKING -> DEDUPING transition: Continue on the verified dedupe path, deduplicating
	 * against a location that was previously verified or written to.
	 */
	start_deduping(lock, agent, false);
}

static bool acquire_provisional_reference(struct data_vio *agent, struct pbn_lock *lock,
					  struct slab_depot *depot)
{
	/* Ensure that the newly-locked block is referenced. */
	struct vdo_slab *slab = vdo_get_slab(depot, agent->duplicate.pbn);
	int result = vdo_acquire_provisional_reference(slab, agent->duplicate.pbn, lock);

	if (result == VDO_SUCCESS)
		return true;

	vdo_log_warning_strerror(result,
				 "Error acquiring provisional reference for dedupe candidate; aborting dedupe");
	agent->is_duplicate = false;
	vdo_release_physical_zone_pbn_lock(agent->duplicate.zone,
					   agent->duplicate.pbn, lock);
	continue_data_vio_with_error(agent, result);
	return false;
}

/**
 * lock_duplicate_pbn() - Acquire a read lock on the PBN of the block containing candidate
 *                        duplicate data (compressed or uncompressed).
 * @completion: The completion of the data_vio attempting to acquire the physical block lock on
 *              behalf of its hash lock.
 *
 * If the PBN is already locked for writing, the lock attempt is abandoned and is_duplicate will be
 * cleared before calling back. This continuation is launched from start_locking(), and calls back
 * to finish_locking() on the hash zone thread.
 */
static void lock_duplicate_pbn(struct vdo_completion *completion)
{
	unsigned int increment_limit;
	struct pbn_lock *lock;
	int result;

	struct data_vio *agent = as_data_vio(completion);
	struct slab_depot *depot = vdo_from_data_vio(agent)->depot;
	struct physical_zone *zone = agent->duplicate.zone;

	assert_data_vio_in_duplicate_zone(agent);

	set_data_vio_hash_zone_callback(agent, finish_locking);

	/*
	 * While in the zone that owns it, find out how many additional references can be made to
	 * the block if it turns out to truly be a duplicate.
	 */
	increment_limit = vdo_get_increment_limit(depot, agent->duplicate.pbn);
	if (increment_limit == 0) {
		/*
		 * We could deduplicate against it later if a reference happened to be released
		 * during verification, but it's probably better to bail out now.
		 */
		agent->is_duplicate = false;
		continue_data_vio(agent);
		return;
	}

	result = vdo_attempt_physical_zone_pbn_lock(zone, agent->duplicate.pbn,
						    VIO_READ_LOCK, &lock);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(agent, result);
		return;
	}

	if (!vdo_is_pbn_read_lock(lock)) {
		/*
		 * There are three cases of write locks: uncompressed data block writes, compressed
		 * (packed) block writes, and block map page writes. In all three cases, we give up
		 * on trying to verify the advice and don't bother to try deduplicate against the
		 * data in the write lock holder.
		 *
		 * 1) We don't ever want to try to deduplicate against a block map page.
		 *
		 * 2a) It's very unlikely we'd deduplicate against an entire packed block, both
		 * because of the chance of matching it, and because we don't record advice for it,
		 * but for the uncompressed representation of all the fragments it contains. The
		 * only way we'd be getting lock contention is if we've written the same
		 * representation coincidentally before, had it become unreferenced, and it just
		 * happened to be packed together from compressed writes when we go to verify the
		 * lucky advice. Giving up is a minuscule loss of potential dedupe.
		 *
		 * 2b) If the advice is for a slot of a compressed block, it's about to get
		 * smashed, and the write smashing it cannot contain our data--it would have to be
		 * writing on behalf of our hash lock, but that's impossible since we're the lock
		 * agent.
		 *
		 * 3a) If the lock is held by a data_vio with different data, the advice is already
		 * stale or is about to become stale.
		 *
		 * 3b) If the lock is held by a data_vio that matches us, we may as well either
		 * write it ourselves (or reference the copy we already wrote) instead of
		 * potentially having many duplicates wait for the lock holder to write, journal,
		 * hash, and finally arrive in the hash lock. We lose a chance to avoid a UDS
		 * update in the very rare case of advice for a free block that just happened to be
		 * allocated to a data_vio with the same hash. There's also a chance to save on a
		 * block write, at the cost of a block verify. Saving on a full block compare in
		 * all stale advice cases almost certainly outweighs saving a UDS update and
		 * trading a write for a read in a lucky case where advice would have been saved
		 * from becoming stale.
		 */
		agent->is_duplicate = false;
		continue_data_vio(agent);
		return;
	}

	if (lock->holder_count == 0) {
		if (!acquire_provisional_reference(agent, lock, depot))
			return;

		/*
		 * The increment limit we grabbed earlier is still valid. The lock now holds the
		 * rights to acquire all those references. Those rights will be claimed by hash
		 * locks sharing this read lock.
		 */
		lock->increment_limit = increment_limit;
	}

	/*
	 * We've successfully acquired a read lock on behalf of the hash lock, so mark it as such.
	 */
	set_duplicate_lock(agent->hash_lock, lock);

	/*
	 * TODO: Optimization: We could directly launch the block verify, then switch to a hash
	 * thread.
	 */
	continue_data_vio(agent);
}

/**
 * start_locking() - Continue deduplication for a hash lock that has obtained valid advice of a
 *                   potential duplicate through its agent.
 * @lock: The hash lock (currently must be QUERYING).
 * @agent: The data_vio bearing the dedupe advice.
 */
static void start_locking(struct hash_lock *lock, struct data_vio *agent)
{
	VDO_ASSERT_LOG_ONLY(lock->duplicate_lock == NULL,
			    "must not acquire a duplicate lock when already holding it");

	lock->state = VDO_HASH_LOCK_LOCKING;

	/*
	 * TODO: Optimization: If we arrange to continue on the duplicate zone thread when
	 * accepting the advice, and don't explicitly change lock states (or use an agent-local
	 * state, or an atomic), we can avoid a thread transition here.
	 */
	agent->last_async_operation = VIO_ASYNC_OP_LOCK_DUPLICATE_PBN;
	launch_data_vio_duplicate_zone_callback(agent, lock_duplicate_pbn);
}

/**
 * finish_writing() - Re-entry point for the lock agent after it has finished writing or
 *                    compressing its copy of the data block.
 * @lock: The hash lock, which must be in state WRITING.
 * @agent: The data_vio that wrote its data for the lock.
 *
 * The agent will never need to dedupe against anything, so it's done with the lock, but the lock
 * may not be finished with it, as a UDS update might still be needed.
 *
 * If there are other lock holders, the agent will hand the job to one of them and exit, leaving
 * the lock to deduplicate against the just-written block. If there are no other lock holders, the
 * agent either exits (and later tears down the hash lock), or it remains the agent and updates
 * UDS.
 */
static void finish_writing(struct hash_lock *lock, struct data_vio *agent)
{
	/*
	 * Dedupe against the data block or compressed block slot the agent wrote. Since we know
	 * the write succeeded, there's no need to verify it.
	 */
	lock->duplicate = agent->new_mapped;
	lock->verified = true;

	if (vdo_is_state_compressed(lock->duplicate.state) && lock->registered) {
		/*
		 * Compression means the location we gave in the UDS query is not the location
		 * we're using to deduplicate.
		 */
		lock->update_advice = true;
	}

	/* If there are any waiters, we need to start deduping them. */
	if (vdo_waitq_has_waiters(&lock->waiters)) {
		/*
		 * WRITING -> DEDUPING transition: an asynchronously-written block failed to
		 * compress, so the PBN lock on the written copy was already transferred. The agent
		 * is done with the lock, but the lock may still need to use it to clean up after
		 * rollover.
		 */
		start_deduping(lock, agent, true);
		return;
	}

	/*
	 * There are no waiters and the agent has successfully written, so take a step towards
	 * being able to release the hash lock (or just release it).
	 */
	if (lock->update_advice) {
		/*
		 * WRITING -> UPDATING transition: There's no waiter and a UDS update is needed, so
		 * retain the WRITING agent and use it to launch the update. The happens on
		 * compression, rollover, or the QUERYING agent not having an allocation.
		 */
		start_updating(lock, agent);
	} else if (lock->duplicate_lock != NULL) {
		/*
		 * WRITING -> UNLOCKING transition: There's no waiter and no update needed, but the
		 * compressed write gave us a shared duplicate lock that we must release.
		 */
		set_duplicate_location(agent, lock->duplicate);
		start_unlocking(lock, agent);
	} else {
		/*
		 * WRITING -> BYPASSING transition: There's no waiter, no update needed, and no
		 * duplicate lock held, so both the agent and lock have no more work to do. The
		 * agent will release its allocation lock in cleanup.
		 */
		start_bypassing(lock, agent);
	}
}

/**
 * select_writing_agent() - Search through the lock waiters for a data_vio that has an allocation.
 * @lock: The hash lock to modify.
 *
 * If an allocation is found, swap agents, put the old agent at the head of the wait queue, then
 * return the new agent. Otherwise, just return the current agent.
 */
static struct data_vio *select_writing_agent(struct hash_lock *lock)
{
	struct vdo_wait_queue temp_queue;
	struct data_vio *data_vio;

	vdo_waitq_init(&temp_queue);

	/*
	 * Move waiters to the temp queue one-by-one until we find an allocation. Not ideal to
	 * search, but it only happens when nearly out of space.
	 */
	while (((data_vio = dequeue_lock_waiter(lock)) != NULL) &&
	       !data_vio_has_allocation(data_vio)) {
		/* Use the lower-level enqueue since we're just moving waiters around. */
		vdo_waitq_enqueue_waiter(&temp_queue, &data_vio->waiter);
	}

	if (data_vio != NULL) {
		/*
		 * Move the rest of the waiters over to the temp queue, preserving the order they
		 * arrived at the lock.
		 */
		vdo_waitq_transfer_all_waiters(&lock->waiters, &temp_queue);

		/*
		 * The current agent is being replaced and will have to wait to dedupe; make it the
		 * first waiter since it was the first to reach the lock.
		 */
		vdo_waitq_enqueue_waiter(&lock->waiters, &lock->agent->waiter);
		lock->agent = data_vio;
	} else {
		/* No one has an allocation, so keep the current agent. */
		data_vio = lock->agent;
	}

	/* Swap all the waiters back onto the lock's queue. */
	vdo_waitq_transfer_all_waiters(&temp_queue, &lock->waiters);
	return data_vio;
}

/**
 * start_writing() - Begin the non-duplicate write path.
 * @lock: The hash lock (currently must be QUERYING).
 * @agent: The data_vio currently acting as the agent for the lock.
 *
 * Begins the non-duplicate write path for a hash lock that had no advice, selecting a data_vio
 * with an allocation as a new agent, if necessary, then resuming the agent on the data_vio write
 * path.
 */
static void start_writing(struct hash_lock *lock, struct data_vio *agent)
{
	lock->state = VDO_HASH_LOCK_WRITING;

	/*
	 * The agent might not have received an allocation and so can't be used for writing, but
	 * it's entirely possible that one of the waiters did.
	 */
	if (!data_vio_has_allocation(agent)) {
		agent = select_writing_agent(lock);
		/* If none of the waiters had an allocation, the writes all have to fail. */
		if (!data_vio_has_allocation(agent)) {
			/*
			 * TODO: Should we keep a variant of BYPASSING that causes new arrivals to
			 * fail immediately if they don't have an allocation? It might be possible
			 * that on some path there would be non-waiters still referencing the lock,
			 * so it would remain in the map as everything is currently spelled, even
			 * if the agent and all waiters release.
			 */
			continue_data_vio_with_error(agent, VDO_NO_SPACE);
			return;
		}
	}

	/*
	 * If the agent compresses, it might wait indefinitely in the packer, which would be bad if
	 * there are any other data_vios waiting.
	 */
	if (vdo_waitq_has_waiters(&lock->waiters))
		cancel_data_vio_compression(agent);

	/*
	 * Send the agent to the compress/pack/write path in vioWrite. If it succeeds, it will
	 * return to the hash lock via vdo_continue_hash_lock() and call finish_writing().
	 */
	launch_compress_data_vio(agent);
}

/*
 * Decode VDO duplicate advice from the old_metadata field of a UDS request.
 * Returns true if valid advice was found and decoded
 */
static bool decode_uds_advice(struct dedupe_context *context)
{
	const struct uds_request *request = &context->request;
	struct data_vio *data_vio = context->requestor;
	size_t offset = 0;
	const struct uds_record_data *encoding = &request->old_metadata;
	struct vdo *vdo = vdo_from_data_vio(data_vio);
	struct zoned_pbn *advice = &data_vio->duplicate;
	u8 version;
	int result;

	if ((request->status != UDS_SUCCESS) || !request->found)
		return false;

	version = encoding->data[offset++];
	if (version != UDS_ADVICE_VERSION) {
		vdo_log_error("invalid UDS advice version code %u", version);
		return false;
	}

	advice->state = encoding->data[offset++];
	advice->pbn = get_unaligned_le64(&encoding->data[offset]);
	offset += sizeof(u64);
	BUG_ON(offset != UDS_ADVICE_SIZE);

	/* Don't use advice that's clearly meaningless. */
	if ((advice->state == VDO_MAPPING_STATE_UNMAPPED) || (advice->pbn == VDO_ZERO_BLOCK)) {
		vdo_log_debug("Invalid advice from deduplication server: pbn %llu, state %u. Giving up on deduplication of logical block %llu",
			      (unsigned long long) advice->pbn, advice->state,
			      (unsigned long long) data_vio->logical.lbn);
		atomic64_inc(&vdo->stats.invalid_advice_pbn_count);
		return false;
	}

	result = vdo_get_physical_zone(vdo, advice->pbn, &advice->zone);
	if ((result != VDO_SUCCESS) || (advice->zone == NULL)) {
		vdo_log_debug("Invalid physical block number from deduplication server: %llu, giving up on deduplication of logical block %llu",
			      (unsigned long long) advice->pbn,
			      (unsigned long long) data_vio->logical.lbn);
		atomic64_inc(&vdo->stats.invalid_advice_pbn_count);
		return false;
	}

	return true;
}

static void process_query_result(struct data_vio *agent)
{
	struct dedupe_context *context = agent->dedupe_context;

	if (context == NULL)
		return;

	if (change_context_state(context, DEDUPE_CONTEXT_COMPLETE, DEDUPE_CONTEXT_IDLE)) {
		agent->is_duplicate = decode_uds_advice(context);
		agent->dedupe_context = NULL;
		release_context(context);
	}
}

/**
 * finish_querying() - Process the result of a UDS query performed by the agent for the lock.
 * @completion: The completion of the data_vio that performed the query.
 *
 * This continuation is registered in start_querying().
 */
static void finish_querying(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct hash_lock *lock = agent->hash_lock;

	assert_hash_lock_agent(agent, __func__);

	process_query_result(agent);

	if (agent->is_duplicate) {
		lock->duplicate = agent->duplicate;
		/*
		 * QUERYING -> LOCKING transition: Valid advice was obtained from UDS. Use the
		 * QUERYING agent to start the hash lock on the unverified dedupe path, verifying
		 * that the advice can be used.
		 */
		start_locking(lock, agent);
	} else {
		/*
		 * The agent will be used as the duplicate if has an allocation; if it does, that
		 * location was posted to UDS, so no update will be needed.
		 */
		lock->update_advice = !data_vio_has_allocation(agent);
		/*
		 * QUERYING -> WRITING transition: There was no advice or the advice wasn't valid,
		 * so try to write or compress the data.
		 */
		start_writing(lock, agent);
	}
}

/**
 * start_querying() - Start deduplication for a hash lock.
 * @lock: The initialized hash lock.
 * @data_vio: The data_vio that has just obtained the new lock.
 *
 * Starts deduplication for a hash lock that has finished initializing by making the data_vio that
 * requested it the agent, entering the QUERYING state, and using the agent to perform the UDS
 * query on behalf of the lock.
 */
static void start_querying(struct hash_lock *lock, struct data_vio *data_vio)
{
	lock->agent = data_vio;
	lock->state = VDO_HASH_LOCK_QUERYING;
	data_vio->last_async_operation = VIO_ASYNC_OP_CHECK_FOR_DUPLICATION;
	set_data_vio_hash_zone_callback(data_vio, finish_querying);
	query_index(data_vio,
		    (data_vio_has_allocation(data_vio) ? UDS_POST : UDS_QUERY));
}

/**
 * report_bogus_lock_state() - Complain that a data_vio has entered a hash_lock that is in an
 *                             unimplemented or unusable state and continue the data_vio with an
 *                             error.
 * @lock: The hash lock.
 * @data_vio: The data_vio attempting to enter the lock.
 */
static void report_bogus_lock_state(struct hash_lock *lock, struct data_vio *data_vio)
{
	VDO_ASSERT_LOG_ONLY(false, "hash lock must not be in unimplemented state %s",
			    get_hash_lock_state_name(lock->state));
	continue_data_vio_with_error(data_vio, VDO_LOCK_ERROR);
}

/**
 * vdo_continue_hash_lock() - Continue the processing state after writing, compressing, or
 *                            deduplicating.
 * @data_vio: The data_vio to continue processing in its hash lock.
 *
 * Asynchronously continue processing a data_vio in its hash lock after it has finished writing,
 * compressing, or deduplicating, so it can share the result with any data_vios waiting in the hash
 * lock, or update the UDS index, or simply release its share of the lock.
 *
 * Context: This must only be called in the correct thread for the hash zone.
 */
void vdo_continue_hash_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct hash_lock *lock = data_vio->hash_lock;

	switch (lock->state) {
	case VDO_HASH_LOCK_WRITING:
		VDO_ASSERT_LOG_ONLY(data_vio == lock->agent,
				    "only the lock agent may continue the lock");
		finish_writing(lock, data_vio);
		break;

	case VDO_HASH_LOCK_DEDUPING:
		finish_deduping(lock, data_vio);
		break;

	case VDO_HASH_LOCK_BYPASSING:
		/* This data_vio has finished the write path and the lock doesn't need it. */
		exit_hash_lock(data_vio);
		break;

	case VDO_HASH_LOCK_INITIALIZING:
	case VDO_HASH_LOCK_QUERYING:
	case VDO_HASH_LOCK_UPDATING:
	case VDO_HASH_LOCK_LOCKING:
	case VDO_HASH_LOCK_VERIFYING:
	case VDO_HASH_LOCK_UNLOCKING:
		/* A lock in this state should never be re-entered. */
		report_bogus_lock_state(lock, data_vio);
		break;

	default:
		report_bogus_lock_state(lock, data_vio);
	}
}

/**
 * is_hash_collision() - Check to see if a hash collision has occurred.
 * @lock: The lock to check.
 * @candidate: The data_vio seeking to share the lock.
 *
 * Check whether the data in data_vios sharing a lock is different than in a data_vio seeking to
 * share the lock, which should only be possible in the extremely unlikely case of a hash
 * collision.
 *
 * Return: true if the given data_vio must not share the lock because it doesn't have the same data
 *         as the lock holders.
 */
static bool is_hash_collision(struct hash_lock *lock, struct data_vio *candidate)
{
	struct data_vio *lock_holder;
	struct hash_zone *zone;
	bool collides;

	if (list_empty(&lock->duplicate_ring))
		return false;

	lock_holder = list_first_entry(&lock->duplicate_ring, struct data_vio,
				       hash_lock_entry);
	zone = candidate->hash_zone;
	collides = !blocks_equal(lock_holder->vio.data, candidate->vio.data);
	if (collides)
		increment_stat(&zone->statistics.concurrent_hash_collisions);
	else
		increment_stat(&zone->statistics.concurrent_data_matches);

	return collides;
}

static inline int assert_hash_lock_preconditions(const struct data_vio *data_vio)
{
	int result;

	/* FIXME: BUG_ON() and/or enter read-only mode? */
	result = VDO_ASSERT(data_vio->hash_lock == NULL,
			    "must not already hold a hash lock");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(list_empty(&data_vio->hash_lock_entry),
			    "must not already be a member of a hash lock ring");
	if (result != VDO_SUCCESS)
		return result;

	return VDO_ASSERT(data_vio->recovery_sequence_number == 0,
			  "must not hold a recovery lock when getting a hash lock");
}

/**
 * vdo_acquire_hash_lock() - Acquire or share a lock on a record name.
 * @data_vio: The data_vio acquiring a lock on its record name.
 *
 * Acquire or share a lock on the hash (record name) of the data in a data_vio, updating the
 * data_vio to reference the lock. This must only be called in the correct thread for the zone. In
 * the unlikely case of a hash collision, this function will succeed, but the data_vio will not get
 * a lock reference.
 */
void vdo_acquire_hash_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct hash_lock *lock;
	int result;

	assert_data_vio_in_hash_zone(data_vio);

	result = assert_hash_lock_preconditions(data_vio);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	result = acquire_lock(data_vio->hash_zone, &data_vio->record_name, NULL, &lock);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	if (is_hash_collision(lock, data_vio)) {
		/*
		 * Hash collisions are extremely unlikely, but the bogus dedupe would be a data
		 * corruption. Bypass optimization entirely. We can't compress a data_vio without
		 * a hash_lock as the compressed write depends on the hash_lock to manage the
		 * references for the compressed block.
		 */
		write_data_vio(data_vio);
		return;
	}

	set_hash_lock(data_vio, lock);
	switch (lock->state) {
	case VDO_HASH_LOCK_INITIALIZING:
		start_querying(lock, data_vio);
		return;

	case VDO_HASH_LOCK_QUERYING:
	case VDO_HASH_LOCK_WRITING:
	case VDO_HASH_LOCK_UPDATING:
	case VDO_HASH_LOCK_LOCKING:
	case VDO_HASH_LOCK_VERIFYING:
	case VDO_HASH_LOCK_UNLOCKING:
		/* The lock is busy, and can't be shared yet. */
		wait_on_hash_lock(lock, data_vio);
		return;

	case VDO_HASH_LOCK_BYPASSING:
		/* We can't use this lock, so bypass optimization entirely. */
		vdo_release_hash_lock(data_vio);
		write_data_vio(data_vio);
		return;

	case VDO_HASH_LOCK_DEDUPING:
		launch_dedupe(lock, data_vio, false);
		return;

	default:
		/* A lock in this state should not be acquired by new VIOs. */
		report_bogus_lock_state(lock, data_vio);
	}
}

/**
 * vdo_release_hash_lock() - Release a data_vio's share of a hash lock, if held, and null out the
 *                           data_vio's reference to it.
 * @data_vio: The data_vio releasing its hash lock.
 *
 * If the data_vio is the only one holding the lock, this also releases any resources or locks used
 * by the hash lock (such as a PBN read lock on a block containing data with the same hash) and
 * returns the lock to the hash zone's lock pool.
 *
 * Context: This must only be called in the correct thread for the hash zone.
 */
void vdo_release_hash_lock(struct data_vio *data_vio)
{
	u64 lock_key;
	struct hash_lock *lock = data_vio->hash_lock;
	struct hash_zone *zone = data_vio->hash_zone;

	if (lock == NULL)
		return;

	set_hash_lock(data_vio, NULL);

	if (lock->reference_count > 0) {
		/* The lock is still in use by other data_vios. */
		return;
	}

	lock_key = hash_lock_key(lock);
	if (lock->registered) {
		struct hash_lock *removed;

		removed = vdo_int_map_remove(zone->hash_lock_map, lock_key);
		VDO_ASSERT_LOG_ONLY(lock == removed,
				    "hash lock being released must have been mapped");
	} else {
		VDO_ASSERT_LOG_ONLY(lock != vdo_int_map_get(zone->hash_lock_map, lock_key),
				    "unregistered hash lock must not be in the lock map");
	}

	VDO_ASSERT_LOG_ONLY(!vdo_waitq_has_waiters(&lock->waiters),
			    "hash lock returned to zone must have no waiters");
	VDO_ASSERT_LOG_ONLY((lock->duplicate_lock == NULL),
			    "hash lock returned to zone must not reference a PBN lock");
	VDO_ASSERT_LOG_ONLY((lock->state == VDO_HASH_LOCK_BYPASSING),
			    "returned hash lock must not be in use with state %s",
			    get_hash_lock_state_name(lock->state));
	VDO_ASSERT_LOG_ONLY(list_empty(&lock->pool_node),
			    "hash lock returned to zone must not be in a pool ring");
	VDO_ASSERT_LOG_ONLY(list_empty(&lock->duplicate_ring),
			    "hash lock returned to zone must not reference DataVIOs");

	return_hash_lock_to_pool(zone, lock);
}

/**
 * transfer_allocation_lock() - Transfer a data_vio's downgraded allocation PBN lock to the
 *                              data_vio's hash lock, converting it to a duplicate PBN lock.
 * @data_vio: The data_vio holding the allocation lock to transfer.
 */
static void transfer_allocation_lock(struct data_vio *data_vio)
{
	struct allocation *allocation = &data_vio->allocation;
	struct hash_lock *hash_lock = data_vio->hash_lock;

	VDO_ASSERT_LOG_ONLY(data_vio->new_mapped.pbn == allocation->pbn,
			    "transferred lock must be for the block written");

	allocation->pbn = VDO_ZERO_BLOCK;

	VDO_ASSERT_LOG_ONLY(vdo_is_pbn_read_lock(allocation->lock),
			    "must have downgraded the allocation lock before transfer");

	hash_lock->duplicate = data_vio->new_mapped;
	data_vio->duplicate = data_vio->new_mapped;

	/*
	 * Since the lock is being transferred, the holder count doesn't change (and isn't even
	 * safe to examine on this thread).
	 */
	hash_lock->duplicate_lock = vdo_forget(allocation->lock);
}

/**
 * vdo_share_compressed_write_lock() - Make a data_vio's hash lock a shared holder of the PBN lock
 *                                     on the compressed block to which its data was just written.
 * @data_vio: The data_vio which was just compressed.
 * @pbn_lock: The PBN lock on the compressed block.
 *
 * If the lock is still a write lock (as it will be for the first share), it will be converted to a
 * read lock. This also reserves a reference count increment for the data_vio.
 */
void vdo_share_compressed_write_lock(struct data_vio *data_vio,
				     struct pbn_lock *pbn_lock)
{
	bool claimed;

	VDO_ASSERT_LOG_ONLY(vdo_get_duplicate_lock(data_vio) == NULL,
			    "a duplicate PBN lock should not exist when writing");
	VDO_ASSERT_LOG_ONLY(vdo_is_state_compressed(data_vio->new_mapped.state),
			    "lock transfer must be for a compressed write");
	assert_data_vio_in_new_mapped_zone(data_vio);

	/* First sharer downgrades the lock. */
	if (!vdo_is_pbn_read_lock(pbn_lock))
		vdo_downgrade_pbn_write_lock(pbn_lock, true);

	/*
	 * Get a share of the PBN lock, ensuring it cannot be released until after this data_vio
	 * has had a chance to journal a reference.
	 */
	data_vio->duplicate = data_vio->new_mapped;
	data_vio->hash_lock->duplicate = data_vio->new_mapped;
	set_duplicate_lock(data_vio->hash_lock, pbn_lock);

	/*
	 * Claim a reference for this data_vio. Necessary since another hash_lock might start
	 * deduplicating against it before our incRef.
	 */
	claimed = vdo_claim_pbn_lock_increment(pbn_lock);
	VDO_ASSERT_LOG_ONLY(claimed, "impossible to fail to claim an initial increment");
}

static void start_uds_queue(void *ptr)
{
	/*
	 * Allow the UDS dedupe worker thread to do memory allocations. It will only do allocations
	 * during the UDS calls that open or close an index, but those allocations can safely sleep
	 * while reserving a large amount of memory. We could use an allocations_allowed boolean
	 * (like the base threads do), but it would be an unnecessary embellishment.
	 */
	struct vdo_thread *thread = vdo_get_work_queue_owner(vdo_get_current_work_queue());

	vdo_register_allocating_thread(&thread->allocating_thread, NULL);
}

static void finish_uds_queue(void *ptr __always_unused)
{
	vdo_unregister_allocating_thread();
}

static void close_index(struct hash_zones *zones)
	__must_hold(&zones->lock)
{
	int result;

	/*
	 * Change the index state so that get_index_statistics() will not try to use the index
	 * session we are closing.
	 */
	zones->index_state = IS_CHANGING;
	/* Close the index session, while not holding the lock. */
	spin_unlock(&zones->lock);
	result = uds_close_index(zones->index_session);

	if (result != UDS_SUCCESS)
		vdo_log_error_strerror(result, "Error closing index");
	spin_lock(&zones->lock);
	zones->index_state = IS_CLOSED;
	zones->error_flag |= result != UDS_SUCCESS;
	/* ASSERTION: We leave in IS_CLOSED state. */
}

static void open_index(struct hash_zones *zones)
	__must_hold(&zones->lock)
{
	/* ASSERTION: We enter in IS_CLOSED state. */
	int result;
	bool create_flag = zones->create_flag;

	zones->create_flag = false;
	/*
	 * Change the index state so that the it will be reported to the outside world as
	 * "opening".
	 */
	zones->index_state = IS_CHANGING;
	zones->error_flag = false;

	/* Open the index session, while not holding the lock */
	spin_unlock(&zones->lock);
	result = uds_open_index(create_flag ? UDS_CREATE : UDS_LOAD,
				&zones->parameters, zones->index_session);
	if (result != UDS_SUCCESS)
		vdo_log_error_strerror(result, "Error opening index");

	spin_lock(&zones->lock);
	if (!create_flag) {
		switch (result) {
		case -ENOENT:
			/*
			 * Either there is no index, or there is no way we can recover the index.
			 * We will be called again and try to create a new index.
			 */
			zones->index_state = IS_CLOSED;
			zones->create_flag = true;
			return;
		default:
			break;
		}
	}
	if (result == UDS_SUCCESS) {
		zones->index_state = IS_OPENED;
	} else {
		zones->index_state = IS_CLOSED;
		zones->index_target = IS_CLOSED;
		zones->error_flag = true;
		spin_unlock(&zones->lock);
		vdo_log_info("Setting UDS index target state to error");
		spin_lock(&zones->lock);
	}
	/*
	 * ASSERTION: On success, we leave in IS_OPENED state.
	 * ASSERTION: On failure, we leave in IS_CLOSED state.
	 */
}

static void change_dedupe_state(struct vdo_completion *completion)
{
	struct hash_zones *zones = as_hash_zones(completion);

	spin_lock(&zones->lock);

	/* Loop until the index is in the target state and the create flag is clear. */
	while (vdo_is_state_normal(&zones->state) &&
	       ((zones->index_state != zones->index_target) || zones->create_flag)) {
		if (zones->index_state == IS_OPENED)
			close_index(zones);
		else
			open_index(zones);
	}

	zones->changing = false;
	spin_unlock(&zones->lock);
}

static void start_expiration_timer(struct dedupe_context *context)
{
	u64 start_time = context->submission_jiffies;
	u64 end_time;

	if (!change_timer_state(context->zone, DEDUPE_QUERY_TIMER_IDLE,
				DEDUPE_QUERY_TIMER_RUNNING))
		return;

	end_time = max(start_time + vdo_dedupe_index_timeout_jiffies,
		       jiffies + vdo_dedupe_index_min_timer_jiffies);
	mod_timer(&context->zone->timer, end_time);
}

/**
 * report_dedupe_timeouts() - Record and eventually report that some dedupe requests reached their
 *                            expiration time without getting answers, so we timed them out.
 * @zones: the hash zones.
 * @timeouts: the number of newly timed out requests.
 */
static void report_dedupe_timeouts(struct hash_zones *zones, unsigned int timeouts)
{
	atomic64_add(timeouts, &zones->timeouts);
	spin_lock(&zones->lock);
	if (__ratelimit(&zones->ratelimiter)) {
		u64 unreported = atomic64_read(&zones->timeouts);

		unreported -= zones->reported_timeouts;
		vdo_log_debug("UDS index timeout on %llu requests",
			      (unsigned long long) unreported);
		zones->reported_timeouts += unreported;
	}
	spin_unlock(&zones->lock);
}

static int initialize_index(struct vdo *vdo, struct hash_zones *zones)
{
	int result;
	off_t uds_offset;
	struct volume_geometry geometry = vdo->geometry;
	static const struct vdo_work_queue_type uds_queue_type = {
		.start = start_uds_queue,
		.finish = finish_uds_queue,
		.max_priority = UDS_Q_MAX_PRIORITY,
		.default_priority = UDS_Q_PRIORITY,
	};

	vdo_set_dedupe_index_timeout_interval(vdo_dedupe_index_timeout_interval);
	vdo_set_dedupe_index_min_timer_interval(vdo_dedupe_index_min_timer_interval);

	/*
	 * Since we will save up the timeouts that would have been reported but were ratelimited,
	 * we don't need to report ratelimiting.
	 */
	ratelimit_default_init(&zones->ratelimiter);
	ratelimit_set_flags(&zones->ratelimiter, RATELIMIT_MSG_ON_RELEASE);
	uds_offset = ((vdo_get_index_region_start(geometry) -
		       geometry.bio_offset) * VDO_BLOCK_SIZE);
	zones->parameters = (struct uds_parameters) {
		.bdev = vdo->device_config->owned_device->bdev,
		.offset = uds_offset,
		.size = (vdo_get_index_region_size(geometry) * VDO_BLOCK_SIZE),
		.memory_size = geometry.index_config.mem,
		.sparse = geometry.index_config.sparse,
		.nonce = (u64) geometry.nonce,
	};

	result = uds_create_index_session(&zones->index_session);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_make_thread(vdo, vdo->thread_config.dedupe_thread, &uds_queue_type,
				 1, NULL);
	if (result != VDO_SUCCESS) {
		uds_destroy_index_session(vdo_forget(zones->index_session));
		vdo_log_error("UDS index queue initialization failed (%d)", result);
		return result;
	}

	vdo_initialize_completion(&zones->completion, vdo, VDO_HASH_ZONES_COMPLETION);
	vdo_set_completion_callback(&zones->completion, change_dedupe_state,
				    vdo->thread_config.dedupe_thread);
	return VDO_SUCCESS;
}

/**
 * finish_index_operation() - This is the UDS callback for index queries.
 * @request: The uds request which has just completed.
 */
static void finish_index_operation(struct uds_request *request)
{
	struct dedupe_context *context = container_of(request, struct dedupe_context,
						      request);

	if (change_context_state(context, DEDUPE_CONTEXT_PENDING,
				 DEDUPE_CONTEXT_COMPLETE)) {
		/*
		 * This query has not timed out, so send its data_vio back to its hash zone to
		 * process the results.
		 */
		continue_data_vio(context->requestor);
		return;
	}

	/*
	 * This query has timed out, so try to mark it complete and hence eligible for reuse. Its
	 * data_vio has already moved on.
	 */
	if (!change_context_state(context, DEDUPE_CONTEXT_TIMED_OUT,
				  DEDUPE_CONTEXT_TIMED_OUT_COMPLETE)) {
		VDO_ASSERT_LOG_ONLY(false, "uds request was timed out (state %d)",
				    atomic_read(&context->state));
	}

	vdo_funnel_queue_put(context->zone->timed_out_complete, &context->queue_entry);
}

/**
 * check_for_drain_complete() - Check whether this zone has drained.
 * @zone: The zone to check.
 */
static void check_for_drain_complete(struct hash_zone *zone)
{
	data_vio_count_t recycled = 0;

	if (!vdo_is_state_draining(&zone->state))
		return;

	if ((atomic_read(&zone->timer_state) == DEDUPE_QUERY_TIMER_IDLE) ||
	    change_timer_state(zone, DEDUPE_QUERY_TIMER_RUNNING,
			       DEDUPE_QUERY_TIMER_IDLE)) {
		del_timer_sync(&zone->timer);
	} else {
		/*
		 * There is an in flight time-out, which must get processed before we can continue.
		 */
		return;
	}

	for (;;) {
		struct dedupe_context *context;
		struct funnel_queue_entry *entry;

		entry = vdo_funnel_queue_poll(zone->timed_out_complete);
		if (entry == NULL)
			break;

		context = container_of(entry, struct dedupe_context, queue_entry);
		atomic_set(&context->state, DEDUPE_CONTEXT_IDLE);
		list_add(&context->list_entry, &zone->available);
		recycled++;
	}

	if (recycled > 0)
		WRITE_ONCE(zone->active, zone->active - recycled);
	VDO_ASSERT_LOG_ONLY(READ_ONCE(zone->active) == 0, "all contexts inactive");
	vdo_finish_draining(&zone->state);
}

static void timeout_index_operations_callback(struct vdo_completion *completion)
{
	struct dedupe_context *context, *tmp;
	struct hash_zone *zone = as_hash_zone(completion);
	u64 timeout_jiffies = msecs_to_jiffies(vdo_dedupe_index_timeout_interval);
	unsigned long cutoff = jiffies - timeout_jiffies;
	unsigned int timed_out = 0;

	atomic_set(&zone->timer_state, DEDUPE_QUERY_TIMER_IDLE);
	list_for_each_entry_safe(context, tmp, &zone->pending, list_entry) {
		if (cutoff <= context->submission_jiffies) {
			/*
			 * We have reached the oldest query which has not timed out yet, so restart
			 * the timer.
			 */
			start_expiration_timer(context);
			break;
		}

		if (!change_context_state(context, DEDUPE_CONTEXT_PENDING,
					  DEDUPE_CONTEXT_TIMED_OUT)) {
			/*
			 * This context completed between the time the timeout fired, and now. We
			 * can treat it as a successful query, its requestor is already enqueued
			 * to process it.
			 */
			continue;
		}

		/*
		 * Remove this context from the pending list so we won't look at it again on a
		 * subsequent timeout. Once the index completes it, it will be reused. Meanwhile,
		 * send its requestor on its way.
		 */
		list_del_init(&context->list_entry);
		context->requestor->dedupe_context = NULL;
		continue_data_vio(context->requestor);
		timed_out++;
	}

	if (timed_out > 0)
		report_dedupe_timeouts(completion->vdo->hash_zones, timed_out);

	check_for_drain_complete(zone);
}

static void timeout_index_operations(struct timer_list *t)
{
	struct hash_zone *zone = from_timer(zone, t, timer);

	if (change_timer_state(zone, DEDUPE_QUERY_TIMER_RUNNING,
			       DEDUPE_QUERY_TIMER_FIRED))
		vdo_launch_completion(&zone->completion);
}

static int __must_check initialize_zone(struct vdo *vdo, struct hash_zones *zones,
					zone_count_t zone_number)
{
	int result;
	data_vio_count_t i;
	struct hash_zone *zone = &zones->zones[zone_number];

	result = vdo_int_map_create(VDO_LOCK_MAP_CAPACITY, &zone->hash_lock_map);
	if (result != VDO_SUCCESS)
		return result;

	vdo_set_admin_state_code(&zone->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	zone->zone_number = zone_number;
	zone->thread_id = vdo->thread_config.hash_zone_threads[zone_number];
	vdo_initialize_completion(&zone->completion, vdo, VDO_HASH_ZONE_COMPLETION);
	vdo_set_completion_callback(&zone->completion, timeout_index_operations_callback,
				    zone->thread_id);
	INIT_LIST_HEAD(&zone->lock_pool);
	result = vdo_allocate(LOCK_POOL_CAPACITY, struct hash_lock, "hash_lock array",
			      &zone->lock_array);
	if (result != VDO_SUCCESS)
		return result;

	for (i = 0; i < LOCK_POOL_CAPACITY; i++)
		return_hash_lock_to_pool(zone, &zone->lock_array[i]);

	INIT_LIST_HEAD(&zone->available);
	INIT_LIST_HEAD(&zone->pending);
	result = vdo_make_funnel_queue(&zone->timed_out_complete);
	if (result != VDO_SUCCESS)
		return result;

	timer_setup(&zone->timer, timeout_index_operations, 0);

	for (i = 0; i < MAXIMUM_VDO_USER_VIOS; i++) {
		struct dedupe_context *context = &zone->contexts[i];

		context->zone = zone;
		context->request.callback = finish_index_operation;
		context->request.session = zones->index_session;
		list_add(&context->list_entry, &zone->available);
	}

	return vdo_make_default_thread(vdo, zone->thread_id);
}

/** get_thread_id_for_zone() - Implements vdo_zone_thread_getter_fn. */
static thread_id_t get_thread_id_for_zone(void *context, zone_count_t zone_number)
{
	struct hash_zones *zones = context;

	return zones->zones[zone_number].thread_id;
}

/**
 * vdo_make_hash_zones() - Create the hash zones.
 *
 * @vdo: The vdo to which the zone will belong.
 * @zones_ptr: A pointer to hold the zones.
 *
 * Return: VDO_SUCCESS or an error code.
 */
int vdo_make_hash_zones(struct vdo *vdo, struct hash_zones **zones_ptr)
{
	int result;
	struct hash_zones *zones;
	zone_count_t z;
	zone_count_t zone_count = vdo->thread_config.hash_zone_count;

	if (zone_count == 0)
		return VDO_SUCCESS;

	result = vdo_allocate_extended(struct hash_zones, zone_count, struct hash_zone,
				       __func__, &zones);
	if (result != VDO_SUCCESS)
		return result;

	result = initialize_index(vdo, zones);
	if (result != VDO_SUCCESS) {
		vdo_free(zones);
		return result;
	}

	vdo_set_admin_state_code(&zones->state, VDO_ADMIN_STATE_NEW);

	zones->zone_count = zone_count;
	for (z = 0; z < zone_count; z++) {
		result = initialize_zone(vdo, zones, z);
		if (result != VDO_SUCCESS) {
			vdo_free_hash_zones(zones);
			return result;
		}
	}

	result = vdo_make_action_manager(zones->zone_count, get_thread_id_for_zone,
					 vdo->thread_config.admin_thread, zones, NULL,
					 vdo, &zones->manager);
	if (result != VDO_SUCCESS) {
		vdo_free_hash_zones(zones);
		return result;
	}

	*zones_ptr = zones;
	return VDO_SUCCESS;
}

void vdo_finish_dedupe_index(struct hash_zones *zones)
{
	if (zones == NULL)
		return;

	uds_destroy_index_session(vdo_forget(zones->index_session));
}

/**
 * vdo_free_hash_zones() - Free the hash zones.
 * @zones: The zone to free.
 */
void vdo_free_hash_zones(struct hash_zones *zones)
{
	zone_count_t i;

	if (zones == NULL)
		return;

	vdo_free(vdo_forget(zones->manager));

	for (i = 0; i < zones->zone_count; i++) {
		struct hash_zone *zone = &zones->zones[i];

		vdo_free_funnel_queue(vdo_forget(zone->timed_out_complete));
		vdo_int_map_free(vdo_forget(zone->hash_lock_map));
		vdo_free(vdo_forget(zone->lock_array));
	}

	if (zones->index_session != NULL)
		vdo_finish_dedupe_index(zones);

	ratelimit_state_exit(&zones->ratelimiter);
	vdo_free(zones);
}

static void initiate_suspend_index(struct admin_state *state)
{
	struct hash_zones *zones = container_of(state, struct hash_zones, state);
	enum index_state index_state;

	spin_lock(&zones->lock);
	index_state = zones->index_state;
	spin_unlock(&zones->lock);

	if (index_state != IS_CLOSED) {
		bool save = vdo_is_state_saving(&zones->state);
		int result;

		result = uds_suspend_index_session(zones->index_session, save);
		if (result != UDS_SUCCESS)
			vdo_log_error_strerror(result, "Error suspending dedupe index");
	}

	vdo_finish_draining(state);
}

/**
 * suspend_index() - Suspend the UDS index prior to draining hash zones.
 *
 * Implements vdo_action_preamble_fn
 */
static void suspend_index(void *context, struct vdo_completion *completion)
{
	struct hash_zones *zones = context;

	vdo_start_draining(&zones->state,
			   vdo_get_current_manager_operation(zones->manager), completion,
			   initiate_suspend_index);
}

/**
 * initiate_drain() - Initiate a drain.
 *
 * Implements vdo_admin_initiator_fn.
 */
static void initiate_drain(struct admin_state *state)
{
	check_for_drain_complete(container_of(state, struct hash_zone, state));
}

/**
 * drain_hash_zone() - Drain a hash zone.
 *
 * Implements vdo_zone_action_fn.
 */
static void drain_hash_zone(void *context, zone_count_t zone_number,
			    struct vdo_completion *parent)
{
	struct hash_zones *zones = context;

	vdo_start_draining(&zones->zones[zone_number].state,
			   vdo_get_current_manager_operation(zones->manager), parent,
			   initiate_drain);
}

/** vdo_drain_hash_zones() - Drain all hash zones. */
void vdo_drain_hash_zones(struct hash_zones *zones, struct vdo_completion *parent)
{
	vdo_schedule_operation(zones->manager, parent->vdo->suspend_type, suspend_index,
			       drain_hash_zone, NULL, parent);
}

static void launch_dedupe_state_change(struct hash_zones *zones)
	__must_hold(&zones->lock)
{
	/* ASSERTION: We enter with the lock held. */
	if (zones->changing || !vdo_is_state_normal(&zones->state))
		/* Either a change is already in progress, or changes are not allowed. */
		return;

	if (zones->create_flag || (zones->index_state != zones->index_target)) {
		zones->changing = true;
		vdo_launch_completion(&zones->completion);
		return;
	}

	/* ASSERTION: We exit with the lock held. */
}

/**
 * resume_index() - Resume the UDS index prior to resuming hash zones.
 *
 * Implements vdo_action_preamble_fn
 */
static void resume_index(void *context, struct vdo_completion *parent)
{
	struct hash_zones *zones = context;
	struct device_config *config = parent->vdo->device_config;
	int result;

	zones->parameters.bdev = config->owned_device->bdev;
	result = uds_resume_index_session(zones->index_session, zones->parameters.bdev);
	if (result != UDS_SUCCESS)
		vdo_log_error_strerror(result, "Error resuming dedupe index");

	spin_lock(&zones->lock);
	vdo_resume_if_quiescent(&zones->state);

	if (config->deduplication) {
		zones->index_target = IS_OPENED;
		WRITE_ONCE(zones->dedupe_flag, true);
	} else {
		zones->index_target = IS_CLOSED;
	}

	launch_dedupe_state_change(zones);
	spin_unlock(&zones->lock);

	vdo_finish_completion(parent);
}

/**
 * resume_hash_zone() - Resume a hash zone.
 *
 * Implements vdo_zone_action_fn.
 */
static void resume_hash_zone(void *context, zone_count_t zone_number,
			     struct vdo_completion *parent)
{
	struct hash_zone *zone = &(((struct hash_zones *) context)->zones[zone_number]);

	vdo_fail_completion(parent, vdo_resume_if_quiescent(&zone->state));
}

/**
 * vdo_resume_hash_zones() - Resume a set of hash zones.
 * @zones: The hash zones to resume.
 * @parent: The object to notify when the zones have resumed.
 */
void vdo_resume_hash_zones(struct hash_zones *zones, struct vdo_completion *parent)
{
	if (vdo_is_read_only(parent->vdo)) {
		vdo_launch_completion(parent);
		return;
	}

	vdo_schedule_operation(zones->manager, VDO_ADMIN_STATE_RESUMING, resume_index,
			       resume_hash_zone, NULL, parent);
}

/**
 * get_hash_zone_statistics() - Add the statistics for this hash zone to the tally for all zones.
 * @zone: The hash zone to query.
 * @tally: The tally
 */
static void get_hash_zone_statistics(const struct hash_zone *zone,
				     struct hash_lock_statistics *tally)
{
	const struct hash_lock_statistics *stats = &zone->statistics;

	tally->dedupe_advice_valid += READ_ONCE(stats->dedupe_advice_valid);
	tally->dedupe_advice_stale += READ_ONCE(stats->dedupe_advice_stale);
	tally->concurrent_data_matches += READ_ONCE(stats->concurrent_data_matches);
	tally->concurrent_hash_collisions += READ_ONCE(stats->concurrent_hash_collisions);
	tally->curr_dedupe_queries += READ_ONCE(zone->active);
}

static void get_index_statistics(struct hash_zones *zones,
				 struct index_statistics *stats)
{
	enum index_state state;
	struct uds_index_stats index_stats;
	int result;

	spin_lock(&zones->lock);
	state = zones->index_state;
	spin_unlock(&zones->lock);

	if (state != IS_OPENED)
		return;

	result = uds_get_index_session_stats(zones->index_session, &index_stats);
	if (result != UDS_SUCCESS) {
		vdo_log_error_strerror(result, "Error reading index stats");
		return;
	}

	stats->entries_indexed = index_stats.entries_indexed;
	stats->posts_found = index_stats.posts_found;
	stats->posts_not_found = index_stats.posts_not_found;
	stats->queries_found = index_stats.queries_found;
	stats->queries_not_found = index_stats.queries_not_found;
	stats->updates_found = index_stats.updates_found;
	stats->updates_not_found = index_stats.updates_not_found;
	stats->entries_discarded = index_stats.entries_discarded;
}

/**
 * vdo_get_dedupe_statistics() - Tally the statistics from all the hash zones and the UDS index.
 * @hash_zones: The hash zones to query
 *
 * Return: The sum of the hash lock statistics from all hash zones plus the statistics from the UDS
 *         index
 */
void vdo_get_dedupe_statistics(struct hash_zones *zones, struct vdo_statistics *stats)

{
	zone_count_t zone;

	for (zone = 0; zone < zones->zone_count; zone++)
		get_hash_zone_statistics(&zones->zones[zone], &stats->hash_lock);

	get_index_statistics(zones, &stats->index);

	/*
	 * zones->timeouts gives the number of timeouts, and dedupe_context_busy gives the number
	 * of queries not made because of earlier timeouts.
	 */
	stats->dedupe_advice_timeouts =
		(atomic64_read(&zones->timeouts) + atomic64_read(&zones->dedupe_context_busy));
}

/**
 * vdo_select_hash_zone() - Select the hash zone responsible for locking a given record name.
 * @zones: The hash_zones from which to select.
 * @name: The record name.
 *
 * Return: The hash zone responsible for the record name.
 */
struct hash_zone *vdo_select_hash_zone(struct hash_zones *zones,
				       const struct uds_record_name *name)
{
	/*
	 * Use a fragment of the record name as a hash code. Eight bits of hash should suffice
	 * since the number of hash zones is small.
	 * TODO: Verify that the first byte is independent enough.
	 */
	u32 hash = name->name[0];

	/*
	 * Scale the 8-bit hash fragment to a zone index by treating it as a binary fraction and
	 * multiplying that by the zone count. If the hash is uniformly distributed over [0 ..
	 * 2^8-1], then (hash * count / 2^8) should be uniformly distributed over [0 .. count-1].
	 * The multiply and shift is much faster than a divide (modulus) on X86 CPUs.
	 */
	hash = (hash * zones->zone_count) >> 8;
	return &zones->zones[hash];
}

/**
 * dump_hash_lock() - Dump a compact description of hash_lock to the log if the lock is not on the
 *                    free list.
 * @lock: The hash lock to dump.
 */
static void dump_hash_lock(const struct hash_lock *lock)
{
	const char *state;

	if (!list_empty(&lock->pool_node)) {
		/* This lock is on the free list. */
		return;
	}

	/*
	 * Necessarily cryptic since we can log a lot of these. First three chars of state is
	 * unambiguous. 'U' indicates a lock not registered in the map.
	 */
	state = get_hash_lock_state_name(lock->state);
	vdo_log_info("  hl %px: %3.3s %c%llu/%u rc=%u wc=%zu agt=%px",
		     lock, state, (lock->registered ? 'D' : 'U'),
		     (unsigned long long) lock->duplicate.pbn,
		     lock->duplicate.state, lock->reference_count,
		     vdo_waitq_num_waiters(&lock->waiters), lock->agent);
}

static const char *index_state_to_string(struct hash_zones *zones,
					 enum index_state state)
{
	if (!vdo_is_state_normal(&zones->state))
		return SUSPENDED;

	switch (state) {
	case IS_CLOSED:
		return zones->error_flag ? ERROR : CLOSED;
	case IS_CHANGING:
		return zones->index_target == IS_OPENED ? OPENING : CLOSING;
	case IS_OPENED:
		return READ_ONCE(zones->dedupe_flag) ? ONLINE : OFFLINE;
	default:
		return UNKNOWN;
	}
}

/**
 * dump_hash_zone() - Dump information about a hash zone to the log for debugging.
 * @zone: The zone to dump.
 */
static void dump_hash_zone(const struct hash_zone *zone)
{
	data_vio_count_t i;

	if (zone->hash_lock_map == NULL) {
		vdo_log_info("struct hash_zone %u: NULL map", zone->zone_number);
		return;
	}

	vdo_log_info("struct hash_zone %u: mapSize=%zu",
		     zone->zone_number, vdo_int_map_size(zone->hash_lock_map));
	for (i = 0; i < LOCK_POOL_CAPACITY; i++)
		dump_hash_lock(&zone->lock_array[i]);
}

/**
 * vdo_dump_hash_zones() - Dump information about the hash zones to the log for debugging.
 * @zones: The zones to dump.
 */
void vdo_dump_hash_zones(struct hash_zones *zones)
{
	const char *state, *target;
	zone_count_t zone;

	spin_lock(&zones->lock);
	state = index_state_to_string(zones, zones->index_state);
	target = (zones->changing ? index_state_to_string(zones, zones->index_target) : NULL);
	spin_unlock(&zones->lock);

	vdo_log_info("UDS index: state: %s", state);
	if (target != NULL)
		vdo_log_info("UDS index: changing to state: %s", target);

	for (zone = 0; zone < zones->zone_count; zone++)
		dump_hash_zone(&zones->zones[zone]);
}

void vdo_set_dedupe_index_timeout_interval(unsigned int value)
{
	u64 alb_jiffies;

	/* Arbitrary maximum value is two minutes */
	if (value > 120000)
		value = 120000;
	/* Arbitrary minimum value is 2 jiffies */
	alb_jiffies = msecs_to_jiffies(value);

	if (alb_jiffies < 2) {
		alb_jiffies = 2;
		value = jiffies_to_msecs(alb_jiffies);
	}
	vdo_dedupe_index_timeout_interval = value;
	vdo_dedupe_index_timeout_jiffies = alb_jiffies;
}

void vdo_set_dedupe_index_min_timer_interval(unsigned int value)
{
	u64 min_jiffies;

	/* Arbitrary maximum value is one second */
	if (value > 1000)
		value = 1000;

	/* Arbitrary minimum value is 2 jiffies */
	min_jiffies = msecs_to_jiffies(value);

	if (min_jiffies < 2) {
		min_jiffies = 2;
		value = jiffies_to_msecs(min_jiffies);
	}

	vdo_dedupe_index_min_timer_interval = value;
	vdo_dedupe_index_min_timer_jiffies = min_jiffies;
}

/**
 * acquire_context() - Acquire a dedupe context from a hash_zone if any are available.
 * @zone: the hash zone
 *
 * Return: A dedupe_context or NULL if none are available
 */
static struct dedupe_context * __must_check acquire_context(struct hash_zone *zone)
{
	struct dedupe_context *context;
	struct funnel_queue_entry *entry;

	assert_in_hash_zone(zone, __func__);

	if (!list_empty(&zone->available)) {
		WRITE_ONCE(zone->active, zone->active + 1);
		context = list_first_entry(&zone->available, struct dedupe_context,
					   list_entry);
		list_del_init(&context->list_entry);
		return context;
	}

	entry = vdo_funnel_queue_poll(zone->timed_out_complete);
	return ((entry == NULL) ?
		NULL : container_of(entry, struct dedupe_context, queue_entry));
}

static void prepare_uds_request(struct uds_request *request, struct data_vio *data_vio,
				enum uds_request_type operation)
{
	request->record_name = data_vio->record_name;
	request->type = operation;
	if ((operation == UDS_POST) || (operation == UDS_UPDATE)) {
		size_t offset = 0;
		struct uds_record_data *encoding = &request->new_metadata;

		encoding->data[offset++] = UDS_ADVICE_VERSION;
		encoding->data[offset++] = data_vio->new_mapped.state;
		put_unaligned_le64(data_vio->new_mapped.pbn, &encoding->data[offset]);
		offset += sizeof(u64);
		BUG_ON(offset != UDS_ADVICE_SIZE);
	}
}

/*
 * The index operation will inquire about data_vio.record_name, providing (if the operation is
 * appropriate) advice from the data_vio's new_mapped fields. The advice found in the index (or
 * NULL if none) will be returned via receive_data_vio_dedupe_advice(). dedupe_context.status is
 * set to the return status code of any asynchronous index processing.
 */
static void query_index(struct data_vio *data_vio, enum uds_request_type operation)
{
	int result;
	struct dedupe_context *context;
	struct vdo *vdo = vdo_from_data_vio(data_vio);
	struct hash_zone *zone = data_vio->hash_zone;

	assert_data_vio_in_hash_zone(data_vio);

	if (!READ_ONCE(vdo->hash_zones->dedupe_flag)) {
		continue_data_vio(data_vio);
		return;
	}

	context = acquire_context(zone);
	if (context == NULL) {
		atomic64_inc(&vdo->hash_zones->dedupe_context_busy);
		continue_data_vio(data_vio);
		return;
	}

	data_vio->dedupe_context = context;
	context->requestor = data_vio;
	context->submission_jiffies = jiffies;
	prepare_uds_request(&context->request, data_vio, operation);
	atomic_set(&context->state, DEDUPE_CONTEXT_PENDING);
	list_add_tail(&context->list_entry, &zone->pending);
	start_expiration_timer(context);
	result = uds_launch_request(&context->request);
	if (result != UDS_SUCCESS) {
		context->request.status = result;
		finish_index_operation(&context->request);
	}
}

static void set_target_state(struct hash_zones *zones, enum index_state target,
			     bool change_dedupe, bool dedupe, bool set_create)
{
	const char *old_state, *new_state;

	spin_lock(&zones->lock);
	old_state = index_state_to_string(zones, zones->index_target);
	if (change_dedupe)
		WRITE_ONCE(zones->dedupe_flag, dedupe);

	if (set_create)
		zones->create_flag = true;

	zones->index_target = target;
	launch_dedupe_state_change(zones);
	new_state = index_state_to_string(zones, zones->index_target);
	spin_unlock(&zones->lock);

	if (old_state != new_state)
		vdo_log_info("Setting UDS index target state to %s", new_state);
}

const char *vdo_get_dedupe_index_state_name(struct hash_zones *zones)
{
	const char *state;

	spin_lock(&zones->lock);
	state = index_state_to_string(zones, zones->index_state);
	spin_unlock(&zones->lock);

	return state;
}

/* Handle a dmsetup message relevant to the index. */
int vdo_message_dedupe_index(struct hash_zones *zones, const char *name)
{
	if (strcasecmp(name, "index-close") == 0) {
		set_target_state(zones, IS_CLOSED, false, false, false);
		return 0;
	} else if (strcasecmp(name, "index-create") == 0) {
		set_target_state(zones, IS_OPENED, false, false, true);
		return 0;
	} else if (strcasecmp(name, "index-disable") == 0) {
		set_target_state(zones, IS_OPENED, true, false, false);
		return 0;
	} else if (strcasecmp(name, "index-enable") == 0) {
		set_target_state(zones, IS_OPENED, true, true, false);
		return 0;
	}

	return -EINVAL;
}

void vdo_set_dedupe_state_normal(struct hash_zones *zones)
{
	vdo_set_admin_state_code(&zones->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
}

/* If create_flag, create a new index without first attempting to load an existing index. */
void vdo_start_dedupe_index(struct hash_zones *zones, bool create_flag)
{
	set_target_state(zones, IS_OPENED, true, true, create_flag);
}
