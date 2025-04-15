// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/types.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <uapi/linux/ovpn.h>

#include "ovpnpriv.h"
#include "main.h"
#include "pktid.h"
#include "crypto_aead.h"
#include "crypto.h"

static void ovpn_ks_destroy_rcu(struct rcu_head *head)
{
	struct ovpn_crypto_key_slot *ks;

	ks = container_of(head, struct ovpn_crypto_key_slot, rcu);
	ovpn_aead_crypto_key_slot_destroy(ks);
}

void ovpn_crypto_key_slot_release(struct kref *kref)
{
	struct ovpn_crypto_key_slot *ks;

	ks = container_of(kref, struct ovpn_crypto_key_slot, refcount);
	call_rcu(&ks->rcu, ovpn_ks_destroy_rcu);
}

/* can only be invoked when all peer references have been dropped (i.e. RCU
 * release routine)
 */
void ovpn_crypto_state_release(struct ovpn_crypto_state *cs)
{
	struct ovpn_crypto_key_slot *ks;

	ks = rcu_access_pointer(cs->slots[0]);
	if (ks) {
		RCU_INIT_POINTER(cs->slots[0], NULL);
		ovpn_crypto_key_slot_put(ks);
	}

	ks = rcu_access_pointer(cs->slots[1]);
	if (ks) {
		RCU_INIT_POINTER(cs->slots[1], NULL);
		ovpn_crypto_key_slot_put(ks);
	}
}

/* Reset the ovpn_crypto_state object in a way that is atomic
 * to RCU readers.
 */
int ovpn_crypto_state_reset(struct ovpn_crypto_state *cs,
			    const struct ovpn_peer_key_reset *pkr)
{
	struct ovpn_crypto_key_slot *old = NULL, *new;
	u8 idx;

	if (pkr->slot != OVPN_KEY_SLOT_PRIMARY &&
	    pkr->slot != OVPN_KEY_SLOT_SECONDARY)
		return -EINVAL;

	new = ovpn_aead_crypto_key_slot_new(&pkr->key);
	if (IS_ERR(new))
		return PTR_ERR(new);

	spin_lock_bh(&cs->lock);
	idx = cs->primary_idx;
	switch (pkr->slot) {
	case OVPN_KEY_SLOT_PRIMARY:
		old = rcu_replace_pointer(cs->slots[idx], new,
					  lockdep_is_held(&cs->lock));
		break;
	case OVPN_KEY_SLOT_SECONDARY:
		old = rcu_replace_pointer(cs->slots[!idx], new,
					  lockdep_is_held(&cs->lock));
		break;
	}
	spin_unlock_bh(&cs->lock);

	if (old)
		ovpn_crypto_key_slot_put(old);

	return 0;
}

void ovpn_crypto_key_slot_delete(struct ovpn_crypto_state *cs,
				 enum ovpn_key_slot slot)
{
	struct ovpn_crypto_key_slot *ks = NULL;
	u8 idx;

	if (slot != OVPN_KEY_SLOT_PRIMARY &&
	    slot != OVPN_KEY_SLOT_SECONDARY) {
		pr_warn("Invalid slot to release: %u\n", slot);
		return;
	}

	spin_lock_bh(&cs->lock);
	idx = cs->primary_idx;
	switch (slot) {
	case OVPN_KEY_SLOT_PRIMARY:
		ks = rcu_replace_pointer(cs->slots[idx], NULL,
					 lockdep_is_held(&cs->lock));
		break;
	case OVPN_KEY_SLOT_SECONDARY:
		ks = rcu_replace_pointer(cs->slots[!idx], NULL,
					 lockdep_is_held(&cs->lock));
		break;
	}
	spin_unlock_bh(&cs->lock);

	if (!ks) {
		pr_debug("Key slot already released: %u\n", slot);
		return;
	}

	pr_debug("deleting key slot %u, key_id=%u\n", slot, ks->key_id);
	ovpn_crypto_key_slot_put(ks);
}

void ovpn_crypto_key_slots_swap(struct ovpn_crypto_state *cs)
{
	const struct ovpn_crypto_key_slot *old_primary, *old_secondary;
	u8 idx;

	spin_lock_bh(&cs->lock);
	idx = cs->primary_idx;
	old_primary = rcu_dereference_protected(cs->slots[idx],
						lockdep_is_held(&cs->lock));
	old_secondary = rcu_dereference_protected(cs->slots[!idx],
						  lockdep_is_held(&cs->lock));
	/* perform real swap by switching the index of the primary key */
	WRITE_ONCE(cs->primary_idx, !cs->primary_idx);

	pr_debug("key swapped: (old primary) %d <-> (new primary) %d\n",
		 old_primary ? old_primary->key_id : -1,
		 old_secondary ? old_secondary->key_id : -1);

	spin_unlock_bh(&cs->lock);
}
