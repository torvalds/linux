/* AFS security handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

/*
 * get a key
 */
struct key *afs_request_key(struct afs_cell *cell)
{
	struct key *key;

	_enter("{%x}", key_serial(cell->anonymous_key));

	_debug("key %s", cell->anonymous_key->description);
	key = request_key(&key_type_rxrpc, cell->anonymous_key->description,
			  NULL);
	if (IS_ERR(key)) {
		if (PTR_ERR(key) != -ENOKEY) {
			_leave(" = %ld", PTR_ERR(key));
			return key;
		}

		/* act as anonymous user */
		_leave(" = {%x} [anon]", key_serial(cell->anonymous_key));
		return key_get(cell->anonymous_key);
	} else {
		/* act as authorised user */
		_leave(" = {%x} [auth]", key_serial(key));
		return key;
	}
}

/*
 * dispose of a permits list
 */
void afs_zap_permits(struct rcu_head *rcu)
{
	struct afs_permits *permits =
		container_of(rcu, struct afs_permits, rcu);
	int loop;

	_enter("{%d}", permits->count);

	for (loop = permits->count - 1; loop >= 0; loop--)
		key_put(permits->permits[loop].key);
	kfree(permits);
}

/*
 * dispose of a permits list in which all the key pointers have been copied
 */
static void afs_dispose_of_permits(struct rcu_head *rcu)
{
	struct afs_permits *permits =
		container_of(rcu, struct afs_permits, rcu);

	_enter("{%d}", permits->count);

	kfree(permits);
}

/*
 * get the authorising vnode - this is the specified inode itself if it's a
 * directory or it's the parent directory if the specified inode is a file or
 * symlink
 * - the caller must release the ref on the inode
 */
static struct afs_vnode *afs_get_auth_inode(struct afs_vnode *vnode,
					    struct key *key)
{
	struct afs_vnode *auth_vnode;
	struct inode *auth_inode;

	_enter("");

	if (S_ISDIR(vnode->vfs_inode.i_mode)) {
		auth_inode = igrab(&vnode->vfs_inode);
		ASSERT(auth_inode != NULL);
	} else {
		auth_inode = afs_iget(vnode->vfs_inode.i_sb, key,
				      &vnode->status.parent, NULL, NULL);
		if (IS_ERR(auth_inode))
			return ERR_CAST(auth_inode);
	}

	auth_vnode = AFS_FS_I(auth_inode);
	_leave(" = {%x}", auth_vnode->fid.vnode);
	return auth_vnode;
}

/*
 * clear the permit cache on a directory vnode
 */
void afs_clear_permits(struct afs_vnode *vnode)
{
	struct afs_permits *permits;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	mutex_lock(&vnode->permits_lock);
	permits = vnode->permits;
	RCU_INIT_POINTER(vnode->permits, NULL);
	mutex_unlock(&vnode->permits_lock);

	if (permits)
		call_rcu(&permits->rcu, afs_zap_permits);
	_leave("");
}

/*
 * add the result obtained for a vnode to its or its parent directory's cache
 * for the key used to access it
 */
void afs_cache_permit(struct afs_vnode *vnode, struct key *key, long acl_order)
{
	struct afs_permits *permits, *xpermits;
	struct afs_permit *permit;
	struct afs_vnode *auth_vnode;
	int count, loop;

	_enter("{%x:%u},%x,%lx",
	       vnode->fid.vid, vnode->fid.vnode, key_serial(key), acl_order);

	auth_vnode = afs_get_auth_inode(vnode, key);
	if (IS_ERR(auth_vnode)) {
		_leave(" [get error %ld]", PTR_ERR(auth_vnode));
		return;
	}

	mutex_lock(&auth_vnode->permits_lock);

	/* guard against a rename being detected whilst we waited for the
	 * lock */
	if (memcmp(&auth_vnode->fid, &vnode->status.parent,
		   sizeof(struct afs_fid)) != 0) {
		_debug("renamed");
		goto out_unlock;
	}

	/* have to be careful as the directory's callback may be broken between
	 * us receiving the status we're trying to cache and us getting the
	 * lock to update the cache for the status */
	if (auth_vnode->acl_order - acl_order > 0) {
		_debug("ACL changed?");
		goto out_unlock;
	}

	/* always update the anonymous mask */
	_debug("anon access %x", vnode->status.anon_access);
	auth_vnode->status.anon_access = vnode->status.anon_access;
	if (key == vnode->volume->cell->anonymous_key)
		goto out_unlock;

	xpermits = auth_vnode->permits;
	count = 0;
	if (xpermits) {
		/* see if the permit is already in the list
		 * - if it is then we just amend the list
		 */
		count = xpermits->count;
		permit = xpermits->permits;
		for (loop = count; loop > 0; loop--) {
			if (permit->key == key) {
				permit->access_mask =
					vnode->status.caller_access;
				goto out_unlock;
			}
			permit++;
		}
	}

	permits = kmalloc(sizeof(*permits) + sizeof(*permit) * (count + 1),
			  GFP_NOFS);
	if (!permits)
		goto out_unlock;

	if (xpermits)
		memcpy(permits->permits, xpermits->permits,
			count * sizeof(struct afs_permit));

	_debug("key %x access %x",
	       key_serial(key), vnode->status.caller_access);
	permits->permits[count].access_mask = vnode->status.caller_access;
	permits->permits[count].key = key_get(key);
	permits->count = count + 1;

	rcu_assign_pointer(auth_vnode->permits, permits);
	if (xpermits)
		call_rcu(&xpermits->rcu, afs_dispose_of_permits);

out_unlock:
	mutex_unlock(&auth_vnode->permits_lock);
	iput(&auth_vnode->vfs_inode);
	_leave("");
}

