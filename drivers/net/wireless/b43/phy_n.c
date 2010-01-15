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

#include <linux/delay.h>
#include <linux/types.h>

#include "b43.h"
#include "phy_n.h"
#include "tables_nphy.h"
#include "main.h"

struct nphy_txgains {
	u16 txgm[2];
	u16 pga[2];
	u16 pad[2];
	u16 ipa[2];
};

struct nphy_iqcal_params {
	u16 txgm;
	u16 pga;
	u16 pad;
	u16 ipa;
	u16 cal_gain;
	u16 ncorr[5];
};

struct nphy_iq_est {
	s32 iq0_prod;
	u32 i0_pwr;
	u32 q0_pwr;
	s32 iq1_prod;
	u32 i1_pwr;
	u32 q1_pwr;
};

void b43_nphy_set_rxantenna(struct b43_wldev *dev, int antenna)
{//TODO
}

static void b43_nphy_op_adjust_txpower(struct b43_wldev *dev)
{//TODO
}

static enum b43_txpwr_result b43_nphy_op_recalc_txpower(struct b43_wldev *dev,
							bool ignore_tssi)
{//TODO
	return B43_TXPWR_RES_DONE;
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

/* Tune the hardware to a new channel. */
static int nphy_channel_switch(struct b43_wldev *dev, unsigned int channel)
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
	if ((sprom->revision != 4) ||
	   !(sprom->boardflags_hi & B43_BFH_RSSIINV)) {
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
	nphy_channel_switch(dev, dev->phy.channel);
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

#define ntab_upload(dev, offset, data) do { \
		unsigned int i;						\
		for (i = 0; i < (offset##_SIZE); i++)			\
			b43_ntab_write(dev, (offset) + i, (data)[i]);	\
	} while (0)

/*
 * Upload the N-PHY tables.
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/InitTables
 */
static void b43_nphy_tables_init(struct b43_wldev *dev)
{
	if (dev->phy.rev < 3)
		b43_nphy_rev0_1_2_tables_init(dev);
	else
		b43_nphy_rev3plus_tables_init(dev);
}

static void b43_nphy_workarounds(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	unsigned int i;

	b43_phy_set(dev, B43_NPHY_IQFLIP,
		    B43_NPHY_IQFLIP_ADC1 | B43_NPHY_IQFLIP_ADC2);
	if (1 /* FIXME band is 2.4GHz */) {
		b43_phy_set(dev, B43_NPHY_CLASSCTL,
			    B43_NPHY_CLASSCTL_CCKEN);
	} else {
		b43_phy_mask(dev, B43_NPHY_CLASSCTL,
			     ~B43_NPHY_CLASSCTL_CCKEN);
	}
	b43_radio_set(dev, B2055_C1_TX_RF_SPARE, 0x8);
	b43_phy_write(dev, B43_NPHY_TXFRAMEDELAY, 8);

	/* Fixup some tables */
	b43_ntab_write(dev, B43_NTAB16(8, 0x00), 0xA);
	b43_ntab_write(dev, B43_NTAB16(8, 0x10), 0xA);
	b43_ntab_write(dev, B43_NTAB16(8, 0x02), 0xCDAA);
	b43_ntab_write(dev, B43_NTAB16(8, 0x12), 0xCDAA);
	b43_ntab_write(dev, B43_NTAB16(8, 0x08), 0);
	b43_ntab_write(dev, B43_NTAB16(8, 0x18), 0);
	b43_ntab_write(dev, B43_NTAB16(8, 0x07), 0x7AAB);
	b43_ntab_write(dev, B43_NTAB16(8, 0x17), 0x7AAB);
	b43_ntab_write(dev, B43_NTAB16(8, 0x06), 0x800);
	b43_ntab_write(dev, B43_NTAB16(8, 0x16), 0x800);

	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO1, 0x2D8);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1, 0x301);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO2, 0x2D8);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2, 0x301);

	//TODO set RF sequence

	/* Set narrowband clip threshold */
	b43_phy_write(dev, B43_NPHY_C1_NBCLIPTHRES, 66);
	b43_phy_write(dev, B43_NPHY_C2_NBCLIPTHRES, 66);

	/* Set wideband clip 2 threshold */
	b43_phy_maskset(dev, B43_NPHY_C1_CLIPWBTHRES,
			~B43_NPHY_C1_CLIPWBTHRES_CLIP2,
			21 << B43_NPHY_C1_CLIPWBTHRES_CLIP2_SHIFT);
	b43_phy_maskset(dev, B43_NPHY_C2_CLIPWBTHRES,
			~B43_NPHY_C2_CLIPWBTHRES_CLIP2,
			21 << B43_NPHY_C2_CLIPWBTHRES_CLIP2_SHIFT);

	/* Set Clip 2 detect */
	b43_phy_set(dev, B43_NPHY_C1_CGAINI,
		    B43_NPHY_C1_CGAINI_CL2DETECT);
	b43_phy_set(dev, B43_NPHY_C2_CGAINI,
		    B43_NPHY_C2_CGAINI_CL2DETECT);

	if (0 /*FIXME*/) {
		/* Set dwell lengths */
		b43_phy_write(dev, B43_NPHY_CLIP1_NBDWELL_LEN, 43);
		b43_phy_write(dev, B43_NPHY_CLIP2_NBDWELL_LEN, 43);
		b43_phy_write(dev, B43_NPHY_W1CLIP1_DWELL_LEN, 9);
		b43_phy_write(dev, B43_NPHY_W1CLIP2_DWELL_LEN, 9);

		/* Set gain backoff */
		b43_phy_maskset(dev, B43_NPHY_C1_CGAINI,
				~B43_NPHY_C1_CGAINI_GAINBKOFF,
				1 << B43_NPHY_C1_CGAINI_GAINBKOFF_SHIFT);
		b43_phy_maskset(dev, B43_NPHY_C2_CGAINI,
				~B43_NPHY_C2_CGAINI_GAINBKOFF,
				1 << B43_NPHY_C2_CGAINI_GAINBKOFF_SHIFT);

		/* Set HPVGA2 index */
		b43_phy_maskset(dev, B43_NPHY_C1_INITGAIN,
				~B43_NPHY_C1_INITGAIN_HPVGA2,
				6 << B43_NPHY_C1_INITGAIN_HPVGA2_SHIFT);
		b43_phy_maskset(dev, B43_NPHY_C2_INITGAIN,
				~B43_NPHY_C2_INITGAIN_HPVGA2,
				6 << B43_NPHY_C2_INITGAIN_HPVGA2_SHIFT);

		//FIXME verify that the specs really mean to use autoinc here.
		for (i = 0; i < 3; i++)
			b43_ntab_write(dev, B43_NTAB16(7, 0x106) + i, 0x673);
	}

	/* Set minimum gain value */
	b43_phy_maskset(dev, B43_NPHY_C1_MINMAX_GAIN,
			~B43_NPHY_C1_MINGAIN,
			23 << B43_NPHY_C1_MINGAIN_SHIFT);
	b43_phy_maskset(dev, B43_NPHY_C2_MINMAX_GAIN,
			~B43_NPHY_C2_MINGAIN,
			23 << B43_NPHY_C2_MINGAIN_SHIFT);

	if (phy->rev < 2) {
		b43_phy_mask(dev, B43_NPHY_SCRAM_SIGCTL,
			     ~B43_NPHY_SCRAM_SIGCTL_SCM);
	}

	/* Set phase track alpha and beta */
	b43_phy_write(dev, B43_NPHY_PHASETR_A0, 0x125);
	b43_phy_write(dev, B43_NPHY_PHASETR_A1, 0x1B3);
	b43_phy_write(dev, B43_NPHY_PHASETR_A2, 0x105);
	b43_phy_write(dev, B43_NPHY_PHASETR_B0, 0x16E);
	b43_phy_write(dev, B43_NPHY_PHASETR_B1, 0xCD);
	b43_phy_write(dev, B43_NPHY_PHASETR_B2, 0x20);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/PA%20override */
