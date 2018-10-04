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

#include <linux/relay.h>
#include <linux/random.h>
#include "ath9k.h"

static s8 fix_rssi_inv_only(u8 rssi_val)
{
	if (rssi_val == 128)
		rssi_val = 0;
	return (s8) rssi_val;
}

static void ath_debug_send_fft_sample(struct ath_spec_scan_priv *spec_priv,
				      struct fft_sample_tlv *fft_sample_tlv)
{
	int length;
	if (!spec_priv->rfs_chan_spec_scan)
		return;

	length = __be16_to_cpu(fft_sample_tlv->length) +
		 sizeof(*fft_sample_tlv);
	relay_write(spec_priv->rfs_chan_spec_scan, fft_sample_tlv, length);
}

typedef int (ath_cmn_fft_idx_validator) (u8 *sample_end, int bytes_read);

static int
ath_cmn_max_idx_verify_ht20_fft(u8 *sample_end, int bytes_read)
{
	struct ath_ht20_mag_info *mag_info;
	u8 *sample;
	u16 max_magnitude;
	u8 max_index;
	u8 max_exp;

	/* Sanity check so that we don't read outside the read
	 * buffer
	 */
	if (bytes_read < SPECTRAL_HT20_SAMPLE_LEN - 1)
		return -1;

	mag_info = (struct ath_ht20_mag_info *) (sample_end -
				sizeof(struct ath_ht20_mag_info) + 1);

	sample = sample_end - SPECTRAL_HT20_SAMPLE_LEN + 1;

	max_index = spectral_max_index_ht20(mag_info->all_bins);
	max_magnitude = spectral_max_magnitude(mag_info->all_bins);

	max_exp = mag_info->max_exp & 0xf;

	/* Don't try to read something outside the read buffer
	 * in case of a missing byte (so bins[0] will be outside
	 * the read buffer)
	 */
	if (bytes_read < SPECTRAL_HT20_SAMPLE_LEN && max_index < 1)
		return -1;

	if ((sample[max_index] & 0xf8) != ((max_magnitude >> max_exp) & 0xf8))
		return -1;
	else
		return 0;
}

static int
ath_cmn_max_idx_verify_ht20_40_fft(u8 *sample_end, int bytes_read)
{
	struct ath_ht20_40_mag_info *mag_info;
	u8 *sample;
	u16 lower_mag, upper_mag;
	u8 lower_max_index, upper_max_index;
	u8 max_exp;
	int dc_pos = SPECTRAL_HT20_40_NUM_BINS / 2;

	/* Sanity check so that we don't read outside the read
	 * buffer
	 */
	if (bytes_read < SPECTRAL_HT20_40_SAMPLE_LEN - 1)
		return -1;

	mag_info = (struct ath_ht20_40_mag_info *) (sample_end -
				sizeof(struct ath_ht20_40_mag_info) + 1);

	sample = sample_end - SPECTRAL_HT20_40_SAMPLE_LEN + 1;

	lower_mag = spectral_max_magnitude(mag_info->lower_bins);
	lower_max_index = spectral_max_index_ht40(mag_info->lower_bins);

	upper_mag = spectral_max_magnitude(mag_info->upper_bins);
	upper_max_index = spectral_max_index_ht40(mag_info->upper_bins);

	max_exp = mag_info->max_exp & 0xf;

	/* Don't try to read something outside the read buffer
	 * in case of a missing byte (so bins[0] will be outside
	 * the read buffer)
	 */
	if (bytes_read < SPECTRAL_HT20_40_SAMPLE_LEN &&
	   ((upper_max_index < 1) || (lower_max_index < 1)))
		return -1;

	if (((sample[upper_max_index + dc_pos] & 0xf8) !=
	     ((upper_mag >> max_exp) & 0xf8)) ||
	    ((sample[lower_max_index] & 0xf8) !=
	     ((lower_mag >> max_exp) & 0xf8)))
		return -1;
	else
		return 0;
}

typedef int (ath_cmn_fft_sample_handler) (struct ath_rx_status *rs,
			struct ath_spec_scan_priv *spec_priv,
			u8 *sample_buf, u64 tsf, u16 freq, int chan_type);

