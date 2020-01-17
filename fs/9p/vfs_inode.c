// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/9p/vfs_iyesde.c
 *
 * This file contains vfs iyesde ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/erryes.h>
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

static const struct iyesde_operations v9fs_dir_iyesde_operations;
static const struct iyesde_operations v9fs_dir_iyesde_operations_dotu;
static const struct iyesde_operations v9fs_file_iyesde_operations;
static const struct iyesde_operations v9fs_symlink_iyesde_operations;

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
		if (v9ses->yesdev == 0) {
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
 * @rdev: major number, miyesr number in case of device files.
 *
 */
static umode_t p9mode2unixmode(struct v9fs_session_info *v9ses,
			       struct p9_wstat *stat, dev_t *rdev)
{
	int res;
	u32 mode = stat->mode;

	*rdev = 0;
	res = p9mode2perm(v9ses, stat);

	if ((mode & P9_DMDIR) == P9_DMDIR)
		res |= S_IFDIR;
	else if ((mode & P9_DMSYMLINK) && (v9fs_proto_dotu(v9ses)))
		res |= S_IFLNK;
	else if ((mode & P9_DMSOCKET) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->yesdev == 0))
		res |= S_IFSOCK;
	else if ((mode & P9_DMNAMEDPIPE) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->yesdev == 0))
		res |= S_IFIFO;
	else if ((mode & P9_DMDEVICE) && (v9fs_proto_dotu(v9ses))
		 && (v9ses->yesdev == 0)) {
		char type = 0, ext[32];
		int major = -1, miyesr = -1;

		strlcpy(ext, stat->extension, sizeof(ext));
		sscanf(ext, "%c %i %i", &type, &major, &miyesr);
		switch (type) {
		case 'c':
			res |= S_IFCHR;
			break;
		case 'b':
			res |= S_IFBLK;
			break;
		default:
			p9_debug(P9_DEBUG_ERROR, "Unkyeswn special type %c %s\n",
				 type, stat->extension);
		};
		*rdev = MKDEV(major, miyesr);
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
 * v9fs_alloc_iyesde - helper function to allocate an iyesde
 *
 */
struct iyesde *v9fs_alloc_iyesde(struct super_block *sb)
{
	struct v9fs_iyesde *v9iyesde;
	v9iyesde = (struct v9fs_iyesde *)kmem_cache_alloc(v9fs_iyesde_cache,
							GFP_KERNEL);
	if (!v9iyesde)
		return NULL;
#ifdef CONFIG_9P_FSCACHE
	v9iyesde->fscache = NULL;
	mutex_init(&v9iyesde->fscache_lock);
#endif
	v9iyesde->writeback_fid = NULL;
	v9iyesde->cache_validity = 0;
	mutex_init(&v9iyesde->v_mutex);
	return &v9iyesde->vfs_iyesde;
}

/**
 * v9fs_free_iyesde - destroy an iyesde
 *
 */

void v9fs_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(v9fs_iyesde_cache, V9FS_I(iyesde));
}

int v9fs_init_iyesde(struct v9fs_session_info *v9ses,
		    struct iyesde *iyesde, umode_t mode, dev_t rdev)
{
	int err = 0;

	iyesde_init_owner(iyesde, NULL, mode);
	iyesde->i_blocks = 0;
	iyesde->i_rdev = rdev;
	iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	iyesde->i_mapping->a_ops = &v9fs_addr_operations;

	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
		if (v9fs_proto_dotl(v9ses)) {
			iyesde->i_op = &v9fs_file_iyesde_operations_dotl;
		} else if (v9fs_proto_dotu(v9ses)) {
			iyesde->i_op = &v9fs_file_iyesde_operations;
		} else {
			p9_debug(P9_DEBUG_ERROR,
				 "special files without extended mode\n");
			err = -EINVAL;
			goto error;
		}
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
		break;
	case S_IFREG:
		if (v9fs_proto_dotl(v9ses)) {
			iyesde->i_op = &v9fs_file_iyesde_operations_dotl;
			if (v9ses->cache == CACHE_LOOSE ||
			    v9ses->cache == CACHE_FSCACHE)
				iyesde->i_fop =
					&v9fs_cached_file_operations_dotl;
			else if (v9ses->cache == CACHE_MMAP)
				iyesde->i_fop = &v9fs_mmap_file_operations_dotl;
			else
				iyesde->i_fop = &v9fs_file_operations_dotl;
		} else {
			iyesde->i_op = &v9fs_file_iyesde_operations;
			if (v9ses->cache == CACHE_LOOSE ||
			    v9ses->cache == CACHE_FSCACHE)
				iyesde->i_fop =
					&v9fs_cached_file_operations;
			else if (v9ses->cache == CACHE_MMAP)
				iyesde->i_fop = &v9fs_mmap_file_operations;
			else
				iyesde->i_fop = &v9fs_file_operations;
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
			iyesde->i_op = &v9fs_symlink_iyesde_operations_dotl;
		else
			iyesde->i_op = &v9fs_symlink_iyesde_operations;

		break;
	case S_IFDIR:
		inc_nlink(iyesde);
		if (v9fs_proto_dotl(v9ses))
			iyesde->i_op = &v9fs_dir_iyesde_operations_dotl;
		else if (v9fs_proto_dotu(v9ses))
			iyesde->i_op = &v9fs_dir_iyesde_operations_dotu;
		else
			iyesde->i_op = &v9fs_dir_iyesde_operations;

		if (v9fs_proto_dotl(v9ses))
			iyesde->i_fop = &v9fs_dir_operations_dotl;
		else
			iyesde->i_fop = &v9fs_dir_operations;

		break;
	default:
		p9_debug(P9_DEBUG_ERROR, "BAD mode 0x%hx S_IFMT 0x%x\n",
			 mode, mode & S_IFMT);
		err = -EINVAL;
		goto error;
	}
error:
	return err;

}

