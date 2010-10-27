/* $Id: capifs.c,v 1.1.2.3 2004/01/16 21:09:26 keil Exp $
 * 
 * Copyright 2000 by Carsten Paeth <calle@calle.de>
 *
 * Heavily based on devpts filesystem from H. Peter Anvin
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/sched.h>	/* current */

#include "capifs.h"

MODULE_DESCRIPTION("CAPI4Linux: /dev/capi/ filesystem");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

#define CAPIFS_SUPER_MAGIC (('C'<<8)|'N')

static struct vfsmount *capifs_mnt;
static int capifs_mnt_count;

static struct {
	int setuid;
	int setgid;
	uid_t   uid;
	gid_t   gid;
	umode_t mode;
} config = {.mode = 0600};

/* ------------------------------------------------------------------ */

static int capifs_remount(struct super_block *s, int *flags, char *data)
{
	int setuid = 0;
	int setgid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	umode_t mode = 0600;
	char *this_char;
	char *new_opt = kstrdup(data, GFP_KERNEL);

	this_char = NULL;
	while ((this_char = strsep(&data, ",")) != NULL) {
		int n;
		char dummy;
		if (!*this_char)
			continue;
		if (sscanf(this_char, "uid=%i%c", &n, &dummy) == 1) {
			setuid = 1;
			uid = n;
		} else if (sscanf(this_char, "gid=%i%c", &n, &dummy) == 1) {
			setgid = 1;
			gid = n;
		} else if (sscanf(this_char, "mode=%o%c", &n, &dummy) == 1)
			mode = n & ~S_IFMT;
		else {
			kfree(new_opt);
			printk("capifs: called with bogus options\n");
			return -EINVAL;
		}
	}

	mutex_lock(&s->s_root->d_inode->i_mutex);

	replace_mount_options(s, new_opt);
	config.setuid  = setuid;
	config.setgid  = setgid;
	config.uid     = uid;
	config.gid     = gid;
	config.mode    = mode;

	mutex_unlock(&s->s_root->d_inode->i_mutex);

	return 0;
}

static const struct super_operations capifs_sops =
{
	.statfs		= simple_statfs,
	.remount_fs	= capifs_remount,
	.show_options	= generic_show_options,
};


static int
capifs_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * inode;

	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = CAPIFS_SUPER_MAGIC;
	s->s_op = &capifs_sops;
	s->s_time_gran = 1;

	inode = new_inode(s);
	if (!inode)
		goto fail;
	inode->i_ino = 1;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_nlink = 2;

	s->s_root = d_alloc_root(inode);
	if (s->s_root)
		return 0;
	
	printk("capifs: get root dentry failed\n");
	iput(inode);
fail:
	return -ENOMEM;
}

static int capifs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, capifs_fill_super, mnt);
}

static struct file_system_type capifs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "capifs",
	.get_sb		= capifs_get_sb,
	.kill_sb	= kill_anon_super,
};

static struct dentry *new_ncci(unsigned int number, dev_t device)
{
	struct super_block *s = capifs_mnt->mnt_sb;
	struct dentry *root = s->s_root;
	struct dentry *dentry;
	struct inode *inode;
	char name[10];
	int namelen;

	mutex_lock(&root->d_inode->i_mutex);

	namelen = sprintf(name, "%d", number);
	dentry = lookup_one_len(name, root, namelen);
	if (IS_ERR(dentry)) {
		dentry = NULL;
		goto unlock_out;
	}

	if (dentry->d_inode) {
		dput(dentry);
		dentry = NULL;
		goto unlock_out;
	}

	inode = new_inode(s);
	if (!inode) {
		dput(dentry);
		dentry = NULL;
		goto unlock_out;
	}

	/* config contents is protected by root's i_mutex */
	inode->i_uid = config.setuid ? config.uid : current_fsuid();
	inode->i_gid = config.setgid ? config.gid : current_fsgid();
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_ino = number + 2;
	init_special_inode(inode, S_IFCHR|config.mode, device);

	d_instantiate(dentry, inode);
	dget(dentry);

unlock_out:
	mutex_unlock(&root->d_inode->i_mutex);

	return dentry;
}

struct dentry *capifs_new_ncci(unsigned int number, dev_t device)
{
	struct dentry *dentry;

	if (simple_pin_fs(&capifs_fs_type, &capifs_mnt, &capifs_mnt_count) < 0)
		return NULL;

	dentry = new_ncci(number, device);
	if (!dentry)
		simple_release_fs(&capifs_mnt, &capifs_mnt_count);

	return dentry;
}

void capifs_free_ncci(struct dentry *dentry)
{
	struct dentry *root = capifs_mnt->mnt_sb->s_root;
	struct inode *inode;

	if (!dentry)
		return;

	mutex_lock(&root->d_inode->i_mutex);

	inode = dentry->d_inode;
	if (inode) {
		drop_nlink(inode);
		d_delete(dentry);
		dput(dentry);
	}
	dput(dentry);

	mutex_unlock(&root->d_inode->i_mutex);

	simple_release_fs(&capifs_mnt, &capifs_mnt_count);
}

static int __init capifs_init(void)
{
	return register_filesystem(&capifs_fs_type);
}

static void __exit capifs_exit(void)
{
	unregister_filesystem(&capifs_fs_type);
}

EXPORT_SYMBOL(capifs_new_ncci);
EXPORT_SYMBOL(capifs_free_ncci);

module_init(capifs_init);
module_exit(capifs_exit);
