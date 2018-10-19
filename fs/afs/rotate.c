/* Handle fileserver selection and rotation.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include "internal.h"
#include "afs_fs.h"

/*
 * Begin an operation on the fileserver.
 *
 * Fileserver operations are serialised on the server by vnode, so we serialise
 * them here also using the io_lock.
 */
bool afs_begin_vnode_operation(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
			       struct key *key)
{
	memset(fc, 0, sizeof(*fc));
	fc->vnode = vnode;
	fc->key = key;
	fc->ac.error = SHRT_MAX;
	fc->error = -EDESTADDRREQ;

	if (mutex_lock_interruptible(&vnode->io_lock) < 0) {
		fc->error = -EINTR;
		fc->flags |= AFS_FS_CURSOR_STOP;
		return false;
	}

	if (vnode->lock_state != AFS_VNODE_LOCK_NONE)
		fc->flags |= AFS_FS_CURSOR_CUR_ONLY;
	return true;
}

/*
 * Begin iteration through a server list, starting with the vnode's last used
 * server if possible, or the last recorded good server if not.
 */
static bool afs_start_fs_iteration(struct afs_fs_cursor *fc,
				   struct afs_vnode *vnode)
{
	struct afs_cb_interest *cbi;
	int i;

	read_lock(&vnode->volume->servers_lock);
	fc->server_list = afs_get_serverlist(vnode->volume->servers);
	read_unlock(&vnode->volume->servers_lock);

	fc->untried = (1UL << fc->server_list->nr_servers) - 1;
	fc->index = READ_ONCE(fc->server_list->preferred);

	cbi = vnode->cb_interest;
	if (cbi) {
		/* See if the vnode's preferred record is still available */
		for (i = 0; i < fc->server_list->nr_servers; i++) {
			if (fc->server_list->servers[i].cb_interest == cbi) {
				fc->index = i;
				goto found_interest;
			}
		}

		/* If we have a lock outstanding on a server that's no longer
		 * serving this vnode, then we can't switch to another server
		 * and have to return an error.
		 */
		if (fc->flags & AFS_FS_CURSOR_CUR_ONLY) {
			fc->error = -ESTALE;
			return false;
		}

		/* Note that the callback promise is effectively broken */
		write_seqlock(&vnode->cb_lock);
		ASSERTCMP(cbi, ==, vnode->cb_interest);
		vnode->cb_interest = NULL;
		if (test_and_clear_bit(AFS_VNODE_CB_PROMISED, &vnode->flags))
			vnode->cb_break++;
		write_sequnlock(&vnode->cb_lock);

		afs_put_cb_interest(afs_v2net(vnode), cbi);
		cbi = NULL;
	}

found_interest:
	return true;
}

/*
 * Post volume busy note.
 */
static void afs_busy(struct afs_volume *volume, u32 abort_code)
{
	const char *m;

	switch (abort_code) {
	case VOFFLINE:		m = "offline";		break;
	case VRESTARTING:	m = "restarting";	break;
	case VSALVAGING:	m = "being salvaged";	break;
	default:		m = "busy";		break;
	}

	pr_notice("kAFS: Volume %llu '%s' is %s\n", volume->vid, volume->name, m);
}

/*
 * Sleep and retry the operation to the same fileserver.
 */
static bool afs_sleep_and_retry(struct afs_fs_cursor *fc)
{
	msleep_interruptible(1000);
	if (signal_pending(current)) {
		fc->error = -ERESTARTSYS;
		return false;
	}

	return true;
}

/*
 * Select the fileserver to use.  May be called multiple times to rotate
 * through the fileservers.
 */
