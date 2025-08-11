// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey module sysfs related functions
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/sysfs.h>

#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

#include "pkey_base.h"

/*
 * Wrapper around pkey_handler_gen_key() which deals with the
 * ENODEV return code and then tries to enforce a pkey handler
 * module load.
 */
static int sys_pkey_handler_gen_key(u32 keytype, u32 keysubtype,
				    u32 keybitsize, u32 flags,
				    u8 *keybuf, u32 *keybuflen, u32 *keyinfo)
{
	int rc;

	rc = pkey_handler_gen_key(NULL, 0,
				  keytype, keysubtype,
				  keybitsize, flags,
				  keybuf, keybuflen, keyinfo, 0);
	if (rc == -ENODEV) {
		pkey_handler_request_modules();
		rc = pkey_handler_gen_key(NULL, 0,
					  keytype, keysubtype,
					  keybitsize, flags,
					  keybuf, keybuflen, keyinfo, 0);
	}

	return rc;
}

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
	rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_PROTKEY, 0, 0,
				      protkey.protkey, &protkey.len,
				      &protkey.type);
	if (rc)
		return rc;

	protkeytoken.len = protkey.len;
	memcpy(&protkeytoken.protkey, &protkey.protkey, protkey.len);

	memcpy(buf, &protkeytoken, sizeof(protkeytoken));

	if (is_xts) {
		/* xts needs a second protected key, reuse protkey struct */
		protkey.len = sizeof(protkey.protkey);
		rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_PROTKEY, 0, 0,
					      protkey.protkey, &protkey.len,
					      &protkey.type);
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

/*
 * Sysfs attribute read function for the AES XTS prot key binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * protected key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 */
static ssize_t pkey_protkey_aes_xts_attr_read(u32 keytype, char *buf,
					      loff_t off, size_t count)
{
	struct protkeytoken *t = (struct protkeytoken *)buf;
	u32 protlen, prottype;
	int rc;

	switch (keytype) {
	case PKEY_KEYTYPE_AES_XTS_128:
		protlen = 64;
		break;
	case PKEY_KEYTYPE_AES_XTS_256:
		protlen = 96;
		break;
	default:
		return -EINVAL;
	}

	if (off != 0 || count < sizeof(*t) + protlen)
		return -EINVAL;

	memset(t, 0, sizeof(*t) + protlen);
	t->type = TOKTYPE_NON_CCA;
	t->version = TOKVER_PROTECTED_KEY;
	t->keytype = keytype;

	rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_PROTKEY, 0, 0,
				      t->protkey, &protlen, &prottype);
	if (rc)
		return rc;

	t->len = protlen;

	return sizeof(*t) + protlen;
}

/*
 * Sysfs attribute read function for the HMAC prot key binary attributes.
 * The implementation can not deal with partial reads, because a new random
 * protected key blob is generated with each read. In case of partial reads
 * (i.e. off != 0 or count < key blob size) -EINVAL is returned.
 */
static ssize_t pkey_protkey_hmac_attr_read(u32 keytype, char *buf,
					   loff_t off, size_t count)
{
	struct protkeytoken *t = (struct protkeytoken *)buf;
	u32 protlen, prottype;
	int rc;

	switch (keytype) {
	case PKEY_KEYTYPE_HMAC_512:
		protlen = 96;
		break;
	case PKEY_KEYTYPE_HMAC_1024:
		protlen = 160;
		break;
	default:
		return -EINVAL;
	}

	if (off != 0 || count < sizeof(*t) + protlen)
		return -EINVAL;

	memset(t, 0, sizeof(*t) + protlen);
	t->type = TOKTYPE_NON_CCA;
	t->version = TOKVER_PROTECTED_KEY;
	t->keytype = keytype;

	rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_PROTKEY, 0, 0,
				      t->protkey, &protlen, &prottype);
	if (rc)
		return rc;

	t->len = protlen;

	return sizeof(*t) + protlen;
}

static ssize_t protkey_aes_128_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_128, false, buf,
					  off, count);
}

