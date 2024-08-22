// SPDX-License-Identifier: GPL-2.0
/*
 *  pkey module sysfs related functions
 *
 *  Copyright IBM Corp. 2024
 */

#define KMSG_COMPONENT "pkey"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <asm/pkey.h>
#include <linux/sysfs.h>

#include "zcrypt_api.h"
#include "zcrypt_ccamisc.h"
#include "zcrypt_ep11misc.h"

#include "pkey_base.h"

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
	rc = pkey_pckmo_gen_protkey(protkeytoken.keytype,
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
		rc = pkey_pckmo_gen_protkey(protkeytoken.keytype,
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
	u32 keysize = CCACIPHERTOKENSIZE;
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
	u32 keysize = MAXEP11AESKEYBLOBSIZE;
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

const struct attribute_group *pkey_attr_groups[] = {
	&protkey_attr_group,
	&ccadata_attr_group,
	&ccacipher_attr_group,
	&ep11_attr_group,
	NULL,
};
