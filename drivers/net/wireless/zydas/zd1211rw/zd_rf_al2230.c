// SPDX-License-Identifier: GPL-2.0-or-later
/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
 */

#include <linux/kernel.h>

#include "zd_rf.h"
#include "zd_usb.h"
#include "zd_chip.h"

#define IS_AL2230S(chip) ((chip)->al2230s_bit || (chip)->rf.type == AL2230S_RF)

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
	{ ZD_CR240, 0x57 }, { ZD_CR9,   0xe0 },
};

static const struct zd_ioreq16 ioreqs_init_al2230s[] = {
	{ ZD_CR47,   0x1e }, /* MARK_002 */
	{ ZD_CR106,  0x22 },
	{ ZD_CR107,  0x2a }, /* MARK_002 */
	{ ZD_CR109,  0x13 }, /* MARK_002 */
	{ ZD_CR118,  0xf8 }, /* MARK_002 */
	{ ZD_CR119,  0x12 }, { ZD_CR122,  0xe0 },
	{ ZD_CR128,  0x10 }, /* MARK_001 from 0xe->0x10 */
	{ ZD_CR129,  0x0e }, /* MARK_001 from 0xd->0x0e */
	{ ZD_CR130,  0x10 }, /* MARK_001 from 0xb->0x0d */
};

static int zd1211b_al2230_finalize_rf(struct zd_chip *chip)
{
	int r;
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR80,  0x30 }, { ZD_CR81,  0x30 }, { ZD_CR79,  0x58 },
		{ ZD_CR12,  0xf0 }, { ZD_CR77,  0x1b }, { ZD_CR78,  0x58 },
		{ ZD_CR203, 0x06 },
		{ },

		{ ZD_CR240, 0x80 },
	};

	r = zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
	if (r)
		return r;

	/* related to antenna selection? */
	if (chip->new_phy_layout) {
		r = zd_iowrite16_locked(chip, 0xe1, ZD_CR9);
		if (r)
			return r;
	}

	return zd_iowrite16_locked(chip, 0x06, ZD_CR203);
}

