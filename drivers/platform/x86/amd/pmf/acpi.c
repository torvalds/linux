// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/acpi.h>
#include "pmf.h"

#define APMF_CQL_NOTIFICATION  2
#define APMF_AMT_NOTIFICATION  3

static union acpi_object *apmf_if_call(struct amd_pmf_dev *pdev, int fn, struct acpi_buffer *param)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle ahandle = ACPI_HANDLE(pdev->dev);
	struct acpi_object_list apmf_if_arg_list;
	union acpi_object apmf_if_args[2];
	acpi_status status;

	apmf_if_arg_list.count = 2;
	apmf_if_arg_list.pointer = &apmf_if_args[0];

	apmf_if_args[0].type = ACPI_TYPE_INTEGER;
	apmf_if_args[0].integer.value = fn;

	if (param) {
		apmf_if_args[1].type = ACPI_TYPE_BUFFER;
		apmf_if_args[1].buffer.length = param->length;
		apmf_if_args[1].buffer.pointer = param->pointer;
	} else {
		apmf_if_args[1].type = ACPI_TYPE_INTEGER;
		apmf_if_args[1].integer.value = 0;
	}

	status = acpi_evaluate_object(ahandle, "APMF", &apmf_if_arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(pdev->dev, "APMF method:%d call failed\n", fn);
		kfree(buffer.pointer);
		return NULL;
	}

	return buffer.pointer;
}

static int apmf_if_call_store_buffer(struct amd_pmf_dev *pdev, int fn, void *dest, size_t out_sz)
{
	union acpi_object *info;
	size_t size;
	int err = 0;

	info = apmf_if_call(pdev, fn, NULL);
	if (!info)
		return -EIO;

	if (info->type != ACPI_TYPE_BUFFER) {
		dev_err(pdev->dev, "object is not a buffer\n");
		err = -EINVAL;
		goto out;
	}

	if (info->buffer.length < 2) {
		dev_err(pdev->dev, "buffer too small\n");
		err = -EINVAL;
		goto out;
	}

	size = *(u16 *)info->buffer.pointer;
	if (info->buffer.length < size) {
		dev_err(pdev->dev, "buffer smaller then headersize %u < %zu\n",
			info->buffer.length, size);
		err = -EINVAL;
		goto out;
	}

	if (size < out_sz) {
		dev_err(pdev->dev, "buffer too small %zu\n", size);
		err = -EINVAL;
		goto out;
	}

	memcpy(dest, info->buffer.pointer, out_sz);

out:
	kfree(info);
	return err;
}

int is_apmf_func_supported(struct amd_pmf_dev *pdev, unsigned long index)
{
	/* If bit-n is set, that indicates function n+1 is supported */
	return !!(pdev->supported_func & BIT(index - 1));
}

int apmf_get_static_slider_granular(struct amd_pmf_dev *pdev,
				    struct apmf_static_slider_granular_output *data)
{
	if (!is_apmf_func_supported(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		return -EINVAL;

	return apmf_if_call_store_buffer(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR,
									 data, sizeof(*data));
}

int apmf_os_power_slider_update(struct amd_pmf_dev *pdev, u8 event)
{
	struct os_power_slider args;
	struct acpi_buffer params;
	union acpi_object *info;
	int err = 0;

	args.size = sizeof(args);
	args.slider_event = event;

	params.length = sizeof(args);
	params.pointer = (void *)&args;

	info = apmf_if_call(pdev, APMF_FUNC_OS_POWER_SLIDER_UPDATE, &params);
	if (!info)
		err = -EIO;

	kfree(info);
	return err;
}

static void apmf_sbios_heartbeat_notify(struct work_struct *work)
{
	struct amd_pmf_dev *dev = container_of(work, struct amd_pmf_dev, heart_beat.work);
	union acpi_object *info;

	dev_dbg(dev->dev, "Sending heartbeat to SBIOS\n");
	info = apmf_if_call(dev, APMF_FUNC_SBIOS_HEARTBEAT, NULL);
	if (!info)
		goto out;

	schedule_delayed_work(&dev->heart_beat, msecs_to_jiffies(dev->hb_interval * 1000));

out:
	kfree(info);
}

int apmf_update_fan_idx(struct amd_pmf_dev *pdev, bool manual, u32 idx)
{
	union acpi_object *info;
	struct apmf_fan_idx args;
	struct acpi_buffer params;
	int err = 0;

	args.size = sizeof(args);
	args.fan_ctl_mode = manual;
	args.fan_ctl_idx = idx;

	params.length = sizeof(args);
	params.pointer = (void *)&args;

	info = apmf_if_call(pdev, APMF_FUNC_SET_FAN_IDX, &params);
	if (!info) {
		err = -EIO;
		goto out;
	}

out:
	kfree(info);
	return err;
}

int apmf_get_auto_mode_def(struct amd_pmf_dev *pdev, struct apmf_auto_mode *data)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_AUTO_MODE, data, sizeof(*data));
}

int apmf_get_sbios_requests(struct amd_pmf_dev *pdev, struct apmf_sbios_req *req)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_SBIOS_REQUESTS,
									 req, sizeof(*req));
}

