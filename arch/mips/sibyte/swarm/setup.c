/*
 * Copyright (C) 2000, 2001, 2002, 2003, 2004 Broadcom Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * Setup code for the SWARM board
 */

#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
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
extern int xicor_set_time(unsigned long);
extern unsigned long xicor_get_time(void);

extern int m41t81_probe(void);
extern int m41t81_set_time(unsigned long);
extern unsigned long m41t81_get_time(void);

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
	return (is_fixup ? MIPS_BE_FIXUP : MIPS_BE_FATAL);
}

enum swarm_rtc_type {
	RTC_NONE,
	RTC_XICOR,
	RTC_M4LT81
};

enum swarm_rtc_type swarm_rtc_type;

unsigned long read_persistent_clock(void)
{
	switch (swarm_rtc_type) {
	case RTC_XICOR:
		return xicor_get_time();

	case RTC_M4LT81:
		return m41t81_get_time();

	case RTC_NONE:
	default:
		return mktime(2000, 1, 1, 0, 0, 0);
	}
}

int rtc_mips_set_time(unsigned long sec)
{
	switch (swarm_rtc_type) {
	case RTC_XICOR:
		return xicor_set_time(sec);

	case RTC_M4LT81:
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

	panic_timeout = 5;  /* For debug.  */

	board_be_handler = swarm_be_handler;

	if (xicor_probe())
		swarm_rtc_type = RTC_XICOR;
	if (m41t81_probe())
		swarm_rtc_type = RTC_M4LT81;

	printk("This kernel optimized for "
#ifdef CONFIG_SIMULATION
	       "simulation"
#else
	       "board"
#endif
	       " runs "
#ifdef CONFIG_SIBYTE_CFE
	       "with"
#else
	       "without"
#endif
	       " CFE\n");

#ifdef CONFIG_VT
	screen_info = (struct screen_info) {
		0, 0,           /* orig-x, orig-y */
		0,              /* unused */
		52,             /* orig_video_page */
		3,              /* orig_video_mode */
		80,             /* orig_video_cols */
		4626, 3, 9,     /* unused, ega_bx, unused */
		25,             /* orig_video_lines */
		0x22,           /* orig_video_isVGA */
		16              /* orig_video_points */
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
