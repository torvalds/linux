/* U3memcpy.S: UltraSparc-III optimized memcpy.
 *
 * Copyright (C) 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 */

#ifdef __KERNEL__
#include <linux/linkage.h>
#include <asm/visasm.h>
#include <asm/asi.h>
#define GLOBAL_SPARE	%g7
#else
#define ASI_BLK_P 0xf0
#define FPRS_FEF  0x04
#ifdef MEMCPY_DEBUG
#define VISEntryHalf rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs; \
		     clr %g1; clr %g2; clr %g3; subcc %g0, %g0, %g0;
#define VISExitHalf and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#else
#define VISEntryHalf rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs
#define VISExitHalf and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#endif
#define GLOBAL_SPARE	%g5
#endif

#ifndef EX_LD
#define EX_LD(x,y)	x
#endif
#ifndef EX_LD_FP
#define EX_LD_FP(x,y)	x
#endif

#ifndef EX_ST
#define EX_ST(x,y)	x
#endif
#ifndef EX_ST_FP
#define EX_ST_FP(x,y)	x
#endif

#ifndef LOAD
#define LOAD(type,addr,dest)	type [addr], dest
#endif

#ifndef STORE
#define STORE(type,src,addr)	type src, [addr]
#endif

#ifndef STORE_BLK
#define STORE_BLK(src,addr)	stda src, [addr] ASI_BLK_P
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	U3memcpy
#endif

#ifndef PREAMBLE
#define PREAMBLE
#endif

#ifndef XCC
#define XCC xcc
#endif

	.register	%g2,#scratch
	.register	%g3,#scratch

	/* Special/non-trivial issues of this code:
	 *
	 * 1) %o5 is preserved from VISEntryHalf to VISExitHalf
	 * 2) Only low 32 FPU registers are used so that only the
	 *    lower half of the FPU register set is dirtied by this
	 *    code.  This is especially important in the kernel.
	 * 3) This code never prefetches cachelines past the end
	 *    of the source buffer.
	 */

	.text
#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
__restore_fp:
	VISExitHalf
	retl
	 nop
ENTRY(U3_retl_o2_plus_g2_plus_g1_plus_1_fp)
	add	%g1, 1, %g1
	add	%g2, %g1, %g2
	ba,pt	%xcc, __restore_fp
	 add	%o2, %g2, %o0
ENDPROC(U3_retl_o2_plus_g2_plus_g1_plus_1_fp)
ENTRY(U3_retl_o2_plus_g2_fp)
	ba,pt	%xcc, __restore_fp
	 add	%o2, %g2, %o0
ENDPROC(U3_retl_o2_plus_g2_fp)
ENTRY(U3_retl_o2_plus_g2_plus_8_fp)
	add	%g2, 8, %g2
	ba,pt	%xcc, __restore_fp
	 add	%o2, %g2, %o0
ENDPROC(U3_retl_o2_plus_g2_plus_8_fp)
ENTRY(U3_retl_o2)
	retl
	 mov	%o2, %o0
ENDPROC(U3_retl_o2)
ENTRY(U3_retl_o2_plus_1)
	retl
	 add	%o2, 1, %o0
ENDPROC(U3_retl_o2_plus_1)
ENTRY(U3_retl_o2_plus_4)
	retl
	 add	%o2, 4, %o0
ENDPROC(U3_retl_o2_plus_4)
ENTRY(U3_retl_o2_plus_8)
	retl
	 add	%o2, 8, %o0
ENDPROC(U3_retl_o2_plus_8)
ENTRY(U3_retl_o2_plus_g1_plus_1)
	add	%g1, 1, %g1
	retl
	 add	%o2, %g1, %o0
ENDPROC(U3_retl_o2_plus_g1_plus_1)
ENTRY(U3_retl_o2_fp)
	ba,pt	%xcc, __restore_fp
	 mov	%o2, %o0
ENDPROC(U3_retl_o2_fp)
ENTRY(U3_retl_o2_plus_o3_sll_6_plus_0x80_fp)
	sll	%o3, 6, %o3
	add	%o3, 0x80, %o3
	ba,pt	%xcc, __restore_fp
	 add	%o2, %o3, %o0
ENDPROC(U3_retl_o2_plus_o3_sll_6_plus_0x80_fp)
ENTRY(U3_retl_o2_plus_o3_sll_6_plus_0x40_fp)
	sll	%o3, 6, %o3
	add	%o3, 0x40, %o3
	ba,pt	%xcc, __restore_fp
	 add	%o2, %o3, %o0
