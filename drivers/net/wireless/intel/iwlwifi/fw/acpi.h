/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2023 Intel Corporation
 */
#ifndef __iwl_fw_acpi__
#define __iwl_fw_acpi__

#include <linux/acpi.h>
#include "fw/api/commands.h"
#include "fw/api/power.h"
#include "fw/api/phy.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/config.h"
#include "fw/img.h"
#include "iwl-trans.h"


#define ACPI_WRDS_METHOD	"WRDS"
#define ACPI_EWRD_METHOD	"EWRD"
#define ACPI_WGDS_METHOD	"WGDS"
#define ACPI_WRDD_METHOD	"WRDD"
#define ACPI_SPLC_METHOD	"SPLC"
#define ACPI_ECKV_METHOD	"ECKV"
#define ACPI_PPAG_METHOD	"PPAG"
#define ACPI_WTAS_METHOD	"WTAS"
#define ACPI_WPFC_METHOD	"WPFC"
#define ACPI_GLAI_METHOD	"GLAI"

#define ACPI_WIFI_DOMAIN	(0x07)

#define ACPI_SAR_PROFILE_NUM		4

#define ACPI_NUM_GEO_PROFILES		3
#define ACPI_NUM_GEO_PROFILES_REV3	8
#define ACPI_GEO_PER_CHAIN_SIZE		3

#define ACPI_SAR_NUM_CHAINS_REV0	2
#define ACPI_SAR_NUM_CHAINS_REV1	2
#define ACPI_SAR_NUM_CHAINS_REV2	4
#define ACPI_SAR_NUM_SUB_BANDS_REV0	5
#define ACPI_SAR_NUM_SUB_BANDS_REV1	11
#define ACPI_SAR_NUM_SUB_BANDS_REV2	11

#define ACPI_WRDS_WIFI_DATA_SIZE_REV0	(ACPI_SAR_NUM_CHAINS_REV0 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV0 + 2)
#define ACPI_WRDS_WIFI_DATA_SIZE_REV1	(ACPI_SAR_NUM_CHAINS_REV1 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV1 + 2)
#define ACPI_WRDS_WIFI_DATA_SIZE_REV2	(ACPI_SAR_NUM_CHAINS_REV2 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV2 + 2)
#define ACPI_EWRD_WIFI_DATA_SIZE_REV0	((ACPI_SAR_PROFILE_NUM - 1) * \
					 ACPI_SAR_NUM_CHAINS_REV0 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV0 + 3)
#define ACPI_EWRD_WIFI_DATA_SIZE_REV1	((ACPI_SAR_PROFILE_NUM - 1) * \
					 ACPI_SAR_NUM_CHAINS_REV1 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV1 + 3)
#define ACPI_EWRD_WIFI_DATA_SIZE_REV2	((ACPI_SAR_PROFILE_NUM - 1) * \
					 ACPI_SAR_NUM_CHAINS_REV2 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV2 + 3)
#define ACPI_WPFC_WIFI_DATA_SIZE	5 /* domain and 4 filter config words */

/* revision 0 and 1 are identical, except for the semantics in the FW */
#define ACPI_GEO_NUM_BANDS_REV0		2
#define ACPI_GEO_NUM_BANDS_REV2		3

#define ACPI_WRDD_WIFI_DATA_SIZE	2
#define ACPI_SPLC_WIFI_DATA_SIZE	2
#define ACPI_ECKV_WIFI_DATA_SIZE	2
/*
 * One element for domain type,
 * and one for the status
 */
#define ACPI_GLAI_WIFI_DATA_SIZE	2
#define ACPI_GLAI_MAX_STATUS		2
/*
 * TAS size: 1 elelment for type,
 *	     1 element for enabled field,
 *	     1 element for block list size,
 *	     16 elements for block list array
 */
#define ACPI_WTAS_WIFI_DATA_SIZE	(3 + IWL_WTAS_BLACK_LIST_MAX)

