// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Northstar Plus switch SerDes/SGMII PHY main logic
 *
 * Copyright (C) 2018 Florian Fainelli <f.fainelli@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "b53_priv.h"
#include "b53_serdes.h"
#include "b53_regs.h"

static inline struct b53_pcs *pcs_to_b53_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct b53_pcs, pcs);
}

static void b53_serdes_write_blk(struct b53_device *dev, u8 offset, u16 block,
				 u16 value)
{
	b53_write16(dev, B53_SERDES_PAGE, B53_SERDES_BLKADDR, block);
	b53_write16(dev, B53_SERDES_PAGE, offset, value);
}

static u16 b53_serdes_read_blk(struct b53_device *dev, u8 offset, u16 block)
{
	u16 value;

	b53_write16(dev, B53_SERDES_PAGE, B53_SERDES_BLKADDR, block);
	b53_read16(dev, B53_SERDES_PAGE, offset, &value);

	return value;
}

static void b53_serdes_set_lane(struct b53_device *dev, u8 lane)
{
	if (dev->serdes_lane == lane)
		return;

	WARN_ON(lane > 1);

	b53_serdes_write_blk(dev, B53_SERDES_LANE,
			     SERDES_XGXSBLK0_BLOCKADDRESS, lane);
	dev->serdes_lane = lane;
}

static void b53_serdes_write(struct b53_device *dev, u8 lane,
			     u8 offset, u16 block, u16 value)
{
	b53_serdes_set_lane(dev, lane);
	b53_serdes_write_blk(dev, offset, block, value);
}

static u16 b53_serdes_read(struct b53_device *dev, u8 lane,
			   u8 offset, u16 block)
{
	b53_serdes_set_lane(dev, lane);
	return b53_serdes_read_blk(dev, offset, block);
}

static int b53_serdes_config(struct phylink_pcs *pcs, unsigned int mode,
			     phy_interface_t interface,
			     const unsigned long *advertising,
			     bool permit_pause_to_mac)
{
	struct b53_device *dev = pcs_to_b53_pcs(pcs)->dev;
	u8 lane = pcs_to_b53_pcs(pcs)->lane;
	u16 reg;

	reg = b53_serdes_read(dev, lane, B53_SERDES_DIGITAL_CONTROL(1),
			      SERDES_DIGITAL_BLK);
	if (interface == PHY_INTERFACE_MODE_1000BASEX)
		reg |= FIBER_MODE_1000X;
	else
		reg &= ~FIBER_MODE_1000X;
	b53_serdes_write(dev, lane, B53_SERDES_DIGITAL_CONTROL(1),
			 SERDES_DIGITAL_BLK, reg);

	return 0;
}

static void b53_serdes_an_restart(struct phylink_pcs *pcs)
{
	struct b53_device *dev = pcs_to_b53_pcs(pcs)->dev;
	u8 lane = pcs_to_b53_pcs(pcs)->lane;
	u16 reg;

	reg = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			      SERDES_MII_BLK);
	reg |= BMCR_ANRESTART;
	b53_serdes_write(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			 SERDES_MII_BLK, reg);
}

static void b53_serdes_get_state(struct phylink_pcs *pcs,
				  struct phylink_link_state *state)
{
	struct b53_device *dev = pcs_to_b53_pcs(pcs)->dev;
	u8 lane = pcs_to_b53_pcs(pcs)->lane;
	u16 dig, bmsr;

	dig = b53_serdes_read(dev, lane, B53_SERDES_DIGITAL_STATUS,
			      SERDES_DIGITAL_BLK);
	bmsr = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_BMSR),
			       SERDES_MII_BLK);

	switch ((dig >> SPEED_STATUS_SHIFT) & SPEED_STATUS_MASK) {
	case SPEED_STATUS_10:
		state->speed = SPEED_10;
		break;
	case SPEED_STATUS_100:
		state->speed = SPEED_100;
		break;
	case SPEED_STATUS_1000:
		state->speed = SPEED_1000;
		break;
	default:
	case SPEED_STATUS_2500:
		state->speed = SPEED_2500;
		break;
	}

	state->duplex = dig & DUPLEX_STATUS ? DUPLEX_FULL : DUPLEX_HALF;
	state->an_complete = !!(bmsr & BMSR_ANEGCOMPLETE);
	state->link = !!(dig & LINK_STATUS);
	if (dig & PAUSE_RESOLUTION_RX_SIDE)
		state->pause |= MLO_PAUSE_RX;
	if (dig & PAUSE_RESOLUTION_TX_SIDE)
		state->pause |= MLO_PAUSE_TX;
}

