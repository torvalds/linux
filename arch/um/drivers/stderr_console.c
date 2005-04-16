#include <linux/init.h>
#include <linux/console.h>

#include "chan_user.h"

/* ----------------------------------------------------------------------------- */
/* trivial console driver -- simply dump everything to stderr                    */

/*
 * Don't register by default -- as this registeres very early in the
 * boot process it becomes the default console.  And as this isn't a
 * real tty driver init isn't able to open /dev/console then.
 *
 * In most cases this isn't what you want ...
 */
static int use_stderr_console = 0;

static void stderr_console_write(struct console *console, const char *string,
				 unsigned len)
{
	generic_write(2 /* stderr */, string, len, NULL);
}

static struct console stderr_console = {
	.name		"stderr",
	.write		stderr_console_write,
	.flags		CON_PRINTBUFFER,
};

static int __init stderr_console_init(void)
{
	if (use_stderr_console)
		register_console(&stderr_console);
	return 0;
}
console_initcall(stderr_console_init);

static int stderr_setup(char *str)
{
	if (!str)
		return 0;
	use_stderr_console = simple_strtoul(str,&str,0);
	return 1;
}
__setup("stderr=", stderr_setup);
