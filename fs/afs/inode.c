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
	.listxattr	= afs_listxattr,
};

static noinline void dump_vnode(struct afs_vnode *vnode, struct afs_vnode *parent_vnode)
{
	static unsigned long once_only;

	pr_warn("kAFS: AFS vnode with undefined type %u\n",
		vnode->status.type);
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
 * Initialise an inode from the vnode status.
 */
static int afs_inode_init_from_status(struct afs_vnode *vnode, struct key *key,
				      struct afs_cb_interest *cbi,
				      struct afs_vnode *parent_vnode,
				      struct afs_status_cb *scb)
{
	struct afs_cb_interest *old_cbi = NULL;
	struct afs_file_status *status = &scb->status;
	struct inode *inode = AFS_VNODE_TO_I(vnode);
	struct timespec64 t;

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       status->type,
	       status->nlink,
	       (unsigned long long) status->size,
	       status->data_version,
	       status->mode);

	write_seqlock(&vnode->cb_lock);

	vnode->status = *status;

	t = status->mtime_client;
	inode->i_ctime = t;
	inode->i_mtime = t;
	inode->i_atime = t;
	inode->i_uid = make_kuid(&init_user_ns, status->owner);
	inode->i_gid = make_kgid(&init_user_ns, status->group);
	set_nlink(&vnode->vfs_inode, status->nlink);

	switch (status->type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | status->mode;
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &afs_file_operations;
		inode->i_mapping->a_ops	= &afs_fs_aops;
		break;
	case AFS_FTYPE_DIR:
		inode->i_mode	= S_IFDIR | status->mode;
		inode->i_op	= &afs_dir_inode_operations;
		inode->i_fop	= &afs_dir_file_operations;
		inode->i_mapping->a_ops	= &afs_dir_aops;
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((status->mode & 0777) == 0644) {
			inode->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);

			inode->i_mode	= S_IFDIR | 0555;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
			inode->i_mapping->a_ops	= &afs_fs_aops;
		} else {
			inode->i_mode	= S_IFLNK | status->mode;
			inode->i_op	= &afs_symlink_inode_operations;
			inode->i_mapping->a_ops	= &afs_fs_aops;
		}
		inode_nohighmem(inode);
		break;
	default:
		dump_vnode(vnode, parent_vnode);
		write_sequnlock(&vnode->cb_lock);
		return afs_protocol_error(NULL, -EBADMSG, afs_eproto_file_type);
	}

	/*
	 * Estimate 512 bytes  blocks used, rounded up to nearest 1K
	 * for consistency with other AFS clients.
	 */
	inode->i_blocks		= ((i_size_read(inode) + 1023) >> 10) << 1;
	i_size_write(&vnode->vfs_inode, status->size);

	vnode->invalid_before	= status->data_version;
	inode_set_iversion_raw(&vnode->vfs_inode, status->data_version);

	if (!scb->have_cb) {
		/* it's a symlink we just created (the fileserver
		 * didn't give us a callback) */
		vnode->cb_expires_at = ktime_get_real_seconds();
	} else {
		vnode->cb_expires_at = scb->callback.expires_at;
		old_cbi = rcu_dereference_protected(vnode->cb_interest,
						    lockdep_is_held(&vnode->cb_lock.lock));
		if (cbi != old_cbi)
			rcu_assign_pointer(vnode->cb_interest, afs_get_cb_interest(cbi));
		else
			old_cbi = NULL;
		set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
	}

	write_sequnlock(&vnode->cb_lock);
	afs_put_cb_interest(afs_v2net(vnode), old_cbi);
	return 0;
}

/*
 * Update the core inode struct from a returned status record.
 */
