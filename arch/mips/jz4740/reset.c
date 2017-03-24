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

#include <linux/clk.h>
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

void jz4740_reset_init(void)
{
	_machine_restart = jz4740_restart;
	_machine_halt = jz4740_halt;
}
