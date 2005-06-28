/* U1memcpy.S: UltraSPARC-I/II/IIi/IIe optimized memcpy.
 *
 * Copyright (C) 1997, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 1996, 1997, 1998, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#ifdef __KERNEL__
#include <asm/visasm.h>
#include <asm/asi.h>
#define GLOBAL_SPARE	g7
#else
#define GLOBAL_SPARE	g5
#define ASI_BLK_P 0xf0
#define FPRS_FEF  0x04
#ifdef MEMCPY_DEBUG
#define VISEntry rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs; \
		 clr %g1; clr %g2; clr %g3; subcc %g0, %g0, %g0;
#define VISExit and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#else
#define VISEntry rd %fprs, %o5; wr %g0, FPRS_FEF, %fprs
#define VISExit and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#endif
#endif

#ifndef EX_LD
#define EX_LD(x)	x
#endif

#ifndef EX_ST
#define EX_ST(x)	x
#endif

#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
#endif

#ifndef LOAD
#define LOAD(type,addr,dest)	type [addr], dest
#endif

#ifndef LOAD_BLK
#define LOAD_BLK(addr,dest)	ldda [addr] ASI_BLK_P, dest
#endif

#ifndef STORE
#define STORE(type,src,addr)	type src, [addr]
#endif

#ifndef STORE_BLK
#define STORE_BLK(src,addr)	stda src, [addr] ASI_BLK_P
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	memcpy
#endif

#ifndef PREAMBLE
#define PREAMBLE
#endif

#ifndef XCC
#define XCC xcc
#endif

#define FREG_FROB(f1, f2, f3, f4, f5, f6, f7, f8, f9)		\
	faligndata		%f1, %f2, %f48;			\
	faligndata		%f2, %f3, %f50;			\
	faligndata		%f3, %f4, %f52;			\
	faligndata		%f4, %f5, %f54;			\
	faligndata		%f5, %f6, %f56;			\
	faligndata		%f6, %f7, %f58;			\
	faligndata		%f7, %f8, %f60;			\
	faligndata		%f8, %f9, %f62;

#define MAIN_LOOP_CHUNK(src, dest, fdest, fsrc, len, jmptgt)	\
	EX_LD(LOAD_BLK(%src, %fdest));				\
	EX_ST(STORE_BLK(%fsrc, %dest));				\
	add			%src, 0x40, %src;		\
	subcc			%len, 0x40, %len;		\
	be,pn			%xcc, jmptgt;			\
	 add			%dest, 0x40, %dest;		\

#define LOOP_CHUNK1(src, dest, len, branch_dest)		\
	MAIN_LOOP_CHUNK(src, dest, f0,  f48, len, branch_dest)
#define LOOP_CHUNK2(src, dest, len, branch_dest)		\
	MAIN_LOOP_CHUNK(src, dest, f16, f48, len, branch_dest)
#define LOOP_CHUNK3(src, dest, len, branch_dest)		\
	MAIN_LOOP_CHUNK(src, dest, f32, f48, len, branch_dest)

#define DO_SYNC			membar	#Sync;
#define STORE_SYNC(dest, fsrc)				\
	EX_ST(STORE_BLK(%fsrc, %dest));			\
	add			%dest, 0x40, %dest;	\
	DO_SYNC

#define STORE_JUMP(dest, fsrc, target)			\
	EX_ST(STORE_BLK(%fsrc, %dest));			\
	add			%dest, 0x40, %dest;	\
	ba,pt			%xcc, target;		\
	 nop;

#define FINISH_VISCHUNK(dest, f0, f1, left)	\
	subcc			%left, 8, %left;\
	bl,pn			%xcc, 95f;	\
	 faligndata		%f0, %f1, %f48;	\
	EX_ST(STORE(std, %f48, %dest));		\
	add			%dest, 8, %dest;

#define UNEVEN_VISCHUNK_LAST(dest, f0, f1, left)	\
	subcc			%left, 8, %left;	\
	bl,pn			%xcc, 95f;		\
	 fsrc1			%f0, %f1;

#define UNEVEN_VISCHUNK(dest, f0, f1, left)		\
	UNEVEN_VISCHUNK_LAST(dest, f0, f1, left)	\
	ba,a,pt			%xcc, 93f;

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
	.align		64

	.globl		FUNC_NAME
	.type		FUNC_NAME,#function
FUNC_NAME:		/* %o0=dst, %o1=src, %o2=len */
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

	cmp		%o2, (5 * 64)
	blu,pt		%XCC, 70f
	 andcc		%o3, 0x7, %g0

	/* Clobbers o5/g1/g2/g3/g7/icc/xcc.  */
	VISEntry

	/* Is 'dst' already aligned on an 64-byte boundary? */
	andcc		%o0, 0x3f, %g2
	be,pt		%XCC, 2f

	/* Compute abs((dst & 0x3f) - 0x40) into %g2.  This is the number
	 * of bytes to copy to make 'dst' 64-byte aligned.  We pre-
	 * subtract this from 'len'.
	 */
	 sub		%o0, %o1, %GLOBAL_SPARE
	sub		%g2, 0x40, %g2
	sub		%g0, %g2, %g2
	sub		%o2, %g2, %o2
	andcc		%g2, 0x7, %g1
	be,pt		%icc, 2f
	 and		%g2, 0x38, %g2

