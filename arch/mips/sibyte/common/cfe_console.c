#include <linux/init.h>
#include <linux/errno.h>
#include <linux/console.h>

#include <asm/sibyte/board.h>

#include <asm/fw/cfe/cfe_api.h>
#include <asm/fw/cfe/cfe_error.h>

extern int cfe_cons_handle;

static void cfe_console_write(struct console *cons, const char *str,
		       unsigned int count)
{
	int i, last, written;

	for (i=0, last=0; i<count; i++) {
		if (!str[i])
			/* XXXKW can/should this ever happen? */
			return;
		if (str[i] == '\n') {
			do {
				written = cfe_write(cfe_cons_handle, &str[last], i-last);
				if (written < 0)
					;
				last += written;
			} while (last < i);
			while (cfe_write(cfe_cons_handle, "\r", 1) <= 0)
				;
		}
	}
	if (last != count) {
		do {
			written = cfe_write(cfe_cons_handle, &str[last], count-last);
			if (written < 0)
				;
			last += written;
		} while (last < count);
	}

}

static int cfe_console_setup(struct console *cons, char *str)
{
	char consdev[32];
	/* XXXKW think about interaction with 'console=' cmdline arg */
	/* If none of the console options are configured, the build will break. */
	if (cfe_getenv("BOOT_CONSOLE", consdev, 32) >= 0) {
#ifdef CONFIG_SERIAL_SB1250_DUART
		if (!strcmp(consdev, "uart0")) {
			setleds("u0cn");
		} else if (!strcmp(consdev, "uart1")) {
			setleds("u1cn");
#endif
#ifdef CONFIG_VGA_CONSOLE
		} else if (!strcmp(consdev, "pcconsole0")) {
			setleds("pccn");
#endif
		} else
			return -ENODEV;
	}
	return 0;
}

static struct console sb1250_cfe_cons = {
	.name		= "cfe",
	.write		= cfe_console_write,
	.setup		= cfe_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static int __init sb1250_cfe_console_init(void)
{
	register_console(&sb1250_cfe_cons);
	return 0;
}

console_initcall(sb1250_cfe_console_init);
