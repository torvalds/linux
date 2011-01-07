/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/time.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/ctype.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/namei.h>

#include "smb_fs.h"
#include "smb_mount.h"
#include "smbno.h"

#include "smb_debug.h"
#include "proto.h"

static int smb_readdir(struct file *, void *, filldir_t);
static int smb_dir_open(struct inode *, struct file *);

static struct dentry *smb_lookup(struct inode *, struct dentry *, struct nameidata *);
static int smb_create(struct inode *, struct dentry *, int, struct nameidata *);
static int smb_mkdir(struct inode *, struct dentry *, int);
static int smb_rmdir(struct inode *, struct dentry *);
static int smb_unlink(struct inode *, struct dentry *);
static int smb_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);
static int smb_make_node(struct inode *,struct dentry *,int,dev_t);
static int smb_link(struct dentry *, struct inode *, struct dentry *);

const struct file_operations smb_dir_operations =
{
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= smb_readdir,
	.unlocked_ioctl	= smb_ioctl,
	.open		= smb_dir_open,
};

const struct inode_operations smb_dir_inode_operations =
{
	.create		= smb_create,
	.lookup		= smb_lookup,
	.unlink		= smb_unlink,
	.mkdir		= smb_mkdir,
	.rmdir		= smb_rmdir,
	.rename		= smb_rename,
	.getattr	= smb_getattr,
	.setattr	= smb_notify_change,
};

const struct inode_operations smb_dir_inode_operations_unix =
{
	.create		= smb_create,
	.lookup		= smb_lookup,
	.unlink		= smb_unlink,
	.mkdir		= smb_mkdir,
	.rmdir		= smb_rmdir,
	.rename		= smb_rename,
	.getattr	= smb_getattr,
	.setattr	= smb_notify_change,
	.symlink	= smb_symlink,
	.mknod		= smb_make_node,
	.link		= smb_link,
};

/*
 * Read a directory, using filldir to fill the dirent memory.
 * smb_proc_readdir does the actual reading from the smb server.
 *
 * The cache code is almost directly taken from ncpfs
 */
static int 
smb_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *dir = dentry->d_inode;
	struct smb_sb_info *server = server_from_dentry(dentry);
	union  smb_dir_cache *cache = NULL;
	struct smb_cache_control ctl;
	struct page *page = NULL;
	int result;

	ctl.page  = NULL;
	ctl.cache = NULL;

	VERBOSE("reading %s/%s, f_pos=%d\n",
		DENTRY_PATH(dentry),  (int) filp->f_pos);

	result = 0;

	lock_kernel();

	switch ((unsigned int) filp->f_pos) {
	case 0:
		if (filldir(dirent, ".", 1, 0, dir->i_ino, DT_DIR) < 0)
			goto out;
		filp->f_pos = 1;
		/* fallthrough */
	case 1:
		if (filldir(dirent, "..", 2, 1, parent_ino(dentry), DT_DIR) < 0)
			goto out;
		filp->f_pos = 2;
	}

	/*
	 * Make sure our inode is up-to-date.
	 */
	result = smb_revalidate_inode(dentry);
	if (result)
		goto out;


	page = grab_cache_page(&dir->i_data, 0);
	if (!page)
		goto read_really;

	ctl.cache = cache = kmap(page);
	ctl.head  = cache->head;

	if (!PageUptodate(page) || !ctl.head.eof) {
		VERBOSE("%s/%s, page uptodate=%d, eof=%d\n",
			 DENTRY_PATH(dentry), PageUptodate(page),ctl.head.eof);
		goto init_cache;
	}

	if (filp->f_pos == 2) {
		if (jiffies - ctl.head.time >= SMB_MAX_AGE(server))
			goto init_cache;

		/*
		 * N.B. ncpfs checks mtime of dentry too here, we don't.
		 *   1. common smb servers do not update mtime on dir changes
		 *   2. it requires an extra smb request
		 *      (revalidate has the same timeout as ctl.head.time)
		 *
		 * Instead smbfs invalidates its own cache on local changes
		 * and remote changes are not seen until timeout.
		 */
	}

	if (filp->f_pos > ctl.head.end)
		goto finished;

	ctl.fpos = filp->f_pos + (SMB_DIRCACHE_START - 2);
	ctl.ofs  = ctl.fpos / SMB_DIRCACHE_SIZE;
	ctl.idx  = ctl.fpos % SMB_DIRCACHE_SIZE;

	for (;;) {
		if (ctl.ofs != 0) {
			ctl.page = find_lock_page(&dir->i_data, ctl.ofs);
			if (!ctl.page)
				goto invalid_cache;
			ctl.cache = kmap(ctl.page);
			if (!PageUptodate(ctl.page))
				goto invalid_cache;
		}
		while (ctl.idx < SMB_DIRCACHE_SIZE) {
			struct dentry *dent;
			int res;

			dent = smb_dget_fpos(ctl.cache->dentry[ctl.idx],
					     dentry, filp->f_pos);
			if (!dent)
				goto invalid_cache;

			res = filldir(dirent, dent->d_name.name,
				      dent->d_name.len, filp->f_pos,
				      dent->d_inode->i_ino, DT_UNKNOWN);
			dput(dent);
			if (res)
				goto finished;
			filp->f_pos += 1;
			ctl.idx += 1;
			if (filp->f_pos > ctl.head.end)
				goto finished;
		}
		if (ctl.page) {
			kunmap(ctl.page);
			SetPageUptodate(ctl.page);
			unlock_page(ctl.page);
			page_cache_release(ctl.page);
			ctl.page = NULL;
		}
		ctl.idx  = 0;
		ctl.ofs += 1;
	}
