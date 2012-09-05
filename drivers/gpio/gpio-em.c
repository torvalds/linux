/*
 * Emma Mobile GPIO Support - GIO
 *
 *  Copyright (C) 2012 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_data/gpio-em.h>

struct em_gio_priv {
	void __iomem *base0;
	void __iomem *base1;
	unsigned int irq_base;
	spinlock_t sense_lock;
	struct platform_device *pdev;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
	struct irq_domain *irq_domain;
};

#define GIO_E1 0x00
#define GIO_E0 0x04
#define GIO_EM 0x04
#define GIO_OL 0x08
#define GIO_OH 0x0c
#define GIO_I 0x10
#define GIO_IIA 0x14
#define GIO_IEN 0x18
#define GIO_IDS 0x1c
#define GIO_IIM 0x1c
#define GIO_RAW 0x20
#define GIO_MST 0x24
#define GIO_IIR 0x28

#define GIO_IDT0 0x40
#define GIO_IDT1 0x44
#define GIO_IDT2 0x48
#define GIO_IDT3 0x4c
#define GIO_RAWBL 0x50
#define GIO_RAWBH 0x54
#define GIO_IRBL 0x58
#define GIO_IRBH 0x5c

#define GIO_IDT(n) (GIO_IDT0 + ((n) * 4))

static inline unsigned long em_gio_read(struct em_gio_priv *p, int offs)
{
	if (offs < GIO_IDT0)
		return ioread32(p->base0 + offs);
	else
		return ioread32(p->base1 + (offs - GIO_IDT0));
}

static inline void em_gio_write(struct em_gio_priv *p, int offs,
				unsigned long value)
{
	if (offs < GIO_IDT0)
		iowrite32(value, p->base0 + offs);
	else
		iowrite32(value, p->base1 + (offs - GIO_IDT0));
}

static inline struct em_gio_priv *irq_to_priv(struct irq_data *d)
{
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	return container_of(chip, struct em_gio_priv, irq_chip);
}

static void em_gio_irq_disable(struct irq_data *d)
{
	struct em_gio_priv *p = irq_to_priv(d);

	em_gio_write(p, GIO_IDS, BIT(irqd_to_hwirq(d)));
}

static void em_gio_irq_enable(struct irq_data *d)
{
	struct em_gio_priv *p = irq_to_priv(d);

	em_gio_write(p, GIO_IEN, BIT(irqd_to_hwirq(d)));
}

#define GIO_ASYNC(x) (x + 8)

static unsigned char em_gio_sense_table[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_EDGE_RISING] = GIO_ASYNC(0x00),
	[IRQ_TYPE_EDGE_FALLING] = GIO_ASYNC(0x01),
	[IRQ_TYPE_LEVEL_HIGH] = GIO_ASYNC(0x02),
	[IRQ_TYPE_LEVEL_LOW] = GIO_ASYNC(0x03),
	[IRQ_TYPE_EDGE_BOTH] = GIO_ASYNC(0x04),
};

static int em_gio_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned char value = em_gio_sense_table[type & IRQ_TYPE_SENSE_MASK];
	struct em_gio_priv *p = irq_to_priv(d);
	unsigned int reg, offset, shift;
	unsigned long flags;
	unsigned long tmp;

	if (!value)
		return -EINVAL;

	offset = irqd_to_hwirq(d);

	pr_debug("gio: sense irq = %d, mode = %d\n", offset, value);

	/* 8 x 4 bit fields in 4 IDT registers */
	reg = GIO_IDT(offset >> 3);
	shift = (offset & 0x07) << 4;

	spin_lock_irqsave(&p->sense_lock, flags);

	/* disable the interrupt in IIA */
	tmp = em_gio_read(p, GIO_IIA);
	tmp &= ~BIT(offset);
	em_gio_write(p, GIO_IIA, tmp);

	/* change the sense setting in IDT */
	tmp = em_gio_read(p, reg);
	tmp &= ~(0xf << shift);
	tmp |= value << shift;
	em_gio_write(p, reg, tmp);

	/* clear pending interrupts */
	em_gio_write(p, GIO_IIR, BIT(offset));

	/* enable the interrupt in IIA */
	tmp = em_gio_read(p, GIO_IIA);
	tmp |= BIT(offset);
	em_gio_write(p, GIO_IIA, tmp);

	spin_unlock_irqrestore(&p->sense_lock, flags);

	return 0;
}

