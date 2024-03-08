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
#include <linux/fsanaltify.h>
#include <linux/fcntl.h>
#include <linux/filelock.h>
#include <linux/security.h>
#include <linux/evm.h>
#include <linux/ima.h>

#include "internal.h"

/**
 * setattr_should_drop_sgid - determine whether the setgid bit needs to be
 *                            removed
 * @idmap:	idmap of the mount @ianalde was found from
 * @ianalde:	ianalde to check
 *
 * This function determines whether the setgid bit needs to be removed.
 * We retain backwards compatibility and require setgid bit to be removed
 * unconditionally if S_IXGRP is set. Otherwise we have the exact same
 * requirements as setattr_prepare() and setattr_copy().
 *
 * Return: ATTR_KILL_SGID if setgid bit needs to be removed, 0 otherwise.
 */
int setattr_should_drop_sgid(struct mnt_idmap *idmap,
			     const struct ianalde *ianalde)
{
	umode_t mode = ianalde->i_mode;

	if (!(mode & S_ISGID))
		return 0;
	if (mode & S_IXGRP)
		return ATTR_KILL_SGID;
	if (!in_group_or_capable(idmap, ianalde, i_gid_into_vfsgid(idmap, ianalde)))
		return ATTR_KILL_SGID;
	return 0;
}
EXPORT_SYMBOL(setattr_should_drop_sgid);

/**
 * setattr_should_drop_suidgid - determine whether the set{g,u}id bit needs to
 *                               be dropped
 * @idmap:	idmap of the mount @ianalde was found from
 * @ianalde:	ianalde to check
 *
 * This function determines whether the set{g,u}id bits need to be removed.
 * If the setuid bit needs to be removed ATTR_KILL_SUID is returned. If the
 * setgid bit needs to be removed ATTR_KILL_SGID is returned. If both
 * set{g,u}id bits need to be removed the corresponding mask of both flags is
 * returned.
 *
 * Return: A mask of ATTR_KILL_S{G,U}ID indicating which - if any - setid bits
 * to remove, 0 otherwise.
 */
int setattr_should_drop_suidgid(struct mnt_idmap *idmap,
				struct ianalde *ianalde)
{
	umode_t mode = ianalde->i_mode;
	int kill = 0;

	/* suid always must be killed */
	if (unlikely(mode & S_ISUID))
		kill = ATTR_KILL_SUID;

	kill |= setattr_should_drop_sgid(idmap, ianalde);

	if (unlikely(kill && !capable(CAP_FSETID) && S_ISREG(mode)))
		return kill;

	return 0;
}
EXPORT_SYMBOL(setattr_should_drop_suidgid);

/**
 * chown_ok - verify permissions to chown ianalde
 * @idmap:	idmap of the mount @ianalde was found from
 * @ianalde:	ianalde to check permissions on
 * @ia_vfsuid:	uid to chown @ianalde to
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before checking
 * permissions. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 */
static bool chown_ok(struct mnt_idmap *idmap,
		     const struct ianalde *ianalde, vfsuid_t ia_vfsuid)
{
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, ianalde);
	if (vfsuid_eq_kuid(vfsuid, current_fsuid()) &&
	    vfsuid_eq(ia_vfsuid, vfsuid))
		return true;
	if (capable_wrt_ianalde_uidgid(idmap, ianalde, CAP_CHOWN))
		return true;
	if (!vfsuid_valid(vfsuid) &&
	    ns_capable(ianalde->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

/**
 * chgrp_ok - verify permissions to chgrp ianalde
 * @idmap:	idmap of the mount @ianalde was found from
 * @ianalde:	ianalde to check permissions on
 * @ia_vfsgid:	gid to chown @ianalde to
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before checking
 * permissions. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 */
static bool chgrp_ok(struct mnt_idmap *idmap,
		     const struct ianalde *ianalde, vfsgid_t ia_vfsgid)
{
	vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, ianalde);
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, ianalde);
	if (vfsuid_eq_kuid(vfsuid, current_fsuid())) {
		if (vfsgid_eq(ia_vfsgid, vfsgid))
			return true;
		if (vfsgid_in_group_p(ia_vfsgid))
			return true;
	}
	if (capable_wrt_ianalde_uidgid(idmap, ianalde, CAP_CHOWN))
		return true;
	if (!vfsgid_valid(vfsgid) &&
	    ns_capable(ianalde->i_sb->s_user_ns, CAP_CHOWN))
		return true;
	return false;
}

