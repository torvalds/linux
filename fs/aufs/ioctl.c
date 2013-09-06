/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * ioctl
 * plink-management and readdir in userspace.
 * assist the pathconf(3) wrapper library.
 * move-down
 */

#include <linux/compat.h>
#include <linux/file.h>
#include "aufs.h"

static int au_wbr_fd(struct path *path, struct aufs_wbr_fd __user *arg)
{
	int err, fd;
	aufs_bindex_t wbi, bindex, bend;
	struct file *h_file;
	struct super_block *sb;
	struct dentry *root;
	struct au_branch *br;
	struct aufs_wbr_fd wbrfd = {
		.oflags	= au_dir_roflags,
		.brid	= -1
	};
	const int valid = O_RDONLY | O_NONBLOCK | O_LARGEFILE | O_DIRECTORY
		| O_NOATIME | O_CLOEXEC;

	AuDebugOn(wbrfd.oflags & ~valid);

	if (arg) {
		err = copy_from_user(&wbrfd, arg, sizeof(wbrfd));
		if (unlikely(err)) {
			err = -EFAULT;
			goto out;
		}

		err = -EINVAL;
		AuDbg("wbrfd{0%o, %d}\n", wbrfd.oflags, wbrfd.brid);
		wbrfd.oflags |= au_dir_roflags;
		AuDbg("0%o\n", wbrfd.oflags);
		if (unlikely(wbrfd.oflags & ~valid))
			goto out;
	}

	fd = get_unused_fd();
	err = fd;
	if (unlikely(fd < 0))
		goto out;

	h_file = ERR_PTR(-EINVAL);
	wbi = 0;
	br = NULL;
	sb = path->dentry->d_sb;
	root = sb->s_root;
	aufs_read_lock(root, AuLock_IR);
	bend = au_sbend(sb);
	if (wbrfd.brid >= 0) {
		wbi = au_br_index(sb, wbrfd.brid);
		if (unlikely(wbi < 0 || wbi > bend))
			goto out_unlock;
	}

	h_file = ERR_PTR(-ENOENT);
	br = au_sbr(sb, wbi);
	if (!au_br_writable(br->br_perm)) {
		if (arg)
			goto out_unlock;

		bindex = wbi + 1;
		wbi = -1;
		for (; bindex <= bend; bindex++) {
			br = au_sbr(sb, bindex);
			if (au_br_writable(br->br_perm)) {
				wbi = bindex;
				br = au_sbr(sb, wbi);
				break;
			}
		}
	}
	AuDbg("wbi %d\n", wbi);
	if (wbi >= 0)
		h_file = au_h_open(root, wbi, wbrfd.oflags, NULL,
				   /*force_wr*/0);

out_unlock:
	aufs_read_unlock(root, AuLock_IR);
	err = PTR_ERR(h_file);
	if (IS_ERR(h_file))
		goto out_fd;

	atomic_dec(&br->br_count); /* cf. au_h_open() */
	fd_install(fd, h_file);
	err = fd;
	goto out; /* success */

out_fd:
	put_unused_fd(fd);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

long aufs_ioctl_dir(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err;

	switch (cmd) {
	case AUFS_CTL_RDU:
	case AUFS_CTL_RDU_INO:
		err = au_rdu_ioctl(file, cmd, arg);
		break;

	case AUFS_CTL_WBR_FD:
		err = au_wbr_fd(&file->f_path, (void __user *)arg);
		break;

	case AUFS_CTL_IBUSY:
		err = au_ibusy_ioctl(file, arg);
		break;

	default:
		/* do not call the lower */
		AuDbg("0x%x\n", cmd);
		err = -ENOTTY;
	}

	AuTraceErr(err);
	return err;
}

long aufs_ioctl_nondir(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err;

	switch (cmd) {
	case AUFS_CTL_MVDOWN:
		err = au_mvdown(file->f_dentry, (void __user *)arg);
		break;

	case AUFS_CTL_WBR_FD:
		err = au_wbr_fd(&file->f_path, (void __user *)arg);
		break;

	default:
		/* do not call the lower */
		AuDbg("0x%x\n", cmd);
		err = -ENOTTY;
	}

	AuTraceErr(err);
	return err;
}

#ifdef CONFIG_COMPAT
long aufs_compat_ioctl_dir(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	long err;

	switch (cmd) {
	case AUFS_CTL_RDU:
	case AUFS_CTL_RDU_INO:
		err = au_rdu_compat_ioctl(file, cmd, arg);
		break;

	case AUFS_CTL_IBUSY:
		err = au_ibusy_compat_ioctl(file, arg);
		break;

	default:
		err = aufs_ioctl_dir(file, cmd, arg);
	}

	AuTraceErr(err);
	return err;
}

long aufs_compat_ioctl_nondir(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	return aufs_ioctl_nondir(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
