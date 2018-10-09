// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "ASYM-TPM: "fmt
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/tpm.h>
#include <asm/unaligned.h>
#include <keys/asymmetric-subtype.h>
#include <crypto/asym_tpm_subtype.h>

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void asym_tpm_describe(const struct key *asymmetric_key,
			      struct seq_file *m)
{
	struct tpm_key *tk = asymmetric_key->payload.data[asym_crypto];

	if (!tk)
		return;

	seq_printf(m, "TPM1.2/Blob");
}

static void asym_tpm_destroy(void *payload0, void *payload3)
{
	struct tpm_key *tk = payload0;

	if (!tk)
		return;

	kfree(tk->blob);
	tk->blob_len = 0;

	kfree(tk);
}

/*
 * Parse enough information out of TPM_KEY structure:
 * TPM_STRUCT_VER -> 4 bytes
 * TPM_KEY_USAGE -> 2 bytes
 * TPM_KEY_FLAGS -> 4 bytes
 * TPM_AUTH_DATA_USAGE -> 1 byte
 * TPM_KEY_PARMS -> variable
 * UINT32 PCRInfoSize -> 4 bytes
 * BYTE* -> PCRInfoSize bytes
 * TPM_STORE_PUBKEY
 * UINT32 encDataSize;
 * BYTE* -> encDataSize;
 *
 * TPM_KEY_PARMS:
 * TPM_ALGORITHM_ID -> 4 bytes
 * TPM_ENC_SCHEME -> 2 bytes
 * TPM_SIG_SCHEME -> 2 bytes
 * UINT32 parmSize -> 4 bytes
 * BYTE* -> variable
 */
static int extract_key_parameters(struct tpm_key *tk)
{
	const void *cur = tk->blob;
	uint32_t len = tk->blob_len;
	const void *pub_key;
	uint32_t sz;
	uint32_t key_len;

	if (len < 11)
		return -EBADMSG;

	/* Ensure this is a legacy key */
	if (get_unaligned_be16(cur + 4) != 0x0015)
		return -EBADMSG;

	/* Skip to TPM_KEY_PARMS */
	cur += 11;
	len -= 11;

	if (len < 12)
		return -EBADMSG;

	/* Make sure this is an RSA key */
	if (get_unaligned_be32(cur) != 0x00000001)
		return -EBADMSG;

	/* Make sure this is TPM_ES_RSAESPKCSv15 encoding scheme */
	if (get_unaligned_be16(cur + 4) != 0x0002)
		return -EBADMSG;

	/* Make sure this is TPM_SS_RSASSAPKCS1v15_DER signature scheme */
	if (get_unaligned_be16(cur + 6) != 0x0003)
		return -EBADMSG;

	sz = get_unaligned_be32(cur + 8);
	if (len < sz + 12)
		return -EBADMSG;

	/* Move to TPM_RSA_KEY_PARMS */
	len -= 12;
	cur += 12;

	/* Grab the RSA key length */
	key_len = get_unaligned_be32(cur);

	switch (key_len) {
	case 512:
	case 1024:
	case 1536:
	case 2048:
		break;
	default:
		return -EINVAL;
	}

	/* Move just past TPM_KEY_PARMS */
	cur += sz;
	len -= sz;

	if (len < 4)
		return -EBADMSG;

	sz = get_unaligned_be32(cur);
	if (len < 4 + sz)
		return -EBADMSG;

	/* Move to TPM_STORE_PUBKEY */
	cur += 4 + sz;
	len -= 4 + sz;

	/* Grab the size of the public key, it should jive with the key size */
	sz = get_unaligned_be32(cur);
	if (sz > 256)
		return -EINVAL;

	pub_key = cur + 4;

	tk->key_len = key_len;
	tk->pub_key = pub_key;
	tk->pub_key_len = sz;

	return 0;
}

/* Given the blob, parse it and load it into the TPM */
struct tpm_key *tpm_key_create(const void *blob, uint32_t blob_len)
{
	int r;
	struct tpm_key *tk;

	r = tpm_is_tpm2(NULL);
	if (r < 0)
		goto error;

	/* We don't support TPM2 yet */
	if (r > 0) {
		r = -ENODEV;
		goto error;
	}

	r = -ENOMEM;
	tk = kzalloc(sizeof(struct tpm_key), GFP_KERNEL);
	if (!tk)
		goto error;

	tk->blob = kmemdup(blob, blob_len, GFP_KERNEL);
	if (!tk->blob)
		goto error_memdup;

	tk->blob_len = blob_len;

	r = extract_key_parameters(tk);
	if (r < 0)
		goto error_extract;

	return tk;

error_extract:
	kfree(tk->blob);
	tk->blob_len = 0;
error_memdup:
	kfree(tk);
error:
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(tpm_key_create);

/*
 * TPM-based asymmetric key subtype
 */
struct asymmetric_key_subtype asym_tpm_subtype = {
	.owner			= THIS_MODULE,
	.name			= "asym_tpm",
	.name_len		= sizeof("asym_tpm") - 1,
	.describe		= asym_tpm_describe,
	.destroy		= asym_tpm_destroy,
};
EXPORT_SYMBOL_GPL(asym_tpm_subtype);

MODULE_DESCRIPTION("TPM based asymmetric key subtype");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
