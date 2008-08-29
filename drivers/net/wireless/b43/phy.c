/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005-2007 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005, 2006 Michael Buesch <mb@bu3sch.de>
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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/bitrev.h>

#include "b43.h"
#include "phy.h"
#include "nphy.h"
#include "main.h"
#include "tables.h"
#include "lo.h"
#include "wa.h"


static void b43_shm_clear_tssi(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;

	switch (phy->type) {
	case B43_PHYTYPE_A:
		b43_shm_write16(dev, B43_SHM_SHARED, 0x0068, 0x7F7F);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x006a, 0x7F7F);
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
		b43_shm_write16(dev, B43_SHM_SHARED, 0x0058, 0x7F7F);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x005a, 0x7F7F);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x0070, 0x7F7F);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x0072, 0x7F7F);
		break;
	}
}

/* http://bcm-specs.sipsolutions.net/EstimatePowerOut
 * This function converts a TSSI value to dBm in Q5.2
 */
static s8 b43_phy_estimate_power_out(struct b43_wldev *dev, s8 tssi)
{
	struct b43_phy *phy = &dev->phy;
	s8 dbm = 0;
	s32 tmp;

	tmp = (phy->tgt_idle_tssi - phy->cur_idle_tssi + tssi);

	switch (phy->type) {
	case B43_PHYTYPE_A:
		tmp += 0x80;
		tmp = clamp_val(tmp, 0x00, 0xFF);
		dbm = phy->tssi2dbm[tmp];
		//TODO: There's a FIXME on the specs
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
		tmp = clamp_val(tmp, 0x00, 0x3F);
		dbm = phy->tssi2dbm[tmp];
		break;
	default:
		B43_WARN_ON(1);
	}

	return dbm;
}

void b43_put_attenuation_into_ranges(struct b43_wldev *dev,
				     int *_bbatt, int *_rfatt)
{
	int rfatt = *_rfatt;
	int bbatt = *_bbatt;
	struct b43_txpower_lo_control *lo = dev->phy.lo_control;

	/* Get baseband and radio attenuation values into their permitted ranges.
	 * Radio attenuation affects power level 4 times as much as baseband. */

	/* Range constants */
	const int rf_min = lo->rfatt_list.min_val;
	const int rf_max = lo->rfatt_list.max_val;
	const int bb_min = lo->bbatt_list.min_val;
	const int bb_max = lo->bbatt_list.max_val;

	while (1) {
		if (rfatt > rf_max && bbatt > bb_max - 4)
			break;	/* Can not get it into ranges */
		if (rfatt < rf_min && bbatt < bb_min + 4)
			break;	/* Can not get it into ranges */
		if (bbatt > bb_max && rfatt > rf_max - 1)
			break;	/* Can not get it into ranges */
		if (bbatt < bb_min && rfatt < rf_min + 1)
			break;	/* Can not get it into ranges */

		if (bbatt > bb_max) {
			bbatt -= 4;
			rfatt += 1;
			continue;
		}
		if (bbatt < bb_min) {
			bbatt += 4;
			rfatt -= 1;
			continue;
		}
		if (rfatt > rf_max) {
			rfatt -= 1;
			bbatt += 4;
			continue;
		}
		if (rfatt < rf_min) {
			rfatt += 1;
			bbatt -= 4;
			continue;
		}
		break;
	}

	*_rfatt = clamp_val(rfatt, rf_min, rf_max);
	*_bbatt = clamp_val(bbatt, bb_min, bb_max);
}

