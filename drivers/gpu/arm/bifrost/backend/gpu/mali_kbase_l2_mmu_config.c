// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */


#include <mali_kbase.h>
#include <mali_kbase_bits.h>
#include <mali_kbase_config_defaults.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include "mali_kbase_l2_mmu_config.h"

/**
 * struct l2_mmu_config_limit_region
 *
 * @value:    The default value to load into the L2_MMU_CONFIG register
 * @mask:     The shifted mask of the field in the L2_MMU_CONFIG register
 * @shift:    The shift of where the field starts in the L2_MMU_CONFIG register
 *            This should be the same value as the smaller of the two mask
 *            values
 */
struct l2_mmu_config_limit_region {
	u32 value, mask, shift;
};

/**
 * struct l2_mmu_config_limit
 *
 * @product_model:    The GPU for which this entry applies
 * @read:             Values for the read limit field
 * @write:            Values for the write limit field
 */
struct l2_mmu_config_limit {
	u32 product_model;
	struct l2_mmu_config_limit_region read;
	struct l2_mmu_config_limit_region write;
};

/*
 * Zero represents no limit
 *
 * For LBEX TBEX TTRX and TNAX:
 *   The value represents the number of outstanding reads (6 bits) or writes (5 bits)
 *
 * For all other GPUS it is a fraction see: mali_kbase_config_defaults.h
 */
static const struct l2_mmu_config_limit limits[] = {
	 /* GPU                       read                  write            */
	 {GPU_ID2_PRODUCT_LBEX, {0, GENMASK(10, 5), 5}, {0, GENMASK(16, 12), 12} },
	 {GPU_ID2_PRODUCT_TBEX, {0, GENMASK(10, 5), 5}, {0, GENMASK(16, 12), 12} },
	 {GPU_ID2_PRODUCT_TTRX, {0, GENMASK(12, 7), 7}, {0, GENMASK(17, 13), 13} },
	 {GPU_ID2_PRODUCT_TNAX, {0, GENMASK(12, 7), 7}, {0, GENMASK(17, 13), 13} },
	 {GPU_ID2_PRODUCT_TGOX,
	   {KBASE_3BIT_AID_32, GENMASK(14, 12), 12},
	   {KBASE_3BIT_AID_32, GENMASK(17, 15), 15} },
	 {GPU_ID2_PRODUCT_TNOX,
	   {KBASE_3BIT_AID_32, GENMASK(14, 12), 12},
	   {KBASE_3BIT_AID_32, GENMASK(17, 15), 15} },
};

void kbase_set_mmu_quirks(struct kbase_device *kbdev)
{
	/* All older GPUs had 2 bits for both fields, this is a default */
	struct l2_mmu_config_limit limit = {
		  0, /* Any GPU not in the limits array defined above */
		 {KBASE_AID_32, GENMASK(25, 24), 24},
		 {KBASE_AID_32, GENMASK(27, 26), 26}
		};
	u32 product_model, gpu_id;
	u32 mmu_config;
	int i;

	gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	product_model = gpu_id & GPU_ID2_PRODUCT_MODEL;

	/* Limit the GPU bus bandwidth if the platform needs this. */
	for (i = 0; i < ARRAY_SIZE(limits); i++) {
		if (product_model == limits[i].product_model) {
			limit = limits[i];
			break;
		}
	}

	mmu_config = kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_MMU_CONFIG));

	mmu_config &= ~(limit.read.mask | limit.write.mask);
	/* Can't use FIELD_PREP() macro here as the mask isn't constant */
	mmu_config |= (limit.read.value << limit.read.shift) |
		      (limit.write.value << limit.write.shift);

	kbdev->hw_quirks_mmu = mmu_config;

	if (kbdev->system_coherency == COHERENCY_ACE) {
		/* Allow memory configuration disparity to be ignored,
		 * we optimize the use of shared memory and thus we
		 * expect some disparity in the memory configuration.
		 */
		kbdev->hw_quirks_mmu |= L2_MMU_CONFIG_ALLOW_SNOOP_DISPARITY;
	}
}
