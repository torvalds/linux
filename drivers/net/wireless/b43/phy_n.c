/*

  Broadcom B43 wireless driver
  IEEE 802.11n PHY support

  Copyright (c) 2008 Michael Buesch <m@bues.ch>
  Copyright (c) 2010-2011 Rafał Miłecki <zajec5@gmail.com>

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
#include <linux/slab.h>
#include <linux/types.h>

#include "b43.h"
#include "phy_n.h"
#include "tables_nphy.h"
#include "radio_2055.h"
#include "radio_2056.h"
#include "radio_2057.h"
#include "main.h"

struct nphy_txgains {
	u16 tx_lpf[2];
	u16 txgm[2];
	u16 pga[2];
	u16 pad[2];
	u16 ipa[2];
};

struct nphy_iqcal_params {
	u16 tx_lpf;
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

enum b43_nphy_rf_sequence {
	B43_RFSEQ_RX2TX,
	B43_RFSEQ_TX2RX,
	B43_RFSEQ_RESET2RX,
	B43_RFSEQ_UPDATE_GAINH,
	B43_RFSEQ_UPDATE_GAINL,
	B43_RFSEQ_UPDATE_GAINU,
};

enum n_rf_ctl_over_cmd {
	N_RF_CTL_OVER_CMD_RXRF_PU = 0,
	N_RF_CTL_OVER_CMD_RX_PU = 1,
	N_RF_CTL_OVER_CMD_TX_PU = 2,
	N_RF_CTL_OVER_CMD_RX_GAIN = 3,
	N_RF_CTL_OVER_CMD_TX_GAIN = 4,
};

enum n_intc_override {
	N_INTC_OVERRIDE_OFF = 0,
	N_INTC_OVERRIDE_TRSW = 1,
	N_INTC_OVERRIDE_PA = 2,
	N_INTC_OVERRIDE_EXT_LNA_PU = 3,
	N_INTC_OVERRIDE_EXT_LNA_GAIN = 4,
};

enum n_rssi_type {
	N_RSSI_W1 = 0,
	N_RSSI_W2,
	N_RSSI_NB,
	N_RSSI_IQ,
	N_RSSI_TSSI_2G,
	N_RSSI_TSSI_5G,
	N_RSSI_TBD,
};

enum n_rail_type {
	N_RAIL_I = 0,
	N_RAIL_Q = 1,
};

static inline bool b43_nphy_ipa(struct b43_wldev *dev)
{
	enum ieee80211_band band = b43_current_band(dev->wl);
	return ((dev->phy.n->ipa2g_on && band == IEEE80211_BAND_2GHZ) ||
		(dev->phy.n->ipa5g_on && band == IEEE80211_BAND_5GHZ));
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCoreGetState */
static u8 b43_nphy_get_rx_core_state(struct b43_wldev *dev)
{
	return (b43_phy_read(dev, B43_NPHY_RFSEQCA) & B43_NPHY_RFSEQCA_RXEN) >>
		B43_NPHY_RFSEQCA_RXEN_SHIFT;
}

/**************************************************
 * RF (just without b43_nphy_rf_ctl_intc_override)
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ForceRFSeq */
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
	u16 seq_mode = b43_phy_read(dev, B43_NPHY_RFSEQMODE);

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
	b43_phy_write(dev, B43_NPHY_RFSEQMODE, seq_mode);
}

static void b43_nphy_rf_ctl_override_rev19(struct b43_wldev *dev, u16 field,
					   u16 value, u8 core, bool off,
					   u8 override_id)
{
	/* TODO */
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverrideRev7 */
static void b43_nphy_rf_ctl_override_rev7(struct b43_wldev *dev, u16 field,
					  u16 value, u8 core, bool off,
					  u8 override)
{
	struct b43_phy *phy = &dev->phy;
	const struct nphy_rf_control_override_rev7 *e;
	u16 en_addrs[3][2] = {
		{ 0x0E7, 0x0EC }, { 0x342, 0x343 }, { 0x346, 0x347 }
	};
	u16 en_addr;
	u16 en_mask = field;
	u16 val_addr;
	u8 i;

	if (phy->rev >= 19 || phy->rev < 3) {
		B43_WARN_ON(1);
		return;
	}

	/* Remember: we can get NULL! */
	e = b43_nphy_get_rf_ctl_over_rev7(dev, field, override);

	for (i = 0; i < 2; i++) {
		if (override >= ARRAY_SIZE(en_addrs)) {
			b43err(dev->wl, "Invalid override value %d\n", override);
			return;
		}
		en_addr = en_addrs[override][i];

		if (e)
			val_addr = (i == 0) ? e->val_addr_core0 : e->val_addr_core1;

		if (off) {
			b43_phy_mask(dev, en_addr, ~en_mask);
			if (e) /* Do it safer, better than wl */
				b43_phy_mask(dev, val_addr, ~e->val_mask);
		} else {
			if (!core || (core & (1 << i))) {
				b43_phy_set(dev, en_addr, en_mask);
				if (e)
					b43_phy_maskset(dev, val_addr, ~e->val_mask, (value << e->val_shift));
			}
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverideOneToMany */
static void b43_nphy_rf_ctl_override_one_to_many(struct b43_wldev *dev,
						 enum n_rf_ctl_over_cmd cmd,
						 u16 value, u8 core, bool off)
{
	struct b43_phy *phy = &dev->phy;
	u16 tmp;

	B43_WARN_ON(phy->rev < 7);

	switch (cmd) {
	case N_RF_CTL_OVER_CMD_RXRF_PU:
		b43_nphy_rf_ctl_override_rev7(dev, 0x20, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x10, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x08, value, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_RX_PU:
		b43_nphy_rf_ctl_override_rev7(dev, 0x4, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x2, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x1, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x2, value, core, off, 2);
		b43_nphy_rf_ctl_override_rev7(dev, 0x0800, value, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_TX_PU:
		b43_nphy_rf_ctl_override_rev7(dev, 0x4, value, core, off, 0);
		b43_nphy_rf_ctl_override_rev7(dev, 0x2, value, core, off, 1);
		b43_nphy_rf_ctl_override_rev7(dev, 0x1, value, core, off, 2);
		b43_nphy_rf_ctl_override_rev7(dev, 0x0800, value, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_RX_GAIN:
		tmp = value & 0xFF;
		b43_nphy_rf_ctl_override_rev7(dev, 0x0800, tmp, core, off, 0);
		tmp = value >> 8;
		b43_nphy_rf_ctl_override_rev7(dev, 0x6000, tmp, core, off, 0);
		break;
	case N_RF_CTL_OVER_CMD_TX_GAIN:
		tmp = value & 0x7FFF;
		b43_nphy_rf_ctl_override_rev7(dev, 0x1000, tmp, core, off, 0);
		tmp = value >> 14;
		b43_nphy_rf_ctl_override_rev7(dev, 0x4000, tmp, core, off, 0);
		break;
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverride */
static void b43_nphy_rf_ctl_override(struct b43_wldev *dev, u16 field,
				     u16 value, u8 core, bool off)
{
	int i;
	u8 index = fls(field);
	u8 addr, en_addr, val_addr;
	/* we expect only one bit set */
	B43_WARN_ON(field & (~(1 << (index - 1))));

	if (dev->phy.rev >= 3) {
		const struct nphy_rf_control_override_rev3 *rf_ctrl;
		for (i = 0; i < 2; i++) {
			if (index == 0 || index == 16) {
				b43err(dev->wl,
					"Unsupported RF Ctrl Override call\n");
				return;
			}

			rf_ctrl = &tbl_rf_control_override_rev3[index - 1];
			en_addr = B43_PHY_N((i == 0) ?
				rf_ctrl->en_addr0 : rf_ctrl->en_addr1);
			val_addr = B43_PHY_N((i == 0) ?
				rf_ctrl->val_addr0 : rf_ctrl->val_addr1);

			if (off) {
				b43_phy_mask(dev, en_addr, ~(field));
				b43_phy_mask(dev, val_addr,
						~(rf_ctrl->val_mask));
			} else {
				if (core == 0 || ((1 << i) & core)) {
					b43_phy_set(dev, en_addr, field);
					b43_phy_maskset(dev, val_addr,
						~(rf_ctrl->val_mask),
						(value << rf_ctrl->val_shift));
				}
			}
		}
	} else {
		const struct nphy_rf_control_override_rev2 *rf_ctrl;
		if (off) {
			b43_phy_mask(dev, B43_NPHY_RFCTL_OVER, ~(field));
			value = 0;
		} else {
			b43_phy_set(dev, B43_NPHY_RFCTL_OVER, field);
		}

		for (i = 0; i < 2; i++) {
			if (index <= 1 || index == 16) {
				b43err(dev->wl,
					"Unsupported RF Ctrl Override call\n");
				return;
			}

			if (index == 2 || index == 10 ||
			    (index >= 13 && index <= 15)) {
				core = 1;
			}

			rf_ctrl = &tbl_rf_control_override_rev2[index - 2];
			addr = B43_PHY_N((i == 0) ?
				rf_ctrl->addr0 : rf_ctrl->addr1);

			if ((1 << i) & core)
				b43_phy_maskset(dev, addr, ~(rf_ctrl->bmask),
						(value << rf_ctrl->shift));

			b43_phy_set(dev, B43_NPHY_RFCTL_OVER, 0x1);
			b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
					B43_NPHY_RFCTL_CMD_START);
			udelay(1);
			b43_phy_mask(dev, B43_NPHY_RFCTL_OVER, 0xFFFE);
		}
	}
}

static void b43_nphy_rf_ctl_intc_override_rev7(struct b43_wldev *dev,
					       enum n_intc_override intc_override,
					       u16 value, u8 core_sel)
{
	u16 reg, tmp, tmp2, val;
	int core;

	/* TODO: What about rev19+? Revs 3+ and 7+ are a bit similar */

	for (core = 0; core < 2; core++) {
		if ((core_sel == 1 && core != 0) ||
		    (core_sel == 2 && core != 1))
			continue;

		reg = (core == 0) ? B43_NPHY_RFCTL_INTC1 : B43_NPHY_RFCTL_INTC2;

		switch (intc_override) {
		case N_INTC_OVERRIDE_OFF:
			b43_phy_write(dev, reg, 0);
			b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
			break;
		case N_INTC_OVERRIDE_TRSW:
			b43_phy_maskset(dev, reg, ~0xC0, value << 6);
			b43_phy_set(dev, reg, 0x400);

			b43_phy_mask(dev, 0x2ff, ~0xC000 & 0xFFFF);
			b43_phy_set(dev, 0x2ff, 0x2000);
			b43_phy_set(dev, 0x2ff, 0x0001);
			break;
		case N_INTC_OVERRIDE_PA:
			tmp = 0x0030;
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
				val = value << 5;
			else
				val = value << 4;
			b43_phy_maskset(dev, reg, ~tmp, val);
			b43_phy_set(dev, reg, 0x1000);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_PU:
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
				tmp = 0x0001;
				tmp2 = 0x0004;
				val = value;
			} else {
				tmp = 0x0004;
				tmp2 = 0x0001;
				val = value << 2;
			}
			b43_phy_maskset(dev, reg, ~tmp, val);
			b43_phy_mask(dev, reg, ~tmp2);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_GAIN:
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
				tmp = 0x0002;
				tmp2 = 0x0008;
				val = value << 1;
			} else {
				tmp = 0x0008;
				tmp2 = 0x0002;
				val = value << 3;
			}
			b43_phy_maskset(dev, reg, ~tmp, val);
			b43_phy_mask(dev, reg, ~tmp2);
			break;
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlIntcOverride */
static void b43_nphy_rf_ctl_intc_override(struct b43_wldev *dev,
					  enum n_intc_override intc_override,
					  u16 value, u8 core)
{
	u8 i, j;
	u16 reg, tmp, val;

	if (dev->phy.rev >= 7) {
		b43_nphy_rf_ctl_intc_override_rev7(dev, intc_override, value,
						   core);
		return;
	}

	B43_WARN_ON(dev->phy.rev < 3);

	for (i = 0; i < 2; i++) {
		if ((core == 1 && i == 1) || (core == 2 && !i))
			continue;

		reg = (i == 0) ?
			B43_NPHY_RFCTL_INTC1 : B43_NPHY_RFCTL_INTC2;
		b43_phy_set(dev, reg, 0x400);

		switch (intc_override) {
		case N_INTC_OVERRIDE_OFF:
			b43_phy_write(dev, reg, 0);
			b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
			break;
		case N_INTC_OVERRIDE_TRSW:
			if (!i) {
				b43_phy_maskset(dev, B43_NPHY_RFCTL_INTC1,
						0xFC3F, (value << 6));
				b43_phy_maskset(dev, B43_NPHY_TXF_40CO_B1S1,
						0xFFFE, 1);
				b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
						B43_NPHY_RFCTL_CMD_START);
				for (j = 0; j < 100; j++) {
					if (!(b43_phy_read(dev, B43_NPHY_RFCTL_CMD) & B43_NPHY_RFCTL_CMD_START)) {
						j = 0;
						break;
					}
					udelay(10);
				}
				if (j)
					b43err(dev->wl,
						"intc override timeout\n");
				b43_phy_mask(dev, B43_NPHY_TXF_40CO_B1S1,
						0xFFFE);
			} else {
				b43_phy_maskset(dev, B43_NPHY_RFCTL_INTC2,
						0xFC3F, (value << 6));
				b43_phy_maskset(dev, B43_NPHY_RFCTL_OVER,
						0xFFFE, 1);
				b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
						B43_NPHY_RFCTL_CMD_RXTX);
				for (j = 0; j < 100; j++) {
					if (!(b43_phy_read(dev, B43_NPHY_RFCTL_CMD) & B43_NPHY_RFCTL_CMD_RXTX)) {
						j = 0;
						break;
					}
					udelay(10);
				}
				if (j)
					b43err(dev->wl,
						"intc override timeout\n");
				b43_phy_mask(dev, B43_NPHY_RFCTL_OVER,
						0xFFFE);
			}
			break;
		case N_INTC_OVERRIDE_PA:
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
				tmp = 0x0020;
				val = value << 5;
			} else {
				tmp = 0x0010;
				val = value << 4;
			}
			b43_phy_maskset(dev, reg, ~tmp, val);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_PU:
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
				tmp = 0x0001;
				val = value;
			} else {
				tmp = 0x0004;
				val = value << 2;
			}
			b43_phy_maskset(dev, reg, ~tmp, val);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_GAIN:
			if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
				tmp = 0x0002;
				val = value << 1;
			} else {
				tmp = 0x0008;
				val = value << 3;
			}
			b43_phy_maskset(dev, reg, ~tmp, val);
			break;
		}
	}
}

/**************************************************
 * Various PHY ops
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/clip-detection */
static void b43_nphy_write_clip_detection(struct b43_wldev *dev,
					  const u16 *clip_st)
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

	if (dev->dev->core_rev == 16)
		b43_mac_suspend(dev);

	tmp = b43_phy_read(dev, B43_NPHY_CLASSCTL);
	tmp &= (B43_NPHY_CLASSCTL_CCKEN | B43_NPHY_CLASSCTL_OFDMEN |
		B43_NPHY_CLASSCTL_WAITEDEN);
	tmp &= ~mask;
	tmp |= (val & mask);
	b43_phy_maskset(dev, B43_NPHY_CLASSCTL, 0xFFF8, tmp);

	if (dev->dev->core_rev == 16)
		b43_mac_enable(dev);

	return tmp;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CCA */
static void b43_nphy_reset_cca(struct b43_wldev *dev)
{
	u16 bbcfg;

	b43_phy_force_clock(dev, 1);
	bbcfg = b43_phy_read(dev, B43_NPHY_BBCFG);
	b43_phy_write(dev, B43_NPHY_BBCFG, bbcfg | B43_NPHY_BBCFG_RSTCCA);
	udelay(1);
	b43_phy_write(dev, B43_NPHY_BBCFG, bbcfg & ~B43_NPHY_BBCFG_RSTCCA);
	b43_phy_force_clock(dev, 0);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/carriersearch */
static void b43_nphy_stay_in_carrier_search(struct b43_wldev *dev, bool enable)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	if (enable) {
		static const u16 clip[] = { 0xFFFF, 0xFFFF };
		if (nphy->deaf_count++ == 0) {
			nphy->classifier_state = b43_nphy_classifier(dev, 0, 0);
			b43_nphy_classifier(dev, 0x7,
					    B43_NPHY_CLASSCTL_WAITEDEN);
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

/* http://bcm-v4.sipsolutions.net/PHY/N/Read_Lpf_Bw_Ctl */
static u16 b43_nphy_read_lpf_ctl(struct b43_wldev *dev, u16 offset)
{
	if (!offset)
		offset = b43_is_40mhz(dev) ? 0x159 : 0x154;
	return b43_ntab_read(dev, B43_NTAB16(7, offset)) & 0x7;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/AdjustLnaGainTbl */
static void b43_nphy_adjust_lna_gain_table(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u8 i;
	s16 tmp;
	u16 data[4];
	s16 gain[2];
	u16 minmax[2];
	static const u16 lna_gain[4] = { -2, 10, 19, 25 };

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	if (nphy->gain_boost) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			gain[0] = 6;
			gain[1] = 6;
		} else {
			tmp = 40370 - 315 * dev->phy.channel;
			gain[0] = ((tmp >> 13) + ((tmp >> 12) & 1));
			tmp = 23242 - 224 * dev->phy.channel;
			gain[1] = ((tmp >> 13) + ((tmp >> 12) & 1));
		}
	} else {
		gain[0] = 0;
		gain[1] = 0;
	}

	for (i = 0; i < 2; i++) {
		if (nphy->elna_gain_config) {
			data[0] = 19 + gain[i];
			data[1] = 25 + gain[i];
			data[2] = 25 + gain[i];
			data[3] = 25 + gain[i];
		} else {
			data[0] = lna_gain[0] + gain[i];
			data[1] = lna_gain[1] + gain[i];
			data[2] = lna_gain[2] + gain[i];
			data[3] = lna_gain[3] + gain[i];
		}
		b43_ntab_write_bulk(dev, B43_NTAB16(i, 8), 4, data);

		minmax[i] = 23 + gain[i];
	}

	b43_phy_maskset(dev, B43_NPHY_C1_MINMAX_GAIN, ~B43_NPHY_C1_MINGAIN,
				minmax[0] << B43_NPHY_C1_MINGAIN_SHIFT);
	b43_phy_maskset(dev, B43_NPHY_C2_MINMAX_GAIN, ~B43_NPHY_C2_MINGAIN,
				minmax[1] << B43_NPHY_C2_MINGAIN_SHIFT);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetRfSeq */
static void b43_nphy_set_rf_sequence(struct b43_wldev *dev, u8 cmd,
					u8 *events, u8 *delays, u8 length)
{
	struct b43_phy_n *nphy = dev->phy.n;
	u8 i;
	u8 end = (dev->phy.rev >= 3) ? 0x1F : 0x0F;
	u16 offset1 = cmd << 4;
	u16 offset2 = offset1 + 0x80;

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, true);

	b43_ntab_write_bulk(dev, B43_NTAB8(7, offset1), length, events);
	b43_ntab_write_bulk(dev, B43_NTAB8(7, offset2), length, delays);

	for (i = length; i < 16; i++) {
		b43_ntab_write(dev, B43_NTAB8(7, offset1 + i), end);
		b43_ntab_write(dev, B43_NTAB8(7, offset2 + i), 1);
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, false);
}

/**************************************************
 * Radio 0x2057
 **************************************************/

static void b43_radio_2057_chantab_upload(struct b43_wldev *dev,
					  const struct b43_nphy_chantabent_rev7 *e_r7,
					  const struct b43_nphy_chantabent_rev7_2g *e_r7_2g)
{
	if (e_r7_2g) {
		b43_radio_write(dev, R2057_VCOCAL_COUNTVAL0, e_r7_2g->radio_vcocal_countval0);
		b43_radio_write(dev, R2057_VCOCAL_COUNTVAL1, e_r7_2g->radio_vcocal_countval1);
		b43_radio_write(dev, R2057_RFPLL_REFMASTER_SPAREXTALSIZE, e_r7_2g->radio_rfpll_refmaster_sparextalsize);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_R1, e_r7_2g->radio_rfpll_loopfilter_r1);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C2, e_r7_2g->radio_rfpll_loopfilter_c2);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C1, e_r7_2g->radio_rfpll_loopfilter_c1);
		b43_radio_write(dev, R2057_CP_KPD_IDAC, e_r7_2g->radio_cp_kpd_idac);
		b43_radio_write(dev, R2057_RFPLL_MMD0, e_r7_2g->radio_rfpll_mmd0);
		b43_radio_write(dev, R2057_RFPLL_MMD1, e_r7_2g->radio_rfpll_mmd1);
		b43_radio_write(dev, R2057_VCOBUF_TUNE, e_r7_2g->radio_vcobuf_tune);
		b43_radio_write(dev, R2057_LOGEN_MX2G_TUNE, e_r7_2g->radio_logen_mx2g_tune);
		b43_radio_write(dev, R2057_LOGEN_INDBUF2G_TUNE, e_r7_2g->radio_logen_indbuf2g_tune);
		b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0, e_r7_2g->radio_txmix2g_tune_boost_pu_core0);
		b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE0, e_r7_2g->radio_pad2g_tune_pus_core0);
		b43_radio_write(dev, R2057_LNA2G_TUNE_CORE0, e_r7_2g->radio_lna2g_tune_core0);
		b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1, e_r7_2g->radio_txmix2g_tune_boost_pu_core1);
		b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE1, e_r7_2g->radio_pad2g_tune_pus_core1);
		b43_radio_write(dev, R2057_LNA2G_TUNE_CORE1, e_r7_2g->radio_lna2g_tune_core1);

	} else {
		b43_radio_write(dev, R2057_VCOCAL_COUNTVAL0, e_r7->radio_vcocal_countval0);
		b43_radio_write(dev, R2057_VCOCAL_COUNTVAL1, e_r7->radio_vcocal_countval1);
		b43_radio_write(dev, R2057_RFPLL_REFMASTER_SPAREXTALSIZE, e_r7->radio_rfpll_refmaster_sparextalsize);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_R1, e_r7->radio_rfpll_loopfilter_r1);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C2, e_r7->radio_rfpll_loopfilter_c2);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C1, e_r7->radio_rfpll_loopfilter_c1);
		b43_radio_write(dev, R2057_CP_KPD_IDAC, e_r7->radio_cp_kpd_idac);
		b43_radio_write(dev, R2057_RFPLL_MMD0, e_r7->radio_rfpll_mmd0);
		b43_radio_write(dev, R2057_RFPLL_MMD1, e_r7->radio_rfpll_mmd1);
		b43_radio_write(dev, R2057_VCOBUF_TUNE, e_r7->radio_vcobuf_tune);
		b43_radio_write(dev, R2057_LOGEN_MX2G_TUNE, e_r7->radio_logen_mx2g_tune);
		b43_radio_write(dev, R2057_LOGEN_MX5G_TUNE, e_r7->radio_logen_mx5g_tune);
		b43_radio_write(dev, R2057_LOGEN_INDBUF2G_TUNE, e_r7->radio_logen_indbuf2g_tune);
		b43_radio_write(dev, R2057_LOGEN_INDBUF5G_TUNE, e_r7->radio_logen_indbuf5g_tune);
		b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0, e_r7->radio_txmix2g_tune_boost_pu_core0);
		b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE0, e_r7->radio_pad2g_tune_pus_core0);
		b43_radio_write(dev, R2057_PGA_BOOST_TUNE_CORE0, e_r7->radio_pga_boost_tune_core0);
		b43_radio_write(dev, R2057_TXMIX5G_BOOST_TUNE_CORE0, e_r7->radio_txmix5g_boost_tune_core0);
		b43_radio_write(dev, R2057_PAD5G_TUNE_MISC_PUS_CORE0, e_r7->radio_pad5g_tune_misc_pus_core0);
		b43_radio_write(dev, R2057_LNA2G_TUNE_CORE0, e_r7->radio_lna2g_tune_core0);
		b43_radio_write(dev, R2057_LNA5G_TUNE_CORE0, e_r7->radio_lna5g_tune_core0);
		b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1, e_r7->radio_txmix2g_tune_boost_pu_core1);
		b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE1, e_r7->radio_pad2g_tune_pus_core1);
		b43_radio_write(dev, R2057_PGA_BOOST_TUNE_CORE1, e_r7->radio_pga_boost_tune_core1);
		b43_radio_write(dev, R2057_TXMIX5G_BOOST_TUNE_CORE1, e_r7->radio_txmix5g_boost_tune_core1);
		b43_radio_write(dev, R2057_PAD5G_TUNE_MISC_PUS_CORE1, e_r7->radio_pad5g_tune_misc_pus_core1);
		b43_radio_write(dev, R2057_LNA2G_TUNE_CORE1, e_r7->radio_lna2g_tune_core1);
		b43_radio_write(dev, R2057_LNA5G_TUNE_CORE1, e_r7->radio_lna5g_tune_core1);
	}
}

static void b43_radio_2057_setup(struct b43_wldev *dev,
				 const struct b43_nphy_chantabent_rev7 *tabent_r7,
				 const struct b43_nphy_chantabent_rev7_2g *tabent_r7_2g)
{
	struct b43_phy *phy = &dev->phy;

	b43_radio_2057_chantab_upload(dev, tabent_r7, tabent_r7_2g);

	switch (phy->radio_rev) {
	case 0 ... 4:
	case 6:
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_R1, 0x3f);
			b43_radio_write(dev, R2057_CP_KPD_IDAC, 0x3f);
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C1, 0x8);
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C2, 0x8);
		} else {
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_R1, 0x1f);
			b43_radio_write(dev, R2057_CP_KPD_IDAC, 0x3f);
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C1, 0x8);
			b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C2, 0x8);
		}
		break;
	case 9: /* e.g. PHY rev 16 */
		b43_radio_write(dev, R2057_LOGEN_PTAT_RESETS, 0x20);
		b43_radio_write(dev, R2057_VCOBUF_IDACS, 0x18);
		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			b43_radio_write(dev, R2057_LOGEN_PTAT_RESETS, 0x38);
			b43_radio_write(dev, R2057_VCOBUF_IDACS, 0x0f);

			if (b43_is_40mhz(dev)) {
				/* TODO */
			} else {
				b43_radio_write(dev,
						R2057_PAD_BIAS_FILTER_BWS_CORE0,
						0x3c);
				b43_radio_write(dev,
						R2057_PAD_BIAS_FILTER_BWS_CORE1,
						0x3c);
			}
		}
		break;
	case 14: /* 2 GHz only */
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_R1, 0x1b);
		b43_radio_write(dev, R2057_CP_KPD_IDAC, 0x3f);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C1, 0x1f);
		b43_radio_write(dev, R2057_RFPLL_LOOPFILTER_C2, 0x1f);
		break;
	}

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		u16 txmix2g_tune_boost_pu = 0;
		u16 pad2g_tune_pus = 0;

		if (b43_nphy_ipa(dev)) {
			switch (phy->radio_rev) {
			case 9:
				txmix2g_tune_boost_pu = 0x0041;
				/* TODO */
				break;
			case 14:
				txmix2g_tune_boost_pu = 0x21;
				pad2g_tune_pus = 0x23;
				break;
			}
		}

		if (txmix2g_tune_boost_pu)
			b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0,
					txmix2g_tune_boost_pu);
		if (pad2g_tune_pus)
			b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE0,
					pad2g_tune_pus);
		if (txmix2g_tune_boost_pu)
			b43_radio_write(dev, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1,
					txmix2g_tune_boost_pu);
		if (pad2g_tune_pus)
			b43_radio_write(dev, R2057_PAD2G_TUNE_PUS_CORE1,
					pad2g_tune_pus);
	}

	usleep_range(50, 100);

	/* VCO calibration */
	b43_radio_mask(dev, R2057_RFPLL_MISC_EN, ~0x01);
	b43_radio_mask(dev, R2057_RFPLL_MISC_CAL_RESETN, ~0x04);
	b43_radio_set(dev, R2057_RFPLL_MISC_CAL_RESETN, 0x4);
	b43_radio_set(dev, R2057_RFPLL_MISC_EN, 0x01);
	usleep_range(300, 600);
}

/* Calibrate resistors in LPF of PLL?
 * http://bcm-v4.sipsolutions.net/PHY/radio205x_rcal
 */
