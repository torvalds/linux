/*
 * Copyright (c) 2013-2017 Qualcomm Atheros, Inc.
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
#include "core.h"
#include "debug.h"
#include "wmi-ops.h"

static void send_fft_sample(struct ath10k *ar,
			    const struct fft_sample_tlv *fft_sample_tlv)
{
	int length;

	if (!ar->spectral.rfs_chan_spec_scan)
		return;

	length = __be16_to_cpu(fft_sample_tlv->length) +
		 sizeof(*fft_sample_tlv);
	relay_write(ar->spectral.rfs_chan_spec_scan, fft_sample_tlv, length);
}

static uint8_t get_max_exp(s8 max_index, u16 max_magnitude, size_t bin_len,
			   u8 *data)
{
	int dc_pos;
	u8 max_exp;

	dc_pos = bin_len / 2;

	/* peak index outside of bins */
	if (dc_pos < max_index || -dc_pos >= max_index)
		return 0;

	for (max_exp = 0; max_exp < 8; max_exp++) {
		if (data[dc_pos + max_index] == (max_magnitude >> max_exp))
			break;
	}

	/* max_exp not found */
	if (data[dc_pos + max_index] != (max_magnitude >> max_exp))
		return 0;

	return max_exp;
}

static inline size_t ath10k_spectral_fix_bin_size(struct ath10k *ar,
						  size_t bin_len)
{
	/* some chipsets reports bin size as 2^n bytes + 'm' bytes in
	 * report mode 2. First 2^n bytes carries inband tones and last
	 * 'm' bytes carries band edge detection data mainly used in
	 * radar detection purpose. Strip last 'm' bytes to make bin size
	 * as a valid one. 'm' can take possible values of 4, 12.
	 */
	if (!is_power_of_2(bin_len))
		bin_len -= ar->hw_params.spectral_bin_discard;

	return bin_len;
}

int ath10k_spectral_process_fft(struct ath10k *ar,
				struct wmi_phyerr_ev_arg *phyerr,
				const struct phyerr_fft_report *fftr,
				size_t bin_len, u64 tsf)
{
	struct fft_sample_ath10k *fft_sample;
	u8 buf[sizeof(*fft_sample) + SPECTRAL_ATH10K_MAX_NUM_BINS];
	u16 freq1, freq2, total_gain_db, base_pwr_db, length, peak_mag;
	u32 reg0, reg1;
	u8 chain_idx, *bins;
	int dc_pos;

	fft_sample = (struct fft_sample_ath10k *)&buf;

	bin_len = ath10k_spectral_fix_bin_size(ar, bin_len);

	if (bin_len < 64 || bin_len > SPECTRAL_ATH10K_MAX_NUM_BINS)
		return -EINVAL;

	reg0 = __le32_to_cpu(fftr->reg0);
	reg1 = __le32_to_cpu(fftr->reg1);

	length = sizeof(*fft_sample) - sizeof(struct fft_sample_tlv) + bin_len;
	fft_sample->tlv.type = ATH_FFT_SAMPLE_ATH10K;
	fft_sample->tlv.length = __cpu_to_be16(length);

	/* TODO: there might be a reason why the hardware reports 20/40/80 MHz,
	 * but the results/plots suggest that its actually 22/44/88 MHz.
	 */
	switch (phyerr->chan_width_mhz) {
	case 20:
		fft_sample->chan_width_mhz = 22;
		break;
	case 40:
		fft_sample->chan_width_mhz = 44;
		break;
	case 80:
		/* TODO: As experiments with an analogue sender and various
		 * configurations (fft-sizes of 64/128/256 and 20/40/80 Mhz)
		 * show, the particular configuration of 80 MHz/64 bins does
		 * not match with the other samples at all. Until the reason
		 * for that is found, don't report these samples.
		 */
		if (bin_len == 64)
			return -EINVAL;
		fft_sample->chan_width_mhz = 88;
		break;
	default:
		fft_sample->chan_width_mhz = phyerr->chan_width_mhz;
	}

	fft_sample->relpwr_db = MS(reg1, SEARCH_FFT_REPORT_REG1_RELPWR_DB);
	fft_sample->avgpwr_db = MS(reg1, SEARCH_FFT_REPORT_REG1_AVGPWR_DB);

	peak_mag = MS(reg1, SEARCH_FFT_REPORT_REG1_PEAK_MAG);
	fft_sample->max_magnitude = __cpu_to_be16(peak_mag);
	fft_sample->max_index = MS(reg0, SEARCH_FFT_REPORT_REG0_PEAK_SIDX);
	fft_sample->rssi = phyerr->rssi_combined;

	total_gain_db = MS(reg0, SEARCH_FFT_REPORT_REG0_TOTAL_GAIN_DB);
	base_pwr_db = MS(reg0, SEARCH_FFT_REPORT_REG0_BASE_PWR_DB);
	fft_sample->total_gain_db = __cpu_to_be16(total_gain_db);
	fft_sample->base_pwr_db = __cpu_to_be16(base_pwr_db);

	freq1 = phyerr->freq1;
	freq2 = phyerr->freq2;
	fft_sample->freq1 = __cpu_to_be16(freq1);
	fft_sample->freq2 = __cpu_to_be16(freq2);

	chain_idx = MS(reg0, SEARCH_FFT_REPORT_REG0_FFT_CHN_IDX);

	fft_sample->noise = __cpu_to_be16(phyerr->nf_chains[chain_idx]);

	bins = (u8 *)fftr;
	bins += sizeof(*fftr) + ar->hw_params.spectral_bin_offset;

	fft_sample->tsf = __cpu_to_be64(tsf);

	/* max_exp has been directly reported by previous hardware (ath9k),
	 * maybe its possible to get it by other means?
	 */
	fft_sample->max_exp = get_max_exp(fft_sample->max_index, peak_mag,
					  bin_len, bins);

	memcpy(fft_sample->data, bins, bin_len);

	/* DC value (value in the middle) is the blind spot of the spectral
	 * sample and invalid, interpolate it.
	 */
	dc_pos = bin_len / 2;
	fft_sample->data[dc_pos] = (fft_sample->data[dc_pos + 1] +
				    fft_sample->data[dc_pos - 1]) / 2;

	send_fft_sample(ar, &fft_sample->tlv);

	return 0;
}

