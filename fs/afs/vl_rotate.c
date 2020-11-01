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
	memset(vc, 0, sizeof(*vc));
	vc->cell = cell;
	vc->key = key;
	vc->error = -EDESTADDRREQ;
	vc->ac.error = SHRT_MAX;

	if (signal_pending(current)) {
		vc->error = -EINTR;
		vc->flags |= AFS_VL_CURSOR_STOP;
		return false;
	}

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
		queue_work(afs_wq, &cell->manager);

		if (cell->dns_source == DNS_RECORD_UNAVAILABLE) {
			if (wait_var_event_interruptible(
				    &cell->dns_lookup_count,
				    smp_load_acquire(&cell->dns_lookup_count)
				    != dns_lookup_count) < 0) {
				vc->error = -ERESTARTSYS;
				return false;
			}
		}

		/* Status load is ordered after lookup counter load */
		if (cell->dns_source == DNS_RECORD_UNAVAILABLE) {
			vc->error = -EDESTADDRREQ;
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

	vc->untried = (1UL << vc->server_list->nr_servers) - 1;
	vc->index = -1;
	return true;
}

/*
 * Select the vlserver to use.  May be called multiple times to rotate
 * through the vlservers.
 */
bool afs_select_vlserver(struct afs_vl_cursor *vc)
{
	struct afs_addr_list *alist;
	struct afs_vlserver *vlserver;
	struct afs_error e;
	u32 rtt;
	int error = vc->ac.error, i;

	_enter("%lx[%d],%lx[%d],%d,%d",
	       vc->untried, vc->index,
	       vc->ac.tried, vc->ac.index,
	       error, vc->ac.abort_code);

	if (vc->flags & AFS_VL_CURSOR_STOP) {
		_leave(" = f [stopped]");
		return false;
	}

	vc->nr_iterations++;

	/* Evaluate the result of the previous operation, if there was one. */
	switch (error) {
	case SHRT_MAX:
		goto start;

	default:
	case 0:
		/* Success or local failure.  Stop. */
		vc->error = error;
		vc->flags |= AFS_VL_CURSOR_STOP;
		_leave(" = f [okay/local %d]", vc->ac.error);
		return false;

	case -ECONNABORTED:
		/* The far side rejected the operation on some grounds.  This
		 * might involve the server being busy or the volume having been moved.
		 */
		switch (vc->ac.abort_code) {
		case AFSVL_IO:
		case AFSVL_BADVOLOPER:
		case AFSVL_NOMEM:
			/* The server went weird. */
			vc->error = -EREMOTEIO;
			//write_lock(&vc->cell->vl_servers_lock);
			//vc->server_list->weird_mask |= 1 << vc->index;
			//write_unlock(&vc->cell->vl_servers_lock);
			goto next_server;

		default:
			vc->error = afs_abort_to_error(vc->ac.abort_code);
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
		vc->error = error;
		goto iterate_address;

	case -ECONNRESET:
		_debug("call reset");
		vc->error = error;
		vc->flags |= AFS_VL_CURSOR_RETRY;
		goto next_server;

	case -EOPNOTSUPP:
		_debug("notsupp");
		goto next_server;
	}

restart_from_beginning:
	_debug("restart");
	afs_end_cursor(&vc->ac);
	afs_put_vlserverlist(vc->cell->net, vc->server_list);
	vc->server_list = NULL;
	if (vc->flags & AFS_VL_CURSOR_RETRIED)
		goto failed;
	vc->flags |= AFS_VL_CURSOR_RETRIED;
start:
	_debug("start");

	if (!afs_start_vl_iteration(vc))
		goto failed;

	error = afs_send_vl_probes(vc->cell->net, vc->key, vc->server_list);
	if (error < 0)
		goto failed_set_error;

pick_server:
	_debug("pick [%lx]", vc->untried);

	error = afs_wait_for_vl_probes(vc->server_list, vc->untried);
	if (error < 0)
		goto failed_set_error;

	/* Pick the untried server with the lowest RTT. */
	vc->index = vc->server_list->preferred;
	if (test_bit(vc->index, &vc->untried))
		goto selected_server;

	vc->index = -1;
	rtt = U32_MAX;
	for (i = 0; i < vc->server_list->nr_servers; i++) {
		struct afs_vlserver *s = vc->server_list->servers[i].server;

		if (!test_bit(i, &vc->untried) ||
		    !test_bit(AFS_VLSERVER_FL_RESPONDING, &s->flags))
			continue;
		if (s->probe.rtt < rtt) {
			vc->index = i;
			rtt = s->probe.rtt;
		}
	}

	if (vc->index == -1)
		goto no_more_servers;

selected_server:
	_debug("use %d", vc->index);
	__clear_bit(vc->index, &vc->untried);

	/* We're starting on a different vlserver from the list.  We need to
	 * check it, find its address list and probe its capabilities before we
	 * use it.
	 */
	ASSERTCMP(vc->ac.alist, ==, NULL);
	vlserver = vc->server_list->servers[vc->index].server;
	vc->server = vlserver;

	_debug("USING VLSERVER: %s", vlserver->name);

	read_lock(&vlserver->lock);
	alist = rcu_dereference_protected(vlserver->addresses,
					  lockdep_is_held(&vlserver->lock));
	afs_get_addrlist(alist);
	read_unlock(&vlserver->lock);

	memset(&vc->ac, 0, sizeof(vc->ac));

	if (!vc->ac.alist)
		vc->ac.alist = alist;
	else
		afs_put_addrlist(alist);

	vc->ac.index = -1;

iterate_address:
	ASSERT(vc->ac.alist);
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	if (!afs_iterate_addresses(&vc->ac))
		goto next_server;

	_debug("VL address %d/%d", vc->ac.index, vc->ac.alist->nr_addrs);

	_leave(" = t %pISpc", &vc->ac.alist->addrs[vc->ac.index].transport);
	return true;

next_server:
	_debug("next");
	afs_end_cursor(&vc->ac);
	goto pick_server;

no_more_servers:
	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	if (vc->flags & AFS_VL_CURSOR_RETRY)
		goto restart_from_beginning;

	e.error = -EDESTADDRREQ;
	e.responded = false;
	for (i = 0; i < vc->server_list->nr_servers; i++) {
		struct afs_vlserver *s = vc->server_list->servers[i].server;

		if (test_bit(AFS_VLSERVER_FL_RESPONDING, &s->flags))
			e.responded = true;
		afs_prioritise_error(&e, READ_ONCE(s->probe.error),
				     s->probe.abort_code);
	}

	error = e.error;

failed_set_error:
	vc->error = error;
failed:
	vc->flags |= AFS_VL_CURSOR_STOP;
	afs_end_cursor(&vc->ac);
	_leave(" = f [failed %d]", vc->error);
	return false;
}

/*
 * Dump cursor state in the case of the error being EDESTADDRREQ.
 */
static void afs_vl_dump_edestaddrreq(const struct afs_vl_cursor *vc)
{
	static int count;
	int i;

	if (!IS_ENABLED(CONFIG_AFS_DEBUG_CURSOR) || count > 3)
		return;
	count++;

	rcu_read_lock();
	pr_notice("EDESTADDR occurred\n");
	pr_notice("VC: ut=%lx ix=%u ni=%hu fl=%hx err=%hd\n",
		  vc->untried, vc->index, vc->nr_iterations, vc->flags, vc->error);

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
					  a->responded, a->failed);
				if (a == vc->ac.alist)
					pr_notice("VC:  - current\n");
			}
		}
	}

	pr_notice("AC: t=%lx ax=%u ac=%d er=%d r=%u ni=%u\n",
		  vc->ac.tried, vc->ac.index, vc->ac.abort_code, vc->ac.error,
		  vc->ac.responded, vc->ac.nr_iterations);
	rcu_read_unlock();
}

/*
 * Tidy up a volume location server cursor and unlock the vnode.
 */
int afs_end_vlserver_operation(struct afs_vl_cursor *vc)
{
	struct afs_net *net = vc->cell->net;

	if (vc->error == -EDESTADDRREQ ||
	    vc->error == -EADDRNOTAVAIL ||
	    vc->error == -ENETUNREACH ||
	    vc->error == -EHOSTUNREACH)
		afs_vl_dump_edestaddrreq(vc);

	afs_end_cursor(&vc->ac);
	afs_put_vlserverlist(net, vc->server_list);

	if (vc->error == -ECONNABORTED)
		vc->error = afs_abort_to_error(vc->ac.abort_code);

	return vc->error;
}
