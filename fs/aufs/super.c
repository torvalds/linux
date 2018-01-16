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
 * mount and super_block operations
 */

#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/vmalloc.h>
#include "aufs.h"

/*
 * super_operations
 */
static struct inode *aufs_alloc_inode(struct super_block *sb __maybe_unused)
{
	struct au_icntnr *c;

	c = au_cache_alloc_icntnr();
	if (c) {
		au_icntnr_init(c);
		c->vfs_inode.i_version = 1; /* sigen(sb); */
		c->iinfo.ii_hinode = NULL;
		return &c->vfs_inode;
	}
	return NULL;
}

static void aufs_destroy_inode_cb(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);

	au_cache_free_icntnr(container_of(inode, struct au_icntnr, vfs_inode));
}

static void aufs_destroy_inode(struct inode *inode)
{
	if (!au_is_bad_inode(inode))
		au_iinfo_fin(inode);
	call_rcu(&inode->i_rcu, aufs_destroy_inode_cb);
}

struct inode *au_iget_locked(struct super_block *sb, ino_t ino)
{
	struct inode *inode;
	int err;

	inode = iget_locked(sb, ino);
	if (unlikely(!inode)) {
		inode = ERR_PTR(-ENOMEM);
		goto out;
	}
	if (!(inode->i_state & I_NEW))
		goto out;

	err = au_xigen_new(inode);
	if (!err)
		err = au_iinfo_init(inode);
	if (!err)
		inode->i_version++;
	else {
		iget_failed(inode);
		inode = ERR_PTR(err);
	}

out:
	/* never return NULL */
	AuDebugOn(!inode);
	AuTraceErrPtr(inode);
	return inode;
}

/* lock free root dinfo */
static int au_show_brs(struct seq_file *seq, struct super_block *sb)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct path path;
	struct au_hdentry *hdp;
	struct au_branch *br;
	au_br_perm_str_t perm;

	err = 0;
	bbot = au_sbbot(sb);
	bindex = 0;
	hdp = au_hdentry(au_di(sb->s_root), bindex);
	for (; !err && bindex <= bbot; bindex++, hdp++) {
		br = au_sbr(sb, bindex);
		path.mnt = au_br_mnt(br);
		path.dentry = hdp->hd_dentry;
		err = au_seq_path(seq, &path);
		if (!err) {
			au_optstr_br_perm(&perm, br->br_perm);
			seq_printf(seq, "=%s", perm.a);
			if (bindex != bbot)
				seq_putc(seq, ':');
		}
	}
	if (unlikely(err || seq_has_overflowed(seq)))
		err = -E2BIG;

	return err;
}

static void au_gen_fmt(char *fmt, int len __maybe_unused, const char *pat,
		       const char *append)
{
	char *p;

	p = fmt;
	while (*pat != ':')
		*p++ = *pat++;
	*p++ = *pat++;
	strcpy(p, append);
	AuDebugOn(strlen(fmt) >= len);
}

static void au_show_wbr_create(struct seq_file *m, int v,
			       struct au_sbinfo *sbinfo)
{
	const char *pat;
	char fmt[32];
	struct au_wbr_mfs *mfs;

	AuRwMustAnyLock(&sbinfo->si_rwsem);

	seq_puts(m, ",create=");
	pat = au_optstr_wbr_create(v);
	mfs = &sbinfo->si_wbr_mfs;
	switch (v) {
	case AuWbrCreate_TDP:
	case AuWbrCreate_RR:
	case AuWbrCreate_MFS:
	case AuWbrCreate_PMFS:
		seq_puts(m, pat);
		break;
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_TDMFS:
	case AuWbrCreate_PMFSRR:
		au_gen_fmt(fmt, sizeof(fmt), pat, "%llu");
		seq_printf(m, fmt, mfs->mfsrr_watermark);
		break;
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFSV:
		au_gen_fmt(fmt, sizeof(fmt), pat, "%lu");
		seq_printf(m, fmt,
			   jiffies_to_msecs(mfs->mfs_expire)
			   / MSEC_PER_SEC);
		break;
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_TDMFSV:
	case AuWbrCreate_PMFSRRV:
		au_gen_fmt(fmt, sizeof(fmt), pat, "%llu:%lu");
		seq_printf(m, fmt, mfs->mfsrr_watermark,
			   jiffies_to_msecs(mfs->mfs_expire) / MSEC_PER_SEC);
		break;
	default:
		BUG();
	}
}

