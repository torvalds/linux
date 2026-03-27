// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include <linux/dmi.h>

#include "fw/regulatory.h"
#include "fw/acpi.h"
#include "fw/uefi.h"

#include "regulatory.h"
#include "mld.h"
#include "hcmd.h"

void iwl_mld_get_bios_tables(struct iwl_mld *mld)
{
	int ret;

	iwl_acpi_get_guid_lock_status(&mld->fwrt);

	ret = iwl_bios_get_ppag_table(&mld->fwrt);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mld,
				"PPAG BIOS table invalid or unavailable. (%d)\n",
				ret);
	}

	ret = iwl_bios_get_wrds_table(&mld->fwrt);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mld,
				"WRDS SAR BIOS table invalid or unavailable. (%d)\n",
				ret);

		/* If not available, don't fail and don't bother with EWRD and
		 * WGDS
		 */

		if (!iwl_bios_get_wgds_table(&mld->fwrt)) {
			/* If basic SAR is not available, we check for WGDS,
			 * which should *not* be available either. If it is
			 * available, issue an error, because we can't use SAR
			 * Geo without basic SAR.
			 */
			IWL_ERR(mld, "BIOS contains WGDS but no WRDS\n");
		}

	} else {
		ret = iwl_bios_get_ewrd_table(&mld->fwrt);
		/* If EWRD is not available, we can still use
		 * WRDS, so don't fail.
		 */
		if (ret < 0)
			IWL_DEBUG_RADIO(mld,
					"EWRD SAR BIOS table invalid or unavailable. (%d)\n",
					ret);

		ret = iwl_bios_get_wgds_table(&mld->fwrt);
		if (ret < 0)
			IWL_DEBUG_RADIO(mld,
					"Geo SAR BIOS table invalid or unavailable. (%d)\n",
					ret);
		/* we don't fail if the table is not available */
	}

	iwl_uefi_get_uats_table(mld->trans, &mld->fwrt);
	iwl_uefi_get_uneb_table(mld->trans, &mld->fwrt);

	iwl_bios_get_phy_filters(&mld->fwrt);
}

static int iwl_mld_geo_sar_init(struct iwl_mld *mld)
{
	u32 cmd_id = WIDE_ID(PHY_OPS_GROUP, PER_CHAIN_LIMIT_OFFSET_CMD);
	/* Only set to South Korea if the table revision is 1 */
	u8 sk = mld->fwrt.geo_rev == 1 ? 1 : 0;
	union iwl_geo_tx_power_profiles_cmd cmd = {
		.v5.ops = cpu_to_le32(IWL_PER_CHAIN_OFFSET_SET_TABLES),
	};
	u32 cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, cmd_id, 0);
	int n_subbands;
	int cmd_size;
	int ret;

	switch (cmd_ver) {
	case 5:
		n_subbands = ARRAY_SIZE(cmd.v5.table[0]);
		cmd.v5.table_revision = cpu_to_le32(sk);
		cmd_size = sizeof(cmd.v5);
		break;
	case 6:
		n_subbands = ARRAY_SIZE(cmd.v6.table[0]);
		cmd.v6.bios_hdr.table_revision = mld->fwrt.geo_rev;
		cmd.v6.bios_hdr.table_source = mld->fwrt.geo_bios_source;
		cmd_size = sizeof(cmd.v6);
		break;
	default:
		WARN(false, "unsupported version: %d", cmd_ver);
		return -EINVAL;
	}

	BUILD_BUG_ON(offsetof(typeof(cmd), v6.table) !=
		     offsetof(typeof(cmd), v5.table));
	ret = iwl_sar_geo_fill_table(&mld->fwrt, &cmd.v6.table[0][0],
				     n_subbands, BIOS_GEO_MAX_PROFILE_NUM);

	/* It is a valid scenario to not support SAR, or miss wgds table,
	 * but in that case there is no need to send the command.
	 */
	if (ret)
		return 0;

	return iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd, cmd_size);
}

