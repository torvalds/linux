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
#include "internal.h"

struct afs_iget_data {
	struct afs_fid		fid;
	struct afs_volume	*volume;	/* volume on which resides */
};

/*
 * map the AFS file status to the inode member variables
 */
static int afs_inode_map_status(struct afs_vnode *vnode, struct key *key)
{
	struct inode *inode = AFS_VNODE_TO_I(vnode);

	_debug("FS: ft=%d lk=%d sz=%llu ver=%Lu mod=%hu",
	       vnode->status.type,
	       vnode->status.nlink,
	       (unsigned long long) vnode->status.size,
	       vnode->status.data_version,
	       vnode->status.mode);

	switch (vnode->status.type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | vnode->status.mode;
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &afs_file_operations;
		break;
	case AFS_FTYPE_DIR:
		inode->i_mode	= S_IFDIR | vnode->status.mode;
		inode->i_op	= &afs_dir_inode_operations;
		inode->i_fop	= &afs_dir_file_operations;
		break;
	case AFS_FTYPE_SYMLINK:
		inode->i_mode	= S_IFLNK | vnode->status.mode;
		inode->i_op	= &page_symlink_inode_operations;
		break;
	default:
		printk("kAFS: AFS vnode with undefined type\n");
		return -EBADMSG;
	}

#ifdef CONFIG_AFS_FSCACHE
	if (vnode->status.size != inode->i_size)
		fscache_attr_changed(vnode->cache);
#endif

	inode->i_nlink		= vnode->status.nlink;
	inode->i_uid		= vnode->status.owner;
	inode->i_gid		= 0;
	inode->i_size		= vnode->status.size;
	inode->i_ctime.tv_sec	= vnode->status.mtime_server;
	inode->i_ctime.tv_nsec	= 0;
	inode->i_atime		= inode->i_mtime = inode->i_ctime;
	inode->i_blocks		= 0;
	inode->i_generation	= vnode->fid.unique;
	inode->i_version	= vnode->status.data_version;
	inode->i_mapping->a_ops	= &afs_fs_aops;

	/* check to see whether a symbolic link is really a mountpoint */
	if (vnode->status.type == AFS_FTYPE_SYMLINK) {
		afs_mntpt_check_symlink(vnode, key);

		if (test_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags)) {
			inode->i_mode	= S_IFDIR | vnode->status.mode;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
		}
	}

	return 0;
}

/*
 * iget5() comparator
 */
static int afs_iget5_test(struct inode *inode, void *opaque)
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
static int afs_iget5_autocell_test(struct inode *inode, void *opaque)
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
 * inode retrieval for autocell
 */
struct inode *afs_iget_autocell(struct inode *dir, const char *dev_name,
				int namesz, struct key *key)
{
	struct afs_iget_data data;
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct super_block *sb;
	struct inode *inode;
	static atomic_t afs_autocell_ino;

	_enter("{%x:%u},%*.*s,",
	       AFS_FS_I(dir)->fid.vid, AFS_FS_I(dir)->fid.vnode,
	       namesz, namesz, dev_name ?: "");

	sb = dir->i_sb;
	as = sb->s_fs_info;
	data.volume = as->volume;
	data.fid.vid = as->volume->vid;
	data.fid.unique = 0;
	data.fid.vnode = 0;

	inode = iget5_locked(sb, atomic_inc_return(&afs_autocell_ino),
			     afs_iget5_autocell_test, afs_iget5_set,
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
	inode->i_op		= &afs_autocell_inode_operations;
	inode->i_nlink		= 2;
	inode->i_uid		= 0;
	inode->i_gid		= 0;
	inode->i_ctime.tv_sec	= get_seconds();
	inode->i_ctime.tv_nsec	= 0;
	inode->i_atime		= inode->i_mtime = inode->i_ctime;
	inode->i_blocks		= 0;
	inode->i_version	= 0;
	inode->i_generation	= 0;

	set_bit(AFS_VNODE_PSEUDODIR, &vnode->flags);
	set_bit(AFS_VNODE_MOUNTPOINT, &vnode->flags);
	inode->i_flags |= S_AUTOMOUNT | S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p", inode);
	return inode;
}

/*
 * inode retrieval
 */
struct inode *afs_iget(struct super_block *sb, struct key *key,
		       struct afs_fid *fid, struct afs_file_status *status,
		       struct afs_callback *cb)
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
		set_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);
		ret = afs_vnode_fetch_status(vnode, NULL, key);
		if (ret < 0)
			goto bad_inode;
	} else {
		/* it's an inode we just created */
		memcpy(&vnode->status, status, sizeof(vnode->status));

		if (!cb) {
			/* it's a symlink we just created (the fileserver
			 * didn't give us a callback) */
			vnode->cb_version = 0;
			vnode->cb_expiry = 0;
			vnode->cb_type = 0;
			vnode->cb_expires = get_seconds();
		} else {
			vnode->cb_version = cb->version;
			vnode->cb_expiry = cb->expiry;
			vnode->cb_type = cb->type;
			vnode->cb_expires = vnode->cb_expiry + get_seconds();
		}
	}

	/* set up caching before mapping the status, as map-status reads the
	 * first page of symlinks to see if they're really mountpoints */
	inode->i_size = vnode->status.size;
