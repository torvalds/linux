// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey device driver
 *
 *  Copyright IBM Corp. 2017, 2023
 *
 *  Author(s): Harald Freudenberger
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/cpufeature.h>
#include <asm/zcrypt.h>
#include <asm/cpacf.h>
#include <asm/pkey.h>
#include <crypto/aes.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key interface");

#define KEYBLOBBUFSIZE 8192	/* key buffer size used for internal processing */
#define MINKEYBLOBBUFSIZE (sizeof(struct keytoken_header))
#define PROTKEYBLOBBUFSIZE 256	/* protected key buffer size used internal */
#define MAXAPQNSINLIST 64	/* max 64 apqns within a apqn list */
#define AES_WK_VP_SIZE 32	/* Size of WK VP block appended to a prot key */

/*
 * debug feature data and functions
 */

static debug_info_t *debug_info;

#define DEBUG_DBG(...)	debug_sprintf_event(debug_info, 6, ##__VA_ARGS__)
#define DEBUG_INFO(...) debug_sprintf_event(debug_info, 5, ##__VA_ARGS__)
#define DEBUG_WARN(...) debug_sprintf_event(debug_info, 4, ##__VA_ARGS__)
#define DEBUG_ERR(...)	debug_sprintf_event(debug_info, 3, ##__VA_ARGS__)

static void __init pkey_debug_init(void)
{
	/* 5 arguments per dbf entry (including the format string ptr) */
	debug_info = debug_register("pkey", 1, 1, 5 * sizeof(long));
	debug_register_view(debug_info, &debug_sprintf_view);
	debug_set_level(debug_info, 3);
}

static void __exit pkey_debug_exit(void)
{
	debug_unregister(debug_info);
}

/* inside view of a protected key token (only type 0x00 version 0x01) */
struct protaeskeytoken {
	u8  type;     /* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;  /* should be 0x01 for protected AES key token */
	u8  res1[3];
	u32 keytype;  /* key type, one of the PKEY_KEYTYPE values */
	u32 len;      /* bytes actually stored in protkey[] */
	u8  protkey[MAXPROTKEYSIZE]; /* the protected key blob */
} __packed;

/* inside view of a clear key token (type 0x00 version 0x02) */
struct clearkeytoken {
	u8  type;	/* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;	/* 0x02 for clear key token */
	u8  res1[3];
	u32 keytype;	/* key type, one of the PKEY_KEYTYPE_* values */
	u32 len;	/* bytes actually stored in clearkey[] */
	u8  clearkey[]; /* clear key value */
} __packed;

/* helper function which translates the PKEY_KEYTYPE_AES_* to their keysize */
static inline u32 pkey_keytype_aes_to_size(u32 keytype)
{
	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		return 16;
	case PKEY_KEYTYPE_AES_192:
		return 24;
	case PKEY_KEYTYPE_AES_256:
		return 32;
	default:
		return 0;
	}
}

/*
 * Create a protected key from a clear key value via PCKMO instruction.
 */
static int pkey_clr2protkey(u32 keytype, const u8 *clrkey,
			    u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	/* mask of available pckmo subfunctions */
	static cpacf_mask_t pckmo_functions;

	u8 paramblock[112];
	u32 pkeytype;
	int keysize;
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
		DEBUG_ERR("%s unknown/unsupported keytype %u\n",
			  __func__, keytype);
		return -EINVAL;
	}

	if (*protkeylen < keysize + AES_WK_VP_SIZE) {
		DEBUG_ERR("%s prot key buffer size too small: %u < %d\n",
			  __func__, *protkeylen, keysize + AES_WK_VP_SIZE);
		return -EINVAL;
	}

	/* Did we already check for PCKMO ? */
	if (!pckmo_functions.bytes[0]) {
		/* no, so check now */
		if (!cpacf_query(CPACF_PCKMO, &pckmo_functions))
			return -ENODEV;
	}
	/* check for the pckmo subfunction we need now */
	if (!cpacf_test_func(&pckmo_functions, fc)) {
		DEBUG_ERR("%s pckmo functions not available\n", __func__);
		return -ENODEV;
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

	return 0;
}

/*
 * Find card and transform secure key into protected key.
 */
static int pkey_skey2pkey(const u8 *key, u8 *protkey,
			  u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u16 cardnr, domain;
	int rc, verify;

	zcrypt_wait_api_operational();

	/*
	 * The cca_xxx2protkey call may fail when a card has been
	 * addressed where the master key was changed after last fetch
	 * of the mkvp into the cache. Try 3 times: First without verify
	 * then with verify and last round with verify and old master
	 * key verification pattern match not ignored.
	 */
	for (verify = 0; verify < 3; verify++) {
		rc = cca_findcard(key, &cardnr, &domain, verify);
		if (rc < 0)
			continue;
		if (rc > 0 && verify < 2)
			continue;
		switch (hdr->version) {
		case TOKVER_CCA_AES:
			rc = cca_sec2protkey(cardnr, domain, key,
					     protkey, protkeylen, protkeytype);
			break;
		case TOKVER_CCA_VLSC:
			rc = cca_cipher2protkey(cardnr, domain, key,
						protkey, protkeylen,
						protkeytype);
			break;
		default:
			return -EINVAL;
		}
		if (rc == 0)
			break;
	}

	if (rc)
		DEBUG_DBG("%s failed rc=%d\n", __func__, rc);

	return rc;
}

/*
 * Construct EP11 key with given clear key value.
 */
static int pkey_clr2ep11key(const u8 *clrkey, size_t clrkeylen,
			    u8 *keybuf, size_t *keybuflen)
{
	u32 nr_apqns, *apqns = NULL;
	u16 card, dom;
	int i, rc;

	zcrypt_wait_api_operational();

	/* build a list of apqns suitable for ep11 keys with cpacf support */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7,
			    ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4,
			    NULL);
	if (rc)
		goto out;

	/* go through the list of apqns and try to bild an ep11 key */
	for (rc = -ENODEV, i = 0; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_clr2keyblob(card, dom, clrkeylen * 8,
				      0, clrkey, keybuf, keybuflen,
				      PKEY_TYPE_EP11);
		if (rc == 0)
			break;
	}

out:
	kfree(apqns);
	if (rc)
		DEBUG_DBG("%s failed rc=%d\n", __func__, rc);
	return rc;
}

/*
 * Find card and transform EP11 secure key into protected key.
 */
static int pkey_ep11key2pkey(const u8 *key, size_t keylen,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	u32 nr_apqns, *apqns = NULL;
	u16 card, dom;
	int i, rc;

	zcrypt_wait_api_operational();

	/* build a list of apqns suitable for this key */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7,
			    ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4,
			    ep11_kb_wkvp(key, keylen));
	if (rc)
		goto out;

	/* go through the list of apqns and try to derive an pkey */
	for (rc = -ENODEV, i = 0; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_kblob2protkey(card, dom, key, keylen,
					protkey, protkeylen, protkeytype);
		if (rc == 0)
			break;
	}

out:
	kfree(apqns);
	if (rc)
		DEBUG_DBG("%s failed rc=%d\n", __func__, rc);
	return rc;
}

/*
 * Verify key and give back some info about the key.
 */
static int pkey_verifykey(const struct pkey_seckey *seckey,
			  u16 *pcardnr, u16 *pdomain,
			  u16 *pkeysize, u32 *pattributes)
{
	struct secaeskeytoken *t = (struct secaeskeytoken *)seckey;
	u16 cardnr, domain;
	int rc;

	/* check the secure key for valid AES secure key */
	rc = cca_check_secaeskeytoken(debug_info, 3, (u8 *)seckey, 0);
	if (rc)
		goto out;
	if (pattributes)
		*pattributes = PKEY_VERIFY_ATTR_AES;
	if (pkeysize)
		*pkeysize = t->bitsize;

	/* try to find a card which can handle this key */
	rc = cca_findcard(seckey->seckey, &cardnr, &domain, 1);
	if (rc < 0)
		goto out;

	if (rc > 0) {
		/* key mkvp matches to old master key mkvp */
		DEBUG_DBG("%s secure key has old mkvp\n", __func__);
		if (pattributes)
			*pattributes |= PKEY_VERIFY_ATTR_OLD_MKVP;
		rc = 0;
	}

	if (pcardnr)
		*pcardnr = cardnr;
	if (pdomain)
		*pdomain = domain;

out:
	DEBUG_DBG("%s rc=%d\n", __func__, rc);
	return rc;
}

