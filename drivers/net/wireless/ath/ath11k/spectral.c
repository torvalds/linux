// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/relay.h>
#include "core.h"
#include "debug.h"

#define ATH11K_SPECTRAL_NUM_RESP_PER_EVENT	2
#define ATH11K_SPECTRAL_EVENT_TIMEOUT_MS	1

#define ATH11K_SPECTRAL_DWORD_SIZE		4
#define ATH11K_SPECTRAL_MIN_BINS		32
#define ATH11K_SPECTRAL_MIN_IB_BINS		(ATH11K_SPECTRAL_MIN_BINS >> 1)
#define ATH11K_SPECTRAL_MAX_IB_BINS(x)	((x)->hw_params.spectral.max_fft_bins >> 1)

#define ATH11K_SPECTRAL_SCAN_COUNT_MAX		4095

/* Max channel computed by sum of 2g and 5g band channels */
#define ATH11K_SPECTRAL_TOTAL_CHANNEL		41
#define ATH11K_SPECTRAL_SAMPLES_PER_CHANNEL	70
#define ATH11K_SPECTRAL_PER_SAMPLE_SIZE(x)	(sizeof(struct fft_sample_ath11k) + \
						 ATH11K_SPECTRAL_MAX_IB_BINS(x))
#define ATH11K_SPECTRAL_TOTAL_SAMPLE		(ATH11K_SPECTRAL_TOTAL_CHANNEL * \
						 ATH11K_SPECTRAL_SAMPLES_PER_CHANNEL)
#define ATH11K_SPECTRAL_SUB_BUFF_SIZE(x)	ATH11K_SPECTRAL_PER_SAMPLE_SIZE(x)
#define ATH11K_SPECTRAL_NUM_SUB_BUF		ATH11K_SPECTRAL_TOTAL_SAMPLE

#define ATH11K_SPECTRAL_20MHZ			20
#define ATH11K_SPECTRAL_40MHZ			40
#define ATH11K_SPECTRAL_80MHZ			80
#define ATH11K_SPECTRAL_160MHZ			160

#define ATH11K_SPECTRAL_SIGNATURE		0xFA

#define ATH11K_SPECTRAL_TAG_RADAR_SUMMARY	0x0
#define ATH11K_SPECTRAL_TAG_RADAR_FFT		0x1
#define ATH11K_SPECTRAL_TAG_SCAN_SUMMARY	0x2
#define ATH11K_SPECTRAL_TAG_SCAN_SEARCH		0x3

#define SPECTRAL_TLV_HDR_LEN				GENMASK(15, 0)
#define SPECTRAL_TLV_HDR_TAG				GENMASK(23, 16)
#define SPECTRAL_TLV_HDR_SIGN				GENMASK(31, 24)

#define SPECTRAL_SUMMARY_INFO0_AGC_TOTAL_GAIN		GENMASK(7, 0)
#define SPECTRAL_SUMMARY_INFO0_OB_FLAG			BIT(8)
#define SPECTRAL_SUMMARY_INFO0_GRP_IDX			GENMASK(16, 9)
#define SPECTRAL_SUMMARY_INFO0_RECENT_RFSAT		BIT(17)
#define SPECTRAL_SUMMARY_INFO0_INBAND_PWR_DB		GENMASK(27, 18)
#define SPECTRAL_SUMMARY_INFO0_FALSE_SCAN		BIT(28)
#define SPECTRAL_SUMMARY_INFO0_DETECTOR_ID		GENMASK(30, 29)
#define SPECTRAL_SUMMARY_INFO0_PRI80			BIT(31)

#define SPECTRAL_SUMMARY_INFO2_PEAK_SIGNED_IDX		GENMASK(11, 0)
#define SPECTRAL_SUMMARY_INFO2_PEAK_MAGNITUDE		GENMASK(21, 12)
#define SPECTRAL_SUMMARY_INFO2_NARROWBAND_MASK		GENMASK(29, 22)
#define SPECTRAL_SUMMARY_INFO2_GAIN_CHANGE		BIT(30)

struct spectral_tlv {
	__le32 timestamp;
	__le32 header;
} __packed;

struct spectral_summary_fft_report {
	__le32 timestamp;
	__le32 tlv_header;
	__le32 info0;
	__le32 reserve0;
	__le32 info2;
	__le32 reserve1;
} __packed;

struct ath11k_spectral_summary_report {
	struct wmi_dma_buf_release_meta_data meta;
	u32 timestamp;
	u8 agc_total_gain;
	u8 grp_idx;
	u16 inb_pwr_db;
	s16 peak_idx;
	u16 peak_mag;
	u8 detector_id;
	bool out_of_band_flag;
	bool rf_saturation;
	bool primary80;
	bool gain_change;
	bool false_scan;
};

