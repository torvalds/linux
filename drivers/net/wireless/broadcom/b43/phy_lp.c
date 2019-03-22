/*

  Broadcom B43 wireless driver
  IEEE 802.11a/g LP-PHY driver

  Copyright (c) 2008-2009 Michael Buesch <m@bues.ch>
  Copyright (c) 2009 Gábor Stefanik <netrolller.3d@gmail.com>

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

#include <linux/cordic.h>
#include <linux/slab.h>

#include "b43.h"
#include "main.h"
#include "phy_lp.h"
#include "phy_common.h"
#include "tables_lpphy.h"


static inline u16 channel2freq_lp(u8 channel)
{
	if (channel < 14)
		return (2407 + 5 * channel);
	else if (channel == 14)
		return 2484;
	else if (channel < 184)
		return (5000 + 5 * channel);
	else
		return (4000 + 5 * channel);
}

static unsigned int b43_lpphy_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
		return 1;
	return 36;
}

static int b43_lpphy_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy;

	lpphy = kzalloc(sizeof(*lpphy), GFP_KERNEL);
	if (!lpphy)
		return -ENOMEM;
	dev->phy.lp = lpphy;

	return 0;
}

static void b43_lpphy_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lp *lpphy = phy->lp;

	memset(lpphy, 0, sizeof(*lpphy));
	lpphy->antenna = B43_ANTENNA_DEFAULT;

	//TODO
}

static void b43_lpphy_op_free(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	kfree(lpphy);
	dev->phy.lp = NULL;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/LP/ReadBandSrom */
static void lpphy_read_band_sprom(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 cckpo, maxpwr;
	u32 ofdmpo;
	int i;

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		lpphy->tx_isolation_med_band = sprom->tri2g;
		lpphy->bx_arch = sprom->bxa2g;
		lpphy->rx_pwr_offset = sprom->rxpo2g;
		lpphy->rssi_vf = sprom->rssismf2g;
		lpphy->rssi_vc = sprom->rssismc2g;
		lpphy->rssi_gs = sprom->rssisav2g;
		lpphy->txpa[0] = sprom->pa0b0;
		lpphy->txpa[1] = sprom->pa0b1;
		lpphy->txpa[2] = sprom->pa0b2;
		maxpwr = sprom->maxpwr_bg;
		lpphy->max_tx_pwr_med_band = maxpwr;
		cckpo = sprom->cck2gpo;
		if (cckpo) {
			ofdmpo = sprom->ofdm2gpo;
			for (i = 0; i < 4; i++) {
				lpphy->tx_max_rate[i] =
					maxpwr - (ofdmpo & 0xF) * 2;
				ofdmpo >>= 4;
			}
			ofdmpo = sprom->ofdm2gpo;
			for (i = 4; i < 15; i++) {
				lpphy->tx_max_rate[i] =
					maxpwr - (ofdmpo & 0xF) * 2;
				ofdmpo >>= 4;
			}
		} else {
			u8 opo = sprom->opo;
			for (i = 0; i < 4; i++)
				lpphy->tx_max_rate[i] = maxpwr;
			for (i = 4; i < 15; i++)
				lpphy->tx_max_rate[i] = maxpwr - opo;
		}
	} else { /* 5GHz */
		lpphy->tx_isolation_low_band = sprom->tri5gl;
		lpphy->tx_isolation_med_band = sprom->tri5g;
		lpphy->tx_isolation_hi_band = sprom->tri5gh;
		lpphy->bx_arch = sprom->bxa5g;
		lpphy->rx_pwr_offset = sprom->rxpo5g;
		lpphy->rssi_vf = sprom->rssismf5g;
		lpphy->rssi_vc = sprom->rssismc5g;
		lpphy->rssi_gs = sprom->rssisav5g;
		lpphy->txpa[0] = sprom->pa1b0;
		lpphy->txpa[1] = sprom->pa1b1;
		lpphy->txpa[2] = sprom->pa1b2;
		lpphy->txpal[0] = sprom->pa1lob0;
		lpphy->txpal[1] = sprom->pa1lob1;
		lpphy->txpal[2] = sprom->pa1lob2;
		lpphy->txpah[0] = sprom->pa1hib0;
		lpphy->txpah[1] = sprom->pa1hib1;
		lpphy->txpah[2] = sprom->pa1hib2;
		maxpwr = sprom->maxpwr_al;
		ofdmpo = sprom->ofdm5glpo;
		lpphy->max_tx_pwr_low_band = maxpwr;
		for (i = 4; i < 12; i++) {
			lpphy->tx_max_ratel[i] = maxpwr - (ofdmpo & 0xF) * 2;
			ofdmpo >>= 4;
		}
		maxpwr = sprom->maxpwr_a;
		ofdmpo = sprom->ofdm5gpo;
		lpphy->max_tx_pwr_med_band = maxpwr;
		for (i = 4; i < 12; i++) {
			lpphy->tx_max_rate[i] = maxpwr - (ofdmpo & 0xF) * 2;
			ofdmpo >>= 4;
		}
		maxpwr = sprom->maxpwr_ah;
		ofdmpo = sprom->ofdm5ghpo;
		lpphy->max_tx_pwr_hi_band = maxpwr;
		for (i = 4; i < 12; i++) {
			lpphy->tx_max_rateh[i] = maxpwr - (ofdmpo & 0xF) * 2;
			ofdmpo >>= 4;
		}
	}
}

static void lpphy_adjust_gain_table(struct b43_wldev *dev, u32 freq)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 temp[3];
	u16 isolation;

	B43_WARN_ON(dev->phy.rev >= 2);

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
		isolation = lpphy->tx_isolation_med_band;
	else if (freq <= 5320)
		isolation = lpphy->tx_isolation_low_band;
	else if (freq <= 5700)
		isolation = lpphy->tx_isolation_med_band;
	else
		isolation = lpphy->tx_isolation_hi_band;

	temp[0] = ((isolation - 26) / 12) << 12;
	temp[1] = temp[0] + 0x1000;
	temp[2] = temp[0] + 0x2000;

	b43_lptab_write_bulk(dev, B43_LPTAB16(13, 0), 3, temp);
	b43_lptab_write_bulk(dev, B43_LPTAB16(12, 0), 3, temp);
}

static void lpphy_table_init(struct b43_wldev *dev)
{
	u32 freq = channel2freq_lp(b43_lpphy_op_get_default_chan(dev));

	if (dev->phy.rev < 2)
		lpphy_rev0_1_table_init(dev);
	else
		lpphy_rev2plus_table_init(dev);

	lpphy_init_tx_gain_table(dev);

	if (dev->phy.rev < 2)
		lpphy_adjust_gain_table(dev, freq);
}

static void lpphy_baseband_rev0_1_init(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->sdev->bus;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 tmp, tmp2;

	b43_phy_mask(dev, B43_LPPHY_AFE_DAC_CTL, 0xF7FF);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL, 0);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL_OVR, 0);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_0, 0);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_2, 0);
	b43_phy_set(dev, B43_LPPHY_AFE_DAC_CTL, 0x0004);
	b43_phy_maskset(dev, B43_LPPHY_OFDMSYNCTHRESH0, 0xFF00, 0x0078);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0x83FF, 0x5800);
	b43_phy_write(dev, B43_LPPHY_ADC_COMPENSATION_CTL, 0x0016);
	b43_phy_maskset(dev, B43_LPPHY_AFE_ADC_CTL_0, 0xFFF8, 0x0004);
	b43_phy_maskset(dev, B43_LPPHY_VERYLOWGAINDB, 0x00FF, 0x5400);
	b43_phy_maskset(dev, B43_LPPHY_HIGAINDB, 0x00FF, 0x2400);
	b43_phy_maskset(dev, B43_LPPHY_LOWGAINDB, 0x00FF, 0x2100);
	b43_phy_maskset(dev, B43_LPPHY_VERYLOWGAINDB, 0xFF00, 0x0006);
	b43_phy_mask(dev, B43_LPPHY_RX_RADIO_CTL, 0xFFFE);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0xFFE0, 0x0005);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0xFC1F, 0x0180);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0x83FF, 0x3C00);
	b43_phy_maskset(dev, B43_LPPHY_GAINDIRECTMISMATCH, 0xFFF0, 0x0005);
	b43_phy_maskset(dev, B43_LPPHY_GAIN_MISMATCH_LIMIT, 0xFFC0, 0x001A);
	b43_phy_maskset(dev, B43_LPPHY_CRS_ED_THRESH, 0xFF00, 0x00B3);
	b43_phy_maskset(dev, B43_LPPHY_CRS_ED_THRESH, 0x00FF, 0xAD00);
	b43_phy_maskset(dev, B43_LPPHY_INPUT_PWRDB,
			0xFF00, lpphy->rx_pwr_offset);
	if ((sprom->boardflags_lo & B43_BFL_FEM) &&
	   ((b43_current_band(dev->wl) == NL80211_BAND_5GHZ) ||
	   (sprom->boardflags_hi & B43_BFH_PAREF))) {
		ssb_pmu_set_ldo_voltage(&bus->chipco, LDO_PAREF, 0x28);
		ssb_pmu_set_ldo_paref(&bus->chipco, true);
		if (dev->phy.rev == 0) {
			b43_phy_maskset(dev, B43_LPPHY_LP_RF_SIGNAL_LUT,
					0xFFCF, 0x0010);
		}
		b43_lptab_write(dev, B43_LPTAB16(11, 7), 60);
	} else {
		ssb_pmu_set_ldo_paref(&bus->chipco, false);
		b43_phy_maskset(dev, B43_LPPHY_LP_RF_SIGNAL_LUT,
				0xFFCF, 0x0020);
		b43_lptab_write(dev, B43_LPTAB16(11, 7), 100);
	}
	tmp = lpphy->rssi_vf | lpphy->rssi_vc << 4 | 0xA000;
	b43_phy_write(dev, B43_LPPHY_AFE_RSSI_CTL_0, tmp);
	if (sprom->boardflags_hi & B43_BFH_RSSIINV)
		b43_phy_maskset(dev, B43_LPPHY_AFE_RSSI_CTL_1, 0xF000, 0x0AAA);
	else
		b43_phy_maskset(dev, B43_LPPHY_AFE_RSSI_CTL_1, 0xF000, 0x02AA);
	b43_lptab_write(dev, B43_LPTAB16(11, 1), 24);
	b43_phy_maskset(dev, B43_LPPHY_RX_RADIO_CTL,
			0xFFF9, (lpphy->bx_arch << 1));
	if (dev->phy.rev == 1 &&
	   (sprom->boardflags_hi & B43_BFH_FEM_BT)) {
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0x3F00, 0x0900);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xC0FF, 0x0B00);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xC0FF, 0x0400);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xC0FF, 0x0B00);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_5, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_5, 0xC0FF, 0x0900);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_6, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_6, 0xC0FF, 0x0B00);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_7, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_7, 0xC0FF, 0x0900);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_8, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_8, 0xC0FF, 0x0B00);
	} else if (b43_current_band(dev->wl) == NL80211_BAND_5GHZ ||
		   (dev->dev->board_type == SSB_BOARD_BU4312) ||
		   (dev->phy.rev == 0 && (sprom->boardflags_lo & B43_BFL_FEM))) {
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xFFC0, 0x0001);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xC0FF, 0x0400);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xFFC0, 0x0001);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xC0FF, 0x0500);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xFFC0, 0x0002);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xC0FF, 0x0800);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xFFC0, 0x0002);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xC0FF, 0x0A00);
	} else if (dev->phy.rev == 1 ||
		  (sprom->boardflags_lo & B43_BFL_FEM)) {
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xFFC0, 0x0004);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xC0FF, 0x0800);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xFFC0, 0x0004);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xC0FF, 0x0C00);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xFFC0, 0x0002);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xC0FF, 0x0100);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xFFC0, 0x0002);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xC0FF, 0x0300);
	} else {
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_1, 0xC0FF, 0x0900);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xFFC0, 0x000A);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_2, 0xC0FF, 0x0B00);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xFFC0, 0x0006);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_3, 0xC0FF, 0x0500);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xFFC0, 0x0006);
		b43_phy_maskset(dev, B43_LPPHY_TR_LOOKUP_4, 0xC0FF, 0x0700);
	}
	if (dev->phy.rev == 1 && (sprom->boardflags_hi & B43_BFH_PAREF)) {
		b43_phy_copy(dev, B43_LPPHY_TR_LOOKUP_5, B43_LPPHY_TR_LOOKUP_1);
		b43_phy_copy(dev, B43_LPPHY_TR_LOOKUP_6, B43_LPPHY_TR_LOOKUP_2);
		b43_phy_copy(dev, B43_LPPHY_TR_LOOKUP_7, B43_LPPHY_TR_LOOKUP_3);
		b43_phy_copy(dev, B43_LPPHY_TR_LOOKUP_8, B43_LPPHY_TR_LOOKUP_4);
	}
	if ((sprom->boardflags_hi & B43_BFH_FEM_BT) &&
	    (dev->dev->chip_id == 0x5354) &&
	    (dev->dev->chip_pkg == SSB_CHIPPACK_BCM4712S)) {
		b43_phy_set(dev, B43_LPPHY_CRSGAIN_CTL, 0x0006);
		b43_phy_write(dev, B43_LPPHY_GPIO_SELECT, 0x0005);
		b43_phy_write(dev, B43_LPPHY_GPIO_OUTEN, 0xFFFF);
		//FIXME the Broadcom driver caches & delays this HF write!
		b43_hf_write(dev, b43_hf_read(dev) | B43_HF_PR45960W);
	}
	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		b43_phy_set(dev, B43_LPPHY_LP_PHY_CTL, 0x8000);
		b43_phy_set(dev, B43_LPPHY_CRSGAIN_CTL, 0x0040);
		b43_phy_maskset(dev, B43_LPPHY_MINPWR_LEVEL, 0x00FF, 0xA400);
		b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL, 0xF0FF, 0x0B00);
		b43_phy_maskset(dev, B43_LPPHY_SYNCPEAKCNT, 0xFFF8, 0x0007);
		b43_phy_maskset(dev, B43_LPPHY_DSSS_CONFIRM_CNT, 0xFFF8, 0x0003);
		b43_phy_maskset(dev, B43_LPPHY_DSSS_CONFIRM_CNT, 0xFFC7, 0x0020);
		b43_phy_mask(dev, B43_LPPHY_IDLEAFTERPKTRXTO, 0x00FF);
	} else { /* 5GHz */
		b43_phy_mask(dev, B43_LPPHY_LP_PHY_CTL, 0x7FFF);
		b43_phy_mask(dev, B43_LPPHY_CRSGAIN_CTL, 0xFFBF);
	}
	if (dev->phy.rev == 1) {
		tmp = b43_phy_read(dev, B43_LPPHY_CLIPCTRTHRESH);
		tmp2 = (tmp & 0x03E0) >> 5;
		tmp2 |= tmp2 << 5;
		b43_phy_write(dev, B43_LPPHY_4C3, tmp2);
		tmp = b43_phy_read(dev, B43_LPPHY_GAINDIRECTMISMATCH);
		tmp2 = (tmp & 0x1F00) >> 8;
		tmp2 |= tmp2 << 5;
		b43_phy_write(dev, B43_LPPHY_4C4, tmp2);
		tmp = b43_phy_read(dev, B43_LPPHY_VERYLOWGAINDB);
		tmp2 = tmp & 0x00FF;
		tmp2 |= tmp << 8;
		b43_phy_write(dev, B43_LPPHY_4C5, tmp2);
	}
}

