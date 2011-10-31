/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCM_RATE_H_
#define _BRCM_RATE_H_

#include "types.h"
#include "d11.h"

extern const u8 rate_info[];
extern const struct brcms_c_rateset cck_ofdm_mimo_rates;
extern const struct brcms_c_rateset ofdm_mimo_rates;
extern const struct brcms_c_rateset cck_ofdm_rates;
extern const struct brcms_c_rateset ofdm_rates;
extern const struct brcms_c_rateset cck_rates;
extern const struct brcms_c_rateset gphy_legacy_rates;
extern const struct brcms_c_rateset rate_limit_1_2;

struct brcms_mcs_info {
	/* phy rate in kbps [20Mhz] */
	u32 phy_rate_20;
	/* phy rate in kbps [40Mhz] */
	u32 phy_rate_40;
	/* phy rate in kbps [20Mhz] with SGI */
	u32 phy_rate_20_sgi;
	/* phy rate in kbps [40Mhz] with SGI */
	u32 phy_rate_40_sgi;
	/* phy ctl byte 3, code rate, modulation type, # of streams */
	u8 tx_phy_ctl3;
	/* matching legacy ofdm rate in 500bkps */
	u8 leg_ofdm;
};

#define BRCMS_MAXMCS	32	/* max valid mcs index */
#define MCS_TABLE_SIZE	33	/* Number of mcs entries in the table */
extern const struct brcms_mcs_info mcs_table[];

#define MCS_TXS_MASK	0xc0	/* num tx streams - 1 bit mask */
#define MCS_TXS_SHIFT	6	/* num tx streams - 1 bit shift */

/* returns num tx streams - 1 */
static inline u8 mcs_2_txstreams(u8 mcs)
{
	return (mcs_table[mcs].tx_phy_ctl3 & MCS_TXS_MASK) >> MCS_TXS_SHIFT;
}

static inline uint mcs_2_rate(u8 mcs, bool is40, bool sgi)
{
	if (sgi) {
		if (is40)
			return mcs_table[mcs].phy_rate_40_sgi;
		return mcs_table[mcs].phy_rate_20_sgi;
	}
	if (is40)
		return mcs_table[mcs].phy_rate_40;

	return mcs_table[mcs].phy_rate_20;
}

/* Macro to use the rate_info table */
#define	BRCMS_RATE_MASK_FULL 0xff /* Rate value mask with basic rate flag */

/*
 * rate spec : holds rate and mode specific information required to generate a
 * tx frame. Legacy CCK and OFDM information is held in the same manner as was
 * done in the past (in the lower byte) the upper 3 bytes primarily hold MIMO
 * specific information
 */

/* rate spec bit fields */

/* Either 500Kbps units or MIMO MCS idx */
#define RSPEC_RATE_MASK		0x0000007F
/* mimo MCS is stored in RSPEC_RATE_MASK */
#define RSPEC_MIMORATE		0x08000000
/* mimo bw mask */
#define RSPEC_BW_MASK		0x00000700
/* mimo bw shift */
#define RSPEC_BW_SHIFT		8
/* mimo Space/Time/Frequency mode mask */
#define RSPEC_STF_MASK		0x00003800
/* mimo Space/Time/Frequency mode shift */
#define RSPEC_STF_SHIFT		11
/* mimo coding type mask */
#define RSPEC_CT_MASK		0x0000C000
/* mimo coding type shift */
#define RSPEC_CT_SHIFT		14
/* mimo num STC streams per PLCP defn. */
#define RSPEC_STC_MASK		0x00300000
/* mimo num STC streams per PLCP defn. */
#define RSPEC_STC_SHIFT		20
/* mimo bit indicates adv coding in use */
#define RSPEC_LDPC_CODING	0x00400000
/* mimo bit indicates short GI in use */
#define RSPEC_SHORT_GI		0x00800000
/* bit indicates override both rate & mode */
#define RSPEC_OVERRIDE		0x80000000
/* bit indicates override rate only */
#define RSPEC_OVERRIDE_MCS_ONLY 0x40000000

static inline bool rspec_active(u32 rspec)
{
	return rspec & (RSPEC_RATE_MASK | RSPEC_MIMORATE);
}

static inline u8 rspec_phytxbyte2(u32 rspec)
{
	return (rspec & 0xff00) >> 8;
}

static inline u32 rspec_get_bw(u32 rspec)
{
	return (rspec & RSPEC_BW_MASK) >> RSPEC_BW_SHIFT;
}

