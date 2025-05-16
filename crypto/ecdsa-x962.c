// SPDX-License-Identifier: GPL-2.0+
/*
 * ECDSA X9.62 signature encoding
 *
 * Copyright (c) 2021 IBM Corporation
 * Copyright (c) 2024 Intel Corporation
 */

#include <linux/asn1_decoder.h>
#include <linux/err.h>
#include <linux/module.h>
#include <crypto/algapi.h>
#include <crypto/sig.h>
#include <crypto/internal/ecc.h>
#include <crypto/internal/sig.h>

#include "ecdsasignature.asn1.h"

struct ecdsa_x962_ctx {
	struct crypto_sig *child;
};

struct ecdsa_x962_signature_ctx {
	struct ecdsa_raw_sig sig;
	unsigned int ndigits;
};

/* Get the r and s components of a signature from the X.509 certificate. */
static int ecdsa_get_signature_rs(u64 *dest, size_t hdrlen, unsigned char tag,
				  const void *value, size_t vlen,
				  unsigned int ndigits)
{
	size_t bufsize = ndigits * sizeof(u64);
	const char *d = value;

	if (!value || !vlen || vlen > bufsize + 1)
		return -EINVAL;

	/*
	 * vlen may be 1 byte larger than bufsize due to a leading zero byte
	 * (necessary if the most significant bit of the integer is set).
	 */
	if (vlen > bufsize) {
		/* skip over leading zeros that make 'value' a positive int */
		if (*d == 0) {
			vlen -= 1;
			d++;
		} else {
			return -EINVAL;
		}
	}

	ecc_digits_from_bytes(d, vlen, dest, ndigits);

	return 0;
}

int ecdsa_get_signature_r(void *context, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	struct ecdsa_x962_signature_ctx *sig_ctx = context;

	return ecdsa_get_signature_rs(sig_ctx->sig.r, hdrlen, tag, value, vlen,
				      sig_ctx->ndigits);
}

int ecdsa_get_signature_s(void *context, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	struct ecdsa_x962_signature_ctx *sig_ctx = context;

	return ecdsa_get_signature_rs(sig_ctx->sig.s, hdrlen, tag, value, vlen,
				      sig_ctx->ndigits);
}

static int ecdsa_x962_verify(struct crypto_sig *tfm,
			     const void *src, unsigned int slen,
			     const void *digest, unsigned int dlen)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);
	struct ecdsa_x962_signature_ctx sig_ctx;
	int err;

	sig_ctx.ndigits = DIV_ROUND_UP_POW2(crypto_sig_keysize(ctx->child),
					    sizeof(u64));

	err = asn1_ber_decoder(&ecdsasignature_decoder, &sig_ctx, src, slen);
	if (err < 0)
		return err;

	return crypto_sig_verify(ctx->child, &sig_ctx.sig, sizeof(sig_ctx.sig),
				 digest, dlen);
}

static unsigned int ecdsa_x962_key_size(struct crypto_sig *tfm)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_keysize(ctx->child);
}

static unsigned int ecdsa_x962_max_size(struct crypto_sig *tfm)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);
	struct sig_alg *alg = crypto_sig_alg(ctx->child);
	int slen = crypto_sig_keysize(ctx->child);

	/*
	 * Verify takes ECDSA-Sig-Value (described in RFC 5480) as input,
	 * which is actually 2 'key_size'-bit integers encoded in ASN.1.
	 * Account for the ASN.1 encoding overhead here.
	 *
	 * NIST P192/256/384 may prepend a '0' to a coordinate to indicate
	 * a positive integer. NIST P521 never needs it.
	 */
	if (strcmp(alg->base.cra_name, "ecdsa-nist-p521") != 0)
		slen += 1;

	/* Length of encoding the x & y coordinates */
	slen = 2 * (slen + 2);

	/*
	 * If coordinate encoding takes at least 128 bytes then an
	 * additional byte for length encoding is needed.
	 */
	return 1 + (slen >= 128) + 1 + slen;
}

static unsigned int ecdsa_x962_digest_size(struct crypto_sig *tfm)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_digestsize(ctx->child);
}

static int ecdsa_x962_set_pub_key(struct crypto_sig *tfm,
				  const void *key, unsigned int keylen)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);

	return crypto_sig_set_pubkey(ctx->child, key, keylen);
}

static int ecdsa_x962_init_tfm(struct crypto_sig *tfm)
{
	struct sig_instance *inst = sig_alg_instance(tfm);
	struct crypto_sig_spawn *spawn = sig_instance_ctx(inst);
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);
	struct crypto_sig *child_tfm;

	child_tfm = crypto_spawn_sig(spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;

	return 0;
}

static void ecdsa_x962_exit_tfm(struct crypto_sig *tfm)
{
	struct ecdsa_x962_ctx *ctx = crypto_sig_ctx(tfm);

	crypto_free_sig(ctx->child);
}

static void ecdsa_x962_free(struct sig_instance *inst)
{
	struct crypto_sig_spawn *spawn = sig_instance_ctx(inst);

	crypto_drop_sig(spawn);
	kfree(inst);
}

static int ecdsa_x962_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_sig_spawn *spawn;
	struct sig_instance *inst;
	struct sig_alg *ecdsa_alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SIG, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = sig_instance_ctx(inst);

	err = crypto_grab_sig(spawn, sig_crypto_instance(inst),
			      crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	ecdsa_alg = crypto_spawn_sig_alg(spawn);

	err = -EINVAL;
	if (strncmp(ecdsa_alg->base.cra_name, "ecdsa", 5) != 0)
		goto err_free_inst;

	err = crypto_inst_setname(sig_crypto_instance(inst), tmpl->name,
				  &ecdsa_alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = ecdsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct ecdsa_x962_ctx);

	inst->alg.init = ecdsa_x962_init_tfm;
	inst->alg.exit = ecdsa_x962_exit_tfm;

	inst->alg.verify = ecdsa_x962_verify;
	inst->alg.key_size = ecdsa_x962_key_size;
	inst->alg.max_size = ecdsa_x962_max_size;
	inst->alg.digest_size = ecdsa_x962_digest_size;
	inst->alg.set_pub_key = ecdsa_x962_set_pub_key;

	inst->free = ecdsa_x962_free;

	err = sig_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		ecdsa_x962_free(inst);
	}
	return err;
}

struct crypto_template ecdsa_x962_tmpl = {
	.name = "x962",
	.create = ecdsa_x962_create,
	.module = THIS_MODULE,
};

MODULE_ALIAS_CRYPTO("x962");
