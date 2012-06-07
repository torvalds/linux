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
 * whiteout for logical deletion and opaque directory
 */

#include "aufs.h"

#define WH_MASK			S_IRUGO

/*
 * If a directory contains this file, then it is opaque.  We start with the
 * .wh. flag so that it is blocked by lookup.
 */
static struct qstr diropq_name = QSTR_INIT(AUFS_WH_DIROPQ,
					   sizeof(AUFS_WH_DIROPQ) - 1);

/*
 * generate whiteout name, which is NOT terminated by NULL.
 * @name: original d_name.name
 * @len: original d_name.len
 * @wh: whiteout qstr
 * returns zero when succeeds, otherwise error.
 * succeeded value as wh->name should be freed by kfree().
 */
int au_wh_name_alloc(struct qstr *wh, const struct qstr *name)
{
	char *p;

	if (unlikely(name->len > PATH_MAX - AUFS_WH_PFX_LEN))
		return -ENAMETOOLONG;

	wh->len = name->len + AUFS_WH_PFX_LEN;
	p = kmalloc(wh->len, GFP_NOFS);
	wh->name = p;
	if (p) {
		memcpy(p, AUFS_WH_PFX, AUFS_WH_PFX_LEN);
		memcpy(p + AUFS_WH_PFX_LEN, name->name, name->len);
		/* smp_mb(); */
		return 0;
	}
	return -ENOMEM;
}

/* ---------------------------------------------------------------------- */

/*
 * test if the @wh_name exists under @h_parent.
 * @try_sio specifies the necessary of super-io.
 */
int au_wh_test(struct dentry *h_parent, struct qstr *wh_name,
	       struct au_branch *br, int try_sio)
{
	int err;
	struct dentry *wh_dentry;

	if (!try_sio)
		wh_dentry = au_lkup_one(wh_name, h_parent, br, /*nd*/NULL);
	else
		wh_dentry = au_sio_lkup_one(wh_name, h_parent, br);
	err = PTR_ERR(wh_dentry);
	if (IS_ERR(wh_dentry))
		goto out;

	err = 0;
	if (!wh_dentry->d_inode)
		goto out_wh; /* success */

	err = 1;
	if (S_ISREG(wh_dentry->d_inode->i_mode))
		goto out_wh; /* success */

	err = -EIO;
	AuIOErr("%.*s Invalid whiteout entry type 0%o.\n",
		AuDLNPair(wh_dentry), wh_dentry->d_inode->i_mode);

out_wh:
	dput(wh_dentry);
out:
	return err;
}

/*
 * test if the @h_dentry sets opaque or not.
 */
int au_diropq_test(struct dentry *h_dentry, struct au_branch *br)
{
	int err;
	struct inode *h_dir;

	h_dir = h_dentry->d_inode;
	err = au_wh_test(h_dentry, &diropq_name, br,
			 au_test_h_perm_sio(h_dir, MAY_EXEC));
	return err;
}

/*
 * returns a negative dentry whose name is unique and temporary.
 */
struct dentry *au_whtmp_lkup(struct dentry *h_parent, struct au_branch *br,
			     struct qstr *prefix)
{
	struct dentry *dentry;
	int i;
	char defname[NAME_MAX - AUFS_MAX_NAMELEN + DNAME_INLINE_LEN + 1],
		*name, *p;
	/* strict atomic_t is unnecessary here */
	static unsigned short cnt;
	struct qstr qs;

	BUILD_BUG_ON(sizeof(cnt) * 2 > AUFS_WH_TMP_LEN);

	name = defname;
	qs.len = sizeof(defname) - DNAME_INLINE_LEN + prefix->len - 1;
	if (unlikely(prefix->len > DNAME_INLINE_LEN)) {
		dentry = ERR_PTR(-ENAMETOOLONG);
		if (unlikely(qs.len > NAME_MAX))
			goto out;
		dentry = ERR_PTR(-ENOMEM);
		name = kmalloc(qs.len + 1, GFP_NOFS);
		if (unlikely(!name))
			goto out;
	}

