/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
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

/*
 * Common ar5xxx EEPROM data offsets (set these on AR5K_EEPROM_BASE)
 */
#define AR5K_EEPROM_MAGIC		0x003d	/* EEPROM Magic number */
#define AR5K_EEPROM_MAGIC_VALUE		0x5aa5	/* Default - found on EEPROM */
#define AR5K_EEPROM_MAGIC_5212		0x0000145c /* 5212 */
#define AR5K_EEPROM_MAGIC_5211		0x0000145b /* 5211 */
#define AR5K_EEPROM_MAGIC_5210		0x0000145a /* 5210 */

#define AR5K_EEPROM_PROTECT		0x003f	/* EEPROM protect status */
#define AR5K_EEPROM_PROTECT_RD_0_31	0x0001	/* Read protection bit for offsets 0x0 - 0x1f */
#define AR5K_EEPROM_PROTECT_WR_0_31	0x0002	/* Write protection bit for offsets 0x0 - 0x1f */
#define AR5K_EEPROM_PROTECT_RD_32_63	0x0004	/* 0x20 - 0x3f */
#define AR5K_EEPROM_PROTECT_WR_32_63	0x0008
#define AR5K_EEPROM_PROTECT_RD_64_127	0x0010	/* 0x40 - 0x7f */
#define AR5K_EEPROM_PROTECT_WR_64_127	0x0020
#define AR5K_EEPROM_PROTECT_RD_128_191	0x0040	/* 0x80 - 0xbf (regdom) */
#define AR5K_EEPROM_PROTECT_WR_128_191	0x0080
#define AR5K_EEPROM_PROTECT_RD_192_207	0x0100	/* 0xc0 - 0xcf */
#define AR5K_EEPROM_PROTECT_WR_192_207	0x0200
#define AR5K_EEPROM_PROTECT_RD_208_223	0x0400	/* 0xd0 - 0xdf */
#define AR5K_EEPROM_PROTECT_WR_208_223	0x0800
#define AR5K_EEPROM_PROTECT_RD_224_239	0x1000	/* 0xe0 - 0xef */
#define AR5K_EEPROM_PROTECT_WR_224_239	0x2000
#define AR5K_EEPROM_PROTECT_RD_240_255	0x4000	/* 0xf0 - 0xff */
#define AR5K_EEPROM_PROTECT_WR_240_255	0x8000
#define AR5K_EEPROM_REG_DOMAIN		0x00bf	/* EEPROM regdom */
#define AR5K_EEPROM_INFO_BASE		0x00c0	/* EEPROM header */
#define AR5K_EEPROM_INFO_MAX		(0x400 - AR5K_EEPROM_INFO_BASE)
#define AR5K_EEPROM_INFO_CKSUM		0xffff
#define AR5K_EEPROM_INFO(_n)		(AR5K_EEPROM_INFO_BASE + (_n))

#define AR5K_EEPROM_VERSION		AR5K_EEPROM_INFO(1)	/* EEPROM Version */
#define AR5K_EEPROM_VERSION_3_0		0x3000	/* No idea what's going on before this version */
#define AR5K_EEPROM_VERSION_3_1		0x3001	/* ob/db values for 2Ghz (ar5211_rfregs) */
#define AR5K_EEPROM_VERSION_3_2		0x3002	/* different frequency representation (eeprom_bin2freq) */
#define AR5K_EEPROM_VERSION_3_3		0x3003	/* offsets changed, has 32 CTLs (see below) and ee_false_detect (eeprom_read_modes) */
#define AR5K_EEPROM_VERSION_3_4		0x3004	/* has ee_i_gain ee_cck_ofdm_power_delta (eeprom_read_modes) */
#define AR5K_EEPROM_VERSION_4_0		0x4000	/* has ee_misc*, ee_cal_pier, ee_turbo_max_power and ee_xr_power (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_1		0x4001	/* has ee_margin_tx_rx (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_2		0x4002	/* has ee_cck_ofdm_gain_delta (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_3		0x4003
#define AR5K_EEPROM_VERSION_4_4		0x4004
#define AR5K_EEPROM_VERSION_4_5		0x4005
#define AR5K_EEPROM_VERSION_4_6		0x4006	/* has ee_scaled_cck_delta */
#define AR5K_EEPROM_VERSION_4_7		0x4007

#define AR5K_EEPROM_MODE_11A		0
#define AR5K_EEPROM_MODE_11B		1
#define AR5K_EEPROM_MODE_11G		2

