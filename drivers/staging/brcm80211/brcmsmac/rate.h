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

extern const u8 rate_info[];
extern const struct brcms_rateset cck_ofdm_mimo_rates;
extern const struct brcms_rateset ofdm_mimo_rates;
extern const struct brcms_rateset cck_ofdm_rates;
extern const struct brcms_rateset ofdm_rates;
extern const struct brcms_rateset cck_rates;
extern const struct brcms_rateset gphy_legacy_rates;
extern const struct brcms_rateset wlc_lrs_rates;
extern const struct brcms_rateset rate_limit_1_2;

struct brcms_mcs_info {
	u32 phy_rate_20;	/* phy rate in kbps [20Mhz] */
	u32 phy_rate_40;	/* phy rate in kbps [40Mhz] */
	u32 phy_rate_20_sgi;	/* phy rate in kbps [20Mhz] with SGI */
	u32 phy_rate_40_sgi;	/* phy rate in kbps [40Mhz] with SGI */
	u8 tx_phy_ctl3;	/* phy ctl byte 3, code rate, modulation type, # of streams */
	u8 leg_ofdm;		/* matching legacy ofdm rate in 500bkps */
};

#define BRCMS_MAXMCS	32	/* max valid mcs index */
#define MCS_TABLE_SIZE	33	/* Number of mcs entries in the table */
extern const struct brcms_mcs_info mcs_table[];

#define MCS_INVALID	0xFF
#define MCS_CR_MASK	0x07	/* Code Rate bit mask */
#define MCS_MOD_MASK	0x38	/* Modulation bit shift */
#define MCS_MOD_SHIFT	3	/* MOdulation bit shift */
#define MCS_TXS_MASK	0xc0	/* num tx streams - 1 bit mask */
#define MCS_TXS_SHIFT	6	/* num tx streams - 1 bit shift */
#define MCS_CR(_mcs)	(mcs_table[_mcs].tx_phy_ctl3 & MCS_CR_MASK)
#define MCS_MOD(_mcs)	((mcs_table[_mcs].tx_phy_ctl3 & MCS_MOD_MASK) >> MCS_MOD_SHIFT)
#define MCS_TXS(_mcs)	((mcs_table[_mcs].tx_phy_ctl3 & MCS_TXS_MASK) >> MCS_TXS_SHIFT)
#define MCS_RATE(_mcs, _is40, _sgi)	(_sgi ? \
	(_is40 ? mcs_table[_mcs].phy_rate_40_sgi : mcs_table[_mcs].phy_rate_20_sgi) : \
	(_is40 ? mcs_table[_mcs].phy_rate_40 : mcs_table[_mcs].phy_rate_20))
#define VALID_MCS(_mcs)	((_mcs < MCS_TABLE_SIZE))

/* Macro to use the rate_info table */
#define	BRCMS_RATE_MASK_FULL 0xff /* Rate value mask with basic rate flag */

/* convert 500kbps to bps */
#define BRCMS_RATE_500K_TO_BPS(rate)	((rate) * 500000)

/* rate spec : holds rate and mode specific information required to generate a tx frame. */
/* Legacy CCK and OFDM information is held in the same manner as was done in the past    */
/* (in the lower byte) the upper 3 bytes primarily hold MIMO specific information        */

/* rate spec bit fields */
#define RSPEC_RATE_MASK		0x0000007F	/* Either 500Kbps units or MIMO MCS idx */
#define RSPEC_MIMORATE		0x08000000	/* mimo MCS is stored in RSPEC_RATE_MASK */
#define RSPEC_BW_MASK		0x00000700	/* mimo bw mask */
#define RSPEC_BW_SHIFT		8	/* mimo bw shift */
#define RSPEC_STF_MASK		0x00003800	/* mimo Space/Time/Frequency mode mask */
#define RSPEC_STF_SHIFT		11	/* mimo Space/Time/Frequency mode shift */
#define RSPEC_CT_MASK		0x0000C000	/* mimo coding type mask */
#define RSPEC_CT_SHIFT		14	/* mimo coding type shift */
#define RSPEC_STC_MASK		0x00300000	/* mimo num STC streams per PLCP defn. */
#define RSPEC_STC_SHIFT		20	/* mimo num STC streams per PLCP defn. */
#define RSPEC_LDPC_CODING	0x00400000	/* mimo bit indicates adv coding in use */
#define RSPEC_SHORT_GI		0x00800000	/* mimo bit indicates short GI in use */
#define RSPEC_OVERRIDE		0x80000000	/* bit indicates override both rate & mode */
#define RSPEC_OVERRIDE_MCS_ONLY 0x40000000	/* bit indicates override rate only */

#define BRCMS_HTPHY		127	/* HT PHY Membership */

#define RSPEC_ACTIVE(rspec)	(rspec & (RSPEC_RATE_MASK | RSPEC_MIMORATE))
#define RSPEC2RATE(rspec)	((rspec & RSPEC_MIMORATE) ? \
	MCS_RATE((rspec & RSPEC_RATE_MASK), RSPEC_IS40MHZ(rspec), RSPEC_ISSGI(rspec)) : \
	(rspec & RSPEC_RATE_MASK))
/* return rate in unit of 500Kbps -- for internal use in wlc_rate_sel.c */
#define RSPEC2RATE500K(rspec)	((rspec & RSPEC_MIMORATE) ? \
		MCS_RATE((rspec & RSPEC_RATE_MASK), state->is40bw, RSPEC_ISSGI(rspec))/500 : \
		(rspec & RSPEC_RATE_MASK))