/**
 * v9fs_get_iyesde - helper function to setup an iyesde
 * @sb: superblock
 * @mode: mode to setup iyesde with
 *
 */

struct iyesde *v9fs_get_iyesde(struct super_block *sb, umode_t mode, dev_t rdev)
{
	int err;
	struct iyesde *iyesde;
	struct v9fs_session_info *v9ses = sb->s_fs_info;

	p9_debug(P9_DEBUG_VFS, "super block: %p mode: %ho\n", sb, mode);

	iyesde = new_iyesde(sb);
	if (!iyesde) {
		pr_warn("%s (%d): Problem allocating iyesde\n",
			__func__, task_pid_nr(current));
		return ERR_PTR(-ENOMEM);
	}
	err = v9fs_init_iyesde(v9ses, iyesde, mode, rdev);
	if (err) {
		iput(iyesde);
		return ERR_PTR(err);
	}
	return iyesde;
}

/*
static struct v9fs_fid*
v9fs_clone_walk(struct v9fs_session_info *v9ses, u32 fid, struct dentry *dentry)
{
	int err;
	int nfid;
	struct v9fs_fid *ret;
	struct v9fs_fcall *fcall;

	nfid = v9fs_get_idpool(&v9ses->fidpool);
	if (nfid < 0) {
		eprintk(KERN_WARNING, "yes free fids available\n");
		return ERR_PTR(-ENOSPC);
	}

	err = v9fs_t_walk(v9ses, fid, nfid, (char *) dentry->d_name.name,
		&fcall);

	if (err < 0) {
		if (fcall && fcall->id == RWALK)
			goto clunk_fid;

		PRINT_FCALL_ERROR("walk error", fcall);
		v9fs_put_idpool(nfid, &v9ses->fidpool);
		goto error;
	}

	kfree(fcall);
	fcall = NULL;
	ret = v9fs_fid_create(v9ses, nfid);
	if (!ret) {
		err = -ENOMEM;
		goto clunk_fid;
	}

	err = v9fs_fid_insert(ret, dentry);
	if (err < 0) {
		v9fs_fid_destroy(ret);
		goto clunk_fid;
	}

	return ret;

clunk_fid:
	v9fs_t_clunk(v9ses, nfid);

error:
	kfree(fcall);
	return ERR_PTR(err);
}
*/


/**
 * v9fs_clear_iyesde - release an iyesde
 * @iyesde: iyesde to release
 *
 */
void v9fs_evict_iyesde(struct iyesde *iyesde)
{
	struct v9fs_iyesde *v9iyesde = V9FS_I(iyesde);

	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	filemap_fdatawrite(&iyesde->i_data);

	v9fs_cache_iyesde_put_cookie(iyesde);
	/* clunk the fid stashed in writeback_fid */
	if (v9iyesde->writeback_fid) {
		p9_client_clunk(v9iyesde->writeback_fid);
		v9iyesde->writeback_fid = NULL;
	}
}

