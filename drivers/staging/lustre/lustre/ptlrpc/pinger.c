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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/pinger.c
 *
 * Portal-RPC reconnection and replay operations, for use in recovery.
 */

#define DEBUG_SUBSYSTEM S_RPC

#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "ptlrpc_internal.h"

static int suppress_pings;
module_param(suppress_pings, int, 0644);
MODULE_PARM_DESC(suppress_pings, "Suppress pings");

struct mutex pinger_mutex;
static LIST_HEAD(pinger_imports);
static struct list_head timeout_list = LIST_HEAD_INIT(timeout_list);

int ptlrpc_pinger_suppress_pings(void)
{
	return suppress_pings;
}
EXPORT_SYMBOL(ptlrpc_pinger_suppress_pings);

struct ptlrpc_request *
ptlrpc_prep_ping(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	req = ptlrpc_request_alloc_pack(imp, &RQF_OBD_PING,
					LUSTRE_OBD_VERSION, OBD_PING);
	if (req) {
		ptlrpc_request_set_replen(req);
		req->rq_no_resend = req->rq_no_delay = 1;
	}
	return req;
}

int ptlrpc_obd_ping(struct obd_device *obd)
{
	int rc;
	struct ptlrpc_request *req;

	req = ptlrpc_prep_ping(obd->u.cli.cl_import);
	if (req == NULL)
		return -ENOMEM;

	req->rq_send_state = LUSTRE_IMP_FULL;

	rc = ptlrpc_queue_wait(req);

	ptlrpc_req_finished(req);

	return rc;
}
EXPORT_SYMBOL(ptlrpc_obd_ping);

int ptlrpc_ping(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	req = ptlrpc_prep_ping(imp);
	if (req == NULL) {
		CERROR("OOM trying to ping %s->%s\n",
		       imp->imp_obd->obd_uuid.uuid,
		       obd2cli_tgt(imp->imp_obd));
		return -ENOMEM;
	}

	DEBUG_REQ(D_INFO, req, "pinging %s->%s",
		  imp->imp_obd->obd_uuid.uuid, obd2cli_tgt(imp->imp_obd));
	ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);

	return 0;
}

void ptlrpc_update_next_ping(struct obd_import *imp, int soon)
{
	int time = soon ? PING_INTERVAL_SHORT : PING_INTERVAL;
	if (imp->imp_state == LUSTRE_IMP_DISCON) {
		int dtime = max_t(int, CONNECTION_SWITCH_MIN,
				  AT_OFF ? 0 :
				  at_get(&imp->imp_at.iat_net_latency));
		time = min(time, dtime);
	}
	imp->imp_next_ping = cfs_time_shift(time);
}

void ptlrpc_ping_import_soon(struct obd_import *imp)
{
	imp->imp_next_ping = cfs_time_current();
}

static inline int imp_is_deactive(struct obd_import *imp)
{
	return (imp->imp_deactive ||
		OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_IMP_DEACTIVE));
}

static inline int ptlrpc_next_reconnect(struct obd_import *imp)
{
	if (imp->imp_server_timeout)
		return cfs_time_shift(obd_timeout / 2);
	else
		return cfs_time_shift(obd_timeout);
}

cfs_duration_t pinger_check_timeout(unsigned long time)
{
	struct timeout_item *item;
	unsigned long timeout = PING_INTERVAL;

	/* The timeout list is a increase order sorted list */
	mutex_lock(&pinger_mutex);
	list_for_each_entry(item, &timeout_list, ti_chain) {
		int ti_timeout = item->ti_timeout;
		if (timeout > ti_timeout)
			timeout = ti_timeout;
		break;
	}
	mutex_unlock(&pinger_mutex);

	return cfs_time_sub(cfs_time_add(time, cfs_time_seconds(timeout)),
					 cfs_time_current());
}

static bool ir_up;

void ptlrpc_pinger_ir_up(void)
{
	CDEBUG(D_HA, "IR up\n");
	ir_up = true;
}
EXPORT_SYMBOL(ptlrpc_pinger_ir_up);

void ptlrpc_pinger_ir_down(void)
{
	CDEBUG(D_HA, "IR down\n");
	ir_up = false;
}
EXPORT_SYMBOL(ptlrpc_pinger_ir_down);

