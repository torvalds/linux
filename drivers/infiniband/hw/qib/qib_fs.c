/*
 * Copyright (c) 2012 Intel Corporation. All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
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

#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>

#include "qib.h"

#define QIBFS_MAGIC 0x726a77

static struct super_block *qib_super;

#define private2dd(file) (file_inode(file)->i_private)

static int qibfs_mknod(struct inode *dir, struct dentry *dentry,
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
	inode->i_uid = GLOBAL_ROOT_UID;
	inode->i_gid = GLOBAL_ROOT_GID;
	inode->i_blocks = 0;
	inode->i_atime = current_time(inode);
	inode->i_mtime = inode->i_atime;
	inode->i_ctime = inode->i_atime;
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

	inode_lock(d_inode(parent));
	*dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(*dentry))
		error = qibfs_mknod(d_inode(parent), *dentry,
				    mode, fops, data);
	else
		error = PTR_ERR(*dentry);
	inode_unlock(d_inode(parent));

	return error;
}

static ssize_t driver_stats_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	qib_stats.sps_ints = qib_sps_ints();
	return simple_read_from_buffer(buf, count, ppos, &qib_stats,
				       sizeof(qib_stats));
}

/*
 * driver stats field names, one line per stat, single string.  Used by
 * programs like ipathstats to print the stats in a way which works for
 * different versions of drivers, without changing program source.
 * if qlogic_ib_stats changes, this needs to change.  Names need to be
 * 12 chars or less (w/o newline), for proper display by ipathstats utility.
 */
static const char qib_statnames[] =
	"KernIntr\n"
	"ErrorIntr\n"
	"Tx_Errs\n"
	"Rcv_Errs\n"
	"H/W_Errs\n"
	"NoPIOBufs\n"
	"CtxtsOpen\n"
	"RcvLen_Errs\n"
	"EgrBufFull\n"
	"EgrHdrFull\n"
	;

static ssize_t driver_names_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, qib_statnames,
		sizeof(qib_statnames) - 1); /* no null */
}

static const struct file_operations driver_ops[] = {
	{ .read = driver_stats_read, .llseek = generic_file_llseek, },
	{ .read = driver_names_read, .llseek = generic_file_llseek, },
};

/* read the per-device counters */
static ssize_t dev_counters_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	u64 *counters;
	size_t avail;
	struct qib_devdata *dd = private2dd(file);

	avail = dd->f_read_cntrs(dd, *ppos, NULL, &counters);
	return simple_read_from_buffer(buf, count, ppos, counters, avail);
}

/* read the per-device counters */
static ssize_t dev_names_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	char *names;
	size_t avail;
	struct qib_devdata *dd = private2dd(file);

	avail = dd->f_read_cntrs(dd, *ppos, &names, NULL);
	return simple_read_from_buffer(buf, count, ppos, names, avail);
}

static const struct file_operations cntr_ops[] = {
	{ .read = dev_counters_read, .llseek = generic_file_llseek, },
	{ .read = dev_names_read, .llseek = generic_file_llseek, },
};

/*
 * Could use file_inode(file)->i_ino to figure out which file,
 * instead of separate routine for each, but for now, this works...
 */

/* read the per-port names (same for each port) */
static ssize_t portnames_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	char *names;
	size_t avail;
	struct qib_devdata *dd = private2dd(file);

	avail = dd->f_read_portcntrs(dd, *ppos, 0, &names, NULL);
	return simple_read_from_buffer(buf, count, ppos, names, avail);
}

/* read the per-port counters for port 1 (pidx 0) */
static ssize_t portcntrs_1_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	u64 *counters;
	size_t avail;
	struct qib_devdata *dd = private2dd(file);

	avail = dd->f_read_portcntrs(dd, *ppos, 0, NULL, &counters);
	return simple_read_from_buffer(buf, count, ppos, counters, avail);
}

/* read the per-port counters for port 2 (pidx 1) */
static ssize_t portcntrs_2_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	u64 *counters;
	size_t avail;
	struct qib_devdata *dd = private2dd(file);

	avail = dd->f_read_portcntrs(dd, *ppos, 1, NULL, &counters);
	return simple_read_from_buffer(buf, count, ppos, counters, avail);
}

