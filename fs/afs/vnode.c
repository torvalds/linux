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

#if 0
static noinline bool dump_tree_aux(struct rb_node *node, struct rb_node *parent,
				   int depth, char lr)
{
	struct afs_vnode *vnode;
	bool bad = false;

	if (!node)
		return false;

	if (node->rb_left)
		bad = dump_tree_aux(node->rb_left, node, depth + 2, '/');

	vnode = rb_entry(node, struct afs_vnode, cb_promise);
	_debug("%c %*.*s%c%p {%d}",
	       rb_is_red(node) ? 'R' : 'B',
	       depth, depth, "", lr,
	       vnode, vnode->cb_expires_at);
	if (rb_parent(node) != parent) {
		printk("BAD: %p != %p\n", rb_parent(node), parent);
		bad = true;
	}

	if (node->rb_right)
		bad |= dump_tree_aux(node->rb_right, node, depth + 2, '\\');

	return bad;
}

static noinline void dump_tree(const char *name, struct afs_server *server)
{
	_enter("%s", name);
	if (dump_tree_aux(server->cb_promises.rb_node, NULL, 0, '-'))
		BUG();
}
#endif

/*
 * insert a vnode into the backing server's vnode tree
 */
static void afs_install_vnode(struct afs_vnode *vnode,
			      struct afs_server *server)
{
	struct afs_server *old_server = vnode->server;
	struct afs_vnode *xvnode;
	struct rb_node *parent, **p;

	_enter("%p,%p", vnode, server);

	if (old_server) {
		spin_lock(&old_server->fs_lock);
		rb_erase(&vnode->server_rb, &old_server->fs_vnodes);
		spin_unlock(&old_server->fs_lock);
	}

	afs_get_server(server);
	vnode->server = server;
	afs_put_server(old_server);

	/* insert into the server's vnode tree in FID order */
	spin_lock(&server->fs_lock);

	parent = NULL;
	p = &server->fs_vnodes.rb_node;
	while (*p) {
		parent = *p;
		xvnode = rb_entry(parent, struct afs_vnode, server_rb);
		if (vnode->fid.vid < xvnode->fid.vid)
			p = &(*p)->rb_left;
		else if (vnode->fid.vid > xvnode->fid.vid)
			p = &(*p)->rb_right;
		else if (vnode->fid.vnode < xvnode->fid.vnode)
			p = &(*p)->rb_left;
		else if (vnode->fid.vnode > xvnode->fid.vnode)
			p = &(*p)->rb_right;
		else if (vnode->fid.unique < xvnode->fid.unique)
			p = &(*p)->rb_left;
		else if (vnode->fid.unique > xvnode->fid.unique)
			p = &(*p)->rb_right;
		else
			BUG(); /* can't happen unless afs_iget() malfunctions */
	}

	rb_link_node(&vnode->server_rb, parent, p);
	rb_insert_color(&vnode->server_rb, &server->fs_vnodes);

	spin_unlock(&server->fs_lock);
	_leave("");
}

/*
 * insert a vnode into the promising server's update/expiration tree
 * - caller must hold vnode->lock
 */
static void afs_vnode_note_promise(struct afs_vnode *vnode,
				   struct afs_server *server)
{
	struct afs_server *old_server;
	struct afs_vnode *xvnode;
	struct rb_node *parent, **p;

	_enter("%p,%p", vnode, server);

	ASSERT(server != NULL);

	old_server = vnode->server;
	if (vnode->cb_promised) {
		if (server == old_server &&
		    vnode->cb_expires == vnode->cb_expires_at) {
			_leave(" [no change]");
			return;
		}

		spin_lock(&old_server->cb_lock);
		if (vnode->cb_promised) {
			_debug("delete");
			rb_erase(&vnode->cb_promise, &old_server->cb_promises);
			vnode->cb_promised = false;
		}
		spin_unlock(&old_server->cb_lock);
	}

	if (vnode->server != server)
		afs_install_vnode(vnode, server);

	vnode->cb_expires_at = vnode->cb_expires;
	_debug("PROMISE on %p {%lu}",
	       vnode, (unsigned long) vnode->cb_expires_at);

	/* abuse an RB-tree to hold the expiration order (we may have multiple
	 * items with the same expiration time) */
	spin_lock(&server->cb_lock);

	parent = NULL;
	p = &server->cb_promises.rb_node;
	while (*p) {
		parent = *p;
		xvnode = rb_entry(parent, struct afs_vnode, cb_promise);
		if (vnode->cb_expires_at < xvnode->cb_expires_at)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&vnode->cb_promise, parent, p);
	rb_insert_color(&vnode->cb_promise, &server->cb_promises);
	vnode->cb_promised = true;

	spin_unlock(&server->cb_lock);
	_leave("");
}

