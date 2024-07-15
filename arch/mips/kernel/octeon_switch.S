/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1998, 1999, 2002, 2003 Ralf Baechle
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1994, 1995, 1996, by Andreas Busse
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 MIPS Technologies, Inc.
 *    written by Carsten Langgaard, carstenl@mips.com
 */
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *		       struct thread_info *next_ti)
 */
	.align	7
	LEAF(resume)
	.set arch=octeon
	mfc0	t1, CP0_STATUS
	LONG_S	t1, THREAD_STATUS(a0)
	cpu_save_nonscratch a0
	LONG_S	ra, THREAD_REG31(a0)

#if CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE > 0
	/* Check if we need to store CVMSEG state */
	dmfc0	t0, $11,7	/* CvmMemCtl */
	bbit0	t0, 6, 3f	/* Is user access enabled? */

	/* Store the CVMSEG state */
	/* Extract the size of CVMSEG */
	andi	t0, 0x3f
	/* Multiply * (cache line size/sizeof(long)/2) */
	sll	t0, 7-LONGLOG-1
	li	t1, -32768	/* Base address of CVMSEG */
	LONG_ADDI t2, a0, THREAD_CVMSEG /* Where to store CVMSEG to */
	synciobdma
2:
	.set noreorder
	LONG_L	t8, 0(t1)	/* Load from CVMSEG */
	subu	t0, 1		/* Decrement loop var */
	LONG_L	t9, LONGSIZE(t1)/* Load from CVMSEG */
	LONG_ADDU t1, LONGSIZE*2 /* Increment loc in CVMSEG */
	LONG_S	t8, 0(t2)	/* Store CVMSEG to thread storage */
	LONG_ADDU t2, LONGSIZE*2 /* Increment loc in thread storage */
	bnez	t0, 2b		/* Loop until we've copied it all */
	 LONG_S t9, -LONGSIZE(t2)/* Store CVMSEG to thread storage */
	.set reorder

	/* Disable access to CVMSEG */
	dmfc0	t0, $11,7	/* CvmMemCtl */
	xori	t0, t0, 0x40	/* Bit 6 is CVMSEG user enable */
	dmtc0	t0, $11,7	/* CvmMemCtl */
#endif
3:

#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	PTR_LA	t8, __stack_chk_guard
	LONG_L	t9, TASK_STACK_CANARY(a1)
	LONG_S	t9, 0(t8)
#endif

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	cpu_restore_nonscratch a1

	PTR_ADDU	t0, $28, _THREAD_SIZE - 32
	set_saved_sp	t0, t1, t2

	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff01
	and	t1, a3
	LONG_L	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
	move	v0, a0
	jr	ra
	END(resume)

/*
 * void octeon_cop2_save(struct octeon_cop2_state *a0)
 */
	.align	7
	.set push
	.set noreorder
	LEAF(octeon_cop2_save)

	dmfc0	t9, $9,7	/* CvmCtl register. */

	/* Save the COP2 CRC state */
	dmfc2	t0, 0x0201
	dmfc2	t1, 0x0202
	dmfc2	t2, 0x0200
	sd	t0, OCTEON_CP2_CRC_IV(a0)
	sd	t1, OCTEON_CP2_CRC_LENGTH(a0)
	/* Skip next instructions if CvmCtl[NODFA_CP2] set */
	bbit1	t9, 28, 1f
	 sd	t2, OCTEON_CP2_CRC_POLY(a0)

	/* Save the LLM state */
	dmfc2	t0, 0x0402
	dmfc2	t1, 0x040A
	sd	t0, OCTEON_CP2_LLM_DAT(a0)

