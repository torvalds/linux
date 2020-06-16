// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include "ufshcd.h"
#include "ufshcd-crypto.h"

/* Blk-crypto modes supported by UFS crypto */
static const struct ufs_crypto_alg_entry {
	enum ufs_crypto_alg ufs_alg;
	enum ufs_crypto_key_size ufs_key_size;
} ufs_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.ufs_alg = UFS_CRYPTO_ALG_AES_XTS,
		.ufs_key_size = UFS_CRYPTO_KEY_SIZE_256,
	},
};

static int ufshcd_program_key(struct ufs_hba *hba,
			      const union ufs_crypto_cfg_entry *cfg,
			      int slot)
{
	int i;
	u32 slot_offset = hba->crypto_cfg_register + slot * sizeof(*cfg);
	int err;

	ufshcd_hold(hba, false);

	if (hba->vops->program_key) {
		err = hba->vops->program_key(hba, cfg, slot);
		goto out;
	}

	/* Ensure that CFGE is cleared before programming the key */
	ufshcd_writel(hba, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));
	for (i = 0; i < 16; i++) {
		ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[i]),
			      slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[17]),
		      slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Dword 16 must be written last */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[16]),
		      slot_offset + 16 * sizeof(cfg->reg_val[0]));
	err = 0;
out:
	ufshcd_release(hba);
	return err;
}

static int ufshcd_crypto_keyslot_program(struct blk_keyslot_manager *ksm,
					 const struct blk_crypto_key *key,
					 unsigned int slot)
{
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);
	const union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;
	const struct ufs_crypto_alg_entry *alg =
			&ufs_crypto_algs[key->crypto_cfg.crypto_mode];
	u8 data_unit_mask = key->crypto_cfg.data_unit_size / 512;
	int i;
	int cap_idx = -1;
	union ufs_crypto_cfg_entry cfg = { 0 };
	int err;

	BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
	for (i = 0; i < hba->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->ufs_alg &&
		    ccap_array[i].key_size == alg->ufs_key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}

	if (WARN_ON(cap_idx < 0))
		return -EOPNOTSUPP;

	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable = UFS_CRYPTO_CONFIGURATION_ENABLE;

	if (ccap_array[cap_idx].algorithm_id == UFS_CRYPTO_ALG_AES_XTS) {
		/* In XTS mode, the blk_crypto_key's size is already doubled */
		memcpy(cfg.crypto_key, key->raw, key->size/2);
		memcpy(cfg.crypto_key + UFS_CRYPTO_KEY_MAX_SIZE/2,
		       key->raw + key->size/2, key->size/2);
	} else {
		memcpy(cfg.crypto_key, key->raw, key->size);
	}

	err = ufshcd_program_key(hba, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));

	return err;
}


static void ufshcd_clear_keyslot(struct ufs_hba *hba, int slot)
{
	union ufs_crypto_cfg_entry cfg = { 0 };
	int err;

	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	err = ufshcd_program_key(hba, &cfg, slot);
	WARN_ON_ONCE(err);
}

static int ufshcd_crypto_keyslot_evict(struct blk_keyslot_manager *ksm,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);

	ufshcd_clear_keyslot(hba, slot);

	return 0;
}

/* Functions implementing UFSHCI v2.1 specification behaviour */
bool ufshcd_crypto_enable_spec(struct ufs_hba *hba)
{
	if (!(hba->caps & UFSHCD_CAP_CRYPTO))
		return false;

	/* Reset might clear all keys, so reprogram all the keys. */
	blk_ksm_reprogram_all_keys(&hba->ksm);
	return true;
}
EXPORT_SYMBOL(ufshcd_crypto_enable_spec);

static const struct blk_ksm_ll_ops ufshcd_ksm_ops = {
	.keyslot_program	= ufshcd_crypto_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_keyslot_evict,
};

