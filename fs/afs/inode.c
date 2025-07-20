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

void afs_init_new_symlink(struct afs_vnode *vnode, struct afs_operation *op)
{
	size_t size = strlen(op->create.symlink) + 1;
	size_t dsize = 0;
	char *p;

	if (netfs_alloc_folioq_buffer(NULL, &vnode->directory, &dsize, size,
				      mapping_gfp_mask(vnode->netfs.inode.i_mapping)) < 0)
		return;

	vnode->directory_size = dsize;
	p = kmap_local_folio(folioq_folio(vnode->directory, 0), 0);
	memcpy(p, op->create.symlink, size);
	kunmap_local(p);
	set_bit(AFS_VNODE_DIR_READ, &vnode->flags);
	netfs_single_mark_inode_dirty(&vnode->netfs.inode);
}

static void afs_put_link(void *arg)
{
	struct folio *folio = virt_to_folio(arg);

	kunmap_local(arg);
	folio_put(folio);
}

const char *afs_get_link(struct dentry *dentry, struct inode *inode,
			 struct delayed_call *callback)
{
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct folio *folio;
	char *content;
	ssize_t ret;

	if (!dentry) {
		/* RCU pathwalk. */
		if (!test_bit(AFS_VNODE_DIR_READ, &vnode->flags) || !afs_check_validity(vnode))
			return ERR_PTR(-ECHILD);
		goto good;
	}

	if (test_bit(AFS_VNODE_DIR_READ, &vnode->flags))
		goto fetch;

	ret = afs_validate(vnode, NULL);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags) &&
	    test_bit(AFS_VNODE_DIR_READ, &vnode->flags))
		goto good;

fetch:
	ret = afs_read_single(vnode, NULL);
	if (ret < 0)
		return ERR_PTR(ret);
	set_bit(AFS_VNODE_DIR_READ, &vnode->flags);

good:
	folio = folioq_folio(vnode->directory, 0);
	folio_get(folio);
	content = kmap_local_folio(folio, 0);
	set_delayed_call(callback, afs_put_link, content);
	return content;
}

int afs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	DEFINE_DELAYED_CALL(done);
	const char *content;
	int len;

	content = afs_get_link(dentry, d_inode(dentry), &done);
	if (IS_ERR(content)) {
		do_delayed_call(&done);
		return PTR_ERR(content);
	}

	len = umin(strlen(content), buflen);
	if (copy_to_user(buffer, content, len))
		len = -EFAULT;
	do_delayed_call(&done);
	return len;
}

