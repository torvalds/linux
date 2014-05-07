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
#include <linux/export.h>
#include <linux/slab.h>
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
	 /* Update callback used to cope with buggy hardware */
	ht_irq_update_t *update;
	unsigned pos;
	unsigned idx;
	struct ht_irq_msg msg;
};


void write_ht_irq_msg(unsigned int irq, struct ht_irq_msg *msg)
{
	struct ht_irq_cfg *cfg = irq_get_handler_data(irq);
	unsigned long flags;
	spin_lock_irqsave(&ht_irq_lock, flags);
	if (cfg->msg.address_lo != msg->address_lo) {
		pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx);
		pci_write_config_dword(cfg->dev, cfg->pos + 4, msg->address_lo);
	}
	if (cfg->msg.address_hi != msg->address_hi) {
		pci_write_config_byte(cfg->dev, cfg->pos + 2, cfg->idx + 1);
		pci_write_config_dword(cfg->dev, cfg->pos + 4, msg->address_hi);
	}
	if (cfg->update)
		cfg->update(cfg->dev, irq, msg);
	spin_unlock_irqrestore(&ht_irq_lock, flags);
	cfg->msg = *msg;
}

void fetch_ht_irq_msg(unsigned int irq, struct ht_irq_msg *msg)
{
	struct ht_irq_cfg *cfg = irq_get_handler_data(irq);
	*msg = cfg->msg;
}

void mask_ht_irq(struct irq_data *data)
{
	struct ht_irq_cfg *cfg = irq_data_get_irq_handler_data(data);
	struct ht_irq_msg msg = cfg->msg;

	msg.address_lo |= 1;
	write_ht_irq_msg(data->irq, &msg);
}

void unmask_ht_irq(struct irq_data *data)
{
	struct ht_irq_cfg *cfg = irq_data_get_irq_handler_data(data);
	struct ht_irq_msg msg = cfg->msg;

	msg.address_lo &= ~1;
	write_ht_irq_msg(data->irq, &msg);
}

/**
 * __ht_create_irq - create an irq and attach it to a device.
 * @dev: The hypertransport device to find the irq capability on.
 * @idx: Which of the possible irqs to attach to.
 * @update: Function to be called when changing the htirq message
 *
 * The irq number of the new irq or a negative error value is returned.
 */
int __ht_create_irq(struct pci_dev *dev, int idx, ht_irq_update_t *update)
{
	struct ht_irq_cfg *cfg;
	int max_irq, pos, irq;
	unsigned long flags;
	u32 data;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_IRQ);
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
	cfg->update = update;
	cfg->pos = pos;
	cfg->idx = 0x10 + (idx * 2);
	/* Initialize msg to a value that will never match the first write. */
	cfg->msg.address_lo = 0xffffffff;
	cfg->msg.address_hi = 0xffffffff;

	irq = irq_alloc_hwirq(dev_to_node(&dev->dev));
	if (!irq) {
		kfree(cfg);
		return -EBUSY;
	}
	irq_set_handler_data(irq, cfg);

	if (arch_setup_ht_irq(irq, dev) < 0) {
		ht_destroy_irq(irq);
		return -EBUSY;
	}

	return irq;
}

/**
 * ht_create_irq - create an irq and attach it to a device.
 * @dev: The hypertransport device to find the irq capability on.
 * @idx: Which of the possible irqs to attach to.
 *
 * ht_create_irq needs to be called for all hypertransport devices
 * that generate irqs.
 *
 * The irq number of the new irq or a negative error value is returned.
 */
int ht_create_irq(struct pci_dev *dev, int idx)
{
	return __ht_create_irq(dev, idx, NULL);
}

/**
 * ht_destroy_irq - destroy an irq created with ht_create_irq
 * @irq: irq to be destroyed
 *
 * This reverses ht_create_irq removing the specified irq from
 * existence.  The irq should be free before this happens.
 */
void ht_destroy_irq(unsigned int irq)
{
	struct ht_irq_cfg *cfg;

	cfg = irq_get_handler_data(irq);
	irq_set_chip(irq, NULL);
	irq_set_handler_data(irq, NULL);
	irq_free_hwirq(irq);

	kfree(cfg);
}

EXPORT_SYMBOL(__ht_create_irq);
EXPORT_SYMBOL(ht_create_irq);
EXPORT_SYMBOL(ht_destroy_irq);
