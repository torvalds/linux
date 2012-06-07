/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
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
 * branch management
 */

#include <linux/compat.h>
#include <linux/statfs.h>
#include "aufs.h"

/*
 * free a single branch
 */
static void au_br_do_free(struct au_branch *br)
{
	int i;
	struct au_wbr *wbr;
	struct au_dykey **key;

	au_hnotify_fin_br(br);

	if (br->br_xino.xi_file)
		fput(br->br_xino.xi_file);
	mutex_destroy(&br->br_xino.xi_nondir_mtx);

	AuDebugOn(atomic_read(&br->br_count));

	wbr = br->br_wbr;
	if (wbr) {
		for (i = 0; i < AuBrWh_Last; i++)
			dput(wbr->wbr_wh[i]);
		AuDebugOn(atomic_read(&wbr->wbr_wh_running));
		AuRwDestroy(&wbr->wbr_wh_rwsem);
	}

	key = br->br_dykey;
	for (i = 0; i < AuBrDynOp; i++, key++)
		if (*key)
			au_dy_put(*key);
		else
			break;

	mntput(br->br_mnt);
	kfree(wbr);
	kfree(br);
}

/*
 * frees all branches
 */
void au_br_free(struct au_sbinfo *sbinfo)
{
	aufs_bindex_t bmax;
	struct au_branch **br;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	bmax = sbinfo->si_bend + 1;
	br = sbinfo->si_branch;
	while (bmax--)
		au_br_do_free(*br++);
}

/*
 * find the index of a branch which is specified by @br_id.
 */
int au_br_index(struct super_block *sb, aufs_bindex_t br_id)
{
	aufs_bindex_t bindex, bend;

	bend = au_sbend(sb);
	for (bindex = 0; bindex <= bend; bindex++)
		if (au_sbr_id(sb, bindex) == br_id)
			return bindex;
	return -1;
}

/* ---------------------------------------------------------------------- */

/*
 * add a branch
 */

static int test_overlap(struct super_block *sb, struct dentry *h_adding,
			struct dentry *h_root)
{
	if (unlikely(h_adding == h_root
		     || au_test_loopback_overlap(sb, h_adding)))
		return 1;
	if (h_adding->d_sb != h_root->d_sb)
		return 0;
	return au_test_subdir(h_adding, h_root)
		|| au_test_subdir(h_root, h_adding);
}

/*
 * returns a newly allocated branch. @new_nbranch is a number of branches
 * after adding a branch.
 */
static struct au_branch *au_br_alloc(struct super_block *sb, int new_nbranch,
				     int perm)
{
	struct au_branch *add_branch;
	struct dentry *root;
	int err;

	err = -ENOMEM;
	root = sb->s_root;
	add_branch = kmalloc(sizeof(*add_branch), GFP_NOFS);
	if (unlikely(!add_branch))
		goto out;

	err = au_hnotify_init_br(add_branch, perm);
	if (unlikely(err))
		goto out_br;

	add_branch->br_wbr = NULL;
	if (au_br_writable(perm)) {
		/* may be freed separately at changing the branch permission */
		add_branch->br_wbr = kmalloc(sizeof(*add_branch->br_wbr),
					     GFP_NOFS);
		if (unlikely(!add_branch->br_wbr))
			goto out_hnotify;
	}

	err = au_sbr_realloc(au_sbi(sb), new_nbranch);
	if (!err)
		err = au_di_realloc(au_di(root), new_nbranch);
	if (!err)
		err = au_ii_realloc(au_ii(root->d_inode), new_nbranch);
	if (!err)
		return add_branch; /* success */

	kfree(add_branch->br_wbr);

out_hnotify:
	au_hnotify_fin_br(add_branch);
out_br:
	kfree(add_branch);
out:
	return ERR_PTR(err);
}

/*
 * test if the branch permission is legal or not.
 */
static int test_br(struct inode *inode, int brperm, char *path)
{
	int err;

	err = (au_br_writable(brperm) && IS_RDONLY(inode));
	if (!err)
		goto out;

	err = -EINVAL;
	pr_err("write permission for readonly mount or inode, %s\n", path);

out:
	return err;
}

/*
 * returns:
 * 0: success, the caller will add it
 * plus: success, it is already unified, the caller should ignore it
 * minus: error
 */
