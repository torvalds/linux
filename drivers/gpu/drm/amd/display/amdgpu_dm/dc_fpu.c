// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dc_trace.h"

#if defined(CONFIG_X86)
#include <asm/fpu/api.h>
#elif defined(CONFIG_PPC64)
#include <asm/switch_to.h>
#include <asm/cputable.h>
#endif

/**
 * dc_fpu_begin - Enables FPU protection
 * @function_name: A string containing the function name for debug purposes
 *   (usually __func__)
 *
 * @line: A line number where DC_FP_START was invoked for debug purpose
 *   (usually __LINE__)
 *
 * This function is responsible for managing the use of kernel_fpu_begin() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_START().
 */
void dc_fpu_begin(const char *function_name, const int line)
{
	TRACE_DCN_FPU(true, function_name, line);

#if defined(CONFIG_X86)
	kernel_fpu_begin();
#elif defined(CONFIG_PPC64)
	if (cpu_has_feature(CPU_FTR_VSX_COMP)) {
		preempt_disable();
		enable_kernel_vsx();
	} else if (cpu_has_feature(CPU_FTR_ALTIVEC_COMP)) {
		preempt_disable();
		enable_kernel_altivec();
	} else if (!cpu_has_feature(CPU_FTR_FPU_UNAVAILABLE)) {
		preempt_disable();
		enable_kernel_fp();
	}
#endif
}

/**
 * dc_fpu_end - Disable FPU protection
 * @function_name: A string containing the function name for debug purposes
 * @line: A-line number where DC_FP_END was invoked for debug purpose
 *
 * This function is responsible for managing the use of kernel_fpu_end() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_END().
 */
void dc_fpu_end(const char *function_name, const int line)
{
	TRACE_DCN_FPU(false, function_name, line);
#if defined(CONFIG_X86)
	kernel_fpu_end();
#elif defined(CONFIG_PPC64)
	if (cpu_has_feature(CPU_FTR_VSX_COMP)) {
		disable_kernel_vsx();
		preempt_enable();
	} else if (cpu_has_feature(CPU_FTR_ALTIVEC_COMP)) {
		disable_kernel_altivec();
		preempt_enable();
	} else if (!cpu_has_feature(CPU_FTR_FPU_UNAVAILABLE)) {
		disable_kernel_fp();
		preempt_enable();
	}
#endif
}
