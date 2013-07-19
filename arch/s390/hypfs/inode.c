/*
 *    Hypervisor filesystem for Linux on s390.
 *
 *    Copyright IBM Corp. 2006, 2008
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#define KMSG_COMPONENT "hypfs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/time.h>
#include <linux/parser.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/aio.h>
#include <asm/ebcdic.h>
#include "hypfs.h"

#define HYPFS_MAGIC 0x687970	/* ASCII 'hyp' */
#define TMP_SIZE 64		/* size of temporary buffers */

static struct dentry *hypfs_create_update_file(struct dentry *dir);

struct hypfs_sb_info {
	kuid_t uid;			/* uid used for files and dirs */
	kgid_t gid;			/* gid used for files and dirs */
	struct dentry *update_file;	/* file to trigger update */
	time_t last_update;		/* last update time in secs since 1970 */
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
	struct inode *inode = sb_info->update_file->d_inode;

	sb_info->last_update = get_seconds();
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

/* directory tree removal functions */

static void hypfs_add_dentry(struct dentry *dentry)
{
	dentry->d_fsdata = hypfs_last_dentry;
	hypfs_last_dentry = dentry;
}

static inline int hypfs_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static void hypfs_remove(struct dentry *dentry)
{
	struct dentry *parent;

	parent = dentry->d_parent;
	mutex_lock(&parent->d_inode->i_mutex);
	if (hypfs_positive(dentry)) {
		if (S_ISDIR(dentry->d_inode->i_mode))
			simple_rmdir(parent->d_inode, dentry);
		else
			simple_unlink(parent->d_inode, dentry);
	}
	d_delete(dentry);
	dput(dentry);
	mutex_unlock(&parent->d_inode->i_mutex);
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

static struct inode *hypfs_make_inode(struct super_block *sb, umode_t mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		struct hypfs_sb_info *hypfs_info = sb->s_fs_info;
		ret->i_ino = get_next_ino();
		ret->i_mode = mode;
		ret->i_uid = hypfs_info->uid;
		ret->i_gid = hypfs_info->gid;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
		if (S_ISDIR(mode))
			set_nlink(ret, 2);
	}
	return ret;
}

static void hypfs_evict_inode(struct inode *inode)
{
	clear_inode(inode);
	kfree(inode->i_private);
}

static int hypfs_open(struct inode *inode, struct file *filp)
{
	char *data = file_inode(filp)->i_private;
	struct hypfs_sb_info *fs_info;

	if (filp->f_mode & FMODE_WRITE) {
		if (!(inode->i_mode & S_IWUGO))
			return -EACCES;
	}
	if (filp->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO))
			return -EACCES;
	}

	fs_info = inode->i_sb->s_fs_info;
	if(data) {
		mutex_lock(&fs_info->lock);
		filp->private_data = kstrdup(data, GFP_KERNEL);
		if (!filp->private_data) {
			mutex_unlock(&fs_info->lock);
			return -ENOMEM;
		}
		mutex_unlock(&fs_info->lock);
	}
	return nonseekable_open(inode, filp);
}

static ssize_t hypfs_aio_read(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t offset)
{
	char *data;
	ssize_t ret;
	struct file *filp = iocb->ki_filp;
	/* XXX: temporary */
	char __user *buf = iov[0].iov_base;
	size_t count = iov[0].iov_len;

	if (nr_segs != 1)
		return -EINVAL;

	data = filp->private_data;
	ret = simple_read_from_buffer(buf, count, &offset, data, strlen(data));
	if (ret <= 0)
		return ret;

	iocb->ki_pos += ret;
	file_accessed(filp);

	return ret;
}
static ssize_t hypfs_aio_write(struct kiocb *iocb, const struct iovec *iov,
			      unsigned long nr_segs, loff_t offset)
{
	int rc;
	struct super_block *sb = file_inode(iocb->ki_filp)->i_sb;
	struct hypfs_sb_info *fs_info = sb->s_fs_info;
	size_t count = iov_length(iov, nr_segs);

	/*
	 * Currently we only allow one update per second for two reasons:
	 * 1. diag 204 is VERY expensive
	 * 2. If several processes do updates in parallel and then read the
	 *    hypfs data, the likelihood of collisions is reduced, if we restrict
	 *    the minimum update interval. A collision occurs, if during the
	 *    data gathering of one process another process triggers an update
	 *    If the first process wants to ensure consistent data, it has
	 *    to restart data collection in this case.
	 */
	mutex_lock(&fs_info->lock);
	if (fs_info->last_update == get_seconds()) {
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
out:
	mutex_unlock(&fs_info->lock);
	return rc;
}

static int hypfs_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

enum { opt_uid, opt_gid, opt_err };

static const match_table_t hypfs_tokens = {
	{opt_uid, "uid=%u"},
	{opt_gid, "gid=%u"},
	{opt_err, NULL}
};

static int hypfs_parse_options(char *options, struct super_block *sb)
{
	char *str;
	substring_t args[MAX_OPT_ARGS];
	kuid_t uid;
	kgid_t gid;

	if (!options)
		return 0;
	while ((str = strsep(&options, ",")) != NULL) {
		int token, option;
		struct hypfs_sb_info *hypfs_info = sb->s_fs_info;

		if (!*str)
			continue;
		token = match_token(str, hypfs_tokens, args);
		switch (token) {
		case opt_uid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid))
				return -EINVAL;
			hypfs_info->uid = uid;
			break;
		case opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid))
				return -EINVAL;
			hypfs_info->gid = gid;
			break;
		case opt_err:
		default:
			pr_err("%s is not a valid mount option\n", str);
			return -EINVAL;
		}
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

