/*
 * Crypto driver for the Aspeed SoC
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "aspeed-acry.h"

#define ASPEED_ECDH_DEBUG

#ifdef ASPEED_ECDH_DEBUG
//#define ECDH_DBG(fmt, args...) printk(KERN_DEBUG "%s() " fmt, __FUNCTION__, ## args)
#define ECDH_DBG(fmt, args...) printk("%s() " fmt, __FUNCTION__, ## args)
#else
#define ECDH_DBG(fmt, args...)
#endif

int aspeed_ecdh_trigger(struct aspeed_hace_dev *hace_dev)
{
	ECDH_DBG("\n");

	return 0;
}

/*
* @generate_public_key: Function generate the public key to be sent to the
*		   counterpart. In case of error, where output is not big
*		   enough req->dst_len will be updated to the size
*		   required

*/

static int aspeed_ecdh_generate_public_key(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct aspeed_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	size_t copied;
	int ret = 0;

	ECDH_DBG("req->src %x , req->dst %x \n", req->src, req->dst);

#if 0
	/* public key was saved at private key generation */
	copied = sg_copy_from_buffer(req->dst, 1, ctx->public_key,
				     ATMEL_ECC_PUBKEY_SIZE);
	if (copied != ATMEL_ECC_PUBKEY_SIZE)
		ret = -EINVAL;
#endif
	return ret;
}


/*
* @compute_shared_secret: Function compute the shared secret as defined by
*		   the algorithm. The result is given back to the user.
*		   In case of error, where output is not big enough,
*		   req->dst_len will be updated to the size required

*/

static int aspeed_ecdh_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct aspeed_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
//	struct qat_crypto_instance *inst = ctx->inst;

	ECDH_DBG("\n");
	
}

static unsigned int aspeed_ecdh_supported_curve(unsigned int curve_id)
{
	switch (curve_id) {
		case ECC_CURVE_NIST_P192: return 3;
		case ECC_CURVE_NIST_P256: return 4;
		default: return 0;
	}
}

/*
* @set_secret:	   Function invokes the protocol specific function to
*		   store the secret private key along with parameters.
*		   The implementation knows how to decode thie buffer

*/
static int aspeed_ecdh_set_secret(struct crypto_kpp *tfm, void *buf,
			     unsigned int len)
{
	struct aspeed_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct ecdh params;
	unsigned int ndigits;
	int ret;

	ECDH_DBG("len %d \n", len);

	if (crypto_ecdh_decode_key(buf, len, &params) < 0) {
		dev_err(&ctx->hace_dev->dev, "crypto_ecdh_decode_key failed\n");
		return -EINVAL;
	}
	ECDH_DBG("curive_id %d, key size %d \n", params.curve_id, params.key_size);

	ndigits = aspeed_ecdh_supported_curve(params.curve_id);
	if (!ndigits)
		return -EINVAL;

	ctx->curve_id = params.curve_id;
 	memcpy(ctx->private_key, params.key, params.key_size);

	return 0;
}

 /*
 * @max_size:		Function returns the size of the output buffer
 
 */
static int aspeed_ecdh_max_size(struct crypto_kpp *tfm)
{
	struct aspeed_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	ECDH_DBG("\n");

	//return ctx->p ? ctx->p_size : -EINVAL;
	return 64;
}

static int aspeed_ecdh_init_tfm(struct crypto_kpp *tfm)
{
	struct aspeed_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	struct aspeed_hace_alg *crypto_alg;
	
	crypto_alg = container_of(alg, struct aspeed_hace_alg, alg.crypto);
	ctx->hace_dev = crypto_alg->hace_dev;
	ECDH_DBG("\n");

	return 0;
}

static void aspeed_ecdh_exit_tfm(struct crypto_tfm *tfm)
{
	//disable clk ??
	ECDH_DBG("\n");
}

struct aspeed_hace_alg aspeed_kpp_algs[] = {
	{
		.alg.kpp = {
			.set_secret = aspeed_ecdh_set_secret,
			.generate_public_key = aspeed_ecdh_generate_public_key,
			.compute_shared_secret = aspeed_ecdh_compute_value,
			.max_size = aspeed_ecdh_max_size,
			.init = aspeed_ecdh_init_tfm,
			.exit = aspeed_ecdh_exit_tfm,
			.base = {
				.cra_name = "ecdh",
				.cra_driver_name = "aspeed-ecdh",
				.cra_priority = 300,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct aspeed_ecdh_ctx),
			},
		},
	},
};

int aspeed_register_kpp_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;
	int err = 0;
	for (i = 0; i < ARRAY_SIZE(aspeed_kpp_algs); i++) {
		aspeed_kpp_algs[i].hace_dev = hace_dev;
		err = crypto_register_kpp(&aspeed_kpp_algs[i].alg.kpp);
		if (err)
			return err;
	}
	return 0;
}
