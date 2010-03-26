/*
 * linux/arch/arm/plat-omap/common.c
 *
 * Code common to all OMAP machines.
 * The file is created by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/control.h>
#include <plat/mux.h>
#include <plat/fpga.h>
#include <plat/serial.h>

#include <plat/clock.h>

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
# include "../mach-omap2/sdrc.h"
#endif

#define NO_LENGTH_CHECK 0xffffffff

unsigned char omap_bootloader_tag[512];
int omap_bootloader_tag_len;

struct omap_board_config_kernel *omap_board_config;
int omap_board_config_size;

/* used by omap-smp.c and board-4430sdp.c */
void __iomem *gic_cpu_base_addr;

static const void *get_config(u16 tag, size_t len, int skip, size_t *len_out)
{
	struct omap_board_config_kernel *kinfo = NULL;
	int i;

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

/*
 * 32KHz clocksource ... always available, on pretty most chips except
 * OMAP 730 and 1510.  Other timers could be used as clocksources, with
 * higher resolution in free-running counter modes (e.g. 12 MHz xtal),
 * but systems won't necessarily want to spend resources that way.
 */

#define OMAP16XX_TIMER_32K_SYNCHRONIZED		0xfffbc410

#if !(defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP15XX))

#include <linux/clocksource.h>

#ifdef CONFIG_ARCH_OMAP16XX
static cycle_t omap16xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP16XX_TIMER_32K_SYNCHRONIZED);
}
#else
#define omap16xx_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP2420
static cycle_t omap2420_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP2420_32KSYNCT_BASE + 0x10);
}
#else
#define omap2420_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP2430
static cycle_t omap2430_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP2430_32KSYNCT_BASE + 0x10);
}
#else
#define omap2430_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP3
static cycle_t omap34xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP3430_32KSYNCT_BASE + 0x10);
}
#else
#define omap34xx_32k_read	NULL
#endif

#ifdef CONFIG_ARCH_OMAP4
static cycle_t omap44xx_32k_read(struct clocksource *cs)
{
	return omap_readl(OMAP4430_32KSYNCT_BASE + 0x10);
}
#else
#define omap44xx_32k_read	NULL
#endif

/*
 * Kernel assumes that sched_clock can be called early but may not have
 * things ready yet.
 */
static cycle_t omap_32k_read_dummy(struct clocksource *cs)
{
	return 0;
}

