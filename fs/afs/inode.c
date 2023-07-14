/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/iversion.h>
#include "internal.h"
#include "afs_fs.h"

static const struct inode_operations afs_symlink_inode_operations = {
	.get_link	= page_get_link,
};

static noinline void dump_vnode(struct afs_vnode *vnode, struct afs_vnode *parent_vnode)
{
	static unsigned long once_only;

	pr_warn("kAFS: AFS vnode with undefined type %u\n", vnode->status.type);
	pr_warn("kAFS: A=%d m=%o s=%llx v=%llx\n",
		vnode->status.abort_code,
		vnode->status.mode,
		vnode->status.size,
		vnode->status.data_version);
	pr_warn("kAFS: vnode %llx:%llx:%x\n",
		vnode->fid.vid,
		vnode->fid.vnode,
		vnode->fid.unique);
	if (parent_vnode)
		pr_warn("kAFS: dir %llx:%llx:%x\n",
			parent_vnode->fid.vid,
			parent_vnode->fid.vnode,
			parent_vnode->fid.unique);

	if (!test_and_set_bit(0, &once_only))
		dump_stack();
}

/*
 * Set parameters for the netfs library
 */
static void afs_set_netfs_context(struct afs_vnode *vnode)
{
	netfs_inode_init(&vnode->netfs, &afs_req_ops);
}

/*
 * Initialise an inode from the vnode status.
 */
static int afs_inode_init_from_status(struct afs_operation *op,
				      struct afs_vnode_param *vp,
				      struct afs_vnode *vnode)
{
	struct afs_file_status *status = &vp->scb.status;
	struct inode *inode = AFS_VNODE_TO_I(vnode);
	struct timespec64 t;

	_enter("{%llx:%llu.%u} %s",
	       vp->fid.vid, vp->fid.vnode, vp->fid.unique,
	       op->type ? op->type->name : "???");

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       status->type,
	       status->nlink,
	       (unsigned long long) status->size,
	       status->data_version,
	       status->mode);

	write_seqlock(&vnode->cb_lock);

	vnode->cb_v_break = op->cb_v_break;
	vnode->cb_s_break = op->cb_s_break;
	vnode->status = *status;

	t = status->mtime_client;
	inode->i_ctime = t;
	inode->i_mtime = t;
	inode->i_atime = t;
	inode->i_flags |= S_NOATIME;
	inode->i_uid = make_kuid(&init_user_ns, status->owner);
	inode->i_gid = make_kgid(&init_user_ns, status->group);
	set_nlink(&vnode->netfs.inode, status->nlink);

	switch (status->type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | (status->mode & S_IALLUGO);
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &afs_file_operations;
		inode->i_mapping->a_ops	= &afs_file_aops;
		mapping_set_large_folios(inode->i_mapping);
		break;
	case AFS_FTYPE_DIR:
		inode->i_mode	= S_IFDIR |  (status->mode & S_IALLUGO);
		inode->i_op	= &afs_dir_inode_operations;
		inode->i_fop	= &afs_dir_file_operations;
		inode->i_mapping->a_ops	= &afs_dir_aops;
		mapping_set_large_folios(inode->i_mapping);
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((status->mode & 0777) == 0644) {
			inode->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);

			inode->i_mode	= S_IFDIR | 0555;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
			inode->i_mapping->a_ops	= &afs_symlink_aops;
		} else {
			inode->i_mode	= S_IFLNK | status->mode;
			inode->i_op	= &afs_symlink_inode_operations;
			inode->i_mapping->a_ops	= &afs_symlink_aops;
		}
		inode_nohighmem(inode);
		break;
	default:
		dump_vnode(vnode, op->file[0].vnode != vnode ? op->file[0].vnode : NULL);
		write_sequnlock(&vnode->cb_lock);
		return afs_protocol_error(NULL, afs_eproto_file_type);
	}

	afs_set_i_size(vnode, status->size);
	afs_set_netfs_context(vnode);

	vnode->invalid_before	= status->data_version;
	inode_set_iversion_raw(&vnode->netfs.inode, status->data_version);

	if (!vp->scb.have_cb) {
		/* it's a symlink we just created (the fileserver
		 * didn't give us a callback) */
		vnode->cb_expires_at = ktime_get_real_seconds();
	} else {
		vnode->cb_expires_at = vp->scb.callback.expires_at;
		vnode->cb_server = op->server;
		set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
	}

	write_sequnlock(&vnode->cb_lock);
	return 0;
}

