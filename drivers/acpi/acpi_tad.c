// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI Time and Alarm (TAD) Device Driver
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This driver is based on Section 9.18 of the ACPI 6.2 specification revision.
 *
 * It only supports the system wakeup capabilities of the TAD.
 *
 * Provided are sysfs attributes, available under the TAD platform device,
 * allowing user space to manage the AC and DC wakeup timers of the TAD:
 * set and read their values, set and check their expire timer wake policies,
 * check and clear their status and check the capabilities of the TAD reported
 * by AML.  The DC timer attributes are only present if the TAD supports a
 * separate DC alarm timer.
 *
 * The wakeup events handling and power management of the TAD is expected to
 * be taken care of by the ACPI PM domain attached to its platform device.
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rafael J. Wysocki");

/* ACPI TAD capability flags (ACPI 6.2, Section 9.18.2) */
#define ACPI_TAD_AC_WAKE	BIT(0)
#define ACPI_TAD_DC_WAKE	BIT(1)
#define ACPI_TAD_RT		BIT(2)
#define ACPI_TAD_RT_IN_MS	BIT(3)
#define ACPI_TAD_S4_S5__GWS	BIT(4)
#define ACPI_TAD_AC_S4_WAKE	BIT(5)
#define ACPI_TAD_AC_S5_WAKE	BIT(6)
#define ACPI_TAD_DC_S4_WAKE	BIT(7)
#define ACPI_TAD_DC_S5_WAKE	BIT(8)

/* ACPI TAD alarm timer selection */
#define ACPI_TAD_AC_TIMER	(u32)0
#define ACPI_TAD_DC_TIMER	(u32)1

/* Special value for disabled timer or expired timer wake policy. */
#define ACPI_TAD_WAKE_DISABLED	(~(u32)0)

struct acpi_tad_driver_data {
	u32 capabilities;
};

struct acpi_tad_rt {
	u16 year;  /* 1900 - 9999 */
	u8 month;  /* 1 - 12 */
	u8 day;    /* 1 - 31 */
	u8 hour;   /* 0 - 23 */
	u8 minute; /* 0 - 59 */
	u8 second; /* 0 - 59 */
	u8 valid;  /* 0 (failed) or 1 (success) for reads, 0 for writes */
	u16 msec;  /* 1 - 1000 */
	s16 tz;    /* -1440 to 1440 or 2047 (unspecified) */
	u8 daylight;
	u8 padding[3]; /* must be 0 */
} __packed;

static int acpi_tad_set_real_time(struct device *dev, struct acpi_tad_rt *rt)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object args[] = {
		{ .type = ACPI_TYPE_BUFFER, },
	};
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};
	unsigned long long retval;
	acpi_status status;

	if (rt->year < 1900 || rt->year > 9999 ||
	    rt->month < 1 || rt->month > 12 ||
	    rt->hour > 23 || rt->minute > 59 || rt->second > 59 ||
	    rt->tz < -1440 || (rt->tz > 1440 && rt->tz != 2047) ||
	    rt->daylight > 3)
		return -ERANGE;

	args[0].buffer.pointer = (u8 *)rt;
	args[0].buffer.length = sizeof(*rt);

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_integer(handle, "_SRT", &arg_list, &retval);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status) || retval)
		return -EIO;

	return 0;
}

static int acpi_tad_get_real_time(struct device *dev, struct acpi_tad_rt *rt)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER };
	union acpi_object *out_obj;
	struct acpi_tad_rt *data;
	acpi_status status;
	int ret = -EIO;

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_object(handle, "_GRT", NULL, &output);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status))
		goto out_free;

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER)
		goto out_free;

	if (out_obj->buffer.length != sizeof(*rt))
		goto out_free;

	data = (struct acpi_tad_rt *)(out_obj->buffer.pointer);
	if (!data->valid)
		goto out_free;

	memcpy(rt, data, sizeof(*rt));
	ret = 0;

out_free:
	ACPI_FREE(output.pointer);
	return ret;
}

static char *acpi_tad_rt_next_field(char *s, int *val)
{
	char *p;

	p = strchr(s, ':');
	if (!p)
		return NULL;

	*p = '\0';
	if (kstrtoint(s, 10, val))
		return NULL;

	return p + 1;
}

