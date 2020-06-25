/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/compiler_types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/gfp.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/ipc_namespace.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/magic.h>
#include <linux/major.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mount.h>
#include <linux/fs_parser.h>
#include <linux/radix-tree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/user_namespace.h>
#include <linux/xarray.h>
#include <uapi/asm-generic/errno-base.h>
#include <uapi/linux/android/binder.h>
#include <uapi/linux/android/binderfs.h>

#include "binder_internal.h"

#define FIRST_INODE 1
#define SECOND_INODE 2
#define INODE_OFFSET 3
#define INTSTRLEN 21
#define BINDERFS_MAX_MINOR (1U << MINORBITS)
/* Ensure that the initial ipc namespace always has devices available. */
#define BINDERFS_MAX_MINOR_CAPPED (BINDERFS_MAX_MINOR - 4)

static dev_t binderfs_dev;
static DEFINE_MUTEX(binderfs_minors_mutex);
static DEFINE_IDA(binderfs_minors);

enum binderfs_param {
	Opt_max,
	Opt_stats_mode,
};

enum binderfs_stats_mode {
	binderfs_stats_mode_unset,
	binderfs_stats_mode_global,
};

static const struct constant_table binderfs_param_stats[] = {
	{ "global", binderfs_stats_mode_global },
	{}
};

const struct fs_parameter_spec binderfs_fs_parameters[] = {
	fsparam_u32("max",	Opt_max),
	fsparam_enum("stats",	Opt_stats_mode, binderfs_param_stats),
	{}
};

static inline struct binderfs_info *BINDERFS_SB(const struct super_block *sb)
{
	return sb->s_fs_info;
}

bool is_binderfs_device(const struct inode *inode)
{
	if (inode->i_sb->s_magic == BINDERFS_SUPER_MAGIC)
		return true;

	return false;
}

/**
 * binderfs_binder_device_create - allocate inode from super block of a
 *                                 binderfs mount
 * @ref_inode: inode from wich the super block will be taken
 * @userp:     buffer to copy information about new device for userspace to
 * @req:       struct binderfs_device as copied from userspace
 *
 * This function allocates a new binder_device and reserves a new minor
 * number for it.
 * Minor numbers are limited and tracked globally in binderfs_minors. The
 * function will stash a struct binder_device for the specific binder
 * device in i_private of the inode.
 * It will go on to allocate a new inode from the super block of the
 * filesystem mount, stash a struct binder_device in its i_private field
 * and attach a dentry to that inode.
 *
 * Return: 0 on success, negative errno on failure
 */
static int binderfs_binder_device_create(struct inode *ref_inode,
					 struct binderfs_device __user *userp,
					 struct binderfs_device *req)
{
	int minor, ret;
	struct dentry *dentry, *root;
	struct binder_device *device;
	char *name = NULL;
	size_t name_len;
	struct inode *inode = NULL;
	struct super_block *sb = ref_inode->i_sb;
	struct binderfs_info *info = sb->s_fs_info;
#if defined(CONFIG_IPC_NS)
	bool use_reserve = (info->ipc_ns == &init_ipc_ns);
#else
	bool use_reserve = true;
#endif

	/* Reserve new minor number for the new device. */
	mutex_lock(&binderfs_minors_mutex);
	if (++info->device_count <= info->mount_opts.max)
		minor = ida_alloc_max(&binderfs_minors,
				      use_reserve ? BINDERFS_MAX_MINOR :
						    BINDERFS_MAX_MINOR_CAPPED,
				      GFP_KERNEL);
	else
		minor = -ENOSPC;
	if (minor < 0) {
		--info->device_count;
		mutex_unlock(&binderfs_minors_mutex);
		return minor;
	}
	mutex_unlock(&binderfs_minors_mutex);

	ret = -ENOMEM;
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		goto err;

	inode = new_inode(sb);
	if (!inode)
		goto err;

	inode->i_ino = minor + INODE_OFFSET;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	init_special_inode(inode, S_IFCHR | 0600,
			   MKDEV(MAJOR(binderfs_dev), minor));
	inode->i_fop = &binder_fops;
	inode->i_uid = info->root_uid;
	inode->i_gid = info->root_gid;