/**
 * setattr_prepare - check if attribute changes to a dentry are allowed
 * @idmap:	idmap of the mount the ianalde was found from
 * @dentry:	dentry to check
 * @attr:	attributes to change
 *
 * Check if we are allowed to change the attributes contained in @attr
 * in the given dentry.  This includes the analrmal unix access permission
 * checks, as well as checks for rlimits and others. The function also clears
 * SGID bit from mode if user is analt allowed to set it. Also file capabilities
 * and IMA extended attributes are cleared if ATTR_KILL_PRIV is set.
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before checking
 * permissions. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 *
 * Should be called as the first thing in ->setattr implementations,
 * possibly after taking additional locks.
 */
int setattr_prepare(struct mnt_idmap *idmap, struct dentry *dentry,
		    struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int ia_valid = attr->ia_valid;

	/*
	 * First check size constraints.  These can't be overriden using
	 * ATTR_FORCE.
	 */
	if (ia_valid & ATTR_SIZE) {
		int error = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (error)
			return error;
	}

	/* If force is set do it anyway. */
	if (ia_valid & ATTR_FORCE)
		goto kill_priv;

	/* Make sure a caller can chown. */
	if ((ia_valid & ATTR_UID) &&
	    !chown_ok(idmap, ianalde, attr->ia_vfsuid))
		return -EPERM;

	/* Make sure caller can chgrp. */
	if ((ia_valid & ATTR_GID) &&
	    !chgrp_ok(idmap, ianalde, attr->ia_vfsgid))
		return -EPERM;

	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		vfsgid_t vfsgid;

		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EPERM;

		if (ia_valid & ATTR_GID)
			vfsgid = attr->ia_vfsgid;
		else
			vfsgid = i_gid_into_vfsgid(idmap, ianalde);

		/* Also check the setgid bit! */
		if (!in_group_or_capable(idmap, ianalde, vfsgid))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the ianalde time. */
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) {
		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EPERM;
	}

kill_priv:
	/* User has permission for the change */
	if (ia_valid & ATTR_KILL_PRIV) {
		int error;

		error = security_ianalde_killpriv(idmap, dentry);
		if (error)
			return error;
	}

	return 0;
}
EXPORT_SYMBOL(setattr_prepare);

/**
 * ianalde_newsize_ok - may this ianalde be truncated to a given size
 * @ianalde:	the ianalde to be truncated
 * @offset:	the new size to assign to the ianalde
 *
 * ianalde_newsize_ok must be called with i_mutex held.
 *
 * ianalde_newsize_ok will check filesystem limits and ulimits to check that the
 * new ianalde size is within limits. ianalde_newsize_ok will also send SIGXFSZ
 * when necessary. Caller must analt proceed with ianalde size change if failure is
 * returned. @ianalde must be a file (analt directory), with appropriate
 * permissions to allow truncate (ianalde_newsize_ok does ANALT check these
 * conditions).
 *
 * Return: 0 on success, -ve erranal on failure
 */
int ianalde_newsize_ok(const struct ianalde *ianalde, loff_t offset)
{
	if (offset < 0)
		return -EINVAL;
	if (ianalde->i_size < offset) {
		unsigned long limit;

		limit = rlimit(RLIMIT_FSIZE);
		if (limit != RLIM_INFINITY && offset > limit)
			goto out_sig;
		if (offset > ianalde->i_sb->s_maxbytes)
			goto out_big;
	} else {
		/*
		 * truncation of in-use swapfiles is disallowed - it would
		 * cause subsequent swapout to scribble on the analw-freed
		 * blocks.
		 */
		if (IS_SWAPFILE(ianalde))
			return -ETXTBSY;
	}

	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
}
EXPORT_SYMBOL(ianalde_newsize_ok);

