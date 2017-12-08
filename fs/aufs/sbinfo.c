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
 * superblock private data
 */

#include "aufs.h"

/*
 * they are necessary regardless sysfs is disabled.
 */
void au_si_free(struct kobject *kobj)
{
	int i;
	struct au_sbinfo *sbinfo;
	char *locked __maybe_unused; /* debug only */

	sbinfo = container_of(kobj, struct au_sbinfo, si_kobj);
	for (i = 0; i < AuPlink_NHASH; i++)
		AuDebugOn(!hlist_bl_empty(sbinfo->si_plink + i));
	AuDebugOn(atomic_read(&sbinfo->si_nowait.nw_len));

	AuDebugOn(percpu_counter_sum(&sbinfo->si_ninodes));
	percpu_counter_destroy(&sbinfo->si_ninodes);
	AuDebugOn(percpu_counter_sum(&sbinfo->si_nfiles));
	percpu_counter_destroy(&sbinfo->si_nfiles);

	au_rw_write_lock(&sbinfo->si_rwsem);
	au_br_free(sbinfo);
	au_rw_write_unlock(&sbinfo->si_rwsem);

	kfree(sbinfo->si_branch);
	mutex_destroy(&sbinfo->si_xib_mtx);
	AuRwDestroy(&sbinfo->si_rwsem);

	kfree(sbinfo);
}

int au_si_alloc(struct super_block *sb)
{
	int err, i;
	struct au_sbinfo *sbinfo;

	err = -ENOMEM;
	sbinfo = kzalloc(sizeof(*sbinfo), GFP_NOFS);
	if (unlikely(!sbinfo))
		goto out;

	/* will be reallocated separately */
	sbinfo->si_branch = kzalloc(sizeof(*sbinfo->si_branch), GFP_NOFS);
	if (unlikely(!sbinfo->si_branch))
		goto out_sbinfo;

	err = sysaufs_si_init(sbinfo);
	if (unlikely(err))
		goto out_br;

	au_nwt_init(&sbinfo->si_nowait);
	au_rw_init_wlock(&sbinfo->si_rwsem);

	percpu_counter_init(&sbinfo->si_ninodes, 0, GFP_NOFS);
	percpu_counter_init(&sbinfo->si_nfiles, 0, GFP_NOFS);

	sbinfo->si_bbot = -1;
	sbinfo->si_last_br_id = AUFS_BRANCH_MAX / 2;

	sbinfo->si_wbr_copyup = AuWbrCopyup_Def;
	sbinfo->si_wbr_create = AuWbrCreate_Def;
	sbinfo->si_wbr_copyup_ops = au_wbr_copyup_ops + sbinfo->si_wbr_copyup;
	sbinfo->si_wbr_create_ops = au_wbr_create_ops + sbinfo->si_wbr_create;

	au_fhsm_init(sbinfo);

	sbinfo->si_mntflags = au_opts_plink(AuOpt_Def);

	sbinfo->si_xino_jiffy = jiffies;
	sbinfo->si_xino_expire
		= msecs_to_jiffies(AUFS_XINO_DEF_SEC * MSEC_PER_SEC);
	mutex_init(&sbinfo->si_xib_mtx);
	sbinfo->si_xino_brid = -1;
	/* leave si_xib_last_pindex and si_xib_next_bit */

	INIT_HLIST_BL_HEAD(&sbinfo->si_aopen);

	sbinfo->si_rdcache = msecs_to_jiffies(AUFS_RDCACHE_DEF * MSEC_PER_SEC);
	sbinfo->si_rdblk = AUFS_RDBLK_DEF;
	sbinfo->si_rdhash = AUFS_RDHASH_DEF;
	sbinfo->si_dirwh = AUFS_DIRWH_DEF;

	for (i = 0; i < AuPlink_NHASH; i++)
		INIT_HLIST_BL_HEAD(sbinfo->si_plink + i);
	init_waitqueue_head(&sbinfo->si_plink_wq);
	spin_lock_init(&sbinfo->si_plink_maint_lock);

	INIT_HLIST_BL_HEAD(&sbinfo->si_files);

	/* with getattr by default */
	sbinfo->si_iop_array = aufs_iop;

	/* leave other members for sysaufs and si_mnt. */
	sbinfo->si_sb = sb;
	sb->s_fs_info = sbinfo;
	si_pid_set(sb);
	return 0; /* success */

out_br:
	kfree(sbinfo->si_branch);
out_sbinfo:
	kfree(sbinfo);
out:
	return err;
}