static int au_show_xino(struct seq_file *seq, struct super_block *sb)
{
#ifdef CONFIG_SYSFS
	return 0;
#else
	int err;
	const int len = sizeof(AUFS_XINO_FNAME) - 1;
	aufs_bindex_t bindex, brid;
	struct qstr *name;
	struct file *f;
	struct dentry *d, *h_root;

	AuRwMustAnyLock(&sbinfo->si_rwsem);

	err = 0;
	f = au_sbi(sb)->si_xib;
	if (!f)
		goto out;

	/* stop printing the default xino path on the first writable branch */
	h_root = NULL;
	brid = au_xino_brid(sb);
	if (brid >= 0) {
		bindex = au_br_index(sb, brid);
		h_root = au_hdentry(au_di(sb->s_root), bindex)->hd_dentry;
	}
	d = f->f_path.dentry;
	name = &d->d_name;
	/* safe ->d_parent because the file is unlinked */
	if (d->d_parent == h_root
	    && name->len == len
	    && !memcmp(name->name, AUFS_XINO_FNAME, len))
		goto out;

	seq_puts(seq, ",xino=");
	err = au_xino_path(seq, f);

out:
	return err;
#endif
}

/* seq_file will re-call me in case of too long string */
static int aufs_show_options(struct seq_file *m, struct dentry *dentry)
{
	int err;
	unsigned int mnt_flags, v;
	struct super_block *sb;
	struct au_sbinfo *sbinfo;

#define AuBool(name, str) do { \
	v = au_opt_test(mnt_flags, name); \
	if (v != au_opt_test(AuOpt_Def, name)) \
		seq_printf(m, ",%s" #str, v ? "" : "no"); \
} while (0)

#define AuStr(name, str) do { \
	v = mnt_flags & AuOptMask_##name; \
	if (v != (AuOpt_Def & AuOptMask_##name)) \
		seq_printf(m, "," #str "=%s", au_optstr_##str(v)); \
} while (0)

#define AuUInt(name, str, val) do { \
	if (val != AUFS_##name##_DEF) \
		seq_printf(m, "," #str "=%u", val); \
} while (0)

	sb = dentry->d_sb;
	if (sb->s_flags & SB_POSIXACL)
		seq_puts(m, ",acl");

	/* lock free root dinfo */
	si_noflush_read_lock(sb);
	sbinfo = au_sbi(sb);
	seq_printf(m, ",si=%lx", sysaufs_si_id(sbinfo));

	mnt_flags = au_mntflags(sb);
	if (au_opt_test(mnt_flags, XINO)) {
		err = au_show_xino(m, sb);
		if (unlikely(err))
			goto out;
	} else
		seq_puts(m, ",noxino");

	AuBool(TRUNC_XINO, trunc_xino);
	AuStr(UDBA, udba);
	AuBool(SHWH, shwh);
	AuBool(PLINK, plink);
	AuBool(DIO, dio);
	AuBool(DIRPERM1, dirperm1);

	v = sbinfo->si_wbr_create;
	if (v != AuWbrCreate_Def)
		au_show_wbr_create(m, v, sbinfo);

	v = sbinfo->si_wbr_copyup;
	if (v != AuWbrCopyup_Def)
		seq_printf(m, ",cpup=%s", au_optstr_wbr_copyup(v));

	v = au_opt_test(mnt_flags, ALWAYS_DIROPQ);
	if (v != au_opt_test(AuOpt_Def, ALWAYS_DIROPQ))
		seq_printf(m, ",diropq=%c", v ? 'a' : 'w');

	AuUInt(DIRWH, dirwh, sbinfo->si_dirwh);

	v = jiffies_to_msecs(sbinfo->si_rdcache) / MSEC_PER_SEC;
	AuUInt(RDCACHE, rdcache, v);

	AuUInt(RDBLK, rdblk, sbinfo->si_rdblk);
	AuUInt(RDHASH, rdhash, sbinfo->si_rdhash);

	au_fhsm_show(m, sbinfo);

	AuBool(DIRREN, dirren);
	AuBool(SUM, sum);
	/* AuBool(SUM_W, wsum); */
	AuBool(WARN_PERM, warn_perm);
	AuBool(VERBOSE, verbose);

out:
	/* be sure to print "br:" last */
	if (!sysaufs_brs) {
		seq_puts(m, ",br:");
		au_show_brs(m, sb);
	}
	si_read_unlock(sb);
	return 0;

#undef AuBool
#undef AuStr
#undef AuUInt
}

/* ---------------------------------------------------------------------- */

/* sum mode which returns the summation for statfs(2) */

static u64 au_add_till_max(u64 a, u64 b)
{
	u64 old;

	old = a;
	a += b;
	if (old <= a)
		return a;
	return ULLONG_MAX;
}

static u64 au_mul_till_max(u64 a, long mul)
{
	u64 old;

	old = a;
	a *= mul;
	if (old <= a)
		return a;
	return ULLONG_MAX;
}

static int au_statfs_sum(struct super_block *sb, struct kstatfs *buf)
{
	int err;
	long bsize, factor;
	u64 blocks, bfree, bavail, files, ffree;
	aufs_bindex_t bbot, bindex, i;
	unsigned char shared;
	struct path h_path;
	struct super_block *h_sb;

	err = 0;
	bsize = LONG_MAX;
	files = 0;
	ffree = 0;
	blocks = 0;
	bfree = 0;
	bavail = 0;
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		h_path.mnt = au_sbr_mnt(sb, bindex);
		h_sb = h_path.mnt->mnt_sb;
		shared = 0;
		for (i = 0; !shared && i < bindex; i++)
			shared = (au_sbr_sb(sb, i) == h_sb);
		if (shared)
			continue;

		/* sb->s_root for NFS is unreliable */
		h_path.dentry = h_path.mnt->mnt_root;
		err = vfs_statfs(&h_path, buf);
		if (unlikely(err))
			goto out;

		if (bsize > buf->f_bsize) {
			/*
			 * we will reduce bsize, so we have to expand blocks
			 * etc. to match them again
			 */
			factor = (bsize / buf->f_bsize);
			blocks = au_mul_till_max(blocks, factor);
			bfree = au_mul_till_max(bfree, factor);
			bavail = au_mul_till_max(bavail, factor);
			bsize = buf->f_bsize;
		}

		factor = (buf->f_bsize / bsize);
		blocks = au_add_till_max(blocks,
				au_mul_till_max(buf->f_blocks, factor));
		bfree = au_add_till_max(bfree,
				au_mul_till_max(buf->f_bfree, factor));
		bavail = au_add_till_max(bavail,
				au_mul_till_max(buf->f_bavail, factor));
		files = au_add_till_max(files, buf->f_files);
		ffree = au_add_till_max(ffree, buf->f_ffree);
	}

	buf->f_bsize = bsize;
	buf->f_blocks = blocks;
	buf->f_bfree = bfree;
	buf->f_bavail = bavail;
	buf->f_files = files;
	buf->f_ffree = ffree;
	buf->f_frsize = 0;

out:
	return err;
}

static int aufs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int err;
	struct path h_path;
	struct super_block *sb;

	/* lock free root dinfo */
	sb = dentry->d_sb;
	si_noflush_read_lock(sb);
	if (!au_opt_test(au_mntflags(sb), SUM)) {
		/* sb->s_root for NFS is unreliable */
		h_path.mnt = au_sbr_mnt(sb, 0);
		h_path.dentry = h_path.mnt->mnt_root;
		err = vfs_statfs(&h_path, buf);
	} else
		err = au_statfs_sum(sb, buf);
	si_read_unlock(sb);

	if (!err) {
		buf->f_type = AUFS_SUPER_MAGIC;
		buf->f_namelen = AUFS_MAX_NAMELEN;
		memset(&buf->f_fsid, 0, sizeof(buf->f_fsid));
	}
	/* buf->f_bsize = buf->f_blocks = buf->f_bfree = buf->f_bavail = -1; */

	return err;
}

/* ---------------------------------------------------------------------- */

static int aufs_sync_fs(struct super_block *sb, int wait)
{
	int err, e;
	aufs_bindex_t bbot, bindex;
	struct au_branch *br;
	struct super_block *h_sb;

	err = 0;
	si_noflush_read_lock(sb);
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		if (!au_br_writable(br->br_perm))
			continue;

		h_sb = au_sbr_sb(sb, bindex);
		e = vfsub_sync_filesystem(h_sb, wait);
		if (unlikely(e && !err))
			err = e;
		/* go on even if an error happens */
	}
	si_read_unlock(sb);

	return err;
}

