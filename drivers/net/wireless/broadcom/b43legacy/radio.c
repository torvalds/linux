/*

  Broadcom B43legacy wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
		     Stefano Brivio <stefano.brivio@polimi.it>
		     Michael Buesch <m@bues.ch>
		     Danny van Dyk <kugelfang@gentoo.org>
		     Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2007 Larry Finger <Larry.Finger@lwfinger.net>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.

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

#include <linux/delay.h>

#include "b43legacy.h"
#include "main.h"
#include "phy.h"
#include "radio.h"
#include "ilt.h"


/* Table for b43legacy_radio_calibrationvalue() */
static const u16 rcc_table[16] = {
	0x0002, 0x0003, 0x0001, 0x000F,
	0x0006, 0x0007, 0x0005, 0x000F,
	0x000A, 0x000B, 0x0009, 0x000F,
	0x000E, 0x000F, 0x000D, 0x000F,
};

/* Reverse the bits of a 4bit value.
 * Example:  1101 is flipped 1011
 */
static u16 flip_4bit(u16 value)
{
	u16 flipped = 0x0000;

	B43legacy_BUG_ON(!((value & ~0x000F) == 0x0000));

	flipped |= (value & 0x0001) << 3;
	flipped |= (value & 0x0002) << 1;
	flipped |= (value & 0x0004) >> 1;
	flipped |= (value & 0x0008) >> 3;

	return flipped;
}

/* Get the freq, as it has to be written to the device. */
static inline
u16 channel2freq_bg(u8 channel)
{
	/* Frequencies are given as frequencies_bg[index] + 2.4GHz
	 * Starting with channel 1
	 */
	static const u16 frequencies_bg[14] = {
		12, 17, 22, 27,
		32, 37, 42, 47,
		52, 57, 62, 67,
		72, 84,
	};

	if (unlikely(channel < 1 || channel > 14)) {
		printk(KERN_INFO "b43legacy: Channel %d is out of range\n",
				  channel);
		dump_stack();
		return 2412;
	}

	return frequencies_bg[channel - 1];
}

void b43legacy_radio_lock(struct b43legacy_wldev *dev)
{
	u32 status;

	status = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	B43legacy_WARN_ON(status & B43legacy_MACCTL_RADIOLOCK);
	status |= B43legacy_MACCTL_RADIOLOCK;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, status);
	mmiowb();
	udelay(10);
}

void b43legacy_radio_unlock(struct b43legacy_wldev *dev)
{
	u32 status;

	b43legacy_read16(dev, B43legacy_MMIO_PHY_VER); /* dummy read */
	status = b43legacy_read32(dev, B43legacy_MMIO_MACCTL);
	B43legacy_WARN_ON(!(status & B43legacy_MACCTL_RADIOLOCK));
	status &= ~B43legacy_MACCTL_RADIOLOCK;
	b43legacy_write32(dev, B43legacy_MMIO_MACCTL, status);
	mmiowb();
}

u16 b43legacy_radio_read16(struct b43legacy_wldev *dev, u16 offset)
{
	struct b43legacy_phy *phy = &dev->phy;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
		if (phy->radio_ver == 0x2053) {
			if (offset < 0x70)
				offset += 0x80;
			else if (offset < 0x80)
				offset += 0x70;
		} else if (phy->radio_ver == 0x2050)
			offset |= 0x80;
		else
			B43legacy_WARN_ON(1);
		break;
	case B43legacy_PHYTYPE_G:
		offset |= 0x80;
		break;
	default:
		B43legacy_BUG_ON(1);
	}

	b43legacy_write16(dev, B43legacy_MMIO_RADIO_CONTROL, offset);
	return b43legacy_read16(dev, B43legacy_MMIO_RADIO_DATA_LOW);
}

void b43legacy_radio_write16(struct b43legacy_wldev *dev, u16 offset, u16 val)
{
	b43legacy_write16(dev, B43legacy_MMIO_RADIO_CONTROL, offset);
	mmiowb();
	b43legacy_write16(dev, B43legacy_MMIO_RADIO_DATA_LOW, val);
}

static void b43legacy_set_all_gains(struct b43legacy_wldev *dev,
				  s16 first, s16 second, s16 third)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 i;
	u16 start = 0x08;
	u16 end = 0x18;
	u16 offset = 0x0400;
	u16 tmp;

	if (phy->rev <= 1) {
		offset = 0x5000;
		start = 0x10;
		end = 0x20;
	}

	for (i = 0; i < 4; i++)
		b43legacy_ilt_write(dev, offset + i, first);

	for (i = start; i < end; i++)
		b43legacy_ilt_write(dev, offset + i, second);

	if (third != -1) {
		tmp = ((u16)third << 14) | ((u16)third << 6);
		b43legacy_phy_write(dev, 0x04A0,
				    (b43legacy_phy_read(dev, 0x04A0) & 0xBFBF)
				    | tmp);
		b43legacy_phy_write(dev, 0x04A1,
				    (b43legacy_phy_read(dev, 0x04A1) & 0xBFBF)
				    | tmp);
		b43legacy_phy_write(dev, 0x04A2,
				    (b43legacy_phy_read(dev, 0x04A2) & 0xBFBF)
				    | tmp);
	}
	b43legacy_dummy_transmission(dev);
}

static void b43legacy_set_original_gains(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 i;
	u16 tmp;
	u16 offset = 0x0400;
	u16 start = 0x0008;
	u16 end = 0x0018;

	if (phy->rev <= 1) {
		offset = 0x5000;
		start = 0x0010;
		end = 0x0020;
	}

	for (i = 0; i < 4; i++) {
		tmp = (i & 0xFFFC);
		tmp |= (i & 0x0001) << 1;
		tmp |= (i & 0x0002) >> 1;

		b43legacy_ilt_write(dev, offset + i, tmp);
	}

	for (i = start; i < end; i++)
		b43legacy_ilt_write(dev, offset + i, i - start);

	b43legacy_phy_write(dev, 0x04A0,
			    (b43legacy_phy_read(dev, 0x04A0) & 0xBFBF)
			    | 0x4040);
	b43legacy_phy_write(dev, 0x04A1,
			    (b43legacy_phy_read(dev, 0x04A1) & 0xBFBF)
			    | 0x4040);
	b43legacy_phy_write(dev, 0x04A2,
			    (b43legacy_phy_read(dev, 0x04A2) & 0xBFBF)
			    | 0x4000);
	b43legacy_dummy_transmission(dev);
}

/* Synthetic PU workaround */
static void b43legacy_synth_pu_workaround(struct b43legacy_wldev *dev,
					  u8 channel)
{
	struct b43legacy_phy *phy = &dev->phy;

	might_sleep();

	if (phy->radio_ver != 0x2050 || phy->radio_rev >= 6)
		/* We do not need the workaround. */
		return;

	if (channel <= 10)
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL,
				  channel2freq_bg(channel + 4));
	else
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL,
				  channel2freq_bg(channel));
	msleep(1);
	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL,
			  channel2freq_bg(channel));
}

u8 b43legacy_radio_aci_detect(struct b43legacy_wldev *dev, u8 channel)
{
	struct b43legacy_phy *phy = &dev->phy;
	u8 ret = 0;
	u16 saved;
	u16 rssi;
	u16 temp;
	int i;
	int j = 0;

	saved = b43legacy_phy_read(dev, 0x0403);
	b43legacy_radio_selectchannel(dev, channel, 0);
	b43legacy_phy_write(dev, 0x0403, (saved & 0xFFF8) | 5);
	if (phy->aci_hw_rssi)
		rssi = b43legacy_phy_read(dev, 0x048A) & 0x3F;
	else
		rssi = saved & 0x3F;
	/* clamp temp to signed 5bit */
	if (rssi > 32)
		rssi -= 64;
	for (i = 0; i < 100; i++) {
		temp = (b43legacy_phy_read(dev, 0x047F) >> 8) & 0x3F;
		if (temp > 32)
			temp -= 64;
		if (temp < rssi)
			j++;
		if (j >= 20)
			ret = 1;
	}
	b43legacy_phy_write(dev, 0x0403, saved);

	return ret;
}

u8 b43legacy_radio_aci_scan(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u8 ret[13];
	unsigned int channel = phy->channel;
	unsigned int i;
	unsigned int j;
	unsigned int start;
	unsigned int end;

	if (!((phy->type == B43legacy_PHYTYPE_G) && (phy->rev > 0)))
		return 0;

	b43legacy_phy_lock(dev);
	b43legacy_radio_lock(dev);
	b43legacy_phy_write(dev, 0x0802,
			    b43legacy_phy_read(dev, 0x0802) & 0xFFFC);
	b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
			    b43legacy_phy_read(dev, B43legacy_PHY_G_CRS)
			    & 0x7FFF);
	b43legacy_set_all_gains(dev, 3, 8, 1);

	start = (channel - 5 > 0) ? channel - 5 : 1;
	end = (channel + 5 < 14) ? channel + 5 : 13;

	for (i = start; i <= end; i++) {
		if (abs(channel - i) > 2)
			ret[i-1] = b43legacy_radio_aci_detect(dev, i);
	}
	b43legacy_radio_selectchannel(dev, channel, 0);
	b43legacy_phy_write(dev, 0x0802,
			    (b43legacy_phy_read(dev, 0x0802) & 0xFFFC)
			    | 0x0003);
	b43legacy_phy_write(dev, 0x0403,
			    b43legacy_phy_read(dev, 0x0403) & 0xFFF8);
	b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
			    b43legacy_phy_read(dev, B43legacy_PHY_G_CRS)
			    | 0x8000);
	b43legacy_set_original_gains(dev);
	for (i = 0; i < 13; i++) {
		if (!ret[i])
			continue;
		end = (i + 5 < 13) ? i + 5 : 13;
		for (j = i; j < end; j++)
			ret[j] = 1;
	}
	b43legacy_radio_unlock(dev);
	b43legacy_phy_unlock(dev);

	return ret[channel - 1];
}

