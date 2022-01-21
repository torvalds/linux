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

#ifndef SPECTRAL_COMMON_H
#define SPECTRAL_COMMON_H

#define SPECTRAL_HT20_NUM_BINS		56
#define SPECTRAL_HT20_40_NUM_BINS		128

/* TODO: could possibly be 512, but no samples this large
 * could be acquired so far.
 */
#define SPECTRAL_ATH10K_MAX_NUM_BINS		256

/* FFT sample format given to userspace via debugfs.
 *
 * Please keep the type/length at the front position and change
 * other fields after adding another sample type
 *
 * TODO: this might need rework when switching to nl80211-based
 * interface.
 */
enum ath_fft_sample_type {
	ATH_FFT_SAMPLE_HT20 = 1,
	ATH_FFT_SAMPLE_HT20_40,
	ATH_FFT_SAMPLE_ATH10K,
	ATH_FFT_SAMPLE_ATH11K
};

struct fft_sample_tlv {
	u8 type;	/* see ath_fft_sample */
	__be16 length;
	/* type dependent data follows */
} __packed;

struct fft_sample_ht20 {
	struct fft_sample_tlv tlv;

	u8 max_exp;

	__be16 freq;
	s8 rssi;
	s8 noise;

	__be16 max_magnitude;
	u8 max_index;
	u8 bitmap_weight;

	__be64 tsf;

	u8 data[SPECTRAL_HT20_NUM_BINS];
} __packed;

struct fft_sample_ht20_40 {
	struct fft_sample_tlv tlv;

	u8 channel_type;
	__be16 freq;

	s8 lower_rssi;
	s8 upper_rssi;

	__be64 tsf;

	s8 lower_noise;
	s8 upper_noise;

	__be16 lower_max_magnitude;
	__be16 upper_max_magnitude;

	u8 lower_max_index;
	u8 upper_max_index;

	u8 lower_bitmap_weight;
	u8 upper_bitmap_weight;

	u8 max_exp;

	u8 data[SPECTRAL_HT20_40_NUM_BINS];
} __packed;

struct fft_sample_ath10k {
	struct fft_sample_tlv tlv;
	u8 chan_width_mhz;
	__be16 freq1;
	__be16 freq2;
	__be16 noise;
	__be16 max_magnitude;
	__be16 total_gain_db;
	__be16 base_pwr_db;
	__be64 tsf;
	s8 max_index;
	u8 rssi;
	u8 relpwr_db;
	u8 avgpwr_db;
	u8 max_exp;

	u8 data[0];
} __packed;

struct fft_sample_ath11k {
	struct fft_sample_tlv tlv;
	u8 chan_width_mhz;
	s8 max_index;
	u8 max_exp;
	__be16 freq1;
	__be16 freq2;
	__be16 max_magnitude;
	__be16 rssi;
	__be32 tsf;
	__be32 noise;

	u8 data[0];
} __packed;

#endif /* SPECTRAL_COMMON_H */