static u8 b43_radio_2057_rcal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 saved_regs_phy[12];
	u16 saved_regs_phy_rf[6];
	u16 saved_regs_radio[2] = { };
	static const u16 phy_to_store[] = {
		B43_NPHY_RFCTL_RSSIO1, B43_NPHY_RFCTL_RSSIO2,
		B43_NPHY_RFCTL_LUT_TRSW_LO1, B43_NPHY_RFCTL_LUT_TRSW_LO2,
		B43_NPHY_RFCTL_RXG1, B43_NPHY_RFCTL_RXG2,
		B43_NPHY_RFCTL_TXG1, B43_NPHY_RFCTL_TXG2,
		B43_NPHY_REV7_RF_CTL_MISC_REG3, B43_NPHY_REV7_RF_CTL_MISC_REG4,
		B43_NPHY_REV7_RF_CTL_MISC_REG5, B43_NPHY_REV7_RF_CTL_MISC_REG6,
	};
	static const u16 phy_to_store_rf[] = {
		B43_NPHY_REV3_RFCTL_OVER0, B43_NPHY_REV3_RFCTL_OVER1,
		B43_NPHY_REV7_RF_CTL_OVER3, B43_NPHY_REV7_RF_CTL_OVER4,
		B43_NPHY_REV7_RF_CTL_OVER5, B43_NPHY_REV7_RF_CTL_OVER6,
	};
	u16 tmp;
	int i;

	/* Save */
	for (i = 0; i < ARRAY_SIZE(phy_to_store); i++)
		saved_regs_phy[i] = b43_phy_read(dev, phy_to_store[i]);
	for (i = 0; i < ARRAY_SIZE(phy_to_store_rf); i++)
		saved_regs_phy_rf[i] = b43_phy_read(dev, phy_to_store_rf[i]);

	/* Set */
	for (i = 0; i < ARRAY_SIZE(phy_to_store); i++)
		b43_phy_write(dev, phy_to_store[i], 0);
	b43_phy_write(dev, B43_NPHY_REV3_RFCTL_OVER0, 0x07ff);
	b43_phy_write(dev, B43_NPHY_REV3_RFCTL_OVER1, 0x07ff);
	b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER3, 0x07ff);
	b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER4, 0x07ff);
	b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER5, 0x007f);
	b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER6, 0x007f);

	switch (phy->radio_rev) {
	case 5:
		b43_phy_mask(dev, B43_NPHY_REV7_RF_CTL_OVER3, ~0x2);
		udelay(10);
		b43_radio_set(dev, R2057_IQTEST_SEL_PU, 0x1);
		b43_radio_maskset(dev, R2057v7_IQTEST_SEL_PU2, ~0x2, 0x1);
		break;
	case 9:
		b43_phy_set(dev, B43_NPHY_REV7_RF_CTL_OVER3, 0x2);
		b43_phy_set(dev, B43_NPHY_REV7_RF_CTL_MISC_REG3, 0x2);
		saved_regs_radio[0] = b43_radio_read(dev, R2057_IQTEST_SEL_PU);
		b43_radio_write(dev, R2057_IQTEST_SEL_PU, 0x11);
		break;
	case 14:
		saved_regs_radio[0] = b43_radio_read(dev, R2057_IQTEST_SEL_PU);
		saved_regs_radio[1] = b43_radio_read(dev, R2057v7_IQTEST_SEL_PU2);
		b43_phy_set(dev, B43_NPHY_REV7_RF_CTL_MISC_REG3, 0x2);
		b43_phy_set(dev, B43_NPHY_REV7_RF_CTL_OVER3, 0x2);
		b43_radio_write(dev, R2057v7_IQTEST_SEL_PU2, 0x2);
		b43_radio_write(dev, R2057_IQTEST_SEL_PU, 0x1);
		break;
	}

	/* Enable */
	b43_radio_set(dev, R2057_RCAL_CONFIG, 0x1);
	udelay(10);

	/* Start */
	b43_radio_set(dev, R2057_RCAL_CONFIG, 0x2);
	usleep_range(100, 200);

	/* Stop */
	b43_radio_mask(dev, R2057_RCAL_CONFIG, ~0x2);

	/* Wait and check for result */
	if (!b43_radio_wait_value(dev, R2057_RCAL_STATUS, 1, 1, 100, 1000000)) {
		b43err(dev->wl, "Radio 0x2057 rcal timeout\n");
		return 0;
	}
	tmp = b43_radio_read(dev, R2057_RCAL_STATUS) & 0x3E;

	/* Disable */
	b43_radio_mask(dev, R2057_RCAL_CONFIG, ~0x1);

	/* Restore */
	for (i = 0; i < ARRAY_SIZE(phy_to_store_rf); i++)
		b43_phy_write(dev, phy_to_store_rf[i], saved_regs_phy_rf[i]);
	for (i = 0; i < ARRAY_SIZE(phy_to_store); i++)
		b43_phy_write(dev, phy_to_store[i], saved_regs_phy[i]);

	switch (phy->radio_rev) {
	case 0 ... 4:
	case 6:
		b43_radio_maskset(dev, R2057_TEMPSENSE_CONFIG, ~0x3C, tmp);
		b43_radio_maskset(dev, R2057_BANDGAP_RCAL_TRIM, ~0xF0,
				  tmp << 2);
		break;
	case 5:
		b43_radio_mask(dev, R2057_IPA2G_CASCONV_CORE0, ~0x1);
		b43_radio_mask(dev, R2057v7_IQTEST_SEL_PU2, ~0x2);
		break;
	case 9:
		b43_radio_write(dev, R2057_IQTEST_SEL_PU, saved_regs_radio[0]);
		break;
	case 14:
		b43_radio_write(dev, R2057_IQTEST_SEL_PU, saved_regs_radio[0]);
		b43_radio_write(dev, R2057v7_IQTEST_SEL_PU2, saved_regs_radio[1]);
		break;
	}

	return tmp & 0x3e;
}

/* Calibrate the internal RC oscillator?
 * http://bcm-v4.sipsolutions.net/PHY/radio2057_rccal
 */
static u16 b43_radio_2057_rccal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	bool special = (phy->radio_rev == 3 || phy->radio_rev == 4 ||
			phy->radio_rev == 6);
	u16 tmp;

	/* Setup cal */
	if (special) {
		b43_radio_write(dev, R2057_RCCAL_MASTER, 0x61);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0xC0);
	} else {
		b43_radio_write(dev, R2057v7_RCCAL_MASTER, 0x61);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0xE9);
	}
	b43_radio_write(dev, R2057_RCCAL_X1, 0x6E);

	/* Start, wait, stop */
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	if (!b43_radio_wait_value(dev, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000))
		b43dbg(dev->wl, "Radio 0x2057 rccal timeout\n");
	usleep_range(35, 70);
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	usleep_range(70, 140);

	/* Setup cal */
	if (special) {
		b43_radio_write(dev, R2057_RCCAL_MASTER, 0x69);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0xB0);
	} else {
		b43_radio_write(dev, R2057v7_RCCAL_MASTER, 0x69);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0xD5);
	}
	b43_radio_write(dev, R2057_RCCAL_X1, 0x6E);

	/* Start, wait, stop */
	usleep_range(35, 70);
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	usleep_range(70, 140);
	if (!b43_radio_wait_value(dev, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000))
		b43dbg(dev->wl, "Radio 0x2057 rccal timeout\n");
	usleep_range(35, 70);
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	usleep_range(70, 140);

	/* Setup cal */
	if (special) {
		b43_radio_write(dev, R2057_RCCAL_MASTER, 0x73);
		b43_radio_write(dev, R2057_RCCAL_X1, 0x28);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0xB0);
	} else {
		b43_radio_write(dev, R2057v7_RCCAL_MASTER, 0x73);
		b43_radio_write(dev, R2057_RCCAL_X1, 0x6E);
		b43_radio_write(dev, R2057_RCCAL_TRC0, 0x99);
	}

	/* Start, wait, stop */
	usleep_range(35, 70);
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	usleep_range(70, 140);
	if (!b43_radio_wait_value(dev, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000)) {
		b43err(dev->wl, "Radio 0x2057 rcal timeout\n");
		return 0;
	}
	tmp = b43_radio_read(dev, R2057_RCCAL_DONE_OSCCAP);
	usleep_range(35, 70);
	b43_radio_write(dev, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	usleep_range(70, 140);

	if (special)
		b43_radio_mask(dev, R2057_RCCAL_MASTER, ~0x1);
	else
		b43_radio_mask(dev, R2057v7_RCCAL_MASTER, ~0x1);

	return tmp;
}

static void b43_radio_2057_init_pre(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD, ~B43_NPHY_RFCTL_CMD_CHIP0PU);
	/* Maybe wl meant to reset and set (order?) RFCTL_CMD_OEPORFORCE? */
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD, B43_NPHY_RFCTL_CMD_OEPORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD, ~B43_NPHY_RFCTL_CMD_OEPORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD, B43_NPHY_RFCTL_CMD_CHIP0PU);
}

static void b43_radio_2057_init_post(struct b43_wldev *dev)
{
	b43_radio_set(dev, R2057_XTALPUOVR_PINCTRL, 0x1);

	if (0) /* FIXME: Is this BCM43217 specific? */
		b43_radio_set(dev, R2057_XTALPUOVR_PINCTRL, 0x2);

	b43_radio_set(dev, R2057_RFPLL_MISC_CAL_RESETN, 0x78);
	b43_radio_set(dev, R2057_XTAL_CONFIG2, 0x80);
	mdelay(2);
	b43_radio_mask(dev, R2057_RFPLL_MISC_CAL_RESETN, ~0x78);
	b43_radio_mask(dev, R2057_XTAL_CONFIG2, ~0x80);

	if (dev->phy.do_full_init) {
		b43_radio_2057_rcal(dev);
		b43_radio_2057_rccal(dev);
	}
	b43_radio_mask(dev, R2057_RFPLL_MASTER, ~0x8);
}

/* http://bcm-v4.sipsolutions.net/802.11/Radio/2057/Init */
static void b43_radio_2057_init(struct b43_wldev *dev)
{
	b43_radio_2057_init_pre(dev);
	r2057_upload_inittabs(dev);
	b43_radio_2057_init_post(dev);
}

/**************************************************
 * Radio 0x2056
 **************************************************/

static void b43_chantab_radio_2056_upload(struct b43_wldev *dev,
				const struct b43_nphy_channeltab_entry_rev3 *e)
{
	b43_radio_write(dev, B2056_SYN_PLL_VCOCAL1, e->radio_syn_pll_vcocal1);
	b43_radio_write(dev, B2056_SYN_PLL_VCOCAL2, e->radio_syn_pll_vcocal2);
	b43_radio_write(dev, B2056_SYN_PLL_REFDIV, e->radio_syn_pll_refdiv);
	b43_radio_write(dev, B2056_SYN_PLL_MMD2, e->radio_syn_pll_mmd2);
	b43_radio_write(dev, B2056_SYN_PLL_MMD1, e->radio_syn_pll_mmd1);
	b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER1,
					e->radio_syn_pll_loopfilter1);
	b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER2,
					e->radio_syn_pll_loopfilter2);
	b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER3,
					e->radio_syn_pll_loopfilter3);
	b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER4,
					e->radio_syn_pll_loopfilter4);
	b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER5,
					e->radio_syn_pll_loopfilter5);
	b43_radio_write(dev, B2056_SYN_RESERVED_ADDR27,
					e->radio_syn_reserved_addr27);
	b43_radio_write(dev, B2056_SYN_RESERVED_ADDR28,
					e->radio_syn_reserved_addr28);
	b43_radio_write(dev, B2056_SYN_RESERVED_ADDR29,
					e->radio_syn_reserved_addr29);
	b43_radio_write(dev, B2056_SYN_LOGEN_VCOBUF1,
					e->radio_syn_logen_vcobuf1);
	b43_radio_write(dev, B2056_SYN_LOGEN_MIXER2, e->radio_syn_logen_mixer2);
	b43_radio_write(dev, B2056_SYN_LOGEN_BUF3, e->radio_syn_logen_buf3);
	b43_radio_write(dev, B2056_SYN_LOGEN_BUF4, e->radio_syn_logen_buf4);

	b43_radio_write(dev, B2056_RX0 | B2056_RX_LNAA_TUNE,
					e->radio_rx0_lnaa_tune);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_LNAG_TUNE,
					e->radio_rx0_lnag_tune);

	b43_radio_write(dev, B2056_TX0 | B2056_TX_INTPAA_BOOST_TUNE,
					e->radio_tx0_intpaa_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_INTPAG_BOOST_TUNE,
					e->radio_tx0_intpag_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_PADA_BOOST_TUNE,
					e->radio_tx0_pada_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_PADG_BOOST_TUNE,
					e->radio_tx0_padg_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_PGAA_BOOST_TUNE,
					e->radio_tx0_pgaa_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_PGAG_BOOST_TUNE,
					e->radio_tx0_pgag_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_MIXA_BOOST_TUNE,
					e->radio_tx0_mixa_boost_tune);
	b43_radio_write(dev, B2056_TX0 | B2056_TX_MIXG_BOOST_TUNE,
					e->radio_tx0_mixg_boost_tune);

	b43_radio_write(dev, B2056_RX1 | B2056_RX_LNAA_TUNE,
					e->radio_rx1_lnaa_tune);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_LNAG_TUNE,
					e->radio_rx1_lnag_tune);

	b43_radio_write(dev, B2056_TX1 | B2056_TX_INTPAA_BOOST_TUNE,
					e->radio_tx1_intpaa_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_INTPAG_BOOST_TUNE,
					e->radio_tx1_intpag_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_PADA_BOOST_TUNE,
					e->radio_tx1_pada_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_PADG_BOOST_TUNE,
					e->radio_tx1_padg_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_PGAA_BOOST_TUNE,
					e->radio_tx1_pgaa_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_PGAG_BOOST_TUNE,
					e->radio_tx1_pgag_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_MIXA_BOOST_TUNE,
					e->radio_tx1_mixa_boost_tune);
	b43_radio_write(dev, B2056_TX1 | B2056_TX_MIXG_BOOST_TUNE,
					e->radio_tx1_mixg_boost_tune);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Radio/2056Setup */
static void b43_radio_2056_setup(struct b43_wldev *dev,
				const struct b43_nphy_channeltab_entry_rev3 *e)
{
	struct b43_phy *phy = &dev->phy;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	enum ieee80211_band band = b43_current_band(dev->wl);
	u16 offset;
	u8 i;
	u16 bias, cbias;
	u16 pag_boost, padg_boost, pgag_boost, mixg_boost;
	u16 paa_boost, pada_boost, pgaa_boost, mixa_boost;
	bool is_pkg_fab_smic;

	B43_WARN_ON(dev->phy.rev < 3);

	is_pkg_fab_smic =
		((dev->dev->chip_id == BCMA_CHIP_ID_BCM43224 ||
		  dev->dev->chip_id == BCMA_CHIP_ID_BCM43225 ||
		  dev->dev->chip_id == BCMA_CHIP_ID_BCM43421) &&
		 dev->dev->chip_pkg == BCMA_PKG_ID_BCM43224_FAB_SMIC);

	b43_chantab_radio_2056_upload(dev, e);
	b2056_upload_syn_pll_cp2(dev, band == IEEE80211_BAND_5GHZ);

	if (sprom->boardflags2_lo & B43_BFL2_GPLL_WAR &&
	    b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER1, 0x1F);
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER2, 0x1F);
		if (dev->dev->chip_id == BCMA_CHIP_ID_BCM4716 ||
		    dev->dev->chip_id == BCMA_CHIP_ID_BCM47162) {
			b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER4, 0x14);
			b43_radio_write(dev, B2056_SYN_PLL_CP2, 0);
		} else {
			b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER4, 0x0B);
			b43_radio_write(dev, B2056_SYN_PLL_CP2, 0x14);
		}
	}
	if (sprom->boardflags2_hi & B43_BFH2_GPLL_WAR2 &&
	    b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER1, 0x1f);
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER2, 0x1f);
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER4, 0x0b);
		b43_radio_write(dev, B2056_SYN_PLL_CP2, 0x20);
	}
	if (sprom->boardflags2_lo & B43_BFL2_APLL_WAR &&
	    b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER1, 0x1F);
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER2, 0x1F);
		b43_radio_write(dev, B2056_SYN_PLL_LOOPFILTER4, 0x05);
		b43_radio_write(dev, B2056_SYN_PLL_CP2, 0x0C);
	}

	if (dev->phy.n->ipa2g_on && band == IEEE80211_BAND_2GHZ) {
		for (i = 0; i < 2; i++) {
			offset = i ? B2056_TX1 : B2056_TX0;
			if (dev->phy.rev >= 5) {
				b43_radio_write(dev,
					offset | B2056_TX_PADG_IDAC, 0xcc);

				if (dev->dev->chip_id == BCMA_CHIP_ID_BCM4716 ||
				    dev->dev->chip_id == BCMA_CHIP_ID_BCM47162) {
					bias = 0x40;
					cbias = 0x45;
					pag_boost = 0x5;
					pgag_boost = 0x33;
					mixg_boost = 0x55;
				} else {
					bias = 0x25;
					cbias = 0x20;
					if (is_pkg_fab_smic) {
						bias = 0x2a;
						cbias = 0x38;
					}
					pag_boost = 0x4;
					pgag_boost = 0x03;
					mixg_boost = 0x65;
				}
				padg_boost = 0x77;

				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_IMAIN_STAT,
					bias);
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_IAUX_STAT,
					bias);
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_CASCBIAS,
					cbias);
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_BOOST_TUNE,
					pag_boost);
				b43_radio_write(dev,
					offset | B2056_TX_PGAG_BOOST_TUNE,
					pgag_boost);
				b43_radio_write(dev,
					offset | B2056_TX_PADG_BOOST_TUNE,
					padg_boost);
				b43_radio_write(dev,
					offset | B2056_TX_MIXG_BOOST_TUNE,
					mixg_boost);
			} else {
				bias = b43_is_40mhz(dev) ? 0x40 : 0x20;
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_IMAIN_STAT,
					bias);
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_IAUX_STAT,
					bias);
				b43_radio_write(dev,
					offset | B2056_TX_INTPAG_CASCBIAS,
					0x30);
			}
			b43_radio_write(dev, offset | B2056_TX_PA_SPARE1, 0xee);
		}
	} else if (dev->phy.n->ipa5g_on && band == IEEE80211_BAND_5GHZ) {
		u16 freq = phy->chandef->chan->center_freq;
		if (freq < 5100) {
			paa_boost = 0xA;
			pada_boost = 0x77;
			pgaa_boost = 0xF;
			mixa_boost = 0xF;
		} else if (freq < 5340) {
			paa_boost = 0x8;
			pada_boost = 0x77;
			pgaa_boost = 0xFB;
			mixa_boost = 0xF;
		} else if (freq < 5650) {
			paa_boost = 0x0;
			pada_boost = 0x77;
			pgaa_boost = 0xB;
			mixa_boost = 0xF;
		} else {
			paa_boost = 0x0;
			pada_boost = 0x77;
			if (freq != 5825)
				pgaa_boost = -(freq - 18) / 36 + 168;
			else
				pgaa_boost = 6;
			mixa_boost = 0xF;
		}

		cbias = is_pkg_fab_smic ? 0x35 : 0x30;

		for (i = 0; i < 2; i++) {
			offset = i ? B2056_TX1 : B2056_TX0;

			b43_radio_write(dev,
				offset | B2056_TX_INTPAA_BOOST_TUNE, paa_boost);
			b43_radio_write(dev,
				offset | B2056_TX_PADA_BOOST_TUNE, pada_boost);
			b43_radio_write(dev,
				offset | B2056_TX_PGAA_BOOST_TUNE, pgaa_boost);
			b43_radio_write(dev,
				offset | B2056_TX_MIXA_BOOST_TUNE, mixa_boost);
			b43_radio_write(dev,
				offset | B2056_TX_TXSPARE1, 0x30);
			b43_radio_write(dev,
				offset | B2056_TX_PA_SPARE2, 0xee);
			b43_radio_write(dev,
				offset | B2056_TX_PADA_CASCBIAS, 0x03);
			b43_radio_write(dev,
				offset | B2056_TX_INTPAA_IAUX_STAT, 0x30);
			b43_radio_write(dev,
				offset | B2056_TX_INTPAA_IMAIN_STAT, 0x30);
			b43_radio_write(dev,
				offset | B2056_TX_INTPAA_CASCBIAS, cbias);
		}
	}

	udelay(50);
	/* VCO calibration */
	b43_radio_write(dev, B2056_SYN_PLL_VCOCAL12, 0x00);
	b43_radio_write(dev, B2056_TX_INTPAA_PA_MISC, 0x38);
	b43_radio_write(dev, B2056_TX_INTPAA_PA_MISC, 0x18);
	b43_radio_write(dev, B2056_TX_INTPAA_PA_MISC, 0x38);
	b43_radio_write(dev, B2056_TX_INTPAA_PA_MISC, 0x39);
	udelay(300);
}

static u8 b43_radio_2056_rcal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	u16 mast2, tmp;

	if (phy->rev != 3)
		return 0;

	mast2 = b43_radio_read(dev, B2056_SYN_PLL_MAST2);
	b43_radio_write(dev, B2056_SYN_PLL_MAST2, mast2 | 0x7);

	udelay(10);
	b43_radio_write(dev, B2056_SYN_RCAL_MASTER, 0x01);
	udelay(10);
	b43_radio_write(dev, B2056_SYN_RCAL_MASTER, 0x09);

	if (!b43_radio_wait_value(dev, B2056_SYN_RCAL_CODE_OUT, 0x80, 0x80, 100,
				  1000000)) {
		b43err(dev->wl, "Radio recalibration timeout\n");
		return 0;
	}

	b43_radio_write(dev, B2056_SYN_RCAL_MASTER, 0x01);
	tmp = b43_radio_read(dev, B2056_SYN_RCAL_CODE_OUT);
	b43_radio_write(dev, B2056_SYN_RCAL_MASTER, 0x00);

	b43_radio_write(dev, B2056_SYN_PLL_MAST2, mast2);

	return tmp & 0x1f;
}

static void b43_radio_init2056_pre(struct b43_wldev *dev)
{
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
		     ~B43_NPHY_RFCTL_CMD_CHIP0PU);
	/* Maybe wl meant to reset and set (order?) RFCTL_CMD_OEPORFORCE? */
	b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
		     B43_NPHY_RFCTL_CMD_OEPORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
		    ~B43_NPHY_RFCTL_CMD_OEPORFORCE);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
		    B43_NPHY_RFCTL_CMD_CHIP0PU);
}

static void b43_radio_init2056_post(struct b43_wldev *dev)
{
	b43_radio_set(dev, B2056_SYN_COM_CTRL, 0xB);
	b43_radio_set(dev, B2056_SYN_COM_PU, 0x2);
	b43_radio_set(dev, B2056_SYN_COM_RESET, 0x2);
	msleep(1);
	b43_radio_mask(dev, B2056_SYN_COM_RESET, ~0x2);
	b43_radio_mask(dev, B2056_SYN_PLL_MAST2, ~0xFC);
	b43_radio_mask(dev, B2056_SYN_RCCAL_CTRL0, ~0x1);
	if (dev->phy.do_full_init)
		b43_radio_2056_rcal(dev);
}

/*
 * Initialize a Broadcom 2056 N-radio
 * http://bcm-v4.sipsolutions.net/802.11/Radio/2056/Init
 */
static void b43_radio_init2056(struct b43_wldev *dev)
{
	b43_radio_init2056_pre(dev);
	b2056_upload_inittabs(dev, 0, 0);
	b43_radio_init2056_post(dev);
}

/**************************************************
 * Radio 0x2055
 **************************************************/

static void b43_chantab_radio_upload(struct b43_wldev *dev,
				const struct b43_nphy_channeltab_entry_rev2 *e)
{
	b43_radio_write(dev, B2055_PLL_REF, e->radio_pll_ref);
	b43_radio_write(dev, B2055_RF_PLLMOD0, e->radio_rf_pllmod0);
	b43_radio_write(dev, B2055_RF_PLLMOD1, e->radio_rf_pllmod1);
	b43_radio_write(dev, B2055_VCO_CAPTAIL, e->radio_vco_captail);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */

	b43_radio_write(dev, B2055_VCO_CAL1, e->radio_vco_cal1);
	b43_radio_write(dev, B2055_VCO_CAL2, e->radio_vco_cal2);
	b43_radio_write(dev, B2055_PLL_LFC1, e->radio_pll_lfc1);
	b43_radio_write(dev, B2055_PLL_LFR1, e->radio_pll_lfr1);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */

	b43_radio_write(dev, B2055_PLL_LFC2, e->radio_pll_lfc2);
	b43_radio_write(dev, B2055_LGBUF_CENBUF, e->radio_lgbuf_cenbuf);
	b43_radio_write(dev, B2055_LGEN_TUNE1, e->radio_lgen_tune1);
	b43_radio_write(dev, B2055_LGEN_TUNE2, e->radio_lgen_tune2);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */

	b43_radio_write(dev, B2055_C1_LGBUF_ATUNE, e->radio_c1_lgbuf_atune);
	b43_radio_write(dev, B2055_C1_LGBUF_GTUNE, e->radio_c1_lgbuf_gtune);
	b43_radio_write(dev, B2055_C1_RX_RFR1, e->radio_c1_rx_rfr1);
	b43_radio_write(dev, B2055_C1_TX_PGAPADTN, e->radio_c1_tx_pgapadtn);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */

	b43_radio_write(dev, B2055_C1_TX_MXBGTRIM, e->radio_c1_tx_mxbgtrim);
	b43_radio_write(dev, B2055_C2_LGBUF_ATUNE, e->radio_c2_lgbuf_atune);
	b43_radio_write(dev, B2055_C2_LGBUF_GTUNE, e->radio_c2_lgbuf_gtune);
	b43_radio_write(dev, B2055_C2_RX_RFR1, e->radio_c2_rx_rfr1);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */

	b43_radio_write(dev, B2055_C2_TX_PGAPADTN, e->radio_c2_tx_pgapadtn);
	b43_radio_write(dev, B2055_C2_TX_MXBGTRIM, e->radio_c2_tx_mxbgtrim);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Radio/2055Setup */
static void b43_radio_2055_setup(struct b43_wldev *dev,
				const struct b43_nphy_channeltab_entry_rev2 *e)
{
	B43_WARN_ON(dev->phy.rev >= 3);

	b43_chantab_radio_upload(dev, e);
	udelay(50);
	b43_radio_write(dev, B2055_VCO_CAL10, 0x05);
	b43_radio_write(dev, B2055_VCO_CAL10, 0x45);
	b43_read32(dev, B43_MMIO_MACCTL); /* flush writes */
	b43_radio_write(dev, B2055_VCO_CAL10, 0x65);
	udelay(300);
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
	struct b43_phy_n *nphy = dev->phy.n;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	bool workaround = false;

	if (sprom->revision < 4)
		workaround = (dev->dev->board_vendor != PCI_VENDOR_ID_BROADCOM
			      && dev->dev->board_type == SSB_BOARD_CB2_4321
			      && dev->dev->board_rev >= 0x41);
	else
		workaround =
			!(sprom->boardflags2_lo & B43_BFL2_RXBB_INT_REG_DIS);

	b43_radio_mask(dev, B2055_MASTER1, 0xFFF3);
	if (workaround) {
		b43_radio_mask(dev, B2055_C1_RX_BB_REG, 0x7F);
		b43_radio_mask(dev, B2055_C2_RX_BB_REG, 0x7F);
	}
	b43_radio_maskset(dev, B2055_RRCCAL_NOPTSEL, 0xFFC0, 0x2C);
	b43_radio_write(dev, B2055_CAL_MISC, 0x3C);
	b43_radio_mask(dev, B2055_CAL_MISC, 0xFFBE);
	b43_radio_set(dev, B2055_CAL_LPOCTL, 0x80);
	b43_radio_set(dev, B2055_CAL_MISC, 0x1);
	msleep(1);
	b43_radio_set(dev, B2055_CAL_MISC, 0x40);
	if (!b43_radio_wait_value(dev, B2055_CAL_COUT2, 0x80, 0x80, 10, 2000))
		b43err(dev->wl, "radio post init timeout\n");
	b43_radio_mask(dev, B2055_CAL_LPOCTL, 0xFF7F);
	b43_switch_channel(dev, dev->phy.channel);
	b43_radio_write(dev, B2055_C1_RX_BB_LPF, 0x9);
	b43_radio_write(dev, B2055_C2_RX_BB_LPF, 0x9);
	b43_radio_write(dev, B2055_C1_RX_BB_MIDACHP, 0x83);
	b43_radio_write(dev, B2055_C2_RX_BB_MIDACHP, 0x83);
	b43_radio_maskset(dev, B2055_C1_LNA_GAINBST, 0xFFF8, 0x6);
	b43_radio_maskset(dev, B2055_C2_LNA_GAINBST, 0xFFF8, 0x6);
	if (!nphy->gain_boost) {
		b43_radio_set(dev, B2055_C1_RX_RFSPC1, 0x2);
		b43_radio_set(dev, B2055_C2_RX_RFSPC1, 0x2);
	} else {
		b43_radio_mask(dev, B2055_C1_RX_RFSPC1, 0xFFFD);
		b43_radio_mask(dev, B2055_C2_RX_RFSPC1, 0xFFFD);
	}
	udelay(2);
}

/*
 * Initialize a Broadcom 2055 N-radio
 * http://bcm-v4.sipsolutions.net/802.11/Radio/2055/Init
 */
static void b43_radio_init2055(struct b43_wldev *dev)
{
	b43_radio_init2055_pre(dev);
	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		/* Follow wl, not specs. Do not force uploading all regs */
		b2055_upload_inittab(dev, 0, 0);
	} else {
		bool ghz5 = b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ;
		b2055_upload_inittab(dev, ghz5, 0);
	}
	b43_radio_init2055_post(dev);
}