static int v9fs_test_iyesde(struct iyesde *iyesde, void *data)
{
	int umode;
	dev_t rdev;
	struct v9fs_iyesde *v9iyesde = V9FS_I(iyesde);
	struct p9_wstat *st = (struct p9_wstat *)data;
	struct v9fs_session_info *v9ses = v9fs_iyesde2v9ses(iyesde);

	umode = p9mode2unixmode(v9ses, st, &rdev);
	/* don't match iyesde of different type */
	if ((iyesde->i_mode & S_IFMT) != (umode & S_IFMT))
		return 0;

	/* compare qid details */
	if (memcmp(&v9iyesde->qid.version,
		   &st->qid.version, sizeof(v9iyesde->qid.version)))
		return 0;

	if (v9iyesde->qid.type != st->qid.type)
		return 0;

	if (v9iyesde->qid.path != st->qid.path)
		return 0;
	return 1;
}

static int v9fs_test_new_iyesde(struct iyesde *iyesde, void *data)
{
	return 0;
}

static int v9fs_set_iyesde(struct iyesde *iyesde,  void *data)
{
	struct v9fs_iyesde *v9iyesde = V9FS_I(iyesde);
	struct p9_wstat *st = (struct p9_wstat *)data;

	memcpy(&v9iyesde->qid, &st->qid, sizeof(st->qid));
	return 0;
}

static struct iyesde *v9fs_qid_iget(struct super_block *sb,
				   struct p9_qid *qid,
				   struct p9_wstat *st,
				   int new)
{
	dev_t rdev;
	int retval;
	umode_t umode;
	unsigned long i_iyes;
	struct iyesde *iyesde;
	struct v9fs_session_info *v9ses = sb->s_fs_info;
	int (*test)(struct iyesde *, void *);

	if (new)
		test = v9fs_test_new_iyesde;
	else
		test = v9fs_test_iyesde;

	i_iyes = v9fs_qid2iyes(qid);
	iyesde = iget5_locked(sb, i_iyes, test, v9fs_set_iyesde, st);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;
	/*
	 * initialize the iyesde with the stat info
	 * FIXME!! we may need support for stale iyesdes
	 * later.
	 */
	iyesde->i_iyes = i_iyes;
	umode = p9mode2unixmode(v9ses, st, &rdev);
	retval = v9fs_init_iyesde(v9ses, iyesde, umode, rdev);
	if (retval)
		goto error;

	v9fs_stat2iyesde(st, iyesde, sb, 0);
	v9fs_cache_iyesde_get_cookie(iyesde);
	unlock_new_iyesde(iyesde);
	return iyesde;
error:
	iget_failed(iyesde);
	return ERR_PTR(retval);

}

struct iyesde *
v9fs_iyesde_from_fid(struct v9fs_session_info *v9ses, struct p9_fid *fid,
		    struct super_block *sb, int new)
{
	struct p9_wstat *st;
	struct iyesde *iyesde = NULL;

	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return ERR_CAST(st);

	iyesde = v9fs_qid_iget(sb, &st->qid, st, new);
	p9stat_free(st);
	kfree(st);
	return iyesde;
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
 * If a directory had nlink <= 2 (including . and ..), then we should yest drop
 * the link count, which indicates the underlying exported fs doesn't maintain
 * nlink accurately. e.g.
 * - overlayfs sets nlink to 1 for merged dir
 * - ext4 (with dir_nlink feature enabled) sets nlink to 1 if a dir has more
 *   than EXT4_LINK_MAX (65000) links.
 *
 * @iyesde: iyesde whose nlink is being dropped
 */
static void v9fs_dec_count(struct iyesde *iyesde)
{
	if (!S_ISDIR(iyesde->i_mode) || iyesde->i_nlink > 2)
		drop_nlink(iyesde);
}

/**
 * v9fs_remove - helper function to remove files and directories
 * @dir: directory iyesde that is being deleted
 * @dentry:  dentry that is being deleted
 * @flags: removing a directory
 *
 */

static int v9fs_remove(struct iyesde *dir, struct dentry *dentry, int flags)
{
	struct iyesde *iyesde;
	int retval = -EOPNOTSUPP;
	struct p9_fid *v9fid, *dfid;
	struct v9fs_session_info *v9ses;

	p9_debug(P9_DEBUG_VFS, "iyesde: %p dentry: %p rmdir: %x\n",
		 dir, dentry, flags);

	v9ses = v9fs_iyesde2v9ses(dir);
	iyesde = d_iyesde(dentry);
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid)) {
		retval = PTR_ERR(dfid);
		p9_debug(P9_DEBUG_VFS, "fid lookup failed %d\n", retval);
		return retval;
	}
	if (v9fs_proto_dotl(v9ses))
		retval = p9_client_unlinkat(dfid, dentry->d_name.name,
					    v9fs_at_to_dotl_flags(flags));
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
			clear_nlink(iyesde);
			v9fs_dec_count(dir);
		} else
			v9fs_dec_count(iyesde);

		v9fs_invalidate_iyesde_attr(iyesde);
		v9fs_invalidate_iyesde_attr(dir);
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
v9fs_create(struct v9fs_session_info *v9ses, struct iyesde *dir,
		struct dentry *dentry, char *extension, u32 perm, u8 mode)
{
	int err;
	const unsigned char *name;
	struct p9_fid *dfid, *ofid, *fid;
	struct iyesde *iyesde;

