/*

  Broadcom B43 wireless driver
  IEEE 802.11n HT-PHY support

  Copyright (c) 2011 Rafał Miłecki <zajec5@gmail.com>

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

#include <linux/slab.h>

#include "b43.h"
#include "phy_ht.h"
#include "tables_phy_ht.h"
#include "radio_2059.h"
#include "main.h"

/* Force values to keep compatibility with wl */
enum ht_rssi_type {
	HT_RSSI_W1 = 0,
	HT_RSSI_W2 = 1,
	HT_RSSI_NB = 2,
	HT_RSSI_IQ = 3,
	HT_RSSI_TSSI_2G = 4,
	HT_RSSI_TSSI_5G = 5,
	HT_RSSI_TBD = 6,
};

/**************************************************
 * Radio 2059.
 **************************************************/

static void b43_radio_2059_channel_setup(struct b43_wldev *dev,
			const struct b43_phy_ht_channeltab_e_radio2059 *e)
{
	static const u16 routing[] = { R2059_C1, R2059_C2, R2059_C3, };
	u16 r;
	int core;

	b43_radio_write(dev, 0x16, e->radio_syn16);
	b43_radio_write(dev, 0x17, e->radio_syn17);
	b43_radio_write(dev, 0x22, e->radio_syn22);
	b43_radio_write(dev, 0x25, e->radio_syn25);
	b43_radio_write(dev, 0x27, e->radio_syn27);
	b43_radio_write(dev, 0x28, e->radio_syn28);
	b43_radio_write(dev, 0x29, e->radio_syn29);
	b43_radio_write(dev, 0x2c, e->radio_syn2c);
	b43_radio_write(dev, 0x2d, e->radio_syn2d);
	b43_radio_write(dev, 0x37, e->radio_syn37);
	b43_radio_write(dev, 0x41, e->radio_syn41);
	b43_radio_write(dev, 0x43, e->radio_syn43);
	b43_radio_write(dev, 0x47, e->radio_syn47);

	for (core = 0; core < 3; core++) {
		r = routing[core];
		b43_radio_write(dev, r | 0x4a, e->radio_rxtx4a);
		b43_radio_write(dev, r | 0x58, e->radio_rxtx58);
		b43_radio_write(dev, r | 0x5a, e->radio_rxtx5a);
		b43_radio_write(dev, r | 0x6a, e->radio_rxtx6a);
		b43_radio_write(dev, r | 0x6d, e->radio_rxtx6d);
		b43_radio_write(dev, r | 0x6e, e->radio_rxtx6e);
		b43_radio_write(dev, r | 0x92, e->radio_rxtx92);
		b43_radio_write(dev, r | 0x98, e->radio_rxtx98);
	}

	udelay(50);

	/* Calibration */
	b43_radio_mask(dev, 0x2b, ~0x1);
	b43_radio_mask(dev, 0x2e, ~0x4);
	b43_radio_set(dev, 0x2e, 0x4);
	b43_radio_set(dev, 0x2b, 0x1);

	udelay(300);
}

static void b43_radio_2059_init(struct b43_wldev *dev)
{
	const u16 routing[] = { R2059_C1, R2059_C2, R2059_C3 };
	const u16 radio_values[3][2] = {
		{ 0x61, 0xE9 }, { 0x69, 0xD5 }, { 0x73, 0x99 },
	};
	u16 i, j;

	b43_radio_write(dev, R2059_ALL | 0x51, 0x0070);
	b43_radio_write(dev, R2059_ALL | 0x5a, 0x0003);

	for (i = 0; i < ARRAY_SIZE(routing); i++)
		b43_radio_set(dev, routing[i] | 0x146, 0x3);

	b43_radio_set(dev, 0x2e, 0x0078);
	b43_radio_set(dev, 0xc0, 0x0080);
	msleep(2);
	b43_radio_mask(dev, 0x2e, ~0x0078);
	b43_radio_mask(dev, 0xc0, ~0x0080);

	if (1) { /* FIXME */
		b43_radio_set(dev, R2059_C3 | 0x4, 0x1);
		udelay(10);
		b43_radio_set(dev, R2059_C3 | 0x0BF, 0x1);
		b43_radio_maskset(dev, R2059_C3 | 0x19B, 0x3, 0x2);

		b43_radio_set(dev, R2059_C3 | 0x4, 0x2);
		udelay(100);
		b43_radio_mask(dev, R2059_C3 | 0x4, ~0x2);

		for (i = 0; i < 10000; i++) {
			if (b43_radio_read(dev, R2059_C3 | 0x145) & 1) {
				i = 0;
				break;
			}
			udelay(100);
		}
		if (i)
			b43err(dev->wl, "radio 0x945 timeout\n");

		b43_radio_mask(dev, R2059_C3 | 0x4, ~0x1);
		b43_radio_set(dev, 0xa, 0x60);

		for (i = 0; i < 3; i++) {
			b43_radio_write(dev, 0x17F, radio_values[i][0]);
			b43_radio_write(dev, 0x13D, 0x6E);
			b43_radio_write(dev, 0x13E, radio_values[i][1]);
			b43_radio_write(dev, 0x13C, 0x55);

			for (j = 0; j < 10000; j++) {
				if (b43_radio_read(dev, 0x140) & 2) {
					j = 0;
					break;
				}
				udelay(500);
			}
			if (j)
				b43err(dev->wl, "radio 0x140 timeout\n");

			b43_radio_write(dev, 0x13C, 0x15);
		}

		b43_radio_mask(dev, 0x17F, ~0x1);
	}

	b43_radio_mask(dev, 0x11, ~0x0008);
}

/**************************************************
 * RF
 **************************************************/

