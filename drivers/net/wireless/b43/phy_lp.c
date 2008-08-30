/*

  Broadcom B43 wireless driver
  IEEE 802.11g LP-PHY driver

  Copyright (c) 2008 Michael Buesch <mb@bu3sch.de>

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
#include "phy_lp.h"
#include "phy_common.h"


static int b43_lpphy_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy;

	lpphy = kzalloc(sizeof(*lpphy), GFP_KERNEL);
	if (!lpphy)
		return -ENOMEM;
	dev->phy.lp = lpphy;

	//TODO

	return 0;
}

static int b43_lpphy_op_init(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	//TODO
	lpphy->initialised = 1;

	return 0;
}

static void b43_lpphy_op_exit(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	if (lpphy->initialised) {
		//TODO
		lpphy->initialised = 0;
	}

	kfree(lpphy);
	dev->phy.lp = NULL;
}

static u16 b43_lpphy_op_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_lpphy_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static u16 b43_lpphy_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);
	/* LP-PHY needs a special bit set for read access */
	if (dev->phy.rev < 2) {
		if (reg != 0x4001)
			reg |= 0x100;
	} else
		reg |= 0x200;

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
}

static void b43_lpphy_op_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO_DATA_LOW, value);
}

static void b43_lpphy_op_software_rfkill(struct b43_wldev *dev,
					 enum rfkill_state state)
{
	//TODO
}

static int b43_lpphy_op_switch_channel(struct b43_wldev *dev,
				       unsigned int new_channel)
{
	//TODO
	return 0;
}

static unsigned int b43_lpphy_op_get_default_chan(struct b43_wldev *dev)
{
	return 1; /* Default to channel 1 */
}

static void b43_lpphy_op_set_rx_antenna(struct b43_wldev *dev, int antenna)
{
	//TODO
}

static void b43_lpphy_op_adjust_txpower(struct b43_wldev *dev)
{
	//TODO
}

static enum b43_txpwr_result b43_lpphy_op_recalc_txpower(struct b43_wldev *dev,
							 bool ignore_tssi)
{
	//TODO
	return B43_TXPWR_RES_DONE;
}


const struct b43_phy_operations b43_phyops_lp = {
	.allocate		= b43_lpphy_op_allocate,
	.init			= b43_lpphy_op_init,
	.exit			= b43_lpphy_op_exit,
	.phy_read		= b43_lpphy_op_read,
	.phy_write		= b43_lpphy_op_write,
	.radio_read		= b43_lpphy_op_radio_read,
	.radio_write		= b43_lpphy_op_radio_write,
	.software_rfkill	= b43_lpphy_op_software_rfkill,
	.switch_channel		= b43_lpphy_op_switch_channel,
	.get_default_chan	= b43_lpphy_op_get_default_chan,
	.set_rx_antenna		= b43_lpphy_op_set_rx_antenna,
	.recalc_txpower		= b43_lpphy_op_recalc_txpower,
	.adjust_txpower		= b43_lpphy_op_adjust_txpower,
};
