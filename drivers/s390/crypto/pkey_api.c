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
#include <linux/cpufeature.h>
#include <asm/zcrypt.h>
#include <asm/cpacf.h>
#include <asm/pkey.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key interface");

/*
 * Debug feature data and functions
 */

debug_info_t *pkey_dbf_info;

static void __init pkey_debug_init(void)
{
	/* 5 arguments per dbf entry (including the format string ptr) */
	pkey_dbf_info = debug_register("pkey", 1, 1, 5 * sizeof(long));
	debug_register_view(pkey_dbf_info, &debug_sprintf_view);
	debug_set_level(pkey_dbf_info, 3);
}

static void __exit pkey_debug_exit(void)
{
	debug_unregister(pkey_dbf_info);
}

/*
 * Helper functions
 */

static int apqns4key(const u8 *key, size_t keylen, u32 flags,
		     struct pkey_apqn *apqns, size_t *nr_apqns)
{
	if (pkey_is_cca_key(key, keylen)) {
		return pkey_cca_apqns4key(key, keylen, flags,
					  apqns, nr_apqns);
	} else if (pkey_is_ep11_key(key, keylen)) {
		return pkey_ep11_apqns4key(key, keylen, flags,
					   apqns, nr_apqns);
	} else {
		struct keytoken_header *hdr = (struct keytoken_header *)key;

		PKEY_DBF_ERR("%s unknown/unsupported key type %d version %d\n",
			     __func__, hdr->type, hdr->version);
		return -EINVAL;
	}
}

static int apqns4keytype(enum pkey_key_type ktype,
			 u8 cur_mkvp[32], u8 alt_mkvp[32], u32 flags,
			 struct pkey_apqn *apqns, size_t *nr_apqns)
{
	if (pkey_is_cca_keytype(ktype)) {
		return pkey_cca_apqns4type(ktype, cur_mkvp, alt_mkvp, flags,
					   apqns, nr_apqns);
	} else if (pkey_is_ep11_keytype(ktype)) {
		return pkey_ep11_apqns4type(ktype, cur_mkvp, alt_mkvp, flags,
					    apqns, nr_apqns);
	} else {
		PKEY_DBF_ERR("%s unknown/unsupported key type %d\n",
			     __func__, ktype);
		return -EINVAL;
	}
}

static int genseck2(const struct pkey_apqn *apqns, size_t nr_apqns,
		    enum pkey_key_type keytype, enum pkey_key_size keybitsize,
		    u32 flags, u8 *keybuf, u32 *keybuflen)
{
	int i, rc;
	u32 u;

	if (pkey_is_cca_keytype(keytype)) {
		/* As of now only CCA AES key generation is supported */
		u = pkey_aes_bitsize_to_keytype(keybitsize);
		if (!u) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, keybitsize);
			return -EINVAL;
		}
		for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
			rc = pkey_cca_gen_key(apqns[i].card,
					      apqns[i].domain,
					      u, keytype, keybitsize, flags,
					      keybuf, keybuflen, NULL);
		}
	} else if (pkey_is_ep11_keytype(keytype)) {
		/* As of now only EP11 AES key generation is supported */
		u = pkey_aes_bitsize_to_keytype(keybitsize);
		if (!u) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, keybitsize);
			return -EINVAL;
		}
		for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
			rc = pkey_ep11_gen_key(apqns[i].card,
					       apqns[i].domain,
					       u, keytype, keybitsize, flags,
					       keybuf, keybuflen, NULL);
		}
	} else {
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n",
			     __func__, keytype);
		return -EINVAL;
	}

	return rc;
}

