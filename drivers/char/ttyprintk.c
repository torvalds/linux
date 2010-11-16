/*
 *  linux/drivers/char/ttyprintk.c
 *
 *  Copyright (C) 2010  Samo Pogacnik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/*
 * This pseudo device allows user to make printk messages. It is possible
 * to store "console" messages inline with kernel messages for better analyses
 * of the boot process, for example.
 */

#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>

struct ttyprintk_port {
	struct tty_port port;
	struct mutex port_write_mutex;
};

static struct ttyprintk_port tpk_port;

/*
 * Our simple preformatting supports transparent output of (time-stamped)
 * printk messages (also suitable for logging service):
 * - any cr is replaced by nl
 * - adds a ttyprintk source tag in front of each line
 * - too long message is fragmeted, with '\'nl between fragments
 * - TPK_STR_SIZE isn't really the write_room limiting factor, bcause
 *   it is emptied on the fly during preformatting.
 */
#define TPK_STR_SIZE 508 /* should be bigger then max expected line length */
#define TPK_MAX_ROOM 4096 /* we could assume 4K for instance */
static const char *tpk_tag = "[U] "; /* U for User */
static int tpk_curr;

static int tpk_printk(const unsigned char *buf, int count)
{
	static char tmp[TPK_STR_SIZE + 4];
	int i = tpk_curr;

	if (buf == NULL) {
		/* flush tmp[] */
		if (tpk_curr > 0) {
			/* non nl or cr terminated message - add nl */
			tmp[tpk_curr + 0] = '\n';
			tmp[tpk_curr + 1] = '\0';
			printk(KERN_INFO "%s%s", tpk_tag, tmp);
			tpk_curr = 0;
		}
		return i;
	}

	for (i = 0; i < count; i++) {
		tmp[tpk_curr] = buf[i];
		if (tpk_curr < TPK_STR_SIZE) {
			switch (buf[i]) {
			case '\r':
				/* replace cr with nl */
				tmp[tpk_curr + 0] = '\n';
				tmp[tpk_curr + 1] = '\0';
				printk(KERN_INFO "%s%s", tpk_tag, tmp);
				tpk_curr = 0;
				if (buf[i + 1] == '\n')
					i++;
				break;
			case '\n':
				tmp[tpk_curr + 1] = '\0';
				printk(KERN_INFO "%s%s", tpk_tag, tmp);
				tpk_curr = 0;
				break;
			default:
				tpk_curr++;
			}
		} else {
			/* end of tmp buffer reached: cut the message in two */
			tmp[tpk_curr + 1] = '\\';
			tmp[tpk_curr + 2] = '\n';
			tmp[tpk_curr + 3] = '\0';
			printk(KERN_INFO "%s%s", tpk_tag, tmp);
			tpk_curr = 0;
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

	mutex_lock(&tpkp->port_write_mutex);
	/* flush tpk_printk buffer */
	tpk_printk(NULL, 0);
	mutex_unlock(&tpkp->port_write_mutex);

	tty_port_close(&tpkp->port, tty, filp);
}

/*
 * TTY operations write function.
 */
static int tpk_write(struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	struct ttyprintk_port *tpkp = tty->driver_data;
	int ret;


	/* exclusive use of tpk_printk within this tty */
	mutex_lock(&tpkp->port_write_mutex);
	ret = tpk_printk(buf, count);
	mutex_unlock(&tpkp->port_write_mutex);

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
static int tpk_ioctl(struct tty_struct *tty, struct file *file,
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

struct tty_port_operations null_ops = { };

static struct tty_driver *ttyprintk_driver;

static int __init ttyprintk_init(void)
{
	int ret = -ENOMEM;
	void *rp;

	ttyprintk_driver = alloc_tty_driver(1);
	if (!ttyprintk_driver)
		return ret;

	ttyprintk_driver->owner = THIS_MODULE;
	ttyprintk_driver->driver_name = "ttyprintk";
	ttyprintk_driver->name = "ttyprintk";
	ttyprintk_driver->major = TTYAUX_MAJOR;
	ttyprintk_driver->minor_start = 3;
	ttyprintk_driver->num = 1;
	ttyprintk_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	ttyprintk_driver->init_termios = tty_std_termios;
	ttyprintk_driver->init_termios.c_oflag = OPOST | OCRNL | ONOCR | ONLRET;
	ttyprintk_driver->flags = TTY_DRIVER_RESET_TERMIOS |
		TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_set_operations(ttyprintk_driver, &ttyprintk_ops);

	ret = tty_register_driver(ttyprintk_driver);
	if (ret < 0) {
		printk(KERN_ERR "Couldn't register ttyprintk driver\n");
		goto error;
	}

	/* create our unnumbered device */
	rp = device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 3), NULL,
				ttyprintk_driver->name);
	if (IS_ERR(rp)) {
		printk(KERN_ERR "Couldn't create ttyprintk device\n");
		ret = PTR_ERR(rp);
		goto error;
	}

	tty_port_init(&tpk_port.port);
	tpk_port.port.ops = &null_ops;
	mutex_init(&tpk_port.port_write_mutex);

	return 0;

error:
	put_tty_driver(ttyprintk_driver);
	ttyprintk_driver = NULL;
	return ret;
}
module_init(ttyprintk_init);
