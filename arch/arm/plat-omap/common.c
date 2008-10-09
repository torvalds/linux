/*
 * linux/arch/arm/plat-omap/common.c
 *
 * Code common to all OMAP machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <mach/common.h>
#include <mach/board.h>
#include <mach/control.h>
#include <mach/mux.h>
#include <mach/fpga.h>

#include <mach/clock.h>

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
# include "../mach-omap2/sdrc.h"
#endif

#define NO_LENGTH_CHECK 0xffffffff

unsigned char omap_bootloader_tag[512];
int omap_bootloader_tag_len;

struct omap_board_config_kernel *omap_board_config;
int omap_board_config_size;

static const void *get_config(u16 tag, size_t len, int skip, size_t *len_out)
{
	struct omap_board_config_kernel *kinfo = NULL;
	int i;

#ifdef CONFIG_OMAP_BOOT_TAG
	struct omap_board_config_entry *info = NULL;

	if (omap_bootloader_tag_len > 4)
		info = (struct omap_board_config_entry *) omap_bootloader_tag;
	while (info != NULL) {
		u8 *next;

		if (info->tag == tag) {
			if (skip == 0)
				break;
			skip--;
		}

		if ((info->len & 0x03) != 0) {
			/* We bail out to avoid an alignment fault */
			printk(KERN_ERR "OMAP peripheral config: Length (%d) not word-aligned (tag %04x)\n",
			       info->len, info->tag);
			return NULL;
		}
		next = (u8 *) info + sizeof(*info) + info->len;
		if (next >= omap_bootloader_tag + omap_bootloader_tag_len)
			info = NULL;
		else
			info = (struct omap_board_config_entry *) next;
	}
	if (info != NULL) {
		/* Check the length as a lame attempt to check for
		 * binary inconsistency. */
		if (len != NO_LENGTH_CHECK) {
			/* Word-align len */
			if (len & 0x03)
				len = (len + 3) & ~0x03;
			if (info->len != len) {
				printk(KERN_ERR "OMAP peripheral config: Length mismatch with tag %x (want %d, got %d)\n",
				       tag, len, info->len);
				return NULL;
			}
		}
		if (len_out != NULL)
			*len_out = info->len;
		return info->data;
	}
#endif
	/* Try to find the config from the board-specific structures
	 * in the kernel. */
	for (i = 0; i < omap_board_config_size; i++) {
		if (omap_board_config[i].tag == tag) {
			if (skip == 0) {
				kinfo = &omap_board_config[i];
				break;
			} else {
				skip--;
			}
		}
	}
	if (kinfo == NULL)
		return NULL;
	return kinfo->data;
}

const void *__omap_get_config(u16 tag, size_t len, int nr)
{
        return get_config(tag, len, nr, NULL);
}
EXPORT_SYMBOL(__omap_get_config);

const void *omap_get_var_config(u16 tag, size_t *len)
{
        return get_config(tag, NO_LENGTH_CHECK, 0, len);
}
EXPORT_SYMBOL(omap_get_var_config);

static int __init omap_add_serial_console(void)
{
	const struct omap_serial_console_config *con_info;
	const struct omap_uart_config *uart_info;
	static char speed[11], *opt = NULL;
	int line, i, uart_idx;

	uart_info = omap_get_config(OMAP_TAG_UART, struct omap_uart_config);
	con_info = omap_get_config(OMAP_TAG_SERIAL_CONSOLE,
					struct omap_serial_console_config);
	if (uart_info == NULL || con_info == NULL)
		return 0;

	if (con_info->console_uart == 0)
		return 0;

	if (con_info->console_speed) {
		snprintf(speed, sizeof(speed), "%u", con_info->console_speed);
		opt = speed;
	}

	uart_idx = con_info->console_uart - 1;
	if (uart_idx >= OMAP_MAX_NR_PORTS) {
		printk(KERN_INFO "Console: external UART#%d. "
			"Not adding it as console this time.\n",
			uart_idx + 1);
		return 0;
	}
	if (!(uart_info->enabled_uarts & (1 << uart_idx))) {
		printk(KERN_ERR "Console: Selected UART#%d is "
			"not enabled for this platform\n",
			uart_idx + 1);
		return -1;
	}
	line = 0;
	for (i = 0; i < uart_idx; i++) {
		if (uart_info->enabled_uarts & (1 << i))
			line++;
	}
	return add_preferred_console("ttyS", line, opt);
}
console_initcall(omap_add_serial_console);


/*
 * 32KHz clocksource ... always available, on pretty most chips except
 * OMAP 730 and 1510.  Other timers could be used as clocksources, with
 * higher resolution in free-running counter modes (e.g. 12 MHz xtal),
 * but systems won't necessarily want to spend resources that way.
 */

