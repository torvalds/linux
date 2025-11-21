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

static union acpi_object *apts_if_call(struct amd_pmf_dev *pdev, u32 state_index)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle ahandle = ACPI_HANDLE(pdev->dev);
	struct acpi_object_list apts_if_arg_list;
	union acpi_object apts_if_args[3];
	acpi_status status;

	apts_if_arg_list.count = 3;
	apts_if_arg_list.pointer = &apts_if_args[0];

	apts_if_args[0].type = ACPI_TYPE_INTEGER;
	apts_if_args[0].integer.value = 1;
	apts_if_args[1].type = ACPI_TYPE_INTEGER;
	apts_if_args[1].integer.value = state_index;
	apts_if_args[2].type = ACPI_TYPE_INTEGER;
	apts_if_args[2].integer.value = 0;

	status = acpi_evaluate_object(ahandle, "APTS", &apts_if_arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(pdev->dev, "APTS state_idx:%u call failed\n", state_index);
		kfree(buffer.pointer);
		return NULL;
	}

	return buffer.pointer;
}

static int apts_if_call_store_buffer(struct amd_pmf_dev *pdev,
				     u32 index, void *data, size_t out_sz)
{
	union acpi_object *info;
	size_t size;
	int err = 0;

	info = apts_if_call(pdev, index);
	if (!info)
		return -EIO;

	if (info->type != ACPI_TYPE_BUFFER) {
		dev_err(pdev->dev, "object is not a buffer\n");
		err = -EINVAL;
		goto out;
	}

	size = *(u16 *)info->buffer.pointer;
	if (info->buffer.length < size) {
		dev_err(pdev->dev, "buffer smaller than header size %u < %zu\n",
			info->buffer.length, size);
		err = -EINVAL;
		goto out;
	}

	if (size < out_sz) {
		dev_err(pdev->dev, "buffer too small %zu\n", size);
		err = -EINVAL;
		goto out;
	}

	memcpy(data, info->buffer.pointer, out_sz);
out:
	kfree(info);
	return err;
}

int is_apmf_func_supported(struct amd_pmf_dev *pdev, unsigned long index)
{
	/* If bit-n is set, that indicates function n+1 is supported */
	return !!(pdev->supported_func & BIT(index - 1));
}

int is_apmf_bios_input_notifications_supported(struct amd_pmf_dev *pdev)
{
	return !!(pdev->notifications & CUSTOM_BIOS_INPUT_BITS);
}

int apts_get_static_slider_granular_v2(struct amd_pmf_dev *pdev,
				       struct amd_pmf_apts_granular_output *data, u32 apts_idx)
{
	if (!is_apmf_func_supported(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		return -EINVAL;

	return apts_if_call_store_buffer(pdev, apts_idx, data, sizeof(*data));
}

int apmf_get_static_slider_granular_v2(struct amd_pmf_dev *pdev,
				       struct apmf_static_slider_granular_output_v2 *data)
{
	if (!is_apmf_func_supported(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		return -EINVAL;

	return apmf_if_call_store_buffer(pdev, APMF_FUNC_STATIC_SLIDER_GRANULAR,
					 data, sizeof(*data));
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

	args.size = sizeof(args);
	args.slider_event = event;

	params.length = sizeof(args);
	params.pointer = (void *)&args;

	info = apmf_if_call(pdev, APMF_FUNC_OS_POWER_SLIDER_UPDATE, &params);
	if (!info)
		return -EIO;

	kfree(info);
	return 0;
}

static void apmf_sbios_heartbeat_notify(struct work_struct *work)
{
	struct amd_pmf_dev *dev = container_of(work, struct amd_pmf_dev, heart_beat.work);
	union acpi_object *info;

	dev_dbg(dev->dev, "Sending heartbeat to SBIOS\n");
	info = apmf_if_call(dev, APMF_FUNC_SBIOS_HEARTBEAT, NULL);
	if (!info)
		return;

	schedule_delayed_work(&dev->heart_beat, secs_to_jiffies(dev->hb_interval));
	kfree(info);
}

int amd_pmf_notify_sbios_heartbeat_event_v2(struct amd_pmf_dev *dev, u8 flag)
{
	struct sbios_hb_event_v2 args = { };
	struct acpi_buffer params;
	union acpi_object *info;

	args.size = sizeof(args);

	switch (flag) {
	case ON_LOAD:
		args.load = 1;
		break;
	case ON_UNLOAD:
		args.unload = 1;
		break;
	case ON_SUSPEND:
		args.suspend = 1;
		break;
	case ON_RESUME:
		args.resume = 1;
		break;
	default:
		dev_dbg(dev->dev, "Failed to send v2 heartbeat event, flag:0x%x\n", flag);
		return -EINVAL;
	}

	params.length = sizeof(args);
	params.pointer = &args;

	info = apmf_if_call(dev, APMF_FUNC_SBIOS_HEARTBEAT_V2, &params);
	if (!info)
		return -EIO;

	kfree(info);
	return 0;
}

int apmf_update_fan_idx(struct amd_pmf_dev *pdev, bool manual, u32 idx)
{
	union acpi_object *info;
	struct apmf_fan_idx args;
	struct acpi_buffer params;

	args.size = sizeof(args);
	args.fan_ctl_mode = manual;
	args.fan_ctl_idx = idx;

	params.length = sizeof(args);
	params.pointer = (void *)&args;

	info = apmf_if_call(pdev, APMF_FUNC_SET_FAN_IDX, &params);
	if (!info)
		return -EIO;

	kfree(info);
	return 0;
}

static int apmf_notify_smart_pc_update(struct amd_pmf_dev *pdev, u32 val, u32 preq, u32 index)
{
	struct amd_pmf_notify_smart_pc_update args;
	struct acpi_buffer params;
	union acpi_object *info;

	args.size = sizeof(args);
	args.pending_req = preq;
	args.custom_bios[index] = val;

	params.length = sizeof(args);
	params.pointer = &args;

	info = apmf_if_call(pdev, APMF_FUNC_NOTIFY_SMART_PC_UPDATES, &params);
	if (!info)
		return -EIO;

	kfree(info);
	dev_dbg(pdev->dev, "Notify smart pc update, val: %u\n", val);

	return 0;
}

int apmf_get_auto_mode_def(struct amd_pmf_dev *pdev, struct apmf_auto_mode *data)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_AUTO_MODE, data, sizeof(*data));
}

int apmf_get_sbios_requests_v2(struct amd_pmf_dev *pdev, struct apmf_sbios_req_v2 *req)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_SBIOS_REQUESTS, req, sizeof(*req));
}