static enum blk_crypto_mode_num
ufshcd_find_blk_crypto_mode(union ufs_crypto_cap_entry cap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ufs_crypto_algs); i++) {
		BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
		if (ufs_crypto_algs[i].ufs_alg == cap.algorithm_id &&
		    ufs_crypto_algs[i].ufs_key_size == cap.key_size) {
			return i;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * ufshcd_hba_init_crypto - Read crypto capabilities, init crypto fields in hba
 * @hba: Per adapter instance
 *
 * Return: 0 if crypto was initialized or is not supported, else a -errno value.
 */
int ufshcd_hba_init_crypto_spec(struct ufs_hba *hba,
				const struct blk_ksm_ll_ops *ksm_ops)
{
	int cap_idx = 0;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;
	int slot = 0;
	int num_keyslots;

	/*
	 * Don't use crypto if either the hardware doesn't advertise the
	 * standard crypto capability bit *or* if the vendor specific driver
	 * hasn't advertised that crypto is supported.
	 */
	if (!(hba->capabilities & MASK_CRYPTO_SUPPORT) ||
	    !(hba->caps & UFSHCD_CAP_CRYPTO))
		goto out;

	hba->crypto_capabilities.reg_val =
			cpu_to_le32(ufshcd_readl(hba, REG_UFS_CCAP));
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		devm_kcalloc(hba->dev, hba->crypto_capabilities.num_crypto_cap,
			     sizeof(hba->crypto_cap_array[0]), GFP_KERNEL);
	if (!hba->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	/* The actual number of configurations supported is (CFGC+1) */
	num_keyslots = hba->crypto_capabilities.config_count + 1;
	err = blk_ksm_init(&hba->ksm, num_keyslots);
	if (err)
		goto out_free_caps;

	hba->ksm.ksm_ll_ops = *ksm_ops;
	/* UFS only supports 8 bytes for any DUN */
	hba->ksm.max_dun_bytes_supported = 8;
	hba->ksm.features = BLK_CRYPTO_FEATURE_STANDARD_KEYS;
	hba->ksm.dev = hba->dev;

	/*
	 * Cache all the UFS crypto capabilities and advertise the supported
	 * crypto modes and data unit sizes to the block layer.
	 */
	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		hba->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(ufshcd_readl(hba,
						 REG_UFS_CRYPTOCAP +
						 cap_idx * sizeof(__le32)));
		blk_mode_num = ufshcd_find_blk_crypto_mode(
						hba->crypto_cap_array[cap_idx]);
		if (blk_mode_num != BLK_ENCRYPTION_MODE_INVALID)
			hba->ksm.crypto_modes_supported[blk_mode_num] |=
				hba->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	for (slot = 0; slot < num_keyslots; slot++)
		ufshcd_clear_keyslot(hba, slot);

	return 0;

out_free_caps:
	devm_kfree(hba->dev, hba->crypto_cap_array);
out:
	/* Indicate that init failed by clearing UFSHCD_CAP_CRYPTO */
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
	return err;
}
EXPORT_SYMBOL(ufshcd_hba_init_crypto_spec);

void ufshcd_crypto_setup_rq_keyslot_manager_spec(struct ufs_hba *hba,
						 struct request_queue *q)
{
	if (hba->caps & UFSHCD_CAP_CRYPTO)
		blk_ksm_register(&hba->ksm, q);
}
EXPORT_SYMBOL(ufshcd_crypto_setup_rq_keyslot_manager_spec);

void ufshcd_crypto_destroy_keyslot_manager_spec(struct ufs_hba *hba)
{
	blk_ksm_destroy(&hba->ksm);
}
EXPORT_SYMBOL(ufshcd_crypto_destroy_keyslot_manager_spec);

/* Crypto Variant Ops Support */

bool ufshcd_crypto_enable(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->enable)
		return hba->crypto_vops->enable(hba);

	return ufshcd_crypto_enable_spec(hba);
}

int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->hba_init_crypto)
		return hba->crypto_vops->hba_init_crypto(hba,
							 &ufshcd_ksm_ops);

	return ufshcd_hba_init_crypto_spec(hba, &ufshcd_ksm_ops);
}

void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					    struct request_queue *q)
{
	if (hba->crypto_vops && hba->crypto_vops->setup_rq_keyslot_manager) {
		hba->crypto_vops->setup_rq_keyslot_manager(hba, q);
		return;
	}

	ufshcd_crypto_setup_rq_keyslot_manager_spec(hba, q);
}

void ufshcd_crypto_destroy_keyslot_manager(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->destroy_keyslot_manager) {
		hba->crypto_vops->destroy_keyslot_manager(hba);
		return;
	}

	ufshcd_crypto_destroy_keyslot_manager_spec(hba);
}

void ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->prepare_lrbp_crypto) {
		hba->crypto_vops->prepare_lrbp_crypto(hba, cmd, lrbp);
		return;
	}

	ufshcd_prepare_lrbp_crypto_spec(hba, cmd, lrbp);
}

int ufshcd_map_sg_crypto(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->map_sg_crypto)
		return hba->crypto_vops->map_sg_crypto(hba, lrbp);

	return 0;
}

int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->complete_lrbp_crypto)
		return hba->crypto_vops->complete_lrbp_crypto(hba, cmd, lrbp);

	return 0;
}

void ufshcd_crypto_debug(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->debug)
		hba->crypto_vops->debug(hba);
}

int ufshcd_crypto_suspend(struct ufs_hba *hba,
			  enum ufs_pm_op pm_op)
{
	if (hba->crypto_vops && hba->crypto_vops->suspend)
		return hba->crypto_vops->suspend(hba, pm_op);

	return 0;
}

int ufshcd_crypto_resume(struct ufs_hba *hba,
			 enum ufs_pm_op pm_op)
{
	if (hba->crypto_vops && hba->crypto_vops->resume)
		return hba->crypto_vops->resume(hba, pm_op);

	return 0;
}

void ufshcd_crypto_set_vops(struct ufs_hba *hba,
			    struct ufs_hba_crypto_variant_ops *crypto_vops)
{
	hba->crypto_vops = crypto_vops;
}
