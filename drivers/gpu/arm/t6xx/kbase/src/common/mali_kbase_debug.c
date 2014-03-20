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





#include <kbase/src/common/mali_kbase.h>

kbasep_debug_assert_cb kbasep_debug_assert_registered_cb = {
	NULL,
	NULL
};

void kbase_debug_assert_register_hook(kbase_debug_assert_hook *func, void *param)
{
	kbasep_debug_assert_registered_cb.func = func;
	kbasep_debug_assert_registered_cb.param = param;
}

void kbasep_debug_assert_call_hook(void)
{
	if (kbasep_debug_assert_registered_cb.func != NULL)
		kbasep_debug_assert_registered_cb.func(kbasep_debug_assert_registered_cb.param);
}
KBASE_EXPORT_SYMBOL(kbasep_debug_assert_call_hook);

/**
 * @brief Contains the module names (modules in the same order as for the kbase_module enumeration)
 * @sa kbasep_module_to_str
 */
static const char *CONST kbasep_str_modules[] = {
	"UNKNOWN",     /**< Unknown module */
	"BASE_MMU",    /**< Base MMU */
	"BASE_JD",     /**< Base Job Dispatch */
	"BASE_JM",     /**< Base Job Manager */
	"BASE_CORE",   /**< Base Core */
	"BASE_MEM",    /**< Base Memory */
	"BASE_EVENT",  /**< Base Event */
	"BASE_CTX",    /**< Base Context */
	"BASE_PM"      /**< Base Power Management */
};

#define MODULE_STRING_ARRAY_SIZE (sizeof(kbasep_str_modules)/sizeof(kbasep_str_modules[0]))

const char *kbasep_debug_module_to_str(const kbase_module module)
{
	if (MODULE_STRING_ARRAY_SIZE <= module)
		return "";

	return kbasep_str_modules[module];
}
KBASE_EXPORT_SYMBOL(kbasep_debug_module_to_str);
