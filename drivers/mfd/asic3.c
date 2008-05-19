/*
 * driver/mfd/asic3.c
 *
 * Compaq ASIC3 support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/mfd/asic3.h>

static inline void asic3_write_register(struct asic3 *asic,
				 unsigned int reg, u32 value)
{
	iowrite16(value, asic->mapping +
		  (reg >> asic->bus_shift));
}

static inline u32 asic3_read_register(struct asic3 *asic,
			       unsigned int reg)
{
	return ioread16(asic->mapping +
			(reg >> asic->bus_shift));
}

/* IRQs */
#define MAX_ASIC_ISR_LOOPS    20
#define ASIC3_GPIO_Base_INCR \
	(ASIC3_GPIO_B_Base - ASIC3_GPIO_A_Base)

static void asic3_irq_flip_edge(struct asic3 *asic,
				u32 base, int bit)
{
	u16 edge;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	edge = asic3_read_register(asic,
				   base + ASIC3_GPIO_EdgeTrigger);
	edge ^= bit;
	asic3_write_register(asic,
			     base + ASIC3_GPIO_EdgeTrigger, edge);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	int iter, i;
	unsigned long flags;
	struct asic3 *asic;

	desc->chip->ack(irq);

	asic = desc->handler_data;

	for (iter = 0 ; iter < MAX_ASIC_ISR_LOOPS; iter++) {
		u32 status;
		int bank;

		spin_lock_irqsave(&asic->lock, flags);
		status = asic3_read_register(asic,
					     ASIC3_OFFSET(INTR, PIntStat));
		spin_unlock_irqrestore(&asic->lock, flags);

		/* Check all ten register bits */
		if ((status & 0x3ff) == 0)
			break;

		/* Handle GPIO IRQs */
		for (bank = 0; bank < ASIC3_NUM_GPIO_BANKS; bank++) {
			if (status & (1 << bank)) {
				unsigned long base, istat;

				base = ASIC3_GPIO_A_Base
				       + bank * ASIC3_GPIO_Base_INCR;

				spin_lock_irqsave(&asic->lock, flags);
				istat = asic3_read_register(asic,
							    base +
							    ASIC3_GPIO_IntStatus);
				/* Clearing IntStatus */
				asic3_write_register(asic,
						     base +
						     ASIC3_GPIO_IntStatus, 0);
				spin_unlock_irqrestore(&asic->lock, flags);

				for (i = 0; i < ASIC3_GPIOS_PER_BANK; i++) {
					int bit = (1 << i);
					unsigned int irqnr;

					if (!(istat & bit))
						continue;

					irqnr = asic->irq_base +
						(ASIC3_GPIOS_PER_BANK * bank)
						+ i;
					desc = irq_desc + irqnr;
					desc->handle_irq(irqnr, desc);
					if (asic->irq_bothedge[bank] & bit)
						asic3_irq_flip_edge(asic, base,
								    bit);
				}
			}
		}

		/* Handle remaining IRQs in the status register */
		for (i = ASIC3_NUM_GPIOS; i < ASIC3_NR_IRQS; i++) {
			/* They start at bit 4 and go up */
			if (status & (1 << (i - ASIC3_NUM_GPIOS + 4))) {
				desc = irq_desc +  + i;
				desc->handle_irq(asic->irq_base + i,
						 desc);
			}
		}
	}

	if (iter >= MAX_ASIC_ISR_LOOPS)
		printk(KERN_ERR "%s: interrupt processing overrun\n",
		       __func__);
}

static inline int asic3_irq_to_bank(struct asic3 *asic, int irq)
{
	int n;

	n = (irq - asic->irq_base) >> 4;

	return (n * (ASIC3_GPIO_B_Base - ASIC3_GPIO_A_Base));
}

static inline int asic3_irq_to_index(struct asic3 *asic, int irq)
{
	return (irq - asic->irq_base) & 0xf;
}