static int
ath_cmn_process_ht20_fft(struct ath_rx_status *rs,
			struct ath_spec_scan_priv *spec_priv,
			u8 *sample_buf,
			u64 tsf, u16 freq, int chan_type)
{
	struct fft_sample_ht20 fft_sample_20;
	struct ath_common *common = ath9k_hw_common(spec_priv->ah);
	struct ath_hw *ah = spec_priv->ah;
	struct ath_ht20_mag_info *mag_info;
	struct fft_sample_tlv *tlv;
	int i = 0;
	int ret = 0;
	int dc_pos = SPECTRAL_HT20_NUM_BINS / 2;
	u16 magnitude, tmp_mag, length;
	u8 max_index, bitmap_w, max_exp;

	length = sizeof(fft_sample_20) - sizeof(struct fft_sample_tlv);
	fft_sample_20.tlv.type = ATH_FFT_SAMPLE_HT20;
	fft_sample_20.tlv.length = __cpu_to_be16(length);
	fft_sample_20.freq = __cpu_to_be16(freq);
	fft_sample_20.rssi = fix_rssi_inv_only(rs->rs_rssi_ctl[0]);
	fft_sample_20.noise = ah->noise;

	mag_info = (struct ath_ht20_mag_info *) (sample_buf +
					SPECTRAL_HT20_NUM_BINS);

	magnitude = spectral_max_magnitude(mag_info->all_bins);
	fft_sample_20.max_magnitude = __cpu_to_be16(magnitude);

	max_index = spectral_max_index_ht20(mag_info->all_bins);
	fft_sample_20.max_index = max_index;

	bitmap_w = spectral_bitmap_weight(mag_info->all_bins);
	fft_sample_20.bitmap_weight = bitmap_w;

	max_exp = mag_info->max_exp & 0xf;
	fft_sample_20.max_exp = max_exp;

	fft_sample_20.tsf = __cpu_to_be64(tsf);

	memcpy(fft_sample_20.data, sample_buf, SPECTRAL_HT20_NUM_BINS);

	ath_dbg(common, SPECTRAL_SCAN, "FFT HT20 frame: max mag 0x%X,"
					"max_mag_idx %i\n",
					magnitude >> max_exp,
					max_index);

	if ((fft_sample_20.data[max_index] & 0xf8) !=
	    ((magnitude >> max_exp) & 0xf8)) {
		ath_dbg(common, SPECTRAL_SCAN, "Magnitude mismatch !\n");
		ret = -1;
	}

	/* DC value (value in the middle) is the blind spot of the spectral
	 * sample and invalid, interpolate it.
	 */
	fft_sample_20.data[dc_pos] = (fft_sample_20.data[dc_pos + 1] +
					fft_sample_20.data[dc_pos - 1]) / 2;

	/* Check if the maximum magnitude is indeed maximum,
	 * also if the maximum value was at dc_pos, calculate
	 * a new one (since value at dc_pos is invalid).
	 */
	if (max_index == dc_pos) {
		tmp_mag = 0;
		for (i = 0; i < dc_pos; i++) {
			if (fft_sample_20.data[i] > tmp_mag) {
				tmp_mag = fft_sample_20.data[i];
				fft_sample_20.max_index = i;
			}
		}

		magnitude = tmp_mag << max_exp;
		fft_sample_20.max_magnitude = __cpu_to_be16(magnitude);

		ath_dbg(common, SPECTRAL_SCAN,
			"Calculated new lower max 0x%X at %i\n",
			tmp_mag, fft_sample_20.max_index);
	} else
	for (i = 0; i < SPECTRAL_HT20_NUM_BINS; i++) {
		if (fft_sample_20.data[i] == (magnitude >> max_exp))
			ath_dbg(common, SPECTRAL_SCAN,
				"Got max: 0x%X at index %i\n",
				fft_sample_20.data[i], i);

		if (fft_sample_20.data[i] > (magnitude >> max_exp)) {
			ath_dbg(common, SPECTRAL_SCAN,
				"Got bin %i greater than max: 0x%X\n",
				i, fft_sample_20.data[i]);
			ret = -1;
		}
	}

	if (ret < 0)
		return ret;

	tlv = (struct fft_sample_tlv *)&fft_sample_20;

	ath_debug_send_fft_sample(spec_priv, tlv);

	return 0;
}

