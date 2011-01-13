/*

  Broadcom B43 wireless driver

  PHY workarounds.

  Copyright (c) 2005-2007 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2007 Michael Buesch <mbuesch@freenet.de>

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
#include "main.h"
#include "tables.h"
#include "phy_common.h"
#include "wa.h"

static void b43_wa_papd(struct b43_wldev *dev)
{
	u16 backup;

	backup = b43_ofdmtab_read16(dev, B43_OFDMTAB_PWRDYN2, 0);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_PWRDYN2, 0, 7);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_APHY, 0, 0);
	b43_dummy_transmission(dev, true, true);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_PWRDYN2, 0, backup);
}

static void b43_wa_auxclipthr(struct b43_wldev *dev)
{
	b43_phy_write(dev, B43_PHY_OFDM(0x8E), 0x3800);
}

static void b43_wa_afcdac(struct b43_wldev *dev)
{
	b43_phy_write(dev, 0x0035, 0x03FF);
	b43_phy_write(dev, 0x0036, 0x0400);
}

static void b43_wa_txdc_offset(struct b43_wldev *dev)
{
	b43_ofdmtab_write16(dev, B43_OFDMTAB_DC, 0, 0x0051);
}

void b43_wa_initgains(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	b43_phy_write(dev, B43_PHY_LNAHPFCTL, 0x1FF9);
	b43_phy_mask(dev, B43_PHY_LPFGAINCTL, 0xFF0F);
	if (phy->rev <= 2)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_LPFGAIN, 0, 0x1FBF);
	b43_radio_write16(dev, 0x0002, 0x1FBF);

	b43_phy_write(dev, 0x0024, 0x4680);
	b43_phy_write(dev, 0x0020, 0x0003);
	b43_phy_write(dev, 0x001D, 0x0F40);
	b43_phy_write(dev, 0x001F, 0x1C00);
	if (phy->rev <= 3)
		b43_phy_maskset(dev, 0x002A, 0x00FF, 0x0400);
	else if (phy->rev == 5) {
		b43_phy_maskset(dev, 0x002A, 0x00FF, 0x1A00);
		b43_phy_write(dev, 0x00CC, 0x2121);
	}
	if (phy->rev >= 3)
		b43_phy_write(dev, 0x00BA, 0x3ED5);
}

static void b43_wa_divider(struct b43_wldev *dev)
{
	b43_phy_mask(dev, 0x002B, ~0x0100);
	b43_phy_write(dev, 0x008E, 0x58C1);
}

static void b43_wa_gt(struct b43_wldev *dev) /* Gain table. */
{
	if (dev->phy.rev <= 2) {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN2, 0, 15);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN2, 1, 31);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN2, 2, 42);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN2, 3, 48);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN2, 4, 58);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 0, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 1, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 2, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 3, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 4, 21);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 5, 21);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 6, 25);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN1, 0, 3);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN1, 1, 3);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN1, 2, 7);
	} else {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 0, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 1, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 2, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 3, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 4, 21);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 5, 21);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_GAIN0, 6, 25);
	}
}

static void b43_wa_rssi_lt(struct b43_wldev *dev) /* RSSI lookup table */
{
	int i;

	if (0 /* FIXME: For APHY.rev=2 this might be needed */) {
		for (i = 0; i < 8; i++)
			b43_ofdmtab_write16(dev, B43_OFDMTAB_RSSI, i, i + 8);
		for (i = 8; i < 16; i++)
			b43_ofdmtab_write16(dev, B43_OFDMTAB_RSSI, i, i - 8);
	} else {
		for (i = 0; i < 64; i++)
			b43_ofdmtab_write16(dev, B43_OFDMTAB_RSSI, i, i);
	}
}

static void b43_wa_analog(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 ofdmrev;

	ofdmrev = b43_phy_read(dev, B43_PHY_VERSION_OFDM) & B43_PHYVER_VERSION;
	if (ofdmrev > 2) {
		if (phy->type == B43_PHYTYPE_A)
			b43_phy_write(dev, B43_PHY_PWRDOWN, 0x1808);
		else
			b43_phy_write(dev, B43_PHY_PWRDOWN, 0x1000);
	} else {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 3, 0x1044);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 4, 0x7201);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 6, 0x0040);
	}
}

static void b43_wa_dac(struct b43_wldev *dev)
{
	if (dev->phy.analog == 1)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 1,
			(b43_ofdmtab_read16(dev, B43_OFDMTAB_DAC, 1) & ~0x0034) | 0x0008);
	else
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 1,
			(b43_ofdmtab_read16(dev, B43_OFDMTAB_DAC, 1) & ~0x0078) | 0x0010);
}

