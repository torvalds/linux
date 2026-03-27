// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Hannes Reinecke, SUSE Linux
 */

#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/base64.h>
#include <linux/prandom.h>
#include <linux/scatterlist.h>
#include <linux/unaligned.h>
#include <crypto/dh.h>
#include <crypto/sha2.h>
#include <linux/nvme.h>
#include <linux/nvme-auth.h>

static u32 nvme_dhchap_seqnum;
static DEFINE_MUTEX(nvme_dhchap_mutex);

u32 nvme_auth_get_seqnum(void)
{
	u32 seqnum;

	mutex_lock(&nvme_dhchap_mutex);
	if (!nvme_dhchap_seqnum)
		nvme_dhchap_seqnum = get_random_u32();
	else {
		nvme_dhchap_seqnum++;
		if (!nvme_dhchap_seqnum)
			nvme_dhchap_seqnum++;
	}
	seqnum = nvme_dhchap_seqnum;
	mutex_unlock(&nvme_dhchap_mutex);
	return seqnum;
}
EXPORT_SYMBOL_GPL(nvme_auth_get_seqnum);

static const struct nvme_auth_dhgroup_map {
	char name[16];
	char kpp[16];
} dhgroup_map[] = {
	[NVME_AUTH_DHGROUP_NULL] = {
		.name = "null", .kpp = "null" },
	[NVME_AUTH_DHGROUP_2048] = {
		.name = "ffdhe2048", .kpp = "ffdhe2048(dh)" },
	[NVME_AUTH_DHGROUP_3072] = {
		.name = "ffdhe3072", .kpp = "ffdhe3072(dh)" },
	[NVME_AUTH_DHGROUP_4096] = {
		.name = "ffdhe4096", .kpp = "ffdhe4096(dh)" },
	[NVME_AUTH_DHGROUP_6144] = {
		.name = "ffdhe6144", .kpp = "ffdhe6144(dh)" },
	[NVME_AUTH_DHGROUP_8192] = {
		.name = "ffdhe8192", .kpp = "ffdhe8192(dh)" },
};

const char *nvme_auth_dhgroup_name(u8 dhgroup_id)
{
	if (dhgroup_id >= ARRAY_SIZE(dhgroup_map))
		return NULL;
	return dhgroup_map[dhgroup_id].name;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_name);

const char *nvme_auth_dhgroup_kpp(u8 dhgroup_id)
{
	if (dhgroup_id >= ARRAY_SIZE(dhgroup_map))
		return NULL;
	return dhgroup_map[dhgroup_id].kpp;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_kpp);

u8 nvme_auth_dhgroup_id(const char *dhgroup_name)
{
	int i;

	if (!dhgroup_name || !strlen(dhgroup_name))
		return NVME_AUTH_DHGROUP_INVALID;
	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (!strlen(dhgroup_map[i].name))
			continue;
		if (!strncmp(dhgroup_map[i].name, dhgroup_name,
			     strlen(dhgroup_map[i].name)))
			return i;
	}
	return NVME_AUTH_DHGROUP_INVALID;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_id);

static const struct nvme_dhchap_hash_map {
	int len;
	char hmac[15];
} hash_map[] = {
	[NVME_AUTH_HASH_SHA256] = {
		.len = 32,
		.hmac = "hmac(sha256)",
	},
	[NVME_AUTH_HASH_SHA384] = {
		.len = 48,
		.hmac = "hmac(sha384)",
	},
	[NVME_AUTH_HASH_SHA512] = {
		.len = 64,
		.hmac = "hmac(sha512)",
	},
};

const char *nvme_auth_hmac_name(u8 hmac_id)
{
	if (hmac_id >= ARRAY_SIZE(hash_map))
		return NULL;
	return hash_map[hmac_id].hmac;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_name);

u8 nvme_auth_hmac_id(const char *hmac_name)
{
	int i;

	if (!hmac_name || !strlen(hmac_name))
		return NVME_AUTH_HASH_INVALID;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (!strlen(hash_map[i].hmac))
			continue;
		if (!strncmp(hash_map[i].hmac, hmac_name,
			     strlen(hash_map[i].hmac)))
			return i;
	}
	return NVME_AUTH_HASH_INVALID;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_id);

