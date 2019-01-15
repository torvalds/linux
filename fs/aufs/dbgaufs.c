// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2018 Junjiro R. Okajima
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
 * debugfs interface
 */

#include <linux/debugfs.h>
#include "aufs.h"

#ifndef CONFIG_SYSFS
#error DEBUG_FS depends upon SYSFS
#endif

static struct dentry *dbgaufs;
static const mode_t dbgaufs_mode = 0444;

/* 20 is max digits length of ulong 64 */
struct dbgaufs_arg {
	int n;
	char a[20 * 4];
};

/*
 * common function for all XINO files
 */
static int dbgaufs_xi_release(struct inode *inode __maybe_unused,
			      struct file *file)
{
	void *p;

	p = file->private_data;
	if (p) {
		/* this is struct dbgaufs_arg */
		AuDebugOn(!au_kfree_sz_test(p));
		au_kfree_do_rcu(p);
	}
	return 0;
}

static int dbgaufs_xi_open(struct file *xf, struct file *file, int do_fcnt,
			   int cnt)
{
	int err;
	struct kstat st;
	struct dbgaufs_arg *p;

	err = -ENOMEM;
	p = kmalloc(sizeof(*p), GFP_NOFS);
	if (unlikely(!p))
		goto out;

	err = 0;
	p->n = 0;
	file->private_data = p;
	if (!xf)
		goto out;

	err = vfsub_getattr(&xf->f_path, &st);
	if (!err) {
		if (do_fcnt)
			p->n = snprintf
				(p->a, sizeof(p->a), "%d, %llux%u %lld\n",
				 cnt, st.blocks, st.blksize,
				 (long long)st.size);
		else
			p->n = snprintf(p->a, sizeof(p->a), "%llux%u %lld\n",
					st.blocks, st.blksize,
					(long long)st.size);
		AuDebugOn(p->n >= sizeof(p->a));
	} else {
		p->n = snprintf(p->a, sizeof(p->a), "err %d\n", err);
		err = 0;
	}

out:
	return err;
}

static ssize_t dbgaufs_xi_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct dbgaufs_arg *p;

	p = file->private_data;
	return simple_read_from_buffer(buf, count, ppos, p->a, p->n);
}

/* ---------------------------------------------------------------------- */

struct dbgaufs_plink_arg {
	int n;
	char a[];
};

static int dbgaufs_plink_release(struct inode *inode __maybe_unused,
				 struct file *file)
{
	free_page((unsigned long)file->private_data);
	return 0;
}

static int dbgaufs_plink_open(struct inode *inode, struct file *file)
{
	int err, i, limit;
	unsigned long n, sum;
	struct dbgaufs_plink_arg *p;
	struct au_sbinfo *sbinfo;
	struct super_block *sb;
	struct hlist_bl_head *hbl;

	err = -ENOMEM;
	p = (void *)get_zeroed_page(GFP_NOFS);
	if (unlikely(!p))
		goto out;

	err = -EFBIG;
	sbinfo = inode->i_private;
	sb = sbinfo->si_sb;
	si_noflush_read_lock(sb);
	if (au_opt_test(au_mntflags(sb), PLINK)) {
		limit = PAGE_SIZE - sizeof(p->n);

		/* the number of buckets */
		n = snprintf(p->a + p->n, limit, "%d\n", AuPlink_NHASH);
		p->n += n;
		limit -= n;

		sum = 0;
		for (i = 0, hbl = sbinfo->si_plink; i < AuPlink_NHASH;
		     i++, hbl++) {
			n = au_hbl_count(hbl);
			sum += n;

			n = snprintf(p->a + p->n, limit, "%lu ", n);
			p->n += n;
			limit -= n;
			if (unlikely(limit <= 0))
				goto out_free;
		}
		p->a[p->n - 1] = '\n';

		/* the sum of plinks */
		n = snprintf(p->a + p->n, limit, "%lu\n", sum);
		p->n += n;
		limit -= n;
		if (unlikely(limit <= 0))
			goto out_free;
	} else {
#define str "1\n0\n0\n"
		p->n = sizeof(str) - 1;
		strcpy(p->a, str);
#undef str
	}
	si_read_unlock(sb);

	err = 0;
	file->private_data = p;
	goto out; /* success */

out_free:
	free_page((unsigned long)p);
out:
	return err;
}

static ssize_t dbgaufs_plink_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct dbgaufs_plink_arg *p;

	p = file->private_data;
	return simple_read_from_buffer(buf, count, ppos, p->a, p->n);
}

static const struct file_operations dbgaufs_plink_fop = {
	.owner		= THIS_MODULE,
	.open		= dbgaufs_plink_open,
	.release	= dbgaufs_plink_release,
	.read		= dbgaufs_plink_read
};

/* ---------------------------------------------------------------------- */

static int dbgaufs_xib_open(struct inode *inode, struct file *file)
{
	int err;
	struct au_sbinfo *sbinfo;
	struct super_block *sb;

	sbinfo = inode->i_private;
	sb = sbinfo->si_sb;
	si_noflush_read_lock(sb);
	err = dbgaufs_xi_open(sbinfo->si_xib, file, /*do_fcnt*/0, /*cnt*/0);
	si_read_unlock(sb);
	return err;
}

