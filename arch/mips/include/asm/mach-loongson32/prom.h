/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#ifndef __ASM_MACH_LOONGSON32_PROM_H
#define __ASM_MACH_LOONGSON32_PROM_H

#include <linux/io.h>
#include <linux/init.h>
#include <linux/irq.h>

/* environment arguments from bootloader */
extern unsigned long memsize, highmemsize;

/* loongson-specific command line, env and memory initialization */
extern char *prom_getenv(char *name);
extern void __init prom_init_cmdline(void);

#endif /* __ASM_MACH_LOONGSON32_PROM_H */