bool afs_select_fileserver(struct afs_fs_cursor *fc)
{
	struct afs_addr_list *alist;
	struct afs_server *server;
	struct afs_vnode *vnode = fc->vnode;
	u32 rtt, abort_code;
	int error = fc->ac.error, i;

	_enter("%lx[%d],%lx[%d],%d,%d",
	       fc->untried, fc->index,
	       fc->ac.tried, fc->ac.index,
	       error, fc->ac.abort_code);

	if (fc->flags & AFS_FS_CURSOR_STOP) {
		_leave(" = f [stopped]");
		return false;
	}

	fc->nr_iterations++;

	/* Evaluate the result of the previous operation, if there was one. */
	switch (error) {
	case SHRT_MAX:
		goto start;

	case 0:
	default:
		/* Success or local failure.  Stop. */
		fc->error = error;
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [okay/local %d]", error);
		return false;

	case -ECONNABORTED:
		/* The far side rejected the operation on some grounds.  This
		 * might involve the server being busy or the volume having been moved.
		 */
		switch (fc->ac.abort_code) {
		case VNOVOL:
			/* This fileserver doesn't know about the volume.
			 * - May indicate that the VL is wrong - retry once and compare
			 *   the results.
			 * - May indicate that the fileserver couldn't attach to the vol.
			 */
			if (fc->flags & AFS_FS_CURSOR_VNOVOL) {
				fc->error = -EREMOTEIO;
				goto next_server;
			}

			write_lock(&vnode->volume->servers_lock);
			fc->server_list->vnovol_mask |= 1 << fc->index;
			write_unlock(&vnode->volume->servers_lock);

			set_bit(AFS_VOLUME_NEEDS_UPDATE, &vnode->volume->flags);
			error = afs_check_volume_status(vnode->volume, fc->key);
			if (error < 0)
				goto failed_set_error;

			if (test_bit(AFS_VOLUME_DELETED, &vnode->volume->flags)) {
				fc->error = -ENOMEDIUM;
				goto failed;
			}

			/* If the server list didn't change, then assume that
			 * it's the fileserver having trouble.
			 */
			if (vnode->volume->servers == fc->server_list) {
				fc->error = -EREMOTEIO;
				goto next_server;
			}

			/* Try again */
			fc->flags |= AFS_FS_CURSOR_VNOVOL;
			_leave(" = t [vnovol]");
			return true;

		case VSALVAGE: /* TODO: Should this return an error or iterate? */
		case VVOLEXISTS:
		case VNOSERVICE:
		case VONLINE:
		case VDISKFULL:
		case VOVERQUOTA:
			fc->error = afs_abort_to_error(fc->ac.abort_code);
			goto next_server;

		case VOFFLINE:
			if (!test_and_set_bit(AFS_VOLUME_OFFLINE, &vnode->volume->flags)) {
				afs_busy(vnode->volume, fc->ac.abort_code);
				clear_bit(AFS_VOLUME_BUSY, &vnode->volume->flags);
			}
			if (fc->flags & AFS_FS_CURSOR_NO_VSLEEP) {
				fc->error = -EADV;
				goto failed;
			}
			if (fc->flags & AFS_FS_CURSOR_CUR_ONLY) {
				fc->error = -ESTALE;
				goto failed;
			}
			goto busy;

		case VSALVAGING:
		case VRESTARTING:
		case VBUSY:
			/* Retry after going round all the servers unless we
			 * have a file lock we need to maintain.
			 */
			if (fc->flags & AFS_FS_CURSOR_NO_VSLEEP) {
				fc->error = -EBUSY;
				goto failed;
			}
			if (!test_and_set_bit(AFS_VOLUME_BUSY, &vnode->volume->flags)) {
				afs_busy(vnode->volume, fc->ac.abort_code);
				clear_bit(AFS_VOLUME_OFFLINE, &vnode->volume->flags);
			}
		busy:
			if (fc->flags & AFS_FS_CURSOR_CUR_ONLY) {
				if (!afs_sleep_and_retry(fc))
					goto failed;

				 /* Retry with same server & address */
				_leave(" = t [vbusy]");
				return true;
			}

			fc->flags |= AFS_FS_CURSOR_VBUSY;
			goto next_server;

		case VMOVED:
			/* The volume migrated to another server.  We consider
			 * consider all locks and callbacks broken and request
			 * an update from the VLDB.
			 *
			 * We also limit the number of VMOVED hops we will
			 * honour, just in case someone sets up a loop.
			 */
			if (fc->flags & AFS_FS_CURSOR_VMOVED) {
				fc->error = -EREMOTEIO;
				goto failed;
			}
			fc->flags |= AFS_FS_CURSOR_VMOVED;

			set_bit(AFS_VOLUME_WAIT, &vnode->volume->flags);
			set_bit(AFS_VOLUME_NEEDS_UPDATE, &vnode->volume->flags);
			error = afs_check_volume_status(vnode->volume, fc->key);
			if (error < 0)
				goto failed_set_error;

			/* If the server list didn't change, then the VLDB is
			 * out of sync with the fileservers.  This is hopefully
			 * a temporary condition, however, so we don't want to
			 * permanently block access to the file.
			 *
			 * TODO: Try other fileservers if we can.
			 *
			 * TODO: Retry a few times with sleeps.
			 */
			if (vnode->volume->servers == fc->server_list) {
				fc->error = -ENOMEDIUM;
				goto failed;
			}

			goto restart_from_beginning;

		default:
			clear_bit(AFS_VOLUME_OFFLINE, &vnode->volume->flags);
			clear_bit(AFS_VOLUME_BUSY, &vnode->volume->flags);
			fc->error = afs_abort_to_error(fc->ac.abort_code);
			goto failed;
		}

	case -ETIMEDOUT:
	case -ETIME:
		if (fc->error != -EDESTADDRREQ)
			goto iterate_address;
		/* Fall through */
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
		_debug("no conn");
		fc->error = error;
		goto iterate_address;

	case -ECONNRESET:
		_debug("call reset");
		fc->error = error;
		goto failed;
	}

restart_from_beginning:
	_debug("restart");
	afs_end_cursor(&fc->ac);
	afs_put_cb_interest(afs_v2net(vnode), fc->cbi);
	fc->cbi = NULL;
	afs_put_serverlist(afs_v2net(vnode), fc->server_list);
	fc->server_list = NULL;
start:
	_debug("start");
	/* See if we need to do an update of the volume record.  Note that the
	 * volume may have moved or even have been deleted.
	 */
	error = afs_check_volume_status(vnode->volume, fc->key);
	if (error < 0)
		goto failed_set_error;

	if (!afs_start_fs_iteration(fc, vnode))
		goto failed;

	_debug("__ VOL %llx __", vnode->volume->vid);
	error = afs_probe_fileservers(afs_v2net(vnode), fc->key, fc->server_list);
	if (error < 0)
		goto failed_set_error;

pick_server:
	_debug("pick [%lx]", fc->untried);

	error = afs_wait_for_fs_probes(fc->server_list, fc->untried);
	if (error < 0)
		goto failed_set_error;

	/* Pick the untried server with the lowest RTT.  If we have outstanding
	 * callbacks, we stick with the server we're already using if we can.
	 */
	if (fc->cbi) {
		_debug("cbi %u", fc->index);
		if (test_bit(fc->index, &fc->untried))
			goto selected_server;
		afs_put_cb_interest(afs_v2net(vnode), fc->cbi);
		fc->cbi = NULL;
		_debug("nocbi");
	}

	fc->index = -1;
	rtt = U32_MAX;
	for (i = 0; i < fc->server_list->nr_servers; i++) {
		struct afs_server *s = fc->server_list->servers[i].server;

		if (!test_bit(i, &fc->untried) || !s->probe.responded)
			continue;
		if (s->probe.rtt < rtt) {
			fc->index = i;
			rtt = s->probe.rtt;
		}
	}

	if (fc->index == -1)
		goto no_more_servers;

selected_server:
	_debug("use %d", fc->index);
	__clear_bit(fc->index, &fc->untried);

	/* We're starting on a different fileserver from the list.  We need to
	 * check it, create a callback intercept, find its address list and
	 * probe its capabilities before we use it.
	 */
	ASSERTCMP(fc->ac.alist, ==, NULL);
	server = fc->server_list->servers[fc->index].server;

	if (!afs_check_server_record(fc, server))
		goto failed;

	_debug("USING SERVER: %pU", &server->uuid);

	/* Make sure we've got a callback interest record for this server.  We
	 * have to link it in before we send the request as we can be sent a
	 * break request before we've finished decoding the reply and
	 * installing the vnode.
	 */
	error = afs_register_server_cb_interest(vnode, fc->server_list,
						fc->index);
	if (error < 0)
		goto failed_set_error;

	fc->cbi = afs_get_cb_interest(vnode->cb_interest);

	read_lock(&server->fs_lock);
	alist = rcu_dereference_protected(server->addresses,
					  lockdep_is_held(&server->fs_lock));
	afs_get_addrlist(alist);
	read_unlock(&server->fs_lock);

	memset(&fc->ac, 0, sizeof(fc->ac));

	if (!fc->ac.alist)
		fc->ac.alist = alist;
	else
		afs_put_addrlist(alist);

	fc->ac.index = -1;

iterate_address:
	ASSERT(fc->ac.alist);
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	if (!afs_iterate_addresses(&fc->ac))
		goto next_server;

	_debug("address [%u] %u/%u", fc->index, fc->ac.index, fc->ac.alist->nr_addrs);

	_leave(" = t");
	return true;

next_server:
	_debug("next");
	afs_end_cursor(&fc->ac);
	goto pick_server;

no_more_servers:
	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	if (fc->flags & AFS_FS_CURSOR_VBUSY)
		goto restart_from_beginning;

	abort_code = 0;
	error = -EDESTADDRREQ;
	for (i = 0; i < fc->server_list->nr_servers; i++) {
		struct afs_server *s = fc->server_list->servers[i].server;
		int probe_error = READ_ONCE(s->probe.error);

		switch (probe_error) {
		case 0:
			continue;
		default:
			if (error == -ETIMEDOUT ||
			    error == -ETIME)
				continue;
		case -ETIMEDOUT:
		case -ETIME:
			if (error == -ENOMEM ||
			    error == -ENONET)
				continue;
		case -ENOMEM:
		case -ENONET:
			if (error == -ENETUNREACH)
				continue;
		case -ENETUNREACH:
			if (error == -EHOSTUNREACH)
				continue;
		case -EHOSTUNREACH:
			if (error == -ECONNREFUSED)
				continue;
		case -ECONNREFUSED:
			if (error == -ECONNRESET)
				continue;
		case -ECONNRESET: /* Responded, but call expired. */
			if (error == -ECONNABORTED)
				continue;
		case -ECONNABORTED:
			abort_code = s->probe.abort_code;
			error = probe_error;
			continue;
		}
	}

	if (error == -ECONNABORTED)
		error = afs_abort_to_error(abort_code);

failed_set_error:
	fc->error = error;
failed:
	fc->flags |= AFS_FS_CURSOR_STOP;
	afs_end_cursor(&fc->ac);
	_leave(" = f [failed %d]", fc->error);
	return false;
}