static const struct file_operations portcntr_ops[] = {
	{ .read = portnames_read, .llseek = generic_file_llseek, },
	{ .read = portcntrs_1_read, .llseek = generic_file_llseek, },
	{ .read = portcntrs_2_read, .llseek = generic_file_llseek, },
};

/*
 * read the per-port QSFP data for port 1 (pidx 0)
 */
static ssize_t qsfp_1_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct qib_devdata *dd = private2dd(file);
	char *tmp;
	int ret;

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = qib_qsfp_dump(dd->pport, tmp, PAGE_SIZE);
	if (ret > 0)
		ret = simple_read_from_buffer(buf, count, ppos, tmp, ret);
	kfree(tmp);
	return ret;
}

/*
 * read the per-port QSFP data for port 2 (pidx 1)
 */
static ssize_t qsfp_2_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct qib_devdata *dd = private2dd(file);
	char *tmp;
	int ret;

	if (dd->num_pports < 2)
		return -ENODEV;

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = qib_qsfp_dump(dd->pport + 1, tmp, PAGE_SIZE);
	if (ret > 0)
		ret = simple_read_from_buffer(buf, count, ppos, tmp, ret);
	kfree(tmp);
	return ret;
}

static const struct file_operations qsfp_ops[] = {
	{ .read = qsfp_1_read, .llseek = generic_file_llseek, },
	{ .read = qsfp_2_read, .llseek = generic_file_llseek, },
};

static ssize_t flash_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct qib_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if (pos < 0) {
		ret = -EINVAL;
		goto bail;
	}

	if (pos >= sizeof(struct qib_flash)) {
		ret = 0;
		goto bail;
	}

	if (count > sizeof(struct qib_flash) - pos)
		count = sizeof(struct qib_flash) - pos;

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp) {
		ret = -ENOMEM;
		goto bail;
	}

	dd = private2dd(file);
	if (qib_eeprom_read(dd, pos, tmp, count)) {
		qib_dev_err(dd, "failed to read from flash\n");
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
	struct qib_devdata *dd;
	ssize_t ret;
	loff_t pos;
	char *tmp;

	pos = *ppos;

	if (pos != 0 || count != sizeof(struct qib_flash))
		return -EINVAL;

	tmp = memdup_user(buf, count);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	dd = private2dd(file);
	if (qib_eeprom_write(dd, pos, tmp, count)) {
		ret = -ENXIO;
		qib_dev_err(dd, "failed to write to flash\n");
		goto bail_tmp;
	}

	*ppos = pos + count;
	ret = count;

bail_tmp:
	kfree(tmp);
	return ret;
}

static const struct file_operations flash_ops = {
	.read = flash_read,
	.write = flash_write,
	.llseek = default_llseek,
};

static int add_cntr_files(struct super_block *sb, struct qib_devdata *dd)
{
	struct dentry *dir, *tmp;
	char unit[10];
	int ret, i;

	/* create the per-unit directory */
	snprintf(unit, sizeof(unit), "%u", dd->unit);
	ret = create_file(unit, S_IFDIR|S_IRUGO|S_IXUGO, sb->s_root, &dir,
			  &simple_dir_operations, dd);
	if (ret) {
		pr_err("create_file(%s) failed: %d\n", unit, ret);
		goto bail;
	}

	/* create the files in the new directory */
	ret = create_file("counters", S_IFREG|S_IRUGO, dir, &tmp,
			  &cntr_ops[0], dd);
	if (ret) {
		pr_err("create_file(%s/counters) failed: %d\n",
		       unit, ret);
		goto bail;
	}
	ret = create_file("counter_names", S_IFREG|S_IRUGO, dir, &tmp,
			  &cntr_ops[1], dd);
	if (ret) {
		pr_err("create_file(%s/counter_names) failed: %d\n",
		       unit, ret);
		goto bail;
	}
	ret = create_file("portcounter_names", S_IFREG|S_IRUGO, dir, &tmp,
			  &portcntr_ops[0], dd);
	if (ret) {
		pr_err("create_file(%s/%s) failed: %d\n",
		       unit, "portcounter_names", ret);
		goto bail;
	}
	for (i = 1; i <= dd->num_pports; i++) {
		char fname[24];

		sprintf(fname, "port%dcounters", i);
		/* create the files in the new directory */
		ret = create_file(fname, S_IFREG|S_IRUGO, dir, &tmp,
				  &portcntr_ops[i], dd);
		if (ret) {
			pr_err("create_file(%s/%s) failed: %d\n",
				unit, fname, ret);
			goto bail;
		}
		if (!(dd->flags & QIB_HAS_QSFP))
			continue;
		sprintf(fname, "qsfp%d", i);
		ret = create_file(fname, S_IFREG|S_IRUGO, dir, &tmp,
				  &qsfp_ops[i - 1], dd);
		if (ret) {
			pr_err("create_file(%s/%s) failed: %d\n",
				unit, fname, ret);
			goto bail;
		}
	}

	ret = create_file("flash", S_IFREG|S_IWUSR|S_IRUGO, dir, &tmp,
			  &flash_ops, dd);
	if (ret)
		pr_err("create_file(%s/flash) failed: %d\n",
			unit, ret);
bail:
	return ret;
}