1:	bbit1	t9, 26, 3f	/* done if CvmCtl[NOCRYPTO] set */
	 sd	t1, OCTEON_CP2_LLM_DAT+8(a0)

	/* Save the COP2 crypto state */
	/* this part is mostly common to both pass 1 and later revisions */
	dmfc2	t0, 0x0084
	dmfc2	t1, 0x0080
	dmfc2	t2, 0x0081
	dmfc2	t3, 0x0082
	sd	t0, OCTEON_CP2_3DES_IV(a0)
	dmfc2	t0, 0x0088
	sd	t1, OCTEON_CP2_3DES_KEY(a0)
	dmfc2	t1, 0x0111			/* only necessary for pass 1 */
	sd	t2, OCTEON_CP2_3DES_KEY+8(a0)
	dmfc2	t2, 0x0102
	sd	t3, OCTEON_CP2_3DES_KEY+16(a0)
	dmfc2	t3, 0x0103
	sd	t0, OCTEON_CP2_3DES_RESULT(a0)
	dmfc2	t0, 0x0104
	sd	t1, OCTEON_CP2_AES_INP0(a0)	/* only necessary for pass 1 */
	dmfc2	t1, 0x0105
	sd	t2, OCTEON_CP2_AES_IV(a0)
	dmfc2	t2, 0x0106
	sd	t3, OCTEON_CP2_AES_IV+8(a0)
	dmfc2	t3, 0x0107
	sd	t0, OCTEON_CP2_AES_KEY(a0)
	dmfc2	t0, 0x0110
	sd	t1, OCTEON_CP2_AES_KEY+8(a0)
	dmfc2	t1, 0x0100
	sd	t2, OCTEON_CP2_AES_KEY+16(a0)
	dmfc2	t2, 0x0101
	sd	t3, OCTEON_CP2_AES_KEY+24(a0)
	mfc0	v0, $15,0	/* Get the processor ID register */
	sd	t0, OCTEON_CP2_AES_KEYLEN(a0)
	li	v1, 0x000d0000	/* This is the processor ID of Octeon Pass1 */
	sd	t1, OCTEON_CP2_AES_RESULT(a0)
	/* Skip to the Pass1 version of the remainder of the COP2 state */
	beq	v0, v1, 2f
	 sd	t2, OCTEON_CP2_AES_RESULT+8(a0)

	/* the non-pass1 state when !CvmCtl[NOCRYPTO] */
	dmfc2	t1, 0x0240
	dmfc2	t2, 0x0241
	ori	v1, v1, 0x9500 /* lowest OCTEON III PrId*/
	dmfc2	t3, 0x0242
	subu	v1, v0, v1 /* prid - lowest OCTEON III PrId */
	dmfc2	t0, 0x0243
	sd	t1, OCTEON_CP2_HSH_DATW(a0)
	dmfc2	t1, 0x0244
	sd	t2, OCTEON_CP2_HSH_DATW+8(a0)
	dmfc2	t2, 0x0245
	sd	t3, OCTEON_CP2_HSH_DATW+16(a0)
	dmfc2	t3, 0x0246
	sd	t0, OCTEON_CP2_HSH_DATW+24(a0)
	dmfc2	t0, 0x0247
	sd	t1, OCTEON_CP2_HSH_DATW+32(a0)
	dmfc2	t1, 0x0248
	sd	t2, OCTEON_CP2_HSH_DATW+40(a0)
	dmfc2	t2, 0x0249
	sd	t3, OCTEON_CP2_HSH_DATW+48(a0)
	dmfc2	t3, 0x024A
	sd	t0, OCTEON_CP2_HSH_DATW+56(a0)
	dmfc2	t0, 0x024B
	sd	t1, OCTEON_CP2_HSH_DATW+64(a0)
	dmfc2	t1, 0x024C
	sd	t2, OCTEON_CP2_HSH_DATW+72(a0)
	dmfc2	t2, 0x024D
	sd	t3, OCTEON_CP2_HSH_DATW+80(a0)
	dmfc2	t3, 0x024E
	sd	t0, OCTEON_CP2_HSH_DATW+88(a0)
	dmfc2	t0, 0x0250
	sd	t1, OCTEON_CP2_HSH_DATW+96(a0)
	dmfc2	t1, 0x0251
	sd	t2, OCTEON_CP2_HSH_DATW+104(a0)
	dmfc2	t2, 0x0252
	sd	t3, OCTEON_CP2_HSH_DATW+112(a0)
	dmfc2	t3, 0x0253
	sd	t0, OCTEON_CP2_HSH_IVW(a0)
	dmfc2	t0, 0x0254
	sd	t1, OCTEON_CP2_HSH_IVW+8(a0)
	dmfc2	t1, 0x0255
	sd	t2, OCTEON_CP2_HSH_IVW+16(a0)
	dmfc2	t2, 0x0256
	sd	t3, OCTEON_CP2_HSH_IVW+24(a0)
	dmfc2	t3, 0x0257
	sd	t0, OCTEON_CP2_HSH_IVW+32(a0)
	dmfc2	t0, 0x0258
	sd	t1, OCTEON_CP2_HSH_IVW+40(a0)
	dmfc2	t1, 0x0259
	sd	t2, OCTEON_CP2_HSH_IVW+48(a0)
	dmfc2	t2, 0x025E
	sd	t3, OCTEON_CP2_HSH_IVW+56(a0)
	dmfc2	t3, 0x025A
	sd	t0, OCTEON_CP2_GFM_MULT(a0)
	dmfc2	t0, 0x025B
	sd	t1, OCTEON_CP2_GFM_MULT+8(a0)
	sd	t2, OCTEON_CP2_GFM_POLY(a0)
	sd	t3, OCTEON_CP2_GFM_RESULT(a0)
	bltz	v1, 4f
	 sd	t0, OCTEON_CP2_GFM_RESULT+8(a0)
	/* OCTEON III things*/
	dmfc2	t0, 0x024F
	dmfc2	t1, 0x0050
	sd	t0, OCTEON_CP2_SHA3(a0)
	sd	t1, OCTEON_CP2_SHA3+8(a0)