1:	subcc		%g1, 0x1, %g1
	EX_LD(LOAD(ldub, %o1 + 0x00, %o3))
	EX_ST(STORE(stb, %o3, %o1 + %GLOBAL_SPARE))
	bgu,pt		%XCC, 1b
	 add		%o1, 0x1, %o1

	add		%o1, %GLOBAL_SPARE, %o0

2:	cmp		%g2, 0x0
	and		%o1, 0x7, %g1
	be,pt		%icc, 3f
	 alignaddr	%o1, %g0, %o1

	EX_LD(LOAD(ldd, %o1, %f4))
1:	EX_LD(LOAD(ldd, %o1 + 0x8, %f6))
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f4, %f6, %f0
	EX_ST(STORE(std, %f0, %o0))
	be,pn		%icc, 3f
	 add		%o0, 0x8, %o0

	EX_LD(LOAD(ldd, %o1 + 0x8, %f4))
	add		%o1, 0x8, %o1
	subcc		%g2, 0x8, %g2
	faligndata	%f6, %f4, %f0
	EX_ST(STORE(std, %f0, %o0))
	bne,pt		%icc, 1b
	 add		%o0, 0x8, %o0

	/* Destination is 64-byte aligned.  */
3:	
	membar		  #LoadStore | #StoreStore | #StoreLoad

	subcc		%o2, 0x40, %GLOBAL_SPARE
	add		%o1, %g1, %g1
	andncc		%GLOBAL_SPARE, (0x40 - 1), %GLOBAL_SPARE
	srl		%g1, 3, %g2
	sub		%o2, %GLOBAL_SPARE, %g3
	andn		%o1, (0x40 - 1), %o1
	and		%g2, 7, %g2
	andncc		%g3, 0x7, %g3
	fmovd		%f0, %f2
	sub		%g3, 0x8, %g3
	sub		%o2, %GLOBAL_SPARE, %o2

	add		%g1, %GLOBAL_SPARE, %g1
	subcc		%o2, %g3, %o2

	EX_LD(LOAD_BLK(%o1, %f0))
	add		%o1, 0x40, %o1
	add		%g1, %g3, %g1
	EX_LD(LOAD_BLK(%o1, %f16))
	add		%o1, 0x40, %o1
	sub		%GLOBAL_SPARE, 0x80, %GLOBAL_SPARE
	EX_LD(LOAD_BLK(%o1, %f32))
	add		%o1, 0x40, %o1

	/* There are 8 instances of the unrolled loop,
	 * one for each possible alignment of the
	 * source buffer.  Each loop instance is 452
	 * bytes.
	 */
	sll		%g2, 3, %o3
	sub		%o3, %g2, %o3
	sllx		%o3, 4, %o3
	add		%o3, %g2, %o3
	sllx		%o3, 2, %g2