#define SPECTRAL_FFT_REPORT_INFO0_DETECTOR_ID		GENMASK(1, 0)
#define SPECTRAL_FFT_REPORT_INFO0_FFT_NUM		GENMASK(4, 2)
#define SPECTRAL_FFT_REPORT_INFO0_RADAR_CHECK		GENMASK(16, 5)
#define SPECTRAL_FFT_REPORT_INFO0_PEAK_SIGNED_IDX	GENMASK(27, 17)
#define SPECTRAL_FFT_REPORT_INFO0_CHAIN_IDX		GENMASK(30, 28)

#define SPECTRAL_FFT_REPORT_INFO1_BASE_PWR_DB		GENMASK(8, 0)
#define SPECTRAL_FFT_REPORT_INFO1_TOTAL_GAIN_DB		GENMASK(16, 9)

#define SPECTRAL_FFT_REPORT_INFO2_NUM_STRONG_BINS	GENMASK(7, 0)
#define SPECTRAL_FFT_REPORT_INFO2_PEAK_MAGNITUDE	GENMASK(17, 8)
#define SPECTRAL_FFT_REPORT_INFO2_AVG_PWR_DB		GENMASK(24, 18)
#define SPECTRAL_FFT_REPORT_INFO2_REL_PWR_DB		GENMASK(31, 25)

struct spectral_search_fft_report {
	__le32 timestamp;
	__le32 tlv_header;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 reserve0;
	u8 bins[];
} __packed;

struct ath11k_spectral_search_report {
	u32 timestamp;
	u8 detector_id;
	u8 fft_count;
	u16 radar_check;
	s16 peak_idx;
	u8 chain_idx;
	u16 base_pwr_db;
	u8 total_gain_db;
	u8 strong_bin_count;
	u16 peak_mag;
	u8 avg_pwr_db;
	u8 rel_pwr_db;
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

static const struct rchan_callbacks rfs_scan_cb = {
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};

static struct ath11k_vif *ath11k_spectral_get_vdev(struct ath11k *ar)
{
	struct ath11k_vif *arvif;

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

static int ath11k_spectral_scan_trigger(struct ath11k *ar)
{
	struct ath11k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	arvif = ath11k_spectral_get_vdev(ar);
	if (!arvif)
		return -ENODEV;

	if (ar->spectral.mode == ATH11K_SPECTRAL_DISABLED)
		return 0;

	ar->spectral.is_primary = true;

	ret = ath11k_wmi_vdev_spectral_enable(ar, arvif->vdev_id,
					      ATH11K_WMI_SPECTRAL_TRIGGER_CMD_CLEAR,
					      ATH11K_WMI_SPECTRAL_ENABLE_CMD_ENABLE);
	if (ret)
		return ret;

	ret = ath11k_wmi_vdev_spectral_enable(ar, arvif->vdev_id,
					      ATH11K_WMI_SPECTRAL_TRIGGER_CMD_TRIGGER,
					      ATH11K_WMI_SPECTRAL_ENABLE_CMD_ENABLE);
	if (ret)
		return ret;

	return 0;
}

static int ath11k_spectral_scan_config(struct ath11k *ar,
				       enum ath11k_spectral_mode mode)
{
	struct ath11k_wmi_vdev_spectral_conf_param param = { 0 };
	struct ath11k_vif *arvif;
	int ret, count;

	lockdep_assert_held(&ar->conf_mutex);

	arvif = ath11k_spectral_get_vdev(ar);
	if (!arvif)
		return -ENODEV;

	arvif->spectral_enabled = (mode != ATH11K_SPECTRAL_DISABLED);

	spin_lock_bh(&ar->spectral.lock);
	ar->spectral.mode = mode;
	spin_unlock_bh(&ar->spectral.lock);

	ret = ath11k_wmi_vdev_spectral_enable(ar, arvif->vdev_id,
					      ATH11K_WMI_SPECTRAL_TRIGGER_CMD_CLEAR,
					      ATH11K_WMI_SPECTRAL_ENABLE_CMD_DISABLE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to enable spectral scan: %d\n", ret);
		return ret;
	}

	if (mode == ATH11K_SPECTRAL_DISABLED)
		return 0;

	if (mode == ATH11K_SPECTRAL_BACKGROUND)
		count = ATH11K_WMI_SPECTRAL_COUNT_DEFAULT;
	else
		count = max_t(u16, 1, ar->spectral.count);

