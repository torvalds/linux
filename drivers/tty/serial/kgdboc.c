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
#include <linux/kdb.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/input.h>
#include <linux/module.h>

#define MAX_CONFIG_LEN		40

static struct kgdb_io		kgdboc_io_ops;

/* -1 = init not run yet, 0 = unconfigured, 1 = configured. */
static int configured		= -1;

static char config[MAX_CONFIG_LEN];
static struct kparam_string kps = {
	.string			= config,
	.maxlen			= MAX_CONFIG_LEN,
};

static int kgdboc_use_kms;  /* 1 if we use kernel mode switching */
static struct tty_driver	*kgdb_tty_driver;
static int			kgdb_tty_line;

#ifdef CONFIG_KDB_KEYBOARD
static int kgdboc_reset_connect(struct input_handler *handler,
				struct input_dev *dev,
				const struct input_device_id *id)
{
	input_reset_device(dev);

	/* Return an error - we do not want to bind, just to reset */
	return -ENODEV;
}

static void kgdboc_reset_disconnect(struct input_handle *handle)
{
	/* We do not expect anyone to actually bind to us */
	BUG();
}

static const struct input_device_id kgdboc_reset_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ }
};

static struct input_handler kgdboc_reset_handler = {
	.connect	= kgdboc_reset_connect,
	.disconnect	= kgdboc_reset_disconnect,
	.name		= "kgdboc_reset",
	.id_table	= kgdboc_reset_ids,
};

static DEFINE_MUTEX(kgdboc_reset_mutex);

static void kgdboc_restore_input_helper(struct work_struct *dummy)
{
	/*
	 * We need to take a mutex to prevent several instances of
	 * this work running on different CPUs so they don't try
	 * to register again already registered handler.
	 */
	mutex_lock(&kgdboc_reset_mutex);

	if (input_register_handler(&kgdboc_reset_handler) == 0)
		input_unregister_handler(&kgdboc_reset_handler);

	mutex_unlock(&kgdboc_reset_mutex);
}

static DECLARE_WORK(kgdboc_restore_input_work, kgdboc_restore_input_helper);

static void kgdboc_restore_input(void)
{
	if (likely(system_state == SYSTEM_RUNNING))
		schedule_work(&kgdboc_restore_input_work);
}

static int kgdboc_register_kbd(char **cptr)
{
	if (strncmp(*cptr, "kbd", 3) == 0 ||
		strncmp(*cptr, "kdb", 3) == 0) {
		if (kdb_poll_idx < KDB_POLL_FUNC_MAX) {
			kdb_poll_funcs[kdb_poll_idx] = kdb_get_kbd_char;
			kdb_poll_idx++;
			if (cptr[0][3] == ',')
				*cptr += 4;
			else
				return 1;
		}
	}
	return 0;
}

static void kgdboc_unregister_kbd(void)
{
	int i;

	for (i = 0; i < kdb_poll_idx; i++) {
		if (kdb_poll_funcs[i] == kdb_get_kbd_char) {
			kdb_poll_idx--;
			kdb_poll_funcs[i] = kdb_poll_funcs[kdb_poll_idx];
			kdb_poll_funcs[kdb_poll_idx] = NULL;
			i--;
		}
	}
	flush_work(&kgdboc_restore_input_work);
}
#else /* ! CONFIG_KDB_KEYBOARD */
#define kgdboc_register_kbd(x) 0
#define kgdboc_unregister_kbd()
#define kgdboc_restore_input()
#endif /* ! CONFIG_KDB_KEYBOARD */

static int kgdboc_option_setup(char *opt)
{
	if (strlen(opt) >= MAX_CONFIG_LEN) {
		printk(KERN_ERR "kgdboc: config string too long\n");
		return -ENOSPC;
	}
	strcpy(config, opt);

	return 0;
}

__setup("kgdboc=", kgdboc_option_setup);

static void cleanup_kgdboc(void)
{
	if (kgdb_unregister_nmi_console())
		return;
	kgdboc_unregister_kbd();
	if (configured == 1)
		kgdb_unregister_io_module(&kgdboc_io_ops);
}

