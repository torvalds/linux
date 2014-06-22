/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
 */

/*
 * sub-routines for dentry cache
 */

#ifndef __AUFS_DCSUB_H__
#define __AUFS_DCSUB_H__

#ifdef __KERNEL__

#include <linux/dcache.h>
#include <linux/fs.h>

struct dentry;

struct au_dpage {
	int ndentry;
	struct dentry **dentries;
};

struct au_dcsub_pages {
	int ndpage;
	struct au_dpage *dpages;
};

/* ---------------------------------------------------------------------- */

/* dcsub.c */
int au_dpages_init(struct au_dcsub_pages *dpages, gfp_t gfp);
void au_dpages_free(struct au_dcsub_pages *dpages);
typedef int (*au_dpages_test)(struct dentry *dentry, void *arg);
int au_dcsub_pages(struct au_dcsub_pages *dpages, struct dentry *root,
		   au_dpages_test test, void *arg);
int au_dcsub_pages_rev(struct au_dcsub_pages *dpages, struct dentry *dentry,
		       int do_include, au_dpages_test test, void *arg);
int au_dcsub_pages_rev_aufs(struct au_dcsub_pages *dpages,
			    struct dentry *dentry, int do_include);
int au_test_subdir(struct dentry *d1, struct dentry *d2);

/* ---------------------------------------------------------------------- */

static inline int au_d_hashed_positive(struct dentry *d)
{
	int err;
	struct inode *inode = d->d_inode;

	err = 0;
	if (unlikely(d_unhashed(d) || !inode || !inode->i_nlink))
		err = -ENOENT;
	return err;
}

static inline int au_d_alive(struct dentry *d)
{
	int err;
	struct inode *inode;

	err = 0;
	if (!IS_ROOT(d))
		err = au_d_hashed_positive(d);
	else {
		inode = d->d_inode;
		if (unlikely(d_unlinked(d) || !inode || !inode->i_nlink))
			err = -ENOENT;
	}
	return err;
}

static inline int au_alive_dir(struct dentry *d)
{
	int err;

	err = au_d_alive(d);
	if (unlikely(err || IS_DEADDIR(d->d_inode)))
		err = -ENOENT;
	return err;
}

static inline int au_qstreq(struct qstr *a, struct qstr *b)
{
	return a->len == b->len
		&& !memcmp(a->name, b->name, a->len);
}

#endif /* __KERNEL__ */
#endif /* __AUFS_DCSUB_H__ */
