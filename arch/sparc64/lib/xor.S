/*
 * arch/sparc64/lib/xor.S
 *
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <asm/visasm.h>
#include <asm/asi.h>
#include <asm/dcu.h>
#include <asm/spitfire.h>

/*
 *	Requirements:
 *	!(((long)dest | (long)sourceN) & (64 - 1)) &&
 *	!(len & 127) && len >= 256
 */
	.text
	.align	32
	.globl	xor_vis_2
	.type	xor_vis_2,#function
xor_vis_2:
	rd	%fprs, %o5
	andcc	%o5, FPRS_FEF|FPRS_DU, %g0
	be,pt	%icc, 0f
	 sethi	%hi(VISenter), %g1
	jmpl	%g1 + %lo(VISenter), %g7
	 add	%g7, 8, %g7
0:	wr	%g0, FPRS_FEF, %fprs
	rd	%asi, %g1
	wr	%g0, ASI_BLK_P, %asi
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%o0, 128, %o0
	ldda	[%o1] %asi, %f0
	ldda	[%o2] %asi, %f16

2:	ldda	[%o1 + 64] %asi, %f32
	fxor	%f0, %f16, %f16
	fxor	%f2, %f18, %f18
	fxor	%f4, %f20, %f20
	fxor	%f6, %f22, %f22
	fxor	%f8, %f24, %f24
	fxor	%f10, %f26, %f26
	fxor	%f12, %f28, %f28
	fxor	%f14, %f30, %f30
	stda	%f16, [%o1] %asi
	ldda	[%o2 + 64] %asi, %f48
	ldda	[%o1 + 128] %asi, %f0
	fxor	%f32, %f48, %f48
	fxor	%f34, %f50, %f50
	add	%o1, 128, %o1
	fxor	%f36, %f52, %f52
	add	%o2, 128, %o2
	fxor	%f38, %f54, %f54
	subcc	%o0, 128, %o0
	fxor	%f40, %f56, %f56
	fxor	%f42, %f58, %f58
	fxor	%f44, %f60, %f60
	fxor	%f46, %f62, %f62
	stda	%f48, [%o1 - 64] %asi
	bne,pt	%xcc, 2b
	 ldda	[%o2] %asi, %f16

	ldda	[%o1 + 64] %asi, %f32
	fxor	%f0, %f16, %f16
	fxor	%f2, %f18, %f18
	fxor	%f4, %f20, %f20
	fxor	%f6, %f22, %f22
	fxor	%f8, %f24, %f24
	fxor	%f10, %f26, %f26
	fxor	%f12, %f28, %f28
	fxor	%f14, %f30, %f30
	stda	%f16, [%o1] %asi
	ldda	[%o2 + 64] %asi, %f48
	membar	#Sync
	fxor	%f32, %f48, %f48
	fxor	%f34, %f50, %f50
	fxor	%f36, %f52, %f52
	fxor	%f38, %f54, %f54
	fxor	%f40, %f56, %f56
	fxor	%f42, %f58, %f58
	fxor	%f44, %f60, %f60
	fxor	%f46, %f62, %f62
	stda	%f48, [%o1 + 64] %asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%g1, %g0, %asi
	retl
	  wr	%g0, 0, %fprs
	.size	xor_vis_2, .-xor_vis_2

	.globl	xor_vis_3
	.type	xor_vis_3,#function
xor_vis_3:
	rd	%fprs, %o5
	andcc	%o5, FPRS_FEF|FPRS_DU, %g0
	be,pt	%icc, 0f
	 sethi	%hi(VISenter), %g1
	jmpl	%g1 + %lo(VISenter), %g7
	 add	%g7, 8, %g7
0:	wr	%g0, FPRS_FEF, %fprs
	rd	%asi, %g1
	wr	%g0, ASI_BLK_P, %asi
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%o0, 64, %o0
	ldda	[%o1] %asi, %f0
	ldda	[%o2] %asi, %f16

