// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/**
 * DOC: The Keyslot Manager
 *
 * Many devices with inline encryption support have a limited number of "slots"
 * into which encryption contexts may be programmed, and requests can be tagged
 * with a slot number to specify the key to use for en/decryption.
 *
 * As the number of slots is limited, and programming keys is expensive on
 * many inline encryption hardware, we don't want to program the same key into
 * multiple slots - if multiple requests are using the same key, we want to
 * program just one slot with that key and use that slot for all requests.
 *
 * The keyslot manager manages these keyslots appropriately, and also acts as
 * an abstraction between the inline encryption hardware and the upper layers.
 *
 * Lower layer devices will set up a keyslot manager in their request queue
 * and tell it how to perform device specific operations like programming/
 * evicting keys from keyslots.
 *
 * Upper layers will call blk_ksm_get_slot_for_key() to program a
 * key into some slot in the inline encryption hardware.
 */

#define pr_fmt(fmt) "blk-crypto: " fmt

#include <linux/keyslot-manager.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/wait.h>
#include <linux/blkdev.h>

struct blk_ksm_keyslot {
	atomic_t slot_refs;
	struct list_head idle_slot_node;
	struct hlist_node hash_node;
	const struct blk_crypto_key *key;
	struct blk_keyslot_manager *ksm;
};

static inline void blk_ksm_hw_enter(struct blk_keyslot_manager *ksm)
{
	/*
	 * Calling into the driver requires ksm->lock held and the device
	 * resumed.  But we must resume the device first, since that can acquire
	 * and release ksm->lock via blk_ksm_reprogram_all_keys().
	 */
	if (ksm->dev)
		pm_runtime_get_sync(ksm->dev);
	down_write(&ksm->lock);
}

static inline void blk_ksm_hw_exit(struct blk_keyslot_manager *ksm)
{
	up_write(&ksm->lock);
	if (ksm->dev)
		pm_runtime_put_sync(ksm->dev);
}

static inline bool blk_ksm_is_passthrough(struct blk_keyslot_manager *ksm)
{
	return ksm->num_slots == 0;
}

/**
 * blk_ksm_init() - Initialize a keyslot manager
 * @ksm: The keyslot_manager to initialize.
 * @num_slots: The number of key slots to manage.
 *
 * Allocate memory for keyslots and initialize a keyslot manager. Called by
 * e.g. storage drivers to set up a keyslot manager in their request_queue.
 *
 * Return: 0 on success, or else a negative error code.
 */
int blk_ksm_init(struct blk_keyslot_manager *ksm, unsigned int num_slots)
{
	unsigned int slot;
	unsigned int i;
	unsigned int slot_hashtable_size;

	memset(ksm, 0, sizeof(*ksm));

	if (num_slots == 0)
		return -EINVAL;

	ksm->slots = kvcalloc(num_slots, sizeof(ksm->slots[0]), GFP_KERNEL);
	if (!ksm->slots)
		return -ENOMEM;

	ksm->num_slots = num_slots;

	init_rwsem(&ksm->lock);

	init_waitqueue_head(&ksm->idle_slots_wait_queue);
	INIT_LIST_HEAD(&ksm->idle_slots);

	for (slot = 0; slot < num_slots; slot++) {
		ksm->slots[slot].ksm = ksm;
		list_add_tail(&ksm->slots[slot].idle_slot_node,
			      &ksm->idle_slots);
	}

	spin_lock_init(&ksm->idle_slots_lock);

	slot_hashtable_size = roundup_pow_of_two(num_slots);
	/*
	 * hash_ptr() assumes bits != 0, so ensure the hash table has at least 2
	 * buckets.  This only makes a difference when there is only 1 keyslot.
	 */
	if (slot_hashtable_size < 2)
		slot_hashtable_size = 2;

	ksm->log_slot_ht_size = ilog2(slot_hashtable_size);
	ksm->slot_hashtable = kvmalloc_array(slot_hashtable_size,
					     sizeof(ksm->slot_hashtable[0]),
					     GFP_KERNEL);
	if (!ksm->slot_hashtable)
		goto err_destroy_ksm;
	for (i = 0; i < slot_hashtable_size; i++)
		INIT_HLIST_HEAD(&ksm->slot_hashtable[i]);

	return 0;

err_destroy_ksm:
	blk_ksm_destroy(ksm);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(blk_ksm_init);

static void blk_ksm_destroy_callback(void *ksm)
{
	blk_ksm_destroy(ksm);
}

/**
 * devm_blk_ksm_init() - Resource-managed blk_ksm_init()
 * @dev: The device which owns the blk_keyslot_manager.
 * @ksm: The blk_keyslot_manager to initialize.
 * @num_slots: The number of key slots to manage.
 *
 * Like blk_ksm_init(), but causes blk_ksm_destroy() to be called automatically
 * on driver detach.
 *
 * Return: 0 on success, or else a negative error code.
 */
int devm_blk_ksm_init(struct device *dev, struct blk_keyslot_manager *ksm,
		      unsigned int num_slots)
{
	int err = blk_ksm_init(ksm, num_slots);

	if (err)
		return err;

	return devm_add_action_or_reset(dev, blk_ksm_destroy_callback, ksm);
}
EXPORT_SYMBOL_GPL(devm_blk_ksm_init);

static inline struct hlist_head *
blk_ksm_hash_bucket_for_key(struct blk_keyslot_manager *ksm,
			    const struct blk_crypto_key *key)
{
	return &ksm->slot_hashtable[hash_ptr(key, ksm->log_slot_ht_size)];
}

static void blk_ksm_remove_slot_from_lru_list(struct blk_ksm_keyslot *slot)
{
	struct blk_keyslot_manager *ksm = slot->ksm;
	unsigned long flags;

	spin_lock_irqsave(&ksm->idle_slots_lock, flags);
	list_del(&slot->idle_slot_node);
	spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);
}

