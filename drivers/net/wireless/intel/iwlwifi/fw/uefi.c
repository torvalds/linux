// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2021-2025 Intel Corporation
 */

#include "iwl-drv.h"
#include "pnvm.h"
#include "iwl-prph.h"
#include "iwl-io.h"

#include "fw/uefi.h"
#include "fw/api/alive.h"
#include <linux/efi.h>
#include "fw/runtime.h"

#define IWL_EFI_WIFI_GUID	EFI_GUID(0x92daaf2f, 0xc02b, 0x455b,	\
					 0xb2, 0xec, 0xf5, 0xa3,	\
					 0x59, 0x4f, 0x4a, 0xea)
#define IWL_EFI_WIFI_BT_GUID	EFI_GUID(0xe65d8884, 0xd4af, 0x4b20,	\
					 0x8d, 0x03, 0x77, 0x2e,	\
					 0xcc, 0x3d, 0xa5, 0x31)

struct iwl_uefi_pnvm_mem_desc {
	__le32 addr;
	__le32 size;
	const u8 data[];
} __packed;

static void *iwl_uefi_get_variable(efi_char16_t *name, efi_guid_t *guid,
				   unsigned long *data_size)
{
	efi_status_t status;
	void *data;

	if (!data_size)
		return ERR_PTR(-EINVAL);

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return ERR_PTR(-ENODEV);

	/* first call with NULL data to get the exact entry size */
	*data_size = 0;
	status = efi.get_variable(name, guid, NULL, data_size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL || !*data_size)
		return ERR_PTR(-EIO);

	data = kmalloc(*data_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	status = efi.get_variable(name, guid, NULL, data_size, data);
	if (status != EFI_SUCCESS) {
		kfree(data);
		return ERR_PTR(-ENOENT);
	}

	return data;
}

void *iwl_uefi_get_pnvm(struct iwl_trans *trans, size_t *len)
{
	unsigned long package_size;
	void *data;

	*len = 0;

	data = iwl_uefi_get_variable(IWL_UEFI_OEM_PNVM_NAME, &IWL_EFI_WIFI_GUID,
				     &package_size);
	if (IS_ERR(data)) {
		IWL_DEBUG_FW(trans,
			     "PNVM UEFI variable not found 0x%lx (len %lu)\n",
			     PTR_ERR(data), package_size);
		return data;
	}

	IWL_DEBUG_FW(trans, "Read PNVM from UEFI with size %lu\n", package_size);
	*len = package_size;

	return data;
}

static void *
iwl_uefi_get_verified_variable_guid(struct iwl_trans *trans,
				    efi_guid_t *guid,
				    efi_char16_t *uefi_var_name,
				    char *var_name,
				    unsigned int expected_size,
				    unsigned long *size)
{
	void *var;
	unsigned long var_size;

	var = iwl_uefi_get_variable(uefi_var_name, guid, &var_size);

	if (IS_ERR(var)) {
		IWL_DEBUG_RADIO(trans,
				"%s UEFI variable not found 0x%lx\n", var_name,
				PTR_ERR(var));
		return var;
	}

	if (var_size < expected_size) {
		IWL_DEBUG_RADIO(trans,
				"Invalid %s UEFI variable len (%lu)\n",
				var_name, var_size);
		kfree(var);
		return ERR_PTR(-EINVAL);
	}

	IWL_DEBUG_RADIO(trans, "%s from UEFI with size %lu\n", var_name,
			var_size);

	if (size)
		*size = var_size;
	return var;
}

static void *
iwl_uefi_get_verified_variable(struct iwl_trans *trans,
			       efi_char16_t *uefi_var_name,
			       char *var_name,
			       unsigned int expected_size,
			       unsigned long *size)
{
	return iwl_uefi_get_verified_variable_guid(trans, &IWL_EFI_WIFI_GUID,
						   uefi_var_name, var_name,
						   expected_size, size);
}

int iwl_uefi_handle_tlv_mem_desc(struct iwl_trans *trans, const u8 *data,
				 u32 tlv_len, struct iwl_pnvm_image *pnvm_data)
{
	const struct iwl_uefi_pnvm_mem_desc *desc = (const void *)data;
	u32 data_len;

	if (tlv_len < sizeof(*desc)) {
		IWL_DEBUG_FW(trans, "TLV len (%d) is too small\n", tlv_len);
		return -EINVAL;
	}

	data_len = tlv_len - sizeof(*desc);

	IWL_DEBUG_FW(trans,
		     "Handle IWL_UCODE_TLV_MEM_DESC, len %d data_len %d\n",
		     tlv_len, data_len);

	if (le32_to_cpu(desc->size) != data_len) {
		IWL_DEBUG_FW(trans, "invalid mem desc size %d\n", desc->size);
		return -EINVAL;
	}

	if (pnvm_data->n_chunks == IPC_DRAM_MAP_ENTRY_NUM_MAX) {
		IWL_DEBUG_FW(trans, "too many payloads to allocate in DRAM.\n");
		return -EINVAL;
	}

	IWL_DEBUG_FW(trans, "Adding data (size %d)\n", data_len);

	pnvm_data->chunks[pnvm_data->n_chunks].data = desc->data;
	pnvm_data->chunks[pnvm_data->n_chunks].len = data_len;
	pnvm_data->n_chunks++;

	return 0;
}

static int iwl_uefi_reduce_power_section(struct iwl_trans *trans,
					 const u8 *data, size_t len,
					 struct iwl_pnvm_image *pnvm_data)
{
	const struct iwl_ucode_tlv *tlv;

	IWL_DEBUG_FW(trans, "Handling REDUCE_POWER section\n");
	memset(pnvm_data, 0, sizeof(*pnvm_data));

	while (len >= sizeof(*tlv)) {
		u32 tlv_len, tlv_type;

		len -= sizeof(*tlv);
		tlv = (const void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le32_to_cpu(tlv->type);

		if (len < tlv_len) {
			IWL_ERR(trans, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}

		data += sizeof(*tlv);

		switch (tlv_type) {
		case IWL_UCODE_TLV_MEM_DESC:
			if (iwl_uefi_handle_tlv_mem_desc(trans, data, tlv_len,
							 pnvm_data))
				return -EINVAL;
			break;
		case IWL_UCODE_TLV_PNVM_SKU:
			IWL_DEBUG_FW(trans,
				     "New REDUCE_POWER section started, stop parsing.\n");
			goto done;
		default:
			IWL_DEBUG_FW(trans, "Found TLV 0x%0x, len %d\n",
				     tlv_type, tlv_len);
			break;
		}

		len -= ALIGN(tlv_len, 4);
		data += ALIGN(tlv_len, 4);
	}

done:
	if (!pnvm_data->n_chunks) {
		IWL_DEBUG_FW(trans, "Empty REDUCE_POWER, skipping.\n");
		return -ENOENT;
	}
	return 0;
}

int iwl_uefi_reduce_power_parse(struct iwl_trans *trans,
				const u8 *data, size_t len,
				struct iwl_pnvm_image *pnvm_data,
				__le32 sku_id[3])
{
	const struct iwl_ucode_tlv *tlv;

	IWL_DEBUG_FW(trans, "Parsing REDUCE_POWER data\n");

	while (len >= sizeof(*tlv)) {
		u32 tlv_len, tlv_type;

		len -= sizeof(*tlv);
		tlv = (const void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le32_to_cpu(tlv->type);

		if (len < tlv_len) {
			IWL_ERR(trans, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}

		if (tlv_type == IWL_UCODE_TLV_PNVM_SKU) {
			const struct iwl_sku_id *tlv_sku_id =
				(const void *)(data + sizeof(*tlv));

			IWL_DEBUG_FW(trans,
				     "Got IWL_UCODE_TLV_PNVM_SKU len %d\n",
				     tlv_len);
			IWL_DEBUG_FW(trans, "sku_id 0x%0x 0x%0x 0x%0x\n",
				     le32_to_cpu(tlv_sku_id->data[0]),
				     le32_to_cpu(tlv_sku_id->data[1]),
				     le32_to_cpu(tlv_sku_id->data[2]));

			data += sizeof(*tlv) + ALIGN(tlv_len, 4);
			len -= ALIGN(tlv_len, 4);

			if (sku_id[0] == tlv_sku_id->data[0] &&
			    sku_id[1] == tlv_sku_id->data[1] &&
			    sku_id[2] == tlv_sku_id->data[2]) {
				int ret = iwl_uefi_reduce_power_section(trans,
								    data, len,
								    pnvm_data);
				if (!ret)
					return 0;
			} else {
				IWL_DEBUG_FW(trans, "SKU ID didn't match!\n");
			}
		} else {
			data += sizeof(*tlv) + ALIGN(tlv_len, 4);
			len -= ALIGN(tlv_len, 4);
		}
	}

	return -ENOENT;
}

u8 *iwl_uefi_get_reduced_power(struct iwl_trans *trans, size_t *len)
{
	struct pnvm_sku_package *package;
	unsigned long package_size;
	u8 *data;

	package = iwl_uefi_get_verified_variable(trans,
						 IWL_UEFI_REDUCED_POWER_NAME,
						 "Reduced Power",
						 sizeof(*package),
						 &package_size);
	if (IS_ERR(package))
		return ERR_CAST(package);

	IWL_DEBUG_FW(trans, "rev %d, total_size %d, n_skus %d\n",
		     package->rev, package->total_size, package->n_skus);

	*len = package_size - sizeof(*package);
	data = kmemdup(package->data, *len, GFP_KERNEL);
	if (!data) {
		kfree(package);
		return ERR_PTR(-ENOMEM);
	}

	kfree(package);

	return data;
}

static int iwl_uefi_step_parse(struct uefi_cnv_common_step_data *common_step_data,
			       struct iwl_trans *trans)
{
	if (common_step_data->revision != 1)
		return -EINVAL;

	trans->conf.mbx_addr_0_step =
		(u32)common_step_data->revision |
		(u32)common_step_data->cnvi_eq_channel << 8 |
		(u32)common_step_data->cnvr_eq_channel << 16 |
		(u32)common_step_data->radio1 << 24;
	trans->conf.mbx_addr_1_step = (u32)common_step_data->radio2;
	return 0;
}

void iwl_uefi_get_step_table(struct iwl_trans *trans)
{
	struct uefi_cnv_common_step_data *data;
	int ret;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return;

	data = iwl_uefi_get_verified_variable_guid(trans, &IWL_EFI_WIFI_BT_GUID,
						   IWL_UEFI_STEP_NAME,
						   "STEP", sizeof(*data), NULL);
	if (IS_ERR(data))
		return;

	ret = iwl_uefi_step_parse(data, trans);
	if (ret < 0)
		IWL_DEBUG_FW(trans, "Cannot read STEP tables. rev is invalid\n");

	kfree(data);
}
IWL_EXPORT_SYMBOL(iwl_uefi_get_step_table);

static int iwl_uefi_sgom_parse(struct uefi_cnv_wlan_sgom_data *sgom_data,
			       struct iwl_fw_runtime *fwrt)
{
	int i, j;

	if (sgom_data->revision != 1)
		return -EINVAL;

	memcpy(fwrt->sgom_table.offset_map, sgom_data->offset_map,
	       sizeof(fwrt->sgom_table.offset_map));

	for (i = 0; i < MCC_TO_SAR_OFFSET_TABLE_ROW_SIZE; i++) {
		for (j = 0; j < MCC_TO_SAR_OFFSET_TABLE_COL_SIZE; j++) {
			/* since each byte is composed of to values, */
			/* one for each letter, */
			/* extract and check each of them separately */
			u8 value = fwrt->sgom_table.offset_map[i][j];
			u8 low = value & 0xF;
			u8 high = (value & 0xF0) >> 4;

			if (high > fwrt->geo_num_profiles)
				high = 0;
			if (low > fwrt->geo_num_profiles)
				low = 0;
			fwrt->sgom_table.offset_map[i][j] = (high << 4) | low;
		}
	}

	fwrt->sgom_enabled = true;
	return 0;
}

void iwl_uefi_get_sgom_table(struct iwl_trans *trans,
			     struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_wlan_sgom_data *data;
	int ret;

	if (!fwrt->geo_enabled)
		return;

	data = iwl_uefi_get_verified_variable(trans, IWL_UEFI_SGOM_NAME,
					      "SGOM", sizeof(*data), NULL);
	if (IS_ERR(data))
		return;

	ret = iwl_uefi_sgom_parse(data, fwrt);
	if (ret < 0)
		IWL_DEBUG_FW(trans, "Cannot read SGOM tables. rev is invalid\n");

	kfree(data);
}
IWL_EXPORT_SYMBOL(iwl_uefi_get_sgom_table);

static int iwl_uefi_uats_parse(struct uefi_cnv_wlan_uats_data *uats_data,
			       struct iwl_fw_runtime *fwrt)
{
	if (uats_data->revision != 1)
		return -EINVAL;

	memcpy(fwrt->uats_table.offset_map, uats_data->offset_map,
	       sizeof(fwrt->uats_table.offset_map));

	fwrt->uats_valid = true;

	return 0;
}

void iwl_uefi_get_uats_table(struct iwl_trans *trans,
			     struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_wlan_uats_data *data;
	int ret;

	data = iwl_uefi_get_verified_variable(trans, IWL_UEFI_UATS_NAME,
					      "UATS", sizeof(*data), NULL);
	if (IS_ERR(data))
		return;

	ret = iwl_uefi_uats_parse(data, fwrt);
	if (ret < 0)
		IWL_DEBUG_FW(trans, "Cannot read UATS table. rev is invalid\n");
	kfree(data);
}
IWL_EXPORT_SYMBOL(iwl_uefi_get_uats_table);

static void iwl_uefi_set_sar_profile(struct iwl_fw_runtime *fwrt,
				     struct uefi_sar_profile *uefi_sar_prof,
				     u8 prof_index, bool enabled)
{
	memcpy(&fwrt->sar_profiles[prof_index].chains, uefi_sar_prof,
	       sizeof(struct uefi_sar_profile));

	fwrt->sar_profiles[prof_index].enabled = enabled & IWL_SAR_ENABLE_MSK;
}

int iwl_uefi_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_var_wrds *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WRDS_NAME,
					      "WRDS", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_WRDS_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WRDS revision:%d\n",
				data->revision);
		goto out;
	}

	/* The profile from WRDS is officially profile 1, but goes
	 * into sar_profiles[0] (because we don't have a profile 0).
	 */
	iwl_uefi_set_sar_profile(fwrt, &data->sar_profile, 0, data->mode);
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_var_ewrd *data;
	int i, ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_EWRD_NAME,
					      "EWRD", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_EWRD_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI EWRD revision:%d\n",
				data->revision);
		goto out;
	}

	if (data->num_profiles >= BIOS_SAR_MAX_PROFILE_NUM) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < data->num_profiles; i++)
		/* The EWRD profiles officially go from 2 to 4, but we
		 * save them in sar_profiles[1-3] (because we don't
		 * have profile 0).  So in the array we start from 1.
		 */
		iwl_uefi_set_sar_profile(fwrt, &data->sar_profiles[i], i + 1,
					 data->mode);