int apmf_get_sbios_requests_v1(struct amd_pmf_dev *pdev, struct apmf_sbios_req_v1 *req)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_SBIOS_REQUESTS, req, sizeof(*req));
}

int apmf_get_sbios_requests(struct amd_pmf_dev *pdev, struct apmf_sbios_req *req)
{
	return apmf_if_call_store_buffer(pdev, APMF_FUNC_SBIOS_REQUESTS,
									 req, sizeof(*req));
}

static void amd_pmf_handle_early_preq(struct amd_pmf_dev *pdev)
{
	if (!pdev->cb_flag)
		return;

	amd_pmf_invoke_cmd_enact(pdev);
	pdev->cb_flag = false;
}

static void apmf_event_handler_v2(acpi_handle handle, u32 event, void *data)
{
	struct amd_pmf_dev *pmf_dev = data;
	int ret;

	guard(mutex)(&pmf_dev->cb_mutex);

	ret = apmf_get_sbios_requests_v2(pmf_dev, &pmf_dev->req);
	if (ret) {
		dev_err(pmf_dev->dev, "Failed to get v2 SBIOS requests: %d\n", ret);
		return;
	}

	dev_dbg(pmf_dev->dev, "Pending request (preq): 0x%x\n", pmf_dev->req.pending_req);

	amd_pmf_handle_early_preq(pmf_dev);
}

static void apmf_event_handler_v1(acpi_handle handle, u32 event, void *data)
{
	struct amd_pmf_dev *pmf_dev = data;
	int ret;

	guard(mutex)(&pmf_dev->cb_mutex);

	ret = apmf_get_sbios_requests_v1(pmf_dev, &pmf_dev->req1);
	if (ret) {
		dev_err(pmf_dev->dev, "Failed to get v1 SBIOS requests: %d\n", ret);
		return;
	}

	dev_dbg(pmf_dev->dev, "Pending request (preq1): 0x%x\n", pmf_dev->req1.pending_req);

	amd_pmf_handle_early_preq(pmf_dev);
}

