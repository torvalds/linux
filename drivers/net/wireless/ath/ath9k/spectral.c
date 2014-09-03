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
#include "ath9k.h"

static s8 fix_rssi_inv_only(u8 rssi_val)
{
	if (rssi_val == 128)
		rssi_val = 0;
	return (s8) rssi_val;
}

static void ath_debug_send_fft_sample(struct ath_softc *sc,
				      struct fft_sample_tlv *fft_sample_tlv)
{
	int length;
	if (!sc->rfs_chan_spec_scan)
		return;

	length = __be16_to_cpu(fft_sample_tlv->length) +
		 sizeof(*fft_sample_tlv);
	relay_write(sc->rfs_chan_spec_scan, fft_sample_tlv, length);
}

/* returns 1 if this was a spectral frame, even if not handled. */
int ath_process_fft(struct ath_softc *sc, struct ieee80211_hdr *hdr,
		    struct ath_rx_status *rs, u64 tsf)
{
	struct ath_hw *ah = sc->sc_ah;
	u8 num_bins, *bins, *vdata = (u8 *)hdr;
	struct fft_sample_ht20 fft_sample_20;
	struct fft_sample_ht20_40 fft_sample_40;
	struct fft_sample_tlv *tlv;
	struct ath_radar_info *radar_info;
	int len = rs->rs_datalen;
	int dc_pos;
	u16 fft_len, length, freq = ah->curchan->chan->center_freq;
	enum nl80211_channel_type chan_type;

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

	chan_type = cfg80211_get_chandef_type(&sc->hw->conf.chandef);
	if ((chan_type == NL80211_CHAN_HT40MINUS) ||
	    (chan_type == NL80211_CHAN_HT40PLUS)) {
		fft_len = SPECTRAL_HT20_40_TOTAL_DATA_LEN;
		num_bins = SPECTRAL_HT20_40_NUM_BINS;
		bins = (u8 *)fft_sample_40.data;
	} else {
		fft_len = SPECTRAL_HT20_TOTAL_DATA_LEN;
		num_bins = SPECTRAL_HT20_NUM_BINS;
		bins = (u8 *)fft_sample_20.data;
	}

	/* Variation in the data length is possible and will be fixed later */
	if ((len > fft_len + 2) || (len < fft_len - 1))
		return 1;

	switch (len - fft_len) {
	case 0:
		/* length correct, nothing to do. */
		memcpy(bins, vdata, num_bins);
		break;
	case -1:
		/* first byte missing, duplicate it. */
		memcpy(&bins[1], vdata, num_bins - 1);
		bins[0] = vdata[0];
		break;
	case 2:
		/* MAC added 2 extra bytes at bin 30 and 32, remove them. */
		memcpy(bins, vdata, 30);
		bins[30] = vdata[31];
		memcpy(&bins[31], &vdata[33], num_bins - 31);
		break;
	case 1:
		/* MAC added 2 extra bytes AND first byte is missing. */
		bins[0] = vdata[0];
		memcpy(&bins[1], vdata, 30);
		bins[31] = vdata[31];
		memcpy(&bins[32], &vdata[33], num_bins - 32);
		break;
	default:
		return 1;
	}

	/* DC value (value in the middle) is the blind spot of the spectral
	 * sample and invalid, interpolate it.
	 */
	dc_pos = num_bins / 2;
	bins[dc_pos] = (bins[dc_pos + 1] + bins[dc_pos - 1]) / 2;

