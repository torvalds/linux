/* Hey EMACS -*- linux-c -*-
 *
 * tipar - low level driver for handling a parallel link cable designed
 * for Texas Instruments graphing calculators (http://lpg.ticalc.org).
 * A part of the TiLP project.
 *
 * Copyright (C) 2000-2002, Romain Lievin <roms@lpg.ticalc.org>
 * under the terms of the GNU General Public License.
 *
 * Various fixes & clean-up from the Linux Kernel Mailing List
 * (Alan Cox, Richard B. Johnson, Christoph Hellwig).
 */

/* This driver should, in theory, work with any parallel port that has an
 * appropriate low-level driver; all I/O is done through the parport
 * abstraction layer.
 *
 * If this driver is built into the kernel, you can configure it using the
 * kernel command-line.  For example:
 *
 *      tipar=timeout,delay       (set timeout and delay)
 *
 * If the driver is loaded as a module, similar functionality is available
 * using module parameters.  The equivalent of the above commands would be:
 *
 *      # insmod tipar timeout=15 delay=10
 */

/* COMPATIBILITY WITH OLD KERNELS
 *
 * Usually, parallel cables were bound to ports at
 * particular I/O addresses, as follows:
 *
 *      tipar0             0x378
 *      tipar1             0x278
 *      tipar2             0x3bc
 *
 *
 * This driver, by default, binds tipar devices according to parport and
 * the minor number.
 *
 */
#undef DEBUG				/* change to #define to get debugging
					 * output - for pr_debug() */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/devfs_fs_kernel.h>	/* DevFs support */
#include <linux/parport.h>		/* Our code depend on parport */
#include <linux/device.h>

/*
 * TI definitions
 */
#include <linux/ticable.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "1.19"
#define DRIVER_AUTHOR  "Romain Lievin <roms@lpg.ticalc.org>"
#define DRIVER_DESC    "Device driver for TI/PC parallel link cables"
#define DRIVER_LICENSE "GPL"

#define VERSION(ver,rel,seq) (((ver)<<16) | ((rel)<<8) | (seq))

/* ----- global variables --------------------------------------------- */

struct tipar_struct {
	struct pardevice *dev;	/* Parport device entry */
};

#define PP_NO 3
static struct tipar_struct table[PP_NO];

static int delay = IO_DELAY;	/* inter-bit delay in microseconds */
static int timeout = TIMAXTIME;	/* timeout in tenth of seconds     */

static unsigned int tp_count;	/* tipar count */
static unsigned long opened;	/* opened devices */

static struct class_simple *tipar_class;

/* --- macros for parport access -------------------------------------- */

#define r_dtr(x)        (parport_read_data(table[(x)].dev->port))
#define r_str(x)        (parport_read_status(table[(x)].dev->port))
#define w_ctr(x,y)      (parport_write_control(table[(x)].dev->port, (y)))
#define w_dtr(x,y)      (parport_write_data(table[(x)].dev->port, (y)))

/* --- setting states on the D-bus with the right timing: ------------- */

static inline void
outbyte(int value, int minor)
{
	w_dtr(minor, value);
}

static inline int
inbyte(int minor)
{
	return (r_str(minor));
}

static inline void
init_ti_parallel(int minor)
{
	outbyte(3, minor);
}

/* ----- global defines ----------------------------------------------- */

#define START(x) { x = jiffies + (HZ * timeout) / 10; }
#define WAIT(x)  { \
  if (time_before((x), jiffies)) return -1; \
  if (need_resched()) schedule(); }

/* ----- D-bus bit-banging functions ---------------------------------- */

/* D-bus protocol (45kbit/s max):
                    1                 0                      0
       _______        ______|______    __________|________    __________
Red  :        ________      |      ____          |        ____
       _        ____________|________      ______|__________       _____
White:  ________            |        ______      |          _______
*/

/* Try to transmit a byte on the specified port (-1 if error). */
static int
put_ti_parallel(int minor, unsigned char data)
{
	unsigned int bit;
	unsigned long max;

	for (bit = 0; bit < 8; bit++) {
		if (data & 1) {
			outbyte(2, minor);
			START(max);
			do {
				WAIT(max);
			} while (inbyte(minor) & 0x10);

			outbyte(3, minor);
			START(max);
			do {
				WAIT(max);
			} while (!(inbyte(minor) & 0x10));
		} else {
			outbyte(1, minor);
			START(max);
			do {
				WAIT(max);
			} while (inbyte(minor) & 0x20);

			outbyte(3, minor);
			START(max);
			do {
				WAIT(max);
			} while (!(inbyte(minor) & 0x20));
		}

		data >>= 1;
		udelay(delay);

		if (need_resched())
			schedule();
	}

	return 0;
}

