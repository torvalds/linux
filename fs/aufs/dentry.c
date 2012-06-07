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
 * lookup and dentry operations
 */

#include <linux/namei.h>
#include "aufs.h"

static void au_h_nd(struct nameidata *h_nd, struct nameidata *nd)
{
	if (nd) {
		*h_nd = *nd;

		/*
		 * gave up supporting LOOKUP_CREATE/OPEN for lower fs,
		 * due to whiteout and branch permission.
		 */
		h_nd->flags &= ~(/*LOOKUP_PARENT |*/ LOOKUP_OPEN | LOOKUP_CREATE
				 | LOOKUP_FOLLOW | LOOKUP_EXCL);
		/* unnecessary? */
		h_nd->intent.open.file = NULL;
	} else
		memset(h_nd, 0, sizeof(*h_nd));
}

struct au_lkup_one_args {
	struct dentry **errp;
	struct qstr *name;
	struct dentry *h_parent;
	struct au_branch *br;
	struct nameidata *nd;
};

struct dentry *au_lkup_one(struct qstr *name, struct dentry *h_parent,
			   struct au_branch *br, struct nameidata *nd)
{
	struct dentry *h_dentry;
	int err;
	struct nameidata h_nd;

	if (au_test_fs_null_nd(h_parent->d_sb))
		return vfsub_lookup_one_len(name->name, h_parent, name->len);

	au_h_nd(&h_nd, nd);
	h_nd.path.dentry = h_parent;
	h_nd.path.mnt = br->br_mnt;

	err = vfsub_name_hash(name->name, &h_nd.last, name->len);
	h_dentry = ERR_PTR(err);
	if (!err) {
		path_get(&h_nd.path);
		h_dentry = vfsub_lookup_hash(&h_nd);
		path_put(&h_nd.path);
	}

	AuTraceErrPtr(h_dentry);
	return h_dentry;
}

static void au_call_lkup_one(void *args)
{
	struct au_lkup_one_args *a = args;
	*a->errp = au_lkup_one(a->name, a->h_parent, a->br, a->nd);
}