/* ---------------------------------------------------------------------- */

/* final actions when unmounting a file system */
static void aufs_put_super(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	sbinfo = au_sbi(sb);
	if (!sbinfo)
		return;

	dbgaufs_si_fin(sbinfo);
	kobject_put(&sbinfo->si_kobj);
}

/* ---------------------------------------------------------------------- */

void *au_array_alloc(unsigned long long *hint, au_arraycb_t cb,
		     struct super_block *sb, void *arg)
{
	void *array;
	unsigned long long n, sz;

	array = NULL;
	n = 0;
	if (!*hint)
		goto out;

	if (*hint > ULLONG_MAX / sizeof(array)) {
		array = ERR_PTR(-EMFILE);
		pr_err("hint %llu\n", *hint);
		goto out;
	}

	sz = sizeof(array) * *hint;
	array = kzalloc(sz, GFP_NOFS);
	if (unlikely(!array))
		array = vzalloc(sz);
	if (unlikely(!array)) {
		array = ERR_PTR(-ENOMEM);
		goto out;
	}

	n = cb(sb, array, *hint, arg);
	AuDebugOn(n > *hint);

out:
	*hint = n;
	return array;
}

static unsigned long long au_iarray_cb(struct super_block *sb, void *a,
				       unsigned long long max __maybe_unused,
				       void *arg)
{
	unsigned long long n;
	struct inode **p, *inode;
	struct list_head *head;

	n = 0;
	p = a;
	head = arg;
	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, head, i_sb_list) {
		if (!au_is_bad_inode(inode)
		    && au_ii(inode)->ii_btop >= 0) {
			spin_lock(&inode->i_lock);
			if (atomic_read(&inode->i_count)) {
				au_igrab(inode);
				*p++ = inode;
				n++;
				AuDebugOn(n > max);
			}
			spin_unlock(&inode->i_lock);
		}
	}
	spin_unlock(&sb->s_inode_list_lock);

	return n;
}

