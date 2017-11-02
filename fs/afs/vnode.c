/* AFS vnode management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include "internal.h"

/*
 * Handle remote file deletion.
 */
static void afs_vnode_deleted_remotely(struct afs_vnode *vnode)
{
	struct afs_cb_interest *cbi = vnode->cb_interest;

	_enter("{%p}", cbi);

	set_bit(AFS_VNODE_DELETED, &vnode->flags);

	if (cbi) {
		vnode->cb_interest = NULL;
		afs_put_cb_interest(afs_v2net(vnode), cbi);
	}

	_leave("");
}

/*
 * finish off updating the recorded status of a file after a successful
 * operation completion
 * - starts callback expiry timer
 * - adds to server's callback list
 */
void afs_vnode_finalise_status_update(struct afs_vnode *vnode,
				      struct afs_server *server)
{
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);

	wake_up_all(&vnode->update_waitq);
	_leave("");
}

/*
 * finish off updating the recorded status of a file after an operation failed
 */
static void afs_vnode_status_update_failed(struct afs_fs_cursor *fc,
					   struct afs_vnode *vnode)
{
	_enter("{%x:%u},%d", vnode->fid.vid, vnode->fid.vnode, fc->ac.error);

	spin_lock(&vnode->lock);

	if (fc->ac.error == -ENOENT) {
		/* the file was deleted on the server */
		_debug("got NOENT from server - marking file deleted");
		afs_vnode_deleted_remotely(vnode);
	}

	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);

	wake_up_all(&vnode->update_waitq);
	_leave("");
}

/*
 * fetch file status from the volume
 * - don't issue a fetch if:
 *   - the changed bit is not set and there's a valid callback
 *   - there are any outstanding ops that will fetch the status
 * - TODO implement local caching
 */
int afs_vnode_fetch_status(struct afs_vnode *vnode, struct key *key, bool force)
{
	struct afs_fs_cursor fc;
	unsigned int cb_break = 0;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%s,{%x:%u.%u,S=%lx},%u",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
	       vnode->flags,
	       force);

	if (!force && test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		_leave(" [unchanged]");
		return 0;
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_leave(" [deleted]");
		return -ENOENT;
	}

	cb_break = vnode->cb_break + vnode->cb_s_break;

	spin_lock(&vnode->lock);

	if (!force && test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		spin_unlock(&vnode->lock);
		_leave(" [unchanged]");
		return 0;
	}

	ASSERTCMP(vnode->update_cnt, >=, 0);

	if (vnode->update_cnt > 0) {
		/* someone else started a fetch */
		_debug("wait on fetch %d", vnode->update_cnt);

		set_current_state(TASK_UNINTERRUPTIBLE);
		ASSERT(myself.func != NULL);
		add_wait_queue(&vnode->update_waitq, &myself);

		/* wait for the status to be updated */
		for (;;) {
			if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags))
				break;
			if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
				break;

			/* check to see if it got updated and invalidated all
			 * before we saw it */
			if (vnode->update_cnt == 0) {
				remove_wait_queue(&vnode->update_waitq,
						  &myself);
				set_current_state(TASK_RUNNING);
				goto get_anyway;
			}

			spin_unlock(&vnode->lock);

			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);

			spin_lock(&vnode->lock);
		}

		remove_wait_queue(&vnode->update_waitq, &myself);
		spin_unlock(&vnode->lock);
		set_current_state(TASK_RUNNING);

		return test_bit(AFS_VNODE_DELETED, &vnode->flags) ?
			-ENOENT : 0;
	}

get_anyway:
	/* okay... we're going to have to initiate the op */
	vnode->update_cnt++;

	spin_unlock(&vnode->lock);

	/* merge AFS status fetches and clear outstanding callback on this
	 * vnode */
	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_fetch_file_status(&fc, key, vnode, NULL, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		_debug("adjust");
		afs_cache_permit(vnode, key, cb_break);
		afs_vnode_finalise_status_update(vnode, fc.server);
	} else {
		_debug("failed [%d]", fc.ac.error);
		afs_vnode_status_update_failed(&fc, vnode);
	}

out:
	afs_end_fs_cursor(&fc, afs_v2net(vnode));
	ASSERTCMP(vnode->update_cnt, >=, 0);
	_leave(" = %d [cnt %d]", fc.ac.error, vnode->update_cnt);
	return fc.ac.error;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * fetch file data from the volume
 * - TODO implement caching
 */