#define AuLkup_ALLOW_NEG	1
#define au_ftest_lkup(flags, name)	((flags) & AuLkup_##name)
#define au_fset_lkup(flags, name) \
	do { (flags) |= AuLkup_##name; } while (0)
#define au_fclr_lkup(flags, name) \
	do { (flags) &= ~AuLkup_##name; } while (0)

struct au_do_lookup_args {
	unsigned int		flags;
	mode_t			type;
	struct nameidata	*nd;
};

/*
 * returns positive/negative dentry, NULL or an error.
 * NULL means whiteout-ed or not-found.
 */
static struct dentry*
au_do_lookup(struct dentry *h_parent, struct dentry *dentry,
	     aufs_bindex_t bindex, struct qstr *wh_name,
	     struct au_do_lookup_args *args)
{
	struct dentry *h_dentry;
	struct inode *h_inode, *inode;
	struct au_branch *br;
	int wh_found, opq;
	unsigned char wh_able;
	const unsigned char allow_neg = !!au_ftest_lkup(args->flags, ALLOW_NEG);

	wh_found = 0;
	br = au_sbr(dentry->d_sb, bindex);
	wh_able = !!au_br_whable(br->br_perm);
	if (wh_able)
		wh_found = au_wh_test(h_parent, wh_name, br, /*try_sio*/0);
	h_dentry = ERR_PTR(wh_found);
	if (!wh_found)
		goto real_lookup;
	if (unlikely(wh_found < 0))
		goto out;

	/* We found a whiteout */
	/* au_set_dbend(dentry, bindex); */
	au_set_dbwh(dentry, bindex);
	if (!allow_neg)
		return NULL; /* success */

real_lookup:
	h_dentry = au_lkup_one(&dentry->d_name, h_parent, br, args->nd);
	if (IS_ERR(h_dentry))
		goto out;

	h_inode = h_dentry->d_inode;
	if (!h_inode) {
		if (!allow_neg)
			goto out_neg;
	} else if (wh_found
		   || (args->type && args->type != (h_inode->i_mode & S_IFMT)))
		goto out_neg;

	if (au_dbend(dentry) <= bindex)
		au_set_dbend(dentry, bindex);
	if (au_dbstart(dentry) < 0 || bindex < au_dbstart(dentry))
		au_set_dbstart(dentry, bindex);
	au_set_h_dptr(dentry, bindex, h_dentry);

	inode = dentry->d_inode;
	if (!h_inode || !S_ISDIR(h_inode->i_mode) || !wh_able
	    || (inode && !S_ISDIR(inode->i_mode)))
		goto out; /* success */

	mutex_lock_nested(&h_inode->i_mutex, AuLsc_I_CHILD);
	opq = au_diropq_test(h_dentry, br);
	mutex_unlock(&h_inode->i_mutex);
	if (opq > 0)
		au_set_dbdiropq(dentry, bindex);
	else if (unlikely(opq < 0)) {
		au_set_h_dptr(dentry, bindex, NULL);
		h_dentry = ERR_PTR(opq);
	}
	goto out;

out_neg:
	dput(h_dentry);
	h_dentry = NULL;
out:
	return h_dentry;
}

static int au_test_shwh(struct super_block *sb, const struct qstr *name)
{
	if (unlikely(!au_opt_test(au_mntflags(sb), SHWH)
		     && !strncmp(name->name, AUFS_WH_PFX, AUFS_WH_PFX_LEN)))
		return -EPERM;
	return 0;
}

/*
 * returns the number of lower positive dentries,
 * otherwise an error.
 * can be called at unlinking with @type is zero.
 */
int au_lkup_dentry(struct dentry *dentry, aufs_bindex_t bstart, mode_t type,
		   struct nameidata *nd)
{
	int npositive, err;
	aufs_bindex_t bindex, btail, bdiropq;
	unsigned char isdir;
	struct qstr whname;
	struct au_do_lookup_args args = {
		.flags	= 0,
		.type	= type,
		.nd	= nd
	};
	const struct qstr *name = &dentry->d_name;
	struct dentry *parent;
	struct inode *inode;

	err = au_test_shwh(dentry->d_sb, name);
	if (unlikely(err))
		goto out;

	err = au_wh_name_alloc(&whname, name);
	if (unlikely(err))
		goto out;

	inode = dentry->d_inode;
	isdir = !!(inode && S_ISDIR(inode->i_mode));
	if (!type)
		au_fset_lkup(args.flags, ALLOW_NEG);

	npositive = 0;
	parent = dget_parent(dentry);
	btail = au_dbtaildir(parent);
	for (bindex = bstart; bindex <= btail; bindex++) {
		struct dentry *h_parent, *h_dentry;
		struct inode *h_inode, *h_dir;

		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry) {
			if (h_dentry->d_inode)
				npositive++;
			if (type != S_IFDIR)
				break;
			continue;
		}
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent)
			continue;
		h_dir = h_parent->d_inode;
		if (!h_dir || !S_ISDIR(h_dir->i_mode))
			continue;

		mutex_lock_nested(&h_dir->i_mutex, AuLsc_I_PARENT);
		h_dentry = au_do_lookup(h_parent, dentry, bindex, &whname,
					&args);
		mutex_unlock(&h_dir->i_mutex);
		err = PTR_ERR(h_dentry);
		if (IS_ERR(h_dentry))
			goto out_parent;
		au_fclr_lkup(args.flags, ALLOW_NEG);

		if (au_dbwh(dentry) >= 0)
			break;
		if (!h_dentry)
			continue;
		h_inode = h_dentry->d_inode;
		if (!h_inode)
			continue;
		npositive++;
		if (!args.type)
			args.type = h_inode->i_mode & S_IFMT;
		if (args.type != S_IFDIR)
			break;
		else if (isdir) {
			/* the type of lower may be different */
			bdiropq = au_dbdiropq(dentry);
			if (bdiropq >= 0 && bdiropq <= bindex)
				break;
		}
	}

	if (npositive) {
		AuLabel(positive);
		au_update_dbstart(dentry);
	}
	err = npositive;
	if (unlikely(!au_opt_test(au_mntflags(dentry->d_sb), UDBA_NONE)
		     && au_dbstart(dentry) < 0)) {
		err = -EIO;
		AuIOErr("both of real entry and whiteout found, %.*s, err %d\n",
			AuDLNPair(dentry), err);
	}

out_parent:
	dput(parent);
	kfree(whname.name);
out:
	return err;
}

struct dentry *au_sio_lkup_one(struct qstr *name, struct dentry *parent,
			       struct au_branch *br)
{
	struct dentry *dentry;
	int wkq_err;

