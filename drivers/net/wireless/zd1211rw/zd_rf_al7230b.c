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

#include "zd_rf.h"
#include "zd_usb.h"
#include "zd_chip.h"

static const u32 chan_rv[][2] = {
	RF_CHANNEL( 1) = { 0x09ec00, 0x8cccc8 },
	RF_CHANNEL( 2) = { 0x09ec00, 0x8cccd8 },
	RF_CHANNEL( 3) = { 0x09ec00, 0x8cccc0 },
	RF_CHANNEL( 4) = { 0x09ec00, 0x8cccd0 },
	RF_CHANNEL( 5) = { 0x05ec00, 0x8cccc8 },
	RF_CHANNEL( 6) = { 0x05ec00, 0x8cccd8 },
	RF_CHANNEL( 7) = { 0x05ec00, 0x8cccc0 },
	RF_CHANNEL( 8) = { 0x05ec00, 0x8cccd0 },
	RF_CHANNEL( 9) = { 0x0dec00, 0x8cccc8 },
	RF_CHANNEL(10) = { 0x0dec00, 0x8cccd8 },
	RF_CHANNEL(11) = { 0x0dec00, 0x8cccc0 },
	RF_CHANNEL(12) = { 0x0dec00, 0x8cccd0 },
	RF_CHANNEL(13) = { 0x03ec00, 0x8cccc8 },
	RF_CHANNEL(14) = { 0x03ec00, 0x866660 },
};

static const u32 std_rv[] = {
	0x4ff821,
	0xc5fbfc,
	0x21ebfe,
	0xafd401, /* freq shift 0xaad401 */
	0x6cf56a,
	0xe04073,
	0x193d76,
	0x9dd844,
	0x500007,
	0xd8c010,
};

static const u32 rv_init1[] = {
	0x3c9000,
	0xbfffff,
	0x700000,
	0xf15d58,
};

static const u32 rv_init2[] = {
	0xf15d59,
	0xf15d5c,
	0xf15d58,
};

static const struct zd_ioreq16 ioreqs_sw[] = {
	{ ZD_CR128, 0x14 }, { ZD_CR129, 0x12 }, { ZD_CR130, 0x10 },
	{ ZD_CR38,  0x38 }, { ZD_CR136, 0xdf },
};

static int zd1211b_al7230b_finalize(struct zd_chip *chip)
{
	int r;
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR80,  0x30 }, { ZD_CR81,  0x30 }, { ZD_CR79,  0x58 },
		{ ZD_CR12,  0xf0 }, { ZD_CR77,  0x1b }, { ZD_CR78,  0x58 },
		{ ZD_CR203, 0x04 },
		{ },
		{ ZD_CR240, 0x80 },
	};

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	if (chip->new_phy_layout) {
		/* antenna selection? */
		r = zd_iowrite16_locked(chip, 0xe5, ZD_CR9);
		if (r)
			return r;
	}

	return zd_iowrite16_locked(chip, 0x04, ZD_CR203);
}

