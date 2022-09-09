// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contains vfs inode ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/namei.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"
#include "cache.h"
#include "xattr.h"
#include "acl.h"

static const struct inode_operations v9fs_dir_inode_operations;
static const struct inode_operations v9fs_dir_inode_operations_dotu;
static const struct inode_operations v9fs_file_inode_operations;
static const struct inode_operations v9fs_symlink_inode_operations;

/**
 * unixmode2p9mode - convert unix mode bits to plan 9
 * @v9ses: v9fs session information
 * @mode: mode to convert
 *
 */

static u32 unixmode2p9mode(struct v9fs_session_info *v9ses, umode_t mode)
{
	int res;

	res = mode & 0777;
	if (S_ISDIR(mode))
		res |= P9_DMDIR;
	if (v9fs_proto_dotu(v9ses)) {
		if (v9ses->nodev == 0) {
			if (S_ISSOCK(mode))
				res |= P9_DMSOCKET;
			if (S_ISFIFO(mode))
				res |= P9_DMNAMEDPIPE;
			if (S_ISBLK(mode))
				res |= P9_DMDEVICE;
			if (S_ISCHR(mode))
				res |= P9_DMDEVICE;
		}

		if ((mode & S_ISUID) == S_ISUID)
			res |= P9_DMSETUID;
		if ((mode & S_ISGID) == S_ISGID)
			res |= P9_DMSETGID;
		if ((mode & S_ISVTX) == S_ISVTX)
			res |= P9_DMSETVTX;
	}
	return res;
}

/**
 * p9mode2perm- convert plan9 mode bits to unix permission bits
 * @v9ses: v9fs session information
 * @stat: p9_wstat from which mode need to be derived
 *
 */
static int p9mode2perm(struct v9fs_session_info *v9ses,
		       struct p9_wstat *stat)
{
	int res;
	int mode = stat->mode;

	res = mode & S_IALLUGO;
	if (v9fs_proto_dotu(v9ses)) {
		if ((mode & P9_DMSETUID) == P9_DMSETUID)
			res |= S_ISUID;

		if ((mode & P9_DMSETGID) == P9_DMSETGID)
			res |= S_ISGID;

		if ((mode & P9_DMSETVTX) == P9_DMSETVTX)
			res |= S_ISVTX;
	}
	return res;
}

/**
 * p9mode2unixmode- convert plan9 mode bits to unix mode bits
 * @v9ses: v9fs session information
 * @stat: p9_wstat from which mode need to be derived
 * @rdev: major number, minor number in case of device files.
 *
 */
static umode_t p9mode2unixmode(struct v9fs_session_info *v9ses,
			       struct p9_wstat *stat, dev_t *rdev)
{
	int res, r;
	u32 mode = stat->mode;

	*rdev = 0;
	res = p9mode2perm(v9ses, stat);

	if ((mode & P9_DMDIR) == P9_DMDIR)
		res |= S_IFDIR;
	else if ((mode & P9_DMSYMLINK) && (v9fs_proto_dotu(v9ses)))
		res |= S_IFLNK;
	else if ((mode & P9_DMSOCKET) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->nodev == 0))
		res |= S_IFSOCK;
	else if ((mode & P9_DMNAMEDPIPE) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->nodev == 0))
		res |= S_IFIFO;
	else if ((mode & P9_DMDEVICE) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->nodev == 0)) {
		char type = 0;
		int major = -1, minor = -1;

		r = sscanf(stat->extension, "%c %i %i", &type, &major, &minor);
		if (r != 3) {
			p9_debug(P9_DEBUG_ERROR,
				 "invalid device string, umode will be bogus: %s\n",
				 stat->extension);
			return res;
		}
		switch (type) {
		case 'c':
			res |= S_IFCHR;
			break;
		case 'b':
			res |= S_IFBLK;
			break;
		default:
			p9_debug(P9_DEBUG_ERROR, "Unknown special type %c %s\n",
				 type, stat->extension);
		}
		*rdev = MKDEV(major, minor);
	} else
		res |= S_IFREG;

	return res;
}

/**
 * v9fs_uflags2omode- convert posix open flags to plan 9 mode bits
 * @uflags: flags to convert
 * @extended: if .u extensions are active
 */

int v9fs_uflags2omode(int uflags, int extended)
{
	int ret;

	ret = 0;
	switch (uflags&3) {
	default:
	case O_RDONLY:
		ret = P9_OREAD;
		break;

	case O_WRONLY:
		ret = P9_OWRITE;
		break;

	case O_RDWR:
		ret = P9_ORDWR;
		break;
	}

	if (extended) {
		if (uflags & O_EXCL)
			ret |= P9_OEXCL;

		if (uflags & O_APPEND)
			ret |= P9_OAPPEND;
	}

	return ret;
}

/**
 * v9fs_blank_wstat - helper function to setup a 9P stat structure
 * @wstat: structure to initialize
 *
 */

