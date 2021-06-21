/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2021 Intel Corporation
 */


#define IWL_UEFI_OEM_PNVM_NAME	L"UefiCnvWlanOemSignedPnvm"

/*
 * TODO: we have these hardcoded values that the caller must pass,
 * because reading from the UEFI is not working.  To implement this
 * properly, we have to change iwl_pnvm_get_from_uefi() to call
 * efivar_entry_size() and return the value to the caller instead.
 */
#define IWL_HARDCODED_PNVM_SIZE 4096

#ifdef CONFIG_EFI
void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len);
#else /* CONFIG_EFI */
static inline
void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif /* CONFIG_EFI */
