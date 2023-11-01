// SPDX-License-Identifier: GPL-2.0-only
/*
 * virtio block crypto ops QTI implementation.
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/crypto_qti_virt.h>
#include <linux/blk-crypto-profile.h>

/*keyslot manager for vrtual IO*/
static struct blk_crypto_profile virtio_crypto_profile;
/* initialize crypto profile only once */
static bool is_crypto_profile_initalized;
/*To get max ice slots for guest vm */
static uint32_t num_ice_slots;

void virtblk_crypto_qti_crypto_register(struct request_queue *q)
{
	 blk_crypto_register(&virtio_crypto_profile, q);
}
EXPORT_SYMBOL_GPL(virtblk_crypto_qti_crypto_register);

static inline bool virtblk_keyslot_valid(unsigned int slot)
{
	/*
	 * slot numbers range from 0 to max available
	 * slots for vm.
	 */
	return slot < num_ice_slots;
}

static int virtblk_crypto_qti_keyslot_program(struct blk_crypto_profile *profile,
					      const struct blk_crypto_key *key,
					      unsigned int slot)
{
	int err = 0;

	if (!virtblk_keyslot_valid(slot)) {
		pr_err("%s: key slot is not valid\n",
			__func__);
		return -EINVAL;
	}
	err = crypto_qti_virt_program_key(key, slot);
	if (err) {
		pr_err("%s: program key failed with error %d\n",
			__func__, err);
		err = crypto_qti_virt_invalidate_key(slot);
		if (err) {
			pr_err("%s: invalidate key failed with error %d\n",
				__func__, err);
			return err;
		}
	}
	return err;
}

static int virtblk_crypto_qti_keyslot_evict(struct blk_crypto_profile *profile,
					const struct blk_crypto_key *key,
					unsigned int slot)
{
	int err = 0;

	if (!virtblk_keyslot_valid(slot)) {
		pr_err("%s: key slot is not valid\n",
			__func__);
		return -EINVAL;
	}
	err = crypto_qti_virt_invalidate_key(slot);
	if (err) {
		pr_err("%s: evict key failed with error %d\n",
			__func__, err);
		return err;
	}
	return err;
}

static int virtblk_crypto_qti_derive_raw_secret(struct blk_crypto_profile *profile,
						const u8 *eph_key,
						size_t eph_key_size,
						u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	int err = 0;

	if (eph_key_size <= BLK_CRYPTO_SW_SECRET_SIZE) {
		pr_err("%s: Invalid wrapped_key_size: %u\n",
			__func__, eph_key_size);
		err = -EINVAL;
		return err;
	}

	if (eph_key_size > 64) {
		err = crypto_qti_virt_derive_raw_secret_platform(eph_key,
								 eph_key_size,
								 sw_secret,
								 BLK_CRYPTO_SW_SECRET_SIZE);
	} else {
		memcpy(sw_secret, eph_key, BLK_CRYPTO_SW_SECRET_SIZE);
	}
	return err;
}

static const struct blk_crypto_ll_ops virtio_blk_qti_crypto_ops = {
	.keyslot_program        = virtblk_crypto_qti_keyslot_program,
	.keyslot_evict          = virtblk_crypto_qti_keyslot_evict,
	.derive_sw_secret      = virtblk_crypto_qti_derive_raw_secret,
};

int virtblk_init_crypto_qti_spec(struct device *dev)
{
	int err = 0;
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX];

	memset(crypto_modes_supported, 0, sizeof(crypto_modes_supported));

	/* Actual determination of capabilities for UFS/EMMC for different
	 * encryption modes are done in the back end (host operating system)
	 * in case of virtualization driver, so will get crypto capabilities
	 * from the back end. The received capabilities is feeded as input
	 * parameter to keyslot manager
	 */
	err = crypto_qti_virt_get_crypto_capabilities(crypto_modes_supported,
						      sizeof(crypto_modes_supported));
	if (err) {
		pr_err("crypto_qti_virt_get_crypto_capabilities failed error = %d\n", err);
		return err;
	}
	/* Get max number of ice  slots for guest vm */
	err = crypto_qti_virt_ice_get_info(&num_ice_slots);
	if (err) {
		pr_err("crypto_qti_virt_ice_get_info failed error = %d\n", err);
		return err;
	}
	/* Return from here incase keyslot manager is already initialized */
	if (is_crypto_profile_initalized)
		return 0;

	/* create keyslot manager and which will manage the keyslots for all
	 * virtual disks
	 */
	err = devm_blk_crypto_profile_init(dev, &virtio_crypto_profile, num_ice_slots);
	if (err) {
		pr_err("%s: crypto profile initialization failed\n", __func__);
		return err;
	}
	is_crypto_profile_initalized = true;
	virtio_crypto_profile.ll_ops = virtio_blk_qti_crypto_ops;
	/* This value suppose to get from host based on storage type
	 * will remove hard code value later
	 */
	virtio_crypto_profile.max_dun_bytes_supported = 8;
	virtio_crypto_profile.key_types_supported = BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;
	virtio_crypto_profile.dev = dev;
	memcpy(virtio_crypto_profile.modes_supported, crypto_modes_supported,
	       sizeof(crypto_modes_supported));

	pr_info("%s: crypto profile initialized.\n", __func__);

	return err;
}
EXPORT_SYMBOL_GPL(virtblk_init_crypto_qti_spec);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto Virtual library for storage encryption");