void
v9fs_blank_wstat(struct p9_wstat *wstat)
{
	wstat->type = ~0;
	wstat->dev = ~0;
	wstat->qid.type = ~0;
	wstat->qid.version = ~0;
	*((long long *)&wstat->qid.path) = ~0;
	wstat->mode = ~0;
	wstat->atime = ~0;
	wstat->mtime = ~0;
	wstat->length = ~0;
	wstat->name = NULL;
	wstat->uid = NULL;
	wstat->gid = NULL;
	wstat->muid = NULL;
	wstat->n_uid = INVALID_UID;
	wstat->n_gid = INVALID_GID;
	wstat->n_muid = INVALID_UID;
	wstat->extension = NULL;
}

/**
 * v9fs_alloc_inode - helper function to allocate an inode
 * @sb: The superblock to allocate the inode from
 */
struct inode *v9fs_alloc_inode(struct super_block *sb)
{
	struct v9fs_inode *v9inode;

	v9inode = alloc_inode_sb(sb, v9fs_inode_cache, GFP_KERNEL);
	if (!v9inode)
		return NULL;
	v9inode->writeback_fid = NULL;
	v9inode->cache_validity = 0;
	mutex_init(&v9inode->v_mutex);
	return &v9inode->netfs.inode;
}

/**
 * v9fs_free_inode - destroy an inode
 * @inode: The inode to be freed
 */

void v9fs_free_inode(struct inode *inode)
{
	kmem_cache_free(v9fs_inode_cache, V9FS_I(inode));
}

/*
 * Set parameters for the netfs library
 */
static void v9fs_set_netfs_context(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	netfs_inode_init(&v9inode->netfs, &v9fs_req_ops);
}

int v9fs_init_inode(struct v9fs_session_info *v9ses,
		    struct inode *inode, umode_t mode, dev_t rdev)
{
	int err = 0;

	inode_init_owner(&init_user_ns, inode, NULL, mode);
	inode->i_blocks = 0;
	inode->i_rdev = rdev;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_mapping->a_ops = &v9fs_addr_operations;
	inode->i_private = NULL;

	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
		if (v9fs_proto_dotl(v9ses)) {
			inode->i_op = &v9fs_file_inode_operations_dotl;
		} else if (v9fs_proto_dotu(v9ses)) {
			inode->i_op = &v9fs_file_inode_operations;
		} else {
			p9_debug(P9_DEBUG_ERROR,
				 "special files without extended mode\n");
			err = -EINVAL;
			goto error;
		}
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	case S_IFREG:
		if (v9fs_proto_dotl(v9ses)) {
			inode->i_op = &v9fs_file_inode_operations_dotl;
			if (v9ses->cache == CACHE_LOOSE ||
			    v9ses->cache == CACHE_FSCACHE)
				inode->i_fop =
					&v9fs_cached_file_operations_dotl;
			else if (v9ses->cache == CACHE_MMAP)
				inode->i_fop = &v9fs_mmap_file_operations_dotl;
			else
				inode->i_fop = &v9fs_file_operations_dotl;
		} else {
			inode->i_op = &v9fs_file_inode_operations;
			if (v9ses->cache == CACHE_LOOSE ||
			    v9ses->cache == CACHE_FSCACHE)
				inode->i_fop =
					&v9fs_cached_file_operations;
			else if (v9ses->cache == CACHE_MMAP)
				inode->i_fop = &v9fs_mmap_file_operations;
			else
				inode->i_fop = &v9fs_file_operations;
		}

		break;
	case S_IFLNK:
		if (!v9fs_proto_dotu(v9ses) && !v9fs_proto_dotl(v9ses)) {
			p9_debug(P9_DEBUG_ERROR,
				 "extended modes used with legacy protocol\n");
			err = -EINVAL;
			goto error;
		}

		if (v9fs_proto_dotl(v9ses))
			inode->i_op = &v9fs_symlink_inode_operations_dotl;
		else
			inode->i_op = &v9fs_symlink_inode_operations;

		break;
	case S_IFDIR:
		inc_nlink(inode);
		if (v9fs_proto_dotl(v9ses))
			inode->i_op = &v9fs_dir_inode_operations_dotl;
		else if (v9fs_proto_dotu(v9ses))
			inode->i_op = &v9fs_dir_inode_operations_dotu;
		else
			inode->i_op = &v9fs_dir_inode_operations;

		if (v9fs_proto_dotl(v9ses))
			inode->i_fop = &v9fs_dir_operations_dotl;
		else
			inode->i_fop = &v9fs_dir_operations;

		break;
	default:
		p9_debug(P9_DEBUG_ERROR, "BAD mode 0x%hx S_IFMT 0x%x\n",
			 mode, mode & S_IFMT);
		err = -EINVAL;
		goto error;
	}

	v9fs_set_netfs_context(inode);
error:
	return err;

}

/**
 * v9fs_get_inode - helper function to setup an inode
 * @sb: superblock
 * @mode: mode to setup inode with
 * @rdev: The device numbers to set
 */

struct inode *v9fs_get_inode(struct super_block *sb, umode_t mode, dev_t rdev)
{
	int err;
	struct inode *inode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	p9_debug(P9_DEBUG_VFS, "super block: %p mode: %ho\n", sb, mode);