4:
	jr	ra
	 nop

2:	/* pass 1 special stuff when !CvmCtl[NOCRYPTO] */
	dmfc2	t3, 0x0040
	dmfc2	t0, 0x0041
	dmfc2	t1, 0x0042
	dmfc2	t2, 0x0043
	sd	t3, OCTEON_CP2_HSH_DATW(a0)
	dmfc2	t3, 0x0044
	sd	t0, OCTEON_CP2_HSH_DATW+8(a0)
	dmfc2	t0, 0x0045
	sd	t1, OCTEON_CP2_HSH_DATW+16(a0)
	dmfc2	t1, 0x0046
	sd	t2, OCTEON_CP2_HSH_DATW+24(a0)
	dmfc2	t2, 0x0048
	sd	t3, OCTEON_CP2_HSH_DATW+32(a0)
	dmfc2	t3, 0x0049
	sd	t0, OCTEON_CP2_HSH_DATW+40(a0)
	dmfc2	t0, 0x004A
	sd	t1, OCTEON_CP2_HSH_DATW+48(a0)
	sd	t2, OCTEON_CP2_HSH_IVW(a0)
	sd	t3, OCTEON_CP2_HSH_IVW+8(a0)
	sd	t0, OCTEON_CP2_HSH_IVW+16(a0)

3:	/* pass 1 or CvmCtl[NOCRYPTO] set */
	jr	ra
	 nop
	END(octeon_cop2_save)
	.set pop

/*
 * void octeon_cop2_restore(struct octeon_cop2_state *a0)
 */
	.align	7
	.set push
	.set noreorder
	LEAF(octeon_cop2_restore)
	/* First cache line was prefetched before the call */
	pref	4,  128(a0)
	dmfc0	t9, $9,7	/* CvmCtl register. */

	pref	4,  256(a0)
	ld	t0, OCTEON_CP2_CRC_IV(a0)
	pref	4,  384(a0)
	ld	t1, OCTEON_CP2_CRC_LENGTH(a0)
	ld	t2, OCTEON_CP2_CRC_POLY(a0)

	/* Restore the COP2 CRC state */
	dmtc2	t0, 0x0201
	dmtc2	t1, 0x1202
	bbit1	t9, 28, 2f	/* Skip LLM if CvmCtl[NODFA_CP2] is set */
	 dmtc2	t2, 0x4200

	/* Restore the LLM state */
	ld	t0, OCTEON_CP2_LLM_DAT(a0)
	ld	t1, OCTEON_CP2_LLM_DAT+8(a0)
	dmtc2	t0, 0x0402
	dmtc2	t1, 0x040A

