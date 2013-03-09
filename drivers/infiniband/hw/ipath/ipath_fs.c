/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
 * Copyright (c) 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/slab.h>

#include "ipath_kernel.h"

#define IPATHFS_MAGIC 0x726a77

static struct super_block *ipath_super;

static int ipathfs_mknod(struct inode *dir, struct dentry *dentry,
			 umode_t mode, const struct file_operations *fops,
			 void *data)
{
	int error;
	struct inode *inode = new_inode(dir->i_sb);

	if (!inode) {
		error = -EPERM;
		goto bail;
	}

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_private = data;
	if (S_ISDIR(mode)) {
		inode->i_op = &simple_dir_inode_operations;
		inc_nlink(inode);
		inc_nlink(dir);
	}

	inode->i_fop = fops;

	d_instantiate(dentry, inode);
	error = 0;

bail:
	return error;
}

static int create_file(const char *name, umode_t mode,
		       struct dentry *parent, struct dentry **dentry,
		       const struct file_operations *fops, void *data)
{
	int error;

	*dentry = NULL;
	mutex_lock(&parent->d_inode->i_mutex);
	*dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(*dentry))
		error = ipathfs_mknod(parent->d_inode, *dentry,
				      mode, fops, data);
	else
		error = PTR_ERR(*dentry);
	mutex_unlock(&parent->d_inode->i_mutex);

	return error;
}

static ssize_t atomic_stats_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, &ipath_stats,
				       sizeof ipath_stats);
}

static const struct file_operations atomic_stats_ops = {
	.read = atomic_stats_read,
	.llseek = default_llseek,
};

static ssize_t atomic_counters_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct infinipath_counters counters;
	struct ipath_devdata *dd;

	dd = file_inode(file)->i_private;
	dd->ipath_f_read_counters(dd, &counters);

	return simple_read_from_buffer(buf, count, ppos, &counters,
				       sizeof counters);
}

static const struct file_operations atomic_counters_ops = {
	.read = atomic_counters_read,
	.llseek = default_llseek,
};

static ssize_t flash_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct ipath_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if ( pos < 0) {
		ret = -EINVAL;
		goto bail;
	}

	if (pos >= sizeof(struct ipath_flash)) {
		ret = 0;
		goto bail;
	}

	if (count > sizeof(struct ipath_flash) - pos)
		count = sizeof(struct ipath_flash) - pos;

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp) {
		ret = -ENOMEM;
		goto bail;
	}

	dd = file_inode(file)->i_private;
	if (ipath_eeprom_read(dd, pos, tmp, count)) {
		ipath_dev_err(dd, "failed to read from flash\n");
		ret = -ENXIO;
		goto bail_tmp;
	}

	if (copy_to_user(buf, tmp, count)) {
		ret = -EFAULT;
		goto bail_tmp;
	}

	*ppos = pos + count;
	ret = count;

bail_tmp:
	kfree(tmp);

bail:
	return ret;
}

static ssize_t flash_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct ipath_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if (pos != 0) {
		ret = -EINVAL;
		goto bail;
	}

	if (count != sizeof(struct ipath_flash)) {
		ret = -EINVAL;
		goto bail;
	}

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp) {
		ret = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(tmp, buf, count)) {
		ret = -EFAULT;
		goto bail_tmp;
	}

	dd = file_inode(file)->i_private;
	if (ipath_eeprom_write(dd, pos, tmp, count)) {
		ret = -ENXIO;
		ipath_dev_err(dd, "failed to write to flash\n");
		goto bail_tmp;
	}

	*ppos = pos + count;
	ret = count;

bail_tmp:
	kfree(tmp);

bail:
	return ret;
}

static const struct file_operations flash_ops = {
	.read = flash_read,
	.write = flash_write,
	.llseek = default_llseek,
};

static int create_device_files(struct super_block *sb,
			       struct ipath_devdata *dd)
{
	struct dentry *dir, *tmp;
	char unit[10];
	int ret;

	snprintf(unit, sizeof unit, "%02d", dd->ipath_unit);
	ret = create_file(unit, S_IFDIR|S_IRUGO|S_IXUGO, sb->s_root, &dir,
			  &simple_dir_operations, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s) failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("atomic_counters", S_IFREG|S_IRUGO, dir, &tmp,
			  &atomic_counters_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/atomic_counters) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