	inode = new_inode(sb);
	if (!inode) {
		pr_warn("%s (%d): Problem allocating inode\n",
			__func__, task_pid_nr(current));
		return ERR_PTR(-ENOMEM);
	}
	err = v9fs_init_inode(v9ses, inode, mode, rdev);
	if (err) {
		iput(inode);
		return ERR_PTR(err);
	}
	return inode;
}

/**
 * v9fs_evict_inode - Remove an inode from the inode cache
 * @inode: inode to release
 *
 */
void v9fs_evict_inode(struct inode *inode)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	__le32 version;

	truncate_inode_pages_final(&inode->i_data);
	version = cpu_to_le32(v9inode->qid.version);
	fscache_clear_inode_writeback(v9fs_inode_cookie(v9inode), inode,
				      &version);
	clear_inode(inode);
	filemap_fdatawrite(&inode->i_data);

	fscache_relinquish_cookie(v9fs_inode_cookie(v9inode), false);
	/* clunk the fid stashed in writeback_fid */
	p9_fid_put(v9inode->writeback_fid);
	v9inode->writeback_fid = NULL;
}

static int v9fs_test_inode(struct inode *inode, void *data)
{
	int umode;
	dev_t rdev;
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct p9_wstat *st = (struct p9_wstat *)data;
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);

	umode = p9mode2unixmode(v9ses, st, &rdev);
	/* don't match inode of different type */
	if (inode_wrong_type(inode, umode))
		return 0;

	/* compare qid details */
	if (memcmp(&v9inode->qid.version,
		   &st->qid.version, sizeof(v9inode->qid.version)))
		return 0;

	if (v9inode->qid.type != st->qid.type)
		return 0;

	if (v9inode->qid.path != st->qid.path)
		return 0;
	return 1;
}

static int v9fs_test_new_inode(struct inode *inode, void *data)
{
	return 0;
}

static int v9fs_set_inode(struct inode *inode,  void *data)
{
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct p9_wstat *st = (struct p9_wstat *)data;

	memcpy(&v9inode->qid, &st->qid, sizeof(st->qid));
	return 0;
}

static struct inode *v9fs_qid_iget(struct super_block *sb,
				   struct p9_qid *qid,
				   struct p9_wstat *st,
				   int new)
{
	dev_t rdev;
	int retval;
	umode_t umode;
	unsigned long i_ino;
	struct inode *inode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;
	int (*test)(struct inode *inode, void *data);

	if (new)
		test = v9fs_test_new_inode;
	else
		test = v9fs_test_inode;

	i_ino = v9fs_qid2ino(qid);
	inode = iget5_locked(sb, i_ino, test, v9fs_set_inode, st);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	/*
	 * initialize the inode with the stat info
	 * FIXME!! we may need support for stale inodes
	 * later.
	 */
	inode->i_ino = i_ino;
	umode = p9mode2unixmode(v9ses, st, &rdev);
	retval = v9fs_init_inode(v9ses, inode, umode, rdev);
	if (retval)
		goto error;

	v9fs_stat2inode(st, inode, sb, 0);
	v9fs_cache_inode_get_cookie(inode);
	unlock_new_inode(inode);
	return inode;
error:
	iget_failed(inode);
	return ERR_PTR(retval);

}

struct inode *
v9fs_inode_from_fid(struct v9fs_session_info *v9ses, struct p9_fid *fid,
		    struct super_block *sb, int new)
{
	struct p9_wstat *st;
	struct inode *inode = NULL;

	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return ERR_CAST(st);

	inode = v9fs_qid_iget(sb, &st->qid, st, new);
	p9stat_free(st);
	kfree(st);
	return inode;
}

/**
 * v9fs_at_to_dotl_flags- convert Linux specific AT flags to
 * plan 9 AT flag.
 * @flags: flags to convert
 */
static int v9fs_at_to_dotl_flags(int flags)
{
	int rflags = 0;

	if (flags & AT_REMOVEDIR)
		rflags |= P9_DOTL_AT_REMOVEDIR;

	return rflags;
}

/**
 * v9fs_dec_count - helper functon to drop i_nlink.
 *
 * If a directory had nlink <= 2 (including . and ..), then we should not drop
 * the link count, which indicates the underlying exported fs doesn't maintain
 * nlink accurately. e.g.
 * - overlayfs sets nlink to 1 for merged dir
 * - ext4 (with dir_nlink feature enabled) sets nlink to 1 if a dir has more
 *   than EXT4_LINK_MAX (65000) links.
 *
 * @inode: inode whose nlink is being dropped
 */
static void v9fs_dec_count(struct inode *inode)
{
	if (!S_ISDIR(inode->i_mode) || inode->i_nlink > 2)
		drop_nlink(inode);
}

/**
 * v9fs_remove - helper function to remove files and directories
 * @dir: directory inode that is being deleted
 * @dentry:  dentry that is being deleted
 * @flags: removing a directory
 *
 */

