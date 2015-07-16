/* ebus.c: EBUS DMA library code.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1999  David S. Miller (davem@redhat.com)
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/ebus_dma.h>
#include <asm/io.h>

#define EBDMA_CSR	0x00UL	/* Control/Status */
#define EBDMA_ADDR	0x04UL	/* DMA Address */
#define EBDMA_COUNT	0x08UL	/* DMA Count */

#define EBDMA_CSR_INT_PEND	0x00000001
#define EBDMA_CSR_ERR_PEND	0x00000002
#define EBDMA_CSR_DRAIN		0x00000004
#define EBDMA_CSR_INT_EN	0x00000010
#define EBDMA_CSR_RESET		0x00000080
#define EBDMA_CSR_WRITE		0x00000100
#define EBDMA_CSR_EN_DMA	0x00000200
#define EBDMA_CSR_CYC_PEND	0x00000400
#define EBDMA_CSR_DIAG_RD_DONE	0x00000800
#define EBDMA_CSR_DIAG_WR_DONE	0x00001000
#define EBDMA_CSR_EN_CNT	0x00002000
#define EBDMA_CSR_TC		0x00004000
#define EBDMA_CSR_DIS_CSR_DRN	0x00010000
#define EBDMA_CSR_BURST_SZ_MASK	0x000c0000
#define EBDMA_CSR_BURST_SZ_1	0x00080000
#define EBDMA_CSR_BURST_SZ_4	0x00000000
#define EBDMA_CSR_BURST_SZ_8	0x00040000
#define EBDMA_CSR_BURST_SZ_16	0x000c0000
#define EBDMA_CSR_DIAG_EN	0x00100000
#define EBDMA_CSR_DIS_ERR_PEND	0x00400000
#define EBDMA_CSR_TCI_DIS	0x00800000
#define EBDMA_CSR_EN_NEXT	0x01000000
#define EBDMA_CSR_DMA_ON	0x02000000
#define EBDMA_CSR_A_LOADED	0x04000000
#define EBDMA_CSR_NA_LOADED	0x08000000
#define EBDMA_CSR_DEV_ID_MASK	0xf0000000

#define EBUS_DMA_RESET_TIMEOUT	10000

static void __ebus_dma_reset(struct ebus_dma_info *p, int no_drain)
{
	int i;
	u32 val = 0;

	writel(EBDMA_CSR_RESET, p->regs + EBDMA_CSR);
	udelay(1);

	if (no_drain)
		return;

	for (i = EBUS_DMA_RESET_TIMEOUT; i > 0; i--) {
		val = readl(p->regs + EBDMA_CSR);

		if (!(val & (EBDMA_CSR_DRAIN | EBDMA_CSR_CYC_PEND)))
			break;
		udelay(10);
	}
}

