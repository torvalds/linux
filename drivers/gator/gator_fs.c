/**
 * @file gatorfs.c
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
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/uaccess.h>

#define gatorfs_MAGIC 0x24051020
#define TMPBUFSIZE 50
DEFINE_SPINLOCK(gatorfs_lock);

static struct inode *gatorfs_get_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
		inode->i_ino = get_next_ino();
#endif
		inode->i_mode = mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}

static const struct super_operations s_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
};

static ssize_t gatorfs_ulong_to_user(unsigned long val, char __user *buf, size_t count, loff_t *offset)
{
	char tmpbuf[TMPBUFSIZE];
	size_t maxlen = snprintf(tmpbuf, TMPBUFSIZE, "%lu\n", val);

	if (maxlen > TMPBUFSIZE)
		maxlen = TMPBUFSIZE;
	return simple_read_from_buffer(buf, count, offset, tmpbuf, maxlen);
}

static ssize_t gatorfs_u64_to_user(u64 val, char __user *buf, size_t count, loff_t *offset)
{
	char tmpbuf[TMPBUFSIZE];
	size_t maxlen = snprintf(tmpbuf, TMPBUFSIZE, "%llu\n", val);

	if (maxlen > TMPBUFSIZE)
		maxlen = TMPBUFSIZE;
	return simple_read_from_buffer(buf, count, offset, tmpbuf, maxlen);
}

static int gatorfs_ulong_from_user(unsigned long *val, char const __user *buf, size_t count)
{
	char tmpbuf[TMPBUFSIZE];
	unsigned long flags;

	if (!count)
		return 0;

	if (count > TMPBUFSIZE - 1)
		return -EINVAL;

	memset(tmpbuf, 0x0, TMPBUFSIZE);

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;

	spin_lock_irqsave(&gatorfs_lock, flags);
	*val = simple_strtoul(tmpbuf, NULL, 0);
	spin_unlock_irqrestore(&gatorfs_lock, flags);
	return 0;
}

static int gatorfs_u64_from_user(u64 *val, char const __user *buf, size_t count)
{
	char tmpbuf[TMPBUFSIZE];
	unsigned long flags;

	if (!count)
		return 0;

	if (count > TMPBUFSIZE - 1)
		return -EINVAL;

	memset(tmpbuf, 0x0, TMPBUFSIZE);

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;

	spin_lock_irqsave(&gatorfs_lock, flags);
	*val = simple_strtoull(tmpbuf, NULL, 0);
	spin_unlock_irqrestore(&gatorfs_lock, flags);
	return 0;
}

static ssize_t ulong_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	unsigned long *val = file->private_data;

	return gatorfs_ulong_to_user(*val, buf, count, offset);
}

static ssize_t u64_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	u64 *val = file->private_data;

	return gatorfs_u64_to_user(*val, buf, count, offset);
}

static ssize_t ulong_write_file(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long *value = file->private_data;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_ulong_from_user(value, buf, count);

	if (retval)
		return retval;
	return count;
}

static ssize_t u64_write_file(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	u64 *value = file->private_data;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_u64_from_user(value, buf, count);

	if (retval)
		return retval;
	return count;
}

static int default_open(struct inode *inode, struct file *filp)
{
	if (inode->i_private)
		filp->private_data = inode->i_private;
	return 0;
}

static const struct file_operations ulong_fops = {
	.read = ulong_read_file,
	.write = ulong_write_file,
	.open = default_open,
};

static const struct file_operations u64_fops = {
	.read = u64_read_file,
	.write = u64_write_file,
	.open = default_open,
};

static const struct file_operations ulong_ro_fops = {
	.read = ulong_read_file,
	.open = default_open,
};

static const struct file_operations u64_ro_fops = {
	.read = u64_read_file,
	.open = default_open,
};

static struct dentry *__gatorfs_create_file(struct super_block *sb,
					    struct dentry *root,
					    char const *name,
					    const struct file_operations *fops,
					    int perm)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return NULL;
	inode = gatorfs_get_inode(sb, S_IFREG | perm);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_fop = fops;
	d_add(dentry, inode);
	return dentry;
}

int gatorfs_create_ulong(struct super_block *sb, struct dentry *root,
			 char const *name, unsigned long *val)
{
	struct dentry *d = __gatorfs_create_file(sb, root, name,
						 &ulong_fops, 0644);
	if (!d)
		return -EFAULT;

	d->d_inode->i_private = val;
	return 0;
}

static int gatorfs_create_u64(struct super_block *sb, struct dentry *root,
			      char const *name, u64 *val)
{
	struct dentry *d = __gatorfs_create_file(sb, root, name,
						 &u64_fops, 0644);
	if (!d)
		return -EFAULT;

	d->d_inode->i_private = val;
	return 0;
}

int gatorfs_create_ro_ulong(struct super_block *sb, struct dentry *root,
			    char const *name, unsigned long *val)
{
	struct dentry *d = __gatorfs_create_file(sb, root, name,
						 &ulong_ro_fops, 0444);
	if (!d)
		return -EFAULT;

	d->d_inode->i_private = val;
	return 0;
}

static int gatorfs_create_ro_u64(struct super_block *sb, struct dentry *root,
				 char const *name, u64 *val)
{
	struct dentry *d =
	    __gatorfs_create_file(sb, root, name, &u64_ro_fops, 0444);
	if (!d)
		return -EFAULT;

	d->d_inode->i_private = val;
	return 0;
}

static ssize_t atomic_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	atomic_t *val = file->private_data;

	return gatorfs_ulong_to_user(atomic_read(val), buf, count, offset);
}

static const struct file_operations atomic_ro_fops = {
	.read = atomic_read_file,
	.open = default_open,
};

static int gatorfs_create_file(struct super_block *sb, struct dentry *root,
			       char const *name, const struct file_operations *fops)
{
	if (!__gatorfs_create_file(sb, root, name, fops, 0644))
		return -EFAULT;
	return 0;
}

static int gatorfs_create_file_perm(struct super_block *sb, struct dentry *root,
				    char const *name,
				    const struct file_operations *fops, int perm)
{
	if (!__gatorfs_create_file(sb, root, name, fops, perm))
		return -EFAULT;
	return 0;
}

struct dentry *gatorfs_mkdir(struct super_block *sb,
			     struct dentry *root, char const *name)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return NULL;
	inode = gatorfs_get_inode(sb, S_IFDIR | 0755);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	d_add(dentry, inode);
	return dentry;
}

static int gatorfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct dentry *root_dentry;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = gatorfs_MAGIC;
	sb->s_op = &s_ops;
	sb->s_time_gran = 1;

	root_inode = gatorfs_get_inode(sb, S_IFDIR | 0755);
	if (!root_inode)
		return -ENOMEM;
	root_inode->i_op = &simple_dir_inode_operations;
	root_inode->i_fop = &simple_dir_operations;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	root_dentry = d_alloc_root(root_inode);
#else
	root_dentry = d_make_root(root_inode);
#endif

	if (!root_dentry) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
		iput(root_inode);
#endif
		return -ENOMEM;
	}

	sb->s_root = root_dentry;

	gator_op_create_files(sb, root_dentry);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
static int gatorfs_get_sb(struct file_system_type *fs_type,
			  int flags, const char *dev_name, void *data,
			  struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, gatorfs_fill_super, mnt);
}
#else
static struct dentry *gatorfs_mount(struct file_system_type *fs_type,
				    int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, gatorfs_fill_super);
}
#endif

static struct file_system_type gatorfs_type = {
	.owner = THIS_MODULE,
	.name = "gatorfs",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	.get_sb = gatorfs_get_sb,
#else
	.mount = gatorfs_mount,
#endif

	.kill_sb = kill_litter_super,
};

static int __init gatorfs_register(void)
{
	return register_filesystem(&gatorfs_type);
}

static void gatorfs_unregister(void)
{
	unregister_filesystem(&gatorfs_type);
}
