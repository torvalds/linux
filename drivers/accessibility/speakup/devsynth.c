// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/miscdevice.h>	/* for misc_register, and MISC_DYNAMIC_MINOR */
#include <linux/types.h>
#include <linux/uaccess.h>

#include "speakup.h"
#include "spk_priv.h"

static int synth_registered, synthu_registered;
static int dev_opened;

/* Latin1 version */
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
	return (ssize_t)nbytes;
}

/* UTF-8 version */
static ssize_t speakup_file_writeu(struct file *fp, const char __user *buffer,
				   size_t nbytes, loff_t *ppos)
{
	size_t count = nbytes, want;
	const char __user *ptr = buffer;
	size_t bytes;
	unsigned long flags;
	unsigned char buf[256];
	u16 ubuf[256];
	size_t in, in2, out;

	if (!synth)
		return -ENODEV;

	want = 1;
	while (count >= want) {
		/* Copy some UTF-8 piece from userland */
		bytes = min(count, sizeof(buf));
		if (copy_from_user(buf, ptr, bytes))
			return -EFAULT;

		/* Convert to u16 */
		for (in = 0, out = 0; in < bytes; in++) {
			unsigned char c = buf[in];
			int nbytes = 8 - fls(c ^ 0xff);
			u32 value;

			switch (nbytes) {
			case 8: /* 0xff */
			case 7: /* 0xfe */
			case 1: /* 0x80 */
				/* Invalid, drop */
				goto drop;

			case 0:
				/* ASCII, copy */
				ubuf[out++] = c;
				continue;

			default:
				/* 2..6-byte UTF-8 */

				if (bytes - in < nbytes) {
					/* We don't have it all yet, stop here
					 * and wait for the rest
					 */
					bytes = in;
					want = nbytes;
					continue;
				}

				/* First byte */
				value = c & ((1u << (7 - nbytes)) - 1);

				/* Other bytes */
				for (in2 = 2; in2 <= nbytes; in2++) {
					c = buf[in + 1];
					if ((c & 0xc0) != 0x80)	{
						/* Invalid, drop the head */
						want = 1;
						goto drop;
					}
					value = (value << 6) | (c & 0x3f);
					in++;
				}

				if (value < 0x10000)
					ubuf[out++] = value;
				want = 1;
				break;
			}
drop:
			/* empty statement */;
		}

		count -= bytes;
		ptr += bytes;

		/* And speak this up */
		if (out) {
			spin_lock_irqsave(&speakup_info.spinlock, flags);
			for (in = 0; in < out; in++)
				synth_buffer_add(ubuf[in]);
			synth_start();
			spin_unlock_irqrestore(&speakup_info.spinlock, flags);
		}
	}

	return (ssize_t)(nbytes - count);
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

static const struct file_operations synthu_fops = {
	.read = speakup_file_read,
	.write = speakup_file_writeu,
	.open = speakup_file_open,
	.release = speakup_file_release,
};

static struct miscdevice synth_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "synth",
	.fops = &synth_fops,
};

static struct miscdevice synthu_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "synthu",
	.fops = &synthu_fops,
};

void speakup_register_devsynth(void)
{
	if (!synth_registered) {
		if (misc_register(&synth_device)) {
			pr_warn("Couldn't initialize miscdevice /dev/synth.\n");
		} else {
			pr_info("initialized device: /dev/synth, node (MAJOR %d, MINOR %d)\n",
				MISC_MAJOR, synth_device.minor);
			synth_registered = 1;
		}
	}
	if (!synthu_registered) {
		if (misc_register(&synthu_device)) {
			pr_warn("Couldn't initialize miscdevice /dev/synthu.\n");
		} else {
			pr_info("initialized device: /dev/synthu, node (MAJOR %d, MINOR %d)\n",
				MISC_MAJOR, synthu_device.minor);
			synthu_registered = 1;
		}
	}
}

void speakup_unregister_devsynth(void)
{
	if (synth_registered) {
		pr_info("speakup: unregistering synth device /dev/synth\n");
		misc_deregister(&synth_device);
		synth_registered = 0;
	}
	if (synthu_registered) {
		pr_info("speakup: unregistering synth device /dev/synthu\n");
		misc_deregister(&synthu_device);
		synthu_registered = 0;
	}
}
