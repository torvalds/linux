/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <stefano.brivio@polimi.it>
                     Michael Buesch <mb@bu3sch.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

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

#ifndef B43_MAIN_H_
#define B43_MAIN_H_

#include "b43.h"

#define P4D_BYT3S(magic, nr_bytes)	u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)	P4D_BYT3S(line, nr_bytes)
/* Magic helper macro to pad structures. Ignore those above. It's magic. */
#define PAD_BYTES(nr_bytes)		P4D_BYTES( __LINE__ , (nr_bytes))


extern int b43_modparam_qos;


/* Lightweight function to convert a frequency (in Mhz) to a channel number. */
static inline u8 b43_freq_to_channel_5ghz(int freq)
{
	return ((freq - 5000) / 5);
}
static inline u8 b43_freq_to_channel_2ghz(int freq)
{
	u8 channel;

	if (freq == 2484)
		channel = 14;
	else
		channel = (freq - 2407) / 5;

	return channel;
}

/* Lightweight function to convert a channel number to a frequency (in Mhz). */
static inline int b43_channel_to_freq_5ghz(u8 channel)
{
	return (5000 + (5 * channel));
}
static inline int b43_channel_to_freq_2ghz(u8 channel)
{
	int freq;

	if (channel == 14)
		freq = 2484;
	else
		freq = 2407 + (5 * channel);

	return freq;
}

static inline int b43_is_cck_rate(int rate)
{
	return (rate == B43_CCK_RATE_1MB ||
		rate == B43_CCK_RATE_2MB ||
		rate == B43_CCK_RATE_5MB || rate == B43_CCK_RATE_11MB);
}

static inline int b43_is_ofdm_rate(int rate)
{
	return !b43_is_cck_rate(rate);
}

u8 b43_ieee80211_antenna_sanitize(struct b43_wldev *dev,
				  u8 antenna_nr);

void b43_tsf_read(struct b43_wldev *dev, u64 * tsf);
void b43_tsf_write(struct b43_wldev *dev, u64 tsf);

u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset);
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset);
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value);
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value);

u64 b43_hf_read(struct b43_wldev *dev);
void b43_hf_write(struct b43_wldev *dev, u64 value);

void b43_dummy_transmission(struct b43_wldev *dev);

void b43_wireless_core_reset(struct b43_wldev *dev, u32 flags);

void b43_controller_restart(struct b43_wldev *dev, const char *reason);

#define B43_PS_ENABLED	(1 << 0)	/* Force enable hardware power saving */
#define B43_PS_DISABLED	(1 << 1)	/* Force disable hardware power saving */
#define B43_PS_AWAKE	(1 << 2)	/* Force device awake */
#define B43_PS_ASLEEP	(1 << 3)	/* Force device asleep */
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags);

#endif /* B43_MAIN_H_ */
