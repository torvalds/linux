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

#ifdef CONFIG_PPC_CPM_NEW_BINDING
#include <linux/of_platform.h>
#endif

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

#ifdef CONFIG_PPC_CPM_NEW_BINDING
static int __devinit fs_mii_bitbang_init(struct mii_bus *bus,
                                         struct device_node *np)
{
	struct resource res;
	const u32 *data;
	int mdio_pin, mdc_pin, len;
	struct bb_info *bitbang = bus->priv;

	int ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	if (res.end - res.start < 13)
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

	bitbang->dir = ioremap(res.start, res.end - res.start + 1);
	if (!bitbang->dir)
		return -ENOMEM;

	bitbang->dat = bitbang->dir + 4;
	bitbang->mdio_msk = 1 << (31 - mdio_pin);
	bitbang->mdc_msk = 1 << (31 - mdc_pin);

	return 0;
}

static void __devinit add_phy(struct mii_bus *bus, struct device_node *np)
{
	const u32 *data;
	int len, id, irq;

	data = of_get_property(np, "reg", &len);
	if (!data || len != 4)
		return;

	id = *data;
	bus->phy_mask &= ~(1 << id);

	irq = of_irq_to_resource(np, 0, NULL);
	if (irq != NO_IRQ)
		bus->irq[id] = irq;
}

static int __devinit fs_enet_mdio_probe(struct of_device *ofdev,
                                        const struct of_device_id *match)
{
	struct device_node *np = NULL;
	struct mii_bus *new_bus;
	struct bb_info *bitbang;
	int ret = -ENOMEM;
	int i;

	bitbang = kzalloc(sizeof(struct bb_info), GFP_KERNEL);
	if (!bitbang)
		goto out;

	bitbang->ctrl.ops = &bb_ops;

	new_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!new_bus)
		goto out_free_priv;

	new_bus->name = "CPM2 Bitbanged MII",

	ret = fs_mii_bitbang_init(new_bus, ofdev->node);
	if (ret)
		goto out_free_bus;

	new_bus->phy_mask = ~0;
	new_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!new_bus->irq)
		goto out_unmap_regs;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		new_bus->irq[i] = -1;

	while ((np = of_get_next_child(ofdev->node, np)))
		if (!strcmp(np->type, "ethernet-phy"))
			add_phy(new_bus, np);

	new_bus->dev = &ofdev->dev;
	dev_set_drvdata(&ofdev->dev, new_bus);

	ret = mdiobus_register(new_bus);
	if (ret)
		goto out_free_irqs;

	return 0;

out_free_irqs:
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(new_bus->irq);
out_unmap_regs:
	iounmap(bitbang->dir);
out_free_bus:
	kfree(new_bus);
out_free_priv:
	free_mdio_bitbang(new_bus);
out:
	return ret;
}

static int fs_enet_mdio_remove(struct of_device *ofdev)
{
	struct mii_bus *bus = dev_get_drvdata(&ofdev->dev);
	struct bb_info *bitbang = bus->priv;

	mdiobus_unregister(bus);
	free_mdio_bitbang(bus);
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(bus->irq);
	iounmap(bitbang->dir);
	kfree(bitbang);
	kfree(bus);

	return 0;
}

static struct of_device_id fs_enet_mdio_bb_match[] = {
	{
		.compatible = "fsl,cpm2-mdio-bitbang",
	},
	{},
};

static struct of_platform_driver fs_enet_bb_mdio_driver = {
	.name = "fsl-bb-mdio",
	.match_table = fs_enet_mdio_bb_match,
	.probe = fs_enet_mdio_probe,
	.remove = fs_enet_mdio_remove,
};

static int fs_enet_mdio_bb_init(void)
{
	return of_register_platform_driver(&fs_enet_bb_mdio_driver);
}

static void fs_enet_mdio_bb_exit(void)
{
	of_unregister_platform_driver(&fs_enet_bb_mdio_driver);
}

module_init(fs_enet_mdio_bb_init);
module_exit(fs_enet_mdio_bb_exit);
#else
static int __devinit fs_mii_bitbang_init(struct bb_info *bitbang,
                                         struct fs_mii_bb_platform_info *fmpi)
{
	bitbang->dir = (u32 __iomem *)fmpi->mdio_dir.offset;
	bitbang->dat = (u32 __iomem *)fmpi->mdio_dat.offset;
	bitbang->mdio_msk = 1U << (31 - fmpi->mdio_dat.bit);
	bitbang->mdc_msk = 1U << (31 - fmpi->mdc_dat.bit);

	return 0;
}

static int __devinit fs_enet_mdio_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fs_mii_bb_platform_info *pdata;
	struct mii_bus *new_bus;
	struct bb_info *bitbang;
	int err = 0;

	if (NULL == dev)
		return -EINVAL;

	bitbang = kzalloc(sizeof(struct bb_info), GFP_KERNEL);

	if (NULL == bitbang)
		return -ENOMEM;

	bitbang->ctrl.ops = &bb_ops;

	new_bus = alloc_mdio_bitbang(&bitbang->ctrl);

	if (NULL == new_bus)
		return -ENOMEM;

	new_bus->name = "BB MII Bus",
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", pdev->id);

	new_bus->phy_mask = ~0x9;
	pdata = (struct fs_mii_bb_platform_info *)pdev->dev.platform_data;

	if (NULL == pdata) {
		printk(KERN_ERR "gfar mdio %d: Missing platform data!\n", pdev->id);
		return -ENODEV;
	}

	/*set up workspace*/
	fs_mii_bitbang_init(bitbang, pdata);

	new_bus->priv = bitbang;

	new_bus->irq = pdata->irq;

	new_bus->dev = dev;
	dev_set_drvdata(dev, new_bus);

	err = mdiobus_register(new_bus);

	if (0 != err) {
		printk (KERN_ERR "%s: Cannot register as MDIO bus\n",
				new_bus->name);
		goto bus_register_fail;
	}

	return 0;

bus_register_fail:
	free_mdio_bitbang(new_bus);
	kfree(bitbang);

	return err;
}

static int fs_enet_mdio_remove(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(dev, NULL);

	free_mdio_bitbang(bus);

	return 0;
}

static struct device_driver fs_enet_bb_mdio_driver = {
	.name = "fsl-bb-mdio",
	.bus = &platform_bus_type,
	.probe = fs_enet_mdio_probe,
	.remove = fs_enet_mdio_remove,
};

int fs_enet_mdio_bb_init(void)
{
	return driver_register(&fs_enet_bb_mdio_driver);
}

void fs_enet_mdio_bb_exit(void)
{
	driver_unregister(&fs_enet_bb_mdio_driver);
}
#endif
