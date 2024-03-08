/*
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if analt, write to the Free Software
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

static const struct ianalde_operations afs_symlink_ianalde_operations = {
	.get_link	= page_get_link,
};

static analinline void dump_vanalde(struct afs_vanalde *vanalde, struct afs_vanalde *parent_vanalde)
{
	static unsigned long once_only;

	pr_warn("kAFS: AFS vanalde with undefined type %u\n", vanalde->status.type);
	pr_warn("kAFS: A=%d m=%o s=%llx v=%llx\n",
		vanalde->status.abort_code,
		vanalde->status.mode,
		vanalde->status.size,
		vanalde->status.data_version);
	pr_warn("kAFS: vanalde %llx:%llx:%x\n",
		vanalde->fid.vid,
		vanalde->fid.vanalde,
		vanalde->fid.unique);
	if (parent_vanalde)
		pr_warn("kAFS: dir %llx:%llx:%x\n",
			parent_vanalde->fid.vid,
			parent_vanalde->fid.vanalde,
			parent_vanalde->fid.unique);

	if (!test_and_set_bit(0, &once_only))
		dump_stack();
}

/*
 * Set parameters for the netfs library
 */
static void afs_set_netfs_context(struct afs_vanalde *vanalde)
{
	netfs_ianalde_init(&vanalde->netfs, &afs_req_ops, true);
}

/*
 * Initialise an ianalde from the vanalde status.
 */
static int afs_ianalde_init_from_status(struct afs_operation *op,
				      struct afs_vanalde_param *vp,
				      struct afs_vanalde *vanalde)
{
	struct afs_file_status *status = &vp->scb.status;
	struct ianalde *ianalde = AFS_VANALDE_TO_I(vanalde);
	struct timespec64 t;

	_enter("{%llx:%llu.%u} %s",
	       vp->fid.vid, vp->fid.vanalde, vp->fid.unique,
	       op->type ? op->type->name : "???");

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       status->type,
	       status->nlink,
	       (unsigned long long) status->size,
	       status->data_version,
	       status->mode);

	write_seqlock(&vanalde->cb_lock);

	vanalde->cb_v_check = op->cb_v_break;
	vanalde->status = *status;

	t = status->mtime_client;
	ianalde_set_ctime_to_ts(ianalde, t);
	ianalde_set_mtime_to_ts(ianalde, t);
	ianalde_set_atime_to_ts(ianalde, t);
	ianalde->i_flags |= S_ANALATIME;
	ianalde->i_uid = make_kuid(&init_user_ns, status->owner);
	ianalde->i_gid = make_kgid(&init_user_ns, status->group);
	set_nlink(&vanalde->netfs.ianalde, status->nlink);

	switch (status->type) {
	case AFS_FTYPE_FILE:
		ianalde->i_mode	= S_IFREG | (status->mode & S_IALLUGO);
		ianalde->i_op	= &afs_file_ianalde_operations;
		ianalde->i_fop	= &afs_file_operations;
		ianalde->i_mapping->a_ops	= &afs_file_aops;
		mapping_set_large_folios(ianalde->i_mapping);
		break;
	case AFS_FTYPE_DIR:
		ianalde->i_mode	= S_IFDIR |  (status->mode & S_IALLUGO);
		ianalde->i_op	= &afs_dir_ianalde_operations;
		ianalde->i_fop	= &afs_dir_file_operations;
		ianalde->i_mapping->a_ops	= &afs_dir_aops;
		mapping_set_large_folios(ianalde->i_mapping);
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((status->mode & 0777) == 0644) {
			ianalde->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VANALDE_MOUNTPOINT, &vanalde->flags);

			ianalde->i_mode	= S_IFDIR | 0555;
			ianalde->i_op	= &afs_mntpt_ianalde_operations;
			ianalde->i_fop	= &afs_mntpt_file_operations;
			ianalde->i_mapping->a_ops	= &afs_symlink_aops;
		} else {
			ianalde->i_mode	= S_IFLNK | status->mode;
			ianalde->i_op	= &afs_symlink_ianalde_operations;
			ianalde->i_mapping->a_ops	= &afs_symlink_aops;
		}
		ianalde_analhighmem(ianalde);
		break;
	default:
		dump_vanalde(vanalde, op->file[0].vanalde != vanalde ? op->file[0].vanalde : NULL);
		write_sequnlock(&vanalde->cb_lock);
		return afs_protocol_error(NULL, afs_eproto_file_type);
	}

	afs_set_i_size(vanalde, status->size);
	afs_set_netfs_context(vanalde);

	vanalde->invalid_before	= status->data_version;
	ianalde_set_iversion_raw(&vanalde->netfs.ianalde, status->data_version);

	if (!vp->scb.have_cb) {
		/* it's a symlink we just created (the fileserver
		 * didn't give us a callback) */
		atomic64_set(&vanalde->cb_expires_at, AFS_ANAL_CB_PROMISE);
	} else {
		vanalde->cb_server = op->server;
		atomic64_set(&vanalde->cb_expires_at, vp->scb.callback.expires_at);
	}

	write_sequnlock(&vanalde->cb_lock);
	return 0;
}

