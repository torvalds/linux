// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "physical-zone.h"

#include <linux/list.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "block-map.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "dedupe.h"
#include "encodings.h"
#include "flush.h"
#include "int-map.h"
#include "slab-depot.h"
#include "status-codes.h"
#include "vdo.h"

/* Each user data_vio needs a PBN read lock and write lock. */
#define LOCK_POOL_CAPACITY (2 * MAXIMUM_VDO_USER_VIOS)

struct pbn_lock_implementation {
	enum pbn_lock_type type;
	const char *name;
	const char *release_reason;
};

/* This array must have an entry for every pbn_lock_type value. */
static const struct pbn_lock_implementation LOCK_IMPLEMENTATIONS[] = {
	[VIO_READ_LOCK] = {
		.type = VIO_READ_LOCK,
		.name = "read",
		.release_reason = "candidate duplicate",
	},
	[VIO_WRITE_LOCK] = {
		.type = VIO_WRITE_LOCK,
		.name = "write",
		.release_reason = "newly allocated",
	},
	[VIO_BLOCK_MAP_WRITE_LOCK] = {
		.type = VIO_BLOCK_MAP_WRITE_LOCK,
		.name = "block map write",
		.release_reason = "block map write",
	},
};

static inline bool has_lock_type(const struct pbn_lock *lock, enum pbn_lock_type type)
{
	return (lock->implementation == &LOCK_IMPLEMENTATIONS[type]);
}

/**
 * vdo_is_pbn_read_lock() - Check whether a pbn_lock is a read lock.
 * @lock: The lock to check.
 *
 * Return: true if the lock is a read lock.
 */
bool vdo_is_pbn_read_lock(const struct pbn_lock *lock)
{
	return has_lock_type(lock, VIO_READ_LOCK);
}

static inline void set_pbn_lock_type(struct pbn_lock *lock, enum pbn_lock_type type)
{
	lock->implementation = &LOCK_IMPLEMENTATIONS[type];
}

/**
 * vdo_downgrade_pbn_write_lock() - Downgrade a PBN write lock to a PBN read lock.
 * @lock: The PBN write lock to downgrade.
 *
 * The lock holder count is cleared and the caller is responsible for setting the new count.
 */
void vdo_downgrade_pbn_write_lock(struct pbn_lock *lock, bool compressed_write)
{
	VDO_ASSERT_LOG_ONLY(!vdo_is_pbn_read_lock(lock),
			    "PBN lock must not already have been downgraded");
	VDO_ASSERT_LOG_ONLY(!has_lock_type(lock, VIO_BLOCK_MAP_WRITE_LOCK),
			    "must not downgrade block map write locks");
	VDO_ASSERT_LOG_ONLY(lock->holder_count == 1,
			    "PBN write lock should have one holder but has %u",
			    lock->holder_count);
	/*
	 * data_vio write locks are downgraded in place--the writer retains the hold on the lock.
	 * If this was a compressed write, the holder has not yet journaled its own inc ref,
	 * otherwise, it has.
	 */
	lock->increment_limit =
		(compressed_write ? MAXIMUM_REFERENCE_COUNT : MAXIMUM_REFERENCE_COUNT - 1);
	set_pbn_lock_type(lock, VIO_READ_LOCK);
}

/**
 * vdo_claim_pbn_lock_increment() - Try to claim one of the available reference count increments on
 *				    a read lock.
 * @lock: The PBN read lock from which to claim an increment.
 *
 * Claims may be attempted from any thread. A claim is only valid until the PBN lock is released.
 *
 * Return: true if the claim succeeded, guaranteeing one increment can be made without overflowing
 *	   the PBN's reference count.
 */
bool vdo_claim_pbn_lock_increment(struct pbn_lock *lock)
{
	/*
	 * Claim the next free reference atomically since hash locks from multiple hash zone
	 * threads might be concurrently deduplicating against a single PBN lock on compressed
	 * block. As long as hitting the increment limit will lead to the PBN lock being released
	 * in a sane time-frame, we won't overflow a 32-bit claim counter, allowing a simple add
	 * instead of a compare-and-swap.
	 */
	u32 claim_number = (u32) atomic_add_return(1, &lock->increments_claimed);

	return (claim_number <= lock->increment_limit);
}

/**
 * vdo_assign_pbn_lock_provisional_reference() - Inform a PBN lock that it is responsible for a
 *						 provisional reference.
 * @lock: The PBN lock.
 */
