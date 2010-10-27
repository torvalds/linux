/*
 * iodefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * System-wide channel objects and constants.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef IODEFS_
#define IODEFS_

#define IO_MAXIRQ   0xff	/* Arbitrarily large number. */

/* IO Objects: */
struct io_mgr;

/* IO manager attributes: */
struct io_attrs {
	u8 birq;		/* Channel's I/O IRQ number. */
	bool irq_shared;	/* TRUE if the IRQ is shareable. */
	u32 word_size;		/* DSP Word size. */
	u32 shm_base;		/* Physical base address of shared memory. */
	u32 usm_length;		/* Size (in bytes) of shared memory. */
};

#endif /* IODEFS_ */