static ssize_t protkey_aes_192_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_192, false, buf,
					  off, count);
}

static ssize_t protkey_aes_256_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_256, false, buf,
					  off, count);
}

static ssize_t protkey_aes_128_xts_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_128, true, buf,
					  off, count);
}

static ssize_t protkey_aes_256_xts_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_attr_read(PKEY_KEYTYPE_AES_256, true, buf,
					  off, count);
}

static ssize_t protkey_aes_xts_128_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_xts_attr_read(PKEY_KEYTYPE_AES_XTS_128,
					      buf, off, count);
}

static ssize_t protkey_aes_xts_256_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_protkey_aes_xts_attr_read(PKEY_KEYTYPE_AES_XTS_256,
					      buf, off, count);
}

static ssize_t protkey_hmac_512_read(struct file *filp,
				     struct kobject *kobj,
				     const struct bin_attribute *attr,
				     char *buf, loff_t off,
				     size_t count)
{
	return pkey_protkey_hmac_attr_read(PKEY_KEYTYPE_HMAC_512,
					   buf, off, count);
}

static ssize_t protkey_hmac_1024_read(struct file *filp,
				      struct kobject *kobj,
				      const struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_protkey_hmac_attr_read(PKEY_KEYTYPE_HMAC_1024,
					   buf, off, count);
}

static const BIN_ATTR_RO(protkey_aes_128, sizeof(struct protaeskeytoken));
static const BIN_ATTR_RO(protkey_aes_192, sizeof(struct protaeskeytoken));
static const BIN_ATTR_RO(protkey_aes_256, sizeof(struct protaeskeytoken));
static const BIN_ATTR_RO(protkey_aes_128_xts, 2 * sizeof(struct protaeskeytoken));
static const BIN_ATTR_RO(protkey_aes_256_xts, 2 * sizeof(struct protaeskeytoken));
static const BIN_ATTR_RO(protkey_aes_xts_128, sizeof(struct protkeytoken) + 64);
static const BIN_ATTR_RO(protkey_aes_xts_256, sizeof(struct protkeytoken) + 96);
static const BIN_ATTR_RO(protkey_hmac_512, sizeof(struct protkeytoken) + 96);
static const BIN_ATTR_RO(protkey_hmac_1024, sizeof(struct protkeytoken) + 160);

static const struct bin_attribute *const protkey_attrs[] = {
	&bin_attr_protkey_aes_128,
	&bin_attr_protkey_aes_192,
	&bin_attr_protkey_aes_256,
	&bin_attr_protkey_aes_128_xts,
	&bin_attr_protkey_aes_256_xts,
	&bin_attr_protkey_aes_xts_128,
	&bin_attr_protkey_aes_xts_256,
	&bin_attr_protkey_hmac_512,
	&bin_attr_protkey_hmac_1024,
	NULL
};

static const struct attribute_group protkey_attr_group = {
	.name	       = "protkey",
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
	u32 buflen;
	int rc;

	if (off != 0 || count < sizeof(struct secaeskeytoken))
		return -EINVAL;
	if (is_xts)
		if (count < 2 * sizeof(struct secaeskeytoken))
			return -EINVAL;

	buflen = sizeof(seckey->seckey);
	rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_CCA_DATA, 0, 0,
				      seckey->seckey, &buflen, NULL);
	if (rc)
		return rc;

	if (is_xts) {
		seckey++;
		buflen = sizeof(seckey->seckey);
		rc = sys_pkey_handler_gen_key(keytype, PKEY_TYPE_CCA_DATA, 0, 0,
					      seckey->seckey, &buflen, NULL);
		if (rc)
			return rc;

		return 2 * sizeof(struct secaeskeytoken);
	}

	return sizeof(struct secaeskeytoken);
}

static ssize_t ccadata_aes_128_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_128, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_192_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_192, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_256_read(struct file *filp,
				    struct kobject *kobj,
				    const struct bin_attribute *attr,
				    char *buf, loff_t off,
				    size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_256, false, buf,
					  off, count);
}

