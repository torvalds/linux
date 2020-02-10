// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey device driver
 *
 *  Copyright IBM Corp. 2017,2019
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

#define KEYBLOBBUFSIZE 8192  /* key buffer size used for internal processing */
#define MAXAPQNSINLIST 64    /* max 64 apqns within a apqn list */

/* mask of available pckmo subfunctions, fetched once at module init */
static cpacf_mask_t pckmo_functions;

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
struct clearaeskeytoken {
	u8  type;	 /* 0x00 for PAES specific key tokens */
	u8  res0[3];
	u8  version;	 /* 0x02 for clear AES key token */
	u8  res1[3];
	u32 keytype;	 /* key type, one of the PKEY_KEYTYPE values */
	u32 len;	 /* bytes actually stored in clearkey[] */
	u8  clearkey[0]; /* clear key value */
} __packed;

/*
 * Create a protected key from a clear key value.
 */
static int pkey_clr2protkey(u32 keytype,
			    const struct pkey_clrkey *clrkey,
			    struct pkey_protkey *protkey)
{
	long fc;
	int keysize;
	u8 paramblock[64];

	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		keysize = 16;
		fc = CPACF_PCKMO_ENC_AES_128_KEY;
		break;
	case PKEY_KEYTYPE_AES_192:
		keysize = 24;
		fc = CPACF_PCKMO_ENC_AES_192_KEY;
		break;
	case PKEY_KEYTYPE_AES_256:
		keysize = 32;
		fc = CPACF_PCKMO_ENC_AES_256_KEY;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n",
			  __func__, keytype);
		return -EINVAL;
	}

	/*
	 * Check if the needed pckmo subfunction is available.
	 * These subfunctions can be enabled/disabled by customers
	 * in the LPAR profile or may even change on the fly.
	 */
	if (!cpacf_test_func(&pckmo_functions, fc)) {
		DEBUG_ERR("%s pckmo functions not available\n", __func__);
		return -ENODEV;
	}

	/* prepare param block */
	memset(paramblock, 0, sizeof(paramblock));
	memcpy(paramblock, clrkey->clrkey, keysize);

	/* call the pckmo instruction */
	cpacf_pckmo(fc, paramblock);

	/* copy created protected key */
	protkey->type = keytype;
	protkey->len = keysize + 32;
	memcpy(protkey->protkey, paramblock, keysize + 32);

	return 0;
}

/*
 * Find card and transform secure key into protected key.
 */