static void b43_phy_ht_force_rf_sequence(struct b43_wldev *dev, u16 rf_seq)
{
	u8 i;

	u16 save_seq_mode = b43_phy_read(dev, B43_PHY_HT_RF_SEQ_MODE);
	b43_phy_set(dev, B43_PHY_HT_RF_SEQ_MODE, 0x3);

	b43_phy_set(dev, B43_PHY_HT_RF_SEQ_TRIG, rf_seq);
	for (i = 0; i < 200; i++) {
		if (!(b43_phy_read(dev, B43_PHY_HT_RF_SEQ_STATUS) & rf_seq)) {
			i = 0;
			break;
		}
		msleep(1);
	}
	if (i)
		b43err(dev->wl, "Forcing RF sequence timeout\n");

	b43_phy_write(dev, B43_PHY_HT_RF_SEQ_MODE, save_seq_mode);
}

static void b43_phy_ht_pa_override(struct b43_wldev *dev, bool enable)
{
	struct b43_phy_ht *htphy = dev->phy.ht;
	static const u16 regs[3] = { B43_PHY_HT_RF_CTL_INT_C1,
				     B43_PHY_HT_RF_CTL_INT_C2,
				     B43_PHY_HT_RF_CTL_INT_C3 };
	int i;

	if (enable) {
		for (i = 0; i < 3; i++)
			b43_phy_write(dev, regs[i], htphy->rf_ctl_int_save[i]);
	} else {
		for (i = 0; i < 3; i++)
			htphy->rf_ctl_int_save[i] = b43_phy_read(dev, regs[i]);
		/* TODO: Does 5GHz band use different value (not 0x0400)? */
		for (i = 0; i < 3; i++)
			b43_phy_write(dev, regs[i], 0x0400);
	}
}

/**************************************************
 * Various PHY ops
 **************************************************/

static u16 b43_phy_ht_classifier(struct b43_wldev *dev, u16 mask, u16 val)
{
	u16 tmp;
	u16 allowed = B43_PHY_HT_CLASS_CTL_CCK_EN |
		      B43_PHY_HT_CLASS_CTL_OFDM_EN |
		      B43_PHY_HT_CLASS_CTL_WAITED_EN;

	tmp = b43_phy_read(dev, B43_PHY_HT_CLASS_CTL);
	tmp &= allowed;
	tmp &= ~mask;
	tmp |= (val & mask);
	b43_phy_maskset(dev, B43_PHY_HT_CLASS_CTL, ~allowed, tmp);

	return tmp;
}

static void b43_phy_ht_reset_cca(struct b43_wldev *dev)
{
	u16 bbcfg;

	b43_phy_force_clock(dev, true);
	bbcfg = b43_phy_read(dev, B43_PHY_HT_BBCFG);
	b43_phy_write(dev, B43_PHY_HT_BBCFG, bbcfg | B43_PHY_HT_BBCFG_RSTCCA);
	udelay(1);
	b43_phy_write(dev, B43_PHY_HT_BBCFG, bbcfg & ~B43_PHY_HT_BBCFG_RSTCCA);
	b43_phy_force_clock(dev, false);

	b43_phy_ht_force_rf_sequence(dev, B43_PHY_HT_RF_SEQ_TRIG_RST2RX);
}

static void b43_phy_ht_zero_extg(struct b43_wldev *dev)
{
	u8 i, j;
	u16 base[] = { 0x40, 0x60, 0x80 };

	for (i = 0; i < ARRAY_SIZE(base); i++) {
		for (j = 0; j < 4; j++)
			b43_phy_write(dev, B43_PHY_EXTG(base[i] + j), 0);
	}

	for (i = 0; i < ARRAY_SIZE(base); i++)
		b43_phy_write(dev, B43_PHY_EXTG(base[i] + 0xc), 0);
}

/* Some unknown AFE (Analog Frondned) op */
static void b43_phy_ht_afe_unk1(struct b43_wldev *dev)
{
	u8 i;

	static const u16 ctl_regs[3][2] = {
		{ B43_PHY_HT_AFE_C1_OVER, B43_PHY_HT_AFE_C1 },
		{ B43_PHY_HT_AFE_C2_OVER, B43_PHY_HT_AFE_C2 },
		{ B43_PHY_HT_AFE_C3_OVER, B43_PHY_HT_AFE_C3},
	};

	for (i = 0; i < 3; i++) {
		/* TODO: verify masks&sets */
		b43_phy_set(dev, ctl_regs[i][1], 0x4);
		b43_phy_set(dev, ctl_regs[i][0], 0x4);
		b43_phy_mask(dev, ctl_regs[i][1], ~0x1);
		b43_phy_set(dev, ctl_regs[i][0], 0x1);
		b43_httab_write(dev, B43_HTTAB16(8, 5 + (i * 0x10)), 0);
		b43_phy_mask(dev, ctl_regs[i][0], ~0x4);
	}
}

static void b43_phy_ht_read_clip_detection(struct b43_wldev *dev, u16 *clip_st)
{
	clip_st[0] = b43_phy_read(dev, B43_PHY_HT_C1_CLIP1THRES);
	clip_st[1] = b43_phy_read(dev, B43_PHY_HT_C2_CLIP1THRES);
	clip_st[2] = b43_phy_read(dev, B43_PHY_HT_C3_CLIP1THRES);
}

static void b43_phy_ht_bphy_init(struct b43_wldev *dev)
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

/**************************************************
 * Samples
 **************************************************/

static void b43_phy_ht_stop_playback(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	u16 tmp;
	int i;

	tmp = b43_phy_read(dev, B43_PHY_HT_SAMP_STAT);
	if (tmp & 0x1)
		b43_phy_set(dev, B43_PHY_HT_SAMP_CMD, B43_PHY_HT_SAMP_CMD_STOP);
	else if (tmp & 0x2)
		b43_phy_mask(dev, B43_PHY_HT_IQLOCAL_CMDGCTL, 0x7FFF);

	b43_phy_mask(dev, B43_PHY_HT_SAMP_CMD, ~0x0004);

	for (i = 0; i < 3; i++) {
		if (phy_ht->bb_mult_save[i] >= 0) {
			b43_httab_write(dev, B43_HTTAB16(13, 0x63 + i * 4),
					phy_ht->bb_mult_save[i]);
			b43_httab_write(dev, B43_HTTAB16(13, 0x67 + i * 4),
					phy_ht->bb_mult_save[i]);
		}
	}
}

