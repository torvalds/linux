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
 * branch filesystems and xino for them
 */

#ifndef __AUFS_BRANCH_H__
#define __AUFS_BRANCH_H__

#ifdef __KERNEL__

#include <linux/mount.h>
#include "dynop.h"
#include "rwsem.h"
#include "super.h"

/* ---------------------------------------------------------------------- */

/* a xino file */
struct au_xino_file {
	struct file		*xi_file;
	struct mutex		xi_nondir_mtx;

	/* todo: make xino files an array to support huge inode number */

#ifdef CONFIG_DEBUG_FS
	struct dentry		 *xi_dbgaufs;
#endif
};

/* members for writable branch only */
enum {AuBrWh_BASE, AuBrWh_PLINK, AuBrWh_ORPH, AuBrWh_Last};
struct au_wbr {
	struct au_rwsem		wbr_wh_rwsem;
	struct dentry		*wbr_wh[AuBrWh_Last];
	atomic_t		wbr_wh_running;
#define wbr_whbase		wbr_wh[AuBrWh_BASE]	/* whiteout base */
#define wbr_plink		wbr_wh[AuBrWh_PLINK]	/* pseudo-link dir */
#define wbr_orph		wbr_wh[AuBrWh_ORPH]	/* dir for orphans */

	/* mfs mode */
	unsigned long long	wbr_bytes;
};

/* ext2 has 3 types of operations at least, ext3 has 4 */
#define AuBrDynOp (AuDyLast * 4)

#ifdef CONFIG_AUFS_HFSNOTIFY
/* support for asynchronous destruction */
struct au_br_hfsnotify {
	struct fsnotify_group	*hfsn_group;
};
#endif

/* sysfs entries */
struct au_brsysfs {
	char			name[16];
	struct attribute	attr;
};

enum {
	AuBrSysfs_BR,
	AuBrSysfs_BRID,
	AuBrSysfs_Last
};

/* protected by superblock rwsem */
struct au_branch {
	struct au_xino_file	br_xino;

	aufs_bindex_t		br_id;

	int			br_perm;
	unsigned int		br_dflags;
	struct path		br_path;
	spinlock_t		br_dykey_lock;
	struct au_dykey		*br_dykey[AuBrDynOp];
	atomic_t		br_count;

	struct au_wbr		*br_wbr;

	/* xino truncation */
	atomic_t		br_xino_running;

#ifdef CONFIG_AUFS_HFSNOTIFY
	struct au_br_hfsnotify	*br_hfsn;
#endif

#ifdef CONFIG_SYSFS
	/* entries under sysfs per mount-point */
	struct au_brsysfs	br_sysfs[AuBrSysfs_Last];
#endif
};

/* ---------------------------------------------------------------------- */

static inline struct vfsmount *au_br_mnt(struct au_branch *br)
{
	return br->br_path.mnt;
}

static inline struct dentry *au_br_dentry(struct au_branch *br)
{
	return br->br_path.dentry;
}

static inline struct super_block *au_br_sb(struct au_branch *br)
{
	return au_br_mnt(br)->mnt_sb;
}

/* branch permissions and attributes */
#define AuBrPerm_RW		1		/* writable, hardlinkable wh */
#define AuBrPerm_RO		(1 << 1)	/* readonly */
#define AuBrPerm_RR		(1 << 2)	/* natively readonly */
#define AuBrPerm_Mask		(AuBrPerm_RW | AuBrPerm_RO | AuBrPerm_RR)

#define AuBrRAttr_WH		(1 << 3)	/* whiteout-able */

#define AuBrWAttr_NoLinkWH	(1 << 4)	/* un-hardlinkable whiteouts */

#define AuBrAttr_UNPIN		(1 << 5)	/* rename-able top dir of
						   branch */

static inline int au_br_writable(int brperm)
{
	return brperm & AuBrPerm_RW;
}

static inline int au_br_whable(int brperm)
{
	return brperm & (AuBrPerm_RW | AuBrRAttr_WH);
}

static inline int au_br_wh_linkable(int brperm)
{
	return !(brperm & AuBrWAttr_NoLinkWH);
}

