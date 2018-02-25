/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
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

#include "mali_kbase.h"
#include "mali_kbase_hw.h"
#include "mali_kbase_mem_linux.h"
#include "mali_kbase_gator_api.h"
#include "mali_kbase_gator_hwcnt_names.h"

#define MALI_MAX_CORES_PER_GROUP		4
#define MALI_MAX_NUM_BLOCKS_PER_GROUP	8
#define MALI_COUNTERS_PER_BLOCK			64
#define MALI_BYTES_PER_COUNTER			4

struct kbase_gator_hwcnt_handles {
	struct kbase_device *kbdev;
	struct kbase_vinstr_client *vinstr_cli;
	void *vinstr_buffer;
	struct work_struct dump_work;
	int dump_complete;
	spinlock_t dump_lock;
};

static void dump_worker(struct work_struct *work);

const char * const *kbase_gator_hwcnt_init_names(uint32_t *total_counters)
{
	const char * const *hardware_counters;
	struct kbase_device *kbdev;
	uint32_t product_id;
	uint32_t count;

	if (!total_counters)
		return NULL;

	/* Get the first device - it doesn't matter in this case */
	kbdev = kbase_find_device(-1);
	if (!kbdev)
		return NULL;

	product_id = kbdev->gpu_props.props.core_props.product_id;

	if (GPU_ID_IS_NEW_FORMAT(product_id)) {
		switch (GPU_ID2_MODEL_MATCH_VALUE(product_id)) {
		case GPU_ID2_PRODUCT_TMIX:
			hardware_counters = hardware_counters_mali_tMIx;
			count = ARRAY_SIZE(hardware_counters_mali_tMIx);
			break;
		case GPU_ID2_PRODUCT_THEX:
			hardware_counters = hardware_counters_mali_tHEx;
			count = ARRAY_SIZE(hardware_counters_mali_tHEx);
			break;
		case GPU_ID2_PRODUCT_TSIX:
			hardware_counters = hardware_counters_mali_tSIx;
			count = ARRAY_SIZE(hardware_counters_mali_tSIx);
			break;
		case GPU_ID2_PRODUCT_TNOX:
			hardware_counters = hardware_counters_mali_tNOx;
			count = ARRAY_SIZE(hardware_counters_mali_tNOx);
			break;
		default:
			hardware_counters = NULL;
			count = 0;
			dev_err(kbdev->dev, "Unrecognized product ID: %u\n",
				product_id);
			break;
		}
	} else {
		switch (product_id) {
			/* If we are using a Mali-T60x device */
		case GPU_ID_PI_T60X:
			hardware_counters = hardware_counters_mali_t60x;
			count = ARRAY_SIZE(hardware_counters_mali_t60x);
			break;
			/* If we are using a Mali-T62x device */
		case GPU_ID_PI_T62X:
			hardware_counters = hardware_counters_mali_t62x;
			count = ARRAY_SIZE(hardware_counters_mali_t62x);
			break;
			/* If we are using a Mali-T72x device */
		case GPU_ID_PI_T72X:
			hardware_counters = hardware_counters_mali_t72x;
			count = ARRAY_SIZE(hardware_counters_mali_t72x);
			break;
			/* If we are using a Mali-T76x device */
		case GPU_ID_PI_T76X:
			hardware_counters = hardware_counters_mali_t76x;
			count = ARRAY_SIZE(hardware_counters_mali_t76x);
			break;
			/* If we are using a Mali-T82x device */
		case GPU_ID_PI_T82X:
			hardware_counters = hardware_counters_mali_t82x;
			count = ARRAY_SIZE(hardware_counters_mali_t82x);
			break;
			/* If we are using a Mali-T83x device */
		case GPU_ID_PI_T83X:
			hardware_counters = hardware_counters_mali_t83x;
			count = ARRAY_SIZE(hardware_counters_mali_t83x);
			break;
			/* If we are using a Mali-T86x device */
		case GPU_ID_PI_T86X:
			hardware_counters = hardware_counters_mali_t86x;
			count = ARRAY_SIZE(hardware_counters_mali_t86x);
			break;
			/* If we are using a Mali-T88x device */
		case GPU_ID_PI_TFRX:
			hardware_counters = hardware_counters_mali_t88x;
			count = ARRAY_SIZE(hardware_counters_mali_t88x);
			break;
		default:
			hardware_counters = NULL;
			count = 0;
			dev_err(kbdev->dev, "Unrecognized product ID: %u\n",
				product_id);
			break;
		}
	}

	/* Release the kbdev reference. */
	kbase_release_device(kbdev);

	*total_counters = count;

	/* If we return a string array take a reference on the module (or fail). */
	if (hardware_counters && !try_module_get(THIS_MODULE))
		return NULL;

	return hardware_counters;
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_init_names);

