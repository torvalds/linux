// SPDX-License-Identifier: GPL-2.0-or-later
/* Handle fileserver selection and rotation.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include "internal.h"
#include "afs_fs.h"
#include "protocol_uae.h"

void afs_clear_server_states(struct afs_operation *op)
{
	unsigned int i;

	if (op->server_states) {
		for (i = 0; i < op->server_list->nr_servers; i++)
			afs_put_endpoint_state(op->server_states[i].endpoint_state,
					       afs_estate_trace_put_server_state);
		kfree(op->server_states);
	}
}

/*
 * Begin iteration through a server list, starting with the vnode's last used
 * server if possible, or the last recorded good server if not.
 */
static bool afs_start_fs_iteration(struct afs_operation *op,
				   struct afs_vnode *vnode)
{
	struct afs_server *server;
	void *cb_server;
	int i;

	trace_afs_rotate(op, afs_rotate_trace_start, 0);

	read_lock(&op->volume->servers_lock);
	op->server_list = afs_get_serverlist(
		rcu_dereference_protected(op->volume->servers,
					  lockdep_is_held(&op->volume->servers_lock)));
	read_unlock(&op->volume->servers_lock);

	op->server_states = kcalloc(op->server_list->nr_servers, sizeof(op->server_states[0]),
				    GFP_KERNEL);
	if (!op->server_states) {
		afs_op_nomem(op);
		trace_afs_rotate(op, afs_rotate_trace_nomem, 0);
		return false;
	}

	rcu_read_lock();
	for (i = 0; i < op->server_list->nr_servers; i++) {
		struct afs_endpoint_state *estate;
		struct afs_server_state *s = &op->server_states[i];

		server = op->server_list->servers[i].server;
		estate = rcu_dereference(server->endpoint_state);
		s->endpoint_state = afs_get_endpoint_state(estate,
							   afs_estate_trace_get_server_state);
		s->probe_seq = estate->probe_seq;
		s->untried_addrs = (1UL << estate->addresses->nr_addrs) - 1;
		init_waitqueue_entry(&s->probe_waiter, current);
		afs_get_address_preferences(op->net, estate->addresses);
	}
	rcu_read_unlock();


	op->untried_servers = (1UL << op->server_list->nr_servers) - 1;
	op->server_index = -1;

	cb_server = vnode->cb_server;
	if (cb_server) {
		/* See if the vnode's preferred record is still available */
		for (i = 0; i < op->server_list->nr_servers; i++) {
			server = op->server_list->servers[i].server;
			if (server == cb_server) {
				op->server_index = i;
				goto found_interest;
			}
		}

		/* If we have a lock outstanding on a server that's no longer
		 * serving this vnode, then we can't switch to another server
		 * and have to return an error.
		 */
		if (op->flags & AFS_OPERATION_CUR_ONLY) {
			afs_op_set_error(op, -ESTALE);
			trace_afs_rotate(op, afs_rotate_trace_stale_lock, 0);
			return false;
		}

		/* Note that the callback promise is effectively broken */
		write_seqlock(&vnode->cb_lock);
		ASSERTCMP(cb_server, ==, vnode->cb_server);
		vnode->cb_server = NULL;
		if (afs_clear_cb_promise(vnode, afs_cb_promise_clear_rotate_server))
			vnode->cb_break++;
		write_sequnlock(&vnode->cb_lock);
	}

found_interest:
	return true;
}

/*
 * Post volume busy note.
 */
static void afs_busy(struct afs_operation *op, u32 abort_code)
{
	const char *m;

	switch (abort_code) {
	case VOFFLINE:		m = "offline";		break;
	case VRESTARTING:	m = "restarting";	break;
	case VSALVAGING:	m = "being salvaged";	break;
	default:		m = "busy";		break;
	}

	pr_notice("kAFS: Volume %llu '%s' on server %pU is %s\n",
		  op->volume->vid, op->volume->name, &op->server->uuid, m);
}

/*
 * Sleep and retry the operation to the same fileserver.
 */
static bool afs_sleep_and_retry(struct afs_operation *op)
{
	trace_afs_rotate(op, afs_rotate_trace_busy_sleep, 0);
	if (!(op->flags & AFS_OPERATION_UNINTR)) {
		msleep_interruptible(1000);
		if (signal_pending(current)) {
			afs_op_set_error(op, -ERESTARTSYS);
			return false;
		}
	} else {
		msleep(1000);
	}

	return true;
}

/*
 * Select the fileserver to use.  May be called multiple times to rotate
 * through the fileservers.
 */
