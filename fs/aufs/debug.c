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
 * debug print functions
 */

#include "aufs.h"

/* Returns 0, or -errno.  arg is in kp->arg. */
static int param_atomic_t_set(const char *val, const struct kernel_param *kp)
{
	int err, n;

	err = kstrtoint(val, 0, &n);
	if (!err) {
		if (n > 0)
			au_debug_on();
		else
			au_debug_off();
	}
	return err;
}

/* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
static int param_atomic_t_get(char *buffer, const struct kernel_param *kp)
{
	atomic_t *a;

	a = kp->arg;
	return sprintf(buffer, "%d", atomic_read(a));
}

static struct kernel_param_ops param_ops_atomic_t = {
	.set = param_atomic_t_set,
	.get = param_atomic_t_get
	/* void (*free)(void *arg) */
};

atomic_t aufs_debug = ATOMIC_INIT(0);
MODULE_PARM_DESC(debug, "debug print");
module_param_named(debug, aufs_debug, atomic_t, S_IRUGO | S_IWUSR | S_IWGRP);

DEFINE_MUTEX(au_dbg_mtx);	/* just to serialize the dbg msgs */
char *au_plevel = KERN_DEBUG;
#define dpri(fmt, ...) do {					\
	if ((au_plevel						\
	     && strcmp(au_plevel, KERN_DEBUG))			\
	    || au_debug_test())					\
		printk("%s" fmt, au_plevel, ##__VA_ARGS__);	\
} while (0)

/* ---------------------------------------------------------------------- */

void au_dpri_whlist(struct au_nhash *whlist)
{
	unsigned long ul, n;
	struct hlist_head *head;
	struct au_vdir_wh *pos;

	n = whlist->nh_num;
	head = whlist->nh_head;
	for (ul = 0; ul < n; ul++) {
		hlist_for_each_entry(pos, head, wh_hash)
			dpri("b%d, %.*s, %d\n",
			     pos->wh_bindex,
			     pos->wh_str.len, pos->wh_str.name,
			     pos->wh_str.len);
		head++;
	}
}

void au_dpri_vdir(struct au_vdir *vdir)
{
	unsigned long ul;
	union au_vdir_deblk_p p;
	unsigned char *o;

	if (!vdir || IS_ERR(vdir)) {
		dpri("err %ld\n", PTR_ERR(vdir));
		return;
	}

	dpri("deblk %u, nblk %lu, deblk %p, last{%lu, %p}, ver %lu\n",
	     vdir->vd_deblk_sz, vdir->vd_nblk, vdir->vd_deblk,
	     vdir->vd_last.ul, vdir->vd_last.p.deblk, vdir->vd_version);
	for (ul = 0; ul < vdir->vd_nblk; ul++) {
		p.deblk = vdir->vd_deblk[ul];
		o = p.deblk;
		dpri("[%lu]: %p\n", ul, o);
	}
}

static int do_pri_inode(aufs_bindex_t bindex, struct inode *inode, int hn,
			struct dentry *wh)
{
	char *n = NULL;
	int l = 0;

	if (!inode || IS_ERR(inode)) {
		dpri("i%d: err %ld\n", bindex, PTR_ERR(inode));
		return -1;
	}

	/* the type of i_blocks depends upon CONFIG_LBDAF */
	BUILD_BUG_ON(sizeof(inode->i_blocks) != sizeof(unsigned long)
		     && sizeof(inode->i_blocks) != sizeof(u64));
	if (wh) {
		n = (void *)wh->d_name.name;
		l = wh->d_name.len;
	}

	dpri("i%d: %p, i%lu, %s, cnt %d, nl %u, 0%o, sz %llu, blk %llu,"
	     " hn %d, ct %lld, np %lu, st 0x%lx, f 0x%x, v %llu, g %x%s%.*s\n",
	     bindex, inode,
	     inode->i_ino, inode->i_sb ? au_sbtype(inode->i_sb) : "??",
	     atomic_read(&inode->i_count), inode->i_nlink, inode->i_mode,
	     i_size_read(inode), (unsigned long long)inode->i_blocks,
	     hn, (long long)timespec_to_ns(&inode->i_ctime) & 0x0ffff,
	     inode->i_mapping ? inode->i_mapping->nrpages : 0,
	     inode->i_state, inode->i_flags, inode->i_version,
	     inode->i_generation,
	     l ? ", wh " : "", l, n);
	return 0;
}

void au_dpri_inode(struct inode *inode)
{
	struct au_iinfo *iinfo;
	struct au_hinode *hi;
	aufs_bindex_t bindex;
	int err, hn;

	err = do_pri_inode(-1, inode, -1, NULL);
	if (err || !au_test_aufs(inode->i_sb) || au_is_bad_inode(inode))
		return;

	iinfo = au_ii(inode);
	dpri("i-1: btop %d, bbot %d, gen %d\n",
	     iinfo->ii_btop, iinfo->ii_bbot, au_iigen(inode, NULL));
	if (iinfo->ii_btop < 0)
		return;
	hn = 0;
	for (bindex = iinfo->ii_btop; bindex <= iinfo->ii_bbot; bindex++) {
		hi = au_hinode(iinfo, bindex);
		hn = !!au_hn(hi);
		do_pri_inode(bindex, hi->hi_inode, hn, hi->hi_whdentry);
	}
}

