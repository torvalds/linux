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

#ifndef BCM43xx_PHY_H_
#define BCM43xx_PHY_H_

#include <linux/types.h>

struct bcm43xx_private;

void bcm43xx_raw_phy_lock(struct bcm43xx_private *bcm);
#define bcm43xx_phy_lock(bcm, flags) \
	do {					\
		local_irq_save(flags);		\
		bcm43xx_raw_phy_lock(bcm);	\
	} while (0)
void bcm43xx_raw_phy_unlock(struct bcm43xx_private *bcm);
#define bcm43xx_phy_unlock(bcm, flags) \
	do {					\
		bcm43xx_raw_phy_unlock(bcm);	\
		local_irq_restore(flags);	\
	} while (0)

/* Card uses the loopback gain stuff */
#define has_loopback_gain(phy) \
        (((phy)->rev > 1) || ((phy)->connected))

u16 bcm43xx_phy_read(struct bcm43xx_private *bcm, u16 offset);
void bcm43xx_phy_write(struct bcm43xx_private *bcm, u16 offset, u16 val);

int bcm43xx_phy_init_tssi2dbm_table(struct bcm43xx_private *bcm);
int bcm43xx_phy_init(struct bcm43xx_private *bcm);

void bcm43xx_phy_set_antenna_diversity(struct bcm43xx_private *bcm);
void bcm43xx_phy_calibrate(struct bcm43xx_private *bcm);
int bcm43xx_phy_connect(struct bcm43xx_private *bcm, int connect);

void bcm43xx_phy_lo_b_measure(struct bcm43xx_private *bcm);
void bcm43xx_phy_lo_g_measure(struct bcm43xx_private *bcm);
void bcm43xx_phy_xmitpower(struct bcm43xx_private *bcm);

/* Adjust the LocalOscillator to the saved values.
 * "fixed" is only set to 1 once in initialization. Set to 0 otherwise.
 */
void bcm43xx_phy_lo_adjust(struct bcm43xx_private *bcm, int fixed);
void bcm43xx_phy_lo_mark_all_unused(struct bcm43xx_private *bcm);

void bcm43xx_phy_set_baseband_attenuation(struct bcm43xx_private *bcm,
					  u16 baseband_attenuation);

#endif /* BCM43xx_PHY_H_ */
