// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 *
 * Copyright (c) 2025, Microsoft Corporation.
 *
 * Author:
 *   Naman Jain <namjain@microsoft.com>
 */
#define COMPILE_OFFSETS

#include <linux/kbuild.h>
#include <asm/mshyperv.h>

static void __used common(void)
{
	if (IS_ENABLED(CONFIG_HYPERV_VTL_MODE)) {
		OFFSET(MSHV_VTL_CPU_CONTEXT_rax, mshv_vtl_cpu_context, rax);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rcx, mshv_vtl_cpu_context, rcx);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rdx, mshv_vtl_cpu_context, rdx);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rbx, mshv_vtl_cpu_context, rbx);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rbp, mshv_vtl_cpu_context, rbp);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rsi, mshv_vtl_cpu_context, rsi);
		OFFSET(MSHV_VTL_CPU_CONTEXT_rdi, mshv_vtl_cpu_context, rdi);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r8,  mshv_vtl_cpu_context, r8);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r9,  mshv_vtl_cpu_context, r9);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r10, mshv_vtl_cpu_context, r10);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r11, mshv_vtl_cpu_context, r11);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r12, mshv_vtl_cpu_context, r12);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r13, mshv_vtl_cpu_context, r13);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r14, mshv_vtl_cpu_context, r14);
		OFFSET(MSHV_VTL_CPU_CONTEXT_r15, mshv_vtl_cpu_context, r15);
		OFFSET(MSHV_VTL_CPU_CONTEXT_cr2, mshv_vtl_cpu_context, cr2);
	}
}