	/* doubly whiteout-ed */
	memcpy(name, AUFS_WH_PFX AUFS_WH_PFX, AUFS_WH_PFX_LEN * 2);
	p = name + AUFS_WH_PFX_LEN * 2;
	memcpy(p, prefix->name, prefix->len);
	p += prefix->len;
	*p++ = '.';
	AuDebugOn(name + qs.len + 1 - p <= AUFS_WH_TMP_LEN);

	qs.name = name;
	for (i = 0; i < 3; i++) {
		sprintf(p, "%.*x", AUFS_WH_TMP_LEN, cnt++);
		dentry = au_sio_lkup_one(&qs, h_parent, br);
		if (IS_ERR(dentry) || !dentry->d_inode)
			goto out_name;
		dput(dentry);
	}
	/* pr_warn("could not get random name\n"); */
	dentry = ERR_PTR(-EEXIST);
	AuDbg("%.*s\n", AuLNPair(&qs));
	BUG();

out_name:
	if (name != defname)
		kfree(name);
out:
	AuTraceErrPtr(dentry);
	return dentry;
}

/*
 * rename the @h_dentry on @br to the whiteouted temporary name.
 */
int au_whtmp_ren(struct dentry *h_dentry, struct au_branch *br)
{
	int err;
	struct path h_path = {
		.mnt = br->br_mnt
	};
	struct inode *h_dir;
	struct dentry *h_parent;

	h_parent = h_dentry->d_parent; /* dir inode is locked */
	h_dir = h_parent->d_inode;
	IMustLock(h_dir);

	h_path.dentry = au_whtmp_lkup(h_parent, br, &h_dentry->d_name);
	err = PTR_ERR(h_path.dentry);
	if (IS_ERR(h_path.dentry))
		goto out;

	/* under the same dir, no need to lock_rename() */
	err = vfsub_rename(h_dir, h_dentry, h_dir, &h_path);
	AuTraceErr(err);
	dput(h_path.dentry);

out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * functions for removing a whiteout
 */

static int do_unlink_wh(struct inode *h_dir, struct path *h_path)
{
	int force;

	/*
	 * forces superio when the dir has a sticky bit.
	 * this may be a violation of unix fs semantics.
	 */
	force = (h_dir->i_mode & S_ISVTX)
		&& !uid_eq(current_fsuid(), h_path->dentry->d_inode->i_uid);
	return vfsub_unlink(h_dir, h_path, force);
}

int au_wh_unlink_dentry(struct inode *h_dir, struct path *h_path,
			struct dentry *dentry)
{
	int err;

	err = do_unlink_wh(h_dir, h_path);
	if (!err && dentry)
		au_set_dbwh(dentry, -1);

	return err;
}

static int unlink_wh_name(struct dentry *h_parent, struct qstr *wh,
			  struct au_branch *br)
{
	int err;
	struct path h_path = {
		.mnt = br->br_mnt
	};

	err = 0;
	h_path.dentry = au_lkup_one(wh, h_parent, br, /*nd*/NULL);
	if (IS_ERR(h_path.dentry))
		err = PTR_ERR(h_path.dentry);
	else {
		if (h_path.dentry->d_inode
		    && S_ISREG(h_path.dentry->d_inode->i_mode))
			err = do_unlink_wh(h_parent->d_inode, &h_path);
		dput(h_path.dentry);
	}

	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * initialize/clean whiteout for a branch
 */

static void au_wh_clean(struct inode *h_dir, struct path *whpath,
			const int isdir)
{
	int err;

	if (!whpath->dentry->d_inode)
		return;

	err = mnt_want_write(whpath->mnt);
	if (!err) {
		if (isdir)
			err = vfsub_rmdir(h_dir, whpath);
		else
			err = vfsub_unlink(h_dir, whpath, /*force*/0);
		mnt_drop_write(whpath->mnt);
	}
	if (unlikely(err))
		pr_warn("failed removing %.*s (%d), ignored.\n",
			AuDLNPair(whpath->dentry), err);
}

static int test_linkable(struct dentry *h_root)
{
	struct inode *h_dir = h_root->d_inode;

	if (h_dir->i_op->link)
		return 0;

	pr_err("%.*s (%s) doesn't support link(2), use noplink and rw+nolwh\n",
	       AuDLNPair(h_root), au_sbtype(h_root->d_sb));
	return -ENOSYS;
}

/* todo: should this mkdir be done in /sbin/mount.aufs helper? */
static int au_whdir(struct inode *h_dir, struct path *path)
{
	int err;

	err = -EEXIST;
	if (!path->dentry->d_inode) {
		int mode = S_IRWXU;

		if (au_test_nfs(path->dentry->d_sb))
			mode |= S_IXUGO;
		err = mnt_want_write(path->mnt);
		if (!err) {
			err = vfsub_mkdir(h_dir, path, mode);
			mnt_drop_write(path->mnt);
		}
	} else if (S_ISDIR(path->dentry->d_inode->i_mode))
		err = 0;
	else
		pr_err("unknown %.*s exists\n", AuDLNPair(path->dentry));

	return err;
}

struct au_wh_base {
	const struct qstr *name;
	struct dentry *dentry;
};

static void au_wh_init_ro(struct inode *h_dir, struct au_wh_base base[],
			  struct path *h_path)
{
	h_path->dentry = base[AuBrWh_BASE].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/0);
	h_path->dentry = base[AuBrWh_PLINK].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/1);
	h_path->dentry = base[AuBrWh_ORPH].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/1);
}

