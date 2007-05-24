/* zd_rf_al2230.c: Functions for the AL2230 RF controller
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

static const u32 zd1211_al2230_table[][3] = {
	RF_CHANNEL( 1) = { 0x03f790, 0x033331, 0x00000d, },
	RF_CHANNEL( 2) = { 0x03f790, 0x0b3331, 0x00000d, },
	RF_CHANNEL( 3) = { 0x03e790, 0x033331, 0x00000d, },
	RF_CHANNEL( 4) = { 0x03e790, 0x0b3331, 0x00000d, },
	RF_CHANNEL( 5) = { 0x03f7a0, 0x033331, 0x00000d, },
	RF_CHANNEL( 6) = { 0x03f7a0, 0x0b3331, 0x00000d, },
	RF_CHANNEL( 7) = { 0x03e7a0, 0x033331, 0x00000d, },
	RF_CHANNEL( 8) = { 0x03e7a0, 0x0b3331, 0x00000d, },
	RF_CHANNEL( 9) = { 0x03f7b0, 0x033331, 0x00000d, },
	RF_CHANNEL(10) = { 0x03f7b0, 0x0b3331, 0x00000d, },
	RF_CHANNEL(11) = { 0x03e7b0, 0x033331, 0x00000d, },
	RF_CHANNEL(12) = { 0x03e7b0, 0x0b3331, 0x00000d, },
	RF_CHANNEL(13) = { 0x03f7c0, 0x033331, 0x00000d, },
	RF_CHANNEL(14) = { 0x03e7c0, 0x066661, 0x00000d, },
};

static const u32 zd1211b_al2230_table[][3] = {
	RF_CHANNEL( 1) = { 0x09efc0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL( 2) = { 0x09efc0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL( 3) = { 0x09e7c0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL( 4) = { 0x09e7c0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL( 5) = { 0x05efc0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL( 6) = { 0x05efc0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL( 7) = { 0x05e7c0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL( 8) = { 0x05e7c0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL( 9) = { 0x0defc0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL(10) = { 0x0defc0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL(11) = { 0x0de7c0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL(12) = { 0x0de7c0, 0x8cccd0, 0xb00000, },
	RF_CHANNEL(13) = { 0x03efc0, 0x8cccc0, 0xb00000, },
	RF_CHANNEL(14) = { 0x03e7c0, 0x866660, 0xb00000, },
};

static const struct zd_ioreq16 zd1211b_ioreqs_shared_1[] = {
	{ CR240, 0x57 }, { CR9,   0xe0 },
};

static const struct zd_ioreq16 ioreqs_init_al2230s[] = {
	{ CR47,   0x1e }, /* MARK_002 */
	{ CR106,  0x22 },
	{ CR107,  0x2a }, /* MARK_002 */
	{ CR109,  0x13 }, /* MARK_002 */
	{ CR118,  0xf8 }, /* MARK_002 */
	{ CR119,  0x12 }, { CR122,  0xe0 },
	{ CR128,  0x10 }, /* MARK_001 from 0xe->0x10 */
	{ CR129,  0x0e }, /* MARK_001 from 0xd->0x0e */
	{ CR130,  0x10 }, /* MARK_001 from 0xb->0x0d */
};

static int zd1211b_al2230_finalize_rf(struct zd_chip *chip)
{
	int r;
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR80,  0x30 }, { CR81,  0x30 }, { CR79,  0x58 },
		{ CR12,  0xf0 }, { CR77,  0x1b }, { CR78,  0x58 },
		{ CR203, 0x06 },
		{ },

		{ CR240, 0x80 },
	};

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	/* related to antenna selection? */
	if (chip->new_phy_layout) {
		r = zd_iowrite16_locked(chip, 0xe1, CR9);
		if (r)
			return r;
	}

	return zd_iowrite16_locked(chip, 0x06, CR203);
}