#if defined(CONFIG_ARCH_OMAP16XX)
#define TIMER_32K_SYNCHRONIZED		0xfffbc410
#elif defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
#define TIMER_32K_SYNCHRONIZED		(OMAP2_32KSYNCT_BASE + 0x10)
#endif

#ifdef	TIMER_32K_SYNCHRONIZED

#include <linux/clocksource.h>

static cycle_t omap_32k_read(void)
{
	return omap_readl(TIMER_32K_SYNCHRONIZED);
}

static struct clocksource clocksource_32k = {
	.name		= "32k_counter",
	.rating		= 250,
	.read		= omap_32k_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 10,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Rounds down to nearest nsec.
 */
unsigned long long omap_32k_ticks_to_nsecs(unsigned long ticks_32k)
{
	return cyc2ns(&clocksource_32k, ticks_32k);
}

/*
 * Returns current time from boot in nsecs. It's OK for this to wrap
 * around for now, as it's just a relative time stamp.
 */
unsigned long long sched_clock(void)
{
	return omap_32k_ticks_to_nsecs(omap_32k_read());
}

static int __init omap_init_clocksource_32k(void)
{
	static char err[] __initdata = KERN_ERR
			"%s: can't register clocksource!\n";

	if (cpu_is_omap16xx() || cpu_class_is_omap2()) {
		struct clk *sync_32k_ick;

		sync_32k_ick = clk_get(NULL, "omap_32ksync_ick");
		if (sync_32k_ick)
			clk_enable(sync_32k_ick);

		clocksource_32k.mult = clocksource_hz2mult(32768,
					    clocksource_32k.shift);

		if (clocksource_register(&clocksource_32k))
			printk(err, clocksource_32k.name);
	}
	return 0;
}
arch_initcall(omap_init_clocksource_32k);

#endif	/* TIMER_32K_SYNCHRONIZED */

/* Global address base setup code */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)

static struct omap_globals *omap2_globals;

static void __init __omap2_set_globals(void)
{
	omap2_set_globals_tap(omap2_globals);
	omap2_set_globals_memory(omap2_globals);
	omap2_set_globals_control(omap2_globals);
	omap2_set_globals_prcm(omap2_globals);
}

#endif

#if defined(CONFIG_ARCH_OMAP2420)

static struct omap_globals omap242x_globals = {
	.class	= OMAP242X_CLASS,
	.tap	= OMAP2_IO_ADDRESS(0x48014000),
	.sdrc	= OMAP2_IO_ADDRESS(OMAP2420_SDRC_BASE),
	.sms	= OMAP2_IO_ADDRESS(OMAP2420_SMS_BASE),
	.ctrl	= OMAP2_IO_ADDRESS(OMAP2420_CTRL_BASE),
	.prm	= OMAP2_IO_ADDRESS(OMAP2420_PRM_BASE),
	.cm	= OMAP2_IO_ADDRESS(OMAP2420_CM_BASE),
};

void __init omap2_set_globals_242x(void)
{
	omap2_globals = &omap242x_globals;
	__omap2_set_globals();
}
#endif

#if defined(CONFIG_ARCH_OMAP2430)

static struct omap_globals omap243x_globals = {
	.class	= OMAP243X_CLASS,
	.tap	= OMAP2_IO_ADDRESS(0x4900a000),
	.sdrc	= OMAP2_IO_ADDRESS(OMAP243X_SDRC_BASE),
	.sms	= OMAP2_IO_ADDRESS(OMAP243X_SMS_BASE),
	.ctrl	= OMAP2_IO_ADDRESS(OMAP243X_CTRL_BASE),
	.prm	= OMAP2_IO_ADDRESS(OMAP2430_PRM_BASE),
	.cm	= OMAP2_IO_ADDRESS(OMAP2430_CM_BASE),
};

void __init omap2_set_globals_243x(void)
{
	omap2_globals = &omap243x_globals;
	__omap2_set_globals();
}
#endif

#if defined(CONFIG_ARCH_OMAP3430)

static struct omap_globals omap343x_globals = {
	.class	= OMAP343X_CLASS,
	.tap	= OMAP2_IO_ADDRESS(0x4830A000),
	.sdrc	= OMAP2_IO_ADDRESS(OMAP343X_SDRC_BASE),
	.sms	= OMAP2_IO_ADDRESS(OMAP343X_SMS_BASE),
	.ctrl	= OMAP2_IO_ADDRESS(OMAP343X_CTRL_BASE),
	.prm	= OMAP2_IO_ADDRESS(OMAP3430_PRM_BASE),
	.cm	= OMAP2_IO_ADDRESS(OMAP3430_CM_BASE),
};

void __init omap2_set_globals_343x(void)
{
	omap2_globals = &omap343x_globals;
	__omap2_set_globals();
}
#endif

