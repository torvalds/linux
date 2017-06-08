/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 2003 by Ralf Baechle
 */
#ifndef __ASM_MACH_GENERIC_FLOPPY_H
#define __ASM_MACH_GENERIC_FLOPPY_H

#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/floppy.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

/*
 * How to access the FDC's registers.
 */
static inline unsigned char fd_inb(unsigned int port)
{
	return inb_p(port);
}

static inline void fd_outb(unsigned char value, unsigned int port)
{
	outb_p(value, port);
}

/*
 * How to access the floppy DMA functions.
 */
static inline void fd_enable_dma(void)
{
	enable_dma(FLOPPY_DMA);
}

static inline void fd_disable_dma(void)
{
	disable_dma(FLOPPY_DMA);
}

static inline int fd_request_dma(void)
{
	return request_dma(FLOPPY_DMA, "floppy");
}

static inline void fd_free_dma(void)
{
	free_dma(FLOPPY_DMA);
}

static inline void fd_clear_dma_ff(void)
{
	clear_dma_ff(FLOPPY_DMA);
}

static inline void fd_set_dma_mode(char mode)
{
	set_dma_mode(FLOPPY_DMA, mode);
}

static inline void fd_set_dma_addr(char *addr)
{
	set_dma_addr(FLOPPY_DMA, (unsigned long) addr);
}

static inline void fd_set_dma_count(unsigned int count)
{
	set_dma_count(FLOPPY_DMA, count);
}

static inline int fd_get_dma_residue(void)
{
	return get_dma_residue(FLOPPY_DMA);
}

static inline void fd_enable_irq(void)
{
	enable_irq(FLOPPY_IRQ);
}

static inline void fd_disable_irq(void)
{
	disable_irq(FLOPPY_IRQ);
}

static inline int fd_request_irq(void)
{
	return request_irq(FLOPPY_IRQ, floppy_interrupt,
			   0, "floppy", NULL);
}

static inline void fd_free_irq(void)
{
	free_irq(FLOPPY_IRQ, NULL);
}

#define fd_free_irq()		free_irq(FLOPPY_IRQ, NULL);


static inline unsigned long fd_getfdaddr1(void)
{
	return 0x3f0;
}

static inline unsigned long fd_dma_mem_alloc(unsigned long size)
{
	return __get_dma_pages(GFP_KERNEL, get_order(size));
}

static inline void fd_dma_mem_free(unsigned long addr, unsigned long size)
{
	free_pages(addr, get_order(size));
}

static inline unsigned long fd_drive_type(unsigned long n)
{
	if (n == 0)
		return 4;	/* 3,5", 1.44mb */

	return 0;
}

#endif /* __ASM_MACH_GENERIC_FLOPPY_H */