static void lpphy_save_dig_flt_state(struct b43_wldev *dev)
{
	static const u16 addr[] = {
		B43_PHY_OFDM(0xC1),
		B43_PHY_OFDM(0xC2),
		B43_PHY_OFDM(0xC3),
		B43_PHY_OFDM(0xC4),
		B43_PHY_OFDM(0xC5),
		B43_PHY_OFDM(0xC6),
		B43_PHY_OFDM(0xC7),
		B43_PHY_OFDM(0xC8),
		B43_PHY_OFDM(0xCF),
	};

	static const u16 coefs[] = {
		0xDE5E, 0xE832, 0xE331, 0x4D26,
		0x0026, 0x1420, 0x0020, 0xFE08,
		0x0008,
	};

	struct b43_phy_lp *lpphy = dev->phy.lp;
	int i;

	for (i = 0; i < ARRAY_SIZE(addr); i++) {
		lpphy->dig_flt_state[i] = b43_phy_read(dev, addr[i]);
		b43_phy_write(dev, addr[i], coefs[i]);
	}
}

static void lpphy_restore_dig_flt_state(struct b43_wldev *dev)
{
	static const u16 addr[] = {
		B43_PHY_OFDM(0xC1),
		B43_PHY_OFDM(0xC2),
		B43_PHY_OFDM(0xC3),
		B43_PHY_OFDM(0xC4),
		B43_PHY_OFDM(0xC5),
		B43_PHY_OFDM(0xC6),
		B43_PHY_OFDM(0xC7),
		B43_PHY_OFDM(0xC8),
		B43_PHY_OFDM(0xCF),
	};

	struct b43_phy_lp *lpphy = dev->phy.lp;
	int i;

	for (i = 0; i < ARRAY_SIZE(addr); i++)
		b43_phy_write(dev, addr[i], lpphy->dig_flt_state[i]);
}

static void lpphy_baseband_rev2plus_init(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	b43_phy_write(dev, B43_LPPHY_AFE_DAC_CTL, 0x50);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL, 0x8800);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL_OVR, 0);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_0, 0);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_2, 0);
	b43_phy_write(dev, B43_PHY_OFDM(0xF9), 0);
	b43_phy_write(dev, B43_LPPHY_TR_LOOKUP_1, 0);
	b43_phy_set(dev, B43_LPPHY_ADC_COMPENSATION_CTL, 0x10);
	b43_phy_maskset(dev, B43_LPPHY_OFDMSYNCTHRESH0, 0xFF00, 0xB4);
	b43_phy_maskset(dev, B43_LPPHY_DCOFFSETTRANSIENT, 0xF8FF, 0x200);
	b43_phy_maskset(dev, B43_LPPHY_DCOFFSETTRANSIENT, 0xFF00, 0x7F);
	b43_phy_maskset(dev, B43_LPPHY_GAINDIRECTMISMATCH, 0xFF0F, 0x40);
	b43_phy_maskset(dev, B43_LPPHY_PREAMBLECONFIRMTO, 0xFF00, 0x2);
	b43_phy_mask(dev, B43_LPPHY_CRSGAIN_CTL, ~0x4000);
	b43_phy_mask(dev, B43_LPPHY_CRSGAIN_CTL, ~0x2000);
	b43_phy_set(dev, B43_PHY_OFDM(0x10A), 0x1);
	if (dev->dev->board_rev >= 0x18) {
		b43_lptab_write(dev, B43_LPTAB32(17, 65), 0xEC);
		b43_phy_maskset(dev, B43_PHY_OFDM(0x10A), 0xFF01, 0x14);
	} else {
		b43_phy_maskset(dev, B43_PHY_OFDM(0x10A), 0xFF01, 0x10);
	}
	b43_phy_maskset(dev, B43_PHY_OFDM(0xDF), 0xFF00, 0xF4);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xDF), 0x00FF, 0xF100);
	b43_phy_write(dev, B43_LPPHY_CLIPTHRESH, 0x48);
	b43_phy_maskset(dev, B43_LPPHY_HIGAINDB, 0xFF00, 0x46);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xE4), 0xFF00, 0x10);
	b43_phy_maskset(dev, B43_LPPHY_PWR_THRESH1, 0xFFF0, 0x9);
	b43_phy_mask(dev, B43_LPPHY_GAINDIRECTMISMATCH, ~0xF);
	b43_phy_maskset(dev, B43_LPPHY_VERYLOWGAINDB, 0x00FF, 0x5500);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0xFC1F, 0xA0);
	b43_phy_maskset(dev, B43_LPPHY_GAINDIRECTMISMATCH, 0xE0FF, 0x300);
	b43_phy_maskset(dev, B43_LPPHY_HIGAINDB, 0x00FF, 0x2A00);
	if ((dev->dev->chip_id == 0x4325) && (dev->dev->chip_rev == 0)) {
		b43_phy_maskset(dev, B43_LPPHY_LOWGAINDB, 0x00FF, 0x2100);
		b43_phy_maskset(dev, B43_LPPHY_VERYLOWGAINDB, 0xFF00, 0xA);
	} else {
		b43_phy_maskset(dev, B43_LPPHY_LOWGAINDB, 0x00FF, 0x1E00);
		b43_phy_maskset(dev, B43_LPPHY_VERYLOWGAINDB, 0xFF00, 0xD);
	}
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFE), 0xFFE0, 0x1F);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFF), 0xFFE0, 0xC);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x100), 0xFF00, 0x19);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFF), 0x03FF, 0x3C00);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFE), 0xFC1F, 0x3E0);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFF), 0xFFE0, 0xC);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x100), 0x00FF, 0x1900);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0x83FF, 0x5800);
	b43_phy_maskset(dev, B43_LPPHY_CLIPCTRTHRESH, 0xFFE0, 0x12);
	b43_phy_maskset(dev, B43_LPPHY_GAINMISMATCH, 0x0FFF, 0x9000);

	if ((dev->dev->chip_id == 0x4325) && (dev->dev->chip_rev == 0)) {
		b43_lptab_write(dev, B43_LPTAB16(0x08, 0x14), 0);
		b43_lptab_write(dev, B43_LPTAB16(0x08, 0x12), 0x40);
	}

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		b43_phy_set(dev, B43_LPPHY_CRSGAIN_CTL, 0x40);
		b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL, 0xF0FF, 0xB00);
		b43_phy_maskset(dev, B43_LPPHY_SYNCPEAKCNT, 0xFFF8, 0x6);
		b43_phy_maskset(dev, B43_LPPHY_MINPWR_LEVEL, 0x00FF, 0x9D00);
		b43_phy_maskset(dev, B43_LPPHY_MINPWR_LEVEL, 0xFF00, 0xA1);
		b43_phy_mask(dev, B43_LPPHY_IDLEAFTERPKTRXTO, 0x00FF);
	} else /* 5GHz */
		b43_phy_mask(dev, B43_LPPHY_CRSGAIN_CTL, ~0x40);

	b43_phy_maskset(dev, B43_LPPHY_CRS_ED_THRESH, 0xFF00, 0xB3);
	b43_phy_maskset(dev, B43_LPPHY_CRS_ED_THRESH, 0x00FF, 0xAD00);
	b43_phy_maskset(dev, B43_LPPHY_INPUT_PWRDB, 0xFF00, lpphy->rx_pwr_offset);
	b43_phy_set(dev, B43_LPPHY_RESET_CTL, 0x44);
	b43_phy_write(dev, B43_LPPHY_RESET_CTL, 0x80);
	b43_phy_write(dev, B43_LPPHY_AFE_RSSI_CTL_0, 0xA954);
	b43_phy_write(dev, B43_LPPHY_AFE_RSSI_CTL_1,
		      0x2000 | ((u16)lpphy->rssi_gs << 10) |
		      ((u16)lpphy->rssi_vc << 4) | lpphy->rssi_vf);

	if ((dev->dev->chip_id == 0x4325) && (dev->dev->chip_rev == 0)) {
		b43_phy_set(dev, B43_LPPHY_AFE_ADC_CTL_0, 0x1C);
		b43_phy_maskset(dev, B43_LPPHY_AFE_CTL, 0x00FF, 0x8800);
		b43_phy_maskset(dev, B43_LPPHY_AFE_ADC_CTL_1, 0xFC3C, 0x0400);
	}

	lpphy_save_dig_flt_state(dev);
}

static void lpphy_baseband_init(struct b43_wldev *dev)
{
	lpphy_table_init(dev);
	if (dev->phy.rev >= 2)
		lpphy_baseband_rev2plus_init(dev);
	else
		lpphy_baseband_rev0_1_init(dev);
}

struct b2062_freqdata {
	u16 freq;
	u8 data[6];
};

/* Initialize the 2062 radio. */
static void lpphy_2062_init(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	struct ssb_bus *bus = dev->dev->sdev->bus;
	u32 crystalfreq, tmp, ref;
	unsigned int i;
	const struct b2062_freqdata *fd = NULL;

	static const struct b2062_freqdata freqdata_tab[] = {
		{ .freq = 12000, .data[0] =  6, .data[1] =  6, .data[2] =  6,
				 .data[3] =  6, .data[4] = 10, .data[5] =  6, },
		{ .freq = 13000, .data[0] =  4, .data[1] =  4, .data[2] =  4,
				 .data[3] =  4, .data[4] = 11, .data[5] =  7, },
		{ .freq = 14400, .data[0] =  3, .data[1] =  3, .data[2] =  3,
				 .data[3] =  3, .data[4] = 12, .data[5] =  7, },
		{ .freq = 16200, .data[0] =  3, .data[1] =  3, .data[2] =  3,
				 .data[3] =  3, .data[4] = 13, .data[5] =  8, },
		{ .freq = 18000, .data[0] =  2, .data[1] =  2, .data[2] =  2,
				 .data[3] =  2, .data[4] = 14, .data[5] =  8, },
		{ .freq = 19200, .data[0] =  1, .data[1] =  1, .data[2] =  1,
				 .data[3] =  1, .data[4] = 14, .data[5] =  9, },
	};

	b2062_upload_init_table(dev);

	b43_radio_write(dev, B2062_N_TX_CTL3, 0);
	b43_radio_write(dev, B2062_N_TX_CTL4, 0);
	b43_radio_write(dev, B2062_N_TX_CTL5, 0);
	b43_radio_write(dev, B2062_N_TX_CTL6, 0);
	b43_radio_write(dev, B2062_N_PDN_CTL0, 0x40);
	b43_radio_write(dev, B2062_N_PDN_CTL0, 0);
	b43_radio_write(dev, B2062_N_CALIB_TS, 0x10);
	b43_radio_write(dev, B2062_N_CALIB_TS, 0);
	if (dev->phy.rev > 0) {
		b43_radio_write(dev, B2062_S_BG_CTL1,
			(b43_radio_read(dev, B2062_N_COMM2) >> 1) | 0x80);
	}
	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
		b43_radio_set(dev, B2062_N_TSSI_CTL0, 0x1);
	else
		b43_radio_mask(dev, B2062_N_TSSI_CTL0, ~0x1);

	/* Get the crystal freq, in Hz. */
	crystalfreq = bus->chipco.pmu.crystalfreq * 1000;

	B43_WARN_ON(!(bus->chipco.capabilities & SSB_CHIPCO_CAP_PMU));
	B43_WARN_ON(crystalfreq == 0);

	if (crystalfreq <= 30000000) {
		lpphy->pdiv = 1;
		b43_radio_mask(dev, B2062_S_RFPLL_CTL1, 0xFFFB);
	} else {
		lpphy->pdiv = 2;
		b43_radio_set(dev, B2062_S_RFPLL_CTL1, 0x4);
	}

	tmp = (((800000000 * lpphy->pdiv + crystalfreq) /
	      (2 * crystalfreq)) - 8) & 0xFF;
	b43_radio_write(dev, B2062_S_RFPLL_CTL7, tmp);

	tmp = (((100 * crystalfreq + 16000000 * lpphy->pdiv) /
	      (32000000 * lpphy->pdiv)) - 1) & 0xFF;
	b43_radio_write(dev, B2062_S_RFPLL_CTL18, tmp);

	tmp = (((2 * crystalfreq + 1000000 * lpphy->pdiv) /
	      (2000000 * lpphy->pdiv)) - 1) & 0xFF;
	b43_radio_write(dev, B2062_S_RFPLL_CTL19, tmp);

	ref = (1000 * lpphy->pdiv + 2 * crystalfreq) / (2000 * lpphy->pdiv);
	ref &= 0xFFFF;
	for (i = 0; i < ARRAY_SIZE(freqdata_tab); i++) {
		if (ref < freqdata_tab[i].freq) {
			fd = &freqdata_tab[i];
			break;
		}
	}
	if (!fd)
		fd = &freqdata_tab[ARRAY_SIZE(freqdata_tab) - 1];
	b43dbg(dev->wl, "b2062: Using crystal tab entry %u kHz.\n",
	       fd->freq); /* FIXME: Keep this printk until the code is fully debugged. */

	b43_radio_write(dev, B2062_S_RFPLL_CTL8,
			((u16)(fd->data[1]) << 4) | fd->data[0]);
	b43_radio_write(dev, B2062_S_RFPLL_CTL9,
			((u16)(fd->data[3]) << 4) | fd->data[2]);
	b43_radio_write(dev, B2062_S_RFPLL_CTL10, fd->data[4]);
	b43_radio_write(dev, B2062_S_RFPLL_CTL11, fd->data[5]);
}