static u16 b43_phy_ht_load_samples(struct b43_wldev *dev)
{
	int i;
	u16 len = 20 << 3;

	b43_phy_write(dev, B43_PHY_HT_TABLE_ADDR, 0x4400);

	for (i = 0; i < len; i++) {
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATAHI, 0);
		b43_phy_write(dev, B43_PHY_HT_TABLE_DATALO, 0);
	}

	return len;
}

static void b43_phy_ht_run_samples(struct b43_wldev *dev, u16 samps, u16 loops,
				   u16 wait)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	u16 save_seq_mode;
	int i;

	for (i = 0; i < 3; i++) {
		if (phy_ht->bb_mult_save[i] < 0)
			phy_ht->bb_mult_save[i] = b43_httab_read(dev, B43_HTTAB16(13, 0x63 + i * 4));
	}

	b43_phy_write(dev, B43_PHY_HT_SAMP_DEP_CNT, samps - 1);
	if (loops != 0xFFFF)
		loops--;
	b43_phy_write(dev, B43_PHY_HT_SAMP_LOOP_CNT, loops);
	b43_phy_write(dev, B43_PHY_HT_SAMP_WAIT_CNT, wait);

	save_seq_mode = b43_phy_read(dev, B43_PHY_HT_RF_SEQ_MODE);
	b43_phy_set(dev, B43_PHY_HT_RF_SEQ_MODE,
		    B43_PHY_HT_RF_SEQ_MODE_CA_OVER);

	/* TODO: find out mask bits! Do we need more function arguments? */
	b43_phy_mask(dev, B43_PHY_HT_SAMP_CMD, ~0);
	b43_phy_mask(dev, B43_PHY_HT_SAMP_CMD, ~0);
	b43_phy_mask(dev, B43_PHY_HT_IQLOCAL_CMDGCTL, ~0);
	b43_phy_set(dev, B43_PHY_HT_SAMP_CMD, 0x1);

	for (i = 0; i < 100; i++) {
		if (!(b43_phy_read(dev, B43_PHY_HT_RF_SEQ_STATUS) & 1)) {
			i = 0;
			break;
		}
		udelay(10);
	}
	if (i)
		b43err(dev->wl, "run samples timeout\n");

	b43_phy_write(dev, B43_PHY_HT_RF_SEQ_MODE, save_seq_mode);
}

static void b43_phy_ht_tx_tone(struct b43_wldev *dev)
{
	u16 samp;

	samp = b43_phy_ht_load_samples(dev);
	b43_phy_ht_run_samples(dev, samp, 0xFFFF, 0);
}

/**************************************************
 * RSSI
 **************************************************/

static void b43_phy_ht_rssi_select(struct b43_wldev *dev, u8 core_sel,
				   enum ht_rssi_type rssi_type)
{
	static const u16 ctl_regs[3][2] = {
		{ B43_PHY_HT_AFE_C1, B43_PHY_HT_AFE_C1_OVER, },
		{ B43_PHY_HT_AFE_C2, B43_PHY_HT_AFE_C2_OVER, },
		{ B43_PHY_HT_AFE_C3, B43_PHY_HT_AFE_C3_OVER, },
	};
	static const u16 radio_r[] = { R2059_C1, R2059_C2, R2059_C3, };
	int core;

	if (core_sel == 0) {
		b43err(dev->wl, "RSSI selection for core off not implemented yet\n");
	} else {
		for (core = 0; core < 3; core++) {
			/* Check if caller requested a one specific core */
			if ((core_sel == 1 && core != 0) ||
			    (core_sel == 2 && core != 1) ||
			    (core_sel == 3 && core != 2))
				continue;

			switch (rssi_type) {
			case HT_RSSI_TSSI_2G:
				b43_phy_set(dev, ctl_regs[core][0], 0x3 << 8);
				b43_phy_set(dev, ctl_regs[core][0], 0x3 << 10);
				b43_phy_set(dev, ctl_regs[core][1], 0x1 << 9);
				b43_phy_set(dev, ctl_regs[core][1], 0x1 << 10);

				b43_radio_set(dev, R2059_C3 | 0xbf, 0x1);
				b43_radio_write(dev, radio_r[core] | 0x159,
						0x11);
				break;
			default:
				b43err(dev->wl, "RSSI selection for type %d not implemented yet\n",
				       rssi_type);
			}
		}
	}
}

static void b43_phy_ht_poll_rssi(struct b43_wldev *dev, enum ht_rssi_type type,
				 s32 *buf, u8 nsamp)
{
	u16 phy_regs_values[12];
	static const u16 phy_regs_to_save[] = {
		B43_PHY_HT_AFE_C1, B43_PHY_HT_AFE_C1_OVER,
		0x848, 0x841,
		B43_PHY_HT_AFE_C2, B43_PHY_HT_AFE_C2_OVER,
		0x868, 0x861,
		B43_PHY_HT_AFE_C3, B43_PHY_HT_AFE_C3_OVER,
		0x888, 0x881,
	};
	u16 tmp[3];
	int i;

	for (i = 0; i < 12; i++)
		phy_regs_values[i] = b43_phy_read(dev, phy_regs_to_save[i]);

	b43_phy_ht_rssi_select(dev, 5, type);

	for (i = 0; i < 6; i++)
		buf[i] = 0;

	for (i = 0; i < nsamp; i++) {
		tmp[0] = b43_phy_read(dev, B43_PHY_HT_RSSI_C1);
		tmp[1] = b43_phy_read(dev, B43_PHY_HT_RSSI_C2);
		tmp[2] = b43_phy_read(dev, B43_PHY_HT_RSSI_C3);

		buf[0] += ((s8)((tmp[0] & 0x3F) << 2)) >> 2;
		buf[1] += ((s8)(((tmp[0] >> 8) & 0x3F) << 2)) >> 2;
		buf[2] += ((s8)((tmp[1] & 0x3F) << 2)) >> 2;
		buf[3] += ((s8)(((tmp[1] >> 8) & 0x3F) << 2)) >> 2;
		buf[4] += ((s8)((tmp[2] & 0x3F) << 2)) >> 2;
		buf[5] += ((s8)(((tmp[2] >> 8) & 0x3F) << 2)) >> 2;
	}

	for (i = 0; i < 12; i++)
		b43_phy_write(dev, phy_regs_to_save[i], phy_regs_values[i]);
}