static void afs_apply_status(struct afs_fs_cursor *fc,
			     struct afs_vnode *vnode,
			     struct afs_status_cb *scb,
			     const afs_dataversion_t *expected_version)
{
	struct afs_file_status *status = &scb->status;
	struct timespec64 t;
	umode_t mode;
	bool data_changed = false;

	BUG_ON(test_bit(AFS_VNODE_UNSET, &vnode->flags));

	if (status->type != vnode->status.type) {
		pr_warning("Vnode %llx:%llx:%x changed type %u to %u\n",
			   vnode->fid.vid,
			   vnode->fid.vnode,
			   vnode->fid.unique,
			   status->type, vnode->status.type);
		afs_protocol_error(NULL, -EBADMSG, afs_eproto_bad_status);
		return;
	}

	if (status->nlink != vnode->status.nlink)
		set_nlink(&vnode->vfs_inode, status->nlink);

	if (status->owner != vnode->status.owner)
		vnode->vfs_inode.i_uid = make_kuid(&init_user_ns, status->owner);

	if (status->group != vnode->status.group)
		vnode->vfs_inode.i_gid = make_kgid(&init_user_ns, status->group);

	if (status->mode != vnode->status.mode) {
		mode = vnode->vfs_inode.i_mode;
		mode &= ~S_IALLUGO;
		mode |= status->mode;
		WRITE_ONCE(vnode->vfs_inode.i_mode, mode);
	}

	t = status->mtime_client;
	vnode->vfs_inode.i_ctime = t;
	vnode->vfs_inode.i_mtime = t;
	vnode->vfs_inode.i_atime = t;

	if (vnode->status.data_version != status->data_version)
		data_changed = true;

	vnode->status = *status;

	if (expected_version &&
	    *expected_version != status->data_version) {
		kdebug("vnode modified %llx on {%llx:%llu} [exp %llx] %s",
		       (unsigned long long) status->data_version,
		       vnode->fid.vid, vnode->fid.vnode,
		       (unsigned long long) *expected_version,
		       fc->type ? fc->type->name : "???");
		vnode->invalid_before = status->data_version;
		if (vnode->status.type == AFS_FTYPE_DIR) {
			if (test_and_clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
				afs_stat_v(vnode, n_inval);
		} else {
			set_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
		}
	} else if (vnode->status.type == AFS_FTYPE_DIR) {
		/* Expected directory change is handled elsewhere so
		 * that we can locally edit the directory and save on a
		 * download.
		 */
		if (test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			data_changed = false;
	}

	if (data_changed) {
		inode_set_iversion_raw(&vnode->vfs_inode, status->data_version);
		i_size_write(&vnode->vfs_inode, status->size);
	}
}

/*
 * Apply a callback to a vnode.
 */
static void afs_apply_callback(struct afs_fs_cursor *fc,
			       struct afs_vnode *vnode,
			       struct afs_status_cb *scb,
			       unsigned int cb_break)
{
	struct afs_cb_interest *old;
	struct afs_callback *cb = &scb->callback;

	if (!afs_cb_is_broken(cb_break, vnode, fc->cbi)) {
		vnode->cb_expires_at	= cb->expires_at;
		old = rcu_dereference_protected(vnode->cb_interest,
						lockdep_is_held(&vnode->cb_lock.lock));
		if (old != fc->cbi) {
			rcu_assign_pointer(vnode->cb_interest, afs_get_cb_interest(fc->cbi));
			afs_put_cb_interest(afs_v2net(vnode), old);
		}
		set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
	}
}

/*
 * Apply the received status and callback to an inode all in the same critical
 * section to avoid races with afs_validate().
 */
void afs_vnode_commit_status(struct afs_fs_cursor *fc,
			     struct afs_vnode *vnode,
			     unsigned int cb_break,
			     const afs_dataversion_t *expected_version,
			     struct afs_status_cb *scb)
{
	if (fc->ac.error != 0)
		return;

	write_seqlock(&vnode->cb_lock);

	if (scb->have_error) {
		if (scb->status.abort_code == VNOVNODE) {
			set_bit(AFS_VNODE_DELETED, &vnode->flags);
			clear_nlink(&vnode->vfs_inode);
			__afs_break_callback(vnode);
		}
	} else {
		if (scb->have_status)
			afs_apply_status(fc, vnode, scb, expected_version);
		if (scb->have_cb)
			afs_apply_callback(fc, vnode, scb, cb_break);
	}

	write_sequnlock(&vnode->cb_lock);

	if (fc->ac.error == 0 && scb->have_status)
		afs_cache_permit(vnode, fc->key, cb_break, scb);
}

/*
 * Fetch file status from the volume.
 */
int afs_fetch_status(struct afs_vnode *vnode, struct key *key, bool is_new,
		     afs_access_t *_caller_access)
{
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s,{%llx:%llu.%u,S=%lx}",
	       vnode->volume->name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
	       vnode->flags);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, true)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_fetch_file_status(&fc, scb, NULL);
		}

		if (fc.error) {
			/* Do nothing. */
		} else if (is_new) {
			ret = afs_inode_init_from_status(vnode, key, fc.cbi,
							 NULL, scb);
			fc.error = ret;
			if (ret == 0)
				afs_cache_permit(vnode, key, fc.cb_break, scb);
		} else {
			afs_vnode_commit_status(&fc, vnode, fc.cb_break,
						&data_version, scb);
		}
		afs_check_for_remote_deletion(&fc, vnode);
		ret = afs_end_vnode_operation(&fc);
	}

	if (ret == 0 && _caller_access)
		*_caller_access = scb->status.caller_access;
	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * iget5() comparator
 */