/*
 * returns tri-state,
 * minus: error, caller should print the mesage
 * zero: succuess
 * plus: error, caller should NOT print the mesage
 */
static int au_wh_init_rw_nolink(struct dentry *h_root, struct au_wbr *wbr,
				int do_plink, struct au_wh_base base[],
				struct path *h_path)
{
	int err;
	struct inode *h_dir;

	h_dir = h_root->d_inode;
	h_path->dentry = base[AuBrWh_BASE].dentry;
	au_wh_clean(h_dir, h_path, /*isdir*/0);
	h_path->dentry = base[AuBrWh_PLINK].dentry;
	if (do_plink) {
		err = test_linkable(h_root);
		if (unlikely(err)) {
			err = 1;
			goto out;
		}

		err = au_whdir(h_dir, h_path);
		if (unlikely(err))
			goto out;
		wbr->wbr_plink = dget(base[AuBrWh_PLINK].dentry);
	} else
		au_wh_clean(h_dir, h_path, /*isdir*/1);
	h_path->dentry = base[AuBrWh_ORPH].dentry;
	err = au_whdir(h_dir, h_path);
	if (unlikely(err))
		goto out;
	wbr->wbr_orph = dget(base[AuBrWh_ORPH].dentry);

out:
	return err;
}

/*
 * for the moment, aufs supports the branch filesystem which does not support
 * link(2). testing on FAT which does not support i_op->setattr() fully either,
 * copyup failed. finally, such filesystem will not be used as the writable
 * branch.
 *
 * returns tri-state, see above.
 */
static int au_wh_init_rw(struct dentry *h_root, struct au_wbr *wbr,
			 int do_plink, struct au_wh_base base[],
			 struct path *h_path)
{
	int err;
	struct inode *h_dir;

	WbrWhMustWriteLock(wbr);

	err = test_linkable(h_root);
	if (unlikely(err)) {
		err = 1;
		goto out;
	}

	/*
	 * todo: should this create be done in /sbin/mount.aufs helper?
	 */
	err = -EEXIST;
	h_dir = h_root->d_inode;
	if (!base[AuBrWh_BASE].dentry->d_inode) {
		err = mnt_want_write(h_path->mnt);
		if (!err) {
			h_path->dentry = base[AuBrWh_BASE].dentry;
			err = vfsub_create(h_dir, h_path, WH_MASK);
			mnt_drop_write(h_path->mnt);
		}
	} else if (S_ISREG(base[AuBrWh_BASE].dentry->d_inode->i_mode))
		err = 0;
	else
		pr_err("unknown %.*s/%.*s exists\n",
		       AuDLNPair(h_root), AuDLNPair(base[AuBrWh_BASE].dentry));
	if (unlikely(err))
		goto out;