/*
 * Update the core inode struct from a returned status record.
 */
static void afs_apply_status(struct afs_operation *op,
			     struct afs_vnode_param *vp)
{
	struct afs_file_status *status = &vp->scb.status;
	struct afs_vnode *vnode = vp->vnode;
	struct inode *inode = &vnode->netfs.inode;
	struct timespec64 t;
	umode_t mode;
	bool data_changed = false;
	bool change_size = vp->set_size;

	_enter("{%llx:%llu.%u} %s",
	       vp->fid.vid, vp->fid.vnode, vp->fid.unique,
	       op->type ? op->type->name : "???");

	BUG_ON(test_bit(AFS_VNODE_UNSET, &vnode->flags));

	if (status->type != vnode->status.type) {
		pr_warn("Vnode %llx:%llx:%x changed type %u to %u\n",
			vnode->fid.vid,
			vnode->fid.vnode,
			vnode->fid.unique,
			status->type, vnode->status.type);
		afs_protocol_error(NULL, afs_eproto_bad_status);
		return;
	}

	if (status->nlink != vnode->status.nlink)
		set_nlink(inode, status->nlink);

	if (status->owner != vnode->status.owner)
		inode->i_uid = make_kuid(&init_user_ns, status->owner);

	if (status->group != vnode->status.group)
		inode->i_gid = make_kgid(&init_user_ns, status->group);

	if (status->mode != vnode->status.mode) {
		mode = inode->i_mode;
		mode &= ~S_IALLUGO;
		mode |= status->mode & S_IALLUGO;
		WRITE_ONCE(inode->i_mode, mode);
	}

	t = status->mtime_client;
	inode->i_mtime = t;
	if (vp->update_ctime)
		inode->i_ctime = op->ctime;

	if (vnode->status.data_version != status->data_version)
		data_changed = true;

	vnode->status = *status;

	if (vp->dv_before + vp->dv_delta != status->data_version) {
		if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags))
			pr_warn("kAFS: vnode modified {%llx:%llu} %llx->%llx %s (op=%x)\n",
				vnode->fid.vid, vnode->fid.vnode,
				(unsigned long long)vp->dv_before + vp->dv_delta,
				(unsigned long long)status->data_version,
				op->type ? op->type->name : "???",
				op->debug_id);

		vnode->invalid_before = status->data_version;
		if (vnode->status.type == AFS_FTYPE_DIR) {
			if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
				afs_stat_v(vnode, n_inval);
		} else {
			set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
		}
		change_size = true;
		data_changed = true;
	} else if (vnode->status.type == AFS_FTYPE_DIR) {
		/* Expected directory change is handled elsewhere so
		 * that we can locally edit the directory and save on a
		 * download.
		 */
		if (test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			data_changed = false;
		change_size = true;
	}

	if (data_changed) {
		inode_set_iversion_raw(inode, status->data_version);

		/* Only update the size if the data version jumped.  If the
		 * file is being modified locally, then we might have our own
		 * idea of what the size should be that's not the same as
		 * what's on the server.
		 */
		vnode->netfs.remote_i_size = status->size;
		if (change_size) {
			afs_set_i_size(vnode, status->size);
			inode->i_ctime = t;
			inode->i_atime = t;
		}
	}
}

/*
 * Apply a callback to a vnode.
 */
static void afs_apply_callback(struct afs_operation *op,
			       struct afs_vnode_param *vp)
{
	struct afs_callback *cb = &vp->scb.callback;
	struct afs_vnode *vnode = vp->vnode;

	if (!afs_cb_is_broken(vp->cb_break_before, vnode)) {
		vnode->cb_expires_at	= cb->expires_at;
		vnode->cb_server	= op->server;
		set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
	}
}

/*
 * Apply the received status and callback to an inode all in the same critical
 * section to avoid races with afs_validate().
 */
