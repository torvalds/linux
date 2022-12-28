/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef __IOMMUFD_DOUBLE_SPAN_H
#define __IOMMUFD_DOUBLE_SPAN_H

#include <linux/interval_tree.h>

/*
 * This is a variation of the general interval_tree_span_iter that computes the
 * spans over the union of two different interval trees. Used ranges are broken
 * up and reported based on the tree that provides the interval. The first span
 * always takes priority. Like interval_tree_span_iter it is greedy and the same
 * value of is_used will not repeat on two iteration cycles.
 */
struct interval_tree_double_span_iter {
	struct rb_root_cached *itrees[2];
	struct interval_tree_span_iter spans[2];
	union {
		unsigned long start_hole;
		unsigned long start_used;
	};
	union {
		unsigned long last_hole;
		unsigned long last_used;
	};
	/* 0 = hole, 1 = used span[0], 2 = used span[1], -1 done iteration */
	int is_used;
};

void interval_tree_double_span_iter_update(
	struct interval_tree_double_span_iter *iter);
void interval_tree_double_span_iter_first(
	struct interval_tree_double_span_iter *iter,
	struct rb_root_cached *itree1, struct rb_root_cached *itree2,
	unsigned long first_index, unsigned long last_index);
void interval_tree_double_span_iter_next(
	struct interval_tree_double_span_iter *iter);

static inline bool
interval_tree_double_span_iter_done(struct interval_tree_double_span_iter *state)
{
	return state->is_used == -1;
}

#define interval_tree_for_each_double_span(span, itree1, itree2, first_index, \
					   last_index)                        \
	for (interval_tree_double_span_iter_first(span, itree1, itree2,       \
						  first_index, last_index);   \
	     !interval_tree_double_span_iter_done(span);                      \
	     interval_tree_double_span_iter_next(span))

#endif
