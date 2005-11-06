/*
 * Qemu interrupt handler code.
 *
 * Copyright (C) 2005 by Ralf Baechle
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.align	5
	NESTED(qemu_handle_int, PT_SIZE, sp)
	SAVE_ALL
	CLI
	move	a0, sp
	PTR_LA	ra, ret_from_irq
	j	do_qemu_int
	END(qemu_handle_int)
