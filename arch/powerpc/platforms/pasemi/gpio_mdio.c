/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Author: Olof Johansson, PA Semi
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on drivers/net/fs_enet/mii-bitbang.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/phy.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>

#define DELAY 1

static void __iomem *gpio_regs;

struct gpio_priv {
	int mdc_pin;
	int mdio_pin;
	int mdio_irqs[PHY_MAX_ADDR];
};

#define MDC_PIN(bus)	(((struct gpio_priv *)bus->priv)->mdc_pin)
#define MDIO_PIN(bus)	(((struct gpio_priv *)bus->priv)->mdio_pin)

static inline void mdio_lo(struct mii_bus *bus)
{
	out_le32(gpio_regs+0x10, 1 << MDIO_PIN(bus));
}

static inline void mdio_hi(struct mii_bus *bus)
{
	out_le32(gpio_regs, 1 << MDIO_PIN(bus));
}

static inline void mdc_lo(struct mii_bus *bus)
{
	out_le32(gpio_regs+0x10, 1 << MDC_PIN(bus));
}

static inline void mdc_hi(struct mii_bus *bus)
{
	out_le32(gpio_regs, 1 << MDC_PIN(bus));
}

static inline void mdio_active(struct mii_bus *bus)
{
	out_le32(gpio_regs+0x20, (1 << MDC_PIN(bus)) | (1 << MDIO_PIN(bus)));
}

static inline void mdio_tristate(struct mii_bus *bus)
{
	out_le32(gpio_regs+0x30, (1 << MDIO_PIN(bus)));
}

static inline int mdio_read(struct mii_bus *bus)
{
	return !!(in_le32(gpio_regs+0x40) & (1 << MDIO_PIN(bus)));
}

static void clock_out(struct mii_bus *bus, int bit)
{
	if (bit)
		mdio_hi(bus);
	else
		mdio_lo(bus);
	udelay(DELAY);
	mdc_hi(bus);
	udelay(DELAY);
	mdc_lo(bus);
}

/* Utility to send the preamble, address, and register (common to read and write). */
static void bitbang_pre(struct mii_bus *bus, int read, u8 addr, u8 reg)
{
	int i;

	/* CFE uses a really long preamble (40 bits). We'll do the same. */
	mdio_active(bus);
	for (i = 0; i < 40; i++) {
		clock_out(bus, 1);
	}

	/* send the start bit (01) and the read opcode (10) or write (10) */
	clock_out(bus, 0);
	clock_out(bus, 1);

	clock_out(bus, read);
	clock_out(bus, !read);

	/* send the PHY address */
	for (i = 0; i < 5; i++) {
		clock_out(bus, (addr & 0x10) != 0);
		addr <<= 1;
	}

	/* send the register address */
	for (i = 0; i < 5; i++) {
		clock_out(bus, (reg & 0x10) != 0);
		reg <<= 1;
	}
}

static int gpio_mdio_read(struct mii_bus *bus, int phy_id, int location)
{
	u16 rdreg;
	int ret, i;
	u8 addr = phy_id & 0xff;
	u8 reg = location & 0xff;

	bitbang_pre(bus, 1, addr, reg);

	/* tri-state our MDIO I/O pin so we can read */
	mdio_tristate(bus);
	udelay(DELAY);
	mdc_hi(bus);
	udelay(DELAY);
	mdc_lo(bus);

	/* read 16 bits of register data, MSB first */
	rdreg = 0;
	for (i = 0; i < 16; i++) {
		mdc_lo(bus);
		udelay(DELAY);
		mdc_hi(bus);
		udelay(DELAY);
		mdc_lo(bus);
		udelay(DELAY);
		rdreg <<= 1;
		rdreg |= mdio_read(bus);
	}

	mdc_hi(bus);
	udelay(DELAY);
	mdc_lo(bus);
	udelay(DELAY);

	ret = rdreg;

	return ret;
}

