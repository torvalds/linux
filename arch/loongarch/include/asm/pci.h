/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/types.h>
#include <asm/io.h>

#define PCIBIOS_MIN_IO		0x4000
#define PCIBIOS_MIN_MEM		0x20000000
#define PCIBIOS_MIN_CARDBUS_IO	0x4000

#define HAVE_PCI_MMAP
#define pcibios_assign_all_busses()     0

extern phys_addr_t mcfg_addr_init(int node);

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* _ASM_PCI_H */