void vdo_assign_pbn_lock_provisional_reference(struct pbn_lock *lock)
{
	VDO_ASSERT_LOG_ONLY(!lock->has_provisional_reference,
			    "lock does not have a provisional reference");
	lock->has_provisional_reference = true;
}

/**
 * vdo_unassign_pbn_lock_provisional_reference() - Inform a PBN lock that it is no longer
 *						   responsible for a provisional reference.
 * @lock: The PBN lock.
 */
void vdo_unassign_pbn_lock_provisional_reference(struct pbn_lock *lock)
{
	lock->has_provisional_reference = false;
}

/**
 * release_pbn_lock_provisional_reference() - If the lock is responsible for a provisional
 *					      reference, release that reference.
 * @lock: The lock.
 * @locked_pbn: The PBN covered by the lock.
 * @allocator: The block allocator from which to release the reference.
 *
 * This method is called when the lock is released.
 */
static void release_pbn_lock_provisional_reference(struct pbn_lock *lock,
						   physical_block_number_t locked_pbn,
						   struct block_allocator *allocator)
{
	int result;

	if (!vdo_pbn_lock_has_provisional_reference(lock))
		return;

	result = vdo_release_block_reference(allocator, locked_pbn);
	if (result != VDO_SUCCESS) {
		vdo_log_error_strerror(result,
				       "Failed to release reference to %s physical block %llu",
				       lock->implementation->release_reason,
				       (unsigned long long) locked_pbn);
	}

	vdo_unassign_pbn_lock_provisional_reference(lock);
}

/**
 * union idle_pbn_lock - PBN lock list entries.
 *
 * Unused (idle) PBN locks are kept in a list. Just like in a malloc implementation, the lock
 * structure is unused memory, so we can save a bit of space (and not pollute the lock structure
 * proper) by using a union to overlay the lock structure with the free list.
 */
typedef union {
	/** @entry: Only used while locks are in the pool. */
	struct list_head entry;
	/** @lock: Only used while locks are not in the pool. */
	struct pbn_lock lock;
} idle_pbn_lock;

/**
 * struct pbn_lock_pool - list of PBN locks.
 *
 * The lock pool is little more than the memory allocated for the locks.
 */
struct pbn_lock_pool {
	/** @capacity: The number of locks allocated for the pool. */
	size_t capacity;
	/** @borrowed: The number of locks currently borrowed from the pool. */
	size_t borrowed;
	/** @idle_list: A list containing all idle PBN lock instances. */
	struct list_head idle_list;
	/** @locks: The memory for all the locks allocated by this pool. */
	idle_pbn_lock locks[];
};

/**
 * return_pbn_lock_to_pool() - Return a pbn lock to its pool.
 * @pool: The pool from which the lock was borrowed.
 * @lock: The last reference to the lock being returned.
 *
 * It must be the last live reference, as if the memory were being freed (the lock memory will
 * re-initialized or zeroed).
 */
static void return_pbn_lock_to_pool(struct pbn_lock_pool *pool, struct pbn_lock *lock)
{
	idle_pbn_lock *idle;

	/* A bit expensive, but will promptly catch some use-after-free errors. */
	memset(lock, 0, sizeof(*lock));

	idle = container_of(lock, idle_pbn_lock, lock);
	INIT_LIST_HEAD(&idle->entry);
	list_add_tail(&idle->entry, &pool->idle_list);

	VDO_ASSERT_LOG_ONLY(pool->borrowed > 0, "shouldn't return more than borrowed");
	pool->borrowed -= 1;
}

/**
 * make_pbn_lock_pool() - Create a new PBN lock pool and all the lock instances it can loan out.
 *
 * @capacity: The number of PBN locks to allocate for the pool.
 * @pool_ptr: A pointer to receive the new pool.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int make_pbn_lock_pool(size_t capacity, struct pbn_lock_pool **pool_ptr)
{
	size_t i;
	struct pbn_lock_pool *pool;
	int result;

	result = vdo_allocate_extended(struct pbn_lock_pool, capacity, idle_pbn_lock,
				       __func__, &pool);
	if (result != VDO_SUCCESS)
		return result;

	pool->capacity = capacity;
	pool->borrowed = capacity;
	INIT_LIST_HEAD(&pool->idle_list);

	for (i = 0; i < capacity; i++)
		return_pbn_lock_to_pool(pool, &pool->locks[i].lock);

	*pool_ptr = pool;
	return VDO_SUCCESS;
}

/**
 * free_pbn_lock_pool() - Free a PBN lock pool.
 * @pool: The lock pool to free.
 *
 * This also frees all the PBN locks it allocated, so the caller must ensure that all locks have
 * been returned to the pool.
 */
