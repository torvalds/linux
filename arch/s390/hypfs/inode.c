// SPDX-License-Identifier: GPL-1.0+
/*
 *    Hypervisor filesystem for Linux on s390.
 *
 *    Copyright IBM Corp. 2006, 2008
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#define KMSG_COMPONENT "hypfs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/erryes.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/seq_file.h>
#include <linux/uio.h>
#include <asm/ebcdic.h>
#include "hypfs.h"

#define HYPFS_MAGIC 0x687970	/* ASCII 'hyp' */
#define TMP_SIZE 64		/* size of temporary buffers */

static struct dentry *hypfs_create_update_file(struct dentry *dir);

struct hypfs_sb_info {
	kuid_t uid;			/* uid used for files and dirs */
	kgid_t gid;			/* gid used for files and dirs */
	struct dentry *update_file;	/* file to trigger update */
	time64_t last_update;		/* last update, CLOCK_MONOTONIC time */
	struct mutex lock;		/* lock to protect update process */
};

static const struct file_operations hypfs_file_ops;
static struct file_system_type hypfs_type;
static const struct super_operations hypfs_s_ops;

/* start of list of all dentries, which have to be deleted on update */
static struct dentry *hypfs_last_dentry;

static void hypfs_update_update(struct super_block *sb)
{
	struct hypfs_sb_info *sb_info = sb->s_fs_info;
	struct iyesde *iyesde = d_iyesde(sb_info->update_file);

	sb_info->last_update = ktime_get_seconds();
	iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
}

/* directory tree removal functions */

static void hypfs_add_dentry(struct dentry *dentry)
{
	dentry->d_fsdata = hypfs_last_dentry;
	hypfs_last_dentry = dentry;
}

static void hypfs_remove(struct dentry *dentry)
{
	struct dentry *parent;

	parent = dentry->d_parent;
	iyesde_lock(d_iyesde(parent));
	if (simple_positive(dentry)) {
		if (d_is_dir(dentry))
			simple_rmdir(d_iyesde(parent), dentry);
		else
			simple_unlink(d_iyesde(parent), dentry);
	}
	d_drop(dentry);
	dput(dentry);
	iyesde_unlock(d_iyesde(parent));
}

static void hypfs_delete_tree(struct dentry *root)
{
	while (hypfs_last_dentry) {
		struct dentry *next_dentry;
		next_dentry = hypfs_last_dentry->d_fsdata;
		hypfs_remove(hypfs_last_dentry);
		hypfs_last_dentry = next_dentry;
	}
}

static struct iyesde *hypfs_make_iyesde(struct super_block *sb, umode_t mode)
{
	struct iyesde *ret = new_iyesde(sb);

	if (ret) {
		struct hypfs_sb_info *hypfs_info = sb->s_fs_info;
		ret->i_iyes = get_next_iyes();
		ret->i_mode = mode;
		ret->i_uid = hypfs_info->uid;
		ret->i_gid = hypfs_info->gid;
		ret->i_atime = ret->i_mtime = ret->i_ctime = current_time(ret);
		if (S_ISDIR(mode))
			set_nlink(ret, 2);
	}
	return ret;
}

static void hypfs_evict_iyesde(struct iyesde *iyesde)
{
	clear_iyesde(iyesde);
	kfree(iyesde->i_private);
}

static int hypfs_open(struct iyesde *iyesde, struct file *filp)
{
	char *data = file_iyesde(filp)->i_private;
	struct hypfs_sb_info *fs_info;

	if (filp->f_mode & FMODE_WRITE) {
		if (!(iyesde->i_mode & S_IWUGO))
			return -EACCES;
	}
	if (filp->f_mode & FMODE_READ) {
		if (!(iyesde->i_mode & S_IRUGO))
			return -EACCES;
	}

	fs_info = iyesde->i_sb->s_fs_info;
	if(data) {
		mutex_lock(&fs_info->lock);
		filp->private_data = kstrdup(data, GFP_KERNEL);
		if (!filp->private_data) {
			mutex_unlock(&fs_info->lock);
			return -ENOMEM;
		}
		mutex_unlock(&fs_info->lock);
	}
	return yesnseekable_open(iyesde, filp);
}

static ssize_t hypfs_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	char *data = file->private_data;
	size_t available = strlen(data);
	loff_t pos = iocb->ki_pos;
	size_t count;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !iov_iter_count(to))
		return 0;
	count = copy_to_iter(data + pos, available - pos, to);
	if (!count)
		return -EFAULT;
	iocb->ki_pos = pos + count;
	file_accessed(file);
	return count;
}