static void ptlrpc_pinger_process_import(struct obd_import *imp,
					 unsigned long this_ping)
{
	int level;
	int force;
	int force_next;
	int suppress;

	spin_lock(&imp->imp_lock);

	level = imp->imp_state;
	force = imp->imp_force_verify;
	force_next = imp->imp_force_next_verify;
	/*
	 * This will be used below only if the import is "FULL".
	 */
	suppress = ir_up && OCD_HAS_FLAG(&imp->imp_connect_data, PINGLESS);

	imp->imp_force_verify = 0;

	if (cfs_time_aftereq(imp->imp_next_ping - 5 * CFS_TICK, this_ping) &&
	    !force) {
		spin_unlock(&imp->imp_lock);
		return;
	}

	imp->imp_force_next_verify = 0;

	spin_unlock(&imp->imp_lock);

	CDEBUG(level == LUSTRE_IMP_FULL ? D_INFO : D_HA, "%s->%s: level %s/%u "
	       "force %u force_next %u deactive %u pingable %u suppress %u\n",
	       imp->imp_obd->obd_uuid.uuid, obd2cli_tgt(imp->imp_obd),
	       ptlrpc_import_state_name(level), level, force, force_next,
	       imp->imp_deactive, imp->imp_pingable, suppress);

	if (level == LUSTRE_IMP_DISCON && !imp_is_deactive(imp)) {
		/* wait for a while before trying recovery again */
		imp->imp_next_ping = ptlrpc_next_reconnect(imp);
		if (!imp->imp_no_pinger_recover)
			ptlrpc_initiate_recovery(imp);
	} else if (level != LUSTRE_IMP_FULL ||
		   imp->imp_obd->obd_no_recov ||
		   imp_is_deactive(imp)) {
		CDEBUG(D_HA, "%s->%s: not pinging (in recovery "
		       "or recovery disabled: %s)\n",
		       imp->imp_obd->obd_uuid.uuid, obd2cli_tgt(imp->imp_obd),
		       ptlrpc_import_state_name(level));
		if (force) {
			spin_lock(&imp->imp_lock);
			imp->imp_force_verify = 1;
			spin_unlock(&imp->imp_lock);
		}
	} else if ((imp->imp_pingable && !suppress) || force_next || force) {
		ptlrpc_ping(imp);
	}
}

static int ptlrpc_pinger_main(void *arg)
{
	struct ptlrpc_thread *thread = (struct ptlrpc_thread *)arg;

	/* Record that the thread is running */
	thread_set_flags(thread, SVC_RUNNING);
	wake_up(&thread->t_ctl_waitq);

	/* And now, loop forever, pinging as needed. */
	while (1) {
		unsigned long this_ping = cfs_time_current();
		struct l_wait_info lwi;
		cfs_duration_t time_to_next_wake;
		struct timeout_item *item;
		struct list_head *iter;

		mutex_lock(&pinger_mutex);
		list_for_each_entry(item, &timeout_list, ti_chain) {
			item->ti_cb(item, item->ti_cb_data);
		}
		list_for_each(iter, &pinger_imports) {
			struct obd_import *imp =
				list_entry(iter, struct obd_import,
					       imp_pinger_chain);

			ptlrpc_pinger_process_import(imp, this_ping);
			/* obd_timeout might have changed */
			if (imp->imp_pingable && imp->imp_next_ping &&
			    cfs_time_after(imp->imp_next_ping,
					   cfs_time_add(this_ping,
							cfs_time_seconds(PING_INTERVAL))))
				ptlrpc_update_next_ping(imp, 0);
		}
		mutex_unlock(&pinger_mutex);
		/* update memory usage info */
		obd_update_maxusage();

		/* Wait until the next ping time, or until we're stopped. */
		time_to_next_wake = pinger_check_timeout(this_ping);
		/* The ping sent by ptlrpc_send_rpc may get sent out
		   say .01 second after this.
		   ptlrpc_pinger_sending_on_import will then set the
		   next ping time to next_ping + .01 sec, which means
		   we will SKIP the next ping at next_ping, and the
		   ping will get sent 2 timeouts from now!  Beware. */
		CDEBUG(D_INFO, "next wakeup in "CFS_DURATION_T" ("
		       CFS_TIME_T")\n", time_to_next_wake,
		       cfs_time_add(this_ping,cfs_time_seconds(PING_INTERVAL)));
		if (time_to_next_wake > 0) {
			lwi = LWI_TIMEOUT(max_t(cfs_duration_t,
						time_to_next_wake,
						cfs_time_seconds(1)),
					  NULL, NULL);
			l_wait_event(thread->t_ctl_waitq,
				     thread_is_stopping(thread) ||
				     thread_is_event(thread),
				     &lwi);
			if (thread_test_and_clear_flags(thread, SVC_STOPPING)) {
				break;
			} else {
				/* woken after adding import to reset timer */
				thread_test_and_clear_flags(thread, SVC_EVENT);
			}
		}
	}

	thread_set_flags(thread, SVC_STOPPED);
	wake_up(&thread->t_ctl_waitq);

	CDEBUG(D_NET, "pinger thread exiting, process %d\n", current_pid());
	return 0;
}

