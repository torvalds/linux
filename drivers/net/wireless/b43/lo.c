/*

  Broadcom B43 wireless driver

  G PHY LO (LocalOscillator) Measuring and Control routines

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005, 2006 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2007 Michael Buesch <mb@bu3sch.de>
  Copyright (c) 2005, 2006 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005, 2006 Andreas Jaggi <andreas.jaggi@waterwave.ch>

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
#include "lo.h"
#include "phy_g.h"
#include "main.h"

#include <linux/delay.h>
#include <linux/sched.h>


static struct b43_lo_calib *b43_find_lo_calib(struct b43_txpower_lo_control *lo,
					      const struct b43_bbatt *bbatt,
					       const struct b43_rfatt *rfatt)
{
	struct b43_lo_calib *c;

	list_for_each_entry(c, &lo->calib_list, list) {
		if (!b43_compare_bbatt(&c->bbatt, bbatt))
			continue;
		if (!b43_compare_rfatt(&c->rfatt, rfatt))
			continue;
		return c;
	}

	return NULL;
}

/* Write the LocalOscillator Control (adjust) value-pair. */
static void b43_lo_write(struct b43_wldev *dev, struct b43_loctl *control)
{
	struct b43_phy *phy = &dev->phy;
	u16 value;

	if (B43_DEBUG) {
		if (unlikely(abs(control->i) > 16 || abs(control->q) > 16)) {
			b43dbg(dev->wl, "Invalid LO control pair "
			       "(I: %d, Q: %d)\n", control->i, control->q);
			dump_stack();
			return;
		}
	}
	B43_WARN_ON(phy->type != B43_PHYTYPE_G);

	value = (u8) (control->q);
	value |= ((u8) (control->i)) << 8;
	b43_phy_write(dev, B43_PHY_LO_CTL, value);
}

static u16 lo_measure_feedthrough(struct b43_wldev *dev,
				  u16 lna, u16 pga, u16 trsw_rx)
{
	struct b43_phy *phy = &dev->phy;
	u16 rfover;
	u16 feedthrough;

	if (phy->gmode) {
		lna <<= B43_PHY_RFOVERVAL_LNA_SHIFT;
		pga <<= B43_PHY_RFOVERVAL_PGA_SHIFT;

		B43_WARN_ON(lna & ~B43_PHY_RFOVERVAL_LNA);
		B43_WARN_ON(pga & ~B43_PHY_RFOVERVAL_PGA);
/*FIXME This assertion fails		B43_WARN_ON(trsw_rx & ~(B43_PHY_RFOVERVAL_TRSWRX |
				    B43_PHY_RFOVERVAL_BW));
*/
		trsw_rx &= (B43_PHY_RFOVERVAL_TRSWRX | B43_PHY_RFOVERVAL_BW);

		/* Construct the RF Override Value */
		rfover = B43_PHY_RFOVERVAL_UNK;
		rfover |= pga;
		rfover |= lna;
		rfover |= trsw_rx;
		if ((dev->dev->bus->sprom.boardflags_lo & B43_BFL_EXTLNA)
		    && phy->rev > 6)
			rfover |= B43_PHY_RFOVERVAL_EXTLNA;

		b43_phy_write(dev, B43_PHY_PGACTL, 0xE300);
		b43_phy_write(dev, B43_PHY_RFOVERVAL, rfover);
		udelay(10);
		rfover |= B43_PHY_RFOVERVAL_BW_LBW;
		b43_phy_write(dev, B43_PHY_RFOVERVAL, rfover);
		udelay(10);
		rfover |= B43_PHY_RFOVERVAL_BW_LPF;
		b43_phy_write(dev, B43_PHY_RFOVERVAL, rfover);
		udelay(10);
		b43_phy_write(dev, B43_PHY_PGACTL, 0xF300);
	} else {
		pga |= B43_PHY_PGACTL_UNKNOWN;
		b43_phy_write(dev, B43_PHY_PGACTL, pga);
		udelay(10);
		pga |= B43_PHY_PGACTL_LOWBANDW;
		b43_phy_write(dev, B43_PHY_PGACTL, pga);
		udelay(10);
		pga |= B43_PHY_PGACTL_LPF;
		b43_phy_write(dev, B43_PHY_PGACTL, pga);
	}
	udelay(21);
	feedthrough = b43_phy_read(dev, B43_PHY_LO_LEAKAGE);

	/* This is a good place to check if we need to relax a bit,
	 * as this is the main function called regularly
	 * in the LO calibration. */
	cond_resched();

	return feedthrough;
}

/* TXCTL Register and Value Table.
 * Returns the "TXCTL Register".
 * "value" is the "TXCTL Value".
 * "pad_mix_gain" is the PAD Mixer Gain.
 */
