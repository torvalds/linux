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

static bool afs_vl_probe_done(struct afs_vlserver *server)
{
	if (!atomic_dec_and_test(&server->probe_outstanding))
		return false;

	wake_up_var(&server->probe_outstanding);
	clear_bit_unlock(AFS_VLSERVER_FL_PROBING, &server->flags);
	wake_up_bit(&server->flags, AFS_VLSERVER_FL_PROBING);
	return true;
}

/*
 * Process the result of probing a vlserver.  This is called after successful
 * or failed delivery of an VL.GetCapabilities operation.
 */
void afs_vlserver_probe_result(struct afs_call *call)
{
	struct afs_addr_list *alist = call->alist;
	struct afs_vlserver *server = call->vlserver;
	unsigned int server_index = call->server_index;
	unsigned int rtt_us = 0;
	unsigned int index = call->addr_ix;
	bool have_result = false;
	int ret = call->error;

	_enter("%s,%u,%u,%d,%d", server->name, server_index, index, ret, call->abort_code);

	spin_lock(&server->probe_lock);

	switch (ret) {
	case 0:
		server->probe.error = 0;
		goto responded;
	case -ECONNABORTED:
		if (!server->probe.responded) {
			server->probe.abort_code = call->abort_code;
			server->probe.error = ret;
		}
		goto responded;
	case -ENOMEM:
	case -ENONET:
		server->probe.local_failure = true;
		afs_io_error(call, afs_io_error_vl_probe_fail);
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
		set_bit(index, &alist->failed);
		if (!server->probe.responded &&
		    (server->probe.error == 0 ||
		     server->probe.error == -ETIMEDOUT ||
		     server->probe.error == -ETIME))
			server->probe.error = ret;
		afs_io_error(call, afs_io_error_vl_probe_fail);
		goto out;
	}

responded:
	set_bit(index, &alist->responded);
	clear_bit(index, &alist->failed);

	if (call->service_id == YFS_VL_SERVICE) {
		server->probe.is_yfs = true;
		set_bit(AFS_VLSERVER_FL_IS_YFS, &server->flags);
		alist->addrs[index].srx_service = call->service_id;
	} else {
		server->probe.not_yfs = true;
		if (!server->probe.is_yfs) {
			clear_bit(AFS_VLSERVER_FL_IS_YFS, &server->flags);
			alist->addrs[index].srx_service = call->service_id;
		}
	}

	rtt_us = rxrpc_kernel_get_srtt(call->net->socket, call->rxcall);
	if (rtt_us < server->probe.rtt) {
		server->probe.rtt = rtt_us;
		alist->preferred = index;
		have_result = true;
	}

	smp_wmb(); /* Set rtt before responded. */
	server->probe.responded = true;
	set_bit(AFS_VLSERVER_FL_PROBED, &server->flags);
out:
	spin_unlock(&server->probe_lock);

	_debug("probe [%u][%u] %pISpc rtt=%u ret=%d",
	       server_index, index, &alist->addrs[index].transport, rtt_us, ret);

	have_result |= afs_vl_probe_done(server);
	if (have_result) {
		server->probe.have_result = true;
		wake_up_var(&server->probe.have_result);
		wake_up_all(&server->probe_wq);
	}
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
	struct afs_addr_cursor ac = {
		.index = 0,
	};
	struct afs_call *call;
	bool in_progress = false;

	_enter("%s", server->name);

	read_lock(&server->lock);
	ac.alist = rcu_dereference_protected(server->addresses,
					     lockdep_is_held(&server->lock));
	read_unlock(&server->lock);

	atomic_set(&server->probe_outstanding, ac.alist->nr_addrs);
	memset(&server->probe, 0, sizeof(server->probe));
	server->probe.rtt = UINT_MAX;

	for (ac.index = 0; ac.index < ac.alist->nr_addrs; ac.index++) {
		call = afs_vl_get_capabilities(net, &ac, key, server,
					       server_index);
		if (!IS_ERR(call)) {
			afs_put_call(call);
			in_progress = true;
		} else {
			afs_prioritise_error(_e, PTR_ERR(call), ac.abort_code);
		}
	}

	if (!in_progress)
		afs_vl_probe_done(server);
	return in_progress;
}

/*
 * Send off probes to all unprobed servers.
 */
int afs_send_vl_probes(struct afs_net *net, struct key *key,
		       struct afs_vlserver_list *vllist)
{
	struct afs_vlserver *server;
	struct afs_error e;
	bool in_progress = false;
	int i;

	e.error = 0;
	e.responded = false;
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
	unsigned int rtt = UINT_MAX;
	bool have_responders = false;
	int pref = -1, i;

	_enter("%u,%lx", vllist->nr_servers, untried);

	/* Only wait for servers that have a probe outstanding. */
	for (i = 0; i < vllist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = vllist->servers[i].server;
			if (!test_bit(AFS_VLSERVER_FL_PROBING, &server->flags))
				__clear_bit(i, &untried);
			if (server->probe.responded)
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
				if (server->probe.responded)
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
			if (server->probe.responded &&
			    server->probe.rtt < rtt) {
				pref = i;
				rtt = server->probe.rtt;
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