static struct blk_ksm_keyslot *blk_ksm_find_keyslot(
					struct blk_keyslot_manager *ksm,
					const struct blk_crypto_key *key)
{
	const struct hlist_head *head = blk_ksm_hash_bucket_for_key(ksm, key);
	struct blk_ksm_keyslot *slotp;

	hlist_for_each_entry(slotp, head, hash_node) {
		if (slotp->key == key)
			return slotp;
	}
	return NULL;
}

static struct blk_ksm_keyslot *blk_ksm_find_and_grab_keyslot(
					struct blk_keyslot_manager *ksm,
					const struct blk_crypto_key *key)
{
	struct blk_ksm_keyslot *slot;

	slot = blk_ksm_find_keyslot(ksm, key);
	if (!slot)
		return NULL;
	if (atomic_inc_return(&slot->slot_refs) == 1) {
		/* Took first reference to this slot; remove it from LRU list */
		blk_ksm_remove_slot_from_lru_list(slot);
	}
	return slot;
}

unsigned int blk_ksm_get_slot_idx(struct blk_ksm_keyslot *slot)
{
	return slot - slot->ksm->slots;
}
EXPORT_SYMBOL_GPL(blk_ksm_get_slot_idx);

/**
 * blk_ksm_get_slot_for_key() - Program a key into a keyslot.
 * @ksm: The keyslot manager to program the key into.
 * @key: Pointer to the key object to program, including the raw key, crypto
 *	 mode, and data unit size.
 * @slot_ptr: A pointer to return the pointer of the allocated keyslot.
 *
 * Get a keyslot that's been programmed with the specified key.  If one already
 * exists, return it with incremented refcount.  Otherwise, wait for a keyslot
 * to become idle and program it.
 *
 * Context: Process context. Takes and releases ksm->lock.
 * Return: BLK_STS_OK on success (and keyslot is set to the pointer of the
 *	   allocated keyslot), or some other blk_status_t otherwise (and
 *	   keyslot is set to NULL).
 */
blk_status_t blk_ksm_get_slot_for_key(struct blk_keyslot_manager *ksm,
				      const struct blk_crypto_key *key,
				      struct blk_ksm_keyslot **slot_ptr)
{
	struct blk_ksm_keyslot *slot;
	int slot_idx;
	int err;

	*slot_ptr = NULL;

	if (blk_ksm_is_passthrough(ksm))
		return BLK_STS_OK;

	down_read(&ksm->lock);
	slot = blk_ksm_find_and_grab_keyslot(ksm, key);
	up_read(&ksm->lock);
	if (slot)
		goto success;