void b53_serdes_link_set(struct b53_device *dev, int port, unsigned int mode,
			 phy_interface_t interface, bool link_up)
{
	u8 lane = b53_serdes_map_lane(dev, port);
	u16 reg;

	if (lane == B53_INVALID_LANE)
		return;

	reg = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			      SERDES_MII_BLK);
	if (link_up)
		reg &= ~BMCR_PDOWN;
	else
		reg |= BMCR_PDOWN;
	b53_serdes_write(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			 SERDES_MII_BLK, reg);
}
EXPORT_SYMBOL(b53_serdes_link_set);

static const struct phylink_pcs_ops b53_pcs_ops = {
	.pcs_get_state = b53_serdes_get_state,
	.pcs_config = b53_serdes_config,
	.pcs_an_restart = b53_serdes_an_restart,
};

void b53_serdes_phylink_get_caps(struct b53_device *dev, int port,
				 struct phylink_config *config)
{
	u8 lane = b53_serdes_map_lane(dev, port);

	if (lane == B53_INVALID_LANE)
		return;

	switch (lane) {
	case 0:
		/* It appears lane 0 supports 2500base-X and 1000base-X */
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_2500FD;
		fallthrough;
	case 1:
		/* It appears lane 1 only supports 1000base-X and SGMII */
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_1000FD;
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(b53_serdes_phylink_get_caps);

struct phylink_pcs *b53_serdes_phylink_mac_select_pcs(struct b53_device *dev,
						      int port,
						      phy_interface_t interface)
{
	u8 lane = b53_serdes_map_lane(dev, port);

	if (lane == B53_INVALID_LANE || lane >= B53_N_PCS ||
	    !dev->pcs[lane].dev)
		return NULL;

	if (!phy_interface_mode_is_8023z(interface) &&
	    interface != PHY_INTERFACE_MODE_SGMII)
		return NULL;

	return &dev->pcs[lane].pcs;
}
EXPORT_SYMBOL(b53_serdes_phylink_mac_select_pcs);

int b53_serdes_init(struct b53_device *dev, int port)
{
	u8 lane = b53_serdes_map_lane(dev, port);
	struct b53_pcs *pcs;
	u16 id0, msb, lsb;

	if (lane == B53_INVALID_LANE)
		return -EINVAL;

	id0 = b53_serdes_read(dev, lane, B53_SERDES_ID0, SERDES_ID0);
	msb = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_PHYSID1),
			      SERDES_MII_BLK);
	lsb = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_PHYSID2),
			      SERDES_MII_BLK);
	if (id0 == 0 || id0 == 0xffff) {
		dev_err(dev->dev, "SerDes not initialized, check settings\n");
		return -ENODEV;
	}

	dev_info(dev->dev,
		 "SerDes lane %d, model: %d, rev %c%d (OUI: 0x%08x)\n",
		 lane, id0 & SERDES_ID0_MODEL_MASK,
		 (id0 >> SERDES_ID0_REV_LETTER_SHIFT) + 0x41,
		 (id0 >> SERDES_ID0_REV_NUM_SHIFT) & SERDES_ID0_REV_NUM_MASK,
		 (u32)msb << 16 | lsb);

	pcs = &dev->pcs[lane];
	pcs->dev = dev;
	pcs->lane = lane;
	pcs->pcs.ops = &b53_pcs_ops;

	return 0;
}
EXPORT_SYMBOL(b53_serdes_init);

MODULE_AUTHOR("Florian Fainelli <f.fainelli@gmail.com>");
MODULE_DESCRIPTION("B53 Switch SerDes driver");
MODULE_LICENSE("Dual BSD/GPL");
