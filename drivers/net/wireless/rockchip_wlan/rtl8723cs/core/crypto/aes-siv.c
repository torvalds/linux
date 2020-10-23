/*
 * AES SIV (RFC 5297)
 * Copyright (c) 2013 Cozybit, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "rtw_crypto_wrap.h"

#include "aes.h"
#include "aes_wrap.h"
#include "aes_siv.h"


static const u8 zero[AES_BLOCK_SIZE];


static void dbl(u8 *pad)
{
	int i, carry;

	carry = pad[0] & 0x80;
	for (i = 0; i < AES_BLOCK_SIZE - 1; i++)
		pad[i] = (pad[i] << 1) | (pad[i + 1] >> 7);
	pad[AES_BLOCK_SIZE - 1] <<= 1;
	if (carry)
		pad[AES_BLOCK_SIZE - 1] ^= 0x87;
}


static void xor(u8 *a, const u8 *b)
{
	int i;

	for (i = 0; i < AES_BLOCK_SIZE; i++)
		*a++ ^= *b++;
}


static void xorend(u8 *a, int alen, const u8 *b, int blen)
{
	int i;

	if (alen < blen)
		return;

	for (i = 0; i < blen; i++)
		a[alen - blen + i] ^= b[i];
}


static void pad_block(u8 *pad, const u8 *addr, size_t len)
{
	os_memset(pad, 0, AES_BLOCK_SIZE);
	os_memcpy(pad, addr, len);

	if (len < AES_BLOCK_SIZE)
		pad[len] = 0x80;
}


static int aes_s2v(const u8 *key, size_t key_len,
		   size_t num_elem, const u8 *addr[], size_t *len, u8 *mac)
{
	u8 tmp[AES_BLOCK_SIZE], tmp2[AES_BLOCK_SIZE];
	u8 *buf = NULL;
	int ret;
	size_t i;
	const u8 *data[1];
	size_t data_len[1];

	if (!num_elem) {
		os_memcpy(tmp, zero, sizeof(zero));
		tmp[AES_BLOCK_SIZE - 1] = 1;
		data[0] = tmp;
		data_len[0] = sizeof(tmp);
		return omac1_aes_vector(key, key_len, 1, data, data_len, mac);
	}

	data[0] = zero;
	data_len[0] = sizeof(zero);
	ret = omac1_aes_vector(key, key_len, 1, data, data_len, tmp);
	if (ret)
		return ret;

	for (i = 0; i < num_elem - 1; i++) {
		ret = omac1_aes_vector(key, key_len, 1, &addr[i], &len[i],
				       tmp2);
		if (ret)
			return ret;

		dbl(tmp);
		xor(tmp, tmp2);
	}
	if (len[i] >= AES_BLOCK_SIZE) {
		buf = os_memdup(addr[i], len[i]);
		if (!buf)
			return -ENOMEM;

		xorend(buf, len[i], tmp, AES_BLOCK_SIZE);
		data[0] = buf;
		ret = omac1_aes_vector(key, key_len, 1, data, &len[i], mac);
		bin_clear_free(buf, len[i]);
		return ret;
	}

	dbl(tmp);
	pad_block(tmp2, addr[i], len[i]);
	xor(tmp, tmp2);

	data[0] = tmp;
	data_len[0] = sizeof(tmp);
	return omac1_aes_vector(key, key_len, 1, data, data_len, mac);
}


int aes_siv_encrypt(const u8 *key, size_t key_len,
		    const u8 *pw, size_t pwlen,
		    size_t num_elem, const u8 *addr[], const size_t *len,
		    u8 *out)
{
	const u8 *_addr[6];
	size_t _len[6];
	const u8 *k1, *k2;
	u8 v[AES_BLOCK_SIZE];
	size_t i;
	u8 *iv, *crypt_pw;

	if (num_elem > ARRAY_SIZE(_addr) - 1 ||
	    (key_len != 32 && key_len != 48 && key_len != 64))
		return -1;

	key_len /= 2;
	k1 = key;
	k2 = key + key_len;

	for (i = 0; i < num_elem; i++) {
		_addr[i] = addr[i];
		_len[i] = len[i];
	}
	_addr[num_elem] = pw;
	_len[num_elem] = pwlen;

	if (aes_s2v(k1, key_len, num_elem + 1, _addr, _len, v))
		return -1;

	iv = out;
	crypt_pw = out + AES_BLOCK_SIZE;

	os_memcpy(iv, v, AES_BLOCK_SIZE);
	os_memcpy(crypt_pw, pw, pwlen);

	/* zero out 63rd and 31st bits of ctr (from right) */
	v[8] &= 0x7f;
	v[12] &= 0x7f;
	return aes_ctr_encrypt(k2, key_len, v, crypt_pw, pwlen);
}


int aes_siv_decrypt(const u8 *key, size_t key_len,
		    const u8 *iv_crypt, size_t iv_c_len,
		    size_t num_elem, const u8 *addr[], const size_t *len,
		    u8 *out)
{
	const u8 *_addr[6];
	size_t _len[6];
	const u8 *k1, *k2;
	size_t crypt_len;
	size_t i;
	int ret;
	u8 iv[AES_BLOCK_SIZE];
	u8 check[AES_BLOCK_SIZE];

	if (iv_c_len < AES_BLOCK_SIZE || num_elem > ARRAY_SIZE(_addr) - 1 ||
	    (key_len != 32 && key_len != 48 && key_len != 64))
		return -1;
	crypt_len = iv_c_len - AES_BLOCK_SIZE;
	key_len /= 2;
	k1 = key;
	k2 = key + key_len;

	for (i = 0; i < num_elem; i++) {
		_addr[i] = addr[i];
		_len[i] = len[i];
	}
	_addr[num_elem] = out;
	_len[num_elem] = crypt_len;

	os_memcpy(iv, iv_crypt, AES_BLOCK_SIZE);
	os_memcpy(out, iv_crypt + AES_BLOCK_SIZE, crypt_len);

	iv[8] &= 0x7f;
	iv[12] &= 0x7f;

	ret = aes_ctr_encrypt(k2, key_len, iv, out, crypt_len);
	if (ret)
		return ret;

	ret = aes_s2v(k1, key_len, num_elem + 1, _addr, _len, check);
	if (ret)
		return ret;
	if (os_memcmp(check, iv_crypt, AES_BLOCK_SIZE) == 0)
		return 0;

	return -1;
}
