/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2021 Intel Corporation
 */
#ifndef __iwl_fw_uefi__
#define __iwl_fw_uefi__

#define IWL_UEFI_OEM_PNVM_NAME		L"UefiCnvWlanOemSignedPnvm"
#define IWL_UEFI_REDUCED_POWER_NAME	L"UefiCnvWlanReducedPower"
#define IWL_UEFI_SGOM_NAME		L"UefiCnvWlanSarGeoOffsetMapping"

/*
 * TODO: we have these hardcoded values that the caller must pass,
 * because reading from the UEFI is not working.  To implement this
 * properly, we have to change iwl_pnvm_get_from_uefi() to call
 * efivar_entry_size() and return the value to the caller instead.
 */
#define IWL_HARDCODED_PNVM_SIZE		4096
#define IWL_HARDCODED_REDUCE_POWER_SIZE	32768
#define IWL_HARDCODED_SGOM_SIZE		339

struct pnvm_sku_package {
	u8 rev;
	u32 total_size;
	u8 n_skus;
	u32 reserved[2];
	u8 data[];
} __packed;

struct uefi_cnv_wlan_sgom_data {
	u8 revision;
	u8 offset_map[IWL_HARDCODED_SGOM_SIZE - 1];
} __packed;

/*
 * This is known to be broken on v4.19 and to work on v5.4.  Until we
 * figure out why this is the case and how to make it work, simply
 * disable the feature in old kernels.
 */
#ifdef CONFIG_EFI
void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len);
void *iwl_uefi_get_reduced_power(struct iwl_trans *trans, size_t *len);
#else /* CONFIG_EFI */
static inline
void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline
void *iwl_uefi_get_reduced_power(struct iwl_trans *trans, size_t *len)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif /* CONFIG_EFI */

#if defined(CONFIG_EFI) && defined(CONFIG_ACPI)
void iwl_uefi_get_sgom_table(struct iwl_trans *trans, struct iwl_fw_runtime *fwrt);
#else
static inline
void iwl_uefi_get_sgom_table(struct iwl_trans *trans, struct iwl_fw_runtime *fwrt)
{
}
#endif
#endif /* __iwl_fw_uefi__ */
