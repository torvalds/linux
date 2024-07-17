// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004 IBM Corporation
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Copyright (C) 2013 Obsidian Research Corp
 * Jason Gunthorpe <jgunthorpe@obsidianresearch.com>
 *
 * sysfs filesystem inspection interface to the TPM
 */
#include <linux/device.h>
#include "tpm.h"

struct tpm_readpubek_out {
	u8 algorithm[4];
	u8 encscheme[2];
	u8 sigscheme[2];
	__be32 paramsize;
	u8 parameters[12];
	__be32 keysize;
	u8 modulus[256];
	u8 checksum[20];
} __packed;

#define READ_PUBEK_RESULT_MIN_BODY_SIZE (28 + 256)
#define TPM_ORD_READPUBEK 124

static ssize_t pubek_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tpm_buf tpm_buf;
	struct tpm_readpubek_out *out;
	int i;
	char *str = buf;
	struct tpm_chip *chip = to_tpm_chip(dev);
	char anti_replay[20];

	memset(&anti_replay, 0, sizeof(anti_replay));

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm_buf_init(&tpm_buf, TPM_TAG_RQU_COMMAND, TPM_ORD_READPUBEK))
		goto out_ops;

	tpm_buf_append(&tpm_buf, anti_replay, sizeof(anti_replay));

	if (tpm_transmit_cmd(chip, &tpm_buf, READ_PUBEK_RESULT_MIN_BODY_SIZE,
			     "attempting to read the PUBEK"))
		goto out_buf;

	out = (struct tpm_readpubek_out *)&tpm_buf.data[10];
	str +=
	    sprintf(str,
		    "Algorithm: %4ph\n"
		    "Encscheme: %2ph\n"
		    "Sigscheme: %2ph\n"
		    "Parameters: %12ph\n"
		    "Modulus length: %d\n"
		    "Modulus:\n",
		    out->algorithm,
		    out->encscheme,
		    out->sigscheme,
		    out->parameters,
		    be32_to_cpu(out->keysize));

	for (i = 0; i < 256; i += 16)
		str += sprintf(str, "%16ph\n", &out->modulus[i]);

out_buf:
	tpm_buf_destroy(&tpm_buf);
out_ops:
	tpm_put_ops(chip);
	return str - buf;
}
static DEVICE_ATTR_RO(pubek);

static ssize_t pcrs_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	cap_t cap;
	u8 digest[TPM_DIGEST_SIZE];
	u32 i, j, num_pcrs;
	char *str = buf;
	struct tpm_chip *chip = to_tpm_chip(dev);

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(chip, TPM_CAP_PROP_PCR, &cap,
			"attempting to determine the number of PCRS",
			sizeof(cap.num_pcrs))) {
		tpm_put_ops(chip);
		return 0;
	}

	num_pcrs = be32_to_cpu(cap.num_pcrs);
	for (i = 0; i < num_pcrs; i++) {
		if (tpm1_pcr_read(chip, i, digest)) {
			str = buf;
			break;
		}
		str += sprintf(str, "PCR-%02d: ", i);
		for (j = 0; j < TPM_DIGEST_SIZE; j++)
			str += sprintf(str, "%02X ", digest[j]);
		str += sprintf(str, "\n");
	}
	tpm_put_ops(chip);
	return str - buf;
}
static DEVICE_ATTR_RO(pcrs);

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	ssize_t rc = 0;
	cap_t cap;

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(chip, TPM_CAP_FLAG_PERM, &cap,
			"attempting to determine the permanent enabled state",
			sizeof(cap.perm_flags)))
		goto out_ops;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.disable);
out_ops:
	tpm_put_ops(chip);
	return rc;
}
static DEVICE_ATTR_RO(enabled);

static ssize_t active_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	ssize_t rc = 0;
	cap_t cap;

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(chip, TPM_CAP_FLAG_PERM, &cap,
			"attempting to determine the permanent active state",
			sizeof(cap.perm_flags)))
		goto out_ops;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.deactivated);
out_ops:
	tpm_put_ops(chip);
	return rc;
}
static DEVICE_ATTR_RO(active);

