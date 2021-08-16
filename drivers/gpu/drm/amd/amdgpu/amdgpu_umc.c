/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "amdgpu_ras.h"

int amdgpu_umc_ras_late_init(struct amdgpu_device *adev)
{
	int r;
	struct ras_fs_if fs_info = {
		.sysfs_name = "umc_err_count",
	};
	struct ras_ih_if ih_info = {
		.cb = amdgpu_umc_process_ras_data_cb,
	};

	if (!adev->umc.ras_if) {
		adev->umc.ras_if =
			kmalloc(sizeof(struct ras_common_if), GFP_KERNEL);
		if (!adev->umc.ras_if)
			return -ENOMEM;
		adev->umc.ras_if->block = AMDGPU_RAS_BLOCK__UMC;
		adev->umc.ras_if->type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
		adev->umc.ras_if->sub_block_index = 0;
		strcpy(adev->umc.ras_if->name, "umc");
	}
	ih_info.head = fs_info.head = *adev->umc.ras_if;

	r = amdgpu_ras_late_init(adev, adev->umc.ras_if,
				 &fs_info, &ih_info);
	if (r)
		goto free;

	if (amdgpu_ras_is_supported(adev, adev->umc.ras_if->block)) {
		r = amdgpu_irq_get(adev, &adev->gmc.ecc_irq, 0);
		if (r)
			goto late_fini;
	} else {
		r = 0;
		goto free;
	}

	/* ras init of specific umc version */
	if (adev->umc.ras_funcs &&
	    adev->umc.ras_funcs->err_cnt_init)
		adev->umc.ras_funcs->err_cnt_init(adev);

	return 0;

late_fini:
	amdgpu_ras_late_fini(adev, adev->umc.ras_if, &ih_info);
free:
	kfree(adev->umc.ras_if);
	adev->umc.ras_if = NULL;
	return r;
}

void amdgpu_umc_ras_fini(struct amdgpu_device *adev)
{
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__UMC) &&
			adev->umc.ras_if) {
		struct ras_common_if *ras_if = adev->umc.ras_if;
		struct ras_ih_if ih_info = {
			.head = *ras_if,
			.cb = amdgpu_umc_process_ras_data_cb,
		};

		amdgpu_ras_late_fini(adev, ras_if, &ih_info);
		kfree(ras_if);
	}
}

int amdgpu_umc_process_ras_data_cb(struct amdgpu_device *adev,
		void *ras_error_status,
		struct amdgpu_iv_entry *entry)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);
	if (adev->umc.ras_funcs &&
	    adev->umc.ras_funcs->query_ras_error_count)
	    adev->umc.ras_funcs->query_ras_error_count(adev, ras_error_status);

	if (adev->umc.ras_funcs &&
	    adev->umc.ras_funcs->query_ras_error_address &&
	    adev->umc.max_ras_err_cnt_per_query) {
		err_data->err_addr =
			kcalloc(adev->umc.max_ras_err_cnt_per_query,
				sizeof(struct eeprom_table_record), GFP_KERNEL);

		/* still call query_ras_error_address to clear error status
		 * even NOMEM error is encountered
		 */
		if(!err_data->err_addr)
			dev_warn(adev->dev, "Failed to alloc memory for "
					"umc error address record!\n");

		/* umc query_ras_error_address is also responsible for clearing
		 * error status
		 */
		adev->umc.ras_funcs->query_ras_error_address(adev, ras_error_status);
	}

	/* only uncorrectable error needs gpu reset */
	if (err_data->ue_count) {
		dev_info(adev->dev, "%ld uncorrectable hardware errors "
				"detected in UMC block\n",
				err_data->ue_count);

		if ((amdgpu_bad_page_threshold != 0) &&
			err_data->err_addr_cnt) {
			amdgpu_ras_add_bad_pages(adev, err_data->err_addr,
						err_data->err_addr_cnt);
			amdgpu_ras_save_bad_pages(adev);

			if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->send_hbm_bad_pages_num)
				adev->smu.ppt_funcs->send_hbm_bad_pages_num(&adev->smu, con->eeprom_control.ras_num_recs);
		}

		amdgpu_ras_reset_gpu(adev);
	}

	kfree(err_data->err_addr);
	return AMDGPU_RAS_SUCCESS;
}

int amdgpu_umc_process_ecc_irq(struct amdgpu_device *adev,
		struct amdgpu_irq_src *source,
		struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->umc.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;

	amdgpu_ras_interrupt_dispatch(adev, &ih_data);
	return 0;
}
