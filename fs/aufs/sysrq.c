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
 * magic sysrq handler
 */

/* #include <linux/sysrq.h> */
#include <linux/writeback.h>
#include "aufs.h"

/* ---------------------------------------------------------------------- */

static void sysrq_sb(struct super_block *sb)
{
	char *plevel;
	struct au_sbinfo *sbinfo;
	struct file *file;
	struct hlist_bl_head *files;
	struct hlist_bl_node *pos;
	struct au_finfo *finfo;

	plevel = au_plevel;
	au_plevel = KERN_WARNING;

	/* since we define pr_fmt, call printk directly */
#define pr(str) printk(KERN_WARNING AUFS_NAME ": " str)

	sbinfo = au_sbi(sb);
	printk(KERN_WARNING "si=%lx\n", sysaufs_si_id(sbinfo));
	pr("superblock\n");
	au_dpri_sb(sb);

#if 0
	pr("root dentry\n");
	au_dpri_dentry(sb->s_root);
	pr("root inode\n");
	au_dpri_inode(d_inode(sb->s_root));
#endif

#if 0
	do {
		int err, i, j, ndentry;
		struct au_dcsub_pages dpages;
		struct au_dpage *dpage;

		err = au_dpages_init(&dpages, GFP_ATOMIC);
		if (unlikely(err))
			break;
		err = au_dcsub_pages(&dpages, sb->s_root, NULL, NULL);
		if (!err)
			for (i = 0; i < dpages.ndpage; i++) {
				dpage = dpages.dpages + i;
				ndentry = dpage->ndentry;
				for (j = 0; j < ndentry; j++)
					au_dpri_dentry(dpage->dentries[j]);
			}
		au_dpages_free(&dpages);
	} while (0);
#endif

#if 1
	{
		struct inode *i;

		pr("isolated inode\n");
		spin_lock(&sb->s_inode_list_lock);
		list_for_each_entry(i, &sb->s_inodes, i_sb_list) {
			spin_lock(&i->i_lock);
			if (1 || hlist_empty(&i->i_dentry))
				au_dpri_inode(i);
			spin_unlock(&i->i_lock);
		}
		spin_unlock(&sb->s_inode_list_lock);
	}
#endif
	pr("files\n");
	files = &au_sbi(sb)->si_files;
	hlist_bl_lock(files);
	hlist_bl_for_each_entry(finfo, pos, files, fi_hlist) {
		umode_t mode;

		file = finfo->fi_file;
		mode = file_inode(file)->i_mode;
		if (!special_file(mode))
			au_dpri_file(file);
	}
	hlist_bl_unlock(files);
	pr("done\n");

#undef pr
	au_plevel = plevel;
}

/* ---------------------------------------------------------------------- */

/* module parameter */
static char *aufs_sysrq_key = "a";
module_param_named(sysrq, aufs_sysrq_key, charp, 0444);
MODULE_PARM_DESC(sysrq, "MagicSysRq key for " AUFS_NAME);

static void au_sysrq(int key __maybe_unused)
{
	struct au_sbinfo *sbinfo;
	struct hlist_bl_node *pos;

	lockdep_off();
	au_sbilist_lock();
	hlist_bl_for_each_entry(sbinfo, pos, &au_sbilist, si_list)
		sysrq_sb(sbinfo->si_sb);
	au_sbilist_unlock();
	lockdep_on();
}

static struct sysrq_key_op au_sysrq_op = {
	.handler	= au_sysrq,
	.help_msg	= "Aufs",
	.action_msg	= "Aufs",
	.enable_mask	= SYSRQ_ENABLE_DUMP
};

/* ---------------------------------------------------------------------- */

int __init au_sysrq_init(void)
{
	int err;
	char key;

	err = -1;
	key = *aufs_sysrq_key;
	if ('a' <= key && key <= 'z')
		err = register_sysrq_key(key, &au_sysrq_op);
	if (unlikely(err))
		pr_err("err %d, sysrq=%c\n", err, key);
	return err;
}

void au_sysrq_fin(void)
{
	int err;

	err = unregister_sysrq_key(*aufs_sysrq_key, &au_sysrq_op);
	if (unlikely(err))
		pr_err("err %d (ignored)\n", err);
}
