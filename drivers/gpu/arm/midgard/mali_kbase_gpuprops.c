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





/**
 * @file mali_kbase_gpuprops.c
 * Base kernel property query APIs
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gpuprops.h>

/**
 * @brief Extracts bits from a 32-bit bitfield.
 * @hideinitializer
 *
 * @param[in]    value       The value from which to extract bits.
 * @param[in]    offset      The first bit to extract (0 being the LSB).
 * @param[in]    size        The number of bits to extract.
 * @return                   Bits [@a offset, @a offset + @a size) from @a value.
 *
 * @pre offset + size <= 32.
 */
/* from mali_cdsb.h */
#define KBASE_UBFX32(value, offset, size) \
	(((u32)(value) >> (u32)(offset)) & (u32)((1ULL << (u32)(size)) - 1))

mali_error kbase_gpuprops_uk_get_props(struct kbase_context *kctx, struct kbase_uk_gpuprops * const kbase_props)
{
	kbase_gpuprops_clock_speed_function get_gpu_speed_mhz;
	u32 gpu_speed_mhz;
	int rc = 1;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != kbase_props);

	/* Current GPU speed is requested from the system integrator via the KBASE_CONFIG_ATTR_GPU_SPEED_FUNC function.
	 * If that function fails, or the function is not provided by the system integrator, we report the maximum
	 * GPU speed as specified by GPU_FREQ_KHZ_MAX.
	 */
	get_gpu_speed_mhz = (kbase_gpuprops_clock_speed_function) kbasep_get_config_value(kctx->kbdev, kctx->kbdev->config_attributes, KBASE_CONFIG_ATTR_GPU_SPEED_FUNC);
	if (get_gpu_speed_mhz != NULL) {
		rc = get_gpu_speed_mhz(&gpu_speed_mhz);
#ifdef CONFIG_MALI_DEBUG
		/* Issue a warning message when the reported GPU speed falls outside the min/max range */
		if (rc == 0) {
			u32 gpu_speed_khz = gpu_speed_mhz * 1000;
			if (gpu_speed_khz < kctx->kbdev->gpu_props.props.core_props.gpu_freq_khz_min || gpu_speed_khz > kctx->kbdev->gpu_props.props.core_props.gpu_freq_khz_max)
				dev_warn(kctx->kbdev->dev, "GPU Speed is outside of min/max range (got %lu Khz, min %lu Khz, max %lu Khz)\n", (unsigned long)gpu_speed_khz, (unsigned long)kctx->kbdev->gpu_props.props.core_props.gpu_freq_khz_min, (unsigned long)kctx->kbdev->gpu_props.props.core_props.gpu_freq_khz_max);
		}
#endif				/* CONFIG_MALI_DEBUG */
	}
	if (rc != 0)
		gpu_speed_mhz = kctx->kbdev->gpu_props.props.core_props.gpu_freq_khz_max / 1000;

	kctx->kbdev->gpu_props.props.core_props.gpu_speed_mhz = gpu_speed_mhz;

	memcpy(&kbase_props->props, &kctx->kbdev->gpu_props.props, sizeof(kbase_props->props));

	return MALI_ERROR_NONE;
}

