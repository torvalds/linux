/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/sparc64/lib/xor.S
 *
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set and Niagara store-init/twin-load.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <linux/export.h>
#include <linux/linkage.h>
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

	/* VIS versions. */
ENTRY(xor_vis_2)
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
ENDPROC(xor_vis_2)
EXPORT_SYMBOL(xor_vis_2)

ENTRY(xor_vis_3)
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
ENDPROC(xor_vis_3)
EXPORT_SYMBOL(xor_vis_3)

ENTRY(xor_vis_4)
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
ENDPROC(xor_vis_4)
EXPORT_SYMBOL(xor_vis_4)

ENTRY(xor_vis_5)
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
ENDPROC(xor_vis_5)
EXPORT_SYMBOL(xor_vis_5)

	/* Niagara versions. */
ENTRY(xor_niagara_2) /* %o0=bytes, %o1=dest, %o2=src */
	save		%sp, -192, %sp
	prefetch	[%i1], #n_writes
	prefetch	[%i2], #one_read
	rd		%asi, %g7
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	srlx		%i0, 6, %g1
	mov		%i1, %i0
	mov		%i2, %i1
1:	ldda		[%i1 + 0x00] %asi, %i2	/* %i2/%i3 = src  + 0x00 */
	ldda		[%i1 + 0x10] %asi, %i4	/* %i4/%i5 = src  + 0x10 */
	ldda		[%i1 + 0x20] %asi, %g2	/* %g2/%g3 = src  + 0x20 */
	ldda		[%i1 + 0x30] %asi, %l0	/* %l0/%l1 = src  + 0x30 */
	prefetch	[%i1 + 0x40], #one_read
	ldda		[%i0 + 0x00] %asi, %o0  /* %o0/%o1 = dest + 0x00 */
	ldda		[%i0 + 0x10] %asi, %o2  /* %o2/%o3 = dest + 0x10 */
	ldda		[%i0 + 0x20] %asi, %o4  /* %o4/%o5 = dest + 0x20 */
	ldda		[%i0 + 0x30] %asi, %l2  /* %l2/%l3 = dest + 0x30 */
	prefetch	[%i0 + 0x40], #n_writes
	xor		%o0, %i2, %o0
	xor		%o1, %i3, %o1
	stxa		%o0, [%i0 + 0x00] %asi
	stxa		%o1, [%i0 + 0x08] %asi
	xor		%o2, %i4, %o2
	xor		%o3, %i5, %o3
	stxa		%o2, [%i0 + 0x10] %asi
	stxa		%o3, [%i0 + 0x18] %asi
	xor		%o4, %g2, %o4
	xor		%o5, %g3, %o5
	stxa		%o4, [%i0 + 0x20] %asi
	stxa		%o5, [%i0 + 0x28] %asi
	xor		%l2, %l0, %l2
	xor		%l3, %l1, %l3
	stxa		%l2, [%i0 + 0x30] %asi
	stxa		%l3, [%i0 + 0x38] %asi
	add		%i0, 0x40, %i0
	subcc		%g1, 1, %g1
	bne,pt		%xcc, 1b
	 add		%i1, 0x40, %i1
	membar		#Sync
	wr		%g7, 0x0, %asi
	ret
	 restore
ENDPROC(xor_niagara_2)
EXPORT_SYMBOL(xor_niagara_2)

ENTRY(xor_niagara_3) /* %o0=bytes, %o1=dest, %o2=src1, %o3=src2 */
	save		%sp, -192, %sp
	prefetch	[%i1], #n_writes
	prefetch	[%i2], #one_read
	prefetch	[%i3], #one_read
	rd		%asi, %g7
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	srlx		%i0, 6, %g1
	mov		%i1, %i0
	mov		%i2, %i1
	mov		%i3, %l7