int iwl_mld_config_sar_profile(struct iwl_mld *mld, int prof_a, int prof_b)
{
	struct iwl_dev_tx_power_cmd cmd = {
		.common.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_CHAINS),
	};
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, REDUCE_TX_POWER_CMD, 10);
	int num_subbands;
	int cmd_size;
	int ret;

	switch (cmd_ver) {
	case 10:
		cmd.v10.flags = cpu_to_le32(mld->fwrt.reduced_power_flags);
		cmd_size = sizeof(cmd.common) + sizeof(cmd.v10);
		num_subbands = IWL_NUM_SUB_BANDS_V2;
		break;
	case 11:
		cmd.v11.flags = cpu_to_le32(mld->fwrt.reduced_power_flags);
		cmd_size = sizeof(cmd.common) + sizeof(cmd.v11);
		num_subbands = IWL_NUM_SUB_BANDS_V3;
		break;
	default:
		WARN_ONCE(1, "Bad version for REDUCE_TX_POWER_CMD: %d\n",
			  cmd_ver);
		return -EOPNOTSUPP;
	}

	/* TODO: CDB - support IWL_NUM_CHAIN_TABLES_V2 */
	/* v10 and v11 have the same position for per_chain */
	BUILD_BUG_ON(offsetof(typeof(cmd), v11.per_chain) !=
		     offsetof(typeof(cmd), v10.per_chain));
	ret = iwl_sar_fill_profile(&mld->fwrt, &cmd.v11.per_chain[0][0][0],
				   IWL_NUM_CHAIN_TABLES, num_subbands,
				   prof_a, prof_b);
	/* return on error or if the profile is disabled (positive number) */
	if (ret)
		return ret;

	return iwl_mld_send_cmd_pdu(mld, REDUCE_TX_POWER_CMD, &cmd, cmd_size);
}

int iwl_mld_init_sar(struct iwl_mld *mld)
{
	int chain_a_prof = 1;
	int chain_b_prof = 1;
	int ret;

	/* If no profile was chosen by the user yet, choose profile 1 (WRDS) as
	 * default for both chains
	 */
	if (mld->fwrt.sar_chain_a_profile && mld->fwrt.sar_chain_b_profile) {
		chain_a_prof = mld->fwrt.sar_chain_a_profile;
		chain_b_prof = mld->fwrt.sar_chain_b_profile;
	}

	ret = iwl_mld_config_sar_profile(mld, chain_a_prof, chain_b_prof);
	if (ret < 0)
		return ret;

	if (ret)
		return 0;

	return iwl_mld_geo_sar_init(mld);
}

int iwl_mld_init_sgom(struct iwl_mld *mld)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP,
			      SAR_OFFSET_MAPPING_TABLE_CMD),
		.data[0] = &mld->fwrt.sgom_table,
		.len[0] =  sizeof(mld->fwrt.sgom_table),
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};

	if (!mld->fwrt.sgom_enabled) {
		IWL_DEBUG_RADIO(mld, "SGOM table is disabled\n");
		return 0;
	}

	ret = iwl_mld_send_cmd(mld, &cmd);
	if (ret)
		IWL_ERR(mld,
			"failed to send SAR_OFFSET_MAPPING_CMD (%d)\n", ret);

	return ret;
}

