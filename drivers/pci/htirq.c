/*
 * File:	htirq.c
 * Purpose:	Hypertransport Interrupt Capability
 *
 * Copyright (C) 2006 Linux Networx
 * Copyright (C) Eric Biederman <ebiederman@lnxi.com>
 */

#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/htirq.h>

/* Global ht irq lock.
 *
 * This is needed to serialize access to the data port in hypertransport
 * irq capability.
 *
 * With multiple simultaneous hypertransport irq devices it might pay
 * to make this more fine grained.  But start with simple, stupid, and correct.
 */
static DEFINE_SPINLOCK(ht_irq_lock);

struct ht_irq_cfg {
	struct pci_dev *dev;
	unsigned pos;
	unsigned idx;
};

void write_ht_irq_low(unsigned int irq, u32 data)
{
	struct ht_irq_cfg *cfg = get_irq_data(irq);
	unsigned long flags;
	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx);
	pci_write_config_dword(cfg->dev, cfg->pos + 4, data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
}

void write_ht_irq_high(unsigned int irq, u32 data)
{
	struct ht_irq_cfg *cfg = get_irq_data(irq);
	unsigned long flags;
	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx + 1);
	pci_write_config_dword(cfg->dev, cfg->pos + 4, data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
}

u32 read_ht_irq_low(unsigned int irq)
{
	struct ht_irq_cfg *cfg = get_irq_data(irq);
	unsigned long flags;
	u32 data;
	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx);
	pci_read_config_dword(cfg->dev, cfg->pos + 4, &data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
	return data;
}

u32 read_ht_irq_high(unsigned int irq)
{
	struct ht_irq_cfg *cfg = get_irq_data(irq);
	unsigned long flags;
	u32 data;
	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx + 1);
	pci_read_config_dword(cfg->dev, cfg->pos + 4, &data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
	return data;
}

void mask_ht_irq(unsigned int irq)
{
	struct ht_irq_cfg *cfg;
	unsigned long flags;
	u32 data;

	cfg = get_irq_data(irq);

	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx);
	pci_read_config_dword(cfg->dev, cfg->pos + 4, &data);
	data |= 1;
	pci_write_config_dword(cfg->dev, cfg->pos + 4, data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
}

void unmask_ht_irq(unsigned int irq)
{
	struct ht_irq_cfg *cfg;
	unsigned long flags;
	u32 data;

	cfg = get_irq_data(irq);

	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx);
	pci_read_config_dword(cfg->dev, cfg->pos + 4, &data);
	data &= ~1;
	pci_write_config_dword(cfg->dev, cfg->pos + 4, data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
}

/**
 * ht_create_irq - create an irq and attach it to a device.
 * @dev: The hypertransport device to find the irq capability on.
 * @idx: Which of the possible irqs to attach to.
 *
 * ht_create_irq is needs to be called for all hypertransport devices
 * that generate irqs.
 *
 * The irq number of the new irq or a negative error value is returned.
 */
int ht_create_irq(struct pci_dev *dev, int idx)
{
	struct ht_irq_cfg *cfg;
	unsigned long flags;
	u32 data;
	int max_irq;
	int pos;
	int irq;

	pos = pci_find_capability(dev, PCI_CAP_ID_HT);
	while (pos) {
		u8 subtype;
		pci_read_config_byte(dev, pos + 3, &subtype);
		if (subtype == HT_CAPTYPE_IRQ)
			break;
		pos = pci_find_next_capability(dev, pos, PCI_CAP_ID_HT);
	}
	if (!pos)
		return -EINVAL;

	/* Verify the idx I want to use is in range */
	spin_lock_irqsave(&ht_irq_lock, flags);
	pci_write_config_byte(dev, pos + 2, 1);
	pci_read_config_dword(dev, pos + 4, &data);
	spin_unlock_irqrestore(&ht_irq_lock, flags);

	max_irq = (data >> 16) & 0xff;
	if ( idx > max_irq)
		return -EINVAL;

	cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	cfg->dev = dev;
	cfg->pos = pos;
	cfg->idx = 0x10 + (idx * 2);

	irq = create_irq();
	if (irq < 0) {
		kfree(cfg);
		return -EBUSY;
	}
	set_irq_data(irq, cfg);

	if (arch_setup_ht_irq(irq, dev) < 0) {
		ht_destroy_irq(irq);
		return -EBUSY;
	}

	return irq;
}

/**
 * ht_destroy_irq - destroy an irq created with ht_create_irq
 *
 * This reverses ht_create_irq removing the specified irq from
 * existence.  The irq should be free before this happens.
 */
void ht_destroy_irq(unsigned int irq)
{
	struct ht_irq_cfg *cfg;

	cfg = get_irq_data(irq);
	set_irq_chip(irq, NULL);
	set_irq_data(irq, NULL);
	destroy_irq(irq);

	kfree(cfg);
}

EXPORT_SYMBOL(ht_create_irq);
EXPORT_SYMBOL(ht_destroy_irq);
