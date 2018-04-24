// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/sec_gc.c
 *
 * Author: Eric Mei <ericm@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_SEC

#include <linux/libcfs/libcfs.h>

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>
#include <lustre_sec.h>

#include "ptlrpc_internal.h"

#define SEC_GC_INTERVAL (30 * 60)

static struct mutex sec_gc_mutex;
static LIST_HEAD(sec_gc_list);
static spinlock_t sec_gc_list_lock;

static LIST_HEAD(sec_gc_ctx_list);
static spinlock_t sec_gc_ctx_list_lock;

static atomic_t sec_gc_wait_del = ATOMIC_INIT(0);

void sptlrpc_gc_add_sec(struct ptlrpc_sec *sec)
{
	LASSERT(sec->ps_policy->sp_cops->gc_ctx);
	LASSERT(sec->ps_gc_interval > 0);
	LASSERT(list_empty(&sec->ps_gc_list));

	sec->ps_gc_next = ktime_get_real_seconds() + sec->ps_gc_interval;

	spin_lock(&sec_gc_list_lock);
	list_add_tail(&sec->ps_gc_list, &sec_gc_list);
	spin_unlock(&sec_gc_list_lock);

	CDEBUG(D_SEC, "added sec %p(%s)\n", sec, sec->ps_policy->sp_name);
}

void sptlrpc_gc_del_sec(struct ptlrpc_sec *sec)
{
	if (list_empty(&sec->ps_gc_list))
		return;

	might_sleep();

	/* signal before list_del to make iteration in gc thread safe */
	atomic_inc(&sec_gc_wait_del);

	spin_lock(&sec_gc_list_lock);
	list_del_init(&sec->ps_gc_list);
	spin_unlock(&sec_gc_list_lock);

	/* barrier */
	mutex_lock(&sec_gc_mutex);
	mutex_unlock(&sec_gc_mutex);

	atomic_dec(&sec_gc_wait_del);

	CDEBUG(D_SEC, "del sec %p(%s)\n", sec, sec->ps_policy->sp_name);
}

static void sec_process_ctx_list(void)
{
	struct ptlrpc_cli_ctx *ctx;

	spin_lock(&sec_gc_ctx_list_lock);

	while (!list_empty(&sec_gc_ctx_list)) {
		ctx = list_entry(sec_gc_ctx_list.next,
				 struct ptlrpc_cli_ctx, cc_gc_chain);
		list_del_init(&ctx->cc_gc_chain);
		spin_unlock(&sec_gc_ctx_list_lock);

		LASSERT(ctx->cc_sec);
		LASSERT(atomic_read(&ctx->cc_refcount) == 1);
		CDEBUG(D_SEC, "gc pick up ctx %p(%u->%s)\n",
		       ctx, ctx->cc_vcred.vc_uid, sec2target_str(ctx->cc_sec));
		sptlrpc_cli_ctx_put(ctx, 1);

		spin_lock(&sec_gc_ctx_list_lock);
	}

	spin_unlock(&sec_gc_ctx_list_lock);
}

static void sec_do_gc(struct ptlrpc_sec *sec)
{
	LASSERT(sec->ps_policy->sp_cops->gc_ctx);

	if (unlikely(sec->ps_gc_next == 0)) {
		CDEBUG(D_SEC, "sec %p(%s) has 0 gc time\n",
		       sec, sec->ps_policy->sp_name);
		return;
	}

	CDEBUG(D_SEC, "check on sec %p(%s)\n", sec, sec->ps_policy->sp_name);

	if (sec->ps_gc_next > ktime_get_real_seconds())
		return;

	sec->ps_policy->sp_cops->gc_ctx(sec);
	sec->ps_gc_next = ktime_get_real_seconds() + sec->ps_gc_interval;
}

static void sec_gc_main(struct work_struct *ws);
static DECLARE_DELAYED_WORK(sec_gc_work, sec_gc_main);

static void sec_gc_main(struct work_struct *ws)
{
	struct ptlrpc_sec *sec;

	sec_process_ctx_list();
again:
	/* go through sec list do gc.
	 * FIXME here we iterate through the whole list each time which
	 * is not optimal. we perhaps want to use balanced binary tree
	 * to trace each sec as order of expiry time.
	 * another issue here is we wakeup as fixed interval instead of
	 * according to each sec's expiry time
	 */
	mutex_lock(&sec_gc_mutex);
	list_for_each_entry(sec, &sec_gc_list, ps_gc_list) {
		/* if someone is waiting to be deleted, let it
		 * proceed as soon as possible.
		 */
		if (atomic_read(&sec_gc_wait_del)) {
			CDEBUG(D_SEC, "deletion pending, start over\n");
			mutex_unlock(&sec_gc_mutex);
			goto again;
		}

		sec_do_gc(sec);
	}
	mutex_unlock(&sec_gc_mutex);

	/* check ctx list again before sleep */
	sec_process_ctx_list();
	schedule_delayed_work(&sec_gc_work, SEC_GC_INTERVAL * HZ);
}

int sptlrpc_gc_init(void)
{
	mutex_init(&sec_gc_mutex);
	spin_lock_init(&sec_gc_list_lock);
	spin_lock_init(&sec_gc_ctx_list_lock);

	schedule_delayed_work(&sec_gc_work, 0);
	return 0;
}

void sptlrpc_gc_fini(void)
{
	cancel_delayed_work_sync(&sec_gc_work);
}