static void b43_nphy_pa_override(struct b43_wldev *dev, bool enable)
{
	struct b43_phy_n *nphy = dev->phy.n;
	enum ieee80211_band band;
	u16 tmp;

	if (!enable) {
		nphy->rfctrl_intc1_save = b43_phy_read(dev,
						       B43_NPHY_RFCTL_INTC1);
		nphy->rfctrl_intc2_save = b43_phy_read(dev,
						       B43_NPHY_RFCTL_INTC2);
		band = b43_current_band(dev->wl);
		if (dev->phy.rev >= 3) {
			if (band == IEEE80211_BAND_5GHZ)
				tmp = 0x600;
			else
				tmp = 0x480;
		} else {
			if (band == IEEE80211_BAND_5GHZ)
				tmp = 0x180;
			else
				tmp = 0x120;
		}
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, tmp);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, tmp);
	} else {
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC1,
				nphy->rfctrl_intc1_save);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC2,
				nphy->rfctrl_intc2_save);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxLpFbw */
static void b43_nphy_tx_lp_fbw(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;
	u16 tmp;
	enum ieee80211_band band = b43_current_band(dev->wl);
	bool ipa = (nphy->ipa2g_on && band == IEEE80211_BAND_2GHZ) ||
			(nphy->ipa5g_on && band == IEEE80211_BAND_5GHZ);

	if (dev->phy.rev >= 3) {
		if (ipa) {
			tmp = 4;
			b43_phy_write(dev, B43_NPHY_TXF_40CO_B32S2,
			      (((((tmp << 3) | tmp) << 3) | tmp) << 3) | tmp);
		}

		tmp = 1;
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S2,
			      (((((tmp << 3) | tmp) << 3) | tmp) << 3) | tmp);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/BmacPhyClkFgc */
static void b43_nphy_bmac_clock_fgc(struct b43_wldev *dev, bool force)
{
	u32 tmslow;

	if (dev->phy.type != B43_PHYTYPE_N)
		return;

	tmslow = ssb_read32(dev->dev, SSB_TMSLOW);
	if (force)
		tmslow |= SSB_TMSLOW_FGC;
	else
		tmslow &= ~SSB_TMSLOW_FGC;
	ssb_write32(dev->dev, SSB_TMSLOW, tmslow);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CCA */
static void b43_nphy_reset_cca(struct b43_wldev *dev)
{
	u16 bbcfg;

	b43_nphy_bmac_clock_fgc(dev, 1);
	bbcfg = b43_phy_read(dev, B43_NPHY_BBCFG);
	b43_phy_write(dev, B43_NPHY_BBCFG, bbcfg | B43_NPHY_BBCFG_RSTCCA);
	udelay(1);
	b43_phy_write(dev, B43_NPHY_BBCFG, bbcfg & ~B43_NPHY_BBCFG_RSTCCA);
	b43_nphy_bmac_clock_fgc(dev, 0);
	/* TODO: N PHY Force RF Seq with argument 2 */
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxIqEst */
static void b43_nphy_rx_iq_est(struct b43_wldev *dev, struct nphy_iq_est *est,
				u16 samps, u8 time, bool wait)
{
	int i;
	u16 tmp;

	b43_phy_write(dev, B43_NPHY_IQEST_SAMCNT, samps);
	b43_phy_maskset(dev, B43_NPHY_IQEST_WT, ~B43_NPHY_IQEST_WT_VAL, time);
	if (wait)
		b43_phy_set(dev, B43_NPHY_IQEST_CMD, B43_NPHY_IQEST_CMD_MODE);
	else
		b43_phy_mask(dev, B43_NPHY_IQEST_CMD, ~B43_NPHY_IQEST_CMD_MODE);

	b43_phy_set(dev, B43_NPHY_IQEST_CMD, B43_NPHY_IQEST_CMD_START);

	for (i = 1000; i; i--) {
		tmp = b43_phy_read(dev, B43_NPHY_IQEST_CMD);
		if (!(tmp & B43_NPHY_IQEST_CMD_START)) {
			est->i0_pwr = (b43_phy_read(dev, B43_NPHY_IQEST_IPACC_HI0) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_IPACC_LO0);
			est->q0_pwr = (b43_phy_read(dev, B43_NPHY_IQEST_QPACC_HI0) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_QPACC_LO0);
			est->iq0_prod = (b43_phy_read(dev, B43_NPHY_IQEST_IQACC_HI0) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_IQACC_LO0);

			est->i1_pwr = (b43_phy_read(dev, B43_NPHY_IQEST_IPACC_HI1) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_IPACC_LO1);
			est->q1_pwr = (b43_phy_read(dev, B43_NPHY_IQEST_QPACC_HI1) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_QPACC_LO1);
			est->iq1_prod = (b43_phy_read(dev, B43_NPHY_IQEST_IQACC_HI1) << 16) |
					b43_phy_read(dev, B43_NPHY_IQEST_IQACC_LO1);
			return;
		}
		udelay(10);
	}
	memset(est, 0, sizeof(*est));
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxIqCoeffs */
static void b43_nphy_rx_iq_coeffs(struct b43_wldev *dev, bool write,
					struct b43_phy_n_iq_comp *pcomp)
{
	if (write) {
		b43_phy_write(dev, B43_NPHY_C1_RXIQ_COMPA0, pcomp->a0);
		b43_phy_write(dev, B43_NPHY_C1_RXIQ_COMPB0, pcomp->b0);
		b43_phy_write(dev, B43_NPHY_C2_RXIQ_COMPA1, pcomp->a1);
		b43_phy_write(dev, B43_NPHY_C2_RXIQ_COMPB1, pcomp->b1);
	} else {
		pcomp->a0 = b43_phy_read(dev, B43_NPHY_C1_RXIQ_COMPA0);
		pcomp->b0 = b43_phy_read(dev, B43_NPHY_C1_RXIQ_COMPB0);
		pcomp->a1 = b43_phy_read(dev, B43_NPHY_C2_RXIQ_COMPA1);
		pcomp->b1 = b43_phy_read(dev, B43_NPHY_C2_RXIQ_COMPB1);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalcRxIqComp */
static void b43_nphy_calc_rx_iq_comp(struct b43_wldev *dev, u8 mask)
{
	int i;
	s32 iq;
	u32 ii;
	u32 qq;
	int iq_nbits, qq_nbits;
	int arsh, brsh;
	u16 tmp, a, b;

	struct nphy_iq_est est;
	struct b43_phy_n_iq_comp old;
	struct b43_phy_n_iq_comp new = { };
	bool error = false;

	if (mask == 0)
		return;

	b43_nphy_rx_iq_coeffs(dev, false, &old);
	b43_nphy_rx_iq_coeffs(dev, true, &new);
	b43_nphy_rx_iq_est(dev, &est, 0x4000, 32, false);
	new = old;

	for (i = 0; i < 2; i++) {
		if (i == 0 && (mask & 1)) {
			iq = est.iq0_prod;
			ii = est.i0_pwr;
			qq = est.q0_pwr;
		} else if (i == 1 && (mask & 2)) {
			iq = est.iq1_prod;
			ii = est.i1_pwr;
			qq = est.q1_pwr;
		} else {
			B43_WARN_ON(1);
			continue;
		}

		if (ii + qq < 2) {
			error = true;
			break;
		}

		iq_nbits = fls(abs(iq));
		qq_nbits = fls(qq);

		arsh = iq_nbits - 20;
		if (arsh >= 0) {
			a = -((iq << (30 - iq_nbits)) + (ii >> (1 + arsh)));
			tmp = ii >> arsh;
		} else {
			a = -((iq << (30 - iq_nbits)) + (ii << (-1 - arsh)));
			tmp = ii << -arsh;
		}
		if (tmp == 0) {
			error = true;
			break;
		}
		a /= tmp;

		brsh = qq_nbits - 11;
		if (brsh >= 0) {
			b = (qq << (31 - qq_nbits));
			tmp = ii >> brsh;
		} else {
			b = (qq << (31 - qq_nbits));
			tmp = ii << -brsh;
		}
		if (tmp == 0) {
			error = true;
			break;
		}
		b = int_sqrt(b / tmp - a * a) - (1 << 10);

		if (i == 0 && (mask & 0x1)) {
			if (dev->phy.rev >= 3) {
				new.a0 = a & 0x3FF;
				new.b0 = b & 0x3FF;
			} else {
				new.a0 = b & 0x3FF;
				new.b0 = a & 0x3FF;
			}
		} else if (i == 1 && (mask & 0x2)) {
			if (dev->phy.rev >= 3) {
				new.a1 = a & 0x3FF;
				new.b1 = b & 0x3FF;
			} else {
				new.a1 = b & 0x3FF;
				new.b1 = a & 0x3FF;
			}
		}
	}

	if (error)
		new = old;

	b43_nphy_rx_iq_coeffs(dev, true, &new);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxIqWar */
static void b43_nphy_tx_iq_workaround(struct b43_wldev *dev)
{
	u16 array[4];
	int i;

	b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x3C50);
	for (i = 0; i < 4; i++)
		array[i] = b43_phy_read(dev, B43_NPHY_TABLE_DATALO);

	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW0, array[0]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW1, array[1]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW2, array[2]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW3, array[3]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/clip-detection */
static void b43_nphy_write_clip_detection(struct b43_wldev *dev, u16 *clip_st)
{
	b43_phy_write(dev, B43_NPHY_C1_CLIP1THRES, clip_st[0]);
	b43_phy_write(dev, B43_NPHY_C2_CLIP1THRES, clip_st[1]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/clip-detection */
static void b43_nphy_read_clip_detection(struct b43_wldev *dev, u16 *clip_st)
{
	clip_st[0] = b43_phy_read(dev, B43_NPHY_C1_CLIP1THRES);
	clip_st[1] = b43_phy_read(dev, B43_NPHY_C2_CLIP1THRES);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/classifier */
static u16 b43_nphy_classifier(struct b43_wldev *dev, u16 mask, u16 val)
{
	u16 tmp;

	if (dev->dev->id.revision == 16)
		b43_mac_suspend(dev);

	tmp = b43_phy_read(dev, B43_NPHY_CLASSCTL);
	tmp &= (B43_NPHY_CLASSCTL_CCKEN | B43_NPHY_CLASSCTL_OFDMEN |
		B43_NPHY_CLASSCTL_WAITEDEN);
	tmp &= ~mask;
	tmp |= (val & mask);
	b43_phy_maskset(dev, B43_NPHY_CLASSCTL, 0xFFF8, tmp);

	if (dev->dev->id.revision == 16)
		b43_mac_enable(dev);

	return tmp;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/carriersearch */
static void b43_nphy_stay_in_carrier_search(struct b43_wldev *dev, bool enable)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	if (enable) {
		u16 clip[] = { 0xFFFF, 0xFFFF };
		if (nphy->deaf_count++ == 0) {
			nphy->classifier_state = b43_nphy_classifier(dev, 0, 0);
			b43_nphy_classifier(dev, 0x7, 0);
			b43_nphy_read_clip_detection(dev, nphy->clip_state);
			b43_nphy_write_clip_detection(dev, clip);
		}
		b43_nphy_reset_cca(dev);
	} else {
		if (--nphy->deaf_count == 0) {
			b43_nphy_classifier(dev, 0x7, nphy->classifier_state);
			b43_nphy_write_clip_detection(dev, nphy->clip_state);
		}
	}
}

enum b43_nphy_rf_sequence {
	B43_RFSEQ_RX2TX,
	B43_RFSEQ_TX2RX,
	B43_RFSEQ_RESET2RX,
	B43_RFSEQ_UPDATE_GAINH,
	B43_RFSEQ_UPDATE_GAINL,
	B43_RFSEQ_UPDATE_GAINU,
};

static void b43_nphy_force_rf_sequence(struct b43_wldev *dev,
				       enum b43_nphy_rf_sequence seq)
{
	static const u16 trigger[] = {
		[B43_RFSEQ_RX2TX]		= B43_NPHY_RFSEQTR_RX2TX,
		[B43_RFSEQ_TX2RX]		= B43_NPHY_RFSEQTR_TX2RX,
		[B43_RFSEQ_RESET2RX]		= B43_NPHY_RFSEQTR_RST2RX,
		[B43_RFSEQ_UPDATE_GAINH]	= B43_NPHY_RFSEQTR_UPGH,
		[B43_RFSEQ_UPDATE_GAINL]	= B43_NPHY_RFSEQTR_UPGL,
		[B43_RFSEQ_UPDATE_GAINU]	= B43_NPHY_RFSEQTR_UPGU,
	};
	int i;

	B43_WARN_ON(seq >= ARRAY_SIZE(trigger));

	b43_phy_set(dev, B43_NPHY_RFSEQMODE,
		    B43_NPHY_RFSEQMODE_CAOVER | B43_NPHY_RFSEQMODE_TROVER);
	b43_phy_set(dev, B43_NPHY_RFSEQTR, trigger[seq]);
	for (i = 0; i < 200; i++) {
		if (!(b43_phy_read(dev, B43_NPHY_RFSEQST) & trigger[seq]))
			goto ok;
		msleep(1);
	}
	b43err(dev->wl, "RF sequence status timeout\n");
ok:
	b43_phy_mask(dev, B43_NPHY_RFSEQMODE,
		     ~(B43_NPHY_RFSEQMODE_CAOVER | B43_NPHY_RFSEQMODE_TROVER));
}

static void b43_nphy_bphy_init(struct b43_wldev *dev)
{
	unsigned int i;
	u16 val;

	val = 0x1E1F;
	for (i = 0; i < 14; i++) {
		b43_phy_write(dev, B43_PHY_N_BMODE(0x88 + i), val);
		val -= 0x202;
	}
	val = 0x3E3F;
	for (i = 0; i < 16; i++) {
		b43_phy_write(dev, B43_PHY_N_BMODE(0x97 + i), val);
		val -= 0x202;
	}
	b43_phy_write(dev, B43_PHY_N_BMODE(0x38), 0x668);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ScaleOffsetRssi */
static void b43_nphy_scale_offset_rssi(struct b43_wldev *dev, u16 scale,
				       s8 offset, u8 core, u8 rail, u8 type)
{
	u16 tmp;
	bool core1or5 = (core == 1) || (core == 5);
	bool core2or5 = (core == 2) || (core == 5);

	offset = clamp_val(offset, -32, 31);
	tmp = ((scale & 0x3F) << 8) | (offset & 0x3F);

	if (core1or5 && (rail == 0) && (type == 2))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Z, tmp);
	if (core1or5 && (rail == 1) && (type == 2))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Z, tmp);
	if (core2or5 && (rail == 0) && (type == 2))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Z, tmp);
	if (core2or5 && (rail == 1) && (type == 2))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Z, tmp);
	if (core1or5 && (rail == 0) && (type == 0))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_X, tmp);
	if (core1or5 && (rail == 1) && (type == 0))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_X, tmp);
	if (core2or5 && (rail == 0) && (type == 0))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_X, tmp);
	if (core2or5 && (rail == 1) && (type == 0))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_X, tmp);
	if (core1or5 && (rail == 0) && (type == 1))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Y, tmp);
	if (core1or5 && (rail == 1) && (type == 1))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Y, tmp);
	if (core2or5 && (rail == 0) && (type == 1))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Y, tmp);
	if (core2or5 && (rail == 1) && (type == 1))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Y, tmp);
	if (core1or5 && (rail == 0) && (type == 6))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_TBD, tmp);
	if (core1or5 && (rail == 1) && (type == 6))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_TBD, tmp);
	if (core2or5 && (rail == 0) && (type == 6))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_TBD, tmp);
	if (core2or5 && (rail == 1) && (type == 6))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_TBD, tmp);
	if (core1or5 && (rail == 0) && (type == 3))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_PWRDET, tmp);
	if (core1or5 && (rail == 1) && (type == 3))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_PWRDET, tmp);
	if (core2or5 && (rail == 0) && (type == 3))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_PWRDET, tmp);
	if (core2or5 && (rail == 1) && (type == 3))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_PWRDET, tmp);
	if (core1or5 && (type == 4))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0I_TSSI, tmp);
	if (core2or5 && (type == 4))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1I_TSSI, tmp);
	if (core1or5 && (type == 5))
		b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_TSSI, tmp);
	if (core2or5 && (type == 5))
		b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_TSSI, tmp);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSISel */
