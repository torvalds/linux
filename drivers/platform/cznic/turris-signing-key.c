// SPDX-License-Identifier: GPL-2.0
/*
 * Some of CZ.NIC's Turris devices support signing messages with a per-device unique asymmetric
 * cryptographic key that was burned into the device at manufacture.
 *
 * This helper module exposes this message signing ability via the keyctl() syscall. Upon load, it
 * creates the `.turris-signing-keys` keyring. A device-specific driver then has to create a signing
 * key by calling devm_turris_signing_key_create().
 *
 * 2025 by Marek Behún <kabel@kernel.org>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/key-type.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/turris-signing-key.h>

static int turris_signing_key_instantiate(struct key *key,
					  struct key_preparsed_payload *payload)
{
	return 0;
}

static void turris_signing_key_describe(const struct key *key, struct seq_file *m)
{
	const struct turris_signing_key_subtype *subtype = dereference_key_rcu(key);

	if (!subtype)
		return;

	seq_printf(m, "%s: %*phN", key->description, subtype->public_key_size,
		   subtype->get_public_key(key));
}

static long turris_signing_key_read(const struct key *key, char *buffer, size_t buflen)
{
	const struct turris_signing_key_subtype *subtype = dereference_key_rcu(key);

	if (!subtype)
		return -EIO;

	if (buffer) {
		if (buflen > subtype->public_key_size)
			buflen = subtype->public_key_size;

		memcpy(buffer, subtype->get_public_key(key), subtype->public_key_size);
	}

	return subtype->public_key_size;
}

static bool turris_signing_key_asym_valid_params(const struct turris_signing_key_subtype *subtype,
						 const struct kernel_pkey_params *params)
{
	if (params->encoding && strcmp(params->encoding, "raw"))
		return false;

	if (params->hash_algo && strcmp(params->hash_algo, subtype->hash_algo))
		return false;

	return true;
}

static int turris_signing_key_asym_query(const struct kernel_pkey_params *params,
					 struct kernel_pkey_query *info)
{
	const struct turris_signing_key_subtype *subtype = dereference_key_rcu(params->key);

	if (!subtype)
		return -EIO;

	if (!turris_signing_key_asym_valid_params(subtype, params))
		return -EINVAL;

	info->supported_ops = KEYCTL_SUPPORTS_SIGN;
	info->key_size = subtype->key_size;
	info->max_data_size = subtype->data_size;
	info->max_sig_size = subtype->sig_size;
	info->max_enc_size = 0;
	info->max_dec_size = 0;

	return 0;
}

static int turris_signing_key_asym_eds_op(struct kernel_pkey_params *params,
				 const void *in, void *out)
{
	const struct turris_signing_key_subtype *subtype = dereference_key_rcu(params->key);
	int err;

	if (!subtype)
		return -EIO;

	if (!turris_signing_key_asym_valid_params(subtype, params))
		return -EINVAL;

	if (params->op != kernel_pkey_sign)
		return -EOPNOTSUPP;

	if (params->in_len != subtype->data_size || params->out_len != subtype->sig_size)
		return -EINVAL;

	err = subtype->sign(params->key, in, out);
	if (err)
		return err;

	return subtype->sig_size;
}

static struct key_type turris_signing_key_type = {
	.name		= "turris-signing-key",
	.instantiate	= turris_signing_key_instantiate,
	.describe	= turris_signing_key_describe,
	.read		= turris_signing_key_read,
	.asym_query	= turris_signing_key_asym_query,
	.asym_eds_op	= turris_signing_key_asym_eds_op,
};

static struct key *turris_signing_keyring;

static void turris_signing_key_release(void *key)
{
	key_unlink(turris_signing_keyring, key);
	key_put(key);
}

int
devm_turris_signing_key_create(struct device *dev, const struct turris_signing_key_subtype *subtype,
			       const char *desc)
{
	struct key *key;
	key_ref_t kref;

	kref = key_create(make_key_ref(turris_signing_keyring, true),
			  turris_signing_key_type.name, desc, NULL, 0,
			  (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW | KEY_USR_READ |
			  KEY_USR_SEARCH,
			  KEY_ALLOC_BUILT_IN | KEY_ALLOC_SET_KEEP | KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(kref))
		return PTR_ERR(kref);

	key = key_ref_to_ptr(kref);
	key->payload.data[1] = dev;
	rcu_assign_keypointer(key, subtype);

	return devm_add_action_or_reset(dev, turris_signing_key_release, key);
}
EXPORT_SYMBOL_GPL(devm_turris_signing_key_create);

static int turris_signing_key_init(void)
{
	int err;

	err = register_key_type(&turris_signing_key_type);
	if (err)
		return err;

	turris_signing_keyring = keyring_alloc(".turris-signing-keys",
					       GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, current_cred(),
					       (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW |
					       KEY_USR_READ | KEY_USR_SEARCH,
					       KEY_ALLOC_BUILT_IN | KEY_ALLOC_SET_KEEP |
					       KEY_ALLOC_NOT_IN_QUOTA,
					       NULL, NULL);
	if (IS_ERR(turris_signing_keyring)) {
		pr_err("Cannot allocate Turris keyring\n");

		unregister_key_type(&turris_signing_key_type);

		return PTR_ERR(turris_signing_keyring);
	}

	return 0;
}
module_init(turris_signing_key_init);

static void turris_signing_key_exit(void)
{
	key_put(turris_signing_keyring);
	unregister_key_type(&turris_signing_key_type);
}
module_exit(turris_signing_key_exit);

MODULE_AUTHOR("Marek Behun <kabel@kernel.org>");
MODULE_DESCRIPTION("CZ.NIC's Turris signing key helper");
MODULE_LICENSE("GPL");
