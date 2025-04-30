/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2023-2025 Intel Corporation
 */

#ifndef __fw_regulatory_h__
#define __fw_regulatory_h__

#include "fw/img.h"
#include "fw/api/commands.h"
#include "fw/api/power.h"
#include "fw/api/phy.h"
#include "fw/api/config.h"
#include "fw/api/nvm-reg.h"
#include "fw/img.h"
#include "iwl-trans.h"

#define BIOS_SAR_MAX_PROFILE_NUM	4
/*
 * Each SAR profile has (up to, depends on the table revision) 4 chains:
 * chain A, chain B, chain A when in CDB, chain B when in CDB
 */
#define BIOS_SAR_MAX_CHAINS_PER_PROFILE 4
#define BIOS_SAR_NUM_CHAINS             2
#define BIOS_SAR_MAX_SUB_BANDS_NUM      11

#define BIOS_GEO_NUM_CHAINS		2
#define BIOS_GEO_MAX_NUM_BANDS		3
#define BIOS_GEO_MAX_PROFILE_NUM	8
#define BIOS_GEO_MIN_PROFILE_NUM	3

#define IWL_SAR_ENABLE_MSK		BIT(0)

/* PPAG gain value bounds in 1/8 dBm */
#define IWL_PPAG_MIN_LB	-16
#define IWL_PPAG_MAX_LB 24
#define IWL_PPAG_MIN_HB -16
#define IWL_PPAG_MAX_HB 40

#define IWL_PPAG_ETSI_CHINA_MASK	3
#define IWL_PPAG_REV3_MASK		0x7FF

#define IWL_WTAS_ENABLED_MSK		BIT(0)
#define IWL_WTAS_OVERRIDE_IEC_MSK	BIT(1)
#define IWL_WTAS_ENABLE_IEC_MSK		BIT(2)
#define IWL_WTAS_CANADA_UHB_MSK		BIT(15)
#define IWL_WTAS_USA_UHB_MSK		BIT(16)

struct iwl_tas_selection_data {
	u8 override_tas_iec:1,
	   enable_tas_iec:1,
	   usa_tas_uhb_allowed:1,
	   canada_tas_uhb_allowed:1;
};

#define BIOS_MCC_CHINA 0x434e

/*
 * The profile for revision 2 is a superset of revision 1, which is in
 * turn a superset of revision 0.  So we can store all revisions
 * inside revision 2, which is what we represent here.
 */

/*
 * struct iwl_sar_profile_chain - per-chain values of a SAR profile
 * @subbands: the SAR value for each subband
 */
struct iwl_sar_profile_chain {
	u8 subbands[BIOS_SAR_MAX_SUB_BANDS_NUM];
};

/*
 * struct iwl_sar_profile - SAR profile from SAR tables
 * @enabled: whether the profile is enabled or not
 * @chains: per-chain SAR values
 */
struct iwl_sar_profile {
	bool enabled;
	struct iwl_sar_profile_chain chains[BIOS_SAR_MAX_CHAINS_PER_PROFILE];
};

/* Same thing as with SAR, all revisions fit in revision 2 */

/*
 * struct iwl_geo_profile_band - per-band geo SAR offsets
 * @max: the max tx power allowed for the band
 * @chains: SAR offsets values for each chain
 */
struct iwl_geo_profile_band {
	u8 max;
	u8 chains[BIOS_GEO_NUM_CHAINS];
};

/*
 * struct iwl_geo_profile - geo profile
 * @bands: per-band table of the SAR offsets
 */
struct iwl_geo_profile {
	struct iwl_geo_profile_band bands[BIOS_GEO_MAX_NUM_BANDS];
};

/* Same thing as with SAR, all revisions fit in revision 2 */
struct iwl_ppag_chain {
	s8 subbands[BIOS_SAR_MAX_SUB_BANDS_NUM];
};

struct iwl_tas_data {
	u8 block_list_size;
	u16 block_list_array[IWL_WTAS_BLACK_LIST_MAX];
	u8 table_source;
	u8 table_revision;
	u32 tas_selection;
};

/* For DSM revision 0 and 4 */
enum iwl_dsm_funcs {
	DSM_FUNC_QUERY = 0,
	DSM_FUNC_DISABLE_SRD = 1,
	DSM_FUNC_ENABLE_INDONESIA_5G2 = 2,
	DSM_FUNC_ENABLE_6E = 3,
	DSM_FUNC_REGULATORY_CONFIG = 4,
	DSM_FUNC_11AX_ENABLEMENT = 6,
	DSM_FUNC_ENABLE_UNII4_CHAN = 7,
	DSM_FUNC_ACTIVATE_CHANNEL = 8,
	DSM_FUNC_FORCE_DISABLE_CHANNELS = 9,
	DSM_FUNC_ENERGY_DETECTION_THRESHOLD = 10,
	DSM_FUNC_RFI_CONFIG = 11,
	DSM_FUNC_ENABLE_11BE = 12,
	DSM_FUNC_NUM_FUNCS = 13,
};

enum iwl_dsm_values_srd {
	DSM_VALUE_SRD_ACTIVE,
	DSM_VALUE_SRD_PASSIVE,
	DSM_VALUE_SRD_DISABLE,
	DSM_VALUE_SRD_MAX
};