static int
ath_cmn_process_ht20_40_fft(struct ath_rx_status *rs,
			struct ath_spec_scan_priv *spec_priv,
			u8 *sample_buf,
			u64 tsf, u16 freq, int chan_type)
{
	struct fft_sample_ht20_40 fft_sample_40;
	struct ath_common *common = ath9k_hw_common(spec_priv->ah);
	struct ath_hw *ah = spec_priv->ah;
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	struct ath_ht20_40_mag_info *mag_info;
	struct fft_sample_tlv *tlv;
	int dc_pos = SPECTRAL_HT20_40_NUM_BINS / 2;
	int i = 0;
	int ret = 0;
	s16 ext_nf;
	u16 lower_mag, upper_mag, tmp_mag, length;
	s8 lower_rssi, upper_rssi;
	u8 lower_max_index, upper_max_index;
	u8 lower_bitmap_w, upper_bitmap_w, max_exp;

	if (caldata)
		ext_nf = ath9k_hw_getchan_noise(ah, ah->curchan,
				caldata->nfCalHist[3].privNF);
	else
		ext_nf = ATH_DEFAULT_NOISE_FLOOR;

	length = sizeof(fft_sample_40) - sizeof(struct fft_sample_tlv);
	fft_sample_40.tlv.type = ATH_FFT_SAMPLE_HT20_40;
	fft_sample_40.tlv.length = __cpu_to_be16(length);
	fft_sample_40.freq = __cpu_to_be16(freq);
	fft_sample_40.channel_type = chan_type;

	if (chan_type == NL80211_CHAN_HT40PLUS) {
		lower_rssi = fix_rssi_inv_only(rs->rs_rssi_ctl[0]);
		upper_rssi = fix_rssi_inv_only(rs->rs_rssi_ext[0]);

		fft_sample_40.lower_noise = ah->noise;
		fft_sample_40.upper_noise = ext_nf;
	} else {
		lower_rssi = fix_rssi_inv_only(rs->rs_rssi_ext[0]);
		upper_rssi = fix_rssi_inv_only(rs->rs_rssi_ctl[0]);

		fft_sample_40.lower_noise = ext_nf;
		fft_sample_40.upper_noise = ah->noise;
	}

	fft_sample_40.lower_rssi = lower_rssi;
	fft_sample_40.upper_rssi = upper_rssi;

	mag_info = (struct ath_ht20_40_mag_info *) (sample_buf +
					SPECTRAL_HT20_40_NUM_BINS);

	lower_mag = spectral_max_magnitude(mag_info->lower_bins);
	fft_sample_40.lower_max_magnitude = __cpu_to_be16(lower_mag);

	upper_mag = spectral_max_magnitude(mag_info->upper_bins);
	fft_sample_40.upper_max_magnitude = __cpu_to_be16(upper_mag);

	lower_max_index = spectral_max_index_ht40(mag_info->lower_bins);
	fft_sample_40.lower_max_index = lower_max_index;

	upper_max_index = spectral_max_index_ht40(mag_info->upper_bins);
	fft_sample_40.upper_max_index = upper_max_index;

	lower_bitmap_w = spectral_bitmap_weight(mag_info->lower_bins);
	fft_sample_40.lower_bitmap_weight = lower_bitmap_w;

	upper_bitmap_w = spectral_bitmap_weight(mag_info->upper_bins);
	fft_sample_40.upper_bitmap_weight = upper_bitmap_w;

	max_exp = mag_info->max_exp & 0xf;
	fft_sample_40.max_exp = max_exp;

	fft_sample_40.tsf = __cpu_to_be64(tsf);

	memcpy(fft_sample_40.data, sample_buf, SPECTRAL_HT20_40_NUM_BINS);

	ath_dbg(common, SPECTRAL_SCAN, "FFT HT20/40 frame: lower mag 0x%X,"
					"lower_mag_idx %i, upper mag 0x%X,"
					"upper_mag_idx %i\n",
					lower_mag >> max_exp,
					lower_max_index,
					upper_mag >> max_exp,
					upper_max_index);

	/* Check if we got the expected magnitude values at
	 * the expected bins
	 */
	if (((fft_sample_40.data[upper_max_index + dc_pos] & 0xf8)
	    != ((upper_mag >> max_exp) & 0xf8)) ||
	   ((fft_sample_40.data[lower_max_index] & 0xf8)
	    != ((lower_mag >> max_exp) & 0xf8))) {
		ath_dbg(common, SPECTRAL_SCAN, "Magnitude mismatch !\n");
		ret = -1;
	}

	/* DC value (value in the middle) is the blind spot of the spectral
	 * sample and invalid, interpolate it.
	 */
	fft_sample_40.data[dc_pos] = (fft_sample_40.data[dc_pos + 1] +
					fft_sample_40.data[dc_pos - 1]) / 2;

	/* Check if the maximum magnitudes are indeed maximum,
	 * also if the maximum value was at dc_pos, calculate
	 * a new one (since value at dc_pos is invalid).
	 */
	if (lower_max_index == dc_pos) {
		tmp_mag = 0;
		for (i = 0; i < dc_pos; i++) {
			if (fft_sample_40.data[i] > tmp_mag) {
				tmp_mag = fft_sample_40.data[i];
				fft_sample_40.lower_max_index = i;
			}
		}

		lower_mag = tmp_mag << max_exp;
		fft_sample_40.lower_max_magnitude = __cpu_to_be16(lower_mag);

		ath_dbg(common, SPECTRAL_SCAN,
			"Calculated new lower max 0x%X at %i\n",
			tmp_mag, fft_sample_40.lower_max_index);
	} else
	for (i = 0; i < dc_pos; i++) {
		if (fft_sample_40.data[i] == (lower_mag >> max_exp))
			ath_dbg(common, SPECTRAL_SCAN,
				"Got lower mag: 0x%X at index %i\n",
				fft_sample_40.data[i], i);

		if (fft_sample_40.data[i] > (lower_mag >> max_exp)) {
			ath_dbg(common, SPECTRAL_SCAN,
				"Got lower bin %i higher than max: 0x%X\n",
				i, fft_sample_40.data[i]);
			ret = -1;
		}
	}

	if (upper_max_index == dc_pos) {
		tmp_mag = 0;
		for (i = dc_pos; i < SPECTRAL_HT20_40_NUM_BINS; i++) {
			if (fft_sample_40.data[i] > tmp_mag) {
				tmp_mag = fft_sample_40.data[i];
				fft_sample_40.upper_max_index = i;
			}
		}
		upper_mag = tmp_mag << max_exp;
		fft_sample_40.upper_max_magnitude = __cpu_to_be16(upper_mag);

		ath_dbg(common, SPECTRAL_SCAN,
			"Calculated new upper max 0x%X at %i\n",
			tmp_mag, fft_sample_40.upper_max_index);
	} else
	for (i = dc_pos; i < SPECTRAL_HT20_40_NUM_BINS; i++) {
		if (fft_sample_40.data[i] == (upper_mag >> max_exp))
			ath_dbg(common, SPECTRAL_SCAN,
				"Got upper mag: 0x%X at index %i\n",
				fft_sample_40.data[i], i);

		if (fft_sample_40.data[i] > (upper_mag >> max_exp)) {
			ath_dbg(common, SPECTRAL_SCAN,
				"Got upper bin %i higher than max: 0x%X\n",
				i, fft_sample_40.data[i]);

			ret = -1;
		}
	}

	if (ret < 0)
		return ret;

	tlv = (struct fft_sample_tlv *)&fft_sample_40;

	ath_debug_send_fft_sample(spec_priv, tlv);

	return 0;
}

