// SPDX-License-Identifier: GPL-2.0
/*
 * devtmpfs - kernel-maintained tmpfs-based /dev
 *
 * Copyright (C) 2009, Kay Sievers <kay.sievers@vrfy.org>
 *
 * During bootup, before any driver core device is registered,
 * devtmpfs, a tmpfs-based filesystem is created. Every driver-core
 * device which requests a device node, will add a node in this
 * filesystem.
 * By default, all devices are named after the name of the device,
 * owned by root and have a default mode of 0600. Subsystems can
 * overwrite the default setting if needed.
 */

#define pr_fmt(fmt) "devtmpfs: " fmt

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/shmem_fs.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/init_syscalls.h>
#include <uapi/linux/mount.h>
#include "base.h"

#ifdef CONFIG_DEVTMPFS_SAFE
#define DEVTMPFS_MFLAGS       (MS_SILENT | MS_NOEXEC | MS_NOSUID)
#else
#define DEVTMPFS_MFLAGS       (MS_SILENT)
#endif

static struct task_struct *thread;

static int __initdata mount_dev = IS_ENABLED(CONFIG_DEVTMPFS_MOUNT);

static DEFINE_SPINLOCK(req_lock);

static struct req {
	struct req *next;
	struct completion done;
	int err;
	const char *name;
	umode_t mode;	/* 0 => delete */
	kuid_t uid;
	kgid_t gid;
	struct device *dev;
} *requests;

