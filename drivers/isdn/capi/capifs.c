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

static char *revision = "$Revision: 1.1.2.3 $";

/* ------------------------------------------------------------------ */

#define CAPIFS_SUPER_MAGIC (('C'<<8)|'N')

static struct vfsmount *capifs_mnt;
static struct dentry *capifs_root;

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
			printk("capifs: called with bogus options\n");
			return -EINVAL;
		}
	}
	config.setuid  = setuid;
	config.setgid  = setgid;
	config.uid     = uid;
	config.gid     = gid;
	config.mode    = mode;
	return 0;
}

static struct super_operations capifs_sops =
{
	.statfs		= simple_statfs,
	.remount_fs	= capifs_remount,
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
	inode->i_blocks = 0;
	inode->i_blksize = 1024;
	inode->i_uid = inode->i_gid = 0;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_nlink = 2;

	capifs_root = s->s_root = d_alloc_root(inode);
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

static struct dentry *get_node(int num)
{
	char s[10];
	struct dentry *root = capifs_root;
	mutex_lock(&root->d_inode->i_mutex);
	return lookup_one_len(s, root, sprintf(s, "%d", num));
}

void capifs_new_ncci(unsigned int number, dev_t device)
{
	struct dentry *dentry;
	struct inode *inode = new_inode(capifs_mnt->mnt_sb);
	if (!inode)
		return;
	inode->i_ino = number+2;
	inode->i_blksize = 1024;
	inode->i_uid = config.setuid ? config.uid : current->fsuid;
	inode->i_gid = config.setgid ? config.gid : current->fsgid;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	init_special_inode(inode, S_IFCHR|config.mode, device);
	//inode->i_op = &capifs_file_inode_operations;

	dentry = get_node(number);
	if (!IS_ERR(dentry) && !dentry->d_inode)
		d_instantiate(dentry, inode);
	mutex_unlock(&capifs_root->d_inode->i_mutex);
}

void capifs_free_ncci(unsigned int number)
{
	struct dentry *dentry = get_node(number);

	if (!IS_ERR(dentry)) {
		struct inode *inode = dentry->d_inode;
		if (inode) {
			inode->i_nlink--;
			d_delete(dentry);
			dput(dentry);
		}
		dput(dentry);
	}
	mutex_unlock(&capifs_root->d_inode->i_mutex);
}

static int __init capifs_init(void)
{
	char rev[32];
	char *p;
	int err;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, sizeof(rev));
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	err = register_filesystem(&capifs_fs_type);
	if (!err) {
		capifs_mnt = kern_mount(&capifs_fs_type);
		if (IS_ERR(capifs_mnt)) {
			err = PTR_ERR(capifs_mnt);
			unregister_filesystem(&capifs_fs_type);
		}
	}
	if (!err)
		printk(KERN_NOTICE "capifs: Rev %s\n", rev);
	return err;
}

static void __exit capifs_exit(void)
{
	unregister_filesystem(&capifs_fs_type);
	mntput(capifs_mnt);
}

EXPORT_SYMBOL(capifs_new_ncci);
EXPORT_SYMBOL(capifs_free_ncci);

module_init(capifs_init);
module_exit(capifs_exit);
