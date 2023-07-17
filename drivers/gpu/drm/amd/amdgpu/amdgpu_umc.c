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

#include "amdgpu.h"
#include "umc_v6_7.h"

static int amdgpu_umc_convert_error_address(struct amdgpu_device *adev,
				    struct ras_err_data *err_data, uint64_t err_addr,
				    uint32_t ch_inst, uint32_t umc_inst)
{
	switch (adev->ip_versions[UMC_HWIP][0]) {
	case IP_VERSION(6, 7, 0):
		umc_v6_7_convert_error_address(adev,
				err_data, err_addr, ch_inst, umc_inst);
		break;
	default:
		dev_warn(adev->dev,
			 "UMC address to Physical address translation is not supported\n");
		return AMDGPU_RAS_FAIL;
	}

	return AMDGPU_RAS_SUCCESS;
}

int amdgpu_umc_page_retirement_mca(struct amdgpu_device *adev,
			uint64_t err_addr, uint32_t ch_inst, uint32_t umc_inst)
{
	struct ras_err_data err_data = {0, 0, 0, NULL};
	int ret = AMDGPU_RAS_FAIL;

	err_data.err_addr =
		kcalloc(adev->umc.max_ras_err_cnt_per_query,
			sizeof(struct eeprom_table_record), GFP_KERNEL);
	if (!err_data.err_addr) {
		dev_warn(adev->dev,
			"Failed to alloc memory for umc error record in MCA notifier!\n");
		return AMDGPU_RAS_FAIL;
	}

	/*
	 * Translate UMC channel address to Physical address
	 */
	ret = amdgpu_umc_convert_error_address(adev, &err_data, err_addr,
					ch_inst, umc_inst);
	if (ret)
		goto out;

	if (amdgpu_bad_page_threshold != 0) {
		amdgpu_ras_add_bad_pages(adev, err_data.err_addr,
						err_data.err_addr_cnt);
		amdgpu_ras_save_bad_pages(adev, NULL);
	}

out:
	kfree(err_data.err_addr);
	return ret;
}

static int amdgpu_umc_do_page_retirement(struct amdgpu_device *adev,
		void *ras_error_status,
		struct amdgpu_iv_entry *entry,
		bool reset)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	int ret = 0;

	kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);
	ret = amdgpu_dpm_get_ecc_info(adev, (void *)&(con->umc_ecc));
	if (ret == -EOPNOTSUPP) {
		if (adev->umc.ras && adev->umc.ras->ras_block.hw_ops &&
		    adev->umc.ras->ras_block.hw_ops->query_ras_error_count)
		    adev->umc.ras->ras_block.hw_ops->query_ras_error_count(adev, ras_error_status);

		if (adev->umc.ras && adev->umc.ras->ras_block.hw_ops &&
		    adev->umc.ras->ras_block.hw_ops->query_ras_error_address &&
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
			adev->umc.ras->ras_block.hw_ops->query_ras_error_address(adev, ras_error_status);
		}
	} else if (!ret) {
		if (adev->umc.ras &&
		    adev->umc.ras->ecc_info_query_ras_error_count)
		    adev->umc.ras->ecc_info_query_ras_error_count(adev, ras_error_status);

		if (adev->umc.ras &&
		    adev->umc.ras->ecc_info_query_ras_error_address &&
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
			adev->umc.ras->ecc_info_query_ras_error_address(adev, ras_error_status);
		}
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
			amdgpu_ras_save_bad_pages(adev, &(err_data->ue_count));

			amdgpu_dpm_send_hbm_bad_pages_num(adev, con->eeprom_control.ras_num_recs);

			if (con->update_channel_flag == true) {
				amdgpu_dpm_send_hbm_bad_channel_flag(adev, con->eeprom_control.bad_channel_bitmap);
				con->update_channel_flag = false;
			}
		}

		if (reset)
			amdgpu_ras_reset_gpu(adev);
	}

	kfree(err_data->err_addr);
	return AMDGPU_RAS_SUCCESS;
}