static void apmf_event_handler(acpi_handle handle, u32 event, void *data)
{
	struct amd_pmf_dev *pmf_dev = data;
	struct apmf_sbios_req req;
	int ret;

	mutex_lock(&pmf_dev->update_mutex);
	ret = apmf_get_sbios_requests(pmf_dev, &req);
	if (ret) {
		dev_err(pmf_dev->dev, "Failed to get SBIOS requests:%d\n", ret);
		goto out;
	}

	if (req.pending_req & BIT(APMF_AMT_NOTIFICATION)) {
		dev_dbg(pmf_dev->dev, "AMT is supported and notifications %s\n",
			req.amt_event ? "Enabled" : "Disabled");
		pmf_dev->amt_enabled = !!req.amt_event;

		if (pmf_dev->amt_enabled)
			amd_pmf_handle_amt(pmf_dev);
		else
			amd_pmf_reset_amt(pmf_dev);
	}

	if (req.pending_req & BIT(APMF_CQL_NOTIFICATION)) {
		dev_dbg(pmf_dev->dev, "CQL is supported and notifications %s\n",
			req.cql_event ? "Enabled" : "Disabled");

		/* update the target mode information */
		if (pmf_dev->amt_enabled)
			amd_pmf_update_2_cql(pmf_dev, req.cql_event);
	}
out:
	mutex_unlock(&pmf_dev->update_mutex);
}

static int apmf_if_verify_interface(struct amd_pmf_dev *pdev)
{
	struct apmf_verify_interface output;
	int err;

	err = apmf_if_call_store_buffer(pdev, APMF_FUNC_VERIFY_INTERFACE, &output, sizeof(output));
	if (err)
		return err;

	pdev->supported_func = output.supported_functions;
	dev_dbg(pdev->dev, "supported functions:0x%x notifications:0x%x\n",
		output.supported_functions, output.notification_mask);

	return 0;
}

static int apmf_get_system_params(struct amd_pmf_dev *dev)
{
	struct apmf_system_params params;
	int err;

	if (!is_apmf_func_supported(dev, APMF_FUNC_GET_SYS_PARAMS))
		return -EINVAL;

	err = apmf_if_call_store_buffer(dev, APMF_FUNC_GET_SYS_PARAMS, &params, sizeof(params));
	if (err)
		return err;

	dev_dbg(dev->dev, "system params mask:0x%x flags:0x%x cmd_code:0x%x heartbeat:%d\n",
		params.valid_mask,
		params.flags,
		params.command_code,
		params.heartbeat_int);
	params.flags = params.flags & params.valid_mask;
	dev->hb_interval = params.heartbeat_int;

	return 0;
}

int apmf_get_dyn_slider_def_ac(struct amd_pmf_dev *pdev, struct apmf_dyn_slider_output *data)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_DYN_SLIDER_AC, data, sizeof(*data));
}

int apmf_get_dyn_slider_def_dc(struct amd_pmf_dev *pdev, struct apmf_dyn_slider_output *data)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_DYN_SLIDER_DC, data, sizeof(*data));
}

int apmf_install_handler(struct amd_pmf_dev *pmf_dev)
{
	acpi_handle ahandle = ACPI_HANDLE(pmf_dev->dev);
	acpi_status status;

	/* Install the APMF Notify handler */
	if (is_apmf_func_supported(pmf_dev, APMF_FUNC_AUTO_MODE) &&
	    is_apmf_func_supported(pmf_dev, APMF_FUNC_SBIOS_REQUESTS)) {
		status = acpi_install_notify_handler(ahandle, ACPI_ALL_NOTIFY,
						     apmf_event_handler, pmf_dev);
		if (ACPI_FAILURE(status)) {
			dev_err(pmf_dev->dev, "failed to install notify handler\n");
			return -ENODEV;
		}

		/* Call the handler once manually to catch up with possibly missed notifies. */
		apmf_event_handler(ahandle, 0, pmf_dev);
	}

	return 0;
}

void apmf_acpi_deinit(struct amd_pmf_dev *pmf_dev)
{
	acpi_handle ahandle = ACPI_HANDLE(pmf_dev->dev);

	if (pmf_dev->hb_interval)
		cancel_delayed_work_sync(&pmf_dev->heart_beat);

	if (is_apmf_func_supported(pmf_dev, APMF_FUNC_AUTO_MODE) &&
	    is_apmf_func_supported(pmf_dev, APMF_FUNC_SBIOS_REQUESTS))
		acpi_remove_notify_handler(ahandle, ACPI_ALL_NOTIFY, apmf_event_handler);
}

int apmf_acpi_init(struct amd_pmf_dev *pmf_dev)
{
	int ret;

	ret = apmf_if_verify_interface(pmf_dev);
	if (ret) {
		dev_err(pmf_dev->dev, "APMF verify interface failed :%d\n", ret);
		goto out;
	}

	ret = apmf_get_system_params(pmf_dev);
	if (ret) {
		dev_dbg(pmf_dev->dev, "APMF apmf_get_system_params failed :%d\n", ret);
		goto out;
	}

	if (pmf_dev->hb_interval) {
		/* send heartbeats only if the interval is not zero */
		INIT_DELAYED_WORK(&pmf_dev->heart_beat, apmf_sbios_heartbeat_notify);
		schedule_delayed_work(&pmf_dev->heart_beat, 0);
	}

out:
	return ret;
}
