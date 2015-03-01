/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * whiteout for logical deletion and opaque directory
 */

#ifndef __AUFS_WHOUT_H__
#define __AUFS_WHOUT_H__

#ifdef __KERNEL__

#include "dir.h"

/* whout.c */
int au_wh_name_alloc(struct qstr *wh, const struct qstr *name);
struct au_branch;
int au_wh_test(struct dentry *h_parent, struct qstr *wh_name, int try_sio);
int au_diropq_test(struct dentry *h_dentry);
struct dentry *au_whtmp_lkup(struct dentry *h_parent, struct au_branch *br,
			     struct qstr *prefix);
int au_whtmp_ren(struct dentry *h_dentry, struct au_branch *br);
int au_wh_unlink_dentry(struct inode *h_dir, struct path *h_path,
			struct dentry *dentry);
int au_wh_init(struct au_branch *br, struct super_block *sb);

/* diropq flags */
#define AuDiropq_CREATE	1
#define au_ftest_diropq(flags, name)	((flags) & AuDiropq_##name)
#define au_fset_diropq(flags, name) \
	do { (flags) |= AuDiropq_##name; } while (0)
#define au_fclr_diropq(flags, name) \
	do { (flags) &= ~AuDiropq_##name; } while (0)

struct dentry *au_diropq_sio(struct dentry *dentry, aufs_bindex_t bindex,
			     unsigned int flags);
struct dentry *au_wh_lkup(struct dentry *h_parent, struct qstr *base_name,
			  struct au_branch *br);
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent);

/* real rmdir for the whiteout-ed dir */
struct au_whtmp_rmdir {
	struct inode *dir;
	struct au_branch *br;
	struct dentry *wh_dentry;
	struct au_nhash whlist;
};

struct au_whtmp_rmdir *au_whtmp_rmdir_alloc(struct super_block *sb, gfp_t gfp);
void au_whtmp_rmdir_free(struct au_whtmp_rmdir *whtmp);
int au_whtmp_rmdir(struct inode *dir, aufs_bindex_t bindex,
		   struct dentry *wh_dentry, struct au_nhash *whlist);
void au_whtmp_kick_rmdir(struct inode *dir, aufs_bindex_t bindex,
			 struct dentry *wh_dentry, struct au_whtmp_rmdir *args);

/* ---------------------------------------------------------------------- */

static inline struct dentry *au_diropq_create(struct dentry *dentry,
					      aufs_bindex_t bindex)
{
	return au_diropq_sio(dentry, bindex, AuDiropq_CREATE);
}

static inline int au_diropq_remove(struct dentry *dentry, aufs_bindex_t bindex)
{
	return PTR_ERR(au_diropq_sio(dentry, bindex, !AuDiropq_CREATE));
}

#endif /* __KERNEL__ */
#endif /* __AUFS_WHOUT_H__ */