1:	rd		%pc, %o3
	add		%o3, %lo(1f - 1b), %o3
	jmpl		%o3 + %g2, %g0
	 nop

	.align		64
1:	FREG_FROB(f0, f2, f4, f6, f8, f10,f12,f14,f16)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f16,f18,f20,f22,f24,f26,f28,f30,f32)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f32,f34,f36,f38,f40,f42,f44,f46,f0)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f0, %f2, %f48
1:	FREG_FROB(f16,f18,f20,f22,f24,f26,f28,f30,f32)
	STORE_SYNC(o0, f48)
	FREG_FROB(f32,f34,f36,f38,f40,f42,f44,f46,f0)
	STORE_JUMP(o0, f48, 40f)
2:	FREG_FROB(f32,f34,f36,f38,f40,f42,f44,f46,f0)
	STORE_SYNC(o0, f48)
	FREG_FROB(f0, f2, f4, f6, f8, f10,f12,f14,f16)
	STORE_JUMP(o0, f48, 48f)
3:	FREG_FROB(f0, f2, f4, f6, f8, f10,f12,f14,f16)
	STORE_SYNC(o0, f48)
	FREG_FROB(f16,f18,f20,f22,f24,f26,f28,f30,f32)
	STORE_JUMP(o0, f48, 56f)

1:	FREG_FROB(f2, f4, f6, f8, f10,f12,f14,f16,f18)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f18,f20,f22,f24,f26,f28,f30,f32,f34)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f34,f36,f38,f40,f42,f44,f46,f0, f2)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f2, %f4, %f48
1:	FREG_FROB(f18,f20,f22,f24,f26,f28,f30,f32,f34)
	STORE_SYNC(o0, f48)
	FREG_FROB(f34,f36,f38,f40,f42,f44,f46,f0, f2)
	STORE_JUMP(o0, f48, 41f)
2:	FREG_FROB(f34,f36,f38,f40,f42,f44,f46,f0, f2)
	STORE_SYNC(o0, f48)
	FREG_FROB(f2, f4, f6, f8, f10,f12,f14,f16,f18)
	STORE_JUMP(o0, f48, 49f)
3:	FREG_FROB(f2, f4, f6, f8, f10,f12,f14,f16,f18)
	STORE_SYNC(o0, f48)
	FREG_FROB(f18,f20,f22,f24,f26,f28,f30,f32,f34)
	STORE_JUMP(o0, f48, 57f)

1:	FREG_FROB(f4, f6, f8, f10,f12,f14,f16,f18,f20)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f20,f22,f24,f26,f28,f30,f32,f34,f36)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f36,f38,f40,f42,f44,f46,f0, f2, f4)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f4, %f6, %f48
1:	FREG_FROB(f20,f22,f24,f26,f28,f30,f32,f34,f36)
	STORE_SYNC(o0, f48)
	FREG_FROB(f36,f38,f40,f42,f44,f46,f0, f2, f4)
	STORE_JUMP(o0, f48, 42f)
2:	FREG_FROB(f36,f38,f40,f42,f44,f46,f0, f2, f4)
	STORE_SYNC(o0, f48)
	FREG_FROB(f4, f6, f8, f10,f12,f14,f16,f18,f20)
	STORE_JUMP(o0, f48, 50f)
3:	FREG_FROB(f4, f6, f8, f10,f12,f14,f16,f18,f20)
	STORE_SYNC(o0, f48)
	FREG_FROB(f20,f22,f24,f26,f28,f30,f32,f34,f36)
	STORE_JUMP(o0, f48, 58f)

1:	FREG_FROB(f6, f8, f10,f12,f14,f16,f18,f20,f22)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f22,f24,f26,f28,f30,f32,f34,f36,f38)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f38,f40,f42,f44,f46,f0, f2, f4, f6) 
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f6, %f8, %f48
1:	FREG_FROB(f22,f24,f26,f28,f30,f32,f34,f36,f38)
	STORE_SYNC(o0, f48)
	FREG_FROB(f38,f40,f42,f44,f46,f0, f2, f4, f6)
	STORE_JUMP(o0, f48, 43f)
