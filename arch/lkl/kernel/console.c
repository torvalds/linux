#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <asm/host_ops.h>

static void console_write(struct console *con, const char *str, unsigned len)
{
	if (lkl_ops->print)
		lkl_ops->print(str, len);
}

#ifdef CONFIG_LKL_EARLY_CONSOLE
static struct console lkl_boot_console = {
	.name	= "lkl_boot_console",
	.write	= console_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1,
};

int __init lkl_boot_console_init(void)
{
	register_console(&lkl_boot_console);
	return 0;
}
early_initcall(lkl_boot_console_init);
#endif

static struct console lkl_console = {
	.name	= "lkl_console",
	.write	= console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

int __init lkl_console_init(void)
{
	register_console(&lkl_console);
	return 0;
}
core_initcall(lkl_console_init);