/*
 * Select the same fileserver we used for a vnode before and only that
 * fileserver.  We use this when we have a lock on that file, which is backed
 * only by the fileserver we obtained it from.
 */
bool afs_select_current_fileserver(struct afs_fs_cursor *fc)
{
	struct afs_vnode *vnode = fc->vnode;
	struct afs_cb_interest *cbi = vnode->cb_interest;
	struct afs_addr_list *alist;
	int error = fc->ac.error;

	_enter("");

	switch (error) {
	case SHRT_MAX:
		if (!cbi) {
			fc->error = -ESTALE;
			fc->flags |= AFS_FS_CURSOR_STOP;
			return false;
		}

		fc->cbi = afs_get_cb_interest(vnode->cb_interest);

		read_lock(&cbi->server->fs_lock);
		alist = rcu_dereference_protected(cbi->server->addresses,
						  lockdep_is_held(&cbi->server->fs_lock));
		afs_get_addrlist(alist);
		read_unlock(&cbi->server->fs_lock);
		if (!alist) {
			fc->error = -ESTALE;
			fc->flags |= AFS_FS_CURSOR_STOP;
			return false;
		}

		memset(&fc->ac, 0, sizeof(fc->ac));
		fc->ac.alist = alist;
		fc->ac.index = -1;
		goto iterate_address;

	case 0:
	default:
		/* Success or local failure.  Stop. */
		fc->error = error;
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [okay/local %d]", error);
		return false;

	case -ECONNABORTED:
		fc->error = afs_abort_to_error(fc->ac.abort_code);
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [abort]");
		return false;

	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -ETIME:
		_debug("no conn");
		fc->error = error;
		goto iterate_address;
	}

iterate_address:
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	if (afs_iterate_addresses(&fc->ac)) {
		_leave(" = t");
		return true;
	}

	afs_end_cursor(&fc->ac);
	return false;
}