static inline int au_br_rdonly(struct au_branch *br)
{
	return ((au_br_sb(br)->s_flags & MS_RDONLY)
		|| !au_br_writable(br->br_perm))
		? -EROFS : 0;
}

static inline int au_br_hnotifyable(int brperm __maybe_unused)
{
#ifdef CONFIG_AUFS_HNOTIFY
	return !(brperm & AuBrPerm_RR);
#else
	return 0;
#endif
}

/* ---------------------------------------------------------------------- */

/* branch.c */
struct au_sbinfo;
void au_br_free(struct au_sbinfo *sinfo);
int au_br_index(struct super_block *sb, aufs_bindex_t br_id);
struct au_opt_add;
int au_br_add(struct super_block *sb, struct au_opt_add *add, int remount);
struct au_opt_del;
int au_br_del(struct super_block *sb, struct au_opt_del *del, int remount);
long au_ibusy_ioctl(struct file *file, unsigned long arg);
#ifdef CONFIG_COMPAT
long au_ibusy_compat_ioctl(struct file *file, unsigned long arg);
#endif
struct au_opt_mod;
int au_br_mod(struct super_block *sb, struct au_opt_mod *mod, int remount,
	      int *do_refresh);

/* xino.c */
static const loff_t au_loff_max = LLONG_MAX;

int au_xib_trunc(struct super_block *sb);
ssize_t xino_fread(au_readf_t func, struct file *file, void *buf, size_t size,
		   loff_t *pos);
ssize_t xino_fwrite(au_writef_t func, struct file *file, void *buf, size_t size,
		    loff_t *pos);
struct file *au_xino_create2(struct file *base_file, struct file *copy_src);
struct file *au_xino_create(struct super_block *sb, char *fname, int silent);
ino_t au_xino_new_ino(struct super_block *sb);
void au_xino_delete_inode(struct inode *inode, const int unlinked);
int au_xino_write(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		  ino_t ino);
int au_xino_read(struct super_block *sb, aufs_bindex_t bindex, ino_t h_ino,
		 ino_t *ino);
int au_xino_br(struct super_block *sb, struct au_branch *br, ino_t hino,
	       struct file *base_file, int do_test);
int au_xino_trunc(struct super_block *sb, aufs_bindex_t bindex);

struct au_opt_xino;
int au_xino_set(struct super_block *sb, struct au_opt_xino *xino, int remount);
void au_xino_clr(struct super_block *sb);
struct file *au_xino_def(struct super_block *sb);
int au_xino_path(struct seq_file *seq, struct file *file);

/* ---------------------------------------------------------------------- */

/* Superblock to branch */
static inline
aufs_bindex_t au_sbr_id(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_sbr(sb, bindex)->br_id;
}

static inline
struct vfsmount *au_sbr_mnt(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_br_mnt(au_sbr(sb, bindex));
}

static inline
struct super_block *au_sbr_sb(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_br_sb(au_sbr(sb, bindex));
}

static inline void au_sbr_put(struct super_block *sb, aufs_bindex_t bindex)
{
	atomic_dec(&au_sbr(sb, bindex)->br_count);
}

static inline int au_sbr_perm(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_sbr(sb, bindex)->br_perm;
}

static inline int au_sbr_whable(struct super_block *sb, aufs_bindex_t bindex)
{
	return au_br_whable(au_sbr_perm(sb, bindex));
}

/* ---------------------------------------------------------------------- */

/*
 * wbr_wh_read_lock, wbr_wh_write_lock
 * wbr_wh_read_unlock, wbr_wh_write_unlock, wbr_wh_downgrade_lock
 */
AuSimpleRwsemFuncs(wbr_wh, struct au_wbr *wbr, &wbr->wbr_wh_rwsem);

#define WbrWhMustNoWaiters(wbr)	AuRwMustNoWaiters(&wbr->wbr_wh_rwsem)
#define WbrWhMustAnyLock(wbr)	AuRwMustAnyLock(&wbr->wbr_wh_rwsem)
#define WbrWhMustWriteLock(wbr)	AuRwMustWriteLock(&wbr->wbr_wh_rwsem)

#endif /* __KERNEL__ */
#endif /* __AUFS_BRANCH_H__ */