/* Initialize the 2063 radio. */
static void lpphy_2063_init(struct b43_wldev *dev)
{
	b2063_upload_init_table(dev);
	b43_radio_write(dev, B2063_LOGEN_SP5, 0);
	b43_radio_set(dev, B2063_COMM8, 0x38);
	b43_radio_write(dev, B2063_REG_SP1, 0x56);
	b43_radio_mask(dev, B2063_RX_BB_CTL2, ~0x2);
	b43_radio_write(dev, B2063_PA_SP7, 0);
	b43_radio_write(dev, B2063_TX_RF_SP6, 0x20);
	b43_radio_write(dev, B2063_TX_RF_SP9, 0x40);
	if (dev->phy.rev == 2) {
		b43_radio_write(dev, B2063_PA_SP3, 0xa0);
		b43_radio_write(dev, B2063_PA_SP4, 0xa0);
		b43_radio_write(dev, B2063_PA_SP2, 0x18);
	} else {
		b43_radio_write(dev, B2063_PA_SP3, 0x20);
		b43_radio_write(dev, B2063_PA_SP2, 0x20);
	}
}

struct lpphy_stx_table_entry {
	u16 phy_offset;
	u16 phy_shift;
	u16 rf_addr;
	u16 rf_shift;
	u16 mask;
};

static const struct lpphy_stx_table_entry lpphy_stx_table[] = {
	{ .phy_offset = 2, .phy_shift = 6, .rf_addr = 0x3d, .rf_shift = 3, .mask = 0x01, },
	{ .phy_offset = 1, .phy_shift = 12, .rf_addr = 0x4c, .rf_shift = 1, .mask = 0x01, },
	{ .phy_offset = 1, .phy_shift = 8, .rf_addr = 0x50, .rf_shift = 0, .mask = 0x7f, },
	{ .phy_offset = 0, .phy_shift = 8, .rf_addr = 0x44, .rf_shift = 0, .mask = 0xff, },
	{ .phy_offset = 1, .phy_shift = 0, .rf_addr = 0x4a, .rf_shift = 0, .mask = 0xff, },
	{ .phy_offset = 0, .phy_shift = 4, .rf_addr = 0x4d, .rf_shift = 0, .mask = 0xff, },
	{ .phy_offset = 1, .phy_shift = 4, .rf_addr = 0x4e, .rf_shift = 0, .mask = 0xff, },
	{ .phy_offset = 0, .phy_shift = 12, .rf_addr = 0x4f, .rf_shift = 0, .mask = 0x0f, },
	{ .phy_offset = 1, .phy_shift = 0, .rf_addr = 0x4f, .rf_shift = 4, .mask = 0x0f, },
	{ .phy_offset = 3, .phy_shift = 0, .rf_addr = 0x49, .rf_shift = 0, .mask = 0x0f, },
	{ .phy_offset = 4, .phy_shift = 3, .rf_addr = 0x46, .rf_shift = 4, .mask = 0x07, },
	{ .phy_offset = 3, .phy_shift = 15, .rf_addr = 0x46, .rf_shift = 0, .mask = 0x01, },
	{ .phy_offset = 4, .phy_shift = 0, .rf_addr = 0x46, .rf_shift = 1, .mask = 0x07, },
	{ .phy_offset = 3, .phy_shift = 8, .rf_addr = 0x48, .rf_shift = 4, .mask = 0x07, },
	{ .phy_offset = 3, .phy_shift = 11, .rf_addr = 0x48, .rf_shift = 0, .mask = 0x0f, },
	{ .phy_offset = 3, .phy_shift = 4, .rf_addr = 0x49, .rf_shift = 4, .mask = 0x0f, },
	{ .phy_offset = 2, .phy_shift = 15, .rf_addr = 0x45, .rf_shift = 0, .mask = 0x01, },
	{ .phy_offset = 5, .phy_shift = 13, .rf_addr = 0x52, .rf_shift = 4, .mask = 0x07, },
	{ .phy_offset = 6, .phy_shift = 0, .rf_addr = 0x52, .rf_shift = 7, .mask = 0x01, },
	{ .phy_offset = 5, .phy_shift = 3, .rf_addr = 0x41, .rf_shift = 5, .mask = 0x07, },
	{ .phy_offset = 5, .phy_shift = 6, .rf_addr = 0x41, .rf_shift = 0, .mask = 0x0f, },
	{ .phy_offset = 5, .phy_shift = 10, .rf_addr = 0x42, .rf_shift = 5, .mask = 0x07, },
	{ .phy_offset = 4, .phy_shift = 15, .rf_addr = 0x42, .rf_shift = 0, .mask = 0x01, },
	{ .phy_offset = 5, .phy_shift = 0, .rf_addr = 0x42, .rf_shift = 1, .mask = 0x07, },
	{ .phy_offset = 4, .phy_shift = 11, .rf_addr = 0x43, .rf_shift = 4, .mask = 0x0f, },
	{ .phy_offset = 4, .phy_shift = 7, .rf_addr = 0x43, .rf_shift = 0, .mask = 0x0f, },
	{ .phy_offset = 4, .phy_shift = 6, .rf_addr = 0x45, .rf_shift = 1, .mask = 0x01, },
	{ .phy_offset = 2, .phy_shift = 7, .rf_addr = 0x40, .rf_shift = 4, .mask = 0x0f, },
	{ .phy_offset = 2, .phy_shift = 11, .rf_addr = 0x40, .rf_shift = 0, .mask = 0x0f, },
};

static void lpphy_sync_stx(struct b43_wldev *dev)
{
	const struct lpphy_stx_table_entry *e;
	unsigned int i;
	u16 tmp;

	for (i = 0; i < ARRAY_SIZE(lpphy_stx_table); i++) {
		e = &lpphy_stx_table[i];
		tmp = b43_radio_read(dev, e->rf_addr);
		tmp >>= e->rf_shift;
		tmp <<= e->phy_shift;
		b43_phy_maskset(dev, B43_PHY_OFDM(0xF2 + e->phy_offset),
				~(e->mask << e->phy_shift), tmp);
	}
}

static void lpphy_radio_init(struct b43_wldev *dev)
{
	/* The radio is attached through the 4wire bus. */
	b43_phy_set(dev, B43_LPPHY_FOURWIRE_CTL, 0x2);
	udelay(1);
	b43_phy_mask(dev, B43_LPPHY_FOURWIRE_CTL, 0xFFFD);
	udelay(1);

	if (dev->phy.radio_ver == 0x2062) {
		lpphy_2062_init(dev);
	} else {
		lpphy_2063_init(dev);
		lpphy_sync_stx(dev);
		b43_phy_write(dev, B43_PHY_OFDM(0xF0), 0x5F80);
		b43_phy_write(dev, B43_PHY_OFDM(0xF1), 0);
		if (dev->dev->chip_id == 0x4325) {
			// TODO SSB PMU recalibration
		}
	}
}

struct lpphy_iq_est { u32 iq_prod, i_pwr, q_pwr; };

static void lpphy_set_rc_cap(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	u8 rc_cap = (lpphy->rc_cap & 0x1F) >> 1;

	if (dev->phy.rev == 1) //FIXME check channel 14!
		rc_cap = min_t(u8, rc_cap + 5, 15);

	b43_radio_write(dev, B2062_N_RXBB_CALIB2,
			max_t(u8, lpphy->rc_cap - 4, 0x80));
	b43_radio_write(dev, B2062_N_TX_CTL_A, rc_cap | 0x80);
	b43_radio_write(dev, B2062_S_RXG_CNT16,
			((lpphy->rc_cap & 0x1F) >> 2) | 0x80);
}

static u8 lpphy_get_bb_mult(struct b43_wldev *dev)
{
	return (b43_lptab_read(dev, B43_LPTAB16(0, 87)) & 0xFF00) >> 8;
}

static void lpphy_set_bb_mult(struct b43_wldev *dev, u8 bb_mult)
{
	b43_lptab_write(dev, B43_LPTAB16(0, 87), (u16)bb_mult << 8);
}

static void lpphy_set_deaf(struct b43_wldev *dev, bool user)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	if (user)
		lpphy->crs_usr_disable = true;
	else
		lpphy->crs_sys_disable = true;
	b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL, 0xFF1F, 0x80);
}

static void lpphy_clear_deaf(struct b43_wldev *dev, bool user)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	if (user)
		lpphy->crs_usr_disable = false;
	else
		lpphy->crs_sys_disable = false;

	if (!lpphy->crs_usr_disable && !lpphy->crs_sys_disable) {
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
			b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL,
					0xFF1F, 0x60);
		else
			b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL,
					0xFF1F, 0x20);
	}
}

static void lpphy_set_trsw_over(struct b43_wldev *dev, bool tx, bool rx)
{
	u16 trsw = (tx << 1) | rx;
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFFC, trsw);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x3);
}

static void lpphy_disable_crs(struct b43_wldev *dev, bool user)
{
	lpphy_set_deaf(dev, user);
	lpphy_set_trsw_over(dev, false, true);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFFB);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x4);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFF7);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x8);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x10);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x10);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFDF);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x20);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFBF);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x40);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0x7);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0x38);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFF3F);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0x100);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFDFF);
	b43_phy_write(dev, B43_LPPHY_PS_CTL_OVERRIDE_VAL0, 0);
	b43_phy_write(dev, B43_LPPHY_PS_CTL_OVERRIDE_VAL1, 1);
	b43_phy_write(dev, B43_LPPHY_PS_CTL_OVERRIDE_VAL2, 0x20);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFBFF);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xF7FF);
	b43_phy_write(dev, B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL, 0);
	b43_phy_write(dev, B43_LPPHY_RX_GAIN_CTL_OVERRIDE_VAL, 0x45AF);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_2, 0x3FF);
}

static void lpphy_restore_crs(struct b43_wldev *dev, bool user)
{
	lpphy_clear_deaf(dev, user);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFF80);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFC00);
}

struct lpphy_tx_gains { u16 gm, pga, pad, dac; };

static void lpphy_disable_rx_gain_override(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFFE);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFEF);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFBF);
	if (dev->phy.rev >= 2) {
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFEFF);
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFBFF);
			b43_phy_mask(dev, B43_PHY_OFDM(0xE5), 0xFFF7);
		}
	} else {
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFDFF);
	}
}

static void lpphy_enable_rx_gain_override(struct b43_wldev *dev)
{
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x1);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x10);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x40);
	if (dev->phy.rev >= 2) {
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x100);
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
			b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x400);
			b43_phy_set(dev, B43_PHY_OFDM(0xE5), 0x8);
		}
	} else {
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x200);
	}
}

static void lpphy_disable_tx_gain_override(struct b43_wldev *dev)
{
	if (dev->phy.rev < 2)
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFEFF);
	else {
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFF7F);
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xBFFF);
	}
	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVR, 0xFFBF);
}

static void lpphy_enable_tx_gain_override(struct b43_wldev *dev)
{
	if (dev->phy.rev < 2)
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x100);
	else {
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x80);
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x4000);
	}
	b43_phy_set(dev, B43_LPPHY_AFE_CTL_OVR, 0x40);
}

static struct lpphy_tx_gains lpphy_get_tx_gains(struct b43_wldev *dev)
{
	struct lpphy_tx_gains gains;
	u16 tmp;

	gains.dac = (b43_phy_read(dev, B43_LPPHY_AFE_DAC_CTL) & 0x380) >> 7;
	if (dev->phy.rev < 2) {
		tmp = b43_phy_read(dev,
				   B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL) & 0x7FF;
		gains.gm = tmp & 0x0007;
		gains.pga = (tmp & 0x0078) >> 3;
		gains.pad = (tmp & 0x780) >> 7;
	} else {
		tmp = b43_phy_read(dev, B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL);
		gains.pad = b43_phy_read(dev, B43_PHY_OFDM(0xFB)) & 0xFF;
		gains.gm = tmp & 0xFF;
		gains.pga = (tmp >> 8) & 0xFF;
	}

	return gains;
}

static void lpphy_set_dac_gain(struct b43_wldev *dev, u16 dac)
{
	u16 ctl = b43_phy_read(dev, B43_LPPHY_AFE_DAC_CTL) & 0xC7F;
	ctl |= dac << 7;
	b43_phy_maskset(dev, B43_LPPHY_AFE_DAC_CTL, 0xF000, ctl);
}

static u16 lpphy_get_pa_gain(struct b43_wldev *dev)
{
	return b43_phy_read(dev, B43_PHY_OFDM(0xFB)) & 0x7F;
}

static void lpphy_set_pa_gain(struct b43_wldev *dev, u16 gain)
{
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFB), 0xE03F, gain << 6);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xFD), 0x80FF, gain << 8);
}

static void lpphy_set_tx_gains(struct b43_wldev *dev,
			       struct lpphy_tx_gains gains)
{
	u16 rf_gain, pa_gain;

	if (dev->phy.rev < 2) {
		rf_gain = (gains.pad << 7) | (gains.pga << 3) | gains.gm;
		b43_phy_maskset(dev, B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL,
				0xF800, rf_gain);
	} else {
		pa_gain = lpphy_get_pa_gain(dev);
		b43_phy_write(dev, B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL,
			      (gains.pga << 8) | gains.gm);
		/*
		 * SPEC FIXME The spec calls for (pa_gain << 8) here, but that
		 * conflicts with the spec for set_pa_gain! Vendor driver bug?
		 */
		b43_phy_maskset(dev, B43_PHY_OFDM(0xFB),
				0x8000, gains.pad | (pa_gain << 6));
		b43_phy_write(dev, B43_PHY_OFDM(0xFC),
			      (gains.pga << 8) | gains.gm);
		b43_phy_maskset(dev, B43_PHY_OFDM(0xFD),
				0x8000, gains.pad | (pa_gain << 8));
	}
	lpphy_set_dac_gain(dev, gains.dac);
	lpphy_enable_tx_gain_override(dev);
}

static void lpphy_rev0_1_set_rx_gain(struct b43_wldev *dev, u32 gain)
{
	u16 trsw = gain & 0x1;
	u16 lna = (gain & 0xFFFC) | ((gain & 0xC) >> 2);
	u16 ext_lna = (gain & 2) >> 1;

	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFFE, trsw);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL,
			0xFBFF, ext_lna << 10);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL,
			0xF7FF, ext_lna << 11);
	b43_phy_write(dev, B43_LPPHY_RX_GAIN_CTL_OVERRIDE_VAL, lna);
}