ENDPROC(U3_retl_o2_plus_o3_sll_6_plus_0x40_fp)
ENTRY(U3_retl_o2_plus_GS_plus_0x10)
	add	GLOBAL_SPARE, 0x10, GLOBAL_SPARE
	retl
	 add	%o2, GLOBAL_SPARE, %o0
ENDPROC(U3_retl_o2_plus_GS_plus_0x10)
ENTRY(U3_retl_o2_plus_GS_plus_0x08)
	add	GLOBAL_SPARE, 0x08, GLOBAL_SPARE
	retl
	 add	%o2, GLOBAL_SPARE, %o0
ENDPROC(U3_retl_o2_plus_GS_plus_0x08)
ENTRY(U3_retl_o2_and_7_plus_GS)
	and	%o2, 7, %o2
	retl
	 add	%o2, GLOBAL_SPARE, %o0
ENDPROC(U3_retl_o2_and_7_plus_GS)
ENTRY(U3_retl_o2_and_7_plus_GS_plus_8)
	add	GLOBAL_SPARE, 8, GLOBAL_SPARE
	and	%o2, 7, %o2
	retl
	 add	%o2, GLOBAL_SPARE, %o0
ENDPROC(U3_retl_o2_and_7_plus_GS_plus_8)
#endif

	.align		64

	/* The cheetah's flexible spine, oversized liver, enlarged heart,
	 * slender muscular body, and claws make it the swiftest hunter
	 * in Africa and the fastest animal on land.  Can reach speeds
	 * of up to 2.4GB per second.
	 */

	.globl	FUNC_NAME
	.type	FUNC_NAME,#function
FUNC_NAME:	/* %o0=dst, %o1=src, %o2=len */
	srlx		%o2, 31, %g2
	cmp		%g2, 0
	tne		%xcc, 5
	PREAMBLE
	mov		%o0, %o4
	cmp		%o2, 0
	be,pn		%XCC, 85f
	 or		%o0, %o1, %o3
	cmp		%o2, 16
	blu,a,pn	%XCC, 80f
	 or		%o3, %o2, %o3

	cmp		%o2, (3 * 64)
	blu,pt		%XCC, 70f
	 andcc		%o3, 0x7, %g0

	/* Clobbers o5/g1/g2/g3/g7/icc/xcc.  We must preserve
	 * o5 from here until we hit VISExitHalf.
	 */
	VISEntryHalf

	/* Is 'dst' already aligned on an 64-byte boundary? */
	andcc		%o0, 0x3f, %g2
	be,pt		%XCC, 2f

	/* Compute abs((dst & 0x3f) - 0x40) into %g2.  This is the number
	 * of bytes to copy to make 'dst' 64-byte aligned.  We pre-
	 * subtract this from 'len'.
	 */
	 sub		%o0, %o1, GLOBAL_SPARE
	sub		%g2, 0x40, %g2
	sub		%g0, %g2, %g2
	sub		%o2, %g2, %o2
	andcc		%g2, 0x7, %g1
	be,pt		%icc, 2f
	 and		%g2, 0x38, %g2

1:	subcc		%g1, 0x1, %g1
	EX_LD_FP(LOAD(ldub, %o1 + 0x00, %o3), U3_retl_o2_plus_g2_plus_g1_plus_1)
	EX_ST_FP(STORE(stb, %o3, %o1 + GLOBAL_SPARE), U3_retl_o2_plus_g2_plus_g1_plus_1)
	bgu,pt		%XCC, 1b
	 add		%o1, 0x1, %o1

	add		%o1, GLOBAL_SPARE, %o0

2:	cmp		%g2, 0x0
	and		%o1, 0x7, %g1
	be,pt		%icc, 3f
	 alignaddr	%o1, %g0, %o1

	EX_LD_FP(LOAD(ldd, %o1, %f4), U3_retl_o2_plus_g2)