static struct clocksource clocksource_32k = {
	.name		= "32k_counter",
	.rating		= 250,
	.read		= omap_32k_read_dummy,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 10,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * Returns current time from boot in nsecs. It's OK for this to wrap
 * around for now, as it's just a relative time stamp.
 */
unsigned long long sched_clock(void)
{
	return clocksource_cyc2ns(clocksource_32k.read(&clocksource_32k),
				  clocksource_32k.mult, clocksource_32k.shift);
}

/**
 * read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 */
static struct timespec persistent_ts;
static cycles_t cycles, last_cycles;
void read_persistent_clock(struct timespec *ts)
{
	unsigned long long nsecs;
	cycles_t delta;
	struct timespec *tsp = &persistent_ts;

	last_cycles = cycles;
	cycles = clocksource_32k.read(&clocksource_32k);
	delta = cycles - last_cycles;

	nsecs = clocksource_cyc2ns(delta,
				   clocksource_32k.mult, clocksource_32k.shift);

	timespec_add_ns(tsp, nsecs);
	*ts = *tsp;
}

static int __init omap_init_clocksource_32k(void)
{
	static char err[] __initdata = KERN_ERR
			"%s: can't register clocksource!\n";

	if (cpu_is_omap16xx() || cpu_class_is_omap2()) {
		struct clk *sync_32k_ick;

		if (cpu_is_omap16xx())
			clocksource_32k.read = omap16xx_32k_read;
		else if (cpu_is_omap2420())
			clocksource_32k.read = omap2420_32k_read;
		else if (cpu_is_omap2430())
			clocksource_32k.read = omap2430_32k_read;
		else if (cpu_is_omap34xx())
			clocksource_32k.read = omap34xx_32k_read;
		else if (cpu_is_omap44xx())
			clocksource_32k.read = omap44xx_32k_read;
		else
			return -ENODEV;

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

#endif	/* !(defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP15XX)) */

/* Global address base setup code */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)

static void __init __omap2_set_globals(struct omap_globals *omap2_globals)
{
	omap2_set_globals_tap(omap2_globals);
	omap2_set_globals_sdrc(omap2_globals);
	omap2_set_globals_control(omap2_globals);
	omap2_set_globals_prcm(omap2_globals);
	omap2_set_globals_uart(omap2_globals);
}

#endif

#if defined(CONFIG_ARCH_OMAP2420)

static struct omap_globals omap242x_globals = {
	.class	= OMAP242X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x48014000),
	.sdrc	= OMAP2420_SDRC_BASE,
	.sms	= OMAP2420_SMS_BASE,
	.ctrl	= OMAP2420_CTRL_BASE,
	.prm	= OMAP2420_PRM_BASE,
	.cm	= OMAP2420_CM_BASE,
	.uart1_phys	= OMAP2_UART1_BASE,
	.uart2_phys	= OMAP2_UART2_BASE,
	.uart3_phys	= OMAP2_UART3_BASE,
};

void __init omap2_set_globals_242x(void)
{
	__omap2_set_globals(&omap242x_globals);
}
#endif

#if defined(CONFIG_ARCH_OMAP2430)

static struct omap_globals omap243x_globals = {
	.class	= OMAP243X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x4900a000),
	.sdrc	= OMAP243X_SDRC_BASE,
	.sms	= OMAP243X_SMS_BASE,
	.ctrl	= OMAP243X_CTRL_BASE,
	.prm	= OMAP2430_PRM_BASE,
	.cm	= OMAP2430_CM_BASE,
	.uart1_phys	= OMAP2_UART1_BASE,
	.uart2_phys	= OMAP2_UART2_BASE,
	.uart3_phys	= OMAP2_UART3_BASE,
};

void __init omap2_set_globals_243x(void)
{
	__omap2_set_globals(&omap243x_globals);
}
#endif

#if defined(CONFIG_ARCH_OMAP3)

static struct omap_globals omap3_globals = {
	.class	= OMAP343X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x4830A000),
	.sdrc	= OMAP343X_SDRC_BASE,
	.sms	= OMAP343X_SMS_BASE,
	.ctrl	= OMAP343X_CTRL_BASE,
	.prm	= OMAP3430_PRM_BASE,
	.cm	= OMAP3430_CM_BASE,
	.uart1_phys	= OMAP3_UART1_BASE,
	.uart2_phys	= OMAP3_UART2_BASE,
	.uart3_phys	= OMAP3_UART3_BASE,
};

void __init omap2_set_globals_343x(void)
{
	__omap2_set_globals(&omap3_globals);
}

void __init omap2_set_globals_36xx(void)
{
	omap3_globals.uart4_phys = OMAP3_UART4_BASE;

	__omap2_set_globals(&omap3_globals);
}
#endif

#if defined(CONFIG_ARCH_OMAP4)
static struct omap_globals omap4_globals = {
	.class	= OMAP443X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(OMAP443X_SCM_BASE),
	.ctrl	= OMAP443X_CTRL_BASE,
	.prm	= OMAP4430_PRM_BASE,
	.cm	= OMAP4430_CM_BASE,
	.cm2	= OMAP4430_CM2_BASE,
	.uart1_phys	= OMAP4_UART1_BASE,
	.uart2_phys	= OMAP4_UART2_BASE,
	.uart3_phys	= OMAP4_UART3_BASE,
	.uart4_phys	= OMAP4_UART4_BASE,
};

void __init omap2_set_globals_443x(void)
{
	omap2_set_globals_tap(&omap4_globals);
	omap2_set_globals_control(&omap4_globals);
	omap2_set_globals_prcm(&omap4_globals);
	omap2_set_globals_uart(&omap4_globals);
}
#endif

