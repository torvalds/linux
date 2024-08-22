// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey pckmo specific code
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <asm/cpacf.h>
#include <crypto/aes.h>
#include <linux/random.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"

#include "pkey_base.h"

/*
 * Check key blob for known and supported here.
 */
bool pkey_is_pckmo_key(const u8 *key, u32 keylen)
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

int pkey_pckmo_key2protkey(const u8 *key, u32 keylen,
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
		struct protaeskeytoken *t;

		if (keylen != sizeof(struct protaeskeytoken))
			goto out;
		t = (struct protaeskeytoken *)key;
		rc = pkey_pckmo_verify_protkey(t->protkey, t->len,
					       t->keytype);
		if (rc)
			goto out;
		memcpy(protkey, t->protkey, t->len);
		*protkeylen = t->len;
		*protkeytype = t->keytype;
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
		rc = pkey_pckmo_clr2protkey(t->keytype, t->clearkey,
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
int pkey_pckmo_gen_protkey(u32 keytype, u8 *protkey,
			   u32 *protkeylen, u32 *protkeytype)
{
	u8 clrkey[32];
	int keysize;
	int rc;

	keysize = pkey_keytype_aes_to_size(keytype);
	if (!keysize) {
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n", __func__,
			     keytype);
		return -EINVAL;
	}

	/* generate a dummy random clear key */
	get_random_bytes(clrkey, keysize);

	/* convert it to a dummy protected key */
	rc = pkey_pckmo_clr2protkey(keytype, clrkey,
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
 * Create a protected key from a clear key value via PCKMO instruction.
 */
int pkey_pckmo_clr2protkey(u32 keytype, const u8 *clrkey,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	/* mask of available pckmo subfunctions */
	static cpacf_mask_t pckmo_functions;

	int keysize, rc = -EINVAL;
	u8 paramblock[112];
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
	default:
		PKEY_DBF_ERR("%s unknown/unsupported keytype %u\n",
			     __func__, keytype);
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
 * Verify a protected key blob.
 * Currently only AES protected keys are supported.
 */
int pkey_pckmo_verify_protkey(const u8 *protkey, u32 protkeylen,
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
