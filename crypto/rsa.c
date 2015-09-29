/* RSA asymmetric public-key algorithm [RFC3447]
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/akcipher.h>
#include <crypto/akcipher.h>

/*
 * RSAEP function [RFC3447 sec 5.1.1]
 * c = m^e mod n;
 */
static int _rsa_enc(const struct rsa_key *key, MPI c, MPI m)
{
	/* (1) Validate 0 <= m < n */
	if (mpi_cmp_ui(m, 0) < 0 || mpi_cmp(m, key->n) >= 0)
		return -EINVAL;

	/* (2) c = m^e mod n */
	return mpi_powm(c, m, key->e, key->n);
}

/*
 * RSADP function [RFC3447 sec 5.1.2]
 * m = c^d mod n;
 */
static int _rsa_dec(const struct rsa_key *key, MPI m, MPI c)
{
	/* (1) Validate 0 <= c < n */
	if (mpi_cmp_ui(c, 0) < 0 || mpi_cmp(c, key->n) >= 0)
		return -EINVAL;

	/* (2) m = c^d mod n */
	return mpi_powm(m, c, key->d, key->n);
}

/*
 * RSASP1 function [RFC3447 sec 5.2.1]
 * s = m^d mod n
 */
static int _rsa_sign(const struct rsa_key *key, MPI s, MPI m)
{
	/* (1) Validate 0 <= m < n */
	if (mpi_cmp_ui(m, 0) < 0 || mpi_cmp(m, key->n) >= 0)
		return -EINVAL;

	/* (2) s = m^d mod n */
	return mpi_powm(s, m, key->d, key->n);
}

/*
 * RSAVP1 function [RFC3447 sec 5.2.2]
 * m = s^e mod n;
 */
static int _rsa_verify(const struct rsa_key *key, MPI m, MPI s)
{
	/* (1) Validate 0 <= s < n */
	if (mpi_cmp_ui(s, 0) < 0 || mpi_cmp(s, key->n) >= 0)
		return -EINVAL;

	/* (2) m = s^e mod n */
	return mpi_powm(m, s, key->e, key->n);
}

static inline struct rsa_key *rsa_get_key(struct crypto_akcipher *tfm)
{
	return akcipher_tfm_ctx(tfm);
}

static int rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	const struct rsa_key *pkey = rsa_get_key(tfm);
	MPI m, c = mpi_alloc(0);
	int ret = 0;
	int sign;

	if (!c)
		return -ENOMEM;

	if (unlikely(!pkey->n || !pkey->e)) {
		ret = -EINVAL;
		goto err_free_c;
	}

	if (req->dst_len < mpi_get_size(pkey->n)) {
		req->dst_len = mpi_get_size(pkey->n);
		ret = -EOVERFLOW;
		goto err_free_c;
	}

	m = mpi_read_raw_data(req->src, req->src_len);
	if (!m) {
		ret = -ENOMEM;
		goto err_free_c;
	}

	ret = _rsa_enc(pkey, c, m);
	if (ret)
		goto err_free_m;

	ret = mpi_read_buffer(c, req->dst, req->dst_len, &req->dst_len, &sign);
	if (ret)
		goto err_free_m;

	if (sign < 0) {
		ret = -EBADMSG;
		goto err_free_m;
	}

err_free_m:
	mpi_free(m);
err_free_c:
	mpi_free(c);
	return ret;
}

static int rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	const struct rsa_key *pkey = rsa_get_key(tfm);
	MPI c, m = mpi_alloc(0);
	int ret = 0;
	int sign;

	if (!m)
		return -ENOMEM;

	if (unlikely(!pkey->n || !pkey->d)) {
		ret = -EINVAL;
		goto err_free_m;
	}

	if (req->dst_len < mpi_get_size(pkey->n)) {
		req->dst_len = mpi_get_size(pkey->n);
		ret = -EOVERFLOW;
		goto err_free_m;
	}

	c = mpi_read_raw_data(req->src, req->src_len);
	if (!c) {
		ret = -ENOMEM;
		goto err_free_m;
	}

	ret = _rsa_dec(pkey, m, c);
	if (ret)
		goto err_free_c;

	ret = mpi_read_buffer(m, req->dst, req->dst_len, &req->dst_len, &sign);
	if (ret)
		goto err_free_c;

	if (sign < 0) {
		ret = -EBADMSG;
		goto err_free_c;
	}