	for (;;) {
		blk_ksm_hw_enter(ksm);
		slot = blk_ksm_find_and_grab_keyslot(ksm, key);
		if (slot) {
			blk_ksm_hw_exit(ksm);
			goto success;
		}

		/*
		 * If we're here, that means there wasn't a slot that was
		 * already programmed with the key. So try to program it.
		 */
		if (!list_empty(&ksm->idle_slots))
			break;

		blk_ksm_hw_exit(ksm);
		wait_event(ksm->idle_slots_wait_queue,
			   !list_empty(&ksm->idle_slots));
	}

	slot = list_first_entry(&ksm->idle_slots, struct blk_ksm_keyslot,
				idle_slot_node);
	slot_idx = blk_ksm_get_slot_idx(slot);

	err = ksm->ksm_ll_ops.keyslot_program(ksm, key, slot_idx);
	if (err) {
		wake_up(&ksm->idle_slots_wait_queue);
		blk_ksm_hw_exit(ksm);
		return errno_to_blk_status(err);
	}

	/* Move this slot to the hash list for the new key. */
	if (slot->key)
		hlist_del(&slot->hash_node);
	slot->key = key;
	hlist_add_head(&slot->hash_node, blk_ksm_hash_bucket_for_key(ksm, key));

	atomic_set(&slot->slot_refs, 1);

	blk_ksm_remove_slot_from_lru_list(slot);

	blk_ksm_hw_exit(ksm);
success:
	*slot_ptr = slot;
	return BLK_STS_OK;
}

/**
 * blk_ksm_put_slot() - Release a reference to a slot
 * @slot: The keyslot to release the reference of.
 *
 * Context: Any context.
 */
void blk_ksm_put_slot(struct blk_ksm_keyslot *slot)
{
	struct blk_keyslot_manager *ksm;
	unsigned long flags;

	if (!slot)
		return;

	ksm = slot->ksm;

	if (atomic_dec_and_lock_irqsave(&slot->slot_refs,
					&ksm->idle_slots_lock, flags)) {
		list_add_tail(&slot->idle_slot_node, &ksm->idle_slots);
		spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);
		wake_up(&ksm->idle_slots_wait_queue);
	}
}

/**
 * blk_ksm_crypto_cfg_supported() - Find out if a crypto configuration is
 *				    supported by a ksm.
 * @ksm: The keyslot manager to check
 * @cfg: The crypto configuration to check for.
 *
 * Checks for crypto_mode/data unit size/dun bytes support.
 *
 * Return: Whether or not this ksm supports the specified crypto config.
 */
bool blk_ksm_crypto_cfg_supported(struct blk_keyslot_manager *ksm,
				  const struct blk_crypto_config *cfg)
{
	if (!ksm)
		return false;
	if (!(ksm->crypto_modes_supported[cfg->crypto_mode] &
	      cfg->data_unit_size))
		return false;
	if (ksm->max_dun_bytes_supported < cfg->dun_bytes)
		return false;
	if (cfg->is_hw_wrapped) {
		if (!(ksm->features & BLK_CRYPTO_FEATURE_WRAPPED_KEYS))
			return false;
	} else {
		if (!(ksm->features & BLK_CRYPTO_FEATURE_STANDARD_KEYS))
			return false;
	}
	return true;
}

/*
 * This is an internal function that evicts a key from an inline encryption
 * device that can be either a real device or the blk-crypto-fallback "device".
 * It is used only by blk_crypto_evict_key(); see that function for details.
 */
int blk_ksm_evict_key(struct blk_keyslot_manager *ksm,
		      const struct blk_crypto_key *key)
{
	struct blk_ksm_keyslot *slot;
	int err;

	if (blk_ksm_is_passthrough(ksm)) {
		if (ksm->ksm_ll_ops.keyslot_evict) {
			blk_ksm_hw_enter(ksm);
			err = ksm->ksm_ll_ops.keyslot_evict(ksm, key, -1);
			blk_ksm_hw_exit(ksm);
			return err;
		}
		return 0;
	}

	blk_ksm_hw_enter(ksm);
	slot = blk_ksm_find_keyslot(ksm, key);
	if (!slot) {
		/*
		 * Not an error, since a key not in use by I/O is not guaranteed
		 * to be in a keyslot.  There can be more keys than keyslots.
		 */
		err = 0;
		goto out;
	}

	if (WARN_ON_ONCE(atomic_read(&slot->slot_refs) != 0)) {
		/* BUG: key is still in use by I/O */
		err = -EBUSY;
		goto out_remove;
	}
	err = ksm->ksm_ll_ops.keyslot_evict(ksm, key,
					    blk_ksm_get_slot_idx(slot));
out_remove:
	/*
	 * Callers free the key even on error, so unlink the key from the hash
	 * table and clear slot->key even on error.
	 */
	hlist_del(&slot->hash_node);
	slot->key = NULL;
out:
	blk_ksm_hw_exit(ksm);
	return err;
}