2:	FREG_FROB(f38,f40,f42,f44,f46,f0, f2, f4, f6)
	STORE_SYNC(o0, f48)
	FREG_FROB(f6, f8, f10,f12,f14,f16,f18,f20,f22)
	STORE_JUMP(o0, f48, 51f)
3:	FREG_FROB(f6, f8, f10,f12,f14,f16,f18,f20,f22)
	STORE_SYNC(o0, f48)
	FREG_FROB(f22,f24,f26,f28,f30,f32,f34,f36,f38)
	STORE_JUMP(o0, f48, 59f)

1:	FREG_FROB(f8, f10,f12,f14,f16,f18,f20,f22,f24)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f24,f26,f28,f30,f32,f34,f36,f38,f40)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f40,f42,f44,f46,f0, f2, f4, f6, f8)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f8, %f10, %f48
1:	FREG_FROB(f24,f26,f28,f30,f32,f34,f36,f38,f40)
	STORE_SYNC(o0, f48)
	FREG_FROB(f40,f42,f44,f46,f0, f2, f4, f6, f8)
	STORE_JUMP(o0, f48, 44f)
2:	FREG_FROB(f40,f42,f44,f46,f0, f2, f4, f6, f8)
	STORE_SYNC(o0, f48)
	FREG_FROB(f8, f10,f12,f14,f16,f18,f20,f22,f24)
	STORE_JUMP(o0, f48, 52f)
3:	FREG_FROB(f8, f10,f12,f14,f16,f18,f20,f22,f24)
	STORE_SYNC(o0, f48)
	FREG_FROB(f24,f26,f28,f30,f32,f34,f36,f38,f40)
	STORE_JUMP(o0, f48, 60f)

1:	FREG_FROB(f10,f12,f14,f16,f18,f20,f22,f24,f26)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f26,f28,f30,f32,f34,f36,f38,f40,f42)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f42,f44,f46,f0, f2, f4, f6, f8, f10)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f10, %f12, %f48
1:	FREG_FROB(f26,f28,f30,f32,f34,f36,f38,f40,f42)
	STORE_SYNC(o0, f48)
	FREG_FROB(f42,f44,f46,f0, f2, f4, f6, f8, f10)
	STORE_JUMP(o0, f48, 45f)
2:	FREG_FROB(f42,f44,f46,f0, f2, f4, f6, f8, f10)
	STORE_SYNC(o0, f48)
	FREG_FROB(f10,f12,f14,f16,f18,f20,f22,f24,f26)
	STORE_JUMP(o0, f48, 53f)
3:	FREG_FROB(f10,f12,f14,f16,f18,f20,f22,f24,f26)
	STORE_SYNC(o0, f48)
	FREG_FROB(f26,f28,f30,f32,f34,f36,f38,f40,f42)
	STORE_JUMP(o0, f48, 61f)

1:	FREG_FROB(f12,f14,f16,f18,f20,f22,f24,f26,f28)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f28,f30,f32,f34,f36,f38,f40,f42,f44)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f44,f46,f0, f2, f4, f6, f8, f10,f12)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f12, %f14, %f48
1:	FREG_FROB(f28,f30,f32,f34,f36,f38,f40,f42,f44)
	STORE_SYNC(o0, f48)
	FREG_FROB(f44,f46,f0, f2, f4, f6, f8, f10,f12)
	STORE_JUMP(o0, f48, 46f)
2:	FREG_FROB(f44,f46,f0, f2, f4, f6, f8, f10,f12)
	STORE_SYNC(o0, f48)
	FREG_FROB(f12,f14,f16,f18,f20,f22,f24,f26,f28)
	STORE_JUMP(o0, f48, 54f)