void afs_vnode_commit_status(struct afs_operation *op, struct afs_vnode_param *vp)
{
	struct afs_vnode *vnode = vp->vnode;

	_enter("");

	write_seqlock(&vnode->cb_lock);

	if (vp->scb.have_error) {
		/* A YFS server will return this from RemoveFile2 and AFS and
		 * YFS will return this from InlineBulkStatus.
		 */
		if (vp->scb.status.abort_code == VNOVNODE) {
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			clear_nlink(&vnode->netfs.inode);
			__afs_break_callback(vnode, afs_cb_break_for_deleted);
			op->flags &= ~AFS_OPERATION_DIR_CONFLICT;
		}
	} else if (vp->scb.have_status) {
		if (vp->speculative &&
		    (test_bit(AFS_VNODE_MODIFYING, &vnode->flags) ||
		     vp->dv_before != vnode->status.data_version))
			/* Ignore the result of a speculative bulk status fetch
			 * if it splits around a modification op, thereby
			 * appearing to regress the data version.
			 */
			goto out;
		afs_apply_status(op, vp);
		if (vp->scb.have_cb)
			afs_apply_callback(op, vp);
	} else if (vp->op_unlinked && !(op->flags & AFS_OPERATION_DIR_CONFLICT)) {
		drop_nlink(&vnode->netfs.inode);
		if (vnode->netfs.inode.i_nlink == 0) {
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			__afs_break_callback(vnode, afs_cb_break_for_deleted);
		}
	}

out:
	write_sequnlock(&vnode->cb_lock);

	if (vp->scb.have_status)
		afs_cache_permit(vnode, op->key, vp->cb_break_before, &vp->scb);
}

static void afs_fetch_status_success(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[op->fetch_status.which];
	struct afs_vnode *vnode = vp->vnode;
	int ret;

	if (vnode->netfs.inode.i_state & I_NEW) {
		ret = afs_inode_init_from_status(op, vp, vnode);
		op->error = ret;
		if (ret == 0)
			afs_cache_permit(vnode, op->key, vp->cb_break_before, &vp->scb);
	} else {
		afs_vnode_commit_status(op, vp);
	}
}

const struct afs_operation_ops afs_fetch_status_operation = {
	.issue_afs_rpc	= afs_fs_fetch_status,
	.issue_yfs_rpc	= yfs_fs_fetch_status,
	.success	= afs_fetch_status_success,
	.aborted	= afs_check_for_remote_deletion,
};

/*
 * Fetch file status from the volume.
 */
int afs_fetch_status(struct afs_vnode *vnode, struct key *key, bool is_new,
		     afs_access_t *_caller_access)
{
	struct afs_operation *op;

	_enter("%s,{%llx:%llu.%u,S=%lx}",
	       vnode->volume->name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
	       vnode->flags);

	op = afs_alloc_operation(key, vnode->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	afs_op_set_vnode(op, 0, vnode);

	op->nr_files	= 1;
	op->ops		= &afs_fetch_status_operation;
	afs_begin_vnode_operation(op);
	afs_wait_for_operation(op);

	if (_caller_access)
		*_caller_access = op->file[0].scb.status.caller_access;
	return afs_put_operation(op);
}

/*
 * ilookup() comparator
 */
int afs_ilookup5_test_by_fid(struct inode *inode, void *opaque)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct afs_fid *fid = opaque;

	return (fid->vnode == vnode->fid.vnode &&
		fid->vnode_hi == vnode->fid.vnode_hi &&
		fid->unique == vnode->fid.unique);
}

/*
 * iget5() comparator
 */
static int afs_iget5_test(struct inode *inode, void *opaque)
{
	struct afs_vnode_param *vp = opaque;
	//struct afs_vnode *vnode = AFS_FS_I(inode);

	return afs_ilookup5_test_by_fid(inode, &vp->fid);
}

/*
 * iget5() inode initialiser
 */
