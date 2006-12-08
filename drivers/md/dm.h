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
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#define DM_NAME "device-mapper"

#define DMERR(f, arg...) printk(KERN_ERR DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#define DMWARN(f, arg...) printk(KERN_WARNING DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#define DMINFO(f, arg...) printk(KERN_INFO DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#ifdef CONFIG_DM_DEBUG
#  define DMDEBUG(f, arg...) printk(KERN_DEBUG DM_NAME ": " DM_MSG_PREFIX " DEBUG: " f "\n", ## arg)
#else
#  define DMDEBUG(f, arg...) do {} while (0)
#endif

#define DMEMIT(x...) sz += ((sz >= maxlen) ? \
			  0 : scnprintf(result + sz, maxlen - sz, x))

#define SECTOR_SHIFT 9

/*
 * Suspend feature flags
 */
#define DM_SUSPEND_LOCKFS_FLAG		(1 << 0)

/*
 * List of devices that a metadevice uses and should open/close.
 */
struct dm_dev {
	struct list_head list;

	atomic_t count;
	int mode;
	struct block_device *bdev;
	char name[16];
};

struct dm_table;

/*-----------------------------------------------------------------
 * Internal table functions.
 *---------------------------------------------------------------*/
void dm_table_event_callback(struct dm_table *t,
			     void (*fn)(void *), void *context);
struct dm_target *dm_table_get_target(struct dm_table *t, unsigned int index);
struct dm_target *dm_table_find_target(struct dm_table *t, sector_t sector);
void dm_table_set_restrictions(struct dm_table *t, struct request_queue *q);
struct list_head *dm_table_get_devices(struct dm_table *t);
void dm_table_presuspend_targets(struct dm_table *t);
void dm_table_postsuspend_targets(struct dm_table *t);
int dm_table_resume_targets(struct dm_table *t);
int dm_table_any_congested(struct dm_table *t, int bdi_bits);
void dm_table_unplug_all(struct dm_table *t);
int dm_table_flush_all(struct dm_table *t);

/*-----------------------------------------------------------------
 * A registry of target types.
 *---------------------------------------------------------------*/
int dm_target_init(void);
void dm_target_exit(void);
struct target_type *dm_get_target_type(const char *name);
void dm_put_target_type(struct target_type *t);
int dm_target_iterate(void (*iter_func)(struct target_type *tt,
					void *param), void *param);

/*-----------------------------------------------------------------
 * Useful inlines.
 *---------------------------------------------------------------*/
static inline int array_too_big(unsigned long fixed, unsigned long obj,
				unsigned long num)
{
	return (num > (ULONG_MAX - fixed) / obj);
}

/*
 * Ceiling(n / sz)
 */
#define dm_div_up(n, sz) (((n) + (sz) - 1) / (sz))

#define dm_sector_div_up(n, sz) ( \
{ \
	sector_t _r = ((n) + (sz) - 1); \
	sector_div(_r, (sz)); \
	_r; \
} \
)

/*
 * ceiling(n / size) * size
 */
#define dm_round_up(n, sz) (dm_div_up((n), (sz)) * (sz))

static inline sector_t to_sector(unsigned long n)
{
	return (n >> 9);
}

static inline unsigned long to_bytes(sector_t n)
{
	return (n << 9);
}

int dm_split_args(int *argc, char ***argvp, char *input);

/*
 * The device-mapper can be driven through one of two interfaces;
 * ioctl or filesystem, depending which patch you have applied.
 */
int dm_interface_init(void);
void dm_interface_exit(void);

/*
 * Targets for linear and striped mappings
 */
int dm_linear_init(void);
void dm_linear_exit(void);

int dm_stripe_init(void);
void dm_stripe_exit(void);

void *dm_vcalloc(unsigned long nmemb, unsigned long elem_size);
union map_info *dm_get_mapinfo(struct bio *bio);
int dm_open_count(struct mapped_device *md);
int dm_lock_for_deletion(struct mapped_device *md);

#endif