/*
 * handle remote file deletion by discarding the callback promise
 */
static void afs_vnode_deleted_remotely(struct afs_vnode *vnode)
{
	struct afs_server *server;

	_enter("{%p}", vnode->server);

	set_bit(AFS_VNODE_DELETED, &vnode->flags);

	server = vnode->server;
	if (server) {
		if (vnode->cb_promised) {
			spin_lock(&server->cb_lock);
			if (vnode->cb_promised) {
				rb_erase(&vnode->cb_promise,
					 &server->cb_promises);
				vnode->cb_promised = false;
			}
			spin_unlock(&server->cb_lock);
		}

		spin_lock(&server->fs_lock);
		rb_erase(&vnode->server_rb, &server->fs_vnodes);
		spin_unlock(&server->fs_lock);

		vnode->server = NULL;
		afs_put_server(server);
	} else {
		ASSERT(!vnode->cb_promised);
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
	struct afs_server *oldserver = NULL;

	_enter("%p,%p", vnode, server);

	spin_lock(&vnode->lock);
	clear_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);
	afs_vnode_note_promise(vnode, server);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);

	wake_up_all(&vnode->update_waitq);
	afs_put_server(oldserver);
	_leave("");
}

/*
 * finish off updating the recorded status of a file after an operation failed
 */
static void afs_vnode_status_update_failed(struct afs_vnode *vnode, int ret)
{
	_enter("{%x:%u},%d", vnode->fid.vid, vnode->fid.vnode, ret);

	spin_lock(&vnode->lock);

	clear_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);

	if (ret == -ENOENT) {
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
int afs_vnode_fetch_status(struct afs_vnode *vnode,
			   struct afs_vnode *auth_vnode, struct key *key)
{
	struct afs_server *server;
	unsigned long acl_order;
	int ret;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%s,{%x:%u.%u}",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	if (!test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags) &&
	    vnode->cb_promised) {
		_leave(" [unchanged]");
		return 0;
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_leave(" [deleted]");
		return -ENOENT;
	}

	acl_order = 0;
	if (auth_vnode)
		acl_order = auth_vnode->acl_order;

	spin_lock(&vnode->lock);

	if (!test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags) &&
	    vnode->cb_promised) {
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
			if (!test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags))
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
	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %p{%08x}",
		       server, ntohl(server->addr.s_addr));

		ret = afs_fs_fetch_file_status(server, key, vnode, NULL,
					       false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		_debug("adjust");
		if (auth_vnode)
			afs_cache_permit(vnode, key, acl_order);
		afs_vnode_finalise_status_update(vnode, server);
		afs_put_server(server);
	} else {
		_debug("failed [%d]", ret);
		afs_vnode_status_update_failed(vnode, ret);
	}

	ASSERTCMP(vnode->update_cnt, >=, 0);

	_leave(" = %d [cnt %d]", ret, vnode->update_cnt);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	_leave(" = %ld [cnt %d]", PTR_ERR(server), vnode->update_cnt);
	return PTR_ERR(server);
}

/*
 * fetch file data from the volume
 * - TODO implement caching
 */
int afs_vnode_fetch_data(struct afs_vnode *vnode, struct key *key,
			 struct afs_read *desc)
{
	struct afs_server *server;
	int ret;

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
	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_fetch_data(server, key, vnode, desc,
					false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(vnode, ret);
	}

	_leave(" = %d", ret);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	return PTR_ERR(server);
}

/*
 * make a file or a directory
 */
int afs_vnode_create(struct afs_vnode *vnode, struct key *key,
		     const char *name, umode_t mode, struct afs_fid *newfid,
		     struct afs_file_status *newstatus,
		     struct afs_callback *newcb, struct afs_server **_server)
{
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_create(server, key, vnode, name, mode, newfid,
				    newstatus, newcb, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		*_server = server;
	} else {
		afs_vnode_status_update_failed(vnode, ret);
		*_server = NULL;
	}

	_leave(" = %d [cnt %d]", ret, vnode->update_cnt);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	_leave(" = %ld [cnt %d]", PTR_ERR(server), vnode->update_cnt);
	return PTR_ERR(server);
}

/*
 * remove a file or directory
 */
int afs_vnode_remove(struct afs_vnode *vnode, struct key *key, const char *name,
		     bool isdir)
{
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_remove(server, key, vnode, name, isdir,
				    false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(vnode, ret);
	}

	_leave(" = %d [cnt %d]", ret, vnode->update_cnt);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	_leave(" = %ld [cnt %d]", PTR_ERR(server), vnode->update_cnt);
	return PTR_ERR(server);
}

/*
 * create a hard link
 */
