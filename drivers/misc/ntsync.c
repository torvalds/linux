// SPDX-License-Identifier: GPL-2.0-only
/*
 * ntsync.c - Kernel driver for NT synchronization primitives
 *
 * Copyright (C) 2024 Elizabeth Figura <zfigura@codeweavers.com>
 */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <uapi/linux/ntsync.h>

#define NTSYNC_NAME	"ntsync"

enum ntsync_type {
	NTSYNC_TYPE_SEM,
};

/*
 * Individual synchronization primitives are represented by
 * struct ntsync_obj, and each primitive is backed by a file.
 *
 * The whole namespace is represented by a struct ntsync_device also
 * backed by a file.
 *
 * Both rely on struct file for reference counting. Individual
 * ntsync_obj objects take a reference to the device when created.
 */

struct ntsync_obj {
	spinlock_t lock;

	enum ntsync_type type;

	struct file *file;
	struct ntsync_device *dev;

	/* The following fields are protected by the object lock. */
	union {
		struct {
			__u32 count;
			__u32 max;
		} sem;
	} u;
};

struct ntsync_device {
	struct file *file;
};

/*
 * Actually change the semaphore state, returning -EOVERFLOW if it is made
 * invalid.
 */
static int post_sem_state(struct ntsync_obj *sem, __u32 count)
{
	__u32 sum;

	lockdep_assert_held(&sem->lock);

	if (check_add_overflow(sem->u.sem.count, count, &sum) ||
	    sum > sem->u.sem.max)
		return -EOVERFLOW;

	sem->u.sem.count = sum;
	return 0;
}

static int ntsync_sem_post(struct ntsync_obj *sem, void __user *argp)
{
	__u32 __user *user_args = argp;
	__u32 prev_count;
	__u32 args;
	int ret;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (sem->type != NTSYNC_TYPE_SEM)
		return -EINVAL;

	spin_lock(&sem->lock);

	prev_count = sem->u.sem.count;
	ret = post_sem_state(sem, args);

	spin_unlock(&sem->lock);

	if (!ret && put_user(prev_count, user_args))
		ret = -EFAULT;

	return ret;
}

static int ntsync_obj_release(struct inode *inode, struct file *file)
{
	struct ntsync_obj *obj = file->private_data;

	fput(obj->dev->file);
	kfree(obj);

	return 0;
}

static long ntsync_obj_ioctl(struct file *file, unsigned int cmd,
			     unsigned long parm)
{
	struct ntsync_obj *obj = file->private_data;
	void __user *argp = (void __user *)parm;

	switch (cmd) {
	case NTSYNC_IOC_SEM_POST:
		return ntsync_sem_post(obj, argp);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations ntsync_obj_fops = {
	.owner		= THIS_MODULE,
	.release	= ntsync_obj_release,
	.unlocked_ioctl	= ntsync_obj_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= no_llseek,
};

static struct ntsync_obj *ntsync_alloc_obj(struct ntsync_device *dev,
					   enum ntsync_type type)
{
	struct ntsync_obj *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;
	obj->type = type;
	obj->dev = dev;
	get_file(dev->file);
	spin_lock_init(&obj->lock);

	return obj;
}

static int ntsync_obj_get_fd(struct ntsync_obj *obj)
{
	struct file *file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;
	file = anon_inode_getfile("ntsync", &ntsync_obj_fops, obj, O_RDWR);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}
	obj->file = file;
	fd_install(fd, file);

	return fd;
}

static int ntsync_create_sem(struct ntsync_device *dev, void __user *argp)
{
	struct ntsync_sem_args __user *user_args = argp;
	struct ntsync_sem_args args;
	struct ntsync_obj *sem;
	int fd;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (args.count > args.max)
		return -EINVAL;

	sem = ntsync_alloc_obj(dev, NTSYNC_TYPE_SEM);
	if (!sem)
		return -ENOMEM;
	sem->u.sem.count = args.count;
	sem->u.sem.max = args.max;
	fd = ntsync_obj_get_fd(sem);
	if (fd < 0) {
		kfree(sem);
		return fd;
	}

	return put_user(fd, &user_args->sem);
}

static int ntsync_char_open(struct inode *inode, struct file *file)
{
	struct ntsync_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	file->private_data = dev;
	dev->file = file;
	return nonseekable_open(inode, file);
}

static int ntsync_char_release(struct inode *inode, struct file *file)
{
	struct ntsync_device *dev = file->private_data;

	kfree(dev);

	return 0;
}

static long ntsync_char_ioctl(struct file *file, unsigned int cmd,
			      unsigned long parm)
{
	struct ntsync_device *dev = file->private_data;
	void __user *argp = (void __user *)parm;

	switch (cmd) {
	case NTSYNC_IOC_CREATE_SEM:
		return ntsync_create_sem(dev, argp);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations ntsync_fops = {
	.owner		= THIS_MODULE,
	.open		= ntsync_char_open,
	.release	= ntsync_char_release,
	.unlocked_ioctl	= ntsync_char_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice ntsync_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= NTSYNC_NAME,
	.fops		= &ntsync_fops,
};

module_misc_device(ntsync_misc);

MODULE_AUTHOR("Elizabeth Figura <zfigura@codeweavers.com>");
MODULE_DESCRIPTION("Kernel driver for NT synchronization primitives");
MODULE_LICENSE("GPL");
