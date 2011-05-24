/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2011 John Crispin <blogic@openwrt.org>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>

#include <asm/time.h>
#include <asm/irq.h>
#include <asm/div64.h>

#include <lantiq_soc.h>

/* cgu registers */
#define LTQ_CGU_SYS	0x0010

unsigned int ltq_get_io_region_clock(void)
{
	return CLOCK_133M;
}
EXPORT_SYMBOL(ltq_get_io_region_clock);

unsigned int ltq_get_fpi_bus_clock(int fpi)
{
	return CLOCK_133M;
}
EXPORT_SYMBOL(ltq_get_fpi_bus_clock);

unsigned int ltq_get_cpu_hz(void)
{
	if (ltq_cgu_r32(LTQ_CGU_SYS) & (1 << 5))
		return CLOCK_266M;
	else
		return CLOCK_133M;
}
EXPORT_SYMBOL(ltq_get_cpu_hz);

unsigned int ltq_get_fpi_hz(void)
{
	return CLOCK_133M;
}
EXPORT_SYMBOL(ltq_get_fpi_hz);
