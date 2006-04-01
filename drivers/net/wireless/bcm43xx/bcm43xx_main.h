/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
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

#ifndef BCM43xx_MAIN_H_
#define BCM43xx_MAIN_H_

#include "bcm43xx.h"

#ifdef CONFIG_BCM947XX
#define atoi(str) simple_strtoul(((str != NULL) ? str : ""), NULL, 0)

static inline void e_aton(char *str, char *dest)
{
	int i = 0;
	u16 *d = (u16 *) dest;

	for (;;) {
		dest[i++] = (char) simple_strtoul(str, NULL, 16);
		str += 2;
		if (!*str++ || i == 6)
			break;
	}
	for (i = 0; i < 3; i++)
		d[i] = cpu_to_be16(d[i]);
}
#endif

#define P4D_BYT3S(magic, nr_bytes)	u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)	P4D_BYT3S(line, nr_bytes)
/* Magic helper macro to pad structures. Ignore those above. It's magic. */
#define PAD_BYTES(nr_bytes)		P4D_BYTES( __LINE__ , (nr_bytes))


/* Lightweight function to convert a frequency (in Mhz) to a channel number. */
static inline
u8 bcm43xx_freq_to_channel_a(int freq)
{
	return ((freq - 5000) / 5);
}
static inline
u8 bcm43xx_freq_to_channel_bg(int freq)
{
	u8 channel;

	if (freq == 2484)
		channel = 14;
	else
		channel = (freq - 2407) / 5;

	return channel;
}
static inline
u8 bcm43xx_freq_to_channel(struct bcm43xx_private *bcm,
			   int freq)
{
	if (bcm43xx_current_phy(bcm)->type == BCM43xx_PHYTYPE_A)
		return bcm43xx_freq_to_channel_a(freq);
	return bcm43xx_freq_to_channel_bg(freq);
}

/* Lightweight function to convert a channel number to a frequency (in Mhz). */
static inline
int bcm43xx_channel_to_freq_a(u8 channel)
{
	return (5000 + (5 * channel));
}
static inline
int bcm43xx_channel_to_freq_bg(u8 channel)
{
	int freq;

	if (channel == 14)
		freq = 2484;
	else
		freq = 2407 + (5 * channel);

	return freq;
}
static inline
int bcm43xx_channel_to_freq(struct bcm43xx_private *bcm,
			    u8 channel)
{
	if (bcm43xx_current_phy(bcm)->type == BCM43xx_PHYTYPE_A)
		return bcm43xx_channel_to_freq_a(channel);
	return bcm43xx_channel_to_freq_bg(channel);
}

/* Lightweight function to check if a channel number is valid.
 * Note that this does _NOT_ check for geographical restrictions!
 */
static inline
int bcm43xx_is_valid_channel_a(u8 channel)
{
	return (channel <= 200);
}
static inline
int bcm43xx_is_valid_channel_bg(u8 channel)
{
	return (channel >= 1 && channel <= 14);
}
static inline
int bcm43xx_is_valid_channel(struct bcm43xx_private *bcm,
			     u8 channel)
{
	if (bcm43xx_current_phy(bcm)->type == BCM43xx_PHYTYPE_A)
		return bcm43xx_is_valid_channel_a(channel);
	return bcm43xx_is_valid_channel_bg(channel);
}

void bcm43xx_tsf_read(struct bcm43xx_private *bcm, u64 *tsf);
void bcm43xx_tsf_write(struct bcm43xx_private *bcm, u64 tsf);

void bcm43xx_set_iwmode(struct bcm43xx_private *bcm,
			int iw_mode);

u32 bcm43xx_shm_read32(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset);
u16 bcm43xx_shm_read16(struct bcm43xx_private *bcm,
		       u16 routing, u16 offset);
void bcm43xx_shm_write32(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u32 value);
void bcm43xx_shm_write16(struct bcm43xx_private *bcm,
			 u16 routing, u16 offset,
			 u16 value);

void bcm43xx_dummy_transmission(struct bcm43xx_private *bcm);

int bcm43xx_switch_core(struct bcm43xx_private *bcm, struct bcm43xx_coreinfo *new_core);

void bcm43xx_wireless_core_reset(struct bcm43xx_private *bcm, int connect_phy);

void bcm43xx_mac_suspend(struct bcm43xx_private *bcm);
void bcm43xx_mac_enable(struct bcm43xx_private *bcm);

void bcm43xx_controller_restart(struct bcm43xx_private *bcm, const char *reason);

int bcm43xx_sprom_read(struct bcm43xx_private *bcm, u16 *sprom);
int bcm43xx_sprom_write(struct bcm43xx_private *bcm, const u16 *sprom);

#endif /* BCM43xx_MAIN_H_ */
