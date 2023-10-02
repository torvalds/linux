// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SM2 asymmetric public-key algorithm
 * as specified by OSCCA GM/T 0003.1-2012 -- 0003.5-2012 SM2 and
 * described at https://tools.ietf.org/html/draft-shen-sm2-ecdsa-02
 *
 * Copyright (c) 2020, Alibaba Group.
 * Authors: Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/mpi.h>
#include <crypto/internal/akcipher.h>
#include <crypto/akcipher.h>
#include <crypto/hash.h>
#include <crypto/rng.h>
#include <crypto/sm2.h>
#include "sm2signature.asn1.h"

/* The default user id as specified in GM/T 0009-2012 */
#define SM2_DEFAULT_USERID "1234567812345678"
#define SM2_DEFAULT_USERID_LEN 16

#define MPI_NBYTES(m)   ((mpi_get_nbits(m) + 7) / 8)

struct ecc_domain_parms {
	const char *desc;           /* Description of the curve.  */
	unsigned int nbits;         /* Number of bits.  */
	unsigned int fips:1; /* True if this is a FIPS140-2 approved curve */

	/* The model describing this curve.  This is mainly used to select
	 * the group equation.
	 */
	enum gcry_mpi_ec_models model;

	/* The actual ECC dialect used.  This is used for curve specific
	 * optimizations and to select encodings etc.
	 */
	enum ecc_dialects dialect;

	const char *p;              /* The prime defining the field.  */
	const char *a, *b;          /* The coefficients.  For Twisted Edwards
				     * Curves b is used for d.  For Montgomery
				     * Curves (a,b) has ((A-2)/4,B^-1).
				     */
	const char *n;              /* The order of the base point.  */
	const char *g_x, *g_y;      /* Base point.  */
	unsigned int h;             /* Cofactor.  */
};

static const struct ecc_domain_parms sm2_ecp = {
	.desc = "sm2p256v1",
	.nbits = 256,
	.fips = 0,
	.model = MPI_EC_WEIERSTRASS,
	.dialect = ECC_DIALECT_STANDARD,
	.p   = "0xfffffffeffffffffffffffffffffffffffffffff00000000ffffffffffffffff",
	.a   = "0xfffffffeffffffffffffffffffffffffffffffff00000000fffffffffffffffc",
	.b   = "0x28e9fa9e9d9f5e344d5a9e4bcf6509a7f39789f515ab8f92ddbcbd414d940e93",
	.n   = "0xfffffffeffffffffffffffffffffffff7203df6b21c6052b53bbf40939d54123",
	.g_x = "0x32c4ae2c1f1981195f9904466a39c9948fe30bbff2660be1715a4589334c74c7",
	.g_y = "0xbc3736a2f4f6779c59bdcee36b692153d0a9877cc62a474002df32e52139f0a0",
	.h = 1
};

static int __sm2_set_pub_key(struct mpi_ec_ctx *ec,
			     const void *key, unsigned int keylen);

static int sm2_ec_ctx_init(struct mpi_ec_ctx *ec)
{
	const struct ecc_domain_parms *ecp = &sm2_ecp;
	MPI p, a, b;
	MPI x, y;
	int rc = -EINVAL;

	p = mpi_scanval(ecp->p);
	a = mpi_scanval(ecp->a);
	b = mpi_scanval(ecp->b);
	if (!p || !a || !b)
		goto free_p;

	x = mpi_scanval(ecp->g_x);
	y = mpi_scanval(ecp->g_y);
	if (!x || !y)
		goto free;

	rc = -ENOMEM;

	ec->Q = mpi_point_new(0);
	if (!ec->Q)
		goto free;

	/* mpi_ec_setup_elliptic_curve */
	ec->G = mpi_point_new(0);
	if (!ec->G) {
		mpi_point_release(ec->Q);
		goto free;
	}

	mpi_set(ec->G->x, x);
	mpi_set(ec->G->y, y);
	mpi_set_ui(ec->G->z, 1);

	rc = -EINVAL;
	ec->n = mpi_scanval(ecp->n);
	if (!ec->n) {
		mpi_point_release(ec->Q);
		mpi_point_release(ec->G);
		goto free;
	}

	ec->h = ecp->h;
	ec->name = ecp->desc;
	mpi_ec_init(ec, ecp->model, ecp->dialect, 0, p, a, b);

	rc = 0;

free:
	mpi_free(x);
	mpi_free(y);
free_p:
	mpi_free(p);
	mpi_free(a);
	mpi_free(b);

	return rc;
}

static void sm2_ec_ctx_deinit(struct mpi_ec_ctx *ec)
{
	mpi_ec_deinit(ec);

	memset(ec, 0, sizeof(*ec));
}

/* RESULT must have been initialized and is set on success to the
 * point given by VALUE.
 */
