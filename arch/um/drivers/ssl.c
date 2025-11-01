// SPDX-License-Identifier: GPL-2.0
/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 */

#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/console.h>
#include <asm/termbits.h>
#include <asm/irq.h>
#include "chan.h"
#include <init.h>
#include <irq_user.h>
#include "mconsole_kern.h"

static const int ssl_version = 1;

#define NR_PORTS 64

static void ssl_announce(char *dev_name, int dev)
{
	printk(KERN_INFO "Serial line %d assigned device '%s'\n", dev,
	       dev_name);
}

/* Almost const, except that xterm_title may be changed in an initcall */
static struct chan_opts opts = {
	.announce 	= ssl_announce,
	.xterm_title	= "Serial Line #%d",
	.raw		= 1,
};

static int ssl_config(char *str, char **error_out);
static int ssl_get_config(char *dev, char *str, int size, char **error_out);
static int ssl_remove(int n, char **error_out);


/* Const, except for .mc.list */
static struct line_driver driver = {
	.name 			= "UML serial line",
	.device_name 		= "ttyS",
	.major 			= TTY_MAJOR,
	.minor_start 		= 64,
	.type 		 	= TTY_DRIVER_TYPE_SERIAL,
	.subtype 	 	= 0,
	.read_irq_name 		= "ssl",
	.write_irq_name 	= "ssl-write",
	.mc  = {
		.list		= LIST_HEAD_INIT(driver.mc.list),
		.name  		= "ssl",
		.config 	= ssl_config,
		.get_config 	= ssl_get_config,
		.id		= line_id,
		.remove 	= ssl_remove,
	},
};

/* The array is initialized by line_init, at initcall time.  The
 * elements are locked individually as needed.
 */
static char *conf[NR_PORTS];
static char *def_conf = CONFIG_SSL_CHAN;
static struct line serial_lines[NR_PORTS];

static int ssl_config(char *str, char **error_out)
{
	return line_config(serial_lines, ARRAY_SIZE(serial_lines), str, &opts,
			   error_out);
}

static int ssl_get_config(char *dev, char *str, int size, char **error_out)
{
	return line_get_config(dev, serial_lines, ARRAY_SIZE(serial_lines), str,
			       size, error_out);
}

static int ssl_remove(int n, char **error_out)
{
	return line_remove(serial_lines, ARRAY_SIZE(serial_lines), n,
			   error_out);
}

static int ssl_install(struct tty_driver *driver, struct tty_struct *tty)
{
	return line_install(driver, tty, &serial_lines[tty->index]);
}

static const struct tty_operations ssl_ops = {
	.open 	 		= line_open,
	.close 	 		= line_close,
	.write 	 		= line_write,
	.write_room		= line_write_room,
	.chars_in_buffer 	= line_chars_in_buffer,
	.flush_buffer 		= line_flush_buffer,
	.flush_chars 		= line_flush_chars,
	.throttle 		= line_throttle,
	.unthrottle 		= line_unthrottle,
	.install		= ssl_install,
	.hangup			= line_hangup,
};

/* Changed by ssl_init and referenced by ssl_exit, which are both serialized
 * by being an initcall and exitcall, respectively.
 */
static int ssl_init_done;

static void ssl_console_write(struct console *c, const char *string,
			      unsigned len)
{
	struct line *line = &serial_lines[c->index];
	unsigned long flags;

	spin_lock_irqsave(&line->lock, flags);
	console_write_chan(line->chan_out, string, len);
	spin_unlock_irqrestore(&line->lock, flags);
}

static struct tty_driver *ssl_console_device(struct console *c, int *index)
{
	*index = c->index;
	return driver.driver;
}

static int ssl_console_setup(struct console *co, char *options)
{
	struct line *line = &serial_lines[co->index];

	return console_open_chan(line, co);
}

/* No locking for register_console call - relies on single-threaded initcalls */
static struct console ssl_cons = {
	.name		= "ttyS",
	.write		= ssl_console_write,
	.device		= ssl_console_device,
	.setup		= ssl_console_setup,
	.flags		= CON_PRINTBUFFER|CON_ANYTIME,
	.index		= -1,
};

static int ssl_init(void)
{
	char *new_title;
	int err;
	int i;

	printk(KERN_INFO "Initializing software serial port version %d\n",
	       ssl_version);

	err = register_lines(&driver, &ssl_ops, serial_lines,
				    ARRAY_SIZE(serial_lines));
	if (err)
		return err;

	new_title = add_xterm_umid(opts.xterm_title);
	if (new_title != NULL)
		opts.xterm_title = new_title;

	for (i = 0; i < NR_PORTS; i++) {
		char *error;
		char *s = conf[i];
		if (!s)
			s = def_conf;
		if (setup_one_line(serial_lines, i, s, &opts, &error))
			printk(KERN_ERR "setup_one_line failed for "
			       "device %d : %s\n", i, error);
	}

	ssl_init_done = 1;
	register_console(&ssl_cons);
	return 0;
}
late_initcall(ssl_init);

static void ssl_exit(void)
{
	if (!ssl_init_done)
		return;
	close_lines(serial_lines, ARRAY_SIZE(serial_lines));
}
__uml_exitcall(ssl_exit);

static int ssl_chan_setup(char *str)
{
	line_setup(conf, NR_PORTS, &def_conf, str, "serial line");
	return 1;
}

__setup("ssl", ssl_chan_setup);
__channel_help(ssl_chan_setup, "ssl");

static int ssl_non_raw_setup(char *str)
{
	opts.raw = 0;
	return 1;
}
__setup("ssl-non-raw", ssl_non_raw_setup);
__uml_help(ssl_non_raw_setup,
"ssl-non-raw\n"
"    Set serial lines to non-raw mode.\n\n"
);
