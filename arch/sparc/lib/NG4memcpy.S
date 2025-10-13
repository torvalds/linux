/* SPDX-License-Identifier: GPL-2.0 */
/* NG4memcpy.S: Niagara-4 optimized memcpy.
 *
 * Copyright (C) 2012 David S. Miller (davem@davemloft.net)
 */

#ifdef __KERNEL__
#include <linux/linkage.h>
#include <asm/visasm.h>
#include <asm/asi.h>
#define GLOBAL_SPARE	%g7
#else
#define ASI_BLK_INIT_QUAD_LDD_P 0xe2
#define FPRS_FEF  0x04

/* On T4 it is very expensive to access ASRs like %fprs and
 * %asi, avoiding a read or a write can save ~50 cycles.
 */
#define FPU_ENTER			\
	rd	%fprs, %o5;		\
	andcc	%o5, FPRS_FEF, %g0;	\
	be,a,pn	%icc, 999f;		\
	 wr	%g0, FPRS_FEF, %fprs;	\
	999:

#ifdef MEMCPY_DEBUG
#define VISEntryHalf FPU_ENTER; \
		     clr %g1; clr %g2; clr %g3; clr %g5; subcc %g0, %g0, %g0;
#define VISExitHalf and %o5, FPRS_FEF, %o5; wr %o5, 0x0, %fprs
#else
#define VISEntryHalf FPU_ENTER
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

#if !defined(EX_LD) && !defined(EX_ST)
#define NON_USER_COPY
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
#ifndef MEMCPY_DEBUG
#define STORE(type,src,addr)	type src, [addr]
#else
#define STORE(type,src,addr)	type##a src, [addr] %asi
#endif
#endif

#ifndef STORE_INIT
#define STORE_INIT(src,addr)	stxa src, [addr] STORE_ASI
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	NG4memcpy
#endif
#ifndef PREAMBLE
#define PREAMBLE
#endif

#ifndef XCC
#define XCC xcc
#endif

	.register	%g2,#scratch
	.register	%g3,#scratch

	.text
#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
#endif
	.align		64

	.globl	FUNC_NAME
	.type	FUNC_NAME,#function
FUNC_NAME:	/* %o0=dst, %o1=src, %o2=len */
#ifdef MEMCPY_DEBUG
	wr		%g0, 0x80, %asi
#endif
	srlx		%o2, 31, %g2
	cmp		%g2, 0
	tne		%XCC, 5
	PREAMBLE
	mov		%o0, %o3
	brz,pn		%o2, .Lexit
	 cmp		%o2, 3
	ble,pn		%icc, .Ltiny
	 cmp		%o2, 19
	ble,pn		%icc, .Lsmall
	 or		%o0, %o1, %g2
	cmp		%o2, 128
	bl,pn		%icc, .Lmedium
	 nop

.Llarge:/* len >= 0x80 */
	/* First get dest 8 byte aligned.  */
	sub		%g0, %o0, %g1
	and		%g1, 0x7, %g1
	brz,pt		%g1, 51f
	 sub		%o2, %g1, %o2


1:	EX_LD(LOAD(ldub, %o1 + 0x00, %g2), memcpy_retl_o2_plus_g1)
	add		%o1, 1, %o1
	subcc		%g1, 1, %g1
	add		%o0, 1, %o0
	bne,pt		%icc, 1b
	 EX_ST(STORE(stb, %g2, %o0 - 0x01), memcpy_retl_o2_plus_g1_plus_1)

