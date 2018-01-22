/*
 * Copyright 2011-2012, Meador Inge, Mentor Graphics Corporation.
 *
 * Some ideas based on un-pushed work done by Vivek Mahajan, Jason Jin, and
 * Mingkai Hu from Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/list.h>
#include <linux/of_platform.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>
#include <asm/mpic_msgr.h>

#define MPIC_MSGR_REGISTERS_PER_BLOCK	4
#define MPIC_MSGR_STRIDE		0x10
#define MPIC_MSGR_MER_OFFSET		0x100
#define MSGR_INUSE			0
#define MSGR_FREE			1

static struct mpic_msgr **mpic_msgrs;
static unsigned int mpic_msgr_count;
static DEFINE_RAW_SPINLOCK(msgrs_lock);

static inline void _mpic_msgr_mer_write(struct mpic_msgr *msgr, u32 value)
{
	out_be32(msgr->mer, value);
}

static inline u32 _mpic_msgr_mer_read(struct mpic_msgr *msgr)
{
	return in_be32(msgr->mer);
}

static inline void _mpic_msgr_disable(struct mpic_msgr *msgr)
{
	u32 mer = _mpic_msgr_mer_read(msgr);

	_mpic_msgr_mer_write(msgr, mer & ~(1 << msgr->num));
}

struct mpic_msgr *mpic_msgr_get(unsigned int reg_num)
{
	unsigned long flags;
	struct mpic_msgr *msgr;

	/* Assume busy until proven otherwise.  */
	msgr = ERR_PTR(-EBUSY);

	if (reg_num >= mpic_msgr_count)
		return ERR_PTR(-ENODEV);

	raw_spin_lock_irqsave(&msgrs_lock, flags);
	msgr = mpic_msgrs[reg_num];
	if (msgr->in_use == MSGR_FREE)
		msgr->in_use = MSGR_INUSE;
	raw_spin_unlock_irqrestore(&msgrs_lock, flags);

	return msgr;
}
EXPORT_SYMBOL_GPL(mpic_msgr_get);

void mpic_msgr_put(struct mpic_msgr *msgr)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&msgr->lock, flags);
	msgr->in_use = MSGR_FREE;
	_mpic_msgr_disable(msgr);
	raw_spin_unlock_irqrestore(&msgr->lock, flags);
}
EXPORT_SYMBOL_GPL(mpic_msgr_put);

void mpic_msgr_enable(struct mpic_msgr *msgr)
{
	unsigned long flags;
	u32 mer;

	raw_spin_lock_irqsave(&msgr->lock, flags);
	mer = _mpic_msgr_mer_read(msgr);
	_mpic_msgr_mer_write(msgr, mer | (1 << msgr->num));
	raw_spin_unlock_irqrestore(&msgr->lock, flags);
}
EXPORT_SYMBOL_GPL(mpic_msgr_enable);

void mpic_msgr_disable(struct mpic_msgr *msgr)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&msgr->lock, flags);
	_mpic_msgr_disable(msgr);
	raw_spin_unlock_irqrestore(&msgr->lock, flags);
}
EXPORT_SYMBOL_GPL(mpic_msgr_disable);

/* The following three functions are used to compute the order and number of
 * the message register blocks.  They are clearly very inefficent.  However,
 * they are called *only* a few times during device initialization.
 */
static unsigned int mpic_msgr_number_of_blocks(void)
{
	unsigned int count;
	struct device_node *aliases;

	count = 0;
	aliases = of_find_node_by_name(NULL, "aliases");

	if (aliases) {
		char buf[32];

		for (;;) {
			snprintf(buf, sizeof(buf), "mpic-msgr-block%d", count);
			if (!of_find_property(aliases, buf, NULL))
				break;

			count += 1;
		}
	}

	return count;
}

static unsigned int mpic_msgr_number_of_registers(void)
{
	return mpic_msgr_number_of_blocks() * MPIC_MSGR_REGISTERS_PER_BLOCK;
}

static int mpic_msgr_block_number(struct device_node *node)
{
	struct device_node *aliases;
	unsigned int index, number_of_blocks;
	char buf[64];

	number_of_blocks = mpic_msgr_number_of_blocks();
	aliases = of_find_node_by_name(NULL, "aliases");
	if (!aliases)
		return -1;

	for (index = 0; index < number_of_blocks; ++index) {
		struct property *prop;

		snprintf(buf, sizeof(buf), "mpic-msgr-block%d", index);
		prop = of_find_property(aliases, buf, NULL);
		if (node == of_find_node_by_path(prop->value))
			break;
	}

	return index == number_of_blocks ? -1 : index;
}