STATIC void kbase_gpuprops_dump_registers(struct kbase_device *kbdev, struct kbase_gpuprops_regdump *regdump)
{
	int i;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != regdump);

	/* Fill regdump with the content of the relevant registers */
	regdump->gpu_id = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	regdump->l2_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L2_FEATURES));
	regdump->l3_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L3_FEATURES));
	regdump->tiler_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(TILER_FEATURES));
	regdump->mem_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(MEM_FEATURES));
	regdump->mmu_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(MMU_FEATURES));
	regdump->as_present = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(AS_PRESENT));
	regdump->js_present = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(JS_PRESENT));

	for (i = 0; i < MIDG_MAX_JOB_SLOTS; i++)
		regdump->js_features[i] = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(JS_FEATURES_REG(i)));

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		regdump->texture_features[i] = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(TEXTURE_FEATURES_REG(i)));

	regdump->thread_max_threads = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(THREAD_MAX_THREADS));
	regdump->thread_max_workgroup_size = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(THREAD_MAX_WORKGROUP_SIZE));
	regdump->thread_max_barrier_size = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(THREAD_MAX_BARRIER_SIZE));
	regdump->thread_features = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(THREAD_FEATURES));

	regdump->shader_present_lo = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(SHADER_PRESENT_LO));
	regdump->shader_present_hi = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(SHADER_PRESENT_HI));

	regdump->tiler_present_lo = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(TILER_PRESENT_LO));
	regdump->tiler_present_hi = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(TILER_PRESENT_HI));

	regdump->l2_present_lo = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L2_PRESENT_LO));
	regdump->l2_present_hi = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L2_PRESENT_HI));

	regdump->l3_present_lo = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L3_PRESENT_LO));
	regdump->l3_present_hi = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(L3_PRESENT_HI));
}

STATIC void kbase_gpuprops_construct_coherent_groups(base_gpu_props * const props)
{
	struct mali_base_gpu_coherent_group *current_group;
	u64 group_present;
	u64 group_mask;
	u64 first_set, first_set_prev;
	u32 num_groups = 0;

	KBASE_DEBUG_ASSERT(NULL != props);

	props->coherency_info.coherency = props->raw_props.mem_features;
	props->coherency_info.num_core_groups = hweight64(props->raw_props.l2_present);

	if (props->coherency_info.coherency & GROUPS_L3_COHERENT) {
		/* Group is l3 coherent */
		group_present = props->raw_props.l3_present;
	} else if (props->coherency_info.coherency & GROUPS_L2_COHERENT) {
		/* Group is l2 coherent */
		group_present = props->raw_props.l2_present;
	} else {
		/* Group is l1 coherent */
		group_present = props->raw_props.shader_present;
	}

	/*
	 * The coherent group mask can be computed from the l2/l3 present
	 * register.
	 *
	 * For the coherent group n:
	 * group_mask[n] = (first_set[n] - 1) & ~(first_set[n-1] - 1)
	 * where first_set is group_present with only its nth set-bit kept
	 * (i.e. the position from where a new group starts).
	 *
	 * For instance if the groups are l2 coherent and l2_present=0x0..01111:
	 * The first mask is:
	 * group_mask[1] = (first_set[1] - 1) & ~(first_set[0] - 1)
	 *               = (0x0..010     - 1) & ~(0x0..01      - 1)
	 *               =  0x0..00f
	 * The second mask is:
	 * group_mask[2] = (first_set[2] - 1) & ~(first_set[1] - 1)
	 *               = (0x0..100     - 1) & ~(0x0..010     - 1)
	 *               =  0x0..0f0
	 * And so on until all the bits from group_present have been cleared
	 * (i.e. there is no group left).
	 */

	current_group = props->coherency_info.group;
	first_set = group_present & ~(group_present - 1);

	while (group_present != 0 && num_groups < BASE_MAX_COHERENT_GROUPS) {
		group_present -= first_set;	/* Clear the current group bit */
		first_set_prev = first_set;

		first_set = group_present & ~(group_present - 1);
		group_mask = (first_set - 1) & ~(first_set_prev - 1);

		/* Populate the coherent_group structure for each group */
		current_group->core_mask = group_mask & props->raw_props.shader_present;
		current_group->num_cores = hweight64(current_group->core_mask);

		num_groups++;
		current_group++;
	}

	if (group_present != 0)
		pr_warn("Too many coherent groups (keeping only %d groups).\n", BASE_MAX_COHERENT_GROUPS);

	props->coherency_info.num_groups = num_groups;
}

/**
 * @brief Get the GPU configuration
 *
 * Fill the base_gpu_props structure with values from the GPU configuration registers.
 * Only the raw properties are filled in this function
 *
 * @param gpu_props  The base_gpu_props structure
 * @param kbdev      The struct kbase_device structure for the device
 */
