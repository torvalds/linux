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
#include "tables_phy_ht.h"
#include "radio_2059.h"
#include "main.h"

/**************************************************
 * Radio 2059.
 **************************************************/

static void b43_radio_2059_channel_setup(struct b43_wldev *dev,
			const struct b43_phy_ht_channeltab_e_radio2059 *e)
{
	u8 i;
	u16 routing;

	b43_radio_write(dev, 0x16, e->radio_syn16);
	b43_radio_write(dev, 0x17, e->radio_syn17);
	b43_radio_write(dev, 0x22, e->radio_syn22);
	b43_radio_write(dev, 0x25, e->radio_syn25);
	b43_radio_write(dev, 0x27, e->radio_syn27);
	b43_radio_write(dev, 0x28, e->radio_syn28);
	b43_radio_write(dev, 0x29, e->radio_syn29);
	b43_radio_write(dev, 0x2c, e->radio_syn2c);
	b43_radio_write(dev, 0x2d, e->radio_syn2d);
	b43_radio_write(dev, 0x37, e->radio_syn37);
	b43_radio_write(dev, 0x41, e->radio_syn41);
	b43_radio_write(dev, 0x43, e->radio_syn43);
	b43_radio_write(dev, 0x47, e->radio_syn47);
	b43_radio_write(dev, 0x4a, e->radio_syn4a);
	b43_radio_write(dev, 0x58, e->radio_syn58);
	b43_radio_write(dev, 0x5a, e->radio_syn5a);
	b43_radio_write(dev, 0x6a, e->radio_syn6a);
	b43_radio_write(dev, 0x6d, e->radio_syn6d);
	b43_radio_write(dev, 0x6e, e->radio_syn6e);
	b43_radio_write(dev, 0x92, e->radio_syn92);
	b43_radio_write(dev, 0x98, e->radio_syn98);

	for (i = 0; i < 2; i++) {
		routing = i ? R2059_RXRX1 : R2059_TXRX0;
		b43_radio_write(dev, routing | 0x4a, e->radio_rxtx4a);
		b43_radio_write(dev, routing | 0x58, e->radio_rxtx58);
		b43_radio_write(dev, routing | 0x5a, e->radio_rxtx5a);
		b43_radio_write(dev, routing | 0x6a, e->radio_rxtx6a);
		b43_radio_write(dev, routing | 0x6d, e->radio_rxtx6d);
		b43_radio_write(dev, routing | 0x6e, e->radio_rxtx6e);
		b43_radio_write(dev, routing | 0x92, e->radio_rxtx92);
		b43_radio_write(dev, routing | 0x98, e->radio_rxtx98);
	}

	udelay(50);

	/* Calibration */
	b43_radio_mask(dev, 0x2b, ~0x1);
	b43_radio_mask(dev, 0x2e, ~0x4);
	b43_radio_set(dev, 0x2e, 0x4);
	b43_radio_set(dev, 0x2b, 0x1);

	udelay(300);
}