	h_path->dentry = base[AuBrWh_PLINK].dentry;
	if (do_plink) {
		err = au_whdir(h_dir, h_path);
		if (unlikely(err))
			goto out;
		wbr->wbr_plink = dget(base[AuBrWh_PLINK].dentry);
	} else
		au_wh_clean(h_dir, h_path, /*isdir*/1);
	wbr->wbr_whbase = dget(base[AuBrWh_BASE].dentry);

	h_path->dentry = base[AuBrWh_ORPH].dentry;
	err = au_whdir(h_dir, h_path);
	if (unlikely(err))
		goto out;
	wbr->wbr_orph = dget(base[AuBrWh_ORPH].dentry);

out:
	return err;
}

/*
 * initialize the whiteout base file/dir for @br.
 */
int au_wh_init(struct dentry *h_root, struct au_branch *br,
	       struct super_block *sb)
{
	int err, i;
	const unsigned char do_plink
		= !!au_opt_test(au_mntflags(sb), PLINK);
	struct path path = {
		.mnt = br->br_mnt
	};
	struct inode *h_dir;
	struct au_wbr *wbr = br->br_wbr;
	static const struct qstr base_name[] = {
		[AuBrWh_BASE] = QSTR_INIT(AUFS_BASE_NAME,
					  sizeof(AUFS_BASE_NAME) - 1),
		[AuBrWh_PLINK] = QSTR_INIT(AUFS_PLINKDIR_NAME,
					   sizeof(AUFS_PLINKDIR_NAME) - 1),
		[AuBrWh_ORPH] = QSTR_INIT(AUFS_ORPHDIR_NAME,
					  sizeof(AUFS_ORPHDIR_NAME) - 1)
	};
	struct au_wh_base base[] = {
		[AuBrWh_BASE] = {
			.name	= base_name + AuBrWh_BASE,
			.dentry	= NULL
		},
		[AuBrWh_PLINK] = {
			.name	= base_name + AuBrWh_PLINK,
			.dentry	= NULL
		},
		[AuBrWh_ORPH] = {
			.name	= base_name + AuBrWh_ORPH,
			.dentry	= NULL
		}
	};

	if (wbr)
		WbrWhMustWriteLock(wbr);

	for (i = 0; i < AuBrWh_Last; i++) {
		/* doubly whiteouted */
		struct dentry *d;

		d = au_wh_lkup(h_root, (void *)base[i].name, br);
		err = PTR_ERR(d);
		if (IS_ERR(d))
			goto out;

		base[i].dentry = d;
		AuDebugOn(wbr
			  && wbr->wbr_wh[i]
			  && wbr->wbr_wh[i] != base[i].dentry);
	}

	if (wbr)
		for (i = 0; i < AuBrWh_Last; i++) {
			dput(wbr->wbr_wh[i]);
			wbr->wbr_wh[i] = NULL;
		}

	err = 0;
	if (!au_br_writable(br->br_perm)) {
		h_dir = h_root->d_inode;
		au_wh_init_ro(h_dir, base, &path);
	} else if (!au_br_wh_linkable(br->br_perm)) {
		err = au_wh_init_rw_nolink(h_root, wbr, do_plink, base, &path);
		if (err > 0)
			goto out;
		else if (err)
			goto out_err;
	} else {
		err = au_wh_init_rw(h_root, wbr, do_plink, base, &path);
		if (err > 0)
			goto out;
		else if (err)
			goto out_err;
	}
	goto out; /* success */

out_err:
	pr_err("an error(%d) on the writable branch %.*s(%s)\n",
	       err, AuDLNPair(h_root), au_sbtype(h_root->d_sb));
out:
	for (i = 0; i < AuBrWh_Last; i++)
		dput(base[i].dentry);
	return err;
}

/* ---------------------------------------------------------------------- */
/*
 * whiteouts are all hard-linked usually.
 * when its link count reaches a ceiling, we create a new whiteout base
 * asynchronously.
 */

struct reinit_br_wh {
	struct super_block *sb;
	struct au_branch *br;
};