/* http://bcm-specs.sipsolutions.net/NRSSILookupTable */
void b43legacy_nrssi_hw_write(struct b43legacy_wldev *dev, u16 offset, s16 val)
{
	b43legacy_phy_write(dev, B43legacy_PHY_NRSSILT_CTRL, offset);
	mmiowb();
	b43legacy_phy_write(dev, B43legacy_PHY_NRSSILT_DATA, (u16)val);
}

/* http://bcm-specs.sipsolutions.net/NRSSILookupTable */
s16 b43legacy_nrssi_hw_read(struct b43legacy_wldev *dev, u16 offset)
{
	u16 val;

	b43legacy_phy_write(dev, B43legacy_PHY_NRSSILT_CTRL, offset);
	val = b43legacy_phy_read(dev, B43legacy_PHY_NRSSILT_DATA);

	return (s16)val;
}

/* http://bcm-specs.sipsolutions.net/NRSSILookupTable */
void b43legacy_nrssi_hw_update(struct b43legacy_wldev *dev, u16 val)
{
	u16 i;
	s16 tmp;

	for (i = 0; i < 64; i++) {
		tmp = b43legacy_nrssi_hw_read(dev, i);
		tmp -= val;
		tmp = clamp_val(tmp, -32, 31);
		b43legacy_nrssi_hw_write(dev, i, tmp);
	}
}

/* http://bcm-specs.sipsolutions.net/NRSSILookupTable */
void b43legacy_nrssi_mem_update(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	s16 i;
	s16 delta;
	s32 tmp;

	delta = 0x1F - phy->nrssi[0];
	for (i = 0; i < 64; i++) {
		tmp = (i - delta) * phy->nrssislope;
		tmp /= 0x10000;
		tmp += 0x3A;
		tmp = clamp_val(tmp, 0, 0x3F);
		phy->nrssi_lt[i] = tmp;
	}
}

static void b43legacy_calc_nrssi_offset(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 backup[20] = { 0 };
	s16 v47F;
	u16 i;
	u16 saved = 0xFFFF;

	backup[0] = b43legacy_phy_read(dev, 0x0001);
	backup[1] = b43legacy_phy_read(dev, 0x0811);
	backup[2] = b43legacy_phy_read(dev, 0x0812);
	backup[3] = b43legacy_phy_read(dev, 0x0814);
	backup[4] = b43legacy_phy_read(dev, 0x0815);
	backup[5] = b43legacy_phy_read(dev, 0x005A);
	backup[6] = b43legacy_phy_read(dev, 0x0059);
	backup[7] = b43legacy_phy_read(dev, 0x0058);
	backup[8] = b43legacy_phy_read(dev, 0x000A);
	backup[9] = b43legacy_phy_read(dev, 0x0003);
	backup[10] = b43legacy_radio_read16(dev, 0x007A);
	backup[11] = b43legacy_radio_read16(dev, 0x0043);

	b43legacy_phy_write(dev, 0x0429,
			    b43legacy_phy_read(dev, 0x0429) & 0x7FFF);
	b43legacy_phy_write(dev, 0x0001,
			    (b43legacy_phy_read(dev, 0x0001) & 0x3FFF)
			    | 0x4000);
	b43legacy_phy_write(dev, 0x0811,
			    b43legacy_phy_read(dev, 0x0811) | 0x000C);
	b43legacy_phy_write(dev, 0x0812,
			    (b43legacy_phy_read(dev, 0x0812) & 0xFFF3)
			    | 0x0004);
	b43legacy_phy_write(dev, 0x0802,
			    b43legacy_phy_read(dev, 0x0802) & ~(0x1 | 0x2));
	if (phy->rev >= 6) {
		backup[12] = b43legacy_phy_read(dev, 0x002E);
		backup[13] = b43legacy_phy_read(dev, 0x002F);
		backup[14] = b43legacy_phy_read(dev, 0x080F);
		backup[15] = b43legacy_phy_read(dev, 0x0810);
		backup[16] = b43legacy_phy_read(dev, 0x0801);
		backup[17] = b43legacy_phy_read(dev, 0x0060);
		backup[18] = b43legacy_phy_read(dev, 0x0014);
		backup[19] = b43legacy_phy_read(dev, 0x0478);

		b43legacy_phy_write(dev, 0x002E, 0);
		b43legacy_phy_write(dev, 0x002F, 0);
		b43legacy_phy_write(dev, 0x080F, 0);
		b43legacy_phy_write(dev, 0x0810, 0);
		b43legacy_phy_write(dev, 0x0478,
				    b43legacy_phy_read(dev, 0x0478) | 0x0100);
		b43legacy_phy_write(dev, 0x0801,
				    b43legacy_phy_read(dev, 0x0801) | 0x0040);
		b43legacy_phy_write(dev, 0x0060,
				    b43legacy_phy_read(dev, 0x0060) | 0x0040);
		b43legacy_phy_write(dev, 0x0014,
				    b43legacy_phy_read(dev, 0x0014) | 0x0200);
	}
	b43legacy_radio_write16(dev, 0x007A,
				b43legacy_radio_read16(dev, 0x007A) | 0x0070);
	b43legacy_radio_write16(dev, 0x007A,
				b43legacy_radio_read16(dev, 0x007A) | 0x0080);
	udelay(30);

	v47F = (s16)((b43legacy_phy_read(dev, 0x047F) >> 8) & 0x003F);
	if (v47F >= 0x20)
		v47F -= 0x40;
	if (v47F == 31) {
		for (i = 7; i >= 4; i--) {
			b43legacy_radio_write16(dev, 0x007B, i);
			udelay(20);
			v47F = (s16)((b43legacy_phy_read(dev, 0x047F) >> 8)
							 & 0x003F);
			if (v47F >= 0x20)
				v47F -= 0x40;
			if (v47F < 31 && saved == 0xFFFF)
				saved = i;
		}
		if (saved == 0xFFFF)
			saved = 4;
	} else {
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					& 0x007F);
		b43legacy_phy_write(dev, 0x0814,
				    b43legacy_phy_read(dev, 0x0814) | 0x0001);
		b43legacy_phy_write(dev, 0x0815,
				    b43legacy_phy_read(dev, 0x0815) & 0xFFFE);
		b43legacy_phy_write(dev, 0x0811,
				    b43legacy_phy_read(dev, 0x0811) | 0x000C);
		b43legacy_phy_write(dev, 0x0812,
				    b43legacy_phy_read(dev, 0x0812) | 0x000C);
		b43legacy_phy_write(dev, 0x0811,
				    b43legacy_phy_read(dev, 0x0811) | 0x0030);
		b43legacy_phy_write(dev, 0x0812,
				    b43legacy_phy_read(dev, 0x0812) | 0x0030);
		b43legacy_phy_write(dev, 0x005A, 0x0480);
		b43legacy_phy_write(dev, 0x0059, 0x0810);
		b43legacy_phy_write(dev, 0x0058, 0x000D);
		if (phy->analog == 0)
			b43legacy_phy_write(dev, 0x0003, 0x0122);
		else
			b43legacy_phy_write(dev, 0x000A,
					    b43legacy_phy_read(dev, 0x000A)
					    | 0x2000);
		b43legacy_phy_write(dev, 0x0814,
				    b43legacy_phy_read(dev, 0x0814) | 0x0004);
		b43legacy_phy_write(dev, 0x0815,
				    b43legacy_phy_read(dev, 0x0815) & 0xFFFB);
		b43legacy_phy_write(dev, 0x0003,
				    (b43legacy_phy_read(dev, 0x0003) & 0xFF9F)
				    | 0x0040);
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x000F);
		b43legacy_set_all_gains(dev, 3, 0, 1);
		b43legacy_radio_write16(dev, 0x0043,
					(b43legacy_radio_read16(dev, 0x0043)
					& 0x00F0) | 0x000F);
		udelay(30);
		v47F = (s16)((b43legacy_phy_read(dev, 0x047F) >> 8) & 0x003F);
		if (v47F >= 0x20)
			v47F -= 0x40;
		if (v47F == -32) {
			for (i = 0; i < 4; i++) {
				b43legacy_radio_write16(dev, 0x007B, i);
				udelay(20);
				v47F = (s16)((b43legacy_phy_read(dev, 0x047F) >>
								 8) & 0x003F);
				if (v47F >= 0x20)
					v47F -= 0x40;
				if (v47F > -31 && saved == 0xFFFF)
					saved = i;
			}
			if (saved == 0xFFFF)
				saved = 3;
		} else
			saved = 0;
	}
	b43legacy_radio_write16(dev, 0x007B, saved);

	if (phy->rev >= 6) {
		b43legacy_phy_write(dev, 0x002E, backup[12]);
		b43legacy_phy_write(dev, 0x002F, backup[13]);
		b43legacy_phy_write(dev, 0x080F, backup[14]);
		b43legacy_phy_write(dev, 0x0810, backup[15]);
	}
	b43legacy_phy_write(dev, 0x0814, backup[3]);
	b43legacy_phy_write(dev, 0x0815, backup[4]);
	b43legacy_phy_write(dev, 0x005A, backup[5]);
	b43legacy_phy_write(dev, 0x0059, backup[6]);
	b43legacy_phy_write(dev, 0x0058, backup[7]);
	b43legacy_phy_write(dev, 0x000A, backup[8]);
	b43legacy_phy_write(dev, 0x0003, backup[9]);
	b43legacy_radio_write16(dev, 0x0043, backup[11]);
	b43legacy_radio_write16(dev, 0x007A, backup[10]);
	b43legacy_phy_write(dev, 0x0802,
			    b43legacy_phy_read(dev, 0x0802) | 0x1 | 0x2);
	b43legacy_phy_write(dev, 0x0429,
			    b43legacy_phy_read(dev, 0x0429) | 0x8000);
	b43legacy_set_original_gains(dev);
	if (phy->rev >= 6) {
		b43legacy_phy_write(dev, 0x0801, backup[16]);
		b43legacy_phy_write(dev, 0x0060, backup[17]);
		b43legacy_phy_write(dev, 0x0014, backup[18]);
		b43legacy_phy_write(dev, 0x0478, backup[19]);
	}
	b43legacy_phy_write(dev, 0x0001, backup[0]);
	b43legacy_phy_write(dev, 0x0812, backup[2]);
	b43legacy_phy_write(dev, 0x0811, backup[1]);
}

