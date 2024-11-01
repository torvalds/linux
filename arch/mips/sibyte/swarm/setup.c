// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000, 2001, 2002, 2003, 2004 Broadcom Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 */

/*
 * Setup code for the SWARM board
 */

#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/screen_info.h>
#include <linux/initrd.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/traps.h>
#include <asm/sibyte/sb1250.h>
#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
#include <asm/sibyte/bcm1480_regs.h>
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
#include <asm/sibyte/sb1250_regs.h>
#else
#error invalid SiByte board configuration
#endif
#include <asm/sibyte/sb1250_genbus.h>
#include <asm/sibyte/board.h>

#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
extern void bcm1480_setup(void);
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
extern void sb1250_setup(void);
#else
#error invalid SiByte board configuration
#endif

extern int xicor_probe(void);
extern int xicor_set_time(time64_t);
extern time64_t xicor_get_time(void);

extern int m41t81_probe(void);
extern int m41t81_set_time(time64_t);
extern time64_t m41t81_get_time(void);

const char *get_system_type(void)
{
	return "SiByte " SIBYTE_BOARD_NAME;
}

int swarm_be_handler(struct pt_regs *regs, int is_fixup)
{
	if (!is_fixup && (regs->cp0_cause & 4)) {
		/* Data bus error - print PA */
		printk("DBE physical address: %010Lx\n",
		       __read_64bit_c0_register($26, 1));
	}
	return is_fixup ? MIPS_BE_FIXUP : MIPS_BE_FATAL;
}

enum swarm_rtc_type {
	RTC_NONE,
	RTC_XICOR,
	RTC_M41T81,
};

enum swarm_rtc_type swarm_rtc_type;

void read_persistent_clock64(struct timespec64 *ts)
{
	time64_t sec;

	switch (swarm_rtc_type) {
	case RTC_XICOR:
		sec = xicor_get_time();
		break;

	case RTC_M41T81:
		sec = m41t81_get_time();
		break;

	case RTC_NONE:
	default:
		sec = mktime64(2000, 1, 1, 0, 0, 0);
		break;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = 0;
}

int update_persistent_clock64(struct timespec64 now)
{
	time64_t sec = now.tv_sec;

	switch (swarm_rtc_type) {
	case RTC_XICOR:
		return xicor_set_time(sec);

	case RTC_M41T81:
		return m41t81_set_time(sec);

	case RTC_NONE:
	default:
		return -1;
	}
}

void __init plat_mem_setup(void)
{
#if defined(CONFIG_SIBYTE_BCM1x55) || defined(CONFIG_SIBYTE_BCM1x80)
	bcm1480_setup();
#elif defined(CONFIG_SIBYTE_SB1250) || defined(CONFIG_SIBYTE_BCM112X)
	sb1250_setup();
#else
#error invalid SiByte board configuration
#endif

	mips_set_be_handler(swarm_be_handler);

	if (xicor_probe())
		swarm_rtc_type = RTC_XICOR;
	if (m41t81_probe())
		swarm_rtc_type = RTC_M41T81;

#ifdef CONFIG_VT
	screen_info = (struct screen_info) {
		.orig_video_page	= 52,
		.orig_video_mode	= 3,
		.orig_video_cols	= 80,
		.flags			= 12,
		.orig_video_ega_bx	= 3,
		.orig_video_lines	= 25,
		.orig_video_isVGA	= 0x22,
		.orig_video_points	= 16,
       };
       /* XXXKW for CFE, get lines/cols from environment */
#endif
}

#ifdef LEDS_PHYS

#ifdef CONFIG_SIBYTE_CARMEL
/* XXXKW need to detect Monterey/LittleSur/etc */
#undef LEDS_PHYS
#define LEDS_PHYS MLEDS_PHYS
#endif

void setleds(char *str)
{
	void *reg;
	int i;

	for (i = 0; i < 4; i++) {
		reg = IOADDR(LEDS_PHYS) + 0x20 + ((3 - i) << 3);

		if (!str[i])
			writeb(' ', reg);
		else
			writeb(str[i], reg);
	}
}

#endif /* LEDS_PHYS */