size_t nvme_auth_hmac_hash_len(u8 hmac_id)
{
	if (hmac_id >= ARRAY_SIZE(hash_map))
		return 0;
	return hash_map[hmac_id].len;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_hash_len);

u32 nvme_auth_key_struct_size(u32 key_len)
{
	struct nvme_dhchap_key key;

	return struct_size(&key, key, key_len);
}
EXPORT_SYMBOL_GPL(nvme_auth_key_struct_size);

struct nvme_dhchap_key *nvme_auth_extract_key(const char *secret, u8 key_hash)
{
	struct nvme_dhchap_key *key;
	const char *p;
	u32 crc;
	int ret, key_len;
	size_t allocated_len = strlen(secret);

	/* Secret might be affixed with a ':' */
	p = strrchr(secret, ':');
	if (p)
		allocated_len = p - secret;
	key = nvme_auth_alloc_key(allocated_len, 0);
	if (!key)
		return ERR_PTR(-ENOMEM);

	key_len = base64_decode(secret, allocated_len, key->key, true, BASE64_STD);
	if (key_len < 0) {
		pr_debug("base64 key decoding error %d\n",
			 key_len);
		ret = key_len;
		goto out_free_key;
	}

	if (key_len != 36 && key_len != 52 &&
	    key_len != 68) {
		pr_err("Invalid key len %d\n", key_len);
		ret = -EINVAL;
		goto out_free_key;
	}

	/* The last four bytes is the CRC in little-endian format */
	key_len -= 4;
	/*
	 * The linux implementation doesn't do pre- and post-increments,
	 * so we have to do it manually.
	 */
	crc = ~crc32(~0, key->key, key_len);

	if (get_unaligned_le32(key->key + key_len) != crc) {
		pr_err("key crc mismatch (key %08x, crc %08x)\n",
		       get_unaligned_le32(key->key + key_len), crc);
		ret = -EKEYREJECTED;
		goto out_free_key;
	}
	key->len = key_len;
	key->hash = key_hash;
	return key;
out_free_key:
	nvme_auth_free_key(key);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(nvme_auth_extract_key);

struct nvme_dhchap_key *nvme_auth_alloc_key(u32 len, u8 hash)
{
	u32 num_bytes = nvme_auth_key_struct_size(len);
	struct nvme_dhchap_key *key = kzalloc(num_bytes, GFP_KERNEL);

