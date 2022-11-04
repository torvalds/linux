// SPDX-License-Identifier: GPL-2.0-only
/*
 * INT3400 thermal driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include "acpi_thermal_rel.h"

#define INT3400_THERMAL_TABLE_CHANGED 0x83
#define INT3400_ODVP_CHANGED 0x88
#define INT3400_KEEP_ALIVE 0xA0

enum int3400_thermal_uuid {
	INT3400_THERMAL_ACTIVE = 0,
	INT3400_THERMAL_PASSIVE_1,
	INT3400_THERMAL_CRITICAL,
	INT3400_THERMAL_ADAPTIVE_PERFORMANCE,
	INT3400_THERMAL_EMERGENCY_CALL_MODE,
	INT3400_THERMAL_PASSIVE_2,
	INT3400_THERMAL_POWER_BOSS,
	INT3400_THERMAL_VIRTUAL_SENSOR,
	INT3400_THERMAL_COOLING_MODE,
	INT3400_THERMAL_HARDWARE_DUTY_CYCLING,
	INT3400_THERMAL_MAXIMUM_UUID,
};

static char *int3400_thermal_uuids[INT3400_THERMAL_MAXIMUM_UUID] = {
	"3A95C389-E4B8-4629-A526-C52C88626BAE",
	"42A441D6-AE6A-462b-A84B-4A8CE79027D3",
	"97C68AE7-15FA-499c-B8C9-5DA81D606E0A",
	"63BE270F-1C11-48FD-A6F7-3AF253FF3E2D",
	"5349962F-71E6-431D-9AE8-0A635B710AEE",
	"9E04115A-AE87-4D1C-9500-0F3E340BFE75",
	"F5A35014-C209-46A4-993A-EB56DE7530A1",
	"6ED722A7-9240-48A5-B479-31EEF723D7CF",
	"16CAF1B7-DD38-40ED-B1C1-1B8A1913D531",
	"BE84BABF-C4D4-403D-B495-3128FD44dAC1",
};

struct odvp_attr;

struct int3400_thermal_priv {
	struct acpi_device *adev;
	struct platform_device *pdev;
	struct thermal_zone_device *thermal;
	int art_count;
	struct art *arts;
	int trt_count;
	struct trt *trts;
	u32 uuid_bitmap;
	int rel_misc_dev_res;
	int current_uuid_index;
	char *data_vault;
	int odvp_count;
	int *odvp;
	u32 os_uuid_mask;
	struct odvp_attr *odvp_attrs;
};

static int evaluate_odvp(struct int3400_thermal_priv *priv);

struct odvp_attr {
	int odvp;
	struct int3400_thermal_priv *priv;
	struct device_attribute attr;
};

static ssize_t data_vault_read(struct file *file, struct kobject *kobj,
	     struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	memcpy(buf, attr->private + off, count);
	return count;
}

static BIN_ATTR_RO(data_vault, 0);

static struct bin_attribute *data_attributes[] = {
	&bin_attr_data_vault,
	NULL,
};

static ssize_t imok_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct int3400_thermal_priv *priv = dev_get_drvdata(dev);
	acpi_status status;
	int input, ret;

	ret = kstrtouint(buf, 10, &input);
	if (ret)
		return ret;
	status = acpi_execute_simple_method(priv->adev->handle, "IMOK", input);
	if (ACPI_FAILURE(status))
		return -EIO;

	return count;
}

static DEVICE_ATTR_WO(imok);

static struct attribute *imok_attr[] = {
	&dev_attr_imok.attr,
	NULL
};

static const struct attribute_group imok_attribute_group = {
	.attrs = imok_attr,
};

static const struct attribute_group data_attribute_group = {
	.bin_attrs = data_attributes,
};

static ssize_t available_uuids_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct int3400_thermal_priv *priv = dev_get_drvdata(dev);
	int i;
	int length = 0;

	if (!priv->uuid_bitmap)
		return sprintf(buf, "UNKNOWN\n");

	for (i = 0; i < INT3400_THERMAL_MAXIMUM_UUID; i++) {
		if (priv->uuid_bitmap & (1 << i))
			length += scnprintf(&buf[length],
					    PAGE_SIZE - length,
					    "%s\n",
					    int3400_thermal_uuids[i]);
	}

	return length;
}

static ssize_t current_uuid_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct int3400_thermal_priv *priv = dev_get_drvdata(dev);
	int i, length = 0;

	if (priv->current_uuid_index > 0)
		return sprintf(buf, "%s\n",
			       int3400_thermal_uuids[priv->current_uuid_index]);

	for (i = 0; i <= INT3400_THERMAL_CRITICAL; i++) {
		if (priv->os_uuid_mask & BIT(i))
			length += scnprintf(&buf[length],
					    PAGE_SIZE - length,
					    "%s\n",
					    int3400_thermal_uuids[i]);
	}

	if (length)
		return length;

	return sprintf(buf, "INVALID\n");
}

static int int3400_thermal_run_osc(acpi_handle handle, char *uuid_str, int *enable)
{
	u32 ret, buf[2];
	acpi_status status;
	int result = 0;
	struct acpi_osc_context context = {
		.uuid_str = uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = buf,
	};

	buf[OSC_QUERY_DWORD] = 0;
	buf[OSC_SUPPORT_DWORD] = *enable;

	status = acpi_run_osc(handle, &context);
	if (ACPI_SUCCESS(status)) {
		ret = *((u32 *)(context.ret.pointer + 4));
		if (ret != *enable)
			result = -EPERM;

		kfree(context.ret.pointer);
	} else
		result = -EPERM;

	return result;
}

static int set_os_uuid_mask(struct int3400_thermal_priv *priv, u32 mask)
{
	int cap = 0;

	/*
	 * Capability bits:
	 * Bit 0: set to 1 to indicate DPTF is active
	 * Bi1 1: set to 1 to active cooling is supported by user space daemon
	 * Bit 2: set to 1 to passive cooling is supported by user space daemon
	 * Bit 3: set to 1 to critical trip is handled by user space daemon
	 */
	if (mask)
		cap = (priv->os_uuid_mask << 1) | 0x01;

	return int3400_thermal_run_osc(priv->adev->handle,
				       "b23ba85d-c8b7-3542-88de-8de2ffcfd698",
				       &cap);
}

