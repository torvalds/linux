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

#ifndef BCM43xx_POWER_H_
#define BCM43xx_POWER_H_

#include <linux/types.h>

/* Clock sources */
enum {
	/* PCI clock */
	BCM43xx_PCTL_CLKSRC_PCI,
	/* Crystal slow clock oscillator */
	BCM43xx_PCTL_CLKSRC_XTALOS,
	/* Low power oscillator */
	BCM43xx_PCTL_CLKSRC_LOPWROS,
};

struct bcm43xx_private;

int bcm43xx_pctl_init(struct bcm43xx_private *bcm);
int bcm43xx_pctl_set_clock(struct bcm43xx_private *bcm, u16 mode);
int bcm43xx_pctl_set_crystal(struct bcm43xx_private *bcm, int on);
u16 bcm43xx_pctl_powerup_delay(struct bcm43xx_private *bcm);

void bcm43xx_power_saving_ctl_bits(struct bcm43xx_private *bcm,
				   int bit25, int bit26);

#endif /* BCM43xx_POWER_H_ */