static int pkey_skey2pkey(const u8 *key, struct pkey_protkey *pkey)
{
	int rc, verify;
	u16 cardnr, domain;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	/*
	 * The cca_xxx2protkey call may fail when a card has been
	 * addressed where the master key was changed after last fetch
	 * of the mkvp into the cache. Try 3 times: First witout verify
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
			rc = cca_sec2protkey(cardnr, domain,
					     key, pkey->protkey,
					     &pkey->len, &pkey->type);
			break;
		case TOKVER_CCA_VLSC:
			rc = cca_cipher2protkey(cardnr, domain,
						key, pkey->protkey,
						&pkey->len, &pkey->type);
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
	int i, rc;
	u16 card, dom;
	u32 nr_apqns, *apqns = NULL;

	/* build a list of apqns suitable for ep11 keys with cpacf support */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7, EP11_API_V, NULL);
	if (rc)
		goto out;

	/* go through the list of apqns and try to bild an ep11 key */
	for (rc = -ENODEV, i = 0; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_clr2keyblob(card, dom, clrkeylen * 8,
				      0, clrkey, keybuf, keybuflen);
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
static int pkey_ep11key2pkey(const u8 *key, struct pkey_protkey *pkey)
{
	int i, rc;
	u16 card, dom;
	u32 nr_apqns, *apqns = NULL;
	struct ep11keyblob *kb = (struct ep11keyblob *) key;

	/* build a list of apqns suitable for this key */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7, EP11_API_V, kb->wkvp);
	if (rc)
		goto out;

	/* go through the list of apqns and try to derive an pkey */
	for (rc = -ENODEV, i = 0; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_key2protkey(card, dom, key, kb->head.len,
				      pkey->protkey, &pkey->len, &pkey->type);
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
	struct secaeskeytoken *t = (struct secaeskeytoken *) seckey;
	u16 cardnr, domain;
	int rc;

	/* check the secure key for valid AES secure key */
	rc = cca_check_secaeskeytoken(debug_info, 3, (u8 *) seckey, 0);
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
static int pkey_genprotkey(u32 keytype, struct pkey_protkey *protkey)
{
	struct pkey_clrkey clrkey;
	int keysize;
	int rc;

	switch (keytype) {
	case PKEY_KEYTYPE_AES_128:
		keysize = 16;
		break;
	case PKEY_KEYTYPE_AES_192:
		keysize = 24;
		break;
	case PKEY_KEYTYPE_AES_256:
		keysize = 32;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n", __func__,
			  keytype);
		return -EINVAL;
	}

	/* generate a dummy random clear key */
	get_random_bytes(clrkey.clrkey, keysize);

	/* convert it to a dummy protected key */
	rc = pkey_clr2protkey(keytype, &clrkey, protkey);
	if (rc)
		return rc;

	/* replace the key part of the protected key with random bytes */
	get_random_bytes(protkey->protkey, keysize);

	return 0;
}

/*
 * Verify if a protected key is still valid
 */
static int pkey_verifyprotkey(const struct pkey_protkey *protkey)
{
	unsigned long fc;
	struct {
		u8 iv[AES_BLOCK_SIZE];
		u8 key[MAXPROTKEYSIZE];
	} param;
	u8 null_msg[AES_BLOCK_SIZE];
	u8 dest_buf[AES_BLOCK_SIZE];
	unsigned int k;

	switch (protkey->type) {
	case PKEY_KEYTYPE_AES_128:
		fc = CPACF_KMC_PAES_128;
		break;
	case PKEY_KEYTYPE_AES_192:
		fc = CPACF_KMC_PAES_192;
		break;
	case PKEY_KEYTYPE_AES_256:
		fc = CPACF_KMC_PAES_256;
		break;
	default:
		DEBUG_ERR("%s unknown/unsupported keytype %d\n", __func__,
			  protkey->type);
		return -EINVAL;
	}

	memset(null_msg, 0, sizeof(null_msg));

	memset(param.iv, 0, sizeof(param.iv));
	memcpy(param.key, protkey->protkey, sizeof(param.key));

	k = cpacf_kmc(fc | CPACF_ENCRYPT, &param, null_msg, dest_buf,
		      sizeof(null_msg));
	if (k != sizeof(null_msg)) {
		DEBUG_ERR("%s protected key is not valid\n", __func__);
		return -EKEYREJECTED;
	}

	return 0;
}

/*
 * Transform a non-CCA key token into a protected key
 */
static int pkey_nonccatok2pkey(const u8 *key, u32 keylen,
			       struct pkey_protkey *protkey)
{
	int rc = -EINVAL;
	u8 *tmpbuf = NULL;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	switch (hdr->version) {
	case TOKVER_PROTECTED_KEY: {
		struct protaeskeytoken *t;

		if (keylen != sizeof(struct protaeskeytoken))
			goto out;
		t = (struct protaeskeytoken *)key;
		protkey->len = t->len;
		protkey->type = t->keytype;
		memcpy(protkey->protkey, t->protkey,
		       sizeof(protkey->protkey));
		rc = pkey_verifyprotkey(protkey);
		break;
	}
	case TOKVER_CLEAR_KEY: {
		struct clearaeskeytoken *t;
		struct pkey_clrkey ckey;
		union u_tmpbuf {
			u8 skey[SECKEYBLOBSIZE];
			u8 ep11key[MAXEP11AESKEYBLOBSIZE];
		};
		size_t tmpbuflen = sizeof(union u_tmpbuf);

		if (keylen < sizeof(struct clearaeskeytoken))
			goto out;
		t = (struct clearaeskeytoken *)key;
		if (keylen != sizeof(*t) + t->len)
			goto out;
		if ((t->keytype == PKEY_KEYTYPE_AES_128 && t->len == 16)
		    || (t->keytype == PKEY_KEYTYPE_AES_192 && t->len == 24)
		    || (t->keytype == PKEY_KEYTYPE_AES_256 && t->len == 32))
			memcpy(ckey.clrkey, t->clearkey, t->len);
		else
			goto out;
		/* alloc temp key buffer space */
		tmpbuf = kmalloc(tmpbuflen, GFP_ATOMIC);
		if (!tmpbuf) {
			rc = -ENOMEM;
			goto out;
		}
		/* try direct way with the PCKMO instruction */
		rc = pkey_clr2protkey(t->keytype, &ckey, protkey);
		if (rc == 0)
			break;
		/* PCKMO failed, so try the CCA secure key way */
		rc = cca_clr2seckey(0xFFFF, 0xFFFF, t->keytype,
				    ckey.clrkey, tmpbuf);
		if (rc == 0)
			rc = pkey_skey2pkey(tmpbuf, protkey);
		if (rc == 0)
			break;
		/* if the CCA way also failed, let's try via EP11 */
		rc = pkey_clr2ep11key(ckey.clrkey, t->len,
				      tmpbuf, &tmpbuflen);
		if (rc == 0)
			rc = pkey_ep11key2pkey(tmpbuf, protkey);
		/* now we should really have an protected key */
		DEBUG_ERR("%s unable to build protected key from clear",
			  __func__);
		break;
	}
	case TOKVER_EP11_AES: {
		if (keylen < MINEP11AESKEYBLOBSIZE)
			goto out;
		/* check ep11 key for exportable as protected key */
		rc = ep11_check_aeskeyblob(debug_info, 3, key, 0, 1);
		if (rc)
			goto out;
		rc = pkey_ep11key2pkey(key, protkey);
		break;
	}
	default:
		DEBUG_ERR("%s unknown/unsupported non-CCA token version %d\n",
			  __func__, hdr->version);
		rc = -EINVAL;
	}

out:
	kfree(tmpbuf);
	return rc;
}

/*
 * Transform a CCA internal key token into a protected key
 */
static int pkey_ccainttok2pkey(const u8 *key, u32 keylen,
			       struct pkey_protkey *protkey)
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

	return pkey_skey2pkey(key, protkey);
}