/* http://bcm-specs.sipsolutions.net/RecalculateTransmissionPower */
void b43_phy_xmitpower(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct b43_phy *phy = &dev->phy;

	if (phy->cur_idle_tssi == 0)
		return;
	if ((bus->boardinfo.vendor == SSB_BOARDVENDOR_BCM) &&
	    (bus->boardinfo.type == SSB_BOARD_BU4306))
		return;
#ifdef CONFIG_B43_DEBUG
	if (phy->manual_txpower_control)
		return;
#endif

	switch (phy->type) {
	case B43_PHYTYPE_A:{

			//TODO: Nothing for A PHYs yet :-/

			break;
		}
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:{
			u16 tmp;
			s8 v0, v1, v2, v3;
			s8 average;
			int max_pwr;
			int desired_pwr, estimated_pwr, pwr_adjust;
			int rfatt_delta, bbatt_delta;
			int rfatt, bbatt;
			u8 tx_control;

			tmp = b43_shm_read16(dev, B43_SHM_SHARED, 0x0058);
			v0 = (s8) (tmp & 0x00FF);
			v1 = (s8) ((tmp & 0xFF00) >> 8);
			tmp = b43_shm_read16(dev, B43_SHM_SHARED, 0x005A);
			v2 = (s8) (tmp & 0x00FF);
			v3 = (s8) ((tmp & 0xFF00) >> 8);
			tmp = 0;

			if (v0 == 0x7F || v1 == 0x7F || v2 == 0x7F
			    || v3 == 0x7F) {
				tmp =
				    b43_shm_read16(dev, B43_SHM_SHARED, 0x0070);
				v0 = (s8) (tmp & 0x00FF);
				v1 = (s8) ((tmp & 0xFF00) >> 8);
				tmp =
				    b43_shm_read16(dev, B43_SHM_SHARED, 0x0072);
				v2 = (s8) (tmp & 0x00FF);
				v3 = (s8) ((tmp & 0xFF00) >> 8);
				if (v0 == 0x7F || v1 == 0x7F || v2 == 0x7F
				    || v3 == 0x7F)
					return;
				v0 = (v0 + 0x20) & 0x3F;
				v1 = (v1 + 0x20) & 0x3F;
				v2 = (v2 + 0x20) & 0x3F;
				v3 = (v3 + 0x20) & 0x3F;
				tmp = 1;
			}
			b43_shm_clear_tssi(dev);

			average = (v0 + v1 + v2 + v3 + 2) / 4;

			if (tmp
			    && (b43_shm_read16(dev, B43_SHM_SHARED, 0x005E) &
				0x8))
				average -= 13;

			estimated_pwr =
			    b43_phy_estimate_power_out(dev, average);

			max_pwr = dev->dev->bus->sprom.maxpwr_bg;
			if ((dev->dev->bus->sprom.boardflags_lo
			    & B43_BFL_PACTRL) && (phy->type == B43_PHYTYPE_G))
				max_pwr -= 0x3;
			if (unlikely(max_pwr <= 0)) {
				b43warn(dev->wl,
					"Invalid max-TX-power value in SPROM.\n");
				max_pwr = 60;	/* fake it */
				dev->dev->bus->sprom.maxpwr_bg = max_pwr;
			}

			/*TODO:
			   max_pwr = min(REG - dev->dev->bus->sprom.antennagain_bgphy - 0x6, max_pwr)
			   where REG is the max power as per the regulatory domain
			 */

			/* Get desired power (in Q5.2) */
			desired_pwr = INT_TO_Q52(phy->power_level);
			/* And limit it. max_pwr already is Q5.2 */
			desired_pwr = clamp_val(desired_pwr, 0, max_pwr);
			if (b43_debug(dev, B43_DBG_XMITPOWER)) {
				b43dbg(dev->wl,
				       "Current TX power output: " Q52_FMT
				       " dBm, " "Desired TX power output: "
				       Q52_FMT " dBm\n", Q52_ARG(estimated_pwr),
				       Q52_ARG(desired_pwr));
			}

			/* Calculate the adjustment delta. */
			pwr_adjust = desired_pwr - estimated_pwr;

			/* RF attenuation delta. */
			rfatt_delta = ((pwr_adjust + 7) / 8);
			/* Lower attenuation => Bigger power output. Negate it. */
			rfatt_delta = -rfatt_delta;

			/* Baseband attenuation delta. */
			bbatt_delta = pwr_adjust / 2;
			/* Lower attenuation => Bigger power output. Negate it. */
			bbatt_delta = -bbatt_delta;
			/* RF att affects power level 4 times as much as
			 * Baseband attennuation. Subtract it. */
			bbatt_delta -= 4 * rfatt_delta;

			/* So do we finally need to adjust something? */
			if ((rfatt_delta == 0) && (bbatt_delta == 0))
				return;

			/* Calculate the new attenuation values. */
			bbatt = phy->bbatt.att;
			bbatt += bbatt_delta;
			rfatt = phy->rfatt.att;
			rfatt += rfatt_delta;

			b43_put_attenuation_into_ranges(dev, &bbatt, &rfatt);
			tx_control = phy->tx_control;
			if ((phy->radio_ver == 0x2050) && (phy->radio_rev == 2)) {
				if (rfatt <= 1) {
					if (tx_control == 0) {
						tx_control =
						    B43_TXCTL_PA2DB |
						    B43_TXCTL_TXMIX;
						rfatt += 2;
						bbatt += 2;
					} else if (dev->dev->bus->sprom.
						   boardflags_lo &
						   B43_BFL_PACTRL) {
						bbatt += 4 * (rfatt - 2);
						rfatt = 2;
					}
				} else if (rfatt > 4 && tx_control) {
					tx_control = 0;
					if (bbatt < 3) {
						rfatt -= 3;
						bbatt += 2;
					} else {
						rfatt -= 2;
						bbatt -= 2;
					}
				}
			}
			/* Save the control values */
			phy->tx_control = tx_control;
			b43_put_attenuation_into_ranges(dev, &bbatt, &rfatt);
			phy->rfatt.att = rfatt;
			phy->bbatt.att = bbatt;

			/* Adjust the hardware */
			b43_phy_lock(dev);
			b43_radio_lock(dev);
			b43_set_txpower_g(dev, &phy->bbatt, &phy->rfatt,
					  phy->tx_control);
			b43_radio_unlock(dev);
			b43_phy_unlock(dev);
			break;
		}
	case B43_PHYTYPE_N:
		b43_nphy_xmitpower(dev);
		break;
	default:
		B43_WARN_ON(1);
	}
}

