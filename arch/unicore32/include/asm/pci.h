/*
 * linux/arch/unicore32/include/asm/pci.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_PCI_H__
#define __UNICORE_PCI_H__

#ifdef __KERNEL__
#include <asm-generic/pci.h>
#include <mach/hardware.h> /* for PCIBIOS_MIN_* */

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

#endif /* __KERNEL__ */
#endif
