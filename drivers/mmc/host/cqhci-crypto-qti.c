// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <crypto/algapi.h>
#include "sdhci.h"
#include "sdhci-pltfm.h"
#include "cqhci-crypto-qti.h"
#include <linux/blk-crypto-profile.h>
#include <linux/crypto-qti-common.h>

#define RAW_SECRET_SIZE 32
#define MINIMUM_DUN_SIZE 512
#define MAXIMUM_DUN_SIZE 65536

static const struct cqhci_crypto_alg_entry {
	enum cqhci_crypto_alg alg;
	enum cqhci_crypto_key_size key_size;
} cqhci_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.alg = CQHCI_CRYPTO_ALG_AES_XTS,
		.key_size = CQHCI_CRYPTO_KEY_SIZE_256,
	},
};

static inline struct cqhci_host *
cqhci_host_from_crypto(struct blk_crypto_profile *profile)
{
	struct mmc_host *mmc = container_of(profile, struct mmc_host, crypto_profile);

	return mmc->cqe_private;
}

static void get_mmio_data(struct ice_mmio_data *data, struct cqhci_host *host)
{
	data->ice_base_mmio = host->ice_mmio;
#if (IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER) || IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1))
	data->ice_hwkm_mmio = host->ice_hwkm_mmio;
#endif
}

static int cqhci_crypto_qti_keyslot_program(struct blk_crypto_profile *profile,
					    const struct blk_crypto_key *key,
					    unsigned int slot)
{
	struct cqhci_host *cq_host = cqhci_host_from_crypto(profile);
	int err = 0;
	u8 data_unit_mask = -1;
	struct ice_mmio_data mmio_data;
	const struct cqhci_crypto_alg_entry *alg;
	int i;
	int cap_idx = -1;

	const union cqhci_crypto_cap_entry *ccap_array =
		cq_host->crypto_cap_array;

	if (!key) {
		pr_err("Invalid/no key present\n");
		return -EINVAL;
	}

	alg = &cqhci_crypto_algs[key->crypto_cfg.crypto_mode];
	data_unit_mask = key->crypto_cfg.data_unit_size / MINIMUM_DUN_SIZE;

	BUILD_BUG_ON(CQHCI_CRYPTO_KEY_SIZE_INVALID != 0);
	for (i = 0; i < cq_host->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->alg &&
		    ccap_array[i].key_size == alg->key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}
	if (WARN_ON(cap_idx < 0))
		return -EOPNOTSUPP;

	get_mmio_data(&mmio_data, cq_host);

	err = crypto_qti_keyslot_program(&mmio_data, key,
					 slot, data_unit_mask, cap_idx, SDCC_CE);
	if (err)
		pr_err("%s: failed with error %d\n", __func__, err);

	return err;
}

static int cqhci_crypto_qti_keyslot_evict(struct blk_crypto_profile *profile,
					  const struct blk_crypto_key *key,
					  unsigned int slot)
{
	int err = 0;
	struct cqhci_host *host = cqhci_host_from_crypto(profile);
	struct ice_mmio_data mmio_data;

	get_mmio_data(&mmio_data, host);

	err = crypto_qti_keyslot_evict(&mmio_data, slot, SDCC_CE);
	if (err)
		pr_err("%s: failed with error %d\n", __func__, err);

	return err;
}

static int cqhci_crypto_qti_derive_raw_secret(struct blk_crypto_profile *profile,
		const u8 *wrapped_key, size_t wrapped_key_size,
		u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	int err = 0;
	struct cqhci_host *host = cqhci_host_from_crypto(profile);
	struct ice_mmio_data mmio_data;

	get_mmio_data(&mmio_data, host);

	err = crypto_qti_derive_raw_secret(&mmio_data, wrapped_key, wrapped_key_size,
					  sw_secret, BLK_CRYPTO_SW_SECRET_SIZE);
	if (err)
		pr_err("%s: failed with error %d\n", __func__, err);

	return err;
}

static const struct blk_crypto_ll_ops cqhci_crypto_qti_ops = {
	.keyslot_program	= cqhci_crypto_qti_keyslot_program,
	.keyslot_evict		= cqhci_crypto_qti_keyslot_evict,
	.derive_sw_secret	= cqhci_crypto_qti_derive_raw_secret
};

