/*
 * Marvell Wireless LAN device driver: 802.11h
 *
 * Copyright (C) 2013-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"
#include "fw.h"


void mwifiex_init_11h_params(struct mwifiex_private *priv)
{
	priv->state_11h.is_11h_enabled = true;
	priv->state_11h.is_11h_active = false;
}

inline int mwifiex_is_11h_active(struct mwifiex_private *priv)
{
	return priv->state_11h.is_11h_active;
}
/* This function appends 11h info to a buffer while joining an
 * infrastructure BSS
 */
static void
mwifiex_11h_process_infra_join(struct mwifiex_private *priv, u8 **buffer,
			       struct mwifiex_bssdescriptor *bss_desc)
{
	struct mwifiex_ie_types_header *ie_header;
	struct mwifiex_ie_types_pwr_capability *cap;
	struct mwifiex_ie_types_local_pwr_constraint *constraint;
	struct ieee80211_supported_band *sband;
	u8 radio_type;
	int i;

	if (!buffer || !(*buffer))
		return;

	radio_type = mwifiex_band_to_radio_type((u8) bss_desc->bss_band);
	sband = priv->wdev.wiphy->bands[radio_type];

	cap = (struct mwifiex_ie_types_pwr_capability *)*buffer;
	cap->header.type = cpu_to_le16(WLAN_EID_PWR_CAPABILITY);
	cap->header.len = cpu_to_le16(2);
	cap->min_pwr = 0;
	cap->max_pwr = 0;
	*buffer += sizeof(*cap);

	constraint = (struct mwifiex_ie_types_local_pwr_constraint *)*buffer;
	constraint->header.type = cpu_to_le16(WLAN_EID_PWR_CONSTRAINT);
	constraint->header.len = cpu_to_le16(2);
	constraint->chan = bss_desc->channel;
	constraint->constraint = bss_desc->local_constraint;
	*buffer += sizeof(*constraint);

	ie_header = (struct mwifiex_ie_types_header *)*buffer;
	ie_header->type = cpu_to_le16(TLV_TYPE_PASSTHROUGH);
	ie_header->len  = cpu_to_le16(2 * sband->n_channels + 2);
	*buffer += sizeof(*ie_header);
	*(*buffer)++ = WLAN_EID_SUPPORTED_CHANNELS;
	*(*buffer)++ = 2 * sband->n_channels;
	for (i = 0; i < sband->n_channels; i++) {
		*(*buffer)++ = ieee80211_frequency_to_channel(
					sband->channels[i].center_freq);
		*(*buffer)++ = 1; /* one channel in the subband */
	}
}

/* Enable or disable the 11h extensions in the firmware */
int mwifiex_11h_activate(struct mwifiex_private *priv, bool flag)
{
	u32 enable = flag;

	/* enable master mode radar detection on AP interface */
	if ((GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) && enable)
		enable |= MWIFIEX_MASTER_RADAR_DET_MASK;

	return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_SNMP_MIB,
				HostCmd_ACT_GEN_SET, DOT11H_I, &enable, true);
}

/* This functions processes TLV buffer for a pending BSS Join command.
 *
 * Activate 11h functionality in the firmware if the spectrum management
 * capability bit is found in the network we are joining. Also, necessary
 * TLVs are set based on requested network's 11h capability.
 */
void mwifiex_11h_process_join(struct mwifiex_private *priv, u8 **buffer,
			      struct mwifiex_bssdescriptor *bss_desc)
{
	if (bss_desc->sensed_11h) {
		/* Activate 11h functions in firmware, turns on capability
		 * bit
		 */
		mwifiex_11h_activate(priv, true);
		priv->state_11h.is_11h_active = true;
		bss_desc->cap_info_bitmap |= WLAN_CAPABILITY_SPECTRUM_MGMT;
		mwifiex_11h_process_infra_join(priv, buffer, bss_desc);
	} else {
		/* Deactivate 11h functions in the firmware */
		mwifiex_11h_activate(priv, false);
		priv->state_11h.is_11h_active = false;
		bss_desc->cap_info_bitmap &= ~WLAN_CAPABILITY_SPECTRUM_MGMT;
	}
}

/* This is DFS CAC work queue function.
 * This delayed work emits CAC finished event for cfg80211 if
 * CAC was started earlier.
 */
void mwifiex_dfs_cac_work_queue(struct work_struct *work)
{
	struct cfg80211_chan_def chandef;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mwifiex_private *priv =
			container_of(delayed_work, struct mwifiex_private,
				     dfs_cac_work);

	if (WARN_ON(!priv))
		return;

	chandef = priv->dfs_chandef;
	if (priv->wdev.cac_started) {
		mwifiex_dbg(priv->adapter, MSG,
			    "CAC timer finished; No radar detected\n");
		cfg80211_cac_event(priv->netdev, &chandef,
				   NL80211_RADAR_CAC_FINISHED,
				   GFP_KERNEL);
	}
}

/* This function prepares channel report request command to FW for
 * starting radar detection.
 */
int mwifiex_cmd_issue_chan_report_request(struct mwifiex_private *priv,
					  struct host_cmd_ds_command *cmd,
					  void *data_buf)
{
	struct host_cmd_ds_chan_rpt_req *cr_req = &cmd->params.chan_rpt_req;
	struct mwifiex_radar_params *radar_params = (void *)data_buf;

	cmd->command = cpu_to_le16(HostCmd_CMD_CHAN_REPORT_REQUEST);
	cmd->size = cpu_to_le16(S_DS_GEN);
	le16_unaligned_add_cpu(&cmd->size,
			       sizeof(struct host_cmd_ds_chan_rpt_req));

	cr_req->chan_desc.start_freq = cpu_to_le16(MWIFIEX_A_BAND_START_FREQ);
	cr_req->chan_desc.chan_num = radar_params->chandef->chan->hw_value;
	cr_req->chan_desc.chan_width = radar_params->chandef->width;
	cr_req->msec_dwell_time = cpu_to_le32(radar_params->cac_time_ms);

	if (radar_params->cac_time_ms)
		mwifiex_dbg(priv->adapter, MSG,
			    "11h: issuing DFS Radar check for channel=%d\n",
			    radar_params->chandef->chan->hw_value);
	else
		mwifiex_dbg(priv->adapter, MSG, "cancelling CAC\n");

	return 0;
}

