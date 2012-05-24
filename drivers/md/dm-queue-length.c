/*
 * Copyright (C) 2004-2005 IBM Corp.  All Rights Reserved.
 * Copyright (C) 2006-2009 NEC Corporation.
 *
 * dm-queue-length.c
 *
 * Module Author: Stefan Bader, IBM
 * Modified by: Kiyoshi Ueda, NEC
 *
 * This file is released under the GPL.
 *
 * queue-length path selector - choose a path with the least number of
 * in-flight I/Os.
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/atomic.h>

#define DM_MSG_PREFIX	"multipath queue-length"
#define QL_MIN_IO	128
#define QL_VERSION	"0.1.0"

struct selector {
	struct list_head	valid_paths;
	struct list_head	failed_paths;
};

struct path_info {
	struct list_head	list;
	struct dm_path		*path;
	unsigned		repeat_count;
	atomic_t		qlen;	/* the number of in-flight I/Os */
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->failed_paths);
	}

	return s;
}

static int ql_create(struct path_selector *ps, unsigned argc, char **argv)
{
	struct selector *s = alloc_selector();

	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void ql_free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

static void ql_destroy(struct path_selector *ps)
{
	struct selector *s = ps->context;

	ql_free_paths(&s->valid_paths);
	ql_free_paths(&s->failed_paths);
	kfree(s);
	ps->context = NULL;
}

static int ql_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned maxlen)
{
	unsigned sz = 0;
	struct path_info *pi;

	/* When called with NULL path, return selector status/args. */
	if (!path)
		DMEMIT("0 ");
	else {
		pi = path->pscontext;

		switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("%d ", atomic_read(&pi->qlen));
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("%u ", pi->repeat_count);
			break;
		}
	}

	return sz;
}

static int ql_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = ps->context;
	struct path_info *pi;
	unsigned repeat_count = QL_MIN_IO;
	char dummy;

	/*
	 * Arguments: [<repeat_count>]
	 * 	<repeat_count>: The number of I/Os before switching path.
	 * 			If not given, default (QL_MIN_IO) is used.
	 */
	if (argc > 1) {
		*error = "queue-length ps: incorrect number of arguments";
		return -EINVAL;
	}

	if ((argc == 1) && (sscanf(argv[0], "%u%c", &repeat_count, &dummy) != 1)) {
		*error = "queue-length ps: invalid repeat count";
		return -EINVAL;
	}

	/* Allocate the path information structure */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "queue-length ps: Error allocating path information";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;
	atomic_set(&pi->qlen, 0);

	path->pscontext = pi;

	list_add_tail(&pi->list, &s->valid_paths);

	return 0;
}

static void ql_fail_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;

	list_move(&pi->list, &s->failed_paths);
}

static int ql_reinstate_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;

	list_move_tail(&pi->list, &s->valid_paths);

	return 0;
}

/*
 * Select a path having the minimum number of in-flight I/Os
 */
static struct dm_path *ql_select_path(struct path_selector *ps,
				      unsigned *repeat_count, size_t nr_bytes)
{
	struct selector *s = ps->context;
	struct path_info *pi = NULL, *best = NULL;

	if (list_empty(&s->valid_paths))
		return NULL;

	/* Change preferred (first in list) path to evenly balance. */
	list_move_tail(s->valid_paths.next, &s->valid_paths);

	list_for_each_entry(pi, &s->valid_paths, list) {
		if (!best ||
		    (atomic_read(&pi->qlen) < atomic_read(&best->qlen)))
			best = pi;

		if (!atomic_read(&best->qlen))
			break;
	}

	if (!best)
		return NULL;

	*repeat_count = best->repeat_count;

	return best->path;
}

static int ql_start_io(struct path_selector *ps, struct dm_path *path,
		       size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_inc(&pi->qlen);

	return 0;
}

static int ql_end_io(struct path_selector *ps, struct dm_path *path,
		     size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_dec(&pi->qlen);

	return 0;
}

static struct path_selector_type ql_ps = {
	.name		= "queue-length",
	.module		= THIS_MODULE,
	.table_args	= 1,
	.info_args	= 1,
	.create		= ql_create,
	.destroy	= ql_destroy,
	.status		= ql_status,
	.add_path	= ql_add_path,
	.fail_path	= ql_fail_path,
	.reinstate_path	= ql_reinstate_path,
	.select_path	= ql_select_path,
	.start_io	= ql_start_io,
	.end_io		= ql_end_io,
};

static int __init dm_ql_init(void)
{
	int r = dm_register_path_selector(&ql_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " QL_VERSION " loaded");

	return r;
}

static void __exit dm_ql_exit(void)
{
	int r = dm_unregister_path_selector(&ql_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_ql_init);
module_exit(dm_ql_exit);

MODULE_AUTHOR("Stefan Bader <Stefan.Bader at de.ibm.com>");
MODULE_DESCRIPTION(
	"(C) Copyright IBM Corp. 2004,2005   All Rights Reserved.\n"
	DM_NAME " path selector to balance the number of in-flight I/Os"
);
MODULE_LICENSE("GPL");