static const struct inode_operations afs_symlink_inode_operations = {
	.get_link	= afs_get_link,
	.readlink	= afs_readlink,
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
	netfs_inode_init(&vnode->netfs, &afs_req_ops, true);
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

	vnode->cb_v_check = op->cb_v_break;
	vnode->status = *status;

	t = status->mtime_client;
	inode_set_ctime_to_ts(inode, t);
	inode_set_mtime_to_ts(inode, t);
	inode_set_atime_to_ts(inode, t);
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
		__set_bit(NETFS_ICTX_SINGLE_NO_UPLOAD, &vnode->netfs.flags);
		/* Assume locally cached directory data will be valid. */
		__set_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((status->mode & 0777) == 0644) {
			inode->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);

			inode->i_mode	= S_IFDIR | 0555;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
		} else {
			inode->i_mode	= S_IFLNK | status->mode;
			inode->i_op	= &afs_symlink_inode_operations;
		}
		inode->i_mapping->a_ops	= &afs_dir_aops;
		inode_nohighmem(inode);
		mapping_set_release_always(inode->i_mapping);
		break;
	default:
		dump_vnode(vnode, op->file[0].vnode != vnode ? op->file[0].vnode : NULL);
		write_sequnlock(&vnode->cb_lock);
		return afs_protocol_error(NULL, afs_eproto_file_type);
	}

	afs_set_i_size(vnode, status->size);
	afs_set_netfs_context(vnode);

	vnode->invalid_before	= status->data_version;
	trace_afs_set_dv(vnode, status->data_version);
	inode_set_iversion_raw(&vnode->netfs.inode, status->data_version);

	if (!vp->scb.have_cb) {
		/* it's a symlink we just created (the fileserver
		 * didn't give us a callback) */
		afs_clear_cb_promise(vnode, afs_cb_promise_set_new_symlink);
	} else {
		vnode->cb_server = op->server;
		afs_set_cb_promise(vnode, vp->scb.callback.expires_at,
				   afs_cb_promise_set_new_inode);
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
	bool unexpected_jump = false;
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
	inode_set_mtime_to_ts(inode, t);
	if (vp->update_ctime)
		inode_set_ctime_to_ts(inode, op->ctime);

	if (vnode->status.data_version != status->data_version) {
		trace_afs_set_dv(vnode, status->data_version);
		data_changed = true;
	}

	vnode->status = *status;

	if (vp->dv_before + vp->dv_delta != status->data_version) {
		trace_afs_dv_mismatch(vnode, vp->dv_before, vp->dv_delta,
				      status->data_version);

		if (vnode->cb_ro_snapshot == atomic_read(&vnode->volume->cb_ro_snapshot) &&
		    atomic64_read(&vnode->cb_expires_at) != AFS_NO_CB_PROMISE)
			pr_warn("kAFS: vnode modified {%llx:%llu} %llx->%llx %s (op=%x)\n",
				vnode->fid.vid, vnode->fid.vnode,
				(unsigned long long)vp->dv_before + vp->dv_delta,
				(unsigned long long)status->data_version,
				op->type ? op->type->name : "???",
				op->debug_id);

		vnode->invalid_before = status->data_version;
		if (vnode->status.type == AFS_FTYPE_DIR)
			afs_invalidate_dir(vnode, afs_dir_invalid_dv_mismatch);
		else
			set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
		change_size = true;
		data_changed = true;
		unexpected_jump = true;
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
		if (change_size || status->size > i_size_read(inode)) {
			afs_set_i_size(vnode, status->size);
			if (unexpected_jump)
				vnode->netfs.zero_point = status->size;
			inode_set_ctime_to_ts(inode, t);
			inode_set_atime_to_ts(inode, t);
		}
		if (op->ops == &afs_fetch_data_operation)
			op->fetch.subreq->rreq->i_size = status->size;
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
		if (op->volume->type == AFSVL_RWVOL)
			vnode->cb_server = op->server;
		afs_set_cb_promise(vnode, cb->expires_at, afs_cb_promise_set_apply_cb);
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
		afs_op_set_error(op, ret);
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

	if (vnode->status.type != AFS_FTYPE_FILE &&
	    vnode->status.type != AFS_FTYPE_DIR &&
	    vnode->status.type != AFS_FTYPE_SYMLINK) {
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
	vnode->fid.vid		= as->volume->vid;
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
	vnode->cb_v_check = atomic_read(&as->volume->cb_v_break);
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
 * read the attributes of an inode
 */
int afs_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	struct key *key;
	int ret, seq;

	_enter("{ ino=%lu v=%u }", inode->i_ino, inode->i_generation);

	if (vnode->volume &&
	    !(query_flags & AT_STATX_DONT_SYNC) &&
	    atomic64_read(&vnode->cb_expires_at) == AFS_NO_CB_PROMISE) {
		key = afs_request_key(vnode->volume->cell);
		if (IS_ERR(key))
			return PTR_ERR(key);
		ret = afs_validate(vnode, key);
		key_put(key);
		if (ret < 0)
			return ret;
	}

	do {
		seq = read_seqbegin(&vnode->cb_lock);
		generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
		if (test_bit(AFS_VNODE_SILLY_DELETED, &vnode->flags) &&
		    stat->nlink > 0)
			stat->nlink -= 1;

		/* Lie about the size of directories.  We maintain a locally
		 * edited copy and may make different allocation decisions on
		 * it, but we need to give userspace the server's size.
		 */
		if (S_ISDIR(inode->i_mode))
			stat->size = vnode->netfs.remote_i_size;
	} while (read_seqretry(&vnode->cb_lock, seq));

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
	struct afs_super_info *sbi = AFS_FS_S(inode->i_sb);
	struct afs_vnode *vnode = AFS_FS_I(inode);

	_enter("{%llx:%llu.%d}",
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	_debug("CLEAR INODE %p", inode);

	ASSERTCMP(inode->i_ino, ==, vnode->fid.vnode);

	if ((S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)) &&
	    (inode->i_state & I_DIRTY) &&
	    !sbi->dyn_root) {
		struct writeback_control wbc = {
			.sync_mode = WB_SYNC_ALL,
			.for_sync = true,
			.range_end = LLONG_MAX,
		};

		afs_single_writepages(inode->i_mapping, &wbc);
	}

	netfs_wait_for_outstanding_io(inode);
	truncate_inode_pages_final(&inode->i_data);
	netfs_free_folioq_buffer(vnode->directory);

	afs_set_cache_aux(vnode, &aux);
	netfs_clear_inode_writeback(inode, &aux);
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
	struct afs_vnode *vnode = vp->vnode;
	struct inode *inode = &vnode->netfs.inode;

	if (op->setattr.attr->ia_valid & ATTR_SIZE) {
		loff_t size = op->setattr.attr->ia_size;
		loff_t old = op->setattr.old_i_size;

		/* Note: inode->i_size was updated by afs_apply_status() inside
		 * the I/O and callback locks.
		 */

		if (size != old) {
			truncate_pagecache(inode, size);
			netfs_resize_file(&vnode->netfs, size, true);
			fscache_resize_cookie(afs_vnode_cache(vnode), size);
		}
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
		    attr->ia_size > vnode->netfs.remote_i_size) {
			truncate_setsize(inode, attr->ia_size);
			netfs_resize_file(&vnode->netfs, size, false);
			fscache_resize_cookie(afs_vnode_cache(vnode),
					      attr->ia_size);
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