static int test_add(struct super_block *sb, struct au_opt_add *add, int remount)
{
	int err;
	aufs_bindex_t bend, bindex;
	struct dentry *root;
	struct inode *inode, *h_inode;

	root = sb->s_root;
	bend = au_sbend(sb);
	if (unlikely(bend >= 0
		     && au_find_dbindex(root, add->path.dentry) >= 0)) {
		err = 1;
		if (!remount) {
			err = -EINVAL;
			pr_err("%s duplicated\n", add->pathname);
		}
		goto out;
	}

	err = -ENOSPC; /* -E2BIG; */
	if (unlikely(AUFS_BRANCH_MAX <= add->bindex
		     || AUFS_BRANCH_MAX - 1 <= bend)) {
		pr_err("number of branches exceeded %s\n", add->pathname);
		goto out;
	}

	err = -EDOM;
	if (unlikely(add->bindex < 0 || bend + 1 < add->bindex)) {
		pr_err("bad index %d\n", add->bindex);
		goto out;
	}

	inode = add->path.dentry->d_inode;
	err = -ENOENT;
	if (unlikely(!inode->i_nlink)) {
		pr_err("no existence %s\n", add->pathname);
		goto out;
	}

	err = -EINVAL;
	if (unlikely(inode->i_sb == sb)) {
		pr_err("%s must be outside\n", add->pathname);
		goto out;
	}

	if (unlikely(au_test_fs_unsuppoted(inode->i_sb))) {
		pr_err("unsupported filesystem, %s (%s)\n",
		       add->pathname, au_sbtype(inode->i_sb));
		goto out;
	}

	err = test_br(add->path.dentry->d_inode, add->perm, add->pathname);
	if (unlikely(err))
		goto out;

	if (bend < 0)
		return 0; /* success */

	err = -EINVAL;
	for (bindex = 0; bindex <= bend; bindex++)
		if (unlikely(test_overlap(sb, add->path.dentry,
					  au_h_dptr(root, bindex)))) {
			pr_err("%s is overlapped\n", add->pathname);
			goto out;
		}

	err = 0;
	if (au_opt_test(au_mntflags(sb), WARN_PERM)) {
		h_inode = au_h_dptr(root, 0)->d_inode;
		if ((h_inode->i_mode & S_IALLUGO) != (inode->i_mode & S_IALLUGO)
		    || !uid_eq(h_inode->i_uid, inode->i_uid)
		    || !gid_eq(h_inode->i_gid, inode->i_gid))
			pr_warn("uid/gid/perm %s %u/%u/0%o, %u/%u/0%o\n",
				add->pathname,
				i_uid_read(inode), i_gid_read(inode),
				(inode->i_mode & S_IALLUGO),
				i_uid_read(h_inode), i_gid_read(h_inode),
				(h_inode->i_mode & S_IALLUGO));
	}

out:
	return err;
}

/*
 * initialize or clean the whiteouts for an adding branch
 */
static int au_br_init_wh(struct super_block *sb, struct au_branch *br,
			 int new_perm, struct dentry *h_root)
{
	int err, old_perm;
	aufs_bindex_t bindex;
	struct mutex *h_mtx;
	struct au_wbr *wbr;
	struct au_hinode *hdir;

	wbr = br->br_wbr;
	old_perm = br->br_perm;
	br->br_perm = new_perm;
	hdir = NULL;
	h_mtx = NULL;
	bindex = au_br_index(sb, br->br_id);
	if (0 <= bindex) {
		hdir = au_hi(sb->s_root->d_inode, bindex);
		au_hn_imtx_lock_nested(hdir, AuLsc_I_PARENT);
	} else {
		h_mtx = &h_root->d_inode->i_mutex;
		mutex_lock_nested(h_mtx, AuLsc_I_PARENT);
	}
	if (!wbr)
		err = au_wh_init(h_root, br, sb);
	else {
		wbr_wh_write_lock(wbr);
		err = au_wh_init(h_root, br, sb);
		wbr_wh_write_unlock(wbr);
	}
	if (hdir)
		au_hn_imtx_unlock(hdir);
	else
		mutex_unlock(h_mtx);
	br->br_perm = old_perm;

	if (!err && wbr && !au_br_writable(new_perm)) {
		kfree(wbr);
		br->br_wbr = NULL;
	}

	return err;
}