struct inode **au_iarray_alloc(struct super_block *sb, unsigned long long *max)
{
	*max = au_ninodes(sb);
	return au_array_alloc(max, au_iarray_cb, sb, &sb->s_inodes);
}

void au_iarray_free(struct inode **a, unsigned long long max)
{
	unsigned long long ull;

	for (ull = 0; ull < max; ull++)
		iput(a[ull]);
	kvfree(a);
}

/* ---------------------------------------------------------------------- */

/*
 * refresh dentry and inode at remount time.
 */
/* todo: consolidate with simple_reval_dpath() and au_reval_for_attr() */
static int au_do_refresh(struct dentry *dentry, unsigned int dir_flags,
		      struct dentry *parent)
{
	int err;

	di_write_lock_child(dentry);
	di_read_lock_parent(parent, AuLock_IR);
	err = au_refresh_dentry(dentry, parent);
	if (!err && dir_flags)
		au_hn_reset(d_inode(dentry), dir_flags);
	di_read_unlock(parent, AuLock_IR);
	di_write_unlock(dentry);

	return err;
}

static int au_do_refresh_d(struct dentry *dentry, unsigned int sigen,
			   struct au_sbinfo *sbinfo,
			   const unsigned int dir_flags, unsigned int do_idop)
{
	int err;
	struct dentry *parent;

	err = 0;
	parent = dget_parent(dentry);
	if (!au_digen_test(parent, sigen) && au_digen_test(dentry, sigen)) {
		if (d_really_is_positive(dentry)) {
			if (!d_is_dir(dentry))
				err = au_do_refresh(dentry, /*dir_flags*/0,
						 parent);
			else {
				err = au_do_refresh(dentry, dir_flags, parent);
				if (unlikely(err))
					au_fset_si(sbinfo, FAILED_REFRESH_DIR);
			}
		} else
			err = au_do_refresh(dentry, /*dir_flags*/0, parent);
		AuDbgDentry(dentry);
	}
	dput(parent);

	if (!err) {
		if (do_idop)
			au_refresh_dop(dentry, /*force_reval*/0);
	} else
		au_refresh_dop(dentry, /*force_reval*/1);

	AuTraceErr(err);
	return err;
}