	ret = create_file("flash", S_IFREG|S_IWUSR|S_IRUGO, dir, &tmp,
			  &flash_ops, dd);
	if (ret) {
		printk(KERN_ERR "create_file(%s/flash) "
		       "failed: %d\n", unit, ret);
		goto bail;
	}

bail:
	return ret;
}

static int remove_file(struct dentry *parent, char *name)
{
	struct dentry *tmp;
	int ret;

	tmp = lookup_one_len(name, parent, strlen(name));

	if (IS_ERR(tmp)) {
		ret = PTR_ERR(tmp);
		goto bail;
	}

	spin_lock(&tmp->d_lock);
	if (!(d_unhashed(tmp) && tmp->d_inode)) {
		dget_dlock(tmp);
		__d_drop(tmp);
		spin_unlock(&tmp->d_lock);
		simple_unlink(parent->d_inode, tmp);
	} else
		spin_unlock(&tmp->d_lock);

	ret = 0;
bail:
	/*
	 * We don't expect clients to care about the return value, but
	 * it's there if they need it.
	 */
	return ret;
}

static int remove_device_files(struct super_block *sb,
			       struct ipath_devdata *dd)
{
	struct dentry *dir, *root;
	char unit[10];
	int ret;

	root = dget(sb->s_root);
	mutex_lock(&root->d_inode->i_mutex);
	snprintf(unit, sizeof unit, "%02d", dd->ipath_unit);
	dir = lookup_one_len(unit, root, strlen(unit));

	if (IS_ERR(dir)) {
		ret = PTR_ERR(dir);
		printk(KERN_ERR "Lookup of %s failed\n", unit);
		goto bail;
	}

	remove_file(dir, "flash");
	remove_file(dir, "atomic_counters");
	d_delete(dir);
	ret = simple_rmdir(root->d_inode, dir);

bail:
	mutex_unlock(&root->d_inode->i_mutex);
	dput(root);
	return ret;
}

static int ipathfs_fill_super(struct super_block *sb, void *data,
			      int silent)
{
	struct ipath_devdata *dd, *tmp;
	unsigned long flags;
	int ret;

	static struct tree_descr files[] = {
		[2] = {"atomic_stats", &atomic_stats_ops, S_IRUGO},
		{""},
	};

	ret = simple_fill_super(sb, IPATHFS_MAGIC, files);
	if (ret) {
		printk(KERN_ERR "simple_fill_super failed: %d\n", ret);
		goto bail;
	}

	spin_lock_irqsave(&ipath_devs_lock, flags);

	list_for_each_entry_safe(dd, tmp, &ipath_dev_list, ipath_list) {
		spin_unlock_irqrestore(&ipath_devs_lock, flags);
		ret = create_device_files(sb, dd);
		if (ret)
			goto bail;
		spin_lock_irqsave(&ipath_devs_lock, flags);
	}

	spin_unlock_irqrestore(&ipath_devs_lock, flags);

bail:
	return ret;
}

static struct dentry *ipathfs_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name, void *data)
{
	struct dentry *ret;
	ret = mount_single(fs_type, flags, data, ipathfs_fill_super);
	if (!IS_ERR(ret))
		ipath_super = ret->d_sb;
	return ret;
}

static void ipathfs_kill_super(struct super_block *s)
{
	kill_litter_super(s);
	ipath_super = NULL;
}

int ipathfs_add_device(struct ipath_devdata *dd)
{
	int ret;

	if (ipath_super == NULL) {
		ret = 0;
		goto bail;
	}

	ret = create_device_files(ipath_super, dd);

bail:
	return ret;
}

int ipathfs_remove_device(struct ipath_devdata *dd)
{
	int ret;

	if (ipath_super == NULL) {
		ret = 0;
		goto bail;
	}

	ret = remove_device_files(ipath_super, dd);

bail:
	return ret;
}

static struct file_system_type ipathfs_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"ipathfs",
	.mount =	ipathfs_mount,
	.kill_sb =	ipathfs_kill_super,
};

int __init ipath_init_ipathfs(void)
{
	return register_filesystem(&ipathfs_fs_type);
}

void __exit ipath_exit_ipathfs(void)
{
	unregister_filesystem(&ipathfs_fs_type);
}