static ssize_t current_uuid_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct int3400_thermal_priv *priv = dev_get_drvdata(dev);
	int ret, i;

	for (i = 0; i < INT3400_THERMAL_MAXIMUM_UUID; ++i) {
		if (!strncmp(buf, int3400_thermal_uuids[i],
			     sizeof(int3400_thermal_uuids[i]) - 1)) {
			/*
			 * If we have a list of supported UUIDs, make sure
			 * this one is supported.
			 */
			if (priv->uuid_bitmap & BIT(i)) {
				priv->current_uuid_index = i;
				return count;
			}

			/*
			 * There is support of only 3 policies via the new
			 * _OSC to inform OS capability:
			 * INT3400_THERMAL_ACTIVE
			 * INT3400_THERMAL_PASSIVE_1
			 * INT3400_THERMAL_CRITICAL
			 */

			if (i > INT3400_THERMAL_CRITICAL)
				return -EINVAL;

			priv->os_uuid_mask |= BIT(i);

			break;
		}
	}

	if (priv->os_uuid_mask) {
		ret = set_os_uuid_mask(priv, priv->os_uuid_mask);
		if (ret)
			return ret;
	}

	return count;
}

static DEVICE_ATTR_RW(current_uuid);
static DEVICE_ATTR_RO(available_uuids);
static struct attribute *uuid_attrs[] = {
	&dev_attr_available_uuids.attr,
	&dev_attr_current_uuid.attr,
	NULL
};

static const struct attribute_group uuid_attribute_group = {
	.attrs = uuid_attrs,
	.name = "uuids"
};