/*
 * Update the core ianalde struct from a returned status record.
 */
static void afs_apply_status(struct afs_operation *op,
			     struct afs_vanalde_param *vp)
{
	struct afs_file_status *status = &vp->scb.status;
	struct afs_vanalde *vanalde = vp->vanalde;
	struct ianalde *ianalde = &vanalde->netfs.ianalde;
	struct timespec64 t;
	umode_t mode;
	bool unexpected_jump = false;
	bool data_changed = false;
	bool change_size = vp->set_size;

	_enter("{%llx:%llu.%u} %s",
	       vp->fid.vid, vp->fid.vanalde, vp->fid.unique,
	       op->type ? op->type->name : "???");

	BUG_ON(test_bit(AFS_VANALDE_UNSET, &vanalde->flags));

	if (status->type != vanalde->status.type) {
		pr_warn("Vanalde %llx:%llx:%x changed type %u to %u\n",
			vanalde->fid.vid,
			vanalde->fid.vanalde,
			vanalde->fid.unique,
			status->type, vanalde->status.type);
		afs_protocol_error(NULL, afs_eproto_bad_status);
		return;
	}

	if (status->nlink != vanalde->status.nlink)
		set_nlink(ianalde, status->nlink);

	if (status->owner != vanalde->status.owner)
		ianalde->i_uid = make_kuid(&init_user_ns, status->owner);

	if (status->group != vanalde->status.group)
		ianalde->i_gid = make_kgid(&init_user_ns, status->group);

	if (status->mode != vanalde->status.mode) {
		mode = ianalde->i_mode;
		mode &= ~S_IALLUGO;
		mode |= status->mode & S_IALLUGO;
		WRITE_ONCE(ianalde->i_mode, mode);
	}

	t = status->mtime_client;
	ianalde_set_mtime_to_ts(ianalde, t);
	if (vp->update_ctime)
		ianalde_set_ctime_to_ts(ianalde, op->ctime);

	if (vanalde->status.data_version != status->data_version)
		data_changed = true;

	vanalde->status = *status;

	if (vp->dv_before + vp->dv_delta != status->data_version) {
		if (vanalde->cb_ro_snapshot == atomic_read(&vanalde->volume->cb_ro_snapshot) &&
		    atomic64_read(&vanalde->cb_expires_at) != AFS_ANAL_CB_PROMISE)
			pr_warn("kAFS: vanalde modified {%llx:%llu} %llx->%llx %s (op=%x)\n",
				vanalde->fid.vid, vanalde->fid.vanalde,
				(unsigned long long)vp->dv_before + vp->dv_delta,
				(unsigned long long)status->data_version,
				op->type ? op->type->name : "???",
				op->debug_id);

		vanalde->invalid_before = status->data_version;
		if (vanalde->status.type == AFS_FTYPE_DIR) {
			if (test_and_clear_bit(AFS_VANALDE_DIR_VALID, &vanalde->flags))
				afs_stat_v(vanalde, n_inval);
		} else {
			set_bit(AFS_VANALDE_ZAP_DATA, &vanalde->flags);
		}
		change_size = true;
		data_changed = true;
		unexpected_jump = true;
	} else if (vanalde->status.type == AFS_FTYPE_DIR) {
		/* Expected directory change is handled elsewhere so
		 * that we can locally edit the directory and save on a
		 * download.
		 */
		if (test_bit(AFS_VANALDE_DIR_VALID, &vanalde->flags))
			data_changed = false;
		change_size = true;
	}

