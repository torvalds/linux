/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <net/mac80211.h>
#include "fw-api.h"
#include "mvm.h"

/* Maps the driver specific channel width definition to the the fw values */
static inline u8 iwl_mvm_get_channel_width(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return PHY_VHT_CHANNEL_MODE20;
	case NL80211_CHAN_WIDTH_40:
		return PHY_VHT_CHANNEL_MODE40;
	case NL80211_CHAN_WIDTH_80:
		return PHY_VHT_CHANNEL_MODE80;
	case NL80211_CHAN_WIDTH_160:
		return PHY_VHT_CHANNEL_MODE160;
	default:
		WARN(1, "Invalid channel width=%u", chandef->width);
		return PHY_VHT_CHANNEL_MODE20;
	}
}

/*
 * Maps the driver specific control channel position (relative to the center
 * freq) definitions to the the fw values
 */
static inline u8 iwl_mvm_get_ctrl_pos(struct cfg80211_chan_def *chandef)
{
	switch (chandef->chan->center_freq - chandef->center_freq1) {
	case -70:
		return PHY_VHT_CTRL_POS_4_BELOW;
	case -50:
		return PHY_VHT_CTRL_POS_3_BELOW;
	case -30:
		return PHY_VHT_CTRL_POS_2_BELOW;
	case -10:
		return PHY_VHT_CTRL_POS_1_BELOW;
	case  10:
		return PHY_VHT_CTRL_POS_1_ABOVE;
	case  30:
		return PHY_VHT_CTRL_POS_2_ABOVE;
	case  50:
		return PHY_VHT_CTRL_POS_3_ABOVE;
	case  70:
		return PHY_VHT_CTRL_POS_4_ABOVE;
	default:
		WARN(1, "Invalid channel definition");
	case 0:
		/*
		 * The FW is expected to check the control channel position only
		 * when in HT/VHT and the channel width is not 20MHz. Return
		 * this value as the default one.
		 */
		return PHY_VHT_CTRL_POS_1_BELOW;
	}
}

/*
 * Construct the generic fields of the PHY context command
 */
static void iwl_mvm_phy_ctxt_cmd_hdr(struct iwl_mvm_phy_ctxt *ctxt,
				     struct iwl_phy_context_cmd *cmd,
				     u32 action, u32 apply_time)
{
	memset(cmd, 0, sizeof(struct iwl_phy_context_cmd));

	cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(ctxt->id,
							    ctxt->color));
	cmd->action = cpu_to_le32(action);
	cmd->apply_time = cpu_to_le32(apply_time);
}

/*
 * Add the phy configuration to the PHY context command
 */
static void iwl_mvm_phy_ctxt_cmd_data(struct iwl_mvm *mvm,
				      struct iwl_phy_context_cmd *cmd,
				      struct cfg80211_chan_def *chandef,
				      u8 chains_static, u8 chains_dynamic)
{
	u8 valid_rx_chains, active_cnt, idle_cnt;

	/* Set the channel info data */
	cmd->ci.band = (chandef->chan->band == IEEE80211_BAND_2GHZ ?
	      PHY_BAND_24 : PHY_BAND_5);

	cmd->ci.channel = chandef->chan->hw_value;
	cmd->ci.width = iwl_mvm_get_channel_width(chandef);
	cmd->ci.ctrl_pos = iwl_mvm_get_ctrl_pos(chandef);

	/* Set rx the chains */

	/* TODO:
	 * Need to add on chain noise calibration limitations, and
	 * BT coex considerations.
	 */
	valid_rx_chains = mvm->nvm_data->valid_rx_ant;
	idle_cnt = chains_static;
	active_cnt = chains_dynamic;

	cmd->rxchain_info = cpu_to_le32(valid_rx_chains <<
					PHY_RX_CHAIN_VALID_POS);
	cmd->rxchain_info |= cpu_to_le32(idle_cnt << PHY_RX_CHAIN_CNT_POS);
	cmd->rxchain_info |= cpu_to_le32(active_cnt <<
					 PHY_RX_CHAIN_MIMO_CNT_POS);

	cmd->txchain_info = cpu_to_le32(mvm->nvm_data->valid_tx_ant);
}

