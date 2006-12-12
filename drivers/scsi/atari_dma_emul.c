/*
 * atari_dma_emul.c -- TT SCSI DMA emulator for the Hades.
 *
 * Copyright 1997 Wout Klaren <W.Klaren@inter.nl.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * This code was written using the Hades TOS source code as a
 * reference. This source code can be found on the home page
 * of Medusa Computer Systems.
 *
 * Version 0.1, 1997-09-24.
 * 
 * This code should be considered experimental. It has only been
 * tested on a Hades with a 68060. It might not work on a Hades
 * with a 68040. Make backups of your hard drives before using
 * this code.
 */

#include <linux/compiler.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>

#define hades_dma_ctrl		(*(unsigned char *) 0xffff8717)
#define hades_psdm_reg		(*(unsigned char *) 0xffff8741)

#define TRANSFER_SIZE		16

struct m68040_frame {
	unsigned long  effaddr;  /* effective address */
	unsigned short ssw;      /* special status word */
	unsigned short wb3s;     /* write back 3 status */
	unsigned short wb2s;     /* write back 2 status */
	unsigned short wb1s;     /* write back 1 status */
	unsigned long  faddr;    /* fault address */
	unsigned long  wb3a;     /* write back 3 address */
	unsigned long  wb3d;     /* write back 3 data */
	unsigned long  wb2a;     /* write back 2 address */
	unsigned long  wb2d;     /* write back 2 data */
	unsigned long  wb1a;     /* write back 1 address */
	unsigned long  wb1dpd0;  /* write back 1 data/push data 0*/
	unsigned long  pd1;      /* push data 1*/
	unsigned long  pd2;      /* push data 2*/
	unsigned long  pd3;      /* push data 3*/
};

static void writeback (unsigned short wbs, unsigned long wba,
		       unsigned long wbd, void *old_buserr)
{
	mm_segment_t fs = get_fs();
	static void *save_buserr;

	__asm__ __volatile__ ("movec.l	%%vbr,%%a0\n\t"
			      "move.l	%0,8(%%a0)\n\t"
			      :
			      : "r" (&&bus_error)
			      : "a0" );

	save_buserr = old_buserr;

	set_fs (MAKE_MM_SEG(wbs & WBTM_040));

	switch (wbs & WBSIZ_040) {
	    case BA_SIZE_BYTE:
		put_user (wbd & 0xff, (char *)wba);
		break;
	    case BA_SIZE_WORD:
		put_user (wbd & 0xffff, (short *)wba);
		break;
	    case BA_SIZE_LONG:
		put_user (wbd, (int *)wba);
		break;
	}

	set_fs (fs);
	return;

bus_error:
	__asm__ __volatile__ ("cmp.l	%0,2(%%sp)\n\t"
			      "bcs.s	.jump_old\n\t"
			      "cmp.l	%1,2(%%sp)\n\t"
			      "bls.s	.restore_old\n"
			".jump_old:\n\t"
			      "move.l	%2,-(%%sp)\n\t"
			      "rts\n"
			".restore_old:\n\t"
			      "move.l	%%a0,-(%%sp)\n\t"
			      "movec.l	%%vbr,%%a0\n\t"
			      "move.l	%2,8(%%a0)\n\t"
			      "move.l	(%%sp)+,%%a0\n\t"
			      "rte\n\t"
			      :
			      : "i" (writeback), "i" (&&bus_error),
			        "m" (save_buserr) );
}

/*
 * static inline void set_restdata_reg(unsigned char *cur_addr)
 *
 * Set the rest data register if necessary.
 */

static inline void set_restdata_reg(unsigned char *cur_addr)
{
	if (((long) cur_addr & ~3) != 0)
		tt_scsi_dma.dma_restdata =
			*((unsigned long *) ((long) cur_addr & ~3));
}

/*
 * void hades_dma_emulator(int irq, void *dummy)
 * 
 * This code emulates TT SCSI DMA on the Hades.
 * 
 * Note the following:
 * 
 * 1. When there is no byte available to read from the SCSI bus, or
 *    when a byte cannot yet bet written to the SCSI bus, a bus
 *    error occurs when reading or writing the pseudo DMA data
 *    register (hades_psdm_reg). We have to catch this bus error
 *    and try again to read or write the byte. If after several tries
 *    we still get a bus error, the interrupt handler is left. When
 *    the byte can be read or written, the interrupt handler is
 *    called again.
 * 
 * 2. The SCSI interrupt must be disabled in this interrupt handler.
 * 
 * 3. If we set the EOP signal, the SCSI controller still expects one
 *    byte to be read or written. Therefore the last byte is transferred
 *    separately, after setting the EOP signal.
 * 
 * 4. When this function is left, the address pointer (start_addr) is
 *    converted to a physical address. Because it points one byte
 *    further than the last transferred byte, it can point outside the
 *    current page. If virt_to_phys() is called with this address we
 *    might get an access error. Therefore virt_to_phys() is called with
 *    start_addr - 1 if the count has reached zero. The result is
 *    increased with one.
 */

