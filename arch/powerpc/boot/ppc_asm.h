/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PPC64_PPC_ASM_H
#define _PPC64_PPC_ASM_H
/*
 *
 * Definitions used by various bits of low-level assembly code on PowerPC.
 *
 * Copyright (C) 1995-1999 Gary Thomas, Paul Mackerras, Cort Dougan.
 */

/* Condition Register Bit Fields */

#define	cr0	0
#define	cr1	1
#define	cr2	2
#define	cr3	3
#define	cr4	4
#define	cr5	5
#define	cr6	6
#define	cr7	7


/* General Purpose Registers (GPRs) */

#define	r0	0
#define	r1	1
#define	r2	2
#define	r3	3
#define	r4	4
#define	r5	5
#define	r6	6
#define	r7	7
#define	r8	8
#define	r9	9
#define	r10	10
#define	r11	11
#define	r12	12
#define	r13	13
#define	r14	14
#define	r15	15
#define	r16	16
#define	r17	17
#define	r18	18
#define	r19	19
#define	r20	20
#define	r21	21
#define	r22	22
#define	r23	23
#define	r24	24
#define	r25	25
#define	r26	26
#define	r27	27
#define	r28	28
#define	r29	29
#define	r30	30
#define	r31	31

#define SPRN_TBRL	268
#define SPRN_TBRU	269
#define SPRN_HSRR0	0x13A	/* Hypervisor Save/Restore 0 */
#define SPRN_HSRR1	0x13B	/* Hypervisor Save/Restore 1 */

#define MSR_LE		0x0000000000000001

#define FIXUP_ENDIAN						   \
	tdi   0,0,0x48;	  /* Reverse endian of b . + 8		*/ \
	b     $+44;	  /* Skip trampoline if endian is good	*/ \
	.long 0xa600607d; /* mfmsr r11				*/ \
	.long 0x01006b69; /* xori r11,r11,1			*/ \
	.long 0x00004039; /* li r10,0				*/ \
	.long 0x6401417d; /* mtmsrd r10,1			*/ \
	.long 0x05009f42; /* bcl 20,31,$+4			*/ \
	.long 0xa602487d; /* mflr r10				*/ \
	.long 0x14004a39; /* addi r10,r10,20			*/ \
	.long 0xa6035a7d; /* mtsrr0 r10				*/ \
	.long 0xa6037b7d; /* mtsrr1 r11				*/ \
	.long 0x2400004c  /* rfid				*/

#ifdef CONFIG_PPC_8xx
#define MFTBL(dest)			mftb dest
#define MFTBU(dest)			mftbu dest
#else
#define MFTBL(dest)			mfspr dest, SPRN_TBRL
#define MFTBU(dest)			mfspr dest, SPRN_TBRU
#endif

#endif /* _PPC64_PPC_ASM_H */
