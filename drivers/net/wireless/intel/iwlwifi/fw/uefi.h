/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2021-2024 Intel Corporation
 */
#ifndef __iwl_fw_uefi__
#define __iwl_fw_uefi__

#include "fw/regulatory.h"

#define IWL_UEFI_OEM_PNVM_NAME		L"UefiCnvWlanOemSignedPnvm"
#define IWL_UEFI_REDUCED_POWER_NAME	L"UefiCnvWlanReducedPower"
#define IWL_UEFI_SGOM_NAME		L"UefiCnvWlanSarGeoOffsetMapping"
#define IWL_UEFI_STEP_NAME		L"UefiCnvCommonSTEP"
#define IWL_UEFI_UATS_NAME		L"CnvUefiWlanUATS"
#define IWL_UEFI_WRDS_NAME		L"UefiCnvWlanWRDS"
#define IWL_UEFI_EWRD_NAME		L"UefiCnvWlanEWRD"
#define IWL_UEFI_WGDS_NAME		L"UefiCnvWlanWGDS"
#define IWL_UEFI_PPAG_NAME		L"UefiCnvWlanPPAG"
#define IWL_UEFI_WTAS_NAME		L"UefiCnvWlanWTAS"
#define IWL_UEFI_SPLC_NAME		L"UefiCnvWlanSPLC"
#define IWL_UEFI_WRDD_NAME		L"UefiCnvWlanWRDD"
#define IWL_UEFI_ECKV_NAME		L"UefiCnvWlanECKV"
#define IWL_UEFI_DSM_NAME		L"UefiCnvWlanGeneralCfg"
#define IWL_UEFI_WBEM_NAME		L"UefiCnvWlanWBEM"
#define IWL_UEFI_PUNCTURING_NAME	L"UefiCnvWlanPuncturing"
#define IWL_UEFI_DSBR_NAME		L"UefiCnvCommonDSBR"


#define IWL_SGOM_MAP_SIZE		339
#define IWL_UATS_MAP_SIZE		339

#define IWL_UEFI_WRDS_REVISION		2
#define IWL_UEFI_EWRD_REVISION		2
#define IWL_UEFI_WGDS_REVISION		3
#define IWL_UEFI_MIN_PPAG_REV		1
#define IWL_UEFI_MAX_PPAG_REV		3
#define IWL_UEFI_MIN_WTAS_REVISION	1
#define IWL_UEFI_MAX_WTAS_REVISION	2
#define IWL_UEFI_SPLC_REVISION		0
#define IWL_UEFI_WRDD_REVISION		0
#define IWL_UEFI_ECKV_REVISION		0
#define IWL_UEFI_WBEM_REVISION		0
#define IWL_UEFI_DSM_REVISION		4
#define IWL_UEFI_PUNCTURING_REVISION	0
#define IWL_UEFI_DSBR_REVISION		1

struct pnvm_sku_package {
	u8 rev;
	u32 total_size;
	u8 n_skus;
	u32 reserved[2];
	u8 data[];
} __packed;

struct uefi_cnv_wlan_sgom_data {
	u8 revision;
	u8 offset_map[IWL_SGOM_MAP_SIZE - 1];
} __packed;

struct uefi_cnv_wlan_uats_data {
	u8 revision;
	u8 offset_map[IWL_UATS_MAP_SIZE - 1];
} __packed;

struct uefi_cnv_common_step_data {
	u8 revision;
	u8 step_mode;
	u8 cnvi_eq_channel;
	u8 cnvr_eq_channel;
	u8 radio1;
	u8 radio2;
} __packed;

/*
 * struct uefi_sar_profile - a SAR profile as defined in UEFI
 *
 * @chains: a per-chain table of SAR values
 */
struct uefi_sar_profile {
	struct iwl_sar_profile_chain chains[BIOS_SAR_MAX_CHAINS_PER_PROFILE];
} __packed;

/*
 * struct uefi_cnv_var_wrds - WRDS table as defined in UEFI
 *
 * @revision: the revision of the table
 * @mode: is WRDS enbaled/disabled
 * @sar_profile: sar profile #1
 */
struct uefi_cnv_var_wrds {
	u8 revision;
	u32 mode;
	struct uefi_sar_profile sar_profile;
} __packed;