static u16 lo_txctl_register_table(struct b43_wldev *dev,
				   u16 *value, u16 *pad_mix_gain)
{
	struct b43_phy *phy = &dev->phy;
	u16 reg, v, padmix;

	if (phy->type == B43_PHYTYPE_B) {
		v = 0x30;
		if (phy->radio_rev <= 5) {
			reg = 0x43;
			padmix = 0;
		} else {
			reg = 0x52;
			padmix = 5;
		}
	} else {
		if (phy->rev >= 2 && phy->radio_rev == 8) {
			reg = 0x43;
			v = 0x10;
			padmix = 2;
		} else {
			reg = 0x52;
			v = 0x30;
			padmix = 5;
		}
	}
	if (value)
		*value = v;
	if (pad_mix_gain)
		*pad_mix_gain = padmix;

	return reg;
}

static void lo_measure_txctl_values(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_txpower_lo_control *lo = gphy->lo_control;
	u16 reg, mask;
	u16 trsw_rx, pga;
	u16 radio_pctl_reg;

	static const u8 tx_bias_values[] = {
		0x09, 0x08, 0x0A, 0x01, 0x00,
		0x02, 0x05, 0x04, 0x06,
	};
	static const u8 tx_magn_values[] = {
		0x70, 0x40,
	};

	if (!has_loopback_gain(phy)) {
		radio_pctl_reg = 6;
		trsw_rx = 2;
		pga = 0;
	} else {
		int lb_gain;	/* Loopback gain (in dB) */

		trsw_rx = 0;
		lb_gain = gphy->max_lb_gain / 2;
		if (lb_gain > 10) {
			radio_pctl_reg = 0;
			pga = abs(10 - lb_gain) / 6;
			pga = clamp_val(pga, 0, 15);
		} else {
			int cmp_val;
			int tmp;

			pga = 0;
			cmp_val = 0x24;
			if ((phy->rev >= 2) &&
			    (phy->radio_ver == 0x2050) && (phy->radio_rev == 8))
				cmp_val = 0x3C;
			tmp = lb_gain;
			if ((10 - lb_gain) < cmp_val)
				tmp = (10 - lb_gain);
			if (tmp < 0)
				tmp += 6;
			else
				tmp += 3;
			cmp_val /= 4;
			tmp /= 4;
			if (tmp >= cmp_val)
				radio_pctl_reg = cmp_val;
			else
				radio_pctl_reg = tmp;
		}
	}
	b43_radio_maskset(dev, 0x43, 0xFFF0, radio_pctl_reg);
	b43_gphy_set_baseband_attenuation(dev, 2);

	reg = lo_txctl_register_table(dev, &mask, NULL);
	mask = ~mask;
	b43_radio_mask(dev, reg, mask);

	if (has_tx_magnification(phy)) {
		int i, j;
		int feedthrough;
		int min_feedth = 0xFFFF;
		u8 tx_magn, tx_bias;

		for (i = 0; i < ARRAY_SIZE(tx_magn_values); i++) {
			tx_magn = tx_magn_values[i];
			b43_radio_maskset(dev, 0x52, 0xFF0F, tx_magn);
			for (j = 0; j < ARRAY_SIZE(tx_bias_values); j++) {
				tx_bias = tx_bias_values[j];
				b43_radio_maskset(dev, 0x52, 0xFFF0, tx_bias);
				feedthrough =
				    lo_measure_feedthrough(dev, 0, pga,
							   trsw_rx);
				if (feedthrough < min_feedth) {
					lo->tx_bias = tx_bias;
					lo->tx_magn = tx_magn;
					min_feedth = feedthrough;
				}
				if (lo->tx_bias == 0)
					break;
			}
			b43_radio_write16(dev, 0x52,
					  (b43_radio_read16(dev, 0x52)
					   & 0xFF00) | lo->tx_bias | lo->
					  tx_magn);
		}
	} else {
		lo->tx_magn = 0;
		lo->tx_bias = 0;
		b43_radio_mask(dev, 0x52, 0xFFF0);	/* TX bias == 0 */
	}
	lo->txctl_measured_time = jiffies;
}

static void lo_read_power_vector(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_txpower_lo_control *lo = gphy->lo_control;
	int i;
	u64 tmp;
	u64 power_vector = 0;

	for (i = 0; i < 8; i += 2) {
		tmp = b43_shm_read16(dev, B43_SHM_SHARED, 0x310 + i);
		power_vector |= (tmp << (i * 8));
		/* Clear the vector on the device. */
		b43_shm_write16(dev, B43_SHM_SHARED, 0x310 + i, 0);
	}
	if (power_vector)
		lo->power_vector = power_vector;
	lo->pwr_vec_read_time = jiffies;
}