#define ACPI_PPAG_WIFI_DATA_SIZE_V1	((IWL_NUM_CHAIN_LIMITS * \
					  IWL_NUM_SUB_BANDS_V1) + 2)
#define ACPI_PPAG_WIFI_DATA_SIZE_V2	((IWL_NUM_CHAIN_LIMITS * \
					  IWL_NUM_SUB_BANDS_V2) + 2)

#define IWL_SAR_ENABLE_MSK		BIT(0)
#define IWL_REDUCE_POWER_FLAGS_POS	1

/* The Inidcator whether UEFI WIFI GUID tables are locked is read from ACPI */
#define UEFI_WIFI_GUID_UNLOCKED		0

enum iwl_dsm_funcs_rev_0 {
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

/* DSM RFI uses a different GUID, so need separate definitions */

#define DSM_RFI_FUNC_ENABLE 3

enum iwl_dsm_values_rfi {
	DSM_VALUE_RFI_ENABLE,
	DSM_VALUE_RFI_DISABLE,
	DSM_VALUE_RFI_MAX
};

enum iwl_dsm_masks_reg {
	DSM_MASK_CHINA_22_REG = BIT(2)
};

#ifdef CONFIG_ACPI

struct iwl_fw_runtime;

extern const guid_t iwl_guid;
extern const guid_t iwl_rfi_guid;

int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func,
			const guid_t *guid, u8 *value);

int iwl_acpi_get_dsm_u32(struct device *dev, int rev, int func,
			 const guid_t *guid, u32 *value);

/**
 * iwl_acpi_get_mcc - read MCC from ACPI, if available
 *
 * @dev: the struct device
 * @mcc: output buffer (3 bytes) that will get the MCC
 *
 * This function tries to read the current MCC from ACPI if available.
 */
int iwl_acpi_get_mcc(struct device *dev, char *mcc);

u64 iwl_acpi_get_pwr_limit(struct device *dev);

/*
 * iwl_acpi_get_eckv - read external clock validation from ACPI, if available
 *
 * @dev: the struct device
 * @extl_clk: output var (2 bytes) that will get the clk indication.
 *
 * This function tries to read the external clock indication
 * from ACPI if available.
 */
int iwl_acpi_get_eckv(struct device *dev, u32 *extl_clk);

int iwl_acpi_get_wrds_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_ewrd_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_wgds_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
		     union iwl_tas_config_cmd *cmd, int fw_ver);

__le32 iwl_acpi_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt);

void iwl_acpi_get_phy_filters(struct iwl_fw_runtime *fwrt,
			      struct iwl_phy_specific_cfg *filters);

void iwl_acpi_get_guid_lock_status(struct iwl_fw_runtime *fwrt);

#else /* CONFIG_ACPI */

static inline void *iwl_acpi_get_dsm_object(struct device *dev, int rev,
					    int func, union acpi_object *args)
{
	return ERR_PTR(-ENOENT);
}

static inline int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func,
				      const guid_t *guid, u8 *value)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_dsm_u32(struct device *dev, int rev, int func,
				       const guid_t *guid, u32 *value)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_mcc(struct device *dev, char *mcc)
{
	return -ENOENT;
}

static inline u64 iwl_acpi_get_pwr_limit(struct device *dev)
{
	return 0;
}

static inline int iwl_acpi_get_eckv(struct device *dev, u32 *extl_clk)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	return 1;
}

static inline int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
				   union iwl_tas_config_cmd *cmd, int fw_ver)
{
	return -ENOENT;
}

static inline __le32 iwl_acpi_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt)
{
	return 0;
}

static inline int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline void iwl_acpi_get_phy_filters(struct iwl_fw_runtime *fwrt,
					    struct iwl_phy_specific_cfg *filters)
{
}

static inline void iwl_acpi_get_guid_lock_status(struct iwl_fw_runtime *fwrt)
{
}
#endif /* CONFIG_ACPI */

#endif /* __iwl_fw_acpi__ */