2:
	bbit1	t9, 26, done_restore	/* done if CvmCtl[NOCRYPTO] set */
	 nop

	/* Restore the COP2 crypto state common to pass 1 and pass 2 */
	ld	t0, OCTEON_CP2_3DES_IV(a0)
	ld	t1, OCTEON_CP2_3DES_KEY(a0)
	ld	t2, OCTEON_CP2_3DES_KEY+8(a0)
	dmtc2	t0, 0x0084
	ld	t0, OCTEON_CP2_3DES_KEY+16(a0)
	dmtc2	t1, 0x0080
	ld	t1, OCTEON_CP2_3DES_RESULT(a0)
	dmtc2	t2, 0x0081
	ld	t2, OCTEON_CP2_AES_INP0(a0) /* only really needed for pass 1 */
	dmtc2	t0, 0x0082
	ld	t0, OCTEON_CP2_AES_IV(a0)
	dmtc2	t1, 0x0098
	ld	t1, OCTEON_CP2_AES_IV+8(a0)
	dmtc2	t2, 0x010A		    /* only really needed for pass 1 */
	ld	t2, OCTEON_CP2_AES_KEY(a0)
	dmtc2	t0, 0x0102
	ld	t0, OCTEON_CP2_AES_KEY+8(a0)
	dmtc2	t1, 0x0103
	ld	t1, OCTEON_CP2_AES_KEY+16(a0)
	dmtc2	t2, 0x0104
	ld	t2, OCTEON_CP2_AES_KEY+24(a0)
	dmtc2	t0, 0x0105
	ld	t0, OCTEON_CP2_AES_KEYLEN(a0)
	dmtc2	t1, 0x0106
	ld	t1, OCTEON_CP2_AES_RESULT(a0)
	dmtc2	t2, 0x0107
	ld	t2, OCTEON_CP2_AES_RESULT+8(a0)
	mfc0	t3, $15,0	/* Get the processor ID register */
	dmtc2	t0, 0x0110
	li	v0, 0x000d0000	/* This is the processor ID of Octeon Pass1 */
	dmtc2	t1, 0x0100
	bne	v0, t3, 3f	/* Skip the next stuff for non-pass1 */
	 dmtc2	t2, 0x0101

	/* this code is specific for pass 1 */
	ld	t0, OCTEON_CP2_HSH_DATW(a0)
	ld	t1, OCTEON_CP2_HSH_DATW+8(a0)
	ld	t2, OCTEON_CP2_HSH_DATW+16(a0)
	dmtc2	t0, 0x0040
	ld	t0, OCTEON_CP2_HSH_DATW+24(a0)
	dmtc2	t1, 0x0041
	ld	t1, OCTEON_CP2_HSH_DATW+32(a0)
	dmtc2	t2, 0x0042
	ld	t2, OCTEON_CP2_HSH_DATW+40(a0)
	dmtc2	t0, 0x0043
	ld	t0, OCTEON_CP2_HSH_DATW+48(a0)
	dmtc2	t1, 0x0044
	ld	t1, OCTEON_CP2_HSH_IVW(a0)
	dmtc2	t2, 0x0045
	ld	t2, OCTEON_CP2_HSH_IVW+8(a0)
	dmtc2	t0, 0x0046
	ld	t0, OCTEON_CP2_HSH_IVW+16(a0)
	dmtc2	t1, 0x0048
	dmtc2	t2, 0x0049
	b done_restore	 /* unconditional branch */
	 dmtc2	t0, 0x004A