	req->name[BINDERFS_MAX_NAME] = '\0'; /* NUL-terminate */
	name_len = strlen(req->name);
	/* Make sure to include terminating NUL byte */
	name = kmemdup(req->name, name_len + 1, GFP_KERNEL);
	if (!name)
		goto err;

	refcount_set(&device->ref, 1);
	device->binderfs_inode = inode;
	device->context.binder_context_mgr_uid = INVALID_UID;
	device->context.name = name;
	device->miscdev.name = name;
	device->miscdev.minor = minor;
	mutex_init(&device->context.context_mgr_node_lock);

	req->major = MAJOR(binderfs_dev);
	req->minor = minor;

	if (userp && copy_to_user(userp, req, sizeof(*req))) {
		ret = -EFAULT;
		goto err;
	}

	root = sb->s_root;
	inode_lock(d_inode(root));

	/* look it up */
	dentry = lookup_one_len(name, root, name_len);
	if (IS_ERR(dentry)) {
		inode_unlock(d_inode(root));
		ret = PTR_ERR(dentry);
		goto err;
	}

	if (d_really_is_positive(dentry)) {
		/* already exists */
		dput(dentry);
		inode_unlock(d_inode(root));
		ret = -EEXIST;
		goto err;
	}

	inode->i_private = device;
	d_instantiate(dentry, inode);
	fsnotify_create(root->d_inode, dentry);
	inode_unlock(d_inode(root));

	return 0;

err:
	kfree(name);
	kfree(device);
	mutex_lock(&binderfs_minors_mutex);
	--info->device_count;
	ida_free(&binderfs_minors, minor);
	mutex_unlock(&binderfs_minors_mutex);
	iput(inode);

	return ret;
}

/**
 * binderfs_ctl_ioctl - handle binder device node allocation requests
 *
 * The request handler for the binder-control device. All requests operate on
 * the binderfs mount the binder-control device resides in:
 * - BINDER_CTL_ADD
 *   Allocate a new binder device.
 *
 * Return: 0 on success, negative errno on failure
 */
static long binder_ctl_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	int ret = -EINVAL;
	struct inode *inode = file_inode(file);
	struct binderfs_device __user *device = (struct binderfs_device __user *)arg;
	struct binderfs_device device_req;

	switch (cmd) {
	case BINDER_CTL_ADD:
		ret = copy_from_user(&device_req, device, sizeof(device_req));
		if (ret) {
			ret = -EFAULT;
			break;
		}

		ret = binderfs_binder_device_create(inode, device, &device_req);
		break;
	default:
		break;
	}

	return ret;
}

static void binderfs_evict_inode(struct inode *inode)
{
	struct binder_device *device = inode->i_private;
	struct binderfs_info *info = BINDERFS_SB(inode->i_sb);

	clear_inode(inode);

	if (!S_ISCHR(inode->i_mode) || !device)
		return;

	mutex_lock(&binderfs_minors_mutex);
	--info->device_count;
	ida_free(&binderfs_minors, device->miscdev.minor);
	mutex_unlock(&binderfs_minors_mutex);

	if (refcount_dec_and_test(&device->ref)) {
		kfree(device->context.name);
		kfree(device);
	}
}

static int binderfs_fs_context_parse_param(struct fs_context *fc,
					   struct fs_parameter *param)
{
	int opt;
	struct binderfs_mount_opts *ctx = fc->fs_private;
	struct fs_parse_result result;

	opt = fs_parse(fc, binderfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_max:
		if (result.uint_32 > BINDERFS_MAX_MINOR)
			return invalfc(fc, "Bad value for '%s'", param->key);

		ctx->max = result.uint_32;
		break;
	case Opt_stats_mode:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		ctx->stats_mode = result.uint_32;
		break;
	default:
		return invalfc(fc, "Unsupported parameter '%s'", param->key);
	}

	return 0;
}

static int binderfs_fs_context_reconfigure(struct fs_context *fc)
{
	struct binderfs_mount_opts *ctx = fc->fs_private;
	struct binderfs_info *info = BINDERFS_SB(fc->root->d_sb);

	if (info->mount_opts.stats_mode != ctx->stats_mode)
		return invalfc(fc, "Binderfs stats mode cannot be changed during a remount");

	info->mount_opts.stats_mode = ctx->stats_mode;
	info->mount_opts.max = ctx->max;
	return 0;
}