static void b43_nphy_rssi_select(struct b43_wldev *dev, u8 code, u8 type)
{
	u16 val;

	if (dev->phy.rev >= 3) {
		/* TODO */
	} else {
		if (type < 3)
			val = 0;
		else if (type == 6)
			val = 1;
		else if (type == 3)
			val = 2;
		else
			val = 3;

		val = (val << 12) | (val << 14);
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C1, 0x0FFF, val);
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C2, 0x0FFF, val);

		if (type < 3) {
			b43_phy_maskset(dev, B43_NPHY_RFCTL_RSSIO1, 0xFFCF,
					(type + 1) << 4);
			b43_phy_maskset(dev, B43_NPHY_RFCTL_RSSIO2, 0xFFCF,
					(type + 1) << 4);
		}

		/* TODO use some definitions */
		if (code == 0) {
			b43_phy_maskset(dev, B43_NPHY_AFECTL_OVER, 0xCFFF, 0);
			if (type < 3) {
				b43_phy_maskset(dev, B43_NPHY_RFCTL_CMD,
						0xFEC7, 0);
				b43_phy_maskset(dev, B43_NPHY_RFCTL_OVER,
						0xEFDC, 0);
				b43_phy_maskset(dev, B43_NPHY_RFCTL_CMD,
						0xFFFE, 0);
				udelay(20);
				b43_phy_maskset(dev, B43_NPHY_RFCTL_OVER,
						0xFFFE, 0);
			}
		} else {
			b43_phy_maskset(dev, B43_NPHY_AFECTL_OVER, 0xCFFF,
					0x3000);
			if (type < 3) {
				b43_phy_maskset(dev, B43_NPHY_RFCTL_CMD,
						0xFEC7, 0x0180);
				b43_phy_maskset(dev, B43_NPHY_RFCTL_OVER,
						0xEFDC, (code << 1 | 0x1021));
				b43_phy_maskset(dev, B43_NPHY_RFCTL_CMD,
						0xFFFE, 0x0001);
				udelay(20);
				b43_phy_maskset(dev, B43_NPHY_RFCTL_OVER,
						0xFFFE, 0);
			}
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetRssi2055Vcm */
static void b43_nphy_set_rssi_2055_vcm(struct b43_wldev *dev, u8 type, u8 *buf)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (type == 2) {
			if (i == 0) {
				b43_radio_maskset(dev, B2055_C1_B0NB_RSSIVCM,
						  0xFC, buf[0]);
				b43_radio_maskset(dev, B2055_C1_RX_BB_RSSICTL5,
						  0xFC, buf[1]);
			} else {
				b43_radio_maskset(dev, B2055_C2_B0NB_RSSIVCM,
						  0xFC, buf[2 * i]);
				b43_radio_maskset(dev, B2055_C2_RX_BB_RSSICTL5,
						  0xFC, buf[2 * i + 1]);
			}
		} else {
			if (i == 0)
				b43_radio_maskset(dev, B2055_C1_RX_BB_RSSICTL5,
						  0xF3, buf[0] << 2);
			else
				b43_radio_maskset(dev, B2055_C2_RX_BB_RSSICTL5,
						  0xF3, buf[2 * i + 1] << 2);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/PollRssi */
static int b43_nphy_poll_rssi(struct b43_wldev *dev, u8 type, s32 *buf,
				u8 nsamp)
{
	int i;
	int out;
	u16 save_regs_phy[9];
	u16 s[2];

	if (dev->phy.rev >= 3) {
		save_regs_phy[0] = b43_phy_read(dev,
						B43_NPHY_RFCTL_LUT_TRSW_UP1);
		save_regs_phy[1] = b43_phy_read(dev,
						B43_NPHY_RFCTL_LUT_TRSW_UP2);
		save_regs_phy[2] = b43_phy_read(dev, B43_NPHY_AFECTL_C1);
		save_regs_phy[3] = b43_phy_read(dev, B43_NPHY_AFECTL_C2);
		save_regs_phy[4] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER1);
		save_regs_phy[5] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		save_regs_phy[6] = b43_phy_read(dev, B43_NPHY_TXF_40CO_B1S0);
		save_regs_phy[7] = b43_phy_read(dev, B43_NPHY_TXF_40CO_B32S1);
	}

	b43_nphy_rssi_select(dev, 5, type);

	if (dev->phy.rev < 2) {
		save_regs_phy[8] = b43_phy_read(dev, B43_NPHY_GPIO_SEL);
		b43_phy_write(dev, B43_NPHY_GPIO_SEL, 5);
	}

	for (i = 0; i < 4; i++)
		buf[i] = 0;

	for (i = 0; i < nsamp; i++) {
		if (dev->phy.rev < 2) {
			s[0] = b43_phy_read(dev, B43_NPHY_GPIO_LOOUT);
			s[1] = b43_phy_read(dev, B43_NPHY_GPIO_HIOUT);
		} else {
			s[0] = b43_phy_read(dev, B43_NPHY_RSSI1);
			s[1] = b43_phy_read(dev, B43_NPHY_RSSI2);
		}

		buf[0] += ((s8)((s[0] & 0x3F) << 2)) >> 2;
		buf[1] += ((s8)(((s[0] >> 8) & 0x3F) << 2)) >> 2;
		buf[2] += ((s8)((s[1] & 0x3F) << 2)) >> 2;
		buf[3] += ((s8)(((s[1] >> 8) & 0x3F) << 2)) >> 2;
	}
	out = (buf[0] & 0xFF) << 24 | (buf[1] & 0xFF) << 16 |
		(buf[2] & 0xFF) << 8 | (buf[3] & 0xFF);

	if (dev->phy.rev < 2)
		b43_phy_write(dev, B43_NPHY_GPIO_SEL, save_regs_phy[8]);

	if (dev->phy.rev >= 3) {
		b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1,
				save_regs_phy[0]);
		b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2,
				save_regs_phy[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_C1, save_regs_phy[2]);
		b43_phy_write(dev, B43_NPHY_AFECTL_C2, save_regs_phy[3]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, save_regs_phy[4]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, save_regs_phy[5]);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S0, save_regs_phy[6]);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B32S1, save_regs_phy[7]);
	}

	return out;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal */
static void b43_nphy_rev2_rssi_cal(struct b43_wldev *dev, u8 type)
{
	int i, j;
	u8 state[4];
	u8 code, val;
	u16 class, override;
	u8 regs_save_radio[2];
	u16 regs_save_phy[2];
	s8 offset[4];

	u16 clip_state[2];
	u16 clip_off[2] = { 0xFFFF, 0xFFFF };
	s32 results_min[4] = { };
	u8 vcm_final[4] = { };
	s32 results[4][4] = { };
	s32 miniq[4][2] = { };

	if (type == 2) {
		code = 0;
		val = 6;
	} else if (type < 2) {
		code = 25;
		val = 4;
	} else {
		B43_WARN_ON(1);
		return;
	}

	class = b43_nphy_classifier(dev, 0, 0);
	b43_nphy_classifier(dev, 7, 4);
	b43_nphy_read_clip_detection(dev, clip_state);
	b43_nphy_write_clip_detection(dev, clip_off);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
		override = 0x140;
	else
		override = 0x110;

	regs_save_phy[0] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC1);
	regs_save_radio[0] = b43_radio_read16(dev, B2055_C1_PD_RXTX);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, override);
	b43_radio_write16(dev, B2055_C1_PD_RXTX, val);

	regs_save_phy[1] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);
	regs_save_radio[1] = b43_radio_read16(dev, B2055_C2_PD_RXTX);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, override);
	b43_radio_write16(dev, B2055_C2_PD_RXTX, val);

	state[0] = b43_radio_read16(dev, B2055_C1_PD_RSSIMISC) & 0x07;
	state[1] = b43_radio_read16(dev, B2055_C2_PD_RSSIMISC) & 0x07;
	b43_radio_mask(dev, B2055_C1_PD_RSSIMISC, 0xF8);
	b43_radio_mask(dev, B2055_C2_PD_RSSIMISC, 0xF8);
	state[2] = b43_radio_read16(dev, B2055_C1_SP_RSSI) & 0x07;
	state[3] = b43_radio_read16(dev, B2055_C2_SP_RSSI) & 0x07;

	b43_nphy_rssi_select(dev, 5, type);
	b43_nphy_scale_offset_rssi(dev, 0, 0, 5, 0, type);
	b43_nphy_scale_offset_rssi(dev, 0, 0, 5, 1, type);

	for (i = 0; i < 4; i++) {
		u8 tmp[4];
		for (j = 0; j < 4; j++)
			tmp[j] = i;
		if (type != 1)
			b43_nphy_set_rssi_2055_vcm(dev, type, tmp);
		b43_nphy_poll_rssi(dev, type, results[i], 8);
		if (type < 2)
			for (j = 0; j < 2; j++)
				miniq[i][j] = min(results[i][2 * j],
						results[i][2 * j + 1]);
	}

	for (i = 0; i < 4; i++) {
		s32 mind = 40;
		u8 minvcm = 0;
		s32 minpoll = 249;
		s32 curr;
		for (j = 0; j < 4; j++) {
			if (type == 2)
				curr = abs(results[j][i]);
			else
				curr = abs(miniq[j][i / 2] - code * 8);

			if (curr < mind) {
				mind = curr;
				minvcm = j;
			}

			if (results[j][i] < minpoll)
				minpoll = results[j][i];
		}
		results_min[i] = minpoll;
		vcm_final[i] = minvcm;
	}

	if (type != 1)
		b43_nphy_set_rssi_2055_vcm(dev, type, vcm_final);

	for (i = 0; i < 4; i++) {
		offset[i] = (code * 8) - results[vcm_final[i]][i];

		if (offset[i] < 0)
			offset[i] = -((abs(offset[i]) + 4) / 8);
		else
			offset[i] = (offset[i] + 4) / 8;

		if (results_min[i] == 248)
			offset[i] = code - 32;

		if (i % 2 == 0)
			b43_nphy_scale_offset_rssi(dev, 0, offset[i], 1, 0,
							type);
		else
			b43_nphy_scale_offset_rssi(dev, 0, offset[i], 2, 1,
							type);
	}

	b43_radio_maskset(dev, B2055_C1_PD_RSSIMISC, 0xF8, state[0]);
	b43_radio_maskset(dev, B2055_C1_PD_RSSIMISC, 0xF8, state[1]);

	switch (state[2]) {
	case 1:
		b43_nphy_rssi_select(dev, 1, 2);
		break;
	case 4:
		b43_nphy_rssi_select(dev, 1, 0);
		break;
	case 2:
		b43_nphy_rssi_select(dev, 1, 1);
		break;
	default:
		b43_nphy_rssi_select(dev, 1, 1);
		break;
	}

	switch (state[3]) {
	case 1:
		b43_nphy_rssi_select(dev, 2, 2);
		break;
	case 4:
		b43_nphy_rssi_select(dev, 2, 0);
		break;
	default:
		b43_nphy_rssi_select(dev, 2, 1);
		break;
	}

	b43_nphy_rssi_select(dev, 0, type);

	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, regs_save_phy[0]);
	b43_radio_write16(dev, B2055_C1_PD_RXTX, regs_save_radio[0]);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, regs_save_phy[1]);
	b43_radio_write16(dev, B2055_C2_PD_RXTX, regs_save_radio[1]);

	b43_nphy_classifier(dev, 7, class);
	b43_nphy_write_clip_detection(dev, clip_state);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICalRev3 */