static void b43_radio_2059_init(struct b43_wldev *dev)
{
	const u16 routing[] = { R2059_SYN, R2059_TXRX0, R2059_RXRX1 };
	const u16 radio_values[3][2] = {
		{ 0x61, 0xE9 }, { 0x69, 0xD5 }, { 0x73, 0x99 },
	};
	u16 i, j;

	b43_radio_write(dev, R2059_ALL | 0x51, 0x0070);
	b43_radio_write(dev, R2059_ALL | 0x5a, 0x0003);

	for (i = 0; i < ARRAY_SIZE(routing); i++)
		b43_radio_set(dev, routing[i] | 0x146, 0x3);

	b43_radio_set(dev, 0x2e, 0x0078);
	b43_radio_set(dev, 0xc0, 0x0080);
	msleep(2);
	b43_radio_mask(dev, 0x2e, ~0x0078);
	b43_radio_mask(dev, 0xc0, ~0x0080);

	if (1) { /* FIXME */
		b43_radio_set(dev, R2059_RXRX1 | 0x4, 0x1);
		udelay(10);
		b43_radio_set(dev, R2059_RXRX1 | 0x0BF, 0x1);
		b43_radio_maskset(dev, R2059_RXRX1 | 0x19B, 0x3, 0x2);

		b43_radio_set(dev, R2059_RXRX1 | 0x4, 0x2);
		udelay(100);
		b43_radio_mask(dev, R2059_RXRX1 | 0x4, ~0x2);

		for (i = 0; i < 10000; i++) {
			if (b43_radio_read(dev, R2059_RXRX1 | 0x145) & 1) {
				i = 0;
				break;
			}
			udelay(100);
		}
		if (i)
			b43err(dev->wl, "radio 0x945 timeout\n");

		b43_radio_mask(dev, R2059_RXRX1 | 0x4, ~0x1);
		b43_radio_set(dev, 0xa, 0x60);

		for (i = 0; i < 3; i++) {
			b43_radio_write(dev, 0x17F, radio_values[i][0]);
			b43_radio_write(dev, 0x13D, 0x6E);
			b43_radio_write(dev, 0x13E, radio_values[i][1]);
			b43_radio_write(dev, 0x13C, 0x55);

			for (j = 0; j < 10000; j++) {
				if (b43_radio_read(dev, 0x140) & 2) {
					j = 0;
					break;
				}
				udelay(500);
			}
			if (j)
				b43err(dev->wl, "radio 0x140 timeout\n");

			b43_radio_write(dev, 0x13C, 0x15);
		}

		b43_radio_mask(dev, 0x17F, ~0x1);
	}

	b43_radio_mask(dev, 0x11, ~0x0008);
}

/**************************************************
 * Channel switching ops.
 **************************************************/

static void b43_phy_ht_channel_setup(struct b43_wldev *dev,
				const struct b43_phy_ht_channeltab_e_phy *e,
				struct ieee80211_channel *new_channel)
{
	bool old_band_5ghz;
	u8 i;

	old_band_5ghz = b43_phy_read(dev, B43_PHY_HT_BANDCTL) & 0; /* FIXME */
	if (new_channel->band == IEEE80211_BAND_5GHZ && !old_band_5ghz) {
		/* TODO */
	} else if (new_channel->band == IEEE80211_BAND_2GHZ && old_band_5ghz) {
		/* TODO */
	}

	b43_phy_write(dev, B43_PHY_HT_BW1, e->bw1);
	b43_phy_write(dev, B43_PHY_HT_BW2, e->bw2);
	b43_phy_write(dev, B43_PHY_HT_BW3, e->bw3);
	b43_phy_write(dev, B43_PHY_HT_BW4, e->bw4);
	b43_phy_write(dev, B43_PHY_HT_BW5, e->bw5);
	b43_phy_write(dev, B43_PHY_HT_BW6, e->bw6);

	/* TODO: some ops on PHY regs 0x0B0 and 0xC0A */

	/* TODO: separated function? */
	for (i = 0; i < 3; i++) {
		u16 mask;
		u32 tmp = b43_httab_read(dev, B43_HTTAB32(26, 0xE8));

		if (0) /* FIXME */
			mask = 0x2 << (i * 4);
		else
			mask = 0;
		b43_phy_mask(dev, B43_PHY_EXTG(0x108), mask);

		b43_httab_write(dev, B43_HTTAB16(7, 0x110 + i), tmp >> 16);
		b43_httab_write(dev, B43_HTTAB8(13, 0x63 + (i * 4)),
				tmp & 0xFF);
		b43_httab_write(dev, B43_HTTAB8(13, 0x73 + (i * 4)),
				tmp & 0xFF);
	}

	b43_phy_write(dev, 0x017e, 0x3830);
}

static int b43_phy_ht_set_channel(struct b43_wldev *dev,
				  struct ieee80211_channel *channel,
				  enum nl80211_channel_type channel_type)
{
	struct b43_phy *phy = &dev->phy;

	const struct b43_phy_ht_channeltab_e_radio2059 *chent_r2059 = NULL;