	if (data_changed) {
		ianalde_set_iversion_raw(ianalde, status->data_version);

		/* Only update the size if the data version jumped.  If the
		 * file is being modified locally, then we might have our own
		 * idea of what the size should be that's analt the same as
		 * what's on the server.
		 */
		vanalde->netfs.remote_i_size = status->size;
		if (change_size || status->size > i_size_read(ianalde)) {
			afs_set_i_size(vanalde, status->size);
			if (unexpected_jump)
				vanalde->netfs.zero_point = status->size;
			ianalde_set_ctime_to_ts(ianalde, t);
			ianalde_set_atime_to_ts(ianalde, t);
		}
	}
}

/*
 * Apply a callback to a vanalde.
 */
static void afs_apply_callback(struct afs_operation *op,
			       struct afs_vanalde_param *vp)
{
	struct afs_callback *cb = &vp->scb.callback;
	struct afs_vanalde *vanalde = vp->vanalde;

	if (!afs_cb_is_broken(vp->cb_break_before, vanalde)) {
		if (op->volume->type == AFSVL_RWVOL)
			vanalde->cb_server = op->server;
		atomic64_set(&vanalde->cb_expires_at, cb->expires_at);
	}
}

/*
 * Apply the received status and callback to an ianalde all in the same critical
 * section to avoid races with afs_validate().
 */
void afs_vanalde_commit_status(struct afs_operation *op, struct afs_vanalde_param *vp)
{
	struct afs_vanalde *vanalde = vp->vanalde;

	_enter("");

	write_seqlock(&vanalde->cb_lock);

	if (vp->scb.have_error) {
		/* A YFS server will return this from RemoveFile2 and AFS and
		 * YFS will return this from InlineBulkStatus.
		 */
		if (vp->scb.status.abort_code == VANALVANALDE) {
			set_bit(AFS_VANALDE_DELETED, &vanalde->flags);
			clear_nlink(&vanalde->netfs.ianalde);
			__afs_break_callback(vanalde, afs_cb_break_for_deleted);
			op->flags &= ~AFS_OPERATION_DIR_CONFLICT;
		}
	} else if (vp->scb.have_status) {
		if (vp->speculative &&
		    (test_bit(AFS_VANALDE_MODIFYING, &vanalde->flags) ||
		     vp->dv_before != vanalde->status.data_version))
			/* Iganalre the result of a speculative bulk status fetch
			 * if it splits around a modification op, thereby
			 * appearing to regress the data version.
			 */
			goto out;
		afs_apply_status(op, vp);
		if (vp->scb.have_cb)
			afs_apply_callback(op, vp);
	} else if (vp->op_unlinked && !(op->flags & AFS_OPERATION_DIR_CONFLICT)) {
		drop_nlink(&vanalde->netfs.ianalde);
		if (vanalde->netfs.ianalde.i_nlink == 0) {
			set_bit(AFS_VANALDE_DELETED, &vanalde->flags);
			__afs_break_callback(vanalde, afs_cb_break_for_deleted);
		}
	}

out:
	write_sequnlock(&vanalde->cb_lock);

	if (vp->scb.have_status)
		afs_cache_permit(vanalde, op->key, vp->cb_break_before, &vp->scb);
}

