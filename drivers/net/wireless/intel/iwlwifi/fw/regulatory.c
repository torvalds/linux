// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2023 Intel Corporation
 */
#include <linux/dmi.h>
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "regulatory.h"
#include "fw/runtime.h"
#include "fw/uefi.h"

#define GET_BIOS_TABLE(__name, ...)					\
do {									\
	int ret = -ENOENT;						\
	if (fwrt->uefi_tables_lock_status > UEFI_WIFI_GUID_UNLOCKED)	\
		ret = iwl_uefi_get_ ## __name(__VA_ARGS__);		\
	if (ret < 0)							\
		ret = iwl_acpi_get_ ## __name(__VA_ARGS__);		\
	return ret;							\
} while (0)

#define IWL_BIOS_TABLE_LOADER(__name)					\
int iwl_bios_get_ ## __name(struct iwl_fw_runtime *fwrt)		\
{GET_BIOS_TABLE(__name, fwrt); }					\
IWL_EXPORT_SYMBOL(iwl_bios_get_ ## __name)

#define IWL_BIOS_TABLE_LOADER_DATA(__name, data_type)			\
int iwl_bios_get_ ## __name(struct iwl_fw_runtime *fwrt,		\
			    data_type * data)				\
{GET_BIOS_TABLE(__name, fwrt, data); }					\
IWL_EXPORT_SYMBOL(iwl_bios_get_ ## __name)

IWL_BIOS_TABLE_LOADER(wrds_table);
IWL_BIOS_TABLE_LOADER(ewrd_table);
IWL_BIOS_TABLE_LOADER(wgds_table);
IWL_BIOS_TABLE_LOADER(ppag_table);
IWL_BIOS_TABLE_LOADER_DATA(tas_table, struct iwl_tas_data);
IWL_BIOS_TABLE_LOADER_DATA(pwr_limit, u64);
IWL_BIOS_TABLE_LOADER_DATA(mcc, char);
IWL_BIOS_TABLE_LOADER_DATA(eckv, u32);
IWL_BIOS_TABLE_LOADER_DATA(wbem, u32);
IWL_BIOS_TABLE_LOADER_DATA(dsbr, u32);


static const struct dmi_system_id dmi_ppag_approved_list[] = {
	{ .ident = "HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		},
	},
	{ .ident = "SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
	{ .ident = "MSFT",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
		},
	},
	{ .ident = "ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		},
	},
	{ .ident = "GOOGLE-HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "HP"),
		},
	},
	{ .ident = "GOOGLE-ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTek COMPUTER INC."),
		},
	},
	{ .ident = "GOOGLE-SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
		},
	},
	{ .ident = "RAZER",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Razer"),
		},
	},
	{ .ident = "Honor",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HONOR"),
		},
	},
	{ .ident = "WIKO",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "WIKO"),
		},
	},
	{}
};

static const struct dmi_system_id dmi_tas_approved_list[] = {
	{ .ident = "HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		},
	},
	{ .ident = "SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
		{ .ident = "LENOVO",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{ .ident = "MSFT",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
		},
	},
	{ .ident = "Acer",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		},
	},
	{ .ident = "ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		},
	},
	{ .ident = "GOOGLE-HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "HP"),
		},
	},
	{ .ident = "MSI",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International Co., Ltd."),
		},
	},
	{ .ident = "Honor",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HONOR"),
		},
	},
	/* keep last */
	{}
};

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt)
{
	/*
	 * The PER_CHAIN_LIMIT_OFFSET_CMD command is not supported on
	 * earlier firmware versions.  Unfortunately, we don't have a
	 * TLV API flag to rely on, so rely on the major version which
	 * is in the first byte of ucode_ver.  This was implemented
	 * initially on version 38 and then backported to 17.  It was
	 * also backported to 29, but only for 7265D devices.  The
	 * intention was to have it in 36 as well, but not all 8000
	 * family got this feature enabled.  The 8000 family is the
	 * only one using version 36, so skip this version entirely.
	 */
	return IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) >= 38 ||
		(IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 17 &&
		 fwrt->trans->hw_rev != CSR_HW_REV_TYPE_3160) ||
		(IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 29 &&
		 ((fwrt->trans->hw_rev & CSR_HW_REV_TYPE_MSK) ==
		  CSR_HW_REV_TYPE_7265D));
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_support);

