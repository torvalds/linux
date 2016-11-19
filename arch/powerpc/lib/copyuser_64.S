/*
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/export.h>

#ifdef __BIG_ENDIAN__
#define sLd sld		/* Shift towards low-numbered address. */
#define sHd srd		/* Shift towards high-numbered address. */
#else
#define sLd srd		/* Shift towards low-numbered address. */
#define sHd sld		/* Shift towards high-numbered address. */
#endif

	.align	7
_GLOBAL_TOC(__copy_tofrom_user)
BEGIN_FTR_SECTION
	nop
FTR_SECTION_ELSE
	b	__copy_tofrom_user_power7
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_VMX_COPY)
_GLOBAL(__copy_tofrom_user_base)
	/* first check for a whole page copy on a page boundary */
	cmpldi	cr1,r5,16
	cmpdi	cr6,r5,4096
	or	r0,r3,r4
	neg	r6,r3		/* LS 3 bits = # bytes to 8-byte dest bdry */
	andi.	r0,r0,4095
	std	r3,-24(r1)
	crand	cr0*4+2,cr0*4+2,cr6*4+2
	std	r4,-16(r1)
	std	r5,-8(r1)
	dcbt	0,r4
	beq	.Lcopy_page_4K
	andi.	r6,r6,7
	PPC_MTOCRF(0x01,r5)
	blt	cr1,.Lshort_copy
/* Below we want to nop out the bne if we're on a CPU that has the
 * CPU_FTR_UNALIGNED_LD_STD bit set and the CPU_FTR_CP_USE_DCBTZ bit
 * cleared.
 * At the time of writing the only CPU that has this combination of bits
 * set is Power6.
 */
BEGIN_FTR_SECTION
	nop
FTR_SECTION_ELSE
	bne	.Ldst_unaligned
ALT_FTR_SECTION_END(CPU_FTR_UNALIGNED_LD_STD | CPU_FTR_CP_USE_DCBTZ, \
		    CPU_FTR_UNALIGNED_LD_STD)
.Ldst_aligned:
	addi	r3,r3,-16
BEGIN_FTR_SECTION
	andi.	r0,r4,7
	bne	.Lsrc_unaligned
END_FTR_SECTION_IFCLR(CPU_FTR_UNALIGNED_LD_STD)
	blt	cr1,.Ldo_tail		/* if < 16 bytes to copy */
	srdi	r0,r5,5
	cmpdi	cr1,r0,0
20:	ld	r7,0(r4)
220:	ld	r6,8(r4)
	addi	r4,r4,16
	mtctr	r0
	andi.	r0,r5,0x10
	beq	22f
	addi	r3,r3,16
	addi	r4,r4,-16
	mr	r9,r7
	mr	r8,r6
	beq	cr1,72f
21:	ld	r7,16(r4)
221:	ld	r6,24(r4)
	addi	r4,r4,32
70:	std	r9,0(r3)
270:	std	r8,8(r3)
22:	ld	r9,0(r4)
222:	ld	r8,8(r4)
71:	std	r7,16(r3)
271:	std	r6,24(r3)
	addi	r3,r3,32
	bdnz	21b
72:	std	r9,0(r3)
272:	std	r8,8(r3)
	andi.	r5,r5,0xf
	beq+	3f
	addi	r4,r4,16
.Ldo_tail:
	addi	r3,r3,16
	bf	cr7*4+0,246f
244:	ld	r9,0(r4)
	addi	r4,r4,8
245:	std	r9,0(r3)
	addi	r3,r3,8
246:	bf	cr7*4+1,1f
23:	lwz	r9,0(r4)
	addi	r4,r4,4
73:	stw	r9,0(r3)
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
44:	lhz	r9,0(r4)
	addi	r4,r4,2
74:	sth	r9,0(r3)
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
45:	lbz	r9,0(r4)
75:	stb	r9,0(r3)
3:	li	r3,0
	blr

.Lsrc_unaligned:
	srdi	r6,r5,3
	addi	r5,r5,-16
	subf	r4,r0,r4
	srdi	r7,r5,4
	sldi	r10,r0,3
	cmpldi	cr6,r6,3
	andi.	r5,r5,7
	mtctr	r7
	subfic	r11,r10,64
	add	r5,r5,r0
	bt	cr7*4+0,28f

24:	ld	r9,0(r4)	/* 3+2n loads, 2+2n stores */
25:	ld	r0,8(r4)
	sLd	r6,r9,r10
26:	ldu	r9,16(r4)
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	or	r7,r7,r6
	blt	cr6,79f
27:	ld	r0,8(r4)
	b	2f