/* The probe function for a single message register block.
 */
static int mpic_msgr_probe(struct platform_device *dev)
{
	void __iomem *msgr_block_addr;
	int block_number;
	struct resource rsrc;
	unsigned int i;
	unsigned int irq_index;
	struct device_node *np = dev->dev.of_node;
	unsigned int receive_mask;
	const unsigned int *prop;

	if (!np) {
		dev_err(&dev->dev, "Device OF-Node is NULL");
		return -EFAULT;
	}

	/* Allocate the message register array upon the first device
	 * registered.
	 */
	if (!mpic_msgrs) {
		mpic_msgr_count = mpic_msgr_number_of_registers();
		dev_info(&dev->dev, "Found %d message registers\n",
				mpic_msgr_count);

		mpic_msgrs = kcalloc(mpic_msgr_count, sizeof(*mpic_msgrs),
							 GFP_KERNEL);
		if (!mpic_msgrs) {
			dev_err(&dev->dev,
				"No memory for message register blocks\n");
			return -ENOMEM;
		}
	}
	dev_info(&dev->dev, "Of-device full name %pOF\n", np);

	/* IO map the message register block. */
	of_address_to_resource(np, 0, &rsrc);
	msgr_block_addr = ioremap(rsrc.start, rsrc.end - rsrc.start);
	if (!msgr_block_addr) {
		dev_err(&dev->dev, "Failed to iomap MPIC message registers");
		return -EFAULT;
	}

	/* Ensure the block has a defined order. */
	block_number = mpic_msgr_block_number(np);
	if (block_number < 0) {
		dev_err(&dev->dev,
			"Failed to find message register block alias\n");
		return -ENODEV;
	}
	dev_info(&dev->dev, "Setting up message register block %d\n",
			block_number);

	/* Grab the receive mask which specifies what registers can receive
	 * interrupts.
	 */
	prop = of_get_property(np, "mpic-msgr-receive-mask", NULL);
	receive_mask = (prop) ? *prop : 0xF;

	/* Build up the appropriate message register data structures. */
	for (i = 0, irq_index = 0; i < MPIC_MSGR_REGISTERS_PER_BLOCK; ++i) {
		struct mpic_msgr *msgr;
		unsigned int reg_number;

		msgr = kzalloc(sizeof(struct mpic_msgr), GFP_KERNEL);
		if (!msgr) {
			dev_err(&dev->dev, "No memory for message register\n");
			return -ENOMEM;
		}

		reg_number = block_number * MPIC_MSGR_REGISTERS_PER_BLOCK + i;
		msgr->base = msgr_block_addr + i * MPIC_MSGR_STRIDE;
		msgr->mer = (u32 *)((u8 *)msgr->base + MPIC_MSGR_MER_OFFSET);
		msgr->in_use = MSGR_FREE;
		msgr->num = i;
		raw_spin_lock_init(&msgr->lock);

		if (receive_mask & (1 << i)) {
			msgr->irq = irq_of_parse_and_map(np, irq_index);
			if (!msgr->irq) {
				dev_err(&dev->dev,
						"Missing interrupt specifier");
				kfree(msgr);
				return -EFAULT;
			}
			irq_index += 1;
		} else {
			msgr->irq = 0;
		}

		mpic_msgrs[reg_number] = msgr;
		mpic_msgr_disable(msgr);
		dev_info(&dev->dev, "Register %d initialized: irq %d\n",
				reg_number, msgr->irq);

	}

	return 0;
}

static const struct of_device_id mpic_msgr_ids[] = {
	{
		.compatible = "fsl,mpic-v3.1-msgr",
		.data = NULL,
	},
	{}
};

static struct platform_driver mpic_msgr_driver = {
	.driver = {
		.name = "mpic-msgr",
		.of_match_table = mpic_msgr_ids,
	},
	.probe = mpic_msgr_probe,
};

static __init int mpic_msgr_init(void)
{
	return platform_driver_register(&mpic_msgr_driver);
}
subsys_initcall(mpic_msgr_init);
