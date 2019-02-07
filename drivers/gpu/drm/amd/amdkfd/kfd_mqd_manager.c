/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include "kfd_mqd_manager.h"
#include "amdgpu_amdkfd.h"

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev)
{
	switch (dev->device_info->asic_family) {
	case CHIP_KAVERI:
		return mqd_manager_init_cik(type, dev);
	case CHIP_HAWAII:
		return mqd_manager_init_cik_hawaii(type, dev);
	case CHIP_CARRIZO:
		return mqd_manager_init_vi(type, dev);
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		return mqd_manager_init_vi_tonga(type, dev);
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
		return mqd_manager_init_v9(type, dev);
	default:
		WARN(1, "Unexpected ASIC family %u",
		     dev->device_info->asic_family);
	}

	return NULL;
}

void mqd_symmetrically_map_cu_mask(struct mqd_manager *mm,
		const uint32_t *cu_mask, uint32_t cu_mask_count,
		uint32_t *se_mask)
{
	struct kfd_cu_info cu_info;
	uint32_t cu_per_sh[4] = {0};
	int i, se, cu = 0;

	amdgpu_amdkfd_get_cu_info(mm->dev->kgd, &cu_info);

	if (cu_mask_count > cu_info.cu_active_number)
		cu_mask_count = cu_info.cu_active_number;

	for (se = 0; se < cu_info.num_shader_engines; se++)
		for (i = 0; i < 4; i++)
			cu_per_sh[se] += hweight32(cu_info.cu_bitmap[se][i]);

	/* Symmetrically map cu_mask to all SEs:
	 * cu_mask[0] bit0 -> se_mask[0] bit0;
	 * cu_mask[0] bit1 -> se_mask[1] bit0;
	 * ... (if # SE is 4)
	 * cu_mask[0] bit4 -> se_mask[0] bit1;
	 * ...
	 */
	se = 0;
	for (i = 0; i < cu_mask_count; i++) {
		if (cu_mask[i / 32] & (1 << (i % 32)))
			se_mask[se] |= 1 << cu;

		do {
			se++;
			if (se == cu_info.num_shader_engines) {
				se = 0;
				cu++;
			}
		} while (cu >= cu_per_sh[se] && cu < 32);
	}
}