static int binderfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct binderfs_info *info = BINDERFS_SB(root->d_sb);

	if (info->mount_opts.max <= BINDERFS_MAX_MINOR)
		seq_printf(seq, ",max=%d", info->mount_opts.max);

	switch (info->mount_opts.stats_mode) {
	case binderfs_stats_mode_unset:
		break;
	case binderfs_stats_mode_global:
		seq_printf(seq, ",stats=global");
		break;
	}

	return 0;
}

static void binderfs_put_super(struct super_block *sb)
{
	struct binderfs_info *info = sb->s_fs_info;

	if (info && info->ipc_ns)
		put_ipc_ns(info->ipc_ns);

	kfree(info);
	sb->s_fs_info = NULL;
}

static const struct super_operations binderfs_super_ops = {
	.evict_inode    = binderfs_evict_inode,
	.show_options	= binderfs_show_options,
	.statfs         = simple_statfs,
	.put_super	= binderfs_put_super,
};

static inline bool is_binderfs_control_device(const struct dentry *dentry)
{
	struct binderfs_info *info = dentry->d_sb->s_fs_info;
	return info->control_dentry == dentry;
}

static int binderfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	if (is_binderfs_control_device(old_dentry) ||
	    is_binderfs_control_device(new_dentry))
		return -EPERM;

	return simple_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

static int binderfs_unlink(struct inode *dir, struct dentry *dentry)
{
	if (is_binderfs_control_device(dentry))
		return -EPERM;

	return simple_unlink(dir, dentry);
}

static const struct file_operations binder_ctl_fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.unlocked_ioctl	= binder_ctl_ioctl,
	.compat_ioctl	= binder_ctl_ioctl,
	.llseek		= noop_llseek,
};

/**
 * binderfs_binder_ctl_create - create a new binder-control device
 * @sb: super block of the binderfs mount
 *
 * This function creates a new binder-control device node in the binderfs mount
 * referred to by @sb.
 *
 * Return: 0 on success, negative errno on failure
 */
static int binderfs_binder_ctl_create(struct super_block *sb)
{
	int minor, ret;
	struct dentry *dentry;
	struct binder_device *device;
	struct inode *inode = NULL;
	struct dentry *root = sb->s_root;
	struct binderfs_info *info = sb->s_fs_info;
#if defined(CONFIG_IPC_NS)
	bool use_reserve = (info->ipc_ns == &init_ipc_ns);
#else
	bool use_reserve = true;
#endif

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	/* If we have already created a binder-control node, return. */
	if (info->control_dentry) {
		ret = 0;
		goto out;
	}

	ret = -ENOMEM;
	inode = new_inode(sb);
	if (!inode)
		goto out;

	/* Reserve a new minor number for the new device. */
	mutex_lock(&binderfs_minors_mutex);
	minor = ida_alloc_max(&binderfs_minors,
			      use_reserve ? BINDERFS_MAX_MINOR :
					    BINDERFS_MAX_MINOR_CAPPED,
			      GFP_KERNEL);
	mutex_unlock(&binderfs_minors_mutex);
	if (minor < 0) {
		ret = minor;
		goto out;
	}

	inode->i_ino = SECOND_INODE;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	init_special_inode(inode, S_IFCHR | 0600,
			   MKDEV(MAJOR(binderfs_dev), minor));
	inode->i_fop = &binder_ctl_fops;
	inode->i_uid = info->root_uid;
	inode->i_gid = info->root_gid;

	refcount_set(&device->ref, 1);
	device->binderfs_inode = inode;
	device->miscdev.minor = minor;

	dentry = d_alloc_name(root, "binder-control");
	if (!dentry)
		goto out;

	inode->i_private = device;
	info->control_dentry = dentry;
	d_add(dentry, inode);

	return 0;

out:
	kfree(device);
	iput(inode);

	return ret;
}

static const struct inode_operations binderfs_dir_inode_operations = {
	.lookup = simple_lookup,
	.rename = binderfs_rename,
	.unlink = binderfs_unlink,
};

static struct inode *binderfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret;

	ret = new_inode(sb);
	if (ret) {
		ret->i_ino = iunique(sb, BINDERFS_MAX_MINOR + INODE_OFFSET);
		ret->i_mode = mode;
		ret->i_atime = ret->i_mtime = ret->i_ctime = current_time(ret);
	}
	return ret;
}

