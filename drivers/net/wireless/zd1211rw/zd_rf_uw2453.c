/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "zd_rf.h"
#include "zd_usb.h"
#include "zd_chip.h"

/* This RF programming code is based upon the code found in v2.16.0.0 of the
 * ZyDAS vendor driver. Unlike other RF's, Ubec publish full technical specs
 * for this RF on their website, so we're able to understand more than
 * usual as to what is going on. Thumbs up for Ubec for doing that. */

/* The 3-wire serial interface provides access to 8 write-only registers.
 * The data format is a 4 bit register address followed by a 20 bit value. */
#define UW2453_REGWRITE(reg, val) ((((reg) & 0xf) << 20) | ((val) & 0xfffff))

/* For channel tuning, we have to configure registers 1 (synthesizer), 2 (synth
 * fractional divide ratio) and 3 (VCO config).
 *
 * We configure the RF to produce an interrupt when the PLL is locked onto
 * the configured frequency. During initialization, we run through a variety
 * of different VCO configurations on channel 1 until we detect a PLL lock.
 * When this happens, we remember which VCO configuration produced the lock
 * and use it later. Actually, we use the configuration *after* the one that
 * produced the lock, which seems odd, but it works.
 *
 * If we do not see a PLL lock on any standard VCO config, we fall back on an
 * autocal configuration, which has a fixed (as opposed to per-channel) VCO
 * config and different synth values from the standard set (divide ratio
 * is still shared with the standard set). */

/* The per-channel synth values for all standard VCO configurations. These get
 * written to register 1. */
static const u8 uw2453_std_synth[] = {
	RF_CHANNEL( 1) = 0x47,
	RF_CHANNEL( 2) = 0x47,
	RF_CHANNEL( 3) = 0x67,
	RF_CHANNEL( 4) = 0x67,
	RF_CHANNEL( 5) = 0x67,
	RF_CHANNEL( 6) = 0x67,
	RF_CHANNEL( 7) = 0x57,
	RF_CHANNEL( 8) = 0x57,
	RF_CHANNEL( 9) = 0x57,
	RF_CHANNEL(10) = 0x57,
	RF_CHANNEL(11) = 0x77,
	RF_CHANNEL(12) = 0x77,
	RF_CHANNEL(13) = 0x77,
	RF_CHANNEL(14) = 0x4f,
};

/* This table stores the synthesizer fractional divide ratio for *all* VCO
 * configurations (both standard and autocal). These get written to register 2.
 */
static const u16 uw2453_synth_divide[] = {
	RF_CHANNEL( 1) = 0x999,
	RF_CHANNEL( 2) = 0x99b,
	RF_CHANNEL( 3) = 0x998,
	RF_CHANNEL( 4) = 0x99a,
	RF_CHANNEL( 5) = 0x999,
	RF_CHANNEL( 6) = 0x99b,
	RF_CHANNEL( 7) = 0x998,
	RF_CHANNEL( 8) = 0x99a,
	RF_CHANNEL( 9) = 0x999,
	RF_CHANNEL(10) = 0x99b,
	RF_CHANNEL(11) = 0x998,
	RF_CHANNEL(12) = 0x99a,
	RF_CHANNEL(13) = 0x999,
	RF_CHANNEL(14) = 0xccc,
};

/* Here is the data for all the standard VCO configurations. We shrink our
 * table a little by observing that both channels in a consecutive pair share
 * the same value. We also observe that the high 4 bits ([0:3] in the specs)
 * are all 'Reserved' and are always set to 0x4 - we chop them off in the data
 * below. */
