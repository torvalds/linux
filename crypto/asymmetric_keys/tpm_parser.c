// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "TPM-PARSER: "fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <keys/asymmetric-subtype.h>
#include <keys/asymmetric-parser.h>
#include <crypto/asym_tpm_subtype.h>
#include "tpm.asn1.h"

struct tpm_parse_context {
	const void	*blob;
	u32		blob_len;
};

/*
 * Note the key data of the ASN.1 blob.
 */
int tpm_note_key(void *context, size_t hdrlen,
		   unsigned char tag,
		   const void *value, size_t vlen)
{
	struct tpm_parse_context *ctx = context;

	ctx->blob = value;
	ctx->blob_len = vlen;

	return 0;
}

/*
 * Parse a TPM-encrypted private key blob.
 */
static struct tpm_key *tpm_parse(const void *data, size_t datalen)
{
	struct tpm_parse_context ctx;
	long ret;

	memset(&ctx, 0, sizeof(ctx));

	/* Attempt to decode the private key */
	ret = asn1_ber_decoder(&tpm_decoder, &ctx, data, datalen);
	if (ret < 0)
		goto error;

	return tpm_key_create(ctx.blob, ctx.blob_len);

error:
	return ERR_PTR(ret);
}
/*
 * Attempt to parse a data blob for a key as a TPM private key blob.
 */
static int tpm_key_preparse(struct key_preparsed_payload *prep)
{
	struct tpm_key *tk;

	/*
	 * TPM 1.2 keys are max 2048 bits long, so assume the blob is no
	 * more than 4x that
	 */
	if (prep->datalen > 256 * 4)
		return -EMSGSIZE;

	tk = tpm_parse(prep->data, prep->datalen);

	if (IS_ERR(tk))
		return PTR_ERR(tk);

	/* We're pinning the module by being linked against it */
	__module_get(asym_tpm_subtype.owner);
	prep->payload.data[asym_subtype] = &asym_tpm_subtype;
	prep->payload.data[asym_key_ids] = NULL;
	prep->payload.data[asym_crypto] = tk;
	prep->payload.data[asym_auth] = NULL;
	prep->quotalen = 100;
	return 0;
}

static struct asymmetric_key_parser tpm_key_parser = {
	.owner	= THIS_MODULE,
	.name	= "tpm_parser",
	.parse	= tpm_key_preparse,
};

static int __init tpm_key_init(void)
{
	return register_asymmetric_key_parser(&tpm_key_parser);
}

static void __exit tpm_key_exit(void)
{
	unregister_asymmetric_key_parser(&tpm_key_parser);
}

module_init(tpm_key_init);
module_exit(tpm_key_exit);

MODULE_DESCRIPTION("TPM private key-blob parser");
MODULE_LICENSE("GPL v2");
