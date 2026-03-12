// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Microsoft Corporation.
 *
 * Author:  Jaskaran Singh Khurana <jaskarankhurana@linux.microsoft.com>
 *
 */
#include <linux/device-mapper.h>
#include <linux/verification.h>
#include <linux/key.h>
#include <keys/user-type.h>
#include <linux/module.h>
#include "dm-verity.h"
#include "dm-verity-verify-sig.h"

#define DM_VERITY_VERIFY_ERR(s) DM_VERITY_ROOT_HASH_VERIFICATION " " s

static struct key *dm_verity_keyring;

static bool dm_verity_keyring_unsealed __ro_after_init;
module_param_named(keyring_unsealed, dm_verity_keyring_unsealed, bool, 0444);
MODULE_PARM_DESC(keyring_unsealed, "Leave the dm-verity keyring unsealed");

static bool require_signatures;
module_param(require_signatures, bool, 0444);
MODULE_PARM_DESC(require_signatures,
		"Verify the roothash of dm-verity hash tree");

#define DM_VERITY_IS_SIG_FORCE_ENABLED() \
	(require_signatures != false)

bool verity_verify_is_sig_opt_arg(const char *arg_name)
{
	return (!strcasecmp(arg_name,
			    DM_VERITY_ROOT_HASH_VERIFICATION_OPT_SIG_KEY));
}

static int verity_verify_get_sig_from_key(const char *key_desc,
					struct dm_verity_sig_opts *sig_opts)
{
	struct key *key;
	const struct user_key_payload *ukp;
	int ret = 0;

	key = request_key(&key_type_user,
			key_desc, NULL);
	if (IS_ERR(key))
		return PTR_ERR(key);

	down_read(&key->sem);

	ukp = user_key_payload_locked(key);
	if (!ukp) {
		ret = -EKEYREVOKED;
		goto end;
	}

	sig_opts->sig = kmalloc(ukp->datalen, GFP_KERNEL);
	if (!sig_opts->sig) {
		ret = -ENOMEM;
		goto end;
	}
	sig_opts->sig_size = ukp->datalen;

	memcpy(sig_opts->sig, ukp->data, sig_opts->sig_size);

end:
	up_read(&key->sem);
	key_put(key);

	return ret;
}

int verity_verify_sig_parse_opt_args(struct dm_arg_set *as,
				     struct dm_verity *v,
				     struct dm_verity_sig_opts *sig_opts,
				     unsigned int *argc,
				     const char *arg_name)
{
	struct dm_target *ti = v->ti;
	int ret;
	const char *sig_key = NULL;

	if (v->signature_key_desc) {
		ti->error = DM_VERITY_VERIFY_ERR("root_hash_sig_key_desc already specified");
		return -EINVAL;
	}

	if (!*argc) {
		ti->error = DM_VERITY_VERIFY_ERR("Signature key not specified");
		return -EINVAL;
	}

	sig_key = dm_shift_arg(as);
	(*argc)--;

	ret = verity_verify_get_sig_from_key(sig_key, sig_opts);
	if (ret < 0) {
		ti->error = DM_VERITY_VERIFY_ERR("Invalid key specified");
		return ret;
	}

	v->signature_key_desc = kstrdup(sig_key, GFP_KERNEL);
	if (!v->signature_key_desc) {
		ti->error = DM_VERITY_VERIFY_ERR("Could not allocate memory for signature key");
		return -ENOMEM;
	}

	return 0;
}

/*
 * verify_verify_roothash - Verify the root hash of the verity hash device
 *			     using builtin trusted keys.
 *
 * @root_hash: For verity, the roothash/data to be verified.
 * @root_hash_len: Size of the roothash/data to be verified.
 * @sig_data: The trusted signature that verifies the roothash/data.
 * @sig_len: Size of the signature.
 *
 */
int verity_verify_root_hash(const void *root_hash, size_t root_hash_len,
			    const void *sig_data, size_t sig_len)
{
	int ret;

	if (!root_hash || root_hash_len == 0)
		return -EINVAL;

	if (!sig_data  || sig_len == 0) {
		if (DM_VERITY_IS_SIG_FORCE_ENABLED())
			return -ENOKEY;
		else
			return 0;
	}

	ret = verify_pkcs7_signature(root_hash, root_hash_len, sig_data,
				sig_len,
#ifdef CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG_SECONDARY_KEYRING
				VERIFY_USE_SECONDARY_KEYRING,
#else
				NULL,
#endif
				VERIFYING_UNSPECIFIED_SIGNATURE, NULL, NULL);
#ifdef CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG_PLATFORM_KEYRING
	if (ret == -ENOKEY || ret == -EKEYREJECTED)
		ret = verify_pkcs7_signature(root_hash, root_hash_len, sig_data,
					sig_len,
					VERIFY_USE_PLATFORM_KEYRING,
					VERIFYING_UNSPECIFIED_SIGNATURE, NULL, NULL);
#endif

	if (ret != -ENOKEY && ret != -EKEYREJECTED)
		return ret;

	if (dm_verity_keyring->keys.nr_leaves_on_tree &&
	    dm_verity_keyring->restrict_link)
		ret = verify_pkcs7_signature(root_hash, root_hash_len,
					     sig_data, sig_len,
					     dm_verity_keyring,
					     VERIFYING_UNSPECIFIED_SIGNATURE,
					     NULL, NULL);

	return ret;
}

void verity_verify_sig_opts_cleanup(struct dm_verity_sig_opts *sig_opts)
{
	kfree(sig_opts->sig);
	sig_opts->sig = NULL;
	sig_opts->sig_size = 0;
}

int __init dm_verity_verify_sig_init(void)
{
	dm_verity_keyring = keyring_alloc(".dm-verity",
					  GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
					  current_cred(),
					  KEY_POS_SEARCH |
					  KEY_USR_VIEW | KEY_USR_READ |
					  KEY_USR_WRITE | KEY_USR_SEARCH |
					  KEY_USR_SETATTR,
					  KEY_ALLOC_NOT_IN_QUOTA,
					  NULL, NULL);
	if (IS_ERR(dm_verity_keyring))
		panic("dm-verity can't allocate keyring\n");

	if (!dm_verity_keyring_unsealed &&
	    keyring_restrict(make_key_ref(dm_verity_keyring, true), NULL, NULL))
		panic("dm-verity can't seal keyring\n");

	return 0;
}

void dm_verity_verify_sig_exit(void)
{
	key_revoke(dm_verity_keyring);
	key_put(dm_verity_keyring);
}
