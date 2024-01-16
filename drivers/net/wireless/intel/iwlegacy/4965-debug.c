// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*
* Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
*
* Contact Information:
*  Intel Linux Wireless <ilw@linux.intel.com>
* Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*****************************************************************************/
#include "common.h"
#include "4965.h"

static const char *fmt_value = "  %-30s %10u\n";
static const char *fmt_table = "  %-30s %10u  %10u  %10u  %10u\n";
static const char *fmt_header =
    "%-32s    current  cumulative       delta         max\n";

static int
il4965_stats_flag(struct il_priv *il, char *buf, int bufsz)
{
	int p = 0;
	u32 flag;

	flag = le32_to_cpu(il->_4965.stats.flag);

	p += scnprintf(buf + p, bufsz - p, "Statistics Flag(0x%X):\n", flag);
	if (flag & UCODE_STATS_CLEAR_MSK)
		p += scnprintf(buf + p, bufsz - p,
			       "\tStatistics have been cleared\n");
	p += scnprintf(buf + p, bufsz - p, "\tOperational Frequency: %s\n",
		       (flag & UCODE_STATS_FREQUENCY_MSK) ? "2.4 GHz" :
		       "5.2 GHz");
	p += scnprintf(buf + p, bufsz - p, "\tTGj Narrow Band: %s\n",
		       (flag & UCODE_STATS_NARROW_BAND_MSK) ? "enabled" :
		       "disabled");

	return p;
}

