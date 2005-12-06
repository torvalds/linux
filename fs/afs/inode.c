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
 * Authors: David Woodhouse <dwmw2@cambridge.redhat.com>
 *          David Howells <dhowells@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include "super.h"
#include "internal.h"

struct afs_iget_data {
	struct afs_fid		fid;
	struct afs_volume	*volume;	/* volume on which resides */
};

/*****************************************************************************/
/*
 * map the AFS file status to the inode member variables
 */
static int afs_inode_map_status(struct afs_vnode *vnode)
{
	struct inode *inode = AFS_VNODE_TO_I(vnode);

	_debug("FS: ft=%d lk=%d sz=%Zu ver=%Lu mod=%hu",
	       vnode->status.type,
	       vnode->status.nlink,
	       vnode->status.size,
	       vnode->status.version,
	       vnode->status.mode);

	switch (vnode->status.type) {
	case AFS_FTYPE_FILE:
		inode->i_mode	= S_IFREG | vnode->status.mode;
		inode->i_op	= &afs_file_inode_operations;
		inode->i_fop	= &generic_ro_fops;
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

	inode->i_nlink		= vnode->status.nlink;
	inode->i_uid		= vnode->status.owner;
	inode->i_gid		= 0;
	inode->i_size		= vnode->status.size;
	inode->i_ctime.tv_sec	= vnode->status.mtime_server;
	inode->i_ctime.tv_nsec	= 0;
	inode->i_atime		= inode->i_mtime = inode->i_ctime;
	inode->i_blksize	= PAGE_CACHE_SIZE;
	inode->i_blocks		= 0;
	inode->i_version	= vnode->fid.unique;
	inode->i_mapping->a_ops	= &afs_fs_aops;

	/* check to see whether a symbolic link is really a mountpoint */
	if (vnode->status.type == AFS_FTYPE_SYMLINK) {
		afs_mntpt_check_symlink(vnode);

		if (vnode->flags & AFS_VNODE_MOUNTPOINT) {
			inode->i_mode	= S_IFDIR | vnode->status.mode;
			inode->i_op	= &afs_mntpt_inode_operations;
			inode->i_fop	= &afs_mntpt_file_operations;
		}
	}

	return 0;
} /* end afs_inode_map_status() */

/*****************************************************************************/
/*
 * attempt to fetch the status of an inode, coelescing multiple simultaneous
 * fetches
 */
static int afs_inode_fetch_status(struct inode *inode)
{
	struct afs_vnode *vnode;
	int ret;

	vnode = AFS_FS_I(inode);

	ret = afs_vnode_fetch_status(vnode);

	if (ret == 0)
		ret = afs_inode_map_status(vnode);

	return ret;

} /* end afs_inode_fetch_status() */

/*****************************************************************************/
/*
 * iget5() comparator
 */
static int afs_iget5_test(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;

	return inode->i_ino == data->fid.vnode &&
		inode->i_version == data->fid.unique;
} /* end afs_iget5_test() */

/*****************************************************************************/
/*
 * iget5() inode initialiser
 */
static int afs_iget5_set(struct inode *inode, void *opaque)
{
	struct afs_iget_data *data = opaque;
	struct afs_vnode *vnode = AFS_FS_I(inode);

	inode->i_ino = data->fid.vnode;
	inode->i_version = data->fid.unique;
	vnode->fid = data->fid;
	vnode->volume = data->volume;

	return 0;
} /* end afs_iget5_set() */

/*****************************************************************************/
/*
 * inode retrieval
 */
inline int afs_iget(struct super_block *sb, struct afs_fid *fid,
		    struct inode **_inode)
{
	struct afs_iget_data data = { .fid = *fid };
	struct afs_super_info *as;
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	_enter(",{%u,%u,%u},,", fid->vid, fid->vnode, fid->unique);

	as = sb->s_fs_info;
	data.volume = as->volume;

	inode = iget5_locked(sb, fid->vnode, afs_iget5_test, afs_iget5_set,
			     &data);
	if (!inode) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	vnode = AFS_FS_I(inode);

	/* deal with an existing inode */
	if (!(inode->i_state & I_NEW)) {
		ret = afs_vnode_fetch_status(vnode);
		if (ret==0)
			*_inode = inode;
		else
			iput(inode);
		_leave(" = %d", ret);
		return ret;
	}

#ifdef AFS_CACHING_SUPPORT
	/* set up caching before reading the status, as fetch-status reads the
	 * first page of symlinks to see if they're really mntpts */
	cachefs_acquire_cookie(vnode->volume->cache,
			       NULL,
			       vnode,
			       &vnode->cache);
#endif

	/* okay... it's a new inode */
	inode->i_flags |= S_NOATIME;
	vnode->flags |= AFS_VNODE_CHANGED;
	ret = afs_inode_fetch_status(inode);
	if (ret<0)
		goto bad_inode;

	/* success */
	unlock_new_inode(inode);

	*_inode = inode;
	_leave(" = 0 [CB { v=%u x=%lu t=%u }]",
	       vnode->cb_version,
	       vnode->cb_timeout.timo_jif,
	       vnode->cb_type);
	return 0;

	/* failure */
 bad_inode:
	make_bad_inode(inode);
	unlock_new_inode(inode);
	iput(inode);

	_leave(" = %d [bad]", ret);
	return ret;
} /* end afs_iget() */

/*****************************************************************************/
/*
 * read the attributes of an inode
 */
int afs_inode_getattr(struct vfsmount *mnt, struct dentry *dentry,
		      struct kstat *stat)
{
	struct afs_vnode *vnode;
	struct inode *inode;
	int ret;

	inode = dentry->d_inode;

	_enter("{ ino=%lu v=%lu }", inode->i_ino, inode->i_version);

	vnode = AFS_FS_I(inode);

	ret = afs_inode_fetch_status(inode);
	if (ret == -ENOENT) {
		_leave(" = %d [%d %p]",
		       ret, atomic_read(&dentry->d_count), dentry->d_inode);
		return ret;
	}
	else if (ret < 0) {
		make_bad_inode(inode);
		_leave(" = %d", ret);
		return ret;
	}

	/* transfer attributes from the inode structure to the stat
	 * structure */
	generic_fillattr(inode, stat);

	_leave(" = 0 CB { v=%u x=%u t=%u }",
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type);

	return 0;
} /* end afs_inode_getattr() */

/*****************************************************************************/
/*
 * clear an AFS inode
 */
void afs_clear_inode(struct inode *inode)
{
	struct afs_vnode *vnode;

	vnode = AFS_FS_I(inode);

	_enter("ino=%lu { vn=%08x v=%u x=%u t=%u }",
	       inode->i_ino,
	       vnode->fid.vnode,
	       vnode->cb_version,
	       vnode->cb_expiry,
	       vnode->cb_type
	       );

	BUG_ON(inode->i_ino != vnode->fid.vnode);

	afs_vnode_give_up_callback(vnode);

#ifdef AFS_CACHING_SUPPORT
	cachefs_relinquish_cookie(vnode->cache, 0);
	vnode->cache = NULL;
#endif

	_leave("");
} /* end afs_clear_inode() */
