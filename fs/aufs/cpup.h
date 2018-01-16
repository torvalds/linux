/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * copy-up/down functions
 */

#ifndef __AUFS_CPUP_H__
#define __AUFS_CPUP_H__

#ifdef __KERNEL__

#include <linux/path.h>

struct inode;
struct file;
struct au_pin;

void au_cpup_attr_flags(struct inode *dst, unsigned int iflags);
void au_cpup_attr_timesizes(struct inode *inode);
void au_cpup_attr_nlink(struct inode *inode, int force);
void au_cpup_attr_changeable(struct inode *inode);
void au_cpup_igen(struct inode *inode, struct inode *h_inode);
void au_cpup_attr_all(struct inode *inode, int force);

/* ---------------------------------------------------------------------- */

struct au_cp_generic {
	struct dentry	*dentry;
	aufs_bindex_t	bdst, bsrc;
	loff_t		len;
	struct au_pin	*pin;
	unsigned int	flags;
};

/* cpup flags */
#define AuCpup_DTIME		1		/* do dtime_store/revert */
#define AuCpup_KEEPLINO		(1 << 1)	/* do not clear the lower xino,
						   for link(2) */
#define AuCpup_RENAME		(1 << 2)	/* rename after cpup */
#define AuCpup_HOPEN		(1 << 3)	/* call h_open_pre/post() in
						   cpup */
#define AuCpup_OVERWRITE	(1 << 4)	/* allow overwriting the
						   existing entry */
#define AuCpup_RWDST		(1 << 5)	/* force write target even if
						   the branch is marked as RO */

#ifndef CONFIG_AUFS_BR_HFSPLUS
#undef AuCpup_HOPEN
#define AuCpup_HOPEN		0
#endif

#define au_ftest_cpup(flags, name)	((flags) & AuCpup_##name)
#define au_fset_cpup(flags, name) \
	do { (flags) |= AuCpup_##name; } while (0)
#define au_fclr_cpup(flags, name) \
	do { (flags) &= ~AuCpup_##name; } while (0)

int au_copy_file(struct file *dst, struct file *src, loff_t len);
int au_sio_cpup_simple(struct au_cp_generic *cpg);
int au_sio_cpdown_simple(struct au_cp_generic *cpg);
int au_sio_cpup_wh(struct au_cp_generic *cpg, struct file *file);

int au_cp_dirs(struct dentry *dentry, aufs_bindex_t bdst,
	       int (*cp)(struct dentry *dentry, aufs_bindex_t bdst,
			 struct au_pin *pin,
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
