// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilSecure APIs.
 *
 * Copyright (C) 2014-2022 Xilinx, Inc.
 * Copyright (C) 2022-2025 Advanced Micro Devices, Inc.
 */

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/module.h>

/**
 * zynqmp_pm_aes_engine - Access AES hardware to encrypt/decrypt the data using
 * AES-GCM core.
 * @address:	Address of the AesParams structure.
 * @out:	Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_SECURE_AES, ret_payload, 2, upper_32_bits(address),
				  lower_32_bits(address));
	*out = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_aes_engine);

/**
 * zynqmp_pm_sha_hash - Access the SHA engine to calculate the hash
 * @address:	Address of the data/ Address of output buffer where
 *		hash should be stored.
 * @size:	Size of the data.
 * @flags:
 *		BIT(0) - for initializing csudma driver and SHA3(Here address
 *		and size inputs can be NULL).
 *		BIT(1) - to call Sha3_Update API which can be called multiple
 *		times when data is not contiguous.
 *		BIT(2) - to get final hash of the whole updated data.
 *		Hash will be overwritten at provided address with
 *		48 bytes.
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_addr = lower_32_bits(address);
	u32 upper_addr = upper_32_bits(address);

	return zynqmp_pm_invoke_fn(PM_SECURE_SHA, NULL, 4, upper_addr, lower_addr, size, flags);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sha_hash);

/**
 * xlnx_get_crypto_dev_data() - Get crypto dev data of platform
 * @feature_map:       List of available feature map of all platform
 *
 * Return: Returns crypto dev data, either address crypto dev or ERR PTR
 */
void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map)
{
	struct xlnx_feature *feature;
	u32 pm_family_code;
	int ret;

	/* Get the Family code and sub family code of platform */
	ret = zynqmp_pm_get_family_info(&pm_family_code);
	if (ret < 0)
		return ERR_PTR(ret);

	feature = feature_map;
	for (; feature->family; feature++) {
		if (feature->family == pm_family_code) {
			ret = zynqmp_pm_feature(feature->feature_id);
			if (ret < 0)
				return ERR_PTR(ret);

			return feature->data;
		}
	}
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(xlnx_get_crypto_dev_data);
