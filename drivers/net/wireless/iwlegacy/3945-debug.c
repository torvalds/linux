/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include "common.h"
#include "3945.h"

static int
il3945_stats_flag(struct il_priv *il, char *buf, int bufsz)
{
	int p = 0;

	p += scnprintf(buf + p, bufsz - p, "Statistics Flag(0x%X):\n",
		       le32_to_cpu(il->_3945.stats.flag));
	if (le32_to_cpu(il->_3945.stats.flag) & UCODE_STATS_CLEAR_MSK)
		p += scnprintf(buf + p, bufsz - p,
			       "\tStatistics have been cleared\n");
	p += scnprintf(buf + p, bufsz - p, "\tOperational Frequency: %s\n",
		       (le32_to_cpu(il->_3945.stats.flag) &
			UCODE_STATS_FREQUENCY_MSK) ? "2.4 GHz" : "5.2 GHz");
	p += scnprintf(buf + p, bufsz - p, "\tTGj Narrow Band: %s\n",
		       (le32_to_cpu(il->_3945.stats.flag) &
			UCODE_STATS_NARROW_BAND_MSK) ? "enabled" : "disabled");
	return p;
}

static ssize_t
il3945_ucode_rx_stats_read(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz =
	    sizeof(struct iwl39_stats_rx_phy) * 40 +
	    sizeof(struct iwl39_stats_rx_non_phy) * 40 + 400;
	ssize_t ret;
	struct iwl39_stats_rx_phy *ofdm, *accum_ofdm, *delta_ofdm, *max_ofdm;
	struct iwl39_stats_rx_phy *cck, *accum_cck, *delta_cck, *max_cck;
	struct iwl39_stats_rx_non_phy *general, *accum_general;
	struct iwl39_stats_rx_non_phy *delta_general, *max_general;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/*
	 * The statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	ofdm = &il->_3945.stats.rx.ofdm;
	cck = &il->_3945.stats.rx.cck;
	general = &il->_3945.stats.rx.general;
	accum_ofdm = &il->_3945.accum_stats.rx.ofdm;
	accum_cck = &il->_3945.accum_stats.rx.cck;
	accum_general = &il->_3945.accum_stats.rx.general;
	delta_ofdm = &il->_3945.delta_stats.rx.ofdm;
	delta_cck = &il->_3945.delta_stats.rx.cck;
	delta_general = &il->_3945.delta_stats.rx.general;
	max_ofdm = &il->_3945.max_delta.rx.ofdm;
	max_cck = &il->_3945.max_delta.rx.cck;
	max_general = &il->_3945.max_delta.rx.general;

	pos += il3945_stats_flag(il, buf, bufsz);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "%-32s     current"
		      "acumulative       delta         max\n",
		      "Statistics_Rx - OFDM:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "ina_cnt:",
		      le32_to_cpu(ofdm->ina_cnt), accum_ofdm->ina_cnt,
		      delta_ofdm->ina_cnt, max_ofdm->ina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_cnt:",
		      le32_to_cpu(ofdm->fina_cnt), accum_ofdm->fina_cnt,
		      delta_ofdm->fina_cnt, max_ofdm->fina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "plcp_err:",
		      le32_to_cpu(ofdm->plcp_err), accum_ofdm->plcp_err,
		      delta_ofdm->plcp_err, max_ofdm->plcp_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "crc32_err:",
		      le32_to_cpu(ofdm->crc32_err), accum_ofdm->crc32_err,
		      delta_ofdm->crc32_err, max_ofdm->crc32_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "overrun_err:",
		      le32_to_cpu(ofdm->overrun_err), accum_ofdm->overrun_err,
		      delta_ofdm->overrun_err, max_ofdm->overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "early_overrun_err:",
		      le32_to_cpu(ofdm->early_overrun_err),
		      accum_ofdm->early_overrun_err,
		      delta_ofdm->early_overrun_err,
		      max_ofdm->early_overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "crc32_good:",
		      le32_to_cpu(ofdm->crc32_good), accum_ofdm->crc32_good,
		      delta_ofdm->crc32_good, max_ofdm->crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "false_alarm_cnt:",
		      le32_to_cpu(ofdm->false_alarm_cnt),
		      accum_ofdm->false_alarm_cnt, delta_ofdm->false_alarm_cnt,
		      max_ofdm->false_alarm_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_sync_err_cnt:",
		      le32_to_cpu(ofdm->fina_sync_err_cnt),
		      accum_ofdm->fina_sync_err_cnt,
		      delta_ofdm->fina_sync_err_cnt,
		      max_ofdm->fina_sync_err_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sfd_timeout:",
		      le32_to_cpu(ofdm->sfd_timeout), accum_ofdm->sfd_timeout,
		      delta_ofdm->sfd_timeout, max_ofdm->sfd_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_timeout:",
		      le32_to_cpu(ofdm->fina_timeout), accum_ofdm->fina_timeout,
		      delta_ofdm->fina_timeout, max_ofdm->fina_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "unresponded_rts:",
		      le32_to_cpu(ofdm->unresponded_rts),
		      accum_ofdm->unresponded_rts, delta_ofdm->unresponded_rts,
		      max_ofdm->unresponded_rts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n",
		      "rxe_frame_lmt_ovrun:",
		      le32_to_cpu(ofdm->rxe_frame_limit_overrun),
		      accum_ofdm->rxe_frame_limit_overrun,
		      delta_ofdm->rxe_frame_limit_overrun,
		      max_ofdm->rxe_frame_limit_overrun);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sent_ack_cnt:",
		      le32_to_cpu(ofdm->sent_ack_cnt), accum_ofdm->sent_ack_cnt,
		      delta_ofdm->sent_ack_cnt, max_ofdm->sent_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sent_cts_cnt:",
		      le32_to_cpu(ofdm->sent_cts_cnt), accum_ofdm->sent_cts_cnt,
		      delta_ofdm->sent_cts_cnt, max_ofdm->sent_cts_cnt);

	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "%-32s     current"
		      "acumulative       delta         max\n",
		      "Statistics_Rx - CCK:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "ina_cnt:",
		      le32_to_cpu(cck->ina_cnt), accum_cck->ina_cnt,
		      delta_cck->ina_cnt, max_cck->ina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_cnt:",
		      le32_to_cpu(cck->fina_cnt), accum_cck->fina_cnt,
		      delta_cck->fina_cnt, max_cck->fina_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "plcp_err:",
		      le32_to_cpu(cck->plcp_err), accum_cck->plcp_err,
		      delta_cck->plcp_err, max_cck->plcp_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "crc32_err:",
		      le32_to_cpu(cck->crc32_err), accum_cck->crc32_err,
		      delta_cck->crc32_err, max_cck->crc32_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "overrun_err:",
		      le32_to_cpu(cck->overrun_err), accum_cck->overrun_err,
		      delta_cck->overrun_err, max_cck->overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "early_overrun_err:",
		      le32_to_cpu(cck->early_overrun_err),
		      accum_cck->early_overrun_err,
		      delta_cck->early_overrun_err, max_cck->early_overrun_err);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "crc32_good:",
		      le32_to_cpu(cck->crc32_good), accum_cck->crc32_good,
		      delta_cck->crc32_good, max_cck->crc32_good);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "false_alarm_cnt:",
		      le32_to_cpu(cck->false_alarm_cnt),
		      accum_cck->false_alarm_cnt, delta_cck->false_alarm_cnt,
		      max_cck->false_alarm_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_sync_err_cnt:",
		      le32_to_cpu(cck->fina_sync_err_cnt),
		      accum_cck->fina_sync_err_cnt,
		      delta_cck->fina_sync_err_cnt, max_cck->fina_sync_err_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sfd_timeout:",
		      le32_to_cpu(cck->sfd_timeout), accum_cck->sfd_timeout,
		      delta_cck->sfd_timeout, max_cck->sfd_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "fina_timeout:",
		      le32_to_cpu(cck->fina_timeout), accum_cck->fina_timeout,
		      delta_cck->fina_timeout, max_cck->fina_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "unresponded_rts:",
		      le32_to_cpu(cck->unresponded_rts),
		      accum_cck->unresponded_rts, delta_cck->unresponded_rts,
		      max_cck->unresponded_rts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n",
		      "rxe_frame_lmt_ovrun:",
		      le32_to_cpu(cck->rxe_frame_limit_overrun),
		      accum_cck->rxe_frame_limit_overrun,
		      delta_cck->rxe_frame_limit_overrun,
		      max_cck->rxe_frame_limit_overrun);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sent_ack_cnt:",
		      le32_to_cpu(cck->sent_ack_cnt), accum_cck->sent_ack_cnt,
		      delta_cck->sent_ack_cnt, max_cck->sent_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sent_cts_cnt:",
		      le32_to_cpu(cck->sent_cts_cnt), accum_cck->sent_cts_cnt,
		      delta_cck->sent_cts_cnt, max_cck->sent_cts_cnt);

	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "%-32s     current"
		      "acumulative       delta         max\n",
		      "Statistics_Rx - GENERAL:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "bogus_cts:",
		      le32_to_cpu(general->bogus_cts), accum_general->bogus_cts,
		      delta_general->bogus_cts, max_general->bogus_cts);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "bogus_ack:",
		      le32_to_cpu(general->bogus_ack), accum_general->bogus_ack,
		      delta_general->bogus_ack, max_general->bogus_ack);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "non_bssid_frames:",
		      le32_to_cpu(general->non_bssid_frames),
		      accum_general->non_bssid_frames,
		      delta_general->non_bssid_frames,
		      max_general->non_bssid_frames);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "filtered_frames:",
		      le32_to_cpu(general->filtered_frames),
		      accum_general->filtered_frames,
		      delta_general->filtered_frames,
		      max_general->filtered_frames);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n",
		      "non_channel_beacons:",
		      le32_to_cpu(general->non_channel_beacons),
		      accum_general->non_channel_beacons,
		      delta_general->non_channel_beacons,
		      max_general->non_channel_beacons);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il3945_ucode_tx_stats_read(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct iwl39_stats_tx) * 48) + 250;
	ssize_t ret;
	struct iwl39_stats_tx *tx, *accum_tx, *delta_tx, *max_tx;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/*
	 * The statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	tx = &il->_3945.stats.tx;
	accum_tx = &il->_3945.accum_stats.tx;
	delta_tx = &il->_3945.delta_stats.tx;
	max_tx = &il->_3945.max_delta.tx;
	pos += il3945_stats_flag(il, buf, bufsz);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "%-32s     current"
		      "acumulative       delta         max\n",
		      "Statistics_Tx:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "preamble:",
		      le32_to_cpu(tx->preamble_cnt), accum_tx->preamble_cnt,
		      delta_tx->preamble_cnt, max_tx->preamble_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "rx_detected_cnt:",
		      le32_to_cpu(tx->rx_detected_cnt),
		      accum_tx->rx_detected_cnt, delta_tx->rx_detected_cnt,
		      max_tx->rx_detected_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "bt_prio_defer_cnt:",
		      le32_to_cpu(tx->bt_prio_defer_cnt),
		      accum_tx->bt_prio_defer_cnt, delta_tx->bt_prio_defer_cnt,
		      max_tx->bt_prio_defer_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "bt_prio_kill_cnt:",
		      le32_to_cpu(tx->bt_prio_kill_cnt),
		      accum_tx->bt_prio_kill_cnt, delta_tx->bt_prio_kill_cnt,
		      max_tx->bt_prio_kill_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "few_bytes_cnt:",
		      le32_to_cpu(tx->few_bytes_cnt), accum_tx->few_bytes_cnt,
		      delta_tx->few_bytes_cnt, max_tx->few_bytes_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "cts_timeout:",
		      le32_to_cpu(tx->cts_timeout), accum_tx->cts_timeout,
		      delta_tx->cts_timeout, max_tx->cts_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "ack_timeout:",
		      le32_to_cpu(tx->ack_timeout), accum_tx->ack_timeout,
		      delta_tx->ack_timeout, max_tx->ack_timeout);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "expected_ack_cnt:",
		      le32_to_cpu(tx->expected_ack_cnt),
		      accum_tx->expected_ack_cnt, delta_tx->expected_ack_cnt,
		      max_tx->expected_ack_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "actual_ack_cnt:",
		      le32_to_cpu(tx->actual_ack_cnt), accum_tx->actual_ack_cnt,
		      delta_tx->actual_ack_cnt, max_tx->actual_ack_cnt);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il3945_ucode_general_stats_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct iwl39_stats_general) * 10 + 300;
	ssize_t ret;
	struct iwl39_stats_general *general, *accum_general;
	struct iwl39_stats_general *delta_general, *max_general;
	struct stats_dbg *dbg, *accum_dbg, *delta_dbg, *max_dbg;
	struct iwl39_stats_div *div, *accum_div, *delta_div, *max_div;

	if (!il_is_alive(il))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/*
	 * The statistic information display here is based on
	 * the last stats notification from uCode
	 * might not reflect the current uCode activity
	 */
	general = &il->_3945.stats.general;
	dbg = &il->_3945.stats.general.dbg;
	div = &il->_3945.stats.general.div;
	accum_general = &il->_3945.accum_stats.general;
	delta_general = &il->_3945.delta_stats.general;
	max_general = &il->_3945.max_delta.general;
	accum_dbg = &il->_3945.accum_stats.general.dbg;
	delta_dbg = &il->_3945.delta_stats.general.dbg;
	max_dbg = &il->_3945.max_delta.general.dbg;
	accum_div = &il->_3945.accum_stats.general.div;
	delta_div = &il->_3945.delta_stats.general.div;
	max_div = &il->_3945.max_delta.general.div;
	pos += il3945_stats_flag(il, buf, bufsz);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "%-32s     current"
		      "acumulative       delta         max\n",
		      "Statistics_General:");
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "burst_check:",
		      le32_to_cpu(dbg->burst_check), accum_dbg->burst_check,
		      delta_dbg->burst_check, max_dbg->burst_check);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "burst_count:",
		      le32_to_cpu(dbg->burst_count), accum_dbg->burst_count,
		      delta_dbg->burst_count, max_dbg->burst_count);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "sleep_time:",
		      le32_to_cpu(general->sleep_time),
		      accum_general->sleep_time, delta_general->sleep_time,
		      max_general->sleep_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "slots_out:",
		      le32_to_cpu(general->slots_out), accum_general->slots_out,
		      delta_general->slots_out, max_general->slots_out);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "slots_idle:",
		      le32_to_cpu(general->slots_idle),
		      accum_general->slots_idle, delta_general->slots_idle,
		      max_general->slots_idle);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "ttl_timestamp:\t\t\t%u\n",
		      le32_to_cpu(general->ttl_timestamp));
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "tx_on_a:",
		      le32_to_cpu(div->tx_on_a), accum_div->tx_on_a,
		      delta_div->tx_on_a, max_div->tx_on_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "tx_on_b:",
		      le32_to_cpu(div->tx_on_b), accum_div->tx_on_b,
		      delta_div->tx_on_b, max_div->tx_on_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "exec_time:",
		      le32_to_cpu(div->exec_time), accum_div->exec_time,
		      delta_div->exec_time, max_div->exec_time);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "  %-30s %10u  %10u  %10u  %10u\n", "probe_time:",
		      le32_to_cpu(div->probe_time), accum_div->probe_time,
		      delta_div->probe_time, max_div->probe_time);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

const struct il_debugfs_ops il3945_debugfs_ops = {
	.rx_stats_read = il3945_ucode_rx_stats_read,
	.tx_stats_read = il3945_ucode_tx_stats_read,
	.general_stats_read = il3945_ucode_general_stats_read,
};
