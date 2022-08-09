// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2020 - 2021 Intel Corporation
 */

#include "mvm.h"
#include "fw/api/commands.h"
#include "fw/api/phy-ctxt.h"

/*
 * DDR needs frequency in units of 16.666MHz, so provide FW with the
 * frequency values in the adjusted format.
 */
static const struct iwl_rfi_lut_entry iwl_rfi_table[IWL_RFI_LUT_SIZE] = {
	/* frequency 2667MHz */
	{cpu_to_le16(160), {50, 58, 60, 62, 64, 52, 54, 56},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 2933MHz */
	{cpu_to_le16(176), {149, 151, 153, 157, 159, 161, 165, 163, 167, 169,
			    171, 173, 175},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 3200MHz */
	{cpu_to_le16(192), {79, 81, 83, 85, 87, 89, 91, 93},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 3733MHz */
	{cpu_to_le16(223), {114, 116, 118, 120, 122, 106, 110, 124, 126},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 4000MHz */
	{cpu_to_le16(240), {114, 151, 155, 157, 159, 161, 165},
	      {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
	       PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 4267MHz */
	{cpu_to_le16(256), {79, 83, 85, 87, 89, 91, 93,},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 4400MHz */
	{cpu_to_le16(264), {111, 119, 123, 125, 129, 131, 133, 135, 143,},
	      {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
	       PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 5200MHz */
	{cpu_to_le16(312), {36, 38, 40, 42, 44, 46, 50,},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
		PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 5600MHz */
	{cpu_to_le16(336), {106, 110, 112, 114, 116, 118, 120, 122},
	       {PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,
		PHY_BAND_5, PHY_BAND_5, PHY_BAND_5,}},

	/* frequency 6000MHz */
	{cpu_to_le16(360), {3, 5, 7, 9, 11, 13, 15,},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6,}},

	/* frequency 6400MHz */
	{cpu_to_le16(384), {79, 83, 85, 87, 89, 91, 93,},
	       {PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6, PHY_BAND_6,
		PHY_BAND_6, PHY_BAND_6,}},
};

int iwl_rfi_send_config_cmd(struct iwl_mvm *mvm, struct iwl_rfi_lut_entry *rfi_table)
{
	int ret;
	struct iwl_rfi_config_cmd cmd;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(SYSTEM_GROUP, RFI_CONFIG_CMD),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_RFIM_SUPPORT))
		return -EOPNOTSUPP;

	lockdep_assert_held(&mvm->mutex);

	/* in case no table is passed, use the default one */
	if (!rfi_table) {
		memcpy(cmd.table, iwl_rfi_table, sizeof(cmd.table));
	} else {
		memcpy(cmd.table, rfi_table, sizeof(cmd.table));
		/* notify FW the table is not the default one */
		cmd.oem = 1;
	}

	ret = iwl_mvm_send_cmd(mvm, &hcmd);

	if (ret)
		IWL_ERR(mvm, "Failed to send RFI config cmd %d\n", ret);

	return ret;
}

struct iwl_rfi_freq_table_resp_cmd *iwl_rfi_get_freq_table(struct iwl_mvm *mvm)
{
	struct iwl_rfi_freq_table_resp_cmd *resp;
	int resp_size = sizeof(*resp);
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(SYSTEM_GROUP, RFI_GET_FREQ_TABLE_CMD),
		.flags = CMD_WANT_SKB,
	};

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_RFIM_SUPPORT))
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &cmd);
	mutex_unlock(&mvm->mutex);
	if (ret)
		return ERR_PTR(ret);

	if (WARN_ON_ONCE(iwl_rx_packet_payload_len(cmd.resp_pkt) != resp_size))
		return ERR_PTR(-EIO);

	resp = kzalloc(resp_size, GFP_KERNEL);
	if (!resp)
		return ERR_PTR(-ENOMEM);

	memcpy(resp, cmd.resp_pkt->data, resp_size);

	iwl_free_resp(&cmd);
	return resp;
}