void b43legacy_calc_nrssi_slope(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 backup[18] = { 0 };
	u16 tmp;
	s16 nrssi0;
	s16 nrssi1;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
		backup[0] = b43legacy_radio_read16(dev, 0x007A);
		backup[1] = b43legacy_radio_read16(dev, 0x0052);
		backup[2] = b43legacy_radio_read16(dev, 0x0043);
		backup[3] = b43legacy_phy_read(dev, 0x0030);
		backup[4] = b43legacy_phy_read(dev, 0x0026);
		backup[5] = b43legacy_phy_read(dev, 0x0015);
		backup[6] = b43legacy_phy_read(dev, 0x002A);
		backup[7] = b43legacy_phy_read(dev, 0x0020);
		backup[8] = b43legacy_phy_read(dev, 0x005A);
		backup[9] = b43legacy_phy_read(dev, 0x0059);
		backup[10] = b43legacy_phy_read(dev, 0x0058);
		backup[11] = b43legacy_read16(dev, 0x03E2);
		backup[12] = b43legacy_read16(dev, 0x03E6);
		backup[13] = b43legacy_read16(dev, B43legacy_MMIO_CHANNEL_EXT);

		tmp  = b43legacy_radio_read16(dev, 0x007A);
		tmp &= (phy->rev >= 5) ? 0x007F : 0x000F;
		b43legacy_radio_write16(dev, 0x007A, tmp);
		b43legacy_phy_write(dev, 0x0030, 0x00FF);
		b43legacy_write16(dev, 0x03EC, 0x7F7F);
		b43legacy_phy_write(dev, 0x0026, 0x0000);
		b43legacy_phy_write(dev, 0x0015,
				    b43legacy_phy_read(dev, 0x0015) | 0x0020);
		b43legacy_phy_write(dev, 0x002A, 0x08A3);
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x0080);

		nrssi0 = (s16)b43legacy_phy_read(dev, 0x0027);
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					& 0x007F);
		if (phy->analog >= 2)
			b43legacy_write16(dev, 0x03E6, 0x0040);
		else if (phy->analog == 0)
			b43legacy_write16(dev, 0x03E6, 0x0122);
		else
			b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT,
					  b43legacy_read16(dev,
					  B43legacy_MMIO_CHANNEL_EXT) & 0x2000);
		b43legacy_phy_write(dev, 0x0020, 0x3F3F);
		b43legacy_phy_write(dev, 0x0015, 0xF330);
		b43legacy_radio_write16(dev, 0x005A, 0x0060);
		b43legacy_radio_write16(dev, 0x0043,
					b43legacy_radio_read16(dev, 0x0043)
					& 0x00F0);
		b43legacy_phy_write(dev, 0x005A, 0x0480);
		b43legacy_phy_write(dev, 0x0059, 0x0810);
		b43legacy_phy_write(dev, 0x0058, 0x000D);
		udelay(20);

		nrssi1 = (s16)b43legacy_phy_read(dev, 0x0027);
		b43legacy_phy_write(dev, 0x0030, backup[3]);
		b43legacy_radio_write16(dev, 0x007A, backup[0]);
		b43legacy_write16(dev, 0x03E2, backup[11]);
		b43legacy_phy_write(dev, 0x0026, backup[4]);
		b43legacy_phy_write(dev, 0x0015, backup[5]);
		b43legacy_phy_write(dev, 0x002A, backup[6]);
		b43legacy_synth_pu_workaround(dev, phy->channel);
		if (phy->analog != 0)
			b43legacy_write16(dev, 0x03F4, backup[13]);

		b43legacy_phy_write(dev, 0x0020, backup[7]);
		b43legacy_phy_write(dev, 0x005A, backup[8]);
		b43legacy_phy_write(dev, 0x0059, backup[9]);
		b43legacy_phy_write(dev, 0x0058, backup[10]);
		b43legacy_radio_write16(dev, 0x0052, backup[1]);
		b43legacy_radio_write16(dev, 0x0043, backup[2]);

		if (nrssi0 == nrssi1)
			phy->nrssislope = 0x00010000;
		else
			phy->nrssislope = 0x00400000 / (nrssi0 - nrssi1);

		if (nrssi0 <= -4) {
			phy->nrssi[0] = nrssi0;
			phy->nrssi[1] = nrssi1;
		}
		break;
	case B43legacy_PHYTYPE_G:
		if (phy->radio_rev >= 9)
			return;
		if (phy->radio_rev == 8)
			b43legacy_calc_nrssi_offset(dev);

		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
				    b43legacy_phy_read(dev, B43legacy_PHY_G_CRS)
				    & 0x7FFF);
		b43legacy_phy_write(dev, 0x0802,
				    b43legacy_phy_read(dev, 0x0802) & 0xFFFC);
		backup[7] = b43legacy_read16(dev, 0x03E2);
		b43legacy_write16(dev, 0x03E2,
				  b43legacy_read16(dev, 0x03E2) | 0x8000);
		backup[0] = b43legacy_radio_read16(dev, 0x007A);
		backup[1] = b43legacy_radio_read16(dev, 0x0052);
		backup[2] = b43legacy_radio_read16(dev, 0x0043);
		backup[3] = b43legacy_phy_read(dev, 0x0015);
		backup[4] = b43legacy_phy_read(dev, 0x005A);
		backup[5] = b43legacy_phy_read(dev, 0x0059);
		backup[6] = b43legacy_phy_read(dev, 0x0058);
		backup[8] = b43legacy_read16(dev, 0x03E6);
		backup[9] = b43legacy_read16(dev, B43legacy_MMIO_CHANNEL_EXT);
		if (phy->rev >= 3) {
			backup[10] = b43legacy_phy_read(dev, 0x002E);
			backup[11] = b43legacy_phy_read(dev, 0x002F);
			backup[12] = b43legacy_phy_read(dev, 0x080F);
			backup[13] = b43legacy_phy_read(dev,
						B43legacy_PHY_G_LO_CONTROL);
			backup[14] = b43legacy_phy_read(dev, 0x0801);
			backup[15] = b43legacy_phy_read(dev, 0x0060);
			backup[16] = b43legacy_phy_read(dev, 0x0014);
			backup[17] = b43legacy_phy_read(dev, 0x0478);
			b43legacy_phy_write(dev, 0x002E, 0);
			b43legacy_phy_write(dev, B43legacy_PHY_G_LO_CONTROL, 0);
			switch (phy->rev) {
			case 4: case 6: case 7:
				b43legacy_phy_write(dev, 0x0478,
						    b43legacy_phy_read(dev,
						    0x0478) | 0x0100);
				b43legacy_phy_write(dev, 0x0801,
						    b43legacy_phy_read(dev,
						    0x0801) | 0x0040);
				break;
			case 3: case 5:
				b43legacy_phy_write(dev, 0x0801,
						    b43legacy_phy_read(dev,
						    0x0801) & 0xFFBF);
				break;
			}
			b43legacy_phy_write(dev, 0x0060,
					    b43legacy_phy_read(dev, 0x0060)
					    | 0x0040);
			b43legacy_phy_write(dev, 0x0014,
					    b43legacy_phy_read(dev, 0x0014)
					    | 0x0200);
		}
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x0070);
		b43legacy_set_all_gains(dev, 0, 8, 0);
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					& 0x00F7);
		if (phy->rev >= 2) {
			b43legacy_phy_write(dev, 0x0811,
					    (b43legacy_phy_read(dev, 0x0811)
					    & 0xFFCF) | 0x0030);
			b43legacy_phy_write(dev, 0x0812,
					    (b43legacy_phy_read(dev, 0x0812)
					    & 0xFFCF) | 0x0010);
		}
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x0080);
		udelay(20);

		nrssi0 = (s16)((b43legacy_phy_read(dev, 0x047F) >> 8) & 0x003F);
		if (nrssi0 >= 0x0020)
			nrssi0 -= 0x0040;

		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					& 0x007F);
		if (phy->analog >= 2)
			b43legacy_phy_write(dev, 0x0003,
					    (b43legacy_phy_read(dev, 0x0003)
					    & 0xFF9F) | 0x0040);

		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT,
				  b43legacy_read16(dev,
				  B43legacy_MMIO_CHANNEL_EXT) | 0x2000);
		b43legacy_radio_write16(dev, 0x007A,
					b43legacy_radio_read16(dev, 0x007A)
					| 0x000F);
		b43legacy_phy_write(dev, 0x0015, 0xF330);
		if (phy->rev >= 2) {
			b43legacy_phy_write(dev, 0x0812,
					    (b43legacy_phy_read(dev, 0x0812)
					    & 0xFFCF) | 0x0020);
			b43legacy_phy_write(dev, 0x0811,
					    (b43legacy_phy_read(dev, 0x0811)
					    & 0xFFCF) | 0x0020);
		}

		b43legacy_set_all_gains(dev, 3, 0, 1);
		if (phy->radio_rev == 8)
			b43legacy_radio_write16(dev, 0x0043, 0x001F);
		else {
			tmp = b43legacy_radio_read16(dev, 0x0052) & 0xFF0F;
			b43legacy_radio_write16(dev, 0x0052, tmp | 0x0060);
			tmp = b43legacy_radio_read16(dev, 0x0043) & 0xFFF0;
			b43legacy_radio_write16(dev, 0x0043, tmp | 0x0009);
		}
		b43legacy_phy_write(dev, 0x005A, 0x0480);
		b43legacy_phy_write(dev, 0x0059, 0x0810);
		b43legacy_phy_write(dev, 0x0058, 0x000D);
		udelay(20);
		nrssi1 = (s16)((b43legacy_phy_read(dev, 0x047F) >> 8) & 0x003F);
		if (nrssi1 >= 0x0020)
			nrssi1 -= 0x0040;
		if (nrssi0 == nrssi1)
			phy->nrssislope = 0x00010000;
		else
			phy->nrssislope = 0x00400000 / (nrssi0 - nrssi1);
		if (nrssi0 >= -4) {
			phy->nrssi[0] = nrssi1;
			phy->nrssi[1] = nrssi0;
		}
		if (phy->rev >= 3) {
			b43legacy_phy_write(dev, 0x002E, backup[10]);
			b43legacy_phy_write(dev, 0x002F, backup[11]);
			b43legacy_phy_write(dev, 0x080F, backup[12]);
			b43legacy_phy_write(dev, B43legacy_PHY_G_LO_CONTROL,
					    backup[13]);
		}
		if (phy->rev >= 2) {
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_phy_read(dev, 0x0812)
					    & 0xFFCF);
			b43legacy_phy_write(dev, 0x0811,
					    b43legacy_phy_read(dev, 0x0811)
					    & 0xFFCF);
		}

		b43legacy_radio_write16(dev, 0x007A, backup[0]);
		b43legacy_radio_write16(dev, 0x0052, backup[1]);
		b43legacy_radio_write16(dev, 0x0043, backup[2]);
		b43legacy_write16(dev, 0x03E2, backup[7]);
		b43legacy_write16(dev, 0x03E6, backup[8]);
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, backup[9]);
		b43legacy_phy_write(dev, 0x0015, backup[3]);
		b43legacy_phy_write(dev, 0x005A, backup[4]);
		b43legacy_phy_write(dev, 0x0059, backup[5]);
		b43legacy_phy_write(dev, 0x0058, backup[6]);
		b43legacy_synth_pu_workaround(dev, phy->channel);
		b43legacy_phy_write(dev, 0x0802,
				    b43legacy_phy_read(dev, 0x0802) | 0x0003);
		b43legacy_set_original_gains(dev);
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
				    b43legacy_phy_read(dev, B43legacy_PHY_G_CRS)
				    | 0x8000);
		if (phy->rev >= 3) {
			b43legacy_phy_write(dev, 0x0801, backup[14]);
			b43legacy_phy_write(dev, 0x0060, backup[15]);
			b43legacy_phy_write(dev, 0x0014, backup[16]);
			b43legacy_phy_write(dev, 0x0478, backup[17]);
		}
		b43legacy_nrssi_mem_update(dev);
		b43legacy_calc_nrssi_threshold(dev);
		break;
	default:
		B43legacy_BUG_ON(1);
	}
}