	param.vdev_id = arvif->vdev_id;
	param.scan_count = count;
	param.scan_fft_size = ar->spectral.fft_size;
	param.scan_period = ATH11K_WMI_SPECTRAL_PERIOD_DEFAULT;
	param.scan_priority = ATH11K_WMI_SPECTRAL_PRIORITY_DEFAULT;
	param.scan_gc_ena = ATH11K_WMI_SPECTRAL_GC_ENA_DEFAULT;
	param.scan_restart_ena = ATH11K_WMI_SPECTRAL_RESTART_ENA_DEFAULT;
	param.scan_noise_floor_ref = ATH11K_WMI_SPECTRAL_NOISE_FLOOR_REF_DEFAULT;
	param.scan_init_delay = ATH11K_WMI_SPECTRAL_INIT_DELAY_DEFAULT;
	param.scan_nb_tone_thr = ATH11K_WMI_SPECTRAL_NB_TONE_THR_DEFAULT;
	param.scan_str_bin_thr = ATH11K_WMI_SPECTRAL_STR_BIN_THR_DEFAULT;
	param.scan_wb_rpt_mode = ATH11K_WMI_SPECTRAL_WB_RPT_MODE_DEFAULT;
	param.scan_rssi_rpt_mode = ATH11K_WMI_SPECTRAL_RSSI_RPT_MODE_DEFAULT;
	param.scan_rssi_thr = ATH11K_WMI_SPECTRAL_RSSI_THR_DEFAULT;
	param.scan_pwr_format = ATH11K_WMI_SPECTRAL_PWR_FORMAT_DEFAULT;
	param.scan_rpt_mode = ATH11K_WMI_SPECTRAL_RPT_MODE_DEFAULT;
	param.scan_bin_scale = ATH11K_WMI_SPECTRAL_BIN_SCALE_DEFAULT;
	param.scan_dbm_adj = ATH11K_WMI_SPECTRAL_DBM_ADJ_DEFAULT;
	param.scan_chn_mask = ATH11K_WMI_SPECTRAL_CHN_MASK_DEFAULT;

	ret = ath11k_wmi_vdev_spectral_conf(ar, &param);
	if (ret) {
		ath11k_warn(ar->ab, "failed to configure spectral scan: %d\n", ret);
		return ret;
	}

