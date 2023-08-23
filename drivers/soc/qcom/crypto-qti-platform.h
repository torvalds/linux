/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CRYPTO_QTI_PLATFORM_H
#define _CRYPTO_QTI_PLATFORM_H

#include <linux/blk-crypto.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/device.h>

#if IS_ENABLED(CONFIG_QTI_CRYPTO_COMMON)
int crypto_qti_program_key(const struct ice_mmio_data *mmio_data,
			   const struct blk_crypto_key *key,
			   unsigned int slot,
			   unsigned int data_unit_mask, int capid, int storage_type);
int crypto_qti_invalidate_key(const struct ice_mmio_data *mmio_data,
			      unsigned int slot, int storage_type);
int crypto_qti_derive_raw_secret_platform(const struct ice_mmio_data *mmio_data,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size);

#if (IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER) || IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1))
void crypto_qti_disable_platform(void);
#else
static inline void crypto_qti_disable_platform(void)
{}
#endif /* CONFIG_QTI_HW_KEY_MANAGER */
#else
static inline int crypto_qti_program_key(
				const struct ice_mmio_data *mmio_data,
				const struct blk_crypto_key *key,
				unsigned int slot,
				unsigned int data_unit_mask, int capid, int storage_type)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_invalidate_key(
		const struct ice_mmio_data *mmio_data, unsigned int slot, int storage_type)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_derive_raw_secret_platform(const struct ice_mmio_data *mmio_data,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	return -EOPNOTSUPP;
}

static inline void crypto_qti_disable_platform(void)
{}
#endif /* CONFIG_QTI_CRYPTO_TZ || CONFIG_QTI_HW_KEY_MANAGER */
#endif /* _CRYPTO_QTI_PLATFORM_H */
