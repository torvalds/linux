/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
#include "mali_kbase_mem_linux.h"
#include "mali_kbase_gator_api.h"
#include "mali_kbase_gator_hwcnt_names.h"

#define MALI_MAX_CORES_PER_GROUP		4
#define MALI_MAX_NUM_BLOCKS_PER_GROUP	8
#define MALI_COUNTERS_PER_BLOCK			64
#define MALI_BYTES_PER_COUNTER			4

struct kbase_gator_hwcnt_handles {
	struct kbase_device  *kbdev;
	struct kbase_context *kctx;
	struct kbase_hwc_dma_mapping kernel_dump_buffer_handle;
};

const char * const *kbase_gator_hwcnt_init_names(uint32_t *total_number_of_counters)
{
	uint32_t gpu_id;
	const char * const *hardware_counter_names;
	struct kbase_device *kbdev;

	if (!total_number_of_counters)
		return NULL;

	/* Get the first device - it doesn't matter in this case */
	kbdev = kbase_find_device(-1);
	if (!kbdev)
		return NULL;

	gpu_id = kbdev->gpu_props.props.core_props.product_id;

	switch (gpu_id) {
	/* If we are using a Mali-T60x device */
	case GPU_ID_PI_T60X:
			hardware_counter_names = hardware_counter_names_mali_t60x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t60x);
			break;
	/* If we are using a Mali-T62x device */
	case GPU_ID_PI_T62X:
			hardware_counter_names = hardware_counter_names_mali_t62x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t62x);
			break;
	/* If we are using a Mali-T72x device */
	case GPU_ID_PI_T72X:
			hardware_counter_names = hardware_counter_names_mali_t72x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t72x);
			break;
	/* If we are using a Mali-T76x device */
	case GPU_ID_PI_T76X:
			hardware_counter_names = hardware_counter_names_mali_t76x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t76x);
			break;
#ifdef MALI_INCLUDE_TFRX
	/* If we are using a Mali-TFRX device - for now just mimic the T760 counters */
	case GPU_ID_PI_TFRX:
			hardware_counter_names = hardware_counter_names_mali_t76x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t76x);
			break;
#endif /* MALI_INCLUDE_TRFX */
#ifdef MALI_INCLUDE_TF2X
	/* If we are using a Mali-TF2X device - for now just mimic the T760 counters */
	case GPU_ID_PI_TF2X:
			hardware_counter_names = hardware_counter_names_mali_t76x;
			*total_number_of_counters = ARRAY_SIZE(hardware_counter_names_mali_t76x);
			break;
#endif /* MALI_INCLUDE_TF2X */
	default:
			hardware_counter_names = NULL;
			*total_number_of_counters = 0;
			dev_err(kbdev->dev, "Unrecognized gpu ID: %u\n", gpu_id);
			break;
	}

	/* Release the kbdev reference. */
	kbase_release_device(kbdev);

	/* If we return a string array take a reference on the module (or fail). */
	if (hardware_counter_names && !try_module_get(THIS_MODULE))
		return NULL;

	return hardware_counter_names;
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_init_names)

void kbase_gator_hwcnt_term_names(void)
{
	/* Release the module reference. */
	module_put(THIS_MODULE);
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_term_names)

struct kbase_gator_hwcnt_handles *kbase_gator_hwcnt_init(struct kbase_gator_hwcnt_info *in_out_info)
{
	struct kbase_gator_hwcnt_handles *hand;
	struct kbase_uk_hwcnt_setup setup;
	mali_error err;
	uint32_t dump_size = 0, i = 0;

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
	hand->kctx = kbase_create_context(hand->kbdev);
	if (!hand->kctx)
		goto release_device;

	in_out_info->nr_cores = hand->kbdev->gpu_props.num_cores;
	in_out_info->nr_core_groups = hand->kbdev->gpu_props.num_core_groups;
	in_out_info->gpu_id = hand->kbdev->gpu_props.props.core_props.product_id;

