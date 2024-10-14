// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey cca specific code
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
#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key CCA handler");

#if IS_MODULE(CONFIG_PKEY_CCA)
static struct ap_device_id pkey_cca_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX4 },
	{ .dev_type = AP_DEVICE_TYPE_CEX5 },
	{ .dev_type = AP_DEVICE_TYPE_CEX6 },
	{ .dev_type = AP_DEVICE_TYPE_CEX7 },
	{ .dev_type = AP_DEVICE_TYPE_CEX8 },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(ap, pkey_cca_card_ids);
#endif

/*
 * Check key blob for known and supported CCA key.
 */
static bool is_cca_key(const u8 *key, u32 keylen)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	if (keylen < sizeof(*hdr))
		return false;

	switch (hdr->type) {
	case TOKTYPE_CCA_INTERNAL:
		switch (hdr->version) {
		case TOKVER_CCA_AES:
		case TOKVER_CCA_VLSC:
			return true;
		default:
			return false;
		}
	case TOKTYPE_CCA_INTERNAL_PKA:
		return true;
	default:
		return false;
	}
}

static bool is_cca_keytype(enum pkey_key_type key_type)
{
	switch (key_type) {
	case PKEY_TYPE_CCA_DATA:
	case PKEY_TYPE_CCA_CIPHER:
	case PKEY_TYPE_CCA_ECC:
		return true;
	default:
		return false;
	}
}

static int cca_apqns4key(const u8 *key, u32 keylen, u32 flags,
			 struct pkey_apqn *apqns, size_t *nr_apqns)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

	if (!flags)
		flags = PKEY_FLAGS_MATCH_CUR_MKVP | PKEY_FLAGS_MATCH_ALT_MKVP;

	if (keylen < sizeof(struct keytoken_header))
		return -EINVAL;

	zcrypt_wait_api_operational();

	if (hdr->type == TOKTYPE_CCA_INTERNAL) {
		u64 cur_mkvp = 0, old_mkvp = 0;
		int minhwtype = ZCRYPT_CEX3C;

		if (hdr->version == TOKVER_CCA_AES) {
			struct secaeskeytoken *t = (struct secaeskeytoken *)key;

			if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
				cur_mkvp = t->mkvp;
			if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
				old_mkvp = t->mkvp;
		} else if (hdr->version == TOKVER_CCA_VLSC) {
			struct cipherkeytoken *t = (struct cipherkeytoken *)key;

			minhwtype = ZCRYPT_CEX6;
			if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
				cur_mkvp = t->mkvp0;
			if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
				old_mkvp = t->mkvp0;
		} else {
			/* unknown CCA internal token type */
			return -EINVAL;
		}
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   minhwtype, AES_MK_SET,
				   cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;

	} else if (hdr->type == TOKTYPE_CCA_INTERNAL_PKA) {
		struct eccprivkeytoken *t = (struct eccprivkeytoken *)key;
		u64 cur_mkvp = 0, old_mkvp = 0;

		if (t->secid == 0x20) {
			if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
				cur_mkvp = t->mkvp;
			if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
				old_mkvp = t->mkvp;
		} else {
			/* unknown CCA internal 2 token type */
			return -EINVAL;
		}
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   ZCRYPT_CEX7, APKA_MK_SET,
				   cur_mkvp, old_mkvp, 1);
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

static int cca_apqns4type(enum pkey_key_type ktype,
			  u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
			  struct pkey_apqn *apqns, size_t *nr_apqns)
{
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

	zcrypt_wait_api_operational();