/* 802.11/LO/GPHY/MeasuringGains */
static void lo_measure_gain_values(struct b43_wldev *dev,
				   s16 max_rx_gain, int use_trsw_rx)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	u16 tmp;

	if (max_rx_gain < 0)
		max_rx_gain = 0;

	if (has_loopback_gain(phy)) {
		int trsw_rx = 0;
		int trsw_rx_gain;

		if (use_trsw_rx) {
			trsw_rx_gain = gphy->trsw_rx_gain / 2;
			if (max_rx_gain >= trsw_rx_gain) {
				trsw_rx_gain = max_rx_gain - trsw_rx_gain;
				trsw_rx = 0x20;
			}
		} else
			trsw_rx_gain = max_rx_gain;
		if (trsw_rx_gain < 9) {
			gphy->lna_lod_gain = 0;
		} else {
			gphy->lna_lod_gain = 1;
			trsw_rx_gain -= 8;
		}
		trsw_rx_gain = clamp_val(trsw_rx_gain, 0, 0x2D);
		gphy->pga_gain = trsw_rx_gain / 3;
		if (gphy->pga_gain >= 5) {
			gphy->pga_gain -= 5;
			gphy->lna_gain = 2;
		} else
			gphy->lna_gain = 0;
	} else {
		gphy->lna_gain = 0;
		gphy->trsw_rx_gain = 0x20;
		if (max_rx_gain >= 0x14) {
			gphy->lna_lod_gain = 1;
			gphy->pga_gain = 2;
		} else if (max_rx_gain >= 0x12) {
			gphy->lna_lod_gain = 1;
			gphy->pga_gain = 1;
		} else if (max_rx_gain >= 0xF) {
			gphy->lna_lod_gain = 1;
			gphy->pga_gain = 0;
		} else {
			gphy->lna_lod_gain = 0;
			gphy->pga_gain = 0;
		}
	}

	tmp = b43_radio_read16(dev, 0x7A);
	if (gphy->lna_lod_gain == 0)
		tmp &= ~0x0008;
	else
		tmp |= 0x0008;
	b43_radio_write16(dev, 0x7A, tmp);
}

struct lo_g_saved_values {
	u8 old_channel;

	/* Core registers */
	u16 reg_3F4;
	u16 reg_3E2;

	/* PHY registers */
	u16 phy_lo_mask;
	u16 phy_extg_01;
	u16 phy_dacctl_hwpctl;
	u16 phy_dacctl;
	u16 phy_cck_14;
	u16 phy_hpwr_tssictl;
	u16 phy_analogover;
	u16 phy_analogoverval;
	u16 phy_rfover;
	u16 phy_rfoverval;
	u16 phy_classctl;
	u16 phy_cck_3E;
	u16 phy_crs0;
	u16 phy_pgactl;
	u16 phy_cck_2A;
	u16 phy_syncctl;
	u16 phy_cck_30;
	u16 phy_cck_06;

	/* Radio registers */
	u16 radio_43;
	u16 radio_7A;
	u16 radio_52;
};

static void lo_measure_setup(struct b43_wldev *dev,
			     struct lo_g_saved_values *sav)
{
	struct ssb_sprom *sprom = &dev->dev->bus->sprom;
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_txpower_lo_control *lo = gphy->lo_control;
	u16 tmp;

	if (b43_has_hardware_pctl(dev)) {
		sav->phy_lo_mask = b43_phy_read(dev, B43_PHY_LO_MASK);
		sav->phy_extg_01 = b43_phy_read(dev, B43_PHY_EXTG(0x01));
		sav->phy_dacctl_hwpctl = b43_phy_read(dev, B43_PHY_DACCTL);
		sav->phy_cck_14 = b43_phy_read(dev, B43_PHY_CCK(0x14));
		sav->phy_hpwr_tssictl = b43_phy_read(dev, B43_PHY_HPWR_TSSICTL);

		b43_phy_set(dev, B43_PHY_HPWR_TSSICTL, 0x100);
		b43_phy_set(dev, B43_PHY_EXTG(0x01), 0x40);
		b43_phy_set(dev, B43_PHY_DACCTL, 0x40);
		b43_phy_set(dev, B43_PHY_CCK(0x14), 0x200);
	}
	if (phy->type == B43_PHYTYPE_B &&
	    phy->radio_ver == 0x2050 && phy->radio_rev < 6) {
		b43_phy_write(dev, B43_PHY_CCK(0x16), 0x410);
		b43_phy_write(dev, B43_PHY_CCK(0x17), 0x820);
	}
	if (phy->rev >= 2) {
		sav->phy_analogover = b43_phy_read(dev, B43_PHY_ANALOGOVER);
		sav->phy_analogoverval =
		    b43_phy_read(dev, B43_PHY_ANALOGOVERVAL);
		sav->phy_rfover = b43_phy_read(dev, B43_PHY_RFOVER);
		sav->phy_rfoverval = b43_phy_read(dev, B43_PHY_RFOVERVAL);
		sav->phy_classctl = b43_phy_read(dev, B43_PHY_CLASSCTL);
		sav->phy_cck_3E = b43_phy_read(dev, B43_PHY_CCK(0x3E));
		sav->phy_crs0 = b43_phy_read(dev, B43_PHY_CRS0);

		b43_phy_mask(dev, B43_PHY_CLASSCTL, 0xFFFC);
		b43_phy_mask(dev, B43_PHY_CRS0, 0x7FFF);
		b43_phy_set(dev, B43_PHY_ANALOGOVER, 0x0003);
		b43_phy_mask(dev, B43_PHY_ANALOGOVERVAL, 0xFFFC);
		if (phy->type == B43_PHYTYPE_G) {
			if ((phy->rev >= 7) &&
			    (sprom->boardflags_lo & B43_BFL_EXTLNA)) {
				b43_phy_write(dev, B43_PHY_RFOVER, 0x933);
			} else {
				b43_phy_write(dev, B43_PHY_RFOVER, 0x133);
			}
		} else {
			b43_phy_write(dev, B43_PHY_RFOVER, 0);
		}
		b43_phy_write(dev, B43_PHY_CCK(0x3E), 0);
	}
	sav->reg_3F4 = b43_read16(dev, 0x3F4);
	sav->reg_3E2 = b43_read16(dev, 0x3E2);
	sav->radio_43 = b43_radio_read16(dev, 0x43);
	sav->radio_7A = b43_radio_read16(dev, 0x7A);
	sav->phy_pgactl = b43_phy_read(dev, B43_PHY_PGACTL);
	sav->phy_cck_2A = b43_phy_read(dev, B43_PHY_CCK(0x2A));
	sav->phy_syncctl = b43_phy_read(dev, B43_PHY_SYNCCTL);
	sav->phy_dacctl = b43_phy_read(dev, B43_PHY_DACCTL);