1:	ldda		[%i1 + 0x00] %asi, %i2	/* %i2/%i3 = src1 + 0x00 */
	ldda		[%i1 + 0x10] %asi, %i4	/* %i4/%i5 = src1 + 0x10 */
	ldda		[%l7 + 0x00] %asi, %g2	/* %g2/%g3 = src2 + 0x00 */
	ldda		[%l7 + 0x10] %asi, %l0	/* %l0/%l1 = src2 + 0x10 */
	ldda		[%i0 + 0x00] %asi, %o0  /* %o0/%o1 = dest + 0x00 */
	ldda		[%i0 + 0x10] %asi, %o2  /* %o2/%o3 = dest + 0x10 */
	xor		%g2, %i2, %g2
	xor		%g3, %i3, %g3
	xor		%o0, %g2, %o0
	xor		%o1, %g3, %o1
	stxa		%o0, [%i0 + 0x00] %asi
	stxa		%o1, [%i0 + 0x08] %asi
	ldda		[%i1 + 0x20] %asi, %i2	/* %i2/%i3 = src1 + 0x20 */
	ldda		[%l7 + 0x20] %asi, %g2	/* %g2/%g3 = src2 + 0x20 */
	ldda		[%i0 + 0x20] %asi, %o0	/* %o0/%o1 = dest + 0x20 */
	xor		%l0, %i4, %l0
	xor		%l1, %i5, %l1
	xor		%o2, %l0, %o2
	xor		%o3, %l1, %o3
	stxa		%o2, [%i0 + 0x10] %asi
	stxa		%o3, [%i0 + 0x18] %asi
	ldda		[%i1 + 0x30] %asi, %i4	/* %i4/%i5 = src1 + 0x30 */
	ldda		[%l7 + 0x30] %asi, %l0	/* %l0/%l1 = src2 + 0x30 */
	ldda		[%i0 + 0x30] %asi, %o2	/* %o2/%o3 = dest + 0x30 */
	prefetch	[%i1 + 0x40], #one_read
	prefetch	[%l7 + 0x40], #one_read
	prefetch	[%i0 + 0x40], #n_writes
	xor		%g2, %i2, %g2
	xor		%g3, %i3, %g3
	xor		%o0, %g2, %o0
	xor		%o1, %g3, %o1
	stxa		%o0, [%i0 + 0x20] %asi
	stxa		%o1, [%i0 + 0x28] %asi
	xor		%l0, %i4, %l0
	xor		%l1, %i5, %l1
	xor		%o2, %l0, %o2
	xor		%o3, %l1, %o3
	stxa		%o2, [%i0 + 0x30] %asi
	stxa		%o3, [%i0 + 0x38] %asi
	add		%i0, 0x40, %i0
	add		%i1, 0x40, %i1
	subcc		%g1, 1, %g1
	bne,pt		%xcc, 1b
	 add		%l7, 0x40, %l7
	membar		#Sync
	wr		%g7, 0x0, %asi
	ret
	 restore
ENDPROC(xor_niagara_3)
EXPORT_SYMBOL(xor_niagara_3)

ENTRY(xor_niagara_4) /* %o0=bytes, %o1=dest, %o2=src1, %o3=src2, %o4=src3 */
	save		%sp, -192, %sp
	prefetch	[%i1], #n_writes
	prefetch	[%i2], #one_read
	prefetch	[%i3], #one_read
	prefetch	[%i4], #one_read
	rd		%asi, %g7
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	srlx		%i0, 6, %g1
	mov		%i1, %i0
	mov		%i2, %i1
	mov		%i3, %l7
	mov		%i4, %l6
1:	ldda		[%i1 + 0x00] %asi, %i2	/* %i2/%i3 = src1 + 0x00 */
	ldda		[%l7 + 0x00] %asi, %i4	/* %i4/%i5 = src2 + 0x00 */
	ldda		[%l6 + 0x00] %asi, %g2	/* %g2/%g3 = src3 + 0x00 */
	ldda		[%i0 + 0x00] %asi, %l0	/* %l0/%l1 = dest + 0x00 */
	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x10] %asi, %i2	/* %i2/%i3 = src1 + 0x10 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x10] %asi, %i4	/* %i4/%i5 = src2 + 0x10 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	stxa		%l0, [%i0 + 0x00] %asi
	stxa		%l1, [%i0 + 0x08] %asi
	ldda		[%l6 + 0x10] %asi, %g2	/* %g2/%g3 = src3 + 0x10 */
	ldda		[%i0 + 0x10] %asi, %l0	/* %l0/%l1 = dest + 0x10 */

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x20] %asi, %i2	/* %i2/%i3 = src1 + 0x20 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x20] %asi, %i4	/* %i4/%i5 = src2 + 0x20 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	stxa		%l0, [%i0 + 0x10] %asi
	stxa		%l1, [%i0 + 0x18] %asi
	ldda		[%l6 + 0x20] %asi, %g2	/* %g2/%g3 = src3 + 0x20 */
	ldda		[%i0 + 0x20] %asi, %l0	/* %l0/%l1 = dest + 0x20 */

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x30] %asi, %i2	/* %i2/%i3 = src1 + 0x30 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x30] %asi, %i4	/* %i4/%i5 = src2 + 0x30 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	stxa		%l0, [%i0 + 0x20] %asi
	stxa		%l1, [%i0 + 0x28] %asi
	ldda		[%l6 + 0x30] %asi, %g2	/* %g2/%g3 = src3 + 0x30 */
	ldda		[%i0 + 0x30] %asi, %l0	/* %l0/%l1 = dest + 0x30 */

	prefetch	[%i1 + 0x40], #one_read
	prefetch	[%l7 + 0x40], #one_read
	prefetch	[%l6 + 0x40], #one_read
	prefetch	[%i0 + 0x40], #n_writes

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	stxa		%l0, [%i0 + 0x30] %asi
	stxa		%l1, [%i0 + 0x38] %asi

	add		%i0, 0x40, %i0
	add		%i1, 0x40, %i1
	add		%l7, 0x40, %l7
	subcc		%g1, 1, %g1
	bne,pt		%xcc, 1b
	 add		%l6, 0x40, %l6
	membar		#Sync
	wr		%g7, 0x0, %asi
	ret
	 restore