static int iwl_mld_ppag_send_cmd(struct iwl_mld *mld)
{
	struct iwl_fw_runtime *fwrt = &mld->fwrt;
	union iwl_ppag_table_cmd cmd = {
		/* v7 and v8 have the same layout for the ppag_config_info */
		.v8.ppag_config_info.hdr.table_source = fwrt->ppag_bios_source,
		.v8.ppag_config_info.hdr.table_revision = fwrt->ppag_bios_rev,
		.v8.ppag_config_info.value = cpu_to_le32(fwrt->ppag_flags),
	};
	u32 cmd_id = WIDE_ID(PHY_OPS_GROUP, PER_PLATFORM_ANT_GAIN_CMD);
	int cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, cmd_id, 1);
	int cmd_len = sizeof(cmd.v8);
	u8 cmd_bios_rev;
	int ret;

	BUILD_BUG_ON(offsetof(typeof(cmd), v8.ppag_config_info.hdr) !=
		     offsetof(typeof(cmd), v7.ppag_config_info.hdr));
	BUILD_BUG_ON(offsetof(typeof(cmd), v8.gain) !=
		     offsetof(typeof(cmd), v7.gain));

	BUILD_BUG_ON(ARRAY_SIZE(cmd.v7.gain) > ARRAY_SIZE(fwrt->ppag_chains));
	BUILD_BUG_ON(ARRAY_SIZE(cmd.v7.gain[0]) >
		     ARRAY_SIZE(fwrt->ppag_chains[0].subbands));
	BUILD_BUG_ON(ARRAY_SIZE(cmd.v8.gain) > ARRAY_SIZE(fwrt->ppag_chains));
	BUILD_BUG_ON(ARRAY_SIZE(cmd.v8.gain[0]) >
		     ARRAY_SIZE(fwrt->ppag_chains[0].subbands));

	IWL_DEBUG_RADIO(fwrt,
			"PPAG MODE bits going to be sent: %d\n",
			fwrt->ppag_flags);

	/* Since ver 7 will be deprecated at some point, don't bother making
	 * this code generic for both ver 7 and ver 8: duplicate the code.
	 */
	if (cmd_ver == 7) {
		for (int chain = 0; chain < ARRAY_SIZE(cmd.v7.gain); chain++) {
			for (int subband = 0;
			     subband < ARRAY_SIZE(cmd.v7.gain[0]);
			     subband++) {
				cmd.v7.gain[chain][subband] =
					fwrt->ppag_chains[chain].subbands[subband];
				IWL_DEBUG_RADIO(fwrt,
						"PPAG table: chain[%d] band[%d]: gain = %d\n",
						chain, subband,
						cmd.v7.gain[chain][subband]);
			}
		}
		cmd_len = sizeof(cmd.v7);
		cmd_bios_rev =
			iwl_fw_lookup_cmd_bios_supported_revision(fwrt->fw,
								  fwrt->ppag_bios_source,
								  cmd_id, 4);
	} else if (cmd_ver == 8) {
		for (int chain = 0; chain < ARRAY_SIZE(cmd.v8.gain); chain++) {
			for (int subband = 0;
			     subband < ARRAY_SIZE(cmd.v8.gain[0]);
			     subband++) {
				cmd.v8.gain[chain][subband] =
					fwrt->ppag_chains[chain].subbands[subband];
				IWL_DEBUG_RADIO(fwrt,
						"PPAG table: chain[%d] band[%d]: gain = %d\n",
						chain, subband,
						cmd.v8.gain[chain][subband]);
			}
		}
		cmd_bios_rev =
			iwl_fw_lookup_cmd_bios_supported_revision(fwrt->fw,
								  fwrt->ppag_bios_source,
								  cmd_id, 5);
	} else {
		WARN(1, "Bad version for PER_PLATFORM_ANT_GAIN_CMD %d\n",
		     cmd_ver);
		return -EINVAL;
	}

	if (cmd_bios_rev < fwrt->ppag_bios_rev) {
		IWL_ERR(mld,
			"BIOS revision compatibility check failed - Supported: %d, Current: %d\n",
			cmd_bios_rev, fwrt->ppag_bios_rev);
		return 0;
	}

	IWL_DEBUG_RADIO(mld, "Sending PER_PLATFORM_ANT_GAIN_CMD\n");
	ret = iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd, cmd_len);
	if (ret < 0)
		IWL_ERR(mld, "failed to send PER_PLATFORM_ANT_GAIN_CMD (%d)\n",
			ret);

	return ret;
}

int iwl_mld_init_ppag(struct iwl_mld *mld)
{
	/* no need to read the table, done in INIT stage */

	if (!(iwl_is_ppag_approved(&mld->fwrt)))
		return 0;

	return iwl_mld_ppag_send_cmd(mld);
}

static __le32 iwl_mld_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt)
{
	int ret;
	u32 val;

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_DISABLE_SRD, &val);
	if (!ret) {
		if (val == DSM_VALUE_SRD_PASSIVE)
			return cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_PASSIVE_MSK);
		else if (val == DSM_VALUE_SRD_DISABLE)
			return cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_DISABLED_MSK);
	}

	return 0;
}

