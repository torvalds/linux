/*
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/string.h>
#include <crypto/dh.h>
#include <crypto/kpp.h>

#define DH_KPP_SECRET_MIN_SIZE (sizeof(struct kpp_secret) + 4 * sizeof(int))

static inline u8 *dh_pack_data(u8 *dst, u8 *end, const void *src, size_t size)
{
	if (!dst || size > end - dst)
		return NULL;
	memcpy(dst, src, size);
	return dst + size;
}

static inline const u8 *dh_unpack_data(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
	return src + size;
}

static inline unsigned int dh_data_size(const struct dh *p)
{
	return p->key_size + p->p_size + p->q_size + p->g_size;
}

unsigned int crypto_dh_key_len(const struct dh *p)
{
	return DH_KPP_SECRET_MIN_SIZE + dh_data_size(p);
}
EXPORT_SYMBOL_GPL(crypto_dh_key_len);

int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params)
{
	u8 *ptr = buf;
	u8 * const end = ptr + len;
	struct kpp_secret secret = {
		.type = CRYPTO_KPP_SECRET_TYPE_DH,
		.len = len
	};

	if (unlikely(!len))
		return -EINVAL;

	ptr = dh_pack_data(ptr, end, &secret, sizeof(secret));
	ptr = dh_pack_data(ptr, end, &params->key_size,
			   sizeof(params->key_size));
	ptr = dh_pack_data(ptr, end, &params->p_size, sizeof(params->p_size));
	ptr = dh_pack_data(ptr, end, &params->q_size, sizeof(params->q_size));
	ptr = dh_pack_data(ptr, end, &params->g_size, sizeof(params->g_size));
	ptr = dh_pack_data(ptr, end, params->key, params->key_size);
	ptr = dh_pack_data(ptr, end, params->p, params->p_size);
	ptr = dh_pack_data(ptr, end, params->q, params->q_size);
	ptr = dh_pack_data(ptr, end, params->g, params->g_size);
	if (ptr != end)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_encode_key);

int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params)
{
	const u8 *ptr = buf;
	struct kpp_secret secret;

	if (unlikely(!buf || len < DH_KPP_SECRET_MIN_SIZE))
		return -EINVAL;

	ptr = dh_unpack_data(&secret, ptr, sizeof(secret));
	if (secret.type != CRYPTO_KPP_SECRET_TYPE_DH)
		return -EINVAL;

	ptr = dh_unpack_data(&params->key_size, ptr, sizeof(params->key_size));
	ptr = dh_unpack_data(&params->p_size, ptr, sizeof(params->p_size));
	ptr = dh_unpack_data(&params->q_size, ptr, sizeof(params->q_size));
	ptr = dh_unpack_data(&params->g_size, ptr, sizeof(params->g_size));
	if (secret.len != crypto_dh_key_len(params))
		return -EINVAL;

	/*
	 * Don't permit the buffer for 'key' or 'g' to be larger than 'p', since
	 * some drivers assume otherwise.
	 */
	if (params->key_size > params->p_size ||
	    params->g_size > params->p_size || params->q_size > params->p_size)
		return -EINVAL;

	/* Don't allocate memory. Set pointers to data within
	 * the given buffer
	 */
	params->key = (void *)ptr;
	params->p = (void *)(ptr + params->key_size);
	params->q = (void *)(ptr + params->key_size + params->p_size);
	params->g = (void *)(ptr + params->key_size + params->p_size +
			     params->q_size);

	/*
	 * Don't permit 'p' to be 0.  It's not a prime number, and it's subject
	 * to corner cases such as 'mod 0' being undefined or
	 * crypto_kpp_maxsize() returning 0.
	 */
	if (memchr_inv(params->p, 0, params->p_size) == NULL)
		return -EINVAL;

	/* It is permissible to not provide Q. */
	if (params->q_size == 0)
		params->q = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_decode_key);
