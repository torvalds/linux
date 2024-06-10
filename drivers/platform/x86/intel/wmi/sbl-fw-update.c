// SPDX-License-Identifier: GPL-2.0
/*
 * Slim Bootloader(SBL) firmware update signaling driver
 *
 * Slim Bootloader is a small, open-source, non UEFI compliant, boot firmware
 * optimized for running on certain Intel platforms.
 *
 * SBL exposes an ACPI-WMI device via /sys/bus/wmi/devices/<INTEL_WMI_SBL_GUID>.
 * This driver further adds "firmware_update_request" device attribute.
 * This attribute normally has a value of 0 and userspace can signal SBL
 * to update firmware, on next reboot, by writing a value of 1.
 *
 * More details of SBL firmware update process is available at:
 * https://slimbootloader.github.io/security/firmware-update.html
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/wmi.h>

#define INTEL_WMI_SBL_GUID  "44FADEB1-B204-40F2-8581-394BBDC1B651"

static int get_fwu_request(struct device *dev, u32 *out)
{
	union acpi_object *obj;

	obj = wmidev_block_query(to_wmi_device(dev), 0);
	if (!obj)
		return -ENODEV;

	if (obj->type != ACPI_TYPE_INTEGER) {
		dev_warn(dev, "wmidev_block_query returned invalid value\n");
		kfree(obj);
		return -EINVAL;
	}

	*out = obj->integer.value;
	kfree(obj);

	return 0;
}

static int set_fwu_request(struct device *dev, u32 in)
{
	struct acpi_buffer input;
	acpi_status status;
	u32 value;

	value = in;
	input.length = sizeof(u32);
	input.pointer = &value;

	status = wmidev_block_set(to_wmi_device(dev), 0, &input);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "wmidev_block_set failed\n");
		return -ENODEV;
	}

	return 0;
}

static ssize_t firmware_update_request_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	u32 val;
	int ret;

	ret = get_fwu_request(dev, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t firmware_update_request_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	/* May later be extended to support values other than 0 and 1 */
	if (val > 1)
		return -ERANGE;

	ret = set_fwu_request(dev, val);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(firmware_update_request);

static struct attribute *firmware_update_attrs[] = {
	&dev_attr_firmware_update_request.attr,
	NULL
};
ATTRIBUTE_GROUPS(firmware_update);

static int intel_wmi_sbl_fw_update_probe(struct wmi_device *wdev,
					 const void *context)
{
	dev_info(&wdev->dev, "Slim Bootloader signaling driver attached\n");
	return 0;
}

static void intel_wmi_sbl_fw_update_remove(struct wmi_device *wdev)
{
	dev_info(&wdev->dev, "Slim Bootloader signaling driver removed\n");
}

static const struct wmi_device_id intel_wmi_sbl_id_table[] = {
	{ .guid_string = INTEL_WMI_SBL_GUID },
	{}
};
MODULE_DEVICE_TABLE(wmi, intel_wmi_sbl_id_table);

static struct wmi_driver intel_wmi_sbl_fw_update_driver = {
	.driver = {
		.name = "intel-wmi-sbl-fw-update",
		.dev_groups = firmware_update_groups,
	},
	.probe = intel_wmi_sbl_fw_update_probe,
	.remove = intel_wmi_sbl_fw_update_remove,
	.id_table = intel_wmi_sbl_id_table,
	.no_singleton = true,
};
module_wmi_driver(intel_wmi_sbl_fw_update_driver);

MODULE_AUTHOR("Jithu Joseph <jithu.joseph@intel.com>");
MODULE_DESCRIPTION("Slim Bootloader firmware update signaling driver");
MODULE_LICENSE("GPL v2");