err_free_c:
	mpi_free(c);
err_free_m:
	mpi_free(m);
	return ret;
}

static int rsa_sign(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	const struct rsa_key *pkey = rsa_get_key(tfm);
	MPI m, s = mpi_alloc(0);
	int ret = 0;
	int sign;

	if (!s)
		return -ENOMEM;

	if (unlikely(!pkey->n || !pkey->d)) {
		ret = -EINVAL;
		goto err_free_s;
	}

	if (req->dst_len < mpi_get_size(pkey->n)) {
		req->dst_len = mpi_get_size(pkey->n);
		ret = -EOVERFLOW;
		goto err_free_s;
	}

	m = mpi_read_raw_data(req->src, req->src_len);
	if (!m) {
		ret = -ENOMEM;
		goto err_free_s;
	}

	ret = _rsa_sign(pkey, s, m);
	if (ret)
		goto err_free_m;

	ret = mpi_read_buffer(s, req->dst, req->dst_len, &req->dst_len, &sign);
	if (ret)
		goto err_free_m;

	if (sign < 0) {
		ret = -EBADMSG;
		goto err_free_m;
	}

err_free_m:
	mpi_free(m);
err_free_s:
	mpi_free(s);
	return ret;
}

static int rsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	const struct rsa_key *pkey = rsa_get_key(tfm);
	MPI s, m = mpi_alloc(0);
	int ret = 0;
	int sign;

	if (!m)
		return -ENOMEM;

	if (unlikely(!pkey->n || !pkey->e)) {
		ret = -EINVAL;
		goto err_free_m;
	}

	if (req->dst_len < mpi_get_size(pkey->n)) {
		req->dst_len = mpi_get_size(pkey->n);
		ret = -EOVERFLOW;
		goto err_free_m;
	}

	s = mpi_read_raw_data(req->src, req->src_len);
	if (!s) {
		ret = -ENOMEM;
		goto err_free_m;
	}

	ret = _rsa_verify(pkey, m, s);
	if (ret)
		goto err_free_s;

	ret = mpi_read_buffer(m, req->dst, req->dst_len, &req->dst_len, &sign);
	if (ret)
		goto err_free_s;

	if (sign < 0) {
		ret = -EBADMSG;
		goto err_free_s;
	}

err_free_s:
	mpi_free(s);
err_free_m:
	mpi_free(m);
	return ret;
}

static int rsa_check_key_length(unsigned int len)
{
	switch (len) {
	case 512:
	case 1024:
	case 1536:
	case 2048:
	case 3072:
	case 4096:
		return 0;
	}

	return -EINVAL;
}

static int rsa_setkey(struct crypto_akcipher *tfm, const void *key,
		      unsigned int keylen)
{
	struct rsa_key *pkey = akcipher_tfm_ctx(tfm);
	int ret;

	ret = rsa_parse_key(pkey, key, keylen);
	if (ret)
		return ret;

	if (rsa_check_key_length(mpi_get_size(pkey->n) << 3)) {
		rsa_free_key(pkey);
		ret = -EINVAL;
	}
	return ret;
}

static void rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct rsa_key *pkey = akcipher_tfm_ctx(tfm);

	rsa_free_key(pkey);
}

static struct akcipher_alg rsa = {
	.encrypt = rsa_enc,
	.decrypt = rsa_dec,
	.sign = rsa_sign,
	.verify = rsa_verify,
	.setkey = rsa_setkey,
	.exit = rsa_exit_tfm,
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "rsa-generic",
		.cra_priority = 100,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct rsa_key),
	},
};

static int rsa_init(void)
{
	return crypto_register_akcipher(&rsa);
}

static void rsa_exit(void)
{
	crypto_unregister_akcipher(&rsa);
}

module_init(rsa_init);
module_exit(rsa_exit);
MODULE_ALIAS_CRYPTO("rsa");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RSA generic algorithm");
