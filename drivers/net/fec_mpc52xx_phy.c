/*
 * Driver for the MPC5200 Fast Ethernet Controller - MDIO bus driver
 *
 * Copyright (C) 2007  Domen Puncer, Telargo, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/of_platform.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>
#include "fec_mpc52xx.h"

struct mpc52xx_fec_mdio_priv {
	struct mpc52xx_fec __iomem *regs;
};

static int mpc52xx_fec_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct mpc52xx_fec_mdio_priv *priv = bus->priv;
	struct mpc52xx_fec __iomem *fec;
	int tries = 100;
	u32 request = FEC_MII_READ_FRAME;

	fec = priv->regs;
	out_be32(&fec->ievent, FEC_IEVENT_MII);

	request |= (phy_id << FEC_MII_DATA_PA_SHIFT) & FEC_MII_DATA_PA_MSK;
	request |= (reg << FEC_MII_DATA_RA_SHIFT) & FEC_MII_DATA_RA_MSK;

	out_be32(&priv->regs->mii_data, request);

	/* wait for it to finish, this takes about 23 us on lite5200b */
	while (!(in_be32(&fec->ievent) & FEC_IEVENT_MII) && --tries)
		udelay(5);

	if (tries == 0)
		return -ETIMEDOUT;

	return in_be32(&priv->regs->mii_data) & FEC_MII_DATA_DATAMSK;
}

static int mpc52xx_fec_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 data)
{
	struct mpc52xx_fec_mdio_priv *priv = bus->priv;
	struct mpc52xx_fec __iomem *fec;
	u32 value = data;
	int tries = 100;

	fec = priv->regs;
	out_be32(&fec->ievent, FEC_IEVENT_MII);

	value |= FEC_MII_WRITE_FRAME;
	value |= (phy_id << FEC_MII_DATA_PA_SHIFT) & FEC_MII_DATA_PA_MSK;
	value |= (reg << FEC_MII_DATA_RA_SHIFT) & FEC_MII_DATA_RA_MSK;

	out_be32(&priv->regs->mii_data, value);

	/* wait for request to finish */
	while (!(in_be32(&fec->ievent) & FEC_IEVENT_MII) && --tries)
		udelay(5);

	if (tries == 0)
		return -ETIMEDOUT;

	return 0;
}

static int mpc52xx_fec_mdio_probe(struct of_device *of, const struct of_device_id *match)
{
	struct device *dev = &of->dev;
	struct device_node *np = of->node;
	struct device_node *child = NULL;
	struct mii_bus *bus;
	struct mpc52xx_fec_mdio_priv *priv;
	struct resource res = {};
	int err;
	int i;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL)
		return -ENOMEM;
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		err = -ENOMEM;
		goto out_free;
	}

	bus->name = "mpc52xx MII bus";
	bus->read = mpc52xx_fec_mdio_read;
	bus->write = mpc52xx_fec_mdio_write;

	/* setup irqs */
	bus->irq = kmalloc(sizeof(bus->irq[0]) * PHY_MAX_ADDR, GFP_KERNEL);
	if (bus->irq == NULL) {
		err = -ENOMEM;
		goto out_free;
	}
	for (i=0; i<PHY_MAX_ADDR; i++)
		bus->irq[i] = PHY_POLL;

	while ((child = of_get_next_child(np, child)) != NULL) {
		int irq = irq_of_parse_and_map(child, 0);
		if (irq != NO_IRQ) {
			const u32 *id = of_get_property(child, "reg", NULL);
			bus->irq[*id] = irq;
		}
	}

	/* setup registers */
	err = of_address_to_resource(np, 0, &res);
	if (err)
		goto out_free;
	priv->regs = ioremap(res.start, res.end - res.start + 1);
	if (priv->regs == NULL) {
		err = -ENOMEM;
		goto out_free;
	}

	bus->id = res.start;
	bus->priv = priv;

	bus->dev = dev;
	dev_set_drvdata(dev, bus);

	/* set MII speed */
	out_be32(&priv->regs->mii_speed, ((mpc52xx_find_ipb_freq(of->node) >> 20) / 5) << 1);

	/* enable MII interrupt */
	out_be32(&priv->regs->imask, in_be32(&priv->regs->imask) | FEC_IMASK_MII);

	err = mdiobus_register(bus);
	if (err)
		goto out_unmap;

	return 0;

 out_unmap:
	iounmap(priv->regs);
 out_free:
	for (i=0; i<PHY_MAX_ADDR; i++)
		if (bus->irq[i] != PHY_POLL)
			irq_dispose_mapping(bus->irq[i]);
	kfree(bus->irq);
	kfree(priv);
	kfree(bus);

	return err;
}

static int mpc52xx_fec_mdio_remove(struct of_device *of)
{
	struct device *dev = &of->dev;
	struct mii_bus *bus = dev_get_drvdata(dev);
	struct mpc52xx_fec_mdio_priv *priv = bus->priv;
	int i;

	mdiobus_unregister(bus);
	dev_set_drvdata(dev, NULL);

	iounmap(priv->regs);
	for (i=0; i<PHY_MAX_ADDR; i++)
		if (bus->irq[i])
			irq_dispose_mapping(bus->irq[i]);
	kfree(priv);
	kfree(bus->irq);
	kfree(bus);

	return 0;
}


static struct of_device_id mpc52xx_fec_mdio_match[] = {
	{
		.type = "mdio",
		.compatible = "mpc5200b-fec-phy",
	},
	{},
};

struct of_platform_driver mpc52xx_fec_mdio_driver = {
	.name = "mpc5200b-fec-phy",
	.probe = mpc52xx_fec_mdio_probe,
	.remove = mpc52xx_fec_mdio_remove,
	.match_table = mpc52xx_fec_mdio_match,
};

/* let fec driver call it, since this has to be registered before it */
EXPORT_SYMBOL_GPL(mpc52xx_fec_mdio_driver);


MODULE_LICENSE("Dual BSD/GPL");
