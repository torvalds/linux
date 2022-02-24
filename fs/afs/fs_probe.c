// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS fileserver probing
 *
 * Copyright (C) 2018, 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "afs_fs.h"
#include "internal.h"
#include "protocol_afs.h"
#include "protocol_yfs.h"

static unsigned int afs_fs_probe_fast_poll_interval = 30 * HZ;
static unsigned int afs_fs_probe_slow_poll_interval = 5 * 60 * HZ;

/*
 * Start the probe polling timer.  We have to supply it with an inc on the
 * outstanding server count.
 */
static void afs_schedule_fs_probe(struct afs_net *net,
				  struct afs_server *server, bool fast)
{
	unsigned long atj;

	if (!net->live)
		return;

	atj = server->probed_at;
	atj += fast ? afs_fs_probe_fast_poll_interval : afs_fs_probe_slow_poll_interval;

	afs_inc_servers_outstanding(net);
	if (timer_reduce(&net->fs_probe_timer, atj))
		afs_dec_servers_outstanding(net);
}

/*
 * Handle the completion of a set of probes.
 */
static void afs_finished_fs_probe(struct afs_net *net, struct afs_server *server)
{
	bool responded = server->probe.responded;

	write_seqlock(&net->fs_lock);
	if (responded) {
		list_add_tail(&server->probe_link, &net->fs_probe_slow);
	} else {
		server->rtt = UINT_MAX;
		clear_bit(AFS_SERVER_FL_RESPONDING, &server->flags);
		list_add_tail(&server->probe_link, &net->fs_probe_fast);
	}
	write_sequnlock(&net->fs_lock);

	afs_schedule_fs_probe(net, server, !responded);
}

/*
 * Handle the completion of a probe.
 */
static void afs_done_one_fs_probe(struct afs_net *net, struct afs_server *server)
{
	_enter("");

	if (atomic_dec_and_test(&server->probe_outstanding))
		afs_finished_fs_probe(net, server);

	wake_up_all(&server->probe_wq);
}

/*
 * Handle inability to send a probe due to ENOMEM when trying to allocate a
 * call struct.
 */
static void afs_fs_probe_not_done(struct afs_net *net,
				  struct afs_server *server,
				  struct afs_addr_cursor *ac)
{
	struct afs_addr_list *alist = ac->alist;
	unsigned int index = ac->index;

	_enter("");

	trace_afs_io_error(0, -ENOMEM, afs_io_error_fs_probe_fail);
	spin_lock(&server->probe_lock);

	server->probe.local_failure = true;
	if (server->probe.error == 0)
		server->probe.error = -ENOMEM;

	set_bit(index, &alist->failed);

	spin_unlock(&server->probe_lock);
	return afs_done_one_fs_probe(net, server);
}

/*
 * Process the result of probing a fileserver.  This is called after successful
 * or failed delivery of an FS.GetCapabilities operation.
 */
void afs_fileserver_probe_result(struct afs_call *call)
{
	struct afs_addr_list *alist = call->alist;
	struct afs_server *server = call->server;
	unsigned int index = call->addr_ix;
	unsigned int rtt_us = 0, cap0;
	int ret = call->error;

	_enter("%pU,%u", &server->uuid, index);

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
		clear_bit(index, &alist->responded);
		server->probe.local_failure = true;
		trace_afs_io_error(call->debug_id, ret, afs_io_error_fs_probe_fail);
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
		trace_afs_io_error(call->debug_id, ret, afs_io_error_fs_probe_fail);
		goto out;
	}

responded:
	clear_bit(index, &alist->failed);

	if (call->service_id == YFS_FS_SERVICE) {
		server->probe.is_yfs = true;
		set_bit(AFS_SERVER_FL_IS_YFS, &server->flags);
		alist->addrs[index].srx_service = call->service_id;
	} else {
		server->probe.not_yfs = true;
		if (!server->probe.is_yfs) {
			clear_bit(AFS_SERVER_FL_IS_YFS, &server->flags);
			alist->addrs[index].srx_service = call->service_id;
		}
		cap0 = ntohl(call->tmp);
		if (cap0 & AFS3_VICED_CAPABILITY_64BITFILES)
			set_bit(AFS_SERVER_FL_HAS_FS64, &server->flags);
		else
			clear_bit(AFS_SERVER_FL_HAS_FS64, &server->flags);
	}

	if (rxrpc_kernel_get_srtt(call->net->socket, call->rxcall, &rtt_us) &&
	    rtt_us < server->probe.rtt) {
		server->probe.rtt = rtt_us;
		server->rtt = rtt_us;
		alist->preferred = index;
	}

	smp_wmb(); /* Set rtt before responded. */
	server->probe.responded = true;
	set_bit(index, &alist->responded);
	set_bit(AFS_SERVER_FL_RESPONDING, &server->flags);
