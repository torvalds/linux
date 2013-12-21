#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/list_sort.h>

#include <linux/interval_tree_generic.h>
#include "usnic_uiom_interval_tree.h"

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

#define MAKE_NODE(node, start, end, ref_cnt, flags, err, err_out)	\
		do {							\
			node = usnic_uiom_interval_node_alloc(start,	\
					end, ref_cnt, flags);		\
				if (!node) {				\
					err = -ENOMEM;			\
					goto err_out;			\
				}					\
		} while (0)

#define MARK_FOR_ADD(node, list) (list_add_tail(&node->link, list))

#define MAKE_NODE_AND_APPEND(node, start, end, ref_cnt, flags, err,	\
				err_out, list)				\
				do {					\
					MAKE_NODE(node, start, end,	\
						ref_cnt, flags, err,	\
						err_out);		\
					MARK_FOR_ADD(node, list);	\
				} while (0)

#define FLAGS_EQUAL(flags1, flags2, mask)				\
			(((flags1) & (mask)) == ((flags2) & (mask)))

static struct usnic_uiom_interval_node*
usnic_uiom_interval_node_alloc(long int start, long int last, int ref_cnt,
				int flags)
{
	struct usnic_uiom_interval_node *interval = kzalloc(sizeof(*interval),
								GFP_ATOMIC);
	if (!interval)
		return NULL;

	interval->start = start;
	interval->last = last;
	interval->flags = flags;
	interval->ref_cnt = ref_cnt;

	return interval;
}

static int interval_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct usnic_uiom_interval_node *node_a, *node_b;

	node_a = list_entry(a, struct usnic_uiom_interval_node, link);
	node_b = list_entry(b, struct usnic_uiom_interval_node, link);

	/* long to int */
	if (node_a->start < node_b->start)
		return -1;
	else if (node_a->start > node_b->start)
		return 1;

	return 0;
}

static void
find_intervals_intersection_sorted(struct rb_root *root, unsigned long start,
					unsigned long last,
					struct list_head *list)
{
	struct usnic_uiom_interval_node *node;

	INIT_LIST_HEAD(list);

	for (node = usnic_uiom_interval_tree_iter_first(root, start, last);
		node;
		node = usnic_uiom_interval_tree_iter_next(node, start, last))
		list_add_tail(&node->link, list);

	list_sort(NULL, list, interval_cmp);
}

int usnic_uiom_get_intervals_diff(unsigned long start, unsigned long last,
					int flags, int flag_mask,
					struct rb_root *root,
					struct list_head *diff_set)
{
	struct usnic_uiom_interval_node *interval, *tmp;
	int err = 0;
	long int pivot = start;
	LIST_HEAD(intersection_set);

	INIT_LIST_HEAD(diff_set);

	find_intervals_intersection_sorted(root, start, last,
						&intersection_set);

	list_for_each_entry(interval, &intersection_set, link) {
		if (pivot < interval->start) {
			MAKE_NODE_AND_APPEND(tmp, pivot, interval->start - 1,
						1, flags, err, err_out,
						diff_set);
			pivot = interval->start;
		}

		/*
		 * Invariant: Set [start, pivot] is either in diff_set or root,
		 * but not in both.
		 */

		if (pivot > interval->last) {
			continue;
		} else if (pivot <= interval->last &&
				FLAGS_EQUAL(interval->flags, flags,
				flag_mask)) {
			pivot = interval->last + 1;
		}
	}

	if (pivot <= last)
		MAKE_NODE_AND_APPEND(tmp, pivot, last, 1, flags, err, err_out,
					diff_set);

	return 0;

err_out:
	list_for_each_entry_safe(interval, tmp, diff_set, link) {
		list_del(&interval->link);
		kfree(interval);
	}

	return err;
}

void usnic_uiom_put_interval_set(struct list_head *intervals)
{
	struct usnic_uiom_interval_node *interval, *tmp;
	list_for_each_entry_safe(interval, tmp, intervals, link)
		kfree(interval);
}

int usnic_uiom_insert_interval(struct rb_root *root, unsigned long start,
				unsigned long last, int flags)
{
	struct usnic_uiom_interval_node *interval, *tmp;
	unsigned long istart, ilast;
	int iref_cnt, iflags;
	unsigned long lpivot = start;
	int err = 0;
	LIST_HEAD(to_add);
	LIST_HEAD(intersection_set);

	find_intervals_intersection_sorted(root, start, last,
						&intersection_set);

	list_for_each_entry(interval, &intersection_set, link) {
		/*
		 * Invariant - lpivot is the left edge of next interval to be
		 * inserted
		 */
		istart = interval->start;
		ilast = interval->last;
		iref_cnt = interval->ref_cnt;
		iflags = interval->flags;

		if (istart < lpivot) {
			MAKE_NODE_AND_APPEND(tmp, istart, lpivot - 1, iref_cnt,
						iflags, err, err_out, &to_add);
		} else if (istart > lpivot) {
			MAKE_NODE_AND_APPEND(tmp, lpivot, istart - 1, 1, flags,
						err, err_out, &to_add);
			lpivot = istart;
		} else {
			lpivot = istart;
		}

		if (ilast > last) {
			MAKE_NODE_AND_APPEND(tmp, lpivot, last, iref_cnt + 1,
						iflags | flags, err, err_out,
						&to_add);
			MAKE_NODE_AND_APPEND(tmp, last + 1, ilast, iref_cnt,
						iflags, err, err_out, &to_add);
		} else {
			MAKE_NODE_AND_APPEND(tmp, lpivot, ilast, iref_cnt + 1,
						iflags | flags, err, err_out,
						&to_add);
		}

		lpivot = ilast + 1;
	}

	if (lpivot <= last)
		MAKE_NODE_AND_APPEND(tmp, lpivot, last, 1, flags, err, err_out,
					&to_add);

	list_for_each_entry_safe(interval, tmp, &intersection_set, link) {
		usnic_uiom_interval_tree_remove(interval, root);
		kfree(interval);
	}

	list_for_each_entry(interval, &to_add, link)
		usnic_uiom_interval_tree_insert(interval, root);

	return 0;

err_out:
	list_for_each_entry_safe(interval, tmp, &to_add, link)
		kfree(interval);

	return err;
}

void usnic_uiom_remove_interval(struct rb_root *root, unsigned long start,
				unsigned long last, struct list_head *removed)
{
	struct usnic_uiom_interval_node *interval;

	for (interval = usnic_uiom_interval_tree_iter_first(root, start, last);
			interval;
			interval = usnic_uiom_interval_tree_iter_next(interval,
									start,
									last)) {
		if (--interval->ref_cnt == 0)
			list_add_tail(&interval->link, removed);
	}

	list_for_each_entry(interval, removed, link)
		usnic_uiom_interval_tree_remove(interval, root);
}

INTERVAL_TREE_DEFINE(struct usnic_uiom_interval_node, rb,
			unsigned long, __subtree_last,
			START, LAST, , usnic_uiom_interval_tree)