static int remove_device_files(struct super_block *sb,
			       struct qib_devdata *dd)
{
	struct dentry *dir;
	char unit[10];

	snprintf(unit, sizeof(unit), "%u", dd->unit);
	dir = lookup_one_len_unlocked(unit, sb->s_root, strlen(unit));

	if (IS_ERR(dir)) {
		pr_err("Lookup of %s failed\n", unit);
		return PTR_ERR(dir);
	}
	simple_recursive_removal(dir, NULL);
	return 0;
}

/*
 * This fills everything in when the fs is mounted, to handle umount/mount
 * after device init.  The direct add_cntr_files() call handles adding
 * them from the init code, when the fs is already mounted.
 */
static int qibfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct qib_devdata *dd;
	unsigned long index;
	int ret;

	static const struct tree_descr files[] = {
		[2] = {"driver_stats", &driver_ops[0], S_IRUGO},
		[3] = {"driver_stats_names", &driver_ops[1], S_IRUGO},
		{""},
	};

	ret = simple_fill_super(sb, QIBFS_MAGIC, files);
	if (ret) {
		pr_err("simple_fill_super failed: %d\n", ret);
		goto bail;
	}

	xa_for_each(&qib_dev_table, index, dd) {
		ret = add_cntr_files(sb, dd);
		if (ret)
			goto bail;
	}

bail:
	return ret;
}

static int qibfs_get_tree(struct fs_context *fc)
{
	int ret = get_tree_single(fc, qibfs_fill_super);
	if (ret == 0)
		qib_super = fc->root->d_sb;
	return ret;
}

static const struct fs_context_operations qibfs_context_ops = {
	.get_tree	= qibfs_get_tree,
};

static int qibfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &qibfs_context_ops;
	return 0;
}

static void qibfs_kill_super(struct super_block *s)
{
	kill_litter_super(s);
	qib_super = NULL;
}

int qibfs_add(struct qib_devdata *dd)
{
	int ret;

	/*
	 * On first unit initialized, qib_super will not yet exist
	 * because nobody has yet tried to mount the filesystem, so
	 * we can't consider that to be an error; if an error occurs
	 * during the mount, that will get a complaint, so this is OK.
	 * add_cntr_files() for all units is done at mount from
	 * qibfs_fill_super(), so one way or another, everything works.
	 */
	if (qib_super == NULL)
		ret = 0;
	else
		ret = add_cntr_files(qib_super, dd);
	return ret;
}

int qibfs_remove(struct qib_devdata *dd)
{
	int ret = 0;

	if (qib_super)
		ret = remove_device_files(qib_super, dd);

	return ret;
}

static struct file_system_type qibfs_fs_type = {
	.owner =        THIS_MODULE,
	.name =         "ipathfs",
	.init_fs_context = qibfs_init_fs_context,
	.kill_sb =      qibfs_kill_super,
};
MODULE_ALIAS_FS("ipathfs");

int __init qib_init_qibfs(void)
{
	return register_filesystem(&qibfs_fs_type);
}

int __exit qib_exit_qibfs(void)
{
	return unregister_filesystem(&qibfs_fs_type);
}
