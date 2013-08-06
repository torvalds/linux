/*
 * Copyright (C) 2005-2013 Junjiro R. Okajima
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
 * support for loopback block device as a branch
 */

#include "aufs.h"

/* added into drivers/block/loop.c */
static struct file *(*backing_file_func)(struct super_block *sb);

/*
 * test if two lower dentries have overlapping branches.
 */
int au_test_loopback_overlap(struct super_block *sb, struct dentry *h_adding)
{
	struct super_block *h_sb;
	struct file *backing_file;

	h_sb = h_adding->d_sb;
	backing_file = backing_file_func(h_sb);
	if (!backing_file)
		return 0;

	h_adding = backing_file->f_dentry;
	/*
	 * h_adding can be local NFS.
	 * in this case aufs cannot detect the loop.
	 */
	if (unlikely(h_adding->d_sb == sb))
		return 1;
	return !!au_test_subdir(h_adding, sb->s_root);
}

/* true if a kernel thread named 'loop[0-9].*' accesses a file */
int au_test_loopback_kthread(void)
{
	int ret;
	struct task_struct *tsk = current;
	char c, comm[sizeof(tsk->comm)];

	ret = 0;
	if (tsk->flags & PF_KTHREAD) {
		get_task_comm(comm, tsk);
		c = comm[4];
		ret = ('0' <= c && c <= '9'
		       && !strncmp(comm, "loop", 4));
	}

	return ret;
}

/* ---------------------------------------------------------------------- */

#define au_warn_loopback_step	16
static int au_warn_loopback_nelem = au_warn_loopback_step;
static unsigned long *au_warn_loopback_array;

void au_warn_loopback(struct super_block *h_sb)
{
	int i, new_nelem;
	unsigned long *a, magic;
	static DEFINE_SPINLOCK(spin);

	magic = h_sb->s_magic;
	spin_lock(&spin);
	a = au_warn_loopback_array;
	for (i = 0; i < au_warn_loopback_nelem && *a; i++)
		if (a[i] == magic) {
			spin_unlock(&spin);
			return;
		}

	/* h_sb is new to us, print it */
	if (i < au_warn_loopback_nelem) {
		a[i] = magic;
		goto pr;
	}

	/* expand the array */
	new_nelem = au_warn_loopback_nelem + au_warn_loopback_step;
	a = au_kzrealloc(au_warn_loopback_array,
			 au_warn_loopback_nelem * sizeof(unsigned long),
			 new_nelem * sizeof(unsigned long), GFP_ATOMIC);
	if (a) {
		au_warn_loopback_nelem = new_nelem;
		au_warn_loopback_array = a;
		a[i] = magic;
		goto pr;
	}

	spin_unlock(&spin);
	AuWarn1("realloc failed, ignored\n");
	return;

pr:
	spin_unlock(&spin);
	pr_warn("you may want to try another patch for loopback file "
		"on %s(0x%lx) branch\n", au_sbtype(h_sb), magic);
}

int au_loopback_init(void)
{
	int err;
	struct super_block *sb __maybe_unused;

	AuDebugOn(sizeof(sb->s_magic) != sizeof(unsigned long));

	err = -ENOMEM;
	au_warn_loopback_array = kcalloc(au_warn_loopback_step,
					 sizeof(unsigned long), GFP_NOFS);
	if (unlikely(!au_warn_loopback_array))
		goto out;

	err = 0;
	backing_file_func = symbol_get(loop_backing_file);
	if (backing_file_func)
		goto out; /* success */

	pr_err("loop_backing_file() is not defined\n");
	err = -ENOSYS;
	kfree(au_warn_loopback_array);
out:
	return err;
}

void au_loopback_fin(void)
{
	symbol_put(loop_backing_file);
	kfree(au_warn_loopback_array);
}