	if (ktype == PKEY_TYPE_CCA_DATA || ktype == PKEY_TYPE_CCA_CIPHER) {
		u64 cur_mkvp = 0, old_mkvp = 0;
		int minhwtype = ZCRYPT_CEX3C;

		if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
			cur_mkvp = *((u64 *)cur_mkvp);
		if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
			old_mkvp = *((u64 *)alt_mkvp);
		if (ktype == PKEY_TYPE_CCA_CIPHER)
			minhwtype = ZCRYPT_CEX6;
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   minhwtype, AES_MK_SET,
				   cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;

	} else if (ktype == PKEY_TYPE_CCA_ECC) {
		u64 cur_mkvp = 0, old_mkvp = 0;

		if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
			cur_mkvp = *((u64 *)cur_mkvp);
		if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
			old_mkvp = *((u64 *)alt_mkvp);
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   ZCRYPT_CEX7, APKA_MK_SET,
				   cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;

	} else {
		PKEY_DBF_ERR("%s unknown/unsupported key type %d",
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

static int cca_key2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
			   const u8 *key, u32 keylen,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	struct pkey_apqn *local_apqns = NULL;
	int i, rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	if (hdr->type == TOKTYPE_CCA_INTERNAL &&
	    hdr->version == TOKVER_CCA_AES) {
		/* CCA AES data key */
		if (keylen != sizeof(struct secaeskeytoken))
			return -EINVAL;
		if (cca_check_secaeskeytoken(pkey_dbf_info, 3, key, 0))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
		   hdr->version == TOKVER_CCA_VLSC) {
		/* CCA AES cipher key */
		if (keylen < hdr->len || keylen > MAXCCAVLSCTOKENSIZE)
			return -EINVAL;
		if (cca_check_secaescipherkey(pkey_dbf_info,
					      3, key, 0, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_CCA_INTERNAL_PKA) {
		/* CCA ECC (private) key */
		if (keylen < sizeof(struct eccprivkeytoken))
			return -EINVAL;
		if (cca_check_sececckeytoken(pkey_dbf_info, 3, key, keylen, 1))
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
		rc = cca_apqns4key(key, keylen, 0, local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		if (hdr->type == TOKTYPE_CCA_INTERNAL &&
		    hdr->version == TOKVER_CCA_AES) {
			rc = cca_sec2protkey(apqns[i].card, apqns[i].domain,
					     key, protkey,
					     protkeylen, protkeytype);
		} else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
			   hdr->version == TOKVER_CCA_VLSC) {
			rc = cca_cipher2protkey(apqns[i].card, apqns[i].domain,
						key, protkey,
						protkeylen, protkeytype);
		} else if (hdr->type == TOKTYPE_CCA_INTERNAL_PKA) {
			rc = cca_ecc2protkey(apqns[i].card, apqns[i].domain,
					     key, protkey,
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
 * Generate CCA secure key.
 * As of now only CCA AES Data or Cipher secure keys are
 * supported.
 * keytype is one of the PKEY_KEYTYPE_* constants,
 * subtype may be 0 or PKEY_TYPE_CCA_DATA or PKEY_TYPE_CCA_CIPHER,
 * keybitsize is the bit size of the key (may be 0 for
 * keytype PKEY_KEYTYPE_AES_*).
 */
static int cca_gen_key(const struct pkey_apqn *apqns, size_t nr_apqns,
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
		case PKEY_TYPE_CCA_DATA:
		case PKEY_TYPE_CCA_CIPHER:
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
		rc = cca_apqns4type(subtype, NULL, NULL, 0,
				    local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		if (subtype == PKEY_TYPE_CCA_CIPHER) {
			rc = cca_gencipherkey(apqns[i].card, apqns[i].domain,
					      keybitsize, flags,
					      keybuf, keybuflen);
		} else {
			/* PKEY_TYPE_CCA_DATA */
			rc = cca_genseckey(apqns[i].card, apqns[i].domain,
					   keybitsize, keybuf);
			*keybuflen = (rc ? 0 : SECKEYBLOBSIZE);
		}
	}

out:
	kfree(local_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Generate CCA secure key with given clear key value.
 * As of now only CCA AES Data or Cipher secure keys are
 * supported.
 * keytype is one of the PKEY_KEYTYPE_* constants,
 * subtype may be 0 or PKEY_TYPE_CCA_DATA or PKEY_TYPE_CCA_CIPHER,
 * keybitsize is the bit size of the key (may be 0 for
 * keytype PKEY_KEYTYPE_AES_*).
 */
static int cca_clr2key(const struct pkey_apqn *apqns, size_t nr_apqns,
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
		case PKEY_TYPE_CCA_DATA:
		case PKEY_TYPE_CCA_CIPHER:
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
		rc = cca_apqns4type(subtype, NULL, NULL, 0,
				    local_apqns, &nr_apqns);
		if (rc)
			goto out;
		apqns = local_apqns;
	}

	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		if (subtype == PKEY_TYPE_CCA_CIPHER) {
			rc = cca_clr2cipherkey(apqns[i].card, apqns[i].domain,
					       keybitsize, flags, clrkey,
					       keybuf, keybuflen);
		} else {
			/* PKEY_TYPE_CCA_DATA */
			rc = cca_clr2seckey(apqns[i].card, apqns[i].domain,
					    keybitsize, clrkey, keybuf);
			*keybuflen = (rc ? 0 : SECKEYBLOBSIZE);
		}
	}

out:
	kfree(local_apqns);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int cca_verifykey(const u8 *key, u32 keylen,
			 u16 *card, u16 *dom,
			 u32 *keytype, u32 *keybitsize, u32 *flags)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 nr_apqns, *apqns = NULL;
	int rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	zcrypt_wait_api_operational();

	if (hdr->type == TOKTYPE_CCA_INTERNAL &&
	    hdr->version == TOKVER_CCA_AES) {
		struct secaeskeytoken *t = (struct secaeskeytoken *)key;

		rc = cca_check_secaeskeytoken(pkey_dbf_info, 3, key, 0);
		if (rc)
			goto out;
		*keytype = PKEY_TYPE_CCA_DATA;
		*keybitsize = t->bitsize;
		rc = cca_findcard2(&apqns, &nr_apqns, *card, *dom,
				   ZCRYPT_CEX3C, AES_MK_SET,
				   t->mkvp, 0, 1);
		if (!rc)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&apqns, &nr_apqns, *card, *dom,
					   ZCRYPT_CEX3C, AES_MK_SET,
					   0, t->mkvp, 1);
			if (!rc)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

		*card = ((struct pkey_apqn *)apqns)->card;
		*dom = ((struct pkey_apqn *)apqns)->domain;

	} else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
		   hdr->version == TOKVER_CCA_VLSC) {
		struct cipherkeytoken *t = (struct cipherkeytoken *)key;

		rc = cca_check_secaescipherkey(pkey_dbf_info, 3, key, 0, 1);
		if (rc)
			goto out;
		*keytype = PKEY_TYPE_CCA_CIPHER;
		*keybitsize = PKEY_SIZE_UNKNOWN;
		if (!t->plfver && t->wpllen == 512)
			*keybitsize = PKEY_SIZE_AES_128;
		else if (!t->plfver && t->wpllen == 576)
			*keybitsize = PKEY_SIZE_AES_192;
		else if (!t->plfver && t->wpllen == 640)
			*keybitsize = PKEY_SIZE_AES_256;
		rc = cca_findcard2(&apqns, &nr_apqns, *card, *dom,
				   ZCRYPT_CEX6, AES_MK_SET,
				   t->mkvp0, 0, 1);
		if (!rc)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&apqns, &nr_apqns, *card, *dom,
					   ZCRYPT_CEX6, AES_MK_SET,
					   0, t->mkvp0, 1);
			if (!rc)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

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
 * a protected key. This is done via an intermediate step
 * which creates a CCA AES DATA secure key first and then
 * derives the protected key from this secure key.
 */
static int cca_slowpath_key2protkey(const struct pkey_apqn *apqns,
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
	tmpbuf = kmalloc(SECKEYBLOBSIZE, GFP_ATOMIC);
	if (!tmpbuf)
		return -ENOMEM;

	/* try two times in case of failure */
	for (i = 0, rc = -ENODEV; i < 2 && rc; i++) {
		tmplen = SECKEYBLOBSIZE;
		rc = cca_clr2key(NULL, 0, t->keytype, PKEY_TYPE_CCA_DATA,
				 8 * keysize, 0, t->clearkey, t->len,
				 tmpbuf, &tmplen, NULL);
		pr_debug("cca_clr2key()=%d\n", rc);
		if (rc)
			continue;
		rc = cca_key2protkey(NULL, 0, tmpbuf, tmplen,
				     protkey, protkeylen, protkeytype);
		pr_debug("cca_key2protkey()=%d\n", rc);
	}

	kfree(tmpbuf);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static struct pkey_handler cca_handler = {
	.module			 = THIS_MODULE,
	.name			 = "PKEY CCA handler",
	.is_supported_key	 = is_cca_key,
	.is_supported_keytype	 = is_cca_keytype,
	.key_to_protkey		 = cca_key2protkey,
	.slowpath_key_to_protkey = cca_slowpath_key2protkey,
	.gen_key		 = cca_gen_key,
	.clr_to_key		 = cca_clr2key,
	.verify_key		 = cca_verifykey,
	.apqns_for_key		 = cca_apqns4key,
	.apqns_for_keytype	 = cca_apqns4type,
};

/*
 * Module init
 */
static int __init pkey_cca_init(void)
{
	/* register this module as pkey handler for all the cca stuff */
	return pkey_handler_register(&cca_handler);
}

/*
 * Module exit
 */
static void __exit pkey_cca_exit(void)
{
	/* unregister this module as pkey handler */
	pkey_handler_unregister(&cca_handler);
}

module_init(pkey_cca_init);
module_exit(pkey_cca_exit);
