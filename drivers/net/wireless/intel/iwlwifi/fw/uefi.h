/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2021-2023 Intel Corporation
 */
#ifndef __iwl_fw_uefi__
#define __iwl_fw_uefi__

#define IWL_UEFI_OEM_PNVM_NAME		L"UefiCnvWlanOemSignedPnvm"
#define IWL_UEFI_REDUCED_POWER_NAME	L"UefiCnvWlanReducedPower"
#define IWL_UEFI_SGOM_NAME		L"UefiCnvWlanSarGeoOffsetMapping"
#define IWL_UEFI_STEP_NAME		L"UefiCnvCommonSTEP"
#define IWL_UEFI_UATS_NAME		L"CnvUefiWlanUATS"

#define IWL_SGOM_MAP_SIZE		339
#define IWL_UATS_MAP_SIZE		339

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
#endif /* CONFIG_EFI */

#if defined(CONFIG_EFI) && defined(CONFIG_ACPI)
void iwl_uefi_get_sgom_table(struct iwl_trans *trans, struct iwl_fw_runtime *fwrt);
int iwl_uefi_get_uats_table(struct iwl_trans *trans,
			    struct iwl_fw_runtime *fwrt);
#else
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

#endif
#endif /* __iwl_fw_uefi__ */