/*
 * Generate a random protected key
 */
static int pkey_genprotkey(u32 keytype, u8 *protkey,
			   u32 *protkeylen, u32 *protkeytype)
{
	u8 clrkey[32];
	int keysize;
	int rc;

	keysize = pkey_keytype_aes_to_size(keytype);
	if (!keysize) {
		DEBUG_ERR("%s unknown/unsupported keytype %d\n", __func__,
			  keytype);
		return -EINVAL;
	}

	/* generate a dummy random clear key */
	get_random_bytes(clrkey, keysize);

	/* convert it to a dummy protected key */
	rc = pkey_clr2protkey(keytype, clrkey,
			      protkey, protkeylen, protkeytype);
	if (rc)
		return rc;

	/* replace the key part of the protected key with random bytes */
	get_random_bytes(protkey, keysize);

	return 0;
}

/*
 * Verify if a protected key is still valid
 */
static int pkey_verifyprotkey(const u8 *protkey, u32 protkeylen,
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
		DEBUG_ERR("%s unknown/unsupported keytype %u\n", __func__,
			  protkeytype);
		return -EINVAL;
	}
	if (protkeylen != pkeylen) {
		DEBUG_ERR("%s invalid protected key size %u for keytype %u\n",
			  __func__, protkeylen, protkeytype);
		return -EINVAL;
	}

	memset(null_msg, 0, sizeof(null_msg));

	memset(param.iv, 0, sizeof(param.iv));
	memcpy(param.key, protkey, protkeylen);

	k = cpacf_kmc(fc | CPACF_ENCRYPT, &param, null_msg, dest_buf,
		      sizeof(null_msg));
	if (k != sizeof(null_msg)) {
		DEBUG_ERR("%s protected key is not valid\n", __func__);
		return -EKEYREJECTED;
	}

	return 0;
}

/* Helper for pkey_nonccatok2pkey, handles aes clear key token */
static int nonccatokaes2pkey(const struct clearkeytoken *t,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	size_t tmpbuflen = max_t(size_t, SECKEYBLOBSIZE, MAXEP11AESKEYBLOBSIZE);
	u8 *tmpbuf = NULL;
	u32 keysize;
	int rc;

	keysize = pkey_keytype_aes_to_size(t->keytype);
	if (!keysize) {
		DEBUG_ERR("%s unknown/unsupported keytype %u\n",
			  __func__, t->keytype);
		return -EINVAL;
	}
	if (t->len != keysize) {
		DEBUG_ERR("%s non clear key aes token: invalid key len %u\n",
			  __func__, t->len);
		return -EINVAL;
	}

	/* try direct way with the PCKMO instruction */
	rc = pkey_clr2protkey(t->keytype, t->clearkey,
			      protkey, protkeylen, protkeytype);
	if (!rc)
		goto out;

	/* PCKMO failed, so try the CCA secure key way */
	tmpbuf = kmalloc(tmpbuflen, GFP_ATOMIC);
	if (!tmpbuf)
		return -ENOMEM;
	zcrypt_wait_api_operational();
	rc = cca_clr2seckey(0xFFFF, 0xFFFF, t->keytype, t->clearkey, tmpbuf);
	if (rc)
		goto try_via_ep11;
	rc = pkey_skey2pkey(tmpbuf,
			    protkey, protkeylen, protkeytype);
	if (!rc)
		goto out;

try_via_ep11:
	/* if the CCA way also failed, let's try via EP11 */
	rc = pkey_clr2ep11key(t->clearkey, t->len,
			      tmpbuf, &tmpbuflen);
	if (rc)
		goto failure;
	rc = pkey_ep11key2pkey(tmpbuf, tmpbuflen,
			       protkey, protkeylen, protkeytype);
	if (!rc)
		goto out;

failure:
	DEBUG_ERR("%s unable to build protected key from clear", __func__);

out:
	kfree(tmpbuf);
	return rc;
}

/* Helper for pkey_nonccatok2pkey, handles ecc clear key token */
static int nonccatokecc2pkey(const struct clearkeytoken *t,
			     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	u32 keylen;
	int rc;

	switch (t->keytype) {
	case PKEY_KEYTYPE_ECC_P256:
		keylen = 32;
		break;
	case PKEY_KEYTYPE_ECC_P384:
		keylen = 48;
		break;
	case PKEY_KEYTYPE_ECC_P521:
		keylen = 80;
		break;
	case PKEY_KEYTYPE_ECC_ED25519:
		keylen = 32;
		break;
	case PKEY_KEYTYPE_ECC_ED448:
		keylen = 64;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %u\n",
			  __func__, t->keytype);
		return -EINVAL;
	}

	if (t->len != keylen) {
		DEBUG_ERR("%s non clear key ecc token: invalid key len %u\n",
			  __func__, t->len);
		return -EINVAL;
	}

	/* only one path possible: via PCKMO instruction */
	rc = pkey_clr2protkey(t->keytype, t->clearkey,
			      protkey, protkeylen, protkeytype);
	if (rc) {
		DEBUG_ERR("%s unable to build protected key from clear",
			  __func__);
	}

	return rc;
}

/*
 * Transform a non-CCA key token into a protected key
 */
static int pkey_nonccatok2pkey(const u8 *key, u32 keylen,
			       u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int rc = -EINVAL;

	switch (hdr->version) {
	case TOKVER_PROTECTED_KEY: {
		struct protaeskeytoken *t;

		if (keylen != sizeof(struct protaeskeytoken))
			goto out;
		t = (struct protaeskeytoken *)key;
		rc = pkey_verifyprotkey(t->protkey, t->len, t->keytype);
		if (rc)
			goto out;
		memcpy(protkey, t->protkey, t->len);
		*protkeylen = t->len;
		*protkeytype = t->keytype;
		break;
	}
	case TOKVER_CLEAR_KEY: {
		struct clearkeytoken *t = (struct clearkeytoken *)key;

		if (keylen < sizeof(struct clearkeytoken) ||
		    keylen != sizeof(*t) + t->len)
			goto out;
		switch (t->keytype) {
		case PKEY_KEYTYPE_AES_128:
		case PKEY_KEYTYPE_AES_192:
		case PKEY_KEYTYPE_AES_256:
			rc = nonccatokaes2pkey(t, protkey,
					       protkeylen, protkeytype);
			break;
		case PKEY_KEYTYPE_ECC_P256:
		case PKEY_KEYTYPE_ECC_P384:
		case PKEY_KEYTYPE_ECC_P521:
		case PKEY_KEYTYPE_ECC_ED25519:
		case PKEY_KEYTYPE_ECC_ED448:
			rc = nonccatokecc2pkey(t, protkey,
					       protkeylen, protkeytype);
			break;
		default:
			DEBUG_ERR("%s unknown/unsupported non cca clear key type %u\n",
				  __func__, t->keytype);
			return -EINVAL;
		}
		break;
	}
	case TOKVER_EP11_AES: {
		/* check ep11 key for exportable as protected key */
		rc = ep11_check_aes_key(debug_info, 3, key, keylen, 1);
		if (rc)
			goto out;
		rc = pkey_ep11key2pkey(key, keylen,
				       protkey, protkeylen, protkeytype);
		break;
	}
	case TOKVER_EP11_AES_WITH_HEADER:
		/* check ep11 key with header for exportable as protected key */
		rc = ep11_check_aes_key_with_hdr(debug_info, 3, key, keylen, 1);
		if (rc)
			goto out;
		rc = pkey_ep11key2pkey(key, keylen,
				       protkey, protkeylen, protkeytype);
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported non-CCA token version %d\n",
			  __func__, hdr->version);
	}