static int __init mount_param(char *str)
{
	mount_dev = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("devtmpfs.mount=", mount_param);

static struct vfsmount *mnt;

static struct file_system_type internal_fs_type = {
	.name = "devtmpfs",
#ifdef CONFIG_TMPFS
	.init_fs_context = shmem_init_fs_context,
#else
	.init_fs_context = ramfs_init_fs_context,
#endif
	.kill_sb = kill_litter_super,
};

/* Simply take a ref on the existing mount */
static int devtmpfs_get_tree(struct fs_context *fc)
{
	struct super_block *sb = mnt->mnt_sb;

	atomic_inc(&sb->s_active);
	down_write(&sb->s_umount);
	fc->root = dget(sb->s_root);
	return 0;
}

/* Ops are filled in during init depending on underlying shmem or ramfs type */
struct fs_context_operations devtmpfs_context_ops = {};

/* Call the underlying initialization and set to our ops */
static int devtmpfs_init_fs_context(struct fs_context *fc)
{
	int ret;
#ifdef CONFIG_TMPFS
	ret = shmem_init_fs_context(fc);
#else
	ret = ramfs_init_fs_context(fc);
#endif
	if (ret < 0)
		return ret;

	fc->ops = &devtmpfs_context_ops;

	return 0;
}

static struct file_system_type dev_fs_type = {
	.name = "devtmpfs",
	.init_fs_context = devtmpfs_init_fs_context,
};

static int devtmpfs_submit_req(struct req *req, const char *tmp)
{
	init_completion(&req->done);

	spin_lock(&req_lock);
	req->next = requests;
	requests = req;
	spin_unlock(&req_lock);

	wake_up_process(thread);
	wait_for_completion(&req->done);

	kfree(tmp);

	return req->err;
}

int devtmpfs_create_node(struct device *dev)
{
	const char *tmp = NULL;
	struct req req;

	if (!thread)
		return 0;

	req.mode = 0;
	req.uid = GLOBAL_ROOT_UID;
	req.gid = GLOBAL_ROOT_GID;
	req.name = device_get_devnode(dev, &req.mode, &req.uid, &req.gid, &tmp);
	if (!req.name)
		return -ENOMEM;

	if (req.mode == 0)
		req.mode = 0600;
	if (is_blockdev(dev))
		req.mode |= S_IFBLK;
	else
		req.mode |= S_IFCHR;

	req.dev = dev;

	return devtmpfs_submit_req(&req, tmp);
}

int devtmpfs_delete_node(struct device *dev)
{
	const char *tmp = NULL;
	struct req req;

	if (!thread)
		return 0;

	req.name = device_get_devnode(dev, NULL, NULL, NULL, &tmp);
	if (!req.name)
		return -ENOMEM;

	req.mode = 0;
	req.dev = dev;

	return devtmpfs_submit_req(&req, tmp);
}

static int dev_mkdir(const char *name, umode_t mode)
{
	struct dentry *dentry;
	struct path path;

	dentry = start_creating_path(AT_FDCWD, name, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	dentry = vfs_mkdir(&nop_mnt_idmap, d_inode(path.dentry), dentry, mode);
	if (!IS_ERR(dentry))
		/* mark as kernel-created inode */
		d_inode(dentry)->i_private = &thread;
	end_creating_path(&path, dentry);
	return PTR_ERR_OR_ZERO(dentry);
}

static int create_path(const char *nodepath)
{
	char *path;
	char *s;
	int err = 0;

	/* parent directories do not exist, create them */
	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	s = path;
	for (;;) {
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
	kfree(path);
	return err;
}

static int handle_create(const char *nodename, umode_t mode, kuid_t uid,
			 kgid_t gid, struct device *dev)
{
	struct dentry *dentry;
	struct path path;
	int err;

	dentry = start_creating_path(AT_FDCWD, nodename, &path, 0);
	if (dentry == ERR_PTR(-ENOENT)) {
		create_path(nodename);
		dentry = start_creating_path(AT_FDCWD, nodename, &path, 0);
	}
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	err = vfs_mknod(&nop_mnt_idmap, d_inode(path.dentry), dentry, mode,
			dev->devt);
	if (!err) {
		struct iattr newattrs;

		newattrs.ia_mode = mode;
		newattrs.ia_uid = uid;
		newattrs.ia_gid = gid;
		newattrs.ia_valid = ATTR_MODE|ATTR_UID|ATTR_GID;
		inode_lock(d_inode(dentry));
		notify_change(&nop_mnt_idmap, dentry, &newattrs, NULL);
		inode_unlock(d_inode(dentry));

		/* mark as kernel-created inode */
		d_inode(dentry)->i_private = &thread;
	}
	end_creating_path(&path, dentry);
	return err;
}

static int dev_rmdir(const char *name)
{
	struct path parent;
	struct dentry *dentry;
	int err;

	dentry = start_removing_path(name, &parent);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	if (d_inode(dentry)->i_private == &thread)
		err = vfs_rmdir(&nop_mnt_idmap, d_inode(parent.dentry),
				dentry);
	else
		err = -EPERM;

	end_removing_path(&parent, dentry);
	return err;
}

static int delete_path(const char *nodepath)
{
	char *path;
	int err = 0;

	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	for (;;) {
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

static int dev_mynode(struct device *dev, struct inode *inode)
{
	/* did we create it */
	if (inode->i_private != &thread)
		return 0;

	/* does the dev_t match */
	if (is_blockdev(dev)) {
		if (!S_ISBLK(inode->i_mode))
			return 0;
	} else {
		if (!S_ISCHR(inode->i_mode))
			return 0;
	}
	if (inode->i_rdev != dev->devt)
		return 0;

	/* ours */
	return 1;
}

static int handle_remove(const char *nodename, struct device *dev)
{
	struct path parent;
	struct dentry *dentry;
	struct inode *inode;
	int deleted = 0;
	int err = 0;

	dentry = start_removing_path(nodename, &parent);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	inode = d_inode(dentry);
	if (dev_mynode(dev, inode)) {
		struct iattr newattrs;
		/*
		 * before unlinking this node, reset permissions
		 * of possible references like hardlinks
		 */
		newattrs.ia_uid = GLOBAL_ROOT_UID;
		newattrs.ia_gid = GLOBAL_ROOT_GID;
		newattrs.ia_mode = inode->i_mode & ~0777;
		newattrs.ia_valid =
			ATTR_UID|ATTR_GID|ATTR_MODE;
		inode_lock(d_inode(dentry));
		notify_change(&nop_mnt_idmap, dentry, &newattrs, NULL);
		inode_unlock(d_inode(dentry));
		err = vfs_unlink(&nop_mnt_idmap, d_inode(parent.dentry),
				 dentry, NULL);
		if (!err || err == -ENOENT)
			deleted = 1;
	}
	end_removing_path(&parent, dentry);

	if (deleted && strchr(nodename, '/'))
		delete_path(nodename);
	return err;
}

/*
 * If configured, or requested by the commandline, devtmpfs will be
 * auto-mounted after the kernel mounted the root filesystem.
 */
int __init devtmpfs_mount(void)
{
	int err;

	if (!mount_dev)
		return 0;

	if (!thread)
		return 0;

	err = init_mount("devtmpfs", "dev", "devtmpfs", DEVTMPFS_MFLAGS, NULL);
	if (err)
		pr_info("error mounting %d\n", err);
	else
		pr_info("mounted\n");
	return err;
}

static __initdata DECLARE_COMPLETION(setup_done);

static int handle(const char *name, umode_t mode, kuid_t uid, kgid_t gid,
		  struct device *dev)
{
	if (mode)
		return handle_create(name, mode, uid, gid, dev);
	else
		return handle_remove(name, dev);
}

static void __noreturn devtmpfs_work_loop(void)
{
	while (1) {
		spin_lock(&req_lock);
		while (requests) {
			struct req *req = requests;
			requests = NULL;
			spin_unlock(&req_lock);
			while (req) {
				struct req *next = req->next;
				req->err = handle(req->name, req->mode,
						  req->uid, req->gid, req->dev);
				complete(&req->done);
				req = next;
			}
			spin_lock(&req_lock);
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock(&req_lock);
		schedule();
	}
}

static noinline int __init devtmpfs_setup(void *p)
{
	int err;

	err = ksys_unshare(CLONE_NEWNS);
	if (err)
		goto out;
	err = init_mount("devtmpfs", "/", "devtmpfs", DEVTMPFS_MFLAGS, NULL);
	if (err)
		goto out;
	init_chdir("/.."); /* will traverse into overmounted root */
	init_chroot(".");
out:
	*(int *)p = err;
	return err;
}

/*
 * The __ref is because devtmpfs_setup needs to be __init for the routines it
 * calls.  That call is done while devtmpfs_init, which is marked __init,
 * synchronously waits for it to complete.
 */
static int __ref devtmpfsd(void *p)
{
	int err = devtmpfs_setup(p);

	complete(&setup_done);
	if (err)
		return err;
	devtmpfs_work_loop();
	return 0;
}

/*
 * Get the underlying (shmem/ramfs) context ops to build ours
 */
static int devtmpfs_configure_context(void)
{
	struct fs_context *fc;

	fc = fs_context_for_reconfigure(mnt->mnt_root, mnt->mnt_sb->s_flags,
					MS_RMT_MASK);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	/* Set up devtmpfs_context_ops based on underlying type */
	devtmpfs_context_ops.free	      = fc->ops->free;
	devtmpfs_context_ops.dup	      = fc->ops->dup;
	devtmpfs_context_ops.parse_param      = fc->ops->parse_param;
	devtmpfs_context_ops.parse_monolithic = fc->ops->parse_monolithic;
	devtmpfs_context_ops.get_tree	      = &devtmpfs_get_tree;
	devtmpfs_context_ops.reconfigure      = fc->ops->reconfigure;

	put_fs_context(fc);

	return 0;
}

/*
 * Create devtmpfs instance, driver-core devices will add their device
 * nodes here.
 */
int __init devtmpfs_init(void)
{
	char opts[] = "mode=0755";
	int err;

	mnt = vfs_kern_mount(&internal_fs_type, 0, "devtmpfs", opts);
	if (IS_ERR(mnt)) {
		pr_err("unable to create devtmpfs %ld\n", PTR_ERR(mnt));
		return PTR_ERR(mnt);
	}

	err = devtmpfs_configure_context();
	if (err) {
		pr_err("unable to configure devtmpfs type %d\n", err);
		return err;
	}

	err = register_filesystem(&dev_fs_type);
	if (err) {
		pr_err("unable to register devtmpfs type %d\n", err);
		return err;
	}

	thread = kthread_run(devtmpfsd, &err, "kdevtmpfs");
	if (!IS_ERR(thread)) {
		wait_for_completion(&setup_done);
	} else {
		err = PTR_ERR(thread);
		thread = NULL;
	}

	if (err) {
		pr_err("unable to create devtmpfs %d\n", err);
		unregister_filesystem(&dev_fs_type);
		thread = NULL;
		return err;
	}

	pr_info("initialized\n");
	return 0;
}
