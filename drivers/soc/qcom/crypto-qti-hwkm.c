// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto HWKM library for storage encryption.
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/crypto-qti-common.h>
#include <linux/hwkm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cacheflush.h>
#include <linux/qcom_scm.h>
#include <linux/qtee_shmbridge.h>

#include "crypto-qti-ice-regs.h"
#include "crypto-qti-platform.h"

#define KEYMANAGER_ICE_MAP_SLOT(slot)	((slot * 2))

union crypto_cfg {
	__le32 regval[2];
	struct {
		u8 dusize;
		u8 capidx;
		u8 nop;
		u8 cfge;
		u8 dumb[4];
	};
};

static bool qti_hwkm_init_done;

static void print_key(const struct blk_crypto_key *key,
				unsigned int slot)
{
	int i = 0;

	pr_err("%s: Printing key for slot %d\n", __func__, slot);
	for (i = 0; i < key->size; i++)
		pr_err("key->raw[%d] = 0x%x\n", i, key->raw[i]);
}

static int crypto_qti_program_hwkm_tz(const struct blk_crypto_key *key,
						unsigned int slot)
{
	int err = 0;
	struct qtee_shm shm;

	err = qtee_shmbridge_allocate_shm(key->size, &shm);
	if (err)
		return -ENOMEM;

	memcpy(shm.vaddr, key->raw, key->size);
	qtee_shmbridge_flush_shm_buf(&shm);

	err = qcom_scm_config_set_ice_key(slot, shm.paddr, key->size,
					0, 0, 0);
	if (err) {
		pr_err("%s:SCM call Error for get contents keyblob: 0x%x\n",
				__func__, err);
		print_key(key, slot);
	}

	qtee_shmbridge_inv_shm_buf(&shm);
	qtee_shmbridge_free_shm(&shm);
	return err;
}

int crypto_qti_get_hwkm_raw_secret_tz(
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	int err = 0;
	struct qtee_shm shm_key, shm_secret;

	err = qtee_shmbridge_allocate_shm(wrapped_key_size, &shm_key);
	if (err)
		return -ENOMEM;

	err = qtee_shmbridge_allocate_shm(secret_size, &shm_secret);
	if (err) {
		qtee_shmbridge_free_shm(&shm_key);
		return -ENOMEM;
	}

	memcpy(shm_key.vaddr, wrapped_key, wrapped_key_size);
	qtee_shmbridge_flush_shm_buf(&shm_key);

	memset(shm_secret.vaddr, 0, secret_size);
	qtee_shmbridge_flush_shm_buf(&shm_secret);

	err = qcom_scm_derive_raw_secret(shm_key.paddr, wrapped_key_size,
					shm_secret.paddr, secret_size);
	if (err) {
		pr_err("%s:SCM call error for raw secret: 0x%x\n", __func__, err);
		goto exit;
	}

	qtee_shmbridge_inv_shm_buf(&shm_secret);
	memcpy(secret, shm_secret.vaddr, secret_size);
	qtee_shmbridge_inv_shm_buf(&shm_key);

exit:
	qtee_shmbridge_free_shm(&shm_key);
	qtee_shmbridge_free_shm(&shm_secret);
	return err;
}

static int crypto_qti_hwkm_evict_slot(unsigned int slot)
{
	int err = 0;

	err = qcom_scm_clear_ice_key(slot, 0);
	if (err)
		pr_err("%s:SCM call Error: 0x%x\n", __func__, err);

	return err;
}

int crypto_qti_program_key(const struct ice_mmio_data *mmio_data,
			   const struct blk_crypto_key *key, unsigned int slot,
			   unsigned int data_unit_mask, int capid)
{
	int err = 0;
	union crypto_cfg cfg;

	if ((key->size) <= RAW_SECRET_SIZE) {
		pr_err("%s: Incorrect key size %d\n", __func__, key->size);
		return -EINVAL;
	}

	if (!qti_hwkm_init_done) {
		err = qti_hwkm_init(mmio_data);
		if (err) {
			pr_err("%s: Error with HWKM init %d\n", __func__, err);
			return -EINVAL;
		}
		qti_hwkm_init_done = true;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.dusize = data_unit_mask;
	cfg.capidx = capid;
	cfg.cfge = 0x80;

	ice_writel(mmio_data->ice_base_mmio, 0x0, (ICE_LUT_KEYS_CRYPTOCFG_R_16 +
					ICE_LUT_KEYS_CRYPTOCFG_OFFSET*slot));
	/* Make sure CFGE is cleared */
	wmb();

	/*
	 * Call TZ to get a contents keyblob
	 * TZ unwraps the derivation key, derives a 512 bit XTS key
	 * and wraps it with the TP Key. It then unwraps to the ICE slot.
	 */
	err = crypto_qti_program_hwkm_tz(key, KEYMANAGER_ICE_MAP_SLOT(slot));
	if (err) {
		pr_err("%s: Error programming hwkm keyblob , err = %d\n",
								__func__, err);
		goto exit;
	}

	ice_writel(mmio_data->ice_base_mmio, cfg.regval[0],
		(ICE_LUT_KEYS_CRYPTOCFG_R_16 + ICE_LUT_KEYS_CRYPTOCFG_OFFSET*slot));
	/* Make sure CFGE is enabled before moving forward */
	wmb();

exit:
	return err;
}
EXPORT_SYMBOL(crypto_qti_program_key);

int crypto_qti_invalidate_key(const struct ice_mmio_data *mmio_data,
			      unsigned int slot)
{
	int err = 0;

	if (!qti_hwkm_init_done)
		return 0;

	/* Clear key from ICE keyslot */
	err = crypto_qti_hwkm_evict_slot(KEYMANAGER_ICE_MAP_SLOT(slot));
	if (err)
		pr_err("%s: Error with key clear %d, slot %d\n", __func__, err, slot);

	return err;
}
EXPORT_SYMBOL(crypto_qti_invalidate_key);

void crypto_qti_disable_platform(void)
{
	qti_hwkm_init_done = false;
}
EXPORT_SYMBOL(crypto_qti_disable_platform);

int crypto_qti_derive_raw_secret_platform(
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	int err = 0;

	/*
	 * Call TZ to get a raw secret
	 * TZ unwraps the derivation key, derives a CMAC key
	 * and then another derivation to get 32 bytes of key data.
	 */
	err = crypto_qti_get_hwkm_raw_secret_tz(wrapped_key, wrapped_key_size,
						secret, secret_size);
	if (err) {
		pr_err("%s: Error with getting derived contents keyblob , err = %d\n",
							__func__, err);
	}

	return err;
}
EXPORT_SYMBOL(crypto_qti_derive_raw_secret_platform);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto HWKM library for storage encryption");