void b43legacy_calc_nrssi_threshold(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	s32 threshold;
	s32 a;
	s32 b;
	s16 tmp16;
	u16 tmp_u16;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B: {
		if (phy->radio_ver != 0x2050)
			return;
		if (!(dev->dev->bus->sprom.boardflags_lo &
		    B43legacy_BFL_RSSI))
			return;

		if (phy->radio_rev >= 6) {
			threshold = (phy->nrssi[1] - phy->nrssi[0]) * 32;
			threshold += 20 * (phy->nrssi[0] + 1);
			threshold /= 40;
		} else
			threshold = phy->nrssi[1] - 5;

		threshold = clamp_val(threshold, 0, 0x3E);
		b43legacy_phy_read(dev, 0x0020); /* dummy read */
		b43legacy_phy_write(dev, 0x0020, (((u16)threshold) << 8)
				    | 0x001C);

		if (phy->radio_rev >= 6) {
			b43legacy_phy_write(dev, 0x0087, 0x0E0D);
			b43legacy_phy_write(dev, 0x0086, 0x0C0B);
			b43legacy_phy_write(dev, 0x0085, 0x0A09);
			b43legacy_phy_write(dev, 0x0084, 0x0808);
			b43legacy_phy_write(dev, 0x0083, 0x0808);
			b43legacy_phy_write(dev, 0x0082, 0x0604);
			b43legacy_phy_write(dev, 0x0081, 0x0302);
			b43legacy_phy_write(dev, 0x0080, 0x0100);
		}
		break;
	}
	case B43legacy_PHYTYPE_G:
		if (!phy->gmode ||
		    !(dev->dev->bus->sprom.boardflags_lo &
		    B43legacy_BFL_RSSI)) {
			tmp16 = b43legacy_nrssi_hw_read(dev, 0x20);
			if (tmp16 >= 0x20)
				tmp16 -= 0x40;
			if (tmp16 < 3)
				b43legacy_phy_write(dev, 0x048A,
						    (b43legacy_phy_read(dev,
						    0x048A) & 0xF000) | 0x09EB);
			else
				b43legacy_phy_write(dev, 0x048A,
						    (b43legacy_phy_read(dev,
						    0x048A) & 0xF000) | 0x0AED);
		} else {
			if (phy->interfmode ==
			    B43legacy_RADIO_INTERFMODE_NONWLAN) {
				a = 0xE;
				b = 0xA;
			} else if (!phy->aci_wlan_automatic &&
				    phy->aci_enable) {
				a = 0x13;
				b = 0x12;
			} else {
				a = 0xE;
				b = 0x11;
			}

			a = a * (phy->nrssi[1] - phy->nrssi[0]);
			a += (phy->nrssi[0] << 6);
			if (a < 32)
				a += 31;
			else
				a += 32;
			a = a >> 6;
			a = clamp_val(a, -31, 31);

			b = b * (phy->nrssi[1] - phy->nrssi[0]);
			b += (phy->nrssi[0] << 6);
			if (b < 32)
				b += 31;
			else
				b += 32;
			b = b >> 6;
			b = clamp_val(b, -31, 31);

			tmp_u16 = b43legacy_phy_read(dev, 0x048A) & 0xF000;
			tmp_u16 |= ((u32)b & 0x0000003F);
			tmp_u16 |= (((u32)a & 0x0000003F) << 6);
			b43legacy_phy_write(dev, 0x048A, tmp_u16);
		}
		break;
	default:
		B43legacy_BUG_ON(1);
	}
}

/* Stack implementation to save/restore values from the
 * interference mitigation code.
 * It is save to restore values in random order.
 */
static void _stack_save(u32 *_stackptr, size_t *stackidx,
			u8 id, u16 offset, u16 value)
{
	u32 *stackptr = &(_stackptr[*stackidx]);

	B43legacy_WARN_ON(!((offset & 0xE000) == 0x0000));
	B43legacy_WARN_ON(!((id & 0xF8) == 0x00));
	*stackptr = offset;
	*stackptr |= ((u32)id) << 13;
	*stackptr |= ((u32)value) << 16;
	(*stackidx)++;
	B43legacy_WARN_ON(!(*stackidx < B43legacy_INTERFSTACK_SIZE));
}

static u16 _stack_restore(u32 *stackptr,
			  u8 id, u16 offset)
{
	size_t i;

	B43legacy_WARN_ON(!((offset & 0xE000) == 0x0000));
	B43legacy_WARN_ON(!((id & 0xF8) == 0x00));
	for (i = 0; i < B43legacy_INTERFSTACK_SIZE; i++, stackptr++) {
		if ((*stackptr & 0x00001FFF) != offset)
			continue;
		if (((*stackptr & 0x00007000) >> 13) != id)
			continue;
		return ((*stackptr & 0xFFFF0000) >> 16);
	}
	B43legacy_BUG_ON(1);

	return 0;
}

#define phy_stacksave(offset)					\
	do {							\
		_stack_save(stack, &stackidx, 0x1, (offset),	\
			    b43legacy_phy_read(dev, (offset)));	\
	} while (0)
#define phy_stackrestore(offset)				\
	do {							\
		b43legacy_phy_write(dev, (offset),		\
				    _stack_restore(stack, 0x1,	\
				    (offset)));			\
	} while (0)
#define radio_stacksave(offset)						\
	do {								\
		_stack_save(stack, &stackidx, 0x2, (offset),		\
			    b43legacy_radio_read16(dev, (offset)));	\
	} while (0)
#define radio_stackrestore(offset)					\
	do {								\
		b43legacy_radio_write16(dev, (offset),			\
					_stack_restore(stack, 0x2,	\
					(offset)));			\
	} while (0)
#define ilt_stacksave(offset)					\
	do {							\
		_stack_save(stack, &stackidx, 0x3, (offset),	\
			    b43legacy_ilt_read(dev, (offset)));	\
	} while (0)
#define ilt_stackrestore(offset)				\
	do {							\
		b43legacy_ilt_write(dev, (offset),		\
				  _stack_restore(stack, 0x3,	\
						 (offset)));	\
	} while (0)