void au_dpri_dalias(struct inode *inode)
{
	struct dentry *d;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(d, &inode->i_dentry, d_u.d_alias)
		au_dpri_dentry(d);
	spin_unlock(&inode->i_lock);
}

static int do_pri_dentry(aufs_bindex_t bindex, struct dentry *dentry)
{
	struct dentry *wh = NULL;
	int hn;
	struct inode *inode;
	struct au_iinfo *iinfo;
	struct au_hinode *hi;

	if (!dentry || IS_ERR(dentry)) {
		dpri("d%d: err %ld\n", bindex, PTR_ERR(dentry));
		return -1;
	}
	/* do not call dget_parent() here */
	/* note: access d_xxx without d_lock */
	dpri("d%d: %p, %pd2?, %s, cnt %d, flags 0x%x, %shashed\n",
	     bindex, dentry, dentry,
	     dentry->d_sb ? au_sbtype(dentry->d_sb) : "??",
	     au_dcount(dentry), dentry->d_flags,
	     d_unhashed(dentry) ? "un" : "");
	hn = -1;
	inode = NULL;
	if (d_is_positive(dentry))
		inode = d_inode(dentry);
	if (inode
	    && au_test_aufs(dentry->d_sb)
	    && bindex >= 0
	    && !au_is_bad_inode(inode)) {
		iinfo = au_ii(inode);
		hi = au_hinode(iinfo, bindex);
		hn = !!au_hn(hi);
		wh = hi->hi_whdentry;
	}
	do_pri_inode(bindex, inode, hn, wh);
	return 0;
}

void au_dpri_dentry(struct dentry *dentry)
{
	struct au_dinfo *dinfo;
	aufs_bindex_t bindex;
	int err;

	err = do_pri_dentry(-1, dentry);
	if (err || !au_test_aufs(dentry->d_sb))
		return;

	dinfo = au_di(dentry);
	if (!dinfo)
		return;
	dpri("d-1: btop %d, bbot %d, bwh %d, bdiropq %d, gen %d, tmp %d\n",
	     dinfo->di_btop, dinfo->di_bbot,
	     dinfo->di_bwh, dinfo->di_bdiropq, au_digen(dentry),
	     dinfo->di_tmpfile);
	if (dinfo->di_btop < 0)
		return;
	for (bindex = dinfo->di_btop; bindex <= dinfo->di_bbot; bindex++)
		do_pri_dentry(bindex, au_hdentry(dinfo, bindex)->hd_dentry);
}

static int do_pri_file(aufs_bindex_t bindex, struct file *file)
{
	char a[32];

	if (!file || IS_ERR(file)) {
		dpri("f%d: err %ld\n", bindex, PTR_ERR(file));
		return -1;
	}
	a[0] = 0;
	if (bindex < 0
	    && !IS_ERR_OR_NULL(file->f_path.dentry)
	    && au_test_aufs(file->f_path.dentry->d_sb)
	    && au_fi(file))
		snprintf(a, sizeof(a), ", gen %d, mmapped %d",
			 au_figen(file), atomic_read(&au_fi(file)->fi_mmapped));
	dpri("f%d: mode 0x%x, flags 0%o, cnt %ld, v %llu, pos %llu%s\n",
	     bindex, file->f_mode, file->f_flags, (long)file_count(file),
	     file->f_version, file->f_pos, a);
	if (!IS_ERR_OR_NULL(file->f_path.dentry))
		do_pri_dentry(bindex, file->f_path.dentry);
	return 0;
}

void au_dpri_file(struct file *file)
{
	struct au_finfo *finfo;
	struct au_fidir *fidir;
	struct au_hfile *hfile;
	aufs_bindex_t bindex;
	int err;

	err = do_pri_file(-1, file);
	if (err
	    || IS_ERR_OR_NULL(file->f_path.dentry)
	    || !au_test_aufs(file->f_path.dentry->d_sb))
		return;

	finfo = au_fi(file);
	if (!finfo)
		return;
	if (finfo->fi_btop < 0)
		return;
	fidir = finfo->fi_hdir;
	if (!fidir)
		do_pri_file(finfo->fi_btop, finfo->fi_htop.hf_file);
	else
		for (bindex = finfo->fi_btop;
		     bindex >= 0 && bindex <= fidir->fd_bbot;
		     bindex++) {
			hfile = fidir->fd_hfile + bindex;
			do_pri_file(bindex, hfile ? hfile->hf_file : NULL);
		}
}

