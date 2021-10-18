// SPDX-License-Identifier: GPL-2.0-only
/*
 * CQHCI crypto engine (inline encryption) support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/blk-crypto.h>
#include <linux/blk-crypto-profile.h>
#include <linux/mmc/host.h>

#include "cqhci-crypto.h"

/* Map from blk-crypto modes to CQHCI crypto algorithm IDs and key sizes */
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
cqhci_host_from_crypto_profile(struct blk_crypto_profile *profile)
{
	struct mmc_host *mmc =
		container_of(profile, struct mmc_host, crypto_profile);

	return mmc->cqe_private;
}

static int cqhci_crypto_program_key(struct cqhci_host *cq_host,
				    const union cqhci_crypto_cfg_entry *cfg,
				    int slot)
{
	u32 slot_offset = cq_host->crypto_cfg_register + slot * sizeof(*cfg);
	int i;

	if (cq_host->ops->program_key)
		return cq_host->ops->program_key(cq_host, cfg, slot);

	/* Clear CFGE */
	cqhci_writel(cq_host, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));

	/* Write the key */
	for (i = 0; i < 16; i++) {
		cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[i]),
			     slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[17]),
		     slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Write dword 16, which includes the new value of CFGE */
	cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[16]),
		     slot_offset + 16 * sizeof(cfg->reg_val[0]));
	return 0;
}

static int cqhci_crypto_keyslot_program(struct blk_crypto_profile *profile,
					const struct blk_crypto_key *key,
					unsigned int slot)

{
	struct cqhci_host *cq_host = cqhci_host_from_crypto_profile(profile);
	const union cqhci_crypto_cap_entry *ccap_array =
		cq_host->crypto_cap_array;
	const struct cqhci_crypto_alg_entry *alg =
			&cqhci_crypto_algs[key->crypto_cfg.crypto_mode];
	u8 data_unit_mask = key->crypto_cfg.data_unit_size / 512;
	int i;
	int cap_idx = -1;
	union cqhci_crypto_cfg_entry cfg = {};
	int err;

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

	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable = CQHCI_CRYPTO_CONFIGURATION_ENABLE;

	if (ccap_array[cap_idx].algorithm_id == CQHCI_CRYPTO_ALG_AES_XTS) {
		/* In XTS mode, the blk_crypto_key's size is already doubled */
		memcpy(cfg.crypto_key, key->raw, key->size/2);
		memcpy(cfg.crypto_key + CQHCI_CRYPTO_KEY_MAX_SIZE/2,
		       key->raw + key->size/2, key->size/2);
	} else {
		memcpy(cfg.crypto_key, key->raw, key->size);
	}

	err = cqhci_crypto_program_key(cq_host, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));
	return err;
}

static int cqhci_crypto_clear_keyslot(struct cqhci_host *cq_host, int slot)
{
	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	union cqhci_crypto_cfg_entry cfg = {};

	return cqhci_crypto_program_key(cq_host, &cfg, slot);
}

static int cqhci_crypto_keyslot_evict(struct blk_crypto_profile *profile,
				      const struct blk_crypto_key *key,
				      unsigned int slot)
{
	struct cqhci_host *cq_host = cqhci_host_from_crypto_profile(profile);

	return cqhci_crypto_clear_keyslot(cq_host, slot);
}

/*
 * The keyslot management operations for CQHCI crypto.
 *
 * Note that the block layer ensures that these are never called while the host
 * controller is runtime-suspended.  However, the CQE won't necessarily be
 * "enabled" when these are called, i.e. CQHCI_ENABLE might not be set in the
 * CQHCI_CFG register.  But the hardware allows that.
 */
static const struct blk_crypto_ll_ops cqhci_crypto_ops = {
	.keyslot_program	= cqhci_crypto_keyslot_program,
	.keyslot_evict		= cqhci_crypto_keyslot_evict,
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
 * crypto capability registers, initializing the blk_crypto_profile, clearing
 * all keyslots, and enabling 128-bit task descriptors.
 *
 * Return: 0 if crypto was initialized or isn't supported; whether
 *	   MMC_CAP2_CRYPTO remains set indicates which one of those cases it is.
 *	   Also can return a negative errno value on unexpected error.
 */
int cqhci_crypto_init(struct cqhci_host *cq_host)
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

	profile->ll_ops = cqhci_crypto_ops;
	profile->dev = dev;

	/* Unfortunately, CQHCI crypto only supports 32 DUN bits. */
	profile->max_dun_bytes_supported = 4;

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
		cqhci_crypto_clear_keyslot(cq_host, slot);

	/* CQHCI crypto requires the use of 128-bit task descriptors. */
	cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

	return 0;

out:
	mmc->caps2 &= ~MMC_CAP2_CRYPTO;
	return err;
}