static ssize_t hypfs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	int rc;
	struct super_block *sb = file_iyesde(iocb->ki_filp)->i_sb;
	struct hypfs_sb_info *fs_info = sb->s_fs_info;
	size_t count = iov_iter_count(from);

	/*
	 * Currently we only allow one update per second for two reasons:
	 * 1. diag 204 is VERY expensive
	 * 2. If several processes do updates in parallel and then read the
	 *    hypfs data, the likelihood of collisions is reduced, if we restrict
	 *    the minimum update interval. A collision occurs, if during the
	 *    data gathering of one process ayesther process triggers an update
	 *    If the first process wants to ensure consistent data, it has
	 *    to restart data collection in this case.
	 */
	mutex_lock(&fs_info->lock);
	if (fs_info->last_update == ktime_get_seconds()) {
		rc = -EBUSY;
		goto out;
	}
	hypfs_delete_tree(sb->s_root);
	if (MACHINE_IS_VM)
		rc = hypfs_vm_create_files(sb->s_root);
	else
		rc = hypfs_diag_create_files(sb->s_root);
	if (rc) {
		pr_err("Updating the hypfs tree failed\n");
		hypfs_delete_tree(sb->s_root);
		goto out;
	}
	hypfs_update_update(sb);
	rc = count;
	iov_iter_advance(from, count);
out:
	mutex_unlock(&fs_info->lock);
	return rc;
}

static int hypfs_release(struct iyesde *iyesde, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

enum { Opt_uid, Opt_gid, };

static const struct fs_parameter_spec hypfs_param_specs[] = {
	fsparam_u32("gid", Opt_gid),
	fsparam_u32("uid", Opt_uid),
	{}
};

static const struct fs_parameter_description hypfs_fs_parameters = {
	.name		= "hypfs",
	.specs		= hypfs_param_specs,
};

static int hypfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct hypfs_sb_info *hypfs_info = fc->s_fs_info;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt;

	opt = fs_parse(fc, &hypfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(uid))
			return invalf(fc, "Unkyeswn uid");
		hypfs_info->uid = uid;
		break;
	case Opt_gid:
		gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(gid))
			return invalf(fc, "Unkyeswn gid");
		hypfs_info->gid = gid;
		break;
	}
	return 0;
}

static int hypfs_show_options(struct seq_file *s, struct dentry *root)
{
	struct hypfs_sb_info *hypfs_info = root->d_sb->s_fs_info;

	seq_printf(s, ",uid=%u", from_kuid_munged(&init_user_ns, hypfs_info->uid));
	seq_printf(s, ",gid=%u", from_kgid_munged(&init_user_ns, hypfs_info->gid));
	return 0;
}

static int hypfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct hypfs_sb_info *sbi = sb->s_fs_info;
	struct iyesde *root_iyesde;
	struct dentry *root_dentry, *update_file;
	int rc;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = HYPFS_MAGIC;
	sb->s_op = &hypfs_s_ops;

	root_iyesde = hypfs_make_iyesde(sb, S_IFDIR | 0755);
	if (!root_iyesde)
		return -ENOMEM;
	root_iyesde->i_op = &simple_dir_iyesde_operations;
	root_iyesde->i_fop = &simple_dir_operations;
	sb->s_root = root_dentry = d_make_root(root_iyesde);
	if (!root_dentry)
		return -ENOMEM;
	if (MACHINE_IS_VM)
		rc = hypfs_vm_create_files(root_dentry);
	else
		rc = hypfs_diag_create_files(root_dentry);
	if (rc)
		return rc;
	update_file = hypfs_create_update_file(root_dentry);
	if (IS_ERR(update_file))
		return PTR_ERR(update_file);
	sbi->update_file = update_file;
	hypfs_update_update(sb);
	pr_info("Hypervisor filesystem mounted\n");
	return 0;
}

static int hypfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, hypfs_fill_super);
}

static void hypfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations hypfs_context_ops = {
	.free		= hypfs_free_fc,
	.parse_param	= hypfs_parse_param,
	.get_tree	= hypfs_get_tree,
};

static int hypfs_init_fs_context(struct fs_context *fc)
{
	struct hypfs_sb_info *sbi;

	sbi = kzalloc(sizeof(struct hypfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	mutex_init(&sbi->lock);
	sbi->uid = current_uid();
	sbi->gid = current_gid();

	fc->s_fs_info = sbi;
	fc->ops = &hypfs_context_ops;
	return 0;
}

static void hypfs_kill_super(struct super_block *sb)
{
	struct hypfs_sb_info *sb_info = sb->s_fs_info;

	if (sb->s_root)
		hypfs_delete_tree(sb->s_root);
	if (sb_info && sb_info->update_file)
		hypfs_remove(sb_info->update_file);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	kill_litter_super(sb);
}

static struct dentry *hypfs_create_file(struct dentry *parent, const char *name,
					char *data, umode_t mode)
{
	struct dentry *dentry;
	struct iyesde *iyesde;

	iyesde_lock(d_iyesde(parent));
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		dentry = ERR_PTR(-ENOMEM);
		goto fail;
	}
	iyesde = hypfs_make_iyesde(parent->d_sb, mode);
	if (!iyesde) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto fail;
	}
	if (S_ISREG(mode)) {
		iyesde->i_fop = &hypfs_file_ops;
		if (data)
			iyesde->i_size = strlen(data);
		else
			iyesde->i_size = 0;
	} else if (S_ISDIR(mode)) {
		iyesde->i_op = &simple_dir_iyesde_operations;
		iyesde->i_fop = &simple_dir_operations;
		inc_nlink(d_iyesde(parent));
	} else
		BUG();
	iyesde->i_private = data;
	d_instantiate(dentry, iyesde);
	dget(dentry);
fail:
	iyesde_unlock(d_iyesde(parent));
	return dentry;
}