static void b43_nphy_rev3_rssi_cal(struct b43_wldev *dev)
{
	/* TODO */
}

/*
 * RSSI Calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal
 */
static void b43_nphy_rssi_cal(struct b43_wldev *dev)
{
	if (dev->phy.rev >= 3) {
		b43_nphy_rev3_rssi_cal(dev);
	} else {
		b43_nphy_rev2_rssi_cal(dev, 2);
		b43_nphy_rev2_rssi_cal(dev, 0);
		b43_nphy_rev2_rssi_cal(dev, 1);
	}
}

/*
 * Restore RSSI Calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/RestoreRssiCal
 */
static void b43_nphy_restore_rssi_cal(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u16 *rssical_radio_regs = NULL;
	u16 *rssical_phy_regs = NULL;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if (!nphy->rssical_chanspec_2G)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_2G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_2G;
	} else {
		if (!nphy->rssical_chanspec_5G)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_5G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_5G;
	}

	/* TODO use some definitions */
	b43_radio_maskset(dev, 0x602B, 0xE3, rssical_radio_regs[0]);
	b43_radio_maskset(dev, 0x702B, 0xE3, rssical_radio_regs[1]);

	b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Z, rssical_phy_regs[0]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Z, rssical_phy_regs[1]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Z, rssical_phy_regs[2]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Z, rssical_phy_regs[3]);

	b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_X, rssical_phy_regs[4]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_X, rssical_phy_regs[5]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_X, rssical_phy_regs[6]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_X, rssical_phy_regs[7]);

	b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Y, rssical_phy_regs[8]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Y, rssical_phy_regs[9]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Y, rssical_phy_regs[10]);
	b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Y, rssical_phy_regs[11]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GetIpaGainTbl */
