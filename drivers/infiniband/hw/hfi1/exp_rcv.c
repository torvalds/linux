// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2017 Intel Corporation.
 */

#include "exp_rcv.h"
#include "trace.h"

/**
 * hfi1_exp_tid_set_init - initialize exp_tid_set
 * @set: the set
 */
static void hfi1_exp_tid_set_init(struct exp_tid_set *set)
{
	INIT_LIST_HEAD(&set->list);
	set->count = 0;
}

/**
 * hfi1_exp_tid_group_init - initialize rcd expected receive
 * @rcd: the rcd
 */
void hfi1_exp_tid_group_init(struct hfi1_ctxtdata *rcd)
{
	hfi1_exp_tid_set_init(&rcd->tid_group_list);
	hfi1_exp_tid_set_init(&rcd->tid_used_list);
	hfi1_exp_tid_set_init(&rcd->tid_full_list);
}

/**
 * hfi1_alloc_ctxt_rcv_groups - initialize expected receive groups
 * @rcd: the context to add the groupings to
 */
int hfi1_alloc_ctxt_rcv_groups(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 tidbase;
	struct tid_group *grp;
	int i;
	u32 ngroups;

	ngroups = rcd->expected_count / dd->rcv_entries.group_size;
	rcd->groups =
		kcalloc_node(ngroups, sizeof(*rcd->groups),
			     GFP_KERNEL, rcd->numa_id);
	if (!rcd->groups)
		return -ENOMEM;
	tidbase = rcd->expected_base;
	for (i = 0; i < ngroups; i++) {
		grp = &rcd->groups[i];
		grp->size = dd->rcv_entries.group_size;
		grp->base = tidbase;
		tid_group_add_tail(grp, &rcd->tid_group_list);
		tidbase += dd->rcv_entries.group_size;
	}

	return 0;
}

/**
 * hfi1_free_ctxt_rcv_groups - free  expected receive groups
 * @rcd: the context to free
 *
 * The routine dismantles the expect receive linked
 * list and clears any tids associated with the receive
 * context.
 *
 * This should only be called for kernel contexts and the
 * a base user context.
 */
void hfi1_free_ctxt_rcv_groups(struct hfi1_ctxtdata *rcd)
{
	kfree(rcd->groups);
	rcd->groups = NULL;
	hfi1_exp_tid_group_init(rcd);

	hfi1_clear_tids(rcd);
}