/*
 * struct uefi_cnv_var_ewrd - EWRD table as defined in UEFI
 * @revision: the revision of the table
 * @mode: is WRDS enbaled/disabled
 * @num_profiles: how many additional profiles we have in this table (0-3)
 * @sar_profiles: the additional SAR profiles (#2-#4)
 */
struct uefi_cnv_var_ewrd {
	u8 revision;
	u32 mode;
	u32 num_profiles;
	struct uefi_sar_profile sar_profiles[BIOS_SAR_MAX_PROFILE_NUM - 1];
} __packed;

/*
 * struct uefi_cnv_var_wgds - WGDS table as defined in UEFI
 * @revision: the revision of the table
 * @num_profiles: the number of geo profiles we have in the table.
 *	The first 3 are mandatory, and can have up to 8.
 * @geo_profiles: a per-profile table of the offsets to add to SAR values.
 */
struct uefi_cnv_var_wgds {
	u8 revision;
	u8 num_profiles;
	struct iwl_geo_profile geo_profiles[BIOS_GEO_MAX_PROFILE_NUM];
} __packed;

/*
 * struct uefi_cnv_var_ppag - PPAG table as defined in UEFI
 * @revision: the revision of the table
 * @ppag_modes: values from &enum iwl_ppag_flags
 * @ppag_chains: the PPAG values per chain and band
 */
struct uefi_cnv_var_ppag {
	u8 revision;
	u32 ppag_modes;
	struct iwl_ppag_chain ppag_chains[IWL_NUM_CHAIN_LIMITS];
} __packed;

/* struct uefi_cnv_var_wtas - WTAS tabled as defined in UEFI
 * @revision: the revision of the table
 * @tas_selection: different options of TAS enablement.
 * @black_list_size: the number of defined entried in the black list
 * @black_list: a list of countries that are not allowed to use the TAS feature
 */
struct uefi_cnv_var_wtas {
	u8 revision;
	u32 tas_selection;
	u8 black_list_size;
	u16 black_list[IWL_WTAS_BLACK_LIST_MAX];
} __packed;

/* struct uefi_cnv_var_splc - SPLC tabled as defined in UEFI
 * @revision: the revision of the table
 * @default_pwr_limit: The default maximum power per device
 */
struct uefi_cnv_var_splc {
	u8 revision;
	u32 default_pwr_limit;
} __packed;

/* struct uefi_cnv_var_wrdd - WRDD table as defined in UEFI
 * @revision: the revision of the table
 * @mcc: country identifier as defined in ISO/IEC 3166-1 Alpha 2 code
 */
struct uefi_cnv_var_wrdd {
	u8 revision;
	u32 mcc;
} __packed;

/* struct uefi_cnv_var_eckv - ECKV table as defined in UEFI
 * @revision: the revision of the table
 * @ext_clock_valid: indicates if external 32KHz clock is valid
 */
struct uefi_cnv_var_eckv {
	u8 revision;
	u32 ext_clock_valid;
} __packed;

#define UEFI_MAX_DSM_FUNCS 32

/* struct uefi_cnv_var_general_cfg - DSM-like table as defined in UEFI
 * @revision: the revision of the table
 * @functions: payload of the different DSM functions
 */
struct uefi_cnv_var_general_cfg {
	u8 revision;
	u32 functions[UEFI_MAX_DSM_FUNCS];
} __packed;

#define IWL_UEFI_WBEM_REV0_MASK (BIT(0) | BIT(1))
/* struct uefi_cnv_wlan_wbem_data - Bandwidth enablement per MCC as defined
 *	in UEFI
 * @revision: the revision of the table
 * @wbem_320mhz_per_mcc: enablement of 320MHz bandwidth per MCC
 *	bit 0 - if set, 320MHz is enabled for Japan
 *	bit 1 - if set, 320MHz is enabled for South Korea
 *	bit 2- 31, Reserved
 */
struct uefi_cnv_wlan_wbem_data {
	u8 revision;
	u32 wbem_320mhz_per_mcc;
} __packed;

enum iwl_uefi_cnv_puncturing_flags {
	IWL_UEFI_CNV_PUNCTURING_USA_EN_MSK	= BIT(0),
	IWL_UEFI_CNV_PUNCTURING_CANADA_EN_MSK	= BIT(1),
};

#define IWL_UEFI_PUNCTURING_REV0_MASK (IWL_UEFI_CNV_PUNCTURING_USA_EN_MSK | \
				       IWL_UEFI_CNV_PUNCTURING_CANADA_EN_MSK)
