/* NG2memcpy.S: Niagara-2 optimized memcpy.
 *
 * Copyright (C) 2007 David S. Miller (davem@davemloft.net)
 */

#ifdef __KERNEL__
#include <linux/linkage.h>
#include <asm/visasm.h>
#include <asm/asi.h>
#define GLOBAL_SPARE	%g7
#else
#define ASI_PNF 0x82
#define ASI_BLK_P 0xf0
#define ASI_BLK_INIT_QUAD_LDD_P 0xe2
#define FPRS_FEF  0x04
#ifdef MEMCPY_DEBUG
#define VISEntryHalf rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs; \
		     clr %g1; clr %g2; clr %g3; clr %g5; subcc %g0, %g0, %g0;
#define VISExitHalf and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#else
#define VISEntryHalf rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs
#define VISExitHalf and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#endif
#define GLOBAL_SPARE	%g5
#endif

#ifndef STORE_ASI
#ifndef SIMULATE_NIAGARA_ON_NON_NIAGARA
#define STORE_ASI	ASI_BLK_INIT_QUAD_LDD_P
#else
#define STORE_ASI	0x80		/* ASI_P */
#endif
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

#ifndef LOAD_BLK
#define LOAD_BLK(addr,dest)	ldda [addr] ASI_BLK_P, dest
#endif

#ifndef STORE
#ifndef MEMCPY_DEBUG
#define STORE(type,src,addr)	type src, [addr]
#else
#define STORE(type,src,addr)	type##a src, [addr] 0x80
#endif
#endif

#ifndef STORE_BLK
#define STORE_BLK(src,addr)	stda src, [addr] ASI_BLK_P
#endif

#ifndef STORE_INIT
#define STORE_INIT(src,addr)	stxa src, [addr] STORE_ASI
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	NG2memcpy
#endif

#ifndef PREAMBLE
#define PREAMBLE
#endif

#ifndef XCC
#define XCC xcc
#endif

#define FREG_FROB(x0, x1, x2, x3, x4, x5, x6, x7, x8) \
	faligndata	%x0, %x1, %f0; \
	faligndata	%x1, %x2, %f2; \
	faligndata	%x2, %x3, %f4; \
	faligndata	%x3, %x4, %f6; \
	faligndata	%x4, %x5, %f8; \
	faligndata	%x5, %x6, %f10; \
	faligndata	%x6, %x7, %f12; \
	faligndata	%x7, %x8, %f14;

#define FREG_MOVE_1(x0) \
	fsrc2		%x0, %f0;
#define FREG_MOVE_2(x0, x1) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2;
#define FREG_MOVE_3(x0, x1, x2) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4;
#define FREG_MOVE_4(x0, x1, x2, x3) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4; \
	fsrc2		%x3, %f6;
#define FREG_MOVE_5(x0, x1, x2, x3, x4) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4; \
	fsrc2		%x3, %f6; \
	fsrc2		%x4, %f8;
#define FREG_MOVE_6(x0, x1, x2, x3, x4, x5) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4; \
	fsrc2		%x3, %f6; \
	fsrc2		%x4, %f8; \
	fsrc2		%x5, %f10;
#define FREG_MOVE_7(x0, x1, x2, x3, x4, x5, x6) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4; \
	fsrc2		%x3, %f6; \
	fsrc2		%x4, %f8; \
	fsrc2		%x5, %f10; \
	fsrc2		%x6, %f12;
#define FREG_MOVE_8(x0, x1, x2, x3, x4, x5, x6, x7) \
	fsrc2		%x0, %f0; \
	fsrc2		%x1, %f2; \
	fsrc2		%x2, %f4; \
	fsrc2		%x3, %f6; \
	fsrc2		%x4, %f8; \
	fsrc2		%x5, %f10; \
	fsrc2		%x6, %f12; \
	fsrc2		%x7, %f14;