static inline s32 b43_tssi2dbm_ad(s32 num, s32 den)
{
	if (num < 0)
		return num / den;
	else
		return (num + den / 2) / den;
}

static inline
    s8 b43_tssi2dbm_entry(s8 entry[], u8 index, s16 pab0, s16 pab1, s16 pab2)
{
	s32 m1, m2, f = 256, q, delta;
	s8 i = 0;

	m1 = b43_tssi2dbm_ad(16 * pab0 + index * pab1, 32);
	m2 = max(b43_tssi2dbm_ad(32768 + index * pab2, 256), 1);
	do {
		if (i > 15)
			return -EINVAL;
		q = b43_tssi2dbm_ad(f * 4096 -
				    b43_tssi2dbm_ad(m2 * f, 16) * f, 2048);
		delta = abs(q - f);
		f = q;
		i++;
	} while (delta >= 2);
	entry[index] = clamp_val(b43_tssi2dbm_ad(m1 * f, 8192), -127, 128);
	return 0;
}

/* http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table */
int b43_phy_init_tssi2dbm_table(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	s16 pab0, pab1, pab2;
	u8 idx;
	s8 *dyn_tssi2dbm;

	if (phy->type == B43_PHYTYPE_A) {
		pab0 = (s16) (dev->dev->bus->sprom.pa1b0);
		pab1 = (s16) (dev->dev->bus->sprom.pa1b1);
		pab2 = (s16) (dev->dev->bus->sprom.pa1b2);
	} else {
		pab0 = (s16) (dev->dev->bus->sprom.pa0b0);
		pab1 = (s16) (dev->dev->bus->sprom.pa0b1);
		pab2 = (s16) (dev->dev->bus->sprom.pa0b2);
	}

	if ((dev->dev->bus->chip_id == 0x4301) && (phy->radio_ver != 0x2050)) {
		phy->tgt_idle_tssi = 0x34;
		phy->tssi2dbm = b43_tssi2dbm_b_table;
		return 0;
	}

	if (pab0 != 0 && pab1 != 0 && pab2 != 0 &&
	    pab0 != -1 && pab1 != -1 && pab2 != -1) {
		/* The pabX values are set in SPROM. Use them. */
		if (phy->type == B43_PHYTYPE_A) {
			if ((s8) dev->dev->bus->sprom.itssi_a != 0 &&
			    (s8) dev->dev->bus->sprom.itssi_a != -1)
				phy->tgt_idle_tssi =
				    (s8) (dev->dev->bus->sprom.itssi_a);
			else
				phy->tgt_idle_tssi = 62;
		} else {
			if ((s8) dev->dev->bus->sprom.itssi_bg != 0 &&
			    (s8) dev->dev->bus->sprom.itssi_bg != -1)
				phy->tgt_idle_tssi =
				    (s8) (dev->dev->bus->sprom.itssi_bg);
			else
				phy->tgt_idle_tssi = 62;
		}
		dyn_tssi2dbm = kmalloc(64, GFP_KERNEL);
		if (dyn_tssi2dbm == NULL) {
			b43err(dev->wl, "Could not allocate memory "
			       "for tssi2dbm table\n");
			return -ENOMEM;
		}
		for (idx = 0; idx < 64; idx++)
			if (b43_tssi2dbm_entry
			    (dyn_tssi2dbm, idx, pab0, pab1, pab2)) {
				phy->tssi2dbm = NULL;
				b43err(dev->wl, "Could not generate "
				       "tssi2dBm table\n");
				kfree(dyn_tssi2dbm);
				return -ENODEV;
			}
		phy->tssi2dbm = dyn_tssi2dbm;
		phy->dyn_tssi_tbl = 1;
	} else {
		/* pabX values not set in SPROM. */
		switch (phy->type) {
		case B43_PHYTYPE_A:
			/* APHY needs a generated table. */
			phy->tssi2dbm = NULL;
			b43err(dev->wl, "Could not generate tssi2dBm "
			       "table (wrong SPROM info)!\n");
			return -ENODEV;
		case B43_PHYTYPE_B:
			phy->tgt_idle_tssi = 0x34;
			phy->tssi2dbm = b43_tssi2dbm_b_table;
			break;
		case B43_PHYTYPE_G:
			phy->tgt_idle_tssi = 0x34;
			phy->tssi2dbm = b43_tssi2dbm_g_table;
			break;
		}
	}

	return 0;
}

