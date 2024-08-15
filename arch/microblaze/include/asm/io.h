/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_IO_H
#define _ASM_MICROBLAZE_IO_H

#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/types.h>
#include <linux/mm.h>          /* Get struct page {...} */

#ifndef CONFIG_PCI
#define _IO_BASE	0
#define _ISA_MEM_BASE	0
#else
#define _IO_BASE	isa_io_base
#define _ISA_MEM_BASE	isa_mem_base
struct pci_dev;
extern void pci_iounmap(struct pci_dev *dev, void __iomem *);
#define pci_iounmap pci_iounmap

extern unsigned long isa_io_base;
extern resource_size_t isa_mem_base;
#endif

#define PCI_IOBASE	((void __iomem *)_IO_BASE)
#define IO_SPACE_LIMIT (0xFFFFFFFF)

extern void iounmap(volatile void __iomem *addr);

extern void __iomem *ioremap(phys_addr_t address, unsigned long size);

/* Big Endian */
#define out_be32(a, v) __raw_writel((v), (void __iomem __force *)(a))
#define out_be16(a, v) __raw_writew((v), (a))

#define in_be32(a) __raw_readl((const void __iomem __force *)(a))
#define in_be16(a) __raw_readw(a)

#define writel_be(v, a)	out_be32((__force unsigned *)a, v)
#define readl_be(a)	in_be32((__force unsigned *)a)

/* Little endian */
#define out_le32(a, v) __raw_writel(__cpu_to_le32(v), (a))
#define out_le16(a, v) __raw_writew(__cpu_to_le16(v), (a))

#define in_le32(a) __le32_to_cpu(__raw_readl(a))
#define in_le16(a) __le16_to_cpu(__raw_readw(a))

/* Byte ops */
#define out_8(a, v) __raw_writeb((v), (a))
#define in_8(a) __raw_readb(a)

#include <asm-generic/io.h>

#endif /* _ASM_MICROBLAZE_IO_H */
