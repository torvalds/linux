/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2023 Intel Corporation
 */
#ifndef __iwl_fw_acpi__
#define __iwl_fw_acpi__

#include <linux/acpi.h>
#include "fw/regulatory.h"
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
#define ACPI_WBEM_METHOD	"WBEM"

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
 * and one for enablement of Wi-Fi 320MHz per MCC
 */
#define ACPI_WBEM_WIFI_DATA_SIZE	2
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

#define ACPI_DSM_REV 0

#define IWL_ACPI_WBEM_REV0_MASK (BIT(0) | BIT(1))
#define IWL_ACPI_WBEM_REVISION 0

#ifdef CONFIG_ACPI

struct iwl_fw_runtime;

extern const guid_t iwl_guid;

/**
 * iwl_acpi_get_mcc - read MCC from ACPI, if available
 *
 * @fwrt: the fw runtime struct
 * @mcc: output buffer (3 bytes) that will get the MCC
 *
 * This function tries to read the current MCC from ACPI if available.
 */
int iwl_acpi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc);

int iwl_acpi_get_pwr_limit(struct iwl_fw_runtime *fwrt, u64 *dflt_pwr_limit);

/*
 * iwl_acpi_get_eckv - read external clock validation from ACPI, if available
 *
 * @fwrt: the fw runtime struct
 * @extl_clk: output var (2 bytes) that will get the clk indication.
 *
 * This function tries to read the external clock indication
 * from ACPI if available.
 */
int iwl_acpi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk);

int iwl_acpi_get_wrds_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_ewrd_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_wgds_table(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_tas_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_tas_data *data);

int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt);

void iwl_acpi_get_phy_filters(struct iwl_fw_runtime *fwrt,
			      struct iwl_phy_specific_cfg *filters);

void iwl_acpi_get_guid_lock_status(struct iwl_fw_runtime *fwrt);

int iwl_acpi_get_dsm(struct iwl_fw_runtime *fwrt,
		     enum iwl_dsm_funcs func, u32 *value);

int iwl_acpi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value);
#else /* CONFIG_ACPI */

static inline void *iwl_acpi_get_dsm_object(struct device *dev, int rev,
					    int func, union acpi_object *args)
{
	return ERR_PTR(-ENOENT);
}

static inline int iwl_acpi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_pwr_limit(struct iwl_fw_runtime *fwrt,
					 u64 *dflt_pwr_limit)
{
	*dflt_pwr_limit = 0;
	return 0;
}

static inline int iwl_acpi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk)
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

static inline int iwl_acpi_get_tas_table(struct iwl_fw_runtime *fwrt,
					 struct iwl_tas_data *data)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

/* macro since the second argument doesn't always exist */
#define iwl_acpi_get_phy_filters(fwrt, filters) do { } while (0)

static inline void iwl_acpi_get_guid_lock_status(struct iwl_fw_runtime *fwrt)
{
}

static inline int iwl_acpi_get_dsm(struct iwl_fw_runtime *fwrt,
				   enum iwl_dsm_funcs func, u32 *value)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value)
{
	return -ENOENT;
}
#endif /* CONFIG_ACPI */

#endif /* __iwl_fw_acpi__ */