	if (phy->radio_ver == 0x2059) {
		chent_r2059 = b43_phy_ht_get_channeltab_e_r2059(dev,
							channel->center_freq);
		if (!chent_r2059)
			return -ESRCH;
	} else {
		return -ESRCH;
	}

	/* TODO: In case of N-PHY some bandwidth switching goes here */

	if (phy->radio_ver == 0x2059) {
		b43_radio_2059_channel_setup(dev, chent_r2059);
		b43_phy_ht_channel_setup(dev, &(chent_r2059->phy_regs),
					 channel);
	} else {
		return -ESRCH;
	}

	return 0;
}

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

static int b43_phy_ht_op_init(struct b43_wldev *dev)
{
	b43_phy_ht_tables_init(dev);

	return 0;
}

static void b43_phy_ht_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_ht *phy_ht = phy->ht;

	kfree(phy_ht);
	phy->ht = NULL;
}

/* http://bcm-v4.sipsolutions.net/802.11/Radio/Switch%20Radio */
static void b43_phy_ht_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{
	if (b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_ENABLED)
		b43err(dev->wl, "MAC not suspended\n");

	/* In the following PHY ops we copy wl's dummy behaviour.
	 * TODO: Find out if reads (currently hidden in masks/masksets) are
	 * needed and replace following ops with just writes or w&r.
	 * Note: B43_PHY_HT_RF_CTL1 register is tricky, wrong operation can
	 * cause delayed (!) machine lock up. */
	if (blocked) {
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
	} else {
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
		b43_phy_maskset(dev, B43_PHY_HT_RF_CTL1, 0, 0x1);
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
		b43_phy_maskset(dev, B43_PHY_HT_RF_CTL1, 0, 0x2);

		if (dev->phy.radio_ver == 0x2059)
			b43_radio_2059_init(dev);
		else
			B43_WARN_ON(1);

		b43_switch_channel(dev, dev->phy.channel);
	}
}

static void b43_phy_ht_op_switch_analog(struct b43_wldev *dev, bool on)
{
	if (on) {
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL2, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL1, 0x0000);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL4, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL3, 0x0000);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL6, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL5, 0x0000);
	} else {
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL1, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL2, 0x00fd);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL3, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL4, 0x00fd);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL5, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_CTL6, 0x00fd);
	}
}

static int b43_phy_ht_op_switch_channel(struct b43_wldev *dev,
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

	return b43_phy_ht_set_channel(dev, channel, channel_type);
}

static unsigned int b43_phy_ht_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		return 11;
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

static u16 b43_phy_ht_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* HT-PHY needs 0x200 for read access */
	reg |= 0x200;

	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO24_DATA);
}

static void b43_phy_ht_op_radio_write(struct b43_wldev *dev, u16 reg,
				      u16 value)
{
	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO24_DATA, value);
}

static enum b43_txpwr_result
b43_phy_ht_op_recalc_txpower(struct b43_wldev *dev, bool ignore_tssi)
{
	return B43_TXPWR_RES_DONE;
}

static void b43_phy_ht_op_adjust_txpower(struct b43_wldev *dev)
{
}

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_ht = {
	.allocate		= b43_phy_ht_op_allocate,
	.free			= b43_phy_ht_op_free,
	.prepare_structs	= b43_phy_ht_op_prepare_structs,
	.init			= b43_phy_ht_op_init,
	.phy_read		= b43_phy_ht_op_read,
	.phy_write		= b43_phy_ht_op_write,
	.phy_maskset		= b43_phy_ht_op_maskset,
	.radio_read		= b43_phy_ht_op_radio_read,
	.radio_write		= b43_phy_ht_op_radio_write,
	.software_rfkill	= b43_phy_ht_op_software_rfkill,
	.switch_analog		= b43_phy_ht_op_switch_analog,
	.switch_channel		= b43_phy_ht_op_switch_channel,
	.get_default_chan	= b43_phy_ht_op_get_default_chan,
	.recalc_txpower		= b43_phy_ht_op_recalc_txpower,
	.adjust_txpower		= b43_phy_ht_op_adjust_txpower,
};