static int hypfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct dentry *root_dentry;
	int rc = 0;
	struct hypfs_sb_info *sbi;

	sbi = kzalloc(sizeof(struct hypfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	mutex_init(&sbi->lock);
	sbi->uid = current_uid();
	sbi->gid = current_gid();
	sb->s_fs_info = sbi;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = HYPFS_MAGIC;
	sb->s_op = &hypfs_s_ops;
	if (hypfs_parse_options(data, sb))
		return -EINVAL;
	root_inode = hypfs_make_inode(sb, S_IFDIR | 0755);
	if (!root_inode)
		return -ENOMEM;
	root_inode->i_op = &simple_dir_inode_operations;
	root_inode->i_fop = &simple_dir_operations;
	sb->s_root = root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		return -ENOMEM;
	if (MACHINE_IS_VM)
		rc = hypfs_vm_create_files(root_dentry);
	else
		rc = hypfs_diag_create_files(root_dentry);
	if (rc)
		return rc;
	sbi->update_file = hypfs_create_update_file(root_dentry);
	if (IS_ERR(sbi->update_file))
		return PTR_ERR(sbi->update_file);
	hypfs_update_update(sb);
	pr_info("Hypervisor filesystem mounted\n");
	return 0;
}

static struct dentry *hypfs_mount(struct file_system_type *fst, int flags,
			const char *devname, void *data)
{
	return mount_single(fst, flags, data, hypfs_fill_super);
}

static void hypfs_kill_super(struct super_block *sb)
{
	struct hypfs_sb_info *sb_info = sb->s_fs_info;

	if (sb->s_root)
		hypfs_delete_tree(sb->s_root);
	if (sb_info->update_file)
		hypfs_remove(sb_info->update_file);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	kill_litter_super(sb);
}

static struct dentry *hypfs_create_file(struct dentry *parent, const char *name,
					char *data, umode_t mode)
{
	struct dentry *dentry;
	struct inode *inode;

	mutex_lock(&parent->d_inode->i_mutex);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		dentry = ERR_PTR(-ENOMEM);
		goto fail;
	}
	inode = hypfs_make_inode(parent->d_sb, mode);
	if (!inode) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto fail;
	}
	if (S_ISREG(mode)) {
		inode->i_fop = &hypfs_file_ops;
		if (data)
			inode->i_size = strlen(data);
		else
			inode->i_size = 0;
	} else if (S_ISDIR(mode)) {
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(parent->d_inode);
	} else
		BUG();
	inode->i_private = data;
	d_instantiate(dentry, inode);
	dget(dentry);
fail:
	mutex_unlock(&parent->d_inode->i_mutex);
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
	 * We do not put the update file on the 'delete' list with
	 * hypfs_add_dentry(), since it should not be removed when the tree
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
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= hypfs_aio_read,
	.aio_write	= hypfs_aio_write,
	.llseek		= no_llseek,
};

static struct file_system_type hypfs_type = {
	.owner		= THIS_MODULE,
	.name		= "s390_hypfs",
	.mount		= hypfs_mount,
	.kill_sb	= hypfs_kill_super
};
MODULE_ALIAS_FS("s390_hypfs");

static const struct super_operations hypfs_s_ops = {
	.statfs		= simple_statfs,
	.evict_inode	= hypfs_evict_inode,
	.show_options	= hypfs_show_options,
};

static struct kobject *s390_kobj;

static int __init hypfs_init(void)
{
	int rc;

	rc = hypfs_dbfs_init();
	if (rc)
		return rc;
	if (hypfs_diag_init()) {
		rc = -ENODATA;
		goto fail_dbfs_exit;
	}
	if (hypfs_vm_init()) {
		rc = -ENODATA;
		goto fail_hypfs_diag_exit;
	}
	s390_kobj = kobject_create_and_add("s390", hypervisor_kobj);
	if (!s390_kobj) {
		rc = -ENOMEM;
		goto fail_hypfs_vm_exit;
	}
	rc = register_filesystem(&hypfs_type);
	if (rc)
		goto fail_filesystem;
	return 0;

fail_filesystem:
	kobject_put(s390_kobj);
fail_hypfs_vm_exit:
	hypfs_vm_exit();
fail_hypfs_diag_exit:
	hypfs_diag_exit();
fail_dbfs_exit:
	hypfs_dbfs_exit();
	pr_err("Initialization of hypfs failed with rc=%i\n", rc);
	return rc;
}

static void __exit hypfs_exit(void)
{
	hypfs_diag_exit();
	hypfs_vm_exit();
	hypfs_dbfs_exit();
	unregister_filesystem(&hypfs_type);
	kobject_put(s390_kobj);
}

module_init(hypfs_init)
module_exit(hypfs_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Holzheu <holzheu@de.ibm.com>");
MODULE_DESCRIPTION("s390 Hypervisor Filesystem");
