/*
 * Internal header file for device mapper
 *
 * Copyright (C) 2001, 2002 Sistina Software
 * Copyright (C) 2004-2006 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#ifndef DM_INTERNAL_H
#define DM_INTERNAL_H

#include <linux/fs.h>
#include <linux/device-mapper.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/hdreg.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/refcount.h>

#include "dm-stats.h"

/*
 * Suspend feature flags
 */
#define DM_SUSPEND_LOCKFS_FLAG		(1 << 0)
#define DM_SUSPEND_NOFLUSH_FLAG		(1 << 1)

/*
 * Status feature flags
 */
#define DM_STATUS_NOFLUSH_FLAG		(1 << 0)

/*
 * List of devices that a metadevice uses and should open/close.
 */
struct dm_dev_internal {
	struct list_head list;
	refcount_t count;
	struct dm_dev *dm_dev;
};

struct dm_table;
struct dm_md_mempools;

/*-----------------------------------------------------------------
 * Internal table functions.
 *---------------------------------------------------------------*/
void dm_table_event_callback(struct dm_table *t,
			     void (*fn)(void *), void *context);
struct dm_target *dm_table_get_target(struct dm_table *t, unsigned int index);
struct dm_target *dm_table_find_target(struct dm_table *t, sector_t sector);
bool dm_table_has_no_data_devices(struct dm_table *table);
int dm_calculate_queue_limits(struct dm_table *table,
			      struct queue_limits *limits);
void dm_table_set_restrictions(struct dm_table *t, struct request_queue *q,
			       struct queue_limits *limits);
struct list_head *dm_table_get_devices(struct dm_table *t);
void dm_table_presuspend_targets(struct dm_table *t);
void dm_table_presuspend_undo_targets(struct dm_table *t);
void dm_table_postsuspend_targets(struct dm_table *t);
int dm_table_resume_targets(struct dm_table *t);
int dm_table_any_congested(struct dm_table *t, int bdi_bits);
enum dm_queue_mode dm_table_get_type(struct dm_table *t);
struct target_type *dm_table_get_immutable_target_type(struct dm_table *t);
struct dm_target *dm_table_get_immutable_target(struct dm_table *t);
struct dm_target *dm_table_get_wildcard_target(struct dm_table *t);
bool dm_table_bio_based(struct dm_table *t);
bool dm_table_request_based(struct dm_table *t);
void dm_table_free_md_mempools(struct dm_table *t);
struct dm_md_mempools *dm_table_get_md_mempools(struct dm_table *t);
bool dm_table_supports_dax(struct dm_table *t, iterate_devices_callout_fn fn,
			   int *blocksize);
int device_supports_dax(struct dm_target *ti, struct dm_dev *dev,
			   sector_t start, sector_t len, void *data);

void dm_lock_md_type(struct mapped_device *md);
void dm_unlock_md_type(struct mapped_device *md);
void dm_set_md_type(struct mapped_device *md, enum dm_queue_mode type);
enum dm_queue_mode dm_get_md_type(struct mapped_device *md);
struct target_type *dm_get_immutable_target_type(struct mapped_device *md);

int dm_setup_md_queue(struct mapped_device *md, struct dm_table *t);

/*
 * To check whether the target type is bio-based or not (request-based).
 */
#define dm_target_bio_based(t) ((t)->type->map != NULL)

/*
 * To check whether the target type is request-based or not (bio-based).
 */
#define dm_target_request_based(t) ((t)->type->clone_and_map_rq != NULL)

/*
 * To check whether the target type is a hybrid (capable of being
 * either request-based or bio-based).
 */
#define dm_target_hybrid(t) (dm_target_bio_based(t) && dm_target_request_based(t))

/*-----------------------------------------------------------------
 * A registry of target types.
 *---------------------------------------------------------------*/
int dm_target_init(void);
void dm_target_exit(void);
struct target_type *dm_get_target_type(const char *name);
void dm_put_target_type(struct target_type *tt);
int dm_target_iterate(void (*iter_func)(struct target_type *tt,
					void *param), void *param);

int dm_split_args(int *argc, char ***argvp, char *input);

/*
 * Is this mapped_device being deleted?
 */
int dm_deleting_md(struct mapped_device *md);

/*
 * Is this mapped_device suspended?
 */
int dm_suspended_md(struct mapped_device *md);

/*
 * Internal suspend and resume methods.
 */
int dm_suspended_internally_md(struct mapped_device *md);
void dm_internal_suspend_fast(struct mapped_device *md);
void dm_internal_resume_fast(struct mapped_device *md);
void dm_internal_suspend_noflush(struct mapped_device *md);
void dm_internal_resume(struct mapped_device *md);

/*
 * Test if the device is scheduled for deferred remove.
 */
int dm_test_deferred_remove_flag(struct mapped_device *md);

/*
 * Try to remove devices marked for deferred removal.
 */
void dm_deferred_remove(void);

/*
 * The device-mapper can be driven through one of two interfaces;
 * ioctl or filesystem, depending which patch you have applied.
 */
int dm_interface_init(void);
void dm_interface_exit(void);

/*
 * sysfs interface
 */
int dm_sysfs_init(struct mapped_device *md);
void dm_sysfs_exit(struct mapped_device *md);
struct kobject *dm_kobject(struct mapped_device *md);
struct mapped_device *dm_get_from_kobject(struct kobject *kobj);

/*
 * The kobject helper
 */
void dm_kobject_release(struct kobject *kobj);

/*
 * Targets for linear and striped mappings
 */
int dm_linear_init(void);
void dm_linear_exit(void);

int dm_stripe_init(void);
void dm_stripe_exit(void);

/*
 * mapped_device operations
 */
void dm_destroy(struct mapped_device *md);
void dm_destroy_immediate(struct mapped_device *md);
int dm_open_count(struct mapped_device *md);
int dm_lock_for_deletion(struct mapped_device *md, bool mark_deferred, bool only_deferred);
int dm_cancel_deferred_remove(struct mapped_device *md);
int dm_request_based(struct mapped_device *md);
sector_t dm_get_size(struct mapped_device *md);
struct request_queue *dm_get_md_queue(struct mapped_device *md);
int dm_get_table_device(struct mapped_device *md, dev_t dev, fmode_t mode,
			struct dm_dev **result);
void dm_put_table_device(struct mapped_device *md, struct dm_dev *d);
struct dm_stats *dm_get_stats(struct mapped_device *md);

int dm_kobject_uevent(struct mapped_device *md, enum kobject_action action,
		      unsigned cookie);

void dm_internal_suspend(struct mapped_device *md);
void dm_internal_resume(struct mapped_device *md);

int dm_io_init(void);
void dm_io_exit(void);

int dm_kcopyd_init(void);
void dm_kcopyd_exit(void);

/*
 * Mempool operations
 */
struct dm_md_mempools *dm_alloc_md_mempools(struct mapped_device *md, enum dm_queue_mode type,
					    unsigned integrity, unsigned per_bio_data_size,
					    unsigned min_pool_size);
void dm_free_md_mempools(struct dm_md_mempools *pools);

/*
 * Various helpers
 */
unsigned dm_get_reserved_bio_based_ios(void);

#endif
