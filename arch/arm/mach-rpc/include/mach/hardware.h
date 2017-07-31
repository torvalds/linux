/*
 *  arch/arm/mach-rpc/include/mach/hardware.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the hardware definitions of the RiscPC series machines.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <mach/memory.h>

/*
 * What hardware must be present
 */
#define HAS_IOMD
#define HAS_VIDC20

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define RAM_SIZE		0x10000000
#define RAM_START		0x10000000

#define EASI_SIZE		0x08000000	/* EASI I/O */
#define EASI_START		0x08000000
#define EASI_BASE		IOMEM(0xe5000000)

#define IO_START		0x03000000	/* I/O */
#define IO_SIZE			0x01000000
#define IO_BASE			IOMEM(0xe0000000)

#define SCREEN_START		0x02000000	/* VRAM */
#define SCREEN_END		0xdfc00000
#define SCREEN_BASE		0xdf800000

#define UNCACHEABLE_ADDR	(FLUSH_BASE + 0x10000)

/*
 * IO Addresses
 */
#define ECARD_EASI_BASE		(EASI_BASE)
#define VIDC_BASE		(IO_BASE + 0x00400000)
#define EXPMASK_BASE		(IO_BASE + 0x00360000)
#define ECARD_IOC4_BASE		(IO_BASE + 0x00270000)
#define ECARD_IOC_BASE		(IO_BASE + 0x00240000)
#define IOMD_BASE		(IO_BASE + 0x00200000)
#define IOC_BASE		(IO_BASE + 0x00200000)
#define ECARD_MEMC8_BASE	(IO_BASE + 0x0002b000)
#define FLOPPYDMA_BASE		(IO_BASE + 0x0002a000)
#define PCIO_BASE		(IO_BASE + 0x00010000)
#define ECARD_MEMC_BASE		(IO_BASE + 0x00000000)

#define vidc_writel(val)	__raw_writel(val, VIDC_BASE)

#define NETSLOT_BASE		0x0302b000
#define NETSLOT_SIZE		0x00001000

#define PODSLOT_IOC0_BASE	0x03240000
#define PODSLOT_IOC4_BASE	0x03270000
#define PODSLOT_IOC_SIZE	(1 << 14)
#define PODSLOT_MEMC_BASE	0x03000000
#define PODSLOT_MEMC_SIZE	(1 << 14)
#define PODSLOT_EASI_BASE	0x08000000
#define PODSLOT_EASI_SIZE	(1 << 24)

#define	EXPMASK_STATUS		(EXPMASK_BASE + 0x00)
#define EXPMASK_ENABLE		(EXPMASK_BASE + 0x04)

#endif