#define CRSPEC2RATE500K(rspec)	((rspec & RSPEC_MIMORATE) ? \
		MCS_RATE((rspec & RSPEC_RATE_MASK), RSPEC_IS40MHZ(rspec), RSPEC_ISSGI(rspec))/500 :\
		(rspec & RSPEC_RATE_MASK))

#define RSPEC2KBPS(rspec)	(IS_MCS(rspec) ? RSPEC2RATE(rspec) : RSPEC2RATE(rspec)*500)
#define RSPEC_PHYTXBYTE2(rspec)	((rspec & 0xff00) >> 8)
#define RSPEC_GET_BW(rspec)	((rspec & RSPEC_BW_MASK) >> RSPEC_BW_SHIFT)
#define RSPEC_IS40MHZ(rspec)	((((rspec & RSPEC_BW_MASK) >> RSPEC_BW_SHIFT) == \
				PHY_TXC1_BW_40MHZ) || (((rspec & RSPEC_BW_MASK) >> \
				RSPEC_BW_SHIFT) == PHY_TXC1_BW_40MHZ_DUP))
#define RSPEC_ISSGI(rspec)	((rspec & RSPEC_SHORT_GI) == RSPEC_SHORT_GI)
#define RSPEC_MIMOPLCP3(rspec)	((rspec & 0xf00000) >> 16)
#define PLCP3_ISSGI(plcp)	(plcp & (RSPEC_SHORT_GI >> 16))
#define RSPEC_STC(rspec)	((rspec & RSPEC_STC_MASK) >> RSPEC_STC_SHIFT)
#define RSPEC_STF(rspec)	((rspec & RSPEC_STF_MASK) >> RSPEC_STF_SHIFT)
#define PLCP3_ISSTBC(plcp)	((plcp & (RSPEC_STC_MASK) >> 16) == 0x10)
#define PLCP3_STC_MASK          0x30
#define PLCP3_STC_SHIFT         4

/* Rate info table; takes a legacy rate or ratespec_t */
#define	IS_MCS(r)	(r & RSPEC_MIMORATE)
#define	IS_OFDM(r)	(!IS_MCS(r) && (rate_info[(r) & RSPEC_RATE_MASK] & \
					BRCMS_RATE_FLAG))
#define	IS_CCK(r)	(!IS_MCS(r) && ( \
			 ((r) & BRCMS_RATE_MASK) == BRCM_RATE_1M || \
			 ((r) & BRCMS_RATE_MASK) == BRCM_RATE_2M || \
			 ((r) & BRCMS_RATE_MASK) == BRCM_RATE_5M5 || \
			 ((r) & BRCMS_RATE_MASK) == BRCM_RATE_11M))
#define IS_SINGLE_STREAM(mcs)	(((mcs) <= HIGHEST_SINGLE_STREAM_MCS) || ((mcs) == 32))
#define CCK_RSPEC(cck)		((cck) & RSPEC_RATE_MASK)
#define OFDM_RSPEC(ofdm)	(((ofdm) & RSPEC_RATE_MASK) |\
	(PHY_TXC1_MODE_CDD << RSPEC_STF_SHIFT))
#define LEGACY_RSPEC(rate)	(IS_CCK(rate) ? CCK_RSPEC(rate) : OFDM_RSPEC(rate))

#define MCS_RSPEC(mcs)		(((mcs) & RSPEC_RATE_MASK) | RSPEC_MIMORATE | \
	(IS_SINGLE_STREAM(mcs) ? (PHY_TXC1_MODE_CDD << RSPEC_STF_SHIFT) : \
	(PHY_TXC1_MODE_SDM << RSPEC_STF_SHIFT)))

/* Convert encoded rate value in plcp header to numerical rates in 500 KHz increments */
extern const u8 ofdm_rate_lookup[];
#define OFDM_PHY2MAC_RATE(rlpt)		(ofdm_rate_lookup[rlpt & 0x7])
#define CCK_PHY2MAC_RATE(signal)	(signal/5)

/* Rates specified in brcms_c_rateset_filter() */
#define BRCMS_RATES_CCK_OFDM	0
#define BRCMS_RATES_CCK		1
#define BRCMS_RATES_OFDM		2

/* sanitize, and sort a rateset with the basic bit(s) preserved, validate rateset */
extern bool
brcms_c_rate_hwrs_filter_sort_validate(struct brcms_rateset *rs,
				       const struct brcms_rateset *hw_rs,
				       bool check_brate, u8 txstreams);
/* copy rateset src to dst as-is (no masking or sorting) */
extern void brcms_c_rateset_copy(const struct brcms_rateset *src,
			     struct brcms_rateset *dst);

/* would be nice to have these documented ... */
extern ratespec_t brcms_c_compute_rspec(struct d11rxhdr *rxh, u8 *plcp);

extern void brcms_c_rateset_filter(struct brcms_rateset *src,
	struct brcms_rateset *dst, bool basic_only, u8 rates, uint xmask,
	bool mcsallow);

extern void
brcms_c_rateset_default(struct brcms_rateset *rs_tgt,
			const struct brcms_rateset *rs_hw, uint phy_type,
			int bandtype, bool cck_only, uint rate_mask,
			bool mcsallow, u8 bw, u8 txstreams);

extern s16 brcms_c_rate_legacy_phyctl(uint rate);

extern void brcms_c_rateset_mcs_upd(struct brcms_rateset *rs, u8 txstreams);
extern void brcms_c_rateset_mcs_clear(struct brcms_rateset *rateset);
extern void brcms_c_rateset_mcs_build(struct brcms_rateset *rateset,
				      u8 txstreams);
extern void brcms_c_rateset_bw_mcs_filter(struct brcms_rateset *rateset, u8 bw);

#endif				/* _BRCM_RATE_H_ */