#define FREG_LOAD_1(base, x0) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1)
#define FREG_LOAD_2(base, x0, x1) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1);
#define FREG_LOAD_3(base, x0, x1, x2) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x10, %x2), NG2_retl_o2_plus_g1);
#define FREG_LOAD_4(base, x0, x1, x2, x3) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x10, %x2), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x18, %x3), NG2_retl_o2_plus_g1);
#define FREG_LOAD_5(base, x0, x1, x2, x3, x4) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x10, %x2), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x18, %x3), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x20, %x4), NG2_retl_o2_plus_g1);
#define FREG_LOAD_6(base, x0, x1, x2, x3, x4, x5) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x10, %x2), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x18, %x3), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x20, %x4), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x28, %x5), NG2_retl_o2_plus_g1);
#define FREG_LOAD_7(base, x0, x1, x2, x3, x4, x5, x6) \
	EX_LD_FP(LOAD(ldd, base + 0x00, %x0), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x08, %x1), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x10, %x2), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x18, %x3), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x20, %x4), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x28, %x5), NG2_retl_o2_plus_g1); \
	EX_LD_FP(LOAD(ldd, base + 0x30, %x6), NG2_retl_o2_plus_g1);

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
__restore_fp:
	VISExitHalf
__restore_asi:
	retl
	 wr	%g0, ASI_AIUS, %asi
ENTRY(NG2_retl_o2)
	ba,pt	%xcc, __restore_asi
	 mov	%o2, %o0
ENDPROC(NG2_retl_o2)
ENTRY(NG2_retl_o2_plus_1)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 1, %o0
ENDPROC(NG2_retl_o2_plus_1)
ENTRY(NG2_retl_o2_plus_4)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 4, %o0
ENDPROC(NG2_retl_o2_plus_4)
ENTRY(NG2_retl_o2_plus_8)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 8, %o0
ENDPROC(NG2_retl_o2_plus_8)
ENTRY(NG2_retl_o2_plus_o4_plus_1)
	add	%o4, 1, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(NG2_retl_o2_plus_o4_plus_1)
ENTRY(NG2_retl_o2_plus_o4_plus_8)
	add	%o4, 8, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(NG2_retl_o2_plus_o4_plus_8)
ENTRY(NG2_retl_o2_plus_o4_plus_16)
	add	%o4, 16, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(NG2_retl_o2_plus_o4_plus_16)
ENTRY(NG2_retl_o2_plus_g1_fp)
	ba,pt	%xcc, __restore_fp
	 add	%o2, %g1, %o0
ENDPROC(NG2_retl_o2_plus_g1_fp)
ENTRY(NG2_retl_o2_plus_g1_plus_64_fp)
	add	%g1, 64, %g1
	ba,pt	%xcc, __restore_fp
	 add	%o2, %g1, %o0
ENDPROC(NG2_retl_o2_plus_g1_plus_64_fp)
ENTRY(NG2_retl_o2_plus_g1_plus_1)
	add	%g1, 1, %g1
	ba,pt	%xcc, __restore_asi
	 add	%o2, %g1, %o0
ENDPROC(NG2_retl_o2_plus_g1_plus_1)
ENTRY(NG2_retl_o2_and_7_plus_o4)
	and	%o2, 7, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(NG2_retl_o2_and_7_plus_o4)
ENTRY(NG2_retl_o2_and_7_plus_o4_plus_8)
	and	%o2, 7, %o2
	add	%o4, 8, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(NG2_retl_o2_and_7_plus_o4_plus_8)
#endif

	.align		64

	.globl	FUNC_NAME
	.type	FUNC_NAME,#function