static void kbase_gpuprops_get_props(base_gpu_props * const gpu_props, struct kbase_device *kbdev)
{
	struct kbase_gpuprops_regdump regdump;
	int i;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != gpu_props);

	/* Dump relevant registers */
	kbase_gpuprops_dump_registers(kbdev, &regdump);
	gpu_props->raw_props.gpu_id = regdump.gpu_id;
	gpu_props->raw_props.tiler_features = regdump.tiler_features;
	gpu_props->raw_props.mem_features = regdump.mem_features;
	gpu_props->raw_props.mmu_features = regdump.mmu_features;
	gpu_props->raw_props.l2_features = regdump.l2_features;
	gpu_props->raw_props.l3_features = regdump.l3_features;

	gpu_props->raw_props.as_present = regdump.as_present;
	gpu_props->raw_props.js_present = regdump.js_present;
	gpu_props->raw_props.shader_present = ((u64) regdump.shader_present_hi << 32) + regdump.shader_present_lo;
	gpu_props->raw_props.tiler_present = ((u64) regdump.tiler_present_hi << 32) + regdump.tiler_present_lo;
	gpu_props->raw_props.l2_present = ((u64) regdump.l2_present_hi << 32) + regdump.l2_present_lo;
	gpu_props->raw_props.l3_present = ((u64) regdump.l3_present_hi << 32) + regdump.l3_present_lo;

	for (i = 0; i < MIDG_MAX_JOB_SLOTS; i++)
		gpu_props->raw_props.js_features[i] = regdump.js_features[i];

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		gpu_props->raw_props.texture_features[i] = regdump.texture_features[i];

	gpu_props->raw_props.thread_max_barrier_size = regdump.thread_max_barrier_size;
	gpu_props->raw_props.thread_max_threads = regdump.thread_max_threads;
	gpu_props->raw_props.thread_max_workgroup_size = regdump.thread_max_workgroup_size;
	gpu_props->raw_props.thread_features = regdump.thread_features;
}

/**
 * @brief Calculate the derived properties
 *
 * Fill the base_gpu_props structure with values derived from the GPU configuration registers
 *
 * @param gpu_props  The base_gpu_props structure
 * @param kbdev      The struct kbase_device structure for the device
 */