#define CHAN_TO_PAIRIDX(a) ((a - 1) / 2)
#define RF_CHANPAIR(a,b) [CHAN_TO_PAIRIDX(a)]
static const u16 uw2453_std_vco_cfg[][7] = {
	{ /* table 1 */
		RF_CHANPAIR( 1,  2) = 0x664d,
		RF_CHANPAIR( 3,  4) = 0x604d,
		RF_CHANPAIR( 5,  6) = 0x6675,
		RF_CHANPAIR( 7,  8) = 0x6475,
		RF_CHANPAIR( 9, 10) = 0x6655,
		RF_CHANPAIR(11, 12) = 0x6455,
		RF_CHANPAIR(13, 14) = 0x6665,
	},
	{ /* table 2 */
		RF_CHANPAIR( 1,  2) = 0x666d,
		RF_CHANPAIR( 3,  4) = 0x606d,
		RF_CHANPAIR( 5,  6) = 0x664d,
		RF_CHANPAIR( 7,  8) = 0x644d,
		RF_CHANPAIR( 9, 10) = 0x6675,
		RF_CHANPAIR(11, 12) = 0x6475,
		RF_CHANPAIR(13, 14) = 0x6655,
	},
	{ /* table 3 */
		RF_CHANPAIR( 1,  2) = 0x665d,
		RF_CHANPAIR( 3,  4) = 0x605d,
		RF_CHANPAIR( 5,  6) = 0x666d,
		RF_CHANPAIR( 7,  8) = 0x646d,
		RF_CHANPAIR( 9, 10) = 0x664d,
		RF_CHANPAIR(11, 12) = 0x644d,
		RF_CHANPAIR(13, 14) = 0x6675,
	},
	{ /* table 4 */
		RF_CHANPAIR( 1,  2) = 0x667d,
		RF_CHANPAIR( 3,  4) = 0x607d,
		RF_CHANPAIR( 5,  6) = 0x665d,
		RF_CHANPAIR( 7,  8) = 0x645d,
		RF_CHANPAIR( 9, 10) = 0x666d,
		RF_CHANPAIR(11, 12) = 0x646d,
		RF_CHANPAIR(13, 14) = 0x664d,
	},
	{ /* table 5 */
		RF_CHANPAIR( 1,  2) = 0x6643,
		RF_CHANPAIR( 3,  4) = 0x6043,
		RF_CHANPAIR( 5,  6) = 0x667d,
		RF_CHANPAIR( 7,  8) = 0x647d,
		RF_CHANPAIR( 9, 10) = 0x665d,
		RF_CHANPAIR(11, 12) = 0x645d,
		RF_CHANPAIR(13, 14) = 0x666d,
	},
	{ /* table 6 */
		RF_CHANPAIR( 1,  2) = 0x6663,
		RF_CHANPAIR( 3,  4) = 0x6063,
		RF_CHANPAIR( 5,  6) = 0x6643,
		RF_CHANPAIR( 7,  8) = 0x6443,
		RF_CHANPAIR( 9, 10) = 0x667d,
		RF_CHANPAIR(11, 12) = 0x647d,
		RF_CHANPAIR(13, 14) = 0x665d,
	},
	{ /* table 7 */
		RF_CHANPAIR( 1,  2) = 0x6653,
		RF_CHANPAIR( 3,  4) = 0x6053,
		RF_CHANPAIR( 5,  6) = 0x6663,
		RF_CHANPAIR( 7,  8) = 0x6463,
		RF_CHANPAIR( 9, 10) = 0x6643,
		RF_CHANPAIR(11, 12) = 0x6443,
		RF_CHANPAIR(13, 14) = 0x667d,
	},
	{ /* table 8 */
		RF_CHANPAIR( 1,  2) = 0x6673,
		RF_CHANPAIR( 3,  4) = 0x6073,
		RF_CHANPAIR( 5,  6) = 0x6653,
		RF_CHANPAIR( 7,  8) = 0x6453,
		RF_CHANPAIR( 9, 10) = 0x6663,
		RF_CHANPAIR(11, 12) = 0x6463,
		RF_CHANPAIR(13, 14) = 0x6643,
	},
	{ /* table 9 */
		RF_CHANPAIR( 1,  2) = 0x664b,
		RF_CHANPAIR( 3,  4) = 0x604b,
		RF_CHANPAIR( 5,  6) = 0x6673,
		RF_CHANPAIR( 7,  8) = 0x6473,
		RF_CHANPAIR( 9, 10) = 0x6653,
		RF_CHANPAIR(11, 12) = 0x6453,
		RF_CHANPAIR(13, 14) = 0x6663,
	},
	{ /* table 10 */
		RF_CHANPAIR( 1,  2) = 0x666b,
		RF_CHANPAIR( 3,  4) = 0x606b,
		RF_CHANPAIR( 5,  6) = 0x664b,
		RF_CHANPAIR( 7,  8) = 0x644b,
		RF_CHANPAIR( 9, 10) = 0x6673,
		RF_CHANPAIR(11, 12) = 0x6473,
		RF_CHANPAIR(13, 14) = 0x6653,
	},
	{ /* table 11 */
		RF_CHANPAIR( 1,  2) = 0x665b,
		RF_CHANPAIR( 3,  4) = 0x605b,
		RF_CHANPAIR( 5,  6) = 0x666b,
		RF_CHANPAIR( 7,  8) = 0x646b,
		RF_CHANPAIR( 9, 10) = 0x664b,
		RF_CHANPAIR(11, 12) = 0x644b,
		RF_CHANPAIR(13, 14) = 0x6673,
	},

};

