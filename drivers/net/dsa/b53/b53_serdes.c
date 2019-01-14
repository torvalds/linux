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

void b53_serdes_config(struct b53_device *dev, int port, unsigned int mode,
		       const struct phylink_link_state *state)
{
	u8 lane = b53_serdes_map_lane(dev, port);
	u16 reg;

	if (lane == B53_INVALID_LANE)
		return;

	reg = b53_serdes_read(dev, lane, B53_SERDES_DIGITAL_CONTROL(1),
			      SERDES_DIGITAL_BLK);
	if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
		reg |= FIBER_MODE_1000X;
	else
		reg &= ~FIBER_MODE_1000X;
	b53_serdes_write(dev, lane, B53_SERDES_DIGITAL_CONTROL(1),
			 SERDES_DIGITAL_BLK, reg);
}
EXPORT_SYMBOL(b53_serdes_config);

void b53_serdes_an_restart(struct b53_device *dev, int port)
{
	u8 lane = b53_serdes_map_lane(dev, port);
	u16 reg;

	if (lane == B53_INVALID_LANE)
		return;

	reg = b53_serdes_read(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			      SERDES_MII_BLK);
	reg |= BMCR_ANRESTART;
	b53_serdes_write(dev, lane, B53_SERDES_MII_REG(MII_BMCR),
			 SERDES_MII_BLK, reg);
}
EXPORT_SYMBOL(b53_serdes_an_restart);

int b53_serdes_link_state(struct b53_device *dev, int port,
			  struct phylink_link_state *state)
{
	u8 lane = b53_serdes_map_lane(dev, port);
	u16 dig, bmsr;

	if (lane == B53_INVALID_LANE)
		return 1;

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

	return 0;
}
EXPORT_SYMBOL(b53_serdes_link_state);

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

void b53_serdes_phylink_validate(struct b53_device *dev, int port,
				 unsigned long *supported,
				 struct phylink_link_state *state)
{
	u8 lane = b53_serdes_map_lane(dev, port);

	if (lane == B53_INVALID_LANE)
		return;

	switch (lane) {
	case 0:
		phylink_set(supported, 2500baseX_Full);
		/* fallthrough */
	case 1:
		phylink_set(supported, 1000baseX_Full);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(b53_serdes_phylink_validate);

int b53_serdes_init(struct b53_device *dev, int port)
{
	u8 lane = b53_serdes_map_lane(dev, port);
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

	return 0;
}
EXPORT_SYMBOL(b53_serdes_init);

MODULE_AUTHOR("Florian Fainelli <f.fainelli@gmail.com>");
MODULE_DESCRIPTION("B53 Switch SerDes driver");
MODULE_LICENSE("Dual BSD/GPL");
