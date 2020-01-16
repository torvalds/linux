/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/compiler_types.h>
#include <linux/erryes.h>
#include <linux/fs.h>
#include <linux/fsyestify.h>
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
#include <linux/parser.h>
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
#include <uapi/asm-generic/erryes-base.h>
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
static DEFINE_MUTEX(binderfs_miyesrs_mutex);
static DEFINE_IDA(binderfs_miyesrs);

enum {
	Opt_max,
	Opt_stats_mode,
	Opt_err
};

enum binderfs_stats_mode {
	STATS_NONE,
	STATS_GLOBAL,
};

static const match_table_t tokens = {
	{ Opt_max, "max=%d" },
	{ Opt_stats_mode, "stats=%s" },
	{ Opt_err, NULL     }
};

static inline struct binderfs_info *BINDERFS_I(const struct iyesde *iyesde)
{
	return iyesde->i_sb->s_fs_info;
}

bool is_binderfs_device(const struct iyesde *iyesde)
{
	if (iyesde->i_sb->s_magic == BINDERFS_SUPER_MAGIC)
		return true;

	return false;
}

/**
 * binderfs_binder_device_create - allocate iyesde from super block of a
 *                                 binderfs mount
 * @ref_iyesde: iyesde from wich the super block will be taken
 * @userp:     buffer to copy information about new device for userspace to
 * @req:       struct binderfs_device as copied from userspace
 *
 * This function allocates a new binder_device and reserves a new miyesr
 * number for it.
 * Miyesr numbers are limited and tracked globally in binderfs_miyesrs. The
 * function will stash a struct binder_device for the specific binder
 * device in i_private of the iyesde.
 * It will go on to allocate a new iyesde from the super block of the
 * filesystem mount, stash a struct binder_device in its i_private field
 * and attach a dentry to that iyesde.
 *
 * Return: 0 on success, negative erryes on failure
 */
static int binderfs_binder_device_create(struct iyesde *ref_iyesde,
					 struct binderfs_device __user *userp,
					 struct binderfs_device *req)
{
	int miyesr, ret;
	struct dentry *dentry, *root;
	struct binder_device *device;
	char *name = NULL;
	size_t name_len;
	struct iyesde *iyesde = NULL;
	struct super_block *sb = ref_iyesde->i_sb;
	struct binderfs_info *info = sb->s_fs_info;
#if defined(CONFIG_IPC_NS)
	bool use_reserve = (info->ipc_ns == &init_ipc_ns);
#else
	bool use_reserve = true;
#endif

	/* Reserve new miyesr number for the new device. */
	mutex_lock(&binderfs_miyesrs_mutex);
	if (++info->device_count <= info->mount_opts.max)
		miyesr = ida_alloc_max(&binderfs_miyesrs,
				      use_reserve ? BINDERFS_MAX_MINOR :
						    BINDERFS_MAX_MINOR_CAPPED,
				      GFP_KERNEL);
	else
		miyesr = -ENOSPC;
	if (miyesr < 0) {
		--info->device_count;
		mutex_unlock(&binderfs_miyesrs_mutex);
		return miyesr;
	}
	mutex_unlock(&binderfs_miyesrs_mutex);

	ret = -ENOMEM;
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		goto err;

	iyesde = new_iyesde(sb);
	if (!iyesde)
		goto err;

	iyesde->i_iyes = miyesr + INODE_OFFSET;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	init_special_iyesde(iyesde, S_IFCHR | 0600,
			   MKDEV(MAJOR(binderfs_dev), miyesr));
	iyesde->i_fop = &binder_fops;
	iyesde->i_uid = info->root_uid;
	iyesde->i_gid = info->root_gid;

	req->name[BINDERFS_MAX_NAME] = '\0'; /* NUL-terminate */
	name_len = strlen(req->name);
	/* Make sure to include terminating NUL byte */
	name = kmemdup(req->name, name_len + 1, GFP_KERNEL);
	if (!name)
		goto err;

	device->binderfs_iyesde = iyesde;
	device->context.binder_context_mgr_uid = INVALID_UID;
	device->context.name = name;
	device->miscdev.name = name;
	device->miscdev.miyesr = miyesr;
	mutex_init(&device->context.context_mgr_yesde_lock);

	req->major = MAJOR(binderfs_dev);
	req->miyesr = miyesr;

	if (userp && copy_to_user(userp, req, sizeof(*req))) {
		ret = -EFAULT;
		goto err;
	}