static ssize_t time_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct acpi_tad_rt rt;
	char *str, *s;
	int val, ret = -ENODATA;

	str = kmemdup_nul(buf, count, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	s = acpi_tad_rt_next_field(str, &val);
	if (!s)
		goto out_free;

	rt.year = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.month = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.day = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.hour = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.minute = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.second = val;

	s = acpi_tad_rt_next_field(s, &val);
	if (!s)
		goto out_free;

	rt.tz = val;

	if (kstrtoint(s, 10, &val))
		goto out_free;

	rt.daylight = val;

	rt.valid = 0;
	rt.msec = 0;
	memset(rt.padding, 0, 3);

	ret = acpi_tad_set_real_time(dev, &rt);

out_free:
	kfree(str);
	return ret ? ret : count;
}

static ssize_t time_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct acpi_tad_rt rt;
	int ret;

	ret = acpi_tad_get_real_time(dev, &rt);
	if (ret)
		return ret;

	return sprintf(buf, "%u:%u:%u:%u:%u:%u:%d:%u\n",
		       rt.year, rt.month, rt.day, rt.hour, rt.minute, rt.second,
		       rt.tz, rt.daylight);
}

static DEVICE_ATTR_RW(time);

static struct attribute *acpi_tad_time_attrs[] = {
	&dev_attr_time.attr,
	NULL,
};
static const struct attribute_group acpi_tad_time_attr_group = {
	.attrs	= acpi_tad_time_attrs,
};

static int acpi_tad_wake_set(struct device *dev, char *method, u32 timer_id,
			     u32 value)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object args[] = {
		{ .type = ACPI_TYPE_INTEGER, },
		{ .type = ACPI_TYPE_INTEGER, },
	};
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};
	unsigned long long retval;
	acpi_status status;

	args[0].integer.value = timer_id;
	args[1].integer.value = value;

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_integer(handle, method, &arg_list, &retval);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status) || retval)
		return -EIO;

	return 0;
}

static int acpi_tad_wake_write(struct device *dev, const char *buf, char *method,
			       u32 timer_id, const char *specval)
{
	u32 value;

	if (sysfs_streq(buf, specval)) {
		value = ACPI_TAD_WAKE_DISABLED;
	} else {
		int ret = kstrtou32(buf, 0, &value);

		if (ret)
			return ret;

		if (value == ACPI_TAD_WAKE_DISABLED)
			return -EINVAL;
	}

	return acpi_tad_wake_set(dev, method, timer_id, value);
}

static ssize_t acpi_tad_wake_read(struct device *dev, char *buf, char *method,
				  u32 timer_id, const char *specval)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object args[] = {
		{ .type = ACPI_TYPE_INTEGER, },
	};
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};
	unsigned long long retval;
	acpi_status status;

	args[0].integer.value = timer_id;

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_integer(handle, method, &arg_list, &retval);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status))
		return -EIO;

	if ((u32)retval == ACPI_TAD_WAKE_DISABLED)
		return sprintf(buf, "%s\n", specval);

	return sprintf(buf, "%u\n", (u32)retval);
}

static const char *alarm_specval = "disabled";

static int acpi_tad_alarm_write(struct device *dev, const char *buf,
				u32 timer_id)
{
	return acpi_tad_wake_write(dev, buf, "_STV", timer_id, alarm_specval);
}

static ssize_t acpi_tad_alarm_read(struct device *dev, char *buf, u32 timer_id)
{
	return acpi_tad_wake_read(dev, buf, "_TIV", timer_id, alarm_specval);
}

static const char *policy_specval = "never";

static int acpi_tad_policy_write(struct device *dev, const char *buf,
				 u32 timer_id)
{
	return acpi_tad_wake_write(dev, buf, "_STP", timer_id, policy_specval);
}

static ssize_t acpi_tad_policy_read(struct device *dev, char *buf, u32 timer_id)
{
	return acpi_tad_wake_read(dev, buf, "_TIP", timer_id, policy_specval);
}

