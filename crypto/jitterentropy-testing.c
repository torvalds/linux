/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Test interface for Jitter RNG.
 *
 * Copyright (C) 2023, Stephan Mueller <smueller@chronox.de>
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "jitterentropy.h"

#define JENT_TEST_RINGBUFFER_SIZE	(1<<10)
#define JENT_TEST_RINGBUFFER_MASK	(JENT_TEST_RINGBUFFER_SIZE - 1)

struct jent_testing {
	u32 jent_testing_rb[JENT_TEST_RINGBUFFER_SIZE];
	u32 rb_reader;
	atomic_t rb_writer;
	atomic_t jent_testing_enabled;
	spinlock_t lock;
	wait_queue_head_t read_wait;
};

static struct dentry *jent_raw_debugfs_root = NULL;

/*************************** Generic Data Handling ****************************/

/*
 * boot variable:
 * 0 ==> No boot test, gathering of runtime data allowed
 * 1 ==> Boot test enabled and ready for collecting data, gathering runtime
 *	 data is disabled
 * 2 ==> Boot test completed and disabled, gathering of runtime data is
 *	 disabled
 */

static void jent_testing_reset(struct jent_testing *data)
{
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	data->rb_reader = 0;
	atomic_set(&data->rb_writer, 0);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void jent_testing_data_init(struct jent_testing *data, u32 boot)
{
	/*
	 * The boot time testing implies we have a running test. If the
	 * caller wants to clear it, he has to unset the boot_test flag
	 * at runtime via sysfs to enable regular runtime testing
	 */
	if (boot)
		return;

	jent_testing_reset(data);
	atomic_set(&data->jent_testing_enabled, 1);
	pr_warn("Enabling data collection\n");
}

static void jent_testing_fini(struct jent_testing *data, u32 boot)
{
	/* If we have boot data, we do not reset yet to allow data to be read */
	if (boot)
		return;

	atomic_set(&data->jent_testing_enabled, 0);
	jent_testing_reset(data);
	pr_warn("Disabling data collection\n");
}

static bool jent_testing_store(struct jent_testing *data, u32 value,
			       u32 *boot)
{
	unsigned long flags;

	if (!atomic_read(&data->jent_testing_enabled) && (*boot != 1))
		return false;

	spin_lock_irqsave(&data->lock, flags);

	/*
	 * Disable entropy testing for boot time testing after ring buffer
	 * is filled.
	 */
	if (*boot) {
		if (((u32)atomic_read(&data->rb_writer)) >
		     JENT_TEST_RINGBUFFER_SIZE) {
			*boot = 2;
			pr_warn_once("One time data collection test disabled\n");
			spin_unlock_irqrestore(&data->lock, flags);
			return false;
		}

		if (atomic_read(&data->rb_writer) == 1)
			pr_warn("One time data collection test enabled\n");
	}

	data->jent_testing_rb[((u32)atomic_read(&data->rb_writer)) &
			      JENT_TEST_RINGBUFFER_MASK] = value;
	atomic_inc(&data->rb_writer);

	spin_unlock_irqrestore(&data->lock, flags);

	if (wq_has_sleeper(&data->read_wait))
		wake_up_interruptible(&data->read_wait);

	return true;
}

static bool jent_testing_have_data(struct jent_testing *data)
{
	return ((((u32)atomic_read(&data->rb_writer)) &
		 JENT_TEST_RINGBUFFER_MASK) !=
		 (data->rb_reader & JENT_TEST_RINGBUFFER_MASK));
}

static int jent_testing_reader(struct jent_testing *data, u32 *boot,
			       u8 *outbuf, u32 outbuflen)
{
	unsigned long flags;
	int collected_data = 0;

	jent_testing_data_init(data, *boot);

	while (outbuflen) {
		u32 writer = (u32)atomic_read(&data->rb_writer);

		spin_lock_irqsave(&data->lock, flags);

		/* We have no data or reached the writer. */
		if (!writer || (writer == data->rb_reader)) {

			spin_unlock_irqrestore(&data->lock, flags);

			/*
			 * Now we gathered all boot data, enable regular data
			 * collection.
			 */
			if (*boot) {
				*boot = 0;
				goto out;
			}

			wait_event_interruptible(data->read_wait,
						 jent_testing_have_data(data));
			if (signal_pending(current)) {
				collected_data = -ERESTARTSYS;
				goto out;
			}

			continue;
		}

		/* We copy out word-wise */
		if (outbuflen < sizeof(u32)) {
			spin_unlock_irqrestore(&data->lock, flags);
			goto out;
		}

		memcpy(outbuf, &data->jent_testing_rb[data->rb_reader],
		       sizeof(u32));
		data->rb_reader++;

		spin_unlock_irqrestore(&data->lock, flags);

		outbuf += sizeof(u32);
		outbuflen -= sizeof(u32);
		collected_data += sizeof(u32);
	}

out:
	jent_testing_fini(data, *boot);
	return collected_data;
}

static int jent_testing_extract_user(struct file *file, char __user *buf,
				     size_t nbytes, loff_t *ppos,
				     int (*reader)(u8 *outbuf, u32 outbuflen))
{
	u8 *tmp, *tmp_aligned;
	int ret = 0, large_request = (nbytes > 256);

	if (!nbytes)
		return 0;

	/*
	 * The intention of this interface is for collecting at least
	 * 1000 samples due to the SP800-90B requirements. So, we make no
	 * effort in avoiding allocating more memory that actually needed
	 * by the user. Hence, we allocate sufficient memory to always hold
	 * that amount of data.
	 */
	tmp = kmalloc(JENT_TEST_RINGBUFFER_SIZE + sizeof(u32), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp_aligned = PTR_ALIGN(tmp, sizeof(u32));

	while (nbytes) {
		int i;

		if (large_request && need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			schedule();
		}

		i = min_t(int, nbytes, JENT_TEST_RINGBUFFER_SIZE);
		i = reader(tmp_aligned, i);
		if (i <= 0) {
			if (i < 0)
				ret = i;
			break;
		}
		if (copy_to_user(buf, tmp_aligned, i)) {
			ret = -EFAULT;
			break;
		}

		nbytes -= i;
		buf += i;
		ret += i;
	}

	kfree_sensitive(tmp);

	if (ret > 0)
		*ppos += ret;

	return ret;
}

/************** Raw High-Resolution Timer Entropy Data Handling **************/

static u32 boot_raw_hires_test = 0;
module_param(boot_raw_hires_test, uint, 0644);
MODULE_PARM_DESC(boot_raw_hires_test,
		 "Enable gathering boot time high resolution timer entropy of the first Jitter RNG entropy events");

static struct jent_testing jent_raw_hires = {
	.rb_reader = 0,
	.rb_writer = ATOMIC_INIT(0),
	.lock      = __SPIN_LOCK_UNLOCKED(jent_raw_hires.lock),
	.read_wait = __WAIT_QUEUE_HEAD_INITIALIZER(jent_raw_hires.read_wait)
};

int jent_raw_hires_entropy_store(__u32 value)
{
	return jent_testing_store(&jent_raw_hires, value, &boot_raw_hires_test);
}
EXPORT_SYMBOL(jent_raw_hires_entropy_store);

static int jent_raw_hires_entropy_reader(u8 *outbuf, u32 outbuflen)
{
	return jent_testing_reader(&jent_raw_hires, &boot_raw_hires_test,
				   outbuf, outbuflen);
}

static ssize_t jent_raw_hires_read(struct file *file, char __user *to,
				   size_t count, loff_t *ppos)
{
	return jent_testing_extract_user(file, to, count, ppos,
					 jent_raw_hires_entropy_reader);
}

static const struct file_operations jent_raw_hires_fops = {
	.owner = THIS_MODULE,
	.read = jent_raw_hires_read,
};

/******************************* Initialization *******************************/

void jent_testing_init(void)
{
	jent_raw_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);

	debugfs_create_file_unsafe("jent_raw_hires", 0400,
				   jent_raw_debugfs_root, NULL,
				   &jent_raw_hires_fops);
}
EXPORT_SYMBOL(jent_testing_init);

void jent_testing_exit(void)
{
	debugfs_remove_recursive(jent_raw_debugfs_root);
}
EXPORT_SYMBOL(jent_testing_exit);