invalid_cache:
	if (ctl.page) {
		kunmap(ctl.page);
		unlock_page(ctl.page);
		page_cache_release(ctl.page);
		ctl.page = NULL;
	}
	ctl.cache = cache;
init_cache:
	smb_invalidate_dircache_entries(dentry);
	ctl.head.time = jiffies;
	ctl.head.eof = 0;
	ctl.fpos = 2;
	ctl.ofs = 0;
	ctl.idx = SMB_DIRCACHE_START;
	ctl.filled = 0;
	ctl.valid  = 1;
read_really:
	result = server->ops->readdir(filp, dirent, filldir, &ctl);
	if (result == -ERESTARTSYS && page)
		ClearPageUptodate(page);
	if (ctl.idx == -1)
		goto invalid_cache;	/* retry */
	ctl.head.end = ctl.fpos - 1;
	ctl.head.eof = ctl.valid;
finished:
	if (page) {
		cache->head = ctl.head;
		kunmap(page);
		if (result != -ERESTARTSYS)
			SetPageUptodate(page);
		unlock_page(page);
		page_cache_release(page);
	}
	if (ctl.page) {
		kunmap(ctl.page);
		SetPageUptodate(ctl.page);
		unlock_page(ctl.page);
		page_cache_release(ctl.page);
	}
out:
	unlock_kernel();
	return result;
}

static int
smb_dir_open(struct inode *dir, struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct smb_sb_info *server;
	int error = 0;

	VERBOSE("(%s/%s)\n", dentry->d_parent->d_name.name,
		file->f_path.dentry->d_name.name);

	/*
	 * Directory timestamps in the core protocol aren't updated
	 * when a file is added, so we give them a very short TTL.
	 */
	lock_kernel();
	server = server_from_dentry(dentry);
	if (server->opt.protocol < SMB_PROTOCOL_LANMAN2) {
		unsigned long age = jiffies - SMB_I(dir)->oldmtime;
		if (age > 2*HZ)
			smb_invalid_dir_cache(dir);
	}

	/*
	 * Note: in order to allow the smbmount process to open the
	 * mount point, we only revalidate if the connection is valid or
	 * if the process is trying to access something other than the root.
	 */
	if (server->state == CONN_VALID || !IS_ROOT(dentry))
		error = smb_revalidate_inode(dentry);
	unlock_kernel();
	return error;
}

