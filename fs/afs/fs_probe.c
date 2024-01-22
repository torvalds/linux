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

struct afs_endpoint_state *afs_get_endpoint_state(struct afs_endpoint_state *estate,
						  enum afs_estate_trace where)
{
	if (estate) {
		int r;

		__refcount_inc(&estate->ref, &r);
		trace_afs_estate(estate->server_id, estate->probe_seq, r, where);
	}
	return estate;
}

static void afs_endpoint_state_rcu(struct rcu_head *rcu)
{
	struct afs_endpoint_state *estate = container_of(rcu, struct afs_endpoint_state, rcu);

	trace_afs_estate(estate->server_id, estate->probe_seq, refcount_read(&estate->ref),
			 afs_estate_trace_free);
	afs_put_addrlist(estate->addresses, afs_alist_trace_put_estate);
	kfree(estate);
}

void afs_put_endpoint_state(struct afs_endpoint_state *estate, enum afs_estate_trace where)
{
	if (estate) {
		unsigned int server_id = estate->server_id, probe_seq = estate->probe_seq;
		bool dead;
		int r;

		dead = __refcount_dec_and_test(&estate->ref, &r);
		trace_afs_estate(server_id, probe_seq, r, where);
		if (dead)
			call_rcu(&estate->rcu, afs_endpoint_state_rcu);
	}
}

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
static void afs_finished_fs_probe(struct afs_net *net, struct afs_server *server,
				  struct afs_endpoint_state *estate)
{
	bool responded = test_bit(AFS_ESTATE_RESPONDED, &estate->flags);

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
static void afs_done_one_fs_probe(struct afs_net *net, struct afs_server *server,
				  struct afs_endpoint_state *estate)
{
	_enter("");

	if (atomic_dec_and_test(&estate->nr_probing))
		afs_finished_fs_probe(net, server, estate);

