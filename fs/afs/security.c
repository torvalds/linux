/* AFS security handling
 *
 * Copyright (C) 2007, 2017 Red Hat, Inc. All Rights Reserved.
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
#include <linux/hashtable.h>
#include <keys/rxrpc-type.h>
#include "internal.h"

static DEFINE_HASHTABLE(afs_permits_cache, 10);
static DEFINE_SPINLOCK(afs_permits_lock);

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
 * Dispose of a list of permits.
 */
static void afs_permits_rcu(struct rcu_head *rcu)
{
	struct afs_permits *permits =
		container_of(rcu, struct afs_permits, rcu);
	int i;

	for (i = 0; i < permits->nr_permits; i++)
		key_put(permits->permits[i].key);
	kfree(permits);
}

/*
 * Discard a permission cache.
 */
void afs_put_permits(struct afs_permits *permits)
{
	if (permits && refcount_dec_and_test(&permits->usage)) {
		spin_lock(&afs_permits_lock);
		hash_del_rcu(&permits->hash_node);
		spin_unlock(&afs_permits_lock);
		call_rcu(&permits->rcu, afs_permits_rcu);
	}
}

/*
 * Clear a permit cache on callback break.
 */
void afs_clear_permits(struct afs_vnode *vnode)
{
	struct afs_permits *permits;

	spin_lock(&vnode->lock);
	permits = rcu_dereference_protected(vnode->permit_cache,
					    lockdep_is_held(&vnode->lock));
	RCU_INIT_POINTER(vnode->permit_cache, NULL);
	vnode->cb_break++;
	spin_unlock(&vnode->lock);

	if (permits)
		afs_put_permits(permits);
}

/*
 * Hash a list of permits.  Use simple addition to make it easy to add an extra
 * one at an as-yet indeterminate position in the list.
 */
static void afs_hash_permits(struct afs_permits *permits)
{
	unsigned long h = permits->nr_permits;
	int i;

	for (i = 0; i < permits->nr_permits; i++) {
		h += (unsigned long)permits->permits[i].key / sizeof(void *);
		h += permits->permits[i].access;
	}

	permits->h = h;
}

/*
 * Cache the CallerAccess result obtained from doing a fileserver operation
 * that returned a vnode status for a particular key.  If a callback break
 * occurs whilst the operation was in progress then we have to ditch the cache
 * as the ACL *may* have changed.
 */