int iwl_sar_geo_fill_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_per_chain_offset *table,
			   u32 n_bands, u32 n_profiles)
{
	int i, j;

	if (!fwrt->geo_enabled)
		return -ENODATA;

	if (!iwl_sar_geo_support(fwrt))
		return -EOPNOTSUPP;

	for (i = 0; i < n_profiles; i++) {
		for (j = 0; j < n_bands; j++) {
			struct iwl_per_chain_offset *chain =
				&table[i * n_bands + j];

			chain->max_tx_power =
				cpu_to_le16(fwrt->geo_profiles[i].bands[j].max);
			chain->chain_a =
				fwrt->geo_profiles[i].bands[j].chains[0];
			chain->chain_b =
				fwrt->geo_profiles[i].bands[j].chains[1];
			IWL_DEBUG_RADIO(fwrt,
					"SAR geographic profile[%d] Band[%d]: chain A = %d chain B = %d max_tx_power = %d\n",
					i, j,
					fwrt->geo_profiles[i].bands[j].chains[0],
					fwrt->geo_profiles[i].bands[j].chains[1],
					fwrt->geo_profiles[i].bands[j].max);
		}
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_fill_table);

static int iwl_sar_fill_table(struct iwl_fw_runtime *fwrt,
			      __le16 *per_chain, u32 n_subbands,
			      int prof_a, int prof_b)
{
	int profs[BIOS_SAR_NUM_CHAINS] = { prof_a, prof_b };
	int i, j;

	for (i = 0; i < BIOS_SAR_NUM_CHAINS; i++) {
		struct iwl_sar_profile *prof;

		/* don't allow SAR to be disabled (profile 0 means disable) */
		if (profs[i] == 0)
			return -EPERM;

		/* we are off by one, so allow up to BIOS_SAR_MAX_PROFILE_NUM */
		if (profs[i] > BIOS_SAR_MAX_PROFILE_NUM)
			return -EINVAL;

		/* profiles go from 1 to 4, so decrement to access the array */
		prof = &fwrt->sar_profiles[profs[i] - 1];

		/* if the profile is disabled, do nothing */
		if (!prof->enabled) {
			IWL_DEBUG_RADIO(fwrt, "SAR profile %d is disabled.\n",
					profs[i]);
			/*
			 * if one of the profiles is disabled, we
			 * ignore all of them and return 1 to
			 * differentiate disabled from other failures.
			 */
			return 1;
		}

		IWL_DEBUG_INFO(fwrt,
			       "SAR EWRD: chain %d profile index %d\n",
			       i, profs[i]);
		IWL_DEBUG_RADIO(fwrt, "  Chain[%d]:\n", i);
		for (j = 0; j < n_subbands; j++) {
			per_chain[i * n_subbands + j] =
				cpu_to_le16(prof->chains[i].subbands[j]);
			IWL_DEBUG_RADIO(fwrt, "    Band[%d] = %d * .125dBm\n",
					j, prof->chains[i].subbands[j]);
		}
	}

	return 0;
}

int iwl_sar_fill_profile(struct iwl_fw_runtime *fwrt,
			 __le16 *per_chain, u32 n_tables, u32 n_subbands,
			 int prof_a, int prof_b)
{
	int i, ret = 0;

	for (i = 0; i < n_tables; i++) {
		ret = iwl_sar_fill_table(fwrt,
			&per_chain[i * n_subbands * BIOS_SAR_NUM_CHAINS],
			n_subbands, prof_a, prof_b);
		if (ret)
			break;
	}

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_fill_profile);

static bool iwl_ppag_value_valid(struct iwl_fw_runtime *fwrt, int chain,
				 int subband)
{
	s8 ppag_val = fwrt->ppag_chains[chain].subbands[subband];

	if ((subband == 0 &&
	     (ppag_val > IWL_PPAG_MAX_LB || ppag_val < IWL_PPAG_MIN_LB)) ||
	    (subband != 0 &&
	     (ppag_val > IWL_PPAG_MAX_HB || ppag_val < IWL_PPAG_MIN_HB))) {
		IWL_DEBUG_RADIO(fwrt, "Invalid PPAG value: %d\n", ppag_val);
		return false;
	}
	return true;
}

int iwl_fill_ppag_table(struct iwl_fw_runtime *fwrt,
			union iwl_ppag_table_cmd *cmd, int *cmd_size)
{
	u8 cmd_ver;
	int i, j, num_sub_bands;
	s8 *gain;
	bool send_ppag_always;

	/* many firmware images for JF lie about this */
	if (CSR_HW_RFID_TYPE(fwrt->trans->hw_rf_id) ==
	    CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_JF))
		return -EOPNOTSUPP;

	if (!fw_has_capa(&fwrt->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SET_PPAG)) {
		IWL_DEBUG_RADIO(fwrt,
				"PPAG capability not supported by FW, command not sent.\n");
		return -EINVAL;
	}

	cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					WIDE_ID(PHY_OPS_GROUP,
						PER_PLATFORM_ANT_GAIN_CMD), 1);
	/*
	 * Starting from ver 4, driver needs to send the PPAG CMD regardless
	 * if PPAG is enabled/disabled or valid/invalid.
	 */
	send_ppag_always = cmd_ver > 3;

	/* Don't send PPAG if it is disabled */
	if (!send_ppag_always && !fwrt->ppag_flags) {
		IWL_DEBUG_RADIO(fwrt, "PPAG not enabled, command not sent.\n");
		return -EINVAL;
	}

	/* The 'flags' field is the same in v1 and in v2 so we can just
	 * use v1 to access it.
	 */
	cmd->v1.flags = cpu_to_le32(fwrt->ppag_flags);

	IWL_DEBUG_RADIO(fwrt, "PPAG cmd ver is %d\n", cmd_ver);
	if (cmd_ver == 1) {
		num_sub_bands = IWL_NUM_SUB_BANDS_V1;
		gain = cmd->v1.gain[0];
		*cmd_size = sizeof(cmd->v1);
		if (fwrt->ppag_ver >= 1) {
			/* in this case FW supports revision 0 */
			IWL_DEBUG_RADIO(fwrt,
					"PPAG table rev is %d, send truncated table\n",
					fwrt->ppag_ver);
		}
	} else if (cmd_ver >= 2 && cmd_ver <= 6) {
		num_sub_bands = IWL_NUM_SUB_BANDS_V2;
		gain = cmd->v2.gain[0];
		*cmd_size = sizeof(cmd->v2);
		if (fwrt->ppag_ver == 0) {
			/* in this case FW supports revisions 1,2 or 3 */
			IWL_DEBUG_RADIO(fwrt,
					"PPAG table rev is 0, send padded table\n");
		}
	} else {
		IWL_DEBUG_RADIO(fwrt, "Unsupported PPAG command version\n");
		return -EINVAL;
	}

	/* ppag mode */
	IWL_DEBUG_RADIO(fwrt,
			"PPAG MODE bits were read from bios: %d\n",
			le32_to_cpu(cmd->v1.flags));

	if (cmd_ver == 5)
		cmd->v1.flags &= cpu_to_le32(IWL_PPAG_CMD_V5_MASK);
	else if (cmd_ver < 5)
		cmd->v1.flags &= cpu_to_le32(IWL_PPAG_CMD_V4_MASK);

	if ((cmd_ver == 1 &&
	     !fw_has_capa(&fwrt->fw->ucode_capa,
			  IWL_UCODE_TLV_CAPA_PPAG_CHINA_BIOS_SUPPORT)) ||
	    (cmd_ver == 2 && fwrt->ppag_ver >= 2)) {
		cmd->v1.flags &= cpu_to_le32(IWL_PPAG_ETSI_MASK);
		IWL_DEBUG_RADIO(fwrt, "masking ppag China bit\n");
	} else {
		IWL_DEBUG_RADIO(fwrt, "isn't masking ppag China bit\n");
	}

	IWL_DEBUG_RADIO(fwrt,
			"PPAG MODE bits going to be sent: %d\n",
			le32_to_cpu(cmd->v1.flags));

	for (i = 0; i < IWL_NUM_CHAIN_LIMITS; i++) {
		for (j = 0; j < num_sub_bands; j++) {
			if (!send_ppag_always &&
			    !iwl_ppag_value_valid(fwrt, i, j))
				return -EINVAL;

			gain[i * num_sub_bands + j] =
				fwrt->ppag_chains[i].subbands[j];
			IWL_DEBUG_RADIO(fwrt,
					"PPAG table: chain[%d] band[%d]: gain = %d\n",
					i, j, gain[i * num_sub_bands + j]);
		}
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fill_ppag_table);

