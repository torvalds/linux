/*
 * Driver for Aeroflex Gaisler GRGPIO General Purpose I/O cores.
 *
 * 2013 (c) Aeroflex Gaisler AB
 *
 * This driver supports the GRGPIO GPIO core available in the GRLIB VHDL
 * IP core library.
 *
 * Full documentation of the GRGPIO core can be found here:
 * http://www.gaisler.com/products/grlib/grip.pdf
 *
 * See "Documentation/devicetree/bindings/gpio/gpio-grgpio.txt" for
 * information on open firmware properties.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Contributors: Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/basic_mmio_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#define GRGPIO_MAX_NGPIO 32

#define GRGPIO_DATA		0x00
#define GRGPIO_OUTPUT		0x04
#define GRGPIO_DIR		0x08
#define GRGPIO_IMASK		0x0c
#define GRGPIO_IPOL		0x10
#define GRGPIO_IEDGE		0x14
#define GRGPIO_BYPASS		0x18
#define GRGPIO_IMAP_BASE	0x20

/* Structure for an irq of the core - called an underlying irq */
struct grgpio_uirq {
	u8 refcnt; /* Reference counter to manage requesting/freeing of uirq */
	u8 uirq; /* Underlying irq of the gpio driver */
};

/*
 * Structure for an irq of a gpio line handed out by this driver. The index is
 * used to map to the corresponding underlying irq.
 */
struct grgpio_lirq {
	s8 index; /* Index into struct grgpio_priv's uirqs, or -1 */
	u8 irq; /* irq for the gpio line */
};

struct grgpio_priv {
	struct bgpio_chip bgc;
	void __iomem *regs;
	struct device *dev;

	u32 imask; /* irq mask shadow register */

	/*
	 * The grgpio core can have multiple "underlying" irqs. The gpio lines
	 * can be mapped to any one or none of these underlying irqs
	 * independently of each other. This driver sets up an irq domain and
	 * hands out separate irqs to each gpio line
	 */
	struct irq_domain *domain;

	/*
	 * This array contains information on each underlying irq, each
	 * irq of the grgpio core itself.
	 */
	struct grgpio_uirq uirqs[GRGPIO_MAX_NGPIO];

	/*
	 * This array contains information for each gpio line on the irqs
	 * obtains from this driver. An index value of -1 for a certain gpio
	 * line indicates that the line has no irq. Otherwise the index connects
	 * the irq to the underlying irq by pointing into the uirqs array.
	 */
	struct grgpio_lirq lirqs[GRGPIO_MAX_NGPIO];
};

static inline struct grgpio_priv *grgpio_gc_to_priv(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return container_of(bgc, struct grgpio_priv, bgc);
}

static void grgpio_set_imask(struct grgpio_priv *priv, unsigned int offset,
			     int val)
{
	struct bgpio_chip *bgc = &priv->bgc;
	unsigned long mask = bgc->pin2mask(bgc, offset);

	if (val)
		priv->imask |= mask;
	else
		priv->imask &= ~mask;
	bgc->write_reg(priv->regs + GRGPIO_IMASK, priv->imask);
}

static int grgpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct grgpio_priv *priv = grgpio_gc_to_priv(gc);

	if (offset >= gc->ngpio)
		return -ENXIO;

	if (priv->lirqs[offset].index < 0)
		return -ENXIO;

	return irq_create_mapping(priv->domain, offset);
}

/* -------------------- IRQ chip functions -------------------- */

static int grgpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct grgpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	u32 mask = BIT(d->hwirq);
	u32 ipol;
	u32 iedge;
	u32 pol;
	u32 edge;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
		pol = 0;
		edge = 0;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		pol = mask;
		edge = 0;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		pol = 0;
		edge = mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		pol = mask;
		edge = mask;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&priv->bgc.lock, flags);

	ipol = priv->bgc.read_reg(priv->regs + GRGPIO_IPOL) & ~mask;
	iedge = priv->bgc.read_reg(priv->regs + GRGPIO_IEDGE) & ~mask;

	priv->bgc.write_reg(priv->regs + GRGPIO_IPOL, ipol | pol);
	priv->bgc.write_reg(priv->regs + GRGPIO_IEDGE, iedge | edge);

	spin_unlock_irqrestore(&priv->bgc.lock, flags);

	return 0;
}

static void grgpio_irq_mask(struct irq_data *d)
{
	struct grgpio_priv *priv = irq_data_get_irq_chip_data(d);
	int offset = d->hwirq;
	unsigned long flags;

	spin_lock_irqsave(&priv->bgc.lock, flags);

	grgpio_set_imask(priv, offset, 0);

	spin_unlock_irqrestore(&priv->bgc.lock, flags);
}