static void asic3_mask_gpio_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_Mask);
	val |= 1 << index;
	asic3_write_register(asic, bank + ASIC3_GPIO_Mask, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_mask_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_Base +
				     ASIC3_INTR_IntMask);

	regval &= ~(ASIC3_INTMASK_MASK0 <<
		    (irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_Base +
			     ASIC3_INTR_IntMask,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_gpio_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_Mask);
	val &= ~(1 << index);
	asic3_write_register(asic, bank + ASIC3_GPIO_Mask, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_Base +
				     ASIC3_INTR_IntMask);

	regval |= (ASIC3_INTMASK_MASK0 <<
		   (irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_Base +
			     ASIC3_INTR_IntMask,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static int asic3_gpio_irq_type(unsigned int irq, unsigned int type)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 bank, index;
	u16 trigger, level, edge, bit;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);
	bit = 1<<index;

	spin_lock_irqsave(&asic->lock, flags);
	level = asic3_read_register(asic,
				    bank + ASIC3_GPIO_LevelTrigger);
	edge = asic3_read_register(asic,
				   bank + ASIC3_GPIO_EdgeTrigger);
	trigger = asic3_read_register(asic,
				      bank + ASIC3_GPIO_TriggerType);
	asic->irq_bothedge[(irq - asic->irq_base) >> 4] &= ~bit;

	if (type == IRQT_RISING) {
		trigger |= bit;
		edge |= bit;
	} else if (type == IRQT_FALLING) {
		trigger |= bit;
		edge &= ~bit;
	} else if (type == IRQT_BOTHEDGE) {
		trigger |= bit;
		if (asic3_gpio_get_value(asic, irq - asic->irq_base))
			edge &= ~bit;
		else
			edge |= bit;
		asic->irq_bothedge[(irq - asic->irq_base) >> 4] |= bit;
	} else if (type == IRQT_LOW) {
		trigger &= ~bit;
		level &= ~bit;
	} else if (type == IRQT_HIGH) {
		trigger &= ~bit;
		level |= bit;
	} else {
		/*
		 * if type == IRQT_NOEDGE, we should mask interrupts, but
		 * be careful to not unmask them if mask was also called.
		 * Probably need internal state for mask.
		 */
		printk(KERN_NOTICE "asic3: irq type not changed.\n");
	}
	asic3_write_register(asic, bank + ASIC3_GPIO_LevelTrigger,
			     level);
	asic3_write_register(asic, bank + ASIC3_GPIO_EdgeTrigger,
			     edge);
	asic3_write_register(asic, bank + ASIC3_GPIO_TriggerType,
			     trigger);
	spin_unlock_irqrestore(&asic->lock, flags);
	return 0;
}

static struct irq_chip asic3_gpio_irq_chip = {
	.name		= "ASIC3-GPIO",
	.ack		= asic3_mask_gpio_irq,
	.mask		= asic3_mask_gpio_irq,
	.unmask		= asic3_unmask_gpio_irq,
	.set_type	= asic3_gpio_irq_type,
};

static struct irq_chip asic3_irq_chip = {
	.name		= "ASIC3",
	.ack		= asic3_mask_irq,
	.mask		= asic3_mask_irq,
	.unmask		= asic3_unmask_irq,
};

static int asic3_irq_probe(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	unsigned long clksel = 0;
	unsigned int irq, irq_base;

	asic->irq_nr = platform_get_irq(pdev, 0);
	if (asic->irq_nr < 0)
		return asic->irq_nr;

	/* turn on clock to IRQ controller */
	clksel |= CLOCK_SEL_CX;
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL),
			     clksel);

	irq_base = asic->irq_base;

	for (irq = irq_base; irq < irq_base + ASIC3_NR_IRQS; irq++) {
		if (irq < asic->irq_base + ASIC3_NUM_GPIOS)
			set_irq_chip(irq, &asic3_gpio_irq_chip);
		else
			set_irq_chip(irq, &asic3_irq_chip);

		set_irq_chip_data(irq, asic);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	asic3_write_register(asic, ASIC3_OFFSET(INTR, IntMask),
			     ASIC3_INTMASK_GINTMASK);

	set_irq_chained_handler(asic->irq_nr, asic3_irq_demux);
	set_irq_type(asic->irq_nr, IRQT_RISING);
	set_irq_data(asic->irq_nr, asic);

	return 0;
}