static int acpi_tad_clear_status(struct device *dev, u32 timer_id)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object args[] = {
		{ .type = ACPI_TYPE_INTEGER, },
	};
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};
	unsigned long long retval;
	acpi_status status;

	args[0].integer.value = timer_id;

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_integer(handle, "_CWS", &arg_list, &retval);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status) || retval)
		return -EIO;

	return 0;
}

static int acpi_tad_status_write(struct device *dev, const char *buf, u32 timer_id)
{
	int ret, value;

	ret = kstrtoint(buf, 0, &value);
	if (ret)
		return ret;

	if (value)
		return -EINVAL;

	return acpi_tad_clear_status(dev, timer_id);
}

static ssize_t acpi_tad_status_read(struct device *dev, char *buf, u32 timer_id)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	union acpi_object args[] = {
		{ .type = ACPI_TYPE_INTEGER, },
	};
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};
	unsigned long long retval;
	acpi_status status;

	args[0].integer.value = timer_id;

	pm_runtime_get_sync(dev);

	status = acpi_evaluate_integer(handle, "_GWS", &arg_list, &retval);

	pm_runtime_put_sync(dev);

	if (ACPI_FAILURE(status))
		return -EIO;

	return sprintf(buf, "0x%02X\n", (u32)retval);
}

static ssize_t caps_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct acpi_tad_driver_data *dd = dev_get_drvdata(dev);

	return sprintf(buf, "0x%02X\n", dd->capabilities);
}

static DEVICE_ATTR_RO(caps);

static ssize_t ac_alarm_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret = acpi_tad_alarm_write(dev, buf, ACPI_TAD_AC_TIMER);

	return ret ? ret : count;
}

static ssize_t ac_alarm_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return acpi_tad_alarm_read(dev, buf, ACPI_TAD_AC_TIMER);
}

static DEVICE_ATTR_RW(ac_alarm);

static ssize_t ac_policy_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = acpi_tad_policy_write(dev, buf, ACPI_TAD_AC_TIMER);

	return ret ? ret : count;
}

static ssize_t ac_policy_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return acpi_tad_policy_read(dev, buf, ACPI_TAD_AC_TIMER);
}

static DEVICE_ATTR_RW(ac_policy);

static ssize_t ac_status_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = acpi_tad_status_write(dev, buf, ACPI_TAD_AC_TIMER);

	return ret ? ret : count;
}

static ssize_t ac_status_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return acpi_tad_status_read(dev, buf, ACPI_TAD_AC_TIMER);
}

static DEVICE_ATTR_RW(ac_status);

static struct attribute *acpi_tad_attrs[] = {
	&dev_attr_caps.attr,
	&dev_attr_ac_alarm.attr,
	&dev_attr_ac_policy.attr,
	&dev_attr_ac_status.attr,
	NULL,
};
static const struct attribute_group acpi_tad_attr_group = {
	.attrs	= acpi_tad_attrs,
};

static ssize_t dc_alarm_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int ret = acpi_tad_alarm_write(dev, buf, ACPI_TAD_DC_TIMER);

	return ret ? ret : count;
}

static ssize_t dc_alarm_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return acpi_tad_alarm_read(dev, buf, ACPI_TAD_DC_TIMER);
}

static DEVICE_ATTR_RW(dc_alarm);

static ssize_t dc_policy_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = acpi_tad_policy_write(dev, buf, ACPI_TAD_DC_TIMER);

	return ret ? ret : count;
}

static ssize_t dc_policy_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return acpi_tad_policy_read(dev, buf, ACPI_TAD_DC_TIMER);
}

static DEVICE_ATTR_RW(dc_policy);

static ssize_t dc_status_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret = acpi_tad_status_write(dev, buf, ACPI_TAD_DC_TIMER);

	return ret ? ret : count;
}

static ssize_t dc_status_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return acpi_tad_status_read(dev, buf, ACPI_TAD_DC_TIMER);
}

static DEVICE_ATTR_RW(dc_status);

static struct attribute *acpi_tad_dc_attrs[] = {
	&dev_attr_dc_alarm.attr,
	&dev_attr_dc_policy.attr,
	&dev_attr_dc_status.attr,
	NULL,
};
static const struct attribute_group acpi_tad_dc_attr_group = {
	.attrs	= acpi_tad_dc_attrs,
};

