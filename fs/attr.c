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
#include <linux/capability.h>
#include <linux/fsnotify.h>
#include <linux/fcntl.h>
#include <linux/security.h>
#include <linux/richacl.h>

static int richacl_change_ok(struct inode *inode, int mask)
{
	if (!IS_RICHACL(inode))
		return -EPERM;

	if (inode->i_op->permission)
		return inode->i_op->permission(inode, mask);
	if (inode->i_op->get_richacl)
		return check_richacl(inode, mask);
	return -EPERM;
}

static bool inode_uid_change_ok(struct inode *inode, uid_t ia_uid)
{
	if (current_fsuid() == inode->i_uid && ia_uid == inode->i_uid)
		return true;
	if (current_fsuid() == ia_uid &&
	    richacl_change_ok(inode, MAY_TAKE_OWNERSHIP) == 0)
		return true;
	if (capable(CAP_CHOWN))
		return true;
	return false;
}

static bool inode_gid_change_ok(struct inode *inode, gid_t ia_gid)
{
	int in_group = in_group_p(ia_gid);
	if (current_fsuid() == inode->i_uid &&
	    (in_group || ia_gid == inode->i_gid))
		return true;
	if (in_group && richacl_change_ok(inode, MAY_TAKE_OWNERSHIP) == 0)
		return true;
	if (capable(CAP_CHOWN))
		return true;
	return false;
}

static bool inode_owner_permitted_or_capable(struct inode *inode, int mask)
{
	struct user_namespace *ns = inode_userns(inode);

	if (current_user_ns() == ns && current_fsuid() == inode->i_uid)
		return true;
	if (richacl_change_ok(inode, mask) == 0)
		return true;
	if (ns_capable(ns, CAP_FOWNER))
		return true;
	return false;
}

/**
 * inode_change_ok - check if attribute changes to an inode are allowed
 * @inode:	inode to check
 * @attr:	attributes to change
 *
 * Check if we are allowed to change the attributes contained in @attr
 * in the given inode.  This includes the normal unix access permission
 * checks, as well as checks for rlimits and others.
 *
 * Should be called as the first thing in ->setattr implementations,
 * possibly after taking additional locks.
 */
int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	/*
	 * First check size constraints.  These can't be overriden using
	 * ATTR_FORCE.
	 */
	if (ia_valid & ATTR_SIZE) {
		int error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;
	}

	/* If force is set do it anyway. */
	if (ia_valid & ATTR_FORCE)
		return 0;

	/* Make sure a caller can chown. */
	if (ia_valid & ATTR_UID) {
		if (!inode_uid_change_ok(inode, attr->ia_uid))
			return -EPERM;
	}

	/* Make sure caller can chgrp. */
	if (ia_valid & ATTR_GID) {
		if (!inode_gid_change_ok(inode, attr->ia_gid))
			return -EPERM;
	}

	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		if (!inode_owner_permitted_or_capable(inode, MAY_CHMOD))
			return -EPERM;
		/* Also check the setgid bit! */
		if (!in_group_p((ia_valid & ATTR_GID) ? attr->ia_gid :
				inode->i_gid) && !capable(CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time. */
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) {
		if (!inode_owner_permitted_or_capable(inode, MAY_SET_TIMES))
			return -EPERM;
	}

	return 0;
}
EXPORT_SYMBOL(inode_change_ok);

/**
 * inode_newsize_ok - may this inode be truncated to a given size
 * @inode:	the inode to be truncated
 * @offset:	the new size to assign to the inode
 * @Returns:	0 on success, -ve errno on failure
 *
 * inode_newsize_ok must be called with i_mutex held.
 *
 * inode_newsize_ok will check filesystem limits and ulimits to check that the
 * new inode size is within limits. inode_newsize_ok will also send SIGXFSZ
 * when necessary. Caller must not proceed with inode size change if failure is
 * returned. @inode must be a file (not directory), with appropriate
 * permissions to allow truncate (inode_newsize_ok does NOT check these
 * conditions).
 */
