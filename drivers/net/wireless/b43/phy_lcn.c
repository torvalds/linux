/*

  Broadcom B43 wireless driver
  IEEE 802.11n LCN-PHY support

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
#include "phy_lcn.h"
#include "tables_phy_lcn.h"
#include "main.h"

/**************************************************
 * Radio 2064.
 **************************************************/

static void b43_radio_2064_init(struct b43_wldev *dev)
{
	b43_radio_write(dev, 0x09c, 0x0020);
	b43_radio_write(dev, 0x105, 0x0008);
	b43_radio_write(dev, 0x032, 0x0062);
	b43_radio_write(dev, 0x033, 0x0019);
	b43_radio_write(dev, 0x090, 0x0010);
	b43_radio_write(dev, 0x010, 0x0000);
	b43_radio_write(dev, 0x060, 0x007f);
	b43_radio_write(dev, 0x061, 0x0072);
	b43_radio_write(dev, 0x062, 0x007f);
	b43_radio_write(dev, 0x01d, 0x0002);
	b43_radio_write(dev, 0x01e, 0x0006);

	b43_phy_write(dev, 0x4ea, 0x4688);
	b43_phy_maskset(dev, 0x4eb, ~0x7, 0x2);
	b43_phy_mask(dev, 0x4eb, ~0x01c0);
	b43_phy_maskset(dev, 0x4eb, 0xff00, 0x19);

	b43_lcntab_write(dev, B43_LCNTAB16(0x00, 0x55), 0);

	b43_radio_mask(dev, 0x05b, (u16) ~0xff02);
	b43_radio_set(dev, 0x004, 0x40);
	b43_radio_set(dev, 0x120, 0x10);
	b43_radio_set(dev, 0x078, 0x80);
	b43_radio_set(dev, 0x129, 0x2);
	b43_radio_set(dev, 0x057, 0x1);
	b43_radio_set(dev, 0x05b, 0x2);

	/* TODO: wait for some bit to be set */
	b43_radio_read(dev, 0x05c);

	b43_radio_mask(dev, 0x05b, (u16) ~0xff02);
	b43_radio_mask(dev, 0x057, (u16) ~0xff01);

	b43_phy_write(dev, 0x933, 0x2d6b);
	b43_phy_write(dev, 0x934, 0x2d6b);
	b43_phy_write(dev, 0x935, 0x2d6b);
	b43_phy_write(dev, 0x936, 0x2d6b);
	b43_phy_write(dev, 0x937, 0x016b);

	b43_radio_mask(dev, 0x057, (u16) ~0xff02);
	b43_radio_write(dev, 0x0c2, 0x006f);
}

/**************************************************
 * Various PHY ops
 **************************************************/

static void b43_phy_lcn_afe_set_unset(struct b43_wldev *dev)
{
	u16 afe_ctl2 = b43_phy_read(dev, B43_PHY_LCN_AFE_CTL2);
	u16 afe_ctl1 = b43_phy_read(dev, B43_PHY_LCN_AFE_CTL1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2 | 0x1);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1 | 0x1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2 & ~0x1);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1 & ~0x1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1);
}

static void b43_phy_lcn_clean_0x18_table(struct b43_wldev *dev)
{
	u8 i;

	for (i = 0; i < 0x80; i++)
		b43_lcntab_write(dev, B43_LCNTAB32(0x18, i), 0x80000);
}

static void b43_phy_lcn_clear_0x07_table(struct b43_wldev *dev)
{
	u8 i;

	b43_phy_write(dev, B43_PHY_LCN_TABLE_ADDR, (0x7 << 10) | 0x340);
	for (i = 0; i < 30; i++) {
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATAHI, 0);
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATALO, 0);
	}

	b43_phy_write(dev, B43_PHY_LCN_TABLE_ADDR, (0x7 << 10) | 0x80);
	for (i = 0; i < 64; i++) {
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATAHI, 0);
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATALO, 0);
	}
}

