/*
 * arch/arm/mach-ks8695/include/mach/hardware.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - Memory Map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

/*
 * Physical RAM address.
 */
#define KS8695_SDRAM_PA		0x00000000


/*
 * We map an entire MiB with the System Configuration Registers in even
 * though only 64KiB is needed. This makes it easier for use with the
 * head debug code as the initial MMU setup only deals in L1 sections.
 */
#define KS8695_IO_PA		0x03F00000
#define KS8695_IO_VA		0xF0000000
#define KS8695_IO_SIZE		SZ_1M

#define KS8695_PCIMEM_PA	0x60000000
#define KS8695_PCIMEM_SIZE	SZ_512M

#define KS8695_PCIIO_PA		0x80000000
#define KS8695_PCIIO_SIZE	SZ_64K


/*
 * PCI support
 */
#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0

#endif