/**************************************************
 * Samples
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/LoadSampleTable */
static int b43_nphy_load_samples(struct b43_wldev *dev,
					struct b43_c32 *samples, u16 len) {
	struct b43_phy_n *nphy = dev->phy.n;
	u16 i;
	u32 *data;

	data = kzalloc(len * sizeof(u32), GFP_KERNEL);
	if (!data) {
		b43err(dev->wl, "allocation for samples loading failed\n");
		return -ENOMEM;
	}
	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	for (i = 0; i < len; i++) {
		data[i] = (samples[i].i & 0x3FF << 10);
		data[i] |= samples[i].q & 0x3FF;
	}
	b43_ntab_write_bulk(dev, B43_NTAB32(17, 0), len, data);

	kfree(data);
	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
	return 0;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GenLoadSamples */
static u16 b43_nphy_gen_load_samples(struct b43_wldev *dev, u32 freq, u16 max,
					bool test)
{
	int i;
	u16 bw, len, rot, angle;
	struct b43_c32 *samples;

	bw = b43_is_40mhz(dev) ? 40 : 20;
	len = bw << 3;

	if (test) {
		if (b43_phy_read(dev, B43_NPHY_BBCFG) & B43_NPHY_BBCFG_RSTRX)
			bw = 82;
		else
			bw = 80;

		if (b43_is_40mhz(dev))
			bw <<= 1;

		len = bw << 1;
	}

	samples = kcalloc(len, sizeof(struct b43_c32), GFP_KERNEL);
	if (!samples) {
		b43err(dev->wl, "allocation for samples generation failed\n");
		return 0;
	}
	rot = (((freq * 36) / bw) << 16) / 100;
	angle = 0;

	for (i = 0; i < len; i++) {
		samples[i] = b43_cordic(angle);
		angle += rot;
		samples[i].q = CORDIC_CONVERT(samples[i].q * max);
		samples[i].i = CORDIC_CONVERT(samples[i].i * max);
	}

	i = b43_nphy_load_samples(dev, samples, len);
	kfree(samples);
	return (i < 0) ? 0 : len;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RunSamples */
static void b43_nphy_run_samples(struct b43_wldev *dev, u16 samps, u16 loops,
				 u16 wait, bool iqmode, bool dac_test,
				 bool modify_bbmult)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	int i;
	u16 seq_mode;
	u32 tmp;

	b43_nphy_stay_in_carrier_search(dev, true);

	if (phy->rev >= 7) {
		bool lpf_bw3, lpf_bw4;

		lpf_bw3 = b43_phy_read(dev, B43_NPHY_REV7_RF_CTL_OVER3) & 0x80;
		lpf_bw4 = b43_phy_read(dev, B43_NPHY_REV7_RF_CTL_OVER3) & 0x80;

		if (lpf_bw3 || lpf_bw4) {
			/* TODO */
		} else {
			u16 value = b43_nphy_read_lpf_ctl(dev, 0);
			if (phy->rev >= 19)
				b43_nphy_rf_ctl_override_rev19(dev, 0x80, value,
							       0, false, 1);
			else
				b43_nphy_rf_ctl_override_rev7(dev, 0x80, value,
							      0, false, 1);
			nphy->lpf_bw_overrode_for_sample_play = true;
		}
	}

	if ((nphy->bb_mult_save & 0x80000000) == 0) {
		tmp = b43_ntab_read(dev, B43_NTAB16(15, 87));
		nphy->bb_mult_save = (tmp & 0xFFFF) | 0x80000000;
	}

	if (modify_bbmult) {
		tmp = !b43_is_40mhz(dev) ? 0x6464 : 0x4747;
		b43_ntab_write(dev, B43_NTAB16(15, 87), tmp);
	}

	b43_phy_write(dev, B43_NPHY_SAMP_DEPCNT, (samps - 1));

	if (loops != 0xFFFF)
		b43_phy_write(dev, B43_NPHY_SAMP_LOOPCNT, (loops - 1));
	else
		b43_phy_write(dev, B43_NPHY_SAMP_LOOPCNT, loops);

	b43_phy_write(dev, B43_NPHY_SAMP_WAITCNT, wait);

	seq_mode = b43_phy_read(dev, B43_NPHY_RFSEQMODE);

	b43_phy_set(dev, B43_NPHY_RFSEQMODE, B43_NPHY_RFSEQMODE_CAOVER);
	if (iqmode) {
		b43_phy_mask(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0x7FFF);
		b43_phy_set(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0x8000);
	} else {
		tmp = dac_test ? 5 : 1;
		b43_phy_write(dev, B43_NPHY_SAMP_CMD, tmp);
	}
	for (i = 0; i < 100; i++) {
		if (!(b43_phy_read(dev, B43_NPHY_RFSEQST) & 1)) {
			i = 0;
			break;
		}
		udelay(10);
	}
	if (i)
		b43err(dev->wl, "run samples timeout\n");

	b43_phy_write(dev, B43_NPHY_RFSEQMODE, seq_mode);

	b43_nphy_stay_in_carrier_search(dev, false);
}

/**************************************************
 * RSSI
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ScaleOffsetRssi */
static void b43_nphy_scale_offset_rssi(struct b43_wldev *dev, u16 scale,
					s8 offset, u8 core,
					enum n_rail_type rail,
					enum n_rssi_type rssi_type)
{
	u16 tmp;
	bool core1or5 = (core == 1) || (core == 5);
	bool core2or5 = (core == 2) || (core == 5);

	offset = clamp_val(offset, -32, 31);
	tmp = ((scale & 0x3F) << 8) | (offset & 0x3F);

	switch (rssi_type) {
	case N_RSSI_NB:
		if (core1or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Z, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Z, tmp);
		if (core2or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Z, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Z, tmp);
		break;
	case N_RSSI_W1:
		if (core1or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_X, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_X, tmp);
		if (core2or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_X, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_X, tmp);
		break;
	case N_RSSI_W2:
		if (core1or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_RSSI_Y, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_RSSI_Y, tmp);
		if (core2or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_RSSI_Y, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_RSSI_Y, tmp);
		break;
	case N_RSSI_TBD:
		if (core1or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_TBD, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_TBD, tmp);
		if (core2or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_TBD, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_TBD, tmp);
		break;
	case N_RSSI_IQ:
		if (core1or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_PWRDET, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_PWRDET, tmp);
		if (core2or5 && rail == N_RAIL_I)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_PWRDET, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_PWRDET, tmp);
		break;
	case N_RSSI_TSSI_2G:
		if (core1or5)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0I_TSSI, tmp);
		if (core2or5)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1I_TSSI, tmp);
		break;
	case N_RSSI_TSSI_5G:
		if (core1or5)
			b43_phy_write(dev, B43_NPHY_RSSIMC_0Q_TSSI, tmp);
		if (core2or5)
			b43_phy_write(dev, B43_NPHY_RSSIMC_1Q_TSSI, tmp);
		break;
	}
}

static void b43_nphy_rssi_select_rev19(struct b43_wldev *dev, u8 code,
				       enum n_rssi_type rssi_type)
{
	/* TODO */
}

static void b43_nphy_rev3_rssi_select(struct b43_wldev *dev, u8 code,
				      enum n_rssi_type rssi_type)
{
	u8 i;
	u16 reg, val;

	if (code == 0) {
		b43_phy_mask(dev, B43_NPHY_AFECTL_OVER1, 0xFDFF);
		b43_phy_mask(dev, B43_NPHY_AFECTL_OVER, 0xFDFF);
		b43_phy_mask(dev, B43_NPHY_AFECTL_C1, 0xFCFF);
		b43_phy_mask(dev, B43_NPHY_AFECTL_C2, 0xFCFF);
		b43_phy_mask(dev, B43_NPHY_TXF_40CO_B1S0, 0xFFDF);
		b43_phy_mask(dev, B43_NPHY_TXF_40CO_B32S1, 0xFFDF);
		b43_phy_mask(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1, 0xFFC3);
		b43_phy_mask(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2, 0xFFC3);
	} else {
		for (i = 0; i < 2; i++) {
			if ((code == 1 && i == 1) || (code == 2 && !i))
				continue;

			reg = (i == 0) ?
				B43_NPHY_AFECTL_OVER1 : B43_NPHY_AFECTL_OVER;
			b43_phy_maskset(dev, reg, 0xFDFF, 0x0200);

			if (rssi_type == N_RSSI_W1 ||
			    rssi_type == N_RSSI_W2 ||
			    rssi_type == N_RSSI_NB) {
				reg = (i == 0) ?
					B43_NPHY_AFECTL_C1 :
					B43_NPHY_AFECTL_C2;
				b43_phy_maskset(dev, reg, 0xFCFF, 0);

				reg = (i == 0) ?
					B43_NPHY_RFCTL_LUT_TRSW_UP1 :
					B43_NPHY_RFCTL_LUT_TRSW_UP2;
				b43_phy_maskset(dev, reg, 0xFFC3, 0);

				if (rssi_type == N_RSSI_W1)
					val = (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) ? 4 : 8;
				else if (rssi_type == N_RSSI_W2)
					val = 16;
				else
					val = 32;
				b43_phy_set(dev, reg, val);

				reg = (i == 0) ?
					B43_NPHY_TXF_40CO_B1S0 :
					B43_NPHY_TXF_40CO_B32S1;
				b43_phy_set(dev, reg, 0x0020);
			} else {
				if (rssi_type == N_RSSI_TBD)
					val = 0x0100;
				else if (rssi_type == N_RSSI_IQ)
					val = 0x0200;
				else
					val = 0x0300;

				reg = (i == 0) ?
					B43_NPHY_AFECTL_C1 :
					B43_NPHY_AFECTL_C2;

				b43_phy_maskset(dev, reg, 0xFCFF, val);
				b43_phy_maskset(dev, reg, 0xF3FF, val << 2);

				if (rssi_type != N_RSSI_IQ &&
				    rssi_type != N_RSSI_TBD) {
					enum ieee80211_band band =
						b43_current_band(dev->wl);

					if (dev->phy.rev < 7) {
						if (b43_nphy_ipa(dev))
							val = (band == IEEE80211_BAND_5GHZ) ? 0xC : 0xE;
						else
							val = 0x11;
						reg = (i == 0) ? B2056_TX0 : B2056_TX1;
						reg |= B2056_TX_TX_SSI_MUX;
						b43_radio_write(dev, reg, val);
					}

					reg = (i == 0) ?
						B43_NPHY_AFECTL_OVER1 :
						B43_NPHY_AFECTL_OVER;
					b43_phy_set(dev, reg, 0x0200);
				}
			}
		}
	}
}

static void b43_nphy_rev2_rssi_select(struct b43_wldev *dev, u8 code,
				      enum n_rssi_type rssi_type)
{
	u16 val;
	bool rssi_w1_w2_nb = false;

	switch (rssi_type) {
	case N_RSSI_W1:
	case N_RSSI_W2:
	case N_RSSI_NB:
		val = 0;
		rssi_w1_w2_nb = true;
		break;
	case N_RSSI_TBD:
		val = 1;
		break;
	case N_RSSI_IQ:
		val = 2;
		break;
	default:
		val = 3;
	}

	val = (val << 12) | (val << 14);
	b43_phy_maskset(dev, B43_NPHY_AFECTL_C1, 0x0FFF, val);
	b43_phy_maskset(dev, B43_NPHY_AFECTL_C2, 0x0FFF, val);

	if (rssi_w1_w2_nb) {
		b43_phy_maskset(dev, B43_NPHY_RFCTL_RSSIO1, 0xFFCF,
				(rssi_type + 1) << 4);
		b43_phy_maskset(dev, B43_NPHY_RFCTL_RSSIO2, 0xFFCF,
				(rssi_type + 1) << 4);
	}

	if (code == 0) {
		b43_phy_mask(dev, B43_NPHY_AFECTL_OVER, ~0x3000);
		if (rssi_w1_w2_nb) {
			b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
				~(B43_NPHY_RFCTL_CMD_RXEN |
				  B43_NPHY_RFCTL_CMD_CORESEL));
			b43_phy_mask(dev, B43_NPHY_RFCTL_OVER,
				~(0x1 << 12 |
				  0x1 << 5 |
				  0x1 << 1 |
				  0x1));
			b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
				~B43_NPHY_RFCTL_CMD_START);
			udelay(20);
			b43_phy_mask(dev, B43_NPHY_RFCTL_OVER, ~0x1);
		}
	} else {
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x3000);
		if (rssi_w1_w2_nb) {
			b43_phy_maskset(dev, B43_NPHY_RFCTL_CMD,
				~(B43_NPHY_RFCTL_CMD_RXEN |
				  B43_NPHY_RFCTL_CMD_CORESEL),
				(B43_NPHY_RFCTL_CMD_RXEN |
				 code << B43_NPHY_RFCTL_CMD_CORESEL_SHIFT));
			b43_phy_set(dev, B43_NPHY_RFCTL_OVER,
				(0x1 << 12 |
				  0x1 << 5 |
				  0x1 << 1 |
				  0x1));
			b43_phy_set(dev, B43_NPHY_RFCTL_CMD,
				B43_NPHY_RFCTL_CMD_START);
			udelay(20);
			b43_phy_mask(dev, B43_NPHY_RFCTL_OVER, ~0x1);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSISel */
static void b43_nphy_rssi_select(struct b43_wldev *dev, u8 code,
				 enum n_rssi_type type)
{
	if (dev->phy.rev >= 19)
		b43_nphy_rssi_select_rev19(dev, code, type);
	else if (dev->phy.rev >= 3)
		b43_nphy_rev3_rssi_select(dev, code, type);
	else
		b43_nphy_rev2_rssi_select(dev, code, type);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetRssi2055Vcm */
static void b43_nphy_set_rssi_2055_vcm(struct b43_wldev *dev,
				       enum n_rssi_type rssi_type, u8 *buf)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (rssi_type == N_RSSI_NB) {
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
static int b43_nphy_poll_rssi(struct b43_wldev *dev, enum n_rssi_type rssi_type,
			      s32 *buf, u8 nsamp)
{
	int i;
	int out;
	u16 save_regs_phy[9];
	u16 s[2];

	/* TODO: rev7+ is treated like rev3+, what about rev19+? */

	if (dev->phy.rev >= 3) {
		save_regs_phy[0] = b43_phy_read(dev, B43_NPHY_AFECTL_C1);
		save_regs_phy[1] = b43_phy_read(dev, B43_NPHY_AFECTL_C2);
		save_regs_phy[2] = b43_phy_read(dev,
						B43_NPHY_RFCTL_LUT_TRSW_UP1);
		save_regs_phy[3] = b43_phy_read(dev,
						B43_NPHY_RFCTL_LUT_TRSW_UP2);
		save_regs_phy[4] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER1);
		save_regs_phy[5] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		save_regs_phy[6] = b43_phy_read(dev, B43_NPHY_TXF_40CO_B1S0);
		save_regs_phy[7] = b43_phy_read(dev, B43_NPHY_TXF_40CO_B32S1);
		save_regs_phy[8] = 0;
	} else {
		save_regs_phy[0] = b43_phy_read(dev, B43_NPHY_AFECTL_C1);
		save_regs_phy[1] = b43_phy_read(dev, B43_NPHY_AFECTL_C2);
		save_regs_phy[2] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		save_regs_phy[3] = b43_phy_read(dev, B43_NPHY_RFCTL_CMD);
		save_regs_phy[4] = b43_phy_read(dev, B43_NPHY_RFCTL_OVER);
		save_regs_phy[5] = b43_phy_read(dev, B43_NPHY_RFCTL_RSSIO1);
		save_regs_phy[6] = b43_phy_read(dev, B43_NPHY_RFCTL_RSSIO2);
		save_regs_phy[7] = 0;
		save_regs_phy[8] = 0;
	}

	b43_nphy_rssi_select(dev, 5, rssi_type);

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
		b43_phy_write(dev, B43_NPHY_AFECTL_C1, save_regs_phy[0]);
		b43_phy_write(dev, B43_NPHY_AFECTL_C2, save_regs_phy[1]);
		b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1,
				save_regs_phy[2]);
		b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2,
				save_regs_phy[3]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, save_regs_phy[4]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, save_regs_phy[5]);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S0, save_regs_phy[6]);
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B32S1, save_regs_phy[7]);
	} else {
		b43_phy_write(dev, B43_NPHY_AFECTL_C1, save_regs_phy[0]);
		b43_phy_write(dev, B43_NPHY_AFECTL_C2, save_regs_phy[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, save_regs_phy[2]);
		b43_phy_write(dev, B43_NPHY_RFCTL_CMD, save_regs_phy[3]);
		b43_phy_write(dev, B43_NPHY_RFCTL_OVER, save_regs_phy[4]);
		b43_phy_write(dev, B43_NPHY_RFCTL_RSSIO1, save_regs_phy[5]);
		b43_phy_write(dev, B43_NPHY_RFCTL_RSSIO2, save_regs_phy[6]);
	}

	return out;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICalRev3 */
static void b43_nphy_rev3_rssi_cal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;

	u16 saved_regs_phy_rfctl[2];
	u16 saved_regs_phy[22];
	u16 regs_to_store_rev3[] = {
		B43_NPHY_AFECTL_OVER1, B43_NPHY_AFECTL_OVER,
		B43_NPHY_AFECTL_C1, B43_NPHY_AFECTL_C2,
		B43_NPHY_TXF_40CO_B1S1, B43_NPHY_RFCTL_OVER,
		B43_NPHY_TXF_40CO_B1S0, B43_NPHY_TXF_40CO_B32S1,
		B43_NPHY_RFCTL_CMD,
		B43_NPHY_RFCTL_LUT_TRSW_UP1, B43_NPHY_RFCTL_LUT_TRSW_UP2,
		B43_NPHY_RFCTL_RSSIO1, B43_NPHY_RFCTL_RSSIO2
	};
	u16 regs_to_store_rev7[] = {
		B43_NPHY_AFECTL_OVER1, B43_NPHY_AFECTL_OVER,
		B43_NPHY_AFECTL_C1, B43_NPHY_AFECTL_C2,
		B43_NPHY_TXF_40CO_B1S1, B43_NPHY_RFCTL_OVER,
		B43_NPHY_REV7_RF_CTL_OVER3, B43_NPHY_REV7_RF_CTL_OVER4,
		B43_NPHY_REV7_RF_CTL_OVER5, B43_NPHY_REV7_RF_CTL_OVER6,
		0x2ff,
		B43_NPHY_TXF_40CO_B1S0, B43_NPHY_TXF_40CO_B32S1,
		B43_NPHY_RFCTL_CMD,
		B43_NPHY_RFCTL_LUT_TRSW_UP1, B43_NPHY_RFCTL_LUT_TRSW_UP2,
		B43_NPHY_REV7_RF_CTL_MISC_REG3, B43_NPHY_REV7_RF_CTL_MISC_REG4,
		B43_NPHY_REV7_RF_CTL_MISC_REG5, B43_NPHY_REV7_RF_CTL_MISC_REG6,
		B43_NPHY_RFCTL_RSSIO1, B43_NPHY_RFCTL_RSSIO2
	};
	u16 *regs_to_store;
	int regs_amount;

	u16 class;

	u16 clip_state[2];
	u16 clip_off[2] = { 0xFFFF, 0xFFFF };

	u8 vcm_final = 0;
	s32 offset[4];
	s32 results[8][4] = { };
	s32 results_min[4] = { };
	s32 poll_results[4] = { };

	u16 *rssical_radio_regs = NULL;
	u16 *rssical_phy_regs = NULL;

	u16 r; /* routing */
	u8 rx_core_state;
	int core, i, j, vcm;

	if (dev->phy.rev >= 7) {
		regs_to_store = regs_to_store_rev7;
		regs_amount = ARRAY_SIZE(regs_to_store_rev7);
	} else {
		regs_to_store = regs_to_store_rev3;
		regs_amount = ARRAY_SIZE(regs_to_store_rev3);
	}
	BUG_ON(regs_amount > ARRAY_SIZE(saved_regs_phy));

	class = b43_nphy_classifier(dev, 0, 0);
	b43_nphy_classifier(dev, 7, 4);
	b43_nphy_read_clip_detection(dev, clip_state);
	b43_nphy_write_clip_detection(dev, clip_off);

	saved_regs_phy_rfctl[0] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC1);
	saved_regs_phy_rfctl[1] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);
	for (i = 0; i < regs_amount; i++)
		saved_regs_phy[i] = b43_phy_read(dev, regs_to_store[i]);

	b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_OFF, 0, 7);
	b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_TRSW, 1, 7);

	if (dev->phy.rev >= 7) {
		b43_nphy_rf_ctl_override_one_to_many(dev,
						     N_RF_CTL_OVER_CMD_RXRF_PU,
						     0, 0, false);
		b43_nphy_rf_ctl_override_one_to_many(dev,
						     N_RF_CTL_OVER_CMD_RX_PU,
						     1, 0, false);
		b43_nphy_rf_ctl_override_rev7(dev, 0x80, 1, 0, false, 0);
		b43_nphy_rf_ctl_override_rev7(dev, 0x80, 1, 0, false, 0);
		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			b43_nphy_rf_ctl_override_rev7(dev, 0x20, 0, 0, false,
						      0);
			b43_nphy_rf_ctl_override_rev7(dev, 0x10, 1, 0, false,
						      0);
		} else {
			b43_nphy_rf_ctl_override_rev7(dev, 0x10, 0, 0, false,
						      0);
			b43_nphy_rf_ctl_override_rev7(dev, 0x20, 1, 0, false,
						      0);
		}
	} else {
		b43_nphy_rf_ctl_override(dev, 0x1, 0, 0, false);
		b43_nphy_rf_ctl_override(dev, 0x2, 1, 0, false);
		b43_nphy_rf_ctl_override(dev, 0x80, 1, 0, false);
		b43_nphy_rf_ctl_override(dev, 0x40, 1, 0, false);
		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			b43_nphy_rf_ctl_override(dev, 0x20, 0, 0, false);
			b43_nphy_rf_ctl_override(dev, 0x10, 1, 0, false);
		} else {
			b43_nphy_rf_ctl_override(dev, 0x10, 0, 0, false);
			b43_nphy_rf_ctl_override(dev, 0x20, 1, 0, false);
		}
	}

	rx_core_state = b43_nphy_get_rx_core_state(dev);
	for (core = 0; core < 2; core++) {
		if (!(rx_core_state & (1 << core)))
			continue;
		r = core ? B2056_RX1 : B2056_RX0;
		b43_nphy_scale_offset_rssi(dev, 0, 0, core + 1, N_RAIL_I,
					   N_RSSI_NB);
		b43_nphy_scale_offset_rssi(dev, 0, 0, core + 1, N_RAIL_Q,
					   N_RSSI_NB);

		/* Grab RSSI results for every possible VCM */
		for (vcm = 0; vcm < 8; vcm++) {
			if (dev->phy.rev >= 7)
				b43_radio_maskset(dev,
						  core ? R2057_NB_MASTER_CORE1 :
							 R2057_NB_MASTER_CORE0,
						  ~R2057_VCM_MASK, vcm);
			else
				b43_radio_maskset(dev, r | B2056_RX_RSSI_MISC,
						  0xE3, vcm << 2);
			b43_nphy_poll_rssi(dev, N_RSSI_NB, results[vcm], 8);
		}

		/* Find out which VCM got the best results */
		for (i = 0; i < 4; i += 2) {
			s32 currd;
			s32 mind = 0x100000;
			s32 minpoll = 249;
			u8 minvcm = 0;
			if (2 * core != i)
				continue;
			for (vcm = 0; vcm < 8; vcm++) {
				currd = results[vcm][i] * results[vcm][i] +
					results[vcm][i + 1] * results[vcm][i];
				if (currd < mind) {
					mind = currd;
					minvcm = vcm;
				}
				if (results[vcm][i] < minpoll)
					minpoll = results[vcm][i];
			}
			vcm_final = minvcm;
			results_min[i] = minpoll;
		}

		/* Select the best VCM */
		if (dev->phy.rev >= 7)
			b43_radio_maskset(dev,
					  core ? R2057_NB_MASTER_CORE1 :
						 R2057_NB_MASTER_CORE0,
					  ~R2057_VCM_MASK, vcm);
		else
			b43_radio_maskset(dev, r | B2056_RX_RSSI_MISC,
					  0xE3, vcm_final << 2);

		for (i = 0; i < 4; i++) {
			if (core != i / 2)
				continue;
			offset[i] = -results[vcm_final][i];
			if (offset[i] < 0)
				offset[i] = -((abs(offset[i]) + 4) / 8);
			else
				offset[i] = (offset[i] + 4) / 8;
			if (results_min[i] == 248)
				offset[i] = -32;
			b43_nphy_scale_offset_rssi(dev, 0, offset[i],
						   (i / 2 == 0) ? 1 : 2,
						   (i % 2 == 0) ? N_RAIL_I : N_RAIL_Q,
						   N_RSSI_NB);
		}
	}

	for (core = 0; core < 2; core++) {
		if (!(rx_core_state & (1 << core)))
			continue;
		for (i = 0; i < 2; i++) {
			b43_nphy_scale_offset_rssi(dev, 0, 0, core + 1,
						   N_RAIL_I, i);
			b43_nphy_scale_offset_rssi(dev, 0, 0, core + 1,
						   N_RAIL_Q, i);
			b43_nphy_poll_rssi(dev, i, poll_results, 8);
			for (j = 0; j < 4; j++) {
				if (j / 2 == core) {
					offset[j] = 232 - poll_results[j];
					if (offset[j] < 0)
						offset[j] = -(abs(offset[j] + 4) / 8);
					else
						offset[j] = (offset[j] + 4) / 8;
					b43_nphy_scale_offset_rssi(dev, 0,
						offset[2 * core], core + 1, j % 2, i);
				}
			}
		}
	}

	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, saved_regs_phy_rfctl[0]);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, saved_regs_phy_rfctl[1]);

	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);

	b43_phy_set(dev, B43_NPHY_TXF_40CO_B1S1, 0x1);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD, B43_NPHY_RFCTL_CMD_START);
	b43_phy_mask(dev, B43_NPHY_TXF_40CO_B1S1, ~0x1);

	b43_phy_set(dev, B43_NPHY_RFCTL_OVER, 0x1);
	b43_phy_set(dev, B43_NPHY_RFCTL_CMD, B43_NPHY_RFCTL_CMD_RXTX);
	b43_phy_mask(dev, B43_NPHY_RFCTL_OVER, ~0x1);

	for (i = 0; i < regs_amount; i++)
		b43_phy_write(dev, regs_to_store[i], saved_regs_phy[i]);

	/* Store for future configuration */
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_2G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_2G;
	} else {
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_5G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_5G;
	}
	if (dev->phy.rev >= 7) {
		rssical_radio_regs[0] = b43_radio_read(dev,
						       R2057_NB_MASTER_CORE0);
		rssical_radio_regs[1] = b43_radio_read(dev,
						       R2057_NB_MASTER_CORE1);
	} else {
		rssical_radio_regs[0] = b43_radio_read(dev, B2056_RX0 |
						       B2056_RX_RSSI_MISC);
		rssical_radio_regs[1] = b43_radio_read(dev, B2056_RX1 |
						       B2056_RX_RSSI_MISC);
	}
	rssical_phy_regs[0] = b43_phy_read(dev, B43_NPHY_RSSIMC_0I_RSSI_Z);
	rssical_phy_regs[1] = b43_phy_read(dev, B43_NPHY_RSSIMC_0Q_RSSI_Z);
	rssical_phy_regs[2] = b43_phy_read(dev, B43_NPHY_RSSIMC_1I_RSSI_Z);
	rssical_phy_regs[3] = b43_phy_read(dev, B43_NPHY_RSSIMC_1Q_RSSI_Z);
	rssical_phy_regs[4] = b43_phy_read(dev, B43_NPHY_RSSIMC_0I_RSSI_X);
	rssical_phy_regs[5] = b43_phy_read(dev, B43_NPHY_RSSIMC_0Q_RSSI_X);
	rssical_phy_regs[6] = b43_phy_read(dev, B43_NPHY_RSSIMC_1I_RSSI_X);
	rssical_phy_regs[7] = b43_phy_read(dev, B43_NPHY_RSSIMC_1Q_RSSI_X);
	rssical_phy_regs[8] = b43_phy_read(dev, B43_NPHY_RSSIMC_0I_RSSI_Y);
	rssical_phy_regs[9] = b43_phy_read(dev, B43_NPHY_RSSIMC_0Q_RSSI_Y);
	rssical_phy_regs[10] = b43_phy_read(dev, B43_NPHY_RSSIMC_1I_RSSI_Y);
	rssical_phy_regs[11] = b43_phy_read(dev, B43_NPHY_RSSIMC_1Q_RSSI_Y);

	/* Remember for which channel we store configuration */
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		nphy->rssical_chanspec_2G.center_freq = phy->chandef->chan->center_freq;
	else
		nphy->rssical_chanspec_5G.center_freq = phy->chandef->chan->center_freq;

	/* End of calibration, restore configuration */
	b43_nphy_classifier(dev, 7, class);
	b43_nphy_write_clip_detection(dev, clip_state);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal */
