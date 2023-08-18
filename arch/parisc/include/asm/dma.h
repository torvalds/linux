/* SPDX-License-Identifier: GPL-2.0 */
/* asm/dma.h: Defines for using and allocating dma channels.
 * Written by Hennus Bergman, 1992.
 * High DMA channel support & info by Hannu Savolainen
 * and John Boyd, Nov. 1992.
 * (c) Copyright 2000, Grant Grundler
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

#include <asm/io.h>		/* need byte IO */

#define dma_outb	outb
#define dma_inb		inb

extern unsigned long pcxl_dma_start;

/*
** DMA_CHUNK_SIZE is used by the SCSI mid-layer to break up
** (or rather not merge) DMAs into manageable chunks.
** On parisc, this is more of the software/tuning constraint
** rather than the HW. I/O MMU allocation algorithms can be
** faster with smaller sizes (to some degree).
*/
#define DMA_CHUNK_SIZE	(BITS_PER_LONG*PAGE_SIZE)

/* The maximum address that we can perform a DMA transfer to on this platform
** New dynamic DMA interfaces should obsolete this....
*/
#define MAX_DMA_ADDRESS (~0UL)

/*
** We don't have DMA channels... well V-class does but the
** Dynamic DMA Mapping interface will support them... right? :^)
** Note: this is not relevant right now for PA-RISC, but we cannot 
** leave this as undefined because some things (e.g. sound)
** won't compile :-(
*/
#define MAX_DMA_CHANNELS 8
#define DMA_MODE_READ	0x44	/* I/O to memory, no autoinit, increment, single mode */
#define DMA_MODE_WRITE	0x48	/* memory to I/O, no autoinit, increment, single mode */
#define DMA_MODE_CASCADE 0xC0	/* pass thru DREQ->HRQ, DACK<-HLDA only */

#define DMA_AUTOINIT	0x10

/* 8237 DMA controllers */
#define IO_DMA1_BASE	0x00	/* 8 bit slave DMA, channels 0..3 */
#define IO_DMA2_BASE	0xC0	/* 16 bit master DMA, ch 4(=slave input)..7 */

/* DMA controller registers */
#define DMA1_CMD_REG		0x08	/* command register (w) */
#define DMA1_STAT_REG		0x08	/* status register (r) */
#define DMA1_REQ_REG            0x09    /* request register (w) */
#define DMA1_MASK_REG		0x0A	/* single-channel mask (w) */
#define DMA1_MODE_REG		0x0B	/* mode register (w) */
#define DMA1_CLEAR_FF_REG	0x0C	/* clear pointer flip-flop (w) */
#define DMA1_TEMP_REG           0x0D    /* Temporary Register (r) */
#define DMA1_RESET_REG		0x0D	/* Master Clear (w) */
#define DMA1_CLR_MASK_REG       0x0E    /* Clear Mask */
#define DMA1_MASK_ALL_REG       0x0F    /* all-channels mask (w) */
#define DMA1_EXT_MODE_REG	(0x400 | DMA1_MODE_REG)

#define DMA2_CMD_REG		0xD0	/* command register (w) */
#define DMA2_STAT_REG		0xD0	/* status register (r) */
#define DMA2_REQ_REG            0xD2    /* request register (w) */
#define DMA2_MASK_REG		0xD4	/* single-channel mask (w) */
#define DMA2_MODE_REG		0xD6	/* mode register (w) */
#define DMA2_CLEAR_FF_REG	0xD8	/* clear pointer flip-flop (w) */
#define DMA2_TEMP_REG           0xDA    /* Temporary Register (r) */
#define DMA2_RESET_REG		0xDA	/* Master Clear (w) */
#define DMA2_CLR_MASK_REG       0xDC    /* Clear Mask */
#define DMA2_MASK_ALL_REG       0xDE    /* all-channels mask (w) */
#define DMA2_EXT_MODE_REG	(0x400 | DMA2_MODE_REG)

static __inline__ unsigned long claim_dma_lock(void)
{
	return 0;
}

static __inline__ void release_dma_lock(unsigned long flags)
{
}


/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 *
 * Assumes DMA flip-flop is clear.
 */
static __inline__ int get_dma_residue(unsigned int dmanr)
{
	unsigned int io_port = (dmanr<=3)? ((dmanr&3)<<1) + 1 + IO_DMA1_BASE
					 : ((dmanr&3)<<2) + 2 + IO_DMA2_BASE;

	/* using short to get 16-bit wrap around */
	unsigned short count;

	count = 1 + dma_inb(io_port);
	count += dma_inb(io_port) << 8;
	
	return (dmanr<=3)? count : (count<<1);
}

/* enable/disable a specific DMA channel */
static __inline__ void enable_dma(unsigned int dmanr)
{
#ifdef CONFIG_SUPERIO
	if (dmanr<=3)
		dma_outb(dmanr,  DMA1_MASK_REG);
	else
		dma_outb(dmanr & 3,  DMA2_MASK_REG);
#endif
}

static __inline__ void disable_dma(unsigned int dmanr)
{
#ifdef CONFIG_SUPERIO
	if (dmanr<=3)
		dma_outb(dmanr | 4,  DMA1_MASK_REG);
	else
		dma_outb((dmanr & 3) | 4,  DMA2_MASK_REG);
#endif
}

/* reserve a DMA channel */
#define request_dma(dmanr, device_id)	(0)

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while holding the DMA lock ! ---
 */
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
}

/* set mode (above) for a specific DMA channel */
static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{
}

/* Set only the page register bits of the transfer address.
 * This is used for successive transfers when we know the contents of
 * the lower 16 bits of the DMA current address register, but a 64k boundary
 * may have been crossed.
 */
static __inline__ void set_dma_page(unsigned int dmanr, char pagenr)
{
}


/* Set transfer address & page bits for specific DMA channel.
 * Assumes dma flipflop is clear.
 */
static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int a)
{
}


/* Set transfer size (max 64k for DMA1..3, 128k for DMA5..7) for
 * a specific DMA channel.
 * You must ensure the parameters are valid.
 * NOTE: from a manual: "the number of transfers is one more
 * than the initial word count"! This is taken into account.
 * Assumes dma flip-flop is clear.
 * NOTE 2: "count" represents _bytes_ and must be even for channels 5-7.
 */
static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
}


#define free_dma(dmanr)

#endif /* _ASM_DMA_H */