/*
 * Dump cursor state in the case of the error being EDESTADDRREQ.
 */
static void afs_dump_edestaddrreq(const struct afs_fs_cursor *fc)
{
	static int count;
	int i;

	if (!IS_ENABLED(CONFIG_AFS_DEBUG_CURSOR) || count > 3)
		return;
	count++;

	rcu_read_lock();

	pr_notice("EDESTADDR occurred\n");
	pr_notice("FC: cbb=%x cbb2=%x fl=%hx err=%hd\n",
		  fc->cb_break, fc->cb_break_2, fc->flags, fc->error);
	pr_notice("FC: ut=%lx ix=%d ni=%u\n",
		  fc->untried, fc->index, fc->nr_iterations);

	if (fc->server_list) {
		const struct afs_server_list *sl = fc->server_list;
		pr_notice("FC: SL nr=%u pr=%u vnov=%hx\n",
			  sl->nr_servers, sl->preferred, sl->vnovol_mask);
		for (i = 0; i < sl->nr_servers; i++) {
			const struct afs_server *s = sl->servers[i].server;
			pr_notice("FC: server fl=%lx av=%u %pU\n",
				  s->flags, s->addr_version, &s->uuid);
			if (s->addresses) {
				const struct afs_addr_list *a =
					rcu_dereference(s->addresses);
				pr_notice("FC:  - av=%u nr=%u/%u/%u pr=%u\n",
					  a->version,
					  a->nr_ipv4, a->nr_addrs, a->max_addrs,
					  a->preferred);
				pr_notice("FC:  - pr=%lx R=%lx F=%lx\n",
					  a->probed, a->responded, a->failed);
				if (a == fc->ac.alist)
					pr_notice("FC:  - current\n");
			}
		}
	}

	pr_notice("AC: t=%lx ax=%u ac=%d er=%d r=%u ni=%u\n",
		  fc->ac.tried, fc->ac.index, fc->ac.abort_code, fc->ac.error,
		  fc->ac.responded, fc->ac.nr_iterations);
	rcu_read_unlock();
}

/*
 * Tidy up a filesystem cursor and unlock the vnode.
 */
int afs_end_vnode_operation(struct afs_fs_cursor *fc)
{
	struct afs_net *net = afs_v2net(fc->vnode);

	if (fc->error == -EDESTADDRREQ ||
	    fc->error == -ENETUNREACH ||
	    fc->error == -EHOSTUNREACH)
		afs_dump_edestaddrreq(fc);

	mutex_unlock(&fc->vnode->io_lock);

	afs_end_cursor(&fc->ac);
	afs_put_cb_interest(net, fc->cbi);
	afs_put_serverlist(net, fc->server_list);

	if (fc->error == -ECONNABORTED)
		fc->error = afs_abort_to_error(fc->ac.abort_code);

	return fc->error;
}
