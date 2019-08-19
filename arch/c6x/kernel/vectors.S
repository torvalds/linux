; SPDX-License-Identifier: GPL-2.0-only
;
;  Port on Texas Instruments TMS320C6x architecture
;
;  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
;  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
;
;  This section handles all the interrupt vector routines.
;  At RESET the processor sets up the DRAM timing parameters and
;  branches to the label _c_int00 which handles initialization for the C code.
;

#define ALIGNMENT 5

	.macro IRQVEC name, handler
	.align ALIGNMENT
	.hidden \name
	.global \name
\name:
#ifdef CONFIG_C6X_BIG_KERNEL
	STW	.D2T1	A0,*B15--[2]
 ||	MVKL	.S1	\handler,A0
	MVKH	.S1	\handler,A0
	B	.S2X	A0
	LDW	.D2T1	*++B15[2],A0
	NOP	4
	NOP
	NOP
	.endm
#else /* CONFIG_C6X_BIG_KERNEL */
	B	.S2	\handler
	NOP
	NOP
	NOP
	NOP
	NOP
	NOP
	NOP
	.endm
#endif /* CONFIG_C6X_BIG_KERNEL */

	   .sect ".vectors","ax"
	   .align ALIGNMENT
	   .global RESET
	   .hidden RESET
RESET:
#ifdef CONFIG_C6X_BIG_KERNEL
	   MVKL	.S1	_c_int00,A0		; branch to _c_int00
	   MVKH	.S1	_c_int00,A0
	   B	.S2X	A0
#else
	   B	.S2	_c_int00
	   NOP
	   NOP
#endif
	   NOP
	   NOP
	   NOP
	   NOP
	   NOP


	   IRQVEC NMI,_nmi_handler		; NMI interrupt
	   IRQVEC AINT,_bad_interrupt		; reserved
	   IRQVEC MSGINT,_bad_interrupt		; reserved

	   IRQVEC INT4,_int4_handler
	   IRQVEC INT5,_int5_handler
	   IRQVEC INT6,_int6_handler
	   IRQVEC INT7,_int7_handler
	   IRQVEC INT8,_int8_handler
	   IRQVEC INT9,_int9_handler
	   IRQVEC INT10,_int10_handler
	   IRQVEC INT11,_int11_handler
	   IRQVEC INT12,_int12_handler
	   IRQVEC INT13,_int13_handler
	   IRQVEC INT14,_int14_handler
	   IRQVEC INT15,_int15_handler