/*
 * Dentry operations routines
 */
static int smb_lookup_validate(struct dentry *, struct nameidata *);
static int smb_hash_dentry(const struct dentry *, const struct inode *,
		struct qstr *);
static int smb_compare_dentry(const struct dentry *,
		const struct inode *,
		const struct dentry *, const struct inode *,
		unsigned int, const char *, const struct qstr *);
static int smb_delete_dentry(const struct dentry *);

static const struct dentry_operations smbfs_dentry_operations =
{
	.d_revalidate	= smb_lookup_validate,
	.d_hash		= smb_hash_dentry,
	.d_compare	= smb_compare_dentry,
	.d_delete	= smb_delete_dentry,
};

static const struct dentry_operations smbfs_dentry_operations_case =
{
	.d_revalidate	= smb_lookup_validate,
	.d_delete	= smb_delete_dentry,
};


/*
 * This is the callback when the dcache has a lookup hit.
 */
static int
smb_lookup_validate(struct dentry *dentry, struct nameidata *nd)
{
	struct smb_sb_info *server;
	struct inode *inode;
	unsigned long age;
	int valid;

	if (nd->flags & LOOKUP_RCU)
		return -ECHILD;

	server = server_from_dentry(dentry);
	inode = dentry->d_inode;
	age = jiffies - dentry->d_time;

	/*
	 * The default validation is based on dentry age:
	 * we believe in dentries for a few seconds.  (But each
	 * successful server lookup renews the timestamp.)
	 */
	valid = (age <= SMB_MAX_AGE(server));
#ifdef SMBFS_DEBUG_VERBOSE
	if (!valid)
		VERBOSE("%s/%s not valid, age=%lu\n", 
			DENTRY_PATH(dentry), age);
#endif

	if (inode) {
		lock_kernel();
		if (is_bad_inode(inode)) {
			PARANOIA("%s/%s has dud inode\n", DENTRY_PATH(dentry));
			valid = 0;
		} else if (!valid)
			valid = (smb_revalidate_inode(dentry) == 0);
		unlock_kernel();
	} else {
		/*
		 * What should we do for negative dentries?
		 */
	}
	return valid;
}

static int 
smb_hash_dentry(const struct dentry *dir, const struct inode *inode,
		struct qstr *this)
{
	unsigned long hash;
	int i;

	hash = init_name_hash();
	for (i=0; i < this->len ; i++)
		hash = partial_name_hash(tolower(this->name[i]), hash);
	this->hash = end_name_hash(hash);
  
	return 0;
}

static int
smb_compare_dentry(const struct dentry *parent,
		const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	int i, result = 1;

	if (len != name->len)
		goto out;
	for (i=0; i < len; i++) {
		if (tolower(str[i]) != tolower(name->name[i]))
			goto out;
	}
	result = 0;
out:
	return result;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad inodes.
 */
static int
smb_delete_dentry(const struct dentry *dentry)
{
	if (dentry->d_inode) {
		if (is_bad_inode(dentry->d_inode)) {
			PARANOIA("bad inode, unhashing %s/%s\n",
				 DENTRY_PATH(dentry));
			return 1;
		}
	} else {
		/* N.B. Unhash negative dentries? */
	}
	return 0;
}

/*
 * Initialize a new dentry
 */
void
smb_new_dentry(struct dentry *dentry)
{
	struct smb_sb_info *server = server_from_dentry(dentry);

	if (server->mnt->flags & SMB_MOUNT_CASE)
		d_set_d_op(dentry, &smbfs_dentry_operations_case);
	else
		d_set_d_op(dentry, &smbfs_dentry_operations);
	dentry->d_time = jiffies;
}


/*
 * Whenever a lookup succeeds, we know the parent directories
 * are all valid, so we want to update the dentry timestamps.
 * N.B. Move this to dcache?
 */
void
smb_renew_times(struct dentry * dentry)
{
	dget(dentry);
	dentry->d_time = jiffies;

	while (!IS_ROOT(dentry)) {
		struct dentry *parent = dget_parent(dentry);
		dput(dentry);
		dentry = parent;

		dentry->d_time = jiffies;
	}
	dput(dentry);
}

static struct dentry *
smb_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct smb_fattr finfo;
	struct inode *inode;
	int error;
	struct smb_sb_info *server;

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > SMB_MAXNAMELEN)
		goto out;

	/* Do not allow lookup of names with backslashes in */
	error = -EINVAL;
	if (memchr(dentry->d_name.name, '\\', dentry->d_name.len))
		goto out;

	lock_kernel();
	error = smb_proc_getattr(dentry, &finfo);