int afs_vnode_fetch_data(struct afs_vnode *vnode, struct key *key,
			 struct afs_read *desc)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,,,",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	/* this op will fetch the status */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	/* merge in AFS status fetches and clear outstanding callback on this
	 * vnode */
	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_fetch_data(&fc, key, vnode, desc, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0)
		afs_vnode_finalise_status_update(vnode, fc.server);
	else
		afs_vnode_status_update_failed(&fc, vnode);

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * make a file or a directory
 */
int afs_vnode_create(struct afs_vnode *vnode, struct key *key,
		     const char *name, umode_t mode, struct afs_fid *newfid,
		     struct afs_file_status *newstatus,
		     struct afs_callback *newcb, struct afs_server **_server)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,%s,,",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key),
	       name);

	/* this op will fetch the status on the directory we're creating in */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_create(&fc, key, vnode, name, mode, newfid,
					    newstatus, newcb, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(vnode, fc.server);
		*_server = fc.server;
		fc.server = NULL;
	} else {
		afs_vnode_status_update_failed(&fc, vnode);
		*_server = NULL;
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * remove a file or directory
 */
int afs_vnode_remove(struct afs_vnode *vnode, struct key *key, const char *name,
		     bool isdir)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,%s",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key),
	       name);

	/* this op will fetch the status on the directory we're removing from */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_remove(&fc, key, vnode, name, isdir, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0)
		afs_vnode_finalise_status_update(vnode, fc.server);
	else
		afs_vnode_status_update_failed(&fc, vnode);

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * create a hard link
 */
int afs_vnode_link(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			  struct key *key, const char *name)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%s{%x:%u.%u},%x,%s",
	       dvnode->volume->vlocation->vldb.name,
	       dvnode->fid.vid,
	       dvnode->fid.vnode,
	       dvnode->fid.unique,
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key),
	       name);

	/* this op will fetch the status on the directory we're removing from */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);
	spin_lock(&dvnode->lock);
	dvnode->update_cnt++;
	spin_unlock(&dvnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, dvnode))
			goto no_server;

		fc.ac.error = afs_fs_link(&fc, key, dvnode, vnode, name, false);

	} while (afs_iterate_fs_cursor(&fc, dvnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(vnode, fc.server);
		afs_vnode_finalise_status_update(dvnode, fc.server);
	} else {
		afs_vnode_status_update_failed(&fc, vnode);
		afs_vnode_status_update_failed(&fc, dvnode);
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	spin_lock(&dvnode->lock);
	dvnode->update_cnt--;
	ASSERTCMP(dvnode->update_cnt, >=, 0);
	spin_unlock(&dvnode->lock);
	goto out;
}

/*
 * create a symbolic link
 */
int afs_vnode_symlink(struct afs_vnode *vnode, struct key *key,
		      const char *name, const char *content,
		      struct afs_fid *newfid,
		      struct afs_file_status *newstatus,
		      struct afs_server **_server)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,%s,%s,,,",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key),
	       name, content);

	/* this op will fetch the status on the directory we're creating in */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_symlink(&fc, key, vnode, name, content,
					     newfid, newstatus, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(vnode, fc.server);
		*_server = fc.server;
		fc.server = NULL;
	} else {
		afs_vnode_status_update_failed(&fc, vnode);
		*_server = NULL;
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	*_server = NULL;
	goto out;
}

/*
 * rename a file
 */