	if ((chan_type == NL80211_CHAN_HT40MINUS) ||
	    (chan_type == NL80211_CHAN_HT40PLUS)) {
		s8 lower_rssi, upper_rssi;
		s16 ext_nf;
		u8 lower_max_index, upper_max_index;
		u8 lower_bitmap_w, upper_bitmap_w;
		u16 lower_mag, upper_mag;
		struct ath9k_hw_cal_data *caldata = ah->caldata;
		struct ath_ht20_40_mag_info *mag_info;

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

		mag_info = ((struct ath_ht20_40_mag_info *)radar_info) - 1;
		lower_mag = spectral_max_magnitude(mag_info->lower_bins);
		upper_mag = spectral_max_magnitude(mag_info->upper_bins);
		fft_sample_40.lower_max_magnitude = __cpu_to_be16(lower_mag);
		fft_sample_40.upper_max_magnitude = __cpu_to_be16(upper_mag);
		lower_max_index = spectral_max_index(mag_info->lower_bins);
		upper_max_index = spectral_max_index(mag_info->upper_bins);
		fft_sample_40.lower_max_index = lower_max_index;
		fft_sample_40.upper_max_index = upper_max_index;
		lower_bitmap_w = spectral_bitmap_weight(mag_info->lower_bins);
		upper_bitmap_w = spectral_bitmap_weight(mag_info->upper_bins);
		fft_sample_40.lower_bitmap_weight = lower_bitmap_w;
		fft_sample_40.upper_bitmap_weight = upper_bitmap_w;
		fft_sample_40.max_exp = mag_info->max_exp & 0xf;

		fft_sample_40.tsf = __cpu_to_be64(tsf);

		tlv = (struct fft_sample_tlv *)&fft_sample_40;
	} else {
		u8 max_index, bitmap_w;
		u16 magnitude;
		struct ath_ht20_mag_info *mag_info;

		length = sizeof(fft_sample_20) - sizeof(struct fft_sample_tlv);
		fft_sample_20.tlv.type = ATH_FFT_SAMPLE_HT20;
		fft_sample_20.tlv.length = __cpu_to_be16(length);
		fft_sample_20.freq = __cpu_to_be16(freq);

		fft_sample_20.rssi = fix_rssi_inv_only(rs->rs_rssi_ctl[0]);
		fft_sample_20.noise = ah->noise;

		mag_info = ((struct ath_ht20_mag_info *)radar_info) - 1;
		magnitude = spectral_max_magnitude(mag_info->all_bins);
		fft_sample_20.max_magnitude = __cpu_to_be16(magnitude);
		max_index = spectral_max_index(mag_info->all_bins);
		fft_sample_20.max_index = max_index;
		bitmap_w = spectral_bitmap_weight(mag_info->all_bins);
		fft_sample_20.bitmap_weight = bitmap_w;
		fft_sample_20.max_exp = mag_info->max_exp & 0xf;

		fft_sample_20.tsf = __cpu_to_be64(tsf);

		tlv = (struct fft_sample_tlv *)&fft_sample_20;
	}

	ath_debug_send_fft_sample(sc, tlv);

	return 1;
}

/*********************/
/* spectral_scan_ctl */
/*********************/

static ssize_t read_file_spec_scan_ctl(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	char *mode = "";
	unsigned int len;

	switch (sc->spectral_mode) {
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

static ssize_t write_file_spec_scan_ctl(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	char buf[32];
	ssize_t len;

	if (config_enabled(CONFIG_ATH9K_TX99))
		return -EOPNOTSUPP;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (strncmp("trigger", buf, 7) == 0) {
		ath9k_spectral_scan_trigger(sc->hw);
	} else if (strncmp("background", buf, 9) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_BACKGROUND);
		ath_dbg(common, CONFIG, "spectral scan: background mode enabled\n");
	} else if (strncmp("chanscan", buf, 8) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_CHANSCAN);
		ath_dbg(common, CONFIG, "spectral scan: channel scan mode enabled\n");
	} else if (strncmp("manual", buf, 6) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_MANUAL);
		ath_dbg(common, CONFIG, "spectral scan: manual mode enabled\n");
	} else if (strncmp("disable", buf, 7) == 0) {
		ath9k_spectral_scan_config(sc->hw, SPECTRAL_DISABLED);
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
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.short_repeat);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_short_repeat(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
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

	sc->spec_config.short_repeat = val;
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
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.count);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_count(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
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

	sc->spec_config.count = val;
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
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_period(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
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

	sc->spec_config.period = val;
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
	struct ath_softc *sc = file->private_data;
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%d\n", sc->spec_config.fft_period);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_fft_period(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
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

	sc->spec_config.fft_period = val;
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

void ath9k_spectral_deinit_debug(struct ath_softc *sc)
{
	if (config_enabled(CONFIG_ATH9K_DEBUGFS) && sc->rfs_chan_spec_scan) {
		relay_close(sc->rfs_chan_spec_scan);
		sc->rfs_chan_spec_scan = NULL;
	}
}

void ath9k_spectral_init_debug(struct ath_softc *sc)
{
	sc->rfs_chan_spec_scan = relay_open("spectral_scan",
					    sc->debug.debugfs_phy,
					    1024, 256, &rfs_spec_scan_cb,
					    NULL);
	debugfs_create_file("spectral_scan_ctl",
			    S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spec_scan_ctl);
	debugfs_create_file("spectral_short_repeat",
			    S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_short_repeat);
	debugfs_create_file("spectral_count",
			    S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_count);
	debugfs_create_file("spectral_period",
			    S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_period);
	debugfs_create_file("spectral_fft_period",
			    S_IRUSR | S_IWUSR,
			    sc->debug.debugfs_phy, sc,
			    &fops_spectral_fft_period);
}
