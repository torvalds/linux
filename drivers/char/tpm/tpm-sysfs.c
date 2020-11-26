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
	NULL
};

static const struct attribute_group tpm1_dev_group = {
	.attrs = tpm1_dev_attrs,
};

static const struct attribute_group tpm2_dev_group = {
	.attrs = tpm2_dev_attrs,
};

void tpm_sysfs_add_device(struct tpm_chip *chip)
{
	WARN_ON(chip->groups_cnt != 0);
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		chip->groups[chip->groups_cnt++] = &tpm2_dev_group;
	else
		chip->groups[chip->groups_cnt++] = &tpm1_dev_group;
}
