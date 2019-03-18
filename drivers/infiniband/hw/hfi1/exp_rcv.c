/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "exp_rcv.h"
#include "trace.h"

/**
 * exp_tid_group_init - initialize exp_tid_set
 * @set - the set
 */
static void hfi1_exp_tid_set_init(struct exp_tid_set *set)
{
	INIT_LIST_HEAD(&set->list);
	set->count = 0;
}

/**
 * hfi1_exp_tid_group_init - initialize rcd expected receive
 * @rcd - the rcd
 */
void hfi1_exp_tid_group_init(struct hfi1_ctxtdata *rcd)
{
	hfi1_exp_tid_set_init(&rcd->tid_group_list);
	hfi1_exp_tid_set_init(&rcd->tid_used_list);
	hfi1_exp_tid_set_init(&rcd->tid_full_list);
}

/**
 * alloc_ctxt_rcv_groups - initialize expected receive groups
 * @rcd - the context to add the groupings to
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
 * free_ctxt_rcv_groups - free  expected receive groups
 * @rcd - the context to free
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