static void free_pbn_lock_pool(struct pbn_lock_pool *pool)
{
	if (pool == NULL)
		return;

	VDO_ASSERT_LOG_ONLY(pool->borrowed == 0,
			    "All PBN locks must be returned to the pool before it is freed, but %zu locks are still on loan",
			    pool->borrowed);
	vdo_free(pool);
}

/**
 * borrow_pbn_lock_from_pool() - Borrow a PBN lock from the pool and initialize it with the
 *				 provided type.
 * @pool: The pool from which to borrow.
 * @type: The type with which to initialize the lock.
 * @lock_ptr:  A pointer to receive the borrowed lock.
 *
 * Pools do not grow on demand or allocate memory, so this will fail if the pool is empty. Borrowed
 * locks are still associated with this pool and must be returned to only this pool.
 *
 * Return: VDO_SUCCESS, or VDO_LOCK_ERROR if the pool is empty.
 */
static int __must_check borrow_pbn_lock_from_pool(struct pbn_lock_pool *pool,
						  enum pbn_lock_type type,
						  struct pbn_lock **lock_ptr)
{
	int result;
	struct list_head *idle_entry;
	idle_pbn_lock *idle;

	if (pool->borrowed >= pool->capacity)
		return vdo_log_error_strerror(VDO_LOCK_ERROR,
					      "no free PBN locks left to borrow");
	pool->borrowed += 1;

	result = VDO_ASSERT(!list_empty(&pool->idle_list),
			    "idle list should not be empty if pool not at capacity");
	if (result != VDO_SUCCESS)
		return result;

	idle_entry = pool->idle_list.prev;
	list_del(idle_entry);
	memset(idle_entry, 0, sizeof(*idle_entry));

	idle = list_entry(idle_entry, idle_pbn_lock, entry);
	idle->lock.holder_count = 0;
	set_pbn_lock_type(&idle->lock, type);

	*lock_ptr = &idle->lock;
	return VDO_SUCCESS;
}

/**
 * initialize_zone() - Initialize a physical zone.
 * @vdo: The vdo to which the zone will belong.
 * @zones: The physical_zones to which the zone being initialized belongs
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int initialize_zone(struct vdo *vdo, struct physical_zones *zones)
{
	int result;
	zone_count_t zone_number = zones->zone_count;
	struct physical_zone *zone = &zones->zones[zone_number];

	result = vdo_int_map_create(VDO_LOCK_MAP_CAPACITY, &zone->pbn_operations);
	if (result != VDO_SUCCESS)
		return result;

	result = make_pbn_lock_pool(LOCK_POOL_CAPACITY, &zone->lock_pool);
	if (result != VDO_SUCCESS) {
		vdo_int_map_free(zone->pbn_operations);
		return result;
	}

	zone->zone_number = zone_number;
	zone->thread_id = vdo->thread_config.physical_threads[zone_number];
	zone->allocator = &vdo->depot->allocators[zone_number];
	zone->next = &zones->zones[(zone_number + 1) % vdo->thread_config.physical_zone_count];
	result = vdo_make_default_thread(vdo, zone->thread_id);
	if (result != VDO_SUCCESS) {
		free_pbn_lock_pool(vdo_forget(zone->lock_pool));
		vdo_int_map_free(zone->pbn_operations);
		return result;
	}
	return result;
}

/**
 * vdo_make_physical_zones() - Make the physical zones for a vdo.
 * @vdo: The vdo being constructed
 * @zones_ptr: A pointer to hold the zones
 *
 * Return: VDO_SUCCESS or an error code.
 */
int vdo_make_physical_zones(struct vdo *vdo, struct physical_zones **zones_ptr)
{
	struct physical_zones *zones;
	int result;
	zone_count_t zone_count = vdo->thread_config.physical_zone_count;

	if (zone_count == 0)
		return VDO_SUCCESS;

	result = vdo_allocate_extended(struct physical_zones, zone_count,
				       struct physical_zone, __func__, &zones);
	if (result != VDO_SUCCESS)
		return result;

	for (zones->zone_count = 0; zones->zone_count < zone_count; zones->zone_count++) {
		result = initialize_zone(vdo, zones);
		if (result != VDO_SUCCESS) {
			vdo_free_physical_zones(zones);
			return result;
		}
	}

	*zones_ptr = zones;
	return VDO_SUCCESS;
}

/**
 * vdo_free_physical_zones() - Destroy the physical zones.
 * @zones: The zones to free.
 */
