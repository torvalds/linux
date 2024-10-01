// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"

#include "debug.h"

static int ath11k_fw_request_firmware_api_n(struct ath11k_base *ab,
					    const char *name)
{
	size_t magic_len, len, ie_len;
	int ie_id, i, index, bit, ret;
	struct ath11k_fw_ie *hdr;
	const u8 *data;
	__le32 *timestamp;

	ab->fw.fw = ath11k_core_firmware_request(ab, name);
	if (IS_ERR(ab->fw.fw)) {
		ret = PTR_ERR(ab->fw.fw);
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "failed to load %s: %d\n", name, ret);
		ab->fw.fw = NULL;
		return ret;
	}

	data = ab->fw.fw->data;
	len = ab->fw.fw->size;

	/* magic also includes the null byte, check that as well */
	magic_len = strlen(ATH11K_FIRMWARE_MAGIC) + 1;

	if (len < magic_len) {
		ath11k_err(ab, "firmware image too small to contain magic: %zu\n",
			   len);
		ret = -EINVAL;
		goto err;
	}

	if (memcmp(data, ATH11K_FIRMWARE_MAGIC, magic_len) != 0) {
		ath11k_err(ab, "Invalid firmware magic\n");
		ret = -EINVAL;
		goto err;
	}

	/* jump over the padding */
	magic_len = ALIGN(magic_len, 4);

	/* make sure there's space for padding */
	if (magic_len > len) {
		ath11k_err(ab, "No space for padding after magic\n");
		ret = -EINVAL;
		goto err;
	}

	len -= magic_len;
	data += magic_len;

	/* loop elements */
	while (len > sizeof(struct ath11k_fw_ie)) {
		hdr = (struct ath11k_fw_ie *)data;

		ie_id = le32_to_cpu(hdr->id);
		ie_len = le32_to_cpu(hdr->len);

		len -= sizeof(*hdr);
		data += sizeof(*hdr);

		if (len < ie_len) {
			ath11k_err(ab, "Invalid length for FW IE %d (%zu < %zu)\n",
				   ie_id, len, ie_len);
			ret = -EINVAL;
			goto err;
		}

		switch (ie_id) {
		case ATH11K_FW_IE_TIMESTAMP:
			if (ie_len != sizeof(u32))
				break;

			timestamp = (__le32 *)data;

			ath11k_dbg(ab, ATH11K_DBG_BOOT, "found fw timestamp %d\n",
				   le32_to_cpup(timestamp));
			break;
		case ATH11K_FW_IE_FEATURES:
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "found firmware features ie (%zd B)\n",
				   ie_len);

			for (i = 0; i < ATH11K_FW_FEATURE_COUNT; i++) {
				index = i / 8;
				bit = i % 8;

				if (index == ie_len)
					break;

				if (data[index] & (1 << bit))
					__set_bit(i, ab->fw.fw_features);
			}

			ath11k_dbg_dump(ab, ATH11K_DBG_BOOT, "features", "",
					ab->fw.fw_features,
					sizeof(ab->fw.fw_features));
			break;
		case ATH11K_FW_IE_AMSS_IMAGE:
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "found fw image ie (%zd B)\n",
				   ie_len);

			ab->fw.amss_data = data;
			ab->fw.amss_len = ie_len;
			break;
		case ATH11K_FW_IE_M3_IMAGE:
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "found m3 image ie (%zd B)\n",
				   ie_len);

			ab->fw.m3_data = data;
			ab->fw.m3_len = ie_len;
			break;
		default:
			ath11k_warn(ab, "Unknown FW IE: %u\n", ie_id);
			break;
		}

		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		/* make sure there's space for padding */
		if (ie_len > len)
			break;

		len -= ie_len;
		data += ie_len;
	}

	return 0;

err:
	release_firmware(ab->fw.fw);
	ab->fw.fw = NULL;
	return ret;
}

int ath11k_fw_pre_init(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_fw_request_firmware_api_n(ab, ATH11K_FW_API2_FILE);
	if (ret == 0) {
		ab->fw.api_version = 2;
		goto out;
	}

	ab->fw.api_version = 1;

out:
	ath11k_dbg(ab, ATH11K_DBG_BOOT, "using fw api %d\n",
		   ab->fw.api_version);

	return 0;
}

void ath11k_fw_destroy(struct ath11k_base *ab)
{
	release_firmware(ab->fw.fw);
}
