/*
 * Support for the GPIO/IRQ expander chips present on several HTC phones.
 * These are implemented in CPLD chips present on the board.
 *
 * Copyright (c) 2007 Kevin O'Connor <kevin@koconnor.net>
 * Copyright (c) 2007 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * This file may be distributed under the terms of the GNU GPL license.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mfd/htc-egpio.h>

struct egpio_chip {
	int              reg_start;
	int              cached_values;
	unsigned long    is_out;
	struct device    *dev;
	struct gpio_chip chip;
};

struct egpio_info {
	spinlock_t        lock;

	/* iomem info */
	void __iomem      *base_addr;
	int               bus_shift;	/* byte shift */
	int               reg_shift;	/* bit shift */
	int               reg_mask;

	/* irq info */
	int               ack_register;
	int               ack_write;
	u16               irqs_enabled;
	uint              irq_start;
	int               nirqs;
	uint              chained_irq;

	/* egpio info */
	struct egpio_chip *chip;
	int               nchips;
};

static inline void egpio_writew(u16 value, struct egpio_info *ei, int reg)
{
	writew(value, ei->base_addr + (reg << ei->bus_shift));
}

static inline u16 egpio_readw(struct egpio_info *ei, int reg)
{
	return readw(ei->base_addr + (reg << ei->bus_shift));
}

/*
 * IRQs
 */

static inline void ack_irqs(struct egpio_info *ei)
{
	egpio_writew(ei->ack_write, ei, ei->ack_register);
	pr_debug("EGPIO ack - write %x to base+%x\n",
			ei->ack_write, ei->ack_register << ei->bus_shift);
}

static void egpio_ack(unsigned int irq)
{
}

/* There does not appear to be a way to proactively mask interrupts
 * on the egpio chip itself.  So, we simply ignore interrupts that
 * aren't desired. */
static void egpio_mask(unsigned int irq)
{
	struct egpio_info *ei = get_irq_chip_data(irq);
	ei->irqs_enabled &= ~(1 << (irq - ei->irq_start));
	pr_debug("EGPIO mask %d %04x\n", irq, ei->irqs_enabled);
}
static void egpio_unmask(unsigned int irq)
{
	struct egpio_info *ei = get_irq_chip_data(irq);
	ei->irqs_enabled |= 1 << (irq - ei->irq_start);
	pr_debug("EGPIO unmask %d %04x\n", irq, ei->irqs_enabled);
}

static struct irq_chip egpio_muxed_chip = {
	.name   = "htc-egpio",
	.ack	= egpio_ack,
	.mask   = egpio_mask,
	.unmask = egpio_unmask,
};

static void egpio_handler(unsigned int irq, struct irq_desc *desc)
{
	struct egpio_info *ei = get_irq_data(irq);
	int irqpin;

	/* Read current pins. */
	unsigned long readval = egpio_readw(ei, ei->ack_register);
	pr_debug("IRQ reg: %x\n", (unsigned int)readval);
	/* Ack/unmask interrupts. */
	ack_irqs(ei);
	/* Process all set pins. */
	readval &= ei->irqs_enabled;
	for_each_bit(irqpin, &readval, ei->nirqs) {
		/* Run irq handler */
		pr_debug("got IRQ %d\n", irqpin);
		irq = ei->irq_start + irqpin;
		desc = &irq_desc[irq];
		desc->handle_irq(irq, desc);
	}
}

int htc_egpio_get_wakeup_irq(struct device *dev)
{
	struct egpio_info *ei = dev_get_drvdata(dev);

	/* Read current pins. */
	u16 readval = egpio_readw(ei, ei->ack_register);
	/* Ack/unmask interrupts. */
	ack_irqs(ei);
	/* Return first set pin. */
	readval &= ei->irqs_enabled;
	return ei->irq_start + ffs(readval) - 1;
}
EXPORT_SYMBOL(htc_egpio_get_wakeup_irq);

static inline int egpio_pos(struct egpio_info *ei, int bit)
{
	return bit >> ei->reg_shift;
}

static inline int egpio_bit(struct egpio_info *ei, int bit)
{
	return 1 << (bit & ((1 << ei->reg_shift)-1));
}

/*
 * Input pins
 */