static int au_wbr_init(struct au_branch *br, struct super_block *sb,
		       int perm, struct path *path)
{
	int err;
	struct kstatfs kst;
	struct au_wbr *wbr;
	struct dentry *h_dentry;

	wbr = br->br_wbr;
	au_rw_init(&wbr->wbr_wh_rwsem);
	memset(wbr->wbr_wh, 0, sizeof(wbr->wbr_wh));
	atomic_set(&wbr->wbr_wh_running, 0);
	wbr->wbr_bytes = 0;

	/*
	 * a limit for rmdir/rename a dir
	 * cf. AUFS_MAX_NAMELEN in include/linux/aufs_type.h
	 */
	err = vfs_statfs(path, &kst);
	if (unlikely(err))
		goto out;
	err = -EINVAL;
	h_dentry = path->dentry;
	if (kst.f_namelen >= NAME_MAX)
		err = au_br_init_wh(sb, br, perm, h_dentry);
	else
		pr_err("%.*s(%s), unsupported namelen %ld\n",
		       AuDLNPair(h_dentry), au_sbtype(h_dentry->d_sb),
		       kst.f_namelen);

out:
	return err;
}

/* intialize a new branch */
static int au_br_init(struct au_branch *br, struct super_block *sb,
		      struct au_opt_add *add)
{
	int err;

	err = 0;
	memset(&br->br_xino, 0, sizeof(br->br_xino));
	mutex_init(&br->br_xino.xi_nondir_mtx);
	br->br_perm = add->perm;
	br->br_mnt = add->path.mnt; /* set first, mntget() later */
	spin_lock_init(&br->br_dykey_lock);
	memset(br->br_dykey, 0, sizeof(br->br_dykey));
	atomic_set(&br->br_count, 0);
	br->br_xino_upper = AUFS_XINO_TRUNC_INIT;
	atomic_set(&br->br_xino_running, 0);
	br->br_id = au_new_br_id(sb);
	AuDebugOn(br->br_id < 0);

	if (au_br_writable(add->perm)) {
		err = au_wbr_init(br, sb, add->perm, &add->path);
		if (unlikely(err))
			goto out_err;
	}

	if (au_opt_test(au_mntflags(sb), XINO)) {
		err = au_xino_br(sb, br, add->path.dentry->d_inode->i_ino,
				 au_sbr(sb, 0)->br_xino.xi_file, /*do_test*/1);
		if (unlikely(err)) {
			AuDebugOn(br->br_xino.xi_file);
			goto out_err;
		}
	}

	sysaufs_br_init(br);
	mntget(add->path.mnt);
	goto out; /* success */

out_err:
	br->br_mnt = NULL;
out:
	return err;
}

static void au_br_do_add_brp(struct au_sbinfo *sbinfo, aufs_bindex_t bindex,
			     struct au_branch *br, aufs_bindex_t bend,
			     aufs_bindex_t amount)
{
	struct au_branch **brp;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	brp = sbinfo->si_branch + bindex;
	memmove(brp + 1, brp, sizeof(*brp) * amount);
	*brp = br;
	sbinfo->si_bend++;
	if (unlikely(bend < 0))
		sbinfo->si_bend = 0;
}

static void au_br_do_add_hdp(struct au_dinfo *dinfo, aufs_bindex_t bindex,
			     aufs_bindex_t bend, aufs_bindex_t amount)
{
	struct au_hdentry *hdp;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	hdp = dinfo->di_hdentry + bindex;
	memmove(hdp + 1, hdp, sizeof(*hdp) * amount);
	au_h_dentry_init(hdp);
	dinfo->di_bend++;
	if (unlikely(bend < 0))
		dinfo->di_bstart = 0;
}

static void au_br_do_add_hip(struct au_iinfo *iinfo, aufs_bindex_t bindex,
			     aufs_bindex_t bend, aufs_bindex_t amount)
{
	struct au_hinode *hip;

	AuRwMustWriteLock(&iinfo->ii_rwsem);

	hip = iinfo->ii_hinode + bindex;
	memmove(hip + 1, hip, sizeof(*hip) * amount);
	hip->hi_inode = NULL;
	au_hn_init(hip);
	iinfo->ii_bend++;
	if (unlikely(bend < 0))
		iinfo->ii_bstart = 0;
}

static void au_br_do_add(struct super_block *sb, struct dentry *h_dentry,
			 struct au_branch *br, aufs_bindex_t bindex)
{
	struct dentry *root;
	struct inode *root_inode;
	aufs_bindex_t bend, amount;

