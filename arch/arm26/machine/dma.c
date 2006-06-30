/*
 *  linux/arch/arm26/kernel/dma.c
 *
 *  Copyright (C) 1998-1999 Dave Gilbert / Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA functions specific to Archimedes and A5000 architecture
 */
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/fiq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

#define DPRINTK(x...) printk(KERN_DEBUG x)

#if defined(CONFIG_BLK_DEV_FD1772) || defined(CONFIG_BLK_DEV_FD1772_MODULE)

extern unsigned char fdc1772_dma_read, fdc1772_dma_read_end;
extern unsigned char fdc1772_dma_write, fdc1772_dma_write_end;
extern void fdc1772_setupdma(unsigned int count,unsigned int addr);

static void arc_floppy_data_enable_dma(dmach_t channel, dma_t *dma)
{
	DPRINTK("arc_floppy_data_enable_dma\n");

	if (dma->using_sg)
		BUG();

	switch (dma->dma_mode) {
	case DMA_MODE_READ: { /* read */
		unsigned long flags;
		DPRINTK("enable_dma fdc1772 data read\n");
		local_save_flags_cli(flags);
		clf();
			
		memcpy ((void *)0x1c, (void *)&fdc1772_dma_read,
			&fdc1772_dma_read_end - &fdc1772_dma_read);
		fdc1772_setupdma(dma->buf.length, dma->buf.__address); /* Sets data pointer up */
		enable_fiq(FIQ_FLOPPYDATA);
		local_irq_restore(flags);
	   }
	   break;

	case DMA_MODE_WRITE: { /* write */
		unsigned long flags;
		DPRINTK("enable_dma fdc1772 data write\n");
		local_save_flags_cli(flags);
		clf();
		memcpy ((void *)0x1c, (void *)&fdc1772_dma_write,
			&fdc1772_dma_write_end - &fdc1772_dma_write);
		fdc1772_setupdma(dma->buf.length, dma->buf.__address); /* Sets data pointer up */
		enable_fiq(FIQ_FLOPPYDATA);

		local_irq_restore(flags);
	    }
	    break;
	default:
		printk ("enable_dma: dma%d not initialised\n", channel);
	}
}

static int arc_floppy_data_get_dma_residue(dmach_t channel, dma_t *dma)
{
	extern unsigned int fdc1772_bytestogo;

	/* 10/1/1999 DAG - I presume its the number of bytes left? */
	return fdc1772_bytestogo;
}

static void arc_floppy_cmdend_enable_dma(dmach_t channel, dma_t *dma)
{
	/* Need to build a branch at the FIQ address */
	extern void fdc1772_comendhandler(void);
	unsigned long flags;

	DPRINTK("arc_floppy_cmdend_enable_dma\n");
	/*printk("enable_dma fdc1772 command end FIQ\n");*/
	save_flags(flags);
	clf();
	
	/* B fdc1772_comendhandler */
	*((unsigned int *)0x1c)=0xea000000 |
			(((unsigned int)fdc1772_comendhandler-(0x1c+8))/4);

	local_irq_restore(flags);
}

static int arc_floppy_cmdend_get_dma_residue(dmach_t channel, dma_t *dma)
{
	/* 10/1/1999 DAG - Presume whether there is an outstanding command? */
	extern unsigned int fdc1772_fdc_int_done;

	/* Explicit! If the int done is 0 then 1 int to go */
	return (fdc1772_fdc_int_done==0)?1:0;
}

static void arc_disable_dma(dmach_t channel, dma_t *dma)
{
	disable_fiq(dma->dma_irq);
}

static struct dma_ops arc_floppy_data_dma_ops = {
	.type		= "FIQDMA",
	.enable		= arc_floppy_data_enable_dma,
	.disable	= arc_disable_dma,
	.residue	= arc_floppy_data_get_dma_residue,
};

static struct dma_ops arc_floppy_cmdend_dma_ops = {
	.type		= "FIQCMD",
	.enable		= arc_floppy_cmdend_enable_dma,
	.disable	= arc_disable_dma,
	.residue	= arc_floppy_cmdend_get_dma_residue,
};
#endif

#ifdef CONFIG_ARCH_A5K
static struct fiq_handler fh = {
	.name	= "floppydata"
};

static int a5k_floppy_get_dma_residue(dmach_t channel, dma_t *dma)
{
	struct pt_regs regs;
	get_fiq_regs(&regs);
	return regs.ARM_r9;
}

static void a5k_floppy_enable_dma(dmach_t channel, dma_t *dma)
{
	struct pt_regs regs;
	void *fiqhandler_start;
	unsigned int fiqhandler_length;
	extern void floppy_fiqsetup(unsigned long len, unsigned long addr,
				     unsigned long port);

	if (dma->using_sg)
		BUG();

	if (dma->dma_mode == DMA_MODE_READ) {
		extern unsigned char floppy_fiqin_start, floppy_fiqin_end;
		fiqhandler_start = &floppy_fiqin_start;
		fiqhandler_length = &floppy_fiqin_end - &floppy_fiqin_start;
	} else {
		extern unsigned char floppy_fiqout_start, floppy_fiqout_end;
		fiqhandler_start = &floppy_fiqout_start;
		fiqhandler_length = &floppy_fiqout_end - &floppy_fiqout_start;
	}
	if (claim_fiq(&fh)) {
		printk("floppydma: couldn't claim FIQ.\n");
		return;
	}
	memcpy((void *)0x1c, fiqhandler_start, fiqhandler_length);
	regs.ARM_r9 = dma->buf.length;
	regs.ARM_r10 = (unsigned long)dma->buf.__address;
	regs.ARM_fp = FLOPPYDMA_BASE;
	set_fiq_regs(&regs);
	enable_fiq(dma->dma_irq);
}

static void a5k_floppy_disable_dma(dmach_t channel, dma_t *dma)
{
	disable_fiq(dma->dma_irq);
	release_fiq(&fh);
}

static struct dma_ops a5k_floppy_dma_ops = {
	.type		= "FIQDMA",
	.enable		= a5k_floppy_enable_dma,
	.disable	= a5k_floppy_disable_dma,
	.residue	= a5k_floppy_get_dma_residue,
};
#endif

/*
 * This is virtual DMA - we don't need anything here
 */
static void sound_enable_disable_dma(dmach_t channel, dma_t *dma)
{
}

static struct dma_ops sound_dma_ops = {
	.type		= "VIRTUAL",
	.enable		= sound_enable_disable_dma,
	.disable	= sound_enable_disable_dma,
};

void __init arch_dma_init(dma_t *dma)
{
#if defined(CONFIG_BLK_DEV_FD1772) || defined(CONFIG_BLK_DEV_FD1772_MODULE)
	if (machine_is_archimedes()) {
		dma[DMA_VIRTUAL_FLOPPY0].dma_irq = FIQ_FLOPPYDATA;
		dma[DMA_VIRTUAL_FLOPPY0].d_ops   = &arc_floppy_data_dma_ops;
		dma[DMA_VIRTUAL_FLOPPY1].dma_irq = 1;
		dma[DMA_VIRTUAL_FLOPPY1].d_ops   = &arc_floppy_cmdend_dma_ops;
	}
#endif
#ifdef CONFIG_ARCH_A5K
	if (machine_is_a5k()) {
		dma[DMA_VIRTUAL_FLOPPY0].dma_irq = FIQ_FLOPPYDATA;
		dma[DMA_VIRTUAL_FLOPPY0].d_ops   = &a5k_floppy_dma_ops;
	}
#endif
	dma[DMA_VIRTUAL_SOUND].d_ops = &sound_dma_ops;
}