static struct ptlrpc_thread pinger_thread;

int ptlrpc_start_pinger(void)
{
	struct l_wait_info lwi = { 0 };
	int rc;

	if (!thread_is_init(&pinger_thread) &&
	    !thread_is_stopped(&pinger_thread))
		return -EALREADY;

	init_waitqueue_head(&pinger_thread.t_ctl_waitq);

	strcpy(pinger_thread.t_name, "ll_ping");

	/* CLONE_VM and CLONE_FILES just avoid a needless copy, because we
	 * just drop the VM and FILES in cfs_daemonize_ctxt() right away. */
	rc = PTR_ERR(kthread_run(ptlrpc_pinger_main, &pinger_thread,
				 "%s", pinger_thread.t_name));
	if (IS_ERR_VALUE(rc)) {
		CERROR("cannot start thread: %d\n", rc);
		return rc;
	}
	l_wait_event(pinger_thread.t_ctl_waitq,
		     thread_is_running(&pinger_thread), &lwi);

	if (suppress_pings)
		CWARN("Pings will be suppressed at the request of the "
		      "administrator.  The configuration shall meet the "
		      "additional requirements described in the manual.  "
		      "(Search for the \"suppress_pings\" kernel module "
		      "parameter.)\n");

	return 0;
}

int ptlrpc_pinger_remove_timeouts(void);

int ptlrpc_stop_pinger(void)
{
	struct l_wait_info lwi = { 0 };
	int rc = 0;

	if (thread_is_init(&pinger_thread) ||
	    thread_is_stopped(&pinger_thread))
		return -EALREADY;

	ptlrpc_pinger_remove_timeouts();
	thread_set_flags(&pinger_thread, SVC_STOPPING);
	wake_up(&pinger_thread.t_ctl_waitq);

	l_wait_event(pinger_thread.t_ctl_waitq,
		     thread_is_stopped(&pinger_thread), &lwi);

	return rc;
}

void ptlrpc_pinger_sending_on_import(struct obd_import *imp)
{
	ptlrpc_update_next_ping(imp, 0);
}
EXPORT_SYMBOL(ptlrpc_pinger_sending_on_import);

void ptlrpc_pinger_commit_expected(struct obd_import *imp)
{
	ptlrpc_update_next_ping(imp, 1);
	assert_spin_locked(&imp->imp_lock);
	/*
	 * Avoid reading stale imp_connect_data.  When not sure if pings are
	 * expected or not on next connection, we assume they are not and force
	 * one anyway to guarantee the chance of updating
	 * imp_peer_committed_transno.
	 */
	if (imp->imp_state != LUSTRE_IMP_FULL ||
	    OCD_HAS_FLAG(&imp->imp_connect_data, PINGLESS))
		imp->imp_force_next_verify = 1;
}

int ptlrpc_pinger_add_import(struct obd_import *imp)
{
	if (!list_empty(&imp->imp_pinger_chain))
		return -EALREADY;

	mutex_lock(&pinger_mutex);
	CDEBUG(D_HA, "adding pingable import %s->%s\n",
	       imp->imp_obd->obd_uuid.uuid, obd2cli_tgt(imp->imp_obd));
	/* if we add to pinger we want recovery on this import */
	imp->imp_obd->obd_no_recov = 0;
	ptlrpc_update_next_ping(imp, 0);
	/* XXX sort, blah blah */
	list_add_tail(&imp->imp_pinger_chain, &pinger_imports);
	class_import_get(imp);

	ptlrpc_pinger_wake_up();
	mutex_unlock(&pinger_mutex);

	return 0;
}
EXPORT_SYMBOL(ptlrpc_pinger_add_import);

