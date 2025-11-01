/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/floppy.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 *  Note that we don't touch FLOPPY_DMA nor FLOPPY_IRQ here
 */
#ifndef __ASM_ARM_FLOPPY_H
#define __ASM_ARM_FLOPPY_H

#define fd_outb(val, base, reg)						\
	do {								\
		int new_val = (val);					\
		if ((reg) == FD_DOR) {					\
			if (new_val & 0xf0)				\
				new_val = (new_val & 0x0c) |		\
					  floppy_selects[new_val & 3];	\
			else						\
				new_val &= 0x0c;			\
		}							\
		outb(new_val, (base) + (reg));				\
	} while(0)

#define fd_inb(base, reg)	inb((base) + (reg))
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
static unsigned char floppy_selects[4] = { 0x10, 0x21, 0x23, 0x33 };

#define FDC1 (0x3f0)

#define FLOPPY0_TYPE 4
#define FLOPPY1_TYPE 4

#define N_FDC 1
#define N_DRIVE 4

/*
 * This allows people to reverse the order of
 * fd0 and fd1, in case their hardware is
 * strangely connected (as some RiscPCs
 * and A5000s seem to be).
 */
static void driveswap(int *ints, int dummy, int dummy2)
{
	swap(floppy_selects[0], floppy_selects[1]);
}

#define EXTRA_FLOPPY_PARAMS ,{ "driveswap", &driveswap, NULL, 0, 0 }
	
#endif