	if (!au_test_h_perm_sio(parent->d_inode, MAY_EXEC))
		dentry = au_lkup_one(name, parent, br, /*nd*/NULL);
	else {
		struct au_lkup_one_args args = {
			.errp		= &dentry,
			.name		= name,
			.h_parent	= parent,
			.br		= br,
			.nd		= NULL
		};

		wkq_err = au_wkq_wait(au_call_lkup_one, &args);
		if (unlikely(wkq_err))
			dentry = ERR_PTR(wkq_err);
	}

	return dentry;
}

/*
 * lookup @dentry on @bindex which should be negative.
 */
int au_lkup_neg(struct dentry *dentry, aufs_bindex_t bindex)
{
	int err;
	struct dentry *parent, *h_parent, *h_dentry;

	parent = dget_parent(dentry);
	h_parent = au_h_dptr(parent, bindex);
	h_dentry = au_sio_lkup_one(&dentry->d_name, h_parent,
				   au_sbr(dentry->d_sb, bindex));
	err = PTR_ERR(h_dentry);
	if (IS_ERR(h_dentry))
		goto out;
	if (unlikely(h_dentry->d_inode)) {
		err = -EIO;
		AuIOErr("%.*s should be negative on b%d.\n",
			AuDLNPair(h_dentry), bindex);
		dput(h_dentry);
		goto out;
	}

	err = 0;
	if (bindex < au_dbstart(dentry))
		au_set_dbstart(dentry, bindex);
	if (au_dbend(dentry) < bindex)
		au_set_dbend(dentry, bindex);
	au_set_h_dptr(dentry, bindex, h_dentry);

out:
	dput(parent);
	return err;
}

/* ---------------------------------------------------------------------- */

/* subset of struct inode */
struct au_iattr {
	unsigned long		i_ino;
	/* unsigned int		i_nlink; */
	kuid_t			i_uid;
	kgid_t			i_gid;
	u64			i_version;
/*
	loff_t			i_size;
	blkcnt_t		i_blocks;
*/
	umode_t			i_mode;
};

static void au_iattr_save(struct au_iattr *ia, struct inode *h_inode)
{
	ia->i_ino = h_inode->i_ino;
	/* ia->i_nlink = h_inode->i_nlink; */
	ia->i_uid = h_inode->i_uid;
	ia->i_gid = h_inode->i_gid;
	ia->i_version = h_inode->i_version;
/*
	ia->i_size = h_inode->i_size;
	ia->i_blocks = h_inode->i_blocks;
*/
	ia->i_mode = (h_inode->i_mode & S_IFMT);
}

static int au_iattr_test(struct au_iattr *ia, struct inode *h_inode)
{
	return ia->i_ino != h_inode->i_ino
		/* || ia->i_nlink != h_inode->i_nlink */
		|| !uid_eq(ia->i_uid, h_inode->i_uid)
		|| !uid_eq(ia->i_gid, h_inode->i_gid)
		|| ia->i_version != h_inode->i_version
/*
		|| ia->i_size != h_inode->i_size
		|| ia->i_blocks != h_inode->i_blocks
*/
		|| ia->i_mode != (h_inode->i_mode & S_IFMT);
}

static int au_h_verify_dentry(struct dentry *h_dentry, struct dentry *h_parent,
			      struct au_branch *br)
{
	int err;
	struct au_iattr ia;
	struct inode *h_inode;
	struct dentry *h_d;
	struct super_block *h_sb;

	err = 0;
	memset(&ia, -1, sizeof(ia));
	h_sb = h_dentry->d_sb;
	h_inode = h_dentry->d_inode;
	if (h_inode)
		au_iattr_save(&ia, h_inode);
	else if (au_test_nfs(h_sb) || au_test_fuse(h_sb))
		/* nfs d_revalidate may return 0 for negative dentry */
		/* fuse d_revalidate always return 0 for negative dentry */
		goto out;

	/* main purpose is namei.c:cached_lookup() and d_revalidate */
	h_d = au_lkup_one(&h_dentry->d_name, h_parent, br, /*nd*/NULL);
	err = PTR_ERR(h_d);
	if (IS_ERR(h_d))
		goto out;

	err = 0;
	if (unlikely(h_d != h_dentry
		     || h_d->d_inode != h_inode
		     || (h_inode && au_iattr_test(&ia, h_inode))))
		err = au_busy_or_stale();
	dput(h_d);

out:
	AuTraceErr(err);
	return err;
}

int au_h_verify(struct dentry *h_dentry, unsigned int udba, struct inode *h_dir,
		struct dentry *h_parent, struct au_branch *br)
{
	int err;