static void reinit_br_wh(void *arg)
{
	int err;
	aufs_bindex_t bindex;
	struct path h_path;
	struct reinit_br_wh *a = arg;
	struct au_wbr *wbr;
	struct inode *dir;
	struct dentry *h_root;
	struct au_hinode *hdir;

	err = 0;
	wbr = a->br->br_wbr;
	/* big aufs lock */
	si_noflush_write_lock(a->sb);
	if (!au_br_writable(a->br->br_perm))
		goto out;
	bindex = au_br_index(a->sb, a->br->br_id);
	if (unlikely(bindex < 0))
		goto out;

	di_read_lock_parent(a->sb->s_root, AuLock_IR);
	dir = a->sb->s_root->d_inode;
	hdir = au_hi(dir, bindex);
	h_root = au_h_dptr(a->sb->s_root, bindex);

	au_hn_imtx_lock_nested(hdir, AuLsc_I_PARENT);
	wbr_wh_write_lock(wbr);
	err = au_h_verify(wbr->wbr_whbase, au_opt_udba(a->sb), hdir->hi_inode,
			  h_root, a->br);
	if (!err) {
		err = mnt_want_write(a->br->br_mnt);
		if (!err) {
			h_path.dentry = wbr->wbr_whbase;
			h_path.mnt = a->br->br_mnt;
			err = vfsub_unlink(hdir->hi_inode, &h_path, /*force*/0);
			mnt_drop_write(a->br->br_mnt);
		}
	} else {
		pr_warn("%.*s is moved, ignored\n",
			AuDLNPair(wbr->wbr_whbase));
		err = 0;
	}
	dput(wbr->wbr_whbase);
	wbr->wbr_whbase = NULL;
	if (!err)
		err = au_wh_init(h_root, a->br, a->sb);
	wbr_wh_write_unlock(wbr);
	au_hn_imtx_unlock(hdir);
	di_read_unlock(a->sb->s_root, AuLock_IR);

out:
	if (wbr)
		atomic_dec(&wbr->wbr_wh_running);
	atomic_dec(&a->br->br_count);
	si_write_unlock(a->sb);
	au_nwt_done(&au_sbi(a->sb)->si_nowait);
	kfree(arg);
	if (unlikely(err))
		AuIOErr("err %d\n", err);
}

static void kick_reinit_br_wh(struct super_block *sb, struct au_branch *br)
{
	int do_dec, wkq_err;
	struct reinit_br_wh *arg;

	do_dec = 1;
	if (atomic_inc_return(&br->br_wbr->wbr_wh_running) != 1)
		goto out;

	/* ignore ENOMEM */
	arg = kmalloc(sizeof(*arg), GFP_NOFS);
	if (arg) {
		/*
		 * dec(wh_running), kfree(arg) and dec(br_count)
		 * in reinit function
		 */
		arg->sb = sb;
		arg->br = br;
		atomic_inc(&br->br_count);
		wkq_err = au_wkq_nowait(reinit_br_wh, arg, sb, /*flags*/0);
		if (unlikely(wkq_err)) {
			atomic_dec(&br->br_wbr->wbr_wh_running);
			atomic_dec(&br->br_count);
			kfree(arg);
		}
		do_dec = 0;
	}

out:
	if (do_dec)
		atomic_dec(&br->br_wbr->wbr_wh_running);
}

/* ---------------------------------------------------------------------- */

/*
 * create the whiteout @wh.
 */
static int link_or_create_wh(struct super_block *sb, aufs_bindex_t bindex,
			     struct dentry *wh)
{
	int err;
	struct path h_path = {
		.dentry = wh
	};
	struct au_branch *br;
	struct au_wbr *wbr;
	struct dentry *h_parent;
	struct inode *h_dir;

	h_parent = wh->d_parent; /* dir inode is locked */
	h_dir = h_parent->d_inode;
	IMustLock(h_dir);

	br = au_sbr(sb, bindex);
	h_path.mnt = br->br_mnt;
	wbr = br->br_wbr;
	wbr_wh_read_lock(wbr);
	if (wbr->wbr_whbase) {
		err = vfsub_link(wbr->wbr_whbase, h_dir, &h_path);
		if (!err || err != -EMLINK)
			goto out;

		/* link count full. re-initialize br_whbase. */
		kick_reinit_br_wh(sb, br);
	}