static int int3400_thermal_get_uuids(struct int3400_thermal_priv *priv)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obja, *objb;
	int i, j;
	int result = 0;
	acpi_status status;

	status = acpi_evaluate_object(priv->adev->handle, "IDSP", NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obja = (union acpi_object *)buf.pointer;
	if (obja->type != ACPI_TYPE_PACKAGE) {
		result = -EINVAL;
		goto end;
	}

	for (i = 0; i < obja->package.count; i++) {
		objb = &obja->package.elements[i];
		if (objb->type != ACPI_TYPE_BUFFER) {
			result = -EINVAL;
			goto end;
		}

		/* UUID must be 16 bytes */
		if (objb->buffer.length != 16) {
			result = -EINVAL;
			goto end;
		}

		for (j = 0; j < INT3400_THERMAL_MAXIMUM_UUID; j++) {
			guid_t guid;

			guid_parse(int3400_thermal_uuids[j], &guid);
			if (guid_equal((guid_t *)objb->buffer.pointer, &guid)) {
				priv->uuid_bitmap |= (1 << j);
				break;
			}
		}
	}

end:
	kfree(buf.pointer);
	return result;
}

static ssize_t odvp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct odvp_attr *odvp_attr;

	odvp_attr = container_of(attr, struct odvp_attr, attr);

	return sprintf(buf, "%d\n", odvp_attr->priv->odvp[odvp_attr->odvp]);
}

static void cleanup_odvp(struct int3400_thermal_priv *priv)
{
	int i;

	if (priv->odvp_attrs) {
		for (i = 0; i < priv->odvp_count; i++) {
			sysfs_remove_file(&priv->pdev->dev.kobj,
					  &priv->odvp_attrs[i].attr.attr);
			kfree(priv->odvp_attrs[i].attr.attr.name);
		}
		kfree(priv->odvp_attrs);
	}
	kfree(priv->odvp);
	priv->odvp_count = 0;
}

static int evaluate_odvp(struct int3400_thermal_priv *priv)
{
	struct acpi_buffer odvp = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj = NULL;
	acpi_status status;
	int i, ret;

	status = acpi_evaluate_object(priv->adev->handle, "ODVP", NULL, &odvp);
	if (ACPI_FAILURE(status)) {
		ret = -EINVAL;
		goto out_err;
	}

	obj = odvp.pointer;
	if (obj->type != ACPI_TYPE_PACKAGE) {
		ret = -EINVAL;
		goto out_err;
	}

	if (priv->odvp == NULL) {
		priv->odvp_count = obj->package.count;
		priv->odvp = kmalloc_array(priv->odvp_count, sizeof(int),
				     GFP_KERNEL);
		if (!priv->odvp) {
			ret = -ENOMEM;
			goto out_err;
		}
	}

	if (priv->odvp_attrs == NULL) {
		priv->odvp_attrs = kcalloc(priv->odvp_count,
					   sizeof(struct odvp_attr),
					   GFP_KERNEL);
		if (!priv->odvp_attrs) {
			ret = -ENOMEM;
			goto out_err;
		}
		for (i = 0; i < priv->odvp_count; i++) {
			struct odvp_attr *odvp = &priv->odvp_attrs[i];

			sysfs_attr_init(&odvp->attr.attr);
			odvp->priv = priv;
			odvp->odvp = i;
			odvp->attr.attr.name = kasprintf(GFP_KERNEL,
							 "odvp%d", i);

			if (!odvp->attr.attr.name) {
				ret = -ENOMEM;
				goto out_err;
			}
			odvp->attr.attr.mode = 0444;
			odvp->attr.show = odvp_show;
			odvp->attr.store = NULL;
			ret = sysfs_create_file(&priv->pdev->dev.kobj,
						&odvp->attr.attr);
			if (ret)
				goto out_err;
		}
	}

	for (i = 0; i < obj->package.count; i++) {
		if (obj->package.elements[i].type == ACPI_TYPE_INTEGER)
			priv->odvp[i] = obj->package.elements[i].integer.value;
	}

	kfree(obj);
	return 0;

out_err:
	cleanup_odvp(priv);
	kfree(obj);
	return ret;
}

