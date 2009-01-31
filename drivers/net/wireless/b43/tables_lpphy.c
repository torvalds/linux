/*

  Broadcom B43 wireless driver
  IEEE 802.11g LP-PHY and radio device data tables

  Copyright (c) 2009 Michael Buesch <mb@bu3sch.de>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "b43.h"
#include "tables_lpphy.h"
#include "phy_common.h"
#include "phy_lp.h"


u32 b43_lptab_read(struct b43_wldev *dev, u32 offset)
{
	u32 type, value;

	type = offset & B43_LPTAB_TYPEMASK;
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	switch (type) {
	case B43_LPTAB_8BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATALO) & 0xFF;
		break;
	case B43_LPTAB_16BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
		break;
	case B43_LPTAB_32BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_LPPHY_TABLEDATAHI);
		value <<= 16;
		value |= b43_phy_read(dev, B43_LPPHY_TABLEDATALO);
		break;
	default:
		B43_WARN_ON(1);
		value = 0;
	}

	return value;
}

void b43_lptab_write(struct b43_wldev *dev, u32 offset, u32 value)
{
	u32 type;

	type = offset & B43_LPTAB_TYPEMASK;
	offset &= ~B43_LPTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	switch (type) {
	case B43_LPTAB_8BIT:
		B43_WARN_ON(value & ~0xFF);
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	case B43_LPTAB_16BIT:
		B43_WARN_ON(value & ~0xFFFF);
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	case B43_LPTAB_32BIT:
		b43_phy_write(dev, B43_LPPHY_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_LPPHY_TABLEDATAHI, value >> 16);
		b43_phy_write(dev, B43_LPPHY_TABLEDATALO, value);
		break;
	default:
		B43_WARN_ON(1);
	}
}
