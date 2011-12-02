/*
 * RF Buffer handling functions
 *
 * Copyright (c) 2009 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */


/**
 * DOC: RF Buffer registers
 *
 * There are some special registers on the RF chip
 * that control various operation settings related mostly to
 * the analog parts (channel, gain adjustment etc).
 *
 * We don't write on those registers directly but
 * we send a data packet on the chip, using a special register,
 * that holds all the settings we need. After we've sent the
 * data packet, we write on another special register to notify hw
 * to apply the settings. This is done so that control registers
 * can be dynamically programmed during operation and the settings
 * are applied faster on the hw.
 *
 * We call each data packet an "RF Bank" and all the data we write
 * (all RF Banks) "RF Buffer". This file holds initial RF Buffer
 * data for the different RF chips, and various info to match RF
 * Buffer offsets with specific RF registers so that we can access
 * them. We tweak these settings on rfregs_init function.
 *
 * Also check out reg.h and U.S. Patent 6677779 B1 (about buffer
 * registers and control registers):
 *
 * http://www.google.com/patents?id=qNURAAAAEBAJ
 */


/**
 * struct ath5k_ini_rfbuffer - Initial RF Buffer settings
 * @rfb_bank: RF Bank number
 * @rfb_ctrl_register: RF Buffer control register
 * @rfb_mode_data: RF Buffer data for each mode
 *
 * Struct to hold default mode specific RF
 * register values (RF Banks) for each chip.
 */
struct ath5k_ini_rfbuffer {
	u8	rfb_bank;
	u16	rfb_ctrl_register;
	u32	rfb_mode_data[3];
};

/**
 * struct ath5k_rfb_field - An RF Buffer field (register/value)
 * @len: Field length
 * @pos: Offset on the raw packet
 * @col: Used for shifting
 *
 * Struct to hold RF Buffer field
 * infos used to access certain RF
 * analog registers
 */
struct ath5k_rfb_field {
	u8	len;
	u16	pos;
	u8	col;
};

/**
 * struct ath5k_rf_reg - RF analog register definition
 * @bank: RF Buffer Bank number
 * @index: Register's index on ath5k_rf_regx_idx
 * @field: The &struct ath5k_rfb_field
 *
 * We use this struct to define the set of RF registers
 * on each chip that we want to tweak. Some RF registers
 * are common between different chip versions so this saves
 * us space and complexity because we can refer to an rf
 * register by it's index no matter what chip we work with
 * as long as it has that register.
 */
struct ath5k_rf_reg {
	u8			bank;
	u8			index;
	struct ath5k_rfb_field	field;
};

/**
 * enum ath5k_rf_regs_idx - Map RF registers to indexes
 *
 * We do this to handle common bits and make our
 * life easier by using an index for each register
 * instead of a full rfb_field
 */
enum ath5k_rf_regs_idx {
	/* BANK 2 */
	AR5K_RF_TURBO = 0,
	/* BANK 6 */
	AR5K_RF_OB_2GHZ,
	AR5K_RF_OB_5GHZ,
	AR5K_RF_DB_2GHZ,
	AR5K_RF_DB_5GHZ,
	AR5K_RF_FIXED_BIAS_A,
	AR5K_RF_FIXED_BIAS_B,
	AR5K_RF_PWD_XPD,
	AR5K_RF_XPD_SEL,
	AR5K_RF_XPD_GAIN,
	AR5K_RF_PD_GAIN_LO,
	AR5K_RF_PD_GAIN_HI,
	AR5K_RF_HIGH_VC_CP,
	AR5K_RF_MID_VC_CP,
	AR5K_RF_LOW_VC_CP,
	AR5K_RF_PUSH_UP,
	AR5K_RF_PAD2GND,
	AR5K_RF_XB2_LVL,
	AR5K_RF_XB5_LVL,
	AR5K_RF_PWD_ICLOBUF_2G,
	AR5K_RF_PWD_84,
	AR5K_RF_PWD_90,
	AR5K_RF_PWD_130,
	AR5K_RF_PWD_131,
	AR5K_RF_PWD_132,
	AR5K_RF_PWD_136,
	AR5K_RF_PWD_137,
	AR5K_RF_PWD_138,
	AR5K_RF_PWD_166,
	AR5K_RF_PWD_167,
	AR5K_RF_DERBY_CHAN_SEL_MODE,
	/* BANK 7 */
	AR5K_RF_GAIN_I,
	AR5K_RF_PLO_SEL,
	AR5K_RF_RFGAIN_SEL,
	AR5K_RF_RFGAIN_STEP,
	AR5K_RF_WAIT_S,
	AR5K_RF_WAIT_I,
	AR5K_RF_MAX_TIME,
	AR5K_RF_MIXVGA_OVR,
	AR5K_RF_MIXGAIN_OVR,
	AR5K_RF_MIXGAIN_STEP,
	AR5K_RF_PD_DELAY_A,
	AR5K_RF_PD_DELAY_B,
	AR5K_RF_PD_DELAY_XR,
	AR5K_RF_PD_PERIOD_A,
	AR5K_RF_PD_PERIOD_B,
	AR5K_RF_PD_PERIOD_XR,
};


/*******************\
* RF5111 (Sombrero) *
\*******************/

/* BANK 2				len  pos col */
#define	AR5K_RF5111_RF_TURBO		{ 1, 3,   0 }

/* BANK 6				len  pos col */
#define	AR5K_RF5111_OB_2GHZ		{ 3, 119, 0 }
#define	AR5K_RF5111_DB_2GHZ		{ 3, 122, 0 }