void kbase_gator_hwcnt_term_names(void)
{
	/* Release the module reference. */
	module_put(THIS_MODULE);
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_term_names);

struct kbase_gator_hwcnt_handles *kbase_gator_hwcnt_init(struct kbase_gator_hwcnt_info *in_out_info)
{
	struct kbase_gator_hwcnt_handles *hand;
	struct kbase_uk_hwcnt_reader_setup setup;
	uint32_t dump_size = 0, i = 0;

	if (!in_out_info)
		return NULL;

	hand = kzalloc(sizeof(*hand), GFP_KERNEL);
	if (!hand)
		return NULL;

	INIT_WORK(&hand->dump_work, dump_worker);
	spin_lock_init(&hand->dump_lock);

	/* Get the first device */
	hand->kbdev = kbase_find_device(-1);
	if (!hand->kbdev)
		goto free_hand;

	dump_size = kbase_vinstr_dump_size(hand->kbdev);
	hand->vinstr_buffer = kzalloc(dump_size, GFP_KERNEL);
	if (!hand->vinstr_buffer)
		goto release_device;
	in_out_info->kernel_dump_buffer = hand->vinstr_buffer;

	in_out_info->nr_cores = hand->kbdev->gpu_props.num_cores;
	in_out_info->nr_core_groups = hand->kbdev->gpu_props.num_core_groups;
	in_out_info->gpu_id = hand->kbdev->gpu_props.props.core_props.product_id;

	/* If we are using a v4 device (Mali-T6xx or Mali-T72x) */
	if (kbase_hw_has_feature(hand->kbdev, BASE_HW_FEATURE_V4)) {
		uint32_t cg, j;
		uint64_t core_mask;

		/* There are 8 hardware counters blocks per core group */
		in_out_info->hwc_layout = kmalloc(sizeof(enum hwc_type) *
			MALI_MAX_NUM_BLOCKS_PER_GROUP *
			in_out_info->nr_core_groups, GFP_KERNEL);

		if (!in_out_info->hwc_layout)
			goto free_vinstr_buffer;

		dump_size = in_out_info->nr_core_groups *
			MALI_MAX_NUM_BLOCKS_PER_GROUP *
			MALI_COUNTERS_PER_BLOCK *
			MALI_BYTES_PER_COUNTER;

		for (cg = 0; cg < in_out_info->nr_core_groups; cg++) {
			core_mask = hand->kbdev->gpu_props.props.coherency_info.group[cg].core_mask;

			for (j = 0; j < MALI_MAX_CORES_PER_GROUP; j++) {
				if (core_mask & (1u << j))
					in_out_info->hwc_layout[i++] = SHADER_BLOCK;
				else
					in_out_info->hwc_layout[i++] = RESERVED_BLOCK;
			}

			in_out_info->hwc_layout[i++] = TILER_BLOCK;
			in_out_info->hwc_layout[i++] = MMU_L2_BLOCK;

			in_out_info->hwc_layout[i++] = RESERVED_BLOCK;

			if (0 == cg)
				in_out_info->hwc_layout[i++] = JM_BLOCK;
			else
				in_out_info->hwc_layout[i++] = RESERVED_BLOCK;
		}
	/* If we are using any other device */
	} else {
		uint32_t nr_l2, nr_sc_bits, j;
		uint64_t core_mask;

		nr_l2 = hand->kbdev->gpu_props.props.l2_props.num_l2_slices;

		core_mask = hand->kbdev->gpu_props.props.coherency_info.group[0].core_mask;

		nr_sc_bits = fls64(core_mask);

		/* The job manager and tiler sets of counters
		 * are always present */
		in_out_info->hwc_layout = kmalloc(sizeof(enum hwc_type) * (2 + nr_sc_bits + nr_l2), GFP_KERNEL);

		if (!in_out_info->hwc_layout)
			goto free_vinstr_buffer;

		dump_size = (2 + nr_sc_bits + nr_l2) * MALI_COUNTERS_PER_BLOCK * MALI_BYTES_PER_COUNTER;

		in_out_info->hwc_layout[i++] = JM_BLOCK;
		in_out_info->hwc_layout[i++] = TILER_BLOCK;

		for (j = 0; j < nr_l2; j++)
			in_out_info->hwc_layout[i++] = MMU_L2_BLOCK;

		while (core_mask != 0ull) {
			if ((core_mask & 1ull) != 0ull)
				in_out_info->hwc_layout[i++] = SHADER_BLOCK;
			else
				in_out_info->hwc_layout[i++] = RESERVED_BLOCK;
			core_mask >>= 1;
		}
	}

