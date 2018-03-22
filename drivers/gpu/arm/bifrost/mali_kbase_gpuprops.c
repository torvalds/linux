/*
 *
 * (C) COPYRIGHT 2011-2018 ARM Limited. All rights reserved.
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



/*
 * Base kernel property query APIs
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gpuprops.h>
#include <mali_kbase_hwaccess_gpuprops.h>
#include "mali_kbase_ioctl.h"
#include <linux/clk.h>

/**
 * KBASE_UBFX32 - Extracts bits from a 32-bit bitfield.
 * @value:  The value from which to extract bits.
 * @offset: The first bit to extract (0 being the LSB).
 * @size:   The number of bits to extract.
 *
 * Context: @offset + @size <= 32.
 *
 * Return: Bits [@offset, @offset + @size) from @value.
 */
/* from mali_cdsb.h */
#define KBASE_UBFX32(value, offset, size) \
	(((u32)(value) >> (u32)(offset)) & (u32)((1ULL << (u32)(size)) - 1))

static void kbase_gpuprops_construct_coherent_groups(base_gpu_props * const props)
{
	struct mali_base_gpu_coherent_group *current_group;
	u64 group_present;
	u64 group_mask;
	u64 first_set, first_set_prev;
	u32 num_groups = 0;

	KBASE_DEBUG_ASSERT(NULL != props);

	props->coherency_info.coherency = props->raw_props.mem_features;
	props->coherency_info.num_core_groups = hweight64(props->raw_props.l2_present);

	if (props->coherency_info.coherency & GROUPS_L2_COHERENT) {
		/* Group is l2 coherent */
		group_present = props->raw_props.l2_present;
	} else {
		/* Group is l1 coherent */
		group_present = props->raw_props.shader_present;
	}

	/*
	 * The coherent group mask can be computed from the l2 present
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
 * kbase_gpuprops_get_props - Get the GPU configuration
 * @gpu_props: The &base_gpu_props structure
 * @kbdev: The &struct kbase_device structure for the device
 *
 * Fill the &base_gpu_props structure with values from the GPU configuration
 * registers. Only the raw properties are filled in this function
 */
static void kbase_gpuprops_get_props(base_gpu_props * const gpu_props, struct kbase_device *kbdev)
{
	struct kbase_gpuprops_regdump regdump;
	int i;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != gpu_props);

	/* Dump relevant registers */
	kbase_backend_gpuprops_get(kbdev, &regdump);

	gpu_props->raw_props.gpu_id = regdump.gpu_id;
	gpu_props->raw_props.tiler_features = regdump.tiler_features;
	gpu_props->raw_props.mem_features = regdump.mem_features;
	gpu_props->raw_props.mmu_features = regdump.mmu_features;
	gpu_props->raw_props.l2_features = regdump.l2_features;
	gpu_props->raw_props.core_features = regdump.core_features;

	gpu_props->raw_props.as_present = regdump.as_present;
	gpu_props->raw_props.js_present = regdump.js_present;
	gpu_props->raw_props.shader_present =
		((u64) regdump.shader_present_hi << 32) +
		regdump.shader_present_lo;
	gpu_props->raw_props.tiler_present =
		((u64) regdump.tiler_present_hi << 32) +
		regdump.tiler_present_lo;
	gpu_props->raw_props.l2_present =
		((u64) regdump.l2_present_hi << 32) +
		regdump.l2_present_lo;
	gpu_props->raw_props.stack_present =
		((u64) regdump.stack_present_hi << 32) +
		regdump.stack_present_lo;

	for (i = 0; i < GPU_MAX_JOB_SLOTS; i++)
		gpu_props->raw_props.js_features[i] = regdump.js_features[i];

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		gpu_props->raw_props.texture_features[i] = regdump.texture_features[i];

	gpu_props->raw_props.thread_max_barrier_size = regdump.thread_max_barrier_size;
	gpu_props->raw_props.thread_max_threads = regdump.thread_max_threads;
	gpu_props->raw_props.thread_max_workgroup_size = regdump.thread_max_workgroup_size;
	gpu_props->raw_props.thread_features = regdump.thread_features;
	gpu_props->raw_props.thread_tls_alloc = regdump.thread_tls_alloc;
}

void kbase_gpuprops_update_core_props_gpu_id(base_gpu_props * const gpu_props)
{
	gpu_props->core_props.version_status =
		KBASE_UBFX32(gpu_props->raw_props.gpu_id, 0U, 4);
	gpu_props->core_props.minor_revision =
		KBASE_UBFX32(gpu_props->raw_props.gpu_id, 4U, 8);
	gpu_props->core_props.major_revision =
		KBASE_UBFX32(gpu_props->raw_props.gpu_id, 12U, 4);
	gpu_props->core_props.product_id =
		KBASE_UBFX32(gpu_props->raw_props.gpu_id, 16U, 16);
}