#define	AR5K_RF5111_OB_5GHZ		{ 3, 104, 0 }
#define	AR5K_RF5111_DB_5GHZ		{ 3, 107, 0 }

#define	AR5K_RF5111_PWD_XPD		{ 1, 95,  0 }
#define	AR5K_RF5111_XPD_GAIN		{ 4, 96,  0 }

/* Access to PWD registers */
#define AR5K_RF5111_PWD(_n)		{ 1, (135 - _n), 3 }

/* BANK 7				len  pos col */
#define	AR5K_RF5111_GAIN_I		{ 6, 29,  0 }
#define	AR5K_RF5111_PLO_SEL		{ 1, 4,   0 }
#define	AR5K_RF5111_RFGAIN_SEL		{ 1, 36,  0 }
#define AR5K_RF5111_RFGAIN_STEP		{ 6, 37,  0 }
/* Only on AR5212 BaseBand and up */
#define	AR5K_RF5111_WAIT_S		{ 5, 19,  0 }
#define	AR5K_RF5111_WAIT_I		{ 5, 24,  0 }
#define	AR5K_RF5111_MAX_TIME		{ 2, 49,  0 }

static const struct ath5k_rf_reg rf_regs_5111[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF5111_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF5111_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF5111_DB_2GHZ},
	{6, AR5K_RF_OB_5GHZ,		AR5K_RF5111_OB_5GHZ},
	{6, AR5K_RF_DB_5GHZ,		AR5K_RF5111_DB_5GHZ},
	{6, AR5K_RF_PWD_XPD,		AR5K_RF5111_PWD_XPD},
	{6, AR5K_RF_XPD_GAIN,		AR5K_RF5111_XPD_GAIN},
	{6, AR5K_RF_PWD_84,		AR5K_RF5111_PWD(84)},
	{6, AR5K_RF_PWD_90,		AR5K_RF5111_PWD(90)},
	{7, AR5K_RF_GAIN_I,		AR5K_RF5111_GAIN_I},
	{7, AR5K_RF_PLO_SEL,		AR5K_RF5111_PLO_SEL},
	{7, AR5K_RF_RFGAIN_SEL,		AR5K_RF5111_RFGAIN_SEL},
	{7, AR5K_RF_RFGAIN_STEP,	AR5K_RF5111_RFGAIN_STEP},
	{7, AR5K_RF_WAIT_S,		AR5K_RF5111_WAIT_S},
	{7, AR5K_RF_WAIT_I,		AR5K_RF5111_WAIT_I},
	{7, AR5K_RF_MAX_TIME,		AR5K_RF5111_MAX_TIME}
};

/* Default mode specific settings */
static const struct ath5k_ini_rfbuffer rfb_5111[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00380000, 0x00380000, 0x00380000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 0, 0x989c, { 0x00000000, 0x000000c0, 0x00000080 } },
	{ 0, 0x989c, { 0x000400f9, 0x000400ff, 0x000400fd } },
	{ 0, 0x98d4, { 0x00000000, 0x00000004, 0x00000004 } },
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d4, { 0x00000010, 0x00000010, 0x00000010 } },
	{ 3, 0x98d8, { 0x00601068, 0x00601068, 0x00601068 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x10000000, 0x10000000, 0x10000000 } },
	{ 6, 0x989c, { 0x04000000, 0x04000000, 0x04000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x0a000000, 0x00000000 } },
	{ 6, 0x989c, { 0x003800c0, 0x023800c0, 0x003800c0 } },
	{ 6, 0x989c, { 0x00020006, 0x00000006, 0x00020006 } },
	{ 6, 0x989c, { 0x00000089, 0x00000089, 0x00000089 } },
	{ 6, 0x989c, { 0x000000a0, 0x000000a0, 0x000000a0 } },
	{ 6, 0x989c, { 0x00040007, 0x00040007, 0x00040007 } },
	{ 6, 0x98d4, { 0x0000001a, 0x0000001a, 0x0000001a } },
	{ 7, 0x989c, { 0x00000040, 0x00000040, 0x00000040 } },
	{ 7, 0x989c, { 0x00000010, 0x00000010, 0x00000010 } },
	{ 7, 0x989c, { 0x00000008, 0x00000008, 0x00000008 } },
	{ 7, 0x989c, { 0x0000004f, 0x0000004f, 0x0000004f } },
	{ 7, 0x989c, { 0x000000f1, 0x00000061, 0x000000f1 } },
	{ 7, 0x989c, { 0x0000904f, 0x0000904c, 0x0000904f } },
	{ 7, 0x989c, { 0x0000125a, 0x0000129a, 0x0000125a } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000f, 0x0000000e } },
};



/***********************\
* RF5112/RF2112 (Derby) *
\***********************/

/* BANK 2 (Common)			len  pos col */
#define	AR5K_RF5112X_RF_TURBO		{ 1, 1,   2 }

/* BANK 7 (Common)			len  pos col */
#define	AR5K_RF5112X_GAIN_I		{ 6, 14,  0 }
#define	AR5K_RF5112X_MIXVGA_OVR		{ 1, 36,  0 }
#define	AR5K_RF5112X_MIXGAIN_OVR	{ 2, 37,  0 }
#define AR5K_RF5112X_MIXGAIN_STEP	{ 4, 32,  0 }
#define	AR5K_RF5112X_PD_DELAY_A		{ 4, 58,  0 }
#define	AR5K_RF5112X_PD_DELAY_B		{ 4, 62,  0 }
#define	AR5K_RF5112X_PD_DELAY_XR	{ 4, 66,  0 }
#define	AR5K_RF5112X_PD_PERIOD_A	{ 4, 70,  0 }
#define	AR5K_RF5112X_PD_PERIOD_B	{ 4, 74,  0 }
#define	AR5K_RF5112X_PD_PERIOD_XR	{ 4, 78,  0 }

