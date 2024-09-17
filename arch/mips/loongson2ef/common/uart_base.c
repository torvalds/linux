// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */

#include <linux/export.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>

#include <loongson.h>

/* raw */
unsigned long loongson_uart_base;
/* ioremapped */
unsigned long _loongson_uart_base;

EXPORT_SYMBOL(loongson_uart_base);
EXPORT_SYMBOL(_loongson_uart_base);

void prom_init_loongson_uart_base(void)
{
	switch (mips_machtype) {
	case MACH_LEMOTE_FL2E:
		loongson_uart_base = LOONGSON_PCIIO_BASE + 0x3f8;
		break;
	case MACH_LEMOTE_FL2F:
	case MACH_LEMOTE_LL2F:
		loongson_uart_base = LOONGSON_PCIIO_BASE + 0x2f8;
		break;
	case MACH_LEMOTE_ML2F7:
	case MACH_LEMOTE_YL2F89:
	case MACH_DEXXON_GDIUM2F10:
	case MACH_LEMOTE_NAS:
	default:
		/* The CPU provided serial port (LPC) */
		loongson_uart_base = LOONGSON_LIO1_BASE + 0x3f8;
		break;
	}

	_loongson_uart_base = TO_UNCAC(loongson_uart_base);
	setup_8250_early_printk_port(_loongson_uart_base, 0, 1024);
}