static int zd1211_al2230_init_hw(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs_init[] = {
		{ CR15,   0x20 }, { CR23,   0x40 }, { CR24,  0x20 },
		{ CR26,   0x11 }, { CR28,   0x3e }, { CR29,  0x00 },
		{ CR44,   0x33 }, { CR106,  0x2a }, { CR107, 0x1a },
		{ CR109,  0x09 }, { CR110,  0x27 }, { CR111, 0x2b },
		{ CR112,  0x2b }, { CR119,  0x0a }, { CR10,  0x89 },
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
		{ CR92,   0x0a }, { CR99,   0x28 }, { CR100, 0x00 },
		{ CR101,  0x13 }, { CR102,  0x27 }, { CR106, 0x24 },
		{ CR107,  0x2a }, { CR109,  0x09 }, { CR110, 0x13 },
		{ CR111,  0x1f }, { CR112,  0x1f }, { CR113, 0x27 },
		{ CR114,  0x27 },
		/* for newest (3rd cut) AL2300 */
		{ CR115,  0x24 },
		{ CR116,  0x24 }, { CR117,  0xf4 }, { CR118, 0xfc },
		{ CR119,  0x10 }, { CR120,  0x4f }, { CR121, 0x77 },
		{ CR122,  0xe0 }, { CR137,  0x88 }, { CR252, 0xff },
		{ CR253,  0xff },
	};

	static const struct zd_ioreq16 ioreqs_pll[] = {
		/* shdnb(PLL_ON)=0 */
		{ CR251,  0x2f },
		/* shdnb(PLL_ON)=1 */
		{ CR251,  0x3f },
		{ CR138,  0x28 }, { CR203,  0x06 },
	};

	static const u32 rv1[] = {
		/* Channel 1 */
		0x03f790,
		0x033331,
		0x00000d,

		0x0b3331,
		0x03b812,
		0x00fff3,
	};

	static const u32 rv2[] = {
		0x000da4,
		0x0f4dc5, /* fix freq shift, 0x04edc5 */
		0x0805b6,
		0x011687,
		0x000688,
		0x0403b9, /* external control TX power (CR31) */
		0x00dbba,
		0x00099b,
		0x0bdffc,
		0x00000d,
		0x00500f,
	};

	static const u32 rv3[] = {
		0x00d00f,
		0x004c0f,
		0x00540f,
		0x00700f,
		0x00500f,
	};

	r = zd_iowrite16a_locked(chip, ioreqs_init, ARRAY_SIZE(ioreqs_init));
	if (r)
		return r;

	if (chip->al2230s_bit) {
		r = zd_iowrite16a_locked(chip, ioreqs_init_al2230s,
			ARRAY_SIZE(ioreqs_init_al2230s));
		if (r)
			return r;
	}

	r = zd_rfwritev_locked(chip, rv1, ARRAY_SIZE(rv1), RF_RV_BITS);
	if (r)
		return r;

	/* improve band edge for AL2230S */
	if (chip->al2230s_bit)
		r = zd_rfwrite_locked(chip, 0x000824, RF_RV_BITS);
	else
		r = zd_rfwrite_locked(chip, 0x0005a4, RF_RV_BITS);
	if (r)
		return r;

	r = zd_rfwritev_locked(chip, rv2, ARRAY_SIZE(rv2), RF_RV_BITS);
	if (r)
		return r;

	r = zd_iowrite16a_locked(chip, ioreqs_pll, ARRAY_SIZE(ioreqs_pll));
	if (r)
		return r;

	r = zd_rfwritev_locked(chip, rv3, ARRAY_SIZE(rv3), RF_RV_BITS);
	if (r)
		return r;

	return 0;
}