enum iwl_dsm_values_indonesia {
	DSM_VALUE_INDONESIA_DISABLE,
	DSM_VALUE_INDONESIA_ENABLE,
	DSM_VALUE_INDONESIA_RESERVED,
	DSM_VALUE_INDONESIA_MAX
};

enum iwl_dsm_unii4_bitmap {
	DSM_VALUE_UNII4_US_OVERRIDE_MSK		= BIT(0),
	DSM_VALUE_UNII4_US_EN_MSK		= BIT(1),
	DSM_VALUE_UNII4_ETSI_OVERRIDE_MSK	= BIT(2),
	DSM_VALUE_UNII4_ETSI_EN_MSK		= BIT(3),
	DSM_VALUE_UNII4_CANADA_OVERRIDE_MSK	= BIT(4),
	DSM_VALUE_UNII4_CANADA_EN_MSK		= BIT(5),
};

#define DSM_UNII4_ALLOW_BITMAP (DSM_VALUE_UNII4_US_OVERRIDE_MSK		|\
				DSM_VALUE_UNII4_US_EN_MSK		|\
				DSM_VALUE_UNII4_ETSI_OVERRIDE_MSK	|\
				DSM_VALUE_UNII4_ETSI_EN_MSK		|\
				DSM_VALUE_UNII4_CANADA_OVERRIDE_MSK	|\
				DSM_VALUE_UNII4_CANADA_EN_MSK)

enum iwl_dsm_values_rfi {
	DSM_VALUE_RFI_DLVR_DISABLE	= BIT(0),
	DSM_VALUE_RFI_DDR_DISABLE	= BIT(1),
};

#define DSM_VALUE_RFI_DISABLE	(DSM_VALUE_RFI_DLVR_DISABLE |\
				 DSM_VALUE_RFI_DDR_DISABLE)

bool iwl_rfi_is_enabled_in_bios(struct iwl_fw_runtime *fwrt);

enum iwl_dsm_masks_reg {
	DSM_MASK_CHINA_22_REG = BIT(2)
};

struct iwl_fw_runtime;

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt);

int iwl_sar_geo_fill_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_per_chain_offset *table,
			   u32 n_bands, u32 n_profiles);

int iwl_sar_fill_profile(struct iwl_fw_runtime *fwrt,
			 __le16 *per_chain, u32 n_tables, u32 n_subbands,
			 int prof_a, int prof_b);

int iwl_fill_ppag_table(struct iwl_fw_runtime *fwrt,
			union iwl_ppag_table_cmd *cmd,
			int *cmd_size);

bool iwl_is_ppag_approved(struct iwl_fw_runtime *fwrt);

bool iwl_is_tas_approved(void);
bool iwl_add_mcc_to_tas_block_list(u16 *list, u8 *size, u16 mcc);

struct iwl_tas_selection_data
iwl_parse_tas_selection(const u32 tas_selection, const u8 tbl_rev);

int iwl_bios_get_wrds_table(struct iwl_fw_runtime *fwrt);

int iwl_bios_get_ewrd_table(struct iwl_fw_runtime *fwrt);

int iwl_bios_get_wgds_table(struct iwl_fw_runtime *fwrt);

int iwl_bios_get_ppag_table(struct iwl_fw_runtime *fwrt);

int iwl_bios_get_tas_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_tas_data *data);

int iwl_bios_get_pwr_limit(struct iwl_fw_runtime *fwrt,
			   u64 *dflt_pwr_limit);

int iwl_bios_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc);
int iwl_bios_get_eckv(struct iwl_fw_runtime *fwrt, u32 *ext_clk);
int iwl_bios_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value);

__le32 iwl_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt);
int iwl_fill_lari_config(struct iwl_fw_runtime *fwrt,
			 struct iwl_lari_config_change_cmd *cmd,
			 size_t *cmd_size);

int iwl_bios_get_dsm(struct iwl_fw_runtime *fwrt, enum iwl_dsm_funcs func,
		     u32 *value);

static inline u32 iwl_bios_get_ppag_flags(const u32 ppag_modes,
					  const u8 ppag_bios_rev)
{
	return ppag_modes & (ppag_bios_rev < 3 ? IWL_PPAG_ETSI_CHINA_MASK :
						 IWL_PPAG_REV3_MASK);
}

bool iwl_puncturing_is_allowed_in_bios(u32 puncturing, u16 mcc);

#define IWL_DSBR_FW_MODIFIED_URM_MASK	BIT(8)
#define IWL_DSBR_PERMANENT_URM_MASK	BIT(9)

int iwl_bios_get_dsbr(struct iwl_fw_runtime *fwrt, u32 *value);
int iwl_bios_get_phy_filters(struct iwl_fw_runtime *fwrt);

static inline void iwl_bios_setup_step(struct iwl_trans *trans,
				       struct iwl_fw_runtime *fwrt)
{
	u32 dsbr;

	if (!trans->trans_cfg->integrated)
		return;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_BZ)
		return;

	if (iwl_bios_get_dsbr(fwrt, &dsbr))
		dsbr = 0;

	trans->dsbr_urm_fw_dependent = !!(dsbr & IWL_DSBR_FW_MODIFIED_URM_MASK);
	trans->dsbr_urm_permanent = !!(dsbr & IWL_DSBR_PERMANENT_URM_MASK);
}
#endif /* __fw_regulatory_h__ */
