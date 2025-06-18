/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNCRYPTO_H_
#define _NET_OVPN_OVPNCRYPTO_H_

#include "pktid.h"
#include "proto.h"

/* info needed for both encrypt and decrypt directions */
struct ovpn_key_direction {
	const u8 *cipher_key;
	size_t cipher_key_size;
	const u8 *nonce_tail; /* only needed for GCM modes */
	size_t nonce_tail_size; /* only needed for GCM modes */
};

/* all info for a particular symmetric key (primary or secondary) */
struct ovpn_key_config {
	enum ovpn_cipher_alg cipher_alg;
	u8 key_id;
	struct ovpn_key_direction encrypt;
	struct ovpn_key_direction decrypt;
};

/* used to pass settings from netlink to the crypto engine */
struct ovpn_peer_key_reset {
	enum ovpn_key_slot slot;
	struct ovpn_key_config key;
};

struct ovpn_crypto_key_slot {
	u8 key_id;

	struct crypto_aead *encrypt;
	struct crypto_aead *decrypt;
	u8 nonce_tail_xmit[OVPN_NONCE_TAIL_SIZE];
	u8 nonce_tail_recv[OVPN_NONCE_TAIL_SIZE];

	struct ovpn_pktid_recv pid_recv ____cacheline_aligned_in_smp;
	struct ovpn_pktid_xmit pid_xmit ____cacheline_aligned_in_smp;
	struct kref refcount;
	struct rcu_head rcu;
};

struct ovpn_crypto_state {
	struct ovpn_crypto_key_slot __rcu *slots[2];
	u8 primary_idx;

	/* protects primary and secondary slots */
	spinlock_t lock;
};

static inline bool ovpn_crypto_key_slot_hold(struct ovpn_crypto_key_slot *ks)
{
	return kref_get_unless_zero(&ks->refcount);
}

static inline void ovpn_crypto_state_init(struct ovpn_crypto_state *cs)
{
	RCU_INIT_POINTER(cs->slots[0], NULL);
	RCU_INIT_POINTER(cs->slots[1], NULL);
	cs->primary_idx = 0;
	spin_lock_init(&cs->lock);
}

static inline struct ovpn_crypto_key_slot *
ovpn_crypto_key_id_to_slot(const struct ovpn_crypto_state *cs, u8 key_id)
{
	struct ovpn_crypto_key_slot *ks;
	u8 idx;

	if (unlikely(!cs))
		return NULL;

	rcu_read_lock();
	idx = READ_ONCE(cs->primary_idx);
	ks = rcu_dereference(cs->slots[idx]);
	if (ks && ks->key_id == key_id) {
		if (unlikely(!ovpn_crypto_key_slot_hold(ks)))
			ks = NULL;
		goto out;
	}

	ks = rcu_dereference(cs->slots[!idx]);
	if (ks && ks->key_id == key_id) {
		if (unlikely(!ovpn_crypto_key_slot_hold(ks)))
			ks = NULL;
		goto out;
	}

	/* when both key slots are occupied but no matching key ID is found, ks
	 * has to be reset to NULL to avoid carrying a stale pointer
	 */
	ks = NULL;
out:
	rcu_read_unlock();

	return ks;
}

static inline struct ovpn_crypto_key_slot *
ovpn_crypto_key_slot_primary(const struct ovpn_crypto_state *cs)
{
	struct ovpn_crypto_key_slot *ks;

	rcu_read_lock();
	ks = rcu_dereference(cs->slots[cs->primary_idx]);
	if (unlikely(ks && !ovpn_crypto_key_slot_hold(ks)))
		ks = NULL;
	rcu_read_unlock();

	return ks;
}

void ovpn_crypto_key_slot_release(struct kref *kref);

static inline void ovpn_crypto_key_slot_put(struct ovpn_crypto_key_slot *ks)
{
	kref_put(&ks->refcount, ovpn_crypto_key_slot_release);
}

int ovpn_crypto_state_reset(struct ovpn_crypto_state *cs,
			    const struct ovpn_peer_key_reset *pkr);

void ovpn_crypto_key_slot_delete(struct ovpn_crypto_state *cs,
				 enum ovpn_key_slot slot);

void ovpn_crypto_state_release(struct ovpn_crypto_state *cs);

void ovpn_crypto_key_slots_swap(struct ovpn_crypto_state *cs);

int ovpn_crypto_config_get(struct ovpn_crypto_state *cs,
			   enum ovpn_key_slot slot,
			   struct ovpn_key_config *keyconf);

bool ovpn_crypto_kill_key(struct ovpn_crypto_state *cs, u8 key_id);

#endif /* _NET_OVPN_OVPNCRYPTO_H_ */
