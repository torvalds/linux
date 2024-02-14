/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm-offsets.h>

	.align 2
.L_vdso_data_ptr:
	.long	_start - . - VDSO_DATA_SIZE

ENTRY(__get_datapage)
	.fnstart
	adr	r0, .L_vdso_data_ptr
	ldr	r1, [r0]
	add	r0, r0, r1
	bx	lr
	.fnend
ENDPROC(__get_datapage)