/**
 * blk_ksm_reprogram_all_keys() - Re-program all keyslots.
 * @ksm: The keyslot manager
 *
 * Re-program all keyslots that are supposed to have a key programmed.  This is
 * intended only for use by drivers for hardware that loses its keys on reset.
 *
 * Context: Process context. Takes and releases ksm->lock.
 */
void blk_ksm_reprogram_all_keys(struct blk_keyslot_manager *ksm)
{
	unsigned int slot;

	if (blk_ksm_is_passthrough(ksm))
		return;

	/* This is for device initialization, so don't resume the device */
	down_write(&ksm->lock);
	for (slot = 0; slot < ksm->num_slots; slot++) {
		const struct blk_crypto_key *key = ksm->slots[slot].key;
		int err;

		if (!key)
			continue;

		err = ksm->ksm_ll_ops.keyslot_program(ksm, key, slot);
		WARN_ON(err);
	}
	up_write(&ksm->lock);
}
EXPORT_SYMBOL_GPL(blk_ksm_reprogram_all_keys);

void blk_ksm_destroy(struct blk_keyslot_manager *ksm)
{
	if (!ksm)
		return;
	kvfree(ksm->slot_hashtable);
	kvfree_sensitive(ksm->slots, sizeof(ksm->slots[0]) * ksm->num_slots);
	memzero_explicit(ksm, sizeof(*ksm));
}
EXPORT_SYMBOL_GPL(blk_ksm_destroy);

bool blk_ksm_register(struct blk_keyslot_manager *ksm, struct request_queue *q)
{
	if (blk_integrity_queue_supports_integrity(q)) {
		pr_warn("Integrity and hardware inline encryption are not supported together. Disabling hardware inline encryption.\n");
		return false;
	}
	q->ksm = ksm;
	return true;
}
EXPORT_SYMBOL_GPL(blk_ksm_register);

void blk_ksm_unregister(struct request_queue *q)
{
	q->ksm = NULL;
}

/**
 * blk_ksm_derive_raw_secret() - Derive software secret from wrapped key
 * @ksm: The keyslot manager
 * @wrapped_key: The wrapped key
 * @wrapped_key_size: Size of the wrapped key in bytes
 * @secret: (output) the software secret
 * @secret_size: (output) the number of secret bytes to derive
 *
 * Given a hardware-wrapped key, ask the hardware to derive a secret which
 * software can use for cryptographic tasks other than inline encryption.  The
 * derived secret is guaranteed to be cryptographically isolated from the key
 * with which any inline encryption with this wrapped key would actually be
 * done.  I.e., both will be derived from the unwrapped key.
 *
 * Return: 0 on success, -EOPNOTSUPP if hardware-wrapped keys are unsupported,
 *	   or another -errno code.
 */
int blk_ksm_derive_raw_secret(struct blk_keyslot_manager *ksm,
			      const u8 *wrapped_key,
			      unsigned int wrapped_key_size,
			      u8 *secret, unsigned int secret_size)
{
	int err;

	if (ksm->ksm_ll_ops.derive_raw_secret) {
		blk_ksm_hw_enter(ksm);
		err = ksm->ksm_ll_ops.derive_raw_secret(ksm, wrapped_key,
							wrapped_key_size,
							secret, secret_size);
		blk_ksm_hw_exit(ksm);
	} else {
		err = -EOPNOTSUPP;
	}

	return err;
}
EXPORT_SYMBOL_GPL(blk_ksm_derive_raw_secret);

/**
 * blk_ksm_intersect_modes() - restrict supported modes by child device
 * @parent: The keyslot manager for parent device
 * @child: The keyslot manager for child device, or NULL
 *
 * Clear any crypto mode support bits in @parent that aren't set in @child.
 * If @child is NULL, then all parent bits are cleared.
 *
 * Only use this when setting up the keyslot manager for a layered device,
 * before it's been exposed yet.
 */