int au_sbr_realloc(struct au_sbinfo *sbinfo, int nbr, int may_shrink)
{
	int err, sz;
	struct au_branch **brp;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	err = -ENOMEM;
	sz = sizeof(*brp) * (sbinfo->si_bbot + 1);
	if (unlikely(!sz))
		sz = sizeof(*brp);
	brp = au_kzrealloc(sbinfo->si_branch, sz, sizeof(*brp) * nbr, GFP_NOFS,
			   may_shrink);
	if (brp) {
		sbinfo->si_branch = brp;
		err = 0;
	}

	return err;
}

/* ---------------------------------------------------------------------- */

unsigned int au_sigen_inc(struct super_block *sb)
{
	unsigned int gen;
	struct inode *inode;

	SiMustWriteLock(sb);

	gen = ++au_sbi(sb)->si_generation;
	au_update_digen(sb->s_root);
	inode = d_inode(sb->s_root);
	au_update_iigen(inode, /*half*/0);
	inode->i_version++;
	return gen;
}

aufs_bindex_t au_new_br_id(struct super_block *sb)
{
	aufs_bindex_t br_id;
	int i;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	sbinfo = au_sbi(sb);
	for (i = 0; i <= AUFS_BRANCH_MAX; i++) {
		br_id = ++sbinfo->si_last_br_id;
		AuDebugOn(br_id < 0);
		if (br_id && au_br_index(sb, br_id) < 0)
			return br_id;
	}

	return -1;
}

/* ---------------------------------------------------------------------- */

/* it is ok that new 'nwt' tasks are appended while we are sleeping */
int si_read_lock(struct super_block *sb, int flags)
{
	int err;

	err = 0;
	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);

	si_noflush_read_lock(sb);
	err = au_plink_maint(sb, flags);
	if (unlikely(err))
		si_read_unlock(sb);

	return err;
}

int si_write_lock(struct super_block *sb, int flags)
{
	int err;

	if (au_ftest_lock(flags, FLUSH))
		au_nwt_flush(&au_sbi(sb)->si_nowait);

	si_noflush_write_lock(sb);
	err = au_plink_maint(sb, flags);
	if (unlikely(err))
		si_write_unlock(sb);

	return err;
}

/* dentry and super_block lock. call at entry point */
int aufs_read_lock(struct dentry *dentry, int flags)
{
	int err;
	struct super_block *sb;

	sb = dentry->d_sb;
	err = si_read_lock(sb, flags);
	if (unlikely(err))
		goto out;

	if (au_ftest_lock(flags, DW))
		di_write_lock_child(dentry);
	else
		di_read_lock_child(dentry, flags);

	if (au_ftest_lock(flags, GEN)) {
		err = au_digen_test(dentry, au_sigen(sb));
		if (!au_opt_test(au_mntflags(sb), UDBA_NONE))
			AuDebugOn(!err && au_dbrange_test(dentry));
		else if (!err)
			err = au_dbrange_test(dentry);
		if (unlikely(err))
			aufs_read_unlock(dentry, flags);
	}

out:
	return err;
}

void aufs_read_unlock(struct dentry *dentry, int flags)
{
	if (au_ftest_lock(flags, DW))
		di_write_unlock(dentry);
	else
		di_read_unlock(dentry, flags);
	si_read_unlock(dentry->d_sb);
}

void aufs_write_lock(struct dentry *dentry)
{
	si_write_lock(dentry->d_sb, AuLock_FLUSH | AuLock_NOPLMW);
	di_write_lock_child(dentry);
}

void aufs_write_unlock(struct dentry *dentry)
{
	di_write_unlock(dentry);
	si_write_unlock(dentry->d_sb);
}

int aufs_read_and_write_lock2(struct dentry *d1, struct dentry *d2, int flags)
{
	int err;
	unsigned int sigen;
	struct super_block *sb;

	sb = d1->d_sb;
	err = si_read_lock(sb, flags);
	if (unlikely(err))
		goto out;

	di_write_lock2_child(d1, d2, au_ftest_lock(flags, DIRS));

	if (au_ftest_lock(flags, GEN)) {
		sigen = au_sigen(sb);
		err = au_digen_test(d1, sigen);
		AuDebugOn(!err && au_dbrange_test(d1));
		if (!err) {
			err = au_digen_test(d2, sigen);
			AuDebugOn(!err && au_dbrange_test(d2));
		}
		if (unlikely(err))
			aufs_read_and_write_unlock2(d1, d2);
	}

out:
	return err;
}

void aufs_read_and_write_unlock2(struct dentry *d1, struct dentry *d2)
{
	di_write_unlock2(d1, d2);
	si_read_unlock(d1->d_sb);
}