static void lpphy_rev2plus_set_rx_gain(struct b43_wldev *dev, u32 gain)
{
	u16 low_gain = gain & 0xFFFF;
	u16 high_gain = (gain >> 16) & 0xF;
	u16 ext_lna = (gain >> 21) & 0x1;
	u16 trsw = ~(gain >> 20) & 0x1;
	u16 tmp;

	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xFFFE, trsw);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL,
			0xFDFF, ext_lna << 9);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL,
			0xFBFF, ext_lna << 10);
	b43_phy_write(dev, B43_LPPHY_RX_GAIN_CTL_OVERRIDE_VAL, low_gain);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS, 0xFFF0, high_gain);
	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		tmp = (gain >> 2) & 0x3;
		b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL,
				0xE7FF, tmp<<11);
		b43_phy_maskset(dev, B43_PHY_OFDM(0xE6), 0xFFE7, tmp << 3);
	}
}

static void lpphy_set_rx_gain(struct b43_wldev *dev, u32 gain)
{
	if (dev->phy.rev < 2)
		lpphy_rev0_1_set_rx_gain(dev, gain);
	else
		lpphy_rev2plus_set_rx_gain(dev, gain);
	lpphy_enable_rx_gain_override(dev);
}

static void lpphy_set_rx_gain_by_index(struct b43_wldev *dev, u16 idx)
{
	u32 gain = b43_lptab_read(dev, B43_LPTAB16(12, idx));
	lpphy_set_rx_gain(dev, gain);
}

static void lpphy_stop_ddfs(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_LPPHY_AFE_DDFS, 0xFFFD);
	b43_phy_mask(dev, B43_LPPHY_LP_PHY_CTL, 0xFFDF);
}

static void lpphy_run_ddfs(struct b43_wldev *dev, int i_on, int q_on,
			   int incr1, int incr2, int scale_idx)
{
	lpphy_stop_ddfs(dev);
	b43_phy_mask(dev, B43_LPPHY_AFE_DDFS_POINTER_INIT, 0xFF80);
	b43_phy_mask(dev, B43_LPPHY_AFE_DDFS_POINTER_INIT, 0x80FF);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS_INCR_INIT, 0xFF80, incr1);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS_INCR_INIT, 0x80FF, incr2 << 8);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS, 0xFFF7, i_on << 3);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS, 0xFFEF, q_on << 4);
	b43_phy_maskset(dev, B43_LPPHY_AFE_DDFS, 0xFF9F, scale_idx << 5);
	b43_phy_mask(dev, B43_LPPHY_AFE_DDFS, 0xFFFB);
	b43_phy_set(dev, B43_LPPHY_AFE_DDFS, 0x2);
	b43_phy_set(dev, B43_LPPHY_LP_PHY_CTL, 0x20);
}

static bool lpphy_rx_iq_est(struct b43_wldev *dev, u16 samples, u8 time,
			   struct lpphy_iq_est *iq_est)
{
	int i;

	b43_phy_mask(dev, B43_LPPHY_CRSGAIN_CTL, 0xFFF7);
	b43_phy_write(dev, B43_LPPHY_IQ_NUM_SMPLS_ADDR, samples);
	b43_phy_maskset(dev, B43_LPPHY_IQ_ENABLE_WAIT_TIME_ADDR, 0xFF00, time);
	b43_phy_mask(dev, B43_LPPHY_IQ_ENABLE_WAIT_TIME_ADDR, 0xFEFF);
	b43_phy_set(dev, B43_LPPHY_IQ_ENABLE_WAIT_TIME_ADDR, 0x200);

	for (i = 0; i < 500; i++) {
		if (!(b43_phy_read(dev,
				B43_LPPHY_IQ_ENABLE_WAIT_TIME_ADDR) & 0x200))
			break;
		msleep(1);
	}

	if ((b43_phy_read(dev, B43_LPPHY_IQ_ENABLE_WAIT_TIME_ADDR) & 0x200)) {
		b43_phy_set(dev, B43_LPPHY_CRSGAIN_CTL, 0x8);
		return false;
	}

	iq_est->iq_prod = b43_phy_read(dev, B43_LPPHY_IQ_ACC_HI_ADDR);
	iq_est->iq_prod <<= 16;
	iq_est->iq_prod |= b43_phy_read(dev, B43_LPPHY_IQ_ACC_LO_ADDR);

	iq_est->i_pwr = b43_phy_read(dev, B43_LPPHY_IQ_I_PWR_ACC_HI_ADDR);
	iq_est->i_pwr <<= 16;
	iq_est->i_pwr |= b43_phy_read(dev, B43_LPPHY_IQ_I_PWR_ACC_LO_ADDR);

	iq_est->q_pwr = b43_phy_read(dev, B43_LPPHY_IQ_Q_PWR_ACC_HI_ADDR);
	iq_est->q_pwr <<= 16;
	iq_est->q_pwr |= b43_phy_read(dev, B43_LPPHY_IQ_Q_PWR_ACC_LO_ADDR);

	b43_phy_set(dev, B43_LPPHY_CRSGAIN_CTL, 0x8);
	return true;
}

static int lpphy_loopback(struct b43_wldev *dev)
{
	struct lpphy_iq_est iq_est;
	int i, index = -1;
	u32 tmp;

	memset(&iq_est, 0, sizeof(iq_est));

	lpphy_set_trsw_over(dev, true, true);
	b43_phy_set(dev, B43_LPPHY_AFE_CTL_OVR, 1);
	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0xFFFE);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x800);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x800);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x8);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x8);
	b43_radio_write(dev, B2062_N_TX_CTL_A, 0x80);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x80);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x80);
	for (i = 0; i < 32; i++) {
		lpphy_set_rx_gain_by_index(dev, i);
		lpphy_run_ddfs(dev, 1, 1, 5, 5, 0);
		if (!(lpphy_rx_iq_est(dev, 1000, 32, &iq_est)))
			continue;
		tmp = (iq_est.i_pwr + iq_est.q_pwr) / 1000;
		if ((tmp > 4000) && (tmp < 10000)) {
			index = i;
			break;
		}
	}
	lpphy_stop_ddfs(dev);
	return index;
}

/* Fixed-point division algorithm using only integer math. */
static u32 lpphy_qdiv_roundup(u32 dividend, u32 divisor, u8 precision)
{
	u32 quotient, remainder;

	if (divisor == 0)
		return 0;

	quotient = dividend / divisor;
	remainder = dividend % divisor;

	while (precision > 0) {
		quotient <<= 1;
		if (remainder << 1 >= divisor) {
			quotient++;
			remainder = (remainder << 1) - divisor;
		}
		precision--;
	}

	if (remainder << 1 >= divisor)
		quotient++;

	return quotient;
}

/* Read the TX power control mode from hardware. */
static void lpphy_read_tx_pctl_mode_from_hardware(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 ctl;

	ctl = b43_phy_read(dev, B43_LPPHY_TX_PWR_CTL_CMD);
	switch (ctl & B43_LPPHY_TX_PWR_CTL_CMD_MODE) {
	case B43_LPPHY_TX_PWR_CTL_CMD_MODE_OFF:
		lpphy->txpctl_mode = B43_LPPHY_TXPCTL_OFF;
		break;
	case B43_LPPHY_TX_PWR_CTL_CMD_MODE_SW:
		lpphy->txpctl_mode = B43_LPPHY_TXPCTL_SW;
		break;
	case B43_LPPHY_TX_PWR_CTL_CMD_MODE_HW:
		lpphy->txpctl_mode = B43_LPPHY_TXPCTL_HW;
		break;
	default:
		lpphy->txpctl_mode = B43_LPPHY_TXPCTL_UNKNOWN;
		B43_WARN_ON(1);
		break;
	}
}

/* Set the TX power control mode in hardware. */
static void lpphy_write_tx_pctl_mode_to_hardware(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 ctl;

	switch (lpphy->txpctl_mode) {
	case B43_LPPHY_TXPCTL_OFF:
		ctl = B43_LPPHY_TX_PWR_CTL_CMD_MODE_OFF;
		break;
	case B43_LPPHY_TXPCTL_HW:
		ctl = B43_LPPHY_TX_PWR_CTL_CMD_MODE_HW;
		break;
	case B43_LPPHY_TXPCTL_SW:
		ctl = B43_LPPHY_TX_PWR_CTL_CMD_MODE_SW;
		break;
	default:
		ctl = 0;
		B43_WARN_ON(1);
	}
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_CMD,
			~B43_LPPHY_TX_PWR_CTL_CMD_MODE & 0xFFFF, ctl);
}

static void lpphy_set_tx_power_control(struct b43_wldev *dev,
				       enum b43_lpphy_txpctl_mode mode)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	enum b43_lpphy_txpctl_mode oldmode;

	lpphy_read_tx_pctl_mode_from_hardware(dev);
	oldmode = lpphy->txpctl_mode;
	if (oldmode == mode)
		return;
	lpphy->txpctl_mode = mode;

	if (oldmode == B43_LPPHY_TXPCTL_HW) {
		//TODO Update TX Power NPT
		//TODO Clear all TX Power offsets
	} else {
		if (mode == B43_LPPHY_TXPCTL_HW) {
			//TODO Recalculate target TX power
			b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_CMD,
					0xFF80, lpphy->tssi_idx);
			b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_NNUM,
					0x8FFF, ((u16)lpphy->tssi_npt << 16));
			//TODO Set "TSSI Transmit Count" variable to total transmitted frame count
			lpphy_disable_tx_gain_override(dev);
			lpphy->tx_pwr_idx_over = -1;
		}
	}
	if (dev->phy.rev >= 2) {
		if (mode == B43_LPPHY_TXPCTL_HW)
			b43_phy_set(dev, B43_PHY_OFDM(0xD0), 0x2);
		else
			b43_phy_mask(dev, B43_PHY_OFDM(0xD0), 0xFFFD);
	}
	lpphy_write_tx_pctl_mode_to_hardware(dev);
}

static int b43_lpphy_op_switch_channel(struct b43_wldev *dev,
				       unsigned int new_channel);

static void lpphy_rev0_1_rc_calib(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	struct lpphy_iq_est iq_est;
	struct lpphy_tx_gains tx_gains;
	static const u32 ideal_pwr_table[21] = {
		0x10000, 0x10557, 0x10e2d, 0x113e0, 0x10f22, 0x0ff64,
		0x0eda2, 0x0e5d4, 0x0efd1, 0x0fbe8, 0x0b7b8, 0x04b35,
		0x01a5e, 0x00a0b, 0x00444, 0x001fd, 0x000ff, 0x00088,
		0x0004c, 0x0002c, 0x0001a,
	};
	bool old_txg_ovr;
	u8 old_bbmult;
	u16 old_rf_ovr, old_rf_ovrval, old_afe_ovr, old_afe_ovrval,
	    old_rf2_ovr, old_rf2_ovrval, old_phy_ctl;
	enum b43_lpphy_txpctl_mode old_txpctl;
	u32 normal_pwr, ideal_pwr, mean_sq_pwr, tmp = 0, mean_sq_pwr_min = 0;
	int loopback, i, j, inner_sum, err;

	memset(&iq_est, 0, sizeof(iq_est));

	err = b43_lpphy_op_switch_channel(dev, 7);
	if (err) {
		b43dbg(dev->wl,
		       "RC calib: Failed to switch to channel 7, error = %d\n",
		       err);
	}
	old_txg_ovr = !!(b43_phy_read(dev, B43_LPPHY_AFE_CTL_OVR) & 0x40);
	old_bbmult = lpphy_get_bb_mult(dev);
	if (old_txg_ovr)
		tx_gains = lpphy_get_tx_gains(dev);
	old_rf_ovr = b43_phy_read(dev, B43_LPPHY_RF_OVERRIDE_0);
	old_rf_ovrval = b43_phy_read(dev, B43_LPPHY_RF_OVERRIDE_VAL_0);
	old_afe_ovr = b43_phy_read(dev, B43_LPPHY_AFE_CTL_OVR);
	old_afe_ovrval = b43_phy_read(dev, B43_LPPHY_AFE_CTL_OVRVAL);
	old_rf2_ovr = b43_phy_read(dev, B43_LPPHY_RF_OVERRIDE_2);
	old_rf2_ovrval = b43_phy_read(dev, B43_LPPHY_RF_OVERRIDE_2_VAL);
	old_phy_ctl = b43_phy_read(dev, B43_LPPHY_LP_PHY_CTL);
	lpphy_read_tx_pctl_mode_from_hardware(dev);
	old_txpctl = lpphy->txpctl_mode;

	lpphy_set_tx_power_control(dev, B43_LPPHY_TXPCTL_OFF);
	lpphy_disable_crs(dev, true);
	loopback = lpphy_loopback(dev);
	if (loopback == -1)
		goto finish;
	lpphy_set_rx_gain_by_index(dev, loopback);
	b43_phy_maskset(dev, B43_LPPHY_LP_PHY_CTL, 0xFFBF, 0x40);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFFF8, 0x1);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFFC7, 0x8);
	b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFF3F, 0xC0);
	for (i = 128; i <= 159; i++) {
		b43_radio_write(dev, B2062_N_RXBB_CALIB2, i);
		inner_sum = 0;
		for (j = 5; j <= 25; j++) {
			lpphy_run_ddfs(dev, 1, 1, j, j, 0);
			if (!(lpphy_rx_iq_est(dev, 1000, 32, &iq_est)))
				goto finish;
			mean_sq_pwr = iq_est.i_pwr + iq_est.q_pwr;
			if (j == 5)
				tmp = mean_sq_pwr;
			ideal_pwr = ((ideal_pwr_table[j-5] >> 3) + 1) >> 1;
			normal_pwr = lpphy_qdiv_roundup(mean_sq_pwr, tmp, 12);
			mean_sq_pwr = ideal_pwr - normal_pwr;
			mean_sq_pwr *= mean_sq_pwr;
			inner_sum += mean_sq_pwr;
			if ((i == 128) || (inner_sum < mean_sq_pwr_min)) {
				lpphy->rc_cap = i;
				mean_sq_pwr_min = inner_sum;
			}
		}
	}
	lpphy_stop_ddfs(dev);