static int do_pri_br(aufs_bindex_t bindex, struct au_branch *br)
{
	struct vfsmount *mnt;
	struct super_block *sb;

	if (!br || IS_ERR(br))
		goto out;
	mnt = au_br_mnt(br);
	if (!mnt || IS_ERR(mnt))
		goto out;
	sb = mnt->mnt_sb;
	if (!sb || IS_ERR(sb))
		goto out;

	dpri("s%d: {perm 0x%x, id %d, cnt %lld, wbr %p}, "
	     "%s, dev 0x%02x%02x, flags 0x%lx, cnt %d, active %d, "
	     "xino %d\n",
	     bindex, br->br_perm, br->br_id, au_br_count(br),
	     br->br_wbr, au_sbtype(sb), MAJOR(sb->s_dev), MINOR(sb->s_dev),
	     sb->s_flags, sb->s_count,
	     atomic_read(&sb->s_active), !!br->br_xino.xi_file);
	return 0;

out:
	dpri("s%d: err %ld\n", bindex, PTR_ERR(br));
	return -1;
}

void au_dpri_sb(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;
	aufs_bindex_t bindex;
	int err;
	/* to reuduce stack size */
	struct {
		struct vfsmount mnt;
		struct au_branch fake;
	} *a;

	/* this function can be called from magic sysrq */
	a = kzalloc(sizeof(*a), GFP_ATOMIC);
	if (unlikely(!a)) {
		dpri("no memory\n");
		return;
	}

	a->mnt.mnt_sb = sb;
	a->fake.br_path.mnt = &a->mnt;
	au_br_count_init(&a->fake);
	err = do_pri_br(-1, &a->fake);
	au_br_count_fin(&a->fake);
	kfree(a);
	dpri("dev 0x%x\n", sb->s_dev);
	if (err || !au_test_aufs(sb))
		return;

	sbinfo = au_sbi(sb);
	if (!sbinfo)
		return;
	dpri("nw %d, gen %u, kobj %d\n",
	     atomic_read(&sbinfo->si_nowait.nw_len), sbinfo->si_generation,
	     kref_read(&sbinfo->si_kobj.kref));
	for (bindex = 0; bindex <= sbinfo->si_bbot; bindex++)
		do_pri_br(bindex, sbinfo->si_branch[0 + bindex]);
}

/* ---------------------------------------------------------------------- */

void __au_dbg_verify_dinode(struct dentry *dentry, const char *func, int line)
{
	struct inode *h_inode, *inode = d_inode(dentry);
	struct dentry *h_dentry;
	aufs_bindex_t bindex, bbot, bi;

	if (!inode /* || au_di(dentry)->di_lsc == AuLsc_DI_TMP */)
		return;

	bbot = au_dbbot(dentry);
	bi = au_ibbot(inode);
	if (bi < bbot)
		bbot = bi;
	bindex = au_dbtop(dentry);
	bi = au_ibtop(inode);
	if (bi > bindex)
		bindex = bi;

	for (; bindex <= bbot; bindex++) {
		h_dentry = au_h_dptr(dentry, bindex);
		if (!h_dentry)
			continue;
		h_inode = au_h_iptr(inode, bindex);
		if (unlikely(h_inode != d_inode(h_dentry))) {
			au_debug_on();
			AuDbg("b%d, %s:%d\n", bindex, func, line);
			AuDbgDentry(dentry);
			AuDbgInode(inode);
			au_debug_off();
			BUG();
		}
	}
}

void au_dbg_verify_gen(struct dentry *parent, unsigned int sigen)
{
	int err, i, j;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry **dentries;

	err = au_dpages_init(&dpages, GFP_NOFS);
	AuDebugOn(err);
	err = au_dcsub_pages_rev_aufs(&dpages, parent, /*do_include*/1);
	AuDebugOn(err);
	for (i = dpages.ndpage - 1; !err && i >= 0; i--) {
		dpage = dpages.dpages + i;
		dentries = dpage->dentries;
		for (j = dpage->ndentry - 1; !err && j >= 0; j--)
			AuDebugOn(au_digen_test(dentries[j], sigen));
	}
	au_dpages_free(&dpages);
}

void au_dbg_verify_kthread(void)
{
	if (au_wkq_test()) {
		au_dbg_blocked();
		/*
		 * It may be recursive, but udba=notify between two aufs mounts,
		 * where a single ro branch is shared, is not a problem.
		 */
		/* WARN_ON(1); */
	}
}

/* ---------------------------------------------------------------------- */

int __init au_debug_init(void)
{
	aufs_bindex_t bindex;
	struct au_vdir_destr destr;

	bindex = -1;
	AuDebugOn(bindex >= 0);

	destr.len = -1;
	AuDebugOn(destr.len < NAME_MAX);

#ifdef CONFIG_4KSTACKS
	pr_warn("CONFIG_4KSTACKS is defined.\n");
#endif

	return 0;
}