static ssize_t ccadata_aes_128_xts_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_128, true, buf,
					  off, count);
}

static ssize_t ccadata_aes_256_xts_read(struct file *filp,
					struct kobject *kobj,
					const struct bin_attribute *attr,
					char *buf, loff_t off,
					size_t count)
{
	return pkey_ccadata_aes_attr_read(PKEY_KEYTYPE_AES_256, true, buf,
					  off, count);
}

static const BIN_ATTR_RO(ccadata_aes_128, sizeof(struct secaeskeytoken));
static const BIN_ATTR_RO(ccadata_aes_192, sizeof(struct secaeskeytoken));
static const BIN_ATTR_RO(ccadata_aes_256, sizeof(struct secaeskeytoken));
static const BIN_ATTR_RO(ccadata_aes_128_xts, 2 * sizeof(struct secaeskeytoken));
static const BIN_ATTR_RO(ccadata_aes_256_xts, 2 * sizeof(struct secaeskeytoken));

static const struct bin_attribute *const ccadata_attrs[] = {
	&bin_attr_ccadata_aes_128,
	&bin_attr_ccadata_aes_192,
	&bin_attr_ccadata_aes_256,
	&bin_attr_ccadata_aes_128_xts,
	&bin_attr_ccadata_aes_256_xts,
	NULL
};

static const struct attribute_group ccadata_attr_group = {
	.name	       = "ccadata",
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
	u32 keysize = CCACIPHERTOKENSIZE;
	int rc;

	if (off != 0 || count < CCACIPHERTOKENSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * CCACIPHERTOKENSIZE)
			return -EINVAL;

	memset(buf, 0, is_xts ? 2 * keysize : keysize);

	rc = sys_pkey_handler_gen_key(pkey_aes_bitsize_to_keytype(keybits),
				      PKEY_TYPE_CCA_CIPHER, keybits, 0,
				      buf, &keysize, NULL);
	if (rc)
		return rc;

	if (is_xts) {
		keysize = CCACIPHERTOKENSIZE;
		buf += CCACIPHERTOKENSIZE;
		rc = sys_pkey_handler_gen_key(
			pkey_aes_bitsize_to_keytype(keybits),
			PKEY_TYPE_CCA_CIPHER, keybits, 0,
			buf, &keysize, NULL);
		if (rc)
			return rc;
		return 2 * CCACIPHERTOKENSIZE;
	}

	return CCACIPHERTOKENSIZE;
}

static ssize_t ccacipher_aes_128_read(struct file *filp,
				      struct kobject *kobj,
				      const struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_128, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_192_read(struct file *filp,
				      struct kobject *kobj,
				      const struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_192, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_256_read(struct file *filp,
				      struct kobject *kobj,
				      const struct bin_attribute *attr,
				      char *buf, loff_t off,
				      size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_256, false, buf,
					    off, count);
}

static ssize_t ccacipher_aes_128_xts_read(struct file *filp,
					  struct kobject *kobj,
					  const struct bin_attribute *attr,
					  char *buf, loff_t off,
					  size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_128, true, buf,
					    off, count);
}

static ssize_t ccacipher_aes_256_xts_read(struct file *filp,
					  struct kobject *kobj,
					  const struct bin_attribute *attr,
					  char *buf, loff_t off,
					  size_t count)
{
	return pkey_ccacipher_aes_attr_read(PKEY_SIZE_AES_256, true, buf,
					    off, count);
}

static const BIN_ATTR_RO(ccacipher_aes_128, CCACIPHERTOKENSIZE);
static const BIN_ATTR_RO(ccacipher_aes_192, CCACIPHERTOKENSIZE);
static const BIN_ATTR_RO(ccacipher_aes_256, CCACIPHERTOKENSIZE);
static const BIN_ATTR_RO(ccacipher_aes_128_xts, 2 * CCACIPHERTOKENSIZE);
static const BIN_ATTR_RO(ccacipher_aes_256_xts, 2 * CCACIPHERTOKENSIZE);