	/* return this error in this context */
	err = vfsub_create(h_dir, &h_path, WH_MASK);

out:
	wbr_wh_read_unlock(wbr);
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * create or remove the diropq.
 */
static struct dentry *do_diropq(struct dentry *dentry, aufs_bindex_t bindex,
				unsigned int flags)
{
	struct dentry *opq_dentry, *h_dentry;
	struct super_block *sb;
	struct au_branch *br;
	int err;

	sb = dentry->d_sb;
	br = au_sbr(sb, bindex);
	h_dentry = au_h_dptr(dentry, bindex);
	opq_dentry = au_lkup_one(&diropq_name, h_dentry, br, /*nd*/NULL);
	if (IS_ERR(opq_dentry))
		goto out;

	if (au_ftest_diropq(flags, CREATE)) {
		err = link_or_create_wh(sb, bindex, opq_dentry);
		if (!err) {
			au_set_dbdiropq(dentry, bindex);
			goto out; /* success */
		}
	} else {
		struct path tmp = {
			.dentry = opq_dentry,
			.mnt	= br->br_mnt
		};
		err = do_unlink_wh(au_h_iptr(dentry->d_inode, bindex), &tmp);
		if (!err)
			au_set_dbdiropq(dentry, -1);
	}
	dput(opq_dentry);
	opq_dentry = ERR_PTR(err);

out:
	return opq_dentry;
}

struct do_diropq_args {
	struct dentry **errp;
	struct dentry *dentry;
	aufs_bindex_t bindex;
	unsigned int flags;
};

static void call_do_diropq(void *args)
{
	struct do_diropq_args *a = args;
	*a->errp = do_diropq(a->dentry, a->bindex, a->flags);
}

struct dentry *au_diropq_sio(struct dentry *dentry, aufs_bindex_t bindex,
			     unsigned int flags)
{
	struct dentry *diropq, *h_dentry;

	h_dentry = au_h_dptr(dentry, bindex);
	if (!au_test_h_perm_sio(h_dentry->d_inode, MAY_EXEC | MAY_WRITE))
		diropq = do_diropq(dentry, bindex, flags);
	else {
		int wkq_err;
		struct do_diropq_args args = {
			.errp		= &diropq,
			.dentry		= dentry,
			.bindex		= bindex,
			.flags		= flags
		};

		wkq_err = au_wkq_wait(call_do_diropq, &args);
		if (unlikely(wkq_err))
			diropq = ERR_PTR(wkq_err);
	}

	return diropq;
}

/* ---------------------------------------------------------------------- */

/*
 * lookup whiteout dentry.
 * @h_parent: lower parent dentry which must exist and be locked
 * @base_name: name of dentry which will be whiteouted
 * returns dentry for whiteout.
 */
struct dentry *au_wh_lkup(struct dentry *h_parent, struct qstr *base_name,
			  struct au_branch *br)
{
	int err;
	struct qstr wh_name;
	struct dentry *wh_dentry;

	err = au_wh_name_alloc(&wh_name, base_name);
	wh_dentry = ERR_PTR(err);
	if (!err) {
		wh_dentry = au_lkup_one(&wh_name, h_parent, br, /*nd*/NULL);
		kfree(wh_name.name);
	}
	return wh_dentry;
}

/*
 * link/create a whiteout for @dentry on @bindex.
 */
struct dentry *au_wh_create(struct dentry *dentry, aufs_bindex_t bindex,
			    struct dentry *h_parent)
{
	struct dentry *wh_dentry;
	struct super_block *sb;
	int err;

	sb = dentry->d_sb;
	wh_dentry = au_wh_lkup(h_parent, &dentry->d_name, au_sbr(sb, bindex));
	if (!IS_ERR(wh_dentry) && !wh_dentry->d_inode) {
		err = link_or_create_wh(sb, bindex, wh_dentry);
		if (!err)
			au_set_dbwh(dentry, bindex);
		else {
			dput(wh_dentry);
			wh_dentry = ERR_PTR(err);
		}
	}