int ptlrpc_pinger_del_import(struct obd_import *imp)
{
	if (list_empty(&imp->imp_pinger_chain))
		return -ENOENT;

	mutex_lock(&pinger_mutex);
	list_del_init(&imp->imp_pinger_chain);
	CDEBUG(D_HA, "removing pingable import %s->%s\n",
	       imp->imp_obd->obd_uuid.uuid, obd2cli_tgt(imp->imp_obd));
	/* if we remove from pinger we don't want recovery on this import */
	imp->imp_obd->obd_no_recov = 1;
	class_import_put(imp);
	mutex_unlock(&pinger_mutex);
	return 0;
}
EXPORT_SYMBOL(ptlrpc_pinger_del_import);

/**
 * Register a timeout callback to the pinger list, and the callback will
 * be called when timeout happens.
 */
struct timeout_item* ptlrpc_new_timeout(int time, enum timeout_event event,
					timeout_cb_t cb, void *data)
{
	struct timeout_item *ti;

	OBD_ALLOC_PTR(ti);
	if (!ti)
		return(NULL);

	INIT_LIST_HEAD(&ti->ti_obd_list);
	INIT_LIST_HEAD(&ti->ti_chain);
	ti->ti_timeout = time;
	ti->ti_event = event;
	ti->ti_cb = cb;
	ti->ti_cb_data = data;

	return ti;
}

/**
 * Register timeout event on the the pinger thread.
 * Note: the timeout list is an sorted list with increased timeout value.
 */
static struct timeout_item*
ptlrpc_pinger_register_timeout(int time, enum timeout_event event,
			       timeout_cb_t cb, void *data)
{
	struct timeout_item *item, *tmp;

	LASSERT(mutex_is_locked(&pinger_mutex));

	list_for_each_entry(item, &timeout_list, ti_chain)
		if (item->ti_event == event)
			goto out;

	item = ptlrpc_new_timeout(time, event, cb, data);
	if (item) {
		list_for_each_entry_reverse(tmp, &timeout_list, ti_chain) {
			if (tmp->ti_timeout < time) {
				list_add(&item->ti_chain, &tmp->ti_chain);
				goto out;
			}
		}
		list_add(&item->ti_chain, &timeout_list);
	}
out:
	return item;
}

/* Add a client_obd to the timeout event list, when timeout(@time)
 * happens, the callback(@cb) will be called.
 */
int ptlrpc_add_timeout_client(int time, enum timeout_event event,
			      timeout_cb_t cb, void *data,
			      struct list_head *obd_list)
{
	struct timeout_item *ti;

	mutex_lock(&pinger_mutex);
	ti = ptlrpc_pinger_register_timeout(time, event, cb, data);
	if (!ti) {
		mutex_unlock(&pinger_mutex);
		return (-EINVAL);
	}
	list_add(obd_list, &ti->ti_obd_list);
	mutex_unlock(&pinger_mutex);
	return 0;
}
EXPORT_SYMBOL(ptlrpc_add_timeout_client);

int ptlrpc_del_timeout_client(struct list_head *obd_list,
			      enum timeout_event event)
{
	struct timeout_item *ti = NULL, *item;

	if (list_empty(obd_list))
		return 0;
	mutex_lock(&pinger_mutex);
	list_del_init(obd_list);
	/**
	 * If there are no obd attached to the timeout event
	 * list, remove this timeout event from the pinger
	 */
	list_for_each_entry(item, &timeout_list, ti_chain) {
		if (item->ti_event == event) {
			ti = item;
			break;
		}
	}
	LASSERTF(ti != NULL, "ti is NULL !\n");
	if (list_empty(&ti->ti_obd_list)) {
		list_del(&ti->ti_chain);
		OBD_FREE_PTR(ti);
	}
	mutex_unlock(&pinger_mutex);
	return 0;
}
EXPORT_SYMBOL(ptlrpc_del_timeout_client);

int ptlrpc_pinger_remove_timeouts(void)
{
	struct timeout_item *item, *tmp;

	mutex_lock(&pinger_mutex);
	list_for_each_entry_safe(item, tmp, &timeout_list, ti_chain) {
		LASSERT(list_empty(&item->ti_obd_list));
		list_del(&item->ti_chain);
		OBD_FREE_PTR(item);
	}
	mutex_unlock(&pinger_mutex);
	return 0;
}

void ptlrpc_pinger_wake_up(void)
{
	thread_add_flags(&pinger_thread, SVC_EVENT);
	wake_up(&pinger_thread.t_ctl_waitq);
}

/* Ping evictor thread */
#define PET_READY     1
#define PET_TERMINATE 2

