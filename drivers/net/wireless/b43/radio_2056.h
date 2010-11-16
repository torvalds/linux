/*

  Broadcom B43 wireless driver

  Copyright (c) 2010 Rafał Miłecki <zajec5@gmail.com>

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

#ifndef B43_RADIO_2056_H_
#define B43_RADIO_2056_H_

#include <linux/types.h>

#include "tables_nphy.h"

struct b43_nphy_channeltab_entry_rev3 {
	/* The channel number */
	u8 channel;
	/* The channel frequency in MHz */
	u16 freq;
	/* Radio register values on channelswitch */
	/* TODO */
	/* PHY register values on channelswitch */
	struct b43_phy_n_sfo_cfg phy_regs;
};

#endif /* B43_RADIO_2056_H_ */
