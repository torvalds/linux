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

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

static int bitbang_prep_bit(u8 **datp, u8 *mskp,
		struct fs_mii_bit *mii_bit)
{
	void *dat;
	int adv;
	u8 msk;

	dat = (void*) mii_bit->offset;

	adv = mii_bit->bit >> 3;
	dat = (char *)dat + adv;

	msk = 1 << (7 - (mii_bit->bit & 7));

	*datp = dat;
	*mskp = msk;

	return 0;
}

static inline void bb_set(u8 *p, u8 m)
{
	out_8(p, in_8(p) | m);
}

static inline void bb_clr(u8 *p, u8 m)
{
	out_8(p, in_8(p) & ~m);
}

static inline int bb_read(u8 *p, u8 m)
{
	return (in_8(p) & m) != 0;
}

static inline void mdio_active(struct bb_info *bitbang)
{
	bb_set(bitbang->mdio_dir, bitbang->mdio_dir_msk);
}

static inline void mdio_tristate(struct bb_info *bitbang )
{
	bb_clr(bitbang->mdio_dir, bitbang->mdio_dir_msk);
}

static inline int mdio_read(struct bb_info *bitbang )
{
	return bb_read(bitbang->mdio_dat, bitbang->mdio_dat_msk);
}

static inline void mdio(struct bb_info *bitbang , int what)
{
	if (what)
		bb_set(bitbang->mdio_dat, bitbang->mdio_dat_msk);
	else
		bb_clr(bitbang->mdio_dat, bitbang->mdio_dat_msk);
}

static inline void mdc(struct bb_info *bitbang , int what)
{
	if (what)
		bb_set(bitbang->mdc_dat, bitbang->mdc_msk);
	else
		bb_clr(bitbang->mdc_dat, bitbang->mdc_msk);
}

static inline void mii_delay(struct bb_info *bitbang )
{
	udelay(bitbang->delay);
}

/* Utility to send the preamble, address, and register (common to read and write). */
static void bitbang_pre(struct bb_info *bitbang , int read, u8 addr, u8 reg)
{
	int j;

	/*
	 * Send a 32 bit preamble ('1's) with an extra '1' bit for good measure.
	 * The IEEE spec says this is a PHY optional requirement.  The AMD
	 * 79C874 requires one after power up and one after a MII communications
	 * error.  This means that we are doing more preambles than we need,
	 * but it is safer and will be much more robust.
	 */

	mdio_active(bitbang);
	mdio(bitbang, 1);
	for (j = 0; j < 32; j++) {
		mdc(bitbang, 0);
		mii_delay(bitbang);
		mdc(bitbang, 1);
		mii_delay(bitbang);
	}

	/* send the start bit (01) and the read opcode (10) or write (10) */
	mdc(bitbang, 0);
	mdio(bitbang, 0);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 0);
	mdio(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 0);
	mdio(bitbang, read);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 0);
	mdio(bitbang, !read);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);

	/* send the PHY address */
	for (j = 0; j < 5; j++) {
		mdc(bitbang, 0);
		mdio(bitbang, (addr & 0x10) != 0);
		mii_delay(bitbang);
		mdc(bitbang, 1);
		mii_delay(bitbang);
		addr <<= 1;
	}

	/* send the register address */
	for (j = 0; j < 5; j++) {
		mdc(bitbang, 0);
		mdio(bitbang, (reg & 0x10) != 0);
		mii_delay(bitbang);
		mdc(bitbang, 1);
		mii_delay(bitbang);
		reg <<= 1;
	}
}

static int fs_enet_mii_bb_read(struct mii_bus *bus , int phy_id, int location)
{
	u16 rdreg;
	int ret, j;
	u8 addr = phy_id & 0xff;
	u8 reg = location & 0xff;
	struct bb_info* bitbang = bus->priv;

	bitbang_pre(bitbang, 1, addr, reg);

	/* tri-state our MDIO I/O pin so we can read */
	mdc(bitbang, 0);
	mdio_tristate(bitbang);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);

	/* check the turnaround bit: the PHY should be driving it to zero */
	if (mdio_read(bitbang) != 0) {
		/* PHY didn't drive TA low */
		for (j = 0; j < 32; j++) {
			mdc(bitbang, 0);
			mii_delay(bitbang);
			mdc(bitbang, 1);
			mii_delay(bitbang);
		}
		ret = -1;
		goto out;
	}

	mdc(bitbang, 0);
	mii_delay(bitbang);

	/* read 16 bits of register data, MSB first */
	rdreg = 0;
	for (j = 0; j < 16; j++) {
		mdc(bitbang, 1);
		mii_delay(bitbang);
		rdreg <<= 1;
		rdreg |= mdio_read(bitbang);
		mdc(bitbang, 0);
		mii_delay(bitbang);
	}

	mdc(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 0);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);

	ret = rdreg;
out:
	return ret;
}

static int fs_enet_mii_bb_write(struct mii_bus *bus, int phy_id, int location, u16 val)
{
	int j;
	struct bb_info* bitbang = bus->priv;

	u8 addr = phy_id & 0xff;
	u8 reg = location & 0xff;
	u16 value = val & 0xffff;

	bitbang_pre(bitbang, 0, addr, reg);

	/* send the turnaround (10) */
	mdc(bitbang, 0);
	mdio(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);
	mdc(bitbang, 0);
	mdio(bitbang, 0);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);

	/* write 16 bits of register data, MSB first */
	for (j = 0; j < 16; j++) {
		mdc(bitbang, 0);
		mdio(bitbang, (value & 0x8000) != 0);
		mii_delay(bitbang);
		mdc(bitbang, 1);
		mii_delay(bitbang);
		value <<= 1;
	}

	/*
	 * Tri-state the MDIO line.
	 */
	mdio_tristate(bitbang);
	mdc(bitbang, 0);
	mii_delay(bitbang);
	mdc(bitbang, 1);
	mii_delay(bitbang);
	return 0;
}

static int fs_enet_mii_bb_reset(struct mii_bus *bus)
{
	/*nothing here - dunno how to reset it*/
	return 0;
}

static int fs_mii_bitbang_init(struct bb_info *bitbang, struct fs_mii_bb_platform_info* fmpi)
{
	int r;

	bitbang->delay = fmpi->delay;

	r = bitbang_prep_bit(&bitbang->mdio_dir,
			 &bitbang->mdio_dir_msk,
			 &fmpi->mdio_dir);
	if (r != 0)
		return r;

	r = bitbang_prep_bit(&bitbang->mdio_dat,
			 &bitbang->mdio_dat_msk,
			 &fmpi->mdio_dat);
	if (r != 0)
		return r;

	r = bitbang_prep_bit(&bitbang->mdc_dat,
			 &bitbang->mdc_msk,
			 &fmpi->mdc_dat);
	if (r != 0)
		return r;

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

	new_bus = kzalloc(sizeof(struct mii_bus), GFP_KERNEL);

	if (NULL == new_bus)
		return -ENOMEM;

	bitbang = kzalloc(sizeof(struct bb_info), GFP_KERNEL);

	if (NULL == bitbang)
		return -ENOMEM;

	new_bus->name = "BB MII Bus",
	new_bus->read = &fs_enet_mii_bb_read,
	new_bus->write = &fs_enet_mii_bb_write,
	new_bus->reset = &fs_enet_mii_bb_reset,
	new_bus->id = pdev->id;

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
	kfree(bitbang);
	kfree(new_bus);

	return err;
}


static int fs_enet_mdio_remove(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(dev, NULL);

	iounmap((void *) (&bus->priv));
	bus->priv = NULL;
	kfree(bus);

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

