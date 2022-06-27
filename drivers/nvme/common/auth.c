// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Hannes Reinecke, SUSE Linux
 */

#include <linux/module.h>
#include <linux/crc32.h>
#include <linux/base64.h>
#include <linux/prandom.h>
#include <linux/scatterlist.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/dh.h>
#include <linux/nvme.h>
#include <linux/nvme-auth.h>

static u32 nvme_dhchap_seqnum;
static DEFINE_MUTEX(nvme_dhchap_mutex);

u32 nvme_auth_get_seqnum(void)
{
	u32 seqnum;

	mutex_lock(&nvme_dhchap_mutex);
	if (!nvme_dhchap_seqnum)
		nvme_dhchap_seqnum = prandom_u32();
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

static struct nvme_auth_dhgroup_map {
	const char name[16];
	const char kpp[16];
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
	if (dhgroup_id > ARRAY_SIZE(dhgroup_map))
		return NULL;
	return dhgroup_map[dhgroup_id].name;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_name);

const char *nvme_auth_dhgroup_kpp(u8 dhgroup_id)
{
	if (dhgroup_id > ARRAY_SIZE(dhgroup_map))
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

static struct nvme_dhchap_hash_map {
	int len;
	const char hmac[15];
	const char digest[8];
} hash_map[] = {
	[NVME_AUTH_HASH_SHA256] = {
		.len = 32,
		.hmac = "hmac(sha256)",
		.digest = "sha256",
	},
	[NVME_AUTH_HASH_SHA384] = {
		.len = 48,
		.hmac = "hmac(sha384)",
		.digest = "sha384",
	},
	[NVME_AUTH_HASH_SHA512] = {
		.len = 64,
		.hmac = "hmac(sha512)",
		.digest = "sha512",
	},
};

const char *nvme_auth_hmac_name(u8 hmac_id)
{
	if (hmac_id > ARRAY_SIZE(hash_map))
		return NULL;
	return hash_map[hmac_id].hmac;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_name);

const char *nvme_auth_digest_name(u8 hmac_id)
{
	if (hmac_id > ARRAY_SIZE(hash_map))
		return NULL;
	return hash_map[hmac_id].digest;
}
EXPORT_SYMBOL_GPL(nvme_auth_digest_name);

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
	if (hmac_id > ARRAY_SIZE(hash_map))
		return 0;
	return hash_map[hmac_id].len;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_hash_len);

struct nvme_dhchap_key *nvme_auth_extract_key(unsigned char *secret,
					      u8 key_hash)
{
	struct nvme_dhchap_key *key;
	unsigned char *p;
	u32 crc;
	int ret, key_len;
	size_t allocated_len = strlen(secret);

	/* Secret might be affixed with a ':' */
	p = strrchr(secret, ':');
	if (p)
		allocated_len = p - secret;
	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);
	key->key = kzalloc(allocated_len, GFP_KERNEL);
	if (!key->key) {
		ret = -ENOMEM;
		goto out_free_key;
	}

	key_len = base64_decode(secret, allocated_len, key->key);
	if (key_len < 0) {
		pr_debug("base64 key decoding error %d\n",
			 key_len);
		ret = key_len;
		goto out_free_secret;
	}

	if (key_len != 36 && key_len != 52 &&
	    key_len != 68) {
		pr_err("Invalid key len %d\n", key_len);
		ret = -EINVAL;
		goto out_free_secret;
	}

	if (key_hash > 0 &&
	    (key_len - 4) != nvme_auth_hmac_hash_len(key_hash)) {
		pr_err("Mismatched key len %d for %s\n", key_len,
		       nvme_auth_hmac_name(key_hash));
		ret = -EINVAL;
		goto out_free_secret;
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
		goto out_free_secret;
	}
	key->len = key_len;
	key->hash = key_hash;
	return key;
out_free_secret:
	kfree_sensitive(key->key);
out_free_key:
	kfree(key);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(nvme_auth_extract_key);

void nvme_auth_free_key(struct nvme_dhchap_key *key)
{
	if (!key)
		return;
	kfree_sensitive(key->key);
	kfree(key);
}
EXPORT_SYMBOL_GPL(nvme_auth_free_key);

u8 *nvme_auth_transform_key(struct nvme_dhchap_key *key, char *nqn)
{
	const char *hmac_name;
	struct crypto_shash *key_tfm;
	struct shash_desc *shash;
	u8 *transformed_key;
	int ret;

	if (!key || !key->key) {
		pr_warn("No key specified\n");
		return ERR_PTR(-ENOKEY);
	}
	if (key->hash == 0) {
		transformed_key = kmemdup(key->key, key->len, GFP_KERNEL);
		return transformed_key ? transformed_key : ERR_PTR(-ENOMEM);
	}
	hmac_name = nvme_auth_hmac_name(key->hash);
	if (!hmac_name) {
		pr_warn("Invalid key hash id %d\n", key->hash);
		return ERR_PTR(-EINVAL);
	}

	key_tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(key_tfm))
		return (u8 *)key_tfm;

	shash = kmalloc(sizeof(struct shash_desc) +
			crypto_shash_descsize(key_tfm),
			GFP_KERNEL);
	if (!shash) {
		ret = -ENOMEM;
		goto out_free_key;
	}

	transformed_key = kzalloc(crypto_shash_digestsize(key_tfm), GFP_KERNEL);
	if (!transformed_key) {
		ret = -ENOMEM;
		goto out_free_shash;
	}

	shash->tfm = key_tfm;
	ret = crypto_shash_setkey(key_tfm, key->key, key->len);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_init(shash);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, nqn, strlen(nqn));
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, "NVMe-over-Fabrics", 17);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_final(shash, transformed_key);
out_free_shash:
	kfree(shash);
out_free_key:
	crypto_free_shash(key_tfm);
	if (ret < 0) {
		kfree_sensitive(transformed_key);
		return ERR_PTR(ret);
	}
	return transformed_key;
}
EXPORT_SYMBOL_GPL(nvme_auth_transform_key);

int nvme_auth_generate_key(u8 *secret, struct nvme_dhchap_key **ret_key)
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
EXPORT_SYMBOL_GPL(nvme_auth_generate_key);

MODULE_LICENSE("GPL v2");
