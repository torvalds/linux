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





#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_cpuprops.h>
#include <mali_kbase_config_defaults.h>

/* Specifies how many attributes are permitted in the config (excluding terminating attribute).
 * This is used in validation function so we can detect if configuration is properly terminated. This value can be
 * changed if we need to introduce more attributes or many memory regions need to be defined */
#define ATTRIBUTE_COUNT_MAX 32

int kbasep_get_config_attribute_count(const struct kbase_attribute *attributes)
{
	int count = 1;

	if (!attributes)
		return -EINVAL;

	while (attributes->id != KBASE_CONFIG_ATTR_END) {
		attributes++;
		count++;
	}

	return count;
}

const struct kbase_attribute *kbasep_get_next_attribute(const struct kbase_attribute *attributes, int attribute_id)
{
	KBASE_DEBUG_ASSERT(attributes != NULL);

	while (attributes->id != KBASE_CONFIG_ATTR_END) {
		if (attributes->id == attribute_id)
			return attributes;

		attributes++;
	}
	return NULL;
}

KBASE_EXPORT_TEST_API(kbasep_get_next_attribute)

uintptr_t kbasep_get_config_value(struct kbase_device *kbdev, const struct kbase_attribute *attributes, int attribute_id)
{
	const struct kbase_attribute *attr;

	KBASE_DEBUG_ASSERT(attributes != NULL);

	attr = kbasep_get_next_attribute(attributes, attribute_id);
	if (attr != NULL)
		return attr->data;

	/* default values */
	switch (attribute_id) {
		/* Begin scheduling defaults */
	case KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS:
		return DEFAULT_JS_SCHEDULING_TICK_NS;
	case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS:
		return DEFAULT_JS_SOFT_STOP_TICKS;
	case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS_CL:
		return DEFAULT_JS_SOFT_STOP_TICKS_CL;
	case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS:
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
			return DEFAULT_JS_HARD_STOP_TICKS_SS_HW_ISSUE_8408;
		else
			return DEFAULT_JS_HARD_STOP_TICKS_SS;
	case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL:
		return DEFAULT_JS_HARD_STOP_TICKS_CL;
	case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS:
		return DEFAULT_JS_HARD_STOP_TICKS_NSS;
	case KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS:
		return DEFAULT_JS_CTX_TIMESLICE_NS;
	case KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS:
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
			return DEFAULT_JS_RESET_TICKS_SS_HW_ISSUE_8408;
		else
			return DEFAULT_JS_RESET_TICKS_SS;
	case KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL:
		return DEFAULT_JS_RESET_TICKS_CL;
	case KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS:
		return DEFAULT_JS_RESET_TICKS_NSS;
	case KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS:
		return DEFAULT_JS_RESET_TIMEOUT_MS;
		/* End scheduling defaults */
	case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS:
		return 0;
	case KBASE_CONFIG_ATTR_PLATFORM_FUNCS:
		return 0;
	case KBASE_CONFIG_ATTR_CPU_SPEED_FUNC:
		return DEFAULT_CPU_SPEED_FUNC;
	case KBASE_CONFIG_ATTR_GPU_SPEED_FUNC:
		return 0;
	case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_DVFS_FREQ:
		return DEFAULT_PM_DVFS_FREQ;
	case KBASE_CONFIG_ATTR_PM_GPU_POWEROFF_TICK_NS:
		return DEFAULT_PM_GPU_POWEROFF_TICK_NS;
	case KBASE_CONFIG_ATTR_PM_POWEROFF_TICK_SHADER:
		return DEFAULT_PM_POWEROFF_TICK_SHADER;
	case KBASE_CONFIG_ATTR_PM_POWEROFF_TICK_GPU:
		return DEFAULT_PM_POWEROFF_TICK_GPU;
	case KBASE_CONFIG_ATTR_POWER_MODEL_CALLBACKS:
		return 0;

	default:
		dev_err(kbdev->dev, "kbasep_get_config_value. Cannot get value of attribute with id=%d and no default value defined", attribute_id);
		return 0;
	}
}

KBASE_EXPORT_TEST_API(kbasep_get_config_value)

mali_bool kbasep_platform_device_init(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs;

	platform_funcs = (struct kbase_platform_funcs_conf *)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_PLATFORM_FUNCS);
	if (platform_funcs) {
		if (platform_funcs->platform_init_func)
			return platform_funcs->platform_init_func(kbdev);
	}
	return MALI_TRUE;
}

void kbasep_platform_device_term(struct kbase_device *kbdev)
{
	struct kbase_platform_funcs_conf *platform_funcs;

	platform_funcs = (struct kbase_platform_funcs_conf *)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_PLATFORM_FUNCS);
	if (platform_funcs) {
		if (platform_funcs->platform_term_func)
			platform_funcs->platform_term_func(kbdev);
	}
}

static mali_bool kbasep_validate_pm_callback(const struct kbase_pm_callback_conf *callbacks, const struct kbase_device *kbdev)
{
	if (callbacks == NULL) {
		/* Having no callbacks is valid */
		return MALI_TRUE;
	}

	if ((callbacks->power_off_callback != NULL && callbacks->power_on_callback == NULL) || (callbacks->power_off_callback == NULL && callbacks->power_on_callback != NULL)) {
		dev_warn(kbdev->dev, "Invalid power management callbacks: Only one of power_off_callback and power_on_callback was specified");
		return MALI_FALSE;
	}
	return MALI_TRUE;
}

