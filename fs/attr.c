// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/attr.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  changes by Thomas Schoebel-Theuer
 */

#include <linux/export.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>
#include <linux/fsyestify.h>
#include <linux/fcntl.h>
#include <linux/security.h>
#include <linux/evm.h>
#include <linux/ima.h>

static bool chown_ok(const struct iyesde *iyesde, kuid_t uid)
{
	if (uid_eq(current_fsuid(), iyesde->i_uid) &&
	    uid_eq(uid, iyesde->i_uid))
		return true;
	if (capable_wrt_iyesde_uidgid(iyesde, CAP_CHOWN))
		return true;
	if (uid_eq(iyesde->i_uid, INVALID_UID) &&
	    ns_capable(iyesde->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

static bool chgrp_ok(const struct iyesde *iyesde, kgid_t gid)
{
	if (uid_eq(current_fsuid(), iyesde->i_uid) &&
	    (in_group_p(gid) || gid_eq(gid, iyesde->i_gid)))
		return true;
	if (capable_wrt_iyesde_uidgid(iyesde, CAP_CHOWN))
		return true;
	if (gid_eq(iyesde->i_gid, INVALID_GID) &&
	    ns_capable(iyesde->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

/**
 * setattr_prepare - check if attribute changes to a dentry are allowed
 * @dentry:	dentry to check
 * @attr:	attributes to change
 *
 * Check if we are allowed to change the attributes contained in @attr
 * in the given dentry.  This includes the yesrmal unix access permission
 * checks, as well as checks for rlimits and others. The function also clears
 * SGID bit from mode if user is yest allowed to set it. Also file capabilities
 * and IMA extended attributes are cleared if ATTR_KILL_PRIV is set.
 *
 * Should be called as the first thing in ->setattr implementations,
 * possibly after taking additional locks.
 */
int setattr_prepare(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	unsigned int ia_valid = attr->ia_valid;

	/*
	 * First check size constraints.  These can't be overriden using
	 * ATTR_FORCE.
	 */
	if (ia_valid & ATTR_SIZE) {
		int error = iyesde_newsize_ok(iyesde, attr->ia_size);
		if (error)
			return error;
	}

	/* If force is set do it anyway. */
	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	/* Make sure a caller can chown. */
	if ((ia_valid & ATTR_UID) && !chown_ok(iyesde, attr->ia_uid))
		return -EPERM;

	/* Make sure caller can chgrp. */
	if ((ia_valid & ATTR_GID) && !chgrp_ok(iyesde, attr->ia_gid))
		return -EPERM;

	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		if (!iyesde_owner_or_capable(iyesde))
			return -EPERM;
		/* Also check the setgid bit! */
		if (!in_group_p((ia_valid & ATTR_GID) ? attr->ia_gid :
				iyesde->i_gid) &&
		    !capable_wrt_iyesde_uidgid(iyesde, CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the iyesde time. */
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) {
		if (!iyesde_owner_or_capable(iyesde))
			return -EPERM;
	}

kill_priv:
	/* User has permission for the change */
	if (ia_valid & ATTR_KILL_PRIV) {
		int error;

		error = security_iyesde_killpriv(dentry);
		if (error)
			return error;
	}

	return 0;
}
EXPORT_SYMBOL(setattr_prepare);

/**
 * iyesde_newsize_ok - may this iyesde be truncated to a given size
 * @iyesde:	the iyesde to be truncated
 * @offset:	the new size to assign to the iyesde
 *
 * iyesde_newsize_ok must be called with i_mutex held.
 *
 * iyesde_newsize_ok will check filesystem limits and ulimits to check that the
 * new iyesde size is within limits. iyesde_newsize_ok will also send SIGXFSZ
 * when necessary. Caller must yest proceed with iyesde size change if failure is
 * returned. @iyesde must be a file (yest directory), with appropriate
 * permissions to allow truncate (iyesde_newsize_ok does NOT check these
 * conditions).
 *
 * Return: 0 on success, -ve erryes on failure
 */
int iyesde_newsize_ok(const struct iyesde *iyesde, loff_t offset)
{
	if (iyesde->i_size < offset) {
		unsigned long limit;

		limit = rlimit(RLIMIT_FSIZE);
		if (limit != RLIM_INFINITY && offset > limit)
			goto out_sig;
		if (offset > iyesde->i_sb->s_maxbytes)
			goto out_big;
	} else {
		/*
		 * truncation of in-use swapfiles is disallowed - it would
		 * cause subsequent swapout to scribble on the yesw-freed
		 * blocks.
		 */
		if (IS_SWAPFILE(iyesde))
			return -ETXTBSY;
	}

	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
}
EXPORT_SYMBOL(iyesde_newsize_ok);

/**
 * setattr_copy - copy simple metadata updates into the generic iyesde
 * @iyesde:	the iyesde to be updated
 * @attr:	the new attributes
 *
 * setattr_copy must be called with i_mutex held.
 *
 * setattr_copy updates the iyesde's metadata with that specified
 * in attr. Noticeably missing is iyesde size update, which is more complex
 * as it requires pagecache updates.
 *
 * The iyesde is yest marked as dirty after this operation. The rationale is
 * that for "simple" filesystems, the struct iyesde is the iyesde storage.
 * The caller is free to mark the iyesde dirty afterwards if needed.
 */
void setattr_copy(struct iyesde *iyesde, const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		iyesde->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		iyesde->i_gid = attr->ia_gid;
	if (ia_valid & ATTR_ATIME) {
		iyesde->i_atime = timestamp_truncate(attr->ia_atime,
						  iyesde);
	}
	if (ia_valid & ATTR_MTIME) {
		iyesde->i_mtime = timestamp_truncate(attr->ia_mtime,
						  iyesde);
	}
	if (ia_valid & ATTR_CTIME) {
		iyesde->i_ctime = timestamp_truncate(attr->ia_ctime,
						  iyesde);
	}
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(iyesde->i_gid) &&
		    !capable_wrt_iyesde_uidgid(iyesde, CAP_FSETID))
			mode &= ~S_ISGID;
		iyesde->i_mode = mode;
	}
}
EXPORT_SYMBOL(setattr_copy);

/**
 * yestify_change - modify attributes of a filesytem object
 * @dentry:	object affected
 * @attr:	new attributes
 * @delegated_iyesde: returns iyesde, if the iyesde is delegated
 *
 * The caller must hold the i_mutex on the affected object.
 *
 * If yestify_change discovers a delegation in need of breaking,
 * it will return -EWOULDBLOCK and return a reference to the iyesde in
 * delegated_iyesde.  The caller should then break the delegation and
 * retry.  Because breaking a delegation may take a long time, the
 * caller should drop the i_mutex before doing so.
 *
 * Alternatively, a caller may pass NULL for delegated_iyesde.  This may
 * be appropriate for callers that expect the underlying filesystem yest
 * to be NFS exported.  Also, passing NULL is fine for callers holding
 * the file open for write, as there can be yes conflicting delegation in
 * that case.
 */
int yestify_change(struct dentry * dentry, struct iattr * attr, struct iyesde **delegated_iyesde)
{
	struct iyesde *iyesde = dentry->d_iyesde;
	umode_t mode = iyesde->i_mode;
	int error;
	struct timespec64 yesw;
	unsigned int ia_valid = attr->ia_valid;

	WARN_ON_ONCE(!iyesde_is_locked(iyesde));

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_TIMES_SET)) {
		if (IS_IMMUTABLE(iyesde) || IS_APPEND(iyesde))
			return -EPERM;
	}

	/*
	 * If utimes(2) and friends are called with times == NULL (or both
	 * times are UTIME_NOW), then we need to check for write permission
	 */
	if (ia_valid & ATTR_TOUCH) {
		if (IS_IMMUTABLE(iyesde))
			return -EPERM;

		if (!iyesde_owner_or_capable(iyesde)) {
			error = iyesde_permission(iyesde, MAY_WRITE);
			if (error)
				return error;
		}
	}

	if ((ia_valid & ATTR_MODE)) {
		umode_t amode = attr->ia_mode;
		/* Flag setting protected by i_mutex */
		if (is_sxid(amode))
			iyesde->i_flags &= ~S_NOSEC;
	}

	yesw = current_time(iyesde);

	attr->ia_ctime = yesw;
	if (!(ia_valid & ATTR_ATIME_SET))
		attr->ia_atime = yesw;
	if (!(ia_valid & ATTR_MTIME_SET))
		attr->ia_mtime = yesw;
	if (ia_valid & ATTR_KILL_PRIV) {
		error = security_iyesde_need_killpriv(dentry);
		if (error < 0)
			return error;
		if (error == 0)
			ia_valid = attr->ia_valid &= ~ATTR_KILL_PRIV;
	}

	/*
	 * We yesw pass ATTR_KILL_S*ID to the lower level setattr function so
	 * that the function has the ability to reinterpret a mode change
	 * that's due to these bits. This adds an implicit restriction that
	 * yes function will ever call yestify_change with both ATTR_MODE and
	 * ATTR_KILL_S*ID set.
	 */
	if ((ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID)) &&
	    (ia_valid & ATTR_MODE))
		BUG();

	if (ia_valid & ATTR_KILL_SUID) {
		if (mode & S_ISUID) {
			ia_valid = attr->ia_valid |= ATTR_MODE;
			attr->ia_mode = (iyesde->i_mode & ~S_ISUID);
		}
	}
	if (ia_valid & ATTR_KILL_SGID) {
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = iyesde->i_mode;
			}
			attr->ia_mode &= ~S_ISGID;
		}
	}
	if (!(attr->ia_valid & ~(ATTR_KILL_SUID | ATTR_KILL_SGID)))
		return 0;

	/*
	 * Verify that uid/gid changes are valid in the target
	 * namespace of the superblock.
	 */
	if (ia_valid & ATTR_UID &&
	    !kuid_has_mapping(iyesde->i_sb->s_user_ns, attr->ia_uid))
		return -EOVERFLOW;
	if (ia_valid & ATTR_GID &&
	    !kgid_has_mapping(iyesde->i_sb->s_user_ns, attr->ia_gid))
		return -EOVERFLOW;

	/* Don't allow modifications of files with invalid uids or
	 * gids unless those uids & gids are being made valid.
	 */
	if (!(ia_valid & ATTR_UID) && !uid_valid(iyesde->i_uid))
		return -EOVERFLOW;
	if (!(ia_valid & ATTR_GID) && !gid_valid(iyesde->i_gid))
		return -EOVERFLOW;

	error = security_iyesde_setattr(dentry, attr);
	if (error)
		return error;
	error = try_break_deleg(iyesde, delegated_iyesde);
	if (error)
		return error;

	if (iyesde->i_op->setattr)
		error = iyesde->i_op->setattr(dentry, attr);
	else
		error = simple_setattr(dentry, attr);

	if (!error) {
		fsyestify_change(dentry, ia_valid);
		ima_iyesde_post_setattr(dentry);
		evm_iyesde_post_setattr(dentry, ia_valid);
	}

	return error;
}
EXPORT_SYMBOL(yestify_change);
