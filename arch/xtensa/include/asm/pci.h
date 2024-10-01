/*
 * linux/include/asm-xtensa/pci.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_PCI_H
#define _XTENSA_PCI_H

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader
 */

#define pcibios_assign_all_busses()	0

/* Assume some values. (We should revise them, if necessary) */

#define PCIBIOS_MIN_IO		0x2000
#define PCIBIOS_MIN_MEM		0x10000000

/* Dynamic DMA mapping stuff.
 * Xtensa has everything mapped statically like x86.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

/* The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce buffer
 * decisions.
 */

/* Tell PCI code what kind of PCI resource mappings we support */
#define HAVE_PCI_MMAP			1
#define ARCH_GENERIC_PCI_MMAP_RESOURCE	1
#define arch_can_pci_mmap_io()		1

#endif	/* _XTENSA_PCI_H */