/*
 * check with the fileserver to see if the directory or parent directory is
 * permitted to be accessed with this authorisation, and if so, what access it
 * is granted
 */
static int afs_check_permit(struct afs_vnode *vnode, struct key *key,
			    afs_access_t *_access)
{
	struct afs_permits *permits;
	struct afs_permit *permit;
	struct afs_vnode *auth_vnode;
	bool valid;
	int loop, ret;

	_enter("{%x:%u},%x",
	       vnode->fid.vid, vnode->fid.vnode, key_serial(key));

	auth_vnode = afs_get_auth_inode(vnode, key);
	if (IS_ERR(auth_vnode)) {
		*_access = 0;
		_leave(" = %ld", PTR_ERR(auth_vnode));
		return PTR_ERR(auth_vnode);
	}

	ASSERT(S_ISDIR(auth_vnode->vfs_inode.i_mode));

	/* check the permits to see if we've got one yet */
	if (key == auth_vnode->volume->cell->anonymous_key) {
		_debug("anon");
		*_access = auth_vnode->status.anon_access;
		valid = true;
	} else {
		valid = false;
		rcu_read_lock();
		permits = rcu_dereference(auth_vnode->permits);
		if (permits) {
			permit = permits->permits;
			for (loop = permits->count; loop > 0; loop--) {
				if (permit->key == key) {
					_debug("found in cache");
					*_access = permit->access_mask;
					valid = true;
					break;
				}
				permit++;
			}
		}
		rcu_read_unlock();
	}

	if (!valid) {
		/* check the status on the file we're actually interested in
		 * (the post-processing will cache the result on auth_vnode) */
		_debug("no valid permit");

		set_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);
		ret = afs_vnode_fetch_status(vnode, auth_vnode, key);
		if (ret < 0) {
			iput(&auth_vnode->vfs_inode);
			*_access = 0;
			_leave(" = %d", ret);
			return ret;
		}
		*_access = vnode->status.caller_access;
	}

	iput(&auth_vnode->vfs_inode);
	_leave(" = 0 [access %x]", *_access);
	return 0;
}

/*
 * check the permissions on an AFS file
 * - AFS ACLs are attached to directories only, and a file is controlled by its
 *   parent directory's ACL
 */
int afs_permission(struct inode *inode, int mask)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	afs_access_t uninitialized_var(access);
	struct key *key;
	int ret;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	_enter("{{%x:%u},%lx},%x,",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags, mask);

	key = afs_request_key(vnode->volume->cell);
	if (IS_ERR(key)) {
		_leave(" = %ld [key]", PTR_ERR(key));
		return PTR_ERR(key);
	}

	/* if the promise has expired, we need to check the server again */
	if (!vnode->cb_promised) {
		_debug("not promised");
		ret = afs_vnode_fetch_status(vnode, NULL, key);
		if (ret < 0)
			goto error;
		_debug("new promise [fl=%lx]", vnode->flags);
	}

	/* check the permits to see if we've got one yet */
	ret = afs_check_permit(vnode, key, &access);
	if (ret < 0)
		goto error;

	/* interpret the access mask */
	_debug("REQ %x ACC %x on %s",
	       mask, access, S_ISDIR(inode->i_mode) ? "dir" : "file");

	if (S_ISDIR(inode->i_mode)) {
		if (mask & MAY_EXEC) {
			if (!(access & AFS_ACE_LOOKUP))
				goto permission_denied;
		} else if (mask & MAY_READ) {
			if (!(access & AFS_ACE_READ))
				goto permission_denied;
		} else if (mask & MAY_WRITE) {
			if (!(access & (AFS_ACE_DELETE | /* rmdir, unlink, rename from */
					AFS_ACE_INSERT | /* create, mkdir, symlink, rename to */
					AFS_ACE_WRITE))) /* chmod */
				goto permission_denied;
		} else {
			BUG();
		}
	} else {
		if (!(access & AFS_ACE_LOOKUP))
			goto permission_denied;
		if ((mask & MAY_EXEC) && !(inode->i_mode & S_IXUSR))
			goto permission_denied;
		if (mask & (MAY_EXEC | MAY_READ)) {
			if (!(access & AFS_ACE_READ))
				goto permission_denied;
			if (!(inode->i_mode & S_IRUSR))
				goto permission_denied;
		} else if (mask & MAY_WRITE) {
			if (!(access & AFS_ACE_WRITE))
				goto permission_denied;
			if (!(inode->i_mode & S_IWUSR))
				goto permission_denied;
		}
	}

	key_put(key);
	_leave(" = %d", ret);
	return ret;

permission_denied:
	ret = -EACCES;
error:
	key_put(key);
	_leave(" = %d", ret);
	return ret;
}
