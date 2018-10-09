// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "ASYM-TPM: "fmt
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/tpm.h>
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

	return tk;

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
