// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "amdgpu.h"
#include "amdgpu_ras_bert.h"

#if defined(CONFIG_X86_MCE_AMD) && defined(CONFIG_ACPI_APEI)
#include <linux/acpi.h>
#include <linux/mutex.h>

#include "amdgpu_ras_mgr.h"

struct amdgpu_ras_bert_boot_err_state {
	struct mutex lock; /* protects cached boot error state */
	bool polled;
	bool platform_written;
	bool written[MAX_GPU_INSTANCE];
	u32 users;
	int poll_result;
	u8 *raw_data;
	u32 raw_data_len;
};

static struct amdgpu_ras_bert_boot_err_state boot_err_state = {
	.lock = __MUTEX_INITIALIZER(boot_err_state.lock),
};

static int amdgpu_ras_bert_process_boot_errors(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct amdgpu_ras_bert_boot_err_state *state = &boot_err_state;
	struct acpi_bert_region *boot_error_region;
	struct acpi_table_header *table;
	struct acpi_table_bert *bert_tab;
	unsigned int region_len;
	acpi_status status;
	u32 socket_id;
	int ret;

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EPERM;

	mutex_lock(&state->lock);
	state->users++;

	if (!state->polled) {
		state->polled = true;
		state->poll_result = 0;
		ret = 0;

		if (acpi_disabled)
			goto out_unlock;

		status = acpi_get_table(ACPI_SIG_BERT, 0, &table);
		if (status == AE_NOT_FOUND)
			goto out_unlock;

		if (ACPI_FAILURE(status)) {
			RAS_DEV_ERR(adev, "get table failed, %s.\n", acpi_format_exception(status));
			state->poll_result = -EINVAL;
			goto out_unlock;
		}
		bert_tab = (struct acpi_table_bert *)table;

		if (bert_tab->header.length < sizeof(struct acpi_table_bert) ||
		    bert_tab->region_length < sizeof(struct acpi_bert_region)) {
			RAS_DEV_ERR(adev, "table invalid.\n");
			state->poll_result = -EINVAL;
			goto out_put_bert_tab;
		}

		region_len = bert_tab->region_length;
		boot_error_region = acpi_os_map_memory(bert_tab->address, region_len);
		if (boot_error_region) {
			state->raw_data = kmemdup(boot_error_region, region_len, GFP_KERNEL);
			acpi_os_unmap_memory(boot_error_region, region_len);
			if (!state->raw_data) {
				RAS_DEV_ERR(adev, "failed to cache BERT raw data.\n");
				state->poll_result = -ENOMEM;
			} else {
				state->raw_data_len = region_len;
			}
		} else {
			RAS_DEV_ERR(adev, "failed to map BERT region.\n");
			state->poll_result = -ENOMEM;
		}

out_put_bert_tab:
		acpi_put_table(table);
	}

	if (state->poll_result) {
		ret = state->poll_result;
		goto out_unlock;
	}

	if (!state->raw_data || !state->raw_data_len ||
	    !(adev && (adev->gmc.xgmi.connected_to_cpu || adev->gmc.is_app_apu))) {
		ret = 0;
		goto out_unlock;
	}

	if (!adev->smuio.funcs || !adev->smuio.funcs->get_socket_id) {
		RAS_DEV_WARN(adev, "No interface to obtain current device socket ID!\n");
		ret = 0;
		goto out_unlock;
	}

	socket_id = adev->smuio.funcs->get_socket_id(adev);
	if (socket_id >= MAX_GPU_INSTANCE) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (state->written[socket_id]) {
		ret = 0;
		goto out_unlock;
	}

	ras_mgr->ras_core->bert_platform_owner = !state->platform_written;
	if (ras_mgr->ras_core->bert_platform_owner)
		state->platform_written = true;
	state->written[socket_id] = true;
	ret = ras_bert_process_records(ras_mgr->ras_core,
				       state->raw_data, state->raw_data_len);
	ras_mgr->ras_core->bert_platform_owner = false;

out_unlock:
	mutex_unlock(&state->lock);

	return ret;
}

static void amdgpu_ras_bert_release_boot_errors(void)
{
	struct amdgpu_ras_bert_boot_err_state *state = &boot_err_state;

	mutex_lock(&state->lock);
	if (!state->users || --state->users)
		goto out_unlock;

	memset(state->written, 0, sizeof(state->written));
	state->platform_written = false;
	kfree(state->raw_data);
	state->raw_data = NULL;
	state->raw_data_len = 0;
	state->poll_result = 0;
	state->polled = false;

out_unlock:
	mutex_unlock(&state->lock);
}
#endif

int amdgpu_ras_bert_sw_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_X86_MCE_AMD) && defined(CONFIG_ACPI_APEI)
	return amdgpu_ras_bert_process_boot_errors(adev);
#else
	return 0;
#endif
}

int amdgpu_ras_bert_sw_fini(struct amdgpu_device *adev)
{
#if defined(CONFIG_X86_MCE_AMD) && defined(CONFIG_ACPI_APEI)
	amdgpu_ras_bert_release_boot_errors();
#endif
	return 0;
}