static int afs_iget5_set(struct inode *inode, void *opaque)
{
	struct afs_vnode_param *vp = opaque;
	struct afs_super_info *as = AFS_FS_S(inode->i_sb);
	struct afs_vnode *vnode = AFS_FS_I(inode);

	vnode->volume		= as->volume;
	vnode->fid		= vp->fid;

	/* YFS supports 96-bit vnode IDs, but Linux only supports
	 * 64-bit inode numbers.
	 */
	inode->i_ino		= vnode->fid.vnode;
	inode->i_generation	= vnode->fid.unique;
	return 0;
}

/*
 * Get a cache cookie for an inode.
 */
static void afs_get_inode_cache(struct afs_vnode *vnode)
{
#ifdef CONFIG_AFS_FSCACHE
	struct {
		__be32 vnode_id;
		__be32 unique;
		__be32 vnode_id_ext[2];	/* Allow for a 96-bit key */
	} __packed key;
	struct afs_vnode_cache_aux aux;

	if (vnode->status.type != AFS_FTYPE_FILE) {
		vnode->netfs.cache = NULL;
		return;
	}

	key.vnode_id		= htonl(vnode->fid.vnode);
	key.unique		= htonl(vnode->fid.unique);
	key.vnode_id_ext[0]	= htonl(vnode->fid.vnode >> 32);
	key.vnode_id_ext[1]	= htonl(vnode->fid.vnode_hi);
	afs_set_cache_aux(vnode, &aux);

	afs_vnode_set_cache(vnode,
			    fscache_acquire_cookie(
				    vnode->volume->cache,
				    vnode->status.type == AFS_FTYPE_FILE ?
				    0 : FSCACHE_ADV_SINGLE_CHUNK,
				    &key, sizeof(key),
				    &aux, sizeof(aux),
				    i_size_read(&vnode->netfs.inode)));
#endif
}

/*
 * inode retrieval
 */
struct inode *afs_iget(struct afs_operation *op, struct afs_vnode_param *vp)
{
	struct afs_vnode_param *dvp = &op->file[0];
	struct super_block *sb = dvp->vnode->netfs.inode.i_sb;
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	_enter(",{%llx:%llu.%u},,", vp->fid.vid, vp->fid.vnode, vp->fid.unique);

	inode = iget5_locked(sb, vp->fid.vnode, afs_iget5_test, afs_iget5_set, vp);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	vnode = AFS_FS_I(inode);