	wake_up_all(&server->probe_wq);
}

/*
 * Handle inability to send a probe due to ENOMEM when trying to allocate a
 * call struct.
 */
static void afs_fs_probe_not_done(struct afs_net *net,
				  struct afs_server *server,
				  struct afs_endpoint_state *estate,
				  int index)
{
	_enter("");

	trace_afs_io_error(0, -ENOMEM, afs_io_error_fs_probe_fail);
	spin_lock(&server->probe_lock);

	set_bit(AFS_ESTATE_LOCAL_FAILURE, &estate->flags);
	if (estate->error == 0)
		estate->error = -ENOMEM;

	set_bit(index, &estate->failed_set);

	spin_unlock(&server->probe_lock);
	return afs_done_one_fs_probe(net, server, estate);
}

/*
 * Process the result of probing a fileserver.  This is called after successful
 * or failed delivery of an FS.GetCapabilities operation.
 */
void afs_fileserver_probe_result(struct afs_call *call)
{
	struct afs_endpoint_state *estate = call->probe;
	struct afs_addr_list *alist = estate->addresses;
	struct afs_address *addr = &alist->addrs[call->probe_index];
	struct afs_server *server = call->server;
	unsigned int index = call->probe_index;
	unsigned int rtt_us = -1, cap0;
	int ret = call->error;

	_enter("%pU,%u", &server->uuid, index);

	WRITE_ONCE(addr->last_error, ret);

	spin_lock(&server->probe_lock);

	switch (ret) {
	case 0:
		estate->error = 0;
		goto responded;
	case -ECONNABORTED:
		if (!test_bit(AFS_ESTATE_RESPONDED, &estate->flags)) {
			estate->abort_code = call->abort_code;
			estate->error = ret;
		}
		goto responded;
	case -ENOMEM:
	case -ENONET:
		clear_bit(index, &estate->responsive_set);
		set_bit(AFS_ESTATE_LOCAL_FAILURE, &estate->flags);
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
		clear_bit(index, &estate->responsive_set);
		set_bit(index, &estate->failed_set);
		if (!test_bit(AFS_ESTATE_RESPONDED, &estate->flags) &&
		    (estate->error == 0 ||
		     estate->error == -ETIMEDOUT ||
		     estate->error == -ETIME))
			estate->error = ret;
		trace_afs_io_error(call->debug_id, ret, afs_io_error_fs_probe_fail);
		goto out;
	}

responded:
	clear_bit(index, &estate->failed_set);

	if (call->service_id == YFS_FS_SERVICE) {
		set_bit(AFS_ESTATE_IS_YFS, &estate->flags);
		set_bit(AFS_SERVER_FL_IS_YFS, &server->flags);
		server->service_id = call->service_id;
	} else {
		set_bit(AFS_ESTATE_NOT_YFS, &estate->flags);
		if (!test_bit(AFS_ESTATE_IS_YFS, &estate->flags)) {
			clear_bit(AFS_SERVER_FL_IS_YFS, &server->flags);
			server->service_id = call->service_id;
		}
		cap0 = ntohl(call->tmp);
		if (cap0 & AFS3_VICED_CAPABILITY_64BITFILES)
			set_bit(AFS_SERVER_FL_HAS_FS64, &server->flags);
		else
			clear_bit(AFS_SERVER_FL_HAS_FS64, &server->flags);
	}

	rtt_us = rxrpc_kernel_get_srtt(addr->peer);
	if (rtt_us < estate->rtt) {
		estate->rtt = rtt_us;
		server->rtt = rtt_us;
		alist->preferred = index;
	}

	smp_wmb(); /* Set rtt before responded. */
	set_bit(AFS_ESTATE_RESPONDED, &estate->flags);
	set_bit(index, &estate->responsive_set);
	set_bit(AFS_SERVER_FL_RESPONDING, &server->flags);
out:
	spin_unlock(&server->probe_lock);

	trace_afs_fs_probe(server, false, estate, index, call->error, call->abort_code, rtt_us);
	_debug("probe[%x] %pU [%u] %pISpc rtt=%d ret=%d",
	       estate->probe_seq, &server->uuid, index,
	       rxrpc_kernel_remote_addr(alist->addrs[index].peer),
	       rtt_us, ret);

	return afs_done_one_fs_probe(call->net, server, estate);
}

/*
 * Probe all of a fileserver's addresses to find out the best route and to
 * query its capabilities.
 */
void afs_fs_probe_fileserver(struct afs_net *net, struct afs_server *server,
			     struct afs_addr_list *new_alist, struct key *key)
{
	struct afs_endpoint_state *estate, *old;
	struct afs_addr_list *alist;
	unsigned long unprobed;

	_enter("%pU", &server->uuid);

	estate = kzalloc(sizeof(*estate), GFP_KERNEL);
	if (!estate)
		return;

	refcount_set(&estate->ref, 1);
	estate->server_id = server->debug_id;
	estate->rtt = UINT_MAX;

	write_lock(&server->fs_lock);

	old = rcu_dereference_protected(server->endpoint_state,
					lockdep_is_held(&server->fs_lock));
	estate->responsive_set = old->responsive_set;
	estate->addresses = afs_get_addrlist(new_alist ?: old->addresses,
					     afs_alist_trace_get_estate);
	alist = estate->addresses;
	estate->probe_seq = ++server->probe_counter;
	atomic_set(&estate->nr_probing, alist->nr_addrs);

	rcu_assign_pointer(server->endpoint_state, estate);
	set_bit(AFS_ESTATE_SUPERSEDED, &old->flags);
	write_unlock(&server->fs_lock);

	trace_afs_estate(estate->server_id, estate->probe_seq, refcount_read(&estate->ref),
			 afs_estate_trace_alloc_probe);

	afs_get_address_preferences(net, alist);

	server->probed_at = jiffies;
	unprobed = (1UL << alist->nr_addrs) - 1;
	while (unprobed) {
		unsigned int index = 0, i;
		int best_prio = -1;

		for (i = 0; i < alist->nr_addrs; i++) {
			if (test_bit(i, &unprobed) &&
			    alist->addrs[i].prio > best_prio) {
				index = i;
				best_prio = alist->addrs[i].prio;
			}
		}
		__clear_bit(index, &unprobed);

		trace_afs_fs_probe(server, true, estate, index, 0, 0, 0);
		if (!afs_fs_get_capabilities(net, server, estate, index, key))
			afs_fs_probe_not_done(net, server, estate, index);
	}