28:	ld	r0,0(r4)	/* 4+2n loads, 3+2n stores */
29:	ldu	r9,8(r4)
	sLd	r8,r0,r10
	addi	r3,r3,-8
	blt	cr6,5f
30:	ld	r0,8(r4)
	sHd	r12,r9,r11
	sLd	r6,r9,r10
31:	ldu	r9,16(r4)
	or	r12,r8,r12
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	addi	r3,r3,16
	beq	cr6,78f

1:	or	r7,r7,r6
32:	ld	r0,8(r4)
76:	std	r12,8(r3)
2:	sHd	r12,r9,r11
	sLd	r6,r9,r10
33:	ldu	r9,16(r4)
	or	r12,r8,r12
77:	stdu	r7,16(r3)
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	bdnz	1b

78:	std	r12,8(r3)
	or	r7,r7,r6
79:	std	r7,16(r3)
5:	sHd	r12,r9,r11
	or	r12,r8,r12
80:	std	r12,24(r3)
	bne	6f
	li	r3,0
	blr
6:	cmpwi	cr1,r5,8
	addi	r3,r3,32
	sLd	r9,r9,r10
	ble	cr1,7f
34:	ld	r0,8(r4)
	sHd	r7,r0,r11
	or	r9,r7,r9
7:
	bf	cr7*4+1,1f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,32
#endif
94:	stw	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,32
#endif
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,16
#endif
95:	sth	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,16
#endif
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,8
#endif
96:	stb	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,8
#endif
3:	li	r3,0
	blr

.Ldst_unaligned:
	PPC_MTOCRF(0x01,r6)		/* put #bytes to 8B bdry into cr7 */
	subf	r5,r6,r5
	li	r7,0
	cmpldi	cr1,r5,16
	bf	cr7*4+3,1f
35:	lbz	r0,0(r4)
81:	stb	r0,0(r3)
	addi	r7,r7,1
1:	bf	cr7*4+2,2f
36:	lhzx	r0,r7,r4
82:	sthx	r0,r7,r3
	addi	r7,r7,2
2:	bf	cr7*4+1,3f
37:	lwzx	r0,r7,r4
83:	stwx	r0,r7,r3
3:	PPC_MTOCRF(0x01,r5)
	add	r4,r6,r4
	add	r3,r6,r3
	b	.Ldst_aligned

.Lshort_copy:
	bf	cr7*4+0,1f
38:	lwz	r0,0(r4)
39:	lwz	r9,4(r4)
	addi	r4,r4,8
84:	stw	r0,0(r3)
85:	stw	r9,4(r3)
	addi	r3,r3,8
1:	bf	cr7*4+1,2f
40:	lwz	r0,0(r4)
	addi	r4,r4,4
86:	stw	r0,0(r3)
	addi	r3,r3,4
2:	bf	cr7*4+2,3f
41:	lhz	r0,0(r4)
	addi	r4,r4,2
87:	sth	r0,0(r3)
	addi	r3,r3,2
3:	bf	cr7*4+3,4f
42:	lbz	r0,0(r4)
88:	stb	r0,0(r3)
4:	li	r3,0
	blr

/*
 * exception handlers follow
 * we have to return the number of bytes not copied
 * for an exception on a load, we set the rest of the destination to 0
 */

136:
137:
	add	r3,r3,r7
	b	1f
130:
131:
	addi	r3,r3,8
120:
320:
122:
322:
124:
125:
126:
127:
128:
129:
133:
	addi	r3,r3,8
132:
	addi	r3,r3,8
121:
321:
344:
134:
135:
138:
139:
140:
141:
142:
123:
144:
145:

/*
 * here we have had a fault on a load and r3 points to the first
 * unmodified byte of the destination
 */
1:	ld	r6,-24(r1)
	ld	r4,-16(r1)
	ld	r5,-8(r1)
	subf	r6,r6,r3
	add	r4,r4,r6
	subf	r5,r6,r5	/* #bytes left to go */

/*
 * first see if we can copy any more bytes before hitting another exception
 */
	mtctr	r5
43:	lbz	r0,0(r4)
	addi	r4,r4,1
89:	stb	r0,0(r3)
	addi	r3,r3,1
	bdnz	43b
	li	r3,0		/* huh? all copied successfully this time? */
	blr

/*
 * here we have trapped again, need to clear ctr bytes starting at r3
 */
143:	mfctr	r5
	li	r0,0
	mr	r4,r3
	mr	r3,r5		/* return the number of bytes not copied */
1:	andi.	r9,r4,7
	beq	3f
90:	stb	r0,0(r4)
	addic.	r5,r5,-1
	addi	r4,r4,1
	bne	1b
	blr
