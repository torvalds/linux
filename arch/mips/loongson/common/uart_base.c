/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzj@lemote.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <asm/bootinfo.h>

#include <loongson.h>

unsigned long __maybe_unused _loongson_uart_base;
EXPORT_SYMBOL(_loongson_uart_base);

unsigned long __maybe_unused uart8250_base[] = {
	[MACH_LOONGSON_UNKNOWN]	0,
	[MACH_LEMOTE_FL2E]	(LOONGSON_PCIIO_BASE + 0x3f8),
	[MACH_LEMOTE_FL2F]	(LOONGSON_PCIIO_BASE + 0x2f8),
	[MACH_LEMOTE_ML2F7]	(LOONGSON_LIO1_BASE + 0x3f8),
	[MACH_LEMOTE_YL2F89]	(LOONGSON_LIO1_BASE + 0x3f8),
	[MACH_DEXXON_GDIUM2F10]	(LOONGSON_LIO1_BASE + 0x3f8),
	[MACH_LOONGSON_END]	0,
};
EXPORT_SYMBOL(uart8250_base);

void __maybe_unused prom_init_uart_base(void)
{
	_loongson_uart_base =
		(unsigned long)ioremap_nocache(uart8250_base[mips_machtype], 8);
}
