/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/bootmem.h>
#include <asm/smp-ops.h>

#include <loongson.h>

/* Loongson CPU address windows config space base address */
unsigned long __maybe_unused _loongson_addrwincfg_base;

void __init prom_init(void)
{
#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG
	_loongson_addrwincfg_base = (unsigned long)
		ioremap(LOONGSON_ADDRWINCFG_BASE, LOONGSON_ADDRWINCFG_SIZE);
#endif

	prom_init_cmdline();
	prom_init_env();

	/* init base address of io space */
	set_io_port_base((unsigned long)
		ioremap(LOONGSON_PCIIO_BASE, LOONGSON_PCIIO_SIZE));

	prom_init_memory();

	/*init the uart base address */
	prom_init_uart_base();
	register_smp_ops(&loongson3_smp_ops);
}

void __init prom_free_prom_memory(void)
{
}
