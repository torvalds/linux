// SPDX-License-Identifier: GPL-2.0-or-later
/* Handle vlserver selection and rotation.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include "internal.h"
#include "afs_vl.h"

/*
 * Begin an operation on a volume location server.
 */
bool afs_begin_vlserver_operation(struct afs_vl_cursor *vc, struct afs_cell *cell,
				  struct key *key)
{
	static atomic_t debug_ids;

	memset(vc, 0, sizeof(*vc));
	vc->cell = cell;
	vc->key = key;
	vc->cumul_error.error = -EDESTADDRREQ;
	vc->nr_iterations = -1;

	if (signal_pending(current)) {
		vc->cumul_error.error = -EINTR;
		vc->flags |= AFS_VL_CURSOR_STOP;
		return false;
	}

	vc->debug_id = atomic_inc_return(&debug_ids);
	return true;
}

/*
 * Begin iteration through a server list, starting with the last used server if
 * possible, or the last recorded good server if not.
 */
static bool afs_start_vl_iteration(struct afs_vl_cursor *vc)
{
	struct afs_cell *cell = vc->cell;
	unsigned int dns_lookup_count;

	if (cell->dns_source == DNS_RECORD_UNAVAILABLE ||
	    cell->dns_expiry <= ktime_get_real_seconds()) {
		dns_lookup_count = smp_load_acquire(&cell->dns_lookup_count);
		set_bit(AFS_CELL_FL_DO_LOOKUP, &cell->flags);
		afs_queue_cell(cell, afs_cell_trace_queue_dns);

		if (cell->dns_source == DNS_RECORD_UNAVAILABLE) {
			if (wait_var_event_interruptible(
				    &cell->dns_lookup_count,
				    smp_load_acquire(&cell->dns_lookup_count)
				    != dns_lookup_count) < 0) {
				vc->cumul_error.error = -ERESTARTSYS;
				return false;
			}
		}

		/* Status load is ordered after lookup counter load */
		if (cell->dns_status == DNS_LOOKUP_GOT_NOT_FOUND) {
			pr_warn("No record of cell %s\n", cell->name);
			vc->cumul_error.error = -ENOENT;
			return false;
		}

		if (cell->dns_source == DNS_RECORD_UNAVAILABLE) {
			vc->cumul_error.error = -EDESTADDRREQ;
			return false;
		}
	}

	read_lock(&cell->vl_servers_lock);
	vc->server_list = afs_get_vlserverlist(
		rcu_dereference_protected(cell->vl_servers,
					  lockdep_is_held(&cell->vl_servers_lock)));
	read_unlock(&cell->vl_servers_lock);
	if (!vc->server_list->nr_servers)
		return false;

	vc->untried_servers = (1UL << vc->server_list->nr_servers) - 1;
	vc->server_index = -1;
	return true;
}

/*
 * Select the vlserver to use.  May be called multiple times to rotate
 * through the vlservers.
 */