#ifdef CONFIG_AFS_FSCACHE
	vnode->cache = fscache_acquire_cookie(vnode->volume->cache,
					      &afs_vnode_cache_index_def,
					      vnode);
#endif

	ret = afs_inode_map_status(vnode, key);
	if (ret < 0)
		goto bad_inode;

	/* success */
	clear_bit(AFS_VNODE_UNSET, &vnode->flags);
	inode->i_flags |= S_NOATIME;
	unlock_new_inode(inode);
	_leave(" = %p [CB { v=%u t=%u }]", inode, vnode->cb_version, vnode->cb_type);
	return inode;

	/* failure */
bad_inode:
#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(vnode->cache, 0);
	vnode->cache = NULL;
#endif
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
	int ret;

	_enter("{v={%x:%u} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	if (vnode->cb_promised &&
	    !test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags) &&
	    !test_bit(AFS_VNODE_MODIFIED, &vnode->flags) &&
	    !test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags)) {
		if (vnode->cb_expires < get_seconds() + 10) {
			_debug("callback expired");
			set_bit(AFS_VNODE_CB_BROKEN, &vnode->flags);
		} else {
			goto valid;
		}
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		goto valid;

	mutex_lock(&vnode->validate_lock);

	/* if the promise has expired, we need to check the server again to get
	 * a new promise - note that if the (parent) directory's metadata was
	 * changed then the security may be different and we may no longer have
	 * access */
	if (!vnode->cb_promised ||
	    test_bit(AFS_VNODE_CB_BROKEN, &vnode->flags)) {
		_debug("not promised");
		ret = afs_vnode_fetch_status(vnode, NULL, key);
		if (ret < 0)
			goto error_unlock;
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

	clear_bit(AFS_VNODE_MODIFIED, &vnode->flags);
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
int afs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		      struct kstat *stat)
{
	struct inode *inode;

	inode = dentry->d_inode;

	_enter("{ ino=%lu v=%u }", inode->i_ino, inode->i_generation);

	generic_fillattr(inode, stat);
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
	struct afs_permits *permits;
	struct afs_vnode *vnode;

	vnode = AFS_FS_I(inode);

	_enter("{%x:%u.%d} v=%u x=%u t=%u }",
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type);

	_debug("CLEAR INODE %p", inode);

	ASSERTCMP(inode->i_ino, ==, vnode->fid.vnode);

	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);

	afs_give_up_callback(vnode);

	if (vnode->server) {
		spin_lock(&vnode->server->fs_lock);
		rb_erase(&vnode->server_rb, &vnode->server->fs_vnodes);
		spin_unlock(&vnode->server->fs_lock);
		afs_put_server(vnode->server);
		vnode->server = NULL;
	}

	ASSERT(list_empty(&vnode->writebacks));
	ASSERT(!vnode->cb_promised);

#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(vnode->cache, 0);
	vnode->cache = NULL;
#endif

	mutex_lock(&vnode->permits_lock);
	permits = vnode->permits;
	rcu_assign_pointer(vnode->permits, NULL);
	mutex_unlock(&vnode->permits_lock);
	if (permits)
		call_rcu(&permits->rcu, afs_zap_permits);

	_leave("");
}

/*
 * set the attributes of an inode
 */
int afs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct afs_vnode *vnode = AFS_FS_I(dentry->d_inode);
	struct key *key;
	int ret;

	_enter("{%x:%u},{n=%s},%x",
	       vnode->fid.vid, vnode->fid.vnode, dentry->d_name.name,
	       attr->ia_valid);

	if (!(attr->ia_valid & (ATTR_SIZE | ATTR_MODE | ATTR_UID | ATTR_GID |
				ATTR_MTIME))) {
		_leave(" = 0 [unsupported]");
		return 0;
	}

	/* flush any dirty data outstanding on a regular file */
	if (S_ISREG(vnode->vfs_inode.i_mode)) {
		filemap_write_and_wait(vnode->vfs_inode.i_mapping);
		afs_writeback_all(vnode);
	}

	if (attr->ia_valid & ATTR_FILE) {
		key = attr->ia_file->private_data;
	} else {
		key = afs_request_key(vnode->volume->cell);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
			goto error;
		}
	}

	ret = afs_vnode_setattr(vnode, key, attr);
	if (!(attr->ia_valid & ATTR_FILE))
		key_put(key);

error:
	_leave(" = %d", ret);
	return ret;
}
