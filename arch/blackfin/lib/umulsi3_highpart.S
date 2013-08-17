/*
 * Copyright 2007 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

.align 2
.global ___umulsi3_highpart;
.type ___umulsi3_highpart, STT_FUNC;

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif

___umulsi3_highpart:
	R2 = R1.H * R0.H, R3 = R1.L * R0.H (FU);
	R0 = R1.L * R0.L, R1 = R1.H * R0.L (FU);
	R0 >>= 16;
	/* Unsigned multiplication has the nice property that we can
	   ignore carry on this first addition.  */
	R0 = R0 + R3;
	R0 = R0 + R1;
	cc = ac0;
	R1 = cc;
	R1 = PACK(R1.l,R0.h);
	R0 = R1 + R2;
	RTS;

.size ___umulsi3_highpart, .-___umulsi3_highpart