static int zd1211_al7230b_init_hw(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	/* All of these writes are identical to AL2230 unless otherwise
	 * specified */
	static const struct zd_ioreq16 ioreqs_1[] = {
		/* This one is 7230-specific, and happens before the rest */
		{ ZD_CR240,  0x57 },
		{ },

		{ ZD_CR15,   0x20 }, { ZD_CR23,   0x40 }, { ZD_CR24,  0x20 },
		{ ZD_CR26,   0x11 }, { ZD_CR28,   0x3e }, { ZD_CR29,  0x00 },
		{ ZD_CR44,   0x33 },
		/* This value is different for 7230 (was: 0x2a) */
		{ ZD_CR106,  0x22 },
		{ ZD_CR107,  0x1a }, { ZD_CR109,  0x09 }, { ZD_CR110,  0x27 },
		{ ZD_CR111,  0x2b }, { ZD_CR112,  0x2b }, { ZD_CR119,  0x0a },
		/* This happened further down in AL2230,
		 * and the value changed (was: 0xe0) */
		{ ZD_CR122,  0xfc },
		{ ZD_CR10,   0x89 },
		/* for newest (3rd cut) AL2300 */
		{ ZD_CR17,   0x28 },
		{ ZD_CR26,   0x93 }, { ZD_CR34,   0x30 },
		/* for newest (3rd cut) AL2300 */
		{ ZD_CR35,   0x3e },
		{ ZD_CR41,   0x24 }, { ZD_CR44,   0x32 },
		/* for newest (3rd cut) AL2300 */
		{ ZD_CR46,   0x96 },
		{ ZD_CR47,   0x1e }, { ZD_CR79,   0x58 }, { ZD_CR80,  0x30 },
		{ ZD_CR81,   0x30 }, { ZD_CR87,   0x0a }, { ZD_CR89,  0x04 },
		{ ZD_CR92,   0x0a }, { ZD_CR99,   0x28 },
		/* This value is different for 7230 (was: 0x00) */
		{ ZD_CR100,  0x02 },
		{ ZD_CR101,  0x13 }, { ZD_CR102,  0x27 },
		/* This value is different for 7230 (was: 0x24) */
		{ ZD_CR106,  0x22 },
		/* This value is different for 7230 (was: 0x2a) */
		{ ZD_CR107,  0x3f },
		{ ZD_CR109,  0x09 },
		/* This value is different for 7230 (was: 0x13) */
		{ ZD_CR110,  0x1f },
		{ ZD_CR111,  0x1f }, { ZD_CR112,  0x1f }, { ZD_CR113, 0x27 },
		{ ZD_CR114,  0x27 },
		/* for newest (3rd cut) AL2300 */
		{ ZD_CR115,  0x24 },
		/* This value is different for 7230 (was: 0x24) */
		{ ZD_CR116,  0x3f },
		/* This value is different for 7230 (was: 0xf4) */
		{ ZD_CR117,  0xfa },
		{ ZD_CR118,  0xfc }, { ZD_CR119,  0x10 }, { ZD_CR120, 0x4f },
		{ ZD_CR121,  0x77 }, { ZD_CR137,  0x88 },
		/* This one is 7230-specific */
		{ ZD_CR138,  0xa8 },
		/* This value is different for 7230 (was: 0xff) */
		{ ZD_CR252,  0x34 },
		/* This value is different for 7230 (was: 0xff) */
		{ ZD_CR253,  0x34 },

		/* PLL_OFF */
		{ ZD_CR251, 0x2f },
	};

	static const struct zd_ioreq16 ioreqs_2[] = {
		{ ZD_CR251, 0x3f }, /* PLL_ON */
		{ ZD_CR128, 0x14 }, { ZD_CR129, 0x12 }, { ZD_CR130, 0x10 },
		{ ZD_CR38,  0x38 }, { ZD_CR136, 0xdf },
	};

	r = zd_iowrite16a_locked(chip, ioreqs_1, ARRAY_SIZE(ioreqs_1));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, chan_rv[0], ARRAY_SIZE(chan_rv[0]));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, std_rv, ARRAY_SIZE(std_rv));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv_init1, ARRAY_SIZE(rv_init1));
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_2, ARRAY_SIZE(ioreqs_2));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv_init2, ARRAY_SIZE(rv_init2));
	if (r)
		return r;

	r = zd_iowrite16_locked(chip, 0x06, ZD_CR203);
	if (r)
		return r;
	r = zd_iowrite16_locked(chip, 0x80, ZD_CR240);
	if (r)
		return r;

	return 0;
}