static inline void
ath_cmn_copy_fft_frame(u8 *in, u8 *out, int sample_len, int sample_bytes)
{
	switch (sample_bytes - sample_len) {
	case -1:
		/* First byte missing */
		memcpy(&out[1], in,
		       sample_len - 1);
		break;
	case 0:
		/* Length correct, nothing to do. */
		memcpy(out, in, sample_len);
		break;
	case 1:
		/* MAC added 2 extra bytes AND first byte
		 * is missing.
		 */
		memcpy(&out[1], in, 30);
		out[31] = in[31];
		memcpy(&out[32], &in[33],
		       sample_len - 32);
		break;
	case 2:
		/* MAC added 2 extra bytes at bin 30 and 32,
		 * remove them.
		 */
		memcpy(out, in, 30);
		out[30] = in[31];
		memcpy(&out[31], &in[33],
		       sample_len - 31);
		break;
	default:
		break;
	}
}

static int
ath_cmn_is_fft_buf_full(struct ath_spec_scan_priv *spec_priv)
{
	int i = 0;
	int ret = 0;
	struct rchan_buf *buf;
	struct rchan *rc = spec_priv->rfs_chan_spec_scan;

	for_each_possible_cpu(i) {
		if ((buf = *per_cpu_ptr(rc->buf, i))) {
			ret += relay_buf_full(buf);
		}
	}

	if (ret)
		return 1;
	else
		return 0;
}