	return wh_dentry;
}

/* ---------------------------------------------------------------------- */

/* Delete all whiteouts in this directory on branch bindex. */
static int del_wh_children(struct dentry *h_dentry, struct au_nhash *whlist,
			   aufs_bindex_t bindex, struct au_branch *br)
{
	int err;
	unsigned long ul, n;
	struct qstr wh_name;
	char *p;
	struct hlist_head *head;
	struct au_vdir_wh *tpos;
	struct hlist_node *pos;
	struct au_vdir_destr *str;

	err = -ENOMEM;
	p = __getname_gfp(GFP_NOFS);
	wh_name.name = p;
	if (unlikely(!wh_name.name))
		goto out;

	err = 0;
	memcpy(p, AUFS_WH_PFX, AUFS_WH_PFX_LEN);
	p += AUFS_WH_PFX_LEN;
	n = whlist->nh_num;
	head = whlist->nh_head;
	for (ul = 0; !err && ul < n; ul++, head++) {
		hlist_for_each_entry(tpos, pos, head, wh_hash) {
			if (tpos->wh_bindex != bindex)
				continue;

			str = &tpos->wh_str;
			if (str->len + AUFS_WH_PFX_LEN <= PATH_MAX) {
				memcpy(p, str->name, str->len);
				wh_name.len = AUFS_WH_PFX_LEN + str->len;
				err = unlink_wh_name(h_dentry, &wh_name, br);
				if (!err)
					continue;
				break;
			}
			AuIOErr("whiteout name too long %.*s\n",
				str->len, str->name);
			err = -EIO;
			break;
		}
	}
	__putname(wh_name.name);

out:
	return err;
}

struct del_wh_children_args {
	int *errp;
	struct dentry *h_dentry;
	struct au_nhash *whlist;
	aufs_bindex_t bindex;
	struct au_branch *br;
};

static void call_del_wh_children(void *args)
{
	struct del_wh_children_args *a = args;
	*a->errp = del_wh_children(a->h_dentry, a->whlist, a->bindex, a->br);
}

/* ---------------------------------------------------------------------- */

struct au_whtmp_rmdir *au_whtmp_rmdir_alloc(struct super_block *sb, gfp_t gfp)
{
	struct au_whtmp_rmdir *whtmp;
	int err;
	unsigned int rdhash;

	SiMustAnyLock(sb);

	whtmp = kmalloc(sizeof(*whtmp), gfp);
	if (unlikely(!whtmp)) {
		whtmp = ERR_PTR(-ENOMEM);
		goto out;
	}

	whtmp->dir = NULL;
	whtmp->br = NULL;
	whtmp->wh_dentry = NULL;
	/* no estimation for dir size */
	rdhash = au_sbi(sb)->si_rdhash;
	if (!rdhash)
		rdhash = AUFS_RDHASH_DEF;
	err = au_nhash_alloc(&whtmp->whlist, rdhash, gfp);
	if (unlikely(err)) {
		kfree(whtmp);
		whtmp = ERR_PTR(err);
	}

out:
	return whtmp;
}

void au_whtmp_rmdir_free(struct au_whtmp_rmdir *whtmp)
{
	if (whtmp->br)
		atomic_dec(&whtmp->br->br_count);
	dput(whtmp->wh_dentry);
	iput(whtmp->dir);
	au_nhash_wh_free(&whtmp->whlist);
	kfree(whtmp);
}

/*
 * rmdir the whiteouted temporary named dir @h_dentry.
 * @whlist: whiteouted children.
 */
int au_whtmp_rmdir(struct inode *dir, aufs_bindex_t bindex,
		   struct dentry *wh_dentry, struct au_nhash *whlist)
{
	int err;
	struct path h_tmp;
	struct inode *wh_inode, *h_dir;
	struct au_branch *br;

	h_dir = wh_dentry->d_parent->d_inode; /* dir inode is locked */
	IMustLock(h_dir);

	br = au_sbr(dir->i_sb, bindex);
	wh_inode = wh_dentry->d_inode;
	mutex_lock_nested(&wh_inode->i_mutex, AuLsc_I_CHILD);