1:	EX_LD_FP(LOAD(ldd, %o1 + 0x8, %f6), U3_retl_o2_plus_g2)
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f4, %f6, %f0
	EX_ST_FP(STORE(std, %f0, %o0), U3_retl_o2_plus_g2_plus_8)
	be,pn		%icc, 3f
	 add		%o0, 0x8, %o0

	EX_LD_FP(LOAD(ldd, %o1 + 0x8, %f4), U3_retl_o2_plus_g2)
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f6, %f4, %f2
	EX_ST_FP(STORE(std, %f2, %o0), U3_retl_o2_plus_g2_plus_8)
	bne,pt		%icc, 1b
	 add		%o0, 0x8, %o0

3:	LOAD(prefetch, %o1 + 0x000, #one_read)
	LOAD(prefetch, %o1 + 0x040, #one_read)
	andn		%o2, (0x40 - 1), GLOBAL_SPARE
	LOAD(prefetch, %o1 + 0x080, #one_read)
	LOAD(prefetch, %o1 + 0x0c0, #one_read)
	LOAD(prefetch, %o1 + 0x100, #one_read)
	EX_LD_FP(LOAD(ldd, %o1 + 0x000, %f0), U3_retl_o2)
	LOAD(prefetch, %o1 + 0x140, #one_read)
	EX_LD_FP(LOAD(ldd, %o1 + 0x008, %f2), U3_retl_o2)
	LOAD(prefetch, %o1 + 0x180, #one_read)
	EX_LD_FP(LOAD(ldd, %o1 + 0x010, %f4), U3_retl_o2)
	LOAD(prefetch, %o1 + 0x1c0, #one_read)
	faligndata	%f0, %f2, %f16
	EX_LD_FP(LOAD(ldd, %o1 + 0x018, %f6), U3_retl_o2)
	faligndata	%f2, %f4, %f18
	EX_LD_FP(LOAD(ldd, %o1 + 0x020, %f8), U3_retl_o2)
	faligndata	%f4, %f6, %f20
	EX_LD_FP(LOAD(ldd, %o1 + 0x028, %f10), U3_retl_o2)
	faligndata	%f6, %f8, %f22

	EX_LD_FP(LOAD(ldd, %o1 + 0x030, %f12), U3_retl_o2)
	faligndata	%f8, %f10, %f24
	EX_LD_FP(LOAD(ldd, %o1 + 0x038, %f14), U3_retl_o2)
	faligndata	%f10, %f12, %f26
	EX_LD_FP(LOAD(ldd, %o1 + 0x040, %f0), U3_retl_o2)

	subcc		GLOBAL_SPARE, 0x80, GLOBAL_SPARE
	add		%o1, 0x40, %o1
	bgu,pt		%XCC, 1f
	 srl		GLOBAL_SPARE, 6, %o3
	ba,pt		%xcc, 2f
	 nop

	.align		64
1:
	EX_LD_FP(LOAD(ldd, %o1 + 0x008, %f2), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	faligndata	%f12, %f14, %f28
	EX_LD_FP(LOAD(ldd, %o1 + 0x010, %f4), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	faligndata	%f14, %f0, %f30
	EX_ST_FP(STORE_BLK(%f16, %o0), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	EX_LD_FP(LOAD(ldd, %o1 + 0x018, %f6), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f0, %f2, %f16
	add		%o0, 0x40, %o0

	EX_LD_FP(LOAD(ldd, %o1 + 0x020, %f8), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f2, %f4, %f18
	EX_LD_FP(LOAD(ldd, %o1 + 0x028, %f10), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f4, %f6, %f20
	EX_LD_FP(LOAD(ldd, %o1 + 0x030, %f12), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	subcc		%o3, 0x01, %o3
	faligndata	%f6, %f8, %f22
	EX_LD_FP(LOAD(ldd, %o1 + 0x038, %f14), U3_retl_o2_plus_o3_sll_6_plus_0x80)

	faligndata	%f8, %f10, %f24
	EX_LD_FP(LOAD(ldd, %o1 + 0x040, %f0), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	LOAD(prefetch, %o1 + 0x1c0, #one_read)
	faligndata	%f10, %f12, %f26
	bg,pt		%XCC, 1b
	 add		%o1, 0x40, %o1

	/* Finally we copy the last full 64-byte block. */
2:
	EX_LD_FP(LOAD(ldd, %o1 + 0x008, %f2), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	faligndata	%f12, %f14, %f28
	EX_LD_FP(LOAD(ldd, %o1 + 0x010, %f4), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	faligndata	%f14, %f0, %f30
	EX_ST_FP(STORE_BLK(%f16, %o0), U3_retl_o2_plus_o3_sll_6_plus_0x80)
	EX_LD_FP(LOAD(ldd, %o1 + 0x018, %f6), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f0, %f2, %f16
	EX_LD_FP(LOAD(ldd, %o1 + 0x020, %f8), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f2, %f4, %f18
	EX_LD_FP(LOAD(ldd, %o1 + 0x028, %f10), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f4, %f6, %f20
	EX_LD_FP(LOAD(ldd, %o1 + 0x030, %f12), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f6, %f8, %f22
	EX_LD_FP(LOAD(ldd, %o1 + 0x038, %f14), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	faligndata	%f8, %f10, %f24
	cmp		%g1, 0
	be,pt		%XCC, 1f
	 add		%o0, 0x40, %o0
	EX_LD_FP(LOAD(ldd, %o1 + 0x040, %f0), U3_retl_o2_plus_o3_sll_6_plus_0x40)
1:	faligndata	%f10, %f12, %f26
	faligndata	%f12, %f14, %f28
	faligndata	%f14, %f0, %f30
	EX_ST_FP(STORE_BLK(%f16, %o0), U3_retl_o2_plus_o3_sll_6_plus_0x40)
	add		%o0, 0x40, %o0
	add		%o1, 0x40, %o1
	membar		#Sync

	/* Now we copy the (len modulo 64) bytes at the end.
	 * Note how we borrow the %f0 loaded above.
	 *
	 * Also notice how this code is careful not to perform a
	 * load past the end of the src buffer.
	 */
	and		%o2, 0x3f, %o2
	andcc		%o2, 0x38, %g2
	be,pn		%XCC, 2f
	 subcc		%g2, 0x8, %g2
	be,pn		%XCC, 2f
	 cmp		%g1, 0

	sub		%o2, %g2, %o2
	be,a,pt		%XCC, 1f
	 EX_LD_FP(LOAD(ldd, %o1 + 0x00, %f0), U3_retl_o2_plus_g2)

1:	EX_LD_FP(LOAD(ldd, %o1 + 0x08, %f2), U3_retl_o2_plus_g2)
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f0, %f2, %f8
	EX_ST_FP(STORE(std, %f8, %o0), U3_retl_o2_plus_g2_plus_8)
	be,pn		%XCC, 2f
	 add		%o0, 0x8, %o0
	EX_LD_FP(LOAD(ldd, %o1 + 0x08, %f0), U3_retl_o2_plus_g2)
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f2, %f0, %f8
	EX_ST_FP(STORE(std, %f8, %o0), U3_retl_o2_plus_g2_plus_8)
	bne,pn		%XCC, 1b
	 add		%o0, 0x8, %o0

	/* If anything is left, we copy it one byte at a time.
	 * Note that %g1 is (src & 0x3) saved above before the
	 * alignaddr was performed.
	 */
2:
	cmp		%o2, 0
	add		%o1, %g1, %o1
	VISExitHalf
	be,pn		%XCC, 85f
	 sub		%o0, %o1, %o3

	andcc		%g1, 0x7, %g0
	bne,pn		%icc, 90f
	 andcc		%o2, 0x8, %g0
	be,pt		%icc, 1f
	 nop
	EX_LD(LOAD(ldx, %o1, %o5), U3_retl_o2)
	EX_ST(STORE(stx, %o5, %o1 + %o3), U3_retl_o2)
	add		%o1, 0x8, %o1
	sub		%o2, 8, %o2

1:	andcc		%o2, 0x4, %g0
	be,pt		%icc, 1f
	 nop
	EX_LD(LOAD(lduw, %o1, %o5), U3_retl_o2)
	EX_ST(STORE(stw, %o5, %o1 + %o3), U3_retl_o2)
	add		%o1, 0x4, %o1
	sub		%o2, 4, %o2

1:	andcc		%o2, 0x2, %g0
	be,pt		%icc, 1f
	 nop
	EX_LD(LOAD(lduh, %o1, %o5), U3_retl_o2)
	EX_ST(STORE(sth, %o5, %o1 + %o3), U3_retl_o2)
	add		%o1, 0x2, %o1
	sub		%o2, 2, %o2

1:	andcc		%o2, 0x1, %g0
	be,pt		%icc, 85f
	 nop
	EX_LD(LOAD(ldub, %o1, %o5), U3_retl_o2)
	ba,pt		%xcc, 85f
	 EX_ST(STORE(stb, %o5, %o1 + %o3), U3_retl_o2)

	.align		64
70: /* 16 < len <= 64 */
	bne,pn		%XCC, 75f
	 sub		%o0, %o1, %o3

72:
	andn		%o2, 0xf, GLOBAL_SPARE
	and		%o2, 0xf, %o2
1:	subcc		GLOBAL_SPARE, 0x10, GLOBAL_SPARE
	EX_LD(LOAD(ldx, %o1 + 0x00, %o5), U3_retl_o2_plus_GS_plus_0x10)
	EX_LD(LOAD(ldx, %o1 + 0x08, %g1), U3_retl_o2_plus_GS_plus_0x10)
	EX_ST(STORE(stx, %o5, %o1 + %o3), U3_retl_o2_plus_GS_plus_0x10)
	add		%o1, 0x8, %o1
	EX_ST(STORE(stx, %g1, %o1 + %o3), U3_retl_o2_plus_GS_plus_0x08)
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
73:	andcc		%o2, 0x8, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x8, %o2
	EX_LD(LOAD(ldx, %o1, %o5), U3_retl_o2_plus_8)
	EX_ST(STORE(stx, %o5, %o1 + %o3), U3_retl_o2_plus_8)
	add		%o1, 0x8, %o1
1:	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EX_LD(LOAD(lduw, %o1, %o5), U3_retl_o2_plus_4)
	EX_ST(STORE(stw, %o5, %o1 + %o3), U3_retl_o2_plus_4)
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, 85f
	 nop
	ba,pt		%xcc, 90f
	 nop

75:
	andcc		%o0, 0x7, %g1
	sub		%g1, 0x8, %g1
	be,pn		%icc, 2f
	 sub		%g0, %g1, %g1
	sub		%o2, %g1, %o2

1:	subcc		%g1, 1, %g1
	EX_LD(LOAD(ldub, %o1, %o5), U3_retl_o2_plus_g1_plus_1)
	EX_ST(STORE(stb, %o5, %o1 + %o3), U3_retl_o2_plus_g1_plus_1)
	bgu,pt		%icc, 1b
	 add		%o1, 1, %o1

2:	add		%o1, %o3, %o0
	andcc		%o1, 0x7, %g1
	bne,pt		%icc, 8f
	 sll		%g1, 3, %g1

	cmp		%o2, 16
	bgeu,pt		%icc, 72b
	 nop
	ba,a,pt		%xcc, 73b

8:	mov		64, %o3
	andn		%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1, %g2), U3_retl_o2)
	sub		%o3, %g1, %o3
	andn		%o2, 0x7, GLOBAL_SPARE
	sllx		%g2, %g1, %g2
1:	EX_LD(LOAD(ldx, %o1 + 0x8, %g3), U3_retl_o2_and_7_plus_GS)
	subcc		GLOBAL_SPARE, 0x8, GLOBAL_SPARE
	add		%o1, 0x8, %o1
	srlx		%g3, %o3, %o5
	or		%o5, %g2, %o5
	EX_ST(STORE(stx, %o5, %o0), U3_retl_o2_and_7_plus_GS_plus_8)
	add		%o0, 0x8, %o0
	bgu,pt		%icc, 1b
	 sllx		%g3, %g1, %g2

	srl		%g1, 3, %g1
	andcc		%o2, 0x7, %o2
	be,pn		%icc, 85f
	 add		%o1, %g1, %o1
	ba,pt		%xcc, 90f
	 sub		%o0, %o1, %o3

	.align		64
80: /* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

1:
	subcc		%o2, 4, %o2
	EX_LD(LOAD(lduw, %o1, %g1), U3_retl_o2_plus_4)
	EX_ST(STORE(stw, %g1, %o1 + %o3), U3_retl_o2_plus_4)
	bgu,pt		%XCC, 1b
	 add		%o1, 4, %o1

85:	retl
	 mov		EX_RETVAL(%o4), %o0

	.align		32
90:
	subcc		%o2, 1, %o2
	EX_LD(LOAD(ldub, %o1, %g1), U3_retl_o2_plus_1)
	EX_ST(STORE(stb, %g1, %o1 + %o3), U3_retl_o2_plus_1)
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 mov		EX_RETVAL(%o4), %o0

	.size		FUNC_NAME, .-FUNC_NAME
