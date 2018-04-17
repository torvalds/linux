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

static const struct inode_operations afs_symlink_inode_operations = {
	.get_link	= page_get_link,
	.listxattr	= afs_listxattr,
};

/*
 * Initialise an inode from the vnode status.
 */
static int afs_inode_init_from_status(struct afs_vnode *vnode, struct key *key)
{
	struct inode *inode = AFS_VNODE_TO_I(vnode);

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       vnode->status.type,
	       vnode->status.nlink,
	       (unsigned long long) vnode->status.size,
	       vnode->status.data_version,
	       vnode->status.mode);

	read_seqlock_excl(&vnode->cb_lock);

	afs_update_inode_from_status(vnode, &vnode->status, NULL,
				     AFS_VNODE_NOT_YET_SET);

	switch (vnode->status.type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | vnode->status.mode;
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &afs_file_operations;
		inode->i_mapping->a_ops	= &afs_fs_aops;
		break;
	case AFS_FTYPE_DIR:
		inode->i_mode	= S_IFDIR | vnode->status.mode;
		inode->i_op	= &afs_dir_inode_operations;
		inode->i_fop	= &afs_dir_file_operations;
		inode->i_mapping->a_ops	= &afs_dir_aops;
		break;
	case AFS_FTYPE_SYMLINK:
		/* Symlinks with a mode of 0644 are actually mountpoints. */
		if ((vnode->status.mode & 0777) == 0644) {
			inode->i_flags |= S_AUTOMOUNT;

			set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);

			inode->i_mode	= S_IFDIR | 0555;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
			inode->i_mapping->a_ops	= &afs_fs_aops;
		} else {
			inode->i_mode	= S_IFLNK | vnode->status.mode;
			inode->i_op	= &afs_symlink_inode_operations;
			inode->i_mapping->a_ops	= &afs_fs_aops;
		}
		inode_nohighmem(inode);
		break;
	default:
		printk("kAFS: AFS vnode with undefined type\n");
		read_sequnlock_excl(&vnode->cb_lock);
		return afs_protocol_error(NULL, -EBADMSG);
	}

	inode->i_blocks		= 0;
	vnode->invalid_before	= vnode->status.data_version;

	read_sequnlock_excl(&vnode->cb_lock);
	return 0;
}

/*
 * Fetch file status from the volume.
 */
int afs_fetch_status(struct afs_vnode *vnode, struct key *key, bool new_inode)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s,{%x:%u.%u,S=%lx}",
	       vnode->volume->name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
	       vnode->flags);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = vnode->cb_break + vnode->cb_s_break;
			afs_fs_fetch_file_status(&fc, NULL, new_inode);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * iget5() comparator
 */
int afs_iget5_test(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;

	return inode->i_ino == data->fid.vnode &&
		inode->i_generation == data->fid.unique;
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
	struct afs_iget_data *data = opaque;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	inode->i_ino = data->fid.vnode;
	inode->i_generation = data->fid.unique;
	vnode->fid = data->fid;
	vnode->volume = data->volume;

	return 0;
}

/*
 * Create an inode for a dynamic root directory or an autocell dynamic
 * automount dir.
 */
struct inode *afs_iget_pseudo_dir(struct super_block *sb, bool root)
{
	struct afs_iget_data data;
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct inode *inode;
	static atomic_t afs_autocell_ino;

	_enter("");

	as = sb->s_fs_info;
	if (as->volume) {
		data.volume = as->volume;
		data.fid.vid = as->volume->vid;
	}
	if (root) {
		data.fid.vnode = 1;
		data.fid.unique = 1;
	} else {
		data.fid.vnode = atomic_inc_return(&afs_autocell_ino);
		data.fid.unique = 0;
	}

	inode = iget5_locked(sb, data.fid.vnode,
			     afs_iget5_pseudo_dir_test, afs_iget5_set,
			     &data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { ino=%lu, vl=%x, vn=%x, u=%x }",
	       inode, inode->i_ino, data.fid.vid, data.fid.vnode,
	       data.fid.unique);

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
	inode->i_ctime.tv_sec	= get_seconds();
	inode->i_ctime.tv_nsec	= 0;
	inode->i_atime		= inode->i_mtime = inode->i_ctime;
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
	key.vnode_id_ext[0]	= 0;
	key.vnode_id_ext[1]	= 0;
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
		       struct afs_fid *fid, struct afs_file_status *status,
		       struct afs_callback *cb, struct afs_cb_interest *cbi)
{
	struct afs_iget_data data = { .fid = *fid };
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	_enter(",{%x:%u.%u},,", fid->vid, fid->vnode, fid->unique);

	as = sb->s_fs_info;
	data.volume = as->volume;

	inode = iget5_locked(sb, fid->vnode, afs_iget5_test, afs_iget5_set,
			     &data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return ERR_PTR(-ENOMEM);
	}