static int clr2seckey2(const struct pkey_apqn *apqns, size_t nr_apqns,
		       enum pkey_key_type keytype, enum pkey_key_size kbitsize,
		       u32 flags, const u8 *clrkey, u8 *keybuf, u32 *keybuflen)
{
	int i, rc;
	u32 u;

	if (pkey_is_cca_keytype(keytype)) {
		/* As of now only CCA AES key generation is supported */
		u = pkey_aes_bitsize_to_keytype(kbitsize);
		if (!u) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, kbitsize);
			return -EINVAL;
		}
		for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
			rc = pkey_cca_clr2key(apqns[i].card,
					      apqns[i].domain,
					      u, keytype, kbitsize, flags,
					      clrkey, kbitsize / 8,
					      keybuf, keybuflen, NULL);
		}
	} else if (pkey_is_ep11_keytype(keytype)) {
		/* As of now only EP11 AES key generation is supported */
		u = pkey_aes_bitsize_to_keytype(kbitsize);
		if (!u) {
			PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
				     __func__, kbitsize);
			return -EINVAL;
		}
		for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
			rc = pkey_ep11_clr2key(apqns[i].card,
					       apqns[i].domain,
					       u, keytype, kbitsize, flags,
					       clrkey, kbitsize / 8,
					       keybuf, keybuflen, NULL);
		}
	} else {
		PKEY_DBF_ERR("%s unknown/unsupported keytype %d\n",
			     __func__, keytype);
		return -EINVAL;
	}

	return rc;
}

static int ccakey2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
			  const u8 *key, size_t keylen,
			  u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct pkey_apqn *local_apqns = NULL;
	int i, j, rc;

	/* alloc space for list of apqns if no list given */
	if (!apqns || (nr_apqns == 1 &&
		       apqns[0].card == 0xFFFF && apqns[0].domain == 0xFFFF)) {
		nr_apqns = MAXAPQNSINLIST;
		local_apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn),
					    GFP_KERNEL);
		if (!local_apqns)
			return -ENOMEM;
		apqns = local_apqns;
	}

	/* try two times in case of failure */
	for (i = 0, rc = -ENODEV; i < 2 && rc; i++) {
		if (local_apqns) {
			/* gather list of apqns able to deal with this key */
			nr_apqns = MAXAPQNSINLIST;
			rc = pkey_cca_apqns4key(key, keylen, 0,
						local_apqns, &nr_apqns);
			if (rc)
				continue;
		}
		/* go through the list of apqns until success or end */
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			rc = pkey_cca_key2protkey(apqns[j].card,
						  apqns[j].domain,
						  key, keylen,
						  protkey, protkeylen,
						  protkeytype);
		}
	}

	kfree(local_apqns);

	return rc;
}

static int ep11key2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
			   const u8 *key, size_t keylen,
			   u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	struct pkey_apqn *local_apqns = NULL;
	int i, j, rc;

	/* alloc space for list of apqns if no list given */
	if (!apqns || (nr_apqns == 1 &&
		       apqns[0].card == 0xFFFF && apqns[0].domain == 0xFFFF)) {
		nr_apqns = MAXAPQNSINLIST;
		local_apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn),
					    GFP_KERNEL);
		if (!local_apqns)
			return -ENOMEM;
		apqns = local_apqns;
	}

	/* try two times in case of failure */
	for (i = 0, rc = -ENODEV; i < 2 && rc; i++) {
		if (local_apqns) {
			/* gather list of apqns able to deal with this key */
			nr_apqns = MAXAPQNSINLIST;
			rc = pkey_ep11_apqns4key(key, keylen, 0,
						 local_apqns, &nr_apqns);
			if (rc)
				continue;
		}
		/* go through the list of apqns until success or end */
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			rc = pkey_ep11_key2protkey(apqns[j].card,
						   apqns[j].domain,
						   key, keylen,
						   protkey, protkeylen,
						   protkeytype);
		}
	}

	kfree(local_apqns);

	return rc;
}

