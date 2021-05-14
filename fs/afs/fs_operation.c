// SPDX-License-Identifier: GPL-2.0-or-later
/* Fileserver-directed operation handling.
 *
 * Copyright (C) 2020 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "internal.h"

static atomic_t afs_operation_debug_counter;

/*
 * Create an operation against a volume.
 */
struct afs_operation *afs_alloc_operation(struct key *key, struct afs_volume *volume)
{
	struct afs_operation *op;

	_enter("");

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return ERR_PTR(-ENOMEM);

	if (!key) {
		key = afs_request_key(volume->cell);
		if (IS_ERR(key)) {
			kfree(op);
			return ERR_CAST(key);
		}
	} else {
		key_get(key);
	}

	op->key		= key;
	op->volume	= afs_get_volume(volume, afs_volume_trace_get_new_op);
	op->net		= volume->cell->net;
	op->cb_v_break	= volume->cb_v_break;
	op->debug_id	= atomic_inc_return(&afs_operation_debug_counter);
	op->error	= -EDESTADDRREQ;
	op->ac.error	= SHRT_MAX;

	_leave(" = [op=%08x]", op->debug_id);
	return op;
}

/*
 * Lock the vnode(s) being operated upon.
 */
static bool afs_get_io_locks(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;
	struct afs_vnode *vnode2 = op->file[1].vnode;

	_enter("");

	if (op->flags & AFS_OPERATION_UNINTR) {
		mutex_lock(&vnode->io_lock);
		op->flags |= AFS_OPERATION_LOCK_0;
		_leave(" = t [1]");
		return true;
	}

	if (!vnode2 || !op->file[1].need_io_lock || vnode == vnode2)
		vnode2 = NULL;

	if (vnode2 > vnode)
		swap(vnode, vnode2);

	if (mutex_lock_interruptible(&vnode->io_lock) < 0) {
		op->error = -ERESTARTSYS;
		op->flags |= AFS_OPERATION_STOP;
		_leave(" = f [I 0]");
		return false;
	}
	op->flags |= AFS_OPERATION_LOCK_0;

	if (vnode2) {
		if (mutex_lock_interruptible_nested(&vnode2->io_lock, 1) < 0) {
			op->error = -ERESTARTSYS;
			op->flags |= AFS_OPERATION_STOP;
			mutex_unlock(&vnode->io_lock);
			op->flags &= ~AFS_OPERATION_LOCK_0;
			_leave(" = f [I 1]");
			return false;
		}
		op->flags |= AFS_OPERATION_LOCK_1;
	}

	_leave(" = t [2]");
	return true;
}

static void afs_drop_io_locks(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;
	struct afs_vnode *vnode2 = op->file[1].vnode;

	_enter("");

	if (op->flags & AFS_OPERATION_LOCK_1)
		mutex_unlock(&vnode2->io_lock);
	if (op->flags & AFS_OPERATION_LOCK_0)
		mutex_unlock(&vnode->io_lock);
}

static void afs_prepare_vnode(struct afs_operation *op, struct afs_vnode_param *vp,
			      unsigned int index)
{
	struct afs_vnode *vnode = vp->vnode;

	if (vnode) {
		vp->fid			= vnode->fid;
		vp->dv_before		= vnode->status.data_version;
		vp->cb_break_before	= afs_calc_vnode_cb_break(vnode);
		if (vnode->lock_state != AFS_VNODE_LOCK_NONE)
			op->flags	|= AFS_OPERATION_CUR_ONLY;
		if (vp->modification)
			set_bit(AFS_VNODE_MODIFYING, &vnode->flags);
	}

	if (vp->fid.vnode)
		_debug("PREP[%u] {%llx:%llu.%u}",
		       index, vp->fid.vid, vp->fid.vnode, vp->fid.unique);
}

/*
 * Begin an operation on the fileserver.
 *
 * Fileserver operations are serialised on the server by vnode, so we serialise
 * them here also using the io_lock.
 */
bool afs_begin_vnode_operation(struct afs_operation *op)
{
	struct afs_vnode *vnode = op->file[0].vnode;

	ASSERT(vnode);

	_enter("");

	if (op->file[0].need_io_lock)
		if (!afs_get_io_locks(op))
			return false;

	afs_prepare_vnode(op, &op->file[0], 0);
	afs_prepare_vnode(op, &op->file[1], 1);
	op->cb_v_break = op->volume->cb_v_break;
	_leave(" = true");
	return true;
}

/*
 * Tidy up a filesystem cursor and unlock the vnode.
 */
static void afs_end_vnode_operation(struct afs_operation *op)
{
	_enter("");

	if (op->error == -EDESTADDRREQ ||
	    op->error == -EADDRNOTAVAIL ||
	    op->error == -ENETUNREACH ||
	    op->error == -EHOSTUNREACH)
		afs_dump_edestaddrreq(op);

	afs_drop_io_locks(op);

	if (op->error == -ECONNABORTED)
		op->error = afs_abort_to_error(op->ac.abort_code);
}

/*
 * Wait for an in-progress operation to complete.
 */
void afs_wait_for_operation(struct afs_operation *op)
{
	_enter("");

	while (afs_select_fileserver(op)) {
		op->cb_s_break = op->server->cb_s_break;
		if (test_bit(AFS_SERVER_FL_IS_YFS, &op->server->flags) &&
		    op->ops->issue_yfs_rpc)
			op->ops->issue_yfs_rpc(op);
		else if (op->ops->issue_afs_rpc)
			op->ops->issue_afs_rpc(op);
		else
			op->ac.error = -ENOTSUPP;

		if (op->call)
			op->error = afs_wait_for_call_to_complete(op->call, &op->ac);
	}

	switch (op->error) {
	case 0:
		_debug("success");
		op->ops->success(op);
		break;
	case -ECONNABORTED:
		if (op->ops->aborted)
			op->ops->aborted(op);
		break;
	default:
		break;
	}

	afs_end_vnode_operation(op);

	if (op->error == 0 && op->ops->edit_dir) {
		_debug("edit_dir");
		op->ops->edit_dir(op);
	}
	_leave("");
}

/*
 * Dispose of an operation.
 */
int afs_put_operation(struct afs_operation *op)
{
	int i, ret = op->error;

	_enter("op=%08x,%d", op->debug_id, ret);

	if (op->ops && op->ops->put)
		op->ops->put(op);
	if (op->file[0].modification)
		clear_bit(AFS_VNODE_MODIFYING, &op->file[0].vnode->flags);
	if (op->file[1].modification && op->file[1].vnode != op->file[0].vnode)
		clear_bit(AFS_VNODE_MODIFYING, &op->file[1].vnode->flags);
	if (op->file[0].put_vnode)
		iput(&op->file[0].vnode->vfs_inode);
	if (op->file[1].put_vnode)
		iput(&op->file[1].vnode->vfs_inode);

	if (op->more_files) {
		for (i = 0; i < op->nr_files - 2; i++)
			if (op->more_files[i].put_vnode)
				iput(&op->more_files[i].vnode->vfs_inode);
		kfree(op->more_files);
	}

	afs_end_cursor(&op->ac);
	afs_put_serverlist(op->net, op->server_list);
	afs_put_volume(op->net, op->volume, afs_volume_trace_put_put_op);
	key_put(op->key);
	kfree(op);
	return ret;
}

int afs_do_sync_operation(struct afs_operation *op)
{
	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);
	return afs_put_operation(op);
}
