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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mpc5xxx.h>

#include "fs_enet.h"
#include "fec.h"

/* Make MII read/write commands for the FEC.
*/
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

#define FEC_MII_LOOPS	10000

static int fs_enet_fec_mii_read(struct mii_bus *bus , int phy_id, int location)
{
	struct fec_info* fec = bus->priv;
	struct fec __iomem *fecp = fec->fecp;
	int i, ret = -1;

	BUG_ON((in_be32(&fecp->fec_r_cntrl) & FEC_RCNTRL_MII_MODE) == 0);

	/* Add PHY address to register command.  */
	out_be32(&fecp->fec_mii_data, (phy_id << 23) | mk_mii_read(location));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((in_be32(&fecp->fec_ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS) {
		out_be32(&fecp->fec_ievent, FEC_ENET_MII);
		ret = in_be32(&fecp->fec_mii_data) & 0xffff;
	}

	return ret;
}

static int fs_enet_fec_mii_write(struct mii_bus *bus, int phy_id, int location, u16 val)
{
	struct fec_info* fec = bus->priv;
	struct fec __iomem *fecp = fec->fecp;
	int i;

	/* this must never happen */
	BUG_ON((in_be32(&fecp->fec_r_cntrl) & FEC_RCNTRL_MII_MODE) == 0);

	/* Add PHY address to register command.  */
	out_be32(&fecp->fec_mii_data, (phy_id << 23) | mk_mii_write(location, val));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((in_be32(&fecp->fec_ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS)
		out_be32(&fecp->fec_ievent, FEC_ENET_MII);

	return 0;

}

static int fs_enet_fec_mii_reset(struct mii_bus *bus)
{
	/* nothing here - for now */
	return 0;
}

static int __devinit fs_enet_mdio_probe(struct of_device *ofdev,
                                        const struct of_device_id *match)
{
	struct resource res;
	struct mii_bus *new_bus;
	struct fec_info *fec;
	int (*get_bus_freq)(struct device_node *) = match->data;
	int ret = -ENOMEM, clock, speed;

	new_bus = mdiobus_alloc();
	if (!new_bus)
		goto out;

	fec = kzalloc(sizeof(struct fec_info), GFP_KERNEL);
	if (!fec)
		goto out_mii;

	new_bus->priv = fec;
	new_bus->name = "FEC MII Bus";
	new_bus->read = &fs_enet_fec_mii_read;
	new_bus->write = &fs_enet_fec_mii_write;
	new_bus->reset = &fs_enet_fec_mii_reset;

	ret = of_address_to_resource(ofdev->dev.of_node, 0, &res);
	if (ret)
		goto out_res;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", res.start);

	fec->fecp = ioremap(res.start, res.end - res.start + 1);
	if (!fec->fecp)
		goto out_fec;

	if (get_bus_freq) {
		clock = get_bus_freq(ofdev->dev.of_node);
		if (!clock) {
			/* Use maximum divider if clock is unknown */
			dev_warn(&ofdev->dev, "could not determine IPS clock\n");
			clock = 0x3F * 5000000;
		}
	} else
		clock = ppc_proc_freq;

	/*
	 * Scale for a MII clock <= 2.5 MHz
	 * Note that only 6 bits (25:30) are available for MII speed.
	 */
	speed = (clock + 4999999) / 5000000;
	if (speed > 0x3F) {
		speed = 0x3F;
		dev_err(&ofdev->dev,
			"MII clock (%d Hz) exceeds max (2.5 MHz)\n",
			clock / speed);
	}

	fec->mii_speed = speed << 1;

	setbits32(&fec->fecp->fec_r_cntrl, FEC_RCNTRL_MII_MODE);
	setbits32(&fec->fecp->fec_ecntrl, FEC_ECNTRL_PINMUX |
	                                  FEC_ECNTRL_ETHER_EN);
	out_be32(&fec->fecp->fec_ievent, FEC_ENET_MII);
	clrsetbits_be32(&fec->fecp->fec_mii_speed, 0x7E, fec->mii_speed);

	new_bus->phy_mask = ~0;
	new_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!new_bus->irq)
		goto out_unmap_regs;

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
	iounmap(fec->fecp);
out_res:
out_fec:
	kfree(fec);
out_mii:
	mdiobus_free(new_bus);
out:
	return ret;
}

static int fs_enet_mdio_remove(struct of_device *ofdev)
{
	struct mii_bus *bus = dev_get_drvdata(&ofdev->dev);
	struct fec_info *fec = bus->priv;

	mdiobus_unregister(bus);
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(bus->irq);
	iounmap(fec->fecp);
	kfree(fec);
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id fs_enet_mdio_fec_match[] = {
	{
		.compatible = "fsl,pq1-fec-mdio",
	},
#if defined(CONFIG_PPC_MPC512x)
	{
		.compatible = "fsl,mpc5121-fec-mdio",
		.data = mpc5xxx_get_bus_frequency,
	},
#endif
	{},
};
MODULE_DEVICE_TABLE(of, fs_enet_mdio_fec_match);

static struct of_platform_driver fs_enet_fec_mdio_driver = {
	.driver = {
		.name = "fsl-fec-mdio",
		.owner = THIS_MODULE,
		.of_match_table = fs_enet_mdio_fec_match,
	},
	.probe = fs_enet_mdio_probe,
	.remove = fs_enet_mdio_remove,
};

static int fs_enet_mdio_fec_init(void)
{
	return of_register_platform_driver(&fs_enet_fec_mdio_driver);
}

static void fs_enet_mdio_fec_exit(void)
{
	of_unregister_platform_driver(&fs_enet_fec_mdio_driver);
}

module_init(fs_enet_mdio_fec_init);
module_exit(fs_enet_mdio_fec_exit);