#define AR5K_EEPROM_HDR			AR5K_EEPROM_INFO(2)	/* Header that contains the device caps */
#define AR5K_EEPROM_HDR_11A(_v)		(((_v) >> AR5K_EEPROM_MODE_11A) & 0x1)
#define AR5K_EEPROM_HDR_11B(_v)		(((_v) >> AR5K_EEPROM_MODE_11B) & 0x1)
#define AR5K_EEPROM_HDR_11G(_v)		(((_v) >> AR5K_EEPROM_MODE_11G) & 0x1)
#define AR5K_EEPROM_HDR_T_2GHZ_DIS(_v)	(((_v) >> 3) & 0x1)	/* Disable turbo for 2Ghz (?) */
#define AR5K_EEPROM_HDR_T_5GHZ_DBM(_v)	(((_v) >> 4) & 0x7f)	/* Max turbo power for a/XR mode (eeprom_init) */
#define AR5K_EEPROM_HDR_DEVICE(_v)	(((_v) >> 11) & 0x7)
#define AR5K_EEPROM_HDR_T_5GHZ_DIS(_v)	(((_v) >> 15) & 0x1)	/* Disable turbo for 5Ghz (?) */
#define AR5K_EEPROM_HDR_RFKILL(_v)	(((_v) >> 14) & 0x1)	/* Device has RFKill support */

#define AR5K_EEPROM_RFKILL_GPIO_SEL	0x0000001c
#define AR5K_EEPROM_RFKILL_GPIO_SEL_S	2
#define AR5K_EEPROM_RFKILL_POLARITY	0x00000002
#define AR5K_EEPROM_RFKILL_POLARITY_S	1

/* Newer EEPROMs are using a different offset */
#define AR5K_EEPROM_OFF(_v, _v3_0, _v3_3) \
	(((_v) >= AR5K_EEPROM_VERSION_3_3) ? _v3_3 : _v3_0)

#define AR5K_EEPROM_ANT_GAIN(_v)	AR5K_EEPROM_OFF(_v, 0x00c4, 0x00c3)
#define AR5K_EEPROM_ANT_GAIN_5GHZ(_v)	((int8_t)(((_v) >> 8) & 0xff))
#define AR5K_EEPROM_ANT_GAIN_2GHZ(_v)	((int8_t)((_v) & 0xff))

/* calibration settings */
#define AR5K_EEPROM_MODES_11A(_v)	AR5K_EEPROM_OFF(_v, 0x00c5, 0x00d4)
#define AR5K_EEPROM_MODES_11B(_v)	AR5K_EEPROM_OFF(_v, 0x00d0, 0x00f2)
#define AR5K_EEPROM_MODES_11G(_v)	AR5K_EEPROM_OFF(_v, 0x00da, 0x010d)
#define AR5K_EEPROM_CTL(_v)		AR5K_EEPROM_OFF(_v, 0x00e4, 0x0128)	/* Conformance test limits */

/* [3.1 - 3.3] */
#define AR5K_EEPROM_OBDB0_2GHZ		0x00ec
#define AR5K_EEPROM_OBDB1_2GHZ		0x00ed

/* Misc values available since EEPROM 4.0 */
#define AR5K_EEPROM_MISC0		0x00c4
#define AR5K_EEPROM_EARSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_EEMAP(_v)		(((_v) >> 14) & 0x3)
#define AR5K_EEPROM_MISC1		0x00c5
#define AR5K_EEPROM_TARGET_PWRSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_HAS32KHZCRYSTAL(_v)	(((_v) >> 14) & 0x1)


