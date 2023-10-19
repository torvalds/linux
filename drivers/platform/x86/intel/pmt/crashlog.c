// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitoring Technology Crashlog driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "Alexander Duyck" <alexander.h.duyck@linux.intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/overflow.h>

#include "../vsec.h"
#include "class.h"

/* Crashlog discovery header types */
#define CRASH_TYPE_OOBMSM	1

/* Control Flags */
#define CRASHLOG_FLAG_DISABLE		BIT(28)

/*
 * Bits 29 and 30 control the state of bit 31.
 *
 * Bit 29 will clear bit 31, if set, allowing a new crashlog to be captured.
 * Bit 30 will immediately trigger a crashlog to be generated, setting bit 31.
 * Bit 31 is the read-only status with a 1 indicating log is complete.
 */
#define CRASHLOG_FLAG_TRIGGER_CLEAR	BIT(29)
#define CRASHLOG_FLAG_TRIGGER_EXECUTE	BIT(30)
#define CRASHLOG_FLAG_TRIGGER_COMPLETE	BIT(31)
#define CRASHLOG_FLAG_TRIGGER_MASK	GENMASK(31, 28)

/* Crashlog Discovery Header */
#define CONTROL_OFFSET		0x0
#define GUID_OFFSET		0x4
#define BASE_OFFSET		0x8
#define SIZE_OFFSET		0xC
#define GET_ACCESS(v)		((v) & GENMASK(3, 0))
#define GET_TYPE(v)		(((v) & GENMASK(7, 4)) >> 4)
#define GET_VERSION(v)		(((v) & GENMASK(19, 16)) >> 16)
/* size is in bytes */
#define GET_SIZE(v)		((v) * sizeof(u32))

struct crashlog_entry {
	/* entry must be first member of struct */
	struct intel_pmt_entry		entry;
	struct mutex			control_mutex;
};

struct pmt_crashlog_priv {
	int			num_entries;
	struct crashlog_entry	entry[];
};

/*
 * I/O
 */
static bool pmt_crashlog_complete(struct intel_pmt_entry *entry)
{
	u32 control = readl(entry->disc_table + CONTROL_OFFSET);

	/* return current value of the crashlog complete flag */
	return !!(control & CRASHLOG_FLAG_TRIGGER_COMPLETE);
}

static bool pmt_crashlog_disabled(struct intel_pmt_entry *entry)
{
	u32 control = readl(entry->disc_table + CONTROL_OFFSET);

	/* return current value of the crashlog disabled flag */
	return !!(control & CRASHLOG_FLAG_DISABLE);
}

static bool pmt_crashlog_supported(struct intel_pmt_entry *entry)
{
	u32 discovery_header = readl(entry->disc_table + CONTROL_OFFSET);
	u32 crash_type, version;

	crash_type = GET_TYPE(discovery_header);
	version = GET_VERSION(discovery_header);

	/*
	 * Currently we only recognize OOBMSM version 0 devices.
	 * We can ignore all other crashlog devices in the system.
	 */
	return crash_type == CRASH_TYPE_OOBMSM && version == 0;
}

static void pmt_crashlog_set_disable(struct intel_pmt_entry *entry,
				     bool disable)
{
	u32 control = readl(entry->disc_table + CONTROL_OFFSET);

	/* clear trigger bits so we are only modifying disable flag */
	control &= ~CRASHLOG_FLAG_TRIGGER_MASK;

	if (disable)
		control |= CRASHLOG_FLAG_DISABLE;
	else
		control &= ~CRASHLOG_FLAG_DISABLE;

	writel(control, entry->disc_table + CONTROL_OFFSET);
}

static void pmt_crashlog_set_clear(struct intel_pmt_entry *entry)
{
	u32 control = readl(entry->disc_table + CONTROL_OFFSET);

	control &= ~CRASHLOG_FLAG_TRIGGER_MASK;
	control |= CRASHLOG_FLAG_TRIGGER_CLEAR;

	writel(control, entry->disc_table + CONTROL_OFFSET);
}

static void pmt_crashlog_set_execute(struct intel_pmt_entry *entry)
{
	u32 control = readl(entry->disc_table + CONTROL_OFFSET);

	control &= ~CRASHLOG_FLAG_TRIGGER_MASK;
	control |= CRASHLOG_FLAG_TRIGGER_EXECUTE;

	writel(control, entry->disc_table + CONTROL_OFFSET);
}

/*
 * sysfs
 */
static ssize_t
enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct intel_pmt_entry *entry = dev_get_drvdata(dev);
	int enabled = !pmt_crashlog_disabled(entry);

	return sprintf(buf, "%d\n", enabled);
}