static int au_refresh_d(struct super_block *sb, unsigned int do_idop)
{
	int err, i, j, ndentry, e;
	unsigned int sigen;
	struct au_dcsub_pages dpages;
	struct au_dpage *dpage;
	struct dentry **dentries, *d;
	struct au_sbinfo *sbinfo;
	struct dentry *root = sb->s_root;
	const unsigned int dir_flags = au_hi_flags(d_inode(root), /*isdir*/1);

	if (do_idop)
		au_refresh_dop(root, /*force_reval*/0);

	err = au_dpages_init(&dpages, GFP_NOFS);
	if (unlikely(err))
		goto out;
	err = au_dcsub_pages(&dpages, root, NULL, NULL);
	if (unlikely(err))
		goto out_dpages;

	sigen = au_sigen(sb);
	sbinfo = au_sbi(sb);
	for (i = 0; i < dpages.ndpage; i++) {
		dpage = dpages.dpages + i;
		dentries = dpage->dentries;
		ndentry = dpage->ndentry;
		for (j = 0; j < ndentry; j++) {
			d = dentries[j];
			e = au_do_refresh_d(d, sigen, sbinfo, dir_flags,
					    do_idop);
			if (unlikely(e && !err))
				err = e;
			/* go on even err */
		}
	}

out_dpages:
	au_dpages_free(&dpages);
out:
	return err;
}

static int au_refresh_i(struct super_block *sb, unsigned int do_idop)
{
	int err, e;
	unsigned int sigen;
	unsigned long long max, ull;
	struct inode *inode, **array;

	array = au_iarray_alloc(sb, &max);
	err = PTR_ERR(array);
	if (IS_ERR(array))
		goto out;

	err = 0;
	sigen = au_sigen(sb);
	for (ull = 0; ull < max; ull++) {
		inode = array[ull];
		if (unlikely(!inode))
			break;

		e = 0;
		ii_write_lock_child(inode);
		if (au_iigen(inode, NULL) != sigen) {
			e = au_refresh_hinode_self(inode);
			if (unlikely(e)) {
				au_refresh_iop(inode, /*force_getattr*/1);
				pr_err("error %d, i%lu\n", e, inode->i_ino);
				if (!err)
					err = e;
				/* go on even if err */
			}
		}
		if (!e && do_idop)
			au_refresh_iop(inode, /*force_getattr*/0);
		ii_write_unlock(inode);
	}

	au_iarray_free(array, max);

out:
	return err;
}

static void au_remount_refresh(struct super_block *sb, unsigned int do_idop)
{
	int err, e;
	unsigned int udba;
	aufs_bindex_t bindex, bbot;
	struct dentry *root;
	struct inode *inode;
	struct au_branch *br;
	struct au_sbinfo *sbi;

	au_sigen_inc(sb);
	sbi = au_sbi(sb);
	au_fclr_si(sbi, FAILED_REFRESH_DIR);

	root = sb->s_root;
	DiMustNoWaiters(root);
	inode = d_inode(root);
	IiMustNoWaiters(inode);

	udba = au_opt_udba(sb);
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		err = au_hnotify_reset_br(udba, br, br->br_perm);
		if (unlikely(err))
			AuIOErr("hnotify failed on br %d, %d, ignored\n",
				bindex, err);
		/* go on even if err */
	}
	au_hn_reset(inode, au_hi_flags(inode, /*isdir*/1));

	if (do_idop) {
		if (au_ftest_si(sbi, NO_DREVAL)) {
			AuDebugOn(sb->s_d_op == &aufs_dop_noreval);
			sb->s_d_op = &aufs_dop_noreval;
			AuDebugOn(sbi->si_iop_array == aufs_iop_nogetattr);
			sbi->si_iop_array = aufs_iop_nogetattr;
		} else {
			AuDebugOn(sb->s_d_op == &aufs_dop);
			sb->s_d_op = &aufs_dop;
			AuDebugOn(sbi->si_iop_array == aufs_iop);
			sbi->si_iop_array = aufs_iop;
		}
		pr_info("reset to %pf and %pf\n",
			sb->s_d_op, sbi->si_iop_array);
	}

	di_write_unlock(root);
	err = au_refresh_d(sb, do_idop);
	e = au_refresh_i(sb, do_idop);
	if (unlikely(e && !err))
		err = e;
	/* aufs_write_lock() calls ..._child() */
	di_write_lock_child(root);

	au_cpup_attr_all(inode, /*force*/1);

	if (unlikely(err))
		AuIOErr("refresh failed, ignored, %d\n", err);
}

