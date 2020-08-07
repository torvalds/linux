/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/linkage.h>

#include <abi/entry.h>

ENTRY(kretprobe_trampoline)
	SAVE_REGS_FTRACE

	mov	a0, sp /* pt_regs */

	jbsr	trampoline_probe_handler

	/* use the result as the return-address */
	mov	lr, a0

	RESTORE_REGS_FTRACE
	rts
ENDPROC(kretprobe_trampoline)
