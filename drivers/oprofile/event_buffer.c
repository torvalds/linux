/**
 * @file event_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 *
 * This is the global event buffer that the user-space
 * daemon reads from. The event buffer is an untyped array
 * of unsigned longs. Entries are prefixed by the
 * escape value ESCAPE_CODE followed by an identifying code.
 */

#include <linux/vmalloc.h>
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/dcookies.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
 
#include "oprof.h"
#include "event_buffer.h"
#include "oprofile_stats.h"

DEFINE_MUTEX(buffer_mutex);
 
static unsigned long buffer_opened;
static DECLARE_WAIT_QUEUE_HEAD(buffer_wait);
static unsigned long * event_buffer;
static unsigned long buffer_size;
static unsigned long buffer_watershed;
static size_t buffer_pos;
/* atomic_t because wait_event checks it outside of buffer_mutex */
static atomic_t buffer_ready = ATOMIC_INIT(0);

/* Add an entry to the event buffer. When we
 * get near to the end we wake up the process
 * sleeping on the read() of the file.
 */
void add_event_entry(unsigned long value)
{
	if (buffer_pos == buffer_size) {
		atomic_inc(&oprofile_stats.event_lost_overflow);
		return;
	}

	event_buffer[buffer_pos] = value;
	if (++buffer_pos == buffer_size - buffer_watershed) {
		atomic_set(&buffer_ready, 1);
		wake_up(&buffer_wait);
	}
}


/* Wake up the waiting process if any. This happens
 * on "echo 0 >/dev/oprofile/enable" so the daemon
 * processes the data remaining in the event buffer.
 */
void wake_up_buffer_waiter(void)
{
	mutex_lock(&buffer_mutex);
	atomic_set(&buffer_ready, 1);
	wake_up(&buffer_wait);
	mutex_unlock(&buffer_mutex);
}

 
int alloc_event_buffer(void)
{
	int err = -ENOMEM;
	unsigned long flags;

	spin_lock_irqsave(&oprofilefs_lock, flags);
	buffer_size = fs_buffer_size;
	buffer_watershed = fs_buffer_watershed;
	spin_unlock_irqrestore(&oprofilefs_lock, flags);
 
	if (buffer_watershed >= buffer_size)
		return -EINVAL;
 
	event_buffer = vmalloc(sizeof(unsigned long) * buffer_size);
	if (!event_buffer)
		goto out; 

	err = 0;
out:
	return err;
}


void free_event_buffer(void)
{
	vfree(event_buffer);
}

 
static int event_buffer_open(struct inode * inode, struct file * file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit(0, &buffer_opened))
		return -EBUSY;

	/* Register as a user of dcookies
	 * to ensure they persist for the lifetime of
	 * the open event file
	 */
	err = -EINVAL;
	file->private_data = dcookie_register();
	if (!file->private_data)
		goto out;
		 
	if ((err = oprofile_setup()))
		goto fail;

	/* NB: the actual start happens from userspace
	 * echo 1 >/dev/oprofile/enable
	 */
 
	return 0;

fail:
	dcookie_unregister(file->private_data);
out:
	clear_bit(0, &buffer_opened);
	return err;
}


static int event_buffer_release(struct inode * inode, struct file * file)
{
	oprofile_stop();
	oprofile_shutdown();
	dcookie_unregister(file->private_data);
	buffer_pos = 0;
	atomic_set(&buffer_ready, 0);
	clear_bit(0, &buffer_opened);
	return 0;
}


static ssize_t event_buffer_read(struct file * file, char __user * buf,
				 size_t count, loff_t * offset)
{
	int retval = -EINVAL;
	size_t const max = buffer_size * sizeof(unsigned long);

	/* handling partial reads is more trouble than it's worth */
	if (count != max || *offset)
		return -EINVAL;

	wait_event_interruptible(buffer_wait, atomic_read(&buffer_ready));

	if (signal_pending(current))
		return -EINTR;

	/* can't currently happen */
	if (!atomic_read(&buffer_ready))
		return -EAGAIN;

	mutex_lock(&buffer_mutex);

	atomic_set(&buffer_ready, 0);

	retval = -EFAULT;

	count = buffer_pos * sizeof(unsigned long);
 
	if (copy_to_user(buf, event_buffer, count))
		goto out;

	retval = count;
	buffer_pos = 0;
 
out:
	mutex_unlock(&buffer_mutex);
	return retval;
}
 
const struct file_operations event_buffer_fops = {
	.open		= event_buffer_open,
	.release	= event_buffer_release,
	.read		= event_buffer_read,
};
