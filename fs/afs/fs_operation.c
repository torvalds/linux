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

	op->key			= key;
	op->volume		= afs_get_volume(volume, afs_volume_trace_get_new_op);
	op->net			= volume->cell->net;
	op->cb_v_break		= atomic_read(&volume->cb_v_break);
	op->pre_volsync.creation = volume->creation_time;
	op->pre_volsync.update	= volume->update_time;
	op->debug_id		= atomic_inc_return(&afs_operation_debug_counter);
	op->nr_iterations	= -1;
	afs_op_set_error(op, -EDESTADDRREQ);

	_leave(" = [op=%08x]", op->debug_id);
	return op;
}

struct afs_io_locker {
	struct list_head	link;
	struct task_struct	*task;
	unsigned long		have_lock;
};

/*
 * Unlock the I/O lock on a vnode.
 */
static void afs_unlock_for_io(struct afs_vnode *vnode)
{
	struct afs_io_locker *locker;

	spin_lock(&vnode->lock);
	locker = list_first_entry_or_null(&vnode->io_lock_waiters,
					  struct afs_io_locker, link);
	if (locker) {
		list_del(&locker->link);
		smp_store_release(&locker->have_lock, 1); /* The unlock barrier. */
		smp_mb__after_atomic(); /* Store have_lock before task state */
		wake_up_process(locker->task);
	} else {
		clear_bit(AFS_VNODE_IO_LOCK, &vnode->flags);
	}
	spin_unlock(&vnode->lock);
}

/*
 * Lock the I/O lock on a vnode uninterruptibly.  We can't use an ordinary
 * mutex as lockdep will complain if we unlock it in the wrong thread.
 */
static void afs_lock_for_io(struct afs_vnode *vnode)
{
	struct afs_io_locker myself = { .task = current, };

	spin_lock(&vnode->lock);

	if (!test_and_set_bit(AFS_VNODE_IO_LOCK, &vnode->flags)) {
		spin_unlock(&vnode->lock);
		return;
	}

	list_add_tail(&myself.link, &vnode->io_lock_waiters);
	spin_unlock(&vnode->lock);

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (smp_load_acquire(&myself.have_lock)) /* The lock barrier */
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);
}

/*
 * Lock the I/O lock on a vnode interruptibly.  We can't use an ordinary mutex
 * as lockdep will complain if we unlock it in the wrong thread.
 */