static irqreturn_t em_gio_irq_handler(int irq, void *dev_id)
{
	struct em_gio_priv *p = dev_id;
	unsigned long pending;
	unsigned int offset, irqs_handled = 0;

	while ((pending = em_gio_read(p, GIO_MST))) {
		offset = __ffs(pending);
		em_gio_write(p, GIO_IIR, BIT(offset));
		generic_handle_irq(irq_find_mapping(p->irq_domain, offset));
		irqs_handled++;
	}

	return irqs_handled ? IRQ_HANDLED : IRQ_NONE;
}

static inline struct em_gio_priv *gpio_to_priv(struct gpio_chip *chip)
{
	return container_of(chip, struct em_gio_priv, gpio_chip);
}

static int em_gio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	em_gio_write(gpio_to_priv(chip), GIO_E0, BIT(offset));
	return 0;
}

static int em_gio_get(struct gpio_chip *chip, unsigned offset)
{
	return (int)(em_gio_read(gpio_to_priv(chip), GIO_I) & BIT(offset));
}

static void __em_gio_set(struct gpio_chip *chip, unsigned int reg,
			 unsigned shift, int value)
{
	/* upper 16 bits contains mask and lower 16 actual value */
	em_gio_write(gpio_to_priv(chip), reg,
		     (1 << (shift + 16)) | (value << shift));
}

static void em_gio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	/* output is split into two registers */
	if (offset < 16)
		__em_gio_set(chip, GIO_OL, offset, value);
	else
		__em_gio_set(chip, GIO_OH, offset - 16, value);
}

static int em_gio_direction_output(struct gpio_chip *chip, unsigned offset,
				   int value)
{
	/* write GPIO value to output before selecting output mode of pin */
	em_gio_set(chip, offset, value);
	em_gio_write(gpio_to_priv(chip), GIO_E1, BIT(offset));
	return 0;
}

static int em_gio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_find_mapping(gpio_to_priv(chip)->irq_domain, offset);
}

static int em_gio_irq_domain_map(struct irq_domain *h, unsigned int virq,
				 irq_hw_number_t hw)
{
	struct em_gio_priv *p = h->host_data;

	pr_debug("gio: map hw irq = %d, virq = %d\n", (int)hw, virq);

	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &p->irq_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID); /* kill me now */
	return 0;
}

static struct irq_domain_ops em_gio_irq_domain_ops = {
	.map	= em_gio_irq_domain_map,
};

static int __devinit em_gio_irq_domain_init(struct em_gio_priv *p)
{
	struct platform_device *pdev = p->pdev;
	struct gpio_em_config *pdata = pdev->dev.platform_data;

	p->irq_base = irq_alloc_descs(pdata->irq_base, 0,
				      pdata->number_of_pins, numa_node_id());
	if (IS_ERR_VALUE(p->irq_base)) {
		dev_err(&pdev->dev, "cannot get irq_desc\n");
		return -ENXIO;
	}
	pr_debug("gio: hw base = %d, nr = %d, sw base = %d\n",
		 pdata->gpio_base, pdata->number_of_pins, p->irq_base);

	p->irq_domain = irq_domain_add_legacy(pdev->dev.of_node,
					      pdata->number_of_pins,
					      p->irq_base, 0,
					      &em_gio_irq_domain_ops, p);
	if (!p->irq_domain) {
		irq_free_descs(p->irq_base, pdata->number_of_pins);
		return -ENXIO;
	}

	return 0;
}

static void em_gio_irq_domain_cleanup(struct em_gio_priv *p)
{
	struct gpio_em_config *pdata = p->pdev->dev.platform_data;

	irq_free_descs(p->irq_base, pdata->number_of_pins);
	/* FIXME: irq domain wants to be freed! */
}