static struct ath10k_vif *ath10k_get_spectral_vdev(struct ath10k *ar)
{
	struct ath10k_vif *arvif;

	lockdep_assert_held(&ar->conf_mutex);

	if (list_empty(&ar->arvifs))
		return NULL;

	/* if there already is a vif doing spectral, return that. */
	list_for_each_entry(arvif, &ar->arvifs, list)
		if (arvif->spectral_enabled)
			return arvif;

	/* otherwise, return the first vif. */
	return list_first_entry(&ar->arvifs, typeof(*arvif), list);
}

static int ath10k_spectral_scan_trigger(struct ath10k *ar)
{
	struct ath10k_vif *arvif;
	int res;
	int vdev_id;

	lockdep_assert_held(&ar->conf_mutex);

	arvif = ath10k_get_spectral_vdev(ar);
	if (!arvif)
		return -ENODEV;
	vdev_id = arvif->vdev_id;

	if (ar->spectral.mode == SPECTRAL_DISABLED)
		return 0;

	res = ath10k_wmi_vdev_spectral_enable(ar, vdev_id,
					      WMI_SPECTRAL_TRIGGER_CMD_CLEAR,
					      WMI_SPECTRAL_ENABLE_CMD_ENABLE);
	if (res < 0)
		return res;

	res = ath10k_wmi_vdev_spectral_enable(ar, vdev_id,
					      WMI_SPECTRAL_TRIGGER_CMD_TRIGGER,
					      WMI_SPECTRAL_ENABLE_CMD_ENABLE);
	if (res < 0)
		return res;

	return 0;
}

static int ath10k_spectral_scan_config(struct ath10k *ar,
				       enum ath10k_spectral_mode mode)
{
	struct wmi_vdev_spectral_conf_arg arg;
	struct ath10k_vif *arvif;
	int vdev_id, count, res = 0;

	lockdep_assert_held(&ar->conf_mutex);

	arvif = ath10k_get_spectral_vdev(ar);
	if (!arvif)
		return -ENODEV;