int afs_vnode_link(struct afs_vnode *dvnode, struct afs_vnode *vnode,
			  struct key *key, const char *name)
{
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(dvnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_link(server, key, dvnode, vnode, name,
				  false);

	} while (!afs_volume_release_fileserver(dvnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		afs_vnode_finalise_status_update(dvnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(vnode, ret);
		afs_vnode_status_update_failed(dvnode, ret);
	}

	_leave(" = %d [cnt %d]", ret, vnode->update_cnt);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	spin_lock(&dvnode->lock);
	dvnode->update_cnt--;
	ASSERTCMP(dvnode->update_cnt, >=, 0);
	spin_unlock(&dvnode->lock);
	_leave(" = %ld [cnt %d]", PTR_ERR(server), vnode->update_cnt);
	return PTR_ERR(server);
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
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_symlink(server, key, vnode, name, content,
				     newfid, newstatus, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		*_server = server;
	} else {
		afs_vnode_status_update_failed(vnode, ret);
		*_server = NULL;
	}

	_leave(" = %d [cnt %d]", ret, vnode->update_cnt);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	_leave(" = %ld [cnt %d]", PTR_ERR(server), vnode->update_cnt);
	return PTR_ERR(server);
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
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(orig_dvnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_rename(server, key, orig_dvnode, orig_name,
				    new_dvnode, new_name, false);

	} while (!afs_volume_release_fileserver(orig_dvnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(orig_dvnode, server);
		if (new_dvnode != orig_dvnode)
			afs_vnode_finalise_status_update(new_dvnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(orig_dvnode, ret);
		if (new_dvnode != orig_dvnode)
			afs_vnode_status_update_failed(new_dvnode, ret);
	}

	_leave(" = %d [cnt %d]", ret, orig_dvnode->update_cnt);
	return ret;

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
	_leave(" = %ld [cnt %d]", PTR_ERR(server), orig_dvnode->update_cnt);
	return PTR_ERR(server);
}

/*
 * write to a file
 */
int afs_vnode_store_data(struct afs_writeback *wb, pgoff_t first, pgoff_t last,
			 unsigned offset, unsigned to)
{
	struct afs_server *server;
	struct afs_vnode *vnode = wb->vnode;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_store_data(server, wb, first, last, offset, to,
					false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(vnode, ret);
	}

	_leave(" = %d", ret);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	return PTR_ERR(server);
}

/*
 * set the attributes on a file
 */
int afs_vnode_setattr(struct afs_vnode *vnode, struct key *key,
		      struct iattr *attr)
{
	struct afs_server *server;
	int ret;

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

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_setattr(server, key, vnode, attr, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0) {
		afs_vnode_finalise_status_update(vnode, server);
		afs_put_server(server);
	} else {
		afs_vnode_status_update_failed(vnode, ret);
	}

	_leave(" = %d", ret);
	return ret;

no_server:
	spin_lock(&vnode->lock);
	vnode->update_cnt--;
	ASSERTCMP(vnode->update_cnt, >=, 0);
	spin_unlock(&vnode->lock);
	return PTR_ERR(server);
}

/*
 * get the status of a volume
 */
int afs_vnode_get_volume_status(struct afs_vnode *vnode, struct key *key,
				struct afs_volume_status *vs)
{
	struct afs_server *server;
	int ret;

	_enter("%s{%x:%u.%u},%x,",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_get_volume_status(server, key, vnode, vs, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0)
		afs_put_server(server);

	_leave(" = %d", ret);
	return ret;

no_server:
	return PTR_ERR(server);
}

/*
 * get a lock on a file
 */
int afs_vnode_set_lock(struct afs_vnode *vnode, struct key *key,
		       afs_lock_type_t type)
{
	struct afs_server *server;
	int ret;

	_enter("%s{%x:%u.%u},%x,%u",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key), type);

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_set_lock(server, key, vnode, type, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0)
		afs_put_server(server);

	_leave(" = %d", ret);
	return ret;

no_server:
	return PTR_ERR(server);
}

/*
 * extend a lock on a file
 */
int afs_vnode_extend_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_server *server;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_extend_lock(server, key, vnode, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0)
		afs_put_server(server);

	_leave(" = %d", ret);
	return ret;

no_server:
	return PTR_ERR(server);
}

/*
 * release a lock on a file
 */
int afs_vnode_release_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_server *server;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	do {
		/* pick a server to query */
		server = afs_volume_pick_fileserver(vnode);
		if (IS_ERR(server))
			goto no_server;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_fs_release_lock(server, key, vnode, false);

	} while (!afs_volume_release_fileserver(vnode, server, ret));

	/* adjust the flags */
	if (ret == 0)
		afs_put_server(server);

	_leave(" = %d", ret);
	return ret;

no_server:
	return PTR_ERR(server);
}
