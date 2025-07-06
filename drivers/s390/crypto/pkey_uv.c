// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey uv specific code
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/uv.h>

#include "zcrypt_ccamisc.h"
#include "pkey_base.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("s390 protected key UV handler");

/*
 * One pre-allocated uv_secret_list for use with uv_find_secret()
 */
static struct uv_secret_list *uv_list;
static DEFINE_MUTEX(uv_list_mutex);

/*
 * UV secret token struct and defines.
 */

#define TOKVER_UV_SECRET 0x09

struct uvsecrettoken {
	u8  type;		/* 0x00 = TOKTYPE_NON_CCA */
	u8  res0[3];
	u8  version;		/* 0x09 = TOKVER_UV_SECRET */
	u8  res1[3];
	u16 secret_type;	/* one of enum uv_secret_types from uv.h */
	u16 secret_len;		/* length in bytes of the secret */
	u8  secret_id[UV_SECRET_ID_LEN]; /* the secret id for this secret */
} __packed;

/*
 * Check key blob for known and supported UV key.
 */
static bool is_uv_key(const u8 *key, u32 keylen)
{
	struct uvsecrettoken *t = (struct uvsecrettoken *)key;

	if (keylen < sizeof(*t))
		return false;

	switch (t->type) {
	case TOKTYPE_NON_CCA:
		switch (t->version) {
		case TOKVER_UV_SECRET:
			switch (t->secret_type) {
			case UV_SECRET_AES_128:
			case UV_SECRET_AES_192:
			case UV_SECRET_AES_256:
			case UV_SECRET_AES_XTS_128:
			case UV_SECRET_AES_XTS_256:
			case UV_SECRET_HMAC_SHA_256:
			case UV_SECRET_HMAC_SHA_512:
			case UV_SECRET_ECDSA_P256:
			case UV_SECRET_ECDSA_P384:
			case UV_SECRET_ECDSA_P521:
			case UV_SECRET_ECDSA_ED25519:
			case UV_SECRET_ECDSA_ED448:
				return true;
			default:
				return false;
			}
		default:
			return false;
		}
	default:
		return false;
	}
}

static bool is_uv_keytype(enum pkey_key_type keytype)
{
	switch (keytype) {
	case PKEY_TYPE_UVSECRET:
		return true;
	default:
		return false;
	}
}

static int get_secret_metadata(const u8 secret_id[UV_SECRET_ID_LEN],
			       struct uv_secret_list_item_hdr *secret)
{
	int rc;

	mutex_lock(&uv_list_mutex);
	memset(uv_list, 0, sizeof(*uv_list));
	rc = uv_find_secret(secret_id, uv_list, secret);
	mutex_unlock(&uv_list_mutex);

	return rc;
}

static int retrieve_secret(const u8 secret_id[UV_SECRET_ID_LEN],
			   u16 *secret_type, u8 *buf, u32 *buflen)
{
	struct uv_secret_list_item_hdr secret_meta_data;
	int rc;

	rc = get_secret_metadata(secret_id, &secret_meta_data);
	if (rc)
		return rc;

	if (*buflen < secret_meta_data.length)
		return -EINVAL;

	rc = uv_retrieve_secret(secret_meta_data.index,
				buf, secret_meta_data.length);
	if (rc)
		return rc;

	*secret_type = secret_meta_data.type;
	*buflen = secret_meta_data.length;

	return 0;
}

