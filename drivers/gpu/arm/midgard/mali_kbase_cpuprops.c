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
 * @file mali_kbase_cpuprops.c
 * Base kernel property query APIs
 */

#include "mali_kbase.h"
#include "mali_kbase_cpuprops.h"
#include "mali_kbase_uku.h"
#include <mali_kbase_config.h>
#include <linux/cache.h>
#include <linux/cpufreq.h>
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#include <asm/cputype.h>
#endif

#define KBASE_DEFAULT_CPU_NUM 0

#define L1_DCACHE_LINE_SIZE_LOG2 L1_CACHE_SHIFT

/**
 * @brief Macros used to extract cpu id info
 * @see Doc's for Main ID register
 */
#define KBASE_CPUPROPS_ID_GET_REV(cpuid)    ((cpuid) & 0x0F)          /* [3:0]   Revision                            */
#define KBASE_CPUPROPS_ID_GET_PART_NR(cpuid)(((cpuid) >>  4) & 0xFFF) /* [15:4]  Part number                         */
#define KBASE_CPUPROPS_ID_GET_ARCH(cpuid)   (((cpuid) >> 16) & 0x0F)  /* [19:16] Architecture                        */
#define KBASE_CPUPROPS_ID_GET_VARIANT(cpuid)(((cpuid) >> 20) & 0x0F)  /* [23:20] Variant                             */
#define KBASE_CPUPROPS_ID_GET_CODE(cpuid)   (((cpuid) >> 24) & 0xFF)  /* [31:23] ASCII code of implementer trademark */

/*Below value sourced from OSK*/
#define L1_DCACHE_SIZE ((u32)0x00008000)


/**
 * @brief Retrieves detailed CPU info from given cpu_val ( ID reg )
 *
 * @param kbase_props CPU props to be filled-in with cpu id info
 *
 */
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64) 
static void kbasep_cpuprops_uk_get_cpu_id_info(struct kbase_uk_cpuprops * const kbase_props)
{
	kbase_props->props.cpu_id.id           = read_cpuid_id();

	kbase_props->props.cpu_id.valid        = 1;
	kbase_props->props.cpu_id.rev          = KBASE_CPUPROPS_ID_GET_REV(kbase_props->props.cpu_id.id);
	kbase_props->props.cpu_id.part         = KBASE_CPUPROPS_ID_GET_PART_NR(kbase_props->props.cpu_id.id);
	kbase_props->props.cpu_id.arch         = KBASE_CPUPROPS_ID_GET_ARCH(kbase_props->props.cpu_id.id);
	kbase_props->props.cpu_id.variant      = KBASE_CPUPROPS_ID_GET_VARIANT(kbase_props->props.cpu_id.id);
	kbase_props->props.cpu_id.implementer  = KBASE_CPUPROPS_ID_GET_CODE(kbase_props->props.cpu_id.id);
}
#else
static void kbasep_cpuprops_uk_get_cpu_id_info(struct kbase_uk_cpuprops * const kbase_props)
{
	kbase_props->props.cpu_id.id           = 0;
	kbase_props->props.cpu_id.valid        = 0;
	kbase_props->props.cpu_id.rev          = 0;
	kbase_props->props.cpu_id.part         = 0;
	kbase_props->props.cpu_id.arch         = 0;
	kbase_props->props.cpu_id.variant      = 0;
	kbase_props->props.cpu_id.implementer  = 'N';
}
#endif

int kbase_cpuprops_get_default_clock_speed(u32 * const clock_speed)
{
	KBASE_DEBUG_ASSERT(NULL != clock_speed);

	*clock_speed = 100;
	return 0;
}

mali_error kbase_cpuprops_uk_get_props(struct kbase_context *kctx, struct kbase_uk_cpuprops * const kbase_props)
{
	unsigned int max_cpu_freq;

	kbase_props->props.cpu_l1_dcache_line_size_log2 = L1_DCACHE_LINE_SIZE_LOG2;
	kbase_props->props.cpu_l1_dcache_size = L1_DCACHE_SIZE;
	kbase_props->props.cpu_flags = BASE_CPU_PROPERTY_FLAG_LITTLE_ENDIAN;

	kbase_props->props.nr_cores = NR_CPUS;
	kbase_props->props.cpu_page_size_log2 = PAGE_SHIFT;
	kbase_props->props.available_memory_size = totalram_pages << PAGE_SHIFT;

	kbasep_cpuprops_uk_get_cpu_id_info(kbase_props);

	/* check if kernel supports dynamic frequency scaling */
	max_cpu_freq = cpufreq_quick_get_max(KBASE_DEFAULT_CPU_NUM);
	if (max_cpu_freq != 0)
	{
		/* convert from kHz to mHz */
		kbase_props->props.max_cpu_clock_speed_mhz = max_cpu_freq / 1000;
	}
	else 
	{
		/* fallback if CONFIG_CPU_FREQ turned off */
		int result;
		kbase_cpuprops_clock_speed_function kbase_cpuprops_uk_get_clock_speed;

		kbase_cpuprops_uk_get_clock_speed = (kbase_cpuprops_clock_speed_function) kbasep_get_config_value(kctx->kbdev, kctx->kbdev->config_attributes, KBASE_CONFIG_ATTR_CPU_SPEED_FUNC);
		result = kbase_cpuprops_uk_get_clock_speed(&kbase_props->props.max_cpu_clock_speed_mhz);
		if (result != 0)
			return MALI_ERROR_FUNCTION_FAILED;
	}

	return MALI_ERROR_NONE;
}