bool iwl_is_ppag_approved(struct iwl_fw_runtime *fwrt)
{
	if (!dmi_check_system(dmi_ppag_approved_list)) {
		IWL_DEBUG_RADIO(fwrt,
				"System vendor '%s' is not in the approved list, disabling PPAG.\n",
				dmi_get_system_info(DMI_SYS_VENDOR) ?: "<unknown>");
		fwrt->ppag_flags = 0;
		return false;
	}

	return true;
}
IWL_EXPORT_SYMBOL(iwl_is_ppag_approved);

bool iwl_is_tas_approved(void)
{
	return dmi_check_system(dmi_tas_approved_list);
}
IWL_EXPORT_SYMBOL(iwl_is_tas_approved);

struct iwl_tas_selection_data
iwl_parse_tas_selection(const u32 tas_selection_in, const u8 tbl_rev)
{
	struct iwl_tas_selection_data tas_selection_out = {};
	u8 override_iec = u32_get_bits(tas_selection_in,
				       IWL_WTAS_OVERRIDE_IEC_MSK);
	u8 canada_tas_uhb = u32_get_bits(tas_selection_in,
					 IWL_WTAS_CANADA_UHB_MSK);
	u8 enabled_iec = u32_get_bits(tas_selection_in,
				      IWL_WTAS_ENABLE_IEC_MSK);
	u8 usa_tas_uhb = u32_get_bits(tas_selection_in,
				      IWL_WTAS_USA_UHB_MSK);

	if (tbl_rev > 0) {
		tas_selection_out.usa_tas_uhb_allowed = usa_tas_uhb;
		tas_selection_out.override_tas_iec = override_iec;
		tas_selection_out.enable_tas_iec = enabled_iec;
	}

	if (tbl_rev > 1)
		tas_selection_out.canada_tas_uhb_allowed = canada_tas_uhb;

	return tas_selection_out;
}
IWL_EXPORT_SYMBOL(iwl_parse_tas_selection);

