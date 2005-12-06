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


#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

static const u16 mii_regs[7] = {
	0x3100,
	0x786d,
	0x0fff,
	0x0fff,
	0x01e1,
	0x45e1,
	0x0003,
};

static int mii_read(struct fs_enet_mii_bus *bus, int phy_id, int location)
{
	int ret = 0;

	if ((unsigned int)location >= ARRAY_SIZE(mii_regs))
		return -1;

	if (location != 5)
		ret = mii_regs[location];
	else
		ret = bus->fixed.lpa;

	return ret;
}

static void mii_write(struct fs_enet_mii_bus *bus, int phy_id, int location, int val)
{
	/* do nothing */
}

int fs_mii_fixed_init(struct fs_enet_mii_bus *bus)
{
	const struct fs_mii_bus_info *bi = bus->bus_info;

	bus->fixed.lpa = 0x45e1;	/* default 100Mb, full duplex */

	/* if speed is fixed at 10Mb, remove 100Mb modes */
	if (bi->i.fixed.speed == 10)
		bus->fixed.lpa &= ~LPA_100;

	/* if duplex is half, remove full duplex modes */
	if (bi->i.fixed.duplex == 0)
		bus->fixed.lpa &= ~LPA_DUPLEX;

	bus->mii_read = mii_read;
	bus->mii_write = mii_write;

	return 0;
}