3:	ldda	[%o3] %asi, %f32
	fxor	%f0, %f16, %f48
	fxor	%f2, %f18, %f50
	add	%o1, 64, %o1
	fxor	%f4, %f20, %f52
	fxor	%f6, %f22, %f54
	add	%o2, 64, %o2
	fxor	%f8, %f24, %f56
	fxor	%f10, %f26, %f58
	fxor	%f12, %f28, %f60
	fxor	%f14, %f30, %f62
	ldda	[%o1] %asi, %f0
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	add	%o3, 64, %o3
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	subcc	%o0, 64, %o0
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	stda	%f48, [%o1 - 64] %asi
	bne,pt	%xcc, 3b
	 ldda	[%o2] %asi, %f16

	ldda	[%o3] %asi, %f32
	fxor	%f0, %f16, %f48
	fxor	%f2, %f18, %f50
	fxor	%f4, %f20, %f52
	fxor	%f6, %f22, %f54
	fxor	%f8, %f24, %f56
	fxor	%f10, %f26, %f58
	fxor	%f12, %f28, %f60
	fxor	%f14, %f30, %f62
	membar	#Sync
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	stda	%f48, [%o1] %asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%g1, %g0, %asi
	retl
	 wr	%g0, 0, %fprs
	.size	xor_vis_3, .-xor_vis_3

	.globl	xor_vis_4
	.type	xor_vis_4,#function
xor_vis_4:
	rd	%fprs, %o5
	andcc	%o5, FPRS_FEF|FPRS_DU, %g0
	be,pt	%icc, 0f
	 sethi	%hi(VISenter), %g1
	jmpl	%g1 + %lo(VISenter), %g7
	 add	%g7, 8, %g7
0:	wr	%g0, FPRS_FEF, %fprs
	rd	%asi, %g1
	wr	%g0, ASI_BLK_P, %asi
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%o0, 64, %o0
	ldda	[%o1] %asi, %f0
	ldda	[%o2] %asi, %f16

4:	ldda	[%o3] %asi, %f32
	fxor	%f0, %f16, %f16
	fxor	%f2, %f18, %f18
	add	%o1, 64, %o1
	fxor	%f4, %f20, %f20
	fxor	%f6, %f22, %f22
	add	%o2, 64, %o2
	fxor	%f8, %f24, %f24
	fxor	%f10, %f26, %f26
	fxor	%f12, %f28, %f28
	fxor	%f14, %f30, %f30
	ldda	[%o4] %asi, %f48
	fxor	%f16, %f32, %f32
	fxor	%f18, %f34, %f34
	fxor	%f20, %f36, %f36
	fxor	%f22, %f38, %f38
	add	%o3, 64, %o3
	fxor	%f24, %f40, %f40
	fxor	%f26, %f42, %f42
	fxor	%f28, %f44, %f44
	fxor	%f30, %f46, %f46
	ldda	[%o1] %asi, %f0
	fxor	%f32, %f48, %f48
	fxor	%f34, %f50, %f50
	fxor	%f36, %f52, %f52
	add	%o4, 64, %o4
	fxor	%f38, %f54, %f54
	fxor	%f40, %f56, %f56
	fxor	%f42, %f58, %f58
	subcc	%o0, 64, %o0
	fxor	%f44, %f60, %f60
	fxor	%f46, %f62, %f62
	stda	%f48, [%o1 - 64] %asi
	bne,pt	%xcc, 4b
	 ldda	[%o2] %asi, %f16

	ldda	[%o3] %asi, %f32
	fxor	%f0, %f16, %f16
	fxor	%f2, %f18, %f18
	fxor	%f4, %f20, %f20
	fxor	%f6, %f22, %f22
	fxor	%f8, %f24, %f24
	fxor	%f10, %f26, %f26
	fxor	%f12, %f28, %f28
	fxor	%f14, %f30, %f30
	ldda	[%o4] %asi, %f48
	fxor	%f16, %f32, %f32
	fxor	%f18, %f34, %f34
	fxor	%f20, %f36, %f36
	fxor	%f22, %f38, %f38
	fxor	%f24, %f40, %f40
	fxor	%f26, %f42, %f42
	fxor	%f28, %f44, %f44
	fxor	%f30, %f46, %f46
	membar	#Sync
	fxor	%f32, %f48, %f48
	fxor	%f34, %f50, %f50
	fxor	%f36, %f52, %f52
	fxor	%f38, %f54, %f54
	fxor	%f40, %f56, %f56
	fxor	%f42, %f58, %f58
	fxor	%f44, %f60, %f60
	fxor	%f46, %f62, %f62
	stda	%f48, [%o1] %asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%g1, %g0, %asi
	retl
	 wr	%g0, 0, %fprs
	.size	xor_vis_4, .-xor_vis_4

	.globl	xor_vis_5
	.type	xor_vis_5,#function