	in_out_info->nr_hwc_blocks = i;
	in_out_info->size = dump_size;

	setup.jm_bm = in_out_info->bitmask[0];
	setup.tiler_bm = in_out_info->bitmask[1];
	setup.shader_bm = in_out_info->bitmask[2];
	setup.mmu_l2_bm = in_out_info->bitmask[3];
	hand->vinstr_cli = kbase_vinstr_hwcnt_kernel_setup(hand->kbdev->vinstr_ctx,
			&setup, hand->vinstr_buffer);
	if (!hand->vinstr_cli) {
		dev_err(hand->kbdev->dev, "Failed to register gator with vinstr core");
		goto free_layout;
	}

	return hand;

free_layout:
	kfree(in_out_info->hwc_layout);

free_vinstr_buffer:
	kfree(hand->vinstr_buffer);

release_device:
	kbase_release_device(hand->kbdev);

free_hand:
	kfree(hand);
	return NULL;
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_init);

void kbase_gator_hwcnt_term(struct kbase_gator_hwcnt_info *in_out_info, struct kbase_gator_hwcnt_handles *opaque_handles)
{
	if (in_out_info)
		kfree(in_out_info->hwc_layout);

	if (opaque_handles) {
		cancel_work_sync(&opaque_handles->dump_work);
		kbase_vinstr_detach_client(opaque_handles->vinstr_cli);
		kfree(opaque_handles->vinstr_buffer);
		kbase_release_device(opaque_handles->kbdev);
		kfree(opaque_handles);
	}
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_term);

static void dump_worker(struct work_struct *work)
{
	struct kbase_gator_hwcnt_handles *hand;

	hand = container_of(work, struct kbase_gator_hwcnt_handles, dump_work);
	if (!kbase_vinstr_hwc_dump(hand->vinstr_cli,
			BASE_HWCNT_READER_EVENT_MANUAL)) {
		spin_lock_bh(&hand->dump_lock);
		hand->dump_complete = 1;
		spin_unlock_bh(&hand->dump_lock);
	} else {
		schedule_work(&hand->dump_work);
	}
}

uint32_t kbase_gator_instr_hwcnt_dump_complete(
		struct kbase_gator_hwcnt_handles *opaque_handles,
		uint32_t * const success)
{

	if (opaque_handles && success) {
		*success = opaque_handles->dump_complete;
		opaque_handles->dump_complete = 0;
		return *success;
	}
	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_complete);

uint32_t kbase_gator_instr_hwcnt_dump_irq(struct kbase_gator_hwcnt_handles *opaque_handles)
{
	if (opaque_handles)
		schedule_work(&opaque_handles->dump_work);
	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_irq);