/**************************************************
 * Basic PHY ops.
 **************************************************/

static int b43_phy_lcn_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_lcn *phy_lcn;

	phy_lcn = kzalloc(sizeof(*phy_lcn), GFP_KERNEL);
	if (!phy_lcn)
		return -ENOMEM;
	dev->phy.lcn = phy_lcn;

	return 0;
}

static void b43_phy_lcn_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lcn *phy_lcn = phy->lcn;

	kfree(phy_lcn);
	phy->lcn = NULL;
}

static void b43_phy_lcn_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lcn *phy_lcn = phy->lcn;

	memset(phy_lcn, 0, sizeof(*phy_lcn));
}

static int b43_phy_lcn_op_init(struct b43_wldev *dev)
{
	b43_phy_set(dev, 0x44a, 0x80);
	b43_phy_mask(dev, 0x44a, 0x7f);
	b43_phy_set(dev, 0x6d1, 0x80);
	b43_phy_write(dev, 0x6d0, 0x7);

	b43_phy_lcn_afe_set_unset(dev);

	b43_phy_write(dev, 0x60a, 0xa0);
	b43_phy_write(dev, 0x46a, 0x19);
	b43_phy_maskset(dev, 0x663, 0xFF00, 0x64);

	b43_phy_lcn_tables_init(dev);
	/* TODO: various tables ops here */
	b43_phy_lcn_clean_0x18_table(dev);

	/* TODO: some ops here */

	b43_phy_lcn_clear_0x07_table(dev);

	if (dev->phy.radio_ver == 0x2064)
		b43_radio_2064_init(dev);
	else
		B43_WARN_ON(1);

	return 0;
}

static void b43_phy_lcn_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{
	if (b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_ENABLED)
		b43err(dev->wl, "MAC not suspended\n");

	if (blocked) {
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL2, ~0x7c00);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL1, 0x1f00);

		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL5, ~0x7f00);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL4, ~0x2);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL3, 0x808);

		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL7, ~0x8);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL6, 0x8);
	} else {
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL1, ~0x1f00);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL3, ~0x808);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL6, ~0x8);
	}
}

static void b43_phy_lcn_op_switch_analog(struct b43_wldev *dev, bool on)
{
	if (on) {
		b43_phy_mask(dev, B43_PHY_LCN_AFE_CTL1, ~0x7);
	} else {
		b43_phy_set(dev, B43_PHY_LCN_AFE_CTL2, 0x7);
		b43_phy_set(dev, B43_PHY_LCN_AFE_CTL1, 0x7);
	}
}

static unsigned int b43_phy_lcn_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		return 1;
	return 36;
}

static enum b43_txpwr_result
b43_phy_lcn_op_recalc_txpower(struct b43_wldev *dev, bool ignore_tssi)
{
	return B43_TXPWR_RES_DONE;
}

static void b43_phy_lcn_op_adjust_txpower(struct b43_wldev *dev)
{
}

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_lcn = {
	.allocate		= b43_phy_lcn_op_allocate,
	.free			= b43_phy_lcn_op_free,
	.prepare_structs	= b43_phy_lcn_op_prepare_structs,
	.init			= b43_phy_lcn_op_init,
	/*
	.phy_read		= b43_phy_lcn_op_read,
	.phy_write		= b43_phy_lcn_op_write,
	.phy_maskset		= b43_phy_lcn_op_maskset,
	.radio_read		= b43_phy_lcn_op_radio_read,
	.radio_write		= b43_phy_lcn_op_radio_write,
	*/
	.software_rfkill	= b43_phy_lcn_op_software_rfkill,
	.switch_analog		= b43_phy_lcn_op_switch_analog,
	/*
	.switch_channel		= b43_phy_lcn_op_switch_channel,
	*/
	.get_default_chan	= b43_phy_lcn_op_get_default_chan,
	.recalc_txpower		= b43_phy_lcn_op_recalc_txpower,
	.adjust_txpower		= b43_phy_lcn_op_adjust_txpower,
};