finish:
	lpphy_restore_crs(dev, true);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, old_rf_ovrval);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_0, old_rf_ovr);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL_OVRVAL, old_afe_ovrval);
	b43_phy_write(dev, B43_LPPHY_AFE_CTL_OVR, old_afe_ovr);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, old_rf2_ovrval);
	b43_phy_write(dev, B43_LPPHY_RF_OVERRIDE_2, old_rf2_ovr);
	b43_phy_write(dev, B43_LPPHY_LP_PHY_CTL, old_phy_ctl);

	lpphy_set_bb_mult(dev, old_bbmult);
	if (old_txg_ovr) {
		/*
		 * SPEC FIXME: The specs say "get_tx_gains" here, which is
		 * illogical. According to lwfinger, vendor driver v4.150.10.5
		 * has a Set here, while v4.174.64.19 has a Get - regression in
		 * the vendor driver? This should be tested this once the code
		 * is testable.
		 */
		lpphy_set_tx_gains(dev, tx_gains);
	}
	lpphy_set_tx_power_control(dev, old_txpctl);
	if (lpphy->rc_cap)
		lpphy_set_rc_cap(dev);
}

static void lpphy_rev2plus_rc_calib(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->sdev->bus;
	u32 crystal_freq = bus->chipco.pmu.crystalfreq * 1000;
	u8 tmp = b43_radio_read(dev, B2063_RX_BB_SP8) & 0xFF;
	int i;

	b43_radio_write(dev, B2063_RX_BB_SP8, 0x0);
	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7E);
	b43_radio_mask(dev, B2063_PLL_SP1, 0xF7);
	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7C);
	b43_radio_write(dev, B2063_RC_CALIB_CTL2, 0x15);
	b43_radio_write(dev, B2063_RC_CALIB_CTL3, 0x70);
	b43_radio_write(dev, B2063_RC_CALIB_CTL4, 0x52);
	b43_radio_write(dev, B2063_RC_CALIB_CTL5, 0x1);
	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7D);

	for (i = 0; i < 10000; i++) {
		if (b43_radio_read(dev, B2063_RC_CALIB_CTL6) & 0x2)
			break;
		msleep(1);
	}

	if (!(b43_radio_read(dev, B2063_RC_CALIB_CTL6) & 0x2))
		b43_radio_write(dev, B2063_RX_BB_SP8, tmp);

	tmp = b43_radio_read(dev, B2063_TX_BB_SP3) & 0xFF;

	b43_radio_write(dev, B2063_TX_BB_SP3, 0x0);
	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7E);
	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7C);
	b43_radio_write(dev, B2063_RC_CALIB_CTL2, 0x55);
	b43_radio_write(dev, B2063_RC_CALIB_CTL3, 0x76);

	if (crystal_freq == 24000000) {
		b43_radio_write(dev, B2063_RC_CALIB_CTL4, 0xFC);
		b43_radio_write(dev, B2063_RC_CALIB_CTL5, 0x0);
	} else {
		b43_radio_write(dev, B2063_RC_CALIB_CTL4, 0x13);
		b43_radio_write(dev, B2063_RC_CALIB_CTL5, 0x1);
	}

	b43_radio_write(dev, B2063_PA_SP7, 0x7D);

	for (i = 0; i < 10000; i++) {
		if (b43_radio_read(dev, B2063_RC_CALIB_CTL6) & 0x2)
			break;
		msleep(1);
	}

	if (!(b43_radio_read(dev, B2063_RC_CALIB_CTL6) & 0x2))
		b43_radio_write(dev, B2063_TX_BB_SP3, tmp);

	b43_radio_write(dev, B2063_RC_CALIB_CTL1, 0x7E);
}

static void lpphy_calibrate_rc(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;

	if (dev->phy.rev >= 2) {
		lpphy_rev2plus_rc_calib(dev);
	} else if (!lpphy->rc_cap) {
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
			lpphy_rev0_1_rc_calib(dev);
	} else {
		lpphy_set_rc_cap(dev);
	}
}

static void b43_lpphy_op_set_rx_antenna(struct b43_wldev *dev, int antenna)
{
	if (dev->phy.rev >= 2)
		return; // rev2+ doesn't support antenna diversity

	if (B43_WARN_ON(antenna > B43_ANTENNA_AUTO1))
		return;

	b43_hf_write(dev, b43_hf_read(dev) & ~B43_HF_ANTDIVHELP);

	b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL, 0xFFFD, antenna & 0x2);
	b43_phy_maskset(dev, B43_LPPHY_CRSGAIN_CTL, 0xFFFE, antenna & 0x1);

	b43_hf_write(dev, b43_hf_read(dev) | B43_HF_ANTDIVHELP);

	dev->phy.lp->antenna = antenna;
}

static void lpphy_set_tx_iqcc(struct b43_wldev *dev, u16 a, u16 b)
{
	u16 tmp[2];

	tmp[0] = a;
	tmp[1] = b;
	b43_lptab_write_bulk(dev, B43_LPTAB16(0, 80), 2, tmp);
}

static void lpphy_set_tx_power_by_index(struct b43_wldev *dev, u8 index)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	struct lpphy_tx_gains gains;
	u32 iq_comp, tx_gain, coeff, rf_power;

	lpphy->tx_pwr_idx_over = index;
	lpphy_read_tx_pctl_mode_from_hardware(dev);
	if (lpphy->txpctl_mode != B43_LPPHY_TXPCTL_OFF)
		lpphy_set_tx_power_control(dev, B43_LPPHY_TXPCTL_SW);
	if (dev->phy.rev >= 2) {
		iq_comp = b43_lptab_read(dev, B43_LPTAB32(7, index + 320));
		tx_gain = b43_lptab_read(dev, B43_LPTAB32(7, index + 192));
		gains.pad = (tx_gain >> 16) & 0xFF;
		gains.gm = tx_gain & 0xFF;
		gains.pga = (tx_gain >> 8) & 0xFF;
		gains.dac = (iq_comp >> 28) & 0xFF;
		lpphy_set_tx_gains(dev, gains);
	} else {
		iq_comp = b43_lptab_read(dev, B43_LPTAB32(10, index + 320));
		tx_gain = b43_lptab_read(dev, B43_LPTAB32(10, index + 192));
		b43_phy_maskset(dev, B43_LPPHY_TX_GAIN_CTL_OVERRIDE_VAL,
				0xF800, (tx_gain >> 4) & 0x7FFF);
		lpphy_set_dac_gain(dev, tx_gain & 0x7);
		lpphy_set_pa_gain(dev, (tx_gain >> 24) & 0x7F);
	}
	lpphy_set_bb_mult(dev, (iq_comp >> 20) & 0xFF);
	lpphy_set_tx_iqcc(dev, (iq_comp >> 10) & 0x3FF, iq_comp & 0x3FF);
	if (dev->phy.rev >= 2) {
		coeff = b43_lptab_read(dev, B43_LPTAB32(7, index + 448));
	} else {
		coeff = b43_lptab_read(dev, B43_LPTAB32(10, index + 448));
	}
	b43_lptab_write(dev, B43_LPTAB16(0, 85), coeff & 0xFFFF);
	if (dev->phy.rev >= 2) {
		rf_power = b43_lptab_read(dev, B43_LPTAB32(7, index + 576));
		b43_phy_maskset(dev, B43_LPPHY_RF_PWR_OVERRIDE, 0xFF00,
				rf_power & 0xFFFF);//SPEC FIXME mask & set != 0
	}
	lpphy_enable_tx_gain_override(dev);
}

static void lpphy_btcoex_override(struct b43_wldev *dev)
{
	b43_write16(dev, B43_MMIO_BTCOEX_CTL, 0x3);
	b43_write16(dev, B43_MMIO_BTCOEX_TXCTL, 0xFF);
}

static void b43_lpphy_op_software_rfkill(struct b43_wldev *dev,
					 bool blocked)
{
	//TODO check MAC control register
	if (blocked) {
		if (dev->phy.rev >= 2) {
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x83FF);
			b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x1F00);
			b43_phy_mask(dev, B43_LPPHY_AFE_DDFS, 0x80FF);
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xDFFF);
			b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x0808);
		} else {
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xE0FF);
			b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x1F00);
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2_VAL, 0xFCFF);
			b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_2, 0x0018);
		}
	} else {
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xE0FF);
		if (dev->phy.rev >= 2)
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xF7F7);
		else
			b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_2, 0xFFE7);
	}
}

/* This was previously called lpphy_japan_filter */
static void lpphy_set_analog_filter(struct b43_wldev *dev, int channel)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 tmp = (channel == 14); //SPEC FIXME check japanwidefilter!

	if (dev->phy.rev < 2) { //SPEC FIXME Isn't this rev0/1-specific?
		b43_phy_maskset(dev, B43_LPPHY_LP_PHY_CTL, 0xFCFF, tmp << 9);
		if ((dev->phy.rev == 1) && (lpphy->rc_cap))
			lpphy_set_rc_cap(dev);
	} else {
		b43_radio_write(dev, B2063_TX_BB_SP3, 0x3F);
	}
}

static void lpphy_set_tssi_mux(struct b43_wldev *dev, enum tssi_mux_mode mode)
{
	if (mode != TSSI_MUX_EXT) {
		b43_radio_set(dev, B2063_PA_SP1, 0x2);
		b43_phy_set(dev, B43_PHY_OFDM(0xF3), 0x1000);
		b43_radio_write(dev, B2063_PA_CTL10, 0x51);
		if (mode == TSSI_MUX_POSTPA) {
			b43_radio_mask(dev, B2063_PA_SP1, 0xFFFE);
			b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0xFFC7);
		} else {
			b43_radio_maskset(dev, B2063_PA_SP1, 0xFFFE, 0x1);
			b43_phy_maskset(dev, B43_LPPHY_AFE_CTL_OVRVAL,
					0xFFC7, 0x20);
		}
	} else {
		B43_WARN_ON(1);
	}
}

static void lpphy_tx_pctl_init_hw(struct b43_wldev *dev)
{
	u16 tmp;
	int i;

	//SPEC TODO Call LP PHY Clear TX Power offsets
	for (i = 0; i < 64; i++) {
		if (dev->phy.rev >= 2)
			b43_lptab_write(dev, B43_LPTAB32(7, i + 1), i);
		else
			b43_lptab_write(dev, B43_LPTAB32(10, i + 1), i);
	}

	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_NNUM, 0xFF00, 0xFF);
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_NNUM, 0x8FFF, 0x5000);
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_IDLETSSI, 0xFFC0, 0x1F);
	if (dev->phy.rev < 2) {
		b43_phy_mask(dev, B43_LPPHY_LP_PHY_CTL, 0xEFFF);
		b43_phy_maskset(dev, B43_LPPHY_LP_PHY_CTL, 0xDFFF, 0x2000);
	} else {
		b43_phy_mask(dev, B43_PHY_OFDM(0x103), 0xFFFE);
		b43_phy_maskset(dev, B43_PHY_OFDM(0x103), 0xFFFB, 0x4);
		b43_phy_maskset(dev, B43_PHY_OFDM(0x103), 0xFFEF, 0x10);
		b43_radio_maskset(dev, B2063_IQ_CALIB_CTL2, 0xF3, 0x1);
		lpphy_set_tssi_mux(dev, TSSI_MUX_POSTPA);
	}
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_IDLETSSI, 0x7FFF, 0x8000);
	b43_phy_mask(dev, B43_LPPHY_TX_PWR_CTL_DELTAPWR_LIMIT, 0xFF);
	b43_phy_write(dev, B43_LPPHY_TX_PWR_CTL_DELTAPWR_LIMIT, 0xA);
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_CMD,
			~B43_LPPHY_TX_PWR_CTL_CMD_MODE & 0xFFFF,
			B43_LPPHY_TX_PWR_CTL_CMD_MODE_OFF);
	b43_phy_mask(dev, B43_LPPHY_TX_PWR_CTL_NNUM, 0xF8FF);
	b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_CMD,
			~B43_LPPHY_TX_PWR_CTL_CMD_MODE & 0xFFFF,
			B43_LPPHY_TX_PWR_CTL_CMD_MODE_SW);

	if (dev->phy.rev < 2) {
		b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_0, 0xEFFF, 0x1000);
		b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0xEFFF);
	} else {
		lpphy_set_tx_power_by_index(dev, 0x7F);
	}

	b43_dummy_transmission(dev, true, true);

	tmp = b43_phy_read(dev, B43_LPPHY_TX_PWR_CTL_STAT);
	if (tmp & 0x8000) {
		b43_phy_maskset(dev, B43_LPPHY_TX_PWR_CTL_IDLETSSI,
				0xFFC0, (tmp & 0xFF) - 32);
	}

	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xEFFF);

	// (SPEC?) TODO Set "Target TX frequency" variable to 0
	// SPEC FIXME "Set BB Multiplier to 0xE000" impossible - bb_mult is u8!
}

static void lpphy_tx_pctl_init_sw(struct b43_wldev *dev)
{
	struct lpphy_tx_gains gains;

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		gains.gm = 4;
		gains.pad = 12;
		gains.pga = 12;
		gains.dac = 0;
	} else {
		gains.gm = 7;
		gains.pad = 14;
		gains.pga = 15;
		gains.dac = 0;
	}
	lpphy_set_tx_gains(dev, gains);
	lpphy_set_bb_mult(dev, 150);
}

/* Initialize TX power control */
static void lpphy_tx_pctl_init(struct b43_wldev *dev)
{
	if (0/*FIXME HWPCTL capable */) {
		lpphy_tx_pctl_init_hw(dev);
	} else { /* This device is only software TX power control capable. */
		lpphy_tx_pctl_init_sw(dev);
	}
}

