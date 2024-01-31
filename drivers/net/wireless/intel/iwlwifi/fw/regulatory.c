// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2023 Intel Corporation
 */
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "regulatory.h"
#include "fw/runtime.h"
#include "fw/uefi.h"

#define IWL_BIOS_TABLE_LOADER(__name)					\
int iwl_bios_get_ ## __name ## _table(struct iwl_fw_runtime *fwrt)	\
{									\
	int ret = -ENOENT;						\
	if (fwrt->uefi_tables_lock_status > UEFI_WIFI_GUID_UNLOCKED)	\
		ret = iwl_uefi_get_ ## __name ## _table(fwrt);		\
	if (ret < 0)							\
		ret = iwl_acpi_get_ ## __name ## _table(fwrt);		\
	return ret;							\
}									\
IWL_EXPORT_SYMBOL(iwl_bios_get_ ## __name ## _table)

IWL_BIOS_TABLE_LOADER(wrds);
IWL_BIOS_TABLE_LOADER(ewrd);
IWL_BIOS_TABLE_LOADER(wgds);

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
