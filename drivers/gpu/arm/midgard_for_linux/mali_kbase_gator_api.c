/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include "mali_kbase.h"
#include "mali_kbase_hw.h"
#include "mali_kbase_mem_linux.h"
#include "mali_kbase_gator_api.h"
#include "mali_kbase_gator_hwcnt_names.h"
#include "mali_kbase_instr.h"

#define MALI_MAX_CORES_PER_GROUP		4
#define MALI_MAX_NUM_BLOCKS_PER_GROUP	8
#define MALI_COUNTERS_PER_BLOCK			64
#define MALI_BYTES_PER_COUNTER			4

struct kbase_gator_hwcnt_handles {
	struct kbase_device  *kbdev;
	struct kbase_context *kctx;
	u64 hwcnt_gpu_va;
	void *hwcnt_cpu_va;
	struct kbase_vmap_struct hwcnt_map;
};

const char * const *kbase_gator_hwcnt_init_names(uint32_t *total_counters)
{
	uint32_t gpu_id;
	const char * const *hardware_counters;
	struct kbase_device *kbdev;

	if (!total_counters)
		return NULL;

	/* Get the first device - it doesn't matter in this case */
	kbdev = kbase_find_device(-1);
	if (!kbdev)
		return NULL;

	gpu_id = kbdev->gpu_props.props.core_props.product_id;

	switch (gpu_id) {
	/* If we are using a Mali-T60x device */
	case GPU_ID_PI_T60X:
		hardware_counters = hardware_counters_mali_t60x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t60x);
		break;
	/* If we are using a Mali-T62x device */
	case GPU_ID_PI_T62X:
		hardware_counters = hardware_counters_mali_t62x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t62x);
		break;
	/* If we are using a Mali-T72x device */
	case GPU_ID_PI_T72X:
		hardware_counters = hardware_counters_mali_t72x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t72x);
		break;
	/* If we are using a Mali-T76x device */
	case GPU_ID_PI_T76X:
		hardware_counters = hardware_counters_mali_t76x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t76x);
		break;
	/* If we are using a Mali-T82x device */
	case GPU_ID_PI_T82X:
		hardware_counters = hardware_counters_mali_t82x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t82x);
		break;
	/* If we are using a Mali-T83x device */
	case GPU_ID_PI_T83X:
		hardware_counters = hardware_counters_mali_t83x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t83x);
		break;
	/* If we are using a Mali-T86x device */
	case GPU_ID_PI_T86X:
		hardware_counters = hardware_counters_mali_t86x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t86x);
		break;
	/* If we are using a Mali-T88x device */
	case GPU_ID_PI_TFRX:
		hardware_counters = hardware_counters_mali_t88x;
		*total_counters = ARRAY_SIZE(hardware_counters_mali_t88x);
		 break;
	default:
		hardware_counters = NULL;
		*total_counters = 0;
		dev_err(kbdev->dev, "Unrecognized gpu ID: %u\n", gpu_id);
		break;
	}

	/* Release the kbdev reference. */
	kbase_release_device(kbdev);

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
	struct kbase_uk_hwcnt_setup setup;
	int err;
	uint32_t dump_size = 0, i = 0;
	struct kbase_va_region *reg;
	u64 flags;
	u64 nr_pages;
	u16 va_alignment = 0;

	if (!in_out_info)
		return NULL;

	hand = kzalloc(sizeof(*hand), GFP_KERNEL);
	if (!hand)
		return NULL;

	/* Get the first device */
	hand->kbdev = kbase_find_device(-1);
	if (!hand->kbdev)
		goto free_hand;

	/* Create a kbase_context */
	hand->kctx = kbase_create_context(hand->kbdev, true);
	if (!hand->kctx)
		goto release_device;

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
			goto destroy_context;

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
		uint32_t nr_l2, nr_sc, j;
		uint64_t core_mask;

		nr_l2 = hand->kbdev->gpu_props.props.l2_props.num_l2_slices;

		core_mask = hand->kbdev->gpu_props.props.coherency_info.group[0].core_mask;

		nr_sc = hand->kbdev->gpu_props.props.coherency_info.group[0].num_cores;

		/* The job manager and tiler sets of counters
		 * are always present */
		in_out_info->hwc_layout = kmalloc(sizeof(enum hwc_type) * (2 + nr_sc + nr_l2), GFP_KERNEL);

		if (!in_out_info->hwc_layout)
			goto destroy_context;

		dump_size = (2 + nr_sc + nr_l2) * MALI_COUNTERS_PER_BLOCK * MALI_BYTES_PER_COUNTER;

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

	flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_WR;
	nr_pages = PFN_UP(dump_size);
	reg = kbase_mem_alloc(hand->kctx, nr_pages, nr_pages, 0,
			&flags, &hand->hwcnt_gpu_va, &va_alignment);
	if (!reg)
		goto free_layout;

	hand->hwcnt_cpu_va = kbase_vmap(hand->kctx, hand->hwcnt_gpu_va,
			dump_size, &hand->hwcnt_map);

	if (!hand->hwcnt_cpu_va)
		goto free_buffer;

	in_out_info->kernel_dump_buffer = hand->hwcnt_cpu_va;
	memset(in_out_info->kernel_dump_buffer, 0, nr_pages * PAGE_SIZE);

	/*setup.dump_buffer = (uintptr_t)in_out_info->kernel_dump_buffer;*/
	setup.dump_buffer = hand->hwcnt_gpu_va;
	setup.jm_bm = in_out_info->bitmask[0];
	setup.tiler_bm = in_out_info->bitmask[1];
	setup.shader_bm = in_out_info->bitmask[2];
	setup.mmu_l2_bm = in_out_info->bitmask[3];

	err = kbase_instr_hwcnt_enable(hand->kctx, &setup);
	if (err)
		goto free_unmap;

	kbase_instr_hwcnt_clear(hand->kctx);

	return hand;

free_unmap:
	kbase_vunmap(hand->kctx, &hand->hwcnt_map);

free_buffer:
	kbase_mem_free(hand->kctx, hand->hwcnt_gpu_va);

free_layout:
	kfree(in_out_info->hwc_layout);

destroy_context:
	kbase_destroy_context(hand->kctx);

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
		kbase_instr_hwcnt_disable(opaque_handles->kctx);
		kbase_vunmap(opaque_handles->kctx, &opaque_handles->hwcnt_map);
		kbase_mem_free(opaque_handles->kctx, opaque_handles->hwcnt_gpu_va);
		kbase_destroy_context(opaque_handles->kctx);
		kbase_release_device(opaque_handles->kbdev);
		kfree(opaque_handles);
	}
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_term);

uint32_t kbase_gator_instr_hwcnt_dump_complete(
		struct kbase_gator_hwcnt_handles *opaque_handles,
		uint32_t * const success)
{
	bool ret_res, success_res;

	if (opaque_handles && success) {
		ret_res = kbase_instr_hwcnt_dump_complete(opaque_handles->kctx,
				&success_res);
		*success = (uint32_t)success_res;
		return (uint32_t)(ret_res != 0);
	}
	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_complete);

uint32_t kbase_gator_instr_hwcnt_dump_irq(struct kbase_gator_hwcnt_handles *opaque_handles)
{
	if (opaque_handles)
		return (kbase_instr_hwcnt_request_dump(
				opaque_handles->kctx) == 0);

	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_irq);
