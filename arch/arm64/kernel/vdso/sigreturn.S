/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Sigreturn trampoline for returning from a signal when the SA_RESTORER
 * flag is not set.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unistd.h>

	.text

	nop
/*
 * GDB relies on being able to identify the sigreturn instruction sequence to
 * unwind from signal handlers. We cannot, therefore, use SYM_FUNC_START()
 * here, as it will emit a BTI C instruction and break the unwinder. Thankfully,
 * this function is only ever called from a RET and so omitting the landing pad
 * is perfectly fine.
 */
SYM_CODE_START(__kernel_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	.cfi_def_cfa	x29, 0
	.cfi_offset	x29, 0 * 8
	.cfi_offset	x30, 1 * 8
	mov	x8, #__NR_rt_sigreturn
	svc	#0
	.cfi_endproc
SYM_CODE_END(__kernel_rt_sigreturn)

emit_aarch64_feature_1_and