	err = 0;
	if (udba == AuOpt_UDBA_REVAL
	    && !au_test_fs_remote(h_dentry->d_sb)) {
		IMustLock(h_dir);
		err = (h_dentry->d_parent->d_inode != h_dir);
	} else if (udba != AuOpt_UDBA_NONE)
		err = au_h_verify_dentry(h_dentry, h_parent, br);

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_do_refresh_hdentry(struct dentry *dentry, struct dentry *parent)
{
	int err;
	aufs_bindex_t new_bindex, bindex, bend, bwh, bdiropq;
	struct au_hdentry tmp, *p, *q;
	struct au_dinfo *dinfo;
	struct super_block *sb;

	DiMustWriteLock(dentry);

	sb = dentry->d_sb;
	dinfo = au_di(dentry);
	bend = dinfo->di_bend;
	bwh = dinfo->di_bwh;
	bdiropq = dinfo->di_bdiropq;
	p = dinfo->di_hdentry + dinfo->di_bstart;
	for (bindex = dinfo->di_bstart; bindex <= bend; bindex++, p++) {
		if (!p->hd_dentry)
			continue;

		new_bindex = au_br_index(sb, p->hd_id);
		if (new_bindex == bindex)
			continue;

		if (dinfo->di_bwh == bindex)
			bwh = new_bindex;
		if (dinfo->di_bdiropq == bindex)
			bdiropq = new_bindex;
		if (new_bindex < 0) {
			au_hdput(p);
			p->hd_dentry = NULL;
			continue;
		}

		/* swap two lower dentries, and loop again */
		q = dinfo->di_hdentry + new_bindex;
		tmp = *q;
		*q = *p;
		*p = tmp;
		if (tmp.hd_dentry) {
			bindex--;
			p--;
		}
	}

	dinfo->di_bwh = -1;
	if (bwh >= 0 && bwh <= au_sbend(sb) && au_sbr_whable(sb, bwh))
		dinfo->di_bwh = bwh;

	dinfo->di_bdiropq = -1;
	if (bdiropq >= 0
	    && bdiropq <= au_sbend(sb)
	    && au_sbr_whable(sb, bdiropq))
		dinfo->di_bdiropq = bdiropq;

	err = -EIO;
	dinfo->di_bstart = -1;
	dinfo->di_bend = -1;
	bend = au_dbend(parent);
	p = dinfo->di_hdentry;
	for (bindex = 0; bindex <= bend; bindex++, p++)
		if (p->hd_dentry) {
			dinfo->di_bstart = bindex;
			break;
		}

	if (dinfo->di_bstart >= 0) {
		p = dinfo->di_hdentry + bend;
		for (bindex = bend; bindex >= 0; bindex--, p--)
			if (p->hd_dentry) {
				dinfo->di_bend = bindex;
				err = 0;
				break;
			}
	}

	return err;
}

static void au_do_hide(struct dentry *dentry)
{
	struct inode *inode;

	inode = dentry->d_inode;
	if (inode) {
		if (!S_ISDIR(inode->i_mode)) {
			if (inode->i_nlink && !d_unhashed(dentry))
				drop_nlink(inode);
		} else {
			clear_nlink(inode);
			/* stop next lookup */
			inode->i_flags |= S_DEAD;
		}
		smp_mb(); /* necessary? */
	}
	d_drop(dentry);
}

static int au_hide_children(struct dentry *parent)
{
	int err, i, j, ndentry;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry *dentry;

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	err = au_dcsub_pages(&dpages, parent, NULL, NULL);
	if (unlikely(err))
		goto out_dpages;

	/* in reverse order */
	for (i = dpages.ndpage - 1; i >= 0; i--) {
		dpage = dpages.dpages + i;
		ndentry = dpage->ndentry;
		for (j = ndentry - 1; j >= 0; j--) {
			dentry = dpage->dentries[j];
			if (dentry != parent)
				au_do_hide(dentry);
		}
	}

out_dpages:
	au_dpages_free(&dpages);
out:
	return err;
}

static void au_hide(struct dentry *dentry)
{
	int err;
	struct inode *inode;

	AuDbgDentry(dentry);
	inode = dentry->d_inode;
	if (inode && S_ISDIR(inode->i_mode)) {
		/* shrink_dcache_parent(dentry); */
		err = au_hide_children(dentry);
		if (unlikely(err))
			AuIOErr("%.*s, failed hiding children, ignored %d\n",
				AuDLNPair(dentry), err);
	}
	au_do_hide(dentry);
}

/*
 * By adding a dirty branch, a cached dentry may be affected in various ways.
 *
 * a dirty branch is added
 * - on the top of layers
 * - in the middle of layers
 * - to the bottom of layers
 *
 * on the added branch there exists
 * - a whiteout
 * - a diropq
 * - a same named entry
 *   + exist
 *     * negative --> positive
 *     * positive --> positive
 *	 - type is unchanged
 *	 - type is changed
 *   + doesn't exist
 *     * negative --> negative
 *     * positive --> negative (rejected by au_br_del() for non-dir case)
 * - none
 */
static int au_refresh_by_dinfo(struct dentry *dentry, struct au_dinfo *dinfo,
			       struct au_dinfo *tmp)
{
	int err;
	aufs_bindex_t bindex, bend;
	struct {
		struct dentry *dentry;
		struct inode *inode;
		mode_t mode;
	} orig_h, tmp_h;
	struct au_hdentry *hd;
	struct inode *inode, *h_inode;
	struct dentry *h_dentry;