	if (key) {
		key->len = len;
		key->hash = hash;
	}
	return key;
}
EXPORT_SYMBOL_GPL(nvme_auth_alloc_key);

void nvme_auth_free_key(struct nvme_dhchap_key *key)
{
	if (!key)
		return;
	kfree_sensitive(key);
}
EXPORT_SYMBOL_GPL(nvme_auth_free_key);

/*
 * Start computing an HMAC value, given the algorithm ID and raw key.
 *
 * The context should be zeroized at the end of its lifetime.  The caller can do
 * that implicitly by calling nvme_auth_hmac_final(), or explicitly (needed when
 * a context is abandoned without finalizing it) by calling memzero_explicit().
 */
int nvme_auth_hmac_init(struct nvme_auth_hmac_ctx *hmac, u8 hmac_id,
			const u8 *key, size_t key_len)
{
	hmac->hmac_id = hmac_id;
	switch (hmac_id) {
	case NVME_AUTH_HASH_SHA256:
		hmac_sha256_init_usingrawkey(&hmac->sha256, key, key_len);
		return 0;
	case NVME_AUTH_HASH_SHA384:
		hmac_sha384_init_usingrawkey(&hmac->sha384, key, key_len);
		return 0;
	case NVME_AUTH_HASH_SHA512:
		hmac_sha512_init_usingrawkey(&hmac->sha512, key, key_len);
		return 0;
	}
	pr_warn("%s: invalid hash algorithm %d\n", __func__, hmac_id);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_init);

void nvme_auth_hmac_update(struct nvme_auth_hmac_ctx *hmac, const u8 *data,
			   size_t data_len)
{
	switch (hmac->hmac_id) {
	case NVME_AUTH_HASH_SHA256:
		hmac_sha256_update(&hmac->sha256, data, data_len);
		return;
	case NVME_AUTH_HASH_SHA384:
		hmac_sha384_update(&hmac->sha384, data, data_len);
		return;
	case NVME_AUTH_HASH_SHA512:
		hmac_sha512_update(&hmac->sha512, data, data_len);
		return;
	}
	/* Unreachable because nvme_auth_hmac_init() validated hmac_id */
	WARN_ON_ONCE(1);
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_update);

/* Finish computing an HMAC value.  Note that this zeroizes the HMAC context. */
void nvme_auth_hmac_final(struct nvme_auth_hmac_ctx *hmac, u8 *out)
{
	switch (hmac->hmac_id) {
	case NVME_AUTH_HASH_SHA256:
		hmac_sha256_final(&hmac->sha256, out);
		return;
	case NVME_AUTH_HASH_SHA384:
		hmac_sha384_final(&hmac->sha384, out);
		return;
	case NVME_AUTH_HASH_SHA512:
		hmac_sha512_final(&hmac->sha512, out);
		return;
	}
	/* Unreachable because nvme_auth_hmac_init() validated hmac_id */
	WARN_ON_ONCE(1);
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_final);

static int nvme_auth_hmac(u8 hmac_id, const u8 *key, size_t key_len,
			  const u8 *data, size_t data_len, u8 *out)
{
	struct nvme_auth_hmac_ctx hmac;
	int ret;

	ret = nvme_auth_hmac_init(&hmac, hmac_id, key, key_len);
	if (ret == 0) {
		nvme_auth_hmac_update(&hmac, data, data_len);
		nvme_auth_hmac_final(&hmac, out);
	}
	return ret;
}

static int nvme_auth_hash(u8 hmac_id, const u8 *data, size_t data_len, u8 *out)
{
	switch (hmac_id) {
	case NVME_AUTH_HASH_SHA256:
		sha256(data, data_len, out);
		return 0;
	case NVME_AUTH_HASH_SHA384:
		sha384(data, data_len, out);
		return 0;
	case NVME_AUTH_HASH_SHA512:
		sha512(data, data_len, out);
		return 0;
	}
	pr_warn("%s: invalid hash algorithm %d\n", __func__, hmac_id);
	return -EINVAL;
}

struct nvme_dhchap_key *nvme_auth_transform_key(
		const struct nvme_dhchap_key *key, const char *nqn)
{
	struct nvme_auth_hmac_ctx hmac;
	struct nvme_dhchap_key *transformed_key;
	int ret, key_len;

	if (!key) {
		pr_warn("No key specified\n");
		return ERR_PTR(-ENOKEY);
	}
	if (key->hash == 0) {
		key_len = nvme_auth_key_struct_size(key->len);
		transformed_key = kmemdup(key, key_len, GFP_KERNEL);
		if (!transformed_key)
			return ERR_PTR(-ENOMEM);
		return transformed_key;
	}
	ret = nvme_auth_hmac_init(&hmac, key->hash, key->key, key->len);
	if (ret)
		return ERR_PTR(ret);
	key_len = nvme_auth_hmac_hash_len(key->hash);
	transformed_key = nvme_auth_alloc_key(key_len, key->hash);
	if (!transformed_key) {
		memzero_explicit(&hmac, sizeof(hmac));
		return ERR_PTR(-ENOMEM);
	}
	nvme_auth_hmac_update(&hmac, nqn, strlen(nqn));
	nvme_auth_hmac_update(&hmac, "NVMe-over-Fabrics", 17);
	nvme_auth_hmac_final(&hmac, transformed_key->key);
	return transformed_key;
}
EXPORT_SYMBOL_GPL(nvme_auth_transform_key);

int nvme_auth_augmented_challenge(u8 hmac_id, const u8 *skey, size_t skey_len,
				  const u8 *challenge, u8 *aug, size_t hlen)
{
	u8 hashed_key[NVME_AUTH_MAX_DIGEST_SIZE];
	int ret;

	ret = nvme_auth_hash(hmac_id, skey, skey_len, hashed_key);
	if (ret)
		return ret;
	ret = nvme_auth_hmac(hmac_id, hashed_key, hlen, challenge, hlen, aug);
	memzero_explicit(hashed_key, sizeof(hashed_key));
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_augmented_challenge);

int nvme_auth_gen_privkey(struct crypto_kpp *dh_tfm, u8 dh_gid)
{
	int ret;

	ret = crypto_kpp_set_secret(dh_tfm, NULL, 0);
	if (ret)
		pr_debug("failed to set private key, error %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_privkey);

int nvme_auth_gen_pubkey(struct crypto_kpp *dh_tfm,
		u8 *host_key, size_t host_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, host_key, host_key_len);
	kpp_request_set_output(req, &dst, host_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);
	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_pubkey);

int nvme_auth_gen_shared_secret(struct crypto_kpp *dh_tfm,
		const u8 *ctrl_key, size_t ctrl_key_len,
		u8 *sess_key, size_t sess_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	sg_init_one(&src, ctrl_key, ctrl_key_len);
	kpp_request_set_input(req, &src, ctrl_key_len);
	sg_init_one(&dst, sess_key, sess_key_len);
	kpp_request_set_output(req, &dst, sess_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);

	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_shared_secret);

int nvme_auth_parse_key(const char *secret, struct nvme_dhchap_key **ret_key)
{
	struct nvme_dhchap_key *key;
	u8 key_hash;

	if (!secret) {
		*ret_key = NULL;
		return 0;
	}

	if (sscanf(secret, "DHHC-1:%hhd:%*s:", &key_hash) != 1)
		return -EINVAL;

	/* Pass in the secret without the 'DHHC-1:XX:' prefix */
	key = nvme_auth_extract_key(secret + 10, key_hash);
	if (IS_ERR(key)) {
		*ret_key = NULL;
		return PTR_ERR(key);
	}

	*ret_key = key;
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_parse_key);

/**
 * nvme_auth_generate_psk - Generate a PSK for TLS
 * @hmac_id: Hash function identifier
 * @skey: Session key
 * @skey_len: Length of @skey
 * @c1: Value of challenge C1
 * @c2: Value of challenge C2
 * @hash_len: Hash length of the hash algorithm
 * @ret_psk: Pointer to the resulting generated PSK
 * @ret_len: length of @ret_psk
 *
 * Generate a PSK for TLS as specified in NVMe base specification, section
 * 8.13.5.9: Generated PSK for TLS
 *
 * The generated PSK for TLS shall be computed applying the HMAC function
 * using the hash function H( ) selected by the HashID parameter in the
 * DH-HMAC-CHAP_Challenge message with the session key KS as key to the
 * concatenation of the two challenges C1 and C2 (i.e., generated
 * PSK = HMAC(KS, C1 || C2)).
 *
 * Returns 0 on success with a valid generated PSK pointer in @ret_psk and
 * the length of @ret_psk in @ret_len, or a negative error number otherwise.
 */
int nvme_auth_generate_psk(u8 hmac_id, const u8 *skey, size_t skey_len,
			   const u8 *c1, const u8 *c2, size_t hash_len,
			   u8 **ret_psk, size_t *ret_len)
{
	size_t psk_len = nvme_auth_hmac_hash_len(hmac_id);
	struct nvme_auth_hmac_ctx hmac;
	u8 *psk;
	int ret;

	if (!c1 || !c2)
		return -EINVAL;

	ret = nvme_auth_hmac_init(&hmac, hmac_id, skey, skey_len);
	if (ret)
		return ret;
	psk = kzalloc(psk_len, GFP_KERNEL);
	if (!psk) {
		memzero_explicit(&hmac, sizeof(hmac));
		return -ENOMEM;
	}
	nvme_auth_hmac_update(&hmac, c1, hash_len);
	nvme_auth_hmac_update(&hmac, c2, hash_len);
	nvme_auth_hmac_final(&hmac, psk);
	*ret_psk = psk;
	*ret_len = psk_len;
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_generate_psk);

/**
 * nvme_auth_generate_digest - Generate TLS PSK digest
 * @hmac_id: Hash function identifier
 * @psk: Generated input PSK
 * @psk_len: Length of @psk
 * @subsysnqn: NQN of the subsystem
 * @hostnqn: NQN of the host
 * @ret_digest: Pointer to the returned digest
 *
 * Generate a TLS PSK digest as specified in TP8018 Section 3.6.1.3:
 *   TLS PSK and PSK identity Derivation
 *
 * The PSK digest shall be computed by encoding in Base64 (refer to RFC
 * 4648) the result of the application of the HMAC function using the hash
 * function specified in item 4 above (ie the hash function of the cipher
 * suite associated with the PSK identity) with the PSK as HMAC key to the
 * concatenation of:
 * - the NQN of the host (i.e., NQNh) not including the null terminator;
 * - a space character;
 * - the NQN of the NVM subsystem (i.e., NQNc) not including the null
 *   terminator;
 * - a space character; and
 * - the seventeen ASCII characters "NVMe-over-Fabrics"
 * (i.e., <PSK digest> = Base64(HMAC(PSK, NQNh || " " || NQNc || " " ||
 *  "NVMe-over-Fabrics"))).
 * The length of the PSK digest depends on the hash function used to compute
 * it as follows:
 * - If the SHA-256 hash function is used, the resulting PSK digest is 44
 *   characters long; or
 * - If the SHA-384 hash function is used, the resulting PSK digest is 64
 *   characters long.
 *
 * Returns 0 on success with a valid digest pointer in @ret_digest, or a
 * negative error number on failure.
 */
int nvme_auth_generate_digest(u8 hmac_id, const u8 *psk, size_t psk_len,
			      const char *subsysnqn, const char *hostnqn,
			      char **ret_digest)
{
	struct nvme_auth_hmac_ctx hmac;
	u8 digest[NVME_AUTH_MAX_DIGEST_SIZE];
	size_t hash_len = nvme_auth_hmac_hash_len(hmac_id);
	char *enc;
	size_t enc_len;
	int ret;

	if (WARN_ON(!subsysnqn || !hostnqn))
		return -EINVAL;

	if (hash_len == 0) {
		pr_warn("%s: invalid hash algorithm %d\n",
			__func__, hmac_id);
		return -EINVAL;
	}

	switch (hash_len) {
	case 32:
		enc_len = 44;
		break;
	case 48:
		enc_len = 64;
		break;
	default:
		pr_warn("%s: invalid hash algorithm '%s'\n",
			__func__, nvme_auth_hmac_name(hmac_id));
		return -EINVAL;
	}

	enc = kzalloc(enc_len + 1, GFP_KERNEL);
	if (!enc) {
		ret = -ENOMEM;
		goto out;
	}

	ret = nvme_auth_hmac_init(&hmac, hmac_id, psk, psk_len);
	if (ret)
		goto out;
	nvme_auth_hmac_update(&hmac, hostnqn, strlen(hostnqn));
	nvme_auth_hmac_update(&hmac, " ", 1);
	nvme_auth_hmac_update(&hmac, subsysnqn, strlen(subsysnqn));
	nvme_auth_hmac_update(&hmac, " NVMe-over-Fabrics", 18);
	nvme_auth_hmac_final(&hmac, digest);

	ret = base64_encode(digest, hash_len, enc, true, BASE64_STD);
	if (ret < enc_len) {
		ret = -ENOKEY;
		goto out;
	}
	*ret_digest = enc;
	ret = 0;

out:
	if (ret)
		kfree_sensitive(enc);
	memzero_explicit(digest, sizeof(digest));
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_generate_digest);

/**
 * nvme_auth_derive_tls_psk - Derive TLS PSK
 * @hmac_id: Hash function identifier
 * @psk: generated input PSK
 * @psk_len: size of @psk
 * @psk_digest: TLS PSK digest
 * @ret_psk: Pointer to the resulting TLS PSK
 *
 * Derive a TLS PSK as specified in TP8018 Section 3.6.1.3:
 *   TLS PSK and PSK identity Derivation
 *
 * The TLS PSK shall be derived as follows from an input PSK
 * (i.e., either a retained PSK or a generated PSK) and a PSK
 * identity using the HKDF-Extract and HKDF-Expand-Label operations
 * (refer to RFC 5869 and RFC 8446) where the hash function is the
 * one specified by the hash specifier of the PSK identity:
 * 1. PRK = HKDF-Extract(0, Input PSK); and
 * 2. TLS PSK = HKDF-Expand-Label(PRK, "nvme-tls-psk", PskIdentityContext, L),
 * where PskIdentityContext is the hash identifier indicated in
 * the PSK identity concatenated to a space character and to the
 * Base64 PSK digest (i.e., "<hash> <PSK digest>") and L is the
 * output size in bytes of the hash function (i.e., 32 for SHA-256
 * and 48 for SHA-384).
 *
 * Returns 0 on success with a valid psk pointer in @ret_psk or a negative
 * error number otherwise.
 */
int nvme_auth_derive_tls_psk(int hmac_id, const u8 *psk, size_t psk_len,
			     const char *psk_digest, u8 **ret_psk)
{
	static const u8 default_salt[NVME_AUTH_MAX_DIGEST_SIZE];
	static const char label[] = "tls13 nvme-tls-psk";
	const size_t label_len = sizeof(label) - 1;
	u8 prk[NVME_AUTH_MAX_DIGEST_SIZE];
	size_t hash_len, ctx_len;
	u8 *hmac_data = NULL, *tls_key;
	size_t i;
	int ret;

	hash_len = nvme_auth_hmac_hash_len(hmac_id);
	if (hash_len == 0) {
		pr_warn("%s: invalid hash algorithm %d\n",
			__func__, hmac_id);
		return -EINVAL;
	}
	if (hmac_id == NVME_AUTH_HASH_SHA512) {
		pr_warn("%s: unsupported hash algorithm %s\n",
			__func__, nvme_auth_hmac_name(hmac_id));
		return -EINVAL;
	}

	if (psk_len != hash_len) {
		pr_warn("%s: unexpected psk_len %zu\n", __func__, psk_len);
		return -EINVAL;
	}

	/* HKDF-Extract */
	ret = nvme_auth_hmac(hmac_id, default_salt, hash_len, psk, psk_len,
			     prk);
	if (ret)
		goto out;

	/*
	 * HKDF-Expand-Label (RFC 8446 section 7.1), with output length equal to
	 * the hash length (so only a single HMAC operation is needed)
	 */

	hmac_data = kmalloc(/* output length */ 2 +
			    /* label */ 1 + label_len +
			    /* context (max) */ 1 + 3 + 1 + strlen(psk_digest) +
			    /* counter */ 1,
			    GFP_KERNEL);
	if (!hmac_data) {
		ret = -ENOMEM;
		goto out;
	}
	/* output length */
	i = 0;
	hmac_data[i++] = hash_len >> 8;
	hmac_data[i++] = hash_len;

	/* label */
	static_assert(label_len <= 255);
	hmac_data[i] = label_len;
	memcpy(&hmac_data[i + 1], label, label_len);
	i += 1 + label_len;

	/* context */
	ctx_len = sprintf(&hmac_data[i + 1], "%02d %s", hmac_id, psk_digest);
	if (ctx_len > 255) {
		ret = -EINVAL;
		goto out;
	}
	hmac_data[i] = ctx_len;
	i += 1 + ctx_len;

	/* counter (this overwrites the NUL terminator written by sprintf) */
	hmac_data[i++] = 1;

	tls_key = kzalloc(psk_len, GFP_KERNEL);
	if (!tls_key) {
		ret = -ENOMEM;
		goto out;
	}
	ret = nvme_auth_hmac(hmac_id, prk, hash_len, hmac_data, i, tls_key);
	if (ret) {
		kfree_sensitive(tls_key);
		goto out;
	}
	*ret_psk = tls_key;
out:
	kfree_sensitive(hmac_data);
	memzero_explicit(prk, sizeof(prk));
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_derive_tls_psk);

MODULE_DESCRIPTION("NVMe Authentication framework");
MODULE_LICENSE("GPL v2");