bool iwl_add_mcc_to_tas_block_list(u16 *list, u8 *size, u16 mcc)
{
	for (int i = 0; i < *size; i++) {
		if (list[i] == mcc)
			return true;
	}

	/* Verify that there is room for another country
	 * If *size == IWL_WTAS_BLACK_LIST_MAX, then the table is full.
	 */
	if (*size >= IWL_WTAS_BLACK_LIST_MAX)
		return false;

	list[*size++] = mcc;
	return true;
}
IWL_EXPORT_SYMBOL(iwl_add_mcc_to_tas_block_list);

__le32 iwl_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt)
{
	int ret;
	u32 val;
	__le32 config_bitmap = 0;

	switch (CSR_HW_RFID_TYPE(fwrt->trans->hw_rf_id)) {
	case IWL_CFG_RF_TYPE_HR1:
	case IWL_CFG_RF_TYPE_HR2:
	case IWL_CFG_RF_TYPE_JF1:
	case IWL_CFG_RF_TYPE_JF2:
		ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_INDONESIA_5G2,
				       &val);

		if (!ret && val == DSM_VALUE_INDONESIA_ENABLE)
			config_bitmap |=
			    cpu_to_le32(LARI_CONFIG_ENABLE_5G2_IN_INDONESIA_MSK);
		break;
	default:
		break;
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_DISABLE_SRD, &val);
	if (!ret) {
		if (val == DSM_VALUE_SRD_PASSIVE)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_PASSIVE_MSK);
		else if (val == DSM_VALUE_SRD_DISABLE)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_DISABLED_MSK);
	}

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_CHINA_22_REG_SUPPORT)) {
		ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_REGULATORY_CONFIG,
				       &val);
		/*
		 * China 2022 enable if the BIOS object does not exist or
		 * if it is enabled in BIOS.
		 */
		if (ret < 0 || val & DSM_MASK_CHINA_22_REG)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_ENABLE_CHINA_22_REG_SUPPORT_MSK);
	}

	return config_bitmap;
}
IWL_EXPORT_SYMBOL(iwl_get_lari_config_bitmap);

