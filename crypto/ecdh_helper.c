// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/string.h>
#include <crypto/ecdh.h>
#include <crypto/kpp.h>

#define ECDH_KPP_SECRET_MIN_SIZE (sizeof(struct kpp_secret) + 2 * sizeof(short))

static inline u8 *ecdh_pack_data(void *dst, const void *src, size_t sz)
{
	memcpy(dst, src, sz);
	return dst + sz;
}

static inline const u8 *ecdh_unpack_data(void *dst, const void *src, size_t sz)
{
	memcpy(dst, src, sz);
	return src + sz;
}

unsigned int crypto_ecdh_key_len(const struct ecdh *params)
{
	return ECDH_KPP_SECRET_MIN_SIZE + params->key_size;
}
EXPORT_SYMBOL_GPL(crypto_ecdh_key_len);

int crypto_ecdh_encode_key(char *buf, unsigned int len,
			   const struct ecdh *params)
{
	u8 *ptr = buf;
	struct kpp_secret secret = {
		.type = CRYPTO_KPP_SECRET_TYPE_ECDH,
		.len = len
	};

	if (unlikely(!buf))
		return -EINVAL;

	if (len != crypto_ecdh_key_len(params))
		return -EINVAL;

	ptr = ecdh_pack_data(ptr, &secret, sizeof(secret));
	ptr = ecdh_pack_data(ptr, &params->curve_id, sizeof(params->curve_id));
	ptr = ecdh_pack_data(ptr, &params->key_size, sizeof(params->key_size));
	ecdh_pack_data(ptr, params->key, params->key_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ecdh_encode_key);

int crypto_ecdh_decode_key(const char *buf, unsigned int len,
			   struct ecdh *params)
{
	const u8 *ptr = buf;
	struct kpp_secret secret;

	if (unlikely(!buf || len < ECDH_KPP_SECRET_MIN_SIZE))
		return -EINVAL;

	ptr = ecdh_unpack_data(&secret, ptr, sizeof(secret));
	if (secret.type != CRYPTO_KPP_SECRET_TYPE_ECDH)
		return -EINVAL;

	ptr = ecdh_unpack_data(&params->curve_id, ptr, sizeof(params->curve_id));
	ptr = ecdh_unpack_data(&params->key_size, ptr, sizeof(params->key_size));
	if (secret.len != crypto_ecdh_key_len(params))
		return -EINVAL;

	/* Don't allocate memory. Set pointer to data
	 * within the given buffer
	 */
	params->key = (void *)ptr;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_ecdh_decode_key);
