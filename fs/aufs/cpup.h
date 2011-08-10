/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * copy-up/down functions
 */

#ifndef __AUFS_CPUP_H__
#define __AUFS_CPUP_H__

#ifdef __KERNEL__

#include <linux/path.h>
#include <linux/time.h>
#include <linux/aufs_type.h>

struct inode;
struct file;

void au_cpup_attr_flags(struct inode *dst, struct inode *src);
void au_cpup_attr_timesizes(struct inode *inode);
void au_cpup_attr_nlink(struct inode *inode, int force);
void au_cpup_attr_changeable(struct inode *inode);
void au_cpup_igen(struct inode *inode, struct inode *h_inode);
void au_cpup_attr_all(struct inode *inode, int force);

/* ---------------------------------------------------------------------- */

/* cpup flags */
#define AuCpup_DTIME	1		/* do dtime_store/revert */
#define AuCpup_KEEPLINO	(1 << 1)	/* do not clear the lower xino,
					   for link(2) */
#define au_ftest_cpup(flags, name)	((flags) & AuCpup_##name)
#define au_fset_cpup(flags, name) \
	do { (flags) |= AuCpup_##name; } while (0)
#define au_fclr_cpup(flags, name) \
	do { (flags) &= ~AuCpup_##name; } while (0)

int au_copy_file(struct file *dst, struct file *src, loff_t len);
int au_sio_cpup_single(struct dentry *dentry, aufs_bindex_t bdst,
		       aufs_bindex_t bsrc, loff_t len, unsigned int flags,
		       struct dentry *dst_parent);
int au_sio_cpup_simple(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
		       unsigned int flags);
int au_sio_cpup_wh(struct dentry *dentry, aufs_bindex_t bdst, loff_t len,
		   struct file *file);

int au_cp_dirs(struct dentry *dentry, aufs_bindex_t bdst,
	       int (*cp)(struct dentry *dentry, aufs_bindex_t bdst,
			 struct dentry *h_parent, void *arg),
	       void *arg);
int au_cpup_dirs(struct dentry *dentry, aufs_bindex_t bdst);
int au_test_and_cpup_dirs(struct dentry *dentry, aufs_bindex_t bdst);

/* ---------------------------------------------------------------------- */

/* keep timestamps when copyup */
struct au_dtime {
	struct dentry *dt_dentry;
	struct path dt_h_path;
	struct timespec dt_atime, dt_mtime;
};
void au_dtime_store(struct au_dtime *dt, struct dentry *dentry,
		    struct path *h_path);
void au_dtime_revert(struct au_dtime *dt);

#endif /* __KERNEL__ */
#endif /* __AUFS_CPUP_H__ */