void iwl_mld_configure_lari(struct iwl_mld *mld)
{
	struct iwl_fw_runtime *fwrt = &mld->fwrt;
	struct iwl_lari_config_change_cmd cmd = {
		.config_bitmap = iwl_mld_get_lari_config_bitmap(fwrt),
	};
	bool has_raw_dsm_capa = fw_has_capa(&fwrt->fw->ucode_capa,
					    IWL_UCODE_TLV_CAPA_FW_ACCEPTS_RAW_DSM_TABLE);
	int ret;
	u32 value;

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_11AX_ENABLEMENT, &value);
	if (!ret) {
		if (!has_raw_dsm_capa)
			value &= DSM_11AX_ALLOW_BITMAP;
		cmd.oem_11ax_allow_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_UNII4_CHAN, &value);
	if (!ret) {
		if (!has_raw_dsm_capa)
			value &= DSM_UNII4_ALLOW_BITMAP;
		cmd.oem_unii4_allow_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ACTIVATE_CHANNEL, &value);
	if (!ret) {
		if (!has_raw_dsm_capa)
			value &= CHAN_STATE_ACTIVE_BITMAP_CMD_V12;
		cmd.chan_state_active_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_6E, &value);
	if (!ret)
		cmd.oem_uhb_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_FORCE_DISABLE_CHANNELS, &value);
	if (!ret) {
		if (!has_raw_dsm_capa)
			value &= DSM_FORCE_DISABLE_CHANNELS_ALLOWED_BITMAP;
		cmd.force_disable_channels_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENERGY_DETECTION_THRESHOLD,
			       &value);
	if (!ret) {
		if (!has_raw_dsm_capa)
			value &= DSM_EDT_ALLOWED_BITMAP;
		cmd.edt_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_wbem(fwrt, &value);
	if (!ret)
		cmd.oem_320mhz_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_11BE, &value);
	if (!ret)
		cmd.oem_11be_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_11BN, &value);
	if (!ret)
		cmd.oem_11bn_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_UNII_9, &value);
	if (!ret)
		cmd.oem_unii9_enable = cpu_to_le32(value);

	if (!cmd.config_bitmap &&
	    !cmd.oem_uhb_allow_bitmap &&
	    !cmd.oem_11ax_allow_bitmap &&
	    !cmd.oem_unii4_allow_bitmap &&
	    !cmd.chan_state_active_bitmap &&
	    !cmd.force_disable_channels_bitmap &&
	    !cmd.edt_bitmap &&
	    !cmd.oem_320mhz_allow_bitmap &&
	    !cmd.oem_11be_allow_bitmap &&
	    !cmd.oem_11bn_allow_bitmap &&
	    !cmd.oem_unii9_enable)
		return;

	cmd.bios_hdr.table_source = fwrt->dsm_source;
	cmd.bios_hdr.table_revision = fwrt->dsm_revision;

	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, config_bitmap=0x%x, oem_11ax_allow_bitmap=0x%x\n",
			le32_to_cpu(cmd.config_bitmap),
			le32_to_cpu(cmd.oem_11ax_allow_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, oem_unii4_allow_bitmap=0x%x, chan_state_active_bitmap=0x%x\n",
			le32_to_cpu(cmd.oem_unii4_allow_bitmap),
			le32_to_cpu(cmd.chan_state_active_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, oem_uhb_allow_bitmap=0x%x, force_disable_channels_bitmap=0x%x\n",
			le32_to_cpu(cmd.oem_uhb_allow_bitmap),
			le32_to_cpu(cmd.force_disable_channels_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, edt_bitmap=0x%x, oem_320mhz_allow_bitmap=0x%x\n",
			le32_to_cpu(cmd.edt_bitmap),
			le32_to_cpu(cmd.oem_320mhz_allow_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, oem_11be_allow_bitmap=0x%x\n",
			le32_to_cpu(cmd.oem_11be_allow_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, oem_11bn_allow_bitmap=0x%x\n",
			le32_to_cpu(cmd.oem_11bn_allow_bitmap));
	IWL_DEBUG_RADIO(mld,
			"sending LARI_CONFIG_CHANGE, oem_unii9_enable=0x%x\n",
			le32_to_cpu(cmd.oem_unii9_enable));

	if (iwl_fw_lookup_cmd_ver(mld->fw,
				  WIDE_ID(REGULATORY_AND_NVM_GROUP,
					  LARI_CONFIG_CHANGE), 12) == 12) {
		int cmd_size = offsetof(typeof(cmd), oem_11bn_allow_bitmap);

		ret = iwl_mld_send_cmd_pdu(mld,
					   WIDE_ID(REGULATORY_AND_NVM_GROUP,
						   LARI_CONFIG_CHANGE),
					   &cmd, cmd_size);
	} else {
		ret = iwl_mld_send_cmd_pdu(mld,
					   WIDE_ID(REGULATORY_AND_NVM_GROUP,
						   LARI_CONFIG_CHANGE),
					   &cmd);
	}
	if (ret)
		IWL_DEBUG_RADIO(mld,
				"Failed to send LARI_CONFIG_CHANGE (%d)\n",
				ret);
}

void iwl_mld_init_ap_type_tables(struct iwl_mld *mld)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP,
			      MCC_ALLOWED_AP_TYPE_CMD),
		.data[0] = &mld->fwrt.ap_type_cmd,
		.len[0] =  sizeof(mld->fwrt.ap_type_cmd),
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};

	if (!mld->fwrt.ap_type_cmd_valid)
		return;

	if (iwl_fw_lookup_cmd_ver(mld->fw, cmd.id, 1) == 1) {
		struct iwl_mcc_allowed_ap_type_cmd_v1 *cmd_v1 =
			kzalloc(sizeof(*cmd_v1), GFP_KERNEL);

		if (!cmd_v1)
			return;

		BUILD_BUG_ON(sizeof(mld->fwrt.ap_type_cmd.mcc_to_ap_type_map) !=
			     sizeof(cmd_v1->mcc_to_ap_type_map));

		memcpy(cmd_v1->mcc_to_ap_type_map,
		       mld->fwrt.ap_type_cmd.mcc_to_ap_type_map,
		       sizeof(mld->fwrt.ap_type_cmd.mcc_to_ap_type_map));

		cmd.data[0] = cmd_v1;
		cmd.len[0] = sizeof(*cmd_v1);
		ret = iwl_mld_send_cmd(mld, &cmd);
		kfree(cmd_v1);
	} else {
		ret = iwl_mld_send_cmd(mld, &cmd);
	}

	if (ret)
		IWL_ERR(mld, "failed to send MCC_ALLOWED_AP_TYPE_CMD (%d)\n",
			ret);
}