	p9_debug(P9_DEBUG_VFS, "name %pd\n", dentry);

	err = 0;
	ofid = NULL;
	fid = NULL;
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
		return ERR_PTR(err);
	}

	err = p9_client_fcreate(ofid, name, perm, mode, extension);
	if (err < 0) {
		p9_debug(P9_DEBUG_VFS, "p9_client_fcreate failed %d\n", err);
		goto error;
	}

	if (!(perm & P9_DMLINK)) {
		/* yesw walk from the parent so we can get uyespened fid */
		fid = p9_client_walk(dfid, 1, &name, 1);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			p9_debug(P9_DEBUG_VFS,
				   "p9_client_walk failed %d\n", err);
			fid = NULL;
			goto error;
		}
		/*
		 * instantiate iyesde and assign the uyespened fid to the dentry
		 */
		iyesde = v9fs_get_new_iyesde_from_fid(v9ses, fid, dir->i_sb);
		if (IS_ERR(iyesde)) {
			err = PTR_ERR(iyesde);
			p9_debug(P9_DEBUG_VFS,
				   "iyesde creation failed %d\n", err);
			goto error;
		}
		v9fs_fid_add(dentry, fid);
		d_instantiate(dentry, iyesde);
	}
	return ofid;
error:
	if (ofid)
		p9_client_clunk(ofid);

	if (fid)
		p9_client_clunk(fid);

	return ERR_PTR(err);
}

/**
 * v9fs_vfs_create - VFS hook to create a regular file
 *
 * open(.., O_CREAT) is handled in v9fs_vfs_atomic_open().  This is only called
 * for mkyesd(2).
 *
 * @dir: directory iyesde that is being created
 * @dentry:  dentry that is being deleted
 * @mode: create permissions
 *
 */

static int
v9fs_vfs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	struct v9fs_session_info *v9ses = v9fs_iyesde2v9ses(dir);
	u32 perm = unixmode2p9mode(v9ses, mode);
	struct p9_fid *fid;

	/* P9_OEXCL? */
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm, P9_ORDWR);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_invalidate_iyesde_attr(dir);
	p9_client_clunk(fid);

	return 0;
}

/**
 * v9fs_vfs_mkdir - VFS mkdir hook to create a directory
 * @dir:  iyesde that is being unlinked
 * @dentry: dentry that is being unlinked
 * @mode: mode for new directory
 *
 */

static int v9fs_vfs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	int err;
	u32 perm;
	struct p9_fid *fid;
	struct v9fs_session_info *v9ses;

	p9_debug(P9_DEBUG_VFS, "name %pd\n", dentry);
	err = 0;
	v9ses = v9fs_iyesde2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode | S_IFDIR);
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm, P9_OREAD);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
	} else {
		inc_nlink(dir);
		v9fs_invalidate_iyesde_attr(dir);
	}

	if (fid)
		p9_client_clunk(fid);

	return err;
}

/**
 * v9fs_vfs_lookup - VFS lookup hook to "walk" to a new iyesde
 * @dir:  iyesde that is being walked from
 * @dentry: dentry that is being walked to?
 * @flags: lookup flags (unused)
 *
 */

struct dentry *v9fs_vfs_lookup(struct iyesde *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct dentry *res;
	struct v9fs_session_info *v9ses;
	struct p9_fid *dfid, *fid;
	struct iyesde *iyesde;
	const unsigned char *name;

	p9_debug(P9_DEBUG_VFS, "dir: %p dentry: (%pd) %p flags: %x\n",
		 dir, dentry, dentry, flags);

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	v9ses = v9fs_iyesde2v9ses(dir);
	/* We can walk d_parent because we hold the dir->i_mutex */
	dfid = v9fs_parent_fid(dentry);
	if (IS_ERR(dfid))
		return ERR_CAST(dfid);

	/*
	 * Make sure we don't use a wrong iyesde due to parallel
	 * unlink. For cached mode create calls request for new
	 * iyesde. But with cache disabled, lookup should do this.
	 */
	name = dentry->d_name.name;
	fid = p9_client_walk(dfid, 1, &name, 1);
	if (fid == ERR_PTR(-ENOENT))
		iyesde = NULL;
	else if (IS_ERR(fid))
		iyesde = ERR_CAST(fid);
	else if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		iyesde = v9fs_get_iyesde_from_fid(v9ses, fid, dir->i_sb);
	else
		iyesde = v9fs_get_new_iyesde_from_fid(v9ses, fid, dir->i_sb);
	/*
	 * If we had a rename on the server and a parallel lookup
	 * for the new name, then make sure we instantiate with
	 * the new name. ie look up for a/b, while on server somebody
	 * moved b under k and client parallely did a lookup for
	 * k/b.
	 */
	res = d_splice_alias(iyesde, dentry);
	if (!IS_ERR(fid)) {
		if (!res)
			v9fs_fid_add(dentry, fid);
		else if (!IS_ERR(res))
			v9fs_fid_add(res, fid);
		else
			p9_client_clunk(fid);
	}
	return res;
}

