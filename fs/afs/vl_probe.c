// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS vlserver probing
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "afs_fs.h"
#include "internal.h"
#include "protocol_yfs.h"


/*
 * Handle the completion of a set of probes.
 */
static void afs_finished_vl_probe(struct afs_vlserver *server)
{
	if (!(server->probe.flags & AFS_VLSERVER_PROBE_RESPONDED)) {
		server->rtt = UINT_MAX;
		clear_bit(AFS_VLSERVER_FL_RESPONDING, &server->flags);
	}

	clear_bit_unlock(AFS_VLSERVER_FL_PROBING, &server->flags);
	wake_up_bit(&server->flags, AFS_VLSERVER_FL_PROBING);
}

/*
 * Handle the completion of a probe RPC call.
 */
static void afs_done_one_vl_probe(struct afs_vlserver *server, bool wake_up)
{
	if (atomic_dec_and_test(&server->probe_outstanding)) {
		afs_finished_vl_probe(server);
		wake_up = true;
	}

	if (wake_up)
		wake_up_all(&server->probe_wq);
}

/*
 * Process the result of probing a vlserver.  This is called after successful
 * or failed delivery of an VL.GetCapabilities operation.
 */
void afs_vlserver_probe_result(struct afs_call *call)
{
	struct afs_addr_list *alist = call->vl_probe;
	struct afs_vlserver *server = call->vlserver;
	struct afs_address *addr = &alist->addrs[call->probe_index];
	unsigned int server_index = call->server_index;
	unsigned int rtt_us = 0;
	unsigned int index = call->probe_index;
	bool have_result = false;
	int ret = call->error;

	_enter("%s,%u,%u,%d,%d", server->name, server_index, index, ret, call->abort_code);

	spin_lock(&server->probe_lock);

	switch (ret) {
	case 0:
		server->probe.error = 0;
		goto responded;
	case -ECONNABORTED:
		if (!(server->probe.flags & AFS_VLSERVER_PROBE_RESPONDED)) {
			server->probe.abort_code = call->abort_code;
			server->probe.error = ret;
		}
		goto responded;
	case -ENOMEM:
	case -ENONET:
	case -EKEYEXPIRED:
	case -EKEYREVOKED:
	case -EKEYREJECTED:
		server->probe.flags |= AFS_VLSERVER_PROBE_LOCAL_FAILURE;
		if (server->probe.error == 0)
			server->probe.error = ret;
		trace_afs_io_error(call->debug_id, ret, afs_io_error_vl_probe_fail);
		goto out;
	case -ECONNRESET: /* Responded, but call expired. */
	case -ERFKILL:
	case -EADDRNOTAVAIL:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EHOSTDOWN:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -ETIME:
	default:
		clear_bit(index, &alist->responded);
		set_bit(index, &alist->probe_failed);
		if (!(server->probe.flags & AFS_VLSERVER_PROBE_RESPONDED) &&
		    (server->probe.error == 0 ||
		     server->probe.error == -ETIMEDOUT ||
		     server->probe.error == -ETIME))
			server->probe.error = ret;
		trace_afs_io_error(call->debug_id, ret, afs_io_error_vl_probe_fail);
		goto out;
	}

responded:
	set_bit(index, &alist->responded);
	clear_bit(index, &alist->probe_failed);

	if (call->service_id == YFS_VL_SERVICE) {
		server->probe.flags |= AFS_VLSERVER_PROBE_IS_YFS;
		set_bit(AFS_VLSERVER_FL_IS_YFS, &server->flags);
		server->service_id = call->service_id;
	} else {
		server->probe.flags |= AFS_VLSERVER_PROBE_NOT_YFS;
		if (!(server->probe.flags & AFS_VLSERVER_PROBE_IS_YFS)) {
			clear_bit(AFS_VLSERVER_FL_IS_YFS, &server->flags);
			server->service_id = call->service_id;
		}
	}

	rtt_us = rxrpc_kernel_get_srtt(addr->peer);
	if (rtt_us < server->probe.rtt) {
		server->probe.rtt = rtt_us;
		server->rtt = rtt_us;
		alist->preferred = index;
	}

	smp_wmb(); /* Set rtt before responded. */
	server->probe.flags |= AFS_VLSERVER_PROBE_RESPONDED;
	set_bit(AFS_VLSERVER_FL_PROBED, &server->flags);
	set_bit(AFS_VLSERVER_FL_RESPONDING, &server->flags);
	have_result = true;
out:
	spin_unlock(&server->probe_lock);

	trace_afs_vl_probe(server, false, alist, index, call->error, call->abort_code, rtt_us);
	_debug("probe [%u][%u] %pISpc rtt=%d ret=%d",
	       server_index, index, rxrpc_kernel_remote_addr(addr->peer),
	       rtt_us, ret);

	afs_done_one_vl_probe(server, have_result);
}

/*
 * Probe all of a vlserver's addresses to find out the best route and to
 * query its capabilities.
 */