	/* If we are using a Mali-T6xx or Mali-T72x device */
	if (in_out_info->gpu_id == GPU_ID_PI_T60X ||
	    in_out_info->gpu_id == GPU_ID_PI_T62X ||
	    in_out_info->gpu_id == GPU_ID_PI_T72X) {
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

			/* There are no implementation with L3 cache */
			in_out_info->hwc_layout[i++] = RESERVED_BLOCK;

			if (0 == cg)
				in_out_info->hwc_layout[i++] = JM_BLOCK;
			else
				in_out_info->hwc_layout[i++] = RESERVED_BLOCK;
		}
	/* If we are using a Mali-T76x device */
	} else if (
			(in_out_info->gpu_id == GPU_ID_PI_T76X)
#ifdef MALI_INCLUDE_TFRX
				|| (in_out_info->gpu_id == GPU_ID_PI_TFRX)
#endif /* MALI_INCLUDE_TFRX */
#ifdef MALI_INCLUDE_TF2X
				|| (in_out_info->gpu_id == GPU_ID_PI_TF2X)
#endif /* MALI_INCLUDE_TF2X */
			) {
		uint32_t nr_l2, nr_sc, j;
		uint64_t core_mask;

		nr_l2 = hand->kbdev->gpu_props.props.l2_props.num_l2_slices;

		core_mask = hand->kbdev->gpu_props.props.coherency_info.group[0].core_mask;

		nr_sc = hand->kbdev->gpu_props.props.coherency_info.group[0].num_cores;

		/* For Mali-T76x, the job manager and tiler sets of counters are always present */
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

	in_out_info->kernel_dump_buffer = kbase_va_alloc(hand->kctx, dump_size, &hand->kernel_dump_buffer_handle);
	if (!in_out_info->kernel_dump_buffer)
		goto free_layout;

	setup.dump_buffer = (uintptr_t)in_out_info->kernel_dump_buffer;
	setup.jm_bm = in_out_info->bitmask[0];
	setup.tiler_bm = in_out_info->bitmask[1];
	setup.shader_bm = in_out_info->bitmask[2];
	setup.mmu_l2_bm = in_out_info->bitmask[3];

	/* There are no implementations with L3 cache */
	setup.l3_cache_bm = 0;

	err = kbase_instr_hwcnt_enable(hand->kctx, &setup);
	if (err != MALI_ERROR_NONE)
		goto free_buffer;

	kbase_instr_hwcnt_clear(hand->kctx);

	return hand;

free_buffer:
	kbase_va_free(hand->kctx, &hand->kernel_dump_buffer_handle);

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
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_init)

void kbase_gator_hwcnt_term(struct kbase_gator_hwcnt_info *in_out_info, struct kbase_gator_hwcnt_handles *opaque_handles)
{
	if (in_out_info)
		kfree(in_out_info->hwc_layout);

	if (opaque_handles) {
		kbase_instr_hwcnt_disable(opaque_handles->kctx);
		kbase_va_free(opaque_handles->kctx, &opaque_handles->kernel_dump_buffer_handle);
		kbase_destroy_context(opaque_handles->kctx);
		kbase_release_device(opaque_handles->kbdev);
		kfree(opaque_handles);
	}
}
KBASE_EXPORT_SYMBOL(kbase_gator_hwcnt_term)

uint32_t kbase_gator_instr_hwcnt_dump_complete(struct kbase_gator_hwcnt_handles *opaque_handles, uint32_t * const success)
{
	if (opaque_handles && success)
		return (kbase_instr_hwcnt_dump_complete(opaque_handles->kctx, success) != 0);
	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_complete)

uint32_t kbase_gator_instr_hwcnt_dump_irq(struct kbase_gator_hwcnt_handles *opaque_handles)
{
	if (opaque_handles)
		return  (kbase_instr_hwcnt_dump_irq(opaque_handles->kctx) == MALI_ERROR_NONE);
	return 0;
}
KBASE_EXPORT_SYMBOL(kbase_gator_instr_hwcnt_dump_irq)