	if (!has_tx_magnification(phy)) {
		sav->radio_52 = b43_radio_read16(dev, 0x52);
		sav->radio_52 &= 0x00F0;
	}
	if (phy->type == B43_PHYTYPE_B) {
		sav->phy_cck_30 = b43_phy_read(dev, B43_PHY_CCK(0x30));
		sav->phy_cck_06 = b43_phy_read(dev, B43_PHY_CCK(0x06));
		b43_phy_write(dev, B43_PHY_CCK(0x30), 0x00FF);
		b43_phy_write(dev, B43_PHY_CCK(0x06), 0x3F3F);
	} else {
		b43_write16(dev, 0x3E2, b43_read16(dev, 0x3E2)
			    | 0x8000);
	}
	b43_write16(dev, 0x3F4, b43_read16(dev, 0x3F4)
		    & 0xF000);

	tmp =
	    (phy->type == B43_PHYTYPE_G) ? B43_PHY_LO_MASK : B43_PHY_CCK(0x2E);
	b43_phy_write(dev, tmp, 0x007F);

	tmp = sav->phy_syncctl;
	b43_phy_write(dev, B43_PHY_SYNCCTL, tmp & 0xFF7F);
	tmp = sav->radio_7A;
	b43_radio_write16(dev, 0x007A, tmp & 0xFFF0);

	b43_phy_write(dev, B43_PHY_CCK(0x2A), 0x8A3);
	if (phy->type == B43_PHYTYPE_G ||
	    (phy->type == B43_PHYTYPE_B &&
	     phy->radio_ver == 0x2050 && phy->radio_rev >= 6)) {
		b43_phy_write(dev, B43_PHY_CCK(0x2B), 0x1003);
	} else
		b43_phy_write(dev, B43_PHY_CCK(0x2B), 0x0802);
	if (phy->rev >= 2)
		b43_dummy_transmission(dev);
	b43_gphy_channel_switch(dev, 6, 0);
	b43_radio_read16(dev, 0x51);	/* dummy read */
	if (phy->type == B43_PHYTYPE_G)
		b43_phy_write(dev, B43_PHY_CCK(0x2F), 0);

	/* Re-measure the txctl values, if needed. */
	if (time_before(lo->txctl_measured_time,
			jiffies - B43_LO_TXCTL_EXPIRE))
		lo_measure_txctl_values(dev);

	if (phy->type == B43_PHYTYPE_G && phy->rev >= 3) {
		b43_phy_write(dev, B43_PHY_LO_MASK, 0xC078);
	} else {
		if (phy->type == B43_PHYTYPE_B)
			b43_phy_write(dev, B43_PHY_CCK(0x2E), 0x8078);
		else
			b43_phy_write(dev, B43_PHY_LO_MASK, 0x8078);
	}
}