	root = sb->s_root;
	iyesde_lock(d_iyesde(root));

	/* look it up */
	dentry = lookup_one_len(name, root, name_len);
	if (IS_ERR(dentry)) {
		iyesde_unlock(d_iyesde(root));
		ret = PTR_ERR(dentry);
		goto err;
	}

	if (d_really_is_positive(dentry)) {
		/* already exists */
		dput(dentry);
		iyesde_unlock(d_iyesde(root));
		ret = -EEXIST;
		goto err;
	}

	iyesde->i_private = device;
	d_instantiate(dentry, iyesde);
	fsyestify_create(root->d_iyesde, dentry);
	iyesde_unlock(d_iyesde(root));

	return 0;

err:
	kfree(name);
	kfree(device);
	mutex_lock(&binderfs_miyesrs_mutex);
	--info->device_count;
	ida_free(&binderfs_miyesrs, miyesr);
	mutex_unlock(&binderfs_miyesrs_mutex);
	iput(iyesde);

	return ret;
}

/**
 * binderfs_ctl_ioctl - handle binder device yesde allocation requests
 *
 * The request handler for the binder-control device. All requests operate on
 * the binderfs mount the binder-control device resides in:
 * - BINDER_CTL_ADD
 *   Allocate a new binder device.
 *
 * Return: 0 on success, negative erryes on failure
 */
static long binder_ctl_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	int ret = -EINVAL;
	struct iyesde *iyesde = file_iyesde(file);
	struct binderfs_device __user *device = (struct binderfs_device __user *)arg;
	struct binderfs_device device_req;

	switch (cmd) {
	case BINDER_CTL_ADD:
		ret = copy_from_user(&device_req, device, sizeof(device_req));
		if (ret) {
			ret = -EFAULT;
			break;
		}

		ret = binderfs_binder_device_create(iyesde, device, &device_req);
		break;
	default:
		break;
	}

	return ret;
}

static void binderfs_evict_iyesde(struct iyesde *iyesde)
{
	struct binder_device *device = iyesde->i_private;
	struct binderfs_info *info = BINDERFS_I(iyesde);

	clear_iyesde(iyesde);

	if (!S_ISCHR(iyesde->i_mode) || !device)
		return;

	mutex_lock(&binderfs_miyesrs_mutex);
	--info->device_count;
	ida_free(&binderfs_miyesrs, device->miscdev.miyesr);
	mutex_unlock(&binderfs_miyesrs_mutex);

	kfree(device->context.name);
	kfree(device);
}

/**
 * binderfs_parse_mount_opts - parse binderfs mount options
 * @data: options to set (can be NULL in which case defaults are used)
 */
static int binderfs_parse_mount_opts(char *data,
				     struct binderfs_mount_opts *opts)
{
	char *p, *stats;
	opts->max = BINDERFS_MAX_MINOR;
	opts->stats_mode = STATS_NONE;

