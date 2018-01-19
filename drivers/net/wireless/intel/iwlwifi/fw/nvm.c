/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
#include "iwl-drv.h"
#include "runtime.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/commands.h"
#include "iwl-nvm-parse.h"

struct iwl_nvm_data *iwl_fw_get_nvm(struct iwl_fw_runtime *fwrt)
{
	struct iwl_nvm_get_info cmd = {};
	struct iwl_nvm_get_info_rsp *rsp;
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_nvm_data *nvm;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP, NVM_GET_INFO)
	};
	int  ret;
	bool lar_fw_supported = !iwlwifi_mod_params.lar_disable &&
				fw_has_capa(&fwrt->fw->ucode_capa,
					    IWL_UCODE_TLV_CAPA_LAR_SUPPORT);

	ret = iwl_trans_send_cmd(trans, &hcmd);
	if (ret)
		return ERR_PTR(ret);

	if (WARN(iwl_rx_packet_payload_len(hcmd.resp_pkt) != sizeof(*rsp),
		 "Invalid payload len in NVM response from FW %d",
		 iwl_rx_packet_payload_len(hcmd.resp_pkt))) {
		ret = -EINVAL;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	if (le32_to_cpu(rsp->general.flags) & NVM_GENERAL_FLAGS_EMPTY_OTP)
		IWL_INFO(fwrt, "OTP is empty\n");

	nvm = kzalloc(sizeof(*nvm) +
		      sizeof(struct ieee80211_channel) * IWL_NUM_CHANNELS,
		      GFP_KERNEL);
	if (!nvm) {
		ret = -ENOMEM;
		goto out;
	}

	iwl_set_hw_address_from_csr(trans, nvm);
	/* TODO: if platform NVM has MAC address - override it here */

	if (!is_valid_ether_addr(nvm->hw_addr)) {
		IWL_ERR(fwrt, "no valid mac address was found\n");
		ret = -EINVAL;
		goto err_free;
	}

	IWL_INFO(trans, "base HW address: %pM\n", nvm->hw_addr);

	/* Initialize general data */
	nvm->nvm_version = le16_to_cpu(rsp->general.nvm_version);

	/* Initialize MAC sku data */
	nvm->sku_cap_11ac_enable =
		le32_to_cpu(rsp->mac_sku.enable_11ac);
	nvm->sku_cap_11n_enable =
		le32_to_cpu(rsp->mac_sku.enable_11n);
	nvm->sku_cap_band_24GHz_enable =
		le32_to_cpu(rsp->mac_sku.enable_24g);
	nvm->sku_cap_band_52GHz_enable =
		le32_to_cpu(rsp->mac_sku.enable_5g);
	nvm->sku_cap_mimo_disabled =
		le32_to_cpu(rsp->mac_sku.mimo_disable);

	/* Initialize PHY sku data */
	nvm->valid_tx_ant = (u8)le32_to_cpu(rsp->phy_sku.tx_chains);
	nvm->valid_rx_ant = (u8)le32_to_cpu(rsp->phy_sku.rx_chains);

	/* Initialize regulatory data */
	nvm->lar_enabled =
		le32_to_cpu(rsp->regulatory.lar_enabled) && lar_fw_supported;

	iwl_init_sbands(trans->dev, trans->cfg, nvm,
			rsp->regulatory.channel_profile,
			nvm->valid_tx_ant & fwrt->fw->valid_tx_ant,
			nvm->valid_rx_ant & fwrt->fw->valid_rx_ant,
			nvm->lar_enabled, false);

	iwl_free_resp(&hcmd);
	return nvm;

err_free:
	kfree(nvm);
out:
	iwl_free_resp(&hcmd);
	return ERR_PTR(ret);
}
IWL_EXPORT_SYMBOL(iwl_fw_get_nvm);