static void b43_wa_fft(struct b43_wldev *dev) /* Fine frequency table */
{
	int i;

	if (dev->phy.type == B43_PHYTYPE_A)
		for (i = 0; i < B43_TAB_FINEFREQA_SIZE; i++)
			b43_ofdmtab_write16(dev, B43_OFDMTAB_DACRFPABB, i, b43_tab_finefreqa[i]);
	else
		for (i = 0; i < B43_TAB_FINEFREQG_SIZE; i++)
			b43_ofdmtab_write16(dev, B43_OFDMTAB_DACRFPABB, i, b43_tab_finefreqg[i]);
}

static void b43_wa_nft(struct b43_wldev *dev) /* Noise figure table */
{
	struct b43_phy *phy = &dev->phy;
	int i;

	if (phy->type == B43_PHYTYPE_A) {
		if (phy->rev == 2)
			for (i = 0; i < B43_TAB_NOISEA2_SIZE; i++)
				b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, i, b43_tab_noisea2[i]);
		else
			for (i = 0; i < B43_TAB_NOISEA3_SIZE; i++)
				b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, i, b43_tab_noisea3[i]);
	} else {
		if (phy->rev == 1)
			for (i = 0; i < B43_TAB_NOISEG1_SIZE; i++)
				b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, i, b43_tab_noiseg1[i]);
		else
			for (i = 0; i < B43_TAB_NOISEG2_SIZE; i++)
				b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, i, b43_tab_noiseg2[i]);
	}
}

static void b43_wa_rt(struct b43_wldev *dev) /* Rotor table */
{
	int i;

	for (i = 0; i < B43_TAB_ROTOR_SIZE; i++)
		b43_ofdmtab_write32(dev, B43_OFDMTAB_ROTOR, i, b43_tab_rotor[i]);
}

static void b43_write_null_nst(struct b43_wldev *dev)
{
	int i;

	for (i = 0; i < B43_TAB_NOISESCALE_SIZE; i++)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_NOISESCALE, i, 0);
}

static void b43_write_nst(struct b43_wldev *dev, const u16 *nst)
{
	int i;

	for (i = 0; i < B43_TAB_NOISESCALE_SIZE; i++)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_NOISESCALE, i, nst[i]);
}

static void b43_wa_nst(struct b43_wldev *dev) /* Noise scale table */
{
	struct b43_phy *phy = &dev->phy;

	if (phy->type == B43_PHYTYPE_A) {
		if (phy->rev <= 1)
			b43_write_null_nst(dev);
		else if (phy->rev == 2)
			b43_write_nst(dev, b43_tab_noisescalea2);
		else if (phy->rev == 3)
			b43_write_nst(dev, b43_tab_noisescalea3);
		else
			b43_write_nst(dev, b43_tab_noisescaleg3);
	} else {
		if (phy->rev >= 6) {
			if (b43_phy_read(dev, B43_PHY_ENCORE) & B43_PHY_ENCORE_EN)
				b43_write_nst(dev, b43_tab_noisescaleg3);
			else
				b43_write_nst(dev, b43_tab_noisescaleg2);
		} else {
			b43_write_nst(dev, b43_tab_noisescaleg1);
		}
	}
}

static void b43_wa_art(struct b43_wldev *dev) /* ADV retard table */
{
	int i;

	for (i = 0; i < B43_TAB_RETARD_SIZE; i++)
			b43_ofdmtab_write32(dev, B43_OFDMTAB_ADVRETARD,
				i, b43_tab_retard[i]);
}

static void b43_wa_txlna_gain(struct b43_wldev *dev)
{
	b43_ofdmtab_write16(dev, B43_OFDMTAB_DC, 13, 0x0000);
}

static void b43_wa_crs_reset(struct b43_wldev *dev)
{
	b43_phy_write(dev, 0x002C, 0x0064);
}

static void b43_wa_2060txlna_gain(struct b43_wldev *dev)
{
	b43_hf_write(dev, b43_hf_read(dev) |
			 B43_HF_2060W);
}

static void b43_wa_lms(struct b43_wldev *dev)
{
	b43_phy_maskset(dev, 0x0055, 0xFFC0, 0x0004);
}

static void b43_wa_mixedsignal(struct b43_wldev *dev)
{
	b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 1, 3);
}

