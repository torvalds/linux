// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008 Red Hat, Inc. All rights reserved.
 * Copyright 2008 Ian Kent <raven@themaw.net>
 */

#include <linux/miscdevice.h>
#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/magic.h>

#include "autofs_i.h"

/*
 * This module implements an interface for routing autofs ioctl control
 * commands via a miscellaneous device file.
 *
 * The alternate interface is needed because we need to be able open
 * an ioctl file descriptor on an autofs mount that may be covered by
 * another mount. This situation arises when starting automount(8)
 * or other user space daemon which uses direct mounts or offset
 * mounts (used for autofs lazy mount/umount of nested mount trees),
 * which have been left busy at service shutdown.
 */

typedef int (*ioctl_fn)(struct file *, struct autofs_sb_info *,
			struct autofs_dev_ioctl *);

static int check_name(const char *name)
{
	if (!strchr(name, '/'))
		return -EINVAL;
	return 0;
}

/*
 * Check a string doesn't overrun the chunk of
 * memory we copied from user land.
 */
static int invalid_str(char *str, size_t size)
{
	if (memchr(str, 0, size))
		return 0;
	return -EINVAL;
}

/*
 * Check that the user compiled against correct version of autofs
 * misc device code.
 *
 * As well as checking the version compatibility this always copies
 * the kernel interface version out.
 */
static int check_dev_ioctl_version(int cmd, struct autofs_dev_ioctl *param)
{
	int err = 0;

	if ((param->ver_major != AUTOFS_DEV_IOCTL_VERSION_MAJOR) ||
	    (param->ver_minor > AUTOFS_DEV_IOCTL_VERSION_MINOR)) {
		pr_warn("ioctl control interface version mismatch: "
			"kernel(%u.%u), user(%u.%u), cmd(0x%08x)\n",
			AUTOFS_DEV_IOCTL_VERSION_MAJOR,
			AUTOFS_DEV_IOCTL_VERSION_MINOR,
			param->ver_major, param->ver_minor, cmd);
		err = -EINVAL;
	}

	/* Fill in the kernel version. */
	param->ver_major = AUTOFS_DEV_IOCTL_VERSION_MAJOR;
	param->ver_minor = AUTOFS_DEV_IOCTL_VERSION_MINOR;

	return err;
}

/*
 * Copy parameter control struct, including a possible path allocated
 * at the end of the struct.
 */