/* RFX112 (Derby 1) */

/* BANK 6				len  pos col */
#define	AR5K_RF5112_OB_2GHZ		{ 3, 269, 0 }
#define	AR5K_RF5112_DB_2GHZ		{ 3, 272, 0 }

#define	AR5K_RF5112_OB_5GHZ		{ 3, 261, 0 }
#define	AR5K_RF5112_DB_5GHZ		{ 3, 264, 0 }

#define	AR5K_RF5112_FIXED_BIAS_A	{ 1, 260, 0 }
#define	AR5K_RF5112_FIXED_BIAS_B	{ 1, 259, 0 }

#define	AR5K_RF5112_XPD_SEL		{ 1, 284, 0 }
#define	AR5K_RF5112_XPD_GAIN		{ 2, 252, 0 }

/* Access to PWD registers */
#define AR5K_RF5112_PWD(_n)		{ 1, (302 - _n), 3 }

static const struct ath5k_rf_reg rf_regs_5112[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF5112X_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF5112_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF5112_DB_2GHZ},
	{6, AR5K_RF_OB_5GHZ,		AR5K_RF5112_OB_5GHZ},
	{6, AR5K_RF_DB_5GHZ,		AR5K_RF5112_DB_5GHZ},
	{6, AR5K_RF_FIXED_BIAS_A,	AR5K_RF5112_FIXED_BIAS_A},
	{6, AR5K_RF_FIXED_BIAS_B,	AR5K_RF5112_FIXED_BIAS_B},
	{6, AR5K_RF_XPD_SEL,		AR5K_RF5112_XPD_SEL},
	{6, AR5K_RF_XPD_GAIN,		AR5K_RF5112_XPD_GAIN},
	{6, AR5K_RF_PWD_130,		AR5K_RF5112_PWD(130)},
	{6, AR5K_RF_PWD_131,		AR5K_RF5112_PWD(131)},
	{6, AR5K_RF_PWD_132,		AR5K_RF5112_PWD(132)},
	{6, AR5K_RF_PWD_136,		AR5K_RF5112_PWD(136)},
	{6, AR5K_RF_PWD_137,		AR5K_RF5112_PWD(137)},
	{6, AR5K_RF_PWD_138,		AR5K_RF5112_PWD(138)},
	{7, AR5K_RF_GAIN_I,		AR5K_RF5112X_GAIN_I},
	{7, AR5K_RF_MIXVGA_OVR,		AR5K_RF5112X_MIXVGA_OVR},
	{7, AR5K_RF_MIXGAIN_OVR,	AR5K_RF5112X_MIXGAIN_OVR},
	{7, AR5K_RF_MIXGAIN_STEP,	AR5K_RF5112X_MIXGAIN_STEP},
	{7, AR5K_RF_PD_DELAY_A,		AR5K_RF5112X_PD_DELAY_A},
	{7, AR5K_RF_PD_DELAY_B,		AR5K_RF5112X_PD_DELAY_B},
	{7, AR5K_RF_PD_DELAY_XR,	AR5K_RF5112X_PD_DELAY_XR},
	{7, AR5K_RF_PD_PERIOD_A,	AR5K_RF5112X_PD_PERIOD_A},
	{7, AR5K_RF_PD_PERIOD_B,	AR5K_RF5112X_PD_PERIOD_B},
	{7, AR5K_RF_PD_PERIOD_XR,	AR5K_RF5112X_PD_PERIOD_XR},
};

