// SPDX-License-Identifier: GPL-2.0
/*
 *  Inspur WMI Platform Profile
 *
 *  Copyright (C) 2018	      Ai Chao <aichao@kylinos.cn>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_profile.h>
#include <linux/wmi.h>

#define WMI_INSPUR_POWERMODE_BIOS_GUID "596C31E3-332D-43C9-AEE9-585493284F5D"

enum inspur_wmi_method_ids {
	INSPUR_WMI_GET_POWERMODE = 0x02,
	INSPUR_WMI_SET_POWERMODE = 0x03,
};

/*
 * Power Mode:
 *           0x0: Balance Mode
 *           0x1: Performance Mode
 *           0x2: Power Saver Mode
 */
enum inspur_tmp_profile {
	INSPUR_TMP_PROFILE_BALANCE	= 0,
	INSPUR_TMP_PROFILE_PERFORMANCE	= 1,
	INSPUR_TMP_PROFILE_POWERSAVE	= 2,
};

struct inspur_wmi_priv {
	struct wmi_device *wdev;
	struct platform_profile_handler handler;
};

static int inspur_wmi_perform_query(struct wmi_device *wdev,
				    enum inspur_wmi_method_ids query_id,
				    void *buffer, size_t insize,
				    size_t outsize)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input = { insize, buffer};
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	status = wmidev_evaluate_method(wdev, 0, query_id, &input, &output);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "EC Powermode control failed: %s\n",
			acpi_format_exception(status));
		return -EIO;
	}

	obj = output.pointer;
	if (!obj)
		return -EINVAL;

	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length != outsize) {
		ret = -EINVAL;
		goto out_free;
	}

	memcpy(buffer, obj->buffer.pointer, obj->buffer.length);

out_free:
	kfree(obj);
	return ret;
}

/*
 * Set Power Mode to EC RAM. If Power Mode value greater than 0x3,
 * return error
 * Method ID: 0x3
 * Arg: 4 Bytes
 * Byte [0]: Power Mode:
 *         0x0: Balance Mode
 *         0x1: Performance Mode
 *         0x2: Power Saver Mode
 * Return Value: 4 Bytes
 * Byte [0]: Return Code
 *         0x0: No Error
 *         0x1: Error
 */
static int inspur_platform_profile_set(struct platform_profile_handler *pprof,
				       enum platform_profile_option profile)
{
	struct inspur_wmi_priv *priv = container_of(pprof, struct inspur_wmi_priv,
						    handler);
	u8 ret_code[4] = {0, 0, 0, 0};
	int ret;

	switch (profile) {
	case PLATFORM_PROFILE_BALANCED:
		ret_code[0] = INSPUR_TMP_PROFILE_BALANCE;
		break;
	case PLATFORM_PROFILE_PERFORMANCE:
		ret_code[0] = INSPUR_TMP_PROFILE_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
		ret_code[0] = INSPUR_TMP_PROFILE_POWERSAVE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = inspur_wmi_perform_query(priv->wdev, INSPUR_WMI_SET_POWERMODE,
				       ret_code, sizeof(ret_code),
				       sizeof(ret_code));

	if (ret < 0)
		return ret;

	if (ret_code[0])
		return -EBADRQC;

	return 0;
}

/*
 * Get Power Mode from EC RAM, If Power Mode value greater than 0x3,
 * return error
 * Method ID: 0x2
 * Return Value: 4 Bytes
 * Byte [0]: Return Code
 *         0x0: No Error
 *         0x1: Error
 * Byte [1]: Power Mode
 *         0x0: Balance Mode
 *         0x1: Performance Mode
 *         0x2: Power Saver Mode
 */
static int inspur_platform_profile_get(struct platform_profile_handler *pprof,
				       enum platform_profile_option *profile)
{
	struct inspur_wmi_priv *priv = container_of(pprof, struct inspur_wmi_priv,
						    handler);
	u8 ret_code[4] = {0, 0, 0, 0};
	int ret;

	ret = inspur_wmi_perform_query(priv->wdev, INSPUR_WMI_GET_POWERMODE,
				       &ret_code, sizeof(ret_code),
				       sizeof(ret_code));
	if (ret < 0)
		return ret;

	if (ret_code[0])
		return -EBADRQC;

	switch (ret_code[1]) {
	case INSPUR_TMP_PROFILE_BALANCE:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case INSPUR_TMP_PROFILE_PERFORMANCE:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case INSPUR_TMP_PROFILE_POWERSAVE:
		*profile = PLATFORM_PROFILE_LOW_POWER;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int inspur_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct inspur_wmi_priv *priv;

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);

	priv->handler.profile_get = inspur_platform_profile_get;
	priv->handler.profile_set = inspur_platform_profile_set;

	set_bit(PLATFORM_PROFILE_LOW_POWER, priv->handler.choices);
	set_bit(PLATFORM_PROFILE_BALANCED, priv->handler.choices);
	set_bit(PLATFORM_PROFILE_PERFORMANCE, priv->handler.choices);

	return platform_profile_register(&priv->handler);
}

static void inspur_wmi_remove(struct wmi_device *wdev)
{
	platform_profile_remove();
}

static const struct wmi_device_id inspur_wmi_id_table[] = {
	{ .guid_string = WMI_INSPUR_POWERMODE_BIOS_GUID },
	{  }
};

MODULE_DEVICE_TABLE(wmi, inspur_wmi_id_table);

static struct wmi_driver inspur_wmi_driver = {
	.driver = {
		.name = "inspur-wmi-platform-profile",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = inspur_wmi_id_table,
	.probe = inspur_wmi_probe,
	.remove = inspur_wmi_remove,
};

module_wmi_driver(inspur_wmi_driver);

MODULE_AUTHOR("Ai Chao <aichao@kylinos.cn>");
MODULE_DESCRIPTION("Platform Profile Support for Inspur");
MODULE_LICENSE("GPL");
