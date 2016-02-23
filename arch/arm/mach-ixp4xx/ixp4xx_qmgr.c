/*
 * Intel IXP4xx Queue Manager driver for Linux
 *
 * Copyright (C) 2007 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/qmgr.h>

static struct qmgr_regs __iomem *qmgr_regs = IXP4XX_QMGR_BASE_VIRT;
static struct resource *mem_res;
static spinlock_t qmgr_lock;
static u32 used_sram_bitmap[4]; /* 128 16-dword pages */
static void (*irq_handlers[QUEUES])(void *pdev);
static void *irq_pdevs[QUEUES];

#if DEBUG_QMGR
char qmgr_queue_descs[QUEUES][32];
#endif

void qmgr_set_irq(unsigned int queue, int src,
		  void (*handler)(void *pdev), void *pdev)
{
	unsigned long flags;

	spin_lock_irqsave(&qmgr_lock, flags);
	if (queue < HALF_QUEUES) {
		u32 __iomem *reg;
		int bit;
		BUG_ON(src > QUEUE_IRQ_SRC_NOT_FULL);
		reg = &qmgr_regs->irqsrc[queue >> 3]; /* 8 queues per u32 */
		bit = (queue % 8) * 4; /* 3 bits + 1 reserved bit per queue */
		__raw_writel((__raw_readl(reg) & ~(7 << bit)) | (src << bit),
			     reg);
	} else
		/* IRQ source for queues 32-63 is fixed */
		BUG_ON(src != QUEUE_IRQ_SRC_NOT_NEARLY_EMPTY);

	irq_handlers[queue] = handler;
	irq_pdevs[queue] = pdev;
	spin_unlock_irqrestore(&qmgr_lock, flags);
}


static irqreturn_t qmgr_irq1_a0(int irq, void *pdev)
{
	int i, ret = 0;
	u32 en_bitmap, src, stat;

	/* ACK - it may clear any bits so don't rely on it */
	__raw_writel(0xFFFFFFFF, &qmgr_regs->irqstat[0]);

	en_bitmap = qmgr_regs->irqen[0];
	while (en_bitmap) {
		i = __fls(en_bitmap); /* number of the last "low" queue */
		en_bitmap &= ~BIT(i);
		src = qmgr_regs->irqsrc[i >> 3];
		stat = qmgr_regs->stat1[i >> 3];
		if (src & 4) /* the IRQ condition is inverted */
			stat = ~stat;
		if (stat & BIT(src & 3)) {
			irq_handlers[i](irq_pdevs[i]);
			ret = IRQ_HANDLED;
		}
	}
	return ret;
}


static irqreturn_t qmgr_irq2_a0(int irq, void *pdev)
{
	int i, ret = 0;
	u32 req_bitmap;

	/* ACK - it may clear any bits so don't rely on it */
	__raw_writel(0xFFFFFFFF, &qmgr_regs->irqstat[1]);

	req_bitmap = qmgr_regs->irqen[1] & qmgr_regs->statne_h;
	while (req_bitmap) {
		i = __fls(req_bitmap); /* number of the last "high" queue */
		req_bitmap &= ~BIT(i);
		irq_handlers[HALF_QUEUES + i](irq_pdevs[HALF_QUEUES + i]);
		ret = IRQ_HANDLED;
	}
	return ret;
}


static irqreturn_t qmgr_irq(int irq, void *pdev)
{
	int i, half = (irq == IRQ_IXP4XX_QM1 ? 0 : 1);
	u32 req_bitmap = __raw_readl(&qmgr_regs->irqstat[half]);

	if (!req_bitmap)
		return 0;
	__raw_writel(req_bitmap, &qmgr_regs->irqstat[half]); /* ACK */

	while (req_bitmap) {
		i = __fls(req_bitmap); /* number of the last queue */
		req_bitmap &= ~BIT(i);
		i += half * HALF_QUEUES;
		irq_handlers[i](irq_pdevs[i]);
	}
	return IRQ_HANDLED;
}


void qmgr_enable_irq(unsigned int queue)
{
	unsigned long flags;
	int half = queue / 32;
	u32 mask = 1 << (queue & (HALF_QUEUES - 1));

	spin_lock_irqsave(&qmgr_lock, flags);
	__raw_writel(__raw_readl(&qmgr_regs->irqen[half]) | mask,
		     &qmgr_regs->irqen[half]);
	spin_unlock_irqrestore(&qmgr_lock, flags);
}

