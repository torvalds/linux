/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2008 Zhang Le <r0bertz@gentoo.org>
 * Copyright (c) 2009 Wu Zhangjin <wuzhangjin@gmail.com>
 */

#ifndef __ASM_MACH_LOONGSON64_PCI_H_
#define __ASM_MACH_LOONGSON64_PCI_H_

extern struct pci_ops loongson_pci_ops;

/* this is an offset from mips_io_port_base */
#define LOONGSON_PCI_IO_START	0x00004000UL

#define LOONGSON_PCI_MEM_START	0x40000000UL
#define LOONGSON_PCI_MEM_END	0x7effffffUL


#endif /* !__ASM_MACH_LOONGSON64_PCI_H_ */
