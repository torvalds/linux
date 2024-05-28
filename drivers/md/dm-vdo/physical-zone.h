/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_PHYSICAL_ZONE_H
#define VDO_PHYSICAL_ZONE_H

#include <linux/atomic.h>

#include "types.h"

/*
 * The type of a PBN lock.
 */
enum pbn_lock_type {
	VIO_READ_LOCK,
	VIO_WRITE_LOCK,
	VIO_BLOCK_MAP_WRITE_LOCK,
};

struct pbn_lock_implementation;

/*
 * A PBN lock.
 */
struct pbn_lock {
	/* The implementation of the lock */
	const struct pbn_lock_implementation *implementation;

	/* The number of VIOs holding or sharing this lock */
	data_vio_count_t holder_count;
	/*
	 * The number of compressed block writers holding a share of this lock while they are
	 * acquiring a reference to the PBN.
	 */
	u8 fragment_locks;

	/* Whether the locked PBN has been provisionally referenced on behalf of the lock holder. */
	bool has_provisional_reference;

	/*
	 * For read locks, the number of references that were known to be available on the locked
	 * block at the time the lock was acquired.
	 */
	u8 increment_limit;

	/*
	 * For read locks, the number of data_vios that have tried to claim one of the available
	 * increments during the lifetime of the lock. Each claim will first increment this
	 * counter, so it can exceed the increment limit.
	 */
	atomic_t increments_claimed;
};

struct physical_zone {
	/* Which physical zone this is */
	zone_count_t zone_number;
	/* The thread ID for this zone */
	thread_id_t thread_id;
	/* In progress operations keyed by PBN */
	struct int_map *pbn_operations;
	/* Pool of unused pbn_lock instances */
	struct pbn_lock_pool *lock_pool;
	/* The block allocator for this zone */
	struct block_allocator *allocator;
	/* The next zone from which to attempt an allocation */
	struct physical_zone *next;
};

struct physical_zones {
	/* The number of zones */
	zone_count_t zone_count;
	/* The physical zones themselves */
	struct physical_zone zones[];
};

bool __must_check vdo_is_pbn_read_lock(const struct pbn_lock *lock);
void vdo_downgrade_pbn_write_lock(struct pbn_lock *lock, bool compressed_write);
bool __must_check vdo_claim_pbn_lock_increment(struct pbn_lock *lock);

/**
 * vdo_pbn_lock_has_provisional_reference() - Check whether a PBN lock has a provisional reference.
 * @lock: The PBN lock.
 */
static inline bool vdo_pbn_lock_has_provisional_reference(struct pbn_lock *lock)
{
	return ((lock != NULL) && lock->has_provisional_reference);
}

void vdo_assign_pbn_lock_provisional_reference(struct pbn_lock *lock);
void vdo_unassign_pbn_lock_provisional_reference(struct pbn_lock *lock);

int __must_check vdo_make_physical_zones(struct vdo *vdo,
					 struct physical_zones **zones_ptr);

void vdo_free_physical_zones(struct physical_zones *zones);

struct pbn_lock * __must_check vdo_get_physical_zone_pbn_lock(struct physical_zone *zone,
							      physical_block_number_t pbn);

int __must_check vdo_attempt_physical_zone_pbn_lock(struct physical_zone *zone,
						    physical_block_number_t pbn,
						    enum pbn_lock_type type,
						    struct pbn_lock **lock_ptr);

bool __must_check vdo_allocate_block_in_zone(struct data_vio *data_vio);

void vdo_release_physical_zone_pbn_lock(struct physical_zone *zone,
					physical_block_number_t locked_pbn,
					struct pbn_lock *lock);

void vdo_dump_physical_zone(const struct physical_zone *zone);

#endif /* VDO_PHYSICAL_ZONE_H */