static void b43_wa_msst(struct b43_wldev *dev) /* Min sigma square table */
{
	struct b43_phy *phy = &dev->phy;
	int i;
	const u16 *tab;

	if (phy->type == B43_PHYTYPE_A) {
		tab = b43_tab_sigmasqr1;
	} else if (phy->type == B43_PHYTYPE_G) {
		tab = b43_tab_sigmasqr2;
	} else {
		B43_WARN_ON(1);
		return;
	}

	for (i = 0; i < B43_TAB_SIGMASQR_SIZE; i++) {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_MINSIGSQ,
					i, tab[i]);
	}
}

static void b43_wa_iqadc(struct b43_wldev *dev)
{
	if (dev->phy.analog == 4)
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DAC, 0,
			b43_ofdmtab_read16(dev, B43_OFDMTAB_DAC, 0) & ~0xF000);
}

static void b43_wa_crs_ed(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->rev == 1) {
		b43_phy_write(dev, B43_PHY_CRSTHRES1_R1, 0x4F19);
	} else if (phy->rev == 2) {
		b43_phy_write(dev, B43_PHY_CRSTHRES1, 0x1861);
		b43_phy_write(dev, B43_PHY_CRSTHRES2, 0x0271);
		b43_phy_set(dev, B43_PHY_ANTDWELL, 0x0800);
	} else {
		b43_phy_write(dev, B43_PHY_CRSTHRES1, 0x0098);
		b43_phy_write(dev, B43_PHY_CRSTHRES2, 0x0070);
		b43_phy_write(dev, B43_PHY_OFDM(0xC9), 0x0080);
		b43_phy_set(dev, B43_PHY_ANTDWELL, 0x0800);
	}
}

static void b43_wa_crs_thr(struct b43_wldev *dev)
{
	b43_phy_maskset(dev, B43_PHY_CRS0, ~0x03C0, 0xD000);
}

static void b43_wa_crs_blank(struct b43_wldev *dev)
{
	b43_phy_write(dev, B43_PHY_OFDM(0x2C), 0x005A);
}

static void b43_wa_cck_shiftbits(struct b43_wldev *dev)
{
	b43_phy_write(dev, B43_PHY_CCKSHIFTBITS, 0x0026);
}

static void b43_wa_wrssi_offset(struct b43_wldev *dev)
{
	int i;

	if (dev->phy.rev == 1) {
		for (i = 0; i < 16; i++) {
			b43_ofdmtab_write16(dev, B43_OFDMTAB_WRSSI_R1,
						i, 0x0020);
		}
	} else {
		for (i = 0; i < 32; i++) {
			b43_ofdmtab_write16(dev, B43_OFDMTAB_WRSSI,
						i, 0x0820);
		}
	}
}

static void b43_wa_txpuoff_rxpuon(struct b43_wldev *dev)
{
	b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_0F, 2, 15);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_0F, 3, 20);
}