int afs_iget5_test(struct inode *inode, void *opaque)
{
	struct afs_iget_data *iget_data = opaque;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	return memcmp(&vnode->fid, &iget_data->fid, sizeof(iget_data->fid)) == 0;
}

/*
 * iget5() comparator for inode created by autocell operations
 *
 * These pseudo inodes don't match anything.
 */
static int afs_iget5_pseudo_dir_test(struct inode *inode, void *opaque)
{
	return 0;
}

/*
 * iget5() inode initialiser
 */
static int afs_iget5_set(struct inode *inode, void *opaque)
{
	struct afs_iget_data *iget_data = opaque;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	vnode->fid		= iget_data->fid;
	vnode->volume		= iget_data->volume;
	vnode->cb_v_break	= iget_data->cb_v_break;
	vnode->cb_s_break	= iget_data->cb_s_break;

	/* YFS supports 96-bit vnode IDs, but Linux only supports
	 * 64-bit inode numbers.
	 */
	inode->i_ino		= iget_data->fid.vnode;
	inode->i_generation	= iget_data->fid.unique;
	return 0;
}

/*
 * Create an inode for a dynamic root directory or an autocell dynamic
 * automount dir.
 */
struct inode *afs_iget_pseudo_dir(struct super_block *sb, bool root)
{
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct inode *inode;
	static atomic_t afs_autocell_ino;

	struct afs_iget_data iget_data = {
		.cb_v_break = 0,
		.cb_s_break = 0,
	};

	_enter("");

	as = sb->s_fs_info;
	if (as->volume) {
		iget_data.volume = as->volume;
		iget_data.fid.vid = as->volume->vid;
	}
	if (root) {
		iget_data.fid.vnode = 1;
		iget_data.fid.unique = 1;
	} else {
		iget_data.fid.vnode = atomic_inc_return(&afs_autocell_ino);
		iget_data.fid.unique = 0;
	}

	inode = iget5_locked(sb, iget_data.fid.vnode,
			     afs_iget5_pseudo_dir_test, afs_iget5_set,
			     &iget_data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { ino=%lu, vl=%llx, vn=%llx, u=%x }",
	       inode, inode->i_ino, iget_data.fid.vid, iget_data.fid.vnode,
	       iget_data.fid.unique);

	vnode = AFS_FS_I(inode);

	/* there shouldn't be an existing inode */
	BUG_ON(!(inode->i_state & I_NEW));

	inode->i_size		= 0;
	inode->i_mode		= S_IFDIR | S_IRUGO | S_IXUGO;
	if (root) {
		inode->i_op	= &afs_dynroot_inode_operations;
		inode->i_fop	= &afs_dynroot_file_operations;
	} else {
		inode->i_op	= &afs_autocell_inode_operations;
	}
	set_nlink(inode, 2);
	inode->i_uid		= GLOBAL_ROOT_UID;
	inode->i_gid		= GLOBAL_ROOT_GID;
	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
	inode->i_blocks		= 0;
	inode_set_iversion_raw(inode, 0);
	inode->i_generation	= 0;

	set_bit(AFS_VNODE_PSEUDODIR, &vnode->flags);
	if (!root) {
		set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);
		inode->i_flags |= S_AUTOMOUNT;
	}

	inode->i_flags |= S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;
}

/*
 * Get a cache cookie for an inode.
 */