out:
	return rc;
}

/*
 * Transform a CCA internal key token into a protected key
 */
static int pkey_ccainttok2pkey(const u8 *key, u32 keylen,
			       u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	switch (hdr->version) {
	case TOKVER_CCA_AES:
		if (keylen != sizeof(struct secaeskeytoken))
			return -EINVAL;
		break;
	case TOKVER_CCA_VLSC:
		if (keylen < hdr->len || keylen > MAXCCAVLSCTOKENSIZE)
			return -EINVAL;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported CCA internal token version %d\n",
			  __func__, hdr->version);
		return -EINVAL;
	}

	return pkey_skey2pkey(key, protkey, protkeylen, protkeytype);
}

/*
 * Transform a key blob (of any type) into a protected key
 */
int pkey_keyblob2pkey(const u8 *key, u32 keylen,
		      u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int rc;

	if (keylen < sizeof(struct keytoken_header)) {
		DEBUG_ERR("%s invalid keylen %d\n", __func__, keylen);
		return -EINVAL;
	}

	switch (hdr->type) {
	case TOKTYPE_NON_CCA:
		rc = pkey_nonccatok2pkey(key, keylen,
					 protkey, protkeylen, protkeytype);
		break;
	case TOKTYPE_CCA_INTERNAL:
		rc = pkey_ccainttok2pkey(key, keylen,
					 protkey, protkeylen, protkeytype);
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported blob type %d\n",
			  __func__, hdr->type);
		return -EINVAL;
	}

	DEBUG_DBG("%s rc=%d\n", __func__, rc);
	return rc;
}
EXPORT_SYMBOL(pkey_keyblob2pkey);

static int pkey_genseckey2(const struct pkey_apqn *apqns, size_t nr_apqns,
			   enum pkey_key_type ktype, enum pkey_key_size ksize,
			   u32 kflags, u8 *keybuf, size_t *keybufsize)
{
	int i, card, dom, rc;

	/* check for at least one apqn given */
	if (!apqns || !nr_apqns)
		return -EINVAL;

	/* check key type and size */
	switch (ktype) {
	case PKEY_TYPE_CCA_DATA:
	case PKEY_TYPE_CCA_CIPHER:
		if (*keybufsize < SECKEYBLOBSIZE)
			return -EINVAL;
		break;
	case PKEY_TYPE_EP11:
		if (*keybufsize < MINEP11AESKEYBLOBSIZE)
			return -EINVAL;
		break;
	case PKEY_TYPE_EP11_AES:
		if (*keybufsize < (sizeof(struct ep11kblob_header) +
				   MINEP11AESKEYBLOBSIZE))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	switch (ksize) {
	case PKEY_SIZE_AES_128:
	case PKEY_SIZE_AES_192:
	case PKEY_SIZE_AES_256:
		break;
	default:
		return -EINVAL;
	}

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i].card;
		dom = apqns[i].domain;
		if (ktype == PKEY_TYPE_EP11 ||
		    ktype == PKEY_TYPE_EP11_AES) {
			rc = ep11_genaeskey(card, dom, ksize, kflags,
					    keybuf, keybufsize, ktype);
		} else if (ktype == PKEY_TYPE_CCA_DATA) {
			rc = cca_genseckey(card, dom, ksize, keybuf);
			*keybufsize = (rc ? 0 : SECKEYBLOBSIZE);
		} else {
			/* TOKVER_CCA_VLSC */
			rc = cca_gencipherkey(card, dom, ksize, kflags,
					      keybuf, keybufsize);
		}
		if (rc == 0)
			break;
	}

	return rc;
}

static int pkey_clr2seckey2(const struct pkey_apqn *apqns, size_t nr_apqns,
			    enum pkey_key_type ktype, enum pkey_key_size ksize,
			    u32 kflags, const u8 *clrkey,
			    u8 *keybuf, size_t *keybufsize)
{
	int i, card, dom, rc;

	/* check for at least one apqn given */
	if (!apqns || !nr_apqns)
		return -EINVAL;

	/* check key type and size */
	switch (ktype) {
	case PKEY_TYPE_CCA_DATA:
	case PKEY_TYPE_CCA_CIPHER:
		if (*keybufsize < SECKEYBLOBSIZE)
			return -EINVAL;
		break;
	case PKEY_TYPE_EP11:
		if (*keybufsize < MINEP11AESKEYBLOBSIZE)
			return -EINVAL;
		break;
	case PKEY_TYPE_EP11_AES:
		if (*keybufsize < (sizeof(struct ep11kblob_header) +
				   MINEP11AESKEYBLOBSIZE))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	switch (ksize) {
	case PKEY_SIZE_AES_128:
	case PKEY_SIZE_AES_192:
	case PKEY_SIZE_AES_256:
		break;
	default:
		return -EINVAL;
	}

	zcrypt_wait_api_operational();

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i].card;
		dom = apqns[i].domain;
		if (ktype == PKEY_TYPE_EP11 ||
		    ktype == PKEY_TYPE_EP11_AES) {
			rc = ep11_clr2keyblob(card, dom, ksize, kflags,
					      clrkey, keybuf, keybufsize,
					      ktype);
		} else if (ktype == PKEY_TYPE_CCA_DATA) {
			rc = cca_clr2seckey(card, dom, ksize,
					    clrkey, keybuf);
			*keybufsize = (rc ? 0 : SECKEYBLOBSIZE);
		} else {
			/* TOKVER_CCA_VLSC */
			rc = cca_clr2cipherkey(card, dom, ksize, kflags,
					       clrkey, keybuf, keybufsize);
		}
		if (rc == 0)
			break;
	}

	return rc;
}

static int pkey_verifykey2(const u8 *key, size_t keylen,
			   u16 *cardnr, u16 *domain,
			   enum pkey_key_type *ktype,
			   enum pkey_key_size *ksize, u32 *flags)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

	if (keylen < sizeof(struct keytoken_header))
		return -EINVAL;

	if (hdr->type == TOKTYPE_CCA_INTERNAL &&
	    hdr->version == TOKVER_CCA_AES) {
		struct secaeskeytoken *t = (struct secaeskeytoken *)key;

		rc = cca_check_secaeskeytoken(debug_info, 3, key, 0);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_CCA_DATA;
		if (ksize)
			*ksize = (enum pkey_key_size)t->bitsize;

		rc = cca_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				   ZCRYPT_CEX3C, AES_MK_SET, t->mkvp, 0, 1);
		if (rc == 0 && flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&_apqns, &_nr_apqns,
					   *cardnr, *domain,
					   ZCRYPT_CEX3C, AES_MK_SET,
					   0, t->mkvp, 1);
			if (rc == 0 && flags)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
		   hdr->version == TOKVER_CCA_VLSC) {
		struct cipherkeytoken *t = (struct cipherkeytoken *)key;

		rc = cca_check_secaescipherkey(debug_info, 3, key, 0, 1);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_CCA_CIPHER;
		if (ksize) {
			*ksize = PKEY_SIZE_UNKNOWN;
			if (!t->plfver && t->wpllen == 512)
				*ksize = PKEY_SIZE_AES_128;
			else if (!t->plfver && t->wpllen == 576)
				*ksize = PKEY_SIZE_AES_192;
			else if (!t->plfver && t->wpllen == 640)
				*ksize = PKEY_SIZE_AES_256;
		}

		rc = cca_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				   ZCRYPT_CEX6, AES_MK_SET, t->mkvp0, 0, 1);
		if (rc == 0 && flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&_apqns, &_nr_apqns,
					   *cardnr, *domain,
					   ZCRYPT_CEX6, AES_MK_SET,
					   0, t->mkvp0, 1);
			if (rc == 0 && flags)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES) {
		struct ep11keyblob *kb = (struct ep11keyblob *)key;
		int api;

		rc = ep11_check_aes_key(debug_info, 3, key, keylen, 1);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_EP11;
		if (ksize)
			*ksize = kb->head.bitlen;

		api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		rc = ep11_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				    ZCRYPT_CEX7, api,
				    ep11_kb_wkvp(key, keylen));
		if (rc)
			goto out;

		if (flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES_WITH_HEADER) {
		struct ep11kblob_header *kh = (struct ep11kblob_header *)key;
		int api;

		rc = ep11_check_aes_key_with_hdr(debug_info, 3,
						 key, keylen, 1);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_EP11_AES;
		if (ksize)
			*ksize = kh->bitlen;

		api = ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4;
		rc = ep11_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				    ZCRYPT_CEX7, api,
				    ep11_kb_wkvp(key, keylen));
		if (rc)
			goto out;

		if (flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;
	} else {
		rc = -EINVAL;
	}

