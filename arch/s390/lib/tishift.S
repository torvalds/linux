/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/nospec-insn.h>

	.section .noinstr.text, "ax"

	GEN_BR_THUNK %r14

SYM_FUNC_START(__ashlti3)
	lmg	%r0,%r1,0(%r3)
	cije	%r4,0,1f
	lhi	%r3,64
	sr	%r3,%r4
	jnh	0f
	srlg	%r3,%r1,0(%r3)
	sllg	%r0,%r0,0(%r4)
	sllg	%r1,%r1,0(%r4)
	ogr	%r0,%r3
	j	1f
0:	sllg	%r0,%r1,-64(%r4)
	lghi	%r1,0
1:	stmg	%r0,%r1,0(%r2)
	BR_EX	%r14
SYM_FUNC_END(__ashlti3)
EXPORT_SYMBOL(__ashlti3)

SYM_FUNC_START(__ashrti3)
	lmg	%r0,%r1,0(%r3)
	cije	%r4,0,1f
	lhi	%r3,64
	sr	%r3,%r4
	jnh	0f
	sllg	%r3,%r0,0(%r3)
	srlg	%r1,%r1,0(%r4)
	srag	%r0,%r0,0(%r4)
	ogr	%r1,%r3
	j	1f
0:	srag	%r1,%r0,-64(%r4)
	srag	%r0,%r0,63
1:	stmg	%r0,%r1,0(%r2)
	BR_EX	%r14
SYM_FUNC_END(__ashrti3)
EXPORT_SYMBOL(__ashrti3)

SYM_FUNC_START(__lshrti3)
	lmg	%r0,%r1,0(%r3)
	cije	%r4,0,1f
	lhi	%r3,64
	sr	%r3,%r4
	jnh	0f
	sllg	%r3,%r0,0(%r3)
	srlg	%r1,%r1,0(%r4)
	srlg	%r0,%r0,0(%r4)
	ogr	%r1,%r3
	j	1f
0:	srlg	%r1,%r0,-64(%r4)
	lghi	%r0,0
1:	stmg	%r0,%r1,0(%r2)
	BR_EX	%r14
SYM_FUNC_END(__lshrti3)
EXPORT_SYMBOL(__lshrti3)