/* returns 1 if this was a spectral frame, even if not handled. */
int ath_cmn_process_fft(struct ath_spec_scan_priv *spec_priv, struct ieee80211_hdr *hdr,
		    struct ath_rx_status *rs, u64 tsf)
{
	u8 sample_buf[SPECTRAL_SAMPLE_MAX_LEN] = {0};
	struct ath_hw *ah = spec_priv->ah;
	struct ath_common *common = ath9k_hw_common(spec_priv->ah);
	struct ath_softc *sc = (struct ath_softc *)common->priv;
	u8 num_bins, *vdata = (u8 *)hdr;
	struct ath_radar_info *radar_info;
	int len = rs->rs_datalen;
	int i;
	int got_slen = 0;
	u8  *sample_start;
	int sample_bytes = 0;
	int ret = 0;
	u16 fft_len, sample_len, freq = ah->curchan->chan->center_freq;
	enum nl80211_channel_type chan_type;
	ath_cmn_fft_idx_validator *fft_idx_validator;
	ath_cmn_fft_sample_handler *fft_handler;

	/* AR9280 and before report via ATH9K_PHYERR_RADAR, AR93xx and newer
	 * via ATH9K_PHYERR_SPECTRAL. Haven't seen ATH9K_PHYERR_FALSE_RADAR_EXT
	 * yet, but this is supposed to be possible as well.
	 */
	if (rs->rs_phyerr != ATH9K_PHYERR_RADAR &&
	    rs->rs_phyerr != ATH9K_PHYERR_FALSE_RADAR_EXT &&
	    rs->rs_phyerr != ATH9K_PHYERR_SPECTRAL)
		return 0;

	/* check if spectral scan bit is set. This does not have to be checked
	 * if received through a SPECTRAL phy error, but shouldn't hurt.
	 */
	radar_info = ((struct ath_radar_info *)&vdata[len]) - 1;
	if (!(radar_info->pulse_bw_info & SPECTRAL_SCAN_BITMASK))
		return 0;

	if (!spec_priv->rfs_chan_spec_scan)
		return 1;

	/* Output buffers are full, no need to process anything
	 * since there is no space to put the result anyway
	 */
	ret = ath_cmn_is_fft_buf_full(spec_priv);
	if (ret == 1) {
		ath_dbg(common, SPECTRAL_SCAN, "FFT report ignored, no space "
						"left on output buffers\n");
		return 1;
	}

	chan_type = cfg80211_get_chandef_type(&common->hw->conf.chandef);
	if ((chan_type == NL80211_CHAN_HT40MINUS) ||
	    (chan_type == NL80211_CHAN_HT40PLUS)) {
		fft_len = SPECTRAL_HT20_40_TOTAL_DATA_LEN;
		sample_len = SPECTRAL_HT20_40_SAMPLE_LEN;
		num_bins = SPECTRAL_HT20_40_NUM_BINS;
		fft_idx_validator = &ath_cmn_max_idx_verify_ht20_40_fft;
		fft_handler = &ath_cmn_process_ht20_40_fft;
	} else {
		fft_len = SPECTRAL_HT20_TOTAL_DATA_LEN;
		sample_len = SPECTRAL_HT20_SAMPLE_LEN;
		num_bins = SPECTRAL_HT20_NUM_BINS;
		fft_idx_validator = ath_cmn_max_idx_verify_ht20_fft;
		fft_handler = &ath_cmn_process_ht20_fft;
	}