static ssize_t owned_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	ssize_t rc = 0;
	cap_t cap;

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(to_tpm_chip(dev), TPM_CAP_PROP_OWNER, &cap,
			"attempting to determine the owner state",
			sizeof(cap.owned)))
		goto out_ops;

	rc = sprintf(buf, "%d\n", cap.owned);
out_ops:
	tpm_put_ops(chip);
	return rc;
}
static DEVICE_ATTR_RO(owned);

static ssize_t temp_deactivated_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	ssize_t rc = 0;
	cap_t cap;

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(to_tpm_chip(dev), TPM_CAP_FLAG_VOL, &cap,
			"attempting to determine the temporary state",
			sizeof(cap.stclear_flags)))
		goto out_ops;

	rc = sprintf(buf, "%d\n", cap.stclear_flags.deactivated);
out_ops:
	tpm_put_ops(chip);
	return rc;
}
static DEVICE_ATTR_RO(temp_deactivated);

static ssize_t caps_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	struct tpm1_version *version;
	ssize_t rc = 0;
	char *str = buf;
	cap_t cap;

	if (tpm_try_get_ops(chip))
		return 0;

	if (tpm1_getcap(chip, TPM_CAP_PROP_MANUFACTURER, &cap,
			"attempting to determine the manufacturer",
			sizeof(cap.manufacturer_id)))
		goto out_ops;

	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(cap.manufacturer_id));

	/* TPM 1.2 */
	if (!tpm1_getcap(chip, TPM_CAP_VERSION_1_2, &cap,
			 "attempting to determine the 1.2 version",
			 sizeof(cap.version2))) {
		version = &cap.version2.version;
		goto out_print;
	}

	/* TPM 1.1 */
	if (tpm1_getcap(chip, TPM_CAP_VERSION_1_1, &cap,
			"attempting to determine the 1.1 version",
			sizeof(cap.version1))) {
		goto out_ops;
	}

	version = &cap.version1;

out_print:
	str += sprintf(str,
		       "TCG version: %d.%d\nFirmware version: %d.%d\n",
		       version->major, version->minor,
		       version->rev_major, version->rev_minor);

	rc = str - buf;

out_ops:
	tpm_put_ops(chip);
	return rc;
}
static DEVICE_ATTR_RO(caps);

static ssize_t cancel_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct tpm_chip *chip = to_tpm_chip(dev);

	if (tpm_try_get_ops(chip))
		return 0;

	chip->ops->cancel(chip);
	tpm_put_ops(chip);
	return count;
}
static DEVICE_ATTR_WO(cancel);

static ssize_t durations_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);

	if (chip->duration[TPM_LONG] == 0)
		return 0;

	return sprintf(buf, "%d %d %d [%s]\n",
		       jiffies_to_usecs(chip->duration[TPM_SHORT]),
		       jiffies_to_usecs(chip->duration[TPM_MEDIUM]),
		       jiffies_to_usecs(chip->duration[TPM_LONG]),
		       chip->duration_adjusted
		       ? "adjusted" : "original");
}
static DEVICE_ATTR_RO(durations);

static ssize_t timeouts_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);

	return sprintf(buf, "%d %d %d %d [%s]\n",
		       jiffies_to_usecs(chip->timeout_a),
		       jiffies_to_usecs(chip->timeout_b),
		       jiffies_to_usecs(chip->timeout_c),
		       jiffies_to_usecs(chip->timeout_d),
		       chip->timeout_adjusted
		       ? "adjusted" : "original");
}
static DEVICE_ATTR_RO(timeouts);

static ssize_t tpm_version_major_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);

	return sprintf(buf, "%s\n", chip->flags & TPM_CHIP_FLAG_TPM2
		       ? "2" : "1");
}
static DEVICE_ATTR_RO(tpm_version_major);

#ifdef CONFIG_TCG_TPM2_HMAC
static ssize_t null_name_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	int size = TPM2_NAME_SIZE;

	bin2hex(buf, chip->null_key_name, size);
	size *= 2;
	buf[size++] = '\n';
	return size;
}
static DEVICE_ATTR_RO(null_name);
#endif