static const u32 *b43_nphy_get_ipa_gain_table(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if (dev->phy.rev >= 6) {
			/* TODO If the chip is 47162
				return txpwrctrl_tx_gain_ipa_rev5 */
			return txpwrctrl_tx_gain_ipa_rev6;
		} else if (dev->phy.rev >= 5) {
			return txpwrctrl_tx_gain_ipa_rev5;
		} else {
			return txpwrctrl_tx_gain_ipa;
		}
	} else {
		return txpwrctrl_tx_gain_ipa_5g;
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalRadioSetup */
static void b43_nphy_tx_cal_radio_setup(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;
	u16 *save = nphy->tx_rx_cal_radio_saveregs;

	if (dev->phy.rev >= 3) {
		/* TODO */
	} else {
		save[0] = b43_radio_read16(dev, B2055_C1_TX_RF_IQCAL1);
		b43_radio_write16(dev, B2055_C1_TX_RF_IQCAL1, 0x29);

		save[1] = b43_radio_read16(dev, B2055_C1_TX_RF_IQCAL2);
		b43_radio_write16(dev, B2055_C1_TX_RF_IQCAL2, 0x54);

		save[2] = b43_radio_read16(dev, B2055_C2_TX_RF_IQCAL1);
		b43_radio_write16(dev, B2055_C2_TX_RF_IQCAL1, 0x29);

		save[3] = b43_radio_read16(dev, B2055_C2_TX_RF_IQCAL2);
		b43_radio_write16(dev, B2055_C2_TX_RF_IQCAL2, 0x54);

		save[3] = b43_radio_read16(dev, B2055_C1_PWRDET_RXTX);
		save[4] = b43_radio_read16(dev, B2055_C2_PWRDET_RXTX);

		if (!(b43_phy_read(dev, B43_NPHY_BANDCTL) &
		    B43_NPHY_BANDCTL_5GHZ)) {
			b43_radio_write16(dev, B2055_C1_PWRDET_RXTX, 0x04);
			b43_radio_write16(dev, B2055_C2_PWRDET_RXTX, 0x04);
		} else {
			b43_radio_write16(dev, B2055_C1_PWRDET_RXTX, 0x20);
			b43_radio_write16(dev, B2055_C2_PWRDET_RXTX, 0x20);
		}

		if (dev->phy.rev < 2) {
			b43_radio_set(dev, B2055_C1_TX_BB_MXGM, 0x20);
			b43_radio_set(dev, B2055_C2_TX_BB_MXGM, 0x20);
		} else {
			b43_radio_mask(dev, B2055_C1_TX_BB_MXGM, ~0x20);
			b43_radio_mask(dev, B2055_C2_TX_BB_MXGM, ~0x20);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/IqCalGainParams */
static void b43_nphy_iq_cal_gain_params(struct b43_wldev *dev, u16 core,
					struct nphy_txgains target,
					struct nphy_iqcal_params *params)
{
	int i, j, indx;
	u16 gain;

	if (dev->phy.rev >= 3) {
		params->txgm = target.txgm[core];
		params->pga = target.pga[core];
		params->pad = target.pad[core];
		params->ipa = target.ipa[core];
		params->cal_gain = (params->txgm << 12) | (params->pga << 8) |
					(params->pad << 4) | (params->ipa);
		for (j = 0; j < 5; j++)
			params->ncorr[j] = 0x79;
	} else {
		gain = (target.pad[core]) | (target.pga[core] << 4) |
			(target.txgm[core] << 8);

		indx = (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) ?
			1 : 0;
		for (i = 0; i < 9; i++)
			if (tbl_iqcal_gainparams[indx][i][0] == gain)
				break;
		i = min(i, 8);

		params->txgm = tbl_iqcal_gainparams[indx][i][1];
		params->pga = tbl_iqcal_gainparams[indx][i][2];
		params->pad = tbl_iqcal_gainparams[indx][i][3];
		params->cal_gain = (params->txgm << 7) | (params->pga << 4) |
					(params->pad << 2);
		for (j = 0; j < 4; j++)
			params->ncorr[j] = tbl_iqcal_gainparams[indx][i][4 + j];
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/UpdateTxCalLadder */
static void b43_nphy_update_tx_cal_ladder(struct b43_wldev *dev, u16 core)
{
	struct b43_phy_n *nphy = dev->phy.n;
	int i;
	u16 scale, entry;

	u16 tmp = nphy->txcal_bbmult;
	if (core == 0)
		tmp >>= 8;
	tmp &= 0xff;

	for (i = 0; i < 18; i++) {
		scale = (ladder_lo[i].percent * tmp) / 100;
		entry = ((scale & 0xFF) << 8) | ladder_lo[i].g_env;
		/* TODO: Write an N PHY Table with ID 15, length 1,
			offset i, width 16, and data entry */

		scale = (ladder_iq[i].percent * tmp) / 100;
		entry = ((scale & 0xFF) << 8) | ladder_iq[i].g_env;
		/* TODO: Write an N PHY Table with ID 15, length 1,
			offset i + 32, width 16, and data entry */
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GetTxGain */
static struct nphy_txgains b43_nphy_get_tx_gains(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u16 curr_gain[2];
	struct nphy_txgains target;
	const u32 *table = NULL;

	if (nphy->txpwrctrl == 0) {
		int i;

		if (nphy->hang_avoid)
			b43_nphy_stay_in_carrier_search(dev, true);
		/* TODO: Read an N PHY Table with ID 7, length 2,
			offset 0x110, width 16, and curr_gain */
		if (nphy->hang_avoid)
			b43_nphy_stay_in_carrier_search(dev, false);

		for (i = 0; i < 2; ++i) {
			if (dev->phy.rev >= 3) {
				target.ipa[i] = curr_gain[i] & 0x000F;
				target.pad[i] = (curr_gain[i] & 0x00F0) >> 4;
				target.pga[i] = (curr_gain[i] & 0x0F00) >> 8;
				target.txgm[i] = (curr_gain[i] & 0x7000) >> 12;
			} else {
				target.ipa[i] = curr_gain[i] & 0x0003;
				target.pad[i] = (curr_gain[i] & 0x000C) >> 2;
				target.pga[i] = (curr_gain[i] & 0x0070) >> 4;
				target.txgm[i] = (curr_gain[i] & 0x0380) >> 7;
			}
		}
	} else {
		int i;
		u16 index[2];
		index[0] = (b43_phy_read(dev, B43_NPHY_C1_TXPCTL_STAT) &
			B43_NPHY_TXPCTL_STAT_BIDX) >>
			B43_NPHY_TXPCTL_STAT_BIDX_SHIFT;
		index[1] = (b43_phy_read(dev, B43_NPHY_C2_TXPCTL_STAT) &
			B43_NPHY_TXPCTL_STAT_BIDX) >>
			B43_NPHY_TXPCTL_STAT_BIDX_SHIFT;

		for (i = 0; i < 2; ++i) {
			if (dev->phy.rev >= 3) {
				enum ieee80211_band band =
					b43_current_band(dev->wl);

				if ((nphy->ipa2g_on &&
				     band == IEEE80211_BAND_2GHZ) ||
				    (nphy->ipa5g_on &&
				     band == IEEE80211_BAND_5GHZ)) {
					table = b43_nphy_get_ipa_gain_table(dev);
				} else {
					if (band == IEEE80211_BAND_5GHZ) {
						if (dev->phy.rev == 3)
							table = b43_ntab_tx_gain_rev3_5ghz;
						else if (dev->phy.rev == 4)
							table = b43_ntab_tx_gain_rev4_5ghz;
						else
							table = b43_ntab_tx_gain_rev5plus_5ghz;
					} else {
						table = b43_ntab_tx_gain_rev3plus_2ghz;
					}
				}

				target.ipa[i] = (table[index[i]] >> 16) & 0xF;
				target.pad[i] = (table[index[i]] >> 20) & 0xF;
				target.pga[i] = (table[index[i]] >> 24) & 0xF;
				target.txgm[i] = (table[index[i]] >> 28) & 0xF;
			} else {
				table = b43_ntab_tx_gain_rev0_1_2;

				target.ipa[i] = (table[index[i]] >> 16) & 0x3;
				target.pad[i] = (table[index[i]] >> 18) & 0x3;
				target.pga[i] = (table[index[i]] >> 20) & 0x7;
				target.txgm[i] = (table[index[i]] >> 23) & 0x7;
			}
		}
	}

	return target;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RestoreCal */
static void b43_nphy_restore_cal(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u16 coef[4];
	u16 *loft = NULL;
	u16 *table = NULL;

	int i;
	u16 *txcal_radio_regs = NULL;
	struct b43_phy_n_iq_comp *rxcal_coeffs = NULL;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if (nphy->iqcal_chanspec_2G == 0)
			return;
		table = nphy->cal_cache.txcal_coeffs_2G;
		loft = &nphy->cal_cache.txcal_coeffs_2G[5];
	} else {
		if (nphy->iqcal_chanspec_5G == 0)
			return;
		table = nphy->cal_cache.txcal_coeffs_5G;
		loft = &nphy->cal_cache.txcal_coeffs_5G[5];
	}

	/* TODO: Write an N PHY table with ID 15, length 4, offset 80,
		width 16, and data from table */

	for (i = 0; i < 4; i++) {
		if (dev->phy.rev >= 3)
			table[i] = coef[i];
		else
			coef[i] = 0;
	}

	/* TODO: Write an N PHY table with ID 15, length 4, offset 88,
		width 16, and data from coef */
	/* TODO: Write an N PHY table with ID 15, length 2, offset 85,
		width 16 and data from loft */
	/* TODO: Write an N PHY table with ID 15, length 2, offset 93,
		width 16 and data from loft */

	if (dev->phy.rev < 2)
		b43_nphy_tx_iq_workaround(dev);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_2G;
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_2G;
	} else {
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_5G;
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_5G;
	}

	/* TODO use some definitions */
	if (dev->phy.rev >= 3) {
		b43_radio_write(dev, 0x2021, txcal_radio_regs[0]);
		b43_radio_write(dev, 0x2022, txcal_radio_regs[1]);
		b43_radio_write(dev, 0x3021, txcal_radio_regs[2]);
		b43_radio_write(dev, 0x3022, txcal_radio_regs[3]);
		b43_radio_write(dev, 0x2023, txcal_radio_regs[4]);
		b43_radio_write(dev, 0x2024, txcal_radio_regs[5]);
		b43_radio_write(dev, 0x3023, txcal_radio_regs[6]);
		b43_radio_write(dev, 0x3024, txcal_radio_regs[7]);
	} else {
		b43_radio_write(dev, 0x8B, txcal_radio_regs[0]);
		b43_radio_write(dev, 0xBA, txcal_radio_regs[1]);
		b43_radio_write(dev, 0x8D, txcal_radio_regs[2]);
		b43_radio_write(dev, 0xBC, txcal_radio_regs[3]);
	}
	b43_nphy_rx_iq_coeffs(dev, true, rxcal_coeffs);
}

/*
 * Init N-PHY
 * http://bcm-v4.sipsolutions.net/802.11/PHY/Init/N
 */
int b43_phy_initn(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;
	u8 tx_pwr_state;
	struct nphy_txgains target;
	u16 tmp;
	enum ieee80211_band tmp2;
	bool do_rssi_cal;

	u16 clip[2];
	bool do_cal = false;

	if ((dev->phy.rev >= 3) &&
	   (bus->sprom.boardflags_lo & B43_BFL_EXTLNA) &&
	   (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)) {
		chipco_set32(&dev->dev->bus->chipco, SSB_CHIPCO_CHIPCTL, 0x40);
	}
	nphy->deaf_count = 0;
	b43_nphy_tables_init(dev);
	nphy->crsminpwr_adjusted = false;
	nphy->noisevars_adjusted = false;

	/* Clear all overrides */
	if (dev->phy.rev >= 3) {
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S1, 0);
		b43_phy_write(dev, B43_NPHY_RFCTL_OVER, 0);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S0, 0);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B32S1, 0);
	} else {
		b43_phy_write(dev, B43_NPHY_RFCTL_OVER, 0);
	}
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, 0);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, 0);
	if (dev->phy.rev < 6) {
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC3, 0);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC4, 0);
	}
	b43_phy_mask(dev, B43_NPHY_RFSEQMODE,
		     ~(B43_NPHY_RFSEQMODE_CAOVER |
		       B43_NPHY_RFSEQMODE_TROVER));
	if (dev->phy.rev >= 3)
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, 0);
	b43_phy_write(dev, B43_NPHY_AFECTL_OVER, 0);

	if (dev->phy.rev <= 2) {
		tmp = (dev->phy.rev == 2) ? 0x3B : 0x40;
		b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3,
				~B43_NPHY_BPHY_CTL3_SCALE,
				tmp << B43_NPHY_BPHY_CTL3_SCALE_SHIFT);
	}
	b43_phy_write(dev, B43_NPHY_AFESEQ_TX2RX_PUD_20M, 0x20);
	b43_phy_write(dev, B43_NPHY_AFESEQ_TX2RX_PUD_40M, 0x20);

	if (bus->sprom.boardflags2_lo & 0x100 ||
	    (bus->boardinfo.vendor == PCI_VENDOR_ID_APPLE &&
	     bus->boardinfo.type == 0x8B))
		b43_phy_write(dev, B43_NPHY_TXREALFD, 0xA0);
	else
		b43_phy_write(dev, B43_NPHY_TXREALFD, 0xB8);
	b43_phy_write(dev, B43_NPHY_MIMO_CRSTXEXT, 0xC8);
	b43_phy_write(dev, B43_NPHY_PLOAD_CSENSE_EXTLEN, 0x50);
	b43_phy_write(dev, B43_NPHY_TXRIFS_FRDEL, 0x30);

	/* TODO MIMO-Config */
	/* TODO Update TX/RX chain */

	if (phy->rev < 2) {
		b43_phy_write(dev, B43_NPHY_DUP40_GFBL, 0xAA8);
		b43_phy_write(dev, B43_NPHY_DUP40_BL, 0x9A4);
	}

	tmp2 = b43_current_band(dev->wl);
	if ((nphy->ipa2g_on && tmp2 == IEEE80211_BAND_2GHZ) ||
	    (nphy->ipa5g_on && tmp2 == IEEE80211_BAND_5GHZ)) {
		b43_phy_set(dev, B43_NPHY_PAPD_EN0, 0x1);
		b43_phy_maskset(dev, B43_NPHY_EPS_TABLE_ADJ0, 0x007F,
				nphy->papd_epsilon_offset[0] << 7);
		b43_phy_set(dev, B43_NPHY_PAPD_EN1, 0x1);
		b43_phy_maskset(dev, B43_NPHY_EPS_TABLE_ADJ1, 0x007F,
				nphy->papd_epsilon_offset[1] << 7);
		/* TODO N PHY IPA Set TX Dig Filters */
	} else if (phy->rev >= 5) {
		/* TODO N PHY Ext PA Set TX Dig Filters */
	}

	b43_nphy_workarounds(dev);

	/* Reset CCA, in init code it differs a little from standard way */
	/* b43_nphy_bmac_clock_fgc(dev, 1); */
	tmp = b43_phy_read(dev, B43_NPHY_BBCFG);
	b43_phy_write(dev, B43_NPHY_BBCFG, tmp | B43_NPHY_BBCFG_RSTCCA);
	b43_phy_write(dev, B43_NPHY_BBCFG, tmp & ~B43_NPHY_BBCFG_RSTCCA);
	/* b43_nphy_bmac_clock_fgc(dev, 0); */

	/* TODO N PHY MAC PHY Clock Set with argument 1 */

	b43_nphy_pa_override(dev, false);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RX2TX);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
	b43_nphy_pa_override(dev, true);

	b43_nphy_classifier(dev, 0, 0);
	b43_nphy_read_clip_detection(dev, clip);
	tx_pwr_state = nphy->txpwrctrl;
	/* TODO N PHY TX power control with argument 0
		(turning off power control) */
	/* TODO Fix the TX Power Settings */
	/* TODO N PHY TX Power Control Idle TSSI */
	/* TODO N PHY TX Power Control Setup */

	if (phy->rev >= 3) {
		/* TODO */
	} else {
		/* TODO Write an N PHY table with ID 26, length 128, offset 192, width 32, and the data from Rev 2 TX Power Control Table */
		/* TODO Write an N PHY table with ID 27, length 128, offset 192, width 32, and the data from Rev 2 TX Power Control Table */
	}

	if (nphy->phyrxchain != 3)
		;/* TODO N PHY RX Core Set State with phyrxchain as argument */
	if (nphy->mphase_cal_phase_id > 0)
		;/* TODO PHY Periodic Calibration Multi-Phase Restart */

	do_rssi_cal = false;
	if (phy->rev >= 3) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
			do_rssi_cal = (nphy->rssical_chanspec_2G == 0);
		else
			do_rssi_cal = (nphy->rssical_chanspec_5G == 0);

		if (do_rssi_cal)
			b43_nphy_rssi_cal(dev);
		else
			b43_nphy_restore_rssi_cal(dev);
	} else {
		b43_nphy_rssi_cal(dev);
	}

	if (!((nphy->measure_hold & 0x6) != 0)) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
			do_cal = (nphy->iqcal_chanspec_2G == 0);
		else
			do_cal = (nphy->iqcal_chanspec_5G == 0);

		if (nphy->mute)
			do_cal = false;

		if (do_cal) {
			target = b43_nphy_get_tx_gains(dev);

			if (nphy->antsel_type == 2)
				;/*TODO NPHY Superswitch Init with argument 1*/
			if (nphy->perical != 2) {
				b43_nphy_rssi_cal(dev);
				if (phy->rev >= 3) {
					nphy->cal_orig_pwr_idx[0] =
					    nphy->txpwrindex[0].index_internal;
					nphy->cal_orig_pwr_idx[1] =
					    nphy->txpwrindex[1].index_internal;
					/* TODO N PHY Pre Calibrate TX Gain */
					target = b43_nphy_get_tx_gains(dev);
				}
			}
		}
	}

	/*
	if (!b43_nphy_cal_tx_iq_lo(dev, target, true, false)) {
		if (b43_nphy_cal_rx_iq(dev, target, 2, 0) == 0)
			Call N PHY Save Cal
		else if (nphy->mphase_cal_phase_id == 0)
			N PHY Periodic Calibration with argument 3
	} else {
		b43_nphy_restore_cal(dev);
	}
	*/

	/* b43_nphy_tx_pwr_ctrl_coef_setup(dev); */
	/* TODO N PHY TX Power Control Enable with argument tx_pwr_state */
	b43_phy_write(dev, B43_NPHY_TXMACIF_HOLDOFF, 0x0015);
	b43_phy_write(dev, B43_NPHY_TXMACDELAY, 0x0320);
	if (phy->rev >= 3 && phy->rev <= 6)
		b43_phy_write(dev, B43_NPHY_PLOAD_CSENSE_EXTLEN, 0x0014);
	b43_nphy_tx_lp_fbw(dev);
	/* TODO N PHY Spur Workaround */

	b43err(dev->wl, "IEEE 802.11n devices are not supported, yet.\n");
	return 0;
}