out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_var_wgds *data;
	int i, ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WGDS_NAME,
					      "WGDS", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_WGDS_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WGDS revision:%d\n",
				data->revision);
		goto out;
	}

	if (data->num_profiles < BIOS_GEO_MIN_PROFILE_NUM ||
	    data->num_profiles > BIOS_GEO_MAX_PROFILE_NUM) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Invalid number of profiles in WGDS: %d\n",
				data->num_profiles);
		goto out;
	}

	fwrt->geo_rev = data->revision;
	for (i = 0; i < data->num_profiles; i++)
		memcpy(&fwrt->geo_profiles[i], &data->geo_profiles[i],
		       sizeof(struct iwl_geo_profile));

	fwrt->geo_num_profiles = data->num_profiles;
	fwrt->geo_enabled = true;
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_var_ppag *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_PPAG_NAME,
					      "PPAG", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision < IWL_UEFI_MIN_PPAG_REV ||
	    data->revision > IWL_UEFI_MAX_PPAG_REV) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI PPAG revision:%d\n",
				data->revision);
		goto out;
	}

	fwrt->ppag_bios_rev = data->revision;
	fwrt->ppag_flags = iwl_bios_get_ppag_flags(data->ppag_modes,
						   fwrt->ppag_bios_rev);

	BUILD_BUG_ON(sizeof(fwrt->ppag_chains) != sizeof(data->ppag_chains));
	memcpy(&fwrt->ppag_chains, &data->ppag_chains,
	       sizeof(data->ppag_chains));
	fwrt->ppag_bios_source = BIOS_SOURCE_UEFI;
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_tas_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_tas_data *tas_data)
{
	struct uefi_cnv_var_wtas *uefi_tas;
	int ret, enabled;

	uefi_tas = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WTAS_NAME,
						  "WTAS", sizeof(*uefi_tas), NULL);
	if (IS_ERR(uefi_tas))
		return -EINVAL;

	if (uefi_tas->revision < IWL_UEFI_MIN_WTAS_REVISION ||
	    uefi_tas->revision > IWL_UEFI_MAX_WTAS_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WTAS revision:%d\n",
				uefi_tas->revision);
		goto out;
	}

	IWL_DEBUG_RADIO(fwrt, "TAS selection as read from BIOS: 0x%x\n",
			uefi_tas->tas_selection);

	enabled = uefi_tas->tas_selection & IWL_WTAS_ENABLED_MSK;
	tas_data->table_source = BIOS_SOURCE_UEFI;
	tas_data->table_revision = uefi_tas->revision;
	tas_data->tas_selection = uefi_tas->tas_selection;

	IWL_DEBUG_RADIO(fwrt, "TAS %s enabled\n",
			enabled ? "is" : "not");

	IWL_DEBUG_RADIO(fwrt, "Reading TAS table revision %d\n",
			uefi_tas->revision);
	if (uefi_tas->black_list_size > IWL_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size %d\n",
				uefi_tas->black_list_size);
		ret = -EINVAL;
		goto out;
	}

	tas_data->block_list_size = uefi_tas->black_list_size;
	IWL_DEBUG_RADIO(fwrt, "TAS array size %u\n", uefi_tas->black_list_size);

	for (u8 i = 0; i < uefi_tas->black_list_size; i++) {
		tas_data->block_list_array[i] = uefi_tas->black_list[i];
		IWL_DEBUG_RADIO(fwrt, "TAS block list country %d\n",
				uefi_tas->black_list[i]);
	}
	ret = enabled;
