// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2020-2025 Intel Corporation
 */

#include "iwl-drv.h"
#include "pnvm.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "fw/api/commands.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/alive.h"
#include "fw/uefi.h"

#define IWL_PNVM_REDUCED_CAP_BIT BIT(25)

struct iwl_pnvm_section {
	__le32 offset;
	const u8 data[];
} __packed;

static bool iwl_pnvm_complete_fn(struct iwl_notif_wait_data *notif_wait,
				 struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_trans *trans = (struct iwl_trans *)data;
	struct iwl_pnvm_init_complete_ntfy *pnvm_ntf = (void *)pkt->data;

	IWL_DEBUG_FW(trans,
		     "PNVM complete notification received with status 0x%0x\n",
		     le32_to_cpu(pnvm_ntf->status));

	return true;
}

static int iwl_pnvm_handle_section(struct iwl_trans *trans, const u8 *data,
				   size_t len,
				   struct iwl_pnvm_image *pnvm_data)
{
	const struct iwl_ucode_tlv *tlv;
	u32 sha1 = 0;
	u16 mac_type = 0, rf_id = 0;
	bool hw_match = false;

	IWL_DEBUG_FW(trans, "Handling PNVM section\n");

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
		case IWL_UCODE_TLV_PNVM_VERSION:
			if (tlv_len < sizeof(__le32)) {
				IWL_DEBUG_FW(trans,
					     "Invalid size for IWL_UCODE_TLV_PNVM_VERSION (expected %zd, got %d)\n",
					     sizeof(__le32), tlv_len);
				break;
			}

			sha1 = le32_to_cpup((const __le32 *)data);

			IWL_DEBUG_FW(trans,
				     "Got IWL_UCODE_TLV_PNVM_VERSION %0x\n",
				     sha1);
			pnvm_data->version = sha1;
			break;
		case IWL_UCODE_TLV_HW_TYPE:
			if (tlv_len < 2 * sizeof(__le16)) {
				IWL_DEBUG_FW(trans,
					     "Invalid size for IWL_UCODE_TLV_HW_TYPE (expected %zd, got %d)\n",
					     2 * sizeof(__le16), tlv_len);
				break;
			}

			if (hw_match)
				break;

			mac_type = le16_to_cpup((const __le16 *)data);
			rf_id = le16_to_cpup((const __le16 *)(data + sizeof(__le16)));

			IWL_DEBUG_FW(trans,
				     "Got IWL_UCODE_TLV_HW_TYPE mac_type 0x%0x rf_id 0x%0x\n",
				     mac_type, rf_id);

			if (mac_type == CSR_HW_REV_TYPE(trans->info.hw_rev) &&
			    rf_id == CSR_HW_RFID_TYPE(trans->info.hw_rf_id))
				hw_match = true;
			break;
		case IWL_UCODE_TLV_SEC_RT: {
			const struct iwl_pnvm_section *section = (const void *)data;
			u32 data_len = tlv_len - sizeof(*section);

			IWL_DEBUG_FW(trans,
				     "Got IWL_UCODE_TLV_SEC_RT len %d\n",
				     tlv_len);

			/* TODO: remove, this is a deprecated separator */
			if (le32_to_cpup((const __le32 *)data) == 0xddddeeee) {
				IWL_DEBUG_FW(trans, "Ignoring separator.\n");
				break;
			}

			if (pnvm_data->n_chunks == IPC_DRAM_MAP_ENTRY_NUM_MAX) {
				IWL_DEBUG_FW(trans,
					     "too many payloads to allocate in DRAM.\n");
				return -EINVAL;
			}

			IWL_DEBUG_FW(trans, "Adding data (size %d)\n",
				     data_len);

			pnvm_data->chunks[pnvm_data->n_chunks].data = section->data;
			pnvm_data->chunks[pnvm_data->n_chunks].len = data_len;
			pnvm_data->n_chunks++;

			break;
		}
		case IWL_UCODE_TLV_MEM_DESC:
			if (iwl_uefi_handle_tlv_mem_desc(trans, data, tlv_len,
							 pnvm_data))
				return -EINVAL;
			break;
		case IWL_UCODE_TLV_PNVM_SKU:
			IWL_DEBUG_FW(trans,
				     "New PNVM section started, stop parsing.\n");
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
	if (!hw_match) {
		IWL_DEBUG_FW(trans,
			     "HW mismatch, skipping PNVM section (need mac_type 0x%x rf_id 0x%x)\n",
			     CSR_HW_REV_TYPE(trans->info.hw_rev),
			     CSR_HW_RFID_TYPE(trans->info.hw_rf_id));
		return -ENOENT;
	}