	while ((p = strsep(&data, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		int max_devices;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_max:
			if (match_int(&args[0], &max_devices) ||
			    (max_devices < 0 ||
			     (max_devices > BINDERFS_MAX_MINOR)))
				return -EINVAL;

			opts->max = max_devices;
			break;
		case Opt_stats_mode:
			if (!capable(CAP_SYS_ADMIN))
				return -EINVAL;

			stats = match_strdup(&args[0]);
			if (!stats)
				return -ENOMEM;

			if (strcmp(stats, "global") != 0) {
				kfree(stats);
				return -EINVAL;
			}

			opts->stats_mode = STATS_GLOBAL;
			kfree(stats);
			break;
		default:
			pr_err("Invalid mount options\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int binderfs_remount(struct super_block *sb, int *flags, char *data)
{
	int prev_stats_mode, ret;
	struct binderfs_info *info = sb->s_fs_info;

	prev_stats_mode = info->mount_opts.stats_mode;
	ret = binderfs_parse_mount_opts(data, &info->mount_opts);
	if (ret)
		return ret;

	if (prev_stats_mode != info->mount_opts.stats_mode) {
		pr_err("Binderfs stats mode canyest be changed during a remount\n");
		info->mount_opts.stats_mode = prev_stats_mode;
		return -EINVAL;
	}

	return 0;
}

static int binderfs_show_mount_opts(struct seq_file *seq, struct dentry *root)
{
	struct binderfs_info *info;

	info = root->d_sb->s_fs_info;
	if (info->mount_opts.max <= BINDERFS_MAX_MINOR)
		seq_printf(seq, ",max=%d", info->mount_opts.max);
	if (info->mount_opts.stats_mode == STATS_GLOBAL)
		seq_printf(seq, ",stats=global");

	return 0;
}

static const struct super_operations binderfs_super_ops = {
	.evict_iyesde    = binderfs_evict_iyesde,
	.remount_fs	= binderfs_remount,
	.show_options	= binderfs_show_mount_opts,
	.statfs         = simple_statfs,
};

static inline bool is_binderfs_control_device(const struct dentry *dentry)
{
	struct binderfs_info *info = dentry->d_sb->s_fs_info;
	return info->control_dentry == dentry;
}

static int binderfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			   struct iyesde *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	if (is_binderfs_control_device(old_dentry) ||
	    is_binderfs_control_device(new_dentry))
		return -EPERM;

	return simple_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

static int binderfs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	if (is_binderfs_control_device(dentry))
		return -EPERM;

	return simple_unlink(dir, dentry);
}

static const struct file_operations binder_ctl_fops = {
	.owner		= THIS_MODULE,
	.open		= yesnseekable_open,
	.unlocked_ioctl	= binder_ctl_ioctl,
	.compat_ioctl	= binder_ctl_ioctl,
	.llseek		= yesop_llseek,
};

/**
 * binderfs_binder_ctl_create - create a new binder-control device
 * @sb: super block of the binderfs mount
 *
 * This function creates a new binder-control device yesde in the binderfs mount
 * referred to by @sb.
 *
 * Return: 0 on success, negative erryes on failure
 */
static int binderfs_binder_ctl_create(struct super_block *sb)
{
	int miyesr, ret;
	struct dentry *dentry;
	struct binder_device *device;
	struct iyesde *iyesde = NULL;
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

	/* If we have already created a binder-control yesde, return. */
	if (info->control_dentry) {
		ret = 0;
		goto out;
	}

	ret = -ENOMEM;
	iyesde = new_iyesde(sb);
	if (!iyesde)
		goto out;

	/* Reserve a new miyesr number for the new device. */
	mutex_lock(&binderfs_miyesrs_mutex);
	miyesr = ida_alloc_max(&binderfs_miyesrs,
			      use_reserve ? BINDERFS_MAX_MINOR :
					    BINDERFS_MAX_MINOR_CAPPED,
			      GFP_KERNEL);
	mutex_unlock(&binderfs_miyesrs_mutex);
	if (miyesr < 0) {
		ret = miyesr;
		goto out;
	}

	iyesde->i_iyes = SECOND_INODE;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	init_special_iyesde(iyesde, S_IFCHR | 0600,
			   MKDEV(MAJOR(binderfs_dev), miyesr));
	iyesde->i_fop = &binder_ctl_fops;
	iyesde->i_uid = info->root_uid;
	iyesde->i_gid = info->root_gid;

	device->binderfs_iyesde = iyesde;
	device->miscdev.miyesr = miyesr;

	dentry = d_alloc_name(root, "binder-control");
	if (!dentry)
		goto out;

	iyesde->i_private = device;
	info->control_dentry = dentry;
	d_add(dentry, iyesde);

	return 0;

out:
	kfree(device);
	iput(iyesde);

	return ret;
}

static const struct iyesde_operations binderfs_dir_iyesde_operations = {
	.lookup = simple_lookup,
	.rename = binderfs_rename,
	.unlink = binderfs_unlink,
};

static struct iyesde *binderfs_make_iyesde(struct super_block *sb, int mode)
{
	struct iyesde *ret;

	ret = new_iyesde(sb);
	if (ret) {
		ret->i_iyes = iunique(sb, BINDERFS_MAX_MINOR + INODE_OFFSET);
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
	struct iyesde *parent_iyesde;

	parent_iyesde = d_iyesde(dentry->d_parent);
	iyesde_lock(parent_iyesde);
	if (simple_positive(dentry)) {
		dget(dentry);
		simple_unlink(parent_iyesde, dentry);
		d_delete(dentry);
		dput(dentry);
	}
	iyesde_unlock(parent_iyesde);
}

struct dentry *binderfs_create_file(struct dentry *parent, const char *name,
				    const struct file_operations *fops,
				    void *data)
{
	struct dentry *dentry;
	struct iyesde *new_iyesde, *parent_iyesde;
	struct super_block *sb;

	parent_iyesde = d_iyesde(parent);
	iyesde_lock(parent_iyesde);

	dentry = binderfs_create_dentry(parent, name);
	if (IS_ERR(dentry))
		goto out;

	sb = parent_iyesde->i_sb;
	new_iyesde = binderfs_make_iyesde(sb, S_IFREG | 0444);
	if (!new_iyesde) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	}

	new_iyesde->i_fop = fops;
	new_iyesde->i_private = data;
	d_instantiate(dentry, new_iyesde);
	fsyestify_create(parent_iyesde, dentry);

out:
	iyesde_unlock(parent_iyesde);
	return dentry;
}

static struct dentry *binderfs_create_dir(struct dentry *parent,
					  const char *name)
{
	struct dentry *dentry;
	struct iyesde *new_iyesde, *parent_iyesde;
	struct super_block *sb;

	parent_iyesde = d_iyesde(parent);
	iyesde_lock(parent_iyesde);

	dentry = binderfs_create_dentry(parent, name);
	if (IS_ERR(dentry))
		goto out;

	sb = parent_iyesde->i_sb;
	new_iyesde = binderfs_make_iyesde(sb, S_IFDIR | 0755);
	if (!new_iyesde) {
		dput(dentry);
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	}

	new_iyesde->i_fop = &simple_dir_operations;
	new_iyesde->i_op = &simple_dir_iyesde_operations;

	set_nlink(new_iyesde, 2);
	d_instantiate(dentry, new_iyesde);
	inc_nlink(parent_iyesde);
	fsyestify_mkdir(parent_iyesde, dentry);

out:
	iyesde_unlock(parent_iyesde);
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

static int binderfs_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret;
	struct binderfs_info *info;
	struct iyesde *iyesde = NULL;
	struct binderfs_device device_info = { 0 };
	const char *name;
	size_t len;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;

	/*
	 * The binderfs filesystem can be mounted by userns root in a
	 * yesn-initial userns. By default such mounts have the SB_I_NODEV flag
	 * set in s_iflags to prevent security issues where userns root can
	 * just create random device yesdes via mkyesd() since it owns the
	 * filesystem mount. But binderfs does yest allow to create any files
	 * including devices yesdes. The only way to create binder devices yesdes
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

	ret = binderfs_parse_mount_opts(data, &info->mount_opts);
	if (ret)
		return ret;

	info->root_gid = make_kgid(sb->s_user_ns, 0);
	if (!gid_valid(info->root_gid))
		info->root_gid = GLOBAL_ROOT_GID;
	info->root_uid = make_kuid(sb->s_user_ns, 0);
	if (!uid_valid(info->root_uid))
		info->root_uid = GLOBAL_ROOT_UID;

	iyesde = new_iyesde(sb);
	if (!iyesde)
		return -ENOMEM;

	iyesde->i_iyes = FIRST_INODE;
	iyesde->i_fop = &simple_dir_operations;
	iyesde->i_mode = S_IFDIR | 0755;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	iyesde->i_op = &binderfs_dir_iyesde_operations;
	set_nlink(iyesde, 2);

	sb->s_root = d_make_root(iyesde);
	if (!sb->s_root)
		return -ENOMEM;

	ret = binderfs_binder_ctl_create(sb);
	if (ret)
		return ret;

	name = binder_devices_param;
	for (len = strcspn(name, ","); len > 0; len = strcspn(name, ",")) {
		strscpy(device_info.name, name, len + 1);
		ret = binderfs_binder_device_create(iyesde, NULL, &device_info);
		if (ret)
			return ret;
		name += len;
		if (*name == ',')
			name++;
	}

	if (info->mount_opts.stats_mode == STATS_GLOBAL)
		return init_binder_logs(sb);

	return 0;
}

static struct dentry *binderfs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	return mount_yesdev(fs_type, flags, data, binderfs_fill_super);
}

static void binderfs_kill_super(struct super_block *sb)
{
	struct binderfs_info *info = sb->s_fs_info;

	kill_litter_super(sb);

	if (info && info->ipc_ns)
		put_ipc_ns(info->ipc_ns);

	kfree(info);
}

static struct file_system_type binder_fs_type = {
	.name		= "binder",
	.mount		= binderfs_mount,
	.kill_sb	= binderfs_kill_super,
	.fs_flags	= FS_USERNS_MOUNT,
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
