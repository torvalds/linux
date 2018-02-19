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
 * Initialise a filesystem server cursor for iterating over FS servers.
 */
void afs_init_fs_cursor(struct afs_fs_cursor *fc, struct afs_vnode *vnode)
{
	memset(fc, 0, sizeof(*fc));
}

/*
 * Begin an operation on the fileserver.
 *
 * Fileserver operations are serialised on the server by vnode, so we serialise
 * them here also using the io_lock.
 */
bool afs_begin_vnode_operation(struct afs_fs_cursor *fc, struct afs_vnode *vnode,
			       struct key *key)
{
	afs_init_fs_cursor(fc, vnode);
	fc->vnode = vnode;
	fc->key = key;
	fc->ac.error = SHRT_MAX;

	if (mutex_lock_interruptible(&vnode->io_lock) < 0) {
		fc->ac.error = -EINTR;
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

	cbi = vnode->cb_interest;
	if (cbi) {
		/* See if the vnode's preferred record is still available */
		for (i = 0; i < fc->server_list->nr_servers; i++) {
			if (fc->server_list->servers[i].cb_interest == cbi) {
				fc->start = i;
				goto found_interest;
			}
		}

		/* If we have a lock outstanding on a server that's no longer
		 * serving this vnode, then we can't switch to another server
		 * and have to return an error.
		 */
		if (fc->flags & AFS_FS_CURSOR_CUR_ONLY) {
			fc->ac.error = -ESTALE;
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
	} else {
		fc->start = READ_ONCE(fc->server_list->index);
	}

found_interest:
	fc->index = fc->start;
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

	pr_notice("kAFS: Volume %u '%s' is %s\n", volume->vid, volume->name, m);
}

/*
 * Sleep and retry the operation to the same fileserver.
 */
static bool afs_sleep_and_retry(struct afs_fs_cursor *fc)
{
	msleep_interruptible(1000);
	if (signal_pending(current)) {
		fc->ac.error = -ERESTARTSYS;
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

	_enter("%u/%u,%u/%u,%d,%d",
	       fc->index, fc->start,
	       fc->ac.index, fc->ac.start,
	       fc->ac.error, fc->ac.abort_code);

	if (fc->flags & AFS_FS_CURSOR_STOP) {
		_leave(" = f [stopped]");
		return false;
	}

	/* Evaluate the result of the previous operation, if there was one. */
	switch (fc->ac.error) {
	case SHRT_MAX:
		goto start;

	case 0:
	default:
		/* Success or local failure.  Stop. */
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [okay/local %d]", fc->ac.error);
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
				fc->ac.error = -EREMOTEIO;
				goto failed;
			}

			write_lock(&vnode->volume->servers_lock);
			fc->server_list->vnovol_mask |= 1 << fc->index;
			write_unlock(&vnode->volume->servers_lock);

			set_bit(AFS_VOLUME_NEEDS_UPDATE, &vnode->volume->flags);
			fc->ac.error = afs_check_volume_status(vnode->volume, fc->key);
			if (fc->ac.error < 0)
				goto failed;

			if (test_bit(AFS_VOLUME_DELETED, &vnode->volume->flags)) {
				fc->ac.error = -ENOMEDIUM;
				goto failed;
			}

			/* If the server list didn't change, then assume that
			 * it's the fileserver having trouble.
			 */
			if (vnode->volume->servers == fc->server_list) {
				fc->ac.error = -EREMOTEIO;
				goto failed;
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
			fc->ac.error = afs_abort_to_error(fc->ac.abort_code);
			goto next_server;

		case VOFFLINE:
			if (!test_and_set_bit(AFS_VOLUME_OFFLINE, &vnode->volume->flags)) {
				afs_busy(vnode->volume, fc->ac.abort_code);
				clear_bit(AFS_VOLUME_BUSY, &vnode->volume->flags);
			}
			if (fc->flags & AFS_FS_CURSOR_NO_VSLEEP) {
				fc->ac.error = -EADV;
				goto failed;
			}
			if (fc->flags & AFS_FS_CURSOR_CUR_ONLY) {
				fc->ac.error = -ESTALE;
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
				fc->ac.error = -EBUSY;
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
				fc->ac.error = -EREMOTEIO;
				goto failed;
			}
			fc->flags |= AFS_FS_CURSOR_VMOVED;

			set_bit(AFS_VOLUME_WAIT, &vnode->volume->flags);
			set_bit(AFS_VOLUME_NEEDS_UPDATE, &vnode->volume->flags);
			fc->ac.error = afs_check_volume_status(vnode->volume, fc->key);
			if (fc->ac.error < 0)
				goto failed;

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
				fc->ac.error = -ENOMEDIUM;
				goto failed;
			}

			goto restart_from_beginning;

		default:
			clear_bit(AFS_VOLUME_OFFLINE, &vnode->volume->flags);
			clear_bit(AFS_VOLUME_BUSY, &vnode->volume->flags);
			fc->ac.error = afs_abort_to_error(fc->ac.abort_code);
			goto failed;
		}

	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -ETIME:
		_debug("no conn");
		goto iterate_address;
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
	fc->ac.error = afs_check_volume_status(vnode->volume, fc->key);
	if (fc->ac.error < 0)
		goto failed;

	if (!afs_start_fs_iteration(fc, vnode))
		goto failed;
	goto use_server;

next_server:
	_debug("next");
	afs_put_cb_interest(afs_v2net(vnode), fc->cbi);
	fc->cbi = NULL;
	fc->index++;
	if (fc->index >= fc->server_list->nr_servers)
		fc->index = 0;
	if (fc->index != fc->start)
		goto use_server;

	/* That's all the servers poked to no good effect.  Try again if some
	 * of them were busy.
	 */
	if (fc->flags & AFS_FS_CURSOR_VBUSY)
		goto restart_from_beginning;

	fc->ac.error = -EDESTADDRREQ;
	goto failed;

use_server:
	_debug("use");
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
	fc->ac.error = afs_register_server_cb_interest(
		vnode, &fc->server_list->servers[fc->index]);
	if (fc->ac.error < 0)
		goto failed;

	fc->cbi = afs_get_cb_interest(vnode->cb_interest);

	read_lock(&server->fs_lock);
	alist = rcu_dereference_protected(server->addresses,
					  lockdep_is_held(&server->fs_lock));
	afs_get_addrlist(alist);
	read_unlock(&server->fs_lock);


	/* Probe the current fileserver if we haven't done so yet. */
	if (!test_bit(AFS_SERVER_FL_PROBED, &server->flags)) {
		fc->ac.alist = afs_get_addrlist(alist);

		if (!afs_probe_fileserver(fc))
			goto failed;
	}

	if (!fc->ac.alist)
		fc->ac.alist = alist;
	else
		afs_put_addrlist(alist);

	fc->ac.addr  = NULL;
	fc->ac.start = READ_ONCE(alist->index);
	fc->ac.index = fc->ac.start;
	fc->ac.error = 0;
	fc->ac.begun = false;
	goto iterate_address;

iterate_address:
	ASSERT(fc->ac.alist);
	_debug("iterate %d/%d", fc->ac.index, fc->ac.alist->nr_addrs);
	/* Iterate over the current server's address list to try and find an
	 * address on which it will respond to us.
	 */
	if (afs_iterate_addresses(&fc->ac)) {
		_leave(" = t");
		return true;
	}

	afs_end_cursor(&fc->ac);
	goto next_server;

failed:
	fc->flags |= AFS_FS_CURSOR_STOP;
	_leave(" = f [failed %d]", fc->ac.error);
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

	_enter("");

	switch (fc->ac.error) {
	case SHRT_MAX:
		if (!cbi) {
			fc->ac.error = -ESTALE;
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
			fc->ac.error = -ESTALE;
			fc->flags |= AFS_FS_CURSOR_STOP;
			return false;
		}

		fc->ac.alist = alist;
		fc->ac.addr  = NULL;
		fc->ac.start = READ_ONCE(alist->index);
		fc->ac.index = fc->ac.start;
		fc->ac.error = 0;
		fc->ac.begun = false;
		goto iterate_address;

	case 0:
	default:
		/* Success or local failure.  Stop. */
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [okay/local %d]", fc->ac.error);
		return false;

	case -ECONNABORTED:
		fc->flags |= AFS_FS_CURSOR_STOP;
		_leave(" = f [abort]");
		return false;

	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -ETIME:
		_debug("no conn");
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
 * Tidy up a filesystem cursor and unlock the vnode.
 */
int afs_end_vnode_operation(struct afs_fs_cursor *fc)
{
	struct afs_net *net = afs_v2net(fc->vnode);
	int ret;

	mutex_unlock(&fc->vnode->io_lock);

	afs_end_cursor(&fc->ac);
	afs_put_cb_interest(net, fc->cbi);
	afs_put_serverlist(net, fc->server_list);

	ret = fc->ac.error;
	if (ret == -ECONNABORTED)
		afs_abort_to_error(fc->ac.abort_code);

	return fc->ac.error;
}

#if 0
/*
 * Set a filesystem server cursor for using a specific FS server.
 */
int afs_set_fs_cursor(struct afs_fs_cursor *fc, struct afs_vnode *vnode)
{
	afs_init_fs_cursor(fc, vnode);

	read_seqlock_excl(&vnode->cb_lock);
	if (vnode->cb_interest) {
		if (vnode->cb_interest->server->fs_state == 0)
			fc->server = afs_get_server(vnode->cb_interest->server);
		else
			fc->ac.error = vnode->cb_interest->server->fs_state;
	} else {
		fc->ac.error = -ESTALE;
	}
	read_sequnlock_excl(&vnode->cb_lock);

	return fc->ac.error;
}

/*
 * pick a server to use to try accessing this volume
 * - returns with an elevated usage count on the server chosen
 */
bool afs_volume_pick_fileserver(struct afs_fs_cursor *fc, struct afs_vnode *vnode)
{
	struct afs_volume *volume = vnode->volume;
	struct afs_server *server;
	int ret, state, loop;

	_enter("%s", volume->vlocation->vldb.name);

	/* stick with the server we're already using if we can */
	if (vnode->cb_interest && vnode->cb_interest->server->fs_state == 0) {
		fc->server = afs_get_server(vnode->cb_interest->server);
		goto set_server;
	}

	down_read(&volume->server_sem);

	/* handle the no-server case */
	if (volume->nservers == 0) {
		fc->ac.error = volume->rjservers ? -ENOMEDIUM : -ESTALE;
		up_read(&volume->server_sem);
		_leave(" = f [no servers %d]", fc->ac.error);
		return false;
	}

	/* basically, just search the list for the first live server and use
	 * that */
	ret = 0;
	for (loop = 0; loop < volume->nservers; loop++) {
		server = volume->servers[loop];
		state = server->fs_state;

		_debug("consider %d [%d]", loop, state);

		switch (state) {
		case 0:
			goto picked_server;

		case -ENETUNREACH:
			if (ret == 0)
				ret = state;
			break;

		case -EHOSTUNREACH:
			if (ret == 0 ||
			    ret == -ENETUNREACH)
				ret = state;
			break;

		case -ECONNREFUSED:
			if (ret == 0 ||
			    ret == -ENETUNREACH ||
			    ret == -EHOSTUNREACH)
				ret = state;
			break;

		default:
		case -EREMOTEIO:
			if (ret == 0 ||
			    ret == -ENETUNREACH ||
			    ret == -EHOSTUNREACH ||
			    ret == -ECONNREFUSED)
				ret = state;
			break;
		}
	}

error:
	fc->ac.error = ret;

	/* no available servers
	 * - TODO: handle the no active servers case better
	 */
	up_read(&volume->server_sem);
	_leave(" = f [%d]", fc->ac.error);
	return false;

picked_server:
	/* Found an apparently healthy server.  We need to register an interest
	 * in receiving callbacks before we talk to it.
	 */
	ret = afs_register_server_cb_interest(vnode,
					      &volume->cb_interests[loop], server);
	if (ret < 0)
		goto error;

	fc->server = afs_get_server(server);
	up_read(&volume->server_sem);
set_server:
	fc->ac.alist = afs_get_addrlist(fc->server->addrs);
	fc->ac.addr = &fc->ac.alist->addrs[0];
	_debug("USING SERVER: %pIS\n", &fc->ac.addr->transport);
	_leave(" = t (picked %pIS)", &fc->ac.addr->transport);
	return true;
}

/*
 * release a server after use
 * - releases the ref on the server struct that was acquired by picking
 * - records result of using a particular server to access a volume
 * - return true to try again, false if okay or to issue error
 * - the caller must release the server struct if result was false
 */
bool afs_iterate_fs_cursor(struct afs_fs_cursor *fc,
			   struct afs_vnode *vnode)
{
	struct afs_volume *volume = vnode->volume;
	struct afs_server *server = fc->server;
	unsigned loop;

	_enter("%s,%pIS,%d",
	       volume->vlocation->vldb.name, &fc->ac.addr->transport,
	       fc->ac.error);

	switch (fc->ac.error) {
		/* success */
	case 0:
		server->fs_state = 0;
		_leave(" = f");
		return false;

		/* the fileserver denied all knowledge of the volume */
	case -ENOMEDIUM:
		down_write(&volume->server_sem);

		/* firstly, find where the server is in the active list (if it
		 * is) */
		for (loop = 0; loop < volume->nservers; loop++)
			if (volume->servers[loop] == server)
				goto present;

		/* no longer there - may have been discarded by another op */
		goto try_next_server_upw;

	present:
		volume->nservers--;
		memmove(&volume->servers[loop],
			&volume->servers[loop + 1],
			sizeof(volume->servers[loop]) *
			(volume->nservers - loop));
		volume->servers[volume->nservers] = NULL;
		afs_put_server(afs_v2net(vnode), server);
		volume->rjservers++;

		if (volume->nservers > 0)
			/* another server might acknowledge its existence */
			goto try_next_server_upw;

		/* handle the case where all the fileservers have rejected the
		 * volume
		 * - TODO: try asking the fileservers for volume information
		 * - TODO: contact the VL server again to see if the volume is
		 *         no longer registered
		 */
		up_write(&volume->server_sem);
		afs_put_server(afs_v2net(vnode), server);
		fc->server = NULL;
		_leave(" = f [completely rejected]");
		return false;

		/* problem reaching the server */
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -ETIME:
	case -ETIMEDOUT:
	case -EREMOTEIO:
		/* mark the server as dead
		 * TODO: vary dead timeout depending on error
		 */
		spin_lock(&server->fs_lock);
		if (!server->fs_state) {
			server->fs_state = fc->ac.error;
			printk("kAFS: SERVER DEAD state=%d\n", fc->ac.error);
		}
		spin_unlock(&server->fs_lock);
		goto try_next_server;

		/* miscellaneous error */
	default:
	case -ENOMEM:
	case -ENONET:
		/* tell the caller to accept the result */
		afs_put_server(afs_v2net(vnode), server);
		fc->server = NULL;
		_leave(" = f [local failure]");
		return false;
	}

	/* tell the caller to loop around and try the next server */
try_next_server_upw:
	up_write(&volume->server_sem);
try_next_server:
	afs_put_server(afs_v2net(vnode), server);
	_leave(" = t [try next server]");
	return true;
}

/*
 * Clean up a fileserver cursor.
 */
int afs_end_fs_cursor(struct afs_fs_cursor *fc, struct afs_net *net)
{
	afs_end_cursor(&fc->ac);
	afs_put_server(net, fc->server);
	return fc->ac.error;
}

#endif