/* Default mode specific settings */
static const struct ath5k_ini_rfbuffer rfb_5112[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x03060408, 0x03060408, 0x03060408 } },
	{ 3, 0x98dc, { 0x00a0c0c0, 0x00e0c0c0, 0x00e0c0c0 } },
	{ 6, 0x989c, { 0x00a00000, 0x00a00000, 0x00a00000 } },
	{ 6, 0x989c, { 0x000a0000, 0x000a0000, 0x000a0000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00660000, 0x00660000, 0x00660000 } },
	{ 6, 0x989c, { 0x00db0000, 0x00db0000, 0x00db0000 } },
	{ 6, 0x989c, { 0x00f10000, 0x00f10000, 0x00f10000 } },
	{ 6, 0x989c, { 0x00120000, 0x00120000, 0x00120000 } },
	{ 6, 0x989c, { 0x00120000, 0x00120000, 0x00120000 } },
	{ 6, 0x989c, { 0x00730000, 0x00730000, 0x00730000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x000c0000, 0x000c0000, 0x000c0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x008b0000, 0x008b0000, 0x008b0000 } },
	{ 6, 0x989c, { 0x00600000, 0x00600000, 0x00600000 } },
	{ 6, 0x989c, { 0x000c0000, 0x000c0000, 0x000c0000 } },
	{ 6, 0x989c, { 0x00840000, 0x00840000, 0x00840000 } },
	{ 6, 0x989c, { 0x00640000, 0x00640000, 0x00640000 } },
	{ 6, 0x989c, { 0x00200000, 0x00200000, 0x00200000 } },
	{ 6, 0x989c, { 0x00240000, 0x00240000, 0x00240000 } },
	{ 6, 0x989c, { 0x00250000, 0x00250000, 0x00250000 } },
	{ 6, 0x989c, { 0x00110000, 0x00110000, 0x00110000 } },
	{ 6, 0x989c, { 0x00110000, 0x00110000, 0x00110000 } },
	{ 6, 0x989c, { 0x00510000, 0x00510000, 0x00510000 } },
	{ 6, 0x989c, { 0x1c040000, 0x1c040000, 0x1c040000 } },
	{ 6, 0x989c, { 0x000a0000, 0x000a0000, 0x000a0000 } },
	{ 6, 0x989c, { 0x00a10000, 0x00a10000, 0x00a10000 } },
	{ 6, 0x989c, { 0x00400000, 0x00400000, 0x00400000 } },
	{ 6, 0x989c, { 0x03090000, 0x03090000, 0x03090000 } },
	{ 6, 0x989c, { 0x06000000, 0x06000000, 0x06000000 } },
	{ 6, 0x989c, { 0x000000b0, 0x000000a8, 0x000000a8 } },
	{ 6, 0x989c, { 0x0000002e, 0x0000002e, 0x0000002e } },
	{ 6, 0x989c, { 0x006c4a41, 0x006c4af1, 0x006c4a61 } },
	{ 6, 0x989c, { 0x0050892a, 0x0050892b, 0x0050892b } },
	{ 6, 0x989c, { 0x00842400, 0x00842400, 0x00842400 } },
	{ 6, 0x989c, { 0x00c69200, 0x00c69200, 0x00c69200 } },
	{ 6, 0x98d0, { 0x0002000c, 0x0002000c, 0x0002000c } },
	{ 7, 0x989c, { 0x00000094, 0x00000094, 0x00000094 } },
	{ 7, 0x989c, { 0x00000091, 0x00000091, 0x00000091 } },
	{ 7, 0x989c, { 0x0000000a, 0x00000012, 0x00000012 } },
	{ 7, 0x989c, { 0x00000080, 0x00000080, 0x00000080 } },
	{ 7, 0x989c, { 0x000000c1, 0x000000c1, 0x000000c1 } },
	{ 7, 0x989c, { 0x00000060, 0x00000060, 0x00000060 } },
	{ 7, 0x989c, { 0x000000f0, 0x000000f0, 0x000000f0 } },
	{ 7, 0x989c, { 0x00000022, 0x00000022, 0x00000022 } },
	{ 7, 0x989c, { 0x00000092, 0x00000092, 0x00000092 } },
	{ 7, 0x989c, { 0x000000d4, 0x000000d4, 0x000000d4 } },
	{ 7, 0x989c, { 0x000014cc, 0x000014cc, 0x000014cc } },
	{ 7, 0x989c, { 0x0000048c, 0x0000048c, 0x0000048c } },
	{ 7, 0x98c4, { 0x00000003, 0x00000003, 0x00000003 } },
};

/* RFX112A (Derby 2) */

/* BANK 6				len  pos col */
#define	AR5K_RF5112A_OB_2GHZ		{ 3, 287, 0 }
#define	AR5K_RF5112A_DB_2GHZ		{ 3, 290, 0 }

#define	AR5K_RF5112A_OB_5GHZ		{ 3, 279, 0 }
#define	AR5K_RF5112A_DB_5GHZ		{ 3, 282, 0 }

#define	AR5K_RF5112A_FIXED_BIAS_A	{ 1, 278, 0 }
#define	AR5K_RF5112A_FIXED_BIAS_B	{ 1, 277, 0 }

#define	AR5K_RF5112A_XPD_SEL		{ 1, 302, 0 }
#define	AR5K_RF5112A_PDGAINLO		{ 2, 270, 0 }
#define	AR5K_RF5112A_PDGAINHI		{ 2, 257, 0 }

/* Access to PWD registers */
#define AR5K_RF5112A_PWD(_n)		{ 1, (306 - _n), 3 }

/* Voltage regulators */
#define	AR5K_RF5112A_HIGH_VC_CP		{ 2, 90,  2 }
#define	AR5K_RF5112A_MID_VC_CP		{ 2, 92,  2 }
#define	AR5K_RF5112A_LOW_VC_CP		{ 2, 94,  2 }
#define	AR5K_RF5112A_PUSH_UP		{ 1, 254,  2 }

/* Power consumption */
#define	AR5K_RF5112A_PAD2GND		{ 1, 281, 1 }
#define	AR5K_RF5112A_XB2_LVL		{ 2, 1,	  3 }
#define	AR5K_RF5112A_XB5_LVL		{ 2, 3,	  3 }