	if (!pnvm_data->n_chunks) {
		IWL_DEBUG_FW(trans, "Empty PNVM, skipping.\n");
		return -ENOENT;
	}

	return 0;
}

static int iwl_pnvm_parse(struct iwl_trans *trans, const u8 *data,
			  size_t len,
			  struct iwl_pnvm_image *pnvm_data,
			  __le32 sku_id[3])
{
	const struct iwl_ucode_tlv *tlv;

	IWL_DEBUG_FW(trans, "Parsing PNVM file\n");

	while (len >= sizeof(*tlv)) {
		u32 tlv_len, tlv_type;
		u32 rf_type;

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

			trans->reduced_cap_sku = false;
			rf_type = CSR_HW_RFID_TYPE(trans->info.hw_rf_id);
			if ((sku_id[0] & cpu_to_le32(IWL_PNVM_REDUCED_CAP_BIT)) &&
			    rf_type == IWL_CFG_RF_TYPE_FM)
				trans->reduced_cap_sku = true;

			IWL_DEBUG_FW(trans,
				     "Reduced SKU device %d\n",
				     trans->reduced_cap_sku);

			if (sku_id[0] == tlv_sku_id->data[0] &&
			    sku_id[1] == tlv_sku_id->data[1] &&
			    sku_id[2] == tlv_sku_id->data[2]) {
				int ret;

				ret = iwl_pnvm_handle_section(trans, data, len,
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

static int iwl_pnvm_get_from_fs(struct iwl_trans *trans, u8 **data, size_t *len)
{
	const struct firmware *pnvm;
	char pnvm_name[MAX_PNVM_NAME];
	size_t new_len;
	int ret;

	iwl_pnvm_get_fs_name(trans, pnvm_name, sizeof(pnvm_name));

	ret = firmware_request_nowarn(&pnvm, pnvm_name, trans->dev);
	if (ret) {
		IWL_DEBUG_FW(trans, "PNVM file %s not found %d\n",
			     pnvm_name, ret);
		return ret;
	}

	new_len = pnvm->size;
	*data = kvmemdup(pnvm->data, pnvm->size, GFP_KERNEL);
	release_firmware(pnvm);

	if (!*data)
		return -ENOMEM;

	*len = new_len;

	return 0;
}

static u8 *iwl_get_pnvm_image(struct iwl_trans *trans_p, size_t *len,
			      __le32 sku_id[3])
{
	struct pnvm_sku_package *package;
	u8 *image = NULL;

	/* Get PNVM from BIOS for non-Intel SKU */
	if (sku_id[2]) {
		package = iwl_uefi_get_pnvm(trans_p, len);
		if (!IS_ERR_OR_NULL(package)) {
			if (*len >= sizeof(*package)) {
				/* we need only the data */
				*len -= sizeof(*package);
				image = kvmemdup(package->data,
						 *len, GFP_KERNEL);
			}
			/*
			 * free package regardless of whether kmemdup
			 * succeeded
			 */
			kfree(package);
			if (image)
				return image;
		}
	}

	/* If it's not available, or for Intel SKU, try from the filesystem */
	if (iwl_pnvm_get_from_fs(trans_p, &image, len))
		return NULL;
	return image;
}

static void
iwl_pnvm_load_pnvm_to_trans(struct iwl_trans *trans,
			    const struct iwl_ucode_capabilities *capa,
			    __le32 sku_id[3])
{
	struct iwl_pnvm_image *pnvm_data = NULL;
	u8 *data = NULL;
	size_t length;
	int ret;

	/* failed to get/parse the image in the past, no use trying again */
	if (trans->fail_to_parse_pnvm_image)
		return;

	if (trans->pnvm_loaded)
		goto set;

	data = iwl_get_pnvm_image(trans, &length, sku_id);
	if (!data) {
		trans->fail_to_parse_pnvm_image = true;
		return;
	}

	pnvm_data = kzalloc(sizeof(*pnvm_data), GFP_KERNEL);
	if (!pnvm_data)
		goto free;

	ret = iwl_pnvm_parse(trans, data, length, pnvm_data, sku_id);
	if (ret) {
		trans->fail_to_parse_pnvm_image = true;
		goto free;
	}

	ret = iwl_trans_load_pnvm(trans, pnvm_data, capa);
	if (ret)
		goto free;
	IWL_DEBUG_INFO(trans, "loaded PNVM version %08x\n", pnvm_data->version);

set:
	iwl_trans_set_pnvm(trans, capa);
free:
	kvfree(data);
	kfree(pnvm_data);
}

static void
iwl_pnvm_load_reduce_power_to_trans(struct iwl_trans *trans,
				    const struct iwl_ucode_capabilities *capa,
				    __le32 sku_id[3])
{
	struct iwl_pnvm_image *pnvm_data = NULL;
	u8 *data = NULL;
	size_t length;
	int ret;

	if (trans->failed_to_load_reduce_power_image)
		return;

	if (trans->reduce_power_loaded)
		goto set;

	data = iwl_uefi_get_reduced_power(trans, &length);
	if (IS_ERR(data)) {
		trans->failed_to_load_reduce_power_image = true;
		return;
	}

	pnvm_data = kzalloc(sizeof(*pnvm_data), GFP_KERNEL);
	if (!pnvm_data)
		goto free;

	ret = iwl_uefi_reduce_power_parse(trans, data, length, pnvm_data,
					  sku_id);
	if (ret) {
		trans->failed_to_load_reduce_power_image = true;
		goto free;
	}

	ret = iwl_trans_load_reduce_power(trans, pnvm_data, capa);
	if (ret) {
		IWL_DEBUG_FW(trans,
			     "Failed to load reduce power table %d\n",
			     ret);
		trans->failed_to_load_reduce_power_image = true;
		goto free;
	}

set:
	iwl_trans_set_reduce_power(trans, capa);
free:
	kfree(data);
	kfree(pnvm_data);
}

int iwl_pnvm_load(struct iwl_trans *trans,
		  struct iwl_notif_wait_data *notif_wait,
		  const struct iwl_ucode_capabilities *capa,
		  __le32 sku_id[3])
{
	struct iwl_notification_wait pnvm_wait;
	static const u16 ntf_cmds[] = { WIDE_ID(REGULATORY_AND_NVM_GROUP,
						PNVM_INIT_COMPLETE_NTFY) };

	/* if the SKU_ID is empty, there's nothing to do */
	if (!sku_id[0] && !sku_id[1] && !sku_id[2])
		return 0;

	iwl_pnvm_load_pnvm_to_trans(trans, capa, sku_id);
	iwl_pnvm_load_reduce_power_to_trans(trans, capa, sku_id);

	iwl_init_notification_wait(notif_wait, &pnvm_wait,
				   ntf_cmds, ARRAY_SIZE(ntf_cmds),
				   iwl_pnvm_complete_fn, trans);

	/* kick the doorbell */
	iwl_write_umac_prph(trans, UREG_DOORBELL_TO_ISR6,
			    UREG_DOORBELL_TO_ISR6_PNVM);

	return iwl_wait_notification(notif_wait, &pnvm_wait,
				     MVM_UCODE_PNVM_TIMEOUT);
}
IWL_EXPORT_SYMBOL(iwl_pnvm_load);
