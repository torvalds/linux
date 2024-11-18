// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey ep11 specific code
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufeature.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"
#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key EP11 handler");

#if IS_MODULE(CONFIG_PKEY_EP11)
static struct ap_device_id pkey_ep11_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX4 },
	{ .dev_type = AP_DEVICE_TYPE_CEX5 },
	{ .dev_type = AP_DEVICE_TYPE_CEX6 },
	{ .dev_type = AP_DEVICE_TYPE_CEX7 },
	{ .dev_type = AP_DEVICE_TYPE_CEX8 },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(ap, pkey_ep11_card_ids);
#endif

/*
 * Check key blob for known and supported EP11 key.
 */
static bool is_ep11_key(const u8 *key, u32 keylen)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	if (keylen < sizeof(*hdr))
		return false;

	switch (hdr->type) {
	case TOKTYPE_NON_CCA:
		switch (hdr->version) {
		case TOKVER_EP11_AES:
		case TOKVER_EP11_AES_WITH_HEADER:
		case TOKVER_EP11_ECC_WITH_HEADER:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

static bool is_ep11_keytype(enum pkey_key_type key_type)
{
	switch (key_type) {
	case PKEY_TYPE_EP11:
	case PKEY_TYPE_EP11_AES:
	case PKEY_TYPE_EP11_ECC:
		return true;
	default:
		return false;
	}
}

static int ep11_apqns4key(const u8 *key, u32 keylen, u32 flags,
			  struct pkey_apqn *apqns, size_t *nr_apqns)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

	if (!flags)
		flags = PKEY_FLAGS_MATCH_CUR_MKVP;

	if (keylen < sizeof(struct keytoken_header) || flags == 0)
		return -EINVAL;

	zcrypt_wait_api_operational();

	if (hdr->type == TOKTYPE_NON_CCA &&
	    (hdr->version == TOKVER_EP11_AES_WITH_HEADER ||
	     hdr->version == TOKVER_EP11_ECC_WITH_HEADER) &&
	    is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		struct ep11keyblob *kb = (struct ep11keyblob *)
			(key + sizeof(struct ep11kblob_header));
		int minhwtype = 0, api = 0;

		if (flags != PKEY_FLAGS_MATCH_CUR_MKVP)
			return -EINVAL;
		if (kb->attr & EP11_BLOB_PKEY_EXTRACTABLE) {
			minhwtype = ZCRYPT_CEX7;
			api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		}
		rc = ep11_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				    minhwtype, api, kb->wkvp);
		if (rc)
			goto out;

	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES &&
		   is_ep11_keyblob(key)) {
		struct ep11keyblob *kb = (struct ep11keyblob *)key;
		int minhwtype = 0, api = 0;

		if (flags != PKEY_FLAGS_MATCH_CUR_MKVP)
			return -EINVAL;
		if (kb->attr & EP11_BLOB_PKEY_EXTRACTABLE) {
			minhwtype = ZCRYPT_CEX7;
			api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		}
		rc = ep11_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				    minhwtype, api, kb->wkvp);
		if (rc)
			goto out;

	} else {
		PKEY_DBF_ERR("%s unknown/unsupported blob type %d version %d\n",
			     __func__, hdr->type, hdr->version);
		return -EINVAL;
	}

	if (apqns) {
		if (*nr_apqns < _nr_apqns)
			rc = -ENOSPC;
		else
			memcpy(apqns, _apqns, _nr_apqns * sizeof(u32));
	}
	*nr_apqns = _nr_apqns;