static void
b43legacy_radio_interference_mitigation_enable(struct b43legacy_wldev *dev,
					       int mode)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 tmp;
	u16 flipped;
	u32 tmp32;
	size_t stackidx = 0;
	u32 *stack = phy->interfstack;

	switch (mode) {
	case B43legacy_RADIO_INTERFMODE_NONWLAN:
		if (phy->rev != 1) {
			b43legacy_phy_write(dev, 0x042B,
					    b43legacy_phy_read(dev, 0x042B)
					    | 0x0800);
			b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
					    b43legacy_phy_read(dev,
					    B43legacy_PHY_G_CRS) & ~0x4000);
			break;
		}
		radio_stacksave(0x0078);
		tmp = (b43legacy_radio_read16(dev, 0x0078) & 0x001E);
		flipped = flip_4bit(tmp);
		if (flipped < 10 && flipped >= 8)
			flipped = 7;
		else if (flipped >= 10)
			flipped -= 3;
		flipped = flip_4bit(flipped);
		flipped = (flipped << 1) | 0x0020;
		b43legacy_radio_write16(dev, 0x0078, flipped);

		b43legacy_calc_nrssi_threshold(dev);

		phy_stacksave(0x0406);
		b43legacy_phy_write(dev, 0x0406, 0x7E28);

		b43legacy_phy_write(dev, 0x042B,
				    b43legacy_phy_read(dev, 0x042B) | 0x0800);
		b43legacy_phy_write(dev, B43legacy_PHY_RADIO_BITFIELD,
				    b43legacy_phy_read(dev,
				    B43legacy_PHY_RADIO_BITFIELD) | 0x1000);

		phy_stacksave(0x04A0);
		b43legacy_phy_write(dev, 0x04A0,
				    (b43legacy_phy_read(dev, 0x04A0) & 0xC0C0)
				    | 0x0008);
		phy_stacksave(0x04A1);
		b43legacy_phy_write(dev, 0x04A1,
				    (b43legacy_phy_read(dev, 0x04A1) & 0xC0C0)
				    | 0x0605);
		phy_stacksave(0x04A2);
		b43legacy_phy_write(dev, 0x04A2,
				    (b43legacy_phy_read(dev, 0x04A2) & 0xC0C0)
				    | 0x0204);
		phy_stacksave(0x04A8);
		b43legacy_phy_write(dev, 0x04A8,
				    (b43legacy_phy_read(dev, 0x04A8) & 0xC0C0)
				    | 0x0803);
		phy_stacksave(0x04AB);
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB) & 0xC0C0)
				    | 0x0605);

		phy_stacksave(0x04A7);
		b43legacy_phy_write(dev, 0x04A7, 0x0002);
		phy_stacksave(0x04A3);
		b43legacy_phy_write(dev, 0x04A3, 0x287A);
		phy_stacksave(0x04A9);
		b43legacy_phy_write(dev, 0x04A9, 0x2027);
		phy_stacksave(0x0493);
		b43legacy_phy_write(dev, 0x0493, 0x32F5);
		phy_stacksave(0x04AA);
		b43legacy_phy_write(dev, 0x04AA, 0x2027);
		phy_stacksave(0x04AC);
		b43legacy_phy_write(dev, 0x04AC, 0x32F5);
		break;
	case B43legacy_RADIO_INTERFMODE_MANUALWLAN:
		if (b43legacy_phy_read(dev, 0x0033) & 0x0800)
			break;

		phy->aci_enable = true;

		phy_stacksave(B43legacy_PHY_RADIO_BITFIELD);
		phy_stacksave(B43legacy_PHY_G_CRS);
		if (phy->rev < 2)
			phy_stacksave(0x0406);
		else {
			phy_stacksave(0x04C0);
			phy_stacksave(0x04C1);
		}
		phy_stacksave(0x0033);
		phy_stacksave(0x04A7);
		phy_stacksave(0x04A3);
		phy_stacksave(0x04A9);
		phy_stacksave(0x04AA);
		phy_stacksave(0x04AC);
		phy_stacksave(0x0493);
		phy_stacksave(0x04A1);
		phy_stacksave(0x04A0);
		phy_stacksave(0x04A2);
		phy_stacksave(0x048A);
		phy_stacksave(0x04A8);
		phy_stacksave(0x04AB);
		if (phy->rev == 2) {
			phy_stacksave(0x04AD);
			phy_stacksave(0x04AE);
		} else if (phy->rev >= 3) {
			phy_stacksave(0x04AD);
			phy_stacksave(0x0415);
			phy_stacksave(0x0416);
			phy_stacksave(0x0417);
			ilt_stacksave(0x1A00 + 0x2);
			ilt_stacksave(0x1A00 + 0x3);
		}
		phy_stacksave(0x042B);
		phy_stacksave(0x048C);

		b43legacy_phy_write(dev, B43legacy_PHY_RADIO_BITFIELD,
				    b43legacy_phy_read(dev,
				    B43legacy_PHY_RADIO_BITFIELD) & ~0x1000);
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
				    (b43legacy_phy_read(dev,
				    B43legacy_PHY_G_CRS)
				    & 0xFFFC) | 0x0002);

		b43legacy_phy_write(dev, 0x0033, 0x0800);
		b43legacy_phy_write(dev, 0x04A3, 0x2027);
		b43legacy_phy_write(dev, 0x04A9, 0x1CA8);
		b43legacy_phy_write(dev, 0x0493, 0x287A);
		b43legacy_phy_write(dev, 0x04AA, 0x1CA8);
		b43legacy_phy_write(dev, 0x04AC, 0x287A);

		b43legacy_phy_write(dev, 0x04A0,
				    (b43legacy_phy_read(dev, 0x04A0)
				    & 0xFFC0) | 0x001A);
		b43legacy_phy_write(dev, 0x04A7, 0x000D);

		if (phy->rev < 2)
			b43legacy_phy_write(dev, 0x0406, 0xFF0D);
		else if (phy->rev == 2) {
			b43legacy_phy_write(dev, 0x04C0, 0xFFFF);
			b43legacy_phy_write(dev, 0x04C1, 0x00A9);
		} else {
			b43legacy_phy_write(dev, 0x04C0, 0x00C1);
			b43legacy_phy_write(dev, 0x04C1, 0x0059);
		}

		b43legacy_phy_write(dev, 0x04A1,
				    (b43legacy_phy_read(dev, 0x04A1)
				    & 0xC0FF) | 0x1800);
		b43legacy_phy_write(dev, 0x04A1,
				    (b43legacy_phy_read(dev, 0x04A1)
				    & 0xFFC0) | 0x0015);
		b43legacy_phy_write(dev, 0x04A8,
				    (b43legacy_phy_read(dev, 0x04A8)
				    & 0xCFFF) | 0x1000);
		b43legacy_phy_write(dev, 0x04A8,
				    (b43legacy_phy_read(dev, 0x04A8)
				    & 0xF0FF) | 0x0A00);
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB)
				    & 0xCFFF) | 0x1000);
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB)
				    & 0xF0FF) | 0x0800);
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB)
				    & 0xFFCF) | 0x0010);
		b43legacy_phy_write(dev, 0x04AB,
				    (b43legacy_phy_read(dev, 0x04AB)
				    & 0xFFF0) | 0x0005);
		b43legacy_phy_write(dev, 0x04A8,
				    (b43legacy_phy_read(dev, 0x04A8)
				    & 0xFFCF) | 0x0010);
		b43legacy_phy_write(dev, 0x04A8,
				    (b43legacy_phy_read(dev, 0x04A8)
				    & 0xFFF0) | 0x0006);
		b43legacy_phy_write(dev, 0x04A2,
				    (b43legacy_phy_read(dev, 0x04A2)
				    & 0xF0FF) | 0x0800);
		b43legacy_phy_write(dev, 0x04A0,
				    (b43legacy_phy_read(dev, 0x04A0)
				    & 0xF0FF) | 0x0500);
		b43legacy_phy_write(dev, 0x04A2,
				    (b43legacy_phy_read(dev, 0x04A2)
				    & 0xFFF0) | 0x000B);

		if (phy->rev >= 3) {
			b43legacy_phy_write(dev, 0x048A,
					    b43legacy_phy_read(dev, 0x048A)
					    & ~0x8000);
			b43legacy_phy_write(dev, 0x0415,
					    (b43legacy_phy_read(dev, 0x0415)
					    & 0x8000) | 0x36D8);
			b43legacy_phy_write(dev, 0x0416,
					    (b43legacy_phy_read(dev, 0x0416)
					    & 0x8000) | 0x36D8);
			b43legacy_phy_write(dev, 0x0417,
					    (b43legacy_phy_read(dev, 0x0417)
					    & 0xFE00) | 0x016D);
		} else {
			b43legacy_phy_write(dev, 0x048A,
					    b43legacy_phy_read(dev, 0x048A)
					    | 0x1000);
			b43legacy_phy_write(dev, 0x048A,
					    (b43legacy_phy_read(dev, 0x048A)
					    & 0x9FFF) | 0x2000);
			tmp32 = b43legacy_shm_read32(dev, B43legacy_SHM_SHARED,
					    B43legacy_UCODEFLAGS_OFFSET);
			if (!(tmp32 & 0x800)) {
				tmp32 |= 0x800;
				b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
					    B43legacy_UCODEFLAGS_OFFSET,
					    tmp32);
			}
		}
		if (phy->rev >= 2)
			b43legacy_phy_write(dev, 0x042B,
					    b43legacy_phy_read(dev, 0x042B)
					    | 0x0800);
		b43legacy_phy_write(dev, 0x048C,
				    (b43legacy_phy_read(dev, 0x048C)
				    & 0xF0FF) | 0x0200);
		if (phy->rev == 2) {
			b43legacy_phy_write(dev, 0x04AE,
					    (b43legacy_phy_read(dev, 0x04AE)
					    & 0xFF00) | 0x007F);
			b43legacy_phy_write(dev, 0x04AD,
					    (b43legacy_phy_read(dev, 0x04AD)
					    & 0x00FF) | 0x1300);
		} else if (phy->rev >= 6) {
			b43legacy_ilt_write(dev, 0x1A00 + 0x3, 0x007F);
			b43legacy_ilt_write(dev, 0x1A00 + 0x2, 0x007F);
			b43legacy_phy_write(dev, 0x04AD,
					    b43legacy_phy_read(dev, 0x04AD)
					    & 0x00FF);
		}
		b43legacy_calc_nrssi_slope(dev);
		break;
	default:
		B43legacy_BUG_ON(1);
	}
}