static void afs_get_inode_cache(struct afs_vnode *vnode)
{
#ifdef CONFIG_AFS_FSCACHE
	struct {
		u32 vnode_id;
		u32 unique;
		u32 vnode_id_ext[2];	/* Allow for a 96-bit key */
	} __packed key;
	struct afs_vnode_cache_aux aux;

	if (vnode->status.type == AFS_FTYPE_DIR) {
		vnode->cache = NULL;
		return;
	}

	key.vnode_id		= vnode->fid.vnode;
	key.unique		= vnode->fid.unique;
	key.vnode_id_ext[0]	= vnode->fid.vnode >> 32;
	key.vnode_id_ext[1]	= vnode->fid.vnode_hi;
	aux.data_version	= vnode->status.data_version;

	vnode->cache = fscache_acquire_cookie(vnode->volume->cache,
					      &afs_vnode_cache_index_def,
					      &key, sizeof(key),
					      &aux, sizeof(aux),
					      vnode, vnode->status.size, true);
#endif
}

/*
 * inode retrieval
 */
struct inode *afs_iget(struct super_block *sb, struct key *key,
		       struct afs_iget_data *iget_data,
		       struct afs_status_cb *scb,
		       struct afs_cb_interest *cbi,
		       struct afs_vnode *parent_vnode)
{
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct afs_fid *fid = &iget_data->fid;
	struct inode *inode;
	int ret;

	_enter(",{%llx:%llu.%u},,", fid->vid, fid->vnode, fid->unique);

	as = sb->s_fs_info;
	iget_data->volume = as->volume;

	inode = iget5_locked(sb, fid->vnode, afs_iget5_test, afs_iget5_set,
			     iget_data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { vl=%llx vn=%llx, u=%x }",
	       inode, fid->vid, fid->vnode, fid->unique);

	vnode = AFS_FS_I(inode);

	/* deal with an existing inode */
	if (!(inode->i_state & I_NEW)) {
		_leave(" = %p", inode);
		return inode;
	}

	if (!scb) {
		/* it's a remotely extant inode */
		ret = afs_fetch_status(vnode, key, true, NULL);
		if (ret < 0)
			goto bad_inode;
	} else {
		ret = afs_inode_init_from_status(vnode, key, cbi, parent_vnode,
						 scb);
		if (ret < 0)
			goto bad_inode;
	}

	afs_get_inode_cache(vnode);

	/* success */
	clear_bit(AFS_VNODE_UNSET, &vnode->flags);
	inode->i_flags |= S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;

	/* failure */
bad_inode:
	iget_failed(inode);
	_leave(" = %d [bad]", ret);
	return ERR_PTR(ret);
}

/*
 * mark the data attached to an inode as obsolete due to a write on the server
 * - might also want to ditch all the outstanding writes and dirty pages
 */
void afs_zap_data(struct afs_vnode *vnode)
{
	_enter("{%llx:%llu}", vnode->fid.vid, vnode->fid.vnode);

#ifdef CONFIG_AFS_FSCACHE
	fscache_invalidate(vnode->cache);
#endif

	/* nuke all the non-dirty pages that aren't locked, mapped or being
	 * written back in a regular file and completely discard the pages in a
	 * directory or symlink */
	if (S_ISREG(vnode->vfs_inode.i_mode))
		invalidate_remote_inode(&vnode->vfs_inode);
	else
		invalidate_inode_pages2(vnode->vfs_inode.i_mapping);
}

/*
 * Check the validity of a vnode/inode.
 */