static void grgpio_irq_unmask(struct irq_data *d)
{
	struct grgpio_priv *priv = irq_data_get_irq_chip_data(d);
	int offset = d->hwirq;
	unsigned long flags;

	spin_lock_irqsave(&priv->bgc.lock, flags);

	grgpio_set_imask(priv, offset, 1);

	spin_unlock_irqrestore(&priv->bgc.lock, flags);
}

static struct irq_chip grgpio_irq_chip = {
	.name			= "grgpio",
	.irq_mask		= grgpio_irq_mask,
	.irq_unmask		= grgpio_irq_unmask,
	.irq_set_type		= grgpio_irq_set_type,
};

static irqreturn_t grgpio_irq_handler(int irq, void *dev)
{
	struct grgpio_priv *priv = dev;
	int ngpio = priv->bgc.gc.ngpio;
	unsigned long flags;
	int i;
	int match = 0;

	spin_lock_irqsave(&priv->bgc.lock, flags);

	/*
	 * For each gpio line, call its interrupt handler if it its underlying
	 * irq matches the current irq that is handled.
	 */
	for (i = 0; i < ngpio; i++) {
		struct grgpio_lirq *lirq = &priv->lirqs[i];

		if (priv->imask & BIT(i) && lirq->index >= 0 &&
		    priv->uirqs[lirq->index].uirq == irq) {
			generic_handle_irq(lirq->irq);
			match = 1;
		}
	}

	spin_unlock_irqrestore(&priv->bgc.lock, flags);

	if (!match)
		dev_warn(priv->dev, "No gpio line matched irq %d\n", irq);

	return IRQ_HANDLED;
}

/*
 * This function will be called as a consequence of the call to
 * irq_create_mapping in grgpio_to_irq
 */
static int grgpio_irq_map(struct irq_domain *d, unsigned int irq,
			  irq_hw_number_t hwirq)
{
	struct grgpio_priv *priv = d->host_data;
	struct grgpio_lirq *lirq;
	struct grgpio_uirq *uirq;
	unsigned long flags;
	int offset = hwirq;
	int ret = 0;

	if (!priv)
		return -EINVAL;

	lirq = &priv->lirqs[offset];
	if (lirq->index < 0)
		return -EINVAL;

	dev_dbg(priv->dev, "Mapping irq %d for gpio line %d\n",
		irq, offset);

	spin_lock_irqsave(&priv->bgc.lock, flags);

	/* Request underlying irq if not already requested */
	lirq->irq = irq;
	uirq = &priv->uirqs[lirq->index];
	if (uirq->refcnt == 0) {
		ret = request_irq(uirq->uirq, grgpio_irq_handler, 0,
				  dev_name(priv->dev), priv);
		if (ret) {
			dev_err(priv->dev,
				"Could not request underlying irq %d\n",
				uirq->uirq);

			spin_unlock_irqrestore(&priv->bgc.lock, flags);

			return ret;
		}
	}
	uirq->refcnt++;

	spin_unlock_irqrestore(&priv->bgc.lock, flags);

	/* Setup irq  */
	irq_set_chip_data(irq, priv);
	irq_set_chip_and_handler(irq, &grgpio_irq_chip,
				 handle_simple_irq);
	irq_set_noprobe(irq);

	return ret;
}

static void grgpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	struct grgpio_priv *priv = d->host_data;
	int index;
	struct grgpio_lirq *lirq;
	struct grgpio_uirq *uirq;
	unsigned long flags;
	int ngpio = priv->bgc.gc.ngpio;
	int i;

	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);

	spin_lock_irqsave(&priv->bgc.lock, flags);

	/* Free underlying irq if last user unmapped */
	index = -1;
	for (i = 0; i < ngpio; i++) {
		lirq = &priv->lirqs[i];
		if (lirq->irq == irq) {
			grgpio_set_imask(priv, i, 0);
			lirq->irq = 0;
			index = lirq->index;
			break;
		}
	}
	WARN_ON(index < 0);

	if (index >= 0) {
		uirq = &priv->uirqs[lirq->index];
		uirq->refcnt--;
		if (uirq->refcnt == 0)
			free_irq(uirq->uirq, priv);
	}

	spin_unlock_irqrestore(&priv->bgc.lock, flags);
}

static const struct irq_domain_ops grgpio_irq_domain_ops = {
	.map	= grgpio_irq_map,
	.unmap	= grgpio_irq_unmap,
};

