/*****************************************************************************
 *                                                                           *
 * File: mv88x201x.c                                                         *
 * $Revision: 1.12 $                                                         *
 * $Date: 2005/04/15 19:27:14 $                                              *
 * Description:                                                              *
 *  Marvell PHY (mv88x201x) functionality.                                   *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#include "cphy.h"
#include "elmer0.h"

/*
 * The 88x2010 Rev C. requires some link status registers * to be read
 * twice in order to get the right values. Future * revisions will fix
 * this problem and then this macro * can disappear.
 */
#define MV88x2010_LINK_STATUS_BUGS    1

static int led_init(struct cphy *cphy)
{
	/* Setup the LED registers so we can turn on/off.
	 * Writing these bits maps control to another
	 * register. mmd(0x1) addr(0x7)
	 */
	mdio_write(cphy, 0x3, 0x8304, 0xdddd);
	return 0;
}

static int led_link(struct cphy *cphy, u32 do_enable)
{
	u32 led = 0;
#define LINK_ENABLE_BIT 0x1

	mdio_read(cphy, 0x1, 0x7, &led);

	if (do_enable & LINK_ENABLE_BIT) {
		led |= LINK_ENABLE_BIT;
		mdio_write(cphy, 0x1, 0x7, led);
	} else {
		led &= ~LINK_ENABLE_BIT;
		mdio_write(cphy, 0x1, 0x7, led);
	}
	return 0;
}

/* Port Reset */
static int mv88x201x_reset(struct cphy *cphy, int wait)
{
	/* This can be done through registers.  It is not required since
	 * a full chip reset is used.
	 */
	return 0;
}

static int mv88x201x_interrupt_enable(struct cphy *cphy)
{
	/* Enable PHY LASI interrupts. */
	mdio_write(cphy, 0x1, 0x9002, 0x1);

	/* Enable Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		u32 elmer;

		t1_tpi_read(cphy->adapter, A_ELMER0_INT_ENABLE, &elmer);
		elmer |= ELMER0_GP_BIT6;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_ENABLE, elmer);
	}
	return 0;
}

static int mv88x201x_interrupt_disable(struct cphy *cphy)
{
	/* Disable PHY LASI interrupts. */
	mdio_write(cphy, 0x1, 0x9002, 0x0);

	/* Disable Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		u32 elmer;

		t1_tpi_read(cphy->adapter, A_ELMER0_INT_ENABLE, &elmer);
		elmer &= ~ELMER0_GP_BIT6;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_ENABLE, elmer);
	}
	return 0;
}

static int mv88x201x_interrupt_clear(struct cphy *cphy)
{
	u32 elmer;
	u32 val;

#ifdef MV88x2010_LINK_STATUS_BUGS
	/* Required to read twice before clear takes affect. */
	mdio_read(cphy, 0x1, 0x9003, &val);
	mdio_read(cphy, 0x1, 0x9004, &val);
	mdio_read(cphy, 0x1, 0x9005, &val);

	/* Read this register after the others above it else
	 * the register doesn't clear correctly.
	 */
	mdio_read(cphy, 0x1, 0x1, &val);
#endif

	/* Clear link status. */
	mdio_read(cphy, 0x1, 0x1, &val);
	/* Clear PHY LASI interrupts. */
	mdio_read(cphy, 0x1, 0x9005, &val);

#ifdef MV88x2010_LINK_STATUS_BUGS
	/* Do it again. */
	mdio_read(cphy, 0x1, 0x9003, &val);
	mdio_read(cphy, 0x1, 0x9004, &val);
#endif

	/* Clear Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		t1_tpi_read(cphy->adapter, A_ELMER0_INT_CAUSE, &elmer);
		elmer |= ELMER0_GP_BIT6;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_CAUSE, elmer);
	}
	return 0;
}

static int mv88x201x_interrupt_handler(struct cphy *cphy)
{
	/* Clear interrupts */
	mv88x201x_interrupt_clear(cphy);

	/* We have only enabled link change interrupts and so
	 * cphy_cause must be a link change interrupt.
	 */
	return cphy_cause_link_change;
}

static int mv88x201x_set_loopback(struct cphy *cphy, int on)
{
	return 0;
}

static int mv88x201x_get_link_status(struct cphy *cphy, int *link_ok,
				     int *speed, int *duplex, int *fc)
{
	u32 val = 0;
#define LINK_STATUS_BIT 0x4

	if (link_ok) {
		/* Read link status. */
		mdio_read(cphy, 0x1, 0x1, &val);
		val &= LINK_STATUS_BIT;
		*link_ok = (val == LINK_STATUS_BIT);
		/* Turn on/off Link LED */
		led_link(cphy, *link_ok);
	}
	if (speed)
		*speed = SPEED_10000;
	if (duplex)
		*duplex = DUPLEX_FULL;
	if (fc)
		*fc = PAUSE_RX | PAUSE_TX;
	return 0;
}

static void mv88x201x_destroy(struct cphy *cphy)
{
	kfree(cphy);
}

static struct cphy_ops mv88x201x_ops = {
	.destroy           = mv88x201x_destroy,
	.reset             = mv88x201x_reset,
	.interrupt_enable  = mv88x201x_interrupt_enable,
	.interrupt_disable = mv88x201x_interrupt_disable,
	.interrupt_clear   = mv88x201x_interrupt_clear,
	.interrupt_handler = mv88x201x_interrupt_handler,
	.get_link_status   = mv88x201x_get_link_status,
	.set_loopback      = mv88x201x_set_loopback,
};

static struct cphy *mv88x201x_phy_create(adapter_t *adapter, int phy_addr,
					 struct mdio_ops *mdio_ops)
{
	u32 val;
	struct cphy *cphy = kzalloc(sizeof(*cphy), GFP_KERNEL);

	if (!cphy)
		return NULL;

	cphy_init(cphy, adapter, phy_addr, &mv88x201x_ops, mdio_ops);

	/* Commands the PHY to enable XFP's clock. */
	mdio_read(cphy, 0x3, 0x8300, &val);
	mdio_write(cphy, 0x3, 0x8300, val | 1);

	/* Clear link status. Required because of a bug in the PHY.  */
	mdio_read(cphy, 0x1, 0x8, &val);
	mdio_read(cphy, 0x3, 0x8, &val);

	/* Allows for Link,Ack LED turn on/off */
	led_init(cphy);
	return cphy;
}

/* Chip Reset */
static int mv88x201x_phy_reset(adapter_t *adapter)
{
	u32 val;

	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	val &= ~4;
	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	msleep(100);

	t1_tpi_write(adapter, A_ELMER0_GPO, val | 4);
	msleep(1000);

	/* Now lets enable the Laser. Delay 100us */
	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	val |= 0x8000;
	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	udelay(100);
	return 0;
}

struct gphy t1_mv88x201x_ops = {
	mv88x201x_phy_create,
	mv88x201x_phy_reset
};