/**************************************************
 * Tx/Rx
 **************************************************/

static void b43_phy_ht_tx_power_fix(struct b43_wldev *dev)
{
	int i;

	for (i = 0; i < 3; i++) {
		u16 mask;
		u32 tmp = b43_httab_read(dev, B43_HTTAB32(26, 0xE8));

		if (0) /* FIXME */
			mask = 0x2 << (i * 4);
		else
			mask = 0;
		b43_phy_mask(dev, B43_PHY_EXTG(0x108), mask);

		b43_httab_write(dev, B43_HTTAB16(7, 0x110 + i), tmp >> 16);
		b43_httab_write(dev, B43_HTTAB8(13, 0x63 + (i * 4)),
				tmp & 0xFF);
		b43_httab_write(dev, B43_HTTAB8(13, 0x73 + (i * 4)),
				tmp & 0xFF);
	}
}

static void b43_phy_ht_tx_power_ctl(struct b43_wldev *dev, bool enable)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	u16 en_bits = B43_PHY_HT_TXPCTL_CMD_C1_COEFF |
		      B43_PHY_HT_TXPCTL_CMD_C1_HWPCTLEN |
		      B43_PHY_HT_TXPCTL_CMD_C1_PCTLEN;
	static const u16 cmd_regs[3] = { B43_PHY_HT_TXPCTL_CMD_C1,
					 B43_PHY_HT_TXPCTL_CMD_C2,
					 B43_PHY_HT_TXPCTL_CMD_C3 };
	static const u16 status_regs[3] = { B43_PHY_HT_TX_PCTL_STATUS_C1,
					    B43_PHY_HT_TX_PCTL_STATUS_C2,
					    B43_PHY_HT_TX_PCTL_STATUS_C3 };
	int i;

	if (!enable) {
		if (b43_phy_read(dev, B43_PHY_HT_TXPCTL_CMD_C1) & en_bits) {
			/* We disable enabled TX pwr ctl, save it's state */
			for (i = 0; i < 3; i++)
				phy_ht->tx_pwr_idx[i] =
					b43_phy_read(dev, status_regs[i]);
		}
		b43_phy_mask(dev, B43_PHY_HT_TXPCTL_CMD_C1, ~en_bits);
	} else {
		b43_phy_set(dev, B43_PHY_HT_TXPCTL_CMD_C1, en_bits);

		if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ) {
			for (i = 0; i < 3; i++)
				b43_phy_write(dev, cmd_regs[i], 0x32);
		}

		for (i = 0; i < 3; i++)
			if (phy_ht->tx_pwr_idx[i] <=
			    B43_PHY_HT_TXPCTL_CMD_C1_INIT)
				b43_phy_write(dev, cmd_regs[i],
					      phy_ht->tx_pwr_idx[i]);
	}

	phy_ht->tx_pwr_ctl = enable;
}

static void b43_phy_ht_tx_power_ctl_idle_tssi(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	static const u16 base[] = { 0x840, 0x860, 0x880 };
	u16 save_regs[3][3];
	s32 rssi_buf[6];
	int core;

	for (core = 0; core < 3; core++) {
		save_regs[core][1] = b43_phy_read(dev, base[core] + 6);
		save_regs[core][2] = b43_phy_read(dev, base[core] + 7);
		save_regs[core][0] = b43_phy_read(dev, base[core] + 0);

		b43_phy_write(dev, base[core] + 6, 0);
		b43_phy_mask(dev, base[core] + 7, ~0xF); /* 0xF? Or just 0x6? */
		b43_phy_set(dev, base[core] + 0, 0x0400);
		b43_phy_set(dev, base[core] + 0, 0x1000);
	}

	b43_phy_ht_tx_tone(dev);
	udelay(20);
	b43_phy_ht_poll_rssi(dev, HT_RSSI_TSSI_2G, rssi_buf, 1);
	b43_phy_ht_stop_playback(dev);
	b43_phy_ht_reset_cca(dev);

	phy_ht->idle_tssi[0] = rssi_buf[0] & 0xff;
	phy_ht->idle_tssi[1] = rssi_buf[2] & 0xff;
	phy_ht->idle_tssi[2] = rssi_buf[4] & 0xff;

	for (core = 0; core < 3; core++) {
		b43_phy_write(dev, base[core] + 0, save_regs[core][0]);
		b43_phy_write(dev, base[core] + 6, save_regs[core][1]);
		b43_phy_write(dev, base[core] + 7, save_regs[core][2]);
	}
}

static void b43_phy_ht_tssi_setup(struct b43_wldev *dev)
{
	static const u16 routing[] = { R2059_C1, R2059_C2, R2059_C3, };
	int core;

	/* 0x159 is probably TX_SSI_MUX or TSSIG (by comparing to N-PHY) */
	for (core = 0; core < 3; core++) {
		b43_radio_set(dev, 0x8bf, 0x1);
		b43_radio_write(dev, routing[core] | 0x0159, 0x0011);
	}
}