void vdo_free_physical_zones(struct physical_zones *zones)
{
	zone_count_t index;

	if (zones == NULL)
		return;

	for (index = 0; index < zones->zone_count; index++) {
		struct physical_zone *zone = &zones->zones[index];

		free_pbn_lock_pool(vdo_forget(zone->lock_pool));
		vdo_int_map_free(vdo_forget(zone->pbn_operations));
	}

	vdo_free(zones);
}

/**
 * vdo_get_physical_zone_pbn_lock() - Get the lock on a PBN if one exists.
 * @zone: The physical zone responsible for the PBN.
 * @pbn: The physical block number whose lock is desired.
 *
 * Return: The lock or NULL if the PBN is not locked.
 */
struct pbn_lock *vdo_get_physical_zone_pbn_lock(struct physical_zone *zone,
						physical_block_number_t pbn)
{
	return ((zone == NULL) ? NULL : vdo_int_map_get(zone->pbn_operations, pbn));
}

/**
 * vdo_attempt_physical_zone_pbn_lock() - Attempt to lock a physical block in the zone responsible
 *					  for it.
 * @zone: The physical zone responsible for the PBN.
 * @pbn: The physical block number to lock.
 * @type: The type with which to initialize a new lock.
 * @lock_ptr:  A pointer to receive the lock, existing or new.
 *
 * If the PBN is already locked, the existing lock will be returned. Otherwise, a new lock instance
 * will be borrowed from the pool, initialized, and returned. The lock owner will be NULL for a new
 * lock acquired by the caller, who is responsible for setting that field promptly. The lock owner
 * will be non-NULL when there is already an existing lock on the PBN.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_attempt_physical_zone_pbn_lock(struct physical_zone *zone,
				       physical_block_number_t pbn,
				       enum pbn_lock_type type,
				       struct pbn_lock **lock_ptr)
{
	/*
	 * Borrow and prepare a lock from the pool so we don't have to do two int_map accesses in
	 * the common case of no lock contention.
	 */
	struct pbn_lock *lock, *new_lock = NULL;
	int result;

	result = borrow_pbn_lock_from_pool(zone->lock_pool, type, &new_lock);
	if (result != VDO_SUCCESS) {
		VDO_ASSERT_LOG_ONLY(false, "must always be able to borrow a PBN lock");
		return result;
	}

	result = vdo_int_map_put(zone->pbn_operations, pbn, new_lock, false,
				 (void **) &lock);
	if (result != VDO_SUCCESS) {
		return_pbn_lock_to_pool(zone->lock_pool, new_lock);
		return result;
	}

	if (lock != NULL) {
		/* The lock is already held, so we don't need the borrowed one. */
		return_pbn_lock_to_pool(zone->lock_pool, vdo_forget(new_lock));
		result = VDO_ASSERT(lock->holder_count > 0, "physical block %llu lock held",
				    (unsigned long long) pbn);
		if (result != VDO_SUCCESS)
			return result;
		*lock_ptr = lock;
	} else {
		*lock_ptr = new_lock;
	}
	return VDO_SUCCESS;
}

/**
 * allocate_and_lock_block() - Attempt to allocate a block from this zone.
 * @allocation: The struct allocation of the data_vio attempting to allocate.
 *
 * If a block is allocated, the recipient will also hold a lock on it.
 *
 * Return: VDO_SUCCESS if a block was allocated, or an error code.
 */
static int allocate_and_lock_block(struct allocation *allocation)
{
	int result;
	struct pbn_lock *lock;

	VDO_ASSERT_LOG_ONLY(allocation->lock == NULL,
			    "must not allocate a block while already holding a lock on one");

	result = vdo_allocate_block(allocation->zone->allocator, &allocation->pbn);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_attempt_physical_zone_pbn_lock(allocation->zone, allocation->pbn,
						    allocation->write_lock_type, &lock);
	if (result != VDO_SUCCESS)
		return result;

	if (lock->holder_count > 0) {
		/* This block is already locked, which should be impossible. */
		return vdo_log_error_strerror(VDO_LOCK_ERROR,
					      "Newly allocated block %llu was spuriously locked (holder_count=%u)",
					      (unsigned long long) allocation->pbn,
					      lock->holder_count);
	}

	/* We've successfully acquired a new lock, so mark it as ours. */
	lock->holder_count += 1;
	allocation->lock = lock;
	vdo_assign_pbn_lock_provisional_reference(lock);
	return VDO_SUCCESS;
}