	_debug("GOT INODE %p { vl=%llx vn=%llx, u=%x }",
	       inode, vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	/* deal with an existing inode */
	if (!(inode->i_state & I_NEW)) {
		_leave(" = %p", inode);
		return inode;
	}

	ret = afs_inode_init_from_status(op, vp, vnode);
	if (ret < 0)
		goto bad_inode;

	afs_get_inode_cache(vnode);

	/* success */
	clear_bit(AFS_VNODE_UNSET, &vnode->flags);
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;

	/* failure */
bad_inode:
	iget_failed(inode);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

static int afs_iget5_set_root(struct inode *inode, void *opaque)
{
	struct afs_super_info *as = AFS_FS_S(inode->i_sb);
	struct afs_vnode *vnode = AFS_FS_I(inode);

	vnode->volume		= as->volume;
	vnode->fid.vid		= as->volume->vid,
	vnode->fid.vnode	= 1;
	vnode->fid.unique	= 1;
	inode->i_ino		= 1;
	inode->i_generation	= 1;
	return 0;
}

/*
 * Set up the root inode for a volume.  This is always vnode 1, unique 1 within
 * the volume.
 */
struct inode *afs_root_iget(struct super_block *sb, struct key *key)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_operation *op;
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	_enter(",{%llx},,", as->volume->vid);

	inode = iget5_locked(sb, 1, NULL, afs_iget5_set_root, NULL);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT ROOT INODE %p { vl=%llx }", inode, as->volume->vid);

	BUG_ON(!(inode->i_state & I_NEW));

	vnode = AFS_FS_I(inode);
	vnode->cb_v_break = as->volume->cb_v_break,
	afs_set_netfs_context(vnode);

	op = afs_alloc_operation(key, as->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto error;
	}

	afs_op_set_vnode(op, 0, vnode);

	op->nr_files	= 1;
	op->ops		= &afs_fetch_status_operation;
	ret = afs_do_sync_operation(op);
	if (ret < 0)
		goto error;

	afs_get_inode_cache(vnode);

	clear_bit(AFS_VNODE_UNSET, &vnode->flags);
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;

error:
	iget_failed(inode);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

/*
 * mark the data attached to an inode as obsolete due to a write on the server
 * - might also want to ditch all the outstanding writes and dirty pages
 */
static void afs_zap_data(struct afs_vnode *vnode)
{
	_enter("{%llx:%llu}", vnode->fid.vid, vnode->fid.vnode);

	afs_invalidate_cache(vnode, 0);

	/* nuke all the non-dirty pages that aren't locked, mapped or being
	 * written back in a regular file and completely discard the pages in a
	 * directory or symlink */
	if (S_ISREG(vnode->netfs.inode.i_mode))
		invalidate_remote_inode(&vnode->netfs.inode);
	else
		invalidate_inode_pages2(vnode->netfs.inode.i_mapping);
}

/*
 * Check to see if we have a server currently serving this volume and that it
 * hasn't been reinitialised or dropped from the list.
 */
static bool afs_check_server_good(struct afs_vnode *vnode)
{
	struct afs_server_list *slist;
	struct afs_server *server;
	bool good;
	int i;

	if (vnode->cb_fs_s_break == atomic_read(&vnode->volume->cell->fs_s_break))
		return true;

	rcu_read_lock();

	slist = rcu_dereference(vnode->volume->servers);
	for (i = 0; i < slist->nr_servers; i++) {
		server = slist->servers[i].server;
		if (server == vnode->cb_server) {
			good = (vnode->cb_s_break == server->cb_s_break);
			rcu_read_unlock();
			return good;
		}
	}

	rcu_read_unlock();
	return false;
}

/*
 * Check the validity of a vnode/inode.
 */
bool afs_check_validity(struct afs_vnode *vnode)
{
	enum afs_cb_break_reason need_clear = afs_cb_break_no_break;
	time64_t now = ktime_get_real_seconds();
	unsigned int cb_break;
	int seq = 0;

	do {
		read_seqbegin_or_lock(&vnode->cb_lock, &seq);
		cb_break = vnode->cb_break;

		if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
			if (vnode->cb_v_break != vnode->volume->cb_v_break)
				need_clear = afs_cb_break_for_v_break;
			else if (!afs_check_server_good(vnode))
				need_clear = afs_cb_break_for_s_reinit;
			else if (test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
				need_clear = afs_cb_break_for_zap;
			else if (vnode->cb_expires_at - 10 <= now)
				need_clear = afs_cb_break_for_lapsed;
		} else if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
			;
		} else {
			need_clear = afs_cb_break_no_promise;
		}

	} while (need_seqretry(&vnode->cb_lock, seq));

	done_seqretry(&vnode->cb_lock, seq);

	if (need_clear == afs_cb_break_no_break)
		return true;

	write_seqlock(&vnode->cb_lock);
	if (need_clear == afs_cb_break_no_promise)
		vnode->cb_v_break = vnode->volume->cb_v_break;
	else if (cb_break == vnode->cb_break)
		__afs_break_callback(vnode, need_clear);
	else
		trace_afs_cb_miss(&vnode->fid, need_clear);
	write_sequnlock(&vnode->cb_lock);
	return false;
}

/*
 * Returns true if the pagecache is still valid.  Does not sleep.
 */
bool afs_pagecache_valid(struct afs_vnode *vnode)
{
	if (unlikely(test_bit(AFS_VNODE_DELETED, &vnode->flags))) {
		if (vnode->netfs.inode.i_nlink)
			clear_nlink(&vnode->netfs.inode);
		return true;
	}

	if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags) &&
	    afs_check_validity(vnode))
		return true;

	return false;
}

/*
 * validate a vnode/inode
 * - there are several things we need to check
 *   - parent dir data changes (rm, rmdir, rename, mkdir, create, link,
 *     symlink)
 *   - parent dir metadata changed (security changes)
 *   - dentry data changed (write, truncate)
 *   - dentry metadata changed (security changes)
 */
