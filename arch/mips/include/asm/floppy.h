/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000 Ralf Baechle
 */
#ifndef _ASM_FLOPPY_H
#define _ASM_FLOPPY_H

#include <linux/dma-mapping.h>

static inline void fd_cacheflush(char * addr, long size)
{
	dma_cache_sync(NULL, addr, size, DMA_BIDIRECTIONAL);
}

#define MAX_BUFFER_SECTORS 24


/*
 * And on Mips's the CMOS info fails also ...
 *
 * FIXME: This information should come from the ARC configuration tree
 *        or whereever a particular machine has stored this ...
 */
#define FLOPPY0_TYPE 		fd_drive_type(0)
#define FLOPPY1_TYPE		fd_drive_type(1)

#define FDC1			fd_getfdaddr1();

#define N_FDC 1			/* do you *really* want a second controller? */
#define N_DRIVE 8

/*
 * The DMA channel used by the floppy controller cannot access data at
 * addresses >= 16MB
 *
 * Went back to the 1MB limit, as some people had problems with the floppy
 * driver otherwise. It doesn't matter much for performance anyway, as most
 * floppy accesses go through the track buffer.
 *
 * On MIPSes using vdma, this actually means that *all* transfers go thru
 * the * track buffer since 0x1000000 is always smaller than KSEG0/1.
 * Actually this needs to be a bit more complicated since the so much different
 * hardware available with MIPS CPUs ...
 */
#define CROSS_64KB(a, s) ((unsigned long)(a)/K_64 != ((unsigned long)(a) + (s) - 1) / K_64)

#define EXTRA_FLOPPY_PARAMS

#include <floppy.h>

#endif /* _ASM_FLOPPY_H */
