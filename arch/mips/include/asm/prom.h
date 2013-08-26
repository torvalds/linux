/*
 *  arch/mips/include/asm/prom.h
 *
 *  Copyright (C) 2010 Cisco Systems Inc. <dediao@cisco.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __ASM_PROM_H
#define __ASM_PROM_H

#ifdef CONFIG_OF
#include <linux/bug.h>
#include <linux/io.h>
#include <linux/types.h>
#include <asm/bootinfo.h>

extern void device_tree_init(void);

static inline unsigned long pci_address_to_pio(phys_addr_t address)
{
	/*
	 * The ioport address can be directly used by inX() / outX()
	 */
	BUG_ON(address > IO_SPACE_LIMIT);

	return (unsigned long) address;
}
#define pci_address_to_pio pci_address_to_pio

struct boot_param_header;

extern void __dt_setup_arch(struct boot_param_header *bph);

#define dt_setup_arch(sym)						\
({									\
	extern struct boot_param_header __dtb_##sym##_begin;		\
									\
	__dt_setup_arch(&__dtb_##sym##_begin);				\
})

#else /* CONFIG_OF */
static inline void device_tree_init(void) { }
#endif /* CONFIG_OF */

extern char *mips_get_machine_name(void);
extern void mips_set_machine_name(const char *name);

#endif /* __ASM_PROM_H */