static void lo_measure_restore(struct b43_wldev *dev,
			       struct lo_g_saved_values *sav)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	u16 tmp;

	if (phy->rev >= 2) {
		b43_phy_write(dev, B43_PHY_PGACTL, 0xE300);
		tmp = (gphy->pga_gain << 8);
		b43_phy_write(dev, B43_PHY_RFOVERVAL, tmp | 0xA0);
		udelay(5);
		b43_phy_write(dev, B43_PHY_RFOVERVAL, tmp | 0xA2);
		udelay(2);
		b43_phy_write(dev, B43_PHY_RFOVERVAL, tmp | 0xA3);
	} else {
		tmp = (gphy->pga_gain | 0xEFA0);
		b43_phy_write(dev, B43_PHY_PGACTL, tmp);
	}
	if (phy->type == B43_PHYTYPE_G) {
		if (phy->rev >= 3)
			b43_phy_write(dev, B43_PHY_CCK(0x2E), 0xC078);
		else
			b43_phy_write(dev, B43_PHY_CCK(0x2E), 0x8078);
		if (phy->rev >= 2)
			b43_phy_write(dev, B43_PHY_CCK(0x2F), 0x0202);
		else
			b43_phy_write(dev, B43_PHY_CCK(0x2F), 0x0101);
	}
	b43_write16(dev, 0x3F4, sav->reg_3F4);
	b43_phy_write(dev, B43_PHY_PGACTL, sav->phy_pgactl);
	b43_phy_write(dev, B43_PHY_CCK(0x2A), sav->phy_cck_2A);
	b43_phy_write(dev, B43_PHY_SYNCCTL, sav->phy_syncctl);
	b43_phy_write(dev, B43_PHY_DACCTL, sav->phy_dacctl);
	b43_radio_write16(dev, 0x43, sav->radio_43);
	b43_radio_write16(dev, 0x7A, sav->radio_7A);
	if (!has_tx_magnification(phy)) {
		tmp = sav->radio_52;
		b43_radio_maskset(dev, 0x52, 0xFF0F, tmp);
	}
	b43_write16(dev, 0x3E2, sav->reg_3E2);
	if (phy->type == B43_PHYTYPE_B &&
	    phy->radio_ver == 0x2050 && phy->radio_rev <= 5) {
		b43_phy_write(dev, B43_PHY_CCK(0x30), sav->phy_cck_30);
		b43_phy_write(dev, B43_PHY_CCK(0x06), sav->phy_cck_06);
	}
	if (phy->rev >= 2) {
		b43_phy_write(dev, B43_PHY_ANALOGOVER, sav->phy_analogover);
		b43_phy_write(dev, B43_PHY_ANALOGOVERVAL,
			      sav->phy_analogoverval);
		b43_phy_write(dev, B43_PHY_CLASSCTL, sav->phy_classctl);
		b43_phy_write(dev, B43_PHY_RFOVER, sav->phy_rfover);
		b43_phy_write(dev, B43_PHY_RFOVERVAL, sav->phy_rfoverval);
		b43_phy_write(dev, B43_PHY_CCK(0x3E), sav->phy_cck_3E);
		b43_phy_write(dev, B43_PHY_CRS0, sav->phy_crs0);
	}
	if (b43_has_hardware_pctl(dev)) {
		tmp = (sav->phy_lo_mask & 0xBFFF);
		b43_phy_write(dev, B43_PHY_LO_MASK, tmp);
		b43_phy_write(dev, B43_PHY_EXTG(0x01), sav->phy_extg_01);
		b43_phy_write(dev, B43_PHY_DACCTL, sav->phy_dacctl_hwpctl);
		b43_phy_write(dev, B43_PHY_CCK(0x14), sav->phy_cck_14);
		b43_phy_write(dev, B43_PHY_HPWR_TSSICTL, sav->phy_hpwr_tssictl);
	}
	b43_gphy_channel_switch(dev, sav->old_channel, 1);
}

struct b43_lo_g_statemachine {
	int current_state;
	int nr_measured;
	int state_val_multiplier;
	u16 lowest_feedth;
	struct b43_loctl min_loctl;
};