3:	FREG_FROB(f12,f14,f16,f18,f20,f22,f24,f26,f28)
	STORE_SYNC(o0, f48)
	FREG_FROB(f28,f30,f32,f34,f36,f38,f40,f42,f44)
	STORE_JUMP(o0, f48, 62f)

1:	FREG_FROB(f14,f16,f18,f20,f22,f24,f26,f28,f30)
	LOOP_CHUNK1(o1, o0, GLOBAL_SPARE, 1f)
	FREG_FROB(f30,f32,f34,f36,f38,f40,f42,f44,f46)
	LOOP_CHUNK2(o1, o0, GLOBAL_SPARE, 2f)
	FREG_FROB(f46,f0, f2, f4, f6, f8, f10,f12,f14)
	LOOP_CHUNK3(o1, o0, GLOBAL_SPARE, 3f)
	ba,pt		%xcc, 1b+4
	 faligndata	%f14, %f16, %f48
1:	FREG_FROB(f30,f32,f34,f36,f38,f40,f42,f44,f46)
	STORE_SYNC(o0, f48)
	FREG_FROB(f46,f0, f2, f4, f6, f8, f10,f12,f14)
	STORE_JUMP(o0, f48, 47f)
2:	FREG_FROB(f46,f0, f2, f4, f6, f8, f10,f12,f14)
	STORE_SYNC(o0, f48)
	FREG_FROB(f14,f16,f18,f20,f22,f24,f26,f28,f30)
	STORE_JUMP(o0, f48, 55f)
3:	FREG_FROB(f14,f16,f18,f20,f22,f24,f26,f28,f30)
	STORE_SYNC(o0, f48)
	FREG_FROB(f30,f32,f34,f36,f38,f40,f42,f44,f46)
	STORE_JUMP(o0, f48, 63f)

40:	FINISH_VISCHUNK(o0, f0,  f2,  g3)
41:	FINISH_VISCHUNK(o0, f2,  f4,  g3)
42:	FINISH_VISCHUNK(o0, f4,  f6,  g3)
43:	FINISH_VISCHUNK(o0, f6,  f8,  g3)
44:	FINISH_VISCHUNK(o0, f8,  f10, g3)
45:	FINISH_VISCHUNK(o0, f10, f12, g3)
46:	FINISH_VISCHUNK(o0, f12, f14, g3)
47:	UNEVEN_VISCHUNK(o0, f14, f0,  g3)
48:	FINISH_VISCHUNK(o0, f16, f18, g3)
49:	FINISH_VISCHUNK(o0, f18, f20, g3)
50:	FINISH_VISCHUNK(o0, f20, f22, g3)
51:	FINISH_VISCHUNK(o0, f22, f24, g3)
52:	FINISH_VISCHUNK(o0, f24, f26, g3)
53:	FINISH_VISCHUNK(o0, f26, f28, g3)
54:	FINISH_VISCHUNK(o0, f28, f30, g3)
55:	UNEVEN_VISCHUNK(o0, f30, f0,  g3)
56:	FINISH_VISCHUNK(o0, f32, f34, g3)
57:	FINISH_VISCHUNK(o0, f34, f36, g3)
58:	FINISH_VISCHUNK(o0, f36, f38, g3)
59:	FINISH_VISCHUNK(o0, f38, f40, g3)
60:	FINISH_VISCHUNK(o0, f40, f42, g3)
61:	FINISH_VISCHUNK(o0, f42, f44, g3)
62:	FINISH_VISCHUNK(o0, f44, f46, g3)
63:	UNEVEN_VISCHUNK_LAST(o0, f46, f0,  g3)

93:	EX_LD(LOAD(ldd, %o1, %f2))
	add		%o1, 8, %o1
	subcc		%g3, 8, %g3
	faligndata	%f0, %f2, %f8
	EX_ST(STORE(std, %f8, %o0))
	bl,pn		%xcc, 95f
	 add		%o0, 8, %o0
	EX_LD(LOAD(ldd, %o1, %f0))
	add		%o1, 8, %o1
	subcc		%g3, 8, %g3
	faligndata	%f2, %f0, %f8
	EX_ST(STORE(std, %f8, %o0))
	bge,pt		%xcc, 93b
	 add		%o0, 8, %o0