static int v9fs_remove(struct inode *dir, struct dentry *dentry, int flags)
{
	struct inode *inode;
	int retval = -EOPNOTSUPP;
	struct p9_fid *v9fid, *dfid;
	struct v9fs_session_info *v9ses;

	p9_debug(P9_DEBUG_VFS, "inode: %p dentry: %p rmdir: %x\n",
		 dir, dentry, flags);

	v9ses = v9fs_inode2v9ses(dir);
	inode = d_inode(dentry);
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		retval = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", retval);
		return retval;
	}
	if (v9fs_proto_dotl(v9ses))
		retval = p9_client_unlinkat(dfid, dentry->d_name.name,
					    v9fs_at_to_dotl_flags(flags));
	p9_fid_put(dfid);
	if (retval == -EOPNOTSUPP) {
		/* Try the one based on path */
		v9fid = v9fs_fid_clone(dentry);
		if (IS_ERR(v9fid))
			return PTR_ERR(v9fid);
		retval = p9_client_remove(v9fid);
	}
	if (!retval) {
		/*
		 * directories on unlink should have zero
		 * link count
		 */
		if (flags & AT_REMOVEDIR) {
			clear_nlink(inode);
			v9fs_dec_count(dir);
		} else
			v9fs_dec_count(inode);

		v9fs_invalidate_inode_attr(inode);
		v9fs_invalidate_inode_attr(dir);

		/* invalidate all fids associated with dentry */
		/* NOTE: This will not include open fids */
		dentry->d_op->d_release(dentry);
	}
	return retval;
}

/**
 * v9fs_create - Create a file
 * @v9ses: session information
 * @dir: directory that dentry is being created in
 * @dentry:  dentry that is being created
 * @extension: 9p2000.u extension string to support devices, etc.
 * @perm: create permissions
 * @mode: open mode
 *
 */
static struct p9_fid *
v9fs_create(struct v9fs_session_info *v9ses, struct inode *dir,
		struct dentry *dentry, char *extension, u32 perm, u8 mode)
{
	int err;
	const unsigned char *name;
	struct p9_fid *dfid, *ofid = NULL, *fid = NULL;
	struct inode *inode;

	p9_debug(P9_DEBUG_VFS, "name %pd\n", dentry);

	err = 0;
	name = dentry->d_name.name;
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		err = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", err);
		return ERR_PTR(err);
	}

	/* clone a fid to use for creation */
	ofid = clone_fid(dfid);
	if (IS_ERR(ofid)) {
		err = PTR_ERR(ofid);
		p9_debug(P9_DEBUG_VFS, "p9_client_walk failed %d\n", err);
		goto error;
	}

	err = p9_client_fcreate(ofid, name, perm, mode, extension);
	if (err < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_fcreate failed %d\n", err);
		goto error;
	}

	if (!(perm & P9_DMLINK)) {
		/* now walk from the parent so we can get unopened fid */
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			p9_debug(P9_DEBUG_VFS,
				   "p9_client_walk failed %d\n", err);
			goto error;
		}
		/*
		 * instantiate inode and assign the unopened fid to the dentry
		 */
		inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			p9_debug(P9_DEBUG_VFS,
				   "inode creation failed %d\n", err);
			goto error;
		}
		v9fs_fid_add(dentry, &fid);
		d_instantiate(dentry, inode);
	}
	p9_fid_put(dfid);
	return ofid;
error:
	p9_fid_put(dfid);
	p9_fid_put(ofid);
	p9_fid_put(fid);
	return ERR_PTR(err);
}

/**
 * v9fs_vfs_create - VFS hook to create a regular file
 * @mnt_userns: The user namespace of the mount
 * @dir: The parent directory
 * @dentry: The name of file to be created
 * @mode: The UNIX file mode to set
 * @excl: True if the file must not yet exist
 *
 * open(.., O_CREAT) is handled in v9fs_vfs_atomic_open().  This is only called
 * for mknod(2).
 *
 */

static int
v9fs_vfs_create(struct user_namespace *mnt_userns, struct inode *dir,
		struct dentry *dentry, umode_t mode, bool excl)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dir);
	u32 perm = unixmode2p9mode(v9ses, mode);
	struct p9_fid *fid;

	/* P9_OEXCL? */
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm, P9_ORDWR);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_invalidate_inode_attr(dir);
	p9_fid_put(fid);

	return 0;
}

/**
 * v9fs_vfs_mkdir - VFS mkdir hook to create a directory
 * @mnt_userns: The user namespace of the mount
 * @dir:  inode that is being unlinked
 * @dentry: dentry that is being unlinked
 * @mode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
			  struct dentry *dentry, umode_t mode)
{
	int err;
	u32 perm;
	struct p9_fid *fid;
	struct v9fs_session_info *v9ses;

	p9_debug(P9_DEBUG_VFS, "name %pd\n", dentry);
	err = 0;
	v9ses = v9fs_inode2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode | S_IFDIR);
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm, P9_OREAD);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
	} else {
		inc_nlink(dir);
		v9fs_invalidate_inode_attr(dir);
	}

	if (fid)
		p9_fid_put(fid);

	return err;
}

/**
 * v9fs_vfs_lookup - VFS lookup hook to "walk" to a new inode
 * @dir:  inode that is being walked from
 * @dentry: dentry that is being walked to?
 * @flags: lookup flags (unused)
 *
 */