/* Loop over each possible value in this state. */
static int lo_probe_possible_loctls(struct b43_wldev *dev,
				    struct b43_loctl *probe_loctl,
				    struct b43_lo_g_statemachine *d)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_loctl test_loctl;
	struct b43_loctl orig_loctl;
	struct b43_loctl prev_loctl = {
		.i = -100,
		.q = -100,
	};
	int i;
	int begin, end;
	int found_lower = 0;
	u16 feedth;

	static const struct b43_loctl modifiers[] = {
		{.i = 1,.q = 1,},
		{.i = 1,.q = 0,},
		{.i = 1,.q = -1,},
		{.i = 0,.q = -1,},
		{.i = -1,.q = -1,},
		{.i = -1,.q = 0,},
		{.i = -1,.q = 1,},
		{.i = 0,.q = 1,},
	};

	if (d->current_state == 0) {
		begin = 1;
		end = 8;
	} else if (d->current_state % 2 == 0) {
		begin = d->current_state - 1;
		end = d->current_state + 1;
	} else {
		begin = d->current_state - 2;
		end = d->current_state + 2;
	}
	if (begin < 1)
		begin += 8;
	if (end > 8)
		end -= 8;

	memcpy(&orig_loctl, probe_loctl, sizeof(struct b43_loctl));
	i = begin;
	d->current_state = i;
	while (1) {
		B43_WARN_ON(!(i >= 1 && i <= 8));
		memcpy(&test_loctl, &orig_loctl, sizeof(struct b43_loctl));
		test_loctl.i += modifiers[i - 1].i * d->state_val_multiplier;
		test_loctl.q += modifiers[i - 1].q * d->state_val_multiplier;
		if ((test_loctl.i != prev_loctl.i ||
		     test_loctl.q != prev_loctl.q) &&
		    (abs(test_loctl.i) <= 16 && abs(test_loctl.q) <= 16)) {
			b43_lo_write(dev, &test_loctl);
			feedth = lo_measure_feedthrough(dev, gphy->lna_gain,
							gphy->pga_gain,
							gphy->trsw_rx_gain);
			if (feedth < d->lowest_feedth) {
				memcpy(probe_loctl, &test_loctl,
				       sizeof(struct b43_loctl));
				found_lower = 1;
				d->lowest_feedth = feedth;
				if ((d->nr_measured < 2) &&
				    !has_loopback_gain(phy))
					break;
			}
		}
		memcpy(&prev_loctl, &test_loctl, sizeof(prev_loctl));
		if (i == end)
			break;
		if (i == 8)
			i = 1;
		else
			i++;
		d->current_state = i;
	}

	return found_lower;
}

static void lo_probe_loctls_statemachine(struct b43_wldev *dev,
					 struct b43_loctl *loctl,
					 int *max_rx_gain)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_lo_g_statemachine d;
	u16 feedth;
	int found_lower;
	struct b43_loctl probe_loctl;
	int max_repeat = 1, repeat_cnt = 0;

	d.nr_measured = 0;
	d.state_val_multiplier = 1;
	if (has_loopback_gain(phy))
		d.state_val_multiplier = 3;

	memcpy(&d.min_loctl, loctl, sizeof(struct b43_loctl));
	if (has_loopback_gain(phy))
		max_repeat = 4;
	do {
		b43_lo_write(dev, &d.min_loctl);
		feedth = lo_measure_feedthrough(dev, gphy->lna_gain,
						gphy->pga_gain,
						gphy->trsw_rx_gain);
		if (feedth < 0x258) {
			if (feedth >= 0x12C)
				*max_rx_gain += 6;
			else
				*max_rx_gain += 3;
			feedth = lo_measure_feedthrough(dev, gphy->lna_gain,
							gphy->pga_gain,
							gphy->trsw_rx_gain);
		}
		d.lowest_feedth = feedth;

		d.current_state = 0;
		do {
			B43_WARN_ON(!
				    (d.current_state >= 0
				     && d.current_state <= 8));
			memcpy(&probe_loctl, &d.min_loctl,
			       sizeof(struct b43_loctl));
			found_lower =
			    lo_probe_possible_loctls(dev, &probe_loctl, &d);
			if (!found_lower)
				break;
			if ((probe_loctl.i == d.min_loctl.i) &&
			    (probe_loctl.q == d.min_loctl.q))
				break;
			memcpy(&d.min_loctl, &probe_loctl,
			       sizeof(struct b43_loctl));
			d.nr_measured++;
		} while (d.nr_measured < 24);
		memcpy(loctl, &d.min_loctl, sizeof(struct b43_loctl));

		if (has_loopback_gain(phy)) {
			if (d.lowest_feedth > 0x1194)
				*max_rx_gain -= 6;
			else if (d.lowest_feedth < 0x5DC)
				*max_rx_gain += 3;
			if (repeat_cnt == 0) {
				if (d.lowest_feedth <= 0x5DC) {
					d.state_val_multiplier = 1;
					repeat_cnt++;
				} else
					d.state_val_multiplier = 2;
			} else if (repeat_cnt == 2)
				d.state_val_multiplier = 1;
		}
		lo_measure_gain_values(dev, *max_rx_gain,
				       has_loopback_gain(phy));
	} while (++repeat_cnt < max_repeat);
}