3:	/* this is post-pass1 code */
	ld	t2, OCTEON_CP2_HSH_DATW(a0)
	ori	v0, v0, 0x9500 /* lowest OCTEON III PrId*/
	ld	t0, OCTEON_CP2_HSH_DATW+8(a0)
	ld	t1, OCTEON_CP2_HSH_DATW+16(a0)
	dmtc2	t2, 0x0240
	ld	t2, OCTEON_CP2_HSH_DATW+24(a0)
	dmtc2	t0, 0x0241
	ld	t0, OCTEON_CP2_HSH_DATW+32(a0)
	dmtc2	t1, 0x0242
	ld	t1, OCTEON_CP2_HSH_DATW+40(a0)
	dmtc2	t2, 0x0243
	ld	t2, OCTEON_CP2_HSH_DATW+48(a0)
	dmtc2	t0, 0x0244
	ld	t0, OCTEON_CP2_HSH_DATW+56(a0)
	dmtc2	t1, 0x0245
	ld	t1, OCTEON_CP2_HSH_DATW+64(a0)
	dmtc2	t2, 0x0246
	ld	t2, OCTEON_CP2_HSH_DATW+72(a0)
	dmtc2	t0, 0x0247
	ld	t0, OCTEON_CP2_HSH_DATW+80(a0)
	dmtc2	t1, 0x0248
	ld	t1, OCTEON_CP2_HSH_DATW+88(a0)
	dmtc2	t2, 0x0249
	ld	t2, OCTEON_CP2_HSH_DATW+96(a0)
	dmtc2	t0, 0x024A
	ld	t0, OCTEON_CP2_HSH_DATW+104(a0)
	dmtc2	t1, 0x024B
	ld	t1, OCTEON_CP2_HSH_DATW+112(a0)
	dmtc2	t2, 0x024C
	ld	t2, OCTEON_CP2_HSH_IVW(a0)
	dmtc2	t0, 0x024D
	ld	t0, OCTEON_CP2_HSH_IVW+8(a0)
	dmtc2	t1, 0x024E
	ld	t1, OCTEON_CP2_HSH_IVW+16(a0)
	dmtc2	t2, 0x0250
	ld	t2, OCTEON_CP2_HSH_IVW+24(a0)
	dmtc2	t0, 0x0251
	ld	t0, OCTEON_CP2_HSH_IVW+32(a0)
	dmtc2	t1, 0x0252
	ld	t1, OCTEON_CP2_HSH_IVW+40(a0)
	dmtc2	t2, 0x0253
	ld	t2, OCTEON_CP2_HSH_IVW+48(a0)
	dmtc2	t0, 0x0254
	ld	t0, OCTEON_CP2_HSH_IVW+56(a0)
	dmtc2	t1, 0x0255
	ld	t1, OCTEON_CP2_GFM_MULT(a0)
	dmtc2	t2, 0x0256
	ld	t2, OCTEON_CP2_GFM_MULT+8(a0)
	dmtc2	t0, 0x0257
	ld	t0, OCTEON_CP2_GFM_POLY(a0)
	dmtc2	t1, 0x0258
	ld	t1, OCTEON_CP2_GFM_RESULT(a0)
	dmtc2	t2, 0x0259
	ld	t2, OCTEON_CP2_GFM_RESULT+8(a0)
	dmtc2	t0, 0x025E
	subu	v0, t3, v0 /* prid - lowest OCTEON III PrId */
	dmtc2	t1, 0x025A
	bltz	v0, done_restore
	 dmtc2	t2, 0x025B
	/* OCTEON III things*/
	ld	t0, OCTEON_CP2_SHA3(a0)
	ld	t1, OCTEON_CP2_SHA3+8(a0)
	dmtc2	t0, 0x0051
	dmtc2	t1, 0x0050
done_restore:
	jr	ra
	 nop
	END(octeon_cop2_restore)
	.set pop

/*
 * void octeon_mult_save()
 * sp is assumed to point to a struct pt_regs
 *
 * NOTE: This is called in SAVE_TEMP in stackframe.h. It can
 *       safely modify v1,k0, k1,$10-$15, and $24.  It will
 *	 be overwritten with a processor specific version of the code.
 */
	.p2align 7
	.set push
	.set noreorder
	LEAF(octeon_mult_save)
	jr	ra
	 nop
	.space 30 * 4, 0
	EXPORT(octeon_mult_save_end)
	END(octeon_mult_save)

	LEAF(octeon_mult_save2)
	/* Save the multiplier state OCTEON II and earlier*/
	v3mulu	k0, $0, $0
	v3mulu	k1, $0, $0
	sd	k0, PT_MTP(sp)	      /* PT_MTP	   has P0 */
	v3mulu	k0, $0, $0
	sd	k1, PT_MTP+8(sp)      /* PT_MTP+8  has P1 */
	ori	k1, $0, 1
	v3mulu	k1, k1, $0
	sd	k0, PT_MTP+16(sp)     /* PT_MTP+16 has P2 */
	v3mulu	k0, $0, $0
	sd	k1, PT_MPL(sp)	      /* PT_MPL	   has MPL0 */
	v3mulu	k1, $0, $0
	sd	k0, PT_MPL+8(sp)      /* PT_MPL+8  has MPL1 */
	jr	ra
	 sd	k1, PT_MPL+16(sp)     /* PT_MPL+16 has MPL2 */
	EXPORT(octeon_mult_save2_end)
	END(octeon_mult_save2)

	LEAF(octeon_mult_save3)
	/* Save the multiplier state OCTEON III */
	v3mulu	$10, $0, $0		/* read P0 */
	v3mulu	$11, $0, $0		/* read P1 */
	v3mulu	$12, $0, $0		/* read P2 */
	sd	$10, PT_MTP+(0*8)(sp)	/* store P0 */
	v3mulu	$10, $0, $0		/* read P3 */
	sd	$11, PT_MTP+(1*8)(sp)	/*  store P1 */
	v3mulu	$11, $0, $0		/* read P4 */
	sd	$12, PT_MTP+(2*8)(sp)	/* store P2 */
	ori	$13, $0, 1
	v3mulu	$12, $0, $0		/* read P5 */
	sd	$10, PT_MTP+(3*8)(sp)	/* store P3 */
	v3mulu	$13, $13, $0		/* P4-P0 = MPL5-MPL1, $13 = MPL0 */
	sd	$11, PT_MTP+(4*8)(sp)	/* store P4 */
	v3mulu	$10, $0, $0		/* read MPL1 */
	sd	$12, PT_MTP+(5*8)(sp)	/* store P5 */
	v3mulu	$11, $0, $0		/* read MPL2 */
	sd	$13, PT_MPL+(0*8)(sp)	/* store MPL0 */
	v3mulu	$12, $0, $0		/* read MPL3 */
	sd	$10, PT_MPL+(1*8)(sp)	/* store MPL1 */
	v3mulu	$10, $0, $0		/* read MPL4 */
	sd	$11, PT_MPL+(2*8)(sp)	/* store MPL2 */
	v3mulu	$11, $0, $0		/* read MPL5 */
	sd	$12, PT_MPL+(3*8)(sp)	/* store MPL3 */
	sd	$10, PT_MPL+(4*8)(sp)	/* store MPL4 */
	jr	ra
	 sd	$11, PT_MPL+(5*8)(sp)	/* store MPL5 */
	EXPORT(octeon_mult_save3_end)
	END(octeon_mult_save3)
	.set pop

