// SPDX-License-Identifier: GPL-2.0-only
/*
 * /dev/mcelog driver
 *
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s).
 * 2004 Andi Kleen. Rewrote most of it.
 * Copyright 2008 Intel Corporation
 * Author: Andi Kleen
 */

#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/poll.h>

#include "internal.h"

static BLOCKING_NOTIFIER_HEAD(mce_injector_chain);

static DEFINE_MUTEX(mce_chrdev_read_mutex);

static char mce_helper[128];
static char *mce_helper_argv[2] = { mce_helper, NULL };

/*
 * Lockless MCE logging infrastructure.
 * This avoids deadlocks on printk locks without having to break locks. Also
 * separate MCEs from kernel messages to avoid bogus bug reports.
 */

static struct mce_log_buffer *mcelog;

static DECLARE_WAIT_QUEUE_HEAD(mce_chrdev_wait);

static int dev_mce_log(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct mce *mce = (struct mce *)data;
	unsigned int entry;

	mutex_lock(&mce_chrdev_read_mutex);

	entry = mcelog->next;

	/*
	 * When the buffer fills up discard new entries. Assume that the
	 * earlier errors are the more interesting ones:
	 */
	if (entry >= mcelog->len) {
		set_bit(MCE_OVERFLOW, (unsigned long *)&mcelog->flags);
		goto unlock;
	}

	mcelog->next = entry + 1;

	memcpy(mcelog->entry + entry, mce, sizeof(struct mce));
	mcelog->entry[entry].finished = 1;

	/* wake processes polling /dev/mcelog */
	wake_up_interruptible(&mce_chrdev_wait);

unlock:
	mutex_unlock(&mce_chrdev_read_mutex);

	return NOTIFY_OK;
}

static struct notifier_block dev_mcelog_nb = {
	.notifier_call	= dev_mce_log,
	.priority	= MCE_PRIO_MCELOG,
};

static void mce_do_trigger(struct work_struct *work)
{
	call_usermodehelper(mce_helper, mce_helper_argv, NULL, UMH_NO_WAIT);
}

static DECLARE_WORK(mce_trigger_work, mce_do_trigger);


void mce_work_trigger(void)
{
	if (mce_helper[0])
		schedule_work(&mce_trigger_work);
}

static ssize_t
show_trigger(struct device *s, struct device_attribute *attr, char *buf)
{
	strcpy(buf, mce_helper);
	strcat(buf, "\n");
	return strlen(mce_helper) + 1;
}

static ssize_t set_trigger(struct device *s, struct device_attribute *attr,
				const char *buf, size_t siz)
{
	char *p;

	strncpy(mce_helper, buf, sizeof(mce_helper));
	mce_helper[sizeof(mce_helper)-1] = 0;
	p = strchr(mce_helper, '\n');

	if (p)
		*p = 0;

	return strlen(mce_helper) + !!p;
}

DEVICE_ATTR(trigger, 0644, show_trigger, set_trigger);

/*
 * mce_chrdev: Character device /dev/mcelog to read and clear the MCE log.
 */

static DEFINE_SPINLOCK(mce_chrdev_state_lock);
static int mce_chrdev_open_count;	/* #times opened */
static int mce_chrdev_open_exclu;	/* already open exclusive? */

static int mce_chrdev_open(struct inode *inode, struct file *file)
{
	spin_lock(&mce_chrdev_state_lock);

	if (mce_chrdev_open_exclu ||
	    (mce_chrdev_open_count && (file->f_flags & O_EXCL))) {
		spin_unlock(&mce_chrdev_state_lock);

		return -EBUSY;
	}

	if (file->f_flags & O_EXCL)
		mce_chrdev_open_exclu = 1;
	mce_chrdev_open_count++;

	spin_unlock(&mce_chrdev_state_lock);

	return nonseekable_open(inode, file);
}

static int mce_chrdev_release(struct inode *inode, struct file *file)
{
	spin_lock(&mce_chrdev_state_lock);

	mce_chrdev_open_count--;
	mce_chrdev_open_exclu = 0;

	spin_unlock(&mce_chrdev_state_lock);

	return 0;
}

static int mce_apei_read_done;

/* Collect MCE record of previous boot in persistent storage via APEI ERST. */
static int __mce_read_apei(char __user **ubuf, size_t usize)
{
	int rc;
	u64 record_id;
	struct mce m;

	if (usize < sizeof(struct mce))
		return -EINVAL;

	rc = apei_read_mce(&m, &record_id);
	/* Error or no more MCE record */
	if (rc <= 0) {
		mce_apei_read_done = 1;
		/*
		 * When ERST is disabled, mce_chrdev_read() should return
		 * "no record" instead of "no device."
		 */
		if (rc == -ENODEV)
			return 0;
		return rc;
	}
	rc = -EFAULT;
	if (copy_to_user(*ubuf, &m, sizeof(struct mce)))
		return rc;
	/*
	 * In fact, we should have cleared the record after that has
	 * been flushed to the disk or sent to network in
	 * /sbin/mcelog, but we have no interface to support that now,
	 * so just clear it to avoid duplication.
	 */
	rc = apei_clear_mce(record_id);
	if (rc) {
		mce_apei_read_done = 1;
		return rc;
	}
	*ubuf += sizeof(struct mce);

	return 0;
}

