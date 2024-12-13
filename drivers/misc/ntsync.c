// SPDX-License-Identifier: GPL-2.0-only
/*
 * ntsync.c - Kernel driver for NT synchronization primitives
 *
 * Copyright (C) 2024 Elizabeth Figura <zfigura@codeweavers.com>
 */

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
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
 * Wait operations take a reference to each object being waited on for
 * the duration of the wait.
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

	struct list_head any_waiters;
};

struct ntsync_q_entry {
	struct list_head node;
	struct ntsync_q *q;
	struct ntsync_obj *obj;
	__u32 index;
};

struct ntsync_q {
	struct task_struct *task;

	/*
	 * Protected via atomic_try_cmpxchg(). Only the thread that wins the
	 * compare-and-swap may actually change object states and wake this
	 * task.
	 */
	atomic_t signaled;

	__u32 count;
	struct ntsync_q_entry entries[];
};

struct ntsync_device {
	struct file *file;
};

static void try_wake_any_sem(struct ntsync_obj *sem)
{
	struct ntsync_q_entry *entry;

	lockdep_assert_held(&sem->lock);

	list_for_each_entry(entry, &sem->any_waiters, node) {
		struct ntsync_q *q = entry->q;
		int signaled = -1;

		if (!sem->u.sem.count)
			break;

		if (atomic_try_cmpxchg(&q->signaled, &signaled, entry->index)) {
			sem->u.sem.count--;
			wake_up_process(q->task);
		}
	}
}

/*
 * Actually change the semaphore state, returning -EOVERFLOW if it is made
 * invalid.
 */
static int release_sem_state(struct ntsync_obj *sem, __u32 count)
{
	__u32 sum;

	lockdep_assert_held(&sem->lock);

	if (check_add_overflow(sem->u.sem.count, count, &sum) ||
	    sum > sem->u.sem.max)
		return -EOVERFLOW;

	sem->u.sem.count = sum;
	return 0;
}

static int ntsync_sem_release(struct ntsync_obj *sem, void __user *argp)
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
	if (!ret)
		try_wake_any_sem(sem);

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
	case NTSYNC_IOC_SEM_RELEASE:
		return ntsync_sem_release(obj, argp);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct file_operations ntsync_obj_fops = {
	.owner		= THIS_MODULE,
	.release	= ntsync_obj_release,
	.unlocked_ioctl	= ntsync_obj_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
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
	INIT_LIST_HEAD(&obj->any_waiters);

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
	if (fd < 0)
		kfree(sem);

	return fd;
}

static struct ntsync_obj *get_obj(struct ntsync_device *dev, int fd)
{
	struct file *file = fget(fd);
	struct ntsync_obj *obj;

	if (!file)
		return NULL;

	if (file->f_op != &ntsync_obj_fops) {
		fput(file);
		return NULL;
	}

	obj = file->private_data;
	if (obj->dev != dev) {
		fput(file);
		return NULL;
	}

	return obj;
}

static void put_obj(struct ntsync_obj *obj)
{
	fput(obj->file);
}

static int ntsync_schedule(const struct ntsync_q *q, const struct ntsync_wait_args *args)
{
	ktime_t timeout = ns_to_ktime(args->timeout);
	clockid_t clock = CLOCK_MONOTONIC;
	ktime_t *timeout_ptr;
	int ret = 0;

	timeout_ptr = (args->timeout == U64_MAX ? NULL : &timeout);

	if (args->flags & NTSYNC_WAIT_REALTIME)
		clock = CLOCK_REALTIME;

	do {
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (atomic_read(&q->signaled) != -1) {
			ret = 0;
			break;
		}
		ret = schedule_hrtimeout_range_clock(timeout_ptr, 0, HRTIMER_MODE_ABS, clock);
	} while (ret < 0);
	__set_current_state(TASK_RUNNING);

	return ret;
}

/*
 * Allocate and initialize the ntsync_q structure, but do not queue us yet.
 */
static int setup_wait(struct ntsync_device *dev,
		      const struct ntsync_wait_args *args,
		      struct ntsync_q **ret_q)
{
	const __u32 count = args->count;
	int fds[NTSYNC_MAX_WAIT_COUNT];
	struct ntsync_q *q;
	__u32 i, j;

	if (args->pad[0] || args->pad[1] || args->pad[2] || (args->flags & ~NTSYNC_WAIT_REALTIME))
		return -EINVAL;

	if (args->count > NTSYNC_MAX_WAIT_COUNT)
		return -EINVAL;

	if (copy_from_user(fds, u64_to_user_ptr(args->objs),
			   array_size(count, sizeof(*fds))))
		return -EFAULT;

	q = kmalloc(struct_size(q, entries, count), GFP_KERNEL);
	if (!q)
		return -ENOMEM;
	q->task = current;
	atomic_set(&q->signaled, -1);
	q->count = count;

	for (i = 0; i < count; i++) {
		struct ntsync_q_entry *entry = &q->entries[i];
		struct ntsync_obj *obj = get_obj(dev, fds[i]);

		if (!obj)
			goto err;

		entry->obj = obj;
		entry->q = q;
		entry->index = i;
	}

	*ret_q = q;
	return 0;

err:
	for (j = 0; j < i; j++)
		put_obj(q->entries[j].obj);
	kfree(q);
	return -EINVAL;
}

static void try_wake_any_obj(struct ntsync_obj *obj)
{
	switch (obj->type) {
	case NTSYNC_TYPE_SEM:
		try_wake_any_sem(obj);
		break;
	}
}

static int ntsync_wait_any(struct ntsync_device *dev, void __user *argp)
{
	struct ntsync_wait_args args;
	struct ntsync_q *q;
	int signaled;
	__u32 i;
	int ret;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	ret = setup_wait(dev, &args, &q);
	if (ret < 0)
		return ret;

	/* queue ourselves */

	for (i = 0; i < args.count; i++) {
		struct ntsync_q_entry *entry = &q->entries[i];
		struct ntsync_obj *obj = entry->obj;

		spin_lock(&obj->lock);
		list_add_tail(&entry->node, &obj->any_waiters);
		spin_unlock(&obj->lock);
	}

	/* check if we are already signaled */

	for (i = 0; i < args.count; i++) {
		struct ntsync_obj *obj = q->entries[i].obj;

		if (atomic_read(&q->signaled) != -1)
			break;

		spin_lock(&obj->lock);
		try_wake_any_obj(obj);
		spin_unlock(&obj->lock);
	}

	/* sleep */

	ret = ntsync_schedule(q, &args);

	/* and finally, unqueue */

	for (i = 0; i < args.count; i++) {
		struct ntsync_q_entry *entry = &q->entries[i];
		struct ntsync_obj *obj = entry->obj;

		spin_lock(&obj->lock);
		list_del(&entry->node);
		spin_unlock(&obj->lock);

		put_obj(obj);
	}

	signaled = atomic_read(&q->signaled);
	if (signaled != -1) {
		struct ntsync_wait_args __user *user_args = argp;

		/* even if we caught a signal, we need to communicate success */
		ret = 0;

		if (put_user(signaled, &user_args->index))
			ret = -EFAULT;
	} else if (!ret) {
		ret = -ETIMEDOUT;
	}

	kfree(q);
	return ret;
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
	case NTSYNC_IOC_WAIT_ANY:
		return ntsync_wait_any(dev, argp);
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