static int gpio_mdio_write(struct mii_bus *bus, int phy_id, int location, u16 val)
{
	int i;

	u8 addr = phy_id & 0xff;
	u8 reg = location & 0xff;
	u16 value = val & 0xffff;

	bitbang_pre(bus, 0, addr, reg);

	/* send the turnaround (10) */
	mdc_lo(bus);
	mdio_hi(bus);
	udelay(DELAY);
	mdc_hi(bus);
	udelay(DELAY);
	mdc_lo(bus);
	mdio_lo(bus);
	udelay(DELAY);
	mdc_hi(bus);
	udelay(DELAY);

	/* write 16 bits of register data, MSB first */
	for (i = 0; i < 16; i++) {
		mdc_lo(bus);
		if (value & 0x8000)
			mdio_hi(bus);
		else
			mdio_lo(bus);
		udelay(DELAY);
		mdc_hi(bus);
		udelay(DELAY);
		value <<= 1;
	}

	/*
	 * Tri-state the MDIO line.
	 */
	mdio_tristate(bus);
	mdc_lo(bus);
	udelay(DELAY);
	mdc_hi(bus);
	udelay(DELAY);
	return 0;
}

static int gpio_mdio_reset(struct mii_bus *bus)
{
	/*nothing here - dunno how to reset it*/
	return 0;
}


static int __devinit gpio_mdio_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct mii_bus *new_bus;
	struct gpio_priv *priv;
	const unsigned int *prop;
	int err;

	err = -ENOMEM;
	priv = kzalloc(sizeof(struct gpio_priv), GFP_KERNEL);
	if (!priv)
		goto out;

	new_bus = mdiobus_alloc();

	if (!new_bus)
		goto out_free_priv;

	new_bus->name = "pasemi gpio mdio bus";
	new_bus->read = &gpio_mdio_read;
	new_bus->write = &gpio_mdio_write;
	new_bus->reset = &gpio_mdio_reset;

	prop = of_get_property(np, "reg", NULL);
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", *prop);
	new_bus->priv = priv;

	new_bus->irq = priv->mdio_irqs;

	prop = of_get_property(np, "mdc-pin", NULL);
	priv->mdc_pin = *prop;

	prop = of_get_property(np, "mdio-pin", NULL);
	priv->mdio_pin = *prop;

	new_bus->parent = dev;
	dev_set_drvdata(dev, new_bus);

	err = of_mdiobus_register(new_bus, np);

	if (err != 0) {
		printk(KERN_ERR "%s: Cannot register as MDIO bus, err %d\n",
				new_bus->name, err);
		goto out_free_irq;
	}

	return 0;

out_free_irq:
	kfree(new_bus);
out_free_priv:
	kfree(priv);
out:
	return err;
}


static int gpio_mdio_remove(struct platform_device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(&dev->dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(&dev->dev, NULL);

	kfree(bus->priv);
	bus->priv = NULL;
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id gpio_mdio_match[] =
{
	{
		.compatible      = "gpio-mdio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gpio_mdio_match);

static struct platform_driver gpio_mdio_driver =
{
	.probe		= gpio_mdio_probe,
	.remove		= gpio_mdio_remove,
	.driver = {
		.name = "gpio-mdio-bitbang",
		.owner = THIS_MODULE,
		.of_match_table = gpio_mdio_match,
	},
};

int gpio_mdio_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "1682m-gpio");
	if (!np)
		np = of_find_compatible_node(NULL, NULL,
					     "pasemi,pwrficient-gpio");
	if (!np)
		return -ENODEV;
	gpio_regs = of_iomap(np, 0);
	of_node_put(np);

	if (!gpio_regs)
		return -ENODEV;

	return platform_driver_register(&gpio_mdio_driver);
}
module_init(gpio_mdio_init);

void gpio_mdio_exit(void)
{
	platform_driver_unregister(&gpio_mdio_driver);
	if (gpio_regs)
		iounmap(gpio_regs);
}
module_exit(gpio_mdio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("Driver for MDIO over GPIO on PA Semi PWRficient-based boards");