	return 0;
}

static ssize_t ath11k_read_file_spec_scan_ctl(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char *mode = "";
	size_t len;
	enum ath11k_spectral_mode spectral_mode;

	mutex_lock(&ar->conf_mutex);
	spectral_mode = ar->spectral.mode;
	mutex_unlock(&ar->conf_mutex);

	switch (spectral_mode) {
	case ATH11K_SPECTRAL_DISABLED:
		mode = "disable";
		break;
	case ATH11K_SPECTRAL_BACKGROUND:
		mode = "background";
		break;
	case ATH11K_SPECTRAL_MANUAL:
		mode = "manual";
		break;
	}

	len = strlen(mode);
	return simple_read_from_buffer(user_buf, count, ppos, mode, len);
}

static ssize_t ath11k_write_file_spec_scan_ctl(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	ssize_t len;
	int ret;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	mutex_lock(&ar->conf_mutex);

	if (strncmp("trigger", buf, 7) == 0) {
		if (ar->spectral.mode == ATH11K_SPECTRAL_MANUAL ||
		    ar->spectral.mode == ATH11K_SPECTRAL_BACKGROUND) {
			/* reset the configuration to adopt possibly changed
			 * debugfs parameters
			 */
			ret = ath11k_spectral_scan_config(ar, ar->spectral.mode);
			if (ret) {
				ath11k_warn(ar->ab, "failed to reconfigure spectral scan: %d\n",
					    ret);
				goto unlock;
			}

			ret = ath11k_spectral_scan_trigger(ar);
			if (ret) {
				ath11k_warn(ar->ab, "failed to trigger spectral scan: %d\n",
					    ret);
			}
		} else {
			ret = -EINVAL;
		}
	} else if (strncmp("background", buf, 10) == 0) {
		ret = ath11k_spectral_scan_config(ar, ATH11K_SPECTRAL_BACKGROUND);
	} else if (strncmp("manual", buf, 6) == 0) {
		ret = ath11k_spectral_scan_config(ar, ATH11K_SPECTRAL_MANUAL);
	} else if (strncmp("disable", buf, 7) == 0) {
		ret = ath11k_spectral_scan_config(ar, ATH11K_SPECTRAL_DISABLED);
	} else {
		ret = -EINVAL;
	}

unlock:
	mutex_unlock(&ar->conf_mutex);

	if (ret)
		return ret;

	return count;
}

static const struct file_operations fops_scan_ctl = {
	.read = ath11k_read_file_spec_scan_ctl,
	.write = ath11k_write_file_spec_scan_ctl,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_read_file_spectral_count(struct file *file,
					       char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	size_t len;
	u16 spectral_count;

	mutex_lock(&ar->conf_mutex);
	spectral_count = ar->spectral.count;
	mutex_unlock(&ar->conf_mutex);

	len = sprintf(buf, "%d\n", spectral_count);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath11k_write_file_spectral_count(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &val);
	if (ret)
		return ret;

	if (val > ATH11K_SPECTRAL_SCAN_COUNT_MAX)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	ar->spectral.count = val;
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_scan_count = {
	.read = ath11k_read_file_spectral_count,
	.write = ath11k_write_file_spectral_count,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_read_file_spectral_bins(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	unsigned int bins, fft_size;
	size_t len;

	mutex_lock(&ar->conf_mutex);

	fft_size = ar->spectral.fft_size;
	bins = 1 << fft_size;

	mutex_unlock(&ar->conf_mutex);

	len = sprintf(buf, "%d\n", bins);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath11k_write_file_spectral_bins(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &val);
	if (ret)
		return ret;

	if (val < ATH11K_SPECTRAL_MIN_BINS ||
	    val > ar->ab->hw_params.spectral.max_fft_bins)
		return -EINVAL;

	if (!is_power_of_2(val))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	ar->spectral.fft_size = ilog2(val);
	mutex_unlock(&ar->conf_mutex);

	return count;
}

static const struct file_operations fops_scan_bins = {
	.read = ath11k_read_file_spectral_bins,
	.write = ath11k_write_file_spectral_bins,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath11k_spectral_pull_summary(struct ath11k *ar,
					struct wmi_dma_buf_release_meta_data *meta,
					struct spectral_summary_fft_report *summary,
					struct ath11k_spectral_summary_report *report)
{
	report->timestamp = __le32_to_cpu(summary->timestamp);
	report->agc_total_gain = FIELD_GET(SPECTRAL_SUMMARY_INFO0_AGC_TOTAL_GAIN,
					   __le32_to_cpu(summary->info0));
	report->out_of_band_flag = FIELD_GET(SPECTRAL_SUMMARY_INFO0_OB_FLAG,
					     __le32_to_cpu(summary->info0));
	report->grp_idx = FIELD_GET(SPECTRAL_SUMMARY_INFO0_GRP_IDX,
				    __le32_to_cpu(summary->info0));
	report->rf_saturation = FIELD_GET(SPECTRAL_SUMMARY_INFO0_RECENT_RFSAT,
					  __le32_to_cpu(summary->info0));
	report->inb_pwr_db = FIELD_GET(SPECTRAL_SUMMARY_INFO0_INBAND_PWR_DB,
				       __le32_to_cpu(summary->info0));
	report->false_scan = FIELD_GET(SPECTRAL_SUMMARY_INFO0_FALSE_SCAN,
				       __le32_to_cpu(summary->info0));
	report->detector_id = FIELD_GET(SPECTRAL_SUMMARY_INFO0_DETECTOR_ID,
					__le32_to_cpu(summary->info0));
	report->primary80 = FIELD_GET(SPECTRAL_SUMMARY_INFO0_PRI80,
				      __le32_to_cpu(summary->info0));
	report->peak_idx = FIELD_GET(SPECTRAL_SUMMARY_INFO2_PEAK_SIGNED_IDX,
				     __le32_to_cpu(summary->info2));
	report->peak_mag = FIELD_GET(SPECTRAL_SUMMARY_INFO2_PEAK_MAGNITUDE,
				     __le32_to_cpu(summary->info2));
	report->gain_change = FIELD_GET(SPECTRAL_SUMMARY_INFO2_GAIN_CHANGE,
					__le32_to_cpu(summary->info2));

	memcpy(&report->meta, meta, sizeof(*meta));

	return 0;
}

static int ath11k_spectral_pull_search(struct ath11k *ar,
				       struct spectral_search_fft_report *search,
				       struct ath11k_spectral_search_report *report)
{
	report->timestamp = __le32_to_cpu(search->timestamp);
	report->detector_id = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_DETECTOR_ID,
					__le32_to_cpu(search->info0));
	report->fft_count = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_FFT_NUM,
				      __le32_to_cpu(search->info0));
	report->radar_check = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_RADAR_CHECK,
					__le32_to_cpu(search->info0));
	report->peak_idx = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_PEAK_SIGNED_IDX,
				     __le32_to_cpu(search->info0));
	report->chain_idx = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_CHAIN_IDX,
				      __le32_to_cpu(search->info0));
	report->base_pwr_db = FIELD_GET(SPECTRAL_FFT_REPORT_INFO1_BASE_PWR_DB,
					__le32_to_cpu(search->info1));
	report->total_gain_db = FIELD_GET(SPECTRAL_FFT_REPORT_INFO1_TOTAL_GAIN_DB,
					  __le32_to_cpu(search->info1));
	report->strong_bin_count = FIELD_GET(SPECTRAL_FFT_REPORT_INFO2_NUM_STRONG_BINS,
					     __le32_to_cpu(search->info2));
	report->peak_mag = FIELD_GET(SPECTRAL_FFT_REPORT_INFO2_PEAK_MAGNITUDE,
				     __le32_to_cpu(search->info2));
	report->avg_pwr_db = FIELD_GET(SPECTRAL_FFT_REPORT_INFO2_AVG_PWR_DB,
				       __le32_to_cpu(search->info2));
	report->rel_pwr_db = FIELD_GET(SPECTRAL_FFT_REPORT_INFO2_REL_PWR_DB,
				       __le32_to_cpu(search->info2));

