/*
 *  Driver for the LID cover switch of the Surface 3
 *
 *  Copyright (c) 2016 Red Hat Inc.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@redhat.com>");
MODULE_DESCRIPTION("Surface 3 platform driver");
MODULE_LICENSE("GPL");

#define ACPI_BUTTON_HID_LID		"PNP0C0D"
#define SPI_CTL_OBJ_NAME		"SPI"
#define SPI_TS_OBJ_NAME			"NTRG"

#define SURFACE3_LID_GUID "F7CC25EC-D20B-404C-8903-0ED4359C18AE"

MODULE_ALIAS("wmi:" SURFACE3_LID_GUID);

static const struct dmi_system_id surface3_dmi_table[] = {
#if defined(CONFIG_X86)
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Surface 3"),
		},
	},
#endif
	{ }
};

struct surface3_wmi {
	struct acpi_device *touchscreen_adev;
	struct acpi_device *pnp0c0d_adev;
	struct acpi_hotplug_context hp;
	struct input_dev *input;
};

static struct platform_device *s3_wmi_pdev;

static struct surface3_wmi s3_wmi;

static DEFINE_MUTEX(s3_wmi_lock);

static int s3_wmi_query_block(const char *guid, int instance, int *ret)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	union acpi_object *obj;
	int error = 0;

	mutex_lock(&s3_wmi_lock);
	status = wmi_query_block(guid, instance, &output);

	obj = output.pointer;

	if (!obj || obj->type != ACPI_TYPE_INTEGER) {
		if (obj) {
			pr_err("query block returned object type: %d - buffer length:%d\n",
			       obj->type,
			       obj->type == ACPI_TYPE_BUFFER ?
						obj->buffer.length : 0);
		}
		error = -EINVAL;
		goto out_free_unlock;
	}
	*ret = obj->integer.value;
 out_free_unlock:
	kfree(obj);
	mutex_unlock(&s3_wmi_lock);
	return error;
}

static inline int s3_wmi_query_lid(int *ret)
{
	return s3_wmi_query_block(SURFACE3_LID_GUID, 0, ret);
}

static int s3_wmi_send_lid_state(void)
{
	int ret, lid_sw;

	ret = s3_wmi_query_lid(&lid_sw);
	if (ret)
		return ret;

	input_report_switch(s3_wmi.input, SW_LID, lid_sw);
	input_sync(s3_wmi.input);

	return 0;
}

static int s3_wmi_hp_notify(struct acpi_device *adev, u32 value)
{
	return s3_wmi_send_lid_state();
}

static acpi_status s3_wmi_attach_spi_device(acpi_handle handle,
					    u32 level,
					    void *data,
					    void **return_value)
{
	struct acpi_device *adev, **ts_adev;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	ts_adev = data;

	if (strncmp(acpi_device_bid(adev), SPI_TS_OBJ_NAME,
	    strlen(SPI_TS_OBJ_NAME)))
		return AE_OK;

	if (*ts_adev) {
		pr_err("duplicate entry %s\n", SPI_TS_OBJ_NAME);
		return AE_OK;
	}

	*ts_adev = adev;

	return AE_OK;
}

static int s3_wmi_check_platform_device(struct device *dev, void *data)
{
	struct acpi_device *adev, *ts_adev = NULL;
	acpi_handle handle;
	acpi_status status;

	/* ignore non ACPI devices */
	handle = ACPI_HANDLE(dev);
	if (!handle || acpi_bus_get_device(handle, &adev))
		return 0;

	/* check for LID ACPI switch */
	if (!strcmp(ACPI_BUTTON_HID_LID, acpi_device_hid(adev))) {
		s3_wmi.pnp0c0d_adev = adev;
		return 0;
	}

	/* ignore non SPI controllers */
	if (strncmp(acpi_device_bid(adev), SPI_CTL_OBJ_NAME,
	    strlen(SPI_CTL_OBJ_NAME)))
		return 0;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     s3_wmi_attach_spi_device, NULL,
				     &ts_adev, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(dev, "failed to enumerate SPI slaves\n");

	if (!ts_adev)
		return 0;

	s3_wmi.touchscreen_adev = ts_adev;

	return 0;
}

static int s3_wmi_create_and_register_input(struct platform_device *pdev)
{
	struct input_dev *input;
	int error;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "Lid Switch";
	input->phys = "button/input0";
	input->id.bustype = BUS_HOST;
	input->id.product = 0x0005;

	input_set_capability(input, EV_SW, SW_LID);

	error = input_register_device(input);
	if (error)
		goto out_err;

	s3_wmi.input = input;

	return 0;
 out_err:
	input_free_device(s3_wmi.input);
	return error;
}

static int __init s3_wmi_probe(struct platform_device *pdev)
{
	int error;

	if (!dmi_check_system(surface3_dmi_table))
		return -ENODEV;

	memset(&s3_wmi, 0, sizeof(s3_wmi));

	bus_for_each_dev(&platform_bus_type, NULL, NULL,
			 s3_wmi_check_platform_device);

	if (!s3_wmi.touchscreen_adev)
		return -ENODEV;

	acpi_bus_trim(s3_wmi.pnp0c0d_adev);

	error = s3_wmi_create_and_register_input(pdev);
	if (error)
		goto restore_acpi_lid;

	acpi_initialize_hp_context(s3_wmi.touchscreen_adev, &s3_wmi.hp,
				   s3_wmi_hp_notify, NULL);

	s3_wmi_send_lid_state();

	return 0;

 restore_acpi_lid:
	acpi_bus_scan(s3_wmi.pnp0c0d_adev->handle);
	return error;
}

static int s3_wmi_remove(struct platform_device *device)
{
	/* remove the hotplug context from the acpi device */
	s3_wmi.touchscreen_adev->hp = NULL;

	/* reinstall the actual PNPC0C0D LID default handle */
	acpi_bus_scan(s3_wmi.pnp0c0d_adev->handle);
	return 0;
}

static int __maybe_unused s3_wmi_resume(struct device *dev)
{
	s3_wmi_send_lid_state();
	return 0;
}
static SIMPLE_DEV_PM_OPS(s3_wmi_pm, NULL, s3_wmi_resume);

static struct platform_driver s3_wmi_driver = {
	.driver = {
		.name = "surface3-wmi",
		.pm = &s3_wmi_pm,
	},
	.remove = s3_wmi_remove,
};

static int __init s3_wmi_init(void)
{
	int error;

	s3_wmi_pdev = platform_device_alloc("surface3-wmi", -1);
	if (!s3_wmi_pdev)
		return -ENOMEM;

	error = platform_device_add(s3_wmi_pdev);
	if (error)
		goto err_device_put;

	error = platform_driver_probe(&s3_wmi_driver, s3_wmi_probe);
	if (error)
		goto err_device_del;

	pr_info("Surface 3 WMI Extras loaded\n");
	return 0;

 err_device_del:
	platform_device_del(s3_wmi_pdev);
 err_device_put:
	platform_device_put(s3_wmi_pdev);
	return error;
}

static void __exit s3_wmi_exit(void)
{
	platform_device_unregister(s3_wmi_pdev);
	platform_driver_unregister(&s3_wmi_driver);
}

module_init(s3_wmi_init);
module_exit(s3_wmi_exit);