static int afs_lock_for_io_interruptible(struct afs_vnode *vnode)
{
	struct afs_io_locker myself = { .task = current, };
	int ret = 0;

	spin_lock(&vnode->lock);

	if (!test_and_set_bit(AFS_VNODE_IO_LOCK, &vnode->flags)) {
		spin_unlock(&vnode->lock);
		return 0;
	}

	list_add_tail(&myself.link, &vnode->io_lock_waiters);
	spin_unlock(&vnode->lock);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (smp_load_acquire(&myself.have_lock) || /* The lock barrier */
		    signal_pending(current))
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	/* If we got a signal, try to transfer the lock onto the next
	 * waiter.
	 */
	if (unlikely(signal_pending(current))) {
		spin_lock(&vnode->lock);
		if (myself.have_lock) {
			spin_unlock(&vnode->lock);
			afs_unlock_for_io(vnode);
		} else {
			list_del(&myself.link);
			spin_unlock(&vnode->lock);
		}
		ret = -ERESTARTSYS;
	}
	return ret;
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
		afs_lock_for_io(vnode);
		op->flags |= AFS_OPERATION_LOCK_0;
		_leave(" = t [1]");
		return true;
	}

	if (!vnode2 || !op->file[1].need_io_lock || vnode == vnode2)
		vnode2 = NULL;

	if (vnode2 > vnode)
		swap(vnode, vnode2);

	if (afs_lock_for_io_interruptible(vnode) < 0) {
		afs_op_set_error(op, -ERESTARTSYS);
		op->flags |= AFS_OPERATION_STOP;
		_leave(" = f [I 0]");
		return false;
	}
	op->flags |= AFS_OPERATION_LOCK_0;

	if (vnode2) {
		if (afs_lock_for_io_interruptible(vnode2) < 0) {
			afs_op_set_error(op, -ERESTARTSYS);
			op->flags |= AFS_OPERATION_STOP;
			afs_unlock_for_io(vnode);
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
		afs_unlock_for_io(vnode2);
	if (op->flags & AFS_OPERATION_LOCK_0)
		afs_unlock_for_io(vnode);
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
	op->cb_v_break = atomic_read(&op->volume->cb_v_break);
	_leave(" = true");
	return true;
}

/*
 * Tidy up a filesystem cursor and unlock the vnode.
 */
void afs_end_vnode_operation(struct afs_operation *op)
{
	_enter("");

	switch (afs_op_error(op)) {
	case -EDESTADDRREQ:
	case -EADDRNOTAVAIL:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
		afs_dump_edestaddrreq(op);
		break;
	}

	afs_drop_io_locks(op);
}

/*
 * Wait for an in-progress operation to complete.
 */
void afs_wait_for_operation(struct afs_operation *op)
{
	_enter("");

	while (afs_select_fileserver(op)) {
		op->call_responded = false;
		op->call_error = 0;
		op->call_abort_code = 0;
		if (test_bit(AFS_SERVER_FL_IS_YFS, &op->server->flags) &&
		    op->ops->issue_yfs_rpc)
			op->ops->issue_yfs_rpc(op);
		else if (op->ops->issue_afs_rpc)
			op->ops->issue_afs_rpc(op);
		else
			op->call_error = -ENOTSUPP;

		if (op->call) {
			afs_wait_for_call_to_complete(op->call);
			op->call_abort_code = op->call->abort_code;
			op->call_error = op->call->error;
			op->call_responded = op->call->responded;
			afs_put_call(op->call);
		}
	}

	if (op->call_responded && op->server)
		set_bit(AFS_SERVER_FL_RESPONDING, &op->server->flags);

	if (!afs_op_error(op)) {
		_debug("success");
		op->ops->success(op);
	} else if (op->cumul_error.aborted) {
		if (op->ops->aborted)
			op->ops->aborted(op);
	} else {
		if (op->ops->failed)
			op->ops->failed(op);
	}

	afs_end_vnode_operation(op);

	if (!afs_op_error(op) && op->ops->edit_dir) {
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
	struct afs_addr_list *alist;
	int i, ret = afs_op_error(op);

	_enter("op=%08x,%d", op->debug_id, ret);

	if (op->ops && op->ops->put)
		op->ops->put(op);
	if (op->file[0].modification)
		clear_bit(AFS_VNODE_MODIFYING, &op->file[0].vnode->flags);
	if (op->file[1].modification && op->file[1].vnode != op->file[0].vnode)
		clear_bit(AFS_VNODE_MODIFYING, &op->file[1].vnode->flags);
	if (op->file[0].put_vnode)
		iput(&op->file[0].vnode->netfs.inode);
	if (op->file[1].put_vnode)
		iput(&op->file[1].vnode->netfs.inode);

	if (op->more_files) {
		for (i = 0; i < op->nr_files - 2; i++)
			if (op->more_files[i].put_vnode)
				iput(&op->more_files[i].vnode->netfs.inode);
		kfree(op->more_files);
	}

	if (op->estate) {
		alist = op->estate->addresses;
		if (alist) {
			if (op->call_responded &&
			    op->addr_index != alist->preferred &&
			    test_bit(alist->preferred, &op->addr_tried))
				WRITE_ONCE(alist->preferred, op->addr_index);
		}
	}

	afs_clear_server_states(op);
	afs_put_serverlist(op->net, op->server_list);
	afs_put_volume(op->volume, afs_volume_trace_put_put_op);
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