3:	cmpldi	cr1,r5,8
	srdi	r9,r5,3
	andi.	r5,r5,7
	blt	cr1,93f
	mtctr	r9
91:	std	r0,0(r4)
	addi	r4,r4,8
	bdnz	91b
93:	beqlr
	mtctr	r5	
92:	stb	r0,0(r4)
	addi	r4,r4,1
	bdnz	92b
	blr

/*
 * exception handlers for stores: we just need to work
 * out how many bytes weren't copied
 */
182:
183:
	add	r3,r3,r7
	b	1f
371:
180:
	addi	r3,r3,8
171:
177:
179:
	addi	r3,r3,8
370:
372:
176:
178:
	addi	r3,r3,4
185:
	addi	r3,r3,4
170:
172:
345:
173:
174:
175:
181:
184:
186:
187:
188:
189:	
194:
195:
196:
1:
	ld	r6,-24(r1)
	ld	r5,-8(r1)
	add	r6,r6,r5
	subf	r3,r3,r6	/* #bytes not copied */
190:
191:
192:
	blr			/* #bytes not copied in r3 */

	.section __ex_table,"a"
	.align	3
	.llong	20b,120b
	.llong	220b,320b
	.llong	21b,121b
	.llong	221b,321b
	.llong	70b,170b
	.llong	270b,370b
	.llong	22b,122b
	.llong	222b,322b
	.llong	71b,171b
	.llong	271b,371b
	.llong	72b,172b
	.llong	272b,372b
	.llong	244b,344b
	.llong	245b,345b
	.llong	23b,123b
	.llong	73b,173b
	.llong	44b,144b
	.llong	74b,174b
	.llong	45b,145b
	.llong	75b,175b
	.llong	24b,124b
	.llong	25b,125b
	.llong	26b,126b
	.llong	27b,127b
	.llong	28b,128b
	.llong	29b,129b
	.llong	30b,130b
	.llong	31b,131b
	.llong	32b,132b
	.llong	76b,176b
	.llong	33b,133b
	.llong	77b,177b
	.llong	78b,178b
	.llong	79b,179b
	.llong	80b,180b
	.llong	34b,134b
	.llong	94b,194b
	.llong	95b,195b
	.llong	96b,196b
	.llong	35b,135b
	.llong	81b,181b
	.llong	36b,136b
	.llong	82b,182b
	.llong	37b,137b
	.llong	83b,183b
	.llong	38b,138b
	.llong	39b,139b
	.llong	84b,184b
	.llong	85b,185b
	.llong	40b,140b
	.llong	86b,186b
	.llong	41b,141b
	.llong	87b,187b
	.llong	42b,142b
	.llong	88b,188b
	.llong	43b,143b
	.llong	89b,189b
	.llong	90b,190b
	.llong	91b,191b
	.llong	92b,192b
	
	.text

/*
 * Routine to copy a whole page of data, optimized for POWER4.
 * On POWER4 it is more than 50% faster than the simple loop
 * above (following the .Ldst_aligned label).
 */
.Lcopy_page_4K:
	std	r31,-32(1)
	std	r30,-40(1)
	std	r29,-48(1)
	std	r28,-56(1)
	std	r27,-64(1)
	std	r26,-72(1)
	std	r25,-80(1)
	std	r24,-88(1)
	std	r23,-96(1)
	std	r22,-104(1)
	std	r21,-112(1)
	std	r20,-120(1)
	li	r5,4096/32 - 1
	addi	r3,r3,-8
	li	r0,5
0:	addi	r5,r5,-24
	mtctr	r0
20:	ld	r22,640(4)
21:	ld	r21,512(4)
22:	ld	r20,384(4)
23:	ld	r11,256(4)
24:	ld	r9,128(4)
25:	ld	r7,0(4)
26:	ld	r25,648(4)
27:	ld	r24,520(4)
28:	ld	r23,392(4)
29:	ld	r10,264(4)
30:	ld	r8,136(4)
31:	ldu	r6,8(4)
	cmpwi	r5,24
1:
32:	std	r22,648(3)
33:	std	r21,520(3)
34:	std	r20,392(3)
35:	std	r11,264(3)
36:	std	r9,136(3)
37:	std	r7,8(3)
38:	ld	r28,648(4)
39:	ld	r27,520(4)
40:	ld	r26,392(4)
41:	ld	r31,264(4)
42:	ld	r30,136(4)
43:	ld	r29,8(4)
44:	std	r25,656(3)
45:	std	r24,528(3)
46:	std	r23,400(3)
47:	std	r10,272(3)
48:	std	r8,144(3)
49:	std	r6,16(3)
50:	ld	r22,656(4)
51:	ld	r21,528(4)
52:	ld	r20,400(4)
53:	ld	r11,272(4)
54:	ld	r9,144(4)
55:	ld	r7,16(4)
56:	std	r28,664(3)
57:	std	r27,536(3)
58:	std	r26,408(3)
59:	std	r31,280(3)
60:	std	r30,152(3)
61:	stdu	r29,24(3)
62:	ld	r25,664(4)
63:	ld	r24,536(4)
64:	ld	r23,408(4)
65:	ld	r10,280(4)
66:	ld	r8,152(4)
67:	ldu	r6,24(4)
	bdnz	1b
