/**
 * @file oprofilefs.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 *
 * A simple filesystem for configuration and
 * access of oprofile.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/oprofile.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "oprof.h"

#define OPROFILEFS_MAGIC 0x6f70726f

DEFINE_SPINLOCK(oprofilefs_lock);

static struct inode * oprofilefs_get_inode(struct super_block * sb, int mode)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}


static struct super_operations s_ops = {
	.statfs		= simple_statfs,
	.drop_inode 	= generic_delete_inode,
};


ssize_t oprofilefs_str_to_user(char const * str, char __user * buf, size_t count, loff_t * offset)
{
	return simple_read_from_buffer(buf, count, offset, str, strlen(str));
}


#define TMPBUFSIZE 50

ssize_t oprofilefs_ulong_to_user(unsigned long val, char __user * buf, size_t count, loff_t * offset)
{
	char tmpbuf[TMPBUFSIZE];
	size_t maxlen = snprintf(tmpbuf, TMPBUFSIZE, "%lu\n", val);
	if (maxlen > TMPBUFSIZE)
		maxlen = TMPBUFSIZE;
	return simple_read_from_buffer(buf, count, offset, tmpbuf, maxlen);
}


int oprofilefs_ulong_from_user(unsigned long * val, char const __user * buf, size_t count)
{
	char tmpbuf[TMPBUFSIZE];

	if (!count)
		return 0;

	if (count > TMPBUFSIZE - 1)
		return -EINVAL;

	memset(tmpbuf, 0x0, TMPBUFSIZE);

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;

	spin_lock(&oprofilefs_lock);
	*val = simple_strtoul(tmpbuf, NULL, 0);
	spin_unlock(&oprofilefs_lock);
	return 0;
}


static ssize_t ulong_read_file(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	unsigned long * val = file->private_data;
	return oprofilefs_ulong_to_user(*val, buf, count, offset);
}


static ssize_t ulong_write_file(struct file * file, char const __user * buf, size_t count, loff_t * offset)
{
	unsigned long * value = file->private_data;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(value, buf, count);

	if (retval)
		return retval;
	return count;
}


static int default_open(struct inode * inode, struct file * filp)
{
	if (inode->u.generic_ip)
		filp->private_data = inode->u.generic_ip;
	return 0;
}


static struct file_operations ulong_fops = {
	.read		= ulong_read_file,
	.write		= ulong_write_file,
	.open		= default_open,
};


static struct file_operations ulong_ro_fops = {
	.read		= ulong_read_file,
	.open		= default_open,
};


static struct dentry * __oprofilefs_create_file(struct super_block * sb,
	struct dentry * root, char const * name, struct file_operations * fops,
	int perm)
{
	struct dentry * dentry;
	struct inode * inode;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return NULL;
	inode = oprofilefs_get_inode(sb, S_IFREG | perm);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_fop = fops;
	d_add(dentry, inode);
	return dentry;
}


int oprofilefs_create_ulong(struct super_block * sb, struct dentry * root,
	char const * name, unsigned long * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name,
						     &ulong_fops, 0644);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}


int oprofilefs_create_ro_ulong(struct super_block * sb, struct dentry * root,
	char const * name, unsigned long * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name,
						     &ulong_ro_fops, 0444);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}


static ssize_t atomic_read_file(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	atomic_t * val = file->private_data;
	return oprofilefs_ulong_to_user(atomic_read(val), buf, count, offset);
}
 

static struct file_operations atomic_ro_fops = {
	.read		= atomic_read_file,
	.open		= default_open,
};
 

int oprofilefs_create_ro_atomic(struct super_block * sb, struct dentry * root,
	char const * name, atomic_t * val)
{
	struct dentry * d = __oprofilefs_create_file(sb, root, name,
						     &atomic_ro_fops, 0444);
	if (!d)
		return -EFAULT;

	d->d_inode->u.generic_ip = val;
	return 0;
}

 
int oprofilefs_create_file(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops)
{
	if (!__oprofilefs_create_file(sb, root, name, fops, 0644))
		return -EFAULT;
	return 0;
}


int oprofilefs_create_file_perm(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops, int perm)
{
	if (!__oprofilefs_create_file(sb, root, name, fops, perm))
		return -EFAULT;
	return 0;
}


struct dentry * oprofilefs_mkdir(struct super_block * sb,
	struct dentry * root, char const * name)
{
	struct dentry * dentry;
	struct inode * inode;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return NULL;
	inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	d_add(dentry, inode);
	return dentry;
}


static int oprofilefs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * root_inode;
	struct dentry * root_dentry;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = OPROFILEFS_MAGIC;
	sb->s_op = &s_ops;
	sb->s_time_gran = 1;

	root_inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!root_inode)
		return -ENOMEM;
	root_inode->i_op = &simple_dir_inode_operations;
	root_inode->i_fop = &simple_dir_operations;
	root_dentry = d_alloc_root(root_inode);
	if (!root_dentry) {
		iput(root_inode);
		return -ENOMEM;
	}

	sb->s_root = root_dentry;

	oprofile_create_files(sb, root_dentry);

	// FIXME: verify kill_litter_super removes our dentries
	return 0;
}


static struct super_block *oprofilefs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, oprofilefs_fill_super);
}


static struct file_system_type oprofilefs_type = {
	.owner		= THIS_MODULE,
	.name		= "oprofilefs",
	.get_sb		= oprofilefs_get_sb,
	.kill_sb	= kill_litter_super,
};


int __init oprofilefs_register(void)
{
	return register_filesystem(&oprofilefs_type);
}


void __exit oprofilefs_unregister(void)
{
	unregister_filesystem(&oprofilefs_type);
}
