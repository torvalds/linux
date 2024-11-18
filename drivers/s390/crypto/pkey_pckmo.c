// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey pckmo specific code
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <asm/cpacf.h>
#include <crypto/aes.h>
#include <linux/random.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key PCKMO handler");

/*
 * Check key blob for known and supported here.
 */
static bool is_pckmo_key(const u8 *key, u32 keylen)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	struct clearkeytoken *t = (struct clearkeytoken *)key;

	if (keylen < sizeof(*hdr))
		return false;

	switch (hdr->type) {
	case TOKTYPE_NON_CCA:
		switch (hdr->version) {
		case TOKVER_CLEAR_KEY:
			switch (t->keytype) {
			case PKEY_KEYTYPE_AES_128:
			case PKEY_KEYTYPE_AES_192:
			case PKEY_KEYTYPE_AES_256:
			case PKEY_KEYTYPE_ECC_P256:
			case PKEY_KEYTYPE_ECC_P384:
			case PKEY_KEYTYPE_ECC_P521:
			case PKEY_KEYTYPE_ECC_ED25519:
			case PKEY_KEYTYPE_ECC_ED448:
			case PKEY_KEYTYPE_AES_XTS_128:
			case PKEY_KEYTYPE_AES_XTS_256:
			case PKEY_KEYTYPE_HMAC_512:
			case PKEY_KEYTYPE_HMAC_1024:
				return true;
			default:
				return false;
			}
		case TOKVER_PROTECTED_KEY:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

static bool is_pckmo_keytype(enum pkey_key_type keytype)
{
	switch (keytype) {
	case PKEY_TYPE_PROTKEY:
		return true;
	default:
		return false;
	}
}

/*
 * Create a protected key from a clear key value via PCKMO instruction.
 */
static int pckmo_clr2protkey(u32 keytype, const u8 *clrkey, u32 clrkeylen,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	/* mask of available pckmo subfunctions */
	static cpacf_mask_t pckmo_functions;

	int keysize, rc = -EINVAL;
	u8 paramblock[160];
	u32 pkeytype;
	long fc;

	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		/* 16 byte key, 32 byte aes wkvp, total 48 bytes */
		keysize = 16;
		pkeytype = keytype;
		fc = CPACF_PCKMO_ENC_AES_128_KEY;
		break;
	case PKEY_KEYTYPE_AES_192:
		/* 24 byte key, 32 byte aes wkvp, total 56 bytes */
		keysize = 24;
		pkeytype = keytype;
		fc = CPACF_PCKMO_ENC_AES_192_KEY;
		break;
	case PKEY_KEYTYPE_AES_256:
		/* 32 byte key, 32 byte aes wkvp, total 64 bytes */
		keysize = 32;
		pkeytype = keytype;
		fc = CPACF_PCKMO_ENC_AES_256_KEY;
		break;
	case PKEY_KEYTYPE_ECC_P256:
		/* 32 byte key, 32 byte aes wkvp, total 64 bytes */
		keysize = 32;
		pkeytype = PKEY_KEYTYPE_ECC;
		fc = CPACF_PCKMO_ENC_ECC_P256_KEY;
		break;
	case PKEY_KEYTYPE_ECC_P384:
		/* 48 byte key, 32 byte aes wkvp, total 80 bytes */
		keysize = 48;
		pkeytype = PKEY_KEYTYPE_ECC;
		fc = CPACF_PCKMO_ENC_ECC_P384_KEY;
		break;
	case PKEY_KEYTYPE_ECC_P521:
		/* 80 byte key, 32 byte aes wkvp, total 112 bytes */
		keysize = 80;
		pkeytype = PKEY_KEYTYPE_ECC;
		fc = CPACF_PCKMO_ENC_ECC_P521_KEY;
		break;
	case PKEY_KEYTYPE_ECC_ED25519:
		/* 32 byte key, 32 byte aes wkvp, total 64 bytes */
		keysize = 32;
		pkeytype = PKEY_KEYTYPE_ECC;
		fc = CPACF_PCKMO_ENC_ECC_ED25519_KEY;
		break;
	case PKEY_KEYTYPE_ECC_ED448:
		/* 64 byte key, 32 byte aes wkvp, total 96 bytes */
		keysize = 64;
		pkeytype = PKEY_KEYTYPE_ECC;
		fc = CPACF_PCKMO_ENC_ECC_ED448_KEY;
		break;
	case PKEY_KEYTYPE_AES_XTS_128:
		/* 2x16 byte keys, 32 byte aes wkvp, total 64 bytes */
		keysize = 32;
		pkeytype = PKEY_KEYTYPE_AES_XTS_128;
		fc = CPACF_PCKMO_ENC_AES_XTS_128_DOUBLE_KEY;
		break;
	case PKEY_KEYTYPE_AES_XTS_256:
		/* 2x32 byte keys, 32 byte aes wkvp, total 96 bytes */
		keysize = 64;
		pkeytype = PKEY_KEYTYPE_AES_XTS_256;
		fc = CPACF_PCKMO_ENC_AES_XTS_256_DOUBLE_KEY;
		break;
	case PKEY_KEYTYPE_HMAC_512:
		/* 64 byte key, 32 byte aes wkvp, total 96 bytes */
		keysize = 64;
		pkeytype = PKEY_KEYTYPE_HMAC_512;
		fc = CPACF_PCKMO_ENC_HMAC_512_KEY;
		break;
	case PKEY_KEYTYPE_HMAC_1024:
		/* 128 byte key, 32 byte aes wkvp, total 160 bytes */
		keysize = 128;
		pkeytype = PKEY_KEYTYPE_HMAC_1024;
		fc = CPACF_PCKMO_ENC_HMAC_1024_KEY;
		break;
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %u\n",
			     __func__, keytype);
		goto out;
	}

	if (clrkeylen && clrkeylen < keysize) {
		PKEY_DBF_ERR("%s clear key size too small: %u < %d\n",
			     __func__, clrkeylen, keysize);
		goto out;
	}
	if (*protkeylen < keysize + AES_WK_VP_SIZE) {
		PKEY_DBF_ERR("%s prot key buffer size too small: %u < %d\n",
			     __func__, *protkeylen, keysize + AES_WK_VP_SIZE);
		goto out;
	}

	/* Did we already check for PCKMO ? */
	if (!pckmo_functions.bytes[0]) {
		/* no, so check now */
		if (!cpacf_query(CPACF_PCKMO, &pckmo_functions)) {
			PKEY_DBF_ERR("%s cpacf_query() failed\n", __func__);
			rc = -ENODEV;
			goto out;
		}
	}
	/* check for the pckmo subfunction we need now */
	if (!cpacf_test_func(&pckmo_functions, fc)) {
		PKEY_DBF_ERR("%s pckmo functions not available\n", __func__);
		rc = -ENODEV;
		goto out;
	}

	/* prepare param block */
	memset(paramblock, 0, sizeof(paramblock));
	memcpy(paramblock, clrkey, keysize);

	/* call the pckmo instruction */
	cpacf_pckmo(fc, paramblock);

	/* copy created protected key to key buffer including the wkvp block */
	*protkeylen = keysize + AES_WK_VP_SIZE;
	memcpy(protkey, paramblock, *protkeylen);
	*protkeytype = pkeytype;

	rc = 0;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Verify a raw protected key blob.
 * Currently only AES protected keys are supported.
 */
static int pckmo_verify_protkey(const u8 *protkey, u32 protkeylen,
				u32 protkeytype)
{
	struct {
		u8 iv[AES_BLOCK_SIZE];
		u8 key[MAXPROTKEYSIZE];
	} param;
	u8 null_msg[AES_BLOCK_SIZE];
	u8 dest_buf[AES_BLOCK_SIZE];
	unsigned int k, pkeylen;
	unsigned long fc;
	int rc = -EINVAL;

	switch (protkeytype) {
	case PKEY_KEYTYPE_AES_128:
		pkeylen = 16 + AES_WK_VP_SIZE;
		fc = CPACF_KMC_PAES_128;
		break;
	case PKEY_KEYTYPE_AES_192:
		pkeylen = 24 + AES_WK_VP_SIZE;
		fc = CPACF_KMC_PAES_192;
		break;
	case PKEY_KEYTYPE_AES_256:
		pkeylen = 32 + AES_WK_VP_SIZE;
		fc = CPACF_KMC_PAES_256;
		break;
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %u\n", __func__,
			     protkeytype);
		goto out;
	}
	if (protkeylen != pkeylen) {
		PKEY_DBF_ERR("%s invalid protected key size %u for keytype %u\n",
			     __func__, protkeylen, protkeytype);
		goto out;
	}

	memset(null_msg, 0, sizeof(null_msg));

	memset(param.iv, 0, sizeof(param.iv));
	memcpy(param.key, protkey, protkeylen);

	k = cpacf_kmc(fc | CPACF_ENCRYPT, &param, null_msg, dest_buf,
		      sizeof(null_msg));
	if (k != sizeof(null_msg)) {
		PKEY_DBF_ERR("%s protected key is not valid\n", __func__);
		rc = -EKEYREJECTED;
		goto out;
	}

	rc = 0;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int pckmo_key2protkey(const u8 *key, u32 keylen,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int rc = -EINVAL;

	if (keylen < sizeof(*hdr))
		return -EINVAL;
	if (hdr->type != TOKTYPE_NON_CCA)
		return -EINVAL;

	switch (hdr->version) {
	case TOKVER_PROTECTED_KEY: {
		struct protkeytoken *t = (struct protkeytoken *)key;

		if (keylen < sizeof(*t))
			goto out;
		switch (t->keytype) {
		case PKEY_KEYTYPE_AES_128:
		case PKEY_KEYTYPE_AES_192:
		case PKEY_KEYTYPE_AES_256:
			if (keylen != sizeof(struct protaeskeytoken))
				goto out;
			rc = pckmo_verify_protkey(t->protkey, t->len,
						  t->keytype);
			if (rc)
				goto out;
			break;
		case PKEY_KEYTYPE_AES_XTS_128:
			if (t->len != 64 || keylen != sizeof(*t) + t->len)
				goto out;
			break;
		case PKEY_KEYTYPE_AES_XTS_256:
		case PKEY_KEYTYPE_HMAC_512:
			if (t->len != 96 || keylen != sizeof(*t) + t->len)
				goto out;
			break;
		case PKEY_KEYTYPE_HMAC_1024:
			if (t->len != 160 || keylen != sizeof(*t) + t->len)
				goto out;
			break;
		default:
			PKEY_DBF_ERR("%s protected key token: unknown keytype %u\n",
				     __func__, t->keytype);
			goto out;
		}
		memcpy(protkey, t->protkey, t->len);
		*protkeylen = t->len;
		*protkeytype = t->keytype;
		rc = 0;
		break;
	}
	case TOKVER_CLEAR_KEY: {
		struct clearkeytoken *t = (struct clearkeytoken *)key;
		u32 keysize = 0;

		if (keylen < sizeof(struct clearkeytoken) ||
		    keylen != sizeof(*t) + t->len)
			goto out;
		switch (t->keytype) {
		case PKEY_KEYTYPE_AES_128:
		case PKEY_KEYTYPE_AES_192:
		case PKEY_KEYTYPE_AES_256:
			keysize = pkey_keytype_aes_to_size(t->keytype);
			break;
		case PKEY_KEYTYPE_ECC_P256:
			keysize = 32;
			break;
		case PKEY_KEYTYPE_ECC_P384:
			keysize = 48;
			break;
		case PKEY_KEYTYPE_ECC_P521:
			keysize = 80;
			break;
		case PKEY_KEYTYPE_ECC_ED25519:
			keysize = 32;
			break;
		case PKEY_KEYTYPE_ECC_ED448:
			keysize = 64;
			break;
		case PKEY_KEYTYPE_AES_XTS_128:
			keysize = 32;
			break;
		case PKEY_KEYTYPE_AES_XTS_256:
			keysize = 64;
			break;
		case PKEY_KEYTYPE_HMAC_512:
			keysize = 64;
			break;
		case PKEY_KEYTYPE_HMAC_1024:
			keysize = 128;
			break;
		default:
			break;
		}
		if (!keysize) {
			PKEY_DBF_ERR("%s clear key token: unknown keytype %u\n",
				     __func__, t->keytype);
			goto out;
		}
		if (t->len != keysize) {
			PKEY_DBF_ERR("%s clear key token: invalid key len %u\n",
				     __func__, t->len);
			goto out;
		}
		rc = pckmo_clr2protkey(t->keytype, t->clearkey, t->len,
				       protkey, protkeylen, protkeytype);
		break;
	}
	default:
		PKEY_DBF_ERR("%s unknown non-CCA token version %d\n",
			     __func__, hdr->version);
		break;
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Generate a random protected key.
 * Currently only the generation of AES protected keys
 * is supported.
 */
static int pckmo_gen_protkey(u32 keytype, u32 subtype,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	u8 clrkey[128];
	int keysize;
	int rc;

	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
	case PKEY_KEYTYPE_AES_192:
	case PKEY_KEYTYPE_AES_256:
		keysize = pkey_keytype_aes_to_size(keytype);
		break;
	case PKEY_KEYTYPE_AES_XTS_128:
		keysize = 32;
		break;
	case PKEY_KEYTYPE_AES_XTS_256:
	case PKEY_KEYTYPE_HMAC_512:
		keysize = 64;
		break;
	case PKEY_KEYTYPE_HMAC_1024:
		keysize = 128;
		break;
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n",
			     __func__, keytype);
		return -EINVAL;
	}
	if (subtype != PKEY_TYPE_PROTKEY) {
		PKEY_DBF_ERR("%s unknown/unsupported subtype %d\n",
			     __func__, subtype);
		return -EINVAL;
	}

	/* generate a dummy random clear key */
	get_random_bytes(clrkey, keysize);

	/* convert it to a dummy protected key */
	rc = pckmo_clr2protkey(keytype, clrkey, keysize,
			       protkey, protkeylen, protkeytype);
	if (rc)
		goto out;

	/* replace the key part of the protected key with random bytes */
	get_random_bytes(protkey, keysize);

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Verify a protected key token blob.
 * Currently only AES protected keys are supported.
 */
static int pckmo_verify_key(const u8 *key, u32 keylen)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int rc = -EINVAL;

	if (keylen < sizeof(*hdr))
		return -EINVAL;
	if (hdr->type != TOKTYPE_NON_CCA)
		return -EINVAL;

	switch (hdr->version) {
	case TOKVER_PROTECTED_KEY: {
		struct protaeskeytoken *t;

		if (keylen != sizeof(struct protaeskeytoken))
			goto out;
		t = (struct protaeskeytoken *)key;
		rc = pckmo_verify_protkey(t->protkey, t->len, t->keytype);
		break;
	}
	default:
		PKEY_DBF_ERR("%s unknown non-CCA token version %d\n",
			     __func__, hdr->version);
		break;
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * Wrapper functions used for the pkey handler struct
 */

static int pkey_pckmo_key2protkey(const struct pkey_apqn *_apqns,
				  size_t _nr_apqns,
				  const u8 *key, u32 keylen,
				  u8 *protkey, u32 *protkeylen, u32 *keyinfo)
{
	return pckmo_key2protkey(key, keylen,
				 protkey, protkeylen, keyinfo);
}

static int pkey_pckmo_gen_key(const struct pkey_apqn *_apqns, size_t _nr_apqns,
			      u32 keytype, u32 keysubtype,
			      u32 _keybitsize, u32 _flags,
			      u8 *keybuf, u32 *keybuflen, u32 *keyinfo)
{
	return pckmo_gen_protkey(keytype, keysubtype,
				 keybuf, keybuflen, keyinfo);
}

static int pkey_pckmo_verifykey(const u8 *key, u32 keylen,
				u16 *_card, u16 *_dom,
				u32 *_keytype, u32 *_keybitsize, u32 *_flags)
{
	return pckmo_verify_key(key, keylen);
}

static struct pkey_handler pckmo_handler = {
	.module		      = THIS_MODULE,
	.name		      = "PKEY PCKMO handler",
	.is_supported_key     = is_pckmo_key,
	.is_supported_keytype = is_pckmo_keytype,
	.key_to_protkey	      = pkey_pckmo_key2protkey,
	.gen_key	      = pkey_pckmo_gen_key,
	.verify_key	      = pkey_pckmo_verifykey,
};

/*
 * Module init
 */
static int __init pkey_pckmo_init(void)
{
	cpacf_mask_t func_mask;

	/*
	 * The pckmo instruction should be available - even if we don't
	 * actually invoke it. This instruction comes with MSA 3 which
	 * is also the minimum level for the kmc instructions which
	 * are able to work with protected keys.
	 */
	if (!cpacf_query(CPACF_PCKMO, &func_mask))
		return -ENODEV;

	/* register this module as pkey handler for all the pckmo stuff */
	return pkey_handler_register(&pckmo_handler);
}

/*
 * Module exit
 */
static void __exit pkey_pckmo_exit(void)
{
	/* unregister this module as pkey handler */
	pkey_handler_unregister(&pckmo_handler);
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, pkey_pckmo_init);
module_exit(pkey_pckmo_exit);