	_debug("GOT INODE %p { vl=%x vn=%x, u=%x }",
	       inode, fid->vid, fid->vnode, fid->unique);

	vnode = AFS_FS_I(inode);

	/* deal with an existing inode */
	if (!(inode->i_state & I_NEW)) {
		_leave(" = %p", inode);
		return inode;
	}

	if (!status) {
		/* it's a remotely extant inode */
		ret = afs_fetch_status(vnode, key, true);
		if (ret < 0)
			goto bad_inode;
	} else {
		/* it's an inode we just created */
		memcpy(&vnode->status, status, sizeof(vnode->status));

		if (!cb) {
			/* it's a symlink we just created (the fileserver
			 * didn't give us a callback) */
			vnode->cb_version = 0;
			vnode->cb_type = 0;
			vnode->cb_expires_at = 0;
		} else {
			vnode->cb_version = cb->version;
			vnode->cb_type = cb->type;
			vnode->cb_expires_at = cb->expiry;
			vnode->cb_interest = afs_get_cb_interest(cbi);
			set_bit(AFS_VNODE_CB_PROMISED, &vnode->flags);
		}

		vnode->cb_expires_at += ktime_get_real_seconds();
	}

	ret = afs_inode_init_from_status(vnode, key);
	if (ret < 0)
		goto bad_inode;

	afs_get_inode_cache(vnode);

	/* success */
	clear_bit(AFS_VNODE_UNSET, &vnode->flags);
	inode->i_flags |= S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p [CB { v=%u t=%u }]", inode, vnode->cb_version, vnode->cb_type);
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
	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

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
	time64_t now = ktime_get_real_seconds();
	bool valid = false;
	int ret;

	_enter("{v={%x:%u} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	/* Quickly check the callback state.  Ideally, we'd use read_seqbegin
	 * here, but we have no way to pass the net namespace to the RCU
	 * cleanup for the server record.
	 */
	read_seqlock_excl(&vnode->cb_lock);

	if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		if (vnode->cb_s_break != vnode->cb_interest->server->cb_s_break) {
			vnode->cb_s_break = vnode->cb_interest->server->cb_s_break;
		} else if (vnode->status.type == AFS_FTYPE_DIR &&
			   test_bit(AFS_VNODE_DIR_VALID, &vnode->flags) &&
			   vnode->cb_expires_at - 10 > now) {
				valid = true;
		} else if (!test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags) &&
			   vnode->cb_expires_at - 10 > now) {
				valid = true;
		}
	} else if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		valid = true;
	}

	read_sequnlock_excl(&vnode->cb_lock);

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		clear_nlink(&vnode->vfs_inode);

	if (valid)
		goto valid;

	mutex_lock(&vnode->validate_lock);

	/* if the promise has expired, we need to check the server again to get
	 * a new promise - note that if the (parent) directory's metadata was
	 * changed then the security may be different and we may no longer have
	 * access */
	if (!test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		_debug("not promised");
		ret = afs_fetch_status(vnode, key, false);
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
	mutex_unlock(&vnode->validate_lock);
valid:
	_leave(" = 0");
	return 0;

error_unlock:
	mutex_unlock(&vnode->validate_lock);
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
	struct afs_vnode *vnode;

	vnode = AFS_FS_I(inode);

	_enter("{%x:%u.%d}",
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	_debug("CLEAR INODE %p", inode);

	ASSERTCMP(inode->i_ino, ==, vnode->fid.vnode);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	if (vnode->cb_interest) {
		afs_put_cb_interest(afs_i2net(inode), vnode->cb_interest);
		vnode->cb_interest = NULL;
	}

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

	afs_put_permits(rcu_access_pointer(vnode->permit_cache));
	_leave("");
}

/*
 * set the attributes of an inode
 */
int afs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct afs_fs_cursor fc;
	struct afs_vnode *vnode = AFS_FS_I(d_inode(dentry));
	struct key *key;
	int ret;

	_enter("{%x:%u},{n=%pd},%x",
	       vnode->fid.vid, vnode->fid.vnode, dentry,
	       attr->ia_valid);

	if (!(attr->ia_valid & (ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
				ATTR_MTIME))) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	/* flush any dirty data outstanding on a regular file */
	if (S_ISREG(vnode->vfs_inode.i_mode))
		filemap_write_and_wait(vnode->vfs_inode.i_mapping);

	if (attr->ia_valid & ATTR_FILE) {
		key = afs_file_key(attr->ia_file);
	} else {
		key = afs_request_key(vnode->volume->cell);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
			goto error;
		}
	}

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = vnode->cb_break + vnode->cb_s_break;
			afs_fs_setattr(&fc, attr);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	if (!(attr->ia_valid & ATTR_FILE))
		key_put(key);

error:
	_leave(" = %d", ret);
	return ret;
}