out:
	kfree(_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ep11_apqns4type(enum pkey_key_type ktype,
			   u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
			   struct pkey_apqn *apqns, size_t *nr_apqns)
{
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

	zcrypt_wait_api_operational();

	if (ktype == PKEY_TYPE_EP11 ||
	    ktype == PKEY_TYPE_EP11_AES ||
	    ktype == PKEY_TYPE_EP11_ECC) {
		u8 *wkvp = NULL;
		int api;

		if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
			wkvp = cur_mkvp;
		api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		rc = ep11_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				    ZCRYPT_CEX7, api, wkvp);
		if (rc)
			goto out;

	} else {
		PKEY_DBF_ERR("%s unknown/unsupported key type %d\n",
			     __func__, (int)ktype);
		return -EINVAL;
	}

	if (apqns) {
		if (*nr_apqns < _nr_apqns)
			rc = -ENOSPC;
		else
			memcpy(apqns, _apqns, _nr_apqns * sizeof(u32));
	}
	*nr_apqns = _nr_apqns;

out:
	kfree(_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ep11_key2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
			    const u8 *key, u32 keylen,
			    u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	struct pkey_apqn *local_apqns = NULL;
	int i, rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	if (hdr->type == TOKTYPE_NON_CCA &&
	    hdr->version == TOKVER_EP11_AES_WITH_HEADER &&
	    is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 AES key blob with header */
		if (ep11_check_aes_key_with_hdr(pkey_dbf_info,
						3, key, keylen, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_ECC_WITH_HEADER &&
		   is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 ECC key blob with header */
		if (ep11_check_ecc_key_with_hdr(pkey_dbf_info,
						3, key, keylen, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES &&
		   is_ep11_keyblob(key)) {
		/* EP11 AES key blob with header in session field */
		if (ep11_check_aes_key(pkey_dbf_info, 3, key, keylen, 1))
			return -EINVAL;
	} else {
		PKEY_DBF_ERR("%s unknown/unsupported blob type %d version %d\n",
			     __func__, hdr->type, hdr->version);
		return -EINVAL;
	}

	zcrypt_wait_api_operational();

	if (!apqns || (nr_apqns == 1 &&
		       apqns[0].card == 0xFFFF && apqns[0].domain == 0xFFFF)) {
		nr_apqns = MAXAPQNSINLIST;
		local_apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn),
					    GFP_KERNEL);
		if (!local_apqns)
			return -ENOMEM;
		rc = ep11_apqns4key(key, keylen, 0, local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		if (hdr->type == TOKTYPE_NON_CCA &&
		    hdr->version == TOKVER_EP11_AES_WITH_HEADER &&
		    is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
			rc = ep11_kblob2protkey(apqns[i].card, apqns[i].domain,
						key, hdr->len, protkey,
						protkeylen, protkeytype);
		} else if (hdr->type == TOKTYPE_NON_CCA &&
			   hdr->version == TOKVER_EP11_ECC_WITH_HEADER &&
			   is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
			rc = ep11_kblob2protkey(apqns[i].card, apqns[i].domain,
						key, hdr->len, protkey,
						protkeylen, protkeytype);
		} else if (hdr->type == TOKTYPE_NON_CCA &&
			   hdr->version == TOKVER_EP11_AES &&
			   is_ep11_keyblob(key)) {
			rc = ep11_kblob2protkey(apqns[i].card, apqns[i].domain,
						key, hdr->len, protkey,
						protkeylen, protkeytype);
		} else {
			rc = -EINVAL;
			break;
		}
	}

out:
	kfree(local_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Generate EP11 secure key.
 * As of now only EP11 AES secure keys are supported.
 * keytype is one of the PKEY_KEYTYPE_* constants,
 * subtype may be PKEY_TYPE_EP11 or PKEY_TYPE_EP11_AES
 * or 0 (results in subtype PKEY_TYPE_EP11_AES),
 * keybitsize is the bit size of the key (may be 0 for
 * keytype PKEY_KEYTYPE_AES_*).
 */
static int ep11_gen_key(const struct pkey_apqn *apqns, size_t nr_apqns,
			u32 keytype, u32 subtype,
			u32 keybitsize, u32 flags,
			u8 *keybuf, u32 *keybuflen, u32 *_keyinfo)
{
	struct pkey_apqn *local_apqns = NULL;
	int i, len, rc;

	/* check keytype, subtype, keybitsize */
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
	case PKEY_KEYTYPE_AES_192:
	case PKEY_KEYTYPE_AES_256:
		len = pkey_keytype_aes_to_size(keytype);
		if (keybitsize && keybitsize != 8 * len) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, keybitsize);
			return -EINVAL;
		}
		keybitsize = 8 * len;
		switch (subtype) {
		case PKEY_TYPE_EP11:
		case PKEY_TYPE_EP11_AES:
			break;
		default:
			PKEY_DBF_ERR("%s unknown/unsupported subtype %d\n",
				     __func__, subtype);
			return -EINVAL;
		}
		break;
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n",
			     __func__, keytype);
		return -EINVAL;
	}

	zcrypt_wait_api_operational();

	if (!apqns || (nr_apqns == 1 &&
		       apqns[0].card == 0xFFFF && apqns[0].domain == 0xFFFF)) {
		nr_apqns = MAXAPQNSINLIST;
		local_apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn),
					    GFP_KERNEL);
		if (!local_apqns)
			return -ENOMEM;
		rc = ep11_apqns4type(subtype, NULL, NULL, 0,
				     local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		rc = ep11_genaeskey(apqns[i].card, apqns[i].domain,
				    keybitsize, flags,
				    keybuf, keybuflen, subtype);
	}

out:
	kfree(local_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Generate EP11 secure key with given clear key value.
 * As of now only EP11 AES secure keys are supported.
 * keytype is one of the PKEY_KEYTYPE_* constants,
 * subtype may be PKEY_TYPE_EP11 or PKEY_TYPE_EP11_AES
 * or 0 (assumes PKEY_TYPE_EP11_AES then).
 * keybitsize is the bit size of the key (may be 0 for
 * keytype PKEY_KEYTYPE_AES_*).
 */
static int ep11_clr2key(const struct pkey_apqn *apqns, size_t nr_apqns,
			u32 keytype, u32 subtype,
			u32 keybitsize, u32 flags,
			const u8 *clrkey, u32 clrkeylen,
			u8 *keybuf, u32 *keybuflen, u32 *_keyinfo)
{
	struct pkey_apqn *local_apqns = NULL;
	int i, len, rc;

	/* check keytype, subtype, clrkeylen, keybitsize */
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
	case PKEY_KEYTYPE_AES_192:
	case PKEY_KEYTYPE_AES_256:
		len = pkey_keytype_aes_to_size(keytype);
		if (keybitsize && keybitsize != 8 * len) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, keybitsize);
			return -EINVAL;
		}
		keybitsize = 8 * len;
		if (clrkeylen != len) {
			PKEY_DBF_ERR("%s invalid clear key len %d != %d\n",
				     __func__, clrkeylen, len);
			return -EINVAL;
		}
		switch (subtype) {
		case PKEY_TYPE_EP11:
		case PKEY_TYPE_EP11_AES:
			break;
		default:
			PKEY_DBF_ERR("%s unknown/unsupported subtype %d\n",
				     __func__, subtype);
			return -EINVAL;
		}
		break;
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n",
			     __func__, keytype);
		return -EINVAL;
	}

	zcrypt_wait_api_operational();

	if (!apqns || (nr_apqns == 1 &&
		       apqns[0].card == 0xFFFF && apqns[0].domain == 0xFFFF)) {
		nr_apqns = MAXAPQNSINLIST;
		local_apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn),
					    GFP_KERNEL);
		if (!local_apqns)
			return -ENOMEM;
		rc = ep11_apqns4type(subtype, NULL, NULL, 0,
				     local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		rc = ep11_clr2keyblob(apqns[i].card, apqns[i].domain,
				      keybitsize, flags, clrkey,
				      keybuf, keybuflen, subtype);
	}