static
struct b43_lo_calib *b43_calibrate_lo_setting(struct b43_wldev *dev,
					      const struct b43_bbatt *bbatt,
					      const struct b43_rfatt *rfatt)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_loctl loctl = {
		.i = 0,
		.q = 0,
	};
	int max_rx_gain;
	struct b43_lo_calib *cal;
	struct lo_g_saved_values uninitialized_var(saved_regs);
	/* Values from the "TXCTL Register and Value Table" */
	u16 txctl_reg;
	u16 txctl_value;
	u16 pad_mix_gain;

	saved_regs.old_channel = phy->channel;
	b43_mac_suspend(dev);
	lo_measure_setup(dev, &saved_regs);

	txctl_reg = lo_txctl_register_table(dev, &txctl_value, &pad_mix_gain);

	b43_radio_maskset(dev, 0x43, 0xFFF0, rfatt->att);
	b43_radio_maskset(dev, txctl_reg, ~txctl_value, (rfatt->with_padmix ? txctl_value :0));

	max_rx_gain = rfatt->att * 2;
	max_rx_gain += bbatt->att / 2;
	if (rfatt->with_padmix)
		max_rx_gain -= pad_mix_gain;
	if (has_loopback_gain(phy))
		max_rx_gain += gphy->max_lb_gain;
	lo_measure_gain_values(dev, max_rx_gain,
			       has_loopback_gain(phy));

	b43_gphy_set_baseband_attenuation(dev, bbatt->att);
	lo_probe_loctls_statemachine(dev, &loctl, &max_rx_gain);

	lo_measure_restore(dev, &saved_regs);
	b43_mac_enable(dev);

	if (b43_debug(dev, B43_DBG_LO)) {
		b43dbg(dev->wl, "LO: Calibrated for BB(%u), RF(%u,%u) "
		       "=> I=%d Q=%d\n",
		       bbatt->att, rfatt->att, rfatt->with_padmix,
		       loctl.i, loctl.q);
	}

	cal = kmalloc(sizeof(*cal), GFP_KERNEL);
	if (!cal) {
		b43warn(dev->wl, "LO calib: out of memory\n");
		return NULL;
	}
	memcpy(&cal->bbatt, bbatt, sizeof(*bbatt));
	memcpy(&cal->rfatt, rfatt, sizeof(*rfatt));
	memcpy(&cal->ctl, &loctl, sizeof(loctl));
	cal->calib_time = jiffies;
	INIT_LIST_HEAD(&cal->list);

	return cal;
}

/* Get a calibrated LO setting for the given attenuation values.
 * Might return a NULL pointer under OOM! */
static
struct b43_lo_calib *b43_get_calib_lo_settings(struct b43_wldev *dev,
					       const struct b43_bbatt *bbatt,
					       const struct b43_rfatt *rfatt)
{
	struct b43_txpower_lo_control *lo = dev->phy.g->lo_control;
	struct b43_lo_calib *c;

	c = b43_find_lo_calib(lo, bbatt, rfatt);
	if (c)
		return c;
	/* Not in the list of calibrated LO settings.
	 * Calibrate it now. */
	c = b43_calibrate_lo_setting(dev, bbatt, rfatt);
	if (!c)
		return NULL;
	list_add(&c->list, &lo->calib_list);

	return c;
}

void b43_gphy_dc_lt_init(struct b43_wldev *dev, bool update_all)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_txpower_lo_control *lo = gphy->lo_control;
	int i;
	int rf_offset, bb_offset;
	const struct b43_rfatt *rfatt;
	const struct b43_bbatt *bbatt;
	u64 power_vector;
	bool table_changed = 0;

	BUILD_BUG_ON(B43_DC_LT_SIZE != 32);
	B43_WARN_ON(lo->rfatt_list.len * lo->bbatt_list.len > 64);

	power_vector = lo->power_vector;
	if (!update_all && !power_vector)
		return; /* Nothing to do. */

	/* Suspend the MAC now to avoid continuous suspend/enable
	 * cycles in the loop. */
	b43_mac_suspend(dev);

	for (i = 0; i < B43_DC_LT_SIZE * 2; i++) {
		struct b43_lo_calib *cal;
		int idx;
		u16 val;

		if (!update_all && !(power_vector & (((u64)1ULL) << i)))
			continue;
		/* Update the table entry for this power_vector bit.
		 * The table rows are RFatt entries and columns are BBatt. */
		bb_offset = i / lo->rfatt_list.len;
		rf_offset = i % lo->rfatt_list.len;
		bbatt = &(lo->bbatt_list.list[bb_offset]);
		rfatt = &(lo->rfatt_list.list[rf_offset]);

		cal = b43_calibrate_lo_setting(dev, bbatt, rfatt);
		if (!cal) {
			b43warn(dev->wl, "LO: Could not "
				"calibrate DC table entry\n");
			continue;
		}
		/*FIXME: Is Q really in the low nibble? */
		val = (u8)(cal->ctl.q);
		val |= ((u8)(cal->ctl.i)) << 4;
		kfree(cal);

		/* Get the index into the hardware DC LT. */
		idx = i / 2;
		/* Change the table in memory. */
		if (i % 2) {
			/* Change the high byte. */
			lo->dc_lt[idx] = (lo->dc_lt[idx] & 0x00FF)
					 | ((val & 0x00FF) << 8);
		} else {
			/* Change the low byte. */
			lo->dc_lt[idx] = (lo->dc_lt[idx] & 0xFF00)
					 | (val & 0x00FF);
		}
		table_changed = 1;
	}
	if (table_changed) {
		/* The table changed in memory. Update the hardware table. */
		for (i = 0; i < B43_DC_LT_SIZE; i++)
			b43_phy_write(dev, 0x3A0 + i, lo->dc_lt[i]);
	}
	b43_mac_enable(dev);
}