	return 0;
}

static u8 ath11k_spectral_get_max_exp(s8 max_index, u8 max_magnitude,
				      int bin_len, u8 *bins)
{
	int dc_pos;
	u8 max_exp;

	dc_pos = bin_len / 2;

	/* peak index outside of bins */
	if (dc_pos <= max_index || -dc_pos >= max_index)
		return 0;

	for (max_exp = 0; max_exp < 8; max_exp++) {
		if (bins[dc_pos + max_index] == (max_magnitude >> max_exp))
			break;
	}

	/* max_exp not found */
	if (bins[dc_pos + max_index] != (max_magnitude >> max_exp))
		return 0;

	return max_exp;
}

static void ath11k_spectral_parse_fft(u8 *outbins, u8 *inbins, int num_bins, u8 fft_sz)
{
	int i, j;

	i = 0;
	j = 0;
	while (i < num_bins) {
		outbins[i] = inbins[j];
		i++;
		j += fft_sz;
	}
}

static
int ath11k_spectral_process_fft(struct ath11k *ar,
				struct ath11k_spectral_summary_report *summary,
				void *data,
				struct fft_sample_ath11k *fft_sample,
				u32 data_len)
{
	struct ath11k_base *ab = ar->ab;
	struct spectral_search_fft_report *fft_report = data;
	struct ath11k_spectral_search_report search;
	struct spectral_tlv *tlv;
	int tlv_len, bin_len, num_bins;
	u16 length, freq;
	u8 chan_width_mhz, bin_sz;
	int ret;
	u32 check_length;
	bool fragment_sample = false;

	lockdep_assert_held(&ar->spectral.lock);

	if (!ab->hw_params.spectral.fft_sz) {
		ath11k_warn(ab, "invalid bin size type for hw rev %d\n",
			    ab->hw_rev);
		return -EINVAL;
	}

	tlv = data;
	tlv_len = FIELD_GET(SPECTRAL_TLV_HDR_LEN, __le32_to_cpu(tlv->header));
	/* convert Dword into bytes */
	tlv_len *= ATH11K_SPECTRAL_DWORD_SIZE;
	bin_len = tlv_len - ab->hw_params.spectral.fft_hdr_len;

	if (data_len < (bin_len + sizeof(*fft_report))) {
		ath11k_warn(ab, "mismatch in expected bin len %d and data len %d\n",
			    bin_len, data_len);
		return -EINVAL;
	}

	bin_sz = ab->hw_params.spectral.fft_sz + ab->hw_params.spectral.fft_pad_sz;
	num_bins = bin_len / bin_sz;
	/* Only In-band bins are useful to user for visualize */
	num_bins >>= 1;

	if (num_bins < ATH11K_SPECTRAL_MIN_IB_BINS ||
	    num_bins > ATH11K_SPECTRAL_MAX_IB_BINS(ab) ||
	    !is_power_of_2(num_bins)) {
		ath11k_warn(ab, "Invalid num of bins %d\n", num_bins);
		return -EINVAL;
	}

	check_length = sizeof(*fft_report) + (num_bins * ab->hw_params.spectral.fft_sz);
	ret = ath11k_dbring_validate_buffer(ar, data, check_length);
	if (ret) {
		ath11k_warn(ar->ab, "found magic value in fft data, dropping\n");
		return ret;
	}

	ret = ath11k_spectral_pull_search(ar, data, &search);
	if (ret) {
		ath11k_warn(ab, "failed to pull search report %d\n", ret);
		return ret;
	}

	chan_width_mhz = summary->meta.ch_width;

	switch (chan_width_mhz) {
	case ATH11K_SPECTRAL_20MHZ:
	case ATH11K_SPECTRAL_40MHZ:
	case ATH11K_SPECTRAL_80MHZ:
		fft_sample->chan_width_mhz = chan_width_mhz;
		break;
	case ATH11K_SPECTRAL_160MHZ:
		if (ab->hw_params.spectral.fragment_160mhz) {
			chan_width_mhz /= 2;
			fragment_sample = true;
		}
		fft_sample->chan_width_mhz = chan_width_mhz;
		break;
	default:
		ath11k_warn(ab, "invalid channel width %d\n", chan_width_mhz);
		return -EINVAL;
	}

	length = sizeof(*fft_sample) - sizeof(struct fft_sample_tlv) + num_bins;
	fft_sample->tlv.type = ATH_FFT_SAMPLE_ATH11K;
	fft_sample->tlv.length = __cpu_to_be16(length);

	fft_sample->tsf = __cpu_to_be32(search.timestamp);
	fft_sample->max_magnitude = __cpu_to_be16(search.peak_mag);
	fft_sample->max_index = FIELD_GET(SPECTRAL_FFT_REPORT_INFO0_PEAK_SIGNED_IDX,
					  __le32_to_cpu(fft_report->info0));

	summary->inb_pwr_db >>= 1;
	fft_sample->rssi = __cpu_to_be16(summary->inb_pwr_db);
	fft_sample->noise = __cpu_to_be32(summary->meta.noise_floor[search.chain_idx]);

	freq = summary->meta.freq1;
	fft_sample->freq1 = __cpu_to_be16(freq);

	freq = summary->meta.freq2;
	fft_sample->freq2 = __cpu_to_be16(freq);

	/* If freq2 is available then the spectral scan results are fragmented
	 * as primary and secondary
	 */
	if (fragment_sample && freq) {
		if (!ar->spectral.is_primary)
			fft_sample->freq1 = cpu_to_be16(freq);

		/* We have to toggle the is_primary to handle the next report */
		ar->spectral.is_primary = !ar->spectral.is_primary;
	}

	ath11k_spectral_parse_fft(fft_sample->data, fft_report->bins, num_bins,
				  ab->hw_params.spectral.fft_sz);

	fft_sample->max_exp = ath11k_spectral_get_max_exp(fft_sample->max_index,
							  search.peak_mag,
							  num_bins,
							  fft_sample->data);

	if (ar->spectral.rfs_scan)
		relay_write(ar->spectral.rfs_scan, fft_sample,
			    length + sizeof(struct fft_sample_tlv));

	return 0;
}

static int ath11k_spectral_process_data(struct ath11k *ar,
					struct ath11k_dbring_data *param)
{
	struct ath11k_base *ab = ar->ab;
	struct spectral_tlv *tlv;
	struct spectral_summary_fft_report *summary = NULL;
	struct ath11k_spectral_summary_report summ_rpt;
	struct fft_sample_ath11k *fft_sample = NULL;
	u8 *data;
	u32 data_len, i;
	u8 sign, tag;
	int tlv_len, sample_sz;
	int ret;
	bool quit = false;