#ifdef SMBFS_PARANOIA
	if (error && error != -ENOENT)
		PARANOIA("find %s/%s failed, error=%d\n",
			 DENTRY_PATH(dentry), error);
#endif

	inode = NULL;
	if (error == -ENOENT)
		goto add_entry;
	if (!error) {
		error = -EACCES;
		finfo.f_ino = iunique(dentry->d_sb, 2);
		inode = smb_iget(dir->i_sb, &finfo);
		if (inode) {
	add_entry:
			server = server_from_dentry(dentry);
			if (server->mnt->flags & SMB_MOUNT_CASE)
				d_set_d_op(dentry, &smbfs_dentry_operations_case);
			else
				d_set_d_op(dentry, &smbfs_dentry_operations);

			d_add(dentry, inode);
			smb_renew_times(dentry);
			error = 0;
		}
	}
	unlock_kernel();
out:
	return ERR_PTR(error);
}

/*
 * This code is common to all routines creating a new inode.
 */
static int
smb_instantiate(struct dentry *dentry, __u16 fileid, int have_id)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	struct inode *inode;
	int error;
	struct smb_fattr fattr;

	VERBOSE("file %s/%s, fileid=%u\n", DENTRY_PATH(dentry), fileid);

	error = smb_proc_getattr(dentry, &fattr);
	if (error)
		goto out_close;

	smb_renew_times(dentry);
	fattr.f_ino = iunique(dentry->d_sb, 2);
	inode = smb_iget(dentry->d_sb, &fattr);
	if (!inode)
		goto out_no_inode;

	if (have_id) {
		struct smb_inode_info *ei = SMB_I(inode);
		ei->fileid = fileid;
		ei->access = SMB_O_RDWR;
		ei->open = server->generation;
	}
	d_instantiate(dentry, inode);
out:
	return error;

out_no_inode:
	error = -EACCES;
out_close:
	if (have_id) {
		PARANOIA("%s/%s failed, error=%d, closing %u\n",
			 DENTRY_PATH(dentry), error, fileid);
		smb_close_fileid(dentry, fileid);
	}
	goto out;
}

/* N.B. How should the mode argument be used? */
static int
smb_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	__u16 fileid;
	int error;
	struct iattr attr;

	VERBOSE("creating %s/%s, mode=%d\n", DENTRY_PATH(dentry), mode);

	lock_kernel();
	smb_invalid_dir_cache(dir);
	error = smb_proc_create(dentry, 0, get_seconds(), &fileid);
	if (!error) {
		if (server->opt.capabilities & SMB_CAP_UNIX) {
			/* Set attributes for new file */
			attr.ia_valid = ATTR_MODE;
			attr.ia_mode = mode;
			error = smb_proc_setattr_unix(dentry, &attr, 0, 0);
		}
		error = smb_instantiate(dentry, fileid, 1);
	} else {
		PARANOIA("%s/%s failed, error=%d\n",
			 DENTRY_PATH(dentry), error);
	}
	unlock_kernel();
	return error;
}

