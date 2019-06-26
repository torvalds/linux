/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-ks8695/include/mach/hardware.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - Memory Map definitions
*/

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/sizes.h>

/*
 * Clocks are derived from MCLK, which is 25MHz
 */
#define KS8695_CLOCK_RATE	25000000

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
#define KS8695_IO_VA		IOMEM(0xF0000000)
#define KS8695_IO_SIZE		SZ_1M

#define KS8695_PCIMEM_PA	0x60000000
#define KS8695_PCIMEM_SIZE	SZ_512M

#define KS8695_PCIIO_PA		0x80000000
#define KS8695_PCIIO_SIZE	SZ_64K

#endif