/*
 * void octeon_mult_restore()
 * sp is assumed to point to a struct pt_regs
 *
 * NOTE: This is called in RESTORE_TEMP in stackframe.h.
 */
	.p2align 7
	.set push
	.set noreorder
	LEAF(octeon_mult_restore)
	jr	ra
	 nop
	.space 30 * 4, 0
	EXPORT(octeon_mult_restore_end)
	END(octeon_mult_restore)

	LEAF(octeon_mult_restore2)
	ld	v0, PT_MPL(sp)        	/* MPL0 */
	ld	v1, PT_MPL+8(sp)      	/* MPL1 */
	ld	k0, PT_MPL+16(sp)     	/* MPL2 */
	/* Restore the multiplier state */
	ld	k1, PT_MTP+16(sp)     	/* P2 */
	mtm0	v0			/* MPL0 */
	ld	v0, PT_MTP+8(sp)	/* P1 */
	mtm1	v1			/* MPL1 */
	ld	v1, PT_MTP(sp)   	/* P0 */
	mtm2	k0			/* MPL2 */
	mtp2	k1			/* P2 */
	mtp1	v0			/* P1 */
	jr	ra
	 mtp0	v1			/* P0 */
	EXPORT(octeon_mult_restore2_end)
	END(octeon_mult_restore2)

	LEAF(octeon_mult_restore3)
	ld	$12, PT_MPL+(0*8)(sp)	/* read MPL0 */
	ld	$13, PT_MPL+(3*8)(sp)	/* read MPL3 */
	ld	$10, PT_MPL+(1*8)(sp)	/* read MPL1 */
	ld	$11, PT_MPL+(4*8)(sp)	/* read MPL4 */
	.word	0x718d0008
	/* mtm0	$12, $13		   restore MPL0 and MPL3 */
	ld	$12, PT_MPL+(2*8)(sp)	/* read MPL2 */
	.word	0x714b000c
	/* mtm1	$10, $11		   restore MPL1 and MPL4 */
	ld	$13, PT_MPL+(5*8)(sp)	/* read MPL5 */
	ld	$10, PT_MTP+(0*8)(sp)	/* read P0 */
	ld	$11, PT_MTP+(3*8)(sp)	/* read P3 */
	.word	0x718d000d
	/* mtm2	$12, $13		   restore MPL2 and MPL5 */
	ld	$12, PT_MTP+(1*8)(sp)	/* read P1 */
	.word	0x714b0009
	/* mtp0	$10, $11		   restore P0 and P3 */
	ld	$13, PT_MTP+(4*8)(sp)	/* read P4 */
	ld	$10, PT_MTP+(2*8)(sp)	/* read P2 */
	ld	$11, PT_MTP+(5*8)(sp)	/* read P5 */
	.word	0x718d000a
	/* mtp1	$12, $13		   restore P1 and P4 */
	jr	ra
	.word	0x714b000b
	/* mtp2	$10, $11		   restore P2 and P5 */

	EXPORT(octeon_mult_restore3_end)
	END(octeon_mult_restore3)
	.set pop