/* N.B. How should the mode argument be used? */
static int
smb_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int error;
	struct iattr attr;

	lock_kernel();
	smb_invalid_dir_cache(dir);
	error = smb_proc_mkdir(dentry);
	if (!error) {
		if (server->opt.capabilities & SMB_CAP_UNIX) {
			/* Set attributes for new directory */
			attr.ia_valid = ATTR_MODE;
			attr.ia_mode = mode;
			error = smb_proc_setattr_unix(dentry, &attr, 0, 0);
		}
		error = smb_instantiate(dentry, 0, 0);
	}
	unlock_kernel();
	return error;
}

static int
smb_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int error;

	/*
	 * Close the directory if it's open.
	 */
	lock_kernel();
	smb_close(inode);

	/*
	 * Check that nobody else is using the directory..
	 */
	error = -EBUSY;
	if (!d_unhashed(dentry))
		goto out;

	smb_invalid_dir_cache(dir);
	error = smb_proc_rmdir(dentry);

out:
	unlock_kernel();
	return error;
}

static int
smb_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	/*
	 * Close the file if it's open.
	 */
	lock_kernel();
	smb_close(dentry->d_inode);

	smb_invalid_dir_cache(dir);
	error = smb_proc_unlink(dentry);
	if (!error)
		smb_renew_times(dentry);
	unlock_kernel();
	return error;
}

static int
smb_rename(struct inode *old_dir, struct dentry *old_dentry,
	   struct inode *new_dir, struct dentry *new_dentry)
{
	int error;

	/*
	 * Close any open files, and check whether to delete the
	 * target before attempting the rename.
	 */
	lock_kernel();
	if (old_dentry->d_inode)
		smb_close(old_dentry->d_inode);
	if (new_dentry->d_inode) {
		smb_close(new_dentry->d_inode);
		error = smb_proc_unlink(new_dentry);
		if (error) {
			VERBOSE("unlink %s/%s, error=%d\n",
				DENTRY_PATH(new_dentry), error);
			goto out;
		}
		/* FIXME */
		d_delete(new_dentry);
	}

	smb_invalid_dir_cache(old_dir);
	smb_invalid_dir_cache(new_dir);
	error = smb_proc_mv(old_dentry, new_dentry);
	if (!error) {
		smb_renew_times(old_dentry);
		smb_renew_times(new_dentry);
	}
out:
	unlock_kernel();
	return error;
}

/*
 * FIXME: samba servers won't let you create device nodes unless uid/gid
 * matches the connection credentials (and we don't know which those are ...)
 */
static int
smb_make_node(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	int error;
	struct iattr attr;

	attr.ia_valid = ATTR_MODE | ATTR_UID | ATTR_GID;
	attr.ia_mode = mode;
	current_euid_egid(&attr.ia_uid, &attr.ia_gid);

	if (!new_valid_dev(dev))
		return -EINVAL;

	smb_invalid_dir_cache(dir);
	error = smb_proc_setattr_unix(dentry, &attr, MAJOR(dev), MINOR(dev));
	if (!error) {
		error = smb_instantiate(dentry, 0, 0);
	}
	return error;
}

/*
 * dentry = existing file
 * new_dentry = new file
 */
static int
smb_link(struct dentry *dentry, struct inode *dir, struct dentry *new_dentry)
{
	int error;

	DEBUG1("smb_link old=%s/%s new=%s/%s\n",
	       DENTRY_PATH(dentry), DENTRY_PATH(new_dentry));
	smb_invalid_dir_cache(dir);
	error = smb_proc_link(server_from_dentry(dentry), dentry, new_dentry);
	if (!error) {
		smb_renew_times(dentry);
		error = smb_instantiate(new_dentry, 0, 0);
	}
	return error;
}
