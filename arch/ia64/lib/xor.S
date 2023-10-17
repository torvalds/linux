/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/ia64/lib/xor.S
 *
 * Optimized RAID-5 checksumming functions for IA-64.
 */

#include <linux/export.h>
#include <asm/asmmacro.h>

GLOBAL_ENTRY(xor_ia64_2)
	.prologue
	.fframe 0
	.save ar.pfs, r31
	alloc r31 = ar.pfs, 3, 0, 13, 16
	.save ar.lc, r30
	mov r30 = ar.lc
	.save pr, r29
	mov r29 = pr
	;;
	.body
	mov r8 = in1
	mov ar.ec = 6 + 2
	shr in0 = in0, 3
	;;
	adds in0 = -1, in0
	mov r16 = in1
	mov r17 = in2
	;;
	mov ar.lc = in0
	mov pr.rot = 1 << 16
	;;
	.rotr s1[6+1], s2[6+1], d[2]
	.rotp p[6+2]
0:
(p[0])	ld8.nta s1[0] = [r16], 8
(p[0])	ld8.nta s2[0] = [r17], 8
(p[6])	xor d[0] = s1[6], s2[6]
(p[6+1])st8.nta [r8] = d[1], 8
	nop.f 0
	br.ctop.dptk.few 0b
	;;
	mov ar.lc = r30
	mov pr = r29, -1
	br.ret.sptk.few rp
END(xor_ia64_2)
EXPORT_SYMBOL(xor_ia64_2)

GLOBAL_ENTRY(xor_ia64_3)
	.prologue
	.fframe 0
	.save ar.pfs, r31
	alloc r31 = ar.pfs, 4, 0, 20, 24
	.save ar.lc, r30
	mov r30 = ar.lc
	.save pr, r29
	mov r29 = pr
	;;
	.body
	mov r8 = in1
	mov ar.ec = 6 + 2
	shr in0 = in0, 3
	;;
	adds in0 = -1, in0
	mov r16 = in1
	mov r17 = in2
	;;
	mov r18 = in3
	mov ar.lc = in0
	mov pr.rot = 1 << 16
	;;
	.rotr s1[6+1], s2[6+1], s3[6+1], d[2]
	.rotp p[6+2]
0:
(p[0])	ld8.nta s1[0] = [r16], 8
(p[0])	ld8.nta s2[0] = [r17], 8
(p[6])	xor d[0] = s1[6], s2[6]
	;;
(p[0])	ld8.nta s3[0] = [r18], 8
(p[6+1])st8.nta [r8] = d[1], 8
(p[6])	xor d[0] = d[0], s3[6]
	br.ctop.dptk.few 0b
	;;
	mov ar.lc = r30
	mov pr = r29, -1
	br.ret.sptk.few rp
END(xor_ia64_3)
EXPORT_SYMBOL(xor_ia64_3)

GLOBAL_ENTRY(xor_ia64_4)
	.prologue
	.fframe 0
	.save ar.pfs, r31
	alloc r31 = ar.pfs, 5, 0, 27, 32
	.save ar.lc, r30
	mov r30 = ar.lc
	.save pr, r29
	mov r29 = pr
	;;
	.body
	mov r8 = in1
	mov ar.ec = 6 + 2
	shr in0 = in0, 3
	;;
	adds in0 = -1, in0
	mov r16 = in1
	mov r17 = in2
	;;
	mov r18 = in3
	mov ar.lc = in0
	mov pr.rot = 1 << 16
	mov r19 = in4
	;;
	.rotr s1[6+1], s2[6+1], s3[6+1], s4[6+1], d[2]
	.rotp p[6+2]
0:
(p[0])	ld8.nta s1[0] = [r16], 8
(p[0])	ld8.nta s2[0] = [r17], 8
(p[6])	xor d[0] = s1[6], s2[6]
(p[0])	ld8.nta s3[0] = [r18], 8
(p[0])	ld8.nta s4[0] = [r19], 8
(p[6])	xor r20 = s3[6], s4[6]
	;;
(p[6+1])st8.nta [r8] = d[1], 8
(p[6])	xor d[0] = d[0], r20
	br.ctop.dptk.few 0b
	;;
	mov ar.lc = r30
	mov pr = r29, -1
	br.ret.sptk.few rp
END(xor_ia64_4)
EXPORT_SYMBOL(xor_ia64_4)

GLOBAL_ENTRY(xor_ia64_5)
	.prologue
	.fframe 0
	.save ar.pfs, r31
	alloc r31 = ar.pfs, 6, 0, 34, 40
	.save ar.lc, r30
	mov r30 = ar.lc
	.save pr, r29
	mov r29 = pr
	;;
	.body
	mov r8 = in1
	mov ar.ec = 6 + 2
	shr in0 = in0, 3
	;;
	adds in0 = -1, in0
	mov r16 = in1
	mov r17 = in2
	;;
	mov r18 = in3
	mov ar.lc = in0
	mov pr.rot = 1 << 16
	mov r19 = in4
	mov r20 = in5
	;;
	.rotr s1[6+1], s2[6+1], s3[6+1], s4[6+1], s5[6+1], d[2]
	.rotp p[6+2]
0:
(p[0])	ld8.nta s1[0] = [r16], 8
(p[0])	ld8.nta s2[0] = [r17], 8
(p[6])	xor d[0] = s1[6], s2[6]
(p[0])	ld8.nta s3[0] = [r18], 8
(p[0])	ld8.nta s4[0] = [r19], 8
(p[6])	xor r21 = s3[6], s4[6]
	;;
(p[0])	ld8.nta s5[0] = [r20], 8
(p[6+1])st8.nta [r8] = d[1], 8
(p[6])	xor d[0] = d[0], r21
	;;
(p[6])	  xor d[0] = d[0], s5[6]
	nop.f 0
	br.ctop.dptk.few 0b
	;;
	mov ar.lc = r30
	mov pr = r29, -1
	br.ret.sptk.few rp
END(xor_ia64_5)
EXPORT_SYMBOL(xor_ia64_5)