static size_t iwl_get_lari_config_cmd_size(u8 cmd_ver)
{
	size_t cmd_size;

	switch (cmd_ver) {
	case 12:
	case 11:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd);
		break;
	case 10:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v10);
		break;
	case 9:
	case 8:
	case 7:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v7);
		break;
	case 6:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v6);
		break;
	case 5:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v5);
		break;
	case 4:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v4);
		break;
	case 3:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v3);
		break;
	case 2:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v2);
		break;
	default:
		cmd_size = sizeof(struct iwl_lari_config_change_cmd_v1);
		break;
	}
	return cmd_size;
}

int iwl_fill_lari_config(struct iwl_fw_runtime *fwrt,
			 struct iwl_lari_config_change_cmd *cmd,
			 size_t *cmd_size)
{
	int ret;
	u32 value;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					   WIDE_ID(REGULATORY_AND_NVM_GROUP,
						   LARI_CONFIG_CHANGE), 1);

	if (WARN_ONCE(cmd_ver > 12,
		      "Don't add newer versions to this function\n"))
		return -EINVAL;

	memset(cmd, 0, sizeof(*cmd));
	*cmd_size = iwl_get_lari_config_cmd_size(cmd_ver);

	cmd->config_bitmap = iwl_get_lari_config_bitmap(fwrt);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_11AX_ENABLEMENT, &value);
	if (!ret)
		cmd->oem_11ax_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_UNII4_CHAN, &value);
	if (!ret) {
		value &= DSM_UNII4_ALLOW_BITMAP;

		/* Since version 9, bits 4 and 5 are supported
		 * regardless of this capability.
		 */
		if (cmd_ver < 9 &&
		    !fw_has_capa(&fwrt->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_BIOS_OVERRIDE_5G9_FOR_CA))
			value &= ~(DSM_VALUE_UNII4_CANADA_OVERRIDE_MSK |
				   DSM_VALUE_UNII4_CANADA_EN_MSK);

		cmd->oem_unii4_allow_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ACTIVATE_CHANNEL, &value);
	if (!ret) {
		if (cmd_ver < 8)
			value &= ~ACTIVATE_5G2_IN_WW_MASK;

		/* Since version 12, bits 5 and 6 are supported
		 * regardless of this capability.
		 */
		if (cmd_ver < 12 &&
		    !fw_has_capa(&fwrt->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_BIOS_OVERRIDE_UNII4_US_CA))
			value &= CHAN_STATE_ACTIVE_BITMAP_CMD_V11;

		cmd->chan_state_active_bitmap = cpu_to_le32(value);
	}

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_6E, &value);
	if (!ret)
		cmd->oem_uhb_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_FORCE_DISABLE_CHANNELS, &value);
	if (!ret)
		cmd->force_disable_channels_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENERGY_DETECTION_THRESHOLD,
			       &value);
	if (!ret)
		cmd->edt_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_wbem(fwrt, &value);
	if (!ret)
		cmd->oem_320mhz_allow_bitmap = cpu_to_le32(value);

	ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_ENABLE_11BE, &value);
	if (!ret)
		cmd->oem_11be_allow_bitmap = cpu_to_le32(value);

	if (cmd->config_bitmap ||
	    cmd->oem_uhb_allow_bitmap ||
	    cmd->oem_11ax_allow_bitmap ||
	    cmd->oem_unii4_allow_bitmap ||
	    cmd->chan_state_active_bitmap ||
	    cmd->force_disable_channels_bitmap ||
	    cmd->edt_bitmap ||
	    cmd->oem_320mhz_allow_bitmap ||
	    cmd->oem_11be_allow_bitmap) {
		IWL_DEBUG_RADIO(fwrt,
				"sending LARI_CONFIG_CHANGE, config_bitmap=0x%x, oem_11ax_allow_bitmap=0x%x\n",
				le32_to_cpu(cmd->config_bitmap),
				le32_to_cpu(cmd->oem_11ax_allow_bitmap));
		IWL_DEBUG_RADIO(fwrt,
				"sending LARI_CONFIG_CHANGE, oem_unii4_allow_bitmap=0x%x, chan_state_active_bitmap=0x%x, cmd_ver=%d\n",
				le32_to_cpu(cmd->oem_unii4_allow_bitmap),
				le32_to_cpu(cmd->chan_state_active_bitmap),
				cmd_ver);
		IWL_DEBUG_RADIO(fwrt,
				"sending LARI_CONFIG_CHANGE, oem_uhb_allow_bitmap=0x%x, force_disable_channels_bitmap=0x%x\n",
				le32_to_cpu(cmd->oem_uhb_allow_bitmap),
				le32_to_cpu(cmd->force_disable_channels_bitmap));
		IWL_DEBUG_RADIO(fwrt,
				"sending LARI_CONFIG_CHANGE, edt_bitmap=0x%x, oem_320mhz_allow_bitmap=0x%x\n",
				le32_to_cpu(cmd->edt_bitmap),
				le32_to_cpu(cmd->oem_320mhz_allow_bitmap));
		IWL_DEBUG_RADIO(fwrt,
				"sending LARI_CONFIG_CHANGE, oem_11be_allow_bitmap=0x%x\n",
				le32_to_cpu(cmd->oem_11be_allow_bitmap));
	} else {
		return 1;
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fill_lari_config);

