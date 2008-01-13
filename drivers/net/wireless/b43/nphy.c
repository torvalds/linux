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

static void b43_chantab_radio_upload(struct b43_wldev *dev,
				     const struct b43_nphy_channeltab_entry *e)
{
	b43_radio_write16(dev, B2055_PLL_REF, e->radio_pll_ref);
	b43_radio_write16(dev, B2055_RF_PLLMOD0, e->radio_rf_pllmod0);
	b43_radio_write16(dev, B2055_RF_PLLMOD1, e->radio_rf_pllmod1);
	b43_radio_write16(dev, B2055_VCO_CAPTAIL, e->radio_vco_captail);
	b43_radio_write16(dev, B2055_VCO_CAL1, e->radio_vco_cal1);
	b43_radio_write16(dev, B2055_VCO_CAL2, e->radio_vco_cal2);
	b43_radio_write16(dev, B2055_PLL_LFC1, e->radio_pll_lfc1);
	b43_radio_write16(dev, B2055_PLL_LFR1, e->radio_pll_lfr1);
	b43_radio_write16(dev, B2055_PLL_LFC2, e->radio_pll_lfc2);
	b43_radio_write16(dev, B2055_LGBUF_CENBUF, e->radio_lgbuf_cenbuf);
	b43_radio_write16(dev, B2055_LGEN_TUNE1, e->radio_lgen_tune1);
	b43_radio_write16(dev, B2055_LGEN_TUNE2, e->radio_lgen_tune2);
	b43_radio_write16(dev, B2055_C1_LGBUF_ATUNE, e->radio_c1_lgbuf_atune);
	b43_radio_write16(dev, B2055_C1_LGBUF_GTUNE, e->radio_c1_lgbuf_gtune);
	b43_radio_write16(dev, B2055_C1_RX_RFR1, e->radio_c1_rx_rfr1);
	b43_radio_write16(dev, B2055_C1_TX_PGAPADTN, e->radio_c1_tx_pgapadtn);
	b43_radio_write16(dev, B2055_C1_TX_MXBGTRIM, e->radio_c1_tx_mxbgtrim);
	b43_radio_write16(dev, B2055_C2_LGBUF_ATUNE, e->radio_c2_lgbuf_atune);
	b43_radio_write16(dev, B2055_C2_LGBUF_GTUNE, e->radio_c2_lgbuf_gtune);
	b43_radio_write16(dev, B2055_C2_RX_RFR1, e->radio_c2_rx_rfr1);
	b43_radio_write16(dev, B2055_C2_TX_PGAPADTN, e->radio_c2_tx_pgapadtn);
	b43_radio_write16(dev, B2055_C2_TX_MXBGTRIM, e->radio_c2_tx_mxbgtrim);
}

static void b43_chantab_phy_upload(struct b43_wldev *dev,
				   const struct b43_nphy_channeltab_entry *e)
{
	b43_phy_write(dev, B43_NPHY_BW1A, e->phy_bw1a);
	b43_phy_write(dev, B43_NPHY_BW2, e->phy_bw2);
	b43_phy_write(dev, B43_NPHY_BW3, e->phy_bw3);
	b43_phy_write(dev, B43_NPHY_BW4, e->phy_bw4);
	b43_phy_write(dev, B43_NPHY_BW5, e->phy_bw5);
	b43_phy_write(dev, B43_NPHY_BW6, e->phy_bw6);
}

static void b43_nphy_tx_power_fix(struct b43_wldev *dev)
{
	//TODO
}

/* Tune the hardware to a new channel. Don't call this directly.
 * Use b43_radio_selectchannel() */
int b43_nphy_selectchannel(struct b43_wldev *dev, u8 channel)
{
	const struct b43_nphy_channeltab_entry *tabent;

	tabent = b43_nphy_get_chantabent(dev, channel);
	if (!tabent)
		return -ESRCH;

	//FIXME enable/disable band select upper20 in RXCTL
	if (0 /*FIXME 5Ghz*/)
		b43_radio_maskset(dev, B2055_MASTER1, 0xFF8F, 0x20);
	else
		b43_radio_maskset(dev, B2055_MASTER1, 0xFF8F, 0x50);
	b43_chantab_radio_upload(dev, tabent);
	udelay(50);
	b43_radio_write16(dev, B2055_VCO_CAL10, 5);
	b43_radio_write16(dev, B2055_VCO_CAL10, 45);
	b43_radio_write16(dev, B2055_VCO_CAL10, 65);
	udelay(300);
	if (0 /*FIXME 5Ghz*/)
		b43_phy_set(dev, B43_NPHY_BANDCTL, B43_NPHY_BANDCTL_5GHZ);
	else
		b43_phy_mask(dev, B43_NPHY_BANDCTL, ~B43_NPHY_BANDCTL_5GHZ);
	b43_chantab_phy_upload(dev, tabent);
	b43_nphy_tx_power_fix(dev);

	return 0;
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