/**
 * struct uefi_cnv_var_puncturing_data - controlling channel
 *	puncturing for few countries.
 * @revision: the revision of the table
 * @puncturing: enablement of channel puncturing per mcc
 *	see &enum iwl_uefi_cnv_puncturing_flags.
 */
struct uefi_cnv_var_puncturing_data {
	u8 revision;
	u32 puncturing;
} __packed;

/**
 * struct uefi_cnv_wlan_dsbr_data - BIOS STEP configuration information
 * @revision: the revision of the table
 * @config: STEP configuration flags:
 *	bit 8, switch to URM depending on FW setting
 *	bit 9, switch to URM
 *
 * Platform information for STEP configuration/workarounds.
 */
struct uefi_cnv_wlan_dsbr_data {
	u8 revision;
	u32 config;
} __packed;

/*
 * This is known to be broken on v4.19 and to work on v5.4.  Until we
 * figure out why this is the case and how to make it work, simply
 * disable the feature in old kernels.
 */
#ifdef CONFIG_EFI
void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len);
u8 *iwl_uefi_get_reduced_power(struct iwl_trans *trans, size_t *len);
int iwl_uefi_reduce_power_parse(struct iwl_trans *trans,
				const u8 *data, size_t len,
				struct iwl_pnvm_image *pnvm_data);
void iwl_uefi_get_step_table(struct iwl_trans *trans);
int iwl_uefi_handle_tlv_mem_desc(struct iwl_trans *trans, const u8 *data,
				 u32 tlv_len, struct iwl_pnvm_image *pnvm_data);
int iwl_uefi_get_wrds_table(struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_ewrd_table(struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_wgds_table(struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_ppag_table(struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_tas_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_tas_data *data);
int iwl_uefi_get_pwr_limit(struct iwl_fw_runtime *fwrt,
			   u64 *dflt_pwr_limit);
int iwl_uefi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc);
int iwl_uefi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk);
int iwl_uefi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value);
int iwl_uefi_get_dsm(struct iwl_fw_runtime *fwrt, enum iwl_dsm_funcs func,
		     u32 *value);
void iwl_uefi_get_sgom_table(struct iwl_trans *trans, struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_uats_table(struct iwl_trans *trans,
			    struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_puncturing(struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_dsbr(struct iwl_fw_runtime *fwrt, u32 *value);
#else /* CONFIG_EFI */
static inline void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int
iwl_uefi_reduce_power_parse(struct iwl_trans *trans,
			    const u8 *data, size_t len,
			    struct iwl_pnvm_image *pnvm_data)
{
	return -EOPNOTSUPP;
}

static inline u8 *
iwl_uefi_get_reduced_power(struct iwl_trans *trans, size_t *len)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void iwl_uefi_get_step_table(struct iwl_trans *trans)
{
}

static inline int
iwl_uefi_handle_tlv_mem_desc(struct iwl_trans *trans, const u8 *data,
			     u32 tlv_len, struct iwl_pnvm_image *pnvm_data)
{
	return 0;
}

static inline int iwl_uefi_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_tas_table(struct iwl_fw_runtime *fwrt,
					 struct iwl_tas_data *data)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_pwr_limit(struct iwl_fw_runtime *fwrt,
					 u64 *dflt_pwr_limit)
{
	*dflt_pwr_limit = 0;
	return 0;
}

static inline int iwl_uefi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value)
{
	return -ENOENT;
}

static inline int iwl_uefi_get_dsm(struct iwl_fw_runtime *fwrt,
				   enum iwl_dsm_funcs func, u32 *value)
{
	return -ENOENT;
}

static inline
void iwl_uefi_get_sgom_table(struct iwl_trans *trans, struct iwl_fw_runtime *fwrt)
{
}

static inline
int iwl_uefi_get_uats_table(struct iwl_trans *trans,
			    struct iwl_fw_runtime *fwrt)
{
	return 0;
}

static inline
int iwl_uefi_get_puncturing(struct iwl_fw_runtime *fwrt)
{
	return 0;
}

static inline
int iwl_uefi_get_dsbr(struct iwl_fw_runtime *fwrt, u32 *value)
{
	return -ENOENT;
}
#endif /* CONFIG_EFI */
#endif /* __iwl_fw_uefi__ */