static void
b43legacy_radio_interference_mitigation_disable(struct b43legacy_wldev *dev,
						int mode)
{
	struct b43legacy_phy *phy = &dev->phy;
	u32 tmp32;
	u32 *stack = phy->interfstack;

	switch (mode) {
	case B43legacy_RADIO_INTERFMODE_NONWLAN:
		if (phy->rev != 1) {
			b43legacy_phy_write(dev, 0x042B,
					    b43legacy_phy_read(dev, 0x042B)
					    & ~0x0800);
			b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
					    b43legacy_phy_read(dev,
					    B43legacy_PHY_G_CRS) | 0x4000);
			break;
		}
		phy_stackrestore(0x0078);
		b43legacy_calc_nrssi_threshold(dev);
		phy_stackrestore(0x0406);
		b43legacy_phy_write(dev, 0x042B,
				    b43legacy_phy_read(dev, 0x042B) & ~0x0800);
		if (!dev->bad_frames_preempt)
			b43legacy_phy_write(dev, B43legacy_PHY_RADIO_BITFIELD,
					    b43legacy_phy_read(dev,
					    B43legacy_PHY_RADIO_BITFIELD)
					    & ~(1 << 11));
		b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
				    b43legacy_phy_read(dev, B43legacy_PHY_G_CRS)
				    | 0x4000);
		phy_stackrestore(0x04A0);
		phy_stackrestore(0x04A1);
		phy_stackrestore(0x04A2);
		phy_stackrestore(0x04A8);
		phy_stackrestore(0x04AB);
		phy_stackrestore(0x04A7);
		phy_stackrestore(0x04A3);
		phy_stackrestore(0x04A9);
		phy_stackrestore(0x0493);
		phy_stackrestore(0x04AA);
		phy_stackrestore(0x04AC);
		break;
	case B43legacy_RADIO_INTERFMODE_MANUALWLAN:
		if (!(b43legacy_phy_read(dev, 0x0033) & 0x0800))
			break;

		phy->aci_enable = false;

		phy_stackrestore(B43legacy_PHY_RADIO_BITFIELD);
		phy_stackrestore(B43legacy_PHY_G_CRS);
		phy_stackrestore(0x0033);
		phy_stackrestore(0x04A3);
		phy_stackrestore(0x04A9);
		phy_stackrestore(0x0493);
		phy_stackrestore(0x04AA);
		phy_stackrestore(0x04AC);
		phy_stackrestore(0x04A0);
		phy_stackrestore(0x04A7);
		if (phy->rev >= 2) {
			phy_stackrestore(0x04C0);
			phy_stackrestore(0x04C1);
		} else
			phy_stackrestore(0x0406);
		phy_stackrestore(0x04A1);
		phy_stackrestore(0x04AB);
		phy_stackrestore(0x04A8);
		if (phy->rev == 2) {
			phy_stackrestore(0x04AD);
			phy_stackrestore(0x04AE);
		} else if (phy->rev >= 3) {
			phy_stackrestore(0x04AD);
			phy_stackrestore(0x0415);
			phy_stackrestore(0x0416);
			phy_stackrestore(0x0417);
			ilt_stackrestore(0x1A00 + 0x2);
			ilt_stackrestore(0x1A00 + 0x3);
		}
		phy_stackrestore(0x04A2);
		phy_stackrestore(0x04A8);
		phy_stackrestore(0x042B);
		phy_stackrestore(0x048C);
		tmp32 = b43legacy_shm_read32(dev, B43legacy_SHM_SHARED,
					     B43legacy_UCODEFLAGS_OFFSET);
		if (tmp32 & 0x800) {
			tmp32 &= ~0x800;
			b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
					      B43legacy_UCODEFLAGS_OFFSET,
					      tmp32);
		}
		b43legacy_calc_nrssi_slope(dev);
		break;
	default:
		B43legacy_BUG_ON(1);
	}
}

#undef phy_stacksave
#undef phy_stackrestore
#undef radio_stacksave
#undef radio_stackrestore
#undef ilt_stacksave
#undef ilt_stackrestore

int b43legacy_radio_set_interference_mitigation(struct b43legacy_wldev *dev,
						int mode)
{
	struct b43legacy_phy *phy = &dev->phy;
	int currentmode;

	if ((phy->type != B43legacy_PHYTYPE_G) ||
	    (phy->rev == 0) || (!phy->gmode))
		return -ENODEV;

	phy->aci_wlan_automatic = false;
	switch (mode) {
	case B43legacy_RADIO_INTERFMODE_AUTOWLAN:
		phy->aci_wlan_automatic = true;
		if (phy->aci_enable)
			mode = B43legacy_RADIO_INTERFMODE_MANUALWLAN;
		else
			mode = B43legacy_RADIO_INTERFMODE_NONE;
		break;
	case B43legacy_RADIO_INTERFMODE_NONE:
	case B43legacy_RADIO_INTERFMODE_NONWLAN:
	case B43legacy_RADIO_INTERFMODE_MANUALWLAN:
		break;
	default:
		return -EINVAL;
	}

	currentmode = phy->interfmode;
	if (currentmode == mode)
		return 0;
	if (currentmode != B43legacy_RADIO_INTERFMODE_NONE)
		b43legacy_radio_interference_mitigation_disable(dev,
								currentmode);

	if (mode == B43legacy_RADIO_INTERFMODE_NONE) {
		phy->aci_enable = false;
		phy->aci_hw_rssi = false;
	} else
		b43legacy_radio_interference_mitigation_enable(dev, mode);
	phy->interfmode = mode;

	return 0;
}

u16 b43legacy_radio_calibrationvalue(struct b43legacy_wldev *dev)
{
	u16 reg;
	u16 index;
	u16 ret;

	reg = b43legacy_radio_read16(dev, 0x0060);
	index = (reg & 0x001E) >> 1;
	ret = rcc_table[index] << 1;
	ret |= (reg & 0x0001);
	ret |= 0x0020;

	return ret;
}

#define LPD(L, P, D)    (((L) << 2) | ((P) << 1) | ((D) << 0))
static u16 b43legacy_get_812_value(struct b43legacy_wldev *dev, u8 lpd)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 loop_or = 0;
	u16 adj_loopback_gain = phy->loopback_gain[0];
	u8 loop;
	u16 extern_lna_control;

	if (!phy->gmode)
		return 0;
	if (!has_loopback_gain(phy)) {
		if (phy->rev < 7 || !(dev->dev->bus->sprom.boardflags_lo
		    & B43legacy_BFL_EXTLNA)) {
			switch (lpd) {
			case LPD(0, 1, 1):
				return 0x0FB2;
			case LPD(0, 0, 1):
				return 0x00B2;
			case LPD(1, 0, 1):
				return 0x30B2;
			case LPD(1, 0, 0):
				return 0x30B3;
			default:
				B43legacy_BUG_ON(1);
			}
		} else {
			switch (lpd) {
			case LPD(0, 1, 1):
				return 0x8FB2;
			case LPD(0, 0, 1):
				return 0x80B2;
			case LPD(1, 0, 1):
				return 0x20B2;
			case LPD(1, 0, 0):
				return 0x20B3;
			default:
				B43legacy_BUG_ON(1);
			}
		}
	} else {
		if (phy->radio_rev == 8)
			adj_loopback_gain += 0x003E;
		else
			adj_loopback_gain += 0x0026;
		if (adj_loopback_gain >= 0x46) {
			adj_loopback_gain -= 0x46;
			extern_lna_control = 0x3000;
		} else if (adj_loopback_gain >= 0x3A) {
			adj_loopback_gain -= 0x3A;
			extern_lna_control = 0x2000;
		} else if (adj_loopback_gain >= 0x2E) {
			adj_loopback_gain -= 0x2E;
			extern_lna_control = 0x1000;
		} else {
			adj_loopback_gain -= 0x10;
			extern_lna_control = 0x0000;
		}
		for (loop = 0; loop < 16; loop++) {
			u16 tmp = adj_loopback_gain - 6 * loop;
			if (tmp < 6)
				break;
		}

		loop_or = (loop << 8) | extern_lna_control;
		if (phy->rev >= 7 && dev->dev->bus->sprom.boardflags_lo
		    & B43legacy_BFL_EXTLNA) {
			if (extern_lna_control)
				loop_or |= 0x8000;
			switch (lpd) {
			case LPD(0, 1, 1):
				return 0x8F92;
			case LPD(0, 0, 1):
				return (0x8092 | loop_or);
			case LPD(1, 0, 1):
				return (0x2092 | loop_or);
			case LPD(1, 0, 0):
				return (0x2093 | loop_or);
			default:
				B43legacy_BUG_ON(1);
			}
		} else {
			switch (lpd) {
			case LPD(0, 1, 1):
				return 0x0F92;
			case LPD(0, 0, 1):
			case LPD(1, 0, 1):
				return (0x0092 | loop_or);
			case LPD(1, 0, 0):
				return (0x0093 | loop_or);
			default:
				B43legacy_BUG_ON(1);
			}
		}
	}
	return 0;
}