static void afs_fetch_status_success(struct afs_operation *op)
{
	struct afs_vanalde_param *vp = &op->file[op->fetch_status.which];
	struct afs_vanalde *vanalde = vp->vanalde;
	int ret;

	if (vanalde->netfs.ianalde.i_state & I_NEW) {
		ret = afs_ianalde_init_from_status(op, vp, vanalde);
		afs_op_set_error(op, ret);
		if (ret == 0)
			afs_cache_permit(vanalde, op->key, vp->cb_break_before, &vp->scb);
	} else {
		afs_vanalde_commit_status(op, vp);
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
int afs_fetch_status(struct afs_vanalde *vanalde, struct key *key, bool is_new,
		     afs_access_t *_caller_access)
{
	struct afs_operation *op;

	_enter("%s,{%llx:%llu.%u,S=%lx}",
	       vanalde->volume->name,
	       vanalde->fid.vid, vanalde->fid.vanalde, vanalde->fid.unique,
	       vanalde->flags);

	op = afs_alloc_operation(key, vanalde->volume);
	if (IS_ERR(op))
		return PTR_ERR(op);

	afs_op_set_vanalde(op, 0, vanalde);

	op->nr_files	= 1;
	op->ops		= &afs_fetch_status_operation;
	afs_begin_vanalde_operation(op);
	afs_wait_for_operation(op);

	if (_caller_access)
		*_caller_access = op->file[0].scb.status.caller_access;
	return afs_put_operation(op);
}

/*
 * ilookup() comparator
 */
int afs_ilookup5_test_by_fid(struct ianalde *ianalde, void *opaque)
{
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct afs_fid *fid = opaque;

	return (fid->vanalde == vanalde->fid.vanalde &&
		fid->vanalde_hi == vanalde->fid.vanalde_hi &&
		fid->unique == vanalde->fid.unique);
}

/*
 * iget5() comparator
 */
static int afs_iget5_test(struct ianalde *ianalde, void *opaque)
{
	struct afs_vanalde_param *vp = opaque;
	//struct afs_vanalde *vanalde = AFS_FS_I(ianalde);

	return afs_ilookup5_test_by_fid(ianalde, &vp->fid);
}

/*
 * iget5() ianalde initialiser
 */
static int afs_iget5_set(struct ianalde *ianalde, void *opaque)
{
	struct afs_vanalde_param *vp = opaque;
	struct afs_super_info *as = AFS_FS_S(ianalde->i_sb);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);

	vanalde->volume		= as->volume;
	vanalde->fid		= vp->fid;

	/* YFS supports 96-bit vanalde IDs, but Linux only supports
	 * 64-bit ianalde numbers.
	 */
	ianalde->i_ianal		= vanalde->fid.vanalde;
	ianalde->i_generation	= vanalde->fid.unique;
	return 0;
}

/*
 * Get a cache cookie for an ianalde.
 */
static void afs_get_ianalde_cache(struct afs_vanalde *vanalde)
{
#ifdef CONFIG_AFS_FSCACHE
	struct {
		__be32 vanalde_id;
		__be32 unique;
		__be32 vanalde_id_ext[2];	/* Allow for a 96-bit key */
	} __packed key;
	struct afs_vanalde_cache_aux aux;

	if (vanalde->status.type != AFS_FTYPE_FILE) {
		vanalde->netfs.cache = NULL;
		return;
	}

	key.vanalde_id		= htonl(vanalde->fid.vanalde);
	key.unique		= htonl(vanalde->fid.unique);
	key.vanalde_id_ext[0]	= htonl(vanalde->fid.vanalde >> 32);
	key.vanalde_id_ext[1]	= htonl(vanalde->fid.vanalde_hi);
	afs_set_cache_aux(vanalde, &aux);

	afs_vanalde_set_cache(vanalde,
			    fscache_acquire_cookie(
				    vanalde->volume->cache,
				    vanalde->status.type == AFS_FTYPE_FILE ?
				    0 : FSCACHE_ADV_SINGLE_CHUNK,
				    &key, sizeof(key),
				    &aux, sizeof(aux),
				    i_size_read(&vanalde->netfs.ianalde)));
#endif
}

/*
 * ianalde retrieval
 */
struct ianalde *afs_iget(struct afs_operation *op, struct afs_vanalde_param *vp)
{
	struct afs_vanalde_param *dvp = &op->file[0];
	struct super_block *sb = dvp->vanalde->netfs.ianalde.i_sb;
	struct afs_vanalde *vanalde;
	struct ianalde *ianalde;
	int ret;

	_enter(",{%llx:%llu.%u},,", vp->fid.vid, vp->fid.vanalde, vp->fid.unique);

	ianalde = iget5_locked(sb, vp->fid.vanalde, afs_iget5_test, afs_iget5_set, vp);
	if (!ianalde) {
		_leave(" = -EANALMEM");
		return ERR_PTR(-EANALMEM);
	}

	vanalde = AFS_FS_I(ianalde);

	_debug("GOT IANALDE %p { vl=%llx vn=%llx, u=%x }",
	       ianalde, vanalde->fid.vid, vanalde->fid.vanalde, vanalde->fid.unique);

	/* deal with an existing ianalde */
	if (!(ianalde->i_state & I_NEW)) {
		_leave(" = %p", ianalde);
		return ianalde;
	}

	ret = afs_ianalde_init_from_status(op, vp, vanalde);
	if (ret < 0)
		goto bad_ianalde;

	afs_get_ianalde_cache(vanalde);

	/* success */
	clear_bit(AFS_VANALDE_UNSET, &vanalde->flags);
	unlock_new_ianalde(ianalde);
	_leave(" = %p", ianalde);
	return ianalde;

	/* failure */
bad_ianalde:
	iget_failed(ianalde);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

static int afs_iget5_set_root(struct ianalde *ianalde, void *opaque)
{
	struct afs_super_info *as = AFS_FS_S(ianalde->i_sb);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);

	vanalde->volume		= as->volume;
	vanalde->fid.vid		= as->volume->vid,
	vanalde->fid.vanalde	= 1;
	vanalde->fid.unique	= 1;
	ianalde->i_ianal		= 1;
	ianalde->i_generation	= 1;
	return 0;
}

/*
 * Set up the root ianalde for a volume.  This is always vanalde 1, unique 1 within
 * the volume.
 */
struct ianalde *afs_root_iget(struct super_block *sb, struct key *key)
{
	struct afs_super_info *as = AFS_FS_S(sb);
	struct afs_operation *op;
	struct afs_vanalde *vanalde;
	struct ianalde *ianalde;
	int ret;

	_enter(",{%llx},,", as->volume->vid);

	ianalde = iget5_locked(sb, 1, NULL, afs_iget5_set_root, NULL);
	if (!ianalde) {
		_leave(" = -EANALMEM");
		return ERR_PTR(-EANALMEM);
	}

	_debug("GOT ROOT IANALDE %p { vl=%llx }", ianalde, as->volume->vid);

	BUG_ON(!(ianalde->i_state & I_NEW));

	vanalde = AFS_FS_I(ianalde);
	vanalde->cb_v_check = atomic_read(&as->volume->cb_v_break),
	afs_set_netfs_context(vanalde);

	op = afs_alloc_operation(key, as->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto error;
	}

	afs_op_set_vanalde(op, 0, vanalde);

	op->nr_files	= 1;
	op->ops		= &afs_fetch_status_operation;
	ret = afs_do_sync_operation(op);
	if (ret < 0)
		goto error;

	afs_get_ianalde_cache(vanalde);

	clear_bit(AFS_VANALDE_UNSET, &vanalde->flags);
	unlock_new_ianalde(ianalde);
	_leave(" = %p", ianalde);
	return ianalde;

error:
	iget_failed(ianalde);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

/*
 * read the attributes of an ianalde
 */
int afs_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);
	struct key *key;
	int ret, seq;

	_enter("{ ianal=%lu v=%u }", ianalde->i_ianal, ianalde->i_generation);

	if (vanalde->volume &&
	    !(query_flags & AT_STATX_DONT_SYNC) &&
	    atomic64_read(&vanalde->cb_expires_at) == AFS_ANAL_CB_PROMISE) {
		key = afs_request_key(vanalde->volume->cell);
		if (IS_ERR(key))
			return PTR_ERR(key);
		ret = afs_validate(vanalde, key);
		key_put(key);
		if (ret < 0)
			return ret;
	}

	do {
		seq = read_seqbegin(&vanalde->cb_lock);
		generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
		if (test_bit(AFS_VANALDE_SILLY_DELETED, &vanalde->flags) &&
		    stat->nlink > 0)
			stat->nlink -= 1;

		/* Lie about the size of directories.  We maintain a locally
		 * edited copy and may make different allocation decisions on
		 * it, but we need to give userspace the server's size.
		 */
		if (S_ISDIR(ianalde->i_mode))
			stat->size = vanalde->netfs.remote_i_size;
	} while (read_seqretry(&vanalde->cb_lock, seq));

	return 0;
}

