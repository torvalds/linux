/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Syscall restart trampoline for 32 and 64 bits processes.
 *
 * Copyright (C) 2018-2022 Helge Deller <deller@gmx.de>
 * Copyright (C) 2022 John David Anglin <dave.anglin@bell.net>
 */

#include <asm/unistd.h>
#include <asm/vdso.h>

#include <linux/linkage.h>

	.text

ENTRY_CFI(__kernel_restart_syscall)
	/*
	 * Setup a trampoline to restart the syscall
	 * with __NR_restart_syscall
	 */

	/* load return pointer */
#if defined(__VDSO64__)
	ldd	0(%sp), %r31
#elif defined(__VDSO32__)
	ldw	0(%sp), %r31
#endif

	be	0x100(%sr2, %r0)
	ldi	__NR_restart_syscall, %r20

ENDPROC_CFI(__kernel_restart_syscall)