void b43_radio_turn_on(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	int err;
	u8 channel;

	might_sleep();

	if (phy->radio_on)
		return;

	switch (phy->type) {
	case B43_PHYTYPE_A:
		b43_radio_write16(dev, 0x0004, 0x00C0);
		b43_radio_write16(dev, 0x0005, 0x0008);
		b43_phy_write(dev, 0x0010, b43_phy_read(dev, 0x0010) & 0xFFF7);
		b43_phy_write(dev, 0x0011, b43_phy_read(dev, 0x0011) & 0xFFF7);
		b43_radio_init2060(dev);
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
		//XXX
		break;
	case B43_PHYTYPE_N:
		b43_nphy_radio_turn_on(dev);
		break;
	default:
		B43_WARN_ON(1);
	}
	phy->radio_on = 1;
}

void b43_radio_turn_off(struct b43_wldev *dev, bool force)
{
	struct b43_phy *phy = &dev->phy;

	if (!phy->radio_on && !force)
		return;

	switch (phy->type) {
	case B43_PHYTYPE_N:
		b43_nphy_radio_turn_off(dev);
		break;
	case B43_PHYTYPE_A:
		b43_radio_write16(dev, 0x0004, 0x00FF);
		b43_radio_write16(dev, 0x0005, 0x00FB);
		b43_phy_write(dev, 0x0010, b43_phy_read(dev, 0x0010) | 0x0008);
		b43_phy_write(dev, 0x0011, b43_phy_read(dev, 0x0011) | 0x0008);
		break;
	case B43_PHYTYPE_G: {
		//XXX
		break;
	}
	default:
		B43_WARN_ON(1);
	}
	phy->radio_on = 0;
}
