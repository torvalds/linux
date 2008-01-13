/*

  Broadcom B43 wireless driver
  IEEE 802.11n PHY support

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
#include "nphy.h"
#include "tables_nphy.h"


void b43_nphy_set_rxantenna(struct b43_wldev *dev, int antenna)
{//TODO
}

void b43_nphy_xmitpower(struct b43_wldev *dev)
{//TODO
}

/* Tune the hardware to a new channel. Don't call this directly.
 * Use b43_radio_selectchannel() */
void b43_nphy_selectchannel(struct b43_wldev *dev, u8 channel)
{

//TODO
}

static void b43_radio_init2055_pre(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
		     ~B43_NPHY_RFCTL_CMD_PORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
		    B43_NPHY_RFCTL_CMD_CHIP0PU |
		    B43_NPHY_RFCTL_CMD_OEPORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
		    B43_NPHY_RFCTL_CMD_PORFORCE);
}

static void b43_radio_init2055_post(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = &(dev->dev->bus->sprom);
	struct ssb_boardinfo *binfo = &(dev->dev->bus->boardinfo);
	int i;
	u16 val;

	b43_radio_mask(dev, B2055_MASTER1, 0xFFF3);
	msleep(1);
	if ((sprom->revision != 4) || !(sprom->boardflags_hi & 0x0002)) {
		if ((binfo->vendor != PCI_VENDOR_ID_BROADCOM) ||
		    (binfo->type != 0x46D) ||
		    (binfo->rev < 0x41)) {
			b43_radio_mask(dev, B2055_C1_RX_BB_REG, 0x7F);
			b43_radio_mask(dev, B2055_C1_RX_BB_REG, 0x7F);
			msleep(1);
		}
	}
	b43_radio_maskset(dev, B2055_RRCCAL_NOPTSEL, 0x3F, 0x2C);
	msleep(1);
	b43_radio_write16(dev, B2055_CAL_MISC, 0x3C);
	msleep(1);
	b43_radio_mask(dev, B2055_CAL_MISC, 0xFFBE);
	msleep(1);
	b43_radio_set(dev, B2055_CAL_LPOCTL, 0x80);
	msleep(1);
	b43_radio_set(dev, B2055_CAL_MISC, 0x1);
	msleep(1);
	b43_radio_set(dev, B2055_CAL_MISC, 0x40);
	msleep(1);
	for (i = 0; i < 100; i++) {
		val = b43_radio_read16(dev, B2055_CAL_COUT2);
		if (val & 0x80)
			break;
		udelay(10);
	}
	msleep(1);
	b43_radio_mask(dev, B2055_CAL_LPOCTL, 0xFF7F);
	msleep(1);
	b43_radio_selectchannel(dev, dev->phy.channel, 0);
	b43_radio_write16(dev, B2055_C1_RX_BB_LPF, 0x9);
	b43_radio_write16(dev, B2055_C2_RX_BB_LPF, 0x9);
	b43_radio_write16(dev, B2055_C1_RX_BB_MIDACHP, 0x83);
	b43_radio_write16(dev, B2055_C2_RX_BB_MIDACHP, 0x83);
}

/* Initialize a Broadcom 2055 N-radio */
static void b43_radio_init2055(struct b43_wldev *dev)
{
	b43_radio_init2055_pre(dev);
	if (b43_status(dev) < B43_STAT_INITIALIZED)
		b2055_upload_inittab(dev, 0, 1);
	else
		b2055_upload_inittab(dev, 0/*FIXME on 5ghz band*/, 0);
	b43_radio_init2055_post(dev);
}

void b43_nphy_radio_turn_on(struct b43_wldev *dev)
{
	b43_radio_init2055(dev);
}

void b43_nphy_radio_turn_off(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
		     ~B43_NPHY_RFCTL_CMD_EN);
}

int b43_phy_initn(struct b43_wldev *dev)
{
	b43err(dev->wl, "IEEE 802.11n devices are not supported, yet.\n");

	return 0;
}
