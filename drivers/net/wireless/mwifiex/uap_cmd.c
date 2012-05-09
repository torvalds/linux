/*
 * Marvell Wireless LAN device driver: AP specific command handling
 *
 * Copyright (C) 2012, Marvell International Ltd.
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

/* This function initializes some of mwifiex_uap_bss_param variables.
 * This helps FW in ignoring invalid values. These values may or may not
 * be get updated to valid ones at later stage.
 */
void mwifiex_set_sys_config_invalid_data(struct mwifiex_uap_bss_param *config)
{
	config->rts_threshold = 0x7FFF;
	config->frag_threshold = 0x7FFF;
	config->retry_limit = 0x7F;
}

/* Parse AP config structure and prepare TLV based command structure
 * to be sent to FW for uAP configuration
 */
static int mwifiex_cmd_uap_sys_config(struct host_cmd_ds_command *cmd,
				      u16 cmd_action, void *cmd_buf)
{
	u8 *tlv;
	struct host_cmd_ds_sys_config *sys_config = &cmd->params.uap_sys_config;
	struct host_cmd_tlv_channel_band *chan_band;
	struct host_cmd_tlv_frag_threshold *frag_threshold;
	struct host_cmd_tlv_rts_threshold *rts_threshold;
	struct host_cmd_tlv_retry_limit *retry_limit;
	struct mwifiex_uap_bss_param *bss_cfg = cmd_buf;
	u16 cmd_size;

	cmd->command = cpu_to_le16(HostCmd_CMD_UAP_SYS_CONFIG);
	cmd_size = (u16)(sizeof(struct host_cmd_ds_sys_config) + S_DS_GEN);

	sys_config->action = cpu_to_le16(cmd_action);

	tlv = sys_config->tlv;

	if (bss_cfg->channel && bss_cfg->channel <= MAX_CHANNEL_BAND_BG) {
		chan_band = (struct host_cmd_tlv_channel_band *)tlv;
		chan_band->tlv.type = cpu_to_le16(TLV_TYPE_CHANNELBANDLIST);
		chan_band->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_channel_band) -
				    sizeof(struct host_cmd_tlv));
		chan_band->band_config = bss_cfg->band_cfg;
		chan_band->channel = bss_cfg->channel;
		cmd_size += sizeof(struct host_cmd_tlv_channel_band);
		tlv += sizeof(struct host_cmd_tlv_channel_band);
	}
	if (bss_cfg->rts_threshold <= MWIFIEX_RTS_MAX_VALUE) {
		rts_threshold = (struct host_cmd_tlv_rts_threshold *)tlv;
		rts_threshold->tlv.type =
					cpu_to_le16(TLV_TYPE_UAP_RTS_THRESHOLD);
		rts_threshold->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_rts_threshold) -
				    sizeof(struct host_cmd_tlv));
		rts_threshold->rts_thr = cpu_to_le16(bss_cfg->rts_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if ((bss_cfg->frag_threshold >= MWIFIEX_FRAG_MIN_VALUE) &&
	    (bss_cfg->frag_threshold <= MWIFIEX_FRAG_MAX_VALUE)) {
		frag_threshold = (struct host_cmd_tlv_frag_threshold *)tlv;
		frag_threshold->tlv.type =
				cpu_to_le16(TLV_TYPE_UAP_FRAG_THRESHOLD);
		frag_threshold->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_frag_threshold) -
				    sizeof(struct host_cmd_tlv));
		frag_threshold->frag_thr = cpu_to_le16(bss_cfg->frag_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if (bss_cfg->retry_limit <= MWIFIEX_RETRY_LIMIT) {
		retry_limit = (struct host_cmd_tlv_retry_limit *)tlv;
		retry_limit->tlv.type = cpu_to_le16(TLV_TYPE_UAP_RETRY_LIMIT);
		retry_limit->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_retry_limit) -
				    sizeof(struct host_cmd_tlv));
		retry_limit->limit = (u8)bss_cfg->retry_limit;
		cmd_size += sizeof(struct host_cmd_tlv_retry_limit);
		tlv += sizeof(struct host_cmd_tlv_retry_limit);
	}

	cmd->size = cpu_to_le16(cmd_size);
	return 0;
}

/* This function prepares the AP specific commands before sending them
 * to the firmware.
 * This is a generic function which calls specific command preparation
 * routines based upon the command number.
 */
int mwifiex_uap_prepare_cmd(struct mwifiex_private *priv, u16 cmd_no,
			    u16 cmd_action, u32 cmd_oid,
			    void *data_buf, void *cmd_buf)
{
	struct host_cmd_ds_command *cmd = cmd_buf;

	switch (cmd_no) {
	case HostCmd_CMD_UAP_SYS_CONFIG:
		if (mwifiex_cmd_uap_sys_config(cmd, cmd_action, data_buf))
			return -1;
		break;
	case HostCmd_CMD_UAP_BSS_START:
	case HostCmd_CMD_UAP_BSS_STOP:
		cmd->command = cpu_to_le16(cmd_no);
		cmd->size = cpu_to_le16(S_DS_GEN);
		break;
	default:
		dev_err(priv->adapter->dev,
			"PREP_CMD: unknown cmd %#x\n", cmd_no);
		return -1;
	}

	return 0;
}

/* This function sets the RF channel for AP.
 *
 * This function populates channel information in AP config structure
 * and sends command to configure channel information in AP.
 */
int mwifiex_uap_set_channel(struct mwifiex_private *priv, int channel)
{
	struct mwifiex_uap_bss_param *bss_cfg;
	struct wiphy *wiphy = priv->wdev->wiphy;

	bss_cfg = kzalloc(sizeof(struct mwifiex_uap_bss_param), GFP_KERNEL);
	if (!bss_cfg)
		return -ENOMEM;

	bss_cfg->band_cfg = BAND_CONFIG_MANUAL;
	bss_cfg->channel = channel;

	if (mwifiex_send_cmd_async(priv, HostCmd_CMD_UAP_SYS_CONFIG,
				   HostCmd_ACT_GEN_SET, 0, bss_cfg)) {
		wiphy_err(wiphy, "Failed to set the uAP channel\n");
		kfree(bss_cfg);
		return -1;
	}

	kfree(bss_cfg);
	return 0;
}