static inline bool rspec_issgi(u32 rspec)
{
	return (rspec & RSPEC_SHORT_GI) == RSPEC_SHORT_GI;
}

static inline bool rspec_is40mhz(u32 rspec)
{
	u32 bw = rspec_get_bw(rspec);

	return bw == PHY_TXC1_BW_40MHZ || bw == PHY_TXC1_BW_40MHZ_DUP;
}

static inline uint rspec2rate(u32 rspec)
{
	if (rspec & RSPEC_MIMORATE)
		return mcs_2_rate(rspec & RSPEC_RATE_MASK, rspec_is40mhz(rspec),
				  rspec_issgi(rspec));
	return rspec & RSPEC_RATE_MASK;
}

static inline u8 rspec_mimoplcp3(u32 rspec)
{
	return (rspec & 0xf00000) >> 16;
}

static inline bool plcp3_issgi(u8 plcp)
{
	return (plcp & (RSPEC_SHORT_GI >> 16)) != 0;
}

static inline uint rspec_stc(u32 rspec)
{
	return (rspec & RSPEC_STC_MASK) >> RSPEC_STC_SHIFT;
}

static inline uint rspec_stf(u32 rspec)
{
	return (rspec & RSPEC_STF_MASK) >> RSPEC_STF_SHIFT;
}

static inline bool is_mcs_rate(u32 ratespec)
{
	return (ratespec & RSPEC_MIMORATE) != 0;
}

static inline bool is_ofdm_rate(u32 ratespec)
{
	return !is_mcs_rate(ratespec) &&
	       (rate_info[ratespec & RSPEC_RATE_MASK] & BRCMS_RATE_FLAG);
}

static inline bool is_cck_rate(u32 ratespec)
{
	u32 rate = (ratespec & BRCMS_RATE_MASK);

	return !is_mcs_rate(ratespec) && (
			rate == BRCM_RATE_1M || rate == BRCM_RATE_2M ||
			rate == BRCM_RATE_5M5 || rate == BRCM_RATE_11M);
}

static inline bool is_single_stream(u8 mcs)
{
	return mcs <= HIGHEST_SINGLE_STREAM_MCS || mcs == 32;
}

static inline u8 cck_rspec(u8 cck)
{
	return cck & RSPEC_RATE_MASK;
}

/* Convert encoded rate value in plcp header to numerical rates in 500 KHz
 * increments */
extern const u8 ofdm_rate_lookup[];

static inline u8 ofdm_phy2mac_rate(u8 rlpt)
{
	return ofdm_rate_lookup[rlpt & 0x7];
}

static inline u8 cck_phy2mac_rate(u8 signal)
{
	return signal/5;
}

/* Rates specified in brcms_c_rateset_filter() */
#define BRCMS_RATES_CCK_OFDM	0
#define BRCMS_RATES_CCK		1
#define BRCMS_RATES_OFDM		2

/* sanitize, and sort a rateset with the basic bit(s) preserved, validate
 * rateset */
extern bool
brcms_c_rate_hwrs_filter_sort_validate(struct brcms_c_rateset *rs,
				       const struct brcms_c_rateset *hw_rs,
				       bool check_brate, u8 txstreams);
/* copy rateset src to dst as-is (no masking or sorting) */
extern void brcms_c_rateset_copy(const struct brcms_c_rateset *src,
			     struct brcms_c_rateset *dst);

/* would be nice to have these documented ... */
extern u32 brcms_c_compute_rspec(struct d11rxhdr *rxh, u8 *plcp);

extern void brcms_c_rateset_filter(struct brcms_c_rateset *src,
	struct brcms_c_rateset *dst, bool basic_only, u8 rates, uint xmask,
	bool mcsallow);

extern void
brcms_c_rateset_default(struct brcms_c_rateset *rs_tgt,
			const struct brcms_c_rateset *rs_hw, uint phy_type,
			int bandtype, bool cck_only, uint rate_mask,
			bool mcsallow, u8 bw, u8 txstreams);

extern s16 brcms_c_rate_legacy_phyctl(uint rate);

extern void brcms_c_rateset_mcs_upd(struct brcms_c_rateset *rs, u8 txstreams);
extern void brcms_c_rateset_mcs_clear(struct brcms_c_rateset *rateset);
extern void brcms_c_rateset_mcs_build(struct brcms_c_rateset *rateset,
				      u8 txstreams);
extern void brcms_c_rateset_bw_mcs_filter(struct brcms_c_rateset *rateset,
					  u8 bw);

#endif				/* _BRCM_RATE_H_ */
