/*
 *  fs/hypfs/inode.c
 *    Hypervisor filesystem for Linux on s390.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/pagemap.h>
#include <linux/gfp.h>
#include <linux/time.h>
#include <linux/parser.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <asm/ebcdic.h>
#include "hypfs.h"
#include "hypfs_diag.h"

#define HYPFS_MAGIC 0x687970	/* ASCII 'hyp' */
#define TMP_SIZE 64		/* size of temporary buffers */

static struct dentry *hypfs_create_update_file(struct super_block *sb,
					       struct dentry *dir);

struct hypfs_sb_info {
	uid_t uid;			/* uid used for files and dirs */
	gid_t gid;			/* gid used for files and dirs */
	struct dentry *update_file;	/* file to trigger update */
	time_t last_update;		/* last update time in secs since 1970 */
	struct mutex lock;		/* lock to protect update process */
};

static struct file_operations hypfs_file_ops;
static struct file_system_type hypfs_type;
static struct super_operations hypfs_s_ops;

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

static void hypfs_remove(struct dentry *dentry)
{
	struct dentry *parent;

	parent = dentry->d_parent;
	if (S_ISDIR(dentry->d_inode->i_mode))
		simple_rmdir(parent->d_inode, dentry);
	else
		simple_unlink(parent->d_inode, dentry);
	d_delete(dentry);
	dput(dentry);
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

static struct inode *hypfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		struct hypfs_sb_info *hypfs_info = sb->s_fs_info;
		ret->i_mode = mode;
		ret->i_uid = hypfs_info->uid;
		ret->i_gid = hypfs_info->gid;
		ret->i_blksize = PAGE_CACHE_SIZE;
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
		if (mode & S_IFDIR)
			ret->i_nlink = 2;
		else
			ret->i_nlink = 1;
	}
	return ret;
}

static void hypfs_drop_inode(struct inode *inode)
{
	kfree(inode->u.generic_ip);
	generic_delete_inode(inode);
}

static int hypfs_open(struct inode *inode, struct file *filp)
{
	char *data = filp->f_dentry->d_inode->u.generic_ip;
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
	return 0;
}

static ssize_t hypfs_aio_read(struct kiocb *iocb, __user char *buf,
			      size_t count, loff_t offset)
{
	char *data;
	size_t len;
	struct file *filp = iocb->ki_filp;

	data = filp->private_data;
	len = strlen(data);
	if (offset > len) {
		count = 0;
		goto out;
	}
	if (count > len - offset)
		count = len - offset;
	if (copy_to_user(buf, data + offset, count)) {
		count = -EFAULT;
		goto out;
	}
	iocb->ki_pos += count;
	file_accessed(filp);
out:
	return count;
}
static ssize_t hypfs_aio_write(struct kiocb *iocb, const char __user *buf,
			       size_t count, loff_t pos)
{
	int rc;
	struct super_block *sb;
	struct hypfs_sb_info *fs_info;

	sb = iocb->ki_filp->f_dentry->d_inode->i_sb;
	fs_info = sb->s_fs_info;
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
	rc = hypfs_diag_create_files(sb, sb->s_root);
	if (rc) {
		printk(KERN_ERR "hypfs: Update failed\n");
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

static match_table_t hypfs_tokens = {
	{opt_uid, "uid=%u"},
	{opt_gid, "gid=%u"},
	{opt_err, NULL}
};

static int hypfs_parse_options(char *options, struct super_block *sb)
{
	char *str;
	substring_t args[MAX_OPT_ARGS];

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
			hypfs_info->uid = option;
			break;
		case opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			hypfs_info->gid = option;
			break;
		case opt_err:
		default:
			printk(KERN_ERR "hypfs: Unrecognized mount option "
			       "\"%s\" or missing value\n", str);
			return -EINVAL;
		}
	}
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
	sbi->uid = current->uid;
	sbi->gid = current->gid;
	sb->s_fs_info = sbi;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = HYPFS_MAGIC;
	sb->s_op = &hypfs_s_ops;
	if (hypfs_parse_options(data, sb)) {
		rc = -EINVAL;
		goto err_alloc;
	}
	root_inode = hypfs_make_inode(sb, S_IFDIR | 0755);
	if (!root_inode) {
		rc = -ENOMEM;
		goto err_alloc;
	}
	root_inode->i_op = &simple_dir_inode_operations;
	root_inode->i_fop = &simple_dir_operations;
	root_dentry = d_alloc_root(root_inode);
	if (!root_dentry) {
		iput(root_inode);
		rc = -ENOMEM;
		goto err_alloc;
	}
	rc = hypfs_diag_create_files(sb, root_dentry);
	if (rc)
		goto err_tree;
	sbi->update_file = hypfs_create_update_file(sb, root_dentry);
	if (IS_ERR(sbi->update_file)) {
		rc = PTR_ERR(sbi->update_file);
		goto err_tree;
	}
	hypfs_update_update(sb);
	sb->s_root = root_dentry;
	return 0;

err_tree:
	hypfs_delete_tree(root_dentry);
	d_genocide(root_dentry);
	dput(root_dentry);
err_alloc:
	kfree(sbi);
	return rc;
}

