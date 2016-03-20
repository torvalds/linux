/*
 * RSA key extract helper
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <crypto/internal/rsa.h>
#include "rsapubkey-asn1.h"
#include "rsaprivkey-asn1.h"

int rsa_get_n(void *context, size_t hdrlen, unsigned char tag,
	      const void *value, size_t vlen)
{
	struct rsa_key *key = context;

	key->n = mpi_read_raw_data(value, vlen);

	if (!key->n)
		return -ENOMEM;

	/* In FIPS mode only allow key size 2K & 3K */
	if (fips_enabled && (mpi_get_size(key->n) != 256 &&
			     mpi_get_size(key->n) != 384)) {
		pr_err("RSA: key size not allowed in FIPS mode\n");
		mpi_free(key->n);
		key->n = NULL;
		return -EINVAL;
	}
	return 0;
}

int rsa_get_e(void *context, size_t hdrlen, unsigned char tag,
	      const void *value, size_t vlen)
{
	struct rsa_key *key = context;

	key->e = mpi_read_raw_data(value, vlen);

	if (!key->e)
		return -ENOMEM;

	return 0;
}

int rsa_get_d(void *context, size_t hdrlen, unsigned char tag,
	      const void *value, size_t vlen)
{
	struct rsa_key *key = context;

	key->d = mpi_read_raw_data(value, vlen);

	if (!key->d)
		return -ENOMEM;

	/* In FIPS mode only allow key size 2K & 3K */
	if (fips_enabled && (mpi_get_size(key->d) != 256 &&
			     mpi_get_size(key->d) != 384)) {
		pr_err("RSA: key size not allowed in FIPS mode\n");
		mpi_free(key->d);
		key->d = NULL;
		return -EINVAL;
	}
	return 0;
}

static void free_mpis(struct rsa_key *key)
{
	mpi_free(key->n);
	mpi_free(key->e);
	mpi_free(key->d);
	key->n = NULL;
	key->e = NULL;
	key->d = NULL;
}

/**
 * rsa_free_key() - frees rsa key allocated by rsa_parse_key()
 *
 * @rsa_key:	struct rsa_key key representation
 */
void rsa_free_key(struct rsa_key *key)
{
	free_mpis(key);
}
EXPORT_SYMBOL_GPL(rsa_free_key);

/**
 * rsa_parse_pub_key() - extracts an rsa public key from BER encoded buffer
 *			 and stores it in the provided struct rsa_key
 *
 * @rsa_key:	struct rsa_key key representation
 * @key:	key in BER format
 * @key_len:	length of key
 *
 * Return:	0 on success or error code in case of error
 */
int rsa_parse_pub_key(struct rsa_key *rsa_key, const void *key,
		      unsigned int key_len)
{
	int ret;

	free_mpis(rsa_key);
	ret = asn1_ber_decoder(&rsapubkey_decoder, rsa_key, key, key_len);
	if (ret < 0)
		goto error;

	return 0;
error:
	free_mpis(rsa_key);
	return ret;
}
EXPORT_SYMBOL_GPL(rsa_parse_pub_key);

/**
 * rsa_parse_pub_key() - extracts an rsa private key from BER encoded buffer
 *			 and stores it in the provided struct rsa_key
 *
 * @rsa_key:	struct rsa_key key representation
 * @key:	key in BER format
 * @key_len:	length of key
 *
 * Return:	0 on success or error code in case of error
 */
int rsa_parse_priv_key(struct rsa_key *rsa_key, const void *key,
		       unsigned int key_len)
{
	int ret;

	free_mpis(rsa_key);
	ret = asn1_ber_decoder(&rsaprivkey_decoder, rsa_key, key, key_len);
	if (ret < 0)
		goto error;

	return 0;
error:
	free_mpis(rsa_key);
	return ret;
}
EXPORT_SYMBOL_GPL(rsa_parse_priv_key);