68:	std	r22,648(3)
69:	std	r21,520(3)
70:	std	r20,392(3)
71:	std	r11,264(3)
72:	std	r9,136(3)
73:	std	r7,8(3)
74:	addi	r4,r4,640
75:	addi	r3,r3,648
	bge	0b
	mtctr	r5
76:	ld	r7,0(4)
77:	ld	r8,8(4)
78:	ldu	r9,16(4)
3:
79:	ld	r10,8(4)
80:	std	r7,8(3)
81:	ld	r7,16(4)
82:	std	r8,16(3)
83:	ld	r8,24(4)
84:	std	r9,24(3)
85:	ldu	r9,32(4)
86:	stdu	r10,32(3)
	bdnz	3b
4:
87:	ld	r10,8(4)
88:	std	r7,8(3)
89:	std	r8,16(3)
90:	std	r9,24(3)
91:	std	r10,32(3)
9:	ld	r20,-120(1)
	ld	r21,-112(1)
	ld	r22,-104(1)
	ld	r23,-96(1)
	ld	r24,-88(1)
	ld	r25,-80(1)
	ld	r26,-72(1)
	ld	r27,-64(1)
	ld	r28,-56(1)
	ld	r29,-48(1)
	ld	r30,-40(1)
	ld	r31,-32(1)
	li	r3,0
	blr

/*
 * on an exception, reset to the beginning and jump back into the
 * standard __copy_tofrom_user
 */
100:	ld	r20,-120(1)
	ld	r21,-112(1)
	ld	r22,-104(1)
	ld	r23,-96(1)
	ld	r24,-88(1)
	ld	r25,-80(1)
	ld	r26,-72(1)
	ld	r27,-64(1)
	ld	r28,-56(1)
	ld	r29,-48(1)
	ld	r30,-40(1)
	ld	r31,-32(1)
	ld	r3,-24(r1)
	ld	r4,-16(r1)
	li	r5,4096
	b	.Ldst_aligned

	.section __ex_table,"a"
	.align	3
	.llong	20b,100b
	.llong	21b,100b
	.llong	22b,100b
	.llong	23b,100b
	.llong	24b,100b
	.llong	25b,100b
	.llong	26b,100b
	.llong	27b,100b
	.llong	28b,100b
	.llong	29b,100b
	.llong	30b,100b
	.llong	31b,100b
	.llong	32b,100b
	.llong	33b,100b
	.llong	34b,100b
	.llong	35b,100b
	.llong	36b,100b
	.llong	37b,100b
	.llong	38b,100b
	.llong	39b,100b
	.llong	40b,100b
	.llong	41b,100b
	.llong	42b,100b
	.llong	43b,100b
	.llong	44b,100b
	.llong	45b,100b
	.llong	46b,100b
	.llong	47b,100b
	.llong	48b,100b
	.llong	49b,100b
	.llong	50b,100b
	.llong	51b,100b
	.llong	52b,100b
	.llong	53b,100b
	.llong	54b,100b
	.llong	55b,100b
	.llong	56b,100b
	.llong	57b,100b
	.llong	58b,100b
	.llong	59b,100b
	.llong	60b,100b
	.llong	61b,100b
	.llong	62b,100b
	.llong	63b,100b
	.llong	64b,100b
	.llong	65b,100b
	.llong	66b,100b
	.llong	67b,100b
	.llong	68b,100b
	.llong	69b,100b
	.llong	70b,100b
	.llong	71b,100b
	.llong	72b,100b
	.llong	73b,100b
	.llong	74b,100b
	.llong	75b,100b
	.llong	76b,100b
	.llong	77b,100b
	.llong	78b,100b
	.llong	79b,100b
	.llong	80b,100b
	.llong	81b,100b
	.llong	82b,100b
	.llong	83b,100b
	.llong	84b,100b
	.llong	85b,100b
	.llong	86b,100b
	.llong	87b,100b
	.llong	88b,100b
	.llong	89b,100b
	.llong	90b,100b
	.llong	91b,100b
EXPORT_SYMBOL(__copy_tofrom_user)
