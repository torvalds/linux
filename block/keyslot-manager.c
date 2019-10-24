// SPDX-License-Identifier: GPL-2.0
/*
 * keyslot-manager.c
 *
 * Copyright 2019 Google LLC
 */

/**
 * DOC: The Keyslot Manager
 *
 * Many devices with inline encryption support have a limited number of "slots"
 * into which encryption contexts may be programmed, and requests can be tagged
 * with a slot number to specify the key to use for en/decryption.
 *
 * As the number of slots are limited, and programming keys is expensive on
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
 * Upper layers will call keyslot_manager_get_slot_for_key() to program a
 * key into some slot in the inline encryption hardware.
 */
#include <linux/keyslot-manager.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/blkdev.h>

struct keyslot {
	atomic_t slot_refs;
	struct list_head idle_slot_node;
};

struct keyslot_manager {
	unsigned int num_slots;
	atomic_t num_idle_slots;
	struct keyslot_mgmt_ll_ops ksm_ll_ops;
	void *ll_priv_data;

	/* Protects programming and evicting keys from the device */
	struct rw_semaphore lock;

	/* List of idle slots, with least recently used slot at front */
	wait_queue_head_t idle_slots_wait_queue;
	struct list_head idle_slots;
	spinlock_t idle_slots_lock;

	/* Per-keyslot data */
	struct keyslot slots[];
};

/**
 * keyslot_manager_create() - Create a keyslot manager
 * @num_slots: The number of key slots to manage.
 * @ksm_ll_ops: The struct keyslot_mgmt_ll_ops for the device that this keyslot
 *		manager will use to perform operations like programming and
 *		evicting keys.
 * @ll_priv_data: Private data passed as is to the functions in ksm_ll_ops.
 *
 * Allocate memory for and initialize a keyslot manager. Called by e.g.
 * storage drivers to set up a keyslot manager in their request_queue.
 *
 * Context: May sleep
 * Return: Pointer to constructed keyslot manager or NULL on error.
 */
struct keyslot_manager *keyslot_manager_create(unsigned int num_slots,
				const struct keyslot_mgmt_ll_ops *ksm_ll_ops,
				void *ll_priv_data)
{
	struct keyslot_manager *ksm;
	int slot;

	if (num_slots == 0)
		return NULL;

	/* Check that all ops are specified */
	if (ksm_ll_ops->keyslot_program == NULL ||
	    ksm_ll_ops->keyslot_evict == NULL ||
	    ksm_ll_ops->crypto_mode_supported == NULL ||
	    ksm_ll_ops->keyslot_find == NULL)
		return NULL;

	ksm = kvzalloc(struct_size(ksm, slots, num_slots), GFP_KERNEL);
	if (!ksm)
		return NULL;

	ksm->num_slots = num_slots;
	atomic_set(&ksm->num_idle_slots, num_slots);
	ksm->ksm_ll_ops = *ksm_ll_ops;
	ksm->ll_priv_data = ll_priv_data;

	init_rwsem(&ksm->lock);

	init_waitqueue_head(&ksm->idle_slots_wait_queue);
	INIT_LIST_HEAD(&ksm->idle_slots);

	for (slot = 0; slot < num_slots; slot++) {
		list_add_tail(&ksm->slots[slot].idle_slot_node,
			      &ksm->idle_slots);
	}

	spin_lock_init(&ksm->idle_slots_lock);

	return ksm;
}
EXPORT_SYMBOL(keyslot_manager_create);

static void remove_slot_from_lru_list(struct keyslot_manager *ksm, int slot)
{
	unsigned long flags;

	spin_lock_irqsave(&ksm->idle_slots_lock, flags);
	list_del(&ksm->slots[slot].idle_slot_node);
	spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);

	atomic_dec(&ksm->num_idle_slots);
}