static void b43_nphy_rev2_rssi_cal(struct b43_wldev *dev, enum n_rssi_type type)
{
	int i, j, vcm;
	u8 state[4];
	u8 code, val;
	u16 class, override;
	u8 regs_save_radio[2];
	u16 regs_save_phy[2];

	s32 offset[4];
	u8 core;
	u8 rail;

	u16 clip_state[2];
	u16 clip_off[2] = { 0xFFFF, 0xFFFF };
	s32 results_min[4] = { };
	u8 vcm_final[4] = { };
	s32 results[4][4] = { };
	s32 miniq[4][2] = { };

	if (type == N_RSSI_NB) {
		code = 0;
		val = 6;
	} else if (type == N_RSSI_W1 || type == N_RSSI_W2) {
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
	regs_save_radio[0] = b43_radio_read(dev, B2055_C1_PD_RXTX);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, override);
	b43_radio_write(dev, B2055_C1_PD_RXTX, val);

	regs_save_phy[1] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);
	regs_save_radio[1] = b43_radio_read(dev, B2055_C2_PD_RXTX);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, override);
	b43_radio_write(dev, B2055_C2_PD_RXTX, val);

	state[0] = b43_radio_read(dev, B2055_C1_PD_RSSIMISC) & 0x07;
	state[1] = b43_radio_read(dev, B2055_C2_PD_RSSIMISC) & 0x07;
	b43_radio_mask(dev, B2055_C1_PD_RSSIMISC, 0xF8);
	b43_radio_mask(dev, B2055_C2_PD_RSSIMISC, 0xF8);
	state[2] = b43_radio_read(dev, B2055_C1_SP_RSSI) & 0x07;
	state[3] = b43_radio_read(dev, B2055_C2_SP_RSSI) & 0x07;

	b43_nphy_rssi_select(dev, 5, type);
	b43_nphy_scale_offset_rssi(dev, 0, 0, 5, N_RAIL_I, type);
	b43_nphy_scale_offset_rssi(dev, 0, 0, 5, N_RAIL_Q, type);

	for (vcm = 0; vcm < 4; vcm++) {
		u8 tmp[4];
		for (j = 0; j < 4; j++)
			tmp[j] = vcm;
		if (type != N_RSSI_W2)
			b43_nphy_set_rssi_2055_vcm(dev, type, tmp);
		b43_nphy_poll_rssi(dev, type, results[vcm], 8);
		if (type == N_RSSI_W1 || type == N_RSSI_W2)
			for (j = 0; j < 2; j++)
				miniq[vcm][j] = min(results[vcm][2 * j],
						    results[vcm][2 * j + 1]);
	}

	for (i = 0; i < 4; i++) {
		s32 mind = 0x100000;
		u8 minvcm = 0;
		s32 minpoll = 249;
		s32 currd;
		for (vcm = 0; vcm < 4; vcm++) {
			if (type == N_RSSI_NB)
				currd = abs(results[vcm][i] - code * 8);
			else
				currd = abs(miniq[vcm][i / 2] - code * 8);

			if (currd < mind) {
				mind = currd;
				minvcm = vcm;
			}

			if (results[vcm][i] < minpoll)
				minpoll = results[vcm][i];
		}
		results_min[i] = minpoll;
		vcm_final[i] = minvcm;
	}

	if (type != N_RSSI_W2)
		b43_nphy_set_rssi_2055_vcm(dev, type, vcm_final);

	for (i = 0; i < 4; i++) {
		offset[i] = (code * 8) - results[vcm_final[i]][i];

		if (offset[i] < 0)
			offset[i] = -((abs(offset[i]) + 4) / 8);
		else
			offset[i] = (offset[i] + 4) / 8;

		if (results_min[i] == 248)
			offset[i] = code - 32;

		core = (i / 2) ? 2 : 1;
		rail = (i % 2) ? N_RAIL_Q : N_RAIL_I;

		b43_nphy_scale_offset_rssi(dev, 0, offset[i], core, rail,
						type);
	}

	b43_radio_maskset(dev, B2055_C1_PD_RSSIMISC, 0xF8, state[0]);
	b43_radio_maskset(dev, B2055_C2_PD_RSSIMISC, 0xF8, state[1]);

	switch (state[2]) {
	case 1:
		b43_nphy_rssi_select(dev, 1, N_RSSI_NB);
		break;
	case 4:
		b43_nphy_rssi_select(dev, 1, N_RSSI_W1);
		break;
	case 2:
		b43_nphy_rssi_select(dev, 1, N_RSSI_W2);
		break;
	default:
		b43_nphy_rssi_select(dev, 1, N_RSSI_W2);
		break;
	}

	switch (state[3]) {
	case 1:
		b43_nphy_rssi_select(dev, 2, N_RSSI_NB);
		break;
	case 4:
		b43_nphy_rssi_select(dev, 2, N_RSSI_W1);
		break;
	default:
		b43_nphy_rssi_select(dev, 2, N_RSSI_W2);
		break;
	}

	b43_nphy_rssi_select(dev, 0, type);

	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, regs_save_phy[0]);
	b43_radio_write(dev, B2055_C1_PD_RXTX, regs_save_radio[0]);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, regs_save_phy[1]);
	b43_radio_write(dev, B2055_C2_PD_RXTX, regs_save_radio[1]);

	b43_nphy_classifier(dev, 7, class);
	b43_nphy_write_clip_detection(dev, clip_state);
	/* Specs don't say about reset here, but it makes wl and b43 dumps
	   identical, it really seems wl performs this */
	b43_nphy_reset_cca(dev);
}

/*
 * RSSI Calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal
 */
static void b43_nphy_rssi_cal(struct b43_wldev *dev)
{
	if (dev->phy.rev >= 19) {
		/* TODO */
	} else if (dev->phy.rev >= 3) {
		b43_nphy_rev3_rssi_cal(dev);
	} else {
		b43_nphy_rev2_rssi_cal(dev, N_RSSI_NB);
		b43_nphy_rev2_rssi_cal(dev, N_RSSI_W1);
		b43_nphy_rev2_rssi_cal(dev, N_RSSI_W2);
	}
}

/**************************************************
 * Workarounds
 **************************************************/

static void b43_nphy_gain_ctl_workarounds_rev19(struct b43_wldev *dev)
{
	/* TODO */
}

static void b43_nphy_gain_ctl_workarounds_rev7(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	switch (phy->rev) {
	/* TODO */
	}
}

static void b43_nphy_gain_ctl_workarounds_rev3(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	bool ghz5;
	bool ext_lna;
	u16 rssi_gain;
	struct nphy_gain_ctl_workaround_entry *e;
	u8 lpf_gain[6] = { 0x00, 0x06, 0x0C, 0x12, 0x12, 0x12 };
	u8 lpf_bits[6] = { 0, 1, 2, 3, 3, 3 };

	/* Prepare values */
	ghz5 = b43_phy_read(dev, B43_NPHY_BANDCTL)
		& B43_NPHY_BANDCTL_5GHZ;
	ext_lna = ghz5 ? sprom->boardflags_hi & B43_BFH_EXTLNA_5GHZ :
		sprom->boardflags_lo & B43_BFL_EXTLNA;
	e = b43_nphy_get_gain_ctl_workaround_ent(dev, ghz5, ext_lna);
	if (ghz5 && dev->phy.rev >= 5)
		rssi_gain = 0x90;
	else
		rssi_gain = 0x50;

	b43_phy_set(dev, B43_NPHY_RXCTL, 0x0040);

	/* Set Clip 2 detect */
	b43_phy_set(dev, B43_NPHY_C1_CGAINI, B43_NPHY_C1_CGAINI_CL2DETECT);
	b43_phy_set(dev, B43_NPHY_C2_CGAINI, B43_NPHY_C2_CGAINI_CL2DETECT);

	b43_radio_write(dev, B2056_RX0 | B2056_RX_BIASPOLE_LNAG1_IDAC,
			0x17);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_BIASPOLE_LNAG1_IDAC,
			0x17);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_LNAG2_IDAC, 0xF0);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_LNAG2_IDAC, 0xF0);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_RSSI_POLE, 0x00);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_RSSI_POLE, 0x00);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_RSSI_GAIN,
			rssi_gain);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_RSSI_GAIN,
			rssi_gain);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_BIASPOLE_LNAA1_IDAC,
			0x17);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_BIASPOLE_LNAA1_IDAC,
			0x17);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_LNAA2_IDAC, 0xFF);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_LNAA2_IDAC, 0xFF);

	b43_ntab_write_bulk(dev, B43_NTAB8(0, 8), 4, e->lna1_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(1, 8), 4, e->lna1_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(0, 16), 4, e->lna2_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(1, 16), 4, e->lna2_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(0, 32), 10, e->gain_db);
	b43_ntab_write_bulk(dev, B43_NTAB8(1, 32), 10, e->gain_db);
	b43_ntab_write_bulk(dev, B43_NTAB8(2, 32), 10, e->gain_bits);
	b43_ntab_write_bulk(dev, B43_NTAB8(3, 32), 10, e->gain_bits);
	b43_ntab_write_bulk(dev, B43_NTAB8(0, 0x40), 6, lpf_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(1, 0x40), 6, lpf_gain);
	b43_ntab_write_bulk(dev, B43_NTAB8(2, 0x40), 6, lpf_bits);
	b43_ntab_write_bulk(dev, B43_NTAB8(3, 0x40), 6, lpf_bits);

	b43_phy_write(dev, B43_NPHY_REV3_C1_INITGAIN_A, e->init_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C2_INITGAIN_A, e->init_gain);

	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x106), 2,
				e->rfseq_init);

	b43_phy_write(dev, B43_NPHY_REV3_C1_CLIP_HIGAIN_A, e->cliphi_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C2_CLIP_HIGAIN_A, e->cliphi_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C1_CLIP_MEDGAIN_A, e->clipmd_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C2_CLIP_MEDGAIN_A, e->clipmd_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C1_CLIP_LOGAIN_A, e->cliplo_gain);
	b43_phy_write(dev, B43_NPHY_REV3_C2_CLIP_LOGAIN_A, e->cliplo_gain);

	b43_phy_maskset(dev, B43_NPHY_CRSMINPOWER0, 0xFF00, e->crsmin);
	b43_phy_maskset(dev, B43_NPHY_CRSMINPOWERL0, 0xFF00, e->crsminl);
	b43_phy_maskset(dev, B43_NPHY_CRSMINPOWERU0, 0xFF00, e->crsminu);
	b43_phy_write(dev, B43_NPHY_C1_NBCLIPTHRES, e->nbclip);
	b43_phy_write(dev, B43_NPHY_C2_NBCLIPTHRES, e->nbclip);
	b43_phy_maskset(dev, B43_NPHY_C1_CLIPWBTHRES,
			~B43_NPHY_C1_CLIPWBTHRES_CLIP2, e->wlclip);
	b43_phy_maskset(dev, B43_NPHY_C2_CLIPWBTHRES,
			~B43_NPHY_C2_CLIPWBTHRES_CLIP2, e->wlclip);
	b43_phy_write(dev, B43_NPHY_CCK_SHIFTB_REF, 0x809C);
}

static void b43_nphy_gain_ctl_workarounds_rev1_2(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u8 i, j;
	u8 code;
	u16 tmp;
	u8 rfseq_events[3] = { 6, 8, 7 };
	u8 rfseq_delays[3] = { 10, 30, 1 };

	/* Set Clip 2 detect */
	b43_phy_set(dev, B43_NPHY_C1_CGAINI, B43_NPHY_C1_CGAINI_CL2DETECT);
	b43_phy_set(dev, B43_NPHY_C2_CGAINI, B43_NPHY_C2_CGAINI_CL2DETECT);

	/* Set narrowband clip threshold */
	b43_phy_write(dev, B43_NPHY_C1_NBCLIPTHRES, 0x84);
	b43_phy_write(dev, B43_NPHY_C2_NBCLIPTHRES, 0x84);

	if (!b43_is_40mhz(dev)) {
		/* Set dwell lengths */
		b43_phy_write(dev, B43_NPHY_CLIP1_NBDWELL_LEN, 0x002B);
		b43_phy_write(dev, B43_NPHY_CLIP2_NBDWELL_LEN, 0x002B);
		b43_phy_write(dev, B43_NPHY_W1CLIP1_DWELL_LEN, 0x0009);
		b43_phy_write(dev, B43_NPHY_W1CLIP2_DWELL_LEN, 0x0009);
	}

	/* Set wideband clip 2 threshold */
	b43_phy_maskset(dev, B43_NPHY_C1_CLIPWBTHRES,
			~B43_NPHY_C1_CLIPWBTHRES_CLIP2, 21);
	b43_phy_maskset(dev, B43_NPHY_C2_CLIPWBTHRES,
			~B43_NPHY_C2_CLIPWBTHRES_CLIP2, 21);

	if (!b43_is_40mhz(dev)) {
		b43_phy_maskset(dev, B43_NPHY_C1_CGAINI,
			~B43_NPHY_C1_CGAINI_GAINBKOFF, 0x1);
		b43_phy_maskset(dev, B43_NPHY_C2_CGAINI,
			~B43_NPHY_C2_CGAINI_GAINBKOFF, 0x1);
		b43_phy_maskset(dev, B43_NPHY_C1_CCK_CGAINI,
			~B43_NPHY_C1_CCK_CGAINI_GAINBKOFF, 0x1);
		b43_phy_maskset(dev, B43_NPHY_C2_CCK_CGAINI,
			~B43_NPHY_C2_CCK_CGAINI_GAINBKOFF, 0x1);
	}

	b43_phy_write(dev, B43_NPHY_CCK_SHIFTB_REF, 0x809C);

	if (nphy->gain_boost) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ &&
		    b43_is_40mhz(dev))
			code = 4;
		else
			code = 5;
	} else {
		code = b43_is_40mhz(dev) ? 6 : 7;
	}

	/* Set HPVGA2 index */
	b43_phy_maskset(dev, B43_NPHY_C1_INITGAIN, ~B43_NPHY_C1_INITGAIN_HPVGA2,
			code << B43_NPHY_C1_INITGAIN_HPVGA2_SHIFT);
	b43_phy_maskset(dev, B43_NPHY_C2_INITGAIN, ~B43_NPHY_C2_INITGAIN_HPVGA2,
			code << B43_NPHY_C2_INITGAIN_HPVGA2_SHIFT);

	b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x1D06);
	/* specs say about 2 loops, but wl does 4 */
	for (i = 0; i < 4; i++)
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, (code << 8 | 0x7C));

	b43_nphy_adjust_lna_gain_table(dev);

	if (nphy->elna_gain_config) {
		b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x0808);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x0);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);

		b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x0C08);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x0);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);
		b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0x1);

		b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x1D06);
		/* specs say about 2 loops, but wl does 4 */
		for (i = 0; i < 4; i++)
			b43_phy_write(dev, B43_NPHY_TABLE_DATALO,
						(code << 8 | 0x74));
	}

	if (dev->phy.rev == 2) {
		for (i = 0; i < 4; i++) {
			b43_phy_write(dev, B43_NPHY_TABLE_ADDR,
					(0x0400 * i) + 0x0020);
			for (j = 0; j < 21; j++) {
				tmp = j * (i < 2 ? 3 : 1);
				b43_phy_write(dev,
					B43_NPHY_TABLE_DATALO, tmp);
			}
		}
	}

	b43_nphy_set_rf_sequence(dev, 5, rfseq_events, rfseq_delays, 3);
	b43_phy_maskset(dev, B43_NPHY_OVER_DGAIN1,
		~B43_NPHY_OVER_DGAIN_CCKDGECV & 0xFFFF,
		0x5A << B43_NPHY_OVER_DGAIN_CCKDGECV_SHIFT);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		b43_phy_maskset(dev, B43_PHY_N(0xC5D), 0xFF80, 4);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/WorkaroundsGainCtrl */
static void b43_nphy_gain_ctl_workarounds(struct b43_wldev *dev)
{
	if (dev->phy.rev >= 19)
		b43_nphy_gain_ctl_workarounds_rev19(dev);
	else if (dev->phy.rev >= 7)
		b43_nphy_gain_ctl_workarounds_rev7(dev);
	else if (dev->phy.rev >= 3)
		b43_nphy_gain_ctl_workarounds_rev3(dev);
	else
		b43_nphy_gain_ctl_workarounds_rev1_2(dev);
}

static void b43_nphy_workarounds_rev7plus(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	struct b43_phy *phy = &dev->phy;

	u8 rx2tx_events_ipa[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0xF, 0x3,
					0x1F };
	u8 rx2tx_delays_ipa[9] = { 8, 6, 6, 4, 4, 16, 43, 1, 1 };

	u16 ntab7_15e_16e[] = { 0x10f, 0x10f };
	u8 ntab7_138_146[] = { 0x11, 0x11 };
	u8 ntab7_133[] = { 0x77, 0x11, 0x11 };

	u16 lpf_20, lpf_40, lpf_11b;
	u16 bcap_val, bcap_val_11b, bcap_val_11n_20, bcap_val_11n_40;
	u16 scap_val, scap_val_11b, scap_val_11n_20, scap_val_11n_40;
	bool rccal_ovrd = false;

	u16 rx2tx_lut_20_11b, rx2tx_lut_20_11n, rx2tx_lut_40_11n;
	u16 bias, conv, filt;

	u32 tmp32;
	u8 core;

	if (phy->rev == 7) {
		b43_phy_set(dev, B43_NPHY_FINERX2_CGC, 0x10);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN0, 0xFF80, 0x0020);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN0, 0x80FF, 0x2700);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN1, 0xFF80, 0x002E);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN1, 0x80FF, 0x3300);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN2, 0xFF80, 0x0037);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN2, 0x80FF, 0x3A00);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN3, 0xFF80, 0x003C);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN3, 0x80FF, 0x3E00);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN4, 0xFF80, 0x003E);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN4, 0x80FF, 0x3F00);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN5, 0xFF80, 0x0040);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN5, 0x80FF, 0x4000);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN6, 0xFF80, 0x0040);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN6, 0x80FF, 0x4000);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN7, 0xFF80, 0x0040);
		b43_phy_maskset(dev, B43_NPHY_FREQGAIN7, 0x80FF, 0x4000);
	}
	if (phy->rev <= 8) {
		b43_phy_write(dev, B43_NPHY_FORCEFRONT0, 0x1B0);
		b43_phy_write(dev, B43_NPHY_FORCEFRONT1, 0x1B0);
	}
	if (phy->rev >= 8)
		b43_phy_maskset(dev, B43_NPHY_TXTAILCNT, ~0xFF, 0x72);

	b43_ntab_write(dev, B43_NTAB16(8, 0x00), 2);
	b43_ntab_write(dev, B43_NTAB16(8, 0x10), 2);
	tmp32 = b43_ntab_read(dev, B43_NTAB32(30, 0));
	tmp32 &= 0xffffff;
	b43_ntab_write(dev, B43_NTAB32(30, 0), tmp32);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x15e), 2, ntab7_15e_16e);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x16e), 2, ntab7_15e_16e);

	if (b43_nphy_ipa(dev))
		b43_nphy_set_rf_sequence(dev, 0, rx2tx_events_ipa,
				rx2tx_delays_ipa, ARRAY_SIZE(rx2tx_events_ipa));

	b43_phy_maskset(dev, B43_NPHY_EPS_OVERRIDEI_0, 0x3FFF, 0x4000);
	b43_phy_maskset(dev, B43_NPHY_EPS_OVERRIDEI_1, 0x3FFF, 0x4000);

	lpf_20 = b43_nphy_read_lpf_ctl(dev, 0x154);
	lpf_40 = b43_nphy_read_lpf_ctl(dev, 0x159);
	lpf_11b = b43_nphy_read_lpf_ctl(dev, 0x152);
	if (b43_nphy_ipa(dev)) {
		if ((phy->radio_rev == 5 && b43_is_40mhz(dev)) ||
		    phy->radio_rev == 7 || phy->radio_rev == 8) {
			bcap_val = b43_radio_read(dev, 0x16b);
			scap_val = b43_radio_read(dev, 0x16a);
			scap_val_11b = scap_val;
			bcap_val_11b = bcap_val;
			if (phy->radio_rev == 5 && b43_is_40mhz(dev)) {
				scap_val_11n_20 = scap_val;
				bcap_val_11n_20 = bcap_val;
				scap_val_11n_40 = bcap_val_11n_40 = 0xc;
				rccal_ovrd = true;
			} else { /* Rev 7/8 */
				lpf_20 = 4;
				lpf_11b = 1;
				if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
					scap_val_11n_20 = 0xc;
					bcap_val_11n_20 = 0xc;
					scap_val_11n_40 = 0xa;
					bcap_val_11n_40 = 0xa;
				} else {
					scap_val_11n_20 = 0x14;
					bcap_val_11n_20 = 0x14;
					scap_val_11n_40 = 0xf;
					bcap_val_11n_40 = 0xf;
				}
				rccal_ovrd = true;
			}
		}
	} else {
		if (phy->radio_rev == 5) {
			lpf_20 = 1;
			lpf_40 = 3;
			bcap_val = b43_radio_read(dev, 0x16b);
			scap_val = b43_radio_read(dev, 0x16a);
			scap_val_11b = scap_val;
			bcap_val_11b = bcap_val;
			scap_val_11n_20 = 0x11;
			scap_val_11n_40 = 0x11;
			bcap_val_11n_20 = 0x13;
			bcap_val_11n_40 = 0x13;
			rccal_ovrd = true;
		}
	}
	if (rccal_ovrd) {
		rx2tx_lut_20_11b = (bcap_val_11b << 8) |
				   (scap_val_11b << 3) |
				   lpf_11b;
		rx2tx_lut_20_11n = (bcap_val_11n_20 << 8) |
				   (scap_val_11n_20 << 3) |
				   lpf_20;
		rx2tx_lut_40_11n = (bcap_val_11n_40 << 8) |
				   (scap_val_11n_40 << 3) |
				   lpf_40;
		for (core = 0; core < 2; core++) {
			b43_ntab_write(dev, B43_NTAB16(7, 0x152 + core * 16),
				       rx2tx_lut_20_11b);
			b43_ntab_write(dev, B43_NTAB16(7, 0x153 + core * 16),
				       rx2tx_lut_20_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x154 + core * 16),
				       rx2tx_lut_20_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x155 + core * 16),
				       rx2tx_lut_40_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x156 + core * 16),
				       rx2tx_lut_40_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x157 + core * 16),
				       rx2tx_lut_40_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x158 + core * 16),
				       rx2tx_lut_40_11n);
			b43_ntab_write(dev, B43_NTAB16(7, 0x159 + core * 16),
				       rx2tx_lut_40_11n);
		}
		b43_nphy_rf_ctl_override_rev7(dev, 16, 1, 3, false, 2);
	}
	b43_phy_write(dev, 0x32F, 0x3);
	if (phy->radio_rev == 4 || phy->radio_rev == 6)
		b43_nphy_rf_ctl_override_rev7(dev, 4, 1, 3, false, 0);

	if (phy->radio_rev == 3 || phy->radio_rev == 4 || phy->radio_rev == 6) {
		if (sprom->revision &&
		    sprom->boardflags2_hi & B43_BFH2_IPALVLSHIFT_3P3) {
			b43_radio_write(dev, 0x5, 0x05);
			b43_radio_write(dev, 0x6, 0x30);
			b43_radio_write(dev, 0x7, 0x00);
			b43_radio_set(dev, 0x4f, 0x1);
			b43_radio_set(dev, 0xd4, 0x1);
			bias = 0x1f;
			conv = 0x6f;
			filt = 0xaa;
		} else {
			bias = 0x2b;
			conv = 0x7f;
			filt = 0xee;
		}
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			for (core = 0; core < 2; core++) {
				if (core == 0) {
					b43_radio_write(dev, 0x5F, bias);
					b43_radio_write(dev, 0x64, conv);
					b43_radio_write(dev, 0x66, filt);
				} else {
					b43_radio_write(dev, 0xE8, bias);
					b43_radio_write(dev, 0xE9, conv);
					b43_radio_write(dev, 0xEB, filt);
				}
			}
		}
	}

	if (b43_nphy_ipa(dev)) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			if (phy->radio_rev == 3 || phy->radio_rev == 4 ||
			    phy->radio_rev == 6) {
				for (core = 0; core < 2; core++) {
					if (core == 0)
						b43_radio_write(dev, 0x51,
								0x7f);
					else
						b43_radio_write(dev, 0xd6,
								0x7f);
				}
			}
			if (phy->radio_rev == 3) {
				for (core = 0; core < 2; core++) {
					if (core == 0) {
						b43_radio_write(dev, 0x64,
								0x13);
						b43_radio_write(dev, 0x5F,
								0x1F);
						b43_radio_write(dev, 0x66,
								0xEE);
						b43_radio_write(dev, 0x59,
								0x8A);
						b43_radio_write(dev, 0x80,
								0x3E);
					} else {
						b43_radio_write(dev, 0x69,
								0x13);
						b43_radio_write(dev, 0xE8,
								0x1F);
						b43_radio_write(dev, 0xEB,
								0xEE);
						b43_radio_write(dev, 0xDE,
								0x8A);
						b43_radio_write(dev, 0x105,
								0x3E);
					}
				}
			} else if (phy->radio_rev == 7 || phy->radio_rev == 8) {
				if (!b43_is_40mhz(dev)) {
					b43_radio_write(dev, 0x5F, 0x14);
					b43_radio_write(dev, 0xE8, 0x12);
				} else {
					b43_radio_write(dev, 0x5F, 0x16);
					b43_radio_write(dev, 0xE8, 0x16);
				}
			}
		} else {
			u16 freq = phy->chandef->chan->center_freq;
			if ((freq >= 5180 && freq <= 5230) ||
			    (freq >= 5745 && freq <= 5805)) {
				b43_radio_write(dev, 0x7D, 0xFF);
				b43_radio_write(dev, 0xFE, 0xFF);
			}
		}
	} else {
		if (phy->radio_rev != 5) {
			for (core = 0; core < 2; core++) {
				if (core == 0) {
					b43_radio_write(dev, 0x5c, 0x61);
					b43_radio_write(dev, 0x51, 0x70);
				} else {
					b43_radio_write(dev, 0xe1, 0x61);
					b43_radio_write(dev, 0xd6, 0x70);
				}
			}
		}
	}

	if (phy->radio_rev == 4) {
		b43_ntab_write(dev, B43_NTAB16(8, 0x05), 0x20);
		b43_ntab_write(dev, B43_NTAB16(8, 0x15), 0x20);
		for (core = 0; core < 2; core++) {
			if (core == 0) {
				b43_radio_write(dev, 0x1a1, 0x00);
				b43_radio_write(dev, 0x1a2, 0x3f);
				b43_radio_write(dev, 0x1a6, 0x3f);
			} else {
				b43_radio_write(dev, 0x1a7, 0x00);
				b43_radio_write(dev, 0x1ab, 0x3f);
				b43_radio_write(dev, 0x1ac, 0x3f);
			}
		}
	} else {
		b43_phy_set(dev, B43_NPHY_AFECTL_C1, 0x4);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER1, 0x4);
		b43_phy_set(dev, B43_NPHY_AFECTL_C2, 0x4);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x4);

		b43_phy_mask(dev, B43_NPHY_AFECTL_C1, ~0x1);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER1, 0x1);
		b43_phy_mask(dev, B43_NPHY_AFECTL_C2, ~0x1);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x1);
		b43_ntab_write(dev, B43_NTAB16(8, 0x05), 0x20);
		b43_ntab_write(dev, B43_NTAB16(8, 0x15), 0x20);

		b43_phy_mask(dev, B43_NPHY_AFECTL_C1, ~0x4);
		b43_phy_mask(dev, B43_NPHY_AFECTL_OVER1, ~0x4);
		b43_phy_mask(dev, B43_NPHY_AFECTL_C2, ~0x4);
		b43_phy_mask(dev, B43_NPHY_AFECTL_OVER, ~0x4);
	}

	b43_phy_write(dev, B43_NPHY_ENDROP_TLEN, 0x2);

	b43_ntab_write(dev, B43_NTAB32(16, 0x100), 20);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x138), 2, ntab7_138_146);
	b43_ntab_write(dev, B43_NTAB16(7, 0x141), 0x77);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x133), 3, ntab7_133);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x146), 2, ntab7_138_146);
	b43_ntab_write(dev, B43_NTAB16(7, 0x123), 0x77);
	b43_ntab_write(dev, B43_NTAB16(7, 0x12A), 0x77);

	if (!b43_is_40mhz(dev)) {
		b43_ntab_write(dev, B43_NTAB32(16, 0x03), 0x18D);
		b43_ntab_write(dev, B43_NTAB32(16, 0x7F), 0x18D);
	} else {
		b43_ntab_write(dev, B43_NTAB32(16, 0x03), 0x14D);
		b43_ntab_write(dev, B43_NTAB32(16, 0x7F), 0x14D);
	}

	b43_nphy_gain_ctl_workarounds(dev);

	/* TODO
	b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x08), 4,
			    aux_adc_vmid_rev7_core0);
	b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x18), 4,
			    aux_adc_vmid_rev7_core1);
	b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x0C), 4,
			    aux_adc_gain_rev7);
	b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x1C), 4,
			    aux_adc_gain_rev7);
	*/
}