/* ------------------------------------------------------------ */

static int grgpio_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	void  __iomem *regs;
	struct gpio_chip *gc;
	struct bgpio_chip *bgc;
	struct grgpio_priv *priv;
	struct resource *res;
	int err;
	u32 prop;
	s32 *irqmap;
	int size;
	int i;

	priv = devm_kzalloc(&ofdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	bgc = &priv->bgc;
	err = bgpio_init(bgc, &ofdev->dev, 4, regs + GRGPIO_DATA,
			 regs + GRGPIO_OUTPUT, NULL, regs + GRGPIO_DIR, NULL,
			 BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (err) {
		dev_err(&ofdev->dev, "bgpio_init() failed\n");
		return err;
	}

	priv->regs = regs;
	priv->imask = bgc->read_reg(regs + GRGPIO_IMASK);
	priv->dev = &ofdev->dev;

	gc = &bgc->gc;
	gc->of_node = np;
	gc->owner = THIS_MODULE;
	gc->to_irq = grgpio_to_irq;
	gc->label = np->full_name;
	gc->base = -1;

	err = of_property_read_u32(np, "nbits", &prop);
	if (err || prop <= 0 || prop > GRGPIO_MAX_NGPIO) {
		gc->ngpio = GRGPIO_MAX_NGPIO;
		dev_dbg(&ofdev->dev,
			"No or invalid nbits property: assume %d\n", gc->ngpio);
	} else {
		gc->ngpio = prop;
	}

	/*
	 * The irqmap contains the index values indicating which underlying irq,
	 * if anyone, is connected to that line
	 */
	irqmap = (s32 *)of_get_property(np, "irqmap", &size);
	if (irqmap) {
		if (size < gc->ngpio) {
			dev_err(&ofdev->dev,
				"irqmap shorter than ngpio (%d < %d)\n",
				size, gc->ngpio);
			return -EINVAL;
		}

		priv->domain = irq_domain_add_linear(np, gc->ngpio,
						     &grgpio_irq_domain_ops,
						     priv);
		if (!priv->domain) {
			dev_err(&ofdev->dev, "Could not add irq domain\n");
			return -EINVAL;
		}

		for (i = 0; i < gc->ngpio; i++) {
			struct grgpio_lirq *lirq;
			int ret;

			lirq = &priv->lirqs[i];
			lirq->index = irqmap[i];

			if (lirq->index < 0)
				continue;

			ret = platform_get_irq(ofdev, lirq->index);
			if (ret <= 0) {
				/*
				 * Continue without irq functionality for that
				 * gpio line
				 */
				dev_err(priv->dev,
					"Failed to get irq for offset %d\n", i);
				continue;
			}
			priv->uirqs[lirq->index].uirq = ret;
		}
	}

	platform_set_drvdata(ofdev, priv);

	err = gpiochip_add(gc);
	if (err) {
		dev_err(&ofdev->dev, "Could not add gpiochip\n");
		if (priv->domain)
			irq_domain_remove(priv->domain);
		return err;
	}

	dev_info(&ofdev->dev, "regs=0x%p, base=%d, ngpio=%d, irqs=%s\n",
		 priv->regs, gc->base, gc->ngpio, priv->domain ? "on" : "off");

	return 0;
}

static int grgpio_remove(struct platform_device *ofdev)
{
	struct grgpio_priv *priv = platform_get_drvdata(ofdev);
	unsigned long flags;
	int i;
	int ret = 0;

	spin_lock_irqsave(&priv->bgc.lock, flags);

	if (priv->domain) {
		for (i = 0; i < GRGPIO_MAX_NGPIO; i++) {
			if (priv->uirqs[i].refcnt != 0) {
				ret = -EBUSY;
				goto out;
			}
		}
	}

	gpiochip_remove(&priv->bgc.gc);

	if (priv->domain)
		irq_domain_remove(priv->domain);

out:
	spin_unlock_irqrestore(&priv->bgc.lock, flags);

	return ret;
}

static const struct of_device_id grgpio_match[] = {
	{.name = "GAISLER_GPIO"},
	{.name = "01_01a"},
	{},
};

MODULE_DEVICE_TABLE(of, grgpio_match);

static struct platform_driver grgpio_driver = {
	.driver = {
		.name = "grgpio",
		.of_match_table = grgpio_match,
	},
	.probe = grgpio_probe,
	.remove = grgpio_remove,
};
module_platform_driver(grgpio_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION("Driver for Aeroflex Gaisler GRGPIO");
MODULE_LICENSE("GPL");
