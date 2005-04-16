/*
 * arch/sh64/kernel/dma.c
 *
 * DMA routines for the SH-5 DMAC.
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <asm/hardware.h>
#include <asm/dma.h>
#include <asm/signal.h>
#include <asm/errno.h>
#include <asm/io.h>

typedef struct {
	unsigned long dev_addr;
	unsigned long mem_addr;

	unsigned int mode;
	unsigned int count;
} dma_info_t;

static dma_info_t dma_info[MAX_DMA_CHANNELS];
static DEFINE_SPINLOCK(dma_spin_lock);

/* arch/sh64/kernel/irq_intc.c */
extern void make_intc_irq(unsigned int irq);

/* DMAC Interrupts */
#define DMA_IRQ_DMTE0	18
#define DMA_IRQ_DERR	22

#define DMAC_COMMON_BASE	(dmac_base + 0x08)
#define DMAC_SAR_BASE		(dmac_base + 0x10)
#define DMAC_DAR_BASE		(dmac_base + 0x18)
#define DMAC_COUNT_BASE		(dmac_base + 0x20)
#define DMAC_CTRL_BASE		(dmac_base + 0x28)
#define DMAC_STATUS_BASE	(dmac_base + 0x30)

#define DMAC_SAR(n)	(DMAC_SAR_BASE    + ((n) * 0x28))
#define DMAC_DAR(n)	(DMAC_DAR_BASE    + ((n) * 0x28))
#define DMAC_COUNT(n)	(DMAC_COUNT_BASE  + ((n) * 0x28))
#define DMAC_CTRL(n)	(DMAC_CTRL_BASE   + ((n) * 0x28))
#define DMAC_STATUS(n)	(DMAC_STATUS_BASE + ((n) * 0x28))

/* DMAC.COMMON Bit Definitions */
#define DMAC_COMMON_PR	0x00000001	/* Priority */
					/* Bits 1-2 Reserved */
#define DMAC_COMMON_ME	0x00000008	/* Master Enable */
#define DMAC_COMMON_NMI	0x00000010	/* NMI Flag */
					/* Bits 5-6 Reserved */
#define DMAC_COMMON_ER	0x00000780	/* Error Response */
#define DMAC_COMMON_AAE	0x00007800	/* Address Alignment Error */
					/* Bits 15-63 Reserved */

/* DMAC.SAR Bit Definitions */
#define DMAC_SAR_ADDR	0xffffffff	/* Source Address */

/* DMAC.DAR Bit Definitions */
#define DMAC_DAR_ADDR	0xffffffff	/* Destination Address */

/* DMAC.COUNT Bit Definitions */
#define DMAC_COUNT_CNT	0xffffffff	/* Transfer Count */

/* DMAC.CTRL Bit Definitions */
#define DMAC_CTRL_TS	0x00000007	/* Transfer Size */
#define DMAC_CTRL_SI	0x00000018	/* Source Increment */
#define DMAC_CTRL_DI	0x00000060	/* Destination Increment */
#define DMAC_CTRL_RS	0x00000780	/* Resource Select */
#define DMAC_CTRL_IE	0x00000800	/* Interrupt Enable */
#define DMAC_CTRL_TE	0x00001000	/* Transfer Enable */
					/* Bits 15-63 Reserved */

/* DMAC.STATUS Bit Definitions */
#define DMAC_STATUS_TE	0x00000001	/* Transfer End */
#define DMAC_STATUS_AAE	0x00000002	/* Address Alignment Error */
					/* Bits 2-63 Reserved */

static unsigned long dmac_base;

void set_dma_count(unsigned int chan, unsigned int count);
void set_dma_addr(unsigned int chan, unsigned int addr);

static irqreturn_t dma_mte(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int chan = irq - DMA_IRQ_DMTE0;
	dma_info_t *info = dma_info + chan;
	u64 status;

	if (info->mode & DMA_MODE_WRITE) {
		sh64_out64(info->mem_addr & DMAC_SAR_ADDR, DMAC_SAR(chan));
	} else {
		sh64_out64(info->mem_addr & DMAC_DAR_ADDR, DMAC_DAR(chan));
	}

	set_dma_count(chan, info->count);

	/* Clear the TE bit */
	status = sh64_in64(DMAC_STATUS(chan));
	status &= ~DMAC_STATUS_TE;
	sh64_out64(status, DMAC_STATUS(chan));

	return IRQ_HANDLED;
}

static struct irqaction irq_dmte = {
	.handler	= dma_mte,
	.flags		= SA_INTERRUPT,
	.name		= "DMA MTE",
};