struct dentry *hypfs_mkdir(struct dentry *parent, const char *name)
{
	struct dentry *dentry;

	dentry = hypfs_create_file(parent, name, NULL, S_IFDIR | DIR_MODE);
	if (IS_ERR(dentry))
		return dentry;
	hypfs_add_dentry(dentry);
	return dentry;
}

static struct dentry *hypfs_create_update_file(struct dentry *dir)
{
	struct dentry *dentry;

	dentry = hypfs_create_file(dir, "update", NULL,
				   S_IFREG | UPDATE_FILE_MODE);
	/*
	 * We do yest put the update file on the 'delete' list with
	 * hypfs_add_dentry(), since it should yest be removed when the tree
	 * is updated.
	 */
	return dentry;
}

struct dentry *hypfs_create_u64(struct dentry *dir,
				const char *name, __u64 value)
{
	char *buffer;
	char tmp[TMP_SIZE];
	struct dentry *dentry;

	snprintf(tmp, TMP_SIZE, "%llu\n", (unsigned long long int)value);
	buffer = kstrdup(tmp, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	dentry =
	    hypfs_create_file(dir, name, buffer, S_IFREG | REG_FILE_MODE);
	if (IS_ERR(dentry)) {
		kfree(buffer);
		return ERR_PTR(-ENOMEM);
	}
	hypfs_add_dentry(dentry);
	return dentry;
}

struct dentry *hypfs_create_str(struct dentry *dir,
				const char *name, char *string)
{
	char *buffer;
	struct dentry *dentry;

	buffer = kmalloc(strlen(string) + 2, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	sprintf(buffer, "%s\n", string);
	dentry =
	    hypfs_create_file(dir, name, buffer, S_IFREG | REG_FILE_MODE);
	if (IS_ERR(dentry)) {
		kfree(buffer);
		return ERR_PTR(-ENOMEM);
	}
	hypfs_add_dentry(dentry);
	return dentry;
}

static const struct file_operations hypfs_file_ops = {
	.open		= hypfs_open,
	.release	= hypfs_release,
	.read_iter	= hypfs_read_iter,
	.write_iter	= hypfs_write_iter,
	.llseek		= yes_llseek,
};

static struct file_system_type hypfs_type = {
	.owner		= THIS_MODULE,
	.name		= "s390_hypfs",
	.init_fs_context = hypfs_init_fs_context,
	.parameters	= &hypfs_fs_parameters,
	.kill_sb	= hypfs_kill_super
};

static const struct super_operations hypfs_s_ops = {
	.statfs		= simple_statfs,
	.evict_iyesde	= hypfs_evict_iyesde,
	.show_options	= hypfs_show_options,
};

static int __init hypfs_init(void)
{
	int rc;

	hypfs_dbfs_init();

	if (hypfs_diag_init()) {
		rc = -ENODATA;
		goto fail_dbfs_exit;
	}
	if (hypfs_vm_init()) {
		rc = -ENODATA;
		goto fail_hypfs_diag_exit;
	}
	hypfs_sprp_init();
	if (hypfs_diag0c_init()) {
		rc = -ENODATA;
		goto fail_hypfs_sprp_exit;
	}
	rc = sysfs_create_mount_point(hypervisor_kobj, "s390");
	if (rc)
		goto fail_hypfs_diag0c_exit;
	rc = register_filesystem(&hypfs_type);
	if (rc)
		goto fail_filesystem;
	return 0;

fail_filesystem:
	sysfs_remove_mount_point(hypervisor_kobj, "s390");
fail_hypfs_diag0c_exit:
	hypfs_diag0c_exit();
fail_hypfs_sprp_exit:
	hypfs_sprp_exit();
	hypfs_vm_exit();
fail_hypfs_diag_exit:
	hypfs_diag_exit();
fail_dbfs_exit:
	hypfs_dbfs_exit();
	pr_err("Initialization of hypfs failed with rc=%i\n", rc);
	return rc;
}
device_initcall(hypfs_init)
