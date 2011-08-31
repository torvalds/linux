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

static void b43_radio_2064_channel_setup(struct b43_wldev *dev)
{
	u16 save[2];

	b43_radio_set(dev, 0x09d, 0x4);
	b43_radio_write(dev, 0x09e, 0xf);

	b43_radio_write(dev, 0x02a, 0xb);
	b43_radio_maskset(dev, 0x030, ~0x3, 0xa);
	b43_radio_maskset(dev, 0x091, ~0x3, 0);
	b43_radio_maskset(dev, 0x038, ~0xf, 0x7);
	b43_radio_maskset(dev, 0x030, ~0xc, 0x8);
	b43_radio_maskset(dev, 0x05e, ~0xf, 0x8);
	b43_radio_maskset(dev, 0x05e, ~0xf0, 0x80);
	b43_radio_write(dev, 0x06c, 0x80);

	save[0] = b43_radio_read(dev, 0x044);
	save[1] = b43_radio_read(dev, 0x12b);

	b43_radio_set(dev, 0x044, 0x7);
	b43_radio_set(dev, 0x12b, 0xe);

	/* TODO */

	b43_radio_write(dev, 0x040, 0xfb);

	b43_radio_write(dev, 0x041, 0x9a);
	b43_radio_write(dev, 0x042, 0xa3);
	b43_radio_write(dev, 0x043, 0x0c);

	/* TODO */

	b43_radio_set(dev, 0x044, 0x0c);
	udelay(1);

	b43_radio_write(dev, 0x044, save[0]);
	b43_radio_write(dev, 0x12b, save[1]);

	b43_radio_write(dev, 0x038, 0x0);
	b43_radio_write(dev, 0x091, 0x7);
}

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
	b43_phy_maskset(dev, 0x46a, 0xff00, 0x19);

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

static void b43_phy_lcn_pre_radio_init(struct b43_wldev *dev)
{
	b43_radio_write(dev, 0x11c, 0);

	b43_phy_write(dev, 0x43b, 0);
	b43_phy_write(dev, 0x43c, 0);
	b43_phy_write(dev, 0x44c, 0);
	b43_phy_write(dev, 0x4e6, 0);
	b43_phy_write(dev, 0x4f9, 0);
	b43_phy_write(dev, 0x4b0, 0);
	b43_phy_write(dev, 0x938, 0);
	b43_phy_write(dev, 0x4b0, 0);
	b43_phy_write(dev, 0x44e, 0);

	b43_phy_set(dev, 0x567, 0x03);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_phy_maskset(dev, 0x634, ~0xff, 0xc);
	b43_phy_maskset(dev, 0x634, ~0xff, 0xa);

	b43_phy_write(dev, 0x910, 0x1);

	b43_phy_maskset(dev, 0x448, ~0x300, 0x100);
	b43_phy_maskset(dev, 0x608, ~0xff, 0x17);
	b43_phy_maskset(dev, 0x604, ~0x7ff, 0x3ea);

	b43_phy_set(dev, 0x805, 0x1);

	b43_phy_maskset(dev, 0x42f, ~0x7, 0x3);
	b43_phy_maskset(dev, 0x030, ~0x7, 0x3);

	b43_phy_write(dev, 0x414, 0x1e10);
	b43_phy_write(dev, 0x415, 0x0640);

	b43_phy_maskset(dev, 0x4df, (u16) ~0xff00, 0xf700);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_phy_maskset(dev, 0x434, ~0xff, 0xfd);
	b43_phy_maskset(dev, 0x420, ~0xff, 0x10);

	b43_radio_set(dev, 0x09b, 0xf0);

	b43_phy_write(dev, 0x7d6, 0x0902);

	/* TODO: more ops */
}

