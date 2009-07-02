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

#ifndef __ASM_MACH_LEMOTE_PCI_H_
#define __ASM_MACH_LEMOTE_PCI_H_

extern struct pci_ops bonito64_pci_ops;

#define LOONGSON2E_PCI_MEM_START	0x14000000UL
#define LOONGSON2E_PCI_MEM_END		0x1fffffffUL
#define LOONGSON2E_PCI_IO_START		0x00004000UL

#endif /* !__ASM_MACH_LEMOTE_PCI_H_ */
