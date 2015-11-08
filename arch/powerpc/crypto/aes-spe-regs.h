/*
 * Common registers for PPC AES implementation
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#define rKS r0	/* copy of en-/decryption key pointer			*/
#define rDP r3	/* destination pointer					*/
#define rSP r4	/* source pointer					*/
#define rKP r5	/* pointer to en-/decryption key pointer		*/
#define rRR r6	/* en-/decryption rounds				*/
#define rLN r7	/* length of data to be processed			*/
#define rIP r8	/* potiner to IV (CBC/CTR/XTS modes)			*/
#define rKT r9	/* pointer to tweak key (XTS mode)			*/
#define rT0 r11	/* pointers to en-/decrpytion tables			*/
#define rT1 r10
#define rD0 r9	/* data 						*/
#define rD1 r14
#define rD2 r12
#define rD3 r15
#define rW0 r16	/* working registers					*/
#define rW1 r17
#define rW2 r18
#define rW3 r19
#define rW4 r20
#define rW5 r21
#define rW6 r22
#define rW7 r23
#define rI0 r24	/* IV							*/
#define rI1 r25
#define rI2 r26
#define rI3 r27
#define rG0 r28	/* endian reversed tweak (XTS mode)			*/
#define rG1 r29
#define rG2 r30
#define rG3 r31