/* Fixup the RF attenuation value for the case where we are
 * using the PAD mixer. */
static inline void b43_lo_fixup_rfatt(struct b43_rfatt *rf)
{
	if (!rf->with_padmix)
		return;
	if ((rf->att != 1) && (rf->att != 2) && (rf->att != 3))
		rf->att = 4;
}

void b43_lo_g_adjust(struct b43_wldev *dev)
{
	struct b43_phy_g *gphy = dev->phy.g;
	struct b43_lo_calib *cal;
	struct b43_rfatt rf;

	memcpy(&rf, &gphy->rfatt, sizeof(rf));
	b43_lo_fixup_rfatt(&rf);

	cal = b43_get_calib_lo_settings(dev, &gphy->bbatt, &rf);
	if (!cal)
		return;
	b43_lo_write(dev, &cal->ctl);
}

void b43_lo_g_adjust_to(struct b43_wldev *dev,
			u16 rfatt, u16 bbatt, u16 tx_control)
{
	struct b43_rfatt rf;
	struct b43_bbatt bb;
	struct b43_lo_calib *cal;

	memset(&rf, 0, sizeof(rf));
	memset(&bb, 0, sizeof(bb));
	rf.att = rfatt;
	bb.att = bbatt;
	b43_lo_fixup_rfatt(&rf);
	cal = b43_get_calib_lo_settings(dev, &bb, &rf);
	if (!cal)
		return;
	b43_lo_write(dev, &cal->ctl);
}

/* Periodic LO maintanance work */
void b43_lo_g_maintanance_work(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	struct b43_txpower_lo_control *lo = gphy->lo_control;
	unsigned long now;
	unsigned long expire;
	struct b43_lo_calib *cal, *tmp;
	bool current_item_expired = 0;
	bool hwpctl;

	if (!lo)
		return;
	now = jiffies;
	hwpctl = b43_has_hardware_pctl(dev);

	if (hwpctl) {
		/* Read the power vector and update it, if needed. */
		expire = now - B43_LO_PWRVEC_EXPIRE;
		if (time_before(lo->pwr_vec_read_time, expire)) {
			lo_read_power_vector(dev);
			b43_gphy_dc_lt_init(dev, 0);
		}
		//FIXME Recalc the whole DC table from time to time?
	}

	if (hwpctl)
		return;
	/* Search for expired LO settings. Remove them.
	 * Recalibrate the current setting, if expired. */
	expire = now - B43_LO_CALIB_EXPIRE;
	list_for_each_entry_safe(cal, tmp, &lo->calib_list, list) {
		if (!time_before(cal->calib_time, expire))
			continue;
		/* This item expired. */
		if (b43_compare_bbatt(&cal->bbatt, &gphy->bbatt) &&
		    b43_compare_rfatt(&cal->rfatt, &gphy->rfatt)) {
			B43_WARN_ON(current_item_expired);
			current_item_expired = 1;
		}
		if (b43_debug(dev, B43_DBG_LO)) {
			b43dbg(dev->wl, "LO: Item BB(%u), RF(%u,%u), "
			       "I=%d, Q=%d expired\n",
			       cal->bbatt.att, cal->rfatt.att,
			       cal->rfatt.with_padmix,
			       cal->ctl.i, cal->ctl.q);
		}
		list_del(&cal->list);
		kfree(cal);
	}
	if (current_item_expired || unlikely(list_empty(&lo->calib_list))) {
		/* Recalibrate currently used LO setting. */
		if (b43_debug(dev, B43_DBG_LO))
			b43dbg(dev->wl, "LO: Recalibrating current LO setting\n");
		cal = b43_calibrate_lo_setting(dev, &gphy->bbatt, &gphy->rfatt);
		if (cal) {
			list_add(&cal->list, &lo->calib_list);
			b43_lo_write(dev, &cal->ctl);
		} else
			b43warn(dev->wl, "Failed to recalibrate current LO setting\n");
	}
}

void b43_lo_g_cleanup(struct b43_wldev *dev)
{
	struct b43_txpower_lo_control *lo = dev->phy.g->lo_control;
	struct b43_lo_calib *cal, *tmp;

	if (!lo)
		return;
	list_for_each_entry_safe(cal, tmp, &lo->calib_list, list) {
		list_del(&cal->list);
		kfree(cal);
	}
}

/* LO Initialization */
void b43_lo_g_init(struct b43_wldev *dev)
{
	if (b43_has_hardware_pctl(dev)) {
		lo_read_power_vector(dev);
		b43_gphy_dc_lt_init(dev, 1);
	}
}