	ath_dbg(common, SPECTRAL_SCAN, "Got radar dump bw_info: 0x%X,"
					"len: %i fft_len: %i\n",
					radar_info->pulse_bw_info,
					len,
					fft_len);
	sample_start = vdata;
	for (i = 0; i < len - 2; i++) {
		sample_bytes++;

		/* Only a single sample received, no need to look
		 * for the sample's end, do the correction based
		 * on the packet's length instead. Note that hw
		 * will always put the radar_info structure on
		 * the end.
		 */
		if (len <= fft_len + 2) {
			sample_bytes = len - sizeof(struct ath_radar_info);
			got_slen = 1;
		}

		/* Search for the end of the FFT frame between
		 * sample_len - 1 and sample_len + 2. exp_max is 3
		 * bits long and it's the only value on the last
		 * byte of the frame so since it'll be smaller than
		 * the next byte (the first bin of the next sample)
		 * 90% of the time, we can use it as a separator.
		 */
		if (vdata[i] <= 0x7 && sample_bytes >= sample_len - 1) {

			/* Got a frame length within boundaries, there are
			 * four scenarios here:
			 *
			 * a) sample_len -> We got the correct length
			 * b) sample_len + 2 -> 2 bytes added around bin[31]
			 * c) sample_len - 1 -> The first byte is missing
			 * d) sample_len + 1 -> b + c at the same time
			 *
			 * When MAC adds 2 extra bytes, bin[31] and bin[32]
			 * have the same value, so we can use that for further
			 * verification in cases b and d.
			 */

			/* Did we go too far ? If so we couldn't determine
			 * this sample's boundaries, discard any further
			 * data
			 */
			if ((sample_bytes > sample_len + 2) ||
			   ((sample_bytes > sample_len) &&
			   (sample_start[31] != sample_start[32])))
				break;

			/* See if we got a valid frame by checking the
			 * consistency of mag_info fields. This is to
			 * prevent from "fixing" a correct frame.
			 * Failure is non-fatal, later frames may
			 * be valid.
			 */
			if (!fft_idx_validator(&vdata[i], i)) {
				ath_dbg(common, SPECTRAL_SCAN,
					"Found valid fft frame at %i\n", i);
				got_slen = 1;
			}

			/* We expect 1 - 2 more bytes */
			else if ((sample_start[31] == sample_start[32]) &&
				(sample_bytes >= sample_len) &&
				(sample_bytes < sample_len + 2) &&
				(vdata[i + 1] <= 0x7))
				continue;

			/* Try to distinguish cases a and c */
			else if ((sample_bytes == sample_len - 1) &&
				(vdata[i + 1] <= 0x7))
				continue;

			got_slen = 1;
		}

		if (got_slen) {
			ath_dbg(common, SPECTRAL_SCAN, "FFT frame len: %i\n",
				sample_bytes);

			/* Only try to fix a frame if it's the only one
			 * on the report, else just skip it.
			 */
			if (sample_bytes != sample_len && len <= fft_len + 2) {
				ath_cmn_copy_fft_frame(sample_start,
						       sample_buf, sample_len,
						       sample_bytes);

				ret = fft_handler(rs, spec_priv, sample_buf,
						  tsf, freq, chan_type);

				if (ret == 0)
					RX_STAT_INC(rx_spectral_sample_good);
				else
					RX_STAT_INC(rx_spectral_sample_err);

				memset(sample_buf, 0, SPECTRAL_SAMPLE_MAX_LEN);

				/* Mix the received bins to the /dev/random
				 * pool
				 */
				add_device_randomness(sample_buf, num_bins);
			}

			/* Process a normal frame */
			if (sample_bytes == sample_len) {
				ret = fft_handler(rs, spec_priv, sample_start,
						  tsf, freq, chan_type);

				if (ret == 0)
					RX_STAT_INC(rx_spectral_sample_good);
				else
					RX_STAT_INC(rx_spectral_sample_err);

				/* Mix the received bins to the /dev/random
				 * pool
				 */
				add_device_randomness(sample_start, num_bins);
			}

			/* Short report processed, break out of the
			 * loop.
			 */
			if (len <= fft_len + 2)
				return 1;

			sample_start = &vdata[i + 1];

			/* -1 to grab sample_len -1, -2 since
			 * they 'll get increased by one. In case
			 * of failure try to recover by going byte
			 * by byte instead.
			 */
			if (ret == 0) {
				i += num_bins - 2;
				sample_bytes = num_bins - 2;
			}
			got_slen = 0;
		}
	}

	i -= num_bins - 2;
	if (len - i != sizeof(struct ath_radar_info))
		ath_dbg(common, SPECTRAL_SCAN, "FFT report truncated"
						"(bytes left: %i)\n",
						len - i);
	return 1;
}
EXPORT_SYMBOL(ath_cmn_process_fft);

/*********************/
/* spectral_scan_ctl */
/*********************/