	err = 0;
	AuDebugOn(dinfo->di_bstart < 0);
	orig_h.dentry = dinfo->di_hdentry[dinfo->di_bstart].hd_dentry;
	orig_h.inode = orig_h.dentry->d_inode;
	orig_h.mode = 0;
	if (orig_h.inode)
		orig_h.mode = orig_h.inode->i_mode & S_IFMT;
	memset(&tmp_h, 0, sizeof(tmp_h));
	if (tmp->di_bstart >= 0) {
		tmp_h.dentry = tmp->di_hdentry[tmp->di_bstart].hd_dentry;
		tmp_h.inode = tmp_h.dentry->d_inode;
		if (tmp_h.inode)
			tmp_h.mode = tmp_h.inode->i_mode & S_IFMT;
	}

	inode = dentry->d_inode;
	if (!orig_h.inode) {
		AuDbg("nagative originally\n");
		if (inode) {
			au_hide(dentry);
			goto out;
		}
		AuDebugOn(inode);
		AuDebugOn(dinfo->di_bstart != dinfo->di_bend);
		AuDebugOn(dinfo->di_bdiropq != -1);

		if (!tmp_h.inode) {
			AuDbg("negative --> negative\n");
			/* should have only one negative lower */
			if (tmp->di_bstart >= 0
			    && tmp->di_bstart < dinfo->di_bstart) {
				AuDebugOn(tmp->di_bstart != tmp->di_bend);
				AuDebugOn(dinfo->di_bstart != dinfo->di_bend);
				au_set_h_dptr(dentry, dinfo->di_bstart, NULL);
				au_di_cp(dinfo, tmp);
				hd = tmp->di_hdentry + tmp->di_bstart;
				au_set_h_dptr(dentry, tmp->di_bstart,
					      dget(hd->hd_dentry));
			}
			au_dbg_verify_dinode(dentry);
		} else {
			AuDbg("negative --> positive\n");
			/*
			 * similar to the behaviour of creating with bypassing
			 * aufs.
			 * unhash it in order to force an error in the
			 * succeeding create operation.
			 * we should not set S_DEAD here.
			 */
			d_drop(dentry);
			/* au_di_swap(tmp, dinfo); */
			au_dbg_verify_dinode(dentry);
		}
	} else {
		AuDbg("positive originally\n");
		/* inode may be NULL */
		AuDebugOn(inode && (inode->i_mode & S_IFMT) != orig_h.mode);
		if (!tmp_h.inode) {
			AuDbg("positive --> negative\n");
			/* or bypassing aufs */
			au_hide(dentry);
			if (tmp->di_bwh >= 0 && tmp->di_bwh <= dinfo->di_bstart)
				dinfo->di_bwh = tmp->di_bwh;
			if (inode)
				err = au_refresh_hinode_self(inode);
			au_dbg_verify_dinode(dentry);
		} else if (orig_h.mode == tmp_h.mode) {
			AuDbg("positive --> positive, same type\n");
			if (!S_ISDIR(orig_h.mode)
			    && dinfo->di_bstart > tmp->di_bstart) {
				/*
				 * similar to the behaviour of removing and
				 * creating.
				 */
				au_hide(dentry);
				if (inode)
					err = au_refresh_hinode_self(inode);
				au_dbg_verify_dinode(dentry);
			} else {
				/* fill empty slots */
				if (dinfo->di_bstart > tmp->di_bstart)
					dinfo->di_bstart = tmp->di_bstart;
				if (dinfo->di_bend < tmp->di_bend)
					dinfo->di_bend = tmp->di_bend;
				dinfo->di_bwh = tmp->di_bwh;
				dinfo->di_bdiropq = tmp->di_bdiropq;
				hd = tmp->di_hdentry;
				bend = dinfo->di_bend;
				for (bindex = tmp->di_bstart; bindex <= bend;
				     bindex++) {
					if (au_h_dptr(dentry, bindex))
						continue;
					h_dentry = hd[bindex].hd_dentry;
					if (!h_dentry)
						continue;
					h_inode = h_dentry->d_inode;
					AuDebugOn(!h_inode);
					AuDebugOn(orig_h.mode
						  != (h_inode->i_mode
						      & S_IFMT));
					au_set_h_dptr(dentry, bindex,
						      dget(h_dentry));
				}
				err = au_refresh_hinode(inode, dentry);
				au_dbg_verify_dinode(dentry);
			}
		} else {
			AuDbg("positive --> positive, different type\n");
			/* similar to the behaviour of removing and creating */
			au_hide(dentry);
			if (inode)
				err = au_refresh_hinode_self(inode);
			au_dbg_verify_dinode(dentry);
		}
	}

out:
	return err;
}

int au_refresh_dentry(struct dentry *dentry, struct dentry *parent)
{
	int err, ebrange;
	unsigned int sigen;
	struct au_dinfo *dinfo, *tmp;
	struct super_block *sb;
	struct inode *inode;

	DiMustWriteLock(dentry);
	AuDebugOn(IS_ROOT(dentry));
	AuDebugOn(!parent->d_inode);

	sb = dentry->d_sb;
	inode = dentry->d_inode;
	sigen = au_sigen(sb);
	err = au_digen_test(parent, sigen);
	if (unlikely(err))
		goto out;

	dinfo = au_di(dentry);
	err = au_di_realloc(dinfo, au_sbend(sb) + 1);
	if (unlikely(err))
		goto out;
	ebrange = au_dbrange_test(dentry);
	if (!ebrange)
		ebrange = au_do_refresh_hdentry(dentry, parent);

	if (d_unhashed(dentry) || ebrange) {
		AuDebugOn(au_dbstart(dentry) < 0 && au_dbend(dentry) >= 0);
		if (inode)
			err = au_refresh_hinode_self(inode);
		au_dbg_verify_dinode(dentry);
		if (!err)
			goto out_dgen; /* success */
		goto out;
	}

	/* temporary dinfo */
	AuDbgDentry(dentry);
	err = -ENOMEM;
	tmp = au_di_alloc(sb, AuLsc_DI_TMP);
	if (unlikely(!tmp))
		goto out;
	au_di_swap(tmp, dinfo);
	/* returns the number of positive dentries */
	/*
	 * if current working dir is removed, it returns an error.
	 * but the dentry is legal.
	 */
	err = au_lkup_dentry(dentry, /*bstart*/0, /*type*/0, /*nd*/NULL);
	AuDbgDentry(dentry);
	au_di_swap(tmp, dinfo);
	if (err == -ENOENT)
		err = 0;
	if (err >= 0) {
		/* compare/refresh by dinfo */
		AuDbgDentry(dentry);
		err = au_refresh_by_dinfo(dentry, dinfo, tmp);
		au_dbg_verify_dinode(dentry);
		AuTraceErr(err);
	}
	au_rw_write_unlock(&tmp->di_rwsem);
	au_di_free(tmp);
	if (unlikely(err))
		goto out;

out_dgen:
	au_update_digen(dentry);
out:
	if (unlikely(err && !(dentry->d_flags & DCACHE_NFSFS_RENAMED))) {
		AuIOErr("failed refreshing %.*s, %d\n",
			AuDLNPair(dentry), err);
		AuDbgDentry(dentry);
	}
	AuTraceErr(err);
	return err;
}

static noinline_for_stack
int au_do_h_d_reval(struct dentry *h_dentry, struct nameidata *nd,
		    struct dentry *dentry, aufs_bindex_t bindex)
{
	int err, valid;
	int (*reval)(struct dentry *, struct nameidata *);

