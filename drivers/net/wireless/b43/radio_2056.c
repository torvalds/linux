/*

  Broadcom B43 wireless driver
  IEEE 802.11n 2056 radio device data tables

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

#include "b43.h"
#include "radio_2056.h"
#include "phy_common.h"

static const struct b43_nphy_channeltab_entry_rev3 b43_nphy_channeltab_rev3[] = {
};

const struct b43_nphy_channeltab_entry_rev3 *
b43_nphy_get_chantabent_rev3(struct b43_wldev *dev, u16 freq)
{
	const struct b43_nphy_channeltab_entry_rev3 *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b43_nphy_channeltab_rev3); i++) {
		e = &(b43_nphy_channeltab_rev3[i]);
		if (e->freq == freq)
			return e;
	}

	return NULL;
}
