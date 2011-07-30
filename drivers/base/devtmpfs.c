/*
 * devtmpfs - kernel-maintained tmpfs-based /dev
 *
 * Copyright (C) 2009, Kay Sievers <kay.sievers@vrfy.org>
 *
 * During bootup, before any driver core device is registered,
 * devtmpfs, a tmpfs-based filesystem is created. Every driver-core
 * device which requests a device node, will add a node in this
 * filesystem.
 * By default, all devices are named after the the name of the
 * device, owned by root and have a default mode of 0600. Subsystems
 * can overwrite the default setting if needed.
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/shmem_fs.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/init_task.h>

static struct vfsmount *dev_mnt;

#if defined CONFIG_DEVTMPFS_MOUNT
static int dev_mount = 1;
#else
static int dev_mount;
#endif

static int __init mount_param(char *str)
{
	dev_mount = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("devtmpfs.mount=", mount_param);

static int dev_get_sb(struct file_system_type *fs_type, int flags,
		      const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, shmem_fill_super, mnt);
}

static struct file_system_type dev_fs_type = {
	.name = "devtmpfs",
	.get_sb = dev_get_sb,
	.kill_sb = kill_litter_super,
};

#ifdef CONFIG_BLOCK
static inline int is_blockdev(struct device *dev)
{
	return dev->class == &block_class;
}
#else
static inline int is_blockdev(struct device *dev) { return 0; }
#endif

static int dev_mkdir(const char *name, mode_t mode)
{
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      name, LOOKUP_PARENT, &nd);
	if (err)
		return err;

	dentry = lookup_create(&nd, 1);
	if (!IS_ERR(dentry)) {
		err = vfs_mkdir(nd.path.dentry->d_inode, dentry, mode);
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	return err;
}

static int create_path(const char *nodepath)
{
	char *path;
	struct nameidata nd;
	int err = 0;

	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      path, LOOKUP_PARENT, &nd);
	if (err == 0) {
		struct dentry *dentry;

		/* create directory right away */
		dentry = lookup_create(&nd, 1);
		if (!IS_ERR(dentry)) {
			err = vfs_mkdir(nd.path.dentry->d_inode,
					dentry, 0755);
			dput(dentry);
		}
		mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

		path_put(&nd.path);
	} else if (err == -ENOENT) {
		char *s;

		/* parent directories do not exist, create them */
		s = path;
		while (1) {
			s = strchr(s, '/');
			if (!s)
				break;
			s[0] = '\0';
			err = dev_mkdir(path, 0755);
			if (err && err != -EEXIST)
				break;
			s[0] = '/';
			s++;
		}
	}

	kfree(path);
	return err;
}

int devtmpfs_create_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *nodename;
	const struct cred *curr_cred;
	mode_t mode = 0;
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	if (!dev_mnt)
		return 0;

	nodename = device_get_devnode(dev, &mode, &tmp);
	if (!nodename)
		return -ENOMEM;

	if (mode == 0)
		mode = 0600;
	if (is_blockdev(dev))
		mode |= S_IFBLK;
	else
		mode |= S_IFCHR;

	curr_cred = override_creds(&init_cred);
	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      nodename, LOOKUP_PARENT, &nd);
	if (err == -ENOENT) {
		/* create missing parent directories */
		create_path(nodename);
		err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
				      nodename, LOOKUP_PARENT, &nd);
		if (err)
			goto out;
	}

	dentry = lookup_create(&nd, 0);
	if (!IS_ERR(dentry)) {
		int umask;

		umask = sys_umask(0000);
		err = vfs_mknod(nd.path.dentry->d_inode,
				dentry, mode, dev->devt);
		sys_umask(umask);
		/* mark as kernel created inode */
		if (!err)
			dentry->d_inode->i_private = &dev_mnt;
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
out:
	kfree(tmp);
	revert_creds(curr_cred);
	return err;
}

static int dev_rmdir(const char *name)
{
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      name, LOOKUP_PARENT, &nd);
	if (err)
		return err;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(nd.last.name, nd.path.dentry, nd.last.len);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode)
			err = vfs_rmdir(nd.path.dentry->d_inode, dentry);
		else
			err = -ENOENT;
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	return err;
}

static int delete_path(const char *nodepath)
{
	const char *path;
	int err = 0;

	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	while (1) {
		char *base;

		base = strrchr(path, '/');
		if (!base)
			break;
		base[0] = '\0';
		err = dev_rmdir(path);
		if (err)
			break;
	}

	kfree(path);
	return err;
}

static int dev_mynode(struct device *dev, struct inode *inode, struct kstat *stat)
{
	/* did we create it */
	if (inode->i_private != &dev_mnt)
		return 0;

	/* does the dev_t match */
	if (is_blockdev(dev)) {
		if (!S_ISBLK(stat->mode))
			return 0;
	} else {
		if (!S_ISCHR(stat->mode))
			return 0;
	}
	if (stat->rdev != dev->devt)
		return 0;

	/* ours */
	return 1;
}

int devtmpfs_delete_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *nodename;
	const struct cred *curr_cred;
	struct nameidata nd;
	struct dentry *dentry;
	struct kstat stat;
	int deleted = 1;
	int err;

	if (!dev_mnt)
		return 0;

	nodename = device_get_devnode(dev, NULL, &tmp);
	if (!nodename)
		return -ENOMEM;

	curr_cred = override_creds(&init_cred);
	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      nodename, LOOKUP_PARENT, &nd);
	if (err)
		goto out;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(nd.last.name, nd.path.dentry, nd.last.len);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode) {
			err = vfs_getattr(nd.path.mnt, dentry, &stat);
			if (!err && dev_mynode(dev, dentry->d_inode, &stat)) {
				err = vfs_unlink(nd.path.dentry->d_inode,
						 dentry);
				if (!err || err == -ENOENT)
					deleted = 1;
			}
		} else {
			err = -ENOENT;
		}
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	if (deleted && strchr(nodename, '/'))
		delete_path(nodename);
out:
	kfree(tmp);
	revert_creds(curr_cred);
	return err;
}

/*
 * If configured, or requested by the commandline, devtmpfs will be
 * auto-mounted after the kernel mounted the root filesystem.
 */
int devtmpfs_mount(const char *mountpoint)
{
	struct path path;
	int err;

	if (!dev_mount)
		return 0;

	if (!dev_mnt)
		return 0;

	err = kern_path(mountpoint, LOOKUP_FOLLOW, &path);
	if (err)
		return err;
	err = do_add_mount(dev_mnt, &path, 0, NULL);
	if (err)
		printk(KERN_INFO "devtmpfs: error mounting %i\n", err);
	else
		printk(KERN_INFO "devtmpfs: mounted\n");
	path_put(&path);
	return err;
}

/*
 * Create devtmpfs instance, driver-core devices will add their device
 * nodes here.
 */
int __init devtmpfs_init(void)
{
	int err;
	struct vfsmount *mnt;
	char options[] = "mode=0755";

	err = register_filesystem(&dev_fs_type);
	if (err) {
		printk(KERN_ERR "devtmpfs: unable to register devtmpfs "
		       "type %i\n", err);
		return err;
	}

	mnt = kern_mount_data(&dev_fs_type, options);
	if (IS_ERR(mnt)) {
		err = PTR_ERR(mnt);
		printk(KERN_ERR "devtmpfs: unable to create devtmpfs %i\n", err);
		unregister_filesystem(&dev_fs_type);
		return err;
	}
	dev_mnt = mnt;

	printk(KERN_INFO "devtmpfs: initialized\n");
	return 0;
}