static void apmf_event_handler(acpi_handle handle, u32 event, void *data)
{
	struct amd_pmf_dev *pmf_dev = data;
	struct apmf_sbios_req req;
	int ret;

	guard(mutex)(&pmf_dev->update_mutex);
	ret = apmf_get_sbios_requests(pmf_dev, &req);
	if (ret) {
		dev_err(pmf_dev->dev, "Failed to get SBIOS requests:%d\n", ret);
		return;
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
}

static int apmf_if_verify_interface(struct amd_pmf_dev *pdev)
{
	struct apmf_verify_interface output;
	int err;

	err = apmf_if_call_store_buffer(pdev, APMF_FUNC_VERIFY_INTERFACE, &output, sizeof(output));
	if (err)
		return err;

	/* only set if not already set by a quirk */
	if (!pdev->supported_func)
		pdev->supported_func = output.supported_functions;

	dev_dbg(pdev->dev, "supported functions:0x%x notifications:0x%x version:%u\n",
		output.supported_functions, output.notification_mask, output.version);

	pdev->pmf_if_version = output.version;

	pdev->notifications =  output.notification_mask;
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

static apmf_event_handler_t apmf_event_handlers[] = {
	[PMF_IF_V1] = apmf_event_handler_v1,
	[PMF_IF_V2] = apmf_event_handler_v2,
};

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

	if (!pmf_dev->smart_pc_enabled)
		return -EINVAL;

	switch (pmf_dev->pmf_if_version) {
	case PMF_IF_V1:
		if (!is_apmf_bios_input_notifications_supported(pmf_dev))
			break;
		fallthrough;
	case PMF_IF_V2:
		status = acpi_install_notify_handler(ahandle, ACPI_ALL_NOTIFY,
				apmf_event_handlers[pmf_dev->pmf_if_version], pmf_dev);
		if (ACPI_FAILURE(status)) {
			dev_err(pmf_dev->dev,
				"failed to install notify handler v%d for custom BIOS inputs\n",
				pmf_dev->pmf_if_version);
			return -ENODEV;
		}
		break;
	default:
		break;
	}

	return 0;
}

int apmf_check_smart_pc(struct amd_pmf_dev *pmf_dev)
{
	struct platform_device *pdev = to_platform_device(pmf_dev->dev);

	pmf_dev->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pmf_dev->res) {
		dev_dbg(pmf_dev->dev, "Failed to get I/O memory resource\n");
		return -EINVAL;
	}

	pmf_dev->policy_addr = pmf_dev->res->start;
	/*
	 * We cannot use resource_size() here because it adds an extra byte to round off the size.
	 * In the case of PMF ResourceTemplate(), this rounding is already handled within the _CRS.
	 * Using resource_size() would increase the resource size by 1, causing a mismatch with the
	 * length field and leading to issues. Therefore, simply use end-start of the ACPI resource
	 * to obtain the actual length.
	 */
	pmf_dev->policy_sz = pmf_dev->res->end - pmf_dev->res->start;

	if (!pmf_dev->policy_addr || pmf_dev->policy_sz > POLICY_BUF_MAX_SZ ||
	    pmf_dev->policy_sz == 0) {
		dev_err(pmf_dev->dev, "Incorrect policy params, possibly a SBIOS bug\n");
		return -EINVAL;
	}

	return 0;
}

int amd_pmf_smartpc_apply_bios_output(struct amd_pmf_dev *dev, u32 val, u32 preq, u32 idx)
{
	if (!is_apmf_func_supported(dev, APMF_FUNC_NOTIFY_SMART_PC_UPDATES))
		return -EINVAL;

	return apmf_notify_smart_pc_update(dev, val, preq, idx);
}

void apmf_acpi_deinit(struct amd_pmf_dev *pmf_dev)
{
	acpi_handle ahandle = ACPI_HANDLE(pmf_dev->dev);

	if (pmf_dev->hb_interval && pmf_dev->pmf_if_version == PMF_IF_V1)
		cancel_delayed_work_sync(&pmf_dev->heart_beat);

	if (is_apmf_func_supported(pmf_dev, APMF_FUNC_AUTO_MODE) &&
	    is_apmf_func_supported(pmf_dev, APMF_FUNC_SBIOS_REQUESTS))
		acpi_remove_notify_handler(ahandle, ACPI_ALL_NOTIFY, apmf_event_handler);

	if (!pmf_dev->smart_pc_enabled)
		return;

	switch (pmf_dev->pmf_if_version) {
	case PMF_IF_V1:
		if (!is_apmf_bios_input_notifications_supported(pmf_dev))
			break;
		fallthrough;
	case PMF_IF_V2:
		acpi_remove_notify_handler(ahandle, ACPI_ALL_NOTIFY,
					   apmf_event_handlers[pmf_dev->pmf_if_version]);
		break;
	default:
		break;
	}
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

	if (pmf_dev->hb_interval && pmf_dev->pmf_if_version == PMF_IF_V1) {
		/* send heartbeats only if the interval is not zero */
		INIT_DELAYED_WORK(&pmf_dev->heart_beat, apmf_sbios_heartbeat_notify);
		schedule_delayed_work(&pmf_dev->heart_beat, 0);
	}

out:
	return ret;
}