static irqreturn_t hades_dma_emulator(int irq, void *dummy)
{
	unsigned long dma_base;
	register unsigned long dma_cnt asm ("d3");
	static long save_buserr;
	register unsigned long save_sp asm ("d4");
	register int tries asm ("d5");
	register unsigned char *start_addr asm ("a3"), *end_addr asm ("a4");
	register unsigned char *eff_addr;
	register unsigned char *psdm_reg;
	unsigned long rem;

	atari_disable_irq(IRQ_TT_MFP_SCSI);

	/*
	 * Read the dma address and count registers.
	 */

	dma_base = SCSI_DMA_READ_P(dma_addr);
	dma_cnt = SCSI_DMA_READ_P(dma_cnt);

	/*
	 * Check if DMA is still enabled.
	 */

	if ((tt_scsi_dma.dma_ctrl & 2) == 0)
	{
		atari_enable_irq(IRQ_TT_MFP_SCSI);
		return IRQ_HANDLED;
	}

	if (dma_cnt == 0)
	{
		printk(KERN_NOTICE "DMA emulation: count is zero.\n");
		tt_scsi_dma.dma_ctrl &= 0xfd;	/* DMA ready. */
		atari_enable_irq(IRQ_TT_MFP_SCSI);
		return IRQ_HANDLED;
	}

	/*
	 * Install new bus error routine.
	 */

	__asm__ __volatile__ ("movec.l	%%vbr,%%a0\n\t"
			      "move.l	8(%%a0),%0\n\t"
			      "move.l	%1,8(%%a0)\n\t"
			      : "=&r" (save_buserr)
			      : "r" (&&scsi_bus_error)
			      : "a0" );

	hades_dma_ctrl &= 0xfc;		/* Bus error and EOP off. */

	/*
	 * Save the stack pointer.
	 */

	__asm__ __volatile__ ("move.l	%%sp,%0\n\t"
			      : "=&r" (save_sp) );

	tries = 100;			/* Maximum number of bus errors. */
	start_addr = phys_to_virt(dma_base);
	end_addr = start_addr + dma_cnt;

scsi_loop:
	dma_cnt--;
	rem = dma_cnt & (TRANSFER_SIZE - 1);
	dma_cnt &= ~(TRANSFER_SIZE - 1);
	psdm_reg = &hades_psdm_reg;

	if (tt_scsi_dma.dma_ctrl & 1)	/* Read or write? */
	{
		/*
		 * SCSI write. Abort when count is zero.
		 */

		switch (rem)
		{
		case 0:
			while (dma_cnt > 0)
			{
				dma_cnt -= TRANSFER_SIZE;

				*psdm_reg = *start_addr++;
		case 15:
				*psdm_reg = *start_addr++;
		case 14:
				*psdm_reg = *start_addr++;
		case 13:
				*psdm_reg = *start_addr++;
		case 12:
				*psdm_reg = *start_addr++;
		case 11:
				*psdm_reg = *start_addr++;
		case 10:
				*psdm_reg = *start_addr++;
		case 9:
				*psdm_reg = *start_addr++;
		case 8:
				*psdm_reg = *start_addr++;
		case 7:
				*psdm_reg = *start_addr++;
		case 6:
				*psdm_reg = *start_addr++;
		case 5:
				*psdm_reg = *start_addr++;
		case 4:
				*psdm_reg = *start_addr++;
		case 3:
				*psdm_reg = *start_addr++;
		case 2:
				*psdm_reg = *start_addr++;
		case 1:
				*psdm_reg = *start_addr++;
			}
		}

		hades_dma_ctrl |= 1;	/* Set EOP. */
		udelay(10);
		*psdm_reg = *start_addr++;	/* Dummy byte. */
		tt_scsi_dma.dma_ctrl &= 0xfd;	/* DMA ready. */
	}
	else
	{
		/*
		 * SCSI read. Abort when count is zero.
		 */

		switch (rem)
		{
		case 0:
			while (dma_cnt > 0)
			{
				dma_cnt -= TRANSFER_SIZE;

				*start_addr++ = *psdm_reg;
		case 15:
				*start_addr++ = *psdm_reg;
		case 14:
				*start_addr++ = *psdm_reg;
		case 13:
				*start_addr++ = *psdm_reg;
		case 12:
				*start_addr++ = *psdm_reg;
		case 11:
				*start_addr++ = *psdm_reg;
		case 10:
				*start_addr++ = *psdm_reg;
		case 9:
				*start_addr++ = *psdm_reg;
		case 8:
				*start_addr++ = *psdm_reg;
		case 7:
				*start_addr++ = *psdm_reg;
		case 6:
				*start_addr++ = *psdm_reg;
		case 5:
				*start_addr++ = *psdm_reg;
		case 4:
				*start_addr++ = *psdm_reg;
		case 3:
				*start_addr++ = *psdm_reg;
		case 2:
				*start_addr++ = *psdm_reg;
		case 1:
				*start_addr++ = *psdm_reg;
			}
		}

		hades_dma_ctrl |= 1;	/* Set EOP. */
		udelay(10);
		*start_addr++ = *psdm_reg;
		tt_scsi_dma.dma_ctrl &= 0xfd;	/* DMA ready. */

		set_restdata_reg(start_addr);
	}

	if (start_addr != end_addr)
		printk(KERN_CRIT "DMA emulation: FATAL: Count is not zero at end of transfer.\n");

	dma_cnt = end_addr - start_addr;

scsi_end:
	dma_base = (dma_cnt == 0) ? virt_to_phys(start_addr - 1) + 1 :  
				    virt_to_phys(start_addr);

	SCSI_DMA_WRITE_P(dma_addr, dma_base);
	SCSI_DMA_WRITE_P(dma_cnt, dma_cnt);

	/*
	 * Restore old bus error routine.
	 */

	__asm__ __volatile__ ("movec.l	%%vbr,%%a0\n\t"
			      "move.l	%0,8(%%a0)\n\t"
			      :
			      : "r" (save_buserr)
			      : "a0" );

	atari_enable_irq(IRQ_TT_MFP_SCSI);

	return IRQ_HANDLED;

scsi_bus_error:
	/*
	 * First check if the bus error is caused by our code.
	 * If not, call the original handler.
	 */

	__asm__ __volatile__ ("cmp.l	%0,2(%%sp)\n\t"
			      "bcs.s	.old_vector\n\t"
			      "cmp.l	%1,2(%%sp)\n\t"
			      "bls.s	.scsi_buserr\n"
			".old_vector:\n\t"
			      "move.l	%2,-(%%sp)\n\t"
			      "rts\n"
			".scsi_buserr:\n\t"
			      :
			      : "i" (&&scsi_loop), "i" (&&scsi_end),
			        "m" (save_buserr) );

	if (CPU_IS_060)
	{
		/*
		 * Get effective address and restore the stack.
		 */

		__asm__ __volatile__ ("move.l	8(%%sp),%0\n\t"
				      "move.l	%1,%%sp\n\t"
				      : "=a&" (eff_addr)
				      : "r" (save_sp) );
	}
	else
	{
		register struct m68040_frame *frame;

		__asm__ __volatile__ ("lea	8(%%sp),%0\n\t"
				      : "=a&" (frame) );

		if (tt_scsi_dma.dma_ctrl & 1)
		{
			/*
			 * Bus error while writing.
			 */

			if (frame->wb3s & WBV_040)
			{
				if (frame->wb3a == (long) &hades_psdm_reg)
					start_addr--;
				else
					writeback(frame->wb3s, frame->wb3a,
						  frame->wb3d, &&scsi_bus_error);
			}

			if (frame->wb2s & WBV_040)
			{
				if (frame->wb2a == (long) &hades_psdm_reg)
					start_addr--;
				else
					writeback(frame->wb2s, frame->wb2a,
						  frame->wb2d, &&scsi_bus_error);
			}

			if (frame->wb1s & WBV_040)
			{
				if (frame->wb1a == (long) &hades_psdm_reg)
					start_addr--;
			}
		}
		else
		{
			/*
			 * Bus error while reading.
			 */

			if (frame->wb3s & WBV_040)
				writeback(frame->wb3s, frame->wb3a,
					  frame->wb3d, &&scsi_bus_error);
		}

		eff_addr = (unsigned char *) frame->faddr;

		__asm__ __volatile__ ("move.l	%0,%%sp\n\t"
				      :
				      : "r" (save_sp) );
	}

	dma_cnt = end_addr - start_addr;

	if (eff_addr == &hades_psdm_reg)
	{
		/*
		 * Bus error occurred while reading the pseudo
		 * DMA register. Time out.
		 */

		tries--;

		if (tries <= 0)
		{
			if ((tt_scsi_dma.dma_ctrl & 1) == 0)	/* Read or write? */
				set_restdata_reg(start_addr);

			if (dma_cnt <= 1)
				printk(KERN_CRIT "DMA emulation: Fatal "
				       "error while %s the last byte.\n",
				       (tt_scsi_dma.dma_ctrl & 1)
				       ? "writing" : "reading");

			goto scsi_end;
		}
		else
			goto scsi_loop;
	}
	else
	{
		/*
		 * Bus error during pseudo DMA transfer.
		 * Terminate the DMA transfer.
		 */

		hades_dma_ctrl |= 3;	/* Set EOP and bus error. */
		if ((tt_scsi_dma.dma_ctrl & 1) == 0)	/* Read or write? */
			set_restdata_reg(start_addr);
		goto scsi_end;
	}
}