static enum blk_crypto_mode_num
cqhci_find_blk_crypto_mode(union cqhci_crypto_cap_entry cap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cqhci_crypto_algs); i++) {
		BUILD_BUG_ON(CQHCI_CRYPTO_KEY_SIZE_INVALID != 0);
		if (cqhci_crypto_algs[i].alg == cap.algorithm_id &&
		    cqhci_crypto_algs[i].key_size == cap.key_size)
			return i;
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * cqhci_crypto_init - initialize CQHCI crypto support
 * @cq_host: a cqhci host
 *
 * If the driver previously set MMC_CAP2_CRYPTO and the CQE declares
 * CQHCI_CAP_CS, initialize the crypto support.  This involves reading the
 * crypto capability registers, initializing the keyslot manager, clearing all
 * keyslots, and enabling 128-bit task descriptors.
 *
 * Return: 0 if crypto was initialized or isn't supported; whether
 *	   MMC_CAP2_CRYPTO remains set indicates which one of those cases it is.
 *	   Also can return a negative errno value on unexpected error.
 */
int cqhci_qti_crypto_init(struct cqhci_host *cq_host)
{
	struct mmc_host *mmc = cq_host->mmc;
	struct device *dev = mmc_dev(mmc);
	struct blk_crypto_profile *profile = &mmc->crypto_profile;
	unsigned int num_keyslots;
	unsigned int cap_idx;
	enum blk_crypto_mode_num blk_mode_num;
	unsigned int slot;
	int err = 0;

	if (!(mmc->caps2 & MMC_CAP2_CRYPTO) ||
	    !(cqhci_readl(cq_host, CQHCI_CAP) & CQHCI_CAP_CS))
		goto out;

	cq_host->crypto_capabilities.reg_val =
			cpu_to_le32(cqhci_readl(cq_host, CQHCI_CCAP));

	cq_host->crypto_cfg_register =
		(u32)cq_host->crypto_capabilities.config_array_ptr * 0x100;

	cq_host->crypto_cap_array =
		devm_kcalloc(dev, cq_host->crypto_capabilities.num_crypto_cap,
			     sizeof(cq_host->crypto_cap_array[0]), GFP_KERNEL);
	if (!cq_host->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * CCAP.CFGC is off by one, so the actual number of crypto
	 * configurations (a.k.a. keyslots) is CCAP.CFGC + 1.
	 */
	num_keyslots = cq_host->crypto_capabilities.config_count + 1;

	err = devm_blk_crypto_profile_init(dev, profile, num_keyslots);
	if (err)
		goto out;

	profile->ll_ops = cqhci_crypto_qti_ops;
	profile->dev = dev;

	/* Unfortunately, CQHCI crypto only supports 32 DUN bits. */
	profile->max_dun_bytes_supported = 4;

	profile->key_types_supported = BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;

	/*
	 * Cache all the crypto capabilities and advertise the supported crypto
	 * modes and data unit sizes to the block layer.
	 */
	for (cap_idx = 0; cap_idx < cq_host->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		cq_host->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(cqhci_readl(cq_host,
						CQHCI_CRYPTOCAP +
						cap_idx * sizeof(__le32)));
		blk_mode_num = cqhci_find_blk_crypto_mode(
					cq_host->crypto_cap_array[cap_idx]);
		if (blk_mode_num == BLK_ENCRYPTION_MODE_INVALID)
			continue;
		profile->modes_supported[blk_mode_num] |=
			cq_host->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	/* Clear all the keyslots so that we start in a known state. */
	for (slot = 0; slot < num_keyslots; slot++)
		profile->ll_ops.keyslot_evict(profile, NULL, slot);

	/* CQHCI crypto requires the use of 128-bit task descriptors. */
	cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

	return 0;

out:
	mmc->caps2 &= ~MMC_CAP2_CRYPTO;
	return err;
}

MODULE_DESCRIPTION("Vendor specific CQHCI Crypto Engine Support");
MODULE_LICENSE("GPL");