out:
	kfree(_apqns);
	return rc;
}

static int pkey_keyblob2pkey2(const struct pkey_apqn *apqns, size_t nr_apqns,
			      const u8 *key, size_t keylen,
			      u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int i, card, dom, rc;

	/* check for at least one apqn given */
	if (!apqns || !nr_apqns)
		return -EINVAL;

	if (keylen < sizeof(struct keytoken_header))
		return -EINVAL;

	if (hdr->type == TOKTYPE_CCA_INTERNAL) {
		if (hdr->version == TOKVER_CCA_AES) {
			if (keylen != sizeof(struct secaeskeytoken))
				return -EINVAL;
			if (cca_check_secaeskeytoken(debug_info, 3, key, 0))
				return -EINVAL;
		} else if (hdr->version == TOKVER_CCA_VLSC) {
			if (keylen < hdr->len || keylen > MAXCCAVLSCTOKENSIZE)
				return -EINVAL;
			if (cca_check_secaescipherkey(debug_info, 3, key, 0, 1))
				return -EINVAL;
		} else {
			DEBUG_ERR("%s unknown CCA internal token version %d\n",
				  __func__, hdr->version);
			return -EINVAL;
		}
	} else if (hdr->type == TOKTYPE_NON_CCA) {
		if (hdr->version == TOKVER_EP11_AES) {
			if (ep11_check_aes_key(debug_info, 3, key, keylen, 1))
				return -EINVAL;
		} else if (hdr->version == TOKVER_EP11_AES_WITH_HEADER) {
			if (ep11_check_aes_key_with_hdr(debug_info, 3,
							key, keylen, 1))
				return -EINVAL;
		} else {
			return pkey_nonccatok2pkey(key, keylen,
						   protkey, protkeylen,
						   protkeytype);
		}
	} else {
		DEBUG_ERR("%s unknown/unsupported blob type %d\n",
			  __func__, hdr->type);
		return -EINVAL;
	}

	zcrypt_wait_api_operational();

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i].card;
		dom = apqns[i].domain;
		if (hdr->type == TOKTYPE_CCA_INTERNAL &&
		    hdr->version == TOKVER_CCA_AES) {
			rc = cca_sec2protkey(card, dom, key,
					     protkey, protkeylen, protkeytype);
		} else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
			   hdr->version == TOKVER_CCA_VLSC) {
			rc = cca_cipher2protkey(card, dom, key,
						protkey, protkeylen,
						protkeytype);
		} else {
			rc = ep11_kblob2protkey(card, dom, key, keylen,
						protkey, protkeylen,
						protkeytype);
		}
		if (rc == 0)
			break;
	}

	return rc;
}

static int pkey_apqns4key(const u8 *key, size_t keylen, u32 flags,
			  struct pkey_apqn *apqns, size_t *nr_apqns)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	u32 _nr_apqns, *_apqns = NULL;
	int rc;

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
	} else if (hdr->type == TOKTYPE_CCA_INTERNAL) {
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
			/* unknown cca internal token type */
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
			/* unknown cca internal 2 token type */
			return -EINVAL;
		}
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   ZCRYPT_CEX7, APKA_MK_SET,
				   cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;
	} else {
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
	return rc;
}

static int pkey_apqns4keytype(enum pkey_key_type ktype,
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

	} else if (ktype == PKEY_TYPE_EP11 ||
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
	return rc;
}