static const struct file_operations dbgaufs_xib_fop = {
	.owner		= THIS_MODULE,
	.open		= dbgaufs_xib_open,
	.release	= dbgaufs_xi_release,
	.read		= dbgaufs_xi_read
};

/* ---------------------------------------------------------------------- */

#define DbgaufsXi_PREFIX "xi"

static int dbgaufs_xino_open(struct inode *inode, struct file *file)
{
	int err, idx;
	long l;
	aufs_bindex_t bindex;
	char *p, a[sizeof(DbgaufsXi_PREFIX) + 8];
	struct au_sbinfo *sbinfo;
	struct super_block *sb;
	struct au_xino *xi;
	struct file *xf;
	struct qstr *name;
	struct au_branch *br;

	err = -ENOENT;
	name = &file->f_path.dentry->d_name;
	if (unlikely(name->len < sizeof(DbgaufsXi_PREFIX)
		     || memcmp(name->name, DbgaufsXi_PREFIX,
			       sizeof(DbgaufsXi_PREFIX) - 1)))
		goto out;

	AuDebugOn(name->len >= sizeof(a));
	memcpy(a, name->name, name->len);
	a[name->len] = '\0';
	p = strchr(a, '-');
	if (p)
		*p = '\0';
	err = kstrtol(a + sizeof(DbgaufsXi_PREFIX) - 1, 10, &l);
	if (unlikely(err))
		goto out;
	bindex = l;
	idx = 0;
	if (p) {
		err = kstrtol(p + 1, 10, &l);
		if (unlikely(err))
			goto out;
		idx = l;
	}

	err = -ENOENT;
	sbinfo = inode->i_private;
	sb = sbinfo->si_sb;
	si_noflush_read_lock(sb);
	if (unlikely(bindex < 0 || bindex > au_sbbot(sb)))
		goto out_si;
	br = au_sbr(sb, bindex);
	xi = br->br_xino;
	if (unlikely(idx >= xi->xi_nfile))
		goto out_si;
	xf = au_xino_file(xi, idx);
	if (xf)
		err = dbgaufs_xi_open(xf, file, /*do_fcnt*/1,
				      au_xino_count(br));

out_si:
	si_read_unlock(sb);
out:
	AuTraceErr(err);
	return err;
}

static const struct file_operations dbgaufs_xino_fop = {
	.owner		= THIS_MODULE,
	.open		= dbgaufs_xino_open,
	.release	= dbgaufs_xi_release,
	.read		= dbgaufs_xi_read
};

void dbgaufs_xino_del(struct au_branch *br)
{
	struct dentry *dbgaufs;

	dbgaufs = br->br_dbgaufs;
	if (!dbgaufs)
		return;

	br->br_dbgaufs = NULL;
	/* debugfs acquires the parent i_mutex */
	lockdep_off();
	debugfs_remove(dbgaufs);
	lockdep_on();
}

void dbgaufs_brs_del(struct super_block *sb, aufs_bindex_t bindex)
{
	aufs_bindex_t bbot;
	struct au_branch *br;

	if (!au_sbi(sb)->si_dbgaufs)
		return;

	bbot = au_sbbot(sb);
	for (; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		dbgaufs_xino_del(br);
	}
}

static void dbgaufs_br_do_add(struct super_block *sb, aufs_bindex_t bindex,
			      unsigned int idx, struct dentry *parent,
			      struct au_sbinfo *sbinfo)
{
	struct au_branch *br;
	struct dentry *d;
	/* "xi" bindex(5) "-" idx(2) NULL */
	char name[sizeof(DbgaufsXi_PREFIX) + 8];

	if (!idx)
		snprintf(name, sizeof(name), DbgaufsXi_PREFIX "%d", bindex);
	else
		snprintf(name, sizeof(name), DbgaufsXi_PREFIX "%d-%u",
			 bindex, idx);
	br = au_sbr(sb, bindex);
	if (br->br_dbgaufs) {
		struct qstr qstr = QSTR_INIT(name, strlen(name));

		if (!au_qstreq(&br->br_dbgaufs->d_name, &qstr)) {
			/* debugfs acquires the parent i_mutex */
			lockdep_off();
			d = debugfs_rename(parent, br->br_dbgaufs, parent,
					   name);
			lockdep_on();
			if (unlikely(!d))
				pr_warn("failed renaming %pd/%s, ignored.\n",
					parent, name);
		}
	} else {
		lockdep_off();
		br->br_dbgaufs = debugfs_create_file(name, dbgaufs_mode, parent,
						     sbinfo, &dbgaufs_xino_fop);
		lockdep_on();
		if (unlikely(!br->br_dbgaufs))
			pr_warn("failed creating %pd/%s, ignored.\n",
				parent, name);
	}
}

static void dbgaufs_br_add(struct super_block *sb, aufs_bindex_t bindex,
			   struct dentry *parent, struct au_sbinfo *sbinfo)
{
	struct au_branch *br;
	struct au_xino *xi;
	unsigned int u;