static bool afs_do_probe_vlserver(struct afs_net *net,
				  struct afs_vlserver *server,
				  struct key *key,
				  unsigned int server_index,
				  struct afs_error *_e)
{
	struct afs_addr_list *alist;
	struct afs_call *call;
	unsigned long unprobed;
	unsigned int index, i;
	bool in_progress = false;
	int best_prio;

	_enter("%s", server->name);

	read_lock(&server->lock);
	alist = rcu_dereference_protected(server->addresses,
					  lockdep_is_held(&server->lock));
	afs_get_addrlist(alist, afs_alist_trace_get_vlprobe);
	read_unlock(&server->lock);

	atomic_set(&server->probe_outstanding, alist->nr_addrs);
	memset(&server->probe, 0, sizeof(server->probe));
	server->probe.rtt = UINT_MAX;

	unprobed = (1UL << alist->nr_addrs) - 1;
	while (unprobed) {
		best_prio = -1;
		index = 0;
		for (i = 0; i < alist->nr_addrs; i++) {
			if (test_bit(i, &unprobed) &&
			    alist->addrs[i].prio > best_prio) {
				index = i;
				best_prio = alist->addrs[i].prio;
			}
		}
		__clear_bit(index, &unprobed);

		trace_afs_vl_probe(server, true, alist, index, 0, 0, 0);
		call = afs_vl_get_capabilities(net, alist, index, key, server,
					       server_index);
		if (!IS_ERR(call)) {
			afs_prioritise_error(_e, call->error, call->abort_code);
			afs_put_call(call);
			in_progress = true;
		} else {
			afs_prioritise_error(_e, PTR_ERR(call), 0);
			afs_done_one_vl_probe(server, false);
		}
	}

	afs_put_addrlist(alist, afs_alist_trace_put_vlprobe);
	return in_progress;
}

/*
 * Send off probes to all unprobed servers.
 */
int afs_send_vl_probes(struct afs_net *net, struct key *key,
		       struct afs_vlserver_list *vllist)
{
	struct afs_vlserver *server;
	struct afs_error e = {};
	bool in_progress = false;
	int i;

	for (i = 0; i < vllist->nr_servers; i++) {
		server = vllist->servers[i].server;
		if (test_bit(AFS_VLSERVER_FL_PROBED, &server->flags))
			continue;

		if (!test_and_set_bit_lock(AFS_VLSERVER_FL_PROBING, &server->flags) &&
		    afs_do_probe_vlserver(net, server, key, i, &e))
			in_progress = true;
	}

	return in_progress ? 0 : e.error;
}

/*
 * Wait for the first as-yet untried server to respond.
 */
int afs_wait_for_vl_probes(struct afs_vlserver_list *vllist,
			   unsigned long untried)
{
	struct wait_queue_entry *waits;
	struct afs_vlserver *server;
	unsigned int rtt = UINT_MAX, rtt_s;
	bool have_responders = false;
	int pref = -1, i;

	_enter("%u,%lx", vllist->nr_servers, untried);

	/* Only wait for servers that have a probe outstanding. */
	for (i = 0; i < vllist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = vllist->servers[i].server;
			if (!test_bit(AFS_VLSERVER_FL_PROBING, &server->flags))
				__clear_bit(i, &untried);
			if (server->probe.flags & AFS_VLSERVER_PROBE_RESPONDED)
				have_responders = true;
		}
	}
	if (have_responders || !untried)
		return 0;

	waits = kmalloc(array_size(vllist->nr_servers, sizeof(*waits)), GFP_KERNEL);
	if (!waits)
		return -ENOMEM;

	for (i = 0; i < vllist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = vllist->servers[i].server;
			init_waitqueue_entry(&waits[i], current);
			add_wait_queue(&server->probe_wq, &waits[i]);
		}
	}

	for (;;) {
		bool still_probing = false;

		set_current_state(TASK_INTERRUPTIBLE);
		for (i = 0; i < vllist->nr_servers; i++) {
			if (test_bit(i, &untried)) {
				server = vllist->servers[i].server;
				if (server->probe.flags & AFS_VLSERVER_PROBE_RESPONDED)
					goto stop;
				if (test_bit(AFS_VLSERVER_FL_PROBING, &server->flags))
					still_probing = true;
			}
		}

		if (!still_probing || signal_pending(current))
			goto stop;
		schedule();
	}

stop:
	set_current_state(TASK_RUNNING);

	for (i = 0; i < vllist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = vllist->servers[i].server;
			rtt_s = READ_ONCE(server->rtt);
			if (test_bit(AFS_VLSERVER_FL_RESPONDING, &server->flags) &&
			    rtt_s < rtt) {
				pref = i;
				rtt = rtt_s;
			}

			remove_wait_queue(&server->probe_wq, &waits[i]);
		}
	}

	kfree(waits);

	if (pref == -1 && signal_pending(current))
		return -ERESTARTSYS;

	if (pref >= 0)
		vllist->preferred = pref;

	_leave(" = 0 [%u]", pref);
	return 0;
}
