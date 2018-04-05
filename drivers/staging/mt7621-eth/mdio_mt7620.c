/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "mtk_eth_soc.h"
#include "gsw_mt7620.h"
#include "mdio.h"

static int mt7620_mii_busy_wait(struct mt7620_gsw *gsw)
{
	unsigned long t_start = jiffies;

	while (1) {
		if (!(mtk_switch_r32(gsw,
				     gsw->piac_offset + MT7620_GSW_REG_PIAC) &
				     GSW_MDIO_ACCESS))
			return 0;
		if (time_after(jiffies, t_start + GSW_REG_PHY_TIMEOUT))
			break;
	}

	dev_err(gsw->dev, "mdio: MDIO timeout\n");
	return -1;
}

u32 _mt7620_mii_write(struct mt7620_gsw *gsw, u32 phy_addr,
		      u32 phy_register, u32 write_data)
{
	if (mt7620_mii_busy_wait(gsw))
		return -1;

	write_data &= 0xffff;

	mtk_switch_w32(gsw, GSW_MDIO_ACCESS | GSW_MDIO_START | GSW_MDIO_WRITE |
		(phy_register << GSW_MDIO_REG_SHIFT) |
		(phy_addr << GSW_MDIO_ADDR_SHIFT) | write_data,
		MT7620_GSW_REG_PIAC);

	if (mt7620_mii_busy_wait(gsw))
		return -1;

	return 0;
}
EXPORT_SYMBOL_GPL(_mt7620_mii_write);

u32 _mt7620_mii_read(struct mt7620_gsw *gsw, int phy_addr, int phy_reg)
{
	u32 d;

	if (mt7620_mii_busy_wait(gsw))
		return 0xffff;

	mtk_switch_w32(gsw, GSW_MDIO_ACCESS | GSW_MDIO_START | GSW_MDIO_READ |
		(phy_reg << GSW_MDIO_REG_SHIFT) |
		(phy_addr << GSW_MDIO_ADDR_SHIFT),
		MT7620_GSW_REG_PIAC);

	if (mt7620_mii_busy_wait(gsw))
		return 0xffff;

	d = mtk_switch_r32(gsw, MT7620_GSW_REG_PIAC) & 0xffff;

	return d;
}
EXPORT_SYMBOL_GPL(_mt7620_mii_read);

int mt7620_mdio_write(struct mii_bus *bus, int phy_addr, int phy_reg, u16 val)
{
	struct mtk_eth *eth = bus->priv;
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;

	return _mt7620_mii_write(gsw, phy_addr, phy_reg, val);
}

int mt7620_mdio_read(struct mii_bus *bus, int phy_addr, int phy_reg)
{
	struct mtk_eth *eth = bus->priv;
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;

	return _mt7620_mii_read(gsw, phy_addr, phy_reg);
}

void mt7530_mdio_w32(struct mt7620_gsw *gsw, u32 reg, u32 val)
{
	_mt7620_mii_write(gsw, 0x1f, 0x1f, (reg >> 6) & 0x3ff);
	_mt7620_mii_write(gsw, 0x1f, (reg >> 2) & 0xf,  val & 0xffff);
	_mt7620_mii_write(gsw, 0x1f, 0x10, val >> 16);
}
EXPORT_SYMBOL_GPL(mt7530_mdio_w32);

u32 mt7530_mdio_r32(struct mt7620_gsw *gsw, u32 reg)
{
	u16 high, low;

	_mt7620_mii_write(gsw, 0x1f, 0x1f, (reg >> 6) & 0x3ff);
	low = _mt7620_mii_read(gsw, 0x1f, (reg >> 2) & 0xf);
	high = _mt7620_mii_read(gsw, 0x1f, 0x10);

	return (high << 16) | (low & 0xffff);
}
EXPORT_SYMBOL_GPL(mt7530_mdio_r32);

void mt7530_mdio_m32(struct mt7620_gsw *gsw, u32 mask, u32 set, u32 reg)
{
	u32 val = mt7530_mdio_r32(gsw, reg);

	val &= ~mask;
	val |= set;
	mt7530_mdio_w32(gsw, reg, val);
}
EXPORT_SYMBOL_GPL(mt7530_mdio_m32);

static unsigned char *mtk_speed_str(int speed)
{
	switch (speed) {
	case 2:
	case SPEED_1000:
		return "1000";
	case 1:
	case SPEED_100:
		return "100";
	case 0:
	case SPEED_10:
		return "10";
	}

	return "? ";
}

int mt7620_has_carrier(struct mtk_eth *eth)
{
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;
	int i;

	for (i = 0; i < GSW_PORT6; i++)
		if (mt7530_mdio_r32(gsw, GSW_REG_PORT_STATUS(i)) & 0x1)
			return 1;
	return 0;
}

void mt7620_print_link_state(struct mtk_eth *eth, int port, int link,
			     int speed, int duplex)
{
	struct mt7620_gsw *gsw = eth->sw_priv;

	if (link)
		dev_info(gsw->dev, "port %d link up (%sMbps/%s duplex)\n",
			 port, mtk_speed_str(speed),
			 (duplex) ? "Full" : "Half");
	else
		dev_info(gsw->dev, "port %d link down\n", port);
}

void mt7620_mdio_link_adjust(struct mtk_eth *eth, int port)
{
	mt7620_print_link_state(eth, port, eth->link[port],
				eth->phy->speed[port],
				(eth->phy->duplex[port] == DUPLEX_FULL));
}
