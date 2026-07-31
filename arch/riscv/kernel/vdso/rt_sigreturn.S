/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Regents of the University of California
 */

#include <linux/linkage.h>
#include <asm/unistd.h>
#include <asm/assembler.h>

/*
 * WARNING: Do NOT add a CFI landing pad at the start of this function.
 * Unwinders such as libgcc identify the sigreturn trampoline by matching the
 * instruction sequence. Adding a landing pad here would break unwinding from
 * signal handlers.
 *
 * This trampoline is used only for signal return and not via an indirect
 * call/jump from userspace, so adding CFI landing pad is unnecessary.
 */
	.text
SYM_FUNC_START(__vdso_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	li a7, __NR_rt_sigreturn
	ecall
	.cfi_endproc
SYM_FUNC_END(__vdso_rt_sigreturn)

emit_riscv_feature_1_and
