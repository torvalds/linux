/*
 *  linux/arch/arm/mach-rpc/dma.c
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA functions specific to RiscPC architecture
 */
#include <linux/slab.h>
#include <linux/mman.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/page.h>
#include <asm/dma.h>
#include <asm/fiq.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm/uaccess.h>

#include <asm/mach/dma.h>
#include <asm/hardware/iomd.h>

#if 0
typedef enum {
	dma_size_8	= 1,
	dma_size_16	= 2,
	dma_size_32	= 4,
	dma_size_128	= 16
} dma_size_t;
#endif

#define TRANSFER_SIZE	2

#define CURA	(0)
#define ENDA	(IOMD_IO0ENDA - IOMD_IO0CURA)
#define CURB	(IOMD_IO0CURB - IOMD_IO0CURA)
#define ENDB	(IOMD_IO0ENDB - IOMD_IO0CURA)
#define CR	(IOMD_IO0CR - IOMD_IO0CURA)
#define ST	(IOMD_IO0ST - IOMD_IO0CURA)

static void iomd_get_next_sg(struct scatterlist *sg, dma_t *dma)
{
	unsigned long end, offset, flags = 0;

	if (dma->sg) {
		sg->dma_address = dma->sg->dma_address;
		offset = sg->dma_address & ~PAGE_MASK;

		end = offset + dma->sg->length;

		if (end > PAGE_SIZE)
			end = PAGE_SIZE;

		if (offset + TRANSFER_SIZE >= end)
			flags |= DMA_END_L;

		sg->length = end - TRANSFER_SIZE;

		dma->sg->length -= end - offset;
		dma->sg->dma_address += end - offset;

		if (dma->sg->length == 0) {
			if (dma->sgcount > 1) {
				dma->sg++;
				dma->sgcount--;
			} else {
				dma->sg = NULL;
				flags |= DMA_END_S;
			}
		}
	} else {
		flags = DMA_END_S | DMA_END_L;
		sg->dma_address = 0;
		sg->length = 0;
	}

	sg->length |= flags;
}

static irqreturn_t iomd_dma_handle(int irq, void *dev_id)
{
	dma_t *dma = (dma_t *)dev_id;
	unsigned long base = dma->dma_base;

	do {
		unsigned int status;

		status = iomd_readb(base + ST);
		if (!(status & DMA_ST_INT))
			return IRQ_HANDLED;

		if ((dma->state ^ status) & DMA_ST_AB)
			iomd_get_next_sg(&dma->cur_sg, dma);

		switch (status & (DMA_ST_OFL | DMA_ST_AB)) {
		case DMA_ST_OFL:			/* OIA */
		case DMA_ST_AB:				/* .IB */
			iomd_writel(dma->cur_sg.dma_address, base + CURA);
			iomd_writel(dma->cur_sg.length, base + ENDA);
			dma->state = DMA_ST_AB;
			break;

		case DMA_ST_OFL | DMA_ST_AB:		/* OIB */
		case 0:					/* .IA */
			iomd_writel(dma->cur_sg.dma_address, base + CURB);
			iomd_writel(dma->cur_sg.length, base + ENDB);
			dma->state = 0;
			break;
		}

		if (status & DMA_ST_OFL &&
		    dma->cur_sg.length == (DMA_END_S|DMA_END_L))
			break;
	} while (1);

	dma->state = ~DMA_ST_AB;
	disable_irq(irq);

	return IRQ_HANDLED;
}

static int iomd_request_dma(unsigned int chan, dma_t *dma)
{
	return request_irq(dma->dma_irq, iomd_dma_handle,
			   IRQF_DISABLED, dma->device_id, dma);
}

static void iomd_free_dma(unsigned int chan, dma_t *dma)
{
	free_irq(dma->dma_irq, dma);
}

static void iomd_enable_dma(unsigned int chan, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned int ctrl = TRANSFER_SIZE | DMA_CR_E;

	if (dma->invalid) {
		dma->invalid = 0;

		/*
		 * Cope with ISA-style drivers which expect cache
		 * coherence.
		 */
		if (!dma->sg) {
			dma->sg = &dma->buf;
			dma->sgcount = 1;
			dma->buf.length = dma->count;
			dma->buf.dma_address = dma_map_single(NULL,
				dma->addr, dma->count,
				dma->dma_mode == DMA_MODE_READ ?
				DMA_FROM_DEVICE : DMA_TO_DEVICE);
		}

		iomd_writeb(DMA_CR_C, dma_base + CR);
		dma->state = DMA_ST_AB;
	}
		
	if (dma->dma_mode == DMA_MODE_READ)
		ctrl |= DMA_CR_D;

	iomd_writeb(ctrl, dma_base + CR);
	enable_irq(dma->dma_irq);
}

static void iomd_disable_dma(unsigned int chan, dma_t *dma)
{
	unsigned long dma_base = dma->dma_base;
	unsigned long flags;

	local_irq_save(flags);
	if (dma->state != ~DMA_ST_AB)
		disable_irq(dma->dma_irq);
	iomd_writeb(0, dma_base + CR);
	local_irq_restore(flags);
}

