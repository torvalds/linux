/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>
#include <asm/unistd.h>
#include <abi/vdso.h>

	.text
ENTRY(__vdso_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	SET_SYSCALL_ID
	trap	0
	.cfi_endproc
ENDPROC(__vdso_rt_sigreturn)