	br = au_sbr(sb, bindex);
	xi = br->br_xino;
	for (u = 0; u < xi->xi_nfile; u++)
		dbgaufs_br_do_add(sb, bindex, u, parent, sbinfo);
}

void dbgaufs_brs_add(struct super_block *sb, aufs_bindex_t bindex, int topdown)
{
	struct au_sbinfo *sbinfo;
	struct dentry *parent;
	aufs_bindex_t bbot;

	if (!au_opt_test(au_mntflags(sb), XINO))
		return;

	sbinfo = au_sbi(sb);
	parent = sbinfo->si_dbgaufs;
	if (!parent)
		return;

	bbot = au_sbbot(sb);
	if (topdown)
		for (; bindex <= bbot; bindex++)
			dbgaufs_br_add(sb, bindex, parent, sbinfo);
	else
		for (; bbot >= bindex; bbot--)
			dbgaufs_br_add(sb, bbot, parent, sbinfo);
}

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_EXPORT
static int dbgaufs_xigen_open(struct inode *inode, struct file *file)
{
	int err;
	struct au_sbinfo *sbinfo;
	struct super_block *sb;

	sbinfo = inode->i_private;
	sb = sbinfo->si_sb;
	si_noflush_read_lock(sb);
	err = dbgaufs_xi_open(sbinfo->si_xigen, file, /*do_fcnt*/0, /*cnt*/0);
	si_read_unlock(sb);
	return err;
}

static const struct file_operations dbgaufs_xigen_fop = {
	.owner		= THIS_MODULE,
	.open		= dbgaufs_xigen_open,
	.release	= dbgaufs_xi_release,
	.read		= dbgaufs_xi_read
};

static int dbgaufs_xigen_init(struct au_sbinfo *sbinfo)
{
	int err;

	/*
	 * This function is a dynamic '__init' function actually,
	 * so the tiny check for si_rwsem is unnecessary.
	 */
	/* AuRwMustWriteLock(&sbinfo->si_rwsem); */

	err = -EIO;
	sbinfo->si_dbgaufs_xigen = debugfs_create_file
		("xigen", dbgaufs_mode, sbinfo->si_dbgaufs, sbinfo,
		 &dbgaufs_xigen_fop);
	if (sbinfo->si_dbgaufs_xigen)
		err = 0;

	return err;
}
#else
static int dbgaufs_xigen_init(struct au_sbinfo *sbinfo)
{
	return 0;
}
#endif /* CONFIG_AUFS_EXPORT */

/* ---------------------------------------------------------------------- */

void dbgaufs_si_fin(struct au_sbinfo *sbinfo)
{
	/*
	 * This function is a dynamic '__fin' function actually,
	 * so the tiny check for si_rwsem is unnecessary.
	 */
	/* AuRwMustWriteLock(&sbinfo->si_rwsem); */

	debugfs_remove_recursive(sbinfo->si_dbgaufs);
	sbinfo->si_dbgaufs = NULL;
}

int dbgaufs_si_init(struct au_sbinfo *sbinfo)
{
	int err;
	char name[SysaufsSiNameLen];

	/*
	 * This function is a dynamic '__init' function actually,
	 * so the tiny check for si_rwsem is unnecessary.
	 */
	/* AuRwMustWriteLock(&sbinfo->si_rwsem); */

	err = -ENOENT;
	if (!dbgaufs) {
		AuErr1("/debug/aufs is uninitialized\n");
		goto out;
	}

	err = -EIO;
	sysaufs_name(sbinfo, name);
	sbinfo->si_dbgaufs = debugfs_create_dir(name, dbgaufs);
	if (unlikely(!sbinfo->si_dbgaufs))
		goto out;

	/* regardless plink/noplink option */
	sbinfo->si_dbgaufs_plink = debugfs_create_file
		("plink", dbgaufs_mode, sbinfo->si_dbgaufs, sbinfo,
		 &dbgaufs_plink_fop);
	if (unlikely(!sbinfo->si_dbgaufs_plink))
		goto out_dir;

	/* regardless xino/noxino option */
	sbinfo->si_dbgaufs_xib = debugfs_create_file
		("xib", dbgaufs_mode, sbinfo->si_dbgaufs, sbinfo,
		 &dbgaufs_xib_fop);
	if (unlikely(!sbinfo->si_dbgaufs_xib))
		goto out_dir;

	err = dbgaufs_xigen_init(sbinfo);
	if (!err)
		goto out; /* success */

out_dir:
	dbgaufs_si_fin(sbinfo);
out:
	if (unlikely(err))
		pr_err("debugfs/aufs failed\n");
	return err;
}

/* ---------------------------------------------------------------------- */

void dbgaufs_fin(void)
{
	debugfs_remove(dbgaufs);
}

int __init dbgaufs_init(void)
{
	int err;

	err = -EIO;
	dbgaufs = debugfs_create_dir(AUFS_NAME, NULL);
	if (dbgaufs)
		err = 0;
	return err;
}
