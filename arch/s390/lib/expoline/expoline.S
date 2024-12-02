/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/nospec-insn.h>
#include <linux/linkage.h>

.macro GEN_ALL_BR_THUNK_EXTERN
	.irp r1,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	GEN_BR_THUNK_EXTERN %r\r1
	.endr
.endm

GEN_ALL_BR_THUNK_EXTERN