	root = sb->s_root;
	root_inode = root->d_inode;
	bend = au_sbend(sb);
	amount = bend + 1 - bindex;
	au_sbilist_lock();
	au_br_do_add_brp(au_sbi(sb), bindex, br, bend, amount);
	au_br_do_add_hdp(au_di(root), bindex, bend, amount);
	au_br_do_add_hip(au_ii(root_inode), bindex, bend, amount);
	au_set_h_dptr(root, bindex, dget(h_dentry));
	au_set_h_iptr(root_inode, bindex, au_igrab(h_dentry->d_inode),
		      /*flags*/0);
	au_sbilist_unlock();
}

int au_br_add(struct super_block *sb, struct au_opt_add *add, int remount)
{
	int err;
	aufs_bindex_t bend, add_bindex;
	struct dentry *root, *h_dentry;
	struct inode *root_inode;
	struct au_branch *add_branch;

	root = sb->s_root;
	root_inode = root->d_inode;
	IMustLock(root_inode);
	err = test_add(sb, add, remount);
	if (unlikely(err < 0))
		goto out;
	if (err) {
		err = 0;
		goto out; /* success */
	}

	bend = au_sbend(sb);
	add_branch = au_br_alloc(sb, bend + 2, add->perm);
	err = PTR_ERR(add_branch);
	if (IS_ERR(add_branch))
		goto out;

	err = au_br_init(add_branch, sb, add);
	if (unlikely(err)) {
		au_br_do_free(add_branch);
		goto out;
	}

	add_bindex = add->bindex;
	h_dentry = add->path.dentry;
	if (!remount)
		au_br_do_add(sb, h_dentry, add_branch, add_bindex);
	else {
		sysaufs_brs_del(sb, add_bindex);
		au_br_do_add(sb, h_dentry, add_branch, add_bindex);
		sysaufs_brs_add(sb, add_bindex);
	}

	if (!add_bindex) {
		au_cpup_attr_all(root_inode, /*force*/1);
		sb->s_maxbytes = h_dentry->d_sb->s_maxbytes;
	} else
		au_add_nlink(root_inode, h_dentry->d_inode);

	/*
	 * this test/set prevents aufs from handling unnecesary notify events
	 * of xino files, in case of re-adding a writable branch which was
	 * once detached from aufs.
	 */
	if (au_xino_brid(sb) < 0
	    && au_br_writable(add_branch->br_perm)
	    && !au_test_fs_bad_xino(h_dentry->d_sb)
	    && add_branch->br_xino.xi_file
	    && add_branch->br_xino.xi_file->f_dentry->d_parent == h_dentry)
		au_xino_brid_set(sb, add_branch->br_id);

out:
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * delete a branch
 */

/* to show the line number, do not make it inlined function */
#define AuVerbose(do_info, fmt, ...) do { \
	if (do_info) \
		pr_info(fmt, ##__VA_ARGS__); \
} while (0)

static int au_test_ibusy(struct inode *inode, aufs_bindex_t bstart,
			 aufs_bindex_t bend)
{
	return (inode && !S_ISDIR(inode->i_mode)) || bstart == bend;
}

static int au_test_dbusy(struct dentry *dentry, aufs_bindex_t bstart,
			 aufs_bindex_t bend)
{
	return au_test_ibusy(dentry->d_inode, bstart, bend);
}

/*
 * test if the branch is deletable or not.
 */
static int test_dentry_busy(struct dentry *root, aufs_bindex_t bindex,
			    unsigned int sigen, const unsigned int verbose)
{
	int err, i, j, ndentry;
	aufs_bindex_t bstart, bend;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry *d;

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	err = au_dcsub_pages(&dpages, root, NULL, NULL);
	if (unlikely(err))
		goto out_dpages;

	for (i = 0; !err && i < dpages.ndpage; i++) {
		dpage = dpages.dpages + i;
		ndentry = dpage->ndentry;
		for (j = 0; !err && j < ndentry; j++) {
			d = dpage->dentries[j];
			AuDebugOn(!d->d_count);
			if (!au_digen_test(d, sigen)) {
				di_read_lock_child(d, AuLock_IR);
				if (unlikely(au_dbrange_test(d))) {
					di_read_unlock(d, AuLock_IR);
					continue;
				}
			} else {
				di_write_lock_child(d);
				if (unlikely(au_dbrange_test(d))) {
					di_write_unlock(d);
					continue;
				}
				err = au_reval_dpath(d, sigen);
				if (!err)
					di_downgrade_lock(d, AuLock_IR);
				else {
					di_write_unlock(d);
					break;
				}
			}

			/* AuDbgDentry(d); */
			bstart = au_dbstart(d);
			bend = au_dbend(d);
			if (bstart <= bindex
			    && bindex <= bend
			    && au_h_dptr(d, bindex)
			    && au_test_dbusy(d, bstart, bend)) {
				err = -EBUSY;
				AuVerbose(verbose, "busy %.*s\n", AuDLNPair(d));
				AuDbgDentry(d);
			}
			di_read_unlock(d, AuLock_IR);
		}
	}

out_dpages:
	au_dpages_free(&dpages);
out:
	return err;
}

static int test_inode_busy(struct super_block *sb, aufs_bindex_t bindex,
			   unsigned int sigen, const unsigned int verbose)
{
	int err;
	unsigned long long max, ull;
	struct inode *i, **array;
	aufs_bindex_t bstart, bend;

	array = au_iarray_alloc(sb, &max);
	err = PTR_ERR(array);
	if (IS_ERR(array))
		goto out;

	err = 0;
	AuDbg("b%d\n", bindex);
	for (ull = 0; !err && ull < max; ull++) {
		i = array[ull];
		if (i->i_ino == AUFS_ROOT_INO)
			continue;

		/* AuDbgInode(i); */
		if (au_iigen(i) == sigen)
			ii_read_lock_child(i);
		else {
			ii_write_lock_child(i);
			err = au_refresh_hinode_self(i);
			au_iigen_dec(i);
			if (!err)
				ii_downgrade_lock(i);
			else {
				ii_write_unlock(i);
				break;
			}
		}

		bstart = au_ibstart(i);
		bend = au_ibend(i);
		if (bstart <= bindex
		    && bindex <= bend
		    && au_h_iptr(i, bindex)
		    && au_test_ibusy(i, bstart, bend)) {
			err = -EBUSY;
			AuVerbose(verbose, "busy i%lu\n", i->i_ino);
			AuDbgInode(i);
		}
		ii_read_unlock(i);
	}
	au_iarray_free(array, max);

out:
	return err;
}

static int test_children_busy(struct dentry *root, aufs_bindex_t bindex,
			      const unsigned int verbose)
{
	int err;
	unsigned int sigen;

	sigen = au_sigen(root->d_sb);
	DiMustNoWaiters(root);
	IiMustNoWaiters(root->d_inode);
	di_write_unlock(root);
	err = test_dentry_busy(root, bindex, sigen, verbose);
	if (!err)
		err = test_inode_busy(root->d_sb, bindex, sigen, verbose);
	di_write_lock_child(root); /* aufs_write_lock() calls ..._child() */

	return err;
}

static void au_br_do_del_brp(struct au_sbinfo *sbinfo,
			     const aufs_bindex_t bindex,
			     const aufs_bindex_t bend)
{
	struct au_branch **brp, **p;

	AuRwMustWriteLock(&sbinfo->si_rwsem);

	brp = sbinfo->si_branch + bindex;
	if (bindex < bend)
		memmove(brp, brp + 1, sizeof(*brp) * (bend - bindex));
	sbinfo->si_branch[0 + bend] = NULL;
	sbinfo->si_bend--;

	p = krealloc(sbinfo->si_branch, sizeof(*p) * bend, AuGFP_SBILIST);
	if (p)
		sbinfo->si_branch = p;
	/* harmless error */
}

static void au_br_do_del_hdp(struct au_dinfo *dinfo, const aufs_bindex_t bindex,
			     const aufs_bindex_t bend)
{
	struct au_hdentry *hdp, *p;

	AuRwMustWriteLock(&dinfo->di_rwsem);

	hdp = dinfo->di_hdentry;
	if (bindex < bend)
		memmove(hdp + bindex, hdp + bindex + 1,
			sizeof(*hdp) * (bend - bindex));
	hdp[0 + bend].hd_dentry = NULL;
	dinfo->di_bend--;

	p = krealloc(hdp, sizeof(*p) * bend, AuGFP_SBILIST);
	if (p)
		dinfo->di_hdentry = p;
	/* harmless error */
}

static void au_br_do_del_hip(struct au_iinfo *iinfo, const aufs_bindex_t bindex,
			     const aufs_bindex_t bend)
{
	struct au_hinode *hip, *p;

	AuRwMustWriteLock(&iinfo->ii_rwsem);

	hip = iinfo->ii_hinode + bindex;
	if (bindex < bend)
		memmove(hip, hip + 1, sizeof(*hip) * (bend - bindex));
	iinfo->ii_hinode[0 + bend].hi_inode = NULL;
	au_hn_init(iinfo->ii_hinode + bend);
	iinfo->ii_bend--;

	p = krealloc(iinfo->ii_hinode, sizeof(*p) * bend, AuGFP_SBILIST);
	if (p)
		iinfo->ii_hinode = p;
	/* harmless error */
}

static void au_br_do_del(struct super_block *sb, aufs_bindex_t bindex,
			 struct au_branch *br)
{
	aufs_bindex_t bend;
	struct au_sbinfo *sbinfo;
	struct dentry *root, *h_root;
	struct inode *inode, *h_inode;
	struct au_hinode *hinode;

	SiMustWriteLock(sb);

	root = sb->s_root;
	inode = root->d_inode;
	sbinfo = au_sbi(sb);
	bend = sbinfo->si_bend;

	h_root = au_h_dptr(root, bindex);
	hinode = au_hi(inode, bindex);
	h_inode = au_igrab(hinode->hi_inode);
	au_hiput(hinode);

	au_sbilist_lock();
	au_br_do_del_brp(sbinfo, bindex, bend);
	au_br_do_del_hdp(au_di(root), bindex, bend);
	au_br_do_del_hip(au_ii(inode), bindex, bend);
	au_sbilist_unlock();

	dput(h_root);
	iput(h_inode);
	au_br_do_free(br);
}

int au_br_del(struct super_block *sb, struct au_opt_del *del, int remount)
{
	int err, rerr, i;
	unsigned int mnt_flags;
	aufs_bindex_t bindex, bend, br_id;
	unsigned char do_wh, verbose;
	struct au_branch *br;
	struct au_wbr *wbr;

	err = 0;
	bindex = au_find_dbindex(sb->s_root, del->h_path.dentry);
	if (bindex < 0) {
		if (remount)
			goto out; /* success */
		err = -ENOENT;
		pr_err("%s no such branch\n", del->pathname);
		goto out;
	}
	AuDbg("bindex b%d\n", bindex);

	err = -EBUSY;
	mnt_flags = au_mntflags(sb);
	verbose = !!au_opt_test(mnt_flags, VERBOSE);
	bend = au_sbend(sb);
	if (unlikely(!bend)) {
		AuVerbose(verbose, "no more branches left\n");
		goto out;
	}
	br = au_sbr(sb, bindex);
	i = atomic_read(&br->br_count);
	if (unlikely(i)) {
		AuVerbose(verbose, "%d file(s) opened\n", i);
		goto out;
	}

	wbr = br->br_wbr;
	do_wh = wbr && (wbr->wbr_whbase || wbr->wbr_plink || wbr->wbr_orph);
	if (do_wh) {
		/* instead of WbrWhMustWriteLock(wbr) */
		SiMustWriteLock(sb);
		for (i = 0; i < AuBrWh_Last; i++) {
			dput(wbr->wbr_wh[i]);
			wbr->wbr_wh[i] = NULL;
		}
	}

	err = test_children_busy(sb->s_root, bindex, verbose);
	if (unlikely(err)) {
		if (do_wh)
			goto out_wh;
		goto out;
	}

	err = 0;
	br_id = br->br_id;
	if (!remount)
		au_br_do_del(sb, bindex, br);
	else {
		sysaufs_brs_del(sb, bindex);
		au_br_do_del(sb, bindex, br);
		sysaufs_brs_add(sb, bindex);
	}

	if (!bindex) {
		au_cpup_attr_all(sb->s_root->d_inode, /*force*/1);
		sb->s_maxbytes = au_sbr_sb(sb, 0)->s_maxbytes;
	} else
		au_sub_nlink(sb->s_root->d_inode, del->h_path.dentry->d_inode);
	if (au_opt_test(mnt_flags, PLINK))
		au_plink_half_refresh(sb, br_id);

	if (au_xino_brid(sb) == br_id)
		au_xino_brid_set(sb, -1);
	goto out; /* success */

out_wh:
	/* revert */
	rerr = au_br_init_wh(sb, br, br->br_perm, del->h_path.dentry);
	if (rerr)
		pr_warn("failed re-creating base whiteout, %s. (%d)\n",
			del->pathname, rerr);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

static int au_ibusy(struct super_block *sb, struct aufs_ibusy __user *arg)
{
	int err;
	aufs_bindex_t bstart, bend;
	struct aufs_ibusy ibusy;
	struct inode *inode, *h_inode;

	err = -EPERM;
	if (unlikely(!capable(CAP_SYS_ADMIN)))
		goto out;

	err = copy_from_user(&ibusy, arg, sizeof(ibusy));
	if (!err)
		err = !access_ok(VERIFY_WRITE, &arg->h_ino, sizeof(arg->h_ino));
	if (unlikely(err)) {
		err = -EFAULT;
		AuTraceErr(err);
		goto out;
	}

	err = -EINVAL;
	si_read_lock(sb, AuLock_FLUSH);
	if (unlikely(ibusy.bindex < 0 || ibusy.bindex > au_sbend(sb)))
		goto out_unlock;

	err = 0;
	ibusy.h_ino = 0; /* invalid */
	inode = ilookup(sb, ibusy.ino);
	if (!inode
	    || inode->i_ino == AUFS_ROOT_INO
	    || is_bad_inode(inode))
		goto out_unlock;

	ii_read_lock_child(inode);
	bstart = au_ibstart(inode);
	bend = au_ibend(inode);
	if (bstart <= ibusy.bindex && ibusy.bindex <= bend) {
		h_inode = au_h_iptr(inode, ibusy.bindex);
		if (h_inode && au_test_ibusy(inode, bstart, bend))
			ibusy.h_ino = h_inode->i_ino;
	}
	ii_read_unlock(inode);
	iput(inode);

out_unlock:
	si_read_unlock(sb);
	if (!err) {
		err = __put_user(ibusy.h_ino, &arg->h_ino);
		if (unlikely(err)) {
			err = -EFAULT;
			AuTraceErr(err);
		}
	}
out:
	return err;
}

long au_ibusy_ioctl(struct file *file, unsigned long arg)
{
	return au_ibusy(file->f_dentry->d_sb, (void __user *)arg);
}

#ifdef CONFIG_COMPAT
long au_ibusy_compat_ioctl(struct file *file, unsigned long arg)
{
	return au_ibusy(file->f_dentry->d_sb, compat_ptr(arg));
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * change a branch permission
 */

static void au_warn_ima(void)
{
#ifdef CONFIG_IMA
	/* since it doesn't support mark_files_ro() */
	AuWarn1("RW -> RO makes IMA to produce wrong message\n");
#endif
}

static int do_need_sigen_inc(int a, int b)
{
	return au_br_whable(a) && !au_br_whable(b);
}

static int need_sigen_inc(int old, int new)
{
	return do_need_sigen_inc(old, new)
		|| do_need_sigen_inc(new, old);
}

static unsigned long long au_farray_cb(void *a,
				       unsigned long long max __maybe_unused,
				       void *arg)
{
	unsigned long long n;
	struct file **p, *f;
	struct super_block *sb = arg;

	n = 0;
	p = a;
	lg_global_lock(&files_lglock);
	do_file_list_for_each_entry(sb, f) {
		if (au_fi(f)
		    && file_count(f)
		    && !special_file(f->f_dentry->d_inode->i_mode)) {
			get_file(f);
			*p++ = f;
			n++;
			AuDebugOn(n > max);
		}
	} while_file_list_for_each_entry;
	lg_global_unlock(&files_lglock);

	return n;
}

static struct file **au_farray_alloc(struct super_block *sb,
				     unsigned long long *max)
{
	*max = atomic_long_read(&au_sbi(sb)->si_nfiles);
	return au_array_alloc(max, au_farray_cb, sb);
}

static void au_farray_free(struct file **a, unsigned long long max)
{
	unsigned long long ull;

	for (ull = 0; ull < max; ull++)
		if (a[ull])
			fput(a[ull]);
	au_array_free(a);
}

static int au_br_mod_files_ro(struct super_block *sb, aufs_bindex_t bindex)
{
	int err, do_warn;
	unsigned int mnt_flags;
	unsigned long long ull, max;
	aufs_bindex_t br_id;
	unsigned char verbose;
	struct file *file, *hf, **array;
	struct inode *inode;
	struct au_hfile *hfile;

	mnt_flags = au_mntflags(sb);
	verbose = !!au_opt_test(mnt_flags, VERBOSE);

	array = au_farray_alloc(sb, &max);
	err = PTR_ERR(array);
	if (IS_ERR(array))
		goto out;

	do_warn = 0;
	br_id = au_sbr_id(sb, bindex);
	for (ull = 0; ull < max; ull++) {
		file = array[ull];

		/* AuDbg("%.*s\n", AuDLNPair(file->f_dentry)); */
		fi_read_lock(file);
		if (unlikely(au_test_mmapped(file))) {
			err = -EBUSY;
			AuVerbose(verbose, "mmapped %.*s\n",
				  AuDLNPair(file->f_dentry));
			AuDbgFile(file);
			FiMustNoWaiters(file);
			fi_read_unlock(file);
			goto out_array;
		}

		inode = file->f_dentry->d_inode;
		hfile = &au_fi(file)->fi_htop;
		hf = hfile->hf_file;
		if (!S_ISREG(inode->i_mode)
		    || !(file->f_mode & FMODE_WRITE)
		    || hfile->hf_br->br_id != br_id
		    || !(hf->f_mode & FMODE_WRITE))
			array[ull] = NULL;
		else {
			do_warn = 1;
			get_file(file);
		}

		FiMustNoWaiters(file);
		fi_read_unlock(file);
		fput(file);
	}

	err = 0;
	if (do_warn)
		au_warn_ima();

	for (ull = 0; ull < max; ull++) {
		file = array[ull];
		if (!file)
			continue;

		/* todo: already flushed? */
		/* cf. fs/super.c:mark_files_ro() */
		/* fi_read_lock(file); */
		hfile = &au_fi(file)->fi_htop;
		hf = hfile->hf_file;
		/* fi_read_unlock(file); */
		spin_lock(&hf->f_lock);
		hf->f_mode &= ~FMODE_WRITE;
		spin_unlock(&hf->f_lock);
		if (!file_check_writeable(hf)) {
			file_release_write(hf);
			mnt_drop_write(hf->f_vfsmnt);
		}
	}

out_array:
	au_farray_free(array, max);
out:
	AuTraceErr(err);
	return err;
}

int au_br_mod(struct super_block *sb, struct au_opt_mod *mod, int remount,
	      int *do_refresh)
{
	int err, rerr;
	aufs_bindex_t bindex;
	struct path path;
	struct dentry *root;
	struct au_branch *br;

	root = sb->s_root;
	bindex = au_find_dbindex(root, mod->h_root);
	if (bindex < 0) {
		if (remount)
			return 0; /* success */
		err = -ENOENT;
		pr_err("%s no such branch\n", mod->path);
		goto out;
	}
	AuDbg("bindex b%d\n", bindex);

	err = test_br(mod->h_root->d_inode, mod->perm, mod->path);
	if (unlikely(err))
		goto out;

	br = au_sbr(sb, bindex);
	if (br->br_perm == mod->perm)
		return 0; /* success */

	if (au_br_writable(br->br_perm)) {
		/* remove whiteout base */
		err = au_br_init_wh(sb, br, mod->perm, mod->h_root);
		if (unlikely(err))
			goto out;

		if (!au_br_writable(mod->perm)) {
			/* rw --> ro, file might be mmapped */
			DiMustNoWaiters(root);
			IiMustNoWaiters(root->d_inode);
			di_write_unlock(root);
			err = au_br_mod_files_ro(sb, bindex);
			/* aufs_write_lock() calls ..._child() */
			di_write_lock_child(root);

			if (unlikely(err)) {
				rerr = -ENOMEM;
				br->br_wbr = kmalloc(sizeof(*br->br_wbr),
						     GFP_NOFS);
				if (br->br_wbr) {
					path.mnt = br->br_mnt;
					path.dentry = mod->h_root;
					rerr = au_wbr_init(br, sb, br->br_perm,
							   &path);
				}
				if (unlikely(rerr)) {
					AuIOErr("nested error %d (%d)\n",
						rerr, err);
					br->br_perm = mod->perm;
				}
			}
		}
	} else if (au_br_writable(mod->perm)) {
		/* ro --> rw */
		err = -ENOMEM;
		br->br_wbr = kmalloc(sizeof(*br->br_wbr), GFP_NOFS);
		if (br->br_wbr) {
			path.mnt = br->br_mnt;
			path.dentry = mod->h_root;
			err = au_wbr_init(br, sb, mod->perm, &path);
			if (unlikely(err)) {
				kfree(br->br_wbr);
				br->br_wbr = NULL;
			}
		}
	}

	if (!err) {
		*do_refresh |= need_sigen_inc(br->br_perm, mod->perm);
		br->br_perm = mod->perm;
	}

out:
	AuTraceErr(err);
	return err;
}