/* The per-channel synth values for autocal. These get written to register 1. */
static const u16 uw2453_autocal_synth[] = {
	RF_CHANNEL( 1) = 0x6847,
	RF_CHANNEL( 2) = 0x6847,
	RF_CHANNEL( 3) = 0x6867,
	RF_CHANNEL( 4) = 0x6867,
	RF_CHANNEL( 5) = 0x6867,
	RF_CHANNEL( 6) = 0x6867,
	RF_CHANNEL( 7) = 0x6857,
	RF_CHANNEL( 8) = 0x6857,
	RF_CHANNEL( 9) = 0x6857,
	RF_CHANNEL(10) = 0x6857,
	RF_CHANNEL(11) = 0x6877,
	RF_CHANNEL(12) = 0x6877,
	RF_CHANNEL(13) = 0x6877,
	RF_CHANNEL(14) = 0x684f,
};

/* The VCO configuration for autocal (all channels) */
static const u16 UW2453_AUTOCAL_VCO_CFG = 0x6662;

/* TX gain settings. The array index corresponds to the TX power integration
 * values found in the EEPROM. The values get written to register 7. */
static u32 uw2453_txgain[] = {
	[0x00] = 0x0e313,
	[0x01] = 0x0fb13,
	[0x02] = 0x0e093,
	[0x03] = 0x0f893,
	[0x04] = 0x0ea93,
	[0x05] = 0x1f093,
	[0x06] = 0x1f493,
	[0x07] = 0x1f693,
	[0x08] = 0x1f393,
	[0x09] = 0x1f35b,
	[0x0a] = 0x1e6db,
	[0x0b] = 0x1ff3f,
	[0x0c] = 0x1ffff,
	[0x0d] = 0x361d7,
	[0x0e] = 0x37fbf,
	[0x0f] = 0x3ff8b,
	[0x10] = 0x3ff33,
	[0x11] = 0x3fb3f,
	[0x12] = 0x3ffff,
};

/* RF-specific structure */
struct uw2453_priv {
	/* index into synth/VCO config tables where PLL lock was found
	 * -1 means autocal */
	int config;
};

#define UW2453_PRIV(rf) ((struct uw2453_priv *) (rf)->priv)

static int uw2453_synth_set_channel(struct zd_chip *chip, int channel,
	bool autocal)
{
	int r;
	int idx = channel - 1;
	u32 val;

	if (autocal)
		val = UW2453_REGWRITE(1, uw2453_autocal_synth[idx]);
	else
		val = UW2453_REGWRITE(1, uw2453_std_synth[idx]);

	r = zd_rfwrite_locked(chip, val, RF_RV_BITS);
	if (r)
		return r;

	return zd_rfwrite_locked(chip,
		UW2453_REGWRITE(2, uw2453_synth_divide[idx]), RF_RV_BITS);
}

static int uw2453_write_vco_cfg(struct zd_chip *chip, u16 value)
{
	/* vendor driver always sets these upper bits even though the specs say
	 * they are reserved */
	u32 val = 0x40000 | value;
	return zd_rfwrite_locked(chip, UW2453_REGWRITE(3, val), RF_RV_BITS);
}

static int uw2453_init_mode(struct zd_chip *chip)
{
	static const u32 rv[] = {
		UW2453_REGWRITE(0, 0x25f98), /* enter IDLE mode */
		UW2453_REGWRITE(0, 0x25f9a), /* enter CAL_VCO mode */
		UW2453_REGWRITE(0, 0x25f94), /* enter RX/TX mode */
		UW2453_REGWRITE(0, 0x27fd4), /* power down RSSI circuit */
	};

	return zd_rfwritev_locked(chip, rv, ARRAY_SIZE(rv), RF_RV_BITS);
}

static int uw2453_set_tx_gain_level(struct zd_chip *chip, int channel)
{
	u8 int_value = chip->pwr_int_values[channel - 1];

	if (int_value >= ARRAY_SIZE(uw2453_txgain)) {
		dev_dbg_f(zd_chip_dev(chip), "can't configure TX gain for "
			  "int value %x on channel %d\n", int_value, channel);
		return 0;
	}

	return zd_rfwrite_locked(chip,
		UW2453_REGWRITE(7, uw2453_txgain[int_value]), RF_RV_BITS);
}

