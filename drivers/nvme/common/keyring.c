// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Hannes Reinecke, SUSE Labs
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/key.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include <linux/nvme.h>
#include <linux/nvme-tcp.h>
#include <linux/nvme-keyring.h>

static struct key *nvme_keyring;

key_serial_t nvme_keyring_id(void)
{
	return nvme_keyring->serial;
}
EXPORT_SYMBOL_GPL(nvme_keyring_id);

static void nvme_tls_psk_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
	seq_printf(m, ": %u", key->datalen);
}

static bool nvme_tls_psk_match(const struct key *key,
			       const struct key_match_data *match_data)
{
	const char *match_id;
	size_t match_len;

	if (!key->description) {
		pr_debug("%s: no key description\n", __func__);
		return false;
	}
	match_len = strlen(key->description);
	pr_debug("%s: id %s len %zd\n", __func__, key->description, match_len);

	if (!match_data->raw_data) {
		pr_debug("%s: no match data\n", __func__);
		return false;
	}
	match_id = match_data->raw_data;
	pr_debug("%s: match '%s' '%s' len %zd\n",
		 __func__, match_id, key->description, match_len);
	return !memcmp(key->description, match_id, match_len);
}

static int nvme_tls_psk_match_preparse(struct key_match_data *match_data)
{
	match_data->lookup_type = KEYRING_SEARCH_LOOKUP_ITERATE;
	match_data->cmp = nvme_tls_psk_match;
	return 0;
}

static struct key_type nvme_tls_psk_key_type = {
	.name           = "psk",
	.flags          = KEY_TYPE_NET_DOMAIN,
	.preparse       = user_preparse,
	.free_preparse  = user_free_preparse,
	.match_preparse = nvme_tls_psk_match_preparse,
	.instantiate    = generic_key_instantiate,
	.revoke         = user_revoke,
	.destroy        = user_destroy,
	.describe       = nvme_tls_psk_describe,
	.read           = user_read,
};

static struct key *nvme_tls_psk_lookup(struct key *keyring,
		const char *hostnqn, const char *subnqn,
		int hmac, bool generated)
{
	char *identity;
	size_t identity_len = (NVMF_NQN_SIZE) * 2 + 11;
	key_ref_t keyref;
	key_serial_t keyring_id;

	identity = kzalloc(identity_len, GFP_KERNEL);
	if (!identity)
		return ERR_PTR(-ENOMEM);

	snprintf(identity, identity_len, "NVMe0%c%02d %s %s",
		 generated ? 'G' : 'R', hmac, hostnqn, subnqn);

	if (!keyring)
		keyring = nvme_keyring;
	keyring_id = key_serial(keyring);
	pr_debug("keyring %x lookup tls psk '%s'\n",
		 keyring_id, identity);
	keyref = keyring_search(make_key_ref(keyring, true),
				&nvme_tls_psk_key_type,
				identity, false);
	if (IS_ERR(keyref)) {
		pr_debug("lookup tls psk '%s' failed, error %ld\n",
			 identity, PTR_ERR(keyref));
		kfree(identity);
		return ERR_PTR(-ENOKEY);
	}
	kfree(identity);

	return key_ref_to_ptr(keyref);
}

/*
 * NVMe PSK priority list
 *
 * 'Retained' PSKs (ie 'generated == false')
 * should be preferred to 'generated' PSKs,
 * and SHA-384 should be preferred to SHA-256.
 */
static struct nvme_tls_psk_priority_list {
	bool generated;
	enum nvme_tcp_tls_cipher cipher;
} nvme_tls_psk_prio[] = {
	{ .generated = false,
	  .cipher = NVME_TCP_TLS_CIPHER_SHA384, },
	{ .generated = false,
	  .cipher = NVME_TCP_TLS_CIPHER_SHA256, },
	{ .generated = true,
	  .cipher = NVME_TCP_TLS_CIPHER_SHA384, },
	{ .generated = true,
	  .cipher = NVME_TCP_TLS_CIPHER_SHA256, },
};

/*
 * nvme_tls_psk_default - Return the preferred PSK to use for TLS ClientHello
 */
key_serial_t nvme_tls_psk_default(struct key *keyring,
		      const char *hostnqn, const char *subnqn)
{
	struct key *tls_key;
	key_serial_t tls_key_id;
	int prio;

	for (prio = 0; prio < ARRAY_SIZE(nvme_tls_psk_prio); prio++) {
		bool generated = nvme_tls_psk_prio[prio].generated;
		enum nvme_tcp_tls_cipher cipher = nvme_tls_psk_prio[prio].cipher;

		tls_key = nvme_tls_psk_lookup(keyring, hostnqn, subnqn,
					      cipher, generated);
		if (!IS_ERR(tls_key)) {
			tls_key_id = tls_key->serial;
			key_put(tls_key);
			return tls_key_id;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_tls_psk_default);

static int __init nvme_keyring_init(void)
{
	int err;

	nvme_keyring = keyring_alloc(".nvme",
				     GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				     current_cred(),
				     (KEY_POS_ALL & ~KEY_POS_SETATTR) |
				     (KEY_USR_ALL & ~KEY_USR_SETATTR),
				     KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(nvme_keyring))
		return PTR_ERR(nvme_keyring);

	err = register_key_type(&nvme_tls_psk_key_type);
	if (err) {
		key_put(nvme_keyring);
		return err;
	}
	return 0;
}

static void __exit nvme_keyring_exit(void)
{
	unregister_key_type(&nvme_tls_psk_key_type);
	key_revoke(nvme_keyring);
	key_put(nvme_keyring);
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.de>");
module_init(nvme_keyring_init);
module_exit(nvme_keyring_exit);