/**
 * retry_allocation() - Retry allocating a block now that we're done waiting for scrubbing.
 * @waiter: The allocating_vio that was waiting to allocate.
 * @context: The context (unused).
 */
static void retry_allocation(struct vdo_waiter *waiter, void *context __always_unused)
{
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);

	/* Now that some slab has scrubbed, restart the allocation process. */
	data_vio->allocation.wait_for_clean_slab = false;
	data_vio->allocation.first_allocation_zone = data_vio->allocation.zone->zone_number;
	continue_data_vio(data_vio);
}

/**
 * continue_allocating() - Continue searching for an allocation by enqueuing to wait for scrubbing
 *			   or switching to the next zone.
 * @data_vio: The data_vio attempting to get an allocation.
 *
 * This method should only be called from the error handler set in data_vio_allocate_data_block.
 *
 * Return: true if the allocation process has continued in another zone.
 */
static bool continue_allocating(struct data_vio *data_vio)
{
	struct allocation *allocation = &data_vio->allocation;
	struct physical_zone *zone = allocation->zone;
	struct vdo_completion *completion = &data_vio->vio.completion;
	int result = VDO_SUCCESS;
	bool was_waiting = allocation->wait_for_clean_slab;
	bool tried_all = (allocation->first_allocation_zone == zone->next->zone_number);

	vdo_reset_completion(completion);

	if (tried_all && !was_waiting) {
		/*
		 * We've already looked in all the zones, and found nothing. So go through the
		 * zones again, and wait for each to scrub before trying to allocate.
		 */
		allocation->wait_for_clean_slab = true;
		allocation->first_allocation_zone = zone->zone_number;
	}

	if (allocation->wait_for_clean_slab) {
		data_vio->waiter.callback = retry_allocation;
		result = vdo_enqueue_clean_slab_waiter(zone->allocator,
						       &data_vio->waiter);
		if (result == VDO_SUCCESS) {
			/* We've enqueued to wait for a slab to be scrubbed. */
			return true;
		}

		if ((result != VDO_NO_SPACE) || (was_waiting && tried_all)) {
			vdo_set_completion_result(completion, result);
			return false;
		}
	}

	allocation->zone = zone->next;
	completion->callback_thread_id = allocation->zone->thread_id;
	vdo_launch_completion(completion);
	return true;
}

/**
 * vdo_allocate_block_in_zone() - Attempt to allocate a block in the current physical zone, and if
 *				  that fails try the next if possible.
 * @data_vio: The data_vio needing an allocation.
 *
 * Return: true if a block was allocated, if not the data_vio will have been dispatched so the
 *         caller must not touch it.
 */
bool vdo_allocate_block_in_zone(struct data_vio *data_vio)
{
	int result = allocate_and_lock_block(&data_vio->allocation);

	if (result == VDO_SUCCESS)
		return true;

	if ((result != VDO_NO_SPACE) || !continue_allocating(data_vio))
		continue_data_vio_with_error(data_vio, result);

	return false;
}

/**
 * vdo_release_physical_zone_pbn_lock() - Release a physical block lock if it is held and return it
 *                                        to the lock pool.
 * @zone: The physical zone in which the lock was obtained.
 * @locked_pbn: The physical block number to unlock.
 * @lock: The lock being released.
 *
 * It must be the last live reference, as if the memory were being freed (the
 * lock memory will re-initialized or zeroed).
 */
void vdo_release_physical_zone_pbn_lock(struct physical_zone *zone,
					physical_block_number_t locked_pbn,
					struct pbn_lock *lock)
{
	struct pbn_lock *holder;

	if (lock == NULL)
		return;

	VDO_ASSERT_LOG_ONLY(lock->holder_count > 0,
			    "should not be releasing a lock that is not held");

	lock->holder_count -= 1;
	if (lock->holder_count > 0) {
		/* The lock was shared and is still referenced, so don't release it yet. */
		return;
	}

	holder = vdo_int_map_remove(zone->pbn_operations, locked_pbn);
	VDO_ASSERT_LOG_ONLY((lock == holder), "physical block lock mismatch for block %llu",
			    (unsigned long long) locked_pbn);

	release_pbn_lock_provisional_reference(lock, locked_pbn, zone->allocator);
	return_pbn_lock_to_pool(zone->lock_pool, lock);
}

/**
 * vdo_dump_physical_zone() - Dump information about a physical zone to the log for debugging.
 * @zone: The zone to dump.
 */
void vdo_dump_physical_zone(const struct physical_zone *zone)
{
	vdo_dump_block_allocator(zone->allocator);
}