FUNC_NAME:	/* %o0=dst, %o1=src, %o2=len */
	srlx		%o2, 31, %g2
	cmp		%g2, 0
	tne		%xcc, 5
	PREAMBLE
	mov		%o0, %o3
	cmp		%o2, 0
	be,pn		%XCC, 85f
	 or		%o0, %o1, GLOBAL_SPARE
	cmp		%o2, 16
	blu,a,pn	%XCC, 80f
	 or		GLOBAL_SPARE, %o2, GLOBAL_SPARE

	/* 2 blocks (128 bytes) is the minimum we can do the block
	 * copy with.  We need to ensure that we'll iterate at least
	 * once in the block copy loop.  At worst we'll need to align
	 * the destination to a 64-byte boundary which can chew up
	 * to (64 - 1) bytes from the length before we perform the
	 * block copy loop.
	 *
	 * However, the cut-off point, performance wise, is around
	 * 4 64-byte blocks.
	 */
	cmp		%o2, (4 * 64)
	blu,pt		%XCC, 75f
	 andcc		GLOBAL_SPARE, 0x7, %g0

	/* %o0:	dst
	 * %o1:	src
	 * %o2:	len  (known to be >= 128)
	 *
	 * The block copy loops can use %o4, %g2, %g3 as
	 * temporaries while copying the data.  %o5 must
	 * be preserved between VISEntryHalf and VISExitHalf
	 */

	LOAD(prefetch, %o1 + 0x000, #one_read)
	LOAD(prefetch, %o1 + 0x040, #one_read)
	LOAD(prefetch, %o1 + 0x080, #one_read)

	/* Align destination on 64-byte boundary.  */
	andcc		%o0, (64 - 1), %o4
	be,pt		%XCC, 2f
	 sub		%o4, 64, %o4
	sub		%g0, %o4, %o4	! bytes to align dst
	sub		%o2, %o4, %o2
1:	subcc		%o4, 1, %o4
	EX_LD(LOAD(ldub, %o1, %g1), NG2_retl_o2_plus_o4_plus_1)
	EX_ST(STORE(stb, %g1, %o0), NG2_retl_o2_plus_o4_plus_1)
	add		%o1, 1, %o1
	bne,pt		%XCC, 1b
	add		%o0, 1, %o0

2:
	/* Clobbers o5/g1/g2/g3/g7/icc/xcc.  We must preserve
	 * o5 from here until we hit VISExitHalf.
	 */
	VISEntryHalf

	membar		#Sync
	alignaddr	%o1, %g0, %g0

	add		%o1, (64 - 1), %o4
	andn		%o4, (64 - 1), %o4
	andn		%o2, (64 - 1), %g1
	sub		%o2, %g1, %o2

	and		%o1, (64 - 1), %g2
	add		%o1, %g1, %o1
	sub		%o0, %o4, %g3
	brz,pt		%g2, 190f
	 cmp		%g2, 32
	blu,a		5f
	 cmp		%g2, 16
	cmp		%g2, 48
	blu,a		4f
	 cmp		%g2, 40
	cmp		%g2, 56
	blu		170f
	 nop
	ba,a,pt		%xcc, 180f

4:	/* 32 <= low bits < 48 */
	blu		150f
	 nop
	ba,a,pt		%xcc, 160f
5:	/* 0 < low bits < 32 */
	blu,a		6f
	 cmp		%g2, 8
	cmp		%g2, 24
	blu		130f
	 nop
	ba,a,pt		%xcc, 140f
6:	/* 0 < low bits < 16 */
	bgeu		120f
	 nop
	/* fall through for 0 < low bits < 8 */
110:	sub		%o4, 64, %g2
	EX_LD_FP(LOAD_BLK(%g2, %f0), NG2_retl_o2_plus_g1)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f6, f8, f10, f12, f14, f16)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_8(f16, f18, f20, f22, f24, f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

120:	sub		%o4, 56, %g2
	FREG_LOAD_7(%g2, f0, f2, f4, f6, f8, f10, f12)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f6, f8, f10, f12, f16, f18)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_7(f18, f20, f22, f24, f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

130:	sub		%o4, 48, %g2
	FREG_LOAD_6(%g2, f0, f2, f4, f6, f8, f10)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f6, f8, f10, f16, f18, f20)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_6(f20, f22, f24, f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

140:	sub		%o4, 40, %g2
	FREG_LOAD_5(%g2, f0, f2, f4, f6, f8)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f6, f8, f16, f18, f20, f22)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_5(f22, f24, f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

150:	sub		%o4, 32, %g2
	FREG_LOAD_4(%g2, f0, f2, f4, f6)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f6, f16, f18, f20, f22, f24)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_4(f24, f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

160:	sub		%o4, 24, %g2
	FREG_LOAD_3(%g2, f0, f2, f4)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f4, f16, f18, f20, f22, f24, f26)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_3(f26, f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

170:	sub		%o4, 16, %g2
	FREG_LOAD_2(%g2, f0, f2)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f2, f16, f18, f20, f22, f24, f26, f28)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_2(f28, f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

180:	sub		%o4, 8, %g2
	FREG_LOAD_1(%g2, f0)
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	EX_LD_FP(LOAD_BLK(%o4, %f16), NG2_retl_o2_plus_g1)
	FREG_FROB(f0, f16, f18, f20, f22, f24, f26, f28, f30)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1)
	FREG_MOVE_1(f30)
	subcc		%g1, 64, %g1
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)
	ba,pt		%xcc, 195f
	 nop

190:
1:	EX_ST_FP(STORE_INIT(%g0, %o4 + %g3), NG2_retl_o2_plus_g1)
	subcc		%g1, 64, %g1
	EX_LD_FP(LOAD_BLK(%o4, %f0), NG2_retl_o2_plus_g1_plus_64)
	EX_ST_FP(STORE_BLK(%f0, %o4 + %g3), NG2_retl_o2_plus_g1_plus_64)
	add		%o4, 64, %o4
	bne,pt		%xcc, 1b
	 LOAD(prefetch, %o4 + 64, #one_read)

195:
	add		%o4, %g3, %o0
	membar		#Sync

	VISExitHalf

	/* %o2 contains any final bytes still needed to be copied
	 * over. If anything is left, we copy it one byte at a time.
	 */
	brz,pt		%o2, 85f
	 sub		%o0, %o1, GLOBAL_SPARE
	ba,a,pt		%XCC, 90f

	.align		64
75: /* 16 < len <= 64 */
	bne,pn		%XCC, 75f
	 sub		%o0, %o1, GLOBAL_SPARE

72:
	andn		%o2, 0xf, %o4
	and		%o2, 0xf, %o2
1:	subcc		%o4, 0x10, %o4
	EX_LD(LOAD(ldx, %o1, %o5), NG2_retl_o2_plus_o4_plus_16)
	add		%o1, 0x08, %o1
	EX_LD(LOAD(ldx, %o1, %g1), NG2_retl_o2_plus_o4_plus_16)
	sub		%o1, 0x08, %o1
	EX_ST(STORE(stx, %o5, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_o4_plus_16)
	add		%o1, 0x8, %o1
	EX_ST(STORE(stx, %g1, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_o4_plus_8)
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
73:	andcc		%o2, 0x8, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x8, %o2
	EX_LD(LOAD(ldx, %o1, %o5), NG2_retl_o2_plus_8)
	EX_ST(STORE(stx, %o5, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_8)
	add		%o1, 0x8, %o1
1:	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	sub		%o2, 0x4, %o2
	EX_LD(LOAD(lduw, %o1, %o5), NG2_retl_o2_plus_4)
	EX_ST(STORE(stw, %o5, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_4)
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
	EX_LD(LOAD(ldub, %o1, %o5), NG2_retl_o2_plus_g1_plus_1)
	EX_ST(STORE(stb, %o5, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_g1_plus_1)
	bgu,pt		%icc, 1b
	 add		%o1, 1, %o1

2:	add		%o1, GLOBAL_SPARE, %o0
	andcc		%o1, 0x7, %g1
	bne,pt		%icc, 8f
	 sll		%g1, 3, %g1

	cmp		%o2, 16
	bgeu,pt		%icc, 72b
	 nop
	ba,a,pt		%xcc, 73b

8:	mov		64, GLOBAL_SPARE
	andn		%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1, %g2), NG2_retl_o2)
	sub		GLOBAL_SPARE, %g1, GLOBAL_SPARE
	andn		%o2, 0x7, %o4
	sllx		%g2, %g1, %g2
1:	add		%o1, 0x8, %o1
	EX_LD(LOAD(ldx, %o1, %g3), NG2_retl_o2_and_7_plus_o4)
	subcc		%o4, 0x8, %o4
	srlx		%g3, GLOBAL_SPARE, %o5
	or		%o5, %g2, %o5
	EX_ST(STORE(stx, %o5, %o0), NG2_retl_o2_and_7_plus_o4_plus_8)
	add		%o0, 0x8, %o0
	bgu,pt		%icc, 1b
	 sllx		%g3, %g1, %g2

	srl		%g1, 3, %g1
	andcc		%o2, 0x7, %o2
	be,pn		%icc, 85f
	 add		%o1, %g1, %o1
	ba,pt		%xcc, 90f
	 sub		%o0, %o1, GLOBAL_SPARE

	.align		64
80: /* 0 < len <= 16 */
	andcc		GLOBAL_SPARE, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, GLOBAL_SPARE

1:
	subcc		%o2, 4, %o2
	EX_LD(LOAD(lduw, %o1, %g1), NG2_retl_o2_plus_4)
	EX_ST(STORE(stw, %g1, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_4)
	bgu,pt		%XCC, 1b
	 add		%o1, 4, %o1

85:	retl
	 mov		EX_RETVAL(%o3), %o0

	.align		32
90:
	subcc		%o2, 1, %o2
	EX_LD(LOAD(ldub, %o1, %g1), NG2_retl_o2_plus_1)
	EX_ST(STORE(stb, %g1, %o1 + GLOBAL_SPARE), NG2_retl_o2_plus_1)
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 mov		EX_RETVAL(%o3), %o0

	.size		FUNC_NAME, .-FUNC_NAME