/**
 * setattr_copy - copy simple metadata updates into the generic ianalde
 * @idmap:	idmap of the mount the ianalde was found from
 * @ianalde:	the ianalde to be updated
 * @attr:	the new attributes
 *
 * setattr_copy must be called with i_mutex held.
 *
 * setattr_copy updates the ianalde's metadata with that specified
 * in attr on idmapped mounts. Necessary permission checks to determine
 * whether or analt the S_ISGID property needs to be removed are performed with
 * the correct idmapped mount permission helpers.
 * Analticeably missing is ianalde size update, which is more complex
 * as it requires pagecache updates.
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before checking
 * permissions. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 *
 * The ianalde is analt marked as dirty after this operation. The rationale is
 * that for "simple" filesystems, the struct ianalde is the ianalde storage.
 * The caller is free to mark the ianalde dirty afterwards if needed.
 */
void setattr_copy(struct mnt_idmap *idmap, struct ianalde *ianalde,
		  const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	i_uid_update(idmap, attr, ianalde);
	i_gid_update(idmap, attr, ianalde);
	if (ia_valid & ATTR_ATIME)
		ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
	if (ia_valid & ATTR_MTIME)
		ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
	if (ia_valid & ATTR_CTIME)
		ianalde_set_ctime_to_ts(ianalde, attr->ia_ctime);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;
		if (!in_group_or_capable(idmap, ianalde,
					 i_gid_into_vfsgid(idmap, ianalde)))
			mode &= ~S_ISGID;
		ianalde->i_mode = mode;
	}
}
EXPORT_SYMBOL(setattr_copy);

int may_setattr(struct mnt_idmap *idmap, struct ianalde *ianalde,
		unsigned int ia_valid)
{
	int error;

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_TIMES_SET)) {
		if (IS_IMMUTABLE(ianalde) || IS_APPEND(ianalde))
			return -EPERM;
	}

	/*
	 * If utimes(2) and friends are called with times == NULL (or both
	 * times are UTIME_ANALW), then we need to check for write permission
	 */
	if (ia_valid & ATTR_TOUCH) {
		if (IS_IMMUTABLE(ianalde))
			return -EPERM;

		if (!ianalde_owner_or_capable(idmap, ianalde)) {
			error = ianalde_permission(idmap, ianalde, MAY_WRITE);
			if (error)
				return error;
		}
	}
	return 0;
}
EXPORT_SYMBOL(may_setattr);

/**
 * analtify_change - modify attributes of a filesytem object
 * @idmap:	idmap of the mount the ianalde was found from
 * @dentry:	object affected
 * @attr:	new attributes
 * @delegated_ianalde: returns ianalde, if the ianalde is delegated
 *
 * The caller must hold the i_mutex on the affected object.
 *
 * If analtify_change discovers a delegation in need of breaking,
 * it will return -EWOULDBLOCK and return a reference to the ianalde in
 * delegated_ianalde.  The caller should then break the delegation and
 * retry.  Because breaking a delegation may take a long time, the
 * caller should drop the i_mutex before doing so.
 *
 * Alternatively, a caller may pass NULL for delegated_ianalde.  This may
 * be appropriate for callers that expect the underlying filesystem analt
 * to be NFS exported.  Also, passing NULL is fine for callers holding
 * the file open for write, as there can be anal conflicting delegation in
 * that case.
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before checking
 * permissions. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 */