static void b43_nphy_workarounds_rev3plus(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	/* TX to RX */
	u8 tx2rx_events[7] = { 0x4, 0x3, 0x5, 0x2, 0x1, 0x8, 0x1F };
	u8 tx2rx_delays[7] = { 8, 4, 4, 4, 4, 6, 1 };
	/* RX to TX */
	u8 rx2tx_events_ipa[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0xF, 0x3,
					0x1F };
	u8 rx2tx_delays_ipa[9] = { 8, 6, 6, 4, 4, 16, 43, 1, 1 };
	u8 rx2tx_events[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0x3, 0x4, 0x1F };
	u8 rx2tx_delays[9] = { 8, 6, 6, 4, 4, 18, 42, 1, 1 };

	u16 vmids[5][4] = {
		{ 0xa2, 0xb4, 0xb4, 0x89, }, /* 0 */
		{ 0xb4, 0xb4, 0xb4, 0x24, }, /* 1 */
		{ 0xa2, 0xb4, 0xb4, 0x74, }, /* 2 */
		{ 0xa2, 0xb4, 0xb4, 0x270, }, /* 3 */
		{ 0xa2, 0xb4, 0xb4, 0x00, }, /* 4 and 5 */
	};
	u16 gains[5][4] = {
		{ 0x02, 0x02, 0x02, 0x00, }, /* 0 */
		{ 0x02, 0x02, 0x02, 0x02, }, /* 1 */
		{ 0x02, 0x02, 0x02, 0x04, }, /* 2 */
		{ 0x02, 0x02, 0x02, 0x00, }, /* 3 */
		{ 0x02, 0x02, 0x02, 0x00, }, /* 4 and 5 */
	};
	u16 *vmid, *gain;

	u8 pdet_range;
	u16 tmp16;
	u32 tmp32;

	b43_phy_write(dev, B43_NPHY_FORCEFRONT0, 0x1f8);
	b43_phy_write(dev, B43_NPHY_FORCEFRONT1, 0x1f8);

	tmp32 = b43_ntab_read(dev, B43_NTAB32(30, 0));
	tmp32 &= 0xffffff;
	b43_ntab_write(dev, B43_NTAB32(30, 0), tmp32);

	b43_phy_write(dev, B43_NPHY_PHASETR_A0, 0x0125);
	b43_phy_write(dev, B43_NPHY_PHASETR_A1, 0x01B3);
	b43_phy_write(dev, B43_NPHY_PHASETR_A2, 0x0105);
	b43_phy_write(dev, B43_NPHY_PHASETR_B0, 0x016E);
	b43_phy_write(dev, B43_NPHY_PHASETR_B1, 0x00CD);
	b43_phy_write(dev, B43_NPHY_PHASETR_B2, 0x0020);

	b43_phy_write(dev, B43_NPHY_REV3_C1_CLIP_LOGAIN_B, 0x000C);
	b43_phy_write(dev, B43_NPHY_REV3_C2_CLIP_LOGAIN_B, 0x000C);

	/* TX to RX */
	b43_nphy_set_rf_sequence(dev, 1, tx2rx_events, tx2rx_delays,
				 ARRAY_SIZE(tx2rx_events));

	/* RX to TX */
	if (b43_nphy_ipa(dev))
		b43_nphy_set_rf_sequence(dev, 0, rx2tx_events_ipa,
				rx2tx_delays_ipa, ARRAY_SIZE(rx2tx_events_ipa));
	if (nphy->hw_phyrxchain != 3 &&
	    nphy->hw_phyrxchain != nphy->hw_phytxchain) {
		if (b43_nphy_ipa(dev)) {
			rx2tx_delays[5] = 59;
			rx2tx_delays[6] = 1;
			rx2tx_events[7] = 0x1F;
		}
		b43_nphy_set_rf_sequence(dev, 0, rx2tx_events, rx2tx_delays,
					 ARRAY_SIZE(rx2tx_events));
	}

	tmp16 = (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) ?
		0x2 : 0x9C40;
	b43_phy_write(dev, B43_NPHY_ENDROP_TLEN, tmp16);

	b43_phy_maskset(dev, B43_NPHY_SGILTRNOFFSET, 0xF0FF, 0x0700);

	if (!b43_is_40mhz(dev)) {
		b43_ntab_write(dev, B43_NTAB32(16, 3), 0x18D);
		b43_ntab_write(dev, B43_NTAB32(16, 127), 0x18D);
	} else {
		b43_ntab_write(dev, B43_NTAB32(16, 3), 0x14D);
		b43_ntab_write(dev, B43_NTAB32(16, 127), 0x14D);
	}

	b43_nphy_gain_ctl_workarounds(dev);

	b43_ntab_write(dev, B43_NTAB16(8, 0), 2);
	b43_ntab_write(dev, B43_NTAB16(8, 16), 2);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		pdet_range = sprom->fem.ghz2.pdet_range;
	else
		pdet_range = sprom->fem.ghz5.pdet_range;
	vmid = vmids[min_t(u16, pdet_range, 4)];
	gain = gains[min_t(u16, pdet_range, 4)];
	switch (pdet_range) {
	case 3:
		if (!(dev->phy.rev >= 4 &&
		      b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ))
			break;
		/* FALL THROUGH */
	case 0:
	case 1:
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x08), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x18), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x0c), 4, gain);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x1c), 4, gain);
		break;
	case 2:
		if (dev->phy.rev >= 6) {
			if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
				vmid[3] = 0x94;
			else
				vmid[3] = 0x8e;
			gain[3] = 3;
		} else if (dev->phy.rev == 5) {
			vmid[3] = 0x84;
			gain[3] = 2;
		}
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x08), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x18), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x0c), 4, gain);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x1c), 4, gain);
		break;
	case 4:
	case 5:
		if (b43_current_band(dev->wl) != IEEE80211_BAND_2GHZ) {
			if (pdet_range == 4) {
				vmid[3] = 0x8e;
				tmp16 = 0x96;
				gain[3] = 0x2;
			} else {
				vmid[3] = 0x89;
				tmp16 = 0x89;
				gain[3] = 0;
			}
		} else {
			if (pdet_range == 4) {
				vmid[3] = 0x89;
				tmp16 = 0x8b;
				gain[3] = 0x2;
			} else {
				vmid[3] = 0x74;
				tmp16 = 0x70;
				gain[3] = 0;
			}
		}
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x08), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x0c), 4, gain);
		vmid[3] = tmp16;
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x18), 4, vmid);
		b43_ntab_write_bulk(dev, B43_NTAB16(8, 0x1c), 4, gain);
		break;
	}

	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXA_MAST_BIAS, 0x00);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXA_MAST_BIAS, 0x00);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXA_BIAS_MAIN, 0x06);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXA_BIAS_MAIN, 0x06);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXA_BIAS_AUX, 0x07);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXA_BIAS_AUX, 0x07);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXA_LOB_BIAS, 0x88);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXA_LOB_BIAS, 0x88);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXA_CMFB_IDAC, 0x00);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXA_CMFB_IDAC, 0x00);
	b43_radio_write(dev, B2056_RX0 | B2056_RX_MIXG_CMFB_IDAC, 0x00);
	b43_radio_write(dev, B2056_RX1 | B2056_RX_MIXG_CMFB_IDAC, 0x00);

	/* N PHY WAR TX Chain Update with hw_phytxchain as argument */

	if ((sprom->boardflags2_lo & B43_BFL2_APLL_WAR &&
	     b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) ||
	    (sprom->boardflags2_lo & B43_BFL2_GPLL_WAR &&
	     b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ))
		tmp32 = 0x00088888;
	else
		tmp32 = 0x88888888;
	b43_ntab_write(dev, B43_NTAB32(30, 1), tmp32);
	b43_ntab_write(dev, B43_NTAB32(30, 2), tmp32);
	b43_ntab_write(dev, B43_NTAB32(30, 3), tmp32);

	if (dev->phy.rev == 4 &&
	    b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
		b43_radio_write(dev, B2056_TX0 | B2056_TX_GMBB_IDAC,
				0x70);
		b43_radio_write(dev, B2056_TX1 | B2056_TX_GMBB_IDAC,
				0x70);
	}

	/* Dropped probably-always-true condition */
	b43_phy_write(dev, B43_NPHY_ED_CRS40ASSERTTHRESH0, 0x03eb);
	b43_phy_write(dev, B43_NPHY_ED_CRS40ASSERTTHRESH1, 0x03eb);
	b43_phy_write(dev, B43_NPHY_ED_CRS40DEASSERTTHRESH0, 0x0341);
	b43_phy_write(dev, B43_NPHY_ED_CRS40DEASSERTTHRESH1, 0x0341);
	b43_phy_write(dev, B43_NPHY_ED_CRS20LASSERTTHRESH0, 0x042b);
	b43_phy_write(dev, B43_NPHY_ED_CRS20LASSERTTHRESH1, 0x042b);
	b43_phy_write(dev, B43_NPHY_ED_CRS20LDEASSERTTHRESH0, 0x0381);
	b43_phy_write(dev, B43_NPHY_ED_CRS20LDEASSERTTHRESH1, 0x0381);
	b43_phy_write(dev, B43_NPHY_ED_CRS20UASSERTTHRESH0, 0x042b);
	b43_phy_write(dev, B43_NPHY_ED_CRS20UASSERTTHRESH1, 0x042b);
	b43_phy_write(dev, B43_NPHY_ED_CRS20UDEASSERTTHRESH0, 0x0381);
	b43_phy_write(dev, B43_NPHY_ED_CRS20UDEASSERTTHRESH1, 0x0381);

	if (dev->phy.rev >= 6 && sprom->boardflags2_lo & B43_BFL2_SINGLEANT_CCK)
		; /* TODO: 0x0080000000000000 HF */
}

static void b43_nphy_workarounds_rev1_2(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	u8 events1[7] = { 0x0, 0x1, 0x2, 0x8, 0x4, 0x5, 0x3 };
	u8 delays1[7] = { 0x8, 0x6, 0x6, 0x2, 0x4, 0x3C, 0x1 };

	u8 events2[7] = { 0x0, 0x3, 0x5, 0x4, 0x2, 0x1, 0x8 };
	u8 delays2[7] = { 0x8, 0x6, 0x2, 0x4, 0x4, 0x6, 0x1 };

	if (sprom->boardflags2_lo & B43_BFL2_SKWRKFEM_BRD ||
	    dev->dev->board_type == BCMA_BOARD_TYPE_BCM943224M93) {
		delays1[0] = 0x1;
		delays1[5] = 0x14;
	}

	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ &&
	    nphy->band5g_pwrgain) {
		b43_radio_mask(dev, B2055_C1_TX_RF_SPARE, ~0x8);
		b43_radio_mask(dev, B2055_C2_TX_RF_SPARE, ~0x8);
	} else {
		b43_radio_set(dev, B2055_C1_TX_RF_SPARE, 0x8);
		b43_radio_set(dev, B2055_C2_TX_RF_SPARE, 0x8);
	}

	b43_ntab_write(dev, B43_NTAB16(8, 0x00), 0x000A);
	b43_ntab_write(dev, B43_NTAB16(8, 0x10), 0x000A);
	if (dev->phy.rev < 3) {
		b43_ntab_write(dev, B43_NTAB16(8, 0x02), 0xCDAA);
		b43_ntab_write(dev, B43_NTAB16(8, 0x12), 0xCDAA);
	}

	if (dev->phy.rev < 2) {
		b43_ntab_write(dev, B43_NTAB16(8, 0x08), 0x0000);
		b43_ntab_write(dev, B43_NTAB16(8, 0x18), 0x0000);
		b43_ntab_write(dev, B43_NTAB16(8, 0x07), 0x7AAB);
		b43_ntab_write(dev, B43_NTAB16(8, 0x17), 0x7AAB);
		b43_ntab_write(dev, B43_NTAB16(8, 0x06), 0x0800);
		b43_ntab_write(dev, B43_NTAB16(8, 0x16), 0x0800);
	}

	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO1, 0x2D8);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1, 0x301);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO2, 0x2D8);
	b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2, 0x301);

	b43_nphy_set_rf_sequence(dev, 0, events1, delays1, 7);
	b43_nphy_set_rf_sequence(dev, 1, events2, delays2, 7);

	b43_nphy_gain_ctl_workarounds(dev);

	if (dev->phy.rev < 2) {
		if (b43_phy_read(dev, B43_NPHY_RXCTL) & 0x2)
			b43_hf_write(dev, b43_hf_read(dev) |
					B43_HF_MLADVW);
	} else if (dev->phy.rev == 2) {
		b43_phy_write(dev, B43_NPHY_CRSCHECK2, 0);
		b43_phy_write(dev, B43_NPHY_CRSCHECK3, 0);
	}

	if (dev->phy.rev < 2)
		b43_phy_mask(dev, B43_NPHY_SCRAM_SIGCTL,
				~B43_NPHY_SCRAM_SIGCTL_SCM);

	/* Set phase track alpha and beta */
	b43_phy_write(dev, B43_NPHY_PHASETR_A0, 0x125);
	b43_phy_write(dev, B43_NPHY_PHASETR_A1, 0x1B3);
	b43_phy_write(dev, B43_NPHY_PHASETR_A2, 0x105);
	b43_phy_write(dev, B43_NPHY_PHASETR_B0, 0x16E);
	b43_phy_write(dev, B43_NPHY_PHASETR_B1, 0xCD);
	b43_phy_write(dev, B43_NPHY_PHASETR_B2, 0x20);

	if (dev->phy.rev < 3) {
		b43_phy_mask(dev, B43_NPHY_PIL_DW1,
			     ~B43_NPHY_PIL_DW_64QAM & 0xFFFF);
		b43_phy_write(dev, B43_NPHY_TXF_20CO_S2B1, 0xB5);
		b43_phy_write(dev, B43_NPHY_TXF_20CO_S2B2, 0xA4);
		b43_phy_write(dev, B43_NPHY_TXF_20CO_S2B3, 0x00);
	}

	if (dev->phy.rev == 2)
		b43_phy_set(dev, B43_NPHY_FINERX2_CGC,
				B43_NPHY_FINERX2_CGC_DECGC);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/Workarounds */
static void b43_nphy_workarounds(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
		b43_nphy_classifier(dev, 1, 0);
	else
		b43_nphy_classifier(dev, 1, 1);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	b43_phy_set(dev, B43_NPHY_IQFLIP,
		    B43_NPHY_IQFLIP_ADC1 | B43_NPHY_IQFLIP_ADC2);

	/* TODO: rev19+ */
	if (dev->phy.rev >= 7)
		b43_nphy_workarounds_rev7plus(dev);
	else if (dev->phy.rev >= 3)
		b43_nphy_workarounds_rev3plus(dev);
	else
		b43_nphy_workarounds_rev1_2(dev);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/**************************************************
 * Tx/Rx common
 **************************************************/

/*
 * Transmits a known value for LO calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TXTone
 */
static int b43_nphy_tx_tone(struct b43_wldev *dev, u32 freq, u16 max_val,
			    bool iqmode, bool dac_test, bool modify_bbmult)
{
	u16 samp = b43_nphy_gen_load_samples(dev, freq, max_val, dac_test);
	if (samp == 0)
		return -1;
	b43_nphy_run_samples(dev, samp, 0xFFFF, 0, iqmode, dac_test,
			     modify_bbmult);
	return 0;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/Chains */
static void b43_nphy_update_txrx_chain(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	bool override = false;
	u16 chain = 0x33;

	if (nphy->txrx_chain == 0) {
		chain = 0x11;
		override = true;
	} else if (nphy->txrx_chain == 1) {
		chain = 0x22;
		override = true;
	}

	b43_phy_maskset(dev, B43_NPHY_RFSEQCA,
			~(B43_NPHY_RFSEQCA_TXEN | B43_NPHY_RFSEQCA_RXEN),
			chain);

	if (override)
		b43_phy_set(dev, B43_NPHY_RFSEQMODE,
				B43_NPHY_RFSEQMODE_CAOVER);
	else
		b43_phy_mask(dev, B43_NPHY_RFSEQMODE,
				~B43_NPHY_RFSEQMODE_CAOVER);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/stop-playback */
static void b43_nphy_stop_playback(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	u16 tmp;

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	tmp = b43_phy_read(dev, B43_NPHY_SAMP_STAT);
	if (tmp & 0x1)
		b43_phy_set(dev, B43_NPHY_SAMP_CMD, B43_NPHY_SAMP_CMD_STOP);
	else if (tmp & 0x2)
		b43_phy_mask(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0x7FFF);

	b43_phy_mask(dev, B43_NPHY_SAMP_CMD, ~0x0004);

	if (nphy->bb_mult_save & 0x80000000) {
		tmp = nphy->bb_mult_save & 0xFFFF;
		b43_ntab_write(dev, B43_NTAB16(15, 87), tmp);
		nphy->bb_mult_save = 0;
	}

	if (phy->rev >= 7) {
		if (phy->rev >= 19)
			b43_nphy_rf_ctl_override_rev19(dev, 0x80, 0, 0, true,
						       1);
		else
			b43_nphy_rf_ctl_override_rev7(dev, 0x80, 0, 0, true, 1);
		nphy->lpf_bw_overrode_for_sample_play = false;
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/IqCalGainParams */
static void b43_nphy_iq_cal_gain_params(struct b43_wldev *dev, u16 core,
					struct nphy_txgains target,
					struct nphy_iqcal_params *params)
{
	struct b43_phy *phy = &dev->phy;
	int i, j, indx;
	u16 gain;

	if (dev->phy.rev >= 3) {
		params->tx_lpf = target.tx_lpf[core]; /* Rev 7+ */
		params->txgm = target.txgm[core];
		params->pga = target.pga[core];
		params->pad = target.pad[core];
		params->ipa = target.ipa[core];
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 7) {
			params->cal_gain = (params->txgm << 12) | (params->pga << 8) | (params->pad << 3) | (params->ipa) | (params->tx_lpf << 15);
		} else {
			params->cal_gain = (params->txgm << 12) | (params->pga << 8) | (params->pad << 4) | (params->ipa);
		}
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

/**************************************************
 * Tx and Rx
 **************************************************/

static void b43_nphy_op_adjust_txpower(struct b43_wldev *dev)
{//TODO
}

static enum b43_txpwr_result b43_nphy_op_recalc_txpower(struct b43_wldev *dev,
							bool ignore_tssi)
{//TODO
	return B43_TXPWR_RES_DONE;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlEnable */
static void b43_nphy_tx_power_ctrl(struct b43_wldev *dev, bool enable)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	u8 i;
	u16 bmask, val, tmp;
	enum ieee80211_band band = b43_current_band(dev->wl);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	nphy->txpwrctrl = enable;
	if (!enable) {
		if (dev->phy.rev >= 3 &&
		    (b43_phy_read(dev, B43_NPHY_TXPCTL_CMD) &
		     (B43_NPHY_TXPCTL_CMD_COEFF |
		      B43_NPHY_TXPCTL_CMD_HWPCTLEN |
		      B43_NPHY_TXPCTL_CMD_PCTLEN))) {
			/* We disable enabled TX pwr ctl, save it's state */
			nphy->tx_pwr_idx[0] = b43_phy_read(dev,
						B43_NPHY_C1_TXPCTL_STAT) & 0x7f;
			nphy->tx_pwr_idx[1] = b43_phy_read(dev,
						B43_NPHY_C2_TXPCTL_STAT) & 0x7f;
		}

		b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x6840);
		for (i = 0; i < 84; i++)
			b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0);

		b43_phy_write(dev, B43_NPHY_TABLE_ADDR, 0x6C40);
		for (i = 0; i < 84; i++)
			b43_phy_write(dev, B43_NPHY_TABLE_DATALO, 0);

		tmp = B43_NPHY_TXPCTL_CMD_COEFF | B43_NPHY_TXPCTL_CMD_HWPCTLEN;
		if (dev->phy.rev >= 3)
			tmp |= B43_NPHY_TXPCTL_CMD_PCTLEN;
		b43_phy_mask(dev, B43_NPHY_TXPCTL_CMD, ~tmp);

		if (dev->phy.rev >= 3) {
			b43_phy_set(dev, B43_NPHY_AFECTL_OVER1, 0x0100);
			b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x0100);
		} else {
			b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x4000);
		}

		if (dev->phy.rev == 2)
			b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3,
				~B43_NPHY_BPHY_CTL3_SCALE, 0x53);
		else if (dev->phy.rev < 2)
			b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3,
				~B43_NPHY_BPHY_CTL3_SCALE, 0x5A);

		if (dev->phy.rev < 2 && b43_is_40mhz(dev))
			b43_hf_write(dev, b43_hf_read(dev) | B43_HF_TSSIRPSMW);
	} else {
		b43_ntab_write_bulk(dev, B43_NTAB16(26, 64), 84,
				    nphy->adj_pwr_tbl);
		b43_ntab_write_bulk(dev, B43_NTAB16(27, 64), 84,
				    nphy->adj_pwr_tbl);

		bmask = B43_NPHY_TXPCTL_CMD_COEFF |
			B43_NPHY_TXPCTL_CMD_HWPCTLEN;
		/* wl does useless check for "enable" param here */
		val = B43_NPHY_TXPCTL_CMD_COEFF | B43_NPHY_TXPCTL_CMD_HWPCTLEN;
		if (dev->phy.rev >= 3) {
			bmask |= B43_NPHY_TXPCTL_CMD_PCTLEN;
			if (val)
				val |= B43_NPHY_TXPCTL_CMD_PCTLEN;
		}
		b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD, ~(bmask), val);

		if (band == IEEE80211_BAND_5GHZ) {
			if (phy->rev >= 19) {
				/* TODO */
			} else if (phy->rev >= 7) {
				b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD,
						~B43_NPHY_TXPCTL_CMD_INIT,
						0x32);
				b43_phy_maskset(dev, B43_NPHY_TXPCTL_INIT,
						~B43_NPHY_TXPCTL_INIT_PIDXI1,
						0x32);
			} else {
				b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD,
						~B43_NPHY_TXPCTL_CMD_INIT,
						0x64);
				if (phy->rev > 1)
					b43_phy_maskset(dev,
							B43_NPHY_TXPCTL_INIT,
							~B43_NPHY_TXPCTL_INIT_PIDXI1,
							0x64);
			}
		}

		if (dev->phy.rev >= 3) {
			if (nphy->tx_pwr_idx[0] != 128 &&
			    nphy->tx_pwr_idx[1] != 128) {
				/* Recover TX pwr ctl state */
				b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD,
						~B43_NPHY_TXPCTL_CMD_INIT,
						nphy->tx_pwr_idx[0]);
				if (dev->phy.rev > 1)
					b43_phy_maskset(dev,
						B43_NPHY_TXPCTL_INIT,
						~0xff, nphy->tx_pwr_idx[1]);
			}
		}

		if (phy->rev >= 7) {
			/* TODO */
		}

		if (dev->phy.rev >= 3) {
			b43_phy_mask(dev, B43_NPHY_AFECTL_OVER1, ~0x100);
			b43_phy_mask(dev, B43_NPHY_AFECTL_OVER, ~0x100);
		} else {
			b43_phy_mask(dev, B43_NPHY_AFECTL_OVER, ~0x4000);
		}

		if (dev->phy.rev == 2)
			b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3, ~0xFF, 0x3b);
		else if (dev->phy.rev < 2)
			b43_phy_maskset(dev, B43_NPHY_BPHY_CTL3, ~0xFF, 0x40);

		if (dev->phy.rev < 2 && b43_is_40mhz(dev))
			b43_hf_write(dev, b43_hf_read(dev) & ~B43_HF_TSSIRPSMW);

		if (b43_nphy_ipa(dev)) {
			b43_phy_mask(dev, B43_NPHY_PAPD_EN0, ~0x4);
			b43_phy_mask(dev, B43_NPHY_PAPD_EN1, ~0x4);
		}
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrFix */
static void b43_nphy_tx_power_fix(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	u8 txpi[2], bbmult, i;
	u16 tmp, radio_gain, dac_gain;
	u16 freq = phy->chandef->chan->center_freq;
	u32 txgain;
	/* u32 gaintbl; rev3+ */

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	/* TODO: rev19+ */
	if (dev->phy.rev >= 7) {
		txpi[0] = txpi[1] = 30;
	} else if (dev->phy.rev >= 3) {
		txpi[0] = 40;
		txpi[1] = 40;
	} else if (sprom->revision < 4) {
		txpi[0] = 72;
		txpi[1] = 72;
	} else {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			txpi[0] = sprom->txpid2g[0];
			txpi[1] = sprom->txpid2g[1];
		} else if (freq >= 4900 && freq < 5100) {
			txpi[0] = sprom->txpid5gl[0];
			txpi[1] = sprom->txpid5gl[1];
		} else if (freq >= 5100 && freq < 5500) {
			txpi[0] = sprom->txpid5g[0];
			txpi[1] = sprom->txpid5g[1];
		} else if (freq >= 5500) {
			txpi[0] = sprom->txpid5gh[0];
			txpi[1] = sprom->txpid5gh[1];
		} else {
			txpi[0] = 91;
			txpi[1] = 91;
		}
	}
	if (dev->phy.rev < 7 &&
	    (txpi[0] < 40 || txpi[0] > 100 || txpi[1] < 40 || txpi[1] > 100))
		txpi[0] = txpi[1] = 91;

	/*
	for (i = 0; i < 2; i++) {
		nphy->txpwrindex[i].index_internal = txpi[i];
		nphy->txpwrindex[i].index_internal_save = txpi[i];
	}
	*/

	for (i = 0; i < 2; i++) {
		const u32 *table = b43_nphy_get_tx_gain_table(dev);

		if (!table)
			break;
		txgain = *(table + txpi[i]);

		if (dev->phy.rev >= 3)
			radio_gain = (txgain >> 16) & 0x1FFFF;
		else
			radio_gain = (txgain >> 16) & 0x1FFF;

		if (dev->phy.rev >= 7)
			dac_gain = (txgain >> 8) & 0x7;
		else
			dac_gain = (txgain >> 8) & 0x3F;
		bbmult = txgain & 0xFF;

		if (dev->phy.rev >= 3) {
			if (i == 0)
				b43_phy_set(dev, B43_NPHY_AFECTL_OVER1, 0x0100);
			else
				b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x0100);
		} else {
			b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x4000);
		}

		if (i == 0)
			b43_phy_write(dev, B43_NPHY_AFECTL_DACGAIN1, dac_gain);
		else
			b43_phy_write(dev, B43_NPHY_AFECTL_DACGAIN2, dac_gain);

		b43_ntab_write(dev, B43_NTAB16(0x7, 0x110 + i), radio_gain);

		tmp = b43_ntab_read(dev, B43_NTAB16(0xF, 0x57));
		if (i == 0)
			tmp = (tmp & 0x00FF) | (bbmult << 8);
		else
			tmp = (tmp & 0xFF00) | bbmult;
		b43_ntab_write(dev, B43_NTAB16(0xF, 0x57), tmp);

		if (b43_nphy_ipa(dev)) {
			u32 tmp32;
			u16 reg = (i == 0) ?
				B43_NPHY_PAPD_EN0 : B43_NPHY_PAPD_EN1;
			tmp32 = b43_ntab_read(dev, B43_NTAB32(26 + i,
							      576 + txpi[i]));
			b43_phy_maskset(dev, reg, 0xE00F, (u32) tmp32 << 4);
			b43_phy_set(dev, reg, 0x4);
		}
	}

	b43_phy_mask(dev, B43_NPHY_BPHY_CTL2, ~B43_NPHY_BPHY_CTL2_LUT);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

static void b43_nphy_ipa_internal_tssi_setup(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	u8 core;
	u16 r; /* routing */

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		for (core = 0; core < 2; core++) {
			r = core ? 0x190 : 0x170;
			if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
				b43_radio_write(dev, r + 0x5, 0x5);
				b43_radio_write(dev, r + 0x9, 0xE);
				if (phy->rev != 5)
					b43_radio_write(dev, r + 0xA, 0);
				if (phy->rev != 7)
					b43_radio_write(dev, r + 0xB, 1);
				else
					b43_radio_write(dev, r + 0xB, 0x31);
			} else {
				b43_radio_write(dev, r + 0x5, 0x9);
				b43_radio_write(dev, r + 0x9, 0xC);
				b43_radio_write(dev, r + 0xB, 0x0);
				if (phy->rev != 5)
					b43_radio_write(dev, r + 0xA, 1);
				else
					b43_radio_write(dev, r + 0xA, 0x31);
			}
			b43_radio_write(dev, r + 0x6, 0);
			b43_radio_write(dev, r + 0x7, 0);
			b43_radio_write(dev, r + 0x8, 3);
			b43_radio_write(dev, r + 0xC, 0);
		}
	} else {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
			b43_radio_write(dev, B2056_SYN_RESERVED_ADDR31, 0x128);
		else
			b43_radio_write(dev, B2056_SYN_RESERVED_ADDR31, 0x80);
		b43_radio_write(dev, B2056_SYN_RESERVED_ADDR30, 0);
		b43_radio_write(dev, B2056_SYN_GPIO_MASTER1, 0x29);

		for (core = 0; core < 2; core++) {
			r = core ? B2056_TX1 : B2056_TX0;

			b43_radio_write(dev, r | B2056_TX_IQCAL_VCM_HG, 0);
			b43_radio_write(dev, r | B2056_TX_IQCAL_IDAC, 0);
			b43_radio_write(dev, r | B2056_TX_TSSI_VCM, 3);
			b43_radio_write(dev, r | B2056_TX_TX_AMP_DET, 0);
			b43_radio_write(dev, r | B2056_TX_TSSI_MISC1, 8);
			b43_radio_write(dev, r | B2056_TX_TSSI_MISC2, 0);
			b43_radio_write(dev, r | B2056_TX_TSSI_MISC3, 0);
			if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
				b43_radio_write(dev, r | B2056_TX_TX_SSI_MASTER,
						0x5);
				if (phy->rev != 5)
					b43_radio_write(dev, r | B2056_TX_TSSIA,
							0x00);
				if (phy->rev >= 5)
					b43_radio_write(dev, r | B2056_TX_TSSIG,
							0x31);
				else
					b43_radio_write(dev, r | B2056_TX_TSSIG,
							0x11);
				b43_radio_write(dev, r | B2056_TX_TX_SSI_MUX,
						0xE);
			} else {
				b43_radio_write(dev, r | B2056_TX_TX_SSI_MASTER,
						0x9);
				b43_radio_write(dev, r | B2056_TX_TSSIA, 0x31);
				b43_radio_write(dev, r | B2056_TX_TSSIG, 0x0);
				b43_radio_write(dev, r | B2056_TX_TX_SSI_MUX,
						0xC);
			}
		}
	}
}

/*
 * Stop radio and transmit known signal. Then check received signal strength to
 * get TSSI (Transmit Signal Strength Indicator).
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlIdleTssi
 */
