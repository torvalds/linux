/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 */

#ifndef SPECTRAL_H
#define SPECTRAL_H

#include "../spectral_common.h"

/* enum spectral_mode:
 *
 * @SPECTRAL_DISABLED: spectral mode is disabled
 * @SPECTRAL_BACKGROUND: hardware sends samples when it is not busy with
 *	something else.
 * @SPECTRAL_MANUAL: spectral scan is enabled, triggering for samples
 *	is performed manually.
 * @SPECTRAL_CHANSCAN: Like manual, but also triggered when changing channels
 *	during a channel scan.
 */
enum spectral_mode {
	SPECTRAL_DISABLED = 0,
	SPECTRAL_BACKGROUND,
	SPECTRAL_MANUAL,
	SPECTRAL_CHANSCAN,
};

#define SPECTRAL_SCAN_BITMASK		0x10
/* Radar info packet format, used for DFS and spectral formats. */
struct ath_radar_info {
	u8 pulse_length_pri;
	u8 pulse_length_ext;
	u8 pulse_bw_info;
} __packed;

/* The HT20 spectral data has 4 bytes of additional information at it's end.
 *
 * [7:0]: all bins {max_magnitude[1:0], bitmap_weight[5:0]}
 * [7:0]: all bins  max_magnitude[9:2]
 * [7:0]: all bins {max_index[5:0], max_magnitude[11:10]}
 * [3:0]: max_exp (shift amount to size max bin to 8-bit unsigned)
 */
struct ath_ht20_mag_info {
	u8 all_bins[3];
	u8 max_exp;
} __packed;

/* WARNING: don't actually use this struct! MAC may vary the amount of
 * data by -1/+2. This struct is for reference only.
 */
struct ath_ht20_fft_packet {
	u8 data[SPECTRAL_HT20_NUM_BINS];
	struct ath_ht20_mag_info mag_info;
	struct ath_radar_info radar_info;
} __packed;

#define SPECTRAL_HT20_TOTAL_DATA_LEN	(sizeof(struct ath_ht20_fft_packet))
#define	SPECTRAL_HT20_SAMPLE_LEN	(sizeof(struct ath_ht20_mag_info) +\
					SPECTRAL_HT20_NUM_BINS)

/* Dynamic 20/40 mode:
 *
 * [7:0]: lower bins {max_magnitude[1:0], bitmap_weight[5:0]}
 * [7:0]: lower bins  max_magnitude[9:2]
 * [7:0]: lower bins {max_index[5:0], max_magnitude[11:10]}
 * [7:0]: upper bins {max_magnitude[1:0], bitmap_weight[5:0]}
 * [7:0]: upper bins  max_magnitude[9:2]
 * [7:0]: upper bins {max_index[5:0], max_magnitude[11:10]}
 * [3:0]: max_exp (shift amount to size max bin to 8-bit unsigned)
 */
struct ath_ht20_40_mag_info {
	u8 lower_bins[3];
	u8 upper_bins[3];
	u8 max_exp;
} __packed;

/* WARNING: don't actually use this struct! MAC may vary the amount of
 * data. This struct is for reference only.
 */
struct ath_ht20_40_fft_packet {
	u8 data[SPECTRAL_HT20_40_NUM_BINS];
	struct ath_ht20_40_mag_info mag_info;
	struct ath_radar_info radar_info;
} __packed;

struct ath_spec_scan_priv {
	struct ath_hw *ah;
	/* relay(fs) channel for spectral scan */
	struct rchan *rfs_chan_spec_scan;
	enum spectral_mode spectral_mode;
	struct ath_spec_scan spec_config;
};

#define SPECTRAL_HT20_40_TOTAL_DATA_LEN	(sizeof(struct ath_ht20_40_fft_packet))
#define	SPECTRAL_HT20_40_SAMPLE_LEN	(sizeof(struct ath_ht20_40_mag_info) +\
					SPECTRAL_HT20_40_NUM_BINS)

#define	SPECTRAL_SAMPLE_MAX_LEN		SPECTRAL_HT20_40_SAMPLE_LEN

/* grabs the max magnitude from the all/upper/lower bins */
static inline u16 spectral_max_magnitude(u8 *bins)
{
	return (bins[0] & 0xc0) >> 6 |
	       (bins[1] & 0xff) << 2 |
	       (bins[2] & 0x03) << 10;
}

/* return the max magnitude from the all/upper/lower bins */
static inline u8 spectral_max_index(u8 *bins, int num_bins)
{
	s8 m = (bins[2] & 0xfc) >> 2;
	u8 zero_idx = num_bins / 2;

	/* It's a 5 bit signed int, remove its sign and use one's
	 * complement interpretation to add the sign back to the 8
	 * bit int
	 */
	if (m & 0x20) {
		m &= ~0x20;
		m |= 0xe0;
	}

	/* Bring the zero point to the beginning
	 * instead of the middle so that we can use
	 * it for array lookup and that we don't deal
	 * with negative values later
	 */
	m += zero_idx;

	/* Sanity check to make sure index is within bounds */
	if (m < 0 || m > num_bins - 1)
		m = 0;

	return m;
}

/* return the bitmap weight from the all/upper/lower bins */
static inline u8 spectral_bitmap_weight(u8 *bins)
{
	return bins[0] & 0x3f;
}

void ath9k_cmn_spectral_init_debug(struct ath_spec_scan_priv *spec_priv, struct dentry *debugfs_phy);
void ath9k_cmn_spectral_deinit_debug(struct ath_spec_scan_priv *spec_priv);

void ath9k_cmn_spectral_scan_trigger(struct ath_common *common,
				 struct ath_spec_scan_priv *spec_priv);
int ath9k_cmn_spectral_scan_config(struct ath_common *common,
			       struct ath_spec_scan_priv *spec_priv,
			       enum spectral_mode spectral_mode);
int ath_cmn_process_fft(struct ath_spec_scan_priv *spec_priv, struct ieee80211_hdr *hdr,
		    struct ath_rx_status *rs, u64 tsf);

#endif /* SPECTRAL_H */
