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

#ifndef BCM43xx_RADIO_H_
#define BCM43xx_RADIO_H_

#include "bcm43xx.h"


#define BCM43xx_RADIO_DEFAULT_CHANNEL_A		36
#define BCM43xx_RADIO_DEFAULT_CHANNEL_BG	6

/* Force antenna 0. */
#define BCM43xx_RADIO_TXANTENNA_0		0
/* Force antenna 1. */
#define BCM43xx_RADIO_TXANTENNA_1		1
/* Use the RX antenna, that was selected for the most recently
 * received good PLCP header.
 */
#define BCM43xx_RADIO_TXANTENNA_LASTPLCP	3
#define BCM43xx_RADIO_TXANTENNA_DEFAULT		BCM43xx_RADIO_TXANTENNA_LASTPLCP

#define BCM43xx_RADIO_INTERFMODE_NONE		0
#define BCM43xx_RADIO_INTERFMODE_NONWLAN	1
#define BCM43xx_RADIO_INTERFMODE_MANUALWLAN	2
#define BCM43xx_RADIO_INTERFMODE_AUTOWLAN	3


void bcm43xx_radio_lock(struct bcm43xx_private *bcm);
void bcm43xx_radio_unlock(struct bcm43xx_private *bcm);

u16 bcm43xx_radio_read16(struct bcm43xx_private *bcm, u16 offset);
void bcm43xx_radio_write16(struct bcm43xx_private *bcm, u16 offset, u16 val);

u16 bcm43xx_radio_init2050(struct bcm43xx_private *bcm);
void bcm43xx_radio_init2060(struct bcm43xx_private *bcm);

void bcm43xx_radio_turn_on(struct bcm43xx_private *bcm);
void bcm43xx_radio_turn_off(struct bcm43xx_private *bcm);

static inline
int bcm43xx_is_hw_radio_enabled(struct bcm43xx_private *bcm)
{
	/* function to return state of hardware enable of radio
	 * returns 0 if radio disabled, 1 if radio enabled
	 */
	if (bcm->current_core->rev >= 3)
		return ((bcm43xx_read32(bcm, BCM43xx_MMIO_RADIO_HWENABLED_HI)
					& BCM43xx_MMIO_RADIO_HWENABLED_HI_MASK)
					== 0) ? 1 : 0;
	else
		return ((bcm43xx_read16(bcm, BCM43xx_MMIO_RADIO_HWENABLED_LO)
					& BCM43xx_MMIO_RADIO_HWENABLED_LO_MASK)
					== 0) ? 0 : 1;
}

int bcm43xx_radio_selectchannel(struct bcm43xx_private *bcm, u8 channel,
				int synthetic_pu_workaround);

void bcm43xx_radio_set_txpower_a(struct bcm43xx_private *bcm, u16 txpower);
void bcm43xx_radio_set_txpower_bg(struct bcm43xx_private *bcm,
                               u16 baseband_attenuation, u16 attenuation,
			       u16 txpower);

u16 bcm43xx_default_baseband_attenuation(struct bcm43xx_private *bcm);
u16 bcm43xx_default_radio_attenuation(struct bcm43xx_private *bcm);
u16 bcm43xx_default_txctl1(struct bcm43xx_private *bcm);

void bcm43xx_radio_set_txantenna(struct bcm43xx_private *bcm, u32 val);

void bcm43xx_radio_clear_tssi(struct bcm43xx_private *bcm);

u8 bcm43xx_radio_aci_detect(struct bcm43xx_private *bcm, u8 channel);
u8 bcm43xx_radio_aci_scan(struct bcm43xx_private *bcm);

int bcm43xx_radio_set_interference_mitigation(struct bcm43xx_private *bcm, int mode);

void bcm43xx_calc_nrssi_slope(struct bcm43xx_private *bcm);
void bcm43xx_calc_nrssi_threshold(struct bcm43xx_private *bcm);
s16 bcm43xx_nrssi_hw_read(struct bcm43xx_private *bcm, u16 offset);
void bcm43xx_nrssi_hw_write(struct bcm43xx_private *bcm, u16 offset, s16 val);
void bcm43xx_nrssi_hw_update(struct bcm43xx_private *bcm, u16 val);
void bcm43xx_nrssi_mem_update(struct bcm43xx_private *bcm);

void bcm43xx_radio_set_tx_iq(struct bcm43xx_private *bcm);
u16 bcm43xx_radio_calibrationvalue(struct bcm43xx_private *bcm);

#endif /* BCM43xx_RADIO_H_ */