static void lpphy_pr41573_workaround(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u32 *saved_tab;
	const unsigned int saved_tab_size = 256;
	enum b43_lpphy_txpctl_mode txpctl_mode;
	s8 tx_pwr_idx_over;
	u16 tssi_npt, tssi_idx;

	saved_tab = kcalloc(saved_tab_size, sizeof(saved_tab[0]), GFP_KERNEL);
	if (!saved_tab) {
		b43err(dev->wl, "PR41573 failed. Out of memory!\n");
		return;
	}

	lpphy_read_tx_pctl_mode_from_hardware(dev);
	txpctl_mode = lpphy->txpctl_mode;
	tx_pwr_idx_over = lpphy->tx_pwr_idx_over;
	tssi_npt = lpphy->tssi_npt;
	tssi_idx = lpphy->tssi_idx;

	if (dev->phy.rev < 2) {
		b43_lptab_read_bulk(dev, B43_LPTAB32(10, 0x140),
				    saved_tab_size, saved_tab);
	} else {
		b43_lptab_read_bulk(dev, B43_LPTAB32(7, 0x140),
				    saved_tab_size, saved_tab);
	}
	//FIXME PHY reset
	lpphy_table_init(dev); //FIXME is table init needed?
	lpphy_baseband_init(dev);
	lpphy_tx_pctl_init(dev);
	b43_lpphy_op_software_rfkill(dev, false);
	lpphy_set_tx_power_control(dev, B43_LPPHY_TXPCTL_OFF);
	if (dev->phy.rev < 2) {
		b43_lptab_write_bulk(dev, B43_LPTAB32(10, 0x140),
				     saved_tab_size, saved_tab);
	} else {
		b43_lptab_write_bulk(dev, B43_LPTAB32(7, 0x140),
				     saved_tab_size, saved_tab);
	}
	b43_write16(dev, B43_MMIO_CHANNEL, lpphy->channel);
	lpphy->tssi_npt = tssi_npt;
	lpphy->tssi_idx = tssi_idx;
	lpphy_set_analog_filter(dev, lpphy->channel);
	if (tx_pwr_idx_over != -1)
		lpphy_set_tx_power_by_index(dev, tx_pwr_idx_over);
	if (lpphy->rc_cap)
		lpphy_set_rc_cap(dev);
	b43_lpphy_op_set_rx_antenna(dev, lpphy->antenna);
	lpphy_set_tx_power_control(dev, txpctl_mode);
	kfree(saved_tab);
}

struct lpphy_rx_iq_comp { u8 chan; s8 c1, c0; };

static const struct lpphy_rx_iq_comp lpphy_5354_iq_table[] = {
	{ .chan = 1, .c1 = -66, .c0 = 15, },
	{ .chan = 2, .c1 = -66, .c0 = 15, },
	{ .chan = 3, .c1 = -66, .c0 = 15, },
	{ .chan = 4, .c1 = -66, .c0 = 15, },
	{ .chan = 5, .c1 = -66, .c0 = 15, },
	{ .chan = 6, .c1 = -66, .c0 = 15, },
	{ .chan = 7, .c1 = -66, .c0 = 14, },
	{ .chan = 8, .c1 = -66, .c0 = 14, },
	{ .chan = 9, .c1 = -66, .c0 = 14, },
	{ .chan = 10, .c1 = -66, .c0 = 14, },
	{ .chan = 11, .c1 = -66, .c0 = 14, },
	{ .chan = 12, .c1 = -66, .c0 = 13, },
	{ .chan = 13, .c1 = -66, .c0 = 13, },
	{ .chan = 14, .c1 = -66, .c0 = 13, },
};

static const struct lpphy_rx_iq_comp lpphy_rev0_1_iq_table[] = {
	{ .chan = 1, .c1 = -64, .c0 = 13, },
	{ .chan = 2, .c1 = -64, .c0 = 13, },
	{ .chan = 3, .c1 = -64, .c0 = 13, },
	{ .chan = 4, .c1 = -64, .c0 = 13, },
	{ .chan = 5, .c1 = -64, .c0 = 12, },
	{ .chan = 6, .c1 = -64, .c0 = 12, },
	{ .chan = 7, .c1 = -64, .c0 = 12, },
	{ .chan = 8, .c1 = -64, .c0 = 12, },
	{ .chan = 9, .c1 = -64, .c0 = 12, },
	{ .chan = 10, .c1 = -64, .c0 = 11, },
	{ .chan = 11, .c1 = -64, .c0 = 11, },
	{ .chan = 12, .c1 = -64, .c0 = 11, },
	{ .chan = 13, .c1 = -64, .c0 = 11, },
	{ .chan = 14, .c1 = -64, .c0 = 10, },
	{ .chan = 34, .c1 = -62, .c0 = 24, },
	{ .chan = 38, .c1 = -62, .c0 = 24, },
	{ .chan = 42, .c1 = -62, .c0 = 24, },
	{ .chan = 46, .c1 = -62, .c0 = 23, },
	{ .chan = 36, .c1 = -62, .c0 = 24, },
	{ .chan = 40, .c1 = -62, .c0 = 24, },
	{ .chan = 44, .c1 = -62, .c0 = 23, },
	{ .chan = 48, .c1 = -62, .c0 = 23, },
	{ .chan = 52, .c1 = -62, .c0 = 23, },
	{ .chan = 56, .c1 = -62, .c0 = 22, },
	{ .chan = 60, .c1 = -62, .c0 = 22, },
	{ .chan = 64, .c1 = -62, .c0 = 22, },
	{ .chan = 100, .c1 = -62, .c0 = 16, },
	{ .chan = 104, .c1 = -62, .c0 = 16, },
	{ .chan = 108, .c1 = -62, .c0 = 15, },
	{ .chan = 112, .c1 = -62, .c0 = 14, },
	{ .chan = 116, .c1 = -62, .c0 = 14, },
	{ .chan = 120, .c1 = -62, .c0 = 13, },
	{ .chan = 124, .c1 = -62, .c0 = 12, },
	{ .chan = 128, .c1 = -62, .c0 = 12, },
	{ .chan = 132, .c1 = -62, .c0 = 12, },
	{ .chan = 136, .c1 = -62, .c0 = 11, },
	{ .chan = 140, .c1 = -62, .c0 = 10, },
	{ .chan = 149, .c1 = -61, .c0 = 9, },
	{ .chan = 153, .c1 = -61, .c0 = 9, },
	{ .chan = 157, .c1 = -61, .c0 = 9, },
	{ .chan = 161, .c1 = -61, .c0 = 8, },
	{ .chan = 165, .c1 = -61, .c0 = 8, },
	{ .chan = 184, .c1 = -62, .c0 = 25, },
	{ .chan = 188, .c1 = -62, .c0 = 25, },
	{ .chan = 192, .c1 = -62, .c0 = 25, },
	{ .chan = 196, .c1 = -62, .c0 = 25, },
	{ .chan = 200, .c1 = -62, .c0 = 25, },
	{ .chan = 204, .c1 = -62, .c0 = 25, },
	{ .chan = 208, .c1 = -62, .c0 = 25, },
	{ .chan = 212, .c1 = -62, .c0 = 25, },
	{ .chan = 216, .c1 = -62, .c0 = 26, },
};

static const struct lpphy_rx_iq_comp lpphy_rev2plus_iq_comp = {
	.chan = 0,
	.c1 = -64,
	.c0 = 0,
};

static int lpphy_calc_rx_iq_comp(struct b43_wldev *dev, u16 samples)
{
	struct lpphy_iq_est iq_est;
	u16 c0, c1;
	int prod, ipwr, qpwr, prod_msb, q_msb, tmp1, tmp2, tmp3, tmp4, ret;

	c1 = b43_phy_read(dev, B43_LPPHY_RX_COMP_COEFF_S);
	c0 = c1 >> 8;
	c1 |= 0xFF;

	b43_phy_maskset(dev, B43_LPPHY_RX_COMP_COEFF_S, 0xFF00, 0x00C0);
	b43_phy_mask(dev, B43_LPPHY_RX_COMP_COEFF_S, 0x00FF);

	ret = lpphy_rx_iq_est(dev, samples, 32, &iq_est);
	if (!ret)
		goto out;

	prod = iq_est.iq_prod;
	ipwr = iq_est.i_pwr;
	qpwr = iq_est.q_pwr;

	if (ipwr + qpwr < 2) {
		ret = 0;
		goto out;
	}

	prod_msb = fls(abs(prod));
	q_msb = fls(abs(qpwr));
	tmp1 = prod_msb - 20;

	if (tmp1 >= 0) {
		tmp3 = ((prod << (30 - prod_msb)) + (ipwr >> (1 + tmp1))) /
			(ipwr >> tmp1);
	} else {
		tmp3 = ((prod << (30 - prod_msb)) + (ipwr << (-1 - tmp1))) /
			(ipwr << -tmp1);
	}

	tmp2 = q_msb - 11;

	if (tmp2 >= 0)
		tmp4 = (qpwr << (31 - q_msb)) / (ipwr >> tmp2);
	else
		tmp4 = (qpwr << (31 - q_msb)) / (ipwr << -tmp2);

	tmp4 -= tmp3 * tmp3;
	tmp4 = -int_sqrt(tmp4);

	c0 = tmp3 >> 3;
	c1 = tmp4 >> 4;

out:
	b43_phy_maskset(dev, B43_LPPHY_RX_COMP_COEFF_S, 0xFF00, c1);
	b43_phy_maskset(dev, B43_LPPHY_RX_COMP_COEFF_S, 0x00FF, c0 << 8);
	return ret;
}

static void lpphy_run_samples(struct b43_wldev *dev, u16 samples, u16 loops,
			      u16 wait)
{
	b43_phy_maskset(dev, B43_LPPHY_SMPL_PLAY_BUFFER_CTL,
			0xFFC0, samples - 1);
	if (loops != 0xFFFF)
		loops--;
	b43_phy_maskset(dev, B43_LPPHY_SMPL_PLAY_COUNT, 0xF000, loops);
	b43_phy_maskset(dev, B43_LPPHY_SMPL_PLAY_BUFFER_CTL, 0x3F, wait << 6);
	b43_phy_set(dev, B43_LPPHY_A_PHY_CTL_ADDR, 0x1);
}

//SPEC FIXME what does a negative freq mean?
static void lpphy_start_tx_tone(struct b43_wldev *dev, s32 freq, u16 max)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	u16 buf[64];
	int i, samples = 0, theta = 0;
	int rotation = (((36 * freq) / 20) << 16) / 100;
	struct cordic_iq sample;

	lpphy->tx_tone_freq = freq;

	if (freq) {
		/* Find i for which abs(freq) integrally divides 20000 * i */
		for (i = 1; samples * abs(freq) != 20000 * i; i++) {
			samples = (20000 * i) / abs(freq);
			if(B43_WARN_ON(samples > 63))
				return;
		}
	} else {
		samples = 2;
	}

	for (i = 0; i < samples; i++) {
		sample = cordic_calc_iq(CORDIC_FIXED(theta));
		theta += rotation;
		buf[i] = CORDIC_FLOAT((sample.i * max) & 0xFF) << 8;
		buf[i] |= CORDIC_FLOAT((sample.q * max) & 0xFF);
	}

	b43_lptab_write_bulk(dev, B43_LPTAB16(5, 0), samples, buf);

	lpphy_run_samples(dev, samples, 0xFFFF, 0);
}

static void lpphy_stop_tx_tone(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	int i;

	lpphy->tx_tone_freq = 0;

	b43_phy_mask(dev, B43_LPPHY_SMPL_PLAY_COUNT, 0xF000);
	for (i = 0; i < 31; i++) {
		if (!(b43_phy_read(dev, B43_LPPHY_A_PHY_CTL_ADDR) & 0x1))
			break;
		udelay(100);
	}
}


static void lpphy_papd_cal(struct b43_wldev *dev, struct lpphy_tx_gains gains,
			   int mode, bool useindex, u8 index)
{
	//TODO
}

static void lpphy_papd_cal_txpwr(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	struct lpphy_tx_gains oldgains;
	int old_txpctl, old_afe_ovr, old_rf, old_bbmult;

	lpphy_read_tx_pctl_mode_from_hardware(dev);
	old_txpctl = lpphy->txpctl_mode;
	old_afe_ovr = b43_phy_read(dev, B43_LPPHY_AFE_CTL_OVR) & 0x40;
	if (old_afe_ovr)
		oldgains = lpphy_get_tx_gains(dev);
	old_rf = b43_phy_read(dev, B43_LPPHY_RF_PWR_OVERRIDE) & 0xFF;
	old_bbmult = lpphy_get_bb_mult(dev);

	lpphy_set_tx_power_control(dev, B43_LPPHY_TXPCTL_OFF);

	if (dev->dev->chip_id == 0x4325 && dev->dev->chip_rev == 0)
		lpphy_papd_cal(dev, oldgains, 0, 1, 30);
	else
		lpphy_papd_cal(dev, oldgains, 0, 1, 65);

	if (old_afe_ovr)
		lpphy_set_tx_gains(dev, oldgains);
	lpphy_set_bb_mult(dev, old_bbmult);
	lpphy_set_tx_power_control(dev, old_txpctl);
	b43_phy_maskset(dev, B43_LPPHY_RF_PWR_OVERRIDE, 0xFF00, old_rf);
}

static int lpphy_rx_iq_cal(struct b43_wldev *dev, bool noise, bool tx,
			    bool rx, bool pa, struct lpphy_tx_gains *gains)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	const struct lpphy_rx_iq_comp *iqcomp = NULL;
	struct lpphy_tx_gains nogains, oldgains;
	u16 tmp;
	int i, ret;

	memset(&nogains, 0, sizeof(nogains));
	memset(&oldgains, 0, sizeof(oldgains));

	if (dev->dev->chip_id == 0x5354) {
		for (i = 0; i < ARRAY_SIZE(lpphy_5354_iq_table); i++) {
			if (lpphy_5354_iq_table[i].chan == lpphy->channel) {
				iqcomp = &lpphy_5354_iq_table[i];
			}
		}
	} else if (dev->phy.rev >= 2) {
		iqcomp = &lpphy_rev2plus_iq_comp;
	} else {
		for (i = 0; i < ARRAY_SIZE(lpphy_rev0_1_iq_table); i++) {
			if (lpphy_rev0_1_iq_table[i].chan == lpphy->channel) {
				iqcomp = &lpphy_rev0_1_iq_table[i];
			}
		}
	}

	if (B43_WARN_ON(!iqcomp))
		return 0;

	b43_phy_maskset(dev, B43_LPPHY_RX_COMP_COEFF_S, 0xFF00, iqcomp->c1);
	b43_phy_maskset(dev, B43_LPPHY_RX_COMP_COEFF_S,
			0x00FF, iqcomp->c0 << 8);

	if (noise) {
		tx = true;
		rx = false;
		pa = false;
	}

	lpphy_set_trsw_over(dev, tx, rx);

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x8);
		b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_VAL_0,
				0xFFF7, pa << 3);
	} else {
		b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x20);
		b43_phy_maskset(dev, B43_LPPHY_RF_OVERRIDE_VAL_0,
				0xFFDF, pa << 5);
	}

	tmp = b43_phy_read(dev, B43_LPPHY_AFE_CTL_OVR) & 0x40;

	if (noise)
		lpphy_set_rx_gain(dev, 0x2D5D);
	else {
		if (tmp)
			oldgains = lpphy_get_tx_gains(dev);
		if (!gains)
			gains = &nogains;
		lpphy_set_tx_gains(dev, *gains);
	}

	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVR, 0xFFFE);
	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0xFFFE);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_0, 0x800);
	b43_phy_set(dev, B43_LPPHY_RF_OVERRIDE_VAL_0, 0x800);
	lpphy_set_deaf(dev, false);
	if (noise)
		ret = lpphy_calc_rx_iq_comp(dev, 0xFFF0);
	else {
		lpphy_start_tx_tone(dev, 4000, 100);
		ret = lpphy_calc_rx_iq_comp(dev, 0x4000);
		lpphy_stop_tx_tone(dev);
	}
	lpphy_clear_deaf(dev, false);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFFC);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFF7);
	b43_phy_mask(dev, B43_LPPHY_RF_OVERRIDE_0, 0xFFDF);
	if (!noise) {
		if (tmp)
			lpphy_set_tx_gains(dev, oldgains);
		else
			lpphy_disable_tx_gain_override(dev);
	}
	lpphy_disable_rx_gain_override(dev);
	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVR, 0xFFFE);
	b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0xF7FF);
	return ret;
}