out:
	kfree(uefi_tas);
	return ret;
}

int iwl_uefi_get_pwr_limit(struct iwl_fw_runtime *fwrt,
			   u64 *dflt_pwr_limit)
{
	struct uefi_cnv_var_splc *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_SPLC_NAME,
					      "SPLC", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_SPLC_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI SPLC revision:%d\n",
				data->revision);
		goto out;
	}
	*dflt_pwr_limit = data->default_pwr_limit;
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc)
{
	struct uefi_cnv_var_wrdd *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WRDD_NAME,
					      "WRDD", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_WRDD_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WRDD revision:%d\n",
				data->revision);
		goto out;
	}

	if (data->mcc != BIOS_MCC_CHINA) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "UEFI WRDD is supported only for CN\n");
		goto out;
	}

	mcc[0] = (data->mcc >> 8) & 0xff;
	mcc[1] = data->mcc & 0xff;
	mcc[2] = '\0';
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk)
{
	struct uefi_cnv_var_eckv *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable_guid(fwrt->trans,
						   &IWL_EFI_WIFI_BT_GUID,
						   IWL_UEFI_ECKV_NAME,
						   "ECKV", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_ECKV_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI ECKV revision:%d\n",
				data->revision);
		goto out;
	}
	*extl_clk = data->ext_clock_valid;
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value)
{
	struct uefi_cnv_wlan_wbem_data *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WBEM_NAME,
					      "WBEM", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_WBEM_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WBEM revision:%d\n",
				data->revision);
		goto out;
	}
	*value = data->wbem_320mhz_per_mcc & IWL_UEFI_WBEM_REV0_MASK;
	IWL_DEBUG_RADIO(fwrt, "Loaded WBEM config from UEFI\n");
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_dsm(struct iwl_fw_runtime *fwrt, enum iwl_dsm_funcs func,
		     u32 *value)
{
	struct uefi_cnv_var_general_cfg *data;
	int ret = -EINVAL;

	/* Not supported function index */
	if (func >= DSM_FUNC_NUM_FUNCS || func == 5)
		return -EOPNOTSUPP;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_DSM_NAME,
					      "DSM", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_DSM_REVISION) {
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI DSM revision:%d\n",
				data->revision);
		goto out;
	}

	if (ARRAY_SIZE(data->functions) != UEFI_MAX_DSM_FUNCS) {
		IWL_DEBUG_RADIO(fwrt, "Invalid size of DSM functions array\n");
		goto out;
	}

	*value = data->functions[func];

	IWL_DEBUG_RADIO(fwrt,
			"UEFI: DSM func=%d: value=%d\n", func, *value);

	ret = 0;
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_puncturing(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_var_puncturing_data *data;
	/* default value is not enabled if there is any issue in reading
	 * uefi variable or revision is not supported
	 */
	int puncturing = 0;

	data = iwl_uefi_get_verified_variable(fwrt->trans,
					      IWL_UEFI_PUNCTURING_NAME,
					      "UefiCnvWlanPuncturing",
					      sizeof(*data), NULL);
	if (IS_ERR(data))
		return puncturing;

	if (data->revision != IWL_UEFI_PUNCTURING_REVISION) {
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI PUNCTURING rev:%d\n",
				data->revision);
	} else {
		puncturing = data->puncturing & IWL_UEFI_PUNCTURING_REV0_MASK;
		IWL_DEBUG_RADIO(fwrt, "Loaded puncturing bits from UEFI: %d\n",
				puncturing);
	}

	kfree(data);
	return puncturing;
}
IWL_EXPORT_SYMBOL(iwl_uefi_get_puncturing);