static struct dentry *binderfs_create_dentry(struct dentry *parent,
					     const char *name)
{
	struct dentry *dentry;

	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		return dentry;

	/* Return error if the file/dir already exists. */
	if (d_really_is_positive(dentry)) {
		dput(dentry);
		return ERR_PTR(-EEXIST);
	}

	return dentry;
}

void binderfs_remove_file(struct dentry *dentry)
{
	struct inode *parent_inode;

	parent_inode = d_inode(dentry->d_parent);
	inode_lock(parent_inode);
	if (simple_positive(dentry)) {
		dget(dentry);
		simple_unlink(parent_inode, dentry);
		d_delete(dentry);
		dput(dentry);
	}
	inode_unlock(parent_inode);
}

struct dentry *binderfs_create_file(struct dentry *parent, const char *name,
				    const struct file_operations *fops,
				    void *data)
{
	struct dentry *dentry;
	struct inode *new_inode, *parent_inode;
	struct super_block *sb;

	parent_inode = d_inode(parent);
	inode_lock(parent_inode);

	dentry = binderfs_create_dentry(parent, name);
	if (IS_ERR(dentry))
		goto out;

	sb = parent_inode->i_sb;
	new_inode = binderfs_make_inode(sb, S_IFREG | 0444);
	if (!new_inode) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	}

	new_inode->i_fop = fops;
	new_inode->i_private = data;
	d_instantiate(dentry, new_inode);
	fsnotify_create(parent_inode, dentry);

out:
	inode_unlock(parent_inode);
	return dentry;
}

static struct dentry *binderfs_create_dir(struct dentry *parent,
					  const char *name)
{
	struct dentry *dentry;
	struct inode *new_inode, *parent_inode;
	struct super_block *sb;

	parent_inode = d_inode(parent);
	inode_lock(parent_inode);

	dentry = binderfs_create_dentry(parent, name);
	if (IS_ERR(dentry))
		goto out;

	sb = parent_inode->i_sb;
	new_inode = binderfs_make_inode(sb, S_IFDIR | 0755);
	if (!new_inode) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	}

	new_inode->i_fop = &simple_dir_operations;
	new_inode->i_op = &simple_dir_inode_operations;

	set_nlink(new_inode, 2);
	d_instantiate(dentry, new_inode);
	inc_nlink(parent_inode);
	fsnotify_mkdir(parent_inode, dentry);

out:
	inode_unlock(parent_inode);
	return dentry;
}

static int init_binder_logs(struct super_block *sb)
{
	struct dentry *binder_logs_root_dir, *dentry, *proc_log_dir;
	struct binderfs_info *info;
	int ret = 0;

	binder_logs_root_dir = binderfs_create_dir(sb->s_root,
						   "binder_logs");
	if (IS_ERR(binder_logs_root_dir)) {
		ret = PTR_ERR(binder_logs_root_dir);
		goto out;
	}

	dentry = binderfs_create_file(binder_logs_root_dir, "stats",
				      &binder_stats_fops, NULL);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	dentry = binderfs_create_file(binder_logs_root_dir, "state",
				      &binder_state_fops, NULL);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	dentry = binderfs_create_file(binder_logs_root_dir, "transactions",
				      &binder_transactions_fops, NULL);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	dentry = binderfs_create_file(binder_logs_root_dir,
				      "transaction_log",
				      &binder_transaction_log_fops,
				      &binder_transaction_log);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	dentry = binderfs_create_file(binder_logs_root_dir,
				      "failed_transaction_log",
				      &binder_transaction_log_fops,
				      &binder_transaction_log_failed);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	proc_log_dir = binderfs_create_dir(binder_logs_root_dir, "proc");
	if (IS_ERR(proc_log_dir)) {
		ret = PTR_ERR(proc_log_dir);
		goto out;
	}
	info = sb->s_fs_info;
	info->proc_log_dir = proc_log_dir;

out:
	return ret;
}