void qmgr_disable_irq(unsigned int queue)
{
	unsigned long flags;
	int half = queue / 32;
	u32 mask = 1 << (queue & (HALF_QUEUES - 1));

	spin_lock_irqsave(&qmgr_lock, flags);
	__raw_writel(__raw_readl(&qmgr_regs->irqen[half]) & ~mask,
		     &qmgr_regs->irqen[half]);
	__raw_writel(mask, &qmgr_regs->irqstat[half]); /* clear */
	spin_unlock_irqrestore(&qmgr_lock, flags);
}

static inline void shift_mask(u32 *mask)
{
	mask[3] = mask[3] << 1 | mask[2] >> 31;
	mask[2] = mask[2] << 1 | mask[1] >> 31;
	mask[1] = mask[1] << 1 | mask[0] >> 31;
	mask[0] <<= 1;
}

#if DEBUG_QMGR
int qmgr_request_queue(unsigned int queue, unsigned int len /* dwords */,
		       unsigned int nearly_empty_watermark,
		       unsigned int nearly_full_watermark,
		       const char *desc_format, const char* name)
#else
int __qmgr_request_queue(unsigned int queue, unsigned int len /* dwords */,
			 unsigned int nearly_empty_watermark,
			 unsigned int nearly_full_watermark)
#endif
{
	u32 cfg, addr = 0, mask[4]; /* in 16-dwords */
	int err;

	BUG_ON(queue >= QUEUES);

	if ((nearly_empty_watermark | nearly_full_watermark) & ~7)
		return -EINVAL;

	switch (len) {
	case  16:
		cfg = 0 << 24;
		mask[0] = 0x1;
		break;
	case  32:
		cfg = 1 << 24;
		mask[0] = 0x3;
		break;
	case  64:
		cfg = 2 << 24;
		mask[0] = 0xF;
		break;
	case 128:
		cfg = 3 << 24;
		mask[0] = 0xFF;
		break;
	default:
		return -EINVAL;
	}

	cfg |= nearly_empty_watermark << 26;
	cfg |= nearly_full_watermark << 29;
	len /= 16;		/* in 16-dwords: 1, 2, 4 or 8 */
	mask[1] = mask[2] = mask[3] = 0;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	spin_lock_irq(&qmgr_lock);
	if (__raw_readl(&qmgr_regs->sram[queue])) {
		err = -EBUSY;
		goto err;
	}

	while (1) {
		if (!(used_sram_bitmap[0] & mask[0]) &&
		    !(used_sram_bitmap[1] & mask[1]) &&
		    !(used_sram_bitmap[2] & mask[2]) &&
		    !(used_sram_bitmap[3] & mask[3]))
			break; /* found free space */

		addr++;
		shift_mask(mask);
		if (addr + len > ARRAY_SIZE(qmgr_regs->sram)) {
			printk(KERN_ERR "qmgr: no free SRAM space for"
			       " queue %i\n", queue);
			err = -ENOMEM;
			goto err;
		}
	}

	used_sram_bitmap[0] |= mask[0];
	used_sram_bitmap[1] |= mask[1];
	used_sram_bitmap[2] |= mask[2];
	used_sram_bitmap[3] |= mask[3];
	__raw_writel(cfg | (addr << 14), &qmgr_regs->sram[queue]);
#if DEBUG_QMGR
	snprintf(qmgr_queue_descs[queue], sizeof(qmgr_queue_descs[0]),
		 desc_format, name);
	printk(KERN_DEBUG "qmgr: requested queue %s(%i) addr = 0x%02X\n",
	       qmgr_queue_descs[queue], queue, addr);
#endif
	spin_unlock_irq(&qmgr_lock);
	return 0;

err:
	spin_unlock_irq(&qmgr_lock);
	module_put(THIS_MODULE);
	return err;
}

