/*****************************************************************************
 *                                                                           *
 * File: cphy.h                                                              *
 * $Revision: 1.7 $                                                          *
 * $Date: 2005/06/21 18:29:47 $                                              *
 * Description:                                                              *
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

#ifndef _CXGB_CPHY_H_
#define _CXGB_CPHY_H_

#include "common.h"

struct mdio_ops {
	void (*init)(adapter_t *adapter, const struct board_info *bi);
	int  (*read)(struct net_device *dev, int phy_addr, int mmd_addr,
		     u16 reg_addr);
	int  (*write)(struct net_device *dev, int phy_addr, int mmd_addr,
		      u16 reg_addr, u16 val);
	unsigned mode_support;
};

/* PHY interrupt types */
enum {
	cphy_cause_link_change = 0x1,
	cphy_cause_error = 0x2,
	cphy_cause_fifo_error = 0x3
};

enum {
	PHY_LINK_UP = 0x1,
	PHY_AUTONEG_RDY = 0x2,
	PHY_AUTONEG_EN = 0x4
};

struct cphy;

/* PHY operations */
struct cphy_ops {
	void (*destroy)(struct cphy *);
	int (*reset)(struct cphy *, int wait);

	int (*interrupt_enable)(struct cphy *);
	int (*interrupt_disable)(struct cphy *);
	int (*interrupt_clear)(struct cphy *);
	int (*interrupt_handler)(struct cphy *);

	int (*autoneg_enable)(struct cphy *);
	int (*autoneg_disable)(struct cphy *);
	int (*autoneg_restart)(struct cphy *);

	int (*advertise)(struct cphy *phy, unsigned int advertise_map);
	int (*set_loopback)(struct cphy *, int on);
	int (*set_speed_duplex)(struct cphy *phy, int speed, int duplex);
	int (*get_link_status)(struct cphy *phy, int *link_ok, int *speed,
			       int *duplex, int *fc);

	u32 mmds;
};

/* A PHY instance */
struct cphy {
	int state;	/* Link status state machine */
	adapter_t *adapter;                  /* associated adapter */

	struct delayed_work phy_update;

	u16 bmsr;
	int count;
	int act_count;
	int act_on;

	u32 elmer_gpo;

	const struct cphy_ops *ops;            /* PHY operations */
	struct mdio_if_info mdio;
	struct cphy_instance *instance;
};

/* Convenience MDIO read/write wrappers */
static inline int cphy_mdio_read(struct cphy *cphy, int mmd, int reg,
				 unsigned int *valp)
{
	int rc = cphy->mdio.mdio_read(cphy->mdio.dev, cphy->mdio.prtad, mmd,
				      reg);
	*valp = (rc >= 0) ? rc : -1;
	return (rc >= 0) ? 0 : rc;
}

static inline int cphy_mdio_write(struct cphy *cphy, int mmd, int reg,
				  unsigned int val)
{
	return cphy->mdio.mdio_write(cphy->mdio.dev, cphy->mdio.prtad, mmd,
				     reg, val);
}

static inline int simple_mdio_read(struct cphy *cphy, int reg,
				   unsigned int *valp)
{
	return cphy_mdio_read(cphy, MDIO_DEVAD_NONE, reg, valp);
}

static inline int simple_mdio_write(struct cphy *cphy, int reg,
				    unsigned int val)
{
	return cphy_mdio_write(cphy, MDIO_DEVAD_NONE, reg, val);
}

/* Convenience initializer */
static inline void cphy_init(struct cphy *phy, struct net_device *dev,
			     int phy_addr, struct cphy_ops *phy_ops,
			     const struct mdio_ops *mdio_ops)
{
	struct adapter *adapter = netdev_priv(dev);
	phy->adapter = adapter;
	phy->ops     = phy_ops;
	if (mdio_ops) {
		phy->mdio.prtad = phy_addr;
		phy->mdio.mmds = phy_ops->mmds;
		phy->mdio.mode_support = mdio_ops->mode_support;
		phy->mdio.mdio_read = mdio_ops->read;
		phy->mdio.mdio_write = mdio_ops->write;
	}
	phy->mdio.dev = dev;
}

/* Operations of the PHY-instance factory */
struct gphy {
	/* Construct a PHY instance with the given PHY address */
	struct cphy *(*create)(struct net_device *dev, int phy_addr,
			       const struct mdio_ops *mdio_ops);

	/*
	 * Reset the PHY chip.  This resets the whole PHY chip, not individual
	 * ports.
	 */
	int (*reset)(adapter_t *adapter);
};

extern const struct gphy t1_my3126_ops;
extern const struct gphy t1_mv88e1xxx_ops;
extern const struct gphy t1_vsc8244_ops;
extern const struct gphy t1_mv88x201x_ops;

#endif /* _CXGB_CPHY_H_ */