u16 b43legacy_radio_init2050(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 backup[21] = { 0 };
	u16 ret;
	u16 i;
	u16 j;
	u32 tmp1 = 0;
	u32 tmp2 = 0;

	backup[0] = b43legacy_radio_read16(dev, 0x0043);
	backup[14] = b43legacy_radio_read16(dev, 0x0051);
	backup[15] = b43legacy_radio_read16(dev, 0x0052);
	backup[1] = b43legacy_phy_read(dev, 0x0015);
	backup[16] = b43legacy_phy_read(dev, 0x005A);
	backup[17] = b43legacy_phy_read(dev, 0x0059);
	backup[18] = b43legacy_phy_read(dev, 0x0058);
	if (phy->type == B43legacy_PHYTYPE_B) {
		backup[2] = b43legacy_phy_read(dev, 0x0030);
		backup[3] = b43legacy_read16(dev, 0x03EC);
		b43legacy_phy_write(dev, 0x0030, 0x00FF);
		b43legacy_write16(dev, 0x03EC, 0x3F3F);
	} else {
		if (phy->gmode) {
			backup[4] = b43legacy_phy_read(dev, 0x0811);
			backup[5] = b43legacy_phy_read(dev, 0x0812);
			backup[6] = b43legacy_phy_read(dev, 0x0814);
			backup[7] = b43legacy_phy_read(dev, 0x0815);
			backup[8] = b43legacy_phy_read(dev,
						       B43legacy_PHY_G_CRS);
			backup[9] = b43legacy_phy_read(dev, 0x0802);
			b43legacy_phy_write(dev, 0x0814,
					    (b43legacy_phy_read(dev, 0x0814)
					    | 0x0003));
			b43legacy_phy_write(dev, 0x0815,
					    (b43legacy_phy_read(dev, 0x0815)
					    & 0xFFFC));
			b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
					    (b43legacy_phy_read(dev,
					    B43legacy_PHY_G_CRS) & 0x7FFF));
			b43legacy_phy_write(dev, 0x0802,
					    (b43legacy_phy_read(dev, 0x0802)
					    & 0xFFFC));
			if (phy->rev > 1) { /* loopback gain enabled */
				backup[19] = b43legacy_phy_read(dev, 0x080F);
				backup[20] = b43legacy_phy_read(dev, 0x0810);
				if (phy->rev >= 3)
					b43legacy_phy_write(dev, 0x080F,
							    0xC020);
				else
					b43legacy_phy_write(dev, 0x080F,
							    0x8020);
				b43legacy_phy_write(dev, 0x0810, 0x0000);
			}
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_get_812_value(dev,
					    LPD(0, 1, 1)));
			if (phy->rev < 7 ||
			    !(dev->dev->bus->sprom.boardflags_lo
			    & B43legacy_BFL_EXTLNA))
				b43legacy_phy_write(dev, 0x0811, 0x01B3);
			else
				b43legacy_phy_write(dev, 0x0811, 0x09B3);
		}
	}
	b43legacy_write16(dev, B43legacy_MMIO_PHY_RADIO,
			(b43legacy_read16(dev, B43legacy_MMIO_PHY_RADIO)
					  | 0x8000));
	backup[10] = b43legacy_phy_read(dev, 0x0035);
	b43legacy_phy_write(dev, 0x0035,
			    (b43legacy_phy_read(dev, 0x0035) & 0xFF7F));
	backup[11] = b43legacy_read16(dev, 0x03E6);
	backup[12] = b43legacy_read16(dev, B43legacy_MMIO_CHANNEL_EXT);

	/* Initialization */
	if (phy->analog == 0)
		b43legacy_write16(dev, 0x03E6, 0x0122);
	else {
		if (phy->analog >= 2)
			b43legacy_phy_write(dev, 0x0003,
					    (b43legacy_phy_read(dev, 0x0003)
					    & 0xFFBF) | 0x0040);
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT,
				  (b43legacy_read16(dev,
				  B43legacy_MMIO_CHANNEL_EXT) | 0x2000));
	}

	ret = b43legacy_radio_calibrationvalue(dev);

	if (phy->type == B43legacy_PHYTYPE_B)
		b43legacy_radio_write16(dev, 0x0078, 0x0026);

	if (phy->gmode)
		b43legacy_phy_write(dev, 0x0812,
				    b43legacy_get_812_value(dev,
				    LPD(0, 1, 1)));
	b43legacy_phy_write(dev, 0x0015, 0xBFAF);
	b43legacy_phy_write(dev, 0x002B, 0x1403);
	if (phy->gmode)
		b43legacy_phy_write(dev, 0x0812,
				    b43legacy_get_812_value(dev,
				    LPD(0, 0, 1)));
	b43legacy_phy_write(dev, 0x0015, 0xBFA0);
	b43legacy_radio_write16(dev, 0x0051,
				(b43legacy_radio_read16(dev, 0x0051)
				| 0x0004));
	if (phy->radio_rev == 8)
		b43legacy_radio_write16(dev, 0x0043, 0x001F);
	else {
		b43legacy_radio_write16(dev, 0x0052, 0x0000);
		b43legacy_radio_write16(dev, 0x0043,
					(b43legacy_radio_read16(dev, 0x0043)
					& 0xFFF0) | 0x0009);
	}
	b43legacy_phy_write(dev, 0x0058, 0x0000);

	for (i = 0; i < 16; i++) {
		b43legacy_phy_write(dev, 0x005A, 0x0480);
		b43legacy_phy_write(dev, 0x0059, 0xC810);
		b43legacy_phy_write(dev, 0x0058, 0x000D);
		if (phy->gmode)
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_get_812_value(dev,
					    LPD(1, 0, 1)));
		b43legacy_phy_write(dev, 0x0015, 0xAFB0);
		udelay(10);
		if (phy->gmode)
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_get_812_value(dev,
					    LPD(1, 0, 1)));
		b43legacy_phy_write(dev, 0x0015, 0xEFB0);
		udelay(10);
		if (phy->gmode)
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_get_812_value(dev,
					    LPD(1, 0, 0)));
		b43legacy_phy_write(dev, 0x0015, 0xFFF0);
		udelay(20);
		tmp1 += b43legacy_phy_read(dev, 0x002D);
		b43legacy_phy_write(dev, 0x0058, 0x0000);
		if (phy->gmode)
			b43legacy_phy_write(dev, 0x0812,
					    b43legacy_get_812_value(dev,
					    LPD(1, 0, 1)));
		b43legacy_phy_write(dev, 0x0015, 0xAFB0);
	}

	tmp1++;
	tmp1 >>= 9;
	udelay(10);
	b43legacy_phy_write(dev, 0x0058, 0x0000);

	for (i = 0; i < 16; i++) {
		b43legacy_radio_write16(dev, 0x0078, (flip_4bit(i) << 1)
					| 0x0020);
		backup[13] = b43legacy_radio_read16(dev, 0x0078);
		udelay(10);
		for (j = 0; j < 16; j++) {
			b43legacy_phy_write(dev, 0x005A, 0x0D80);
			b43legacy_phy_write(dev, 0x0059, 0xC810);
			b43legacy_phy_write(dev, 0x0058, 0x000D);
			if (phy->gmode)
				b43legacy_phy_write(dev, 0x0812,
						    b43legacy_get_812_value(dev,
						    LPD(1, 0, 1)));
			b43legacy_phy_write(dev, 0x0015, 0xAFB0);
			udelay(10);
			if (phy->gmode)
				b43legacy_phy_write(dev, 0x0812,
						    b43legacy_get_812_value(dev,
						    LPD(1, 0, 1)));
			b43legacy_phy_write(dev, 0x0015, 0xEFB0);
			udelay(10);
			if (phy->gmode)
				b43legacy_phy_write(dev, 0x0812,
						    b43legacy_get_812_value(dev,
						    LPD(1, 0, 0)));
			b43legacy_phy_write(dev, 0x0015, 0xFFF0);
			udelay(10);
			tmp2 += b43legacy_phy_read(dev, 0x002D);
			b43legacy_phy_write(dev, 0x0058, 0x0000);
			if (phy->gmode)
				b43legacy_phy_write(dev, 0x0812,
						    b43legacy_get_812_value(dev,
						    LPD(1, 0, 1)));
			b43legacy_phy_write(dev, 0x0015, 0xAFB0);
		}
		tmp2++;
		tmp2 >>= 8;
		if (tmp1 < tmp2)
			break;
	}

	/* Restore the registers */
	b43legacy_phy_write(dev, 0x0015, backup[1]);
	b43legacy_radio_write16(dev, 0x0051, backup[14]);
	b43legacy_radio_write16(dev, 0x0052, backup[15]);
	b43legacy_radio_write16(dev, 0x0043, backup[0]);
	b43legacy_phy_write(dev, 0x005A, backup[16]);
	b43legacy_phy_write(dev, 0x0059, backup[17]);
	b43legacy_phy_write(dev, 0x0058, backup[18]);
	b43legacy_write16(dev, 0x03E6, backup[11]);
	if (phy->analog != 0)
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT, backup[12]);
	b43legacy_phy_write(dev, 0x0035, backup[10]);
	b43legacy_radio_selectchannel(dev, phy->channel, 1);
	if (phy->type == B43legacy_PHYTYPE_B) {
		b43legacy_phy_write(dev, 0x0030, backup[2]);
		b43legacy_write16(dev, 0x03EC, backup[3]);
	} else {
		if (phy->gmode) {
			b43legacy_write16(dev, B43legacy_MMIO_PHY_RADIO,
					  (b43legacy_read16(dev,
					  B43legacy_MMIO_PHY_RADIO) & 0x7FFF));
			b43legacy_phy_write(dev, 0x0811, backup[4]);
			b43legacy_phy_write(dev, 0x0812, backup[5]);
			b43legacy_phy_write(dev, 0x0814, backup[6]);
			b43legacy_phy_write(dev, 0x0815, backup[7]);
			b43legacy_phy_write(dev, B43legacy_PHY_G_CRS,
					    backup[8]);
			b43legacy_phy_write(dev, 0x0802, backup[9]);
			if (phy->rev > 1) {
				b43legacy_phy_write(dev, 0x080F, backup[19]);
				b43legacy_phy_write(dev, 0x0810, backup[20]);
			}
		}
	}
	if (i >= 15)
		ret = backup[13];

	return ret;
}

static inline
u16 freq_r3A_value(u16 frequency)
{
	u16 value;

	if (frequency < 5091)
		value = 0x0040;
	else if (frequency < 5321)
		value = 0x0000;
	else if (frequency < 5806)
		value = 0x0080;
	else
		value = 0x0040;

	return value;
}

int b43legacy_radio_selectchannel(struct b43legacy_wldev *dev,
				  u8 channel,
				  int synthetic_pu_workaround)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (channel == 0xFF) {
		switch (phy->type) {
		case B43legacy_PHYTYPE_B:
		case B43legacy_PHYTYPE_G:
			channel = B43legacy_RADIO_DEFAULT_CHANNEL_BG;
			break;
		default:
			B43legacy_WARN_ON(1);
		}
	}

/* TODO: Check if channel is valid - return -EINVAL if not */
	if (synthetic_pu_workaround)
		b43legacy_synth_pu_workaround(dev, channel);

	b43legacy_write16(dev, B43legacy_MMIO_CHANNEL,
			  channel2freq_bg(channel));

	if (channel == 14) {
		if (dev->dev->bus->sprom.country_code == 5)   /* JAPAN) */
			b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
					      B43legacy_UCODEFLAGS_OFFSET,
					      b43legacy_shm_read32(dev,
					      B43legacy_SHM_SHARED,
					      B43legacy_UCODEFLAGS_OFFSET)
					      & ~(1 << 7));
		else
			b43legacy_shm_write32(dev, B43legacy_SHM_SHARED,
					      B43legacy_UCODEFLAGS_OFFSET,
					      b43legacy_shm_read32(dev,
					      B43legacy_SHM_SHARED,
					      B43legacy_UCODEFLAGS_OFFSET)
					      | (1 << 7));
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT,
				  b43legacy_read16(dev,
				  B43legacy_MMIO_CHANNEL_EXT) | (1 << 11));
	} else
		b43legacy_write16(dev, B43legacy_MMIO_CHANNEL_EXT,
				  b43legacy_read16(dev,
				  B43legacy_MMIO_CHANNEL_EXT) & 0xF7BF);

	phy->channel = channel;
	/*XXX: Using the longer of 2 timeouts (8000 vs 2000 usecs). Specs states
	 *     that 2000 usecs might suffice. */
	msleep(8);

	return 0;
}