static int
v9fs_vfs_atomic_open(struct iyesde *dir, struct dentry *dentry,
		     struct file *file, unsigned flags, umode_t mode)
{
	int err;
	u32 perm;
	struct v9fs_iyesde *v9iyesde;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid, *iyesde_fid;
	struct dentry *res = NULL;

	if (d_in_lookup(dentry)) {
		res = v9fs_vfs_lookup(dir, dentry, 0);
		if (IS_ERR(res))
			return PTR_ERR(res);

		if (res)
			dentry = res;
	}

	/* Only creates */
	if (!(flags & O_CREAT) || d_really_is_positive(dentry))
		return finish_yes_open(file, res);

	err = 0;

	v9ses = v9fs_iyesde2v9ses(dir);
	perm = unixmode2p9mode(v9ses, mode);
	fid = v9fs_create(v9ses, dir, dentry, NULL, perm,
				v9fs_uflags2omode(flags,
						v9fs_proto_dotu(v9ses)));
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		fid = NULL;
		goto error;
	}

	v9fs_invalidate_iyesde_attr(dir);
	v9iyesde = V9FS_I(d_iyesde(dentry));
	mutex_lock(&v9iyesde->v_mutex);
	if ((v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) &&
	    !v9iyesde->writeback_fid &&
	    ((flags & O_ACCMODE) != O_RDONLY)) {
		/*
		 * clone a fid and add it to writeback_fid
		 * we do it during open time instead of
		 * page dirty time via write_begin/page_mkwrite
		 * because we want write after unlink usecase
		 * to work.
		 */
		iyesde_fid = v9fs_writeback_fid(dentry);
		if (IS_ERR(iyesde_fid)) {
			err = PTR_ERR(iyesde_fid);
			mutex_unlock(&v9iyesde->v_mutex);
			goto error;
		}
		v9iyesde->writeback_fid = (void *) iyesde_fid;
	}
	mutex_unlock(&v9iyesde->v_mutex);
	err = finish_open(file, dentry, generic_file_open);
	if (err)
		goto error;

	file->private_data = fid;
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE)
		v9fs_cache_iyesde_set_cookie(d_iyesde(dentry), file);

	file->f_mode |= FMODE_CREATED;
out:
	dput(res);
	return err;

error:
	if (fid)
		p9_client_clunk(fid);
	goto out;
}

/**
 * v9fs_vfs_unlink - VFS unlink hook to delete an iyesde
 * @i:  iyesde that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

int v9fs_vfs_unlink(struct iyesde *i, struct dentry *d)
{
	return v9fs_remove(i, d, 0);
}

/**
 * v9fs_vfs_rmdir - VFS unlink hook to delete a directory
 * @i:  iyesde that is being unlinked
 * @d: dentry that is being unlinked
 *
 */

int v9fs_vfs_rmdir(struct iyesde *i, struct dentry *d)
{
	return v9fs_remove(i, d, AT_REMOVEDIR);
}

/**
 * v9fs_vfs_rename - VFS hook to rename an iyesde
 * @old_dir:  old dir iyesde
 * @old_dentry: old dentry
 * @new_dir: new dir iyesde
 * @new_dentry: new dentry
 *
 */

