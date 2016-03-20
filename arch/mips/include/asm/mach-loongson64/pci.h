/*
 * Copyright (c) 2008 Zhang Le <r0bertz@gentoo.org>
 * Copyright (c) 2009 Wu Zhangjin <wuzhangjin@gmail.com>
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __ASM_MACH_LOONGSON64_PCI_H_
#define __ASM_MACH_LOONGSON64_PCI_H_

extern struct pci_ops loongson_pci_ops;

/* this is an offset from mips_io_port_base */
#define LOONGSON_PCI_IO_START	0x00004000UL

#ifdef CONFIG_CPU_SUPPORTS_ADDRWINCFG

/*
 * we use address window2 to map cpu address space to pci space
 * window2: cpu [1G, 2G] -> pci [1G, 2G]
 * why not use window 0 & 1? because they are used by cpu when booting.
 * window0: cpu [0, 256M] -> ddr [0, 256M]
 * window1: cpu [256M, 512M] -> pci [256M, 512M]
 */

/* the smallest LOONGSON_CPU_MEM_SRC can be 512M */
#define LOONGSON_CPU_MEM_SRC	0x40000000ul		/* 1G */
#define LOONGSON_PCI_MEM_DST	LOONGSON_CPU_MEM_SRC

#define LOONGSON_PCI_MEM_START	LOONGSON_PCI_MEM_DST
#define LOONGSON_PCI_MEM_END	(0x80000000ul-1)	/* 2G */

#define MMAP_CPUTOPCI_SIZE	(LOONGSON_PCI_MEM_END - \
					LOONGSON_PCI_MEM_START + 1)

#else	/* loongson2f/32bit & loongson2e */

/* this pci memory space is mapped by pcimap in pci.c */
#ifdef CONFIG_CPU_LOONGSON3
#define LOONGSON_PCI_MEM_START	0x40000000UL
#define LOONGSON_PCI_MEM_END	0x7effffffUL
#else
#define LOONGSON_PCI_MEM_START	LOONGSON_PCILO1_BASE
#define LOONGSON_PCI_MEM_END	(LOONGSON_PCILO1_BASE + 0x04000000 * 2)
#endif
/* this is an offset from mips_io_port_base */
#define LOONGSON_PCI_IO_START	0x00004000UL

#endif	/* !CONFIG_CPU_SUPPORTS_ADDRWINCFG */

#endif /* !__ASM_MACH_LOONGSON64_PCI_H_ */
