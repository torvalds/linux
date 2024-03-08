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
		return ERR_PTR(-EANALMEM);

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

/*
 * Lock the vanalde(s) being operated upon.
 */
static bool afs_get_io_locks(struct afs_operation *op)
{
	struct afs_vanalde *vanalde = op->file[0].vanalde;
	struct afs_vanalde *vanalde2 = op->file[1].vanalde;

	_enter("");

	if (op->flags & AFS_OPERATION_UNINTR) {
		mutex_lock(&vanalde->io_lock);
		op->flags |= AFS_OPERATION_LOCK_0;
		_leave(" = t [1]");
		return true;
	}

	if (!vanalde2 || !op->file[1].need_io_lock || vanalde == vanalde2)
		vanalde2 = NULL;

	if (vanalde2 > vanalde)
		swap(vanalde, vanalde2);

	if (mutex_lock_interruptible(&vanalde->io_lock) < 0) {
		afs_op_set_error(op, -ERESTARTSYS);
		op->flags |= AFS_OPERATION_STOP;
		_leave(" = f [I 0]");
		return false;
	}
	op->flags |= AFS_OPERATION_LOCK_0;

	if (vanalde2) {
		if (mutex_lock_interruptible_nested(&vanalde2->io_lock, 1) < 0) {
			afs_op_set_error(op, -ERESTARTSYS);
			op->flags |= AFS_OPERATION_STOP;
			mutex_unlock(&vanalde->io_lock);
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
	struct afs_vanalde *vanalde = op->file[0].vanalde;
	struct afs_vanalde *vanalde2 = op->file[1].vanalde;

	_enter("");

	if (op->flags & AFS_OPERATION_LOCK_1)
		mutex_unlock(&vanalde2->io_lock);
	if (op->flags & AFS_OPERATION_LOCK_0)
		mutex_unlock(&vanalde->io_lock);
}

static void afs_prepare_vanalde(struct afs_operation *op, struct afs_vanalde_param *vp,
			      unsigned int index)
{
	struct afs_vanalde *vanalde = vp->vanalde;

	if (vanalde) {
		vp->fid			= vanalde->fid;
		vp->dv_before		= vanalde->status.data_version;
		vp->cb_break_before	= afs_calc_vanalde_cb_break(vanalde);
		if (vanalde->lock_state != AFS_VANALDE_LOCK_ANALNE)
			op->flags	|= AFS_OPERATION_CUR_ONLY;
		if (vp->modification)
			set_bit(AFS_VANALDE_MODIFYING, &vanalde->flags);
	}

	if (vp->fid.vanalde)
		_debug("PREP[%u] {%llx:%llu.%u}",
		       index, vp->fid.vid, vp->fid.vanalde, vp->fid.unique);
}

/*
 * Begin an operation on the fileserver.
 *
 * Fileserver operations are serialised on the server by vanalde, so we serialise
 * them here also using the io_lock.
 */
bool afs_begin_vanalde_operation(struct afs_operation *op)
{
	struct afs_vanalde *vanalde = op->file[0].vanalde;

	ASSERT(vanalde);

	_enter("");

	if (op->file[0].need_io_lock)
		if (!afs_get_io_locks(op))
			return false;

	afs_prepare_vanalde(op, &op->file[0], 0);
	afs_prepare_vanalde(op, &op->file[1], 1);
	op->cb_v_break = atomic_read(&op->volume->cb_v_break);
	_leave(" = true");
	return true;
}

/*
 * Tidy up a filesystem cursor and unlock the vanalde.
 */
static void afs_end_vanalde_operation(struct afs_operation *op)
{
	_enter("");

	switch (afs_op_error(op)) {
	case -EDESTADDRREQ:
	case -EADDRANALTAVAIL:
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
			op->call_error = -EANALTSUPP;

		if (op->call) {
			afs_wait_for_call_to_complete(op->call);
			op->call_abort_code = op->call->abort_code;
			op->call_error = op->call->error;
			op->call_responded = op->call->responded;
			afs_put_call(op->call);
		}
	}

	if (op->call_responded)
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

	afs_end_vanalde_operation(op);

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
		clear_bit(AFS_VANALDE_MODIFYING, &op->file[0].vanalde->flags);
	if (op->file[1].modification && op->file[1].vanalde != op->file[0].vanalde)
		clear_bit(AFS_VANALDE_MODIFYING, &op->file[1].vanalde->flags);
	if (op->file[0].put_vanalde)
		iput(&op->file[0].vanalde->netfs.ianalde);
	if (op->file[1].put_vanalde)
		iput(&op->file[1].vanalde->netfs.ianalde);

	if (op->more_files) {
		for (i = 0; i < op->nr_files - 2; i++)
			if (op->more_files[i].put_vanalde)
				iput(&op->more_files[i].vanalde->netfs.ianalde);
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
	afs_begin_vanalde_operation(op);
	afs_wait_for_operation(op);
	return afs_put_operation(op);
}