out:
	spin_unlock(&server->probe_lock);

	_debug("probe %pU [%u] %pISpc rtt=%u ret=%d",
	       &server->uuid, index, &alist->addrs[index].transport,
	       rtt_us, ret);

	return afs_done_one_fs_probe(call->net, server);
}

/*
 * Probe one or all of a fileserver's addresses to find out the best route and
 * to query its capabilities.
 */
void afs_fs_probe_fileserver(struct afs_net *net, struct afs_server *server,
			     struct key *key, bool all)
{
	struct afs_addr_cursor ac = {
		.index = 0,
	};

	_enter("%pU", &server->uuid);

	read_lock(&server->fs_lock);
	ac.alist = rcu_dereference_protected(server->addresses,
					     lockdep_is_held(&server->fs_lock));
	afs_get_addrlist(ac.alist);
	read_unlock(&server->fs_lock);

	server->probed_at = jiffies;
	atomic_set(&server->probe_outstanding, all ? ac.alist->nr_addrs : 1);
	memset(&server->probe, 0, sizeof(server->probe));
	server->probe.rtt = UINT_MAX;

	ac.index = ac.alist->preferred;
	if (ac.index < 0 || ac.index >= ac.alist->nr_addrs)
		all = true;

	if (all) {
		for (ac.index = 0; ac.index < ac.alist->nr_addrs; ac.index++)
			if (!afs_fs_get_capabilities(net, server, &ac, key))
				afs_fs_probe_not_done(net, server, &ac);
	} else {
		if (!afs_fs_get_capabilities(net, server, &ac, key))
			afs_fs_probe_not_done(net, server, &ac);
	}

	afs_put_addrlist(ac.alist);
}

/*
 * Wait for the first as-yet untried fileserver to respond.
 */
int afs_wait_for_fs_probes(struct afs_server_list *slist, unsigned long untried)
{
	struct wait_queue_entry *waits;
	struct afs_server *server;
	unsigned int rtt = UINT_MAX, rtt_s;
	bool have_responders = false;
	int pref = -1, i;

	_enter("%u,%lx", slist->nr_servers, untried);

	/* Only wait for servers that have a probe outstanding. */
	for (i = 0; i < slist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = slist->servers[i].server;
			if (!atomic_read(&server->probe_outstanding))
				__clear_bit(i, &untried);
			if (server->probe.responded)
				have_responders = true;
		}
	}
	if (have_responders || !untried)
		return 0;

	waits = kmalloc(array_size(slist->nr_servers, sizeof(*waits)), GFP_KERNEL);
	if (!waits)
		return -ENOMEM;

	for (i = 0; i < slist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = slist->servers[i].server;
			init_waitqueue_entry(&waits[i], current);
			add_wait_queue(&server->probe_wq, &waits[i]);
		}
	}

	for (;;) {
		bool still_probing = false;

		set_current_state(TASK_INTERRUPTIBLE);
		for (i = 0; i < slist->nr_servers; i++) {
			if (test_bit(i, &untried)) {
				server = slist->servers[i].server;
				if (server->probe.responded)
					goto stop;
				if (atomic_read(&server->probe_outstanding))
					still_probing = true;
			}
		}

		if (!still_probing || signal_pending(current))
			goto stop;
		schedule();
	}

