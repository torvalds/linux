/*
 * MXC GPIO support. (c) 2008 Daniel Mack <daniel@caiaq.de>
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * Based on code from Freescale,
 * Copyright (C) 2004-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/basic_mmio_gpio.h>
#include <mach/hardware.h>
#include <asm-generic/bug.h>

struct mxc_gpio_port {
	struct list_head node;
	void __iomem *base;
	int irq;
	int irq_high;
	int virtual_irq_start;
	struct bgpio_chip bgc;
	u32 both_edges;
};

/*
 * MX2 has one interrupt *for all* gpio ports. The list is used
 * to save the references to all ports, so that mx2_gpio_irq_handler
 * can walk through all interrupt status registers.
 */
static LIST_HEAD(mxc_gpio_ports);

#define cpu_is_mx1_mx2()	(cpu_is_mx1() || cpu_is_mx2())

#define GPIO_DR		(cpu_is_mx1_mx2() ? 0x1c : 0x00)
#define GPIO_GDIR	(cpu_is_mx1_mx2() ? 0x00 : 0x04)
#define GPIO_PSR	(cpu_is_mx1_mx2() ? 0x24 : 0x08)
#define GPIO_ICR1	(cpu_is_mx1_mx2() ? 0x28 : 0x0C)
#define GPIO_ICR2	(cpu_is_mx1_mx2() ? 0x2C : 0x10)
#define GPIO_IMR	(cpu_is_mx1_mx2() ? 0x30 : 0x14)
#define GPIO_ISR	(cpu_is_mx1_mx2() ? 0x34 : 0x18)

#define GPIO_INT_LOW_LEV	(cpu_is_mx1_mx2() ? 0x3 : 0x0)
#define GPIO_INT_HIGH_LEV	(cpu_is_mx1_mx2() ? 0x2 : 0x1)
#define GPIO_INT_RISE_EDGE	(cpu_is_mx1_mx2() ? 0x0 : 0x2)
#define GPIO_INT_FALL_EDGE	(cpu_is_mx1_mx2() ? 0x1 : 0x3)
#define GPIO_INT_NONE		0x4

/* Note: This driver assumes 32 GPIOs are handled in one register */

static int gpio_set_irq_type(struct irq_data *d, u32 type)
{
	u32 gpio = irq_to_gpio(d->irq);
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mxc_gpio_port *port = gc->private;
	u32 bit, val;
	int edge;
	void __iomem *reg = port->base;

	port->both_edges &= ~(1 << (gpio & 31));
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		edge = GPIO_INT_RISE_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		edge = GPIO_INT_FALL_EDGE;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val = gpio_get_value(gpio);
		if (val) {
			edge = GPIO_INT_LOW_LEV;
			pr_debug("mxc: set GPIO %d to low trigger\n", gpio);
		} else {
			edge = GPIO_INT_HIGH_LEV;
			pr_debug("mxc: set GPIO %d to high trigger\n", gpio);
		}
		port->both_edges |= 1 << (gpio & 31);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		edge = GPIO_INT_LOW_LEV;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		edge = GPIO_INT_HIGH_LEV;
		break;
	default:
		return -EINVAL;
	}

	reg += GPIO_ICR1 + ((gpio & 0x10) >> 2); /* lower or upper register */
	bit = gpio & 0xf;
	val = readl(reg) & ~(0x3 << (bit << 1));
	writel(val | (edge << (bit << 1)), reg);
	writel(1 << (gpio & 0x1f), port->base + GPIO_ISR);

	return 0;
}

static void mxc_flip_edge(struct mxc_gpio_port *port, u32 gpio)
{
	void __iomem *reg = port->base;
	u32 bit, val;
	int edge;

	reg += GPIO_ICR1 + ((gpio & 0x10) >> 2); /* lower or upper register */
	bit = gpio & 0xf;
	val = readl(reg);
	edge = (val >> (bit << 1)) & 3;
	val &= ~(0x3 << (bit << 1));
	if (edge == GPIO_INT_HIGH_LEV) {
		edge = GPIO_INT_LOW_LEV;
		pr_debug("mxc: switch GPIO %d to low trigger\n", gpio);
	} else if (edge == GPIO_INT_LOW_LEV) {
		edge = GPIO_INT_HIGH_LEV;
		pr_debug("mxc: switch GPIO %d to high trigger\n", gpio);
	} else {
		pr_err("mxc: invalid configuration for GPIO %d: %x\n",
		       gpio, edge);
		return;
	}
	writel(val | (edge << (bit << 1)), reg);
}

/* handle 32 interrupts in one status register */
static void mxc_gpio_irq_handler(struct mxc_gpio_port *port, u32 irq_stat)
{
	u32 gpio_irq_no_base = port->virtual_irq_start;

	while (irq_stat != 0) {
		int irqoffset = fls(irq_stat) - 1;

		if (port->both_edges & (1 << irqoffset))
			mxc_flip_edge(port, irqoffset);

		generic_handle_irq(gpio_irq_no_base + irqoffset);

		irq_stat &= ~(1 << irqoffset);
	}
}

/* MX1 and MX3 has one interrupt *per* gpio port */
static void mx3_gpio_irq_handler(u32 irq, struct irq_desc *desc)
{
	u32 irq_stat;
	struct mxc_gpio_port *port = irq_get_handler_data(irq);

	irq_stat = readl(port->base + GPIO_ISR) & readl(port->base + GPIO_IMR);

	mxc_gpio_irq_handler(port, irq_stat);
}

