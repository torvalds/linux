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

	dev_dbg(dev->dev, "system params mask:0x%x flags:0x%x cmd_code:0x%x\n",
		params.valid_mask,
		params.flags,
		params.command_code);
	params.flags = params.flags & params.valid_mask;

	return 0;
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
		dev_err(pmf_dev->dev, "APMF apmf_get_system_params failed :%d\n", ret);
		goto out;
	}

out:
	return ret;
}