static void b43_phy_ht_tx_power_ctl_setup(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	struct ssb_sprom *sprom = dev->dev->bus_sprom;

	u8 *idle = phy_ht->idle_tssi;
	u8 target[3];
	s16 a1[3], b0[3], b1[3];

	u16 freq = dev->phy.chandef->chan->center_freq;
	int i, c;

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		for (c = 0; c < 3; c++) {
			target[c] = sprom->core_pwr_info[c].maxpwr_2g;
			a1[c] = sprom->core_pwr_info[c].pa_2g[0];
			b0[c] = sprom->core_pwr_info[c].pa_2g[1];
			b1[c] = sprom->core_pwr_info[c].pa_2g[2];
		}
	} else if (freq >= 4900 && freq < 5100) {
		for (c = 0; c < 3; c++) {
			target[c] = sprom->core_pwr_info[c].maxpwr_5gl;
			a1[c] = sprom->core_pwr_info[c].pa_5gl[0];
			b0[c] = sprom->core_pwr_info[c].pa_5gl[1];
			b1[c] = sprom->core_pwr_info[c].pa_5gl[2];
		}
	} else if (freq >= 5100 && freq < 5500) {
		for (c = 0; c < 3; c++) {
			target[c] = sprom->core_pwr_info[c].maxpwr_5g;
			a1[c] = sprom->core_pwr_info[c].pa_5g[0];
			b0[c] = sprom->core_pwr_info[c].pa_5g[1];
			b1[c] = sprom->core_pwr_info[c].pa_5g[2];
		}
	} else if (freq >= 5500) {
		for (c = 0; c < 3; c++) {
			target[c] = sprom->core_pwr_info[c].maxpwr_5gh;
			a1[c] = sprom->core_pwr_info[c].pa_5gh[0];
			b0[c] = sprom->core_pwr_info[c].pa_5gh[1];
			b1[c] = sprom->core_pwr_info[c].pa_5gh[2];
		}
	} else {
		target[0] = target[1] = target[2] = 52;
		a1[0] = a1[1] = a1[2] = -424;
		b0[0] = b0[1] = b0[2] = 5612;
		b1[0] = b1[1] = b1[2] = -1393;
	}

	b43_phy_set(dev, B43_PHY_HT_TSSIMODE, B43_PHY_HT_TSSIMODE_EN);
	b43_phy_mask(dev, B43_PHY_HT_TXPCTL_CMD_C1,
		     ~B43_PHY_HT_TXPCTL_CMD_C1_PCTLEN & 0xFFFF);

	/* TODO: Does it depend on sprom->fem.ghz2.tssipos? */
	b43_phy_set(dev, B43_PHY_HT_TXPCTL_IDLE_TSSI, 0x4000);

	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_CMD_C1,
			~B43_PHY_HT_TXPCTL_CMD_C1_INIT, 0x19);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_CMD_C2,
			~B43_PHY_HT_TXPCTL_CMD_C2_INIT, 0x19);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_CMD_C3,
			~B43_PHY_HT_TXPCTL_CMD_C3_INIT, 0x19);

	b43_phy_set(dev, B43_PHY_HT_TXPCTL_IDLE_TSSI,
		    B43_PHY_HT_TXPCTL_IDLE_TSSI_BINF);

	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_IDLE_TSSI,
			~B43_PHY_HT_TXPCTL_IDLE_TSSI_C1,
			idle[0] << B43_PHY_HT_TXPCTL_IDLE_TSSI_C1_SHIFT);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_IDLE_TSSI,
			~B43_PHY_HT_TXPCTL_IDLE_TSSI_C2,
			idle[1] << B43_PHY_HT_TXPCTL_IDLE_TSSI_C2_SHIFT);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_IDLE_TSSI2,
			~B43_PHY_HT_TXPCTL_IDLE_TSSI2_C3,
			idle[2] << B43_PHY_HT_TXPCTL_IDLE_TSSI2_C3_SHIFT);

	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_N, ~B43_PHY_HT_TXPCTL_N_TSSID,
			0xf0);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_N, ~B43_PHY_HT_TXPCTL_N_NPTIL2,
			0x3 << B43_PHY_HT_TXPCTL_N_NPTIL2_SHIFT);
#if 0
	/* TODO: what to mask/set? */
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_CMD_C1, 0x800, 0)
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_CMD_C1, 0x400, 0)
#endif

	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_TARG_PWR,
			~B43_PHY_HT_TXPCTL_TARG_PWR_C1,
			target[0] << B43_PHY_HT_TXPCTL_TARG_PWR_C1_SHIFT);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_TARG_PWR,
			~B43_PHY_HT_TXPCTL_TARG_PWR_C2 & 0xFFFF,
			target[1] << B43_PHY_HT_TXPCTL_TARG_PWR_C2_SHIFT);
	b43_phy_maskset(dev, B43_PHY_HT_TXPCTL_TARG_PWR2,
			~B43_PHY_HT_TXPCTL_TARG_PWR2_C3,
			target[2] << B43_PHY_HT_TXPCTL_TARG_PWR2_C3_SHIFT);

	for (c = 0; c < 3; c++) {
		s32 num, den, pwr;
		u32 regval[64];

		for (i = 0; i < 64; i++) {
			num = 8 * (16 * b0[c] + b1[c] * i);
			den = 32768 + a1[c] * i;
			pwr = max((4 * num + den / 2) / den, -8);
			regval[i] = pwr;
		}
		b43_httab_write_bulk(dev, B43_HTTAB16(26 + c, 0), 64, regval);
	}
}

/**************************************************
 * Channel switching ops.
 **************************************************/

static void b43_phy_ht_spur_avoid(struct b43_wldev *dev,
				  struct ieee80211_channel *new_channel)
{
	struct bcma_device *core = dev->dev->bdev;
	int spuravoid = 0;
	u16 tmp;

	/* Check for 13 and 14 is just a guess, we don't have enough logs. */
	if (new_channel->hw_value == 13 || new_channel->hw_value == 14)
		spuravoid = 1;
	bcma_core_pll_ctl(core, B43_BCMA_CLKCTLST_PHY_PLL_REQ, 0, false);
	bcma_pmu_spuravoid_pllupdate(&core->bus->drv_cc, spuravoid);
	bcma_core_pll_ctl(core,
			  B43_BCMA_CLKCTLST_80211_PLL_REQ |
			  B43_BCMA_CLKCTLST_PHY_PLL_REQ,
			  B43_BCMA_CLKCTLST_80211_PLL_ST |
			  B43_BCMA_CLKCTLST_PHY_PLL_ST, false);