95:	brz,pt		%o2, 2f
	 mov		%g1, %o1

1:	EX_LD(LOAD(ldub, %o1, %o3))
	add		%o1, 1, %o1
	subcc		%o2, 1, %o2
	EX_ST(STORE(stb, %o3, %o0))
	bne,pt		%xcc, 1b
	 add		%o0, 1, %o0

2:	membar		#StoreLoad | #StoreStore
	VISExit
	retl
	 mov		EX_RETVAL(%o4), %o0

	.align		64
70:	/* 16 < len <= (5 * 64) */
	bne,pn		%XCC, 75f
	 sub		%o0, %o1, %o3

72:	andn		%o2, 0xf, %GLOBAL_SPARE
	and		%o2, 0xf, %o2
1:	EX_LD(LOAD(ldx, %o1 + 0x00, %o5))
	EX_LD(LOAD(ldx, %o1 + 0x08, %g1))
	subcc		%GLOBAL_SPARE, 0x10, %GLOBAL_SPARE
	EX_ST(STORE(stx, %o5, %o1 + %o3))
	add		%o1, 0x8, %o1
	EX_ST(STORE(stx, %g1, %o1 + %o3))
	bgu,pt		%XCC, 1b
	 add		%o1, 0x8, %o1
73:	andcc		%o2, 0x8, %g0
	be,pt		%XCC, 1f
	 nop
	EX_LD(LOAD(ldx, %o1, %o5))
	sub		%o2, 0x8, %o2
	EX_ST(STORE(stx, %o5, %o1 + %o3))
	add		%o1, 0x8, %o1
1:	andcc		%o2, 0x4, %g0
	be,pt		%XCC, 1f
	 nop
	EX_LD(LOAD(lduw, %o1, %o5))
	sub		%o2, 0x4, %o2
	EX_ST(STORE(stw, %o5, %o1 + %o3))
	add		%o1, 0x4, %o1
1:	cmp		%o2, 0
	be,pt		%XCC, 85f
	 nop
	ba,pt		%xcc, 90f
	 nop

75:	andcc		%o0, 0x7, %g1
	sub		%g1, 0x8, %g1
	be,pn		%icc, 2f
	 sub		%g0, %g1, %g1
	sub		%o2, %g1, %o2

1:	EX_LD(LOAD(ldub, %o1, %o5))
	subcc		%g1, 1, %g1
	EX_ST(STORE(stb, %o5, %o1 + %o3))
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
	EX_LD(LOAD(ldx, %o1, %g2))
	sub		%o3, %g1, %o3
	andn		%o2, 0x7, %GLOBAL_SPARE
	sllx		%g2, %g1, %g2
1:	EX_LD(LOAD(ldx, %o1 + 0x8, %g3))
	subcc		%GLOBAL_SPARE, 0x8, %GLOBAL_SPARE
	add		%o1, 0x8, %o1
	srlx		%g3, %o3, %o5
	or		%o5, %g2, %o5
	EX_ST(STORE(stx, %o5, %o0))
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
80:	/* 0 < len <= 16 */
	andcc		%o3, 0x3, %g0
	bne,pn		%XCC, 90f
	 sub		%o0, %o1, %o3

1:	EX_LD(LOAD(lduw, %o1, %g1))
	subcc		%o2, 4, %o2
	EX_ST(STORE(stw, %g1, %o1 + %o3))
	bgu,pt		%XCC, 1b
	 add		%o1, 4, %o1

85:	retl
	 mov		EX_RETVAL(%o4), %o0

	.align		32
90:	EX_LD(LOAD(ldub, %o1, %g1))
	subcc		%o2, 1, %o2
	EX_ST(STORE(stb, %g1, %o1 + %o3))
	bgu,pt		%XCC, 90b
	 add		%o1, 1, %o1
	retl
	 mov		EX_RETVAL(%o4), %o0

	.size		FUNC_NAME, .-FUNC_NAME
