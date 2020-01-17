/* SPDX-License-Identifier: GPL-2.0 */
	.proc foo
	.prologue
foo:	.save rp, r2
	yesp 0
	.align 64
	.endp foo
