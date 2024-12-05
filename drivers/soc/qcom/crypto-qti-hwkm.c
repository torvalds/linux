// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto HWKM library for storage encryption.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "hwkmregs.h"

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
#define KEYMANAGER_ICE_MAP_SLOT(slot, offset)	((slot * 2) + offset)
#endif

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
#define KEYMANAGER_ICE_MAP_SLOT(slot, offset)	((slot * 2) + 10)
#define GP_KEYSLOT			140
#define RAW_SECRET_KEYSLOT		141
#define TPKEY_SLOT_ICEMEM_SLAVE		0x92
#define SLOT_EMPTY_ERROR		0x1000
#define INLINECRYPT_CTX			"inline encryption key"
#define RAW_SECRET_CTX			"raw secret"
#define BYTE_ORDER_VAL			8
#define KEY_WRAPPED_SIZE		68
#endif

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
static int offset;

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

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
static int crypto_qti_hwkm_evict_slot_v1(unsigned int slot, bool double_key)
{
	struct hwkm_cmd cmd_clear;
	struct hwkm_rsp rsp_clear;

	memset(&cmd_clear, 0, sizeof(cmd_clear));
	cmd_clear.op = KEY_SLOT_CLEAR;
	cmd_clear.clear.dks = slot;
	if (double_key)
		cmd_clear.clear.is_double_key = true;

	return qti_hwkm_handle_cmd(&cmd_clear, &rsp_clear);
}
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
static int crypto_qti_hwkm_evict_slot(unsigned int slot)
{
	int err = 0;

	err = qcom_scm_clear_ice_key(slot, 0);
	if (err)
		pr_err("%s:SCM call Error: 0x%x\n", __func__, err);

	return err;
}
#endif

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
static int crypto_qti_program_key_v1(const struct ice_mmio_data *mmio_data,
			   const struct blk_crypto_key *key, unsigned int slot,
			   unsigned int data_unit_mask, int capid)
{
	int err_program;
	int err_clear;
	struct hwkm_cmd cmd_unwrap;
	struct hwkm_cmd cmd_kdf;
	struct hwkm_rsp rsp_unwrap;
	struct hwkm_rsp rsp_kdf;

	struct hwkm_key_policy policy_kdf = {
		.security_lvl = MANAGED_KEY,
		.hw_destination = ICEMEM_SLAVE,
		.key_type = GENERIC_KEY,
		.enc_allowed = true,
		.dec_allowed = true,
		.alg_allowed = AES256_XTS,
		.km_by_nsec_allowed = true,
	};
	struct hwkm_bsve bsve_kdf = {
		.enabled = true,
		.km_swc_en = true,
		.km_child_key_policy_en = true,
	};
	union crypto_cfg cfg;

	err_program = qti_hwkm_clocks(true);
	if (err_program) {
		pr_err("%s: Error enabling clocks %d\n", __func__,
							err_program);
		return err_program;
	}

	/* Failsafe, clear GP_KEYSLOT incase it is not empty for any reason */
	err_clear = crypto_qti_hwkm_evict_slot_v1(GP_KEYSLOT, false);
	if (err_clear && (err_clear != SLOT_EMPTY_ERROR)) {
		pr_err("%s: Error clearing ICE slot %d, err %d\n",
			__func__, GP_KEYSLOT, err_clear);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Unwrap keyblob into a non ICE slot using TP key */
	cmd_unwrap.op = KEY_UNWRAP_IMPORT;
	cmd_unwrap.unwrap.dks = GP_KEYSLOT;
	cmd_unwrap.unwrap.kwk = TPKEY_SLOT_ICEMEM_SLAVE;
	if ((key->size) == KEY_WRAPPED_SIZE) {
		cmd_unwrap.unwrap.sz = key->size;
		memcpy(cmd_unwrap.unwrap.wkb, key->raw,
				cmd_unwrap.unwrap.sz);
	} else {
		cmd_unwrap.unwrap.sz = (key->size) - RAW_SECRET_SIZE;
		memcpy(cmd_unwrap.unwrap.wkb, (key->raw) + RAW_SECRET_SIZE,
				cmd_unwrap.unwrap.sz);
	}

	err_program = qti_hwkm_handle_cmd(&cmd_unwrap, &rsp_unwrap);
	if (err_program) {
		pr_err("%s: Error with key unwrap %d\n", __func__,
							err_program);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Failsafe, clear ICE keyslot incase it is not empty for any reason */
	err_clear = crypto_qti_hwkm_evict_slot_v1(KEYMANAGER_ICE_MAP_SLOT(slot, offset),
						true);
	if (err_clear && (err_clear != SLOT_EMPTY_ERROR)) {
		pr_err("%s: Error clearing ICE slot %d, err %d\n",
			__func__, KEYMANAGER_ICE_MAP_SLOT(slot, offset), err_clear);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Derive a 512-bit key which will be the key to encrypt/decrypt data */
	cmd_kdf.op = SYSTEM_KDF;
	cmd_kdf.kdf.dks = KEYMANAGER_ICE_MAP_SLOT(slot, offset);
	cmd_kdf.kdf.kdk = GP_KEYSLOT;
	cmd_kdf.kdf.policy = policy_kdf;
	cmd_kdf.kdf.bsve = bsve_kdf;
	cmd_kdf.kdf.sz = round_up(strlen(INLINECRYPT_CTX), BYTE_ORDER_VAL);
	memset(cmd_kdf.kdf.ctx, 0, HWKM_MAX_CTX_SIZE);
	memcpy(cmd_kdf.kdf.ctx, INLINECRYPT_CTX, strlen(INLINECRYPT_CTX));

	memset(&cfg, 0, sizeof(cfg));
	cfg.dusize = data_unit_mask;
	cfg.capidx = capid;
	cfg.cfge = 0x80;

	ice_writel(mmio_data->ice_base_mmio, 0x0, (ICE_LUT_KEYS_CRYPTOCFG_R_16 +
					ICE_LUT_KEYS_CRYPTOCFG_OFFSET*slot));
	/* Make sure CFGE is cleared */
	wmb();

	err_program = qti_hwkm_handle_cmd(&cmd_kdf, &rsp_kdf);
	if (err_program) {
		pr_err("%s: Error programming key %d, slot %d\n", __func__,
						err_program, slot);
		err_clear = crypto_qti_hwkm_evict_slot_v1(GP_KEYSLOT, false);
		if (err_clear) {
			pr_err("%s: Error clearing slot %d err %d\n",
					__func__, GP_KEYSLOT, err_clear);
		}
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	err_clear = crypto_qti_hwkm_evict_slot_v1(GP_KEYSLOT, false);
	if (err_clear) {
		pr_err("%s: Error unwrapped slot clear %d\n", __func__,
							err_clear);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	ice_writel(mmio_data->ice_base_mmio, cfg.regval[0], (ICE_LUT_KEYS_CRYPTOCFG_R_16 +
					ICE_LUT_KEYS_CRYPTOCFG_OFFSET*slot));
	/* Make sure CFGE is enabled before moving forward */
	wmb();

	qti_hwkm_clocks(false);

	return err_program;
}
#endif


int crypto_qti_program_key(const struct ice_mmio_data *mmio_data,
			   const struct blk_crypto_key *key, unsigned int slot,
			   unsigned int data_unit_mask, int capid, int storage_type)
{
	int err = 0;
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	int minor_version = 0, major_version = 0;
#endif

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
		offset = 0;
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
		minor_version = qti_hwkm_get_reg_data(mmio_data->ice_hwkm_mmio,
						QTI_HWKM_ICE_RG_IPCAT_VERSION,
						HWKM_VERSION_MINOR_REV,
						HWKM_VERSION_MINOR_REV_MASK, ICE_SLAVE);

		major_version = qti_hwkm_get_reg_data(mmio_data->ice_hwkm_mmio,
						QTI_HWKM_ICE_RG_IPCAT_VERSION,
						HWKM_VERSION_MAJOR_REV,
						HWKM_VERSION_MAJOR_REV_MASK, ICE_SLAVE);

		pr_debug("HWKM minor version is %d.\n", minor_version);
		pr_debug("HWKM major version is %d.\n", major_version);
		if (major_version == 1)
			offset = 10;
		else if (major_version == 2)
			offset = (minor_version < 1) ? 10 : 0;
#endif
	}

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
	/* Call to support Program Key for HWKM v1 */
	return crypto_qti_program_key_v1(mmio_data, key, slot, data_unit_mask, capid);
#endif

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
	err = crypto_qti_program_hwkm_tz(key, KEYMANAGER_ICE_MAP_SLOT(slot, offset));
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
			      unsigned int slot, int storage_type)
{
	int err = 0;

	if (!qti_hwkm_init_done)
		return 0;

	/* Clear key from ICE keyslot */
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	err = crypto_qti_hwkm_evict_slot(KEYMANAGER_ICE_MAP_SLOT(slot, offset));
	if (err)
		pr_err("%s: Error with key clear %d, slot %d\n", __func__, err, slot);
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
	err = qti_hwkm_clocks(true);
	if (err) {
		pr_err("%s: Error enabling clocks %d\n", __func__, err);
		return err;
	}
	err = crypto_qti_hwkm_evict_slot_v1(KEYMANAGER_ICE_MAP_SLOT(slot, offset), true);
	if (err && (err != SLOT_EMPTY_ERROR)) {
		pr_err("%s: Error clearing slot %d, err %d\n",
			__func__, slot, err);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}
	qti_hwkm_clocks(false);
#endif
	return err;
}
EXPORT_SYMBOL(crypto_qti_invalidate_key);

void crypto_qti_disable_platform(void)
{
	qti_hwkm_init_done = false;
}
EXPORT_SYMBOL(crypto_qti_disable_platform);

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
static int crypto_qti_derive_raw_secret_platform_v1(
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	int err_program;
	int err_clear;
	struct hwkm_cmd cmd_unwrap;
	struct hwkm_cmd cmd_kdf;
	struct hwkm_cmd cmd_read;
	struct hwkm_rsp rsp_unwrap;
	struct hwkm_rsp rsp_kdf;
	struct hwkm_rsp rsp_read;

	struct hwkm_key_policy policy_kdf = {
		.security_lvl = SW_KEY,
		.hw_destination = ICEMEM_SLAVE,
		.key_type = GENERIC_KEY,
		.enc_allowed = true,
		.dec_allowed = true,
		.alg_allowed = AES256_CBC,
		.km_by_nsec_allowed = true,
	};
	struct hwkm_bsve bsve_kdf = {
		.enabled = true,
		.km_swc_en = true,
		.km_child_key_policy_en = true,
	};

	if (wrapped_key_size != KEY_WRAPPED_SIZE) {
		memcpy(secret, wrapped_key, secret_size);
		return 0;
	}

	err_program = qti_hwkm_clocks(true);
	if (err_program) {
		pr_err("%s: Error enabling clocks %d\n", __func__,
							err_program);
		return err_program;
	}

	/* Failsafe, clear GP_KEYSLOT incase it is not empty for any reason */
	err_clear = crypto_qti_hwkm_evict_slot_v1(GP_KEYSLOT, false);
	if (err_clear && (err_clear != SLOT_EMPTY_ERROR)) {
		pr_err("%s: Error clearing GP slot %d, err %d\n",
			__func__, GP_KEYSLOT, err_clear);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Unwrap keyblob into a non ICE slot using TP key */
	cmd_unwrap.op = KEY_UNWRAP_IMPORT;
	cmd_unwrap.unwrap.dks = GP_KEYSLOT;
	cmd_unwrap.unwrap.kwk = TPKEY_SLOT_ICEMEM_SLAVE;
	cmd_unwrap.unwrap.sz = wrapped_key_size;
	memcpy(cmd_unwrap.unwrap.wkb, wrapped_key,
			cmd_unwrap.unwrap.sz);

	err_program = qti_hwkm_handle_cmd(&cmd_unwrap, &rsp_unwrap);
	if (err_program) {
		pr_err("%s: Error with key unwrap %d\n", __func__,
							err_program);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Failsafe, clear RAW_SECRET_KEYSLOT incase it is not empty */
	err_clear = crypto_qti_hwkm_evict_slot_v1(RAW_SECRET_KEYSLOT, false);
	if (err_clear && (err_clear != SLOT_EMPTY_ERROR)) {
		pr_err("%s: Error clearing raw secret slot %d, err %d\n",
			__func__, RAW_SECRET_KEYSLOT, err_clear);
		qti_hwkm_clocks(false);
		return -EINVAL;
	}

	/* Derive a 512-bit key which will be the key to encrypt/decrypt data */
	cmd_kdf.op = SYSTEM_KDF;
	cmd_kdf.kdf.dks = RAW_SECRET_KEYSLOT;
	cmd_kdf.kdf.kdk = GP_KEYSLOT;
	cmd_kdf.kdf.policy = policy_kdf;
	cmd_kdf.kdf.bsve = bsve_kdf;
	cmd_kdf.kdf.sz = round_up(strlen(RAW_SECRET_CTX), BYTE_ORDER_VAL);
	memset(cmd_kdf.kdf.ctx, 0, HWKM_MAX_CTX_SIZE);
	memcpy(cmd_kdf.kdf.ctx, RAW_SECRET_CTX, strlen(RAW_SECRET_CTX));

	err_program = qti_hwkm_handle_cmd(&cmd_kdf, &rsp_kdf);
	if (err_program) {
		pr_err("%s: Error deriving secret %d, slot %d\n", __func__,
					err_program, RAW_SECRET_KEYSLOT);
		err_program = -EINVAL;
	}

	/* Read the KDF key for raw secret */
	cmd_read.op = KEY_SLOT_RDWR;
	cmd_read.rdwr.slot = RAW_SECRET_KEYSLOT;
	cmd_read.rdwr.is_write = false;
	err_program = qti_hwkm_handle_cmd(&cmd_read, &rsp_read);
	if (err_program) {
		pr_err("%s: Error with key read %d\n", __func__, err_program);
		err_program = -EINVAL;
	}
	memcpy(secret, rsp_read.rdwr.key, rsp_read.rdwr.sz);

	err_clear = crypto_qti_hwkm_evict_slot_v1(GP_KEYSLOT, false);
	if (err_clear)
		pr_err("%s: GP slot clear %d\n", __func__, err_clear);
	err_clear = crypto_qti_hwkm_evict_slot_v1(RAW_SECRET_KEYSLOT, false);
	if (err_clear)
		pr_err("%s: raw secret slot clear %d\n", __func__, err_clear);

	qti_hwkm_clocks(false);
	return err_program;
}
#endif

int crypto_qti_derive_raw_secret_platform(const struct ice_mmio_data *mmio_data,
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	int err = 0;

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
	return crypto_qti_derive_raw_secret_platform_v1(wrapped_key,
		wrapped_key_size, secret, secret_size);
#endif
	if (!qti_hwkm_init_done) {
		err = qti_hwkm_init(mmio_data);
		if (err) {
			pr_err("%s: Error with HWKM init %d\n", __func__, err);
			return -EINVAL;
		}
		qti_hwkm_init_done = true;
	}

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