static int b43_nphy_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy;

	nphy = kzalloc(sizeof(*nphy), GFP_KERNEL);
	if (!nphy)
		return -ENOMEM;
	dev->phy.n = nphy;

	return 0;
}

static void b43_nphy_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	memset(nphy, 0, sizeof(*nphy));

	//TODO init struct b43_phy_n
}

static void b43_nphy_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	kfree(nphy);
	phy->n = NULL;
}

static int b43_nphy_op_init(struct b43_wldev *dev)
{
	return b43_phy_initn(dev);
}

static inline void check_phyreg(struct b43_wldev *dev, u16 offset)
{
#if B43_DEBUG
	if ((offset & B43_PHYROUTE) == B43_PHYROUTE_OFDM_GPHY) {
		/* OFDM registers are onnly available on A/G-PHYs */
		b43err(dev->wl, "Invalid OFDM PHY access at "
		       "0x%04X on N-PHY\n", offset);
		dump_stack();
	}
	if ((offset & B43_PHYROUTE) == B43_PHYROUTE_EXT_GPHY) {
		/* Ext-G registers are only available on G-PHYs */
		b43err(dev->wl, "Invalid EXT-G PHY access at "
		       "0x%04X on N-PHY\n", offset);
		dump_stack();
	}
#endif /* B43_DEBUG */
}

