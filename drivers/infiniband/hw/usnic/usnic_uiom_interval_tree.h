/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_UIOM_INTERVAL_TREE_H_
#define USNIC_UIOM_INTERVAL_TREE_H_

#include <linux/rbtree.h>

struct usnic_uiom_interval_node {
	struct rb_node			rb;
	struct list_head		link;
	unsigned long			start;
	unsigned long			last;
	unsigned long			__subtree_last;
	unsigned int			ref_cnt;
	int				flags;
};

extern void
usnic_uiom_interval_tree_insert(struct usnic_uiom_interval_node *node,
					struct rb_root_cached *root);
extern void
usnic_uiom_interval_tree_remove(struct usnic_uiom_interval_node *node,
					struct rb_root_cached *root);
extern struct usnic_uiom_interval_node *
usnic_uiom_interval_tree_iter_first(struct rb_root_cached *root,
					unsigned long start,
					unsigned long last);
extern struct usnic_uiom_interval_node *
usnic_uiom_interval_tree_iter_next(struct usnic_uiom_interval_node *node,
			unsigned long start, unsigned long last);
/*
 * Inserts {start...last} into {root}.  If there are overlaps,
 * nodes will be broken up and merged
 */
int usnic_uiom_insert_interval(struct rb_root_cached *root,
				unsigned long start, unsigned long last,
				int flags);
/*
 * Removed {start...last} from {root}.  The nodes removed are returned in
 * 'removed.' The caller is responsibile for freeing memory of nodes in
 * 'removed.'
 */
void usnic_uiom_remove_interval(struct rb_root_cached *root,
				unsigned long start, unsigned long last,
				struct list_head *removed);
/*
 * Returns {start...last} - {root} (relative complement of {start...last} in
 * {root}) in diff_set sorted ascendingly
 */
int usnic_uiom_get_intervals_diff(unsigned long start,
					unsigned long last, int flags,
					int flag_mask,
					struct rb_root_cached *root,
					struct list_head *diff_set);
/* Call this to free diff_set returned by usnic_uiom_get_intervals_diff */
void usnic_uiom_put_interval_set(struct list_head *intervals);
#endif /* USNIC_UIOM_INTERVAL_TREE_H_ */