bool afs_check_validity(struct afs_vnode *vnode)
{
	struct afs_cb_interest *cbi;
	struct afs_server *server;
	struct afs_volume *volume = vnode->volume;
	time64_t now = ktime_get_real_seconds();
	bool valid, need_clear = false;
	unsigned int cb_break, cb_s_break, cb_v_break;
	int seq = 0;

	do {
		read_seqbegin_or_lock(&vnode->cb_lock, &seq);
		cb_v_break = READ_ONCE(volume->cb_v_break);
		cb_break = vnode->cb_break;

		if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
			cbi = rcu_dereference(vnode->cb_interest);
			server = rcu_dereference(cbi->server);
			cb_s_break = READ_ONCE(server->cb_s_break);

			if (vnode->cb_s_break != cb_s_break ||
			    vnode->cb_v_break != cb_v_break) {
				vnode->cb_s_break = cb_s_break;
				vnode->cb_v_break = cb_v_break;
				need_clear = true;
				valid = false;
			} else if (test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags)) {
				need_clear = true;
				valid = false;
			} else if (vnode->cb_expires_at - 10 <= now) {
				need_clear = true;
				valid = false;
			} else {
				valid = true;
			}
		} else if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
			valid = true;
		} else {
			vnode->cb_v_break = cb_v_break;
			valid = false;
		}

	} while (need_seqretry(&vnode->cb_lock, seq));

	done_seqretry(&vnode->cb_lock, seq);

	if (need_clear) {
		write_seqlock(&vnode->cb_lock);
		if (cb_break == vnode->cb_break)
			__afs_break_callback(vnode);
		write_sequnlock(&vnode->cb_lock);
		valid = false;
	}

	return valid;
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
	bool valid;
	int ret;

	_enter("{v={%llx:%llu} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	rcu_read_lock();
	valid = afs_check_validity(vnode);
	rcu_read_unlock();

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		clear_nlink(&vnode->vfs_inode);

	if (valid)
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
int afs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	int seq = 0;

	_enter("{ ino=%lu v=%u }", inode->i_ino, inode->i_generation);

	do {
		read_seqbegin_or_lock(&vnode->cb_lock, &seq);
		generic_fillattr(inode, stat);
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
	struct afs_cb_interest *cbi;
	struct afs_vnode *vnode;

	vnode = AFS_FS_I(inode);

	_enter("{%llx:%llu.%d}",
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	_debug("CLEAR INODE %p", inode);

	ASSERTCMP(inode->i_ino, ==, vnode->fid.vnode);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	write_seqlock(&vnode->cb_lock);
	cbi = rcu_dereference_protected(vnode->cb_interest,
					lockdep_is_held(&vnode->cb_lock.lock));
	if (cbi) {
		afs_put_cb_interest(afs_i2net(inode), cbi);
		rcu_assign_pointer(vnode->cb_interest, NULL);
	}
	write_sequnlock(&vnode->cb_lock);

	while (!list_empty(&vnode->wb_keys)) {
		struct afs_wb_key *wbk = list_entry(vnode->wb_keys.next,
						    struct afs_wb_key, vnode_link);
		list_del(&wbk->vnode_link);
		afs_put_wb_key(wbk);
	}

#ifdef CONFIG_AFS_FSCACHE
	{
		struct afs_vnode_cache_aux aux;

		aux.data_version = vnode->status.data_version;
		fscache_relinquish_cookie(vnode->cache, &aux,
					  test_bit(AFS_VNODE_DELETED, &vnode->flags));
		vnode->cache = NULL;
	}
#endif

	afs_prune_wb_keys(vnode);
	afs_put_permits(rcu_access_pointer(vnode->permit_cache));
	key_put(vnode->silly_key);
	vnode->silly_key = NULL;
	key_put(vnode->lock_key);
	vnode->lock_key = NULL;
	_leave("");
}

/*
 * set the attributes of an inode
 */
int afs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct afs_fs_cursor fc;
	struct afs_status_cb *scb;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	struct key *key;
	int ret = -ENOMEM;

	_enter("{%llx:%llu},{n=%pd},%x",
	       vnode->fid.vid, vnode->fid.vnode, dentry,
	       attr->ia_valid);

	if (!(attr->ia_valid & (ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
				ATTR_MTIME))) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		goto error;

	/* flush any dirty data outstanding on a regular file */
	if (S_ISREG(vnode->vfs_inode.i_mode))
		filemap_write_and_wait(vnode->vfs_inode.i_mapping);

	if (attr->ia_valid & ATTR_FILE) {
		key = afs_file_key(attr->ia_file);
	} else {
		key = afs_request_key(vnode->volume->cell);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
			goto error_scb;
		}
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key, false)) {
		afs_dataversion_t data_version = vnode->status.data_version;

		if (attr->ia_valid & ATTR_SIZE)
			data_version++;

		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
			afs_fs_setattr(&fc, attr, scb);
		}

		afs_check_for_remote_deletion(&fc, vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break,
					&data_version, scb);
		ret = afs_end_vnode_operation(&fc);
	}

	if (!(attr->ia_valid & ATTR_FILE))
		key_put(key);

error_scb:
	kfree(scb);
error:
	_leave(" = %d", ret);
	return ret;
}
