/*
 * Based on the same principle as kgdboe using the NETPOLL api, this
 * driver uses a console polling api to implement a gdb serial inteface
 * which is multiplexed on a console port.
 *
 * Maintainer: Jason Wessel <jason.wessel@windriver.com>
 *
 * 2007-2008 (c) Jason Wessel - Wind River Systems, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/kgdb.h>
#include <linux/tty.h>

#define MAX_CONFIG_LEN		40

static struct kgdb_io		kgdboc_io_ops;

/* -1 = init not run yet, 0 = unconfigured, 1 = configured. */
static int configured		= -1;

static char config[MAX_CONFIG_LEN];
static struct kparam_string kps = {
	.string			= config,
	.maxlen			= MAX_CONFIG_LEN,
};

static struct tty_driver	*kgdb_tty_driver;
static int			kgdb_tty_line;

static int kgdboc_option_setup(char *opt)
{
	if (strlen(opt) > MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdboc: config string too long\n");
		return -ENOSPC;
	}
	strcpy(config, opt);

	return 0;
}

__setup("kgdboc=", kgdboc_option_setup);

static int configure_kgdboc(void)
{
	struct tty_driver *p;
	int tty_line = 0;
	int err;

	err = kgdboc_option_setup(config);
	if (err || !strlen(config) || isspace(config[0]))
		goto noconfig;

	err = -ENODEV;

	p = tty_find_polling_driver(config, &tty_line);
	if (!p)
		goto noconfig;

	kgdb_tty_driver = p;
	kgdb_tty_line = tty_line;

	err = kgdb_register_io_module(&kgdboc_io_ops);
	if (err)
		goto noconfig;

	configured = 1;

	return 0;

noconfig:
	config[0] = 0;
	configured = 0;

	return err;
}

static int __init init_kgdboc(void)
{
	/* Already configured? */
	if (configured == 1)
		return 0;

	return configure_kgdboc();
}

static void cleanup_kgdboc(void)
{
	if (configured == 1)
		kgdb_unregister_io_module(&kgdboc_io_ops);
}

static int kgdboc_get_char(void)
{
	return kgdb_tty_driver->ops->poll_get_char(kgdb_tty_driver,
						kgdb_tty_line);
}

static void kgdboc_put_char(u8 chr)
{
	kgdb_tty_driver->ops->poll_put_char(kgdb_tty_driver,
					kgdb_tty_line, chr);
}

static int param_set_kgdboc_var(const char *kmessage, struct kernel_param *kp)
{
	int len = strlen(kmessage);

	if (len >= MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdboc: config string too long\n");
		return -ENOSPC;
	}

	/* Only copy in the string if the init function has not run yet */
	if (configured < 0) {
		strcpy(config, kmessage);
		return 0;
	}

	if (kgdb_connected) {
		printk(KERN_ERR
		       "kgdboc: Cannot reconfigure while KGDB is connected.\n");

		return -EBUSY;
	}

	strcpy(config, kmessage);
	/* Chop out \n char as a result of echo */
	if (config[len - 1] == '\n')
		config[len - 1] = '\0';

	if (configured == 1)
		cleanup_kgdboc();

	/* Go and configure with the new params. */
	return configure_kgdboc();
}

static void kgdboc_pre_exp_handler(void)
{
	/* Increment the module count when the debugger is active */
	if (!kgdb_connected)
		try_module_get(THIS_MODULE);
}

static void kgdboc_post_exp_handler(void)
{
	/* decrement the module count when the debugger detaches */
	if (!kgdb_connected)
		module_put(THIS_MODULE);
}

static struct kgdb_io kgdboc_io_ops = {
	.name			= "kgdboc",
	.read_char		= kgdboc_get_char,
	.write_char		= kgdboc_put_char,
	.pre_exception		= kgdboc_pre_exp_handler,
	.post_exception		= kgdboc_post_exp_handler,
};

module_init(init_kgdboc);
module_exit(cleanup_kgdboc);
module_param_call(kgdboc, param_set_kgdboc_var, param_get_string, &kps, 0644);
MODULE_PARM_DESC(kgdboc, "<serial_device>[,baud]");
MODULE_DESCRIPTION("KGDB Console TTY Driver");
MODULE_LICENSE("GPL");
