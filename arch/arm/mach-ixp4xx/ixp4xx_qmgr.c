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

struct qmgr_regs __iomem *qmgr_regs;
static struct resource *mem_res;
static spinlock_t qmgr_lock;
static u32 used_sram_bitmap[4]; /* 128 16-dword pages */
static void (*irq_handlers[HALF_QUEUES])(void *pdev);
static void *irq_pdevs[HALF_QUEUES];

#if DEBUG_QMGR
char qmgr_queue_descs[QUEUES][32];
#endif

void qmgr_set_irq(unsigned int queue, int src,
		  void (*handler)(void *pdev), void *pdev)
{
	u32 __iomem *reg = &qmgr_regs->irqsrc[queue / 8]; /* 8 queues / u32 */
	int bit = (queue % 8) * 4; /* 3 bits + 1 reserved bit per queue */
	unsigned long flags;

	src &= 7;
	spin_lock_irqsave(&qmgr_lock, flags);
	__raw_writel((__raw_readl(reg) & ~(7 << bit)) | (src << bit), reg);
	irq_handlers[queue] = handler;
	irq_pdevs[queue] = pdev;
	spin_unlock_irqrestore(&qmgr_lock, flags);
}


static irqreturn_t qmgr_irq1(int irq, void *pdev)
{
	int i;
	u32 val = __raw_readl(&qmgr_regs->irqstat[0]);
	__raw_writel(val, &qmgr_regs->irqstat[0]); /* ACK */

	for (i = 0; i < HALF_QUEUES; i++)
		if (val & (1 << i))
			irq_handlers[i](irq_pdevs[i]);

	return val ? IRQ_HANDLED : 0;
}


void qmgr_enable_irq(unsigned int queue)
{
	unsigned long flags;

	spin_lock_irqsave(&qmgr_lock, flags);
	__raw_writel(__raw_readl(&qmgr_regs->irqen[0]) | (1 << queue),
		     &qmgr_regs->irqen[0]);
	spin_unlock_irqrestore(&qmgr_lock, flags);
}

void qmgr_disable_irq(unsigned int queue)
{
	unsigned long flags;

	spin_lock_irqsave(&qmgr_lock, flags);
	__raw_writel(__raw_readl(&qmgr_regs->irqen[0]) & ~(1 << queue),
		     &qmgr_regs->irqen[0]);
	__raw_writel(1 << queue, &qmgr_regs->irqstat[0]); /* clear */
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

	if (queue >= HALF_QUEUES)
		return -ERANGE;

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

	BUG_ON(queue >= HALF_QUEUES); /* not in valid range */

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
	__raw_writel(0, &qmgr_regs->sram[queue]);

	used_sram_bitmap[0] &= ~mask[0];
	used_sram_bitmap[1] &= ~mask[1];
	used_sram_bitmap[2] &= ~mask[2];
	used_sram_bitmap[3] &= ~mask[3];
	irq_handlers[queue] = NULL; /* catch IRQ bugs */
	spin_unlock_irq(&qmgr_lock);

	module_put(THIS_MODULE);

	while ((addr = qmgr_get_entry(queue)))
		printk(KERN_ERR "qmgr: released queue %i not empty: 0x%08X\n",
		       queue, addr);
}

static int qmgr_init(void)
{
	int i, err;
	mem_res = request_mem_region(IXP4XX_QMGR_BASE_PHYS,
				     IXP4XX_QMGR_REGION_SIZE,
				     "IXP4xx Queue Manager");
	if (mem_res == NULL)
		return -EBUSY;

	qmgr_regs = ioremap(IXP4XX_QMGR_BASE_PHYS, IXP4XX_QMGR_REGION_SIZE);
	if (qmgr_regs == NULL) {
		err = -ENOMEM;
		goto error_map;
	}

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

	for (i = 0; i < QUEUES; i++)
		__raw_writel(0, &qmgr_regs->sram[i]);

	err = request_irq(IRQ_IXP4XX_QM1, qmgr_irq1, 0,
			  "IXP4xx Queue Manager", NULL);
	if (err) {
		printk(KERN_ERR "qmgr: failed to request IRQ%i\n",
		       IRQ_IXP4XX_QM1);
		goto error_irq;
	}

	used_sram_bitmap[0] = 0xF; /* 4 first pages reserved for config */
	spin_lock_init(&qmgr_lock);

	printk(KERN_INFO "IXP4xx Queue Manager initialized.\n");
	return 0;

error_irq:
	iounmap(qmgr_regs);
error_map:
	release_mem_region(IXP4XX_QMGR_BASE_PHYS, IXP4XX_QMGR_REGION_SIZE);
	return err;
}

static void qmgr_remove(void)
{
	free_irq(IRQ_IXP4XX_QM1, NULL);
	synchronize_irq(IRQ_IXP4XX_QM1);
	iounmap(qmgr_regs);
	release_mem_region(IXP4XX_QMGR_BASE_PHYS, IXP4XX_QMGR_REGION_SIZE);
}

module_init(qmgr_init);
module_exit(qmgr_remove);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Krzysztof Halasa");

EXPORT_SYMBOL(qmgr_regs);
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