int iwl_uefi_get_dsbr(struct iwl_fw_runtime *fwrt, u32 *value)
{
	struct uefi_cnv_wlan_dsbr_data *data;
	int ret = 0;

	data = iwl_uefi_get_verified_variable_guid(fwrt->trans,
						   &IWL_EFI_WIFI_BT_GUID,
						   IWL_UEFI_DSBR_NAME, "DSBR",
						   sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != IWL_UEFI_DSBR_REVISION) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI DSBR revision:%d\n",
				data->revision);
		goto out;
	}
	*value = data->config;
	IWL_DEBUG_RADIO(fwrt, "Loaded DSBR config from UEFI value: 0x%x\n",
			*value);
out:
	kfree(data);
	return ret;
}

int iwl_uefi_get_phy_filters(struct iwl_fw_runtime *fwrt)
{
	struct uefi_cnv_wpfc_data *data __free(kfree);
	struct iwl_phy_specific_cfg *filters = &fwrt->phy_filters;

	data = iwl_uefi_get_verified_variable(fwrt->trans, IWL_UEFI_WPFC_NAME,
					      "WPFC", sizeof(*data), NULL);
	if (IS_ERR(data))
		return -EINVAL;

	if (data->revision != 0) {
		IWL_DEBUG_RADIO(fwrt, "Unsupported UEFI WPFC revision:%d\n",
			data->revision);
		return -EINVAL;
	}

	BUILD_BUG_ON(ARRAY_SIZE(filters->filter_cfg_chains) !=
		     ARRAY_SIZE(data->chains));

	for (int i = 0; i < ARRAY_SIZE(filters->filter_cfg_chains); i++) {
		filters->filter_cfg_chains[i] = cpu_to_le32(data->chains[i]);
		IWL_DEBUG_RADIO(fwrt, "WPFC: chain %d: %u\n", i, data->chains[i]);
	}

	IWL_DEBUG_RADIO(fwrt, "Loaded WPFC config from UEFI\n");
	return 0;
}
