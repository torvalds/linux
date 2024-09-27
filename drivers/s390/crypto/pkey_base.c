// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey base: debug feature, pkey handler registry
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rculist.h>

#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key base and api");

/*
 * pkey debug feature
 */
debug_info_t *pkey_dbf_info;
EXPORT_SYMBOL(pkey_dbf_info);

/*
 * pkey handler registry
 */

static DEFINE_SPINLOCK(handler_list_write_lock);
static LIST_HEAD(handler_list);

int pkey_handler_register(struct pkey_handler *handler)
{
	const struct pkey_handler *h;

	if (!handler ||
	    !handler->is_supported_key ||
	    !handler->is_supported_keytype)
		return -EINVAL;

	if (!try_module_get(handler->module))
		return -ENXIO;

	spin_lock(&handler_list_write_lock);

	rcu_read_lock();
	list_for_each_entry_rcu(h, &handler_list, list) {
		if (h == handler) {
			rcu_read_unlock();
			spin_unlock(&handler_list_write_lock);
			module_put(handler->module);
			return -EEXIST;
		}
	}
	rcu_read_unlock();

	list_add_rcu(&handler->list, &handler_list);
	spin_unlock(&handler_list_write_lock);
	synchronize_rcu();

	module_put(handler->module);

	PKEY_DBF_INFO("%s pkey handler '%s' registered\n", __func__,
		      handler->name ?: "<no name>");

	return 0;
}
EXPORT_SYMBOL(pkey_handler_register);

int pkey_handler_unregister(struct pkey_handler *handler)
{
	spin_lock(&handler_list_write_lock);
	list_del_rcu(&handler->list);
	INIT_LIST_HEAD_RCU(&handler->list);
	spin_unlock(&handler_list_write_lock);
	synchronize_rcu();

	PKEY_DBF_INFO("%s pkey handler '%s' unregistered\n", __func__,
		      handler->name ?: "<no name>");

	return 0;
}
EXPORT_SYMBOL(pkey_handler_unregister);

/*
 * Handler invocation functions.
 */

const struct pkey_handler *pkey_handler_get_keybased(const u8 *key, u32 keylen)
{
	const struct pkey_handler *h;

	rcu_read_lock();
	list_for_each_entry_rcu(h, &handler_list, list) {
		if (!try_module_get(h->module))
			continue;
		if (h->is_supported_key(key, keylen)) {
			rcu_read_unlock();
			return h;
		}
		module_put(h->module);
	}
	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL(pkey_handler_get_keybased);

const struct pkey_handler *pkey_handler_get_keytypebased(enum pkey_key_type kt)
{
	const struct pkey_handler *h;

	rcu_read_lock();
	list_for_each_entry_rcu(h, &handler_list, list) {
		if (!try_module_get(h->module))
			continue;
		if (h->is_supported_keytype(kt)) {
			rcu_read_unlock();
			return h;
		}
		module_put(h->module);
	}
	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL(pkey_handler_get_keytypebased);

void pkey_handler_put(const struct pkey_handler *handler)
{
	const struct pkey_handler *h;

	if (!handler)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(h, &handler_list, list) {
		if (h == handler) {
			module_put(h->module);
			break;
		}
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(pkey_handler_put);

int pkey_handler_key_to_protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
				const u8 *key, u32 keylen,
				u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keybased(key, keylen);
	if (h && h->key_to_protkey) {
		rc = h->key_to_protkey(apqns, nr_apqns, key, keylen,
				       protkey, protkeylen,
				       protkeytype);
	}
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_key_to_protkey);

/*
 * This handler invocation is special as there may be more than
 * one handler providing support for the very same key (type).
 * And the handler may not respond true on is_supported_key(),
 * so simple try and check return value here.
 */
int pkey_handler_slowpath_key_to_protkey(const struct pkey_apqn *apqns,
					 size_t nr_apqns,
					 const u8 *key, u32 keylen,
					 u8 *protkey, u32 *protkeylen,
					 u32 *protkeytype)
{
	const struct pkey_handler *h, *htmp[10];
	int i, n = 0, rc = -ENODEV;

	rcu_read_lock();
	list_for_each_entry_rcu(h, &handler_list, list) {
		if (!try_module_get(h->module))
			continue;
		if (h->slowpath_key_to_protkey && n < ARRAY_SIZE(htmp))
			htmp[n++] = h;
		else
			module_put(h->module);
	}
	rcu_read_unlock();

	for (i = 0; i < n; i++) {
		h = htmp[i];
		if (rc)
			rc = h->slowpath_key_to_protkey(apqns, nr_apqns,
							key, keylen,
							protkey, protkeylen,
							protkeytype);
		module_put(h->module);
	}

	return rc;
}
EXPORT_SYMBOL(pkey_handler_slowpath_key_to_protkey);

int pkey_handler_gen_key(const struct pkey_apqn *apqns, size_t nr_apqns,
			 u32 keytype, u32 keysubtype,
			 u32 keybitsize, u32 flags,
			 u8 *keybuf, u32 *keybuflen, u32 *keyinfo)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keytypebased(keysubtype);
	if (h && h->gen_key) {
		rc = h->gen_key(apqns, nr_apqns, keytype, keysubtype,
				keybitsize, flags,
				keybuf, keybuflen, keyinfo);
	}
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_gen_key);

int pkey_handler_clr_to_key(const struct pkey_apqn *apqns, size_t nr_apqns,
			    u32 keytype, u32 keysubtype,
			    u32 keybitsize, u32 flags,
			    const u8 *clrkey, u32 clrkeylen,
			    u8 *keybuf, u32 *keybuflen, u32 *keyinfo)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keytypebased(keysubtype);
	if (h && h->clr_to_key) {
		rc = h->clr_to_key(apqns, nr_apqns, keytype, keysubtype,
				   keybitsize, flags, clrkey, clrkeylen,
				   keybuf, keybuflen, keyinfo);
	}
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_clr_to_key);

int pkey_handler_verify_key(const u8 *key, u32 keylen,
			    u16 *card, u16 *dom,
			    u32 *keytype, u32 *keybitsize, u32 *flags)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keybased(key, keylen);
	if (h && h->verify_key) {
		rc = h->verify_key(key, keylen, card, dom,
				   keytype, keybitsize, flags);
	}
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_verify_key);