static const struct ath5k_rf_reg rf_regs_5112a[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF5112X_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF5112A_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF5112A_DB_2GHZ},
	{6, AR5K_RF_OB_5GHZ,		AR5K_RF5112A_OB_5GHZ},
	{6, AR5K_RF_DB_5GHZ,		AR5K_RF5112A_DB_5GHZ},
	{6, AR5K_RF_FIXED_BIAS_A,	AR5K_RF5112A_FIXED_BIAS_A},
	{6, AR5K_RF_FIXED_BIAS_B,	AR5K_RF5112A_FIXED_BIAS_B},
	{6, AR5K_RF_XPD_SEL,		AR5K_RF5112A_XPD_SEL},
	{6, AR5K_RF_PD_GAIN_LO,		AR5K_RF5112A_PDGAINLO},
	{6, AR5K_RF_PD_GAIN_HI,		AR5K_RF5112A_PDGAINHI},
	{6, AR5K_RF_PWD_130,		AR5K_RF5112A_PWD(130)},
	{6, AR5K_RF_PWD_131,		AR5K_RF5112A_PWD(131)},
	{6, AR5K_RF_PWD_132,		AR5K_RF5112A_PWD(132)},
	{6, AR5K_RF_PWD_136,		AR5K_RF5112A_PWD(136)},
	{6, AR5K_RF_PWD_137,		AR5K_RF5112A_PWD(137)},
	{6, AR5K_RF_PWD_138,		AR5K_RF5112A_PWD(138)},
	{6, AR5K_RF_PWD_166,		AR5K_RF5112A_PWD(166)},
	{6, AR5K_RF_PWD_167,		AR5K_RF5112A_PWD(167)},
	{6, AR5K_RF_HIGH_VC_CP,		AR5K_RF5112A_HIGH_VC_CP},
	{6, AR5K_RF_MID_VC_CP,		AR5K_RF5112A_MID_VC_CP},
	{6, AR5K_RF_LOW_VC_CP,		AR5K_RF5112A_LOW_VC_CP},
	{6, AR5K_RF_PUSH_UP,		AR5K_RF5112A_PUSH_UP},
	{6, AR5K_RF_PAD2GND,		AR5K_RF5112A_PAD2GND},
	{6, AR5K_RF_XB2_LVL,		AR5K_RF5112A_XB2_LVL},
	{6, AR5K_RF_XB5_LVL,		AR5K_RF5112A_XB5_LVL},
	{7, AR5K_RF_GAIN_I,		AR5K_RF5112X_GAIN_I},
	{7, AR5K_RF_MIXVGA_OVR,		AR5K_RF5112X_MIXVGA_OVR},
	{7, AR5K_RF_MIXGAIN_OVR,	AR5K_RF5112X_MIXGAIN_OVR},
	{7, AR5K_RF_MIXGAIN_STEP,	AR5K_RF5112X_MIXGAIN_STEP},
	{7, AR5K_RF_PD_DELAY_A,		AR5K_RF5112X_PD_DELAY_A},
	{7, AR5K_RF_PD_DELAY_B,		AR5K_RF5112X_PD_DELAY_B},
	{7, AR5K_RF_PD_DELAY_XR,	AR5K_RF5112X_PD_DELAY_XR},
	{7, AR5K_RF_PD_PERIOD_A,	AR5K_RF5112X_PD_PERIOD_A},
	{7, AR5K_RF_PD_PERIOD_B,	AR5K_RF5112X_PD_PERIOD_B},
	{7, AR5K_RF_PD_PERIOD_XR,	AR5K_RF5112X_PD_PERIOD_XR},
};

/* Default mode specific settings */
static const struct ath5k_ini_rfbuffer rfb_5112a[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x03060408, 0x03060408, 0x03060408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0x0f000000, 0x0f000000, 0x0f000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00800000, 0x00800000, 0x00800000 } },
	{ 6, 0x989c, { 0x002a0000, 0x002a0000, 0x002a0000 } },
	{ 6, 0x989c, { 0x00010000, 0x00010000, 0x00010000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00180000, 0x00180000, 0x00180000 } },
	{ 6, 0x989c, { 0x00600000, 0x006e0000, 0x006e0000 } },
	{ 6, 0x989c, { 0x00c70000, 0x00c70000, 0x00c70000 } },
	{ 6, 0x989c, { 0x004b0000, 0x004b0000, 0x004b0000 } },
	{ 6, 0x989c, { 0x04480000, 0x04480000, 0x04480000 } },
	{ 6, 0x989c, { 0x004c0000, 0x004c0000, 0x004c0000 } },
	{ 6, 0x989c, { 0x00e40000, 0x00e40000, 0x00e40000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00fc0000, 0x00fc0000, 0x00fc0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x043f0000, 0x043f0000, 0x043f0000 } },
	{ 6, 0x989c, { 0x000c0000, 0x000c0000, 0x000c0000 } },
	{ 6, 0x989c, { 0x02190000, 0x02190000, 0x02190000 } },
	{ 6, 0x989c, { 0x00240000, 0x00240000, 0x00240000 } },
	{ 6, 0x989c, { 0x00b40000, 0x00b40000, 0x00b40000 } },
	{ 6, 0x989c, { 0x00990000, 0x00990000, 0x00990000 } },
	{ 6, 0x989c, { 0x00500000, 0x00500000, 0x00500000 } },
	{ 6, 0x989c, { 0x002a0000, 0x002a0000, 0x002a0000 } },
	{ 6, 0x989c, { 0x00120000, 0x00120000, 0x00120000 } },
	{ 6, 0x989c, { 0xc0320000, 0xc0320000, 0xc0320000 } },
	{ 6, 0x989c, { 0x01740000, 0x01740000, 0x01740000 } },
	{ 6, 0x989c, { 0x00110000, 0x00110000, 0x00110000 } },
	{ 6, 0x989c, { 0x86280000, 0x86280000, 0x86280000 } },
	{ 6, 0x989c, { 0x31840000, 0x31840000, 0x31840000 } },
	{ 6, 0x989c, { 0x00f20080, 0x00f20080, 0x00f20080 } },
	{ 6, 0x989c, { 0x00270019, 0x00270019, 0x00270019 } },
	{ 6, 0x989c, { 0x00000003, 0x00000003, 0x00000003 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x000000b2, 0x000000b2, 0x000000b2 } },
	{ 6, 0x989c, { 0x00b02084, 0x00b02084, 0x00b02084 } },
	{ 6, 0x989c, { 0x004125a4, 0x004125a4, 0x004125a4 } },
	{ 6, 0x989c, { 0x00119220, 0x00119220, 0x00119220 } },
	{ 6, 0x989c, { 0x001a4800, 0x001a4800, 0x001a4800 } },
	{ 6, 0x98d8, { 0x000b0230, 0x000b0230, 0x000b0230 } },
	{ 7, 0x989c, { 0x00000094, 0x00000094, 0x00000094 } },
	{ 7, 0x989c, { 0x00000091, 0x00000091, 0x00000091 } },
	{ 7, 0x989c, { 0x00000012, 0x00000012, 0x00000012 } },
	{ 7, 0x989c, { 0x00000080, 0x00000080, 0x00000080 } },
	{ 7, 0x989c, { 0x000000d9, 0x000000d9, 0x000000d9 } },
	{ 7, 0x989c, { 0x00000060, 0x00000060, 0x00000060 } },
	{ 7, 0x989c, { 0x000000f0, 0x000000f0, 0x000000f0 } },
	{ 7, 0x989c, { 0x000000a2, 0x000000a2, 0x000000a2 } },
	{ 7, 0x989c, { 0x00000052, 0x00000052, 0x00000052 } },
	{ 7, 0x989c, { 0x000000d4, 0x000000d4, 0x000000d4 } },
	{ 7, 0x989c, { 0x000014cc, 0x000014cc, 0x000014cc } },
	{ 7, 0x989c, { 0x0000048c, 0x0000048c, 0x0000048c } },
	{ 7, 0x98c4, { 0x00000003, 0x00000003, 0x00000003 } },
};