xor_vis_5:
	save	%sp, -192, %sp
	rd	%fprs, %o5
	andcc	%o5, FPRS_FEF|FPRS_DU, %g0
	be,pt	%icc, 0f
	 sethi	%hi(VISenter), %g1
	jmpl	%g1 + %lo(VISenter), %g7
	 add	%g7, 8, %g7
0:	wr	%g0, FPRS_FEF, %fprs
	rd	%asi, %g1
	wr	%g0, ASI_BLK_P, %asi
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%i0, 64, %i0
	ldda	[%i1] %asi, %f0
	ldda	[%i2] %asi, %f16

5:	ldda	[%i3] %asi, %f32
	fxor	%f0, %f16, %f48
	fxor	%f2, %f18, %f50
	add	%i1, 64, %i1
	fxor	%f4, %f20, %f52
	fxor	%f6, %f22, %f54
	add	%i2, 64, %i2
	fxor	%f8, %f24, %f56
	fxor	%f10, %f26, %f58
	fxor	%f12, %f28, %f60
	fxor	%f14, %f30, %f62
	ldda	[%i4] %asi, %f16
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	add	%i3, 64, %i3
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	ldda	[%i5] %asi, %f32
	fxor	%f48, %f16, %f48
	fxor	%f50, %f18, %f50
	add	%i4, 64, %i4
	fxor	%f52, %f20, %f52
	fxor	%f54, %f22, %f54
	add	%i5, 64, %i5
	fxor	%f56, %f24, %f56
	fxor	%f58, %f26, %f58
	fxor	%f60, %f28, %f60
	fxor	%f62, %f30, %f62
	ldda	[%i1] %asi, %f0
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	subcc	%i0, 64, %i0
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	stda	%f48, [%i1 - 64] %asi
	bne,pt	%xcc, 5b
	 ldda	[%i2] %asi, %f16

	ldda	[%i3] %asi, %f32
	fxor	%f0, %f16, %f48
	fxor	%f2, %f18, %f50
	fxor	%f4, %f20, %f52
	fxor	%f6, %f22, %f54
	fxor	%f8, %f24, %f56
	fxor	%f10, %f26, %f58
	fxor	%f12, %f28, %f60
	fxor	%f14, %f30, %f62
	ldda	[%i4] %asi, %f16
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	ldda	[%i5] %asi, %f32
	fxor	%f48, %f16, %f48
	fxor	%f50, %f18, %f50
	fxor	%f52, %f20, %f52
	fxor	%f54, %f22, %f54
	fxor	%f56, %f24, %f56
	fxor	%f58, %f26, %f58
	fxor	%f60, %f28, %f60
	fxor	%f62, %f30, %f62
	membar	#Sync
	fxor	%f48, %f32, %f48
	fxor	%f50, %f34, %f50
	fxor	%f52, %f36, %f52
	fxor	%f54, %f38, %f54
	fxor	%f56, %f40, %f56
	fxor	%f58, %f42, %f58
	fxor	%f60, %f44, %f60
	fxor	%f62, %f46, %f62
	stda	%f48, [%i1] %asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%g1, %g0, %asi
	wr	%g0, 0, %fprs
	ret
	 restore
	.size	xor_vis_5, .-xor_vis_5