void b43legacy_radio_set_txantenna(struct b43legacy_wldev *dev, u32 val)
{
	u16 tmp;

	val <<= 8;
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x0022) & 0xFCFF;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0022, tmp | val);
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x03A8) & 0xFCFF;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x03A8, tmp | val);
	tmp = b43legacy_shm_read16(dev, B43legacy_SHM_SHARED, 0x0054) & 0xFCFF;
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0054, tmp | val);
}

/* http://bcm-specs.sipsolutions.net/TX_Gain_Base_Band */
static u16 b43legacy_get_txgain_base_band(u16 txpower)
{
	u16 ret;

	B43legacy_WARN_ON(txpower > 63);

	if (txpower >= 54)
		ret = 2;
	else if (txpower >= 49)
		ret = 4;
	else if (txpower >= 44)
		ret = 5;
	else
		ret = 6;

	return ret;
}

/* http://bcm-specs.sipsolutions.net/TX_Gain_Radio_Frequency_Power_Amplifier */
static u16 b43legacy_get_txgain_freq_power_amp(u16 txpower)
{
	u16 ret;

	B43legacy_WARN_ON(txpower > 63);

	if (txpower >= 32)
		ret = 0;
	else if (txpower >= 25)
		ret = 1;
	else if (txpower >= 20)
		ret = 2;
	else if (txpower >= 12)
		ret = 3;
	else
		ret = 4;

	return ret;
}

/* http://bcm-specs.sipsolutions.net/TX_Gain_Digital_Analog_Converter */
static u16 b43legacy_get_txgain_dac(u16 txpower)
{
	u16 ret;

	B43legacy_WARN_ON(txpower > 63);

	if (txpower >= 54)
		ret = txpower - 53;
	else if (txpower >= 49)
		ret = txpower - 42;
	else if (txpower >= 44)
		ret = txpower - 37;
	else if (txpower >= 32)
		ret = txpower - 32;
	else if (txpower >= 25)
		ret = txpower - 20;
	else if (txpower >= 20)
		ret = txpower - 13;
	else if (txpower >= 12)
		ret = txpower - 8;
	else
		ret = txpower;

	return ret;
}

void b43legacy_radio_set_txpower_a(struct b43legacy_wldev *dev, u16 txpower)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 pamp;
	u16 base;
	u16 dac;
	u16 ilt;

	txpower = clamp_val(txpower, 0, 63);

	pamp = b43legacy_get_txgain_freq_power_amp(txpower);
	pamp <<= 5;
	pamp &= 0x00E0;
	b43legacy_phy_write(dev, 0x0019, pamp);

	base = b43legacy_get_txgain_base_band(txpower);
	base &= 0x000F;
	b43legacy_phy_write(dev, 0x0017, base | 0x0020);

	ilt = b43legacy_ilt_read(dev, 0x3001);
	ilt &= 0x0007;

	dac = b43legacy_get_txgain_dac(txpower);
	dac <<= 3;
	dac |= ilt;

	b43legacy_ilt_write(dev, 0x3001, dac);

	phy->txpwr_offset = txpower;

	/* TODO: FuncPlaceholder (Adjust BB loft cancel) */
}

void b43legacy_radio_set_txpower_bg(struct b43legacy_wldev *dev,
				    u16 baseband_attenuation,
				    u16 radio_attenuation,
				    u16 txpower)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (baseband_attenuation == 0xFFFF)
		baseband_attenuation = phy->bbatt;
	if (radio_attenuation == 0xFFFF)
		radio_attenuation = phy->rfatt;
	if (txpower == 0xFFFF)
		txpower = phy->txctl1;
	phy->bbatt = baseband_attenuation;
	phy->rfatt = radio_attenuation;
	phy->txctl1 = txpower;

	B43legacy_WARN_ON(baseband_attenuation > 11);
	if (phy->radio_rev < 6)
		B43legacy_WARN_ON(radio_attenuation > 9);
	else
		B43legacy_WARN_ON(radio_attenuation > 31);
	B43legacy_WARN_ON(txpower > 7);

	b43legacy_phy_set_baseband_attenuation(dev, baseband_attenuation);
	b43legacy_radio_write16(dev, 0x0043, radio_attenuation);
	b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0064,
			      radio_attenuation);
	if (phy->radio_ver == 0x2050)
		b43legacy_radio_write16(dev, 0x0052,
					(b43legacy_radio_read16(dev, 0x0052)
					& ~0x0070) | ((txpower << 4) & 0x0070));
	/* FIXME: The spec is very weird and unclear here. */
	if (phy->type == B43legacy_PHYTYPE_G)
		b43legacy_phy_lo_adjust(dev, 0);
}

u16 b43legacy_default_baseband_attenuation(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (phy->radio_ver == 0x2050 && phy->radio_rev < 6)
		return 0;
	return 2;
}

u16 b43legacy_default_radio_attenuation(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	u16 att = 0xFFFF;

	switch (phy->radio_ver) {
	case 0x2053:
		switch (phy->radio_rev) {
		case 1:
			att = 6;
			break;
		}
		break;
	case 0x2050:
		switch (phy->radio_rev) {
		case 0:
			att = 5;
			break;
		case 1:
			if (phy->type == B43legacy_PHYTYPE_G) {
				if (is_bcm_board_vendor(dev) &&
				    dev->dev->bus->boardinfo.type == 0x421 &&
				    dev->dev->bus->sprom.board_rev >= 30)
					att = 3;
				else if (is_bcm_board_vendor(dev) &&
					 dev->dev->bus->boardinfo.type == 0x416)
					att = 3;
				else
					att = 1;
			} else {
				if (is_bcm_board_vendor(dev) &&
				    dev->dev->bus->boardinfo.type == 0x421 &&
				    dev->dev->bus->sprom.board_rev >= 30)
					att = 7;
				else
					att = 6;
			}
			break;
		case 2:
			if (phy->type == B43legacy_PHYTYPE_G) {
				if (is_bcm_board_vendor(dev) &&
				    dev->dev->bus->boardinfo.type == 0x421 &&
				    dev->dev->bus->sprom.board_rev >= 30)
					att = 3;
				else if (is_bcm_board_vendor(dev) &&
					 dev->dev->bus->boardinfo.type ==
					 0x416)
					att = 5;
				else if (dev->dev->bus->chip_id == 0x4320)
					att = 4;
				else
					att = 3;
			} else
				att = 6;
			break;
		case 3:
			att = 5;
			break;
		case 4:
		case 5:
			att = 1;
			break;
		case 6:
		case 7:
			att = 5;
			break;
		case 8:
			att = 0x1A;
			break;
		case 9:
		default:
			att = 5;
		}
	}
	if (is_bcm_board_vendor(dev) &&
	    dev->dev->bus->boardinfo.type == 0x421) {
		if (dev->dev->bus->sprom.board_rev < 0x43)
			att = 2;
		else if (dev->dev->bus->sprom.board_rev < 0x51)
			att = 3;
	}
	if (att == 0xFFFF)
		att = 5;

	return att;
}

u16 b43legacy_default_txctl1(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (phy->radio_ver != 0x2050)
		return 0;
	if (phy->radio_rev == 1)
		return 3;
	if (phy->radio_rev < 6)
		return 2;
	if (phy->radio_rev == 8)
		return 1;
	return 0;
}

void b43legacy_radio_turn_on(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;
	int err;
	u8 channel;

	might_sleep();

	if (phy->radio_on)
		return;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
	case B43legacy_PHYTYPE_G:
		b43legacy_phy_write(dev, 0x0015, 0x8000);
		b43legacy_phy_write(dev, 0x0015, 0xCC00);
		b43legacy_phy_write(dev, 0x0015,
				    (phy->gmode ? 0x00C0 : 0x0000));
		if (phy->radio_off_context.valid) {
			/* Restore the RFover values. */
			b43legacy_phy_write(dev, B43legacy_PHY_RFOVER,
					    phy->radio_off_context.rfover);
			b43legacy_phy_write(dev, B43legacy_PHY_RFOVERVAL,
					    phy->radio_off_context.rfoverval);
			phy->radio_off_context.valid = false;
		}
		channel = phy->channel;
		err = b43legacy_radio_selectchannel(dev,
					B43legacy_RADIO_DEFAULT_CHANNEL_BG, 1);
		err |= b43legacy_radio_selectchannel(dev, channel, 0);
		B43legacy_WARN_ON(err);
		break;
	default:
		B43legacy_BUG_ON(1);
	}
	phy->radio_on = true;
}

void b43legacy_radio_turn_off(struct b43legacy_wldev *dev, bool force)
{
	struct b43legacy_phy *phy = &dev->phy;

	if (!phy->radio_on && !force)
		return;

	if (phy->type == B43legacy_PHYTYPE_G && dev->dev->id.revision >= 5) {
		u16 rfover, rfoverval;

		rfover = b43legacy_phy_read(dev, B43legacy_PHY_RFOVER);
		rfoverval = b43legacy_phy_read(dev, B43legacy_PHY_RFOVERVAL);
		if (!force) {
			phy->radio_off_context.rfover = rfover;
			phy->radio_off_context.rfoverval = rfoverval;
			phy->radio_off_context.valid = true;
		}
		b43legacy_phy_write(dev, B43legacy_PHY_RFOVER, rfover | 0x008C);
		b43legacy_phy_write(dev, B43legacy_PHY_RFOVERVAL,
				    rfoverval & 0xFF73);
	} else
		b43legacy_phy_write(dev, 0x0015, 0xAA00);
	phy->radio_on = false;
	b43legacydbg(dev->wl, "Radio initialized\n");
}

void b43legacy_radio_clear_tssi(struct b43legacy_wldev *dev)
{
	struct b43legacy_phy *phy = &dev->phy;

	switch (phy->type) {
	case B43legacy_PHYTYPE_B:
	case B43legacy_PHYTYPE_G:
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0058,
				      0x7F7F);
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x005a,
				      0x7F7F);
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0070,
				      0x7F7F);
		b43legacy_shm_write16(dev, B43legacy_SHM_SHARED, 0x0072,
				      0x7F7F);
		break;
	}
}