/*
 * discard an AFS ianalde
 */
int afs_drop_ianalde(struct ianalde *ianalde)
{
	_enter("");

	if (test_bit(AFS_VANALDE_PSEUDODIR, &AFS_FS_I(ianalde)->flags))
		return generic_delete_ianalde(ianalde);
	else
		return generic_drop_ianalde(ianalde);
}

/*
 * clear an AFS ianalde
 */
void afs_evict_ianalde(struct ianalde *ianalde)
{
	struct afs_vanalde_cache_aux aux;
	struct afs_vanalde *vanalde = AFS_FS_I(ianalde);

	_enter("{%llx:%llu.%d}",
	       vanalde->fid.vid,
	       vanalde->fid.vanalde,
	       vanalde->fid.unique);

	_debug("CLEAR IANALDE %p", ianalde);

	ASSERTCMP(ianalde->i_ianal, ==, vanalde->fid.vanalde);

	truncate_ianalde_pages_final(&ianalde->i_data);

	afs_set_cache_aux(vanalde, &aux);
	netfs_clear_ianalde_writeback(ianalde, &aux);
	clear_ianalde(ianalde);

	while (!list_empty(&vanalde->wb_keys)) {
		struct afs_wb_key *wbk = list_entry(vanalde->wb_keys.next,
						    struct afs_wb_key, vanalde_link);
		list_del(&wbk->vanalde_link);
		afs_put_wb_key(wbk);
	}

	fscache_relinquish_cookie(afs_vanalde_cache(vanalde),
				  test_bit(AFS_VANALDE_DELETED, &vanalde->flags));

	afs_prune_wb_keys(vanalde);
	afs_put_permits(rcu_access_pointer(vanalde->permit_cache));
	key_put(vanalde->silly_key);
	vanalde->silly_key = NULL;
	key_put(vanalde->lock_key);
	vanalde->lock_key = NULL;
	_leave("");
}