static int sm2_ecc_os2ec(MPI_POINT result, MPI value)
{
	int rc;
	size_t n;
	unsigned char *buf;
	MPI x, y;

	n = MPI_NBYTES(value);
	buf = kmalloc(n, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = mpi_print(GCRYMPI_FMT_USG, buf, n, &n, value);
	if (rc)
		goto err_freebuf;

	rc = -EINVAL;
	if (n < 1 || ((n - 1) % 2))
		goto err_freebuf;
	/* No support for point compression */
	if (*buf != 0x4)
		goto err_freebuf;

	rc = -ENOMEM;
	n = (n - 1) / 2;
	x = mpi_read_raw_data(buf + 1, n);
	if (!x)
		goto err_freebuf;
	y = mpi_read_raw_data(buf + 1 + n, n);
	if (!y)
		goto err_freex;

	mpi_normalize(x);
	mpi_normalize(y);
	mpi_set(result->x, x);
	mpi_set(result->y, y);
	mpi_set_ui(result->z, 1);

	rc = 0;

	mpi_free(y);
err_freex:
	mpi_free(x);
err_freebuf:
	kfree(buf);
	return rc;
}

struct sm2_signature_ctx {
	MPI sig_r;
	MPI sig_s;
};

int sm2_get_signature_r(void *context, size_t hdrlen, unsigned char tag,
				const void *value, size_t vlen)
{
	struct sm2_signature_ctx *sig = context;

	if (!value || !vlen)
		return -EINVAL;

	sig->sig_r = mpi_read_raw_data(value, vlen);
	if (!sig->sig_r)
		return -ENOMEM;

	return 0;
}

int sm2_get_signature_s(void *context, size_t hdrlen, unsigned char tag,
				const void *value, size_t vlen)
{
	struct sm2_signature_ctx *sig = context;

	if (!value || !vlen)
		return -EINVAL;

	sig->sig_s = mpi_read_raw_data(value, vlen);
	if (!sig->sig_s)
		return -ENOMEM;

	return 0;
}

static int sm2_z_digest_update(struct shash_desc *desc,
			       MPI m, unsigned int pbytes)
{
	static const unsigned char zero[32];
	unsigned char *in;
	unsigned int inlen;
	int err;

	in = mpi_get_buffer(m, &inlen, NULL);
	if (!in)
		return -EINVAL;

	if (inlen < pbytes) {
		/* padding with zero */
		err = crypto_shash_update(desc, zero, pbytes - inlen) ?:
		      crypto_shash_update(desc, in, inlen);
	} else if (inlen > pbytes) {
		/* skip the starting zero */
		err = crypto_shash_update(desc, in + inlen - pbytes, pbytes);
	} else {
		err = crypto_shash_update(desc, in, inlen);
	}

	kfree(in);
	return err;
}

static int sm2_z_digest_update_point(struct shash_desc *desc,
				     MPI_POINT point, struct mpi_ec_ctx *ec,
				     unsigned int pbytes)
{
	MPI x, y;
	int ret = -EINVAL;

	x = mpi_new(0);
	y = mpi_new(0);

	ret = mpi_ec_get_affine(x, y, point, ec) ? -EINVAL :
	      sm2_z_digest_update(desc, x, pbytes) ?:
	      sm2_z_digest_update(desc, y, pbytes);

	mpi_free(x);
	mpi_free(y);
	return ret;
}

int sm2_compute_z_digest(struct shash_desc *desc,
			 const void *key, unsigned int keylen, void *dgst)
{
	struct mpi_ec_ctx *ec;
	unsigned int bits_len;
	unsigned int pbytes;
	u8 entl[2];
	int err;

	ec = kmalloc(sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	err = sm2_ec_ctx_init(ec);
	if (err)
		goto out_free_ec;

	err = __sm2_set_pub_key(ec, key, keylen);
	if (err)
		goto out_deinit_ec;

	bits_len = SM2_DEFAULT_USERID_LEN * 8;
	entl[0] = bits_len >> 8;
	entl[1] = bits_len & 0xff;

	pbytes = MPI_NBYTES(ec->p);

	/* ZA = H256(ENTLA | IDA | a | b | xG | yG | xA | yA) */
	err = crypto_shash_init(desc);
	if (err)
		goto out_deinit_ec;

	err = crypto_shash_update(desc, entl, 2);
	if (err)
		goto out_deinit_ec;

	err = crypto_shash_update(desc, SM2_DEFAULT_USERID,
				  SM2_DEFAULT_USERID_LEN);
	if (err)
		goto out_deinit_ec;

	err = sm2_z_digest_update(desc, ec->a, pbytes) ?:
	      sm2_z_digest_update(desc, ec->b, pbytes) ?:
	      sm2_z_digest_update_point(desc, ec->G, ec, pbytes) ?:
	      sm2_z_digest_update_point(desc, ec->Q, ec, pbytes);
	if (err)
		goto out_deinit_ec;

	err = crypto_shash_final(desc, dgst);

out_deinit_ec:
	sm2_ec_ctx_deinit(ec);
out_free_ec:
	kfree(ec);
	return err;
}
EXPORT_SYMBOL_GPL(sm2_compute_z_digest);

static int _sm2_verify(struct mpi_ec_ctx *ec, MPI hash, MPI sig_r, MPI sig_s)
{
	int rc = -EINVAL;
	struct gcry_mpi_point sG, tP;
	MPI t = NULL;
	MPI x1 = NULL, y1 = NULL;

	mpi_point_init(&sG);
	mpi_point_init(&tP);
	x1 = mpi_new(0);
	y1 = mpi_new(0);
	t = mpi_new(0);

	/* r, s in [1, n-1] */
	if (mpi_cmp_ui(sig_r, 1) < 0 || mpi_cmp(sig_r, ec->n) > 0 ||
		mpi_cmp_ui(sig_s, 1) < 0 || mpi_cmp(sig_s, ec->n) > 0) {
		goto leave;
	}

	/* t = (r + s) % n, t == 0 */
	mpi_addm(t, sig_r, sig_s, ec->n);
	if (mpi_cmp_ui(t, 0) == 0)
		goto leave;

	/* sG + tP = (x1, y1) */
	rc = -EBADMSG;
	mpi_ec_mul_point(&sG, sig_s, ec->G, ec);
	mpi_ec_mul_point(&tP, t, ec->Q, ec);
	mpi_ec_add_points(&sG, &sG, &tP, ec);
	if (mpi_ec_get_affine(x1, y1, &sG, ec))
		goto leave;

	/* R = (e + x1) % n */
	mpi_addm(t, hash, x1, ec->n);

	/* check R == r */
	rc = -EKEYREJECTED;
	if (mpi_cmp(t, sig_r))
		goto leave;

	rc = 0;

leave:
	mpi_point_free_parts(&sG);
	mpi_point_free_parts(&tP);
	mpi_free(x1);
	mpi_free(y1);
	mpi_free(t);

	return rc;
}

static int sm2_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct mpi_ec_ctx *ec = akcipher_tfm_ctx(tfm);
	unsigned char *buffer;
	struct sm2_signature_ctx sig;
	MPI hash;
	int ret;

	if (unlikely(!ec->Q))
		return -EINVAL;

	buffer = kmalloc(req->src_len + req->dst_len, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	sg_pcopy_to_buffer(req->src,
		sg_nents_for_len(req->src, req->src_len + req->dst_len),
		buffer, req->src_len + req->dst_len, 0);

	sig.sig_r = NULL;
	sig.sig_s = NULL;
	ret = asn1_ber_decoder(&sm2signature_decoder, &sig,
				buffer, req->src_len);
	if (ret)
		goto error;

	ret = -ENOMEM;
	hash = mpi_read_raw_data(buffer + req->src_len, req->dst_len);
	if (!hash)
		goto error;

	ret = _sm2_verify(ec, hash, sig.sig_r, sig.sig_s);

	mpi_free(hash);
error:
	mpi_free(sig.sig_r);
	mpi_free(sig.sig_s);
	kfree(buffer);
	return ret;
}

static int sm2_set_pub_key(struct crypto_akcipher *tfm,
			const void *key, unsigned int keylen)
{
	struct mpi_ec_ctx *ec = akcipher_tfm_ctx(tfm);

	return __sm2_set_pub_key(ec, key, keylen);

}

static int __sm2_set_pub_key(struct mpi_ec_ctx *ec,
			     const void *key, unsigned int keylen)
{
	MPI a;
	int rc;

	/* include the uncompressed flag '0x04' */
	a = mpi_read_raw_data(key, keylen);
	if (!a)
		return -ENOMEM;

	mpi_normalize(a);
	rc = sm2_ecc_os2ec(ec->Q, a);
	mpi_free(a);

	return rc;
}

static unsigned int sm2_max_size(struct crypto_akcipher *tfm)
{
	/* Unlimited max size */
	return PAGE_SIZE;
}

static int sm2_init_tfm(struct crypto_akcipher *tfm)
{
	struct mpi_ec_ctx *ec = akcipher_tfm_ctx(tfm);

	return sm2_ec_ctx_init(ec);
}

static void sm2_exit_tfm(struct crypto_akcipher *tfm)
{
	struct mpi_ec_ctx *ec = akcipher_tfm_ctx(tfm);

	sm2_ec_ctx_deinit(ec);
}

static struct akcipher_alg sm2 = {
	.verify = sm2_verify,
	.set_pub_key = sm2_set_pub_key,
	.max_size = sm2_max_size,
	.init = sm2_init_tfm,
	.exit = sm2_exit_tfm,
	.base = {
		.cra_name = "sm2",
		.cra_driver_name = "sm2-generic",
		.cra_priority = 100,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct mpi_ec_ctx),
	},
};

static int __init sm2_init(void)
{
	return crypto_register_akcipher(&sm2);
}

static void __exit sm2_exit(void)
{
	crypto_unregister_akcipher(&sm2);
}

subsys_initcall(sm2_init);
module_exit(sm2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_DESCRIPTION("SM2 generic algorithm");
MODULE_ALIAS_CRYPTO("sm2-generic");