void qmgr_release_queue(unsigned int queue)
{
	u32 cfg, addr, mask[4];

	BUG_ON(queue >= QUEUES); /* not in valid range */

	spin_lock_irq(&qmgr_lock);
	cfg = __raw_readl(&qmgr_regs->sram[queue]);
	addr = (cfg >> 14) & 0xFF;

	BUG_ON(!addr);		/* not requested */

	switch ((cfg >> 24) & 3) {
	case 0: mask[0] = 0x1; break;
	case 1: mask[0] = 0x3; break;
	case 2: mask[0] = 0xF; break;
	case 3: mask[0] = 0xFF; break;
	}

	mask[1] = mask[2] = mask[3] = 0;

	while (addr--)
		shift_mask(mask);

#if DEBUG_QMGR
	printk(KERN_DEBUG "qmgr: releasing queue %s(%i)\n",
	       qmgr_queue_descs[queue], queue);
	qmgr_queue_descs[queue][0] = '\x0';
#endif

	while ((addr = qmgr_get_entry(queue)))
		printk(KERN_ERR "qmgr: released queue %i not empty: 0x%08X\n",
		       queue, addr);

	__raw_writel(0, &qmgr_regs->sram[queue]);

	used_sram_bitmap[0] &= ~mask[0];
	used_sram_bitmap[1] &= ~mask[1];
	used_sram_bitmap[2] &= ~mask[2];
	used_sram_bitmap[3] &= ~mask[3];
	irq_handlers[queue] = NULL; /* catch IRQ bugs */
	spin_unlock_irq(&qmgr_lock);

	module_put(THIS_MODULE);
}

static int qmgr_init(void)
{
	int i, err;
	irq_handler_t handler1, handler2;

	mem_res = request_mem_region(IXP4XX_QMGR_BASE_PHYS,
				     IXP4XX_QMGR_REGION_SIZE,
				     "IXP4xx Queue Manager");
	if (mem_res == NULL)
		return -EBUSY;

	/* reset qmgr registers */
	for (i = 0; i < 4; i++) {
		__raw_writel(0x33333333, &qmgr_regs->stat1[i]);
		__raw_writel(0, &qmgr_regs->irqsrc[i]);
	}
	for (i = 0; i < 2; i++) {
		__raw_writel(0, &qmgr_regs->stat2[i]);
		__raw_writel(0xFFFFFFFF, &qmgr_regs->irqstat[i]); /* clear */
		__raw_writel(0, &qmgr_regs->irqen[i]);
	}

	__raw_writel(0xFFFFFFFF, &qmgr_regs->statne_h);
	__raw_writel(0, &qmgr_regs->statf_h);

	for (i = 0; i < QUEUES; i++)
		__raw_writel(0, &qmgr_regs->sram[i]);

	if (cpu_is_ixp42x_rev_a0()) {
		handler1 = qmgr_irq1_a0;
		handler2 = qmgr_irq2_a0;
	} else
		handler1 = handler2 = qmgr_irq;

	err = request_irq(IRQ_IXP4XX_QM1, handler1, 0, "IXP4xx Queue Manager",
			  NULL);
	if (err) {
		printk(KERN_ERR "qmgr: failed to request IRQ%i (%i)\n",
		       IRQ_IXP4XX_QM1, err);
		goto error_irq;
	}

	err = request_irq(IRQ_IXP4XX_QM2, handler2, 0, "IXP4xx Queue Manager",
			  NULL);
	if (err) {
		printk(KERN_ERR "qmgr: failed to request IRQ%i (%i)\n",
		       IRQ_IXP4XX_QM2, err);
		goto error_irq2;
	}

	used_sram_bitmap[0] = 0xF; /* 4 first pages reserved for config */
	spin_lock_init(&qmgr_lock);

	printk(KERN_INFO "IXP4xx Queue Manager initialized.\n");
	return 0;

error_irq2:
	free_irq(IRQ_IXP4XX_QM1, NULL);
error_irq:
	release_mem_region(IXP4XX_QMGR_BASE_PHYS, IXP4XX_QMGR_REGION_SIZE);
	return err;
}

static void qmgr_remove(void)
{
	free_irq(IRQ_IXP4XX_QM1, NULL);
	free_irq(IRQ_IXP4XX_QM2, NULL);
	synchronize_irq(IRQ_IXP4XX_QM1);
	synchronize_irq(IRQ_IXP4XX_QM2);
	release_mem_region(IXP4XX_QMGR_BASE_PHYS, IXP4XX_QMGR_REGION_SIZE);
}

module_init(qmgr_init);
module_exit(qmgr_remove);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Krzysztof Halasa");

EXPORT_SYMBOL(qmgr_set_irq);
EXPORT_SYMBOL(qmgr_enable_irq);
EXPORT_SYMBOL(qmgr_disable_irq);
#if DEBUG_QMGR
EXPORT_SYMBOL(qmgr_queue_descs);
EXPORT_SYMBOL(qmgr_request_queue);
#else
EXPORT_SYMBOL(__qmgr_request_queue);
#endif
EXPORT_SYMBOL(qmgr_release_queue);