static int zd1211b_al7230b_init_hw(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs_1[] = {
		{ ZD_CR240, 0x57 }, { ZD_CR9,   0x9 },
		{ },
		{ ZD_CR10,  0x8b }, { ZD_CR15,  0x20 },
		{ ZD_CR17,  0x2B }, /* for newest (3rd cut) AL2230 */
		{ ZD_CR20,  0x10 }, /* 4N25->Stone Request */
		{ ZD_CR23,  0x40 }, { ZD_CR24,  0x20 }, { ZD_CR26,  0x93 },
		{ ZD_CR28,  0x3e }, { ZD_CR29,  0x00 },
		{ ZD_CR33,  0x28 }, /* 5613 */
		{ ZD_CR34,  0x30 },
		{ ZD_CR35,  0x3e }, /* for newest (3rd cut) AL2230 */
		{ ZD_CR41,  0x24 }, { ZD_CR44,  0x32 },
		{ ZD_CR46,  0x99 }, /* for newest (3rd cut) AL2230 */
		{ ZD_CR47,  0x1e },

		/* ZD1215 5610 */
		{ ZD_CR48,  0x00 }, { ZD_CR49,  0x00 }, { ZD_CR51,  0x01 },
		{ ZD_CR52,  0x80 }, { ZD_CR53,  0x7e }, { ZD_CR65,  0x00 },
		{ ZD_CR66,  0x00 }, { ZD_CR67,  0x00 }, { ZD_CR68,  0x00 },
		{ ZD_CR69,  0x28 },

		{ ZD_CR79,  0x58 }, { ZD_CR80,  0x30 }, { ZD_CR81,  0x30 },
		{ ZD_CR87,  0x0A }, { ZD_CR89,  0x04 },
		{ ZD_CR90,  0x58 }, /* 5112 */
		{ ZD_CR91,  0x00 }, /* 5613 */
		{ ZD_CR92,  0x0a },
		{ ZD_CR98,  0x8d }, /* 4804, for 1212 new algorithm */
		{ ZD_CR99,  0x00 }, { ZD_CR100, 0x02 }, { ZD_CR101, 0x13 },
		{ ZD_CR102, 0x27 },
		{ ZD_CR106, 0x20 }, /* change to 0x24 for AL7230B */
		{ ZD_CR109, 0x13 }, /* 4804, for 1212 new algorithm */
		{ ZD_CR112, 0x1f },
	};

	static const struct zd_ioreq16 ioreqs_new_phy[] = {
		{ ZD_CR107, 0x28 },
		{ ZD_CR110, 0x1f }, /* 5127, 0x13->0x1f */
		{ ZD_CR111, 0x1f }, /* 0x13 to 0x1f for AL7230B */
		{ ZD_CR116, 0x2a }, { ZD_CR118, 0xfa }, { ZD_CR119, 0x12 },
		{ ZD_CR121, 0x6c }, /* 5613 */
	};

	static const struct zd_ioreq16 ioreqs_old_phy[] = {
		{ ZD_CR107, 0x24 },
		{ ZD_CR110, 0x13 }, /* 5127, 0x13->0x1f */
		{ ZD_CR111, 0x13 }, /* 0x13 to 0x1f for AL7230B */
		{ ZD_CR116, 0x24 }, { ZD_CR118, 0xfc }, { ZD_CR119, 0x11 },
		{ ZD_CR121, 0x6a }, /* 5613 */
	};

	static const struct zd_ioreq16 ioreqs_2[] = {
		{ ZD_CR113, 0x27 }, { ZD_CR114, 0x27 }, { ZD_CR115, 0x24 },
		{ ZD_CR117, 0xfa }, { ZD_CR120, 0x4f },
		{ ZD_CR122, 0xfc }, /* E0->FCh at 4901 */
		{ ZD_CR123, 0x57 }, /* 5613 */
		{ ZD_CR125, 0xad }, /* 4804, for 1212 new algorithm */
		{ ZD_CR126, 0x6c }, /* 5613 */
		{ ZD_CR127, 0x03 }, /* 4804, for 1212 new algorithm */
		{ ZD_CR130, 0x10 },
		{ ZD_CR131, 0x00 }, /* 5112 */
		{ ZD_CR137, 0x50 }, /* 5613 */
		{ ZD_CR138, 0xa8 }, /* 5112 */
		{ ZD_CR144, 0xac }, /* 5613 */
		{ ZD_CR148, 0x40 }, /* 5112 */
		{ ZD_CR149, 0x40 }, /* 4O07, 50->40 */
		{ ZD_CR150, 0x1a }, /* 5112, 0C->1A */
		{ ZD_CR252, 0x34 }, { ZD_CR253, 0x34 },
		{ ZD_CR251, 0x2f }, /* PLL_OFF */
	};

	static const struct zd_ioreq16 ioreqs_3[] = {
		{ ZD_CR251, 0x7f }, /* PLL_ON */
		{ ZD_CR128, 0x14 }, { ZD_CR129, 0x12 }, { ZD_CR130, 0x10 },
		{ ZD_CR38,  0x38 }, { ZD_CR136, 0xdf },
	};

	r = zd_iowrite16a_locked(chip, ioreqs_1, ARRAY_SIZE(ioreqs_1));
	if (r)
		return r;

	if (chip->new_phy_layout)
		r = zd_iowrite16a_locked(chip, ioreqs_new_phy,
			ARRAY_SIZE(ioreqs_new_phy));
	else
		r = zd_iowrite16a_locked(chip, ioreqs_old_phy,
			ARRAY_SIZE(ioreqs_old_phy));
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_2, ARRAY_SIZE(ioreqs_2));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, chan_rv[0], ARRAY_SIZE(chan_rv[0]));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, std_rv, ARRAY_SIZE(std_rv));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv_init1, ARRAY_SIZE(rv_init1));
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_3, ARRAY_SIZE(ioreqs_3));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv_init2, ARRAY_SIZE(rv_init2));
	if (r)
		return r;

	return zd1211b_al7230b_finalize(chip);
}