int afs_validate(struct afs_vnode *vnode, struct key *key)
{
	int ret;

	_enter("{v={%llx:%llu} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	if (afs_pagecache_valid(vnode))
		goto valid;

	down_write(&vnode->validate_lock);

	/* if the promise has expired, we need to check the server again to get
	 * a new promise - note that if the (parent) directory's metadata was
	 * changed then the security may be different and we may no longer have
	 * access */
	if (!test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		_debug("not promised");
		ret = afs_fetch_status(vnode, key, false, NULL);
		if (ret < 0) {
			if (ret == -ENOENT) {
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
				ret = -ESTALE;
			}
			goto error_unlock;
		}
		_debug("new promise [fl=%lx]", vnode->flags);
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_debug("file already deleted");
		ret = -ESTALE;
		goto error_unlock;
	}

	/* if the vnode's data version number changed then its contents are
	 * different */
	if (test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
		afs_zap_data(vnode);
	up_write(&vnode->validate_lock);
valid:
	_leave(" = 0");
	return 0;

error_unlock:
	up_write(&vnode->validate_lock);
	_leave(" = %d", ret);
	return ret;
}

/*
 * read the attributes of an inode
 */
int afs_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct key *key;
	int ret, seq = 0;

	_enter("{ ino=%lu v=%u }", inode->i_ino, inode->i_generation);

	if (vnode->volume &&
	    !(query_flags & AT_STATX_DONT_SYNC) &&
	    !test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		key = afs_request_key(vnode->volume->cell);
		if (IS_ERR(key))
			return PTR_ERR(key);
		ret = afs_validate(vnode, key);
		key_put(key);
		if (ret < 0)
			return ret;
	}

	do {
		read_seqbegin_or_lock(&vnode->cb_lock, &seq);
		generic_fillattr(&nop_mnt_idmap, inode, stat);
		if (test_bit(AFS_VNODE_SILLY_DELETED, &vnode->flags) &&
		    stat->nlink > 0)
			stat->nlink -= 1;

		/* Lie about the size of directories.  We maintain a locally
		 * edited copy and may make different allocation decisions on
		 * it, but we need to give userspace the server's size.
		 */
		if (S_ISDIR(inode->i_mode))
			stat->size = vnode->netfs.remote_i_size;
	} while (need_seqretry(&vnode->cb_lock, seq));

	done_seqretry(&vnode->cb_lock, seq);
	return 0;
}

/*
 * discard an AFS inode
 */
int afs_drop_inode(struct inode *inode)
{
	_enter("");

	if (test_bit(AFS_VNODE_PSEUDODIR, &AFS_FS_I(inode)->flags))
		return generic_delete_inode(inode);
	else
		return generic_drop_inode(inode);
}

/*
 * clear an AFS inode
 */
void afs_evict_inode(struct inode *inode)
{
	struct afs_vnode_cache_aux aux;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("{%llx:%llu.%d}",
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	_debug("CLEAR INODE %p", inode);

	ASSERTCMP(inode->i_ino, ==, vnode->fid.vnode);

	truncate_inode_pages_final(&inode->i_data);

	afs_set_cache_aux(vnode, &aux);
	fscache_clear_inode_writeback(afs_vnode_cache(vnode), inode, &aux);
	clear_inode(inode);

	while (!list_empty(&vnode->wb_keys)) {
		struct afs_wb_key *wbk = list_entry(vnode->wb_keys.next,
						    struct afs_wb_key, vnode_link);
		list_del(&wbk->vnode_link);
		afs_put_wb_key(wbk);
	}

	fscache_relinquish_cookie(afs_vnode_cache(vnode),
				  test_bit(AFS_VNODE_DELETED, &vnode->flags));

	afs_prune_wb_keys(vnode);
	afs_put_permits(rcu_access_pointer(vnode->permit_cache));
	key_put(vnode->silly_key);
	vnode->silly_key = NULL;
	key_put(vnode->lock_key);
	vnode->lock_key = NULL;
	_leave("");
}

static void afs_setattr_success(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct inode *inode = &vp->vnode->netfs.inode;
	loff_t old_i_size = i_size_read(inode);

	op->setattr.old_i_size = old_i_size;
	afs_vnode_commit_status(op, vp);
	/* inode->i_size has now been changed. */

	if (op->setattr.attr->ia_valid & ATTR_SIZE) {
		loff_t size = op->setattr.attr->ia_size;
		if (size > old_i_size)
			pagecache_isize_extended(inode, old_i_size, size);
	}
}

static void afs_setattr_edit_file(struct afs_operation *op)
{
	struct afs_vnode_param *vp = &op->file[0];
	struct inode *inode = &vp->vnode->netfs.inode;

	if (op->setattr.attr->ia_valid & ATTR_SIZE) {
		loff_t size = op->setattr.attr->ia_size;
		loff_t i_size = op->setattr.old_i_size;

		if (size < i_size)
			truncate_pagecache(inode, size);
		if (size != i_size)
			fscache_resize_cookie(afs_vnode_cache(vp->vnode),
					      vp->scb.status.size);
	}
}

static const struct afs_operation_ops afs_setattr_operation = {
	.issue_afs_rpc	= afs_fs_setattr,
	.issue_yfs_rpc	= yfs_fs_setattr,
	.success	= afs_setattr_success,
	.edit_dir	= afs_setattr_edit_file,
};

/*
 * set the attributes of an inode
 */
int afs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *attr)
{
	const unsigned int supported =
		ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
		ATTR_MTIME | ATTR_MTIME_SET | ATTR_TIMES_SET | ATTR_TOUCH;
	struct afs_operation *op;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	struct inode *inode = &vnode->netfs.inode;
	loff_t i_size;
	int ret;

	_enter("{%llx:%llu},{n=%pd},%x",
	       vnode->fid.vid, vnode->fid.vnode, dentry,
	       attr->ia_valid);

	if (!(attr->ia_valid & supported)) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	i_size = i_size_read(inode);
	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(inode->i_mode))
			return -EISDIR;

		ret = inode_newsize_ok(inode, attr->ia_size);
		if (ret)
			return ret;

		if (attr->ia_size == i_size)
			attr->ia_valid &= ~ATTR_SIZE;
	}

	fscache_use_cookie(afs_vnode_cache(vnode), true);

	/* Prevent any new writebacks from starting whilst we do this. */
	down_write(&vnode->validate_lock);

	if ((attr->ia_valid & ATTR_SIZE) && S_ISREG(inode->i_mode)) {
		loff_t size = attr->ia_size;

		/* Wait for any outstanding writes to the server to complete */
		loff_t from = min(size, i_size);
		loff_t to = max(size, i_size);
		ret = filemap_fdatawait_range(inode->i_mapping, from, to);
		if (ret < 0)
			goto out_unlock;

		/* Don't talk to the server if we're just shortening in-memory
		 * writes that haven't gone to the server yet.
		 */
		if (!(attr->ia_valid & (supported & ~ATTR_SIZE & ~ATTR_MTIME)) &&
		    attr->ia_size < i_size &&
		    attr->ia_size > vnode->status.size) {
			truncate_pagecache(inode, attr->ia_size);
			fscache_resize_cookie(afs_vnode_cache(vnode),
					      attr->ia_size);
			i_size_write(inode, attr->ia_size);
			ret = 0;
			goto out_unlock;
		}
	}

	op = afs_alloc_operation(((attr->ia_valid & ATTR_FILE) ?
				  afs_file_key(attr->ia_file) : NULL),
				 vnode->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto out_unlock;
	}

	afs_op_set_vnode(op, 0, vnode);
	op->setattr.attr = attr;

	if (attr->ia_valid & ATTR_SIZE) {
		op->file[0].dv_delta = 1;
		op->file[0].set_size = true;
	}
	op->ctime = attr->ia_ctime;
	op->file[0].update_ctime = 1;
	op->file[0].modification = true;

	op->ops = &afs_setattr_operation;
	ret = afs_do_sync_operation(op);

out_unlock:
	up_write(&vnode->validate_lock);
	fscache_unuse_cookie(afs_vnode_cache(vnode), NULL, NULL);
	_leave(" = %d", ret);
	return ret;
}