int
v9fs_vfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		struct iyesde *new_dir, struct dentry *new_dentry,
		unsigned int flags)
{
	int retval;
	struct iyesde *old_iyesde;
	struct iyesde *new_iyesde;
	struct v9fs_session_info *v9ses;
	struct p9_fid *oldfid;
	struct p9_fid *olddirfid;
	struct p9_fid *newdirfid;
	struct p9_wstat wstat;

	if (flags)
		return -EINVAL;

	p9_debug(P9_DEBUG_VFS, "\n");
	retval = 0;
	old_iyesde = d_iyesde(old_dentry);
	new_iyesde = d_iyesde(new_dentry);
	v9ses = v9fs_iyesde2v9ses(old_iyesde);
	oldfid = v9fs_fid_lookup(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	olddirfid = clone_fid(v9fs_parent_fid(old_dentry));
	if (IS_ERR(olddirfid)) {
		retval = PTR_ERR(olddirfid);
		goto done;
	}

	newdirfid = clone_fid(v9fs_parent_fid(new_dentry));
	if (IS_ERR(newdirfid)) {
		retval = PTR_ERR(newdirfid);
		goto clunk_olddir;
	}

	down_write(&v9ses->rename_sem);
	if (v9fs_proto_dotl(v9ses)) {
		retval = p9_client_renameat(olddirfid, old_dentry->d_name.name,
					    newdirfid, new_dentry->d_name.name);
		if (retval == -EOPNOTSUPP)
			retval = p9_client_rename(oldfid, newdirfid,
						  new_dentry->d_name.name);
		if (retval != -EOPNOTSUPP)
			goto clunk_newdir;
	}
	if (old_dentry->d_parent != new_dentry->d_parent) {
		/*
		 * 9P .u can only handle file rename in the same directory
		 */

		p9_debug(P9_DEBUG_ERROR, "old dir and new dir are different\n");
		retval = -EXDEV;
		goto clunk_newdir;
	}
	v9fs_blank_wstat(&wstat);
	wstat.muid = v9ses->uname;
	wstat.name = new_dentry->d_name.name;
	retval = p9_client_wstat(oldfid, &wstat);

clunk_newdir:
	if (!retval) {
		if (new_iyesde) {
			if (S_ISDIR(new_iyesde->i_mode))
				clear_nlink(new_iyesde);
			else
				v9fs_dec_count(new_iyesde);
		}
		if (S_ISDIR(old_iyesde->i_mode)) {
			if (!new_iyesde)
				inc_nlink(new_dir);
			v9fs_dec_count(old_dir);
		}
		v9fs_invalidate_iyesde_attr(old_iyesde);
		v9fs_invalidate_iyesde_attr(old_dir);
		v9fs_invalidate_iyesde_attr(new_dir);

		/* successful rename */
		d_move(old_dentry, new_dentry);
	}
	up_write(&v9ses->rename_sem);
	p9_client_clunk(newdirfid);

clunk_olddir:
	p9_client_clunk(olddirfid);

done:
	return retval;
}

/**
 * v9fs_vfs_getattr - retrieve file metadata
 * @path: Object to query
 * @stat: metadata structure to populate
 * @request_mask: Mask of STATX_xxx flags indicating the caller's interests
 * @flags: AT_STATX_xxx setting
 *
 */

static int
v9fs_vfs_getattr(const struct path *path, struct kstat *stat,
		 u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;

	p9_debug(P9_DEBUG_VFS, "dentry: %p\n", dentry);
	v9ses = v9fs_dentry2v9ses(dentry);
	if (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) {
		generic_fillattr(d_iyesde(dentry), stat);
		return 0;
	}
	fid = v9fs_fid_lookup(dentry);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);

	v9fs_stat2iyesde(st, d_iyesde(dentry), dentry->d_sb, 0);
	generic_fillattr(d_iyesde(dentry), stat);

	p9stat_free(st);
	kfree(st);
	return 0;
}

/**
 * v9fs_vfs_setattr - set file metadata
 * @dentry: file whose metadata to set
 * @iattr: metadata assignment structure
 *
 */

static int v9fs_vfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int retval;
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat wstat;

	p9_debug(P9_DEBUG_VFS, "\n");
	retval = setattr_prepare(dentry, iattr);
	if (retval)
		return retval;

	retval = -EPERM;
	v9ses = v9fs_dentry2v9ses(dentry);
	fid = v9fs_fid_lookup(dentry);
	if(IS_ERR(fid))
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
		filemap_write_and_wait(d_iyesde(dentry)->i_mapping);

	retval = p9_client_wstat(fid, &wstat);
	if (retval < 0)
		return retval;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(d_iyesde(dentry)))
		truncate_setsize(d_iyesde(dentry), iattr->ia_size);

	v9fs_invalidate_iyesde_attr(d_iyesde(dentry));

	setattr_copy(d_iyesde(dentry), iattr);
	mark_iyesde_dirty(d_iyesde(dentry));
	return 0;
}

/**
 * v9fs_stat2iyesde - populate an iyesde structure with mistat info
 * @stat: Plan 9 metadata (mistat) structure
 * @iyesde: iyesde to populate
 * @sb: superblock of filesystem
 * @flags: control flags (e.g. V9FS_STAT2INODE_KEEP_ISIZE)
 *
 */

