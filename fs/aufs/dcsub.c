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
 * sub-routines for dentry cache
 */

#include "aufs.h"

static void au_dpage_free(struct au_dpage *dpage)
{
	int i;
	struct dentry **p;

	p = dpage->dentries;
	for (i = 0; i < dpage->ndentry; i++)
		dput(*p++);
	free_page((unsigned long)dpage->dentries);
}

int au_dpages_init(struct au_dcsub_pages *dpages, gfp_t gfp)
{
	int err;
	void *p;

	err = -ENOMEM;
	dpages->dpages = kmalloc(sizeof(*dpages->dpages), gfp);
	if (unlikely(!dpages->dpages))
		goto out;

	p = (void *)__get_free_page(gfp);
	if (unlikely(!p))
		goto out_dpages;

	dpages->dpages[0].ndentry = 0;
	dpages->dpages[0].dentries = p;
	dpages->ndpage = 1;
	return 0; /* success */

out_dpages:
	kfree(dpages->dpages);
out:
	return err;
}

void au_dpages_free(struct au_dcsub_pages *dpages)
{
	int i;
	struct au_dpage *p;

	p = dpages->dpages;
	for (i = 0; i < dpages->ndpage; i++)
		au_dpage_free(p++);
	kfree(dpages->dpages);
}

static int au_dpages_append(struct au_dcsub_pages *dpages,
			    struct dentry *dentry, gfp_t gfp)
{
	int err, sz;
	struct au_dpage *dpage;
	void *p;

	dpage = dpages->dpages + dpages->ndpage - 1;
	sz = PAGE_SIZE / sizeof(dentry);
	if (unlikely(dpage->ndentry >= sz)) {
		AuLabel(new dpage);
		err = -ENOMEM;
		sz = dpages->ndpage * sizeof(*dpages->dpages);
		p = au_kzrealloc(dpages->dpages, sz,
				 sz + sizeof(*dpages->dpages), gfp,
				 /*may_shrink*/0);
		if (unlikely(!p))
			goto out;

		dpages->dpages = p;
		dpage = dpages->dpages + dpages->ndpage;
		p = (void *)__get_free_page(gfp);
		if (unlikely(!p))
			goto out;

		dpage->ndentry = 0;
		dpage->dentries = p;
		dpages->ndpage++;
	}

	AuDebugOn(au_dcount(dentry) <= 0);
	dpage->dentries[dpage->ndentry++] = dget_dlock(dentry);
	return 0; /* success */

out:
	return err;
}

/* todo: BAD approach */
/* copied from linux/fs/dcache.c */
enum d_walk_ret {
	D_WALK_CONTINUE,
	D_WALK_QUIT,
	D_WALK_NORETRY,
	D_WALK_SKIP,
};

extern void d_walk(struct dentry *parent, void *data,
		   enum d_walk_ret (*enter)(void *, struct dentry *),
		   void (*finish)(void *));

struct ac_dpages_arg {
	int err;
	struct au_dcsub_pages *dpages;
	struct super_block *sb;
	au_dpages_test test;
	void *arg;
};

static enum d_walk_ret au_call_dpages_append(void *_arg, struct dentry *dentry)
{
	enum d_walk_ret ret;
	struct ac_dpages_arg *arg = _arg;

	ret = D_WALK_CONTINUE;
	if (dentry->d_sb == arg->sb
	    && !IS_ROOT(dentry)
	    && au_dcount(dentry) > 0
	    && au_di(dentry)
	    && (!arg->test || arg->test(dentry, arg->arg))) {
		arg->err = au_dpages_append(arg->dpages, dentry, GFP_ATOMIC);
		if (unlikely(arg->err))
			ret = D_WALK_QUIT;
	}

	return ret;
}

int au_dcsub_pages(struct au_dcsub_pages *dpages, struct dentry *root,
		   au_dpages_test test, void *arg)
{
	struct ac_dpages_arg args = {
		.err	= 0,
		.dpages	= dpages,
		.sb	= root->d_sb,
		.test	= test,
		.arg	= arg
	};

	d_walk(root, &args, au_call_dpages_append, NULL);

	return args.err;
}

int au_dcsub_pages_rev(struct au_dcsub_pages *dpages, struct dentry *dentry,
		       int do_include, au_dpages_test test, void *arg)
{
	int err;

	err = 0;
	write_seqlock(&rename_lock);
	spin_lock(&dentry->d_lock);
	if (do_include
	    && au_dcount(dentry) > 0
	    && (!test || test(dentry, arg)))
		err = au_dpages_append(dpages, dentry, GFP_ATOMIC);
	spin_unlock(&dentry->d_lock);
	if (unlikely(err))
		goto out;

	/*
	 * RCU for vfsmount is unnecessary since this is a traverse in a single
	 * mount
	 */
	while (!IS_ROOT(dentry)) {
		dentry = dentry->d_parent; /* rename_lock is locked */
		spin_lock(&dentry->d_lock);
		if (au_dcount(dentry) > 0
		    && (!test || test(dentry, arg)))
			err = au_dpages_append(dpages, dentry, GFP_ATOMIC);
		spin_unlock(&dentry->d_lock);
		if (unlikely(err))
			break;
	}

out:
	write_sequnlock(&rename_lock);
	return err;
}

static inline int au_dcsub_dpages_aufs(struct dentry *dentry, void *arg)
{
	return au_di(dentry) && dentry->d_sb == arg;
}

int au_dcsub_pages_rev_aufs(struct au_dcsub_pages *dpages,
			    struct dentry *dentry, int do_include)
{
	return au_dcsub_pages_rev(dpages, dentry, do_include,
				  au_dcsub_dpages_aufs, dentry->d_sb);
}

int au_test_subdir(struct dentry *d1, struct dentry *d2)
{
	struct path path[2] = {
		{
			.dentry = d1
		},
		{
			.dentry = d2
		}
	};

	return path_is_under(path + 0, path + 1);
}