ENDPROC(xor_niagara_4)
EXPORT_SYMBOL(xor_niagara_4)

ENTRY(xor_niagara_5) /* %o0=bytes, %o1=dest, %o2=src1, %o3=src2, %o4=src3, %o5=src4 */
	save		%sp, -192, %sp
	prefetch	[%i1], #n_writes
	prefetch	[%i2], #one_read
	prefetch	[%i3], #one_read
	prefetch	[%i4], #one_read
	prefetch	[%i5], #one_read
	rd		%asi, %g7
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	srlx		%i0, 6, %g1
	mov		%i1, %i0
	mov		%i2, %i1
	mov		%i3, %l7
	mov		%i4, %l6
	mov		%i5, %l5
1:	ldda		[%i1 + 0x00] %asi, %i2	/* %i2/%i3 = src1 + 0x00 */
	ldda		[%l7 + 0x00] %asi, %i4	/* %i4/%i5 = src2 + 0x00 */
	ldda		[%l6 + 0x00] %asi, %g2	/* %g2/%g3 = src3 + 0x00 */
	ldda		[%l5 + 0x00] %asi, %l0	/* %l0/%l1 = src4 + 0x00 */
	ldda		[%i0 + 0x00] %asi, %l2	/* %l2/%l3 = dest + 0x00 */
	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x10] %asi, %i2	/* %i2/%i3 = src1 + 0x10 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x10] %asi, %i4	/* %i4/%i5 = src2 + 0x10 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	ldda		[%l6 + 0x10] %asi, %g2	/* %g2/%g3 = src3 + 0x10 */
	xor		%l2, %l0, %l2
	xor		%l3, %l1, %l3
	stxa		%l2, [%i0 + 0x00] %asi
	stxa		%l3, [%i0 + 0x08] %asi
	ldda		[%l5 + 0x10] %asi, %l0	/* %l0/%l1 = src4 + 0x10 */
	ldda		[%i0 + 0x10] %asi, %l2	/* %l2/%l3 = dest + 0x10 */

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x20] %asi, %i2	/* %i2/%i3 = src1 + 0x20 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x20] %asi, %i4	/* %i4/%i5 = src2 + 0x20 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	ldda		[%l6 + 0x20] %asi, %g2	/* %g2/%g3 = src3 + 0x20 */
	xor		%l2, %l0, %l2
	xor		%l3, %l1, %l3
	stxa		%l2, [%i0 + 0x10] %asi
	stxa		%l3, [%i0 + 0x18] %asi
	ldda		[%l5 + 0x20] %asi, %l0	/* %l0/%l1 = src4 + 0x20 */
	ldda		[%i0 + 0x20] %asi, %l2	/* %l2/%l3 = dest + 0x20 */

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	ldda		[%i1 + 0x30] %asi, %i2	/* %i2/%i3 = src1 + 0x30 */
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	ldda		[%l7 + 0x30] %asi, %i4	/* %i4/%i5 = src2 + 0x30 */
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	ldda		[%l6 + 0x30] %asi, %g2	/* %g2/%g3 = src3 + 0x30 */
	xor		%l2, %l0, %l2
	xor		%l3, %l1, %l3
	stxa		%l2, [%i0 + 0x20] %asi
	stxa		%l3, [%i0 + 0x28] %asi
	ldda		[%l5 + 0x30] %asi, %l0	/* %l0/%l1 = src4 + 0x30 */
	ldda		[%i0 + 0x30] %asi, %l2	/* %l2/%l3 = dest + 0x30 */

	prefetch	[%i1 + 0x40], #one_read
	prefetch	[%l7 + 0x40], #one_read
	prefetch	[%l6 + 0x40], #one_read
	prefetch	[%l5 + 0x40], #one_read
	prefetch	[%i0 + 0x40], #n_writes

	xor		%i4, %i2, %i4
	xor		%i5, %i3, %i5
	xor		%g2, %i4, %g2
	xor		%g3, %i5, %g3
	xor		%l0, %g2, %l0
	xor		%l1, %g3, %l1
	xor		%l2, %l0, %l2
	xor		%l3, %l1, %l3
	stxa		%l2, [%i0 + 0x30] %asi
	stxa		%l3, [%i0 + 0x38] %asi

	add		%i0, 0x40, %i0
	add		%i1, 0x40, %i1
	add		%l7, 0x40, %l7
	add		%l6, 0x40, %l6
	subcc		%g1, 1, %g1
	bne,pt		%xcc, 1b
	 add		%l5, 0x40, %l5
	membar		#Sync
	wr		%g7, 0x0, %asi
	ret
	 restore
ENDPROC(xor_niagara_5)
EXPORT_SYMBOL(xor_niagara_5)
