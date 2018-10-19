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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
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
	ssize_t rc;
	int i;
	char *str = buf;
	struct tpm_chip *chip = to_tpm_chip(dev);
	char anti_replay[20];

	memset(&anti_replay, 0, sizeof(anti_replay));

	rc = tpm_buf_init(&tpm_buf, TPM_TAG_RQU_COMMAND, TPM_ORD_READPUBEK);
	if (rc)
		return rc;

	tpm_buf_append(&tpm_buf, anti_replay, sizeof(anti_replay));

	rc = tpm_transmit_cmd(chip, NULL, tpm_buf.data, PAGE_SIZE,
			      READ_PUBEK_RESULT_MIN_BODY_SIZE, 0,
			      "attempting to read the PUBEK");
	if (rc) {
		tpm_buf_destroy(&tpm_buf);
		return 0;
	}

	out = (struct tpm_readpubek_out *)&tpm_buf.data[10];
	str +=
	    sprintf(str,
		    "Algorithm: %02X %02X %02X %02X\n"
		    "Encscheme: %02X %02X\n"
		    "Sigscheme: %02X %02X\n"
		    "Parameters: %02X %02X %02X %02X "
		    "%02X %02X %02X %02X "
		    "%02X %02X %02X %02X\n"
		    "Modulus length: %d\n"
		    "Modulus:\n",
		    out->algorithm[0], out->algorithm[1], out->algorithm[2],
		    out->algorithm[3],
		    out->encscheme[0], out->encscheme[1],
		    out->sigscheme[0], out->sigscheme[1],
		    out->parameters[0], out->parameters[1],
		    out->parameters[2], out->parameters[3],
		    out->parameters[4], out->parameters[5],
		    out->parameters[6], out->parameters[7],
		    out->parameters[8], out->parameters[9],
		    out->parameters[10], out->parameters[11],
		    be32_to_cpu(out->keysize));

	for (i = 0; i < 256; i++) {
		str += sprintf(str, "%02X ", out->modulus[i]);
		if ((i + 1) % 16 == 0)
			str += sprintf(str, "\n");
	}

	rc = str - buf;
	tpm_buf_destroy(&tpm_buf);
	return rc;
}
static DEVICE_ATTR_RO(pubek);

static ssize_t pcrs_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	cap_t cap;
	u8 digest[TPM_DIGEST_SIZE];
	ssize_t rc;
	int i, j, num_pcrs;
	char *str = buf;
	struct tpm_chip *chip = to_tpm_chip(dev);

	rc = tpm1_getcap(chip, TPM_CAP_PROP_PCR, &cap,
			 "attempting to determine the number of PCRS",
			 sizeof(cap.num_pcrs));
	if (rc)
		return 0;

	num_pcrs = be32_to_cpu(cap.num_pcrs);
	for (i = 0; i < num_pcrs; i++) {
		rc = tpm1_pcr_read_dev(chip, i, digest);
		if (rc)
			break;
		str += sprintf(str, "PCR-%02d: ", i);
		for (j = 0; j < TPM_DIGEST_SIZE; j++)
			str += sprintf(str, "%02X ", digest[j]);
		str += sprintf(str, "\n");
	}
	return str - buf;
}
static DEVICE_ATTR_RO(pcrs);

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm1_getcap(to_tpm_chip(dev), TPM_CAP_FLAG_PERM, &cap,
			 "attempting to determine the permanent enabled state",
			 sizeof(cap.perm_flags));
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.disable);
	return rc;
}
static DEVICE_ATTR_RO(enabled);

static ssize_t active_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm1_getcap(to_tpm_chip(dev), TPM_CAP_FLAG_PERM, &cap,
			 "attempting to determine the permanent active state",
			 sizeof(cap.perm_flags));
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", !cap.perm_flags.deactivated);
	return rc;
}
static DEVICE_ATTR_RO(active);

static ssize_t owned_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm1_getcap(to_tpm_chip(dev), TPM_CAP_PROP_OWNER, &cap,
			 "attempting to determine the owner state",
			 sizeof(cap.owned));
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", cap.owned);
	return rc;
}
static DEVICE_ATTR_RO(owned);

static ssize_t temp_deactivated_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	cap_t cap;
	ssize_t rc;

	rc = tpm1_getcap(to_tpm_chip(dev), TPM_CAP_FLAG_VOL, &cap,
			 "attempting to determine the temporary state",
			 sizeof(cap.stclear_flags));
	if (rc)
		return 0;

	rc = sprintf(buf, "%d\n", cap.stclear_flags.deactivated);
	return rc;
}
static DEVICE_ATTR_RO(temp_deactivated);

static ssize_t caps_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	cap_t cap;
	ssize_t rc;
	char *str = buf;

	rc = tpm1_getcap(chip, TPM_CAP_PROP_MANUFACTURER, &cap,
			 "attempting to determine the manufacturer",
			 sizeof(cap.manufacturer_id));
	if (rc)
		return 0;
	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(cap.manufacturer_id));

	/* Try to get a TPM version 1.2 TPM_CAP_VERSION_INFO */
	rc = tpm1_getcap(chip, TPM_CAP_VERSION_1_2, &cap,
			 "attempting to determine the 1.2 version",
			 sizeof(cap.tpm_version_1_2));
	if (!rc) {
		str += sprintf(str,
			       "TCG version: %d.%d\nFirmware version: %d.%d\n",
			       cap.tpm_version_1_2.Major,
			       cap.tpm_version_1_2.Minor,
			       cap.tpm_version_1_2.revMajor,
			       cap.tpm_version_1_2.revMinor);
	} else {
		/* Otherwise just use TPM_STRUCT_VER */
		rc = tpm1_getcap(chip, TPM_CAP_VERSION_1_1, &cap,
				 "attempting to determine the 1.1 version",
				 sizeof(cap.tpm_version));
		if (rc)
			return 0;
		str += sprintf(str,
			       "TCG version: %d.%d\nFirmware version: %d.%d\n",
			       cap.tpm_version.Major,
			       cap.tpm_version.Minor,
			       cap.tpm_version.revMajor,
			       cap.tpm_version.revMinor);
	}

	return str - buf;
}
static DEVICE_ATTR_RO(caps);

static ssize_t cancel_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct tpm_chip *chip = to_tpm_chip(dev);
	if (chip == NULL)
		return 0;

	chip->ops->cancel(chip);
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

static struct attribute *tpm_dev_attrs[] = {
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
	NULL,
};

static const struct attribute_group tpm_dev_group = {
	.attrs = tpm_dev_attrs,
};

void tpm_sysfs_add_device(struct tpm_chip *chip)
{
	/* XXX: If you wish to remove this restriction, you must first update
	 * tpm_sysfs to explicitly lock chip->ops.
	 */
	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return;

	/* The sysfs routines rely on an implicit tpm_try_get_ops, device_del
	 * is called before ops is null'd and the sysfs core synchronizes this
	 * removal so that no callbacks are running or can run again
	 */
	WARN_ON(chip->groups_cnt != 0);
	chip->groups[chip->groups_cnt++] = &tpm_dev_group;
}
