/*
 * include/asm-v850/rte_cb_leds.c -- Midas lab RTE-CB board LED device support
 *
 *  Copyright (C) 2002,03  NEC Electronics Corporation
 *  Copyright (C) 2002,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include <asm/uaccess.h>

#define LEDS_MINOR	169	/* Minor device number, using misc major.  */

/* The actual LED hardware is write-only, so we hold the contents here too.  */
static unsigned char leds_image[LED_NUM_DIGITS] = { 0 };

/* Spinlock protecting the above leds.  */
static DEFINE_SPINLOCK(leds_lock);

/* Common body of LED read/write functions, checks POS and LEN for
   correctness, declares a variable using IMG_DECL, initialized pointing at
   the POS position in the LED image buffer, and and iterates COPY_EXPR
   until BUF is equal to the last buffer position; finally, sets LEN to be
   the amount actually copied.  IMG should be a variable declaration
   (without an initializer or a terminating semicolon); POS, BUF, and LEN
   should all be simple variables.  */
#define DO_LED_COPY(img_decl, pos, buf, len, copy_expr)			\
do {									\
	if (pos > LED_NUM_DIGITS)					\
		len = 0;						\
	else {								\
		if (pos + len > LED_NUM_DIGITS)				\
			len = LED_NUM_DIGITS - pos;			\
									\
		if (len > 0) {						\
			int _flags;					\
			const char *_end = buf + len;			\
			img_decl = &leds_image[pos];			\
									\
			spin_lock_irqsave (leds_lock, _flags);		\
			do						\
				(copy_expr);				\
			while (buf != _end);				\
			spin_unlock_irqrestore (leds_lock, _flags);	\
		}							\
	}								\
} while (0)

/* Read LEN bytes from LEDs at position POS, into BUF.
   Returns actual amount read.  */
unsigned read_leds (unsigned pos, char *buf, unsigned len)
{
	DO_LED_COPY (const char *img, pos, buf, len, *buf++ = *img++);
	return len;
}

/* Write LEN bytes to LEDs at position POS, from BUF.
   Returns actual amount written.  */
unsigned write_leds (unsigned pos, const char *buf, unsigned len)
{
	/* We write the actual LED values backwards, because
	   increasing memory addresses reflect LEDs right-to-left. */
	volatile char *led = &LED (LED_NUM_DIGITS - pos - 1);
	/* We invert the value written to the hardware, because 1 = off,
	   and 0 = on.  */
	DO_LED_COPY (char *img, pos, buf, len,
		     *led-- = 0xFF ^ (*img++ = *buf++));
	return len;
}


/* Device functions.  */

static ssize_t leds_dev_read (struct file *file, char *buf, size_t len,
			      loff_t *pos)
{
	char temp_buf[LED_NUM_DIGITS];
	len = read_leds (*pos, temp_buf, len);
	if (copy_to_user (buf, temp_buf, len))
		return -EFAULT;
	*pos += len;
	return len;
}

static ssize_t leds_dev_write (struct file *file, const char *buf, size_t len,
			       loff_t *pos)
{
	char temp_buf[LED_NUM_DIGITS];
	if (copy_from_user (temp_buf, buf, min_t(size_t, len, LED_NUM_DIGITS)))
		return -EFAULT;
	len = write_leds (*pos, temp_buf, len);
	*pos += len;
	return len;
}

static loff_t leds_dev_lseek (struct file *file, loff_t offs, int whence)
{
	if (whence == 1)
		offs += file->f_pos; /* relative */
	else if (whence == 2)
		offs += LED_NUM_DIGITS; /* end-relative */

	if (offs < 0 || offs > LED_NUM_DIGITS)
		return -EINVAL;

	file->f_pos = offs;

	return 0;
}

static struct file_operations leds_fops = {
	.read		= leds_dev_read,
	.write		= leds_dev_write,
	.llseek		= leds_dev_lseek
};

static struct miscdevice leds_miscdev = {
	.name		= "leds",
	.minor		= LEDS_MINOR,
	.fops		= &leds_fops
};

int __init leds_dev_init (void)
{
	return misc_register (&leds_miscdev);
}

__initcall (leds_dev_init);