static void asic3_irq_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	unsigned int irq, irq_base;

	irq_base = asic->irq_base;

	for (irq = irq_base; irq < irq_base + ASIC3_NR_IRQS; irq++) {
		set_irq_flags(irq, 0);
		set_irq_handler(irq, NULL);
		set_irq_chip(irq, NULL);
		set_irq_chip_data(irq, NULL);
	}
	set_irq_chained_handler(asic->irq_nr, NULL);
}

/* GPIOs */
static inline u32 asic3_get_gpio(struct asic3 *asic, unsigned int base,
				 unsigned int function)
{
	return asic3_read_register(asic, base + function);
}

static void asic3_set_gpio(struct asic3 *asic, unsigned int base,
			   unsigned int function, u32 bits, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	val |= (asic3_read_register(asic, base + function) & ~bits);

	asic3_write_register(asic, base + function, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

#define asic3_get_gpio_a(asic, fn) \
	asic3_get_gpio(asic, ASIC3_GPIO_A_Base, ASIC3_GPIO_##fn)
#define asic3_get_gpio_b(asic, fn) \
	asic3_get_gpio(asic, ASIC3_GPIO_B_Base, ASIC3_GPIO_##fn)
#define asic3_get_gpio_c(asic, fn) \
	asic3_get_gpio(asic, ASIC3_GPIO_C_Base, ASIC3_GPIO_##fn)
#define asic3_get_gpio_d(asic, fn) \
	asic3_get_gpio(asic, ASIC3_GPIO_D_Base, ASIC3_GPIO_##fn)

#define asic3_set_gpio_a(asic, fn, bits, val) \
	asic3_set_gpio(asic, ASIC3_GPIO_A_Base, ASIC3_GPIO_##fn, bits, val)
#define asic3_set_gpio_b(asic, fn, bits, val) \
	asic3_set_gpio(asic, ASIC3_GPIO_B_Base, ASIC3_GPIO_##fn, bits, val)
#define asic3_set_gpio_c(asic, fn, bits, val) \
	asic3_set_gpio(asic, ASIC3_GPIO_C_Base, ASIC3_GPIO_##fn, bits, val)
#define asic3_set_gpio_d(asic, fn, bits, val) \
	asic3_set_gpio(asic, ASIC3_GPIO_D_Base, ASIC3_GPIO_##fn, bits, val)

#define asic3_set_gpio_banks(asic, fn, bits, pdata, field) 		  \
	do {								  \
	     asic3_set_gpio_a((asic), fn, (bits), (pdata)->gpio_a.field); \
	     asic3_set_gpio_b((asic), fn, (bits), (pdata)->gpio_b.field); \
	     asic3_set_gpio_c((asic), fn, (bits), (pdata)->gpio_c.field); \
	     asic3_set_gpio_d((asic), fn, (bits), (pdata)->gpio_d.field); \
	} while (0)

int asic3_gpio_get_value(struct asic3 *asic, unsigned gpio)
{
	u32 mask = ASIC3_GPIO_bit(gpio);

	switch (gpio >> 4) {
	case ASIC3_GPIO_BANK_A:
		return asic3_get_gpio_a(asic, Status) & mask;
	case ASIC3_GPIO_BANK_B:
		return asic3_get_gpio_b(asic, Status) & mask;
	case ASIC3_GPIO_BANK_C:
		return asic3_get_gpio_c(asic, Status) & mask;
	case ASIC3_GPIO_BANK_D:
		return asic3_get_gpio_d(asic, Status) & mask;
	default:
		printk(KERN_ERR "%s: invalid GPIO value 0x%x",
		       __func__, gpio);
		return -EINVAL;
	}
}
EXPORT_SYMBOL(asic3_gpio_get_value);

void asic3_gpio_set_value(struct asic3 *asic, unsigned gpio, int val)
{
	u32 mask = ASIC3_GPIO_bit(gpio);
	u32 bitval = 0;
	if (val)
		bitval = mask;

	switch (gpio >> 4) {
	case ASIC3_GPIO_BANK_A:
		asic3_set_gpio_a(asic, Out, mask, bitval);
		return;
	case ASIC3_GPIO_BANK_B:
		asic3_set_gpio_b(asic, Out, mask, bitval);
		return;
	case ASIC3_GPIO_BANK_C:
		asic3_set_gpio_c(asic, Out, mask, bitval);
		return;
	case ASIC3_GPIO_BANK_D:
		asic3_set_gpio_d(asic, Out, mask, bitval);
		return;
	default:
		printk(KERN_ERR "%s: invalid GPIO value 0x%x",
		       __func__, gpio);
		return;
	}
}
EXPORT_SYMBOL(asic3_gpio_set_value);

static int asic3_gpio_probe(struct platform_device *pdev)
{
	struct asic3_platform_data *pdata = pdev->dev.platform_data;
	struct asic3 *asic = platform_get_drvdata(pdev);

	asic3_write_register(asic, ASIC3_GPIO_OFFSET(A, Mask), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(B, Mask), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(C, Mask), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(D, Mask), 0xffff);

	asic3_set_gpio_a(asic, SleepMask, 0xffff, 0xffff);
	asic3_set_gpio_b(asic, SleepMask, 0xffff, 0xffff);
	asic3_set_gpio_c(asic, SleepMask, 0xffff, 0xffff);
	asic3_set_gpio_d(asic, SleepMask, 0xffff, 0xffff);

	if (pdata) {
		asic3_set_gpio_banks(asic, Out, 0xffff, pdata, init);
		asic3_set_gpio_banks(asic, Direction, 0xffff, pdata, dir);
		asic3_set_gpio_banks(asic, SleepMask, 0xffff, pdata,
				     sleep_mask);
		asic3_set_gpio_banks(asic, SleepOut, 0xffff, pdata, sleep_out);
		asic3_set_gpio_banks(asic, BattFaultOut, 0xffff, pdata,
				     batt_fault_out);
		asic3_set_gpio_banks(asic, SleepConf, 0xffff, pdata,
				     sleep_conf);
		asic3_set_gpio_banks(asic, AltFunction, 0xffff, pdata,
				     alt_function);
	}

	return 0;
}

static void asic3_gpio_remove(struct platform_device *pdev)
{
	return;
}


/* Core */
static int asic3_probe(struct platform_device *pdev)
{
	struct asic3_platform_data *pdata = pdev->dev.platform_data;
	struct asic3 *asic;
	struct resource *mem;
	unsigned long clksel;
	int ret;

	asic = kzalloc(sizeof(struct asic3), GFP_KERNEL);
	if (!asic)
		return -ENOMEM;

	spin_lock_init(&asic->lock);
	platform_set_drvdata(pdev, asic);
	asic->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		ret = -ENOMEM;
		printk(KERN_ERR "asic3: no MEM resource\n");
		goto err_out_1;
	}

	asic->mapping = ioremap(mem->start, PAGE_SIZE);
	if (!asic->mapping) {
		ret = -ENOMEM;
		printk(KERN_ERR "asic3: couldn't ioremap\n");
		goto err_out_1;
	}

	asic->irq_base = pdata->irq_base;

	if (pdata && pdata->bus_shift)
		asic->bus_shift = 2 - pdata->bus_shift;
	else
		asic->bus_shift = 0;

	clksel = 0;
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), clksel);

	ret = asic3_irq_probe(pdev);
	if (ret < 0) {
		printk(KERN_ERR "asic3: couldn't probe IRQs\n");
		goto err_out_2;
	}
	asic3_gpio_probe(pdev);

	if (pdata->children) {
		int i;
		for (i = 0; i < pdata->n_children; i++) {
			pdata->children[i]->dev.parent = &pdev->dev;
			platform_device_register(pdata->children[i]);
		}
	}

	printk(KERN_INFO "ASIC3 Core driver\n");

	return 0;

 err_out_2:
	iounmap(asic->mapping);
 err_out_1:
	kfree(asic);

	return ret;
}

static int asic3_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);

	asic3_gpio_remove(pdev);
	asic3_irq_remove(pdev);

	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), 0);

	iounmap(asic->mapping);

	kfree(asic);

	return 0;
}

static void asic3_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver asic3_device_driver = {
	.driver		= {
		.name	= "asic3",
	},
	.probe		= asic3_probe,
	.remove		= __devexit_p(asic3_remove),
	.shutdown	= asic3_shutdown,
};

static int __init asic3_init(void)
{
	int retval = 0;
	retval = platform_driver_register(&asic3_device_driver);
	return retval;
}

subsys_initcall(asic3_init);
