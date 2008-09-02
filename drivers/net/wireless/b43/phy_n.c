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

/* Upload the N-PHY tables. */
static void b43_nphy_tables_init(struct b43_wldev *dev)
{
	/* Static tables */
	ntab_upload(dev, B43_NTAB_FRAMESTRUCT, b43_ntab_framestruct);
	ntab_upload(dev, B43_NTAB_FRAMELT, b43_ntab_framelookup);
	ntab_upload(dev, B43_NTAB_TMAP, b43_ntab_tmap);
	ntab_upload(dev, B43_NTAB_TDTRN, b43_ntab_tdtrn);
	ntab_upload(dev, B43_NTAB_INTLEVEL, b43_ntab_intlevel);
	ntab_upload(dev, B43_NTAB_PILOT, b43_ntab_pilot);
	ntab_upload(dev, B43_NTAB_PILOTLT, b43_ntab_pilotlt);
	ntab_upload(dev, B43_NTAB_TDI20A0, b43_ntab_tdi20a0);
	ntab_upload(dev, B43_NTAB_TDI20A1, b43_ntab_tdi20a1);
	ntab_upload(dev, B43_NTAB_TDI40A0, b43_ntab_tdi40a0);
	ntab_upload(dev, B43_NTAB_TDI40A1, b43_ntab_tdi40a1);
	ntab_upload(dev, B43_NTAB_BDI, b43_ntab_bdi);
	ntab_upload(dev, B43_NTAB_CHANEST, b43_ntab_channelest);
	ntab_upload(dev, B43_NTAB_MCS, b43_ntab_mcs);

	/* Volatile tables */
	ntab_upload(dev, B43_NTAB_NOISEVAR10, b43_ntab_noisevar10);
	ntab_upload(dev, B43_NTAB_NOISEVAR11, b43_ntab_noisevar11);
	ntab_upload(dev, B43_NTAB_C0_ESTPLT, b43_ntab_estimatepowerlt0);
	ntab_upload(dev, B43_NTAB_C1_ESTPLT, b43_ntab_estimatepowerlt1);
	ntab_upload(dev, B43_NTAB_C0_ADJPLT, b43_ntab_adjustpower0);
	ntab_upload(dev, B43_NTAB_C1_ADJPLT, b43_ntab_adjustpower1);
	ntab_upload(dev, B43_NTAB_C0_GAINCTL, b43_ntab_gainctl0);
	ntab_upload(dev, B43_NTAB_C1_GAINCTL, b43_ntab_gainctl1);
	ntab_upload(dev, B43_NTAB_C0_IQLT, b43_ntab_iqlt0);
	ntab_upload(dev, B43_NTAB_C1_IQLT, b43_ntab_iqlt1);
	ntab_upload(dev, B43_NTAB_C0_LOFEEDTH, b43_ntab_loftlt0);
	ntab_upload(dev, B43_NTAB_C1_LOFEEDTH, b43_ntab_loftlt1);
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

static void b43_nphy_reset_cca(struct b43_wldev *dev)
{
	u16 bbcfg;

	ssb_write32(dev->dev, SSB_TMSLOW,
		    ssb_read32(dev->dev, SSB_TMSLOW) | SSB_TMSLOW_FGC);
	bbcfg = b43_phy_read(dev, B43_NPHY_BBCFG);
	b43_phy_set(dev, B43_NPHY_BBCFG, B43_NPHY_BBCFG_RSTCCA);
	b43_phy_write(dev, B43_NPHY_BBCFG,
		      bbcfg & ~B43_NPHY_BBCFG_RSTCCA);
	ssb_write32(dev->dev, SSB_TMSLOW,
		    ssb_read32(dev->dev, SSB_TMSLOW) & ~SSB_TMSLOW_FGC);
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

/* RSSI Calibration */
static void b43_nphy_rssi_cal(struct b43_wldev *dev, u8 type)
{
	//TODO
}

int b43_phy_initn(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 tmp;

	//TODO: Spectral management
	b43_nphy_tables_init(dev);

	/* Clear all overrides */
	b43_phy_write(dev, B43_NPHY_RFCTL_OVER, 0);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, 0);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, 0);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC3, 0);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC4, 0);
	b43_phy_mask(dev, B43_NPHY_RFSEQMODE,
		     ~(B43_NPHY_RFSEQMODE_CAOVER |
		       B43_NPHY_RFSEQMODE_TROVER));
	b43_phy_write(dev, B43_NPHY_AFECTL_OVER, 0);

	tmp = (phy->rev < 2) ? 64 : 59;
	b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3,
			~B43_NPHY_BPHY_CTL3_SCALE,
			tmp << B43_NPHY_BPHY_CTL3_SCALE_SHIFT);

	b43_phy_write(dev, B43_NPHY_AFESEQ_TX2RX_PUD_20M, 0x20);
	b43_phy_write(dev, B43_NPHY_AFESEQ_TX2RX_PUD_40M, 0x20);

	b43_phy_write(dev, B43_NPHY_TXREALFD, 184);
	b43_phy_write(dev, B43_NPHY_MIMO_CRSTXEXT, 200);
	b43_phy_write(dev, B43_NPHY_PLOAD_CSENSE_EXTLEN, 80);
	b43_phy_write(dev, B43_NPHY_C2_BCLIPBKOFF, 511);

	//TODO MIMO-Config
	//TODO Update TX/RX chain

	if (phy->rev < 2) {
		b43_phy_write(dev, B43_NPHY_DUP40_GFBL, 0xAA8);
		b43_phy_write(dev, B43_NPHY_DUP40_BL, 0x9A4);
	}
	b43_nphy_workarounds(dev);
	b43_nphy_reset_cca(dev);

	ssb_write32(dev->dev, SSB_TMSLOW,
		    ssb_read32(dev->dev, SSB_TMSLOW) | B43_TMSLOW_MACPHYCLKEN);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RX2TX);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);

	b43_phy_read(dev, B43_NPHY_CLASSCTL); /* dummy read */
	//TODO read core1/2 clip1 thres regs

	if (1 /* FIXME Band is 2.4GHz */)
		b43_nphy_bphy_init(dev);
	//TODO disable TX power control
	//TODO Fix the TX power settings
	//TODO Init periodic calibration with reason 3
	b43_nphy_rssi_cal(dev, 2);
	b43_nphy_rssi_cal(dev, 0);
	b43_nphy_rssi_cal(dev, 1);
	//TODO get TX gain
	//TODO init superswitch
	//TODO calibrate LO
	//TODO idle TSSI TX pctl
	//TODO TX power control power setup
	//TODO table writes
	//TODO TX power control coefficients
	//TODO enable TX power control
	//TODO control antenna selection
	//TODO init radar detection
	//TODO reset channel if changed

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
					enum rfkill_state state)
{//TODO
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
	.switch_channel		= b43_nphy_op_switch_channel,
	.get_default_chan	= b43_nphy_op_get_default_chan,
	.recalc_txpower		= b43_nphy_op_recalc_txpower,
	.adjust_txpower		= b43_nphy_op_adjust_txpower,
};
