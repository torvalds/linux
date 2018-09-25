/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2018 Junjiro R. Okajima
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
 * renamed dir info
 */

#ifndef __AUFS_DIRREN_H__
#define __AUFS_DIRREN_H__

#ifdef __KERNEL__

#include <linux/dcache.h>
#include <linux/statfs.h>
#include <linux/uuid.h>
#include "hbl.h"

#define AuDirren_NHASH 100

#ifdef CONFIG_AUFS_DIRREN
enum au_brid_type {
	AuBrid_Unset,
	AuBrid_UUID,
	AuBrid_FSID,
	AuBrid_DEV
};

struct au_dr_brid {
	enum au_brid_type	type;
	union {
		uuid_t	uuid;	/* unimplemented yet */
		fsid_t	fsid;
		dev_t	dev;
	};
};

/* 20 is the max digits length of ulong 64 */
/* brid-type "_" uuid "_" inum */
#define AUFS_DIRREN_FNAME_SZ	(1 + 1 + UUID_STRING_LEN + 20)
#define AUFS_DIRREN_ENV_VAL_SZ	(AUFS_DIRREN_FNAME_SZ + 1 + 20)

struct au_dr_hino {
	struct hlist_bl_node	dr_hnode;
	ino_t			dr_h_ino;
};

struct au_dr_br {
	struct hlist_bl_head	dr_h_ino[AuDirren_NHASH];
	struct au_dr_brid	dr_brid;
};

struct au_dr_lookup {
	/* dr_name is pointed by struct au_do_lookup_args.name */
	struct qstr		dr_name; /* subset of dr_info */
	aufs_bindex_t		ninfo;
	struct au_drinfo	**drinfo;
};
#else
struct au_dr_hino;
/* empty */
struct au_dr_br { };
struct au_dr_lookup { };
#endif

/* ---------------------------------------------------------------------- */

struct au_branch;
struct au_do_lookup_args;
struct au_hinode;
#ifdef CONFIG_AUFS_DIRREN
int au_dr_hino_test_add(struct au_dr_br *dr, ino_t h_ino,
			struct au_dr_hino *add_ent);
void au_dr_hino_free(struct au_dr_br *dr);
int au_dr_br_init(struct super_block *sb, struct au_branch *br,
		  const struct path *path);
int au_dr_br_fin(struct super_block *sb, struct au_branch *br);
int au_dr_rename(struct dentry *src, aufs_bindex_t bindex,
		 struct qstr *dst_name, void *_rev);
void au_dr_rename_fin(struct dentry *src, aufs_bindex_t btgt, void *rev);
void au_dr_rename_rev(struct dentry *src, aufs_bindex_t bindex, void *rev);
int au_dr_lkup(struct au_do_lookup_args *lkup, struct dentry *dentry,
	       aufs_bindex_t bindex);
int au_dr_lkup_name(struct au_do_lookup_args *lkup, aufs_bindex_t btgt);
int au_dr_lkup_h_ino(struct au_do_lookup_args *lkup, aufs_bindex_t bindex,
		     ino_t h_ino);
void au_dr_lkup_fin(struct au_do_lookup_args *lkup);
int au_dr_opt_set(struct super_block *sb);
int au_dr_opt_flush(struct super_block *sb);
int au_dr_opt_clr(struct super_block *sb, int no_flush);
#else
AuStubInt0(au_dr_hino_test_add, struct au_dr_br *dr, ino_t h_ino,
	   struct au_dr_hino *add_ent);
AuStubVoid(au_dr_hino_free, struct au_dr_br *dr);
AuStubInt0(au_dr_br_init, struct super_block *sb, struct au_branch *br,
	   const struct path *path);
AuStubInt0(au_dr_br_fin, struct super_block *sb, struct au_branch *br);
AuStubInt0(au_dr_rename, struct dentry *src, aufs_bindex_t bindex,
	   struct qstr *dst_name, void *_rev);
AuStubVoid(au_dr_rename_fin, struct dentry *src, aufs_bindex_t btgt, void *rev);
AuStubVoid(au_dr_rename_rev, struct dentry *src, aufs_bindex_t bindex,
	   void *rev);
AuStubInt0(au_dr_lkup, struct au_do_lookup_args *lkup, struct dentry *dentry,
	   aufs_bindex_t bindex);
AuStubInt0(au_dr_lkup_name, struct au_do_lookup_args *lkup, aufs_bindex_t btgt);
AuStubInt0(au_dr_lkup_h_ino, struct au_do_lookup_args *lkup,
	   aufs_bindex_t bindex, ino_t h_ino);
AuStubVoid(au_dr_lkup_fin, struct au_do_lookup_args *lkup);
AuStubInt0(au_dr_opt_set, struct super_block *sb);
AuStubInt0(au_dr_opt_flush, struct super_block *sb);
AuStubInt0(au_dr_opt_clr, struct super_block *sb, int no_flush);
#endif

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_DIRREN
static inline int au_dr_ihash(ino_t h_ino)
{
	return h_ino % AuDirren_NHASH;
}
#else
AuStubInt0(au_dr_ihash, ino_t h_ino);
#endif

#endif /* __KERNEL__ */
#endif /* __AUFS_DIRREN_H__ */