static ssize_t
enable_store(struct device *dev, struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct crashlog_entry *entry;
	bool enabled;
	int result;

	entry = dev_get_drvdata(dev);

	result = kstrtobool(buf, &enabled);
	if (result)
		return result;

	mutex_lock(&entry->control_mutex);
	pmt_crashlog_set_disable(&entry->entry, !enabled);
	mutex_unlock(&entry->control_mutex);

	return count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t
trigger_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct intel_pmt_entry *entry;
	int trigger;

	entry = dev_get_drvdata(dev);
	trigger = pmt_crashlog_complete(entry);

	return sprintf(buf, "%d\n", trigger);
}

static ssize_t
trigger_store(struct device *dev, struct device_attribute *attr,
	    const char *buf, size_t count)
{
	struct crashlog_entry *entry;
	bool trigger;
	int result;

	entry = dev_get_drvdata(dev);

	result = kstrtobool(buf, &trigger);
	if (result)
		return result;

	mutex_lock(&entry->control_mutex);

	if (!trigger) {
		pmt_crashlog_set_clear(&entry->entry);
	} else if (pmt_crashlog_complete(&entry->entry)) {
		/* we cannot trigger a new crash if one is still pending */
		result = -EEXIST;
		goto err;
	} else if (pmt_crashlog_disabled(&entry->entry)) {
		/* if device is currently disabled, return busy */
		result = -EBUSY;
		goto err;
	} else {
		pmt_crashlog_set_execute(&entry->entry);
	}

	result = count;
err:
	mutex_unlock(&entry->control_mutex);
	return result;
}
static DEVICE_ATTR_RW(trigger);

static struct attribute *pmt_crashlog_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_trigger.attr,
	NULL
};

static const struct attribute_group pmt_crashlog_group = {
	.attrs	= pmt_crashlog_attrs,
};

static int pmt_crashlog_header_decode(struct intel_pmt_entry *entry,
				      struct intel_pmt_header *header,
				      struct device *dev)
{
	void __iomem *disc_table = entry->disc_table;
	struct crashlog_entry *crashlog;

	if (!pmt_crashlog_supported(entry))
		return 1;

	/* initialize control mutex */
	crashlog = container_of(entry, struct crashlog_entry, entry);
	mutex_init(&crashlog->control_mutex);

	header->access_type = GET_ACCESS(readl(disc_table));
	header->guid = readl(disc_table + GUID_OFFSET);
	header->base_offset = readl(disc_table + BASE_OFFSET);

	/* Size is measured in DWORDS, but accessor returns bytes */
	header->size = GET_SIZE(readl(disc_table + SIZE_OFFSET));

	return 0;
}

static DEFINE_XARRAY_ALLOC(crashlog_array);
static struct intel_pmt_namespace pmt_crashlog_ns = {
	.name = "crashlog",
	.xa = &crashlog_array,
	.attr_grp = &pmt_crashlog_group,
	.pmt_header_decode = pmt_crashlog_header_decode,
};

/*
 * initialization
 */
static void pmt_crashlog_remove(struct auxiliary_device *auxdev)
{
	struct pmt_crashlog_priv *priv = auxiliary_get_drvdata(auxdev);
	int i;

	for (i = 0; i < priv->num_entries; i++)
		intel_pmt_dev_destroy(&priv->entry[i].entry, &pmt_crashlog_ns);
}

static int pmt_crashlog_probe(struct auxiliary_device *auxdev,
			      const struct auxiliary_device_id *id)
{
	struct intel_vsec_device *intel_vsec_dev = auxdev_to_ivdev(auxdev);
	struct pmt_crashlog_priv *priv;
	size_t size;
	int i, ret;

	size = struct_size(priv, entry, intel_vsec_dev->num_resources);
	priv = devm_kzalloc(&auxdev->dev, size, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	auxiliary_set_drvdata(auxdev, priv);

	for (i = 0; i < intel_vsec_dev->num_resources; i++) {
		struct intel_pmt_entry *entry = &priv->entry[priv->num_entries].entry;

		ret = intel_pmt_dev_create(entry, &pmt_crashlog_ns, intel_vsec_dev, i);
		if (ret < 0)
			goto abort_probe;
		if (ret)
			continue;

		priv->num_entries++;
	}

	return 0;
abort_probe:
	pmt_crashlog_remove(auxdev);
	return ret;
}

static const struct auxiliary_device_id pmt_crashlog_id_table[] = {
	{ .name = "intel_vsec.crashlog" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, pmt_crashlog_id_table);

static struct auxiliary_driver pmt_crashlog_aux_driver = {
	.id_table	= pmt_crashlog_id_table,
	.remove		= pmt_crashlog_remove,
	.probe		= pmt_crashlog_probe,
};

static int __init pmt_crashlog_init(void)
{
	return auxiliary_driver_register(&pmt_crashlog_aux_driver);
}

static void __exit pmt_crashlog_exit(void)
{
	auxiliary_driver_unregister(&pmt_crashlog_aux_driver);
	xa_destroy(&crashlog_array);
}

module_init(pmt_crashlog_init);
module_exit(pmt_crashlog_exit);

MODULE_AUTHOR("Alexander Duyck <alexander.h.duyck@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Crashlog driver");
MODULE_LICENSE("GPL v2");