static int uw2453_init_hw(struct zd_rf *rf)
{
	int i, r;
	int found_config = -1;
	u16 intr_status;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR10,  0x89 }, { ZD_CR15,  0x20 },
		{ ZD_CR17,  0x28 }, /* 6112 no change */
		{ ZD_CR23,  0x38 }, { ZD_CR24,  0x20 }, { ZD_CR26,  0x93 },
		{ ZD_CR27,  0x15 }, { ZD_CR28,  0x3e }, { ZD_CR29,  0x00 },
		{ ZD_CR33,  0x28 }, { ZD_CR34,  0x30 },
		{ ZD_CR35,  0x43 }, /* 6112 3e->43 */
		{ ZD_CR41,  0x24 }, { ZD_CR44,  0x32 },
		{ ZD_CR46,  0x92 }, /* 6112 96->92 */
		{ ZD_CR47,  0x1e },
		{ ZD_CR48,  0x04 }, /* 5602 Roger */
		{ ZD_CR49,  0xfa }, { ZD_CR79,  0x58 }, { ZD_CR80,  0x30 },
		{ ZD_CR81,  0x30 }, { ZD_CR87,  0x0a }, { ZD_CR89,  0x04 },
		{ ZD_CR91,  0x00 }, { ZD_CR92,  0x0a }, { ZD_CR98,  0x8d },
		{ ZD_CR99,  0x28 }, { ZD_CR100, 0x02 },
		{ ZD_CR101, 0x09 }, /* 6112 13->1f 6220 1f->13 6407 13->9 */
		{ ZD_CR102, 0x27 },
		{ ZD_CR106, 0x1c }, /* 5d07 5112 1f->1c 6220 1c->1f
				     * 6221 1f->1c
				     */
		{ ZD_CR107, 0x1c }, /* 6220 1c->1a 5221 1a->1c */
		{ ZD_CR109, 0x13 },
		{ ZD_CR110, 0x1f }, /* 6112 13->1f 6221 1f->13 6407 13->0x09 */
		{ ZD_CR111, 0x13 }, { ZD_CR112, 0x1f }, { ZD_CR113, 0x27 },
		{ ZD_CR114, 0x23 }, /* 6221 27->23 */
		{ ZD_CR115, 0x24 }, /* 6112 24->1c 6220 1c->24 */
		{ ZD_CR116, 0x24 }, /* 6220 1c->24 */
		{ ZD_CR117, 0xfa }, /* 6112 fa->f8 6220 f8->f4 6220 f4->fa */
		{ ZD_CR118, 0xf0 }, /* 5d07 6112 f0->f2 6220 f2->f0 */
		{ ZD_CR119, 0x1a }, /* 6112 1a->10 6220 10->14 6220 14->1a */
		{ ZD_CR120, 0x4f },
		{ ZD_CR121, 0x1f }, /* 6220 4f->1f */
		{ ZD_CR122, 0xf0 }, { ZD_CR123, 0x57 }, { ZD_CR125, 0xad },
		{ ZD_CR126, 0x6c }, { ZD_CR127, 0x03 },
		{ ZD_CR128, 0x14 }, /* 6302 12->11 */
		{ ZD_CR129, 0x12 }, /* 6301 10->0f */
		{ ZD_CR130, 0x10 }, { ZD_CR137, 0x50 }, { ZD_CR138, 0xa8 },
		{ ZD_CR144, 0xac }, { ZD_CR146, 0x20 }, { ZD_CR252, 0xff },
		{ ZD_CR253, 0xff },
	};

	static const u32 rv[] = {
		UW2453_REGWRITE(4, 0x2b),    /* configure receiver gain */
		UW2453_REGWRITE(5, 0x19e4f), /* configure transmitter gain */
		UW2453_REGWRITE(6, 0xf81ad), /* enable RX/TX filter tuning */
		UW2453_REGWRITE(7, 0x3fffe), /* disable TX gain in test mode */

		/* enter CAL_FIL mode, TX gain set by registers, RX gain set by pins,
		 * RSSI circuit powered down, reduced RSSI range */
		UW2453_REGWRITE(0, 0x25f9c), /* 5d01 cal_fil */

		/* synthesizer configuration for channel 1 */
		UW2453_REGWRITE(1, 0x47),
		UW2453_REGWRITE(2, 0x999),

		/* disable manual VCO band selection */
		UW2453_REGWRITE(3, 0x7602),

		/* enable manual VCO band selection, configure current level */
		UW2453_REGWRITE(3, 0x46063),
	};

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	r = zd_rfwritev_locked(chip, rv, ARRAY_SIZE(rv), RF_RV_BITS);
	if (r)
		return r;

	r = uw2453_init_mode(chip);
	if (r)
		return r;

	/* Try all standard VCO configuration settings on channel 1 */
	for (i = 0; i < ARRAY_SIZE(uw2453_std_vco_cfg) - 1; i++) {
		/* Configure synthesizer for channel 1 */
		r = uw2453_synth_set_channel(chip, 1, false);
		if (r)
			return r;

		/* Write VCO config */
		r = uw2453_write_vco_cfg(chip, uw2453_std_vco_cfg[i][0]);
		if (r)
			return r;

		/* ack interrupt event */
		r = zd_iowrite16_locked(chip, 0x0f, UW2453_INTR_REG);
		if (r)
			return r;

		/* check interrupt status */
		r = zd_ioread16_locked(chip, &intr_status, UW2453_INTR_REG);
		if (r)
			return r;

		if (!(intr_status & 0xf)) {
			dev_dbg_f(zd_chip_dev(chip),
				"PLL locked on configuration %d\n", i);
			found_config = i;
			break;
		}
	}

	if (found_config == -1) {
		/* autocal */
		dev_dbg_f(zd_chip_dev(chip),
			"PLL did not lock, using autocal\n");

		r = uw2453_synth_set_channel(chip, 1, true);
		if (r)
			return r;

		r = uw2453_write_vco_cfg(chip, UW2453_AUTOCAL_VCO_CFG);
		if (r)
			return r;
	}

	/* To match the vendor driver behaviour, we use the configuration after
	 * the one that produced a lock. */
	UW2453_PRIV(rf)->config = found_config + 1;

	return zd_iowrite16_locked(chip, 0x06, ZD_CR203);
}

