/*
 * Copyright (C) 2001 Sistina Software (UK) Limited
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/bio.h>
#include <linux/slab.h>

struct tt_internal {
	struct target_type tt;

	struct list_head list;
	long use;
};

static LIST_HEAD(_targets);
static DECLARE_RWSEM(_lock);

#define DM_MOD_NAME_SIZE 32

static inline struct tt_internal *__find_target_type(const char *name)
{
	struct tt_internal *ti;

	list_for_each_entry (ti, &_targets, list)
		if (!strcmp(name, ti->tt.name))
			return ti;

	return NULL;
}

static struct tt_internal *get_target_type(const char *name)
{
	struct tt_internal *ti;

	down_read(&_lock);

	ti = __find_target_type(name);
	if (ti) {
		if ((ti->use == 0) && !try_module_get(ti->tt.module))
			ti = NULL;
		else
			ti->use++;
	}

	up_read(&_lock);
	return ti;
}

static void load_module(const char *name)
{
	request_module("dm-%s", name);
}

struct target_type *dm_get_target_type(const char *name)
{
	struct tt_internal *ti = get_target_type(name);

	if (!ti) {
		load_module(name);
		ti = get_target_type(name);
	}

	return ti ? &ti->tt : NULL;
}

void dm_put_target_type(struct target_type *t)
{
	struct tt_internal *ti = (struct tt_internal *) t;

	down_read(&_lock);
	if (--ti->use == 0)
		module_put(ti->tt.module);

	if (ti->use < 0)
		BUG();
	up_read(&_lock);

	return;
}

static struct tt_internal *alloc_target(struct target_type *t)
{
	struct tt_internal *ti = kmalloc(sizeof(*ti), GFP_KERNEL);

	if (ti) {
		memset(ti, 0, sizeof(*ti));
		ti->tt = *t;
	}

	return ti;
}


int dm_target_iterate(void (*iter_func)(struct target_type *tt,
					void *param), void *param)
{
	struct tt_internal *ti;

	down_read(&_lock);
	list_for_each_entry (ti, &_targets, list)
		iter_func(&ti->tt, param);
	up_read(&_lock);

	return 0;
}

int dm_register_target(struct target_type *t)
{
	int rv = 0;
	struct tt_internal *ti = alloc_target(t);

	if (!ti)
		return -ENOMEM;

	down_write(&_lock);
	if (__find_target_type(t->name))
		rv = -EEXIST;
	else
		list_add(&ti->list, &_targets);

	up_write(&_lock);
	if (rv)
		kfree(ti);
	return rv;
}

int dm_unregister_target(struct target_type *t)
{
	struct tt_internal *ti;

	down_write(&_lock);
	if (!(ti = __find_target_type(t->name))) {
		up_write(&_lock);
		return -EINVAL;
	}

	if (ti->use) {
		up_write(&_lock);
		return -ETXTBSY;
	}

	list_del(&ti->list);
	kfree(ti);

	up_write(&_lock);
	return 0;
}

/*
 * io-err: always fails an io, useful for bringing
 * up LVs that have holes in them.
 */
static int io_err_ctr(struct dm_target *ti, unsigned int argc, char **args)
{
	return 0;
}

static void io_err_dtr(struct dm_target *ti)
{
	/* empty */
}

static int io_err_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	return -EIO;
}

static struct target_type error_target = {
	.name = "error",
	.version = {1, 0, 1},
	.ctr  = io_err_ctr,
	.dtr  = io_err_dtr,
	.map  = io_err_map,
};

int __init dm_target_init(void)
{
	return dm_register_target(&error_target);
}

void dm_target_exit(void)
{
	if (dm_unregister_target(&error_target))
		DMWARN("error target unregistration failed");
}

EXPORT_SYMBOL(dm_register_target);
EXPORT_SYMBOL(dm_unregister_target);