static void b43_phy_lcn_save_configsth_restore(struct b43_wldev *dev)
{
	u8 i;

	u16 save_radio_regs[6][2] = {
		{ 0x007, 0 }, { 0x0ff, 0 }, { 0x11f, 0 }, { 0x005, 0 },
		{ 0x025, 0 }, { 0x112, 0 },
	};
	u16 save_phy_regs[14][2] = {
		{ 0x503, 0 }, { 0x4a4, 0 }, { 0x4d0, 0 }, { 0x4d9, 0 },
		{ 0x4da, 0 }, { 0x4a6, 0 }, { 0x938, 0 }, { 0x939, 0 },
		{ 0x4d8, 0 }, { 0x4d0, 0 }, { 0x4d7, 0 }, { 0x4a5, 0 },
		{ 0x40d, 0 }, { 0x4a2, 0 },
	};
	u16 save_radio_4a4;

	for (i = 0; i < 6; i++)
		save_radio_regs[i][1] = b43_radio_read(dev,
						       save_radio_regs[i][0]);
	for (i = 0; i < 14; i++)
		save_phy_regs[i][1] = b43_phy_read(dev, save_phy_regs[i][0]);
	save_radio_4a4 = b43_radio_read(dev, 0x4a4);

	/* TODO: config sth */

	for (i = 0; i < 6; i++)
		b43_radio_write(dev, save_radio_regs[i][0],
				save_radio_regs[i][1]);
	for (i = 0; i < 14; i++)
		b43_phy_write(dev, save_phy_regs[i][0], save_phy_regs[i][1]);
	b43_radio_write(dev, 0x4a4, save_radio_4a4);
}

/**************************************************
 * Channel switching ops.
 **************************************************/

static int b43_phy_lcn_set_channel(struct b43_wldev *dev,
				   struct ieee80211_channel *channel,
				   enum nl80211_channel_type channel_type)
{
	/* TODO: PLL and PHY ops */

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_radio_2064_channel_setup(dev);
	mdelay(1);

	b43_phy_lcn_afe_set_unset(dev);

	/* TODO */

	return 0;
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

	b43_phy_lcn_pre_radio_init(dev);
	b43_phy_lcn_clear_0x07_table(dev);

	if (dev->phy.radio_ver == 0x2064)
		b43_radio_2064_init(dev);
	else
		B43_WARN_ON(1);

	b43_phy_lcn_save_configsth_restore(dev);

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

static int b43_phy_lcn_op_switch_channel(struct b43_wldev *dev,
					unsigned int new_channel)
{
	struct ieee80211_channel *channel = dev->wl->hw->conf.channel;
	enum nl80211_channel_type channel_type = dev->wl->hw->conf.channel_type;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return b43_phy_lcn_set_channel(dev, channel, channel_type);
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
 * R/W ops.
 **************************************************/

static u16 b43_phy_lcn_op_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_phy_lcn_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static void b43_phy_lcn_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				   u16 set)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
}

static u16 b43_phy_lcn_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* LCN-PHY needs 0x200 for read access */
	reg |= 0x200;

	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO24_DATA);
}

static void b43_phy_lcn_op_radio_write(struct b43_wldev *dev, u16 reg,
				       u16 value)
{
	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO24_DATA, value);
}

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_lcn = {
	.allocate		= b43_phy_lcn_op_allocate,
	.free			= b43_phy_lcn_op_free,
	.prepare_structs	= b43_phy_lcn_op_prepare_structs,
	.init			= b43_phy_lcn_op_init,
	.phy_read		= b43_phy_lcn_op_read,
	.phy_write		= b43_phy_lcn_op_write,
	.phy_maskset		= b43_phy_lcn_op_maskset,
	.radio_read		= b43_phy_lcn_op_radio_read,
	.radio_write		= b43_phy_lcn_op_radio_write,
	.software_rfkill	= b43_phy_lcn_op_software_rfkill,
	.switch_analog		= b43_phy_lcn_op_switch_analog,
	.switch_channel		= b43_phy_lcn_op_switch_channel,
	.get_default_chan	= b43_phy_lcn_op_get_default_chan,
	.recalc_txpower		= b43_phy_lcn_op_recalc_txpower,
	.adjust_txpower		= b43_phy_lcn_op_adjust_txpower,
};