static void b43_nphy_tx_power_ctl_idle_tssi(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;

	u32 tmp;
	s32 rssi[4] = { };

	/* TODO: check if we can transmit */

	if (b43_nphy_ipa(dev))
		b43_nphy_ipa_internal_tssi_setup(dev);

	if (phy->rev >= 19)
		b43_nphy_rf_ctl_override_rev19(dev, 0x2000, 0, 3, false, 0);
	else if (phy->rev >= 7)
		b43_nphy_rf_ctl_override_rev7(dev, 0x2000, 0, 3, false, 0);
	else if (phy->rev >= 3)
		b43_nphy_rf_ctl_override(dev, 0x2000, 0, 3, false);

	b43_nphy_stop_playback(dev);
	b43_nphy_tx_tone(dev, 4000, 0, false, false, false);
	udelay(20);
	tmp = b43_nphy_poll_rssi(dev, N_RSSI_TSSI_2G, rssi, 1);
	b43_nphy_stop_playback(dev);

	b43_nphy_rssi_select(dev, 0, N_RSSI_W1);

	if (phy->rev >= 19)
		b43_nphy_rf_ctl_override_rev19(dev, 0x2000, 0, 3, true, 0);
	else if (phy->rev >= 7)
		b43_nphy_rf_ctl_override_rev7(dev, 0x2000, 0, 3, true, 0);
	else if (phy->rev >= 3)
		b43_nphy_rf_ctl_override(dev, 0x2000, 0, 3, true);

	if (phy->rev >= 19) {
		/* TODO */
		return;
	} else if (phy->rev >= 3) {
		nphy->pwr_ctl_info[0].idle_tssi_5g = (tmp >> 24) & 0xFF;
		nphy->pwr_ctl_info[1].idle_tssi_5g = (tmp >> 8) & 0xFF;
	} else {
		nphy->pwr_ctl_info[0].idle_tssi_5g = (tmp >> 16) & 0xFF;
		nphy->pwr_ctl_info[1].idle_tssi_5g = tmp & 0xFF;
	}
	nphy->pwr_ctl_info[0].idle_tssi_2g = (tmp >> 24) & 0xFF;
	nphy->pwr_ctl_info[1].idle_tssi_2g = (tmp >> 8) & 0xFF;
}

/* http://bcm-v4.sipsolutions.net/PHY/N/TxPwrLimitToTbl */
static void b43_nphy_tx_prepare_adjusted_power_table(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u8 idx, delta;
	u8 i, stf_mode;

	/* Array adj_pwr_tbl corresponds to the hardware table. It consists of
	 * 21 groups, each containing 4 entries.
	 *
	 * First group has entries for CCK modulation.
	 * The rest of groups has 1 entry per modulation (SISO, CDD, STBC, SDM).
	 *
	 * Group 0 is for CCK
	 * Groups 1..4 use BPSK (group per coding rate)
	 * Groups 5..8 use QPSK (group per coding rate)
	 * Groups 9..12 use 16-QAM (group per coding rate)
	 * Groups 13..16 use 64-QAM (group per coding rate)
	 * Groups 17..20 are unknown
	 */

	for (i = 0; i < 4; i++)
		nphy->adj_pwr_tbl[i] = nphy->tx_power_offset[i];

	for (stf_mode = 0; stf_mode < 4; stf_mode++) {
		delta = 0;
		switch (stf_mode) {
		case 0:
			if (b43_is_40mhz(dev) && dev->phy.rev >= 5) {
				idx = 68;
			} else {
				delta = 1;
				idx = b43_is_40mhz(dev) ? 52 : 4;
			}
			break;
		case 1:
			idx = b43_is_40mhz(dev) ? 76 : 28;
			break;
		case 2:
			idx = b43_is_40mhz(dev) ? 84 : 36;
			break;
		case 3:
			idx = b43_is_40mhz(dev) ? 92 : 44;
			break;
		}

		for (i = 0; i < 20; i++) {
			nphy->adj_pwr_tbl[4 + 4 * i + stf_mode] =
				nphy->tx_power_offset[idx];
			if (i == 0)
				idx += delta;
			if (i == 14)
				idx += 1 - delta;
			if (i == 3 || i == 4 || i == 7 || i == 8 || i == 11 ||
			    i == 13)
				idx += 1;
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlSetup */
static void b43_nphy_tx_power_ctl_setup(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	s16 a1[2], b0[2], b1[2];
	u8 idle[2];
	s8 target[2];
	s32 num, den, pwr;
	u32 regval[64];

	u16 freq = phy->chandef->chan->center_freq;
	u16 tmp;
	u16 r; /* routing */
	u8 i, c;

	if (dev->dev->core_rev == 11 || dev->dev->core_rev == 12) {
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0, 0x200000);
		b43_read32(dev, B43_MMIO_MACCTL);
		udelay(1);
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, true);

	b43_phy_set(dev, B43_NPHY_TSSIMODE, B43_NPHY_TSSIMODE_EN);
	if (dev->phy.rev >= 3)
		b43_phy_mask(dev, B43_NPHY_TXPCTL_CMD,
			     ~B43_NPHY_TXPCTL_CMD_PCTLEN & 0xFFFF);
	else
		b43_phy_set(dev, B43_NPHY_TXPCTL_CMD,
			    B43_NPHY_TXPCTL_CMD_PCTLEN);

	if (dev->dev->core_rev == 11 || dev->dev->core_rev == 12)
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0x200000, 0);

	if (sprom->revision < 4) {
		idle[0] = nphy->pwr_ctl_info[0].idle_tssi_2g;
		idle[1] = nphy->pwr_ctl_info[1].idle_tssi_2g;
		target[0] = target[1] = 52;
		a1[0] = a1[1] = -424;
		b0[0] = b0[1] = 5612;
		b1[0] = b1[1] = -1393;
	} else {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_2g;
				target[c] = sprom->core_pwr_info[c].maxpwr_2g;
				a1[c] = sprom->core_pwr_info[c].pa_2g[0];
				b0[c] = sprom->core_pwr_info[c].pa_2g[1];
				b1[c] = sprom->core_pwr_info[c].pa_2g[2];
			}
		} else if (freq >= 4900 && freq < 5100) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = sprom->core_pwr_info[c].maxpwr_5gl;
				a1[c] = sprom->core_pwr_info[c].pa_5gl[0];
				b0[c] = sprom->core_pwr_info[c].pa_5gl[1];
				b1[c] = sprom->core_pwr_info[c].pa_5gl[2];
			}
		} else if (freq >= 5100 && freq < 5500) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = sprom->core_pwr_info[c].maxpwr_5g;
				a1[c] = sprom->core_pwr_info[c].pa_5g[0];
				b0[c] = sprom->core_pwr_info[c].pa_5g[1];
				b1[c] = sprom->core_pwr_info[c].pa_5g[2];
			}
		} else if (freq >= 5500) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = sprom->core_pwr_info[c].maxpwr_5gh;
				a1[c] = sprom->core_pwr_info[c].pa_5gh[0];
				b0[c] = sprom->core_pwr_info[c].pa_5gh[1];
				b1[c] = sprom->core_pwr_info[c].pa_5gh[2];
			}
		} else {
			idle[0] = nphy->pwr_ctl_info[0].idle_tssi_5g;
			idle[1] = nphy->pwr_ctl_info[1].idle_tssi_5g;
			target[0] = target[1] = 52;
			a1[0] = a1[1] = -424;
			b0[0] = b0[1] = 5612;
			b1[0] = b1[1] = -1393;
		}
	}
	/* target[0] = target[1] = nphy->tx_power_max; */

	if (dev->phy.rev >= 3) {
		if (sprom->fem.ghz2.tssipos)
			b43_phy_set(dev, B43_NPHY_TXPCTL_ITSSI, 0x4000);
		if (dev->phy.rev >= 7) {
			for (c = 0; c < 2; c++) {
				r = c ? 0x190 : 0x170;
				if (b43_nphy_ipa(dev))
					b43_radio_write(dev, r + 0x9, (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) ? 0xE : 0xC);
			}
		} else {
			if (b43_nphy_ipa(dev)) {
				tmp = (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) ? 0xC : 0xE;
				b43_radio_write(dev,
					B2056_TX0 | B2056_TX_TX_SSI_MUX, tmp);
				b43_radio_write(dev,
					B2056_TX1 | B2056_TX_TX_SSI_MUX, tmp);
			} else {
				b43_radio_write(dev,
					B2056_TX0 | B2056_TX_TX_SSI_MUX, 0x11);
				b43_radio_write(dev,
					B2056_TX1 | B2056_TX_TX_SSI_MUX, 0x11);
			}
		}
	}

	if (dev->dev->core_rev == 11 || dev->dev->core_rev == 12) {
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0, 0x200000);
		b43_read32(dev, B43_MMIO_MACCTL);
		udelay(1);
	}

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD,
				~B43_NPHY_TXPCTL_CMD_INIT, 0x19);
		b43_phy_maskset(dev, B43_NPHY_TXPCTL_INIT,
				~B43_NPHY_TXPCTL_INIT_PIDXI1, 0x19);
	} else {
		b43_phy_maskset(dev, B43_NPHY_TXPCTL_CMD,
				~B43_NPHY_TXPCTL_CMD_INIT, 0x40);
		if (dev->phy.rev > 1)
			b43_phy_maskset(dev, B43_NPHY_TXPCTL_INIT,
				~B43_NPHY_TXPCTL_INIT_PIDXI1, 0x40);
	}

	if (dev->dev->core_rev == 11 || dev->dev->core_rev == 12)
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0x200000, 0);

	b43_phy_write(dev, B43_NPHY_TXPCTL_N,
		      0xF0 << B43_NPHY_TXPCTL_N_TSSID_SHIFT |
		      3 << B43_NPHY_TXPCTL_N_NPTIL2_SHIFT);
	b43_phy_write(dev, B43_NPHY_TXPCTL_ITSSI,
		      idle[0] << B43_NPHY_TXPCTL_ITSSI_0_SHIFT |
		      idle[1] << B43_NPHY_TXPCTL_ITSSI_1_SHIFT |
		      B43_NPHY_TXPCTL_ITSSI_BINF);
	b43_phy_write(dev, B43_NPHY_TXPCTL_TPWR,
		      target[0] << B43_NPHY_TXPCTL_TPWR_0_SHIFT |
		      target[1] << B43_NPHY_TXPCTL_TPWR_1_SHIFT);

	for (c = 0; c < 2; c++) {
		for (i = 0; i < 64; i++) {
			num = 8 * (16 * b0[c] + b1[c] * i);
			den = 32768 + a1[c] * i;
			pwr = max((4 * num + den / 2) / den, -8);
			if (dev->phy.rev < 3 && (i <= (31 - idle[c] + 1)))
				pwr = max(pwr, target[c] + 1);
			regval[i] = pwr;
		}
		b43_ntab_write_bulk(dev, B43_NTAB32(26 + c, 0), 64, regval);
	}

	b43_nphy_tx_prepare_adjusted_power_table(dev);
	b43_ntab_write_bulk(dev, B43_NTAB16(26, 64), 84, nphy->adj_pwr_tbl);
	b43_ntab_write_bulk(dev, B43_NTAB16(27, 64), 84, nphy->adj_pwr_tbl);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, false);
}

static void b43_nphy_tx_gain_table_upload(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	const u32 *table = NULL;
	u32 rfpwr_offset;
	u8 pga_gain;
	int i;

	table = b43_nphy_get_tx_gain_table(dev);
	if (!table)
		return;

	b43_ntab_write_bulk(dev, B43_NTAB32(26, 192), 128, table);
	b43_ntab_write_bulk(dev, B43_NTAB32(27, 192), 128, table);

	if (phy->rev < 3)
		return;

#if 0
	nphy->gmval = (table[0] >> 16) & 0x7000;
#endif

	for (i = 0; i < 128; i++) {
		if (phy->rev >= 19) {
			/* TODO */
			return;
		} else if (phy->rev >= 7) {
			/* TODO */
			return;
		} else {
			pga_gain = (table[i] >> 24) & 0xF;
			if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
				rfpwr_offset = b43_ntab_papd_pga_gain_delta_ipa_2g[pga_gain];
			else
				rfpwr_offset = 0; /* FIXME */
		}

		b43_ntab_write(dev, B43_NTAB32(26, 576 + i), rfpwr_offset);
		b43_ntab_write(dev, B43_NTAB32(27, 576 + i), rfpwr_offset);
	}
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
		if (dev->phy.rev >= 7) {
			tmp = 0x1480;
		} else if (dev->phy.rev >= 3) {
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

/*
 * TX low-pass filter bandwidth setup
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxLpFbw
 */
static void b43_nphy_tx_lpf_bw(struct b43_wldev *dev)
{
	u16 tmp;

	if (dev->phy.rev < 3 || dev->phy.rev >= 7)
		return;

	if (b43_nphy_ipa(dev))
		tmp = b43_is_40mhz(dev) ? 5 : 4;
	else
		tmp = b43_is_40mhz(dev) ? 3 : 1;
	b43_phy_write(dev, B43_NPHY_TXF_40CO_B32S2,
		      (tmp << 9) | (tmp << 6) | (tmp << 3) | tmp);

	if (b43_nphy_ipa(dev)) {
		tmp = b43_is_40mhz(dev) ? 4 : 1;
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S2,
			      (tmp << 9) | (tmp << 6) | (tmp << 3) | tmp);
	}
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

#if 0
/* Ready but not used anywhere */
/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCalPhyCleanup */
static void b43_nphy_rx_cal_phy_cleanup(struct b43_wldev *dev, u8 core)
{
	u16 *regs = dev->phy.n->tx_rx_cal_phy_saveregs;

	b43_phy_write(dev, B43_NPHY_RFSEQCA, regs[0]);
	if (core == 0) {
		b43_phy_write(dev, B43_NPHY_AFECTL_C1, regs[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, regs[2]);
	} else {
		b43_phy_write(dev, B43_NPHY_AFECTL_C2, regs[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, regs[2]);
	}
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, regs[3]);
	b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, regs[4]);
	b43_phy_write(dev, B43_NPHY_RFCTL_RSSIO1, regs[5]);
	b43_phy_write(dev, B43_NPHY_RFCTL_RSSIO2, regs[6]);
	b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S1, regs[7]);
	b43_phy_write(dev, B43_NPHY_RFCTL_OVER, regs[8]);
	b43_phy_write(dev, B43_NPHY_PAPD_EN0, regs[9]);
	b43_phy_write(dev, B43_NPHY_PAPD_EN1, regs[10]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCalPhySetup */
static void b43_nphy_rx_cal_phy_setup(struct b43_wldev *dev, u8 core)
{
	u8 rxval, txval;
	u16 *regs = dev->phy.n->tx_rx_cal_phy_saveregs;

	regs[0] = b43_phy_read(dev, B43_NPHY_RFSEQCA);
	if (core == 0) {
		regs[1] = b43_phy_read(dev, B43_NPHY_AFECTL_C1);
		regs[2] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER1);
	} else {
		regs[1] = b43_phy_read(dev, B43_NPHY_AFECTL_C2);
		regs[2] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
	}
	regs[3] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC1);
	regs[4] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);
	regs[5] = b43_phy_read(dev, B43_NPHY_RFCTL_RSSIO1);
	regs[6] = b43_phy_read(dev, B43_NPHY_RFCTL_RSSIO2);
	regs[7] = b43_phy_read(dev, B43_NPHY_TXF_40CO_B1S1);
	regs[8] = b43_phy_read(dev, B43_NPHY_RFCTL_OVER);
	regs[9] = b43_phy_read(dev, B43_NPHY_PAPD_EN0);
	regs[10] = b43_phy_read(dev, B43_NPHY_PAPD_EN1);

	b43_phy_mask(dev, B43_NPHY_PAPD_EN0, ~0x0001);
	b43_phy_mask(dev, B43_NPHY_PAPD_EN1, ~0x0001);

	b43_phy_maskset(dev, B43_NPHY_RFSEQCA,
			~B43_NPHY_RFSEQCA_RXDIS & 0xFFFF,
			((1 - core) << B43_NPHY_RFSEQCA_RXDIS_SHIFT));
	b43_phy_maskset(dev, B43_NPHY_RFSEQCA, ~B43_NPHY_RFSEQCA_TXEN,
			((1 - core) << B43_NPHY_RFSEQCA_TXEN_SHIFT));
	b43_phy_maskset(dev, B43_NPHY_RFSEQCA, ~B43_NPHY_RFSEQCA_RXEN,
			(core << B43_NPHY_RFSEQCA_RXEN_SHIFT));
	b43_phy_maskset(dev, B43_NPHY_RFSEQCA, ~B43_NPHY_RFSEQCA_TXDIS,
			(core << B43_NPHY_RFSEQCA_TXDIS_SHIFT));

	if (core == 0) {
		b43_phy_mask(dev, B43_NPHY_AFECTL_C1, ~0x0007);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER1, 0x0007);
	} else {
		b43_phy_mask(dev, B43_NPHY_AFECTL_C2, ~0x0007);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x0007);
	}

	b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_PA, 0, 3);
	b43_nphy_rf_ctl_override(dev, 8, 0, 3, false);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RX2TX);

	if (core == 0) {
		rxval = 1;
		txval = 8;
	} else {
		rxval = 4;
		txval = 2;
	}
	b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_TRSW, rxval,
				      core + 1);
	b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_TRSW, txval,
				      2 - core);
}
#endif

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
	b43_ntab_read_bulk(dev, B43_NTAB16(0xF, 0x50), 4, array);

	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW0, array[0]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW1, array[1]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW2, array[2]);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_NPHY_TXIQW3, array[3]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SpurWar */
static void b43_nphy_spur_workaround(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u8 channel = dev->phy.channel;
	int tone[2] = { 57, 58 };
	u32 noise[2] = { 0x3FF, 0x3FF };

	B43_WARN_ON(dev->phy.rev < 3);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	if (nphy->gband_spurwar_en) {
		/* TODO: N PHY Adjust Analog Pfbw (7) */
		if (channel == 11 && b43_is_40mhz(dev))
			; /* TODO: N PHY Adjust Min Noise Var(2, tone, noise)*/
		else
			; /* TODO: N PHY Adjust Min Noise Var(0, NULL, NULL)*/
		/* TODO: N PHY Adjust CRS Min Power (0x1E) */
	}

	if (nphy->aband_spurwar_en) {
		if (channel == 54) {
			tone[0] = 0x20;
			noise[0] = 0x25F;
		} else if (channel == 38 || channel == 102 || channel == 118) {
			if (0 /* FIXME */) {
				tone[0] = 0x20;
				noise[0] = 0x21F;
			} else {
				tone[0] = 0;
				noise[0] = 0;
			}
		} else if (channel == 134) {
			tone[0] = 0x20;
			noise[0] = 0x21F;
		} else if (channel == 151) {
			tone[0] = 0x10;
			noise[0] = 0x23F;
		} else if (channel == 153 || channel == 161) {
			tone[0] = 0x30;
			noise[0] = 0x23F;
		} else {
			tone[0] = 0;
			noise[0] = 0;
		}

		if (!tone[0] && !noise[0])
			; /* TODO: N PHY Adjust Min Noise Var(1, tone, noise)*/
		else
			; /* TODO: N PHY Adjust Min Noise Var(0, NULL, NULL)*/
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlCoefSetup */
static void b43_nphy_tx_pwr_ctrl_coef_setup(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;
	int i, j;
	u32 tmp;
	u32 cur_real, cur_imag, real_part, imag_part;

	u16 buffer[7];

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, true);

	b43_ntab_read_bulk(dev, B43_NTAB16(15, 80), 7, buffer);

	for (i = 0; i < 2; i++) {
		tmp = ((buffer[i * 2] & 0x3FF) << 10) |
			(buffer[i * 2 + 1] & 0x3FF);
		b43_phy_write(dev, B43_NPHY_TABLE_ADDR,
				(((i + 26) << 10) | 320));
		for (j = 0; j < 128; j++) {
			b43_phy_write(dev, B43_NPHY_TABLE_DATAHI,
					((tmp >> 16) & 0xFFFF));
			b43_phy_write(dev, B43_NPHY_TABLE_DATALO,
					(tmp & 0xFFFF));
		}
	}

	for (i = 0; i < 2; i++) {
		tmp = buffer[5 + i];
		real_part = (tmp >> 8) & 0xFF;
		imag_part = (tmp & 0xFF);
		b43_phy_write(dev, B43_NPHY_TABLE_ADDR,
				(((i + 26) << 10) | 448));

		if (dev->phy.rev >= 3) {
			cur_real = real_part;
			cur_imag = imag_part;
			tmp = ((cur_real & 0xFF) << 8) | (cur_imag & 0xFF);
		}

		for (j = 0; j < 128; j++) {
			if (dev->phy.rev < 3) {
				cur_real = (real_part * loscale[j] + 128) >> 8;
				cur_imag = (imag_part * loscale[j] + 128) >> 8;
				tmp = ((cur_real & 0xFF) << 8) |
					(cur_imag & 0xFF);
			}
			b43_phy_write(dev, B43_NPHY_TABLE_DATAHI,
					((tmp >> 16) & 0xFFFF));
			b43_phy_write(dev, B43_NPHY_TABLE_DATALO,
					(tmp & 0xFFFF));
		}
	}

	if (dev->phy.rev >= 3) {
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_NPHY_TXPWR_INDX0, 0xFFFF);
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_NPHY_TXPWR_INDX1, 0xFFFF);
	}

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, false);
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
		if (!nphy->rssical_chanspec_2G.center_freq)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_2G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_2G;
	} else {
		if (!nphy->rssical_chanspec_5G.center_freq)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_5G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_5G;
	}

	if (dev->phy.rev >= 19) {
		/* TODO */
	} else if (dev->phy.rev >= 7) {
		b43_radio_maskset(dev, R2057_NB_MASTER_CORE0, ~R2057_VCM_MASK,
				  rssical_radio_regs[0]);
		b43_radio_maskset(dev, R2057_NB_MASTER_CORE1, ~R2057_VCM_MASK,
				  rssical_radio_regs[1]);
	} else {
		b43_radio_maskset(dev, B2056_RX0 | B2056_RX_RSSI_MISC, 0xE3,
				  rssical_radio_regs[0]);
		b43_radio_maskset(dev, B2056_RX1 | B2056_RX_RSSI_MISC, 0xE3,
				  rssical_radio_regs[1]);
	}

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

static void b43_nphy_tx_cal_radio_setup_rev19(struct b43_wldev *dev)
{
	/* TODO */
}

