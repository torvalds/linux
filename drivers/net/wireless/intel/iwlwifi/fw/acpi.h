/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2021 Intel Corporation
 */
#ifndef __iwl_fw_acpi__
#define __iwl_fw_acpi__

#include <linux/acpi.h>
#include "fw/api/commands.h"
#include "fw/api/power.h"
#include "fw/api/phy.h"
#include "fw/api/nvm-reg.h"
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

#define ACPI_WIFI_DOMAIN	(0x07)

#define ACPI_SAR_PROFILE_NUM		4

#define ACPI_GEO_TABLE_SIZE		6
#define ACPI_NUM_GEO_PROFILES		3
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
#define ACPI_EWRD_WIFI_DATA_SIZE	((ACPI_SAR_PROFILE_NUM - 1) * \
					 ACPI_SAR_NUM_CHAINS_REV0 * \
					 ACPI_SAR_NUM_SUB_BANDS_REV0 + 3)
#define ACPI_WGDS_WIFI_DATA_SIZE	19
#define ACPI_WRDD_WIFI_DATA_SIZE	2
#define ACPI_SPLC_WIFI_DATA_SIZE	2
#define ACPI_ECKV_WIFI_DATA_SIZE	2

/*
 * 1 type, 1 enabled, 1 block list size, 16 block list array
 */
#define APCI_WTAS_BLACK_LIST_MAX	16
#define ACPI_WTAS_WIFI_DATA_SIZE	(3 + APCI_WTAS_BLACK_LIST_MAX)

#define ACPI_WGDS_TABLE_SIZE		3

#define ACPI_PPAG_WIFI_DATA_SIZE_V1	((IWL_NUM_CHAIN_LIMITS * \
					  IWL_NUM_SUB_BANDS_V1) + 2)
#define ACPI_PPAG_WIFI_DATA_SIZE_V2	((IWL_NUM_CHAIN_LIMITS * \
					  IWL_NUM_SUB_BANDS_V2) + 2)

/* PPAG gain value bounds in 1/8 dBm */
#define ACPI_PPAG_MIN_LB -16
#define ACPI_PPAG_MAX_LB 24
#define ACPI_PPAG_MIN_HB -16
#define ACPI_PPAG_MAX_HB 40

/*
 * The profile for revision 2 is a superset of revision 1, which is in
 * turn a superset of revision 0.  So we can store all revisions
 * inside revision 2, which is what we represent here.
 */
struct iwl_sar_profile_chain {
	u8 subbands[ACPI_SAR_NUM_SUB_BANDS_REV2];
};

struct iwl_sar_profile {
	bool enabled;
	struct iwl_sar_profile_chain chains[ACPI_SAR_NUM_CHAINS_REV2];
};

struct iwl_geo_profile {
	u8 values[ACPI_GEO_TABLE_SIZE];
};

enum iwl_dsm_funcs_rev_0 {
	DSM_FUNC_QUERY = 0,
	DSM_FUNC_DISABLE_SRD = 1,
	DSM_FUNC_ENABLE_INDONESIA_5G2 = 2,
	DSM_FUNC_11AX_ENABLEMENT = 6,
	DSM_FUNC_ENABLE_UNII4_CHAN = 7
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

#ifdef CONFIG_ACPI

struct iwl_fw_runtime;

extern const guid_t iwl_guid;
extern const guid_t iwl_rfi_guid;

void *iwl_acpi_get_object(struct device *dev, acpi_string method);

int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func,
			const guid_t *guid, u8 *value);

int iwl_acpi_get_dsm_u32(struct device *dev, int rev, int func,
			 const guid_t *guid, u32 *value);

union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
					 union acpi_object *data,
					 int data_size, int *tbl_rev);

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

int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 *per_chain, u32 n_tables, u32 n_subbands,
			   int prof_a, int prof_b);

int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt);

int iwl_sar_get_ewrd_table(struct iwl_fw_runtime *fwrt);

int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt);

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt);

int iwl_sar_geo_init(struct iwl_fw_runtime *fwrt,
		     struct iwl_per_chain_offset *table, u32 n_bands);

int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt, __le32 *block_list_array,
		     int *block_list_size);

__le32 iwl_acpi_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt);

#else /* CONFIG_ACPI */

static inline void *iwl_acpi_get_object(struct device *dev, acpi_string method)
{
	return ERR_PTR(-ENOENT);
}

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

static inline union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
						       union acpi_object *data,
						       int data_size,
						       int *tbl_rev)
{
	return ERR_PTR(-ENOENT);
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

static inline int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 *per_chain, u32 n_tables, u32 n_subbands,
			   int prof_a, int prof_b)
{
	return -ENOENT;
}

static inline int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_sar_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt)
{
	return false;
}

static inline int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
				   __le32 *block_list_array,
				   int *block_list_size)
{
	return -ENOENT;
}

static inline __le32 iwl_acpi_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt)
{
	return 0;
}

#endif /* CONFIG_ACPI */
#endif /* __iwl_fw_acpi__ */