int afs_vnode_rename(struct afs_vnode *orig_dvnode,
		     struct afs_vnode *new_dvnode,
		     struct key *key,
		     const char *orig_name,
		     const char *new_name)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%s{%u,%u,%u},%x,%s,%s",
	       orig_dvnode->volume->vlocation->vldb.name,
	       orig_dvnode->fid.vid,
	       orig_dvnode->fid.vnode,
	       orig_dvnode->fid.unique,
	       new_dvnode->volume->vlocation->vldb.name,
	       new_dvnode->fid.vid,
	       new_dvnode->fid.vnode,
	       new_dvnode->fid.unique,
	       key_serial(key),
	       orig_name,
	       new_name);

	/* this op will fetch the status on both the directories we're dealing
	 * with */
	spin_lock(&orig_dvnode->lock);
	orig_dvnode->update_cnt++;
	spin_unlock(&orig_dvnode->lock);
	if (new_dvnode != orig_dvnode) {
		spin_lock(&new_dvnode->lock);
		new_dvnode->update_cnt++;
		spin_unlock(&new_dvnode->lock);
	}

	afs_init_fs_cursor(&fc, orig_dvnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, orig_dvnode))
			goto no_server;

		fc.ac.error = afs_fs_rename(&fc, key, orig_dvnode, orig_name,
					    new_dvnode, new_name, false);

	} while (afs_iterate_fs_cursor(&fc, orig_dvnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(orig_dvnode, fc.server);
		if (new_dvnode != orig_dvnode)
			afs_vnode_finalise_status_update(new_dvnode, fc.server);
	} else {
		afs_vnode_status_update_failed(&fc, orig_dvnode);
		if (new_dvnode != orig_dvnode)
			afs_vnode_status_update_failed(&fc, new_dvnode);
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(orig_dvnode));

no_server:
	spin_lock(&orig_dvnode->lock);
	orig_dvnode->update_cnt--;
	ASSERTCMP(orig_dvnode->update_cnt, >=, 0);
	spin_unlock(&orig_dvnode->lock);
	if (new_dvnode != orig_dvnode) {
		spin_lock(&new_dvnode->lock);
		new_dvnode->update_cnt--;
		ASSERTCMP(new_dvnode->update_cnt, >=, 0);
		spin_unlock(&new_dvnode->lock);
	}
	goto out;
}

/*
 * write to a file
 */
int afs_vnode_store_data(struct afs_writeback *wb, pgoff_t first, pgoff_t last,
			 unsigned offset, unsigned to)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = wb->vnode;

	_enter("%s{%x:%u.%u},%x,%lx,%lx,%x,%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(wb->key),
	       first, last, offset, to);

	/* this op will fetch the status */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_store_data(&fc, wb, first, last, offset, to,
						false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(vnode, fc.server);
	} else {
		afs_vnode_status_update_failed(&fc, vnode);
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * set the attributes on a file
 */
int afs_vnode_setattr(struct afs_vnode *vnode, struct key *key,
		      struct iattr *attr)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	/* this op will fetch the status */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			goto no_server;

		fc.ac.error = afs_fs_setattr(&fc, key, vnode, attr, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	/* adjust the flags */
	if (fc.ac.error == 0) {
		afs_vnode_finalise_status_update(vnode, fc.server);
	} else {
		afs_vnode_status_update_failed(&fc, vnode);
	}

out:
	return afs_end_fs_cursor(&fc, afs_v2net(vnode));

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	goto out;
}

/*
 * get the status of a volume
 */
int afs_vnode_get_volume_status(struct afs_vnode *vnode, struct key *key,
				struct afs_volume_status *vs)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			break;

		fc.ac.error = afs_fs_get_volume_status(&fc, key, vnode, vs, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	return afs_end_fs_cursor(&fc, afs_v2net(vnode));
}

/*
 * get a lock on a file
 */
int afs_vnode_set_lock(struct afs_vnode *vnode, struct key *key,
		       afs_lock_type_t type)
{
	struct afs_fs_cursor fc;

	_enter("%s{%x:%u.%u},%x,%u",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key), type);

	afs_init_fs_cursor(&fc, vnode);
	do {
		/* pick a server to query */
		if (!afs_volume_pick_fileserver(&fc, vnode))
			break;

		fc.ac.error = afs_fs_set_lock(&fc, key, vnode, type, false);

	} while (afs_iterate_fs_cursor(&fc, vnode));

	return afs_end_fs_cursor(&fc, afs_v2net(vnode));
}

/*
 * extend a lock on a file
 */
int afs_vnode_extend_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	ret = afs_set_fs_cursor(&fc, vnode);
	if (ret < 0)
		return ret;

	fc.ac.error = afs_fs_extend_lock(&fc, key, vnode, false);

	return afs_end_fs_cursor(&fc, afs_v2net(vnode));
}

/*
 * release a lock on a file
 */
int afs_vnode_release_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	ret = afs_set_fs_cursor(&fc, vnode);
	if (ret < 0)
		return ret;

	fc.ac.error = afs_fs_release_lock(&fc, key, vnode, false);

	return afs_end_fs_cursor(&fc, afs_v2net(vnode));
}