static irqreturn_t ebus_dma_irq(int irq, void *dev_id)
{
	struct ebus_dma_info *p = dev_id;
	unsigned long flags;
	u32 csr = 0;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	writel(csr, p->regs + EBDMA_CSR);
	spin_unlock_irqrestore(&p->lock, flags);

	if (csr & EBDMA_CSR_ERR_PEND) {
		printk(KERN_CRIT "ebus_dma(%s): DMA error!\n", p->name);
		p->callback(p, EBUS_DMA_EVENT_ERROR, p->client_cookie);
		return IRQ_HANDLED;
	} else if (csr & EBDMA_CSR_INT_PEND) {
		p->callback(p,
			    (csr & EBDMA_CSR_TC) ?
			    EBUS_DMA_EVENT_DMA : EBUS_DMA_EVENT_DEVICE,
			    p->client_cookie);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;

}

int ebus_dma_register(struct ebus_dma_info *p)
{
	u32 csr;

	if (!p->regs)
		return -EINVAL;
	if (p->flags & ~(EBUS_DMA_FLAG_USE_EBDMA_HANDLER |
			 EBUS_DMA_FLAG_TCI_DISABLE))
		return -EINVAL;
	if ((p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) && !p->callback)
		return -EINVAL;
	if (!strlen(p->name))
		return -EINVAL;

	__ebus_dma_reset(p, 1);

	csr = EBDMA_CSR_BURST_SZ_16 | EBDMA_CSR_EN_CNT;

	if (p->flags & EBUS_DMA_FLAG_TCI_DISABLE)
		csr |= EBDMA_CSR_TCI_DIS;

	writel(csr, p->regs + EBDMA_CSR);

	return 0;
}
EXPORT_SYMBOL(ebus_dma_register);

int ebus_dma_irq_enable(struct ebus_dma_info *p, int on)
{
	unsigned long flags;
	u32 csr;

	if (on) {
		if (p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) {
			if (request_irq(p->irq, ebus_dma_irq, IRQF_SHARED, p->name, p))
				return -EBUSY;
		}

		spin_lock_irqsave(&p->lock, flags);
		csr = readl(p->regs + EBDMA_CSR);
		csr |= EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		spin_unlock_irqrestore(&p->lock, flags);
	} else {
		spin_lock_irqsave(&p->lock, flags);
		csr = readl(p->regs + EBDMA_CSR);
		csr &= ~EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		spin_unlock_irqrestore(&p->lock, flags);

		if (p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) {
			free_irq(p->irq, p);
		}
	}

	return 0;
}
EXPORT_SYMBOL(ebus_dma_irq_enable);

void ebus_dma_unregister(struct ebus_dma_info *p)
{
	unsigned long flags;
	u32 csr;
	int irq_on = 0;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	if (csr & EBDMA_CSR_INT_EN) {
		csr &= ~EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		irq_on = 1;
	}
	spin_unlock_irqrestore(&p->lock, flags);

	if (irq_on)
		free_irq(p->irq, p);
}
EXPORT_SYMBOL(ebus_dma_unregister);

int ebus_dma_request(struct ebus_dma_info *p, dma_addr_t bus_addr, size_t len)
{
	unsigned long flags;
	u32 csr;
	int err;

	if (len >= (1 << 24))
		return -EINVAL;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	err = -EINVAL;
	if (!(csr & EBDMA_CSR_EN_DMA))
		goto out;
	err = -EBUSY;
	if (csr & EBDMA_CSR_NA_LOADED)
		goto out;

	writel(len,      p->regs + EBDMA_COUNT);
	writel(bus_addr, p->regs + EBDMA_ADDR);
	err = 0;

out:
	spin_unlock_irqrestore(&p->lock, flags);

	return err;
}
EXPORT_SYMBOL(ebus_dma_request);

void ebus_dma_prepare(struct ebus_dma_info *p, int write)
{
	unsigned long flags;
	u32 csr;

	spin_lock_irqsave(&p->lock, flags);
	__ebus_dma_reset(p, 0);

	csr = (EBDMA_CSR_INT_EN |
	       EBDMA_CSR_EN_CNT |
	       EBDMA_CSR_BURST_SZ_16 |
	       EBDMA_CSR_EN_NEXT);

	if (write)
		csr |= EBDMA_CSR_WRITE;
	if (p->flags & EBUS_DMA_FLAG_TCI_DISABLE)
		csr |= EBDMA_CSR_TCI_DIS;

	writel(csr, p->regs + EBDMA_CSR);

	spin_unlock_irqrestore(&p->lock, flags);
}
EXPORT_SYMBOL(ebus_dma_prepare);

unsigned int ebus_dma_residue(struct ebus_dma_info *p)
{
	return readl(p->regs + EBDMA_COUNT);
}
EXPORT_SYMBOL(ebus_dma_residue);

unsigned int ebus_dma_addr(struct ebus_dma_info *p)
{
	return readl(p->regs + EBDMA_ADDR);
}
EXPORT_SYMBOL(ebus_dma_addr);

void ebus_dma_enable(struct ebus_dma_info *p, int on)
{
	unsigned long flags;
	u32 orig_csr, csr;

	spin_lock_irqsave(&p->lock, flags);
	orig_csr = csr = readl(p->regs + EBDMA_CSR);
	if (on)
		csr |= EBDMA_CSR_EN_DMA;
	else
		csr &= ~EBDMA_CSR_EN_DMA;
	if ((orig_csr & EBDMA_CSR_EN_DMA) !=
	    (csr & EBDMA_CSR_EN_DMA))
		writel(csr, p->regs + EBDMA_CSR);
	spin_unlock_irqrestore(&p->lock, flags);
}
EXPORT_SYMBOL(ebus_dma_enable);