static int egpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct egpio_chip *egpio;
	struct egpio_info *ei;
	unsigned           bit;
	int                reg;
	int                value;

	pr_debug("egpio_get_value(%d)\n", chip->base + offset);

	egpio = container_of(chip, struct egpio_chip, chip);
	ei    = dev_get_drvdata(egpio->dev);
	bit   = egpio_bit(ei, offset);
	reg   = egpio->reg_start + egpio_pos(ei, offset);

	value = egpio_readw(ei, reg);
	pr_debug("readw(%p + %x) = %x\n",
			ei->base_addr, reg << ei->bus_shift, value);
	return value & bit;
}

static int egpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct egpio_chip *egpio;

	egpio = container_of(chip, struct egpio_chip, chip);
	return test_bit(offset, &egpio->is_out) ? -EINVAL : 0;
}


/*
 * Output pins
 */

static void egpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned long     flag;
	struct egpio_chip *egpio;
	struct egpio_info *ei;
	unsigned          bit;
	int               pos;
	int               reg;
	int               shift;

	pr_debug("egpio_set(%s, %d(%d), %d)\n",
			chip->label, offset, offset+chip->base, value);

	egpio = container_of(chip, struct egpio_chip, chip);
	ei    = dev_get_drvdata(egpio->dev);
	bit   = egpio_bit(ei, offset);
	pos   = egpio_pos(ei, offset);
	reg   = egpio->reg_start + pos;
	shift = pos << ei->reg_shift;

	pr_debug("egpio %s: reg %d = 0x%04x\n", value ? "set" : "clear",
			reg, (egpio->cached_values >> shift) & ei->reg_mask);

	spin_lock_irqsave(&ei->lock, flag);
	if (value)
		egpio->cached_values |= (1 << offset);
	else
		egpio->cached_values &= ~(1 << offset);
	egpio_writew((egpio->cached_values >> shift) & ei->reg_mask, ei, reg);
	spin_unlock_irqrestore(&ei->lock, flag);
}

static int egpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct egpio_chip *egpio;

	egpio = container_of(chip, struct egpio_chip, chip);
	if (test_bit(offset, &egpio->is_out)) {
		egpio_set(chip, offset, value);
		return 0;
	} else {
		return -EINVAL;
	}
}

static void egpio_write_cache(struct egpio_info *ei)
{
	int               i;
	struct egpio_chip *egpio;
	int               shift;

	for (i = 0; i < ei->nchips; i++) {
		egpio = &(ei->chip[i]);
		if (!egpio->is_out)
			continue;

		for (shift = 0; shift < egpio->chip.ngpio;
				shift += (1<<ei->reg_shift)) {

			int reg = egpio->reg_start + egpio_pos(ei, shift);

			if (!((egpio->is_out >> shift) & ei->reg_mask))
				continue;

			pr_debug("EGPIO: setting %x to %x, was %x\n", reg,
				(egpio->cached_values >> shift) & ei->reg_mask,
				egpio_readw(ei, reg));

			egpio_writew((egpio->cached_values >> shift)
					& ei->reg_mask, ei, reg);
		}
	}
}


/*
 * Setup
 */