/******************\
* RF2413 (Griffin) *
\******************/

/* BANK 2				len  pos col */
#define AR5K_RF2413_RF_TURBO		{ 1, 1,   2 }

/* BANK 6				len  pos col */
#define	AR5K_RF2413_OB_2GHZ		{ 3, 168, 0 }
#define	AR5K_RF2413_DB_2GHZ		{ 3, 165, 0 }

static const struct ath5k_rf_reg rf_regs_2413[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF2413_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF2413_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF2413_DB_2GHZ},
};

/* Default mode specific settings
 * XXX: a/aTurbo ???
 */
static const struct ath5k_ini_rfbuffer rfb_2413[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x02001408, 0x02001408, 0x02001408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0xf0000000, 0xf0000000, 0xf0000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x03000000, 0x03000000, 0x03000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x40400000, 0x40400000, 0x40400000 } },
	{ 6, 0x989c, { 0x65050000, 0x65050000, 0x65050000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00420000, 0x00420000, 0x00420000 } },
	{ 6, 0x989c, { 0x00b50000, 0x00b50000, 0x00b50000 } },
	{ 6, 0x989c, { 0x00030000, 0x00030000, 0x00030000 } },
	{ 6, 0x989c, { 0x00f70000, 0x00f70000, 0x00f70000 } },
	{ 6, 0x989c, { 0x009d0000, 0x009d0000, 0x009d0000 } },
	{ 6, 0x989c, { 0x00220000, 0x00220000, 0x00220000 } },
	{ 6, 0x989c, { 0x04220000, 0x04220000, 0x04220000 } },
	{ 6, 0x989c, { 0x00230018, 0x00230018, 0x00230018 } },
	{ 6, 0x989c, { 0x00280000, 0x00280060, 0x00280060 } },
	{ 6, 0x989c, { 0x005000c0, 0x005000c3, 0x005000c3 } },
	{ 6, 0x989c, { 0x0004007f, 0x0004007f, 0x0004007f } },
	{ 6, 0x989c, { 0x00000458, 0x00000458, 0x00000458 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x0000c000, 0x0000c000, 0x0000c000 } },
	{ 6, 0x98d8, { 0x00400230, 0x00400230, 0x00400230 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};



/***************************\
* RF2315/RF2316 (Cobra SoC) *
\***************************/

/* BANK 2				len  pos col */
#define	AR5K_RF2316_RF_TURBO		{ 1, 1,   2 }

/* BANK 6				len  pos col */
#define	AR5K_RF2316_OB_2GHZ		{ 3, 178, 0 }
#define	AR5K_RF2316_DB_2GHZ		{ 3, 175, 0 }

static const struct ath5k_rf_reg rf_regs_2316[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF2316_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF2316_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF2316_DB_2GHZ},
};

