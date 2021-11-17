// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * polling mode stateless debugging stuff, originally for NS16550 Serial Ports
 *
 * c 2001 PPC 64 Team, IBM Corp
 */

#include <linux/stdarg.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/udbg.h>

void (*udbg_putc)(char c);
void (*udbg_flush)(void);
int (*udbg_getc)(void);
int (*udbg_getc_poll)(void);

/*
 * Early debugging facilities. You can enable _one_ of these via .config,
 * if you do so your kernel _will not boot_ on anything else. Be careful.
 */
void __init udbg_early_init(void)
{
#if defined(CONFIG_PPC_EARLY_DEBUG_LPAR)
	/* For LPAR machines that have an HVC console on vterm 0 */
	udbg_init_debug_lpar();
#elif defined(CONFIG_PPC_EARLY_DEBUG_LPAR_HVSI)
	/* For LPAR machines that have an HVSI console on vterm 0 */
	udbg_init_debug_lpar_hvsi();
#elif defined(CONFIG_PPC_EARLY_DEBUG_G5)
	/* For use on Apple G5 machines */
	udbg_init_pmac_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_RTAS_PANEL)
	/* RTAS panel debug */
	udbg_init_rtas_panel();
#elif defined(CONFIG_PPC_EARLY_DEBUG_RTAS_CONSOLE)
	/* RTAS console debug */
	udbg_init_rtas_console();
#elif defined(CONFIG_PPC_EARLY_DEBUG_MAPLE)
	/* Maple real mode debug */
	udbg_init_maple_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_PAS_REALMODE)
	udbg_init_pas_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_BOOTX)
	udbg_init_btext();
#elif defined(CONFIG_PPC_EARLY_DEBUG_44x)
	/* PPC44x debug */
	udbg_init_44x_as1();
#elif defined(CONFIG_PPC_EARLY_DEBUG_40x)
	/* PPC40x debug */
	udbg_init_40x_realmode();
#elif defined(CONFIG_PPC_EARLY_DEBUG_CPM)
	udbg_init_cpm();
#elif defined(CONFIG_PPC_EARLY_DEBUG_USBGECKO)
	udbg_init_usbgecko();
#elif defined(CONFIG_PPC_EARLY_DEBUG_MEMCONS)
	/* In memory console */
	udbg_init_memcons();
#elif defined(CONFIG_PPC_EARLY_DEBUG_EHV_BC)
	udbg_init_ehv_bc();
#elif defined(CONFIG_PPC_EARLY_DEBUG_PS3GELIC)
	udbg_init_ps3gelic();
#elif defined(CONFIG_PPC_EARLY_DEBUG_OPAL_RAW)
	udbg_init_debug_opal_raw();
#elif defined(CONFIG_PPC_EARLY_DEBUG_OPAL_HVSI)
	udbg_init_debug_opal_hvsi();
#endif

#ifdef CONFIG_PPC_EARLY_DEBUG
	console_loglevel = CONSOLE_LOGLEVEL_DEBUG;

	register_early_udbg_console();
#endif
}

/* udbg library, used by xmon et al */
void udbg_puts(const char *s)
{
	if (udbg_putc) {
		char c;

		if (s && *s != '\0') {
			while ((c = *s++) != '\0')
				udbg_putc(c);
		}

		if (udbg_flush)
			udbg_flush();
	}
#if 0
	else {
		printk("%s", s);
	}
#endif
}

int udbg_write(const char *s, int n)
{
	int remain = n;
	char c;

	if (!udbg_putc)
		return 0;

	if (s && *s != '\0') {
		while (((c = *s++) != '\0') && (remain-- > 0)) {
			udbg_putc(c);
		}
	}

	if (udbg_flush)
		udbg_flush();

	return n - remain;
}

#define UDBG_BUFSIZE 256
void udbg_printf(const char *fmt, ...)
{
	if (udbg_putc) {
		char buf[UDBG_BUFSIZE];
		va_list args;

		va_start(args, fmt);
		vsnprintf(buf, UDBG_BUFSIZE, fmt, args);
		udbg_puts(buf);
		va_end(args);
	}
}

void __init udbg_progress(char *s, unsigned short hex)
{
	udbg_puts(s);
	udbg_puts("\n");
}

/*
 * Early boot console based on udbg
 */
static void udbg_console_write(struct console *con, const char *s,
		unsigned int n)
{
	udbg_write(s, n);
}

static struct console udbg_console = {
	.name	= "udbg",
	.write	= udbg_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_BOOT | CON_ANYTIME,
	.index	= 0,
};

/*
 * Called by setup_system after ppc_md->probe and ppc_md->early_init.
 * Call it again after setting udbg_putc in ppc_md->setup_arch.
 */
void __init register_early_udbg_console(void)
{
	if (early_console)
		return;

	if (!udbg_putc)
		return;

	if (strstr(boot_command_line, "udbg-immortal")) {
		printk(KERN_INFO "early console immortal !\n");
		udbg_console.flags &= ~CON_BOOT;
	}
	early_console = &udbg_console;
	register_console(&udbg_console);
}

#if 0   /* if you want to use this as a regular output console */
console_initcall(register_udbg_console);
#endif