/* Some EEPROM defines */
#define AR5K_EEPROM_EEP_SCALE		100
#define AR5K_EEPROM_EEP_DELTA		10
#define AR5K_EEPROM_N_MODES		3
#define AR5K_EEPROM_N_5GHZ_CHAN		10
#define AR5K_EEPROM_N_2GHZ_CHAN		3
#define AR5K_EEPROM_MAX_CHAN		10
#define AR5K_EEPROM_N_PCDAC		11
#define AR5K_EEPROM_N_TEST_FREQ		8
#define AR5K_EEPROM_N_EDGES		8
#define AR5K_EEPROM_N_INTERCEPTS	11
#define AR5K_EEPROM_FREQ_M(_v)		AR5K_EEPROM_OFF(_v, 0x7f, 0xff)
#define AR5K_EEPROM_PCDAC_M		0x3f
#define AR5K_EEPROM_PCDAC_START		1
#define AR5K_EEPROM_PCDAC_STOP		63
#define AR5K_EEPROM_PCDAC_STEP		1
#define AR5K_EEPROM_NON_EDGE_M		0x40
#define AR5K_EEPROM_CHANNEL_POWER	8
#define AR5K_EEPROM_N_OBDB		4
#define AR5K_EEPROM_OBDB_DIS		0xffff
#define AR5K_EEPROM_CHANNEL_DIS		0xff
#define AR5K_EEPROM_SCALE_OC_DELTA(_x)	(((_x) * 2) / 10)
#define AR5K_EEPROM_N_CTLS(_v)		AR5K_EEPROM_OFF(_v, 16, 32)
#define AR5K_EEPROM_MAX_CTLS		32
#define AR5K_EEPROM_N_XPD_PER_CHANNEL	4
#define AR5K_EEPROM_N_XPD0_POINTS	4
#define AR5K_EEPROM_N_XPD3_POINTS	3
#define AR5K_EEPROM_N_INTERCEPT_10_2GHZ	35
#define AR5K_EEPROM_N_INTERCEPT_10_5GHZ	55
#define AR5K_EEPROM_POWER_M		0x3f
#define AR5K_EEPROM_POWER_MIN		0
#define AR5K_EEPROM_POWER_MAX		3150
#define AR5K_EEPROM_POWER_STEP		50
#define AR5K_EEPROM_POWER_TABLE_SIZE	64
#define AR5K_EEPROM_N_POWER_LOC_11B	4
#define AR5K_EEPROM_N_POWER_LOC_11G	6
#define AR5K_EEPROM_I_GAIN		10
#define AR5K_EEPROM_CCK_OFDM_DELTA	15
#define AR5K_EEPROM_N_IQ_CAL		2

#define AR5K_EEPROM_READ(_o, _v) do {			\
	ret = ath5k_hw_eeprom_read(ah, (_o), &(_v));	\
	if (ret)					\
		return ret;				\
} while (0)

#define AR5K_EEPROM_READ_HDR(_o, _v)					\
	AR5K_EEPROM_READ(_o, ah->ah_capabilities.cap_eeprom._v);	\

/* Struct to hold EEPROM calibration data */
struct ath5k_eeprom_info {
	u16	ee_magic;
	u16	ee_protect;
	u16	ee_regdomain;
	u16	ee_version;
	u16	ee_header;
	u16	ee_ant_gain;
	u16	ee_misc0;
	u16	ee_misc1;
	u16	ee_cck_ofdm_gain_delta;
	u16	ee_cck_ofdm_power_delta;
	u16	ee_scaled_cck_delta;

	/* Used for tx thermal adjustment (eeprom_init, rfregs) */
	u16	ee_tx_clip;
	u16	ee_pwd_84;
	u16	ee_pwd_90;
	u16	ee_gain_select;

	/* RF Calibration settings (reset, rfregs) */
	u16	ee_i_cal[AR5K_EEPROM_N_MODES];
	u16	ee_q_cal[AR5K_EEPROM_N_MODES];
	u16	ee_fixed_bias[AR5K_EEPROM_N_MODES];
	u16	ee_turbo_max_power[AR5K_EEPROM_N_MODES];
	u16	ee_xr_power[AR5K_EEPROM_N_MODES];
	u16	ee_switch_settling[AR5K_EEPROM_N_MODES];
	u16	ee_ant_tx_rx[AR5K_EEPROM_N_MODES];
	u16	ee_ant_control[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_PCDAC];
	u16	ee_ob[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
	u16	ee_db[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
	u16	ee_tx_end2xlna_enable[AR5K_EEPROM_N_MODES];
	u16	ee_tx_end2xpa_disable[AR5K_EEPROM_N_MODES];
	u16	ee_tx_frm2xpa_enable[AR5K_EEPROM_N_MODES];
	u16	ee_thr_62[AR5K_EEPROM_N_MODES];
	u16	ee_xlna_gain[AR5K_EEPROM_N_MODES];
	u16	ee_xpd[AR5K_EEPROM_N_MODES];
	u16	ee_x_gain[AR5K_EEPROM_N_MODES];
	u16	ee_i_gain[AR5K_EEPROM_N_MODES];
	u16	ee_margin_tx_rx[AR5K_EEPROM_N_MODES];

	/* Unused */
	u16	ee_false_detect[AR5K_EEPROM_N_MODES];
	u16	ee_cal_pier[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_2GHZ_CHAN];
	u16	ee_channel[AR5K_EEPROM_N_MODES][AR5K_EEPROM_MAX_CHAN]; /*empty*/

	/* Conformance test limits (Unused) */
	u16	ee_ctls;
	u16	ee_ctl[AR5K_EEPROM_MAX_CTLS];

	/* Noise Floor Calibration settings */
	s16	ee_noise_floor_thr[AR5K_EEPROM_N_MODES];
	s8	ee_adc_desired_size[AR5K_EEPROM_N_MODES];
	s8	ee_pga_desired_size[AR5K_EEPROM_N_MODES];
};
