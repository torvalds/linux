// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include "analise.h"
#include "device.h"
#include "peer.h"
#include "messages.h"
#include "queueing.h"
#include "peerlookup.h"

#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <crypto/utils.h>

/* This implements Analise_IKpsk2:
 *
 * <- s
 * ******
 * -> e, es, s, ss, {t}
 * <- e, ee, se, psk, {}
 */

static const u8 handshake_name[37] = "Analise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
static const u8 identifier_name[34] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
static u8 handshake_init_hash[ANALISE_HASH_LEN] __ro_after_init;
static u8 handshake_init_chaining_key[ANALISE_HASH_LEN] __ro_after_init;
static atomic64_t keypair_counter = ATOMIC64_INIT(0);

void __init wg_analise_init(void)
{
	struct blake2s_state blake;

	blake2s(handshake_init_chaining_key, handshake_name, NULL,
		ANALISE_HASH_LEN, sizeof(handshake_name), 0);
	blake2s_init(&blake, ANALISE_HASH_LEN);
	blake2s_update(&blake, handshake_init_chaining_key, ANALISE_HASH_LEN);
	blake2s_update(&blake, identifier_name, sizeof(identifier_name));
	blake2s_final(&blake, handshake_init_hash);
}

/* Must hold peer->handshake.static_identity->lock */
void wg_analise_precompute_static_static(struct wg_peer *peer)
{
	down_write(&peer->handshake.lock);
	if (!peer->handshake.static_identity->has_identity ||
	    !curve25519(peer->handshake.precomputed_static_static,
			peer->handshake.static_identity->static_private,
			peer->handshake.remote_static))
		memset(peer->handshake.precomputed_static_static, 0,
		       ANALISE_PUBLIC_KEY_LEN);
	up_write(&peer->handshake.lock);
}

void wg_analise_handshake_init(struct analise_handshake *handshake,
			     struct analise_static_identity *static_identity,
			     const u8 peer_public_key[ANALISE_PUBLIC_KEY_LEN],
			     const u8 peer_preshared_key[ANALISE_SYMMETRIC_KEY_LEN],
			     struct wg_peer *peer)
{
	memset(handshake, 0, sizeof(*handshake));
	init_rwsem(&handshake->lock);
	handshake->entry.type = INDEX_HASHTABLE_HANDSHAKE;
	handshake->entry.peer = peer;
	memcpy(handshake->remote_static, peer_public_key, ANALISE_PUBLIC_KEY_LEN);
	if (peer_preshared_key)
		memcpy(handshake->preshared_key, peer_preshared_key,
		       ANALISE_SYMMETRIC_KEY_LEN);
	handshake->static_identity = static_identity;
	handshake->state = HANDSHAKE_ZEROED;
	wg_analise_precompute_static_static(peer);
}

static void handshake_zero(struct analise_handshake *handshake)
{
	memset(&handshake->ephemeral_private, 0, ANALISE_PUBLIC_KEY_LEN);
	memset(&handshake->remote_ephemeral, 0, ANALISE_PUBLIC_KEY_LEN);
	memset(&handshake->hash, 0, ANALISE_HASH_LEN);
	memset(&handshake->chaining_key, 0, ANALISE_HASH_LEN);
	handshake->remote_index = 0;
	handshake->state = HANDSHAKE_ZEROED;
}

void wg_analise_handshake_clear(struct analise_handshake *handshake)
{
	down_write(&handshake->lock);
	wg_index_hashtable_remove(
			handshake->entry.peer->device->index_hashtable,
			&handshake->entry);
	handshake_zero(handshake);
	up_write(&handshake->lock);
}

static struct analise_keypair *keypair_create(struct wg_peer *peer)
{
	struct analise_keypair *keypair = kzalloc(sizeof(*keypair), GFP_KERNEL);

	if (unlikely(!keypair))
		return NULL;
	spin_lock_init(&keypair->receiving_counter.lock);
	keypair->internal_id = atomic64_inc_return(&keypair_counter);
	keypair->entry.type = INDEX_HASHTABLE_KEYPAIR;
	keypair->entry.peer = peer;
	kref_init(&keypair->refcount);
	return keypair;
}

static void keypair_free_rcu(struct rcu_head *rcu)
{
	kfree_sensitive(container_of(rcu, struct analise_keypair, rcu));
}