	vdev_id = arvif->vdev_id;

	arvif->spectral_enabled = (mode != SPECTRAL_DISABLED);
	ar->spectral.mode = mode;

	res = ath10k_wmi_vdev_spectral_enable(ar, vdev_id,
					      WMI_SPECTRAL_TRIGGER_CMD_CLEAR,
					      WMI_SPECTRAL_ENABLE_CMD_DISABLE);
	if (res < 0) {
		ath10k_warn(ar, "failed to enable spectral scan: %d\n", res);
		return res;
	}

	if (mode == SPECTRAL_DISABLED)
		return 0;

	if (mode == SPECTRAL_BACKGROUND)
		count = WMI_SPECTRAL_COUNT_DEFAULT;
	else
		count = max_t(u8, 1, ar->spectral.config.count);

	arg.vdev_id = vdev_id;
	arg.scan_count = count;
	arg.scan_period = WMI_SPECTRAL_PERIOD_DEFAULT;
	arg.scan_priority = WMI_SPECTRAL_PRIORITY_DEFAULT;
	arg.scan_fft_size = ar->spectral.config.fft_size;
	arg.scan_gc_ena = WMI_SPECTRAL_GC_ENA_DEFAULT;
	arg.scan_restart_ena = WMI_SPECTRAL_RESTART_ENA_DEFAULT;
	arg.scan_noise_floor_ref = WMI_SPECTRAL_NOISE_FLOOR_REF_DEFAULT;
	arg.scan_init_delay = WMI_SPECTRAL_INIT_DELAY_DEFAULT;
	arg.scan_nb_tone_thr = WMI_SPECTRAL_NB_TONE_THR_DEFAULT;
	arg.scan_str_bin_thr = WMI_SPECTRAL_STR_BIN_THR_DEFAULT;
	arg.scan_wb_rpt_mode = WMI_SPECTRAL_WB_RPT_MODE_DEFAULT;
	arg.scan_rssi_rpt_mode = WMI_SPECTRAL_RSSI_RPT_MODE_DEFAULT;
	arg.scan_rssi_thr = WMI_SPECTRAL_RSSI_THR_DEFAULT;
	arg.scan_pwr_format = WMI_SPECTRAL_PWR_FORMAT_DEFAULT;
	arg.scan_rpt_mode = WMI_SPECTRAL_RPT_MODE_DEFAULT;
	arg.scan_bin_scale = WMI_SPECTRAL_BIN_SCALE_DEFAULT;
	arg.scan_dbm_adj = WMI_SPECTRAL_DBM_ADJ_DEFAULT;
	arg.scan_chn_mask = WMI_SPECTRAL_CHN_MASK_DEFAULT;

	res = ath10k_wmi_vdev_spectral_conf(ar, &arg);
	if (res < 0) {
		ath10k_warn(ar, "failed to configure spectral scan: %d\n", res);
		return res;
	}

	return 0;
}

