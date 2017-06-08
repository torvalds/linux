/*

  Broadcom B43legacy wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005, 2006 Michael Buesch <m@bues.ch>
  Copyright (c) 2005  Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005  Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2007  Larry Finger <Larry.Finger@lwfinger.net>

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

#ifndef B43legacy_MAIN_H_
#define B43legacy_MAIN_H_

#include "b43legacy.h"


#define P4D_BYT3S(magic, nr_bytes)	u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)	P4D_BYT3S(line, nr_bytes)
/* Magic helper macro to pad structures. Ignore those above. It's magic. */
#define PAD_BYTES(nr_bytes)		P4D_BYTES(__LINE__ , (nr_bytes))


/* Lightweight function to convert a frequency (in Mhz) to a channel number. */
static inline
u8 b43legacy_freq_to_channel_bg(int freq)
{
	u8 channel;

	if (freq == 2484)
		channel = 14;
	else
		channel = (freq - 2407) / 5;

	return channel;
}
static inline
u8 b43legacy_freq_to_channel(struct b43legacy_wldev *dev,
			     int freq)
{
	return b43legacy_freq_to_channel_bg(freq);
}

/* Lightweight function to convert a channel number to a frequency (in Mhz). */
static inline
int b43legacy_channel_to_freq_bg(u8 channel)
{
	int freq;

	if (channel == 14)
		freq = 2484;
	else
		freq = 2407 + (5 * channel);

	return freq;
}

static inline
int b43legacy_channel_to_freq(struct b43legacy_wldev *dev,
			      u8 channel)
{
	return b43legacy_channel_to_freq_bg(channel);
}

static inline
int b43legacy_is_cck_rate(int rate)
{
	return (rate == B43legacy_CCK_RATE_1MB ||
		rate == B43legacy_CCK_RATE_2MB ||
		rate == B43legacy_CCK_RATE_5MB ||
		rate == B43legacy_CCK_RATE_11MB);
}

static inline
int b43legacy_is_ofdm_rate(int rate)
{
	return !b43legacy_is_cck_rate(rate);
}

void b43legacy_tsf_read(struct b43legacy_wldev *dev, u64 *tsf);
void b43legacy_tsf_write(struct b43legacy_wldev *dev, u64 tsf);

u32 b43legacy_shm_read32(struct b43legacy_wldev *dev,
			 u16 routing, u16 offset);
u16 b43legacy_shm_read16(struct b43legacy_wldev *dev,
			 u16 routing, u16 offset);
void b43legacy_shm_write32(struct b43legacy_wldev *dev,
			 u16 routing, u16 offset,
			 u32 value);
void b43legacy_shm_write16(struct b43legacy_wldev *dev,
			 u16 routing, u16 offset,
			 u16 value);

u32 b43legacy_hf_read(struct b43legacy_wldev *dev);
void b43legacy_hf_write(struct b43legacy_wldev *dev, u32 value);

void b43legacy_dummy_transmission(struct b43legacy_wldev *dev);

void b43legacy_wireless_core_reset(struct b43legacy_wldev *dev, u32 flags);

void b43legacy_mac_suspend(struct b43legacy_wldev *dev);
void b43legacy_mac_enable(struct b43legacy_wldev *dev);

void b43legacy_controller_restart(struct b43legacy_wldev *dev,
				  const char *reason);

#endif /* B43legacy_MAIN_H_ */