static int iomd_set_dma_speed(unsigned int chan, dma_t *dma, int cycle)
{
	int tcr, speed;

	if (cycle < 188)
		speed = 3;
	else if (cycle <= 250)
		speed = 2;
	else if (cycle < 438)
		speed = 1;
	else
		speed = 0;

	tcr = iomd_readb(IOMD_DMATCR);
	speed &= 3;

	switch (chan) {
	case DMA_0:
		tcr = (tcr & ~0x03) | speed;
		break;

	case DMA_1:
		tcr = (tcr & ~0x0c) | (speed << 2);
		break;

	case DMA_2:
		tcr = (tcr & ~0x30) | (speed << 4);
		break;

	case DMA_3:
		tcr = (tcr & ~0xc0) | (speed << 6);
		break;

	default:
		break;
	}

	iomd_writeb(tcr, IOMD_DMATCR);

	return speed;
}

static struct dma_ops iomd_dma_ops = {
	.type		= "IOMD",
	.request	= iomd_request_dma,
	.free		= iomd_free_dma,
	.enable		= iomd_enable_dma,
	.disable	= iomd_disable_dma,
	.setspeed	= iomd_set_dma_speed,
};

static struct fiq_handler fh = {
	.name	= "floppydma"
};

static void floppy_enable_dma(unsigned int chan, dma_t *dma)
{
	void *fiqhandler_start;
	unsigned int fiqhandler_length;
	struct pt_regs regs;

	if (dma->sg)
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

	regs.ARM_r9  = dma->count;
	regs.ARM_r10 = (unsigned long)dma->addr;
	regs.ARM_fp  = (unsigned long)FLOPPYDMA_BASE;

	if (claim_fiq(&fh)) {
		printk("floppydma: couldn't claim FIQ.\n");
		return;
	}

	set_fiq_handler(fiqhandler_start, fiqhandler_length);
	set_fiq_regs(&regs);
	enable_fiq(dma->dma_irq);
}

static void floppy_disable_dma(unsigned int chan, dma_t *dma)
{
	disable_fiq(dma->dma_irq);
	release_fiq(&fh);
}

static int floppy_get_residue(unsigned int chan, dma_t *dma)
{
	struct pt_regs regs;
	get_fiq_regs(&regs);
	return regs.ARM_r9;
}

static struct dma_ops floppy_dma_ops = {
	.type		= "FIQDMA",
	.enable		= floppy_enable_dma,
	.disable	= floppy_disable_dma,
	.residue	= floppy_get_residue,
};

/*
 * This is virtual DMA - we don't need anything here.
 */
static void sound_enable_disable_dma(unsigned int chan, dma_t *dma)
{
}

static struct dma_ops sound_dma_ops = {
	.type		= "VIRTUAL",
	.enable		= sound_enable_disable_dma,
	.disable	= sound_enable_disable_dma,
};

static dma_t iomd_dma[6];

static dma_t floppy_dma = {
	.dma_irq	= FIQ_FLOPPYDATA,
	.d_ops		= &floppy_dma_ops,
};

static dma_t sound_dma = {
	.d_ops		= &sound_dma_ops,
};

static int __init rpc_dma_init(void)
{
	unsigned int i;
	int ret;

	iomd_writeb(0, IOMD_IO0CR);
	iomd_writeb(0, IOMD_IO1CR);
	iomd_writeb(0, IOMD_IO2CR);
	iomd_writeb(0, IOMD_IO3CR);

	iomd_writeb(0xa0, IOMD_DMATCR);

	/*
	 * Setup DMA channels 2,3 to be for podules
	 * and channels 0,1 for internal devices
	 */
	iomd_writeb(DMA_EXT_IO3|DMA_EXT_IO2, IOMD_DMAEXT);

	iomd_dma[DMA_0].dma_base	= IOMD_IO0CURA;
	iomd_dma[DMA_0].dma_irq		= IRQ_DMA0;
	iomd_dma[DMA_1].dma_base	= IOMD_IO1CURA;
	iomd_dma[DMA_1].dma_irq		= IRQ_DMA1;
	iomd_dma[DMA_2].dma_base	= IOMD_IO2CURA;
	iomd_dma[DMA_2].dma_irq		= IRQ_DMA2;
	iomd_dma[DMA_3].dma_base	= IOMD_IO3CURA;
	iomd_dma[DMA_3].dma_irq		= IRQ_DMA3;
	iomd_dma[DMA_S0].dma_base	= IOMD_SD0CURA;
	iomd_dma[DMA_S0].dma_irq	= IRQ_DMAS0;
	iomd_dma[DMA_S1].dma_base	= IOMD_SD1CURA;
	iomd_dma[DMA_S1].dma_irq	= IRQ_DMAS1;

	for (i = DMA_0; i <= DMA_S1; i++) {
		iomd_dma[i].d_ops = &iomd_dma_ops;

		ret = isa_dma_add(i, &iomd_dma[i]);
		if (ret)
			printk("IOMDDMA%u: unable to register: %d\n", i, ret);
	}

	ret = isa_dma_add(DMA_VIRTUAL_FLOPPY, &floppy_dma);
	if (ret)
		printk("IOMDFLOPPY: unable to register: %d\n", ret);
	ret = isa_dma_add(DMA_VIRTUAL_SOUND, &sound_dma);
	if (ret)
		printk("IOMDSOUND: unable to register: %d\n", ret);
	return 0;
}
core_initcall(rpc_dma_init);