/* Default mode specific settings */
static const struct ath5k_ini_rfbuffer rfb_2316[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x02001408, 0x02001408, 0x02001408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0xc0000000, 0xc0000000, 0xc0000000 } },
	{ 6, 0x989c, { 0x0f000000, 0x0f000000, 0x0f000000 } },
	{ 6, 0x989c, { 0x02000000, 0x02000000, 0x02000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0xf8000000, 0xf8000000, 0xf8000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x95150000, 0x95150000, 0x95150000 } },
	{ 6, 0x989c, { 0xc1000000, 0xc1000000, 0xc1000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00080000, 0x00080000, 0x00080000 } },
	{ 6, 0x989c, { 0x00d50000, 0x00d50000, 0x00d50000 } },
	{ 6, 0x989c, { 0x000e0000, 0x000e0000, 0x000e0000 } },
	{ 6, 0x989c, { 0x00dc0000, 0x00dc0000, 0x00dc0000 } },
	{ 6, 0x989c, { 0x00770000, 0x00770000, 0x00770000 } },
	{ 6, 0x989c, { 0x008a0000, 0x008a0000, 0x008a0000 } },
	{ 6, 0x989c, { 0x10880000, 0x10880000, 0x10880000 } },
	{ 6, 0x989c, { 0x008c0060, 0x008c0060, 0x008c0060 } },
	{ 6, 0x989c, { 0x00a00000, 0x00a00080, 0x00a00080 } },
	{ 6, 0x989c, { 0x00400000, 0x0040000d, 0x0040000d } },
	{ 6, 0x989c, { 0x00110400, 0x00110400, 0x00110400 } },
	{ 6, 0x989c, { 0x00000060, 0x00000060, 0x00000060 } },
	{ 6, 0x989c, { 0x00000001, 0x00000001, 0x00000001 } },
	{ 6, 0x989c, { 0x00000b00, 0x00000b00, 0x00000b00 } },
	{ 6, 0x989c, { 0x00000be8, 0x00000be8, 0x00000be8 } },
	{ 6, 0x98c0, { 0x00010000, 0x00010000, 0x00010000 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};



/******************************\
* RF5413/RF5424 (Eagle/Condor) *
\******************************/

/* BANK 6				len  pos col */
#define	AR5K_RF5413_OB_2GHZ		{ 3, 241, 0 }
#define	AR5K_RF5413_DB_2GHZ		{ 3, 238, 0 }

#define	AR5K_RF5413_OB_5GHZ		{ 3, 247, 0 }
#define	AR5K_RF5413_DB_5GHZ		{ 3, 244, 0 }

#define	AR5K_RF5413_PWD_ICLOBUF2G	{ 3, 131, 3 }
#define	AR5K_RF5413_DERBY_CHAN_SEL_MODE	{ 1, 291, 2 }

static const struct ath5k_rf_reg rf_regs_5413[] = {
	{6, AR5K_RF_OB_2GHZ,		 AR5K_RF5413_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		 AR5K_RF5413_DB_2GHZ},
	{6, AR5K_RF_OB_5GHZ,		 AR5K_RF5413_OB_5GHZ},
	{6, AR5K_RF_DB_5GHZ,		 AR5K_RF5413_DB_5GHZ},
	{6, AR5K_RF_PWD_ICLOBUF_2G,	 AR5K_RF5413_PWD_ICLOBUF2G},
	{6, AR5K_RF_DERBY_CHAN_SEL_MODE, AR5K_RF5413_DERBY_CHAN_SEL_MODE},
};

/* Default mode specific settings */
static const struct ath5k_ini_rfbuffer rfb_5413[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x00000008, 0x00000008, 0x00000008 } },
	{ 3, 0x98dc, { 0x00a000c0, 0x00e000c0, 0x00e000c0 } },
	{ 6, 0x989c, { 0x33000000, 0x33000000, 0x33000000 } },
	{ 6, 0x989c, { 0x01000000, 0x01000000, 0x01000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x1f000000, 0x1f000000, 0x1f000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00b80000, 0x00b80000, 0x00b80000 } },
	{ 6, 0x989c, { 0x00b70000, 0x00b70000, 0x00b70000 } },
	{ 6, 0x989c, { 0x00840000, 0x00840000, 0x00840000 } },
	{ 6, 0x989c, { 0x00980000, 0x00980000, 0x00980000 } },
	{ 6, 0x989c, { 0x00c00000, 0x00c00000, 0x00c00000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x00ff0000, 0x00ff0000, 0x00ff0000 } },
	{ 6, 0x989c, { 0x00d70000, 0x00d70000, 0x00d70000 } },
	{ 6, 0x989c, { 0x00610000, 0x00610000, 0x00610000 } },
	{ 6, 0x989c, { 0x00fe0000, 0x00fe0000, 0x00fe0000 } },
	{ 6, 0x989c, { 0x00de0000, 0x00de0000, 0x00de0000 } },
	{ 6, 0x989c, { 0x007f0000, 0x007f0000, 0x007f0000 } },
	{ 6, 0x989c, { 0x043d0000, 0x043d0000, 0x043d0000 } },
	{ 6, 0x989c, { 0x00770000, 0x00770000, 0x00770000 } },
	{ 6, 0x989c, { 0x00440000, 0x00440000, 0x00440000 } },
	{ 6, 0x989c, { 0x00980000, 0x00980000, 0x00980000 } },
	{ 6, 0x989c, { 0x00100080, 0x00100080, 0x00100080 } },
	{ 6, 0x989c, { 0x0005c034, 0x0005c034, 0x0005c034 } },
	{ 6, 0x989c, { 0x003100f0, 0x003100f0, 0x003100f0 } },
	{ 6, 0x989c, { 0x000c011f, 0x000c011f, 0x000c011f } },
	{ 6, 0x989c, { 0x00510040, 0x00510040, 0x00510040 } },
	{ 6, 0x989c, { 0x005000da, 0x005000da, 0x005000da } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00004044, 0x00004044, 0x00004044 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x000060c0, 0x000060c0, 0x000060c0 } },
	{ 6, 0x989c, { 0x00002c00, 0x00003600, 0x00003600 } },
	{ 6, 0x98c8, { 0x00000403, 0x00040403, 0x00040403 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};



/***************************\
* RF2425/RF2417 (Swan/Nala) *
* AR2317 (Spider SoC)       *
\***************************/

/* BANK 2				len  pos col */
#define AR5K_RF2425_RF_TURBO		{ 1, 1,   2 }

/* BANK 6				len  pos col */
#define	AR5K_RF2425_OB_2GHZ		{ 3, 193, 0 }
#define	AR5K_RF2425_DB_2GHZ		{ 3, 190, 0 }

static const struct ath5k_rf_reg rf_regs_2425[] = {
	{2, AR5K_RF_TURBO,		AR5K_RF2425_RF_TURBO},
	{6, AR5K_RF_OB_2GHZ,		AR5K_RF2425_OB_2GHZ},
	{6, AR5K_RF_DB_2GHZ,		AR5K_RF2425_DB_2GHZ},
};

/* Default mode specific settings
 */
static const struct ath5k_ini_rfbuffer rfb_2425[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x02001408, 0x02001408, 0x02001408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0x10000000, 0x10000000, 0x10000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x002a0000, 0x002a0000, 0x002a0000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00100000, 0x00100000, 0x00100000 } },
	{ 6, 0x989c, { 0x00020000, 0x00020000, 0x00020000 } },
	{ 6, 0x989c, { 0x00730000, 0x00730000, 0x00730000 } },
	{ 6, 0x989c, { 0x00f80000, 0x00f80000, 0x00f80000 } },
	{ 6, 0x989c, { 0x00e70000, 0x00e70000, 0x00e70000 } },
	{ 6, 0x989c, { 0x00140000, 0x00140000, 0x00140000 } },
	{ 6, 0x989c, { 0x00910040, 0x00910040, 0x00910040 } },
	{ 6, 0x989c, { 0x0007001a, 0x0007001a, 0x0007001a } },
	{ 6, 0x989c, { 0x00410000, 0x00410000, 0x00410000 } },
	{ 6, 0x989c, { 0x00810000, 0x00810060, 0x00810060 } },
	{ 6, 0x989c, { 0x00020800, 0x00020803, 0x00020803 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00001660, 0x00001660, 0x00001660 } },
	{ 6, 0x989c, { 0x00001688, 0x00001688, 0x00001688 } },
	{ 6, 0x98c4, { 0x00000001, 0x00000001, 0x00000001 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};

/*
 * TODO: Handle the few differences with swan during
 * bank modification and get rid of this
 */
static const struct ath5k_ini_rfbuffer rfb_2317[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x02001408, 0x02001408, 0x02001408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0x10000000, 0x10000000, 0x10000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x002a0000, 0x002a0000, 0x002a0000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00100000, 0x00100000, 0x00100000 } },
	{ 6, 0x989c, { 0x00020000, 0x00020000, 0x00020000 } },
	{ 6, 0x989c, { 0x00730000, 0x00730000, 0x00730000 } },
	{ 6, 0x989c, { 0x00f80000, 0x00f80000, 0x00f80000 } },
	{ 6, 0x989c, { 0x00e70000, 0x00e70000, 0x00e70000 } },
	{ 6, 0x989c, { 0x00140100, 0x00140100, 0x00140100 } },
	{ 6, 0x989c, { 0x00910040, 0x00910040, 0x00910040 } },
	{ 6, 0x989c, { 0x0007001a, 0x0007001a, 0x0007001a } },
	{ 6, 0x989c, { 0x00410000, 0x00410000, 0x00410000 } },
	{ 6, 0x989c, { 0x00810000, 0x00810060, 0x00810060 } },
	{ 6, 0x989c, { 0x00020800, 0x00020803, 0x00020803 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00001660, 0x00001660, 0x00001660 } },
	{ 6, 0x989c, { 0x00009688, 0x00009688, 0x00009688 } },
	{ 6, 0x98c4, { 0x00000001, 0x00000001, 0x00000001 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};

/*
 * TODO: Handle the few differences with swan during
 * bank modification and get rid of this
 */
static const struct ath5k_ini_rfbuffer rfb_2417[] = {
	/* BANK / C.R.     A/XR         B           G      */
	{ 1, 0x98d4, { 0x00000020, 0x00000020, 0x00000020 } },
	{ 2, 0x98d0, { 0x02001408, 0x02001408, 0x02001408 } },
	{ 3, 0x98dc, { 0x00a020c0, 0x00e020c0, 0x00e020c0 } },
	{ 6, 0x989c, { 0x10000000, 0x10000000, 0x10000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x002a0000, 0x002a0000, 0x002a0000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00100000, 0x00100000, 0x00100000 } },
	{ 6, 0x989c, { 0x00020000, 0x00020000, 0x00020000 } },
	{ 6, 0x989c, { 0x00730000, 0x00730000, 0x00730000 } },
	{ 6, 0x989c, { 0x00f80000, 0x00f80000, 0x00f80000 } },
	{ 6, 0x989c, { 0x00e70000, 0x80e70000, 0x80e70000 } },
	{ 6, 0x989c, { 0x00140000, 0x00140000, 0x00140000 } },
	{ 6, 0x989c, { 0x00910040, 0x00910040, 0x00910040 } },
	{ 6, 0x989c, { 0x0007001a, 0x0207001a, 0x0207001a } },
	{ 6, 0x989c, { 0x00410000, 0x00410000, 0x00410000 } },
	{ 6, 0x989c, { 0x00810000, 0x00810060, 0x00810060 } },
	{ 6, 0x989c, { 0x00020800, 0x00020803, 0x00020803 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00000000, 0x00000000, 0x00000000 } },
	{ 6, 0x989c, { 0x00001660, 0x00001660, 0x00001660 } },
	{ 6, 0x989c, { 0x00001688, 0x00001688, 0x00001688 } },
	{ 6, 0x98c4, { 0x00000001, 0x00000001, 0x00000001 } },
	{ 7, 0x989c, { 0x00006400, 0x00006400, 0x00006400 } },
	{ 7, 0x989c, { 0x00000800, 0x00000800, 0x00000800 } },
	{ 7, 0x98cc, { 0x0000000e, 0x0000000e, 0x0000000e } },
};