/* stop extra interpretation of errno in mount(8), and strange error messages */
static int cvt_err(int err)
{
	AuTraceErr(err);

	switch (err) {
	case -ENOENT:
	case -ENOTDIR:
	case -EEXIST:
	case -EIO:
		err = -EINVAL;
	}
	return err;
}

static int aufs_remount_fs(struct super_block *sb, int *flags, char *data)
{
	int err, do_dx;
	unsigned int mntflags;
	struct au_opts opts = {
		.opt = NULL
	};
	struct dentry *root;
	struct inode *inode;
	struct au_sbinfo *sbinfo;

	err = 0;
	root = sb->s_root;
	if (!data || !*data) {
		err = si_write_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
		if (!err) {
			di_write_lock_child(root);
			err = au_opts_verify(sb, *flags, /*pending*/0);
			aufs_write_unlock(root);
		}
		goto out;
	}

	err = -ENOMEM;
	opts.opt = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!opts.opt))
		goto out;
	opts.max_opt = PAGE_SIZE / sizeof(*opts.opt);
	opts.flags = AuOpts_REMOUNT;
	opts.sb_flags = *flags;

	/* parse it before aufs lock */
	err = au_opts_parse(sb, data, &opts);
	if (unlikely(err))
		goto out_opts;

	sbinfo = au_sbi(sb);
	inode = d_inode(root);
	inode_lock(inode);
	err = si_write_lock(sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out_mtx;
	di_write_lock_child(root);

	/* au_opts_remount() may return an error */
	err = au_opts_remount(sb, &opts);
	au_opts_free(&opts);

	if (au_ftest_opts(opts.flags, REFRESH))
		au_remount_refresh(sb, au_ftest_opts(opts.flags, REFRESH_IDOP));

	if (au_ftest_opts(opts.flags, REFRESH_DYAOP)) {
		mntflags = au_mntflags(sb);
		do_dx = !!au_opt_test(mntflags, DIO);
		au_dy_arefresh(do_dx);
	}

	au_fhsm_wrote_all(sb, /*force*/1); /* ?? */
	aufs_write_unlock(root);

out_mtx:
	inode_unlock(inode);
out_opts:
	free_page((unsigned long)opts.opt);
out:
	err = cvt_err(err);
	AuTraceErr(err);
	return err;
}

static const struct super_operations aufs_sop = {
	.alloc_inode	= aufs_alloc_inode,
	.destroy_inode	= aufs_destroy_inode,
	/* always deleting, no clearing */
	.drop_inode	= generic_delete_inode,
	.show_options	= aufs_show_options,
	.statfs		= aufs_statfs,
	.put_super	= aufs_put_super,
	.sync_fs	= aufs_sync_fs,
	.remount_fs	= aufs_remount_fs,
#ifdef CONFIG_AUFS_BDEV_LOOP
	.real_loop	= aufs_real_loop
#endif
};

/* ---------------------------------------------------------------------- */

static int alloc_root(struct super_block *sb)
{
	int err;
	struct inode *inode;
	struct dentry *root;

	err = -ENOMEM;
	inode = au_iget_locked(sb, AUFS_ROOT_INO);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out;

	inode->i_op = aufs_iop + AuIop_DIR; /* with getattr by default */
	inode->i_fop = &aufs_dir_fop;
	inode->i_mode = S_IFDIR;
	set_nlink(inode, 2);
	unlock_new_inode(inode);

	root = d_make_root(inode);
	if (unlikely(!root))
		goto out;
	err = PTR_ERR(root);
	if (IS_ERR(root))
		goto out;

	err = au_di_init(root);
	if (!err) {
		sb->s_root = root;
		return 0; /* success */
	}
	dput(root);

out:
	return err;
}