bool afs_select_vlserver(struct afs_vl_cursor *vc)
{
	struct afs_addr_list *alist = vc->alist;
	struct afs_vlserver *vlserver;
	unsigned long set, failed;
	unsigned int rtt;
	s32 abort_code = vc->call_abort_code;
	int error = vc->call_error, i;

	vc->nr_iterations++;

	_enter("VC=%x+%x,%d{%lx},%d{%lx},%d,%d",
	       vc->debug_id, vc->nr_iterations, vc->server_index, vc->untried_servers,
	       vc->addr_index, vc->addr_tried,
	       error, abort_code);

	if (vc->flags & AFS_VL_CURSOR_STOP) {
		_leave(" = f [stopped]");
		return false;
	}

	if (vc->nr_iterations == 0)
		goto start;

	WRITE_ONCE(alist->addrs[vc->addr_index].last_error, error);

	/* Evaluate the result of the previous operation, if there was one. */
	switch (error) {
	default:
	case 0:
		/* Success or local failure.  Stop. */
		vc->cumul_error.error = error;
		vc->flags |= AFS_VL_CURSOR_STOP;
		_leave(" = f [okay/local %d]", vc->cumul_error.error);
		return false;

	case -ECONNABORTED:
		/* The far side rejected the operation on some grounds.  This
		 * might involve the server being busy or the volume having been moved.
		 */
		switch (abort_code) {
		case AFSVL_IO:
		case AFSVL_BADVOLOPER:
		case AFSVL_NOMEM:
			/* The server went weird. */
			afs_prioritise_error(&vc->cumul_error, -EREMOTEIO, abort_code);
			//write_lock(&vc->cell->vl_servers_lock);
			//vc->server_list->weird_mask |= 1 << vc->server_index;
			//write_unlock(&vc->cell->vl_servers_lock);
			goto next_server;

		default:
			afs_prioritise_error(&vc->cumul_error, error, abort_code);
			goto failed;
		}

	case -ERFKILL:
	case -EADDRNOTAVAIL:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EHOSTDOWN:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -ETIME:
		_debug("no conn %d", error);
		afs_prioritise_error(&vc->cumul_error, error, 0);
		goto iterate_address;

	case -ECONNRESET:
		_debug("call reset");
		afs_prioritise_error(&vc->cumul_error, error, 0);
		vc->flags |= AFS_VL_CURSOR_RETRY;
		goto next_server;

	case -EOPNOTSUPP:
		_debug("notsupp");
		goto next_server;
	}

restart_from_beginning:
	_debug("restart");
	if (vc->call_responded &&
	    vc->addr_index != vc->alist->preferred &&
	    test_bit(alist->preferred, &vc->addr_tried))
		WRITE_ONCE(alist->preferred, vc->addr_index);
	afs_put_addrlist(alist, afs_alist_trace_put_vlrotate_restart);
	alist = vc->alist = NULL;

	afs_put_vlserverlist(vc->cell->net, vc->server_list);
	vc->server_list = NULL;
	if (vc->flags & AFS_VL_CURSOR_RETRIED)
		goto failed;
	vc->flags |= AFS_VL_CURSOR_RETRIED;
start:
	_debug("start");
	ASSERTCMP(alist, ==, NULL);

	if (!afs_start_vl_iteration(vc))
		goto failed;

	error = afs_send_vl_probes(vc->cell->net, vc->key, vc->server_list);
	if (error < 0) {
		afs_prioritise_error(&vc->cumul_error, error, 0);
		goto failed;
	}

pick_server:
	_debug("pick [%lx]", vc->untried_servers);
	ASSERTCMP(alist, ==, NULL);

	error = afs_wait_for_vl_probes(vc->server_list, vc->untried_servers);
	if (error < 0) {
		afs_prioritise_error(&vc->cumul_error, error, 0);
		goto failed;
	}

	/* Pick the untried server with the lowest RTT. */
	vc->server_index = vc->server_list->preferred;
	if (test_bit(vc->server_index, &vc->untried_servers))
		goto selected_server;

	vc->server_index = -1;
	rtt = UINT_MAX;
	for (i = 0; i < vc->server_list->nr_servers; i++) {
		struct afs_vlserver *s = vc->server_list->servers[i].server;

		if (!test_bit(i, &vc->untried_servers) ||
		    !test_bit(AFS_VLSERVER_FL_RESPONDING, &s->flags))
			continue;
		if (s->probe.rtt <= rtt) {
			vc->server_index = i;
			rtt = s->probe.rtt;
		}
	}

	if (vc->server_index == -1)
		goto no_more_servers;

selected_server:
	_debug("use %d", vc->server_index);
	__clear_bit(vc->server_index, &vc->untried_servers);

	/* We're starting on a different vlserver from the list.  We need to
	 * check it, find its address list and probe its capabilities before we
	 * use it.
	 */
	vlserver = vc->server_list->servers[vc->server_index].server;
	vc->server = vlserver;

	_debug("USING VLSERVER: %s", vlserver->name);

	read_lock(&vlserver->lock);
	alist = rcu_dereference_protected(vlserver->addresses,
					  lockdep_is_held(&vlserver->lock));
	vc->alist = afs_get_addrlist(alist, afs_alist_trace_get_vlrotate_set);
	read_unlock(&vlserver->lock);

	vc->addr_tried = 0;
	vc->addr_index = -1;

iterate_address:
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	set = READ_ONCE(alist->responded);
	failed = READ_ONCE(alist->probe_failed);
	vc->addr_index = READ_ONCE(alist->preferred);

	_debug("%lx-%lx-%lx,%d", set, failed, vc->addr_tried, vc->addr_index);

	set &= ~(failed | vc->addr_tried);

	if (!set)
		goto next_server;

	if (!test_bit(vc->addr_index, &set))
		vc->addr_index = __ffs(set);

	set_bit(vc->addr_index, &vc->addr_tried);
	vc->alist = alist;

	_debug("VL address %d/%d", vc->addr_index, alist->nr_addrs);

	vc->call_responded = false;
	_leave(" = t %pISpc", rxrpc_kernel_remote_addr(alist->addrs[vc->addr_index].peer));
	return true;

next_server:
	_debug("next");
	ASSERT(alist);
	if (vc->call_responded &&
	    vc->addr_index != alist->preferred &&
	    test_bit(alist->preferred, &vc->addr_tried))
		WRITE_ONCE(alist->preferred, vc->addr_index);
	afs_put_addrlist(alist, afs_alist_trace_put_vlrotate_next);
	alist = vc->alist = NULL;
	goto pick_server;

no_more_servers:
	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	if (vc->flags & AFS_VL_CURSOR_RETRY)
		goto restart_from_beginning;

	for (i = 0; i < vc->server_list->nr_servers; i++) {
		struct afs_vlserver *s = vc->server_list->servers[i].server;

		if (test_bit(AFS_VLSERVER_FL_RESPONDING, &s->flags))
			vc->cumul_error.responded = true;
		afs_prioritise_error(&vc->cumul_error, READ_ONCE(s->probe.error),
				     s->probe.abort_code);
	}

failed:
	if (alist) {
		if (vc->call_responded &&
		    vc->addr_index != alist->preferred &&
		    test_bit(alist->preferred, &vc->addr_tried))
			WRITE_ONCE(alist->preferred, vc->addr_index);
		afs_put_addrlist(alist, afs_alist_trace_put_vlrotate_fail);
		alist = vc->alist = NULL;
	}
	vc->flags |= AFS_VL_CURSOR_STOP;
	_leave(" = f [failed %d]", vc->cumul_error.error);
	return false;
}

