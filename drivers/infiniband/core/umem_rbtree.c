/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interval_tree_generic.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <rdma/ib_umem_odp.h>

/*
 * The ib_umem list keeps track of memory regions for which the HW
 * device request to receive notification when the related memory
 * mapping is changed.
 *
 * ib_umem_lock protects the list.
 */

static inline u64 node_start(struct umem_odp_node *n)
{
	struct ib_umem_odp *umem_odp =
			container_of(n, struct ib_umem_odp, interval_tree);

	return ib_umem_start(umem_odp->umem);
}

/* Note that the representation of the intervals in the interval tree
 * considers the ending point as contained in the interval, while the
 * function ib_umem_end returns the first address which is not contained
 * in the umem.
 */
static inline u64 node_last(struct umem_odp_node *n)
{
	struct ib_umem_odp *umem_odp =
			container_of(n, struct ib_umem_odp, interval_tree);

	return ib_umem_end(umem_odp->umem) - 1;
}

INTERVAL_TREE_DEFINE(struct umem_odp_node, rb, u64, __subtree_last,
		     node_start, node_last, , rbt_ib_umem)

/* @last is not a part of the interval. See comment for function
 * node_last.
 */
int rbt_ib_umem_for_each_in_range(struct rb_root *root,
				  u64 start, u64 last,
				  umem_call_back cb,
				  void *cookie)
{
	int ret_val = 0;
	struct umem_odp_node *node, *next;
	struct ib_umem_odp *umem;

	if (unlikely(start == last))
		return ret_val;

	for (node = rbt_ib_umem_iter_first(root, start, last - 1);
			node; node = next) {
		next = rbt_ib_umem_iter_next(node, start, last - 1);
		umem = container_of(node, struct ib_umem_odp, interval_tree);
		ret_val = cb(umem->umem, start, last, cookie) || ret_val;
	}

	return ret_val;
}
EXPORT_SYMBOL(rbt_ib_umem_for_each_in_range);

struct ib_umem_odp *rbt_ib_umem_lookup(struct rb_root *root,
				       u64 addr, u64 length)
{
	struct umem_odp_node *node;

	node = rbt_ib_umem_iter_first(root, addr, addr + length - 1);
	if (node)
		return container_of(node, struct ib_umem_odp, interval_tree);
	return NULL;

}
EXPORT_SYMBOL(rbt_ib_umem_lookup);
