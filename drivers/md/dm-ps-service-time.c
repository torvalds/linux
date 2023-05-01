// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2009 NEC Corporation.  All Rights Reserved.
 *
 * Module Author: Kiyoshi Ueda
 *
 * This file is released under the GPL.
 *
 * Throughput oriented path selector.
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/slab.h>
#include <linux/module.h>

#define DM_MSG_PREFIX	"multipath service-time"
#define ST_MIN_IO	1
#define ST_MAX_RELATIVE_THROUGHPUT	100
#define ST_MAX_RELATIVE_THROUGHPUT_SHIFT	7
#define ST_MAX_INFLIGHT_SIZE	((size_t)-1 >> ST_MAX_RELATIVE_THROUGHPUT_SHIFT)
#define ST_VERSION	"0.3.0"

struct selector {
	struct list_head valid_paths;
	struct list_head failed_paths;
	spinlock_t lock;
};

struct path_info {
	struct list_head list;
	struct dm_path *path;
	unsigned int repeat_count;
	unsigned int relative_throughput;
	atomic_t in_flight_size;	/* Total size of in-flight I/Os */
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->failed_paths);
		spin_lock_init(&s->lock);
	}

	return s;
}

static int st_create(struct path_selector *ps, unsigned int argc, char **argv)
{
	struct selector *s = alloc_selector();

	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

static void st_destroy(struct path_selector *ps)
{
	struct selector *s = ps->context;

	free_paths(&s->valid_paths);
	free_paths(&s->failed_paths);
	kfree(s);
	ps->context = NULL;
}

static int st_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	unsigned int sz = 0;
	struct path_info *pi;

	if (!path)
		DMEMIT("0 ");
	else {
		pi = path->pscontext;

		switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("%d %u ", atomic_read(&pi->in_flight_size),
			       pi->relative_throughput);
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("%u %u ", pi->repeat_count,
			       pi->relative_throughput);
			break;
		case STATUSTYPE_IMA:
			result[0] = '\0';
			break;
		}
	}

	return sz;
}

static int st_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = ps->context;
	struct path_info *pi;
	unsigned int repeat_count = ST_MIN_IO;
	unsigned int relative_throughput = 1;
	char dummy;
	unsigned long flags;

	/*
	 * Arguments: [<repeat_count> [<relative_throughput>]]
	 *	<repeat_count>: The number of I/Os before switching path.
	 *			If not given, default (ST_MIN_IO) is used.
	 *	<relative_throughput>: The relative throughput value of
	 *			the path among all paths in the path-group.
	 *			The valid range: 0-<ST_MAX_RELATIVE_THROUGHPUT>
	 *			If not given, minimum value '1' is used.
	 *			If '0' is given, the path isn't selected while
	 *			other paths having a positive value are	available.
	 */
	if (argc > 2) {
		*error = "service-time ps: incorrect number of arguments";
		return -EINVAL;
	}

	if (argc && (sscanf(argv[0], "%u%c", &repeat_count, &dummy) != 1)) {
		*error = "service-time ps: invalid repeat count";
		return -EINVAL;
	}

	if (repeat_count > 1) {
		DMWARN_LIMIT("repeat_count > 1 is deprecated, using 1 instead");
		repeat_count = 1;
	}

	if ((argc == 2) &&
	    (sscanf(argv[1], "%u%c", &relative_throughput, &dummy) != 1 ||
	     relative_throughput > ST_MAX_RELATIVE_THROUGHPUT)) {
		*error = "service-time ps: invalid relative_throughput value";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "service-time ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;
	pi->relative_throughput = relative_throughput;
	atomic_set(&pi->in_flight_size, 0);

	path->pscontext = pi;

	spin_lock_irqsave(&s->lock, flags);
	list_add_tail(&pi->list, &s->valid_paths);
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

static void st_fail_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	list_move(&pi->list, &s->failed_paths);
	spin_unlock_irqrestore(&s->lock, flags);
}

static int st_reinstate_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = ps->context;
	struct path_info *pi = path->pscontext;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	list_move_tail(&pi->list, &s->valid_paths);
	spin_unlock_irqrestore(&s->lock, flags);

	return 0;
}

/*
 * Compare the estimated service time of 2 paths, pi1 and pi2,
 * for the incoming I/O.
 *
 * Returns:
 * < 0 : pi1 is better
 * 0   : no difference between pi1 and pi2
 * > 0 : pi2 is better
 *
 * Description:
 * Basically, the service time is estimated by:
 *     ('pi->in-flight-size' + 'incoming') / 'pi->relative_throughput'
 * To reduce the calculation, some optimizations are made.
 * (See comments inline)
 */