static int zd1211b_al2230_init_hw(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs1[] = {
		{ CR10,  0x89 }, { CR15,  0x20 },
		{ CR17,  0x2B }, /* for newest(3rd cut) AL2230 */
		{ CR23,  0x40 }, { CR24,  0x20 }, { CR26,  0x93 },
		{ CR28,  0x3e }, { CR29,  0x00 },
		{ CR33,  0x28 }, /* 5621 */
		{ CR34,  0x30 },
		{ CR35,  0x3e }, /* for newest(3rd cut) AL2230 */
		{ CR41,  0x24 }, { CR44,  0x32 },
		{ CR46,  0x99 }, /* for newest(3rd cut) AL2230 */
		{ CR47,  0x1e },

		/* ZD1211B 05.06.10 */
		{ CR48,  0x06 }, { CR49,  0xf9 }, { CR51,  0x01 },
		{ CR52,  0x80 }, { CR53,  0x7e }, { CR65,  0x00 },
		{ CR66,  0x00 }, { CR67,  0x00 }, { CR68,  0x00 },
		{ CR69,  0x28 },

		{ CR79,  0x58 }, { CR80,  0x30 }, { CR81,  0x30 },
		{ CR87,  0x0a }, { CR89,  0x04 },
		{ CR91,  0x00 }, /* 5621 */
		{ CR92,  0x0a },
		{ CR98,  0x8d }, /* 4804,  for 1212 new algorithm */
		{ CR99,  0x00 }, /* 5621 */
		{ CR101, 0x13 }, { CR102, 0x27 },
		{ CR106, 0x24 }, /* for newest(3rd cut) AL2230 */
		{ CR107, 0x2a },
		{ CR109, 0x13 }, /* 4804, for 1212 new algorithm */
		{ CR110, 0x1f }, /* 4804, for 1212 new algorithm */
		{ CR111, 0x1f }, { CR112, 0x1f }, { CR113, 0x27 },
		{ CR114, 0x27 },
		{ CR115, 0x26 }, /* 24->26 at 4902 for newest(3rd cut) AL2230 */
		{ CR116, 0x24 },
		{ CR117, 0xfa }, /* for 1211b */
		{ CR118, 0xfa }, /* for 1211b */
		{ CR119, 0x10 },
		{ CR120, 0x4f },
		{ CR121, 0x6c }, /* for 1211b */
		{ CR122, 0xfc }, /* E0->FC at 4902 */
		{ CR123, 0x57 }, /* 5623 */
		{ CR125, 0xad }, /* 4804, for 1212 new algorithm */
		{ CR126, 0x6c }, /* 5614 */
		{ CR127, 0x03 }, /* 4804, for 1212 new algorithm */
		{ CR137, 0x50 }, /* 5614 */
		{ CR138, 0xa8 },
		{ CR144, 0xac }, /* 5621 */
		{ CR150, 0x0d }, { CR252, 0x34 }, { CR253, 0x34 },
	};

	static const u32 rv1[] = {
		0x8cccd0,
		0x481dc0,
		0xcfff00,
		0x25a000,
	};

	static const u32 rv2[] = {
		/* To improve AL2230 yield, improve phase noise, 4713 */
		0x25a000,
		0xa3b2f0,

		0x6da010, /* Reg6 update for MP versio */
		0xe36280, /* Modified by jxiao for Bor-Chin on 2004/08/02 */
		0x116000,
		0x9dc020, /* External control TX power (CR31) */
		0x5ddb00, /* RegA update for MP version */
		0xd99000, /* RegB update for MP version */
		0x3ffbd0, /* RegC update for MP version */
		0xb00000, /* RegD update for MP version */

		/* improve phase noise and remove phase calibration,4713 */
		0xf01a00,
	};

	static const struct zd_ioreq16 ioreqs2[] = {
		{ CR251, 0x2f }, /* shdnb(PLL_ON)=0 */
		{ CR251, 0x7f }, /* shdnb(PLL_ON)=1 */
	};

	static const u32 rv3[] = {
		/* To improve AL2230 yield, 4713 */
		0xf01b00,
		0xf01e00,
		0xf01a00,
	};

	static const struct zd_ioreq16 ioreqs3[] = {
		/* related to 6M band edge patching, happens unconditionally */
		{ CR128, 0x14 }, { CR129, 0x12 }, { CR130, 0x10 },
	};

	r = zd_iowrite16a_locked(chip, zd1211b_ioreqs_shared_1,
		ARRAY_SIZE(zd1211b_ioreqs_shared_1));
	if (r)
		return r;
	r = zd_iowrite16a_locked(chip, ioreqs1, ARRAY_SIZE(ioreqs1));
	if (r)
		return r;

	if (chip->al2230s_bit) {
		r = zd_iowrite16a_locked(chip, ioreqs_init_al2230s,
			ARRAY_SIZE(ioreqs_init_al2230s));
		if (r)
			return r;
	}

	r = zd_rfwritev_cr_locked(chip, zd1211b_al2230_table[0], 3);
	if (r)
		return r;
	r = zd_rfwritev_cr_locked(chip, rv1, ARRAY_SIZE(rv1));
	if (r)
		return r;

	if (chip->al2230s_bit)
		r = zd_rfwrite_locked(chip, 0x241000, RF_RV_BITS);
	else
		r = zd_rfwrite_locked(chip, 0x25a000, RF_RV_BITS);
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv2, ARRAY_SIZE(rv2));
	if (r)
		return r;
	r = zd_iowrite16a_locked(chip, ioreqs2, ARRAY_SIZE(ioreqs2));
	if (r)
		return r;
	r = zd_rfwritev_cr_locked(chip, rv3, ARRAY_SIZE(rv3));
	if (r)
		return r;
	r = zd_iowrite16a_locked(chip, ioreqs3, ARRAY_SIZE(ioreqs3));
	if (r)
		return r;
	return zd1211b_al2230_finalize_rf(chip);
}

static int zd1211_al2230_set_channel(struct zd_rf *rf, u8 channel)
{
	int r;
	const u32 *rv = zd1211_al2230_table[channel-1];
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR138, 0x28 },
		{ CR203, 0x06 },
	};

	r = zd_rfwritev_locked(chip, rv, 3, RF_RV_BITS);
	if (r)
		return r;
	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_al2230_set_channel(struct zd_rf *rf, u8 channel)
{
	int r;
	const u32 *rv = zd1211b_al2230_table[channel-1];
	struct zd_chip *chip = zd_rf_to_chip(rf);

	r = zd_iowrite16a_locked(chip, zd1211b_ioreqs_shared_1,
		ARRAY_SIZE(zd1211b_ioreqs_shared_1));
	if (r)
		return r;

	r = zd_rfwritev_cr_locked(chip, rv, 3);
	if (r)
		return r;

	return zd1211b_al2230_finalize_rf(chip);
}

static int zd1211_al2230_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR11,  0x00 },
		{ CR251, 0x3f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_al2230_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR11,  0x00 },
		{ CR251, 0x7f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int al2230_switch_radio_off(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ CR11,  0x04 },
		{ CR251, 0x2f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

int zd_rf_init_al2230(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);

	rf->switch_radio_off = al2230_switch_radio_off;
	if (chip->is_zd1211b) {
		rf->init_hw = zd1211b_al2230_init_hw;
		rf->set_channel = zd1211b_al2230_set_channel;
		rf->switch_radio_on = zd1211b_al2230_switch_radio_on;
	} else {
		rf->init_hw = zd1211_al2230_init_hw;
		rf->set_channel = zd1211_al2230_set_channel;
		rf->switch_radio_on = zd1211_al2230_switch_radio_on;
	}
	rf->patch_6m_band_edge = zd_rf_generic_patch_6m;
	rf->patch_cck_gain = 1;
	return 0;
}