	afs_put_endpoint_state(old, afs_estate_trace_put_probe);
}

/*
 * Wait for the first as-yet untried fileserver to respond, for the probe state
 * to be superseded or for all probes to finish.
 */
int afs_wait_for_fs_probes(struct afs_operation *op, struct afs_server_state *states, bool intr)
{
	struct afs_endpoint_state *estate;
	struct afs_server_list *slist = op->server_list;
	bool still_probing = true;
	int ret = 0, i;

	_enter("%u", slist->nr_servers);

	for (i = 0; i < slist->nr_servers; i++) {
		estate = states[i].endpoint_state;
		if (test_bit(AFS_ESTATE_SUPERSEDED, &estate->flags))
			return 2;
		if (atomic_read(&estate->nr_probing))
			still_probing = true;
		if (estate->responsive_set & states[i].untried_addrs)
			return 1;
	}
	if (!still_probing)
		return 0;

	for (i = 0; i < slist->nr_servers; i++)
		add_wait_queue(&slist->servers[i].server->probe_wq, &states[i].probe_waiter);

	for (;;) {
		still_probing = false;

		set_current_state(intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		for (i = 0; i < slist->nr_servers; i++) {
			estate = states[i].endpoint_state;
			if (test_bit(AFS_ESTATE_SUPERSEDED, &estate->flags)) {
				ret = 2;
				goto stop;
			}
			if (atomic_read(&estate->nr_probing))
				still_probing = true;
			if (estate->responsive_set & states[i].untried_addrs) {
				ret = 1;
				goto stop;
			}
		}

		if (!still_probing || signal_pending(current))
			goto stop;
		schedule();
	}

stop:
	set_current_state(TASK_RUNNING);

	for (i = 0; i < slist->nr_servers; i++)
		remove_wait_queue(&slist->servers[i].server->probe_wq, &states[i].probe_waiter);

	if (!ret && signal_pending(current))
		ret = -ERESTARTSYS;
	return ret;
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
static void afs_dispatch_fs_probe(struct afs_net *net, struct afs_server *server)
	__releases(&net->fs_lock)
{
	struct key *key = NULL;

	/* We remove it from the queues here - it will be added back to
	 * one of the queues on the completion of the probe.
	 */
	list_del_init(&server->probe_link);

	afs_get_server(server, afs_server_trace_get_probe);
	write_sequnlock(&net->fs_lock);

	afs_fs_probe_fileserver(net, server, NULL, key);
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
		return afs_dispatch_fs_probe(net, server);
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

	if (!net->live) {
		afs_dec_servers_outstanding(net);
		return;
	}

	_enter("");

	if (list_empty(&net->fs_probe_fast) && list_empty(&net->fs_probe_slow)) {
		afs_dec_servers_outstanding(net);
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
		afs_dispatch_fs_probe(net, server);
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
int afs_wait_for_one_fs_probe(struct afs_server *server, struct afs_endpoint_state *estate,
			      unsigned long exclude, bool is_intr)
{
	struct wait_queue_entry wait;
	unsigned long timo = 2 * HZ;

	if (atomic_read(&estate->nr_probing) == 0)
		goto dont_wait;

	init_wait_entry(&wait, 0);
	for (;;) {
		prepare_to_wait_event(&server->probe_wq, &wait,
				      is_intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		if (timo == 0 ||
		    test_bit(AFS_ESTATE_SUPERSEDED, &estate->flags) ||
		    (estate->responsive_set & ~exclude) ||
		    atomic_read(&estate->nr_probing) == 0 ||
		    (is_intr && signal_pending(current)))
			break;
		timo = schedule_timeout(timo);
	}

	finish_wait(&server->probe_wq, &wait);

dont_wait:
	if (estate->responsive_set & ~exclude)
		return 1;
	if (test_bit(AFS_ESTATE_SUPERSEDED, &estate->flags))
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