static int st_compare_load(struct path_info *pi1, struct path_info *pi2,
			   size_t incoming)
{
	size_t sz1, sz2, st1, st2;

	sz1 = atomic_read(&pi1->in_flight_size);
	sz2 = atomic_read(&pi2->in_flight_size);

	/*
	 * Case 1: Both have same throughput value. Choose less loaded path.
	 */
	if (pi1->relative_throughput == pi2->relative_throughput)
		return sz1 - sz2;

	/*
	 * Case 2a: Both have same load. Choose higher throughput path.
	 * Case 2b: One path has no throughput value. Choose the other one.
	 */
	if (sz1 == sz2 ||
	    !pi1->relative_throughput || !pi2->relative_throughput)
		return pi2->relative_throughput - pi1->relative_throughput;

	/*
	 * Case 3: Calculate service time. Choose faster path.
	 *         Service time using pi1:
	 *             st1 = (sz1 + incoming) / pi1->relative_throughput
	 *         Service time using pi2:
	 *             st2 = (sz2 + incoming) / pi2->relative_throughput
	 *
	 *         To avoid the division, transform the expression to use
	 *         multiplication.
	 *         Because ->relative_throughput > 0 here, if st1 < st2,
	 *         the expressions below are the same meaning:
	 *             (sz1 + incoming) / pi1->relative_throughput <
	 *                 (sz2 + incoming) / pi2->relative_throughput
	 *             (sz1 + incoming) * pi2->relative_throughput <
	 *                 (sz2 + incoming) * pi1->relative_throughput
	 *         So use the later one.
	 */
	sz1 += incoming;
	sz2 += incoming;
	if (unlikely(sz1 >= ST_MAX_INFLIGHT_SIZE ||
		     sz2 >= ST_MAX_INFLIGHT_SIZE)) {
		/*
		 * Size may be too big for multiplying pi->relative_throughput
		 * and overflow.
		 * To avoid the overflow and mis-selection, shift down both.
		 */
		sz1 >>= ST_MAX_RELATIVE_THROUGHPUT_SHIFT;
		sz2 >>= ST_MAX_RELATIVE_THROUGHPUT_SHIFT;
	}
	st1 = sz1 * pi2->relative_throughput;
	st2 = sz2 * pi1->relative_throughput;
	if (st1 != st2)
		return st1 - st2;

	/*
	 * Case 4: Service time is equal. Choose higher throughput path.
	 */
	return pi2->relative_throughput - pi1->relative_throughput;
}

static struct dm_path *st_select_path(struct path_selector *ps, size_t nr_bytes)
{
	struct selector *s = ps->context;
	struct path_info *pi = NULL, *best = NULL;
	struct dm_path *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (list_empty(&s->valid_paths))
		goto out;

	list_for_each_entry(pi, &s->valid_paths, list)
		if (!best || (st_compare_load(pi, best, nr_bytes) < 0))
			best = pi;

	if (!best)
		goto out;

	/* Move most recently used to least preferred to evenly balance. */
	list_move_tail(&best->list, &s->valid_paths);

	ret = best->path;
out:
	spin_unlock_irqrestore(&s->lock, flags);
	return ret;
}

static int st_start_io(struct path_selector *ps, struct dm_path *path,
		       size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_add(nr_bytes, &pi->in_flight_size);

	return 0;
}

static int st_end_io(struct path_selector *ps, struct dm_path *path,
		     size_t nr_bytes, u64 start_time)
{
	struct path_info *pi = path->pscontext;

	atomic_sub(nr_bytes, &pi->in_flight_size);

	return 0;
}

static struct path_selector_type st_ps = {
	.name		= "service-time",
	.module		= THIS_MODULE,
	.table_args	= 2,
	.info_args	= 2,
	.create		= st_create,
	.destroy	= st_destroy,
	.status		= st_status,
	.add_path	= st_add_path,
	.fail_path	= st_fail_path,
	.reinstate_path	= st_reinstate_path,
	.select_path	= st_select_path,
	.start_io	= st_start_io,
	.end_io		= st_end_io,
};

static int __init dm_st_init(void)
{
	int r = dm_register_path_selector(&st_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " ST_VERSION " loaded");

	return r;
}

static void __exit dm_st_exit(void)
{
	int r = dm_unregister_path_selector(&st_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_st_init);
module_exit(dm_st_exit);

MODULE_DESCRIPTION(DM_NAME " throughput oriented path selector");
MODULE_AUTHOR("Kiyoshi Ueda <k-ueda@ct.jp.nec.com>");
MODULE_LICENSE("GPL");