static mali_bool kbasep_validate_cpu_speed_func(kbase_cpuprops_clock_speed_function fcn)
{
	return fcn != NULL;
}

mali_bool kbasep_validate_configuration_attributes(struct kbase_device *kbdev, const struct kbase_attribute *attributes)
{
	int i;

	KBASE_DEBUG_ASSERT(attributes);

	for (i = 0; attributes[i].id != KBASE_CONFIG_ATTR_END; i++) {
		if (i >= ATTRIBUTE_COUNT_MAX) {
			dev_warn(kbdev->dev, "More than ATTRIBUTE_COUNT_MAX=%d configuration attributes defined. Is attribute list properly terminated?", ATTRIBUTE_COUNT_MAX);
			return MALI_FALSE;
		}

		switch (attributes[i].id) {
			/* Only non-zero unsigned 32-bit values accepted */
		case KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS:
#if CSTD_CPU_64BIT
			if (attributes[i].data == 0u || (u64) attributes[i].data > (u64) U32_MAX) {
#else
			if (attributes[i].data == 0u) {
#endif
				dev_warn(kbdev->dev, "Invalid Job Scheduling Configuration attribute for " "KBASE_CONFIG_ATTR_JS_SCHEDULING_TICKS_NS: %d", (int)attributes[i].data);
				return MALI_FALSE;
			}
			break;

			/* All these Job Scheduling attributes are FALLTHROUGH: only unsigned 32-bit values accepted */
		case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS:
		case KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS_CL:
		case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS:
		case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL:
		case KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS:
		case KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS:
		case KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL:
		case KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS:
		case KBASE_CONFIG_ATTR_JS_RESET_TIMEOUT_MS:
		case KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS:
#if CSTD_CPU_64BIT
			if ((u64) attributes[i].data > (u64) U32_MAX) {
				dev_warn(kbdev->dev, "Job Scheduling Configuration attribute exceeds 32-bits: " "id==%d val==%d", attributes[i].id, (int)attributes[i].data);
				return MALI_FALSE;
			}
#endif
			break;

		case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_CALLBACKS:
			if (MALI_FALSE == kbasep_validate_pm_callback((struct kbase_pm_callback_conf *)attributes[i].data, kbdev)) {
				/* Warning message handled by kbasep_validate_pm_callback() */
				return MALI_FALSE;
			}
			break;

		case KBASE_CONFIG_ATTR_CPU_SPEED_FUNC:
			if (MALI_FALSE == kbasep_validate_cpu_speed_func((kbase_cpuprops_clock_speed_function) attributes[i].data)) {
				dev_warn(kbdev->dev, "Invalid function pointer in KBASE_CONFIG_ATTR_CPU_SPEED_FUNC");
				return MALI_FALSE;
			}
			break;

		case KBASE_CONFIG_ATTR_GPU_SPEED_FUNC:
			if (0 == attributes[i].data) {
				dev_warn(kbdev->dev, "Invalid function pointer in KBASE_CONFIG_ATTR_GPU_SPEED_FUNC");
				return MALI_FALSE;
			}
			break;

		case KBASE_CONFIG_ATTR_PLATFORM_FUNCS:
			/* any value is allowed */
			break;

		case KBASE_CONFIG_ATTR_POWER_MANAGEMENT_DVFS_FREQ:
#if CSTD_CPU_64BIT
			if ((u64) attributes[i].data > (u64) U32_MAX) {
				dev_warn(kbdev->dev, "PM DVFS interval exceeds 32-bits: " "id==%d val==%d", attributes[i].id, (int)attributes[i].data);
				return MALI_FALSE;
			}
#endif
			break;

		case KBASE_CONFIG_ATTR_PM_GPU_POWEROFF_TICK_NS:
#if CSTD_CPU_64BIT
			if (attributes[i].data == 0u || (u64) attributes[i].data > (u64) U32_MAX) {
#else
			if (attributes[i].data == 0u) {
#endif
				dev_warn(kbdev->dev, "Invalid Power Manager Configuration attribute for " "KBASE_CONFIG_ATTR_PM_GPU_POWEROFF_TICK_NS: %d", (int)attributes[i].data);
				return MALI_FALSE;
			}
			break;

	case KBASE_CONFIG_ATTR_PM_POWEROFF_TICK_SHADER:
	case KBASE_CONFIG_ATTR_PM_POWEROFF_TICK_GPU:
#if CSTD_CPU_64BIT
			if ((u64) attributes[i].data > (u64) U32_MAX) {
				dev_warn(kbdev->dev, "Power Manager Configuration attribute exceeds 32-bits: " "id==%d val==%d", attributes[i].id, (int)attributes[i].data);
				return MALI_FALSE;
			}
#endif
			break;

		case KBASE_CONFIG_ATTR_POWER_MODEL_CALLBACKS:
			if (0 == attributes[i].data) {
				dev_warn(kbdev->dev, "Power model callbacks is specified but NULL: " "id==%d val==%d",
						attributes[i].id, (int)attributes[i].data);
				return MALI_FALSE;
			}
			break;

		default:
			dev_warn(kbdev->dev, "Invalid attribute found in configuration: %d", attributes[i].id);
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}