	err = 0;
	if (!(h_dentry->d_flags & DCACHE_OP_REVALIDATE))
		goto out;
	reval = h_dentry->d_op->d_revalidate;

	AuDbg("b%d\n", bindex);
	if (au_test_fs_null_nd(h_dentry->d_sb))
		/* it may return tri-state */
		valid = reval(h_dentry, NULL);
	else {
		struct nameidata h_nd;
		int locked;
		struct dentry *parent;

		au_h_nd(&h_nd, nd);
		parent = nd->path.dentry;
		locked = (nd && nd->path.dentry != dentry);
		if (locked)
			di_read_lock_parent(parent, AuLock_IR);
		BUG_ON(bindex > au_dbend(parent));
		h_nd.path.dentry = au_h_dptr(parent, bindex);
		BUG_ON(!h_nd.path.dentry);
		h_nd.path.mnt = au_sbr(parent->d_sb, bindex)->br_mnt;
		path_get(&h_nd.path);
		valid = reval(h_dentry, &h_nd);
		path_put(&h_nd.path);
		if (locked)
			di_read_unlock(parent, AuLock_IR);
	}

	if (unlikely(valid < 0))
		err = valid;
	else if (!valid)
		err = -EINVAL;

out:
	AuTraceErr(err);
	return err;
}

/* todo: remove this */
static int h_d_revalidate(struct dentry *dentry, struct inode *inode,
			  struct nameidata *nd, int do_udba)
{
	int err;
	umode_t mode, h_mode;
	aufs_bindex_t bindex, btail, bstart, ibs, ibe;
	unsigned char plus, unhashed, is_root, h_plus;
	struct inode *h_inode, *h_cached_inode;
	struct dentry *h_dentry;
	struct qstr *name, *h_name;

