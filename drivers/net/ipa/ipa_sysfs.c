// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2021 Linaro Ltd. */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>

#include "ipa.h"
#include "ipa_version.h"
#include "ipa_sysfs.h"

static const char *ipa_version_string(struct ipa *ipa)
{
	switch (ipa->version) {
	case IPA_VERSION_3_0:
		return "3.0";
	case IPA_VERSION_3_1:
		return "3.1";
	case IPA_VERSION_3_5:
		return "3.5";
	case IPA_VERSION_3_5_1:
		return "3.5.1";
	case IPA_VERSION_4_0:
		return "4.0";
	case IPA_VERSION_4_1:
		return "4.1";
	case IPA_VERSION_4_2:
		return "4.2";
	case IPA_VERSION_4_5:
		return "4.5";
	case IPA_VERSION_4_7:
		return "4.7";
	case IPA_VERSION_4_9:
		return "4.9";
	case IPA_VERSION_4_11:
		return "4.11";
	default:
		return "0.0";	/* Won't happen (checked at probe time) */
	}
}

static ssize_t
version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", ipa_version_string(ipa));
}

static DEVICE_ATTR_RO(version);

static struct attribute *ipa_attrs[] = {
	&dev_attr_version.attr,
	NULL
};

const struct attribute_group ipa_attribute_group = {
	.attrs		= ipa_attrs,
};

static const char *ipa_offload_string(struct ipa *ipa)
{
	return ipa->version < IPA_VERSION_4_5 ? "MAPv4" : "MAPv5";
}

static ssize_t rx_offload_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", ipa_offload_string(ipa));
}

static DEVICE_ATTR_RO(rx_offload);

static ssize_t tx_offload_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", ipa_offload_string(ipa));
}

static DEVICE_ATTR_RO(tx_offload);

static struct attribute *ipa_feature_attrs[] = {
	&dev_attr_rx_offload.attr,
	&dev_attr_tx_offload.attr,
	NULL
};

const struct attribute_group ipa_feature_attribute_group = {
	.name		= "feature",
	.attrs		= ipa_feature_attrs,
};

static ssize_t
ipa_endpoint_id_show(struct ipa *ipa, char *buf, enum ipa_endpoint_name name)
{
	u32 endpoint_id = ipa->name_map[name]->endpoint_id;

	return scnprintf(buf, PAGE_SIZE, "%u\n", endpoint_id);
}

static ssize_t rx_endpoint_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return ipa_endpoint_id_show(ipa, buf, IPA_ENDPOINT_AP_MODEM_RX);
}

static DEVICE_ATTR_RO(rx_endpoint_id);

static ssize_t tx_endpoint_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return ipa_endpoint_id_show(ipa, buf, IPA_ENDPOINT_AP_MODEM_TX);
}

static DEVICE_ATTR_RO(tx_endpoint_id);

static struct attribute *ipa_modem_attrs[] = {
	&dev_attr_rx_endpoint_id.attr,
	&dev_attr_tx_endpoint_id.attr,
	NULL
};

const struct attribute_group ipa_modem_attribute_group = {
	.name		= "modem",
	.attrs		= ipa_modem_attrs,
};