static int find_and_grab_keyslot(struct keyslot_manager *ksm, const u8 *key,
				 enum blk_crypto_mode_num crypto_mode,
				 unsigned int data_unit_size)
{
	int slot;

	slot = ksm->ksm_ll_ops.keyslot_find(ksm->ll_priv_data, key,
					    crypto_mode, data_unit_size);
	if (slot < 0)
		return slot;
	if (WARN_ON(slot >= ksm->num_slots))
		return -EINVAL;
	if (atomic_inc_return(&ksm->slots[slot].slot_refs) == 1) {
		/* Took first reference to this slot; remove it from LRU list */
		remove_slot_from_lru_list(ksm, slot);
	}
	return slot;
}

/**
 * keyslot_manager_get_slot_for_key() - Program a key into a keyslot.
 * @ksm: The keyslot manager to program the key into.
 * @key: Pointer to the bytes of the key to program. Must be the correct length
 *      for the chosen @crypto_mode; see blk_crypto_modes in blk-crypto.c.
 * @crypto_mode: Identifier for the encryption algorithm to use.
 * @data_unit_size: The data unit size to use for en/decryption.
 *
 * Get a keyslot that's been programmed with the specified key, crypto_mode, and
 * data_unit_size.  If one already exists, return it with incremented refcount.
 * Otherwise, wait for a keyslot to become idle and program it.
 *
 * Context: Process context. Takes and releases ksm->lock.
 * Return: The keyslot on success, else a -errno value.
 */
int keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     enum blk_crypto_mode_num crypto_mode,
				     unsigned int data_unit_size)
{
	int slot;
	int err;
	struct keyslot *idle_slot;

	down_read(&ksm->lock);
	slot = find_and_grab_keyslot(ksm, key, crypto_mode, data_unit_size);
	up_read(&ksm->lock);
	if (slot != -ENOKEY)
		return slot;

	for (;;) {
		down_write(&ksm->lock);
		slot = find_and_grab_keyslot(ksm, key, crypto_mode,
					     data_unit_size);
		if (slot != -ENOKEY) {
			up_write(&ksm->lock);
			return slot;
		}

		/*
		 * If we're here, that means there wasn't a slot that was
		 * already programmed with the key. So try to program it.
		 */
		if (atomic_read(&ksm->num_idle_slots) > 0)
			break;

		up_write(&ksm->lock);
		wait_event(ksm->idle_slots_wait_queue,
			(atomic_read(&ksm->num_idle_slots) > 0));
	}

	idle_slot = list_first_entry(&ksm->idle_slots, struct keyslot,
					     idle_slot_node);
	slot = idle_slot - ksm->slots;

	err = ksm->ksm_ll_ops.keyslot_program(ksm->ll_priv_data, key,
					      crypto_mode,
					      data_unit_size,
					      slot);

	if (err) {
		wake_up(&ksm->idle_slots_wait_queue);
		up_write(&ksm->lock);
		return err;
	}

	atomic_set(&ksm->slots[slot].slot_refs, 1);
	remove_slot_from_lru_list(ksm, slot);

	up_write(&ksm->lock);
	return slot;

}
EXPORT_SYMBOL(keyslot_manager_get_slot_for_key);

/**
 * keyslot_manager_get_slot() - Increment the refcount on the specified slot.
 * @ksm - The keyslot manager that we want to modify.
 * @slot - The slot to increment the refcount of.
 *
 * This function assumes that there is already an active reference to that slot
 * and simply increments the refcount. This is useful when cloning a bio that
 * already has a reference to a keyslot, and we want the cloned bio to also have
 * its own reference.
 *
 * Context: Any context.
 */
void keyslot_manager_get_slot(struct keyslot_manager *ksm, unsigned int slot)
{
	if (WARN_ON(slot >= ksm->num_slots))
		return;

	WARN_ON(atomic_inc_return(&ksm->slots[slot].slot_refs) < 2);
}
EXPORT_SYMBOL(keyslot_manager_get_slot);

