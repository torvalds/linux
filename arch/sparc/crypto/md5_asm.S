/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/visasm.h>

#include "opcodes.h"

ENTRY(md5_sparc64_transform)
	/* %o0 = digest, %o1 = data, %o2 = rounds */
	VISEntryHalf
	ld	[%o0 + 0x00], %f0
	ld	[%o0 + 0x04], %f1
	andcc	%o1, 0x7, %g0
	ld	[%o0 + 0x08], %f2
	bne,pn	%xcc, 10f
	 ld	[%o0 + 0x0c], %f3

1:
	ldd	[%o1 + 0x00], %f8
	ldd	[%o1 + 0x08], %f10
	ldd	[%o1 + 0x10], %f12
	ldd	[%o1 + 0x18], %f14
	ldd	[%o1 + 0x20], %f16
	ldd	[%o1 + 0x28], %f18
	ldd	[%o1 + 0x30], %f20
	ldd	[%o1 + 0x38], %f22

	MD5

	subcc	%o2, 1, %o2
	bne,pt	%xcc, 1b
	 add	%o1, 0x40, %o1

5:
	st	%f0, [%o0 + 0x00]
	st	%f1, [%o0 + 0x04]
	st	%f2, [%o0 + 0x08]
	st	%f3, [%o0 + 0x0c]
	retl
	 VISExitHalf
10:
	alignaddr %o1, %g0, %o1

	ldd	[%o1 + 0x00], %f10
1:
	ldd	[%o1 + 0x08], %f12
	ldd	[%o1 + 0x10], %f14
	ldd	[%o1 + 0x18], %f16
	ldd	[%o1 + 0x20], %f18
	ldd	[%o1 + 0x28], %f20
	ldd	[%o1 + 0x30], %f22
	ldd	[%o1 + 0x38], %f24
	ldd	[%o1 + 0x40], %f26

	faligndata %f10, %f12, %f8
	faligndata %f12, %f14, %f10
	faligndata %f14, %f16, %f12
	faligndata %f16, %f18, %f14
	faligndata %f18, %f20, %f16
	faligndata %f20, %f22, %f18
	faligndata %f22, %f24, %f20
	faligndata %f24, %f26, %f22

	MD5

	subcc	%o2, 1, %o2
	fsrc2	%f26, %f10
	bne,pt	%xcc, 1b
	 add	%o1, 0x40, %o1

	ba,a,pt	%xcc, 5b
ENDPROC(md5_sparc64_transform)