static void b43_wa_altagc(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->rev == 1) {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1_R1, 0, 254);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1_R1, 1, 13);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1_R1, 2, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1_R1, 3, 25);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, 0, 0x2710);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, 1, 0x9B83);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, 2, 0x9B83);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC2, 3, 0x0F8D);
		b43_phy_write(dev, B43_PHY_LMS, 4);
	} else {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 0, 254);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 1, 13);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 2, 19);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC1, 3, 25);
	}

	b43_phy_maskset(dev, B43_PHY_CCKSHIFTBITS_WA, 0x00FF, 0x5700);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x1A), ~0x007F, 0x000F);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x1A), ~0x3F80, 0x2B80);
	b43_phy_maskset(dev, B43_PHY_ANTWRSETT, 0xF0FF, 0x0300);
	b43_radio_set(dev, 0x7A, 0x0008);
	b43_phy_maskset(dev, B43_PHY_N1P1GAIN, ~0x000F, 0x0008);
	b43_phy_maskset(dev, B43_PHY_P1P2GAIN, ~0x0F00, 0x0600);
	b43_phy_maskset(dev, B43_PHY_N1N2GAIN, ~0x0F00, 0x0700);
	b43_phy_maskset(dev, B43_PHY_N1P1GAIN, ~0x0F00, 0x0100);
	if (phy->rev == 1) {
		b43_phy_maskset(dev, B43_PHY_N1N2GAIN, ~0x000F, 0x0007);
	}
	b43_phy_maskset(dev, B43_PHY_OFDM(0x88), ~0x00FF, 0x001C);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x88), ~0x3F00, 0x0200);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x96), ~0x00FF, 0x001C);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x89), ~0x00FF, 0x0020);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x89), ~0x3F00, 0x0200);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x82), ~0x00FF, 0x002E);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x96), 0x00FF, 0x1A00);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x81), ~0x00FF, 0x0028);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x81), 0x00FF, 0x2C00);
	if (phy->rev == 1) {
		b43_phy_write(dev, B43_PHY_PEAK_COUNT, 0x092B);
		b43_phy_maskset(dev, B43_PHY_OFDM(0x1B), ~0x001E, 0x0002);
	} else {
		b43_phy_mask(dev, B43_PHY_OFDM(0x1B), ~0x001E);
		b43_phy_write(dev, B43_PHY_OFDM(0x1F), 0x287A);
		b43_phy_maskset(dev, B43_PHY_LPFGAINCTL, ~0x000F, 0x0004);
		if (phy->rev >= 6) {
			b43_phy_write(dev, B43_PHY_OFDM(0x22), 0x287A);
			b43_phy_maskset(dev, B43_PHY_LPFGAINCTL, 0x0FFF, 0x3000);
		}
	}
	b43_phy_maskset(dev, B43_PHY_DIVSRCHIDX, 0x8080, 0x7874);
	b43_phy_write(dev, B43_PHY_OFDM(0x8E), 0x1C00);
	if (phy->rev == 1) {
		b43_phy_maskset(dev, B43_PHY_DIVP1P2GAIN, ~0x0F00, 0x0600);
		b43_phy_write(dev, B43_PHY_OFDM(0x8B), 0x005E);
		b43_phy_maskset(dev, B43_PHY_ANTWRSETT, ~0x00FF, 0x001E);
		b43_phy_write(dev, B43_PHY_OFDM(0x8D), 0x0002);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3_R1, 0, 0);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3_R1, 1, 7);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3_R1, 2, 16);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3_R1, 3, 28);
	} else {
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3, 0, 0);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3, 1, 7);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3, 2, 16);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_AGC3, 3, 28);
	}
	if (phy->rev >= 6) {
		b43_phy_mask(dev, B43_PHY_OFDM(0x26), ~0x0003);
		b43_phy_mask(dev, B43_PHY_OFDM(0x26), ~0x1000);
	}
	b43_phy_read(dev, B43_PHY_VERSION_OFDM); /* Dummy read */
}

static void b43_wa_tr_ltov(struct b43_wldev *dev) /* TR Lookup Table Original Values */
{
	b43_gtab_write(dev, B43_GTAB_ORIGTR, 0, 0xC480);
}

static void b43_wa_cpll_nonpilot(struct b43_wldev *dev)
{
	b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_11, 0, 0);
	b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_11, 1, 0);
}

static void b43_wa_rssi_adc(struct b43_wldev *dev)
{
	if (dev->phy.analog == 4)
		b43_phy_write(dev, 0x00DC, 0x7454);
}

static void b43_wa_boards_a(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;

	if (bus->boardinfo.vendor == SSB_BOARDVENDOR_BCM &&
	    bus->boardinfo.type == SSB_BOARD_BU4306 &&
	    bus->boardinfo.rev < 0x30) {
		b43_phy_write(dev, 0x0010, 0xE000);
		b43_phy_write(dev, 0x0013, 0x0140);
		b43_phy_write(dev, 0x0014, 0x0280);
	} else {
		if (bus->boardinfo.type == SSB_BOARD_MP4318 &&
		    bus->boardinfo.rev < 0x20) {
			b43_phy_write(dev, 0x0013, 0x0210);
			b43_phy_write(dev, 0x0014, 0x0840);
		} else {
			b43_phy_write(dev, 0x0013, 0x0140);
			b43_phy_write(dev, 0x0014, 0x0280);
		}
		if (dev->phy.rev <= 4)
			b43_phy_write(dev, 0x0010, 0xE000);
		else
			b43_phy_write(dev, 0x0010, 0x2000);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_DC, 1, 0x0039);
		b43_ofdmtab_write16(dev, B43_OFDMTAB_UNKNOWN_APHY, 7, 0x0040);
	}
}

static void b43_wa_boards_g(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct b43_phy *phy = &dev->phy;

	if (bus->boardinfo.vendor != SSB_BOARDVENDOR_BCM ||
	    bus->boardinfo.type != SSB_BOARD_BU4306 ||
	    bus->boardinfo.rev != 0x17) {
		if (phy->rev < 2) {
			b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX_R1, 1, 0x0002);
			b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX_R1, 2, 0x0001);
		} else {
			b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 1, 0x0002);
			b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 2, 0x0001);
			if ((bus->sprom.boardflags_lo & B43_BFL_EXTLNA) &&
			    (phy->rev >= 7)) {
				b43_phy_mask(dev, B43_PHY_EXTG(0x11), 0xF7FF);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0020, 0x0001);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0021, 0x0001);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0022, 0x0001);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0023, 0x0000);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0000, 0x0000);
				b43_ofdmtab_write16(dev, B43_OFDMTAB_GAINX, 0x0003, 0x0002);
			}
		}
	}
	if (bus->sprom.boardflags_lo & B43_BFL_FEM) {
		b43_phy_write(dev, B43_PHY_GTABCTL, 0x3120);
		b43_phy_write(dev, B43_PHY_GTABDATA, 0xC480);
	}
}