int amdgpu_umc_poison_handler(struct amdgpu_device *adev, bool reset)
{
	int ret = AMDGPU_RAS_SUCCESS;

	if (adev->gmc.xgmi.connected_to_cpu ||
		adev->gmc.is_app_apu) {
		if (reset) {
			/* MCA poison handler is only responsible for GPU reset,
			 * let MCA notifier do page retirement.
			 */
			kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);
			amdgpu_ras_reset_gpu(adev);
		}
		return ret;
	}

	if (!amdgpu_sriov_vf(adev)) {
		struct ras_err_data err_data = {0, 0, 0, NULL};
		struct ras_common_if head = {
			.block = AMDGPU_RAS_BLOCK__UMC,
		};
		struct ras_manager *obj = amdgpu_ras_find_obj(adev, &head);

		ret = amdgpu_umc_do_page_retirement(adev, &err_data, NULL, reset);

		if (ret == AMDGPU_RAS_SUCCESS && obj) {
			obj->err_data.ue_count += err_data.ue_count;
			obj->err_data.ce_count += err_data.ce_count;
		}
	} else {
		if (adev->virt.ops && adev->virt.ops->ras_poison_handler)
			adev->virt.ops->ras_poison_handler(adev);
		else
			dev_warn(adev->dev,
				"No ras_poison_handler interface in SRIOV!\n");
	}

	return ret;
}

int amdgpu_umc_process_ras_data_cb(struct amdgpu_device *adev,
		void *ras_error_status,
		struct amdgpu_iv_entry *entry)
{
	return amdgpu_umc_do_page_retirement(adev, ras_error_status, entry, true);
}

int amdgpu_umc_ras_sw_init(struct amdgpu_device *adev)
{
	int err;
	struct amdgpu_umc_ras *ras;

	if (!adev->umc.ras)
		return 0;

	ras = adev->umc.ras;

	err = amdgpu_ras_register_ras_block(adev, &ras->ras_block);
	if (err) {
		dev_err(adev->dev, "Failed to register umc ras block!\n");
		return err;
	}

	strcpy(adev->umc.ras->ras_block.ras_comm.name, "umc");
	ras->ras_block.ras_comm.block = AMDGPU_RAS_BLOCK__UMC;
	ras->ras_block.ras_comm.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE;
	adev->umc.ras_if = &ras->ras_block.ras_comm;

	if (!ras->ras_block.ras_late_init)
		ras->ras_block.ras_late_init = amdgpu_umc_ras_late_init;

	if (!ras->ras_block.ras_cb)
		ras->ras_block.ras_cb = amdgpu_umc_process_ras_data_cb;

	return 0;
}

int amdgpu_umc_ras_late_init(struct amdgpu_device *adev, struct ras_common_if *ras_block)
{
	int r;

	r = amdgpu_ras_block_late_init(adev, ras_block);
	if (r)
		return r;

	if (amdgpu_ras_is_supported(adev, ras_block->block)) {
		r = amdgpu_irq_get(adev, &adev->gmc.ecc_irq, 0);
		if (r)
			goto late_fini;
	}

	/* ras init of specific umc version */
	if (adev->umc.ras &&
	    adev->umc.ras->err_cnt_init)
		adev->umc.ras->err_cnt_init(adev);

	return 0;

late_fini:
	amdgpu_ras_block_late_fini(adev, ras_block);
	return r;
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

void amdgpu_umc_fill_error_record(struct ras_err_data *err_data,
		uint64_t err_addr,
		uint64_t retired_page,
		uint32_t channel_index,
		uint32_t umc_inst)
{
	struct eeprom_table_record *err_rec =
		&err_data->err_addr[err_data->err_addr_cnt];

	err_rec->address = err_addr;
	/* page frame address is saved */
	err_rec->retired_page = retired_page >> AMDGPU_GPU_PAGE_SHIFT;
	err_rec->ts = (uint64_t)ktime_get_real_seconds();
	err_rec->err_type = AMDGPU_RAS_EEPROM_ERR_NON_RECOVERABLE;
	err_rec->cu = 0;
	err_rec->mem_channel = channel_index;
	err_rec->mcumc_id = umc_inst;

	err_data->err_addr_cnt++;
}

int amdgpu_umc_loop_channels(struct amdgpu_device *adev,
			umc_func func, void *data)
{
	uint32_t node_inst       = 0;
	uint32_t umc_inst        = 0;
	uint32_t ch_inst         = 0;
	int ret = 0;

	if (adev->umc.node_inst_num) {
		LOOP_UMC_EACH_NODE_INST_AND_CH(node_inst, umc_inst, ch_inst) {
			ret = func(adev, node_inst, umc_inst, ch_inst, data);
			if (ret) {
				dev_err(adev->dev, "Node %d umc %d ch %d func returns %d\n",
					node_inst, umc_inst, ch_inst, ret);
				return ret;
			}
		}
	} else {
		LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) {
			ret = func(adev, 0, umc_inst, ch_inst, data);
			if (ret) {
				dev_err(adev->dev, "Umc %d ch %d func returns %d\n",
					umc_inst, ch_inst, ret);
				return ret;
			}
		}
	}

	return 0;
}