static void lpphy_calibration(struct b43_wldev *dev)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	enum b43_lpphy_txpctl_mode saved_pctl_mode;
	bool full_cal = false;

	if (lpphy->full_calib_chan != lpphy->channel) {
		full_cal = true;
		lpphy->full_calib_chan = lpphy->channel;
	}

	b43_mac_suspend(dev);

	lpphy_btcoex_override(dev);
	if (dev->phy.rev >= 2)
		lpphy_save_dig_flt_state(dev);
	lpphy_read_tx_pctl_mode_from_hardware(dev);
	saved_pctl_mode = lpphy->txpctl_mode;
	lpphy_set_tx_power_control(dev, B43_LPPHY_TXPCTL_OFF);
	//TODO Perform transmit power table I/Q LO calibration
	if ((dev->phy.rev == 0) && (saved_pctl_mode != B43_LPPHY_TXPCTL_OFF))
		lpphy_pr41573_workaround(dev);
	if ((dev->phy.rev >= 2) && full_cal) {
		lpphy_papd_cal_txpwr(dev);
	}
	lpphy_set_tx_power_control(dev, saved_pctl_mode);
	if (dev->phy.rev >= 2)
		lpphy_restore_dig_flt_state(dev);
	lpphy_rx_iq_cal(dev, true, true, false, false, NULL);

	b43_mac_enable(dev);
}

static void b43_lpphy_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				 u16 set)
{
	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
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

	b43_write16f(dev, B43_MMIO_RADIO_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
}

static void b43_lpphy_op_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);

	b43_write16f(dev, B43_MMIO_RADIO_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO_DATA_LOW, value);
}

struct b206x_channel {
	u8 channel;
	u16 freq;
	u8 data[12];
};

static const struct b206x_channel b2062_chantbl[] = {
	{ .channel = 1, .freq = 2412, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 2, .freq = 2417, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 3, .freq = 2422, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 4, .freq = 2427, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 5, .freq = 2432, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 6, .freq = 2437, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 7, .freq = 2442, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 8, .freq = 2447, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 9, .freq = 2452, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 10, .freq = 2457, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 11, .freq = 2462, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 12, .freq = 2467, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 13, .freq = 2472, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 14, .freq = 2484, .data[0] = 0xFF, .data[1] = 0xFF,
	  .data[2] = 0xB5, .data[3] = 0x1B, .data[4] = 0x24, .data[5] = 0x32,
	  .data[6] = 0x32, .data[7] = 0x88, .data[8] = 0x88, },
	{ .channel = 34, .freq = 5170, .data[0] = 0x00, .data[1] = 0x22,
	  .data[2] = 0x20, .data[3] = 0x84, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 38, .freq = 5190, .data[0] = 0x00, .data[1] = 0x11,
	  .data[2] = 0x10, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 42, .freq = 5210, .data[0] = 0x00, .data[1] = 0x11,
	  .data[2] = 0x10, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 46, .freq = 5230, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 36, .freq = 5180, .data[0] = 0x00, .data[1] = 0x11,
	  .data[2] = 0x20, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 40, .freq = 5200, .data[0] = 0x00, .data[1] = 0x11,
	  .data[2] = 0x10, .data[3] = 0x84, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 44, .freq = 5220, .data[0] = 0x00, .data[1] = 0x11,
	  .data[2] = 0x00, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 48, .freq = 5240, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 52, .freq = 5260, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 56, .freq = 5280, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x83, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 60, .freq = 5300, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x63, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 64, .freq = 5320, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x62, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 100, .freq = 5500, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x30, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 104, .freq = 5520, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x20, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 108, .freq = 5540, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x20, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 112, .freq = 5560, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x20, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 116, .freq = 5580, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x10, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 120, .freq = 5600, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 124, .freq = 5620, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 128, .freq = 5640, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 132, .freq = 5660, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 136, .freq = 5680, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 140, .freq = 5700, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 149, .freq = 5745, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 153, .freq = 5765, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 157, .freq = 5785, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 161, .freq = 5805, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 165, .freq = 5825, .data[0] = 0x00, .data[1] = 0x00,
	  .data[2] = 0x00, .data[3] = 0x00, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x37, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 184, .freq = 4920, .data[0] = 0x55, .data[1] = 0x77,
	  .data[2] = 0x90, .data[3] = 0xF7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 188, .freq = 4940, .data[0] = 0x44, .data[1] = 0x77,
	  .data[2] = 0x80, .data[3] = 0xE7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 192, .freq = 4960, .data[0] = 0x44, .data[1] = 0x66,
	  .data[2] = 0x80, .data[3] = 0xE7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 196, .freq = 4980, .data[0] = 0x33, .data[1] = 0x66,
	  .data[2] = 0x70, .data[3] = 0xC7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 200, .freq = 5000, .data[0] = 0x22, .data[1] = 0x55,
	  .data[2] = 0x60, .data[3] = 0xD7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 204, .freq = 5020, .data[0] = 0x22, .data[1] = 0x55,
	  .data[2] = 0x60, .data[3] = 0xC7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 208, .freq = 5040, .data[0] = 0x22, .data[1] = 0x44,
	  .data[2] = 0x50, .data[3] = 0xC7, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0xFF, },
	{ .channel = 212, .freq = 5060, .data[0] = 0x11, .data[1] = 0x44,
	  .data[2] = 0x50, .data[3] = 0xA5, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
	{ .channel = 216, .freq = 5080, .data[0] = 0x00, .data[1] = 0x44,
	  .data[2] = 0x40, .data[3] = 0xB6, .data[4] = 0x3C, .data[5] = 0x77,
	  .data[6] = 0x35, .data[7] = 0xFF, .data[8] = 0x88, },
};

static const struct b206x_channel b2063_chantbl[] = {
	{ .channel = 1, .freq = 2412, .data[0] = 0x6F, .data[1] = 0x3C,
	  .data[2] = 0x3C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 2, .freq = 2417, .data[0] = 0x6F, .data[1] = 0x3C,
	  .data[2] = 0x3C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 3, .freq = 2422, .data[0] = 0x6F, .data[1] = 0x3C,
	  .data[2] = 0x3C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 4, .freq = 2427, .data[0] = 0x6F, .data[1] = 0x2C,
	  .data[2] = 0x2C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 5, .freq = 2432, .data[0] = 0x6F, .data[1] = 0x2C,
	  .data[2] = 0x2C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 6, .freq = 2437, .data[0] = 0x6F, .data[1] = 0x2C,
	  .data[2] = 0x2C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 7, .freq = 2442, .data[0] = 0x6F, .data[1] = 0x2C,
	  .data[2] = 0x2C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 8, .freq = 2447, .data[0] = 0x6F, .data[1] = 0x2C,
	  .data[2] = 0x2C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 9, .freq = 2452, .data[0] = 0x6F, .data[1] = 0x1C,
	  .data[2] = 0x1C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 10, .freq = 2457, .data[0] = 0x6F, .data[1] = 0x1C,
	  .data[2] = 0x1C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 11, .freq = 2462, .data[0] = 0x6E, .data[1] = 0x1C,
	  .data[2] = 0x1C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 12, .freq = 2467, .data[0] = 0x6E, .data[1] = 0x1C,
	  .data[2] = 0x1C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 13, .freq = 2472, .data[0] = 0x6E, .data[1] = 0x1C,
	  .data[2] = 0x1C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 14, .freq = 2484, .data[0] = 0x6E, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x04, .data[4] = 0x05, .data[5] = 0x05,
	  .data[6] = 0x05, .data[7] = 0x05, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x80, .data[11] = 0x70, },
	{ .channel = 34, .freq = 5170, .data[0] = 0x6A, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x02, .data[5] = 0x05,
	  .data[6] = 0x0D, .data[7] = 0x0D, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 36, .freq = 5180, .data[0] = 0x6A, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x01, .data[5] = 0x05,
	  .data[6] = 0x0D, .data[7] = 0x0C, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 38, .freq = 5190, .data[0] = 0x6A, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x01, .data[5] = 0x04,
	  .data[6] = 0x0C, .data[7] = 0x0C, .data[8] = 0x77, .data[9] = 0x80,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 40, .freq = 5200, .data[0] = 0x69, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x01, .data[5] = 0x04,
	  .data[6] = 0x0C, .data[7] = 0x0C, .data[8] = 0x77, .data[9] = 0x70,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 42, .freq = 5210, .data[0] = 0x69, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x01, .data[5] = 0x04,
	  .data[6] = 0x0B, .data[7] = 0x0C, .data[8] = 0x77, .data[9] = 0x70,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 44, .freq = 5220, .data[0] = 0x69, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x04,
	  .data[6] = 0x0B, .data[7] = 0x0B, .data[8] = 0x77, .data[9] = 0x60,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 46, .freq = 5230, .data[0] = 0x69, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x03,
	  .data[6] = 0x0A, .data[7] = 0x0B, .data[8] = 0x77, .data[9] = 0x60,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 48, .freq = 5240, .data[0] = 0x69, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x03,
	  .data[6] = 0x0A, .data[7] = 0x0A, .data[8] = 0x77, .data[9] = 0x60,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 52, .freq = 5260, .data[0] = 0x68, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x02,
	  .data[6] = 0x09, .data[7] = 0x09, .data[8] = 0x77, .data[9] = 0x60,
	  .data[10] = 0x20, .data[11] = 0x00, },
	{ .channel = 56, .freq = 5280, .data[0] = 0x68, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x01,
	  .data[6] = 0x08, .data[7] = 0x08, .data[8] = 0x77, .data[9] = 0x50,
	  .data[10] = 0x10, .data[11] = 0x00, },
	{ .channel = 60, .freq = 5300, .data[0] = 0x68, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x01,
	  .data[6] = 0x08, .data[7] = 0x08, .data[8] = 0x77, .data[9] = 0x50,
	  .data[10] = 0x10, .data[11] = 0x00, },
	{ .channel = 64, .freq = 5320, .data[0] = 0x67, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x08, .data[7] = 0x08, .data[8] = 0x77, .data[9] = 0x50,
	  .data[10] = 0x10, .data[11] = 0x00, },
	{ .channel = 100, .freq = 5500, .data[0] = 0x64, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x02, .data[7] = 0x01, .data[8] = 0x77, .data[9] = 0x20,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 104, .freq = 5520, .data[0] = 0x64, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x01, .data[7] = 0x01, .data[8] = 0x77, .data[9] = 0x20,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 108, .freq = 5540, .data[0] = 0x63, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x01, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x10,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 112, .freq = 5560, .data[0] = 0x63, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x10,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 116, .freq = 5580, .data[0] = 0x62, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x10,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 120, .freq = 5600, .data[0] = 0x62, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 124, .freq = 5620, .data[0] = 0x62, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 128, .freq = 5640, .data[0] = 0x61, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 132, .freq = 5660, .data[0] = 0x61, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 136, .freq = 5680, .data[0] = 0x61, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 140, .freq = 5700, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 149, .freq = 5745, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 153, .freq = 5765, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 157, .freq = 5785, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 161, .freq = 5805, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 165, .freq = 5825, .data[0] = 0x60, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x00, .data[5] = 0x00,
	  .data[6] = 0x00, .data[7] = 0x00, .data[8] = 0x77, .data[9] = 0x00,
	  .data[10] = 0x00, .data[11] = 0x00, },
	{ .channel = 184, .freq = 4920, .data[0] = 0x6E, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x09, .data[5] = 0x0E,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xC0,
	  .data[10] = 0x50, .data[11] = 0x00, },
	{ .channel = 188, .freq = 4940, .data[0] = 0x6E, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x09, .data[5] = 0x0D,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xB0,
	  .data[10] = 0x50, .data[11] = 0x00, },
	{ .channel = 192, .freq = 4960, .data[0] = 0x6E, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x08, .data[5] = 0x0C,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xB0,
	  .data[10] = 0x50, .data[11] = 0x00, },
	{ .channel = 196, .freq = 4980, .data[0] = 0x6D, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x08, .data[5] = 0x0C,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xA0,
	  .data[10] = 0x40, .data[11] = 0x00, },
	{ .channel = 200, .freq = 5000, .data[0] = 0x6D, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x08, .data[5] = 0x0B,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xA0,
	  .data[10] = 0x40, .data[11] = 0x00, },
	{ .channel = 204, .freq = 5020, .data[0] = 0x6D, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x08, .data[5] = 0x0A,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0xA0,
	  .data[10] = 0x40, .data[11] = 0x00, },
	{ .channel = 208, .freq = 5040, .data[0] = 0x6C, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x07, .data[5] = 0x09,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0x90,
	  .data[10] = 0x40, .data[11] = 0x00, },
	{ .channel = 212, .freq = 5060, .data[0] = 0x6C, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x06, .data[5] = 0x08,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0x90,
	  .data[10] = 0x40, .data[11] = 0x00, },
	{ .channel = 216, .freq = 5080, .data[0] = 0x6C, .data[1] = 0x0C,
	  .data[2] = 0x0C, .data[3] = 0x00, .data[4] = 0x05, .data[5] = 0x08,
	  .data[6] = 0x0F, .data[7] = 0x0F, .data[8] = 0x77, .data[9] = 0x90,
	  .data[10] = 0x40, .data[11] = 0x00, },
};

static void lpphy_b2062_reset_pll_bias(struct b43_wldev *dev)
{
	b43_radio_write(dev, B2062_S_RFPLL_CTL2, 0xFF);
	udelay(20);
	if (dev->dev->chip_id == 0x5354) {
		b43_radio_write(dev, B2062_N_COMM1, 4);
		b43_radio_write(dev, B2062_S_RFPLL_CTL2, 4);
	} else {
		b43_radio_write(dev, B2062_S_RFPLL_CTL2, 0);
	}
	udelay(5);
}

static void lpphy_b2062_vco_calib(struct b43_wldev *dev)
{
	b43_radio_write(dev, B2062_S_RFPLL_CTL21, 0x42);
	b43_radio_write(dev, B2062_S_RFPLL_CTL21, 0x62);
	udelay(200);
}

static int lpphy_b2062_tune(struct b43_wldev *dev,
			    unsigned int channel)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	struct ssb_bus *bus = dev->dev->sdev->bus;
	const struct b206x_channel *chandata = NULL;
	u32 crystal_freq = bus->chipco.pmu.crystalfreq * 1000;
	u32 tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(b2062_chantbl); i++) {
		if (b2062_chantbl[i].channel == channel) {
			chandata = &b2062_chantbl[i];
			break;
		}
	}

	if (B43_WARN_ON(!chandata))
		return -EINVAL;

	b43_radio_set(dev, B2062_S_RFPLL_CTL14, 0x04);
	b43_radio_write(dev, B2062_N_LGENA_TUNE0, chandata->data[0]);
	b43_radio_write(dev, B2062_N_LGENA_TUNE2, chandata->data[1]);
	b43_radio_write(dev, B2062_N_LGENA_TUNE3, chandata->data[2]);
	b43_radio_write(dev, B2062_N_TX_TUNE, chandata->data[3]);
	b43_radio_write(dev, B2062_S_LGENG_CTL1, chandata->data[4]);
	b43_radio_write(dev, B2062_N_LGENA_CTL5, chandata->data[5]);
	b43_radio_write(dev, B2062_N_LGENA_CTL6, chandata->data[6]);
	b43_radio_write(dev, B2062_N_TX_PGA, chandata->data[7]);
	b43_radio_write(dev, B2062_N_TX_PAD, chandata->data[8]);

	tmp1 = crystal_freq / 1000;
	tmp2 = lpphy->pdiv * 1000;
	b43_radio_write(dev, B2062_S_RFPLL_CTL33, 0xCC);
	b43_radio_write(dev, B2062_S_RFPLL_CTL34, 0x07);
	lpphy_b2062_reset_pll_bias(dev);
	tmp3 = tmp2 * channel2freq_lp(channel);
	if (channel2freq_lp(channel) < 4000)
		tmp3 *= 2;
	tmp4 = 48 * tmp1;
	tmp6 = tmp3 / tmp4;
	tmp7 = tmp3 % tmp4;
	b43_radio_write(dev, B2062_S_RFPLL_CTL26, tmp6);
	tmp5 = tmp7 * 0x100;
	tmp6 = tmp5 / tmp4;
	tmp7 = tmp5 % tmp4;
	b43_radio_write(dev, B2062_S_RFPLL_CTL27, tmp6);
	tmp5 = tmp7 * 0x100;
	tmp6 = tmp5 / tmp4;
	tmp7 = tmp5 % tmp4;
	b43_radio_write(dev, B2062_S_RFPLL_CTL28, tmp6);
	tmp5 = tmp7 * 0x100;
	tmp6 = tmp5 / tmp4;
	tmp7 = tmp5 % tmp4;
	b43_radio_write(dev, B2062_S_RFPLL_CTL29, tmp6 + ((2 * tmp7) / tmp4));
	tmp8 = b43_radio_read(dev, B2062_S_RFPLL_CTL19);
	tmp9 = ((2 * tmp3 * (tmp8 + 1)) + (3 * tmp1)) / (6 * tmp1);
	b43_radio_write(dev, B2062_S_RFPLL_CTL23, (tmp9 >> 8) + 16);
	b43_radio_write(dev, B2062_S_RFPLL_CTL24, tmp9 & 0xFF);

	lpphy_b2062_vco_calib(dev);
	if (b43_radio_read(dev, B2062_S_RFPLL_CTL3) & 0x10) {
		b43_radio_write(dev, B2062_S_RFPLL_CTL33, 0xFC);
		b43_radio_write(dev, B2062_S_RFPLL_CTL34, 0);
		lpphy_b2062_reset_pll_bias(dev);
		lpphy_b2062_vco_calib(dev);
		if (b43_radio_read(dev, B2062_S_RFPLL_CTL3) & 0x10)
			err = -EIO;
	}

	b43_radio_mask(dev, B2062_S_RFPLL_CTL14, ~0x04);
	return err;
}