/* Receive a byte on the specified port or -1 if error. */
static int
get_ti_parallel(int minor)
{
	unsigned int bit;
	unsigned char v, data = 0;
	unsigned long max;

	for (bit = 0; bit < 8; bit++) {
		START(max);
		do {
			WAIT(max);
		} while ((v = inbyte(minor) & 0x30) == 0x30);

		if (v == 0x10) {
			data = (data >> 1) | 0x80;
			outbyte(1, minor);
			START(max);
			do {
				WAIT(max);
			} while (!(inbyte(minor) & 0x20));
			outbyte(3, minor);
		} else {
			data = data >> 1;
			outbyte(2, minor);
			START(max);
			do {
				WAIT(max);
			} while (!(inbyte(minor) & 0x10));
			outbyte(3, minor);
		}

		udelay(delay);
		if (need_resched())
			schedule();
	}

	return (int) data;
}

/* Try to detect a parallel link cable on the specified port */
static int
probe_ti_parallel(int minor)
{
	int i;
	int seq[] = { 0x00, 0x20, 0x10, 0x30 };

	for (i = 3; i >= 0; i--) {
		outbyte(3, minor);
		outbyte(i, minor);
		udelay(delay);
		pr_debug("tipar: Probing -> %i: 0x%02x 0x%02x\n", i,
			data & 0x30, seq[i]);
		if ((inbyte(minor) & 0x30) != seq[i]) {
			outbyte(3, minor);
			return -1;
		}
	}

	outbyte(3, minor);
	return 0;
}

/* ----- kernel module functions--------------------------------------- */

static int
tipar_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode) - TIPAR_MINOR;

	if (minor > tp_count - 1)
		return -ENXIO;

	if (test_and_set_bit(minor, &opened))
		return -EBUSY;

	parport_claim_or_block(table[minor].dev);
	init_ti_parallel(minor);
	parport_release(table[minor].dev);

	return nonseekable_open(inode, file);
}

static int
tipar_close(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode) - TIPAR_MINOR;

	if (minor > tp_count - 1)
		return -ENXIO;

	clear_bit(minor, &opened);

	return 0;
}

static ssize_t
tipar_write (struct file *file, const char __user *buf, size_t count,
		loff_t * ppos)
{
	unsigned int minor = iminor(file->f_dentry->d_inode) - TIPAR_MINOR;
	ssize_t n;

	parport_claim_or_block(table[minor].dev);

	for (n = 0; n < count; n++) {
		unsigned char b;

		if (get_user(b, buf + n)) {
			n = -EFAULT;
			goto out;
		}

		if (put_ti_parallel(minor, b) == -1) {
			init_ti_parallel(minor);
			n = -ETIMEDOUT;
			goto out;
		}
	}
      out:
	parport_release(table[minor].dev);
	return n;
}

static ssize_t
tipar_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
	int b = 0;
	unsigned int minor = iminor(file->f_dentry->d_inode) - TIPAR_MINOR;
	ssize_t retval = 0;
	ssize_t n = 0;

	if (count == 0)
		return 0;

	parport_claim_or_block(table[minor].dev);

	while (n < count) {
		b = get_ti_parallel(minor);
		if (b == -1) {
			init_ti_parallel(minor);
			retval = -ETIMEDOUT;
			goto out;
		} else {
			if (put_user(b, buf + n)) {
				retval = -EFAULT;
				break;
			} else
				retval = ++n;
		}

		/* Non-blocking mode : try again ! */
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}

		/* Signal pending, try again ! */
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}

		if (need_resched())
			schedule();
	}

      out:
	parport_release(table[minor].dev);
	return retval;
}

static int
tipar_ioctl(struct inode *inode, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	switch (cmd) {
	case IOCTL_TIPAR_DELAY:
		delay = (int)arg;    //get_user(delay, &arg);
		break;
	case IOCTL_TIPAR_TIMEOUT:
		if (arg != 0)
                        timeout = (int)arg;
                else
                        retval = -EINVAL;
	  break;
	default:
		retval = -ENOTTY;
		break;
	}

	return retval;
}

/* ----- kernel module registering ------------------------------------ */

static struct file_operations tipar_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = tipar_read,
	.write = tipar_write,
	.ioctl = tipar_ioctl,
	.open = tipar_open,
	.release = tipar_close,
};

/* --- initialisation code ------------------------------------- */