static int configure_kgdboc(void)
{
	struct tty_driver *p;
	int tty_line = 0;
	int err;
	char *cptr = config;
	struct console *cons;

	err = kgdboc_option_setup(config);
	if (err || !strlen(config) || isspace(config[0]))
		goto noconfig;

	err = -ENODEV;
	kgdboc_io_ops.is_console = 0;
	kgdb_tty_driver = NULL;

	kgdboc_use_kms = 0;
	if (strncmp(cptr, "kms,", 4) == 0) {
		cptr += 4;
		kgdboc_use_kms = 1;
	}

	if (kgdboc_register_kbd(&cptr))
		goto do_register;

	p = tty_find_polling_driver(cptr, &tty_line);
	if (!p)
		goto noconfig;

	cons = console_drivers;
	while (cons) {
		int idx;
		if (cons->device && cons->device(cons, &idx) == p &&
		    idx == tty_line) {
			kgdboc_io_ops.is_console = 1;
			break;
		}
		cons = cons->next;
	}

	kgdb_tty_driver = p;
	kgdb_tty_line = tty_line;

do_register:
	err = kgdb_register_io_module(&kgdboc_io_ops);
	if (err)
		goto noconfig;

	err = kgdb_register_nmi_console();
	if (err)
		goto nmi_con_failed;

	configured = 1;

	return 0;

nmi_con_failed:
	kgdb_unregister_io_module(&kgdboc_io_ops);
noconfig:
	kgdboc_unregister_kbd();
	config[0] = 0;
	configured = 0;
	cleanup_kgdboc();

	return err;
}

static int __init init_kgdboc(void)
{
	/* Already configured? */
	if (configured == 1)
		return 0;

	return configure_kgdboc();
}

static int kgdboc_get_char(void)
{
	if (!kgdb_tty_driver)
		return -1;
	return kgdb_tty_driver->ops->poll_get_char(kgdb_tty_driver,
						kgdb_tty_line);
}

static void kgdboc_put_char(u8 chr)
{
	if (!kgdb_tty_driver)
		return;
	kgdb_tty_driver->ops->poll_put_char(kgdb_tty_driver,
					kgdb_tty_line, chr);
}

static int param_set_kgdboc_var(const char *kmessage,
				const struct kernel_param *kp)
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

static int dbg_restore_graphics;

static void kgdboc_pre_exp_handler(void)
{
	if (!dbg_restore_graphics && kgdboc_use_kms) {
		dbg_restore_graphics = 1;
		con_debug_enter(vc_cons[fg_console].d);
	}
	/* Increment the module count when the debugger is active */
	if (!kgdb_connected)
		try_module_get(THIS_MODULE);
}

static void kgdboc_post_exp_handler(void)
{
	/* decrement the module count when the debugger detaches */
	if (!kgdb_connected)
		module_put(THIS_MODULE);
	if (kgdboc_use_kms && dbg_restore_graphics) {
		dbg_restore_graphics = 0;
		con_debug_leave();
	}
	kgdboc_restore_input();
}

static struct kgdb_io kgdboc_io_ops = {
	.name			= "kgdboc",
	.read_char		= kgdboc_get_char,
	.write_char		= kgdboc_put_char,
	.pre_exception		= kgdboc_pre_exp_handler,
	.post_exception		= kgdboc_post_exp_handler,
};

#ifdef CONFIG_KGDB_SERIAL_CONSOLE
/* This is only available if kgdboc is a built in for early debugging */
static int __init kgdboc_early_init(char *opt)
{
	/* save the first character of the config string because the
	 * init routine can destroy it.
	 */
	char save_ch;

	kgdboc_option_setup(opt);
	save_ch = config[0];
	init_kgdboc();
	config[0] = save_ch;
	return 0;
}

early_param("ekgdboc", kgdboc_early_init);
#endif /* CONFIG_KGDB_SERIAL_CONSOLE */

module_init(init_kgdboc);
module_exit(cleanup_kgdboc);
module_param_call(kgdboc, param_set_kgdboc_var, param_get_string, &kps, 0644);
MODULE_PARM_DESC(kgdboc, "<serial_device>[,baud]");
MODULE_DESCRIPTION("KGDB Console TTY Driver");
MODULE_LICENSE("GPL");