static ssize_t read_file_spec_scan_ctl(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	char *mode = "";
	unsigned int len;

	switch (spec_priv->spectral_mode) {
	case SPECTRAL_DISABLED:
		mode = "disable";
		break;
	case SPECTRAL_BACKGROUND:
		mode = "background";
		break;
	case SPECTRAL_CHANSCAN:
		mode = "chanscan";
		break;
	case SPECTRAL_MANUAL:
		mode = "manual";
		break;
	}
	len = strlen(mode);
	return simple_read_from_buffer(user_buf, count, ppos, mode, len);
}

void ath9k_cmn_spectral_scan_trigger(struct ath_common *common,
				 struct ath_spec_scan_priv *spec_priv)
{
	struct ath_hw *ah = spec_priv->ah;
	u32 rxfilter;

	if (IS_ENABLED(CONFIG_ATH9K_TX99))
		return;

	if (!ath9k_hw_ops(ah)->spectral_scan_trigger) {
		ath_err(common, "spectrum analyzer not implemented on this hardware\n");
		return;
	}

	if (!spec_priv->spec_config.enabled)
		return;

	ath_ps_ops(common)->wakeup(common);
	rxfilter = ath9k_hw_getrxfilter(ah);
	ath9k_hw_setrxfilter(ah, rxfilter |
				 ATH9K_RX_FILTER_PHYRADAR |
				 ATH9K_RX_FILTER_PHYERR);

	/* TODO: usually this should not be neccesary, but for some reason
	 * (or in some mode?) the trigger must be called after the
	 * configuration, otherwise the register will have its values reset
	 * (on my ar9220 to value 0x01002310)
	 */
	ath9k_cmn_spectral_scan_config(common, spec_priv, spec_priv->spectral_mode);
	ath9k_hw_ops(ah)->spectral_scan_trigger(ah);
	ath_ps_ops(common)->restore(common);
}
EXPORT_SYMBOL(ath9k_cmn_spectral_scan_trigger);

int ath9k_cmn_spectral_scan_config(struct ath_common *common,
			       struct ath_spec_scan_priv *spec_priv,
			       enum spectral_mode spectral_mode)
{
	struct ath_hw *ah = spec_priv->ah;

	if (!ath9k_hw_ops(ah)->spectral_scan_trigger) {
		ath_err(common, "spectrum analyzer not implemented on this hardware\n");
		return -1;
	}

	switch (spectral_mode) {
	case SPECTRAL_DISABLED:
		spec_priv->spec_config.enabled = 0;
		break;
	case SPECTRAL_BACKGROUND:
		/* send endless samples.
		 * TODO: is this really useful for "background"?
		 */
		spec_priv->spec_config.endless = 1;
		spec_priv->spec_config.enabled = 1;
		break;
	case SPECTRAL_CHANSCAN:
	case SPECTRAL_MANUAL:
		spec_priv->spec_config.endless = 0;
		spec_priv->spec_config.enabled = 1;
		break;
	default:
		return -1;
	}

	ath_ps_ops(common)->wakeup(common);
	ath9k_hw_ops(ah)->spectral_scan_config(ah, &spec_priv->spec_config);
	ath_ps_ops(common)->restore(common);

	spec_priv->spectral_mode = spectral_mode;

	return 0;
}
EXPORT_SYMBOL(ath9k_cmn_spectral_scan_config);