static int uw2453_set_channel(struct zd_rf *rf, u8 channel)
{
	int r;
	u16 vco_cfg;
	int config = UW2453_PRIV(rf)->config;
	bool autocal = (config == -1);
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR80,  0x30 }, { ZD_CR81,  0x30 }, { ZD_CR79,  0x58 },
		{ ZD_CR12,  0xf0 }, { ZD_CR77,  0x1b }, { ZD_CR78,  0x58 },
	};

	r = uw2453_synth_set_channel(chip, channel, autocal);
	if (r)
		return r;

	if (autocal)
		vco_cfg = UW2453_AUTOCAL_VCO_CFG;
	else
		vco_cfg = uw2453_std_vco_cfg[config][CHAN_TO_PAIRIDX(channel)];

	r = uw2453_write_vco_cfg(chip, vco_cfg);
	if (r)
		return r;

	r = uw2453_init_mode(chip);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	r = uw2453_set_tx_gain_level(chip, channel);
	if (r)
		return r;

	return zd_iowrite16_locked(chip, 0x06, ZD_CR203);
}

static int uw2453_switch_radio_on(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);
	struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x00 }, { ZD_CR251, 0x3f },
	};

	/* enter RXTX mode */
	r = zd_rfwrite_locked(chip, UW2453_REGWRITE(0, 0x25f94), RF_RV_BITS);
	if (r)
		return r;

	if (zd_chip_is_zd1211b(chip))
		ioreqs[1].value = 0x7f;

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int uw2453_switch_radio_off(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x04 }, { ZD_CR251, 0x2f },
	};

	/* enter IDLE mode */
	/* FIXME: shouldn't we go to SLEEP? sent email to zydas */
	r = zd_rfwrite_locked(chip, UW2453_REGWRITE(0, 0x25f90), RF_RV_BITS);
	if (r)
		return r;

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static void uw2453_clear(struct zd_rf *rf)
{
	kfree(rf->priv);
}

int zd_rf_init_uw2453(struct zd_rf *rf)
{
	rf->init_hw = uw2453_init_hw;
	rf->set_channel = uw2453_set_channel;
	rf->switch_radio_on = uw2453_switch_radio_on;
	rf->switch_radio_off = uw2453_switch_radio_off;
	rf->patch_6m_band_edge = zd_rf_generic_patch_6m;
	rf->clear = uw2453_clear;
	/* we have our own TX integration code */
	rf->update_channel_int = 0;

	rf->priv = kmalloc(sizeof(struct uw2453_priv), GFP_KERNEL);
	if (rf->priv == NULL)
		return -ENOMEM;

	return 0;
}