/*
 * Transform a key blob (of any type) into a protected key
 */
int pkey_keyblob2pkey(const u8 *key, u32 keylen,
		      struct pkey_protkey *protkey)
{
	int rc;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	if (keylen < sizeof(struct keytoken_header)) {
		DEBUG_ERR("%s invalid keylen %d\n", __func__, keylen);
		return -EINVAL;
	}

	switch (hdr->type) {
	case TOKTYPE_NON_CCA:
		rc = pkey_nonccatok2pkey(key, keylen, protkey);
		break;
	case TOKTYPE_CCA_INTERNAL:
		rc = pkey_ccainttok2pkey(key, keylen, protkey);
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
		if (ktype == PKEY_TYPE_EP11) {
			rc = ep11_genaeskey(card, dom, ksize, kflags,
					    keybuf, keybufsize);
		} else if (ktype == PKEY_TYPE_CCA_DATA) {
			rc = cca_genseckey(card, dom, ksize, keybuf);
			*keybufsize = (rc ? 0 : SECKEYBLOBSIZE);
		} else /* TOKVER_CCA_VLSC */
			rc = cca_gencipherkey(card, dom, ksize, kflags,
					      keybuf, keybufsize);
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
		if (ktype == PKEY_TYPE_EP11) {
			rc = ep11_clr2keyblob(card, dom, ksize, kflags,
					      clrkey, keybuf, keybufsize);
		} else if (ktype == PKEY_TYPE_CCA_DATA) {
			rc = cca_clr2seckey(card, dom, ksize,
					    clrkey, keybuf);
			*keybufsize = (rc ? 0 : SECKEYBLOBSIZE);
		} else /* TOKVER_CCA_VLSC */
			rc = cca_clr2cipherkey(card, dom, ksize, kflags,
					       clrkey, keybuf, keybufsize);
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
	int rc;
	u32 _nr_apqns, *_apqns = NULL;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	if (keylen < sizeof(struct keytoken_header))
		return -EINVAL;

	if (hdr->type == TOKTYPE_CCA_INTERNAL
	    && hdr->version == TOKVER_CCA_AES) {
		struct secaeskeytoken *t = (struct secaeskeytoken *)key;

		rc = cca_check_secaeskeytoken(debug_info, 3, key, 0);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_CCA_DATA;
		if (ksize)
			*ksize = (enum pkey_key_size) t->bitsize;

		rc = cca_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				   ZCRYPT_CEX3C, t->mkvp, 0, 1);
		if (rc == 0 && flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&_apqns, &_nr_apqns,
					   *cardnr, *domain,
					   ZCRYPT_CEX3C, 0, t->mkvp, 1);
			if (rc == 0 && flags)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else if (hdr->type == TOKTYPE_CCA_INTERNAL
		   && hdr->version == TOKVER_CCA_VLSC) {
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
				   ZCRYPT_CEX6, t->mkvp0, 0, 1);
		if (rc == 0 && flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;
		if (rc == -ENODEV) {
			rc = cca_findcard2(&_apqns, &_nr_apqns,
					   *cardnr, *domain,
					   ZCRYPT_CEX6, 0, t->mkvp0, 1);
			if (rc == 0 && flags)
				*flags = PKEY_FLAGS_MATCH_ALT_MKVP;
		}
		if (rc)
			goto out;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else if (hdr->type == TOKTYPE_NON_CCA
		   && hdr->version == TOKVER_EP11_AES) {
		struct ep11keyblob *kb = (struct ep11keyblob *)key;

		rc = ep11_check_aeskeyblob(debug_info, 3, key, 0, 1);
		if (rc)
			goto out;
		if (ktype)
			*ktype = PKEY_TYPE_EP11;
		if (ksize)
			*ksize = kb->head.keybitlen;

		rc = ep11_findcard2(&_apqns, &_nr_apqns, *cardnr, *domain,
				    ZCRYPT_CEX7, EP11_API_V, kb->wkvp);
		if (rc)
			goto out;

		if (flags)
			*flags = PKEY_FLAGS_MATCH_CUR_MKVP;

		*cardnr = ((struct pkey_apqn *)_apqns)->card;
		*domain = ((struct pkey_apqn *)_apqns)->domain;

	} else
		rc = -EINVAL;

out:
	kfree(_apqns);
	return rc;
}

static int pkey_keyblob2pkey2(const struct pkey_apqn *apqns, size_t nr_apqns,
			      const u8 *key, size_t keylen,
			      struct pkey_protkey *pkey)
{
	int i, card, dom, rc;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

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
			if (keylen < sizeof(struct ep11keyblob))
				return -EINVAL;
			if (ep11_check_aeskeyblob(debug_info, 3, key, 0, 1))
				return -EINVAL;
		} else {
			return pkey_nonccatok2pkey(key, keylen, pkey);
		}
	} else {
		DEBUG_ERR("%s unknown/unsupported blob type %d\n",
			  __func__, hdr->type);
		return -EINVAL;
	}

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i].card;
		dom = apqns[i].domain;
		if (hdr->type == TOKTYPE_CCA_INTERNAL
		    && hdr->version == TOKVER_CCA_AES)
			rc = cca_sec2protkey(card, dom, key, pkey->protkey,
					     &pkey->len, &pkey->type);
		else if (hdr->type == TOKTYPE_CCA_INTERNAL
			 && hdr->version == TOKVER_CCA_VLSC)
			rc = cca_cipher2protkey(card, dom, key, pkey->protkey,
						&pkey->len, &pkey->type);
		else { /* EP11 AES secure key blob */
			struct ep11keyblob *kb = (struct ep11keyblob *) key;

			rc = ep11_key2protkey(card, dom, key, kb->head.len,
					      pkey->protkey, &pkey->len,
					      &pkey->type);
		}
		if (rc == 0)
			break;
	}

	return rc;
}

