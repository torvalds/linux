/*
 * Copyright 2007 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

.align 2
.global ___smulsi3_highpart;
.type ___smulsi3_highpart, STT_FUNC;

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif

___smulsi3_highpart:
	R2 = R1.L * R0.L (FU);
	R3 = R1.H * R0.L (IS,M);
	R0 = R0.H * R1.H, R1 = R0.H * R1.L (IS,M);

	R1.L = R2.H + R1.L;
	cc = ac0;
	R2 = cc;

	R1.L = R1.L + R3.L;
	cc = ac0;
	R1 >>>= 16;
	R3 >>>= 16;
	R1 = R1 + R3;
	R1 = R1 + R2;
	R2 = cc;
	R1 = R1 + R2;

	R0 = R0 + R1;
	RTS;

.size ___smulsi3_highpart, .-___smulsi3_highpart