/**
 * keyslot_manager_put_slot() - Release a reference to a slot
 * @ksm: The keyslot manager to release the reference from.
 * @slot: The slot to release the reference from.
 *
 * Context: Any context.
 */
void keyslot_manager_put_slot(struct keyslot_manager *ksm, unsigned int slot)
{
	unsigned long flags;

	if (WARN_ON(slot >= ksm->num_slots))
		return;

	if (atomic_dec_and_lock_irqsave(&ksm->slots[slot].slot_refs,
					&ksm->idle_slots_lock, flags)) {
		list_add_tail(&ksm->slots[slot].idle_slot_node,
			      &ksm->idle_slots);
		spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);
		atomic_inc(&ksm->num_idle_slots);
		wake_up(&ksm->idle_slots_wait_queue);
	}
}
EXPORT_SYMBOL(keyslot_manager_put_slot);

/**
 * keyslot_manager_crypto_mode_supported() - Find out if a crypto_mode/data
 *					     unit size combination is supported
 *					     by a ksm.
 * @ksm - The keyslot manager to check
 * @crypto_mode - The crypto mode to check for.
 * @data_unit_size - The data_unit_size for the mode.
 *
 * Calls and returns the result of the crypto_mode_supported function specified
 * by the ksm.
 *
 * Context: Process context.
 * Return: Whether or not this ksm supports the specified crypto_mode/
 *	   data_unit_size combo.
 */
bool keyslot_manager_crypto_mode_supported(struct keyslot_manager *ksm,
					   enum blk_crypto_mode_num crypto_mode,
					   unsigned int data_unit_size)
{
	if (!ksm)
		return false;
	return ksm->ksm_ll_ops.crypto_mode_supported(ksm->ll_priv_data,
						     crypto_mode,
						     data_unit_size);
}
EXPORT_SYMBOL(keyslot_manager_crypto_mode_supported);

bool keyslot_manager_rq_crypto_mode_supported(struct request_queue *q,
					enum blk_crypto_mode_num crypto_mode,
					unsigned int data_unit_size)
{
	return keyslot_manager_crypto_mode_supported(q->ksm, crypto_mode,
						     data_unit_size);
}
EXPORT_SYMBOL(keyslot_manager_rq_crypto_mode_supported);

/**
 * keyslot_manager_evict_key() - Evict a key from the lower layer device.
 * @ksm - The keyslot manager to evict from
 * @key - The key to evict
 * @crypto_mode - The crypto algorithm the key was programmed with.
 * @data_unit_size - The data_unit_size the key was programmed with.
 *
 * Finds the slot that the specified key, crypto_mode, data_unit_size combo
 * was programmed into, and evicts that slot from the lower layer device if
 * the refcount on the slot is 0. Returns -EBUSY if the refcount is not 0, and
 * -errno on error.
 *
 * Context: Process context. Takes and releases ksm->lock.
 */
int keyslot_manager_evict_key(struct keyslot_manager *ksm,
			      const u8 *key,
			      enum blk_crypto_mode_num crypto_mode,
			      unsigned int data_unit_size)
{
	int slot;
	int err = 0;

	down_write(&ksm->lock);
	slot = ksm->ksm_ll_ops.keyslot_find(ksm->ll_priv_data, key,
					    crypto_mode,
					    data_unit_size);

	if (slot < 0) {
		up_write(&ksm->lock);
		return slot;
	}

	if (atomic_read(&ksm->slots[slot].slot_refs) == 0) {
		err = ksm->ksm_ll_ops.keyslot_evict(ksm->ll_priv_data, key,
						    crypto_mode,
						    data_unit_size,
						    slot);
	} else {
		err = -EBUSY;
	}

	up_write(&ksm->lock);
	return err;
}
EXPORT_SYMBOL(keyslot_manager_evict_key);

void keyslot_manager_destroy(struct keyslot_manager *ksm)
{
	kvfree(ksm);
}
EXPORT_SYMBOL(keyslot_manager_destroy);