static ssize_t mce_chrdev_read(struct file *filp, char __user *ubuf,
				size_t usize, loff_t *off)
{
	char __user *buf = ubuf;
	unsigned next;
	int i, err;

	mutex_lock(&mce_chrdev_read_mutex);

	if (!mce_apei_read_done) {
		err = __mce_read_apei(&buf, usize);
		if (err || buf != ubuf)
			goto out;
	}

	/* Only supports full reads right now */
	err = -EINVAL;
	if (*off != 0 || usize < mcelog->len * sizeof(struct mce))
		goto out;

	next = mcelog->next;
	err = 0;

	for (i = 0; i < next; i++) {
		struct mce *m = &mcelog->entry[i];

		err |= copy_to_user(buf, m, sizeof(*m));
		buf += sizeof(*m);
	}

	memset(mcelog->entry, 0, next * sizeof(struct mce));
	mcelog->next = 0;

	if (err)
		err = -EFAULT;

out:
	mutex_unlock(&mce_chrdev_read_mutex);

	return err ? err : buf - ubuf;
}

static __poll_t mce_chrdev_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &mce_chrdev_wait, wait);
	if (READ_ONCE(mcelog->next))
		return EPOLLIN | EPOLLRDNORM;
	if (!mce_apei_read_done && apei_check_mce())
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static long mce_chrdev_ioctl(struct file *f, unsigned int cmd,
				unsigned long arg)
{
	int __user *p = (int __user *)arg;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case MCE_GET_RECORD_LEN:
		return put_user(sizeof(struct mce), p);
	case MCE_GET_LOG_LEN:
		return put_user(mcelog->len, p);
	case MCE_GETCLEAR_FLAGS: {
		unsigned flags;

		do {
			flags = mcelog->flags;
		} while (cmpxchg(&mcelog->flags, flags, 0) != flags);

		return put_user(flags, p);
	}
	default:
		return -ENOTTY;
	}
}

void mce_register_injector_chain(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&mce_injector_chain, nb);
}
EXPORT_SYMBOL_GPL(mce_register_injector_chain);

void mce_unregister_injector_chain(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&mce_injector_chain, nb);
}
EXPORT_SYMBOL_GPL(mce_unregister_injector_chain);

static ssize_t mce_chrdev_write(struct file *filp, const char __user *ubuf,
				size_t usize, loff_t *off)
{
	struct mce m;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*
	 * There are some cases where real MSR reads could slip
	 * through.
	 */
	if (!boot_cpu_has(X86_FEATURE_MCE) || !boot_cpu_has(X86_FEATURE_MCA))
		return -EIO;

	if ((unsigned long)usize > sizeof(struct mce))
		usize = sizeof(struct mce);
	if (copy_from_user(&m, ubuf, usize))
		return -EFAULT;

	if (m.extcpu >= num_possible_cpus() || !cpu_online(m.extcpu))
		return -EINVAL;

	/*
	 * Need to give user space some time to set everything up,
	 * so do it a jiffie or two later everywhere.
	 */
	schedule_timeout(2);

	blocking_notifier_call_chain(&mce_injector_chain, 0, &m);

	return usize;
}

static const struct file_operations mce_chrdev_ops = {
	.open			= mce_chrdev_open,
	.release		= mce_chrdev_release,
	.read			= mce_chrdev_read,
	.write			= mce_chrdev_write,
	.poll			= mce_chrdev_poll,
	.unlocked_ioctl		= mce_chrdev_ioctl,
	.llseek			= no_llseek,
};

static struct miscdevice mce_chrdev_device = {
	MISC_MCELOG_MINOR,
	"mcelog",
	&mce_chrdev_ops,
};

static __init int dev_mcelog_init_device(void)
{
	int mce_log_len;
	int err;

	mce_log_len = max(MCE_LOG_MIN_LEN, num_online_cpus());
	mcelog = kzalloc(sizeof(*mcelog) + mce_log_len * sizeof(struct mce), GFP_KERNEL);
	if (!mcelog)
		return -ENOMEM;

	strncpy(mcelog->signature, MCE_LOG_SIGNATURE, sizeof(mcelog->signature));
	mcelog->len = mce_log_len;
	mcelog->recordlen = sizeof(struct mce);

	/* register character device /dev/mcelog */
	err = misc_register(&mce_chrdev_device);
	if (err) {
		if (err == -EBUSY)
			/* Xen dom0 might have registered the device already. */
			pr_info("Unable to init device /dev/mcelog, already registered");
		else
			pr_err("Unable to init device /dev/mcelog (rc: %d)\n", err);

		kfree(mcelog);
		return err;
	}

	mce_register_decode_chain(&dev_mcelog_nb);
	return 0;
}
device_initcall_sync(dev_mcelog_init_device);