stop:
	set_current_state(TASK_RUNNING);

	for (i = 0; i < slist->nr_servers; i++) {
		if (test_bit(i, &untried)) {
			server = slist->servers[i].server;
			rtt_s = READ_ONCE(server->rtt);
			if (test_bit(AFS_SERVER_FL_RESPONDING, &server->flags) &&
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
		slist->preferred = pref;
	return 0;
}

/*
 * Probe timer.  We have an increment on fs_outstanding that we need to pass
 * along to the work item.
 */
void afs_fs_probe_timer(struct timer_list *timer)
{
	struct afs_net *net = container_of(timer, struct afs_net, fs_probe_timer);

	if (!net->live || !queue_work(afs_wq, &net->fs_prober))
		afs_dec_servers_outstanding(net);
}

/*
 * Dispatch a probe to a server.
 */
static void afs_dispatch_fs_probe(struct afs_net *net, struct afs_server *server, bool all)
	__releases(&net->fs_lock)
{
	struct key *key = NULL;

	/* We remove it from the queues here - it will be added back to
	 * one of the queues on the completion of the probe.
	 */
	list_del_init(&server->probe_link);

	afs_get_server(server, afs_server_trace_get_probe);
	write_sequnlock(&net->fs_lock);

	afs_fs_probe_fileserver(net, server, key, all);
	afs_put_server(net, server, afs_server_trace_put_probe);
}

/*
 * Probe a server immediately without waiting for its due time to come
 * round.  This is used when all of the addresses have been tried.
 */
void afs_probe_fileserver(struct afs_net *net, struct afs_server *server)
{
	write_seqlock(&net->fs_lock);
	if (!list_empty(&server->probe_link))
		return afs_dispatch_fs_probe(net, server, true);
	write_sequnlock(&net->fs_lock);
}

/*
 * Probe dispatcher to regularly dispatch probes to keep NAT alive.
 */
void afs_fs_probe_dispatcher(struct work_struct *work)
{
	struct afs_net *net = container_of(work, struct afs_net, fs_prober);
	struct afs_server *fast, *slow, *server;
	unsigned long nowj, timer_at, poll_at;
	bool first_pass = true, set_timer = false;

	if (!net->live)
		return;

	_enter("");

	if (list_empty(&net->fs_probe_fast) && list_empty(&net->fs_probe_slow)) {
		_leave(" [none]");
		return;
	}

again:
	write_seqlock(&net->fs_lock);

	fast = slow = server = NULL;
	nowj = jiffies;
	timer_at = nowj + MAX_JIFFY_OFFSET;

	if (!list_empty(&net->fs_probe_fast)) {
		fast = list_first_entry(&net->fs_probe_fast, struct afs_server, probe_link);
		poll_at = fast->probed_at + afs_fs_probe_fast_poll_interval;
		if (time_before(nowj, poll_at)) {
			timer_at = poll_at;
			set_timer = true;
			fast = NULL;
		}
	}

	if (!list_empty(&net->fs_probe_slow)) {
		slow = list_first_entry(&net->fs_probe_slow, struct afs_server, probe_link);
		poll_at = slow->probed_at + afs_fs_probe_slow_poll_interval;
		if (time_before(nowj, poll_at)) {
			if (time_before(poll_at, timer_at))
			    timer_at = poll_at;
			set_timer = true;
			slow = NULL;
		}
	}

	server = fast ?: slow;
	if (server)
		_debug("probe %pU", &server->uuid);

	if (server && (first_pass || !need_resched())) {
		afs_dispatch_fs_probe(net, server, server == fast);
		first_pass = false;
		goto again;
	}

	write_sequnlock(&net->fs_lock);

	if (server) {
		if (!queue_work(afs_wq, &net->fs_prober))
			afs_dec_servers_outstanding(net);
		_leave(" [requeue]");
	} else if (set_timer) {
		if (timer_reduce(&net->fs_probe_timer, timer_at))
			afs_dec_servers_outstanding(net);
		_leave(" [timer]");
	} else {
		afs_dec_servers_outstanding(net);
		_leave(" [quiesce]");
	}
}

/*
 * Wait for a probe on a particular fileserver to complete for 2s.
 */
int afs_wait_for_one_fs_probe(struct afs_server *server, bool is_intr)
{
	struct wait_queue_entry wait;
	unsigned long timo = 2 * HZ;

	if (atomic_read(&server->probe_outstanding) == 0)
		goto dont_wait;

	init_wait_entry(&wait, 0);
	for (;;) {
		prepare_to_wait_event(&server->probe_wq, &wait,
				      is_intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		if (timo == 0 ||
		    server->probe.responded ||
		    atomic_read(&server->probe_outstanding) == 0 ||
		    (is_intr && signal_pending(current)))
			break;
		timo = schedule_timeout(timo);
	}

	finish_wait(&server->probe_wq, &wait);

dont_wait:
	if (server->probe.responded)
		return 0;
	if (is_intr && signal_pending(current))
		return -ERESTARTSYS;
	if (timo == 0)
		return -ETIME;
	return -EDESTADDRREQ;
}

/*
 * Clean up the probing when the namespace is killed off.
 */
void afs_fs_probe_cleanup(struct afs_net *net)
{
	if (del_timer_sync(&net->fs_probe_timer))
		afs_dec_servers_outstanding(net);
}