struct dentry *v9fs_vfs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct dentry *res;
	struct v9fs_session_info *v9ses;
	struct p9_fid *dfid, *fid;
	struct inode *inode;
	const unsigned char *name;

	p9_debug(P9_DEBUG_VFS, "dir: %p dentry: (%pd) %p flags: %x\n",
		 dir, dentry, dentry, flags);

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	v9ses = v9fs_inode2v9ses(dir);
	/* We can walk d_parent because we hold the dir->i_mutex */
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid))
		return ERR_CAST(dfid);

	/*
	 * Make sure we don't use a wrong inode due to parallel
	 * unlink. For cached mode create calls request for new
	 * inode. But with cache disabled, lookup should do this.
	 */
	name = dentry->d_name.name;
	fid = p9_client_walk(dfid, 1, &name, 1);
	p9_fid_put(dfid);
	if (fid == ERR_PTR(-ENOENT))
		inode = NULL;
	else if (IS_ERR(fid))
		inode = ERR_CAST(fid);
	else if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		inode = v9fs_get_inode_from_fid(v9ses, fid, dir->i_sb);
	else
		inode = v9fs_get_new_inode_from_fid(v9ses, fid, dir->i_sb);
	/*
	 * If we had a rename on the server and a parallel lookup
	 * for the new name, then make sure we instantiate with
	 * the new name. ie look up for a/b, while on server somebody
	 * moved b under k and client parallely did a lookup for
	 * k/b.
	 */
	res = d_splice_alias(inode, dentry);
	if (!IS_ERR(fid)) {
		if (!res)
			v9fs_fid_add(dentry, &fid);
		else if (!IS_ERR(res))
			v9fs_fid_add(res, &fid);
		else
			p9_fid_put(fid);
	}
	return res;
}

static int
v9fs_vfs_atomic_open(struct inode *dir, struct dentry *dentry,
		     struct file *file, unsigned int flags, umode_t mode)
{
	int err;
	u32 perm;
	struct v9fs_inode *v9inode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid, *inode_fid;
	struct dentry *res = NULL;
	struct inode *inode;

	if (d_in_lookup(dentry)) {
		res = v9fs_vfs_lookup(dir, dentry, 0);
		if (IS_ERR(res))
			return PTR_ERR(res);

		if (res)
			dentry = res;
	}

	/* Only creates */
	if (!(flags & O_CREAT) || d_really_is_positive(dentry))
		return finish_no_open(file, res);

	err = 0;

	v9ses = v9fs_inode2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode);
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm,
				v9fs_uflags2omode(flags,
						v9fs_proto_dotu(v9ses)));
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		goto error;
	}

	v9fs_invalidate_inode_attr(dir);
	inode = d_inode(dentry);
	v9inode = V9FS_I(inode);
	mutex_lock(&v9inode->v_mutex);
	if ((v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) &&
	    !v9inode->writeback_fid &&
	    ((flags & O_ACCMODE) != O_RDONLY)) {
		/*
		 * clone a fid and add it to writeback_fid
		 * we do it during open time instead of
		 * page dirty time via write_begin/page_mkwrite
		 * because we want write after unlink usecase
		 * to work.
		 */
		inode_fid = v9fs_writeback_fid(dentry);
		if (IS_ERR(inode_fid)) {
			err = PTR_ERR(inode_fid);
			mutex_unlock(&v9inode->v_mutex);
			goto error;
		}
		v9inode->writeback_fid = (void *) inode_fid;
	}
	mutex_unlock(&v9inode->v_mutex);
	err = finish_open(file, dentry, generic_file_open);
	if (err)
		goto error;

	file->private_data = fid;
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		fscache_use_cookie(v9fs_inode_cookie(v9inode),
				   file->f_mode & FMODE_WRITE);
	v9fs_open_fid_add(inode, &fid);

	file->f_mode |= FMODE_CREATED;
out:
	dput(res);
	return err;

error:
	p9_fid_put(fid);
	goto out;
}

/**
 * v9fs_vfs_unlink - VFS unlink hook to delete an inode
 * @i:  inode that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

int v9fs_vfs_unlink(struct inode *i, struct dentry *d)
{
	return v9fs_remove(i, d, 0);
}

/**
 * v9fs_vfs_rmdir - VFS unlink hook to delete a directory
 * @i:  inode that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

int v9fs_vfs_rmdir(struct inode *i, struct dentry *d)
{
	return v9fs_remove(i, d, AT_REMOVEDIR);
}

/**
 * v9fs_vfs_rename - VFS hook to rename an inode
 * @mnt_userns: The user namespace of the mount
 * @old_dir:  old dir inode
 * @old_dentry: old dentry
 * @new_dir: new dir inode
 * @new_dentry: new dentry
 * @flags: RENAME_* flags
 *
 */

int
v9fs_vfs_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
		struct dentry *old_dentry, struct inode *new_dir,
		struct dentry *new_dentry, unsigned int flags)
{
	int retval;
	struct inode *old_inode;
	struct inode *new_inode;
	struct v9fs_session_info *v9ses;
	struct p9_fid *oldfid = NULL, *dfid = NULL;
	struct p9_fid *olddirfid = NULL;
	struct p9_fid *newdirfid = NULL;
	struct p9_wstat wstat;