static struct attribute *tpm1_dev_attrs[] = {
	&dev_attr_pubek.attr,
	&dev_attr_pcrs.attr,
	&dev_attr_enabled.attr,
	&dev_attr_active.attr,
	&dev_attr_owned.attr,
	&dev_attr_temp_deactivated.attr,
	&dev_attr_caps.attr,
	&dev_attr_cancel.attr,
	&dev_attr_durations.attr,
	&dev_attr_timeouts.attr,
	&dev_attr_tpm_version_major.attr,
	NULL,
};

static struct attribute *tpm2_dev_attrs[] = {
	&dev_attr_tpm_version_major.attr,
#ifdef CONFIG_TCG_TPM2_HMAC
	&dev_attr_null_name.attr,
#endif
	NULL
};

static const struct attribute_group tpm1_dev_group = {
	.attrs = tpm1_dev_attrs,
};

static const struct attribute_group tpm2_dev_group = {
	.attrs = tpm2_dev_attrs,
};

struct tpm_pcr_attr {
	int alg_id;
	int pcr;
	struct device_attribute attr;
};

#define to_tpm_pcr_attr(a) container_of(a, struct tpm_pcr_attr, attr)

static ssize_t pcr_value_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct tpm_pcr_attr *ha = to_tpm_pcr_attr(attr);
	struct tpm_chip *chip = to_tpm_chip(dev);
	struct tpm_digest digest;
	int i;
	int digest_size = 0;
	int rc;
	char *str = buf;

	for (i = 0; i < chip->nr_allocated_banks; i++)
		if (ha->alg_id == chip->allocated_banks[i].alg_id)
			digest_size = chip->allocated_banks[i].digest_size;
	/* should never happen */
	if (!digest_size)
		return -EINVAL;

	digest.alg_id = ha->alg_id;
	rc = tpm_pcr_read(chip, ha->pcr, &digest);
	if (rc)
		return rc;
	for (i = 0; i < digest_size; i++)
		str += sprintf(str, "%02X", digest.digest[i]);
	str += sprintf(str, "\n");

	return str - buf;
}

/*
 * The following set of defines represents all the magic to build
 * the per hash attribute groups for displaying each bank of PCRs.
 * The only slight problem with this approach is that every PCR is
 * hard coded to be present, so you don't know if an PCR is missing
 * until a cat of the file returns -EINVAL
 *
 * Also note you must ignore checkpatch warnings in this macro
 * code. This is deep macro magic that checkpatch.pl doesn't
 * understand.
 */

/* Note, this must match TPM2_PLATFORM_PCR which is fixed at 24. */
#define _TPM_HELPER(_alg, _hash, F) \
	F(_alg, _hash, 0)	    \
	F(_alg, _hash, 1)	    \
	F(_alg, _hash, 2)	    \
	F(_alg, _hash, 3)	    \
	F(_alg, _hash, 4)	    \
	F(_alg, _hash, 5)	    \
	F(_alg, _hash, 6)	    \
	F(_alg, _hash, 7)	    \
	F(_alg, _hash, 8)	    \
	F(_alg, _hash, 9)	    \
	F(_alg, _hash, 10)	    \
	F(_alg, _hash, 11)	    \
	F(_alg, _hash, 12)	    \
	F(_alg, _hash, 13)	    \
	F(_alg, _hash, 14)	    \
	F(_alg, _hash, 15)	    \
	F(_alg, _hash, 16)	    \
	F(_alg, _hash, 17)	    \
	F(_alg, _hash, 18)	    \
	F(_alg, _hash, 19)	    \
	F(_alg, _hash, 20)	    \
	F(_alg, _hash, 21)	    \
	F(_alg, _hash, 22)	    \
	F(_alg, _hash, 23)

/* ignore checkpatch warning about trailing ; in macro. */
#define PCR_ATTR(_alg, _hash, _pcr)				   \
	static struct tpm_pcr_attr dev_attr_pcr_##_hash##_##_pcr = {	\
		.alg_id = _alg,					   \
		.pcr = _pcr,					   \
		.attr = {					   \
			.attr = {				   \
				.name = __stringify(_pcr),	   \
				.mode = 0444			   \
			},					   \
			.show = pcr_value_show			   \
		}						   \
	};