int pkey_handler_apqns_for_key(const u8 *key, u32 keylen, u32 flags,
			       struct pkey_apqn *apqns, size_t *nr_apqns)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keybased(key, keylen);
	if (h && h->apqns_for_key)
		rc = h->apqns_for_key(key, keylen, flags, apqns, nr_apqns);
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_apqns_for_key);

int pkey_handler_apqns_for_keytype(enum pkey_key_type keysubtype,
				   u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
				   struct pkey_apqn *apqns, size_t *nr_apqns)
{
	const struct pkey_handler *h;
	int rc = -ENODEV;

	h = pkey_handler_get_keytypebased(keysubtype);
	if (h && h->apqns_for_keytype) {
		rc = h->apqns_for_keytype(keysubtype,
					  cur_mkvp, alt_mkvp, flags,
					  apqns, nr_apqns);
	}
	pkey_handler_put(h);

	return rc;
}
EXPORT_SYMBOL(pkey_handler_apqns_for_keytype);

void pkey_handler_request_modules(void)
{
#ifdef CONFIG_MODULES
	static const char * const pkey_handler_modules[] = {
		"pkey_cca", "pkey_ep11", "pkey_pckmo" };
	int i;

	for (i = 0; i < ARRAY_SIZE(pkey_handler_modules); i++) {
		const struct pkey_handler *h;
		bool found = false;

		rcu_read_lock();
		list_for_each_entry_rcu(h, &handler_list, list) {
			if (h->module &&
			    !strcmp(h->module->name, pkey_handler_modules[i])) {
				found = true;
				break;
			}
		}
		rcu_read_unlock();
		if (!found) {
			pr_debug("request_module(%s)\n", pkey_handler_modules[i]);
			request_module(pkey_handler_modules[i]);
		}
	}
#endif
}
EXPORT_SYMBOL(pkey_handler_request_modules);

/*
 * Module init
 */
static int __init pkey_init(void)
{
	int rc;

	/* init debug feature */
	pkey_dbf_info = debug_register("pkey", 1, 1, 5 * sizeof(long));
	debug_register_view(pkey_dbf_info, &debug_sprintf_view);
	debug_set_level(pkey_dbf_info, 4);

	/* the handler registry does not need any init */

	rc = pkey_api_init();
	if (rc)
		debug_unregister(pkey_dbf_info);

	return rc;
}

/*
 * Module exit
 */
static void __exit pkey_exit(void)
{
	pkey_api_exit();
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, pkey_init);
module_exit(pkey_exit);