static void int3400_notify(acpi_handle handle,
			u32 event,
			void *data)
{
	struct int3400_thermal_priv *priv = data;
	char *thermal_prop[5];
	int therm_event;

	if (!priv)
		return;

	switch (event) {
	case INT3400_THERMAL_TABLE_CHANGED:
		therm_event = THERMAL_TABLE_CHANGED;
		break;
	case INT3400_KEEP_ALIVE:
		therm_event = THERMAL_EVENT_KEEP_ALIVE;
		break;
	case INT3400_ODVP_CHANGED:
		evaluate_odvp(priv);
		therm_event = THERMAL_DEVICE_POWER_CAPABILITY_CHANGED;
		break;
	default:
		/* Ignore unknown notification codes sent to INT3400 device */
		return;
	}

	thermal_prop[0] = kasprintf(GFP_KERNEL, "NAME=%s", priv->thermal->type);
	thermal_prop[1] = kasprintf(GFP_KERNEL, "TEMP=%d", priv->thermal->temperature);
	thermal_prop[2] = kasprintf(GFP_KERNEL, "TRIP=");
	thermal_prop[3] = kasprintf(GFP_KERNEL, "EVENT=%d", therm_event);
	thermal_prop[4] = NULL;
	kobject_uevent_env(&priv->thermal->device.kobj, KOBJ_CHANGE, thermal_prop);
	kfree(thermal_prop[0]);
	kfree(thermal_prop[1]);
	kfree(thermal_prop[2]);
	kfree(thermal_prop[3]);
}

static int int3400_thermal_get_temp(struct thermal_zone_device *thermal,
			int *temp)
{
	*temp = 20 * 1000; /* faked temp sensor with 20C */
	return 0;
}

static int int3400_thermal_change_mode(struct thermal_zone_device *thermal,
				       enum thermal_device_mode mode)
{
	struct int3400_thermal_priv *priv = thermal->devdata;
	int result = 0;

	if (!priv)
		return -EINVAL;

	if (mode != thermal->mode) {
		int enabled;

		enabled = mode == THERMAL_DEVICE_ENABLED;

		if (priv->os_uuid_mask) {
			if (!enabled) {
				priv->os_uuid_mask = 0;
				result = set_os_uuid_mask(priv, priv->os_uuid_mask);
			}
			goto eval_odvp;
		}

		if (priv->current_uuid_index < 0 ||
		    priv->current_uuid_index >= INT3400_THERMAL_MAXIMUM_UUID)
			return -EINVAL;

		result = int3400_thermal_run_osc(priv->adev->handle,
						 int3400_thermal_uuids[priv->current_uuid_index],
						 &enabled);
	}

eval_odvp:
	evaluate_odvp(priv);

	return result;
}

static struct thermal_zone_device_ops int3400_thermal_ops = {
	.get_temp = int3400_thermal_get_temp,
	.change_mode = int3400_thermal_change_mode,
};

static struct thermal_zone_params int3400_thermal_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

static void int3400_setup_gddv(struct int3400_thermal_priv *priv)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = acpi_evaluate_object(priv->adev->handle, "GDDV", NULL,
				      &buffer);
	if (ACPI_FAILURE(status) || !buffer.length)
		return;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 1
	    || obj->package.elements[0].type != ACPI_TYPE_BUFFER)
		goto out_free;

	priv->data_vault = kmemdup(obj->package.elements[0].buffer.pointer,
				   obj->package.elements[0].buffer.length,
				   GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(priv->data_vault))
		goto out_free;

	bin_attr_data_vault.private = priv->data_vault;
	bin_attr_data_vault.size = obj->package.elements[0].buffer.length;
out_free:
	kfree(buffer.pointer);
}