static int aufs_fill_super(struct super_block *sb, void *raw_data,
			   int silent __maybe_unused)
{
	int err;
	struct au_opts opts = {
		.opt = NULL
	};
	struct au_sbinfo *sbinfo;
	struct dentry *root;
	struct inode *inode;
	char *arg = raw_data;

	if (unlikely(!arg || !*arg)) {
		err = -EINVAL;
		pr_err("no arg\n");
		goto out;
	}

	err = -ENOMEM;
	opts.opt = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!opts.opt))
		goto out;
	opts.max_opt = PAGE_SIZE / sizeof(*opts.opt);
	opts.sb_flags = sb->s_flags;

	err = au_si_alloc(sb);
	if (unlikely(err))
		goto out_opts;
	sbinfo = au_sbi(sb);

	/* all timestamps always follow the ones on the branch */
	sb->s_flags |= SB_NOATIME | SB_NODIRATIME;
	sb->s_op = &aufs_sop;
	sb->s_d_op = &aufs_dop;
	sb->s_magic = AUFS_SUPER_MAGIC;
	sb->s_maxbytes = 0;
	sb->s_stack_depth = 1;
	au_export_init(sb);
	au_xattr_init(sb);

	err = alloc_root(sb);
	if (unlikely(err)) {
		si_write_unlock(sb);
		goto out_info;
	}
	root = sb->s_root;
	inode = d_inode(root);

	/*
	 * actually we can parse options regardless aufs lock here.
	 * but at remount time, parsing must be done before aufs lock.
	 * so we follow the same rule.
	 */
	ii_write_lock_parent(inode);
	aufs_write_unlock(root);
	err = au_opts_parse(sb, arg, &opts);
	if (unlikely(err))
		goto out_root;

	/* lock vfs_inode first, then aufs. */
	inode_lock(inode);
	aufs_write_lock(root);
	err = au_opts_mount(sb, &opts);
	au_opts_free(&opts);
	if (!err && au_ftest_si(sbinfo, NO_DREVAL)) {
		sb->s_d_op = &aufs_dop_noreval;
		pr_info("%pf\n", sb->s_d_op);
		au_refresh_dop(root, /*force_reval*/0);
		sbinfo->si_iop_array = aufs_iop_nogetattr;
		au_refresh_iop(inode, /*force_getattr*/0);
	}
	aufs_write_unlock(root);
	inode_unlock(inode);
	if (!err)
		goto out_opts; /* success */

out_root:
	dput(root);
	sb->s_root = NULL;
out_info:
	dbgaufs_si_fin(sbinfo);
	kobject_put(&sbinfo->si_kobj);
	sb->s_fs_info = NULL;
out_opts:
	free_page((unsigned long)opts.opt);
out:
	AuTraceErr(err);
	err = cvt_err(err);
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static struct dentry *aufs_mount(struct file_system_type *fs_type, int flags,
				 const char *dev_name __maybe_unused,
				 void *raw_data)
{
	struct dentry *root;
	struct super_block *sb;

	/* all timestamps always follow the ones on the branch */
	/* mnt->mnt_flags |= MNT_NOATIME | MNT_NODIRATIME; */
	root = mount_nodev(fs_type, flags, raw_data, aufs_fill_super);
	if (IS_ERR(root))
		goto out;

	sb = root->d_sb;
	si_write_lock(sb, !AuLock_FLUSH);
	sysaufs_brs_add(sb, 0);
	si_write_unlock(sb);
	au_sbilist_add(sb);

out:
	return root;
}

static void aufs_kill_sb(struct super_block *sb)
{
	struct au_sbinfo *sbinfo;

	sbinfo = au_sbi(sb);
	if (sbinfo) {
		au_sbilist_del(sb);
		aufs_write_lock(sb->s_root);
		au_fhsm_fin(sb);
		if (sbinfo->si_wbr_create_ops->fin)
			sbinfo->si_wbr_create_ops->fin(sb);
		if (au_opt_test(sbinfo->si_mntflags, UDBA_HNOTIFY)) {
			au_opt_set_udba(sbinfo->si_mntflags, UDBA_NONE);
			au_remount_refresh(sb, /*do_idop*/0);
		}
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_put(sb, /*verbose*/1);
		au_xino_clr(sb);
		au_dr_opt_flush(sb);
		sbinfo->si_sb = NULL;
		aufs_write_unlock(sb->s_root);
		au_nwt_flush(&sbinfo->si_nowait);
	}
	kill_anon_super(sb);
}

struct file_system_type aufs_fs_type = {
	.name		= AUFS_FSTYPE,
	/* a race between rename and others */
	.fs_flags	= FS_RENAME_DOES_D_MOVE,
	.mount		= aufs_mount,
	.kill_sb	= aufs_kill_sb,
	/* no need to __module_get() and module_put(). */
	.owner		= THIS_MODULE,
};
