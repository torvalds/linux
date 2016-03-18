#include <linux/errno.h>
#include <linux/miscdevice.h>	/* for misc_register, and SYNTH_MINOR */
#include <linux/types.h>
#include <linux/uaccess.h>

#include "speakup.h"
#include "spk_priv.h"

#ifndef SYNTH_MINOR
#define SYNTH_MINOR 25
#endif

static int misc_registered;
static int dev_opened;

static ssize_t speakup_file_write(struct file *fp, const char __user *buffer,
				  size_t nbytes, loff_t *ppos)
{
	size_t count = nbytes;
	const char __user *ptr = buffer;
	size_t bytes;
	unsigned long flags;
	u_char buf[256];

	if (!synth)
		return -ENODEV;
	while (count > 0) {
		bytes = min(count, sizeof(buf));
		if (copy_from_user(buf, ptr, bytes))
			return -EFAULT;
		count -= bytes;
		ptr += bytes;
		spin_lock_irqsave(&speakup_info.spinlock, flags);
		synth_write(buf, bytes);
		spin_unlock_irqrestore(&speakup_info.spinlock, flags);
	}
	return (ssize_t) nbytes;
}

static ssize_t speakup_file_read(struct file *fp, char __user *buf,
				 size_t nbytes, loff_t *ppos)
{
	return 0;
}

static int speakup_file_open(struct inode *ip, struct file *fp)
{
	if (!synth)
		return -ENODEV;
	if (xchg(&dev_opened, 1))
		return -EBUSY;
	return 0;
}

static int speakup_file_release(struct inode *ip, struct file *fp)
{
	dev_opened = 0;
	return 0;
}

static const struct file_operations synth_fops = {
	.read = speakup_file_read,
	.write = speakup_file_write,
	.open = speakup_file_open,
	.release = speakup_file_release,
};

static struct miscdevice synth_device = {
	.minor = SYNTH_MINOR,
	.name = "synth",
	.fops = &synth_fops,
};

void speakup_register_devsynth(void)
{
	if (misc_registered != 0)
		return;
/* zero it so if register fails, deregister will not ref invalid ptrs */
	if (misc_register(&synth_device)) {
		pr_warn("Couldn't initialize miscdevice /dev/synth.\n");
	} else {
		pr_info("initialized device: /dev/synth, node (MAJOR %d, MINOR %d)\n",
			MISC_MAJOR, SYNTH_MINOR);
		misc_registered = 1;
	}
}

void speakup_unregister_devsynth(void)
{
	if (!misc_registered)
		return;
	pr_info("speakup: unregistering synth device /dev/synth\n");
	misc_deregister(&synth_device);
	misc_registered = 0;
}