static ssize_t read_file_spec_scan_ctl(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char *mode = "";
	size_t len;
	enum ath10k_spectral_mode spectral_mode;

	mutex_lock(&ar->conf_mutex);
	spectral_mode = ar->spectral.mode;
	mutex_unlock(&ar->conf_mutex);

	switch (spectral_mode) {
	case SPECTRAL_DISABLED:
		mode = "disable";
		break;
	case SPECTRAL_BACKGROUND:
		mode = "background";
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
	struct ath10k *ar = file->private_data;
	char buf[32];
	ssize_t len;
	int res;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	mutex_lock(&ar->conf_mutex);

	if (strncmp("trigger", buf, 7) == 0) {
		if (ar->spectral.mode == SPECTRAL_MANUAL ||
		    ar->spectral.mode == SPECTRAL_BACKGROUND) {
			/* reset the configuration to adopt possibly changed
			 * debugfs parameters
			 */
			res = ath10k_spectral_scan_config(ar,
							  ar->spectral.mode);
			if (res < 0) {
				ath10k_warn(ar, "failed to reconfigure spectral scan: %d\n",
					    res);
			}
			res = ath10k_spectral_scan_trigger(ar);
			if (res < 0) {
				ath10k_warn(ar, "failed to trigger spectral scan: %d\n",
					    res);
			}
		} else {
			res = -EINVAL;
		}
	} else if (strncmp("background", buf, 10) == 0) {
		res = ath10k_spectral_scan_config(ar, SPECTRAL_BACKGROUND);
	} else if (strncmp("manual", buf, 6) == 0) {
		res = ath10k_spectral_scan_config(ar, SPECTRAL_MANUAL);
	} else if (strncmp("disable", buf, 7) == 0) {
		res = ath10k_spectral_scan_config(ar, SPECTRAL_DISABLED);
	} else {
		res = -EINVAL;
	}

	mutex_unlock(&ar->conf_mutex);

	if (res < 0)
		return res;

	return count;
}

static const struct file_operations fops_spec_scan_ctl = {
	.read = read_file_spec_scan_ctl,
	.write = write_file_spec_scan_ctl,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_count(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	size_t len;
	u8 spectral_count;

	mutex_lock(&ar->conf_mutex);
	spectral_count = ar->spectral.config.count;
	mutex_unlock(&ar->conf_mutex);

	len = sprintf(buf, "%d\n", spectral_count);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_count(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
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

	mutex_lock(&ar->conf_mutex);
	ar->spectral.config.count = val;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_spectral_count = {
	.read = read_file_spectral_count,
	.write = write_file_spectral_count,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t read_file_spectral_bins(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	unsigned int bins, fft_size, bin_scale;
	size_t len;

	mutex_lock(&ar->conf_mutex);

	fft_size = ar->spectral.config.fft_size;
	bin_scale = WMI_SPECTRAL_BIN_SCALE_DEFAULT;
	bins = 1 << (fft_size - bin_scale);

	mutex_unlock(&ar->conf_mutex);

	len = sprintf(buf, "%d\n", bins);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_spectral_bins(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < 64 || val > SPECTRAL_ATH10K_MAX_NUM_BINS)
		return -EINVAL;

	if (!is_power_of_2(val))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	ar->spectral.config.fft_size = ilog2(val);
	ar->spectral.config.fft_size += WMI_SPECTRAL_BIN_SCALE_DEFAULT;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_spectral_bins = {
	.read = read_file_spectral_bins,
	.write = write_file_spectral_bins,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

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

int ath10k_spectral_start(struct ath10k *ar)
{
	struct ath10k_vif *arvif;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list)
		arvif->spectral_enabled = 0;

	ar->spectral.mode = SPECTRAL_DISABLED;
	ar->spectral.config.count = WMI_SPECTRAL_COUNT_DEFAULT;
	ar->spectral.config.fft_size = WMI_SPECTRAL_FFT_SIZE_DEFAULT;

	return 0;
}

int ath10k_spectral_vif_stop(struct ath10k_vif *arvif)
{
	if (!arvif->spectral_enabled)
		return 0;

	return ath10k_spectral_scan_config(arvif->ar, SPECTRAL_DISABLED);
}

int ath10k_spectral_create(struct ath10k *ar)
{
	/* The buffer size covers whole channels in dual bands up to 128 bins.
	 * Scan with bigger than 128 bins needs to be run on single band each.
	 */
	ar->spectral.rfs_chan_spec_scan = relay_open("spectral_scan",
						     ar->debug.debugfs_phy,
						     1140, 2500,
						     &rfs_spec_scan_cb, NULL);
	debugfs_create_file("spectral_scan_ctl",
			    0600,
			    ar->debug.debugfs_phy, ar,
			    &fops_spec_scan_ctl);
	debugfs_create_file("spectral_count",
			    0600,
			    ar->debug.debugfs_phy, ar,
			    &fops_spectral_count);
	debugfs_create_file("spectral_bins",
			    0600,
			    ar->debug.debugfs_phy, ar,
			    &fops_spectral_bins);

	return 0;
}

void ath10k_spectral_destroy(struct ath10k *ar)
{
	if (ar->spectral.rfs_chan_spec_scan) {
		relay_close(ar->spectral.rfs_chan_spec_scan);
		ar->spectral.rfs_chan_spec_scan = NULL;
	}
}
