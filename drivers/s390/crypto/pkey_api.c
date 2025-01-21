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

#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"

#include "pkey_base.h"

/*
 * Helper functions
 */
static int key2protkey(const struct pkey_apqn *apqns, size_t nr_apqns,
		       const u8 *key, size_t keylen,
		       u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	int rc;

	/* try the direct way */
	rc = pkey_handler_key_to_protkey(apqns, nr_apqns,
					 key, keylen,
					 protkey, protkeylen,
					 protkeytype);

	/* if this did not work, try the slowpath way */
	if (rc == -ENODEV) {
		rc = pkey_handler_slowpath_key_to_protkey(apqns, nr_apqns,
							  key, keylen,
							  protkey, protkeylen,
							  protkeytype);
		if (rc)
			rc = -ENODEV;
	}

	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * In-Kernel function: Transform a key blob (of any type) into a protected key
 */
int pkey_key2protkey(const u8 *key, u32 keylen,
		     u8 *protkey, u32 *protkeylen, u32 *protkeytype)
{
	int rc;

	rc = key2protkey(NULL, 0, key, keylen,
			 protkey, protkeylen, protkeytype);
	if (rc == -ENODEV) {
		pkey_handler_request_modules();
		rc = key2protkey(NULL, 0, key, keylen,
				 protkey, protkeylen, protkeytype);
	}

	return rc;
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
	struct pkey_apqn apqn;
	u32 keybuflen;
	int rc;

	if (copy_from_user(&kgs, ugs, sizeof(kgs)))
		return -EFAULT;

	apqn.card = kgs.cardnr;
	apqn.domain = kgs.domain;
	keybuflen = sizeof(kgs.seckey.seckey);
	rc = pkey_handler_gen_key(&apqn, 1,
				  kgs.keytype, PKEY_TYPE_CCA_DATA, 0, 0,
				  kgs.seckey.seckey, &keybuflen, NULL);
	pr_debug("gen_key()=%d\n", rc);
	if (!rc && copy_to_user(ugs, &kgs, sizeof(kgs)))
		rc = -EFAULT;
	memzero_explicit(&kgs, sizeof(kgs));

	return rc;
}

static int pkey_ioctl_clr2seck(struct pkey_clr2seck __user *ucs)
{
	struct pkey_clr2seck kcs;
	struct pkey_apqn apqn;
	u32 keybuflen;
	int rc;

	if (copy_from_user(&kcs, ucs, sizeof(kcs)))
		return -EFAULT;

	apqn.card = kcs.cardnr;
	apqn.domain = kcs.domain;
	keybuflen = sizeof(kcs.seckey.seckey);
	rc = pkey_handler_clr_to_key(&apqn, 1,
				     kcs.keytype, PKEY_TYPE_CCA_DATA, 0, 0,
				     kcs.clrkey.clrkey,
				     pkey_keytype_aes_to_size(kcs.keytype),
				     kcs.seckey.seckey, &keybuflen, NULL);
	pr_debug("clr_to_key()=%d\n", rc);
	if (!rc && copy_to_user(ucs, &kcs, sizeof(kcs)))
		rc = -EFAULT;
	memzero_explicit(&kcs, sizeof(kcs));

	return rc;
}

static int pkey_ioctl_sec2protk(struct pkey_sec2protk __user *usp)
{
	struct pkey_sec2protk ksp;
	struct pkey_apqn apqn;
	int rc;

	if (copy_from_user(&ksp, usp, sizeof(ksp)))
		return -EFAULT;

	apqn.card = ksp.cardnr;
	apqn.domain = ksp.domain;
	ksp.protkey.len = sizeof(ksp.protkey.protkey);
	rc = pkey_handler_key_to_protkey(&apqn, 1,
					 ksp.seckey.seckey,
					 sizeof(ksp.seckey.seckey),
					 ksp.protkey.protkey,
					 &ksp.protkey.len, &ksp.protkey.type);
	pr_debug("key_to_protkey()=%d\n", rc);
	if (!rc && copy_to_user(usp, &ksp, sizeof(ksp)))
		rc = -EFAULT;
	memzero_explicit(&ksp, sizeof(ksp));

	return rc;
}

static int pkey_ioctl_clr2protk(struct pkey_clr2protk __user *ucp)
{
	struct pkey_clr2protk kcp;
	struct clearkeytoken *t;
	u32 keylen;
	u8 *tmpbuf;
	int rc;

	if (copy_from_user(&kcp, ucp, sizeof(kcp)))
		return -EFAULT;

	/* build a 'clear key token' from the clear key value */
	keylen = pkey_keytype_aes_to_size(kcp.keytype);
	if (!keylen) {
		PKEY_DBF_ERR("%s unknown/unsupported keytype %u\n",
			     __func__, kcp.keytype);
		memzero_explicit(&kcp, sizeof(kcp));
		return -EINVAL;
	}
	tmpbuf = kzalloc(sizeof(*t) + keylen, GFP_KERNEL);
	if (!tmpbuf) {
		memzero_explicit(&kcp, sizeof(kcp));
		return -ENOMEM;
	}
	t = (struct clearkeytoken *)tmpbuf;
	t->type = TOKTYPE_NON_CCA;
	t->version = TOKVER_CLEAR_KEY;
	t->keytype = (keylen - 8) >> 3;
	t->len = keylen;
	memcpy(t->clearkey, kcp.clrkey.clrkey, keylen);
	kcp.protkey.len = sizeof(kcp.protkey.protkey);

	rc = key2protkey(NULL, 0,
			 tmpbuf, sizeof(*t) + keylen,
			 kcp.protkey.protkey,
			 &kcp.protkey.len, &kcp.protkey.type);
	pr_debug("key2protkey()=%d\n", rc);

	kfree_sensitive(tmpbuf);

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

	nr_apqns = MAXAPQNSINLIST;
	apqns = kmalloc_array(nr_apqns, sizeof(struct pkey_apqn), GFP_KERNEL);
	if (!apqns)
		return -ENOMEM;

	rc = pkey_handler_apqns_for_key(kfc.seckey.seckey,
					sizeof(kfc.seckey.seckey),
					PKEY_FLAGS_MATCH_CUR_MKVP,
					apqns, &nr_apqns);
	if (rc == -ENODEV)
		rc = pkey_handler_apqns_for_key(kfc.seckey.seckey,
						sizeof(kfc.seckey.seckey),
						PKEY_FLAGS_MATCH_ALT_MKVP,
						apqns, &nr_apqns);
	pr_debug("apqns_for_key()=%d\n", rc);
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
	int rc;

	if (copy_from_user(&ksp, usp, sizeof(ksp)))
		return -EFAULT;

	ksp.protkey.len = sizeof(ksp.protkey.protkey);
	rc = pkey_handler_key_to_protkey(NULL, 0,
					 ksp.seckey.seckey,
					 sizeof(ksp.seckey.seckey),
					 ksp.protkey.protkey,
					 &ksp.protkey.len,
					 &ksp.protkey.type);
	pr_debug("key_to_protkey()=%d\n", rc);
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
	rc = pkey_handler_verify_key(kvk.seckey.seckey,
				     sizeof(kvk.seckey.seckey),
				     &kvk.cardnr, &kvk.domain,
				     &keytype, &keybitsize, &flags);
	pr_debug("verify_key()=%d\n", rc);
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
	rc = pkey_handler_gen_key(NULL, 0, kgp.keytype,
				  PKEY_TYPE_PROTKEY, 0, 0,
				  kgp.protkey.protkey, &kgp.protkey.len,
				  &kgp.protkey.type);
	pr_debug("gen_key()=%d\n", rc);
	if (!rc && copy_to_user(ugp, &kgp, sizeof(kgp)))
		rc = -EFAULT;
	memzero_explicit(&kgp, sizeof(kgp));

	return rc;
}

static int pkey_ioctl_verifyprotk(struct pkey_verifyprotk __user *uvp)
{
	struct pkey_verifyprotk kvp;
	struct protaeskeytoken *t;
	u32 keytype;
	u8 *tmpbuf;
	int rc;

	if (copy_from_user(&kvp, uvp, sizeof(kvp)))
		return -EFAULT;

	keytype = pkey_aes_bitsize_to_keytype(8 * kvp.protkey.len);
	if (!keytype) {
		PKEY_DBF_ERR("%s unknown/unsupported protkey length %u\n",
			     __func__, kvp.protkey.len);
		memzero_explicit(&kvp, sizeof(kvp));
		return -EINVAL;
	}

	/* build a 'protected key token' from the raw protected key */
	tmpbuf = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!tmpbuf) {
		memzero_explicit(&kvp, sizeof(kvp));
		return -ENOMEM;
	}
	t = (struct protaeskeytoken *)tmpbuf;
	t->type = TOKTYPE_NON_CCA;
	t->version = TOKVER_PROTECTED_KEY;
	t->keytype = keytype;
	t->len = kvp.protkey.len;
	memcpy(t->protkey, kvp.protkey.protkey, kvp.protkey.len);

	rc = pkey_handler_verify_key(tmpbuf, sizeof(*t),
				     NULL, NULL, NULL, NULL, NULL);
	pr_debug("verify_key()=%d\n", rc);

	kfree_sensitive(tmpbuf);
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
	u32 u;

	if (copy_from_user(&kgs, ugs, sizeof(kgs)))
		return -EFAULT;
	u = pkey_aes_bitsize_to_keytype(kgs.size);
	if (!u) {
		PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
			     __func__, kgs.size);
		return -EINVAL;
	}
	apqns = _copy_apqns_from_user(kgs.apqns, kgs.apqn_entries);
	if (IS_ERR(apqns))
		return PTR_ERR(apqns);
	kkey = kzalloc(klen, GFP_KERNEL);
	if (!kkey) {
		kfree(apqns);
		return -ENOMEM;
	}
	rc = pkey_handler_gen_key(apqns, kgs.apqn_entries,
				  u, kgs.type, kgs.size, kgs.keygenflags,
				  kkey, &klen, NULL);
	pr_debug("gen_key()=%d\n", rc);
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
	u32 u;

	if (copy_from_user(&kcs, ucs, sizeof(kcs)))
		return -EFAULT;
	u = pkey_aes_bitsize_to_keytype(kcs.size);
	if (!u) {
		PKEY_DBF_ERR("%s unknown/unsupported keybitsize %d\n",
			     __func__, kcs.size);
		memzero_explicit(&kcs, sizeof(kcs));
		return -EINVAL;
	}
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
	rc = pkey_handler_clr_to_key(apqns, kcs.apqn_entries,
				     u, kcs.type, kcs.size, kcs.keygenflags,
				     kcs.clrkey.clrkey, kcs.size / 8,
				     kkey, &klen, NULL);
	pr_debug("clr_to_key()=%d\n", rc);
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

	rc = pkey_handler_verify_key(kkey, kvk.keylen,
				     &kvk.cardnr, &kvk.domain,
				     &kvk.type, &kvk.size, &kvk.flags);
	pr_debug("verify_key()=%d\n", rc);

	kfree_sensitive(kkey);
	if (!rc && copy_to_user(uvk, &kvk, sizeof(kvk)))
		return -EFAULT;

	return rc;
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
	rc = pkey_handler_apqns_for_key(kkey, kak.keylen, kak.flags,
					apqns, &nr_apqns);
	pr_debug("apqns_for_key()=%d\n", rc);
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
	rc = pkey_handler_apqns_for_keytype(kat.type,
					    kat.cur_mkvp, kat.alt_mkvp,
					    kat.flags, apqns, &nr_apqns);
	pr_debug("apqns_for_keytype()=%d\n", rc);
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
	.unlocked_ioctl = pkey_unlocked_ioctl,
};

static struct miscdevice pkey_dev = {
	.name	= "pkey",
	.minor	= MISC_DYNAMIC_MINOR,
	.mode	= 0666,
	.fops	= &pkey_fops,
	.groups = pkey_attr_groups,
};

int __init pkey_api_init(void)
{
	/* register as a misc device */
	return misc_register(&pkey_dev);
}

void __exit pkey_api_exit(void)
{
	misc_deregister(&pkey_dev);
}