void iwl_mld_init_tas(struct iwl_mld *mld)
{
	int ret;
	struct iwl_tas_data data = {};
	struct iwl_tas_config_cmd cmd = {};
	u32 cmd_id = WIDE_ID(REGULATORY_AND_NVM_GROUP, TAS_CONFIG);

	BUILD_BUG_ON(ARRAY_SIZE(data.block_list_array) !=
		     IWL_WTAS_BLACK_LIST_MAX);
	BUILD_BUG_ON(ARRAY_SIZE(cmd.block_list_array) !=
		     IWL_WTAS_BLACK_LIST_MAX);

	if (!fw_has_capa(&mld->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TAS_CFG)) {
		IWL_DEBUG_RADIO(mld, "TAS not enabled in FW\n");
		return;
	}

	ret = iwl_bios_get_tas_table(&mld->fwrt, &data);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mld,
				"TAS table invalid or unavailable. (%d)\n",
				ret);
		return;
	}

	if (!iwl_is_tas_approved()) {
		IWL_DEBUG_RADIO(mld,
				"System vendor '%s' is not in the approved list, disabling TAS in US and Canada.\n",
				dmi_get_system_info(DMI_SYS_VENDOR) ?: "<unknown>");
		if ((!iwl_add_mcc_to_tas_block_list(data.block_list_array,
						    &data.block_list_size,
						    IWL_MCC_US)) ||
		    (!iwl_add_mcc_to_tas_block_list(data.block_list_array,
						    &data.block_list_size,
						    IWL_MCC_CANADA))) {
			IWL_DEBUG_RADIO(mld,
					"Unable to add US/Canada to TAS block list, disabling TAS\n");
			return;
		}
	} else {
		IWL_DEBUG_RADIO(mld,
				"System vendor '%s' is in the approved list.\n",
				dmi_get_system_info(DMI_SYS_VENDOR) ?: "<unknown>");
	}

	cmd.block_list_size = cpu_to_le16(data.block_list_size);
	for (u8 i = 0; i < data.block_list_size; i++)
		cmd.block_list_array[i] =
			cpu_to_le16(data.block_list_array[i]);
	cmd.tas_config_info.hdr.table_source = data.table_source;
	cmd.tas_config_info.hdr.table_revision = data.table_revision;
	cmd.tas_config_info.value = cpu_to_le32(data.tas_selection);

	ret = iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
	if (ret)
		IWL_DEBUG_RADIO(mld, "failed to send TAS_CONFIG (%d)\n", ret);
}