static int int3400_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3400_thermal_priv *priv;
	int result;

	if (!adev)
		return -ENODEV;

	priv = kzalloc(sizeof(struct int3400_thermal_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->adev = adev;

	result = int3400_thermal_get_uuids(priv);

	/* Missing IDSP isn't fatal */
	if (result && result != -ENODEV)
		goto free_priv;

	priv->current_uuid_index = -1;

	result = acpi_parse_art(priv->adev->handle, &priv->art_count,
				&priv->arts, true);
	if (result)
		dev_dbg(&pdev->dev, "_ART table parsing error\n");

	result = acpi_parse_trt(priv->adev->handle, &priv->trt_count,
				&priv->trts, true);
	if (result)
		dev_dbg(&pdev->dev, "_TRT table parsing error\n");

	platform_set_drvdata(pdev, priv);

	int3400_setup_gddv(priv);

	evaluate_odvp(priv);

	priv->thermal = thermal_zone_device_register("INT3400 Thermal", 0, 0,
						priv, &int3400_thermal_ops,
						&int3400_thermal_params, 0, 0);
	if (IS_ERR(priv->thermal)) {
		result = PTR_ERR(priv->thermal);
		goto free_art_trt;
	}

	priv->rel_misc_dev_res = acpi_thermal_rel_misc_device_add(
							priv->adev->handle);

	result = sysfs_create_group(&pdev->dev.kobj, &uuid_attribute_group);
	if (result)
		goto free_rel_misc;

	if (acpi_has_method(priv->adev->handle, "IMOK")) {
		result = sysfs_create_group(&pdev->dev.kobj, &imok_attribute_group);
		if (result)
			goto free_imok;
	}

	if (!ZERO_OR_NULL_PTR(priv->data_vault)) {
		result = sysfs_create_group(&pdev->dev.kobj,
					    &data_attribute_group);
		if (result)
			goto free_uuid;
	}

	result = acpi_install_notify_handler(
			priv->adev->handle, ACPI_DEVICE_NOTIFY, int3400_notify,
			(void *)priv);
	if (result)
		goto free_sysfs;

	return 0;

free_sysfs:
	cleanup_odvp(priv);
	if (!ZERO_OR_NULL_PTR(priv->data_vault)) {
		sysfs_remove_group(&pdev->dev.kobj, &data_attribute_group);
		kfree(priv->data_vault);
	}
free_uuid:
	sysfs_remove_group(&pdev->dev.kobj, &uuid_attribute_group);
free_imok:
	sysfs_remove_group(&pdev->dev.kobj, &imok_attribute_group);
free_rel_misc:
	if (!priv->rel_misc_dev_res)
		acpi_thermal_rel_misc_device_remove(priv->adev->handle);
	thermal_zone_device_unregister(priv->thermal);
free_art_trt:
	kfree(priv->trts);
	kfree(priv->arts);
free_priv:
	kfree(priv);
	return result;
}

static int int3400_thermal_remove(struct platform_device *pdev)
{
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);

	acpi_remove_notify_handler(
			priv->adev->handle, ACPI_DEVICE_NOTIFY,
			int3400_notify);

	cleanup_odvp(priv);

	if (!priv->rel_misc_dev_res)
		acpi_thermal_rel_misc_device_remove(priv->adev->handle);

	if (!ZERO_OR_NULL_PTR(priv->data_vault))
		sysfs_remove_group(&pdev->dev.kobj, &data_attribute_group);
	sysfs_remove_group(&pdev->dev.kobj, &uuid_attribute_group);
	sysfs_remove_group(&pdev->dev.kobj, &imok_attribute_group);
	thermal_zone_device_unregister(priv->thermal);
	kfree(priv->data_vault);
	kfree(priv->trts);
	kfree(priv->arts);
	kfree(priv);
	return 0;
}

static const struct acpi_device_id int3400_thermal_match[] = {
	{"INT3400", 0},
	{"INTC1040", 0},
	{"INTC1041", 0},
	{"INTC1042", 0},
	{"INTC10A0", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3400_thermal_match);

static struct platform_driver int3400_thermal_driver = {
	.probe = int3400_thermal_probe,
	.remove = int3400_thermal_remove,
	.driver = {
		   .name = "int3400 thermal",
		   .acpi_match_table = ACPI_PTR(int3400_thermal_match),
		   },
};

module_platform_driver(int3400_thermal_driver);

MODULE_DESCRIPTION("INT3400 Thermal driver");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL");