static int __init egpio_probe(struct platform_device *pdev)
{
	struct htc_egpio_platform_data *pdata = pdev->dev.platform_data;
	struct resource   *res;
	struct egpio_info *ei;
	struct gpio_chip  *chip;
	unsigned int      irq, irq_end;
	int               i;
	int               ret;

	/* Initialize ei data structure. */
	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		return -ENOMEM;

	spin_lock_init(&ei->lock);

	/* Find chained irq */
	ret = -EINVAL;
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res)
		ei->chained_irq = res->start;

	/* Map egpio chip into virtual address space. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		goto fail;
	ei->base_addr = ioremap_nocache(res->start, res->end - res->start);
	if (!ei->base_addr)
		goto fail;
	pr_debug("EGPIO phys=%08x virt=%p\n", res->start, ei->base_addr);

	if ((pdata->bus_width != 16) && (pdata->bus_width != 32))
		goto fail;
	ei->bus_shift = fls(pdata->bus_width - 1) - 3;
	pr_debug("bus_shift = %d\n", ei->bus_shift);

	if ((pdata->reg_width != 8) && (pdata->reg_width != 16))
		goto fail;
	ei->reg_shift = fls(pdata->reg_width - 1);
	pr_debug("reg_shift = %d\n", ei->reg_shift);

	ei->reg_mask = (1 << pdata->reg_width) - 1;

	platform_set_drvdata(pdev, ei);

	ei->nchips = pdata->num_chips;
	ei->chip = kzalloc(sizeof(struct egpio_chip) * ei->nchips, GFP_KERNEL);
	if (!ei) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < ei->nchips; i++) {
		ei->chip[i].reg_start = pdata->chip[i].reg_start;
		ei->chip[i].cached_values = pdata->chip[i].initial_values;
		ei->chip[i].is_out = pdata->chip[i].direction;
		ei->chip[i].dev = &(pdev->dev);
		chip = &(ei->chip[i].chip);
		chip->label           = "htc-egpio";
		chip->get             = egpio_get;
		chip->set             = egpio_set;
		chip->direction_input = egpio_direction_input;
		chip->direction_output = egpio_direction_output;
		chip->base            = pdata->chip[i].gpio_base;
		chip->ngpio           = pdata->chip[i].num_gpios;

		gpiochip_add(chip);
	}

	/* Set initial pin values */
	egpio_write_cache(ei);

	ei->irq_start = pdata->irq_base;
	ei->nirqs = pdata->num_irqs;
	ei->ack_register = pdata->ack_register;

	if (ei->chained_irq) {
		/* Setup irq handlers */
		ei->ack_write = 0xFFFF;
		if (pdata->invert_acks)
			ei->ack_write = 0;
		irq_end = ei->irq_start + ei->nirqs;
		for (irq = ei->irq_start; irq < irq_end; irq++) {
			set_irq_chip(irq, &egpio_muxed_chip);
			set_irq_chip_data(irq, ei);
			set_irq_handler(irq, handle_simple_irq);
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		}
		set_irq_type(ei->chained_irq, IRQ_TYPE_EDGE_RISING);
		set_irq_data(ei->chained_irq, ei);
		set_irq_chained_handler(ei->chained_irq, egpio_handler);
		ack_irqs(ei);

		device_init_wakeup(&pdev->dev, 1);
	}

	return 0;

fail:
	printk(KERN_ERR "EGPIO failed to setup\n");
	kfree(ei);
	return ret;
}

static int __exit egpio_remove(struct platform_device *pdev)
{
	struct egpio_info *ei = platform_get_drvdata(pdev);
	unsigned int      irq, irq_end;

	if (ei->chained_irq) {
		irq_end = ei->irq_start + ei->nirqs;
		for (irq = ei->irq_start; irq < irq_end; irq++) {
			set_irq_chip(irq, NULL);
			set_irq_handler(irq, NULL);
			set_irq_flags(irq, 0);
		}
		set_irq_chained_handler(ei->chained_irq, NULL);
		device_init_wakeup(&pdev->dev, 0);
	}
	iounmap(ei->base_addr);
	kfree(ei->chip);
	kfree(ei);

	return 0;
}

#ifdef CONFIG_PM
static int egpio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct egpio_info *ei = platform_get_drvdata(pdev);

	if (ei->chained_irq && device_may_wakeup(&pdev->dev))
		enable_irq_wake(ei->chained_irq);
	return 0;
}

static int egpio_resume(struct platform_device *pdev)
{
	struct egpio_info *ei = platform_get_drvdata(pdev);

	if (ei->chained_irq && device_may_wakeup(&pdev->dev))
		disable_irq_wake(ei->chained_irq);

	/* Update registers from the cache, in case
	   the CPLD was powered off during suspend */
	egpio_write_cache(ei);
	return 0;
}
#else
#define egpio_suspend NULL
#define egpio_resume NULL
#endif


static struct platform_driver egpio_driver = {
	.driver = {
		.name = "htc-egpio",
	},
	.remove       = __exit_p(egpio_remove),
	.suspend      = egpio_suspend,
	.resume       = egpio_resume,
};

static int __init egpio_init(void)
{
	return platform_driver_probe(&egpio_driver, egpio_probe);
}

static void __exit egpio_exit(void)
{
	platform_driver_unregister(&egpio_driver);
}

/* start early for dependencies */
subsys_initcall(egpio_init);
module_exit(egpio_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin O'Connor <kevin@koconnor.net>");
