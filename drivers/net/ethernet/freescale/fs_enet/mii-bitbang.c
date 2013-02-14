/*
 * Combined Ethernet driver for Motorola MPC8xx and MPC82xx.
 *
 * Copyright (c) 2003 Intracom S.A.
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/platform_device.h>
#include <linux/mdio-bitbang.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>

#include "fs_enet.h"

struct bb_info {
	struct mdiobb_ctrl ctrl;
	__be32 __iomem *dir;
	__be32 __iomem *dat;
	u32 mdio_msk;
	u32 mdc_msk;
};

/* FIXME: If any other users of GPIO crop up, then these will have to
 * have some sort of global synchronization to avoid races with other
 * pins on the same port.  The ideal solution would probably be to
 * bind the ports to a GPIO driver, and have this be a client of it.
 */
static inline void bb_set(u32 __iomem *p, u32 m)
{
	out_be32(p, in_be32(p) | m);
}

static inline void bb_clr(u32 __iomem *p, u32 m)
{
	out_be32(p, in_be32(p) & ~m);
}

static inline int bb_read(u32 __iomem *p, u32 m)
{
	return (in_be32(p) & m) != 0;
}

static inline void mdio_dir(struct mdiobb_ctrl *ctrl, int dir)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (dir)
		bb_set(bitbang->dir, bitbang->mdio_msk);
	else
		bb_clr(bitbang->dir, bitbang->mdio_msk);

	/* Read back to flush the write. */
	in_be32(bitbang->dir);
}

static inline int mdio_read(struct mdiobb_ctrl *ctrl)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);
	return bb_read(bitbang->dat, bitbang->mdio_msk);
}

static inline void mdio(struct mdiobb_ctrl *ctrl, int what)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (what)
		bb_set(bitbang->dat, bitbang->mdio_msk);
	else
		bb_clr(bitbang->dat, bitbang->mdio_msk);

	/* Read back to flush the write. */
	in_be32(bitbang->dat);
}

static inline void mdc(struct mdiobb_ctrl *ctrl, int what)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (what)
		bb_set(bitbang->dat, bitbang->mdc_msk);
	else
		bb_clr(bitbang->dat, bitbang->mdc_msk);

	/* Read back to flush the write. */
	in_be32(bitbang->dat);
}

static struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = mdc,
	.set_mdio_dir = mdio_dir,
	.set_mdio_data = mdio,
	.get_mdio_data = mdio_read,
};

static int fs_mii_bitbang_init(struct mii_bus *bus, struct device_node *np)
{
	struct resource res;
	const u32 *data;
	int mdio_pin, mdc_pin, len;
	struct bb_info *bitbang = bus->priv;

	int ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	if (resource_size(&res) <= 13)
		return -ENODEV;

	/* This should really encode the pin number as well, but all
	 * we get is an int, and the odds of multiple bitbang mdio buses
	 * is low enough that it's not worth going too crazy.
	 */
	snprintf(bus->id, MII_BUS_ID_SIZE, "%x", res.start);

	data = of_get_property(np, "fsl,mdio-pin", &len);
	if (!data || len != 4)
		return -ENODEV;
	mdio_pin = *data;

	data = of_get_property(np, "fsl,mdc-pin", &len);
	if (!data || len != 4)
		return -ENODEV;
	mdc_pin = *data;

	bitbang->dir = ioremap(res.start, resource_size(&res));
	if (!bitbang->dir)
		return -ENOMEM;

	bitbang->dat = bitbang->dir + 4;
	bitbang->mdio_msk = 1 << (31 - mdio_pin);
	bitbang->mdc_msk = 1 << (31 - mdc_pin);

	return 0;
}

static int fs_enet_mdio_probe(struct platform_device *ofdev)
{
	struct mii_bus *new_bus;
	struct bb_info *bitbang;
	int ret = -ENOMEM;

	bitbang = kzalloc(sizeof(struct bb_info), GFP_KERNEL);
	if (!bitbang)
		goto out;

	bitbang->ctrl.ops = &bb_ops;

	new_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!new_bus)
		goto out_free_priv;

	new_bus->name = "CPM2 Bitbanged MII",

	ret = fs_mii_bitbang_init(new_bus, ofdev->dev.of_node);
	if (ret)
		goto out_free_bus;

	new_bus->phy_mask = ~0;
	new_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!new_bus->irq) {
		ret = -ENOMEM;
		goto out_unmap_regs;
	}

	new_bus->parent = &ofdev->dev;
	dev_set_drvdata(&ofdev->dev, new_bus);

	ret = of_mdiobus_register(new_bus, ofdev->dev.of_node);
	if (ret)
		goto out_free_irqs;

	return 0;

out_free_irqs:
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(new_bus->irq);
out_unmap_regs:
	iounmap(bitbang->dir);
out_free_bus:
	free_mdio_bitbang(new_bus);
out_free_priv:
	kfree(bitbang);
out:
	return ret;
}

static int fs_enet_mdio_remove(struct platform_device *ofdev)
{
	struct mii_bus *bus = dev_get_drvdata(&ofdev->dev);
	struct bb_info *bitbang = bus->priv;

	mdiobus_unregister(bus);
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(bus->irq);
	free_mdio_bitbang(bus);
	iounmap(bitbang->dir);
	kfree(bitbang);

	return 0;
}

static struct of_device_id fs_enet_mdio_bb_match[] = {
	{
		.compatible = "fsl,cpm2-mdio-bitbang",
	},
	{},
};
MODULE_DEVICE_TABLE(of, fs_enet_mdio_bb_match);

static struct platform_driver fs_enet_bb_mdio_driver = {
	.driver = {
		.name = "fsl-bb-mdio",
		.owner = THIS_MODULE,
		.of_match_table = fs_enet_mdio_bb_match,
	},
	.probe = fs_enet_mdio_probe,
	.remove = fs_enet_mdio_remove,
};

module_platform_driver(fs_enet_bb_mdio_driver);
