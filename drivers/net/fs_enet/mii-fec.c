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
#include <linux/pci.h>
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

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"
#include "fec.h"

/* Make MII read/write commands for the FEC.
*/
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

#define FEC_MII_LOOPS	10000

static int match_has_phy (struct device *dev, void* data)
{
	struct platform_device* pdev = container_of(dev, struct platform_device, dev);
	struct fs_platform_info* fpi;
	if(strcmp(pdev->name, (char*)data))
	{
	    return 0;
	}

	fpi = pdev->dev.platform_data;
	if((fpi)&&(fpi->has_phy))
		return 1;
	return 0;
}

static int fs_mii_fec_init(struct fec_info* fec, struct fs_mii_fec_platform_info *fmpi)
{
	struct resource *r;
	fec_t *fecp;
	char* name = "fsl-cpm-fec";

	/* we need fec in order to be useful */
	struct platform_device *fec_pdev =
		container_of(bus_find_device(&platform_bus_type, NULL, name, match_has_phy),
				struct platform_device, dev);

	if(fec_pdev == NULL) {
		printk(KERN_ERR"Unable to find PHY for %s", name);
		return -ENODEV;
	}

	r = platform_get_resource_byname(fec_pdev, IORESOURCE_MEM, "regs");

	fec->fecp = fecp = (fec_t*)ioremap(r->start,sizeof(fec_t));
	fec->mii_speed = fmpi->mii_speed;

	setbits32(&fecp->fec_r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
	setbits32(&fecp->fec_ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
	out_be32(&fecp->fec_ievent, FEC_ENET_MII);
	out_be32(&fecp->fec_mii_speed, fec->mii_speed);

	return 0;
}

static int fs_enet_fec_mii_read(struct mii_bus *bus , int phy_id, int location)
{
	struct fec_info* fec = bus->priv;
	fec_t *fecp = fec->fecp;
	int i, ret = -1;

	if ((in_be32(&fecp->fec_r_cntrl) & FEC_RCNTRL_MII_MODE) == 0)
		BUG();

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
	fec_t *fecp = fec->fecp;
	int i;

	/* this must never happen */
	if ((in_be32(&fecp->fec_r_cntrl) & FEC_RCNTRL_MII_MODE) == 0)
		BUG();

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

static int __devinit fs_enet_fec_mdio_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fs_mii_fec_platform_info *pdata;
	struct mii_bus *new_bus;
	struct fec_info *fec;
	int err = 0;
	if (NULL == dev)
		return -EINVAL;
	new_bus = kzalloc(sizeof(struct mii_bus), GFP_KERNEL);

	if (NULL == new_bus)
		return -ENOMEM;

	fec = kzalloc(sizeof(struct fec_info), GFP_KERNEL);

	if (NULL == fec)
		return -ENOMEM;

	new_bus->name = "FEC MII Bus",
	new_bus->read = &fs_enet_fec_mii_read,
	new_bus->write = &fs_enet_fec_mii_write,
	new_bus->reset = &fs_enet_fec_mii_reset,
	new_bus->id = pdev->id;

	pdata = (struct fs_mii_fec_platform_info *)pdev->dev.platform_data;

	if (NULL == pdata) {
		printk(KERN_ERR "fs_enet FEC mdio %d: Missing platform data!\n", pdev->id);
		return -ENODEV;
	}

	/*set up workspace*/

	fs_mii_fec_init(fec, pdata);
	new_bus->priv = fec;

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
	kfree(new_bus);

	return err;
}


static int fs_enet_fec_mdio_remove(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(dev, NULL);
	kfree(bus->priv);

	bus->priv = NULL;
	kfree(bus);

	return 0;
}

static struct device_driver fs_enet_fec_mdio_driver = {
	.name = "fsl-cpm-fec-mdio",
	.bus = &platform_bus_type,
	.probe = fs_enet_fec_mdio_probe,
	.remove = fs_enet_fec_mdio_remove,
};

int fs_enet_mdio_fec_init(void)
{
	return driver_register(&fs_enet_fec_mdio_driver);
}

void fs_enet_mdio_fec_exit(void)
{
	driver_unregister(&fs_enet_fec_mdio_driver);
}