/**
 * kbase_gpuprops_calculate_props - Calculate the derived properties
 * @gpu_props: The &base_gpu_props structure
 * @kbdev:     The &struct kbase_device structure for the device
 *
 * Fill the &base_gpu_props structure with values derived from the GPU
 * configuration registers
 */
static void kbase_gpuprops_calculate_props(base_gpu_props * const gpu_props, struct kbase_device *kbdev)
{
	int i;

	/* Populate the base_gpu_props structure */
	kbase_gpuprops_update_core_props_gpu_id(gpu_props);
	gpu_props->core_props.log2_program_counter_size = KBASE_GPU_PC_SIZE_LOG2;
	gpu_props->core_props.gpu_available_memory_size = totalram_pages << PAGE_SHIFT;
	gpu_props->core_props.num_exec_engines =
		KBASE_UBFX32(gpu_props->raw_props.core_features, 0, 4);

	for (i = 0; i < BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS; i++)
		gpu_props->core_props.texture_features[i] = gpu_props->raw_props.texture_features[i];

	gpu_props->l2_props.log2_line_size = KBASE_UBFX32(gpu_props->raw_props.l2_features, 0U, 8);
	gpu_props->l2_props.log2_cache_size = KBASE_UBFX32(gpu_props->raw_props.l2_features, 16U, 8);

	/* Field with number of l2 slices is added to MEM_FEATURES register
	 * since t76x. Below code assumes that for older GPU reserved bits will
	 * be read as zero. */
	gpu_props->l2_props.num_l2_slices =
		KBASE_UBFX32(gpu_props->raw_props.mem_features, 8U, 4) + 1;

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

	if (gpu_props->raw_props.thread_tls_alloc == 0)
		gpu_props->thread_props.tls_alloc =
				gpu_props->thread_props.max_threads;
	else
		gpu_props->thread_props.tls_alloc =
				gpu_props->raw_props.thread_tls_alloc;

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
	struct kbase_gpu_props *gpu_props;
	struct gpu_raw_gpu_props *raw;

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

	gpu_props->mem.core_group = KBASE_UBFX32(raw->mem_features, 0U, 1);

	gpu_props->mmu.va_bits = KBASE_UBFX32(raw->mmu_features, 0U, 8);
	gpu_props->mmu.pa_bits = KBASE_UBFX32(raw->mmu_features, 8U, 8);

	gpu_props->num_cores = hweight64(raw->shader_present);
	gpu_props->num_core_groups = hweight64(raw->l2_present);
	gpu_props->num_address_spaces = hweight32(raw->as_present);
	gpu_props->num_job_slots = hweight32(raw->js_present);
}

void kbase_gpuprops_set_features(struct kbase_device *kbdev)
{
	base_gpu_props *gpu_props;
	struct kbase_gpuprops_regdump regdump;

	gpu_props = &kbdev->gpu_props.props;

	/* Dump relevant registers */
	kbase_backend_gpuprops_get_features(kbdev, &regdump);

	/*
	 * Copy the raw value from the register, later this will get turned
	 * into the selected coherency mode.
	 * Additionally, add non-coherent mode, as this is always supported.
	 */
	gpu_props->raw_props.coherency_mode = regdump.coherency_features |
		COHERENCY_FEATURE_BIT(COHERENCY_NONE);

	if (!kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_THREAD_GROUP_SPLIT))
		gpu_props->thread_props.max_thread_group_split = 0;
}