	err = 0;
	plus = 0;
	mode = 0;
	ibs = -1;
	ibe = -1;
	unhashed = !!d_unhashed(dentry);
	is_root = !!IS_ROOT(dentry);
	name = &dentry->d_name;

	/*
	 * Theoretically, REVAL test should be unnecessary in case of
	 * {FS,I}NOTIFY.
	 * But {fs,i}notify doesn't fire some necessary events,
	 *	IN_ATTRIB for atime/nlink/pageio
	 *	IN_DELETE for NFS dentry
	 * Let's do REVAL test too.
	 */
	if (do_udba && inode) {
		mode = (inode->i_mode & S_IFMT);
		plus = (inode->i_nlink > 0);
		ibs = au_ibstart(inode);
		ibe = au_ibend(inode);
	}

	bstart = au_dbstart(dentry);
	btail = bstart;
	if (inode && S_ISDIR(inode->i_mode))
		btail = au_dbtaildir(dentry);
	for (bindex = bstart; bindex <= btail; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;

		AuDbg("b%d, %.*s\n", bindex, AuDLNPair(h_dentry));
		spin_lock(&h_dentry->d_lock);
		h_name = &h_dentry->d_name;
		if (unlikely(do_udba
			     && !is_root
			     && (unhashed != !!d_unhashed(h_dentry)
				 || name->len != h_name->len
				 || memcmp(name->name, h_name->name, name->len))
			    )) {
			AuDbg("unhash 0x%x 0x%x, %.*s %.*s\n",
				  unhashed, d_unhashed(h_dentry),
				  AuDLNPair(dentry), AuDLNPair(h_dentry));
			spin_unlock(&h_dentry->d_lock);
			goto err;
		}
		spin_unlock(&h_dentry->d_lock);

		err = au_do_h_d_reval(h_dentry, nd, dentry, bindex);
		if (unlikely(err))
			/* do not goto err, to keep the errno */
			break;

		/* todo: plink too? */
		if (!do_udba)
			continue;

		/* UDBA tests */
		h_inode = h_dentry->d_inode;
		if (unlikely(!!inode != !!h_inode))
			goto err;

		h_plus = plus;
		h_mode = mode;
		h_cached_inode = h_inode;
		if (h_inode) {
			h_mode = (h_inode->i_mode & S_IFMT);
			h_plus = (h_inode->i_nlink > 0);
		}
		if (inode && ibs <= bindex && bindex <= ibe)
			h_cached_inode = au_h_iptr(inode, bindex);

		if (unlikely(plus != h_plus
			     || mode != h_mode
			     || h_cached_inode != h_inode))
			goto err;
		continue;

	err:
		err = -EINVAL;
		break;
	}