int inode_newsize_ok(const struct inode *inode, loff_t offset)
{
	if (inode->i_size < offset) {
		unsigned long limit;

		limit = rlimit(RLIMIT_FSIZE);
		if (limit != RLIM_INFINITY && offset > limit)
			goto out_sig;
		if (offset > inode->i_sb->s_maxbytes)
			goto out_big;
	} else {
		/*
		 * truncation of in-use swapfiles is disallowed - it would
		 * cause subsequent swapout to scribble on the now-freed
		 * blocks.
		 */
		if (IS_SWAPFILE(inode))
			return -ETXTBSY;
	}

	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
}
EXPORT_SYMBOL(inode_newsize_ok);

/**
 * setattr_copy - copy simple metadata updates into the generic inode
 * @inode:	the inode to be updated
 * @attr:	the new attributes
 *
 * setattr_copy must be called with i_mutex held.
 *
 * setattr_copy updates the inode's metadata with that specified
 * in attr. Noticeably missing is inode size update, which is more complex
 * as it requires pagecache updates.
 *
 * The inode is not marked as dirty after this operation. The rationale is
 * that for "simple" filesystems, the struct inode is the inode storage.
 * The caller is free to mark the inode dirty afterwards if needed.
 */
void setattr_copy(struct inode *inode, const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

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
}
EXPORT_SYMBOL(setattr_copy);

int notify_change(struct dentry * dentry, struct iattr * attr)
{
	struct inode *inode = dentry->d_inode;
	mode_t mode = inode->i_mode;
	int error;
	struct timespec now;
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_TIMES_SET)) {
		if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
			return -EPERM;
	}

	if ((ia_valid & ATTR_MODE)) {
		mode_t amode = attr->ia_mode;
		/* Flag setting protected by i_mutex */
		if (is_sxid(amode))
			inode->i_flags &= ~S_NOSEC;
	}

	now = current_fs_time(inode->i_sb);

	attr->ia_ctime = now;
	if (!(ia_valid & ATTR_ATIME_SET))
		attr->ia_atime = now;
	if (!(ia_valid & ATTR_MTIME_SET))
		attr->ia_mtime = now;
	if (ia_valid & ATTR_KILL_PRIV) {
		attr->ia_valid &= ~ATTR_KILL_PRIV;
		ia_valid &= ~ATTR_KILL_PRIV;
		error = security_inode_need_killpriv(dentry);
		if (error > 0)
			error = security_inode_killpriv(dentry);
		if (error)
			return error;
	}

	/*
	 * We now pass ATTR_KILL_S*ID to the lower level setattr function so
	 * that the function has the ability to reinterpret a mode change
	 * that's due to these bits. This adds an implicit restriction that
	 * no function will ever call notify_change with both ATTR_MODE and
	 * ATTR_KILL_S*ID set.
	 */
	if ((ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID)) &&
	    (ia_valid & ATTR_MODE))
		BUG();

	if (ia_valid & ATTR_KILL_SUID) {
		if (mode & S_ISUID) {
			ia_valid = attr->ia_valid |= ATTR_MODE;
			attr->ia_mode = (inode->i_mode & ~S_ISUID);
		}
	}
	if (ia_valid & ATTR_KILL_SGID) {
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = inode->i_mode;
			}
			attr->ia_mode &= ~S_ISGID;
		}
	}
	if (!(attr->ia_valid & ~(ATTR_KILL_SUID | ATTR_KILL_SGID)))
		return 0;

	error = security_inode_setattr(dentry, attr);
	if (error)
		return error;

	if (inode->i_op->setattr)
		error = inode->i_op->setattr(dentry, attr);
	else
		error = simple_setattr(dentry, attr);

	if (!error)
		fsnotify_change(dentry, ia_valid);

	return error;
}

EXPORT_SYMBOL(notify_change);