static int binderfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	int ret;
	struct binderfs_info *info;
	struct binderfs_mount_opts *ctx = fc->fs_private;
	struct inode *inode = NULL;
	struct binderfs_device device_info = {};
	const char *name;
	size_t len;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;

	/*
	 * The binderfs filesystem can be mounted by userns root in a
	 * non-initial userns. By default such mounts have the SB_I_NODEV flag
	 * set in s_iflags to prevent security issues where userns root can
	 * just create random device nodes via mknod() since it owns the
	 * filesystem mount. But binderfs does not allow to create any files
	 * including devices nodes. The only way to create binder devices nodes
	 * is through the binder-control device which userns root is explicitly
	 * allowed to do. So removing the SB_I_NODEV flag from s_iflags is both
	 * necessary and safe.
	 */
	sb->s_iflags &= ~SB_I_NODEV;
	sb->s_iflags |= SB_I_NOEXEC;
	sb->s_magic = BINDERFS_SUPER_MAGIC;
	sb->s_op = &binderfs_super_ops;
	sb->s_time_gran = 1;

	sb->s_fs_info = kzalloc(sizeof(struct binderfs_info), GFP_KERNEL);
	if (!sb->s_fs_info)
		return -ENOMEM;
	info = sb->s_fs_info;

	info->ipc_ns = get_ipc_ns(current->nsproxy->ipc_ns);

	info->root_gid = make_kgid(sb->s_user_ns, 0);
	if (!gid_valid(info->root_gid))
		info->root_gid = GLOBAL_ROOT_GID;
	info->root_uid = make_kuid(sb->s_user_ns, 0);
	if (!uid_valid(info->root_uid))
		info->root_uid = GLOBAL_ROOT_UID;
	info->mount_opts.max = ctx->max;
	info->mount_opts.stats_mode = ctx->stats_mode;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;

	inode->i_ino = FIRST_INODE;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_op = &binderfs_dir_inode_operations;
	set_nlink(inode, 2);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	ret = binderfs_binder_ctl_create(sb);
	if (ret)
		return ret;

	name = binder_devices_param;
	for (len = strcspn(name, ","); len > 0; len = strcspn(name, ",")) {
		strscpy(device_info.name, name, len + 1);
		ret = binderfs_binder_device_create(inode, NULL, &device_info);
		if (ret)
			return ret;
		name += len;
		if (*name == ',')
			name++;
	}

	if (info->mount_opts.stats_mode == binderfs_stats_mode_global)
		return init_binder_logs(sb);

	return 0;
}

static int binderfs_fs_context_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, binderfs_fill_super);
}

static void binderfs_fs_context_free(struct fs_context *fc)
{
	struct binderfs_mount_opts *ctx = fc->fs_private;

	kfree(ctx);
}

static const struct fs_context_operations binderfs_fs_context_ops = {
	.free		= binderfs_fs_context_free,
	.get_tree	= binderfs_fs_context_get_tree,
	.parse_param	= binderfs_fs_context_parse_param,
	.reconfigure	= binderfs_fs_context_reconfigure,
};

static int binderfs_init_fs_context(struct fs_context *fc)
{
	struct binderfs_mount_opts *ctx;

	ctx = kzalloc(sizeof(struct binderfs_mount_opts), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->max = BINDERFS_MAX_MINOR;
	ctx->stats_mode = binderfs_stats_mode_unset;

	fc->fs_private = ctx;
	fc->ops = &binderfs_fs_context_ops;

	return 0;
}

static struct file_system_type binder_fs_type = {
	.name			= "binder",
	.init_fs_context	= binderfs_init_fs_context,
	.parameters		= binderfs_fs_parameters,
	.kill_sb		= kill_litter_super,
	.fs_flags		= FS_USERNS_MOUNT,
};

int __init init_binderfs(void)
{
	int ret;
	const char *name;
	size_t len;

	/* Verify that the default binderfs device names are valid. */
	name = binder_devices_param;
	for (len = strcspn(name, ","); len > 0; len = strcspn(name, ",")) {
		if (len > BINDERFS_MAX_NAME)
			return -E2BIG;
		name += len;
		if (*name == ',')
			name++;
	}

	/* Allocate new major number for binderfs. */
	ret = alloc_chrdev_region(&binderfs_dev, 0, BINDERFS_MAX_MINOR,
				  "binder");
	if (ret)
		return ret;

	ret = register_filesystem(&binder_fs_type);
	if (ret) {
		unregister_chrdev_region(binderfs_dev, BINDERFS_MAX_MINOR);
		return ret;
	}

	return ret;
}
