// SPDX-License-Identifier: GPL-2.0-or-later
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
 * Contributors: Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/bitops.h>

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
	struct gpio_chip gc;
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

static void grgpio_set_imask(struct grgpio_priv *priv, unsigned int offset,
			     int val)
{
	struct gpio_chip *gc = &priv->gc;

	if (val)
		priv->imask |= BIT(offset);
	else
		priv->imask &= ~BIT(offset);
	gc->write_reg(priv->regs + GRGPIO_IMASK, priv->imask);
}

static int grgpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct grgpio_priv *priv = gpiochip_get_data(gc);

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

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

	ipol = priv->gc.read_reg(priv->regs + GRGPIO_IPOL) & ~mask;
	iedge = priv->gc.read_reg(priv->regs + GRGPIO_IEDGE) & ~mask;

	priv->gc.write_reg(priv->regs + GRGPIO_IPOL, ipol | pol);
	priv->gc.write_reg(priv->regs + GRGPIO_IEDGE, iedge | edge);

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);

	return 0;
}

static void grgpio_irq_mask(struct irq_data *d)
{
	struct grgpio_priv *priv = irq_data_get_irq_chip_data(d);
	int offset = d->hwirq;
	unsigned long flags;

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

	grgpio_set_imask(priv, offset, 0);

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);
}

static void grgpio_irq_unmask(struct irq_data *d)
{
	struct grgpio_priv *priv = irq_data_get_irq_chip_data(d);
	int offset = d->hwirq;
	unsigned long flags;

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

	grgpio_set_imask(priv, offset, 1);

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);
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
	int ngpio = priv->gc.ngpio;
	unsigned long flags;
	int i;
	int match = 0;

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

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

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);

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

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

	/* Request underlying irq if not already requested */
	lirq->irq = irq;
	uirq = &priv->uirqs[lirq->index];
	if (uirq->refcnt == 0) {
		raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);
		ret = request_irq(uirq->uirq, grgpio_irq_handler, 0,
				  dev_name(priv->dev), priv);
		if (ret) {
			dev_err(priv->dev,
				"Could not request underlying irq %d\n",
				uirq->uirq);
			return ret;
		}
		raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);
	}
	uirq->refcnt++;

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);

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
	int ngpio = priv->gc.ngpio;
	int i;

	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);

	raw_spin_lock_irqsave(&priv->gc.bgpio_lock, flags);

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
		if (uirq->refcnt == 0) {
			raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);
			free_irq(uirq->uirq, priv);
			return;
		}
	}

	raw_spin_unlock_irqrestore(&priv->gc.bgpio_lock, flags);
}

static const struct irq_domain_ops grgpio_irq_domain_ops = {
	.map	= grgpio_irq_map,
	.unmap	= grgpio_irq_unmap,
};

/* ------------------------------------------------------------ */

static int grgpio_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct device *dev = &ofdev->dev;
	void  __iomem *regs;
	struct gpio_chip *gc;
	struct grgpio_priv *priv;
	int err;
	u32 prop;
	s32 *irqmap;
	int size;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(ofdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	gc = &priv->gc;
	err = bgpio_init(gc, dev, 4, regs + GRGPIO_DATA,
			 regs + GRGPIO_OUTPUT, NULL, regs + GRGPIO_DIR, NULL,
			 BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (err) {
		dev_err(dev, "bgpio_init() failed\n");
		return err;
	}

	priv->regs = regs;
	priv->imask = gc->read_reg(regs + GRGPIO_IMASK);
	priv->dev = dev;

	gc->owner = THIS_MODULE;
	gc->to_irq = grgpio_to_irq;
	gc->label = devm_kasprintf(dev, GFP_KERNEL, "%pOF", np);
	if (!gc->label)
		return -ENOMEM;

	gc->base = -1;

	err = of_property_read_u32(np, "nbits", &prop);
	if (err || prop <= 0 || prop > GRGPIO_MAX_NGPIO) {
		gc->ngpio = GRGPIO_MAX_NGPIO;
		dev_dbg(dev, "No or invalid nbits property: assume %d\n",
			gc->ngpio);
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
			dev_err(dev,
				"irqmap shorter than ngpio (%d < %d)\n",
				size, gc->ngpio);
			return -EINVAL;
		}

		priv->domain = irq_domain_add_linear(np, gc->ngpio,
						     &grgpio_irq_domain_ops,
						     priv);
		if (!priv->domain) {
			dev_err(dev, "Could not add irq domain\n");
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
				continue;
			}
			priv->uirqs[lirq->index].uirq = ret;
		}
	}

	platform_set_drvdata(ofdev, priv);

	err = gpiochip_add_data(gc, priv);
	if (err) {
		dev_err(dev, "Could not add gpiochip\n");
		if (priv->domain)
			irq_domain_remove(priv->domain);
		return err;
	}

	dev_info(dev, "regs=0x%p, base=%d, ngpio=%d, irqs=%s\n",
		 priv->regs, gc->base, gc->ngpio, priv->domain ? "on" : "off");

	return 0;
}

static void grgpio_remove(struct platform_device *ofdev)
{
	struct grgpio_priv *priv = platform_get_drvdata(ofdev);

	gpiochip_remove(&priv->gc);

	if (priv->domain)
		irq_domain_remove(priv->domain);
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
	.remove_new = grgpio_remove,
};
module_platform_driver(grgpio_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION("Driver for Aeroflex Gaisler GRGPIO");
MODULE_LICENSE("GPL");