static int pkey_apqns4key(const u8 *key, size_t keylen, u32 flags,
			  struct pkey_apqn *apqns, size_t *nr_apqns)
{
	int rc = EINVAL;
	u32 _nr_apqns, *_apqns = NULL;
	struct keytoken_header *hdr = (struct keytoken_header *)key;

	if (keylen < sizeof(struct keytoken_header) || flags == 0)
		return -EINVAL;

	if (hdr->type == TOKTYPE_NON_CCA && hdr->version == TOKVER_EP11_AES) {
		int minhwtype = 0, api = 0;
		struct ep11keyblob *kb = (struct ep11keyblob *) key;

		if (flags != PKEY_FLAGS_MATCH_CUR_MKVP)
			return -EINVAL;
		if (kb->attr & EP11_BLOB_PKEY_EXTRACTABLE) {
			minhwtype = ZCRYPT_CEX7;
			api = EP11_API_V;
		}
		rc = ep11_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				    minhwtype, api, kb->wkvp);
		if (rc)
			goto out;
	} else if (hdr->type == TOKTYPE_CCA_INTERNAL) {
		int minhwtype = ZCRYPT_CEX3C;
		u64 cur_mkvp = 0, old_mkvp = 0;

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
				   minhwtype, cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;
	} else
		return -EINVAL;

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
	int rc = -EINVAL;
	u32 _nr_apqns, *_apqns = NULL;

	if (ktype == PKEY_TYPE_CCA_DATA || ktype == PKEY_TYPE_CCA_CIPHER) {
		u64 cur_mkvp = 0, old_mkvp = 0;
		int minhwtype = ZCRYPT_CEX3C;

		if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
			cur_mkvp = *((u64 *) cur_mkvp);
		if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
			old_mkvp = *((u64 *) alt_mkvp);
		if (ktype == PKEY_TYPE_CCA_CIPHER)
			minhwtype = ZCRYPT_CEX6;
		rc = cca_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				   minhwtype, cur_mkvp, old_mkvp, 1);
		if (rc)
			goto out;
	} else if (ktype == PKEY_TYPE_EP11) {
		u8 *wkvp = NULL;

		if (flags & PKEY_FLAGS_MATCH_CUR_MKVP)
			wkvp = cur_mkvp;
		rc = ep11_findcard2(&_apqns, &_nr_apqns, 0xFFFF, 0xFFFF,
				    ZCRYPT_CEX7, EP11_API_V, wkvp);
		if (rc)
			goto out;

	} else
		return -EINVAL;

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

