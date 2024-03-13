// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/jump_label.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/mce.h>

#ifdef CONFIG_X86_MCE
static DEFINE_STATIC_KEY_FALSE(copy_mc_fragile_key);

void enable_copy_mc_fragile(void)
{
	static_branch_inc(&copy_mc_fragile_key);
}
#define copy_mc_fragile_enabled (static_branch_unlikely(&copy_mc_fragile_key))

/*
 * Similar to copy_user_handle_tail, probe for the write fault point, or
 * source exception point.
 */
__visible notrace unsigned long
copy_mc_fragile_handle_tail(char *to, char *from, unsigned len)
{
	for (; len; --len, to++, from++)
		if (copy_mc_fragile(to, from, 1))
			break;
	return len;
}
#else
/*
 * No point in doing careful copying, or consulting a static key when
 * there is no #MC handler in the CONFIG_X86_MCE=n case.
 */
void enable_copy_mc_fragile(void)
{
}
#define copy_mc_fragile_enabled (0)
#endif

unsigned long copy_mc_enhanced_fast_string(void *dst, const void *src, unsigned len);

/**
 * copy_mc_to_kernel - memory copy that handles source exceptions
 *
 * @dst:	destination address
 * @src:	source address
 * @len:	number of bytes to copy
 *
 * Call into the 'fragile' version on systems that benefit from avoiding
 * corner case poison consumption scenarios, For example, accessing
 * poison across 2 cachelines with a single instruction. Almost all
 * other uses case can use copy_mc_enhanced_fast_string() for a fast
 * recoverable copy, or fallback to plain memcpy.
 *
 * Return 0 for success, or number of bytes not copied if there was an
 * exception.
 */
unsigned long __must_check copy_mc_to_kernel(void *dst, const void *src, unsigned len)
{
	if (copy_mc_fragile_enabled)
		return copy_mc_fragile(dst, src, len);
	if (static_cpu_has(X86_FEATURE_ERMS))
		return copy_mc_enhanced_fast_string(dst, src, len);
	memcpy(dst, src, len);
	return 0;
}
EXPORT_SYMBOL_GPL(copy_mc_to_kernel);

unsigned long __must_check copy_mc_to_user(void __user *dst, const void *src, unsigned len)
{
	unsigned long ret;

	if (copy_mc_fragile_enabled) {
		__uaccess_begin();
		ret = copy_mc_fragile((__force void *)dst, src, len);
		__uaccess_end();
		return ret;
	}

	if (static_cpu_has(X86_FEATURE_ERMS)) {
		__uaccess_begin();
		ret = copy_mc_enhanced_fast_string((__force void *)dst, src, len);
		__uaccess_end();
		return ret;
	}

	return copy_user_generic((__force void *)dst, src, len);
}