#ifndef MODULE
/*      You must set these - there is no sane way to probe for this cable.
 *      You can use 'tipar=timeout,delay' to set these now. */
static int __init
tipar_setup(char *str)
{
	int ints[2];

	str = get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] > 0) {
		if (ints[1] != 0)
                        timeout = ints[1];
                else
                        printk(KERN_WARNING "tipar: bad timeout value (0), "
				"using default value instead");
		if (ints[0] > 1) {
			delay = ints[2];
		}
	}

	return 1;
}
#endif

/*
 * Register our module into parport.
 * Pass also 2 callbacks functions to parport: a pre-emptive function and an
 * interrupt handler function (unused).
 * Display a message such "tipar0: using parport0 (polling)".
 */
static int
tipar_register(int nr, struct parport *port)
{
	int err = 0;

	/* Register our module into parport */
	table[nr].dev = parport_register_device(port, "tipar",
						NULL, NULL, NULL, 0,
						(void *) &table[nr]);

	if (table[nr].dev == NULL) {
		err = 1;
		goto out;
	}

	class_simple_device_add(tipar_class, MKDEV(TIPAR_MAJOR,
			TIPAR_MINOR + nr), NULL, "par%d", nr);
	/* Use devfs, tree: /dev/ticables/par/[0..2] */
	err = devfs_mk_cdev(MKDEV(TIPAR_MAJOR, TIPAR_MINOR + nr),
			S_IFCHR | S_IRUGO | S_IWUGO,
			"ticables/par/%d", nr);
	if (err)
		goto out_class;

	/* Display informations */
	pr_info("tipar%d: using %s (%s)\n", nr, port->name, (port->irq ==
		PARPORT_IRQ_NONE) ? "polling" : "interrupt-driven");

	if (probe_ti_parallel(nr) != -1)
		pr_info("tipar%d: link cable found\n", nr);
	else
		pr_info("tipar%d: link cable not found\n", nr);

	err = 0;
	goto out;

out_class:
	class_simple_device_remove(MKDEV(TIPAR_MAJOR, TIPAR_MINOR + nr));
	class_simple_destroy(tipar_class);
out:
	return err;
}

static void
tipar_attach(struct parport *port)
{
	if (tp_count == PP_NO) {
		pr_info("tipar: ignoring parallel port (max. %d)\n", PP_NO);
		return;
	}

	if (!tipar_register(tp_count, port))
		tp_count++;
}

static void
tipar_detach(struct parport *port)
{
	/* Nothing to do */
}

static struct parport_driver tipar_driver = {
	.name = "tipar",
	.attach = tipar_attach,
	.detach = tipar_detach,
};

static int __init
tipar_init_module(void)
{
	int err = 0;

	pr_info("tipar: parallel link cable driver, version %s\n",
		DRIVER_VERSION);

	if (register_chrdev(TIPAR_MAJOR, "tipar", &tipar_fops)) {
		printk(KERN_ERR "tipar: unable to get major %d\n", TIPAR_MAJOR);
		err = -EIO;
		goto out;
	}

	/* Use devfs with tree: /dev/ticables/par/[0..2] */
	devfs_mk_dir("ticables/par");

	tipar_class = class_simple_create(THIS_MODULE, "ticables");
	if (IS_ERR(tipar_class)) {
		err = PTR_ERR(tipar_class);
		goto out_chrdev;
	}
	if (parport_register_driver(&tipar_driver)) {
		printk(KERN_ERR "tipar: unable to register with parport\n");
		err = -EIO;
		goto out;
	}

	err = 0;
	goto out;

out_chrdev:
	unregister_chrdev(TIPAR_MAJOR, "tipar");
out:
	return err;	
}

static void __exit
tipar_cleanup_module(void)
{
	unsigned int i;

	/* Unregistering module */
	parport_unregister_driver(&tipar_driver);

	unregister_chrdev(TIPAR_MAJOR, "tipar");

	for (i = 0; i < PP_NO; i++) {
		if (table[i].dev == NULL)
			continue;
		parport_unregister_device(table[i].dev);
		class_simple_device_remove(MKDEV(TIPAR_MAJOR, i));
		devfs_remove("ticables/par/%d", i);
	}
	class_simple_destroy(tipar_class);
	devfs_remove("ticables/par");

	pr_info("tipar: module unloaded\n");
}

/* --------------------------------------------------------------------- */

__setup("tipar=", tipar_setup);
module_init(tipar_init_module);
module_exit(tipar_cleanup_module);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Timeout (default=1.5 seconds)");
module_param(delay, int, 0);
MODULE_PARM_DESC(delay, "Inter-bit delay (default=10 microseconds)");
