/* 32 and 64-bit millicode, original author Hewlett-Packard
   adapted for gcc by Paul Bame <bame@debian.org>
   and Alan Modra <alan@linuxcare.com.au>.

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of GCC and is released under the terms of
   of the GNU General Public License as published by the Free Software
   Foundation; either version 2, or (at your option) any later version.
   See the file COPYING in the top-level GCC source directory for a copy
   of the license.  */

#ifndef _PA_MILLI_H_
#define _PA_MILLI_H_

#define L_dyncall
#define L_divI
#define L_divU
#define L_remI
#define L_remU
#define L_div_const
#define L_mulI

#ifdef CONFIG_64BIT
        .level  2.0w
#endif

/* Hardware General Registers.  */
r0:	.reg	%r0
r1:	.reg	%r1
r2:	.reg	%r2
r3:	.reg	%r3
r4:	.reg	%r4
r5:	.reg	%r5
r6:	.reg	%r6
r7:	.reg	%r7
r8:	.reg	%r8
r9:	.reg	%r9
r10:	.reg	%r10
r11:	.reg	%r11
r12:	.reg	%r12
r13:	.reg	%r13
r14:	.reg	%r14
r15:	.reg	%r15
r16:	.reg	%r16
r17:	.reg	%r17
r18:	.reg	%r18
r19:	.reg	%r19
r20:	.reg	%r20
r21:	.reg	%r21
r22:	.reg	%r22
r23:	.reg	%r23
r24:	.reg	%r24
r25:	.reg	%r25
r26:	.reg	%r26
r27:	.reg	%r27
r28:	.reg	%r28
r29:	.reg	%r29
r30:	.reg	%r30
r31:	.reg	%r31

/* Hardware Space Registers.  */
sr0:	.reg	%sr0
sr1:	.reg	%sr1
sr2:	.reg	%sr2
sr3:	.reg	%sr3
sr4:	.reg	%sr4
sr5:	.reg	%sr5
sr6:	.reg	%sr6
sr7:	.reg	%sr7

/* Hardware Floating Point Registers.  */
fr0:	.reg	%fr0
fr1:	.reg	%fr1
fr2:	.reg	%fr2
fr3:	.reg	%fr3
fr4:	.reg	%fr4
fr5:	.reg	%fr5
fr6:	.reg	%fr6
fr7:	.reg	%fr7
fr8:	.reg	%fr8
fr9:	.reg	%fr9
fr10:	.reg	%fr10
fr11:	.reg	%fr11
fr12:	.reg	%fr12
fr13:	.reg	%fr13
fr14:	.reg	%fr14
fr15:	.reg	%fr15

/* Hardware Control Registers.  */
cr11:	.reg	%cr11
sar:	.reg	%cr11	/* Shift Amount Register */

/* Software Architecture General Registers.  */
rp:	.reg    r2	/* return pointer */
#ifdef CONFIG_64BIT
mrp:	.reg	r2 	/* millicode return pointer */
#else
mrp:	.reg	r31	/* millicode return pointer */
#endif
ret0:	.reg    r28	/* return value */
ret1:	.reg    r29	/* return value (high part of double) */
sp:	.reg 	r30	/* stack pointer */
dp:	.reg	r27	/* data pointer */
arg0:	.reg	r26	/* argument */
arg1:	.reg	r25	/* argument or high part of double argument */
arg2:	.reg	r24	/* argument */
arg3:	.reg	r23	/* argument or high part of double argument */

/* Software Architecture Space Registers.  */
/* 		sr0	; return link from BLE */
sret:	.reg	sr1	/* return value */
sarg:	.reg	sr1	/* argument */
/* 		sr4	; PC SPACE tracker */
/* 		sr5	; process private data */

/* Frame Offsets (millicode convention!)  Used when calling other
   millicode routines.  Stack unwinding is dependent upon these
   definitions.  */
r31_slot:	.equ	-20	/* "current RP" slot */
sr0_slot:	.equ	-16     /* "static link" slot */
#if defined(CONFIG_64BIT)
mrp_slot:       .equ    -16	/* "current RP" slot */
psp_slot:       .equ    -8	/* "previous SP" slot */
#else
mrp_slot:	.equ	-20     /* "current RP" slot (replacing "r31_slot") */
#endif


#define DEFINE(name,value)name:	.EQU	value
#define RDEFINE(name,value)name:	.REG	value
#ifdef milliext
#define MILLI_BE(lbl)   BE    lbl(sr7,r0)
#define MILLI_BEN(lbl)  BE,n  lbl(sr7,r0)
#define MILLI_BLE(lbl)	BLE   lbl(sr7,r0)
#define MILLI_BLEN(lbl)	BLE,n lbl(sr7,r0)
#define MILLIRETN	BE,n  0(sr0,mrp)
#define MILLIRET	BE    0(sr0,mrp)
#define MILLI_RETN	BE,n  0(sr0,mrp)
#define MILLI_RET	BE    0(sr0,mrp)
#else
#define MILLI_BE(lbl)	B     lbl
#define MILLI_BEN(lbl)  B,n   lbl
#define MILLI_BLE(lbl)	BL    lbl,mrp
#define MILLI_BLEN(lbl)	BL,n  lbl,mrp
#define MILLIRETN	BV,n  0(mrp)
#define MILLIRET	BV    0(mrp)
#define MILLI_RETN	BV,n  0(mrp)
#define MILLI_RET	BV    0(mrp)
#endif

#define CAT(a,b)	a##b

#define SUBSPA_MILLI	 .section .text
#define SUBSPA_MILLI_DIV .section .text.div,"ax",@progbits! .align 16
#define SUBSPA_MILLI_MUL .section .text.mul,"ax",@progbits! .align 16
#define ATTR_MILLI
#define SUBSPA_DATA	 .section .data
#define ATTR_DATA
#define GLOBAL		 $global$
#define GSYM(sym) 	 !sym:
#define LSYM(sym)	 !CAT(.L,sym:)
#define LREF(sym)	 CAT(.L,sym)

#endif /*_PA_MILLI_H_*/