	/* Values has been taken from wlc_bmac_switch_macfreq comments */
	switch (spuravoid) {
	case 2: /* 126MHz */
		tmp = 0x2082;
		break;
	case 1: /* 123MHz */
		tmp = 0x5341;
		break;
	default: /* 120MHz */
		tmp = 0x8889;
	}

	b43_write16(dev, B43_MMIO_TSF_CLK_FRAC_LOW, tmp);
	b43_write16(dev, B43_MMIO_TSF_CLK_FRAC_HIGH, 0x8);

	/* TODO: reset PLL */

	if (spuravoid)
		b43_phy_set(dev, B43_PHY_HT_BBCFG, B43_PHY_HT_BBCFG_RSTRX);
	else
		b43_phy_mask(dev, B43_PHY_HT_BBCFG,
				~B43_PHY_HT_BBCFG_RSTRX & 0xFFFF);

	b43_phy_ht_reset_cca(dev);
}

static void b43_phy_ht_channel_setup(struct b43_wldev *dev,
				const struct b43_phy_ht_channeltab_e_phy *e,
				struct ieee80211_channel *new_channel)
{
	bool old_band_5ghz;

	old_band_5ghz = b43_phy_read(dev, B43_PHY_HT_BANDCTL) & 0; /* FIXME */
	if (new_channel->band == IEEE80211_BAND_5GHZ && !old_band_5ghz) {
		/* TODO */
	} else if (new_channel->band == IEEE80211_BAND_2GHZ && old_band_5ghz) {
		/* TODO */
	}

	b43_phy_write(dev, B43_PHY_HT_BW1, e->bw1);
	b43_phy_write(dev, B43_PHY_HT_BW2, e->bw2);
	b43_phy_write(dev, B43_PHY_HT_BW3, e->bw3);
	b43_phy_write(dev, B43_PHY_HT_BW4, e->bw4);
	b43_phy_write(dev, B43_PHY_HT_BW5, e->bw5);
	b43_phy_write(dev, B43_PHY_HT_BW6, e->bw6);

	if (new_channel->hw_value == 14) {
		b43_phy_ht_classifier(dev, B43_PHY_HT_CLASS_CTL_OFDM_EN, 0);
		b43_phy_set(dev, B43_PHY_HT_TEST, 0x0800);
	} else {
		b43_phy_ht_classifier(dev, B43_PHY_HT_CLASS_CTL_OFDM_EN,
				      B43_PHY_HT_CLASS_CTL_OFDM_EN);
		if (new_channel->band == IEEE80211_BAND_2GHZ)
			b43_phy_mask(dev, B43_PHY_HT_TEST, ~0x840);
	}

	if (1) /* TODO: On N it's for early devices only, what about HT? */
		b43_phy_ht_tx_power_fix(dev);

	b43_phy_ht_spur_avoid(dev, new_channel);

	b43_phy_write(dev, 0x017e, 0x3830);
}

static int b43_phy_ht_set_channel(struct b43_wldev *dev,
				  struct ieee80211_channel *channel,
				  enum nl80211_channel_type channel_type)
{
	struct b43_phy *phy = &dev->phy;

	const struct b43_phy_ht_channeltab_e_radio2059 *chent_r2059 = NULL;

	if (phy->radio_ver == 0x2059) {
		chent_r2059 = b43_phy_ht_get_channeltab_e_r2059(dev,
							channel->center_freq);
		if (!chent_r2059)
			return -ESRCH;
	} else {
		return -ESRCH;
	}

	/* TODO: In case of N-PHY some bandwidth switching goes here */

	if (phy->radio_ver == 0x2059) {
		b43_radio_2059_channel_setup(dev, chent_r2059);
		b43_phy_ht_channel_setup(dev, &(chent_r2059->phy_regs),
					 channel);
	} else {
		return -ESRCH;
	}

	return 0;
}

/**************************************************
 * Basic PHY ops.
 **************************************************/

static int b43_phy_ht_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht;

	phy_ht = kzalloc(sizeof(*phy_ht), GFP_KERNEL);
	if (!phy_ht)
		return -ENOMEM;
	dev->phy.ht = phy_ht;

	return 0;
}

static void b43_phy_ht_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_ht *phy_ht = phy->ht;
	int i;

	memset(phy_ht, 0, sizeof(*phy_ht));

	phy_ht->tx_pwr_ctl = true;
	for (i = 0; i < 3; i++)
		phy_ht->tx_pwr_idx[i] = B43_PHY_HT_TXPCTL_CMD_C1_INIT + 1;

	for (i = 0; i < 3; i++)
		phy_ht->bb_mult_save[i] = -1;
}