static irqreturn_t dma_err(int irq, void *dev_id, struct pt_regs *regs)
{
	u64 tmp;
	u8 chan;

	printk(KERN_NOTICE "DMAC: Got a DMA Error!\n");

	tmp = sh64_in64(DMAC_COMMON_BASE);

	/* Check for the type of error */
	if ((chan = tmp & DMAC_COMMON_AAE)) {
		/* It's an address alignment error.. */
		printk(KERN_NOTICE "DMAC: Alignment error on channel %d, ", chan);

		printk(KERN_NOTICE "SAR: 0x%08llx, DAR: 0x%08llx, COUNT: %lld\n",
		       (sh64_in64(DMAC_SAR(chan)) & DMAC_SAR_ADDR),
		       (sh64_in64(DMAC_DAR(chan)) & DMAC_DAR_ADDR),
		       (sh64_in64(DMAC_COUNT(chan)) & DMAC_COUNT_CNT));

	} else if ((chan = tmp & DMAC_COMMON_ER)) {
		/* Something else went wrong.. */
		printk(KERN_NOTICE "DMAC: Error on channel %d\n", chan);
	}

	/* Reset the ME bit to clear the interrupt */
	tmp |= DMAC_COMMON_ME;
	sh64_out64(tmp, DMAC_COMMON_BASE);

	return IRQ_HANDLED;
}

static struct irqaction irq_derr = {
	.handler	= dma_err,
	.flags		= SA_INTERRUPT,
	.name		= "DMA Error",
};

static inline unsigned long calc_xmit_shift(unsigned int chan)
{
	return sh64_in64(DMAC_CTRL(chan)) & 0x03;
}

void setup_dma(unsigned int chan, dma_info_t *info)
{
	unsigned int irq = DMA_IRQ_DMTE0 + chan;
	dma_info_t *dma = dma_info + chan;

	make_intc_irq(irq);
	setup_irq(irq, &irq_dmte);
	dma = info;
}

void enable_dma(unsigned int chan)
{
	u64 ctrl;

	ctrl = sh64_in64(DMAC_CTRL(chan));
	ctrl |= DMAC_CTRL_TE;
	sh64_out64(ctrl, DMAC_CTRL(chan));
}

void disable_dma(unsigned int chan)
{
	u64 ctrl;

	ctrl = sh64_in64(DMAC_CTRL(chan));
	ctrl &= ~DMAC_CTRL_TE;
	sh64_out64(ctrl, DMAC_CTRL(chan));
}

void set_dma_mode(unsigned int chan, char mode)
{
	dma_info_t *info = dma_info + chan;

	info->mode = mode;

	set_dma_addr(chan, info->mem_addr);
	set_dma_count(chan, info->count);
}

void set_dma_addr(unsigned int chan, unsigned int addr)
{
	dma_info_t *info = dma_info + chan;
	unsigned long sar, dar;

	info->mem_addr = addr;
	sar = (info->mode & DMA_MODE_WRITE) ? info->mem_addr : info->dev_addr;
	dar = (info->mode & DMA_MODE_WRITE) ? info->dev_addr : info->mem_addr;

	sh64_out64(sar & DMAC_SAR_ADDR, DMAC_SAR(chan));
	sh64_out64(dar & DMAC_SAR_ADDR, DMAC_DAR(chan));
}

void set_dma_count(unsigned int chan, unsigned int count)
{
	dma_info_t *info = dma_info + chan;
	u64 tmp;

	info->count = count;

	tmp = (info->count >> calc_xmit_shift(chan)) & DMAC_COUNT_CNT;

	sh64_out64(tmp, DMAC_COUNT(chan));
}

unsigned long claim_dma_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dma_spin_lock, flags);

	return flags;
}

void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

int get_dma_residue(unsigned int chan)
{
	return sh64_in64(DMAC_COUNT(chan) << calc_xmit_shift(chan));
}

int __init init_dma(void)
{
	struct vcr_info vcr;
	u64 tmp;

	/* Remap the DMAC */
	dmac_base = onchip_remap(PHYS_DMAC_BLOCK, 1024, "DMAC");
	if (!dmac_base) {
		printk(KERN_ERR "Unable to remap DMAC\n");
		return -ENOMEM;
	}

	/* Report DMAC.VCR Info */
	vcr = sh64_get_vcr_info(dmac_base);
	printk("DMAC: Module ID: 0x%04x, Module version: 0x%04x\n",
	       vcr.mod_id, vcr.mod_vers);

	/* Set the ME bit */
	tmp = sh64_in64(DMAC_COMMON_BASE);
	tmp |= DMAC_COMMON_ME;
	sh64_out64(tmp, DMAC_COMMON_BASE);

	/* Enable the DMAC Error Interrupt */
	make_intc_irq(DMA_IRQ_DERR);
	setup_irq(DMA_IRQ_DERR, &irq_derr);

	return 0;
}

static void __exit exit_dma(void)
{
	onchip_unmap(dmac_base);
	free_irq(DMA_IRQ_DERR, 0);
}

module_init(init_dma);
module_exit(exit_dma);

MODULE_AUTHOR("Paul Mundt");
MODULE_DESCRIPTION("DMA API for SH-5 DMAC");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(setup_dma);
EXPORT_SYMBOL(claim_dma_lock);
EXPORT_SYMBOL(release_dma_lock);
EXPORT_SYMBOL(enable_dma);
EXPORT_SYMBOL(disable_dma);
EXPORT_SYMBOL(set_dma_mode);
EXPORT_SYMBOL(set_dma_addr);
EXPORT_SYMBOL(set_dma_count);
EXPORT_SYMBOL(get_dma_residue);