static int zd1211_al7230b_set_channel(struct zd_rf *rf, u8 channel)
{
	int r;
	const u32 *rv = chan_rv[channel-1];
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs[] = {
		/* PLL_ON */
		{ ZD_CR251, 0x3f },
		{ ZD_CR203, 0x06 }, { ZD_CR240, 0x08 },
	};

	r = zd_iowrite16_locked(chip, 0x57, ZD_CR240);
	if (r)
		return r;

	/* PLL_OFF */
	r = zd_iowrite16_locked(chip, 0x2f, ZD_CR251);
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, std_rv, ARRAY_SIZE(std_rv));
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d58);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_sw, ARRAY_SIZE(ioreqs_sw));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv, 2);
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_al7230b_set_channel(struct zd_rf *rf, u8 channel)
{
	int r;
	const u32 *rv = chan_rv[channel-1];
	struct zd_chip *chip = zd_rf_to_chip(rf);

	r = zd_iowrite16_locked(chip, 0x57, ZD_CR240);
	if (r)
		return r;
	r = zd_iowrite16_locked(chip, 0xe4, ZD_CR9);
	if (r)
		return r;

	/* PLL_OFF */
	r = zd_iowrite16_locked(chip, 0x2f, ZD_CR251);
	if (r)
		return r;
	r = zd_rfwritev_cr_locked(chip, std_rv, ARRAY_SIZE(std_rv));
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d58);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_sw, ARRAY_SIZE(ioreqs_sw));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv, 2);
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;

	r = zd_iowrite16_locked(chip, 0x7f, ZD_CR251);
	if (r)
		return r;

	return zd1211b_al7230b_finalize(chip);
}

static int zd1211_al7230b_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x00 },
		{ ZD_CR251, 0x3f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_al7230b_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x00 },
		{ ZD_CR251, 0x7f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int al7230b_switch_radio_off(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x04 },
		{ ZD_CR251, 0x2f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

/* ZD1211B+AL7230B 6m band edge patching differs slightly from other
 * configurations */
static int zd1211b_al7230b_patch_6m(struct zd_rf *rf, u8 channel)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR128, 0x14 }, { ZD_CR129, 0x12 },
	};

	/* FIXME: Channel 11 is not the edge for all regulatory domains. */
	if (channel == 1) {
		ioreqs[0].value = 0x0e;
		ioreqs[1].value = 0x10;
	} else if (channel == 11) {
		ioreqs[0].value = 0x10;
		ioreqs[1].value = 0x10;
	}

	dev_dbg_f(zd_chip_dev(chip), "patching for channel %d\n", channel);
	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

int zd_rf_init_al7230b(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);

	if (zd_chip_is_zd1211b(chip)) {
		rf->init_hw = zd1211b_al7230b_init_hw;
		rf->switch_radio_on = zd1211b_al7230b_switch_radio_on;
		rf->set_channel = zd1211b_al7230b_set_channel;
		rf->patch_6m_band_edge = zd1211b_al7230b_patch_6m;
	} else {
		rf->init_hw = zd1211_al7230b_init_hw;
		rf->switch_radio_on = zd1211_al7230b_switch_radio_on;
		rf->set_channel = zd1211_al7230b_set_channel;
		rf->patch_6m_band_edge = zd_rf_generic_patch_6m;
		rf->patch_cck_gain = 1;
	}

	rf->switch_radio_off = al7230b_switch_radio_off;

	return 0;
}
