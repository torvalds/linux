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
 * lookup and dentry operations
 */

#include <linux/namei.h>
#include "aufs.h"

/*
 * returns positive/negative dentry, NULL or an error.
 * NULL means whiteout-ed or not-found.
 */
static struct dentry*
au_do_lookup(struct dentry *h_parent, struct dentry *dentry,
	     aufs_bindex_t bindex, struct au_do_lookup_args *args)
{
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct au_branch *br;
	int wh_found, opq;
	unsigned char wh_able;
	const unsigned char allow_neg = !!au_ftest_lkup(args->flags, ALLOW_NEG);
	const unsigned char ignore_perm = !!au_ftest_lkup(args->flags,
							  IGNORE_PERM);

	wh_found = 0;
	br = au_sbr(dentry->d_sb, bindex);
	wh_able = !!au_br_whable(br->br_perm);
	if (wh_able)
		wh_found = au_wh_test(h_parent, &args->whname, ignore_perm);
	h_dentry = ERR_PTR(wh_found);
	if (!wh_found)
		goto real_lookup;
	if (unlikely(wh_found < 0))
		goto out;

	/* We found a whiteout */
	/* au_set_dbbot(dentry, bindex); */
	au_set_dbwh(dentry, bindex);
	if (!allow_neg)
		return NULL; /* success */

real_lookup:
	if (!ignore_perm)
		h_dentry = vfsub_lkup_one(args->name, h_parent);
	else
		h_dentry = au_sio_lkup_one(args->name, h_parent);
	if (IS_ERR(h_dentry)) {
		if (PTR_ERR(h_dentry) == -ENAMETOOLONG
		    && !allow_neg)
			h_dentry = NULL;
		goto out;
	}

	h_inode = d_inode(h_dentry);
	if (d_is_negative(h_dentry)) {
		if (!allow_neg)
			goto out_neg;
	} else if (wh_found
		   || (args->type && args->type != (h_inode->i_mode & S_IFMT)))
		goto out_neg;
	else if (au_ftest_lkup(args->flags, DIRREN)
		 /* && h_inode */
		 && !au_dr_lkup_h_ino(args, bindex, h_inode->i_ino)) {
		AuDbg("b%d %pd ignored hi%llu\n", bindex, h_dentry,
		      (unsigned long long)h_inode->i_ino);
		goto out_neg;
	}

	if (au_dbbot(dentry) <= bindex)
		au_set_dbbot(dentry, bindex);
	if (au_dbtop(dentry) < 0 || bindex < au_dbtop(dentry))
		au_set_dbtop(dentry, bindex);
	au_set_h_dptr(dentry, bindex, h_dentry);

	if (!d_is_dir(h_dentry)
	    || !wh_able
	    || (d_really_is_positive(dentry) && !d_is_dir(dentry)))
		goto out; /* success */

	vfsub_inode_lock_shared_nested(h_inode, AuLsc_I_CHILD);
	opq = au_diropq_test(h_dentry);
	inode_unlock_shared(h_inode);
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
int au_lkup_dentry(struct dentry *dentry, aufs_bindex_t btop,
		   unsigned int flags)
{
	int npositive, err;
	aufs_bindex_t bindex, btail, bdiropq;
	unsigned char isdir, dirperm1, dirren;
	struct au_do_lookup_args args = {
		.flags		= flags,
		.name		= &dentry->d_name
	};
	struct dentry *parent;
	struct super_block *sb;

	sb = dentry->d_sb;
	err = au_test_shwh(sb, args.name);
	if (unlikely(err))
		goto out;

	err = au_wh_name_alloc(&args.whname, args.name);
	if (unlikely(err))
		goto out;

	isdir = !!d_is_dir(dentry);
	dirperm1 = !!au_opt_test(au_mntflags(sb), DIRPERM1);
	dirren = !!au_opt_test(au_mntflags(sb), DIRREN);
	if (dirren)
		au_fset_lkup(args.flags, DIRREN);

	npositive = 0;
	parent = dget_parent(dentry);
	btail = au_dbtaildir(parent);
	for (bindex = btop; bindex <= btail; bindex++) {
		struct dentry *h_parent, *h_dentry;
		struct inode *h_inode, *h_dir;
		struct au_branch *br;

		h_dentry = au_h_dptr(dentry, bindex);
		if (h_dentry) {
			if (d_is_positive(h_dentry))
				npositive++;
			break;
		}
		h_parent = au_h_dptr(parent, bindex);
		if (!h_parent || !d_is_dir(h_parent))
			continue;

		if (dirren) {
			/* if the inum matches, then use the prepared name */
			err = au_dr_lkup_name(&args, bindex);
			if (unlikely(err))
				goto out_parent;
		}

		h_dir = d_inode(h_parent);
		vfsub_inode_lock_shared_nested(h_dir, AuLsc_I_PARENT);
		h_dentry = au_do_lookup(h_parent, dentry, bindex, &args);
		inode_unlock_shared(h_dir);
		err = PTR_ERR(h_dentry);
		if (IS_ERR(h_dentry))
			goto out_parent;
		if (h_dentry)
			au_fclr_lkup(args.flags, ALLOW_NEG);
		if (dirperm1)
			au_fset_lkup(args.flags, IGNORE_PERM);

		if (au_dbwh(dentry) == bindex)
			break;
		if (!h_dentry)
			continue;
		if (d_is_negative(h_dentry))
			continue;
		h_inode = d_inode(h_dentry);
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
		br = au_sbr(sb, bindex);
		if (dirren
		    && au_dr_hino_test_add(&br->br_dirren, h_inode->i_ino,
					   /*add_ent*/NULL)) {
			/* prepare next name to lookup */
			err = au_dr_lkup(&args, dentry, bindex);
			if (unlikely(err))
				goto out_parent;
		}
	}

	if (npositive) {
		AuLabel(positive);
		au_update_dbtop(dentry);
	}
	err = npositive;
	if (unlikely(!au_opt_test(au_mntflags(sb), UDBA_NONE)
		     && au_dbtop(dentry) < 0)) {
		err = -EIO;
		AuIOErr("both of real entry and whiteout found, %pd, err %d\n",
			dentry, err);
	}

out_parent:
	dput(parent);
	kfree(args.whname.name);
	if (dirren)
		au_dr_lkup_fin(&args);
out:
	return err;
}

struct dentry *au_sio_lkup_one(struct qstr *name, struct dentry *parent)
{
	struct dentry *dentry;
	int wkq_err;

	if (!au_test_h_perm_sio(d_inode(parent), MAY_EXEC))
		dentry = vfsub_lkup_one(name, parent);
	else {
		struct vfsub_lkup_one_args args = {
			.errp	= &dentry,
			.name	= name,
			.parent	= parent
		};

		wkq_err = au_wkq_wait(vfsub_call_lkup_one, &args);
		if (unlikely(wkq_err))
			dentry = ERR_PTR(wkq_err);
	}

	return dentry;
}

/*
 * lookup @dentry on @bindex which should be negative.
 */
int au_lkup_neg(struct dentry *dentry, aufs_bindex_t bindex, int wh)
{
	int err;
	struct dentry *parent, *h_parent, *h_dentry;
	struct au_branch *br;

	parent = dget_parent(dentry);
	h_parent = au_h_dptr(parent, bindex);
	br = au_sbr(dentry->d_sb, bindex);
	if (wh)
		h_dentry = au_whtmp_lkup(h_parent, br, &dentry->d_name);
	else
		h_dentry = au_sio_lkup_one(&dentry->d_name, h_parent);
	err = PTR_ERR(h_dentry);
	if (IS_ERR(h_dentry))
		goto out;
	if (unlikely(d_is_positive(h_dentry))) {
		err = -EIO;
		AuIOErr("%pd should be negative on b%d.\n", h_dentry, bindex);
		dput(h_dentry);
		goto out;
	}

	err = 0;
	if (bindex < au_dbtop(dentry))
		au_set_dbtop(dentry, bindex);
	if (au_dbbot(dentry) < bindex)
		au_set_dbbot(dentry, bindex);
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
		|| !gid_eq(ia->i_gid, h_inode->i_gid)
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
	h_inode = NULL;
	if (d_is_positive(h_dentry)) {
		h_inode = d_inode(h_dentry);
		au_iattr_save(&ia, h_inode);
	} else if (au_test_nfs(h_sb) || au_test_fuse(h_sb))
		/* nfs d_revalidate may return 0 for negative dentry */
		/* fuse d_revalidate always return 0 for negative dentry */
		goto out;

	/* main purpose is namei.c:cached_lookup() and d_revalidate */
	h_d = vfsub_lkup_one(&h_dentry->d_name, h_parent);
	err = PTR_ERR(h_d);
	if (IS_ERR(h_d))
		goto out;

	err = 0;
	if (unlikely(h_d != h_dentry
		     || d_inode(h_d) != h_inode
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
		err = (d_inode(h_dentry->d_parent) != h_dir);
	} else if (udba != AuOpt_UDBA_NONE)
		err = au_h_verify_dentry(h_dentry, h_parent, br);

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_do_refresh_hdentry(struct dentry *dentry, struct dentry *parent)
{
	int err;
	aufs_bindex_t new_bindex, bindex, bbot, bwh, bdiropq;
	struct au_hdentry tmp, *p, *q;
	struct au_dinfo *dinfo;
	struct super_block *sb;

	DiMustWriteLock(dentry);

	sb = dentry->d_sb;
	dinfo = au_di(dentry);
	bbot = dinfo->di_bbot;
	bwh = dinfo->di_bwh;
	bdiropq = dinfo->di_bdiropq;
	bindex = dinfo->di_btop;
	p = au_hdentry(dinfo, bindex);
	for (; bindex <= bbot; bindex++, p++) {
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
		q = au_hdentry(dinfo, new_bindex);
		tmp = *q;
		*q = *p;
		*p = tmp;
		if (tmp.hd_dentry) {
			bindex--;
			p--;
		}
	}

	dinfo->di_bwh = -1;
	if (bwh >= 0 && bwh <= au_sbbot(sb) && au_sbr_whable(sb, bwh))
		dinfo->di_bwh = bwh;

	dinfo->di_bdiropq = -1;
	if (bdiropq >= 0
	    && bdiropq <= au_sbbot(sb)
	    && au_sbr_whable(sb, bdiropq))
		dinfo->di_bdiropq = bdiropq;

	err = -EIO;
	dinfo->di_btop = -1;
	dinfo->di_bbot = -1;
	bbot = au_dbbot(parent);
	bindex = 0;
	p = au_hdentry(dinfo, bindex);
	for (; bindex <= bbot; bindex++, p++)
		if (p->hd_dentry) {
			dinfo->di_btop = bindex;
			break;
		}

	if (dinfo->di_btop >= 0) {
		bindex = bbot;
		p = au_hdentry(dinfo, bindex);
		for (; bindex >= 0; bindex--, p--)
			if (p->hd_dentry) {
				dinfo->di_bbot = bindex;
				err = 0;
				break;
			}
	}

	return err;
}

static void au_do_hide(struct dentry *dentry)
{
	struct inode *inode;

	if (d_really_is_positive(dentry)) {
		inode = d_inode(dentry);
		if (!d_is_dir(dentry)) {
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

	AuDbgDentry(dentry);
	if (d_is_dir(dentry)) {
		/* shrink_dcache_parent(dentry); */
		err = au_hide_children(dentry);
		if (unlikely(err))
			AuIOErr("%pd, failed hiding children, ignored %d\n",
				dentry, err);
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
	aufs_bindex_t bindex, bbot;
	struct {
		struct dentry *dentry;
		struct inode *inode;
		mode_t mode;
	} orig_h, tmp_h = {
		.dentry = NULL
	};
	struct au_hdentry *hd;
	struct inode *inode, *h_inode;
	struct dentry *h_dentry;

	err = 0;
	AuDebugOn(dinfo->di_btop < 0);
	orig_h.mode = 0;
	orig_h.dentry = au_hdentry(dinfo, dinfo->di_btop)->hd_dentry;
	orig_h.inode = NULL;
	if (d_is_positive(orig_h.dentry)) {
		orig_h.inode = d_inode(orig_h.dentry);
		orig_h.mode = orig_h.inode->i_mode & S_IFMT;
	}
	if (tmp->di_btop >= 0) {
		tmp_h.dentry = au_hdentry(tmp, tmp->di_btop)->hd_dentry;
		if (d_is_positive(tmp_h.dentry)) {
			tmp_h.inode = d_inode(tmp_h.dentry);
			tmp_h.mode = tmp_h.inode->i_mode & S_IFMT;
		}
	}

	inode = NULL;
	if (d_really_is_positive(dentry))
		inode = d_inode(dentry);
	if (!orig_h.inode) {
		AuDbg("nagative originally\n");
		if (inode) {
			au_hide(dentry);
			goto out;
		}
		AuDebugOn(inode);
		AuDebugOn(dinfo->di_btop != dinfo->di_bbot);
		AuDebugOn(dinfo->di_bdiropq != -1);

		if (!tmp_h.inode) {
			AuDbg("negative --> negative\n");
			/* should have only one negative lower */
			if (tmp->di_btop >= 0
			    && tmp->di_btop < dinfo->di_btop) {
				AuDebugOn(tmp->di_btop != tmp->di_bbot);
				AuDebugOn(dinfo->di_btop != dinfo->di_bbot);
				au_set_h_dptr(dentry, dinfo->di_btop, NULL);
				au_di_cp(dinfo, tmp);
				hd = au_hdentry(tmp, tmp->di_btop);
				au_set_h_dptr(dentry, tmp->di_btop,
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
			if (tmp->di_bwh >= 0 && tmp->di_bwh <= dinfo->di_btop)
				dinfo->di_bwh = tmp->di_bwh;
			if (inode)
				err = au_refresh_hinode_self(inode);
			au_dbg_verify_dinode(dentry);
		} else if (orig_h.mode == tmp_h.mode) {
			AuDbg("positive --> positive, same type\n");
			if (!S_ISDIR(orig_h.mode)
			    && dinfo->di_btop > tmp->di_btop) {
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
				if (dinfo->di_btop > tmp->di_btop)
					dinfo->di_btop = tmp->di_btop;
				if (dinfo->di_bbot < tmp->di_bbot)
					dinfo->di_bbot = tmp->di_bbot;
				dinfo->di_bwh = tmp->di_bwh;
				dinfo->di_bdiropq = tmp->di_bdiropq;
				bbot = dinfo->di_bbot;
				bindex = tmp->di_btop;
				hd = au_hdentry(tmp, bindex);
				for (; bindex <= bbot; bindex++, hd++) {
					if (au_h_dptr(dentry, bindex))
						continue;
					h_dentry = hd->hd_dentry;
					if (!h_dentry)
						continue;
					AuDebugOn(d_is_negative(h_dentry));
					h_inode = d_inode(h_dentry);
					AuDebugOn(orig_h.mode
						  != (h_inode->i_mode
						      & S_IFMT));
					au_set_h_dptr(dentry, bindex,
						      dget(h_dentry));
				}
				if (inode)
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

void au_refresh_dop(struct dentry *dentry, int force_reval)
{
	const struct dentry_operations *dop
		= force_reval ? &aufs_dop : dentry->d_sb->s_d_op;
	static const unsigned int mask
		= DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE;

	BUILD_BUG_ON(sizeof(mask) != sizeof(dentry->d_flags));

	if (dentry->d_op == dop)
		return;

	AuDbg("%pd\n", dentry);
	spin_lock(&dentry->d_lock);
	if (dop == &aufs_dop)
		dentry->d_flags |= mask;
	else
		dentry->d_flags &= ~mask;
	dentry->d_op = dop;
	spin_unlock(&dentry->d_lock);
}

int au_refresh_dentry(struct dentry *dentry, struct dentry *parent)
{
	int err, ebrange, nbr;
	unsigned int sigen;
	struct au_dinfo *dinfo, *tmp;
	struct super_block *sb;
	struct inode *inode;

	DiMustWriteLock(dentry);
	AuDebugOn(IS_ROOT(dentry));
	AuDebugOn(d_really_is_negative(parent));

	sb = dentry->d_sb;
	sigen = au_sigen(sb);
	err = au_digen_test(parent, sigen);
	if (unlikely(err))
		goto out;

	nbr = au_sbbot(sb) + 1;
	dinfo = au_di(dentry);
	err = au_di_realloc(dinfo, nbr, /*may_shrink*/0);
	if (unlikely(err))
		goto out;
	ebrange = au_dbrange_test(dentry);
	if (!ebrange)
		ebrange = au_do_refresh_hdentry(dentry, parent);

	if (d_unhashed(dentry) || ebrange /* || dinfo->di_tmpfile */) {
		AuDebugOn(au_dbtop(dentry) < 0 && au_dbbot(dentry) >= 0);
		if (d_really_is_positive(dentry)) {
			inode = d_inode(dentry);
			err = au_refresh_hinode_self(inode);
		}
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
	err = au_lkup_dentry(dentry, /*btop*/0, AuLkup_ALLOW_NEG);
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
	au_di_realloc(dinfo, nbr, /*may_shrink*/1); /* harmless if err */
	au_rw_write_unlock(&tmp->di_rwsem);
	au_di_free(tmp);
	if (unlikely(err))
		goto out;

out_dgen:
	au_update_digen(dentry);
out:
	if (unlikely(err && !(dentry->d_flags & DCACHE_NFSFS_RENAMED))) {
		AuIOErr("failed refreshing %pd, %d\n", dentry, err);
		AuDbgDentry(dentry);
	}
	AuTraceErr(err);
	return err;
}

static int au_do_h_d_reval(struct dentry *h_dentry, unsigned int flags,
			   struct dentry *dentry, aufs_bindex_t bindex)
{
	int err, valid;

	err = 0;
	if (!(h_dentry->d_flags & DCACHE_OP_REVALIDATE))
		goto out;

	AuDbg("b%d\n", bindex);
	/*
	 * gave up supporting LOOKUP_CREATE/OPEN for lower fs,
	 * due to whiteout and branch permission.
	 */
	flags &= ~(/*LOOKUP_PARENT |*/ LOOKUP_OPEN | LOOKUP_CREATE
		   | LOOKUP_FOLLOW | LOOKUP_EXCL);
	/* it may return tri-state */
	valid = h_dentry->d_op->d_revalidate(h_dentry, flags);

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
			  unsigned int flags, int do_udba, int dirren)
{
	int err;
	umode_t mode, h_mode;
	aufs_bindex_t bindex, btail, btop, ibs, ibe;
	unsigned char plus, unhashed, is_root, h_plus, h_nfs, tmpfile;
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
	tmpfile = au_di(dentry)->di_tmpfile;

	/*
	 * Theoretically, REVAL test should be unnecessary in case of
	 * {FS,I}NOTIFY.
	 * But {fs,i}notify doesn't fire some necessary events,
	 *	IN_ATTRIB for atime/nlink/pageio
	 * Let's do REVAL test too.
	 */
	if (do_udba && inode) {
		mode = (inode->i_mode & S_IFMT);
		plus = (inode->i_nlink > 0);
		ibs = au_ibtop(inode);
		ibe = au_ibbot(inode);
	}

	btop = au_dbtop(dentry);
	btail = btop;
	if (inode && S_ISDIR(inode->i_mode))
		btail = au_dbtaildir(dentry);
	for (bindex = btop; bindex <= btail; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;

		AuDbg("b%d, %pd\n", bindex, h_dentry);
		h_nfs = !!au_test_nfs(h_dentry->d_sb);
		spin_lock(&h_dentry->d_lock);
		h_name = &h_dentry->d_name;
		if (unlikely(do_udba
			     && !is_root
			     && ((!h_nfs
				  && (unhashed != !!d_unhashed(h_dentry)
				      || (!tmpfile && !dirren
					  && !au_qstreq(name, h_name))
					  ))
				 || (h_nfs
				     && !(flags & LOOKUP_OPEN)
				     && (h_dentry->d_flags
					 & DCACHE_NFSFS_RENAMED)))
			    )) {
			int h_unhashed;

			h_unhashed = d_unhashed(h_dentry);
			spin_unlock(&h_dentry->d_lock);
			AuDbg("unhash 0x%x 0x%x, %pd %pd\n",
			      unhashed, h_unhashed, dentry, h_dentry);
			goto err;
		}
		spin_unlock(&h_dentry->d_lock);

		err = au_do_h_d_reval(h_dentry, flags, dentry, bindex);
		if (unlikely(err))
			/* do not goto err, to keep the errno */
			break;

		/* todo: plink too? */
		if (!do_udba)
			continue;

		/* UDBA tests */
		if (unlikely(!!inode != d_is_positive(h_dentry)))
			goto err;

		h_inode = NULL;
		if (d_is_positive(h_dentry))
			h_inode = d_inode(h_dentry);
		h_plus = plus;
		h_mode = mode;
		h_cached_inode = h_inode;
		if (h_inode) {
			h_mode = (h_inode->i_mode & S_IFMT);
			h_plus = (h_inode->i_nlink > 0);
		}
		if (inode && ibs <= bindex && bindex <= ibe)
			h_cached_inode = au_h_iptr(inode, bindex);

		if (!h_nfs) {
			if (unlikely(plus != h_plus && !tmpfile))
				goto err;
		} else {
			if (unlikely(!(h_dentry->d_flags & DCACHE_NFSFS_RENAMED)
				     && !is_root
				     && !IS_ROOT(h_dentry)
				     && unhashed != d_unhashed(h_dentry)))
				goto err;
		}
		if (unlikely(mode != h_mode
			     || h_cached_inode != h_inode))
			goto err;
		continue;

err:
		err = -EINVAL;
		break;
	}

	AuTraceErr(err);
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
static int aufs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int valid, err;
	unsigned int sigen;
	unsigned char do_udba, dirren;
	struct super_block *sb;
	struct inode *inode;

	/* todo: support rcu-walk? */
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	valid = 0;
	if (unlikely(!au_di(dentry)))
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
	inode = NULL;
	if (d_really_is_positive(dentry))
		inode = d_inode(dentry);
	if (unlikely(inode && au_is_bad_inode(inode))) {
		err = -EINVAL;
		AuTraceErr(err);
		goto out_dgrade;
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
	if (!(flags & (LOOKUP_OPEN | LOOKUP_EMPTY))
	    && inode
	    && !(inode->i_state && I_LINKABLE)
	    && (IS_DEADDIR(inode) || !inode->i_nlink)) {
		AuTraceErr(err);
		goto out_inval;
	}

	do_udba = !au_opt_test(au_mntflags(sb), UDBA_NONE);
	if (do_udba && inode) {
		aufs_bindex_t btop = au_ibtop(inode);
		struct inode *h_inode;

		if (btop >= 0) {
			h_inode = au_h_iptr(inode, btop);
			if (h_inode && au_test_higen(inode, h_inode)) {
				AuTraceErr(err);
				goto out_inval;
			}
		}
	}

	dirren = !!au_opt_test(au_mntflags(sb), DIRREN);
	err = h_d_revalidate(dentry, inode, flags, do_udba, dirren);
	if (unlikely(!err && do_udba && au_dbtop(dentry) < 0)) {
		err = -EIO;
		AuDbg("both of real entry and whiteout found, %p, err %d\n",
		      dentry, err);
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
		AuDbg("%pd invalid, %d\n", dentry, valid);
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
	.d_revalidate		= aufs_d_revalidate,
	.d_weak_revalidate	= aufs_d_revalidate,
	.d_release		= aufs_d_release
};

/* aufs_dop without d_revalidate */
const struct dentry_operations aufs_dop_noreval = {
	.d_release		= aufs_d_release
};