/*
 * Dump cursor state in the case of the error being EDESTADDRREQ.
 */
static void afs_vl_dump_edestaddrreq(const struct afs_vl_cursor *vc)
{
	struct afs_cell *cell = vc->cell;
	static int count;
	int i;

	if (!IS_ENABLED(CONFIG_AFS_DEBUG_CURSOR) || count > 3)
		return;
	count++;

	rcu_read_lock();
	pr_notice("EDESTADDR occurred\n");
	pr_notice("CELL: %s err=%d\n", cell->name, cell->error);
	pr_notice("DNS: src=%u st=%u lc=%x\n",
		  cell->dns_source, cell->dns_status, cell->dns_lookup_count);
	pr_notice("VC: ut=%lx ix=%u ni=%hu fl=%hx err=%hd\n",
		  vc->untried_servers, vc->server_index, vc->nr_iterations,
		  vc->flags, vc->cumul_error.error);
	pr_notice("VC: call  er=%d ac=%d r=%u\n",
		  vc->call_error, vc->call_abort_code, vc->call_responded);

	if (vc->server_list) {
		const struct afs_vlserver_list *sl = vc->server_list;
		pr_notice("VC: SL nr=%u ix=%u\n",
			  sl->nr_servers, sl->index);
		for (i = 0; i < sl->nr_servers; i++) {
			const struct afs_vlserver *s = sl->servers[i].server;
			pr_notice("VC: server %s+%hu fl=%lx E=%hd\n",
				  s->name, s->port, s->flags, s->probe.error);
			if (s->addresses) {
				const struct afs_addr_list *a =
					rcu_dereference(s->addresses);
				pr_notice("VC:  - nr=%u/%u/%u pf=%u\n",
					  a->nr_ipv4, a->nr_addrs, a->max_addrs,
					  a->preferred);
				pr_notice("VC:  - R=%lx F=%lx\n",
					  a->responded, a->probe_failed);
				if (a == vc->alist)
					pr_notice("VC:  - current\n");
			}
		}
	}

	pr_notice("AC: t=%lx ax=%u\n", vc->addr_tried, vc->addr_index);
	rcu_read_unlock();
}

/*
 * Tidy up a volume location server cursor and unlock the vnode.
 */
int afs_end_vlserver_operation(struct afs_vl_cursor *vc)
{
	struct afs_net *net = vc->cell->net;

	_enter("VC=%x+%x", vc->debug_id, vc->nr_iterations);

	switch (vc->cumul_error.error) {
	case -EDESTADDRREQ:
	case -EADDRNOTAVAIL:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
		afs_vl_dump_edestaddrreq(vc);
		break;
	}

	if (vc->alist) {
		if (vc->call_responded &&
		    vc->addr_index != vc->alist->preferred &&
		    test_bit(vc->alist->preferred, &vc->addr_tried))
			WRITE_ONCE(vc->alist->preferred, vc->addr_index);
		afs_put_addrlist(vc->alist, afs_alist_trace_put_vlrotate_end);
		vc->alist = NULL;
	}
	afs_put_vlserverlist(net, vc->server_list);
	return vc->cumul_error.error;
}