bool afs_select_fileserver(struct afs_operation *op)
{
	struct afs_addr_list *alist;
	struct afs_server *server;
	struct afs_vnode *vnode = op->file[0].vnode;
	unsigned long set, failed;
	s32 abort_code = op->call_abort_code;
	int best_prio = 0;
	int error = op->call_error, addr_index, i, j;

	op->nr_iterations++;

	_enter("OP=%x+%x,%llx,%u{%lx},%u{%lx},%d,%d",
	       op->debug_id, op->nr_iterations, op->volume->vid,
	       op->server_index, op->untried_servers,
	       op->addr_index, op->addr_tried,
	       error, abort_code);

	if (op->flags & AFS_OPERATION_STOP) {
		trace_afs_rotate(op, afs_rotate_trace_stopped, 0);
		_leave(" = f [stopped]");
		return false;
	}

	if (op->nr_iterations == 0)
		goto start;

	WRITE_ONCE(op->estate->addresses->addrs[op->addr_index].last_error, error);
	trace_afs_rotate(op, afs_rotate_trace_iter, op->call_error);

	/* Evaluate the result of the previous operation, if there was one. */
	switch (op->call_error) {
	case 0:
		clear_bit(AFS_SE_VOLUME_OFFLINE,
			  &op->server_list->servers[op->server_index].flags);
		clear_bit(AFS_SE_VOLUME_BUSY,
			  &op->server_list->servers[op->server_index].flags);
		op->cumul_error.responded = true;

		/* We succeeded, but we may need to redo the op from another
		 * server if we're looking at a set of RO volumes where some of
		 * the servers have not yet been brought up to date lest we
		 * regress the data.  We only switch to the new version once
		 * >=50% of the servers are updated.
		 */
		error = afs_update_volume_state(op);
		if (error != 0) {
			if (error == 1) {
				afs_sleep_and_retry(op);
				goto restart_from_beginning;
			}
			afs_op_set_error(op, error);
			goto failed;
		}
		fallthrough;
	default:
		/* Success or local failure.  Stop. */
		afs_op_set_error(op, error);
		op->flags |= AFS_OPERATION_STOP;
		trace_afs_rotate(op, afs_rotate_trace_stop, error);
		_leave(" = f [okay/local %d]", error);
		return false;

	case -ECONNABORTED:
		/* The far side rejected the operation on some grounds.  This
		 * might involve the server being busy or the volume having been moved.
		 *
		 * Note that various V* errors should not be sent to a cache manager
		 * by a fileserver as they should be translated to more modern UAE*
		 * errors instead.  IBM AFS and OpenAFS fileservers, however, do leak
		 * these abort codes.
		 */
		trace_afs_rotate(op, afs_rotate_trace_aborted, abort_code);
		op->cumul_error.responded = true;
		switch (abort_code) {
		case VNOVOL:
			/* This fileserver doesn't know about the volume.
			 * - May indicate that the VL is wrong - retry once and compare
			 *   the results.
			 * - May indicate that the fileserver couldn't attach to the vol.
			 * - The volume might have been temporarily removed so that it can
			 *   be replaced by a volume restore.  "vos" might have ended one
			 *   transaction and has yet to create the next.
			 * - The volume might not be blessed or might not be in-service
			 *   (administrative action).
			 */
			if (op->flags & AFS_OPERATION_VNOVOL) {
				afs_op_accumulate_error(op, -EREMOTEIO, abort_code);
				goto next_server;
			}

			write_lock(&op->volume->servers_lock);
			op->server_list->vnovol_mask |= 1 << op->server_index;
			write_unlock(&op->volume->servers_lock);

			set_bit(AFS_VOLUME_NEEDS_UPDATE, &op->volume->flags);
			error = afs_check_volume_status(op->volume, op);
			if (error < 0) {
				afs_op_set_error(op, error);
				goto failed;
			}

			if (test_bit(AFS_VOLUME_DELETED, &op->volume->flags)) {
				afs_op_set_error(op, -ENOMEDIUM);
				goto failed;
			}

			/* If the server list didn't change, then assume that
			 * it's the fileserver having trouble.
			 */
			if (rcu_access_pointer(op->volume->servers) == op->server_list) {
				afs_op_accumulate_error(op, -EREMOTEIO, abort_code);
				goto next_server;
			}

			/* Try again */
			op->flags |= AFS_OPERATION_VNOVOL;
			_leave(" = t [vnovol]");
			return true;

		case VVOLEXISTS:
		case VONLINE:
			/* These should not be returned from the fileserver. */
			pr_warn("Fileserver returned unexpected abort %d\n",
				abort_code);
			afs_op_accumulate_error(op, -EREMOTEIO, abort_code);
			goto next_server;

		case VNOSERVICE:
			/* Prior to AFS 3.2 VNOSERVICE was returned from the fileserver
			 * if the volume was neither in-service nor administratively
			 * blessed.  All usage was replaced by VNOVOL because AFS 3.1 and
			 * earlier cache managers did not handle VNOSERVICE and assumed
			 * it was the client OSes errno 105.
			 *
			 * Starting with OpenAFS 1.4.8 VNOSERVICE was repurposed as the
			 * fileserver idle dead time error which was sent in place of
			 * RX_CALL_TIMEOUT (-3).  The error was intended to be sent if the
			 * fileserver took too long to send a reply to the client.
			 * RX_CALL_TIMEOUT would have caused the cache manager to mark the
			 * server down whereas VNOSERVICE since AFS 3.2 would cause cache
			 * manager to temporarily (up to 15 minutes) mark the volume
			 * instance as unusable.
			 *
			 * The idle dead logic resulted in cache inconsistency since a
			 * state changing call that the cache manager assumed was dead
			 * could still be processed to completion by the fileserver.  This
			 * logic was removed in OpenAFS 1.8.0 and VNOSERVICE is no longer
			 * returned.  However, many 1.4.8 through 1.6.24 fileservers are
			 * still in existence.
			 *
			 * AuriStorFS fileservers have never returned VNOSERVICE.
			 *
			 * VNOSERVICE should be treated as an alias for RX_CALL_TIMEOUT.
			 */
		case RX_CALL_TIMEOUT:
			afs_op_accumulate_error(op, -ETIMEDOUT, abort_code);
			goto next_server;

		case VSALVAGING: /* This error should not be leaked to cache managers
				  * but is from OpenAFS demand attach fileservers.
				  * It should be treated as an alias for VOFFLINE.
				  */
		case VSALVAGE: /* VSALVAGE should be treated as a synonym of VOFFLINE */
		case VOFFLINE:
			/* The volume is in use by the volserver or another volume utility
			 * for an operation that might alter the contents.  The volume is
			 * expected to come back but it might take a long time (could be
			 * days).
			 */
			if (!test_and_set_bit(AFS_SE_VOLUME_OFFLINE,
					      &op->server_list->servers[op->server_index].flags)) {
				afs_busy(op, abort_code);
				clear_bit(AFS_SE_VOLUME_BUSY,
					  &op->server_list->servers[op->server_index].flags);
			}
			if (op->flags & AFS_OPERATION_NO_VSLEEP) {
				afs_op_set_error(op, -EADV);
				goto failed;
			}
			goto busy;

		case VRESTARTING: /* The fileserver is either shutting down or starting up. */
		case VBUSY:
			/* The volume is in use by the volserver or another volume
			 * utility for an operation that is not expected to alter the
			 * contents of the volume.  VBUSY does not need to be returned
			 * for a ROVOL or BACKVOL bound to an ITBusy volserver
			 * transaction.  The fileserver is permitted to continue serving
			 * content from ROVOLs and BACKVOLs during an ITBusy transaction
			 * because the content will not change.  However, many fileserver
			 * releases do return VBUSY for ROVOL and BACKVOL instances under
			 * many circumstances.
			 *
			 * Retry after going round all the servers unless we have a file
			 * lock we need to maintain.
			 */
			if (op->flags & AFS_OPERATION_NO_VSLEEP) {
				afs_op_set_error(op, -EBUSY);
				goto failed;
			}
			if (!test_and_set_bit(AFS_SE_VOLUME_BUSY,
					      &op->server_list->servers[op->server_index].flags)) {
				afs_busy(op, abort_code);
				clear_bit(AFS_SE_VOLUME_OFFLINE,
					  &op->server_list->servers[op->server_index].flags);
			}
		busy:
			if (op->flags & AFS_OPERATION_CUR_ONLY) {
				if (!afs_sleep_and_retry(op))
					goto failed;

				/* Retry with same server & address */
				_leave(" = t [vbusy]");
				return true;
			}

			op->flags |= AFS_OPERATION_VBUSY;
			goto next_server;

		case VMOVED:
			/* The volume migrated to another server.  We consider
			 * consider all locks and callbacks broken and request
			 * an update from the VLDB.
			 *
			 * We also limit the number of VMOVED hops we will
			 * honour, just in case someone sets up a loop.
			 */
			if (op->flags & AFS_OPERATION_VMOVED) {
				afs_op_set_error(op, -EREMOTEIO);
				goto failed;
			}
			op->flags |= AFS_OPERATION_VMOVED;

			set_bit(AFS_VOLUME_WAIT, &op->volume->flags);
			set_bit(AFS_VOLUME_NEEDS_UPDATE, &op->volume->flags);
			error = afs_check_volume_status(op->volume, op);
			if (error < 0) {
				afs_op_set_error(op, error);
				goto failed;
			}

			/* If the server list didn't change, then the VLDB is
			 * out of sync with the fileservers.  This is hopefully
			 * a temporary condition, however, so we don't want to
			 * permanently block access to the file.
			 *
			 * TODO: Try other fileservers if we can.
			 *
			 * TODO: Retry a few times with sleeps.
			 */
			if (rcu_access_pointer(op->volume->servers) == op->server_list) {
				afs_op_accumulate_error(op, -ENOMEDIUM, abort_code);
				goto failed;
			}

			goto restart_from_beginning;

		case UAEIO:
		case VIO:
			afs_op_accumulate_error(op, -EREMOTEIO, abort_code);
			if (op->volume->type != AFSVL_RWVOL)
				goto next_server;
			goto failed;

		case VDISKFULL:
		case UAENOSPC:
			/* The partition is full.  Only applies to RWVOLs.
			 * Translate locally and return ENOSPC.
			 * No replicas to failover to.
			 */
			afs_op_set_error(op, -ENOSPC);
			goto failed_but_online;

		case VOVERQUOTA:
		case UAEDQUOT:
			/* Volume is full.  Only applies to RWVOLs.
			 * Translate locally and return EDQUOT.
			 * No replicas to failover to.
			 */
			afs_op_set_error(op, -EDQUOT);
			goto failed_but_online;

		case RX_INVALID_OPERATION:
		case RXGEN_OPCODE:
			/* Handle downgrading to an older operation. */
			afs_op_set_error(op, -ENOTSUPP);
			if (op->flags & AFS_OPERATION_DOWNGRADE) {
				op->flags &= ~AFS_OPERATION_DOWNGRADE;
				goto go_again;
			}
			goto failed_but_online;

		default:
			afs_op_accumulate_error(op, error, abort_code);
		failed_but_online:
			clear_bit(AFS_SE_VOLUME_OFFLINE,
				  &op->server_list->servers[op->server_index].flags);
			clear_bit(AFS_SE_VOLUME_BUSY,
				  &op->server_list->servers[op->server_index].flags);
			goto failed;
		}

	case -ETIMEDOUT:
	case -ETIME:
		if (afs_op_error(op) != -EDESTADDRREQ)
			goto iterate_address;
		fallthrough;
	case -ERFKILL:
	case -EADDRNOTAVAIL:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EHOSTDOWN:
	case -ECONNREFUSED:
		_debug("no conn");
		afs_op_accumulate_error(op, error, 0);
		goto iterate_address;

	case -ENETRESET:
		pr_warn("kAFS: Peer reset %s (op=%x)\n",
			op->type ? op->type->name : "???", op->debug_id);
		fallthrough;
	case -ECONNRESET:
		_debug("call reset");
		afs_op_set_error(op, error);
		goto failed;
	}

restart_from_beginning:
	trace_afs_rotate(op, afs_rotate_trace_restart, 0);
	_debug("restart");
	op->estate = NULL;
	op->server = NULL;
	afs_clear_server_states(op);
	op->server_states = NULL;
	afs_put_serverlist(op->net, op->server_list);
	op->server_list = NULL;
start:
	_debug("start");
	ASSERTCMP(op->estate, ==, NULL);
	/* See if we need to do an update of the volume record.  Note that the
	 * volume may have moved or even have been deleted.
	 */
	error = afs_check_volume_status(op->volume, op);
	trace_afs_rotate(op, afs_rotate_trace_check_vol_status, error);
	if (error < 0) {
		afs_op_set_error(op, error);
		goto failed;
	}

	if (!afs_start_fs_iteration(op, vnode))
		goto failed;

	_debug("__ VOL %llx __", op->volume->vid);

pick_server:
	_debug("pick [%lx]", op->untried_servers);
	ASSERTCMP(op->estate, ==, NULL);

	error = afs_wait_for_fs_probes(op, op->server_states,
				       !(op->flags & AFS_OPERATION_UNINTR));
	switch (error) {
	case 0: /* No untried responsive servers and no outstanding probes */
		trace_afs_rotate(op, afs_rotate_trace_probe_none, 0);
		goto no_more_servers;
	case 1: /* Got a response */
		trace_afs_rotate(op, afs_rotate_trace_probe_response, 0);
		break;
	case 2: /* Probe data superseded */
		trace_afs_rotate(op, afs_rotate_trace_probe_superseded, 0);
		goto restart_from_beginning;
	default:
		trace_afs_rotate(op, afs_rotate_trace_probe_error, error);
		afs_op_set_error(op, error);
		goto failed;
	}

	/* Pick the untried server with the highest priority untried endpoint.
	 * If we have outstanding callbacks, we stick with the server we're
	 * already using if we can.
	 */
	if (op->server) {
		_debug("server %u", op->server_index);
		if (test_bit(op->server_index, &op->untried_servers))
			goto selected_server;
		op->server = NULL;
		_debug("no server");
	}

	rcu_read_lock();
	op->server_index = -1;
	best_prio = -1;
	for (i = 0; i < op->server_list->nr_servers; i++) {
		struct afs_endpoint_state *es;
		struct afs_server_entry *se = &op->server_list->servers[i];
		struct afs_addr_list *sal;
		struct afs_server *s = se->server;

		if (!test_bit(i, &op->untried_servers) ||
		    test_bit(AFS_SE_EXCLUDED, &se->flags) ||
		    !test_bit(AFS_SERVER_FL_RESPONDING, &s->flags))
			continue;
		es = op->server_states[i].endpoint_state;
		sal = es->addresses;

		afs_get_address_preferences_rcu(op->net, sal);
		for (j = 0; j < sal->nr_addrs; j++) {
			if (es->failed_set & (1 << j))
				continue;
			if (!sal->addrs[j].peer)
				continue;
			if (sal->addrs[j].prio > best_prio) {
				op->server_index = i;
				best_prio = sal->addrs[j].prio;
			}
		}
	}
	rcu_read_unlock();

	if (op->server_index == -1)
		goto no_more_servers;

selected_server:
	trace_afs_rotate(op, afs_rotate_trace_selected_server, best_prio);
	_debug("use %d prio %u", op->server_index, best_prio);
	__clear_bit(op->server_index, &op->untried_servers);

	/* We're starting on a different fileserver from the list.  We need to
	 * check it, create a callback intercept, find its address list and
	 * probe its capabilities before we use it.
	 */
	ASSERTCMP(op->estate, ==, NULL);
	server = op->server_list->servers[op->server_index].server;

	if (!afs_check_server_record(op, server, op->key))
		goto failed;

	_debug("USING SERVER: %pU", &server->uuid);

	op->flags |= AFS_OPERATION_RETRY_SERVER;
	op->server = server;
	if (vnode->cb_server != server) {
		vnode->cb_server = server;
		vnode->cb_v_check = atomic_read(&vnode->volume->cb_v_break);
		afs_clear_cb_promise(vnode, afs_cb_promise_clear_server_change);
	}

retry_server:
	op->addr_tried = 0;
	op->addr_index = -1;

iterate_address:
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	op->estate = op->server_states[op->server_index].endpoint_state;
	set = READ_ONCE(op->estate->responsive_set);
	failed = READ_ONCE(op->estate->failed_set);
	_debug("iterate ES=%x rs=%lx fs=%lx", op->estate->probe_seq, set, failed);
	set &= ~(failed | op->addr_tried);
	trace_afs_rotate(op, afs_rotate_trace_iterate_addr, set);
	if (!set)
		goto wait_for_more_probe_results;

	alist = op->estate->addresses;
	best_prio = -1;
	addr_index = 0;
	for (i = 0; i < alist->nr_addrs; i++) {
		if (!(set & (1 << i)))
			continue;
		if (alist->addrs[i].prio > best_prio) {
			addr_index = i;
			best_prio = alist->addrs[i].prio;
		}
	}

	alist->preferred = addr_index;

	op->addr_index = addr_index;
	set_bit(addr_index, &op->addr_tried);

	_debug("address [%u] %u/%u %pISp",
	       op->server_index, addr_index, alist->nr_addrs,
	       rxrpc_kernel_remote_addr(alist->addrs[op->addr_index].peer));
go_again:
	op->volsync.creation = TIME64_MIN;
	op->volsync.update = TIME64_MIN;
	op->call_responded = false;
	_leave(" = t");
	return true;

wait_for_more_probe_results:
	error = afs_wait_for_one_fs_probe(op->server, op->estate, op->addr_tried,
					  !(op->flags & AFS_OPERATION_UNINTR));
	if (error == 1)
		goto iterate_address;
	if (!error)
		goto restart_from_beginning;

	/* We've now had a failure to respond on all of a server's addresses -
	 * immediately probe them again and consider retrying the server.
	 */
	trace_afs_rotate(op, afs_rotate_trace_probe_fileserver, 0);
	afs_probe_fileserver(op->net, op->server);
	if (op->flags & AFS_OPERATION_RETRY_SERVER) {
		error = afs_wait_for_one_fs_probe(op->server, op->estate, op->addr_tried,
						  !(op->flags & AFS_OPERATION_UNINTR));
		switch (error) {
		case 1:
			op->flags &= ~AFS_OPERATION_RETRY_SERVER;
			trace_afs_rotate(op, afs_rotate_trace_retry_server, 1);
			goto retry_server;
		case 0:
			trace_afs_rotate(op, afs_rotate_trace_retry_server, 0);
			goto restart_from_beginning;
		case -ERESTARTSYS:
			afs_op_set_error(op, error);
			goto failed;
		case -ETIME:
		case -EDESTADDRREQ:
			goto next_server;
		}
	}

next_server:
	trace_afs_rotate(op, afs_rotate_trace_next_server, 0);
	_debug("next");
	op->estate = NULL;
	goto pick_server;

no_more_servers:
	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	trace_afs_rotate(op, afs_rotate_trace_no_more_servers, 0);
	if (op->flags & AFS_OPERATION_VBUSY) {
		afs_sleep_and_retry(op);
		op->flags &= ~AFS_OPERATION_VBUSY;
		goto restart_from_beginning;
	}

	rcu_read_lock();
	for (i = 0; i < op->server_list->nr_servers; i++) {
		struct afs_endpoint_state *estate;

		estate = op->server_states[i].endpoint_state;
		error = READ_ONCE(estate->error);
		if (error < 0)
			afs_op_accumulate_error(op, error, estate->abort_code);
	}
	rcu_read_unlock();

failed:
	trace_afs_rotate(op, afs_rotate_trace_failed, 0);
	op->flags |= AFS_OPERATION_STOP;
	op->estate = NULL;
	_leave(" = f [failed %d]", afs_op_error(op));
	return false;
}

