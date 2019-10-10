/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/mach/memory.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __MACH_PUV3_MEMORY_H__
#define __MACH_PUV3_MEMORY_H__

#include <mach/hardware.h>

/* Physical DRAM offset. */
#define PHYS_OFFSET	UL(0x00000000)
/* The base address of exception vectors. */
#define VECTORS_BASE	UL(0xffff0000)
/* The base address of kuser area. */
#define KUSER_BASE	UL(0x80000000)

#ifdef __ASSEMBLY__
/* The byte offset of the kernel image in RAM from the start of RAM. */
#define KERNEL_IMAGE_START	0x00408000
#endif

#if !defined(__ASSEMBLY__) && defined(CONFIG_PCI)

void puv3_pci_adjust_zones(unsigned long *size, unsigned long *holes);

#define arch_adjust_zones(size, holes) \
	puv3_pci_adjust_zones(size, holes)

#endif

/*
 * PCI controller in PKUnity-3 masks highest 5-bit for upstream channel,
 * so we must limit the DMA allocation within 128M physical memory for
 * supporting PCI devices.
 */
#define PCI_DMA_THRESHOLD	(PHYS_OFFSET + SZ_128M - 1)

#define is_pcibus_device(dev)	(dev &&			\
				(strncmp(dev->bus->name, "pci", 3) == 0))

#define __virt_to_pcibus(x)     (__virt_to_phys((x) + PKUNITY_PCIAHB_BASE))
#define __pcibus_to_virt(x)     (__phys_to_virt(x) - PKUNITY_PCIAHB_BASE)

/* kuser area */
#define KUSER_VECPAGE_BASE	(KUSER_BASE + UL(0x3fff0000))
/* kuser_vecpage (0xbfff0000) is ro, and vectors page (0xffff0000) is rw */
#define kuser_vecpage_to_vectors(x)	((x) - (KUSER_VECPAGE_BASE)	\
					+ (VECTORS_BASE))

#endif