	if (flags)
		return -EINVAL;

	p9_debug(P9_DEBUG_VFS, "\n");
	retval = 0;
	old_inode = d_inode(old_dentry);
	new_inode = d_inode(new_dentry);
	v9ses = v9fs_inode2v9ses(old_inode);
	oldfid = v9fs_fid_lookup(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	dfid = v9fs_parent_fid(old_dentry);
	olddirfid = clone_fid(dfid);
	p9_fid_put(dfid);
	dfid = NULL;

	if (IS_ERR(olddirfid)) {
		retval = PTR_ERR(olddirfid);
		goto error;
	}

	dfid = v9fs_parent_fid(new_dentry);
	newdirfid = clone_fid(dfid);
	p9_fid_put(dfid);
	dfid = NULL;

	if (IS_ERR(newdirfid)) {
		retval = PTR_ERR(newdirfid);
		goto error;
	}

	down_write(&v9ses->rename_sem);
	if (v9fs_proto_dotl(v9ses)) {
		retval = p9_client_renameat(olddirfid, old_dentry->d_name.name,
					    newdirfid, new_dentry->d_name.name);
		if (retval == -EOPNOTSUPP)
			retval = p9_client_rename(oldfid, newdirfid,
						  new_dentry->d_name.name);
		if (retval != -EOPNOTSUPP)
			goto error_locked;
	}
	if (old_dentry->d_parent != new_dentry->d_parent) {
		/*
		 * 9P .u can only handle file rename in the same directory
		 */

		p9_debug(P9_DEBUG_ERROR, "old dir and new dir are different\n");
		retval = -EXDEV;
		goto error_locked;
	}
	v9fs_blank_wstat(&wstat);
	wstat.muid = v9ses->uname;
	wstat.name = new_dentry->d_name.name;
	retval = p9_client_wstat(oldfid, &wstat);

error_locked:
	if (!retval) {
		if (new_inode) {
			if (S_ISDIR(new_inode->i_mode))
				clear_nlink(new_inode);
			else
				v9fs_dec_count(new_inode);
		}
		if (S_ISDIR(old_inode->i_mode)) {
			if (!new_inode)
				inc_nlink(new_dir);
			v9fs_dec_count(old_dir);
		}
		v9fs_invalidate_inode_attr(old_inode);
		v9fs_invalidate_inode_attr(old_dir);
		v9fs_invalidate_inode_attr(new_dir);

		/* successful rename */
		d_move(old_dentry, new_dentry);
	}
	up_write(&v9ses->rename_sem);

error:
	p9_fid_put(newdirfid);
	p9_fid_put(olddirfid);
	p9_fid_put(oldfid);
	return retval;
}

/**
 * v9fs_vfs_getattr - retrieve file metadata
 * @mnt_userns: The user namespace of the mount
 * @path: Object to query
 * @stat: metadata structure to populate
 * @request_mask: Mask of STATX_xxx flags indicating the caller's interests
 * @flags: AT_STATX_xxx setting
 *
 */

static int
v9fs_vfs_getattr(struct user_namespace *mnt_userns, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;

	p9_debug(P9_DEBUG_VFS, "dentry: %p\n", dentry);
	v9ses = v9fs_dentry2v9ses(dentry);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		generic_fillattr(&init_user_ns, d_inode(dentry), stat);
		return 0;
	}
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	st = p9_client_stat(fid);
	p9_fid_put(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);

	v9fs_stat2inode(st, d_inode(dentry), dentry->d_sb, 0);
	generic_fillattr(&init_user_ns, d_inode(dentry), stat);

	p9stat_free(st);
	kfree(st);
	return 0;
}

/**
 * v9fs_vfs_setattr - set file metadata
 * @mnt_userns: The user namespace of the mount
 * @dentry: file whose metadata to set
 * @iattr: metadata assignment structure
 *
 */

static int v9fs_vfs_setattr(struct user_namespace *mnt_userns,
			    struct dentry *dentry, struct iattr *iattr)
{
	int retval, use_dentry = 0;
	struct inode *inode = d_inode(dentry);
	struct v9fs_inode *v9inode = V9FS_I(inode);
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid = NULL;
	struct p9_wstat wstat;

	p9_debug(P9_DEBUG_VFS, "\n");
	retval = setattr_prepare(&init_user_ns, dentry, iattr);
	if (retval)
		return retval;

	retval = -EPERM;
	v9ses = v9fs_dentry2v9ses(dentry);
	if (iattr->ia_valid & ATTR_FILE) {
		fid = iattr->ia_file->private_data;
		WARN_ON(!fid);
	}
	if (!fid) {
		fid = v9fs_fid_lookup(dentry);
		use_dentry = 1;
	}
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_blank_wstat(&wstat);
	if (iattr->ia_valid & ATTR_MODE)
		wstat.mode = unixmode2p9mode(v9ses, iattr->ia_mode);