	spin_lock_bh(&ar->spectral.lock);

	if (!ar->spectral.enabled) {
		ret = -EINVAL;
		goto unlock;
	}

	sample_sz = sizeof(*fft_sample) + ATH11K_SPECTRAL_MAX_IB_BINS(ab);
	fft_sample = kmalloc(sample_sz, GFP_ATOMIC);
	if (!fft_sample) {
		ret = -ENOBUFS;
		goto unlock;
	}

	data = param->data;
	data_len = param->data_sz;
	i = 0;
	while (!quit && (i < data_len)) {
		if ((i + sizeof(*tlv)) > data_len) {
			ath11k_warn(ab, "failed to parse spectral tlv hdr at bytes %d\n",
				    i);
			ret = -EINVAL;
			goto err;
		}

		tlv = (struct spectral_tlv *)&data[i];
		sign = FIELD_GET(SPECTRAL_TLV_HDR_SIGN,
				 __le32_to_cpu(tlv->header));
		if (sign != ATH11K_SPECTRAL_SIGNATURE) {
			ath11k_warn(ab, "Invalid sign 0x%x at bytes %d\n",
				    sign, i);
			ret = -EINVAL;
			goto err;
		}

		tlv_len = FIELD_GET(SPECTRAL_TLV_HDR_LEN,
				    __le32_to_cpu(tlv->header));
		/* convert Dword into bytes */
		tlv_len *= ATH11K_SPECTRAL_DWORD_SIZE;
		if ((i + sizeof(*tlv) + tlv_len) > data_len) {
			ath11k_warn(ab, "failed to parse spectral tlv payload at bytes %d tlv_len:%d data_len:%d\n",
				    i, tlv_len, data_len);
			ret = -EINVAL;
			goto err;
		}

		tag = FIELD_GET(SPECTRAL_TLV_HDR_TAG,
				__le32_to_cpu(tlv->header));
		switch (tag) {
		case ATH11K_SPECTRAL_TAG_SCAN_SUMMARY:
			/* HW bug in tlv length of summary report,
			 * HW report 3 DWORD size but the data payload
			 * is 4 DWORD size (16 bytes).
			 * Need to remove this workaround once HW bug fixed
			 */
			tlv_len = sizeof(*summary) - sizeof(*tlv) +
				  ab->hw_params.spectral.summary_pad_sz;

			if (tlv_len < (sizeof(*summary) - sizeof(*tlv))) {
				ath11k_warn(ab, "failed to parse spectral summary at bytes %d tlv_len:%d\n",
					    i, tlv_len);
				ret = -EINVAL;
				goto err;
			}

			ret = ath11k_dbring_validate_buffer(ar, data, tlv_len);
			if (ret) {
				ath11k_warn(ar->ab, "found magic value in spectral summary, dropping\n");
				goto err;
			}

			summary = (struct spectral_summary_fft_report *)tlv;
			ath11k_spectral_pull_summary(ar, &param->meta,
						     summary, &summ_rpt);
			break;
		case ATH11K_SPECTRAL_TAG_SCAN_SEARCH:
			if (tlv_len < (sizeof(struct spectral_search_fft_report) -
				       sizeof(*tlv))) {
				ath11k_warn(ab, "failed to parse spectral search fft at bytes %d\n",
					    i);
				ret = -EINVAL;
				goto err;
			}

			memset(fft_sample, 0, sample_sz);
			ret = ath11k_spectral_process_fft(ar, &summ_rpt, tlv,
							  fft_sample,
							  data_len - i);
			if (ret) {
				ath11k_warn(ab, "failed to process spectral fft at bytes %d\n",
					    i);
				goto err;
			}
			quit = true;
			break;
		}

		i += sizeof(*tlv) + tlv_len;
	}

