// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Axis Communications AB
 *
 * Based on ttyprintk.c:
 *  Copyright (C) 2010 Samo Pogacnik
 */

#include <linux/console.h>
#include <linux/module.h>
#include <linux/tty.h>

static const struct tty_port_operations ttynull_port_ops;
static struct tty_driver *ttynull_driver;
static struct tty_port ttynull_port;

static int ttynull_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(&ttynull_port, tty, filp);
}

static void ttynull_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(&ttynull_port, tty, filp);
}

static void ttynull_hangup(struct tty_struct *tty)
{
	tty_port_hangup(&ttynull_port);
}

static ssize_t ttynull_write(struct tty_struct *tty, const u8 *buf,
			     size_t count)
{
	return count;
}

static unsigned int ttynull_write_room(struct tty_struct *tty)
{
	return 65536;
}

static const struct tty_operations ttynull_ops = {
	.open = ttynull_open,
	.close = ttynull_close,
	.hangup = ttynull_hangup,
	.write = ttynull_write,
	.write_room = ttynull_write_room,
};

static struct tty_driver *ttynull_device(struct console *c, int *index)
{
	*index = 0;
	return ttynull_driver;
}

static struct console ttynull_console = {
	.name = "ttynull",
	.device = ttynull_device,
};

static int __init ttynull_init(void)
{
	struct tty_driver *driver;
	int ret;

	driver = tty_alloc_driver(1,
		TTY_DRIVER_RESET_TERMIOS |
		TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	tty_port_init(&ttynull_port);
	ttynull_port.ops = &ttynull_port_ops;

	driver->driver_name = "ttynull";
	driver->name = "ttynull";
	driver->type = TTY_DRIVER_TYPE_CONSOLE;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_oflag = OPOST | OCRNL | ONOCR | ONLRET;
	tty_set_operations(driver, &ttynull_ops);
	tty_port_link_device(&ttynull_port, driver, 0);

	ret = tty_register_driver(driver);
	if (ret < 0) {
		tty_driver_kref_put(driver);
		tty_port_destroy(&ttynull_port);
		return ret;
	}

	ttynull_driver = driver;
	register_console(&ttynull_console);

	return 0;
}

static void __exit ttynull_exit(void)
{
	unregister_console(&ttynull_console);
	tty_unregister_driver(ttynull_driver);
	tty_driver_kref_put(ttynull_driver);
	tty_port_destroy(&ttynull_port);
}

module_init(ttynull_init);
module_exit(ttynull_exit);

MODULE_LICENSE("GPL v2");