	/*
	 * someone else might change some whiteouts while we were sleeping.
	 * it means this whlist may have an obsoleted entry.
	 */
	if (!au_test_h_perm_sio(wh_inode, MAY_EXEC | MAY_WRITE))
		err = del_wh_children(wh_dentry, whlist, bindex, br);
	else {
		int wkq_err;
		struct del_wh_children_args args = {
			.errp		= &err,
			.h_dentry	= wh_dentry,
			.whlist		= whlist,
			.bindex		= bindex,
			.br		= br
		};

		wkq_err = au_wkq_wait(call_del_wh_children, &args);
		if (unlikely(wkq_err))
			err = wkq_err;
	}
	mutex_unlock(&wh_inode->i_mutex);

	if (!err) {
		h_tmp.dentry = wh_dentry;
		h_tmp.mnt = br->br_mnt;
		err = vfsub_rmdir(h_dir, &h_tmp);
	}

	if (!err) {
		if (au_ibstart(dir) == bindex) {
			/* todo: dir->i_mutex is necessary */
			au_cpup_attr_timesizes(dir);
			vfsub_drop_nlink(dir);
		}
		return 0; /* success */
	}

	pr_warn("failed removing %.*s(%d), ignored\n",
		AuDLNPair(wh_dentry), err);
	return err;
}

static void call_rmdir_whtmp(void *args)
{
	int err;
	aufs_bindex_t bindex;
	struct au_whtmp_rmdir *a = args;
	struct super_block *sb;
	struct dentry *h_parent;
	struct inode *h_dir;
	struct au_hinode *hdir;

	/* rmdir by nfsd may cause deadlock with this i_mutex */
	/* mutex_lock(&a->dir->i_mutex); */
	err = -EROFS;
	sb = a->dir->i_sb;
	si_read_lock(sb, !AuLock_FLUSH);
	if (!au_br_writable(a->br->br_perm))
		goto out;
	bindex = au_br_index(sb, a->br->br_id);
	if (unlikely(bindex < 0))
		goto out;

	err = -EIO;
	ii_write_lock_parent(a->dir);
	h_parent = dget_parent(a->wh_dentry);
	h_dir = h_parent->d_inode;
	hdir = au_hi(a->dir, bindex);
	au_hn_imtx_lock_nested(hdir, AuLsc_I_PARENT);
	err = au_h_verify(a->wh_dentry, au_opt_udba(sb), h_dir, h_parent,
			  a->br);
	if (!err) {
		err = mnt_want_write(a->br->br_mnt);
		if (!err) {
			err = au_whtmp_rmdir(a->dir, bindex, a->wh_dentry,
					     &a->whlist);
			mnt_drop_write(a->br->br_mnt);
		}
	}
	au_hn_imtx_unlock(hdir);
	dput(h_parent);
	ii_write_unlock(a->dir);

out:
	/* mutex_unlock(&a->dir->i_mutex); */
	au_whtmp_rmdir_free(a);
	si_read_unlock(sb);
	au_nwt_done(&au_sbi(sb)->si_nowait);
	if (unlikely(err))
		AuIOErr("err %d\n", err);
}

void au_whtmp_kick_rmdir(struct inode *dir, aufs_bindex_t bindex,
			 struct dentry *wh_dentry, struct au_whtmp_rmdir *args)
{
	int wkq_err;
	struct super_block *sb;

	IMustLock(dir);

	/* all post-process will be done in do_rmdir_whtmp(). */
	sb = dir->i_sb;
	args->dir = au_igrab(dir);
	args->br = au_sbr(sb, bindex);
	atomic_inc(&args->br->br_count);
	args->wh_dentry = dget(wh_dentry);
	wkq_err = au_wkq_nowait(call_rmdir_whtmp, args, sb, /*flags*/0);
	if (unlikely(wkq_err)) {
		pr_warn("rmdir error %.*s (%d), ignored\n",
			AuDLNPair(wh_dentry), wkq_err);
		au_whtmp_rmdir_free(args);
	}
}