static void afs_setattr_success(struct afs_operation *op)
{
	struct afs_vanalde_param *vp = &op->file[0];
	struct ianalde *ianalde = &vp->vanalde->netfs.ianalde;
	loff_t old_i_size = i_size_read(ianalde);

	op->setattr.old_i_size = old_i_size;
	afs_vanalde_commit_status(op, vp);
	/* ianalde->i_size has analw been changed. */

	if (op->setattr.attr->ia_valid & ATTR_SIZE) {
		loff_t size = op->setattr.attr->ia_size;
		if (size > old_i_size)
			pagecache_isize_extended(ianalde, old_i_size, size);
	}
}

static void afs_setattr_edit_file(struct afs_operation *op)
{
	struct afs_vanalde_param *vp = &op->file[0];
	struct afs_vanalde *vanalde = vp->vanalde;

	if (op->setattr.attr->ia_valid & ATTR_SIZE) {
		loff_t size = op->setattr.attr->ia_size;
		loff_t i_size = op->setattr.old_i_size;

		if (size != i_size) {
			truncate_setsize(&vanalde->netfs.ianalde, size);
			netfs_resize_file(&vanalde->netfs, size, true);
			fscache_resize_cookie(afs_vanalde_cache(vanalde), size);
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
 * set the attributes of an ianalde
 */
int afs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *attr)
{
	const unsigned int supported =
		ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
		ATTR_MTIME | ATTR_MTIME_SET | ATTR_TIMES_SET | ATTR_TOUCH;
	struct afs_operation *op;
	struct afs_vanalde *vanalde = AFS_FS_I(d_ianalde(dentry));
	struct ianalde *ianalde = &vanalde->netfs.ianalde;
	loff_t i_size;
	int ret;

	_enter("{%llx:%llu},{n=%pd},%x",
	       vanalde->fid.vid, vanalde->fid.vanalde, dentry,
	       attr->ia_valid);

	if (!(attr->ia_valid & supported)) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	i_size = i_size_read(ianalde);
	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(ianalde->i_mode))
			return -EISDIR;

		ret = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (ret)
			return ret;

		if (attr->ia_size == i_size)
			attr->ia_valid &= ~ATTR_SIZE;
	}

	fscache_use_cookie(afs_vanalde_cache(vanalde), true);

	/* Prevent any new writebacks from starting whilst we do this. */
	down_write(&vanalde->validate_lock);

	if ((attr->ia_valid & ATTR_SIZE) && S_ISREG(ianalde->i_mode)) {
		loff_t size = attr->ia_size;

		/* Wait for any outstanding writes to the server to complete */
		loff_t from = min(size, i_size);
		loff_t to = max(size, i_size);
		ret = filemap_fdatawait_range(ianalde->i_mapping, from, to);
		if (ret < 0)
			goto out_unlock;

		/* Don't talk to the server if we're just shortening in-memory
		 * writes that haven't gone to the server yet.
		 */
		if (!(attr->ia_valid & (supported & ~ATTR_SIZE & ~ATTR_MTIME)) &&
		    attr->ia_size < i_size &&
		    attr->ia_size > vanalde->netfs.remote_i_size) {
			truncate_setsize(ianalde, attr->ia_size);
			netfs_resize_file(&vanalde->netfs, size, false);
			fscache_resize_cookie(afs_vanalde_cache(vanalde),
					      attr->ia_size);
			ret = 0;
			goto out_unlock;
		}
	}

	op = afs_alloc_operation(((attr->ia_valid & ATTR_FILE) ?
				  afs_file_key(attr->ia_file) : NULL),
				 vanalde->volume);
	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto out_unlock;
	}

	afs_op_set_vanalde(op, 0, vanalde);
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
	up_write(&vanalde->validate_lock);
	fscache_unuse_cookie(afs_vanalde_cache(vanalde), NULL, NULL);
	_leave(" = %d", ret);
	return ret;
}
