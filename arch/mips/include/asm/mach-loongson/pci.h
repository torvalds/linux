/*
 * Copyright (c) 2008 Zhang Le <r0bertz@gentoo.org>
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#ifndef __ASM_MACH_LOONGSON_PCI_H_
#define __ASM_MACH_LOONGSON_PCI_H_

extern struct pci_ops bonito64_pci_ops;

#ifdef CONFIG_LEMOTE_FULOONG2E

/* this pci memory space is mapped by pcimap in pci.c */
#define LOONGSON_PCI_MEM_START	BONITO_PCILO1_BASE
#define LOONGSON_PCI_MEM_END	(BONITO_PCILO1_BASE + 0x04000000 * 2)
/* this is an offset from mips_io_port_base */
#define LOONGSON_PCI_IO_START	0x00004000UL

#endif

#endif /* !__ASM_MACH_LOONGSON_PCI_H_ */