	ret = 0;

err:
	kfree(fft_sample);
unlock:
	spin_unlock_bh(&ar->spectral.lock);
	return ret;
}

static int ath11k_spectral_ring_alloc(struct ath11k *ar,
				      struct ath11k_dbring_cap *db_cap)
{
	struct ath11k_spectral *sp = &ar->spectral;
	int ret;

	ret = ath11k_dbring_srng_setup(ar, &sp->rx_ring,
				       0, db_cap->min_elem);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring\n");
		return ret;
	}

	ath11k_dbring_set_cfg(ar, &sp->rx_ring,
			      ATH11K_SPECTRAL_NUM_RESP_PER_EVENT,
			      ATH11K_SPECTRAL_EVENT_TIMEOUT_MS,
			      ath11k_spectral_process_data);

	ret = ath11k_dbring_buf_setup(ar, &sp->rx_ring, db_cap);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring buffer\n");
		goto srng_cleanup;
	}

	ret = ath11k_dbring_wmi_cfg_setup(ar, &sp->rx_ring,
					  WMI_DIRECT_BUF_SPECTRAL);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring cfg\n");
		goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	ath11k_dbring_buf_cleanup(ar, &sp->rx_ring);
srng_cleanup:
	ath11k_dbring_srng_cleanup(ar, &sp->rx_ring);
	return ret;
}

static inline void ath11k_spectral_ring_free(struct ath11k *ar)
{
	struct ath11k_spectral *sp = &ar->spectral;

	ath11k_dbring_srng_cleanup(ar, &sp->rx_ring);
	ath11k_dbring_buf_cleanup(ar, &sp->rx_ring);
}

static inline void ath11k_spectral_debug_unregister(struct ath11k *ar)
{
	debugfs_remove(ar->spectral.scan_bins);
	ar->spectral.scan_bins = NULL;

	debugfs_remove(ar->spectral.scan_count);
	ar->spectral.scan_count = NULL;

	debugfs_remove(ar->spectral.scan_ctl);
	ar->spectral.scan_ctl = NULL;

	if (ar->spectral.rfs_scan) {
		relay_close(ar->spectral.rfs_scan);
		ar->spectral.rfs_scan = NULL;
	}
}

int ath11k_spectral_vif_stop(struct ath11k_vif *arvif)
{
	if (!arvif->spectral_enabled)
		return 0;

	return ath11k_spectral_scan_config(arvif->ar, ATH11K_SPECTRAL_DISABLED);
}

void ath11k_spectral_reset_buffer(struct ath11k *ar)
{
	if (!ar->spectral.enabled)
		return;

	if (ar->spectral.rfs_scan)
		relay_reset(ar->spectral.rfs_scan);
}