#define PCR_ATTRS(_alg, _hash)			\
	_TPM_HELPER(_alg, _hash, PCR_ATTR)

/* ignore checkpatch warning about trailing , in macro. */
#define PCR_ATTR_VAL(_alg, _hash, _pcr)		\
	&dev_attr_pcr_##_hash##_##_pcr.attr.attr,

#define PCR_ATTR_GROUP_ARRAY(_alg, _hash)		       \
	static struct attribute *pcr_group_attrs_##_hash[] = { \
		_TPM_HELPER(_alg, _hash, PCR_ATTR_VAL)	       \
		NULL					       \
	}

#define PCR_ATTR_GROUP(_alg, _hash)			    \
	static struct attribute_group pcr_group_##_hash = { \
		.name = "pcr-" __stringify(_hash),	    \
		.attrs = pcr_group_attrs_##_hash	    \
	}

#define PCR_ATTR_BUILD(_alg, _hash)	   \
	PCR_ATTRS(_alg, _hash)		   \
	PCR_ATTR_GROUP_ARRAY(_alg, _hash); \
	PCR_ATTR_GROUP(_alg, _hash)
/*
 * End of macro structure to build an attribute group containing 24
 * PCR value files for each supported hash algorithm
 */

/*
 * The next set of macros implements the cleverness for each hash to
 * build a static attribute group called pcr_group_<hash> which can be
 * added to chip->groups[].
 *
 * The first argument is the TPM algorithm id and the second is the
 * hash used as both the suffix and the group name.  Note: the group
 * name is a directory in the top level tpm class with the name
 * pcr-<hash>, so it must not clash with any other names already
 * in the sysfs directory.
 */
PCR_ATTR_BUILD(TPM_ALG_SHA1, sha1);
PCR_ATTR_BUILD(TPM_ALG_SHA256, sha256);
PCR_ATTR_BUILD(TPM_ALG_SHA384, sha384);
PCR_ATTR_BUILD(TPM_ALG_SHA512, sha512);
PCR_ATTR_BUILD(TPM_ALG_SM3_256, sm3);


void tpm_sysfs_add_device(struct tpm_chip *chip)
{
	int i;

	WARN_ON(chip->groups_cnt != 0);

	if (tpm_is_firmware_upgrade(chip))
		return;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		chip->groups[chip->groups_cnt++] = &tpm2_dev_group;
	else
		chip->groups[chip->groups_cnt++] = &tpm1_dev_group;

	/* add one group for each bank hash */
	for (i = 0; i < chip->nr_allocated_banks; i++) {
		switch (chip->allocated_banks[i].alg_id) {
		case TPM_ALG_SHA1:
			chip->groups[chip->groups_cnt++] = &pcr_group_sha1;
			break;
		case TPM_ALG_SHA256:
			chip->groups[chip->groups_cnt++] = &pcr_group_sha256;
			break;
		case TPM_ALG_SHA384:
			chip->groups[chip->groups_cnt++] = &pcr_group_sha384;
			break;
		case TPM_ALG_SHA512:
			chip->groups[chip->groups_cnt++] = &pcr_group_sha512;
			break;
		case TPM_ALG_SM3_256:
			chip->groups[chip->groups_cnt++] = &pcr_group_sm3;
			break;
		default:
			/*
			 * If triggers, send a patch to add both a
			 * PCR_ATTR_BUILD() macro above for the
			 * missing algorithm as well as an additional
			 * case in this switch statement.
			 */
			dev_err(&chip->dev,
				"TPM with unsupported bank algorithm 0x%04x",
				chip->allocated_banks[i].alg_id);
			break;
		}
	}

	/*
	 * This will only trigger if someone has added an additional
	 * hash to the tpm_algorithms enum without incrementing
	 * TPM_MAX_HASHES.
	 */
	WARN_ON(chip->groups_cnt > TPM_MAX_HASHES + 1);
}
