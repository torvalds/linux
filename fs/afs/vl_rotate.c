/* Handle vlserver selection and rotation.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
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

	if (wait_on_bit(&cell->flags, AFS_CELL_FL_NO_LOOKUP_YET,
			TASK_INTERRUPTIBLE)) {
		vc->error = -ERESTARTSYS;
		return false;
	}

	read_lock(&cell->vl_servers_lock);
	vc->server_list = afs_get_vlserverlist(
		rcu_dereference_protected(cell->vl_servers,
					  lockdep_is_held(&cell->vl_servers_lock)));
	read_unlock(&cell->vl_servers_lock);
	if (!vc->server_list || !vc->server_list->nr_servers)
		return false;

	vc->start = READ_ONCE(vc->server_list->index);
	vc->index = vc->start;
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
	int error = vc->ac.error;

	_enter("%u/%u,%u/%u,%d,%d",
	       vc->index, vc->start,
	       vc->ac.index, vc->ac.start,
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

	case -ENETUNREACH:
	case -EHOSTUNREACH:
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

	/* TODO: Consider checking the VL server list */

	if (!afs_start_vl_iteration(vc))
		goto failed;

use_server:
	_debug("use");
	/* We're starting on a different vlserver from the list.  We need to
	 * check it, find its address list and probe its capabilities before we
	 * use it.
	 */
	ASSERTCMP(vc->ac.alist, ==, NULL);
	vlserver = vc->server_list->servers[vc->index].server;

	// TODO: Check the vlserver occasionally
	//if (!afs_check_vlserver_record(vc, vlserver))
	//	goto failed;

	_debug("USING VLSERVER: %s", vlserver->name);

	read_lock(&vlserver->lock);
	alist = rcu_dereference_protected(vlserver->addresses,
					  lockdep_is_held(&vlserver->lock));
	afs_get_addrlist(alist);
	read_unlock(&vlserver->lock);

	memset(&vc->ac, 0, sizeof(vc->ac));

	/* Probe the current vlserver if we haven't done so yet. */
#if 0 // TODO
	if (!test_bit(AFS_VLSERVER_FL_PROBED, &vlserver->flags)) {
		vc->ac.alist = afs_get_addrlist(alist);

		if (!afs_probe_vlserver(vc)) {
			error = vc->ac.error;
			switch (error) {
			case -ENOMEM:
			case -ERESTARTSYS:
			case -EINTR:
				goto failed_set_error;
			default:
				goto next_server;
			}
		}
	}
#endif

	if (!vc->ac.alist)
		vc->ac.alist = alist;
	else
		afs_put_addrlist(alist);

	vc->ac.start = READ_ONCE(alist->index);
	vc->ac.index = vc->ac.start;

iterate_address:
	ASSERT(vc->ac.alist);
	_debug("iterate %d/%d", vc->ac.index, vc->ac.alist->nr_addrs);
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	if (!afs_iterate_addresses(&vc->ac))
		goto next_server;

	_leave(" = t %pISpc", &vc->ac.alist->addrs[vc->ac.index].transport);
	return true;

next_server:
	_debug("next");
	afs_end_cursor(&vc->ac);
	vc->index++;
	if (vc->index >= vc->server_list->nr_servers)
		vc->index = 0;
	if (vc->index != vc->start)
		goto use_server;

	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	if (vc->flags & AFS_VL_CURSOR_RETRY)
		goto restart_from_beginning;

	goto failed;

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
	pr_notice("VC: st=%u ix=%u ni=%hu fl=%hx err=%hd\n",
		  vc->start, vc->index, vc->nr_iterations, vc->flags, vc->error);

	if (vc->server_list) {
		const struct afs_vlserver_list *sl = vc->server_list;
		pr_notice("VC: SL nr=%u ix=%u\n",
			  sl->nr_servers, sl->index);
		for (i = 0; i < sl->nr_servers; i++) {
			const struct afs_vlserver *s = sl->servers[i].server;
			pr_notice("VC: server fl=%lx %s+%hu\n",
				  s->flags, s->name, s->port);
			if (s->addresses) {
				const struct afs_addr_list *a =
					rcu_dereference(s->addresses);
				pr_notice("VC:  - av=%u nr=%u/%u/%u ax=%u\n",
					  a->version,
					  a->nr_ipv4, a->nr_addrs, a->max_addrs,
					  a->index);
				pr_notice("VC:  - pr=%lx yf=%lx\n",
					  a->probed, a->yfs);
				if (a == vc->ac.alist)
					pr_notice("VC:  - current\n");
			}
		}
	}

	pr_notice("AC: as=%u ax=%u ac=%d er=%d b=%u r=%u ni=%hu\n",
		  vc->ac.start, vc->ac.index, vc->ac.abort_code, vc->ac.error,
		  vc->ac.begun, vc->ac.responded, vc->ac.nr_iterations);
	rcu_read_unlock();
}

/*
 * Tidy up a volume location server cursor and unlock the vnode.
 */
int afs_end_vlserver_operation(struct afs_vl_cursor *vc)
{
	struct afs_net *net = vc->cell->net;

	if (vc->error == -EDESTADDRREQ ||
	    vc->error == -ENETUNREACH ||
	    vc->error == -EHOSTUNREACH)
		afs_vl_dump_edestaddrreq(vc);

	afs_end_cursor(&vc->ac);
	afs_put_vlserverlist(net, vc->server_list);

	if (vc->error == -ECONNABORTED)
		vc->error = afs_abort_to_error(vc->ac.abort_code);

	return vc->error;
}