static int zd1211_al2230_init_hw(struct zd_rf *rf)
{
	int r;
	struct zd_chip *chip = zd_rf_to_chip(rf);

	static const struct zd_ioreq16 ioreqs_init[] = {
		{ ZD_CR15,   0x20 }, { ZD_CR23,   0x40 }, { ZD_CR24,  0x20 },
		{ ZD_CR26,   0x11 }, { ZD_CR28,   0x3e }, { ZD_CR29,  0x00 },
		{ ZD_CR44,   0x33 }, { ZD_CR106,  0x2a }, { ZD_CR107, 0x1a },
		{ ZD_CR109,  0x09 }, { ZD_CR110,  0x27 }, { ZD_CR111, 0x2b },
		{ ZD_CR112,  0x2b }, { ZD_CR119,  0x0a }, { ZD_CR10,  0x89 },
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
		{ ZD_CR92,   0x0a }, { ZD_CR99,   0x28 }, { ZD_CR100, 0x00 },
		{ ZD_CR101,  0x13 }, { ZD_CR102,  0x27 }, { ZD_CR106, 0x24 },
		{ ZD_CR107,  0x2a }, { ZD_CR109,  0x09 }, { ZD_CR110, 0x13 },
		{ ZD_CR111,  0x1f }, { ZD_CR112,  0x1f }, { ZD_CR113, 0x27 },
		{ ZD_CR114,  0x27 },
		/* for newest (3rd cut) AL2300 */
		{ ZD_CR115,  0x24 },
		{ ZD_CR116,  0x24 }, { ZD_CR117,  0xf4 }, { ZD_CR118, 0xfc },
		{ ZD_CR119,  0x10 }, { ZD_CR120,  0x4f }, { ZD_CR121, 0x77 },
		{ ZD_CR122,  0xe0 }, { ZD_CR137,  0x88 }, { ZD_CR252, 0xff },
		{ ZD_CR253,  0xff },
	};

	static const struct zd_ioreq16 ioreqs_pll[] = {
		/* shdnb(PLL_ON)=0 */
		{ ZD_CR251,  0x2f },
		/* shdnb(PLL_ON)=1 */
		{ ZD_CR251,  0x3f },
		{ ZD_CR138,  0x28 }, { ZD_CR203,  0x06 },
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
		0x0403b9, /* external control TX power (ZD_CR31) */
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

	if (IS_AL2230S(chip)) {
		r = zd_iowrite16a_locked(chip, ioreqs_init_al2230s,
			ARRAY_SIZE(ioreqs_init_al2230s));
		if (r)
			return r;
	}

	r = zd_rfwritev_locked(chip, rv1, ARRAY_SIZE(rv1), RF_RV_BITS);
	if (r)
		return r;

	/* improve band edge for AL2230S */
	if (IS_AL2230S(chip))
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
		{ ZD_CR10,  0x89 }, { ZD_CR15,  0x20 },
		{ ZD_CR17,  0x2B }, /* for newest(3rd cut) AL2230 */
		{ ZD_CR23,  0x40 }, { ZD_CR24,  0x20 }, { ZD_CR26,  0x93 },
		{ ZD_CR28,  0x3e }, { ZD_CR29,  0x00 },
		{ ZD_CR33,  0x28 }, /* 5621 */
		{ ZD_CR34,  0x30 },
		{ ZD_CR35,  0x3e }, /* for newest(3rd cut) AL2230 */
		{ ZD_CR41,  0x24 }, { ZD_CR44,  0x32 },
		{ ZD_CR46,  0x99 }, /* for newest(3rd cut) AL2230 */
		{ ZD_CR47,  0x1e },

		/* ZD1211B 05.06.10 */
		{ ZD_CR48,  0x06 }, { ZD_CR49,  0xf9 }, { ZD_CR51,  0x01 },
		{ ZD_CR52,  0x80 }, { ZD_CR53,  0x7e }, { ZD_CR65,  0x00 },
		{ ZD_CR66,  0x00 }, { ZD_CR67,  0x00 }, { ZD_CR68,  0x00 },
		{ ZD_CR69,  0x28 },

		{ ZD_CR79,  0x58 }, { ZD_CR80,  0x30 }, { ZD_CR81,  0x30 },
		{ ZD_CR87,  0x0a }, { ZD_CR89,  0x04 },
		{ ZD_CR91,  0x00 }, /* 5621 */
		{ ZD_CR92,  0x0a },
		{ ZD_CR98,  0x8d }, /* 4804,  for 1212 new algorithm */
		{ ZD_CR99,  0x00 }, /* 5621 */
		{ ZD_CR101, 0x13 }, { ZD_CR102, 0x27 },
		{ ZD_CR106, 0x24 }, /* for newest(3rd cut) AL2230 */
		{ ZD_CR107, 0x2a },
		{ ZD_CR109, 0x13 }, /* 4804, for 1212 new algorithm */
		{ ZD_CR110, 0x1f }, /* 4804, for 1212 new algorithm */
		{ ZD_CR111, 0x1f }, { ZD_CR112, 0x1f }, { ZD_CR113, 0x27 },
		{ ZD_CR114, 0x27 },
		{ ZD_CR115, 0x26 }, /* 24->26 at 4902 for newest(3rd cut)
				     * AL2230
				     */
		{ ZD_CR116, 0x24 },
		{ ZD_CR117, 0xfa }, /* for 1211b */
		{ ZD_CR118, 0xfa }, /* for 1211b */
		{ ZD_CR119, 0x10 },
		{ ZD_CR120, 0x4f },
		{ ZD_CR121, 0x6c }, /* for 1211b */
		{ ZD_CR122, 0xfc }, /* E0->FC at 4902 */
		{ ZD_CR123, 0x57 }, /* 5623 */
		{ ZD_CR125, 0xad }, /* 4804, for 1212 new algorithm */
		{ ZD_CR126, 0x6c }, /* 5614 */
		{ ZD_CR127, 0x03 }, /* 4804, for 1212 new algorithm */
		{ ZD_CR137, 0x50 }, /* 5614 */
		{ ZD_CR138, 0xa8 },
		{ ZD_CR144, 0xac }, /* 5621 */
		{ ZD_CR150, 0x0d }, { ZD_CR252, 0x34 }, { ZD_CR253, 0x34 },
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
		0x9dc020, /* External control TX power (ZD_CR31) */
		0x5ddb00, /* RegA update for MP version */
		0xd99000, /* RegB update for MP version */
		0x3ffbd0, /* RegC update for MP version */
		0xb00000, /* RegD update for MP version */

		/* improve phase noise and remove phase calibration,4713 */
		0xf01a00,
	};

	static const struct zd_ioreq16 ioreqs2[] = {
		{ ZD_CR251, 0x2f }, /* shdnb(PLL_ON)=0 */
		{ ZD_CR251, 0x7f }, /* shdnb(PLL_ON)=1 */
	};

	static const u32 rv3[] = {
		/* To improve AL2230 yield, 4713 */
		0xf01b00,
		0xf01e00,
		0xf01a00,
	};

	static const struct zd_ioreq16 ioreqs3[] = {
		/* related to 6M band edge patching, happens unconditionally */
		{ ZD_CR128, 0x14 }, { ZD_CR129, 0x12 }, { ZD_CR130, 0x10 },
	};

	r = zd_iowrite16a_locked(chip, zd1211b_ioreqs_shared_1,
		ARRAY_SIZE(zd1211b_ioreqs_shared_1));
	if (r)
		return r;
	r = zd_iowrite16a_locked(chip, ioreqs1, ARRAY_SIZE(ioreqs1));
	if (r)
		return r;

	if (IS_AL2230S(chip)) {
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

	if (IS_AL2230S(chip))
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
		{ ZD_CR138, 0x28 },
		{ ZD_CR203, 0x06 },
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
		{ ZD_CR11,  0x00 },
		{ ZD_CR251, 0x3f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int zd1211b_al2230_switch_radio_on(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x00 },
		{ ZD_CR251, 0x7f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

static int al2230_switch_radio_off(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);
	static const struct zd_ioreq16 ioreqs[] = {
		{ ZD_CR11,  0x04 },
		{ ZD_CR251, 0x2f },
	};

	return zd_iowrite16a_locked(chip, ioreqs, ARRAY_SIZE(ioreqs));
}

int zd_rf_init_al2230(struct zd_rf *rf)
{
	struct zd_chip *chip = zd_rf_to_chip(rf);

	rf->switch_radio_off = al2230_switch_radio_off;
	if (zd_chip_is_zd1211b(chip)) {
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