/*
 * Dump cursor state in the case of the error being EDESTADDRREQ.
 */
void afs_dump_edestaddrreq(const struct afs_operation *op)
{
	static int count;
	int i;

	if (!IS_ENABLED(CONFIG_AFS_DEBUG_CURSOR) || count > 3)
		return;
	count++;

	rcu_read_lock();

	pr_notice("EDESTADDR occurred\n");
	pr_notice("OP: cbb=%x cbb2=%x fl=%x err=%hd\n",
		  op->file[0].cb_break_before,
		  op->file[1].cb_break_before, op->flags, op->cumul_error.error);
	pr_notice("OP: ut=%lx ix=%d ni=%u\n",
		  op->untried_servers, op->server_index, op->nr_iterations);
	pr_notice("OP: call  er=%d ac=%d r=%u\n",
		  op->call_error, op->call_abort_code, op->call_responded);

	if (op->server_list) {
		const struct afs_server_list *sl = op->server_list;

		pr_notice("FC: SL nr=%u vnov=%hx\n",
			  sl->nr_servers, sl->vnovol_mask);
		for (i = 0; i < sl->nr_servers; i++) {
			const struct afs_server *s = sl->servers[i].server;
			const struct afs_endpoint_state *e =
				rcu_dereference(s->endpoint_state);
			const struct afs_addr_list *a = e->addresses;

			pr_notice("FC: server fl=%lx av=%u %pU\n",
				  s->flags, s->addr_version, &s->uuid);
			pr_notice("FC:  - pq=%x R=%lx F=%lx\n",
				  e->probe_seq, e->responsive_set, e->failed_set);
			if (a) {
				pr_notice("FC:  - av=%u nr=%u/%u/%u pr=%u\n",
					  a->version,
					  a->nr_ipv4, a->nr_addrs, a->max_addrs,
					  a->preferred);
				if (a == e->addresses)
					pr_notice("FC:  - current\n");
			}
		}
	}

	pr_notice("AC: t=%lx ax=%d\n", op->addr_tried, op->addr_index);
	rcu_read_unlock();
}