static int acpi_tad_disable_timer(struct device *dev, u32 timer_id)
{
	return acpi_tad_wake_set(dev, "_STV", timer_id, ACPI_TAD_WAKE_DISABLED);
}

static void acpi_tad_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_tad_driver_data *dd = dev_get_drvdata(dev);

	device_init_wakeup(dev, false);

	pm_runtime_get_sync(dev);

	if (dd->capabilities & ACPI_TAD_DC_WAKE)
		sysfs_remove_group(&dev->kobj, &acpi_tad_dc_attr_group);

	sysfs_remove_group(&dev->kobj, &acpi_tad_attr_group);

	acpi_tad_disable_timer(dev, ACPI_TAD_AC_TIMER);
	acpi_tad_clear_status(dev, ACPI_TAD_AC_TIMER);
	if (dd->capabilities & ACPI_TAD_DC_WAKE) {
		acpi_tad_disable_timer(dev, ACPI_TAD_DC_TIMER);
		acpi_tad_clear_status(dev, ACPI_TAD_DC_TIMER);
	}

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	acpi_remove_cmos_rtc_space_handler(handle);
}

static int acpi_tad_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_tad_driver_data *dd;
	acpi_status status;
	unsigned long long caps;
	int ret;

	ret = acpi_install_cmos_rtc_space_handler(handle);
	if (ret < 0) {
		dev_info(dev, "Unable to install space handler\n");
		return -ENODEV;
	}
	/*
	 * Initialization failure messages are mostly about firmware issues, so
	 * print them at the "info" level.
	 */
	status = acpi_evaluate_integer(handle, "_GCP", NULL, &caps);
	if (ACPI_FAILURE(status)) {
		dev_info(dev, "Unable to get capabilities\n");
		ret = -ENODEV;
		goto remove_handler;
	}

	if (!(caps & ACPI_TAD_AC_WAKE)) {
		dev_info(dev, "Unsupported capabilities\n");
		ret = -ENODEV;
		goto remove_handler;
	}

	if (!acpi_has_method(handle, "_PRW")) {
		dev_info(dev, "Missing _PRW\n");
		ret = -ENODEV;
		goto remove_handler;
	}

	dd = devm_kzalloc(dev, sizeof(*dd), GFP_KERNEL);
	if (!dd) {
		ret = -ENOMEM;
		goto remove_handler;
	}

	dd->capabilities = caps;
	dev_set_drvdata(dev, dd);

	/*
	 * Assume that the ACPI PM domain has been attached to the device and
	 * simply enable system wakeup and runtime PM and put the device into
	 * runtime suspend.  Everything else should be taken care of by the ACPI
	 * PM domain callbacks.
	 */
	device_init_wakeup(dev, true);
	dev_pm_set_driver_flags(dev, DPM_FLAG_SMART_SUSPEND |
				     DPM_FLAG_MAY_SKIP_RESUME);
	/*
	 * The platform bus type layer tells the ACPI PM domain powers up the
	 * device, so set the runtime PM status of it to "active".
	 */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_suspend(dev);

	ret = sysfs_create_group(&dev->kobj, &acpi_tad_attr_group);
	if (ret)
		goto fail;

	if (caps & ACPI_TAD_DC_WAKE) {
		ret = sysfs_create_group(&dev->kobj, &acpi_tad_dc_attr_group);
		if (ret)
			goto fail;
	}

	if (caps & ACPI_TAD_RT) {
		ret = sysfs_create_group(&dev->kobj, &acpi_tad_time_attr_group);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	acpi_tad_remove(pdev);
	/* Don't fallthrough because cmos rtc space handler is removed in acpi_tad_remove() */
	return ret;

remove_handler:
	acpi_remove_cmos_rtc_space_handler(handle);
	return ret;
}

static const struct acpi_device_id acpi_tad_ids[] = {
	{"ACPI000E", 0},
	{}
};

static struct platform_driver acpi_tad_driver = {
	.driver = {
		.name = "acpi-tad",
		.acpi_match_table = acpi_tad_ids,
	},
	.probe = acpi_tad_probe,
	.remove_new = acpi_tad_remove,
};
MODULE_DEVICE_TABLE(acpi, acpi_tad_ids);

module_platform_driver(acpi_tad_driver);