static int __devinit em_gio_probe(struct platform_device *pdev)
{
	struct gpio_em_config *pdata = pdev->dev.platform_data;
	struct em_gio_priv *p;
	struct resource *io[2], *irq[2];
	struct gpio_chip *gpio_chip;
	struct irq_chip *irq_chip;
	const char *name = dev_name(&pdev->dev);
	int ret;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto err0;
	}

	p->pdev = pdev;
	platform_set_drvdata(pdev, p);
	spin_lock_init(&p->sense_lock);

	io[0] = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io[1] = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	irq[0] = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	irq[1] = platform_get_resource(pdev, IORESOURCE_IRQ, 1);

	if (!io[0] || !io[1] || !irq[0] || !irq[1] || !pdata) {
		dev_err(&pdev->dev, "missing IRQ, IOMEM or configuration\n");
		ret = -EINVAL;
		goto err1;
	}

	p->base0 = ioremap_nocache(io[0]->start, resource_size(io[0]));
	if (!p->base0) {
		dev_err(&pdev->dev, "failed to remap low I/O memory\n");
		ret = -ENXIO;
		goto err1;
	}

	p->base1 = ioremap_nocache(io[1]->start, resource_size(io[1]));
	if (!p->base1) {
		dev_err(&pdev->dev, "failed to remap high I/O memory\n");
		ret = -ENXIO;
		goto err2;
	}

	gpio_chip = &p->gpio_chip;
	gpio_chip->direction_input = em_gio_direction_input;
	gpio_chip->get = em_gio_get;
	gpio_chip->direction_output = em_gio_direction_output;
	gpio_chip->set = em_gio_set;
	gpio_chip->to_irq = em_gio_to_irq;
	gpio_chip->label = name;
	gpio_chip->owner = THIS_MODULE;
	gpio_chip->base = pdata->gpio_base;
	gpio_chip->ngpio = pdata->number_of_pins;

	irq_chip = &p->irq_chip;
	irq_chip->name = name;
	irq_chip->irq_mask = em_gio_irq_disable;
	irq_chip->irq_unmask = em_gio_irq_enable;
	irq_chip->irq_enable = em_gio_irq_enable;
	irq_chip->irq_disable = em_gio_irq_disable;
	irq_chip->irq_set_type = em_gio_irq_set_type;
	irq_chip->flags	= IRQCHIP_SKIP_SET_WAKE;

	ret = em_gio_irq_domain_init(p);
	if (ret) {
		dev_err(&pdev->dev, "cannot initialize irq domain\n");
		goto err3;
	}

	if (request_irq(irq[0]->start, em_gio_irq_handler, 0, name, p)) {
		dev_err(&pdev->dev, "failed to request low IRQ\n");
		ret = -ENOENT;
		goto err4;
	}

	if (request_irq(irq[1]->start, em_gio_irq_handler, 0, name, p)) {
		dev_err(&pdev->dev, "failed to request high IRQ\n");
		ret = -ENOENT;
		goto err5;
	}

	ret = gpiochip_add(gpio_chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to add GPIO controller\n");
		goto err6;
	}
	return 0;

err6:
	free_irq(irq[1]->start, pdev);
err5:
	free_irq(irq[0]->start, pdev);
err4:
	em_gio_irq_domain_cleanup(p);
err3:
	iounmap(p->base1);
err2:
	iounmap(p->base0);
err1:
	kfree(p);
err0:
	return ret;
}

static int __devexit em_gio_remove(struct platform_device *pdev)
{
	struct em_gio_priv *p = platform_get_drvdata(pdev);
	struct resource *irq[2];
	int ret;

	ret = gpiochip_remove(&p->gpio_chip);
	if (ret)
		return ret;

	irq[0] = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	irq[1] = platform_get_resource(pdev, IORESOURCE_IRQ, 1);

	free_irq(irq[1]->start, pdev);
	free_irq(irq[0]->start, pdev);
	em_gio_irq_domain_cleanup(p);
	iounmap(p->base1);
	iounmap(p->base0);
	kfree(p);
	return 0;
}

static struct platform_driver em_gio_device_driver = {
	.probe		= em_gio_probe,
	.remove		= __devexit_p(em_gio_remove),
	.driver		= {
		.name	= "em_gio",
	}
};

module_platform_driver(em_gio_device_driver);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Renesas Emma Mobile GIO Driver");
MODULE_LICENSE("GPL v2");