static struct super_block *hypfs_get_super(struct file_system_type *fst,
					   int flags, const char *devname,
					   void *data)
{
	return get_sb_single(fst, flags, data, hypfs_fill_super);
}

static void hypfs_kill_super(struct super_block *sb)
{
	struct hypfs_sb_info *sb_info = sb->s_fs_info;

	hypfs_delete_tree(sb->s_root);
	hypfs_remove(sb_info->update_file);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
	kill_litter_super(sb);
}

static struct dentry *hypfs_create_file(struct super_block *sb,
					struct dentry *parent, const char *name,
					char *data, mode_t mode)
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		return ERR_PTR(-ENOMEM);
	inode = hypfs_make_inode(sb, mode);
	if (!inode) {
		dput(dentry);
		return ERR_PTR(-ENOMEM);
	}
	if (mode & S_IFREG) {
		inode->i_fop = &hypfs_file_ops;
		if (data)
			inode->i_size = strlen(data);
		else
			inode->i_size = 0;
	} else if (mode & S_IFDIR) {
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		parent->d_inode->i_nlink++;
	} else
		BUG();
	inode->u.generic_ip = data;
	d_instantiate(dentry, inode);
	dget(dentry);
	return dentry;
}

struct dentry *hypfs_mkdir(struct super_block *sb, struct dentry *parent,
			   const char *name)
{
	struct dentry *dentry;

	dentry = hypfs_create_file(sb, parent, name, NULL, S_IFDIR | DIR_MODE);
	if (IS_ERR(dentry))
		return dentry;
	hypfs_add_dentry(dentry);
	parent->d_inode->i_nlink++;
	return dentry;
}

static struct dentry *hypfs_create_update_file(struct super_block *sb,
					       struct dentry *dir)
{
	struct dentry *dentry;

	dentry = hypfs_create_file(sb, dir, "update", NULL,
				   S_IFREG | UPDATE_FILE_MODE);
	/*
	 * We do not put the update file on the 'delete' list with
	 * hypfs_add_dentry(), since it should not be removed when the tree
	 * is updated.
	 */
	return dentry;
}

struct dentry *hypfs_create_u64(struct super_block *sb, struct dentry *dir,
				const char *name, __u64 value)
{
	char *buffer;
	char tmp[TMP_SIZE];
	struct dentry *dentry;

	snprintf(tmp, TMP_SIZE, "%lld\n", (unsigned long long int)value);
	buffer = kstrdup(tmp, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	dentry =
	    hypfs_create_file(sb, dir, name, buffer, S_IFREG | REG_FILE_MODE);
	if (IS_ERR(dentry)) {
		kfree(buffer);
		return ERR_PTR(-ENOMEM);
	}
	hypfs_add_dentry(dentry);
	return dentry;
}

struct dentry *hypfs_create_str(struct super_block *sb, struct dentry *dir,
				const char *name, char *string)
{
	char *buffer;
	struct dentry *dentry;

	buffer = kmalloc(strlen(string) + 2, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	sprintf(buffer, "%s\n", string);
	dentry =
	    hypfs_create_file(sb, dir, name, buffer, S_IFREG | REG_FILE_MODE);
	if (IS_ERR(dentry)) {
		kfree(buffer);
		return ERR_PTR(-ENOMEM);
	}
	hypfs_add_dentry(dentry);
	return dentry;
}

static struct file_operations hypfs_file_ops = {
	.open		= hypfs_open,
	.release	= hypfs_release,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= hypfs_aio_read,
	.aio_write	= hypfs_aio_write,
};

static struct file_system_type hypfs_type = {
	.owner		= THIS_MODULE,
	.name		= "s390_hypfs",
	.get_sb		= hypfs_get_super,
	.kill_sb	= hypfs_kill_super
};

static struct super_operations hypfs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= hypfs_drop_inode,
};

static decl_subsys(s390, NULL, NULL);

static int __init hypfs_init(void)
{
	int rc;

	if (MACHINE_IS_VM)
		return -ENODATA;
	if (hypfs_diag_init()) {
		rc = -ENODATA;
		goto fail_diag;
	}
	kset_set_kset_s(&s390_subsys, hypervisor_subsys);
	rc = subsystem_register(&s390_subsys);
	if (rc)
		goto fail_sysfs;
	rc = register_filesystem(&hypfs_type);
	if (rc)
		goto fail_filesystem;
	return 0;

fail_filesystem:
	subsystem_unregister(&s390_subsys);
fail_sysfs:
	hypfs_diag_exit();
fail_diag:
	printk(KERN_ERR "hypfs: Initialization failed with rc = %i.\n", rc);
	return rc;
}

static void __exit hypfs_exit(void)
{
	hypfs_diag_exit();
	unregister_filesystem(&hypfs_type);
	subsystem_unregister(&s390_subsys);
}

module_init(hypfs_init)
module_exit(hypfs_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Holzheu <holzheu@de.ibm.com>");
MODULE_DESCRIPTION("s390 Hypervisor Filesystem");