/*
 * Send a command to apply the current phy configuration. The command is send
 * only if something in the configuration changed: in case that this is the
 * first time that the phy configuration is applied or in case that the phy
 * configuration changed from the previous apply.
 */
static int iwl_mvm_phy_ctxt_apply(struct iwl_mvm *mvm,
				  struct iwl_mvm_phy_ctxt *ctxt,
				  struct cfg80211_chan_def *chandef,
				  u8 chains_static, u8 chains_dynamic,
				  u32 action, u32 apply_time)
{
	struct iwl_phy_context_cmd cmd;
	int ret;

	/* Set the command header fields */
	iwl_mvm_phy_ctxt_cmd_hdr(ctxt, &cmd, action, apply_time);

	/* Set the command data */
	iwl_mvm_phy_ctxt_cmd_data(mvm, &cmd, chandef,
				  chains_static, chains_dynamic);

	ret = iwl_mvm_send_cmd_pdu(mvm, PHY_CONTEXT_CMD, CMD_SYNC,
				   sizeof(struct iwl_phy_context_cmd),
				   &cmd);
	if (ret)
		IWL_ERR(mvm, "PHY ctxt cmd error. ret=%d\n", ret);
	return ret;
}


struct phy_ctx_used_data {
	unsigned long used[BITS_TO_LONGS(NUM_PHY_CTX)];
};

static void iwl_mvm_phy_ctx_used_iter(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx,
				      void *_data)
{
	struct phy_ctx_used_data *data = _data;
	struct iwl_mvm_phy_ctxt *phy_ctxt = (void *)ctx->drv_priv;

	__set_bit(phy_ctxt->id, data->used);
}

/*
 * Send a command to add a PHY context based on the current HW configuration.
 */
int iwl_mvm_phy_ctxt_add(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			 struct cfg80211_chan_def *chandef,
			 u8 chains_static, u8 chains_dynamic)
{
	struct phy_ctx_used_data data = {
		.used = { },
	};

	/*
	 * If this is a regular PHY context (not the ROC one)
	 * skip the ROC PHY context's ID.
	 */
	if (ctxt != &mvm->phy_ctxt_roc)
		__set_bit(mvm->phy_ctxt_roc.id, data.used);

	lockdep_assert_held(&mvm->mutex);
	ctxt->color++;

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		ieee80211_iter_chan_contexts_atomic(
			mvm->hw, iwl_mvm_phy_ctx_used_iter, &data);

		ctxt->id = find_first_zero_bit(data.used, NUM_PHY_CTX);
		if (WARN_ONCE(ctxt->id == NUM_PHY_CTX,
			      "Failed to init PHY context - no free ID!\n"))
			return -EIO;
	}

	ctxt->channel = chandef->chan;
	return iwl_mvm_phy_ctxt_apply(mvm, ctxt, chandef,
				      chains_static, chains_dynamic,
				      FW_CTXT_ACTION_ADD, 0);
}

/*
 * Send a command to modify the PHY context based on the current HW
 * configuration. Note that the function does not check that the configuration
 * changed.
 */
int iwl_mvm_phy_ctxt_changed(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt,
			     struct cfg80211_chan_def *chandef,
			     u8 chains_static, u8 chains_dynamic)
{
	lockdep_assert_held(&mvm->mutex);

	ctxt->channel = chandef->chan;
	return iwl_mvm_phy_ctxt_apply(mvm, ctxt, chandef,
				      chains_static, chains_dynamic,
				      FW_CTXT_ACTION_MODIFY, 0);
}

/*
 * Send a command to the FW to remove the given phy context.
 * Once the command is sent, regardless of success or failure, the context is
 * marked as invalid
 */
void iwl_mvm_phy_ctxt_remove(struct iwl_mvm *mvm, struct iwl_mvm_phy_ctxt *ctxt)
{
	struct iwl_phy_context_cmd cmd;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_phy_ctxt_cmd_hdr(ctxt, &cmd, FW_CTXT_ACTION_REMOVE, 0);
	ret = iwl_mvm_send_cmd_pdu(mvm, PHY_CONTEXT_CMD, CMD_SYNC,
				   sizeof(struct iwl_phy_context_cmd),
				   &cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send PHY remove: ctxt id=%d\n",
			ctxt->id);
}