static int uv_get_size_and_type(u16 secret_type, u32 *pkeysize, u32 *pkeytype)
{
	int rc = 0;

	switch (secret_type) {
	case UV_SECRET_AES_128:
		*pkeysize = 16 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_AES_128;
		break;
	case UV_SECRET_AES_192:
		*pkeysize = 24 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_AES_192;
		break;
	case UV_SECRET_AES_256:
		*pkeysize = 32 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_AES_256;
		break;
	case UV_SECRET_AES_XTS_128:
		*pkeysize = 16 + 16 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_AES_XTS_128;
		break;
	case UV_SECRET_AES_XTS_256:
		*pkeysize = 32 + 32 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_AES_XTS_256;
		break;
	case UV_SECRET_HMAC_SHA_256:
		*pkeysize = 64 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_HMAC_512;
		break;
	case UV_SECRET_HMAC_SHA_512:
		*pkeysize = 128 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_HMAC_1024;
		break;
	case UV_SECRET_ECDSA_P256:
		*pkeysize = 32 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_ECC_P256;
		break;
	case UV_SECRET_ECDSA_P384:
		*pkeysize = 48 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_ECC_P384;
		break;
	case UV_SECRET_ECDSA_P521:
		*pkeysize = 80 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_ECC_P521;
		break;
	case UV_SECRET_ECDSA_ED25519:
		*pkeysize = 32 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_ECC_ED25519;
		break;
	case UV_SECRET_ECDSA_ED448:
		*pkeysize = 64 + AES_WK_VP_SIZE;
		*pkeytype = PKEY_KEYTYPE_ECC_ED448;
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int uv_key2protkey(const struct pkey_apqn *_apqns __always_unused,
			  size_t _nr_apqns __always_unused,
			  const u8 *key, u32 keylen,
			  u8 *protkey, u32 *protkeylen, u32 *keyinfo,
			  u32 _xflags __always_unused)
{
	struct uvsecrettoken *t = (struct uvsecrettoken *)key;
	u32 pkeysize, pkeytype;
	u16 secret_type;
	int rc;

	rc = uv_get_size_and_type(t->secret_type, &pkeysize, &pkeytype);
	if (rc)
		goto out;

	if (*protkeylen < pkeysize) {
		PKEY_DBF_ERR("%s prot key buffer size too small: %u < %u\n",
			     __func__, *protkeylen, pkeysize);
		rc = -EINVAL;
		goto out;
	}

	rc = retrieve_secret(t->secret_id, &secret_type, protkey, protkeylen);
	if (rc) {
		PKEY_DBF_ERR("%s retrieve_secret() failed with %d\n",
			     __func__, rc);
		goto out;
	}
	if (secret_type != t->secret_type) {
		PKEY_DBF_ERR("%s retrieved secret type %u != expected type %u\n",
			     __func__, secret_type, t->secret_type);
		rc = -EINVAL;
		goto out;
	}

	if (keyinfo)
		*keyinfo = pkeytype;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int uv_verifykey(const u8 *key, u32 keylen,
			u16 *_card __always_unused,
			u16 *_dom __always_unused,
			u32 *keytype, u32 *keybitsize, u32 *flags,
			u32 xflags __always_unused)
{
	struct uvsecrettoken *t = (struct uvsecrettoken *)key;
	struct uv_secret_list_item_hdr secret_meta_data;
	u32 pkeysize, pkeytype, bitsize;
	int rc;

	rc = uv_get_size_and_type(t->secret_type, &pkeysize, &pkeytype);
	if (rc)
		goto out;

	rc = get_secret_metadata(t->secret_id, &secret_meta_data);
	if (rc)
		goto out;

	if (secret_meta_data.type != t->secret_type) {
		rc = -EINVAL;
		goto out;
	}

	/* set keytype; keybitsize and flags are not supported */
	if (keytype)
		*keytype = PKEY_TYPE_UVSECRET;
	if (keybitsize) {
		bitsize = 8 * pkey_keytype_to_size(pkeytype);
		*keybitsize = bitsize ?: PKEY_SIZE_UNKNOWN;
	}
	if (flags)
		*flags = pkeytype;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static struct pkey_handler uv_handler = {
	.module			 = THIS_MODULE,
	.name			 = "PKEY UV handler",
	.is_supported_key	 = is_uv_key,
	.is_supported_keytype	 = is_uv_keytype,
	.key_to_protkey		 = uv_key2protkey,
	.verify_key		 = uv_verifykey,
};

/*
 * Module init
 */
static int __init pkey_uv_init(void)
{
	int rc;

	if (!is_prot_virt_guest())
		return -ENODEV;

	if (!test_bit_inv(BIT_UVC_CMD_RETR_SECRET, uv_info.inst_calls_list))
		return -ENODEV;

	uv_list = kmalloc(sizeof(*uv_list), GFP_KERNEL);
	if (!uv_list)
		return -ENOMEM;

	rc = pkey_handler_register(&uv_handler);
	if (rc)
		kfree(uv_list);

	return rc;
}

/*
 * Module exit
 */
static void __exit pkey_uv_exit(void)
{
	pkey_handler_unregister(&uv_handler);
	mutex_lock(&uv_list_mutex);
	kvfree(uv_list);
	mutex_unlock(&uv_list_mutex);
}

module_cpu_feature_match(S390_CPU_FEATURE_UV, pkey_uv_init);
module_exit(pkey_uv_exit);