51:	LOAD(prefetch, %o1 + 0x040, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x080, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x0c0, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x100, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x140, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x180, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x1c0, #n_reads_strong)
	LOAD(prefetch, %o1 + 0x200, #n_reads_strong)

	/* Check if we can use the straight fully aligned
	 * loop, or we require the alignaddr/faligndata variant.
	 */
	andcc		%o1, 0x7, %o5
	bne,pn		%icc, .Llarge_src_unaligned
	 sub		%g0, %o0, %g1

	/* Legitimize the use of initializing stores by getting dest
	 * to be 64-byte aligned.
	 */
	and		%g1, 0x3f, %g1
	brz,pt		%g1, .Llarge_aligned
	 sub		%o2, %g1, %o2

1:	EX_LD(LOAD(ldx, %o1 + 0x00, %g2), memcpy_retl_o2_plus_g1)
	add		%o1, 8, %o1
	subcc		%g1, 8, %g1
	add		%o0, 8, %o0
	bne,pt		%icc, 1b
	 EX_ST(STORE(stx, %g2, %o0 - 0x08), memcpy_retl_o2_plus_g1_plus_8)

.Llarge_aligned:
	/* len >= 0x80 && src 8-byte aligned && dest 8-byte aligned */
	andn		%o2, 0x3f, %o4
	sub		%o2, %o4, %o2

1:	EX_LD(LOAD(ldx, %o1 + 0x00, %g1), memcpy_retl_o2_plus_o4)
	add		%o1, 0x40, %o1
	EX_LD(LOAD(ldx, %o1 - 0x38, %g2), memcpy_retl_o2_plus_o4)
	subcc		%o4, 0x40, %o4
	EX_LD(LOAD(ldx, %o1 - 0x30, %g3), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD(LOAD(ldx, %o1 - 0x28, GLOBAL_SPARE), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD(LOAD(ldx, %o1 - 0x20, %o5), memcpy_retl_o2_plus_o4_plus_64)
	EX_ST(STORE_INIT(%g1, %o0), memcpy_retl_o2_plus_o4_plus_64)
	add		%o0, 0x08, %o0
	EX_ST(STORE_INIT(%g2, %o0), memcpy_retl_o2_plus_o4_plus_56)
	add		%o0, 0x08, %o0
	EX_LD(LOAD(ldx, %o1 - 0x18, %g2), memcpy_retl_o2_plus_o4_plus_48)
	EX_ST(STORE_INIT(%g3, %o0), memcpy_retl_o2_plus_o4_plus_48)
	add		%o0, 0x08, %o0
	EX_LD(LOAD(ldx, %o1 - 0x10, %g3), memcpy_retl_o2_plus_o4_plus_40)
	EX_ST(STORE_INIT(GLOBAL_SPARE, %o0), memcpy_retl_o2_plus_o4_plus_40)
	add		%o0, 0x08, %o0
	EX_LD(LOAD(ldx, %o1 - 0x08, GLOBAL_SPARE), memcpy_retl_o2_plus_o4_plus_32)
	EX_ST(STORE_INIT(%o5, %o0), memcpy_retl_o2_plus_o4_plus_32)
	add		%o0, 0x08, %o0
	EX_ST(STORE_INIT(%g2, %o0), memcpy_retl_o2_plus_o4_plus_24)
	add		%o0, 0x08, %o0
	EX_ST(STORE_INIT(%g3, %o0), memcpy_retl_o2_plus_o4_plus_16)
	add		%o0, 0x08, %o0
	EX_ST(STORE_INIT(GLOBAL_SPARE, %o0), memcpy_retl_o2_plus_o4_plus_8)
	add		%o0, 0x08, %o0
	bne,pt		%icc, 1b
	 LOAD(prefetch, %o1 + 0x200, #n_reads_strong)

	membar		#StoreLoad | #StoreStore

	brz,pn		%o2, .Lexit
	 cmp		%o2, 19
	ble,pn		%icc, .Lsmall_unaligned
	 nop
	ba,a,pt		%icc, .Lmedium_noprefetch

.Lexit:	retl
	 mov		EX_RETVAL(%o3), %o0

.Llarge_src_unaligned:
#ifdef NON_USER_COPY
	VISEntryHalfFast(.Lmedium_vis_entry_fail)
#else
	VISEntryHalf
#endif
	andn		%o2, 0x3f, %o4
	sub		%o2, %o4, %o2
	alignaddr	%o1, %g0, %g1
	add		%o1, %o4, %o1
	EX_LD_FP(LOAD(ldd, %g1 + 0x00, %f0), memcpy_retl_o2_plus_o4)
1:	EX_LD_FP(LOAD(ldd, %g1 + 0x08, %f2), memcpy_retl_o2_plus_o4)
	subcc		%o4, 0x40, %o4
	EX_LD_FP(LOAD(ldd, %g1 + 0x10, %f4), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD_FP(LOAD(ldd, %g1 + 0x18, %f6), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD_FP(LOAD(ldd, %g1 + 0x20, %f8), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD_FP(LOAD(ldd, %g1 + 0x28, %f10), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD_FP(LOAD(ldd, %g1 + 0x30, %f12), memcpy_retl_o2_plus_o4_plus_64)
	EX_LD_FP(LOAD(ldd, %g1 + 0x38, %f14), memcpy_retl_o2_plus_o4_plus_64)
	faligndata	%f0, %f2, %f16
	EX_LD_FP(LOAD(ldd, %g1 + 0x40, %f0), memcpy_retl_o2_plus_o4_plus_64)
	faligndata	%f2, %f4, %f18
	add		%g1, 0x40, %g1
	faligndata	%f4, %f6, %f20
	faligndata	%f6, %f8, %f22
	faligndata	%f8, %f10, %f24
	faligndata	%f10, %f12, %f26
	faligndata	%f12, %f14, %f28
	faligndata	%f14, %f0, %f30
	EX_ST_FP(STORE(std, %f16, %o0 + 0x00), memcpy_retl_o2_plus_o4_plus_64)
	EX_ST_FP(STORE(std, %f18, %o0 + 0x08), memcpy_retl_o2_plus_o4_plus_56)
	EX_ST_FP(STORE(std, %f20, %o0 + 0x10), memcpy_retl_o2_plus_o4_plus_48)
	EX_ST_FP(STORE(std, %f22, %o0 + 0x18), memcpy_retl_o2_plus_o4_plus_40)
	EX_ST_FP(STORE(std, %f24, %o0 + 0x20), memcpy_retl_o2_plus_o4_plus_32)
	EX_ST_FP(STORE(std, %f26, %o0 + 0x28), memcpy_retl_o2_plus_o4_plus_24)
	EX_ST_FP(STORE(std, %f28, %o0 + 0x30), memcpy_retl_o2_plus_o4_plus_16)
	EX_ST_FP(STORE(std, %f30, %o0 + 0x38), memcpy_retl_o2_plus_o4_plus_8)
	add		%o0, 0x40, %o0
	bne,pt		%icc, 1b
	 LOAD(prefetch, %g1 + 0x200, #n_reads_strong)
#ifdef NON_USER_COPY
	VISExitHalfFast
#else
	VISExitHalf
#endif
	brz,pn		%o2, .Lexit
	 cmp		%o2, 19
	ble,pn		%icc, .Lsmall_unaligned
	 nop
	ba,a,pt		%icc, .Lmedium_unaligned

#ifdef NON_USER_COPY
.Lmedium_vis_entry_fail:
	 or		%o0, %o1, %g2
#endif
.Lmedium:
	LOAD(prefetch, %o1 + 0x40, #n_reads_strong)
	andcc		%g2, 0x7, %g0
	bne,pn		%icc, .Lmedium_unaligned
	 nop
.Lmedium_noprefetch:
	andncc		%o2, 0x20 - 1, %o5
	be,pn		%icc, 2f
	 sub		%o2, %o5, %o2
1:	EX_LD(LOAD(ldx, %o1 + 0x00, %g1), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x08, %g2), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x10, GLOBAL_SPARE), memcpy_retl_o2_plus_o5)
	EX_LD(LOAD(ldx, %o1 + 0x18, %o4), memcpy_retl_o2_plus_o5)
	add		%o1, 0x20, %o1
	subcc		%o5, 0x20, %o5
	EX_ST(STORE(stx, %g1, %o0 + 0x00), memcpy_retl_o2_plus_o5_plus_32)
	EX_ST(STORE(stx, %g2, %o0 + 0x08), memcpy_retl_o2_plus_o5_plus_24)
	EX_ST(STORE(stx, GLOBAL_SPARE, %o0 + 0x10), memcpy_retl_o2_plus_o5_plus_16)
	EX_ST(STORE(stx, %o4, %o0 + 0x18), memcpy_retl_o2_plus_o5_plus_8)
	bne,pt		%icc, 1b
	 add		%o0, 0x20, %o0
2:	andcc		%o2, 0x18, %o5
	be,pt		%icc, 3f
	 sub		%o2, %o5, %o2

1:	EX_LD(LOAD(ldx, %o1 + 0x00, %g1), memcpy_retl_o2_plus_o5)
	add		%o1, 0x08, %o1
	add		%o0, 0x08, %o0
	subcc		%o5, 0x08, %o5
	bne,pt		%icc, 1b
	 EX_ST(STORE(stx, %g1, %o0 - 0x08), memcpy_retl_o2_plus_o5_plus_8)
3:	brz,pt		%o2, .Lexit
	 cmp		%o2, 0x04
	bl,pn		%icc, .Ltiny
	 nop
	EX_LD(LOAD(lduw, %o1 + 0x00, %g1), memcpy_retl_o2)
	add		%o1, 0x04, %o1
	add		%o0, 0x04, %o0
	subcc		%o2, 0x04, %o2
	bne,pn		%icc, .Ltiny
	 EX_ST(STORE(stw, %g1, %o0 - 0x04), memcpy_retl_o2_plus_4)
	ba,a,pt		%icc, .Lexit
.Lmedium_unaligned:
	/* First get dest 8 byte aligned.  */
	sub		%g0, %o0, %g1
	and		%g1, 0x7, %g1
	brz,pt		%g1, 2f
	 sub		%o2, %g1, %o2

1:	EX_LD(LOAD(ldub, %o1 + 0x00, %g2), memcpy_retl_o2_plus_g1)
	add		%o1, 1, %o1
	subcc		%g1, 1, %g1
	add		%o0, 1, %o0
	bne,pt		%icc, 1b
	 EX_ST(STORE(stb, %g2, %o0 - 0x01), memcpy_retl_o2_plus_g1_plus_1)
2:
	and		%o1, 0x7, %g1
	brz,pn		%g1, .Lmedium_noprefetch
	 sll		%g1, 3, %g1
	mov		64, %g2
	sub		%g2, %g1, %g2
	andn		%o1, 0x7, %o1
	EX_LD(LOAD(ldx, %o1 + 0x00, %o4), memcpy_retl_o2)
	sllx		%o4, %g1, %o4
	andn		%o2, 0x08 - 1, %o5
	sub		%o2, %o5, %o2
1:	EX_LD(LOAD(ldx, %o1 + 0x08, %g3), memcpy_retl_o2_plus_o5)
	add		%o1, 0x08, %o1
	subcc		%o5, 0x08, %o5
	srlx		%g3, %g2, GLOBAL_SPARE
	or		GLOBAL_SPARE, %o4, GLOBAL_SPARE
	EX_ST(STORE(stx, GLOBAL_SPARE, %o0 + 0x00), memcpy_retl_o2_plus_o5_plus_8)
	add		%o0, 0x08, %o0
	bne,pt		%icc, 1b
	 sllx		%g3, %g1, %o4
	srl		%g1, 3, %g1
	add		%o1, %g1, %o1
	brz,pn		%o2, .Lexit
	 nop
	ba,pt		%icc, .Lsmall_unaligned

.Ltiny:
	EX_LD(LOAD(ldub, %o1 + 0x00, %g1), memcpy_retl_o2)
	subcc		%o2, 1, %o2
	be,pn		%icc, .Lexit
	 EX_ST(STORE(stb, %g1, %o0 + 0x00), memcpy_retl_o2_plus_1)
	EX_LD(LOAD(ldub, %o1 + 0x01, %g1), memcpy_retl_o2)
	subcc		%o2, 1, %o2
	be,pn		%icc, .Lexit
	 EX_ST(STORE(stb, %g1, %o0 + 0x01), memcpy_retl_o2_plus_1)
	EX_LD(LOAD(ldub, %o1 + 0x02, %g1), memcpy_retl_o2)
	ba,pt		%icc, .Lexit
	 EX_ST(STORE(stb, %g1, %o0 + 0x02), memcpy_retl_o2)

.Lsmall:
	andcc		%g2, 0x3, %g0
	bne,pn		%icc, .Lsmall_unaligned
	 andn		%o2, 0x4 - 1, %o5
	sub		%o2, %o5, %o2
1:
	EX_LD(LOAD(lduw, %o1 + 0x00, %g1), memcpy_retl_o2_plus_o5)
	add		%o1, 0x04, %o1
	subcc		%o5, 0x04, %o5
	add		%o0, 0x04, %o0
	bne,pt		%icc, 1b
	 EX_ST(STORE(stw, %g1, %o0 - 0x04), memcpy_retl_o2_plus_o5_plus_4)
	brz,pt		%o2, .Lexit
	 nop
	ba,a,pt		%icc, .Ltiny

.Lsmall_unaligned:
1:	EX_LD(LOAD(ldub, %o1 + 0x00, %g1), memcpy_retl_o2)
	add		%o1, 1, %o1
	add		%o0, 1, %o0
	subcc		%o2, 1, %o2
	bne,pt		%icc, 1b
	 EX_ST(STORE(stb, %g1, %o0 - 0x01), memcpy_retl_o2_plus_1)
	ba,a,pt		%icc, .Lexit
	 nop
	.size		FUNC_NAME, .-FUNC_NAME