static u16 b43_nphy_op_read(struct b43_wldev *dev, u16 reg)
{
	check_phyreg(dev, reg);
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_nphy_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	check_phyreg(dev, reg);
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static u16 b43_nphy_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);
	/* N-PHY needs 0x100 for read access */
	reg |= 0x100;

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
}

static void b43_nphy_op_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(reg == 1);

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO_DATA_LOW, value);
}

static void b43_nphy_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{//TODO
}

static void b43_nphy_op_switch_analog(struct b43_wldev *dev, bool on)
{
	b43_phy_write(dev, B43_NPHY_AFECTL_OVER,
		      on ? 0 : 0x7FFF);
}

static int b43_nphy_op_switch_channel(struct b43_wldev *dev,
				      unsigned int new_channel)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		if (new_channel > 200)
			return -EINVAL;
	}

	return nphy_channel_switch(dev, new_channel);
}

static unsigned int b43_nphy_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		return 1;
	return 36;
}

const struct b43_phy_operations b43_phyops_n = {
	.allocate		= b43_nphy_op_allocate,
	.free			= b43_nphy_op_free,
	.prepare_structs	= b43_nphy_op_prepare_structs,
	.init			= b43_nphy_op_init,
	.phy_read		= b43_nphy_op_read,
	.phy_write		= b43_nphy_op_write,
	.radio_read		= b43_nphy_op_radio_read,
	.radio_write		= b43_nphy_op_radio_write,
	.software_rfkill	= b43_nphy_op_software_rfkill,
	.switch_analog		= b43_nphy_op_switch_analog,
	.switch_channel		= b43_nphy_op_switch_channel,
	.get_default_chan	= b43_nphy_op_get_default_chan,
	.recalc_txpower		= b43_nphy_op_recalc_txpower,
	.adjust_txpower		= b43_nphy_op_adjust_txpower,
};
