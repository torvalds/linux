/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/fs.h"
#include "linux/tty.h"
#include "linux/tty_driver.h"
#include "linux/major.h"
#include "linux/mm.h"
#include "linux/init.h"
#include "linux/console.h"
#include "asm/termbits.h"
#include "asm/irq.h"
#include "line.h"
#include "ssl.h"
#include "chan_kern.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "init.h"
#include "irq_user.h"
#include "mconsole_kern.h"

static const int ssl_version = 1;

/* Referenced only by tty_driver below - presumably it's locked correctly
 * by the tty driver.
 */

static struct tty_driver *ssl_driver;

#define NR_PORTS 64

static void ssl_announce(char *dev_name, int dev)
{
	printk(KERN_INFO "Serial line %d assigned device '%s'\n", dev,
	       dev_name);
}

static struct chan_opts opts = {
	.announce 	= ssl_announce,
	.xterm_title	= "Serial Line #%d",
	.raw		= 1,
	.tramp_stack 	= 0,
	.in_kernel 	= 1,
};

static int ssl_config(char *str);
static int ssl_get_config(char *dev, char *str, int size, char **error_out);
static int ssl_remove(int n);

static struct line_driver driver = {
	.name 			= "UML serial line",
	.device_name 		= "ttyS",
	.major 			= TTY_MAJOR,
	.minor_start 		= 64,
	.type 		 	= TTY_DRIVER_TYPE_SERIAL,
	.subtype 	 	= 0,
	.read_irq 		= SSL_IRQ,
	.read_irq_name 		= "ssl",
	.write_irq 		= SSL_WRITE_IRQ,
	.write_irq_name 	= "ssl-write",
	.symlink_from 		= "serial",
	.symlink_to 		= "tts",
	.mc  = {
		.name  		= "ssl",
		.config 	= ssl_config,
		.get_config 	= ssl_get_config,
		.id		= line_id,
		.remove 	= ssl_remove,
	},
};

/* The array is initialized by line_init, which is an initcall.  The 
 * individual elements are protected by individual semaphores.
 */
static struct line serial_lines[NR_PORTS] =
	{ [0 ... NR_PORTS - 1] = LINE_INIT(CONFIG_SSL_CHAN, &driver) };

static struct lines lines = LINES_INIT(NR_PORTS);

static int ssl_config(char *str)
{
	return line_config(serial_lines, ARRAY_SIZE(serial_lines), str, &opts);
}

static int ssl_get_config(char *dev, char *str, int size, char **error_out)
{
	return line_get_config(dev, serial_lines, ARRAY_SIZE(serial_lines), str,
			       size, error_out);
}

static int ssl_remove(int n)
{
	return line_remove(serial_lines, ARRAY_SIZE(serial_lines), n);
}

static int ssl_open(struct tty_struct *tty, struct file *filp)
{
	return line_open(serial_lines, tty);
}

#if 0
static void ssl_flush_buffer(struct tty_struct *tty)
{
	return;
}

static void ssl_stop(struct tty_struct *tty)
{
	printk(KERN_ERR "Someone should implement ssl_stop\n");
}

static void ssl_start(struct tty_struct *tty)
{
	printk(KERN_ERR "Someone should implement ssl_start\n");
}

void ssl_hangup(struct tty_struct *tty)
{
}
#endif

static const struct tty_operations ssl_ops = {
	.open 	 		= ssl_open,
	.close 	 		= line_close,
	.write 	 		= line_write,
	.put_char 		= line_put_char,
	.write_room		= line_write_room,
	.chars_in_buffer 	= line_chars_in_buffer,
	.flush_buffer 		= line_flush_buffer,
	.flush_chars 		= line_flush_chars,
	.set_termios 		= line_set_termios,
	.ioctl 	 		= line_ioctl,
	.throttle 		= line_throttle,
	.unthrottle 		= line_unthrottle,
#if 0
	.stop 	 		= ssl_stop,
	.start 	 		= ssl_start,
	.hangup 	 	= ssl_hangup,
#endif
};

/* Changed by ssl_init and referenced by ssl_exit, which are both serialized
 * by being an initcall and exitcall, respectively.
 */
static int ssl_init_done = 0;

static void ssl_console_write(struct console *c, const char *string,
			      unsigned len)
{
	struct line *line = &serial_lines[c->index];
	unsigned long flags;

	spin_lock_irqsave(&line->lock, flags);
	console_write_chan(&line->chan_list, string, len);
	spin_unlock_irqrestore(&line->lock, flags);
}

static struct tty_driver *ssl_console_device(struct console *c, int *index)
{
	*index = c->index;
	return ssl_driver;
}

static int ssl_console_setup(struct console *co, char *options)
{
	struct line *line = &serial_lines[co->index];

	return console_open_chan(line, co, &opts);
}

static struct console ssl_cons = {
	.name		= "ttyS",
	.write		= ssl_console_write,
	.device		= ssl_console_device,
	.setup		= ssl_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static int ssl_init(void)
{
	char *new_title;

	printk(KERN_INFO "Initializing software serial port version %d\n",
	       ssl_version);
	ssl_driver = line_register_devfs(&lines, &driver, &ssl_ops,
					 serial_lines,
					 ARRAY_SIZE(serial_lines));

	lines_init(serial_lines, ARRAY_SIZE(serial_lines), &opts);

	new_title = add_xterm_umid(opts.xterm_title);
	if (new_title != NULL)
		opts.xterm_title = new_title;

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
	return line_setup(serial_lines, ARRAY_SIZE(serial_lines), str);
}

__setup("ssl", ssl_chan_setup);
__channel_help(ssl_chan_setup, "ssl");