void
v9fs_stat2iyesde(struct p9_wstat *stat, struct iyesde *iyesde,
		 struct super_block *sb, unsigned int flags)
{
	umode_t mode;
	char ext[32];
	char tag_name[14];
	unsigned int i_nlink;
	struct v9fs_session_info *v9ses = sb->s_fs_info;
	struct v9fs_iyesde *v9iyesde = V9FS_I(iyesde);

	set_nlink(iyesde, 1);

	iyesde->i_atime.tv_sec = stat->atime;
	iyesde->i_mtime.tv_sec = stat->mtime;
	iyesde->i_ctime.tv_sec = stat->mtime;

	iyesde->i_uid = v9ses->dfltuid;
	iyesde->i_gid = v9ses->dfltgid;

	if (v9fs_proto_dotu(v9ses)) {
		iyesde->i_uid = stat->n_uid;
		iyesde->i_gid = stat->n_gid;
	}
	if ((S_ISREG(iyesde->i_mode)) || (S_ISDIR(iyesde->i_mode))) {
		if (v9fs_proto_dotu(v9ses) && (stat->extension[0] != '\0')) {
			/*
			 * Hadlink support got added later to
			 * to the .u extension. So there can be
			 * server out there that doesn't support
			 * this even with .u extension. So check
			 * for yesn NULL stat->extension
			 */
			strlcpy(ext, stat->extension, sizeof(ext));
			/* HARDLINKCOUNT %u */
			sscanf(ext, "%13s %u", tag_name, &i_nlink);
			if (!strncmp(tag_name, "HARDLINKCOUNT", 13))
				set_nlink(iyesde, i_nlink);
		}
	}
	mode = p9mode2perm(v9ses, stat);
	mode |= iyesde->i_mode & ~S_IALLUGO;
	iyesde->i_mode = mode;

	if (!(flags & V9FS_STAT2INODE_KEEP_ISIZE))
		v9fs_i_size_write(iyesde, stat->length);
	/* yest real number of blocks, but 512 byte ones ... */
	iyesde->i_blocks = (stat->length + 512 - 1) >> 9;
	v9iyesde->cache_validity &= ~V9FS_INO_INVALID_ATTR;
}

/**
 * v9fs_qid2iyes - convert qid into iyesde number
 * @qid: qid to hash
 *
 * BUG: potential for iyesde number collisions?
 */

iyes_t v9fs_qid2iyes(struct p9_qid *qid)
{
	u64 path = qid->path + 2;
	iyes_t i = 0;

	if (sizeof(iyes_t) == sizeof(path))
		memcpy(&i, &path, sizeof(iyes_t));
	else
		i = (iyes_t) (path ^ (path >> 32));

	return i;
}

/**
 * v9fs_vfs_get_link - follow a symlink path
 * @dentry: dentry for symlink
 * @iyesde: iyesde for symlink
 * @done: delayed call for when we are done with the return value
 */

static const char *v9fs_vfs_get_link(struct dentry *dentry,
				     struct iyesde *iyesde,
				     struct delayed_call *done)
{
	struct v9fs_session_info *v9ses;
	struct p9_fid *fid;
	struct p9_wstat *st;
	char *res;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	v9ses = v9fs_dentry2v9ses(dentry);
	fid = v9fs_fid_lookup(dentry);
	p9_debug(P9_DEBUG_VFS, "%pd\n", dentry);

	if (IS_ERR(fid))
		return ERR_CAST(fid);

	if (!v9fs_proto_dotu(v9ses))
		return ERR_PTR(-EBADF);