void blk_ksm_intersect_modes(struct blk_keyslot_manager *parent,
			     const struct blk_keyslot_manager *child)
{
	if (child) {
		unsigned int i;

		parent->max_dun_bytes_supported =
			min(parent->max_dun_bytes_supported,
			    child->max_dun_bytes_supported);
		for (i = 0; i < ARRAY_SIZE(child->crypto_modes_supported);
		     i++) {
			parent->crypto_modes_supported[i] &=
				child->crypto_modes_supported[i];
		}
		parent->features &= child->features;
	} else {
		parent->max_dun_bytes_supported = 0;
		memset(parent->crypto_modes_supported, 0,
		       sizeof(parent->crypto_modes_supported));
		parent->features = 0;
	}
}
EXPORT_SYMBOL_GPL(blk_ksm_intersect_modes);

/**
 * blk_ksm_is_superset() - Check if a KSM supports a superset of crypto modes
 *			   and DUN bytes that another KSM supports. Here,
 *			   "superset" refers to the mathematical meaning of the
 *			   word - i.e. if two KSMs have the *same* capabilities,
 *			   they *are* considered supersets of each other.
 * @ksm_superset: The KSM that we want to verify is a superset
 * @ksm_subset: The KSM that we want to verify is a subset
 *
 * Return: True if @ksm_superset supports a superset of the crypto modes and DUN
 *	   bytes that @ksm_subset supports.
 */
bool blk_ksm_is_superset(struct blk_keyslot_manager *ksm_superset,
			 struct blk_keyslot_manager *ksm_subset)
{
	int i;

	if (!ksm_subset)
		return true;

	if (!ksm_superset)
		return false;

	for (i = 0; i < ARRAY_SIZE(ksm_superset->crypto_modes_supported); i++) {
		if (ksm_subset->crypto_modes_supported[i] &
		    (~ksm_superset->crypto_modes_supported[i])) {
			return false;
		}
	}

	if (ksm_subset->max_dun_bytes_supported >
	    ksm_superset->max_dun_bytes_supported) {
		return false;
	}

	if (ksm_subset->features & ~ksm_superset->features)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(blk_ksm_is_superset);

/**
 * blk_ksm_update_capabilities() - Update the restrictions of a KSM to those of
 *				   another KSM
 * @target_ksm: The KSM whose restrictions to update.
 * @reference_ksm: The KSM to whose restrictions this function will update
 *		   @target_ksm's restrictions to.
 *
 * Blk-crypto requires that crypto capabilities that were
 * advertised when a bio was created continue to be supported by the
 * device until that bio is ended. This is turn means that a device cannot
 * shrink its advertised crypto capabilities without any explicit
 * synchronization with upper layers. So if there's no such explicit
 * synchronization, @reference_ksm must support all the crypto capabilities that
 * @target_ksm does
 * (i.e. we need blk_ksm_is_superset(@reference_ksm, @target_ksm) == true).
 *
 * Note also that as long as the crypto capabilities are being expanded, the
 * order of updates becoming visible is not important because it's alright
 * for blk-crypto to see stale values - they only cause blk-crypto to
 * believe that a crypto capability isn't supported when it actually is (which
 * might result in blk-crypto-fallback being used if available, or the bio being
 * failed).
 */
void blk_ksm_update_capabilities(struct blk_keyslot_manager *target_ksm,
				 struct blk_keyslot_manager *reference_ksm)
{
	memcpy(target_ksm->crypto_modes_supported,
	       reference_ksm->crypto_modes_supported,
	       sizeof(target_ksm->crypto_modes_supported));

	target_ksm->max_dun_bytes_supported =
				reference_ksm->max_dun_bytes_supported;

	target_ksm->features = reference_ksm->features;
}
EXPORT_SYMBOL_GPL(blk_ksm_update_capabilities);

/**
 * blk_ksm_init_passthrough() - Init a passthrough keyslot manager
 * @ksm: The keyslot manager to init
 *
 * Initialize a passthrough keyslot manager.
 * Called by e.g. storage drivers to set up a keyslot manager in their
 * request_queue, when the storage driver wants to manage its keys by itself.
 * This is useful for inline encryption hardware that doesn't have the concept
 * of keyslots, and for layered devices.
 */
void blk_ksm_init_passthrough(struct blk_keyslot_manager *ksm)
{
	memset(ksm, 0, sizeof(*ksm));
	init_rwsem(&ksm->lock);
}
EXPORT_SYMBOL_GPL(blk_ksm_init_passthrough);
