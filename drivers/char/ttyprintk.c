// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/char/ttyprintk.c
 *
 *  Copyright (C) 2010  Samo Pogacnik
 */

/*
 * This pseudo device allows user to make printk messages. It is possible
 * to store "console" messages inline with kernel messages for better analyses
 * of the boot process, for example.
 */

#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/spinlock.h>

struct ttyprintk_port {
	struct tty_port port;
	spinlock_t spinlock;
};

static struct ttyprintk_port tpk_port;

/*
 * Our simple preformatting supports transparent output of (time-stamped)
 * printk messages (also suitable for logging service):
 * - any cr is replaced by nl
 * - adds a ttyprintk source tag in front of each line
 * - too long message is fragmented, with '\'nl between fragments
 * - TPK_STR_SIZE isn't really the write_room limiting factor, because
 *   it is emptied on the fly during preformatting.
 */
#define TPK_STR_SIZE 508 /* should be bigger then max expected line length */
#define TPK_MAX_ROOM 4096 /* we could assume 4K for instance */
#define TPK_PREFIX KERN_SOH __stringify(CONFIG_TTY_PRINTK_LEVEL)

static int tpk_curr;

static char tpk_buffer[TPK_STR_SIZE + 4];

static void tpk_flush(void)
{
	if (tpk_curr > 0) {
		tpk_buffer[tpk_curr] = '\0';
		printk(TPK_PREFIX "[U] %s\n", tpk_buffer);
		tpk_curr = 0;
	}
}

static int tpk_printk(const unsigned char *buf, int count)
{
	int i = tpk_curr;

	if (buf == NULL) {
		tpk_flush();
		return i;
	}

	for (i = 0; i < count; i++) {
		if (tpk_curr >= TPK_STR_SIZE) {
			/* end of tmp buffer reached: cut the message in two */
			tpk_buffer[tpk_curr++] = '\\';
			tpk_flush();
		}

		switch (buf[i]) {
		case '\r':
			tpk_flush();
			if ((i + 1) < count && buf[i + 1] == '\n')
				i++;
			break;
		case '\n':
			tpk_flush();
			break;
		default:
			tpk_buffer[tpk_curr++] = buf[i];
			break;
		}
	}

	return count;
}

/*
 * TTY operations open function.
 */
static int tpk_open(struct tty_struct *tty, struct file *filp)
{
	tty->driver_data = &tpk_port;

	return tty_port_open(&tpk_port.port, tty, filp);
}

/*
 * TTY operations close function.
 */
static void tpk_close(struct tty_struct *tty, struct file *filp)
{
	struct ttyprintk_port *tpkp = tty->driver_data;
	unsigned long flags;

	spin_lock_irqsave(&tpkp->spinlock, flags);
	/* flush tpk_printk buffer */
	tpk_printk(NULL, 0);
	spin_unlock_irqrestore(&tpkp->spinlock, flags);

	tty_port_close(&tpkp->port, tty, filp);
}

/*
 * TTY operations write function.
 */
static int tpk_write(struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	struct ttyprintk_port *tpkp = tty->driver_data;
	unsigned long flags;
	int ret;


	/* exclusive use of tpk_printk within this tty */
	spin_lock_irqsave(&tpkp->spinlock, flags);
	ret = tpk_printk(buf, count);
	spin_unlock_irqrestore(&tpkp->spinlock, flags);

	return ret;
}

/*
 * TTY operations write_room function.
 */
static int tpk_write_room(struct tty_struct *tty)
{
	return TPK_MAX_ROOM;
}

/*
 * TTY operations ioctl function.
 */
static int tpk_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg)
{
	struct ttyprintk_port *tpkp = tty->driver_data;

	if (!tpkp)
		return -EINVAL;

	switch (cmd) {
	/* Stop TIOCCONS */
	case TIOCCONS:
		return -EOPNOTSUPP;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static const struct tty_operations ttyprintk_ops = {
	.open = tpk_open,
	.close = tpk_close,
	.write = tpk_write,
	.write_room = tpk_write_room,
	.ioctl = tpk_ioctl,
};

static const struct tty_port_operations null_ops = { };

static struct tty_driver *ttyprintk_driver;

static int __init ttyprintk_init(void)
{
	int ret;

	spin_lock_init(&tpk_port.spinlock);

	ttyprintk_driver = tty_alloc_driver(1,
			TTY_DRIVER_RESET_TERMIOS |
			TTY_DRIVER_REAL_RAW |
			TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(ttyprintk_driver))
		return PTR_ERR(ttyprintk_driver);

	tty_port_init(&tpk_port.port);
	tpk_port.port.ops = &null_ops;

	ttyprintk_driver->driver_name = "ttyprintk";
	ttyprintk_driver->name = "ttyprintk";
	ttyprintk_driver->major = TTYAUX_MAJOR;
	ttyprintk_driver->minor_start = 3;
	ttyprintk_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	ttyprintk_driver->init_termios = tty_std_termios;
	ttyprintk_driver->init_termios.c_oflag = OPOST | OCRNL | ONOCR | ONLRET;
	tty_set_operations(ttyprintk_driver, &ttyprintk_ops);
	tty_port_link_device(&tpk_port.port, ttyprintk_driver, 0);

	ret = tty_register_driver(ttyprintk_driver);
	if (ret < 0) {
		printk(KERN_ERR "Couldn't register ttyprintk driver\n");
		goto error;
	}

	return 0;

error:
	put_tty_driver(ttyprintk_driver);
	tty_port_destroy(&tpk_port.port);
	return ret;
}

static void __exit ttyprintk_exit(void)
{
	tty_unregister_driver(ttyprintk_driver);
	put_tty_driver(ttyprintk_driver);
	tty_port_destroy(&tpk_port.port);
}

device_initcall(ttyprintk_init);
module_exit(ttyprintk_exit);

MODULE_LICENSE("GPL");