static int pkey_keyblob2pkey3(const struct pkey_apqn *apqns, size_t nr_apqns,
			      const u8 *key, size_t keylen,
			      u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct keytoken_header *hdr = (struct keytoken_header *)key;
	int i, card, dom, rc;

	/* check for at least one apqn given */
	if (!apqns || !nr_apqns)
		return -EINVAL;

	if (keylen < sizeof(struct keytoken_header))
		return -EINVAL;

	if (hdr->type == TOKTYPE_NON_CCA &&
	    hdr->version == TOKVER_EP11_AES_WITH_HEADER &&
	    is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 AES key blob with header */
		if (ep11_check_aes_key_with_hdr(debug_info, 3, key, keylen, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_ECC_WITH_HEADER &&
		   is_ep11_keyblob(key + sizeof(struct ep11kblob_header))) {
		/* EP11 ECC key blob with header */
		if (ep11_check_ecc_key_with_hdr(debug_info, 3, key, keylen, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_NON_CCA &&
		   hdr->version == TOKVER_EP11_AES &&
		   is_ep11_keyblob(key)) {
		/* EP11 AES key blob with header in session field */
		if (ep11_check_aes_key(debug_info, 3, key, keylen, 1))
			return -EINVAL;
	} else	if (hdr->type == TOKTYPE_CCA_INTERNAL) {
		if (hdr->version == TOKVER_CCA_AES) {
			/* CCA AES data key */
			if (keylen != sizeof(struct secaeskeytoken))
				return -EINVAL;
			if (cca_check_secaeskeytoken(debug_info, 3, key, 0))
				return -EINVAL;
		} else if (hdr->version == TOKVER_CCA_VLSC) {
			/* CCA AES cipher key */
			if (keylen < hdr->len || keylen > MAXCCAVLSCTOKENSIZE)
				return -EINVAL;
			if (cca_check_secaescipherkey(debug_info, 3, key, 0, 1))
				return -EINVAL;
		} else {
			DEBUG_ERR("%s unknown CCA internal token version %d\n",
				  __func__, hdr->version);
			return -EINVAL;
		}
	} else if (hdr->type == TOKTYPE_CCA_INTERNAL_PKA) {
		/* CCA ECC (private) key */
		if (keylen < sizeof(struct eccprivkeytoken))
			return -EINVAL;
		if (cca_check_sececckeytoken(debug_info, 3, key, keylen, 1))
			return -EINVAL;
	} else if (hdr->type == TOKTYPE_NON_CCA) {
		return pkey_nonccatok2pkey(key, keylen,
					   protkey, protkeylen, protkeytype);
	} else {
		DEBUG_ERR("%s unknown/unsupported blob type %d\n",
			  __func__, hdr->type);
		return -EINVAL;
	}

	/* simple try all apqns from the list */
	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		card = apqns[i].card;
		dom = apqns[i].domain;
		if (hdr->type == TOKTYPE_NON_CCA &&
		    (hdr->version == TOKVER_EP11_AES_WITH_HEADER ||
		     hdr->version == TOKVER_EP11_ECC_WITH_HEADER) &&
		    is_ep11_keyblob(key + sizeof(struct ep11kblob_header)))
			rc = ep11_kblob2protkey(card, dom, key, hdr->len,
						protkey, protkeylen,
						protkeytype);
		else if (hdr->type == TOKTYPE_NON_CCA &&
			 hdr->version == TOKVER_EP11_AES &&
			 is_ep11_keyblob(key))
			rc = ep11_kblob2protkey(card, dom, key, hdr->len,
						protkey, protkeylen,
						protkeytype);
		else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
			 hdr->version == TOKVER_CCA_AES)
			rc = cca_sec2protkey(card, dom, key, protkey,
					     protkeylen, protkeytype);
		else if (hdr->type == TOKTYPE_CCA_INTERNAL &&
			 hdr->version == TOKVER_CCA_VLSC)
			rc = cca_cipher2protkey(card, dom, key, protkey,
						protkeylen, protkeytype);
		else if (hdr->type == TOKTYPE_CCA_INTERNAL_PKA)
			rc = cca_ecc2protkey(card, dom, key, protkey,
					     protkeylen, protkeytype);
		else
			return -EINVAL;
	}

	return rc;
}

/*
 * File io functions
 */

static void *_copy_key_from_user(void __user *ukey, size_t keylen)
{
	if (!ukey || keylen < MINKEYBLOBBUFSIZE || keylen > KEYBLOBBUFSIZE)
		return ERR_PTR(-EINVAL);

	return memdup_user(ukey, keylen);
}

static void *_copy_apqns_from_user(void __user *uapqns, size_t nr_apqns)
{
	if (!uapqns || nr_apqns == 0)
		return NULL;

	return memdup_user(uapqns, nr_apqns * sizeof(struct pkey_apqn));
}

static long pkey_unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	int rc;

	switch (cmd) {
	case PKEY_GENSECK: {
		struct pkey_genseck __user *ugs = (void __user *)arg;
		struct pkey_genseck kgs;

		if (copy_from_user(&kgs, ugs, sizeof(kgs)))
			return -EFAULT;
		rc = cca_genseckey(kgs.cardnr, kgs.domain,
				   kgs.keytype, kgs.seckey.seckey);
		DEBUG_DBG("%s cca_genseckey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ugs, &kgs, sizeof(kgs)))
			return -EFAULT;
		break;
	}
	case PKEY_CLR2SECK: {
		struct pkey_clr2seck __user *ucs = (void __user *)arg;
		struct pkey_clr2seck kcs;

		if (copy_from_user(&kcs, ucs, sizeof(kcs)))
			return -EFAULT;
		rc = cca_clr2seckey(kcs.cardnr, kcs.domain, kcs.keytype,
				    kcs.clrkey.clrkey, kcs.seckey.seckey);
		DEBUG_DBG("%s cca_clr2seckey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ucs, &kcs, sizeof(kcs)))
			return -EFAULT;
		memzero_explicit(&kcs, sizeof(kcs));
		break;
	}
	case PKEY_SEC2PROTK: {
		struct pkey_sec2protk __user *usp = (void __user *)arg;
		struct pkey_sec2protk ksp;

		if (copy_from_user(&ksp, usp, sizeof(ksp)))
			return -EFAULT;
		ksp.protkey.len = sizeof(ksp.protkey.protkey);
		rc = cca_sec2protkey(ksp.cardnr, ksp.domain,
				     ksp.seckey.seckey, ksp.protkey.protkey,
				     &ksp.protkey.len, &ksp.protkey.type);
		DEBUG_DBG("%s cca_sec2protkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(usp, &ksp, sizeof(ksp)))
			return -EFAULT;
		break;
	}
	case PKEY_CLR2PROTK: {
		struct pkey_clr2protk __user *ucp = (void __user *)arg;
		struct pkey_clr2protk kcp;

		if (copy_from_user(&kcp, ucp, sizeof(kcp)))
			return -EFAULT;
		kcp.protkey.len = sizeof(kcp.protkey.protkey);
		rc = pkey_clr2protkey(kcp.keytype, kcp.clrkey.clrkey,
				      kcp.protkey.protkey,
				      &kcp.protkey.len, &kcp.protkey.type);
		DEBUG_DBG("%s pkey_clr2protkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ucp, &kcp, sizeof(kcp)))
			return -EFAULT;
		memzero_explicit(&kcp, sizeof(kcp));
		break;
	}
	case PKEY_FINDCARD: {
		struct pkey_findcard __user *ufc = (void __user *)arg;
		struct pkey_findcard kfc;

		if (copy_from_user(&kfc, ufc, sizeof(kfc)))
			return -EFAULT;
		rc = cca_findcard(kfc.seckey.seckey,
				  &kfc.cardnr, &kfc.domain, 1);
		DEBUG_DBG("%s cca_findcard()=%d\n", __func__, rc);
		if (rc < 0)
			break;
		if (copy_to_user(ufc, &kfc, sizeof(kfc)))
			return -EFAULT;
		break;
	}
	case PKEY_SKEY2PKEY: {
		struct pkey_skey2pkey __user *usp = (void __user *)arg;
		struct pkey_skey2pkey ksp;

		if (copy_from_user(&ksp, usp, sizeof(ksp)))
			return -EFAULT;
		ksp.protkey.len = sizeof(ksp.protkey.protkey);
		rc = pkey_skey2pkey(ksp.seckey.seckey, ksp.protkey.protkey,
				    &ksp.protkey.len, &ksp.protkey.type);
		DEBUG_DBG("%s pkey_skey2pkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(usp, &ksp, sizeof(ksp)))
			return -EFAULT;
		break;
	}
	case PKEY_VERIFYKEY: {
		struct pkey_verifykey __user *uvk = (void __user *)arg;
		struct pkey_verifykey kvk;

		if (copy_from_user(&kvk, uvk, sizeof(kvk)))
			return -EFAULT;
		rc = pkey_verifykey(&kvk.seckey, &kvk.cardnr, &kvk.domain,
				    &kvk.keysize, &kvk.attributes);
		DEBUG_DBG("%s pkey_verifykey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(uvk, &kvk, sizeof(kvk)))
			return -EFAULT;
		break;
	}
	case PKEY_GENPROTK: {
		struct pkey_genprotk __user *ugp = (void __user *)arg;
		struct pkey_genprotk kgp;

		if (copy_from_user(&kgp, ugp, sizeof(kgp)))
			return -EFAULT;
		kgp.protkey.len = sizeof(kgp.protkey.protkey);
		rc = pkey_genprotkey(kgp.keytype, kgp.protkey.protkey,
				     &kgp.protkey.len, &kgp.protkey.type);
		DEBUG_DBG("%s pkey_genprotkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ugp, &kgp, sizeof(kgp)))
			return -EFAULT;
		break;
	}
	case PKEY_VERIFYPROTK: {
		struct pkey_verifyprotk __user *uvp = (void __user *)arg;
		struct pkey_verifyprotk kvp;

		if (copy_from_user(&kvp, uvp, sizeof(kvp)))
			return -EFAULT;
		rc = pkey_verifyprotkey(kvp.protkey.protkey,
					kvp.protkey.len, kvp.protkey.type);
		DEBUG_DBG("%s pkey_verifyprotkey()=%d\n", __func__, rc);
		break;
	}
	case PKEY_KBLOB2PROTK: {
		struct pkey_kblob2pkey __user *utp = (void __user *)arg;
		struct pkey_kblob2pkey ktp;
		u8 *kkey;

		if (copy_from_user(&ktp, utp, sizeof(ktp)))
			return -EFAULT;
		kkey = _copy_key_from_user(ktp.key, ktp.keylen);
		if (IS_ERR(kkey))
			return PTR_ERR(kkey);
		ktp.protkey.len = sizeof(ktp.protkey.protkey);
		rc = pkey_keyblob2pkey(kkey, ktp.keylen, ktp.protkey.protkey,
				       &ktp.protkey.len, &ktp.protkey.type);
		DEBUG_DBG("%s pkey_keyblob2pkey()=%d\n", __func__, rc);
		memzero_explicit(kkey, ktp.keylen);
		kfree(kkey);
		if (rc)
			break;
		if (copy_to_user(utp, &ktp, sizeof(ktp)))
			return -EFAULT;
		break;
	}
	case PKEY_GENSECK2: {
		struct pkey_genseck2 __user *ugs = (void __user *)arg;
		size_t klen = KEYBLOBBUFSIZE;
		struct pkey_genseck2 kgs;
		struct pkey_apqn *apqns;
		u8 *kkey;

		if (copy_from_user(&kgs, ugs, sizeof(kgs)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(kgs.apqns, kgs.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = kzalloc(klen, GFP_KERNEL);
		if (!kkey) {
			kfree(apqns);
			return -ENOMEM;
		}
		rc = pkey_genseckey2(apqns, kgs.apqn_entries,
				     kgs.type, kgs.size, kgs.keygenflags,
				     kkey, &klen);
		DEBUG_DBG("%s pkey_genseckey2()=%d\n", __func__, rc);
		kfree(apqns);
		if (rc) {
			kfree(kkey);
			break;
		}
		if (kgs.key) {
			if (kgs.keylen < klen) {
				kfree(kkey);
				return -EINVAL;
			}
			if (copy_to_user(kgs.key, kkey, klen)) {
				kfree(kkey);
				return -EFAULT;
			}
		}
		kgs.keylen = klen;
		if (copy_to_user(ugs, &kgs, sizeof(kgs)))
			rc = -EFAULT;
		kfree(kkey);
		break;
	}
	case PKEY_CLR2SECK2: {
		struct pkey_clr2seck2 __user *ucs = (void __user *)arg;
		size_t klen = KEYBLOBBUFSIZE;
		struct pkey_clr2seck2 kcs;
		struct pkey_apqn *apqns;
		u8 *kkey;

		if (copy_from_user(&kcs, ucs, sizeof(kcs)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(kcs.apqns, kcs.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = kzalloc(klen, GFP_KERNEL);
		if (!kkey) {
			kfree(apqns);
			return -ENOMEM;
		}
		rc = pkey_clr2seckey2(apqns, kcs.apqn_entries,
				      kcs.type, kcs.size, kcs.keygenflags,
				      kcs.clrkey.clrkey, kkey, &klen);
		DEBUG_DBG("%s pkey_clr2seckey2()=%d\n", __func__, rc);
		kfree(apqns);
		if (rc) {
			kfree(kkey);
			break;
		}
		if (kcs.key) {
			if (kcs.keylen < klen) {
				kfree(kkey);
				return -EINVAL;
			}
			if (copy_to_user(kcs.key, kkey, klen)) {
				kfree(kkey);
				return -EFAULT;
			}
		}
		kcs.keylen = klen;
		if (copy_to_user(ucs, &kcs, sizeof(kcs)))
			rc = -EFAULT;
		memzero_explicit(&kcs, sizeof(kcs));
		kfree(kkey);
		break;
	}
	case PKEY_VERIFYKEY2: {
		struct pkey_verifykey2 __user *uvk = (void __user *)arg;
		struct pkey_verifykey2 kvk;
		u8 *kkey;

		if (copy_from_user(&kvk, uvk, sizeof(kvk)))
			return -EFAULT;
		kkey = _copy_key_from_user(kvk.key, kvk.keylen);
		if (IS_ERR(kkey))
			return PTR_ERR(kkey);
		rc = pkey_verifykey2(kkey, kvk.keylen,
				     &kvk.cardnr, &kvk.domain,
				     &kvk.type, &kvk.size, &kvk.flags);
		DEBUG_DBG("%s pkey_verifykey2()=%d\n", __func__, rc);
		kfree(kkey);
		if (rc)
			break;
		if (copy_to_user(uvk, &kvk, sizeof(kvk)))
			return -EFAULT;
		break;
	}
	case PKEY_KBLOB2PROTK2: {
		struct pkey_kblob2pkey2 __user *utp = (void __user *)arg;
		struct pkey_apqn *apqns = NULL;
		struct pkey_kblob2pkey2 ktp;
		u8 *kkey;

		if (copy_from_user(&ktp, utp, sizeof(ktp)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(ktp.apqns, ktp.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = _copy_key_from_user(ktp.key, ktp.keylen);
		if (IS_ERR(kkey)) {
			kfree(apqns);
			return PTR_ERR(kkey);
		}
		ktp.protkey.len = sizeof(ktp.protkey.protkey);
		rc = pkey_keyblob2pkey2(apqns, ktp.apqn_entries,
					kkey, ktp.keylen,
					ktp.protkey.protkey, &ktp.protkey.len,
					&ktp.protkey.type);
		DEBUG_DBG("%s pkey_keyblob2pkey2()=%d\n", __func__, rc);
		kfree(apqns);
		memzero_explicit(kkey, ktp.keylen);
		kfree(kkey);
		if (rc)
			break;
		if (copy_to_user(utp, &ktp, sizeof(ktp)))
			return -EFAULT;
		break;
	}
	case PKEY_APQNS4K: {
		struct pkey_apqns4key __user *uak = (void __user *)arg;
		struct pkey_apqn *apqns = NULL;
		struct pkey_apqns4key kak;
		size_t nr_apqns, len;
		u8 *kkey;

		if (copy_from_user(&kak, uak, sizeof(kak)))
			return -EFAULT;
		nr_apqns = kak.apqn_entries;
		if (nr_apqns) {
			apqns = kmalloc_array(nr_apqns,
					      sizeof(struct pkey_apqn),
					      GFP_KERNEL);
			if (!apqns)
				return -ENOMEM;
		}
		kkey = _copy_key_from_user(kak.key, kak.keylen);
		if (IS_ERR(kkey)) {
			kfree(apqns);
			return PTR_ERR(kkey);
		}
		rc = pkey_apqns4key(kkey, kak.keylen, kak.flags,
				    apqns, &nr_apqns);
		DEBUG_DBG("%s pkey_apqns4key()=%d\n", __func__, rc);
		kfree(kkey);
		if (rc && rc != -ENOSPC) {
			kfree(apqns);
			break;
		}
		if (!rc && kak.apqns) {
			if (nr_apqns > kak.apqn_entries) {
				kfree(apqns);
				return -EINVAL;
			}
			len = nr_apqns * sizeof(struct pkey_apqn);
			if (len) {
				if (copy_to_user(kak.apqns, apqns, len)) {
					kfree(apqns);
					return -EFAULT;
				}
			}
		}
		kak.apqn_entries = nr_apqns;
		if (copy_to_user(uak, &kak, sizeof(kak)))
			rc = -EFAULT;
		kfree(apqns);
		break;
	}
	case PKEY_APQNS4KT: {
		struct pkey_apqns4keytype __user *uat = (void __user *)arg;
		struct pkey_apqn *apqns = NULL;
		struct pkey_apqns4keytype kat;
		size_t nr_apqns, len;

		if (copy_from_user(&kat, uat, sizeof(kat)))
			return -EFAULT;
		nr_apqns = kat.apqn_entries;
		if (nr_apqns) {
			apqns = kmalloc_array(nr_apqns,
					      sizeof(struct pkey_apqn),
					      GFP_KERNEL);
			if (!apqns)
				return -ENOMEM;
		}
		rc = pkey_apqns4keytype(kat.type, kat.cur_mkvp, kat.alt_mkvp,
					kat.flags, apqns, &nr_apqns);
		DEBUG_DBG("%s pkey_apqns4keytype()=%d\n", __func__, rc);
		if (rc && rc != -ENOSPC) {
			kfree(apqns);
			break;
		}
		if (!rc && kat.apqns) {
			if (nr_apqns > kat.apqn_entries) {
				kfree(apqns);
				return -EINVAL;
			}
			len = nr_apqns * sizeof(struct pkey_apqn);
			if (len) {
				if (copy_to_user(kat.apqns, apqns, len)) {
					kfree(apqns);
					return -EFAULT;
				}
			}
		}
		kat.apqn_entries = nr_apqns;
		if (copy_to_user(uat, &kat, sizeof(kat)))
			rc = -EFAULT;
		kfree(apqns);
		break;
	}
	case PKEY_KBLOB2PROTK3: {
		struct pkey_kblob2pkey3 __user *utp = (void __user *)arg;
		u32 protkeylen = PROTKEYBLOBBUFSIZE;
		struct pkey_apqn *apqns = NULL;
		struct pkey_kblob2pkey3 ktp;
		u8 *kkey, *protkey;

		if (copy_from_user(&ktp, utp, sizeof(ktp)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(ktp.apqns, ktp.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = _copy_key_from_user(ktp.key, ktp.keylen);
		if (IS_ERR(kkey)) {
			kfree(apqns);
			return PTR_ERR(kkey);
		}
		protkey = kmalloc(protkeylen, GFP_KERNEL);
		if (!protkey) {
			kfree(apqns);
			kfree(kkey);
			return -ENOMEM;
		}
		rc = pkey_keyblob2pkey3(apqns, ktp.apqn_entries,
					kkey, ktp.keylen,
					protkey, &protkeylen, &ktp.pkeytype);
		DEBUG_DBG("%s pkey_keyblob2pkey3()=%d\n", __func__, rc);
		kfree(apqns);
		memzero_explicit(kkey, ktp.keylen);
		kfree(kkey);
		if (rc) {
			kfree(protkey);
			break;
		}
		if (ktp.pkey && ktp.pkeylen) {
			if (protkeylen > ktp.pkeylen) {
				kfree(protkey);
				return -EINVAL;
			}
			if (copy_to_user(ktp.pkey, protkey, protkeylen)) {
				kfree(protkey);
				return -EFAULT;
			}
		}
		kfree(protkey);
		ktp.pkeylen = protkeylen;
		if (copy_to_user(utp, &ktp, sizeof(ktp)))
			return -EFAULT;
		break;
	}
	default:
		/* unknown/unsupported ioctl cmd */
		return -ENOTTY;
	}

	return rc;
}

/*
 * Sysfs and file io operations
 */

/*
 * Sysfs attribute read function for all protected key binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * protected key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 */
static ssize_t pkey_protkey_aes_attr_read(u32 keytype, bool is_xts, char *buf,
					  loff_t off, size_t count)
{
	struct protaeskeytoken protkeytoken;
	struct pkey_protkey protkey;
	int rc;

	if (off != 0 || count < sizeof(protkeytoken))
		return -EINVAL;
	if (is_xts)
		if (count < 2 * sizeof(protkeytoken))
			return -EINVAL;

	memset(&protkeytoken, 0, sizeof(protkeytoken));
	protkeytoken.type = TOKTYPE_NON_CCA;
	protkeytoken.version = TOKVER_PROTECTED_KEY;
	protkeytoken.keytype = keytype;

	protkey.len = sizeof(protkey.protkey);
	rc = pkey_genprotkey(protkeytoken.keytype,
			     protkey.protkey, &protkey.len, &protkey.type);
	if (rc)
		return rc;

	protkeytoken.len = protkey.len;
	memcpy(&protkeytoken.protkey, &protkey.protkey, protkey.len);

	memcpy(buf, &protkeytoken, sizeof(protkeytoken));

	if (is_xts) {
		/* xts needs a second protected key, reuse protkey struct */
		protkey.len = sizeof(protkey.protkey);
		rc = pkey_genprotkey(protkeytoken.keytype,
				     protkey.protkey, &protkey.len, &protkey.type);
		if (rc)
			return rc;

		protkeytoken.len = protkey.len;
		memcpy(&protkeytoken.protkey, &protkey.protkey, protkey.len);

		memcpy(buf + sizeof(protkeytoken), &protkeytoken,
		       sizeof(protkeytoken));

		return 2 * sizeof(protkeytoken);
	}

	return sizeof(protkeytoken);
}

static ssize_t protkey_aes_128_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_128, false, buf,
					  off, count);
}

static ssize_t protkey_aes_192_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_192, false, buf,
					  off, count);
}

static ssize_t protkey_aes_256_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_256, false, buf,
					  off, count);
}

static ssize_t protkey_aes_128_xts_read(struct file *filp,
					struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_128, true, buf,
					  off, count);
}

static ssize_t protkey_aes_256_xts_read(struct file *filp,
					struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_256, true, buf,
					  off, count);
}

static BIN_ATTR_RO(protkey_aes_128, sizeof(struct protaeskeytoken));
static BIN_ATTR_RO(protkey_aes_192, sizeof(struct protaeskeytoken));
static BIN_ATTR_RO(protkey_aes_256, sizeof(struct protaeskeytoken));
static BIN_ATTR_RO(protkey_aes_128_xts, 2 * sizeof(struct protaeskeytoken));
static BIN_ATTR_RO(protkey_aes_256_xts, 2 * sizeof(struct protaeskeytoken));

static struct bin_attribute *protkey_attrs[] = {
	&bin_attr_protkey_aes_128,
	&bin_attr_protkey_aes_192,
	&bin_attr_protkey_aes_256,
	&bin_attr_protkey_aes_128_xts,
	&bin_attr_protkey_aes_256_xts,
	NULL
};

static struct attribute_group protkey_attr_group = {
	.name	   = "protkey",
	.bin_attrs = protkey_attrs,
};

/*
 * Sysfs attribute read function for all secure key ccadata binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * protected key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 */
static ssize_t pkey_ccadata_aes_attr_read(u32 keytype, bool is_xts, char *buf,
					  loff_t off, size_t count)
{
	struct pkey_seckey *seckey = (struct pkey_seckey *)buf;
	int rc;

	if (off != 0 || count < sizeof(struct secaeskeytoken))
		return -EINVAL;
	if (is_xts)
		if (count < 2 * sizeof(struct secaeskeytoken))
			return -EINVAL;

	rc = cca_genseckey(-1, -1, keytype, seckey->seckey);
	if (rc)
		return rc;

	if (is_xts) {
		seckey++;
		rc = cca_genseckey(-1, -1, keytype, seckey->seckey);
		if (rc)
			return rc;

		return 2 * sizeof(struct secaeskeytoken);
	}

	return sizeof(struct secaeskeytoken);
}

static ssize_t ccadata_aes_128_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_128, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_192_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_192, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_256_read(struct file *filp,
				    struct kobject *kobj,
				    struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_256, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_128_xts_read(struct file *filp,
					struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_128, true, buf,
					  off, count);
}

static ssize_t ccadata_aes_256_xts_read(struct file *filp,
					struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_256, true, buf,
					  off, count);
}

static BIN_ATTR_RO(ccadata_aes_128, sizeof(struct secaeskeytoken));
static BIN_ATTR_RO(ccadata_aes_192, sizeof(struct secaeskeytoken));
static BIN_ATTR_RO(ccadata_aes_256, sizeof(struct secaeskeytoken));
static BIN_ATTR_RO(ccadata_aes_128_xts, 2 * sizeof(struct secaeskeytoken));
static BIN_ATTR_RO(ccadata_aes_256_xts, 2 * sizeof(struct secaeskeytoken));

static struct bin_attribute *ccadata_attrs[] = {
	&bin_attr_ccadata_aes_128,
	&bin_attr_ccadata_aes_192,
	&bin_attr_ccadata_aes_256,
	&bin_attr_ccadata_aes_128_xts,
	&bin_attr_ccadata_aes_256_xts,
	NULL
};

static struct attribute_group ccadata_attr_group = {
	.name	   = "ccadata",
	.bin_attrs = ccadata_attrs,
};

#define CCACIPHERTOKENSIZE	(sizeof(struct cipherkeytoken) + 80)

/*
 * Sysfs attribute read function for all secure key ccacipher binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * secure key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 */
static ssize_t pkey_ccacipher_aes_attr_read(enum pkey_key_size keybits,
					    bool is_xts, char *buf, loff_t off,
					    size_t count)
{
	size_t keysize = CCACIPHERTOKENSIZE;
	u32 nr_apqns, *apqns = NULL;
	int i, rc, card, dom;

	if (off != 0 || count < CCACIPHERTOKENSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * CCACIPHERTOKENSIZE)
			return -EINVAL;

	/* build a list of apqns able to generate an cipher key */
	rc = cca_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			   ZCRYPT_CEX6, 0, 0, 0, 0);
	if (rc)
		return rc;

	memset(buf, 0, is_xts ? 2 * keysize : keysize);

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = cca_gencipherkey(card, dom, keybits, 0, buf, &keysize);
		if (rc == 0)
			break;
	}
	if (rc)
		return rc;

	if (is_xts) {
		keysize = CCACIPHERTOKENSIZE;
		buf += CCACIPHERTOKENSIZE;
		rc = cca_gencipherkey(card, dom, keybits, 0, buf, &keysize);
		if (rc == 0)
			return 2 * CCACIPHERTOKENSIZE;
	}

	return CCACIPHERTOKENSIZE;
}

static ssize_t ccacipher_aes_128_read(struct file *filp,
				      struct kobject *kobj,
				      struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_128, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_192_read(struct file *filp,
				      struct kobject *kobj,
				      struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_192, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_256_read(struct file *filp,
				      struct kobject *kobj,
				      struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_256, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_128_xts_read(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *attr,
					  char *buf, loff_t off,
					  size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_128, true, buf,
					    off, count);
}

static ssize_t ccacipher_aes_256_xts_read(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *attr,
					  char *buf, loff_t off,
					  size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_256, true, buf,
					    off, count);
}

static BIN_ATTR_RO(ccacipher_aes_128, CCACIPHERTOKENSIZE);
static BIN_ATTR_RO(ccacipher_aes_192, CCACIPHERTOKENSIZE);
static BIN_ATTR_RO(ccacipher_aes_256, CCACIPHERTOKENSIZE);
static BIN_ATTR_RO(ccacipher_aes_128_xts, 2 * CCACIPHERTOKENSIZE);
static BIN_ATTR_RO(ccacipher_aes_256_xts, 2 * CCACIPHERTOKENSIZE);

static struct bin_attribute *ccacipher_attrs[] = {
	&bin_attr_ccacipher_aes_128,
	&bin_attr_ccacipher_aes_192,
	&bin_attr_ccacipher_aes_256,
	&bin_attr_ccacipher_aes_128_xts,
	&bin_attr_ccacipher_aes_256_xts,
	NULL
};

static struct attribute_group ccacipher_attr_group = {
	.name	   = "ccacipher",
	.bin_attrs = ccacipher_attrs,
};

/*
 * Sysfs attribute read function for all ep11 aes key binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * secure key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 * This function and the sysfs attributes using it provide EP11 key blobs
 * padded to the upper limit of MAXEP11AESKEYBLOBSIZE which is currently
 * 336 bytes.
 */
static ssize_t pkey_ep11_aes_attr_read(enum pkey_key_size keybits,
				       bool is_xts, char *buf, loff_t off,
				       size_t count)
{
	size_t keysize = MAXEP11AESKEYBLOBSIZE;
	u32 nr_apqns, *apqns = NULL;
	int i, rc, card, dom;

	if (off != 0 || count < MAXEP11AESKEYBLOBSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * MAXEP11AESKEYBLOBSIZE)
			return -EINVAL;

	/* build a list of apqns able to generate an cipher key */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7,
			    ap_is_se_guest() ? EP11_API_V6 : EP11_API_V4,
			    NULL);
	if (rc)
		return rc;

	memset(buf, 0, is_xts ? 2 * keysize : keysize);

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_genaeskey(card, dom, keybits, 0, buf, &keysize,
				    PKEY_TYPE_EP11_AES);
		if (rc == 0)
			break;
	}
	if (rc)
		return rc;

	if (is_xts) {
		keysize = MAXEP11AESKEYBLOBSIZE;
		buf += MAXEP11AESKEYBLOBSIZE;
		rc = ep11_genaeskey(card, dom, keybits, 0, buf, &keysize,
				    PKEY_TYPE_EP11_AES);
		if (rc == 0)
			return 2 * MAXEP11AESKEYBLOBSIZE;
	}

	return MAXEP11AESKEYBLOBSIZE;
}

static ssize_t ep11_aes_128_read(struct file *filp,
				 struct kobject *kobj,
				 struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_128, false, buf,
				       off, count);
}

static ssize_t ep11_aes_192_read(struct file *filp,
				 struct kobject *kobj,
				 struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_192, false, buf,
				       off, count);
}

static ssize_t ep11_aes_256_read(struct file *filp,
				 struct kobject *kobj,
				 struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_256, false, buf,
				       off, count);
}

static ssize_t ep11_aes_128_xts_read(struct file *filp,
				     struct kobject *kobj,
				     struct bin_attribute *attr,
				     char *buf, loff_t off,
				     size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_128, true, buf,
				       off, count);
}

static ssize_t ep11_aes_256_xts_read(struct file *filp,
				     struct kobject *kobj,
				     struct bin_attribute *attr,
				     char *buf, loff_t off,
				     size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_256, true, buf,
				       off, count);
}

static BIN_ATTR_RO(ep11_aes_128, MAXEP11AESKEYBLOBSIZE);
static BIN_ATTR_RO(ep11_aes_192, MAXEP11AESKEYBLOBSIZE);
static BIN_ATTR_RO(ep11_aes_256, MAXEP11AESKEYBLOBSIZE);
static BIN_ATTR_RO(ep11_aes_128_xts, 2 * MAXEP11AESKEYBLOBSIZE);
static BIN_ATTR_RO(ep11_aes_256_xts, 2 * MAXEP11AESKEYBLOBSIZE);

static struct bin_attribute *ep11_attrs[] = {
	&bin_attr_ep11_aes_128,
	&bin_attr_ep11_aes_192,
	&bin_attr_ep11_aes_256,
	&bin_attr_ep11_aes_128_xts,
	&bin_attr_ep11_aes_256_xts,
	NULL
};

static struct attribute_group ep11_attr_group = {
	.name	   = "ep11",
	.bin_attrs = ep11_attrs,
};

static const struct attribute_group *pkey_attr_groups[] = {
	&protkey_attr_group,
	&ccadata_attr_group,
	&ccacipher_attr_group,
	&ep11_attr_group,
	NULL,
};

static const struct file_operations pkey_fops = {
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.llseek		= no_llseek,
	.unlocked_ioctl = pkey_unlocked_ioctl,
};

static struct miscdevice pkey_dev = {
	.name	= "pkey",
	.minor	= MISC_DYNAMIC_MINOR,
	.mode	= 0666,
	.fops	= &pkey_fops,
	.groups = pkey_attr_groups,
};

/*
 * Module init
 */
static int __init pkey_init(void)
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

	/* check for kmc instructions available */
	if (!cpacf_query(CPACF_KMC, &func_mask))
		return -ENODEV;
	if (!cpacf_test_func(&func_mask, CPACF_KMC_PAES_128) ||
	    !cpacf_test_func(&func_mask, CPACF_KMC_PAES_192) ||
	    !cpacf_test_func(&func_mask, CPACF_KMC_PAES_256))
		return -ENODEV;

	pkey_debug_init();

	return misc_register(&pkey_dev);
}

/*
 * Module exit
 */
static void __exit pkey_exit(void)
{
	misc_deregister(&pkey_dev);
	pkey_debug_exit();
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, pkey_init);
module_exit(pkey_exit);