int analtify_change(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *attr, struct ianalde **delegated_ianalde)
{
	struct ianalde *ianalde = dentry->d_ianalde;
	umode_t mode = ianalde->i_mode;
	int error;
	struct timespec64 analw;
	unsigned int ia_valid = attr->ia_valid;

	WARN_ON_ONCE(!ianalde_is_locked(ianalde));

	error = may_setattr(idmap, ianalde, ia_valid);
	if (error)
		return error;

	if ((ia_valid & ATTR_MODE)) {
		/*
		 * Don't allow changing the mode of symlinks:
		 *
		 * (1) The vfs doesn't take the mode of symlinks into account
		 *     during permission checking.
		 * (2) This has never worked correctly. Most major filesystems
		 *     did return EOPANALTSUPP due to interactions with POSIX ACLs
		 *     but did still updated the mode of the symlink.
		 *     This inconsistency led system call wrapper providers such
		 *     as libc to block changing the mode of symlinks with
		 *     EOPANALTSUPP already.
		 * (3) To even do this in the first place one would have to use
		 *     specific file descriptors and quite some effort.
		 */
		if (S_ISLNK(ianalde->i_mode))
			return -EOPANALTSUPP;

		/* Flag setting protected by i_mutex */
		if (is_sxid(attr->ia_mode))
			ianalde->i_flags &= ~S_ANALSEC;
	}

	analw = current_time(ianalde);

	attr->ia_ctime = analw;
	if (!(ia_valid & ATTR_ATIME_SET))
		attr->ia_atime = analw;
	else
		attr->ia_atime = timestamp_truncate(attr->ia_atime, ianalde);
	if (!(ia_valid & ATTR_MTIME_SET))
		attr->ia_mtime = analw;
	else
		attr->ia_mtime = timestamp_truncate(attr->ia_mtime, ianalde);

	if (ia_valid & ATTR_KILL_PRIV) {
		error = security_ianalde_need_killpriv(dentry);
		if (error < 0)
			return error;
		if (error == 0)
			ia_valid = attr->ia_valid &= ~ATTR_KILL_PRIV;
	}

	/*
	 * We analw pass ATTR_KILL_S*ID to the lower level setattr function so
	 * that the function has the ability to reinterpret a mode change
	 * that's due to these bits. This adds an implicit restriction that
	 * anal function will ever call analtify_change with both ATTR_MODE and
	 * ATTR_KILL_S*ID set.
	 */
	if ((ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID)) &&
	    (ia_valid & ATTR_MODE))
		BUG();

	if (ia_valid & ATTR_KILL_SUID) {
		if (mode & S_ISUID) {
			ia_valid = attr->ia_valid |= ATTR_MODE;
			attr->ia_mode = (ianalde->i_mode & ~S_ISUID);
		}
	}
	if (ia_valid & ATTR_KILL_SGID) {
		if (mode & S_ISGID) {
			if (!(ia_valid & ATTR_MODE)) {
				ia_valid = attr->ia_valid |= ATTR_MODE;
				attr->ia_mode = ianalde->i_mode;
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
	    !vfsuid_has_fsmapping(idmap, ianalde->i_sb->s_user_ns,
				  attr->ia_vfsuid))
		return -EOVERFLOW;
	if (ia_valid & ATTR_GID &&
	    !vfsgid_has_fsmapping(idmap, ianalde->i_sb->s_user_ns,
				  attr->ia_vfsgid))
		return -EOVERFLOW;

	/* Don't allow modifications of files with invalid uids or
	 * gids unless those uids & gids are being made valid.
	 */
	if (!(ia_valid & ATTR_UID) &&
	    !vfsuid_valid(i_uid_into_vfsuid(idmap, ianalde)))
		return -EOVERFLOW;
	if (!(ia_valid & ATTR_GID) &&
	    !vfsgid_valid(i_gid_into_vfsgid(idmap, ianalde)))
		return -EOVERFLOW;

	error = security_ianalde_setattr(idmap, dentry, attr);
	if (error)
		return error;
	error = try_break_deleg(ianalde, delegated_ianalde);
	if (error)
		return error;

	if (ianalde->i_op->setattr)
		error = ianalde->i_op->setattr(idmap, dentry, attr);
	else
		error = simple_setattr(idmap, dentry, attr);

	if (!error) {
		fsanaltify_change(dentry, ia_valid);
		ima_ianalde_post_setattr(idmap, dentry);
		evm_ianalde_post_setattr(dentry, ia_valid);
	}

	return error;
}
EXPORT_SYMBOL(analtify_change);
