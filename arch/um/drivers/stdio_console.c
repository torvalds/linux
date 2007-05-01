/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/posix_types.h"
#include "linux/tty.h"
#include "linux/tty_flip.h"
#include "linux/types.h"
#include "linux/major.h"
#include "linux/kdev_t.h"
#include "linux/console.h"
#include "linux/string.h"
#include "linux/sched.h"
#include "linux/list.h"
#include "linux/init.h"
#include "linux/interrupt.h"
#include "linux/slab.h"
#include "linux/hardirq.h"
#include "asm/current.h"
#include "asm/irq.h"
#include "stdio_console.h"
#include "line.h"
#include "chan_kern.h"
#include "user_util.h"
#include "kern_util.h"
#include "irq_user.h"
#include "mconsole_kern.h"
#include "init.h"

#define MAX_TTYS (16)

/* Referenced only by tty_driver below - presumably it's locked correctly
 * by the tty driver.
 */

static struct tty_driver *console_driver;

void stdio_announce(char *dev_name, int dev)
{
	printk(KERN_INFO "Virtual console %d assigned device '%s'\n", dev,
	       dev_name);
}

/* Almost const, except that xterm_title may be changed in an initcall */
static struct chan_opts opts = {
	.announce 	= stdio_announce,
	.xterm_title	= "Virtual Console #%d",
	.raw		= 1,
	.tramp_stack 	= 0,
	.in_kernel 	= 1,
};

static int con_config(char *str, char **error_out);
static int con_get_config(char *dev, char *str, int size, char **error_out);
static int con_remove(int n, char **con_remove);


/* Const, except for .mc.list */
static struct line_driver driver = {
	.name 			= "UML console",
	.device_name 		= "tty",
	.major 			= TTY_MAJOR,
	.minor_start 		= 0,
	.type 		 	= TTY_DRIVER_TYPE_CONSOLE,
	.subtype 	 	= SYSTEM_TYPE_CONSOLE,
	.read_irq 		= CONSOLE_IRQ,
	.read_irq_name 		= "console",
	.write_irq 		= CONSOLE_WRITE_IRQ,
	.write_irq_name 	= "console-write",
	.mc  = {
		.list		= LIST_HEAD_INIT(driver.mc.list),
		.name  		= "con",
		.config 	= con_config,
		.get_config 	= con_get_config,
		.id		= line_id,
		.remove 	= con_remove,
	},
};

/* The array is initialized by line_init, at initcall time.  The
 * elements are locked individually as needed.
 */
static struct line vts[MAX_TTYS] = { LINE_INIT(CONFIG_CON_ZERO_CHAN, &driver),
				     [ 1 ... MAX_TTYS - 1 ] =
				     LINE_INIT(CONFIG_CON_CHAN, &driver) };

static int con_config(char *str, char **error_out)
{
	return line_config(vts, ARRAY_SIZE(vts), str, &opts, error_out);
}

static int con_get_config(char *dev, char *str, int size, char **error_out)
{
	return line_get_config(dev, vts, ARRAY_SIZE(vts), str, size, error_out);
}

static int con_remove(int n, char **error_out)
{
	return line_remove(vts, ARRAY_SIZE(vts), n, error_out);
}

static int con_open(struct tty_struct *tty, struct file *filp)
{
	return line_open(vts, tty);
}

/* Set in an initcall, checked in an exitcall */
static int con_init_done = 0;

static const struct tty_operations console_ops = {
	.open 	 		= con_open,
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
};

static void uml_console_write(struct console *console, const char *string,
			      unsigned len)
{
	struct line *line = &vts[console->index];
	unsigned long flags;

	spin_lock_irqsave(&line->lock, flags);
	console_write_chan(&line->chan_list, string, len);
	spin_unlock_irqrestore(&line->lock, flags);
}

static struct tty_driver *uml_console_device(struct console *c, int *index)
{
	*index = c->index;
	return console_driver;
}

static int uml_console_setup(struct console *co, char *options)
{
	struct line *line = &vts[co->index];

	return console_open_chan(line, co);
}

/* No locking for register_console call - relies on single-threaded initcalls */
static struct console stdiocons = {
	.name		= "tty",
	.write		= uml_console_write,
	.device		= uml_console_device,
	.setup		= uml_console_setup,
	.flags		= CON_PRINTBUFFER|CON_ANYTIME,
	.index		= -1,
};

int stdio_init(void)
{
	char *new_title;

	console_driver = register_lines(&driver, &console_ops, vts,
					ARRAY_SIZE(vts));
	if (console_driver == NULL)
		return -1;
	printk(KERN_INFO "Initialized stdio console driver\n");

	lines_init(vts, ARRAY_SIZE(vts), &opts);

	new_title = add_xterm_umid(opts.xterm_title);
	if(new_title != NULL)
		opts.xterm_title = new_title;

	con_init_done = 1;
	register_console(&stdiocons);
	return 0;
}
late_initcall(stdio_init);

static void console_exit(void)
{
	if (!con_init_done)
		return;
	close_lines(vts, ARRAY_SIZE(vts));
}
__uml_exitcall(console_exit);

static int console_chan_setup(char *str)
{
	char *error;
	int ret;

	ret = line_setup(vts, ARRAY_SIZE(vts), str, &error);
	if(ret < 0)
		printk(KERN_ERR "Failed to set up console with "
		       "configuration string \"%s\" : %s\n", str, error);

	return 1;
}
__setup("con", console_chan_setup);
__channel_help(console_chan_setup, "con");
