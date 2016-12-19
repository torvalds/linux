/*
 *  arch/arm/include/asm/floppy.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Note that we don't touch FLOPPY_DMA nor FLOPPY_IRQ here
 */
#ifndef __ASM_ARM_FLOPPY_H
#define __ASM_ARM_FLOPPY_H
#if 0
#include <mach/floppy.h>
#endif

#define fd_outb(val,port)			\
	do {					\
		if ((port) == (u32)FD_DOR)	\
			fd_setdor((val));	\
		else				\
			outb((val),(port));	\
	} while(0)

#define fd_inb(port)		inb((port))
#define fd_request_irq()	request_irq(IRQ_FLOPPYDISK,floppy_interrupt,\
					    0,"floppy",NULL)
#define fd_free_irq()		free_irq(IRQ_FLOPPYDISK,NULL)
#define fd_disable_irq()	disable_irq(IRQ_FLOPPYDISK)
#define fd_enable_irq()		enable_irq(IRQ_FLOPPYDISK)

static inline int fd_dma_setup(void *data, unsigned int length,
			       unsigned int mode, unsigned long addr)
{
	set_dma_mode(DMA_FLOPPY, mode);
	__set_dma_addr(DMA_FLOPPY, data);
	set_dma_count(DMA_FLOPPY, length);
	virtual_dma_port = addr;
	enable_dma(DMA_FLOPPY);
	return 0;
}
#define fd_dma_setup		fd_dma_setup

#define fd_request_dma()	request_dma(DMA_FLOPPY,"floppy")
#define fd_free_dma()		free_dma(DMA_FLOPPY)
#define fd_disable_dma()	disable_dma(DMA_FLOPPY)

/* need to clean up dma.h */
#define DMA_FLOPPYDISK		DMA_FLOPPY

/* Floppy_selects is the list of DOR's to select drive fd
 *
 * On initialisation, the floppy list is scanned, and the drives allocated
 * in the order that they are found.  This is done by seeking the drive
 * to a non-zero track, and then restoring it to track 0.  If an error occurs,
 * then there is no floppy drive present.       [to be put back in again]
 */
static unsigned char floppy_selects[2][4] =
{
	{ 0x10, 0x21, 0x23, 0x33 },
	{ 0x10, 0x21, 0x23, 0x33 }
};

#define fd_setdor(dor)								\
do {										\
	int new_dor = (dor);							\
	if (new_dor & 0xf0)							\
		new_dor = (new_dor & 0x0c) | floppy_selects[fdc][new_dor & 3];	\
	else									\
		new_dor &= 0x0c;						\
	outb(new_dor, FD_DOR);							\
} while (0)

/*
 * Someday, we'll automatically detect which drives are present...
 */
static inline void fd_scandrives (void)
{
#if 0
	int floppy, drive_count;

	fd_disable_irq();
	raw_cmd = &default_raw_cmd;
	raw_cmd->flags = FD_RAW_SPIN | FD_RAW_NEED_SEEK;
	raw_cmd->track = 0;
	raw_cmd->rate = ?;
	drive_count = 0;
	for (floppy = 0; floppy < 4; floppy ++) {
		current_drive = drive_count;
		/*
		 * Turn on floppy motor
		 */
		if (start_motor(redo_fd_request))
			continue;
		/*
		 * Set up FDC
		 */
		fdc_specify();
		/*
		 * Tell FDC to recalibrate
		 */
		output_byte(FD_RECALIBRATE);
		LAST_OUT(UNIT(floppy));
		/* wait for command to complete */
		if (!successful) {
			int i;
			for (i = drive_count; i < 3; i--)
				floppy_selects[fdc][i] = floppy_selects[fdc][i + 1];
			floppy_selects[fdc][3] = 0;
			floppy -= 1;
		} else
			drive_count++;
	}
#else
	floppy_selects[0][0] = 0x10;
	floppy_selects[0][1] = 0x21;
	floppy_selects[0][2] = 0x23;
	floppy_selects[0][3] = 0x33;
#endif
}

#define FDC1 (0x3f0)

#define FLOPPY0_TYPE 4
#define FLOPPY1_TYPE 4

#define N_FDC 1
#define N_DRIVE 4

#define CROSS_64KB(a,s) (0)

/*
 * This allows people to reverse the order of
 * fd0 and fd1, in case their hardware is
 * strangely connected (as some RiscPCs
 * and A5000s seem to be).
 */
static void driveswap(int *ints, int dummy, int dummy2)
{
	floppy_selects[0][0] ^= floppy_selects[0][1];
	floppy_selects[0][1] ^= floppy_selects[0][0];
	floppy_selects[0][0] ^= floppy_selects[0][1];
}

#define EXTRA_FLOPPY_PARAMS ,{ "driveswap", &driveswap, NULL, 0, 0 }
	
#endif