	return err;
}

/* todo: consolidate with do_refresh() and au_reval_for_attr() */
static int simple_reval_dpath(struct dentry *dentry, unsigned int sigen)
{
	int err;
	struct dentry *parent;

	if (!au_digen_test(dentry, sigen))
		return 0;

	parent = dget_parent(dentry);
	di_read_lock_parent(parent, AuLock_IR);
	AuDebugOn(au_digen_test(parent, sigen));
	au_dbg_verify_gen(parent, sigen);
	err = au_refresh_dentry(dentry, parent);
	di_read_unlock(parent, AuLock_IR);
	dput(parent);
	AuTraceErr(err);
	return err;
}

int au_reval_dpath(struct dentry *dentry, unsigned int sigen)
{
	int err;
	struct dentry *d, *parent;
	struct inode *inode;

	if (!au_ftest_si(au_sbi(dentry->d_sb), FAILED_REFRESH_DIR))
		return simple_reval_dpath(dentry, sigen);

	/* slow loop, keep it simple and stupid */
	/* cf: au_cpup_dirs() */
	err = 0;
	parent = NULL;
	while (au_digen_test(dentry, sigen)) {
		d = dentry;
		while (1) {
			dput(parent);
			parent = dget_parent(d);
			if (!au_digen_test(parent, sigen))
				break;
			d = parent;
		}

		inode = d->d_inode;
		if (d != dentry)
			di_write_lock_child2(d);

		/* someone might update our dentry while we were sleeping */
		if (au_digen_test(d, sigen)) {
			/*
			 * todo: consolidate with simple_reval_dpath(),
			 * do_refresh() and au_reval_for_attr().
			 */
			di_read_lock_parent(parent, AuLock_IR);
			err = au_refresh_dentry(d, parent);
			di_read_unlock(parent, AuLock_IR);
		}

		if (d != dentry)
			di_write_unlock(d);
		dput(parent);
		if (unlikely(err))
			break;
	}

	return err;
}

/*
 * if valid returns 1, otherwise 0.
 */
static int aufs_d_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	int valid, err;
	unsigned int sigen;
	unsigned char do_udba;
	struct super_block *sb;
	struct inode *inode;

	/* todo: support rcu-walk? */
	if (nd && (nd->flags & LOOKUP_RCU))
		return -ECHILD;

	valid = 0;
	if (unlikely(!au_di(dentry)))
		goto out;

	inode = dentry->d_inode;
	if (inode && is_bad_inode(inode))
		goto out;

	valid = 1;
	sb = dentry->d_sb;
	/*
	 * todo: very ugly
	 * i_mutex of parent dir may be held,
	 * but we should not return 'invalid' due to busy.
	 */
	err = aufs_read_lock(dentry, AuLock_FLUSH | AuLock_DW | AuLock_NOPLM);
	if (unlikely(err)) {
		valid = err;
		AuTraceErr(err);
		goto out;
	}
	if (unlikely(au_dbrange_test(dentry))) {
		err = -EINVAL;
		AuTraceErr(err);
		goto out_dgrade;
	}

	sigen = au_sigen(sb);
	if (au_digen_test(dentry, sigen)) {
		AuDebugOn(IS_ROOT(dentry));
		err = au_reval_dpath(dentry, sigen);
		if (unlikely(err)) {
			AuTraceErr(err);
			goto out_dgrade;
		}
	}
	di_downgrade_lock(dentry, AuLock_IR);

	err = -EINVAL;
	if (inode && (IS_DEADDIR(inode) || !inode->i_nlink))
		goto out_inval;

	do_udba = !au_opt_test(au_mntflags(sb), UDBA_NONE);
	if (do_udba && inode) {
		aufs_bindex_t bstart = au_ibstart(inode);
		struct inode *h_inode;

		if (bstart >= 0) {
			h_inode = au_h_iptr(inode, bstart);
			if (h_inode && au_test_higen(inode, h_inode))
				goto out_inval;
		}
	}

	err = h_d_revalidate(dentry, inode, nd, do_udba);
	if (unlikely(!err && do_udba && au_dbstart(dentry) < 0)) {
		err = -EIO;
		AuDbg("both of real entry and whiteout found, %.*s, err %d\n",
		      AuDLNPair(dentry), err);
	}
	goto out_inval;

out_dgrade:
	di_downgrade_lock(dentry, AuLock_IR);
out_inval:
	aufs_read_unlock(dentry, AuLock_IR);
	AuTraceErr(err);
	valid = !err;
out:
	if (!valid) {
		AuDbg("%.*s invalid, %d\n", AuDLNPair(dentry), valid);
		d_drop(dentry);
	}
	return valid;
}

static void aufs_d_release(struct dentry *dentry)
{
	if (au_di(dentry)) {
		au_di_fin(dentry);
		au_hn_di_reinit(dentry);
	}
}

const struct dentry_operations aufs_dop = {
	.d_revalidate	= aufs_d_revalidate,
	.d_release	= aufs_d_release
};