static const struct bin_attribute *const ccacipher_attrs[] = {
	&bin_attr_ccacipher_aes_128,
	&bin_attr_ccacipher_aes_192,
	&bin_attr_ccacipher_aes_256,
	&bin_attr_ccacipher_aes_128_xts,
	&bin_attr_ccacipher_aes_256_xts,
	NULL
};

static const struct attribute_group ccacipher_attr_group = {
	.name	       = "ccacipher",
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
	u32 keysize = MAXEP11AESKEYBLOBSIZE;
	int rc;

	if (off != 0 || count < MAXEP11AESKEYBLOBSIZE)
		return -EINVAL;
	if (is_xts)
		if (count < 2 * MAXEP11AESKEYBLOBSIZE)
			return -EINVAL;

	memset(buf, 0, is_xts ? 2 * keysize : keysize);

	rc = sys_pkey_handler_gen_key(pkey_aes_bitsize_to_keytype(keybits),
				      PKEY_TYPE_EP11_AES, keybits, 0,
				      buf, &keysize, NULL);
	if (rc)
		return rc;

	if (is_xts) {
		keysize = MAXEP11AESKEYBLOBSIZE;
		buf += MAXEP11AESKEYBLOBSIZE;
		rc = sys_pkey_handler_gen_key(
			pkey_aes_bitsize_to_keytype(keybits),
			PKEY_TYPE_EP11_AES, keybits, 0,
			buf, &keysize, NULL);
		if (rc)
			return rc;
		return 2 * MAXEP11AESKEYBLOBSIZE;
	}

	return MAXEP11AESKEYBLOBSIZE;
}

static ssize_t ep11_aes_128_read(struct file *filp,
				 struct kobject *kobj,
				 const struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_128, false, buf,
				       off, count);
}

static ssize_t ep11_aes_192_read(struct file *filp,
				 struct kobject *kobj,
				 const struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_192, false, buf,
				       off, count);
}

static ssize_t ep11_aes_256_read(struct file *filp,
				 struct kobject *kobj,
				 const struct bin_attribute *attr,
				 char *buf, loff_t off,
				 size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_256, false, buf,
				       off, count);
}

static ssize_t ep11_aes_128_xts_read(struct file *filp,
				     struct kobject *kobj,
				     const struct bin_attribute *attr,
				     char *buf, loff_t off,
				     size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_128, true, buf,
				       off, count);
}

static ssize_t ep11_aes_256_xts_read(struct file *filp,
				     struct kobject *kobj,
				     const struct bin_attribute *attr,
				     char *buf, loff_t off,
				     size_t count)
{
	return pkey_ep11_aes_attr_read(PKEY_SIZE_AES_256, true, buf,
				       off, count);
}

static const BIN_ATTR_RO(ep11_aes_128, MAXEP11AESKEYBLOBSIZE);
static const BIN_ATTR_RO(ep11_aes_192, MAXEP11AESKEYBLOBSIZE);
static const BIN_ATTR_RO(ep11_aes_256, MAXEP11AESKEYBLOBSIZE);
static const BIN_ATTR_RO(ep11_aes_128_xts, 2 * MAXEP11AESKEYBLOBSIZE);
static const BIN_ATTR_RO(ep11_aes_256_xts, 2 * MAXEP11AESKEYBLOBSIZE);

static const struct bin_attribute *const ep11_attrs[] = {
	&bin_attr_ep11_aes_128,
	&bin_attr_ep11_aes_192,
	&bin_attr_ep11_aes_256,
	&bin_attr_ep11_aes_128_xts,
	&bin_attr_ep11_aes_256_xts,
	NULL
};

static const struct attribute_group ep11_attr_group = {
	.name	   = "ep11",
	.bin_attrs = ep11_attrs,
};

const struct attribute_group *pkey_attr_groups[] = {
	&protkey_attr_group,
	&ccadata_attr_group,
	&ccacipher_attr_group,
	&ep11_attr_group,
	NULL,
};