int iwl_bios_get_dsm(struct iwl_fw_runtime *fwrt, enum iwl_dsm_funcs func,
		     u32 *value)
{
	GET_BIOS_TABLE(dsm, fwrt, func, value);
}
IWL_EXPORT_SYMBOL(iwl_bios_get_dsm);

bool iwl_puncturing_is_allowed_in_bios(u32 puncturing, u16 mcc)
{
	/* Some kind of regulatory mess means we need to currently disallow
	 * puncturing in the US and Canada unless enabled in BIOS.
	 */
	switch (mcc) {
	case IWL_MCC_US:
		return puncturing & IWL_UEFI_CNV_PUNCTURING_USA_EN_MSK;
	case IWL_MCC_CANADA:
		return puncturing & IWL_UEFI_CNV_PUNCTURING_CANADA_EN_MSK;
	default:
		return true;
	}
}
IWL_EXPORT_SYMBOL(iwl_puncturing_is_allowed_in_bios);

bool iwl_rfi_is_enabled_in_bios(struct iwl_fw_runtime *fwrt)
{
	/* default behaviour is disabled */
	u32 value = 0;
	int ret = iwl_bios_get_dsm(fwrt, DSM_FUNC_RFI_CONFIG, &value);

	if (ret < 0) {
		IWL_DEBUG_RADIO(fwrt, "Failed to get DSM RFI, ret=%d\n", ret);
		return false;
	}

	value &= DSM_VALUE_RFI_DISABLE;
	/* RFI BIOS CONFIG value can be 0 or 3 only.
	 * i.e 0 means DDR and DLVR enabled. 3 means DDR and DLVR disabled.
	 * 1 and 2 are invalid BIOS configurations, So, it's not possible to
	 * disable ddr/dlvr separately.
	 */
	if (!value) {
		IWL_DEBUG_RADIO(fwrt, "DSM RFI is evaluated to enable\n");
		return true;
	} else if (value == DSM_VALUE_RFI_DISABLE) {
		IWL_DEBUG_RADIO(fwrt, "DSM RFI is evaluated to disable\n");
	} else {
		IWL_DEBUG_RADIO(fwrt,
				"DSM RFI got invalid value, value=%d\n", value);
	}

	return false;
}
IWL_EXPORT_SYMBOL(iwl_rfi_is_enabled_in_bios);