	if (iattr->ia_valid & ATTR_MTIME)
		wstat.mtime = iattr->ia_mtime.tv_sec;

	if (iattr->ia_valid & ATTR_ATIME)
		wstat.atime = iattr->ia_atime.tv_sec;

	if (iattr->ia_valid & ATTR_SIZE)
		wstat.length = iattr->ia_size;

	if (v9fs_proto_dotu(v9ses)) {
		if (iattr->ia_valid & ATTR_UID)
			wstat.n_uid = iattr->ia_uid;

		if (iattr->ia_valid & ATTR_GID)
			wstat.n_gid = iattr->ia_gid;
	}

	/* Write all dirty data */
	if (d_is_reg(dentry))
		filemap_write_and_wait(inode->i_mapping);

	retval = p9_client_wstat(fid, &wstat);

	if (use_dentry)
		p9_fid_put(fid);

	if (retval < 0)
		return retval;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode)) {
		truncate_setsize(inode, iattr->ia_size);
		fscache_resize_cookie(v9fs_inode_cookie(v9inode), iattr->ia_size);
	}

	v9fs_invalidate_inode_attr(inode);

	setattr_copy(&init_user_ns, inode, iattr);
	mark_inode_dirty(inode);
	return 0;
}

/**
 * v9fs_stat2inode - populate an inode structure with mistat info
 * @stat: Plan 9 metadata (mistat) structure
 * @inode: inode to populate
 * @sb: superblock of filesystem
 * @flags: control flags (e.g. V9FS_STAT2INODE_KEEP_ISIZE)
 *
 */

void
v9fs_stat2inode(struct p9_wstat *stat, struct inode *inode,
		 struct super_block *sb, unsigned int flags)
{
	umode_t mode;
	struct v9fs_session_info *v9ses = sb->s_fs_info;
	struct v9fs_inode *v9inode = V9FS_I(inode);

	set_nlink(inode, 1);

	inode->i_atime.tv_sec = stat->atime;
	inode->i_mtime.tv_sec = stat->mtime;
	inode->i_ctime.tv_sec = stat->mtime;

	inode->i_uid = v9ses->dfltuid;
	inode->i_gid = v9ses->dfltgid;

	if (v9fs_proto_dotu(v9ses)) {
		inode->i_uid = stat->n_uid;
		inode->i_gid = stat->n_gid;
	}
	if ((S_ISREG(inode->i_mode)) || (S_ISDIR(inode->i_mode))) {
		if (v9fs_proto_dotu(v9ses)) {
			unsigned int i_nlink;
			/*
			 * Hadlink support got added later to the .u extension.
			 * So there can be a server out there that doesn't
			 * support this even with .u extension. That would
			 * just leave us with stat->extension being an empty
			 * string, though.
			 */
			/* HARDLINKCOUNT %u */
			if (sscanf(stat->extension,
				   " HARDLINKCOUNT %u", &i_nlink) == 1)
				set_nlink(inode, i_nlink);
		}
	}
	mode = p9mode2perm(v9ses, stat);
	mode |= inode->i_mode & ~S_IALLUGO;
	inode->i_mode = mode;

	if (!(flags & V9FS_STAT2INODE_KEEP_ISIZE))
		v9fs_i_size_write(inode, stat->length);
	/* not real number of blocks, but 512 byte ones ... */
	inode->i_blocks = (stat->length + 512 - 1) >> 9;
	v9inode->cache_validity &= ~V9FS_INO_INVALID_ATTR;
}

/**
 * v9fs_qid2ino - convert qid into inode number
 * @qid: qid to hash
 *
 * BUG: potential for inode number collisions?
 */

ino_t v9fs_qid2ino(struct p9_qid *qid)
{
	u64 path = qid->path + 2;
	ino_t i = 0;

	if (sizeof(ino_t) == sizeof(path))
		memcpy(&i, &path, sizeof(ino_t));
	else
		i = (ino_t) (path ^ (path >> 32));

	return i;
}

/**
 * v9fs_vfs_get_link - follow a symlink path
 * @dentry: dentry for symlink
 * @inode: inode for symlink
 * @done: delayed call for when we are done with the return value
 */

static const char *v9fs_vfs_get_link(struct dentry *dentry,
				     struct inode *inode,
				     struct delayed_call *done)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;
	char *res;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	v9ses = v9fs_dentry2v9ses(dentry);
	if (!v9fs_proto_dotu(v9ses))
		return ERR_PTR(-EBADF);

	p9_debug(P9_DEBUG_VFS, "%pd\n", dentry);
	fid = v9fs_fid_lookup(dentry);

	if (IS_ERR(fid))
		return ERR_CAST(fid);

	st = p9_client_stat(fid);
	p9_fid_put(fid);
	if (IS_ERR(st))
		return ERR_CAST(st);

	if (!(st->mode & P9_DMSYMLINK)) {
		p9stat_free(st);
		kfree(st);
		return ERR_PTR(-EINVAL);
	}
	res = st->extension;
	st->extension = NULL;
	if (strlen(res) >= PATH_MAX)
		res[PATH_MAX - 1] = '\0';

	p9stat_free(st);
	kfree(st);
	set_delayed_call(done, kfree_link, res);
	return res;
}