static void lpphy_b2063_vco_calib(struct b43_wldev *dev)
{
	u16 tmp;

	b43_radio_mask(dev, B2063_PLL_SP1, ~0x40);
	tmp = b43_radio_read(dev, B2063_PLL_JTAG_CALNRST) & 0xF8;
	b43_radio_write(dev, B2063_PLL_JTAG_CALNRST, tmp);
	udelay(1);
	b43_radio_write(dev, B2063_PLL_JTAG_CALNRST, tmp | 0x4);
	udelay(1);
	b43_radio_write(dev, B2063_PLL_JTAG_CALNRST, tmp | 0x6);
	udelay(1);
	b43_radio_write(dev, B2063_PLL_JTAG_CALNRST, tmp | 0x7);
	udelay(300);
	b43_radio_set(dev, B2063_PLL_SP1, 0x40);
}

static int lpphy_b2063_tune(struct b43_wldev *dev,
			    unsigned int channel)
{
	struct ssb_bus *bus = dev->dev->sdev->bus;

	static const struct b206x_channel *chandata = NULL;
	u32 crystal_freq = bus->chipco.pmu.crystalfreq * 1000;
	u32 freqref, vco_freq, val1, val2, val3, timeout, timeoutref, count;
	u16 old_comm15, scale;
	u32 tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
	int i, div = (crystal_freq <= 26000000 ? 1 : 2);

	for (i = 0; i < ARRAY_SIZE(b2063_chantbl); i++) {
		if (b2063_chantbl[i].channel == channel) {
			chandata = &b2063_chantbl[i];
			break;
		}
	}

	if (B43_WARN_ON(!chandata))
		return -EINVAL;

	b43_radio_write(dev, B2063_LOGEN_VCOBUF1, chandata->data[0]);
	b43_radio_write(dev, B2063_LOGEN_MIXER2, chandata->data[1]);
	b43_radio_write(dev, B2063_LOGEN_BUF2, chandata->data[2]);
	b43_radio_write(dev, B2063_LOGEN_RCCR1, chandata->data[3]);
	b43_radio_write(dev, B2063_A_RX_1ST3, chandata->data[4]);
	b43_radio_write(dev, B2063_A_RX_2ND1, chandata->data[5]);
	b43_radio_write(dev, B2063_A_RX_2ND4, chandata->data[6]);
	b43_radio_write(dev, B2063_A_RX_2ND7, chandata->data[7]);
	b43_radio_write(dev, B2063_A_RX_PS6, chandata->data[8]);
	b43_radio_write(dev, B2063_TX_RF_CTL2, chandata->data[9]);
	b43_radio_write(dev, B2063_TX_RF_CTL5, chandata->data[10]);
	b43_radio_write(dev, B2063_PA_CTL11, chandata->data[11]);

	old_comm15 = b43_radio_read(dev, B2063_COMM15);
	b43_radio_set(dev, B2063_COMM15, 0x1E);

	if (chandata->freq > 4000) /* spec says 2484, but 4000 is safer */
		vco_freq = chandata->freq << 1;
	else
		vco_freq = chandata->freq << 2;

	freqref = crystal_freq * 3;
	val1 = lpphy_qdiv_roundup(crystal_freq, 1000000, 16);
	val2 = lpphy_qdiv_roundup(crystal_freq, 1000000 * div, 16);
	val3 = lpphy_qdiv_roundup(vco_freq, 3, 16);
	timeout = ((((8 * crystal_freq) / (div * 5000000)) + 1) >> 1) - 1;
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_VCO_CALIB3, 0x2);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_VCO_CALIB6,
			  0xFFF8, timeout >> 2);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_VCO_CALIB7,
			  0xFF9F,timeout << 5);

	timeoutref = ((((8 * crystal_freq) / (div * (timeout + 1))) +
						999999) / 1000000) + 1;
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_VCO_CALIB5, timeoutref);

	count = lpphy_qdiv_roundup(val3, val2 + 16, 16);
	count *= (timeout + 1) * (timeoutref + 1);
	count--;
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_VCO_CALIB7,
						0xF0, count >> 8);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_VCO_CALIB8, count & 0xFF);

	tmp1 = ((val3 * 62500) / freqref) << 4;
	tmp2 = ((val3 * 62500) % freqref) << 4;
	while (tmp2 >= freqref) {
		tmp1++;
		tmp2 -= freqref;
	}
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_SG1, 0xFFE0, tmp1 >> 4);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_SG2, 0xFE0F, tmp1 << 4);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_SG2, 0xFFF0, tmp1 >> 16);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_SG3, (tmp2 >> 8) & 0xFF);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_SG4, tmp2 & 0xFF);

	b43_radio_write(dev, B2063_PLL_JTAG_PLL_LF1, 0xB9);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_LF2, 0x88);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_LF3, 0x28);
	b43_radio_write(dev, B2063_PLL_JTAG_PLL_LF4, 0x63);

	tmp3 = ((41 * (val3 - 3000)) /1200) + 27;
	tmp4 = lpphy_qdiv_roundup(132000 * tmp1, 8451, 16);

	if ((tmp4 + tmp3 - 1) / tmp3 > 60) {
		scale = 1;
		tmp5 = ((tmp4 + tmp3) / (tmp3 << 1)) - 8;
	} else {
		scale = 0;
		tmp5 = ((tmp4 + (tmp3 >> 1)) / tmp3) - 8;
	}
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_CP2, 0xFFC0, tmp5);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_CP2, 0xFFBF, scale << 6);

	tmp6 = lpphy_qdiv_roundup(100 * val1, val3, 16);
	tmp6 *= (tmp5 * 8) * (scale + 1);
	if (tmp6 > 150)
		tmp6 = 0;

	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_CP3, 0xFFE0, tmp6);
	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_CP3, 0xFFDF, scale << 5);

	b43_radio_maskset(dev, B2063_PLL_JTAG_PLL_XTAL_12, 0xFFFB, 0x4);
	if (crystal_freq > 26000000)
		b43_radio_set(dev, B2063_PLL_JTAG_PLL_XTAL_12, 0x2);
	else
		b43_radio_mask(dev, B2063_PLL_JTAG_PLL_XTAL_12, 0xFD);

	if (val1 == 45)
		b43_radio_set(dev, B2063_PLL_JTAG_PLL_VCO1, 0x2);
	else
		b43_radio_mask(dev, B2063_PLL_JTAG_PLL_VCO1, 0xFD);

	b43_radio_set(dev, B2063_PLL_SP2, 0x3);
	udelay(1);
	b43_radio_mask(dev, B2063_PLL_SP2, 0xFFFC);
	lpphy_b2063_vco_calib(dev);
	b43_radio_write(dev, B2063_COMM15, old_comm15);

	return 0;
}

static int b43_lpphy_op_switch_channel(struct b43_wldev *dev,
				       unsigned int new_channel)
{
	struct b43_phy_lp *lpphy = dev->phy.lp;
	int err;

	if (dev->phy.radio_ver == 0x2063) {
		err = lpphy_b2063_tune(dev, new_channel);
		if (err)
			return err;
	} else {
		err = lpphy_b2062_tune(dev, new_channel);
		if (err)
			return err;
		lpphy_set_analog_filter(dev, new_channel);
		lpphy_adjust_gain_table(dev, channel2freq_lp(new_channel));
	}

	lpphy->channel = new_channel;
	b43_write16(dev, B43_MMIO_CHANNEL, new_channel);

	return 0;
}

static int b43_lpphy_op_init(struct b43_wldev *dev)
{
	int err;

	if (dev->dev->bus_type != B43_BUS_SSB) {
		b43err(dev->wl, "LP-PHY is supported only on SSB!\n");
		return -EOPNOTSUPP;
	}

	lpphy_read_band_sprom(dev); //FIXME should this be in prepare_structs?
	lpphy_baseband_init(dev);
	lpphy_radio_init(dev);
	lpphy_calibrate_rc(dev);
	err = b43_lpphy_op_switch_channel(dev, 7);
	if (err) {
		b43dbg(dev->wl, "Switch to channel 7 failed, error = %d.\n",
		       err);
	}
	lpphy_tx_pctl_init(dev);
	lpphy_calibration(dev);
	//TODO ACI init

	return 0;
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

static void b43_lpphy_op_switch_analog(struct b43_wldev *dev, bool on)
{
       if (on) {
               b43_phy_mask(dev, B43_LPPHY_AFE_CTL_OVR, 0xfff8);
       } else {
               b43_phy_set(dev, B43_LPPHY_AFE_CTL_OVRVAL, 0x0007);
               b43_phy_set(dev, B43_LPPHY_AFE_CTL_OVR, 0x0007);
       }
}

static void b43_lpphy_op_pwork_15sec(struct b43_wldev *dev)
{
	//TODO
}

const struct b43_phy_operations b43_phyops_lp = {
	.allocate		= b43_lpphy_op_allocate,
	.free			= b43_lpphy_op_free,
	.prepare_structs	= b43_lpphy_op_prepare_structs,
	.init			= b43_lpphy_op_init,
	.phy_maskset		= b43_lpphy_op_maskset,
	.radio_read		= b43_lpphy_op_radio_read,
	.radio_write		= b43_lpphy_op_radio_write,
	.software_rfkill	= b43_lpphy_op_software_rfkill,
	.switch_analog		= b43_lpphy_op_switch_analog,
	.switch_channel		= b43_lpphy_op_switch_channel,
	.get_default_chan	= b43_lpphy_op_get_default_chan,
	.set_rx_antenna		= b43_lpphy_op_set_rx_antenna,
	.recalc_txpower		= b43_lpphy_op_recalc_txpower,
	.adjust_txpower		= b43_lpphy_op_adjust_txpower,
	.pwork_15sec		= b43_lpphy_op_pwork_15sec,
	.pwork_60sec		= lpphy_calibration,
};
