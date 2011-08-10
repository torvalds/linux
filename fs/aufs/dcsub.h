/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * sub-routines for dentry cache
 */

#ifndef __AUFS_DCSUB_H__
#define __AUFS_DCSUB_H__

#ifdef __KERNEL__

#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/types.h>

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

#endif /* __KERNEL__ */
#endif /* __AUFS_DCSUB_H__ */