/**
 * v9fs_vfs_mkspecial - create a special file
 * @dir: inode to create special file in
 * @dentry: dentry to create
 * @perm: mode to create special file
 * @extension: 9p2000.u format extension string representing special file
 *
 */

static int v9fs_vfs_mkspecial(struct inode *dir, struct dentry *dentry,
	u32 perm, const char *extension)
{
	struct p9_fid *fid;
	struct v9fs_session_info *v9ses;

	v9ses = v9fs_inode2v9ses(dir);
	if (!v9fs_proto_dotu(v9ses)) {
		p9_debug(P9_DEBUG_ERROR, "not extended\n");
		return -EPERM;
	}

	fid = v9fs_create(v9ses, dir, dentry, (char *) extension, perm,
								P9_OREAD);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_invalidate_inode_attr(dir);
	p9_fid_put(fid);
	return 0;
}

/**
 * v9fs_vfs_symlink - helper function to create symlinks
 * @mnt_userns: The user namespace of the mount
 * @dir: directory inode containing symlink
 * @dentry: dentry for symlink
 * @symname: symlink data
 *
 * See Also: 9P2000.u RFC for more information
 *
 */

static int
v9fs_vfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
		 struct dentry *dentry, const char *symname)
{
	p9_debug(P9_DEBUG_VFS, " %lu,%pd,%s\n",
		 dir->i_ino, dentry, symname);

	return v9fs_vfs_mkspecial(dir, dentry, P9_DMSYMLINK, symname);
}

#define U32_MAX_DIGITS 10

/**
 * v9fs_vfs_link - create a hardlink
 * @old_dentry: dentry for file to link to
 * @dir: inode destination for new link
 * @dentry: dentry for link
 *
 */

static int
v9fs_vfs_link(struct dentry *old_dentry, struct inode *dir,
	      struct dentry *dentry)
{
	int retval;
	char name[1 + U32_MAX_DIGITS + 2]; /* sign + number + \n + \0 */
	struct p9_fid *oldfid;

	p9_debug(P9_DEBUG_VFS, " %lu,%pd,%pd\n",
		 dir->i_ino, dentry, old_dentry);

	oldfid = v9fs_fid_clone(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	sprintf(name, "%d\n", oldfid->fid);
	retval = v9fs_vfs_mkspecial(dir, dentry, P9_DMLINK, name);
	if (!retval) {
		v9fs_refresh_inode(oldfid, d_inode(old_dentry));
		v9fs_invalidate_inode_attr(dir);
	}
	p9_fid_put(oldfid);
	return retval;
}

/**
 * v9fs_vfs_mknod - create a special file
 * @mnt_userns: The user namespace of the mount
 * @dir: inode destination for new link
 * @dentry: dentry for file
 * @mode: mode for creation
 * @rdev: device associated with special file
 *
 */

static int
v9fs_vfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(dir);
	int retval;
	char name[2 + U32_MAX_DIGITS + 1 + U32_MAX_DIGITS + 1];
	u32 perm;

	p9_debug(P9_DEBUG_VFS, " %lu,%pd mode: %x MAJOR: %u MINOR: %u\n",
		 dir->i_ino, dentry, mode,
		 MAJOR(rdev), MINOR(rdev));

	/* build extension */
	if (S_ISBLK(mode))
		sprintf(name, "b %u %u", MAJOR(rdev), MINOR(rdev));
	else if (S_ISCHR(mode))
		sprintf(name, "c %u %u", MAJOR(rdev), MINOR(rdev));
	else
		*name = 0;

	perm = unixmode2p9mode(v9ses, mode);
	retval = v9fs_vfs_mkspecial(dir, dentry, perm, name);

	return retval;
}

int v9fs_refresh_inode(struct p9_fid *fid, struct inode *inode)
{
	int umode;
	dev_t rdev;
	struct p9_wstat *st;
	struct v9fs_session_info *v9ses;
	unsigned int flags;

	v9ses = v9fs_inode2v9ses(inode);
	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);
	/*
	 * Don't update inode if the file type is different
	 */
	umode = p9mode2unixmode(v9ses, st, &rdev);
	if (inode_wrong_type(inode, umode))
		goto out;

	/*
	 * We don't want to refresh inode->i_size,
	 * because we may have cached data
	 */
	flags = (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) ?
		V9FS_STAT2INODE_KEEP_ISIZE : 0;
	v9fs_stat2inode(st, inode, inode->i_sb, flags);
out:
	p9stat_free(st);
	kfree(st);
	return 0;
}

static const struct inode_operations v9fs_dir_inode_operations_dotu = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.atomic_open = v9fs_vfs_atomic_open,
	.symlink = v9fs_vfs_symlink,
	.link = v9fs_vfs_link,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mknod = v9fs_vfs_mknod,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_dir_inode_operations = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.atomic_open = v9fs_vfs_atomic_open,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mknod = v9fs_vfs_mknod,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_file_inode_operations = {
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct inode_operations v9fs_symlink_inode_operations = {
	.get_link = v9fs_vfs_get_link,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