static int pckmokey2protkey_fallback(const struct clearkeytoken *t,
				     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	size_t tmpbuflen = max_t(size_t, SECKEYBLOBSIZE, MAXEP11AESKEYBLOBSIZE);
	struct pkey_apqn *apqns = NULL;
	u32 keysize, tmplen;
	u8 *tmpbuf = NULL;
	size_t nr_apqns;
	int i, j, rc;

	/* As of now only for AES keys a fallback is available */

	keysize = pkey_keytype_aes_to_size(t->keytype);
	if (!keysize) {
		PKEY_DBF_ERR("%s unknown/unsupported keytype %u\n",
			     __func__, t->keytype);
		return -EINVAL;
	}
	if (t->len != keysize) {
		PKEY_DBF_ERR("%s clear key AES token: invalid key len %u\n",
			     __func__, t->len);
		return -EINVAL;
	}

	/* alloc tmp buffer and space for apqns */
	tmpbuf = kmalloc(tmpbuflen, GFP_ATOMIC);
	if (!tmpbuf)
		return -ENOMEM;
	nr_apqns = MAXAPQNSINLIST;
	apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn), GFP_KERNEL);
	if (!apqns) {
		kfree(tmpbuf);
		return -ENOMEM;
	}

	/* try two times in case of failure */
	for (i = 0, rc = -ENODEV; i < 2 && rc; i++) {

		/* CCA secure key way */
		nr_apqns = MAXAPQNSINLIST;
		rc = pkey_cca_apqns4type(PKEY_TYPE_CCA_DATA,
					 NULL, NULL, 0, apqns, &nr_apqns);
		pr_debug("pkey_cca_apqns4type(CCA_DATA)=%d\n", rc);
		if (rc)
			goto try_via_ep11;
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			tmplen = tmpbuflen;
			rc = pkey_cca_clr2key(apqns[j].card, apqns[j].domain,
					      t->keytype, PKEY_TYPE_CCA_DATA,
					      8 * keysize, 0,
					      t->clearkey, t->len,
					      tmpbuf, &tmplen, NULL);
			pr_debug("pkey_cca_clr2key()=%d\n", rc);
		}
		if (rc)
			goto try_via_ep11;
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			rc = pkey_cca_key2protkey(apqns[j].card,
						  apqns[j].domain,
						  tmpbuf, tmplen,
						  protkey, protkeylen,
						  protkeytype);
			pr_debug("pkey_cca_key2protkey()=%d\n", rc);
		}
		if (!rc)
			break;

try_via_ep11:
		/* the CCA way failed, try via EP11 */
		nr_apqns = MAXAPQNSINLIST;
		rc = pkey_ep11_apqns4type(PKEY_TYPE_EP11_AES,
					  NULL, NULL, 0, apqns, &nr_apqns);
		pr_debug("pkey_ep11_apqns4type(EP11_AES)=%d\n", rc);
		if (rc)
			continue;
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			tmplen = tmpbuflen;
			rc = pkey_ep11_clr2key(apqns[j].card, apqns[j].domain,
					       t->keytype, PKEY_TYPE_EP11_AES,
					       8 * keysize, 0,
					       t->clearkey, t->len,
					       tmpbuf, &tmplen, NULL);
			pr_debug("pkey_ep11_clr2key()=%d\n", rc);
		}
		if (rc)
			continue;
		for (j = 0, rc = -ENODEV; j < nr_apqns && rc; j++) {
			rc = pkey_ep11_key2protkey(apqns[j].card,
						   apqns[j].domain,
						   tmpbuf, tmplen,
						   protkey, protkeylen,
						   protkeytype);
			pr_debug("pkey_ep11_key2protkey()=%d\n", rc);
		}
	}

	kfree(tmpbuf);
	kfree(apqns);

	return rc;
}

static int pckmokey2protkey(const u8 *key, size_t keylen,
			    u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	int rc;

	rc = pkey_pckmo_key2protkey(0, 0, key, keylen,
				    protkey, protkeylen, protkeytype);
	if (rc == -ENODEV) {
		struct keytoken_header *hdr = (struct keytoken_header *)key;
		struct clearkeytoken *t = (struct clearkeytoken *)key;

		/* maybe a fallback is possible */
		if (hdr->type == TOKTYPE_NON_CCA &&
		    hdr->version == TOKVER_CLEAR_KEY) {
			rc = pckmokey2protkey_fallback(t, protkey,
						       protkeylen,
						       protkeytype);
			if (rc)
				rc = -ENODEV;
		}
	}

	if (rc)
		PKEY_DBF_ERR("%s unable to build protected key from clear, rc=%d",
			     __func__, rc);

	return rc;
}

static int key2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
		       const u8 *key, size_t keylen,
		       u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	if (pkey_is_cca_key(key, keylen)) {
		return ccakey2protkey(apqns, nr_apqns, key, keylen,
				      protkey, protkeylen, protkeytype);
	} else if (pkey_is_ep11_key(key, keylen)) {
		return ep11key2protkey(apqns, nr_apqns, key, keylen,
				       protkey, protkeylen, protkeytype);
	} else if (pkey_is_pckmo_key(key, keylen)) {
		return pckmokey2protkey(key, keylen,
					protkey, protkeylen, protkeytype);
	} else {
		struct keytoken_header *hdr = (struct keytoken_header *)key;

		PKEY_DBF_ERR("%s unknown/unsupported key type %d version %d\n",
			     __func__, hdr->type, hdr->version);
		return -EINVAL;
	}
}