static void kbase_gpuprops_calculate_props(base_gpu_props * const gpu_props, struct kbase_device *kbdev)
{
	int i;

	/* Populate the base_gpu_props structure */
	gpu_props->core_props.version_status = KBASE_UBFX32(gpu_props->raw_props.gpu_id, 0U, 4);
	gpu_props->core_props.minor_revision = KBASE_UBFX32(gpu_props->raw_props.gpu_id, 4U, 8);
	gpu_props->core_props.major_revision = KBASE_UBFX32(gpu_props->raw_props.gpu_id, 12U, 4);
	gpu_props->core_props.product_id = KBASE_UBFX32(gpu_props->raw_props.gpu_id, 16U, 16);
	gpu_props->core_props.log2_program_counter_size = KBASE_GPU_PC_SIZE_LOG2;
	gpu_props->core_props.gpu_available_memory_size = totalram_pages << PAGE_SHIFT;

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		gpu_props->core_props.texture_features[i] = gpu_props->raw_props.texture_features[i];

	gpu_props->l2_props.log2_line_size = KBASE_UBFX32(gpu_props->raw_props.l2_features, 0U, 8);
	gpu_props->l2_props.log2_cache_size = KBASE_UBFX32(gpu_props->raw_props.l2_features, 16U, 8);
	gpu_props->l2_props.num_l2_slices = 1;
	if (gpu_props->core_props.product_id == GPU_ID_PI_T76X) {
		gpu_props->l2_props.num_l2_slices = KBASE_UBFX32(gpu_props->raw_props.mem_features, 8U, 4) + 1;
	}

	gpu_props->l3_props.log2_line_size = KBASE_UBFX32(gpu_props->raw_props.l3_features, 0U, 8);
	gpu_props->l3_props.log2_cache_size = KBASE_UBFX32(gpu_props->raw_props.l3_features, 16U, 8);

	gpu_props->tiler_props.bin_size_bytes = 1 << KBASE_UBFX32(gpu_props->raw_props.tiler_features, 0U, 6);
	gpu_props->tiler_props.max_active_levels = KBASE_UBFX32(gpu_props->raw_props.tiler_features, 8U, 4);

	if (gpu_props->raw_props.thread_max_threads == 0)
		gpu_props->thread_props.max_threads = THREAD_MT_DEFAULT;
	else
		gpu_props->thread_props.max_threads = gpu_props->raw_props.thread_max_threads;

	if (gpu_props->raw_props.thread_max_workgroup_size == 0)
		gpu_props->thread_props.max_workgroup_size = THREAD_MWS_DEFAULT;
	else
		gpu_props->thread_props.max_workgroup_size = gpu_props->raw_props.thread_max_workgroup_size;

	if (gpu_props->raw_props.thread_max_barrier_size == 0)
		gpu_props->thread_props.max_barrier_size = THREAD_MBS_DEFAULT;
	else
		gpu_props->thread_props.max_barrier_size = gpu_props->raw_props.thread_max_barrier_size;

	gpu_props->thread_props.max_registers = KBASE_UBFX32(gpu_props->raw_props.thread_features, 0U, 16);
	gpu_props->thread_props.max_task_queue = KBASE_UBFX32(gpu_props->raw_props.thread_features, 16U, 8);
	gpu_props->thread_props.max_thread_group_split = KBASE_UBFX32(gpu_props->raw_props.thread_features, 24U, 6);
	gpu_props->thread_props.impl_tech = KBASE_UBFX32(gpu_props->raw_props.thread_features, 30U, 2);

	/* If values are not specified, then use defaults */
	if (gpu_props->thread_props.max_registers == 0) {
		gpu_props->thread_props.max_registers = THREAD_MR_DEFAULT;
		gpu_props->thread_props.max_task_queue = THREAD_MTQ_DEFAULT;
		gpu_props->thread_props.max_thread_group_split = THREAD_MTGS_DEFAULT;
	}
	/* Initialize the coherent_group structure for each group */
	kbase_gpuprops_construct_coherent_groups(gpu_props);
}

void kbase_gpuprops_set(struct kbase_device *kbdev)
{
	kbase_gpu_props *gpu_props;
	struct midg_raw_gpu_props *raw;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	gpu_props = &kbdev->gpu_props;
	raw = &gpu_props->props.raw_props;

	/* Initialize the base_gpu_props structure from the hardware */
	kbase_gpuprops_get_props(&gpu_props->props, kbdev);

	/* Populate the derived properties */
	kbase_gpuprops_calculate_props(&gpu_props->props, kbdev);

	/* Populate kbase-only fields */
	gpu_props->l2_props.associativity = KBASE_UBFX32(raw->l2_features, 8U, 8);
	gpu_props->l2_props.external_bus_width = KBASE_UBFX32(raw->l2_features, 24U, 8);

	gpu_props->l3_props.associativity = KBASE_UBFX32(raw->l3_features, 8U, 8);
	gpu_props->l3_props.external_bus_width = KBASE_UBFX32(raw->l3_features, 24U, 8);

	gpu_props->mem.core_group = KBASE_UBFX32(raw->mem_features, 0U, 1);
	gpu_props->mem.supergroup = KBASE_UBFX32(raw->mem_features, 1U, 1);

	gpu_props->mmu.va_bits = KBASE_UBFX32(raw->mmu_features, 0U, 8);
	gpu_props->mmu.pa_bits = KBASE_UBFX32(raw->mmu_features, 8U, 8);

	gpu_props->num_cores = hweight64(raw->shader_present);
	gpu_props->num_core_groups = hweight64(raw->l2_present);
	gpu_props->num_supergroups = hweight64(raw->l3_present);
	gpu_props->num_address_spaces = hweight32(raw->as_present);
	gpu_props->num_job_slots = hweight32(raw->js_present);
}
