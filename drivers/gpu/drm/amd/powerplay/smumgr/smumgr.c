/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <drm/amdgpu_drm.h>
#include "pp_instance.h"
#include "smumgr.h"
#include "cgs_common.h"
#include "linux/delay.h"
#include "cz_smumgr.h"
#include "tonga_smumgr.h"
#include "iceland_smumgr.h"
#include "fiji_smumgr.h"
#include "polaris10_smumgr.h"

int smum_init(struct amd_pp_init *pp_init, struct pp_instance *handle)
{
	struct pp_smumgr *smumgr;

	if ((handle == NULL) || (pp_init == NULL))
		return -EINVAL;

	smumgr = kzalloc(sizeof(struct pp_smumgr), GFP_KERNEL);
	if (smumgr == NULL)
		return -ENOMEM;

	smumgr->device = pp_init->device;
	smumgr->chip_family = pp_init->chip_family;
	smumgr->chip_id = pp_init->chip_id;
	smumgr->hw_revision = pp_init->rev_id;
	smumgr->usec_timeout = AMD_MAX_USEC_TIMEOUT;
	smumgr->reload_fw = 1;
	handle->smu_mgr = smumgr;

	switch (smumgr->chip_family) {
	case AMDGPU_FAMILY_CZ:
		cz_smum_init(smumgr);
		break;
	case AMDGPU_FAMILY_VI:
		switch (smumgr->chip_id) {
		case CHIP_TOPAZ:
			iceland_smum_init(smumgr);
			break;
		case CHIP_TONGA:
			tonga_smum_init(smumgr);
			break;
		case CHIP_FIJI:
			fiji_smum_init(smumgr);
			break;
		case CHIP_POLARIS11:
		case CHIP_POLARIS10:
			polaris10_smum_init(smumgr);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		kfree(smumgr);
		return -EINVAL;
	}

	return 0;
}

int smum_fini(struct pp_smumgr *smumgr)
{
	kfree(smumgr->device);
	kfree(smumgr);
	return 0;
}

int smum_get_argument(struct pp_smumgr *smumgr)
{
	if (NULL != smumgr->smumgr_funcs->get_argument)
		return smumgr->smumgr_funcs->get_argument(smumgr);

	return 0;
}

int smum_download_powerplay_table(struct pp_smumgr *smumgr,
								void **table)
{
	if (NULL != smumgr->smumgr_funcs->download_pptable_settings)
		return smumgr->smumgr_funcs->download_pptable_settings(smumgr,
									table);

	return 0;
}

int smum_upload_powerplay_table(struct pp_smumgr *smumgr)
{
	if (NULL != smumgr->smumgr_funcs->upload_pptable_settings)
		return smumgr->smumgr_funcs->upload_pptable_settings(smumgr);

	return 0;
}

int smum_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg)
{
	if (smumgr == NULL || smumgr->smumgr_funcs->send_msg_to_smc == NULL)
		return -EINVAL;

	return smumgr->smumgr_funcs->send_msg_to_smc(smumgr, msg);
}

int smum_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr,
					uint16_t msg, uint32_t parameter)
{
	if (smumgr == NULL ||
		smumgr->smumgr_funcs->send_msg_to_smc_with_parameter == NULL)
		return -EINVAL;
	return smumgr->smumgr_funcs->send_msg_to_smc_with_parameter(
						smumgr, msg, parameter);
}

/*
 * Returns once the part of the register indicated by the mask has
 * reached the given value.
 */
int smum_wait_on_register(struct pp_smumgr *smumgr,
				uint32_t index,
				uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	for (i = 0; i < smumgr->usec_timeout; i++) {
		cur_value = cgs_read_register(smumgr->device, index);
		if ((cur_value & mask) == (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic*/
	if (i == smumgr->usec_timeout)
		return -1;

	return 0;
}

int smum_wait_for_register_unequal(struct pp_smumgr *smumgr,
					uint32_t index,
					uint32_t value, uint32_t mask)
{
	uint32_t i;
	uint32_t cur_value;

	if (smumgr == NULL)
		return -EINVAL;

	for (i = 0; i < smumgr->usec_timeout; i++) {
		cur_value = cgs_read_register(smumgr->device,
									index);
		if ((cur_value & mask) != (value & mask))
			break;
		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == smumgr->usec_timeout)
		return -1;

	return 0;
}


/*
 * Returns once the part of the register indicated by the mask
 * has reached the given value.The indirect space is described by
 * giving the memory-mapped index of the indirect index register.
 */
int smum_wait_on_indirect_register(struct pp_smumgr *smumgr,
					uint32_t indirect_port,
					uint32_t index,
					uint32_t value,
					uint32_t mask)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	cgs_write_register(smumgr->device, indirect_port, index);
	return smum_wait_on_register(smumgr, indirect_port + 1,
						mask, value);
}

void smum_wait_for_indirect_register_unequal(
						struct pp_smumgr *smumgr,
						uint32_t indirect_port,
						uint32_t index,
						uint32_t value,
						uint32_t mask)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return;
	cgs_write_register(smumgr->device, indirect_port, index);
	smum_wait_for_register_unequal(smumgr, indirect_port + 1,
						value, mask);
}

int smu_allocate_memory(void *device, uint32_t size,
			 enum cgs_gpu_mem_type type,
			 uint32_t byte_align, uint64_t *mc_addr,
			 void **kptr, void *handle)
{
	int ret = 0;
	cgs_handle_t cgs_handle;

	if (device == NULL || handle == NULL ||
	    mc_addr == NULL || kptr == NULL)
		return -EINVAL;

	ret = cgs_alloc_gpu_mem(device, type, size, byte_align,
				0, 0, (cgs_handle_t *)handle);
	if (ret)
		return -ENOMEM;

	cgs_handle = *(cgs_handle_t *)handle;

	ret = cgs_gmap_gpu_mem(device, cgs_handle, mc_addr);
	if (ret)
		goto error_gmap;

	ret = cgs_kmap_gpu_mem(device, cgs_handle, kptr);
	if (ret)
		goto error_kmap;

	return 0;

error_kmap:
	cgs_gunmap_gpu_mem(device, cgs_handle);

error_gmap:
	cgs_free_gpu_mem(device, cgs_handle);
	return ret;
}

int smu_free_memory(void *device, void *handle)
{
	cgs_handle_t cgs_handle = (cgs_handle_t)handle;

	if (device == NULL || handle == NULL)
		return -EINVAL;

	cgs_kunmap_gpu_mem(device, cgs_handle);
	cgs_gunmap_gpu_mem(device, cgs_handle);
	cgs_free_gpu_mem(device, cgs_handle);

	return 0;
}
