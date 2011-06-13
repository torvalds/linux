/*

  Broadcom B43 wireless driver
  IEEE 802.11n HT-PHY support

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

#include <linux/slab.h>

#include "b43.h"
#include "phy_ht.h"
#include "main.h"

/**************************************************
 * Basic PHY ops.
 **************************************************/

static int b43_phy_ht_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht;

	phy_ht = kzalloc(sizeof(*phy_ht), GFP_KERNEL);
	if (!phy_ht)
		return -ENOMEM;
	dev->phy.ht = phy_ht;

	return 0;
}

static void b43_phy_ht_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_ht *phy_ht = phy->ht;

	memset(phy_ht, 0, sizeof(*phy_ht));
}

static void b43_phy_ht_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_ht *phy_ht = phy->ht;

	kfree(phy_ht);
	phy->ht = NULL;
}

static unsigned int b43_phy_ht_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		return 1;
	return 36;
}

/**************************************************
 * R/W ops.
 **************************************************/

static u16 b43_phy_ht_op_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_phy_ht_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static void b43_phy_ht_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				 u16 set)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
}

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_ht = {
	.allocate		= b43_phy_ht_op_allocate,
	.free			= b43_phy_ht_op_free,
	.prepare_structs	= b43_phy_ht_op_prepare_structs,
	/*
	.init			= b43_phy_ht_op_init,
	*/
	.phy_read		= b43_phy_ht_op_read,
	.phy_write		= b43_phy_ht_op_write,
	.phy_maskset		= b43_phy_ht_op_maskset,
	/*
	.radio_read		= b43_phy_ht_op_radio_read,
	.radio_write		= b43_phy_ht_op_radio_write,
	.software_rfkill	= b43_phy_ht_op_software_rfkill,
	.switch_analog		= b43_phy_ht_op_switch_analog,
	.switch_channel		= b43_phy_ht_op_switch_channel,
	*/
	.get_default_chan	= b43_phy_ht_op_get_default_chan,
	/*
	.recalc_txpower		= b43_phy_ht_op_recalc_txpower,
	.adjust_txpower		= b43_phy_ht_op_adjust_txpower,
	*/
};
