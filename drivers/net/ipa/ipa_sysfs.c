// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2021-2024 Linaro Ltd. */

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "ipa.h"
#include "ipa_sysfs.h"
#include "ipa_version.h"

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
	case IPA_VERSION_5_0:
		return "5.0";
	default:
		return "0.0";	/* Won't happen (checked at probe time) */
	}
}

static ssize_t
version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ipa_version_string(ipa));
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

	return sysfs_emit(buf, "%s\n", ipa_offload_string(ipa));
}

static DEVICE_ATTR_RO(rx_offload);

static ssize_t tx_offload_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ipa_offload_string(ipa));
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

static umode_t ipa_endpoint_id_is_visible(struct kobject *kobj,
					  struct attribute *attr, int n)
{
	struct ipa *ipa = dev_get_drvdata(kobj_to_dev(kobj));
	struct device_attribute *dev_attr;
	struct dev_ext_attribute *ea;
	bool visible;

	/* An endpoint id attribute is only visible if it's defined */
	dev_attr = container_of(attr, struct device_attribute, attr);
	ea = container_of(dev_attr, struct dev_ext_attribute, attr);

	visible = !!ipa->name_map[(enum ipa_endpoint_name)(uintptr_t)ea->var];

	return visible ? attr->mode : 0;
}

static ssize_t endpoint_id_attr_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ipa *ipa = dev_get_drvdata(dev);
	struct ipa_endpoint *endpoint;
	struct dev_ext_attribute *ea;

	ea = container_of(attr, struct dev_ext_attribute, attr);
	endpoint = ipa->name_map[(enum ipa_endpoint_name)(uintptr_t)ea->var];

	return sysfs_emit(buf, "%u\n", endpoint->endpoint_id);
}

#define ENDPOINT_ID_ATTR(_n, _endpoint_name)				    \
	static struct dev_ext_attribute dev_attr_endpoint_id_ ## _n = {	    \
		.attr	= __ATTR(_n, 0444, endpoint_id_attr_show, NULL),    \
		.var	= (void *)(_endpoint_name),			    \
	}

ENDPOINT_ID_ATTR(modem_rx, IPA_ENDPOINT_AP_MODEM_RX);
ENDPOINT_ID_ATTR(modem_tx, IPA_ENDPOINT_AP_MODEM_TX);

static struct attribute *ipa_endpoint_id_attrs[] = {
	&dev_attr_endpoint_id_modem_rx.attr.attr,
	&dev_attr_endpoint_id_modem_tx.attr.attr,
	NULL
};

const struct attribute_group ipa_endpoint_id_attribute_group = {
	.name		= "endpoint_id",
	.is_visible	= ipa_endpoint_id_is_visible,
	.attrs		= ipa_endpoint_id_attrs,
};

/* Reuse endpoint ID attributes for the legacy modem endpoint IDs */
#define MODEM_ATTR(_n, _endpoint_name)					    \
	static struct dev_ext_attribute dev_attr_modem_ ## _n = {	    \
		.attr	= __ATTR(_n, 0444, endpoint_id_attr_show, NULL),    \
		.var	= (void *)(_endpoint_name),			    \
	}

MODEM_ATTR(rx_endpoint_id, IPA_ENDPOINT_AP_MODEM_RX);
MODEM_ATTR(tx_endpoint_id, IPA_ENDPOINT_AP_MODEM_TX);

static struct attribute *ipa_modem_attrs[] = {
	&dev_attr_modem_rx_endpoint_id.attr.attr,
	&dev_attr_modem_tx_endpoint_id.attr.attr,
	NULL,
};

const struct attribute_group ipa_modem_attribute_group = {
	.name		= "modem",
	.attrs		= ipa_modem_attrs,
};
