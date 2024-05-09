/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for Device tree / OpenFirmware handling on X86
 *
 * based on arch/powerpc/include/asm/prom.h which is
 *         Copyright (C) 1996-2005 Paul Mackerras.
 */

#ifndef _ASM_X86_PROM_H
#define _ASM_X86_PROM_H
#ifndef __ASSEMBLY__

#include <linux/of.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/irq.h>
#include <linux/atomic.h>
#include <asm/setup.h>

#ifdef CONFIG_OF
extern int of_ioapic;
extern u64 initial_dtb;
extern void add_dtb(u64 data);
void x86_of_pci_init(void);
void x86_dtb_init(void);
#else
static inline void add_dtb(u64 data) { }
static inline void x86_of_pci_init(void) { }
static inline void x86_dtb_init(void) { }
#define of_ioapic 0
#endif

#ifdef CONFIG_OF_EARLY_FLATTREE
void x86_flattree_get_config(void);
#else
static inline void x86_flattree_get_config(void) { }
#endif
extern char cmd_line[COMMAND_LINE_SIZE];

#endif /* __ASSEMBLY__ */
#endif