static int	       pet_refcount = 0;
static int	       pet_state;
static wait_queue_head_t       pet_waitq;
LIST_HEAD(pet_list);
static DEFINE_SPINLOCK(pet_lock);

int ping_evictor_wake(struct obd_export *exp)
{
	struct obd_device *obd;

	spin_lock(&pet_lock);
	if (pet_state != PET_READY) {
		/* eventually the new obd will call here again. */
		spin_unlock(&pet_lock);
		return 1;
	}

	obd = class_exp2obd(exp);
	if (list_empty(&obd->obd_evict_list)) {
		class_incref(obd, "evictor", obd);
		list_add(&obd->obd_evict_list, &pet_list);
	}
	spin_unlock(&pet_lock);

	wake_up(&pet_waitq);
	return 0;
}

static int ping_evictor_main(void *arg)
{
	struct obd_device *obd;
	struct obd_export *exp;
	struct l_wait_info lwi = { 0 };
	time_t expire_time;

	unshare_fs_struct();

	CDEBUG(D_HA, "Starting Ping Evictor\n");
	pet_state = PET_READY;
	while (1) {
		l_wait_event(pet_waitq, (!list_empty(&pet_list)) ||
			     (pet_state == PET_TERMINATE), &lwi);

		/* loop until all obd's will be removed */
		if ((pet_state == PET_TERMINATE) && list_empty(&pet_list))
			break;

		/* we only get here if pet_exp != NULL, and the end of this
		 * loop is the only place which sets it NULL again, so lock
		 * is not strictly necessary. */
		spin_lock(&pet_lock);
		obd = list_entry(pet_list.next, struct obd_device,
				     obd_evict_list);
		spin_unlock(&pet_lock);

		expire_time = cfs_time_current_sec() - PING_EVICT_TIMEOUT;

		CDEBUG(D_HA, "evicting all exports of obd %s older than %ld\n",
		       obd->obd_name, expire_time);

		/* Exports can't be deleted out of the list while we hold
		 * the obd lock (class_unlink_export), which means we can't
		 * lose the last ref on the export.  If they've already been
		 * removed from the list, we won't find them here. */
		spin_lock(&obd->obd_dev_lock);
		while (!list_empty(&obd->obd_exports_timed)) {
			exp = list_entry(obd->obd_exports_timed.next,
					     struct obd_export,
					     exp_obd_chain_timed);
			if (expire_time > exp->exp_last_request_time) {
				class_export_get(exp);
				spin_unlock(&obd->obd_dev_lock);
				LCONSOLE_WARN("%s: haven't heard from client %s"
					      " (at %s) in %ld seconds. I think"
					      " it's dead, and I am evicting"
					      " it. exp %p, cur %ld expire %ld"
					      " last %ld\n",
					      obd->obd_name,
					      obd_uuid2str(&exp->exp_client_uuid),
					      obd_export_nid2str(exp),
					      (long)(cfs_time_current_sec() -
						     exp->exp_last_request_time),
					      exp, (long)cfs_time_current_sec(),
					      (long)expire_time,
					      (long)exp->exp_last_request_time);
				CDEBUG(D_HA, "Last request was at %ld\n",
				       exp->exp_last_request_time);
				class_fail_export(exp);
				class_export_put(exp);
				spin_lock(&obd->obd_dev_lock);
			} else {
				/* List is sorted, so everyone below is ok */
				break;
			}
		}
		spin_unlock(&obd->obd_dev_lock);

		spin_lock(&pet_lock);
		list_del_init(&obd->obd_evict_list);
		spin_unlock(&pet_lock);

		class_decref(obd, "evictor", obd);
	}
	CDEBUG(D_HA, "Exiting Ping Evictor\n");

	return 0;
}

void ping_evictor_start(void)
{
	struct task_struct *task;

	if (++pet_refcount > 1)
		return;

	init_waitqueue_head(&pet_waitq);

	task = kthread_run(ping_evictor_main, NULL, "ll_evictor");
	if (IS_ERR(task)) {
		pet_refcount--;
		CERROR("Cannot start ping evictor thread: %ld\n",
			PTR_ERR(task));
	}
}
EXPORT_SYMBOL(ping_evictor_start);

void ping_evictor_stop(void)
{
	if (--pet_refcount > 0)
		return;

	pet_state = PET_TERMINATE;
	wake_up(&pet_waitq);
}
EXPORT_SYMBOL(ping_evictor_stop);
