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

#include <asm/io.h>

static inline void fd_cacheflush(char * addr, long size)
{
	dma_cache_wback_inv((unsigned long)addr, size);
}

#define MAX_BUFFER_SECTORS 24


/*
 * And on Mips's the CMOS info fails also ...
 *
 * FIXME: This information should come from the ARC configuration tree
 *	  or wherever a particular machine has stored this ...
 */
#define FLOPPY0_TYPE		fd_drive_type(0)
#define FLOPPY1_TYPE		fd_drive_type(1)

#define FDC1			fd_getfdaddr1()

#define N_FDC 1			/* do you *really* want a second controller? */
#define N_DRIVE 8

#define EXTRA_FLOPPY_PARAMS

#include <floppy.h>

#endif /* _ASM_FLOPPY_H */