static struct {
	u32 type;
	size_t offset;
	int size;
} gpu_property_mapping[] = {
#define PROP(name, member) \
	{KBASE_GPUPROP_ ## name, offsetof(struct base_gpu_props, member), \
		sizeof(((struct base_gpu_props *)0)->member)}
	PROP(PRODUCT_ID,                  core_props.product_id),
	PROP(VERSION_STATUS,              core_props.version_status),
	PROP(MINOR_REVISION,              core_props.minor_revision),
	PROP(MAJOR_REVISION,              core_props.major_revision),
	PROP(GPU_FREQ_KHZ_MAX,            core_props.gpu_freq_khz_max),
	PROP(LOG2_PROGRAM_COUNTER_SIZE,   core_props.log2_program_counter_size),
	PROP(TEXTURE_FEATURES_0,          core_props.texture_features[0]),
	PROP(TEXTURE_FEATURES_1,          core_props.texture_features[1]),
	PROP(TEXTURE_FEATURES_2,          core_props.texture_features[2]),
	PROP(TEXTURE_FEATURES_3,          core_props.texture_features[3]),
	PROP(GPU_AVAILABLE_MEMORY_SIZE,   core_props.gpu_available_memory_size),
	PROP(NUM_EXEC_ENGINES,            core_props.num_exec_engines),

	PROP(L2_LOG2_LINE_SIZE,           l2_props.log2_line_size),
	PROP(L2_LOG2_CACHE_SIZE,          l2_props.log2_cache_size),
	PROP(L2_NUM_L2_SLICES,            l2_props.num_l2_slices),

	PROP(TILER_BIN_SIZE_BYTES,        tiler_props.bin_size_bytes),
	PROP(TILER_MAX_ACTIVE_LEVELS,     tiler_props.max_active_levels),

	PROP(MAX_THREADS,                 thread_props.max_threads),
	PROP(MAX_WORKGROUP_SIZE,          thread_props.max_workgroup_size),
	PROP(MAX_BARRIER_SIZE,            thread_props.max_barrier_size),
	PROP(MAX_REGISTERS,               thread_props.max_registers),
	PROP(MAX_TASK_QUEUE,              thread_props.max_task_queue),
	PROP(MAX_THREAD_GROUP_SPLIT,      thread_props.max_thread_group_split),
	PROP(IMPL_TECH,                   thread_props.impl_tech),
	PROP(TLS_ALLOC,                   thread_props.tls_alloc),

	PROP(RAW_SHADER_PRESENT,          raw_props.shader_present),
	PROP(RAW_TILER_PRESENT,           raw_props.tiler_present),
	PROP(RAW_L2_PRESENT,              raw_props.l2_present),
	PROP(RAW_STACK_PRESENT,           raw_props.stack_present),
	PROP(RAW_L2_FEATURES,             raw_props.l2_features),
	PROP(RAW_CORE_FEATURES,           raw_props.core_features),
	PROP(RAW_MEM_FEATURES,            raw_props.mem_features),
	PROP(RAW_MMU_FEATURES,            raw_props.mmu_features),
	PROP(RAW_AS_PRESENT,              raw_props.as_present),
	PROP(RAW_JS_PRESENT,              raw_props.js_present),
	PROP(RAW_JS_FEATURES_0,           raw_props.js_features[0]),
	PROP(RAW_JS_FEATURES_1,           raw_props.js_features[1]),
	PROP(RAW_JS_FEATURES_2,           raw_props.js_features[2]),
	PROP(RAW_JS_FEATURES_3,           raw_props.js_features[3]),
	PROP(RAW_JS_FEATURES_4,           raw_props.js_features[4]),
	PROP(RAW_JS_FEATURES_5,           raw_props.js_features[5]),
	PROP(RAW_JS_FEATURES_6,           raw_props.js_features[6]),
	PROP(RAW_JS_FEATURES_7,           raw_props.js_features[7]),
	PROP(RAW_JS_FEATURES_8,           raw_props.js_features[8]),
	PROP(RAW_JS_FEATURES_9,           raw_props.js_features[9]),
	PROP(RAW_JS_FEATURES_10,          raw_props.js_features[10]),
	PROP(RAW_JS_FEATURES_11,          raw_props.js_features[11]),
	PROP(RAW_JS_FEATURES_12,          raw_props.js_features[12]),
	PROP(RAW_JS_FEATURES_13,          raw_props.js_features[13]),
	PROP(RAW_JS_FEATURES_14,          raw_props.js_features[14]),
	PROP(RAW_JS_FEATURES_15,          raw_props.js_features[15]),
	PROP(RAW_TILER_FEATURES,          raw_props.tiler_features),
	PROP(RAW_TEXTURE_FEATURES_0,      raw_props.texture_features[0]),
	PROP(RAW_TEXTURE_FEATURES_1,      raw_props.texture_features[1]),
	PROP(RAW_TEXTURE_FEATURES_2,      raw_props.texture_features[2]),
	PROP(RAW_TEXTURE_FEATURES_3,      raw_props.texture_features[3]),
	PROP(RAW_GPU_ID,                  raw_props.gpu_id),
	PROP(RAW_THREAD_MAX_THREADS,      raw_props.thread_max_threads),
	PROP(RAW_THREAD_MAX_WORKGROUP_SIZE,
			raw_props.thread_max_workgroup_size),
	PROP(RAW_THREAD_MAX_BARRIER_SIZE, raw_props.thread_max_barrier_size),
	PROP(RAW_THREAD_FEATURES,         raw_props.thread_features),
	PROP(RAW_THREAD_TLS_ALLOC,        raw_props.thread_tls_alloc),
	PROP(RAW_COHERENCY_MODE,          raw_props.coherency_mode),

	PROP(COHERENCY_NUM_GROUPS,        coherency_info.num_groups),
	PROP(COHERENCY_NUM_CORE_GROUPS,   coherency_info.num_core_groups),
	PROP(COHERENCY_COHERENCY,         coherency_info.coherency),
	PROP(COHERENCY_GROUP_0,           coherency_info.group[0].core_mask),
	PROP(COHERENCY_GROUP_1,           coherency_info.group[1].core_mask),
	PROP(COHERENCY_GROUP_2,           coherency_info.group[2].core_mask),
	PROP(COHERENCY_GROUP_3,           coherency_info.group[3].core_mask),
	PROP(COHERENCY_GROUP_4,           coherency_info.group[4].core_mask),
	PROP(COHERENCY_GROUP_5,           coherency_info.group[5].core_mask),
	PROP(COHERENCY_GROUP_6,           coherency_info.group[6].core_mask),
	PROP(COHERENCY_GROUP_7,           coherency_info.group[7].core_mask),
	PROP(COHERENCY_GROUP_8,           coherency_info.group[8].core_mask),
	PROP(COHERENCY_GROUP_9,           coherency_info.group[9].core_mask),
	PROP(COHERENCY_GROUP_10,          coherency_info.group[10].core_mask),
	PROP(COHERENCY_GROUP_11,          coherency_info.group[11].core_mask),
	PROP(COHERENCY_GROUP_12,          coherency_info.group[12].core_mask),
	PROP(COHERENCY_GROUP_13,          coherency_info.group[13].core_mask),
	PROP(COHERENCY_GROUP_14,          coherency_info.group[14].core_mask),
	PROP(COHERENCY_GROUP_15,          coherency_info.group[15].core_mask),

#undef PROP
};

