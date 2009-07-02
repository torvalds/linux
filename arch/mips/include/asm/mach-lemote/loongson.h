/*
 * Copyright (C) 2009 Lemote, Inc. & Institute of Computing Technology
 * Author: Wu Zhangjin <wuzj@lemote.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __ASM_MACH_LOONGSON_LOONGSON_H
#define __ASM_MACH_LOONGSON_LOONGSON_H

#include <linux/io.h>
#include <linux/init.h>

/* there is an internal bonito64-compatiable northbridge in loongson2e/2f */
#include <asm/mips-boards/bonito64.h>

/* loongson internal northbridge initialization */
extern void bonito_irq_init(void);

/* loongson-based machines specific reboot setup */
extern void mips_reboot_setup(void);

/* environment arguments from bootloader */
extern unsigned long bus_clock, cpu_clock_freq;
extern unsigned long memsize, highmemsize;

/* loongson-specific command line, env and memory initialization */
extern void __init prom_init_memory(void);
extern void __init prom_init_cmdline(void);
extern void __init prom_init_env(void);

#endif /* __ASM_MACH_LOONGSON_LOONGSON_H */
