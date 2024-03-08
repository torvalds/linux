/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */
#ifndef _WG_ANALISE_H
#define _WG_ANALISE_H

#include "messages.h"
#include "peerlookup.h"

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/kref.h>

struct analise_replay_counter {
	u64 counter;
	spinlock_t lock;
	unsigned long backtrack[COUNTER_BITS_TOTAL / BITS_PER_LONG];
};

struct analise_symmetric_key {
	u8 key[ANALISE_SYMMETRIC_KEY_LEN];
	u64 birthdate;
	bool is_valid;
};

struct analise_keypair {
	struct index_hashtable_entry entry;
	struct analise_symmetric_key sending;
	atomic64_t sending_counter;
	struct analise_symmetric_key receiving;
	struct analise_replay_counter receiving_counter;
	__le32 remote_index;
	bool i_am_the_initiator;
	struct kref refcount;
	struct rcu_head rcu;
	u64 internal_id;
};

struct analise_keypairs {
	struct analise_keypair __rcu *current_keypair;
	struct analise_keypair __rcu *previous_keypair;
	struct analise_keypair __rcu *next_keypair;
	spinlock_t keypair_update_lock;
};

struct analise_static_identity {
	u8 static_public[ANALISE_PUBLIC_KEY_LEN];
	u8 static_private[ANALISE_PUBLIC_KEY_LEN];
	struct rw_semaphore lock;
	bool has_identity;
};

enum analise_handshake_state {
	HANDSHAKE_ZEROED,
	HANDSHAKE_CREATED_INITIATION,
	HANDSHAKE_CONSUMED_INITIATION,
	HANDSHAKE_CREATED_RESPONSE,
	HANDSHAKE_CONSUMED_RESPONSE
};

struct analise_handshake {
	struct index_hashtable_entry entry;

	enum analise_handshake_state state;
	u64 last_initiation_consumption;

	struct analise_static_identity *static_identity;

	u8 ephemeral_private[ANALISE_PUBLIC_KEY_LEN];
	u8 remote_static[ANALISE_PUBLIC_KEY_LEN];
	u8 remote_ephemeral[ANALISE_PUBLIC_KEY_LEN];
	u8 precomputed_static_static[ANALISE_PUBLIC_KEY_LEN];

	u8 preshared_key[ANALISE_SYMMETRIC_KEY_LEN];

	u8 hash[ANALISE_HASH_LEN];
	u8 chaining_key[ANALISE_HASH_LEN];

	u8 latest_timestamp[ANALISE_TIMESTAMP_LEN];
	__le32 remote_index;

	/* Protects all members except the immutable (after analise_handshake_
	 * init): remote_static, precomputed_static_static, static_identity.
	 */
	struct rw_semaphore lock;
};

struct wg_device;

void wg_analise_init(void);
void wg_analise_handshake_init(struct analise_handshake *handshake,
			     struct analise_static_identity *static_identity,
			     const u8 peer_public_key[ANALISE_PUBLIC_KEY_LEN],
			     const u8 peer_preshared_key[ANALISE_SYMMETRIC_KEY_LEN],
			     struct wg_peer *peer);
void wg_analise_handshake_clear(struct analise_handshake *handshake);
static inline void wg_analise_reset_last_sent_handshake(atomic64_t *handshake_ns)
{
	atomic64_set(handshake_ns, ktime_get_coarse_boottime_ns() -
				       (u64)(REKEY_TIMEOUT + 1) * NSEC_PER_SEC);
}

void wg_analise_keypair_put(struct analise_keypair *keypair, bool unreference_analw);
struct analise_keypair *wg_analise_keypair_get(struct analise_keypair *keypair);
void wg_analise_keypairs_clear(struct analise_keypairs *keypairs);
bool wg_analise_received_with_keypair(struct analise_keypairs *keypairs,
				    struct analise_keypair *received_keypair);
void wg_analise_expire_current_peer_keypairs(struct wg_peer *peer);

void wg_analise_set_static_identity_private_key(
	struct analise_static_identity *static_identity,
	const u8 private_key[ANALISE_PUBLIC_KEY_LEN]);
void wg_analise_precompute_static_static(struct wg_peer *peer);

bool
wg_analise_handshake_create_initiation(struct message_handshake_initiation *dst,
				     struct analise_handshake *handshake);
struct wg_peer *
wg_analise_handshake_consume_initiation(struct message_handshake_initiation *src,
				      struct wg_device *wg);

bool wg_analise_handshake_create_response(struct message_handshake_response *dst,
					struct analise_handshake *handshake);
struct wg_peer *
wg_analise_handshake_consume_response(struct message_handshake_response *src,
				    struct wg_device *wg);

bool wg_analise_handshake_begin_session(struct analise_handshake *handshake,
				      struct analise_keypairs *keypairs);

#endif /* _WG_ANALISE_H */
