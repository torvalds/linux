/*

  Broadcom B43 wireless driver
  Bus abstraction layer

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
#include "bus.h"


/* SSB */
struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssb_device *sdev)
{
	struct b43_bus_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	dev->bus_type = B43_BUS_SSB;
	dev->sdev = sdev;

	return dev;
}