out:
	kfree(local_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ep11_verifykey(const u8 *key, u32 keylen,
			  u16 *card, u16 *dom,
			  u32 *keytype, u32 *keybitsize, u32 *flags)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 nr_apqns, *apqns = NULL;
	int rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	zcrypt_wait_api_operational();

	if (hdr->type == TOKTYPE_NON_CCA &&
	    hdr->version == TOKVER_EP11_AES) {
		struct ep11keyblob *kb = (struct ep11keyblob *)key;
		int api;

		rc = ep11_check_aes_key(pkey_dbf_info, 3, key, keylen, 1);
		if (rc)
			goto out;
		*keytype = PKEY_TYPE_EP11;
		*keybitsize = kb->head.bitlen;

		api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		rc = ep11_findcard2(&apqns, &nr_apqns, *card, *dom,
				    ZCRYPT_CEX7, api,
				    ep11_kb_wkvp(key, keylen));
		if (rc)
			goto out;

		*flags = PKEY_FLAGS_MATCH_CUR_MKVP;

		*card = ((struct pkey_apqn *)apqns)->card;
		*dom = ((struct pkey_apqn *)apqns)->domain;

	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES_WITH_HEADER) {
		struct ep11kblob_header *kh = (struct ep11kblob_header *)key;
		int api;

		rc = ep11_check_aes_key_with_hdr(pkey_dbf_info,
						 3, key, keylen, 1);
		if (rc)
			goto out;
		*keytype = PKEY_TYPE_EP11_AES;
		*keybitsize = kh->bitlen;

		api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		rc = ep11_findcard2(&apqns, &nr_apqns, *card, *dom,
				    ZCRYPT_CEX7, api,
				    ep11_kb_wkvp(key, keylen));
		if (rc)
			goto out;

		*flags = PKEY_FLAGS_MATCH_CUR_MKVP;

		*card = ((struct pkey_apqn *)apqns)->card;
		*dom = ((struct pkey_apqn *)apqns)->domain;

	} else {
		/* unknown/unsupported key blob */
		rc = -EINVAL;
	}

out:
	kfree(apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * This function provides an alternate but usually slow way
 * to convert a 'clear key token' with AES key material into
 * a protected key. That is done via an intermediate step
 * which creates an EP11 AES secure key first and then derives
 * the protected key from this secure key.
 */
static int ep11_slowpath_key2protkey(const struct pkey_apqn *apqns,
				     size_t nr_apqns,
				     const u8 *key, u32 keylen,
				     u8 *protkey, u32 *protkeylen,
				     u32 *protkeytype)
{
	const struct keytoken_header *hdr = (const struct keytoken_header *)key;
	const struct clearkeytoken *t = (const struct clearkeytoken *)key;
	u32 tmplen, keysize = 0;
	u8 *tmpbuf;
	int i, rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	if (hdr->type == TOKTYPE_NON_CCA &&
	    hdr->version == TOKVER_CLEAR_KEY)
		keysize = pkey_keytype_aes_to_size(t->keytype);
	if (!keysize || t->len != keysize)
		return -EINVAL;

	/* alloc tmp key buffer */
	tmpbuf = kmalloc(MAXEP11AESKEYBLOBSIZE, GFP_ATOMIC);
	if (!tmpbuf)
		return -ENOMEM;

	/* try two times in case of failure */
	for (i = 0, rc = -ENODEV; i < 2 && rc; i++) {
		tmplen = MAXEP11AESKEYBLOBSIZE;
		rc = ep11_clr2key(NULL, 0, t->keytype, PKEY_TYPE_EP11,
				  8 * keysize, 0, t->clearkey, t->len,
				  tmpbuf, &tmplen, NULL);
		pr_debug("ep11_clr2key()=%d\n", rc);
		if (rc)
			continue;
		rc = ep11_key2protkey(NULL, 0, tmpbuf, tmplen,
				      protkey, protkeylen, protkeytype);
		pr_debug("ep11_key2protkey()=%d\n", rc);
	}

	kfree(tmpbuf);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static struct pkey_handler ep11_handler = {
	.module			 = THIS_MODULE,
	.name			 = "PKEY EP11 handler",
	.is_supported_key	 = is_ep11_key,
	.is_supported_keytype	 = is_ep11_keytype,
	.key_to_protkey		 = ep11_key2protkey,
	.slowpath_key_to_protkey = ep11_slowpath_key2protkey,
	.gen_key		 = ep11_gen_key,
	.clr_to_key		 = ep11_clr2key,
	.verify_key		 = ep11_verifykey,
	.apqns_for_key		 = ep11_apqns4key,
	.apqns_for_keytype	 = ep11_apqns4type,
};

/*
 * Module init
 */
static int __init pkey_ep11_init(void)
{
	/* register this module as pkey handler for all the ep11 stuff */
	return pkey_handler_register(&ep11_handler);
}

/*
 * Module exit
 */
static void __exit pkey_ep11_exit(void)
{
	/* unregister this module as pkey handler */
	pkey_handler_unregister(&ep11_handler);
}

module_init(pkey_ep11_init);
module_exit(pkey_ep11_exit);