int mwifiex_stop_radar_detection(struct mwifiex_private *priv,
				 struct cfg80211_chan_def *chandef)
{
	struct mwifiex_radar_params radar_params;

	memset(&radar_params, 0, sizeof(struct mwifiex_radar_params));
	radar_params.chandef = chandef;
	radar_params.cac_time_ms = 0;

	return mwifiex_send_cmd(priv, HostCmd_CMD_CHAN_REPORT_REQUEST,
				HostCmd_ACT_GEN_SET, 0, &radar_params, true);
}

/* This function is to abort ongoing CAC upon stopping AP operations
 * or during unload.
 */
void mwifiex_abort_cac(struct mwifiex_private *priv)
{
	if (priv->wdev.cac_started) {
		if (mwifiex_stop_radar_detection(priv, &priv->dfs_chandef))
			mwifiex_dbg(priv->adapter, ERROR,
				    "failed to stop CAC in FW\n");
		mwifiex_dbg(priv->adapter, MSG,
			    "Aborting delayed work for CAC.\n");
		cancel_delayed_work_sync(&priv->dfs_cac_work);
		cfg80211_cac_event(priv->netdev, &priv->dfs_chandef,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
	}
}

/* This function handles channel report event from FW during CAC period.
 * If radar is detected during CAC, driver indicates the same to cfg80211
 * and also cancels ongoing delayed work.
 */
int mwifiex_11h_handle_chanrpt_ready(struct mwifiex_private *priv,
				     struct sk_buff *skb)
{
	struct host_cmd_ds_chan_rpt_event *rpt_event;
	struct mwifiex_ie_types_chan_rpt_data *rpt;
	u8 *evt_buf;
	u16 event_len, tlv_len;

	rpt_event = (void *)(skb->data + sizeof(u32));
	event_len = skb->len - (sizeof(struct host_cmd_ds_chan_rpt_event)+
				sizeof(u32));

	if (le32_to_cpu(rpt_event->result) != HostCmd_RESULT_OK) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "Error in channel report event\n");
		return -1;
	}

	evt_buf = (void *)&rpt_event->tlvbuf;

	while (event_len >= sizeof(struct mwifiex_ie_types_header)) {
		rpt = (void *)&rpt_event->tlvbuf;
		tlv_len = le16_to_cpu(rpt->header.len);

		switch (le16_to_cpu(rpt->header.type)) {
		case TLV_TYPE_CHANRPT_11H_BASIC:
			if (rpt->map.radar) {
				mwifiex_dbg(priv->adapter, MSG,
					    "RADAR Detected on channel %d!\n",
					    priv->dfs_chandef.chan->hw_value);
				cancel_delayed_work_sync(&priv->dfs_cac_work);
				cfg80211_cac_event(priv->netdev,
						   &priv->dfs_chandef,
						   NL80211_RADAR_DETECTED,
						   GFP_KERNEL);
			}
			break;
		default:
			break;
		}

		evt_buf += (tlv_len + sizeof(rpt->header));
		event_len -= (tlv_len + sizeof(rpt->header));
	}

	return 0;
}

/* Handler for radar detected event from FW.*/
int mwifiex_11h_handle_radar_detected(struct mwifiex_private *priv,
				      struct sk_buff *skb)
{
	struct mwifiex_radar_det_event *rdr_event;

	rdr_event = (void *)(skb->data + sizeof(u32));

	mwifiex_dbg(priv->adapter, MSG,
		    "radar detected; indicating kernel\n");
	if (mwifiex_stop_radar_detection(priv, &priv->dfs_chandef))
		mwifiex_dbg(priv->adapter, ERROR,
			    "Failed to stop CAC in FW\n");
	cfg80211_radar_event(priv->adapter->wiphy, &priv->dfs_chandef,
			     GFP_KERNEL);
	mwifiex_dbg(priv->adapter, MSG, "regdomain: %d\n",
		    rdr_event->reg_domain);
	mwifiex_dbg(priv->adapter, MSG, "radar detection type: %d\n",
		    rdr_event->det_type);

	return 0;
}

/* This is work queue function for channel switch handling.
 * This function takes care of updating new channel definitin to
 * bss config structure, restart AP and indicate channel switch success
 * to cfg80211.
 */
void mwifiex_dfs_chan_sw_work_queue(struct work_struct *work)
{
	struct mwifiex_uap_bss_param *bss_cfg;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mwifiex_private *priv =
			container_of(delayed_work, struct mwifiex_private,
				     dfs_chan_sw_work);

	if (WARN_ON(!priv))
		return;

	bss_cfg = &priv->bss_cfg;
	if (!bss_cfg->beacon_period) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "channel switch: AP already stopped\n");
		return;
	}

	mwifiex_uap_set_channel(priv, bss_cfg, priv->dfs_chandef);

	if (mwifiex_config_start_uap(priv, bss_cfg)) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "Failed to start AP after channel switch\n");
		return;
	}

	mwifiex_dbg(priv->adapter, MSG,
		    "indicating channel switch completion to kernel\n");
	cfg80211_ch_switch_notify(priv->netdev, &priv->dfs_chandef);
}