void afs_cache_permit(struct afs_vnode *vnode, struct key *key,
		      unsigned int cb_break)
{
	struct afs_permits *permits, *xpermits, *replacement, *new = NULL;
	afs_access_t caller_access = READ_ONCE(vnode->status.caller_access);
	size_t size = 0;
	bool changed = false;
	int i, j;

	_enter("{%x:%u},%x,%x",
	       vnode->fid.vid, vnode->fid.vnode, key_serial(key), caller_access);

	rcu_read_lock();

	/* Check for the common case first: We got back the same access as last
	 * time we tried and already have it recorded.
	 */
	permits = rcu_dereference(vnode->permit_cache);
	if (permits) {
		if (!permits->invalidated) {
			for (i = 0; i < permits->nr_permits; i++) {
				if (permits->permits[i].key < key)
					continue;
				if (permits->permits[i].key > key)
					break;
				if (permits->permits[i].access != caller_access) {
					changed = true;
					break;
				}

				if (cb_break != (vnode->cb_break +
						 vnode->cb_interest->server->cb_s_break)) {
					changed = true;
					break;
				}

				/* The cache is still good. */
				rcu_read_unlock();
				return;
			}
		}

		changed |= permits->invalidated;
		size = permits->nr_permits;

		/* If this set of permits is now wrong, clear the permits
		 * pointer so that no one tries to use the stale information.
		 */
		if (changed) {
			spin_lock(&vnode->lock);
			if (permits != rcu_access_pointer(vnode->permit_cache))
				goto someone_else_changed_it_unlock;
			RCU_INIT_POINTER(vnode->permit_cache, NULL);
			spin_unlock(&vnode->lock);

			afs_put_permits(permits);
			permits = NULL;
			size = 0;
		}
	}

	if (cb_break != (vnode->cb_break + vnode->cb_interest->server->cb_s_break)) {
		rcu_read_unlock();
		goto someone_else_changed_it;
	}

	/* We need a ref on any permits list we want to copy as we'll have to
	 * drop the lock to do memory allocation.
	 */
	if (permits && !refcount_inc_not_zero(&permits->usage)) {
		rcu_read_unlock();
		goto someone_else_changed_it;
	}

	rcu_read_unlock();

	/* Speculatively create a new list with the revised permission set.  We
	 * discard this if we find an extant match already in the hash, but
	 * it's easier to compare with memcmp this way.
	 *
	 * We fill in the key pointers at this time, but we don't get the refs
	 * yet.
	 */
	size++;
	new = kzalloc(sizeof(struct afs_permits) +
		      sizeof(struct afs_permit) * size, GFP_NOFS);
	if (!new)
		return;

	refcount_set(&new->usage, 1);
	new->nr_permits = size;
	i = j = 0;
	if (permits) {
		for (i = 0; i < permits->nr_permits; i++) {
			if (j == i && permits->permits[i].key > key) {
				new->permits[j].key = key;
				new->permits[j].access = caller_access;
				j++;
			}
			new->permits[j].key = permits->permits[i].key;
			new->permits[j].access = permits->permits[i].access;
			j++;
		}
	}

	if (j == i) {
		new->permits[j].key = key;
		new->permits[j].access = caller_access;
	}

	afs_hash_permits(new);

	afs_put_permits(permits);

	/* Now see if the permit list we want is actually already available */
	spin_lock(&afs_permits_lock);

	hash_for_each_possible(afs_permits_cache, xpermits, hash_node, new->h) {
		if (xpermits->h != new->h ||
		    xpermits->invalidated ||
		    xpermits->nr_permits != new->nr_permits ||
		    memcmp(xpermits->permits, new->permits,
			   new->nr_permits * sizeof(struct afs_permit)) != 0)
			continue;

		if (refcount_inc_not_zero(&xpermits->usage)) {
			replacement = xpermits;
			goto found;
		}

		break;
	}

	for (i = 0; i < new->nr_permits; i++)
		key_get(new->permits[i].key);
	hash_add_rcu(afs_permits_cache, &new->hash_node, new->h);
	replacement = new;
	new = NULL;

found:
	spin_unlock(&afs_permits_lock);

	kfree(new);

	spin_lock(&vnode->lock);
	if (cb_break != (vnode->cb_break + vnode->cb_interest->server->cb_s_break) ||
	    permits != rcu_access_pointer(vnode->permit_cache))
		goto someone_else_changed_it_unlock;
	rcu_assign_pointer(vnode->permit_cache, replacement);
	spin_unlock(&vnode->lock);
	afs_put_permits(permits);
	return;

someone_else_changed_it_unlock:
	spin_unlock(&vnode->lock);
someone_else_changed_it:
	/* Someone else changed the cache under us - don't recheck at this
	 * time.
	 */
	return;
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
	bool valid = false;
	int i, ret;

	_enter("{%x:%u},%x",
	       vnode->fid.vid, vnode->fid.vnode, key_serial(key));

	permits = vnode->permit_cache;

	/* check the permits to see if we've got one yet */
	if (key == vnode->volume->cell->anonymous_key) {
		_debug("anon");
		*_access = vnode->status.anon_access;
		valid = true;
	} else {
		rcu_read_lock();
		permits = rcu_dereference(vnode->permit_cache);
		if (permits) {
			for (i = 0; i < permits->nr_permits; i++) {
				if (permits->permits[i].key < key)
					continue;
				if (permits->permits[i].key > key)
					break;

				*_access = permits->permits[i].access;
				valid = !permits->invalidated;
				break;
			}
		}
		rcu_read_unlock();
	}

	if (!valid) {
		/* Check the status on the file we're actually interested in
		 * (the post-processing will cache the result).
		 */
		_debug("no valid permit");

		ret = afs_fetch_status(vnode, key);
		if (ret < 0) {
			*_access = 0;
			_leave(" = %d", ret);
			return ret;
		}
		*_access = vnode->status.caller_access;
	}

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

	ret = afs_validate(vnode, key);
	if (ret < 0)
		goto error;

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
			if (!(access & AFS_ACE_LOOKUP))
				goto permission_denied;
		} else if (mask & MAY_WRITE) {
			if (!(access & (AFS_ACE_DELETE | /* rmdir, unlink, rename from */
					AFS_ACE_INSERT))) /* create, mkdir, symlink, rename to */
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

void __exit afs_clean_up_permit_cache(void)
{
	int i;

	for (i = 0; i < HASH_SIZE(afs_permits_cache); i++)
		WARN_ON_ONCE(!hlist_empty(&afs_permits_cache[i]));

}