static int b43_phy_ht_op_init(struct b43_wldev *dev)
{
	struct b43_phy_ht *phy_ht = dev->phy.ht;
	u16 tmp;
	u16 clip_state[3];
	bool saved_tx_pwr_ctl;

	if (dev->dev->bus_type != B43_BUS_BCMA) {
		b43err(dev->wl, "HT-PHY is supported only on BCMA bus!\n");
		return -EOPNOTSUPP;
	}

	b43_phy_ht_tables_init(dev);

	b43_phy_mask(dev, 0x0be, ~0x2);
	b43_phy_set(dev, 0x23f, 0x7ff);
	b43_phy_set(dev, 0x240, 0x7ff);
	b43_phy_set(dev, 0x241, 0x7ff);

	b43_phy_ht_zero_extg(dev);

	b43_phy_mask(dev, B43_PHY_EXTG(0), ~0x3);

	b43_phy_write(dev, B43_PHY_HT_AFE_C1_OVER, 0);
	b43_phy_write(dev, B43_PHY_HT_AFE_C2_OVER, 0);
	b43_phy_write(dev, B43_PHY_HT_AFE_C3_OVER, 0);

	b43_phy_write(dev, B43_PHY_EXTG(0x103), 0x20);
	b43_phy_write(dev, B43_PHY_EXTG(0x101), 0x20);
	b43_phy_write(dev, 0x20d, 0xb8);
	b43_phy_write(dev, B43_PHY_EXTG(0x14f), 0xc8);
	b43_phy_write(dev, 0x70, 0x50);
	b43_phy_write(dev, 0x1ff, 0x30);

	if (0) /* TODO: condition */
		; /* TODO: PHY op on reg 0x217 */

	if (b43_current_band(dev->wl) == IEEE80211_BAND_5GHZ)
		b43_phy_ht_classifier(dev, B43_PHY_HT_CLASS_CTL_CCK_EN, 0);
	else
		b43_phy_ht_classifier(dev, B43_PHY_HT_CLASS_CTL_CCK_EN,
				      B43_PHY_HT_CLASS_CTL_CCK_EN);

	b43_phy_set(dev, 0xb1, 0x91);
	b43_phy_write(dev, 0x32f, 0x0003);
	b43_phy_write(dev, 0x077, 0x0010);
	b43_phy_write(dev, 0x0b4, 0x0258);
	b43_phy_mask(dev, 0x17e, ~0x4000);

	b43_phy_write(dev, 0x0b9, 0x0072);

	b43_httab_write_few(dev, B43_HTTAB16(7, 0x14e), 2, 0x010f, 0x010f);
	b43_httab_write_few(dev, B43_HTTAB16(7, 0x15e), 2, 0x010f, 0x010f);
	b43_httab_write_few(dev, B43_HTTAB16(7, 0x16e), 2, 0x010f, 0x010f);

	b43_phy_ht_afe_unk1(dev);

	b43_httab_write_few(dev, B43_HTTAB16(7, 0x130), 9, 0x777, 0x111, 0x111,
			    0x777, 0x111, 0x111, 0x777, 0x111, 0x111);

	b43_httab_write(dev, B43_HTTAB16(7, 0x120), 0x0777);
	b43_httab_write(dev, B43_HTTAB16(7, 0x124), 0x0777);

	b43_httab_write(dev, B43_HTTAB16(8, 0x00), 0x02);
	b43_httab_write(dev, B43_HTTAB16(8, 0x10), 0x02);
	b43_httab_write(dev, B43_HTTAB16(8, 0x20), 0x02);

	b43_httab_write_few(dev, B43_HTTAB16(8, 0x08), 4,
			    0x8e, 0x96, 0x96, 0x96);
	b43_httab_write_few(dev, B43_HTTAB16(8, 0x18), 4,
			    0x8f, 0x9f, 0x9f, 0x9f);
	b43_httab_write_few(dev, B43_HTTAB16(8, 0x28), 4,
			    0x8f, 0x9f, 0x9f, 0x9f);

	b43_httab_write_few(dev, B43_HTTAB16(8, 0x0c), 4, 0x2, 0x2, 0x2, 0x2);
	b43_httab_write_few(dev, B43_HTTAB16(8, 0x1c), 4, 0x2, 0x2, 0x2, 0x2);
	b43_httab_write_few(dev, B43_HTTAB16(8, 0x2c), 4, 0x2, 0x2, 0x2, 0x2);

	b43_phy_maskset(dev, 0x0280, 0xff00, 0x3e);
	b43_phy_maskset(dev, 0x0283, 0xff00, 0x3e);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x0141), 0xff00, 0x46);
	b43_phy_maskset(dev, 0x0283, 0xff00, 0x40);

	b43_httab_write_few(dev, B43_HTTAB16(00, 0x8), 4,
			    0x09, 0x0e, 0x13, 0x18);
	b43_httab_write_few(dev, B43_HTTAB16(01, 0x8), 4,
			    0x09, 0x0e, 0x13, 0x18);
	/* TODO: Did wl mean 2 instead of 40? */
	b43_httab_write_few(dev, B43_HTTAB16(40, 0x8), 4,
			    0x09, 0x0e, 0x13, 0x18);

	b43_phy_maskset(dev, B43_PHY_OFDM(0x24), 0x3f, 0xd);
	b43_phy_maskset(dev, B43_PHY_OFDM(0x64), 0x3f, 0xd);
	b43_phy_maskset(dev, B43_PHY_OFDM(0xa4), 0x3f, 0xd);

	b43_phy_set(dev, B43_PHY_EXTG(0x060), 0x1);
	b43_phy_set(dev, B43_PHY_EXTG(0x064), 0x1);
	b43_phy_set(dev, B43_PHY_EXTG(0x080), 0x1);
	b43_phy_set(dev, B43_PHY_EXTG(0x084), 0x1);

	/* Copy some tables entries */
	tmp = b43_httab_read(dev, B43_HTTAB16(7, 0x144));
	b43_httab_write(dev, B43_HTTAB16(7, 0x14a), tmp);
	tmp = b43_httab_read(dev, B43_HTTAB16(7, 0x154));
	b43_httab_write(dev, B43_HTTAB16(7, 0x15a), tmp);
	tmp = b43_httab_read(dev, B43_HTTAB16(7, 0x164));
	b43_httab_write(dev, B43_HTTAB16(7, 0x16a), tmp);

	/* Reset CCA */
	b43_phy_force_clock(dev, true);
	tmp = b43_phy_read(dev, B43_PHY_HT_BBCFG);
	b43_phy_write(dev, B43_PHY_HT_BBCFG, tmp | B43_PHY_HT_BBCFG_RSTCCA);
	b43_phy_write(dev, B43_PHY_HT_BBCFG, tmp & ~B43_PHY_HT_BBCFG_RSTCCA);
	b43_phy_force_clock(dev, false);

	b43_mac_phy_clock_set(dev, true);

	b43_phy_ht_pa_override(dev, false);
	b43_phy_ht_force_rf_sequence(dev, B43_PHY_HT_RF_SEQ_TRIG_RX2TX);
	b43_phy_ht_force_rf_sequence(dev, B43_PHY_HT_RF_SEQ_TRIG_RST2RX);
	b43_phy_ht_pa_override(dev, true);

	/* TODO: Should we restore it? Or store it in global PHY info? */
	b43_phy_ht_classifier(dev, 0, 0);
	b43_phy_ht_read_clip_detection(dev, clip_state);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		b43_phy_ht_bphy_init(dev);

	b43_httab_write_bulk(dev, B43_HTTAB32(0x1a, 0xc0),
			B43_HTTAB_1A_C0_LATE_SIZE, b43_httab_0x1a_0xc0_late);

	saved_tx_pwr_ctl = phy_ht->tx_pwr_ctl;
	b43_phy_ht_tx_power_fix(dev);
	b43_phy_ht_tx_power_ctl(dev, false);
	b43_phy_ht_tx_power_ctl_idle_tssi(dev);
	b43_phy_ht_tx_power_ctl_setup(dev);
	b43_phy_ht_tssi_setup(dev);
	b43_phy_ht_tx_power_ctl(dev, saved_tx_pwr_ctl);

	return 0;
}

