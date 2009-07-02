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

/* PCI Configuration Registers */
#define LOONGSON_PCI_ISR4C  BONITO_PCI_REG(0x4c)

/* PCI_Hit*_Sel_* */

#define LOONGSON_PCI_HIT0_SEL_L     BONITO(BONITO_REGBASE + 0x50)
#define LOONGSON_PCI_HIT0_SEL_H     BONITO(BONITO_REGBASE + 0x54)
#define LOONGSON_PCI_HIT1_SEL_L     BONITO(BONITO_REGBASE + 0x58)
#define LOONGSON_PCI_HIT1_SEL_H     BONITO(BONITO_REGBASE + 0x5c)
#define LOONGSON_PCI_HIT2_SEL_L     BONITO(BONITO_REGBASE + 0x60)
#define LOONGSON_PCI_HIT2_SEL_H     BONITO(BONITO_REGBASE + 0x64)

/* PXArb Config & Status */

#define LOONGSON_PXARB_CFG      BONITO(BONITO_REGBASE + 0x68)
#define LOONGSON_PXARB_STATUS       BONITO(BONITO_REGBASE + 0x6c)

#endif /* __ASM_MACH_LOONGSON_LOONGSON_H */