static void b43_nphy_tx_cal_radio_setup_rev7(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	u16 *save = nphy->tx_rx_cal_radio_saveregs;
	int core, off;
	u16 r, tmp;

	for (core = 0; core < 2; core++) {
		r = core ? 0x20 : 0;
		off = core * 11;

		save[off + 0] = b43_radio_read(dev, r + R2057_TX0_TX_SSI_MASTER);
		save[off + 1] = b43_radio_read(dev, r + R2057_TX0_IQCAL_VCM_HG);
		save[off + 2] = b43_radio_read(dev, r + R2057_TX0_IQCAL_IDAC);
		save[off + 3] = b43_radio_read(dev, r + R2057_TX0_TSSI_VCM);
		save[off + 4] = 0;
		save[off + 5] = b43_radio_read(dev, r + R2057_TX0_TX_SSI_MUX);
		if (phy->radio_rev != 5)
			save[off + 6] = b43_radio_read(dev, r + R2057_TX0_TSSIA);
		save[off + 7] = b43_radio_read(dev, r + R2057_TX0_TSSIG);
		save[off + 8] = b43_radio_read(dev, r + R2057_TX0_TSSI_MISC1);

		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			b43_radio_write(dev, r + R2057_TX0_TX_SSI_MASTER, 0xA);
			b43_radio_write(dev, r + R2057_TX0_IQCAL_VCM_HG, 0x43);
			b43_radio_write(dev, r + R2057_TX0_IQCAL_IDAC, 0x55);
			b43_radio_write(dev, r + R2057_TX0_TSSI_VCM, 0);
			b43_radio_write(dev, r + R2057_TX0_TSSIG, 0);
			if (nphy->use_int_tx_iq_lo_cal) {
				b43_radio_write(dev, r + R2057_TX0_TX_SSI_MUX, 0x4);
				tmp = true ? 0x31 : 0x21; /* TODO */
				b43_radio_write(dev, r + R2057_TX0_TSSIA, tmp);
			}
			b43_radio_write(dev, r + R2057_TX0_TSSI_MISC1, 0x00);
		} else {
			b43_radio_write(dev, r + R2057_TX0_TX_SSI_MASTER, 0x6);
			b43_radio_write(dev, r + R2057_TX0_IQCAL_VCM_HG, 0x43);
			b43_radio_write(dev, r + R2057_TX0_IQCAL_IDAC, 0x55);
			b43_radio_write(dev, r + R2057_TX0_TSSI_VCM, 0);

			if (phy->radio_rev != 5)
				b43_radio_write(dev, r + R2057_TX0_TSSIA, 0);
			if (nphy->use_int_tx_iq_lo_cal) {
				b43_radio_write(dev, r + R2057_TX0_TX_SSI_MUX, 0x6);
				tmp = true ? 0x31 : 0x21; /* TODO */
				b43_radio_write(dev, r + R2057_TX0_TSSIG, tmp);
			}
			b43_radio_write(dev, r + R2057_TX0_TSSI_MISC1, 0);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalRadioSetup */
static void b43_nphy_tx_cal_radio_setup(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	u16 *save = nphy->tx_rx_cal_radio_saveregs;
	u16 tmp;
	u8 offset, i;

	if (phy->rev >= 19) {
		b43_nphy_tx_cal_radio_setup_rev19(dev);
	} else if (phy->rev >= 7) {
		b43_nphy_tx_cal_radio_setup_rev7(dev);
	} else if (phy->rev >= 3) {
	    for (i = 0; i < 2; i++) {
		tmp = (i == 0) ? 0x2000 : 0x3000;
		offset = i * 11;

		save[offset + 0] = b43_radio_read(dev, B2055_CAL_RVARCTL);
		save[offset + 1] = b43_radio_read(dev, B2055_CAL_LPOCTL);
		save[offset + 2] = b43_radio_read(dev, B2055_CAL_TS);
		save[offset + 3] = b43_radio_read(dev, B2055_CAL_RCCALRTS);
		save[offset + 4] = b43_radio_read(dev, B2055_CAL_RCALRTS);
		save[offset + 5] = b43_radio_read(dev, B2055_PADDRV);
		save[offset + 6] = b43_radio_read(dev, B2055_XOCTL1);
		save[offset + 7] = b43_radio_read(dev, B2055_XOCTL2);
		save[offset + 8] = b43_radio_read(dev, B2055_XOREGUL);
		save[offset + 9] = b43_radio_read(dev, B2055_XOMISC);
		save[offset + 10] = b43_radio_read(dev, B2055_PLL_LFC1);

		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			b43_radio_write(dev, tmp | B2055_CAL_RVARCTL, 0x0A);
			b43_radio_write(dev, tmp | B2055_CAL_LPOCTL, 0x40);
			b43_radio_write(dev, tmp | B2055_CAL_TS, 0x55);
			b43_radio_write(dev, tmp | B2055_CAL_RCCALRTS, 0);
			b43_radio_write(dev, tmp | B2055_CAL_RCALRTS, 0);
			if (nphy->ipa5g_on) {
				b43_radio_write(dev, tmp | B2055_PADDRV, 4);
				b43_radio_write(dev, tmp | B2055_XOCTL1, 1);
			} else {
				b43_radio_write(dev, tmp | B2055_PADDRV, 0);
				b43_radio_write(dev, tmp | B2055_XOCTL1, 0x2F);
			}
			b43_radio_write(dev, tmp | B2055_XOCTL2, 0);
		} else {
			b43_radio_write(dev, tmp | B2055_CAL_RVARCTL, 0x06);
			b43_radio_write(dev, tmp | B2055_CAL_LPOCTL, 0x40);
			b43_radio_write(dev, tmp | B2055_CAL_TS, 0x55);
			b43_radio_write(dev, tmp | B2055_CAL_RCCALRTS, 0);
			b43_radio_write(dev, tmp | B2055_CAL_RCALRTS, 0);
			b43_radio_write(dev, tmp | B2055_XOCTL1, 0);
			if (nphy->ipa2g_on) {
				b43_radio_write(dev, tmp | B2055_PADDRV, 6);
				b43_radio_write(dev, tmp | B2055_XOCTL2,
					(dev->phy.rev < 5) ? 0x11 : 0x01);
			} else {
				b43_radio_write(dev, tmp | B2055_PADDRV, 0);
				b43_radio_write(dev, tmp | B2055_XOCTL2, 0);
			}
		}
		b43_radio_write(dev, tmp | B2055_XOREGUL, 0);
		b43_radio_write(dev, tmp | B2055_XOMISC, 0);
		b43_radio_write(dev, tmp | B2055_PLL_LFC1, 0);
	    }
	} else {
		save[0] = b43_radio_read(dev, B2055_C1_TX_RF_IQCAL1);
		b43_radio_write(dev, B2055_C1_TX_RF_IQCAL1, 0x29);

		save[1] = b43_radio_read(dev, B2055_C1_TX_RF_IQCAL2);
		b43_radio_write(dev, B2055_C1_TX_RF_IQCAL2, 0x54);

		save[2] = b43_radio_read(dev, B2055_C2_TX_RF_IQCAL1);
		b43_radio_write(dev, B2055_C2_TX_RF_IQCAL1, 0x29);

		save[3] = b43_radio_read(dev, B2055_C2_TX_RF_IQCAL2);
		b43_radio_write(dev, B2055_C2_TX_RF_IQCAL2, 0x54);

		save[3] = b43_radio_read(dev, B2055_C1_PWRDET_RXTX);
		save[4] = b43_radio_read(dev, B2055_C2_PWRDET_RXTX);

		if (!(b43_phy_read(dev, B43_NPHY_BANDCTL) &
		    B43_NPHY_BANDCTL_5GHZ)) {
			b43_radio_write(dev, B2055_C1_PWRDET_RXTX, 0x04);
			b43_radio_write(dev, B2055_C2_PWRDET_RXTX, 0x04);
		} else {
			b43_radio_write(dev, B2055_C1_PWRDET_RXTX, 0x20);
			b43_radio_write(dev, B2055_C2_PWRDET_RXTX, 0x20);
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
		b43_ntab_write(dev, B43_NTAB16(15, i), entry);

		scale = (ladder_iq[i].percent * tmp) / 100;
		entry = ((scale & 0xFF) << 8) | ladder_iq[i].g_env;
		b43_ntab_write(dev, B43_NTAB16(15, i + 32), entry);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ExtPaSetTxDigiFilts */
static void b43_nphy_ext_pa_set_tx_dig_filters(struct b43_wldev *dev)
{
	int i;
	for (i = 0; i < 15; i++)
		b43_phy_write(dev, B43_PHY_N(0x2C5 + i),
				tbl_tx_filter_coef_rev4[2][i]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/IpaSetTxDigiFilts */
static void b43_nphy_int_pa_set_tx_dig_filters(struct b43_wldev *dev)
{
	int i, j;
	/* B43_NPHY_TXF_20CO_S0A1, B43_NPHY_TXF_40CO_S0A1, unknown */
	static const u16 offset[] = { 0x186, 0x195, 0x2C5 };

	for (i = 0; i < 3; i++)
		for (j = 0; j < 15; j++)
			b43_phy_write(dev, B43_PHY_N(offset[i] + j),
					tbl_tx_filter_coef_rev4[i][j]);

	if (b43_is_40mhz(dev)) {
		for (j = 0; j < 15; j++)
			b43_phy_write(dev, B43_PHY_N(offset[0] + j),
					tbl_tx_filter_coef_rev4[3][j]);
	} else if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
		for (j = 0; j < 15; j++)
			b43_phy_write(dev, B43_PHY_N(offset[0] + j),
					tbl_tx_filter_coef_rev4[5][j]);
	}

	if (dev->phy.channel == 14)
		for (j = 0; j < 15; j++)
			b43_phy_write(dev, B43_PHY_N(offset[0] + j),
					tbl_tx_filter_coef_rev4[6][j]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GetTxGain */
static struct nphy_txgains b43_nphy_get_tx_gains(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;

	u16 curr_gain[2];
	struct nphy_txgains target;
	const u32 *table = NULL;

	if (!nphy->txpwrctrl) {
		int i;

		if (nphy->hang_avoid)
			b43_nphy_stay_in_carrier_search(dev, true);
		b43_ntab_read_bulk(dev, B43_NTAB16(7, 0x110), 2, curr_gain);
		if (nphy->hang_avoid)
			b43_nphy_stay_in_carrier_search(dev, false);

		for (i = 0; i < 2; ++i) {
			if (dev->phy.rev >= 7) {
				target.ipa[i] = curr_gain[i] & 0x0007;
				target.pad[i] = (curr_gain[i] & 0x00F8) >> 3;
				target.pga[i] = (curr_gain[i] & 0x0F00) >> 8;
				target.txgm[i] = (curr_gain[i] & 0x7000) >> 12;
				target.tx_lpf[i] = (curr_gain[i] & 0x8000) >> 15;
			} else if (dev->phy.rev >= 3) {
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
			table = b43_nphy_get_tx_gain_table(dev);
			if (!table)
				break;

			if (dev->phy.rev >= 7) {
				target.ipa[i] = (table[index[i]] >> 16) & 0x7;
				target.pad[i] = (table[index[i]] >> 19) & 0x1F;
				target.pga[i] = (table[index[i]] >> 24) & 0xF;
				target.txgm[i] = (table[index[i]] >> 28) & 0x7;
				target.tx_lpf[i] = (table[index[i]] >> 31) & 0x1;
			} else if (dev->phy.rev >= 3) {
				target.ipa[i] = (table[index[i]] >> 16) & 0xF;
				target.pad[i] = (table[index[i]] >> 20) & 0xF;
				target.pga[i] = (table[index[i]] >> 24) & 0xF;
				target.txgm[i] = (table[index[i]] >> 28) & 0xF;
			} else {
				target.ipa[i] = (table[index[i]] >> 16) & 0x3;
				target.pad[i] = (table[index[i]] >> 18) & 0x3;
				target.pga[i] = (table[index[i]] >> 20) & 0x7;
				target.txgm[i] = (table[index[i]] >> 23) & 0x7;
			}
		}
	}

	return target;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalPhyCleanup */
static void b43_nphy_tx_cal_phy_cleanup(struct b43_wldev *dev)
{
	u16 *regs = dev->phy.n->tx_rx_cal_phy_saveregs;

	if (dev->phy.rev >= 3) {
		b43_phy_write(dev, B43_NPHY_AFECTL_C1, regs[0]);
		b43_phy_write(dev, B43_NPHY_AFECTL_C2, regs[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, regs[2]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, regs[3]);
		b43_phy_write(dev, B43_NPHY_BBCFG, regs[4]);
		b43_ntab_write(dev, B43_NTAB16(8, 3), regs[5]);
		b43_ntab_write(dev, B43_NTAB16(8, 19), regs[6]);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, regs[7]);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, regs[8]);
		b43_phy_write(dev, B43_NPHY_PAPD_EN0, regs[9]);
		b43_phy_write(dev, B43_NPHY_PAPD_EN1, regs[10]);
		b43_nphy_reset_cca(dev);
	} else {
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C1, 0x0FFF, regs[0]);
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C2, 0x0FFF, regs[1]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, regs[2]);
		b43_ntab_write(dev, B43_NTAB16(8, 2), regs[3]);
		b43_ntab_write(dev, B43_NTAB16(8, 18), regs[4]);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, regs[5]);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, regs[6]);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalPhySetup */
static void b43_nphy_tx_cal_phy_setup(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	u16 *regs = dev->phy.n->tx_rx_cal_phy_saveregs;
	u16 tmp;

	regs[0] = b43_phy_read(dev, B43_NPHY_AFECTL_C1);
	regs[1] = b43_phy_read(dev, B43_NPHY_AFECTL_C2);
	if (dev->phy.rev >= 3) {
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C1, 0xF0FF, 0x0A00);
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C2, 0xF0FF, 0x0A00);

		tmp = b43_phy_read(dev, B43_NPHY_AFECTL_OVER1);
		regs[2] = tmp;
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, tmp | 0x0600);

		tmp = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		regs[3] = tmp;
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, tmp | 0x0600);

		regs[4] = b43_phy_read(dev, B43_NPHY_BBCFG);
		b43_phy_mask(dev, B43_NPHY_BBCFG,
			     ~B43_NPHY_BBCFG_RSTRX & 0xFFFF);

		tmp = b43_ntab_read(dev, B43_NTAB16(8, 3));
		regs[5] = tmp;
		b43_ntab_write(dev, B43_NTAB16(8, 3), 0);

		tmp = b43_ntab_read(dev, B43_NTAB16(8, 19));
		regs[6] = tmp;
		b43_ntab_write(dev, B43_NTAB16(8, 19), 0);
		regs[7] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC1);
		regs[8] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);

		if (!nphy->use_int_tx_iq_lo_cal)
			b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_PA,
						      1, 3);
		else
			b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_PA,
						      0, 3);
		b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_TRSW, 2, 1);
		b43_nphy_rf_ctl_intc_override(dev, N_INTC_OVERRIDE_TRSW, 8, 2);

		regs[9] = b43_phy_read(dev, B43_NPHY_PAPD_EN0);
		regs[10] = b43_phy_read(dev, B43_NPHY_PAPD_EN1);
		b43_phy_mask(dev, B43_NPHY_PAPD_EN0, ~0x0001);
		b43_phy_mask(dev, B43_NPHY_PAPD_EN1, ~0x0001);

		tmp = b43_nphy_read_lpf_ctl(dev, 0);
		if (phy->rev >= 19)
			b43_nphy_rf_ctl_override_rev19(dev, 0x80, tmp, 0, false,
						       1);
		else if (phy->rev >= 7)
			b43_nphy_rf_ctl_override_rev7(dev, 0x80, tmp, 0, false,
						      1);

		if (nphy->use_int_tx_iq_lo_cal && true /* FIXME */) {
			if (phy->rev >= 19) {
				b43_nphy_rf_ctl_override_rev19(dev, 0x8, 0, 0x3,
							       false, 0);
			} else if (phy->rev >= 8) {
				b43_nphy_rf_ctl_override_rev7(dev, 0x8, 0, 0x3,
							      false, 0);
			} else if (phy->rev == 7) {
				b43_radio_maskset(dev, R2057_OVR_REG0, 1 << 4, 1 << 4);
				if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
					b43_radio_maskset(dev, R2057_PAD2G_TUNE_PUS_CORE0, ~1, 0);
					b43_radio_maskset(dev, R2057_PAD2G_TUNE_PUS_CORE1, ~1, 0);
				} else {
					b43_radio_maskset(dev, R2057_IPA5G_CASCOFFV_PU_CORE0, ~1, 0);
					b43_radio_maskset(dev, R2057_IPA5G_CASCOFFV_PU_CORE1, ~1, 0);
				}
			}
		}
	} else {
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C1, 0x0FFF, 0xA000);
		b43_phy_maskset(dev, B43_NPHY_AFECTL_C2, 0x0FFF, 0xA000);
		tmp = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		regs[2] = tmp;
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, tmp | 0x3000);
		tmp = b43_ntab_read(dev, B43_NTAB16(8, 2));
		regs[3] = tmp;
		tmp |= 0x2000;
		b43_ntab_write(dev, B43_NTAB16(8, 2), tmp);
		tmp = b43_ntab_read(dev, B43_NTAB16(8, 18));
		regs[4] = tmp;
		tmp |= 0x2000;
		b43_ntab_write(dev, B43_NTAB16(8, 18), tmp);
		regs[5] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC1);
		regs[6] = b43_phy_read(dev, B43_NPHY_RFCTL_INTC2);
		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
			tmp = 0x0180;
		else
			tmp = 0x0120;
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC1, tmp);
		b43_phy_write(dev, B43_NPHY_RFCTL_INTC2, tmp);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SaveCal */
static void b43_nphy_save_cal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;

	struct b43_phy_n_iq_comp *rxcal_coeffs = NULL;
	u16 *txcal_radio_regs = NULL;
	struct b43_chanspec *iqcal_chanspec;
	u16 *table = NULL;

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 1);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_2G;
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_2G;
		iqcal_chanspec = &nphy->iqcal_chanspec_2G;
		table = nphy->cal_cache.txcal_coeffs_2G;
	} else {
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_5G;
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_5G;
		iqcal_chanspec = &nphy->iqcal_chanspec_5G;
		table = nphy->cal_cache.txcal_coeffs_5G;
	}

	b43_nphy_rx_iq_coeffs(dev, false, rxcal_coeffs);
	/* TODO use some definitions */
	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		txcal_radio_regs[0] = b43_radio_read(dev,
						     R2057_TX0_LOFT_FINE_I);
		txcal_radio_regs[1] = b43_radio_read(dev,
						     R2057_TX0_LOFT_FINE_Q);
		txcal_radio_regs[4] = b43_radio_read(dev,
						     R2057_TX0_LOFT_COARSE_I);
		txcal_radio_regs[5] = b43_radio_read(dev,
						     R2057_TX0_LOFT_COARSE_Q);
		txcal_radio_regs[2] = b43_radio_read(dev,
						     R2057_TX1_LOFT_FINE_I);
		txcal_radio_regs[3] = b43_radio_read(dev,
						     R2057_TX1_LOFT_FINE_Q);
		txcal_radio_regs[6] = b43_radio_read(dev,
						     R2057_TX1_LOFT_COARSE_I);
		txcal_radio_regs[7] = b43_radio_read(dev,
						     R2057_TX1_LOFT_COARSE_Q);
	} else if (phy->rev >= 3) {
		txcal_radio_regs[0] = b43_radio_read(dev, 0x2021);
		txcal_radio_regs[1] = b43_radio_read(dev, 0x2022);
		txcal_radio_regs[2] = b43_radio_read(dev, 0x3021);
		txcal_radio_regs[3] = b43_radio_read(dev, 0x3022);
		txcal_radio_regs[4] = b43_radio_read(dev, 0x2023);
		txcal_radio_regs[5] = b43_radio_read(dev, 0x2024);
		txcal_radio_regs[6] = b43_radio_read(dev, 0x3023);
		txcal_radio_regs[7] = b43_radio_read(dev, 0x3024);
	} else {
		txcal_radio_regs[0] = b43_radio_read(dev, 0x8B);
		txcal_radio_regs[1] = b43_radio_read(dev, 0xBA);
		txcal_radio_regs[2] = b43_radio_read(dev, 0x8D);
		txcal_radio_regs[3] = b43_radio_read(dev, 0xBC);
	}
	iqcal_chanspec->center_freq = dev->phy.chandef->chan->center_freq;
	iqcal_chanspec->channel_type =
				cfg80211_get_chandef_type(dev->phy.chandef);
	b43_ntab_read_bulk(dev, B43_NTAB16(15, 80), 8, table);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RestoreCal */
static void b43_nphy_restore_cal(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;

	u16 coef[4];
	u16 *loft = NULL;
	u16 *table = NULL;

	int i;
	u16 *txcal_radio_regs = NULL;
	struct b43_phy_n_iq_comp *rxcal_coeffs = NULL;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if (!nphy->iqcal_chanspec_2G.center_freq)
			return;
		table = nphy->cal_cache.txcal_coeffs_2G;
		loft = &nphy->cal_cache.txcal_coeffs_2G[5];
	} else {
		if (!nphy->iqcal_chanspec_5G.center_freq)
			return;
		table = nphy->cal_cache.txcal_coeffs_5G;
		loft = &nphy->cal_cache.txcal_coeffs_5G[5];
	}

	b43_ntab_write_bulk(dev, B43_NTAB16(15, 80), 4, table);

	for (i = 0; i < 4; i++) {
		if (dev->phy.rev >= 3)
			table[i] = coef[i];
		else
			coef[i] = 0;
	}

	b43_ntab_write_bulk(dev, B43_NTAB16(15, 88), 4, coef);
	b43_ntab_write_bulk(dev, B43_NTAB16(15, 85), 2, loft);
	b43_ntab_write_bulk(dev, B43_NTAB16(15, 93), 2, loft);

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
	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		b43_radio_write(dev, R2057_TX0_LOFT_FINE_I,
				txcal_radio_regs[0]);
		b43_radio_write(dev, R2057_TX0_LOFT_FINE_Q,
				txcal_radio_regs[1]);
		b43_radio_write(dev, R2057_TX0_LOFT_COARSE_I,
				txcal_radio_regs[4]);
		b43_radio_write(dev, R2057_TX0_LOFT_COARSE_Q,
				txcal_radio_regs[5]);
		b43_radio_write(dev, R2057_TX1_LOFT_FINE_I,
				txcal_radio_regs[2]);
		b43_radio_write(dev, R2057_TX1_LOFT_FINE_Q,
				txcal_radio_regs[3]);
		b43_radio_write(dev, R2057_TX1_LOFT_COARSE_I,
				txcal_radio_regs[6]);
		b43_radio_write(dev, R2057_TX1_LOFT_COARSE_Q,
				txcal_radio_regs[7]);
	} else if (phy->rev >= 3) {
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

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalTxIqlo */
static int b43_nphy_cal_tx_iq_lo(struct b43_wldev *dev,
				struct nphy_txgains target,
				bool full, bool mphase)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	int i;
	int error = 0;
	int freq;
	bool avoid = false;
	u8 length;
	u16 tmp, core, type, count, max, numb, last = 0, cmd;
	const u16 *table;
	bool phy6or5x;

	u16 buffer[11];
	u16 diq_start = 0;
	u16 save[2];
	u16 gain[2];
	struct nphy_iqcal_params params[2];
	bool updated[2] = { };

	b43_nphy_stay_in_carrier_search(dev, true);

	if (dev->phy.rev >= 4) {
		avoid = nphy->hang_avoid;
		nphy->hang_avoid = false;
	}

	b43_ntab_read_bulk(dev, B43_NTAB16(7, 0x110), 2, save);

	for (i = 0; i < 2; i++) {
		b43_nphy_iq_cal_gain_params(dev, i, target, &params[i]);
		gain[i] = params[i].cal_gain;
	}

	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x110), 2, gain);

	b43_nphy_tx_cal_radio_setup(dev);
	b43_nphy_tx_cal_phy_setup(dev);

	phy6or5x = dev->phy.rev >= 6 ||
		(dev->phy.rev == 5 && nphy->ipa2g_on &&
		b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ);
	if (phy6or5x) {
		if (b43_is_40mhz(dev)) {
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 0), 18,
					tbl_tx_iqlo_cal_loft_ladder_40);
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 32), 18,
					tbl_tx_iqlo_cal_iqimb_ladder_40);
		} else {
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 0), 18,
					tbl_tx_iqlo_cal_loft_ladder_20);
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 32), 18,
					tbl_tx_iqlo_cal_iqimb_ladder_20);
		}
	}

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		b43_phy_write(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0x8AD9);
	} else {
		b43_phy_write(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0x8AA9);
	}

	if (!b43_is_40mhz(dev))
		freq = 2500;
	else
		freq = 5000;

	if (nphy->mphase_cal_phase_id > 2)
		b43_nphy_run_samples(dev, (b43_is_40mhz(dev) ? 40 : 20) * 8,
				     0xFFFF, 0, true, false, false);
	else
		error = b43_nphy_tx_tone(dev, freq, 250, true, false, false);

	if (error == 0) {
		if (nphy->mphase_cal_phase_id > 2) {
			table = nphy->mphase_txcal_bestcoeffs;
			length = 11;
			if (dev->phy.rev < 3)
				length -= 2;
		} else {
			if (!full && nphy->txiqlocal_coeffsvalid) {
				table = nphy->txiqlocal_bestc;
				length = 11;
				if (dev->phy.rev < 3)
					length -= 2;
			} else {
				full = true;
				if (dev->phy.rev >= 3) {
					table = tbl_tx_iqlo_cal_startcoefs_nphyrev3;
					length = B43_NTAB_TX_IQLO_CAL_STARTCOEFS_REV3;
				} else {
					table = tbl_tx_iqlo_cal_startcoefs;
					length = B43_NTAB_TX_IQLO_CAL_STARTCOEFS;
				}
			}
		}

		b43_ntab_write_bulk(dev, B43_NTAB16(15, 64), length, table);

		if (full) {
			if (dev->phy.rev >= 3)
				max = B43_NTAB_TX_IQLO_CAL_CMDS_FULLCAL_REV3;
			else
				max = B43_NTAB_TX_IQLO_CAL_CMDS_FULLCAL;
		} else {
			if (dev->phy.rev >= 3)
				max = B43_NTAB_TX_IQLO_CAL_CMDS_RECAL_REV3;
			else
				max = B43_NTAB_TX_IQLO_CAL_CMDS_RECAL;
		}

		if (mphase) {
			count = nphy->mphase_txcal_cmdidx;
			numb = min(max,
				(u16)(count + nphy->mphase_txcal_numcmds));
		} else {
			count = 0;
			numb = max;
		}

		for (; count < numb; count++) {
			if (full) {
				if (dev->phy.rev >= 3)
					cmd = tbl_tx_iqlo_cal_cmds_fullcal_nphyrev3[count];
				else
					cmd = tbl_tx_iqlo_cal_cmds_fullcal[count];
			} else {
				if (dev->phy.rev >= 3)
					cmd = tbl_tx_iqlo_cal_cmds_recal_nphyrev3[count];
				else
					cmd = tbl_tx_iqlo_cal_cmds_recal[count];
			}

			core = (cmd & 0x3000) >> 12;
			type = (cmd & 0x0F00) >> 8;

			if (phy6or5x && updated[core] == 0) {
				b43_nphy_update_tx_cal_ladder(dev, core);
				updated[core] = true;
			}

			tmp = (params[core].ncorr[type] << 8) | 0x66;
			b43_phy_write(dev, B43_NPHY_IQLOCAL_CMDNNUM, tmp);

			if (type == 1 || type == 3 || type == 4) {
				buffer[0] = b43_ntab_read(dev,
						B43_NTAB16(15, 69 + core));
				diq_start = buffer[0];
				buffer[0] = 0;
				b43_ntab_write(dev, B43_NTAB16(15, 69 + core),
						0);
			}

			b43_phy_write(dev, B43_NPHY_IQLOCAL_CMD, cmd);
			for (i = 0; i < 2000; i++) {
				tmp = b43_phy_read(dev, B43_NPHY_IQLOCAL_CMD);
				if (tmp & 0xC000)
					break;
				udelay(10);
			}

			b43_ntab_read_bulk(dev, B43_NTAB16(15, 96), length,
						buffer);
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 64), length,
						buffer);

			if (type == 1 || type == 3 || type == 4)
				buffer[0] = diq_start;
		}

		if (mphase)
			nphy->mphase_txcal_cmdidx = (numb >= max) ? 0 : numb;

		last = (dev->phy.rev < 3) ? 6 : 7;

		if (!mphase || nphy->mphase_cal_phase_id == last) {
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 96), 4, buffer);
			b43_ntab_read_bulk(dev, B43_NTAB16(15, 80), 4, buffer);
			if (dev->phy.rev < 3) {
				buffer[0] = 0;
				buffer[1] = 0;
				buffer[2] = 0;
				buffer[3] = 0;
			}
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 88), 4,
						buffer);
			b43_ntab_read_bulk(dev, B43_NTAB16(15, 101), 2,
						buffer);
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 85), 2,
						buffer);
			b43_ntab_write_bulk(dev, B43_NTAB16(15, 93), 2,
						buffer);
			length = 11;
			if (dev->phy.rev < 3)
				length -= 2;
			b43_ntab_read_bulk(dev, B43_NTAB16(15, 96), length,
						nphy->txiqlocal_bestc);
			nphy->txiqlocal_coeffsvalid = true;
			nphy->txiqlocal_chanspec.center_freq =
						phy->chandef->chan->center_freq;
			nphy->txiqlocal_chanspec.channel_type =
					cfg80211_get_chandef_type(phy->chandef);
		} else {
			length = 11;
			if (dev->phy.rev < 3)
				length -= 2;
			b43_ntab_read_bulk(dev, B43_NTAB16(15, 96), length,
						nphy->mphase_txcal_bestcoeffs);
		}

		b43_nphy_stop_playback(dev);
		b43_phy_write(dev, B43_NPHY_IQLOCAL_CMDGCTL, 0);
	}

	b43_nphy_tx_cal_phy_cleanup(dev);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x110), 2, save);

	if (dev->phy.rev < 2 && (!mphase || nphy->mphase_cal_phase_id == last))
		b43_nphy_tx_iq_workaround(dev);

	if (dev->phy.rev >= 4)
		nphy->hang_avoid = avoid;

	b43_nphy_stay_in_carrier_search(dev, false);

	return error;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ReapplyTxCalCoeffs */
static void b43_nphy_reapply_tx_cal_coeffs(struct b43_wldev *dev)
{
	struct b43_phy_n *nphy = dev->phy.n;
	u8 i;
	u16 buffer[7];
	bool equal = true;

	if (!nphy->txiqlocal_coeffsvalid ||
	    nphy->txiqlocal_chanspec.center_freq != dev->phy.chandef->chan->center_freq ||
	    nphy->txiqlocal_chanspec.channel_type != cfg80211_get_chandef_type(dev->phy.chandef))
		return;

	b43_ntab_read_bulk(dev, B43_NTAB16(15, 80), 7, buffer);
	for (i = 0; i < 4; i++) {
		if (buffer[i] != nphy->txiqlocal_bestc[i]) {
			equal = false;
			break;
		}
	}

	if (!equal) {
		b43_ntab_write_bulk(dev, B43_NTAB16(15, 80), 4,
					nphy->txiqlocal_bestc);
		for (i = 0; i < 4; i++)
			buffer[i] = 0;
		b43_ntab_write_bulk(dev, B43_NTAB16(15, 88), 4,
					buffer);
		b43_ntab_write_bulk(dev, B43_NTAB16(15, 85), 2,
					&nphy->txiqlocal_bestc[5]);
		b43_ntab_write_bulk(dev, B43_NTAB16(15, 93), 2,
					&nphy->txiqlocal_bestc[5]);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalRxIqRev2 */
static int b43_nphy_rev2_cal_rx_iq(struct b43_wldev *dev,
			struct nphy_txgains target, u8 type, bool debug)
{
	struct b43_phy_n *nphy = dev->phy.n;
	int i, j, index;
	u8 rfctl[2];
	u8 afectl_core;
	u16 tmp[6];
	u16 uninitialized_var(cur_hpf1), uninitialized_var(cur_hpf2), cur_lna;
	u32 real, imag;
	enum ieee80211_band band;

	u8 use;
	u16 cur_hpf;
	u16 lna[3] = { 3, 3, 1 };
	u16 hpf1[3] = { 7, 2, 0 };
	u16 hpf2[3] = { 2, 0, 0 };
	u32 power[3] = { };
	u16 gain_save[2];
	u16 cal_gain[2];
	struct nphy_iqcal_params cal_params[2];
	struct nphy_iq_est est;
	int ret = 0;
	bool playtone = true;
	int desired = 13;

	b43_nphy_stay_in_carrier_search(dev, 1);

	if (dev->phy.rev < 2)
		b43_nphy_reapply_tx_cal_coeffs(dev);
	b43_ntab_read_bulk(dev, B43_NTAB16(7, 0x110), 2, gain_save);
	for (i = 0; i < 2; i++) {
		b43_nphy_iq_cal_gain_params(dev, i, target, &cal_params[i]);
		cal_gain[i] = cal_params[i].cal_gain;
	}
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x110), 2, cal_gain);

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			rfctl[0] = B43_NPHY_RFCTL_INTC1;
			rfctl[1] = B43_NPHY_RFCTL_INTC2;
			afectl_core = B43_NPHY_AFECTL_C1;
		} else {
			rfctl[0] = B43_NPHY_RFCTL_INTC2;
			rfctl[1] = B43_NPHY_RFCTL_INTC1;
			afectl_core = B43_NPHY_AFECTL_C2;
		}

		tmp[1] = b43_phy_read(dev, B43_NPHY_RFSEQCA);
		tmp[2] = b43_phy_read(dev, afectl_core);
		tmp[3] = b43_phy_read(dev, B43_NPHY_AFECTL_OVER);
		tmp[4] = b43_phy_read(dev, rfctl[0]);
		tmp[5] = b43_phy_read(dev, rfctl[1]);

		b43_phy_maskset(dev, B43_NPHY_RFSEQCA,
				~B43_NPHY_RFSEQCA_RXDIS & 0xFFFF,
				((1 - i) << B43_NPHY_RFSEQCA_RXDIS_SHIFT));
		b43_phy_maskset(dev, B43_NPHY_RFSEQCA, ~B43_NPHY_RFSEQCA_TXEN,
				(1 - i));
		b43_phy_set(dev, afectl_core, 0x0006);
		b43_phy_set(dev, B43_NPHY_AFECTL_OVER, 0x0006);

		band = b43_current_band(dev->wl);

		if (nphy->rxcalparams & 0xFF000000) {
			if (band == IEEE80211_BAND_5GHZ)
				b43_phy_write(dev, rfctl[0], 0x140);
			else
				b43_phy_write(dev, rfctl[0], 0x110);
		} else {
			if (band == IEEE80211_BAND_5GHZ)
				b43_phy_write(dev, rfctl[0], 0x180);
			else
				b43_phy_write(dev, rfctl[0], 0x120);
		}

		if (band == IEEE80211_BAND_5GHZ)
			b43_phy_write(dev, rfctl[1], 0x148);
		else
			b43_phy_write(dev, rfctl[1], 0x114);

		if (nphy->rxcalparams & 0x10000) {
			b43_radio_maskset(dev, B2055_C1_GENSPARE2, 0xFC,
					(i + 1));
			b43_radio_maskset(dev, B2055_C2_GENSPARE2, 0xFC,
					(2 - i));
		}

		for (j = 0; j < 4; j++) {
			if (j < 3) {
				cur_lna = lna[j];
				cur_hpf1 = hpf1[j];
				cur_hpf2 = hpf2[j];
			} else {
				if (power[1] > 10000) {
					use = 1;
					cur_hpf = cur_hpf1;
					index = 2;
				} else {
					if (power[0] > 10000) {
						use = 1;
						cur_hpf = cur_hpf1;
						index = 1;
					} else {
						index = 0;
						use = 2;
						cur_hpf = cur_hpf2;
					}
				}
				cur_lna = lna[index];
				cur_hpf1 = hpf1[index];
				cur_hpf2 = hpf2[index];
				cur_hpf += desired - hweight32(power[index]);
				cur_hpf = clamp_val(cur_hpf, 0, 10);
				if (use == 1)
					cur_hpf1 = cur_hpf;
				else
					cur_hpf2 = cur_hpf;
			}

			tmp[0] = ((cur_hpf2 << 8) | (cur_hpf1 << 4) |
					(cur_lna << 2));
			b43_nphy_rf_ctl_override(dev, 0x400, tmp[0], 3,
									false);
			b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
			b43_nphy_stop_playback(dev);

			if (playtone) {
				ret = b43_nphy_tx_tone(dev, 4000,
						(nphy->rxcalparams & 0xFFFF),
						false, false, true);
				playtone = false;
			} else {
				b43_nphy_run_samples(dev, 160, 0xFFFF, 0, false,
						     false, true);
			}

			if (ret == 0) {
				if (j < 3) {
					b43_nphy_rx_iq_est(dev, &est, 1024, 32,
									false);
					if (i == 0) {
						real = est.i0_pwr;
						imag = est.q0_pwr;
					} else {
						real = est.i1_pwr;
						imag = est.q1_pwr;
					}
					power[i] = ((real + imag) / 1024) + 1;
				} else {
					b43_nphy_calc_rx_iq_comp(dev, 1 << i);
				}
				b43_nphy_stop_playback(dev);
			}

			if (ret != 0)
				break;
		}

		b43_radio_mask(dev, B2055_C1_GENSPARE2, 0xFC);
		b43_radio_mask(dev, B2055_C2_GENSPARE2, 0xFC);
		b43_phy_write(dev, rfctl[1], tmp[5]);
		b43_phy_write(dev, rfctl[0], tmp[4]);
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, tmp[3]);
		b43_phy_write(dev, afectl_core, tmp[2]);
		b43_phy_write(dev, B43_NPHY_RFSEQCA, tmp[1]);

		if (ret != 0)
			break;
	}

	b43_nphy_rf_ctl_override(dev, 0x400, 0, 3, true);
	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
	b43_ntab_write_bulk(dev, B43_NTAB16(7, 0x110), 2, gain_save);

	b43_nphy_stay_in_carrier_search(dev, 0);

	return ret;
}

