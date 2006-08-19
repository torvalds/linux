/* zd_rf_al7230b.c: Functions for the AL7230B RF controller
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

static int al7230b_init_hw(struct zd_rf *rf)
{
	int i, r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	/* All of these writes are identical to AL2230 unless otherwise
	 * specified */
	static const struct zd_ioreq16 ioreqs_1[] = {
		/* This one is 7230-specific, and happens before the rest */
		{ CR240,  0x57 },
		{ },

		{ CR15,   0x20 }, { CR23,   0x40 }, { CR24,  0x20 },
		{ CR26,   0x11 }, { CR28,   0x3e }, { CR29,  0x00 },
		{ CR44,   0x33 },
		/* This value is different for 7230 (was: 0x2a) */
		{ CR106,  0x22 },
		{ CR107,  0x1a }, { CR109,  0x09 }, { CR110,  0x27 },
		{ CR111,  0x2b }, { CR112,  0x2b }, { CR119,  0x0a },
		/* This happened further down in AL2230,
		 * and the value changed (was: 0xe0) */
		{ CR122,  0xfc },
		{ CR10,   0x89 },
		/* for newest (3rd cut) AL2300 */
		{ CR17,   0x28 },
		{ CR26,   0x93 }, { CR34,   0x30 },
		/* for newest (3rd cut) AL2300 */
		{ CR35,   0x3e },
		{ CR41,   0x24 }, { CR44,   0x32 },
		/* for newest (3rd cut) AL2300 */
		{ CR46,   0x96 },
		{ CR47,   0x1e }, { CR79,   0x58 }, { CR80,  0x30 },
		{ CR81,   0x30 }, { CR87,   0x0a }, { CR89,  0x04 },
		{ CR92,   0x0a }, { CR99,   0x28 },
		/* This value is different for 7230 (was: 0x00) */
		{ CR100,  0x02 },
		{ CR101,  0x13 }, { CR102,  0x27 },
		/* This value is different for 7230 (was: 0x24) */
		{ CR106,  0x22 },
		/* This value is different for 7230 (was: 0x2a) */
		{ CR107,  0x3f },
		{ CR109,  0x09 },
		/* This value is different for 7230 (was: 0x13) */
		{ CR110,  0x1f },
		{ CR111,  0x1f }, { CR112,  0x1f }, { CR113, 0x27 },
		{ CR114,  0x27 },
		/* for newest (3rd cut) AL2300 */
		{ CR115,  0x24 },
		/* This value is different for 7230 (was: 0x24) */
		{ CR116,  0x3f },
		/* This value is different for 7230 (was: 0xf4) */
		{ CR117,  0xfa },
		{ CR118,  0xfc }, { CR119,  0x10 }, { CR120, 0x4f },
		{ CR121,  0x77 }, { CR137,  0x88 },
		/* This one is 7230-specific */
		{ CR138,  0xa8 },
		/* This value is different for 7230 (was: 0xff) */
		{ CR252,  0x34 },
		/* This value is different for 7230 (was: 0xff) */
		{ CR253,  0x34 },

		/* PLL_OFF */
		{ CR251, 0x2f },
	};

	static const struct zd_ioreq16 ioreqs_2[] = {
		/* PLL_ON */
		{ CR251, 0x3f },
		{ CR128, 0x14 }, { CR129, 0x12 }, { CR130, 0x10 },
		{ CR38, 0x38 }, { CR136, 0xdf },
	};

	r = zd_iowrite16a_locked(chip, ioreqs_1, ARRAY_SIZE(ioreqs_1));
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0x09ec04);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0x8cccc8);
	if (r)
		return r;

	for (i = 0; i < ARRAY_SIZE(std_rv); i++) {
		r = zd_rfwrite_cr_locked(chip, std_rv[i]);
		if (r)
			return r;
	}

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xbfffff);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0x700000);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d58);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_2, ARRAY_SIZE(ioreqs_2));
	if (r)
		return r;

	r = zd_rfwrite_cr_locked(chip, 0xf15d59);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d5c);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d58);
	if (r)
		return r;

	r = zd_iowrite16_locked(chip, 0x06, CR203);
	if (r)
		return r;
	r = zd_iowrite16_locked(chip, 0x80, CR240);
	if (r)
		return r;

	return 0;
}

static int al7230b_set_channel(struct zd_rf *rf, u8 channel)
{
	int i, r;
	const u32 *rv = chan_rv[channel-1];
	struct zd_chip *chip = zd_rf_to_chip(rf);

	struct zd_ioreq16 ioreqs_1[] = {
		{ CR128, 0x14 }, { CR129, 0x12 }, { CR130, 0x10 },
		{ CR38,  0x38 }, { CR136, 0xdf },
	};

	struct zd_ioreq16 ioreqs_2[] = {
		/* PLL_ON */
		{ CR251, 0x3f },
		{ CR203, 0x06 }, { CR240, 0x08 },
	};

	r = zd_iowrite16_locked(chip, 0x57, CR240);
	if (r)
		return r;

	/* PLL_OFF */
	r = zd_iowrite16_locked(chip, 0x2f, CR251);
	if (r)
		return r;

	for (i = 0; i < ARRAY_SIZE(std_rv); i++) {
		r = zd_rfwrite_cr_locked(chip, std_rv[i]);
		if (r)
			return r;
	}

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;
	r = zd_rfwrite_cr_locked(chip, 0xf15d58);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_1, ARRAY_SIZE(ioreqs_1));
	if (r)
		return r;

	for (i = 0; i < 2; i++) {
		r = zd_rfwrite_cr_locked(chip, rv[i]);
		if (r)
			return r;
	}

	r = zd_rfwrite_cr_locked(chip, 0x3c9000);
	if (r)
		return r;

	return zd_iowrite16a_locked(chip, ioreqs_2, ARRAY_SIZE(ioreqs_2));
}

static int al7230b_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR11,  0x00 },
		{ CR251, 0x3f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int al7230b_switch_radio_off(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR11,  0x04 },
		{ CR251, 0x2f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

int zd_rf_init_al7230b(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);

	if (chip->is_zd1211b) {
		dev_err(zd_chip_dev(chip), "AL7230B is currently not "
			"supported for ZD1211B devices\n");
		return -ENODEV;
	}

	rf->init_hw = al7230b_init_hw;
	rf->set_channel = al7230b_set_channel;
	rf->switch_radio_on = al7230b_switch_radio_on;
	rf->switch_radio_off = al7230b_switch_radio_off;
	rf->patch_6m_band_edge = 1;
	return 0;
}