static ssize_t write_file_spec_scan_ctl(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	struct ath_common *common = ath9k_hw_common(spec_priv->ah);
	char buf[32];
	ssize_t len;

	if (IS_ENABLED(CONFIG_ATH9K_TX99))
		return -EOPNOTSUPP;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (strncmp("trigger", buf, 7) == 0) {
		ath9k_cmn_spectral_scan_trigger(common, spec_priv);
	} else if (strncmp("background", buf, 10) == 0) {
		ath9k_cmn_spectral_scan_config(common, spec_priv, SPECTRAL_BACKGROUND);
		ath_dbg(common, CONFIG, "spectral scan: background mode enabled\n");
	} else if (strncmp("chanscan", buf, 8) == 0) {
		ath9k_cmn_spectral_scan_config(common, spec_priv, SPECTRAL_CHANSCAN);
		ath_dbg(common, CONFIG, "spectral scan: channel scan mode enabled\n");
	} else if (strncmp("manual", buf, 6) == 0) {
		ath9k_cmn_spectral_scan_config(common, spec_priv, SPECTRAL_MANUAL);
		ath_dbg(common, CONFIG, "spectral scan: manual mode enabled\n");
	} else if (strncmp("disable", buf, 7) == 0) {
		ath9k_cmn_spectral_scan_config(common, spec_priv, SPECTRAL_DISABLED);
		ath_dbg(common, CONFIG, "spectral scan: disabled\n");
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations fops_spec_scan_ctl = {
	.read = read_file_spec_scan_ctl,
	.write = write_file_spec_scan_ctl,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/*************************/
/* spectral_short_repeat */
/*************************/

static ssize_t read_file_spectral_short_repeat(struct file *file,
					       char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", spec_priv->spec_config.short_repeat);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_short_repeat(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	spec_priv->spec_config.short_repeat = val;
	return count;
}

static const struct file_operations fops_spectral_short_repeat = {
	.read = read_file_spectral_short_repeat,
	.write = write_file_spectral_short_repeat,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/******************/
/* spectral_count */
/******************/

static ssize_t read_file_spectral_count(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", spec_priv->spec_config.count);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_count(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 255)
		return -EINVAL;

	spec_priv->spec_config.count = val;
	return count;
}

static const struct file_operations fops_spectral_count = {
	.read = read_file_spectral_count,
	.write = write_file_spectral_count,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/*******************/
/* spectral_period */
/*******************/

static ssize_t read_file_spectral_period(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", spec_priv->spec_config.period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_period(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 255)
		return -EINVAL;

	spec_priv->spec_config.period = val;
	return count;
}

static const struct file_operations fops_spectral_period = {
	.read = read_file_spectral_period,
	.write = write_file_spectral_period,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/***********************/
/* spectral_fft_period */
/***********************/

static ssize_t read_file_spectral_fft_period(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", spec_priv->spec_config.fft_period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_fft_period(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath_spec_scan_priv *spec_priv = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 15)
		return -EINVAL;

	spec_priv->spec_config.fft_period = val;
	return count;
}

static const struct file_operations fops_spectral_fft_period = {
	.read = read_file_spectral_fft_period,
	.write = write_file_spectral_fft_period,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/*******************/
/* Relay interface */
/*******************/

static struct dentry *create_buf_file_handler(const char *filename,
					      struct dentry *parent,
					      umode_t mode,
					      struct rchan_buf *buf,
					      int *is_global)
{
	struct dentry *buf_file;

	buf_file = debugfs_create_file(filename, mode, parent, buf,
				       &relay_file_operations);
	*is_global = 1;
	return buf_file;
}

static int remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

static struct rchan_callbacks rfs_spec_scan_cb = {
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};

/*********************/
/* Debug Init/Deinit */
/*********************/

void ath9k_cmn_spectral_deinit_debug(struct ath_spec_scan_priv *spec_priv)
{
	if (spec_priv->rfs_chan_spec_scan) {
		relay_close(spec_priv->rfs_chan_spec_scan);
		spec_priv->rfs_chan_spec_scan = NULL;
	}
}
EXPORT_SYMBOL(ath9k_cmn_spectral_deinit_debug);

void ath9k_cmn_spectral_init_debug(struct ath_spec_scan_priv *spec_priv,
				   struct dentry *debugfs_phy)
{
	spec_priv->rfs_chan_spec_scan = relay_open("spectral_scan",
					    debugfs_phy,
					    1024, 256, &rfs_spec_scan_cb,
					    NULL);
	if (!spec_priv->rfs_chan_spec_scan)
		return;

	debugfs_create_file("spectral_scan_ctl",
			    0600,
			    debugfs_phy, spec_priv,
			    &fops_spec_scan_ctl);
	debugfs_create_file("spectral_short_repeat",
			    0600,
			    debugfs_phy, spec_priv,
			    &fops_spectral_short_repeat);
	debugfs_create_file("spectral_count",
			    0600,
			    debugfs_phy, spec_priv,
			    &fops_spectral_count);
	debugfs_create_file("spectral_period",
			    0600,
			    debugfs_phy, spec_priv,
			    &fops_spectral_period);
	debugfs_create_file("spectral_fft_period",
			    0600,
			    debugfs_phy, spec_priv,
			    &fops_spectral_fft_period);
}
EXPORT_SYMBOL(ath9k_cmn_spectral_init_debug);
