/*

  Broadcom B43 wireless driver
  IEEE 802.11n HT-PHY data tables

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
#include "tables_phy_ht.h"
#include "phy_common.h"
#include "phy_ht.h"

u32 b43_httab_read(struct b43_wldev *dev, u32 offset)
{
	u32 type, value;

	type = offset & B43_HTTAB_TYPEMASK;
	offset &= ~B43_HTTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	switch (type) {
	case B43_HTTAB_8BIT:
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO) & 0xFF;
		break;
	case B43_HTTAB_16BIT:
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO);
		break;
	case B43_HTTAB_32BIT:
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		value = b43_phy_read(dev, B43_PHY_HT_TABLE_DATAHI);
		value <<= 16;
		value |= b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO);
		break;
	default:
		B43_WARN_ON(1);
		value = 0;
	}

	return value;
}

void b43_httab_read_bulk(struct b43_wldev *dev, u32 offset,
			 unsigned int nr_elements, void *_data)
{
	u32 type;
	u8 *data = _data;
	unsigned int i;

	type = offset & B43_HTTAB_TYPEMASK;
	offset &= ~B43_HTTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);

	for (i = 0; i < nr_elements; i++) {
		switch (type) {
		case B43_HTTAB_8BIT:
			*data = b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO) & 0xFF;
			data++;
			break;
		case B43_HTTAB_16BIT:
			*((u16 *)data) = b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO);
			data += 2;
			break;
		case B43_HTTAB_32BIT:
			*((u32 *)data) = b43_phy_read(dev, B43_PHY_HT_TABLE_DATAHI);
			*((u32 *)data) <<= 16;
			*((u32 *)data) |= b43_phy_read(dev, B43_PHY_HT_TABLE_DATALO);
			data += 4;
			break;
		default:
			B43_WARN_ON(1);
		}
	}
}

void b43_httab_write(struct b43_wldev *dev, u32 offset, u32 value)
{
	u32 type;

	type = offset & B43_HTTAB_TYPEMASK;
	offset &= 0xFFFF;

	switch (type) {
	case B43_HTTAB_8BIT:
		B43_WARN_ON(value & ~0xFF);
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, value);
		break;
	case B43_HTTAB_16BIT:
		B43_WARN_ON(value & ~0xFFFF);
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, value);
		break;
	case B43_HTTAB_32BIT:
		b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATAHI, value >> 16);
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, value & 0xFFFF);
		break;
	default:
		B43_WARN_ON(1);
	}

	return;
}

void b43_httab_write_bulk(struct b43_wldev *dev, u32 offset,
			  unsigned int nr_elements, const void *_data)
{
	u32 type, value;
	const u8 *data = _data;
	unsigned int i;

	type = offset & B43_HTTAB_TYPEMASK;
	offset &= ~B43_HTTAB_TYPEMASK;
	B43_WARN_ON(offset > 0xFFFF);

	b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, offset);

	for (i = 0; i < nr_elements; i++) {
		switch (type) {
		case B43_HTTAB_8BIT:
			value = *data;
			data++;
			B43_WARN_ON(value & ~0xFF);
			b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, value);
			break;
		case B43_HTTAB_16BIT:
			value = *((u16 *)data);
			data += 2;
			B43_WARN_ON(value & ~0xFFFF);
			b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, value);
			break;
		case B43_HTTAB_32BIT:
			value = *((u32 *)data);
			data += 4;
			b43_phy_write(dev, B43_PHY_HT_TABLE_DATAHI, value >> 16);
			b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO,
					value & 0xFFFF);
			break;
		default:
			B43_WARN_ON(1);
		}
	}
}