static int b43_nphy_rev3_cal_rx_iq(struct b43_wldev *dev,
			struct nphy_txgains target, u8 type, bool debug)
{
	return -1;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalRxIq */
static int b43_nphy_cal_rx_iq(struct b43_wldev *dev,
			struct nphy_txgains target, u8 type, bool debug)
{
	if (dev->phy.rev >= 7)
		type = 0;

	if (dev->phy.rev >= 3)
		return b43_nphy_rev3_cal_rx_iq(dev, target, type, debug);
	else
		return b43_nphy_rev2_cal_rx_iq(dev, target, type, debug);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCoreSetState */
static void b43_nphy_set_rx_core_state(struct b43_wldev *dev, u8 mask)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = phy->n;
	/* u16 buf[16]; it's rev3+ */

	nphy->phyrxchain = mask;

	if (0 /* FIXME clk */)
		return;

	b43_mac_suspend(dev);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, true);

	b43_phy_maskset(dev, B43_NPHY_RFSEQCA, ~B43_NPHY_RFSEQCA_RXEN,
			(mask & 0x3) << B43_NPHY_RFSEQCA_RXEN_SHIFT);

	if ((mask & 0x3) != 0x3) {
		b43_phy_write(dev, B43_NPHY_HPANT_SWTHRES, 1);
		if (dev->phy.rev >= 3) {
			/* TODO */
		}
	} else {
		b43_phy_write(dev, B43_NPHY_HPANT_SWTHRES, 0x1E);
		if (dev->phy.rev >= 3) {
			/* TODO */
		}
	}

	b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);

	if (nphy->hang_avoid)
		b43_nphy_stay_in_carrier_search(dev, false);

	b43_mac_enable(dev);
}

/**************************************************
 * N-PHY init
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/MIMOConfig */
static void b43_nphy_update_mimo_config(struct b43_wldev *dev, s32 preamble)
{
	u16 mimocfg = b43_phy_read(dev, B43_NPHY_MIMOCFG);

	mimocfg |= B43_NPHY_MIMOCFG_AUTO;
	if (preamble == 1)
		mimocfg |= B43_NPHY_MIMOCFG_GFMIX;
	else
		mimocfg &= ~B43_NPHY_MIMOCFG_GFMIX;

	b43_phy_write(dev, B43_NPHY_MIMOCFG, mimocfg);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/BPHYInit */
static void b43_nphy_bphy_init(struct b43_wldev *dev)
{
	unsigned int i;
	u16 val;

	val = 0x1E1F;
	for (i = 0; i < 16; i++) {
		b43_phy_write(dev, B43_PHY_N_BMODE(0x88 + i), val);
		val -= 0x202;
	}
	val = 0x3E3F;
	for (i = 0; i < 16; i++) {
		b43_phy_write(dev, B43_PHY_N_BMODE(0x98 + i), val);
		val -= 0x202;
	}
	b43_phy_write(dev, B43_PHY_N_BMODE(0x38), 0x668);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SuperSwitchInit */
static void b43_nphy_superswitch_init(struct b43_wldev *dev, bool init)
{
	if (dev->phy.rev >= 7)
		return;

	if (dev->phy.rev >= 3) {
		if (!init)
			return;
		if (0 /* FIXME */) {
			b43_ntab_write(dev, B43_NTAB16(9, 2), 0x211);
			b43_ntab_write(dev, B43_NTAB16(9, 3), 0x222);
			b43_ntab_write(dev, B43_NTAB16(9, 8), 0x144);
			b43_ntab_write(dev, B43_NTAB16(9, 12), 0x188);
		}
	} else {
		b43_phy_write(dev, B43_NPHY_GPIO_LOOEN, 0);
		b43_phy_write(dev, B43_NPHY_GPIO_HIOEN, 0);

		switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
		case B43_BUS_BCMA:
			bcma_chipco_gpio_control(&dev->dev->bdev->bus->drv_cc,
						 0xFC00, 0xFC00);
			break;
#endif
#ifdef CONFIG_B43_SSB
		case B43_BUS_SSB:
			ssb_chipco_gpio_control(&dev->dev->sdev->bus->chipco,
						0xFC00, 0xFC00);
			break;
#endif
		}

		b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_GPOUTSMSK, 0);
		b43_maskset16(dev, B43_MMIO_GPIO_MASK, ~0, 0xFC00);
		b43_maskset16(dev, B43_MMIO_GPIO_CONTROL, (~0xFC00 & 0xFFFF),
			      0);

		if (init) {
			b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO1, 0x2D8);
			b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP1, 0x301);
			b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_LO2, 0x2D8);
			b43_phy_write(dev, B43_NPHY_RFCTL_LUT_TRSW_UP2, 0x301);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Init/N */
static int b43_phy_initn(struct b43_wldev *dev)
{
	struct ssb_sprom *sprom = dev->dev->bus_sprom;
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
	   (sprom->boardflags_lo & B43_BFL_EXTLNA) &&
	   (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)) {
		switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
		case B43_BUS_BCMA:
			bcma_cc_set32(&dev->dev->bdev->bus->drv_cc,
				      BCMA_CC_CHIPCTL, 0x40);
			break;
#endif
#ifdef CONFIG_B43_SSB
		case B43_BUS_SSB:
			chipco_set32(&dev->dev->sdev->bus->chipco,
				     SSB_CHIPCO_CHIPCTL, 0x40);
			break;
#endif
		}
	}
	nphy->use_int_tx_iq_lo_cal = b43_nphy_ipa(dev) ||
		phy->rev >= 7 ||
		(phy->rev >= 5 &&
		 sprom->boardflags2_hi & B43_BFH2_INTERNDET_TXIQCAL);
	nphy->deaf_count = 0;
	b43_nphy_tables_init(dev);
	nphy->crsminpwr_adjusted = false;
	nphy->noisevars_adjusted = false;

	/* Clear all overrides */
	if (dev->phy.rev >= 3) {
		b43_phy_write(dev, B43_NPHY_TXF_40CO_B1S1, 0);
		b43_phy_write(dev, B43_NPHY_RFCTL_OVER, 0);
		if (phy->rev >= 7) {
			b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER3, 0);
			b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER4, 0);
			b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER5, 0);
			b43_phy_write(dev, B43_NPHY_REV7_RF_CTL_OVER6, 0);
		}
		if (phy->rev >= 19) {
			/* TODO */
		}

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

	if (sprom->boardflags2_lo & B43_BFL2_SKWRKFEM_BRD ||
	    (dev->dev->board_vendor == PCI_VENDOR_ID_APPLE &&
	     dev->dev->board_type == BCMA_BOARD_TYPE_BCM943224M93))
		b43_phy_write(dev, B43_NPHY_TXREALFD, 0xA0);
	else
		b43_phy_write(dev, B43_NPHY_TXREALFD, 0xB8);
	b43_phy_write(dev, B43_NPHY_MIMO_CRSTXEXT, 0xC8);
	b43_phy_write(dev, B43_NPHY_PLOAD_CSENSE_EXTLEN, 0x50);
	b43_phy_write(dev, B43_NPHY_TXRIFS_FRDEL, 0x30);

	if (phy->rev < 8)
		b43_nphy_update_mimo_config(dev, nphy->preamble_override);

	b43_nphy_update_txrx_chain(dev);

	if (phy->rev < 2) {
		b43_phy_write(dev, B43_NPHY_DUP40_GFBL, 0xAA8);
		b43_phy_write(dev, B43_NPHY_DUP40_BL, 0x9A4);
	}

	tmp2 = b43_current_band(dev->wl);
	if (b43_nphy_ipa(dev)) {
		b43_phy_set(dev, B43_NPHY_PAPD_EN0, 0x1);
		b43_phy_maskset(dev, B43_NPHY_EPS_TABLE_ADJ0, 0x007F,
				nphy->papd_epsilon_offset[0] << 7);
		b43_phy_set(dev, B43_NPHY_PAPD_EN1, 0x1);
		b43_phy_maskset(dev, B43_NPHY_EPS_TABLE_ADJ1, 0x007F,
				nphy->papd_epsilon_offset[1] << 7);
		b43_nphy_int_pa_set_tx_dig_filters(dev);
	} else if (phy->rev >= 5) {
		b43_nphy_ext_pa_set_tx_dig_filters(dev);
	}

	b43_nphy_workarounds(dev);

	/* Reset CCA, in init code it differs a little from standard way */
	b43_phy_force_clock(dev, 1);
	tmp = b43_phy_read(dev, B43_NPHY_BBCFG);
	b43_phy_write(dev, B43_NPHY_BBCFG, tmp | B43_NPHY_BBCFG_RSTCCA);
	b43_phy_write(dev, B43_NPHY_BBCFG, tmp & ~B43_NPHY_BBCFG_RSTCCA);
	b43_phy_force_clock(dev, 0);

	b43_mac_phy_clock_set(dev, true);

	if (phy->rev < 7) {
		b43_nphy_pa_override(dev, false);
		b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RX2TX);
		b43_nphy_force_rf_sequence(dev, B43_RFSEQ_RESET2RX);
		b43_nphy_pa_override(dev, true);
	}

	b43_nphy_classifier(dev, 0, 0);
	b43_nphy_read_clip_detection(dev, clip);
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		b43_nphy_bphy_init(dev);

	tx_pwr_state = nphy->txpwrctrl;
	b43_nphy_tx_power_ctrl(dev, false);
	b43_nphy_tx_power_fix(dev);
	b43_nphy_tx_power_ctl_idle_tssi(dev);
	b43_nphy_tx_power_ctl_setup(dev);
	b43_nphy_tx_gain_table_upload(dev);

	if (nphy->phyrxchain != 3)
		b43_nphy_set_rx_core_state(dev, nphy->phyrxchain);
	if (nphy->mphase_cal_phase_id > 0)
		;/* TODO PHY Periodic Calibration Multi-Phase Restart */

	do_rssi_cal = false;
	if (phy->rev >= 3) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
			do_rssi_cal = !nphy->rssical_chanspec_2G.center_freq;
		else
			do_rssi_cal = !nphy->rssical_chanspec_5G.center_freq;

		if (do_rssi_cal)
			b43_nphy_rssi_cal(dev);
		else
			b43_nphy_restore_rssi_cal(dev);
	} else {
		b43_nphy_rssi_cal(dev);
	}

	if (!((nphy->measure_hold & 0x6) != 0)) {
		if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
			do_cal = !nphy->iqcal_chanspec_2G.center_freq;
		else
			do_cal = !nphy->iqcal_chanspec_5G.center_freq;

		if (nphy->mute)
			do_cal = false;

		if (do_cal) {
			target = b43_nphy_get_tx_gains(dev);

			if (nphy->antsel_type == 2)
				b43_nphy_superswitch_init(dev, true);
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
				if (!b43_nphy_cal_tx_iq_lo(dev, target, true, false))
					if (b43_nphy_cal_rx_iq(dev, target, 2, 0) == 0)
						b43_nphy_save_cal(dev);
			} else if (nphy->mphase_cal_phase_id == 0)
				;/* N PHY Periodic Calibration with arg 3 */
		} else {
			b43_nphy_restore_cal(dev);
		}
	}

	b43_nphy_tx_pwr_ctrl_coef_setup(dev);
	b43_nphy_tx_power_ctrl(dev, tx_pwr_state);
	b43_phy_write(dev, B43_NPHY_TXMACIF_HOLDOFF, 0x0015);
	b43_phy_write(dev, B43_NPHY_TXMACDELAY, 0x0320);
	if (phy->rev >= 3 && phy->rev <= 6)
		b43_phy_write(dev, B43_NPHY_PLOAD_CSENSE_EXTLEN, 0x0032);
	b43_nphy_tx_lpf_bw(dev);
	if (phy->rev >= 3)
		b43_nphy_spur_workaround(dev);

	return 0;
}

/**************************************************
 * Channel switching ops.
 **************************************************/

static void b43_chantab_phy_upload(struct b43_wldev *dev,
				   const struct b43_phy_n_sfo_cfg *e)
{
	b43_phy_write(dev, B43_NPHY_BW1A, e->phy_bw1a);
	b43_phy_write(dev, B43_NPHY_BW2, e->phy_bw2);
	b43_phy_write(dev, B43_NPHY_BW3, e->phy_bw3);
	b43_phy_write(dev, B43_NPHY_BW4, e->phy_bw4);
	b43_phy_write(dev, B43_NPHY_BW5, e->phy_bw5);
	b43_phy_write(dev, B43_NPHY_BW6, e->phy_bw6);
}

/* http://bcm-v4.sipsolutions.net/802.11/PmuSpurAvoid */
static void b43_nphy_pmu_spur_avoid(struct b43_wldev *dev, bool avoid)
{
	switch (dev->dev->bus_type) {
#ifdef CONFIG_B43_BCMA
	case B43_BUS_BCMA:
		bcma_pmu_spuravoid_pllupdate(&dev->dev->bdev->bus->drv_cc,
					     avoid);
		break;
#endif
#ifdef CONFIG_B43_SSB
	case B43_BUS_SSB:
		ssb_pmu_spuravoid_pllupdate(&dev->dev->sdev->bus->chipco,
					    avoid);
		break;
#endif
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ChanspecSetup */
static void b43_nphy_channel_setup(struct b43_wldev *dev,
				const struct b43_phy_n_sfo_cfg *e,
				struct ieee80211_channel *new_channel)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_n *nphy = dev->phy.n;
	int ch = new_channel->hw_value;

	u16 old_band_5ghz;
	u16 tmp16;

	old_band_5ghz =
		b43_phy_read(dev, B43_NPHY_BANDCTL) & B43_NPHY_BANDCTL_5GHZ;
	if (new_channel->band == IEEE80211_BAND_5GHZ && !old_band_5ghz) {
		tmp16 = b43_read16(dev, B43_MMIO_PSM_PHY_HDR);
		b43_write16(dev, B43_MMIO_PSM_PHY_HDR, tmp16 | 4);
		b43_phy_set(dev, B43_PHY_B_BBCFG, 0xC000);
		b43_write16(dev, B43_MMIO_PSM_PHY_HDR, tmp16);
		b43_phy_set(dev, B43_NPHY_BANDCTL, B43_NPHY_BANDCTL_5GHZ);
	} else if (new_channel->band == IEEE80211_BAND_2GHZ && old_band_5ghz) {
		b43_phy_mask(dev, B43_NPHY_BANDCTL, ~B43_NPHY_BANDCTL_5GHZ);
		tmp16 = b43_read16(dev, B43_MMIO_PSM_PHY_HDR);
		b43_write16(dev, B43_MMIO_PSM_PHY_HDR, tmp16 | 4);
		b43_phy_mask(dev, B43_PHY_B_BBCFG, 0x3FFF);
		b43_write16(dev, B43_MMIO_PSM_PHY_HDR, tmp16);
	}

	b43_chantab_phy_upload(dev, e);

	if (new_channel->hw_value == 14) {
		b43_nphy_classifier(dev, 2, 0);
		b43_phy_set(dev, B43_PHY_B_TEST, 0x0800);
	} else {
		b43_nphy_classifier(dev, 2, 2);
		if (new_channel->band == IEEE80211_BAND_2GHZ)
			b43_phy_mask(dev, B43_PHY_B_TEST, ~0x840);
	}

	if (!nphy->txpwrctrl)
		b43_nphy_tx_power_fix(dev);

	if (dev->phy.rev < 3)
		b43_nphy_adjust_lna_gain_table(dev);

	b43_nphy_tx_lpf_bw(dev);

	if (dev->phy.rev >= 3 &&
	    dev->phy.n->spur_avoid != B43_SPUR_AVOID_DISABLE) {
		bool avoid = false;
		if (dev->phy.n->spur_avoid == B43_SPUR_AVOID_FORCE) {
			avoid = true;
		} else if (!b43_is_40mhz(dev)) {
			if ((ch >= 5 && ch <= 8) || ch == 13 || ch == 14)
				avoid = true;
		} else { /* 40MHz */
			if (nphy->aband_spurwar_en &&
			    (ch == 38 || ch == 102 || ch == 118))
				avoid = dev->dev->chip_id == 0x4716;
		}

		b43_nphy_pmu_spur_avoid(dev, avoid);

		if (dev->dev->chip_id == 43222 || dev->dev->chip_id == 43224 ||
		    dev->dev->chip_id == 43225) {
			b43_write16(dev, B43_MMIO_TSF_CLK_FRAC_LOW,
				    avoid ? 0x5341 : 0x8889);
			b43_write16(dev, B43_MMIO_TSF_CLK_FRAC_HIGH, 0x8);
		}

		if (dev->phy.rev == 3 || dev->phy.rev == 4)
			; /* TODO: reset PLL */

		if (avoid)
			b43_phy_set(dev, B43_NPHY_BBCFG, B43_NPHY_BBCFG_RSTRX);
		else
			b43_phy_mask(dev, B43_NPHY_BBCFG,
				     ~B43_NPHY_BBCFG_RSTRX & 0xFFFF);

		b43_nphy_reset_cca(dev);

		/* wl sets useless phy_isspuravoid here */
	}

	b43_phy_write(dev, B43_NPHY_NDATAT_DUP40, 0x3830);

	if (phy->rev >= 3)
		b43_nphy_spur_workaround(dev);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetChanspec */
static int b43_nphy_set_channel(struct b43_wldev *dev,
				struct ieee80211_channel *channel,
				enum nl80211_channel_type channel_type)
{
	struct b43_phy *phy = &dev->phy;

	const struct b43_nphy_channeltab_entry_rev2 *tabent_r2 = NULL;
	const struct b43_nphy_channeltab_entry_rev3 *tabent_r3 = NULL;
	const struct b43_nphy_chantabent_rev7 *tabent_r7 = NULL;
	const struct b43_nphy_chantabent_rev7_2g *tabent_r7_2g = NULL;

	u8 tmp;

	if (phy->rev >= 19) {
		return -ESRCH;
		/* TODO */
	} else if (phy->rev >= 7) {
		r2057_get_chantabent_rev7(dev, channel->center_freq,
					  &tabent_r7, &tabent_r7_2g);
		if (!tabent_r7 && !tabent_r7_2g)
			return -ESRCH;
	} else if (phy->rev >= 3) {
		tabent_r3 = b43_nphy_get_chantabent_rev3(dev,
							channel->center_freq);
		if (!tabent_r3)
			return -ESRCH;
	} else {
		tabent_r2 = b43_nphy_get_chantabent_rev2(dev,
							channel->hw_value);
		if (!tabent_r2)
			return -ESRCH;
	}

	/* Channel is set later in common code, but we need to set it on our
	   own to let this function's subcalls work properly. */
	phy->channel = channel->hw_value;

#if 0
	if (b43_channel_type_is_40mhz(phy->channel_type) !=
		b43_channel_type_is_40mhz(channel_type))
		; /* TODO: BMAC BW Set (channel_type) */
#endif

	if (channel_type == NL80211_CHAN_HT40PLUS) {
		b43_phy_set(dev, B43_NPHY_RXCTL, B43_NPHY_RXCTL_BSELU20);
		if (phy->rev >= 7)
			b43_phy_set(dev, 0x310, 0x8000);
	} else if (channel_type == NL80211_CHAN_HT40MINUS) {
		b43_phy_mask(dev, B43_NPHY_RXCTL, ~B43_NPHY_RXCTL_BSELU20);
		if (phy->rev >= 7)
			b43_phy_mask(dev, 0x310, (u16)~0x8000);
	}

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		const struct b43_phy_n_sfo_cfg *phy_regs = tabent_r7 ?
			&(tabent_r7->phy_regs) : &(tabent_r7_2g->phy_regs);

		if (phy->radio_rev <= 4 || phy->radio_rev == 6) {
			tmp = (channel->band == IEEE80211_BAND_5GHZ) ? 2 : 0;
			b43_radio_maskset(dev, R2057_TIA_CONFIG_CORE0, ~2, tmp);
			b43_radio_maskset(dev, R2057_TIA_CONFIG_CORE1, ~2, tmp);
		}

		b43_radio_2057_setup(dev, tabent_r7, tabent_r7_2g);
		b43_nphy_channel_setup(dev, phy_regs, channel);
	} else if (phy->rev >= 3) {
		tmp = (channel->band == IEEE80211_BAND_5GHZ) ? 4 : 0;
		b43_radio_maskset(dev, 0x08, 0xFFFB, tmp);
		b43_radio_2056_setup(dev, tabent_r3);
		b43_nphy_channel_setup(dev, &(tabent_r3->phy_regs), channel);
	} else {
		tmp = (channel->band == IEEE80211_BAND_5GHZ) ? 0x0020 : 0x0050;
		b43_radio_maskset(dev, B2055_MASTER1, 0xFF8F, tmp);
		b43_radio_2055_setup(dev, tabent_r2);
		b43_nphy_channel_setup(dev, &(tabent_r2->phy_regs), channel);
	}

	return 0;
}

/**************************************************
 * Basic PHY ops.
 **************************************************/

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
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	memset(nphy, 0, sizeof(*nphy));

	nphy->hang_avoid = (phy->rev == 3 || phy->rev == 4);
	nphy->spur_avoid = (phy->rev >= 3) ?
				B43_SPUR_AVOID_AUTO : B43_SPUR_AVOID_DISABLE;
	nphy->gain_boost = true; /* this way we follow wl, assume it is true */
	nphy->txrx_chain = 2; /* sth different than 0 and 1 for now */
	nphy->phyrxchain = 3; /* to avoid b43_nphy_set_rx_core_state like wl */
	nphy->perical = 2; /* avoid additional rssi cal on init (like wl) */
	/* 128 can mean disabled-by-default state of TX pwr ctl. Max value is
	 * 0x7f == 127 and we check for 128 when restoring TX pwr ctl. */
	nphy->tx_pwr_idx[0] = 128;
	nphy->tx_pwr_idx[1] = 128;

	/* Hardware TX power control and 5GHz power gain */
	nphy->txpwrctrl = false;
	nphy->pwg_gain_5ghz = false;
	if (dev->phy.rev >= 3 ||
	    (dev->dev->board_vendor == PCI_VENDOR_ID_APPLE &&
	     (dev->dev->core_rev == 11 || dev->dev->core_rev == 12))) {
		nphy->txpwrctrl = true;
		nphy->pwg_gain_5ghz = true;
	} else if (sprom->revision >= 4) {
		if (dev->phy.rev >= 2 &&
		    (sprom->boardflags2_lo & B43_BFL2_TXPWRCTRL_EN)) {
			nphy->txpwrctrl = true;
#ifdef CONFIG_B43_SSB
			if (dev->dev->bus_type == B43_BUS_SSB &&
			    dev->dev->sdev->bus->bustype == SSB_BUSTYPE_PCI) {
				struct pci_dev *pdev =
					dev->dev->sdev->bus->host_pci;
				if (pdev->device == 0x4328 ||
				    pdev->device == 0x432a)
					nphy->pwg_gain_5ghz = true;
			}
#endif
		} else if (sprom->boardflags2_lo & B43_BFL2_5G_PWRGAIN) {
			nphy->pwg_gain_5ghz = true;
		}
	}

	if (dev->phy.rev >= 3) {
		nphy->ipa2g_on = sprom->fem.ghz2.extpa_gain == 2;
		nphy->ipa5g_on = sprom->fem.ghz5.extpa_gain == 2;
	}
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

static void b43_nphy_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				 u16 set)
{
	check_phyreg(dev, reg);
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_maskset16(dev, B43_MMIO_PHY_DATA, mask, set);
}

static u16 b43_nphy_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(dev->phy.rev < 7 && reg == 1);

	if (dev->phy.rev >= 7)
		reg |= 0x200; /* Radio 0x2057 */
	else
		reg |= 0x100;

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO_DATA_LOW);
}

static void b43_nphy_op_radio_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	/* Register 1 is a 32-bit register. */
	B43_WARN_ON(dev->phy.rev < 7 && reg == 1);

	b43_write16(dev, B43_MMIO_RADIO_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO_DATA_LOW, value);
}

/* http://bcm-v4.sipsolutions.net/802.11/Radio/Switch%20Radio */
static void b43_nphy_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{
	struct b43_phy *phy = &dev->phy;

	if (b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_ENABLED)
		b43err(dev->wl, "MAC not suspended\n");

	if (blocked) {
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 8) {
			b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
				     ~B43_NPHY_RFCTL_CMD_CHIP0PU);
		} else if (phy->rev >= 7) {
			/* Nothing needed */
		} else if (phy->rev >= 3) {
			b43_phy_mask(dev, B43_NPHY_RFCTL_CMD,
				     ~B43_NPHY_RFCTL_CMD_CHIP0PU);

			b43_radio_mask(dev, 0x09, ~0x2);

			b43_radio_write(dev, 0x204D, 0);
			b43_radio_write(dev, 0x2053, 0);
			b43_radio_write(dev, 0x2058, 0);
			b43_radio_write(dev, 0x205E, 0);
			b43_radio_mask(dev, 0x2062, ~0xF0);
			b43_radio_write(dev, 0x2064, 0);

			b43_radio_write(dev, 0x304D, 0);
			b43_radio_write(dev, 0x3053, 0);
			b43_radio_write(dev, 0x3058, 0);
			b43_radio_write(dev, 0x305E, 0);
			b43_radio_mask(dev, 0x3062, ~0xF0);
			b43_radio_write(dev, 0x3064, 0);
		}
	} else {
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 7) {
			if (!dev->phy.radio_on)
				b43_radio_2057_init(dev);
			b43_switch_channel(dev, dev->phy.channel);
		} else if (phy->rev >= 3) {
			if (!dev->phy.radio_on)
				b43_radio_init2056(dev);
			b43_switch_channel(dev, dev->phy.channel);
		} else {
			b43_radio_init2055(dev);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Anacore */
static void b43_nphy_op_switch_analog(struct b43_wldev *dev, bool on)
{
	struct b43_phy *phy = &dev->phy;
	u16 override = on ? 0x0 : 0x7FFF;
	u16 core = on ? 0xD : 0x00FD;

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 3) {
		if (on) {
			b43_phy_write(dev, B43_NPHY_AFECTL_C1, core);
			b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, override);
			b43_phy_write(dev, B43_NPHY_AFECTL_C2, core);
			b43_phy_write(dev, B43_NPHY_AFECTL_OVER, override);
		} else {
			b43_phy_write(dev, B43_NPHY_AFECTL_OVER1, override);
			b43_phy_write(dev, B43_NPHY_AFECTL_C1, core);
			b43_phy_write(dev, B43_NPHY_AFECTL_OVER, override);
			b43_phy_write(dev, B43_NPHY_AFECTL_C2, core);
		}
	} else {
		b43_phy_write(dev, B43_NPHY_AFECTL_OVER, override);
	}
}

static int b43_nphy_op_switch_channel(struct b43_wldev *dev,
				      unsigned int new_channel)
{
	struct ieee80211_channel *channel = dev->wl->hw->conf.chandef.chan;
	enum nl80211_channel_type channel_type =
		cfg80211_get_chandef_type(&dev->wl->hw->conf.chandef);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		if (new_channel > 200)
			return -EINVAL;
	}

	return b43_nphy_set_channel(dev, channel, channel_type);
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
	.phy_maskset		= b43_nphy_op_maskset,
	.radio_read		= b43_nphy_op_radio_read,
	.radio_write		= b43_nphy_op_radio_write,
	.software_rfkill	= b43_nphy_op_software_rfkill,
	.switch_analog		= b43_nphy_op_switch_analog,
	.switch_channel		= b43_nphy_op_switch_channel,
	.get_default_chan	= b43_nphy_op_get_default_chan,
	.recalc_txpower		= b43_nphy_op_recalc_txpower,
	.adjust_txpower		= b43_nphy_op_adjust_txpower,
};