static ssize_t
il4965_ucode_rx_stats_read(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz =
	    sizeof(struct stats_rx_phy) * 40 +
	    sizeof(struct stats_rx_non_phy) * 40 +
	    sizeof(struct stats_rx_ht_phy) * 40 + 400;
	ssize_t ret;
	struct stats_rx_phy *ofdm, *accum_ofdm, *delta_ofdm, *max_ofdm;
	struct stats_rx_phy *cck, *accum_cck, *delta_cck, *max_cck;
	struct stats_rx_non_phy *general, *accum_general;
	struct stats_rx_non_phy *delta_general, *max_general;
	struct stats_rx_ht_phy *ht, *accum_ht, *delta_ht, *max_ht;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/*
	 * the statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	ofdm = &il->_4965.stats.rx.ofdm;
	cck = &il->_4965.stats.rx.cck;
	general = &il->_4965.stats.rx.general;
	ht = &il->_4965.stats.rx.ofdm_ht;
	accum_ofdm = &il->_4965.accum_stats.rx.ofdm;
	accum_cck = &il->_4965.accum_stats.rx.cck;
	accum_general = &il->_4965.accum_stats.rx.general;
	accum_ht = &il->_4965.accum_stats.rx.ofdm_ht;
	delta_ofdm = &il->_4965.delta_stats.rx.ofdm;
	delta_cck = &il->_4965.delta_stats.rx.cck;
	delta_general = &il->_4965.delta_stats.rx.general;
	delta_ht = &il->_4965.delta_stats.rx.ofdm_ht;
	max_ofdm = &il->_4965.max_delta.rx.ofdm;
	max_cck = &il->_4965.max_delta.rx.cck;
	max_general = &il->_4965.max_delta.rx.general;
	max_ht = &il->_4965.max_delta.rx.ofdm_ht;

	pos += il4965_stats_flag(il, buf, bufsz);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_header,
		      "Statistics_Rx - OFDM:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "ina_cnt:",
		      le32_to_cpu(ofdm->ina_cnt), accum_ofdm->ina_cnt,
		      delta_ofdm->ina_cnt, max_ofdm->ina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_cnt:",
		      le32_to_cpu(ofdm->fina_cnt), accum_ofdm->fina_cnt,
		      delta_ofdm->fina_cnt, max_ofdm->fina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "plcp_err:",
		      le32_to_cpu(ofdm->plcp_err), accum_ofdm->plcp_err,
		      delta_ofdm->plcp_err, max_ofdm->plcp_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_err:",
		      le32_to_cpu(ofdm->crc32_err), accum_ofdm->crc32_err,
		      delta_ofdm->crc32_err, max_ofdm->crc32_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "overrun_err:",
		      le32_to_cpu(ofdm->overrun_err), accum_ofdm->overrun_err,
		      delta_ofdm->overrun_err, max_ofdm->overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "early_overrun_err:",
		      le32_to_cpu(ofdm->early_overrun_err),
		      accum_ofdm->early_overrun_err,
		      delta_ofdm->early_overrun_err,
		      max_ofdm->early_overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_good:",
		      le32_to_cpu(ofdm->crc32_good), accum_ofdm->crc32_good,
		      delta_ofdm->crc32_good, max_ofdm->crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "false_alarm_cnt:",
		      le32_to_cpu(ofdm->false_alarm_cnt),
		      accum_ofdm->false_alarm_cnt, delta_ofdm->false_alarm_cnt,
		      max_ofdm->false_alarm_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_sync_err_cnt:",
		      le32_to_cpu(ofdm->fina_sync_err_cnt),
		      accum_ofdm->fina_sync_err_cnt,
		      delta_ofdm->fina_sync_err_cnt,
		      max_ofdm->fina_sync_err_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sfd_timeout:",
		      le32_to_cpu(ofdm->sfd_timeout), accum_ofdm->sfd_timeout,
		      delta_ofdm->sfd_timeout, max_ofdm->sfd_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_timeout:",
		      le32_to_cpu(ofdm->fina_timeout), accum_ofdm->fina_timeout,
		      delta_ofdm->fina_timeout, max_ofdm->fina_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "unresponded_rts:",
		      le32_to_cpu(ofdm->unresponded_rts),
		      accum_ofdm->unresponded_rts, delta_ofdm->unresponded_rts,
		      max_ofdm->unresponded_rts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "rxe_frame_lmt_ovrun:",
		      le32_to_cpu(ofdm->rxe_frame_limit_overrun),
		      accum_ofdm->rxe_frame_limit_overrun,
		      delta_ofdm->rxe_frame_limit_overrun,
		      max_ofdm->rxe_frame_limit_overrun);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_ack_cnt:",
		      le32_to_cpu(ofdm->sent_ack_cnt), accum_ofdm->sent_ack_cnt,
		      delta_ofdm->sent_ack_cnt, max_ofdm->sent_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_cts_cnt:",
		      le32_to_cpu(ofdm->sent_cts_cnt), accum_ofdm->sent_cts_cnt,
		      delta_ofdm->sent_cts_cnt, max_ofdm->sent_cts_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_ba_rsp_cnt:",
		      le32_to_cpu(ofdm->sent_ba_rsp_cnt),
		      accum_ofdm->sent_ba_rsp_cnt, delta_ofdm->sent_ba_rsp_cnt,
		      max_ofdm->sent_ba_rsp_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "dsp_self_kill:",
		      le32_to_cpu(ofdm->dsp_self_kill),
		      accum_ofdm->dsp_self_kill, delta_ofdm->dsp_self_kill,
		      max_ofdm->dsp_self_kill);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "mh_format_err:",
		      le32_to_cpu(ofdm->mh_format_err),
		      accum_ofdm->mh_format_err, delta_ofdm->mh_format_err,
		      max_ofdm->mh_format_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "re_acq_main_rssi_sum:",
		      le32_to_cpu(ofdm->re_acq_main_rssi_sum),
		      accum_ofdm->re_acq_main_rssi_sum,
		      delta_ofdm->re_acq_main_rssi_sum,
		      max_ofdm->re_acq_main_rssi_sum);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_header,
		      "Statistics_Rx - CCK:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "ina_cnt:",
		      le32_to_cpu(cck->ina_cnt), accum_cck->ina_cnt,
		      delta_cck->ina_cnt, max_cck->ina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_cnt:",
		      le32_to_cpu(cck->fina_cnt), accum_cck->fina_cnt,
		      delta_cck->fina_cnt, max_cck->fina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "plcp_err:",
		      le32_to_cpu(cck->plcp_err), accum_cck->plcp_err,
		      delta_cck->plcp_err, max_cck->plcp_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_err:",
		      le32_to_cpu(cck->crc32_err), accum_cck->crc32_err,
		      delta_cck->crc32_err, max_cck->crc32_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "overrun_err:",
		      le32_to_cpu(cck->overrun_err), accum_cck->overrun_err,
		      delta_cck->overrun_err, max_cck->overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "early_overrun_err:",
		      le32_to_cpu(cck->early_overrun_err),
		      accum_cck->early_overrun_err,
		      delta_cck->early_overrun_err, max_cck->early_overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_good:",
		      le32_to_cpu(cck->crc32_good), accum_cck->crc32_good,
		      delta_cck->crc32_good, max_cck->crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "false_alarm_cnt:",
		      le32_to_cpu(cck->false_alarm_cnt),
		      accum_cck->false_alarm_cnt, delta_cck->false_alarm_cnt,
		      max_cck->false_alarm_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_sync_err_cnt:",
		      le32_to_cpu(cck->fina_sync_err_cnt),
		      accum_cck->fina_sync_err_cnt,
		      delta_cck->fina_sync_err_cnt, max_cck->fina_sync_err_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sfd_timeout:",
		      le32_to_cpu(cck->sfd_timeout), accum_cck->sfd_timeout,
		      delta_cck->sfd_timeout, max_cck->sfd_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "fina_timeout:",
		      le32_to_cpu(cck->fina_timeout), accum_cck->fina_timeout,
		      delta_cck->fina_timeout, max_cck->fina_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "unresponded_rts:",
		      le32_to_cpu(cck->unresponded_rts),
		      accum_cck->unresponded_rts, delta_cck->unresponded_rts,
		      max_cck->unresponded_rts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "rxe_frame_lmt_ovrun:",
		      le32_to_cpu(cck->rxe_frame_limit_overrun),
		      accum_cck->rxe_frame_limit_overrun,
		      delta_cck->rxe_frame_limit_overrun,
		      max_cck->rxe_frame_limit_overrun);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_ack_cnt:",
		      le32_to_cpu(cck->sent_ack_cnt), accum_cck->sent_ack_cnt,
		      delta_cck->sent_ack_cnt, max_cck->sent_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_cts_cnt:",
		      le32_to_cpu(cck->sent_cts_cnt), accum_cck->sent_cts_cnt,
		      delta_cck->sent_cts_cnt, max_cck->sent_cts_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sent_ba_rsp_cnt:",
		      le32_to_cpu(cck->sent_ba_rsp_cnt),
		      accum_cck->sent_ba_rsp_cnt, delta_cck->sent_ba_rsp_cnt,
		      max_cck->sent_ba_rsp_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "dsp_self_kill:",
		      le32_to_cpu(cck->dsp_self_kill), accum_cck->dsp_self_kill,
		      delta_cck->dsp_self_kill, max_cck->dsp_self_kill);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "mh_format_err:",
		      le32_to_cpu(cck->mh_format_err), accum_cck->mh_format_err,
		      delta_cck->mh_format_err, max_cck->mh_format_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "re_acq_main_rssi_sum:",
		      le32_to_cpu(cck->re_acq_main_rssi_sum),
		      accum_cck->re_acq_main_rssi_sum,
		      delta_cck->re_acq_main_rssi_sum,
		      max_cck->re_acq_main_rssi_sum);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_header,
		      "Statistics_Rx - GENERAL:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "bogus_cts:",
		      le32_to_cpu(general->bogus_cts), accum_general->bogus_cts,
		      delta_general->bogus_cts, max_general->bogus_cts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "bogus_ack:",
		      le32_to_cpu(general->bogus_ack), accum_general->bogus_ack,
		      delta_general->bogus_ack, max_general->bogus_ack);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "non_bssid_frames:",
		      le32_to_cpu(general->non_bssid_frames),
		      accum_general->non_bssid_frames,
		      delta_general->non_bssid_frames,
		      max_general->non_bssid_frames);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "filtered_frames:",
		      le32_to_cpu(general->filtered_frames),
		      accum_general->filtered_frames,
		      delta_general->filtered_frames,
		      max_general->filtered_frames);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "non_channel_beacons:",
		      le32_to_cpu(general->non_channel_beacons),
		      accum_general->non_channel_beacons,
		      delta_general->non_channel_beacons,
		      max_general->non_channel_beacons);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "channel_beacons:",
		      le32_to_cpu(general->channel_beacons),
		      accum_general->channel_beacons,
		      delta_general->channel_beacons,
		      max_general->channel_beacons);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "num_missed_bcon:",
		      le32_to_cpu(general->num_missed_bcon),
		      accum_general->num_missed_bcon,
		      delta_general->num_missed_bcon,
		      max_general->num_missed_bcon);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "adc_rx_saturation_time:",
		      le32_to_cpu(general->adc_rx_saturation_time),
		      accum_general->adc_rx_saturation_time,
		      delta_general->adc_rx_saturation_time,
		      max_general->adc_rx_saturation_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "ina_detect_search_tm:",
		      le32_to_cpu(general->ina_detection_search_time),
		      accum_general->ina_detection_search_time,
		      delta_general->ina_detection_search_time,
		      max_general->ina_detection_search_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "beacon_silence_rssi_a:",
		      le32_to_cpu(general->beacon_silence_rssi_a),
		      accum_general->beacon_silence_rssi_a,
		      delta_general->beacon_silence_rssi_a,
		      max_general->beacon_silence_rssi_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "beacon_silence_rssi_b:",
		      le32_to_cpu(general->beacon_silence_rssi_b),
		      accum_general->beacon_silence_rssi_b,
		      delta_general->beacon_silence_rssi_b,
		      max_general->beacon_silence_rssi_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "beacon_silence_rssi_c:",
		      le32_to_cpu(general->beacon_silence_rssi_c),
		      accum_general->beacon_silence_rssi_c,
		      delta_general->beacon_silence_rssi_c,
		      max_general->beacon_silence_rssi_c);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "interference_data_flag:",
		      le32_to_cpu(general->interference_data_flag),
		      accum_general->interference_data_flag,
		      delta_general->interference_data_flag,
		      max_general->interference_data_flag);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "channel_load:",
		      le32_to_cpu(general->channel_load),
		      accum_general->channel_load, delta_general->channel_load,
		      max_general->channel_load);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "dsp_false_alarms:",
		      le32_to_cpu(general->dsp_false_alarms),
		      accum_general->dsp_false_alarms,
		      delta_general->dsp_false_alarms,
		      max_general->dsp_false_alarms);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_rssi_a:",
		      le32_to_cpu(general->beacon_rssi_a),
		      accum_general->beacon_rssi_a,
		      delta_general->beacon_rssi_a, max_general->beacon_rssi_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_rssi_b:",
		      le32_to_cpu(general->beacon_rssi_b),
		      accum_general->beacon_rssi_b,
		      delta_general->beacon_rssi_b, max_general->beacon_rssi_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_rssi_c:",
		      le32_to_cpu(general->beacon_rssi_c),
		      accum_general->beacon_rssi_c,
		      delta_general->beacon_rssi_c, max_general->beacon_rssi_c);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_energy_a:",
		      le32_to_cpu(general->beacon_energy_a),
		      accum_general->beacon_energy_a,
		      delta_general->beacon_energy_a,
		      max_general->beacon_energy_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_energy_b:",
		      le32_to_cpu(general->beacon_energy_b),
		      accum_general->beacon_energy_b,
		      delta_general->beacon_energy_b,
		      max_general->beacon_energy_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "beacon_energy_c:",
		      le32_to_cpu(general->beacon_energy_c),
		      accum_general->beacon_energy_c,
		      delta_general->beacon_energy_c,
		      max_general->beacon_energy_c);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_header,
		      "Statistics_Rx - OFDM_HT:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "plcp_err:",
		      le32_to_cpu(ht->plcp_err), accum_ht->plcp_err,
		      delta_ht->plcp_err, max_ht->plcp_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "overrun_err:",
		      le32_to_cpu(ht->overrun_err), accum_ht->overrun_err,
		      delta_ht->overrun_err, max_ht->overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "early_overrun_err:",
		      le32_to_cpu(ht->early_overrun_err),
		      accum_ht->early_overrun_err, delta_ht->early_overrun_err,
		      max_ht->early_overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_good:",
		      le32_to_cpu(ht->crc32_good), accum_ht->crc32_good,
		      delta_ht->crc32_good, max_ht->crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "crc32_err:",
		      le32_to_cpu(ht->crc32_err), accum_ht->crc32_err,
		      delta_ht->crc32_err, max_ht->crc32_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "mh_format_err:",
		      le32_to_cpu(ht->mh_format_err), accum_ht->mh_format_err,
		      delta_ht->mh_format_err, max_ht->mh_format_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg_crc32_good:",
		      le32_to_cpu(ht->agg_crc32_good), accum_ht->agg_crc32_good,
		      delta_ht->agg_crc32_good, max_ht->agg_crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg_mpdu_cnt:",
		      le32_to_cpu(ht->agg_mpdu_cnt), accum_ht->agg_mpdu_cnt,
		      delta_ht->agg_mpdu_cnt, max_ht->agg_mpdu_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg_cnt:",
		      le32_to_cpu(ht->agg_cnt), accum_ht->agg_cnt,
		      delta_ht->agg_cnt, max_ht->agg_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "unsupport_mcs:",
		      le32_to_cpu(ht->unsupport_mcs), accum_ht->unsupport_mcs,
		      delta_ht->unsupport_mcs, max_ht->unsupport_mcs);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il4965_ucode_tx_stats_read(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct stats_tx) * 48) + 250;
	ssize_t ret;
	struct stats_tx *tx, *accum_tx, *delta_tx, *max_tx;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/* the statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	tx = &il->_4965.stats.tx;
	accum_tx = &il->_4965.accum_stats.tx;
	delta_tx = &il->_4965.delta_stats.tx;
	max_tx = &il->_4965.max_delta.tx;

	pos += il4965_stats_flag(il, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos, fmt_header, "Statistics_Tx:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "preamble:",
		      le32_to_cpu(tx->preamble_cnt), accum_tx->preamble_cnt,
		      delta_tx->preamble_cnt, max_tx->preamble_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "rx_detected_cnt:",
		      le32_to_cpu(tx->rx_detected_cnt),
		      accum_tx->rx_detected_cnt, delta_tx->rx_detected_cnt,
		      max_tx->rx_detected_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "bt_prio_defer_cnt:",
		      le32_to_cpu(tx->bt_prio_defer_cnt),
		      accum_tx->bt_prio_defer_cnt, delta_tx->bt_prio_defer_cnt,
		      max_tx->bt_prio_defer_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "bt_prio_kill_cnt:",
		      le32_to_cpu(tx->bt_prio_kill_cnt),
		      accum_tx->bt_prio_kill_cnt, delta_tx->bt_prio_kill_cnt,
		      max_tx->bt_prio_kill_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "few_bytes_cnt:",
		      le32_to_cpu(tx->few_bytes_cnt), accum_tx->few_bytes_cnt,
		      delta_tx->few_bytes_cnt, max_tx->few_bytes_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "cts_timeout:",
		      le32_to_cpu(tx->cts_timeout), accum_tx->cts_timeout,
		      delta_tx->cts_timeout, max_tx->cts_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "ack_timeout:",
		      le32_to_cpu(tx->ack_timeout), accum_tx->ack_timeout,
		      delta_tx->ack_timeout, max_tx->ack_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "expected_ack_cnt:",
		      le32_to_cpu(tx->expected_ack_cnt),
		      accum_tx->expected_ack_cnt, delta_tx->expected_ack_cnt,
		      max_tx->expected_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "actual_ack_cnt:",
		      le32_to_cpu(tx->actual_ack_cnt), accum_tx->actual_ack_cnt,
		      delta_tx->actual_ack_cnt, max_tx->actual_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "dump_msdu_cnt:",
		      le32_to_cpu(tx->dump_msdu_cnt), accum_tx->dump_msdu_cnt,
		      delta_tx->dump_msdu_cnt, max_tx->dump_msdu_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "abort_nxt_frame_mismatch:",
		      le32_to_cpu(tx->burst_abort_next_frame_mismatch_cnt),
		      accum_tx->burst_abort_next_frame_mismatch_cnt,
		      delta_tx->burst_abort_next_frame_mismatch_cnt,
		      max_tx->burst_abort_next_frame_mismatch_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "abort_missing_nxt_frame:",
		      le32_to_cpu(tx->burst_abort_missing_next_frame_cnt),
		      accum_tx->burst_abort_missing_next_frame_cnt,
		      delta_tx->burst_abort_missing_next_frame_cnt,
		      max_tx->burst_abort_missing_next_frame_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "cts_timeout_collision:",
		      le32_to_cpu(tx->cts_timeout_collision),
		      accum_tx->cts_timeout_collision,
		      delta_tx->cts_timeout_collision,
		      max_tx->cts_timeout_collision);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "ack_ba_timeout_collision:",
		      le32_to_cpu(tx->ack_or_ba_timeout_collision),
		      accum_tx->ack_or_ba_timeout_collision,
		      delta_tx->ack_or_ba_timeout_collision,
		      max_tx->ack_or_ba_timeout_collision);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg ba_timeout:",
		      le32_to_cpu(tx->agg.ba_timeout), accum_tx->agg.ba_timeout,
		      delta_tx->agg.ba_timeout, max_tx->agg.ba_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "agg ba_resched_frames:",
		      le32_to_cpu(tx->agg.ba_reschedule_frames),
		      accum_tx->agg.ba_reschedule_frames,
		      delta_tx->agg.ba_reschedule_frames,
		      max_tx->agg.ba_reschedule_frames);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "agg scd_query_agg_frame:",
		      le32_to_cpu(tx->agg.scd_query_agg_frame_cnt),
		      accum_tx->agg.scd_query_agg_frame_cnt,
		      delta_tx->agg.scd_query_agg_frame_cnt,
		      max_tx->agg.scd_query_agg_frame_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "agg scd_query_no_agg:",
		      le32_to_cpu(tx->agg.scd_query_no_agg),
		      accum_tx->agg.scd_query_no_agg,
		      delta_tx->agg.scd_query_no_agg,
		      max_tx->agg.scd_query_no_agg);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg scd_query_agg:",
		      le32_to_cpu(tx->agg.scd_query_agg),
		      accum_tx->agg.scd_query_agg, delta_tx->agg.scd_query_agg,
		      max_tx->agg.scd_query_agg);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "agg scd_query_mismatch:",
		      le32_to_cpu(tx->agg.scd_query_mismatch),
		      accum_tx->agg.scd_query_mismatch,
		      delta_tx->agg.scd_query_mismatch,
		      max_tx->agg.scd_query_mismatch);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg frame_not_ready:",
		      le32_to_cpu(tx->agg.frame_not_ready),
		      accum_tx->agg.frame_not_ready,
		      delta_tx->agg.frame_not_ready,
		      max_tx->agg.frame_not_ready);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg underrun:",
		      le32_to_cpu(tx->agg.underrun), accum_tx->agg.underrun,
		      delta_tx->agg.underrun, max_tx->agg.underrun);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg bt_prio_kill:",
		      le32_to_cpu(tx->agg.bt_prio_kill),
		      accum_tx->agg.bt_prio_kill, delta_tx->agg.bt_prio_kill,
		      max_tx->agg.bt_prio_kill);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "agg rx_ba_rsp_cnt:",
		      le32_to_cpu(tx->agg.rx_ba_rsp_cnt),
		      accum_tx->agg.rx_ba_rsp_cnt, delta_tx->agg.rx_ba_rsp_cnt,
		      max_tx->agg.rx_ba_rsp_cnt);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il4965_ucode_general_stats_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct stats_general) * 10 + 300;
	ssize_t ret;
	struct stats_general_common *general, *accum_general;
	struct stats_general_common *delta_general, *max_general;
	struct stats_dbg *dbg, *accum_dbg, *delta_dbg, *max_dbg;
	struct stats_div *div, *accum_div, *delta_div, *max_div;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/* the statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	general = &il->_4965.stats.general.common;
	dbg = &il->_4965.stats.general.common.dbg;
	div = &il->_4965.stats.general.common.div;
	accum_general = &il->_4965.accum_stats.general.common;
	accum_dbg = &il->_4965.accum_stats.general.common.dbg;
	accum_div = &il->_4965.accum_stats.general.common.div;
	delta_general = &il->_4965.delta_stats.general.common;
	max_general = &il->_4965.max_delta.general.common;
	delta_dbg = &il->_4965.delta_stats.general.common.dbg;
	max_dbg = &il->_4965.max_delta.general.common.dbg;
	delta_div = &il->_4965.delta_stats.general.common.div;
	max_div = &il->_4965.max_delta.general.common.div;

	pos += il4965_stats_flag(il, buf, bufsz);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_header,
		      "Statistics_General:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_value, "temperature:",
		      le32_to_cpu(general->temperature));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_value, "ttl_timestamp:",
		      le32_to_cpu(general->ttl_timestamp));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "burst_check:",
		      le32_to_cpu(dbg->burst_check), accum_dbg->burst_check,
		      delta_dbg->burst_check, max_dbg->burst_check);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "burst_count:",
		      le32_to_cpu(dbg->burst_count), accum_dbg->burst_count,
		      delta_dbg->burst_count, max_dbg->burst_count);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table,
		      "wait_for_silence_timeout_count:",
		      le32_to_cpu(dbg->wait_for_silence_timeout_cnt),
		      accum_dbg->wait_for_silence_timeout_cnt,
		      delta_dbg->wait_for_silence_timeout_cnt,
		      max_dbg->wait_for_silence_timeout_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "sleep_time:",
		      le32_to_cpu(general->sleep_time),
		      accum_general->sleep_time, delta_general->sleep_time,
		      max_general->sleep_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "slots_out:",
		      le32_to_cpu(general->slots_out), accum_general->slots_out,
		      delta_general->slots_out, max_general->slots_out);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "slots_idle:",
		      le32_to_cpu(general->slots_idle),
		      accum_general->slots_idle, delta_general->slots_idle,
		      max_general->slots_idle);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "tx_on_a:",
		      le32_to_cpu(div->tx_on_a), accum_div->tx_on_a,
		      delta_div->tx_on_a, max_div->tx_on_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "tx_on_b:",
		      le32_to_cpu(div->tx_on_b), accum_div->tx_on_b,
		      delta_div->tx_on_b, max_div->tx_on_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "exec_time:",
		      le32_to_cpu(div->exec_time), accum_div->exec_time,
		      delta_div->exec_time, max_div->exec_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "probe_time:",
		      le32_to_cpu(div->probe_time), accum_div->probe_time,
		      delta_div->probe_time, max_div->probe_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "rx_enable_counter:",
		      le32_to_cpu(general->rx_enable_counter),
		      accum_general->rx_enable_counter,
		      delta_general->rx_enable_counter,
		      max_general->rx_enable_counter);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, fmt_table, "num_of_sos_states:",
		      le32_to_cpu(general->num_of_sos_states),
		      accum_general->num_of_sos_states,
		      delta_general->num_of_sos_states,
		      max_general->num_of_sos_states);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

const struct il_debugfs_ops il4965_debugfs_ops = {
	.rx_stats_read = il4965_ucode_rx_stats_read,
	.tx_stats_read = il4965_ucode_tx_stats_read,
	.general_stats_read = il4965_ucode_general_stats_read,
};
