/*
 * SH7760 DMABRG IRQ handling
 *
 * (c) 2007 MSC Vertriebsges.m.b.H, Manuel Lauss <mlau@msc-ge.com>
 *  licensed under the GPLv2.
 *
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <asm/dma.h>
#include <asm/dmabrg.h>
#include <asm/io.h>

/*
 * The DMABRG is a special DMA unit within the SH7760. It does transfers
 * from USB-SRAM/Audio units to main memory (and also the LCDC; but that
 * part is sensibly placed  in the LCDC  registers and requires no irqs)
 * It has 3 IRQ lines which trigger 10 events, and works independently
 * from the traditional SH DMAC (although it blocks usage of DMAC 0)
 *
 * BRGIRQID   | component | dir | meaning      | source
 * -----------------------------------------------------
 *     0      | USB-DMA   | ... | xfer done    | DMABRGI1
 *     1      | USB-UAE   | ... | USB addr err.| DMABRGI0
 *     2      | HAC0/SSI0 | play| all done     | DMABRGI1
 *     3      | HAC0/SSI0 | play| half done    | DMABRGI2
 *     4      | HAC0/SSI0 | rec | all done     | DMABRGI1
 *     5      | HAC0/SSI0 | rec | half done    | DMABRGI2
 *     6      | HAC1/SSI1 | play| all done     | DMABRGI1
 *     7      | HAC1/SSI1 | play| half done    | DMABRGI2
 *     8      | HAC1/SSI1 | rec | all done     | DMABRGI1
 *     9      | HAC1/SSI1 | rec | half done    | DMABRGI2
 *
 * all can be enabled/disabled in the DMABRGCR register,
 * as well as checked if they occurred.
 *
 * DMABRGI0 services  USB  DMA  Address  errors,  but it still must be
 * enabled/acked in the DMABRGCR register.  USB-DMA complete indicator
 * is grouped together with the audio buffer end indicators, too bad...
 *
 * DMABRGCR:	Bits 31-24: audio-dma ENABLE flags,
 *		Bits 23-16: audio-dma STATUS flags,
 *		Bits  9-8:  USB error/xfer ENABLE,
 *		Bits  1-0:  USB error/xfer STATUS.
 *	Ack an IRQ by writing 0 to the STATUS flag.
 *	Mask IRQ by writing 0 to ENABLE flag.
 *
 * Usage is almost like with any other IRQ:
 *  dmabrg_request_irq(BRGIRQID, handler, data)
 *  dmabrg_free_irq(BRGIRQID)
 *
 * handler prototype:  void brgirqhandler(void *data)
 */

#define DMARSRA		0xfe090000
#define DMAOR		0xffa00040
#define DMACHCR0	0xffa0000c
#define DMABRGCR	0xfe3c0000

#define DMAOR_BRG	0x0000c000
#define DMAOR_DMEN	0x00000001

#define DMABRGI0	68
#define DMABRGI1	69
#define DMABRGI2	70

struct dmabrg_handler {
	void (*handler)(void *);
	void *data;
} *dmabrg_handlers;

static inline void dmabrg_call_handler(int i)
{
	dmabrg_handlers[i].handler(dmabrg_handlers[i].data);
}

/*
 * main DMABRG irq handler. It acks irqs and then
 * handles every set and unmasked bit sequentially.
 * No locking and no validity checks; it should be
 * as fast as possible (audio!)
 */
static irqreturn_t dmabrg_irq(int irq, void *data)
{
	unsigned long dcr;
	unsigned int i;

	dcr = __raw_readl(DMABRGCR);
	__raw_writel(dcr & ~0x00ff0003, DMABRGCR);	/* ack all */
	dcr &= dcr >> 8;	/* ignore masked */

	/* USB stuff, get it out of the way first */
	if (dcr & 1)
		dmabrg_call_handler(DMABRGIRQ_USBDMA);
	if (dcr & 2)
		dmabrg_call_handler(DMABRGIRQ_USBDMAERR);

	/* Audio */
	dcr >>= 16;
	while (dcr) {
		i = __ffs(dcr);
		dcr &= dcr - 1;
		dmabrg_call_handler(i + DMABRGIRQ_A0TXF);
	}
	return IRQ_HANDLED;
}

static void dmabrg_disable_irq(unsigned int dmairq)
{
	unsigned long dcr;
	dcr = __raw_readl(DMABRGCR);
	dcr &= ~(1 << ((dmairq > 1) ? dmairq + 22 : dmairq + 8));
	__raw_writel(dcr, DMABRGCR);
}

static void dmabrg_enable_irq(unsigned int dmairq)
{
	unsigned long dcr;
	dcr = __raw_readl(DMABRGCR);
	dcr |= (1 << ((dmairq > 1) ? dmairq + 22 : dmairq + 8));
	__raw_writel(dcr, DMABRGCR);
}

int dmabrg_request_irq(unsigned int dmairq, void(*handler)(void*),
		       void *data)
{
	if ((dmairq > 9) || !handler)
		return -ENOENT;
	if (dmabrg_handlers[dmairq].handler)
		return -EBUSY;

	dmabrg_handlers[dmairq].handler = handler;
	dmabrg_handlers[dmairq].data = data;
	
	dmabrg_enable_irq(dmairq);
	return 0;
}
EXPORT_SYMBOL_GPL(dmabrg_request_irq);

void dmabrg_free_irq(unsigned int dmairq)
{
	if (likely(dmairq < 10)) {
		dmabrg_disable_irq(dmairq);
		dmabrg_handlers[dmairq].handler = NULL;
		dmabrg_handlers[dmairq].data = NULL;
	}
}
EXPORT_SYMBOL_GPL(dmabrg_free_irq);

static int __init dmabrg_init(void)
{
	unsigned long or;
	int ret;

	dmabrg_handlers = kzalloc(10 * sizeof(struct dmabrg_handler),
				  GFP_KERNEL);
	if (!dmabrg_handlers)
		return -ENOMEM;

#ifdef CONFIG_SH_DMA
	/* request DMAC channel 0 before anyone else can get it */
	ret = request_dma(0, "DMAC 0 (DMABRG)");
	if (ret < 0)
		printk(KERN_INFO "DMABRG: DMAC ch0 not reserved!\n");
#endif

	__raw_writel(0, DMABRGCR);
	__raw_writel(0, DMACHCR0);
	__raw_writel(0x94000000, DMARSRA);	/* enable DMABRG in DMAC 0 */

	/* enable DMABRG mode, enable the DMAC */
	or = __raw_readl(DMAOR);
	__raw_writel(or | DMAOR_BRG | DMAOR_DMEN, DMAOR);

	ret = request_irq(DMABRGI0, dmabrg_irq, IRQF_DISABLED,
			"DMABRG USB address error", NULL);
	if (ret)
		goto out0;

	ret = request_irq(DMABRGI1, dmabrg_irq, IRQF_DISABLED,
			"DMABRG Transfer End", NULL);
	if (ret)
		goto out1;

	ret = request_irq(DMABRGI2, dmabrg_irq, IRQF_DISABLED,
			"DMABRG Transfer Half", NULL);
	if (ret == 0)
		return ret;

	free_irq(DMABRGI1, 0);
out1:	free_irq(DMABRGI0, 0);
out0:	kfree(dmabrg_handlers);
	return ret;
}
subsys_initcall(dmabrg_init);