int kbase_gpuprops_populate_user_buffer(struct kbase_device *kbdev)
{
	struct kbase_gpu_props *kprops = &kbdev->gpu_props;
	struct base_gpu_props *props = &kprops->props;
	u32 count = ARRAY_SIZE(gpu_property_mapping);
	u32 i;
	u32 size = 0;
	u8 *p;

	for (i = 0; i < count; i++) {
		/* 4 bytes for the ID, and the size of the property */
		size += 4 + gpu_property_mapping[i].size;
	}

	kprops->prop_buffer_size = size;
	kprops->prop_buffer = kmalloc(size, GFP_KERNEL);

	if (!kprops->prop_buffer) {
		kprops->prop_buffer_size = 0;
		return -ENOMEM;
	}

	p = kprops->prop_buffer;

#define WRITE_U8(v) (*p++ = (v) & 0xFF)
#define WRITE_U16(v) do { WRITE_U8(v); WRITE_U8((v) >> 8); } while (0)
#define WRITE_U32(v) do { WRITE_U16(v); WRITE_U16((v) >> 16); } while (0)
#define WRITE_U64(v) do { WRITE_U32(v); WRITE_U32((v) >> 32); } while (0)

	for (i = 0; i < count; i++) {
		u32 type = gpu_property_mapping[i].type;
		u8 type_size;
		void *field = ((u8 *)props) + gpu_property_mapping[i].offset;

		switch (gpu_property_mapping[i].size) {
		case 1:
			type_size = KBASE_GPUPROP_VALUE_SIZE_U8;
			break;
		case 2:
			type_size = KBASE_GPUPROP_VALUE_SIZE_U16;
			break;
		case 4:
			type_size = KBASE_GPUPROP_VALUE_SIZE_U32;
			break;
		case 8:
			type_size = KBASE_GPUPROP_VALUE_SIZE_U64;
			break;
		default:
			dev_err(kbdev->dev,
				"Invalid gpu_property_mapping type=%d size=%d",
				type, gpu_property_mapping[i].size);
			return -EINVAL;
		}

		WRITE_U32((type<<2) | type_size);

		switch (type_size) {
		case KBASE_GPUPROP_VALUE_SIZE_U8:
			WRITE_U8(*((u8 *)field));
			break;
		case KBASE_GPUPROP_VALUE_SIZE_U16:
			WRITE_U16(*((u16 *)field));
			break;
		case KBASE_GPUPROP_VALUE_SIZE_U32:
			WRITE_U32(*((u32 *)field));
			break;
		case KBASE_GPUPROP_VALUE_SIZE_U64:
			WRITE_U64(*((u64 *)field));
			break;
		default: /* Cannot be reached */
			WARN_ON(1);
			return -EINVAL;
		}
	}

	return 0;
}
