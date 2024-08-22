// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey ep11 specific code
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

#include "pkey_base.h"

/*
 * Check key blob for known and supported EP11 key.
 */
bool pkey_is_ep11_key(const u8 *key, u32 keylen)
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

bool pkey_is_ep11_keytype(enum pkey_key_type key_type)
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

int pkey_ep11_key2protkey(u16 card, u16 dom,
			  const u8 *key, u32 keylen,
			  u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int rc;

	if (keylen < sizeof(*hdr))
		return -EINVAL;

	zcrypt_wait_api_operational();

	if (hdr->type == TOKTYPE_NON_CCA &&
	    hdr->version == TOKVER_EP11_AES_WITH_HEADER &&
	    is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 AES key blob with header */
		if (ep11_check_aes_key_with_hdr(pkey_dbf_info,
						3, key, keylen, 1))
			return -EINVAL;
		rc = ep11_kblob2protkey(card, dom, key, hdr->len,
					protkey, protkeylen,
					protkeytype);
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_ECC_WITH_HEADER &&
		   is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 ECC key blob with header */
		if (ep11_check_ecc_key_with_hdr(pkey_dbf_info,
						3, key, keylen, 1))
			return -EINVAL;
		rc = ep11_kblob2protkey(card, dom, key, hdr->len,
					protkey, protkeylen,
					protkeytype);
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES &&
		   is_ep11_keyblob(key)) {
		/* EP11 AES key blob with header in session field */
		if (ep11_check_aes_key(pkey_dbf_info, 3, key, keylen, 1))
			return -EINVAL;
		rc = ep11_kblob2protkey(card, dom, key, hdr->len,
					protkey, protkeylen,
					protkeytype);
	} else {
		PKEY_DBF_ERR("%s unknown/unsupported blob type %d version %d\n",
			     __func__, hdr->type, hdr->version);
		return -EINVAL;
	}

	pr_debug("card=%d dom=%d rc=%d\n", card, dom, rc);
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
int pkey_ep11_gen_key(u16 card, u16 dom,
		      u32 keytype, u32 subtype,
		      u32 keybitsize, u32 flags,
		      u8 *keybuf, u32 *keybuflen)
{
	int len, rc;

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
		case 0:
			subtype = PKEY_TYPE_EP11_AES;
			break;
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

	rc = ep11_genaeskey(card, dom, keybitsize, flags,
			    keybuf, keybuflen, subtype);

	pr_debug("card=%d dom=%d rc=%d\n", card, dom, rc);
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
int pkey_ep11_clr2key(u16 card, u16 dom,
		      u32 keytype, u32 subtype,
		      u32 keybitsize, u32 flags,
		      const u8 *clrkey, u32 clrkeylen,
		      u8 *keybuf, u32 *keybuflen)
{
	int len, rc;

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
		case 0:
			subtype = PKEY_TYPE_EP11_AES;
			break;
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

	rc = ep11_clr2keyblob(card, dom, keybitsize, flags,
			      clrkey, keybuf, keybuflen, subtype);

	pr_debug("card=%d dom=%d rc=%d\n", card, dom, rc);
	return rc;
}

int pkey_ep11_verifykey(const u8 *key, u32 keylen,
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

int pkey_ep11_apqns4key(const u8 *key, u32 keylen, u32 flags,
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

int pkey_ep11_apqns4type(enum pkey_key_type ktype,
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
