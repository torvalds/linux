/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm.h>

#include <asm/reboot.h>

#include <asm/mach-jz4740/base.h>
#include <asm/mach-jz4740/timer.h>

#include "reset.h"
#include "clock.h"

static void jz4740_halt(void)
{
	while (1) {
		__asm__(".set push;\n"
			".set mips3;\n"
			"wait;\n"
			".set pop;\n"
		);
	}
}

#define JZ_REG_WDT_DATA 0x00
#define JZ_REG_WDT_COUNTER_ENABLE 0x04
#define JZ_REG_WDT_COUNTER 0x08
#define JZ_REG_WDT_CTRL 0x0c

static void jz4740_restart(char *command)
{
	void __iomem *wdt_base = ioremap(JZ4740_WDT_BASE_ADDR, 0x0f);

	jz4740_timer_enable_watchdog();

	writeb(0, wdt_base + JZ_REG_WDT_COUNTER_ENABLE);

	writew(0, wdt_base + JZ_REG_WDT_COUNTER);
	writew(0, wdt_base + JZ_REG_WDT_DATA);
	writew(BIT(2), wdt_base + JZ_REG_WDT_CTRL);

	writeb(1, wdt_base + JZ_REG_WDT_COUNTER_ENABLE);
	jz4740_halt();
}

#define JZ_REG_RTC_CTRL			0x00
#define JZ_REG_RTC_HIBERNATE		0x20
#define JZ_REG_RTC_WAKEUP_FILTER	0x24
#define JZ_REG_RTC_RESET_COUNTER	0x28

#define JZ_RTC_CTRL_WRDY		BIT(7)
#define JZ_RTC_WAKEUP_FILTER_MASK	0x0000FFE0
#define JZ_RTC_RESET_COUNTER_MASK	0x00000FE0

static inline void jz4740_rtc_wait_ready(void __iomem *rtc_base)
{
	uint32_t ctrl;

	do {
		ctrl = readl(rtc_base + JZ_REG_RTC_CTRL);
	} while (!(ctrl & JZ_RTC_CTRL_WRDY));
}

static void jz4740_power_off(void)
{
	void __iomem *rtc_base = ioremap(JZ4740_RTC_BASE_ADDR, 0x38);
	unsigned long wakeup_filter_ticks;
	unsigned long reset_counter_ticks;

	/*
	 * Set minimum wakeup pin assertion time: 100 ms.
	 * Range is 0 to 2 sec if RTC is clocked at 32 kHz.
	 */
	wakeup_filter_ticks = (100 * jz4740_clock_bdata.rtc_rate) / 1000;
	if (wakeup_filter_ticks < JZ_RTC_WAKEUP_FILTER_MASK)
		wakeup_filter_ticks &= JZ_RTC_WAKEUP_FILTER_MASK;
	else
		wakeup_filter_ticks = JZ_RTC_WAKEUP_FILTER_MASK;
	jz4740_rtc_wait_ready(rtc_base);
	writel(wakeup_filter_ticks, rtc_base + JZ_REG_RTC_WAKEUP_FILTER);

	/*
	 * Set reset pin low-level assertion time after wakeup: 60 ms.
	 * Range is 0 to 125 ms if RTC is clocked at 32 kHz.
	 */
	reset_counter_ticks = (60 * jz4740_clock_bdata.rtc_rate) / 1000;
	if (reset_counter_ticks < JZ_RTC_RESET_COUNTER_MASK)
		reset_counter_ticks &= JZ_RTC_RESET_COUNTER_MASK;
	else
		reset_counter_ticks = JZ_RTC_RESET_COUNTER_MASK;
	jz4740_rtc_wait_ready(rtc_base);
	writel(reset_counter_ticks, rtc_base + JZ_REG_RTC_RESET_COUNTER);

	jz4740_rtc_wait_ready(rtc_base);
	writel(1, rtc_base + JZ_REG_RTC_HIBERNATE);

	jz4740_halt();
}

void jz4740_reset_init(void)
{
	_machine_restart = jz4740_restart;
	_machine_halt = jz4740_halt;
	pm_power_off = jz4740_power_off;
}