/*
 * File io functions
 */

static void *_copy_key_from_user(void __user *ukey, size_t keylen)
{
	if (!ukey || keylen < MINKEYBLOBSIZE || keylen > KEYBLOBBUFSIZE)
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
		struct pkey_genseck __user *ugs = (void __user *) arg;
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
		struct pkey_clr2seck __user *ucs = (void __user *) arg;
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
		struct pkey_sec2protk __user *usp = (void __user *) arg;
		struct pkey_sec2protk ksp;

		if (copy_from_user(&ksp, usp, sizeof(ksp)))
			return -EFAULT;
		rc = cca_sec2protkey(ksp.cardnr, ksp.domain,
				     ksp.seckey.seckey, ksp.protkey.protkey,
				     NULL, &ksp.protkey.type);
		DEBUG_DBG("%s cca_sec2protkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(usp, &ksp, sizeof(ksp)))
			return -EFAULT;
		break;
	}
	case PKEY_CLR2PROTK: {
		struct pkey_clr2protk __user *ucp = (void __user *) arg;
		struct pkey_clr2protk kcp;

		if (copy_from_user(&kcp, ucp, sizeof(kcp)))
			return -EFAULT;
		rc = pkey_clr2protkey(kcp.keytype,
				      &kcp.clrkey, &kcp.protkey);
		DEBUG_DBG("%s pkey_clr2protkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ucp, &kcp, sizeof(kcp)))
			return -EFAULT;
		memzero_explicit(&kcp, sizeof(kcp));
		break;
	}
	case PKEY_FINDCARD: {
		struct pkey_findcard __user *ufc = (void __user *) arg;
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
		struct pkey_skey2pkey __user *usp = (void __user *) arg;
		struct pkey_skey2pkey ksp;

		if (copy_from_user(&ksp, usp, sizeof(ksp)))
			return -EFAULT;
		rc = pkey_skey2pkey(ksp.seckey.seckey, &ksp.protkey);
		DEBUG_DBG("%s pkey_skey2pkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(usp, &ksp, sizeof(ksp)))
			return -EFAULT;
		break;
	}
	case PKEY_VERIFYKEY: {
		struct pkey_verifykey __user *uvk = (void __user *) arg;
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
		struct pkey_genprotk __user *ugp = (void __user *) arg;
		struct pkey_genprotk kgp;

		if (copy_from_user(&kgp, ugp, sizeof(kgp)))
			return -EFAULT;
		rc = pkey_genprotkey(kgp.keytype, &kgp.protkey);
		DEBUG_DBG("%s pkey_genprotkey()=%d\n", __func__, rc);
		if (rc)
			break;
		if (copy_to_user(ugp, &kgp, sizeof(kgp)))
			return -EFAULT;
		break;
	}
	case PKEY_VERIFYPROTK: {
		struct pkey_verifyprotk __user *uvp = (void __user *) arg;
		struct pkey_verifyprotk kvp;

		if (copy_from_user(&kvp, uvp, sizeof(kvp)))
			return -EFAULT;
		rc = pkey_verifyprotkey(&kvp.protkey);
		DEBUG_DBG("%s pkey_verifyprotkey()=%d\n", __func__, rc);
		break;
	}
	case PKEY_KBLOB2PROTK: {
		struct pkey_kblob2pkey __user *utp = (void __user *) arg;
		struct pkey_kblob2pkey ktp;
		u8 *kkey;

		if (copy_from_user(&ktp, utp, sizeof(ktp)))
			return -EFAULT;
		kkey = _copy_key_from_user(ktp.key, ktp.keylen);
		if (IS_ERR(kkey))
			return PTR_ERR(kkey);
		rc = pkey_keyblob2pkey(kkey, ktp.keylen, &ktp.protkey);
		DEBUG_DBG("%s pkey_keyblob2pkey()=%d\n", __func__, rc);
		kfree(kkey);
		if (rc)
			break;
		if (copy_to_user(utp, &ktp, sizeof(ktp)))
			return -EFAULT;
		break;
	}
	case PKEY_GENSECK2: {
		struct pkey_genseck2 __user *ugs = (void __user *) arg;
		struct pkey_genseck2 kgs;
		struct pkey_apqn *apqns;
		size_t klen = KEYBLOBBUFSIZE;
		u8 *kkey;

		if (copy_from_user(&kgs, ugs, sizeof(kgs)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(kgs.apqns, kgs.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = kmalloc(klen, GFP_KERNEL);
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
		struct pkey_clr2seck2 __user *ucs = (void __user *) arg;
		struct pkey_clr2seck2 kcs;
		struct pkey_apqn *apqns;
		size_t klen = KEYBLOBBUFSIZE;
		u8 *kkey;

		if (copy_from_user(&kcs, ucs, sizeof(kcs)))
			return -EFAULT;
		apqns = _copy_apqns_from_user(kcs.apqns, kcs.apqn_entries);
		if (IS_ERR(apqns))
			return PTR_ERR(apqns);
		kkey = kmalloc(klen, GFP_KERNEL);
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
		struct pkey_verifykey2 __user *uvk = (void __user *) arg;
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
		struct pkey_kblob2pkey2 __user *utp = (void __user *) arg;
		struct pkey_kblob2pkey2 ktp;
		struct pkey_apqn *apqns = NULL;
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
		rc = pkey_keyblob2pkey2(apqns, ktp.apqn_entries,
					kkey, ktp.keylen, &ktp.protkey);
		DEBUG_DBG("%s pkey_keyblob2pkey2()=%d\n", __func__, rc);
		kfree(apqns);
		kfree(kkey);
		if (rc)
			break;
		if (copy_to_user(utp, &ktp, sizeof(ktp)))
			return -EFAULT;
		break;
	}
	case PKEY_APQNS4K: {
		struct pkey_apqns4key __user *uak = (void __user *) arg;
		struct pkey_apqns4key kak;
		struct pkey_apqn *apqns = NULL;
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
		struct pkey_apqns4keytype __user *uat = (void __user *) arg;
		struct pkey_apqns4keytype kat;
		struct pkey_apqn *apqns = NULL;
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

	rc = pkey_genprotkey(protkeytoken.keytype, &protkey);
	if (rc)
		return rc;

	protkeytoken.len = protkey.len;
	memcpy(&protkeytoken.protkey, &protkey.protkey, protkey.len);

	memcpy(buf, &protkeytoken, sizeof(protkeytoken));

	if (is_xts) {
		rc = pkey_genprotkey(protkeytoken.keytype, &protkey);
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
	int rc;
	struct pkey_seckey *seckey = (struct pkey_seckey *) buf;

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
	int i, rc, card, dom;
	u32 nr_apqns, *apqns = NULL;
	size_t keysize = CCACIPHERTOKENSIZE;

	if (off != 0 || count < CCACIPHERTOKENSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * CCACIPHERTOKENSIZE)
			return -EINVAL;

	/* build a list of apqns able to generate an cipher key */
	rc = cca_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			   ZCRYPT_CEX6, 0, 0, 0);
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
 * 320 bytes.
 */
static ssize_t pkey_ep11_aes_attr_read(enum pkey_key_size keybits,
				       bool is_xts, char *buf, loff_t off,
				       size_t count)
{
	int i, rc, card, dom;
	u32 nr_apqns, *apqns = NULL;
	size_t keysize = MAXEP11AESKEYBLOBSIZE;

	if (off != 0 || count < MAXEP11AESKEYBLOBSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * MAXEP11AESKEYBLOBSIZE)
			return -EINVAL;

	/* build a list of apqns able to generate an cipher key */
	rc = ep11_findcard2(&apqns, &nr_apqns, 0xFFFF, 0xFFFF,
			    ZCRYPT_CEX7, EP11_API_V, NULL);
	if (rc)
		return rc;

	memset(buf, 0, is_xts ? 2 * keysize : keysize);

	/* simple try all apqns from the list */
	for (i = 0, rc = -ENODEV; i < nr_apqns; i++) {
		card = apqns[i] >> 16;
		dom = apqns[i] & 0xFFFF;
		rc = ep11_genaeskey(card, dom, keybits, 0, buf, &keysize);
		if (rc == 0)
			break;
	}
	if (rc)
		return rc;

	if (is_xts) {
		keysize = MAXEP11AESKEYBLOBSIZE;
		buf += MAXEP11AESKEYBLOBSIZE;
		rc = ep11_genaeskey(card, dom, keybits, 0, buf, &keysize);
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
	cpacf_mask_t kmc_functions;

	/*
	 * The pckmo instruction should be available - even if we don't
	 * actually invoke it. This instruction comes with MSA 3 which
	 * is also the minimum level for the kmc instructions which
	 * are able to work with protected keys.
	 */
	if (!cpacf_query(CPACF_PCKMO, &pckmo_functions))
		return -ENODEV;

	/* check for kmc instructions available */
	if (!cpacf_query(CPACF_KMC, &kmc_functions))
		return -ENODEV;
	if (!cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_128) ||
	    !cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_192) ||
	    !cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_256))
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

module_cpu_feature_match(MSA, pkey_init);
module_exit(pkey_exit);