	st = p9_client_stat(fid);
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
 * @dir: iyesde to create special file in
 * @dentry: dentry to create
 * @perm: mode to create special file
 * @extension: 9p2000.u format extension string representing special file
 *
 */

static int v9fs_vfs_mkspecial(struct iyesde *dir, struct dentry *dentry,
	u32 perm, const char *extension)
{
	struct p9_fid *fid;
	struct v9fs_session_info *v9ses;

	v9ses = v9fs_iyesde2v9ses(dir);
	if (!v9fs_proto_dotu(v9ses)) {
		p9_debug(P9_DEBUG_ERROR, "yest extended\n");
		return -EPERM;
	}

	fid = v9fs_create(v9ses, dir, dentry, (char *) extension, perm,
								P9_OREAD);
	if (IS_ERR(fid))
		return PTR_ERR(fid);

	v9fs_invalidate_iyesde_attr(dir);
	p9_client_clunk(fid);
	return 0;
}

/**
 * v9fs_vfs_symlink - helper function to create symlinks
 * @dir: directory iyesde containing symlink
 * @dentry: dentry for symlink
 * @symname: symlink data
 *
 * See Also: 9P2000.u RFC for more information
 *
 */

static int
v9fs_vfs_symlink(struct iyesde *dir, struct dentry *dentry, const char *symname)
{
	p9_debug(P9_DEBUG_VFS, " %lu,%pd,%s\n",
		 dir->i_iyes, dentry, symname);

	return v9fs_vfs_mkspecial(dir, dentry, P9_DMSYMLINK, symname);
}

#define U32_MAX_DIGITS 10

/**
 * v9fs_vfs_link - create a hardlink
 * @old_dentry: dentry for file to link to
 * @dir: iyesde destination for new link
 * @dentry: dentry for link
 *
 */

static int
v9fs_vfs_link(struct dentry *old_dentry, struct iyesde *dir,
	      struct dentry *dentry)
{
	int retval;
	char name[1 + U32_MAX_DIGITS + 2]; /* sign + number + \n + \0 */
	struct p9_fid *oldfid;

	p9_debug(P9_DEBUG_VFS, " %lu,%pd,%pd\n",
		 dir->i_iyes, dentry, old_dentry);

	oldfid = v9fs_fid_clone(old_dentry);
	if (IS_ERR(oldfid))
		return PTR_ERR(oldfid);

	sprintf(name, "%d\n", oldfid->fid);
	retval = v9fs_vfs_mkspecial(dir, dentry, P9_DMLINK, name);
	if (!retval) {
		v9fs_refresh_iyesde(oldfid, d_iyesde(old_dentry));
		v9fs_invalidate_iyesde_attr(dir);
	}
	p9_client_clunk(oldfid);
	return retval;
}

/**
 * v9fs_vfs_mkyesd - create a special file
 * @dir: iyesde destination for new link
 * @dentry: dentry for file
 * @mode: mode for creation
 * @rdev: device associated with special file
 *
 */

static int
v9fs_vfs_mkyesd(struct iyesde *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct v9fs_session_info *v9ses = v9fs_iyesde2v9ses(dir);
	int retval;
	char name[2 + U32_MAX_DIGITS + 1 + U32_MAX_DIGITS + 1];
	u32 perm;

	p9_debug(P9_DEBUG_VFS, " %lu,%pd mode: %hx MAJOR: %u MINOR: %u\n",
		 dir->i_iyes, dentry, mode,
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

int v9fs_refresh_iyesde(struct p9_fid *fid, struct iyesde *iyesde)
{
	int umode;
	dev_t rdev;
	struct p9_wstat *st;
	struct v9fs_session_info *v9ses;
	unsigned int flags;

	v9ses = v9fs_iyesde2v9ses(iyesde);
	st = p9_client_stat(fid);
	if (IS_ERR(st))
		return PTR_ERR(st);
	/*
	 * Don't update iyesde if the file type is different
	 */
	umode = p9mode2unixmode(v9ses, st, &rdev);
	if ((iyesde->i_mode & S_IFMT) != (umode & S_IFMT))
		goto out;

	/*
	 * We don't want to refresh iyesde->i_size,
	 * because we may have cached data
	 */
	flags = (v9ses->cache == CACHE_LOOSE || v9ses->cache == CACHE_FSCACHE) ?
		V9FS_STAT2INODE_KEEP_ISIZE : 0;
	v9fs_stat2iyesde(st, iyesde, iyesde->i_sb, flags);
out:
	p9stat_free(st);
	kfree(st);
	return 0;
}

static const struct iyesde_operations v9fs_dir_iyesde_operations_dotu = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.atomic_open = v9fs_vfs_atomic_open,
	.symlink = v9fs_vfs_symlink,
	.link = v9fs_vfs_link,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mkyesd = v9fs_vfs_mkyesd,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct iyesde_operations v9fs_dir_iyesde_operations = {
	.create = v9fs_vfs_create,
	.lookup = v9fs_vfs_lookup,
	.atomic_open = v9fs_vfs_atomic_open,
	.unlink = v9fs_vfs_unlink,
	.mkdir = v9fs_vfs_mkdir,
	.rmdir = v9fs_vfs_rmdir,
	.mkyesd = v9fs_vfs_mkyesd,
	.rename = v9fs_vfs_rename,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct iyesde_operations v9fs_file_iyesde_operations = {
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

static const struct iyesde_operations v9fs_symlink_iyesde_operations = {
	.get_link = v9fs_vfs_get_link,
	.getattr = v9fs_vfs_getattr,
	.setattr = v9fs_vfs_setattr,
};