void b43_wa_all(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	if (phy->type == B43_PHYTYPE_A) {
		switch (phy->rev) {
		case 2:
			b43_wa_papd(dev);
			b43_wa_auxclipthr(dev);
			b43_wa_afcdac(dev);
			b43_wa_txdc_offset(dev);
			b43_wa_initgains(dev);
			b43_wa_divider(dev);
			b43_wa_gt(dev);
			b43_wa_rssi_lt(dev);
			b43_wa_analog(dev);
			b43_wa_dac(dev);
			b43_wa_fft(dev);
			b43_wa_nft(dev);
			b43_wa_rt(dev);
			b43_wa_nst(dev);
			b43_wa_art(dev);
			b43_wa_txlna_gain(dev);
			b43_wa_crs_reset(dev);
			b43_wa_2060txlna_gain(dev);
			b43_wa_lms(dev);
			break;
		case 3:
			b43_wa_papd(dev);
			b43_wa_mixedsignal(dev);
			b43_wa_rssi_lt(dev);
			b43_wa_txdc_offset(dev);
			b43_wa_initgains(dev);
			b43_wa_dac(dev);
			b43_wa_nft(dev);
			b43_wa_nst(dev);
			b43_wa_msst(dev);
			b43_wa_analog(dev);
			b43_wa_gt(dev);
			b43_wa_txpuoff_rxpuon(dev);
			b43_wa_txlna_gain(dev);
			break;
		case 5:
			b43_wa_iqadc(dev);
		case 6:
			b43_wa_papd(dev);
			b43_wa_rssi_lt(dev);
			b43_wa_txdc_offset(dev);
			b43_wa_initgains(dev);
			b43_wa_dac(dev);
			b43_wa_nft(dev);
			b43_wa_nst(dev);
			b43_wa_msst(dev);
			b43_wa_analog(dev);
			b43_wa_gt(dev);
			b43_wa_txpuoff_rxpuon(dev);
			b43_wa_txlna_gain(dev);
			break;
		case 7:
			b43_wa_iqadc(dev);
			b43_wa_papd(dev);
			b43_wa_rssi_lt(dev);
			b43_wa_txdc_offset(dev);
			b43_wa_initgains(dev);
			b43_wa_dac(dev);
			b43_wa_nft(dev);
			b43_wa_nst(dev);
			b43_wa_msst(dev);
			b43_wa_analog(dev);
			b43_wa_gt(dev);
			b43_wa_txpuoff_rxpuon(dev);
			b43_wa_txlna_gain(dev);
			b43_wa_rssi_adc(dev);
		default:
			B43_WARN_ON(1);
		}
		b43_wa_boards_a(dev);
	} else if (phy->type == B43_PHYTYPE_G) {
		switch (phy->rev) {
		case 1://XXX review rev1
			b43_wa_crs_ed(dev);
			b43_wa_crs_thr(dev);
			b43_wa_crs_blank(dev);
			b43_wa_cck_shiftbits(dev);
			b43_wa_fft(dev);
			b43_wa_nft(dev);
			b43_wa_rt(dev);
			b43_wa_nst(dev);
			b43_wa_art(dev);
			b43_wa_wrssi_offset(dev);
			b43_wa_altagc(dev);
			break;
		case 2:
		case 6:
		case 7:
		case 8:
		case 9:
			b43_wa_tr_ltov(dev);
			b43_wa_crs_ed(dev);
			b43_wa_rssi_lt(dev);
			b43_wa_nft(dev);
			b43_wa_nst(dev);
			b43_wa_msst(dev);
			b43_wa_wrssi_offset(dev);
			b43_wa_altagc(dev);
			b43_wa_analog(dev);
			b43_wa_txpuoff_rxpuon(dev);
			break;
		default:
			B43_WARN_ON(1);
		}
		b43_wa_boards_g(dev);
	} else { /* No N PHY support so far, LP PHY is in phy_lp.c */
		B43_WARN_ON(1);
	}

	b43_wa_cpll_nonpilot(dev);
}
