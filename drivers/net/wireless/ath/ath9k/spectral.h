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

#define SPECTRAL_HT20_NUM_BINS		56

/* WARNING: don't actually use this struct! MAC may vary the amount of
 * data by -1/+2. This struct is for reference only.
 */
struct ath_ht20_fft_packet {
	u8 data[SPECTRAL_HT20_NUM_BINS];
	struct ath_ht20_mag_info mag_info;
	struct ath_radar_info radar_info;
} __packed;

#define SPECTRAL_HT20_TOTAL_DATA_LEN	(sizeof(struct ath_ht20_fft_packet))

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

#define SPECTRAL_HT20_40_NUM_BINS		128

/* WARNING: don't actually use this struct! MAC may vary the amount of
 * data. This struct is for reference only.
 */
struct ath_ht20_40_fft_packet {
	u8 data[SPECTRAL_HT20_40_NUM_BINS];
	struct ath_ht20_40_mag_info mag_info;
	struct ath_radar_info radar_info;
} __packed;


#define SPECTRAL_HT20_40_TOTAL_DATA_LEN	(sizeof(struct ath_ht20_40_fft_packet))

/* grabs the max magnitude from the all/upper/lower bins */
static inline u16 spectral_max_magnitude(u8 *bins)
{
	return (bins[0] & 0xc0) >> 6 |
	       (bins[1] & 0xff) << 2 |
	       (bins[2] & 0x03) << 10;
}

/* return the max magnitude from the all/upper/lower bins */
static inline u8 spectral_max_index(u8 *bins)
{
	s8 m = (bins[2] & 0xfc) >> 2;

	/* TODO: this still doesn't always report the right values ... */
	if (m > 32)
		m |= 0xe0;
	else
		m &= ~0xe0;

	return m + 29;
}

/* return the bitmap weight from the all/upper/lower bins */
static inline u8 spectral_bitmap_weight(u8 *bins)
{
	return bins[0] & 0x3f;
}

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

void ath9k_spectral_init_debug(struct ath_softc *sc);
void ath9k_spectral_deinit_debug(struct ath_softc *sc);

void ath9k_spectral_scan_trigger(struct ieee80211_hw *hw);
int ath9k_spectral_scan_config(struct ieee80211_hw *hw,
			       enum spectral_mode spectral_mode);

#ifdef CONFIG_ATH9K_DEBUGFS
int ath_process_fft(struct ath_softc *sc, struct ieee80211_hdr *hdr,
		    struct ath_rx_status *rs, u64 tsf);
#else
static inline int ath_process_fft(struct ath_softc *sc,
				  struct ieee80211_hdr *hdr,
				  struct ath_rx_status *rs, u64 tsf)
{
	return 0;
}
#endif /* CONFIG_ATH9K_DEBUGFS */

#endif /* SPECTRAL_H */