void ath11k_spectral_deinit(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_spectral *sp;
	int i;

	for (i = 0; i <  ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		sp = &ar->spectral;

		if (!sp->enabled)
			continue;

		mutex_lock(&ar->conf_mutex);
		ath11k_spectral_scan_config(ar, ATH11K_SPECTRAL_DISABLED);
		mutex_unlock(&ar->conf_mutex);

		spin_lock_bh(&sp->lock);
		sp->enabled = false;
		spin_unlock_bh(&sp->lock);

		ath11k_spectral_debug_unregister(ar);
		ath11k_spectral_ring_free(ar);
	}
}

static inline int ath11k_spectral_debug_register(struct ath11k *ar)
{
	int ret;

	ar->spectral.rfs_scan = relay_open("spectral_scan",
					   ar->debug.debugfs_pdev,
					   ATH11K_SPECTRAL_SUB_BUFF_SIZE(ar->ab),
					   ATH11K_SPECTRAL_NUM_SUB_BUF,
					   &rfs_scan_cb, NULL);
	if (!ar->spectral.rfs_scan) {
		ath11k_warn(ar->ab, "failed to open relay in pdev %d\n",
			    ar->pdev_idx);
		return -EINVAL;
	}

	ar->spectral.scan_ctl = debugfs_create_file("spectral_scan_ctl",
						    0600,
						    ar->debug.debugfs_pdev, ar,
						    &fops_scan_ctl);
	if (!ar->spectral.scan_ctl) {
		ath11k_warn(ar->ab, "failed to open debugfs in pdev %d\n",
			    ar->pdev_idx);
		ret = -EINVAL;
		goto debug_unregister;
	}

	ar->spectral.scan_count = debugfs_create_file("spectral_count",
						      0600,
						      ar->debug.debugfs_pdev, ar,
						      &fops_scan_count);
	if (!ar->spectral.scan_count) {
		ath11k_warn(ar->ab, "failed to open debugfs in pdev %d\n",
			    ar->pdev_idx);
		ret = -EINVAL;
		goto debug_unregister;
	}

	ar->spectral.scan_bins = debugfs_create_file("spectral_bins",
						     0600,
						     ar->debug.debugfs_pdev, ar,
						     &fops_scan_bins);
	if (!ar->spectral.scan_bins) {
		ath11k_warn(ar->ab, "failed to open debugfs in pdev %d\n",
			    ar->pdev_idx);
		ret = -EINVAL;
		goto debug_unregister;
	}

	return 0;

debug_unregister:
	ath11k_spectral_debug_unregister(ar);
	return ret;
}

int ath11k_spectral_init(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_spectral *sp;
	struct ath11k_dbring_cap db_cap;
	int ret;
	int i;

	if (!test_bit(WMI_TLV_SERVICE_FREQINFO_IN_METADATA,
		      ab->wmi_ab.svc_map))
		return 0;

	if (!ab->hw_params.spectral.fft_sz)
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		sp = &ar->spectral;

		ret = ath11k_dbring_get_cap(ar->ab, ar->pdev_idx,
					    WMI_DIRECT_BUF_SPECTRAL,
					    &db_cap);
		if (ret)
			continue;

		idr_init(&sp->rx_ring.bufs_idr);
		spin_lock_init(&sp->rx_ring.idr_lock);
		spin_lock_init(&sp->lock);

		ret = ath11k_spectral_ring_alloc(ar, &db_cap);
		if (ret) {
			ath11k_warn(ab, "failed to init spectral ring for pdev %d\n",
				    i);
			goto deinit;
		}

		spin_lock_bh(&sp->lock);

		sp->mode = ATH11K_SPECTRAL_DISABLED;
		sp->count = ATH11K_WMI_SPECTRAL_COUNT_DEFAULT;
		sp->fft_size = ATH11K_WMI_SPECTRAL_FFT_SIZE_DEFAULT;
		sp->enabled = true;

		spin_unlock_bh(&sp->lock);

		ret = ath11k_spectral_debug_register(ar);
		if (ret) {
			ath11k_warn(ab, "failed to register spectral for pdev %d\n",
				    i);
			goto deinit;
		}
	}

	return 0;

deinit:
	ath11k_spectral_deinit(ab);
	return ret;
}

enum ath11k_spectral_mode ath11k_spectral_get_mode(struct ath11k *ar)
{
	if (ar->spectral.enabled)
		return ar->spectral.mode;
	else
		return ATH11K_SPECTRAL_DISABLED;
}

struct ath11k_dbring *ath11k_spectral_get_dbring(struct ath11k *ar)
{
	if (ar->spectral.enabled)
		return &ar->spectral.rx_ring;
	else
		return NULL;
}