static struct autofs_dev_ioctl *
copy_dev_ioctl(struct autofs_dev_ioctl __user *in)
{
	struct autofs_dev_ioctl tmp, *res;

	if (copy_from_user(&tmp, in, AUTOFS_DEV_IOCTL_SIZE))
		return ERR_PTR(-EFAULT);

	if (tmp.size < AUTOFS_DEV_IOCTL_SIZE)
		return ERR_PTR(-EINVAL);

	if (tmp.size > AUTOFS_DEV_IOCTL_SIZE + PATH_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	res = memdup_user(in, tmp.size);
	if (!IS_ERR(res))
		res->size = tmp.size;

	return res;
}

static inline void free_dev_ioctl(struct autofs_dev_ioctl *param)
{
	kfree(param);
}

/*
 * Check sanity of parameter control fields and if a path is present
 * check that it is terminated and contains at least one "/".
 */
static int validate_dev_ioctl(int cmd, struct autofs_dev_ioctl *param)
{
	int err;

	err = check_dev_ioctl_version(cmd, param);
	if (err) {
		pr_warn("invalid device control module version "
			"supplied for cmd(0x%08x)\n", cmd);
		goto out;
	}

	if (param->size > AUTOFS_DEV_IOCTL_SIZE) {
		err = invalid_str(param->path, param->size - AUTOFS_DEV_IOCTL_SIZE);
		if (err) {
			pr_warn(
			  "path string terminator missing for cmd(0x%08x)\n",
			  cmd);
			goto out;
		}

		err = check_name(param->path);
		if (err) {
			pr_warn("invalid path supplied for cmd(0x%08x)\n",
				cmd);
			goto out;
		}
	} else {
		unsigned int inr = _IOC_NR(cmd);

		if (inr == AUTOFS_DEV_IOCTL_OPENMOUNT_CMD ||
		    inr == AUTOFS_DEV_IOCTL_REQUESTER_CMD ||
		    inr == AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD) {
			err = -EINVAL;
			goto out;
		}
	}

	err = 0;
out:
	return err;
}

/* Return autofs dev ioctl version */
static int autofs_dev_ioctl_version(struct file *fp,
				    struct autofs_sb_info *sbi,
				    struct autofs_dev_ioctl *param)
{
	/* This should have already been set. */
	param->ver_major = AUTOFS_DEV_IOCTL_VERSION_MAJOR;
	param->ver_minor = AUTOFS_DEV_IOCTL_VERSION_MINOR;
	return 0;
}

/* Return autofs module protocol version */
static int autofs_dev_ioctl_protover(struct file *fp,
				     struct autofs_sb_info *sbi,
				     struct autofs_dev_ioctl *param)
{
	param->protover.version = sbi->version;
	return 0;
}

/* Return autofs module protocol sub version */
static int autofs_dev_ioctl_protosubver(struct file *fp,
					struct autofs_sb_info *sbi,
					struct autofs_dev_ioctl *param)
{
	param->protosubver.sub_version = sbi->sub_version;
	return 0;
}

/* Find the topmost mount satisfying test() */
static int find_autofs_mount(const char *pathname,
			     struct path *res,
			     int test(const struct path *path, void *data),
			     void *data)
{
	struct path path;
	int err;

	err = kern_path(pathname, LOOKUP_MOUNTPOINT, &path);
	if (err)
		return err;
	err = -ENOENT;
	while (path.dentry == path.mnt->mnt_root) {
		if (path.dentry->d_sb->s_magic == AUTOFS_SUPER_MAGIC) {
			if (test(&path, data)) {
				path_get(&path);
				*res = path;
				err = 0;
				break;
			}
		}
		if (!follow_up(&path))
			break;
	}
	path_put(&path);
	return err;
}

static int test_by_dev(const struct path *path, void *p)
{
	return path->dentry->d_sb->s_dev == *(dev_t *)p;
}

static int test_by_type(const struct path *path, void *p)
{
	struct autofs_info *ino = autofs_dentry_ino(path->dentry);

	return ino && ino->sbi->type & *(unsigned *)p;
}

/*
 * Open a file descriptor on the autofs mount point corresponding
 * to the given path and device number (aka. new_encode_dev(sb->s_dev)).
 */
static int autofs_dev_ioctl_open_mountpoint(const char *name, dev_t devid)
{
	int err, fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (likely(fd >= 0)) {
		struct file *filp;
		struct path path;

		err = find_autofs_mount(name, &path, test_by_dev, &devid);
		if (err)
			goto out;

		filp = dentry_open(&path, O_RDONLY, current_cred());
		path_put(&path);
		if (IS_ERR(filp)) {
			err = PTR_ERR(filp);
			goto out;
		}

		fd_install(fd, filp);
	}

	return fd;

out:
	put_unused_fd(fd);
	return err;
}

/* Open a file descriptor on an autofs mount point */
static int autofs_dev_ioctl_openmount(struct file *fp,
				      struct autofs_sb_info *sbi,
				      struct autofs_dev_ioctl *param)
{
	const char *path;
	dev_t devid;
	int err, fd;

	/* param->path has been checked in validate_dev_ioctl() */

	if (!param->openmount.devid)
		return -EINVAL;

	param->ioctlfd = -1;

	path = param->path;
	devid = new_decode_dev(param->openmount.devid);

	err = 0;
	fd = autofs_dev_ioctl_open_mountpoint(path, devid);
	if (unlikely(fd < 0)) {
		err = fd;
		goto out;
	}

	param->ioctlfd = fd;
out:
	return err;
}

/* Close file descriptor allocated above (user can also use close(2)). */
static int autofs_dev_ioctl_closemount(struct file *fp,
				       struct autofs_sb_info *sbi,
				       struct autofs_dev_ioctl *param)
{
	return ksys_close(param->ioctlfd);
}

/*
 * Send "ready" status for an existing wait (either a mount or an expire
 * request).
 */
static int autofs_dev_ioctl_ready(struct file *fp,
				  struct autofs_sb_info *sbi,
				  struct autofs_dev_ioctl *param)
{
	autofs_wqt_t token;

	token = (autofs_wqt_t) param->ready.token;
	return autofs_wait_release(sbi, token, 0);
}

/*
 * Send "fail" status for an existing wait (either a mount or an expire
 * request).
 */
static int autofs_dev_ioctl_fail(struct file *fp,
				 struct autofs_sb_info *sbi,
				 struct autofs_dev_ioctl *param)
{
	autofs_wqt_t token;
	int status;

	token = (autofs_wqt_t) param->fail.token;
	status = param->fail.status < 0 ? param->fail.status : -ENOENT;
	return autofs_wait_release(sbi, token, status);
}

/*
 * Set the pipe fd for kernel communication to the daemon.
 *
 * Normally this is set at mount using an option but if we
 * are reconnecting to a busy mount then we need to use this
 * to tell the autofs mount about the new kernel pipe fd. In
 * order to protect mounts against incorrectly setting the
 * pipefd we also require that the autofs mount be catatonic.
 *
 * This also sets the process group id used to identify the
 * controlling process (eg. the owning automount(8) daemon).
 */
static int autofs_dev_ioctl_setpipefd(struct file *fp,
				      struct autofs_sb_info *sbi,
				      struct autofs_dev_ioctl *param)
{
	int pipefd;
	int err = 0;
	struct pid *new_pid = NULL;

	if (param->setpipefd.pipefd == -1)
		return -EINVAL;

	pipefd = param->setpipefd.pipefd;

	mutex_lock(&sbi->wq_mutex);
	if (!(sbi->flags & AUTOFS_SBI_CATATONIC)) {
		mutex_unlock(&sbi->wq_mutex);
		return -EBUSY;
	} else {
		struct file *pipe;

		new_pid = get_task_pid(current, PIDTYPE_PGID);

		if (ns_of_pid(new_pid) != ns_of_pid(sbi->oz_pgrp)) {
			pr_warn("not allowed to change PID namespace\n");
			err = -EINVAL;
			goto out;
		}

		pipe = fget(pipefd);
		if (!pipe) {
			err = -EBADF;
			goto out;
		}
		if (autofs_prepare_pipe(pipe) < 0) {
			err = -EPIPE;
			fput(pipe);
			goto out;
		}
		swap(sbi->oz_pgrp, new_pid);
		sbi->pipefd = pipefd;
		sbi->pipe = pipe;
		sbi->flags &= ~AUTOFS_SBI_CATATONIC;
	}
out:
	put_pid(new_pid);
	mutex_unlock(&sbi->wq_mutex);
	return err;
}

/*
 * Make the autofs mount point catatonic, no longer responsive to
 * mount requests. Also closes the kernel pipe file descriptor.
 */
static int autofs_dev_ioctl_catatonic(struct file *fp,
				      struct autofs_sb_info *sbi,
				      struct autofs_dev_ioctl *param)
{
	autofs_catatonic_mode(sbi);
	return 0;
}

/* Set the autofs mount timeout */
static int autofs_dev_ioctl_timeout(struct file *fp,
				    struct autofs_sb_info *sbi,
				    struct autofs_dev_ioctl *param)
{
	unsigned long timeout;

	timeout = param->timeout.timeout;
	param->timeout.timeout = sbi->exp_timeout / HZ;
	sbi->exp_timeout = timeout * HZ;
	return 0;
}

/*
 * Return the uid and gid of the last request for the mount
 *
 * When reconstructing an autofs mount tree with active mounts
 * we need to re-connect to mounts that may have used the original
 * process uid and gid (or string variations of them) for mount
 * lookups within the map entry.
 */
static int autofs_dev_ioctl_requester(struct file *fp,
				      struct autofs_sb_info *sbi,
				      struct autofs_dev_ioctl *param)
{
	struct autofs_info *ino;
	struct path path;
	dev_t devid;
	int err = -ENOENT;

	/* param->path has been checked in validate_dev_ioctl() */

	devid = sbi->sb->s_dev;

	param->requester.uid = param->requester.gid = -1;

	err = find_autofs_mount(param->path, &path, test_by_dev, &devid);
	if (err)
		goto out;

	ino = autofs_dentry_ino(path.dentry);
	if (ino) {
		err = 0;
		autofs_expire_wait(&path, 0);
		spin_lock(&sbi->fs_lock);
		param->requester.uid =
			from_kuid_munged(current_user_ns(), ino->uid);
		param->requester.gid =
			from_kgid_munged(current_user_ns(), ino->gid);
		spin_unlock(&sbi->fs_lock);
	}
	path_put(&path);
out:
	return err;
}

/*
 * Call repeatedly until it returns -EAGAIN, meaning there's nothing
 * more that can be done.
 */
static int autofs_dev_ioctl_expire(struct file *fp,
				   struct autofs_sb_info *sbi,
				   struct autofs_dev_ioctl *param)
{
	struct vfsmount *mnt;
	int how;

	how = param->expire.how;
	mnt = fp->f_path.mnt;

	return autofs_do_expire_multi(sbi->sb, mnt, sbi, how);
}

/* Check if autofs mount point is in use */
static int autofs_dev_ioctl_askumount(struct file *fp,
				      struct autofs_sb_info *sbi,
				      struct autofs_dev_ioctl *param)
{
	param->askumount.may_umount = 0;
	if (may_umount(fp->f_path.mnt))
		param->askumount.may_umount = 1;
	return 0;
}

/*
 * Check if the given path is a mountpoint.
 *
 * If we are supplied with the file descriptor of an autofs
 * mount we're looking for a specific mount. In this case
 * the path is considered a mountpoint if it is itself a
 * mountpoint or contains a mount, such as a multi-mount
 * without a root mount. In this case we return 1 if the
 * path is a mount point and the super magic of the covering
 * mount if there is one or 0 if it isn't a mountpoint.
 *
 * If we aren't supplied with a file descriptor then we
 * lookup the path and check if it is the root of a mount.
 * If a type is given we are looking for a particular autofs
 * mount and if we don't find a match we return fail. If the
 * located path is the root of a mount we return 1 along with
 * the super magic of the mount or 0 otherwise.
 *
 * In both cases the device number (as returned by
 * new_encode_dev()) is also returned.
 */
static int autofs_dev_ioctl_ismountpoint(struct file *fp,
					 struct autofs_sb_info *sbi,
					 struct autofs_dev_ioctl *param)
{
	struct path path;
	const char *name;
	unsigned int type;
	unsigned int devid, magic;
	int err = -ENOENT;

	/* param->path has been checked in validate_dev_ioctl() */

	name = param->path;
	type = param->ismountpoint.in.type;

	param->ismountpoint.out.devid = devid = 0;
	param->ismountpoint.out.magic = magic = 0;

	if (!fp || param->ioctlfd == -1) {
		if (autofs_type_any(type))
			err = kern_path(name, LOOKUP_FOLLOW | LOOKUP_MOUNTPOINT,
					&path);
		else
			err = find_autofs_mount(name, &path,
						test_by_type, &type);
		if (err)
			goto out;
		devid = new_encode_dev(path.dentry->d_sb->s_dev);
		err = 0;
		if (path.mnt->mnt_root == path.dentry) {
			err = 1;
			magic = path.dentry->d_sb->s_magic;
		}
	} else {
		dev_t dev = sbi->sb->s_dev;

		err = find_autofs_mount(name, &path, test_by_dev, &dev);
		if (err)
			goto out;

		devid = new_encode_dev(dev);

		err = path_has_submounts(&path);

		if (follow_down_one(&path))
			magic = path.dentry->d_sb->s_magic;
	}

	param->ismountpoint.out.devid = devid;
	param->ismountpoint.out.magic = magic;
	path_put(&path);
out:
	return err;
}

/*
 * Our range of ioctl numbers isn't 0 based so we need to shift
 * the array index by _IOC_NR(AUTOFS_CTL_IOC_FIRST) for the table
 * lookup.
 */
#define cmd_idx(cmd)	(cmd - _IOC_NR(AUTOFS_DEV_IOCTL_IOC_FIRST))

static ioctl_fn lookup_dev_ioctl(unsigned int cmd)
{
	static ioctl_fn _ioctls[] = {
		autofs_dev_ioctl_version,
		autofs_dev_ioctl_protover,
		autofs_dev_ioctl_protosubver,
		autofs_dev_ioctl_openmount,
		autofs_dev_ioctl_closemount,
		autofs_dev_ioctl_ready,
		autofs_dev_ioctl_fail,
		autofs_dev_ioctl_setpipefd,
		autofs_dev_ioctl_catatonic,
		autofs_dev_ioctl_timeout,
		autofs_dev_ioctl_requester,
		autofs_dev_ioctl_expire,
		autofs_dev_ioctl_askumount,
		autofs_dev_ioctl_ismountpoint,
	};
	unsigned int idx = cmd_idx(cmd);

	return (idx >= ARRAY_SIZE(_ioctls)) ? NULL : _ioctls[idx];
}

/* ioctl dispatcher */
static int _autofs_dev_ioctl(unsigned int command,
			     struct autofs_dev_ioctl __user *user)
{
	struct autofs_dev_ioctl *param;
	struct file *fp;
	struct autofs_sb_info *sbi;
	unsigned int cmd_first, cmd;
	ioctl_fn fn = NULL;
	int err = 0;

	cmd_first = _IOC_NR(AUTOFS_DEV_IOCTL_IOC_FIRST);
	cmd = _IOC_NR(command);

	if (_IOC_TYPE(command) != _IOC_TYPE(AUTOFS_DEV_IOCTL_IOC_FIRST) ||
	    cmd - cmd_first > AUTOFS_DEV_IOCTL_IOC_COUNT) {
		return -ENOTTY;
	}

	/* Only root can use ioctls other than AUTOFS_DEV_IOCTL_VERSION_CMD
	 * and AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD
	 */
	if (cmd != AUTOFS_DEV_IOCTL_VERSION_CMD &&
	    cmd != AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Copy the parameters into kernel space. */
	param = copy_dev_ioctl(user);
	if (IS_ERR(param))
		return PTR_ERR(param);

	err = validate_dev_ioctl(command, param);
	if (err)
		goto out;

	fn = lookup_dev_ioctl(cmd);
	if (!fn) {
		pr_warn("unknown command 0x%08x\n", command);
		err = -ENOTTY;
		goto out;
	}

	fp = NULL;
	sbi = NULL;

	/*
	 * For obvious reasons the openmount can't have a file
	 * descriptor yet. We don't take a reference to the
	 * file during close to allow for immediate release,
	 * and the same for retrieving ioctl version.
	 */
	if (cmd != AUTOFS_DEV_IOCTL_VERSION_CMD &&
	    cmd != AUTOFS_DEV_IOCTL_OPENMOUNT_CMD &&
	    cmd != AUTOFS_DEV_IOCTL_CLOSEMOUNT_CMD) {
		struct super_block *sb;

		fp = fget(param->ioctlfd);
		if (!fp) {
			if (cmd == AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD)
				goto cont;
			err = -EBADF;
			goto out;
		}

		sb = file_inode(fp)->i_sb;
		if (sb->s_type != &autofs_fs_type) {
			err = -EINVAL;
			fput(fp);
			goto out;
		}
		sbi = autofs_sbi(sb);

		/*
		 * Admin needs to be able to set the mount catatonic in
		 * order to be able to perform the re-open.
		 */
		if (!autofs_oz_mode(sbi) &&
		    cmd != AUTOFS_DEV_IOCTL_CATATONIC_CMD) {
			err = -EACCES;
			fput(fp);
			goto out;
		}
	}
cont:
	err = fn(fp, sbi, param);

	if (fp)
		fput(fp);
	if (err >= 0 && copy_to_user(user, param, AUTOFS_DEV_IOCTL_SIZE))
		err = -EFAULT;
out:
	free_dev_ioctl(param);
	return err;
}

static long autofs_dev_ioctl(struct file *file, unsigned int command,
			     unsigned long u)
{
	int err;

	err = _autofs_dev_ioctl(command, (struct autofs_dev_ioctl __user *) u);
	return (long) err;
}

#ifdef CONFIG_COMPAT
static long autofs_dev_ioctl_compat(struct file *file, unsigned int command,
				    unsigned long u)
{
	return autofs_dev_ioctl(file, command, (unsigned long) compat_ptr(u));
}
#else
#define autofs_dev_ioctl_compat NULL
#endif

static const struct file_operations _dev_ioctl_fops = {
	.unlocked_ioctl	 = autofs_dev_ioctl,
	.compat_ioctl = autofs_dev_ioctl_compat,
	.owner	 = THIS_MODULE,
	.llseek = noop_llseek,
};

static struct miscdevice _autofs_dev_ioctl_misc = {
	.minor		= AUTOFS_MINOR,
	.name		= AUTOFS_DEVICE_NAME,
	.fops		= &_dev_ioctl_fops,
	.mode           = 0644,
};

MODULE_ALIAS_MISCDEV(AUTOFS_MINOR);
MODULE_ALIAS("devname:autofs");

/* Register/deregister misc character device */
int __init autofs_dev_ioctl_init(void)
{
	int r;

	r = misc_register(&_autofs_dev_ioctl_misc);
	if (r) {
		pr_err("misc_register failed for control device\n");
		return r;
	}

	return 0;
}

void autofs_dev_ioctl_exit(void)
{
	misc_deregister(&_autofs_dev_ioctl_misc);
}