/*
 * In-Kernel function: Transform a key blob (of any type) into a protected key
 */
int pkey_key2protkey(const u8 *key, u32 keylen,
		     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	return key2protkey(NULL, 0, key, keylen,
			   protkey, protkeylen, protkeytype);
}
EXPORT_SYMBOL(pkey_key2protkey);

/*
 * Ioctl functions
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

static int pkey_ioctl_genseck(struct pkey_genseck __user *ugs)
{
	struct pkey_genseck kgs;
	u32 keybuflen;
	int rc;

	if (copy_from_user(&kgs, ugs, sizeof(kgs)))
		return -EFAULT;
	keybuflen = sizeof(kgs.seckey.seckey);
	rc = pkey_cca_gen_key(kgs.cardnr, kgs.domain,
			      kgs.keytype, PKEY_TYPE_CCA_DATA, 0, 0,
			      kgs.seckey.seckey, &keybuflen, NULL);
	pr_debug("pkey_cca_gen_key()=%d\n", rc);
	if (!rc && copy_to_user(ugs, &kgs, sizeof(kgs)))
		rc = -EFAULT;
	memzero_explicit(&kgs, sizeof(kgs));

	return rc;
}

static int pkey_ioctl_clr2seck(struct pkey_clr2seck __user *ucs)
{
	struct pkey_clr2seck kcs;
	u32 keybuflen;
	int rc;

	if (copy_from_user(&kcs, ucs, sizeof(kcs)))
		return -EFAULT;
	keybuflen = sizeof(kcs.seckey.seckey);
	rc = pkey_cca_clr2key(kcs.cardnr, kcs.domain,
			      kcs.keytype, PKEY_TYPE_CCA_DATA, 0, 0,
			      kcs.clrkey.clrkey,
			      pkey_keytype_aes_to_size(kcs.keytype),
			      kcs.seckey.seckey, &keybuflen, NULL);
	pr_debug("pkey_cca_clr2key()=%d\n", rc);
	if (!rc && copy_to_user(ucs, &kcs, sizeof(kcs)))
		rc = -EFAULT;
	memzero_explicit(&kcs, sizeof(kcs));

	return rc;
}

static int pkey_ioctl_sec2protk(struct pkey_sec2protk __user *usp)
{
	struct pkey_sec2protk ksp;
	int rc;

	if (copy_from_user(&ksp, usp, sizeof(ksp)))
		return -EFAULT;
	ksp.protkey.len = sizeof(ksp.protkey.protkey);
	rc = pkey_cca_key2protkey(ksp.cardnr, ksp.domain,
				  ksp.seckey.seckey, sizeof(ksp.seckey.seckey),
				  ksp.protkey.protkey,
				  &ksp.protkey.len, &ksp.protkey.type);
	pr_debug("pkey_cca_key2protkey()=%d\n", rc);
	if (!rc && copy_to_user(usp, &ksp, sizeof(ksp)))
		rc = -EFAULT;
	memzero_explicit(&ksp, sizeof(ksp));

	return rc;
}

static int pkey_ioctl_clr2protk(struct pkey_clr2protk __user *ucp)
{
	struct pkey_clr2protk kcp;
	int rc;

	if (copy_from_user(&kcp, ucp, sizeof(kcp)))
		return -EFAULT;
	kcp.protkey.len = sizeof(kcp.protkey.protkey);
	rc = pkey_pckmo_clr2key(0, 0, kcp.keytype, 0, 0, 0,
				kcp.clrkey.clrkey, 0,
				kcp.protkey.protkey,
				&kcp.protkey.len, &kcp.protkey.type);
	pr_debug("pkey_pckmo_clr2key()=%d\n", rc);
	if (!rc && copy_to_user(ucp, &kcp, sizeof(kcp)))
		rc = -EFAULT;
	memzero_explicit(&kcp, sizeof(kcp));

	return rc;
}

static int pkey_ioctl_findcard(struct pkey_findcard __user *ufc)
{
	struct pkey_findcard kfc;
	struct pkey_apqn *apqns;
	size_t nr_apqns;
	int rc;

	if (copy_from_user(&kfc, ufc, sizeof(kfc)))
		return -EFAULT;

	if (!pkey_is_cca_key(kfc.seckey.seckey, sizeof(kfc.seckey.seckey)))
		return -EINVAL;

	nr_apqns = MAXAPQNSINLIST;
	apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn), GFP_KERNEL);
	if (!apqns)
		return -ENOMEM;
	rc = pkey_cca_apqns4key(kfc.seckey.seckey,
				sizeof(kfc.seckey.seckey),
				PKEY_FLAGS_MATCH_CUR_MKVP,
				apqns, &nr_apqns);
	if (rc == -ENODEV)
		rc = pkey_cca_apqns4key(kfc.seckey.seckey,
					sizeof(kfc.seckey.seckey),
					PKEY_FLAGS_MATCH_ALT_MKVP,
					apqns, &nr_apqns);
	pr_debug("pkey_cca_apqns4key()=%d\n", rc);
	if (rc) {
		kfree(apqns);
		return rc;
	}
	kfc.cardnr = apqns[0].card;
	kfc.domain = apqns[0].domain;
	kfree(apqns);
	if (copy_to_user(ufc, &kfc, sizeof(kfc)))
		return -EFAULT;

	return 0;
}

static int pkey_ioctl_skey2pkey(struct pkey_skey2pkey __user *usp)
{
	struct pkey_skey2pkey ksp;
	struct pkey_apqn *apqns;
	size_t nr_apqns;
	int i, rc;

	if (copy_from_user(&ksp, usp, sizeof(ksp)))
		return -EFAULT;

	if (!pkey_is_cca_key(ksp.seckey.seckey, sizeof(ksp.seckey.seckey)))
		return -EINVAL;

	nr_apqns = MAXAPQNSINLIST;
	apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn), GFP_KERNEL);
	if (!apqns)
		return -ENOMEM;
	rc = pkey_cca_apqns4key(ksp.seckey.seckey, sizeof(ksp.seckey.seckey),
				0, apqns, &nr_apqns);
	pr_debug("pkey_cca_apqns4key()=%d\n", rc);
	if (rc) {
		kfree(apqns);
		return rc;
	}
	ksp.protkey.len = sizeof(ksp.protkey.protkey);
	for (rc = -ENODEV, i = 0; rc && i < nr_apqns; i++) {
		rc = pkey_cca_key2protkey(apqns[i].card, apqns[i].domain,
					  ksp.seckey.seckey,
					  sizeof(ksp.seckey.seckey),
					  ksp.protkey.protkey,
					  &ksp.protkey.len,
					  &ksp.protkey.type);
		pr_debug("pkey_cca_key2protkey()=%d\n", rc);
	}
	kfree(apqns);
	if (!rc && copy_to_user(usp, &ksp, sizeof(ksp)))
		rc = -EFAULT;
	memzero_explicit(&ksp, sizeof(ksp));

	return rc;
}

static int pkey_ioctl_verifykey(struct pkey_verifykey __user *uvk)
{
	u32 keytype, keybitsize, flags;
	struct pkey_verifykey kvk;
	int rc;

	if (copy_from_user(&kvk, uvk, sizeof(kvk)))
		return -EFAULT;
	kvk.cardnr = 0xFFFF;
	kvk.domain = 0xFFFF;
	rc = pkey_cca_verifykey(kvk.seckey.seckey, sizeof(kvk.seckey.seckey),
				&kvk.cardnr, &kvk.domain,
				&keytype, &keybitsize, &flags);
	pr_debug("pkey_cca_verifykey()=%d\n", rc);
	if (!rc && keytype != PKEY_TYPE_CCA_DATA)
		rc = -EINVAL;
	kvk.attributes = PKEY_VERIFY_ATTR_AES;
	kvk.keysize = (u16)keybitsize;
	if (flags & PKEY_FLAGS_MATCH_ALT_MKVP)
		kvk.attributes |= PKEY_VERIFY_ATTR_OLD_MKVP;
	if (!rc && copy_to_user(uvk, &kvk, sizeof(kvk)))
		rc = -EFAULT;
	memzero_explicit(&kvk, sizeof(kvk));

	return rc;
}

static int pkey_ioctl_genprotk(struct pkey_genprotk __user *ugp)
{
	struct pkey_genprotk kgp;
	int rc;

	if (copy_from_user(&kgp, ugp, sizeof(kgp)))
		return -EFAULT;
	kgp.protkey.len = sizeof(kgp.protkey.protkey);
	rc = pkey_pckmo_gen_key(0, 0, kgp.keytype, 0, 0, 0,
				kgp.protkey.protkey,
				&kgp.protkey.len, &kgp.protkey.type);
	pr_debug("pkey_pckmo_gen_key()=%d\n", rc);
	if (!rc && copy_to_user(ugp, &kgp, sizeof(kgp)))
		rc = -EFAULT;
	memzero_explicit(&kgp, sizeof(kgp));

	return rc;
}

static int pkey_ioctl_verifyprotk(struct pkey_verifyprotk __user *uvp)
{
	struct pkey_verifyprotk kvp;
	int rc;

	if (copy_from_user(&kvp, uvp, sizeof(kvp)))
		return -EFAULT;
	rc = pkey_pckmo_verifykey(kvp.protkey.protkey, kvp.protkey.len,
				  0, 0, &kvp.protkey.type, 0, 0);
	pr_debug("pkey_pckmo_verifykey()=%d\n", rc);
	memzero_explicit(&kvp, sizeof(kvp));

	return rc;
}

static int pkey_ioctl_kblob2protk(struct pkey_kblob2pkey __user *utp)
{
	struct pkey_kblob2pkey ktp;
	u8 *kkey;
	int rc;

	if (copy_from_user(&ktp, utp, sizeof(ktp)))
		return -EFAULT;
	kkey = _copy_key_from_user(ktp.key, ktp.keylen);
	if (IS_ERR(kkey))
		return PTR_ERR(kkey);
	ktp.protkey.len = sizeof(ktp.protkey.protkey);
	rc = key2protkey(NULL, 0, kkey, ktp.keylen,
			 ktp.protkey.protkey, &ktp.protkey.len,
			 &ktp.protkey.type);
	pr_debug("key2protkey()=%d\n", rc);
	kfree_sensitive(kkey);
	if (!rc && copy_to_user(utp, &ktp, sizeof(ktp)))
		rc = -EFAULT;
	memzero_explicit(&ktp, sizeof(ktp));

	return rc;
}

static int pkey_ioctl_genseck2(struct pkey_genseck2 __user *ugs)
{
	u32 klen = KEYBLOBBUFSIZE;
	struct pkey_genseck2 kgs;
	struct pkey_apqn *apqns;
	u8 *kkey;
	int rc;

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
	rc = genseck2(apqns, kgs.apqn_entries,
		      kgs.type, kgs.size, kgs.keygenflags,
		      kkey, &klen);
	pr_debug("genseckey2()=%d\n", rc);
	kfree(apqns);
	if (rc) {
		kfree_sensitive(kkey);
		return rc;
	}
	if (kgs.key) {
		if (kgs.keylen < klen) {
			kfree_sensitive(kkey);
			return -EINVAL;
		}
		if (copy_to_user(kgs.key, kkey, klen)) {
			kfree_sensitive(kkey);
			return -EFAULT;
		}
	}
	kgs.keylen = klen;
	if (copy_to_user(ugs, &kgs, sizeof(kgs)))
		rc = -EFAULT;
	kfree_sensitive(kkey);

	return rc;
}

static int pkey_ioctl_clr2seck2(struct pkey_clr2seck2 __user *ucs)
{
	u32 klen = KEYBLOBBUFSIZE;
	struct pkey_clr2seck2 kcs;
	struct pkey_apqn *apqns;
	u8 *kkey;
	int rc;

	if (copy_from_user(&kcs, ucs, sizeof(kcs)))
		return -EFAULT;
	apqns = _copy_apqns_from_user(kcs.apqns, kcs.apqn_entries);
	if (IS_ERR(apqns)) {
		memzero_explicit(&kcs, sizeof(kcs));
		return PTR_ERR(apqns);
	}
	kkey = kzalloc(klen, GFP_KERNEL);
	if (!kkey) {
		kfree(apqns);
		memzero_explicit(&kcs, sizeof(kcs));
		return -ENOMEM;
	}
	rc = clr2seckey2(apqns, kcs.apqn_entries,
			 kcs.type, kcs.size, kcs.keygenflags,
			 kcs.clrkey.clrkey, kkey, &klen);
	pr_debug("clr2seckey2()=%d\n", rc);
	kfree(apqns);
	if (rc) {
		kfree_sensitive(kkey);
		memzero_explicit(&kcs, sizeof(kcs));
		return rc;
	}
	if (kcs.key) {
		if (kcs.keylen < klen) {
			kfree_sensitive(kkey);
			memzero_explicit(&kcs, sizeof(kcs));
			return -EINVAL;
		}
		if (copy_to_user(kcs.key, kkey, klen)) {
			kfree_sensitive(kkey);
			memzero_explicit(&kcs, sizeof(kcs));
			return -EFAULT;
		}
	}
	kcs.keylen = klen;
	if (copy_to_user(ucs, &kcs, sizeof(kcs)))
		rc = -EFAULT;
	memzero_explicit(&kcs, sizeof(kcs));
	kfree_sensitive(kkey);

	return rc;
}

static int pkey_ioctl_verifykey2(struct pkey_verifykey2 __user *uvk)
{
	struct pkey_verifykey2 kvk;
	u8 *kkey;
	int rc;

	if (copy_from_user(&kvk, uvk, sizeof(kvk)))
		return -EFAULT;
	kkey = _copy_key_from_user(kvk.key, kvk.keylen);
	if (IS_ERR(kkey))
		return PTR_ERR(kkey);
	if (pkey_is_cca_key(kkey, kvk.keylen)) {
		rc = pkey_cca_verifykey(kkey, kvk.keylen,
					&kvk.cardnr, &kvk.domain,
					&kvk.type, &kvk.size, &kvk.flags);
		pr_debug("pkey_cca_verifykey()=%d\n", rc);
	} else if (pkey_is_ep11_key(kkey, kvk.keylen)) {
		rc = pkey_ep11_verifykey(kkey, kvk.keylen,
					 &kvk.cardnr, &kvk.domain,
					 &kvk.type, &kvk.size, &kvk.flags);
		pr_debug("pkey_ep11_verifykey()=%d\n", rc);
	} else {
		rc = -EINVAL;
	}
	kfree_sensitive(kkey);
	if (rc)
		return rc;
	if (copy_to_user(uvk, &kvk, sizeof(kvk)))
		return -EFAULT;

	return 0;
}

static int pkey_ioctl_kblob2protk2(struct pkey_kblob2pkey2 __user *utp)
{
	struct pkey_apqn *apqns = NULL;
	struct pkey_kblob2pkey2 ktp;
	u8 *kkey;
	int rc;

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
	rc = key2protkey(apqns, ktp.apqn_entries, kkey, ktp.keylen,
			 ktp.protkey.protkey, &ktp.protkey.len,
			 &ktp.protkey.type);
	pr_debug("key2protkey()=%d\n", rc);
	kfree(apqns);
	kfree_sensitive(kkey);
	if (!rc && copy_to_user(utp, &ktp, sizeof(ktp)))
		rc = -EFAULT;
	memzero_explicit(&ktp, sizeof(ktp));

	return rc;
}

static int pkey_ioctl_apqns4k(struct pkey_apqns4key __user *uak)
{
	struct pkey_apqn *apqns = NULL;
	struct pkey_apqns4key kak;
	size_t nr_apqns, len;
	u8 *kkey;
	int rc;

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
	rc = apqns4key(kkey, kak.keylen, kak.flags,
		       apqns, &nr_apqns);
	pr_debug("apqns4key()=%d\n", rc);
	kfree_sensitive(kkey);
	if (rc && rc != -ENOSPC) {
		kfree(apqns);
		return rc;
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

	return rc;
}

static int pkey_ioctl_apqns4kt(struct pkey_apqns4keytype __user *uat)
{
	struct pkey_apqn *apqns = NULL;
	struct pkey_apqns4keytype kat;
	size_t nr_apqns, len;
	int rc;

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
	rc = apqns4keytype(kat.type, kat.cur_mkvp, kat.alt_mkvp,
			   kat.flags, apqns, &nr_apqns);
	pr_debug("apqns4keytype()=%d\n", rc);
	if (rc && rc != -ENOSPC) {
		kfree(apqns);
		return rc;
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

	return rc;
}

static int pkey_ioctl_kblob2protk3(struct pkey_kblob2pkey3 __user *utp)
{
	u32 protkeylen = PROTKEYBLOBBUFSIZE;
	struct pkey_apqn *apqns = NULL;
	struct pkey_kblob2pkey3 ktp;
	u8 *kkey, *protkey;
	int rc;

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
		kfree_sensitive(kkey);
		return -ENOMEM;
	}
	rc = key2protkey(apqns, ktp.apqn_entries, kkey, ktp.keylen,
			 protkey, &protkeylen, &ktp.pkeytype);
	pr_debug("key2protkey()=%d\n", rc);
	kfree(apqns);
	kfree_sensitive(kkey);
	if (rc) {
		kfree_sensitive(protkey);
		return rc;
	}
	if (ktp.pkey && ktp.pkeylen) {
		if (protkeylen > ktp.pkeylen) {
			kfree_sensitive(protkey);
			return -EINVAL;
		}
		if (copy_to_user(ktp.pkey, protkey, protkeylen)) {
			kfree_sensitive(protkey);
			return -EFAULT;
		}
	}
	kfree_sensitive(protkey);
	ktp.pkeylen = protkeylen;
	if (copy_to_user(utp, &ktp, sizeof(ktp)))
		return -EFAULT;

	return 0;
}

static long pkey_unlocked_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	int rc;

	switch (cmd) {
	case PKEY_GENSECK:
		rc = pkey_ioctl_genseck((struct pkey_genseck __user *)arg);
		break;
	case PKEY_CLR2SECK:
		rc = pkey_ioctl_clr2seck((struct pkey_clr2seck __user *)arg);
		break;
	case PKEY_SEC2PROTK:
		rc = pkey_ioctl_sec2protk((struct pkey_sec2protk __user *)arg);
		break;
	case PKEY_CLR2PROTK:
		rc = pkey_ioctl_clr2protk((struct pkey_clr2protk __user *)arg);
		break;
	case PKEY_FINDCARD:
		rc = pkey_ioctl_findcard((struct pkey_findcard __user *)arg);
		break;
	case PKEY_SKEY2PKEY:
		rc = pkey_ioctl_skey2pkey((struct pkey_skey2pkey __user *)arg);
		break;
	case PKEY_VERIFYKEY:
		rc = pkey_ioctl_verifykey((struct pkey_verifykey __user *)arg);
		break;
	case PKEY_GENPROTK:
		rc = pkey_ioctl_genprotk((struct pkey_genprotk __user *)arg);
		break;
	case PKEY_VERIFYPROTK:
		rc = pkey_ioctl_verifyprotk((struct pkey_verifyprotk __user *)arg);
		break;
	case PKEY_KBLOB2PROTK:
		rc = pkey_ioctl_kblob2protk((struct pkey_kblob2pkey __user *)arg);
		break;
	case PKEY_GENSECK2:
		rc = pkey_ioctl_genseck2((struct pkey_genseck2 __user *)arg);
		break;
	case PKEY_CLR2SECK2:
		rc = pkey_ioctl_clr2seck2((struct pkey_clr2seck2 __user *)arg);
		break;
	case PKEY_VERIFYKEY2:
		rc = pkey_ioctl_verifykey2((struct pkey_verifykey2 __user *)arg);
		break;
	case PKEY_KBLOB2PROTK2:
		rc = pkey_ioctl_kblob2protk2((struct pkey_kblob2pkey2 __user *)arg);
		break;
	case PKEY_APQNS4K:
		rc = pkey_ioctl_apqns4k((struct pkey_apqns4key __user *)arg);
		break;
	case PKEY_APQNS4KT:
		rc = pkey_ioctl_apqns4kt((struct pkey_apqns4keytype __user *)arg);
		break;
	case PKEY_KBLOB2PROTK3:
		rc = pkey_ioctl_kblob2protk3((struct pkey_kblob2pkey3 __user *)arg);
		break;
	default:
		/* unknown/unsupported ioctl cmd */
		return -ENOTTY;
	}

	return rc;
}

/*
 * File io operations
 */

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
