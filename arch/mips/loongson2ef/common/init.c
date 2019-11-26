// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */

#include <linux/memblock.h>
#include <asm/bootinfo.h>
#include <asm/traps.h>
#include <asm/smp-ops.h>
#include <asm/cacheflush.h>
#include <asm/fw/fw.h>

#include <loongson.h>

/* Loongson CPU address windows config space base address */
unsigned long __maybe_unused _loongson_addrwincfg_base;

static void __init mips_nmi_setup(void)
{
	void *base;
	extern char except_vec_nmi;

	base = (void *)(CAC_BASE + 0x380);
	memcpy(base, &except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

void __init prom_init(void)
{
#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG
	_loongson_addrwincfg_base = (unsigned long)
		ioremap(LOONGSON_ADDRWINCFG_BASE, LOONGSON_ADDRWINCFG_SIZE);
#endif

	fw_init_cmdline();
	prom_init_machtype();
	prom_init_env();

	/* init base address of io space */
	set_io_port_base((unsigned long)
		ioremap(LOONGSON_PCIIO_BASE, LOONGSON_PCIIO_SIZE));
	prom_init_memory();

	/*init the uart base address */
	prom_init_uart_base();
	board_nmi_handler_setup = mips_nmi_setup;
}

void __init prom_free_prom_memory(void)
{
}