static void b43_phy_ht_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_ht *phy_ht = phy->ht;

	kfree(phy_ht);
	phy->ht = NULL;
}

/* http://bcm-v4.sipsolutions.net/802.11/Radio/Switch%20Radio */
static void b43_phy_ht_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{
	if (b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_ENABLED)
		b43err(dev->wl, "MAC not suspended\n");

	/* In the following PHY ops we copy wl's dummy behaviour.
	 * TODO: Find out if reads (currently hidden in masks/masksets) are
	 * needed and replace following ops with just writes or w&r.
	 * Note: B43_PHY_HT_RF_CTL1 register is tricky, wrong operation can
	 * cause delayed (!) machine lock up. */
	if (blocked) {
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
	} else {
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
		b43_phy_maskset(dev, B43_PHY_HT_RF_CTL1, 0, 0x1);
		b43_phy_mask(dev, B43_PHY_HT_RF_CTL1, 0);
		b43_phy_maskset(dev, B43_PHY_HT_RF_CTL1, 0, 0x2);

		if (dev->phy.radio_ver == 0x2059)
			b43_radio_2059_init(dev);
		else
			B43_WARN_ON(1);

		b43_switch_channel(dev, dev->phy.channel);
	}
}

static void b43_phy_ht_op_switch_analog(struct b43_wldev *dev, bool on)
{
	if (on) {
		b43_phy_write(dev, B43_PHY_HT_AFE_C1, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_C1_OVER, 0x0000);
		b43_phy_write(dev, B43_PHY_HT_AFE_C2, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_C2_OVER, 0x0000);
		b43_phy_write(dev, B43_PHY_HT_AFE_C3, 0x00cd);
		b43_phy_write(dev, B43_PHY_HT_AFE_C3_OVER, 0x0000);
	} else {
		b43_phy_write(dev, B43_PHY_HT_AFE_C1_OVER, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_C1, 0x00fd);
		b43_phy_write(dev, B43_PHY_HT_AFE_C2_OVER, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_C2, 0x00fd);
		b43_phy_write(dev, B43_PHY_HT_AFE_C3_OVER, 0x07ff);
		b43_phy_write(dev, B43_PHY_HT_AFE_C3, 0x00fd);
	}
}

static int b43_phy_ht_op_switch_channel(struct b43_wldev *dev,
					unsigned int new_channel)
{
	struct ieee80211_channel *channel = dev->wl->hw->conf.chandef.chan;
	enum nl80211_channel_type channel_type =
		cfg80211_get_chandef_type(&dev->wl->hw->conf.chandef);

	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return b43_phy_ht_set_channel(dev, channel, channel_type);
}

static unsigned int b43_phy_ht_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == IEEE80211_BAND_2GHZ)
		return 11;
	return 36;
}

/**************************************************
 * R/W ops.
 **************************************************/

static u16 b43_phy_ht_op_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

static void b43_phy_ht_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA, value);
}

static void b43_phy_ht_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				 u16 set)
{
	b43_write16(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
}

static u16 b43_phy_ht_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* HT-PHY needs 0x200 for read access */
	reg |= 0x200;

	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO24_DATA);
}

static void b43_phy_ht_op_radio_write(struct b43_wldev *dev, u16 reg,
				      u16 value)
{
	b43_write16(dev, B43_MMIO_RADIO24_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO24_DATA, value);
}

static enum b43_txpwr_result
b43_phy_ht_op_recalc_txpower(struct b43_wldev *dev, bool ignore_tssi)
{
	return B43_TXPWR_RES_DONE;
}

static void b43_phy_ht_op_adjust_txpower(struct b43_wldev *dev)
{
}

/**************************************************
 * PHY ops struct.
 **************************************************/

const struct b43_phy_operations b43_phyops_ht = {
	.allocate		= b43_phy_ht_op_allocate,
	.free			= b43_phy_ht_op_free,
	.prepare_structs	= b43_phy_ht_op_prepare_structs,
	.init			= b43_phy_ht_op_init,
	.phy_read		= b43_phy_ht_op_read,
	.phy_write		= b43_phy_ht_op_write,
	.phy_maskset		= b43_phy_ht_op_maskset,
	.radio_read		= b43_phy_ht_op_radio_read,
	.radio_write		= b43_phy_ht_op_radio_write,
	.software_rfkill	= b43_phy_ht_op_software_rfkill,
	.switch_analog		= b43_phy_ht_op_switch_analog,
	.switch_channel		= b43_phy_ht_op_switch_channel,
	.get_default_chan	= b43_phy_ht_op_get_default_chan,
	.recalc_txpower		= b43_phy_ht_op_recalc_txpower,
	.adjust_txpower		= b43_phy_ht_op_adjust_txpower,
};
