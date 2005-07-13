/*
 *  linux/fs/attr.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  changes by Thomas Schoebel-Theuer
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/fsnotify.h>
#include <linux/fcntl.h>
#include <linux/quotaops.h>
#include <linux/security.h>
#include <linux/time.h>

/* Taken over from the old code... */

/* POSIX UID/GID verification for setting inode attributes. */
int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	int retval = -EPERM;
	unsigned int ia_valid = attr->ia_valid;

	/* If force is set do it anyway. */
	if (ia_valid & ATTR_FORCE)
		goto fine;

	/* Make sure a caller can chown. */
	if ((ia_valid & ATTR_UID) &&
	    (current->fsuid != inode->i_uid ||
	     attr->ia_uid != inode->i_uid) && !capable(CAP_CHOWN))
		goto error;

	/* Make sure caller can chgrp. */
	if ((ia_valid & ATTR_GID) &&
	    (current->fsuid != inode->i_uid ||
	    (!in_group_p(attr->ia_gid) && attr->ia_gid != inode->i_gid)) &&
	    !capable(CAP_CHOWN))
		goto error;

	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			goto error;
		/* Also check the setgid bit! */
		if (!in_group_p((ia_valid & ATTR_GID) ? attr->ia_gid :
				inode->i_gid) && !capable(CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time. */
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET)) {
		if (current->fsuid != inode->i_uid && !capable(CAP_FOWNER))
			goto error;
	}
fine:
	retval = 0;
error:
	return retval;
}

EXPORT_SYMBOL(inode_change_ok);

int inode_setattr(struct inode * inode, struct iattr * attr)
{
	unsigned int ia_valid = attr->ia_valid;
	int error = 0;

	if (ia_valid & ATTR_SIZE) {
		if (attr->ia_size != i_size_read(inode)) {
			error = vmtruncate(inode, attr->ia_size);
			if (error || (ia_valid == ATTR_SIZE))
				goto out;
		} else {
			/*
			 * We skipped the truncate but must still update
			 * timestamps
			 */
			ia_valid |= ATTR_MTIME|ATTR_CTIME;
		}
	}

	if (ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = timespec_trunc(attr->ia_atime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MTIME)
		inode->i_mtime = timespec_trunc(attr->ia_mtime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = timespec_trunc(attr->ia_ctime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		inode->i_mode = mode;
	}
	mark_inode_dirty(inode);
out:
	return error;
}
EXPORT_SYMBOL(inode_setattr);

int notify_change(struct dentry * dentry, struct iattr * attr)
{
	struct inode *inode = dentry->d_inode;
	mode_t mode;
	int error;
	struct timespec now;
	unsigned int ia_valid = attr->ia_valid;

	if (!inode)
		BUG();

	mode = inode->i_mode;
	now = current_fs_time(inode->i_sb);

	attr->ia_ctime = now;
	if (!(ia_valid & ATTR_ATIME_SET))
		attr->ia_atime = now;
	if (!(ia_valid & ATTR_MTIME_SET))
		attr->ia_mtime = now;
	if (ia_valid & ATTR_KILL_SUID) {
		attr->ia_valid &= ~ATTR_KILL_SUID;
		if (mode & S_ISUID) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = inode->i_mode;
			}
			attr->ia_mode &= ~S_ISUID;
		}
	}
	if (ia_valid & ATTR_KILL_SGID) {
		attr->ia_valid &= ~ ATTR_KILL_SGID;
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = inode->i_mode;
			}
			attr->ia_mode &= ~S_ISGID;
		}
	}
	if (!attr->ia_valid)
		return 0;

	if (ia_valid & ATTR_SIZE)
		down_write(&dentry->d_inode->i_alloc_sem);

	if (inode->i_op && inode->i_op->setattr) {
		error = security_inode_setattr(dentry, attr);
		if (!error)
			error = inode->i_op->setattr(dentry, attr);
	} else {
		error = inode_change_ok(inode, attr);
		if (!error)
			error = security_inode_setattr(dentry, attr);
		if (!error) {
			if ((ia_valid & ATTR_UID && attr->ia_uid != inode->i_uid) ||
			    (ia_valid & ATTR_GID && attr->ia_gid != inode->i_gid))
				error = DQUOT_TRANSFER(inode, attr) ? -EDQUOT : 0;
			if (!error)
				error = inode_setattr(inode, attr);
		}
	}

	if (ia_valid & ATTR_SIZE)
		up_write(&dentry->d_inode->i_alloc_sem);

	if (!error)
		fsnotify_change(dentry, ia_valid);

	return error;
}

EXPORT_SYMBOL(notify_change);