/* MX2 has one interrupt *for all* gpio ports */
static void mx2_gpio_irq_handler(u32 irq, struct irq_desc *desc)
{
	u32 irq_msk, irq_stat;
	struct mxc_gpio_port *port;

	/* walk through all interrupt status registers */
	list_for_each_entry(port, &mxc_gpio_ports, node) {
		irq_msk = readl(port->base + GPIO_IMR);
		if (!irq_msk)
			continue;

		irq_stat = readl(port->base + GPIO_ISR) & irq_msk;
		if (irq_stat)
			mxc_gpio_irq_handler(port, irq_stat);
	}
}

/*
 * Set interrupt number "irq" in the GPIO as a wake-up source.
 * While system is running, all registered GPIO interrupts need to have
 * wake-up enabled. When system is suspended, only selected GPIO interrupts
 * need to have wake-up enabled.
 * @param  irq          interrupt source number
 * @param  enable       enable as wake-up if equal to non-zero
 * @return       This function returns 0 on success.
 */
static int gpio_set_wake_irq(struct irq_data *d, u32 enable)
{
	u32 gpio = irq_to_gpio(d->irq);
	u32 gpio_idx = gpio & 0x1F;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mxc_gpio_port *port = gc->private;

	if (enable) {
		if (port->irq_high && (gpio_idx >= 16))
			enable_irq_wake(port->irq_high);
		else
			enable_irq_wake(port->irq);
	} else {
		if (port->irq_high && (gpio_idx >= 16))
			disable_irq_wake(port->irq_high);
		else
			disable_irq_wake(port->irq);
	}

	return 0;
}

static void __init mxc_gpio_init_gc(struct mxc_gpio_port *port)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("gpio-mxc", 1, port->virtual_irq_start,
				    port->base, handle_level_irq);
	gc->private = port;

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack,
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_set_type = gpio_set_irq_type;
	ct->chip.irq_set_wake = gpio_set_wake_irq,
	ct->regs.ack = GPIO_ISR;
	ct->regs.mask = GPIO_IMR;

	irq_setup_generic_chip(gc, IRQ_MSK(32), IRQ_GC_INIT_NESTED_LOCK,
			       IRQ_NOREQUEST, 0);
}

static int __devinit mxc_gpio_probe(struct platform_device *pdev)
{
	struct mxc_gpio_port *port;
	struct resource *iores;
	int err;

	port = kzalloc(sizeof(struct mxc_gpio_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->virtual_irq_start = MXC_GPIO_IRQ_START + pdev->id * 32;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores) {
		err = -ENODEV;
		goto out_kfree;
	}

	if (!request_mem_region(iores->start, resource_size(iores),
				pdev->name)) {
		err = -EBUSY;
		goto out_kfree;
	}

	port->base = ioremap(iores->start, resource_size(iores));
	if (!port->base) {
		err = -ENOMEM;
		goto out_release_mem;
	}

	port->irq_high = platform_get_irq(pdev, 1);
	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0) {
		err = -EINVAL;
		goto out_iounmap;
	}

	/* disable the interrupt and clear the status */
	writel(0, port->base + GPIO_IMR);
	writel(~0, port->base + GPIO_ISR);

	/* gpio-mxc can be a generic irq chip */
	mxc_gpio_init_gc(port);

	if (cpu_is_mx2()) {
		/* setup one handler for all GPIO interrupts */
		if (pdev->id == 0)
			irq_set_chained_handler(port->irq,
						mx2_gpio_irq_handler);
	} else {
		/* setup one handler for each entry */
		irq_set_chained_handler(port->irq, mx3_gpio_irq_handler);
		irq_set_handler_data(port->irq, port);
		if (port->irq_high > 0) {
			/* setup handler for GPIO 16 to 31 */
			irq_set_chained_handler(port->irq_high,
						mx3_gpio_irq_handler);
			irq_set_handler_data(port->irq_high, port);
		}
	}

	err = bgpio_init(&port->bgc, &pdev->dev, 4,
			 port->base + GPIO_PSR,
			 port->base + GPIO_DR, NULL,
			 port->base + GPIO_GDIR, NULL, false);
	if (err)
		goto out_iounmap;

	port->bgc.gc.base = pdev->id * 32;
	port->bgc.dir = port->bgc.read_reg(port->bgc.reg_dir);
	port->bgc.data = port->bgc.read_reg(port->bgc.reg_set);

	err = gpiochip_add(&port->bgc.gc);
	if (err)
		goto out_bgpio_remove;

	list_add_tail(&port->node, &mxc_gpio_ports);

	return 0;

out_bgpio_remove:
	bgpio_remove(&port->bgc);
out_iounmap:
	iounmap(port->base);
out_release_mem:
	release_mem_region(iores->start, resource_size(iores));
out_kfree:
	kfree(port);
	dev_info(&pdev->dev, "%s failed with errno %d\n", __func__, err);
	return err;
}

static struct platform_driver mxc_gpio_driver = {
	.driver		= {
		.name	= "gpio-mxc",
		.owner	= THIS_MODULE,
	},
	.probe		= mxc_gpio_probe,
};

static int __init gpio_mxc_init(void)
{
	return platform_driver_register(&mxc_gpio_driver);
}
postcore_initcall(gpio_mxc_init);

MODULE_AUTHOR("Freescale Semiconductor, "
	      "Daniel Mack <danielncaiaq.de>, "
	      "Juergen Beisert <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Freescale MXC GPIO");
MODULE_LICENSE("GPL");