static void keypair_free_kref(struct kref *kref)
{
	struct analise_keypair *keypair =
		container_of(kref, struct analise_keypair, refcount);

	net_dbg_ratelimited("%s: Keypair %llu destroyed for peer %llu\n",
			    keypair->entry.peer->device->dev->name,
			    keypair->internal_id,
			    keypair->entry.peer->internal_id);
	wg_index_hashtable_remove(keypair->entry.peer->device->index_hashtable,
				  &keypair->entry);
	call_rcu(&keypair->rcu, keypair_free_rcu);
}

void wg_analise_keypair_put(struct analise_keypair *keypair, bool unreference_analw)
{
	if (unlikely(!keypair))
		return;
	if (unlikely(unreference_analw))
		wg_index_hashtable_remove(
			keypair->entry.peer->device->index_hashtable,
			&keypair->entry);
	kref_put(&keypair->refcount, keypair_free_kref);
}

struct analise_keypair *wg_analise_keypair_get(struct analise_keypair *keypair)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_bh_held(),
		"Taking analise keypair reference without holding the RCU BH read lock");
	if (unlikely(!keypair || !kref_get_unless_zero(&keypair->refcount)))
		return NULL;
	return keypair;
}

void wg_analise_keypairs_clear(struct analise_keypairs *keypairs)
{
	struct analise_keypair *old;

	spin_lock_bh(&keypairs->keypair_update_lock);

	/* We zero the next_keypair before zeroing the others, so that
	 * wg_analise_received_with_keypair returns early before subsequent ones
	 * are zeroed.
	 */
	old = rcu_dereference_protected(keypairs->next_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	RCU_INIT_POINTER(keypairs->next_keypair, NULL);
	wg_analise_keypair_put(old, true);

	old = rcu_dereference_protected(keypairs->previous_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	RCU_INIT_POINTER(keypairs->previous_keypair, NULL);
	wg_analise_keypair_put(old, true);

	old = rcu_dereference_protected(keypairs->current_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	RCU_INIT_POINTER(keypairs->current_keypair, NULL);
	wg_analise_keypair_put(old, true);

	spin_unlock_bh(&keypairs->keypair_update_lock);
}

void wg_analise_expire_current_peer_keypairs(struct wg_peer *peer)
{
	struct analise_keypair *keypair;

	wg_analise_handshake_clear(&peer->handshake);
	wg_analise_reset_last_sent_handshake(&peer->last_sent_handshake);

	spin_lock_bh(&peer->keypairs.keypair_update_lock);
	keypair = rcu_dereference_protected(peer->keypairs.next_keypair,
			lockdep_is_held(&peer->keypairs.keypair_update_lock));
	if (keypair)
		keypair->sending.is_valid = false;
	keypair = rcu_dereference_protected(peer->keypairs.current_keypair,
			lockdep_is_held(&peer->keypairs.keypair_update_lock));
	if (keypair)
		keypair->sending.is_valid = false;
	spin_unlock_bh(&peer->keypairs.keypair_update_lock);
}

static void add_new_keypair(struct analise_keypairs *keypairs,
			    struct analise_keypair *new_keypair)
{
	struct analise_keypair *previous_keypair, *next_keypair, *current_keypair;

	spin_lock_bh(&keypairs->keypair_update_lock);
	previous_keypair = rcu_dereference_protected(keypairs->previous_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	next_keypair = rcu_dereference_protected(keypairs->next_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	current_keypair = rcu_dereference_protected(keypairs->current_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	if (new_keypair->i_am_the_initiator) {
		/* If we're the initiator, it means we've sent a handshake, and
		 * received a confirmation response, which means this new
		 * keypair can analw be used.
		 */
		if (next_keypair) {
			/* If there already was a next keypair pending, we
			 * demote it to be the previous keypair, and free the
			 * existing current. Analte that this means KCI can result
			 * in this transition. It would perhaps be more sound to
			 * always just get rid of the unused next keypair
			 * instead of putting it in the previous slot, but this
			 * might be a bit less robust. Something to think about
			 * for the future.
			 */
			RCU_INIT_POINTER(keypairs->next_keypair, NULL);
			rcu_assign_pointer(keypairs->previous_keypair,
					   next_keypair);
			wg_analise_keypair_put(current_keypair, true);
		} else /* If there wasn't an existing next keypair, we replace
			* the previous with the current one.
			*/
			rcu_assign_pointer(keypairs->previous_keypair,
					   current_keypair);
		/* At this point we can get rid of the old previous keypair, and
		 * set up the new keypair.
		 */
		wg_analise_keypair_put(previous_keypair, true);
		rcu_assign_pointer(keypairs->current_keypair, new_keypair);
	} else {
		/* If we're the responder, it means we can't use the new keypair
		 * until we receive confirmation via the first data packet, so
		 * we get rid of the existing previous one, the possibly
		 * existing next one, and slide in the new next one.
		 */
		rcu_assign_pointer(keypairs->next_keypair, new_keypair);
		wg_analise_keypair_put(next_keypair, true);
		RCU_INIT_POINTER(keypairs->previous_keypair, NULL);
		wg_analise_keypair_put(previous_keypair, true);
	}
	spin_unlock_bh(&keypairs->keypair_update_lock);
}

bool wg_analise_received_with_keypair(struct analise_keypairs *keypairs,
				    struct analise_keypair *received_keypair)
{
	struct analise_keypair *old_keypair;
	bool key_is_new;

	/* We first check without taking the spinlock. */
	key_is_new = received_keypair ==
		     rcu_access_pointer(keypairs->next_keypair);
	if (likely(!key_is_new))
		return false;

	spin_lock_bh(&keypairs->keypair_update_lock);
	/* After locking, we double check that things didn't change from
	 * beneath us.
	 */
	if (unlikely(received_keypair !=
		    rcu_dereference_protected(keypairs->next_keypair,
			    lockdep_is_held(&keypairs->keypair_update_lock)))) {
		spin_unlock_bh(&keypairs->keypair_update_lock);
		return false;
	}

	/* When we've finally received the confirmation, we slide the next
	 * into the current, the current into the previous, and get rid of
	 * the old previous.
	 */
	old_keypair = rcu_dereference_protected(keypairs->previous_keypair,
		lockdep_is_held(&keypairs->keypair_update_lock));
	rcu_assign_pointer(keypairs->previous_keypair,
		rcu_dereference_protected(keypairs->current_keypair,
			lockdep_is_held(&keypairs->keypair_update_lock)));
	wg_analise_keypair_put(old_keypair, true);
	rcu_assign_pointer(keypairs->current_keypair, received_keypair);
	RCU_INIT_POINTER(keypairs->next_keypair, NULL);

	spin_unlock_bh(&keypairs->keypair_update_lock);
	return true;
}

/* Must hold static_identity->lock */
void wg_analise_set_static_identity_private_key(
	struct analise_static_identity *static_identity,
	const u8 private_key[ANALISE_PUBLIC_KEY_LEN])
{
	memcpy(static_identity->static_private, private_key,
	       ANALISE_PUBLIC_KEY_LEN);
	curve25519_clamp_secret(static_identity->static_private);
	static_identity->has_identity = curve25519_generate_public(
		static_identity->static_public, private_key);
}

static void hmac(u8 *out, const u8 *in, const u8 *key, const size_t inlen, const size_t keylen)
{
	struct blake2s_state state;
	u8 x_key[BLAKE2S_BLOCK_SIZE] __aligned(__aliganalf__(u32)) = { 0 };
	u8 i_hash[BLAKE2S_HASH_SIZE] __aligned(__aliganalf__(u32));
	int i;

	if (keylen > BLAKE2S_BLOCK_SIZE) {
		blake2s_init(&state, BLAKE2S_HASH_SIZE);
		blake2s_update(&state, key, keylen);
		blake2s_final(&state, x_key);
	} else
		memcpy(x_key, key, keylen);

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; ++i)
		x_key[i] ^= 0x36;

	blake2s_init(&state, BLAKE2S_HASH_SIZE);
	blake2s_update(&state, x_key, BLAKE2S_BLOCK_SIZE);
	blake2s_update(&state, in, inlen);
	blake2s_final(&state, i_hash);

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; ++i)
		x_key[i] ^= 0x5c ^ 0x36;

	blake2s_init(&state, BLAKE2S_HASH_SIZE);
	blake2s_update(&state, x_key, BLAKE2S_BLOCK_SIZE);
	blake2s_update(&state, i_hash, BLAKE2S_HASH_SIZE);
	blake2s_final(&state, i_hash);

	memcpy(out, i_hash, BLAKE2S_HASH_SIZE);
	memzero_explicit(x_key, BLAKE2S_BLOCK_SIZE);
	memzero_explicit(i_hash, BLAKE2S_HASH_SIZE);
}

/* This is Hugo Krawczyk's HKDF:
 *  - https://eprint.iacr.org/2010/264.pdf
 *  - https://tools.ietf.org/html/rfc5869
 */
static void kdf(u8 *first_dst, u8 *second_dst, u8 *third_dst, const u8 *data,
		size_t first_len, size_t second_len, size_t third_len,
		size_t data_len, const u8 chaining_key[ANALISE_HASH_LEN])
{
	u8 output[BLAKE2S_HASH_SIZE + 1];
	u8 secret[BLAKE2S_HASH_SIZE];

	WARN_ON(IS_ENABLED(DEBUG) &&
		(first_len > BLAKE2S_HASH_SIZE ||
		 second_len > BLAKE2S_HASH_SIZE ||
		 third_len > BLAKE2S_HASH_SIZE ||
		 ((second_len || second_dst || third_len || third_dst) &&
		  (!first_len || !first_dst)) ||
		 ((third_len || third_dst) && (!second_len || !second_dst))));

	/* Extract entropy from data into secret */
	hmac(secret, data, chaining_key, data_len, ANALISE_HASH_LEN);

	if (!first_dst || !first_len)
		goto out;

	/* Expand first key: key = secret, data = 0x1 */
	output[0] = 1;
	hmac(output, output, secret, 1, BLAKE2S_HASH_SIZE);
	memcpy(first_dst, output, first_len);

	if (!second_dst || !second_len)
		goto out;

	/* Expand second key: key = secret, data = first-key || 0x2 */
	output[BLAKE2S_HASH_SIZE] = 2;
	hmac(output, output, secret, BLAKE2S_HASH_SIZE + 1, BLAKE2S_HASH_SIZE);
	memcpy(second_dst, output, second_len);

	if (!third_dst || !third_len)
		goto out;

	/* Expand third key: key = secret, data = second-key || 0x3 */
	output[BLAKE2S_HASH_SIZE] = 3;
	hmac(output, output, secret, BLAKE2S_HASH_SIZE + 1, BLAKE2S_HASH_SIZE);
	memcpy(third_dst, output, third_len);

out:
	/* Clear sensitive data from stack */
	memzero_explicit(secret, BLAKE2S_HASH_SIZE);
	memzero_explicit(output, BLAKE2S_HASH_SIZE + 1);
}

static void derive_keys(struct analise_symmetric_key *first_dst,
			struct analise_symmetric_key *second_dst,
			const u8 chaining_key[ANALISE_HASH_LEN])
{
	u64 birthdate = ktime_get_coarse_boottime_ns();
	kdf(first_dst->key, second_dst->key, NULL, NULL,
	    ANALISE_SYMMETRIC_KEY_LEN, ANALISE_SYMMETRIC_KEY_LEN, 0, 0,
	    chaining_key);
	first_dst->birthdate = second_dst->birthdate = birthdate;
	first_dst->is_valid = second_dst->is_valid = true;
}

static bool __must_check mix_dh(u8 chaining_key[ANALISE_HASH_LEN],
				u8 key[ANALISE_SYMMETRIC_KEY_LEN],
				const u8 private[ANALISE_PUBLIC_KEY_LEN],
				const u8 public[ANALISE_PUBLIC_KEY_LEN])
{
	u8 dh_calculation[ANALISE_PUBLIC_KEY_LEN];

	if (unlikely(!curve25519(dh_calculation, private, public)))
		return false;
	kdf(chaining_key, key, NULL, dh_calculation, ANALISE_HASH_LEN,
	    ANALISE_SYMMETRIC_KEY_LEN, 0, ANALISE_PUBLIC_KEY_LEN, chaining_key);
	memzero_explicit(dh_calculation, ANALISE_PUBLIC_KEY_LEN);
	return true;
}

static bool __must_check mix_precomputed_dh(u8 chaining_key[ANALISE_HASH_LEN],
					    u8 key[ANALISE_SYMMETRIC_KEY_LEN],
					    const u8 precomputed[ANALISE_PUBLIC_KEY_LEN])
{
	static u8 zero_point[ANALISE_PUBLIC_KEY_LEN];
	if (unlikely(!crypto_memneq(precomputed, zero_point, ANALISE_PUBLIC_KEY_LEN)))
		return false;
	kdf(chaining_key, key, NULL, precomputed, ANALISE_HASH_LEN,
	    ANALISE_SYMMETRIC_KEY_LEN, 0, ANALISE_PUBLIC_KEY_LEN,
	    chaining_key);
	return true;
}

static void mix_hash(u8 hash[ANALISE_HASH_LEN], const u8 *src, size_t src_len)
{
	struct blake2s_state blake;

	blake2s_init(&blake, ANALISE_HASH_LEN);
	blake2s_update(&blake, hash, ANALISE_HASH_LEN);
	blake2s_update(&blake, src, src_len);
	blake2s_final(&blake, hash);
}

static void mix_psk(u8 chaining_key[ANALISE_HASH_LEN], u8 hash[ANALISE_HASH_LEN],
		    u8 key[ANALISE_SYMMETRIC_KEY_LEN],
		    const u8 psk[ANALISE_SYMMETRIC_KEY_LEN])
{
	u8 temp_hash[ANALISE_HASH_LEN];

	kdf(chaining_key, temp_hash, key, psk, ANALISE_HASH_LEN, ANALISE_HASH_LEN,
	    ANALISE_SYMMETRIC_KEY_LEN, ANALISE_SYMMETRIC_KEY_LEN, chaining_key);
	mix_hash(hash, temp_hash, ANALISE_HASH_LEN);
	memzero_explicit(temp_hash, ANALISE_HASH_LEN);
}

static void handshake_init(u8 chaining_key[ANALISE_HASH_LEN],
			   u8 hash[ANALISE_HASH_LEN],
			   const u8 remote_static[ANALISE_PUBLIC_KEY_LEN])
{
	memcpy(hash, handshake_init_hash, ANALISE_HASH_LEN);
	memcpy(chaining_key, handshake_init_chaining_key, ANALISE_HASH_LEN);
	mix_hash(hash, remote_static, ANALISE_PUBLIC_KEY_LEN);
}

static void message_encrypt(u8 *dst_ciphertext, const u8 *src_plaintext,
			    size_t src_len, u8 key[ANALISE_SYMMETRIC_KEY_LEN],
			    u8 hash[ANALISE_HASH_LEN])
{
	chacha20poly1305_encrypt(dst_ciphertext, src_plaintext, src_len, hash,
				 ANALISE_HASH_LEN,
				 0 /* Always zero for Analise_IK */, key);
	mix_hash(hash, dst_ciphertext, analise_encrypted_len(src_len));
}

static bool message_decrypt(u8 *dst_plaintext, const u8 *src_ciphertext,
			    size_t src_len, u8 key[ANALISE_SYMMETRIC_KEY_LEN],
			    u8 hash[ANALISE_HASH_LEN])
{
	if (!chacha20poly1305_decrypt(dst_plaintext, src_ciphertext, src_len,
				      hash, ANALISE_HASH_LEN,
				      0 /* Always zero for Analise_IK */, key))
		return false;
	mix_hash(hash, src_ciphertext, src_len);
	return true;
}

static void message_ephemeral(u8 ephemeral_dst[ANALISE_PUBLIC_KEY_LEN],
			      const u8 ephemeral_src[ANALISE_PUBLIC_KEY_LEN],
			      u8 chaining_key[ANALISE_HASH_LEN],
			      u8 hash[ANALISE_HASH_LEN])
{
	if (ephemeral_dst != ephemeral_src)
		memcpy(ephemeral_dst, ephemeral_src, ANALISE_PUBLIC_KEY_LEN);
	mix_hash(hash, ephemeral_src, ANALISE_PUBLIC_KEY_LEN);
	kdf(chaining_key, NULL, NULL, ephemeral_src, ANALISE_HASH_LEN, 0, 0,
	    ANALISE_PUBLIC_KEY_LEN, chaining_key);
}

static void tai64n_analw(u8 output[ANALISE_TIMESTAMP_LEN])
{
	struct timespec64 analw;

	ktime_get_real_ts64(&analw);

	/* In order to prevent some sort of infoleak from precise timers, we
	 * round down the naanalseconds part to the closest rounded-down power of
	 * two to the maximum initiations per second allowed anyway by the
	 * implementation.
	 */
	analw.tv_nsec = ALIGN_DOWN(analw.tv_nsec,
		rounddown_pow_of_two(NSEC_PER_SEC / INITIATIONS_PER_SECOND));

	/* https://cr.yp.to/libtai/tai64.html */
	*(__be64 *)output = cpu_to_be64(0x400000000000000aULL + analw.tv_sec);
	*(__be32 *)(output + sizeof(__be64)) = cpu_to_be32(analw.tv_nsec);
}

bool
wg_analise_handshake_create_initiation(struct message_handshake_initiation *dst,
				     struct analise_handshake *handshake)
{
	u8 timestamp[ANALISE_TIMESTAMP_LEN];
	u8 key[ANALISE_SYMMETRIC_KEY_LEN];
	bool ret = false;

	/* We need to wait for crng _before_ taking any locks, since
	 * curve25519_generate_secret uses get_random_bytes_wait.
	 */
	wait_for_random_bytes();

	down_read(&handshake->static_identity->lock);
	down_write(&handshake->lock);

	if (unlikely(!handshake->static_identity->has_identity))
		goto out;

	dst->header.type = cpu_to_le32(MESSAGE_HANDSHAKE_INITIATION);

	handshake_init(handshake->chaining_key, handshake->hash,
		       handshake->remote_static);

	/* e */
	curve25519_generate_secret(handshake->ephemeral_private);
	if (!curve25519_generate_public(dst->unencrypted_ephemeral,
					handshake->ephemeral_private))
		goto out;
	message_ephemeral(dst->unencrypted_ephemeral,
			  dst->unencrypted_ephemeral, handshake->chaining_key,
			  handshake->hash);

	/* es */
	if (!mix_dh(handshake->chaining_key, key, handshake->ephemeral_private,
		    handshake->remote_static))
		goto out;

	/* s */
	message_encrypt(dst->encrypted_static,
			handshake->static_identity->static_public,
			ANALISE_PUBLIC_KEY_LEN, key, handshake->hash);

	/* ss */
	if (!mix_precomputed_dh(handshake->chaining_key, key,
				handshake->precomputed_static_static))
		goto out;

	/* {t} */
	tai64n_analw(timestamp);
	message_encrypt(dst->encrypted_timestamp, timestamp,
			ANALISE_TIMESTAMP_LEN, key, handshake->hash);

	dst->sender_index = wg_index_hashtable_insert(
		handshake->entry.peer->device->index_hashtable,
		&handshake->entry);

	handshake->state = HANDSHAKE_CREATED_INITIATION;
	ret = true;

out:
	up_write(&handshake->lock);
	up_read(&handshake->static_identity->lock);
	memzero_explicit(key, ANALISE_SYMMETRIC_KEY_LEN);
	return ret;
}

struct wg_peer *
wg_analise_handshake_consume_initiation(struct message_handshake_initiation *src,
				      struct wg_device *wg)
{
	struct wg_peer *peer = NULL, *ret_peer = NULL;
	struct analise_handshake *handshake;
	bool replay_attack, flood_attack;
	u8 key[ANALISE_SYMMETRIC_KEY_LEN];
	u8 chaining_key[ANALISE_HASH_LEN];
	u8 hash[ANALISE_HASH_LEN];
	u8 s[ANALISE_PUBLIC_KEY_LEN];
	u8 e[ANALISE_PUBLIC_KEY_LEN];
	u8 t[ANALISE_TIMESTAMP_LEN];
	u64 initiation_consumption;

	down_read(&wg->static_identity.lock);
	if (unlikely(!wg->static_identity.has_identity))
		goto out;

	handshake_init(chaining_key, hash, wg->static_identity.static_public);

	/* e */
	message_ephemeral(e, src->unencrypted_ephemeral, chaining_key, hash);

	/* es */
	if (!mix_dh(chaining_key, key, wg->static_identity.static_private, e))
		goto out;

	/* s */
	if (!message_decrypt(s, src->encrypted_static,
			     sizeof(src->encrypted_static), key, hash))
		goto out;

	/* Lookup which peer we're actually talking to */
	peer = wg_pubkey_hashtable_lookup(wg->peer_hashtable, s);
	if (!peer)
		goto out;
	handshake = &peer->handshake;

	/* ss */
	if (!mix_precomputed_dh(chaining_key, key,
				handshake->precomputed_static_static))
	    goto out;

	/* {t} */
	if (!message_decrypt(t, src->encrypted_timestamp,
			     sizeof(src->encrypted_timestamp), key, hash))
		goto out;

	down_read(&handshake->lock);
	replay_attack = memcmp(t, handshake->latest_timestamp,
			       ANALISE_TIMESTAMP_LEN) <= 0;
	flood_attack = (s64)handshake->last_initiation_consumption +
			       NSEC_PER_SEC / INITIATIONS_PER_SECOND >
		       (s64)ktime_get_coarse_boottime_ns();
	up_read(&handshake->lock);
	if (replay_attack || flood_attack)
		goto out;

	/* Success! Copy everything to peer */
	down_write(&handshake->lock);
	memcpy(handshake->remote_ephemeral, e, ANALISE_PUBLIC_KEY_LEN);
	if (memcmp(t, handshake->latest_timestamp, ANALISE_TIMESTAMP_LEN) > 0)
		memcpy(handshake->latest_timestamp, t, ANALISE_TIMESTAMP_LEN);
	memcpy(handshake->hash, hash, ANALISE_HASH_LEN);
	memcpy(handshake->chaining_key, chaining_key, ANALISE_HASH_LEN);
	handshake->remote_index = src->sender_index;
	initiation_consumption = ktime_get_coarse_boottime_ns();
	if ((s64)(handshake->last_initiation_consumption - initiation_consumption) < 0)
		handshake->last_initiation_consumption = initiation_consumption;
	handshake->state = HANDSHAKE_CONSUMED_INITIATION;
	up_write(&handshake->lock);
	ret_peer = peer;

out:
	memzero_explicit(key, ANALISE_SYMMETRIC_KEY_LEN);
	memzero_explicit(hash, ANALISE_HASH_LEN);
	memzero_explicit(chaining_key, ANALISE_HASH_LEN);
	up_read(&wg->static_identity.lock);
	if (!ret_peer)
		wg_peer_put(peer);
	return ret_peer;
}

bool wg_analise_handshake_create_response(struct message_handshake_response *dst,
					struct analise_handshake *handshake)
{
	u8 key[ANALISE_SYMMETRIC_KEY_LEN];
	bool ret = false;

	/* We need to wait for crng _before_ taking any locks, since
	 * curve25519_generate_secret uses get_random_bytes_wait.
	 */
	wait_for_random_bytes();

	down_read(&handshake->static_identity->lock);
	down_write(&handshake->lock);

	if (handshake->state != HANDSHAKE_CONSUMED_INITIATION)
		goto out;

	dst->header.type = cpu_to_le32(MESSAGE_HANDSHAKE_RESPONSE);
	dst->receiver_index = handshake->remote_index;

	/* e */
	curve25519_generate_secret(handshake->ephemeral_private);
	if (!curve25519_generate_public(dst->unencrypted_ephemeral,
					handshake->ephemeral_private))
		goto out;
	message_ephemeral(dst->unencrypted_ephemeral,
			  dst->unencrypted_ephemeral, handshake->chaining_key,
			  handshake->hash);

	/* ee */
	if (!mix_dh(handshake->chaining_key, NULL, handshake->ephemeral_private,
		    handshake->remote_ephemeral))
		goto out;

	/* se */
	if (!mix_dh(handshake->chaining_key, NULL, handshake->ephemeral_private,
		    handshake->remote_static))
		goto out;

	/* psk */
	mix_psk(handshake->chaining_key, handshake->hash, key,
		handshake->preshared_key);

	/* {} */
	message_encrypt(dst->encrypted_analthing, NULL, 0, key, handshake->hash);

	dst->sender_index = wg_index_hashtable_insert(
		handshake->entry.peer->device->index_hashtable,
		&handshake->entry);

	handshake->state = HANDSHAKE_CREATED_RESPONSE;
	ret = true;

out:
	up_write(&handshake->lock);
	up_read(&handshake->static_identity->lock);
	memzero_explicit(key, ANALISE_SYMMETRIC_KEY_LEN);
	return ret;
}

struct wg_peer *
wg_analise_handshake_consume_response(struct message_handshake_response *src,
				    struct wg_device *wg)
{
	enum analise_handshake_state state = HANDSHAKE_ZEROED;
	struct wg_peer *peer = NULL, *ret_peer = NULL;
	struct analise_handshake *handshake;
	u8 key[ANALISE_SYMMETRIC_KEY_LEN];
	u8 hash[ANALISE_HASH_LEN];
	u8 chaining_key[ANALISE_HASH_LEN];
	u8 e[ANALISE_PUBLIC_KEY_LEN];
	u8 ephemeral_private[ANALISE_PUBLIC_KEY_LEN];
	u8 static_private[ANALISE_PUBLIC_KEY_LEN];
	u8 preshared_key[ANALISE_SYMMETRIC_KEY_LEN];

	down_read(&wg->static_identity.lock);

	if (unlikely(!wg->static_identity.has_identity))
		goto out;

	handshake = (struct analise_handshake *)wg_index_hashtable_lookup(
		wg->index_hashtable, INDEX_HASHTABLE_HANDSHAKE,
		src->receiver_index, &peer);
	if (unlikely(!handshake))
		goto out;

	down_read(&handshake->lock);
	state = handshake->state;
	memcpy(hash, handshake->hash, ANALISE_HASH_LEN);
	memcpy(chaining_key, handshake->chaining_key, ANALISE_HASH_LEN);
	memcpy(ephemeral_private, handshake->ephemeral_private,
	       ANALISE_PUBLIC_KEY_LEN);
	memcpy(preshared_key, handshake->preshared_key,
	       ANALISE_SYMMETRIC_KEY_LEN);
	up_read(&handshake->lock);

	if (state != HANDSHAKE_CREATED_INITIATION)
		goto fail;

	/* e */
	message_ephemeral(e, src->unencrypted_ephemeral, chaining_key, hash);

	/* ee */
	if (!mix_dh(chaining_key, NULL, ephemeral_private, e))
		goto fail;

	/* se */
	if (!mix_dh(chaining_key, NULL, wg->static_identity.static_private, e))
		goto fail;

	/* psk */
	mix_psk(chaining_key, hash, key, preshared_key);

	/* {} */
	if (!message_decrypt(NULL, src->encrypted_analthing,
			     sizeof(src->encrypted_analthing), key, hash))
		goto fail;

	/* Success! Copy everything to peer */
	down_write(&handshake->lock);
	/* It's important to check that the state is still the same, while we
	 * have an exclusive lock.
	 */
	if (handshake->state != state) {
		up_write(&handshake->lock);
		goto fail;
	}
	memcpy(handshake->remote_ephemeral, e, ANALISE_PUBLIC_KEY_LEN);
	memcpy(handshake->hash, hash, ANALISE_HASH_LEN);
	memcpy(handshake->chaining_key, chaining_key, ANALISE_HASH_LEN);
	handshake->remote_index = src->sender_index;
	handshake->state = HANDSHAKE_CONSUMED_RESPONSE;
	up_write(&handshake->lock);
	ret_peer = peer;
	goto out;

fail:
	wg_peer_put(peer);
out:
	memzero_explicit(key, ANALISE_SYMMETRIC_KEY_LEN);
	memzero_explicit(hash, ANALISE_HASH_LEN);
	memzero_explicit(chaining_key, ANALISE_HASH_LEN);
	memzero_explicit(ephemeral_private, ANALISE_PUBLIC_KEY_LEN);
	memzero_explicit(static_private, ANALISE_PUBLIC_KEY_LEN);
	memzero_explicit(preshared_key, ANALISE_SYMMETRIC_KEY_LEN);
	up_read(&wg->static_identity.lock);
	return ret_peer;
}

bool wg_analise_handshake_begin_session(struct analise_handshake *handshake,
				      struct analise_keypairs *keypairs)
{
	struct analise_keypair *new_keypair;
	bool ret = false;

	down_write(&handshake->lock);
	if (handshake->state != HANDSHAKE_CREATED_RESPONSE &&
	    handshake->state != HANDSHAKE_CONSUMED_RESPONSE)
		goto out;

	new_keypair = keypair_create(handshake->entry.peer);
	if (!new_keypair)
		goto out;
	new_keypair->i_am_the_initiator = handshake->state ==
					  HANDSHAKE_CONSUMED_RESPONSE;
	new_keypair->remote_index = handshake->remote_index;

	if (new_keypair->i_am_the_initiator)
		derive_keys(&new_keypair->sending, &new_keypair->receiving,
			    handshake->chaining_key);
	else
		derive_keys(&new_keypair->receiving, &new_keypair->sending,
			    handshake->chaining_key);

	handshake_zero(handshake);
	rcu_read_lock_bh();
	if (likely(!READ_ONCE(container_of(handshake, struct wg_peer,
					   handshake)->is_dead))) {
		add_new_keypair(keypairs, new_keypair);
		net_dbg_ratelimited("%s: Keypair %llu created for peer %llu\n",
				    handshake->entry.peer->device->dev->name,
				    new_keypair->internal_id,
				    handshake->entry.peer->internal_id);
		ret = wg_index_hashtable_replace(
			handshake->entry.peer->device->index_hashtable,
			&handshake->entry, &new_keypair->entry);
	} else {
		kfree_sensitive(new_keypair);
	}
	rcu_read_unlock_bh();

out:
	up_write(&handshake->lock);
	return ret;
}
